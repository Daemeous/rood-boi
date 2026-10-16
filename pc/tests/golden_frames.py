#!/usr/bin/env python3
"""Golden frames: render fixed scenes and camera poses and compare the
presented window images with reference images, so renderer changes that alter
the picture are caught without a human looking.

    python3 pc/tests/golden_frames.py            compare with pc/build/golden/
    python3 pc/tests/golden_frames.py --update   (re)write the references
    python3 pc/tests/golden_frames.py --only room-yaw180

The references contain game imagery, so they live in pc/build/golden/<gpu>/
(ignored by git, generated locally with --update; one set per OpenGL
renderer, as GPUs and llvmpipe rasterise differently) and must never be
committed. Without references the test exits 77 (skipped).

Scenes come from New Game with RPC-exact stepping (no wall-clock input):
the opening card, then the first room from the game's camera and from fixed
editor_camera poses, plus graphics view modes (depth view, 1x scale with
nearest presentation, path-traced GI). Frame interpolation is off (it is
time-based), lights come from an empty ROOD_LIGHTS directory and the window
size is fixed. Each capture is taken twice to check that the frame is settled.

Comparison: per-channel absolute differences; a frame fails when its mean
difference or its share of strongly different pixels exceeds the frame's
tolerance (GI is noisier). Failures write <name>-actual.png and
<name>-diff.png (differences amplified) next to the test's output.
"""
import argparse
import os
import pathlib
import re
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

GOLDEN = ROOT / "pc/build/golden"
WINDOW = "960x720"
# After New Game is pressed (vsyncs): the opening card with its caption, then
# the first room under the game's camera.
OPENING_CARD = 620
FIRST_ROOM = 1500
SETTLE = 8  # vsyncs after a camera or mode change (the game draws every 2-4)

# (name, editor camera pose or None for the game's camera, graphics, mean, share)
# mean: mean absolute difference (0-255 scale); share: fraction of pixels
# differing by more than 32 in any channel.
FRAMES = [
    ("room-game-camera", None, {}, 1.0, 0.004),
    ("room-yaw180", (768, -900, 836, 180, -20), {}, 1.0, 0.004),
    ("room-yaw270", (768, -900, 836, 270, -20), {}, 1.0, 0.004),
    ("room-high", (768, -1450, 300, 0, -40), {}, 1.0, 0.004),
    ("room-depth", (768, -900, 836, 180, -20), {"show_depth": True}, 1.0, 0.004),
    ("room-1x-nearest", (768, -900, 836, 270, -20), {"scale": 1, "filter": 0, "aa": False}, 1.0, 0.004),
    ("room-gi", (768, -900, 836, 270, -20),
     {"gi": True, "rt_shadows": True, "gi_resolution": 1, "gi_rays": 1, "gi_bounces": 1}, 4.0, 0.03),
]
DEFAULTS = {"scale": 4, "filter": 2, "aspect": 0, "widescreen": False, "perspective": True, "show_depth": False,
            "interpolate": False, "aa": True, "texture_filter": False, "gi": False, "rt_shadows": False,
            # The references show the free camera with every room group (the
            # editor's view into rooms, editor_cutaway_smoke.py, off).
            "editor_hide_masks": False, "editor_game_cutaway": False, "editor_cutaway": False}


def capture(game, path):
    """The next presented window image as an RGB array (dumped to `path`.ppm)."""
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm))
    for _ in range(4):  # a paused game re-presents its newest frame
        game.run(1)
        if ppm.exists() and ppm.stat().st_size:
            break
    image = np.asarray(Image.open(ppm).convert("RGB"))
    ppm.unlink()
    return image


def settled(game, path):
    a = capture(game, path)
    game.run(2)
    b = capture(game, path)
    if a.shape != b.shape or np.abs(a.astype(int) - b.astype(int)).mean() > 0.5:
        game.run(SETTLE)
        a = capture(game, path)
    return a


