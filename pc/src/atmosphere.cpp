// The room's ground fog and fog volumes (atmosphere.h): per room, from the
// lighting layers' `env fog` item and `fog <id>` items. The editor's
// Atmosphere tab edits them (volumes with a gizmo in the view), RPC
// `environment` {fog, volumes} reads and sets them, the renderers read
// pc_medium_gpu_get (with the room's uniform haze, a room setting).
#include "atmosphere.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImGuizmo.h"
#include "editor_overlays.h"
#include "editor_undo.h"
#include "dev_state.h"
#include "environment.h"
#include "environment_internal.h"
#include "fog_sim.h"
#include "lighting_layers.h"
#include "lighting_quality.h"
#include "light_list.h"
#include "lights_internal.h"
#include "scene_capture.h"

extern "C" {
extern int pc_lights_enabled;
unsigned pc_vsync_count(void) __attribute__((weak)); /* game time */
extern float pc_fog_anisotropy, pc_fog_density;
int pc_room_settings_parse(const char* line);
void pc_lights_mark_dirty(void);
int pc_lights_snap_at(float x, float y, unsigned modes, int apply, pc_snap_result* out); /* lights.cpp */
}

int pc_fog_global = 1; /* settings key fog: 0 switches every room's ground fog and volumes off */
/* The atmosphere's limits (atmosphere.h; room settings, so an area's mood
 * may change them). */
extern "C" {
float pc_haze_max_opacity = 0.55f, pc_haze_max_light = 0.15f, pc_haze_shafts = 0.5f;
/* Forward scattering's peak (Henyey-Greenstein x 4 pi: g 0.55 would reach 7.7
 * looking toward the moon; 0: no limit). */
float pc_haze_max_forward = 3.0f;
}
/* RPC environment {floor_grid}: 0 lays the ground fog on the room's main
 * floor alone, as before the floor grid (comparisons, tests). */
int pc_fog_floor_grid = 1;
/* RPC environment {sim_field}: the next reply lists the interactive fog's field. */
static bool sim_field_reply;
int pc_haze_cell = 4;
int pc_haze_debug;
float pc_fog_time = -1; /* RPC environment {fog_time}: fixed seconds (-1: live) */

/* The interactive fog's global weights per source (atmosphere.h): Ashley,
 * the other actors, attacks, spells (settings fog_disturb_*). */
float pc_fog_disturb[4] = { 1, 1, 0, 0 };
/* The fog volumes' own renderer (atmosphere.h). */
int pc_fog_volume_render = 1, pc_fog_volume_steps = 48, pc_fog_volume_steps_gl = 20, pc_fog_volume_debug,
    pc_fog_volume_shadows = 1;
float pc_fog_volume_resolution = 0.5f, pc_fog_volume_history = 0.06f, pc_fog_volume_ambient = 1, pc_fog_volume_glow = 0.6f,
      pc_fog_volume_scattering = 0.5f, pc_fog_volume_halo = 2.0f;
int pc_fog_sim_budget = PC_FOG_SIM_MAX; /* settings fog_sim_budget */
static const char* const DISTURB_KEYS[4] = { "disturb_player", "disturb_actors", "disturb_attacks", "disturb_spells" };

static_assert(PC_FOG_SIM_MAX == PC_MEDIUM_TEX_LAYERS, "atmosphere.h: a texture layer per simulated volume");
static_assert(PC_FOG_SIM_GRID < PC_MEDIUM_TEX_SIZE, "atmosphere.h: a field fits a slice");
static_assert(PC_MEDIUM_VEC4S == 165, "the shaders' medium_[] arrays");
namespace {
const float DEG = 3.14159265f / 180.0f;

/* Defaults: the schema's (lighting_layers.cpp, kinds "env" and "fog"). */
struct Fog {
    bool defined = false, enabled = true;
    float density = 0.003f; /* per world unit at its base */
    float height = 48; /* scale height: the density falls by e every `height` units up */
    float offset = 0; /* its base above the room's floor */
    float noise = 0.6f; /* 0..1: how much the drifting noise thins and thickens it */
    float scale = 160; /* the noise's feature size */
    float wind[3] = { 4, 0, 1.5f }; /* drift, world units per game second (slow: mist creeps) */
    float color[3] = { 1, 1, 1 }; /* albedo: the colour it scatters */
    float fill[3] = { 0, 0, 0 }; /* light it scatters from everywhere (radiance) */
    float sky = 1; /* how much of the sky's light the haze and fog scatter */
};
struct Volume {
    std::string id, name; /* id: "fog:<id>" */
    bool enabled = true, sphere = false;
    float pos[3] = { 0, 0, 0 };
    float size[3] = { 256, 128, 256 }; /* full extents */
    float yaw = 0; /* degrees */
    float density = 0.004f;
    float edge = 0.5f; /* the soft part, a fraction of the half size */
    float color[3] = { 1, 1, 1 };
    float fill[3] = { 0, 0, 0 };
    float noise = 0.5f;
    /* The interactive fog (fog_sim.h): the characters, attacks and spells part it. */
    bool sim = false;
    float sim_strength = 1, sim_recover = 3;
    float sim_footprint = 1, sim_trail = 1, sim_wind = 1;
    float detail = 0.5f; /* the detail noise at its soft edges and the field's (0..1) */
    float sim_swirl = 0; /* vorticity: the parted fog curls into eddies (0..4) */
    float shadow = 0; /* self-shadowing: its dense parts shade themselves from the main light (0..2) */
    float disturb[4] = { -1, -1, -1, -1 }; /* per source (DISTURB_KEYS); -1: the global weight */
    /* Its own renderer (atmosphere.h pc_fogvol_gpu): exempt from the room's haze limits. */
    float opacity = 1; /* the most of the scene it may hide along a ray (0..1) */
    float brightness = 1; /* scale on the light it scatters and emits */
    float phase = 0.3f; /* forward scattering (-0.9..0.9): brighter looking toward a light through it */
    float ambient = 1; /* scale on its ambient light (the room's baked light around it, the sky) */
    float glow[3] = { 0, 0, 0 }; /* light it emits per unit density (lit from within: a vat, embers) */
    float glow_core = 0.5f; /* 0: the glow even through it; 1: gathered at its base */
    float taper = 0; /* height profile: 0 even, 1 thinning toward its top */
    float detail_size = 0; /* its billows' size (world units; 0: from the room's wisp size) */
    std::string preset; /* the fields not written come from it (fog_presets.json) */
    int layer = -1; /* its field's slice of the medium texture this vsync (-1: none) */
};
/* Fog presets (pc/assets/environment/fog_presets.json; ROOD_FOG_PRESETS):
 * named sets of a volume's fields (lighting values as text). */
struct Preset {
    std::string id, name, why;
    std::vector<std::pair<std::string, std::string>> fields;
    const std::string* get(const std::string& key) const
    {
        for (const auto& f : fields)
            if (f.first == key)
                return &f.second;
        return nullptr;
    }
};
std::vector<Preset> presets;
bool presets_loaded;
std::string presets_status;
Fog fog;
std::vector<Volume> volumes;
int selected = -1;
int operation = 0; /* gizmo: 0 move, 1 size, 2 turn */
bool show_volumes = true;
int page_frame = -100;
int time_zone = -1, time_room = -1;
double time_origin;

std::string num(double v, int d) { return lighting::format_number(v, d); }
std::string vec3(const float* v, int d) { return num(v[0], d) + "," + num(v[1], d) + "," + num(v[2], d); }
bool get3(const lighting::Item& it, const char* key, float* out)
{
    const std::string* v = it.get(key);
    std::string text = v ? *v : lighting::field_default(it.kind, key);
    float x[3];
    if (sscanf(text.c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]) != 3)
        return false;
    memcpy(out, x, sizeof x);
    return true;
}
float get1(const lighting::Item& it, const char* key)
{
    const std::string* v = it.get(key);
    return (float)atof(v ? v->c_str() : lighting::field_default(it.kind, key));
}
double now_seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
/* Game seconds: vsyncs at 59.94 Hz (the fog stops when the game pauses,
 * follows fast-forward and never depends on the presented frame rate);
 * the wall clock without the game's counter. */
double game_seconds() { return pc_vsync_count ? pc_vsync_count() / 59.94 : now_seconds(); }
/* Seconds the fog has drifted in this room (small numbers: the noise keeps
 * its precision), or the fixed time. */
float medium_time()
{
    if (pc_fog_time >= 0)
        return pc_fog_time;
    if (time_zone != rood_lights::zone || time_room != rood_lights::room) {
        time_zone = rood_lights::zone, time_room = rood_lights::room;
        time_origin = game_seconds();
    }
    return (float)fmod(game_seconds() - time_origin, 4096.0);
}
bool fog_on() { return fog.defined && fog.enabled && fog.density > 0; }
int volumes_on()
{
    int n = 0;
    for (const Volume& v : volumes)
        n += v.enabled && v.density > 0;
    return n;
}

/* ---- presets ---- */
std::string presets_path()
{
    const char* e = getenv("ROOD_FOG_PRESETS");
    return e && *e ? e : "pc/assets/environment/fog_presets.json";
}
bool read_text(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buffer[4096];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof buffer, f)) > 0)
        out.append(buffer, n);
    fclose(f);
    return true;
}
std::string json_text(const prefab::Json& j)
{
    if (j.type == prefab::Json::String)
        return j.string;
    if (j.type == prefab::Json::Bool)
        return j.boolean ? "1" : "0";
    if (j.type == prefab::Json::Number)
        return lighting::format_number(j.number, 6);
    return "";
}
void load_presets()
{
    presets_loaded = true;
    presets.clear();
    std::string text, error;
    prefab::Json root;
    if (!read_text(presets_path(), text)) {
        presets_status = "no presets file (" + presets_path() + ")";
        return;
    }
    if (!prefab::json_parse(text.c_str(), root, &error)) {
        presets_status = presets_path() + ": " + error;
        fprintf(stderr, "[atmosphere] %s\n", presets_status.c_str());
        return;
    }
    if (const prefab::Json* list = root.get("presets"))
        for (const auto& m : list->members) {
            Preset p;
            p.id = m.first;
            for (const auto& f : m.second.members) {
                if (f.first == "name")
                    p.name = f.second.string;
                else if (f.first == "why")
                    p.why = f.second.string;
                else if (lighting::preset_field("fog", f.first))
                    p.fields.emplace_back(f.first, lighting::canonical_value("fog", f.first, json_text(f.second)));
            }
            if (p.name.empty())
                p.name = p.id;
            presets.push_back(p);
        }
    presets_status = std::to_string(presets.size()) + " presets (" + presets_path() + ")";
}
const Preset* find_preset(const std::string& id)
{
    if (!presets_loaded)
        load_presets();
    for (const Preset& p : presets)
        if (p.id == id)
            return &p;
    return nullptr;
}
/* A field of a fog item: as written, else its preset's, else the schema's default. */
std::string fog_field(const lighting::Item& it, const Preset* preset, const char* key)
{
    if (const std::string* v = it.get(key))
        return *v;
    if (preset)
        if (const std::string* v = preset->get(key))
            return *v;
    return lighting::field_default("fog", key);
}
/* A volume's preset fields as text (lighting values). */
std::vector<std::pair<std::string, std::string>> volume_fields(const Volume& v)
{
    std::vector<std::pair<std::string, std::string>> f = {
        { "shape", v.sphere ? "sphere" : "box" },
        { "size", vec3(v.size, 1) },
        { "density", num(v.density, 5) },
        { "edge", num(v.edge, 2) },
        { "color", vec3(v.color, 3) },
        { "fill", vec3(v.fill, 3) },
        { "noise", num(v.noise, 2) },
        { "sim", v.sim ? "1" : "0" },
        { "sim_strength", num(v.sim_strength, 2) },
        { "sim_recover", num(v.sim_recover, 2) },
        { "sim_footprint", num(v.sim_footprint, 2) },
        { "sim_trail", num(v.sim_trail, 2) },
        { "sim_wind", num(v.sim_wind, 2) },
        { "detail", num(v.detail, 2) },
        { "sim_swirl", num(v.sim_swirl, 2) },
        { "shadow", num(v.shadow, 2) },
        { "opacity", num(v.opacity, 2) },
        { "brightness", num(v.brightness, 2) },
        { "phase", num(v.phase, 2) },
        { "ambient", num(v.ambient, 2) },
        { "glow", vec3(v.glow, 3) },
        { "glow_core", num(v.glow_core, 2) },
        { "taper", num(v.taper, 2) },
        { "detail_size", num(v.detail_size, 1) },
    };
    for (int s = 0; s < 4; s++)
        f.emplace_back(DISTURB_KEYS[s], num(v.disturb[s] < 0 ? -1 : v.disturb[s], 2));
    return f;
}
/* Read a volume's preset fields from text (an item's, through its preset). */
void volume_from_fields(Volume& v, const std::function<std::string(const char*)>& get)
{
    v.sphere = get("shape") == "sphere";
    auto f1 = [&](const char* k) { return (float)atof(get(k).c_str()); };
    auto f3 = [&](const char* k, float* out) {
        float x[3];
        if (sscanf(get(k).c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]) == 3)
            memcpy(out, x, sizeof x);
    };
    f3("size", v.size);
    v.density = f1("density");
    v.edge = f1("edge");
    f3("color", v.color);
    f3("fill", v.fill);
    v.noise = f1("noise");
    v.sim = f1("sim") != 0;
    v.sim_strength = f1("sim_strength");
    v.sim_recover = f1("sim_recover");
    v.sim_footprint = f1("sim_footprint");
    v.sim_trail = f1("sim_trail");
    v.sim_wind = f1("sim_wind");
    v.detail = f1("detail");
    v.sim_swirl = f1("sim_swirl");
    v.shadow = f1("shadow");
    v.opacity = f1("opacity");
    v.brightness = f1("brightness");
    v.phase = f1("phase");
    v.ambient = f1("ambient");
    f3("glow", v.glow);
    v.glow_core = f1("glow_core");
    v.taper = f1("taper");
    v.detail_size = f1("detail_size");
    for (int s = 0; s < 4; s++) {
        float w = f1(DISTURB_KEYS[s]);
        v.disturb[s] = w < 0 ? -1 : w;
    }
}
/* Apply a preset to a volume: every preset field takes the preset's value
 * (the schema's default where the preset has none). */
