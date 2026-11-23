#!/usr/bin/env python3
"""A held game draws a fixed pose whatever the left stick does (photo mode, analog_move and anim_blend on).

    python3 pc/tests/photo_pose_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Photo mode's camera is the stick's, and each camera step is a render: the game run for a vsync from the held moment,
then rewound. The game sees no buttons in a render, but analog_move.c read the left stick directly, so the render
walked Ashley and ran her stride at the stick's pace (anim_acc, the blend's carried fraction): her pose and place changed
with the stick, the camera step after step. Now a render sees the stick as the last live frame read it, and her stride
stays on the tick she was drawn at with the blend fraction she had (pc_analog_anim_steps), so the held picture of her is a
function of the held moment alone. From the save room warped to the Crumbling Market, Ashley mid-stride at 30%
of the stick, held (rood.game.pause), the camera on one step:

- the picture of her (the middle of the frame) with the stick centred, three times over, is the same
- with the stick held at four angles and pushes (RPC input left_stick, held through the renders) and the camera back on
  the same step, the picture of her is that same one, each time
- the game state digest is the held moment's throughout
- shots (--out): the held picture with the stick centred and held (shot_centred.png, shot_stick.png)"""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT  # noqa: E402
from analog_common import Checks, default_card, field_game, stick_at, warp  # noqa: E402
from photo_foundation_smoke import ppm, difference  # noqa: E402

PLUGIN = "rood.photo"
ROOM = (48, 21)
HER = (0.3, 0.2, 0.7, 0.9)  # Ashley and what she stands on: the middle of the frame
SAME = 0.02  # mean difference (0..255) over HER: the same picture (a pose step is several)
STICKS = ((90, 0.3), (200, 0.6), (10, 1.0), (300, 0.45))


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
    out = pathlib.Path(args.out) if args.out else OUT / "photo_pose"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    (out / "settings.cfg").write_text("aspect 4:3\nui_menus 0\n")
    check = Checks()
    env = {"ROOD_WINDOW": "960x720", "ROOD_GI": "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_PLUGINS": str(out / "plugins"), "ROOD_PLUGIN_DATA": str(out / "plugin_data"),
           "ROOD_PHOTOS": str(out / "photos"), "ROOD_PAUSED_REDRAW_HZ": "60"}
    from checkpoints import launch_at
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    try:
        g.run(300)
        for _ in range(2):
            g.press("cross", vsyncs=3, settle=30)
        check(warp(g, *ROOM), f"warped to zone {ROOM[0]} room {ROOM[1]}")
        g.call("settings", apply=["analog_move 1", "analog_dead_zone 20", "analog_pace 1", "analog_curve 100",
                                  "analog_min_speed 10", "analog_anim_rate 100", "anim_blend 100"])
        g.run(30)
        g.call("input", vsyncs=40, left_stick=stick_at(160, 0.3))
        g.run(40)

        def value(code):
            r = g.repl("eval", session="photo-pose", plugin=PLUGIN, code=code)
            if not r.get("ok"):
                raise rood_rpc.RpcError(f"{code}: {r.get('error')}")
            return r["results"][0] if r.get("results") else None

        def shot(name):
            """The held picture after a camera step away and back (a render each)."""
            value("rood.camera.orbit(0.1, 0) return true")
            value("rood.camera.orbit(-0.1, 0) return true")
            g.wait_presents(8)
            path = out / f"{name}.ppm"
            path.unlink(missing_ok=True)
            g.call("present_dump", path=str(path))
            if not rood_rpc.wait_for_file(path, 30):
                raise rood_rpc.RpcError("no frame presented")
            return ppm(path)

        check(value("return rood.game.pause()") is True, "held (rood.game.pause)")
        value("rood.camera.begin() return true")
        digest = g.call("snapshot", action="hash")
        ref = shot("centred")
        for i in (1, 2):
            again = shot(f"centred_{i}")
            check(difference(ref, again, HER) < SAME, f"the stick centred, again: the same picture of her "
                  f"({difference(ref, again, HER):.3f})")
        held = None
        for heading, travel in STICKS:
            g.call("input", vsyncs=3000, left_stick=stick_at(heading, travel))
            held = shot("stick")
            d = difference(ref, held, HER)
            check(d < SAME, f"the stick held at {travel:.0%} toward {heading} degrees: the same picture of her ({d:.3f})")
        g.call("input", vsyncs=1, left_stick=[0, 0])
        check(g.call("snapshot", action="hash") == digest, "the game state digest is the held moment's")
        if args.out:
            g.call("present_dump", path=str(out / "last.ppm"))
            for name in ("centred", "stick"):
                subprocess = __import__("subprocess")
                subprocess.run(["convert", str(out / f"{name}.ppm"), str(out / f"shot_{name}.png")], check=False,
                               stderr=subprocess.DEVNULL)
        value("rood.game.resume() return true")
    finally:
        g.close()
    return check.finish("photo_pose_smoke")


if __name__ == "__main__":
    sys.exit(main())
