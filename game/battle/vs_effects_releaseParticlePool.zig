//! Counts down the battle's text-buffer timer and frees the buffer when it expires (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_releaseParticlePool (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Decrements vs_effects_particlePoolReferences; on reaching 1 frees the buffer in vs_effects_particlePool, clears it and the counter.
pub fn vs_effects_releaseParticlePool() callconv(.c) void {
    if (v.g.vs_effects_particlePoolReferences().* == 1) {
        if (v.g.vs_effects_particlePool().*) |buf| {
            c.vs_main_freeHeapR(buf);
            psx.put(v.g.vs_effects_particlePool(), null);
        }
        v.g.vs_effects_particlePoolReferences().* = 0;
        return;
    }
    v.g.vs_effects_particlePoolReferences().* -%= 1;
}
