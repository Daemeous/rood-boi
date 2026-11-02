//! An event script opcode (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800BB38C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Resolves the script's actor reference getShort(script + 1) and hands it to func_800BBE10.
pub fn func_800BB38C(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800BBE10(psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1)))));
    return 0;
}
