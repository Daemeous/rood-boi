/* The present frame header and its scramble canary (present_header.h). Host state. */
#ifndef _WIN32
#include <execinfo.h> /* (the canary trace's backtrace; none on Windows) */
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "present_header.h"
#include "lighting_quality.h"
#include "hw_cmd.h"
#include "renderer_rq.h"
#include "antialias.h"
#include "atmosphere.h"
#include "emissive.h"
#include "lighting_quality.h"
#include "post.h"
#include "light_list.h"
#include "ui/menu_light.h"

extern void pc_scene_bounds(float lo[3], float hi[3]);
extern int pc_scene_camera_get(float (*)[3], float*, float*, float*, float*);
extern int pc_scene_camera_inverse(float inverse[3][3]);
extern int pc_scene_projection(float*, float*, float*);
extern int pc_scene_game_camera(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy);
extern int pc_scene_invert_camera(const float r[3][3], float inverse[3][3]);
extern int pc_hw_view_camera(float r[3][3], float t[3], float* h, float* ofx, float* ofy) __attribute__((weak));
extern int pc_hw_replay_camera(float r[3][3], float t[3]) __attribute__((weak));
extern int pc_hw_replay_projection(float* h, float* ofx, float* ofy) __attribute__((weak));
extern void pc_baked_shadow_params(float out[5]);
extern void pc_key_light_params(float out[9]);
extern int pc_ui_menu_active(void) __attribute__((weak));
extern int pc_ui_menu_backdrop(float rgba[4]) __attribute__((weak));
extern int pc_ui_menu_model_light(pc_menu_light* out) __attribute__((weak));
extern int pc_photo_capture_wanted(void) __attribute__((weak));
extern int pc_emissive_active(void);
extern void pc_lighting_sync(void);
extern int pc_particles_gpu_snapshot(void);
extern int pc_lights_output(unsigned long long* keys, float* importance, int max, unsigned* frame);
extern int pc_probe_lights_base(float position[][4], float color[][4], float direction[][4], float extent_right[][4], float extent_up[][4],
    int shadows[], int max) __attribute__((weak));
extern int pc_lights_world(float position[][4], float color[][4], float direction[][4], float extent_right[][4], float extent_up[][4],
    int shadows[], int max);
extern int pc_lighting_ao_traced(void);
extern int pc_hw_scale, pc_hw_perspective, pc_hw_texture_filter, pc_hw_subpixel, pc_hw_interpolate, pc_hw_menu_studio;
extern int pc_hw_present_world(void) __attribute__((weak));
extern int pc_hw_present_menu_model(void) __attribute__((weak));
extern float pc_hw_present_fade(float rect[4]) __attribute__((weak));
extern const float* pc_hw_emissives(int* count, const void** surfaces) __attribute__((weak));
extern const uint32_t* pc_hw_emissive_sources(void) __attribute__((weak));
extern int pc_vsync_get(void);
extern unsigned pc_vsync_count(void);
extern void pc_window_pixel_size(int* w, int* h) __attribute__((weak)); /* boot_window.c */
extern float pc_lighting_traced_haze_density(void);
extern int pc_scene_serial(void), pc_scene_uv_serial(void);
extern const int* pc_scene_animated(int* count);
extern int pc_lighting_ssao(int traced_this_frame);
extern float pc_lighting_low_haze_density(int traced_haze);
extern float pc_lighting_baked_scale(float room_baked, int light_count);

extern int pc_baked_shadows_active, pc_display_rgb24, pc_gi_debug, pc_gi_enabled, pc_gl_aa, pc_gl_aspect, pc_gl_filter,
    pc_gl_show_depth, pc_gl_view_mode, pc_gte_widescreen, pc_present_brightness, pc_rt_shadows;
extern float pc_fog_anisotropy, pc_fog_density, pc_gi_strength, pc_light_size, pc_lights_baked, pc_normal_smoothing,
    pc_shadow_softness;
