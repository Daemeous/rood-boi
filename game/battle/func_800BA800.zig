//! Battle script opcode: calls vs_audio_fadeSfxPitch with a table entry and three script bytes (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA800 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_audio_fadeSfxPitch with a table entry and three script bytes; returns 0.
pub fn func_800BA800(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_audio_fadeSfxPitch(
        v.g.D_800E9B34()[@as(usize, arg0[1])],
        @as(c_int, arg0[2]),
        @as(c_int, psx.conv(i8, arg0[3])),
        arg0[4],
    );
    return 0;
}
