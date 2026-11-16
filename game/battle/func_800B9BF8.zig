//! Battle overlay wrapper: reports whether vs_field_screenTransitionPhase returns nonzero (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9BF8 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when the call to vs_field_screenTransitionPhase is nonzero, else 0.
pub fn func_800B9BF8() callconv(.c) c_int {
    return @as(c_int, @intFromBool(c.vs_field_screenTransitionPhase() != 0));
}
