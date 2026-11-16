#!/usr/bin/env python3
"""Identical runs give identical game states, whatever the host's timing.

A game warped to zone 9 room 5 (RPC room_goto {hold}) and stepped through the load
is hashed (RAM, game state, IO, VRAM; RPC snapshot hash) at every step. The same run is made
several times, the machine loaded with CPU burners of this test's own (the more the
host's threads are starved, the wider a host-timing dependence shows), and every run must
give the very same hashes. Before room_goto {hold}, the warp ran the game free until the
arrival and the next step paused it at whatever vsync the wall clock had reached: 1 to 3
vsyncs of difference under load, the whole route after it from another state.

    python3 pc/tests/determinism_smoke.py --card pc/save/card1.mcd [--runs 5] [--out DIR]
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

ROOM = (9, 5)


def one_run(card, out, socket, index):
    game = launch_at("save_room", card=card, fast=True, log=out / f"game_{index}.log", socket_path=socket)
    trace = []
    try:
        game.run(10)
        before = game.status()["vsync"]
        game.call("room_goto", hold=True, zone=ROOM[0], room=ROOM[1], enemies=False, scripts=False)
        time.sleep(0.3)  # (a host's delay between two requests: the game must not move meanwhile)
        after = game.status()
        trace.append(("held", before, after["vsync"], after["paused"]))
        for _ in range(60):
            game.run(5)
            h = game.call("snapshot", action="hash")
            trace.append((h["vsync"], h["ram"], h["state"], h["io"], h["vram"]))
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        arrived = (game.state()["zone"], game.state()["room"]) == ROOM
    finally:
        game.close()
    return trace, arrived


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--burners", type=int, default=2 * (os.cpu_count() or 4))
    ap.add_argument("--out")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "determinism"
    out.mkdir(parents=True, exist_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-determinism-{os.getpid()}.sock"
    burners = [subprocess.Popen([sys.executable, "-c", "while True: pass"]) for _ in range(args.burners)]
    try:
        runs = [one_run(args.card, out, socket, i) for i in range(args.runs)]
    finally:
        for b in burners:
            b.kill()
            b.wait()
    first, arrived = runs[0]
    failures = 0
    for i, (trace, _) in enumerate(runs):
        held = trace[0]
        if held[1] != held[2] or not held[3]:
            print(f"FAIL: run {i}: the game ran on after room_goto (vsync {held[1]} -> {held[2]}, paused {held[3]})")
            failures += 1
    for i, (trace, _) in enumerate(runs[1:], 1):
        bad = next((k for k, (a, b) in enumerate(zip(first, trace)) if a != b), None)
        same = bad is None and len(trace) == len(first)
        print(("ok:   " if same else "FAIL: ") + f"run {i} against run 0: {len(trace)} steps"
              + ("" if same else f", first difference at step {bad} (vsync {first[bad][0] if bad is not None else '-'})"))
        failures += not same
    if not arrived:
        print("FAIL: the game did not arrive")
        failures += 1
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
