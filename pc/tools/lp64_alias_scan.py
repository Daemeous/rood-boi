#!/usr/bin/env python3
"""Find data imports that alias a field of another import on the 64-bit build.

splat names words inside structs and arrays separately, so the decompiled
units often reach one guest word under two names: D_800F5798 is
D_800F56C8.unkD0 (0x800F56C8 + 0xD0), D_800F188C is _zoneContext.zndEnemies.
On the PS1 and the 32-bit build both names are the same 4 bytes. On LP64,
build_boot.py moves an import whose declared type holds pointers (8 bytes
each) to a native slot, while a struct import whose pointers are psx_ptr
(4-byte guest addresses) keeps its PS1 address: the two names become two
storages, and a write through one never reaches the other.

This tool reads the mover's view of every data import
(pc/build/boot64/lp64_imports.json, written by `build_boot.py --arch 64`:
PS1 addresses, which imports moved, and each object's DWARF type with its
PS1 layout and field offsets from pc/tools/import_types.py) and reports each
import whose address falls inside another import's PS1 extent (a complete
struct or array type) in the same overlay group where:

  HAZARD  one side moved to a native slot and the other did not (the bug
          above; build_boot.py already refuses the other direction, a guest
          import inside a moved region, and merges overlapping moved ones);
  REVIEW  the member's declared type disagrees with the container's field at
          that offset in size or pointer-ness (both stay guest: same bytes,
          but a pointer field read as an integer or vice versa is worth a look).

Fix a HAZARD in the source under ROOD_PC by reaching the word through the
containing import (psx_get(container.field)) and not referencing the alias
name at all on the PC, so it is no longer an import.

  python3 pc/tools/build_boot.py --arch 64
  pc/tools/memcap python3 pc/tools/lp64_alias_scan.py [--review] [--json out.json]

decl_mismatch.py --review runs this check too. Exit status 1 when a HAZARD
not listed in BENIGN is found.
"""

import argparse
import bisect
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# (member import, reason). Reviewed hazards that cannot misbehave.
BENIGN = {}

# (member import, container import): reason. Reviewed REVIEW findings: both
# names stay at the PS1 address (the same bytes on every build) and the member
# is a deliberate wider view of the container's data, not a wrong declaration
# (wrong declarations are fixed instead; pc/docs/LP64_Audit.md, "Import
# aliases"). Only REVIEWs are silenced: a HAZARD for the pair still reports.
REVIEWED = {
    ("D_8005046C", "D_80050468"):
        "5BF94.c passes the battle timer's frames/seconds/minutes/hours bytes to _renderTimer(int*) "
        "as one word; plain bytes, no pointers",
    ("_dsControlBuf", "vs_main_disk"):
        "the DslSetmode parameter: main.c stores the mode in vs_main_disk.cdLoc.minute and passes "
        "_dsControlBuf (that byte) to DsControlB, which reads only param[0] (pc/src/cd_drive.c)",
    ("D_800F18C8", "_zoneContext"):
        "146C.c's view of _zoneContext+0x48..0x5f: shorts over unk48..unk54, unk10/unk14 over "
        "mpdCdFile/mpdData (4-byte psx_ptr); its only pointer-field write is unk14 = 0 (mpdData = NULL "
        "after the free)",
    ("D_800F4438", "D_800F2458"):
        "the shared scale VECTOR after the 63 bone matrices (D_800F3C58[63]); 30DB0.c's "
        "D_800F2458.unk1FE0 is the low half of its vx (a u_short load that matches); no pointers",
    ("D_800F5234", "D_800F5230"):
        "func_800CEF74 only takes its address and +0xc; func_800CE714 reads the 12-byte records there "
        "(bytes 0x4..0x1b of the effect parameter block D_800F5230); no pointers are read through it",
}

# Containers whose declaration is larger than the data the game addresses
# through them: (PS1 extent in bytes, reason).
EXTENTS = {
    # The 12 sound channels are D_80035910[10] followed by SfxSoundChannels[2]:
    # 25AC.c only reaches SfxSoundChannels[-10..1]; _cdlAtv follows at +0x220.
    "SfxSoundChannels": (0x220, "declared [12], the game indexes SfxSoundChannels[-10..1]"),
}


def extent(name, entry):
    """PS1 size of the import's largest complete declaration (0 if none)."""
    if name in EXTENTS:
        return EXTENTS[name][0]
    return max((d.get("ps1_size") or 0 for d in entry["declarations"] if d.get("size")), default=0)


def exclusive(a, b, imports):
    """Imports of two overlays that are never loaded together (two PLG
    effects at the same address, or names build_boot.py renamed to
    name__overlay for different overlays) do not alias each other."""
    key_a, key_b = (n.rpartition("__")[2] if "__" in n else None for n in (a, b))
    if key_a and key_b and key_a != key_b:
        return True
    users_a, users_b = set(imports[a]["users"]), set(imports[b]["users"])
    plg = lambda users: users and all(u.startswith("plg") for u in users)
    return plg(users_a) and plg(users_b) and not users_a & users_b


