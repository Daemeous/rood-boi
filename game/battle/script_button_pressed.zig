//! Script button check: whether a button in the script's mask is held
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: vs_battle_script_buttonPressed (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 4 when a button in the mask byte arg0[1] is held, else 1.
pub fn script_buttonPressed(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // u_int all & (u_char promoted to int): the usual conversions make the AND unsigned.
    const pressed: c_uint = v.g.vs_main_buttonsPressed().all;
    const mask: c_uint = arg0[1];
    if (pressed & mask != 0) {
        return 4;
    }
    return 1;
}
