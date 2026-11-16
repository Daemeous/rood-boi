//! Battle unit 146C: a pass-through to vs_battle_updateActorVitals (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_battle_updateRoomActorVitals (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the result of vs_battle_updateActorVitals.
pub fn vs_battle_updateRoomActorVitals() callconv(.c) c_int {
    return c.vs_battle_updateActorVitals();
}
