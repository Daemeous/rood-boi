//! Stores its argument in the global vs_render_framebufferEffectUsesPreviousFrame (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectPreviousFrame (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores arg0 in the global vs_render_framebufferEffectUsesPreviousFrame.
pub fn vs_render_setFramebufferEffectPreviousFrame(arg0: c_int) callconv(.c) void {
    v.g.vs_render_framebufferEffectUsesPreviousFrame().* = arg0;
}
