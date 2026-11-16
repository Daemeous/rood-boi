//! Sets the battle camera mode (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_battle_finishBattleAbilitySequence (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the camera mode to 9.
pub fn vs_battle_finishBattleAbilitySequence() callconv(.c) void {
    v.g._cameraMode().* = 9;
}
