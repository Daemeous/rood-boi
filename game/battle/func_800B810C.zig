//! An event script test of an actor's flags (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B810C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Whether the actor of getShort(script + 1) has any flag of the mask getShort(script + 3):
/// its func_800A0BE0 flags with bits 0 and 7 inverted, bit 0 also set (when the mask asks
/// for it) where func_8007D03C says 0.
pub fn func_800B810C(script: [*c]u8) callconv(.c) c_int {
    const actor: c_int = c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))) & 0xFFFF;
    var flags: u16 = psx.conv(u16, c.func_800A0BE0(actor) ^ 0x81);
    const mask: i16 = c.vs_battle_getShort(script + 3);
    if (mask & 1 != 0) {
        if (c.func_8007D03C(actor) == 0) flags |= 1;
    }
    const hit: u16 = psx.conv(u16, @as(c_int, flags) & mask);
    return @intFromBool(hit != 0);
}
