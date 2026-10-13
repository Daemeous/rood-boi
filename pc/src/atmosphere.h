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
#define PC_MEDIUM_VEC4S (8 + 5 * PC_MEDIUM_VOLUMES)

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
/* The haze froxel cell in display pixels (settings key haze_cell, from
 * the lighting quality; Vulkan's traced haze). */
extern int pc_haze_cell;
/* Debug view: the haze's light alone (RPC graphics {haze_debug}). */
extern int pc_haze_debug;
#ifdef __cplusplus
}
#endif
#endif
