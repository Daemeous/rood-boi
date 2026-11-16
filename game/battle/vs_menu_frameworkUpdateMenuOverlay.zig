//! Battle menu frame: opens the misc-items menu, runs the active menu and renders its rows (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_menu_frameworkUpdateMenuOverlay (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs the current battle menu and renders its ten item rows when they are shown.
pub fn vs_menu_frameworkUpdateMenuOverlay() callconv(.c) void {
    var s2: [*c]v.vs_battle_menuItem_t = v.g.vs_battle_menuItems().*;
    var s0: c_int = v.g.vs_battle_menuState().currentMenu;

    if (s0 == 0x7F) {
        v.g.vs_menu_frameworkActionSelection().unk0 = 2;
        v.g.vs_battle_menuState().currentMenu = 0x3F;
    }

    // s0 is not updated by the 0x7F case: its low bits still select the 0x3F page.
    const temp_s1: c_int = s0 & 0x3F;

    if (temp_s1 != 0x3F) {
        if (c.vs_battle_loadMenuPrg(0) != 0) {
            if (v.g.vs_battle_shortcutInvoked().* == 5) {
                c.vs_mainMenu_miscItemsShortcutMenu(0);
                return;
            }

            c.vs_mainMenu_exec(s0);
            return;
        }

        if (temp_s1 == 0) {
            const miscIndex: c_int = c.vs_menu_frameworkUpdateMainMenuSelection(0);

            if (miscIndex != 0) {
                s0 = 0x1F;

                if (miscIndex > 0) {
                    s0 = miscIndex | 0x40;
                }

                v.g.vs_battle_menuState().currentMenu = psx.conv(u8, s0);
            }
        }

        if ((s0 >= 0x1F or temp_s1 == 0) and c.pc_ui_menu_hide_rows() == 0) {
            var i: c_int = 0;
            while (i < 10) : (i += 1) {
                c.vs_battle_renderMenuItem(@ptrCast(s2));
                s2 += 1;
            }
        }
    }
}
