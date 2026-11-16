//! Battle camera mode check: a state flag at 0xAB selects camera mode 3 or the default path (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: func_8007B7DC (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets camera mode 3 when state flag 0xAB is at least 3, else takes the default path.
pub fn func_8007B7DC() callconv(.c) void {
    if (c.vs_battle_getStateFlag(0xAB) >= 3) {
        c.vs_menu_frameworkOpenChallengeMenu();
        v.g._cameraMode().* = 3;
        c.vs_battle_beginRewardPause();
        return;
    }
    c.vs_battle_openPendingLootRewards();
}
