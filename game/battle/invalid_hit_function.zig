//! The hit function of index 0 (src/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: _invalidHitFunction (src/BATTLE/BATTLE.PRG/146C.c)
/// The invalid hit function (index 0 of _hitFunctions): returns no value.
pub fn invalidHitFunction(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    // The C returns no value; its object leaves the caller's %eax: uninitialised in the original, 0 here
    // (PORTING.md §11.4).
    return 0;
}
