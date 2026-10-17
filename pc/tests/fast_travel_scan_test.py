#!/usr/bin/env python3
"""Fast travel's script scan (pc/tools/fast_travel_scan.py, rule R9 in pc/docs/Fast_Travel.md).

    python3 pc/tests/fast_travel_scan_test.py

Every room script (data/MAP/MAPnnn.MPD) and event script (data/EVENT/nnnn.EVT)
walks op by op to its end with the interpreter's op lengths; every Circle's
own room script records it (savePointStates[i] = 1, all 35); and no finding is
actionable unless pc/assets/fast_travel/blocks.json mirrors it (a destination
blocked for it, reviewed by hand). Skipped (77) without data/."""
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BLOCKS = ROOT / "pc/assets/fast_travel/blocks.json"


def main():
    if not (ROOT / "data/MAP").is_dir() or not (ROOT / "data/BATTLE/BATTLE.PRG").exists():
        print("SKIP: no data/ (the disc's files)")
        return 77
    out = subprocess.run([sys.executable, str(ROOT / "pc/tools/fast_travel_scan.py")], capture_output=True,
                         text=True, check=True).stdout
    scan = json.loads(out)
    failures = []

    def check(ok, message):
        print(("ok   " if ok else "FAIL ") + message)
        if not ok:
            failures.append(message)

    check(scan["scripts"] > 800 and not scan["desync"],
          f"{scan['scripts']} scripts walked to their end (desync: {scan['desync'][:4]})")
    recorded = {f["point"] for f in scan["findings"] if f["kind"] == "records"}
    check(recorded == set(range(35)), f"every Circle's room script records it ({sorted(set(range(35)) - recorded)} missing)")
    blocks = json.loads(BLOCKS.read_text()) if BLOCKS.exists() else {"blocks": []}
    covered = {(b["file"], b["offset"]) for b in blocks["blocks"]}
    actionable = [f for f in scan["findings"] if f["actionable"]]
    loose = [f for f in actionable if (f["file"], f["offset"]) not in covered]
    for f in actionable:
        print(f"info: actionable: {f['file']}@{f['offset']:#x} {f['what']}")
    check(not loose, f"every actionable finding is mirrored in {BLOCKS.relative_to(ROOT)} ({loose[:4]})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
