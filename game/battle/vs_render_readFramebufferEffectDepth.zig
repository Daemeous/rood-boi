//! Returns the battle global vs_render_framebufferEffectDepth (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_render_readFramebufferEffectDepth (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the value of vs_render_framebufferEffectDepth as an int.
pub fn vs_render_readFramebufferEffectDepth() callconv(.c) c_int {
    return psx.conv(c_int, v.g.vs_render_framebufferEffectDepth().*);
}
