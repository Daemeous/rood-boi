//! Runs func_8007D360 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8007D3F8 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs func_8007D360. (The original first cleared the pointer to the wipe's packet block, vs_render_legacyFramebufferEffectPackets.)
pub fn func_8007D3F8() callconv(.c) void {
    c.func_8007D360();
}
