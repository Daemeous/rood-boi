//! Battle unit 146C: runs vs_battle_grantPendingAbilityUnlock when the low two bits of D_800F1868 are clear
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_updateAbilityUnlockDelay (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_battle_grantPendingAbilityUnlock when the low two bits of D_800F1868 are both clear.
pub fn vs_battle_updateAbilityUnlockDelay() callconv(.c) void {
    if ((v.g.D_800F1868().* & 3) == 0) {
        c.vs_battle_grantPendingAbilityUnlock();
    }
}
