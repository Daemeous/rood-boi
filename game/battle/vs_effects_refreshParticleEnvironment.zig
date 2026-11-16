//! Battle unit 5BF94 wrapper that runs vs_effects_updateParticleGravityAndDrag and reports success (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_refreshParticleEnvironment (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs vs_effects_updateParticleGravityAndDrag and returns 1; the argument is unused.
pub fn vs_effects_refreshParticleEnvironment(_: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.vs_effects_updateParticleGravityAndDrag();
    return 1;
}
