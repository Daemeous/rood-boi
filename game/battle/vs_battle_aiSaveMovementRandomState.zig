//! Copies the scratchpad word at offset 0x3F8 into vs_battle_aiSavedMovementRandomState (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c).
const v = @import("views_6FC00");
const pad = @import("common").pad;

/// orig: vs_battle_aiSaveMovementRandomState (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c)
/// Copies the scratchpad word at offset 0x3F8 into vs_battle_aiSavedMovementRandomState.
pub fn vs_battle_aiSaveMovementRandomState() callconv(.c) void {
    const scratch: *const i32 = @ptrFromInt(pad.addr(0x3F8));
    v.g.vs_battle_aiSavedMovementRandomState().* = scratch.*;
}
