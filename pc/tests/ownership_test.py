#!/usr/bin/env python3
"""pc/tools/ownership.py: the table, its validation, the routed-definition
rename, the declaration-only strip and the generated routing sources."""
import pathlib
import subprocess
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
for abi in ("i386", "win64"):
    check(raises(lambda: ownership.routing_source(t, abi, reloadable=True), "sysv64"),
          f"{abi}: reloadable routing is refused")
check("pc_owned_slots" not in ownership.routing_source(t, "sysv64"),
      "static routing does not gain reload slots")

# Execute the generated host trampolines, including a shadow wrapper's
# direct call to __owned and a previously saved function pointer.
with tempfile.TemporaryDirectory() as d:
    path = pathlib.Path(d)
    source = path / "routes.c"
    source.write_text(ownership.routing_source(t, "sysv64", reloadable=True) + r'''
#include <assert.h>
unsigned char pc_owner[2];
const char F__owned_abi[] = "test-F", G__owned_abi[] = "test-G";
int F(int, int, int, int, int, int, int);
int F__owned(int, int, int, int, int, int, int);
int F__orig(int a, int b, int c, int d, int e, int f, int g) {
    return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g;
}
int first(int a, int b, int c, int d, int e, int f, int g) {
    return F__orig(a,b,c,d,e,f,g) + 100;
}
int second(int a, int b, int c, int d, int e, int f, int g) {
    return F__orig(a,b,c,d,e,f,g) + 200;
}
int F__shadow(int a, int b, int c, int d, int e, int f, int g) {
    return F__owned(a,b,c,d,e,f,g) + 1000;
}
double G(double);
double G__orig(double a) { return a + 1; }
double g_owned(double a) { return a + 2; }
double G__shadow(double a) { return a + 3; }
int main(void) {
    int (*saved)(int,int,int,int,int,int,int) = F__owned;
    pc_owned_slots[0] = first;
    pc_owned_slots[1] = g_owned;
    assert(F(1,2,3,4,5,6,7) == 140);
    pc_owner[0] = 1;
    assert(F(1,2,3,4,5,6,7) == 240);
    assert(saved(1,2,3,4,5,6,7) == 240);
    pc_owned_slots[0] = second;
    assert(F(1,2,3,4,5,6,7) == 340);
    assert(saved(1,2,3,4,5,6,7) == 340);
    pc_owner[0] = 2;
    assert(F(1,2,3,4,5,6,7) == 1340);
    pc_owner[1] = 1;
    assert(G(0.5) == 2.5);
    void* next[] = { first, g_owned };
    assert(__atomic_exchange_n(&pc_owned_table, next, __ATOMIC_ACQ_REL) == pc_owned_slots);
    assert(F(1,2,3,4,5,6,7) == 1240); /* shadow also sees the complete new table */
    assert(saved(1,2,3,4,5,6,7) == 240);
    assert(G(0.5) == 2.5);
    pc_owner[0] = 0;
    assert(F(1,2,3,4,5,6,7) == 140);
    return 0;
}
''')
    compiled = subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-fno-pie", "-no-pie",
                               str(source), "-o", str(path / "routes")], capture_output=True, text=True, timeout=60)
    check(compiled.returncode == 0, "reloadable routing harness compiles: " + compiled.stderr.strip())
    if compiled.returncode == 0:
        result = subprocess.run([str(path / "routes")], timeout=10)
        check(result.returncode == 0,
              "repointing preserves owner/shadow routing, saved addresses and integer/FP/stack arguments")

zig = ownership.ownership_zig(t)
check('.{ .name = "F", .subsystem = 0 }' in zig and "pub const battle_action" not in zig, "the comptime table")

print("FAILED" if failures else "all ownership checks passed")
sys.exit(1 if failures else 0)
