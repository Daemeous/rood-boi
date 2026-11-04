//! zig_diff spec: func_800E6764 -> hold_actor.holdActor (D2: pc/src/battle_movement.c).
//! Inputs: the AI entity table D_800F5878 with each of its 17 slots empty a
//! fourth of the time, else a random-filled block (byte 0x1A random), and an
//! actor id 0..16. func_800E678C is a recording stub whose call sees the
//! state before the hold flag is set.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").holdActor));

pub const classes = [_][:0]const u8{ "empty-slot", "held" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(4)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x40, 16);
            for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
            table[i] = @ptrCast(block);
        }
    }
    return .{ctx.range(c_int, 0, 16)};
}

pub fn classify(args: Args) usize {
    return if (v.g.D_800F5878()[@intCast(args[0])] == null) 0 else 1;
}
