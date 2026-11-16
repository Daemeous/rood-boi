//! Forwards bytes 1..3 of a command record to vs_audio_playMusicSlot (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandPlayMusic (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores byte 1 of the record in vs_event_musicSlot and passes bytes 1..3 to vs_audio_playMusicSlot.
pub fn vs_event_commandPlayMusic(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const b1: u8 = arg0[1];
    v.g.vs_event_musicSlot().* = b1;
    _ = c.vs_audio_playMusicSlot(@as(c_int, b1), @as(c_int, arg0[2]), @as(c_int, arg0[3]));
    return 0;
}
