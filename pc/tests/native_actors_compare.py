#!/usr/bin/env python3
"""Side-by-side frames of the legacy (packet) and native (mesh) character paths.

    python3 pc/tests/native_actors_compare.py [--out DIR] [--times 1500,1900] [--blend] [--fade]

New Game as golden_frames.py does (vsync-exact), then at each of `--times`
(vsyncs after New Game) captures the presented window for the game camera and
a few editor_camera poses, with graphics native_actors 0 and then 1 (the room
is native in both), plus the depth view and a run of frames with frame
interpolation on (every other presented frame is a replayed in-between frame:
native characters blend bone palettes there, legacy ones match triangles).
Writes <name>-legacy.png, <name>-native.png, <name>-diff.png and a side by
side <name>-pair.png, and prints the mean difference, the share of strongly
different pixels and the render_stats counters. Not a pass/fail suite: it is
for looking (frames are game imagery, keep them out of git).

--blend compares graphics native_blend 0 and 1 instead (characters native in
both: their semi-transparent polygons and blended models from packets or at
translucent markers). --fade adds blended characters to the first room: it
forces the blended builders (D_800E8FC0, the battle camera transition flag:
func_800B516C/func_800B5C30 for every actor) and sets the other actors' blend
mode (object byte 9: 1-6 B-F, 7+ B+F; the game then animates it as a fade),
then captures fixed-vsync frames from the game camera and a close pose, plus
interpolated frames.

--shadows compares graphics native_shadows 0 and 1 (drop shadows from packets
or native world-space triangles; everything else native in both), with
poses looking down at the characters' feet. --effects compares
native_effects 0 and 1 (particles from packets or native sprites); with
--spell N the player casts spell N first (RPC "cast", see dev_rpc.c) and the
captures follow its effect."""
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
    ("high", (768, -1450, 300, 0, -40)),
    ("low-close", (900, -300, 700, 90, 5)),
    # free camera on the characters (Ashley and the guard in the first room)
    ("actors-front", (1148, -900, 900, 270, -20)),
    ("actors-side", (1148, -900, 300, 270, -20)),
    ("actors-close", (1000, -850, 800, 250, -20)),
    # looking down at the characters' feet (drop shadows)
    ("feet-high", (1000, -1150, 700, 250, -45)),
    ("feet-guard", (1250, -1250, 450, 250, -60)),
]


def defaults(args):
    # --shadows: the original drop shadows (Auto turns them off or makes them
    # contact-only when traced shadows exist).
    shadows = {"drop_shadows": 1} if args.shadows else {}
    return {**gf.DEFAULTS, "widescreen": args.widescreen, **shadows, **json.loads(args.graphics)}


ACTORS = 0x800F4538  # D_800F4538: the battle actors (pointers)
BLENDED_BUILDERS = 0x800E8FC0  # D_800E8FC0: every actor through the blended builders


def fade_scene(game, out, mode, shots, stats, args):
    """Blended characters (see --fade), at fixed vsyncs (the fade animates)."""
    def read(address, length):
        r = game.call("read", address=address, length=length)
        return bytes.fromhex(r["hex"] if isinstance(r, dict) else r)
    pointers = read(ACTORS, 16)
    actors = [int.from_bytes(pointers[i:i + 4], "little") for i in range(4, 16, 4)]
    game.call("write", address=BLENDED_BUILDERS, hex="01")
    for blend in (3, 8):  # B-F, B+F
        for a in actors:
            if a:
                b = read(a + 9, 1)[0]
                game.call("write", address=a + 9, hex=f"{(b & 0xf0) | blend:02x}")
        for name, pose in (("game-camera", None), ("actors-close", (1000, -850, 800, 250, -20))):
            if pose:
                x, y, z, yaw, pitch = pose
                game.call("editor_camera", x=x, y=y, z=z, yaw=yaw, pitch=pitch)
            else:
                game.call("editor_camera", off=1)
            game.run(4)
            key = f"fade{blend}-{name}"
            shots[key] = gf.capture(game, out / f"{key}-{mode}.png")
            stats[key] = game.call("render_stats")
        game.call("editor_camera", off=1)
    game.call("graphics", **{**defaults(args), "interpolate": True})
    game.run(4)
    for i in range(4):
        shots[f"fade-interpolated{i}"] = gf.capture(game, out / f"fade-interpolated{i}-{mode}.png")
    stats["fade-interpolated"] = game.call("render_stats")
    game.call("graphics", **defaults(args))
    game.call("write", address=BLENDED_BUILDERS, hex="00")


