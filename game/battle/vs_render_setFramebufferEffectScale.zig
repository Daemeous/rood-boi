//! Copies the two words of a battle record into vs_render_framebufferEffectScale (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectScale (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the record's first two words into the global pair vs_render_framebufferEffectScale.
pub fn vs_render_setFramebufferEffectScale(arg0: ?*v.vs_render_FramebufferEffectTranslation) callconv(.c) void {
    const dst = v.g.vs_render_framebufferEffectScale();
    dst[0] = arg0.?.x;
    dst[1] = arg0.?.y;
}
