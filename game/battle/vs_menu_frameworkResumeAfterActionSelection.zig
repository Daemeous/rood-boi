//! Leaves the ability menu after an action (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkResumeAfterActionSelection (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Back to the menu it came from (the item shortcut's own menu for shortcut 5; ROOD_PC: the top level when a modern-menu cast was cancelled), and the menu sound unless muted.
pub fn vs_menu_frameworkResumeAfterActionSelection() callconv(.c) void {
    const CastCancelled = *const fn () callconv(.c) c_int;
    v.g.vs_menu_frameworkActionSelection().unk2 = 0;
    v.g.vs_menu_frameworkActionSelection().unk0 = 1;
    const menu = v.g.vs_battle_menuState();
    if (v.g.vs_battle_shortcutInvoked().* == 5) {
        menu.currentMenu = 4;
        c.vs_mainMenu_miscItemsShortcutMenu(2);
    } else if (if (psx.weak(CastCancelled, "pc_ui_menu_cast_cancelled")) |cancelled| cancelled() != 0 else false) {
        // ROOD_PC: no original screen to return to (5BF94.c).
        menu.currentMenu = 0;
        _ = c.vs_menu_frameworkUpdateMainMenuSelection(1);
    } else {
        menu.currentMenu = menu.returnState;
        v.g.vs_battle_submenuStates()[menu.returnState & 0xF] = menu.executeAbilityType;
    }
    if (v.g.D_800F4FDB().* == 0) {
        _ = c.vs_render_reserveTexturePages(0x1A, 3, 6);
    }
}
