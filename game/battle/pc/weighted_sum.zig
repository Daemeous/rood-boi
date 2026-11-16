//! A weighted sum through the GTE (D2: 7399C.s; oracle pc/src/battle_gte.c).
const c = @import("c_battle_gte");
const gte = @import("common").gte;


/// orig: vs_battle_aiScoreActorCondition (pc/src/battle_gte.c)
/// a * 6 + 4 * b + c + 2 * d for signed 16-bit b, c, d (the halves of the words
/// loaded into the rotation matrix), by MVMVA, leaving the matrix, IR and MAC
/// registers as the PS1 code did.
pub fn weightedSum(a: c_int, b: c_int, cc: c_int, d: c_int) callconv(.c) c_int {
    var ctx = gte.Ctx.init(); // this call's own registers
    ctx.ctc2(0, (@as(c_uint, @bitCast(cc)) << 16) | (@as(c_uint, @bitCast(b)) & 0xffff));
    ctx.ctc2(1, @bitCast(d));
    ctx.ctc2(2, 0);
    ctx.ctc2(3, 0);
    ctx.ctc2(4, 0);
    ctx.mtc2(9, 4);
    ctx.mtc2(10, 1);
    ctx.mtc2(11, 2);
    ctx.cop2(0x041E012); // gte_mvmva(0, 0, 3, 3, 0)
    return a *% 6 +% @as(c_int, @bitCast(ctx.mfc2(25)));
}
