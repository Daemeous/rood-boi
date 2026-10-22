//! Wraps battle vertex pairs across the 8192-unit period when they are a tile apart
//! (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");

/// orig: func_800AF844 (src/BATTLE/BATTLE.PRG/44F14.c)
/// For each of `count` pairs, wraps the destination point across the 8192-unit period when the source-to-destination offset in x and z is at least one tile.
pub fn func_800AF844(dst: ?*v.SVECTOR, src: ?*v.SVECTOR, count: c_int) callconv(.c) void {
    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        var x: c_int = @as(c_int, psx.at(src.?, i).vx) -% @as(c_int, psx.at(dst.?, i).vx);

        if (x >= 4096) {
            x -%= 4096 * 2;
        } else if (x < -4096) {
            x +%= 4096 * 2;
        }

        x = if (x >= 0) x else -%x;
        var z: c_int = @as(c_int, psx.at(src.?, i).vz) -% @as(c_int, psx.at(dst.?, i).vz);

        if (z >= 4096) {
            z -%= 4096 * 2;
        } else if (z < -4096) {
            z +%= 4096 * 2;
        }

        z = if (z >= 0) z else -%z;

        if (x +% z >= 4096) {
            const d = psx.at(dst.?, i);
            d.vx = psx.conv(i16, @rem(@as(c_int, d.vx) +% 4096, 4096 * 2));
            d.vz = psx.conv(i16, @rem(@as(c_int, d.vz) +% 4096, 4096 * 2));
            d.vy = psx.conv(i16, @rem(4096 -% @as(c_int, d.vy), 4096 * 2));
        }
    }
}
