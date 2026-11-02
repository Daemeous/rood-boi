//! Forwards four bytes of a script record to func_8008B808 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B988C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8008B808 with bytes 1, 3, 2 and 4 of the record; returns 0.
pub fn func_800B988C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_8008B808(@as(c_int, arg0[1]), @as(c_int, arg0[3]), @as(c_int, arg0[2]), @as(c_int, arg0[4]));
    return 0;
}
