//! Battle forwarder: passes its five arguments on to vs_render_requestBackgroundGradientTransform (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_render_transformBackgroundColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Forwards its five arguments to vs_render_requestBackgroundGradientTransform, sign-extending the three shorts to int.
pub fn vs_render_transformBackgroundColors(arg0: c_int, arg1: c_int, arg2: c_short, arg3: c_short, arg4: c_short) callconv(.c) void {
    c.vs_render_requestBackgroundGradientTransform(arg0, arg1, @as(c_int, arg2), @as(c_int, arg3), @as(c_int, arg4));
}
