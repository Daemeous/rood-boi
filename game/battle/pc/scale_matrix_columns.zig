//! Scaling a matrix's columns (D2: 4B16C.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const psx = @import("common").psx;
const gte = @import("common").gte;


/// orig: vs_core_mathScaleMatrixColumns (pc/src/boot_sdk.c)
/// Multiplies each column of the matrix by the matching component of `scale`
/// through the GTE (GPF); the translation stays. Returns `matrix`.
pub fn scaleMatrixColumns(matrix: [*c]c.MATRIX, scale: [*c]c.VECTOR) callconv(.c) [*c]c.MATRIX {
    var ctx = gte.Ctx.init(); // this call's own registers
    const factors = [3]c_int{ scale.*.vx, scale.*.vy, scale.*.vz };
    var column: usize = 0;
    while (column < 3) : (column += 1) {
        ctx.mtc2(8, @bitCast(factors[column]));
        ctx.mtc2(9, @bitCast(@as(c_int, matrix.*.m[0][column])));
        ctx.mtc2(10, @bitCast(@as(c_int, matrix.*.m[1][column])));
        ctx.mtc2(11, @bitCast(@as(c_int, matrix.*.m[2][column])));
        ctx.cop2(0x0198003D); // gte_gpf12
        matrix.*.m[0][column] = psx.conv(i16, ctx.mfc2(9));
        matrix.*.m[1][column] = psx.conv(i16, ctx.mfc2(10));
        matrix.*.m[2][column] = psx.conv(i16, ctx.mfc2(11));
    }
    return matrix;
}
