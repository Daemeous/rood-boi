//! zig_diff spec: func_800E45D4 -> pc.scratch_random.scratchRandom (D2: pc/src/battle_movement.c).
//! Inputs: the scratchpad seed word (0x3F8: its low halfword is read signed)
//! and increment word (0x3FC), random with the halfword's sign both ways, and
//! a range n (powers of two, a non-power, 0, 1 and any int, which gives the
//! mask n - 1). The seed word is stored back whole.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").scratchRandom));

pub const classes = [_][:0]const u8{ "seed-positive", "seed-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const seed: *u32 = @ptrFromInt(0x1F8003F8);
    const increment: *u32 = @ptrFromInt(0x1F8003FC);
    seed.* = ctx.pick(u32, &.{ 0, 0x7FFF, 0x8000, 0xFFFF, 0xFFFF8000, 0x12345678 });
    increment.* = ctx.pick(u32, &.{ 0, 1, 0xFFFFFFFF, 0x8000, 0x7FFFFFFF });
    return .{ctx.pick(c_int, &.{ 1, 2, 4, 0x80, 3, 0, -1, 0x7FFFFFFF, -0x80000000 })};
}

pub fn classify(args: Args) usize {
    _ = args;
    return (@as(*const u32, @ptrFromInt(0x1F8003F8)).* >> 15) & 1;
}
