//! Clears a pending flag in vs_actors_sequenceTexturePagesOneBased, after releasing it through vs_actors_restartsTextureFrames
//! (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_content_releaseSequenceEffectTexture (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Releases entry arg0 of vs_actors_sequenceTexturePagesOneBased and clears it; returns -3 if the release call fails.
pub fn vs_content_releaseSequenceEffectTexture(arg0: c_int) callconv(.c) c_int {
    if (psx.at(v.g.vs_actors_sequenceTexturePagesOneBased(), arg0).* != 0) {
        c.vs_actors_restartsTextureFrames(arg0 -% 1);
        // The C reads the entry again after the call, so the argument is re-read here.
        if (c.vs_render_retireTexturePages(psx.conv(c_uint, psx.at(v.g.vs_actors_sequenceTexturePagesOneBased(), arg0).*), 1, 4) == 0) {
            return -3;
        }
        psx.at(v.g.vs_actors_sequenceTexturePagesOneBased(), arg0).* = 0;
    }
    return 0;
}
