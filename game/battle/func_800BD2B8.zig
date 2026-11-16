//! Projects four corners of a box at depth 150 through the current matrices and
//! returns the lowest depth, scaled by ONE (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const gte = common.gte;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BD2B8 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the rotation and translation matrices from `arg0`, projects four corners and returns the lowest depth times ONE.
pub fn func_800BD2B8(arg0: ?*v.MATRIX) callconv(.c) c_int {
    var pos: v.VECTOR = undefined;
    var offset: v.SVECTOR = undefined;
    var first: c_int = undefined;
    var second: c_int = undefined;
    var h: c_int = undefined;
    var lowest: i16 = undefined;

    const view = gte.viewOf(arg0.?, gte.no_screen);

    offset.vx = 80;
    offset.vy = 80;
    offset.vz = 150;
    gte.transformTo(&pos, &view, &offset);
    // Sign-extends the low 15 bits of the depth, then subtracts 200.
    first = ((@as(c_int, c.vs_field_floorHeightAt(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;
    lowest = psx.conv(i16, first);

    offset.vx = -80;
    offset.vy = 80;
    offset.vz = 150;
    gte.transformTo(&pos, &view, &offset);
    second = ((@as(c_int, c.vs_field_floorHeightAt(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;

    if (second < first) {
        lowest = psx.conv(i16, second);
    }

    offset.vx = 80;
    offset.vy = -80;
    offset.vz = 150;
    gte.transformTo(&pos, &view, &offset);
    h = ((@as(c_int, c.vs_field_floorHeightAt(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;

    if (h < lowest) {
        lowest = psx.conv(i16, h);
    }

    offset.vx = -80;
    offset.vy = -80;
    offset.vz = 150;
    gte.transformTo(&pos, &view, &offset);
    h = ((@as(c_int, c.vs_field_floorHeightAt(pos.vx, pos.vz)) << 0x11) >> 0x11) -% 200;

    if (h < lowest) {
        lowest = psx.conv(i16, h);
    }

    return @as(c_int, lowest) *% 4096;
}
