//! Clears the battle global vs_battle_targetingRiskTimer (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: func_80088CA0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the global vs_battle_targetingRiskTimer to zero.
pub fn func_80088CA0() callconv(.c) void {
    v.g.vs_battle_targetingRiskTimer().* = 0;
}
