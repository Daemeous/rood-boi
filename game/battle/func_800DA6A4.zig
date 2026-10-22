//! Battle action-candidate step: runs the movement candidate when flagged, else
//! tests the two candidate checks and falls back (src/BATTLE/BATTLE.PRG/71618.c).
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA6A4 (src/BATTLE/BATTLE.PRG/71618.c)
/// Advances the action candidate state `state`; returns 1 when the candidate was accepted.
pub fn func_800DA6A4(state: ?*v.da4bcState) callconv(.c) c_int {
    const s = state.?;
    if (s.flagE != 0)
        return c.func_800DB61C(@ptrCast(s), 1);
    const target = c.func_800DBB2C(s, null, 3);
    if (target >= 0 and c.func_800DBB2C(s, null, 2) >= 0 and
        c.func_800DB370(@ptrCast(s), target, 1024, 1) != 0)
        return 1;
    return c.func_800DA1D4(@ptrCast(s));
}
