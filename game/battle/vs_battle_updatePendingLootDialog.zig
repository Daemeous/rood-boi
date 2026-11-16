//! Battle frame tail: runs the per-frame battle updates unless the battle state is 1
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updatePendingLootDialog (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the battle frame tail: the wait update, then the flagged updates, unless the battle state is 1.
pub fn vs_battle_updatePendingLootDialog() callconv(.c) void {
    c.vs_menu_frameworkUpdateMenuOverlay();
    const ability: *v.D_800F18EC_t = @alignCast(@ptrCast(c.vs_menu_frameworkActionSelectionState().?));
    if (@as(c_int, ability.unk0) != 1) {
        if (v.g.D_800F1864().* != 0) {
            c.vs_battle_endRewardPause();
        }
        c.vs_menu_frameworkReleaseMenuRows();
        if (v.g.D_800F1860().* != 0) {
            c.vs_battle_resumeAfterRewards();
        } else {
            c.func_8007B508();
        }
    }
}
