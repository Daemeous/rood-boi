//! Sets the loot list node and clears the loot pointer, then calls vs_menu_frameworkOpenGameMenu(12, 2) (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkOpenLootListMenu (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the current loot list node, clears the loot pointer and calls vs_menu_frameworkOpenGameMenu(12, 2).
pub fn vs_menu_frameworkOpenLootListMenu(node: ?*v.vs_battle_lootListNode) callconv(.c) void {
    psx.put(v.g.vs_menu_frameworkLootLists(), node);
    psx.put(v.g.vs_menu_frameworkLootEntry(), null);
    _ = c.vs_menu_frameworkOpenGameMenu(0xC, 2);
}
