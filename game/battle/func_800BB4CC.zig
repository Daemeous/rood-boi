//! Passes byte 1 of its first argument to func_8007C36C (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800BB4CC (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007C36C with the byte after the first byte of `arg0`; returns 0.
pub fn func_800BB4CC(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_8007C36C(@as(c_int, psx.byteAt(u8, arg0, 1).*));
    return 0;
}