void apply_preset(Volume& v, const Preset& p)
{
    v.preset = p.id;
    volume_from_fields(v, [&](const char* key) -> std::string {
        const std::string* x = p.get(key);
        return x ? *x : lighting::field_default("fog", key);
    });
}
/* Write (or replace) preset `id` with the volume's fields. */
bool save_preset(const std::string& id, const std::string& name, const Volume& v, std::string* message)
{
    std::string text, error;
    prefab::Json root = prefab::Json::object();
    if (read_text(presets_path(), text) && !prefab::json_parse(text.c_str(), root, &error)) {
        *message = presets_path() + ": " + error;
        return false;
    }
    if (!root.get("presets"))
        root.set("presets", prefab::Json::object());
    prefab::Json p = prefab::Json::object();
    const Preset* old = find_preset(id);
    p.set("name", prefab::Json::str(name.empty() ? id : name));
    if (old && !old->why.empty())
        p.set("why", prefab::Json::str(old->why));
    for (const auto& f : volume_fields(v)) {
        if (f.second == lighting::canonical_value("fog", f.first, lighting::field_default("fog", f.first))
            && f.first != "sim")
            continue; /* defaults stay out (sim always says) */
        double x;
        char* end;
        x = strtod(f.second.c_str(), &end);
        p.set(f.first.c_str(), *end == 0 && !f.second.empty() ? prefab::Json::num(x) : prefab::Json::str(f.second));
    }
    root.get("presets")->set(id.c_str(), p);
    std::string out = prefab::json_write(root) + "\n";
    FILE* f = fopen(presets_path().c_str(), "wb");
    if (!f || fwrite(out.data(), 1, out.size(), f) != out.size()) {
        if (f)
            fclose(f);
        *message = "cannot write " + presets_path();
        return false;
    }
    fclose(f);
    load_presets();
    *message = "Saved preset " + id + " (" + presets_path() + ")";
    return true;
}
} // namespace

namespace rood_atmo {

void reset()
{
    fog = Fog();
    volumes.clear();
    selected = -1;
}

bool apply_item(const lighting::Item& it)
{
    if (it.kind == "env" && it.name == "fog") {
        fog = Fog();
        fog.defined = true;
        fog.enabled = get1(it, "enabled") != 0;
        fog.density = get1(it, "density");
        fog.height = get1(it, "height");
        fog.offset = get1(it, "offset");
        fog.noise = get1(it, "noise");
        fog.scale = get1(it, "scale");
        get3(it, "wind", fog.wind);
        get3(it, "color", fog.color);
        get3(it, "fill", fog.fill);
        fog.sky = get1(it, "sky");
        return true;
    }
    if (it.kind == "fog") {
        Volume v;
        v.id = it.name;
        if (const std::string* n = it.get("name"))
            v.name = *n;
        v.enabled = get1(it, "enabled") != 0;
        get3(it, "pos", v.pos);
        v.yaw = get1(it, "yaw");
        if (const std::string* p = it.get("preset"))
            v.preset = *p;
        const Preset* preset = v.preset.empty() ? nullptr : find_preset(v.preset);
        if (!v.preset.empty() && !preset)
            fprintf(stderr, "[atmosphere] fog %s: unknown preset '%s' (its fields as written)\n", v.id.c_str(),
                v.preset.c_str());
        volume_from_fields(v, [&](const char* key) { return fog_field(it, preset, key); });
        for (Volume& o : volumes)
            if (o.id == v.id) {
                v.layer = o.layer;
                o = v;
                return true;
            }
        volumes.push_back(v);
        return true;
    }
    return false;
}

void add_items(lighting::State& s)
{
    if (fog.defined) {
        lighting::Item it;
        it.kind = "env", it.name = "fog";
        it.set("enabled", fog.enabled ? "1" : "0");
        it.set("density", num(fog.density, 5));
        it.set("height", num(fog.height, 1));
        it.set("offset", num(fog.offset, 1));
        it.set("noise", num(fog.noise, 2));
        it.set("scale", num(fog.scale, 1));
        it.set("wind", vec3(fog.wind, 1));
        it.set("color", vec3(fog.color, 3));
        it.set("fill", vec3(fog.fill, 3));
        it.set("sky", num(fog.sky, 3));
        lighting::canonicalize(it);
        s[it.id()] = it;
    }
    for (Volume& v : volumes) {
        if (v.id.empty())
            v.id = lighting::new_name();
        lighting::Item it;
        it.kind = "fog", it.name = v.id;
        if (!v.name.empty())
            it.set("name", v.name);
        it.set("pos", vec3(v.pos, 1));
        it.set("yaw", num(v.yaw, 2));
        it.set("enabled", v.enabled ? "1" : "0");
        /* With a preset: only what differs from it (a preset edit updates
         * every volume using it). An unknown preset keeps every value. */
        const Preset* preset = v.preset.empty() ? nullptr : find_preset(v.preset);
        if (!v.preset.empty())
            it.set("preset", v.preset);
        for (const auto& f : volume_fields(v)) {
            if (preset) {
                const std::string* p = preset->get(f.first);
                std::string base = p ? *p : lighting::field_default("fog", f.first);
                if (lighting::canonical_value("fog", f.first, f.second) == lighting::canonical_value("fog", f.first, base))
                    continue;
            }
            it.set(f.first.c_str(), f.second);
        }
        lighting::canonicalize(it);
        s[it.id()] = it;
    }
}

} // namespace rood_atmo

/* ---- renderers ---- */

extern "C" int pc_medium_has_fog(void)
{
    rood_lights::track_room();
    return pc_fog_global && pc_lights_enabled && pc_haze_quality != PC_HAZE_OFF && (fog_on() || volumes_on());
}

/* ---- the medium's texture: detail noise and the fields ---- */

namespace {
const int TS = PC_MEDIUM_TEX_SIZE, PERIOD = PC_MEDIUM_TEX_SIZE - 1;
unsigned char medium_tex[PC_MEDIUM_TEX_SIZE * PC_MEDIUM_TEX_SIZE * PC_MEDIUM_TEX_DEPTH * 4]; /* RGBA8 */
unsigned medium_tex_serial;
bool medium_tex_noise; /* the noise is in (the main thread's view) */
std::atomic<bool> noise_done { false }; /* the background thread finished it */
std::string layer_ids[PC_MEDIUM_TEX_LAYERS];

inline size_t texel(int x, int y, int z) { return (((size_t)z * TS + y) * TS + x) * 4; }
const float VMAX = 160; /* world units per second: a field's velocity at 0 and 255 */
uint32_t hash3(int x, int y, int z, uint32_t seed)
{
    uint32_t h = seed ^ (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu;
    h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
    return h;
}
float unit(uint32_t h) { return (h & 0xffffff) / 16777215.0f; }
/* Tileable gradient noise: `cells` lattice cells over the period. */
float perlin(float x, float y, float z, int cells, uint32_t seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
    float fx = x - xi, fy = y - yi, fz = z - zi;
    auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    auto grad = [&](int i, int j, int k, float dx, float dy, float dz) {
        uint32_t h = hash3(((i % cells) + cells) % cells, ((j % cells) + cells) % cells, ((k % cells) + cells) % cells, seed);
        float gx = unit(h) * 2 - 1, gy = unit(h * 0x9e3779b9u + 1) * 2 - 1, gz = unit(h * 0x85ebca6bu + 7) * 2 - 1;
        return gx * dx + gy * dy + gz * dz;
    };
    float u = fade(fx), v = fade(fy), w = fade(fz);
    float a = grad(xi, yi, zi, fx, fy, fz) * (1 - u) + grad(xi + 1, yi, zi, fx - 1, fy, fz) * u;
    float b = grad(xi, yi + 1, zi, fx, fy - 1, fz) * (1 - u) + grad(xi + 1, yi + 1, zi, fx - 1, fy - 1, fz) * u;
    float c = grad(xi, yi, zi + 1, fx, fy, fz - 1) * (1 - u) + grad(xi + 1, yi, zi + 1, fx - 1, fy, fz - 1) * u;
    float d = grad(xi, yi + 1, zi + 1, fx, fy - 1, fz - 1) * (1 - u) + grad(xi + 1, yi + 1, zi + 1, fx - 1, fy - 1, fz - 1) * u;
    return (a * (1 - v) + b * v) * (1 - w) + (c * (1 - v) + d * v) * w;
}
/* Tileable cellular (Worley) noise: 1 at a feature point, 0 a cell away. */
float worley(float x, float y, float z, int cells, uint32_t seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
    float best = 9;
    for (int k = -1; k <= 1; k++)
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                int a = xi + i, b = yi + j, c = zi + k;
                uint32_t h = hash3(((a % cells) + cells) % cells, ((b % cells) + cells) % cells, ((c % cells) + cells) % cells, seed);
                float px = a + unit(h), py = b + unit(h * 0x9e3779b9u + 3), pz = c + unit(h * 0x85ebca6bu + 5);
                float dx = px - x, dy = py - y, dz = pz - z;
                best = fminf(best, dx * dx + dy * dy + dz * dz);
            }
    return fmaxf(1.0f - sqrtf(best), 0.0f);
}
/* The detail noise (generated once, by this code; nothing is shipped):
 * Perlin-Worley, three octaves of each over a 64-texel period (billowy
 * cells with soft gradient-noise edges, as volumetric clouds use),
 * normalised to mean 0.5. */
void make_noise()
{
    double started = now_seconds();
    std::vector<float> v((size_t)PERIOD * PERIOD * PERIOD);
    double sum = 0, sq = 0;
    for (int z = 0; z < PERIOD; z++)
        for (int y = 0; y < PERIOD; y++)
            for (int x = 0; x < PERIOD; x++) {
                float p = 0, w = 0, amp = 0.5f, wamp = 0.625f;
                int cells = 4;
                for (int o = 0; o < 3; o++, cells *= 2, amp *= 0.5f) {
                    float s = (float)cells / PERIOD;
                    p += amp * perlin(x * s, y * s, z * s, cells, 17u + o);
                    w += wamp * worley(x * s, y * s, z * s, cells, 101u + o);
                    wamp = o == 0 ? 0.25f : 0.125f;
                }
                float p01 = fminf(fmaxf(p / 0.875f * 0.9f + 0.5f, 0.0f), 1.0f);
                float pw = fminf(fmaxf((p01 - (w - 1.0f)) / (2.0f - w) , 0.0f), 1.0f); /* remap(p, w - 1, 1, 0, 1) */
                float value = pw * 0.5f + w * 0.5f;
                v[((size_t)z * PERIOD + y) * PERIOD + x] = value;
                sum += value, sq += (double)value * value;
            }
    double n = (double)v.size(), mean = sum / n, sd = sqrt(fmax(sq / n - mean * mean, 1e-9));
    for (int z = 0; z < TS; z++)
        for (int y = 0; y < TS; y++)
            for (int x = 0; x < TS; x++) {
                /* r: the noise; g: the same half a period over (a second, independent-looking
                 * sample for the flow's two phases); b: the gradient-noise part alone */
                unsigned char* out = &medium_tex[texel(x, y, z)];
                for (int c = 0; c < 2; c++) {
                    int o = c * PERIOD / 2;
                    float value = v[((size_t)((z + o) % PERIOD) * PERIOD + (y + o) % PERIOD) * PERIOD + (x + o) % PERIOD];
                    float t = 0.5f + (float)((value - mean) / (4 * sd)); /* +-2 sd over 0..1 */
                    out[c] = (unsigned char)lroundf(fminf(fmaxf(t, 0.0f), 1.0f) * 255.0f);
                }
                out[2] = 128, out[3] = 255;
            }
    fprintf(stderr, "[atmosphere] detail noise (64^3 Perlin-Worley) made in %.0f ms (in the background)\n",
        (now_seconds() - started) * 1000);
    noise_done = true;
}
/* Made on a thread from start-up: never inside a game frame (the game's AI
 * budgets its frame by the time since the vsync: a 0.17 s stall there
 * changed what the enemies did). */
struct NoiseStarter {
    NoiseStarter() { std::thread(make_noise).detach(); }
} noise_starter;
/* A layer at rest (density 1: 127.5, as 128; no velocity: 128). */
void layer_rest(int layer)
{
    memset(&medium_tex[texel(0, 0, PC_MEDIUM_TEX_NOISE + layer)], 128, (size_t)TS * TS * 4);
}
/* A field: r its density / 2, g b its velocity (world x, z: 128 + v / VMAX x 127). */
void layer_write(int layer, const float* field, const float* velocity)
{
    for (int j = 0; j < PC_FOG_SIM_GRID; j++)
        for (int i = 0; i < PC_FOG_SIM_GRID; i++) {
            int k = j * PC_FOG_SIM_GRID + i;
            unsigned char* out = &medium_tex[texel(i, j, PC_MEDIUM_TEX_NOISE + layer)];
            out[0] = (unsigned char)lroundf(fminf(fmaxf(field[k] * 0.5f, 0.0f), 1.0f) * 255.0f);
            for (int c = 0; c < 2; c++)
                out[1 + c] = (unsigned char)lroundf(fminf(fmaxf(128.0f + velocity[k * 2 + c] / VMAX * 127.0f, 0.0f), 255.0f));
            out[3] = 255;
        }
}
float volume_weight(const Volume& v, int source)
{
    return v.disturb[source] >= 0 ? v.disturb[source] : fmaxf(pc_fog_disturb[source], 0.0f);
}
rood_fog_sim::Frame frame_of(const Volume& v)
{
    rood_fog_sim::Frame f;
    for (int k = 0; k < 3; k++)
        f.pos[k] = v.pos[k], f.half[k] = fmaxf(v.size[k] * 0.5f, 1.0f),
        f.wind[k] = (fog_on() ? fog.wind[k] : Fog().wind[k]) * v.sim_wind;
    f.yaw_cos = cosf(v.yaw * DEG), f.yaw_sin = sinf(v.yaw * DEG);
    f.strength = v.sim_strength, f.recover = v.sim_recover;
    f.footprint = v.sim_footprint, f.trail = v.sim_trail, f.swirl = v.sim_swirl;
    for (int s = 0; s < 4; s++)
        f.weight[s] = volume_weight(v, s);
    f.weight[rood_fog_sim::GHOST] = 1;
    return f;
}
bool simulated(const Volume& v) { return v.sim && v.enabled && v.density > 0; }
} // namespace

