// Per-room lighting and atmosphere settings.
//
// Each setting has a project default (the built-in value, a lighting pack's
// lighting/<pack>/defaults.txt, or the user's pc/user/lighting/defaults.txt)
// and an optional override for the current room, an item of the room's
// lighting layers ("set <key> value=..." lines, lighting/README.md).
// Changing a setting in the editor overrides it for the room; resetting it
// returns to the default. On a room change every setting takes the new
// room's value or the default.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include "pc_paths.h"
#include "lighting_layers.h"

#include "imgui.h"
#include "platform.h"

extern "C" {
extern float pc_gi_strength, pc_emitter_light, pc_emitter_radius, pc_contact_radius, pc_baked_ambient,
    pc_lights_baked, pc_lights_master, pc_light_size, pc_gi_baked_bounce, pc_fog_density, pc_fog_anisotropy,
    pc_fog_glow, pc_glow_category[3][5], pc_haze_max_opacity, pc_haze_max_light, pc_haze_shafts, pc_haze_max_forward,
    pc_haze_interior, pc_haze_interior_fill, pc_haze_under_floor, pc_haze_void, pc_haze_void_level, pc_haze_outside,
    pc_haze_interior_edge, pc_haze_interior_slope, pc_glow_emissive_lift, pc_glow_emissive_disc;
}

