//! An event script opcode that hands func_800AC37C an actor and a byte (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B9D74 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800AC37C with the actor of getShort(script + 1) and script[3].
pub fn func_800B9D74(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800AC37C(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), script[3]);
    return 0;
}
