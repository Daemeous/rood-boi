//! Battle helper in src/BATTLE/BATTLE.PRG/4A0A8.c: stores script byte 1 and passes it on.
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA890 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores byte 1 of the script in D_800F4C69, passes it to func_80044EC8, and returns 0.
pub fn func_800BA890(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const value: u8 = arg0[1];
    v.g.D_800F4C69().* = value;
    _ = c.func_80044EC8(@as(c_int, value));
    return 0;
}
