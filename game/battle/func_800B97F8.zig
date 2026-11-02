//! Battle overlay unit 4A0A8: a small call-through that returns 0.
const c = @import("c_4A0A8");

/// orig: func_800B97F8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007C31C and returns 0.
pub fn func_800B97F8(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007C31C();
    return 0;
}
