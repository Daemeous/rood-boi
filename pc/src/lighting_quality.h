/* Lighting quality presets (Original / Low / Medium / High / Ultra / Custom)
 * and the settings they set (pc/src/lighting_quality.cpp; README "Lighting
 * quality"). A preset is a list of "key value" lines, each value a list of
 * choices in order of preference ("traced|maps": the first one this
 * backend can run). Selecting a preset sets its keys; changing any of them
 * afterwards (overlay, RPC, settings file) makes the preset Custom. Per-room
 * values (room_settings.cpp: light intensity, haze density, light size)
 * still apply on top.
 *
 * Keys (settings file pc/user/settings.cfg, RPC graphics, overlay Graphics >
 * Lighting quality): lighting_preset, dynamic_lights, gi_method
 * (off|screen|probes|path), gi_resolution, bounces, rays_per_pixel,
 * gi_denoiser (raw|filter|nrd), shadows (off|maps|traced),
 * shadow_map_filter (hard|pcf|pcss), shadow_map_softness, shadow_rays,
 * glow_shadows, characters_shadow_baked, haze (off|low|high), ao,
 * ao_radius, ao_strength, probe_spacing, probe_rays (the probe GI's density,
 * probe_gi.h). */
#ifndef ROOD_LIGHTING_QUALITY_H
#define ROOD_LIGHTING_QUALITY_H
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PC_PRESET_ORIGINAL, PC_PRESET_LOW, PC_PRESET_MEDIUM, PC_PRESET_HIGH, PC_PRESET_ULTRA, PC_PRESET_CUSTOM,
    PC_PRESET_COUNT };
/* gi_method: indirect light. Probes come from the probe GI (a weak
 * pc_probe_gi_available() says whether this build has them). */
enum { PC_GI_OFF, PC_GI_SCREEN, PC_GI_PROBES, PC_GI_PATH };
enum { PC_SHADOWS_OFF, PC_SHADOWS_MAPS, PC_SHADOWS_TRACED };
enum { PC_SHADOW_MAP_HARD, PC_SHADOW_MAP_PCF, PC_SHADOW_MAP_PCSS };
enum { PC_HAZE_OFF, PC_HAZE_LOW, PC_HAZE_HIGH };

extern int pc_lighting_preset, pc_gi_method, pc_shadow_mode, pc_shadow_map_filter, pc_haze_quality, pc_ao;
extern float pc_ao_radius, pc_ao_strength;

/* What this backend and GPU can run. */
typedef struct {
    int vulkan; /* the Vulkan renderer is active */
    int ray_query; /* Vulkan ray queries (hardware ray tracing) */
    int rt_disabled; /* ROOD_VK_RT=0 */
    int gl_compute; /* OpenGL 4.3 compute (the GL path tracer) */
    int traced; /* a path tracer runs here (Vulkan ray queries, or GL compute) */
    int integrated; /* integrated (or software) GPU; ROOD_GPU_CLASS overrides */
    int nrd; /* NVIDIA NRD usable (Vulkan with ray tracing, built with NRD) */
    int nrd_built;
    int probes; /* probe GI built in */
    int hardware; /* the hardware renderer (G-buffer, shadow maps) */
    char gpu[160];
} pc_lighting_caps_t;
void pc_lighting_caps(pc_lighting_caps_t* caps);
/* The preset to start with (first run): pure, for tests. */
int pc_lighting_autodetect(int ray_query, int integrated);
const char* pc_lighting_preset_name(int preset);

/* Apply a preset's keys for this backend (no-op for Custom). */
void pc_lighting_apply_preset(int preset);
/* Once per presented frame (and after RPC writes): first-run detection,
 * Custom when a key changed, the legacy switches (pc_gi_enabled,
 * pc_rt_shadows) kept in step with gi_method and shadows. */
void pc_lighting_sync(void);

/* For the renderers. */
float pc_lighting_traced_haze_density(void); /* haze High: the traced haze's density (0: none) */
/* haze Low (or High where nothing traced the haze this frame): the present
 * pass's unshadowed haze density (0: none). */
float pc_lighting_low_haze_density(int traced_haze);
int pc_lighting_ao_traced(void); /* AO is traced with the GI rays (Vulkan ray queries) */
int pc_lighting_ssao(int traced_this_frame); /* screen-space AO in the present pass */
float pc_lighting_baked_scale(float room_baked, int light_count); /* the baked light's scale */

/* Settings file and RPC: set one key from text ("pcss", "1", "true", "0.5");
 * returns 0 for an unknown key or value. */
int pc_lighting_set(const char* key, const char* value);
int pc_lighting_key_count(void);
const char* pc_lighting_key_name(int i);
/* The keys as JSON members (",\"key\":value..." without braces), with the
 * capabilities ("lighting_caps") and why unavailable choices are. */
int pc_lighting_json(char* out, int size);
void pc_lighting_settings_write(FILE* f);
/* One settings line's key and the rest of the line; 1 when it was ours. */
int pc_lighting_settings_parse(const char* key, const char* rest);
/* After a settings file was read: had it a preset? (first run if not). */
void pc_lighting_settings_loaded(int file_exists);

/* Why a choice cannot run on this backend (NULL: it can). */
const char* pc_lighting_unavailable(const char* key, int value);

/* Overlay (dev_overlay.cpp). */
void pc_lighting_toolbar(void);
void pc_lighting_open_menu(void); /* the toolbar's dropdown opens on its next frame (RPC editor_ui) */
void pc_lighting_panel(void);

/* GPU time per presented frame (ROOD_PERF=1), averaged until read (RPC perf). */
void pc_perf_gpu_add(double ms);
int pc_perf_json(char* out, int size, int reset);

#ifdef __cplusplus
}
#endif
#endif
