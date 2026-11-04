//! zig_diff spec: func_800E4BB8 -> movement_flags.rebuildMovementFlags (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes (the flag word at 0x190 is
//! random) whose byte 0x23 is zero, or nonzero with and without the sign bit
//! (the C tests it as a signed char).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").rebuildMovementFlags));

pub const classes = [_][:0]const u8{ "byte-zero", "byte-set" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x240, 16);
    for (state[0..0x240]) |*byte| byte.* = ctx.int(u8);
    state[0x23] = ctx.edge(u8, &.{ 0, 0, 1, 0x7f, 0x80, 0xff });
    return .{state};
}

pub fn classify(args: Args) usize {
    return @intFromBool(args[0][0x23] != 0);
}
