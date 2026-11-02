//! An event script opcode that hands an actor a vector with mode -1 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B8590 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8009FC60 for the actor of getShort(script + 1) with -1, the vector
/// (getShort(+3), getShort(+5), getShort(+7)) and script[9] (0 as 0x1000).
pub fn func_800B8590(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var vec: v.SVECTOR = undefined;
    vec.vx = c.vs_battle_getShort(script + 3);
    vec.vy = c.vs_battle_getShort(script + 5);
    vec.vz = c.vs_battle_getShort(script + 7);
    // pad is uninitialised in the original and func_8009FC60 copies the whole vector into the
    // actor (unk17F4.pad): 0 here (PORTING.md §11.4).
    vec.pad = 0;
    const actor: c_int = c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    var speed: c_int = 0x1000;
    if (script[9] != 0) speed = script[9];
    _ = c.func_8009FC60(actor, -1, psx.cptr(c.SVECTOR, &vec), speed);
    return 0;
}
