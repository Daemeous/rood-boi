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
    shared = dict(addresses, overlay_twin=0x80010010)  # a second overlay's name at the same address
    check(table.problems(shared, {"src/A.c", "src/B.c"}) == [],
          "two names at one address (ENDING over BATTLE) still validate the table's name")
    statics = [*addresses.items(), ("f_one", 0x80030000)]  # another unit's static f_one
    check(table.problems(statics, {"src/A.c", "src/B.c"}) == [],
          "a static name another unit also defines validates at its own address (pairs)")
    check(table.problems(dict(statics), {"src/A.c", "src/B.c"}) != [],
          "a dict keeps one address per name (why the build passes pairs)")
    check(any("src/B.c" in p for p in table.problems(addresses, {"src/A.c"})),
          "a unit that is not compiled is reported (routing needs its definitions)")
    host = TOML.replace('{ addr = 0x80020000, name = "g_one" }', '{ name = "pc_cb_1", host = true }')
    path.write_text(host)
    ht = ownership.load(path)
    check(ht.problems(addresses, {"src/A.c", "src/B.c"}) == [] and ht.routes()[-1] == ("pc_cb_1", 1),
          "a host function (host = true, no addr) is routed and not checked against the ELFs")
    path.write_text(TOML.replace('{ addr = 0x80020000, name = "g_one" }', '{ name = "g_one" }'))
    check(raises(lambda: ownership.load(path), "host"), "a function without addr must say host = true")
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
void Hook(int a)
{
    extern void pc_hook_start(int now);
    pc_hook_start(1);
    {
        extern void pc_hook_end(int message, int action) __attribute__((weak));
        if (pc_hook_end) pc_hook_end(a, 2);
    }
}
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
check(b"int F(int x);\nint F__orig(int x)" in text, "a routed definition keeps a prototype of its own name before it")
check(b"int K();\nint K__orig(a, b)" in text, "a K&R definition gets an unprototyped declaration")
check(b"__attribute__((noinline)) int H(void);\n__attribute__((noinline)) int H__orig(void)" in text,
      "the declaration copies the definition's attributes")
# The unit's only declaration was the definition (2842C's func_8009195C, called by func_800919D8
# after it): the caller must still see `void* P(int)`, not an implicit int (a truncated pointer on LP64).
lone = b"void* P(int i) { return (void*)0; }\nint Q(int i) { return P(i) != 0; }\n"
renamed, _ = ownership.rename_definitions(lone, {"P"})
check(renamed.startswith(b"void* P(int i);\nvoid* P__orig(int i)") and b"return P(i)" in renamed,
      f"a lone definition keeps its prototype for later callers: {renamed!r}")
check(raises(lambda: ownership.rename_definitions(UNIT, {"helper"}), "static"),
      "a static definition cannot be routed")
_, missing = ownership.rename_definitions(UNIT, {"absent"})
check(missing == {"absent": 0}, "an absent definition counts 0 (build_boot stops on it)")

# Static host code in an .inc the unit includes (room_callbacks.inc, charge_skip.inc, the generated
# effect_callbacks.inc / event_callbacks.inc wrappers): routable, found by the linemarkers.
INC_UNIT = b'''# 1 "unit.c"
int target(int);
static int wrap(int);
static int helper(int x) { return x; }
# 1 "gen/wrappers.inc" 1
static int wrap(int x) { return target(x) + 1; }
static inline int quick(int x) { return x; }
# 4 "unit.c" 2
int (*table[])(int) = { wrap, helper };
int use(int v) { return wrap(v) + helper(v); }
'''
renamed, counts = ownership.rename_definitions(INC_UNIT, {"wrap"})
check(counts == {"wrap": 1}, "a static definition in an .inc is routable")
check(b"int wrap__orig(int x) { return target(x) + 1; }" in renamed and b"static int wrap" not in renamed,
      f"its definition, prelude and earlier prototype lose static: {renamed!r}")
check(b"int wrap(int x);\nint wrap__orig" in renamed, "a prelude declaration of the global name precedes it")
check(b"{ wrap, helper }" in renamed and b"return wrap(v) + helper(v)" in renamed
      and b"static int helper(int x)" in renamed, "calls and address-of uses keep the name; other statics stay")
