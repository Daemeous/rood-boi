#!/usr/bin/env python3
"""Run a test suite with its game stopped (SIGSTOP) for a while at a chosen
moment, as the thermal guard (or a busy machine) does: a wait that guesses a
time instead of waiting for a condition the game reports fails here.

    python3 pc/tests/stall_inject.py [--seconds 5] [--write GLOB] [--call METHOD]
                                     [--site TEXT] [--after-reply] [--exec NAME] [--nth N] [--times K]
                                     -- SUITE.py [ARGS...]

The suite runs in this process. The games it starts (any process whose
program is rood_*) are stopped for --seconds (default 5), then continued:
  --call METHOD  just before the suite sends RPC request METHOD (Game.call; "*"
                 any); default: the first request once armed
  --site TEXT    only requests made while a suite source line containing TEXT
                 runs (any line of the call stack outside rood_rpc: stable
                 across edits elsewhere in the file)
  --after-reply  just after the request's reply instead (the game stopped while
                 the suite goes on: a sleep, its own work)
  --exec NAME    just before the suite runs program NAME (subprocess.run; e.g.
                 send_key: an X11 key sent to a stopped game) instead
  --write GLOB   armed only once the suite writes, replaces or deletes a file
                 whose name matches GLOB (pathlib write_text / write_bytes /
                 unlink, os.replace); each such write arms it again
  --nth N        the Nth matching request after arming (default 1)
  --times K      stall at most K times in all (default 1; 0: every time)
Each stall prints a "[stall]" line on stderr. Example (ui_system_smoke's
debounced save, the game stopped right after a change):

    python3 pc/tests/stall_inject.py --site 'a change marks the page dirty' \
        -- pc/tests/ui_system_smoke.py --card pc/save/card1.mcd
"""
import argparse
import fnmatch
import linecache
import os
import pathlib
import runpy
import signal
import subprocess
import sys
import threading

parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
parser.add_argument("--seconds", type=float, default=5.0)
parser.add_argument("--write")
parser.add_argument("--call")
parser.add_argument("--site")
parser.add_argument("--after-reply", action="store_true")
parser.add_argument("--exec", dest="program")
parser.add_argument("--nth", type=int, default=1)
parser.add_argument("--times", type=int, default=1)
parser.add_argument("suite", nargs=argparse.REMAINDER)
args = parser.parse_args()
suite = args.suite[1:] if args.suite[:1] == ["--"] else args.suite
if not suite:
    parser.error("give the suite: -- SUITE.py [ARGS...]")

games = set()
state = {"armed": args.write is None, "seen": 0, "stalls": 0, "why": "from the start"}
lock = threading.Lock()


def log(message):
    print(f"[stall] {message}", file=sys.stderr, flush=True)


def stall(where):
    pids = [pid for pid in sorted(games) if os.path.exists(f"/proc/{pid}")]
    for pid in pids:
        os.kill(pid, signal.SIGSTOP)
    log(f"SIGSTOP {pids} for {args.seconds:g} s {where} (armed {state['why']})")

    def resume():
        for pid in pids:
            try:
                os.kill(pid, signal.SIGCONT)
            except ProcessLookupError:
                pass
        log(f"SIGCONT {pids}")

    threading.Timer(args.seconds, resume).start()


def site(text):
    """The suite's source line on the call stack (the innermost; with `text`, the
    innermost containing it): "file:line", or None when `text` is on none."""
    frame = sys._getframe(2)
    first = None
    while frame:
        name = frame.f_code.co_filename
        if pathlib.Path(name).name not in ("rood_rpc.py", "stall_inject.py", "runpy.py"):
            where = f"{pathlib.Path(name).name}:{frame.f_lineno}"
            first = first or where
            if text and text in linecache.getline(name, frame.f_lineno):
                return where
        frame = frame.f_back
    return None if text else (first or "?")


def maybe_stall(method, what="before RPC"):
    where = site(args.site)
    if where is None:
        return
    if (what == "before RPC") == (args.program is not None):
        return
    with lock:
        if not state["armed"] or (args.times and state["stalls"] >= args.times):
            return
        if what == "before RPC" and args.call not in (None, "*", method):
            return
        state["seen"] += 1
        if state["seen"] < args.nth:
            return
        state["armed"] = args.write is None and (args.times == 0 or state["stalls"] + 1 < args.times)
        state["seen"] = 0
        state["stalls"] += 1
    stall(f"{'after RPC' if args.after_reply and what == 'before RPC' else what} {method} ({where})")


def arm(path, what):
    if args.write and fnmatch.fnmatch(pathlib.Path(path).name, args.write):
        with lock:
            if not args.times or state["stalls"] < args.times:
                state["armed"], state["seen"], state["why"] = True, 0, f"by {what} {pathlib.Path(path).name}"


# the games this suite starts
_popen_init = subprocess.Popen.__init__


def popen_init(self, cmd, *a, **k):
    _popen_init(self, cmd, *a, **k)
    program = cmd[0] if isinstance(cmd, (list, tuple)) else str(cmd).split()[0]
    name = pathlib.Path(str(program)).name
    if name.startswith("rood_") or name == "rood.exe":
        games.add(self.pid)


subprocess.Popen.__init__ = popen_init
_run = subprocess.run


def run(cmd, *a, **k):
    program = cmd[0] if isinstance(cmd, (list, tuple)) else str(cmd).split()[0]
    if args.program and pathlib.Path(str(program)).name == args.program:
        maybe_stall(args.program, "before running")
    return _run(cmd, *a, **k)


subprocess.run = run

# file writes that arm it
for name in ("write_text", "write_bytes", "unlink"):
    original = getattr(pathlib.Path, name)

    def wrapped(self, *a, _original=original, _name=name, **k):
        result = _original(self, *a, **k)
        arm(self, _name)
        return result

    setattr(pathlib.Path, name, wrapped)
_replace = os.replace


def replace(src, dst, *a, **k):
    result = _replace(src, dst, *a, **k)
    arm(dst, "os.replace")
    return result


os.replace = replace

# RPC requests (the suites' own rood_rpc)
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import rood_rpc  # noqa: E402

_call = rood_rpc.Game._call


def call(self, method, params):
    if args.after_reply:
        result = _call(self, method, params)
        maybe_stall(method)
        return result
    maybe_stall(method)
    return _call(self, method, params)


rood_rpc.Game._call = call

script = pathlib.Path(suite[0]).resolve()
sys.argv = [str(script), *suite[1:]]
sys.path.insert(0, str(script.parent))
try:
    runpy.run_path(str(script), run_name="__main__")
finally:
    log(f"{state['stalls']} stall(s)")
