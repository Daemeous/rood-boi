#!/usr/bin/env python3
"""The fixed cost of a frame stays off the game thread (pc/src/frame_cost.h,
ROOD_PERF=1 counters through RPC perf), and what it no longer does draws
the same pictures.

    python3 pc/tests/frame_cost_smoke.py [--card CARD] [--out DIR]

From a copy of the card inside card_fixtures.card_guard (skipped without
one), in the save room, unpaced:
  - standing (120 vsyncs): no read-back of the target; the emissive
    matching hashes almost nothing (unmatched textures are hashed again
    only when their VRAM changed, not every frame); loading.ppm is not
    rewritten every frame; the CPU rasterizer leaves out what the native
    room left stale (2D the target's picture replaces before any read);
  - battle mode, the targeting sphere held: rows of the picture it samples
    are copied on the GPU (own copies), never read back (each read-back
    waited for the GPU's whole frame: the sphere and the menus dropped to
    12-20 fps with path tracing on);
  - the original Status screen (it redraws rows of its own picture): no
    read-back either.
Then the same moments again in a second game with the old paths
(ROOD_GPU_VRAM_COPY=0: read-backs; ROOD_RASTER_SKIP=0: the CPU draws
everything): the displayed pictures (present_dump) are identical."""
import argparse
import os
import pathlib
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def measure(game, vsyncs):
    game.call("perf", reset=True)
    game.run(vsyncs)
    perf = game.call("perf", reset=True)
    return perf.get("cpu_counts"), perf.get("cpu_sections", {})


def shot(game, path):
    """The displayed picture: the next image presented (present_dump)."""
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.wait_presents(3)
    game.call("present_dump", path=str(ppm.resolve()))
    for _ in range(200):
        if ppm.exists():
            break
        game.wait_presents(1)
    image = np.asarray(Image.open(ppm).convert("RGB"))
    ppm.unlink()
    return image


def play(card, out, name, extra):
    """The three moments in one game: their counters and screenshots."""
    env = {"ROOD_PERF": "1", "ROOD_GI": "0", "ROOD_LIGHTS": str(out / f"lights-{name}"), "ROOD_WINDOW": "960x720",
           **extra}
    game = launch_at("save_room", card=str(card), fast=True, log=out / f"{name}.log", extra_env=env)
    result = {}
    try:
        game.run(30)
        result["field"] = measure(game, 120)
        game.press("circle", vsyncs=3, settle=60)  # the weapon drawn
        game.call("input", buttons=["circle"], vsyncs=3)
        game.run(40)  # the targeting sphere open
        result["sphere"] = measure(game, 60)
        result["sphere_image"] = shot(game, out / f"{name}-sphere.png")
        for _ in range(3):
            game.press("cross", vsyncs=3, settle=40)
        game.run(60)
        for button, settle in (("triangle", 90), ("down", 20), ("down", 20), ("circle", 150)):
            game.press(button, vsyncs=4, settle=settle)
        result["status"] = measure(game, 60)
        result["status_image"] = shot(game, out / f"{name}-status.png")
    finally:
        game.close()
    return result


def run(args, out, card):
    new = play(card, out, "new", {})
    counts, sections = new["field"]
    if not check(counts is not None, "RPC perf reports the game thread's counters (cpu_counts)"):
        return 1
    print(f"      field: {sections.get('vsync', 0):.3f} ms a vsync; {counts}")
    check(counts["readbacks"] == 0, f"standing: no read-back ({counts['readbacks']} a vsync)")
    check(counts["hashed_texels"] < 2000,
          f"standing: emissive matching hashes {counts['hashed_texels']:.0f} texels a vsync (< 2000)")
    check(counts["debug_ppm"] < 0.1, f"standing: loading.ppm written {counts['debug_ppm']:.2f} times a vsync")
    check(counts["skipped_px"] > 10 * max(counts["raster_px"], 1),
          f"standing: the CPU rasterizer leaves out the stale drawing area "
          f"({counts['skipped_px']:.0f} pixels left out, {counts['raster_px']:.0f} drawn a vsync)")
    for scene in ("sphere", "status"):
        counts, sections = new[scene]
        print(f"      {scene}: {sections.get('vsync', 0):.3f} ms a vsync; {counts}")
        check(counts["readbacks"] == 0 and counts["gpu_copies"] > 0,
              f"{scene}: rows of its own picture copied on the GPU ({counts['gpu_copies']:.1f} a vsync), "
              f"no read-back ({counts['readbacks']:.1f})")
    old = play(card, out, "old", {"ROOD_GPU_VRAM_COPY": "0", "ROOD_RASTER_SKIP": "0"})
    check(old["sphere"][0]["readbacks"] > 0, "the old path reads the sphere's rows back (the comparison means "
          f"something: {old['sphere'][0]['readbacks']:.1f} a vsync)")
    for scene in ("sphere", "status"):
        a, b = new[scene + "_image"], old[scene + "_image"]
        differ = int(np.count_nonzero(np.any(a != b, axis=2))) if a.shape == b.shape else -1
        if differ and args.out:
            Image.fromarray(a).save(out / f"{scene}-new.png")
            Image.fromarray(b).save(out / f"{scene}-old.png")
        check(differ == 0, f"{scene}: the picture equals the old paths' ({differ} pixels differ)")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="keep differing pictures here (game imagery: keep it out of git)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "frame_cost"
    out.mkdir(parents=True, exist_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)  # the defaults, not the user's settings
    with card_guard(out / "cards", source=args.card) as cards:
        status = run(args, out, cards.card1)
    print("PASS" if not status else f"FAIL ({len(failures)})")
    return status


if __name__ == "__main__":
    sys.exit(main())
