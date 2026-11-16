#!/usr/bin/env python3
"""The game's PS1 ties, counted: the numbers may only go down.

    python3 pc/tools/ps1_address_lint.py            fail if a count rose above the baseline
    python3 pc/tools/ps1_address_lint.py --update   lower the baseline to today's counts
    python3 pc/tools/ps1_address_lint.py --raise    accept a rise too (say why in the commit)
    python3 pc/tools/ps1_address_lint.py --where RULE   each hit of a rule

Counts PS1 addresses, unnamed symbols and guest-pointer helpers in code (not
comments) under pc/src and game/, so the move off the PS1 is a number the
tools hold. The idea and the address patterns follow VS-NativePort's
tools/lint/native_boundary.py (origin/imp/01-ma04-gates, 6c8ac0b9, CC0); the
ratchet is ours, since this tree starts with thousands of hits.
Baseline: pc/tools/ps1_address_baseline.json.
"""
import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = ROOT / "pc/tools/ps1_address_baseline.json"
C_SUFFIXES = {".c", ".h", ".inc", ".cpp", ".hpp"}
TREES = [("pc/src", C_SUFFIXES), ("game", C_SUFFIXES | {".zig"})]
SKIP = ("pc/src/third_party/",)
RULES = {
    "ps1_address": re.compile(r"\b0[xX](?:80|[aA]0)[0-9a-fA-F]{6}\b"),
    "scratchpad": re.compile(r"\b0[xX]1[fF]80[0-9a-fA-F]{4}\b|\bgetScratchAddr\b"),
    "unnamed_symbol": re.compile(r"\b(?:D|func)_80[0-9a-fA-F]{6}\b"),
    # memory lane (pc/docs/plans/2026-11-16-memory-lane.md): code that reaches a field by its PS1 byte offset (S1), and
    # references to the 2 MB RAM block, the game heap's address and the page that stands for PS1 address 0 (S9)
    "byte_offset_access": re.compile(r"\bpsx\.(?:fieldAt|byteAt)\(|\bPSX_OFFSET\("),
    "ram_reference": re.compile(r"\bROOD_GUEST_RAM_(?:BASE|SIZE)\b|\bguest_ram_base\b|\bVS_MAIN_HEAP_ADDRESS\b|\brood_null_page\b|\bmap_guest\s*\("),
    # a struct member still called by its offset (pc/tools/import_fields.py gives them names), used as .unkNN or ->unkNN
    "unnamed_field": re.compile(r"(?<![.])(?:\.|->)unk[0-9A-Fa-f]+\b"),
    "guest_pointer_field": re.compile(r"\bpsx_ptr\("),
    "guest_pointer_access": re.compile(r"\bpsx_(?:get|set|set_raw|copy|add|postinc|load|store)\("),
    "address_as_integer": re.compile(r"\bpsx_to_(?:int|ptr)\(|\bpsx\.(?:addr|rawOf)\("),
    "zig_raw_memory": re.compile(r"\bpsx\.(?:byteAt|fieldAt)\(|@ptrFromInt\((?!@returnAddress\(\)\))"),  # (a caller's address, not memory)
    # GPU packets written by hand (PS1_Ties.md T4): the libgpu macros and calls that build a packet or link it into an
    # ordering table, GP0 command words written as hex (the tag in the top byte, after | = ? or :, not read back
    # with == or case), and packets written field by field and linked by hand. DrawSync and the draw environment are not packets.
    "gpu_packet_writes": re.compile(
        r"\b(?:[sS]et(?:Poly|Line|Sprt|Tile|ShadeTex|DrawTPage|DrawMode|DrawArea|DrawOffset|DrawMove|DrawLoad|DrawStp)\w*"
        r"|setSemiTrans|setShadeTex|[aA]ddPrims?|termPrim|catPrim|setaddr|getaddr|DrawOTag|DrawPrim|ClearOTagR?"
        r"|_insertT[pP]age|insertT[pP]age|vs_battle_(?:addTile|insertTpage|addPrim)\w*)\s*\("
        r"|(?:[|?:(]|(?<![=!<>])=)\s*0[xX](?:[2-7][0-9a-fA-F]|[eE][1-6])000000\b"
        # packets built field by field (the EFFECT plugins: prim->poly.tag, prim->drawMode) and linked by hand
        # (the tag's length byte OR'd into a link)
        r"|->(?:poly\.)?tag\s*=(?!=)|\.tag\s*=(?!=)|\bdrawMode\s*=(?!=)|\|\s*0[xX]0[0-9a-fA-F]000000\b"
        # a command or link byte shifted into a packet's top byte as a constant (the Zig printers: (96 << 24), (0xE1 << 24))
        r"|\(\s*(?:0[xX](?:[2-7][0-9a-fA-F]|[eE][1-6])|(?:3[2-9]|[4-9][0-9]|1[01][0-9]|12[0-7]))\s*<<\s*24\s*\)"),
    # The packet-to-item decoder (pc_frame_add_packets, the claims' word commands in hud_bridge.c): it only shrinks, as the
    # printers and the screens that need it move to items.
    "lift_bridge": re.compile(r"\bpc_frame_add_packets\s*\("),
}
# Rules that count only under a path (the others count in every tree).
ONLY_UNDER = {}
# Rules that leave out paths: the SDK's own libgpu and the walker that draws the packets (they are what the game's
# packets are for, T16), and the typed items' lowering to packets (it goes when a renderer takes items directly).
SKIP_FOR = {"gpu_packet_writes": ("pc/src/game/SLUS_010.40/libgpu/", "pc/src/boot_sdk.c", "pc/src/boot_runtime.c",
                                  "pc/src/boot_polygons.inc", "pc/src/boot_lines.inc", "pc/src/gpu_hw.c",
                                  "pc/src/draw_items.c",
                                  # not GPU packets: the SPU's registers and volume words, the movie library's data words,
                                  # and the classic layer's own records (a `.tag` field of its suppression list)
                                  "pc/src/game/SLUS_010.40/libspu/", "pc/src/game/SLUS_010.40/25AC.c",
                                  "pc/src/game/TITLE/TITLE.PRG/libpress/", "pc/src/classic/classic_parity.c")}
