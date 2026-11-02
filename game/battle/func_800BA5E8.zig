//! Forwards bytes 1..3 of a command record to func_80045000 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA5E8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores byte 1 of the record in D_800F4C69 and passes bytes 1..3 to func_80045000.
pub fn func_800BA5E8(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const b1: u8 = arg0[1];
    v.g.D_800F4C69().* = b1;
    _ = c.func_80045000(@as(c_int, b1), @as(c_int, arg0[2]), @as(c_int, arg0[3]));
    return 0;
}
