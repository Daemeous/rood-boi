//! Clears two battle counters and frees the heap block held in vs_field_weatherParticles (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_releaseWeatherParticles (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Clears the counters at vs_field_weatherParticleCount and vs_field_weatherParticleCapacity, then frees and clears the heap block at vs_field_weatherParticles.
pub fn vs_field_releaseWeatherParticles() callconv(.c) void {
    v.g.vs_field_weatherParticleCount().* = 0;
    v.g.vs_field_weatherParticleCapacity().* = 0;
    if (v.g.vs_field_weatherParticles().*) |block| {
        c.vs_main_freeHeap(block);
    }
    v.g.vs_field_weatherParticles().* = null;
}
