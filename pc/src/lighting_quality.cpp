// Lighting quality presets and their settings (lighting_quality.h).
//
// The presets are data: "key value" lines, a value being choices in order of
// preference ("traced|maps"). Applying a preset takes, per key, the first
// choice this backend can run, so a preset always works: on Vulkan with ray
// queries, on Vulkan without them (ROOD_VK_RT=0 or a GPU without), and on
// OpenGL (64- and 32-bit; GL 4.3 compute runs the GL path tracer). What runs
// is decided again each frame (a Custom "traced" without a tracer draws
// shadow maps), so a settings file from another machine also works.
#include "lighting_quality.h"
#include "frame_cost.h" /* RPC perf: the game thread's sections */
#include "atmosphere.h"
#include "probe_gi.h"
#include "antialias.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "imgui.h"
#include "imgui_internal.h" /* ImHashStr, OpenPopupEx: opening the toolbar's combo by request */

extern "C" {
void pc_settings_env_override(const char* key) __attribute__((weak)); /* project_settings.cpp */
extern int pc_lights_enabled, pc_gi_enabled, pc_rt_shadows, pc_baked_shadows, pc_glow_shadows, pc_shadow_samples,
    pc_gi_bounces, pc_gi_rays, pc_gi_denoise, pc_fog_steps, pc_hw_active, pc_present_rate,
    pc_hw_interpolate;
extern float pc_gi_resolution, pc_shadow_softness, pc_fog_density, pc_shadow_history;
extern int pc_gl_compute;
/* Material response (emissive.h): the specular switch and its tunables. */
extern int pc_specular, pc_reflection_bounces;
extern float pc_specular_strength, pc_roughness_bias;
int pc_material_active(void);
/* Weak: absent in some builds. */
extern int pc_vk_active __attribute__((weak));
int pc_vk_caps(int* ray_query, int* integrated, char* name, int size) __attribute__((weak)); /* vk_core.c */
const char* pc_gl_renderer(void) __attribute__((weak)); /* gl_present.c */
int vk_nrd_built(void) __attribute__((weak));
int pc_gi_denoiser_active(void) __attribute__((weak));
/* The probe GI (gi_method probes) reports itself through this. */
int pc_probe_gi_available(void) __attribute__((weak));
void pc_probe_panel(void) __attribute__((weak)); /* probe_overlay.cpp: the probe field's settings */
extern float pc_shadow_motion_history __attribute__((weak)), pc_haze_history __attribute__((weak));
int pc_aa_msaa_max(void) __attribute__((weak)); /* gpu_hw.c: the most MSAA samples this backend runs */
extern int pc_hw_scale;
extern int pc_gl_view[4], pc_gl_display[4]; /* the game image in the window, the display area it shows */
extern int pc_nrd_glow __attribute__((weak)), pc_nrd_max_frames __attribute__((weak));
}

int pc_lighting_preset = PC_PRESET_CUSTOM; /* decided on the first frame (pc_lighting_sync) */
int pc_gi_method = PC_GI_PATH;
int pc_shadow_mode = PC_SHADOWS_TRACED;
int pc_shadow_map_filter = PC_SHADOW_MAP_PCF;
int pc_haze_quality = PC_HAZE_HIGH;
int pc_ao;
float pc_ao_radius = 64.0f; /* world units */
float pc_ao_strength = 0.6f;