extern int pc_gl_inset[4];
extern int pc_baked_shadows, pc_fog_steps, pc_gi_bounces, pc_gi_denoise, pc_gi_rays, pc_gi_trace_debug, pc_glow_shadows, pc_nrd_glow, pc_nrd_max_frames, pc_probe_budget, pc_probe_rays, pc_probe_show, pc_shadow_samples, pc_fog_volume_render, pc_fog_volume_shadows, pc_fog_volume_steps, pc_haze_cell, pc_reflection_bounces, pc_gi_method, pc_haze_quality, pc_lights_enabled, pc_particles_untraced;
extern float pc_baked_ambient, pc_contact_radius, pc_emitter_light, pc_fog_glow, pc_gi_baked_bounce, pc_gi_resolution, pc_haze_history, pc_probe_hysteresis, pc_probe_normal_bias, pc_probe_spacing, pc_probe_view_bias, pc_shadow_history, pc_shadow_motion_history, pc_fog_volume_history, pc_fog_volume_resolution;

pc_present_header pc_present_header_slots[PC_HEADER_SLOTS] __attribute__((section(".data.rood_host")));
pc_present_header* pc_present_header_cur = &pc_present_header_slots[0];
pc_present_header* pc_present_header_target = &pc_present_header_slots[0];
__thread int pc_present_inside;
int pc_present_guard_on, pc_present_paused;
static pthread_mutex_t slot_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t slot_free = PTHREAD_COND_INITIALIZER;
static int slot_busy[PC_HEADER_SLOTS], slot_next;
void pc_present_header_use(int slot)
{
    if (slot >= 0 && slot < PC_HEADER_SLOTS)
        pc_present_header_cur = &pc_present_header_slots[slot];
}
void pc_present_header_release(int slot)
{
    if (slot < 0 || slot >= PC_HEADER_SLOTS)
        return;
    pthread_mutex_lock(&slot_lock);
    slot_busy[slot] = 0;
    pthread_cond_signal(&slot_free);
    pthread_mutex_unlock(&slot_lock);
}
static struct {
    const char* what;
    unsigned long long count;
} violations[64];
static int violation_count, abort_on_violation;
static void report_violations(void)
{
    for (int i = 0; i < violation_count; i++)
        fprintf(stderr, "[present-canary] live read of %s from the present: %llu calls\n", violations[i].what, violations[i].count);
}
void pc_present_violation(const char* what)
{
    for (int i = 0; i < violation_count; i++)
        if (!strcmp(violations[i].what, what)) {
            ++violations[i].count;
            return;
        }
    if (abort_on_violation) {
        fprintf(stderr, "[present-canary] live read of %s from the present\n", what);
        abort();
    }
    if (violation_count < 64) {
        if (!violation_count)
            atexit(report_violations);
        violations[violation_count].what = what, violations[violation_count++].count = 1;
        if (getenv("ROOD_PRESENT_CANARY_TRACE")) { /* where it was called from (first call of each) */
            fprintf(stderr, "[present-canary] %s called from:\n", what);
#ifndef _WIN32
            void* frames[10];
            backtrace_symbols_fd(frames, backtrace(frames, 10), 2);
#endif
        }
    }
}
static int canary = -1, canary_level;
static struct {
    const char* name;
    void* live;
    unsigned char saved[16];
    int size;
} entries[128];
static int entry_count, paused;

