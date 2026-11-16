//! Battle unit 4A0A8: a test on the result of func_8007BF0C (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_commandWaitEffectContinuation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when func_8007BF0C does not return 1, else 0; both arguments are unused.
pub fn vs_event_commandWaitEffectContinuation(_: [*c]u8, _: c_short) callconv(.c) c_int {
    return @as(c_int, @intFromBool(c.func_8007BF0C() != 1));
}
