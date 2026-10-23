//! zig_diff spec: func_80095B70 -> func_80095B70.func_80095B70 (2842C.c).
//! The C is one store: D_800F2270 = arg0, truncated to the global's i16.
//! Inputs: arg0 across its whole int range, weighted toward the i16 edges
//! (0, +-1, 0x7FFF, 0x8000, 0xFFFF, 0x10000, INT_MIN, INT_MAX) and otherwise
//! any value, so the bits above and below the 16-bit store width all vary.
//! Two classes: arg0 representable as an i16 (the store loses nothing) or not
//! (the store wraps). The global starts as random bytes; the store must
//! overwrite it exactly as the C does. No callees, no branches.
const std = @import("std");
const zd = @import("zdiff");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80095B70));

pub const classes = [_][:0]const u8{ "fits-i16", "wraps-i16" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.pick(c_int, &.{ 0, 1, -1, 32767, -32768, 32768, -32769, 65535, 65536, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{arg};
}

pub fn classify(args: Args) usize {
    const x = args[0];
    return if (x >= -32768 and x <= 32767) 0 else 1;
}
