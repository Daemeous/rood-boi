//! Battle helper from src/BATTLE/BATTLE.PRG/4A0A8.c: forwards two bytes of its argument block to func_80091A1C.
const c = @import("c_4A0A8");

/// orig: func_800B9860 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_80091A1C with bytes 1 and 2 of its argument block and returns 0.
pub fn func_800B9860(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_80091A1C(@as(c_int, arg0[1]), @as(c_int, arg0[2]));
    return 0;
}
