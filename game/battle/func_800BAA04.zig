//! Battle overlay unit 4A0A8: returns 4 when D_800F4C2C is 2, else 0 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BAA04 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007BF04, then returns 4 if D_800F4C2C is 2 and 0 otherwise.
pub fn func_800BAA04(_: [*c]u8, _: c_short) callconv(.c) c_int {
    c.func_8007BF04();
    return @as(c_int, @intFromBool(v.g.D_800F4C2C().* == 2)) * 4;
}