def compare(actual, reference, mean_limit, share_limit):
    if actual.shape != reference.shape:
        return False, f"size {actual.shape[1]}x{actual.shape[0]} != {reference.shape[1]}x{reference.shape[0]}", None
    diff = np.abs(actual.astype(np.int16) - reference.astype(np.int16))
    mean = float(diff.mean())
    share = float((diff.max(axis=2) > 32).mean())
    ok = mean <= mean_limit and share <= share_limit
    return ok, f"mean {mean:.2f} (<= {mean_limit}), {share * 100:.2f}% strong (<= {share_limit * 100:.1f}%)", diff


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true", help="write the references instead of comparing")
    parser.add_argument("--only", action="append", default=[], help="frame names (default: all)")
    parser.add_argument("--golden", default=str(GOLDEN))
    args = parser.parse_args()
    frames = [f for f in FRAMES if not args.only or f[0] in args.only]
    names = ["opening-card"] + [f[0] for f in frames]
    out = OUT / "golden"
    out.mkdir(parents=True, exist_ok=True)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))  # no per-room lights
    env = {"ROOD_WINDOW": WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "4"}
    env.pop("ROOD_SETTINGS", None)
    os.environ.pop("ROOD_SETTINGS", None)
    game = Game.launch(fast=True, log=out / "game.log", extra_env=env)
    try:
        game.wait_title()
        if not game.call("graphics")["hardware"]:
            print("FAIL: no OpenGL renderer (golden frames need the hardware path)")
            return 1
        # One reference set per GPU; the Vulkan renderer (ROOD_RENDERER=vulkan)
        # is compared with the OpenGL references of the same GPU.
        # (Not the Vulkan pipeline cache's line, logged before the device's.)
        renderer = re.search(r"\[(?:gl|vk)\] ((?!validation|pipeline cache)[^/,\n]*)",
                             pathlib.Path(game.log).read_text())
        gpu = re.sub(r"[^a-z0-9]+", "-", (renderer.group(1) if renderer else "unknown").lower()).strip("-")
        golden = pathlib.Path(args.golden) / gpu
        if not args.update:
            missing = [n for n in names if not (golden / f"{n}.png").exists() and (not args.only or n in args.only)]
            if missing:
                print(f"SKIP: no references for {', '.join(missing)} in {golden}; run with --update")
                return 77
        game.call("graphics", **DEFAULTS)
        # Press on a fixed phase of the vsync count: the game draws every 2-4
        # vsyncs, and pressing on the other phase shifts every later frame.
        vsync = game.status()["vsync"]
        game.run(8 - vsync % 8)
        start = game.status()["vsync"]
        game.press("start", vsyncs=4, settle=0)
        shots = {}
        game.run(start + OPENING_CARD - game.status()["vsync"])
        shots["opening-card"] = settled(game, out / "opening-card.png")
        game.run(start + FIRST_ROOM - game.status()["vsync"])
        scene = game.call("scene")
        if not scene["triangles"]:
            print(f"FAIL: first room not loaded {FIRST_ROOM} vsyncs after New Game")
            return 1
        for name, pose, graphics, *_ in frames:
            game.call("graphics", **{**DEFAULTS, **graphics})
            if pose:
                x, y, z, yaw, pitch = pose
                game.call("editor_camera", x=x, y=y, z=z, yaw=yaw, pitch=pitch)
            else:
                game.call("editor_camera", off=1)
            game.run(SETTLE + (30 if graphics.get("gi") else 0))
            shots[name] = settled(game, out / f"{name}.png")
        game.call("editor_camera", off=1)
    finally:
        game.close()
        shutil.rmtree(lights, ignore_errors=True)
    log = pathlib.Path(game.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", game.log)
        return 1

    if args.update:
        golden.mkdir(parents=True, exist_ok=True)
        for name, image in shots.items():
            Image.fromarray(image).save(golden / f"{name}.png")
        print(f"PASS: wrote {len(shots)} references to {golden} (game imagery: never commit them)")
        return 0
    limits = {"opening-card": (1.0, 0.004), **{f[0]: (f[3], f[4]) for f in frames}}
    failed = []
    for name, image in shots.items():
        if args.only and name not in args.only:
            continue
        reference = np.asarray(Image.open(golden / f"{name}.png").convert("RGB"))
        ok, detail, diff = compare(image, reference, *limits[name])
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {detail}")
        if not ok:
            failed.append(name)
            Image.fromarray(image).save(out / f"{name}-actual.png")
            if diff is not None:
                Image.fromarray(np.clip(diff * 8, 0, 255).astype(np.uint8)).save(out / f"{name}-diff.png")
    if failed:
        print(f"FAIL: {len(failed)} golden frames differ ({', '.join(failed)}); images in {out}")
        return 1
    print(f"PASS: {len(shots)} golden frames match {golden}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
