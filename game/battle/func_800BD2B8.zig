//! Projects four corners of a box at depth 150 through the current matrices and
//! returns the lowest depth, scaled by ONE (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD2B8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the rotation and translation matrices from `arg0`, projects four corners and returns the lowest depth times ONE.
pub fn func_800BD2B8(arg0: ?*v.MATRIX) callconv(.c) c_int {
    var pos: v.VECTOR = undefined;
    var offset: v.SVECTOR = undefined;
    var flag: c_int = undefined;
    var first: c_int = undefined;
    var second: c_int = undefined;
    var h: c_int = undefined;
    var lowest: i16 = undefined;

    c.SetRotMatrix(@ptrCast(arg0));
    c.SetTransMatrix(@ptrCast(arg0));

    offset.vx = 80;
    offset.vy = 80;
    offset.vz = 150;
    c.RotTrans(psx.cptr(c.SVECTOR, &offset), psx.cptr(c.VECTOR, &pos), &flag);
    // Sign-extends the low 15 bits of the depth, then subtracts 200.
    first = ((@as(c_int, c.func_8008DC7C(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;
    lowest = psx.conv(i16, first);

    offset.vx = -80;
    offset.vy = 80;
    offset.vz = 150;
    c.RotTrans(psx.cptr(c.SVECTOR, &offset), psx.cptr(c.VECTOR, &pos), &flag);
    second = ((@as(c_int, c.func_8008DC7C(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;

    if (second < first) {
        lowest = psx.conv(i16, second);
    }

    offset.vx = 80;
    offset.vy = -80;
    offset.vz = 150;
    c.RotTrans(psx.cptr(c.SVECTOR, &offset), psx.cptr(c.VECTOR, &pos), &flag);
    h = ((@as(c_int, c.func_8008DC7C(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;

    if (h < lowest) {
        lowest = psx.conv(i16, h);
    }

    offset.vx = -80;
    offset.vy = -80;
    offset.vz = 150;
    c.RotTrans(psx.cptr(c.SVECTOR, &offset), psx.cptr(c.VECTOR, &pos), &flag);
    h = ((@as(c_int, c.func_8008DC7C(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;

    if (h < lowest) {
        lowest = psx.conv(i16, h);
    }

    return @as(c_int, lowest) *% 4096;
}