namespace {
const char* const preset_names[PC_PRESET_COUNT] = { "original", "low", "medium", "high", "ultra", "custom" };
const char* const preset_labels[PC_PRESET_COUNT] = { "Original", "Low", "Medium", "High", "Ultra", "Custom" };
const char* const preset_help[PC_PRESET_COUNT] = {
    "The PS1's baked lighting only: no dynamic lights, no GI, no haze.",
    "Shadow maps (soft PCF), baked light with ambient occlusion. Integrated GPUs, no ray tracing.",
    "Soft shadow maps (PCSS-like), probe GI, light haze. Mid-range GPUs, no ray tracing needed.",
    "Ray-traced soft shadows incl. glow and candle shadows, probe GI, volumetric haze with shadows. Entry ray tracing.",
    "Ray-traced soft shadows, per-pixel path-traced GI with NVIDIA NRD, full volumetrics. Discrete ray-tracing GPUs.",
    "Your own choice of the settings below.",
};
/* One "key value" per line; a value lists choices in order of preference.
 * gi_resolution is capped at 1.5 on integrated GPUs. */
const char* const presets[PC_PRESET_CUSTOM] = {
    /* Original */
    "dynamic_lights 0\ngi_method off\ngi_resolution 1\nbounces 1\nrays_per_pixel 1\ngi_denoiser filter\n"
    "shadows off\nshadow_map_filter pcf\nshadow_map_softness 0.02\nshadow_rays 1\nglow_shadows 0\n"
    "characters_shadow_baked 0\nhaze off\nao 0\nao_radius 64\nprobe_spacing 128\nprobe_rays 32\nhaze_cell 4\n",
    /* Low */
    "dynamic_lights 1\ngi_method off\ngi_resolution 1\nbounces 1\nrays_per_pixel 1\ngi_denoiser filter\n"
    "shadows maps\nshadow_map_filter pcf\nshadow_map_softness 0.02\nshadow_rays 1\nglow_shadows 0\n"
    "characters_shadow_baked 0\nhaze off\nao 1\nao_radius 64\nprobe_spacing 128\nprobe_rays 32\nhaze_cell 4\n",
    /* Medium */
    "dynamic_lights 1\ngi_method probes|off\ngi_resolution 1.5\nbounces 1\nrays_per_pixel 1\ngi_denoiser filter\n"
    "shadows maps\nshadow_map_filter pcss\nshadow_map_softness 0.02\nshadow_rays 1\nglow_shadows 0\n"
    "characters_shadow_baked 0\nhaze low\nao 1\nao_radius 64\nprobe_spacing 128\nprobe_rays 32\nhaze_cell 4\n",
    /* High */
    "dynamic_lights 1\ngi_method probes|off\ngi_resolution 1.5\nbounces 1\nrays_per_pixel 1\ngi_denoiser filter\n"
    "shadows traced|maps\nshadow_map_filter pcss\nshadow_map_softness 0.02\nshadow_rays 1\nglow_shadows 1\n"
    "characters_shadow_baked 1\nhaze high|low\nao 1\nao_radius 64\nprobe_spacing 128\nprobe_rays 64\nhaze_cell 3\n",
    /* Ultra */
    "dynamic_lights 1\ngi_method path|probes|off\ngi_resolution 3\nbounces 1\nrays_per_pixel 1\n"
    "gi_denoiser nrd|filter\nshadows traced|maps\nshadow_map_filter pcss\nshadow_map_softness 0.02\n"
    "shadow_rays 2\nglow_shadows 1\ncharacters_shadow_baked 1\nhaze high|low\nao 0\nao_radius 64\n"
    "probe_spacing 96\nprobe_rays 64\nhaze_cell 2\n",
};

enum Type { BOOL, INT, FLOAT_KEY, ENUM };
struct Key {
    const char* name;
    Type type;
    void* value;
    const char* const* choices; /* ENUM */
    int choice_count;
    bool preset; /* set by the presets: changing it makes the preset Custom */
};
const char* const gi_methods[] = { "off", "screen", "probes", "path" };
const char* const denoisers[] = { "raw", "filter", "nrd" };
const char* const shadow_modes[] = { "off", "maps", "traced" };
const char* const map_filters[] = { "hard", "pcf", "pcss" };
const char* const haze_levels[] = { "off", "low", "high" };
Key keys[] = {
    { "dynamic_lights", BOOL, &pc_lights_enabled, nullptr, 0, true },
    { "gi_method", ENUM, &pc_gi_method, gi_methods, 4, true },
    { "gi_resolution", FLOAT_KEY, &pc_gi_resolution, nullptr, 0, true },
    { "bounces", INT, &pc_gi_bounces, nullptr, 0, true },
    { "rays_per_pixel", INT, &pc_gi_rays, nullptr, 0, true },
    { "gi_denoiser", ENUM, &pc_gi_denoise, denoisers, 3, true },
    { "shadows", ENUM, &pc_shadow_mode, shadow_modes, 3, true },
    { "shadow_map_filter", ENUM, &pc_shadow_map_filter, map_filters, 3, true },
    { "shadow_map_softness", FLOAT_KEY, &pc_shadow_softness, nullptr, 0, true },
    { "shadow_rays", INT, &pc_shadow_samples, nullptr, 0, true },
    { "glow_shadows", BOOL, &pc_glow_shadows, nullptr, 0, true },
    { "characters_shadow_baked", BOOL, &pc_baked_shadows, nullptr, 0, true },
    { "haze", ENUM, &pc_haze_quality, haze_levels, 3, true },
    { "ao", BOOL, &pc_ao, nullptr, 0, true },
    { "ao_radius", FLOAT_KEY, &pc_ao_radius, nullptr, 0, true },
    { "ao_strength", FLOAT_KEY, &pc_ao_strength, nullptr, 0, false },
    /* Probe GI density (probe_gi.h): probes per room and rays per probe per update. */
    { "probe_spacing", FLOAT_KEY, &pc_probe_spacing, nullptr, 0, true },
    { "probe_rays", INT, &pc_probe_rays, nullptr, 0, true },
    /* The traced haze's froxel size in display pixels (atmosphere.h; Vulkan). */
    { "haze_cell", INT, &pc_haze_cell, nullptr, 0, true },
    /* Material response (emissive.h, pc/docs/Materials.md): the specular lobe on (0: off entirely, the
     * picture exactly the one without materials), its global strength, the roughness bias, and the
     * path tracer's reflection bounces (0: highlights from the lights only). */
    { "specular", BOOL, &pc_specular, nullptr, 0, false },
    { "specular_strength", FLOAT_KEY, &pc_specular_strength, nullptr, 0, false },
    { "roughness_bias", FLOAT_KEY, &pc_roughness_bias, nullptr, 0, false },
    { "reflection_bounces", INT, &pc_reflection_bounces, nullptr, 0, false },
};
const int key_count = sizeof keys / sizeof keys[0];

Key* find(const char* name)
{
    for (Key& k : keys)
        if (!strcmp(k.name, name))
            return &k;
    return nullptr;
}
double get(const Key& k)
{
    switch (k.type) {
    case FLOAT_KEY:
        return *(float*)k.value;
    default:
        return *(int*)k.value;
    }
}
void put(const Key& k, double v)
{
    if (k.type == FLOAT_KEY)
        *(float*)k.value = (float)v;
    else if (k.type == BOOL)
        *(int*)k.value = v != 0;
    else
        *(int*)k.value = (int)v;
}
/* One choice of a value ("pcss", "2", "true"); false when it is none. */
bool parse_value(const Key& k, const char* text, double* out)
{
    while (*text == ' ' || *text == '\t' || *text == '"')
        ++text;
    if (k.type == ENUM)
        for (int i = 0; i < k.choice_count; i++) {
            size_t n = strlen(k.choices[i]);
            if (!strncmp(text, k.choices[i], n) && !isalnum((unsigned char)text[n]) && text[n] != '_') {
                *out = i;
                return true;
            }
        }
    if (k.type != ENUM && (!strncmp(text, "true", 4) || !strncmp(text, "on", 2) || !strncmp(text, "yes", 3)))
        return *out = 1, true;
    if (k.type != ENUM && (!strncmp(text, "false", 5) || !strncmp(text, "off", 3) || !strncmp(text, "no", 2)))
        return *out = 0, true;
    char* end;
    double v = strtod(text, &end);
    if (end == text || (k.type == ENUM && (v < 0 || v >= k.choice_count)))
        return false;
    *out = v;
    return true;
}

pc_lighting_caps_t caps_now()
{
    pc_lighting_caps_t c;
    pc_lighting_caps(&c);
    return c;
}
bool available(const pc_lighting_caps_t& c, const Key& k, int v)
{
    if (k.value == &pc_gi_method)
        return v == PC_GI_OFF || (v == PC_GI_PROBES && c.probes) || (v == PC_GI_PATH && c.traced);
    if (k.value == &pc_shadow_mode)
        return v == PC_SHADOWS_OFF || (v == PC_SHADOWS_MAPS && c.hardware) || (v == PC_SHADOWS_TRACED && c.traced);
    if (k.value == &pc_haze_quality)
        return v == PC_HAZE_OFF || (v == PC_HAZE_LOW && c.hardware) || (v == PC_HAZE_HIGH && c.traced);
    if (k.value == &pc_gi_denoise)
        return v < 2 || c.nrd;
    return true;
}
const char* reason(const pc_lighting_caps_t& c, const Key& k, int v)
{
    static char text[256];
    if (available(c, k, v))
        return nullptr;
    bool traced_choice = (k.value == &pc_gi_method && v == PC_GI_PATH)
        || (k.value == &pc_shadow_mode && v == PC_SHADOWS_TRACED) || (k.value == &pc_haze_quality && v == PC_HAZE_HIGH);
    if (traced_choice) {
        if (!c.hardware)
            snprintf(text, sizeof text, "Needs the hardware renderer (ROOD_SCALE > 0)");
        else if (c.vulkan && c.rt_disabled)
            snprintf(text, sizeof text, "Ray tracing is switched off (ROOD_VK_RT=0)");
        else if (c.vulkan)
            snprintf(text, sizeof text, "Needs a GPU with Vulkan ray queries (VK_KHR_ray_query); %s has none",
                c.gpu[0] ? c.gpu : "this GPU");
        else
            snprintf(text, sizeof text,
                "Needs OpenGL 4.3 compute shaders (%s has none), or the Vulkan renderer on a ray-tracing GPU",
                c.gpu[0] ? c.gpu : "this context");
        return text;
    }
    if (k.value == &pc_gi_method && v == PC_GI_PROBES)
        return "Probe GI is not in this build yet (next lower: Off)";
    if (k.value == &pc_gi_method && v == PC_GI_SCREEN)
        return "Screen-space GI is not implemented yet";
    if (k.value == &pc_gi_denoise) {
        if (!c.vulkan)
            snprintf(text, sizeof text, "NRD runs on the Vulkan renderer (64-bit, ray tracing): the filter here");
        else if (!c.nrd_built)
            snprintf(text, sizeof text, "This build has no NRD (build_boot.py --arch 64 --nrd on): the filter here");
        else
            snprintf(text, sizeof text, "NRD denoises the ray-traced light: needs ray tracing (the filter here)");
        return text;
    }
    return "Needs the hardware renderer";
}

/* A preset's values for this backend (all keys; the non-preset ones keep
 * their current values). */
void resolve(int preset, double out[], const pc_lighting_caps_t& c)
{
    for (int i = 0; i < key_count; i++)
        out[i] = get(keys[i]);
    if (preset < 0 || preset >= PC_PRESET_CUSTOM)
        return;
    const char* line = presets[preset];
    while (*line) {
        const char* end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);
        std::string text(line, end);
        line = *end ? end + 1 : end;
        size_t space = text.find(' ');
        if (space == std::string::npos)
            continue;
        std::string name = text.substr(0, space), value = text.substr(space + 1);
        Key* k = find(name.c_str());
        if (!k)
            continue;
        int index = (int)(k - keys);
        double chosen = -1, last = 0;
        bool any = false;
        size_t start = 0;
        while (start <= value.size()) {
            size_t bar = value.find('|', start);
            std::string choice = value.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            double v;
            if (parse_value(*k, choice.c_str(), &v)) {
                last = v, any = true;
                if (available(c, *k, (int)v)) {
                    chosen = v;
                    break;
                }
            }
            if (bar == std::string::npos)
                break;
            start = bar + 1;
        }
        if (any)
            out[index] = chosen >= 0 ? chosen : last;
        if (k->value == &pc_gi_resolution && c.integrated && out[index] > 1.5)
            out[index] = 1.5;
    }
}

