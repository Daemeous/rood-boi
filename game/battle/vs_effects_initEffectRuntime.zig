//! Battle unit 5BF94: runs three battle steps in turn (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const c = @import("c_5BF94");

/// orig: vs_effects_initEffectRuntime (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs the battle steps vs_effects_resetEffectInstances, vs_effects_initializeEffectResources and vs_effects_resetParticlePoolReferences, in that order.
pub fn vs_effects_initEffectRuntime() callconv(.c) void {
    c.vs_effects_resetEffectInstances();
    c.vs_effects_initializeEffectResources();
    c.vs_effects_resetParticlePoolReferences();
}
