//! zig_diff spec: func_800DC284 -> pc.weighted_sum.weightedSum (D2: pc/src/battle_gte.c).
//! Inputs: four ints, small, short-range, or any int (b, c and d take part as
//! signed halves of words, a as a full int times 6, so it wraps). CTC2, MTC2, MVMVA
//! and MFC2 are recording stubs: every register word is compared, and the MFC2
//! value (drawn) must be added to a * 6.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").weightedSum));

pub const classes = [_][:0]const u8{ "short", "wide" };

fn component(ctx: *zd.Ctx, wide: bool) c_int {
    if (wide) return ctx.pick(c_int, &.{ 0x8000, -0x8001, 0x10000, 0x7fffffff, -0x80000000, 0x2aaaaaab });
    return ctx.pick(c_int, &.{ 0, 1, -1, 0x7fff, -0x8000, 100 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const wide = ctx.chance(2);
    return .{ component(ctx, wide), component(ctx, wide), component(ctx, wide), component(ctx, wide) };
}

pub fn classify(args: Args) usize {
    inline for (args) |a| if (a < -0x8000 or a > 0x7fff) return 1;
    return 0;
}
