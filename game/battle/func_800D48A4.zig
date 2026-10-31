//! Battle unit 5BF94: sets a unit's state byte from a helper's result (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D48A4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes the low byte of func_800D5170's result to func_800D55A4, sets unkA to 0xB7, and returns 1.
pub fn func_800D48A4(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const flags: c_int = c.func_800D5170(@ptrCast(arg0)) & 0xFF;
    c.func_800D55A4(@ptrCast(arg0), flags, 0);
    arg0.?.unkA = 0xB7;
    return 1;
}
