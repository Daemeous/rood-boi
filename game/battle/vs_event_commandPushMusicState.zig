//! Battle script handler in unit 4A0A8: calls the shared helper and returns 0
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_commandPushMusicState (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_saveMusicPlaybackState and returns 0; the arguments are ignored.
pub fn vs_event_commandPushMusicState(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_audio_saveMusicPlaybackState();
    return 0;
}
