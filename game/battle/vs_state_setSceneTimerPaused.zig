//! Swaps the two bytes of the vs_state_sceneTimer pair: a nonzero `arg0` moves unk0 into unk1 and clears unk0, zero moves unk1 back into unk0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_state_setSceneTimerPaused (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Exchanges the vs_state_sceneTimer byte pair in the direction `arg0` selects, then calls the matching hook.
pub fn vs_state_setSceneTimerPaused(arg0: c_int) callconv(.c) void {
    const g = v.g.vs_state_sceneTimer();
    if (arg0 != 0) {
        const temp_v1: c_int = g.mode;
        g.mode = 0;
        g.pausedMode = psx.conv(u8, temp_v1);
        c.vs_battle_hidePuzzleTimer();
        return;
    }
    g.mode = g.pausedMode;
    c.vs_battle_showPuzzleTimer();
}
