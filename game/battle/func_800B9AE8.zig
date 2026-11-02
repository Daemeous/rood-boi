//! Calls the 0x8007C444 helper with three bytes of `arg0` (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9AE8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes bytes 1, 2 and 3 * 0x10 of `arg0` to func_8007C444 and returns 0.
pub fn func_800B9AE8(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007C444(@as(c_int, arg0[1]), @as(c_int, arg0[2]), @as(c_int, arg0[3]) *% 0x10);
    return 0;
}
