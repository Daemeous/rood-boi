//! Stores a byte of a script's argument block in D_800F19C8 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800B97E4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores the second byte of `arg0` in D_800F19C8 and returns 0.
pub fn func_800B97E4(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    v.g.D_800F19C8().* = psx.conv(i32, arg0[1]);
    return 0;
}