double snapshot[64];
bool have_snapshot;
int pending = -1; /* a preset to apply on the next sync; -2 auto-detect; -1 none */
bool env_preset; /* `pending` came from ROOD_LIGHTING_PRESET */
bool initialised;
int file_preset = -1; /* lighting_preset read from the settings file */
int last_gi_enabled = -1, last_rt_shadows = -1;
char detected[400]; /* how the first run chose */

void take_snapshot()
{
    for (int i = 0; i < key_count; i++)
        snapshot[i] = get(keys[i]);
    have_snapshot = true;
}
/* The legacy switches the renderers read: pc_gi_enabled (gi_method path)
 * and pc_rt_shadows (shadows traced). */
void derive_switches()
{
    pc_gi_enabled = pc_gi_method == PC_GI_PATH;
    pc_rt_shadows = pc_shadow_mode == PC_SHADOWS_TRACED;
    last_gi_enabled = pc_gi_enabled;
    last_rt_shadows = pc_rt_shadows;
}
int preset_by_name(const char* text)
{
    for (int i = 0; i < PC_PRESET_COUNT; i++)
        if (!strncmp(text, preset_names[i], strlen(preset_names[i])))
            return i;
    char* end;
    long v = strtol(text, &end, 10);
    return end != text && v >= 0 && v < PC_PRESET_COUNT ? (int)v : -1;
}
/* The preset whose values the current settings are, else Custom. */
int matching_preset(const pc_lighting_caps_t& c)
{
    double values[64];
    for (int p = 0; p < PC_PRESET_CUSTOM; p++) {
        resolve(p, values, c);
        bool same = true;
        for (int i = 0; i < key_count && same; i++)
            same = !keys[i].preset || values[i] == get(keys[i]);
        if (same)
            return p;
    }
    return PC_PRESET_CUSTOM;
}

/* GPU time (ROOD_PERF). */
double perf_sum, perf_max;
int perf_count;
} // namespace

