#!/usr/bin/env python3
"""Legacy vs native room frames in the rooms of a save (side by side).

    python3 pc/tests/native_room_tour.py [--card pc/save/card1.mcd] [--rooms 4] [--out DIR]

Continues from a copy of the card (never the card itself), takes control,
and in each room captures the game camera and two free-camera views around
Ashley (high overview, low from behind) with graphics native_room 0 and
then 1 (the same process: the game keeps running between the two, so idle
animation differs slightly), then walks to look for the next room. Writes
<room>-<view>-pair.png (legacy left, native right) and prints the
render_stats counters and frame differences. For looking, not pass/fail:
frames are game imagery, keep them out of git.

--switch KEY compares another graphics switch the same way (native_shadows,
native_effects, native_blend, ...; 0 then --mode), --graphics adds settings
to both (e.g. '{"drop_shadows": 1}' for the original drop shadows); a close
view of Ashley's feet (drop shadows) is added."""
import json
import argparse
import math
import os
import pathlib
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import Game  # noqa: E402
import golden_frames as gf  # noqa: E402


def player(game):
    st = game.state()
    return st, st.get("player")


SWITCH = "native_room"
GRAPHICS = {}


def capture_pair(game, out, name, mode, pose):
    images = []
    for native in (0, mode):
        game.call("graphics", **{**gf.DEFAULTS, **GRAPHICS, SWITCH: native})
        if pose:
            game.call("editor_camera", **pose)
        else:
            game.call("editor_camera", off=1)
        game.run(gf.SETTLE)
        images.append(gf.capture(game, out / f"{name}-{native}.png"))
        if native:
            stats = game.call("render_stats")
    a, b = images
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
    print(f"{name}: mean {diff.mean():.2f}, {(diff.max(axis=2) > 32).mean() * 100:.2f}% strong; native {stats}")
    Image.fromarray(np.concatenate([a, b], axis=1)).save(out / f"{name}-pair.png")


def new_game_tour(args, out):
    """The opening's cutscenes pass through several rooms."""
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": gf.WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4"}
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(fast=True, log=out / "game.log", extra_env=env)
    seen = set()
    try:
        game.wait_title()
        game.call("graphics", **{**gf.DEFAULTS, **GRAPHICS})
        game.press("start", vsyncs=4, settle=60)
        start = game.status()["vsync"]
        while game.status()["vsync"] - start < args.new_game:
            game.run(200)
            st, p = player(game)
            room = (st["zone"], st["room"])
            if room in seen or not game.call("scene")["triangles"]:
                continue
            seen.add(room)
            name = f"zone{room[0]:02d}-room{room[1]:02d}"
            capture_pair(game, out, name + "-game", args.mode, None)
            if p:
                capture_pair(game, out, name + "-high", args.mode,
                             dict(x=p["x"], y=p["y"] - 1600, z=p["z"] - 900, yaw=0, pitch=-55))
            game.call("editor_camera", off=1)
            game.call("graphics", **{**gf.DEFAULTS, **GRAPHICS})
            game.press("circle", vsyncs=2, settle=4)
        print("rooms:", sorted(seen))
    finally:
        game.close()
    for line in (out / "game.log").read_text(errors="replace").splitlines():
        if "[render]" in line or "[crash]" in line or ("[gl]" in line and "shader" in line):
            print(line)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    parser.add_argument("--rooms", type=int, default=4)
    parser.add_argument("--mode", type=int, default=1)
    parser.add_argument("--switch", default="native_room", help="graphics switch compared (0 vs --mode)")
    parser.add_argument("--graphics", default="{}", help="extra graphics settings for both (JSON)")
    parser.add_argument("--out", default=str(ROOT / "pc/build/tests/native_room_tour"))
    parser.add_argument("--new-game", type=int, default=0, metavar="VSYNCS",
                        help="instead of the card: New Game, checking for a new room every 200 vsyncs up to VSYNCS")
    args = parser.parse_args()
    global SWITCH, GRAPHICS
    SWITCH, GRAPHICS = args.switch, json.loads(args.graphics)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    if args.new_game:
        return new_game_tour(args, out)
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    card = out / "card1.mcd"
    shutil.copy(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": gf.WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4"}
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    try:
        game.wait_title()
        game.call("graphics", **{**gf.DEFAULTS, **GRAPHICS})
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        seen = set()
        directions = ["up", "left", "down", "right"]
        turn = 0
        for _ in range(args.rooms * 6):
            st, p = player(game)
            room = (st["zone"], st["room"])
            if p and room not in seen:
                seen.add(room)
                name = f"zone{room[0]:02d}-room{room[1]:02d}"
                yaw = p["facing"] * 360.0 / 4096.0 if "facing" in p else 0
                behind = (p["x"] - 600 * math.sin(math.radians(yaw)), p["y"] - 300,
                          p["z"] - 600 * math.cos(math.radians(yaw)))
                capture_pair(game, out, name + "-game", args.mode, None)
                capture_pair(game, out, name + "-high", args.mode,
                             dict(x=p["x"], y=p["y"] - 1600, z=p["z"] - 900, yaw=0, pitch=-55))
                capture_pair(game, out, name + "-low", args.mode,
                             dict(x=behind[0], y=behind[1], z=behind[2], yaw=yaw, pitch=-5))
                capture_pair(game, out, name + "-feet", args.mode,
                             dict(x=p["x"], y=p["y"] - 700, z=p["z"] - 450, yaw=0, pitch=-50))
                game.call("editor_camera", off=1)
                game.call("graphics", **{**gf.DEFAULTS, **GRAPHICS})
                if len(seen) >= args.rooms:
                    break
            # Look for a door: walk one way for a while, then turn.
            game.hold(directions[turn % 4], 240)
            game.run(250)
            st2, p2 = player(game)
            if (st2["zone"], st2["room"]) == room:
                turn += 1
            game.press("cross", vsyncs=3, settle=10)  # leave battle mode / dialogue
        print("rooms:", sorted(seen))
    finally:
        game.close()
    for line in (out / "game.log").read_text(errors="replace").splitlines():
        if "[render]" in line or "[crash]" in line or ("[gl]" in line and "shader" in line):
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