def capture_all(args, out, mode):
    """One deterministic run with graphics native_actors `mode` (native_blend
    `mode` with --blend)."""
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": gf.WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4",
           "ROOD_NATIVE_ROOM": "1", "ROOD_NATIVE_ACTORS": str(mode)}
    if args.blend:
        env.update(ROOD_NATIVE_ACTORS="1", ROOD_NATIVE_BLEND=str(mode))
    if args.shadows:
        env.update(ROOD_NATIVE_ACTORS="1", ROOD_NATIVE_SHADOWS=str(mode))
    if args.effects:
        env.update(ROOD_NATIVE_ACTORS="1", ROOD_NATIVE_EFFECTS=str(mode))
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(fast=True, log=out / f"game-{mode}.log", extra_env=env,
                       socket_path=f"/tmp/rood-actors-{os.getpid()}-{mode}.sock")  # sun_path: 108 bytes
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
            for name, pose in POSES:
                for depth in (False, True):
                    if depth and pose is None:
                        continue
                    game.call("graphics", **{**defaults(args), "show_depth": depth})
                    if pose:
                        x, y, z, yaw, pitch = pose
                        game.call("editor_camera", x=x, y=y, z=z, yaw=yaw, pitch=pitch)
                    else:
                        game.call("editor_camera", off=1)
                    game.run(gf.SETTLE)
                    key = f"t{when}-{name}" + ("-depth" if depth else "")
                    shots[key] = gf.settled(game, out / f"{key}-{mode}.png")
                    if not depth:
                        stats[key] = game.call("render_stats")
            game.call("editor_camera", off=1)
            game.call("graphics", **defaults(args))
        if args.fade:
            fade_scene(game, out, mode, shots, stats, args)
        # Frame interpolation (the game camera and the characters move).
        game.call("graphics", **{**defaults(args), "interpolate": True})
        game.run(gf.SETTLE)
        for i in range(args.interpolated):
            shots[f"interpolated{i}"] = gf.capture(game, out / f"interpolated{i}-{mode}.png")
        stats["interpolated"] = game.call("render_stats")
        game.call("graphics", **defaults(args))
    finally:
        game.close()
    return shots, stats


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "pc/build/tests/native_actors"))
    parser.add_argument("--times", default=f"{gf.FIRST_ROOM},1900",
                        help="vsyncs after New Game to capture at (comma separated)")
    parser.add_argument("--interpolated", type=int, default=6, help="frames captured with interpolation on")
    parser.add_argument("--blend", action="store_true",
                        help="compare native_blend 0 and 1 (characters native in both) instead of native_actors")
    parser.add_argument("--fade", action="store_true", help="add the forced blended characters scene")
    parser.add_argument("--shadows", action="store_true",
                        help="compare native_shadows 0 and 1 (drop shadows) instead of native_actors")
    parser.add_argument("--effects", action="store_true",
                        help="compare native_effects 0 and 1 (particles) instead of native_actors")
    parser.add_argument("--widescreen", action="store_true", help="16:9 Hor+")
    parser.add_argument("--graphics", default="{}", help="extra graphics settings (JSON), e.g. view modes")
    args = parser.parse_args()
    args.times = [int(t) for t in args.times.split(",")]
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    legacy, legacy_stats = capture_all(args, out, 0)
    native, native_stats = capture_all(args, out, 1)
    for key in legacy:
        a, b = legacy[key], native[key]
        Image.fromarray(a).save(out / f"{key}-legacy.png")
        Image.fromarray(b).save(out / f"{key}-native.png")
        diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
        share = float((diff.max(axis=2) > 32).mean())
        print(f"{key}: mean {diff.mean():.2f}, {share * 100:.2f}% strong")
        Image.fromarray(np.clip(diff * 4, 0, 255).astype(np.uint8)).save(out / f"{key}-diff.png")
        Image.fromarray(np.concatenate([a, b], axis=1)).save(out / f"{key}-pair.png")
        if key in legacy_stats:
            print(f"  legacy {legacy_stats[key]}")
            print(f"  native {native_stats[key]}")
    if "interpolated" in native_stats:
        print(f"interpolated: legacy {legacy_stats['interpolated']}")
        print(f"interpolated: native {native_stats['interpolated']}")
    for mode in (0, 1):
        for line in (out / f"game-{mode}.log").read_text(errors="replace").splitlines():
            if "[render]" in line or "[crash]" in line or ("[gl]" in line and "shader" in line):
                print(f"[{mode}] {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
