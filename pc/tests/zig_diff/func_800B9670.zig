//! zig_diff spec: func_800B9670 -> func_800B9670.func_800B9670 (4A0A8.c).
//! Inputs: arg0 points at a 16-byte arena block of random bytes, so arg0[1..3]
//! are in bounds and arg0[3] (the byte the C reads and passes on) is random;
//! arg1 is an edge value the C never reads, classed by sign and zero so every
//! class occurs. The unit has no branches. The three callees
//! (vs_battle_getShort, func_800BFE50, func_8009D7E8) are left to the harness's
//! default recording stubs: their results come from ctx.value, and none of them
//! can change state the C reads back, since the C reads nothing after the calls.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B9670));

pub const classes = [_][:0]const u8{ "arg1-zero", "arg1-negative", "arg1-positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = ctx.allocBytes(16, 16);
    const a1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ p, a1 };
}

pub fn classify(args: Args) usize {
    const a1 = args[1];
    if (a1 == 0) return 0;
    if (a1 < 0) return 1;
    return 2;
}
