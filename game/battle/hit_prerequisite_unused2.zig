//! An unused attack prerequisite (src/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: _hitPrerequisiteUnused2 (src/BATTLE/BATTLE.PRG/146C.c)
/// An unused attack prerequisite (index 39 of _attackPrerequisiteFunctions): returns no value (0 in the object).
pub fn hitPrerequisiteUnused2(_: ?*v.vs_action_t, _: [*c]u8) callconv(.c) c_int {
    // The C returns no value; GCC's object (both targets) returns 0.
    return 0;
}
