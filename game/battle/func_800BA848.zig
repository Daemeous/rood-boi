//! Calls vs_audio_fadeSfxPan with a D_800E9B34 entry and three script bytes (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA848 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_fadeSfxPan with D_800E9B34 indexed by the script's second byte and the next three bytes; returns 0.
pub fn func_800BA848(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_audio_fadeSfxPan(v.g.D_800E9B34()[arg0[1]], @as(c_int, arg0[2]), @as(c_int, arg0[3]), arg0[4]);
    return 0;
}
