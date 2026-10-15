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
#include <map>
#include <string>

extern "C" {
extern int pc_gl_filter, pc_gl_aspect, pc_gte_widescreen, pc_hw_perspective, pc_hw_subpixel,
    pc_hw_interpolate, pc_hw_texture_filter, pc_hw_scale, pc_hw_scale_setting, pc_interp_skeleton, pc_interp_holds, pc_interp_uv,
    pc_present_rate;
int pc_hw_set_scale(int scale);
extern int pc_gi_enabled, pc_rt_shadows, pc_baked_shadows, pc_shadow_samples, pc_gi_bounces, pc_gi_rays,
    pc_fog_steps, pc_gi_denoise, pc_baked_estimate, pc_glow_shadows;
extern float pc_shadow_history, pc_gi_resolution;
extern int pc_lights_enabled;
extern float pc_shadow_softness, pc_normal_smoothing;
extern int pc_drop_shadow_mode __attribute__((weak)); // battle builds (actor_shadow.c)
extern int pc_native_room, pc_native_occlusion, pc_native_actors, pc_native_actor_order, pc_native_blend,
    pc_native_objects, pc_native_shadows, pc_native_effects, pc_reconstruct, pc_native_room_order,
    pc_room_texture_lod;
extern int pc_editor_hide_masks, pc_editor_game_cutaway, pc_editor_cutaway, pc_editor_cutaway_auto;
extern float pc_editor_cutaway_radius, pc_editor_cutaway_opacity;
extern int pc_lights_snap;
extern int pc_env_global; /* environment.cpp */
extern int pc_fog_global; /* atmosphere.cpp */
extern float pc_fog_disturb[4]; /* atmosphere.cpp: the interactive fog's weights per source */
extern int pc_fog_sim_budget;
extern int pc_fog_volume_render, pc_fog_volume_steps, pc_fog_volume_steps_gl, pc_fog_volume_shadows; /* atmosphere.cpp */
extern float pc_fog_volume_resolution, pc_fog_volume_history, pc_fog_volume_ambient, pc_fog_volume_glow,
    pc_fog_volume_scattering, pc_fog_volume_halo;
extern float pc_lights_snap_move, pc_lights_snap_angle;
/* Gameplay options, read by the game side. qol_weapon_slots: the weapon
 * slots quick swap on L2 + R2 (weapon_slots.c, battle builds), off by default. */
int pc_qol_weapon_slots;
/* skip_intro: start at the title, without the publisher and developer logos
 * and the intro movie (boot_runtime.c); off by default (the original start). */
int pc_skip_intro;
/* qol_map_invert: the map screen pans the other way on both axes (MENU5);
 * qol_fp_invert_y: up/down look the other way in the first-person view
 * (BATTLE func_8006EC7C), the D-pad and the right stick alike. Off by
 * default. */
int pc_qol_map_invert, pc_qol_fp_invert_y;
/* qol_skip_charging: battle charging (a spell's casting, an item's or a
 * Break Art's wind-up before it takes effect) runs at several vsyncs per
 * displayed field, the game logic unchanged (boot_window.c): 0 off (the
 * original pace, default), 1 Fast (4x), 2 Skip (as fast as the machine runs). */
int pc_qol_skip_charging;

/* Modern menus (pc/src/ui, pc/docs/Modern_Menus.md): 0 the game's own
 * menus, 1 the modern shell. Off by default until the screens are verified. */
int pc_ui_menus;
/* Modern menus: the opacity of their backdrop over the game and their panels
 * (System > Interface > Menu opacity; ui/ui_theme.cpp theme()), 0..1. The
 * default is the theme's own ground alpha (0xF0): the menus' look before the
 * setting. */
float pc_ui_menu_opacity = 0.94f;
/* Modern menus: the backdrop and panel colour (System > Interface > Menu
 * tint; ui/ui_tint.h, ui/ui_theme.cpp theme()). 0 Lea Monde = the theme
 * file's own colours (the default). */
int pc_ui_menu_tint;                         /* PC_UI_TINT_* */
int pc_ui_menu_tint_custom[3] = { 8, 7, 7 }; /* Custom's colour, 0..31 per channel */
}
#include "camera_stick.h" /* the right-stick camera (camera_stick_*) */
#include "editor_overlays.h"
#include "gameplay_walls.h" /* gameplay_walls original | smart, gameplay_walls_ghost, _opacity, _radius */
#include "lighting_quality.h" /* the lighting quality keys (lighting_preset, gi_method, shadows, ...) */
#include "probe_gi.h"
#include "ui_upscale.h" /* 2D / UI: ui_upscale nearest | mmpx, ui_subpixel */
#include "ui_text.h"    /* 2D / UI: ui_text original | font, ui_text_<style>_font ... */
#include "ui_bubbles.h" /* 2D / UI: ui_lines, ui_outline_boost, ui_bubbles, ui_bubble_outline */
#include "button_prompts.h" /* button_prompts auto | ...; bind_key_* / bind_pad_* */
#include "antialias.h" /* aa (msaa4+smaa ...), aa_downsample */
#include "lighting_layers.h" /* lighting_packs, lighting_active_pack */
#include "platform.h"
#include "ui/ui_tint.h" /* ui_menu_tint lea_monde | classic | ... | custom, ui_menu_tint_custom r g b */
#include "post.h" /* post_* (post profiles; the old post_flags, focus_mode, ... still load) */
extern bool pc_lights_show_gizmos;
namespace rood_lights {
extern int animate; /* light actors' flicker and pulse (lights.cpp) */
}

