//! Reads the battle global vs_render_framebufferEffectRoll (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_readFramebufferEffectRoll (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the 16-bit value of the global vs_render_framebufferEffectRoll.
pub fn vs_render_readFramebufferEffectRoll() callconv(.c) c_short {
    return v.g.vs_render_framebufferEffectRoll().*;
}