namespace {
struct Setting {
    const char* key;
    const char* label;
    const char* page; // "Lighting" or "Atmosphere"
    float* value;
    float lo, hi;
    const char* format;
    bool logarithmic;
    float base; // project default
    bool overridden; // for the current room
    float room; // the room's value when overridden
};
Setting settings[] = {
    { "baked_lighting", "Baked lighting", "Lighting", &pc_lights_baked, 0, 1.5f, "%.2f", false },
    { "light_intensity", "Light intensity", "Lighting", &pc_lights_master, 0, 4, "%.2f", false },
    { "glow_light", "Glow light", "Lighting", &pc_emitter_light, 0, 4, "%.2f", false },
    { "glow_reach", "Glow reach", "Lighting", &pc_emitter_radius, 100, 2000, "%.0f", false },
    { "light_size", "Shadow softness (light size)", "Lighting", &pc_light_size, 0.5f, 48, "%.1f", true },
    { "contact_radius", "Contact shadow radius", "Lighting", &pc_contact_radius, 0, 160, "%.0f", false },
    { "baked_ambient", "Ambient share of baked light", "Lighting", &pc_baked_ambient, 0, 1, "%.2f", false },
    { "gi_strength", "Indirect light strength", "Lighting", &pc_gi_strength, 0, 4, "%.2f", false },
    { "baked_bounce", "Baked light bounce", "Lighting", &pc_gi_baked_bounce, 0, 1, "%.2f", false },
    { "haze_density", "Haze density", "Atmosphere", &pc_fog_density, 0, 0.004f, "%.5f", true },
    { "haze_anisotropy", "Forward scattering", "Atmosphere", &pc_fog_anisotropy, -0.5f, 0.9f, "%.2f", false },
    { "haze_glow", "Glows in the haze", "Atmosphere", &pc_fog_glow, 0, 4, "%.2f", false },
    /* The limits (atmosphere.h): the haze, fog and volumes never hide more
     * of the scene than this, never scatter more light toward the eye. */
    { "haze_max_opacity", "Most it hides (opacity)", "Atmosphere", &pc_haze_max_opacity, 0, 1, "%.2f", false },
    { "haze_max_light", "Most light it adds", "Atmosphere", &pc_haze_max_light, 0, 1, "%.3f", false },
    { "haze_shafts", "Sun shafts' contrast", "Atmosphere", &pc_haze_shafts, 0, 1, "%.2f", false },
    { "haze_max_forward", "Glow toward the moon (peak)", "Atmosphere", &pc_haze_max_forward, 0, 8, "%.2f", false },
    /* Where the room's air is (atmosphere.h): its interior, not its bounds;
     * clear over the void around the room. */
    { "haze_interior", "Keep to the room's interior", "Atmosphere", &pc_haze_interior, 0, 1, "toggle", false },
    { "haze_interior_fill", "Interior: holes filled (cells)", "Atmosphere", &pc_haze_interior_fill, 0, 4, "%.0f", false },
    { "haze_interior_slope", "Interior: steepest floor (degrees)", "Atmosphere", &pc_haze_interior_slope, 37, 89, "%.0f", false },
    { "haze_interior_edge", "Interior: edge softness", "Atmosphere", &pc_haze_interior_edge, 0, 0.5f, "%.2f", false },
    { "haze_outside", "Interior: haze over the outside", "Atmosphere", &pc_haze_outside, 0, 1, "%.2f", false },
    { "haze_under_floor", "Interior: haze under the floor", "Atmosphere", &pc_haze_under_floor, 0, 256, "%.0f", false },
    { "haze_void", "Clear over the void", "Atmosphere", &pc_haze_void, 0, 1, "toggle", false },
    { "haze_void_level", "Void: brightest black (of 255)", "Atmosphere", &pc_haze_void_level, 0, 32, "%.0f", false },
    /* The game's glows by category (glows.c): fixtures (candles, torches,
     * lanterns, room flames), magic (the save point), transient effects
     * (attacks, spells, sparks; never shadowed). "toggle": a checkbox;
     * "never": shown off and disabled. */
    { "glow_fixtures_enabled", "Fixtures", "Glows", &pc_glow_category[0][0], 0, 1, "toggle", false },
    { "glow_fixtures_intensity", "Fixtures: intensity", "Glows", &pc_glow_category[0][1], 0, 4, "%.2f", false },
    { "glow_fixtures_reach", "Fixtures: reach", "Glows", &pc_glow_category[0][2], 0.1f, 4, "%.2f", false },
    { "glow_fixtures_shadows", "Fixtures: cast shadows", "Glows", &pc_glow_category[0][3], 0, 1, "toggle", false },
    { "glow_fixtures_smoothing", "Fixtures: smoothing (s)", "Glows", &pc_glow_category[0][4], 0, 1, "%.2f", false },
    { "glow_magic_enabled", "Magic (save point)", "Glows", &pc_glow_category[1][0], 0, 1, "toggle", false },
    { "glow_magic_intensity", "Magic: intensity", "Glows", &pc_glow_category[1][1], 0, 4, "%.2f", false },
    { "glow_magic_reach", "Magic: reach", "Glows", &pc_glow_category[1][2], 0.1f, 4, "%.2f", false },
    { "glow_magic_shadows", "Magic: cast shadows", "Glows", &pc_glow_category[1][3], 0, 1, "toggle", false },
    { "glow_magic_smoothing", "Magic: smoothing (s)", "Glows", &pc_glow_category[1][4], 0, 1, "%.2f", false },
    { "glow_effects_enabled", "Effects (attacks, spells, sparks)", "Glows", &pc_glow_category[2][0], 0, 1, "toggle", false },
    { "glow_effects_intensity", "Effects: intensity", "Glows", &pc_glow_category[2][1], 0, 4, "%.2f", false },
    { "glow_effects_reach", "Effects: reach", "Glows", &pc_glow_category[2][2], 0.1f, 4, "%.2f", false },
    { "glow_effects_shadows", "Effects: cast shadows", "Glows", &pc_glow_category[2][3], 0, 0, "never", false },
    { "glow_effects_smoothing", "Effects: smoothing (s)", "Glows", &pc_glow_category[2][4], 0, 1, "%.2f", false },
    /* Glowing surfaces' lights on objects (glows.c: a crate's sides, a
     * floating block's circle): in front of the surface, a disc facing out. */
    { "glow_emissive_lift", "Glowing objects: lift off the surface", "Glows", &pc_glow_emissive_lift, 0, 32, "%.1f", false },
    { "glow_emissive_disc", "Glowing objects: disc size", "Glows", &pc_glow_emissive_disc, 0, 1.5f, "%.2f", false },
};
const int count = sizeof settings / sizeof settings[0];
bool initialised;
int initialised_serial;
/* Project defaults: each pack's lighting/<pack>/defaults.txt (lowest first),
 * then the user's (pc/user/lighting/defaults.txt, or the old
 * pc/lights/defaults.txt), which "Use as the project default" writes. */
std::string user_defaults() { return lighting::user_dir() + "/defaults.txt"; }
std::vector<std::string> defaults_files()
{
    std::vector<std::string> files;
    for (const std::string& pack : lighting::packs())
        files.push_back(lighting::packs_root() + "/" + pack + "/defaults.txt");
    std::string user = user_defaults();
    struct stat st;
    if (stat(user.c_str(), &st) && !lighting::legacy_dir().empty())
        user = lighting::legacy_dir() + "/defaults.txt";
    files.push_back(user);
    return files;
}

Setting* find(const char* key)
{
    for (Setting& s : settings)
        if (!strcmp(s.key, key))
            return &s;
    return nullptr;
}
float builtin[64];
void init()
{
    if (initialised && initialised_serial == lighting::config_serial())
        return;
    if (!initialised)
        for (int i = 0; i < count; i++)
            builtin[i] = *settings[i].value;
    initialised = true;
    initialised_serial = lighting::config_serial();
    for (int i = 0; i < count; i++)
        settings[i].base = *settings[i].value = builtin[i];
    for (const std::string& path : defaults_files()) {
        FILE* f = fopen(path.c_str(), "r");
        if (!f)
            continue;
        char line[256], key[64];
        float v;
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "set %63s %f", key, &v) == 2)
                if (Setting* s = find(key))
                    s->base = *s->value = v;
        fclose(f);
    }
}
} // namespace

