//! Reads the battle byte D_800F4C34 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800BEB00 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the value of D_800F4C34, widened to int as C promotes it.
pub fn func_800BEB00() callconv(.c) c_int {
    return @as(c_int, v.g.D_800F4C34().*);
}
