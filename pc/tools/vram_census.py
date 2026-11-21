#!/usr/bin/env python3
"""The VRAM role census: what touches each 64 x 64 block of the 1024 x 512 array (RPC vram_census), over a route.

    python3 pc/tools/vram_census.py [--menus ROUTE[,ROUTE...]] [--rooms "zone,room;zone,room"] [--no-fight] [--json OUT] [BINARY]

Plays build_compare's own routes (one game each) on BINARY (default pc/build/boot64/rood_battle_probe) with the census on, merges what each game
reported (BUILD_COMPARE_VRAM_CENSUS) and prints, per 64 x 64 block (x, y in halfwords), the counts by what touched it and the role they give it
(pc/docs/plans/2026-11-17-vram.md, T5-0). A count is an event or a primitive, not a pixel:
  upload, fill, copy_dst, copy_src, store    the transfers that wrote or read the block
  raster                                      rectangles the primitives drew there (the renderer's; the name is the CPU rasterizer's, retired in T5-6)
  texel, clut                                 primitives sampling texels or a CLUT row there
  display                                     the display rectangle covered it (one per vsync)
Roles: texture (texels sampled), clut (CLUT rows sampled), frame (display or rasterized), scratch (transferred only), unused."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
ROOMS = "40,3;10,1;9,0;22,0;48,10;53,1;32,3;24,4"
ROUTES = "dialogue,story_evt-0001,story_evt-0069,items,status,title,areamap,storage,data,map,platforms,magic,ending,forest,transition"
KINDS = ("upload", "fill", "copy_dst", "copy_src", "store", "raster", "texel", "clut", "display")


def run(binary, args, env_extra):
    """One game of the route (build_compare's own play children): the census it reported."""
    card = str(ROOT / "pc/build/test-card1.mcd")
    with tempfile.TemporaryDirectory(prefix="vcensus-") as census, tempfile.TemporaryDirectory(prefix="vcensus-game-") as out:
        env = dict(os.environ, BUILD_COMPARE_VRAM_CENSUS=census, **env_extra)
        child = (["--play", binary, "--seconds", "30"] if not args else ["--play-menus", binary] + args)
        subprocess.run([sys.executable, str(ROOT / "pc/tools/build_compare.py")] + child + ["--card", card, "--out", out], env=env,
                       capture_output=True, text=True)
        reports = [json.loads(f.read_text()) for f in sorted(pathlib.Path(census).glob("*.json"))]
    return reports[0] if reports else None


def role(counts):
    """The roles a block has: the texture, CLUT and frame uses at once (a block can be several)."""
    roles = []
    if counts.get("texel"):
        roles.append("texture")
    if counts.get("clut"):
        roles.append("clut")
    if counts.get("display") or counts.get("raster"):
        roles.append("frame")
    if not roles and any(counts.get(k) for k in ("upload", "fill", "copy_dst", "copy_src", "store")):
        roles.append("scratch")
    return "+".join(roles) or "unused"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary", nargs="?", default=os.environ.get("ROOD_BINARY", str(ROOT / "pc/build/boot64/rood_battle_probe")))
    ap.add_argument("--menus", default=ROUTES, help="routes, comma-separated (default: the 13 of the gate, forest and transition)")
    ap.add_argument("--no-fight", action="store_true")
    ap.add_argument("--rooms", nargs="?", const=ROOMS, default="", help="also the fight in each of these rooms")
    ap.add_argument("--json", help="write the merged table here")
    args = ap.parse_args()
    merged, by_route = {}, {}

    def add(label, report):
        if not report:
            print(f"{label}: no census (the build has no vram_census?)")
            return
        by_route[label] = report["cells"]
        for cell, counts in report["cells"].items():
            m = merged.setdefault(cell, {})
            for k, v in counts.items():
                m[k] = m.get(k, 0) + v
        print(f"{label}: {report['frames']} vsyncs, {len(report['cells'])} blocks touched")

    if not args.no_fight:
        add("fight", run(args.binary, [], {}))
    for room in filter(None, args.rooms.split(";")):
        add(f"fight in room {room}", run(args.binary, [], {"BUILD_COMPARE_ROOM": room}))
    for route in filter(None, args.menus.split(",")):
        add(route, run(args.binary, ["--menus", route], {}))
    print(f"{'block':>9}  {'role':<18}" + "".join(f"{k:>9}" for k in KINDS))
    for cell in sorted(merged, key=lambda c: (int(c.split(",")[1]), int(c.split(",")[0]))):
        counts = merged[cell]
        print(f"{cell:>9}  {role(counts):<18}" + "".join(f"{counts.get(k, 0):>9}" for k in KINDS))
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps({"merged": merged, "routes": by_route}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
