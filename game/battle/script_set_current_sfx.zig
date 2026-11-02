//! Script opcode that sets the current sound effect (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_setCurrentSfx (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the current sound effect to the script's first operand byte and returns 0.
pub fn script_setCurrentSfx(script: [*c]u8, _: c_short) callconv(.c) c_int {
    // arg0[1] is an unsigned char: promoted to int, passed as the id.
    _ = c.vs_main_setCurrentSfx(@as(c_int, script[1]));
    return 0;
}
