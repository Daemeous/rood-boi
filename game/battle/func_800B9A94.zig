//! Forwards three bytes of a script opcode to vs_field_beginScreenFadeIn (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9A94 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_field_beginScreenFadeIn with the script's bytes 1 and 2 and byte 3 times 16.
pub fn func_800B9A94(arg0: [*c]u8) callconv(.c) c_int {
    c.vs_field_beginScreenFadeIn(@as(c_int, arg0[1]), @as(c_int, arg0[2]), @as(c_int, arg0[3]) *% 0x10);
    return 0;
}
