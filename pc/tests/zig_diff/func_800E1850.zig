//! zig_diff spec: func_800E1850 -> func_800E1850.func_800E1850 (771A8.c).
//! Inputs: any arg0 (one time in 8 zero, so no bit is tested), any arg1 (its
//! bits above the low 16 are ignored by the C), and arg2 from -8..7 half the
//! time so that the callee's result equals it often, with the int extremes
//! otherwise. The scratchpad at 0x1F8003EC is random, so each dx and dz
//! bitfield is random in -2..1. func_800E42EC is a recording stub that
//! returns a small signed value (-8..7), so both sides of `> arg2` and the
//! equal case occur.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E1850));

pub const classes = [_][:0]const u8{ "arg0-zero", "arg2-negative", "arg2-nonnegative" };

pub const stubs = struct {
    pub fn func_800E42EC(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = a;
        _ = b;
        return @as(c_int, ctx.value(i4));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: u8 = if (ctx.chance(8)) 0 else ctx.int(u8);
    const arg1: c_uint = ctx.int(c_uint);
    const arg2: c_int = if (ctx.chance(2))
        ctx.range(c_int, -8, 7)
    else
        ctx.pick(c_int, &.{ -1, 0, 1, std.math.minInt(c_int), std.math.maxInt(c_int) });
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    return if (args[2] < 0) 1 else 2;
}
