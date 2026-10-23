//! zig_diff spec: func_8009406C -> func_8009406C.func_8009406C (2842C.c).
//! Inputs: arg0 is a tag that is matched against each entry's second byte
//! (sometimes a value outside 0..255, which the C never matches); arg1 and
//! arg2 are random; arg3 has bit 0x80 set or clear. The table at
//! *D_800F1BAC holds 0-5 entries (the count is sometimes -1), with tags and
//! unk6 signs around the match, and displayTextLen around 0x25 so that
//! frame equals 0x25 sometimes. The probe stub fills its out-parameter
//! with coordinates at the 0x141 and 0xF1 edges (negative ones too) and
//! returns 0 or nonzero; it may also set D_800F227E, which the C's trailing
//! `++` reads back. The allocator stub hands out an arena block for the
//! new entry.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009406C));

pub const classes = [_][:0]const u8{ "bit7-clear", "probe-fails", "x-too-big", "y-too-big", "placed" };

var sv_ret: c_int = 0;
var sv_vx: i16 = 0;
var sv_vy: i16 = 0;
var sv_bump: bool = false;
var sv_count: u8 = 0;

pub const stubs = struct {
    pub fn func_800A1280(ctx: *zd.Ctx, a: anytype, b: anytype, out: anytype, d: anytype) c_int {
        _ = ctx;
        _ = a;
        _ = b;
        _ = d;
        const s: *v.SVECTOR = @ptrCast(@alignCast(out));
        s.vx = sv_vx;
        s.vy = sv_vy;
        if (sv_bump) v.g.D_800F227E().* = sv_count;
        return sv_ret;
    }

    pub fn func_800962E4(ctx: *zd.Ctx) ?*anyopaque {
        return ctx.alloc(v.D_800F1BAC_t);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = ctx.alloc([32]v.D_800F1BAC_t);
    const arg0 = ctx.pick(c_int, &.{ 0, 1, 2, 0x7F, 0x80, 0xFF, 256, -1 });
    const tag: u8 = @truncate(@as(c_uint, @bitCast(arg0)));

    var count: i16 = ctx.range(i16, 0, 5);
    if (ctx.chance(12)) count = -1;
    const live: usize = @intCast(@max(count, 0));
    for (table[0..live]) |*e| {
        e.unk6 = ctx.pick(i16, &.{ 0, 0, -1, 1 });
        e.unk8.u8[0] = ctx.pick(u8, &.{ 2, 2, 2, 3 });
        e.unk8.u8[1] = if (ctx.chance(2)) tag else ctx.int(u8);
        e.unkB = ctx.int(u8);
        e.displayTextLen = ctx.pick(u8, &.{ 0, 12, 24, 25, 25, 26 });
    }
    v.g.D_800F1BAC().* = @ptrCast(table);
    v.g.D_800F227E().* = count;

    sv_ret = if (ctx.chance(4)) ctx.edge(c_int, &.{ 1, -1, 256 }) else 0;
    sv_vx = switch (ctx.range(u8, 0, 5)) {
        0 => 0x141,
        1 => 0x140,
        2 => -1,
        3 => ctx.range(i16, 0, 0x140),
        4 => ctx.range(i16, 0x141, 0x7FFF),
        else => ctx.int(i16),
    };
    sv_vy = switch (ctx.range(u8, 0, 5)) {
        0 => 0xF1,
        1 => 0xF0,
        2 => -1,
        3 => ctx.range(i16, 0, 0xF0),
        4 => ctx.range(i16, 0xF1, 0x7FFF),
        else => ctx.int(i16),
    };
    sv_bump = ctx.chance(3);
    sv_count = ctx.pick(u8, &.{ 0, 1, 2, 36, 37 });

    var arg3 = ctx.int(c_int);
    if (ctx.chance(2)) arg3 |= 0x80 else arg3 &= ~@as(c_int, 0x80);
    const arg1 = ctx.int(c_int);
    const arg2 = ctx.int(c_int);
    return .{ arg0, arg1, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    if ((args[3] & 0x80) == 0) return 0;
    if (sv_ret != 0) return 1;
    if (@as(u16, @bitCast(sv_vx)) >= 0x141) return 2;
    if (@as(u16, @bitCast(sv_vy)) >= 0xF1) return 3;
    return 4;
}
