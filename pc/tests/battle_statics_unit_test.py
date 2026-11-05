#!/usr/bin/env python3
"""pc/tools/battle_statics.py for pc/src asm-port units (PORTING.md §6.5, §20 "D2"):
STATE.tsv lists only some of such a unit's statics (map_view.c's emitted-span
table not at all), so the object decides. A synthetic unit with file statics
(one STATE.tsv lists, a struct array, a counter, a const table), a function-local
static, a string literal and a static function is compiled and globalised with
globalise_unit: every file-scope static data symbol becomes `<unit>__<name>`, the
rest stay local; a C program writes them through the global names; the views
reach an unlisted static; globalising twice changes nothing; zig_diff's lookup
(file_statics on the built object) finds the globalised names. 32-bit objects
are compiled and checked; they are linked and run only where a 32-bit libc exists."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import battle_statics  # noqa: E402
import zig_views  # noqa: E402

UNIT = r"""
static short clip_left = 3;
static struct { const unsigned* packet; short row; } emitted[4];
static int emitted_count;
static const short table[3] = { 1, 2, 3 };
__attribute__((noinline)) static int helper(int x) { return x + clip_left; }
void set_clip(short v) { clip_left = v; }
int peek(int i) { return emitted[i & 3].row + clip_left; }
int use(int i) {
    static int local_only;
    const char* s = "text";
    emitted[i & 3].row = (short)helper(i) + table[i % 3] + s[0];
    local_only += emitted[i & 3].row;
    return ++emitted_count + local_only;
}
"""
MAIN = r"""
extern int use(int);
extern short pcunit__clip_left;
extern int pcunit__emitted_count;
int main(void) {
    pcunit__emitted_count = 10;
    pcunit__clip_left = 1;
    return use(0) != 0 && pcunit__emitted_count == 11 ? 0 : 1;
}
"""
failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


def can_link(bits, d):
    (d / "probe.c").write_text("int main(void){return 0;}\n")
    return subprocess.run(["gcc", f"-m{bits}", "-no-pie", str(d / "probe.c"), "-o", str(d / f"probe{bits}")],
                          capture_output=True).returncode == 0


with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "pcunit.c").write_text(UNIT)
    (d / "main.c").write_text(MAIN)
    for bits in ("32", "64"):
        obj = d / f"pcunit{bits}.o"
        subprocess.run(["gcc", f"-m{bits}", "-O2", "-g", "-fno-pie", "-c", str(d / "pcunit.c"), "-o", str(obj)],
                       check=True)
        before = battle_statics.file_statics(obj, "pcunit")
        check(before == ["clip_left", "emitted", "emitted_count", "table"],
              f"{bits}-bit: the object's file statics (not local_only.N, the literal, helper): {before}")
        calls = []

        def objcopy(args, o):
            calls.append(args)
            subprocess.run(["objcopy", *args, str(o)], check=True)

        # STATE.tsv lists clip_left only: the other three come from the object.
        extra = battle_statics.globalise_unit(obj, "pcunit", [(None, "clip_left")], objcopy, work=d / f"pc{bits}")
        check(extra == [(None, "emitted"), (None, "emitted_count"), (None, "table")],
              f"{bits}-bit: the statics STATE.tsv does not list are returned: {extra}")
        check(battle_statics.file_statics(obj, "pcunit") == before, f"{bits}-bit: the same names after globalising")
        nm = subprocess.run(["nm", str(obj)], capture_output=True, text=True, check=True).stdout.split("\n")
        check(all(any(line.endswith(f" {kind} pcunit__{name}") for line in nm)
                  for kind, name in (("B", "emitted"), ("B", "emitted_count"), ("D", "clip_left"), ("R", "table")))
              and any(" b local_only." in line for line in nm) and any(" t helper" in line for line in nm),
              f"{bits}-bit: the four are global, the function-local static and the static function stay local")
        calls.clear()
        again = battle_statics.globalise_unit(obj, "pcunit", [(None, "clip_left")], objcopy, work=d / f"pc{bits}")
        check(again == extra and not calls, f"{bits}-bit: globalising a unit again changes nothing ({again}, {calls})")
        # zig_diff's reference object: only the statics its kept bodies use exist.
        (d / "ref.c").write_text("static int emitted_count;\nint use(void) { return ++emitted_count; }\n")
        ref = d / f"ref{bits}.o"
        subprocess.run(["gcc", f"-m{bits}", "-O2", "-fno-pie", "-c", str(d / "ref.c"), "-o", str(ref)], check=True)
        names = [(None, n) for n in battle_statics.file_statics(obj, "pcunit")]
        renamed = battle_statics.globalise(ref, "pcunit", names, objcopy, required=False, work=d / f"ref{bits}")
        check(renamed == {"emitted_count": "pcunit__emitted_count"},
              f"{bits}-bit: the reference object takes the built unit's names for what it holds ({renamed})")
        dump = zig_views.dump_with_gdb(obj, [], ["emitted_count", "clip_left"], d / f"views{bits}.json")
        views = zig_views.emit(dump, {"globals": {}, "fields": {}}, psx_import="psx", unit_object="pcunit")
        check('psx.global(i32, "pcunit__emitted_count")' in views and 'psx.global(i16, "pcunit__clip_left")' in views,
              f"{bits}-bit: the views reach an unlisted static through its global name")
        if can_link(bits, d):
            exe = d / f"main{bits}"
            subprocess.run(["gcc", f"-m{bits}", "-no-pie", str(d / "main.c"), str(obj), "-o", str(exe)], check=True)
            check(subprocess.run([str(exe)]).returncode == 0, f"{bits}-bit: the C and the global names share the statics")
        else:
            print(f"skip: {bits}-bit link (no {bits}-bit libc here)")

# room_render.c declares several statics in one declaration with initializers.
listed = b"static short near_depth = 8, far_depth = 0x7ff, clip_top;\nstatic int f(void) { int far2 = 1; return far2; }\n"
check([battle_statics.declared(listed, n) for n in ("near_depth", "far_depth", "clip_top", "far2")] == [True, True, True, False],
      "declared: every declarator of a static list with initializers, not a function's local")

print("FAILED" if failures else "battle statics (pc/src unit) checks passed")
sys.exit(1 if failures else 0)
