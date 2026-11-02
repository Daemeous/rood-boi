#!/usr/bin/env python3
"""pc/tools/battle_statics.py (PORTING.md §6.5): a synthetic unit with a file
static, two block-scope statics of the same name in two functions (44F14's
`reported`s), one in an inline function that has no out-of-line copy (40564's
`platforms`) and one the configuration does not declare (146C's `sp20`
outside LP64) is compiled -O2 for 32 and 64 bits and globalised from a
STATE.tsv; a C program then writes each through its global name and calls
the unit, which must read the same storage. The views name the statics as
STATE.tsv does (`F::name`) and link to the globalised symbols, and a
configuration's views leave out a static it does not declare. A listed static
the object lacks fails; the reference objects of zig_diff (required=False)
skip it."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import battle_statics  # noqa: E402
import zig_views  # noqa: E402

UNIT = r"""
static int counter;
int bump(void) { return ++counter; }
int first(int x) { if (x) { static int reported; return reported++; } return -1; }
int second(int x) { if (x) { static int reported; reported += 10; return reported; } return -1; }
static inline int* slot(void) { static int cell[2]; return cell; }
int third(void) { return ++slot()[1]; }
#ifdef WIDE
int wide(void) { static int only; return only++; }
#endif
"""
MAIN = r"""
#include <stdio.h>
int bump(void); int first(int); int second(int); int third(void);
extern int unit__counter, unit__first__reported, unit__second__reported, unit__slot__cell[2];
int main(void) {
    int bad = 0;
    unit__counter = 41;          bad |= (bump() != 42) << 0;     bad |= (unit__counter != 42) << 1;
    unit__first__reported = 7;   bad |= (first(1) != 7) << 2;    bad |= (unit__first__reported != 8) << 3;
    unit__second__reported = 100; bad |= (second(1) != 110) << 4; bad |= (unit__first__reported != 8) << 5;
    unit__slot__cell[1] = 4;     bad |= (third() != 5) << 6;     bad |= (unit__slot__cell[1] != 5) << 7;
    printf("%d\n", bad);
    return bad != 0;
}
"""
ROW = "\t".join(["{symbol}", "-", "int", "-", "4", "{storage}", "{defined}", "unit.c", "-", "-", "-", "-", "-", "-", "-"])
HEADER = "\t".join(["symbol", "address", "type", "size_ps1", "size_64", "storage", "defined_in", "used_in_files",
                    "readers", "writers", "through_pointer", "meaning", "lifetime", "ps1_specific", "zig_plan"])


def local(scope, name):
    return ROW.format(symbol=f"{scope}::{name}", defined="-",
                      storage=f"function-local static in {scope} (object symbol {name}.1 in unit.o; the .N suffix "
                              f"is GCC's and not stable)")


STATE = "\n".join([HEADER,
                   ROW.format(symbol="counter", defined="unit", storage="BATTLE C data (...); file static (local symbol)"),
                   local("first", "reported"), local("second", "reported"), local("slot", "cell"),
                   local("wide", "only"),
                   ROW.format(symbol="folded", defined="-", storage="function-local static in x (no symbol: GCC "
                                                                     "folded it into code; port it as a Zig const value)")
                   ]) + "\n"

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "unit.c").write_text(UNIT)
    (d / "main.c").write_text(MAIN)
    (d / "STATE.tsv").write_text(STATE)
    table = battle_statics.listed(d / "STATE.tsv")
    check(table == {"unit": [(None, "counter"), ("first", "reported"), ("second", "reported"), ("slot", "cell"),
                             ("wide", "only")]}, f"STATE.tsv rows listed, the folded one left out ({table})")
    for bits in ("32", "64"):
        obj = d / "unit.o"
        cflags = [f"-m{bits}", "-O2", "-g", "-fno-pie"]
        source = subprocess.run(["gcc", *cflags, "-E", str(d / "unit.c")], capture_output=True, check=True).stdout
        subprocess.run(["gcc", *cflags, "-c", str(d / "unit.c"), "-o", str(obj)], check=True)
        renames = battle_statics.globalise(obj, "unit", table["unit"],
                                           lambda args, o: subprocess.run(["objcopy", *args, str(o)], check=True),
                                           work=d / f"unit{bits}", source=source)
        check(sorted(renames.values()) == ["unit__counter", "unit__first__reported", "unit__second__reported",
                                           "unit__slot__cell"],
              f"{bits}-bit: four statics globalised, the undeclared one skipped ({renames})")
        again = battle_statics.globalise(obj, "unit", table["unit"], lambda args, o: failures.append("objcopy again"),
                                         work=d / f"unit{bits}", source=source)
        check(again == {}, f"{bits}-bit: globalising again changes nothing")
        exe = d / f"main{bits}"
        subprocess.run(["gcc", f"-m{bits}", "-no-pie", str(d / "main.c"), str(obj), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        check(result.returncode == 0, f"{bits}-bit: the C and the global names share each static "
                                      f"(failed checks mask {result.stdout.strip()})")
        dump = zig_views.dump_with_gdb(obj, [], ["counter", "first::reported", "second::reported", "slot::cell"],
                                       d / f"views{bits}.json")
        views = zig_views.emit(dump, {"globals": {}, "fields": {}}, psx_import="psx", unit_object="unit")
        check('pub inline fn first__reported() *i32 { return psx.global(i32, "unit__first__reported"); }' in views
              and 'pub inline fn second__reported() *i32 { return psx.global(i32, "unit__second__reported"); }' in views
              and 'pub inline fn slot__cell() *[2]i32 { return psx.global([2]i32, "unit__slot__cell"); }' in views
              and 'pub inline fn counter() *i32 { return psx.global(i32, "unit__counter"); }' in views,
              f"{bits}-bit: views accessors link to the globalised names")
        if failures:
            print(views)
        # The views of a configuration drop a static only the other declares
        # (146C's LP64 `sp20`): no accessor there, so no gdb failure.
        wide_source = subprocess.run(["gcc", *cflags, "-DWIDE", "-E", str(d / "unit.c")], capture_output=True,
                                     check=True).stdout
        names = ["counter", "first::reported", "wide::only"]
        check(battle_statics.configured(names, table["unit"], source) == names[:2]
              and battle_statics.configured(names, table["unit"], wide_source) == names,
              f"{bits}-bit: the views keep `wide::only` only where the unit declares it")
    # Missing: a fresh object, a static listed for it that it lacks.
    obj = d / "plain.o"
    subprocess.run(["gcc", "-m64", "-O2", "-c", str(d / "unit.c"), "-o", str(obj)], check=True)
    try:
        battle_statics.resolve(obj, "unit", [("first", "gone")])
        check(False, "a listed static the object lacks fails the build")
    except SystemExit as error:
        check("first::gone" in str(error), f"a listed static the object lacks fails the build ({error})")
    check(battle_statics.resolve(obj, "unit", [("first", "gone")], required=False) == {},
          "zig_diff's reference objects skip a listed static they lack")
    # zig_diff's reference object of `second` alone: one `reported`, two listed.
    (d / "one.c").write_text("int second(int x) { if (x) { static int reported; reported += 10; return reported; } "
                             "return -1; }\n")
    one = d / "one.o"
    subprocess.run(["gcc", "-m64", "-O2", "-g", "-c", str(d / "one.c"), "-o", str(one)], check=True)
    renames = battle_statics.resolve(one, "unit", [("first", "reported"), ("second", "reported")], required=False,
                                     work=d / "one")
    check(list(renames.values()) == ["unit__second__reported"],
          f"one static of a name two functions list goes to its own function ({renames})")
print("FAILED" if failures else "battle statics checks passed")
sys.exit(1 if failures else 0)
