//! Eases each vector of one 42-entry table toward its source, wrapping the
//! difference at one turn, and averages the second table (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");

/// orig: func_800AE980 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Halves each of count vector differences from dst toward src, wrapping past one turn, and averages the unk150 table.
pub fn func_800AE980(dst: ?*v.D_800F4538_unkC54, src: ?*v.D_800F4538_unkC54, count: c_int) callconv(.c) void {
    const d = dst.?;
    const s = src.?;

    d.unk0[41].vx = psx.conv(i16, (@as(c_int, s.unk0[41].vx) +% @as(c_int, d.unk0[41].vx)) >> 1);
    d.unk0[41].vy = psx.conv(i16, (@as(c_int, s.unk0[41].vy) +% @as(c_int, d.unk0[41].vy)) >> 1);
    d.unk0[41].vz = psx.conv(i16, (@as(c_int, s.unk0[41].vz) +% @as(c_int, d.unk0[41].vz)) >> 1);

    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        const dv = psx.at(&d.unk0, i);
        const sv = psx.at(&s.unk0, i);

        var x: c_int = @as(c_int, sv.vx) -% @as(c_int, dv.vx);
        if (x >= 4096) {
            x -%= 4096 * 2;
        } else if (x < -4096) {
            x +%= 4096 * 2;
        }
        dv.vx = psx.conv(i16, @as(c_int, dv.vx) +% (x >> 1));

        var y: c_int = @as(c_int, sv.vy) -% @as(c_int, dv.vy);
        if (y >= 4096) {
            y -%= 4096 * 2;
        } else if (y < -4096) {
            y +%= 4096 * 2;
        }
        dv.vy = psx.conv(i16, @as(c_int, dv.vy) +% (y >> 1));

        var z: c_int = @as(c_int, sv.vz) -% @as(c_int, dv.vz);
        if (z >= 4096) {
            z -%= 4096 * 2;
        } else if (z < -4096) {
            z +%= 4096 * 2;
        }
        dv.vz = psx.conv(i16, @as(c_int, dv.vz) +% (z >> 1));

        const ev = psx.at(&d.unk150, i);
        const es = psx.at(&s.unk150, i);
        ev.vx = psx.conv(i16, (@as(c_int, es.vx) +% @as(c_int, ev.vx)) >> 1);
        ev.vy = psx.conv(i16, (@as(c_int, es.vy) +% @as(c_int, ev.vy)) >> 1);
        ev.vz = psx.conv(i16, (@as(c_int, es.vz) +% @as(c_int, ev.vz)) >> 1);
    }
}
