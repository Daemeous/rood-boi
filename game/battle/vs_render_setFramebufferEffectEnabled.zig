//! Arms the vs_render_framebufferEffectState state of the battle transition's wipe (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");

/// orig: vs_render_setFramebufferEffectEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets state 1, or state 3 when `arg0` is zero. (The original also took a heap block of packets for the wipe on the first call; the wipe
/// is items, and nothing read the block.)
pub fn vs_render_setFramebufferEffectEnabled(arg0: c_int) callconv(.c) void {
    v.g.vs_render_framebufferEffectState().* = if (arg0 != 0) 1 else 3;
}
