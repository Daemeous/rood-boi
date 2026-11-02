//! Battle overlay helper from src/BATTLE/BATTLE.PRG/4A0A8.c: sets a flag and records a bit of its argument.
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: func_800B6744 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets D_800F4C2C, stores the inverted bit 0 of arg0[1] in D_800F4C20, and returns 4 when D_800F4C68 is set.
pub fn func_800B6744(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    v.g.D_800F4C2C().* = 1;
    v.g.D_800F4C20().* = psx.conv(u8, @intFromBool((@as(c_int, arg0[1]) & 1) == 0));
    return @as(c_int, @intFromBool(v.g.D_800F4C68().* != 0)) *% 4;
}
