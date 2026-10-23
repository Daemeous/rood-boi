//! zig_diff spec: func_80096254 -> func_80096254.func_80096254 (2842C.c).
//! Inputs: two random c_int arguments, drawn half the time from edges (0, +-1,
//! 0x7FFF, 0x8000, 0xFFFF, 0x10000, the int extremes), so the low halves carry
//! into the high half, the high halves go negative, and both halves wrap.
//! The C has no branches, globals or callees; the classes name which carry
//! case an input takes.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
const c = @import("c_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80096254));

pub const classes = [_][:0]const u8{ "low-carry", "high-negative", "plain" };

const edges = [_]c_int{
    0,      1,          -1,     0x7FFF,
    0x8000, 0xFFFF,     0x10000, -0x8000,
    std.math.maxInt(c_int), std.math.minInt(c_int),
};

pub fn prepare(ctx: *zd.Ctx) Args {
    return .{ ctx.pick(c_int, &edges), ctx.pick(c_int, &edges) };
}

pub fn classify(args: Args) usize {
    const a: u32 = @bitCast(args[0]);
    const b: u32 = @bitCast(args[1]);
    if ((a & 0xFFFF) + (b & 0xFFFF) > 0xFFFF) return 0;
    if ((args[0] >> 16) + (args[1] >> 16) < 0) return 1;
    return 2;
}
