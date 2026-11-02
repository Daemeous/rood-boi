//! Battle script opcode helper: one table-driven call with three script bytes (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA39C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_80045BFC with a D_800E9B34 entry and script bytes 2 to 4; the result is 0.
pub fn func_800BA39C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_80045BFC(v.g.D_800E9B34()[arg0[1]], @as(c_int, arg0[2]), @as(c_int, arg0[3]), arg0[4]);
    return 0;
}
