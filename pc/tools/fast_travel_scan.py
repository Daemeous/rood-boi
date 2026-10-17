#!/usr/bin/env python3
"""Fast travel's script scan (pc/docs/Fast_Travel.md, rule R9): which scripts
touch the flags fast travel depends on.

    python3 pc/tools/fast_travel_scan.py [--data data]

Every room script (MAP/MAPnnn.MPD: the MPD header's script section, offset at
0x10, length at 0x14) and event script (EVENT/nnnn.EVT) is walked op by op
from byte 16 to its text (the u16 at +2; BATTLE 4A0A8.c func_800BF5F0) with
the interpreter's own op lengths (BATTLE.PRG D_800E9C30, 256 bytes; 4A0A8.c
advances by D_800E9C30[op]). Flag ops (INITBTL 12AC.c's table, 4A0A8.c):
  0x40 jumpFwdIfFlag   [op, mode | flag >> 6, (flag & 63) << 2, value, jump16]
  0x41 jumpFwdIfFlags  [op, mode | f0 >> 6, (f0 & 63) << 2 | f1 >> 8, f1 & 255, jump16]
  0x46 setFlagImmContext [op, flag16]: the flag into the switch context
  0x50 setFlagImm      [op, mode | flag >> 6, (flag & 63) << 2, value] (mode 0x70: set)
  0x51 setFlag         [op, mode | f0 >> 6, (f0 & 63) << 2 | f1 >> 8, f1 & 255] (f0 from f1;
                       mode 0x90 swaps: f1 written too)
  0x1F func_800B7490   [op, actor16, flag16]: an actor's position into flag .. flag + 2
  0x52 setFlagIfItemInInventory [op, item16, flag16]
  0x56 setFlagIfButton [op, flag16 (& 0x3FF; bit 15: pressed, else held)]
  0xB2 func_800BAC5C   [op, actor16, actor16, divisor, flag16]: distance, angle into flag, flag + 1
  0xB6 func_800BAEA0   [op, actor16, actor16, flag16]: an angle into flag
The flags: unkB5 (0xB5, Teleportation learned / the spirit paling), unkA7
(0xA7, saving refused), savePointStates (0x156 + i, Circle i found / paled).

Actionable (fast travel would have to mirror it: blocks.json): a test or copy of
unkB5 (a script that behaves differently once Teleportation is learned), a write of
unkB5 other than "set 1", a write of a Circle's state other than "set 1".
Reported, not actionable: each Circle recording itself ("records"), unkA7
writes (travel is refused while it is set, rule R3), other tests of the
Circles' states. Prints JSON; writes nothing (the output names files and
offsets only: keep it out of git).

What it cannot see: a one-way stretch before the game's own unlock. Until the
Lich teaches Teleportation the original never had to stop it, so a stretch
the story means to be one-way (a door a script locks behind Ashley, a
scripted fall, a chase) need not touch any flag read here. Travel always
lands on a recorded Circle and is free by default, so it cannot strand
Ashley; what is unchecked is returning into an earlier region's room scripts
in a story state those scripts never saw. Closing that needs a hand review
of the stretches between the first Circle and the Lich, or a tour on a card
from that part of the game."""
import argparse
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
VRAM = 0x80068800       # BATTLE.PRG's load address (config/BATTLE/BATTLE.PRG/splat.yaml)
OP_LENGTHS = 0x800E9C30  # D_800E9C30 (573B8.c, 4A0A8.c)
UNK_B5, UNK_A7, POINTS = 0xB5, 0xA7, 0x156


def flag_name(flag):
    if flag == UNK_B5:
        return "unkB5"
    if flag == UNK_A7:
        return "unkA7"
    if POINTS <= flag < POINTS + 48:
        return f"savePointStates[{flag - POINTS}]"
    return None


