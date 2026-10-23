//! zig_diff spec: func_800AA8D0 -> func_800AA8D0.func_800AA8D0 (40564.c).
//! Inputs: an index 0..16 into D_800F4538 whose entry is null or a random-filled
//! actor in the arena; a raw angle arg1 that is either a random short (so the
//! int-to-short stores wrap) or the facing plus an edge value (0, +-2048, 2047,
//! 2049, 4095, the short extremes), so the wrapped angle lands on every edge of
//! the +-2048 window; and a nonzero step arg2 (edges, both signs, and divisors of
//! the wrapped angle so the remainder is zero). arg2 == 0 divides by zero in the
//! C, so it is never built; that leaves the `arg2 >= 0` test with no equal sides.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA8D0));

pub const classes = [_][:0]const u8{
    "null-entry",
    "wrap-high",
    "wrap-low",
    "in-range-exact",
    "in-range-remainder",
};

const angle_edges = [_]i32{ 0, 1, -1, 2047, 2048, 2049, -2047, -2048, -2049, 4095, -4096, 4096, 32767, -32768 };

/// The C's angle step: arg1 - facing stored as a short, then one step of 4096
/// towards the +-2048 window.
fn wrapped(arg1: c_short, facing: i16) i16 {
    var a: i16 = @as(i16, @intCast(arg1)) -% facing;
    if (a > 2048) a -%= 4096;
    if (a < -2048) a +%= 4096;
    return a;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const facing = ctx.pick(i16, &.{ 0, 1, -1, 2048, -2048, 32767, -32768 });
    actor.unk0.facing = facing;

    const idx: i32 = ctx.range(i32, 0, 16);
    const table = v.g.D_800F4538();
    table[@intCast(idx)] = if (ctx.chance(6)) null else actor;

    var arg1: c_short = undefined;
    if (ctx.chance(5)) {
        arg1 = @truncate(ctx.int(i32));
    } else {
        const r: i32 = if (ctx.chance(4)) @as(i32, ctx.int(i16)) else ctx.edge(i32, &angle_edges);
        arg1 = @truncate(r +% @as(i32, facing));
    }

    const n = wrapped(arg1, facing);
    var d: i32 = ctx.pick(i32, &.{ 1, -1, 2, -2, 3, 7, 4096, 0x7FFF_FFFF, -0x7FFF_FFFF, 0x10000 });
    if (d == 0 or d == std.math.minInt(i32)) d = 3;
    if (ctx.chance(3) and n != 0) {
        d = n;
        if (ctx.chance(2)) d = -d;
    }

    const arg0: c_int = @intCast(idx);
    const arg2: c_int = @intCast(d);
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    const actor = table[@intCast(args[0])] orelse return 0;
    const n: i64 = wrapped(args[1], actor.unk0.facing);
    if (n > 2048) return 1;
    if (n < -2048) return 2;
    const d: i64 = @intCast(args[2]);
    const m = if (n < 0) -n else n;
    const a = if (d < 0) -d else d;
    return if (@rem(m, a) == 0) 3 else 4;
}
