//! Loot handoff: stores the loot pointer and clears the loot list head (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkOpenLootEntryMenu (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the current loot pointer, clears the loot list head and runs vs_menu_frameworkOpenGameMenu(12, 2).
pub fn vs_menu_frameworkOpenLootEntryMenu(loot: ?*v.vs_battle_loot) callconv(.c) void {
    v.g.vs_menu_frameworkLootEntry().* = loot;
    v.g.vs_menu_frameworkLootLists().* = null;
    _ = c.vs_menu_frameworkOpenGameMenu(0xC, 2);
}