/* System > Display / Audio (pc/src/ui/ui_settings.h hooks): window_mode
 * windowed | borderless | fullscreen (boot_window.c), master_volume 0..100
 * (audio_output.c), brightness 0..100, 50 unchanged (gl_present.c). Applied
 * live through their setters; weak for builds without them. */
extern "C" {
int pc_window_mode_get(void) __attribute__((weak));
void pc_window_mode_set(int mode) __attribute__((weak));
int pc_master_volume_get(void) __attribute__((weak));
void pc_master_volume_set(int volume) __attribute__((weak));
int pc_brightness_get(void) __attribute__((weak));
void pc_brightness_set(int value) __attribute__((weak));
}
static const char* const window_mode_names[3] = { "windowed", "borderless", "fullscreen" };

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
    I("ui_outline_boost", pc_ui_outline_boost),
    F("ui_bubble_outline", pc_ui_bubble_outline),
    I("native_room", pc_native_room),
    I("native_occlusion", pc_native_occlusion),
    I("native_room_order", pc_native_room_order),
    I("room_texture_lod", pc_room_texture_lod),
    I("native_actors", pc_native_actors),
    I("native_actor_order", pc_native_actor_order),
    I("native_blend", pc_native_blend),
    I("native_objects", pc_native_objects),
    I("native_shadows", pc_native_shadows),
    I("native_effects", pc_native_effects),
    I("reconstruct", pc_reconstruct),
    I("baked_estimate", pc_baked_estimate),
    F("shadow_history", pc_shadow_history),
    I("haze_steps", pc_fog_steps),
    /* Probe GI's own settings (probe_gi.h; gi_method, probe_spacing and probe_rays are lighting quality keys). */
    F("probe_hysteresis", pc_probe_hysteresis),
    I("probe_budget", pc_probe_budget),
    I("probe_show", pc_probe_show),
    I("light_animation", rood_lights::animate),
    I("environment", pc_env_global), /* the rooms' sun and sky (environment.cpp) */
    I("fog", pc_fog_global), /* the rooms' ground fog and fog volumes (atmosphere.cpp) */
    /* The interactive fog (fog_sim.h): each disturbance source's weight (0 off; a volume's disturb_* override). */
    F("fog_disturb_player", pc_fog_disturb[0]),
    F("fog_disturb_actors", pc_fog_disturb[1]),
    F("fog_disturb_attacks", pc_fog_disturb[2]),
    F("fog_disturb_spells", pc_fog_disturb[3]),
    I("fog_sim_budget", pc_fog_sim_budget), /* the most simulated volumes per room (0..4) */
    /* The fog volumes' own renderer (atmosphere.h): on, its resolution, samples, history, light. */
    I("fog_volume_render", pc_fog_volume_render),
    F("fog_volume_resolution", pc_fog_volume_resolution),
    I("fog_volume_steps", pc_fog_volume_steps),
    I("fog_volume_steps_gl", pc_fog_volume_steps_gl),
    F("fog_volume_history", pc_fog_volume_history),
    F("fog_volume_ambient", pc_fog_volume_ambient),
    F("fog_volume_glow", pc_fog_volume_glow),
    F("fog_volume_scattering", pc_fog_volume_scattering),
    F("fog_volume_halo", pc_fog_volume_halo),
    I("fog_volume_shadows", pc_fog_volume_shadows),
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
    I("qol_map_invert", pc_qol_map_invert),
    I("qol_fp_invert_y", pc_qol_fp_invert_y),
    /* Gameplay. */
    I("qol_weapon_slots", pc_qol_weapon_slots),
    I("qol_skip_charging", pc_qol_skip_charging), /* 0 off, 1 fast, 2 skip */
    I("skip_intro", pc_skip_intro),
    I("ui_menus", pc_ui_menus),
    F("ui_menu_opacity", pc_ui_menu_opacity),
};
const char* path()
{
    const char* p = getenv("ROOD_SETTINGS");
    return p && *p ? p : nullptr;
}
long long loaded_mtime;
bool loaded_once;
char status[200];

