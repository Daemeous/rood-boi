//! Battle frame step: runs vs_state_resetSceneTimer when the phase byte is 2, then vs_actors_clearRoomEnemyMemory (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80088CAC (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the phase-2 step when vs_state_sceneTimer's phase is 2, then the common step.
pub fn func_80088CAC() callconv(.c) void {
    if (v.g.vs_state_sceneTimer().resetScope == 2) {
        c.vs_state_resetSceneTimer();
    }
    c.vs_actors_clearRoomEnemyMemory();
}