/* A new room: every setting back to its default until the room's file
 * applies its overrides (pc_room_settings_parse). */
extern "C" void pc_room_settings_reset(void)
{
    init();
    for (Setting& s : settings) {
        s.overridden = false;
        *s.value = s.base;
    }
}

/* One line of a room file; returns 1 when it was a setting. */
extern "C" int pc_room_settings_parse(const char* line)
{
    init();
    char key[64];
    float v;
    if (sscanf(line, "set %63s %f", key, &v) != 2)
        return 0;
    if (Setting* s = find(key)) {
        s->overridden = true;
        s->room = *s->value = v;
    }
    return 1;
}

extern "C" void pc_room_settings_write(FILE* f)
{
    for (const Setting& s : settings)
        if (s.overridden)
            fprintf(f, "set %s %g\n", s.key, s.room);
}

extern "C" int pc_room_settings_overrides(void)
{
    int n = 0;
    for (const Setting& s : settings)
        n += s.overridden;
    return n;
}

static void save_defaults()
{
    std::string path = user_defaults();
    std::string dir = lighting::user_dir();
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/')
            pc_mkdir(dir.substr(0, i).c_str());
    FILE* f = fopen(path.c_str(), "w");
    if (!f)
        return;
    fprintf(f, "# rood-boi project defaults for per-room settings (see pc/src/room_settings.cpp)\n");
    for (const Setting& s : settings)
        fprintf(f, "set %s %g\n", s.key, s.base);
    fclose(f);
}

/* The settings of one page as rows: changing a value overrides it for the
 * room (label highlighted, with a reset button); right-click a row to make
 * its value the project default. Returns true when something changed (the
 * room has unsaved edits). */
extern "C" bool pc_room_settings_page(const char* page)
{
    init();
    bool changed = false;
    for (Setting& s : settings) {
        if (strcmp(s.page, page))
            continue;
        ImGui::PushID(s.key);
        bool highlighted = s.overridden; /* pop what was pushed, whatever the slider does */
        if (highlighted)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.3f, 1));
        ImGuiSliderFlags flags = s.logarithmic ? ImGuiSliderFlags_Logarithmic : 0;
        float width = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 1.6f;
        ImGui::SetNextItemWidth(width * 0.55f);
        bool edited = false;
        if (!strcmp(s.format, "never")) {
            ImGui::BeginDisabled();
            bool off = false;
            ImGui::Checkbox(s.label, &off);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Transient effects (attacks, spells, hit sparks) never cast shadows: they appear,\n"
                                  "move and vanish within frames, and their shadows jumped all over the place.\n"
                                  "Their light still adds, unshadowed and smoothed.");
            ImGui::PopID();
            if (highlighted)
                ImGui::PopStyleColor();
            continue;
        } else if (!strcmp(s.format, "toggle")) {
            bool on = *s.value > 0.5f;
            if ((edited = ImGui::Checkbox(s.label, &on)))
                *s.value = on ? 1.0f : 0.0f;
        } else
            edited = ImGui::SliderFloat(s.label, s.value, s.lo, s.hi, s.format, flags);
        if (edited) {
            s.overridden = true;
            s.room = *s.value;
            changed = true;
        }
        if (highlighted)
            ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem("default")) {
            if (ImGui::MenuItem("Use as the project default")) {
                s.base = *s.value;
                s.overridden = false;
                save_defaults();
                changed = true;
            }
            if (ImGui::MenuItem("Reset to the project default", nullptr, false, s.overridden)) {
                s.overridden = false;
                *s.value = s.base;
                changed = true;
            }
            ImGui::EndPopup();
        }
        if (s.overridden) {
            ImGui::SameLine();
            if (ImGui::SmallButton("reset")) {
                s.overridden = false;
                *s.value = s.base;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Back to the project default (%g)", s.base);
        } else if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Project default. Change it to override for this room;\nright-click for options.");
        }
        ImGui::PopID();
    }
    return changed;
}