extern "C" void pc_lighting_caps(pc_lighting_caps_t* c)
{
    memset(c, 0, sizeof *c);
    c->vulkan = &pc_vk_active && pc_vk_active;
    c->hardware = pc_hw_active != 0;
    const char* rt = getenv("ROOD_VK_RT");
    c->rt_disabled = rt && *rt == '0';
    if (c->vulkan && pc_vk_caps)
        pc_vk_caps(&c->ray_query, &c->integrated, c->gpu, sizeof c->gpu);
    else if (pc_gl_renderer && pc_gl_renderer()) {
        snprintf(c->gpu, sizeof c->gpu, "%s", pc_gl_renderer());
        /* GL names: Intel and AMD APU graphics, software rasterizers. */
        const char* g = c->gpu;
        c->integrated = strstr(g, "Intel") || strstr(g, "llvmpipe") || strstr(g, "softpipe")
            || strstr(g, "Radeon Graphics") || strstr(g, "Radeon(TM) Graphics") || strstr(g, "Vega 8")
            || strstr(g, "Apple");
    }
    c->gl_compute = !c->vulkan && pc_gl_compute;
    c->traced = c->hardware && (c->vulkan ? c->ray_query : c->gl_compute);
    c->nrd_built = vk_nrd_built && vk_nrd_built();
    c->nrd = c->vulkan && c->ray_query && c->nrd_built;
    c->probes = pc_probe_gi_available && pc_probe_gi_available();
    const char* gpu_class = getenv("ROOD_GPU_CLASS"); /* integrated | discrete: simulate the other class */
    if (gpu_class && *gpu_class)
        c->integrated = !strcmp(gpu_class, "integrated");
}

extern "C" int pc_lighting_autodetect(int ray_query, int integrated)
{
    if (ray_query)
        return integrated ? PC_PRESET_HIGH : PC_PRESET_ULTRA;
    return integrated ? PC_PRESET_LOW : PC_PRESET_MEDIUM;
}
extern "C" const char* pc_lighting_preset_name(int preset)
{
    return preset >= 0 && preset < PC_PRESET_COUNT ? preset_names[preset] : "custom";
}

extern "C" void pc_lighting_apply_preset(int preset)
{
    if (preset < 0 || preset >= PC_PRESET_COUNT)
        return;
    pc_lighting_preset = preset;
    if (preset == PC_PRESET_CUSTOM)
        return;
    pc_lighting_caps_t c = caps_now();
    double values[64];
    resolve(preset, values, c);
    for (int i = 0; i < key_count; i++)
        if (keys[i].preset)
            put(keys[i], values[i]);
    derive_switches();
    take_snapshot();
}

extern "C" void pc_lighting_sync(void)
{
    /* Legacy switches written elsewhere (RPC gi / rt_shadows, ROOD_GI=0). */
    if (last_gi_enabled >= 0 && pc_gi_enabled != last_gi_enabled)
        pc_gi_method = pc_gi_enabled ? PC_GI_PATH : (pc_gi_method == PC_GI_PATH ? PC_GI_OFF : pc_gi_method);
    if (last_rt_shadows >= 0 && pc_rt_shadows != last_rt_shadows)
        pc_shadow_mode = pc_rt_shadows ? PC_SHADOWS_TRACED
                                       : (pc_shadow_mode == PC_SHADOWS_TRACED ? PC_SHADOWS_MAPS : pc_shadow_mode);
    if (!initialised) {
        initialised = true;
        /* Anti-aliasing (antialias.h): ROOD_AA, else the first run's pick for this GPU. */
        const char* aa = getenv("ROOD_AA");
        if (aa && *aa) {
            if (!pc_aa_parse(aa))
                fprintf(stderr, "[aa] ROOD_AA=%s: unknown (off, fxaa, smaa, msaa2/4/8, ssaa[factor], '+')\n", aa);
            else if (pc_settings_env_override)
                pc_settings_env_override("aa"); /* not saved as the user's (project_settings.cpp) */
        } else if (pc_aa_autodetect_pending) {
            pc_lighting_caps_t c = caps_now();
            pc_aa_parse(pc_aa_autodetect(c.integrated));
            fprintf(stderr, "[aa] first run: %s GPU -> %s\n", c.integrated ? "integrated" : "discrete", pc_aa_string());
        }
        pc_aa_autodetect_pending = 0;
        const char* env = getenv("ROOD_LIGHTING_PRESET"); /* a preset name or "auto" */
        if (env && *env) {
            pending = !strcmp(env, "auto") ? -2 : preset_by_name(env);
            env_preset = pending != -1;
        }
        if (pending == -1) { /* no settings file, no request: the built-in values */
            pc_lighting_preset = matching_preset(caps_now());
            take_snapshot();
        }
    }
    if (pending != -1) {
        int preset = pending;
        pending = -1;
        if (preset == -2) {
            pc_lighting_caps_t c = caps_now();
            preset = pc_lighting_autodetect(c.vulkan && c.ray_query, c.integrated);
            snprintf(detected, sizeof detected, "%s, %s%s GPU%s%s -> %s", c.vulkan ? "Vulkan" : "OpenGL",
                c.vulkan ? (c.ray_query ? "ray tracing, " : "no ray tracing, ") : "",
                c.integrated ? "integrated" : "discrete", c.gpu[0] ? " " : "", c.gpu, preset_labels[preset]);
            fprintf(stderr, "[lighting] first run: %s\n", detected);
        }
        pc_lighting_apply_preset(preset);
        if (preset == PC_PRESET_CUSTOM)
            take_snapshot();
        fprintf(stderr, "[lighting] quality preset %s\n", preset_labels[pc_lighting_preset]);
        if (env_preset && pc_settings_env_override) { /* ROOD_LIGHTING_PRESET: not saved as the user's */
            env_preset = false;
            pc_settings_env_override("lighting_preset");
            for (int i = 0; i < key_count; i++)
                pc_settings_env_override(keys[i].name);
        }
    }
    derive_switches();
    if (pc_lighting_preset != PC_PRESET_CUSTOM && have_snapshot)
        for (int i = 0; i < key_count; i++)
            if (keys[i].preset && get(keys[i]) != snapshot[i]) {
                pc_lighting_preset = PC_PRESET_CUSTOM;
                break;
            }
}

