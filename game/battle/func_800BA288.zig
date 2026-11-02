//! Forwards two bytes of a battle script operand to func_80091A78 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800BA288 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_80091A78 with the operand's byte 1 (unsigned) and byte 2 (signed); returns 0.
pub fn func_800BA288(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const first: c_int = @as(c_int, arg0[1]);
    const second: c_int = @as(c_int, psx.conv(i8, arg0[2]));
    _ = c.func_80091A78(first, second);
    return 0;
}
