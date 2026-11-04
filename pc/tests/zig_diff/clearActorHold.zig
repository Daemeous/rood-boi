//! zig_diff spec: func_800E6B24 -> clear_actor_hold.clearActorHold (D2: pc/src/battle_movement.c).
//! Inputs: the AI entity table D_800F5878 with each of its 17 slots empty a
//! fourth of the time, else a random-filled block (byte 0x1A random); an id
//! whose low byte is 0..16 and whose upper 24 bits are random half the time
//! (the C masks the id with 0xFF).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").clearActorHold));

pub const classes = [_][:0]const u8{ "empty-slot", "cleared" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(4)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x40, 16);
            for (block[0..0x40]) |*byte| byte.* = ctx.int(u8);
            block[0x1a] = ctx.edge(u8, &.{ 0, 1, 0x80, 0xff });
            table[i] = @ptrCast(block);
        }
    }
    const low = ctx.range(c_int, 0, 16);
    const high: c_int = if (ctx.chance(2)) ctx.int(c_int) & ~@as(c_int, 0xff) else 0;
    return .{high | low};
}

pub fn classify(args: Args) usize {
    return if (v.g.D_800F5878()[@intCast(args[0] & 0xff)] == null) 0 else 1;
}
