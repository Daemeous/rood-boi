//! zig_diff spec: func_800E6694 -> pc.actor_disposition.dispositionWord (D2: pc/src/battle_helpers.c).
//! Inputs: the AI entity table D_800F5878 with each of its 17 slots empty
//! (null) a fifth of the time, else a random-filled block; an actor id 0..16;
//! and for the entity the byte at 0x12D zero or nonzero (the test of bit 5).
//! The word at 0x1A4 and the byte at 0x13 are random.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").dispositionWord));

pub const classes = [_][:0]const u8{ "empty-slot", "byte-zero", "byte-nonzero" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F5878();
    for (0..17) |i| {
        if (ctx.chance(5)) {
            table[i] = null;
        } else {
            const block = ctx.allocBytes(0x200, 16);
            for (block[0..0x200]) |*byte| byte.* = ctx.int(u8);
            if (ctx.chance(2)) block[0x12d] = 0;
            if (block[0x12d] != 0 and ctx.chance(3)) block[0x12d] = 1;
            table[i] = @ptrCast(block);
        }
    }
    return .{ctx.range(c_int, 0, 16)};
}

pub fn classify(args: Args) usize {
    const entity: [*]const u8 = @ptrCast(v.g.D_800F5878()[@intCast(args[0])] orelse return 0);
    return if (entity[0x12d] == 0) 1 else 2;
}
