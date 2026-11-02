//! Battle unit 4A0A8: calls func_80045DC0 and returns 0 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BA3E4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_80045DC0 and returns 0; both arguments are unused.
pub fn func_800BA3E4(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_80045DC0();
    return 0;
}
