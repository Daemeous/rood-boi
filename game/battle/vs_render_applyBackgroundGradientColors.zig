//! Forwards a call to the battle helper vs_render_setBackgroundGradientColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_render_applyBackgroundGradientColors (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Passes its two arguments on to vs_render_setBackgroundGradientColors unchanged.
pub fn vs_render_applyBackgroundGradientColors(arg0: c_int, arg1: ?*v.vs_render_ColorRgb8) callconv(.c) void {
    c.vs_render_setBackgroundGradientColors(arg0, @ptrCast(arg1));
}
