//! An event script opcode that hands func_800A0618 an actor and a table-based value (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B83D4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800A0618 with the actor of getShort(script + 1), D_800E9B58[script[6] >> 6] +
/// script[3], script[6] & 0x3F and script[4].
pub fn func_800B83D4(script: [*c]u8) callconv(.c) c_int {
    const actor: c_int = c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    _ = c.func_800A0618(actor, @as(c_int, v.g.D_800E9B58()[script[6] >> 6]) + script[3], script[6] & 0x3F, script[4]);
    return 0;
}
