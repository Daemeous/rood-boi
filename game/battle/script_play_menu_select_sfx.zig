//! The battle script menu-select sound effect (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_playMenuSelectSfx (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Plays the menu select sound effect for a battle script opcode and returns 0.
pub fn script_playMenuSelectSfx(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_main_playSfxDefault(0x7E, 5);
    return 0;
}
