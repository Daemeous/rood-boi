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
#include "cutscene_sequencer.h"

#include "imgui.h"
#include "platform.h"

extern "C" {
extern float pc_gi_strength, pc_emitter_light, pc_emitter_radius, pc_contact_radius, pc_baked_ambient,
    pc_lights_baked, pc_lights_master, pc_light_size, pc_gi_baked_bounce, pc_fog_density, pc_fog_anisotropy,
    pc_fog_glow, pc_glow_category[3][5], pc_haze_max_opacity, pc_haze_max_light, pc_haze_shafts, pc_haze_max_forward,
    pc_haze_interior, pc_haze_interior_fill, pc_haze_under_floor, pc_haze_void, pc_haze_void_level, pc_haze_outside,
    pc_haze_interior_edge, pc_haze_interior_slope, pc_glow_emissive_lift, pc_glow_emissive_disc,
    pc_baked_shadow_level, pc_baked_shadow_curve, pc_baked_shadow_sky, pc_baked_shadow_indirect,
    pc_baked_shadow_lights;
extern int pc_key_light, pc_lights_enabled;
extern float pc_key_light_strength;
int pc_env_open_sky(void);
}

/* The room's key light (pc/docs/Materials.md "The key light"): its values, the room settings key_*. Their
 * built-in values are the interior defaults; rooms open to the sky take the exterior ones. */
static float key_intensity = 1, key_relief = 1, key_gain = 3, key_azimuth = 35, key_elevation = 45, key_view = 1,
             key_red = 1, key_green = 1, key_blue = 1, key_specular = 1;

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
    bool has_ext = false; // a separate project default for rooms open to the sky (the key light)
    float ext_base = 0; // that default ("set exterior.<key> <value>" in a defaults file)
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
    /* The baked shadow (lights.cpp): the game's vertex light as the shadow of the light we add. A
     * room surface baked darker than the level (1: the neutral 128; 0: off) takes (baked /
     * level)^curve of the sun and sky, the indirect light and the lights, by each strength. */
    { "baked_shadow_level", "Baked shadow: lit from (baked light)", "Lighting", &pc_baked_shadow_level, 0, 2, "%.2f", false },
    { "baked_shadow_curve", "Baked shadow: curve", "Lighting", &pc_baked_shadow_curve, 0.25f, 4, "%.2f", false },
    { "baked_shadow_sky", "Baked shadow on the sun and sky", "Lighting", &pc_baked_shadow_sky, 0, 1, "%.2f", false },
    { "baked_shadow_indirect", "Baked shadow on indirect light", "Lighting", &pc_baked_shadow_indirect, 0, 1, "%.2f", false },
    { "baked_shadow_lights", "Baked shadow on the lights", "Lighting", &pc_baked_shadow_lights, 0, 1, "%.2f", false },
    /* The key light (pc_key_light_params): a soft directional light that shows the maps' relief. */
    { "key_intensity", "Key light: intensity", "Key light", &key_intensity, 0, 4, "%.2f", false },
    { "key_relief", "Key light: relief only (share)", "Key light", &key_relief, 0, 1, "%.2f", false },
    { "key_gain", "Key light: relief gain", "Key light", &key_gain, 0, 16, "%.1f", false },
    { "key_azimuth", "Key light: azimuth (degrees)", "Key light", &key_azimuth, 0, 360, "%.0f", false },
    { "key_elevation", "Key light: elevation (degrees)", "Key light", &key_elevation, 0, 90, "%.0f", false },
    { "key_view", "Key light: azimuth follows the view", "Key light", &key_view, 0, 1, "toggle", false },
    { "key_red", "Key light: red", "Key light", &key_red, 0, 1, "%.2f", false },
    { "key_green", "Key light: green", "Key light", &key_green, 0, 1, "%.2f", false },
    { "key_blue", "Key light: blue", "Key light", &key_blue, 0, 1, "%.2f", false },
    { "key_specular", "Key light: highlight", "Key light", &key_specular, 0, 1, "%.2f", false },
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
struct LookOverride { bool active = false; float before = 0, applied = 0; };
LookOverride look_overrides[count];
bool exterior_class; // the room is open to the sky: the settings with an exterior default take it
/* A setting's project default for the room's class. */
float& dflt(Setting& s) { return exterior_class && s.has_ext ? s.ext_base : s.base; }
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
static_assert(sizeof settings / sizeof settings[0] <= sizeof builtin / sizeof builtin[0], "builtin[] too small");
void init()
{
    if (initialised && initialised_serial == lighting::config_serial())
        return;
    pc_room_look_clear();
    if (!initialised)
        for (int i = 0; i < count; i++)
            builtin[i] = *settings[i].value;
    initialised = true;
    initialised_serial = lighting::config_serial();
    for (int i = 0; i < count; i++) {
        settings[i].has_ext = !strncmp(settings[i].key, "key_", 4);
        settings[i].base = settings[i].ext_base = builtin[i];
    }
    for (const std::string& path : defaults_files()) {
        FILE* f = fopen(path.c_str(), "r");
        if (!f)
            continue;
        char line[256], key[64];
        float v;
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "set %63s %f", key, &v) == 2) {
                const bool ext = !strncmp(key, "exterior.", 9);
                if (Setting* s = find(key + (ext ? 9 : 0)))
                    (ext ? s->ext_base : s->base) = v;
            }
        fclose(f);
    }
    for (Setting& s : settings) // (the exterior default of a setting with no line of its own is its interior one)
        if (s.has_ext && !s.overridden)
            *s.value = dflt(s);
        else if (!s.has_ext)
            *s.value = s.base;
}
} // namespace