extern "C" float pc_lighting_traced_haze_density(void)
{
    return pc_haze_quality == PC_HAZE_HIGH ? pc_fog_density : 0.0f;
}
extern "C" float pc_lighting_low_haze_density(int traced_haze)
{
    if (pc_haze_quality == PC_HAZE_LOW || (pc_haze_quality == PC_HAZE_HIGH && !traced_haze))
        return pc_lights_enabled ? pc_fog_density : 0.0f;
    return 0.0f;
}
extern "C" int pc_lighting_ao_traced(void)
{
    if (!pc_ao || pc_ao_radius <= 0 || pc_ao_strength <= 0)
        return 0;
    pc_lighting_caps_t c = caps_now();
    return c.vulkan && c.ray_query;
}
extern "C" int pc_lighting_ssao(int traced_this_frame)
{
    if (!pc_ao || pc_ao_radius <= 0 || pc_ao_strength <= 0)
        return 0;
    return !(traced_this_frame && pc_lighting_ao_traced());
}
extern "C" float pc_lighting_baked_scale(float room_baked, int light_count)
{
    if (!pc_lights_enabled)
        return 1.0f; /* Original: the PS1's baked light as it is */
    return light_count || room_baked != 1.0f ? room_baked : 1.0f;
}

extern "C" int pc_lighting_set(const char* key, const char* value)
{
    if (!strcmp(key, "lighting_preset")) {
        int p = preset_by_name(value);
        if (!strcmp(value, "auto"))
            p = -2;
        if (p == -1)
            return 0;
        if (p == -2) {
            pending = -2;
            pc_lighting_sync();
        } else {
            pc_lighting_apply_preset(p);
        }
        return 1;
    }
    /* Older names. */
    if (!strcmp(key, "path_traced_light") || !strcmp(key, "gi")) {
        pc_gi_method = atoi(value) || !strncmp(value, "true", 4) ? PC_GI_PATH : PC_GI_OFF;
        derive_switches();
        return 1;
    }
    if (!strcmp(key, "traced_shadows") || !strcmp(key, "rt_shadows")) {
        pc_shadow_mode = atoi(value) || !strncmp(value, "true", 4) ? PC_SHADOWS_TRACED : PC_SHADOWS_MAPS;
        derive_switches();
        return 1;
    }
    Key* k = find(key);
    double v;
    if (!k || !parse_value(*k, value, &v))
        return 0;
    put(*k, v);
    derive_switches();
    return 1;
}
extern "C" int pc_lighting_key_count(void) { return key_count; }
extern "C" const char* pc_lighting_key_name(int i) { return i >= 0 && i < key_count ? keys[i].name : nullptr; }

static void json_text(std::string& s, const char* text)
{
    s += '"';
    for (const char* p = text; *p; p++)
        if (*p == '"' || *p == '\\')
            s += '\\', s += *p;
        else if ((unsigned char)*p >= 32)
            s += *p;
    s += '"';
}
extern "C" int pc_lighting_json(char* out, int size)
{
    pc_lighting_caps_t c = caps_now();
    std::string s = ",\"lighting_preset\":\"";
    s += preset_names[pc_lighting_preset];
    s += '"';
    char number[160];
    for (const Key& k : keys) {
        s += ",\"";
        s += k.name;
        s += "\":";
        if (k.type == ENUM) {
            int v = (int)get(k);
            json_text(s, v >= 0 && v < k.choice_count ? k.choices[v] : "?");
        } else {
            if (k.type == FLOAT_KEY)
                snprintf(number, sizeof number, "%g", get(k));
            else
                snprintf(number, sizeof number, "%d", (int)get(k));
            s += number;
        }
    }
    snprintf(number, sizeof number, ",\"lighting_autodetect\":\"%s\"",
        preset_names[pc_lighting_autodetect(c.vulkan && c.ray_query, c.integrated)]);
    s += number;
    s += ",\"lighting_caps\":{";
    snprintf(number, sizeof number, "\"vulkan\":%d,\"ray_query\":%d,\"gl_compute\":%d,\"traced\":%d,", c.vulkan,
        c.ray_query, c.gl_compute, c.traced);
    s += number;
    snprintf(number, sizeof number, "\"integrated\":%d,\"nrd\":%d,\"probes\":%d,\"gpu\":", c.integrated, c.nrd,
        c.probes);
    s += number;
    json_text(s, c.gpu);
    s += "},\"lighting_unavailable\":{";
    bool first = true;
    for (const Key& k : keys) {
        if (k.type != ENUM)
            continue;
        for (int v = 0; v < k.choice_count; v++) {
            const char* why = reason(c, k, v);
            if (!why)
                continue;
            s += first ? "\"" : ",\"";
            first = false;
            s += k.name;
            s += ':';
            s += k.choices[v];
            s += "\":";
            json_text(s, why);
        }
    }
    s += "}";
    snprintf(out, (size_t)size, "%s", s.c_str());
    return (int)s.size() < size ? (int)s.size() : size - 1;
}

extern "C" void pc_lighting_settings_write(FILE* f)
{
    fprintf(f, "lighting_preset %s\n", preset_names[pc_lighting_preset]);
    for (const Key& k : keys) {
        if (k.type == ENUM) {
            int v = (int)get(k);
            fprintf(f, "%s %s\n", k.name, v >= 0 && v < k.choice_count ? k.choices[v] : "0");
        } else if (k.type == FLOAT_KEY)
            fprintf(f, "%s %g\n", k.name, get(k));
        else
            fprintf(f, "%s %d\n", k.name, (int)get(k));
    }
}
extern "C" int pc_lighting_settings_parse(const char* key, const char* rest)
{
    while (*rest == ' ' || *rest == '\t')
        ++rest;
    if (!strcmp(key, "lighting_preset")) {
        file_preset = preset_by_name(rest);
        return 1;
    }
    if (!strcmp(key, "path_traced_light") || !strcmp(key, "traced_shadows") || find(key)) {
        pc_lighting_set(key, rest);
        return 1;
    }
    return 0;
}
extern "C" void pc_lighting_settings_loaded(int file_exists)
{
    /* A preset re-applies for this backend; Custom keeps the file's values.
     * A settings file from before presets (no lighting_preset) is Custom, so
     * its tuned lighting keys survive; only with no file at all does the
     * first run detect a preset. */
    pending = file_preset >= 0 ? file_preset : file_exists ? PC_PRESET_CUSTOM : -2;
    file_preset = -1;
}

extern "C" const char* pc_lighting_unavailable(const char* key, int value)
{
    Key* k = find(key);
    if (!k)
        return nullptr;
    pc_lighting_caps_t c = caps_now();
    return reason(c, *k, value);
}

