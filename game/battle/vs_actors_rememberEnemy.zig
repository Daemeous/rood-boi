//! Copies a battle actor's stats, status and position into its status entry (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_rememberEnemy (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills the status entry of actor `arg0` from its live stats, with `arg1` as its flag byte.
pub fn vs_actors_rememberEnemy(arg0: c_int, arg1: c_int) callconv(.c) void {
    var sp10: v.func_8006EBF8_t = undefined;
    const actor = psx.at(v.g.vs_battle_actors(), arg0).* orelse return;
    if (actor.unk27 == 0x80) return;

    const stats = psx.get(actor.stats).?;
    const raw: *align(4) anyopaque = @alignCast(c.vs_actors_acquireRememberedEnemy(actor.unk27).?);
    const entry: *v.func_80069EF0_t = @ptrCast(raw);
    entry.unk0 = psx.conv(u8, @as(c_int, actor.unk27) +% 1);
    entry.currentHP = psx.conv(i16, @as(c_int, stats.currentHP));
    entry.maxHP = psx.conv(i16, @as(c_int, stats.maxHP));
    entry.currentMP = psx.conv(i16, @as(c_int, stats.currentMP));
    entry.maxMP = psx.conv(i16, @as(c_int, stats.maxMP));
    entry.totalStrength = psx.conv(i16, @as(c_int, stats.totalStrength));
    entry.totalIntelligence = psx.conv(i16, @as(c_int, stats.totalIntelligence));
    entry.totalAgility = psx.conv(i16, @as(c_int, stats.totalAgility));

    for (0..6) |i| {
        entry.limbHp[i] = stats.limbs[i].hp;
    }

    entry.unk20 = stats.statuses;

    if ((psx.conv(c_uint, c.vs_actors_actorStateFlags(arg0)) & 0x0100_0000) != 0) {
        entry.unk1 = psx.conv(u8, arg1);
        entry.currentHP = 1;
    } else {
        entry.unk1 = psx.conv(u8, arg1);
    }

    entry.unk2 = c.vs_battle_aiPackActorAiPersistenceFlags(arg0);

    _ = c.vs_actors_actorPlacement(arg0, &sp10);
    c.vs_actors_actorSavedPlacement(arg0, psx.conv(c_int, @as(c_int, stats.unk30)), @ptrCast(&sp10));

    entry.unk24 = sp10.unk0.unk0.value;
}
