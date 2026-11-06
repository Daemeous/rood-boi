#!/usr/bin/env python3
"""Play mode: background work that leaves the machine to the player.

    python3 pc/tools/play_mode.py -- COMMAND...   run COMMAND in play mode
    python3 pc/tests/run.py --play ...            the test runner in play mode (also ROOD_PLAY=1)

The process (and everything it starts: builds, ctest, zig_diff, game
suites) is confined to the efficiency cores and put in the idle
scheduling class (SCHED_IDLE: it runs only on time nothing else wants), so
a game played meanwhile keeps the performance cores and wins every
contended slice. Both are inherited and cannot be raised back by the
children. Tunable: ROOD_PLAY_CPUS (a cpulist such as "8-19"; default the
hybrid CPU's efficiency cores, else every CPU but the first half),
ROOD_PLAY_IDLE=0 keeps the normal scheduling class.
"""
import os
import pathlib
import sys


def parse_cpulist(text):
    """A kernel cpulist ("0-3,8,10-11") as a set of CPU numbers."""
    cpus = set()
    for part in text.strip().split(","):
        if not part:
            continue
        lo, _, hi = part.partition("-")
        cpus.update(range(int(lo), int(hi or lo) + 1))
    return cpus


def play_cpus():
    """The CPUs play mode runs on: ROOD_PLAY_CPUS, else the efficiency cores
    (/sys/devices/cpu_atom on a hybrid Intel), else the upper half."""
    available = os.sched_getaffinity(0)
    if os.environ.get("ROOD_PLAY_CPUS"):
        return parse_cpulist(os.environ["ROOD_PLAY_CPUS"]) & available or available
    atom = pathlib.Path("/sys/devices/cpu_atom/cpus")
    if atom.exists():
        cpus = parse_cpulist(atom.read_text()) & available
        if cpus:
            return cpus
    ordered = sorted(available)
    return set(ordered[len(ordered) // 2:]) or available


def apply():
    """Put this process (and its future children) in play mode; a one-line description."""
    cpus = play_cpus()
    os.sched_setaffinity(0, cpus)
    idle = os.environ.get("ROOD_PLAY_IDLE", "1") != "0"
    if idle:
        os.sched_setscheduler(0, os.SCHED_IDLE, os.sched_param(0))
    ordered = sorted(cpus)
    span = f"{ordered[0]}-{ordered[-1]}" if ordered == list(range(ordered[0], ordered[-1] + 1)) else ",".join(map(str, ordered))
    return f"play mode: CPUs {span} ({len(cpus)})" + (", idle scheduling" if idle else "")


def main(argv):
    if argv[:1] == ["--"]:
        argv = argv[1:]
    if not argv:
        print(__doc__.strip())
        return 2
    print(apply(), file=sys.stderr, flush=True)
    os.environ["ROOD_PLAY"] = "1"  # a run.py inside keeps its play-mode limits
    os.execvp(argv[0], argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
