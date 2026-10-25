//! Battle overlay unit 30DB0: stores a value in D_800E8FC0 (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A0870 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores its argument in D_800E8FC0, truncated to the global's width.
pub fn func_800A0870(arg0: c_int) callconv(.c) void {
    v.g.D_800E8FC0().* = psx.conv(u8, arg0);
}