/* ROOD_PRESENT_CANARY_SKIP=name,name: variables left alone (finding which live read a run still has). */
static void add(const char* name, void* live, int size)
{
    const char* skip = getenv("ROOD_PRESENT_CANARY_SKIP");
    size_t n = strlen(name);
    for (const char* p = skip; p && *p;) {
        const char* end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && !strncmp(p, name, n))
            return;
        p = end ? end + 1 : p + len;
    }
    entries[entry_count].name = name, entries[entry_count].live = live, entries[entry_count].size = size;
    ++entry_count;
}
static void catalogue(void)
{
    if (entry_count)
        return;
#define PC_PH_ADD(f, live) add(#f, &live, sizeof live);
    PC_PH_INTS(PC_PH_ADD)
    PC_PH_FLOATS(PC_PH_ADD)
#undef PC_PH_ADD
    add("gl_inset", pc_gl_inset, sizeof pc_gl_inset);
    /* Level 2: the renderer's own settings the presenter still reads live (the 3D-maths slice's settings requests will carry them). */
    if (canary_level == 2)
        add("hw_scale", &pc_hw_scale, sizeof pc_hw_scale);
    /* Level 3: the settings the renderer's drawing reads (perspective, texture filter, subpixel, interpolate, menu studio): the renderer holds its
     * own copies, set by the stream (hw_cmd.c C_SET_SETTINGS), so poisoning the game's variables must change nothing. */
    if (canary_level == 3) {
        add("hw_perspective", &pc_hw_perspective, sizeof pc_hw_perspective);
        add("hw_texture_filter", &pc_hw_texture_filter, sizeof pc_hw_texture_filter);
        add("hw_subpixel", &pc_hw_subpixel, sizeof pc_hw_subpixel);
        add("hw_interpolate", &pc_hw_interpolate, sizeof pc_hw_interpolate);
        add("hw_menu_studio", &pc_hw_menu_studio, sizeof pc_hw_menu_studio);
    }
}
extern void pc_overlay_build(void) __attribute__((weak));
static void pc_overlay_build_hook(void)
{
    if (pc_overlay_build)
        pc_overlay_build();
}
static void scene_triangles_update(void);
extern int pc_ptgi_emitters_gather(float pos[][4], float col[][4], float shape[][4], float shadow[][4], int max) __attribute__((weak));
static int filling, emitters_gathered;
void pc_present_emitters_ensure(void)
{
    pc_present_header* h = pc_present_header_target;
    if (pc_present_inside || (filling && emitters_gathered))
        return;
    emitters_gathered = 1;
    h->emitter_count = pc_ptgi_emitters_gather ? pc_ptgi_emitters_gather(h->emitter_pos, h->emitter_col, h->emitter_shape, h->emitter_shadow,
                                                     PC_EMITTERS_MAX)
                                               : 0;
}
/* The game's vsync a frame is of: the presenter's header's (the frame as requested, whatever the game thread has run on to since), else
 * the live count. For what the lights and the traced lighting do over the game's own time (lights.cpp, vk_rt.inc). */
/* The image's place in the game frame: the phase of the present being drawn, 0 for what the game thread
 * works out for a frame at its fill (pc_present_phase is the render thread's, moving with its progress). */