extern "C" void pc_perf_gpu_add(double ms)
{
    perf_sum += ms;
    perf_max = ms > perf_max ? ms : perf_max;
    ++perf_count;
}
/* The OpenGL presenter's own counts (gl_present.c, ptgi.c; absent on Vulkan). */
extern "C" void pc_gl_present_stats(unsigned*, double*, unsigned*, int) __attribute__((weak));
extern "C" void pc_gi_cadence_stats(unsigned*, unsigned*, double*, int) __attribute__((weak));
extern "C" int pc_gi_cadence_interval(void) __attribute__((weak));
extern "C" int pc_vk_perf_sections_json(char*, int, int) __attribute__((weak)); /* vk_core.c: GPU sections */
extern "C" int pc_particles_cpu_perf(double* ms, int reset) __attribute__((weak)); /* particles.cpp */
extern "C" float pc_gi_budget_effective(void) __attribute__((weak));
/* "frames": timed frames (ROOD_PERF; frames still in flight are not timed);
 * "presents": every presented frame; "gpu_wait_ms": the CPU's total wait for
 * frames in flight; "gi_traced"/"gi_reused": game-view path tracer updates
 * and presents that showed the last result (GI slower than its budget),
 * "gi_gpu_ms" its measured GPU time, "gi_interval" presents per trace now;
 * "sections" (Vulkan): GPU time per pass; "cpu_sections": CPU time per pass
 * that runs on the CPU ("particles": OpenGL's motes, Vulkan's records). */
extern "C" int pc_perf_json(char* out, int size, int reset)
{
    int n = snprintf(out, (size_t)size, "{\"gpu_ms\":%.4f,\"gpu_max_ms\":%.4f,\"frames\":%d,\"enabled\":%s",
        perf_count ? perf_sum / perf_count : 0.0, perf_max, perf_count, getenv("ROOD_PERF") ? "true" : "false");
    if (pc_gl_present_stats && n < size) {
        unsigned presents, waits;
        double wait_ms;
        pc_gl_present_stats(&presents, &wait_ms, &waits, reset);
        n += snprintf(out + n, (size_t)(size - n), ",\"presents\":%u,\"gpu_wait_ms\":%.2f,\"gpu_waits\":%u", presents,
            wait_ms, waits);
    }
    if (pc_gi_cadence_stats && n < size) {
        unsigned traced, reused;
        double gi_ms;
        pc_gi_cadence_stats(&traced, &reused, &gi_ms, reset);
        n += snprintf(out + n, (size_t)(size - n),
            ",\"gi_traced\":%u,\"gi_reused\":%u,\"gi_gpu_ms\":%.3f,\"gi_budget_ms\":%.2f,\"gi_interval\":%d", traced,
            reused, gi_ms, pc_gi_budget_effective ? pc_gi_budget_effective() : 0.0f,
            pc_gi_cadence_interval ? pc_gi_cadence_interval() : 1);
    }
    if (pc_vk_perf_sections_json && n < size)
        n += pc_vk_perf_sections_json(out + n, size - n, reset);
    /* "cpu_sections": CPU time a frame of passes that run on the CPU (only while they run);
     * with ROOD_PERF also the game thread's sections per vsync and "cpu_counts" (frame_cost.h). */
    double particles_ms = 0;
    char particles[64] = "";
    if (pc_particles_cpu_perf && pc_particles_cpu_perf(&particles_ms, reset) > 0)
        snprintf(particles, sizeof particles, "\"particles\":%.4f", particles_ms);
    if (pc_cost_on && n < size)
        n += pc_cost_json(out + n, size - n, reset, particles);
    else if (particles[0] && n < size)
        n += snprintf(out + n, (size_t)(size - n), ",\"cpu_sections\":{%s}", particles);
    if (n < size)
        n += snprintf(out + n, (size_t)(size - n), "}");
    if (reset)
        perf_sum = perf_max = 0, perf_count = 0;
    return n;
}

/* ---- overlay ---- */

namespace {
/* A combo over a key's choices; unavailable ones disabled with the reason. */
bool choice_combo(const char* label, const char* key, const char* const* labels, const char* tooltip = nullptr)
{
    Key* k = find(key);
    pc_lighting_caps_t c = caps_now();
    int v = (int)get(*k);
    bool changed = false;
    char preview[96];
    snprintf(preview, sizeof preview, "%s%s", labels[v], available(c, *k, v) ? "" : " (not available)");
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < k->choice_count; i++) {
            const char* why = reason(c, *k, i);
            char item[96];
            snprintf(item, sizeof item, "%s%s", labels[i], why ? " (not available)" : "");
            if (ImGui::Selectable(item, i == v, why ? ImGuiSelectableFlags_Disabled : 0)) {
                put(*k, i);
                derive_switches();
                changed = true;
            }
            if (why && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", why);
        }
        ImGui::EndCombo();
    }
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", tooltip);
    return changed;
}
/* Greyed, wrapped text (descriptions in a narrow Details panel). */
void dim(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
void note(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.35f, 1));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
bool checkbox(const char* label, int* value)
{
    bool on = *value != 0;
    if (!ImGui::Checkbox(label, &on))
        return false;
    *value = on;
    return true;
}
bool preset_combo(const char* id, float width)
{
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preset_labels[pc_lighting_preset])) {
        for (int p = 0; p < PC_PRESET_COUNT; p++) {
            if (ImGui::Selectable(preset_labels[p], p == pc_lighting_preset)) {
                pc_lighting_apply_preset(p);
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", preset_help[p]);
        }
        ImGui::EndCombo();
    }
    return changed;
}
/* Anti-aliasing (antialias.h): a post-process filter, MSAA on the 3D and
 * supersampling, each on its own; none of them filters the PS1's texels. */
