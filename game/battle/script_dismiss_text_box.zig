//! Text-box script opcode: dismisses the text box named by the script's operand byte (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_dismissTextBox (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Dismisses the text box numbered by the byte after the opcode; always returns 0.
pub fn script_dismissTextBox(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_battle_dismissTextBox(@as(c_int, psx.at(arg0, 1).*));
    return 0;
}
