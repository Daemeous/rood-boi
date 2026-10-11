#!/usr/bin/env python3
"""A GPU too slow for the path tracer (OpenGL): frames are never black and
presentation goes on.

    python3 pc/tests/gi_lag_smoke.py [--card pc/save/card1.mcd] [--shots DIR] [--slow N]

ROOD_GI_SLOW=N makes the GL path tracer trace N more times per update (the
same result, N times the GPU time: a slow GPU, deterministically). In the
save room with a test light and the Ultra preset (gi_method path):

- frames in flight are limited (gl_present.c): with every present traced
  (RPC graphics gi_budget 0), each presented frame is timed (RPC perf
  "frames": the timer queries complete instead of piling up), and after
  seconds paused (the game keeps redrawing) the next presented image comes
  back within a frame or two and is lit. Before the limit, the CPU queued a
  frame every 16 ms while the GPU took far longer; the queue grew without
  bound, a present_dump waited ~20 s and the NVIDIA driver gave up on the
  queued work: a black image (lighting_presets_smoke under `run.py -j 3`,
  light_actors_smoke "no frame presented"). ROOD_FRAMES_IN_FLIGHT=0 brings
  that back (--unlimited, to see it fail);
- the GI cadence (ptgi.c, gi_budget 16 ms): the tracer runs on every n-th
  present only and the others show its last result: perf reports reused
  presents and an interval > 1, every vsync still presents, and the picture
  is the lit one (close to the traced-every-present image, not black and
  not unlit);
- auto budget in fast mode (tests) is 50 ms.

Skips on Vulkan (its own path tracer) and without GL compute."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile
import time

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game  # noqa: E402
from lighting_presets_smoke import capture, default_card  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--shots", help="keep the captured images here")
    parser.add_argument("--slow", type=int, default=8, help="ROOD_GI_SLOW: extra traces per update")
    parser.add_argument("--pause", type=float, default=4.0, help="seconds paused before the capture")
    parser.add_argument("--unlimited", action="store_true",
                        help="ROOD_FRAMES_IN_FLIGHT=0: the old unlimited queue (expected to fail)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if os.environ.get("ROOD_RENDERER") == "vulkan":
        print("SKIP: the OpenGL path tracer's lag (Vulkan limits its frames in flight itself)")
        return 77
    out = OUT / "gi_lag"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    settings.unlink(missing_ok=True)
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_PERF": "1", "ROOD_SETTINGS": str(settings), "ROOD_GI_SLOW": str(args.slow)}
    if args.unlimited:
        env["ROOD_FRAMES_IN_FLIGHT"] = "0"
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-gilag-{os.getpid()}.sock"
    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env, socket_path=socket)

    def shot(name):
        path = out / f"{name}.png"
        start = time.monotonic()
        image = capture(game, path, timeout=90)
        took = time.monotonic() - start
        if shots:
            shutil.copyfile(path, shots / path.name)
        path.unlink()
        return image, took

    try:
        game.wait_title()
        caps = game.call("graphics")["lighting_caps"]
        if not caps["traced"]:
            print(f"SKIP: no GL compute path tracer here ({caps})")
            return 77
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        player = state.get("player")
        if not player or not game.call("scene")["triangles"]:
            print(f"FAIL: no room with Ashley after Continue: {state}")
            return 1
        (lights / f"zone{state['zone']:02d}_room{state['room']:02d}.txt").write_text(
            f"light point {player['x'] + 180} {player['y'] - 260} {player['z'] - 120} 1 0.8 0.55 1.4 1200 "
            "0 1 0 45 1 1\n")
        for _ in range(4):  # room files are polled twice a second
            game.run(15)
            time.sleep(0.25)
        check(len(game.call("lights")["lights"]) == 1, "the test light is loaded")
        g = game.call("graphics", lighting_preset="ultra", gi_budget=0)
        check(g["gi_method"] == "path", f"Ultra traces paths here (gi_method {g['gi_method']})")

        # 1. Every present traced: frames in flight are limited.
        game.run(30)
        game.call("perf", reset=True)
        game.run(60)
        perf = game.call("perf", reset=True)
        print(f"perf, every present traced: {perf}", flush=True)
        check(perf["gi_gpu_ms"] > 20, f"the path tracer is slow ({perf['gi_gpu_ms']:.1f} ms GPU per update; "
              f"ROOD_GI_SLOW={args.slow})")
        check(perf["presents"] >= 60 and perf["gi_reused"] == 0,
              f"gi_budget 0: every vsync presents and traces ({perf['presents']} presents, "
              f"{perf['gi_traced']} traced, {perf['gi_reused']} reused)")
        check(perf["frames"] >= perf["presents"] // 2,
              f"presented frames are timed: none pile up in flight ({perf['frames']} of {perf['presents']})")
        time.sleep(args.pause)  # paused: the game keeps redrawing (and tracing)
        traced, took = shot("traced")
        lit = float(traced.mean())
        check(lit > 8, f"after {args.pause:.0f} s paused, the presented image is lit (mean {lit:.1f})")
        check(took < 10, f"... and comes back promptly ({took:.1f} s; an unlimited queue took ~20 s)")

        # 2. The cadence: the tracer every n-th present, the last result between.
        game.call("graphics", gi_budget=16)
        game.run(30)
        game.call("perf", reset=True)
        game.run(120)
        perf = game.call("perf", reset=True)
        print(f"perf, gi_budget 16: {perf}", flush=True)
        check(perf["gi_interval"] > 1 and perf["gi_reused"] > 0 and perf["gi_traced"] > 0,
              f"gi_budget 16: traced every {perf['gi_interval']} presents ({perf['gi_traced']} traced, "
              f"{perf['gi_reused']} reused)")
        check(perf["presents"] >= 120, f"every vsync still presents ({perf['presents']} for 120)")
        reused, took = shot("cadence")
        check(float(reused.mean()) > 8 and took < 10,
              f"the presented image is lit (mean {reused.mean():.1f}, {took:.1f} s)")
        d = float(np.abs(reused.astype(int) - traced.astype(int)).mean())
        check(d < 6, f"... and is the lit picture of every-present tracing (mean difference {d:.2f})")
        # Without dynamic lighting (Original) for comparison: reuse is not the unlit fallback.
        game.call("graphics", lighting_preset="original")
        game.run(30)
        unlit, _ = shot("original")
        d_unlit = float(np.abs(reused.astype(int) - unlit.astype(int)).mean())
        check(d_unlit > d, f"... not the unlit picture (difference from Original {d_unlit:.2f})")

        # 3. Auto in fast mode.
        game.call("graphics", lighting_preset="ultra", gi_budget=-1)
        game.run(30)
        perf = game.call("perf", reset=True)
        check(abs(perf["gi_budget_ms"] - 50) < 1e-3, f"auto budget in fast mode: {perf['gi_budget_ms']} ms")
    finally:
        game.close()
        shutil.rmtree(lights, ignore_errors=True)
    log = pathlib.Path(game.log).read_text(errors="replace")
    bad = [line for line in log.splitlines() if "[crash]" in line or "[gl] shader" in line or "[gl] compute" in line]
    check(not bad, f"no crash or shader errors {bad[:3]}")
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: a slow path tracer never gives a black frame; presentation goes on")
    return 0


if __name__ == "__main__":
    sys.exit(main())
