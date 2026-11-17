#!/usr/bin/env python3
"""The port arena (memory lane S6: game/common/port_arena.zig; decoded file objects, never the game heap).

    python3 pc/tests/port_arena_smoke.py [--card pc/save/card1.mcd]

In the save room (the battle overlay has loaded Ashley's weapon file, which decodes into the arena):
  - the snapshot range list names "port" (so snapshots, the Oracle digest and the in-process rewind carry it)
  - the header is sound: the bump offset and the high-water mark are past the header (the weapon file was decoded) and
    equal (nothing was released out of order), and every root slot is empty (the decoder released its record)
  - a word written into the arena after the in-process checkpoint comes back at rewind (the range is restored)
The arena is raw memory by design (it has no symbol), so this test reads it by address."""
import argparse
import pathlib
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from checkpoints import launch_at  # noqa: E402

HEADER = 0x130  # port_arena.zig: top, high, 8 roots, 64 free-list heads, rounded to 16


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():
        import subprocess
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(tempfile.mkdtemp(prefix="port-arena-"))
    shutil.copyfile(args.card, out / "card1.mcd")
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log",
                     extra_env={"ROOD_WINDOW": "640x480", "ROOD_GI": "0"})
    try:
        ranges = game.call("snapshot", action="ranges")["ranges"]
        port = [r for r in ranges if r["name"] == "port"]
        check(len(port) == 1 and port[0]["size"] >= 0x10000, f"the snapshot ranges hold 'port': {port}")
        base, size = port[0]["start"], port[0]["size"]
        head = bytes.fromhex(game.call("read", address=base, length=0x40)["hex"])
        top, high = struct.unpack_from("<II", head, 0)
        roots = struct.unpack_from("<8I", head, 8)
        check(top > HEADER and top == high, f"the weapon file was decoded (top {top:#x}, high {high:#x})")
        check(not any(roots), f"every root slot is empty (the decoder released its record): {roots}")
        game.mark()
        spot = base + size - 16
        before = bytes.fromhex(game.call("read", address=spot, length=16)["hex"])
        game.call("write", address=spot, hex=("a5" * 16))
        game.run(2)
        game.rewind()
        after = bytes.fromhex(game.call("read", address=spot, length=16)["hex"])
        check(after == before, "a write into the arena after the checkpoint is undone by the rewind")
    finally:
        game.close()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
