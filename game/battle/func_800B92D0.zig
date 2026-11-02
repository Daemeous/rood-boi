//! Passes the inverted low bit of arg0[1] to func_8007BD8C (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B92D0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007BD8C with arg0[1] ^ 1 and returns 0.
pub fn func_800B92D0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_8007BD8C(@as(c_int, arg0[1] ^ 1));
    return 0;
}
