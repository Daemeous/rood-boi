//! zig_diff spec: func_800DCBD8 -> func_800DCBD8.func_800DCBD8 (74088.c).
//! Inputs: an arg0 whose unkC_0 id is any 4-bit value; D_800F5900 set to an
//! arena table of 0-12 entries (sometimes none), each with a unk0 that matches
//! the id a third of the time (or is an edge: 0, 15, 16 never matches, 255), a
//! unk2 threshold from edges and random bytes, and a unk40 from zero, -1,
//! INT_MIN, 1, INT_MAX and random values. Sometimes the entry one past the end
//! is a matching decoy the C must not read. Several entries often match, so a
//! dropped `break` changes the threshold. The sum threshold + threshold>>2 +
//! threshold>>5 wraps in the 8-bit unkC_19 field for large thresholds.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_74088");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800DCBD8));

pub const classes = [_][:0]const u8{ "empty", "miss", "hit-first", "hit-later" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.func_800DCAA0_t);
    const id: u4 = ctx.int(u4);
    psx.setBits(arg, "unkC_0", id);

    const n: u16 = if (ctx.chance(6)) 0 else ctx.range(u16, 1, 12);
    // n + 1 entries: the last one is past the end of the table the C reads.
    const raw = ctx.allocBytes(76 + (@as(usize, n) + 1) * 68, 16);
    const d: *v.func_800DEEA4_t = @ptrCast(@alignCast(raw));
    d.unk4A = n;
    v.g.D_800F5900().* = d;

    var i: u16 = 0;
    while (i <= n) : (i += 1) {
        const e = psx.byteAt(v.func_800DEB10_t, d, 76 + @as(isize, i) * 68);
        e.unk0 = if (ctx.chance(3)) @as(u8, id) else ctx.pick(u8, &.{ 0, 15, 16, 255 });
        e.unk2 = ctx.pick(u8, &.{ 0, 1, 127, 128, 200, 255 });
        e.unk40 = ctx.pick(i32, &.{ 0, 0, -1, std.math.minInt(i32), 1, 0x7fffffff });
    }
    if (ctx.chance(3)) {
        // A matching decoy past the end: reading it changes the result.
        const e = psx.byteAt(v.func_800DEB10_t, d, 76 + @as(isize, n) * 68);
        e.unk0 = @as(u8, id);
        e.unk40 = 0;
    }
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0].?;
    const id: u8 = psx.getBits(arg, "unkC_0");
    const d = v.g.D_800F5900().*.?;
    var i: u16 = 0;
    while (i < d.unk4A) : (i += 1) {
        const e = psx.byteAt(v.func_800DEB10_t, d, 76 + @as(isize, i) * 68);
        if (e.unk40 == 0 and e.unk0 == id) return if (i == 0) 2 else 3;
    }
    return if (d.unk4A == 0) 0 else 1;
}
