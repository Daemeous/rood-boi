//! zig_diff spec: func_800CE644 -> func_800CE644.func_800CE644 (5BF94.c).
//! The C is `int func_800CE644(int arg0) { }`: it does nothing and falls off
//! its end, so the result register's leftover is the return value. The spec
//! checks that the port touches no state (the arena, the scratchpad and the
//! globals are compared) and makes no call; the argument is any int.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CE644));

pub const classes = [_][:0]const u8{ "any" };

pub const ignore_return = "the C has an empty body and falls off its end: the result register's leftover, which no port can reproduce; func_800D27F0 ignores it, func_800D5150 passes it on as an effect callback result (NOTES-xl.md)";

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_int, &.{ 0, 0x14, 0x1E, -1, 0x7FFFFFFF, -2147483648 })};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
