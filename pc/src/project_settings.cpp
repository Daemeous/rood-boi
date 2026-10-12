// User settings for everything not tied to a room: graphics, post effects,
// path tracer quality and shared lighting values. Loaded at startup from the
// file named by ROOD_SETTINGS (pc/run-game.sh passes pc/user/settings.cfg;
// tests leave it unset and keep the built-in defaults), saved from the
// editor (File > Save settings) and reloaded when the file changes on disk.
// One "key value..." line per setting; unknown keys are ignored.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern "C" {
extern int pc_gl_filter, pc_gl_aa, pc_gl_aspect, pc_gte_widescreen, pc_hw_perspective, pc_hw_subpixel,
    pc_hw_interpolate, pc_hw_texture_filter, pc_hw_scale, pc_interp_skeleton, pc_interp_holds, pc_interp_uv,
    pc_present_rate;
int pc_hw_set_scale(int scale);
extern int pc_post_flags, pc_post_focus_mode;
extern float pc_post_aperture, pc_post_focus, pc_post_dof_radius, pc_post_tilt[3], pc_post_fog[4],
    pc_post_fog_range[2];
extern int pc_gi_enabled, pc_rt_shadows, pc_baked_shadows, pc_shadow_samples, pc_gi_bounces, pc_gi_rays,
    pc_fog_steps, pc_gi_denoise, pc_baked_estimate, pc_glow_shadows;
extern float pc_shadow_history, pc_gi_resolution;
extern int pc_lights_enabled;
extern float pc_shadow_softness, pc_normal_smoothing;
extern int pc_drop_shadow_mode __attribute__((weak)); // battle builds (actor_shadow.c)
extern int pc_native_room, pc_native_occlusion, pc_native_actors, pc_native_actor_order, pc_native_blend,
    pc_native_objects, pc_native_shadows, pc_native_effects, pc_reconstruct;
extern int pc_editor_hide_masks, pc_editor_game_cutaway, pc_editor_cutaway, pc_editor_cutaway_auto;
extern float pc_editor_cutaway_radius, pc_editor_cutaway_opacity;
extern int pc_lights_snap;
extern float pc_lights_snap_move, pc_lights_snap_angle;
}
#include "camera_stick.h" /* the right-stick camera (camera_stick_*) */
#include "editor_overlays.h"
#include "gameplay_walls.h" /* gameplay_walls original | smart, gameplay_walls_ghost, _opacity, _radius */
#include "lighting_quality.h" /* the lighting quality keys (lighting_preset, gi_method, shadows, ...) */
#include "probe_gi.h"
#include "ui_upscale.h" /* 2D / UI: ui_upscale nearest | mmpx, ui_subpixel */
#include "lighting_layers.h" /* lighting_packs, lighting_active_pack */
extern bool pc_lights_show_gizmos;
namespace rood_lights {
extern int animate; /* light actors' flicker and pulse (lights.cpp) */
}

