//! An action effect that does nothing (src/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: func_80084FF0 (src/BATTLE/BATTLE.PRG/146C.c)
/// The empty action effect.
pub fn func_80084FF0(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    _ = target;
    return 0; // no return statement in the C (the game's object returns 0)
}
