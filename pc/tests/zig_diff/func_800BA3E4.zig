//! zig_diff spec: func_800BA3E4 -> func_800BA3E4.func_800BA3E4 (4A0A8.c).
//! The C body is one call to func_80045DC0 followed by `return 0;`: no
//! branch, and both arguments are unused. The inputs give arg0 a valid
//! arena block, so a store through it would be caught, and arg1 any short,
//! negative and not, so a port that returns or uses arg1 is caught.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BA3E4));

pub const classes = [_][:0]const u8{ "arg1-negative", "arg1-nonnegative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @ptrCast(ctx.allocBytes(16, 16));
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 0x7fff, -32768 });
    return .{ buf, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[1] < 0) 0 else 1;
}
