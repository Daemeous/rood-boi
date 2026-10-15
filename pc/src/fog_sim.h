/* Interactive fog (pc/docs/plans/2026-10-13-interactive-fog.md): a fog
 * volume with `sim=1` carries a top-down 2D density field that the room's
 * characters, their attacks and spells push around. The field is a
 * multiplier on the volume's density (1: at rest), advected by a cheap
 * velocity field (semi-Lagrangian), diffused, damped, and relaxing back to
 * rest; each disturbance source clears a soft hole (an actor's footprint,
 * from its own size), pushes the fog along its motion (a wake) and piles a
 * little of what it displaced into a ring; attacks and spells stamp swirls
 * and bursts. Every source has its own weight (0: off), global (settings
 * fog_disturb_*) or per volume (disturb_* fields).
 *
 * Stepped once per game vsync (pc_tick -> atmosphere.cpp pc_fog_vsync: the
 * same with any frame rate, deterministic under the RPC's stepping), on
 * the CPU; a volume only ticks while a source is in or near it or its field
 * has not settled. Up to PC_FOG_SIM_MAX volumes of a room simulate; their
 * fields are layers of the medium's texture (atmosphere.h
 * pc_medium_texture; medium.glsl medium_sim). The whole state is saved in
 * save states (snapshot.c, pc_fog_state_*). */
#ifndef ROOD_FOG_SIM_H
#define ROOD_FOG_SIM_H

#include <string>

#define PC_FOG_SIM_GRID 64 /* cells per side */
#define PC_FOG_SIM_MAX 4 /* simulated volumes per room (atmosphere.h PC_MEDIUM_TEX_LAYERS) */

namespace rood_fog_sim {
/* Disturbance sources (each with a weight). GHOST: the editor's preview
 * actor (and RPC environment {ghost}). */
enum Source { PLAYER, ACTORS, ATTACKS, SPELLS, GHOST, SOURCES };
/* Stamp shapes: BURST clears a disc and blows outward (a spell), RING
 * pushes outward from a thin ring (a landing), SWING clears an arc in front
 * and swirls it (a weapon swing), VORTEX swirls a disc in place. */
enum Shape { BURST, RING, SWING, VORTEX, SHAPES };
const char* shape_name(int shape);
int shape_from_name(const char* name); /* -1: unknown */

/* A simulated volume's frame and tuning (atmosphere.cpp's Volume; medium.glsl's turn into its frame). */
struct Frame {
    float pos[3], half[3], yaw_cos, yaw_sin;
    float strength; /* 0..2: how much a source clears (1: a clear hole at an actor's feet) */
    float recover; /* seconds to settle back to rest */
    float footprint; /* scale on every source's size */
    float trail; /* 0..3: how much of a source's motion the fog takes and keeps (its wake) */
    float swirl; /* 0..4: vorticity confinement (curls into eddies) with an incompressible flow (0: off) */
    float wind[3]; /* the wind that carries it (world units per second): the room's x sim_wind */
    float weight[SOURCES]; /* the effective weights (volume override, else the global setting) */
};
/* A stamp from the game (an attack, a spell) or the RPC/editor: lasts
 * `frames` vsyncs from when it is emitted. */
struct Event {
    int source = ATTACKS, shape = SWING;
    float pos[3] = { 0, 0, 0 };
    float dir[2] = { 0, 1 }; /* x, z: the facing (SWING) */
    float radius = 120; /* world units */
    float strength = 1;
    int frames = 10;
};

/* The fog's clock: the game's vsync plus the ticks the editor's preview
 * added while the game was paused (saved with the rest). */
unsigned clock(unsigned game_vsync);
void preview_tick();
/* Once per vsync, before the volumes step: the actors' positions, sizes and
 * velocities, the game's attack and spell events. */
void begin_vsync(unsigned vsync);
/* Advance volume `id` to `vsync` (a new id or a big jump starts it at rest).
 * Returns whether its field changed. */
bool step(const char* id, const Frame& frame, unsigned vsync);
/* After the volumes: expire events, forget volumes not stepped for a while. */
void end_vsync(unsigned vsync);
/* The field, PC_FOG_SIM_GRID^2 multipliers (row by row in the volume's z,
 * x within a row); null when the volume is not simulated. */
const float* field(const char* id);
/* Its velocity in world units per second, world x and z per cell (the
 * same layout); null when the volume is not simulated. */
void velocity_world(const char* id, const Frame& frame, float* out_xz);
void emit(const Event& e);
/* The preview actor (editor, RPC): on at pos, or off. */
void ghost(bool on, const float pos[3]);
/* Back to rest (RPC environment {sim_reset}). */
void reset();
struct Stats {
    float least = 1, mean = 1, at_player = -1;
    int actors = 0; /* sources that touched it on its last step */
    bool awake = false;
    unsigned steps = 0; /* steps taken (ticks) */
    float ms = 0; /* the CPU time of its last vsync's steps (milliseconds) */
};
bool stats(const char* id, Stats* out);
/* Counters of the game's events seen (RPC): attacks, spells. */
void event_counts(unsigned* attacks, unsigned* spells);
/* RPC: the sources seen this vsync (characters and the ghost), the battle
 * action's phase (0 none) and kind (vs_main_actions type). */
void sources(int* bodies, int* phase, int* type);
/* Save states: the whole state as bytes, and back. */
std::string state_save();
bool state_load(const void* data, size_t size);
} // namespace rood_fog_sim

#endif
