//! Calls vs_battle_showPendingRewardUnlock when the low two bits of D_800F1868 are clear (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_waitForRewardUnlockEffects (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the step routine (vs_battle_showPendingRewardUnlock) when the low two bits of D_800F1868 are clear.
pub fn vs_battle_waitForRewardUnlockEffects() callconv(.c) void {
    if ((v.g.D_800F1868().* & 3) == 0) {
        c.vs_battle_showPendingRewardUnlock();
    }
}
