//! An event script opcode that hands func_8007C28C an actor (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B92A0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007C28C with the actor of getShort(script + 1).
pub fn func_800B92A0(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007C28C(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    return 0;
}
