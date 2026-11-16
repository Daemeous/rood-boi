//! Clears the camera mode global (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_battle_resetActionPhase (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the camera mode to 0.
pub fn vs_battle_resetActionPhase() callconv(.c) void {
    v.g._cameraMode().* = 0;
}
