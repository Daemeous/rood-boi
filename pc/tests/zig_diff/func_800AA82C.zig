//! zig_diff spec: func_800AA82C -> func_800AA82C.func_800AA82C (40564.c).
//! The C body is one call, func_800AA984(arg0, arg1, arg2), with no branches
//! and no comparisons. The inputs are random c_int and c_short arguments, with
//! edge values (0, -1, the int and short extremes) picked often, so that
//! argument swaps, truncation of the short and a dropped call all show in the
//! callee's recorded arguments. func_800AA984 is void and the C reads nothing
//! after it, so the default recording stub models it fully.
const std = @import("std");
const zd = @import("zdiff");
const port = @import("port");

pub const Args = std.meta.ArgsTuple(@TypeOf(port.func_800AA82C));

pub const classes = [_][:0]const u8{ "short-negative", "short-non-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0 = ctx.pick(c_int, &.{ 0, -1, 1, 0x7FFF_FFFF, -0x8000_0000, 0x1_0000, 0xFFFF });
    const a1 = ctx.pick(c_short, &.{ 0, -1, 1, 0x7FFF, -0x8000, 0x4000 });
    const a2 = ctx.pick(c_int, &.{ 0, -1, 0x7FFF_FFFF, -0x8000_0000, 0x8000, 0x1_0000 });
    return .{ a0, a1, a2 };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 0) 0 else 1;
}
