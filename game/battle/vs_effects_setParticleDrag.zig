//! Copies an int from a pointer into the global vs_effects_particleDrag (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_effects_setParticleDrag (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores the int that arg0 points to into vs_effects_particleDrag.
pub fn vs_effects_setParticleDrag(arg0: [*c]c_int) callconv(.c) void {
    v.g.vs_effects_particleDrag().* = arg0.*;
}
