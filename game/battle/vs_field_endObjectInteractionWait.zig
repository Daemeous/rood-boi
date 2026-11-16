//! Three calls in turn (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_field_endObjectInteractionWait (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls func_800CB50C, vs_field_resumeFieldAfterMenu and func_8006F5CC.
pub fn vs_field_endObjectInteractionWait() callconv(.c) void {
    c.func_800CB50C();
    c.vs_field_resumeFieldAfterMenu();
    c.func_8006F5CC();
}
