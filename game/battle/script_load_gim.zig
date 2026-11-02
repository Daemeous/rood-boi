//! Script opcode that loads a GIM (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_loadGim (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Loads the GIM named by the script operand at arg0[1..2] with class arg0[3]; returns 0.
pub fn script_loadGim(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const name = psx.conv(c_int, c.vs_battle_getShort(arg0 + 1));
    c.vs_battle_loadGim(name, psx.conv(c_int, arg0[3]));
    return 0;
}
