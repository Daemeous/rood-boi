//! Sets the lower screen UI state to 1 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: vs_battle_showPuzzleTimer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Marks the lower battle screen UI state as 1.
pub fn vs_battle_showPuzzleTimer() callconv(.c) void {
    v.g.vs_battle_lowerScreenUiState().* = 1;
}
