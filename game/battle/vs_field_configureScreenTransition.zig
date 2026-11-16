//! Screen transition state setters (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_configureScreenTransition (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the screen transition effect, speed and wipe angle.
pub fn vs_field_configureScreenTransition(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    v.g.vs_battle_screenTransitionEffect().* = arg0;
    v.g.vs_battle_screenTransitionSpeed().* = arg1;
    v.g.vs_battle_screenTransitionWipeAngle().* = arg2;
}
