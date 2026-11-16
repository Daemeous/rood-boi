//! Passes a mapped short from arg0+1 and the byte at arg0[3] to vs_audio_playActorScriptSound (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: vs_event_commandPlayActorSfx (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_playActorScriptSound with vs_event_resolveScriptActorReference of the short at arg0+1 and the byte at arg0[3]; returns 0.
pub fn vs_event_commandPlayActorSfx(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_audio_playActorScriptSound(
        c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(arg0 + 1))),
        @as(c_int, arg0[3]),
    );
    return 0;
}