void antialiasing_panel()
{
    static const char* const posts[3] = { "Off", "FXAA", "SMAA" };
    static const char* const post_help[3] = { "No post-process filter",
        "FXAA on the lit image: cheap, smooths every contrast (texel steps and the HUD too)",
        "SMAA 1x on the lit image, only where the geometry has an edge:\n"
        "texels inside a surface and the 2D/UI stay exactly as drawn" };
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    if (ImGui::BeginCombo("Anti-aliasing filter", posts[pc_gl_aa >= 0 && pc_gl_aa < 3 ? pc_gl_aa : 0])) {
        for (int i = 0; i < 3; i++) {
            if (ImGui::Selectable(posts[i], i == pc_gl_aa))
                pc_gl_aa = i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", post_help[i]);
        }
        ImGui::EndCombo();
    }
    int max_samples = pc_aa_msaa_max ? pc_aa_msaa_max() : 0;
    static const int counts[4] = { 0, 2, 4, 8 };
    char preview[48];
    snprintf(preview, sizeof preview, pc_aa_msaa > 1 ? "%dx" : "Off", pc_aa_msaa);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    if (ImGui::BeginCombo("MSAA (3D edges)", preview)) {
        for (int i = 0; i < 4; i++) {
            char item[48];
            bool ok = counts[i] <= max_samples || counts[i] == 0;
            snprintf(item, sizeof item, counts[i] ? "%dx%s" : "Off", counts[i], ok ? "" : " (not available)");
            if (ImGui::Selectable(item, counts[i] == pc_aa_msaa, ok ? 0 : ImGuiSelectableFlags_Disabled))
                pc_aa_msaa = counts[i];
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Multisampled rooms, characters and effects: coverage only (one texel per pixel,\n"
                          "so textures stay crisp); the 2D/UI is drawn as before.");
    if (pc_aa_msaa > 1 && max_samples < pc_aa_msaa)
        note(max_samples > 1 ? "This GPU/backend runs fewer samples." : "MSAA is not available here.");
    static const float factors[5] = { 0, 1.5f, 2, 3, 4 };
    snprintf(preview, sizeof preview, pc_aa_ssaa > 1 ? "%gx" : "Off", pc_aa_ssaa);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    if (ImGui::BeginCombo("Supersampling", preview)) {
        for (int i = 0; i < 5; i++) {
            char item[32];
            snprintf(item, sizeof item, factors[i] > 0 ? "%gx" : "Off", factors[i]);
            if (ImGui::Selectable(item, factors[i] == pc_aa_ssaa))
                pc_aa_ssaa = factors[i];
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Renders at this multiple of the view (internal resolution, up to 8x) and\n"
                          "filters the lit image down. Costly: lighting runs at the internal resolution.");
    if (pc_aa_ssaa > 1) {
        static const char* const filters[3] = { "Area (box)", "Lanczos-3", "Bilinear" };
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
        ImGui::Combo("Downsample filter", &pc_aa_downsample, filters, 3);
        char text[160];
        snprintf(text, sizeof text, "Internal resolution %dx; the view gets %.2fx per axis", pc_hw_scale,
            pc_gl_view[3] > 0 ? (double)(pc_gl_display[3] > 0 ? pc_gl_display[3] : 240) * pc_hw_scale / pc_gl_view[3] : 0.0);
        dim(text);
    }
}
} // namespace

static bool open_menu;
extern "C" void pc_lighting_open_menu(void) { open_menu = true; }

/* The toolbar's compact quality dropdown. */
extern "C" void pc_lighting_toolbar(void)
{
    ImGui::TextUnformatted("Lighting");
    ImGui::SameLine();
    if (open_menu) { /* BeginCombo's own popup id */
        open_menu = false;
        ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetID("##lighting quality")));
    }
    preset_combo("##lighting quality", ImGui::GetFontSize() * 6.5f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Lighting quality: %s\n%s\nEvery setting: Details > Graphics > Lighting quality",
            preset_labels[pc_lighting_preset], preset_help[pc_lighting_preset]);
}

/* Details > Graphics > Lighting quality (UE's scalability settings): the
 * preset, then each group's settings; changing one makes the preset Custom. */
