//! Address of the battle table vs_field_backgroundGradientColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_roomBackgroundColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the address of the five-entry table vs_field_backgroundGradientColors (array decay).
pub fn vs_field_roomBackgroundColors() callconv(.c) [*c]c_int {
    return @ptrCast(v.g.vs_field_backgroundGradientColors());
}
