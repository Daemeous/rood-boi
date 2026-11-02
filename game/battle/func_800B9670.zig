//! An event script opcode that hands func_8009D7E8 an actor and a byte (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B9670 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8009D7E8 with the actor of getShort(script + 1) and script[3].
pub fn func_800B9670(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8009D7E8(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), script[3]);
    return 0;
}
