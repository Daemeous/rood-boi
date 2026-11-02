//! An event script opcode that points an actor at another actor or a target (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B7C90 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// The actor of getShort(script + 1) and the target of getShort(script + 3), both resolved by
/// func_800BFE50 and taken as 16 bits: func_8009FFA8 for a target at or above 0x2000, else
/// func_8009FF08 with script[5] and script[6].
pub fn func_800B7C90(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const actor: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    const target: c_uint = psx.conv(c_uint, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 3)))) & 0xFFFF;
    if (target >= 0x2000) {
        _ = c.func_8009FFA8(actor, psx.conv(c_int, target));
    } else {
        c.func_8009FF08(actor, psx.conv(c_int, target), script[5], script[6]);
    }
    return 0;
}
