//! Battle unit 2842C: stores the value in vs_field_weatherIntensity, then runs the two helpers that follow it (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_setWeatherIntensity (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Sets vs_field_weatherIntensity to `arg0`, then calls vs_field_applyWeatherIntensity and func_80090B28.
pub fn vs_field_setWeatherIntensity(arg0: c_int) callconv(.c) void {
    v.g.vs_field_weatherIntensity().* = psx.conv(i16, arg0);
    c.vs_field_applyWeatherIntensity();
    c.func_80090B28();
}
