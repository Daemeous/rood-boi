#include "hd_textures.h"
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
extern int pc_photo_full_room; /* room_native.c: a plugin's free camera sees the whole room */
extern float pc_editor_cutaway_radius, pc_editor_cutaway_opacity;
extern int pc_lights_snap;
extern int pc_lights_gizmo_space;
extern int pc_env_global; /* environment.cpp */
extern float pc_emissive; /* emissive.cpp: glowing surfaces' strength (0 off) */
/* emissive_editor.cpp: the texture view's look (editor_emissive_view_*) */
int pc_emissive_view_setting(const char* key, const char* rest);
void pc_emissive_view_settings_write(FILE* f);
extern int pc_fog_global; /* atmosphere.cpp */
/* cutscene.cpp: the cutscene player's marks and the instant replay */
extern int pc_take_interval, pc_take_marks_max, pc_take_budget_mb, pc_instant_replay, pc_instant_replay_seconds,
    pc_instant_replay_interval, pc_cutscene_auto_advance, pc_cutscene_dialogue_hold;
extern float pc_fog_disturb[4]; /* atmosphere.cpp: the interactive fog's weights per source */
extern int pc_fog_sim_budget;
extern int pc_fog_volume_render, pc_fog_volume_steps, pc_fog_volume_steps_gl, pc_fog_volume_shadows; /* atmosphere.cpp */
extern float pc_fog_volume_resolution, pc_fog_volume_history, pc_fog_volume_ambient, pc_fog_volume_glow,
    pc_fog_volume_scattering, pc_fog_volume_halo;
extern float pc_fog_detail_wind, pc_fog_detail_rise, pc_fog_flow_cycle; /* atmosphere.cpp */
extern int pc_dust, pc_particles_lights, pc_particles_glow_shadows, pc_particles_cpu_rays; /* particles.cpp */
extern float pc_dust_low, pc_particles_glow;
extern float pc_lights_snap_move, pc_lights_snap_angle;
/* Gameplay options, read by the game side. qol_weapon_slots: the weapon
 * slots quick swap on L2 + R2 (weapon_slots.c, battle builds), off by default. */
int pc_qol_weapon_slots;
/* Its look and timing (weapon_slots.c): qol_weapon_slots_name_ticks, how long
 * a swapped weapon's name stays up (game ticks); qol_weapon_slots_label<N>
 * (N 0..7: 0..3 the button group, 4..7 the D-pad group) a label's text x, y
 * and bullet x, y with the overlay slid in (PS1 pixels); qol_weapon_slots_hint
 * the x, y of the line under the overlay (what Select does). */
int pc_qol_weapon_slots_name_ticks = 150;
int pc_qol_weapon_slots_label[8][4] = {
    { 242, 99, 237, 101 }, { 214, 108, 213, 115 }, { 252, 111, 247, 113 }, { 223, 126, 223, 127 },
    { 94, 99, 89, 101 }, { 71, 112, 70, 115 }, { 103, 117, 98, 113 }, { 81, 124, 80, 127 },
};
int pc_qol_weapon_slots_hint[2] = { 160, 142 };
/* skip_intro: start at the title, without the publisher and developer logos
 * and the intro movie (boot_runtime.c); off by default (the original start). */
int pc_skip_intro;
/* qol_map_invert: the map screen pans the other way on both axes (MENU5);
 * qol_fp_invert_y: up/down look the other way in the first-person view
 * (BATTLE func_8006EC7C), the D-pad and the right stick alike. Off by
 * default. */
int pc_qol_map_invert, pc_qol_fp_invert_y;
/* qol_map_controls: the map screen's right stick and triggers (map_pad.h):
 * 0 Modern (default: the right stick turns and tilts, L2 / R2 zoom), 1
 * Original (the PlayStation's: L2 / R2 + right stick, the stick pans). */
int pc_qol_map_controls;
/* Enemy difficulty: pending user settings; active values live with game state. */
int pc_difficulty_base_percent, pc_difficulty_clear_percent;
/* Boot-only builds have no BATTLE unit. The strong game-owned definitions are snapshotted. */
int pc_difficulty_active_base __attribute__((weak)), pc_difficulty_active_clear __attribute__((weak)),
    pc_difficulty_active_completions __attribute__((weak));