namespace {
struct Entry {
    const char* key;
    int* ints; // one of ints / floats / bools
    float* floats;
    int count;
    bool* bools;
};
#define I(key, var) { key, &(var), nullptr, 1, nullptr }
#define F(key, var) { key, nullptr, &(var), 1, nullptr }
#define FN(key, var, n) { key, nullptr, var, n, nullptr }
#define B(key, var) { key, nullptr, nullptr, 1, &(var) }
const Entry entries[] = {
    I("filter", pc_gl_filter),
    I("antialiasing", pc_gl_aa),
    I("aspect", pc_gl_aspect),
    I("widescreen", pc_gte_widescreen),
    I("perspective_textures", pc_hw_perspective),
    I("subpixel", pc_hw_subpixel),
    I("interpolate", pc_hw_interpolate),
    I("interp_skeleton", pc_interp_skeleton),
    I("interp_holds", pc_interp_holds),
    I("interp_uv", pc_interp_uv),
    I("present_rate", pc_present_rate),
    I("smooth_textures", pc_hw_texture_filter),
    I("ui_subpixel", pc_ui_subpixel),
    I("native_room", pc_native_room),
    I("native_occlusion", pc_native_occlusion),
    I("native_actors", pc_native_actors),
    I("native_actor_order", pc_native_actor_order),
    I("native_blend", pc_native_blend),
    I("native_objects", pc_native_objects),
    I("native_shadows", pc_native_shadows),
    I("native_effects", pc_native_effects),
    I("reconstruct", pc_reconstruct),
    I("post_flags", pc_post_flags),
    I("focus_mode", pc_post_focus_mode),
    F("aperture", pc_post_aperture),
    F("focus_depth", pc_post_focus),
    F("dof_radius", pc_post_dof_radius),
    FN("tilt_shift", pc_post_tilt, 3),
    FN("depth_fog", pc_post_fog, 4),
    FN("depth_fog_range", pc_post_fog_range, 2),
    I("baked_estimate", pc_baked_estimate),
    F("shadow_history", pc_shadow_history),
    I("haze_steps", pc_fog_steps),
    /* Probe GI's own settings (probe_gi.h; gi_method, probe_spacing and probe_rays are lighting quality keys). */
    F("probe_hysteresis", pc_probe_hysteresis),
    I("probe_budget", pc_probe_budget),
    I("probe_show", pc_probe_show),
    I("light_animation", rood_lights::animate),
    F("normal_smoothing", pc_normal_smoothing),
    I("gameplay_walls_ghost", pc_gameplay_walls_ghost),
    F("gameplay_walls_opacity", pc_gameplay_walls_opacity),
    F("gameplay_walls_radius", pc_gameplay_walls_radius),
    I("editor_hide_masks", pc_editor_hide_masks),
    I("editor_game_cutaway", pc_editor_game_cutaway),
    I("editor_cutaway", pc_editor_cutaway),
    F("editor_cutaway_radius", pc_editor_cutaway_radius),
    F("editor_cutaway_opacity", pc_editor_cutaway_opacity),
    I("editor_cutaway_auto", pc_editor_cutaway_auto),
    /* Editor viewport (editor_overlays.h) and snapping. */
    B("editor_show_lights", pc_lights_show_gizmos),
    I("editor_wireframe", pc_overlay_wire),
    I("editor_points", pc_overlay_points),
    I("editor_character_wireframe", pc_overlay_actor_wire),
    I("editor_snap_show", pc_overlay_snap_show),
    F("editor_occluded_opacity", pc_overlay_occluded),
    I("editor_snap_grid", pc_lights_snap),
    F("editor_snap_move", pc_lights_snap_move),
    F("editor_snap_angle", pc_lights_snap_angle),
    I("editor_snap_point", pc_snap_point),
    I("editor_snap_edge", pc_snap_edge),
    I("editor_snap_midpoint", pc_snap_midpoint),
    I("editor_snap_surface", pc_snap_surface),
    F("editor_snap_radius", pc_snap_radius),
    F("editor_snap_highlight", pc_snap_highlight),
    F("editor_snap_offset", pc_snap_offset),
    /* The right-stick camera (camera_stick.c). */
    I("camera_stick", pc_camera_stick),
    F("camera_stick_speed", pc_camera_stick_speed),
    F("camera_stick_deadzone", pc_camera_stick_deadzone),
    F("camera_stick_curve", pc_camera_stick_curve),
    I("camera_stick_invert_x", pc_camera_stick_invert_x),
    I("camera_stick_invert_y", pc_camera_stick_invert_y),
    I("camera_stick_zoom", pc_camera_stick_zoom),
    F("camera_stick_zoom_speed", pc_camera_stick_zoom_speed),
    F("camera_stick_zoom_min", pc_camera_stick_zoom_min),
    F("camera_stick_zoom_max", pc_camera_stick_zoom_max),
    F("camera_stick_zoom_pitch", pc_camera_stick_zoom_pitch),
    I("camera_stick_collision", pc_camera_stick_collision),
    I("camera_stick_r3_reset", pc_camera_stick_r3),
    F("camera_stick_idle_reset", pc_camera_stick_idle_reset),
    I("camera_stick_keys", pc_camera_stick_keys),
};
const char* path()
{
    const char* p = getenv("ROOD_SETTINGS");
    return p && *p ? p : nullptr;
}
long long loaded_mtime;
char status[200];

long long mtime_of(const char* file)
{
    struct stat st;
    return stat(file, &st) ? 0 : (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

/* One "key value..." line; *scale takes internal_resolution. With `now`, a
 * lighting_preset applies at once (the editor's undo) instead of after the
 * whole file (pc_lighting_settings_loaded). Returns 1 when it was a setting. */
int apply_line(const char* line, int* scale, bool now)
{
    char key[64];
    int used;
    if (line[0] == '#' || sscanf(line, "%63s%n", key, &used) != 1)
        return 0;
    const char* rest = line + used;
    if (!strcmp(key, "internal_resolution")) {
        *scale = atoi(rest);
        return 0;
    }
    if (!strcmp(key, "denoise")) { /* before gi_denoiser: on/off; "on" keeps the default */
        if (!atoi(rest))
            pc_gi_denoise = 0;
        return 0;
    }
    if (now && !strcmp(key, "lighting_preset")) {
        char name[32];
        if (sscanf(rest, "%31s", name) == 1)
            pc_lighting_set("lighting_preset", name);
        return 1;
    }
    if (pc_lighting_settings_parse(key, rest))
        return 1;
    if (!strcmp(key, "ui_upscale")) { /* nearest | mmpx (or 0 / 1) */
        char name[32] = "";
        sscanf(rest, "%31s", name);
        for (int k = 0; k < 2; k++)
            if (!strcmp(name, pc_ui_upscale_names[k]) || (name[0] == '0' + k && !name[1]))
                pc_ui_upscale = k;
        return 1;
    }
    if (!strcmp(key, "gameplay_walls")) { /* original | smart (or 0 / 1) */
        char name[32] = "";
        sscanf(rest, "%31s", name);
        if (!strcmp(name, "original") || !strcmp(name, "0"))
            pc_gameplay_walls = 0;
        else if (!strcmp(name, "smart") || !strcmp(name, "1"))
            pc_gameplay_walls = 1;
        return 1;
    }
    if (!strcmp(key, "lighting_packs") || !strcmp(key, "lighting_active_pack")) {
        /* Comma-separated pack names, lowest first (lighting/README.md). */
        std::string value = rest;
        size_t a = value.find_first_not_of(" \t"), b = value.find_last_not_of(" \t\r\n");
        value = a == std::string::npos ? std::string() : value.substr(a, b - a + 1);
        if (!strcmp(key, "lighting_packs"))
            lighting::set_packs(value);
        else
            lighting::set_active_pack(value);
        return 1;
    }
    if (!strcmp(key, "drop_shadows")) {
        if (&pc_drop_shadow_mode)
            pc_drop_shadow_mode = atoi(rest);
        return 0;
    }
    int applied = 0;
    for (const Entry& e : entries) {
        if (strcmp(e.key, key))
            continue;
        const char* cursor = rest;
        for (int i = 0; i < e.count; i++) {
            char* end;
            double v = strtod(cursor, &end);
            if (end == cursor)
                break;
            cursor = end;
            if (e.ints)
                e.ints[i] = (int)v;
            else if (e.bools)
                e.bools[i] = v != 0;
            else
                e.floats[i] = (float)v;
        }
        ++applied;
    }
    return applied;
}
} // namespace

extern "C" const char* pc_settings_status(void) { return status; }
extern "C" const char* pc_settings_path(void) { return path(); }

/* Load the settings file (startup, and again when it changes on disk). */
extern "C" void pc_settings_load(void)
{
    const char* file = path();
    if (!file)
        return;
    FILE* f = fopen(file, "r");
    loaded_mtime = mtime_of(file);
    if (!f) {
        snprintf(status, sizeof status, "No settings saved yet (%s)", file);
        pc_lighting_settings_loaded(0); /* first run: detect the lighting quality preset */
        return;
    }
    char line[512];
    int applied = 0, scale = 0;
    while (fgets(line, sizeof line, f))
        applied += apply_line(line, &scale, false);
    fclose(f);
    pc_lighting_settings_loaded(1);
    if (scale > 0 && scale != pc_hw_scale)
        pc_hw_set_scale(scale);
    snprintf(status, sizeof status, "Loaded %d settings from %s", applied, file);
}

/* Every setting as "key value..." lines (the file's body; the editor's undo
 * compares them). */
extern "C" void pc_settings_write(FILE* f)
{
    fprintf(f, "internal_resolution %d\n", pc_hw_scale);
    if (&pc_drop_shadow_mode)
        fprintf(f, "drop_shadows %d\n", pc_drop_shadow_mode);
    pc_lighting_settings_write(f);
    fprintf(f, "ui_upscale %s\n", pc_ui_upscale_names[pc_ui_upscale ? 1 : 0]);
    fprintf(f, "gameplay_walls %s\n", pc_gameplay_walls ? "smart" : "original");
    fprintf(f, "lighting_packs %s\n", lighting::packs_setting().c_str());
    fprintf(f, "lighting_active_pack %s\n", lighting::active_pack_setting().c_str());
    for (const Entry& e : entries) {
        fprintf(f, "%s", e.key);
        for (int i = 0; i < e.count; i++) {
            if (e.ints)
                fprintf(f, " %d", e.ints[i]);
            else if (e.bools)
                fprintf(f, " %d", e.bools[i] ? 1 : 0);
            else
                fprintf(f, " %g", e.floats[i]);
        }
        fputc('\n', f);
    }
}

/* Apply settings lines now (the editor's undo: the lines that differ, a
 * lighting_preset line first), then let the lighting quality settle. */
extern "C" void pc_settings_apply(const char* text)
{
    int scale = 0;
    const char* p = text;
    while (*p) {
        const char* end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[512];
        snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line - 1 ? n : sizeof line - 1), p);
        apply_line(line, &scale, true);
        p += n + (end ? 1 : 0);
    }
    if (scale > 0 && scale != pc_hw_scale)
        pc_hw_set_scale(scale);
    pc_lighting_sync();
}