check(raises(lambda: ownership.rename_definitions(INC_UNIT, {"helper"}), "static"),
      "a static outside the .inc stays unroutable")
check(raises(lambda: ownership.rename_definitions(INC_UNIT, {"quick"}), "inline"),
      "a static inline in an .inc is refused")
# Same result when the markers come from `#line`, and idempotent names for the same input.
check(ownership.rename_definitions(INC_UNIT, {"wrap"})[0] == renamed, "the rename is deterministic")
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "wrappers.inc").write_text("static int wrap(int x) { return target(x) + 1; }\n"
                                    "static int plain(int x) { return x * 3; }\n")
    (d / "unit.c").write_text('int target(int);\nstatic int wrap(int);\n#include "wrappers.inc"\n'
                              'int (*table)(int) = wrap;\nint use(int v) { return wrap(v) + plain(v); }\n')
    pre = subprocess.run(["gcc", "-E", "-I", str(d), str(d / "unit.c")], capture_output=True, check=True).stdout
    routed, counts = ownership.rename_definitions(pre, {"wrap"})
    check(counts == {"wrap": 1}, "a real gcc -E of a unit with an .inc: the .inc definition is found")
    (d / "r.i").write_bytes(routed)
    subprocess.run(["gcc", "-O2", "-c", str(d / "r.i"), "-o", str(d / "r.o")], check=True)
    syms = subprocess.run(["nm", str(d / "r.o")], capture_output=True, text=True, check=True).stdout
    check(" T wrap__orig" in syms and " U wrap" in syms and " U target" in syms and " T use" in syms,
          f"object: wrap__orig defined, wrap external (the trampoline's), unit calls it: {syms!r}")
    subprocess.run(["gcc", "-O0", "-c", "-x", "c", "-", "-o", str(d / "b.o")], input=pre, check=True)
    base = subprocess.run(["nm", str(d / "b.o")], capture_output=True, text=True, check=True).stdout
    check(" t wrap" in base and "__orig" not in base, f"unrouted: the static stays local: {base!r}")

# The generated wrapper shape (event_callbacks.py): routable as it is, one-line definition.
GEN = (b'# 1 "5BF94.c"\nint func_800D4720(void* n);\n# 1 "pc/build/boot/effect_callbacks.inc" 1\n'
       b'static int pc_effect_func_800D4720(void* node) { return func_800D4720(node); }\n'
       b'static int (*const pc_effect_table[])(void*) = { pc_effect_func_800D4720 };\n# 9 "5BF94.c" 2\n')
gen, counts = ownership.rename_definitions(GEN, {"pc_effect_func_800D4720"})
check(counts == {"pc_effect_func_800D4720": 1} and b"int pc_effect_func_800D4720__orig(void* node)" in gen
      and b"{ pc_effect_func_800D4720 }" in gen and b"static int pc_effect_func_800D4720" not in gen,
      f"a generated static effect wrapper routes; its table entry keeps the name: {gen!r}")

decls = ownership.strip_bodies(UNIT)
check(b"return" not in decls and b"int F(int x);" in decls and b"int G(void);" in decls,
      "strip_bodies keeps every declaration and drops the bodies")
check(b"typedef struct { int a; } S;" in decls and b"{ F, F2 }" in decls,
      "strip_bodies keeps struct bodies and initialisers")
check(b'# 1 "unit.c"' not in decls, "strip_bodies drops linemarkers")
check(b"extern void pc_hook_start(int now);\n" in decls
      and b"extern void pc_hook_end(int message, int action) __attribute__((weak));\n" in decls
      and b"pc_hook_start(1)" not in decls, "strip_bodies keeps block-scope extern declarations at file scope")

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
check('.{ .name = "F", .zig = "F", .subsystem = 0 }' in zig and "pub const battle_action" not in zig,
      "the comptime table (no zig key: the Zig function has the original name)")

