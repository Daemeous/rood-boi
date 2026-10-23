//! Sign-extends the low 15 bits of func_8008DD0C's result (src/BATTLE/BATTLE.PRG/3A1A0.c).
const c = @import("c_3A1A0");

/// orig: func_800A3BC4 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Calls func_8008DD0C and returns its short result, sign-extended from bit 14 (zero stays zero).
pub fn func_800A3BC4(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    const temp_v0: c_short = c.func_8008DD0C(arg0, arg1);
    if (temp_v0 == 0) {
        return 0;
    }
    // (temp << 17) >> 17 in 32-bit int: the shift wraps as GCC's two's complement does.
    const wide: c_int = @as(c_int, temp_v0);
    const shifted: u32 = @as(u32, @bitCast(wide)) << 17;
    const back: c_int = @bitCast(shifted);
    return back >> 17;
}
