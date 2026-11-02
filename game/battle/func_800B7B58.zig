//! An event script opcode that moves an actor to, or by, a position (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B7B58 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// The actor of getShort(script + 1) goes to (getShort(script + 3) >> 1, getShort(script + 5),
/// getShort(script + 7)), added to its func_800A1108 position when script[3] bit 0 is set, with
/// a 0x8000 heading, through func_800A9D90 (third argument 0 while D_800F4C2C is 2, else script[9],
/// 0xFF as -1); then func_8009DC8C and its D_800F4B70 mark.
pub fn func_800B7B58(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var state: v.func_8006EBF8_t = undefined;
    const actor: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    const x: c_int = @as(c_int, c.vs_battle_getShort(script + 3)) >> 1;
    const y: i16 = c.vs_battle_getShort(script + 5);
    const z: i16 = c.vs_battle_getShort(script + 7);
    const pos = &state.unk0.unk4;
    if (script[3] & 1 != 0) {
        _ = c.func_800A1108(actor, @ptrCast(&state));
        pos.vx = psx.conv(i16, @as(c_int, pos.vx) +% x);
        pos.vy = psx.conv(i16, @as(c_int, pos.vy) +% y);
        pos.vz = psx.conv(i16, @as(c_int, pos.vz) +% z);
    } else {
        pos.vx = psx.conv(i16, x);
        pos.vy = y;
        pos.vz = z;
    }
    pos.pad = -0x8000;
    if (v.g.D_800F4C2C().* == 2) {
        c.func_800A9D90(actor, psx.cptr(c.SVECTOR, pos), 0);
    } else {
        c.func_800A9D90(actor, psx.cptr(c.SVECTOR, pos), if (script[9] != 0xFF) script[9] else -1);
    }
    c.func_8009DC8C(actor);
    v.g.D_800F4B70()[actor] = 1;
    return 0;
}
