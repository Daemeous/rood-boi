//! An event script opcode (src/BATTLE/BATTLE.PRG/4A0A8.c): the distance between two actors into a state flag.
const psx = @import("common").psx;
const std = @import("std");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BAEA0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Flag getShort(script + 5): the x/z distance between the actors of references getShort(script + 1)
/// and getShort(script + 3), divided by 128 (rounded toward zero).
pub fn func_800BAEA0(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var first = std.mem.zeroes(v.func_8006EBF8_t); // the C's out-parameter on the stack, written by func_800A1108
    var second = std.mem.zeroes(v.func_8006EBF8_t);
    _ = c.func_800A1108(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), &first);
    _ = c.func_800A1108(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 3))), &second);
    const dx = psx.conv(i16, @as(c_int, first.unk0.unk4.vx) -% second.unk0.unk4.vx);
    const dz = psx.conv(i16, @as(c_int, first.unk0.unk4.vz) -% second.unk0.unk4.vz);
    const flag: c_int = c.vs_battle_getShort(script + 5);
    var distance = c.vs_gte_rsqrt(@as(c_int, dx) * dx +% @as(c_int, dz) * dz); // a short squared: no overflow
    if (distance < 0) distance +%= 0x7F;
    c.vs_battle_setStateFlag(psx.conv(i16, flag), psx.conv(u8, (distance >> 7) & 0xFF));
    return 0;
}
