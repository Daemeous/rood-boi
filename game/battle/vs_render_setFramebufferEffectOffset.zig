//! Copies a 16-byte block into the global vs_render_framebufferEffectOffset (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectOffset (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the whole vs_render_framebufferEffectOffset block from `src` into the global.
pub fn vs_render_setFramebufferEffectOffset(src: ?*v.vs_render_FramebufferEffectTranslation) callconv(.c) void {
    v.g.vs_render_framebufferEffectOffset().* = src.?.*;
}
