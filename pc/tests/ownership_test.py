#!/usr/bin/env python3
"""pc/tools/ownership.py: the table, its validation, the routed-definition
rename, the declaration-only strip and the generated routing sources."""
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import ownership  # noqa: E402

failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def raises(fn, text):
    try:
        fn()
    except (ValueError, SystemExit) as e:
        return text in str(e)
    return False


TOML = """
[subsystem.alpha]
overlay = "battle"
unit = "src/A.c"
default = "original"
roots = ["g"]
functions = [ { addr = 0x80010000, name = "f_one" }, { addr = 0x80010010, name = "f_two" } ]

[subsystem.beta]
overlay = "battle"
unit = "src/B.c"
default = "owned"
functions = [ { addr = 0x80020000, name = "g_one" } ]
"""

with tempfile.TemporaryDirectory() as d:
    path = pathlib.Path(d) / "ownership.toml"
    path.write_text(TOML)
    table = ownership.load(path)
    check([s.name for s in table.subsystems] == ["alpha", "beta"], "subsystems in file order")
    check([s.index for s in table.subsystems] == [0, 1], "indices are the pc_owner[] slots")
    check(table.routed_names("src/A.c") == {"f_one", "f_two"}, "routed names of a unit")
    check(table.routed_names("src/C.c") == set(), "a unit without routes")
    addresses = {"f_one": 0x80010000, "f_two": 0x80010010, "g_one": 0x80020000}
    check(table.problems(addresses, {"src/A.c", "src/B.c"}) == [], "a consistent table validates")
    renamed = dict(addresses, f_two=None, f_two_renamed=0x80010010)
    del renamed["f_two"]
    problems = table.problems(renamed, {"src/A.c", "src/B.c"})
    check(any("0x80010010" in p and "f_two_renamed" in p for p in problems),
          f"an upstream rename names the new name: {problems}")
    check(any("src/B.c" in p for p in table.problems(addresses, {"src/A.c"})),
          "a unit that is not compiled is reported (routing needs its definitions)")
    path.write_text(TOML.replace('default = "owned"', 'default = "mine"'))
    check(raises(lambda: ownership.load(path), "default"), "an unknown default is refused")
    path.write_text(TOML + '\n[subsystem.gamma]\noverlay="battle"\nunit="src/A.c"\ndefault="original"\n'
                    'functions=[{addr=0x80010000, name="f_one"}]\n')
    check(raises(lambda: ownership.load(path), "f_one"), "a function routed twice is refused")

UNIT = b'''# 1 "unit.c"
typedef struct { int a; } S;
int F(int);
int F2(int x) { return x; }
static int helper(int x) { return F(x) + 1; }
int F(int x)
{
    const char* s = "F(1) { not code }";
    return x * 2;
}
int (*table[])(int) = { F, F2 };
int G(void) { return F(1) + F(2) + helper(3); }
int K(a, b)
    int a;
    int b;
{ return a + b; }
__attribute__((noinline)) int H(void) { return F(0); }
'''

text, counts = ownership.rename_definitions(UNIT, {"F", "K", "H"})
check(counts == {"F": 1, "K": 1, "H": 1}, f"one definition each renamed: {counts}")
check(b"int F__orig(int x)\n{" in text, "the definition of F is renamed")
check(b"int F(int);" in text, "the prototype keeps F")
check(text.count(b"F(1)") == 2 and b"return F(x) + 1" in text, "calls keep F (and the string is untouched)")
check(b"{ F, F2 }" in text, "address-of uses keep F (function pointer tables route too)")
check(b"int F2(int x)" in text, "a name that only starts with F is untouched")
check(b"int K__orig(a, b)" in text, "a K&R definition is renamed")
check(b"int H__orig(void)" in text, "a definition with a leading attribute is renamed")
check(raises(lambda: ownership.rename_definitions(UNIT, {"helper"}), "static"),
      "a static definition cannot be routed")
_, missing = ownership.rename_definitions(UNIT, {"absent"})
check(missing == {"absent": 0}, "an absent definition counts 0 (build_boot stops on it)")

decls = ownership.strip_bodies(UNIT)
check(b"return" not in decls and b"int F(int x);" in decls and b"int G(void);" in decls,
      "strip_bodies keeps every declaration and drops the bodies")
check(b"typedef struct { int a; } S;" in decls and b"{ F, F2 }" in decls,
      "strip_bodies keeps struct bodies and initialisers")
check(b'# 1 "unit.c"' not in decls, "strip_bodies drops linemarkers")

t = ownership.Table([ownership.Subsystem("s", 0, "battle", "u.c", "original", [], [(0, "F")], {}),
                     ownership.Subsystem("t", 1, "battle", "u.c", "owned", [], [(0, "G")], {})])
for abi in ("i386", "sysv64", "win64"):
    src = ownership.routing_source(t, abi)
    check("jmp F__orig" in src and "je F__owned" in src and "je F__shadow" in src, f"{abi}: F's trampoline")
    check(("pc_owner+1(%rip)" in src) == (abi != "i386") and ("pc_owner+1," in src) == (abi == "i386"),
          f"{abi}: G reads its own slot")
    check((".type F, @function" in src) == (abi != "win64"), f"{abi}: ELF symbol types only on ELF")
    check("%al" not in src and ("%r11d" in src) == (abi != "i386"), f"{abi}: no argument register clobbered")
    check('"s", "t"' in src and "pc_subsystem_defaults[] = { 0, 1 }" in src, f"{abi}: subsystem tables")
zig = ownership.ownership_zig(t)
check('.{ .name = "F", .subsystem = 0 }' in zig and "pub const battle_action" not in zig, "the comptime table")

print("FAILED" if failures else "all ownership checks passed")
sys.exit(1 if failures else 0)
