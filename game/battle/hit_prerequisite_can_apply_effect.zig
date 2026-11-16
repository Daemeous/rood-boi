//! Whether a status effect's prerequisites allow it to land on an actor
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: _hitPrerequisiteCanApplyEffect (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 0 when neither the effect's first nor its fourth parameter word matches the target's status bits, else 1.
pub fn hitPrerequisiteCanApplyEffect(debuff: c_int, arg1: [*c]u8) callconv(.c) c_int {
    const params = psx.at(v.g.vs_main_statusEffectParams(), debuff);
    const actors = v.g.vs_battle_actors();
    const actor: *v.vs_battle_actor = actors[arg1.*].?;
    const temp_a1: *v.vs_battle_actor2 = psx.get(actor.stats).?;

    if ((params[0] & temp_a1.statusImmunities) == 0 and (params[3] & temp_a1.statuses) == 0) {
        return 0;
    }
    return 1;
}
