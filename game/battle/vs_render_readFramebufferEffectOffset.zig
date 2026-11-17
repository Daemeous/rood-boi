//! Copies the vs_render_framebufferEffectOffset block into the caller's buffer (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_readFramebufferEffectOffset (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the global vs_render_framebufferEffectOffset block into the caller's struct.
pub fn vs_render_readFramebufferEffectOffset(arg0: ?*v.vs_render_FramebufferEffectTranslation) callconv(.c) void {
    arg0.?.* = v.g.vs_render_framebufferEffectOffset().*;
}
