//! Forwards global vs_event_musicSlot and two bytes of its argument to vs_audio_fadeMusicSlotVolume (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandSlideMusicVolume (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_fadeMusicSlotVolume with vs_event_musicSlot and bytes 1 and 2 of its argument; returns 0.
pub fn vs_event_commandSlideMusicVolume(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_audio_fadeMusicSlotVolume(@as(c_int, v.g.vs_event_musicSlot().*), @as(c_int, arg0[1]), @as(c_int, arg0[2]));
    return 0;
}
