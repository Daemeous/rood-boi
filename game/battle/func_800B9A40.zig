//! Battle helper that forwards three bytes of a script record to vs_field_beginScreenFadeOut (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9A40 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_field_screenTransitionPhase; on 0 forwards arg0[1], arg0[2] and arg0[3] * 16 to vs_field_beginScreenFadeOut. Returns 0 only when vs_field_screenTransitionPhase returns 5.
pub fn func_800B9A40(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_v0 = c.vs_field_screenTransitionPhase();
    if (temp_v0 != 5) {
        if (temp_v0 == 0) {
            // arg0[3] promotes to int; at most 255 * 16, so the multiply cannot overflow.
            c.vs_field_beginScreenFadeOut(@as(c_int, arg0[1]), @as(c_int, arg0[2]), @as(c_int, arg0[3]) *% 0x10);
        }
        return 1;
    }
    return 0;
}
