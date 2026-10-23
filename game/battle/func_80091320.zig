//! Battle unit 2842C: stores the value in D_800F1DC4, then runs the two helpers that follow it (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80091320 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Sets D_800F1DC4 to `arg0`, then calls func_8009121C and func_80090B28.
pub fn func_80091320(arg0: c_int) callconv(.c) void {
    v.g.D_800F1DC4().* = psx.conv(i16, arg0);
    c.func_8009121C();
    c.func_80090B28();
}
