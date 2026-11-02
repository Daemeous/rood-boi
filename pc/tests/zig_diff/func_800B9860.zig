//! zig_diff spec: func_800B9860 -> func_800B9860.func_800B9860 (4A0A8.c).
//! Inputs: a three-byte buffer in the arena (arg0[1] and arg0[2] are the
//! only bytes the C reads), every byte value including 0, 127, 128 and 255
//! so a signed read of the bytes would show, and arg1 at its edges (the C
//! never reads it, so the port must not pass it on). The callee
//! func_80091A1C is not stubbed: the C does not read anything it can change
//! and tests none of its results, so the default int stub (a ctx.value
//! return) is enough. Classes split on the sign of arg1.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9860));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(3, 1);
    if (ctx.chance(3)) buf[1] = ctx.pick(u8, &.{ 0, 127, 128, 255 });
    if (ctx.chance(3)) buf[2] = ctx.pick(u8, &.{ 0, 127, 128, 255 });
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 0) 0 else 1;
}
