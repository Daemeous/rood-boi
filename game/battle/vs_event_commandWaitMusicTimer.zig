//! Compares the value from vs_audio_musicBeatPosition against the short at arg0 + 1 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_commandWaitMusicTimer (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns 1 when the high half of vs_audio_musicBeatPosition's result is below the short at `arg0 + 1`, else 0.
pub fn vs_event_commandWaitMusicTimer(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // Call order as in the C: vs_audio_musicBeatPosition first, then vs_battle_getShort.
    const temp_s1: c_int = c.vs_audio_musicBeatPosition() >> 0x10;
    const limit: c_int = c.vs_battle_getShort(arg0 + 1);
    return @intFromBool(temp_s1 < limit);
}
