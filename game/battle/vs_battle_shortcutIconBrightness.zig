//! Battle menu gate: the variant (0x180 or 0x140) of menu entry `arg0`, or 0 when it is locked (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: vs_battle_shortcutIconBrightness (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the menu variant for entry arg0 (0x180, or 0x140 when a flag is set), or 0 when the entry is locked.
pub fn vs_battle_shortcutIconBrightness(arg0: c_int) callconv(.c) c_int {
    const temp_s0: c_int = c.vs_menu_frameworkActionRestrictionFlags(0);
    var var_s2: c_int = 0x180;

    switch (arg0) {
        0...3 => {
            // arg0 is 0..3 here, so arg0 * 4 is 0..12: a valid u5 shift amount.
            const shift: u5 = @intCast(arg0 * 4);
            const spell_class: c_int = (@as(c_int, 0x3021) >> shift) & 0xF;
            if (c.vs_battle_isSpellClassUnlocked(spell_class) == 0) {
                return 0;
            }
            if ((temp_s0 & 0xB7) != 0) {
                var_s2 = 0x140;
            }
        },
        4 => {
            if ((temp_s0 & 0x7) != 0) {
                var_s2 = 0x140;
            }
        },
        5, 6 => {
            if (c.vs_battle_chainAbilitiesUnlocked(6 -% arg0) == 0) {
                return 0;
            }
        },
        7 => {
            if (c._breakArtsUnlocked() == 0) {
                return 0;
            }
            if ((temp_s0 & 0x15F) != 0) {
                var_s2 = 0x140;
            }
        },
        else => {},
    }

    return var_s2;
}
