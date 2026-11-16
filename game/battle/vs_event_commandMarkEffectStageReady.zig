//! Battle unit 4A0A8: forwards byte 1 of its argument to vs_effects_requeueTrack
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_commandMarkEffectStageReady (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes byte 1 of `arg0` to vs_effects_requeueTrack and returns 0; `arg1` is unused.
pub fn vs_event_commandMarkEffectStageReady(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    c.vs_effects_requeueTrack(@as(c_int, arg0[1]));
    return 0;
}
