//! Passes two script bytes to func_800BC1CC and sets D_800F4BB6 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BB3BC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800BC1CC with the two script bytes (-1 where unset) and sets D_800F4BB6.
pub fn func_800BB3BC(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const first: c_int = if (arg0[1] != 0xFF) (@as(c_int, arg0[1]) +% 4) & 7 else -1;
    const second: c_int = if (arg0[2] != 0) @as(c_int, arg0[2]) else -1;
    c.func_800BC1CC(psx.conv(c_short, first), second);
    v.g.D_800F4BB6().* = 1;
    return 0;
}
