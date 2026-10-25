//! zig_diff spec: func_800A0870 -> func_800A0870.func_800A0870 (30DB0.c).
//! The C body is one store: D_800E8FC0 (a u8) = arg0, so only the low byte of
//! the argument is observable. Inputs: the edge values 0, 1, 255, 256, -1,
//! INT_MIN, INT_MAX and -256, plus random c_int values. Classes separate the
//! negative arguments (bit 31 set), the ones whose low byte is zero, the ones
//! with bits above the low byte, and the plain small values. The global starts
//! as random bytes, so a missing store is visible.
const std = @import("std");
const zd = @import("zdiff");
const port = @import("port");

pub const Args = std.meta.ArgsTuple(@TypeOf(port.func_800A0870));

pub const classes = [_][:0]const u8{ "negative", "low-byte-zero", "high-bits-set", "plain" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const x = ctx.pick(c_int, &.{ 0, 1, 255, 256, -1, std.math.minInt(c_int), std.math.maxInt(c_int), -256 });
    return .{x};
}

pub fn classify(args: Args) usize {
    const x = args[0];
    if (x < 0) return 0;
    if (x & 0xFF == 0) return 1;
    if (x & ~@as(c_int, 0xFF) != 0) return 2;
    return 3;
}
