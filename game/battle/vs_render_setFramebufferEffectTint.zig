//! Copies a P_CODE value into the global vs_render_framebufferEffectTint (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectTint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the P_CODE at `arg0` into the global vs_render_framebufferEffectTint.
pub fn vs_render_setFramebufferEffectTint(arg0: ?*v.P_CODE) callconv(.c) void {
    v.g.vs_render_framebufferEffectTint().* = arg0.?.*;
}
