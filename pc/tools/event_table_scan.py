#!/usr/bin/env python3
"""Scans the player's own disc data for the index ranges of the tables the event scripts and the room objects index (memory lane S7):
which values the scripts and objects actually use, so a table can be declared with the extent the data needs. The output is a report to
read (redirect it where you like); nothing here is committed data. Every access to those tables is index-checked at run time (a value
outside the declared extent is logged once and refused), so a modded or unusual script cannot read past a table.

    python3 pc/tools/event_table_scan.py [--data data] > report.txt

Reads data/BATTLE/BATTLE.PRG (the opcode size table), data/EVENT/*.EVT, data/MAP/*.MPD and data/OBJ/*.SHP.
"""
import argparse, collections, glob, pathlib, struct, sys

OPCODE_SIZES = 0x800E9C30  # vs_event_opcodeSizes (256 bytes), in BATTLE.PRG at 0x80068800
BATTLE_BASE = 0x80068800
OP_TIMER = 0x5A            # vs_event_scriptConfigureTimer: byte 1 nonzero starts a timer (byte 2 scope, byte 3 mode), else resets it
EVENT_CODE_START = 16      # the main thread at data + 16, up to the first block the header points at


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default="data")
    args = parser.parse_args()
    data = pathlib.Path(args.data)
    battle = (data / "BATTLE/BATTLE.PRG").read_bytes()
    sizes = battle[OPCODE_SIZES - BATTLE_BASE:OPCODE_SIZES - BATTLE_BASE + 256]

    files = sorted(glob.glob(str(data / "EVENT/*.EVT")))
    exact = 0
    inexact = []
    ops = collections.Counter()
    timer_start, timer_reset = [], 0
    for path in files:
        blob = open(path, "rb").read()
        header = struct.unpack_from("<4H", blob, 0)
        offsets = [h for h in header[1:] if h > EVENT_CODE_START]
        end = min(offsets) if offsets else len(blob)
        pc = EVENT_CODE_START
        while pc < end:
            size = sizes[blob[pc]]
            if size == 0:
                break
            ops[blob[pc]] += 1
            if blob[pc] == OP_TIMER and pc + 4 <= len(blob):
                if blob[pc + 1]:
                    timer_start.append((pathlib.Path(path).stem, pc, blob[pc + 2], blob[pc + 3]))
                else:
                    timer_reset += 1
            pc += size
        if pc == end:
            exact += 1
        else:
            inexact.append((pathlib.Path(path).stem, pc, end))
    print(f"event scripts: {len(files)} files; the main thread's opcodes walk exactly to the first data block in {exact}; not in {len(inexact)} {inexact[:5]}")
    print(f"timer opcode (0x5A): {timer_reset} resets (no table index), {len(timer_start)} starts (scope byte, mode byte): {timer_start[:10]}")
    if timer_start:
        print(f"  vs_event_sceneTimerResetScopes indexes: {sorted({t[2] for t in timer_start})}, vs_event_sceneTimerModes indexes: {sorted({t[3] for t in timer_start})}")

    actors = collections.Counter()
    objects = 0
    for path in sorted(glob.glob(str(data / "MAP/*.MPD"))):
        blob = open(path, "rb").read()
        if len(blob) < 0x28:
            continue
        offset, length = struct.unpack_from("<ii", blob, 0x20)
        if length <= 0 or offset <= 0 or offset + length > len(blob):
            continue
        for i in range(length // 0x28):
            entry = blob[offset + i * 0x28: offset + (i + 1) * 0x28]
            if entry[0] != 0:  # an object, not an enemy
                model = struct.unpack_from("<H", entry, 14)[0]
                actors[((model >> 12) | (model & 0xF00)) & 0xFF] += 1  # vs_field_spawnRoomActors' actor id, as a byte (vs_battle_objectData.actorId)
                objects += 1
    print(f"room objects (MPD, type != 0): {objects}; actor ids (the low byte createAuxiliaryActor stores): {sorted(actors.items())}")
    print(f"  vs_field_gimmickTemplateIndices: indexed by an id up to {max(actors)}; vs_field_gimmickPaletteColors: by id * 32; D_800E8FA8: by min(id, 5) * 2")
    indices = battle[0x800E919C - BATTLE_BASE:0x800E919C - BATTLE_BASE + 8]
    print(f"  template indices for ids 0..7: {list(indices)} -> vs_field_gimmickModelTemplates entries used: {sorted({indices[a] for a in actors if a < 8})}")
    scan_shp(data)


def scan_shp(data):
    """The actor shape files (data/OBJ/*.SHP, the layout in game/battle/shp_file.zig): the texture block's width and height give how many texture slots
    (of vs_render_legacyActorTextureSlots, 24 of them) a model takes: n = height / 64, or 8 for a 256-wide texture (parse_shp.zig)."""
    files = sorted(glob.glob(str(data / "OBJ/*.SHP")))
    shapes = collections.Counter()
    shared = 0
    for path in files:
        blob = open(path, "rb").read()
        model = struct.unpack_from("<i", blob, 0xF4)[0]
        seq = 0xF8 + model
        seq_length = struct.unpack_from("<i", blob, seq + 4)[0]
        block = seq + 8 + seq_length
        size = struct.unpack_from("<i", blob, block)[0]
        if size == 4:
            shared += 1
            continue
        flags, half_width, half_height, cluts = blob[block + 4:block + 8]
        width, height = half_width * 2, half_height * 2
        n = 8 if width == 0x100 else height // 64
        shapes[(width, height, n)] += 1
    print(f"actor shapes (OBJ/*.SHP): {len(files)} files, {shared} share another's texture; (width, height, slots n) -> files: {sorted(shapes.items())}")
    biggest = max((k[2] for k in shapes), default=0)
    print(f"  a model takes at most {biggest} slots; the search ranges are 12..20 (objects 1 and 16) and (n >= 3) * 4 .. 12, the fallback starts at slot 20: slots 20 + n - 1 = {20 + biggest - 1} at most, the tables hold 24")


if __name__ == "__main__":
    sys.exit(main())
