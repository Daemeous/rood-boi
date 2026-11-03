//! zig_diff spec: func_800E4358 -> pc.tick_state_timers.tickStateTimers (D2: pc/src/battle_helpers.c).
//! Inputs: a random-filled state block in the arena whose timer bytes at 0x155
//! and 0x13F are each zero (a third of the time), 1 (the last tick) or any
//! byte, so every zero/nonzero combination occurs; the neighbouring bytes stay
//! random, so a decrement of the wrong byte or width shows.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").tickStateTimers));

pub const classes = [_][:0]const u8{ "both-zero", "first-only", "second-only", "both" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.allocBytes(0x200, 16);
    for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
    block[0x155] = ctx.edge(u8, &.{ 0, 0, 1, 2, 0x80, 0xFF });
    block[0x13f] = ctx.edge(u8, &.{ 0, 0, 1, 2, 0x80, 0xFF });
    return .{block};
}

pub fn classify(args: Args) usize {
    return @as(usize, @intFromBool(args[0][0x155] != 0)) | @as(usize, @intFromBool(args[0][0x13f] != 0)) << 1;
}
