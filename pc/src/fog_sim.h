/* Interactive fog (prototype, off by default): a fog volume with `sim=1`
 * carries a top-down 2D density field that Ashley and the enemies push
 * around (pc/docs/plans/2026-10-13-interactive-fog.md). The field is a
 * multiplier on the volume's density (1: at rest), advected by a cheap
 * velocity field (semi-Lagrangian), diffused, damped, and relaxing back to
 * rest; each actor inside the volume's footprint clears a soft hole around
 * its feet, pushes the fog along its motion (a wake) and piles a little of
 * what it displaced into a ring. Stepped by vsyncs (deterministic under the
 * RPC's stepping), on the CPU; the medium record carries one simulated
 * volume's field (atmosphere.h PC_MEDIUM_SIM; medium.glsl medium_sim). */
#ifndef ROOD_FOG_SIM_H
#define ROOD_FOG_SIM_H

#define PC_FOG_SIM_GRID 32 /* cells per side */

namespace rood_fog_sim {
/* The simulated volume's frame (atmosphere.cpp's Volume, medium.glsl's turn
 * into its frame). */
struct Frame {
    float pos[3], half[3], yaw_cos, yaw_sin;
    float strength; /* 0..2: how much an actor clears (1: a clear hole at its feet) */
    float recover; /* seconds to settle back to rest */
    float wind[3]; /* the room's fog wind (world units per second) */
};
/* Advance the field of volume `id` to vsync `vsync` (a new id or a big jump
 * restarts it at rest). Actor positions come from the game each step. */
void step(const char* id, const Frame& frame, unsigned vsync);
/* The field, PC_FOG_SIM_GRID^2 multipliers (row by row in the volume's z,
 * x within a row); null when no volume is simulated. */
const float* field(const char* id);
/* Back to rest (RPC environment {sim_reset}). */
void reset();
/* RPC: the field's least and mean value and the value under Ashley (-1
 * outside), the actors it saw on its last step. */
void stats(float* least, float* mean, float* at_player, int* actors);
} // namespace rood_fog_sim

#endif
