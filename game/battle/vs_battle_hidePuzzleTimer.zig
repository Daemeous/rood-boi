//! Clears the lower-screen UI state (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_battle_hidePuzzleTimer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears the lower-screen UI state.
pub fn vs_battle_hidePuzzleTimer() callconv(.c) void {
    v.g.vs_battle_lowerScreenUiState().* = 0;
}
