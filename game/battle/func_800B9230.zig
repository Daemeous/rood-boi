//! Passes a byte of the caller's record to func_8007D15C (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800B9230 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007D15C with the byte at offset 1 of its argument; always returns 0.
pub fn func_800B9230(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_8007D15C(@as(c_int, psx.at(arg0, 1).*));
    return 0;
}
