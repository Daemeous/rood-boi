//! Stores two arguments into the table at D_800F1D98, then runs func_80090B28
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009134C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Sets table entries 4 and 5 to the two arguments, then calls func_80090B28.
pub fn func_8009134C(arg0: c_int, arg1: c_int) callconv(.c) void {
    v.g.D_800F1D98()[5] = psx.conv(i16, arg0);
    v.g.D_800F1D98()[4] = psx.conv(i16, arg1);
    c.func_80090B28();
}