extern "C" const unsigned char* pc_medium_texture(unsigned* serial)
{
    if (!medium_tex_noise) {
        while (!noise_done) /* only if asked within ~0.2 s of start-up */
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        medium_tex_noise = true;
        for (int l = 0; l < PC_MEDIUM_TEX_LAYERS; l++)
            layer_rest(l);
        medium_tex_serial++;
    }
    *serial = medium_tex_serial;
    return medium_tex;
}

/* Once per game vsync (pc_tick): the simulated volumes step (the first
 * fog_sim_budget of the room, in the room's order), their fields go to the
 * texture's layers. */
static void fog_tick(void)
{
    for (Volume& v : volumes)
        v.layer = -1;
    if (!pc_fog_global || !pc_lights_enabled || pc_haze_quality == PC_HAZE_OFF || !pc_vsync_count)
        return;
    unsigned vsync = rood_fog_sim::clock(pc_vsync_count());
    rood_fog_sim::begin_vsync(vsync);
    int layer = 0, budget = pc_fog_sim_budget < 0 ? 0 : pc_fog_sim_budget > PC_FOG_SIM_MAX ? PC_FOG_SIM_MAX : pc_fog_sim_budget;
    bool changed = false;
    int shown = 0;
    for (Volume& v : volumes) {
        if (!v.enabled || v.density <= 0 || shown >= PC_MEDIUM_VOLUMES)
            continue;
        shown++;
        if (!simulated(v) || layer >= budget)
            continue;
        bool moved = rood_fog_sim::step(v.id.c_str(), frame_of(v), vsync);
        const float* field = rood_fog_sim::field(v.id.c_str());
        if (field && (moved || layer_ids[layer] != v.id)) {
            if (!medium_tex_noise) {
                unsigned s;
                pc_medium_texture(&s);
            }
            static float velocity[PC_FOG_SIM_GRID * PC_FOG_SIM_GRID * 2];
            rood_fog_sim::velocity_world(v.id.c_str(), frame_of(v), velocity);
            layer_write(layer, field, velocity);
            layer_ids[layer] = v.id;
            changed = true;
        }
        v.layer = layer++;
    }
    for (int l = layer; l < PC_MEDIUM_TEX_LAYERS; l++)
        if (!layer_ids[l].empty()) {
            layer_ids[l].clear();
            layer_rest(l);
            changed = true;
        }
    if (changed)
        medium_tex_serial++;
    rood_fog_sim::end_vsync(vsync);
}
extern "C" void pc_fog_vsync(void)
{
    rood_lights::track_room();
    fog_tick();
}
/* The editor's live preview: while the game is paused (the editor's free
 * camera, Pause) and the Atmosphere tab shows, the fog keeps stepping on
 * the wall clock (extra ticks of its own clock), so the ghost, the stamps
 * and every change show at once. */
extern "C" int pc_rpc_is_paused(void) __attribute__((weak));
static void fog_preview(void)
{
    static double last = -1, owed = 0;
    double now = now_seconds();
    bool any = false;
    for (const Volume& v : volumes)
        any |= v.sim && v.enabled;
    if (!any || !pc_rpc_is_paused || !pc_rpc_is_paused() || last < 0 || now - last > 0.5) {
        last = now, owed = 0;
        return;
    }
    owed += now - last;
    last = now;
    for (int k = 0; owed >= 1 / 59.94 && k < 4; k++) {
        owed -= 1 / 59.94;
        rood_fog_sim::preview_tick();
        fog_tick();
    }
    if (owed > 0.1)
        owed = 0;
}

extern "C" int pc_medium_gpu_get(pc_medium_gpu* out, float haze_density, const float room_lo[3], const float room_hi[3])
{
    memset(out, 0, sizeof *out);
    rood_lights::track_room();
    if (!pc_lights_enabled)
        return 0;
    float(*m)[4] = out->v;
    bool fogs = pc_medium_has_fog() != 0;
    m[0][0] = haze_density > 0 ? haze_density : 0;
    m[0][1] = fminf(fmaxf(pc_fog_anisotropy, -0.9f), 0.9f);
    m[0][2] = medium_time();
    for (int k = 0; k < 3; k++)
        m[6][k] = room_lo[k] - 1, m[7][k] = room_hi[k] + 1;
    float sky = fog.defined ? fmaxf(fog.sky, 0.0f) : 1.0f;
    if (fogs && fog_on()) {
        /* The floor under each point (the grid), else the room's main floor. */
        static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
        float corner[2], cell[2], floor_y;
        if (pc_fog_floor_grid && pc_scene_floor_grid(heights, corner, cell)) {
            float* g = m[PC_MEDIUM_FLOOR];
            float* q = m[PC_MEDIUM_FLOOR + 1];
            float lo = room_lo[1], span = fmaxf(room_hi[1] - room_lo[1], 1.0f);
            g[0] = corner[0], g[1] = corner[1], g[2] = 1.0f / cell[0], g[3] = 1.0f / cell[1];
            q[0] = lo, q[1] = span / 4095.0f, q[2] = PC_MEDIUM_FLOOR_GRID, q[3] = 1;
            float* data = m[PC_MEDIUM_FLOOR_DATA];
            for (int k = 0; k < PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID; k += 2) {
                int a = (int)lroundf((heights[k] - lo) / span * 4095.0f), b = (int)lroundf((heights[k + 1] - lo) / span * 4095.0f);
                a = a < 0 ? 0 : a > 4095 ? 4095 : a, b = b < 0 ? 0 : b > 4095 ? 4095 : b;
                data[k / 2] = (float)(a * 4096 + b); /* exact: below 2^24 */
            }
            m[1][2] = fog.offset; /* the grid's floor minus it (medium.glsl medium_fog_base) */
        } else {
            if (!pc_scene_floor(&floor_y))
                floor_y = room_hi[1];
            m[1][2] = floor_y - fog.offset; /* y down: up is smaller y */
        }
        m[1][0] = fog.density;
        m[1][1] = fmaxf(fog.height, 1.0f);
        m[1][3] = fminf(fmaxf(fog.noise, 0.0f), 1.0f);
        for (int k = 0; k < 3; k++) {
            m[2][k] = fmaxf(fog.color[k], 0.0f);
            m[3][k] = fmaxf(fog.fill[k], 0.0f);
            m[4][k] = fog.wind[k];
        }
        m[3][3] = 1;
    }
    m[2][3] = 1.0f / fmaxf(fog.defined ? fog.scale : 160.0f, 8.0f);
    if (!fog_on())
        for (int k = 0; k < 3; k++)
            m[4][k] = Fog().wind[k];
    int n = 0;
    if (fogs)
        for (const Volume& v : volumes) {
            if (!v.enabled || v.density <= 0 || n >= PC_MEDIUM_VOLUMES)
                continue;
            float(*b)[4] = m + 8 + 5 * n;
            b[4][3] = fminf(fmaxf(v.detail, 0.0f), 1.0f);
            for (int k = 0; k < 3; k++) {
                b[0][k] = v.pos[k];
                b[1][k] = fmaxf(v.size[k] * 0.5f, 1.0f);
                b[2][k] = fmaxf(v.color[k], 0.0f);
                b[3][k] = fmaxf(v.fill[k], 0.0f);
            }
            b[0][3] = v.density;
            b[1][3] = fminf(fmaxf(v.edge, 0.001f), 1.0f);
            b[2][3] = fminf(fmaxf(v.noise, 0.0f), 1.0f);
            /* its field's layer of the texture + 1 (pc_fog_vsync; 0 none), + 8 for a sphere */
            bool field = v.sim && v.layer >= 0 && v.layer < PC_MEDIUM_TEX_LAYERS && layer_ids[v.layer] == v.id;
            b[3][3] = (float)(field ? v.layer + 1 : 0) + (v.sphere ? 8.0f : 0.0f);
            b[4][0] = cosf(v.yaw * DEG), b[4][1] = sinf(v.yaw * DEG), b[4][2] = fminf(fmaxf(v.shadow, 0.0f), 2.0f);
            n++;
        }
    m[0][3] = (float)n;
    /* the first volumes are drawn by their own renderer (fog_volume.glsl), not the haze */
    int first = pc_fog_volume_render ? (n < PC_FOGVOL_MAX ? n : PC_FOGVOL_MAX) : 0;
    m[4][3] = (float)first;
    if (n > 0) { /* the texture (every pass that samples the volumes binds it) */
        float* t = m[PC_MEDIUM_SIM];
        t[0] = PC_FOG_SIM_GRID, t[1] = PC_MEDIUM_TEX_SIZE, t[2] = PC_MEDIUM_TEX_DEPTH, t[3] = 1;
        /* the detail noise's period: about three wisp sizes (512 units at the default 160) */
        m[PC_MEDIUM_DETAIL][0] = 1.0f / (fmaxf(fog.defined ? fog.scale : 160.0f, 8.0f) * 3.2f);
    }
    /* The limits: so the atmosphere can never hide the scene or blow it out. */
    float* lim = m[PC_MEDIUM_LIMITS];
    lim[0] = 1.0f - fminf(fmaxf(pc_haze_max_opacity, 0.0f), 1.0f);
    lim[1] = fmaxf(pc_haze_max_light, 0.0f);
    lim[2] = 1.0f - fminf(fmaxf(pc_haze_shafts, 0.0f), 1.0f);
    lim[3] = fmaxf(pc_haze_max_forward, 0.0f);
    /* The sky's light in the medium: w its strength (the traced haze samples
     * the sky with occlusion: it sets rgb to 0); rgb where nothing traces it,
     * its mean radiance, about half of it blocked (walls, canopies). */
    pc_env_gpu env;
    m[5][3] = sky;
    bool env_on = pc_env_gpu_get(&env) != 0;
    if (env_on && env.zenith[3] > 0.5f)
        for (int k = 0; k < 3; k++)
            m[5][k] = (env.zenith[k] + 2 * env.horizon[k] + env.ground[k]) * 0.25f * sky * 0.5f;
    /* The volumes' self-shadowing: toward the sun or moon, else from above (y down). */
    bool sun = env_on && env.sun_col[0] + env.sun_col[1] + env.sun_col[2] > 0;
    m[PC_MEDIUM_DETAIL][1] = sun ? env.sun_dir[0] : 0.0f;
    m[PC_MEDIUM_DETAIL][2] = sun ? env.sun_dir[1] : -1.0f;
    m[PC_MEDIUM_DETAIL][3] = sun ? env.sun_dir[2] : 0.0f;
    return m[0][0] > 0 || m[1][0] > 0 || n > first;
}

/* ---- the fog volumes' own renderer (atmosphere.h pc_fogvol_gpu) ---- */

