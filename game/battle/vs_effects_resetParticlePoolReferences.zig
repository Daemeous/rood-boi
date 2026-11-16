//! Clears the battle global vs_effects_particlePoolReferences (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_resetParticlePoolReferences (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears vs_effects_particlePoolReferences.
pub fn vs_effects_resetParticlePoolReferences() callconv(.c) void {
    v.g.vs_effects_particlePoolReferences().* = 0;
}
