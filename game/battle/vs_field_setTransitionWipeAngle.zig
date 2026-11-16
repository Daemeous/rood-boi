//! Screen transition wipe angle setter (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_field_setTransitionWipeAngle (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the screen transition wipe angle.
pub fn vs_field_setTransitionWipeAngle(arg0: c_int) callconv(.c) void {
    v.g.vs_battle_screenTransitionWipeAngle().* = arg0;
}
