//! Validates a battle shortcut selection and reports the menu state to open
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_validateShortcutSelection (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Checks that shortcut's ability is unlocked: plays the invalid sound and returns menu state 0 if not, else invokes it.
pub fn validateShortcutSelection(shortcut: c_int) callconv(.c) c_int {
    var noneUnlocked: c_int = 0;
    var menuState: c_int = 0;

    switch (shortcut) {
        1, 2, 3, 4 => {
            noneUnlocked = @intFromBool(c.vs_battle_isSpellClassUnlocked(shortcut -% 1) == 0);
            menuState = 1;
        },
        5 => {
            menuState = 4;
        },
        6 => {
            noneUnlocked = @intFromBool(c._breakArtsUnlocked() == 0);
            menuState = 2;
        },
        7, 8 => {
            noneUnlocked = @intFromBool(c.vs_battle_chainAbilitiesUnlocked(shortcut -% 7) == 0);
            menuState = 3;
        },
        else => {},
    }

    if (noneUnlocked != 0) {
        c.vs_battle_playInvalidSfx();
        menuState = 0;
        v.g.vs_menu_frameworkActionSelection().unk2 = 0;
        v.g.vs_menu_frameworkActionSelection().unk0 = 2;
    } else {
        menuState = c.vs_menu_frameworkOpenGameMenu(menuState, 0);
        v.g.vs_battle_shortcutInvoked().* = psx.conv(u8, shortcut);
        v.g.D_800F4FDB().* = 1;
    }

    return menuState;
}
