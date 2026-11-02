//! Forwards two bytes of a battle command record to func_8007BBB8 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B91C4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes bytes 2 and 1 of its argument to func_8007BBB8 and returns 0.
pub fn func_800B91C4(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007BBB8(@as(c_int, arg0[2]), @as(c_int, arg0[1]));
    return 0;
}