def walk(name, code, lengths, findings, desync):
    if len(code) < 16:
        return 0
    end = struct.unpack_from("<H", code, 2)[0]
    if end < 16 or end > len(code):
        end = len(code)
    p = 16
    while p < end:
        op = code[p]
        n = lengths[op]
        if n == 0 or p + n > len(code):
            desync.append(f"{name}@{p:#x} op {op:#x}")
            return 1
        b = code[p:p + n]
        hits = []
        if op in (0x40, 0x50) and n >= 4:
            hits.append(((b[2] >> 2) | ((b[1] & 0xF) << 6), b[1] & 0xF0, b[3], "test" if op == 0x40 else "write"))
        elif op in (0x41, 0x51) and n >= 4:
            f0, f1 = (b[2] >> 2) | ((b[1] & 0xF) << 6), b[3] | ((b[2] & 3) << 8)
            hits.append((f0, b[1] & 0xF0, None, "test" if op == 0x41 else "write"))
            hits.append((f1, None, None, "test" if op == 0x41 else "write" if b[1] & 0xF0 == 0x90 else "read"))
        elif op == 0x46 and n >= 3:
            hits.append((b[1] | b[2] << 8, None, None, "test"))
        elif op == 0x1F and n >= 5:
            f = b[3] | b[4] << 8
            hits += [(f + k, None, None, "write") for k in range(3)]
        elif op == 0x52 and n >= 5:
            hits.append((b[3] | b[4] << 8, None, None, "write"))
        elif op == 0x56 and n >= 3:
            hits.append(((b[1] | b[2] << 8) & 0x3FF, None, None, "write"))
        elif op == 0xB2 and n >= 8:
            f = b[6] | b[7] << 8
            hits += [(f, None, None, "write"), (f + 1, None, None, "write")]
        elif op == 0xB6 and n >= 7:
            hits.append((b[5] | b[6] << 8, None, None, "write"))
        for flag, mode, value, how in hits:
            what = flag_name(flag)
            if not what:
                continue
            point = flag - POINTS if what.startswith("savePointStates") else None
            sets_one = op == 0x50 and mode == 0x70 and value == 1
            if point is not None and sets_one:
                kind = "records"
            else:
                kind = how
            actionable = (flag == UNK_B5 and how in ("test", "read")) or (flag == UNK_B5 and how == "write" and not sets_one) \
                or (point is not None and how == "write" and not sets_one)
            findings.append({"file": name, "offset": p, "op": op, "flag": flag, "mode": mode, "value": value,
                             "point": point, "kind": kind, "actionable": actionable,
                             "what": f"op {op:#x} {how} {what}" + (f" mode {mode:#x} value {value}"
                                                                   if value is not None else "")})
        p += n
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=str(ROOT / "data"))
    args = parser.parse_args()
    data = pathlib.Path(args.data)
    prg = (data / "BATTLE/BATTLE.PRG").read_bytes()
    lengths = prg[OP_LENGTHS - VRAM:OP_LENGTHS - VRAM + 256]
    # the table as 4A0A8.c's handlers read their operands
    if (lengths[0x40], lengths[0x41], lengths[0x42], lengths[0x46], lengths[0x50], lengths[0x51]) != (6, 6, 3, 3, 4, 4):
        sys.exit(f"BATTLE.PRG: no op length table at {OP_LENGTHS:#x}")
    findings, desync, scripts = [], [], 0
    for f in sorted((data / "MAP").glob("MAP*.MPD")):
        b = f.read_bytes()
        if len(b) < 0x18:
            continue
        off, size = struct.unpack_from("<I", b, 0x10)[0], struct.unpack_from("<H", b, 0x14)[0]
        if size and off + size <= len(b):
            scripts += walk(f.name, b[off:off + size], lengths, findings, desync)
    for f in sorted((data / "EVENT").glob("*.EVT")):
        scripts += walk(f.name, f.read_bytes(), lengths, findings, desync)
    json.dump({"scripts": scripts, "desync": desync, "findings": findings}, sys.stdout, indent=1)
    print()


if __name__ == "__main__":
    main()
