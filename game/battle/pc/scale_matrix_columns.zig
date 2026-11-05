//! Scaling a matrix's columns (D2: 4B16C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const psx = @import("common").psx;

/// orig: func_800B396C (pc/src/boot_sdk.c)
/// Multiplies each column of the matrix by the matching component of `scale`
/// through the GTE (GPF); the translation stays. Returns `matrix`.
pub fn scaleMatrixColumns(matrix: [*c]c.MATRIX, scale: [*c]c.VECTOR) callconv(.c) [*c]c.MATRIX {
    const factors = [3]c_int{ scale.*.vx, scale.*.vy, scale.*.vz };
    var column: usize = 0;
    while (column < 3) : (column += 1) {
        c.MTC2(@bitCast(factors[column]), 8);
        c.MTC2(@bitCast(@as(c_int, matrix.*.m[0][column])), 9);
        c.MTC2(@bitCast(@as(c_int, matrix.*.m[1][column])), 10);
        c.MTC2(@bitCast(@as(c_int, matrix.*.m[2][column])), 11);
        _ = c.doCOP2(0x0198003D); // gte_gpf12
        matrix.*.m[0][column] = psx.conv(i16, c.MFC2(9));
        matrix.*.m[1][column] = psx.conv(i16, c.MFC2(10));
        matrix.*.m[2][column] = psx.conv(i16, c.MFC2(11));
    }
    return matrix;
}
