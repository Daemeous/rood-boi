//! Battle flag setup and a call into vs_battle_openPendingLootRewards (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");
const v = @import("views_146C");

/// orig: vs_battle_beginPostActionRewards (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears D_800F1860, sets D_800F1864, then calls vs_battle_openPendingLootRewards.
pub fn vs_battle_beginPostActionRewards() callconv(.c) void {
    v.g.D_800F1860().* = 0;
    v.g.D_800F1864().* = 1;
    c.vs_battle_openPendingLootRewards();
}
