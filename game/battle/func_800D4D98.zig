//! Clears the battle flag at D_800F522C (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: func_800D4D98 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears D_800F522C and reports success.
pub fn func_800D4D98(_: ?*v.D_800F53B8_t) callconv(.c) c_int {
    v.g.D_800F522C().* = 0;
    return 1;
}
