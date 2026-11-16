//! Stores two arguments into the table at D_800F1D98, then runs func_80090B28
//! (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_setWeatherWindAngles (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Sets table entries 4 and 5 to the two arguments, then calls func_80090B28.
pub fn vs_field_setWeatherWindAngles(arg0: c_int, arg1: c_int) callconv(.c) void {
    v.g.D_800F1D98()[5] = psx.conv(i16, arg0);
    v.g.D_800F1D98()[4] = psx.conv(i16, arg1);
    c.func_80090B28();
}
