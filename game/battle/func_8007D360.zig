//! Clears the battle's per-frame render and colour state (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: func_8007D360 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Resets the battle's scalar, colour and vector state to their defaults.
pub fn func_8007D360() callconv(.c) void {
    v.g.vs_render_framebufferEffectState().* = 0;
    v.g.vs_render_framebufferEffectBlendMode().* = 0;
    v.g.D_800F1A20().* = 0;
    v.g.vs_render_framebufferEffectRotation()[0] = 0;
    v.g.vs_render_framebufferEffectRotation()[1] = 0;
    v.g.vs_render_framebufferEffectRotation()[2] = 0;
    v.g.vs_render_framebufferEffectScale()[0] = 0x1000;
    v.g.vs_render_framebufferEffectScale()[1] = 0x1000;
    v.g.vs_render_framebufferEffectScale()[2] = 0;
    v.g.vs_render_framebufferEffectTint().r0 = 0x80;
    v.g.vs_render_framebufferEffectTint().g0 = 0x80;
    v.g.vs_render_framebufferEffectTint().b0 = 0x80;
    v.g.D_800F1A9C()[0] = 0x80;
    v.g.D_800F1A9C()[1] = 0x80;
    v.g.D_800F1A9C()[2] = 0x80;
    v.g.vs_render_framebufferEffectOffset().x = 0;
    v.g.vs_render_framebufferEffectOffset().y = 0;
    v.g.vs_render_framebufferEffectOffset().unk8 = 0;
    v.g.vs_render_framebufferEffectDepth().* = 0;
    v.g.vs_render_framebufferEffectUsesPreviousFrame().* = 0;
}