extern "C" void pc_settings_save(void)
{
    const char* file = path();
    if (!file) {
        snprintf(status, sizeof status, "Settings are not saved (ROOD_SETTINGS unset; pc/run-game.sh sets it)");
        return;
    }
    char dir[256];
    snprintf(dir, sizeof dir, "%s", file);
    if (char* slash = strrchr(dir, '/')) {
        *slash = 0;
        mkdir(dir, 0777);
    }
    FILE* f = fopen(file, "w");
    if (!f) {
        snprintf(status, sizeof status, "Cannot write %s", file);
        return;
    }
    fprintf(f, "# rood-boi user settings (pc/src/project_settings.cpp); per-room values live in lighting/ and pc/user/lighting\n");
    pc_settings_write(f);
    fclose(f);
    loaded_mtime = mtime_of(file);
    snprintf(status, sizeof status, "Saved settings to %s", file);
}

/* Reload when the file was changed by something else (an editor, a script):
 * called about twice a second. */
extern "C" void pc_settings_poll(void)
{
    const char* file = path();
    if (!file)
        return;
    long long m = mtime_of(file);
    if (m && m != loaded_mtime) {
        pc_settings_load();
        snprintf(status + strlen(status), sizeof status - strlen(status), " (changed on disk)");
    }
}
