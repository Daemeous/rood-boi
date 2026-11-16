//! Battle unit 4A0A8: calls vs_field_setTransitionWipeAngle with byte 1 of its argument scaled by 16
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9AC4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_field_setTransitionWipeAngle with byte 1 of its argument times 16; returns 0.
pub fn func_800B9AC4(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_field_setTransitionWipeAngle(@as(c_int, arg0[1]) *% 0x10);
    return 0;
}
