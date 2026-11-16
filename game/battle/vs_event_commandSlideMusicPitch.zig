//! Forwards a global and two bytes of its argument to vs_audio_fadeMusicSlotPitch (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandSlideMusicPitch (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_fadeMusicSlotPitch with vs_event_musicSlot and the signed byte at arg0[1] and the unsigned byte at arg0[2]; returns 0.
pub fn vs_event_commandSlideMusicPitch(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_audio_fadeMusicSlotPitch(
        @as(c_int, v.g.vs_event_musicSlot().*),
        @as(c_int, @as(i8, @bitCast(arg0[1]))),
        @as(c_int, arg0[2]),
    );
    return 0;
}
