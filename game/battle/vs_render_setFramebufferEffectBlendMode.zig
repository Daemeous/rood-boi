//! Stores its argument in the battle global vs_render_framebufferEffectBlendMode (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectBlendMode (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores its argument in vs_render_framebufferEffectBlendMode.
pub fn vs_render_setFramebufferEffectBlendMode(arg0: c_int) callconv(.c) void {
    v.g.vs_render_framebufferEffectBlendMode().* = arg0;
}
