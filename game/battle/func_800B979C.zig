//! Reads a script's 16-bit value and sets a flag from its sign bit (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B979C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the flag from the sign of the 16-bit value at arg0+1; when set, calls the handler with its low 11 bits.
pub fn func_800B979C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp: c_int = c.vs_battle_getShort(arg0 + 1);
    // Sign bit of the 16-bit value, spread to 0 or -1 (int arithmetic), then stored as u8.
    const signFlag: c_int = (temp << 16) >> 31;
    v.g.D_800F4C6A().* = psx.conv(u8, signFlag);
    if (v.g.D_800F4C6A().* != 0) {
        c.func_80098194(temp & 0x7FF);
    }
    return 0;
}
