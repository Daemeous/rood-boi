/* The present side's frame header (T1 R4a, pc/docs/plans/2026-11-12-t1-renderer-thread.md section 9): the game-owned values the presenter
 * (vk_present.c, post_vk.inc) reads, copied on the game thread when a present is requested (pc_present_header_fill, from
 * pc_window_frame) so the presenter reads only the copy (pc_ph()->name). A renderer thread then needs nothing of the game's state
 * for them. ROOD_PRESENT_CANARY=1 proves it: while the presenter runs, every live variable below holds poison (restored after), so a
 * read that bypassed the header shows as garbage, loudly and at once; the overlay's panel build runs outside it (it edits the live
 * settings, which is the game thread's work: R4c).
 *
 * Each entry: (field, the live variable). */
#ifndef PC_PRESENT_HEADER_H
#define PC_PRESENT_HEADER_H
#include "light_list.h"
#include "renderer_frame_state.h"
#include "post.h"
#include "ui/menu_light.h"
#include "environment.h"
#include "scene_capture.h"
#include "atmosphere.h"
#define PC_PH_INTS(X) \
    X(aa_downsample, pc_aa_downsample) \
    X(aa_geometry_edges, pc_aa_geometry_edges) \
    X(baked_shadows_active, pc_baked_shadows_active) \
    X(display_rgb24, pc_display_rgb24) \
    X(fog_volume_debug, pc_fog_volume_debug) \
    X(gi_debug, pc_gi_debug) \
    X(gi_enabled, pc_gi_enabled) \
    X(gl_aa, pc_gl_aa) \
    X(gl_aspect, pc_gl_aspect) \
    X(gl_filter, pc_gl_filter) \
    X(gl_show_depth, pc_gl_show_depth) \
    X(gl_view_mode, pc_gl_view_mode) \
    X(gte_widescreen, pc_gte_widescreen) \
    X(haze_debug, pc_haze_debug) \
    X(present_brightness, pc_present_brightness) \
    X(rt_shadows, pc_rt_shadows) \
    X(shadow_map_filter, pc_shadow_map_filter) \
    X(baked_shadows, pc_baked_shadows) \
    X(fog_steps, pc_fog_steps) \
    X(gi_bounces, pc_gi_bounces) \
    X(gi_denoise, pc_gi_denoise) \
    X(gi_rays, pc_gi_rays) \
    X(gi_trace_debug, pc_gi_trace_debug) \
    X(glow_shadows, pc_glow_shadows) \
    X(nrd_glow, pc_nrd_glow) \
    X(nrd_max_frames, pc_nrd_max_frames) \
    X(probe_budget, pc_probe_budget) \
    X(probe_rays, pc_probe_rays) \
    X(probe_show, pc_probe_show) \
    X(shadow_samples, pc_shadow_samples) \
    X(fog_volume_render, pc_fog_volume_render) \
    X(fog_volume_shadows, pc_fog_volume_shadows) \
    X(fog_volume_steps, pc_fog_volume_steps) \
    X(haze_cell, pc_haze_cell) \
    X(reflection_bounces, pc_reflection_bounces) \
    X(gi_method, pc_gi_method) \
    X(haze_quality, pc_haze_quality) \
    X(lights_enabled, pc_lights_enabled) \
    X(particles_untraced, pc_particles_untraced)
#define PC_PH_FLOATS(X) \
    X(aa_ssaa, pc_aa_ssaa) \
    X(ao_radius, pc_ao_radius) \
    X(ao_strength, pc_ao_strength) \
    X(baked_energy, pc_baked_energy) \
    X(bump_occlusion, pc_bump_occlusion) \
    X(bump_strength, pc_bump_strength) \
    X(env_specular, pc_env_specular) \
    X(fog_anisotropy, pc_fog_anisotropy) \
    X(fog_density, pc_fog_density) \
    X(gi_strength, pc_gi_strength) \
    X(light_size, pc_light_size) \
    X(lights_baked, pc_lights_baked) \
    X(normal_smoothing, pc_normal_smoothing) \
    X(relief_baked, pc_relief_baked) \
    X(relief_indirect, pc_relief_indirect) \
    X(relief_overhead, pc_relief_overhead) \
    X(relief_reach, pc_relief_reach) \
    X(roughness_bias, pc_roughness_bias) \
    X(shadow_softness, pc_shadow_softness) \
    X(specular_strength, pc_specular_strength) \
    X(baked_ambient, pc_baked_ambient) \
    X(contact_radius, pc_contact_radius) \
    X(emitter_light, pc_emitter_light) \
    X(fog_glow, pc_fog_glow) \
    X(gi_baked_bounce, pc_gi_baked_bounce) \
    X(gi_resolution, pc_gi_resolution) \
    X(haze_history, pc_haze_history) \
    X(probe_hysteresis, pc_probe_hysteresis) \
    X(probe_normal_bias, pc_probe_normal_bias) \
    X(probe_spacing, pc_probe_spacing) \
    X(probe_view_bias, pc_probe_view_bias) \
    X(shadow_history, pc_shadow_history) \
    X(shadow_motion_history, pc_shadow_motion_history) \
    X(fog_volume_history, pc_fog_volume_history) \
    X(fog_volume_resolution, pc_fog_volume_resolution)
