//! Stores two battle globals through its pointer arguments (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: func_8008EB04 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores vs_field_roomWeatherKind (unsigned) through arg0 and vs_field_weatherIntensity (signed) through arg1.
pub fn func_8008EB04(arg0: [*c]c_int, arg1: [*c]c_int) callconv(.c) void {
    arg0.* = @as(c_int, v.g.vs_field_roomWeatherKind().*);
    arg1.* = @as(c_int, v.g.vs_field_weatherIntensity().*);
}
