//! zig_diff spec: func_80076D50 -> func_80076D50.func_80076D50 (146C.c).
//! Inputs: a slot 0..15 holding an arena actor with arena stats (random
//! bytes, so every store shows); arg1 41, 48 or another (and past 16 bits);
//! arg2, arg3 past a byte; arg4 with each low pair and random high bits.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80076D50));

pub const classes = [_][:0]const u8{ "player", "slot-1", "drawn", "ally", "enemy" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.edge(c_uint, &.{ 0, 1, 2, 5, 15 });
    const actor = ctx.alloc(v.vs_battle_actor);
    actor.stats.raw = psx.addr(ctx.alloc(v.vs_battle_actor2));
    v.g.vs_battle_actors()[id] = actor;
    return .{ id, ctx.pick(c_int, &.{ 41, 48, 40, 49, 0x10029 }), ctx.pick(c_int, &.{ 0x1FF, 1 }), ctx.pick(c_int, &.{ 0x1FF, 1 }), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    if (args[0] == 0) return 0;
    if (args[0] == 1) return 1;
    return switch (args[4] & 3) {
        1 => 2,
        0 => 3,
        else => 4,
    };
}
