//! Battle helper that calls the two 4A0A8 routines with table entries and a packed word
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptConfigureTimer (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_state_startSceneTimer with two table entries and a packed 24-bit value when byte 1 is set, else vs_state_resetSceneTimer.
pub fn vs_event_scriptConfigureTimer(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    if (arg0[1] != 0) {
        const word: c_uint = (@as(c_uint, arg0[4]) << 24) | (@as(c_uint, arg0[5]) << 16) | (@as(c_uint, arg0[6]) << 8);
        c.vs_state_startSceneTimer(
            @as(c_int, v.g.vs_event_sceneTimerModes()[arg0[3]]),
            @as(c_int, v.g.vs_event_sceneTimerResetScopes()[arg0[2]]),
            @bitCast(word),
        );
    } else {
        c.vs_state_resetSceneTimer();
    }
    return 0;
}
