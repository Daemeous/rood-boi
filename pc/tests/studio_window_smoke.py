#!/usr/bin/env python3
"""The model studio as the owner uses it: the editor open, the game running live (not paused by RPC), Window > Studio docked
short and wide (pc/src/studio.cpp; the lead and replay in pc/src/photo.cpp).

    python3 pc/tests/studio_window_smoke.py [--card ...] [--out DIR]

The studio_smoke holds a game the RPC paused, which a photo hold renders from M for several frames. A live game's hold is led
(its last frames replayed to M), and the render's scene (the stage, the model) was built only for the last replayed frame, which
did not show it: the Game panel kept the live room. Checked here, in the editor's composed picture (present_dump):
  - before: the game is running, not paused; the Studio window is open and drawn, its Enter button visible, the window does
    not scroll (the controls and the catalog are columns that scroll on their own);
  - entered: the Leave button is visible and the window does not scroll; in the flat look the Game panel's picture is the
    stage's grey (95) away from the model and a summoned character is on it, and it is not the room;
  - left: the photo mode holds nothing, the game runs on, and the Game panel shows the room again (as before entering)."""
import argparse
import os
import pathlib
import sys
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT, Deadline  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402
from photo_foundation_smoke import ppm  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the studio's renders need a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "studio_window"
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        return run(out, cards.card1)


def run(out, card):
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    settings = out / "settings.cfg"
    settings.write_text("aspect 4:3\nui_menus 0\nlight_animation 0\ndust 0\naa off\n")
    (out / "lights").mkdir(exist_ok=True)
    env = {"ROOD_WINDOW": "1600x900", "ROOD_SETTINGS": str(settings), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTS": str(out / "lights"), "ROOD_STUDIO": str(out / "studio.json")}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        def studio(**params):
            return g.call("studio", **params)

        def shot(name):
            path = out / f"{name}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            w, h, data = ppm(path)
            img = np.frombuffer(data, np.uint8).reshape(h, w, 3)
            Image.fromarray(img).save(out / f"{name}.png")
            path.unlink()
            return img

        def game_rect():
            """The picture's visible rectangle (x, y, w, h), top-left origin: the view (bottom-left origin) inside the panel."""
            sh = g.call("editor_shell")
            x, y, w, h = sh["view"]
            y = 900 - y - h
            px, py, pw, ph = sh["game"]["rect"]  # the panel clips the picture
            x0, y0, x1, y1 = max(x, px), max(y, py), min(x + w, px + pw), min(y + h, py + ph)
            return int(x0) + 4, int(y0) + 4, int(x1 - x0) - 8, int(y1 - y0) - 8

        g.call("editor_ui", visible=True)
        g.run(10)
        g.pause(False)  # the game runs live, as in the window
        time.sleep(2)
        check(not g.status().get("paused"), "the game runs (not paused by RPC)")
        g.call("editor_command", action="run", id="window.studio")
        deadline = Deadline(20)
        while not studio()["window"]["drawn"] and not deadline.passed():
            time.sleep(0.05)
        w = studio()["window"]
        check(w["drawn"], "the Studio window is drawn")
        check(w["leave_visible"] and not w["scrolls"],
              f"before entering: the Enter button is in view and the window does not scroll ({w})")
        before = shot("before")

        studio(action="look", name="flat")
        check(not studio(action="enter").get("error"), "enter")
        studio(action="summon", kind="character", id=3)
        deadline = Deadline(60)
        while True:
            s = studio()
            if s["entered"] and s["settled"] and s["framed"] and g.call("photo")["phase"] == "held":
                break
            if deadline.passed():
                check(False, f"not settled ({s})")
                return 1
            time.sleep(0.05)
        g.wait_presents(4)
        w = studio()["window"]
        check(w["leave_shown"] and w["leave_visible"], f"in the studio: Leave is in view ({w['leave']})")
        check(not w["scrolls"], "in the studio: the window itself does not scroll")
        check(w["controls"][2] > 100 and w["catalog"][2] > 100 and w["controls"][1] == w["catalog"][1],
              f"the controls and the catalog are side by side ({w['controls']}, {w['catalog']})")
        inside = shot("entered")
        x, y, gw, gh = game_rect()
        panel = inside[y:y + gh, x:x + gw].astype(int)
        strip = panel[gh // 8: gh * 7 // 8, : max(gw // 8, 4)]  # the picture's left edge: stage, away from the model
        check(np.abs(strip - 95).max() <= 1, f"the Game panel shows the stage's grey away from the model (flat look; {strip.min()}..{strip.max()})")
        check(float((np.abs(panel - 95).max(axis=2) > 8).mean()) > 0.003, "a model stands on the stage in the Game panel")
        room = before[y:y + gh, x:x + gw].astype(int)
        check(float(np.abs(panel - room).mean()) > 10, "the Game panel is not the live room")

        studio(action="leave")
        deadline = Deadline(30)
        while (g.call("photo")["phase"] != "running" or studio()["entered"]) and not deadline.passed():
            time.sleep(0.05)
        check(g.call("photo")["phase"] == "running", f"the game runs again after leaving ({g.call('photo')['phase']})")
        time.sleep(1)
        after = shot("left")
        panel2 = after[y:y + gh, x:x + gw].astype(int)
        check(float(np.abs(panel2 - 95).max(axis=2).mean()) > 10 and float(np.abs(panel2 - room).mean()) < 25,
              f"the Game panel shows the room again (differs from the room by {np.abs(panel2 - room).mean():.1f})")
        return 1 if failures else 0
    finally:
        g.close()


if __name__ == "__main__":
    sys.exit(main())
