//! zig_diff spec: func_800BBE04 -> func_800BBE04.func_800BBE04 (4A0A8.c).
//! Inputs: an arbitrary c_int argument, drawn from edges (0, 1, -1, 255, 256,
//! 0x7FFFFFFF, INT_MIN) and random values. The C stores the low byte of the
//! argument into D_800F4BA0 (a u8 global, compared as part of the state), so
//! the classes separate negative arguments, arguments whose low byte is zero,
//! arguments wider than a byte, and plain bytes. There are no branches,
//! callees or pointers to model.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BBE04));

pub const classes = [_][:0]const u8{ "negative", "zero-low-byte", "wide", "byte" };

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ctx.pick(c_int, &.{ 0, 1, -1, 255, 256, 0x7FFFFFFF, -2147483648 })};
}

pub fn classify(args: Args) usize {
    const x = args[0];
    if (x < 0) return 0;
    if (x & 0xFF == 0) return 1;
    if (x > 255) return 2;
    return 3;
}
