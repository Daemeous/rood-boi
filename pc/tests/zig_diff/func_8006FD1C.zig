//! zig_diff spec: func_8006FD1C -> func_8006FD1C.func_8006FD1C (146C.c).
//! Inputs: D_800F19CC an arena block; the chain link count (unk0) below,
//! at and past 999; the current link's action at the ability ranges'
//! edges; the timing input 0, 1, 2 or 3; the first hit's target alive or
//! not; the hit list 0..5 hits on actors 0..3 (the player's own with and
//! without an hp or mp effect); the mapped ability slot 1..4; the chain
//! counter flag around 10; the battle-ability counters around their
//! requirement; the max chain around the link count. Actors 0..3 are
//! arena actors with arena stats; func_800A1108's stub writes the
//! attacker's position.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8006FD1C));

pub const classes = [_][:0]const u8{ "over", "chain", "chain-dead", "defense", "none" };

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        return ctx.edge(u8, &.{ 0, 9, 10, 11, 0xFF });
    }
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const actor: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        actor.unk0.unk4.vx = ctx.value(i16);
        actor.unk0.unk4.vy = ctx.value(i16);
        actor.unk0.unk4.vz = ctx.value(i16);
        return 0;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    for (0..4) |k| {
        const actor = ctx.alloc(v.vs_battle_actor);
        const stats = ctx.alloc(v.vs_battle_actor2);
        stats.currentHP = if (ctx.chance(4)) 0 else ctx.pick(i16, &.{ 1, -1, 100 });
        actor.stats.raw = psx.addr(stats);
        v.g.vs_battle_actors()[k] = actor;
    }
    block.unk0 = ctx.edge(u32, &.{ 0, 1, 2, 3, 997, 998, 999, 1000 });
    for (&block.unk854) |*link| {
        link.actionIndex = ctx.edge(u16, &.{ 0x8C, 0x8D, 0xB7, 0xB8, 0xDF, 0xE0, 0 });
        link.unk4.unk0.targetActor = ctx.range(u8, 0, 3);
        link.unk4C[0].unk0.targetActor = ctx.range(u8, 0, 3);
    }
    block.battleAbilityInputSuccessful = ctx.edge(u8, &.{ 0, 1, 1, 2, 2, 3 });
    block.unk2C03 = ctx.range(u8, 1, 4);
    block.unk8.unk4A = ctx.range(u16, 0, 5);
    for (0..6) |k| block.unk8.unk4C[k].unk0.targetActor = ctx.range(u8, 0, 3);
    block.unk8.unk4.unk0.targetActor = ctx.range(u8, 0, 3);
    const kills = &v.g.vs_main_artsStatus().kills;
    kills.battleAbilitiesUnlocked = ctx.edge(u16, &.{ 0, 5, 21, 22, 23 });
    const requirement = v.g.vs_main_battleAbilitiesPointsRequirements()[@min(kills.battleAbilitiesUnlocked, 21)];
    kills.total = ctx.edge(u16, &.{ requirement -% 1, requirement, requirement +% 1, 0 });
    v.g.scoreData().maxChain = @truncate(block.unk0 +% ctx.edge(u32, &.{ 0, 1, 2, 0xFFFF_FFFF }));
    return .{};
}

pub fn classify(_: Args) usize {
    const block = v.g.D_800F19CC().*.?;
    if (block.unk0 >= 999) return 0;
    const action = &block.unk854[block.unk0 & 3];
    const target = psx.get(v.g.vs_battle_actors()[action.unk4C[0].unk0.targetActor].?.stats).?;
    return switch (block.battleAbilityInputSuccessful) {
        1 => if (target.currentHP != 0) 1 else 2,
        2 => 3,
        else => 4,
    };
}
