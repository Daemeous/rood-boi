//! zig_diff spec: func_800DC48C -> pc.actor_target_flags.actionTargetBit14 (D2: pc/src/battle_helpers.c, through its static action_target_bit).
//! Inputs: the actor table vs_battle_actors (17 slots, each an actor record
//! with a guest pointer at 0x3C to a stats block whose byte 0x37 is random
//! or has class 2 in its low 3 bits), the action table vs_main_actions with
//! the target-flags halfword of the chosen action random, an edge or 0xFFFF,
//! and an action that is 0x60 half the time (the special case against class
//! 2 returns the whole shifted halfword xor 1, not just the bit) or any
//! index of the 256-entry table.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_helpers");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").actionTargetBit14));

pub const classes = [_][:0]const u8{ "plain-action", "action-60-other-class", "action-60-class-2" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actors = v.g.vs_battle_actors();
    for (0..17) |i| {
        const record = ctx.allocBytes(0x80, 16);
        for (record[0..0x80]) |*byte| byte.* = ctx.int(u8);
        const stats = ctx.allocBytes(0x40, 16);
        for (stats[0..0x40]) |*byte| byte.* = ctx.int(u8);
        if (ctx.chance(2)) stats[0x37] = (stats[0x37] & 0xf8) | 2;
        std.mem.writeInt(u32, record[0x3c..][0..4], @intCast(@intFromPtr(stats)), .little);
        actors[i] = @ptrCast(record);
    }
    const action: c_int = if (ctx.chance(2)) 0x60 else ctx.range(c_int, 0, 255);
    const flags = v.g.actions() + @as(usize, @intCast(action)) * 0x34 + 0xe;
    const value = ctx.pick(u16, &.{ 0, 0xFFFF, 0x8000, 0x4000, 0xC000, 0x3FFF, 0x4001, 0x8001 });
    std.mem.writeInt(u16, flags[0..2], value, .little);
    return .{ action, ctx.range(c_int, 0, 16) };
}

pub fn classify(args: Args) usize {
    if (args[0] != 0x60) return 0;
    const record: [*]const u8 = @ptrCast(v.g.vs_battle_actors()[@intCast(args[1])].?);
    const stats: [*]const u8 = @ptrFromInt(std.mem.readInt(u32, record[0x3c..][0..4], .little));
    return if (stats[0x37] & 7 == 2) 2 else 1;
}
