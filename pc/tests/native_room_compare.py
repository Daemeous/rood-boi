#!/usr/bin/env python3
"""Side-by-side frames of the legacy (packet) and native (mesh) room paths.

    python3 pc/tests/native_room_compare.py [--out DIR] [--mode 1|2] [--blend] [--times 1500]

New Game as golden_frames.py does (vsync-exact), then for the game camera and
a few editor_camera poses in the first room captures the presented window
with graphics native_room 0 and then `--mode`, plus the depth view of each,
writes <name>-legacy.png, <name>-native.png, <name>-diff.png and a side by
side <name>-pair.png, and prints the mean difference and the render_stats
counters. With --blend both runs draw the room natively and the comparison
is graphics native_blend 0 (semi-transparent room polygons from packets) vs
1 (native at translucent markers). Not a pass/fail suite: it is for looking
(frames are game imagery, keep them out of git)."""
import argparse
import json
import os
import pathlib
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import Game  # noqa: E402
import golden_frames as gf  # noqa: E402

POSES = [
    ("game-camera", None),
    ("yaw180", (768, -900, 836, 180, -20)),
    ("yaw270", (768, -900, 836, 270, -20)),
    ("high", (768, -1450, 300, 0, -40)),
    ("low-close", (900, -300, 700, 90, 5)),
]


def defaults(args):
    return {**gf.DEFAULTS, "widescreen": args.widescreen, **json.loads(args.graphics)}


def capture_all(args, out, mode):
    """One deterministic run (New Game on a fixed vsync phase) with
    graphics native_room `mode` (native_blend `mode` with --blend):
    {name: image}, {name: render_stats}."""
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": gf.WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4"}
    if args.blend:
        env.update(ROOD_NATIVE_ROOM="1", ROOD_NATIVE_BLEND=str(mode))
    else:
        env.update(ROOD_NATIVE_ROOM=str(mode))
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(fast=True, log=out / f"game-{mode}.log", extra_env=env,
                       socket_path=f"/tmp/rood-room-{os.getpid()}-{mode}.sock")  # sun_path: 108 bytes
    shots, stats = {}, {}
    try:
        game.wait_title()
        game.call("graphics", **defaults(args))
        vsync = game.status()["vsync"]
        game.run(8 - vsync % 8)
        start = game.status()["vsync"]
        game.press("start", vsyncs=4, settle=0)
        for when in args.times:
            game.run(start + when - game.status()["vsync"])
            prefix = f"t{when}-" if len(args.times) > 1 else ""
            for name, pose in POSES:
                for depth in (False, True):
                    game.call("graphics", **{**defaults(args), "show_depth": depth})
                    if pose:
                        x, y, z, yaw, pitch = pose
                        game.call("editor_camera", x=x, y=y, z=z, yaw=yaw, pitch=pitch)
                    else:
                        game.call("editor_camera", off=1)
                    game.run(gf.SETTLE)
                    key = prefix + name + ("-depth" if depth else "")
                    shots[key] = gf.settled(game, out / f"{key}-{mode}.png")
                    if not depth:
                        stats[prefix + name] = game.call("render_stats")
            game.call("editor_camera", off=1)
            game.call("graphics", **defaults(args))
        # Frame interpolation under the moving game camera: consecutive
        # presented frames (every other one is a replayed in-between frame).
        game.call("graphics", **{**defaults(args), "interpolate": True})
        game.run(gf.SETTLE)
        for i in range(args.interpolated):
            shots[f"interpolated{i}"] = gf.capture(game, out / f"interpolated{i}-{mode}.png")
        game.call("graphics", **defaults(args))
    finally:
        game.close()
    return shots, stats


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "pc/build/tests/native_room"))
    parser.add_argument("--mode", type=int, default=1)
    parser.add_argument("--times", default=str(gf.FIRST_ROOM),
                        help="vsyncs after New Game to capture at (comma separated)")
    parser.add_argument("--blend", action="store_true",
                        help="compare native_blend 0 and 1 (native room in both) instead of native_room")
    parser.add_argument("--interpolated", type=int, default=4, help="frames captured with interpolation on")
    parser.add_argument("--widescreen", action="store_true", help="16:9 Hor+")
    parser.add_argument("--graphics", default="{}", help="extra graphics settings (JSON), e.g. view modes")
    args = parser.parse_args()
    args.times = [int(t) for t in args.times.split(",")]
    if args.blend:
        args.mode = 1
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    legacy, legacy_stats = capture_all(args, out, 0)
    native, native_stats = capture_all(args, out, args.mode)
    for key in legacy:
        a, b = legacy[key], native[key]
        Image.fromarray(a).save(out / f"{key}-legacy.png")
        Image.fromarray(b).save(out / f"{key}-native.png")
        diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
        share = float((diff.max(axis=2) > 32).mean())
        print(f"{key}: mean {diff.mean():.2f}, {share * 100:.2f}% strong")
        Image.fromarray(np.clip(diff * 4, 0, 255).astype(np.uint8)).save(out / f"{key}-diff.png")
        Image.fromarray(np.concatenate([a, b], axis=1)).save(out / f"{key}-pair.png")
        name = key.removesuffix("-depth")
        if key == name and name in legacy_stats:
            print(f"  legacy {legacy_stats[name]}")
            print(f"  native {native_stats[name]}")
    for mode in (0, args.mode):
        for line in (out / f"game-{mode}.log").read_text(errors="replace").splitlines():
            if "[render]" in line or "[crash]" in line or ("[gl]" in line and "shader" in line):
                print(f"[{mode}] {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
