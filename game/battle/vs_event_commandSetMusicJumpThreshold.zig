//! Calls vs_audio_setMusicJumpThreshold with the byte at arg0[1] (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_commandSetMusicJumpThreshold (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes the byte at arg0[1] to vs_audio_setMusicJumpThreshold and returns 0.
pub fn vs_event_commandSetMusicJumpThreshold(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_audio_setMusicJumpThreshold(@as(c_int, arg0[1]));
    return 0;
}
