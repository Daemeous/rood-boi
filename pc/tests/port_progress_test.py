#!/usr/bin/env python3
"""pc/tools/port_progress.py: a D2 function is counted under the unit its
oracle names (pc/src/battle_helpers.c), the unit ownership.toml and the
`/// orig:` line spell, not under the PS1 assembly file of FUNCTIONS.tsv."""
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import port_progress  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


D2 = "D2 (pc/src asm port): asm on the PS1; PC oracle is pc/src/battle_helpers.c:473"
check(port_progress.unit_file("src/BATTLE/BATTLE.PRG/6FA6C.s", D2) == "pc/src/battle_helpers.c",
      "a D2 row's unit is its oracle")
check(port_progress.unit_file("src/BATTLE/BATTLE.PRG/146C.c", "c") == "src/BATTLE/BATTLE.PRG/146C.c",
      "a first-pass row keeps its file")
check(port_progress.unit_file("src/BATTLE/BATTLE.PRG/7B5DC.s", "D2: asm on the PS1; PC runs none: calls trap (pc_unimplemented)")
      == "src/BATTLE/BATTLE.PRG/7B5DC.s", "a D2 row without an oracle keeps its file")

with tempfile.TemporaryDirectory() as d:
    root = pathlib.Path(d)
    (root / "pc/docs/porting/battle").mkdir(parents=True)
    (root / "game/battle/pc").mkdir(parents=True)
    (root / "game").joinpath("ownership.toml").write_text("")
    (root / port_progress.TABLE).write_text(
        "function\taddress\tfile\tlines\tkind\tdifficulty\n"
        f"func_800D826C\t0x800D826C\tsrc/BATTLE/BATTLE.PRG/6FA6C.s\t1\t{D2}\tS\n"
        f"func_800D8280\t0x800D8280\tsrc/BATTLE/BATTLE.PRG/6FA6C.s\t10\t{D2}\tS\n"
        "func_80070CAC\t0x80070CAC\tsrc/BATTLE/BATTLE.PRG/146C.c\t40\tc\tS\n")
    (root / "game/battle/pc/x.zig").write_text(
        "/// orig: func_800D826C (pc/src/battle_helpers.c)\npub fn f() void {}\n"
        "/// orig: func_80070CAC (src/BATTLE/BATTLE.PRG/146C.c)\npub fn g() void {}\n")
    kinds = port_progress.summary(root)["kinds"]
    check(kinds["D2"] == {"ported": 1, "left": 1}, f"one of two D2 functions counted: {kinds['D2']}")
    check(kinds["c"] == {"ported": 1, "left": 0}, f"first-pass counting unchanged: {kinds['c']}")

print("FAILED" if failures else "all port_progress checks passed")
sys.exit(1 if failures else 0)
