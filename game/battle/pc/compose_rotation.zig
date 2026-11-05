//! Rotation composition (D2: 32154.s / 4B16C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const psx = @import("common").psx;

/// orig: compose_rotation (pc/src/boot_sdk.c)
/// Rotates the columns (2, 1, 0) of a copy of `b` by the rotation of `a` through the
/// GTE (RTV0), storing the rotation into `result` and leaving its translation.
fn composeRotation(a: [*c]c.MATRIX, b: [*c]c.MATRIX, result: [*c]c.MATRIX) [*c]c.MATRIX {
    const right: c.MATRIX = b.*;
    c.SetRotMatrix(a);
    var column: usize = 3;
    while (column > 0) {
        column -= 1;
        const vx: c_uint = psx.conv(u16, right.m[0][column]);
        const vy: c_uint = psx.conv(u16, right.m[1][column]);
        const vz: c_uint = psx.conv(u16, right.m[2][column]);
        // gte_ldv0 on the stack SVECTOR { x, y, z, 0 }: its two words.
        c.MTC2(vx | (vy << 16), 0);
        c.MTC2(vz, 1);
        _ = c.doCOP2(0x0486012); // gte_rtv0
        result.*.m[0][column] = psx.conv(i16, c.MFC2(9));
        result.*.m[1][column] = psx.conv(i16, c.MFC2(10));
        result.*.m[2][column] = psx.conv(i16, c.MFC2(11));
    }
    return result;
}

/// orig: func_800B196C (pc/src/boot_sdk.c)
/// Rotates the matrix `b` in place by the rotation of `a` (the translation of `b` stays).
pub fn composeRotationInPlace(a: [*c]c.MATRIX, b: [*c]c.MATRIX) callconv(.c) [*c]c.MATRIX {
    return composeRotation(a, b, b);
}
