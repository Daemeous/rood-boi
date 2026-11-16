//! Forwards its argument to vs_content_releaseSequenceEffectTexture (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_actors_releaseSequenceEffectTextures (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Forwards its argument to vs_content_releaseSequenceEffectTexture.
pub fn vs_actors_releaseSequenceEffectTextures(arg0: c_int) callconv(.c) void {
    _ = c.vs_content_releaseSequenceEffectTexture(arg0);
}
