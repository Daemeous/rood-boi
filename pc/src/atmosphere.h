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
/* (PC_MEDIUM_FLOOR + 1).w: the record's switches. GRID: the floor grid is
 * packed (the ground fog lies on it; a cell's 12-bit step its height, 0..4095,
 * as before the interior); INTERIOR: the ground fog keeps to its cells with a
 * floor, the haze to them and to the air above the cells without one (its
 * share there PC_MEDIUM_AIR.x), never under the floor (a cell's step is then
 * its height, 0..PC_MEDIUM_FLOOR_STEPS_INTERIOR, times 2, + PC_MEDIUM_FLOOR_NONE
 * where it has no floor); VOID: present passes keep the room's air off void
 * pixels past where their ray leaves the interior (record 7.w: the brightest
 * black, (level + 0.5) / 255; 6.w: haze_under_floor). */
#define PC_MEDIUM_GRID 1
#define PC_MEDIUM_INTERIOR 2
#define PC_MEDIUM_VOID 4
#define PC_MEDIUM_FLOOR_NONE 1
#define PC_MEDIUM_FLOOR_STEPS_INTERIOR 2047
/* 163: the medium's texture (pc_medium_texture: the interactive fog's
 * fields, the detail noise): x the fields' cells per side, y the texture's
 * width and height, z its depth, w 1 (bound) */
#define PC_MEDIUM_SIM (PC_MEDIUM_FLOOR_DATA + PC_MEDIUM_FLOOR_GRID * PC_MEDIUM_FLOOR_GRID / 8)
#define PC_MEDIUM_DETAIL (PC_MEDIUM_SIM + 1) /* 164: x 1 / the detail noise's period (world units); yzw the main light's direction (self-shadowing) */
/* 165: the room's air (haze_interior): x the haze's share above cells without a floor (haze_outside),
 * y z the interior's edge (smoothstep of the cells' bilinear floor share; haze_interior_edge), w the room's
 * lowest floor (world y, y down: the largest height of the cells with a floor) */
#define PC_MEDIUM_AIR (PC_MEDIUM_DETAIL + 1)
/* 166: the detail noise's motion: x its drift as a multiple of the wind (settings fog_detail_wind), y its rise
 * in noise periods per second (fog_detail_rise), z the flow map's cycle in seconds (fog_flow_cycle) */
#define PC_MEDIUM_FLOW (PC_MEDIUM_AIR + 1)
/* 167 (the shaders' medium_[] arrays have this size) */
#define PC_MEDIUM_VEC4S (PC_MEDIUM_FLOW + 1)

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

/* The hand-placed fog volumes' own renderer (pc/shaders/vulkan/
 * fog_volume.glsl; pc/docs/plans/2026-10-14-fog-volume-renderer.md): the
 * first PC_FOGVOL_MAX enabled volumes of the room are marched per pixel by
 * it (Vulkan: vk_fogvol.inc, OpenGL: gl_present.c) instead of going
 * through the haze's froxels, lit by up to PC_FOGVOL_LIGHTS lights each
 * (lights, glows), the sun or moon and an ambient term, and are exempt from
 * the room's haze limits (their own opacity and brightness instead). */
#define PC_FOGVOL_MAX 8
#define PC_FOGVOL_LIGHTS 6
#define PC_FOGVOL_STRIDE (4 + 3 * PC_FOGVOL_LIGHTS)
#define PC_FOGVOL_VEC4S (3 + PC_FOGVOL_MAX * PC_FOGVOL_STRIDE) /* 179: fog_volume.glsl FV_VEC4S */
typedef struct {
    float v[PC_FOGVOL_VEC4S][4]; /* layout: fog_volume.glsl */
} pc_fogvol_gpu;
/* What the renderers' history depends on per volume: an identity per light
 * slot (0..5 its lights, 6 the sun, 7 the ambient term; 0 none) and of the
 * volume's shape (a change restarts that slot's or volume's history). */
typedef struct {
    unsigned keys[PC_FOGVOL_MAX][8];
    unsigned shape[PC_FOGVOL_MAX];
    int count;
} pc_fogvol_info;