float pc_frame_phase(void)
{
    extern float pc_present_phase;
    return pc_present_inside ? pc_present_phase : 0.0f;
}
unsigned pc_frame_vsync_count(void)
{
    return pc_present_inside ? pc_present_header_cur->vsync_count : pc_vsync_count();
}
int pc_present_header_fill(void)
{
    pthread_mutex_lock(&slot_lock);
    int slot = slot_next;
    while (slot_busy[slot])
        pthread_cond_wait(&slot_free, &slot_lock);
    slot_busy[slot] = 1;
    slot_next = (slot + 1) % PC_HEADER_SLOTS;
    pthread_mutex_unlock(&slot_lock);
    pc_present_header* h = &pc_present_header_slots[slot];
    pc_present_header_target = h; /* (the fill's own reads of pc_ph() are of this slot) */
    filling = 1, emitters_gathered = 0;
    pc_hwc_sync_widescreen(); /* (the renderer's copy of the flag follows the stream) */
    pc_hwc_sync_settings();
    {
        pc_rq rq = { .kind = PC_RQ_FRAME_STATE, .out = &h->shown, .out_bytes = sizeof h->shown };
        pc_renderer_rq(&rq); /* (everything recorded so far applied, then the renderer's say on the picture shown) */
    }
    h->vsync = pc_vsync_get();
    h->vsync_count = pc_vsync_count();
    h->window_px_w = h->window_px_h = 0;
    if (pc_window_pixel_size)
        pc_window_pixel_size(&h->window_px_w, &h->window_px_h);
    pc_lighting_sync(); /* lighting quality: first-run preset, Custom on changes (the game thread's: it writes the settings) */
#define PC_PH_COPY(f, live) h->f = live;
    PC_PH_INTS(PC_PH_COPY)
    PC_PH_FLOATS(PC_PH_COPY)
#undef PC_PH_COPY
    memcpy(h->gl_inset, pc_gl_inset, sizeof h->gl_inset);
    /* the scene */
    h->game_camera_ok = pc_scene_game_camera(h->game_r, h->game_t, &h->game_h, &h->game_ofx, &h->game_ofy);
    h->projection_ok = h->game_camera_ok && h->game_h > 0;
    pc_scene_bounds(h->room_lo, h->room_hi);
    /* the lights */
    h->light_count = h->projection_ok ? pc_lights_list(h->lights, PC_LIGHTS_MAX) : 0;
    h->out_count = pc_lights_output(h->out_keys, h->out_importance, PC_LIGHTS_MAX, &h->lights_frame); /* (straight after the list the pick goes by) */
    pc_baked_shadow_params(h->baked_shadow);
    pc_key_light_params(h->key_light);
    /* the menu */
    h->menu_active = pc_ui_menu_active && pc_ui_menu_active();
    h->menu_backdrop_ok = pc_ui_menu_backdrop && pc_ui_menu_backdrop(h->menu_backdrop);
    h->menu_light_ok = pc_ui_menu_model_light && pc_ui_menu_model_light(&h->menu_light);
    /* post, photo, emissive, lighting quality */
    pc_post_frame_get(&h->post);
    h->photo_wanted = pc_photo_capture_wanted && pc_photo_capture_wanted();
    h->emissive_active = pc_emissive_active();
    h->ao_traced = pc_lighting_ao_traced();
    h->ssao[0] = pc_lighting_ssao(0), h->ssao[1] = pc_lighting_ssao(1);
    h->low_haze = pc_lighting_low_haze_density(0);
    h->baked_scale = pc_lighting_baked_scale(h->lights_baked, h->light_count);
    h->traced_haze = pc_lighting_traced_haze_density();
    h->scene_serial = pc_scene_serial(), h->scene_uv_serial = pc_scene_uv_serial();
    {
        int count = 0;
        const int* animated = pc_scene_animated(&count);
        h->animated_overflow = count > 4096;
        h->animated_count = count > 4096 ? 4096 : count;
        if (animated)
            memcpy(h->animated, animated, (size_t)h->animated_count * sizeof *h->animated);
    }
    h->light_count_all = pc_lights_list(h->lights_all, PC_LIGHTS_MAX);
    pc_particles_gpu_snapshot(); /* the particles' game-thread half (particles.cpp) */
    memset(h->world_light_shadows, 0, sizeof h->world_light_shadows);
    h->world_light_count = pc_lights_world(h->world_lights[0], h->world_lights[1], h->world_lights[2], h->world_lights[3], h->world_lights[4],
        h->world_light_shadows, 16);
    h->env_active = pc_env_gpu_get(&h->env);
    scene_triangles_update();
    /* after the scene copy (the glows read the room's triangles); when something that follows needs them: the path tracer, the baked shadows,
     * the AO, the probes (the fog volumes and the particles ask for them themselves) */
    if (h->lights_enabled && (h->gi_enabled || h->rt_shadows || h->baked_shadows || h->ao_traced || h->gi_method == PC_GI_PROBES))
        pc_present_emitters_ensure();
    /* the atmosphere */
    h->medium_traced_ok = pc_medium_gpu_get(&h->medium_traced, h->traced_haze, h->room_lo, h->room_hi);
    h->medium_low_ok = pc_medium_gpu_get(&h->medium_low, h->low_haze, h->room_lo, h->room_hi);
    h->fogvol_count = pc_fogvol_gpu_get(&h->fogvol_medium, &h->fogvol, &h->fogvol_info, h->room_lo, h->room_hi);
    h->medium_hash = pc_medium_hash(), h->env_hash = pc_env_hash();
    pc_overlay_build_hook(); /* last: the UI reads and edits the live settings after the values above were copied, as the present's own overlay did */
    memset(h->probe_light_shadows, 0, sizeof h->probe_light_shadows);
    h->probe_light_count = pc_probe_lights_base
        ? pc_probe_lights_base(h->probe_lights[0], h->probe_lights[1], h->probe_lights[2], h->probe_lights[3], h->probe_lights[4],
              h->probe_light_shadows, 16)
        : -1;
    filling = 0;
    return slot;
}
/* The shown picture's state (renderer_frame_state.h): from the header being filled (the game thread) or presented from; the renderer's own
 * otherwise (the GL path, RPC dumps). */
