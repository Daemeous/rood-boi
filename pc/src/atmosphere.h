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
/* 163: the medium's texture (pc_medium_texture: the interactive fog's
 * fields, the detail noise): x the fields' cells per side, y the texture's
 * width and height, z its depth, w 1 (bound) */
#define PC_MEDIUM_SIM (PC_MEDIUM_FLOOR_DATA + PC_MEDIUM_FLOOR_GRID * PC_MEDIUM_FLOOR_GRID / 8)
#define PC_MEDIUM_DETAIL (PC_MEDIUM_SIM + 1) /* 164: x 1 / the detail noise's period (world units); yzw the main light's direction (self-shadowing) */
/* 165 (the shaders' medium_[] arrays have this size) */
#define PC_MEDIUM_VEC4S (PC_MEDIUM_DETAIL + 1)

/* The medium's texture, one RGBA8 3D texture every pass that samples the
 * medium binds (medium.glsl MEDIUM_TEX): PC_MEDIUM_TEX_SIZE x
 * PC_MEDIUM_TEX_SIZE x PC_MEDIUM_TEX_DEPTH texels, x fastest. Slices
 * 0..PC_MEDIUM_TEX_NOISE-1 are a tileable 3D detail noise (64 texels a
 * period, its first row, column and slice repeated at 64 so trilinear
 * filtering with clamping tiles; r the noise, g the same half a period
 * over); then one slice per simulated volume, its field (fog_sim.h,
 * PC_FOG_SIM_GRID^2 in the corner): r density / 2, g b its velocity in
 * world x, z (128 + v / 160 x 127 world units per second). */
#define PC_MEDIUM_TEX_SIZE 65
#define PC_MEDIUM_TEX_NOISE 65
#define PC_MEDIUM_TEX_LAYERS 4 /* fog_sim.h PC_FOG_SIM_MAX */
#define PC_MEDIUM_TEX_DEPTH (PC_MEDIUM_TEX_NOISE + PC_MEDIUM_TEX_LAYERS)

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
/* The interactive fog's global weights per source (settings keys
 * fog_disturb_player, fog_disturb_actors, fog_disturb_attacks,
 * fog_disturb_spells: 1, 1, 0, 0; 0 off), the most simulated volumes per
 * room (fog_sim_budget 4). A volume's disturb_* fields override them. */
extern float pc_fog_disturb[4];
extern int pc_fog_sim_budget;
/* The medium's texture (above): its texels, and a serial that changes when
 * they do (renderers upload the field slices then). */
const unsigned char* pc_medium_texture(unsigned* serial);
/* Once per game vsync (boot_runtime.c pc_tick): the interactive fog steps. */
void pc_fog_vsync(void);
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
