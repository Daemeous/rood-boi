#!/usr/bin/env python3
"""Save states are exact: booting to a checkpoint and resuming its snapshot
give the same game.

    python3 pc/tests/snapshot_equivalence.py [--card pc/save/card1.mcd] [checkpoint...]

For each test checkpoint (pc/tests/checkpoints.py: title, opening, save_room
with a card), with deterministic scanlines (ROOD_DETERMINISTIC=1) and frame
interpolation off in both processes: boot and play the checkpoint's input,
save a snapshot WARMUP vsyncs before the checkpoint, play on WARMUP + FRAMES
vsyncs recording each vsync's guest RAM, scratchpad, I/O page, VRAM and
overlay statics hashes and presented frame; then resume the snapshot in a
new process and record the same. Passes when every recorded hash is equal,
the presented frames are identical from within the warm-up on, and the two
end states (snapshots) are byte-identical in guest memory, native game
statics and SPU (pc/tests/snapshot_diff.py; only the wall-clock time of the
last vsync and the host frames on the game stack may differ).

Under run.py this is the first job: it (re)makes the cache's snapshots
($ROOD_TEST_SNAPSHOT_DIR); the tests resume only the ones that pass here.
The checkpoints are validated in parallel (save_room first: most suites
wait for it), and each suite waits only for its own checkpoint. A
checkpoint already validated for this build (the cache directory is keyed
by the game binary and the disc data) by the same harness
(checkpoints.harness_key) is not validated again; --again does.
"""
import argparse
import fcntl
import os
import pathlib
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tests"))
import checkpoints  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402
from rood_rpc import OUT  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="*", help=f"checkpoints (default: all of {', '.join(checkpoints.CHECKPOINTS)})")
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--again", action="store_true", help="validate checkpoints already validated for this build")
    args = parser.parse_args()
    if not os.environ.get("ROOD_TEST_SNAPSHOT_DIR"):
        os.environ["ROOD_TEST_SNAPSHOT_DIR"] = tempfile.mkdtemp(prefix="snapshots-", dir=OUT)
    card = args.card if pathlib.Path(args.card).exists() else None
    # save_room first: most suites start from it
    names = sorted(args.names or list(checkpoints.CHECKPOINTS), key=lambda name: name != "save_room")
    failures, skipped = [], []

    def check(name):
        needs_card = checkpoints.CHECKPOINTS[name][0]
        directory, snap, ok, bad = checkpoints._paths(name, card if needs_card else None)
        directory.mkdir(parents=True, exist_ok=True)
        with open(directory / f"{snap.stem}.lock", "w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            known = checkpoints.verdict(name, card if needs_card else None)
            if known and not args.again:
                print(f"{'ok:  ' if known == 'ok' else 'FAIL:'} {name}: validated for this build already "
                      f"({(ok if known == 'ok' else bad).name}; --again validates it again)")
                return name, known == "ok"
            try:
                made, report = checkpoints.validate(name, card if needs_card else None)
            except Exception as e:
                bad.write_text(f"harness {checkpoints.harness_key()}\n{name}: {e}\n")
                made, report = None, [f"{name}: {e}"]
                print(report[0])
        print(("ok:   " if made else "FAIL: ") + f"{name} resumes exactly" + ("" if made else " (its tests boot)"))
        return name, bool(made)

    todo = []
    for name in names:
        if checkpoints.CHECKPOINTS[name][0] and not card:
            skipped.append(name)
            print(f"skip: {name} needs a memory card ({args.card})")
        else:
            todo.append(name)
    with ThreadPoolExecutor(max(1, len(todo))) as pool:  # each checkpoint its own games
        for name, made in pool.map(check, todo):
            if not made:
                failures.append(name)
    print("FAIL" if failures else "PASS", f"({', '.join(failures)} not exact)" if failures else
          f"({len(names) - len(skipped)} checkpoints exact)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
