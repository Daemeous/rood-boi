//! Passes bytes 1 and 2 of a script record, as signed values scaled by 128, to func_8008EAE4 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BA2B4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8008EAE4 with the signed bytes 1 and 2 of the record, each shifted left 7; returns 0.
pub fn func_800BA2B4(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // (signed char) promotes to int before the shift; |x| <= 128, so the shifted value fits in int.
    const first: c_int = @as(i8, @bitCast(arg0[1]));
    const second: c_int = @as(i8, @bitCast(arg0[2]));
    c.func_8008EAE4(first << 7, second << 7);
    return 0;
}
