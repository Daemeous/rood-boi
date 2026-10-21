#!/usr/bin/env python3
"""The coverage lane of pc/tools/zig_diff.py (--coverage) finds what a weak
spec misses. Each case weakens one checked-in spec the way a real miss
looked (the spec bake-off's two Haiku misses among them) and requires the
gate to fail naming the right id:

  - an entry test whose failing value is never generated: an uncovered branch;
  - func_800AD494 without the exact count (idx == unk5B6 in the 0xFE branch):
    boundary equality rare or never (`+32 eq`), Haiku's U1 miss;
  - func_80078828 with a cost stub whose top byte is all clear or all set:
    the nonzero test only ever saw the sign bit set (`+76 sign`), Haiku's A3;
  - _updateEnemyKills never exactly at the clamp: `+25 eq`;
and the checked-in specs pass it. CPU only; needs a 64-bit build (exit 77)."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "pc/build/boot64"
SPECS = ROOT / "pc/tests/zig_diff"

CASES = [
    ("func_80078828", "canUseBattleAbility.zig",
     "current.unk4A = if (ctx.chance(8)) 0 else ctx.pick(u16, &.{ 1, 0xFFFF });",
     "if (!ctx.chance(8)) current.unk4A = ctx.pick(u16, &.{ 1, 0xFFFF });", "UNCOVERED +10 b3"),
    ("func_800AD494", "findAnimation.zig",
     "actor.unk5B6 = ctx.pick(u8, &.{ 0, 1, 32, 34, 255 });",
     "actor.unk5B6 = ctx.pick(u8, &.{ 0, 1, 2, 255 });", "FLAG +32 eq"),
    ("func_80078828", "canUseBattleAbility.zig",
     "return @bitCast(if (value & 1 != 0) value | 0x0100_0000 else value & 0x00FF_FFFF);",
     "return @bitCast(if (value & 1 != 0) value | 0xFF00_0000 else value & 0x00FF_FFFF);", "FLAG +76 sign"),
    ("_updateEnemyKills", "updateEnemyKills.zig",
     "if (ctx.chance(4)) 999999999 else 999999999 - ctx.range(u32, 0, 70000)",
     "999999999 - ctx.range(u32, 1, 70000)", "FLAG +25 eq"),
]


def zig_diff(args, work):
    r = subprocess.run([sys.executable, ROOT / "pc/tools/zig_diff.py", *args, "--coverage", "--build", BUILD,
                        "--work", work], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    if not (BUILD / "imports.ld").exists():
        print(f"zig_diff_coverage_test: no 64-bit build in {BUILD}: skipped")
        return 77
    failures = []
    with tempfile.TemporaryDirectory(prefix="zig-diff-coverage-") as tmp:
        tmp = pathlib.Path(tmp)
        for i, (function, spec, old, new, expect) in enumerate(CASES):
            text = (SPECS / spec).read_text()
            if text.count(old) != 1:
                failures.append(f"{spec}: the text to weaken is gone (update this test): {old}")
                continue
            weak = tmp / f"weak{i}_{spec}"
            weak.write_text(text.replace(old, new))
            # The stub change of the third case also needs the edge returns gone.
            if "0xFF00_0000" in new:
                weak.write_text(weak.read_text().replace("if (value % 16 == 2) return 0x00FF_FFFF;", "")
                                .replace("if (value % 16 == 4) return 0x0100_0000;", ""))
            code, out = zig_diff([function, "--spec", weak], tmp / "work")
            if code == 0 or expect not in out:
                failures.append(f"{function} with {new!r}: expected the gate to fail with {expect!r} "
                                f"(exit {code}): {out[-1500:]}")
        code, out = zig_diff(["func_80078828", "func_800AD494", "_updateEnemyKills", "vs_battle_displaySceneMessage"],
                             tmp / "work")
        if code != 0:
            failures.append(f"the checked-in specs fail the coverage gate: {out[-2000:]}")
        elif "waived +38 b1" not in out:
            failures.append("vs_battle_displaySceneMessage: its waiver is not reported")
    for f in failures:
        print("FAIL:", f)
    print("zig_diff_coverage_test: " + ("FAILED" if failures else "passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