# Readable Zig names (PORTING.md §16): the table maps the original symbol to
# the Zig function; routing keeps the original name, the source says `orig:`.
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    path = d / "ownership.toml"
    path.write_text(TOML.replace('{ addr = 0x80010010, name = "f_two" }',
                                 '{ addr = 0x80010010, name = "f_two", zig = "score.countKill" }'))
    named = ownership.load(path)
    check(named.routes() == table.routes(), "routes (and the reload ABI key) stay keyed by the original names")
    check(named.zig_routes()[1] == ("f_two", "score.countKill", 0) and named.zig_routes()[0][1] == "f_one",
          "zig_routes maps original -> Zig function")
    check('.{ .name = "f_two", .zig = "score.countKill", .subsystem = 0 }' in ownership.ownership_zig(named)
          and "pub fn routesOf" in ownership.ownership_zig(named), "routesOf carries the mapping to route.zig")
    path.write_text(TOML.replace('name = "f_two" }', 'name = "f_two", zig = "score.count kill" }'))
    check(raises(lambda: ownership.load(path), "Zig declaration path"), "a malformed zig path is refused")
    (d / "battle").mkdir()
    source = d / "battle/score.zig"
    source.write_text("/// orig: f_two (src/A.c)\n/// Counts a kill.\npub fn countKill() callconv(.c) void {}\n")
    check(ownership.orig_comment_problems(named, d) == [], "a documented Zig function passes")
    source.write_text("/// orig: f_two (src/B.c)\npub fn countKill() callconv(.c) void {}\n")
    check(len(ownership.orig_comment_problems(named, d)) == 1, "the orig line must name the routed unit")
    source.write_text("pub fn countKill() callconv(.c) void {}\n")
    check(len(ownership.orig_comment_problems(named, d)) == 1, "a Zig function without `orig:` is reported")

# A route's `zig` path resolves through the `pub const NAME = @import("file.zig")` lines from the
# overlay's root.zig, in subdirectories too (D2's ports are in game/<overlay>/pc/, PORTING.md §20).
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "battle/pc").mkdir(parents=True)
    (d / "battle/root.zig").write_text('const x = 1;\npub const score = @import("score.zig");\npub const pc = @import("pc/root.zig");\n')
    (d / "battle/pc/root.zig").write_text('// pc/src/a.c\npub const tick = @import("tick_timer.zig");\n')
    check(ownership.zig_source("battle", "score.countKill", d) == (d / "battle/score.zig", "countKill"),
          "a one-module path names game/<overlay>/<module>.zig")
    check(ownership.zig_source("battle", "pc.tick.tickTimer", d) == (d / "battle/pc/tick_timer.zig", "tickTimer"),
          "a nested path follows each root.zig's import, relative to its file")
    check(raises(lambda: ownership.zig_source("battle", "pc.missing.f", d), "missing"),
          "a module root.zig does not import is reported")
    check(raises(lambda: ownership.zig_source("battle", "other.f", d), "other"), "an unknown first module is reported")
    check(ownership.zig_source("battle", "pc.missing.f", d, candidate=True) == (d / "battle/pc/missing.zig", "f"),
          "a candidate port not imported yet is <module>.zig beside the file that would import it")
    check(ownership.zig_source("battle", "fresh.f", d, candidate=True) == (d / "battle/fresh.zig", "f"),
          "a candidate's one-module path is game/<overlay>/<module>.zig, as before D2")

# The real table: every route's Zig function is where its path says, every subsystem fits pc_owner[].
table_now = ownership.load()
check(len(table_now.subsystems) <= ownership.MAX_SUBSYSTEMS, "the real table fits pc_owner[]")
check("pc_owner[%d]" % ownership.MAX_SUBSYSTEMS in (ROOT / "pc/src/ownership.c").read_text(),
      "ownership.c's pc_owner[] holds MAX_SUBSYSTEMS subsystems")
unresolved = []
for s in table_now.subsystems:
    for _, name in s.functions:
        if name in s.zig:
            try:
                file, _ = ownership.zig_source(s.overlay, s.zig[name])
                if not file.exists():
                    unresolved.append(name)
            except ValueError:
                unresolved.append(name)
