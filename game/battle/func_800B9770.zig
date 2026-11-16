//! Forwards two bytes of a script record to vs_field_setMovingPlatformVisibility (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9770 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes bytes 1 and 2 of its record to vs_field_setMovingPlatformVisibility and returns 0.
pub fn func_800B9770(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_field_setMovingPlatformVisibility(@as(c_int, arg0[1]), @as(c_int, arg0[2]));
    return 0;
}
