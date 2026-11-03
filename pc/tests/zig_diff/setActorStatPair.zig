//! zig_diff spec: func_800E685C -> pc.set_actor_stat_pair.setActorStatPair (D2: pc/src/battle_helpers.c).
//! Inputs: the AI entity table D_800F5878 with each of its 17 slots an empty
//! slot (null) a fifth of the time, else a random-filled block; an actor id
//! 0..16; and two ints (edges or any value) in either order, so the larger
//! is the first, the second, or both equal, and the byte and halfword stores
//! see values beyond a byte.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setActorStatPair));

pub const classes = [_][:0]const u8{ "empty-slot", "first-larger", "second-larger" };

const edges = [_]c_int{ 0, 1, 0xFF, 0x100, 0x1FF, 0x7FFFFFFF, -0x80000000, -1 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(5)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x240, 16);
            for (block[0..0x240]) |*byte| byte.* = ctx.int(u8);
            table[i] = @ptrCast(block);
        }
    }
    const first = ctx.pick(c_int, &edges);
    // A third of the time the second equals the first.
    const second = if (ctx.chance(3)) first else ctx.pick(c_int, &edges);
    return .{ ctx.range(c_int, 0, 16), first, second };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F5878()[@intCast(args[0])] == null) return 0;
    return if (args[1] < args[2]) 2 else 1;
}
