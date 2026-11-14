#!/usr/bin/env python3
"""What the renderer thread buys (T1 R4e): the game runs in real time (the 59.94 Hz frame loop) for a stretch with a stall injected on
the game thread (ROOD_TEST_STALL=ms:every: the thread sleeps ms every `every` ticks, as a long stretch of game logic would) and the
presents' pacing is read back (RPC present_stats: the real-time interval between presents, images per field) with render_thread 0 and 2.
ROOD_PERF=1 reports the game thread's own frame work. Not a pass/fail test: it prints the numbers.

    python3 pc/tests/present_stall_bench.py [--seconds 20] [--stall 40:30] [--pacing 0|1] [--sim-hz 144]
"""
import argparse
import os
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game  # noqa: E402


def run(thread, stall, seconds, pacing, sim_hz, rate):
    label = f"t{thread}-{stall.replace(':', '_') or 'none'}"
    log = OUT / f"present-stall-{label}.log"
    env = {"ROOD_RENDER_THREAD": str(thread), "ROOD_PERF": "1", "ROOD_TEST_STALL": stall, "ROOD_SETTINGS": str(OUT / f"present-stall-{label}.cfg")}
    if sim_hz:
        env["ROOD_PRESENT_SIM_HZ"] = str(sim_hz)
    game = Game.launch(socket_path=f"/tmp/rood-stall-{os.getpid()}-{label}.sock", log=str(log), extra_env=env, fast=False)
    try:
        game.wait_title()
        game.call("graphics", interpolate=True, present_rate=rate, present_pacing=pacing)
        game.run(60)
        game.call("present_stats", reset=True)
        game.run(int(seconds * 59.94))
        st = game.call("present_stats")
    finally:
        game.close()
    perf = [float(m.group(1)) for m in re.finditer(r"frame work avg ([0-9.]+) ms", log.read_text())]
    peak = [float(m.group(1)) for m in re.finditer(r"max ([0-9.]+) ms", log.read_text())]
    over = [int(m.group(1)) for m in re.finditer(r"(\d+)/300 over budget", log.read_text())]
    return st, perf, peak, over


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--stall", default="40:30", help="ms:every ticks ('' for none)")
    parser.add_argument("--pacing", type=int, default=0)
    parser.add_argument("--sim-hz", type=float, default=0)
    parser.add_argument("--rate", type=int, default=1)
    parser.add_argument("--threads", default="0,2", help="render_thread values to run")
    args = parser.parse_args()
    for thread in [int(t) for t in args.threads.split(',')]:
        st, perf, peak, over = run(thread, args.stall, args.seconds, args.pacing, args.sim_hz, args.rate)
        iv, per = st["interval_ms"], st["images_per_field"]
        field = 1000.0 / 59.94
        print(f"render_thread {thread}: stall {args.stall or 'none'}, pacing {args.pacing}, rate {args.rate}, sim {args.sim_hz or 'real'}")
        print(f"  presents {st['presents']} ({st['presents_per_second']:.2f}/s), fields {st['fields']} ({st['fields_per_second']:.2f}/s), order violations {st['order_violations']}")
        print(f"  present interval ms: mean {iv['mean']:.2f} stddev {iv['stddev']:.2f} min {iv['min']:.2f} max {iv['max']:.2f} (a field is {field:.2f})")
        print(f"  images per field: mean {per['mean']:.3f}, counts {per['counts']}")
        if perf:
            print(f"  game-thread frame work: avg of avgs {sum(perf) / len(perf):.2f} ms, worst max {max(peak):.2f} ms, over-budget ticks {sum(over)}/{300 * len(over)}")


if __name__ == "__main__":
    main()
