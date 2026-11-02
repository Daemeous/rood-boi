//! An event script opcode (src/BATTLE/BATTLE.PRG/4A0A8.c): the distance and heading between two actors into state flags.
const psx = @import("common").psx;
const std = @import("std");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BAC5C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Flag getShort(script + 6): the x/z distance between the actors of references getShort(script + 1) and
/// getShort(script + 3), divided by script[5] (-1 from 256 up); the next flag: their heading, ratan2 >> 4.
pub fn func_800BAC5C(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var first = std.mem.zeroes(v.func_8006EBF8_t); // the C's out-parameter on the stack, written by func_800A1108
    var second = std.mem.zeroes(v.func_8006EBF8_t);
    const flag = c.vs_battle_getShort(script + 6);
    _ = c.func_800A1108(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))), &first);
    _ = c.func_800A1108(c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 3))), &second);
    const dx = psx.conv(i16, @as(c_int, first.unk0.unk4.vx) -% second.unk0.unk4.vx);
    const dz = psx.conv(i16, @as(c_int, first.unk0.unk4.vz) -% second.unk0.unk4.vz);
    const distance = @divTrunc(c.vs_gte_rsqrt(@as(c_int, dx) * dx +% @as(c_int, dz) * dz), @as(c_int, script[5])); // a short squared: no overflow
    c.vs_battle_setStateFlag(flag, psx.conv(u8, if (distance < 0x100) distance & 0xFF else -1));
    c.vs_battle_setStateFlag(psx.conv(i16, @as(c_int, flag) +% 1), psx.conv(u8, (c.ratan2(dx, dz) >> 4) & 0xFF));
    return 0;
}
