//! Stores its argument in the global D_800F2270 (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80095B70 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Stores `arg0` in D_800F2270, truncated to 16 bits as the C store does.
pub fn func_80095B70(arg0: c_int) callconv(.c) void {
    v.g.D_800F2270().* = psx.conv(i16, arg0);
}
