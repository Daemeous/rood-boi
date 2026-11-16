//! One step of a da4b state: runs one of two helpers by its flagE, then returns 1 (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: vs_battle_aiUpdatePatrolCombatBehavior (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Runs one step of a da4b state (flagE selects the helper) and always returns 1.
pub fn vs_battle_aiUpdatePatrolCombatBehavior(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    const s = state.?;
    if (s.flagE != 0) {
        if (c.vs_battle_aiFleeVisibleEnemies(@ptrCast(s), 1) == 0)
            c.vs_battle_aiWanderAroundSavedCenter(s);
        return 1;
    }
    if (c.vs_battle_aiUpdateCombatPursuit(@ptrCast(s)) == 0)
        c.vs_battle_aiPatrolWaypoints(s);
    return 1;
}
