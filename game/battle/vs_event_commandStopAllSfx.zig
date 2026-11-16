//! Battle unit 4A0A8: calls vs_audio_stopAllSfxPlayback and returns 0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_commandStopAllSfx (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_stopAllSfxPlayback and returns 0; both arguments are unused.
pub fn vs_event_commandStopAllSfx(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_audio_stopAllSfxPlayback();
    return 0;
}
