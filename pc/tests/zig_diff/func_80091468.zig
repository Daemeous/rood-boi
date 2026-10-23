//! zig_diff spec: func_80091468 -> func_80091468.func_80091468 (2842C.c).
//! Inputs: two c_int arguments, each drawn from edge values (0, -1, the i16
//! limits, one past each limit, 65536, the c_int extremes) or any random
//! value. The C only stores each argument's low 16 bits into the i16 globals
//! D_800F1DB4 and D_800F1DB6, so the out-of-i16-range values exercise the
//! truncation. The C has no branches and no callees, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80091468));

pub const classes = [_][:0]const u8{ "both-fit-i16", "wraps-to-i16" };

const edges = [_]c_int{
    0,      1,      -1,     32767,  -32768, 32768,
    -32769, 65535,  65536,  -65536, std.math.maxInt(c_int), std.math.minInt(c_int),
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const a0 = ctx.pick(c_int, &edges);
    const a1 = ctx.pick(c_int, &edges);
    return .{ a0, a1 };
}

pub fn classify(args: Args) usize {
    const lo: c_int = std.math.minInt(i16);
    const hi: c_int = std.math.maxInt(i16);
    const fits0 = args[0] >= lo and args[0] <= hi;
    const fits1 = args[1] >= lo and args[1] <= hi;
    return if (fits0 and fits1) 0 else 1;
}
