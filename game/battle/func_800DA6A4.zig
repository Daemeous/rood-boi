//! Battle action-candidate step: runs the movement candidate when flagged, else
//! tests the two candidate checks and falls back (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA6A4 (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Advances the action candidate state `state`; returns 1 when the candidate was accepted.
pub fn func_800DA6A4(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    const s = state.?;
    if (s.flagE != 0)
        return c.vs_battle_aiFleeVisibleEnemies(@ptrCast(s), 1);
    const target = c.vs_battle_aiFindActorByRelationship(s, null, 3);
    if (target >= 0 and c.vs_battle_aiFindActorByRelationship(s, null, 2) >= 0 and
        c.vs_battle_aiApproachActor(@ptrCast(s), target, 1024, 1) != 0)
        return 1;
    return c.vs_battle_aiUpdateCombatPursuit(@ptrCast(s));
}
