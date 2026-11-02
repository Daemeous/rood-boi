//! Forwards a global and two bytes of its argument to func_800455AC (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA684 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800455AC with D_800F4C69 and the signed byte at arg0[1] and the unsigned byte at arg0[2]; returns 0.
pub fn func_800BA684(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_800455AC(
        @as(c_int, v.g.D_800F4C69().*),
        @as(c_int, @as(i8, @bitCast(arg0[1]))),
        @as(c_int, arg0[2]),
    );
    return 0;
}
