//! Battle overlay unit 4A0A8: a stub that passes a global byte to vs_audio_stopMusicSlotPlayback
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandStopMusic (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_stopMusicSlotPlayback with the byte at vs_event_musicSlot and returns 0.
pub fn vs_event_commandStopMusic(_: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_audio_stopMusicSlotPlayback(@as(c_int, v.g.vs_event_musicSlot().*));
    return 0;
}
