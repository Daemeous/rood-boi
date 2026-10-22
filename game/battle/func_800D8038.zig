//! Stores its argument in the battle global D_800F5680 (src/BATTLE/BATTLE.PRG/6F014.c).
const psx = @import("common").psx;
const v = @import("views_6F014");

/// orig: func_800D8038 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Stores `arg0` in the battle global D_800F5680.
pub fn func_800D8038(arg0: c_int) callconv(.c) void {
    v.g.D_800F5680().* = psx.conv(u32, arg0);
}
