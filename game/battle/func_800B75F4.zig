//! An event script opcode that places an actor on a map cell (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B75F4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Puts the script's actor at the centre of cell (script[3], script[5]): func_800A9D90 with a
/// 0x8000 heading while D_800F4C2C is 2, else func_8007CFF8; then func_8009DC8C and its D_800F4B70 mark.
pub fn func_800B75F4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var pos: v.SVECTOR = undefined;
    const actor: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    pos.vx = psx.conv(i16, (@as(c_int, script[3]) << 7) +% 0x40);
    pos.vy = 0;
    pos.vz = psx.conv(i16, (@as(c_int, script[5]) << 7) +% 0x40);
    if (v.g.D_800F4C2C().* == 2) {
        pos.pad = -0x8000;
        c.func_800A9D90(actor, psx.cptr(c.SVECTOR, &pos), 0);
    } else {
        // pad stays unset as in the C; func_8007CFF8 reads only vx and vz.
        pos.pad = 0;
        c.func_8007CFF8(actor, psx.cptr(c.SVECTOR, &pos));
    }
    c.func_8009DC8C(actor);
    v.g.D_800F4B70()[actor] = 1;
    return 0;
}