namespace {
/* The room's baked light around a volume (the ambient term that keeps fog
 * in a dark room a dim grey, not black): the captured room's vertex
 * colours (128 = the texture as is), weighted by area and by nearness to
 * the volume, times a typical texture albedo. Cached per room capture and
 * volume placement. */
struct AmbientCache {
    std::string id;
    int serial = -1;
    float pos[3] = { 0, 0, 0 }, size[3] = { 0, 0, 0 };
    float rgb[3] = { 0, 0, 0 };
};
std::vector<AmbientCache> ambient_cache;
void baked_ambient(const Volume& v, float out[3])
{
    int serial = pc_scene_serial();
    for (AmbientCache& c : ambient_cache)
        if (c.id == v.id && c.serial == serial && !memcmp(c.pos, v.pos, sizeof c.pos) && !memcmp(c.size, v.size, sizeof c.size)) {
            memcpy(out, c.rgb, sizeof c.rgb);
            return;
        }
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data(&count);
    double sum[3] = { 0, 0, 0 }, weight = 0;
    float reach = fmaxf(fmaxf(v.size[0], v.size[1]), v.size[2]) * 0.75f + 256.0f;
    for (int i = 0; t && i < count; i++) {
        if (t[i].code & 2) /* glows: they are lights here */
            continue;
        const float *a = t[i].vertex[0].position, *b = t[i].vertex[1].position, *c = t[i].vertex[2].position;
        float e1[3], e2[3], centre[3];
        for (int k = 0; k < 3; k++)
            e1[k] = b[k] - a[k], e2[k] = c[k] - a[k], centre[k] = (a[k] + b[k] + c[k]) / 3 - v.pos[k];
        float cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];
        double area = 0.5 * sqrt((double)cx * cx + (double)cy * cy + (double)cz * cz);
        double d2 = (double)centre[0] * centre[0] + (double)centre[1] * centre[1] + (double)centre[2] * centre[2];
        double w = area / (1.0 + d2 / ((double)reach * reach));
        for (int k = 0; k < 3; k++)
            sum[k] += w * (t[i].vertex[0].rgb[k] + t[i].vertex[1].rgb[k] + t[i].vertex[2].rgb[k]) / (3.0 * 128.0);
        weight += w;
    }
    for (int k = 0; k < 3; k++)
        out[k] = weight > 0 ? (float)(sum[k] / weight) * 0.45f : 0.0f; /* x a typical texture's albedo */
    AmbientCache c;
    c.id = v.id, c.serial = serial;
    memcpy(c.pos, v.pos, sizeof c.pos), memcpy(c.size, v.size, sizeof c.size), memcpy(c.rgb, out, sizeof c.rgb);
    for (AmbientCache& o : ambient_cache)
        if (o.id == v.id) {
            o = c;
            return;
        }
    if (ambient_cache.size() > 64)
        ambient_cache.clear();
    ambient_cache.push_back(c);
}
unsigned hash_words(unsigned h, const void* data, size_t bytes)
{
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < bytes; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
/* A volume's distance from a point (0 inside), through its turned frame. */
float volume_distance(const Volume& v, const float p[3])
{
    float c = cosf(v.yaw * DEG), s = sinf(v.yaw * DEG), d[3] = { p[0] - v.pos[0], p[1] - v.pos[1], p[2] - v.pos[2] };
    float local[3] = { c * d[0] - s * d[2], d[1], s * d[0] + c * d[2] }, out = 0;
    for (int k = 0; k < 3; k++) {
        float e = fabsf(local[k]) - fmaxf(v.size[k] * 0.5f, 1.0f);
        out += e > 0 ? e * e : 0;
    }
    return sqrtf(out);
}
struct Candidate {
    float pos[4], col[4], dir[4];
    float importance;
    unsigned key;
};
pc_light_gpu fogvol_lights[PC_LIGHTS_MAX];
int fogvol_volume_lights[PC_FOGVOL_MAX];
float fogvol_volume_light_rgb[PC_FOGVOL_MAX][PC_FOGVOL_LIGHTS][4]; /* each slot's colour and type (RPC) */
float fogvol_volume_ambient[PC_FOGVOL_MAX][3];
int fogvol_drawn;
} // namespace

extern "C" int pc_fogvol_gpu_get(pc_medium_gpu* medium, pc_fogvol_gpu* out, pc_fogvol_info* info, const float room_lo[3],
    const float room_hi[3])
{
    memset(out, 0, sizeof *out);
    if (info)
        memset(info, 0, sizeof *info);
    fogvol_drawn = 0;
    if (!pc_fog_volume_render) {
        memset(medium, 0, sizeof *medium);
        return 0;
    }
    pc_medium_gpu_get(medium, 0, room_lo, room_hi);
    int count = (int)(medium->v[4][3] + 0.5f);
    if (count <= 0)
        return 0;
    /* The volumes the record holds, in its order (pc_medium_gpu_get). */
    const Volume* drawn[PC_FOGVOL_MAX];
    int n = 0;
    for (const Volume& v : volumes)
        if (v.enabled && v.density > 0 && n < count)
            drawn[n++] = &v;
    count = n;
    /* Every light and glow: candidates for each volume's few. */
    std::vector<Candidate> all;
    int lights = pc_lights_list(fogvol_lights, PC_LIGHTS_MAX);
    for (int i = 0; i < lights; i++) {
        const pc_light_gpu& l = fogvol_lights[i];
        Candidate c;
        memcpy(c.pos, l.pos, sizeof c.pos), memcpy(c.col, l.col, sizeof c.col), memcpy(c.dir, l.dir, sizeof c.dir);
        c.importance = 0;
        int what[5] = { (int)floorf(l.pos[0] / 16), (int)floorf(l.pos[1] / 16), (int)floorf(l.pos[2] / 16), (int)l.col[3],
            (int)(l.pos[3] / 16) };
        c.key = hash_words(2166136261u, what, sizeof what) | 1u;
        all.push_back(c);
    }
    static float gp[64][4], gc[64][4], gs[64][4];
    int glows = pc_fog_glows(gp, gc, gs, 64);
    for (int i = 0; i < glows; i++) {
        Candidate c;
        memcpy(c.pos, gp[i], sizeof c.pos);
        for (int k = 0; k < 3; k++)
            c.col[k] = gc[i][k] * pc_fog_volume_glow;
        bool column = gs[i][3] != 0;
        c.col[3] = column ? 4.0f : 3.0f;
        c.dir[0] = column ? gs[i][0] : 0, c.dir[1] = column ? gs[i][1] : 0, c.dir[2] = column ? gs[i][2] : 0, c.dir[3] = 0;
        c.importance = 0;
        int what[5] = { (int)floorf(gp[i][0] / 16), (int)floorf(gp[i][1] / 16), (int)floorf(gp[i][2] / 16), 3 + column,
            (int)(gp[i][3] / 16) };
        c.key = hash_words(2166136261u, what, sizeof what) | 1u;
        all.push_back(c);
    }
    pc_env_gpu env;
    bool env_on = pc_env_gpu_get(&env) != 0;
    bool sun = env_on && env.sun_col[0] + env.sun_col[1] + env.sun_col[2] > 0;
    float(*f)[4] = out->v;
    f[0][0] = (float)count;
    f[0][1] = fmaxf(pc_fog_volume_halo, 0.0f);
    if (sun) {
        for (int k = 0; k < 3; k++)
            f[1][k] = env.sun_col[k], f[2][k] = env.sun_dir[k];
        f[1][3] = 1;
    }
    f[2][3] = fmaxf(pc_fog_volume_scattering, 0.0f);
    float sky[3] = { 0, 0, 0 };
    if (env_on && env.zenith[3] > 0.5f)
        for (int k = 0; k < 3; k++)
            sky[k] = (env.zenith[k] + 2 * env.horizon[k] + env.ground[k]) * 0.25f * (fog.defined ? fmaxf(fog.sky, 0.0f) : 1.0f)
                * 0.5f;
    float room_period = 1.0f / fmaxf(medium->v[PC_MEDIUM_DETAIL][0], 1e-6f);
    /* stable slots: a light keeps last frame's slot while it stays among the chosen (no history restarts) */
    static unsigned previous[PC_FOGVOL_MAX][PC_FOGVOL_LIGHTS];
    for (int vi = 0; vi < count; vi++) {
        const Volume& v = *drawn[vi];
        float(*b)[4] = f + 3 + PC_FOGVOL_STRIDE * vi;
        /* its most important lights: brightness x falloff at its nearest point */
        std::vector<int> order;
        for (int i = 0; i < (int)all.size(); i++) {
            Candidate& c = all[i];
            float d = volume_distance(v, c.pos);
            float x = fminf(d / fmaxf(c.pos[3], 1.0f), 1.0f), a = (1 - x * x) * (1 - x * x);
            c.importance = a * (0.2126f * c.col[0] + 0.7152f * c.col[1] + 0.0722f * c.col[2]);
            if (c.importance > 1e-4f)
                order.push_back(i);
        }
        std::stable_sort(order.begin(), order.end(), [&](int a, int c) { return all[a].importance > all[c].importance; });
        int used = (int)order.size() < PC_FOGVOL_LIGHTS ? (int)order.size() : PC_FOGVOL_LIGHTS;
        int slot_of[PC_FOGVOL_LIGHTS];
        bool taken[PC_FOGVOL_LIGHTS] = { false };
        for (int i = 0; i < used; i++) {
            slot_of[i] = -1;
            for (int s = 0; s < PC_FOGVOL_LIGHTS; s++)
                if (!taken[s] && previous[vi][s] == all[order[i]].key) {
                    slot_of[i] = s, taken[s] = true;
                    break;
                }
        }
        for (int i = 0; i < used; i++)
            if (slot_of[i] < 0)
                for (int s = 0; s < PC_FOGVOL_LIGHTS; s++)
                    if (!taken[s]) {
                        slot_of[i] = s, taken[s] = true;
                        break;
                    }
        unsigned keys[PC_FOGVOL_LIGHTS] = { 0 };
        int top = 0;
        for (int i = 0; i < used; i++) {
            const Candidate& c = all[order[i]];
            int s = slot_of[i];
            keys[s] = c.key;
            top = s + 1 > top ? s + 1 : top;
            memcpy(b[4 + 3 * s], c.pos, sizeof c.pos);
            memcpy(b[5 + 3 * s], c.col, sizeof c.col);
            memcpy(b[6 + 3 * s], c.dir, sizeof c.dir);
        }
        memcpy(previous[vi], keys, sizeof keys);
        fogvol_volume_lights[vi] = used;
        for (int s = 0; s < PC_FOGVOL_LIGHTS; s++)
            memcpy(fogvol_volume_light_rgb[vi][s], b[5 + 3 * s], sizeof fogvol_volume_light_rgb[vi][s]);
        b[0][0] = fminf(fmaxf(v.opacity, 0.0f), 1.0f);
        b[0][1] = fmaxf(v.brightness, 0.0f);
        b[0][2] = fminf(fmaxf(v.phase, -0.9f), 0.9f);
        b[0][3] = (float)top;
        for (int k = 0; k < 3; k++)
            b[1][k] = fmaxf(v.glow[k], 0.0f);
        b[1][3] = fminf(fmaxf(v.glow_core, 0.0f), 1.0f);
        float baked[3];
        baked_ambient(v, baked);
        float scale = fmaxf(pc_fog_volume_ambient, 0.0f) * fmaxf(v.ambient, 0.0f);
        for (int k = 0; k < 3; k++)
            b[2][k] = fogvol_volume_ambient[vi][k] = (baked[k] + sky[k]) * scale;
        b[2][3] = fminf(fmaxf(v.taper, 0.0f), 1.0f);
        b[3][0] = 1.0f / fmaxf(v.detail_size > 0 ? v.detail_size : room_period * 0.6f, 8.0f);
        b[3][1] = 0.5f + fminf(fmaxf(v.shadow, 0.0f), 4.0f);
        b[3][3] = (float)vi; /* its slab of the light grid */
        if (info) {
            memcpy(info->keys[vi], keys, sizeof keys);
            info->keys[vi][6] = sun ? hash_words(2166136261u, env.sun_dir, 3 * sizeof(float)) | 1u : 0;
            info->keys[vi][7] = 1;
            float shape[15] = { v.pos[0], v.pos[1], v.pos[2], v.size[0], v.size[1], v.size[2], v.yaw, v.density, v.edge,
                v.shadow, v.detail, (float)v.sphere, v.taper, v.opacity, v.noise };
            info->shape[vi] = hash_words(2166136261u, shape, sizeof shape) | 1u;
        }
    }
    if (info)
        info->count = count;
    fogvol_drawn = count;
    return count;
}

extern "C" unsigned pc_medium_hash(void)
{
    lighting::State s;
    rood_atmo::add_items(s);
    unsigned h = 2166136261u;
    for (const auto& kv : s)
    {
        lighting::Entry e;
        e.verb = lighting::DEFINE;
        e.id = kv.first;
        e.item = kv.second;
        for (unsigned char c : lighting::format_entry(e))
            h = (h ^ c) * 16777619u;
    }
    return h;
}

/* ---- editor: the Atmosphere tab's fog and volumes ---- */

namespace {
void tooltip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}
bool color_row(const char* label, float* rgb)
{
    return ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
}
/* The highest up-facing room surface under (x, z) at or below world y
 * `from` (y down: its y >= from); with none, the nearest one above it.
 * Brute force over the captured room's triangles (a few rays). */
