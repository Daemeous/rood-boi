//! zig_diff spec: vs_battle_lerp -> lerp.lerp (5BF94.c).
//! Inputs: from and to at the int edges and small values, ratio in the
//! callers' 0..8192 or anywhere (the product wraps); negative products
//! check that the division truncates toward zero (not a shift).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").lerp));

pub const classes = [_][:0]const u8{ "in-range", "negative-product", "product-wraps" };

const ends = [_]c_int{ 0, 1, -1, 4096, -4096, 8191, -8191, 100000, std.math.maxInt(c_int), std.math.minInt(c_int) };

pub fn prepare(ctx: *zd.Ctx) Args {
    const from = ctx.pick(c_int, &ends);
    const to = ctx.pick(c_int, &ends);
    const ratio: c_int = if (ctx.random.boolean()) ctx.range(c_int, 0, 8192) else ctx.pick(c_int, &.{ -1, 8193, 1, 3, -8192, 0x10000 });
    return .{ from, to, ratio };
}

pub fn classify(args: Args) usize {
    const p: i64 = (@as(i64, args[1]) - args[0]) * args[2];
    if (p > std.math.maxInt(i32) or p < std.math.minInt(i32)) return 2;
    if (p < 0 and @rem(p, 8192) != 0) return 1;
    return 0;
}
