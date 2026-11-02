//! Battle helper that forwards three bytes of a script record to func_8007C46C (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9A40 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007C398; on 0 forwards arg0[1], arg0[2] and arg0[3] * 16 to func_8007C46C. Returns 0 only when func_8007C398 returns 5.
pub fn func_800B9A40(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_v0 = c.func_8007C398();
    if (temp_v0 != 5) {
        if (temp_v0 == 0) {
            // arg0[3] promotes to int; at most 255 * 16, so the multiply cannot overflow.
            c.func_8007C46C(@as(c_int, arg0[1]), @as(c_int, arg0[2]), @as(c_int, arg0[3]) *% 0x10);
        }
        return 1;
    }
    return 0;
}
