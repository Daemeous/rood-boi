//! Sets a camera move's duration from the distance it still has to cover (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// Length of the vector in 4.12 fixed point, as the C's `_vectorMagnitude` (a static of 4A0A8.c) computes it.
fn vectorMagnitude(arg0: *v.VECTOR) c_int {
    // _copyVector: copyVector's three components into a local copy.
    var sp10: v.VECTOR = undefined;
    sp10.vx = arg0.vx;
    sp10.vy = arg0.vy;
    sp10.vz = arg0.vz;

    const temp_lo: c_int = (sp10.vx >> 12) *% (sp10.vx >> 12);
    const temp_lo_2: c_int = (sp10.vy >> 12) *% (sp10.vy >> 12);
    const temp_v0: c_int = (sp10.vz >> 12) *% (sp10.vz >> 12);
    sp10.vx = sp10.vx >> 12;
    sp10.vy = sp10.vy >> 12;
    sp10.vz = sp10.vz >> 12;
    return c.vs_gte_rsqrt(temp_lo +% temp_lo_2 +% temp_v0) *% 4096;
}

/// orig: func_800BE53C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the move's duration in proportion to the distance from its current point to its position.
pub fn func_800BE53C(arg0: ?*v.D_800F4BA4_t2) callconv(.c) void {
    const a = arg0.?;
    var sp10: v.VECTOR = undefined;
    _ = c._vectorSubtract(psx.cptr(c.VECTOR, &sp10), psx.cptr(c.VECTOR, &a.current), psx.cptr(c.VECTOR, &a.position));

    const mag: c_int = vectorMagnitude(&sp10) >> 12;
    const prod: c_int = mag *% @as(c_int, a.duration);
    a.duration = psx.conv(u8, @divTrunc(prod, 1000));
}
