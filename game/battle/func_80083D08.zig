//! An action effect: the applicable statuses 0x12 on the target (src/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80083D08 (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets the target's applicable statuses from set 0x12.
pub fn func_80083D08(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    // No return statement in the C: the object tail-calls, so the callee's result is what it returns.
    return c._setApplicableStatuses(0x12, @ptrCast(target));
}
