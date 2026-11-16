//! Battle frame step: runs the per-frame updates unless the battle state at
//! vs_menu_frameworkActionSelectionState() is already in state 1 or func_800CB45C() reports nonzero
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updateAbilityUnlockResults (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the frame's update steps when the battle state is not 1 and the check at func_800CB45C is clear.
pub fn vs_battle_updateAbilityUnlockResults() callconv(.c) void {
    c.vs_menu_frameworkUpdateMenuOverlay();
    const ability: *v.D_800F18EC_t = @ptrCast(@alignCast(c.vs_menu_frameworkActionSelectionState().?));
    if (psx.conv(c_int, ability.unk0) != 1 and c.func_800CB45C() == 0) {
        c.vs_field_resumeFieldAfterMenu();
        c.vs_menu_frameworkReleaseMenuRows();
        c.func_800CB50C();
        c.vs_battle_beginPendingAbilityReward();
    }
}
