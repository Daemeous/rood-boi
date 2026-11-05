//! Blending two animation pose records (D2: 48C7C.s; oracle pc/src/actor_blend.c).
const c = @import("c_actor_blend");
const psx = @import("common").psx;

/// orig: blend_vector (pc/src/actor_blend.c)
/// Moves the three shorts at `dst` toward `src` by the GTE's GPF (scaled by IR0),
/// the difference of a rotation wrapped to half a turn (8192); the first
/// component is unsigned when `unsigned_x`. Stores only what the original
/// does, the third word as a full int over dst[2] and dst[3].
fn blendVector(dst: [*]align(1) i16, src: [*]align(1) const i16, rotation: bool, unsigned_x: bool) void {
    const base = [3]c_int{
        if (unsigned_x) @as(c_int, psx.conv(u16, dst[0])) else @as(c_int, dst[0]),
        dst[1],
        dst[2],
    };
    var delta = [3]c_int{
        (if (unsigned_x) @as(c_int, psx.conv(u16, src[0])) else @as(c_int, src[0])) -% base[0],
        @as(c_int, src[1]) -% base[1],
        @as(c_int, src[2]) -% base[2],
    };
    var i: usize = 0;
    while (i < 3) : (i += 1) {
        if (rotation) {
            if (delta[i] >= 4096) {
                delta[i] -%= 8192;
            } else if (delta[i] < -4096) {
                delta[i] +%= 8192;
            }
        }
        if (i != 2 or !rotation or delta[i] != 0) c.MTC2(@bitCast(delta[i]), @intCast(9 + i));
    }
    _ = c.doCOP2(0x0198003D); // gte_gpf12
    if ((delta[0] | delta[1] | delta[2]) == 0) return;
    const dx: c_int = @bitCast(c.MFC2(25));
    const dy: c_int = @bitCast(c.MFC2(26));
    const dz: c_int = @bitCast(c.MFC2(27));
    if ((dx | dy) != 0) {
        dst[0] = psx.conv(i16, base[0] +% dx);
        dst[1] = psx.conv(i16, base[1] +% dy);
    }
    if (dz != 0) @as(*align(1) c_int, @ptrCast(dst + 2)).* = base[2] +% dz;
}

/// orig: func_800B147C (pc/src/actor_blend.c)
/// Blends the pose record `source` into `destination` (`count` bones of 8
/// bytes at +0, +0x150, and the root at +0x148) by `step` of `duration`:
/// a copy of the whole 0x60C-byte record at the end, the halfway blend
/// (func_800AE980) at half; otherwise the weight step / duration (Q12) goes to
/// IR0 and every vector is blended. A zero duration divides as the R3000 does.
pub fn blendAnimationRecords(destination: [*]u8, source: [*]u8, count: c_int, step: c_int, duration: c_int) callconv(.c) void {
    if (step == duration) {
        _ = c.vs_main_memcpy(destination, source, 0x60c);
        return;
    }
    if (step == psx.conv(c_int, @as(c_uint, @bitCast(duration)) >> 1)) {
        c.func_800AE980(destination, source, count);
        return;
    }
    if (count <= 0 or count > 42) c.pc_unimplemented("invalid animation blend extent");
    const dividend: c_int = @bitCast(@as(c_uint, @bitCast(step)) << 12);
    // `div $zero` has no zero check: the R3000 leaves LO = -1 for a non-negative dividend and 1 for a negative one.
    const weight: c_int = if (duration != 0) @divTrunc(dividend, duration) else if (dividend < 0) 1 else -1;
    c.MTC2(@bitCast(weight), 8);
    const dst: [*]align(1) i16 = @ptrCast(destination);
    const src: [*]align(1) const i16 = @ptrCast(source);
    blendVector(dst + 0x148 / 2, src + 0x148 / 2, false, false);
    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        const bone: usize = @intCast(i); // 0 <= i < count
        blendVector(dst + bone * 4, src + bone * 4, true, false);
        blendVector(dst + 0x150 / 2 + bone * 4, src + 0x150 / 2 + bone * 4, false, true);
    }
}
