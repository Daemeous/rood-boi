//! Fills three words of the caller's block from the global at vs_render_framebufferEffectScale (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_readFramebufferEffectScale (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Writes the value of vs_render_framebufferEffectScale to the first two words of arg0 and zero to the third.
pub fn vs_render_readFramebufferEffectScale(arg0: [*c]c_int) callconv(.c) void {
    // The global is read again after the first store: arg0 may alias it, as in the object.
    arg0[0] = v.g.vs_render_framebufferEffectScale()[0];
    arg0[1] = v.g.vs_render_framebufferEffectScale()[0];
    arg0[2] = 0;
}
