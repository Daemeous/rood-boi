//! zig_diff spec: func_800BAE74 -> func_800BAE74.func_800BAE74 (4A0A8.c).
//! Inputs: arg0 points into a random arena block at a random offset 0-8, so
//! arg0 + 1 is always inside it; arg1 is a short drawn from edges (0, -1, 1,
//! the extremes) and random values, though the C never reads it. The C has no
//! branches. It calls vs_battle_getShort(arg0 + 1) and passes the returned
//! short to vs_battle_decreaseMiscCount. Both are recording stubs that return
//! any value, so the sign and high bits of the short reach the callee, and the
//! value the C ignores is still compared. The C returns 0.

const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAE74));

pub const classes = [_][:0]const u8{ "arg1-zero", "arg1-negative", "arg1-positive" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn vs_battle_decreaseMiscCount(ctx: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(16, 1);
    const off = ctx.range(usize, 0, 8);
    return .{ @ptrCast(block + off), ctx.pick(c_short, &.{ 0, -1, 1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    if (args[1] == 0) return 0;
    return if (args[1] < 0) 1 else 2;
}
