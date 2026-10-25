//! zig_diff spec: func_800A06B8 -> func_800A06B8.func_800A06B8 (30DB0.c).
//! Inputs: a random D_800F4538_t in the arena whose shadow radius rates and
//! unk1864 state (unkC, unkE, unk8) are drawn from wrap-around edges (0, 1,
//! 0x7FFF, 0x8000, 0xFFFF), with unkC sometimes equal to the expand rate, and
//! an arg1 that is 0, positive (including 256, whose low byte is 0 and which
//! stores 0 into the u8 fields), or negative (sign bit set, e.g. -1 and INT_MIN).
//! The C makes no calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A06B8));

pub const classes = [_][:0]const u8{ "zero", "negative", "positive" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.D_800F4538_t);
    const rate = [_]u16{ 0, 1, 0x7FFF, 0x8000, 0xFFFF };
    arg0.shadowRadiusExpandRate = ctx.pick(u16, &rate);
    arg0.shadowRadiusShrinkRate = ctx.pick(u16, &rate);
    arg0.unk1864.unkC = ctx.pick(u16, &rate);
    arg0.unk1864.unkE = ctx.pick(u16, &rate);
    arg0.unk1864.unk8 = ctx.pick(u16, &rate);
    // The expand and shrink differences come out exactly 0 sometimes.
    if (ctx.chance(3)) arg0.unk1864.unkC = arg0.shadowRadiusExpandRate;
    if (ctx.chance(3)) arg0.unk1864.unkE = arg0.shadowRadiusShrinkRate;
    const arg1 = ctx.pick(c_int, &.{ 0, 1, -1, 256, 0x7FFF_FFFF, -0x8000_0000 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    if (args[1] == 0) return 0;
    if (args[1] < 0) return 1;
    return 2;
}