/* qol_skip_charging: battle charging (a spell's casting, an item's or a
 * Break Art's wind-up before it takes effect) is skipped, the action's
 * outcome unchanged (charge_skip.c): 0 off (the original, default), 1 Skip
 * (older settings' 2 too: their Fast and Skip ran it faster). Whose actions:
 * qol_skip_charging_who 0 mine (Ashley's, default), 1 all (enemies' too).
 * qol_skip_charging_name: how long a skipped wind-up's name window stays up
 * at least, seconds (0..2, default 1; 0 closes it at the release, never
 * longer than the original's own 2 s).
 * qol_skip_charging_hold_mine / _hold_enemy: how much of a skipped wind-up's
 * hold is shown first, paced, from the camera's arrival at the caster,
 * seconds (0..1.5; never longer than the original's own hold): for Ashley's
 * actions (default 0) and for the enemies' (default 0.35: a beat to see the
 * spell or art coming, and not press into its release, charge_skip.inc). */
int pc_qol_skip_charging, pc_qol_skip_charging_who;
float pc_qol_skip_charging_name = 1.0f;
float pc_qol_skip_charging_hold_mine = 0.0f, pc_qol_skip_charging_hold_enemy = 0.35f;
/* qol_confirm_cross: Cross confirms and Circle cancels in menus and dialogs;
 * qol_field_cross_attack: in the field Cross draws the weapon and attacks,
 * Circle interacts and sheathes. Off by default; button_roles.c exchanges the
 * two buttons in the pad word where the game's context calls for it. */
int pc_qol_confirm_cross, pc_qol_field_cross_attack;
/* Fast travel (fast_travel.h, pc/docs/Fast_Travel.md): qol_fast_travel offers
 * the game's Teleportation from the first Circle of Magic once the prologue is
 * over (on by default; the game's own unlock always applies); before the
 * game's own unlock it costs qol_fast_travel_cost 0 free (default), 1 flat
 * (qol_fast_travel_flat MP), 2 scaled (qol_fast_travel_scale x the game's), 3
 * the game's. The Travel prompt on a Circle: qol_fast_travel_prompt, its
 * place in the game picture (fast_travel_prompt.cpp): the anchor (0 top left,
 * 1 top, 2 top right (default), 3 left, 4 centre, 5 right, 6 bottom left, 7
 * bottom, 8 bottom right; the plate's same corner or edge goes there), moved
 * by offset_x / _y (fractions of the picture's width / height, + right and
 * down), its size (x qol_fast_travel_prompt_size), and how many vsyncs it stays
 * after the field last offered travel (qol_fast_travel_prompt_hold, 8). */
int pc_qol_fast_travel = 1, pc_qol_fast_travel_cost = 0, pc_qol_fast_travel_flat = 15, pc_qol_fast_travel_prompt = 1,
    pc_qol_fast_travel_prompt_anchor = 2, pc_qol_fast_travel_prompt_hold = 8;
/* The Travel prompt's look (fast_travel_prompt.cpp): drawn like the HUD's
 * own words, the HUD labels' font (ui_text_labels_font), outlined, no plate
 * unless qol_fast_travel_prompt_backdrop; sizes in the menus' layout units
 * (720 high); colours red, green, blue, alpha 0..1; stretch 0: the HUD
 * labels' (ui_text_labels_stretch). Button prompts' icon (x the text size),
 * icon-to-verb gap and the verb's tracking (em), shared with the menus' hint
 * bar (ui/ui_draw.cpp): ui_prompt_icon, ui_prompt_gap, ui_prompt_tracking. */
int pc_qol_fast_travel_prompt_backdrop = 0;
float pc_qol_fast_travel_prompt_size = 18, pc_qol_fast_travel_prompt_stroke = 2.5f,
    pc_qol_fast_travel_prompt_stretch = 0, pc_qol_fast_travel_prompt_pad_x = 12, pc_qol_fast_travel_prompt_pad_y = 6,
    pc_qol_fast_travel_prompt_border = 1;
float pc_qol_fast_travel_prompt_color[4] = { 0.922f, 0.894f, 0.839f, 1.0f },
    pc_qol_fast_travel_prompt_stroke_color[4] = { 0.0f, 0.0f, 0.0f, 0.88f };
