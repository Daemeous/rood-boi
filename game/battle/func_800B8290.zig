//! An event script opcode that turns an actor to, or by, a rotation (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B8290 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Hands the actor of getShort(script + 1) the vector (getShort(+4), getShort(+6), getShort(+8))
/// and script[10] (0xFF as -1): func_800A9F64 when script[3] is set, else func_800A9FD0; then its
/// D_800F4B70 mark.
pub fn func_800B8290(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var vec: v.SVECTOR = undefined;
    const actor: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    var third: i16 = -1;
    if (script[10] != 0xFF) third = script[10];
    vec.vx = c.vs_battle_getShort(script + 4);
    vec.vy = c.vs_battle_getShort(script + 6);
    vec.vz = c.vs_battle_getShort(script + 8);
    // pad is uninitialised in the original; neither callee reads it: 0 here (PORTING.md §11.4).
    vec.pad = 0;
    if (script[3] != 0) {
        c.func_800A9F64(actor, psx.cptr(c.SVECTOR, &vec), third);
    } else {
        c.func_800A9FD0(actor, psx.cptr(c.SVECTOR, &vec), third);
    }
    v.g.D_800F4B70()[actor] = 1;
    return 0;
}
