//! Forwards two bytes of a script operand to vs_audio_fadeMusicSlotTempo with the current vs_event_musicSlot value (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandSlideMusicTempo (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_fadeMusicSlotTempo with vs_event_musicSlot and the next two operand bytes (the second signed); returns 0.
pub fn vs_event_commandSlideMusicTempo(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.vs_audio_fadeMusicSlotTempo(
        @as(c_int, v.g.vs_event_musicSlot().*),
        @as(c_int, @as(i8, @bitCast(arg0[1]))),
        @as(c_int, arg0[2]),
    );
    return 0;
}