static const pc_renderer_frame_state* shown_state(void)
{
    if (pc_present_inside)
        return &pc_present_header_cur->shown;
    return filling ? &pc_present_header_target->shown : NULL;
}
int pc_ph_present_world(void)
{
    const pc_renderer_frame_state* s = shown_state();
    return s ? s->world : (pc_hw_present_world && pc_hw_present_world());
}
int pc_ph_present_menu_model(void)
{
    const pc_renderer_frame_state* s = shown_state();
    return s ? s->menu_model : (pc_hw_present_menu_model && pc_hw_present_menu_model());
}
float pc_ph_present_fade(float rect[4])
{
    const pc_renderer_frame_state* s = shown_state();
    if (!s)
        return pc_hw_present_fade ? pc_hw_present_fade(rect) : 0.0f;
    if (rect)
        memcpy(rect, s->fade_rect, sizeof s->fade_rect);
    return s->fade;
}
const float* pc_ph_emissives(int* count, const void** surfaces)
{
    const pc_renderer_frame_state* s = shown_state();
    if (!s) {
        *count = 0, *surfaces = NULL;
        return pc_hw_emissives ? pc_hw_emissives(count, surfaces) : NULL;
    }
    *count = s->emissive_count, *surfaces = s->surfaces;
    return s->emissives;
}
const uint32_t* pc_ph_emissive_sources(void)
{
    const pc_renderer_frame_state* s = shown_state();
    return s ? s->sources : (pc_hw_emissive_sources ? pc_hw_emissive_sources() : NULL);
}
unsigned pc_ph_vsync(void) { return pc_present_inside ? pc_present_header_cur->vsync_count : pc_vsync_count(); }
void pc_ph_window_pixels(int* w, int* h)
{
    if (pc_present_inside) {
        *w = pc_present_header_now.window_px_w, *h = pc_present_header_now.window_px_h;
    } else if (pc_window_pixel_size) {
        pc_window_pixel_size(w, h);
    } else {
        *w = *h = 0;
    }
}
int pc_ph_medium_gpu_get(pc_medium_gpu* out, float density, const float room_lo[3], const float room_hi[3])
{
    const pc_present_header* p = &pc_present_header_now;
    if (pc_present_inside && !memcmp(room_lo, p->room_lo, sizeof p->room_lo) && !memcmp(room_hi, p->room_hi, sizeof p->room_hi)) {
        if (density == p->traced_haze)
            return *out = p->medium_traced, p->medium_traced_ok;
        if (density == p->low_haze)
            return *out = p->medium_low, p->medium_low_ok;
    }
    return pc_medium_gpu_get(out, density, room_lo, room_hi);
}
int pc_ph_fogvol_gpu_get(pc_medium_gpu* medium, pc_fogvol_gpu* out, pc_fogvol_info* info, const float room_lo[3], const float room_hi[3])
{
    const pc_present_header* p = &pc_present_header_now;
    if (pc_present_inside && !memcmp(room_lo, p->room_lo, sizeof p->room_lo) && !memcmp(room_hi, p->room_hi, sizeof p->room_hi)) {
        *medium = p->fogvol_medium, *out = p->fogvol;
        if (info)
            *info = p->fogvol_info;
        return p->fogvol_count;
    }
    return pc_fogvol_gpu_get(medium, out, info, room_lo, room_hi);
}
unsigned pc_ph_medium_hash(void) { return pc_present_inside ? pc_present_header_now.medium_hash : pc_medium_hash(); }
unsigned pc_ph_env_hash(void) { return pc_present_inside ? pc_present_header_now.env_hash : pc_env_hash(); }
int pc_ph_probe_lights_base(float position[][4], float color[][4], float direction[][4], float extent_right[][4], float extent_up[][4],
    int shadows[], int max)
{
    if (!pc_probe_lights_base)
        return -1;
    if (!pc_present_inside || max != 16)
        return pc_probe_lights_base(position, color, direction, extent_right, extent_up, shadows, max);
    const pc_present_header* p = &pc_present_header_now;
    memcpy(position, p->probe_lights[0], sizeof p->probe_lights[0]), memcpy(color, p->probe_lights[1], sizeof p->probe_lights[1]);
    memcpy(direction, p->probe_lights[2], sizeof p->probe_lights[2]), memcpy(extent_right, p->probe_lights[3], sizeof p->probe_lights[3]);
    memcpy(extent_up, p->probe_lights[4], sizeof p->probe_lights[4]);
    memcpy(shadows, p->probe_light_shadows, sizeof p->probe_light_shadows);
    return p->probe_light_count;
}
static scene_triangle* tri_copy;
static int tri_count, tri_capacity, tri_serial = -1, tri_uv_serial = -1;
static void scene_triangles_update(void)
{
    int count = 0;
    const scene_triangle* live = pc_scene_triangle_data(&count);
    int serial = pc_scene_serial(), uv = pc_scene_uv_serial();
    if (!live || count <= 0) {
        tri_count = 0, tri_serial = -1;
        return;
    }
    if (serial != tri_serial || count != tri_count) {
        if (count > tri_capacity) {
            free(tri_copy);
            tri_capacity = count + count / 4;
            tri_copy = malloc((size_t)tri_capacity * sizeof *tri_copy);
        }
        memcpy(tri_copy, live, (size_t)count * sizeof *live);
        tri_count = count, tri_serial = serial, tri_uv_serial = uv;
    } else if (uv != tri_uv_serial) {
        int n = 0;
        const int* animated = pc_scene_animated(&n);
        for (int k = 0; animated && k < n; k++)
            if (animated[k] >= 0 && animated[k] < count)
                tri_copy[animated[k]] = live[animated[k]];
        tri_uv_serial = uv;
    }
    static int verify = -1;
    if (verify < 0)
        verify = getenv("ROOD_PRESENT_CANARY_TRIANGLES") != NULL;
    if (verify && memcmp(tri_copy, live, (size_t)count * sizeof *live)) {
        int first = 0, diffs = 0;
        for (int i = 0; i < count; i++)
            if (memcmp(&tri_copy[i], &live[i], sizeof *live)) {
                if (!diffs++)
                    first = i;
            }
        fprintf(stderr, "[present-canary] the scene triangles' copy differs from the live array in %d of %d (first %d; serial %d, uv %d)\n", diffs, count,
            first, serial, uv);
        memcpy(tri_copy, live, (size_t)count * sizeof *live); /* (report once per change) */
    }
}
const scene_triangle* pc_ph_scene_triangle_data(int* count)
{
    if (!pc_present_inside)
        return pc_scene_triangle_data(count);
    *count = tri_count;
    return tri_count ? tri_copy : NULL;
}
int pc_ph_lights_world(float position[][4], float color[][4], float direction[][4], float extent_right[][4], float extent_up[][4],
    int shadows[], int max)
{
    if (!pc_present_inside || max != 16)
        return pc_lights_world(position, color, direction, extent_right, extent_up, shadows, max);
    const pc_present_header* p = &pc_present_header_now;
    memcpy(position, p->world_lights[0], sizeof p->world_lights[0]), memcpy(color, p->world_lights[1], sizeof p->world_lights[1]);
    memcpy(direction, p->world_lights[2], sizeof p->world_lights[2]), memcpy(extent_right, p->world_lights[3], sizeof p->world_lights[3]);
    memcpy(extent_up, p->world_lights[4], sizeof p->world_lights[4]);
    memcpy(shadows, p->world_light_shadows, sizeof p->world_light_shadows);
    return p->world_light_count;
}
int pc_ph_env_gpu_get(pc_env_gpu* out)
{
    if (!pc_present_inside)
        return pc_env_gpu_get(out);
    *out = pc_present_header_now.env;
    return pc_present_header_now.env_active;
}
void pc_ph_scene_bounds(float lo[3], float hi[3])
{
    if (!pc_present_inside)
        return pc_scene_bounds(lo, hi);
    memcpy(lo, pc_present_header_now.room_lo, sizeof pc_present_header_now.room_lo);
    memcpy(hi, pc_present_header_now.room_hi, sizeof pc_present_header_now.room_hi);
}
int pc_ph_scene_camera_get(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy)
{
    if (!pc_present_inside)
        return pc_scene_camera_get(rotation, translation, h, ofx, ofy);
    const pc_present_header* p = &pc_present_header_now;
    if (!p->game_camera_ok)
        return 0;
    memcpy(rotation, p->game_r, sizeof p->game_r), memcpy(translation, p->game_t, sizeof p->game_t);
    *h = p->game_h, *ofx = p->game_ofx, *ofy = p->game_ofy;
    /* the picture's camera: the renderer's own state, applied now (scene_capture.c pc_scene_camera_get does the same to the live camera) */
    if (pc_hw_view_camera && pc_hw_view_camera(rotation, translation, h, ofx, ofy))
        return 1;
    if (pc_hw_replay_camera)
        pc_hw_replay_camera(rotation, translation);
    if (pc_hw_replay_projection)
        pc_hw_replay_projection(h, ofx, ofy);
    return 1;
}
int pc_ph_scene_camera_inverse(float inverse[3][3])
{
    if (!pc_present_inside)
        return pc_scene_camera_inverse(inverse);
    float r[3][3], t[3], h, ofx, ofy;
    return pc_ph_scene_camera_get(r, t, &h, &ofx, &ofy) && pc_scene_invert_camera(r, inverse);
}
int pc_ph_scene_projection(float* h, float* ofx, float* ofy)
{
    if (!pc_present_inside)
        return pc_scene_projection(h, ofx, ofy);
    float r[3][3], t[3];
    if (!pc_present_header_now.projection_ok)
        return 0;
    return pc_ph_scene_camera_get(r, t, h, ofx, ofy);
}
int pc_ph_scene_serial(void) { return pc_present_inside ? pc_present_header_now.scene_serial : pc_scene_serial(); }
int pc_ph_scene_uv_serial(void) { return pc_present_inside ? pc_present_header_now.scene_uv_serial : pc_scene_uv_serial(); }
const int* pc_ph_scene_animated(int* count)
{
    if (!pc_present_inside || pc_present_header_now.animated_overflow)
        return pc_scene_animated(count);
    *count = pc_present_header_now.animated_count;
    return pc_present_header_now.animated;
}
/* Shadow maps for the most important casters only (the renderer's copy of lights.cpp's pick: the same choice, its hysteresis state here). */
/* Shadow maps for the most important casters only: the renderer's copy of lights.cpp's pick (tests/shadow_pick_test.py runs the two on the
 * same random frames), its hysteresis state here. */
