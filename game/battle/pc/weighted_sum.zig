//! A weighted sum through the GTE (D2: 7399C.s; oracle pc/src/battle_gte.c).
const c = @import("c_battle_gte");

/// orig: func_800DC284 (pc/src/battle_gte.c)
/// a * 6 + 4 * b + c + 2 * d for signed 16-bit b, c, d (the halves of the words
/// loaded into the rotation matrix), by MVMVA, leaving the matrix, IR and MAC
/// registers as the PS1 code did.
pub fn weightedSum(a: c_int, b: c_int, cc: c_int, d: c_int) callconv(.c) c_int {
    c.CTC2((@as(c_uint, @bitCast(cc)) << 16) | (@as(c_uint, @bitCast(b)) & 0xffff), 0);
    c.CTC2(@bitCast(d), 1);
    c.CTC2(0, 2);
    c.CTC2(0, 3);
    c.CTC2(0, 4);
    c.MTC2(4, 9);
    c.MTC2(1, 10);
    c.MTC2(2, 11);
    _ = c.doCOP2(0x041E012); // gte_mvmva(0, 0, 3, 3, 0)
    return a *% 6 +% @as(c_int, @bitCast(c.MFC2(25)));
}
