//! zig_diff spec: func_800BB4CC -> func_800BB4CC.func_800BB4CC (4A0A8.c).
//! Inputs: a small arena buffer whose byte 1 is an edge value (0, 1, 127,
//! 128, 255, so the sign bit is both set and clear), and an arg1 short
//! edge value (the C never reads it). The callee's return value is ignored
//! and the C always returns 0. The callee's default int stub is used, since
//! nothing the C reads changes across the call.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BB4CC));

pub const classes = [_][:0]const u8{ "high-byte", "low-byte" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(4, 1);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 127, 128, 255 });
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 128) 0 else 1;
}
