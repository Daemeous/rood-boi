//! Fixed-point arctangent of two 4.12 coordinates (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: _atan2FixedPoint (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Angle of (arg1, arg0) from 4.12 fixed point: both arguments drop 6 bits before libgte's ratan2.
pub fn atan2FixedPoint(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    return c.ratan2(arg0 >> 6, arg1 >> 6);
}
