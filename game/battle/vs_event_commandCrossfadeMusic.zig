//! Forwards two bytes of a script record to the two battle calls (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandCrossfadeMusic (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_fadeMusicSlotVolume with the stored byte and arg0[4], stores arg0[1], then calls vs_audio_crossfadeMusicSlot with arg0[1..3].
pub fn vs_event_commandCrossfadeMusic(arg0: [*c]u8) callconv(.c) c_int {
    _ = c.vs_audio_fadeMusicSlotVolume(@as(c_int, v.g.vs_event_musicSlot().*), 0, @as(c_int, arg0[4]));
    v.g.vs_event_musicSlot().* = arg0[1];
    _ = c.vs_audio_crossfadeMusicSlot(@as(c_int, arg0[1]), @as(c_int, arg0[2]), @as(c_uint, arg0[3]));
    return 0;
}
