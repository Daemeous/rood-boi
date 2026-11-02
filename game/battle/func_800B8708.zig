//! Battle flag dispatch: forwards the byte at arg0[1] to one of two C routines (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B8708 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Forwards the flag byte at arg0[1] to one of two C routines, then returns 4 if D_800F4C2C is 2.
pub fn func_800B8708(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const flags: u8 = arg0[1];
    if ((flags & 4) != 0) {
        c.func_8007CC4C(@as(c_int, flags & 3));
    } else {
        c.func_8007CBBC(@as(c_int, flags));
    }
    return @as(c_int, @intFromBool(v.g.D_800F4C2C().* == 2)) *% 4;
}
