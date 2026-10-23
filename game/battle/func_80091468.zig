//! Stores two 16-bit battle globals (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: func_80091468 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Stores arg0 and arg1 into the 16-bit globals D_800F1DB4 and D_800F1DB6.
pub fn func_80091468(arg0: c_int, arg1: c_int) callconv(.c) void {
    v.g.D_800F1DB4().* = psx.conv(i16, arg0);
    v.g.D_800F1DB6().* = psx.conv(i16, arg1);
}