long long mtime_of(const char* file)
{
    struct stat st;
    return stat(file, &st) ? 0 : PC_MTIME_NS(st);
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
    if (pc_post_settings_parse(key, rest))
        return 1;
    if (!strcmp(key, "aa")) { /* off | fxaa | smaa, msaa2/4/8, ssaa[factor], joined by '+' */
        char text[64] = "";
        sscanf(rest, "%63s", text);
        if (!pc_aa_parse(text))
            fprintf(stderr, "[settings] aa: unknown value '%s' (off, fxaa, smaa, msaa2/4/8, ssaa[factor], joined by '+')\n",
                text);
        return 1;
    }
    if (!strcmp(key, "antialiasing")) { /* before aa: 1 FXAA, 0 off */
        pc_gl_aa = atoi(rest) ? PC_AA_FXAA : PC_AA_OFF;
        return 1;
    }
    if (!strcmp(key, "aa_downsample")) { /* area | lanczos | bilinear */
        char name[32] = "";
        sscanf(rest, "%31s", name);
        for (int k = 0; k < 3; k++)
            if (!strcmp(name, pc_aa_downsample_names[k]))
                pc_aa_downsample = k;
        return 1;
    }
    if (!strcmp(key, "ui_upscale")) { /* nearest | mmpx (or 0 / 1) */
        char name[32] = "";
        sscanf(rest, "%31s", name);
        for (int k = 0; k < 2; k++)
            if (!strcmp(name, pc_ui_upscale_names[k]) || (name[0] == '0' + k && !name[1]))
                pc_ui_upscale = k;
        return 1;
    }
    if (pc_ui_text_setting(key, rest))
        return 1;
    if (!strcmp(key, "ui_lines") || !strcmp(key, "ui_bubbles")) { /* joined | legacy, original | vector (or 0 / 1) */
        char name[32] = "";
        sscanf(rest, "%31s", name);
        int lines = !strcmp(key, "ui_lines");
        const char* const* names = lines ? pc_ui_lines_names : pc_ui_bubbles_names;
        for (int k = 0; k < 2; k++)
            if (!strcmp(name, names[k]) || (name[0] == '0' + k && !name[1]))
                *(lines ? &pc_ui_lines : &pc_ui_bubbles) = k;
        return 1;
    }
    if (pc_button_prompts_setting(key, rest))
        return 1;
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
    if (!strcmp(key, "window_mode")) { /* windowed | borderless | fullscreen (or 0 / 1 / 2); else ignored */
        char name[32] = "", extra[2];
        if (sscanf(rest, "%31s %1s", name, extra) != 1) {
            fprintf(stderr, "[settings] window_mode: expected windowed, borderless or fullscreen\n");
            return 1;
        }
        for (int k = 0; k < 3; k++)
            if ((!strcmp(name, window_mode_names[k]) || (name[0] == '0' + k && !name[1])) && pc_window_mode_set)
                pc_window_mode_set(k);
        return 1;
    }
    if (!strcmp(key, "master_volume") || !strcmp(key, "brightness")) { /* 0..100 (clamped); else ignored */
        int value;
        char extra[2];
        if (sscanf(rest, "%d %1s", &value, extra) != 1) {
            fprintf(stderr, "[settings] %s: expected a number from 0 to 100\n", key);
            return 1;
        }
        if (key[0] == 'm' && pc_master_volume_set)
            pc_master_volume_set(value);
        else if (key[0] == 'b' && pc_brightness_set)
            pc_brightness_set(value);
        return 1;
    }
    if (!strcmp(key, "ui_menu_tint")) { /* lea_monde | classic | slate | crimson | verdant | custom (or 0..5) */
        char name[32] = "", extra[2];
        int tint = sscanf(rest, "%31s %1s", name, extra) == 1 ? pc_ui_tint_from_name(name) : -1;
        if (tint < 0)
            fprintf(stderr, "[settings] ui_menu_tint: expected lea_monde, classic, slate, crimson, verdant or custom\n");
        else
            pc_ui_menu_tint = tint;
        return 1;
    }
    if (!strcmp(key, "ui_menu_tint_custom")) { /* r g b, 0..31 each (clamped) */
        int rgb[3];
        char extra[2];
        if (sscanf(rest, "%d %d %d %1s", &rgb[0], &rgb[1], &rgb[2], extra) != 3) {
            fprintf(stderr, "[settings] ui_menu_tint_custom: expected red, green and blue from 0 to 31\n");
            return 1;
        }
        for (int c = 0; c < 3; c++)
            pc_ui_menu_tint_custom[c] = rgb[c] < 0 ? 0 : rgb[c] > PC_UI_TINT_MAX ? PC_UI_TINT_MAX : rgb[c];
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

/* ---- Environment overrides ----
 * Some environment variables override a saved key for this run
 * (ROOD_WIDESCREEN, ROOD_PERSPECTIVE, ROOD_SCALE, ROOD_AA,
 * ROOD_LIGHTING_PRESET, ROOD_GI=0, ROOD_PRESENT_RATE, ROOD_NATIVE_*,
 * ROOD_RECONSTRUCT). The code that applies one calls
 * pc_settings_env_override(key) right after: a save then writes the key as
 * the file had it (or leaves it out when the file did not have it), so the
 * override never becomes the user's setting. Unless the setting changed
 * since: when the line it would write differs from the one the variable gave
 * (any change, from System, the editor or the RPC), or System changed it
 * (pc_settings_touched; also a change back to the variable's value). Keys
 * whose value the variable only sets indirectly (ROOD_GI=0, marked in
 * pc_settings_load) use pc_settings_env_override_flag: they keep the file's
 * value unless System changes them. */
extern "C" void pc_settings_write(FILE* f);
namespace {
struct EnvOverride {
    std::string line; /* the key's line right after the variable applied ("": compare nothing) */
};
std::map<std::string, EnvOverride> env_overrides;
std::map<std::string, std::string> file_lines; /* key -> its line in the file, as last loaded */
std::string line_key(const std::string& line)
{
    size_t end = line.find_first_of(" \t");
    return line.substr(0, end);
}
/* The key's line(s) as pc_settings_write gives them now ("" when it writes none). */
std::string current_line(const char* key)
{
    size_t size = 0;
    char* text = pc_capture_file(pc_settings_write, &size);
    std::string found;
    for (const char* p = text; p && *p;) {
        const char* end = strchr(p, '\n');
        std::string line(p, end ? (size_t)(end - p) : strlen(p));
        if (line_key(line) == key)
            found = line;
        p = end ? end + 1 : p + line.size();
    }
    free(text);
    return found;
}
} // namespace
extern "C" void pc_settings_env_override(const char* key)
{
    if (!env_overrides.count(key))
        env_overrides[key].line = current_line(key);
}
extern "C" void pc_settings_env_override_flag(const char* key)
{
    if (!env_overrides.count(key))
        env_overrides[key].line.clear();
}
extern "C" void pc_settings_touched(const char* key) { env_overrides.erase(key); }

extern "C" const char* pc_settings_status(void) { return status; }
extern "C" const char* pc_settings_path(void) { return path(); }

/* Load the settings file (startup, and again when it changes on disk). */
extern "C" void pc_settings_load(void)
{
    const char* file = path();
    if (!file)
        return;
    /* ROOD_GI=0 (ptgi.c) turns the path tracer off when it first starts, and lighting_quality.cpp derives
     * these keys from that: marked here, whether or not the tracer ever starts this run. */
    const char* gi = getenv("ROOD_GI");
    if (gi && *gi == '0')
        for (const char* key : { "gi_method", "shadows", "characters_shadow_baked" })
            pc_settings_env_override_flag(key);
    FILE* f = fopen(file, "r");
    loaded_mtime = mtime_of(file);
    if (!f) {
        snprintf(status, sizeof status, "No settings saved yet (%s)", file);
        pc_lighting_settings_loaded(0); /* first run: detect the lighting quality preset */
        pc_aa_autodetect_pending = 1; /* and the anti-aliasing */
        return;
    }
    char line[512];
    int applied = 0, scale = 0, aa_seen = 0;
    file_lines.clear();
    while (fgets(line, sizeof line, f)) {
        std::string text = line;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        if (!text.empty() && text[0] != '#')
            file_lines[line_key(text)] = text; /* the environment overrides' saved values (above) */
        applied += apply_line(line, &scale, false);
        aa_seen |= !strncmp(line, "aa ", 3) || !strncmp(line, "antialiasing ", 13);
    }
    if (!aa_seen && !loaded_once)
        pc_aa_autodetect_pending = 1;
    loaded_once = true;
    fclose(f);
    pc_lighting_settings_loaded(1);
    if (scale > 0 && scale != pc_hw_scale_setting)
        pc_hw_set_scale(scale);
    snprintf(status, sizeof status, "Loaded %d settings from %s", applied, file);
}

/* Every setting as "key value..." lines (the file's body; the editor's undo
 * compares them). */
extern "C" void pc_settings_write(FILE* f)
{
    fprintf(f, "internal_resolution %d\n", pc_hw_scale_setting);
    fprintf(f, "aa %s\n", pc_aa_string());
    fprintf(f, "aa_downsample %s\n", pc_aa_downsample_names[pc_aa_downsample]);
    if (&pc_drop_shadow_mode)
        fprintf(f, "drop_shadows %d\n", pc_drop_shadow_mode);
    pc_lighting_settings_write(f);
    fprintf(f, "ui_upscale %s\n", pc_ui_upscale_names[pc_ui_upscale ? 1 : 0]);
    pc_ui_text_settings_write(f);
    fprintf(f, "ui_lines %s\n", pc_ui_lines_names[pc_ui_lines ? 1 : 0]);
    fprintf(f, "ui_bubbles %s\n", pc_ui_bubbles_names[pc_ui_bubbles ? 1 : 0]);
    pc_button_prompts_settings_write(f);
    fprintf(f, "gameplay_walls %s\n", pc_gameplay_walls ? "smart" : "original");
    fprintf(f, "ui_menu_tint %s\n", pc_ui_tint_id(pc_ui_tint_current()));
    fprintf(f, "ui_menu_tint_custom %d %d %d\n", pc_ui_menu_tint_custom[0], pc_ui_menu_tint_custom[1],
        pc_ui_menu_tint_custom[2]);
    if (pc_window_mode_get)
        fprintf(f, "window_mode %s\n", window_mode_names[pc_window_mode_get() % 3]);
    if (pc_master_volume_get)
        fprintf(f, "master_volume %d\n", pc_master_volume_get());
    if (pc_brightness_get)
        fprintf(f, "brightness %d\n", pc_brightness_get());
    pc_post_settings_write(f);
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
    if (scale > 0 && scale != pc_hw_scale_setting)
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
        pc_mkdir(dir);
    }
    /* The lines, with the environment's overrides put back to the file's values (above). */
    size_t size = 0;
    char* text = pc_capture_file(pc_settings_write, &size);
    std::string body;
    for (const char* p = text; p && *p;) {
        const char* end = strchr(p, '\n');
        std::string line(p, end ? (size_t)(end - p) : strlen(p));
        p = end ? end + 1 : p + line.size();
        auto o = env_overrides.find(line_key(line));
        if (o != env_overrides.end()) {
            if (o->second.line.empty() || o->second.line == line) {
                auto saved = file_lines.find(o->first);
                if (saved != file_lines.end())
                    body += saved->second + "\n";
                continue;
            }
            env_overrides.erase(o); /* changed since: the user's now */
        }
        body += line + "\n";
    }
    free(text);
    /* Written whole to <file>.tmp, then put in place (never a half-written file). */
    std::string temp = std::string(file) + ".tmp";
    FILE* f = fopen(temp.c_str(), "w");
    if (!f) {
        snprintf(status, sizeof status, "Cannot write %s", temp.c_str());
        return;
    }
    fprintf(f, "# rood-boi user settings (pc/src/project_settings.cpp); per-room values live in lighting/ and pc/user/lighting\n");
    bool ok = fwrite(body.data(), 1, body.size(), f) == body.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || pc_replace_file(temp.c_str(), file)) {
        remove(temp.c_str());
        snprintf(status, sizeof status, "Cannot write %s", file);
        return;
    }
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
