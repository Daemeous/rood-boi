//! Returns the battle global vs_render_framebufferEffectState (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_framebufferEffectStateCode (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the current value of the battle global vs_render_framebufferEffectState.
pub fn vs_render_framebufferEffectStateCode() callconv(.c) c_int {
    return v.g.vs_render_framebufferEffectState().*;
}
