//! Actor rotation matrix (D2: 48C7C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const psx = @import("common").psx;

fn mtc2(value: c_int, reg: c_int) void {
    c.MTC2(@bitCast(value), reg);
}

fn mfc2(reg: c_int) c_int {
    return @bitCast(c.MFC2(reg));
}

/// orig: func_800B17F0 (pc/src/boot_sdk.c)
/// The rotation matrix of the SVECTOR `angles` (the actors' order, which is not
/// RotMatrixYXZ_gte's), in three GPF stages that keep the original rounding; returns `result`.
pub fn actorRotationMatrix(angles: [*c]c.SVECTOR, result: [*c]c.MATRIX) callconv(.c) [*c]c.MATRIX {
    const sy = c.rsin(@as(c_int, angles.*.vy));
    const cy = c.rcos(@as(c_int, angles.*.vy));
    const sx2 = c.rsin(@as(c_int, angles.*.vx)) *% 2;
    const cx2 = c.rcos(@as(c_int, angles.*.vx)) *% 2;
    const sz2 = c.rsin(@as(c_int, angles.*.vz)) *% 2;
    const cz2 = c.rcos(@as(c_int, angles.*.vz)) *% 2;
    mtc2(sz2, 8);
    mtc2(sx2, 9);
    mtc2(cx2, 10);
    mtc2(cy, 11);
    _ = c.doCOP2(0x0198003D); // gte_gpf12
    const a0 = mfc2(9);
    const a1 = mfc2(10);
    const m10 = mfc2(11) >> 1;
    mtc2(cz2, 8);
    mtc2(sx2, 9);
    mtc2(cx2, 10);
    mtc2(cy, 11);
    _ = c.doCOP2(0x0198003D);
    const a2 = mfc2(9);
    const a3 = mfc2(10);
    const m00 = mfc2(11) >> 1;
    mtc2(sy, 8);
    mtc2(a2, 9);
    mtc2(a0, 10);
    mtc2(a3, 11);
    _ = c.doCOP2(0x0198003D);
    result.*.m[0][0] = psx.conv(i16, m00);
    result.*.m[0][1] = psx.conv(i16, (mfc2(9) -% a1) >> 2);
    result.*.m[0][2] = psx.conv(i16, (mfc2(11) +% a0) >> 2);
    result.*.m[1][0] = psx.conv(i16, m10);
    result.*.m[1][1] = psx.conv(i16, (mfc2(10) +% a3) >> 2);
    result.*.m[1][2] = psx.conv(i16, (((sy *% a1) >> 12) -% a2) >> 2);
    result.*.m[2][0] = psx.conv(i16, -%sy);
    result.*.m[2][1] = psx.conv(i16, (cy *% sx2) >> 13);
    result.*.m[2][2] = psx.conv(i16, (cy *% cx2) >> 13);
    return result;
}
