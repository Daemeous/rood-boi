//! Copies the battle's shared P_CODE value into the caller's record (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_readFramebufferEffectTint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores the shared P_CODE value vs_render_framebufferEffectTint into `out`.
pub fn vs_render_readFramebufferEffectTint(out: ?*v.P_CODE) callconv(.c) void {
    const dst = out.?;
    dst.* = v.g.vs_render_framebufferEffectTint().*;
}
