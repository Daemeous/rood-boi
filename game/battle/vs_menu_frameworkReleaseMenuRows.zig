//! Frees the battle menu item list and clears its pointer (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkReleaseMenuRows (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Releases the battle menu items, if any, after the menu call for 6, and clears the pointer.
pub fn vs_menu_frameworkReleaseMenuRows() callconv(.c) void {
    if (v.g.vs_battle_menuItems().* != null) {
        _ = c.func_8007E1C0(6);
        c.vs_main_freeHeapR(v.g.vs_battle_menuItems().*);
        v.g.vs_battle_menuItems().* = null;
    }
}
