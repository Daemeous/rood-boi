//! Battle overlay unit src/BATTLE/BATTLE.PRG/4A0A8.c: a stub that calls func_800CB79C.
const c = @import("c_4A0A8");

/// orig: func_800BB2E4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800CB79C and returns 0; both arguments are unused.
pub fn func_800BB2E4(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800CB79C();
    return 0;
}
