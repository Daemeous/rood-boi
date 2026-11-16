//! Calls the 146C battle setup steps and runs the rest only while the
//! D_800F18EC state's first byte is not 1 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updateRewardResultsDialog (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the setup step, then the follow-up steps unless the state's unk0 is 1.
pub fn vs_battle_updateRewardResultsDialog() callconv(.c) void {
    c.vs_menu_frameworkUpdateMenuOverlay();
    const state: *align(1) v.D_800F18EC_t = @ptrCast(c.vs_menu_frameworkActionSelectionState().?);
    if (state.unk0 != 1) {
        c.vs_battle_endRewardPause();
        c.vs_menu_frameworkReleaseMenuRows();
        c.vs_battle_openPendingLootRewards();
    }
}