int floor_under(float x, float z, float from, float* out)
{
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data(&count);
    bool below = false, above = false;
    float best_below = 1e30f, best_above = -1e30f;
    for (int i = 0; t && i < count; i++) {
        const float *a = t[i].vertex[0].position, *b = t[i].vertex[1].position, *c = t[i].vertex[2].position;
        float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        float ny = e1[2] * e2[0] - e1[0] * e2[2]; /* the normal's y (sign: winding) */
        float nx = e1[1] * e2[2] - e1[2] * e2[1], nz = e1[0] * e2[1] - e1[1] * e2[0];
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len < 1e-6f || fabsf(ny) / len < 0.5f)
            continue; /* walls and steep slopes are no floor */
        /* barycentric in x, z */
        float d = e1[0] * e2[2] - e1[2] * e2[0];
        if (fabsf(d) < 1e-6f)
            continue;
        float px = x - a[0], pz = z - a[2];
        float u = (px * e2[2] - pz * e2[0]) / d, v = (e1[0] * pz - e1[2] * px) / d;
        if (u < -1e-4f || v < -1e-4f || u + v > 1 + 1e-4f)
            continue;
        float y = a[1] + u * e1[1] + v * e2[1];
        if (y >= from)
            below = true, best_below = fminf(best_below, y);
        else
            above = true, best_above = fmaxf(best_above, y);
    }
    if (below)
        *out = best_below;
    else if (above)
        *out = best_above;
    return below ? 1 : above ? 2 : 0; /* 1: below, 2: only above */
}
/* Drop to floor (End): the volume's bottom face rests on the highest floor
 * under its centre and its footprint's corners. */
bool drop_to_floor(Volume& v)
{
    float half[3] = { v.size[0] * 0.5f, v.size[1] * 0.5f, v.size[2] * 0.5f };
    if (v.sphere)
        half[0] *= 0.7f, half[2] *= 0.7f;
    float c = cosf(v.yaw * DEG), s = sinf(v.yaw * DEG);
    float from = v.pos[1] - half[1] - 64; /* a little above its top */
    /* the highest floor below any of the points (it rests on the floor, never
     * sinks into it); points over a pit or a ledge with nothing below are left
     * out; only when no point has a floor below, the nearest one above */
    float best = 1e30f, up = -1e30f;
    bool any = false, any_up = false;
    static const float pts[5][2] = { { 0, 0 }, { 0.9f, 0.9f }, { -0.9f, 0.9f }, { 0.9f, -0.9f }, { -0.9f, -0.9f } };
    for (const auto& q : pts) {
        float lx = q[0] * half[0], lz = q[1] * half[2];
        float x = v.pos[0] + c * lx + s * lz, z = v.pos[2] - s * lx + c * lz; /* the inverse turn (medium.glsl) */
        float y;
        int hit = floor_under(x, z, from, &y);
        if (hit == 1)
            best = fminf(best, y), any = true;
        else if (hit == 2)
            up = fmaxf(up, y), any_up = true;
    }
    if (!any && !any_up)
        return false;
    v.pos[1] = (any ? best : up) - half[1];
    return true;
}
/* Fit to floor area: the volume's footprint becomes the level floor around
 * its centre (the room's floor grid cells within 12 units of the height
 * under it, connected to it), turned square to the room, dropped onto it. */
bool fit_to_floor(Volume& v)
{
    const int n = PC_SCENE_FLOOR_GRID;
    static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    float corner[2], cell[2];
    if (!pc_scene_floor_grid(heights, corner, cell))
        return false;
    int i0 = (int)floorf((v.pos[0] - corner[0]) / cell[0]), j0 = (int)floorf((v.pos[2] - corner[1]) / cell[1]);
    if (i0 < 0 || j0 < 0 || i0 >= n || j0 >= n)
        return false;
    float h0 = heights[j0 * n + i0];
    std::vector<char> seen(n * n, 0);
    std::vector<int> stack = { j0 * n + i0 };
    seen[j0 * n + i0] = 1;
    int lo[2] = { i0, j0 }, hi[2] = { i0, j0 };
    while (!stack.empty()) {
        int k = stack.back();
        stack.pop_back();
        int i = k % n, j = k / n;
        lo[0] = std::min(lo[0], i), lo[1] = std::min(lo[1], j), hi[0] = std::max(hi[0], i), hi[1] = std::max(hi[1], j);
        const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
        for (int e = 0; e < 4; e++) {
            int a = i + di[e], b = j + dj[e];
            if (a < 0 || b < 0 || a >= n || b >= n || seen[b * n + a] || fabsf(heights[b * n + a] - h0) > 12)
                continue;
            seen[b * n + a] = 1;
            stack.push_back(b * n + a);
        }
    }
    v.yaw = 0;
    v.pos[0] = corner[0] + (lo[0] + hi[0] + 1) * 0.5f * cell[0];
    v.pos[2] = corner[1] + (lo[1] + hi[1] + 1) * 0.5f * cell[1];
    v.size[0] = (hi[0] - lo[0] + 1) * cell[0];
    v.size[2] = (hi[1] - lo[1] + 1) * cell[1];
    v.pos[1] = h0 - v.size[1] * 0.5f; /* on that floor (Drop to floor would rest it on the highest point under it) */
    return true;
}
/* A new volume in front of the camera: on the surface under the view's
 * centre (its bottom on it), else nowhere. */
bool add_volume()
{
    float rect[4];
    rood_lights::Placement where;
    if (!rood_lights::view_rect(rect)
        || !rood_lights::place_compute("light:point", rect[0] + rect[2] * 0.5f, rect[1] + rect[3] * 0.5f,
            PC_SNAP_MODE_SURFACE, where))
        return false;
    Volume v;
    v.id = lighting::new_name();
    memcpy(v.pos, where.surface, sizeof v.pos);
    v.pos[1] -= v.size[1] * 0.5f - 8; /* y down: its bottom just under the floor */
    volumes.push_back(v);
    selected = (int)volumes.size() - 1;
    rood_lights::select_none();
    return true;
}
} // namespace

