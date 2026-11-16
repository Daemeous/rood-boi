//! Battle unit 146C: stores a small value in vs_render_framebufferEffectDepth (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectDepth (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores arg0 in vs_render_framebufferEffectDepth when it is below 0x800; otherwise leaves the global alone.
pub fn vs_render_setFramebufferEffectDepth(arg0: c_uint) callconv(.c) void {
    if (arg0 < 0x800) {
        v.g.vs_render_framebufferEffectDepth().* = arg0;
    }
}