float pc_ui_prompt_icon = 1.6f, pc_ui_prompt_gap = 9, pc_ui_prompt_tracking = 0.22f;
float pc_qol_fast_travel_scale = 0.5f, pc_qol_fast_travel_prompt_offset_x = -0.025f,
    pc_qol_fast_travel_prompt_offset_y = 0.035f, pc_qol_fast_travel_prompt_scale = 1.0f;

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
#include <cstdio>
#include "occlusion.h" /* occlusion off | fade | fade_silhouette | silhouette, occlusion_* */
#include "lighting_quality.h" /* the lighting quality keys (lighting_preset, gi_method, shadows, ...) */
#include "probe_gi.h"
#include "ui_upscale.h" /* 2D / UI: ui_upscale nearest | mmpx, ui_subpixel */
#include "ui_text.h"    /* 2D / UI: ui_text original | font, ui_text_<style>_font ... */
#include "ui_bubbles.h" /* 2D / UI: ui_lines, ui_outline_boost, ui_bubbles, ui_bubble_outline */
#include "button_prompts.h" /* button_prompts auto | ...; bind_key_* / bind_pad_* */
#include "antialias.h" /* aa (msaa4+smaa ...), aa_downsample */
#include "lighting_layers.h" /* lighting_packs, lighting_active_pack */
#include "platform.h"
#include "display_confirm.h" /* risky display changes save their old values until kept */
#include "ui/ui_tint.h" /* ui_menu_tint lea_monde | classic | ... | custom, ui_menu_tint_custom r g b */
#include "post.h" /* post_* (post profiles; the old post_flags, focus_mode, ... still load) */
#include "editor_selection.h" /* editor_hidden_<zone>_<room>: objects hidden in the editor */
#include "editor_colour.h" /* editor_colour_tabs, editor_recent_colours */
#include "hud/hud.h" /* hud_classic_override */
#include "lua/lua_runtime.h" /* plugins_elevated, plugin_enabled, plugin_order, plugin_grant, plugin_budget_* */
#include "editor_fit_window.h" /* editor_fit_window_offset, _kelvin */
#include "editor_library.h" /* editor_library_view, _docked, _window, _favourites */
extern bool pc_lights_show_gizmos;
extern "C" int pc_save_states; /* savestate_slots.c: the save state slots' keys act (0 off, default) */
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
/* System > Display back-ends (boot_window.c): display, fullscreen_mode, vsync, remember_window, window_rect. */
const char* pc_display_setting(void) __attribute__((weak));
void pc_display_choose(const char* value) __attribute__((weak));
const char* pc_fullscreen_mode_setting(void) __attribute__((weak));
void pc_fullscreen_mode_choose(const char* value) __attribute__((weak));
int pc_vsync_get(void) __attribute__((weak));
void pc_vsync_set(int on) __attribute__((weak));
int pc_window_remember_get(void) __attribute__((weak));
void pc_window_remember_set(int on) __attribute__((weak));
int pc_window_rect_get(int out[4]) __attribute__((weak));
void pc_window_rect_set(const int r[4]) __attribute__((weak));
extern int pc_hw_scale_auto; /* gpu_hw.c: internal_resolution auto ("Match window") */
/* card1 / card2: the memory card files (memory_card.c), "" for pc/save/cardN.mcd. */
void pc_memory_card_set_path(int slot, const char* path);
const char* pc_memory_card_path_setting(int slot);
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
#define IN(key, var, n) { key, var, nullptr, n, nullptr }
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
    I("hd_textures", pc_hd_enabled),
    I("ui_subpixel", pc_ui_subpixel),
    I("ui_outline_boost", pc_ui_outline_boost),
    F("ui_bubble_outline", pc_ui_bubble_outline),
    I("native_room", pc_native_room),
    I("native_occlusion", pc_native_occlusion),
    I("native_room_order", pc_native_room_order),
    I("room_texture_lod", pc_room_texture_lod),
    I("cutscene_mark_interval", pc_take_interval),
    I("cutscene_marks_max", pc_take_marks_max),
    I("cutscene_budget_mb", pc_take_budget_mb),
    I("instant_replay", pc_instant_replay),
    I("instant_replay_seconds", pc_instant_replay_seconds),
    I("instant_replay_interval", pc_instant_replay_interval),
    I("cutscene_auto_advance", pc_cutscene_auto_advance),
    I("cutscene_dialogue_hold", pc_cutscene_dialogue_hold),
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
    F("emissive", pc_emissive), /* glowing surfaces (emissive.cpp, pc/docs/Emissive.md): strength 0..2, 0 off */
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
    /* The fog's detail noise in motion (atmosphere.h): drift x the wind, rise per second, the flow map's cycle. */
    F("fog_detail_wind", pc_fog_detail_wind),
    F("fog_detail_rise", pc_fog_detail_rise),
    F("fog_flow_cycle", pc_fog_flow_cycle),
    /* Particles (particles.h, pc/docs/Particles.md): Dust (0 off, 1 low, 2 high), Low's share, the glows'
     * light on them, sources per mote, glow shadows, OpenGL's shadow rays a frame. */
    I("dust", pc_dust),
    F("dust_low", pc_dust_low),
    F("particles_glow", pc_particles_glow),
    I("particles_lights", pc_particles_lights),
    I("particles_glow_shadows", pc_particles_glow_shadows),
    I("particles_cpu_rays", pc_particles_cpu_rays),
    F("normal_smoothing", pc_normal_smoothing),
    I("gameplay_walls_ghost", pc_gameplay_walls_ghost),
    F("gameplay_walls_opacity", pc_gameplay_walls_opacity),
    F("gameplay_walls_radius", pc_gameplay_walls_radius),
    I("gameplay_walls_fade_out", pc_gameplay_walls_fade_out), /* game frames */
    I("gameplay_walls_fade_in", pc_gameplay_walls_fade_in),
    I("gameplay_walls_clear", pc_gameplay_walls_clear),
    I("editor_hide_masks", pc_editor_hide_masks),
    I("editor_game_cutaway", pc_editor_game_cutaway),
    I("editor_cutaway", pc_editor_cutaway),
    F("editor_cutaway_radius", pc_editor_cutaway_radius),
    F("editor_cutaway_opacity", pc_editor_cutaway_opacity),
    I("editor_cutaway_auto", pc_editor_cutaway_auto),
    I("photo_full_room", pc_photo_full_room),
    /* Editor viewport (editor_overlays.h) and snapping. */
    B("editor_show_lights", pc_lights_show_gizmos),
    I("editor_wireframe", pc_overlay_wire),
    I("editor_points", pc_overlay_points),
    I("editor_character_wireframe", pc_overlay_actor_wire),
    I("editor_snap_show", pc_overlay_snap_show),
    F("editor_occluded_opacity", pc_overlay_occluded),
    I("editor_snap_grid", pc_lights_snap),
    I("editor_gizmo_space", pc_lights_gizmo_space), /* 0 world, 1 local (move and rotate) */
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
    I("qol_map_controls", pc_qol_map_controls),
    I("qol_fp_invert_y", pc_qol_fp_invert_y),
    I("qol_confirm_cross", pc_qol_confirm_cross),
    I("qol_field_cross_attack", pc_qol_field_cross_attack),
    /* Gameplay. */
    I("qol_weapon_slots", pc_qol_weapon_slots),
    I("qol_weapon_slots_name_ticks", pc_qol_weapon_slots_name_ticks), /* game ticks */
    IN("qol_weapon_slots_label0", pc_qol_weapon_slots_label[0], 4), /* text x y, bullet x y */
    IN("qol_weapon_slots_label1", pc_qol_weapon_slots_label[1], 4),
    IN("qol_weapon_slots_label2", pc_qol_weapon_slots_label[2], 4),
    IN("qol_weapon_slots_label3", pc_qol_weapon_slots_label[3], 4),
    IN("qol_weapon_slots_label4", pc_qol_weapon_slots_label[4], 4),
    IN("qol_weapon_slots_label5", pc_qol_weapon_slots_label[5], 4),
    IN("qol_weapon_slots_label6", pc_qol_weapon_slots_label[6], 4),
    IN("qol_weapon_slots_label7", pc_qol_weapon_slots_label[7], 4),
    IN("qol_weapon_slots_hint", pc_qol_weapon_slots_hint, 2),
    I("difficulty_base_percent", pc_difficulty_base_percent),
    I("difficulty_clear_percent", pc_difficulty_clear_percent),
    I("qol_skip_charging", pc_qol_skip_charging), /* 0 off, 1 skip (apply_line: older 2 is 1) */
    I("qol_skip_charging_who", pc_qol_skip_charging_who), /* 0 mine, 1 all */
    F("qol_skip_charging_name", pc_qol_skip_charging_name), /* seconds, 0..2 */
    F("qol_skip_charging_hold_mine", pc_qol_skip_charging_hold_mine), /* seconds, 0..1.5 */
    F("qol_skip_charging_hold_enemy", pc_qol_skip_charging_hold_enemy), /* seconds, 0..1.5 */
    I("qol_fast_travel", pc_qol_fast_travel),
    I("qol_fast_travel_cost", pc_qol_fast_travel_cost), /* 0 free, 1 flat, 2 scaled, 3 game */
    I("qol_fast_travel_flat", pc_qol_fast_travel_flat),
    F("qol_fast_travel_scale", pc_qol_fast_travel_scale),
    I("qol_fast_travel_prompt", pc_qol_fast_travel_prompt),
    I("qol_fast_travel_prompt_anchor", pc_qol_fast_travel_prompt_anchor), /* 0..8, 2 top right */
    F("qol_fast_travel_prompt_offset_x", pc_qol_fast_travel_prompt_offset_x), /* fractions of the picture */
    F("qol_fast_travel_prompt_offset_y", pc_qol_fast_travel_prompt_offset_y),
    F("qol_fast_travel_prompt_scale", pc_qol_fast_travel_prompt_scale),
    I("qol_fast_travel_prompt_hold", pc_qol_fast_travel_prompt_hold), /* vsyncs after the last poll */
    I("qol_fast_travel_prompt_backdrop", pc_qol_fast_travel_prompt_backdrop), /* the menus' hint plate */
    F("qol_fast_travel_prompt_size", pc_qol_fast_travel_prompt_size), /* layout units */
    F("qol_fast_travel_prompt_stroke", pc_qol_fast_travel_prompt_stroke), /* the outline (0: none) */
    F("qol_fast_travel_prompt_stretch", pc_qol_fast_travel_prompt_stretch), /* 0: the HUD labels' */
    F("qol_fast_travel_prompt_pad_x", pc_qol_fast_travel_prompt_pad_x), /* the plate's margins */
    F("qol_fast_travel_prompt_pad_y", pc_qol_fast_travel_prompt_pad_y),
    F("qol_fast_travel_prompt_border", pc_qol_fast_travel_prompt_border), /* the plate's rule */
    FN("qol_fast_travel_prompt_color", pc_qol_fast_travel_prompt_color, 4), /* r g b a, 0..1 */
    FN("qol_fast_travel_prompt_stroke_color", pc_qol_fast_travel_prompt_stroke_color, 4),
    F("ui_prompt_icon", pc_ui_prompt_icon), /* x the text size */
    F("ui_prompt_gap", pc_ui_prompt_gap), /* layout units */
    F("ui_prompt_tracking", pc_ui_prompt_tracking), /* em */
    I("skip_intro", pc_skip_intro),
    I("ui_menus", pc_ui_menus),
    I("hud_classic_override", pc_hud_classic_override), /* HUD claims honoured with ui_menus 0 too (hud.h) */
    /* the classic look drawn in Lua per kind (a claim made for the classic flavour: rood.classic; hud.h);
     * on for the kinds through the parity gate (pc/src/classic/classic_parity.h) */
    I("hud_classic_status", pc_hud_classic_status),
    I("hud_classic_chain", pc_hud_classic_chain),
    I("hud_classic_buff", pc_hud_classic_buff),
    I("hud_classic_damage", pc_hud_classic_damage),
    I("hud_classic_gauges", pc_hud_classic_gauges),
    I("hud_classic_figure", pc_hud_classic_figure),
    I("hud_classic_status_icons", pc_hud_classic_status_icons),
    I("hud_classic_battle_mode", pc_hud_classic_battle_mode),
    I("hud_classic_ability_icons", pc_hud_classic_ability_icons),
    I("save_states", pc_save_states), /* the save state slots (savestate_slots.c), 0 off */
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
    if (!strcmp(key, "internal_resolution")) { /* 1..8, or auto: the window's resolution (-1 here) */
        char word[16] = "";
        sscanf(rest, "%15s", word);
        *scale = !strcmp(word, "auto") ? -1 : atoi(rest);
        return 0;
    }
    /* the display back-ends (boot_window.c; their own parsing of the value) */
    if (!strcmp(key, "display") || !strcmp(key, "fullscreen_mode")) {
        std::string value = rest;
        size_t a = value.find_first_not_of(" \t\r\n"), b = value.find_last_not_of(" \t\r\n");
        value = a == std::string::npos ? std::string() : value.substr(a, b - a + 1);
        if (key[0] == 'd' && pc_display_choose)
            pc_display_choose(value.c_str());
        else if (key[0] == 'f' && pc_fullscreen_mode_choose)
            pc_fullscreen_mode_choose(value.c_str());
        return 1;
    }
    if (!strcmp(key, "vsync") || !strcmp(key, "remember_window")) { /* 0 | 1 */
        int on = atoi(rest) != 0;
        if (key[0] == 'v' && pc_vsync_set)
            pc_vsync_set(on);
        else if (key[0] == 'r' && pc_window_remember_set)
            pc_window_remember_set(on);
        return 1;
    }
    if (!strcmp(key, "window_rect")) { /* x y w h: where the window was last left windowed */
        int r[4];
        if (sscanf(rest, "%d %d %d %d", &r[0], &r[1], &r[2], &r[3]) == 4 && pc_window_rect_set)
            pc_window_rect_set(r);
        return 1;
    }
    if (!strcmp(key, "qol_fast_travel_prompt_x") || !strcmp(key, "qol_fast_travel_prompt_y"))
        return 0; /* before the anchor: the centre in fractions of the window; dropped (the anchor's default) */
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
    if (pc_occlusion_setting(key, rest)) /* occlusion off | fade | fade_silhouette | silhouette, occlusion_* */
        return 1;
    if (pc_lua_settings_parse(key, rest))
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
        /* The same blanks at both ends: an empty value ("lighting_active_pack \n",
         * as saved when no pack is active) is "", not "\n" (that changed the
         * lighting config, and the room reloaded and lost its undo history). */
        size_t a = value.find_first_not_of(" \t\r\n"), b = value.find_last_not_of(" \t\r\n");
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
    if (!strncmp(key, "editor_hidden_", 14)) /* editor state: objects hidden in a room (editor_selection.h) */
        return rood_selection::parse_hidden(key, rest) ? 1 : 0;
    if (!strcmp(key, "camera_stick_menus")) { /* menu contexts, space separated (camera_stick.h); "none" */
        pc_camera_stick_menus_set(rest);
        return 1;
    }
    if (!strcmp(key, "card1") || !strcmp(key, "card2")) { /* the rest of the line: a path, spaces allowed */
        pc_memory_card_set_path(key[4] - '1', rest);
        return 1;
    }
    if (rood_library::parse_setting(key, rest)) /* editor state: the library's view, place and favourites */
        return 1;
    if (rood_fit::parse_setting(key, rest)) /* editor tools: fit to window's offset and colour */
        return 1;
    if (rood_colour::parse_setting(key, rest)) /* editor state: the colour popover's tabs and recent colours */
        return 1;
    if (pc_emissive_view_setting(key, rest)) /* editor tools: the emissive texture view's look */
        return 1;
    if (!strcmp(key, "qol_skip_charging")) { /* 0 off, else skip (older settings: 1 fast, 2 skip) */
        char* end;
        long v = strtol(rest, &end, 10);
        if (end != rest)
            pc_qol_skip_charging = v != 0;
        return 1;
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
/* A key's value as the settings file has it now (read from the file, not the live settings: the window is
 * placed before the settings load, boot_window.c pc_window_place); 0 when the file lacks it. */
extern "C" int pc_settings_peek(const char* key, char* out, int size)
{
    const char* file = path();
    FILE* f = file ? fopen(file, "r") : nullptr;
    char line[512];
    int found = 0;
    size_t n = strlen(key);
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, key, n) && (line[n] == ' ' || line[n] == '\t')) {
            std::string value = line + n;
            size_t a = value.find_first_not_of(" \t\r\n"), b = value.find_last_not_of(" \t\r\n");
            snprintf(out, (size_t)size, "%s", a == std::string::npos ? "" : value.substr(a, b - a + 1).c_str());
            found = 1; /* (the last one wins, as the loader's) */
        }
    if (f)
        fclose(f);
    return found;
}
/* RPC settings {get}: the key's line as pc_settings_write gives it now; 0 when it writes none. */
extern "C" int pc_settings_line(const char* key, char* out, int size)
{
    std::string line = current_line(key);
    snprintf(out, (size_t)size, "%s", line.c_str());
    return !line.empty();
}

