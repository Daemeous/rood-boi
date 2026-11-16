//! Battle frame dispatch on state flag 0xAB: loot list, wind-down or the other path (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_openPendingLootRewards (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the loot list's head when state flag 0xAB is 3 or more, else the other path.
pub fn vs_battle_openPendingLootRewards() callconv(.c) void {
    if (@as(c_int, c.vs_battle_getStateFlag(0xAB)) >= 3) {
        const head = v.g._lootListHead().*;
        if (head != null) {
            c.vs_menu_frameworkOpenLootListMenu(@ptrCast(head));
            v.g._cameraMode().* = 4;
            c.vs_battle_beginRewardPause();
            return;
        }
        c.func_8007B508();
        return;
    }
    c.vs_battle_resumeAfterRewards();
}
