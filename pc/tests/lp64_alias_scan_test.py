#!/usr/bin/env python3
"""pc/tools/lp64_alias_scan.py: the import-alias check finds the bug class
(a host-pointer import that is a psx_ptr field of a guest struct import) on
synthetic data, ignores what cannot alias, and, given a 64-bit build
(build_boot.py --arch 64), finds no HAZARD in it.

    python3 pc/tests/lp64_alias_scan_test.py [--build pc/build/boot64]
"""

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import lp64_alias_scan  # noqa: E402


def declaration(type_, size, ps1_size, pointer_kind=None, fields=None, obj="unit"):
    d = {"type": type_, "size": size, "ps1_size": ps1_size, "pointer_kind": pointer_kind, "object": obj,
         "pointer_words": pointer_kind == "pointer" and not fields}
    if fields:
        d["fields"] = fields
        d["leaf_ps1_size"] = ps1_size
    return d


def entry(address, moved, declarations, users=("battle",), group=""):
    return {"address": address, "moved": moved, "users": list(users), "group": group,
            "declarations": declarations}


STRUCT_FIELDS = [
    {"name": "unk0", "offset": 0, "size": 4, "pointer": None},
    {"name": "unkD0", "offset": 0xD0, "size": 4, "pointer": "psx_ptr"},
    {"name": "unkD4", "offset": 0xD4, "size": 4, "pointer": None},
]


def synthetic():
    struct = declaration("D_800F569C_t", 0xD8, 0xD8, "psx_ptr", STRUCT_FIELDS)
    return {
        # The 6F014.c bug: D_800F5798 = D_800F56C8.unkD0 declared as a host pointer.
        "D_800F56C8": entry(0x800F56C8, False, [struct]),
        "D_800F5798": entry(0x800F5798, True, [declaration("func_800CF0E8_t *", 8, 4, "pointer")]),
        # A 4-byte integer view of the same struct: both stay guest, no hazard.
        "D_800F579C": entry(0x800F579C, False, [declaration("int", 4, 4)]),
        # Two PLG effects loaded at one address never coexist.
        "D_800FC000__plg001": entry(0x800FC000, False, [declaration("SVECTOR [16]", 128, 128)], ["plg001"]),
        "D_800FC010__plg002": entry(0x800FC010, True, [declaration("void *", 8, 4, "pointer")], ["plg002"]),
        # ENDING's group is separate from BATTLE's.
        "D_80070000": entry(0x80070000, False, [declaration("int [8]", 32, 32)]),
        "D_80070008": entry(0x80070008, True, [declaration("void *", 8, 4, "pointer")], ["ending"], "ending"),
    }


def check(condition, message, failures):
    print(("ok   " if condition else "FAIL ") + message)
    if not condition:
        failures.append(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="pc/build/boot64")
    args = parser.parse_args()
    failures = []

    findings = lp64_alias_scan.scan(synthetic(), review=True)
    hazards = [f for f in findings if f["severity"] == "HAZARD"]
    check([(f["member"], f["container"], f["offset"]) for f in hazards] == [("D_800F5798", "D_800F56C8", 0xD0)],
          "synthetic: exactly the moved field alias D_800F5798 = D_800F56C8+0xd0 is a HAZARD", failures)
    check(bool(hazards) and "unkD0" in hazards[0]["where"], "synthetic: the hazard names the field (unkD0)", failures)
    check(not any(f["member"] == "D_800F579C" and f["severity"] == "HAZARD" for f in findings),
          "synthetic: an integer view that stays guest is not a hazard", failures)
    check(not any("plg" in f["member"] or f["member"] == "D_80070008" for f in findings),
          "synthetic: imports of overlays never loaded together are ignored", failures)

    path = ROOT / args.build / "lp64_imports.json"
    if path.exists():
        imports = json.loads(path.read_text())
        real = [f for f in lp64_alias_scan.scan(imports) if f["severity"] == "HAZARD"]
        for f in real:
            print(f"     {f['member']} = {f['where']}: {f['why']}")
        check(not real, f"{args.build}: no import aliases a field of another across the LP64 move "
                        f"({len(imports)} data imports)", failures)
    else:
        print(f"skip {path} is missing (no 64-bit build)")

    print("PASS" if not failures else f"FAIL ({len(failures)})")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
