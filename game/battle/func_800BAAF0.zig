//! Calls func_80044DD4 with the byte at arg0[1] (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BAAF0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes the byte at arg0[1] to func_80044DD4 and returns 0.
pub fn func_800BAAF0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_80044DD4(@as(c_int, arg0[1]));
    return 0;
}
