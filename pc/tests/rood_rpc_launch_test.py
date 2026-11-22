#!/usr/bin/env python3
"""Games launched by rood_rpc: one launched from a short-lived thread survives that thread, and one whose script ends on an exception without
closing it does not outlive the script.

    python3 pc/tests/rood_rpc_launch_test.py

PR_SET_PDEATHSIG (rood_rpc._die_with_parent) follows the thread that forked the child: set for a launch from a worker thread it would signal
the game the moment that thread ended, mid-run (the Gitea #3 shape, and snapshot_equivalence launches from a thread pool). So:
  - a game launched in a thread that then ends is still answering afterwards (and closes cleanly);
  - a script (a child process) that launches a game from its main thread and raises leaves no game running (PDEATHSIG, SIGTERM);
  - the same from a thread of that script: the game is reaped when the script ends (the atexit reaper)."""
import os
import pathlib
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import Game  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    try:  # (a zombie of ours is gone for this purpose)
        return pathlib.Path(f"/proc/{pid}/stat").read_text().split()[2] != "Z"
    except OSError:
        return False


def gone_within(pid, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if not alive(pid):
            return True
        time.sleep(0.2)
    return not alive(pid)


SCRIPT = """
import sys, threading
sys.path.insert(0, %r)
from rood_rpc import Game
if sys.argv[1] == "thread":
    box = []
    t = threading.Thread(target=lambda: box.append(Game.launch(fast=True)))
    t.start(); t.join()
    g = box[0]
else:
    g = Game.launch(fast=True)
print(g.processes[0].pid, flush=True)
raise RuntimeError("the script ends without closing its game")
""" % str(ROOT / "pc/tools")


def main():
    # 1. a game launched from a thread that ends is still there
    box = []
    t = threading.Thread(target=lambda: box.append(Game.launch(fast=True)))
    t.start()
    t.join()
    g = box[0]
    pid = g.processes[0].pid
    time.sleep(1.5)
    check(alive(pid), "a game launched from a short-lived thread is running after the thread ended")
    try:
        state = g.call("status")
        check("vsync" in state, f"and answers (vsync {state.get('vsync')})")
        g.run(5)
    finally:
        g.close()
    check(gone_within(pid, 10), "and closes cleanly")
    # 2. a script that ends on an exception takes its game with it
    for mode in ("main", "thread"):
        done = subprocess.run([sys.executable, "-c", SCRIPT, mode], capture_output=True, text=True, timeout=300)
        lines = done.stdout.split()
        if not lines:
            check(False, f"{mode}: the script printed no pid ({done.stderr[-200:]})")
            continue
        gpid = int(lines[0])
        check(done.returncode != 0, f"{mode}: the script ended on its exception")
        check(gone_within(gpid, 10), f"{mode}: its game is gone within 10 s of the script's end")
        if alive(gpid):
            os.kill(gpid, 9)
    print("FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
