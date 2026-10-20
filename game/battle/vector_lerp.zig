//! Linear interpolation of vectors (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_lerpSvector (src/BATTLE/BATTLE.PRG/5BF94.c)
/// `vec` = start + (end - start) * t / 128 per axis, from (start, end) pairs at `src`.
pub fn lerpSvector(src: [*c]i16, t: c_int, vec: ?*v.SVECTOR) callconv(.c) void {
    const start0: c_int = src[0];
    const end0: c_int = src[1];
    const start1: c_int = src[2];
    const end1: c_int = src[3];
    const start2: c_int = src[4];
    const end2: c_int = src[5];

    vec.?.vx = psx.conv(i16, (((end0 -% start0) *% t) >> 7) +% start0);
    vec.?.vy = psx.conv(i16, (((end1 -% start1) *% t) >> 7) +% start1);
    vec.?.vz = psx.conv(i16, (((end2 -% start2) *% t) >> 7) +% start2);
}