namespace {
bool show_field = true; /* the selected volume's field on its floor in the view */
bool ghost_mode; /* the surface point under the mouse is a source (preview) */
const char* const SOURCE_LABELS[4] = { "Ashley", "Other characters", "Attacks", "Spells" };
const char* const SOURCE_TIPS[4] = {
    "Ashley clears it around her feet and leaves a wake when she moves.",
    "Enemies and other characters do the same (their own footprint sizes).",
    "Weapon swings (any actor's, chain steps too) swirl an arc in front of the swinger.",
    "Spells and items burst it where they land; casting pushes a ring around the caster.",
};
/* The preset row: pick one (it sets the preset's fields), save the
 * volume's fields as a preset. */
bool preset_ui(Volume& v)
{
    bool changed = false;
    if (!presets_loaded)
        load_presets();
    const Preset* current = v.preset.empty() ? nullptr : find_preset(v.preset);
    std::string label = v.preset.empty() ? "(none)" : current ? current->name : v.preset + " (missing)";
    if (ImGui::BeginCombo("Preset", label.c_str())) {
        if (ImGui::Selectable("(none)", v.preset.empty())) {
            v.preset.clear(); /* keeps the values: they are written out in full now */
            changed = true;
        }
        for (const Preset& p : presets) {
            if (ImGui::Selectable(p.name.c_str(), p.id == v.preset)) {
                apply_preset(v, p);
                changed = true;
                pc_undo_hint("Apply fog preset");
            }
            if (ImGui::IsItemHovered() && !p.why.empty())
                ImGui::SetTooltip("%s", p.why.c_str());
        }
        ImGui::EndCombo();
    }
    tooltip("Named looks (pc/assets/environment/fog_presets.json). Picking one sets the volume's look and\n"
            "interaction to it; the volume then saves only what you change after (so editing the preset\n"
            "updates every volume using it). (none) keeps the values and writes them all.");
    if (current) {
        if (ImGui::SmallButton("Reset to preset")) {
            apply_preset(v, *current);
            changed = true;
            pc_undo_hint("Reset to fog preset");
        }
        ImGui::SameLine();
    }
    static char preset_id[64];
    static std::string message;
    if (ImGui::SmallButton("Save as preset...")) {
        snprintf(preset_id, sizeof preset_id, "%s", v.preset.c_str());
        ImGui::OpenPopup("save_fog_preset");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload presets")) {
        load_presets();
        message = presets_status;
    }
    if (ImGui::BeginPopup("save_fog_preset")) {
        ImGui::InputText("Name", preset_id, sizeof preset_id);
        ImGui::TextDisabled("lowercase_with_underscores; an existing one is replaced");
        if (ImGui::Button("Save") && preset_id[0]) {
            std::string id = preset_id;
            for (char& c : id)
                c = c == ' ' ? '_' : (char)tolower((unsigned char)c);
            std::string name = preset_id;
            if (const Preset* old = find_preset(id))
                name = old->name;
            if (save_preset(id, name, v, &message)) {
                v.preset = id;
                changed = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!message.empty())
        ImGui::TextDisabled("%s", message.c_str());
    return changed;
}
/* A source weight row: this volume's own weight or the global one. */
bool weight_row(Volume& v, int s)
{
    bool changed = false;
    ImGui::PushID(s);
    bool own = v.disturb[s] >= 0;
    if (ImGui::Checkbox("##own", &own)) {
        v.disturb[s] = own ? fmaxf(pc_fog_disturb[s], 0.0f) : -1;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Its own weight here (else the global one: Interactive fog, all rooms)");
    ImGui::SameLine();
    if (own) {
        changed |= ImGui::SliderFloat(SOURCE_LABELS[s], &v.disturb[s], 0, 2, v.disturb[s] <= 0 ? "off" : "%.2f");
    } else {
        ImGui::BeginDisabled();
        float g = pc_fog_disturb[s];
        ImGui::SliderFloat(SOURCE_LABELS[s], &g, 0, 2, g <= 0 ? "off (global)" : "%.2f (global)");
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s\n0: off.", SOURCE_TIPS[s]);
    ImGui::PopID();
    return changed;
}
/* The selected volume's interaction: on/off, how it parts and settles, each
 * source's weight, the preview tools. */
bool interaction_ui(Volume& v)
{
    bool changed = false;
    ImGui::SeparatorText("Interaction");
    changed |= ImGui::Checkbox("Characters, attacks and spells part it", &v.sim);
    tooltip("Interactive fog: sources clear it, push it around and leave a soft wake that drifts with the\n"
            "wind and settles back. Up to fog_sim_budget (4) volumes per room; it only ticks while a\n"
            "source is in or near it.");
    ImGui::BeginDisabled(!v.sim);
    changed |= ImGui::SliderFloat("Strength", &v.sim_strength, 0, 2, "%.2f");
    tooltip("How much a source clears (1: a character standing still keeps a fifth of the fog at its feet).");
    changed |= ImGui::SliderFloat("Recovery time", &v.sim_recover, 0.3f, 12, "%.1f s");
    tooltip("How long the fog takes to settle back.");
    changed |= ImGui::SliderFloat("Footprint", &v.sim_footprint, 0.25f, 3, "x %.2f");
    tooltip("Scale on every source's size (a character's is its drop shadow's radius).");
    changed |= ImGui::SliderFloat("Trail", &v.sim_trail, 0, 3, "%.2f");
    tooltip("How much of a source's motion the fog takes and how long it keeps it: 0 a hole without a\n"
            "wake, 1 a short wake, 3 long swirling trails.");
    changed |= ImGui::SliderFloat("Wind", &v.sim_wind, 0, 4, "x %.2f");
    changed |= ImGui::SliderFloat("Swirl", &v.sim_swirl, 0, 4, "%.2f");
    tooltip("Vorticity: what is pushed curls into eddies and rolls (an incompressible flow) instead of\n"
            "spreading out; 0 a plain soft wake. Costs a little more CPU while it moves.");
    tooltip("How much the room's fog wind (env fog wind) carries the parted fog (0: still air).");
    ImGui::EndDisabled();
    changed |= ImGui::SliderFloat("Detail", &v.detail, 0, 1, "%.2f");
    tooltip("Billows and fine wisps from a baked 3D noise, carried along by the flow: they follow the\n"
            "wake and the swirls (0: smooth).");
    changed |= ImGui::SliderFloat("Self-shadow", &v.shadow, 0, 2, "%.2f");
    tooltip("Its dense parts shade themselves from the main light (the sun or moon; from above without\n"
            "one): dark cores, lit rims. 0: evenly lit mist.");
    ImGui::BeginDisabled(!v.sim);
    ImGui::TextDisabled("Sources (ticked: this volume's own weight)");
    for (int s = 0; s < 4; s++)
        changed |= weight_row(v, s);
    ImGui::Checkbox("Show the field", &show_field);
    tooltip("Its density on its floor in the view: red cleared, blue piled.");
    ImGui::SameLine();
    ImGui::Checkbox("Ghost at the mouse", &ghost_mode);
    tooltip("The surface point under the mouse parts the fog like a character (a preview without\n"
            "walking). The game must be running: the fog steps on game time.");
    struct pc_dev_state st;
    memset(&st, 0, sizeof st);
    if (pc_dev_state)
        pc_dev_state(&st);
    ImGui::BeginDisabled(!st.has_player);
    static const char* names[rood_fog_sim::SHAPES] = { "Burst", "Ring", "Swing", "Vortex" };
    for (int shape = 0; shape < rood_fog_sim::SHAPES; shape++) {
        if (shape)
            ImGui::SameLine();
        if (ImGui::SmallButton(names[shape])) {
            rood_fog_sim::Event e;
            e.source = rood_fog_sim::GHOST, e.shape = shape;
            e.pos[0] = (float)st.x, e.pos[1] = (float)st.y, e.pos[2] = (float)st.z;
            float a = st.facing * (6.2831853f / 4096);
            e.dir[0] = -sinf(a), e.dir[1] = -cosf(a);
            e.radius = shape == rood_fog_sim::BURST ? 130 : shape == rood_fog_sim::SWING ? 95 : 110;
            e.frames = shape == rood_fog_sim::SWING ? 12 : 18;
            rood_fog_sim::emit(e);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("at Ashley (stamps, whatever the weights)");
    ImGui::EndDisabled();
    rood_fog_sim::Stats s;
    if (rood_fog_sim::stats(v.id.c_str(), &s))
        ImGui::TextDisabled("field: least %.2f, mean %.3f, %s, %d source%s, %u steps, %.3f ms", s.least, s.mean,
            s.awake ? "ticking" : "at rest", s.actors, s.actors == 1 ? "" : "s", s.steps, s.ms);
    else if (v.sim)
        ImGui::TextDisabled("%s", v.layer < 0 ? "not simulated (over the room's budget, the fog off, or no source yet)"
                                             : "no field yet");
    if (ImGui::SmallButton("Settle now"))
        rood_fog_sim::reset();
    ImGui::EndDisabled();
    return changed;
}
/* Every room: the global weights (settings fog_disturb_*), the budget. */
void global_interaction_ui()
{
    if (!ImGui::TreeNode("Interactive fog: all rooms"))
        return;
    static const char* keys[4] = { "player", "actors", "attacks", "spells" };
    for (int s = 0; s < 4; s++) {
        ImGui::SliderFloat(SOURCE_LABELS[s], &pc_fog_disturb[s], 0, 2, pc_fog_disturb[s] <= 0 ? "off" : "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nSettings key fog_disturb_%s (File > Save settings); a volume's own weight wins. 0: off.",
                SOURCE_TIPS[s], keys[s]);
    }
    ImGui::SliderInt("Simulated volumes per room", &pc_fog_sim_budget, 0, PC_FOG_SIM_MAX);
    tooltip("Settings key fog_sim_budget: the first this many interactive volumes of a room simulate.");
    unsigned attacks, spells;
    rood_fog_sim::event_counts(&attacks, &spells);
    ImGui::TextDisabled("seen this session: %u attacks, %u spells", attacks, spells);
    ImGui::TreePop();
}
} // namespace

/* Returns true when something changed (environment.cpp's page marks the room). */
bool pc_atmosphere_page(void)
{
    page_frame = ImGui::GetFrameCount();
    fog_preview();
    bool changed = false;
    ImGui::PushID("atmosphere");
    ImGui::SeparatorText("Ground fog (this room)");
    if (!fog.defined) {
        ImGui::TextDisabled("No ground fog here.");
        if (ImGui::Button("Add ground fog")) {
            fog = Fog();
            fog.defined = changed = true;
            pc_undo_hint("Add ground fog");
        }
    } else {
        changed |= ImGui::Checkbox("Ground fog", &fog.enabled);
        tooltip("A layer of mist on the room's floor: thickest at its base, thinning upward, drifting with\n"
                "the wind; lit by the lights and the sun (with their shadows where the haze is traced).");
        ImGui::BeginDisabled(!fog.enabled);
        changed |= ImGui::SliderFloat("Density##fog", &fog.density, 0, 0.02f, "%.5f", ImGuiSliderFlags_Logarithmic);
        tooltip("Extinction per world unit at its base.");
        changed |= ImGui::SliderFloat("Height", &fog.height, 4, 400, "%.0f", ImGuiSliderFlags_Logarithmic);
        tooltip("Scale height: the density falls by e (to 37 %) every this many units up.");
        changed |= ImGui::SliderFloat("Base above the floor", &fog.offset, -200, 400, "%.0f");
        tooltip("Where it starts thinning, above the room's main floor (found from its level faces).");
        changed |= ImGui::SliderFloat("Wisps", &fog.noise, 0, 1, "%.2f");
        tooltip("How much the drifting noise thins and thickens it.");
        changed |= ImGui::SliderFloat("Wisp size", &fog.scale, 16, 1024, "%.0f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::DragFloat3("Wind", fog.wind, 0.5f, -200, 200, "%.1f");
        tooltip("Drift in world units per second (x, y, z; y is down).");
        changed |= color_row("Colour##fog", fog.color);
        tooltip("Albedo: the share of each colour it scatters.");
        changed |= color_row("Fill light##fog", fog.fill);
        tooltip("Light it scatters from everywhere (radiance): keeps it visible where no light reaches.");
        changed |= ImGui::SliderFloat("Sky light", &fog.sky, 0, 4, "%.2f");
        tooltip("How much of the sky's light (env sky) the haze and the fog scatter.");
        ImGui::EndDisabled();
        if (ImGui::SmallButton("Remove ground fog")) {
            fog = Fog();
            changed = true;
            pc_undo_hint("Remove ground fog");
        }
    }
    ImGui::SeparatorText("Fog volumes");
    if (ImGui::Button("Add volume")) {
        if (add_volume()) {
            changed = true;
            pc_undo_hint("Add fog volume");
        }
    }
    tooltip("A box of mist on the surface under the view's centre; move, size and turn it with the gizmo.");
    ImGui::SameLine();
    ImGui::Checkbox("Show in the view", &show_volumes);
    for (int i = 0; i < (int)volumes.size(); i++) {
        Volume& v = volumes[i];
        char label[128];
        snprintf(label, sizeof label, "%s %d%s%s##%s", v.sphere ? "Sphere" : "Box", i + 1, v.name.empty() ? "" : ": ",
            v.name.c_str(), v.id.c_str());
        if (ImGui::Selectable(label, i == selected)) {
            selected = i == selected ? -1 : i;
            if (selected >= 0)
                rood_lights::select_none();
        }
    }
    if (selected >= 0 && selected < (int)volumes.size()) {
        Volume& v = volumes[selected];
        ImGui::PushID(v.id.c_str());
        ImGui::RadioButton("Move", &operation, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Size", &operation, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Turn", &operation, 2);
        bool end = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_End, false);
        if (ImGui::Button("Drop to floor (End)") || end) {
            pc_undo_hint("Drop fog volume to floor");
            changed |= drop_to_floor(v);
        }
        tooltip("Moves it down (or up) so its bottom face rests on the floor under it: the highest floor under\n"
                "its centre and its footprint's corners. While moving it with the gizmo, the editor's snapping\n"
                "(point, edge, midpoint, surface) puts its bottom face on the target.");
        ImGui::SameLine();
        if (ImGui::Button("Fit to floor area")) {
            pc_undo_hint("Fit fog volume to floor area");
            changed |= fit_to_floor(v);
        }
        tooltip("Its footprint becomes the level floor around its centre (the room's floor grid: the cells\n"
                "connected to the one under it, within 12 units of its height), square to the room, on the floor.");
        changed |= ImGui::Checkbox("Enabled", &v.enabled);
        ImGui::SameLine();
        int shape = v.sphere;
        if (ImGui::Combo("Shape", &shape, "Box\0Sphere\0")) {
            v.sphere = shape != 0;
            changed = true;
        }
        changed |= ImGui::DragFloat3("Position", v.pos, 1.0f, -32768, 32768, "%.1f");
        changed |= ImGui::DragFloat3("Size", v.size, 1.0f, 2, 8192, "%.1f");
        changed |= ImGui::SliderFloat("Yaw", &v.yaw, -180, 180, "%.1f deg");
        changed |= ImGui::SliderFloat("Density", &v.density, 0, 0.05f, "%.5f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Soft edge", &v.edge, 0.01f, 1, "%.2f");
        tooltip("The part of its half size over which it fades out.");
        changed |= ImGui::SliderFloat("Wisps", &v.noise, 0, 1, "%.2f");
        changed |= color_row("Colour", v.color);
        changed |= color_row("Fill light", v.fill);
        if (pc_fog_volume_render) {
            ImGui::SeparatorText("Look (its own renderer)");
            changed |= ImGui::SliderFloat("Opacity", &v.opacity, 0, 1, "%.2f");
            tooltip("The most of the scene it may hide along a ray (1: solid). Volumes are exempt from the\n"
                    "room's haze limits (haze_max_opacity, haze_max_light); this and Brightness replace them.");
            changed |= ImGui::SliderFloat("Brightness", &v.brightness, 0, 4, "%.2f");
            tooltip("Scale on the light it scatters and emits.");
            changed |= ImGui::SliderFloat("Forward scattering", &v.phase, -0.9f, 0.9f, "%.2f");
            tooltip("How much brighter it is looking toward a light through it (0: the same from every side).");
            changed |= ImGui::SliderFloat("Ambient", &v.ambient, 0, 4, "%.2f");
            tooltip("Scale on its ambient light: the room's baked light around it and the sky, so it reads as\n"
                    "dim grey in a dark room, not black (all volumes: settings fog_volume_ambient).");
            changed |= color_row("Glow", v.glow);
            tooltip("Light it emits per unit of density: smoke lit from within (a vat, embers, a fire's base).");
            changed |= ImGui::SliderFloat("Glow at its base", &v.glow_core, 0, 1, "%.2f");
            tooltip("0: the glow even through it; 1: gathered at its base, fading upward.");
            changed |= ImGui::SliderFloat("Taper", &v.taper, 0, 1, "%.2f");
            tooltip("Height profile: 0 a box's even density, 1 thinning toward its top (rolling ground fog).");
            changed |= ImGui::SliderFloat("Billow size", &v.detail_size, 0, 1024, v.detail_size > 0 ? "%.0f" : "auto");
            tooltip("The size of its billows (the detail noise's period, world units; 0: from the room's wisp size).");
        }
        changed |= preset_ui(v);
        changed |= interaction_ui(v);
        char name[96];
        snprintf(name, sizeof name, "%s", v.name.c_str());
        if (ImGui::InputText("Name", name, sizeof name)) {
            v.name = name;
            changed = true;
        }
        if (ImGui::SmallButton("Delete volume")) {
            volumes.erase(volumes.begin() + selected);
            selected = -1;
            changed = true;
            pc_undo_hint("Delete fog volume");
        }
        ImGui::PopID();
    }
    global_interaction_ui();
    bool alone = pc_haze_debug != 0;
    if (ImGui::Checkbox("Show the haze alone", &alone))
        pc_haze_debug = alone;
    tooltip("The light the haze, fog and volumes scatter toward the eye, over black (RPC graphics {haze_debug}).");
    bool global = pc_fog_global != 0;
    if (ImGui::Checkbox("Ground fog and volumes on (all rooms)", &global))
        pc_fog_global = global;
    tooltip("Switches every room's ground fog and fog volumes off for comparison (settings key fog).");
    bool own = pc_fog_volume_render != 0;
    if (ImGui::Checkbox("Volumes' own renderer", &own))
        pc_fog_volume_render = own;
    tooltip("Volumes ray marched per pixel with their own lights, shadows and ambient light (settings\n"
            "fog_volume_render); off: inside the room's haze as before (coarser, under its limits).");
    if (own && ImGui::TreeNode("Volume renderer")) {
        ImGui::SliderFloat("Resolution", &pc_fog_volume_resolution, 0.25f, 1, "%.2f");
        tooltip("Its march pixels per internal pixel (Vulkan; 0.5: half; settings fog_volume_resolution).");
        ImGui::SliderInt("Steps", &pc_fog_volume_steps, 8, 128);
        tooltip("Samples per ray through the volumes (Vulkan; OpenGL: fog_volume_steps_gl).");
        ImGui::SliderFloat("History", &pc_fog_volume_history, 0.02f, 1, "%.2f");
        tooltip("The least weight of a new frame in its temporal accumulation (lower: steadier, slower).");
        ImGui::SliderFloat("Ambient (all)", &pc_fog_volume_ambient, 0, 4, "%.2f");
        ImGui::SliderFloat("Glows' light", &pc_fog_volume_glow, 0, 4, "%.2f");
        tooltip("How much the glows (fires, torches, the save point) light the volumes.");
        ImGui::SliderFloat("Multiple scattering", &pc_fog_volume_scattering, 0, 1, "%.2f");
        ImGui::SliderFloat("Halo near lights", &pc_fog_volume_halo, 0, 8, "%.2f");
        tooltip("Extra light in the fog close to a light (within about 100 units): a fire glows in its smoke.");
        ImGui::Combo("Debug view", &pc_fog_volume_debug, "Off\0Their light alone\0Their transmittance\0");
        ImGui::TreePop();
    }
    ImGui::PopID();
    return changed;
}

/* The volumes in the editor view (while the Atmosphere tab shows, or the
 * selected one), and the selected one's gizmo when no light or actor is
 * selected. Returns true while the gizmo has the mouse. */
bool pc_atmosphere_viewport(const float view[16], const float projection[16], const float rect[4], void* draw_list)
{
    bool page = ImGui::GetFrameCount() - page_frame <= 1;
    if (selected >= (int)volumes.size())
        selected = -1;
    if (!page && selected < 0) {
        if (ghost_mode)
            rood_fog_sim::ghost(false, nullptr);
        return false;
    }
    ImDrawList* draw = (ImDrawList*)draw_list;
    auto project = [&](const float w[3], ImVec2& out) {
        float v[4], c[4];
        for (int r = 0; r < 4; r++)
            v[r] = view[r] * w[0] + view[4 + r] * w[1] + view[8 + r] * w[2] + view[12 + r];
        for (int r = 0; r < 4; r++)
            c[r] = projection[r] * v[0] + projection[4 + r] * v[1] + projection[8 + r] * v[2] + projection[12 + r] * v[3];
        if (c[3] <= 1e-3f)
            return false;
        out = ImVec2(rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * rect[2], rect[1] + (0.5f - c[1] / c[3] * 0.5f) * rect[3]);
        return true;
    };
    /* A point of a volume's frame (unit coordinates -1..1) in the world. */
    auto corner = [](const Volume& v, float x, float y, float z, float out[3]) {
        float c = cosf(v.yaw * DEG), s = sinf(v.yaw * DEG);
        float lx = x * v.size[0] * 0.5f, ly = y * v.size[1] * 0.5f, lz = z * v.size[2] * 0.5f;
        /* the inverse of medium.glsl's turn into the frame */
        out[0] = v.pos[0] + c * lx + s * lz;
        out[1] = v.pos[1] + ly;
        out[2] = v.pos[2] - s * lx + c * lz;
    };
    if (show_volumes)
        for (int i = 0; i < (int)volumes.size(); i++) {
            const Volume& v = volumes[i];
            if (!page && i != selected)
                continue;
            ImU32 col = i == selected ? IM_COL32(255, 230, 120, 230) : IM_COL32(170, 200, 255, v.enabled ? 150 : 60);
            float thick = i == selected ? 2.0f : 1.0f;
            if (v.sphere) {
                for (int axis = 0; axis < 3; axis++) {
                    ImVec2 pts[33];
                    int n = 0;
                    for (int k = 0; k <= 32; k++) {
                        float a = k * 6.2831853f / 32, p[3], u = cosf(a), w = sinf(a);
                        if (axis == 0)
                            corner(v, u, w, 0, p);
                        else if (axis == 1)
                            corner(v, u, 0, w, p);
                        else
                            corner(v, 0, u, w, p);
                        if (project(p, pts[n]))
                            n++;
                    }
                    if (n > 1)
                        draw->AddPolyline(pts, n, col, 0, thick);
                }
            } else {
                static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 },
                    { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                float p[8][3];
                ImVec2 q[8];
                bool ok[8];
                for (int c = 0; c < 8; c++) {
                    corner(v, c & 1 ? 1 : -1, c & 2 ? 1 : -1, c & 4 ? 1 : -1, p[c]);
                    ok[c] = project(p[c], q[c]);
                }
                for (const auto& e : edges)
                    if (ok[e[0]] && ok[e[1]])
                        draw->AddLine(q[e[0]], q[e[1]], col, thick);
            }
            ImVec2 c;
            if (project(v.pos, c)) {
                char label[32];
                snprintf(label, sizeof label, "fog %d", i + 1);
                draw->AddText(ImVec2(c.x + 6, c.y - 6), col, label);
            }
        }
    /* The selected volume's field on its floor: red cleared, blue piled. */
    if (show_field && selected >= 0 && volumes[selected].sim)
        if (const float* field = rood_fog_sim::field(volumes[selected].id.c_str())) {
            const Volume& v = volumes[selected];
            const int n = PC_FOG_SIM_GRID;
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    float d = field[j * n + i];
                    if (fabsf(d - 1.0f) < 0.02f)
                        continue;
                    float x0 = (float)i / n * 2 - 1, x1 = (float)(i + 1) / n * 2 - 1;
                    float z0 = (float)j / n * 2 - 1, z1 = (float)(j + 1) / n * 2 - 1;
                    float p[4][3];
                    corner(v, x0, 1, z0, p[0]), corner(v, x1, 1, z0, p[1]), corner(v, x1, 1, z1, p[2]),
                        corner(v, x0, 1, z1, p[3]);
                    ImVec2 q[4];
                    if (!project(p[0], q[0]) || !project(p[1], q[1]) || !project(p[2], q[2]) || !project(p[3], q[3]))
                        continue;
                    float a = fminf(fabsf(d - 1.0f) * 1.6f, 1.0f);
                    ImU32 col = d < 1 ? IM_COL32(255, 70, 50, (int)(a * 150)) : IM_COL32(60, 120, 255, (int)(a * 150));
                    draw->AddQuadFilled(q[0], q[1], q[2], q[3], col);
                }
        }
    /* The ghost: the surface under the mouse is a source (while the
     * Atmosphere tab shows and no gizmo has the mouse). */
    {
        static bool ghost_was;
        bool on = false;
        ImVec2 mouse = ImGui::GetIO().MousePos;
        if (ghost_mode && page && mouse.x >= rect[0] && mouse.y >= rect[1] && mouse.x < rect[0] + rect[2]
            && mouse.y < rect[1] + rect[3] && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
            rood_lights::Placement where;
            if (rood_lights::place_compute("light:point", mouse.x, mouse.y, PC_SNAP_MODE_SURFACE, where)) {
                rood_fog_sim::ghost(true, where.surface);
                ImVec2 c;
                if (project(where.surface, c))
                    draw->AddCircle(c, 9, IM_COL32(255, 255, 255, 200), 16, 1.5f);
                on = true;
            }
        }
        if (!on && ghost_was)
            rood_fog_sim::ghost(false, nullptr);
        ghost_was = on;
    }
    if (selected < 0 || rood_lights::selected >= 0 || rood_lights::selected_actor >= 0)
        return false;
    Volume& v = volumes[selected];
    float c = cosf(v.yaw * DEG), s = sinf(v.yaw * DEG);
    float model[16] = { c * v.size[0], 0, -s * v.size[0], 0, 0, v.size[1], 0, 0, s * v.size[2], 0, c * v.size[2], 0, v.pos[0],
        v.pos[1], v.pos[2], 1 };
    ImGuizmo::OPERATION op = operation == 1 ? ImGuizmo::SCALE : operation == 2 ? ImGuizmo::ROTATE_Y : ImGuizmo::TRANSLATE;
    if (ImGuizmo::Manipulate(view, projection, op, operation == 0 ? ImGuizmo::WORLD : ImGuizmo::LOCAL, model)) {
        float x[3] = { model[0], model[1], model[2] }, z[3] = { model[8], model[9], model[10] };
        float sx = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]), sz = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
        float sy = sqrtf(model[4] * model[4] + model[5] * model[5] + model[6] * model[6]);
        v.pos[0] = model[12], v.pos[1] = model[13], v.pos[2] = model[14];
        if (operation == 1)
            v.size[0] = fmaxf(sx, 2), v.size[1] = fmaxf(sy, 2), v.size[2] = fmaxf(sz, 2);
        if (operation == 2 && sx > 1e-6f)
            v.yaw = atan2f(-x[2], x[0]) / DEG;
        pc_lights_mark_dirty();
        pc_undo_hint(operation == 0 ? "Move fog volume" : operation == 1 ? "Size fog volume" : "Turn fog volume");
    }
    /* Moving with the editor's snapping on (the lights' point, edge,
     * midpoint and surface modes): the bottom face's centre goes to the
     * target under the cursor (a surface: the bottom face on it). */
    unsigned modes = pc_snap_modes(0);
    if (operation == 0 && ImGuizmo::IsUsing() && modes) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        pc_snap_result target;
        int type = pc_lights_snap_at(mouse.x, mouse.y, modes, 0, &target);
        if (type) {
            const float* at = type == PC_SNAP_SURFACE ? target.surface : target.position;
            v.pos[0] = at[0], v.pos[1] = at[1] - v.size[1] * 0.5f, v.pos[2] = at[2];
            pc_lights_mark_dirty();
        }
    }
    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

/* ---- RPC environment {fog, volumes, fog_time} (environment.cpp calls these) ---- */

namespace {
std::string jbool(bool b) { return b ? "true" : "false"; }
std::string jvec(const float* v, int n)
{
    std::string s = "[";
    for (int i = 0; i < n; i++)
        s += (i ? "," : "") + lighting::format_number(v[i], 5);
    return s + "]";
}
void read_vec(const prefab::Json* j, float* out, int n)
{
    if (!j || j->type != prefab::Json::Array || (int)j->items.size() != n)
        return;
    for (int i = 0; i < n; i++)
        out[i] = (float)j->items[i].number;
}
void read_num(const prefab::Json& o, const char* key, float* out)
{
    if (const prefab::Json* j = o.get(key))
        *out = j->type == prefab::Json::Bool ? (j->boolean ? 1.0f : 0.0f) : (float)j->number;
}
void read_bool(const prefab::Json& o, const char* key, bool* out)
{
    if (const prefab::Json* j = o.get(key))
        *out = j->type == prefab::Json::Bool ? j->boolean : j->number != 0;
}
void read_volume(const prefab::Json& o, Volume& v)
{
    if (const prefab::Json* j = o.get("preset")) { /* first: the fields given with it win */
        if (j->string.empty())
            v.preset.clear();
        else if (const Preset* p = find_preset(j->string))
            apply_preset(v, *p);
        else
            v.preset = j->string;
    }
    read_num(o, "sim_footprint", &v.sim_footprint);
    read_num(o, "sim_trail", &v.sim_trail);
    read_num(o, "sim_wind", &v.sim_wind);
    read_num(o, "detail", &v.detail);
    read_num(o, "sim_swirl", &v.sim_swirl);
    read_num(o, "shadow", &v.shadow);
    for (int s = 0; s < 4; s++)
        if (const prefab::Json* j = o.get(DISTURB_KEYS[s]))
            v.disturb[s] = j->type == prefab::Json::Null || j->number < 0 ? -1 : (float)j->number;
    read_bool(o, "enabled", &v.enabled);
    if (const prefab::Json* j = o.get("shape"))
        v.sphere = j->string == "sphere";
    if (const prefab::Json* j = o.get("name"))
        v.name = j->string;
    read_vec(o.get("pos"), v.pos, 3);
    read_vec(o.get("size"), v.size, 3);
    read_num(o, "yaw", &v.yaw);
    read_num(o, "density", &v.density);
    read_num(o, "edge", &v.edge);
    read_vec(o.get("color"), v.color, 3);
    read_vec(o.get("fill"), v.fill, 3);
    read_num(o, "noise", &v.noise);
    read_bool(o, "sim", &v.sim);
    read_num(o, "sim_strength", &v.sim_strength);
    read_num(o, "sim_recover", &v.sim_recover);
    read_num(o, "opacity", &v.opacity);
    read_num(o, "brightness", &v.brightness);
    read_num(o, "phase", &v.phase);
    read_num(o, "ambient", &v.ambient);
    read_vec(o.get("glow"), v.glow, 3);
    read_num(o, "glow_core", &v.glow_core);
    read_num(o, "taper", &v.taper);
    read_num(o, "detail_size", &v.detail_size);
}
} // namespace

namespace rood_atmo {
/* params: fog {enabled, density, height, offset, noise, scale, wind, color,
 * fill, sky} (adds it when the room has none), remove_fog, volumes: a list
 * of {id (existing: changed; else added), remove, enabled, shape, name,
 * pos, size, yaw, density, edge, color, fill, noise}, fog_time (seconds;
 * -1 live), fog_global. Returns whether the room changed. */
bool rpc(const prefab::Json& params)
{
    bool changed = false;
    sim_field_reply = false;
    if (const prefab::Json* t = params.get("fog_time"))
        pc_fog_time = (float)t->number;
    if (const prefab::Json* g = params.get("fog_global"))
        pc_fog_global = g->type == prefab::Json::Bool ? g->boolean : g->number != 0;
    if (const prefab::Json* f = params.get("sim_field")) /* this reply carries the field's values */
        sim_field_reply = f->type == prefab::Json::Bool ? f->boolean : f->number != 0;
    if (const prefab::Json* r = params.get("sim_reset"))
        if (r->type == prefab::Json::Bool ? r->boolean : r->number != 0)
            rood_fog_sim::reset();
    /* The interactive fog's global weights (settings fog_disturb_*), its budget. */
    if (const prefab::Json* d = params.get("disturb")) {
        static const char* keys[4] = { "player", "actors", "attacks", "spells" };
        for (int s = 0; s < 4; s++)
            read_num(*d, keys[s], &pc_fog_disturb[s]);
    }
    if (const prefab::Json* b = params.get("sim_budget"))
        pc_fog_sim_budget = (int)b->number;
    /* The fog volumes' renderer (settings fog_volume_*; debug: 1 their light alone, 2 their transmittance). */
    if (const prefab::Json* fv = params.get("fog_volumes")) {
        float x;
        struct {
            const char* key;
            int* value;
        } ints[] = { { "render", &pc_fog_volume_render }, { "steps", &pc_fog_volume_steps },
            { "steps_gl", &pc_fog_volume_steps_gl }, { "debug", &pc_fog_volume_debug }, { "shadows", &pc_fog_volume_shadows } };
        for (auto& k : ints) {
            x = (float)*k.value;
            read_num(*fv, k.key, &x);
            *k.value = (int)x;
        }
        read_num(*fv, "resolution", &pc_fog_volume_resolution);
        read_num(*fv, "history", &pc_fog_volume_history);
        read_num(*fv, "ambient", &pc_fog_volume_ambient);
        read_num(*fv, "glow", &pc_fog_volume_glow);
        read_num(*fv, "scattering", &pc_fog_volume_scattering);
        read_num(*fv, "halo", &pc_fog_volume_halo);
    }
    if (params.get("presets_reload"))
        load_presets();
    /* A stamp (as the game's attacks and spells make; source "attack",
     * "spell" or "ghost": weight 1 everywhere). */
    if (const prefab::Json* st = params.get("stamp")) {
        rood_fog_sim::Event e;
        if (const prefab::Json* j = st->get("shape")) {
            int shape = rood_fog_sim::shape_from_name(j->string.c_str());
            e.shape = shape < 0 ? rood_fog_sim::BURST : shape;
        }
        if (const prefab::Json* j = st->get("source"))
            e.source = j->string == "spell" ? rood_fog_sim::SPELLS
                : j->string == "ghost"      ? rood_fog_sim::GHOST
                                            : rood_fog_sim::ATTACKS;
        read_vec(st->get("pos"), e.pos, 3);
        read_vec(st->get("dir"), e.dir, 2);
        read_num(*st, "radius", &e.radius);
        read_num(*st, "strength", &e.strength);
        float frames = (float)e.frames;
        read_num(*st, "frames", &frames);
        e.frames = frames < 1 ? 1 : (int)frames;
        rood_fog_sim::emit(e);
    }
    /* The preview actor: [x, y, z] (on there), or false/null (off). */
    if (const prefab::Json* g = params.get("ghost")) {
        float p[3] = { 0, 0, 0 };
        if (g->type == prefab::Json::Array && g->items.size() == 3) {
            read_vec(g, p, 3);
            rood_fog_sim::ghost(true, p);
        } else
            rood_fog_sim::ghost(false, p);
    }
    if (const prefab::Json* g = params.get("floor_grid"))
        pc_fog_floor_grid = g->type == prefab::Json::Bool ? g->boolean : g->number != 0;
    if (const prefab::Json* hz = params.get("haze")) { /* the room settings haze_density, haze_anisotropy, haze_glow, ... */
        for (const char* key : { "density", "anisotropy", "glow", "max_opacity", "max_light", "shafts", "max_forward" })
            if (const prefab::Json* j = hz->get(key)) {
                char line[96];
                snprintf(line, sizeof line, "set haze_%s %.6f", key, j->number);
                pc_room_settings_parse(line);
                changed = true;
            }
    }
    if (const prefab::Json* r = params.get("remove_fog"))
        if (r->type == prefab::Json::Bool ? r->boolean : r->number != 0)
            fog = Fog(), changed = true;
    if (const prefab::Json* f = params.get("fog")) {
        if (!fog.defined)
            fog = Fog(), fog.defined = true;
        read_bool(*f, "enabled", &fog.enabled);
        read_num(*f, "density", &fog.density);
        read_num(*f, "height", &fog.height);
        read_num(*f, "offset", &fog.offset);
        read_num(*f, "noise", &fog.noise);
        read_num(*f, "scale", &fog.scale);
        read_vec(f->get("wind"), fog.wind, 3);
        read_vec(f->get("color"), fog.color, 3);
        read_vec(f->get("fill"), fog.fill, 3);
        read_num(*f, "sky", &fog.sky);
        changed = true;
    }
    if (const prefab::Json* list = params.get("volumes"))
        for (const prefab::Json& o : list->items) {
            const prefab::Json* id = o.get("id");
            int at = -1;
            for (int i = 0; id && i < (int)volumes.size(); i++)
                if (volumes[i].id == id->string)
                    at = i;
            bool remove = false;
            read_bool(o, "remove", &remove);
            if (remove) {
                if (at >= 0)
                    volumes.erase(volumes.begin() + at), changed = true;
                continue;
            }
            if (at < 0) {
                volumes.push_back(Volume());
                at = (int)volumes.size() - 1;
                volumes[at].id = id && !id->string.empty() ? id->string : lighting::new_name();
            }
            read_volume(o, volumes[at]);
            changed = true;
        }
    /* The editor's Drop to floor (End) and Fit to floor area, by id. */
    for (const char* key : { "drop_volume", "fit_volume" })
        if (const prefab::Json* d = params.get(key))
            for (Volume& v : volumes)
                if (v.id == d->string && (key[0] == 'd' ? drop_to_floor(v) : fit_to_floor(v)))
                    changed = true;
    if (const prefab::Json* sel = params.get("select_volume")) { /* the editor's selection (its gizmo) */
        selected = -1;
        for (int i = 0; i < (int)volumes.size(); i++)
            if (volumes[i].id == sel->string)
                selected = i;
        if (selected >= 0)
            rood_lights::select_none();
    }
    return changed;
}
std::string json()
{
    float floor_y = 0, lo[3], hi[3];
    int has_floor = pc_scene_floor(&floor_y);
    pc_scene_bounds(lo, hi);
    static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    float corner[2], cell[2];
    std::string grid = "null";
    if (pc_scene_floor_grid(heights, corner, cell))
        grid = "{\"n\":" + std::to_string(PC_SCENE_FLOOR_GRID) + ",\"corner\":" + jvec(corner, 2) + ",\"cell\":"
            + jvec(cell, 2) + ",\"heights\":" + jvec(heights, PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID) + "}";
    std::string s = "\"haze\":{\"density\":" + num(pc_fog_density, 6) + ",\"anisotropy\":" + num(pc_fog_anisotropy, 3)
        + ",\"max_opacity\":" + num(pc_haze_max_opacity, 3) + ",\"max_light\":" + num(pc_haze_max_light, 3)
        + ",\"shafts\":" + num(pc_haze_shafts, 3) + ",\"max_forward\":" + num(pc_haze_max_forward, 3) + "},\"fog\":{\"defined\":" + jbool(fog.defined) + ",\"enabled\":" + jbool(fog.enabled)
        + ",\"density\":" + num(fog.density, 5) + ",\"height\":" + num(fog.height, 1) + ",\"offset\":"
        + num(fog.offset, 1) + ",\"noise\":" + num(fog.noise, 2) + ",\"scale\":" + num(fog.scale, 1)
        + ",\"wind\":" + jvec(fog.wind, 3) + ",\"color\":" + jvec(fog.color, 3) + ",\"fill\":" + jvec(fog.fill, 3)
        + ",\"sky\":" + num(fog.sky, 3) + ",\"floor\":" + (has_floor ? num(floor_y, 1) : std::string("null"))
        + ",\"floor_grid\":" + grid + "},\"bounds\":[" + jvec(lo, 3) + "," + jvec(hi, 3) + "],\"fog_global\":" + jbool(pc_fog_global)
        + ",\"fog_time\":" + num(pc_fog_time, 3) + ",\"volumes\":[";
    for (size_t i = 0; i < volumes.size(); i++) {
        const Volume& v = volumes[i];
        s += (i ? "," : "") + std::string("{\"id\":") + prefab::json_quote(v.id) + ",\"name\":"
            + prefab::json_quote(v.name) + ",\"enabled\":" + jbool(v.enabled) + ",\"shape\":\""
            + (v.sphere ? "sphere" : "box") + "\",\"pos\":" + jvec(v.pos, 3) + ",\"size\":" + jvec(v.size, 3)
            + ",\"yaw\":" + num(v.yaw, 2) + ",\"density\":" + num(v.density, 5) + ",\"edge\":" + num(v.edge, 2)
            + ",\"color\":" + jvec(v.color, 3) + ",\"fill\":" + jvec(v.fill, 3) + ",\"noise\":" + num(v.noise, 2)
            + ",\"sim\":" + jbool(v.sim) + ",\"sim_strength\":" + num(v.sim_strength, 2)
            + ",\"sim_recover\":" + num(v.sim_recover, 2) + ",\"sim_footprint\":" + num(v.sim_footprint, 2)
            + ",\"sim_trail\":" + num(v.sim_trail, 2) + ",\"sim_wind\":" + num(v.sim_wind, 2)
            + ",\"detail\":" + num(v.detail, 2) + ",\"sim_swirl\":" + num(v.sim_swirl, 2) + ",\"shadow\":"
            + num(v.shadow, 2) + ",\"preset\":" + prefab::json_quote(v.preset)
            + ",\"layer\":" + std::to_string(v.layer) + ",\"opacity\":" + num(v.opacity, 2) + ",\"brightness\":"
            + num(v.brightness, 2) + ",\"phase\":" + num(v.phase, 2) + ",\"ambient\":" + num(v.ambient, 2)
            + ",\"glow\":" + jvec(v.glow, 3) + ",\"glow_core\":" + num(v.glow_core, 2) + ",\"taper\":"
            + num(v.taper, 2) + ",\"detail_size\":" + num(v.detail_size, 1);
        for (int k = 0; k < 4; k++)
            s += ",\"" + std::string(DISTURB_KEYS[k]) + "\":" + num(v.disturb[k], 2);
        s += "}";
    }
    /* Every simulated volume's field (the first also as `sim`, as before). */
    std::string sims, first;
    for (const Volume& v : volumes) {
        rood_fog_sim::Stats st;
        if (!v.sim || !v.enabled || !rood_fog_sim::stats(v.id.c_str(), &st))
            continue;
        std::string one = "{\"id\":" + prefab::json_quote(v.id) + ",\"n\":" + std::to_string(PC_FOG_SIM_GRID)
            + ",\"layer\":" + std::to_string(v.layer) + ",\"least\":" + num(st.least, 3) + ",\"mean\":"
            + num(st.mean, 3) + ",\"at_player\":" + num(st.at_player, 3) + ",\"actors\":" + std::to_string(st.actors)
            + ",\"awake\":" + jbool(st.awake) + ",\"steps\":" + std::to_string(st.steps) + ",\"ms\":" + num(st.ms, 4)
            + (sim_field_reply ? ",\"field\":" + jvec(rood_fog_sim::field(v.id.c_str()), PC_FOG_SIM_GRID * PC_FOG_SIM_GRID)
                               : "")
            + "}";
        if (first.empty())
            first = one;
        sims += (sims.empty() ? "" : ",") + one;
    }
    unsigned attacks, spells;
    rood_fog_sim::event_counts(&attacks, &spells);
    int bodies, phase, type;
    rood_fog_sim::sources(&bodies, &phase, &type);
    if (!presets_loaded)
        load_presets();
    std::string names;
    for (const Preset& p : presets)
        names += (names.empty() ? "" : ",") + prefab::json_quote(p.id);
    /* The fog volumes' renderer: its settings, what it drew in the last frame (lights, ambient per volume). */
    std::string drawn;
    for (int i = 0; pc_fog_volume_render && i < fogvol_drawn; i++) {
        std::string slots;
        for (int s = 0; s < PC_FOGVOL_LIGHTS; s++)
            slots += (s ? "," : "") + jvec(fogvol_volume_light_rgb[i][s], 4);
        drawn += (i ? "," : "") + std::string("{\"lights\":") + std::to_string(fogvol_volume_lights[i])
            + ",\"slots\":[" + slots + "],\"ambient\":" + jvec(fogvol_volume_ambient[i], 3) + "}";
    }
    s += "],\"fog_volumes\":{\"render\":" + jbool(pc_fog_volume_render) + ",\"resolution\":"
        + num(pc_fog_volume_resolution, 3) + ",\"steps\":" + std::to_string(pc_fog_volume_steps) + ",\"steps_gl\":"
        + std::to_string(pc_fog_volume_steps_gl) + ",\"history\":" + num(pc_fog_volume_history, 3) + ",\"ambient\":"
        + num(pc_fog_volume_ambient, 3) + ",\"glow\":" + num(pc_fog_volume_glow, 3) + ",\"scattering\":"
        + num(pc_fog_volume_scattering, 3) + ",\"halo\":" + num(pc_fog_volume_halo, 3) + ",\"shadows\":" + jbool(pc_fog_volume_shadows) + ",\"debug\":"
        + std::to_string(pc_fog_volume_debug) + ",\"drawn\":[" + drawn + "]";
    return s + "},\"sim\":" + (first.empty() ? "null" : first) + ",\"sims\":[" + sims + "],\"disturb\":{\"player\":"
        + num(pc_fog_disturb[0], 2) + ",\"actors\":" + num(pc_fog_disturb[1], 2) + ",\"attacks\":"
        + num(pc_fog_disturb[2], 2) + ",\"spells\":" + num(pc_fog_disturb[3], 2) + "},\"sim_budget\":"
        + std::to_string(pc_fog_sim_budget) + ",\"events\":{\"attacks\":" + std::to_string(attacks) + ",\"spells\":"
        + std::to_string(spells) + ",\"sources\":" + std::to_string(bodies) + ",\"phase\":" + std::to_string(phase)
        + ",\"type\":" + std::to_string(type) + "},\"presets\":[" + names + "]";
}
} // namespace rood_atmo