#ifdef __cplusplus
extern "C" {
#endif
/* This frame's medium with the given uniform haze density (the caller's,
 * from the lighting quality: pc_lighting_traced_haze_density or
 * pc_lighting_low_haze_density) and the room's bounds. The ground fog and
 * the volumes come with it unless the haze quality is off. Returns 1 when
 * anything is in it (else the record is zeros: no medium). */
int pc_medium_gpu_get(pc_medium_gpu* out, float haze_density, const float room_lo[3], const float room_hi[3]);
/* The fog volumes' renderer's records: `medium` the room's medium (as
 * pc_medium_gpu_get, its volumes 0..count-1 the renderer's), `out` their
 * lighting (fog_volume.glsl), `info` (may be NULL) what their history
 * depends on. Returns the number of volumes it draws (0: none, or the
 * renderer is off: they are in the haze). */
int pc_fogvol_gpu_get(pc_medium_gpu* medium, pc_fogvol_gpu* out, pc_fogvol_info* info, const float room_lo[3],
    const float room_hi[3]);
/* Settings (project_settings.cpp): fog_volume_render (1: the volumes' own
 * renderer; 0: in the haze as before), fog_volume_resolution (its march
 * pixels per internal pixel, Vulkan: 0.25..1), fog_volume_steps (samples per
 * ray; OpenGL fog_volume_steps_gl), fog_volume_history (the least weight of
 * a new frame in its temporal accumulation), fog_volume_ambient (scale on
 * the ambient term), fog_volume_glow (the glows' light in the volumes),
 * fog_volume_scattering (the multiple-scattering octave), fog_volume_halo (extra
 * light in the fog near a light: a fire glows in its smoke), fog_volume_shadows
 * (1: the scene shadows the volumes' lights with ray queries, Vulkan RT);
 * the debug view (RPC environment {fog_volumes: {debug}}: 1 their light
 * alone, 2 their transmittance). */
extern int pc_fog_volume_render, pc_fog_volume_steps, pc_fog_volume_steps_gl, pc_fog_volume_debug, pc_fog_volume_shadows;
extern float pc_fog_volume_resolution, pc_fog_volume_history, pc_fog_volume_ambient, pc_fog_volume_glow,
    pc_fog_volume_scattering, pc_fog_volume_halo;
/* The fog's detail noise in motion (record PC_MEDIUM_FLOW; settings fog_detail_wind, fog_detail_rise,
 * fog_flow_cycle; RPC environment {detail_wind, detail_rise, flow_cycle}): its drift as a multiple of the
 * room's wind (1.3), its rise in noise periods per second (0.01), the flow map's cycle in seconds (2.5). */
extern float pc_fog_detail_wind, pc_fog_detail_rise, pc_fog_flow_cycle;
/* The glows this frame for the fog volumes' lights (ptgi.c: updated once a
 * present): position + radius of influence, colour + emitting radius, a
 * column's half axis + radius (0: a sphere). Returns their count. */
int pc_fog_glows(float (*pos)[4], float (*col)[4], float (*shape)[4], int max);
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
/* Where the room's air is (room settings; pc/docs/Vulkan_Renderer.md "The
 * room's interior and the void"): haze_interior (1: the ground fog keeps to
 * the floor grid's cells with a floor, not the room's bounds box, and the
 * haze never fills the void under the floor; 0 as before),
 * haze_interior_fill (holes up to twice this many cells across count as
 * floor; 0..4), haze_interior_slope (the steepest up-facing face, degrees,
 * that counts as floor), haze_interior_edge (the interior's soft edge, 0
 * hard .. 0.5 a cell wide each side), haze_outside (the uniform haze's share
 * above the cells without a floor: 1 keeps the air between an outside
 * camera and the room, as before; 0 confines the haze to the interior),
 * haze_under_floor (the haze fades out within this many world units under
 * the floor), haze_void (1: pixels that show the void around the room,
 * nothing drawn or a surface the PS1 draws black whatever its texture, keep
 * the room's air only up to where their ray leaves the interior; the fog
 * volumes are exempt), haze_void_level (the brightest vertex colour, of 255,
 * that still counts as that black). */
extern float pc_haze_interior, pc_haze_interior_fill, pc_haze_under_floor, pc_haze_void, pc_haze_void_level,
    pc_haze_outside, pc_haze_interior_edge, pc_haze_interior_slope;
/* The haze froxel cell in display pixels (settings key haze_cell, from
 * the lighting quality; Vulkan's traced haze). */
extern int pc_haze_cell;
/* Debug view: the haze's light alone (RPC graphics {haze_debug}). */
extern int pc_haze_debug;
#ifdef __cplusplus
}
#endif
#endif