COMMENTS = re.compile(r'"(?:\\.|[^"\\\n])*"|/\*.*?\*/|//[^\n]*', re.S)


def strip_comments(text):
    """Comments blanked (their line breaks kept), strings left alone."""
    def blank(match):
        token = match.group(0)
        return token if token.startswith('"') else "\n" * token.count("\n")
    return COMMENTS.sub(blank, text)


def sources():
    for tree, suffixes in TREES:
        for path in sorted((ROOT / tree).rglob("*")):
            name = path.relative_to(ROOT).as_posix()
            if path.suffix in suffixes and path.is_file() and not name.startswith(SKIP):
                yield name, path


def scan():
    """{rule: count}, {rule: [(file, line, text)]}."""
    counts, hits = dict.fromkeys(RULES, 0), {rule: [] for rule in RULES}
    for name, path in sources():
        code = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for number, line in enumerate(code.splitlines(), 1):
            for rule, pattern in RULES.items():
                if not name.startswith(ONLY_UNDER.get(rule, "")) or name.startswith(SKIP_FOR.get(rule, ("\0",))):
                    continue
                found = pattern.findall(line)
                if found:
                    counts[rule] += len(found)
                    hits[rule].append((name, number, line.strip()[:110]))
    return counts, hits


def compare(counts, baseline):
    """The rules that rose, and those that fell, as {rule: (baseline, now)}."""
    rose = {r: (baseline.get(r, 0), n) for r, n in counts.items() if n > baseline.get(r, 0)}
    fell = {r: (baseline.get(r, 0), n) for r, n in counts.items() if n < baseline.get(r, 0)}
    return rose, fell


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--raise", dest="accept_rise", action="store_true")
    parser.add_argument("--where", choices=sorted(RULES))
    args = parser.parse_args()
    counts, hits = scan()
    if args.where:
        for name, number, text in hits[args.where]:
            print(f"{name}:{number}: {text}")
        return 0
    baseline = json.loads(BASELINE.read_text()) if BASELINE.exists() else {}
    rose, fell = compare(counts, baseline)
    if args.accept_rise or (args.update and not rose):
        BASELINE.write_text(json.dumps(counts, indent=1) + "\n")
    for rule, count in counts.items():
        mark = " (rose)" if rule in rose else " (fell)" if rule in fell else ""
        print(f"{rule:22} {count:6}{mark}")
    if rose and not args.accept_rise:
        for rule, (before, now) in rose.items():
            print(f"FAIL: {rule} rose from {before} to {now}; --where {rule} lists them")
        return 1
    if fell and not args.update:
        print("fell: lower the baseline with --update")
    return 0


if __name__ == "__main__":
    sys.exit(main())
