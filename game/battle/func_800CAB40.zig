//! Leaves the ability menu after an action (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CAB40 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Back to the menu it came from (the item shortcut's own menu for shortcut 5; ROOD_PC: the top level when a modern-menu cast was cancelled), and the menu sound unless muted.
pub fn func_800CAB40() callconv(.c) void {
    const CastCancelled = *const fn () callconv(.c) c_int;
    v.g.D_800F4E98().unk2 = 0;
    v.g.D_800F4E98().unk0 = 1;
    const menu = v.g.vs_battle_menuState();
    if (v.g.vs_battle_shortcutInvoked().* == 5) {
        menu.currentMenu = 4;
        c.vs_mainMenu_miscItemsShortcutMenu(2);
    } else if (if (psx.weak(CastCancelled, "pc_ui_menu_cast_cancelled")) |cancelled| cancelled() != 0 else false) {
        // ROOD_PC: no original screen to return to (5BF94.c).
        menu.currentMenu = 0;
        _ = c.func_800C930C(1);
    } else {
        menu.currentMenu = menu.returnState;
        v.g.vs_battle_submenuStates()[menu.returnState & 0xF] = menu.executeAbilityType;
    }
    if (v.g.D_800F4FDB().* == 0) {
        _ = c.func_8007DFF0(0x1A, 3, 6);
    }
}
