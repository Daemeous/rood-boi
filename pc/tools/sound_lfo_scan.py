#!/usr/bin/env python3
"""The LFO wave types the player's own disc asks of the sound driver (S9, g_Sound_LfoTable's extent).

The driver's opcodes B4 (vibrato), B8 (tremolo) and BC (auto pan) carry a wave type byte, and the driver indexes g_Sound_LfoTable by it
(25AC.c SoundVM_B4/B8/BC). The table holds 16 wave pointers; what follows is the PS1 code's jump tables, so a type of 16 or more would read
a code address as a wave. This tool walks every sound sequence on the disc and reports the types it finds.

    python3 pc/tools/sound_lfo_scan.py [--root DIR]      the report goes to stdout (it stays in wip, never committed)

What it reads: the opcode length tables from the executable (vs_audio_seqOpcodeLengths and vs_audio_seqExtendedOpcodeLengths, found through
config/SLUS_010.40/symbol_addrs.txt), then
  - MUSIC/MUSIC*.DAT and every AKAO sequence in SE/SEP*.DAT: a 0x40-byte header (ChannelEnableMask at 0x20), then one u16 per enabled channel
    (at 0x40, relative to its own address) giving that channel's stream;
  - SE/EFFECT*.DAT, the sound effect banks: a 0x10-byte header, 0x600 bytes of u16 program offsets, 0x300 bytes of metadata and the program
    data the offsets point into.
A stream is walked linearly from its start to the next stream's start (so subroutines the jumps reach are covered), with the driver's own
lengths: below 0xA0 one byte, F0..FD two, FE an extended opcode (table lengths; the jumps and loop passes 3, 4, 5), C9 two, other table zeros one.
A parse that leaves the file or meets an unknown opcode is reported by file, not guessed at.
"""
import argparse
import collections
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXTENDED_SPECIAL = {0x06: 3, 0x07: 4, 0x08: 5, 0x09: 5, 0x0E: 3, 0x0F: 1}  # (jump, jump on threshold, nth loop pass, pop, call, return), FE excluded
TYPE_OPERAND = {0xB4: 3, 0xB8: 3, 0xBC: 2}  # the type byte's offset from the opcode
NAMES = {0xB4: "vibrato", 0xB8: "tremolo", 0xBC: "auto pan"}


def exe_tables(root):
    blob = (root / "data/SLUS_010.40").read_bytes()
    symbols = {}
    for line in (root / "pc/names/symbols.tsv").read_text().splitlines():  # data SLUS_010.40 D_8002F5A0 readable-name ...
        f = line.split("\t")
        m = re.fullmatch(r"D_([0-9A-Fa-f]{8})", f[2]) if len(f) > 3 and f[1] == "SLUS_010.40" else None
        if m:
            symbols[f[3]] = int(m.group(1), 16)
    def at(name, count):
        offset = symbols[name] - 0x80010000 + 0x800
        return list(blob[offset:offset + count])
    return at("vs_audio_seqOpcodeLengths", 96), at("vs_audio_seqExtendedOpcodeLengths", 32)


class Walk:
    def __init__(self, lengths, extended):
        self.lengths, self.extended = lengths, extended
        self.found = collections.Counter()  # (opcode, type) -> count
        self.where = collections.defaultdict(list)
        self.problems = []

    def stream(self, data, start, end, label, until_end=False):
        if not 0 <= start < len(data):
            self.problems.append(f"{label}: a stream starts at {start:#x}, outside the file")
            return
        end = min(end, len(data))
        p = start
        while p < end:
            op = data[p]
            if op < 0xA0:
                p += 1
                continue
            if until_end and op in (0xA0, 0xFF):
                return  # a program's own end (the data after it is not sound)
            if op in TYPE_OPERAND:
                if p + TYPE_OPERAND[op] >= len(data):
                    self.problems.append(f"{label}: opcode {op:#x} at {p:#x} runs off the file")
                    return
                t = data[p + TYPE_OPERAND[op]]
                self.found[(op, t)] += 1
                if len(self.where[(op, t)]) < 6:
                    self.where[(op, t)].append(f"{label}+{p:#x}")
            if op == 0xFE:
                if p + 1 >= len(data):
                    self.problems.append(f"{label}: extended opcode at {p:#x} runs off the file")
                    return
                ext = data[p + 1]
                if ext >= len(self.extended):
                    self.problems.append(f"{label}: extended opcode {ext:#x} at {p:#x} is past the table")
                    return
                length = self.extended[ext] or EXTENDED_SPECIAL.get(ext, 0)
                p += 1 + (length or 1)
            elif 0xF0 <= op <= 0xFD:
                p += 2
            elif op == 0xC9:
                p += 2
            else:
                p += self.lengths[op - 0xA0] or 1


def sequence(walk, data, base, label, strict=False):
    """One AKAO sequence at base: its channel streams, each to the next one's start (or the file's end)."""
    if data[base:base + 4] != b"AKAO" or base + 0x40 > len(data):
        return False
    mask = struct.unpack_from("<I", data, base + 0x20)[0]
    if strict and (struct.unpack_from("<I", data, base + 4)[0] == 0 or mask == 0 or bin(mask).count("1") > 25 or base + 0x40 + 2 * bin(mask).count("1") > len(data)):
        return False  # four bytes that spell AKAO inside other data
    starts = []
    entry = base + 0x40
    for channel in range(32):
        if mask >> channel & 1:
            if entry + 2 > len(data):
                walk.problems.append(f"{label}: channel table runs off the file")
                return True
            start = entry + struct.unpack_from("<H", data, entry)[0]
            if strict and not 0 <= start < len(data):
                return False
            starts.append(start)
            entry += 2
    ordered = sorted(set(starts))
    for s in ordered:
        later = [x for x in ordered if x > s]
        walk.stream(data, s, later[0] if later else len(data), label)
    return True


