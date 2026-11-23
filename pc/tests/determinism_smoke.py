#!/usr/bin/env python3
"""Identical runs give identical game states, whatever the host's timing.

A game warped through the five platform rooms (RPC room_goto {hold}) and stepped through the loads
is hashed (RAM, scratch, game state, IO, VRAM, the overlays' statics; RPC snapshot hash) at every step. The same run is made
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

ROOMS = ((9, 5), (15, 7), (24, 2), (25, 2), (32, 0))  # the platform rooms: actors collide with the map
STORY = (("evt-0001", 0), ("evt-0069", 30))  # story scenes (pc/assets/cutscenes/story.json) and the vsyncs run before the first hash (build_compare's STORY_SETTLE)


def one_run(card, out, socket, index):
    game = launch_at("save_room", card=card, fast=True, log=out / f"game_{index}.log", socket_path=socket)
    trace = []
    try:
        game.run(10)
        before = game.status()["vsync"]
        for index, room in enumerate(ROOMS):
            game.call("room_goto", hold=True, zone=room[0], room=room[1], enemies=False, scripts=False)
            if index == 0:
                time.sleep(0.3)  # (a host's delay between two requests: the game must not move meanwhile)
                after = game.status()
                trace.append(("held", before, after["vsync"], after["paused"]))
            for _ in range(80):
                game.run(10)
                h = game.call("snapshot", action="hash")
                trace.append((h["vsync"], h["heap"], h["state"], h["vram"], h["overlays"]))
                if game.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            game.run(120)
            h = game.call("snapshot", action="hash")  # (every sort of memory the snapshot holds, the overlays' statics too)
            trace.append((h["vsync"], h["heap"], h["state"], h["vram"], h["overlays"]))
        arrived = (game.state()["zone"], game.state()["room"]) == ROOMS[-1]
    finally:
        game.close()
    return trace, arrived


def story_run(out, socket, scene, settle, index):
    """A story scene as build_compare plays it: from the title with no card, the game reaches the scene on its own (unpaced), is seeked back to
    the scene's first frame and paused, and from there every step is hashed with the VRAM stream's digest (started again at the first frame:
    the way there is not the same twice, the replay must be)."""
    saved = os.environ.get("ROOD_TEST_SNAPSHOTS")
    os.environ["ROOD_TEST_SNAPSHOTS"] = "0"  # (booted to the title, as build_compare does: the card is none)
    try:
        game = launch_at("title", card=str(out / "no-card.mcd"), fast=True, log=out / f"story_{scene}_{index}.log", socket_path=socket)
    finally:
        if saved is None:
            os.environ.pop("ROOD_TEST_SNAPSHOTS", None)
        else:
            os.environ["ROOD_TEST_SNAPSHOTS"] = saved
    trace = []
    try:
        game.call("cutscene", action="start", scene=scene)
        last, still = game.status()["vsync"], time.monotonic()
        while True:
            time.sleep(0.1)
            status = game.call("cutscene")
            if status["on"] and not status["busy"]:
                break
            now = game.status()["vsync"]
            if now != last:
                last, still = now, time.monotonic()
            elif time.monotonic() - still > 30:
                raise RuntimeError(f"{scene}: {status['message']} (the game's clock stood still for 30 s at vsync {now})")
        game.call("cutscene", action="pause")
        game.call("cutscene", action="seek", frame=game.call("cutscene")["first"])
        for _ in range(300):
            time.sleep(0.1)
            if not game.call("cutscene")["busy"]:
                break
        game.call("vram_stream", reset=True)
        if settle:
            game.call("step", vsyncs=settle)
            game.wait_paused()
        for _ in range(30):
            game.run(10)
            h = game.call("snapshot", action="hash")
            trace.append((h["vsync"], h["heap"], h["state"], h["vram"], h["overlays"],
                          game.call("vram_stream")["total"]["hash"]))
    finally:
        game.close()
    return trace


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--runs", type=int, default=4)
    ap.add_argument("--burners", type=int, default=2 * (os.cpu_count() or 4))
    ap.add_argument("--story-runs", type=int, default=3, help="runs of each story scene (0: none)")
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
        stories = {scene: [story_run(out, socket, scene, settle, i) for i in range(args.story_runs)] for scene, settle in STORY} if args.story_runs else {}
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
    for scene, traces in stories.items():
        for i, trace in enumerate(traces[1:], 1):
            bad = next((k for k, (a, b) in enumerate(zip(traces[0], trace)) if a != b), None)
            same = bad is None and len(trace) == len(traces[0])
            print(("ok:   " if same else "FAIL: ") + f"story {scene}: run {i} against run 0: {len(trace)} steps (state and VRAM stream digest)"
                  + ("" if same else f", first difference at step {bad}: {traces[0][bad] if bad is not None else '-'} against {trace[bad] if bad is not None else '-'}"))
            failures += not same
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
