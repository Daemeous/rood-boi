//! Returns the byte at D_800E8F2C plus one (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: func_8009CFA0 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Returns the byte at D_800E8F2C plus one.
pub fn func_8009CFA0() callconv(.c) c_int {
    // The byte is widened to c_int before the add, so 255 + 1 cannot overflow.
    return @as(c_int, v.g.D_800E8F2C().*) + 1;
}
