//! Stores a byte to the battle global D_800F4BA0 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: func_800BBE04 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets D_800F4BA0 to the low byte of `arg0`.
pub fn func_800BBE04(arg0: c_int) callconv(.c) void {
    v.g.D_800F4BA0().* = psx.conv(u8, arg0);
}
