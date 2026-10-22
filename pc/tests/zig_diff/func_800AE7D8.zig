//! zig_diff spec: func_800AE7D8 -> func_800AE7D8.func_800AE7D8 (44F14.c).
//! Inputs: arg0 points into the arena (the C only offsets it by 0x1C and
//! passes it on, never dereferencing it, so any arena address is safe). arg1
//! is any int, edges included. arg2 is zero, negative or positive, with the
//! edges 1, -1, INT_MAX and INT_MIN, so the `!= 0` test sees both signs of a
//! nonzero value and the zero case that skips the second call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE7D8));

pub const classes = [_][:0]const u8{ "arg2-zero", "arg2-negative", "arg2-positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    // A block in the arena: the C offsets the pointer and hands it to the
    // recording stub, which never reads it.
    const base = ctx.allocBytes(128, 16);
    const arg0: ?*anyopaque = @ptrCast(base);
    const arg1: c_int = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000, 0x7E });
    const arg2: c_int = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFF_FFFF, -0x8000_0000 });
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] == 0) return 0;
    if (args[2] < 0) return 1;
    return 2;
}
