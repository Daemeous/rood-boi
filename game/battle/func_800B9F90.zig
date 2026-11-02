//! Battle overlay wrapper: runs func_8008F234, then returns 4 if D_800F4C2C is 2, else 0
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B9F90 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8008F234, then returns 4 when D_800F4C2C equals 2, otherwise 0.
pub fn func_800B9F90(_: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_8008F234();
    return if (v.g.D_800F4C2C().* == 2) 4 else 0;
}
