//! Reads the first byte of entry `arg0` in the table D_800F1DD8 (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80093764 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Returns the unk0 byte of D_800F1DD8 entry `arg0`, unchecked as the C is.
pub fn func_80093764(arg0: c_int) callconv(.c) c_int {
    return @as(c_int, psx.at(v.g.D_800F1DD8(), arg0).unk0);
}
