//! Battle script opcode helper: one table-driven call with three script bytes (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandSlideSfxVolume (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_fadeSfxVolume with a D_800E9B34 entry and script bytes 2 to 4; the result is 0.
pub fn vs_event_commandSlideSfxVolume(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_audio_fadeSfxVolume(v.g.D_800E9B34()[arg0[1]], @as(c_int, arg0[2]), @as(c_int, arg0[3]), arg0[4]);
    return 0;
}
