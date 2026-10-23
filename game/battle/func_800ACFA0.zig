//! Stores a 6-entry SVECTOR from six big-endian bytes and sets its pad flags (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800ACFA0 (src/BATTLE/BATTLE.PRG/40564.c)
/// Reads three big-endian 16-bit values from `arg1`, doubles them into `arg0`'s vector, and sets pad flags on it and on the entry 84 vectors on.
pub fn func_800ACFA0(arg0: ?*v.SVECTOR, arg1: [*c]u8, arg2: c_int) callconv(.c) void {
    const sv = arg0.?;

    var a3: c_int = @as(c_int, arg1[0]);
    a3 <<= 8;
    a3 |= @as(c_int, arg1[1]);
    a3 *%= 2;
    sv.vx = psx.conv(i16, a3);

    a3 = @as(c_int, arg1[2]);
    a3 <<= 8;
    a3 |= @as(c_int, arg1[3]);
    a3 *%= 2;
    sv.vy = psx.conv(i16, a3);

    a3 = @as(c_int, arg1[4]);
    a3 <<= 8;
    a3 |= @as(c_int, arg1[5]);
    a3 *%= 2;
    sv.vz = psx.conv(i16, a3);

    sv.pad = 6;

    if (arg2 != 0xFF) {
        sv.pad = 0;
    }

    // arg0 += 84: 84 SVECTORs of 8 bytes on.
    psx.byteAt(v.SVECTOR, sv, 84 * 8).pad = 0;
}