def field_at(entry, offset):
    """Name of the deepest field of the container that holds offset."""
    best = None
    for declaration in entry["declarations"]:
        fields = declaration.get("fields")
        if not fields:
            continue
        # Arrays of structs: fields describe element 0.
        leaf = declaration.get("leaf_ps1_size") or 0
        index, inner = (offset // leaf, offset % leaf) if leaf and declaration.get("element") else (None, offset)
        for field in fields:
            start, size = field["offset"], field["size"]
            stride = field.get("stride")
            if not (start <= inner < start + max(size, 1)):
                continue
            name = field["name"]
            within = inner - start
            if stride:
                name += f"[{within // stride}]"
                within %= stride
            # Later entries are nested deeper.
            best = (f"[{index}]." if index is not None else "") + name, within, field
        if best:
            return best
    return None


def declared_types(entry):
    return sorted({d.get("type") for d in entry["declarations"]})


def scan(imports, review=False):
    findings = []
    by_group = {}
    for name, entry in imports.items():
        by_group.setdefault(entry["group"], []).append((entry["address"], name))
    for group, members in by_group.items():
        members.sort()
        addresses = [a for a, _ in members]
        for address, container in members:
            entry = imports[container]
            size = extent(container, entry)
            if size <= 4:
                continue
            low = bisect.bisect_left(addresses, address)
            high = bisect.bisect_left(addresses, address + size)
            for member_address, member in members[low:high]:
                if member == container or exclusive(member, container, imports):
                    continue
                other = imports[member]
                # A member as large as the container is the container's twin.
                if member_address == address and extent(member, other) >= size:
                    continue
                offset = member_address - address
                field = field_at(entry, offset)
                where = f"{container}+{offset:#x}" + (f" ({container}{'.' if not field[0].startswith('[') else ''}"
                                                      f"{field[0]}{f' +{field[1]:#x}' if field[1] else ''})"
                                                      if field else "")
                severity, why = None, None
                if entry["moved"] != other["moved"]:
                    severity = "HAZARD"
                    why = (f"{member} {'moved to a native slot' if other['moved'] else 'stays at its PS1 address'}, "
                           f"{container} {'moved' if entry['moved'] else 'stays at its PS1 address'}")
                elif not entry["moved"] and field:
                    leaf = field[2]
                    kinds = {d.get("pointer_kind") for d in other["declarations"]}
                    sizes = {d.get("ps1_size") for d in other["declarations"] if d.get("size")}
                    if leaf.get("pointer") and None in kinds and sizes and max(sizes) <= 4 and leaf["size"] == 4:
                        why = "a guest pointer field declared as an integer"
                    elif sizes and field[1] + max(sizes) > leaf["size"] and not leaf.get("stride"):
                        why = f"declared {max(sizes)} bytes over a {leaf['size']}-byte field"
                    if why:
                        severity = "REVIEW"
                if not severity:
                    continue
                reason = None
                if severity == "HAZARD" and member in BENIGN:
                    severity, reason = "benign", BENIGN[member]
                elif severity == "REVIEW" and (member, container) in REVIEWED:
                    severity, reason = "benign", REVIEWED[member, container]
                if severity == "REVIEW" and not review:
                    continue
                findings.append({
                    "severity": severity, "member": member, "container": container, "offset": offset,
                    "where": where, "why": why, "benign_reason": reason, "group": group or "main",
                    "member_types": declared_types(other), "container_types": declared_types(entry),
                    "objects": sorted({d["object"] for d in other["declarations"]}),
                })
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="pc/build/boot64", help="output of build_boot.py --arch 64")
    parser.add_argument("--review", action="store_true", help="also print REVIEW findings")
    parser.add_argument("--json", help="write every finding here")
    args = parser.parse_args()
    path = ROOT / args.build / "lp64_imports.json"
    if not path.exists():
        sys.exit(f"{path} is missing: run python3 pc/tools/build_boot.py --arch 64 first")
    imports = json.loads(path.read_text())
    findings = scan(imports, review=args.review)
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(findings, indent=1) + "\n")
    order = {"HAZARD": 0, "REVIEW": 1, "benign": 2}
    for f in sorted(findings, key=lambda f: (order[f["severity"]], f["container"], f["offset"])):
        print(f"{f['severity']:6} {f['member']} = {f['where']} [{f['group']}]: {f['why']}")
        if f["benign_reason"]:
            print(f"       reviewed: {f['benign_reason']}")
        print(f"       {f['member']}: {', '.join(f['member_types'])} in {', '.join(f['objects'])}")
        print(f"       {f['container']}: {', '.join(f['container_types'])}")
    seen = {(f["member"], f["container"]) for f in findings}
    for member, container in sorted(set(REVIEWED) - seen):
        print(f"note: REVIEWED entry {member} in {container} no longer matches a finding: remove it")
    counts = {k: sum(f["severity"] == k for f in findings) for k in order}
    print(f"lp64_alias_scan: {len(imports)} data imports; HAZARD {counts['HAZARD']}"
          + (f", REVIEW {counts['REVIEW']}" if args.review else "") + f", benign {counts['benign']}")
    sys.exit(1 if counts["HAZARD"] else 0)


if __name__ == "__main__":
    main()
