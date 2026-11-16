//! Battle frame step: runs the 146C.c update chain when the current entity is live and no
//! blocking state is set (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updateRewardUnlockDialog (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the per-frame update chain unless the current entry is marked inactive or the blocking check is set.
pub fn vs_battle_updateRewardUnlockDialog() callconv(.c) void {
    c.vs_menu_frameworkUpdateMenuOverlay();
    // The C dereferences the result unconditionally: a null pointer traps here.
    const entry: *v.D_800F18EC_t = @ptrCast(@alignCast(c.vs_menu_frameworkActionSelectionState().?));
    if (entry.unk0 != 1 and c.func_800CB45C() == 0) {
        c.vs_battle_endRewardPause();
        c.vs_menu_frameworkReleaseMenuRows();
        c.func_800CB50C();
        c.func_8007B508();
    }
}
