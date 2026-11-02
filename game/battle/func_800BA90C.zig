//! Battle unit 4A0A8: forwards to func_8007BEC4 and, when D_800F4C2C is 2, to func_800CF1A8 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA90C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007BEC4 with the second byte of arg0, then func_800CF1A8 if D_800F4C2C is 2; returns 0.
pub fn func_800BA90C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007BEC4(@as(c_int, arg0[1]));
    if (v.g.D_800F4C2C().* == 2) {
        c.func_800CF1A8();
    }
    return 0;
}
