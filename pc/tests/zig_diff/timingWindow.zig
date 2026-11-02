//! zig_diff spec: func_80078AB4 -> timing_window.timingWindow (146C.c).
//! Inputs: the link (unk0: 0..6, 30, 33 and huge, so the margin clamps at 2)
//! and its slot: the player's action or an enemy's (target 0 or not), index
//! 53..55, unk40 0 or not, 0..3 hits whose first targets actor 0 or 1 with
//! unk40 0 or not; the window 0 or not and the timer within 30 of it (every
//! grade and the cue's equality); the tick speed 1..4 (the C divides by it);
//! the pressed buttons (none, each ability button, several, others); the
//! mapped chain and defense abilities, the input state (attempted, the last
//! chain button, unk2C02's mask), the streak at 0xFFFE/0xFFFF or below; the
//! first hit's target dead or not; the cast flash bits. Stubs: the state flag,
//! func_80078828 and func_8009E4B0 return 0 or 1; func_80078828 sometimes
//! aims the action or clears the mappings (the C tests both after it).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").timingWindow));

pub const classes = [_][:0]const u8{ "no-button", "player", "enemy" };

pub const stubs = struct {
    pub fn vs_battle_getStateFlag(ctx: *zd.Ctx, id: anytype) u8 {
        _ = id;
        return ctx.edge(u8, &.{ 0, 1 });
    }
    /// Judges the input; now and then it also aims the action (unk40) or clears the mappings, as a callee could:
    /// the C tests both again after it.
    pub fn func_80078828(ctx: *zd.Ctx, button: anytype) c_int {
        _ = button;
        const state = v.g.D_800F19CC().*.?;
        switch (ctx.value(u8) % 16) {
            0 => state.unk854[state.unk0 & 3].unk4.unk40 = 1,
            1 => {
                v.g.vs_main_settings().mappedChainAbilities = .{ 0, 0, 0, 0 };
                v.g.vs_main_settings().mappedDefenseAbilities = .{ 0, 0, 0, 0 };
            },
            else => {},
        }
        return ctx.edge(c_int, &.{ 0, 1, 1, 1 });
    }
    pub fn func_8009E4B0(ctx: *zd.Ctx, actor: anytype) c_uint {
        _ = actor;
        return ctx.edge(c_uint, &.{ 0, 0, 0, 1 });
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = state;
    state.unk0 = ctx.edge(u32, &.{ 0, 1, 2, 3, 4, 5, 6, 30, 33, 0xFFFF_FFFF, 0x8000_0000 });
    const slot = &state.unk854[state.unk0 & 3];
    slot.unk4.unk0.targetActor = if (ctx.chance(2)) 0 else ctx.range(u8, 1, 15);
    slot.actionIndex = ctx.edge(u16, &.{ 53, 54, 55 });
    slot.unk4.unk40 = if (ctx.chance(5)) 1 else 0;
    slot.unk4A = ctx.range(u16, 0, 3);
    slot.unk4C[0].unk0.targetActor = ctx.range(u8, 0, 1);
    slot.unk4C[0].unk40 = if (ctx.chance(4)) 1 else 0;
    for (0..2) |k| {
        const actor = ctx.alloc(v.vs_battle_actor);
        const stats = ctx.alloc(v.vs_battle_actor2);
        stats.currentHP = if (ctx.chance(4)) 0 else ctx.int(i16);
        @import("common").psx.put(&actor.stats, stats);
        v.g.vs_battle_actors()[k] = actor;
    }
    const tick = ctx.range(i32, 1, 4);
    v.g.vs_gametime_tickspeed().* = tick;
    state.battleAbilityTargetWindow = if (ctx.chance(5)) 0 else ctx.edge(u16, &.{ 20, 30, 40 });
    const timer = @as(i32, state.battleAbilityTargetWindow) + ctx.range(i32, -30, 30);
    state.battleAbilityTimer = if (timer < 0) 0 else @intCast(timer);
    v.g.vs_main_buttonsPressed().all = ctx.edge(u32, &.{ 0, 0x10, 0x20, 0x80, 0x30, 0x90, 0xB0, 0x40, 0x10 | 0x1000, ctx.int(u32) });
    const settings = v.g.vs_main_settings();
    for (0..4) |k| {
        settings.mappedChainAbilities[k] = if (ctx.chance(4)) 0 else ctx.range(u8, 1, 40);
        settings.mappedDefenseAbilities[k] = if (ctx.chance(4)) 0 else ctx.range(u8, 1, 40);
    }
    state.battleAbilityInputAttempted = if (ctx.chance(3)) 1 else 0;
    state.battleAbilityInputSuccessful = ctx.range(u8, 0, 2);
    state.previousChainInput = ctx.range(u8, 0, 3);
    state.unk2C03 = ctx.range(u8, 0, 3);
    state.unk2C02 = ctx.edge(u8, &.{ 0, 0x10, 0x20, 0x80, 0xB0, ctx.int(u8) });
    v.g.scoreData().chainStreak = ctx.pick(u16, &.{ 0xFFFD, 0xFFFE, 0xFFFF });
    v.g.D_800F1868().* = ctx.pick(i32, &.{ 0, 1, 2, 3, 4 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const state = v.g.D_800F19CC().*.?;
    if (v.g.vs_main_buttonsPressed().all & 0xB0 == 0) return 0;
    return if (state.unk854[state.unk0 & 3].unk4.unk0.targetActor == 0) 1 else 2;
}