def blob(walk, data, base, label, limit):
    """A sound-effect program blob (vs_audio_soundEffectsPlayBlobPrograms: the units' and effects' own sounds): AKAO, a program count at +4, the
    bank slot at +8, u32 offsets at +0x10 into the data after the 0x20-byte header; each offset names a pair of u16 (one per channel,
    0xFFFF none) and the streams start at the pair's address + 4 + the u16. Returns the number of programs, 0 if it is not one."""
    if base + 0x20 > len(data):
        return 0
    count = struct.unpack_from("<I", data, base + 4)[0]
    if not 1 <= count <= 0x100 or base + 0x10 + 4 * count > len(data):
        return 0
    starts = []
    for i in range(count):
        off = struct.unpack_from("<I", data, base + 0x10 + 4 * i)[0]
        entry = base + 0x20 + off
        if off > len(data) or entry + 4 > len(data):
            return 0
        for k in (0, 1):
            v = struct.unpack_from("<H", data, entry + 2 * k)[0]
            if v != 0xFFFF:
                starts.append(entry + 4 + v)
    if not starts or any(not base <= x < len(data) for x in starts):
        return 0
    ordered = sorted(set(starts))
    for x in ordered:
        later = [y for y in ordered if y > x]
        walk.stream(data, x, min(later[0] if later else limit, limit), label, until_end=True)
    return count


def bank(walk, data, label):
    """A sound effect bank: u16 program offsets (0xFFFF none) into the program data. EFFECT00.DAT (the bound bank, Sound_BindAkaoSfxBlob):
    0x600 bytes of offsets and 0x300 of metadata, the data at 0xC10. EFFECT01.DAT: 0x200 bytes of offsets, the data at 0x210."""
    base, entries = (0x10 + 0x600 + 0x300, 0x300) if len(data) > 0xC10 else (0x10 + 0x200, 0x100)
    table = struct.unpack_from("<%dH" % entries, data, 0x10)
    offsets = sorted({x for x in table if x != 0xFFFF})
    for off in offsets:
        later = [x for x in offsets if x > off]
        walk.stream(data, base + off, base + later[0] if later else len(data), f"{label}[{off:#x}]")
    return len(offsets)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(ROOT))
    root = pathlib.Path(parser.parse_args().root)
    walk = Walk(*exe_tables(root))
    files = sequences = programs = 0
    for path in sorted((root / "data/MUSIC").glob("*.DAT")):
        data = path.read_bytes()
        files += 1
        sequences += sequence(walk, data, 0, path.name)
    for path in sorted((root / "data/SE").glob("*.DAT")):
        data = path.read_bytes()
        files += 1
        if path.name.startswith("EFFECT"):
            programs += bank(walk, data, path.name)
            continue
        magics = [m.start() for m in re.finditer(b"AKAO", data)]
        for i, at in enumerate(magics):
            limit = magics[i + 1] if i + 1 < len(magics) else len(data)
            if blob(walk, data, at, f"{path.name}@{at:#x}", limit):
                programs += 1
            else:
                sequences += sequence(walk, data, at, f"{path.name}@{at:#x}")
    # Everything else on the disc that holds an AKAO header (room sound data in the maps, the objects' sound, the effect packages): a header is
    # accepted when its channel table lies inside the file, its mask is plausible and its word at +4 is not zero (the sample banks that carry the AKAO
    # tag, such as the effect packages' and SOUND/WAVE*.DAT, have zeros there; the other four-byte matches are not sequences either).
    accepted = collections.Counter()
    rejected = collections.Counter()
    for path in sorted((root / "data").rglob("*")):
        if not path.is_file() or path.parent.name in ("MUSIC", "SE"):
            continue
        data = path.read_bytes()
        if b"AKAO" not in data:
            continue
        files += 1
        group = path.parent.name
        magics = [m.start() for m in re.finditer(b"AKAO", data)]
        for i, at in enumerate(magics):
            label = f"{group}/{path.name}@{at:#x}"
            limit = magics[i + 1] if i + 1 < len(magics) else len(data)
            if blob(walk, data, at, label, limit):
                accepted[group + " blobs"] += 1
            elif sequence(walk, data, at, label, strict=True):
                accepted[group + " sequences"] += 1
            else:
                rejected[group] += 1
    print(f"{files} files, {sequences} music and sound-effect sequences, {programs} bank programs walked")
    print("elsewhere on the disc, AKAO headers accepted as sequences: " + (", ".join(f"{g} {n}" for g, n in sorted(accepted.items())) or "none")
          + "; rejected as other data: " + (", ".join(f"{g} {n}" for g, n in sorted(rejected.items())) or "none"))
    for op in sorted(TYPE_OPERAND):
        types = sorted(t for (o, t) in walk.found if o == op)
        print(f"{NAMES[op]} (opcode {op:#x}): types {types if types else 'none'}; uses {sum(c for (o, _), c in walk.found.items() if o == op)}")
    high = sorted((o, t) for (o, t) in walk.found if t >= 16)
    print(f"types of 16 or more: {len(high)}")
    for o, t in high:
        print(f"  {NAMES[o]} type {t}: {walk.found[(o, t)]} uses, e.g. {', '.join(walk.where[(o, t)])}")
    print(f"unparsed: {len(walk.problems)}")
    for line in walk.problems[:20]:
        print("  " + line)
    return 1 if high else 0


if __name__ == "__main__":
    sys.exit(main())
