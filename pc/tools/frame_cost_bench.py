#!/usr/bin/env python3
"""Frame cost of a few game moments, headless (ROOD_PERF=1 sections and
counters, pc/src/frame_cost.h; GPU time from RPC perf).

    python3 pc/tools/frame_cost_bench.py [--vsyncs N] [--paced] [--json OUT]
        [--card CARD] [scene ...]

From the pinned test card's save room (checkpoints.py save_room), each scene
is set up, then N vsyncs (default 240) run with the counters reset first:
  field     standing in the save room
  sphere    battle mode, the targeting sphere held open (Circle, Circle)
  overlay   the L2 + R2 shortcut overlay held, the weapon slots option on
            (the quick weapon switch's labels)
  swap      a quick weapon swap (L2 + R2 + Triangle and back with Circle)
  status    the original Status screen (Triangle, STATUS)
Per scene: wall ms per vsync (unpaced unless --paced: then frames a second
as the window shows them), the game thread's sections (ms per vsync), the
GPU's ms per presented frame and the counters per vsync. Set ROOD_ARCH and
ROOD_RENDERER as for the tests (e.g. ROOD_ARCH=64 ROOD_RENDERER=vulkan);
ROOD_BINARY measures another build."""
import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from checkpoints import launch_at  # noqa: E402
from rood_rpc import OUT  # noqa: E402

SCENES = ["field", "sphere", "overlay", "swap", "status"]


def measure(game, vsyncs, label):
    game.call("perf", reset=True)
    start = time.monotonic()
    game.run(vsyncs)
    wall = time.monotonic() - start
    return report(game.call("perf", reset=True), wall, vsyncs, label)


def report(perf, wall, vsyncs, label):
    sections = perf.get("cpu_sections", {})
    counts = perf.get("cpu_counts", {})
    row = {"scene": label, "vsyncs": vsyncs, "wall_ms": wall * 1000 / vsyncs, "fps": vsyncs / wall,
           "gpu_ms": perf.get("gpu_ms", 0), "sections": sections, "counts": counts,
           "gpu_sections": perf.get("sections", {})}
    shown = " ".join(f"{k} {v:.3f}" for k, v in sections.items() if v >= 0.0005)
    counted = " ".join(f"{k} {v:.1f}" for k, v in counts.items() if v)
    print(f"{label:8s} wall {row['wall_ms']:7.3f} ms/vsync ({row['fps']:6.1f}/s)  gpu {row['gpu_ms']:.3f} ms/frame",
          flush=True)
    print(f"         cpu: {shown}", flush=True)
    print(f"         counts: {counted}", flush=True)
    if row["gpu_sections"]:
        gpu = " ".join(f"{k} {v:.3f}" for k, v in row["gpu_sections"].items() if v >= 0.0005)
        print(f"         gpu: {gpu}", flush=True)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenes", nargs="*", default=SCENES)
    parser.add_argument("--vsyncs", type=int, default=240)
    parser.add_argument("--paced", action="store_true", help="59.94 Hz pacing (frames a second as shown)")
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--gi", action="store_true", help="with the path-traced indirect light (ROOD_GI=1)")
    parser.add_argument("--json", help="write the rows here")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="frame-cost-", dir=OUT))
    card = work / "card1.mcd"
    shutil.copyfile(args.card, card)
    env = {"ROOD_PERF": "1", "ROOD_GI": "1" if args.gi else "0", "ROOD_LIGHTS": str(work / "lights"), "ROOD_WINDOW": "960x720"}
    os.environ.pop("ROOD_SETTINGS", None)
    rows = []
    game = launch_at("save_room", card=str(card), fast=not args.paced, log=OUT / "frame-cost-game.log", extra_env=env)
    try:
        game.run(30)
        for scene in args.scenes:
            if scene == "field":
                rows.append(measure(game, args.vsyncs, scene))
            elif scene == "sphere":
                game.press("circle", vsyncs=3, settle=60)  # the weapon drawn
                game.call("input", buttons=["circle"], vsyncs=3)
                game.run(40)  # the sphere opens; held while the button is up
                rows.append(measure(game, args.vsyncs, scene))
                for _ in range(3):
                    game.press("cross", vsyncs=3, settle=40)
                game.run(60)
            elif scene == "overlay":
                game.call("weapon_slots", enabled=True)
                game.call("input", buttons=["l2", "r2"], vsyncs=args.vsyncs + 60)
                game.run(40)
                rows.append(measure(game, args.vsyncs, scene))
                game.run(40)
            elif scene == "swap":
                game.call("weapon_slots", enabled=True)
                # The equipped weapon into slot 1 (R2 + Select), then swap away and back.
                game.call("input", buttons=["r2"], vsyncs=20)
                game.run(5)
                game.call("input", buttons=["select"], vsyncs=3)
                game.run(30)
                game.call("perf", reset=True)
                total, start = 0, time.monotonic()
                for button in ("triangle", "circle"):
                    game.call("input", buttons=["l2", "r2"], vsyncs=40)
                    game.run(25)
                    game.call("input", buttons=[button], vsyncs=3)
                    game.run(args.vsyncs // 2)
                    total += 25 + args.vsyncs // 2
                wall = time.monotonic() - start
                rows.append(report(game.call("perf", reset=True), wall, total, scene))
            elif scene == "status":
                game.press("triangle", vsyncs=4, settle=90)
                game.press("down", vsyncs=4, settle=20)
                game.press("down", vsyncs=4, settle=20)
                game.press("circle", vsyncs=4, settle=150)
                rows.append(measure(game, args.vsyncs, scene))
                for _ in range(4):
                    game.press("triangle", vsyncs=4, settle=40)
                game.run(60)
            else:
                print("unknown scene", scene)
    finally:
        game.close()
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(rows, indent=1))
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
