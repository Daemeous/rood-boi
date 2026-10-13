/* The room's participating medium: the uniform haze, the ground fog and the
 * fog volumes (pc/src/atmosphere.cpp; lighting/README.md "Environment" and
 * "Fog volumes"; rendering: pc/docs/Vulkan_Renderer.md "Haze, ground fog
 * and sun shafts").
 *
 * Per room, from the lighting layers: the haze's density is the room
 * setting `haze_density`, the ground fog is `env fog`, the volumes are
 * `fog <id>` items. Renderers read one packed record (pc_medium_gpu_get),
 * PC_MEDIUM_VEC4S vec4s that pc/shaders/vulkan/medium.glsl reads through
 * its MEDIUM(i) accessor. World space, y down. */
#ifndef ROOD_ATMOSPHERE_H
#define ROOD_ATMOSPHERE_H

#define PC_MEDIUM_VOLUMES 16
#define PC_MEDIUM_LIMITS (8 + 5 * PC_MEDIUM_VOLUMES) /* 88: medium.glsl MEDIUM_LIMITS */
#define PC_MEDIUM_FLOOR (PC_MEDIUM_LIMITS + 1) /* the ground fog's floor grid (2 vec4s, then its heights) */
#define PC_MEDIUM_FLOOR_GRID 24 /* cells per side (scene_capture.h PC_SCENE_FLOOR_GRID) */
#define PC_MEDIUM_FLOOR_DATA (PC_MEDIUM_FLOOR + 2)
#define PC_MEDIUM_SIM (PC_MEDIUM_FLOOR_DATA + PC_MEDIUM_FLOOR_GRID * PC_MEDIUM_FLOOR_GRID / 8) /* 163: fog_sim.h */
#define PC_MEDIUM_SIM_DATA (PC_MEDIUM_SIM + 1)
/* 250 (the shaders' medium_[] arrays have this size) */
#define PC_MEDIUM_VEC4S (PC_MEDIUM_SIM_DATA + (PC_FOG_SIM_CELLS + 11) / 12)
#define PC_FOG_SIM_CELLS (32 * 32) /* fog_sim.h PC_FOG_SIM_GRID squared */

typedef struct {
    float v[PC_MEDIUM_VEC4S][4]; /* layout: medium.glsl */
} pc_medium_gpu;

#ifdef __cplusplus
extern "C" {
#endif
/* This frame's medium with the given uniform haze density (the caller's,
 * from the lighting quality: pc_lighting_traced_haze_density or
 * pc_lighting_low_haze_density) and the room's bounds. The ground fog and
 * the volumes come with it unless the haze quality is off. Returns 1 when
 * anything is in it (else the record is zeros: no medium). */
int pc_medium_gpu_get(pc_medium_gpu* out, float haze_density, const float room_lo[3], const float room_hi[3]);
/* Whether the room has a ground fog or volumes (so the haze passes run
 * even with the haze density at 0). */
int pc_medium_has_fog(void);
/* A hash of what the medium's lighting depends on, besides time: history
 * resets when it changes. */
unsigned pc_medium_hash(void);
/* The global switch for the ground fog and volumes (settings key `fog`). */
extern int pc_fog_global;
/* The atmosphere's limits (room settings haze_max_opacity, haze_max_light,
 * haze_shafts; atmosphere.cpp): the most of the scene the haze, fog and
 * volumes may hide along any ray, the most light they may scatter toward
 * the eye (luminance), how much darker the sun's shadowed air is than its
 * lit air (the shafts' contrast), forward scattering's peak (the phase
 * function toward the moon; haze_max_forward). */
extern float pc_haze_max_opacity, pc_haze_max_light, pc_haze_shafts, pc_haze_max_forward;
/* The haze froxel cell in display pixels (settings key haze_cell, from
 * the lighting quality; Vulkan's traced haze). */
extern int pc_haze_cell;
/* Debug view: the haze's light alone (RPC graphics {haze_debug}). */
extern int pc_haze_debug;
#ifdef __cplusplus
}
#endif
#endif
