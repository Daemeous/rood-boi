//! Returns the battle global D_800F4C64 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");

/// orig: func_800B8C44 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the value of the global D_800F4C64; both arguments are unused.
pub fn func_800B8C44(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return v.g.D_800F4C64().*;
}
