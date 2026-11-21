//! Copies the movement frame's random state word (0x3F8) into vs_battle_aiSavedMovementRandomState (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c).
const v = @import("views_6FC00");
const pad = @import("common").pad;

/// orig: vs_battle_aiSaveMovementRandomState (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c)
/// Copies the movement frame's random state word (0x3F8) into vs_battle_aiSavedMovementRandomState.
pub fn vs_battle_aiSaveMovementRandomState() callconv(.c) void {
    v.g.vs_battle_aiSavedMovementRandomState().* = @bitCast(pad.move.randomState);
}
