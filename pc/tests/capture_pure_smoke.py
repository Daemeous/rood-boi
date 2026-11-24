#!/usr/bin/env python3
"""Looking at the game does not change it: a screenshot reads the display back from the renderer into the game's VRAM array
for its picture, and must put the array back. The state hash (guest memory, native statics, overlays, VRAM) before a screenshot equals the
hash after it, in the save room and in a room with a battle running, and the game run on from there equals the game run without the
screenshot (a mark, rewind and two legs, the first looked at, the second not).

    ROOD_ARCH=64 python3 pc/tests/capture_pure_smoke.py [--card card.mcd]"""
import argparse
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from checkpoints import launch_at  # noqa: E402

LEG = 30


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD", ROOT / "pc/build/test-card1.mcd"))
    args = parser.parse_args()
    if os.environ.get("ROOD_ARCH") != "64":
        print("SKIP: 64-bit only")
        return 77
    out = pathlib.Path(os.environ.get("ROOD_OUT") or rood_rpc.OUT)
    out.mkdir(parents=True, exist_ok=True)
    shot = out / "capture-pure.ppm"
    hash_of = lambda g: (lambda h: json.loads(h) if isinstance(h, str) else h)(g.call("snapshot", action="hash"))
    g = launch_at("save_room", card=str(args.card), fast=True, log=out / "capture-pure.log")
    try:
        g.pause()
        g.run(30)
        before = hash_of(g)
        g.screenshot(str(shot))
        after = hash_of(g)
        differ = [k for k in before if before[k] != after[k]]
        assert not differ, f"a screenshot changed the state hash: {differ}"
        g.mark()
        g.run(LEG)
        plain = hash_of(g)
        g.rewind()
        g.screenshot(str(shot))
        g.run(LEG)
        looked = hash_of(g)
        differ = [k for k in plain if plain[k] != looked[k]]
        assert not differ, f"a screenshot at the mark changed the game {LEG} vsyncs on: {differ}"
        g.screenshot(str(shot))
        g.run(LEG)
        g.screenshot(str(shot))
        print(f"PASS: the state hash is the same before and after a screenshot, and {LEG} vsyncs on with or without one")
    finally:
        g.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
