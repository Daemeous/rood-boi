//! Stores its argument in the battle global vs_render_framebufferEffectRoll (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectRoll (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Stores its argument in vs_render_framebufferEffectRoll, truncated to a halfword.
pub fn vs_render_setFramebufferEffectRoll(arg0: c_int) callconv(.c) void {
    v.g.vs_render_framebufferEffectRoll().* = psx.conv(i16, arg0);
}
