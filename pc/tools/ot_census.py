#!/usr/bin/env python3
"""The ordering-table census: what the adopted tables hold, by slot and kind (RPC ot_census), over a route.

    python3 pc/tools/ot_census.py [--menus ROUTE[,ROUTE...]] [--rooms "zone,room;zone,room"] [--no-fight] [BINARY]

Plays build_compare's own routes (one game each) on BINARY (default pc/build/boot64/rood_battle_probe) with the census on, merges what each
game reported (BUILD_COMPARE_CENSUS), and prints, per route, the packets left in the tables: raw packets (a draw mode, a
polygon, a line, a rectangle or sprite, other) by slot with the first one seen, and the VRAM copies apart; the packets drawn at once (DrawPrim: the title) in no table, apart (slot -1000). The gate for taking
the ordering table out (S6): "raw" reads 0 on every route; the copies (DR_MOVE and the loads and stores) are T5's.
Exit status 0 when no route has a raw packet, 1 otherwise."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
RAW = ("mode", "polygon", "line", "rect", "other")
ROOMS = "40,3;10,1;9,0;22,0;48,10;53,1;32,3;24,4"  # (the fight in each: build_compare's --rooms)
ROUTES = "dialogue,story_evt-0001,story_evt-0069,items,status,title,areamap,storage,data,map,platforms,magic,forest,transition"


def run(binary, args, label, env_extra):
    """One game of the route (build_compare's own play children: the route as the comparison plays it)."""
    card = str(ROOT / "pc/build/test-card1.mcd")
    with tempfile.TemporaryDirectory(prefix="census-") as census, tempfile.TemporaryDirectory(prefix="census-game-") as out:
        env = dict(os.environ, BUILD_COMPARE_CENSUS=census, **env_extra)
        child = (["--play", binary, "--seconds", "30", "--width", "8"] if not args else ["--play-menus", binary] + args)
        subprocess.run([sys.executable, str(ROOT / "pc/tools/build_compare.py")] + child + ["--card", card, "--out", out], env=env,
                       capture_output=True, text=True)
        reports = [json.loads(f.read_text()) for f in sorted(pathlib.Path(census).glob("*.json"))]
    return reports[0] if reports else None


def show(label, r):
    if not r:
        print(f"{label}: no census (the build has no ot_census?)")
        return 0
    k = r["kinds"]
    raw = sum(k.get(n, 0) for n in RAW)
    print(f"{label}: {r['tables']} tables; item markers {k['item_marker']}, native markers {k['native_marker']}, "
          f"raw {raw} ({', '.join(f'{n} {k[n]}' for n in RAW if k[n])}), vram copies {k['vram_copy']}, no-ops {k['nop']}")
    order = r.get("order")
    if order:
        print(f"    key order against the table walk: {order['tables']} tables, {order['bad']} differ, {order['unknown']} of unknown table"
              + (f" (the first: walk {order['first'][0]} runs, keys {order['first'][1]}, differ at run {order['first'][2]}: walk {order['first'][3]}, keys {order['first'][4]})" if order['bad'] else ""))
    imm = r.get("immediate", {})
    if any(imm.values()):
        print(f"    drawn at once (DrawPrim, in no table: not the gate's): {', '.join(f'{n} {v}' for n, v in imm.items() if v)}")
    for row in r["rows"]:
        if row["kind"] in RAW or row["kind"] == "vram_copy":
            print(f"    slot {row['slot']:>5} {row['kind']:<8} x{row['n']:<8} first words {' '.join('%08x' % w for w in row['words'])}")
    return raw


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary", nargs="?", default=os.environ.get("ROOD_BINARY", str(ROOT / "pc/build/boot64/rood_battle_probe")))
    ap.add_argument("--menus", default=ROUTES, help="routes, comma-separated (default: all 14)")
    ap.add_argument("--no-fight", action="store_true")
    ap.add_argument("--rooms", nargs="?", const=ROOMS, default="", help="also the fight in each of these rooms (zone,room;zone,room; default the eight of the gate)")
    args = ap.parse_args()
    raw = 0
    if not args.no_fight:
        raw += show("fight", run(args.binary, [], "fight", {}))
    for room in filter(None, args.rooms.split(";")):
        raw += show(f"fight in room {room}", run(args.binary, [], f"room {room}", {"BUILD_COMPARE_ROOM": room}))
    for route in args.menus.split(","):
        raw += show(route, run(args.binary, ["--menus", route], route, {}))
    print("gate:", "OPEN (0 raw packets on every route)" if not raw else f"closed ({raw} raw packets)")
    return 1 if raw else 0


if __name__ == "__main__":
    sys.exit(main())