extern "C" void pc_lighting_panel(void)
{
    pc_lighting_caps_t c = caps_now();
    ImGui::PushID("lighting quality");
    preset_combo("Preset", ImGui::CalcItemWidth());
    ImGui::SameLine();
    if (ImGui::SmallButton("Auto-detect")) {
        pending = -2;
        pc_lighting_sync();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Pick the preset for this GPU: ray tracing -> Ultra (High on integrated GPUs),\n"
                          "else Medium (Low on integrated GPUs)");
    dim(preset_help[pc_lighting_preset]);
    char line[512];
    snprintf(line, sizeof line, "%s: %s%s%s%s%s", c.vulkan ? "Vulkan" : "OpenGL",
        c.vulkan ? (c.ray_query ? "ray tracing" : c.rt_disabled ? "ray tracing off (ROOD_VK_RT=0)" : "no ray tracing")
                 : (c.gl_compute ? "compute path tracer" : "no compute shaders"),
        c.nrd ? ", NRD" : "", c.integrated ? ", integrated GPU" : ", discrete GPU", c.gpu[0] ? " - " : "", c.gpu);
    dim(line);
    if (detected[0]) {
        snprintf(line, sizeof line, "First run: %s", detected);
        dim(line);
    }

    static const char* const gi_labels[] = { "Off", "Screen-space", "Probes", "Path traced" };
    static const char* const denoiser_labels[] = { "Off (raw samples)", "Temporal + a-trous filter",
        "NVIDIA NRD RELAX" };
    static const char* const shadow_labels[] = { "Off", "Shadow maps", "Ray traced" };
    static const char* const filter_labels[] = { "Hard (1 tap)", "Soft (PCF)", "Contact-hardening (PCSS)" };
    static const char* const haze_labels[] = { "Off", "Low (unshadowed)", "High (volumetric shadows)" };

    ImGui::SeparatorText("Dynamic lights");
    checkbox("Dynamic lights", &pc_lights_enabled);
    if (!pc_lights_enabled)
        dim("Off: the PS1's baked vertex lighting only (no shadows, GI or haze).");

    ImGui::SeparatorText("Global illumination");
    choice_combo("GI method", "gi_method", gi_labels);
    if (const char* why = reason(c, *find("gi_method"), pc_gi_method)) {
        char text[300];
        snprintf(text, sizeof text, "%s: no indirect light.", why);
        note(text);
    }
    if (pc_gi_method == PC_GI_PATH || pc_gi_method == PC_GI_PROBES || (pc_ao && c.vulkan && c.ray_query)) {
        ImGui::SliderFloat("GI resolution", &pc_gi_resolution, 0.5f, 4, "%.2fx native");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Traced pixels per native display pixel (also the traced AO's resolution)");
    }
    if (pc_gi_method == PC_GI_PATH) {
        ImGui::SliderInt("Bounces", &pc_gi_bounces, 1, 4);
        ImGui::SliderInt("Rays per pixel", &pc_gi_rays, 1, 8);
        int denoiser = pc_gi_denoise < 0 ? 0 : pc_gi_denoise > 2 ? 2 : pc_gi_denoise;
        choice_combo("Denoiser", "gi_denoiser", denoiser_labels);
        if (denoiser == 2 && pc_gi_denoiser_active && pc_gi_denoiser_active() != 2 && c.traced)
            note(c.nrd ? "NRD unavailable here (failed to start): using the filter"
                       : reason(c, *find("gi_denoiser"), 2));
        else if (denoiser == 2 && &pc_nrd_glow) {
            checkbox("Glow light through NRD", &pc_nrd_glow);
            ImGui::SliderInt("NRD history", &pc_nrd_max_frames, 4, 60, "%d frames");
        }
    }
    if (pc_gi_method == PC_GI_PROBES && pc_probe_panel)
        pc_probe_panel();

    ImGui::SeparatorText("Shadows");
    choice_combo("Shadows", "shadows", shadow_labels);
    bool traced_shadows = pc_shadow_mode == PC_SHADOWS_TRACED && c.traced;
    if (pc_shadow_mode == PC_SHADOWS_TRACED && !c.traced) {
        char text[300];
        snprintf(text, sizeof text, "%s: using shadow maps.", reason(c, *find("shadows"), PC_SHADOWS_TRACED));
        note(text);
    }
    if (pc_shadow_mode == PC_SHADOWS_MAPS || (pc_shadow_mode == PC_SHADOWS_TRACED && !c.traced)) {
        choice_combo("Shadow map filter", "shadow_map_filter", filter_labels,
            "Hard: one tap. PCF: a fixed soft edge. PCSS: a blocker search, so shadows\n"
            "are sharp where they touch and soften with distance (light size: Lighting tab)");
        ImGui::SliderFloat("Shadow map softness", &pc_shadow_softness, 0, 0.08f, "%.3f");
    }
    if (traced_shadows) {
        ImGui::SliderInt("Shadow rays", &pc_shadow_samples, 1, 8);
        ImGui::SliderFloat("Shadow history", &pc_shadow_history, 1, 128, "%.0f frames");
        if (c.vulkan && &pc_shadow_motion_history)
            ImGui::SliderFloat("Moving-shadow history", &pc_shadow_motion_history, 1, 32, "%.0f frames");
    }
    if (!c.traced)
        ImGui::BeginDisabled();
    checkbox("Glows cast shadows", &pc_glow_shadows);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(c.traced ? "Candles, torches and other glows shadow their own light (ray traced)"
                                   : "Ray traced: not available here");
    checkbox("Characters shadow the baked light", &pc_baked_shadows);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(c.traced ? "Contact shadows and glow shadows of characters on the room's baked light"
                                   : "Ray traced: not available here");
    if (!c.traced)
        ImGui::EndDisabled();

    ImGui::SeparatorText("Materials");
    checkbox("Specular (material response)", &pc_specular);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Highlights and reflections on surfaces with a material (Details > Material).\n"
                          "Off: the picture is exactly the one without materials.");
    if (pc_specular) {
        ImGui::SliderFloat("Specular strength", &pc_specular_strength, 0, 4, "%.2f");
        ImGui::SliderFloat("Roughness bias", &pc_roughness_bias, -0.5f, 0.5f, "%+.2f");
        if (!c.vulkan)
            dim("OpenGL draws no material response (Vulkan only).");
        else {
            if (!c.traced)
                ImGui::BeginDisabled();
            ImGui::SliderInt("Reflection bounces", &pc_reflection_bounces, 0, 2,
                pc_reflection_bounces ? "%d" : "Off (lights' highlights only)");
            if (!c.traced)
                ImGui::EndDisabled();
            dim(pc_material_active() ? "Materials on surfaces in view." : "No material in view.");
        }
    }

    ImGui::SeparatorText("Volumetrics");
    choice_combo("Haze", "haze", haze_labels,
        "Low: light scattering in the haze without shadows (cheap, every GPU).\n"
        "High: traced, with light shafts and characters' volumetric shadows.\n"
        "Density per room: Atmosphere tab.");
    if (pc_haze_quality == PC_HAZE_HIGH && !c.traced) {
        char text[300];
        snprintf(text, sizeof text, "%s: using Low.", reason(c, *find("haze"), PC_HAZE_HIGH));
        note(text);
    }
    if (pc_haze_quality == PC_HAZE_HIGH && c.traced) {
        if (c.vulkan && &pc_haze_history) { /* Vulkan: a froxel volume instead of a per-pixel march */
            ImGui::SliderFloat("Haze history blend", &pc_haze_history, 0.02f, 1, "%.2f");
            ImGui::SliderInt("Haze cell (display pixels)", &pc_haze_cell, 2, 8);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("The froxel volume's cell: smaller gives sharper sun shafts and costs more");
        } else
            ImGui::SliderInt("Haze march steps", &pc_fog_steps, 2, 48);
    }

    ImGui::SeparatorText("Ambient occlusion");
    checkbox("Ambient occlusion", &pc_ao);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Darkens the baked and indirect light in creases and under props");
    if (pc_ao) {
        dim(c.vulkan && c.ray_query ? "Ray traced (short rays with the GI)" : "Screen space (from the depth and normals)");
        ImGui::SliderFloat("AO radius", &pc_ao_radius, 8, 160, "%.0f units");
        ImGui::SliderFloat("AO strength", &pc_ao_strength, 0, 1, "%.2f");
    }

    ImGui::SeparatorText("Display (not part of the preset)");
    antialiasing_panel();
    if (pc_hw_interpolate) {
        ImGui::SliderInt("Frame pacing (images per vsync)", &pc_present_rate, 0, 8,
            pc_present_rate ? "%d" : "match display");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Interpolated in-betweens per vsync (2: 120 fps, 4: 240 fps)");
    } else {
        dim("Frame pacing: needs frame interpolation (Display and rendering below)");
    }
    ImGui::PopID();
}