/* A plain numeric key's values (the entries table: qol_fast_travel_prompt_size, ...; rood.settings
 * reads them, lua_api_settings.cpp): how many (0: not one of those), at most max in values. */
extern "C" int pc_settings_numbers(const char* key, double* values, int max)
{
    for (const Entry& e : entries) {
        if (strcmp(e.key, key))
            continue;
        int n = e.count < max ? e.count : max;
        for (int i = 0; i < n; i++)
            values[i] = e.ints ? (double)e.ints[i] : e.bools ? (double)e.bools[i] : (double)e.floats[i];
        return n;
    }
    return 0;
}

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
    rood_selection::clear_hidden(); /* the file's sets replace them */
    pc_lua_settings_begin(); /* the file's plugin lines replace them */
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
    pc_lua_settings_end();
    pc_lighting_settings_loaded(1);
    if (scale == -1 || (scale > 0 && (scale != pc_hw_scale_setting || pc_hw_scale_auto)))
        pc_hw_set_scale(scale < 0 ? 0 : scale); /* 0: auto */
    snprintf(status, sizeof status, "Loaded %d settings from %s", applied, file);
}

/* Every setting as "key value..." lines (the file's body; the editor's undo
 * compares them). */
extern "C" void pc_settings_write(FILE* f)
{
    if (pc_hw_scale_auto)
        fprintf(f, "internal_resolution auto\n");
    else
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
    pc_occlusion_settings_write(f);
    fprintf(f, "ui_menu_tint %s\n", pc_ui_tint_id(pc_ui_tint_current()));
    fprintf(f, "ui_menu_tint_custom %d %d %d\n", pc_ui_menu_tint_custom[0], pc_ui_menu_tint_custom[1],
        pc_ui_menu_tint_custom[2]);
    if (pc_window_mode_get)
        fprintf(f, "window_mode %s\n", window_mode_names[pc_window_mode_get() % 3]);
    if (pc_master_volume_get)
        fprintf(f, "master_volume %d\n", pc_master_volume_get());
    if (pc_brightness_get)
        fprintf(f, "brightness %d\n", pc_brightness_get());
    if (pc_display_setting)
        fprintf(f, "display %s\n", pc_display_setting());
    if (pc_fullscreen_mode_setting)
        fprintf(f, "fullscreen_mode %s\n", pc_fullscreen_mode_setting());
    if (pc_vsync_get)
        fprintf(f, "vsync %d\n", pc_vsync_get());
    if (pc_window_remember_get) {
        int r[4];
        fprintf(f, "remember_window %d\n", pc_window_remember_get());
        if (pc_window_remember_get() && pc_window_rect_get && pc_window_rect_get(r))
            fprintf(f, "window_rect %d %d %d %d\n", r[0], r[1], r[2], r[3]);
    }
    if (strcmp(pc_camera_stick_menus, pc_camera_stick_menus_default)) /* (only a choice: the default may grow) */
        fprintf(f, "camera_stick_menus %s\n", *pc_camera_stick_menus ? pc_camera_stick_menus : "none");
    for (int slot = 0; slot < 2; slot++) {
        const char* card = pc_memory_card_path_setting(slot);
        fprintf(f, "card%d %s\n", slot + 1, *card ? card : "default");
    }
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
    pc_lua_settings_write(f);
    rood_selection::write_hidden(f);
    rood_colour::write_settings(f);
    rood_library::write_settings(f);
    rood_fit::write_settings(f);
    pc_emissive_view_settings_write(f);
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
    if (scale == -1 || (scale > 0 && (scale != pc_hw_scale_setting || pc_hw_scale_auto)))
        pc_hw_set_scale(scale < 0 ? 0 : scale); /* 0: auto */
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
        /* A risky display change not confirmed yet (display_confirm.h): the value it had saves. */
        char held[PC_DISPLAY_CONFIRM_VALUE];
        if (pc_display_confirm_held(line_key(line).c_str(), held, (int)sizeof held))
            line = line_key(line) + " " + held;
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
