//! Passes the inverted second byte of a script to vs_field_setWeatherVariationEnabled (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BA0E4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_field_setWeatherVariationEnabled with byte 1 of `arg0` XOR 1, and returns 0.
pub fn func_800BA0E4(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_field_setWeatherVariationEnabled(@as(c_int, arg0[1] ^ 1));
    return 0;
}
