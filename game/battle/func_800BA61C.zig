//! Forwards global D_800F4C69 and two bytes of its argument to func_8004552C (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA61C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8004552C with D_800F4C69 and bytes 1 and 2 of its argument; returns 0.
pub fn func_800BA61C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_8004552C(@as(c_int, v.g.D_800F4C69().*), @as(c_int, arg0[1]), @as(c_int, arg0[2]));
    return 0;
}
