//! zig_diff spec: func_800D1824 -> func_800D1824.func_800D1824 (5BF94.c).
//! Inputs: three arena VECTORs (passed through: the callee is a recording stub); steps small (the callers' frame counts) or at the int
//! edges, step small or anywhere: the angle step * 4096 / 2 / steps goes to rcos (a stub), the divisions truncating toward
//! zero for negative values. steps is never 0 (the C traps) and with steps -1 the
//! step is small (no INT_MIN / -1, which traps too).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D1824));

pub const classes = [_][:0]const u8{ "positive", "negative-numerator", "negative-steps" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const from = ctx.alloc(v.VECTOR);
    const to = ctx.alloc(v.VECTOR);
    const out = ctx.alloc(v.VECTOR);
    var steps: c_int = if (ctx.random.boolean()) ctx.range(c_int, 1, 60) else ctx.pick(c_int, &.{ 1, -1, 3, -7, 4096, std.math.maxInt(c_int), std.math.minInt(c_int) });
    var step: c_int = if (ctx.random.boolean()) ctx.range(c_int, -60, 60) else ctx.pick(c_int, &.{ 0, -1, 1, 0x40000, 0x7FFFF, -0x80000, std.math.maxInt(c_int) });
    if (steps == 0) steps = 1;
    if (steps == -1) step = ctx.range(c_int, -60, 60);
    return .{ from, to, steps, step, out };
}

pub fn classify(args: Args) usize {
    if (args[2] < 0) return 2;
    if (args[3] < 0) return 1;
    return 0;
}
