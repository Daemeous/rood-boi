//! Battle overlay unit 4A0A8: a stub that passes a global byte to func_80044DF4
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA6B8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_80044DF4 with the byte at D_800F4C69 and returns 0.
pub fn func_800BA6B8(_: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_80044DF4(@as(c_int, v.g.D_800F4C69().*));
    return 0;
}
