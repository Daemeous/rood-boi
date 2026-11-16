#!/usr/bin/env python3
"""A save state of one build resumes in another build that only differs in names (a name import, pc/names).

    ROOD_ARCH=64 python3 pc/tests/snapshot_rename_check.py OLD_BINARY NEW_BINARY [--frames 60]

OLD runs to the opening checkpoint and saves a state; NEW resumes it (ROOD_SNAPSHOT: the code and layout keys must
match, else the game refuses it) and its game state (guest RAM, scratchpad, I/O, overlay statics, VRAM) must equal
OLD's at the save; both then run the same vsyncs and must be in the same state again. Needs both builds' keys
equal: pc/tools/snapshot_layout.py hashes the symbols by the decomp's names (the keys are the same across a rename).
"""
import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import checkpoints  # noqa: E402
import rood_rpc  # noqa: E402
from rood_rpc import Game  # noqa: E402

KEYS = ("ram", "scratch", "io", "overlays", "vram")  # not "state": it holds the last vsync's wall-clock time


def state(game):
    h = game.call("snapshot", action="hash")
    return {k: h[k] for k in KEYS}, h["vsync"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--frames", type=int, default=60)
    args = ap.parse_args()
    out = pathlib.Path(os.environ.get("ROOD_OUT") or rood_rpc.OUT)
    work = out / "snapshot_rename"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    socket = f"/tmp/rood-rename-{os.getpid()}.sock"
    env = checkpoints._base_env(work)
    failures = []

    def check(ok, what):
        print(("ok:   " if ok else "FAIL: ") + what, flush=True)
        if not ok:
            failures.append(what)

    rood_rpc.BINARY = pathlib.Path(args.old).resolve()
    game = checkpoints.launch_at("opening", fast=True, socket_path=socket, log=work / "old.log", extra_env=env)
    try:
        before, vsync = state(game)
        game.call("snapshot", action="save", path=str(work / "old.snap"))
        game.run(args.frames)
        old_later, _ = state(game)
    finally:
        game.close()
    rood_rpc.BINARY = pathlib.Path(args.new).resolve()
    game = Game.launch(fast=True, socket_path=socket, log=work / "new.log", extra_env=env, snapshot=work / "old.snap")
    try:
        after, vsync_after = state(game)
        check(vsync_after == vsync, f"the renamed build resumed the old build's state at vsync {vsync}")
        check(after == before, "its game state equals the old build's at the save")
        game.run(args.frames)
        new_later, _ = state(game)
        check(new_later == old_later, f"{args.frames} vsyncs later both are in the same state")
    finally:
        game.close()
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
