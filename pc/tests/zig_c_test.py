#!/usr/bin/env python3
"""pc/tools/zig_c.py: a synthetic unit's .i (statics, a static table, a
static function, a header `static inline`, a data definition, an extern and
an enum) becomes a `c` module in which
  - the unit's data definition is an extern declaration,
  - every file-scope static is a @compileError naming it (owned code cannot
    reach translate-c's copies: they are not the C's variables),
  - externs, prototypes, enum constants and types stay usable;
a Zig file using the usable ones compiles, and one using each static fails
with that static's name."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import zig_c  # noqa: E402
import zig_toolchain  # noqa: E402

UNIT = r"""
typedef struct { short vx, vy, vz, pad; } SVECTOR;
enum { kindA = 1, kindB = 2 };
static inline unsigned int psx_addr(const void* p) { return (unsigned int)(unsigned long)p; }
extern int shared_count;
extern void marker_symbol;
int defined_table[3] = { 1, 2, 3 };
static int skip_from_set;
static unsigned skip_from = 4, skip_to;
static short const lookup[] = { 8, 14, 16 };
static SVECTOR origin = { 0, 0, 0, 0 };
static void (*hook)(int);
static int helper(int index) { return lookup[index] + skip_from_set; }
void api(SVECTOR* v);
int user(int k) { static int calls; calls++; return helper(k) + shared_count + (int)(long)&marker_symbol; }
"""
USES_OK = """
const c = @import("c");
export fn probe(v: *c.SVECTOR) c_int {
    c.api(v);
    return c.shared_count + c.kindB + c.user(1) + c.defined_table[0] + @as(c_int, @intCast(@intFromPtr(&c.marker_symbol) & 0));
}
"""
STATICS = ["psx_addr", "skip_from_set", "skip_from", "skip_to", "lookup", "origin", "hook", "helper"]

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)


zig = str(zig_toolchain.ensure())
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "unit.c").write_text(UNIT)
    subprocess.run(["gcc", "-E", "-P", str(d / "unit.c"), "-o", str(d / "unit.i")], check=True)
    found = zig_c.static_names((d / "unit.i").read_bytes())
    # Over-approximate by design (a static's type name is in it too: only
    # translated values and functions are acted on, and SVECTOR stays a type).
    check(set(STATICS) <= found and not ({"shared_count", "defined_table", "api", "user", "calls", "kindA"} & found),
          f"static_names finds the file-scope statics, not the externs ({sorted(found)})")
    module = zig_c.translate(zig, d / "unit.i", d / "c_unit.zig", "x86_64-linux-none")
    text = module.read_text()
    check("pub extern var defined_table" in text, "a data definition becomes an extern")
    check("pub var " not in text, "no mutable copy is left")
    for name in STATICS:
        check(f'pub const {name} = @compileError("c_unit.{name} is a file-scope static' in text,
              f"{name} is a compile error")

    def build(source):
        (d / "use.zig").write_text(source)
        return subprocess.run([zig, "build-obj", "-target", "x86_64-linux-none", "-fno-emit-bin",
                               "--dep", "c", f"-Mroot={d / 'use.zig'}", f"-Mc={module}"],
                              capture_output=True, text=True)

    result = build(USES_OK)
    check(result.returncode == 0, "externs, prototypes, enum constants and types stay usable"
          + ("" if result.returncode == 0 else ": " + result.stderr[-1500:]))
    for name in STATICS:
        result = build(f'const c = @import("c");\nexport fn probe() void {{ _ = &c.{name}; }}\n')
        check(result.returncode != 0 and f"c_unit.{name} is a file-scope static" in result.stderr,
              f"using {name} fails the build, naming it")
print("FAILED" if failures else "zig c checks passed")
sys.exit(1 if failures else 0)
