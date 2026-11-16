//! The battle's post-action step: loot pickup or the battle-end path (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_openPendingEnemyLoot (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Once state flag 0xAB exceeds 2, starts the loot pickup if loot is waiting, else runs the other tail.
pub fn vs_items_openPendingEnemyLoot() callconv(.c) void {
    if (c.vs_battle_getStateFlag(0xAB) > 2) {
        if (v.g._lootListHead().* != null) {
            c.vs_menu_frameworkOpenLootListMenu(@ptrCast(v.g._lootListHead().*));
            v.g.D_800F196C().* = 2;
            v.g._cameraMode().* = 12;
            c.vs_field_pauseFieldForMenu();
        } else {
            c.vs_battle_beginPendingAbilityReward();
        }
        return;
    }
    c.func_8006F5FC();
}
