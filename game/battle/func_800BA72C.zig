//! Battle script handler in unit 4A0A8: calls the shared helper and returns 0
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BA72C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_80044DB4 and returns 0; the arguments are ignored.
pub fn func_800BA72C(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_80044DB4();
    return 0;
}