check(not unresolved, f"every real route resolves to a Zig file: {unresolved[:5]}")
check(all((ROOT / s.unit).exists() for s in table_now.subsystems), "every subsystem's unit is a file of the checkout")
check(ownership.orig_comment_problems(table_now) == [], "every real route has its `orig:` doc line")
many = "".join(f'[subsystem.s{i}]\noverlay="battle"\nunit="src/A.c"\n' for i in range(ownership.MAX_SUBSYSTEMS + 1))
with tempfile.TemporaryDirectory() as d:
    (pathlib.Path(d) / "o.toml").write_text(many)
    check(raises(lambda: ownership.load(pathlib.Path(d) / "o.toml"), "pc_owner"), "more subsystems than pc_owner[] holds is refused")

# for_build: a unit every configuration compiles (boot_sdk.c) must not pull a
# battle subsystem into a build without BATTLE.PRG.
SDK_TOML = """
[subsystem.d2_boot_sdk]
overlay = "battle"
unit = "pc/src/boot_sdk.c"
default = "original"
roots = []
functions = [ { addr = 0x8009820C, name = "func_8009820C" } ]

[subsystem.d2_room_render]
overlay = "battle"
unit = "pc/src/room_render.c"
default = "original"
roots = []
functions = [ ]
"""
with tempfile.TemporaryDirectory() as d:
    (pathlib.Path(d) / "o.toml").write_text(SDK_TOML)
    sdk_table = ownership.load(pathlib.Path(d) / "o.toml")
boot_units = {"pc/src/boot_sdk.c", "src/SLUS_010.40/main.c"}
check(sdk_table.for_build(boot_units, set()).subsystems == [], "the plain boot build has no battle subsystem, boot_sdk.c compiled or not")
check([s.name for s in sdk_table.for_build(boot_units | {"pc/src/room_render.c"}, {"battle"}).subsystems]
      == ["d2_boot_sdk", "d2_room_render"], "the battle build routes both units")
check([s.name for s in sdk_table.for_build(boot_units, {"battle"}).subsystems] == ["d2_boot_sdk"],
      "a subsystem whose unit is not compiled is left out")
check(sdk_table.for_build(boot_units, {"battle"}).problems({"func_8009820C": 0x8009820C}, boot_units) == []
      and sdk_table.for_build(boot_units, {"battle"}).routed_names("pc/src/boot_sdk.c") == {"func_8009820C"},
      "boot_sdk.c is a unit: routed names and the table check work on it")
real = ownership.load()
sdk = [s for s in real.subsystems if s.unit == "pc/src/boot_sdk.c"]
check(len(sdk) == 1 and sdk[0].name == "d2_boot_sdk" and sdk[0].views is not None, "the real table has the d2_boot_sdk skeleton")
check(real.for_build({"pc/src/boot_sdk.c"}, set()).subsystems == [] and
      [s.name for s in real.for_build({"pc/src/boot_sdk.c"}, {"battle"}).subsystems] == ["d2_boot_sdk"],
      "the real table: d2_boot_sdk is active in the battle build only")
root_zig = (ROOT / "game/battle/pc/root.zig").read_text()
specs = (ROOT / "pc/tests/zig_diff/specs.toml").read_text()
check("// pc/src/boot_sdk.c\n" in root_zig and "# ---- pc/src/boot_sdk.c\n" in specs,
      "boot_sdk.c has its header in game/battle/pc/root.zig and in specs.toml")
check(all(f"// {s.unit}\n" in root_zig and f"# ---- {s.unit}\n" in specs
          for s in real.subsystems if s.name.startswith("d2_")), "every d2_ subsystem has both headers")
# Every pc/src unit the build compiles for battle and the D2 tables name is a source of build_boot.py.
build_text = (ROOT / "pc/tools/build_boot.py").read_text()
check(all(f'("{s.unit}", ' in build_text for s in real.subsystems if s.name.startswith("d2_")),
      "every d2_ subsystem's unit is in build_boot.py's sources")

print("FAILED" if failures else "all ownership checks passed")
sys.exit(1 if failures else 0)
