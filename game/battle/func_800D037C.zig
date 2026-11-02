//! Moves an effect point along a spline (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D037C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// The point on the spline through its saved position, two control points and its endpoint at its frame: linear, eased or by a curve (mode bits 5..6).
pub fn func_800D037C(mode_arg: ?*v.func_800D0548_arg0, point: ?*v.func_800D0548_arg1) callconv(.c) void {
    const a = mode_arg.?;
    const b = point.?;
    var start: v.SVECTOR = undefined;
    var end: v.SVECTOR = undefined;
    if ((a.mode & 0x80) != 0) {
        start = b.saved;
        end.vx = psx.conv(i16, b.endpoint.vx);
        end.vy = psx.conv(i16, b.endpoint.vy);
        end.vz = psx.conv(i16, b.endpoint.vz);
    } else {
        end = b.saved;
        start.vx = psx.conv(i16, b.endpoint.vx);
        start.vy = psx.conv(i16, b.endpoint.vy);
        start.vz = psx.conv(i16, b.endpoint.vz);
    }
    var t: c_int = 0; // mode 3 leaves it uninitialised in the C
    switch ((a.mode >> 5) & 3) {
        0 => t = @divTrunc(@as(c_int, b.frame) *% 3 *% 4096, @as(c_int, a.limit) + 1) -% 4096,
        1 => t = @divTrunc(-%c.rcos(@divTrunc(@as(c_int, b.frame) *% (4096 / 2), @as(c_int, a.limit) + 1)) *% 3, 2) +% 4096 / 2,
        2 => t = c.vs_battle_sampleCurve(@as(c_int, a.curve) + 1, b.frame) *% 96 -% 4096,
        else => {},
    }
    var result: v.SVECTOR = undefined;
    c.vs_battle_splineInterpolate(@ptrCast(&start), @ptrCast(&b.control0), @ptrCast(&b.control1), @ptrCast(&end), t, @ptrCast(&result));
    b.position.vx = result.vx;
    b.position.vy = result.vy;
    b.position.vz = result.vz;
}