// A transient render layer, separate from Setting::room/base and the user layer's dirty state.
// If another editor changed the value since we applied it, preserve that edit on removal.
void pc_room_look_clear()
{
    for (int i = 0; i < count; ++i) {
        LookOverride& o = look_overrides[i];
        if (o.active && *settings[i].value == o.applied)
            *settings[i].value = o.before;
        o.active = false;
    }
}
std::vector<cutscene::LookTarget> pc_room_look_targets()
{
    init();
    std::vector<cutscene::LookTarget> result;
    for (const Setting& s : settings)
        result.push_back({s.key, *s.value, s.lo, s.hi});
    return result;
}
void pc_room_look_set(const char* target, float value)
{
    init();
    if (Setting* s = find(target)) {
        LookOverride& o = look_overrides[s - settings];
        if (!o.active) o.before = *s->value;
        o.active = true;
        o.applied = value;
        *s->value = value;
    }
}

/* A new room: every setting back to its default until the room's file
 * applies its overrides (pc_room_settings_parse). */
extern "C" void pc_room_settings_reset(void)
{
    pc_room_look_clear();
    init();
    for (Setting& s : settings) {
        s.overridden = false;
        *s.value = dflt(s);
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
        pc_room_look_clear();
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

/* RPC room_settings (dev_rpc.c): {key: value, ...} overrides keys for the room (as the editor
 * does; "default" for a value returns it to the project default); the reply lists every setting:
 * its value, project default and whether the room overrides it. */
extern "C" int pc_room_settings_key(int i, const char** key)
{
    if (i < 0 || i >= count)
        return 0;
    *key = settings[i].key;
    return 1;
}
extern "C" void pc_room_settings_set(const char* key, float value, int reset)
{
    init();
    Setting* s = find(key);
    if (!s)
        return;
    pc_room_look_clear();
    s->overridden = !reset;
    s->room = *s->value = reset ? dflt(*s) : value;
}
extern "C" int pc_room_settings_json(char* out, int size)
{
    init();
    std::string j = "{";
    char item[192];
    for (int i = 0; i < count; i++) {
        snprintf(item, sizeof item, "%s\"%s\":{\"value\":%g,\"default\":%g,\"room\":%s}", i ? "," : "",
            settings[i].key, *settings[i].value, dflt(settings[i]), settings[i].overridden ? "true" : "false");
        j += item;
    }
    j += "}";
    snprintf(out, size, "%s", j.c_str());
    return (int)j.size() < size ? 0 : -1;
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
    for (const Setting& s : settings) {
        fprintf(f, "set %s %g\n", s.key, s.base);
        if (s.has_ext)
            fprintf(f, "set exterior.%s %g\n", s.key, s.ext_base);
    }
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
            look_overrides[&s - settings].active = false; // Explicit user edits replace the baseline, even if equal.
            s.overridden = true;
            s.room = *s.value;
            changed = true;
        }
        if (highlighted)
            ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem("default")) {
            if (ImGui::MenuItem("Use as the project default")) {
                look_overrides[&s - settings].active = false;
                dflt(s) = *s.value;
                s.overridden = false;
                save_defaults();
                changed = true;
            }
            if (ImGui::MenuItem("Reset to the project default", nullptr, false, s.overridden)) {
                look_overrides[&s - settings].active = false;
                s.overridden = false;
                *s.value = dflt(s);
                changed = true;
            }
            ImGui::EndPopup();
        }
        if (s.overridden) {
            ImGui::SameLine();
            if (ImGui::SmallButton("reset")) {
                look_overrides[&s - settings].active = false;
                s.overridden = false;
                *s.value = dflt(s);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Back to the project default (%g)", dflt(s));
        } else if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Project default. Change it to override for this room;\nright-click for options.");
        }
        ImGui::PopID();
    }
    return changed;
}

/* The room's key light for the present passes (scaled.frag key_), on the game thread at the frame's header: [0] azimuth
 * and elevation in radians, whether the azimuth is turned with the view, the relief share; [4..6] colour times intensity
 * (all zero: no key light), [7] the highlight's strength, [8] the relief gain. A room open to the sky takes the settings' exterior defaults
 * (the lighting packs' defaults.txt "set exterior.<key>"), anywhere else the interior ones. */
extern "C" void pc_key_light_params(float out[9])
{
    init();
    const bool exterior = pc_env_open_sky() != 0;
    if (exterior != exterior_class) { // a room change, or the sun or sky edited: the settings not overridden follow
        exterior_class = exterior;
        for (Setting& s : settings)
            if (s.has_ext && !s.overridden)
                *s.value = dflt(s);
    }
    for (int i = 0; i < 9; i++)
        out[i] = 0;
    float strength = pc_key_light_strength;
    if (!pc_key_light || !pc_lights_enabled || !(strength > 0) || !(key_intensity > 0))
        return;
    auto clamp = [](float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; };
    const float deg = 3.14159265f / 180.0f, gain = clamp(key_intensity, 0, 4) * clamp(strength, 0, 2);
    out[0] = key_azimuth * deg;
    out[1] = clamp(key_elevation, 0, 90) * deg;
    out[2] = key_view > 0.5f ? 1.0f : 0.0f;
    out[3] = clamp(key_relief, 0, 1);
    out[4] = clamp(key_red, 0, 1) * gain;
    out[5] = clamp(key_green, 0, 1) * gain;
    out[6] = clamp(key_blue, 0, 1) * gain;
    out[7] = clamp(key_specular, 0, 1);
    out[8] = clamp(key_gain, 0, 16);
}