#ifdef __cplusplus
extern "C" {
#endif
#define PC_EMITTERS_MAX 32
typedef struct {
#define PC_PH_INT_FIELD(f, live) int f;
#define PC_PH_FLOAT_FIELD(f, live) float f;
    PC_PH_INTS(PC_PH_INT_FIELD)
    PC_PH_FLOATS(PC_PH_FLOAT_FIELD)
#undef PC_PH_INT_FIELD
#undef PC_PH_FLOAT_FIELD
    int gl_inset[4];
    /* the scene (scene_capture.c): projection, camera and its inverse, the room's bounds */
    /* The GTE camera of the frame the game is building (no overrides): the picture's camera is the renderer's to decide while it presents (the
     * newest frame or an interpolated one: gpu_hw.c), so pc_ph_scene_camera_get applies that to this at the call, as the live one did. */
    int projection_ok, game_camera_ok;
    float game_r[3][3], game_t[3], game_h, game_ofx, game_ofy;
    float room_lo[3], room_hi[3];
    /* the lights (lights.cpp): the list in world space and the baked shadow's parameters */
    int light_count;
    pc_light_gpu lights[PC_LIGHTS_MAX];
    float baked_shadow[5];
    /* the menu (ui/): active, its backdrop, the model's light */
    int menu_active, menu_backdrop_ok, menu_light_ok;
    float menu_backdrop[4];
    pc_menu_light menu_light;
    /* the post profile for this present, photo capture and emissive flags, lighting quality's answers */
    pc_post_frame post;
    int photo_wanted, emissive_active, ao_traced, ssao[2];
    float low_haze, baked_scale, traced_haze;
    /* scene_capture.c's serials and the animated triangles' indices (the room's UV effects) */
    int scene_serial, scene_uv_serial, animated_count, animated_overflow;
    int animated[4096];
    /* every light of the list whatever the projection (the Vulkan trace's), and the full count */
    /* the last gather's keys and importance (lights.cpp pc_lights_output): the renderer's shadow pick reads them */
    int out_count;
    unsigned long long out_keys[PC_LIGHTS_MAX];
    float out_importance[PC_LIGHTS_MAX];
    unsigned lights_frame;
    int light_count_all;
    pc_light_gpu lights_all[PC_LIGHTS_MAX];
    /* the world lights the CPU-side GI gathers (lights.cpp pc_lights_world, at most 16) and the environment's record (environment.cpp) */
    int world_light_count, world_light_shadows[16], env_active;
    float world_lights[5][16][4];
    pc_env_gpu env;
    /* the atmosphere (atmosphere.cpp, environment.cpp, probe_overlay.cpp): the medium at the two hazes the presenter asks for (scene bounds), the
     * fog volumes, the change hashes, the probes' base lights; a call with other arguments falls back to the live function (a guard hit) */
    unsigned medium_hash, env_hash;
    int medium_traced_ok, medium_low_ok, fogvol_count, probe_light_count;
    pc_medium_gpu medium_traced, medium_low, fogvol_medium;
    pc_fogvol_gpu fogvol;
    pc_fogvol_info fogvol_info;
    float probe_lights[5][16][4];
    int probe_light_shadows[16];
    /* the emitters (ptgi.c pc_ptgi_emitters_gather: glows from the room's emissive triangles, the emissive lights and the effects' lists,
     * through glows.c's stable ids and smoothing and the editor's overrides): game-side state, gathered once at the fill */
    unsigned vsync_count;         /* pc_vsync_count(): the game's vsync this frame was requested at (the interpolation's clock) */
    int vsync;                    /* pc_vsync_get(): the swap waits for the display (boot_window.c: the setting, display pacing)  */
    int window_px_w, window_px_h; /* the window's size in pixels (SDL: the game thread's), what a swapchain made in this present is sized to */
    pc_renderer_frame_state shown; /* the renderer's say on the picture shown (renderer_frame_state.h), asked at the fill */
    int emitter_count;
    float emitter_pos[PC_EMITTERS_MAX][4], emitter_col[PC_EMITTERS_MAX][4], emitter_shape[PC_EMITTERS_MAX][4], emitter_shadow[PC_EMITTERS_MAX][4];
} pc_present_header;
/* The headers: a ring of slots (the game thread fills the next free one at a request, the renderer presents from it and gives it back), so a
 * request is a value that carries its header and the game thread may be filling the next while the renderer presents. pc_ph() is the header of
 * the present in progress on the renderer thread (pc_present_inside), and the one last filled anywhere else (the fill's own reads, init code). */
#define PC_HEADER_SLOTS 3
extern pc_present_header pc_present_header_slots[PC_HEADER_SLOTS];
extern pc_present_header* pc_present_header_cur;    /* the renderer's: the slot of the present in progress */
extern pc_present_header* pc_present_header_target; /* the game thread's: the slot last filled */
extern __thread int pc_present_inside;
static inline pc_present_header* pc_present_header_ptr(void) { return pc_present_inside ? pc_present_header_cur : pc_present_header_target; }
#define pc_present_header_now (*pc_present_header_ptr())
#define pc_ph() pc_present_header_ptr()
int pc_present_header_fill(void);          /* the game thread, at a present request: a slot (waits for one to be free), filled; its index */
void pc_present_header_use(int slot);      /* the renderer, as a request begins: the present reads this slot (-1: keep the last) */
void pc_present_header_release(int slot);  /* ... and as it ends: the slot is free to fill again (-1: none) */
/* The header's emitters, gathered on first use during a fill (the atmosphere's and the particles' glows ask for them first) and at its end;
 * inside a present they are the header's. Outside both (nothing today) they are gathered live. */
void pc_present_emitters_ensure(void);
/* The scene getters the GI code calls: inside a present the header's, outside (the editors, RPC dumps) the live ones. */
void pc_ph_scene_bounds(float lo[3], float hi[3]);
/* The window's pixels: the header's inside a present, SDL's outside it (vk_present.c init); 0 0 when unknown. */
void pc_ph_window_pixels(int* w, int* h);
/* The shown picture's state: the header's during the fill and inside a present, the renderer's own otherwise. */
int pc_ph_present_world(void);
int pc_ph_present_menu_model(void);
float pc_ph_present_fade(float rect[4]);
const float* pc_ph_emissives(int* count, const void** surfaces);
const uint32_t* pc_ph_emissive_sources(void);
unsigned pc_ph_vsync(void); /* the vsync the present in progress belongs to (the header's), else the live count */
int pc_ph_scene_camera_get(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy);
int pc_ph_scene_camera_inverse(float inverse[3][3]);
int pc_ph_scene_projection(float* h, float* ofx, float* ofy);
int pc_ph_scene_serial(void);
int pc_ph_scene_uv_serial(void);
const int* pc_ph_scene_animated(int* count);
int pc_ph_lights_list(pc_light_gpu* out, int max);
/* Shadow maps for the most important casters (the renderer's: its hysteresis state is its own; the inputs are the header's). */
void pc_ph_lights_shadow_pick(int count, int* wants, int slots);
/* The captured room's triangles: inside a present the renderer's copy (copied when the capture's serial changes, its animated triangles'
 * UVs when the UV serial does; ROOD_PRESENT_CANARY_TRIANGLES=1 compares it with the live array at every fill and reports a difference). */
const scene_triangle* pc_ph_scene_triangle_data(int* count);
int pc_ph_lights_world(float position[][4], float color[][4], float direction[][4], float extent_right[][4], float extent_up[][4],
    int shadows[], int max);
int pc_ph_env_gpu_get(pc_env_gpu* out);
int pc_ph_medium_gpu_get(pc_medium_gpu* out, float haze_density, const float room_lo[3], const float room_hi[3]);
int pc_ph_fogvol_gpu_get(pc_medium_gpu* medium, pc_fogvol_gpu* out, pc_fogvol_info* info, const float room_lo[3], const float room_hi[3]);
unsigned pc_ph_medium_hash(void);
unsigned pc_ph_env_hash(void);
int pc_ph_probe_lights_base(float position[][4], float color[][4], float direction[][4], float extent_right[][4], float extent_up[][4],
    int shadows[], int max);
int pc_ph_lighting_ao_traced(void);
float pc_ph_lighting_traced_haze_density(void);
void pc_present_enter(void);       /* the presenter begins: with the canary on, the live variables hold poison */
void pc_present_leave(void);       /* ... and ends: they are restored */
void pc_present_canary_pause(int on); /* 1: the live values back for a stretch of game-thread work inside the present (the overlay's build), 0: poison again */
/* A value the present reads from its header, and anything outside a present (the editors, RPC dumps: the game thread's own calls) from the live variable. */
#define PC_PH_GET(field, live) (pc_present_inside ? pc_present_header_now.field : (live))
#include "present_guard.h"
#ifdef __cplusplus
}
#endif
#endif
