#!/usr/bin/env python3
"""pc/tools/zig_views.py: a synthetic unit with bitfields (unsigned chars,
-funsigned-char), a psx_ptr (4 bytes on both builds), a union, arrays, an
unsized global array, a native pointer, a nested anonymous struct and a packed
struct is compiled for 32 and 64 bits; its DWARF becomes Zig views (with a
renamed global, field and bitfield, PORTING.md §16); a C program writes a
filled instance's bytes and `zig test` reads every field back through the
views (the generated comptime asserts also check every offset and size).
A union with a bitfield member is refused."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import zig_toolchain  # noqa: E402
import zig_views  # noqa: E402

UNIT = r"""
#include "pc/src/psx_types.h"
typedef struct { unsigned char lo : 3; unsigned char hi : 5; signed char s : 4; } Bits;
typedef struct Node Node;
struct Node { psx_ptr(Node) next; short x, y; };
typedef struct {
    char id;
    Bits bits;
    union { int i; short h[2]; } u;
    short arr[3];
    Node* native;
    Node node;
    struct { int z; } inner;
    long long wide;
} Rec;
typedef struct __attribute__((packed)) { char c; short s; } Packed;
Rec rec;
Packed packed;
extern short table[];
short* use_table(void) { return table; }
"""
FILL = r"""
#include <stdio.h>
#include <string.h>
short table[4];
int main(void) {
    memset(&rec, 0, sizeof rec);
    rec.id = 200; rec.bits.lo = 5; rec.bits.hi = 17; rec.bits.s = -3;
    rec.u.h[0] = 0x1234; rec.u.h[1] = -2; rec.arr[2] = 77;
    rec.native = (Node*)(unsigned long)0x4000; psx_set_raw(rec.node.next, Node*, 0x1000);
    rec.node.y = -9; rec.inner.z = 123456; rec.wide = -5;
    for (unsigned i = 0; i < sizeof rec; i++) printf("%u,", ((unsigned char*)&rec)[i]);
    return 0;
}
"""
TEST = """
const std = @import("std");
const psx = @import("psx");
const v = @import("views");
const bytes align(16) = [_]u8{ %s };
test "fields through the views" {
    const r: *const v.Rec = @ptrCast(&bytes);
    try std.testing.expectEqual(@as(u8, 200), r.id);
    try std.testing.expectEqual(@as(u3, 5), psx.getBits(&r.bits, "low"));
    try std.testing.expectEqual(@as(u5, 17), psx.getBits(&r.bits, "hi"));
    try std.testing.expectEqual(@as(i4, -3), psx.getBits(&r.bits, "s"));
    try std.testing.expectEqual(@as(i16, 0x1234), r.u.h[0]);
    try std.testing.expectEqual(@as(i16, -2), r.u.h[1]);
    try std.testing.expectEqual(@as(i16, 77), r.values[2]);
    try std.testing.expectEqual(@as(usize, 0x4000), @intFromPtr(r.native.?));
    try std.testing.expectEqual(@as(usize, 0x1000), @intFromPtr(psx.get(r.node.next).?));
    try std.testing.expectEqual(@as(i16, -9), r.node.y);
    try std.testing.expectEqual(@as(i32, 123456), r.inner.z);
    try std.testing.expectEqual(@as(i64, -5), r.wide);
    try std.testing.expectEqual(@as(usize, 1), @offsetOf(v.Packed, "s"));
    try std.testing.expectEqual(@as(usize, 3), @sizeOf(v.Packed));
}
"""
failures = []
zig = str(zig_toolchain.ensure())
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "unit.c").write_text(UNIT)
    (d / "fill.c").write_text('#include "unit.c"\n' + FILL)
    (d / "MEANINGS.tsv").write_text("symbol\tmeaning\tvalue_lifetime\tzig_name\n"
                                    "rec\ta record\tstatic\trecord\n"
                                    "Rec.arr\tthree values\tstatic\tvalues\n"
                                    "Bits.lo\tthe low bits\tstatic\tlow\n"
                                    "Rec.id\tits id\tstatic\t\n")
    names = zig_views.load_names(d / "MEANINGS.tsv")
    for bits, flags, target in [("32", ["-m32"], "x86-linux-musl"),
                                ("64", ["-m64", "-DROOD_PC_LP64"], "x86_64-linux-musl")]:
        common = [*flags, "-g", "-funsigned-char", "-DROOD_PC", f"-I{ROOT}", f"-I{d}"]
        obj = d / f"unit{bits}.o"
        subprocess.run(["gcc", *common, "-c", str(d / "unit.c"), "-o", str(obj)], check=True)
        subprocess.run(["gcc", *common, str(d / "fill.c"), "-o", str(d / f"fill{bits}")], check=True)
        values = subprocess.run([str(d / f"fill{bits}")], capture_output=True, text=True, check=True).stdout
        dump = zig_views.dump_with_gdb(obj, ["Rec", "Packed", "Node"], ["rec", "packed", "table"],
                                       d / f"dump{bits}.json")
        views = zig_views.emit(dump, names, psx_import="psx")
        (d / f"views{bits}.zig").write_text(views)
        (d / f"test{bits}.zig").write_text(TEST % values.rstrip(","))
        ok = "/// orig: rec\n    pub inline fn record()" in views and "/// orig: arr\n    values:" in views \
            and '"rec")' in views and 'pub inline fn table() [*]i16 { return psx.globalMany(i16, "table"); }' in views
        print(("ok:   " if ok else "FAIL: ") + f"{bits}-bit: renamed accessor and field keep their orig: lines and symbol; unsized array is [*]")
        if not ok:
            failures.append(bits + " names")
        result = subprocess.run([zig, "test", "-target", target, "--dep", "psx", "--dep", "views",
                                 f"-Mroot={d / f'test{bits}.zig'}", "--dep", "psx", f"-Mviews={d / f'views{bits}.zig'}",
                                 f"-Mpsx={ROOT / 'game/common/psx.zig'}"], capture_output=True, text=True)
        ok = result.returncode == 0
        print(("ok:   " if ok else "FAIL: ") + f"{bits}-bit views read every field")
        if not ok:
            print(result.stderr[-3000:])
            print(views[:3000])
            failures.append(bits)
# A union with a bitfield member is refused, not emitted full-width.
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "bad.c").write_text("typedef union { unsigned int flag : 1; unsigned short h; } Bad;\nBad bad;\n")
    obj = d / "bad.o"
    subprocess.run(["gcc", "-m64", "-g", "-c", str(d / "bad.c"), "-o", str(obj)], check=True)
    dump = zig_views.dump_with_gdb(obj, ["Bad"], ["bad"], d / "bad.json")
    try:
        zig_views.emit(dump, {"globals": {}, "fields": {}}, psx_import="psx")
        ok, message = False, "emitted"
    except SystemExit as error:
        message = str(error)
        ok = "bitfield member" in message and "flag" in message
    print(("ok:   " if ok else "FAIL: ") + f"a union's bitfield member is refused ({message})")
    if not ok:
        failures.append("union bitfield")
# A global the C declares only as a block-scope `extern` (146C's vs_battle_exec,
# 5BF94's vs_battle_renderImage) has no global or static symbol: gdb has no frame
# to find it by, so the strict dump fails; the lenient one (the pipeline's plan)
# leaves it out, and the plan then skips that function.
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    (d / "scoped.c").write_text("int shown;\nint use(void)\n{\n    extern int hidden;\n    return hidden + shown;\n}\n")
    obj = d / "scoped.o"
    subprocess.run(["gcc", "-m64", "-g", "-c", str(d / "scoped.c"), "-o", str(obj)], check=True)
    try:
        zig_views.dump_with_gdb(obj, [], ["shown", "hidden"], d / "strict.json")
        strict_fails = False
    except SystemExit:
        strict_fails = True
    dump = zig_views.dump_with_gdb(obj, [], ["shown", "hidden"], d / "lenient.json", lenient=True)
    ok = strict_fails and "shown" in dump["globals"] and "hidden" not in dump["globals"]
    print(("ok:   " if ok else "FAIL: ") + "a block-scope extern is missing from the lenient dump (strict refuses it)")
    if not ok:
        failures.append("block-scope extern")
print("FAILED" if failures else "zig views checks passed")
sys.exit(1 if failures else 0)
