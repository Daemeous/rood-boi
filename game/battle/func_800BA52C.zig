//! Forwards two bytes of a script record to the two battle calls (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA52C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8004552C with the stored byte and arg0[4], stores arg0[1], then calls func_80044F60 with arg0[1..3].
pub fn func_800BA52C(arg0: [*c]u8) callconv(.c) c_int {
    _ = c.func_8004552C(@as(c_int, v.g.D_800F4C69().*), 0, @as(c_int, arg0[4]));
    v.g.D_800F4C69().* = arg0[1];
    _ = c.func_80044F60(@as(c_int, arg0[1]), @as(c_int, arg0[2]), @as(c_uint, arg0[3]));
    return 0;
}
