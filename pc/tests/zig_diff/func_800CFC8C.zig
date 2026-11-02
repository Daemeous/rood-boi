//! zig_diff spec: func_800CFC8C -> func_800CFC8C.func_800CFC8C (5BF94.c).
//! Inputs: from and to at the int edges, steps nonzero (small or at the
//! edges), step small or anywhere (step * 4096 wraps); rcos is a stub
//! returning any int, so the weighted product wraps too.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CFC8C));

pub const classes = [_][:0]const u8{ "positive", "negative-step", "negative-steps" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const from = ctx.pick(c_int, &.{ 0, 1, -1, 4096, std.math.maxInt(c_int), std.math.minInt(c_int) });
    const to = ctx.pick(c_int, &.{ 0, 1, -1, 4096, std.math.maxInt(c_int), std.math.minInt(c_int) });
    var steps: c_int = if (ctx.random.boolean()) ctx.range(c_int, 1, 60) else ctx.pick(c_int, &.{ 1, -1, 3, -7, 4096, std.math.maxInt(c_int), std.math.minInt(c_int) });
    if (steps == 0) steps = 1;
    var step: c_int = if (ctx.random.boolean()) ctx.range(c_int, -60, 60) else ctx.pick(c_int, &.{ 0, -1, 1, 0x40000, 0x7FFFF, -0x80000, std.math.maxInt(c_int) });
    if (steps == -1) step = ctx.range(c_int, -60, 60); // a mutant's step * 4096 / -1 must not trap
    return .{ from, to, steps, step };
}

pub fn classify(args: Args) usize {
    if (args[2] < 0) return 2;
    return if (args[3] < 0) 1 else 0;
}
