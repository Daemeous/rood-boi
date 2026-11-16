//! Stores a byte into the global vs_field_isWeatherVariationEnabled (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

/// orig: vs_field_setWeatherVariationEnabled (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Stores the low byte of `arg0` into vs_field_isWeatherVariationEnabled.
pub fn vs_field_setWeatherVariationEnabled(arg0: c_int) callconv(.c) void {
    v.g.vs_field_isWeatherVariationEnabled().* = psx.conv(u8, arg0);
}
