//! Sum of three squares (D2: 7BDD4.s; oracle pc/src/boot_sdk.c).
const c = @import("c_boot_sdk");

/// orig: func_800E4660 (pc/src/boot_sdk.c)
/// x*x + y*y + z*z through the GTE's SQR, which squares the signed 16-bit
/// halves of the arguments (IR1-IR3), whatever their int values.
pub fn squaredComponentSum(x: c_int, y: c_int, z: c_int) callconv(.c) c_int {
    c.MTC2(@bitCast(x), 9);
    c.MTC2(@bitCast(y), 10);
    c.MTC2(@bitCast(z), 11);
    _ = c.doCOP2(0x0A00428); // gte_sqr0
    return @bitCast(c.MFC2(25) +% c.MFC2(26) +% c.MFC2(27));
}
