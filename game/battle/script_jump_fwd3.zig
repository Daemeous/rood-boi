//! Script jump operand: the address a forward jump opcode lands on (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_jumpFwd3 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the script address `arg0` plus the signed 16-bit operand at `arg0 + 1`, truncated to 32 bits.
pub fn script_jumpFwd3(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const off: c_short = c.vs_battle_getShort(arg0 + 1);
    // Address arithmetic wraps in 32 bits, as the (long) cast and the int return keep only the low 32 bits.
    const base: c_uint = psx.addr(arg0);
    return psx.conv(c_int, base +% psx.conv(c_uint, @as(c_int, off)));
}
