#!/usr/bin/env python3
"""16:9: 2D that is part of the 3D view fills the view's sides.

    python3 pc/tests/widescreen_scene_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Widescreen (Hor+) narrows the GTE's projections, so the room, characters and
effects the game projects already fill the 16:9 view (the room renderer does
not cull by screen position: a census of every room polygon in the opening's
cutscenes and the town's found none dropped for being outside the 4:3 frame;
where the sides show black or sky, the modelled set ends there). Some of the
3D view is 2D, though, and was drawn compressed into the 4:3 middle and
clipped there:

- the sky panorama (BATTLE 2842C.c func_8009147C: 32x32 tiles of a 1024-wide
  picture, scrolled with the camera's yaw and pitch) of outdoor scenes such
  as the opening's view of the manor: its tiles now fill the view's sides
  (PC_ANCHOR_SCENE: compressed like the narrowed 3D, clipped to the whole
  view), and the game draws them as far past the 4:3 sides as the view
  reaches (SKY_LEFT/SKY_RIGHT);
- weather particles (146C.c func_8008F440..func_80090434): Snowfly Forest's
  floating motes are screen-space 2D over the whole screen (PC_ANCHOR_COVER:
  drawn over the whole view); snow and falling glints are 2D at un-narrowed
  projections accepted over the 16:9 view's X range (WEATHER_LEFT/RIGHT).

One session each; RPC graphics widescreen_scene off is the control (the old
placement):

- New Game, through the opening's dialogue to the manor's exterior (zone 2
  room 0) while the sky panorama is drawn (D_800F1DC9, D_800F1DCC): the top
  half of the side bands (outside the 4:3 middle) is sky with the fix and
  black in the control, alternately on the same shot;
- with the card: Snowfly Forest (zone 40 room 0, RPC room_goto): pixels that
  change over a few frames (the motes; the field camera holds still) in the
  side bands: none in the control, many with the fix.

4:3 is untouched (the anchors and widened ranges only act in widescreen):
pc/tests/golden_frames.py covers it. --out keeps frames (game imagery: keep
it out of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

GRAPHICS = {"widescreen": True, "interpolate": False, "aa": False, "gi": False}
OPENING_LIMIT = 12000  # vsyncs after New Game (the manor's exterior is near 6000)


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


class Session:
    def __init__(self, out, name, card=None):
        self.out = out / name
        self.out.mkdir(parents=True, exist_ok=True)
        env = {"ROOD_WINDOW": "960x540", "ROOD_SCALE": "1", "ROOD_GI": "0", "ROOD_LIGHTS": str(self.out / "lights"),
               "ROOD_SETTINGS": str(self.out / "settings.cfg"), "ROOD_WIDESCREEN": "1"}
        if card:
            shutil.copyfile(card, self.out / "card1.mcd")
            card = str(self.out / "card1.mcd")
        else:
            env["ROOD_CARD1"] = str(self.out / "absent-card1.mcd")
        socket = f"/tmp/rood-wscene-{name}-{os.getpid()}.sock"
        self.game = Game.launch(fast=True, card=card, log=self.out / "game.log", extra_env=env, socket_path=socket)

    def frame(self, name):
        path = self.game.present_screenshot(self.out / f"{name}.png")
        return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)

    def scene(self, on):
        self.game.call("graphics", widescreen_scene=on)
        self.game.run(8)  # the next tables are built and shown with it

    def close(self):
        self.game.close()


def sky_sides(image):
    """Share of the top half of the side bands (the view's outer eighths,
    outside the 4:3 middle) that is not black."""
    h, w, _ = image.shape
    b = w // 8
    top = image[: h // 2]
    sides = np.concatenate([top[:, :b], top[:, w - b:]], axis=1)
    return float((sides.max(axis=2) > 2).mean())


def opening(out, check):
    s = Session(out, "opening")
    g = s.game
    try:
        g.wait_title()
        g.call("graphics", **GRAPHICS)
        g.press("start", vsyncs=4, settle=60)  # New Game (no card)
        start = g.status()["vsync"]
        found = False
        steps = 0
        while g.status()["vsync"] - start < OPENING_LIMIT:
            g.run(50)
            steps += 1
            st = g.state()
            sky = g.read_symbol("D_800F1DC9", 1)[0] and int.from_bytes(g.read_symbol("D_800F1DCC", 4), "little")
            if (st["zone"], st["room"]) == (2, 0) and sky:
                found = True
                break
            if steps % 4 == 0:
                g.press("circle", vsyncs=2, settle=4)  # the dialogue
        check(found, f"the opening reaches the manor's exterior with its sky panorama "
                     f"(vsync {g.status()['vsync'] - start} after New Game)")
        if not found:
            return
        g.run(30)
        rows = []
        for k in range(3):
            s.scene(False)
            off = sky_sides(s.frame(f"sky-{k}-control"))
            s.scene(True)
            on = sky_sides(s.frame(f"sky-{k}-fix"))
            rows.append((off, on))
            print(f"  shot {k}: sky in the side bands' top half {off:.3f} (control) -> {on:.3f} (fix)")
            g.run(40)
        # (Where the manor itself reaches the sides, both are lit.)
        check(any(off < 0.6 for off, _ in rows), "the control leaves sky in the sides black (the measure is live)")
        check(all(on >= off - 0.02 for off, on in rows) and all(on > 0.8 for off, on in rows)
              and any(on > off + 0.3 for off, on in rows), "the sky panorama fills the side bands with the fix")
    finally:
        s.close()


def forest(out, card, check):
    s = Session(out, "forest", card=card)
    g = s.game
    try:
        g.wait_title()
        g.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            g.press("circle", vsyncs=3, settle=90)
        g.run(400)
        g.call("graphics", **GRAPHICS)
        g.call("room_goto", zone=40, room=0)
        for _ in range(60):
            g.run(20)
            if g.call("room_goto", action="status")["phase"] in (0, 4):
                break
        g.run(60)
        st = g.state()
        weather = g.read_symbol("D_800F1DCA", 1)[0]
        check((st["zone"], st["room"]) == (40, 0) and weather == 3,
              f"Snowfly Forest (zone {st['zone']} room {st['room']}), its motes on (weather {weather})")
        counts = {}
        for on in (False, True):
            s.scene(on)
            frames = []
            for k in range(8):
                frames.append(s.frame(f"motes-{'fix' if on else 'control'}-{k}").mean(axis=2))
                g.run(3)
            frames = np.array(frames)
            moving = (frames - frames.min(axis=0)) > 20
            w = frames.shape[2]
            b = w // 10  # (the HUD's changing digits are at the left: the right band)
            counts[on] = int(moving[:, :, w - b:].sum())
            centre = int(moving[:, :, w // 2 - b: w // 2].sum())
            print(f"  {'fix' if on else 'control'}: moving pixels in the right band {counts[on]}, "
                  f"in as wide a band in the middle {centre}")
        check(counts[False] < 20, "the control has no motes in the side band")
        check(counts[True] > 150, "the motes fill the side band with the fix")
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(default_card()))
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "widescreen_scene"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    opening(out, check)
    if pathlib.Path(args.card).exists():
        forest(out, args.card, check)
    else:
        print("note: no memory card; Snowfly Forest's motes skipped")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS: 16:9 sky panorama and weather fill the view's sides")
    return 0


if __name__ == "__main__":
    sys.exit(main())
