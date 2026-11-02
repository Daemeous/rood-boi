//! Forwards two bytes of a script operand to func_80045574 with the current D_800F4C69 value (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA650 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_80045574 with D_800F4C69 and the next two operand bytes (the second signed); returns 0.
pub fn func_800BA650(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_80045574(
        @as(c_int, v.g.D_800F4C69().*),
        @as(c_int, @as(i8, @bitCast(arg0[1]))),
        @as(c_int, arg0[2]),
    );
    return 0;
}