void pc_ph_lights_shadow_pick(int count, int* wants, int slots)
{
    static struct {
        unsigned long long keys[16];
        int count;
        unsigned frame;
    } kept;
    const pc_present_header* p = &pc_present_header_now;
    if (p->out_count != count || slots > 16)
        return;
    int casters = 0;
    for (int i = 0; i < count; i++)
        casters += wants[i] != 0;
    if (casters <= slots)
        return;
    static int order[PC_LIGHTS_MAX];
    static float score[PC_LIGHTS_MAX];
    int n = 0;
    for (int i = 0; i < count; i++) {
        score[i] = 0;
        if (wants[i]) {
            order[n++] = i;
            int was = 0;
            for (int k = 0; p->out_keys[i] && k < kept.count; k++)
                was |= kept.keys[k] == p->out_keys[i];
            score[i] = p->out_importance[i] * (was ? 1.5f : 1.0f);
        }
    }
    for (int a = 1; a < n; a++) { /* a stable sort, best first */
        int v = order[a], b = a - 1;
        while (b >= 0 && score[order[b]] < score[v])
            order[b + 1] = order[b], --b;
        order[b + 1] = v;
    }
    for (int k = slots; k < n; k++)
        wants[order[k]] = 0;
    if (kept.frame != p->lights_frame) {
        kept.frame = p->lights_frame;
        kept.count = 0;
        for (int k = 0; k < slots && k < n; k++)
            kept.keys[kept.count++] = p->out_keys[order[k]];
    }
}
int pc_ph_lights_list(pc_light_gpu* out, int max)
{
    if (!pc_present_inside)
        return pc_lights_list(out, max);
    int n = pc_present_header_now.light_count_all < max ? pc_present_header_now.light_count_all : max;
    memcpy(out, pc_present_header_now.lights_all, (size_t)n * sizeof *out);
    return n;
}
int pc_ph_lighting_ao_traced(void) { return pc_present_inside ? pc_present_header_now.ao_traced : pc_lighting_ao_traced(); }
float pc_ph_lighting_traced_haze_density(void) { return pc_present_inside ? pc_present_header_now.traced_haze : pc_lighting_traced_haze_density(); }
static void poison(int on)
{
    for (int i = 0; i < entry_count; i++) {
        if (on) {
            memcpy(entries[i].saved, entries[i].live, (size_t)entries[i].size);
            memset(entries[i].live, 0xA5, (size_t)entries[i].size); /* ints 0xA5A5A5A5, floats a large negative */
        } else {
            /* Restore what the poison still covers; a variable the renderer side wrote meanwhile (GI turning itself off when it cannot
             * start, the baked shadow flag the game's drop shadow reads) keeps its value: that write is the renderer's reply. */
            unsigned char* live = entries[i].live;
            int poisoned = 1;
            for (int k = 0; k < entries[i].size; k++)
                poisoned &= live[k] == 0xA5;
            if (poisoned)
                memcpy(entries[i].live, entries[i].saved, (size_t)entries[i].size);
        }
    }
}
void pc_present_guard_check(const char* what)
{
    if (pc_present_inside && !pc_present_paused)
        pc_present_violation(what);
}
void pc_present_enter(void)
{
    if (canary < 0) {
        const char* v = getenv("ROOD_PRESENT_CANARY");
        canary = v && (*v == '1' || *v == '2' || *v == '3');
        canary_level = canary ? *v - '0' : 0;
        pc_present_guard_on = getenv("ROOD_PRESENT_CANARY_CALLS") != NULL;
        abort_on_violation = getenv("ROOD_PRESENT_CANARY_ABORT") != NULL;
        catalogue();
    }
    pc_present_inside = 1;
    if (canary) {
        poison(1);
        paused = 0;
    }
}
void pc_present_leave(void)
{
    if (canary) {
        if (!paused)
            poison(0);
        paused = 0;
    }
    pc_present_inside = pc_present_paused = 0;
    static unsigned leaves;
    if (pc_present_guard_on && ++leaves % 600 == 0)
        report_violations(); /* (the game may not exit through atexit) */
}
void pc_present_canary_pause(int on)
{
    pc_present_paused = on;
    if (!canary || on == paused)
        return;
    if (on)
        poison(0); /* the live values back */
    else
        poison(1); /* (taking them again: the panel's edits are the new live values) */
    paused = on;
}
