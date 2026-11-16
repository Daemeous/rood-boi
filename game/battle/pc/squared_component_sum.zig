//! Sum of three squares (D2: 7BDD4.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");
const gte = @import("common").gte;


/// orig: vs_core_mathSquaredVectorLength (pc/src/boot_sdk.c)
/// x*x + y*y + z*z through the GTE's SQR, which squares the signed 16-bit
/// halves of the arguments (IR1-IR3), whatever their int values.
pub fn squaredComponentSum(x: c_int, y: c_int, z: c_int) callconv(.c) c_int {
    var ctx = gte.Ctx.init(); // this call's own registers
    ctx.mtc2(9, @bitCast(x));
    ctx.mtc2(10, @bitCast(y));
    ctx.mtc2(11, @bitCast(z));
    ctx.cop2(0x0A00428); // gte_sqr0
    return @bitCast(ctx.mfc2(25) +% ctx.mfc2(26) +% ctx.mfc2(27));
}
