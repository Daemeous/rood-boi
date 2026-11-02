//! Builds a rotation matrix from a direction vector (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: _vecToRotMatrix (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Rotation matrix `arg1` for the vector `arg0` (scaled down by 4096), from its yaw and pitch.
pub fn vecToRotMatrix(arg0: ?*v.VECTOR, arg1: ?*v.MATRIX) callconv(.c) void {
    // pad fields are never written by the C: uninitialised stack bytes there.
    var sp10: v.SVECTOR = undefined;
    var sp18: v.SVECTOR = undefined;
    const vec = arg0.?;

    sp18.vx = psx.conv(i16, vec.vx >> 12);
    sp18.vy = psx.conv(i16, vec.vy >> 12);
    sp18.vz = psx.conv(i16, vec.vz >> 12);

    sp10.vy = psx.conv(i16, c.ratan2(@as(c_int, sp18.vx), @as(c_int, sp18.vz)));

    // Products of two i16 can sum past i32 (-32768 squared twice): wrap, as the C does.
    const sumSq: c_int = @as(c_int, sp18.vx) *% @as(c_int, sp18.vx) +%
        @as(c_int, sp18.vz) *% @as(c_int, sp18.vz);
    sp10.vx = psx.conv(i16, c.ratan2(@as(c_int, sp18.vy), c.vs_gte_rsqrt(sumSq)));

    sp10.vz = 0;

    _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, &sp10), @ptrCast(arg1));
}
