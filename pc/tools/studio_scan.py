#!/usr/bin/env python3
"""Write pc/assets/studio/catalog.json: the models the model studio (pc/src/studio.cpp) can summon, as ids only.

    python3 pc/tools/studio_scan.py [--data data] [--out pc/assets/studio/catalog.json] [--check]

  characters  every OBJ/NN.SHP of the disc's file tree (the model numbers the loader files by: OBJ/%02X.SHP)
  weapons     every OBJ/NN.WEP
  enemies     the Bestiary's: SMALL/MON.BIN's records (a zud number each), each zud number's file taken from
              MENU9.PRG's table (src/MENU/MENU9.PRG/592C.c _zudLbas: VS_Z003U00_ZUD_LBA -> Z003U00)

Ids and file names only: nothing of the game's text or data is written (names come from the player's own disc when the
studio runs). --check compares the file with what would be written (a test)."""
import argparse
import json
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def scan(data):
    obj = data / "OBJ"
    characters = sorted(int(p.stem, 16) for p in obj.glob("*.SHP"))
    weapons = sorted(int(p.stem, 16) for p in obj.glob("*.WEP"))
    source = (ROOT / "src/MENU/MENU9.PRG/592C.c").read_text()
    table = re.search(r"_zudLbas\[\]\s*=\s*\{(.*?)\};", source, re.S).group(1)
    files = re.findall(r"VS_(Z\d+U\d+)_ZUD_LBA", table)
    mon = (data / "SMALL" / "MON.BIN").read_bytes()
    enemies, seen = [], set()
    for i in range(78):
        zud = struct.unpack_from("<h", mon, i * 44)[0]
        if 0 <= zud < len(files) and zud not in seen:
            seen.add(zud)
            enemies.append({"id": zud, "file": files[zud]})
    return {"format": 1, "characters": characters, "enemies": enemies, "weapons": weapons}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=str(ROOT / "data"))
    parser.add_argument("--out", default=str(ROOT / "pc/assets/studio/catalog.json"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalog = scan(pathlib.Path(args.data))
    text = ('{\n "format": 1,\n "characters": ' + json.dumps(catalog["characters"]) + ',\n "enemies": [\n'
            + ",\n".join("  " + json.dumps(e) for e in catalog["enemies"]) + '\n ],\n "weapons": '
            + json.dumps(catalog["weapons"]) + "\n}\n")
    out = pathlib.Path(args.out)
    if args.check:
        same = out.exists() and out.read_text() == text
        print("catalog.json is current" if same else "catalog.json differs from the scan")
        return 0 if same else 1
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    print(f"{out}: {len(catalog['characters'])} characters, {len(catalog['enemies'])} enemies, {len(catalog['weapons'])} weapons")
    return 0


if __name__ == "__main__":
    sys.exit(main())
