#!/usr/bin/env python3
"""The bigger bag (build_boot.py --callo --big-inventory container) loading a real save.

Continues from a card's first save in the bigger-bag build and checks the bag it holds at
vs_main_bigInventory / vs_main_bigInventoryIndices (config/big_inventory_addrs.txt) against what
src/BATTLE/BATTLE.PRG/mod/inventory.h's vs_inventory_load makes of the same save offline
(pc/tests/big_inventory_expect.c, built like big_inventory_probe.c). That checks the game's own
wiring (TITLE's load, the addresses) on top of the probe's random cases.

    ROOD_BINARY=pc/build/boot64_callo_biginv/rood_battle_probe \\
        python3 pc/tests/big_inventory_smoke.py --card CARD --decoded SAVE --expected EXPECTED.bin

CARD: a card whose first save is the one to load; SAVE: that save decoded (tools/mod/vs_save.py,
rood-reverse); EXPECTED.bin: big_inventory_expect SAVE EXPECTED.bin.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import Game  # noqa: E402

BAG, ORDER = 0x80062000, 0x80063D00
BAG_SIZE, ORDER_SIZE = 0x1D00, 0x120
CATEGORIES = [  # name, record size, offset, count (doubled bag)
    ("weapons", 0x20, 0x0000, 16), ("shields", 0x30, 0x0200, 16), ("blades", 0x2C, 0x0500, 32),
    ("grips", 0x10, 0x0A80, 32), ("armor", 0x28, 0x0C80, 32), ("gems", 0x1C, 0x1180, 96),
]


def read(g, address, size):
    out = b""
    while len(out) < size:
        n = min(0x400, size - len(out))
        out += g.read(address + len(out), n)
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--card", required=True)
    p.add_argument("--expected", required=True)
    p.add_argument("--out", default="/tmp/big-inventory-smoke")
    a = p.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    expected = pathlib.Path(a.expected).read_bytes()

    g = Game.launch(card=a.card, log=out / "game.log", fast=True)
    try:
        g.wait_title(timeout=180)
        g.press("start", vsyncs=4, settle=90)  # the title menu
        for _ in range(3):  # Continue, the card, its first save
            g.press("circle", vsyncs=3, settle=90)
        g.run(600)
        state = g.state()
        print(f"after Continue: zone {state.get('zone')} room {state.get('room')}")
        bag = read(g, BAG, BAG_SIZE)
        order = read(g, ORDER, ORDER_SIZE)
    finally:
        g.close()

    got = bag + order
    failures = 0
    if not any(bag):
        print("FAIL: the bag is empty (the save wasn't loaded?)")
        return 1
    for name, size, offset, count in CATEGORIES:
        mine = got[offset:offset + size * count]
        want = expected[offset:offset + size * count]
        used = sum(1 for i in range(count) if any(want[i * size:(i + 1) * size]))
        second = sum(1 for i in range(count // 2, count) if any(want[i * size:(i + 1) * size]))
        ok = mine == want
        failures += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {used} records ({second} in the second half)")
    ok = got[BAG_SIZE:] == expected[BAG_SIZE:]
    failures += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} order lists")
    ok = got[0x1C00:BAG_SIZE] == expected[0x1C00:BAG_SIZE]
    print(f"  {'ok  ' if ok else 'FAIL'} misc items")
    failures += not ok
    if failures:
        (out / "got.bin").write_bytes(got)
        print(f"FAIL: {failures} part(s) differ (got.bin in {out})")
        return 1
    print("PASS: the bigger bag after Continue is what vs_inventory_load makes of the save")
    return 0


if __name__ == "__main__":
    sys.exit(main())
