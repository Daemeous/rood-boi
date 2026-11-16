//! Returns the battle global vs_render_framebufferEffectUsesPreviousFrame (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_readFramebufferEffectPreviousFrame (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the value of the battle global vs_render_framebufferEffectUsesPreviousFrame.
pub fn vs_render_readFramebufferEffectPreviousFrame() callconv(.c) c_int {
    return v.g.vs_render_framebufferEffectUsesPreviousFrame().*;
}
