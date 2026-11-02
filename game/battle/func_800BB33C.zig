//! Battle overlay stub that calls func_800BE3A0 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BB33C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800BE3A0 and returns 0.
pub fn func_800BB33C(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800BE3A0();
    return 0;
}
