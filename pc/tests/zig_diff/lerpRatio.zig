//! zig_diff spec: vs_battle_lerpRatio -> lerp_ratio.lerpRatio (5BF94.c).
//! Inputs: from and to at the int edges and small values; step and steps
//! small (the callers' frame counters) or anywhere, steps never 0 and never
//! -1 against a wrapped INT_MIN (both trap in the C). Negative ratios and
//! products check that both divisions truncate toward zero.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").lerpRatio));

pub const classes = [_][:0]const u8{ "in-range", "negative", "wraps" };

const ends = [_]c_int{ 0, 1, -1, 4096, -4096, 100000, std.math.maxInt(c_int), std.math.minInt(c_int) };

pub fn prepare(ctx: *zd.Ctx) Args {
    const from = ctx.pick(c_int, &ends);
    const to = ctx.pick(c_int, &ends);
    var steps: c_int = if (ctx.random.boolean()) ctx.range(c_int, 1, 64) else ctx.pick(c_int, &.{ 1, -1, 3, -7, 0x10000 });
    const step: c_int = if (ctx.random.boolean()) ctx.range(c_int, 0, 64) else ctx.pick(c_int, &.{ -1, 3, 0x80000, 0x7FFFF, -0x80000 });
    if (steps == 0) steps = 1;
    if (steps == -1 and step *% 4096 == std.math.minInt(c_int)) steps = 3;
    return .{ from, to, step, steps };
}

pub fn classify(args: Args) usize {
    const r: i64 = @divTrunc(@as(i64, args[2] *% 4096), args[3]);
    const p: i64 = (@as(i64, args[1]) - args[0]) * r;
    if (p > std.math.maxInt(i32) or p < std.math.minInt(i32)) return 2;
    if (p < 0 and @rem(p, 4096) != 0) return 1;
    return 0;
}
