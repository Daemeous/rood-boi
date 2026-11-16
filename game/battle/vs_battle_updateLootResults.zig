//! Battle per-frame step: runs three update calls unless the state at vs_menu_frameworkActionSelectionState's unk0 is 1
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updateLootResults (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the frame's first update, then the three follow-up steps unless the current state's first field is 1.
pub fn vs_battle_updateLootResults() callconv(.c) void {
    c.vs_menu_frameworkUpdateMenuOverlay();
    const raw = c.vs_menu_frameworkActionSelectionState().?;
    const state: *v.D_800F18EC_t = @ptrFromInt(@intFromPtr(raw));
    if (state.unk0 != 1) {
        c.vs_field_resumeFieldAfterMenu();
        c.vs_menu_frameworkReleaseMenuRows();
        c.vs_battle_beginPendingAbilityReward();
    }
}
