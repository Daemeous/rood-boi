//! Battle stub that reports whether func_8007CCAC returns a positive value (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B86E8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when func_8007CCAC() is greater than 0, else 0.
pub fn func_800B86E8(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return if (c.func_8007CCAC() > 0) 1 else 0;
}
