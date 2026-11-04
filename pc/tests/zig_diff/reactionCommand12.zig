//! zig_diff spec: func_800E4B18 -> reaction_command_12.reactionCommand12 (D2: pc/src/battle_movement.c).
//! Inputs: an actor update state of random bytes. The callee func_800DEEFC is a
//! recording stub: its arguments (the state, command 12) and the state hash at
//! the call show the order of the two stores (0x228 cleared, 0x34 copied to 0x170).
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").reactionCommand12));

pub const classes = [_][:0]const u8{"issued"};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x240, 16);
    for (state[0..0x240]) |*byte| byte.* = ctx.int(u8);
    return .{state};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
