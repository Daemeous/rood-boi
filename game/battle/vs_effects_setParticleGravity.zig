//! Copies a 16-byte block into the global vs_effects_particleGravity (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_setParticleGravity (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Copies the block at `src` over the global vs_effects_particleGravity, all 16 bytes.
pub fn vs_effects_setParticleGravity(src: ?*v.D_800F5620_t) callconv(.c) void {
    v.g.vs_effects_particleGravity().* = src.?.*;
}
