//! Stores a byte into the global D_800F1DCB (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_80091314 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Stores the low byte of `arg0` into D_800F1DCB.
pub fn func_80091314(arg0: c_int) callconv(.c) void {
    v.g.D_800F1DCB().* = psx.conv(u8, arg0);
}
