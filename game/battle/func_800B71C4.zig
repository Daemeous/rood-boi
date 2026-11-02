//! Calls func_800C86AC, then returns 4 when D_800F4C2C is 2, else 0 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B71C4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800C86AC, then returns 4 when D_800F4C2C is 2, else 0.
pub fn func_800B71C4(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_800C86AC();
    return @as(c_int, @intFromBool(@as(c_int, v.g.D_800F4C2C().*) == 2)) * 4;
}
