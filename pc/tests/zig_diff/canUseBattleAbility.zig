//! zig_diff spec: func_80078828 -> ability_window.canUseBattleAbility (146C.c).
//! Inputs: button 0 (any ability mapped?) or 1-5 (4 and 5 index past the
//! three used slots, as the C allows); D_800F19CC an arena block whose
//! current action (unk854[unk0 & 3]) passes or fails the three entry tests,
//! targets nobody (a chain) or an actor 1-15 (a defense), with its first hit
//! on nobody or an actor 1-15 (the same one sometimes), actionIndex at the
//! 40 and 40..53 edges; mapped chain and defense abilities 0 or not; every
//! vs_battle_actors slot an arena actor, unk27 0x80 or not; the character's
//! stats in the arena; the previous input equal to the button or not;
//! battleAbilityInputAttempted 0 or not. _getActionCost's stub returns a
//! value with its top byte set half the time; func_800A0BE0 is a plain stub.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").canUseBattleAbility));

pub const classes = [_][:0]const u8{ "entry-fails", "any-chain", "any-defense", "chain-button", "defense-button",
    "previous-input", "attempted", "action-40-53" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    for (0..16) |slot| {
        const actor = ctx.alloc(v.vs_battle_actor);
        actor.unk27 = if (ctx.chance(3)) 0x80 else ctx.int(u8);
        v.g.vs_battle_actors()[slot] = actor;
    }
    const character = ctx.alloc(v.vs_battle_actor);
    psx.put(&character.stats, ctx.alloc(v.vs_battle_actor2));
    v.g.vs_battle_characterState().* = character;

    const current = &block.unk854[block.unk0 & 3];
    if (!ctx.chance(8)) current.unk4.unk40 = 0;
    if (!ctx.chance(8)) current.unk4A = ctx.pick(u16, &.{ 1, 0xFFFF });
    if (!ctx.chance(8)) current.unk4C[0].unk40 = 0;
    current.unk4.unk0.targetActor = if (ctx.chance(2)) 0 else ctx.range(u8, 1, 15);
    current.unk4C[0].unk0.targetActor = switch (ctx.range(u8, 0, 2)) {
        0 => 0,
        1 => current.unk4.unk0.targetActor,
        else => ctx.range(u8, 1, 15),
    };
    current.actionIndex = ctx.pick(u16, &.{ 0, 39, 40, 41, 53, 54, 0xFFFF, 0x8000 });
    const settings = v.g.vs_main_settings();
    for (0..4) |i| {
        settings.mappedChainAbilities[i] = if (ctx.chance(2)) 0 else ctx.int(u8);
        settings.mappedDefenseAbilities[i] = if (ctx.chance(2)) 0 else ctx.int(u8);
    }
    const button: c_int = if (ctx.chance(3)) 0 else ctx.range(c_int, 1, 5);
    block.previousChainInput = if (ctx.chance(4)) @intCast(button) else ctx.int(u8);
    block.battleAbilityInputAttempted = if (ctx.chance(7)) ctx.int(u8) else 0;
    return .{button};
}

pub fn classify(args: Args) usize {
    const block = v.g.D_800F19CC().*.?;
    const current = &block.unk854[block.unk0 & 3];
    if (current.unk4.unk40 != 0 or current.unk4A == 0 or current.unk4C[0].unk40 != 0) return 0;
    if (block.battleAbilityInputAttempted != 0) return 6;
    if (args[0] != 0 and block.previousChainInput == args[0]) return 5;
    if (current.actionIndex >= 40 and current.actionIndex < 54) return 7;
    const defense = current.unk4.unk0.targetActor != 0;
    if (args[0] == 0) return if (defense) 2 else 1;
    return if (defense) 4 else 3;
}

pub const stubs = struct {
    /// Half the time the top byte (what the C tests) is set.
    pub fn _getActionCost(ctx: *zd.Ctx, _: anytype, _: anytype, _: anytype) c_int {
        const value = ctx.value(u32);
        return @bitCast(if (value & 1 != 0) value | 0x0100_0000 else value & 0x00FF_FFFF);
    }
};
