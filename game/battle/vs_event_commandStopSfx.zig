//! Calls the 0x80045D64 helper with a table entry and a byte argument from a script record (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandStopSfx (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Passes D_800E9B34[arg0[1]] and arg0[2] to vs_audio_stopSfx; returns 0.
pub fn vs_event_commandStopSfx(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const table = v.g.D_800E9B34();
    c.vs_audio_stopSfx(table[arg0[1]], @as(c_int, arg0[2]));
    return 0;
}
