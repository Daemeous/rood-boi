//! Script opcode: sets a state flag from the pad (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_setFlagIfButton (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the state flag named by the script's 16-bit operand, from the pressed pad when bit 15 is set, else from the previous pad state.
pub fn script_setFlagIfButton(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    const temp_v0: c_short = c.vs_battle_getShort(arg0 + 1);
    const operand: c_int = @as(c_int, temp_v0);
    const value: c_int = if ((operand & 0x8000) != 0)
        @as(c_int, v.g.vs_main_buttonsPressed().pad[0].low)
    else
        v.g.vs_main_buttonsPreviousState().*;
    _ = c.vs_battle_setStateFlag(psx.conv(c_short, operand & 0x3FF), psx.conv(u8, value));
    return 0;
}
