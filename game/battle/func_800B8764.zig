//! Battle unit 4A0A8: forwards the byte at arg0[1] to func_8007CC6C
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B8764 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007CC6C with the byte at arg0[1] and returns 0.
pub fn func_800B8764(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007CC6C(@as(c_int, arg0[1]));
    return 0;
}
