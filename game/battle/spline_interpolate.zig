//! A cubic spline point through four SVECTORs (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_splineInterpolate (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The point at t (4.12, 0..4096 between p1 and p2) of the cubic through p0..p3, in the C's fixed-point steps.
pub fn splineInterpolate(p0: ?*v.SVECTOR, p1: ?*v.SVECTOR, p2: ?*v.SVECTOR, p3: ?*v.SVECTOR, t: c_int, out: ?*v.SVECTOR) callconv(.c) void {
    const neg_t = -%t;
    const t_minus = t -% 4096;
    const t_plus = t +% 4096;
    const w0 = (((((neg_t *% t_minus) >> 0xC) *% (t -% 4096 * 2)) >> 0xC) *% (4096 * 2 / 3)) >> 0xE;
    const w1 = (((t_plus *% t_minus) >> 0xC) *% (t -% 4096 * 2)) >> 0xD;
    const w2 = (((t_plus *% neg_t) >> 0xC) *% (t -% 4096 * 2)) >> 0xD;
    const w3 = (((((t_plus *% t) >> 0xC) *% t_minus) >> 0xC) *% (4096 * 2 / 3)) >> 0xE;
    const a = p0.?;
    const b = p1.?;
    const cc = p2.?;
    const d = p3.?;
    out.?.vx = psx.conv(i16, ((@as(c_int, a.vx) *% w0) +% (@as(c_int, b.vx) *% w1) +% (@as(c_int, cc.vx) *% w2) +% (@as(c_int, d.vx) *% w3)) >> 0xC);
    out.?.vy = psx.conv(i16, ((@as(c_int, a.vy) *% w0) +% (@as(c_int, b.vy) *% w1) +% (@as(c_int, cc.vy) *% w2) +% (@as(c_int, d.vy) *% w3)) >> 0xC);
    out.?.vz = psx.conv(i16, ((@as(c_int, a.vz) *% w0) +% (@as(c_int, b.vz) *% w1) +% (@as(c_int, cc.vz) *% w2) +% (@as(c_int, d.vz) *% w3)) >> 0xC);
}
