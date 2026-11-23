#!/usr/bin/env python3
"""Photo mode hides save points (rood.ui.hide{savepoints}; the card's Show save points row).

    python3 pc/tests/photo_savepoint_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

A save point is part of the room: its column and sigil are the room's polygons with a scrolling material
(room_native.c pc_room_savepoint_hidden), and the room's generated light for them (glows.c). From the card's save room,
held (rood.game.pause), the HUD and the game's 2D hidden, the camera on one step:

- shown (the default, savepoints off in rood.ui.hide): the save point is in the picture
- rood.ui.hide{savepoints = true}: the picture differs where it stood (the column and sigil are gone), and the rest of
  the room does not (the room's far sides are the same)
- rood.ui.hidden().savepoints reports it; rood.ui.show() / a hide without it brings the save point back: the same
  picture as shown at first
- the game state digest is the held moment's throughout
- shots (--out): shown.png, hidden.png"""
import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT  # noqa: E402
from analog_common import Checks, default_card  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from photo_foundation_smoke import ppm, difference  # noqa: E402

PLUGIN = "rood.photo"
SAVE = (0.2, 0.25, 0.8, 0.9)  # where the save point stands in the save room's picture (the column is the bright part)
FAR = (0.0, 0.0, 1.0, 0.12)  # the top of the picture: the room's far wall


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "null":
        print("SKIP: the photo mode needs a renderer")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "photo_savepoint"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    (out / "settings.cfg").write_text("aspect 4:3\nui_menus 0\n")
    check = Checks()
    env = {"ROOD_WINDOW": "960x720", "ROOD_GI": "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_PLUGINS": str(out / "plugins"), "ROOD_PLUGIN_DATA": str(out / "plugin_data"),
           "ROOD_PHOTOS": str(out / "photos"), "ROOD_PAUSED_REDRAW_HZ": "60"}
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        g.run(300)

        def value(code):
            r = g.repl("eval", session="photo-savepoint", plugin=PLUGIN, code=code)
            if not r.get("ok"):
                raise rood_rpc.RpcError(f"{code}: {r.get('error')}")
            return r["results"][0] if r.get("results") else None

        def shot(name):
            g.wait_presents(8)
            path = out / f"{name}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            return ppm(path)

        check(value("return rood.game.pause()") is True, "held (rood.game.pause)")
        digest = g.call("snapshot", action="hash")
        value("rood.camera.begin() rood.camera.orbit(0.3, 0) return true")
        value("return rood.ui.hide { hud = true, game2d = true }")
        check(value("return rood.ui.hidden().savepoints") is False, "rood.ui.hidden().savepoints: off by default")
        shown = shot("shown")
        value("return rood.ui.hide { hud = true, game2d = true, savepoints = true }")
        check(value("return rood.ui.hidden().savepoints") is True, "rood.ui.hidden().savepoints: on")
        hidden = shot("hidden")
        d_save, d_far = difference(shown, hidden, SAVE), difference(shown, hidden, FAR)
        check(d_save > 0.5, f"save points hidden: the picture changes where it stood ({d_save:.2f})")
        check(d_far < 0.05, f"... and not at the room's far wall ({d_far:.3f})")
        value("return rood.ui.hide { hud = true, game2d = true }")
        again = shot("shown_again")
        check(difference(shown, again) < 0.05, f"shown again: the same picture ({difference(shown, again):.3f})")
        check(g.call("snapshot", action="hash") == digest, "the game state digest is the held moment's")
        value("rood.ui.show() rood.game.resume() return true")
        if args.out:
            import subprocess
            for name in ("shown", "hidden"):
                subprocess.run(["convert", str(out / f"{name}.ppm"), str(out / f"{name}.png")], check=False,
                               stderr=subprocess.DEVNULL)
    finally:
        g.close()
    return check.finish("photo_savepoint_smoke")


if __name__ == "__main__":
    sys.exit(main())
