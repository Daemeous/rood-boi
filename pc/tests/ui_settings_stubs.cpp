// The game-side variables and calls ui_settings.cpp reaches, for its probe
// (ui_settings_probe.cpp): plain variables with the game's defaults, and
// setters that record what they were called with. The anti-aliasing
// (antialias.c), right-stick camera (camera_stick.c) and bindings
// (boot_controller.c) are the real modules.
//   default:                  with the game options bridge, no feature hooks
//   UI_SETTINGS_PROBE_HOOKS:  no game options bridge (rows hidden), every
//                             hook (window mode, volume, brightness, capture)
#include "ui_settings_stubs.h"
#include "lighting_quality.h"
#include "ui/game_options.h"
#include <cstdio>
#include <cstring>

extern "C" {
int pc_hw_active = 1, pc_hw_scale_setting = 4, pc_gl_filter = 2, pc_gl_aspect, pc_gte_widescreen,
    pc_hw_perspective = 1, pc_hw_subpixel = 1, pc_hw_interpolate = 1, pc_hw_texture_filter, pc_present_rate = 1;
int pc_env_global = 1, pc_lights_enabled = 1, pc_qol_weapon_slots, pc_skip_intro, pc_ui_menus;
float pc_emissive = 1.0f; // emissive.cpp: glowing surfaces' strength
int pc_qol_map_invert, pc_qol_fp_invert_y, pc_qol_map_controls, pc_qol_skip_charging;
int pc_qol_confirm_cross, pc_qol_field_cross_attack;
int pc_qol_fast_travel = 1, pc_qol_fast_travel_cost, pc_qol_fast_travel_flat = 15, pc_qol_fast_travel_prompt = 1;
float pc_qol_fast_travel_scale = 0.5f, pc_qol_fast_travel_prompt_x = 0.5f, pc_qol_fast_travel_prompt_y = 0.84f,
    pc_qol_fast_travel_prompt_scale = 1.0f;
float pc_ui_menu_opacity = 0.94f;
int pc_ui_menu_tint, pc_ui_menu_tint_custom[3] = {8, 7, 7};
// ui_theme.cpp: the theme file's ground (#110f0e), Lea Monde's colour (ui_tint.cpp)
void pc_ui_theme_file_ground_rgb(int* r, int* g, int* b) { *r = 0x11, *g = 0x0f, *b = 0x0e; }
int pc_lighting_preset = PC_PRESET_HIGH, pc_gi_method = PC_GI_PROBES, pc_shadow_mode = PC_SHADOWS_MAPS,
    pc_haze_quality = PC_HAZE_LOW, pc_ao = 1;
int pc_ui_text, pc_ui_upscale, pc_ui_subpixel, pc_ui_lines = 1, pc_ui_outline_boost, pc_ui_bubbles;
int pc_button_prompts, pc_button_prompts_style;
float pc_button_prompts_scale = 0.7f;
int pc_gameplay_walls, pc_gameplay_walls_ghost = 1;

int pc_hw_set_scale(int scale)
{
    stub.set_scale_calls++;
    stub.last_scale = scale;
    pc_hw_scale_setting = scale;
    return scale;
}

void pc_settings_save(void) { stub.saves++; }

int pc_aa_msaa_max(void) { return stub.msaa_max; }

// The lighting keys the rows use, as lighting_quality.cpp parses them.
int pc_lighting_set(const char* key, const char* value)
{
    std::snprintf(stub.last_lighting, sizeof stub.last_lighting, "%s %s", key, value);
    stub.lighting_calls++;
    static const char* const presets[] = {"original", "low", "medium", "high", "ultra", "custom"};
    static const char* const gi[] = {"off", "screen", "probes", "path"};
    static const char* const shadows[] = {"off", "maps", "traced"};
    static const char* const haze[] = {"off", "low", "high"};
    auto pick = [&](const char* const* names, int n, int* var) {
        for (int i = 0; i < n; i++)
            if (!std::strcmp(value, names[i]))
                return *var = i, 1;
        return 0;
    };
    if (!std::strcmp(key, "lighting_preset"))
        return pick(presets, 6, &pc_lighting_preset);
    if (!std::strcmp(key, "gi_method"))
        return pick(gi, 4, &pc_gi_method);
    if (!std::strcmp(key, "shadows"))
        return pick(shadows, 3, &pc_shadow_mode);
    if (!std::strcmp(key, "haze"))
        return pick(haze, 3, &pc_haze_quality);
    if (!std::strcmp(key, "dynamic_lights"))
        return pc_lights_enabled = value[0] == '1', 1;
    if (!std::strcmp(key, "ao"))
        return pc_ao = value[0] == '1', 1;
    return 0;
}

// No ray tracing here: path-traced GI, traced shadows and High haze are out.
const char* pc_lighting_unavailable(const char* key, int value)
{
    if (!std::strcmp(key, "gi_method") && value == PC_GI_SCREEN)
        return "Screen-space GI is not implemented yet";
    if (!stub.traced && ((!std::strcmp(key, "gi_method") && value == PC_GI_PATH)
            || (!std::strcmp(key, "shadows") && value == PC_SHADOWS_TRACED)
            || (!std::strcmp(key, "haze") && value == PC_HAZE_HIGH)))
        return "Needs a GPU with Vulkan ray queries";
    return nullptr;
}

void pc_lighting_caps(pc_lighting_caps_t* c)
{
    std::memset(c, 0, sizeof *c);
    c->hardware = pc_hw_active;
    c->vulkan = stub.vulkan;
    c->ray_query = stub.traced && stub.vulkan;
    c->traced = stub.traced;
}

#ifndef UI_SETTINGS_PROBE_HOOKS
// The game's options (game_options.c's contract).
int pc_game_option_get(int option)
{
    return option >= 0 && option < PC_GAME_OPT_COUNT ? stub.game[option] : -1;
}
const char* pc_game_option_unavailable(int option)
{
    if (option == PC_GAME_OPT_PUZZLE_MODE && stub.puzzle_disabled)
        return "Cannot change in the middle of a puzzle";
    if (option == PC_GAME_OPT_VIBRATION)
        return "No effect on PC yet";
    return nullptr;
}
int pc_game_option_set(int option, int choice)
{
    stub.game_sets++;
    if (pc_game_option_unavailable(option) || stub.game[option] == choice)
        return 0;
    stub.game[option] = choice;
    return 1;
}
#else
int pc_window_mode_get(void) { return stub.window_mode; }
void pc_window_mode_set(int mode) { stub.window_mode = mode; }
int pc_master_volume_get(void) { return stub.volume; }
void pc_master_volume_set(int volume) { stub.volume = volume; }
int pc_brightness_get(void) { return stub.brightness; }
void pc_brightness_set(int value) { stub.brightness = value; }
int pc_ui_capture_begin(void) { return ++stub.captures, 1; }
#endif
}

Stub stub;
