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
#include "editor_picking.h"
#include "editor_scene.h"
#include "editor_schema.h"
#include "editor_selection.h"
#include "editor_undo.h"
#include "dev_state.h"
#include "environment.h"
#include "environment_internal.h"
#include "editor_library.h"
#include "floor_grid.h"
#include "fog_sim.h"
#include "lighting_layers.h"
#include "lighting_quality.h"
#include "light_list.h"
#include "editor_math.h"
#include "library_thumbs.h"
#include "lights_internal.h"
#include "platform.h"
#include "scene_capture.h"

extern "C" {
extern int pc_lights_enabled;
unsigned pc_vsync_count(void) __attribute__((weak)); /* game time */
extern float pc_fog_anisotropy, pc_fog_density;
int pc_room_settings_parse(const char* line);
void pc_lights_mark_dirty(void);
int pc_lights_snap_at(float x, float y, unsigned modes, int apply, pc_snap_result* out); /* lights.cpp */
extern int pc_lights_gizmo_space; /* lights.cpp: the move tool's axes (0 world, 1 local) */
}

int pc_fog_global = 1; /* settings key fog: 0 switches every room's ground fog and volumes off */
/* The atmosphere's limits (atmosphere.h; room settings, so an area's mood
 * may change them). */
extern "C" {
float pc_haze_max_opacity = 0.55f, pc_haze_max_light = 0.15f, pc_haze_shafts = 0.5f;
/* Forward scattering's peak (Henyey-Greenstein x 4 pi: g 0.55 would reach 7.7
 * looking toward the moon; 0: no limit). */
float pc_haze_max_forward = 3.0f;
/* Where the room's air is (atmosphere.h). */
float pc_haze_interior = 1, pc_haze_interior_fill = 1, pc_haze_under_floor = 32, pc_haze_void = 1,
      pc_haze_void_level = 0, pc_haze_outside = 1, pc_haze_interior_edge = 0.2f, pc_haze_interior_slope = 60;
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
static_assert(PC_MEDIUM_VEC4S == 166, "the shaders' medium_[] arrays");
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
    /* Confine to room: it keeps to the room's interior as the ground fog and haze do (the floor grid's
     * cells with a floor, haze_under_floor under them, the void-pixel test); off for a volume meant to
     * sit outside it (over a pit, beyond a window). Only with the room's haze_interior on. */
    bool confine = true;
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
    bool user = false; /* the user's (<user prefab dir>/fog/<id>.json) */
    std::string path; /* the user's: its file */
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
unsigned presets_serial; /* bumps on every (re)load (the library's fog source) */
std::string presets_status;
Fog fog;
std::vector<Volume> volumes;
int selected = -1;
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
/* A volume the renderers draw: on, with density, and not hidden in the editor (editor_scene.cpp). */
bool drawn(const Volume& v) { return v.enabled && v.density > 0 && !rood_scene::volume_hidden_id(v.id); }
int volumes_on()
{
    int n = 0;
    for (const Volume& v : volumes)
        n += drawn(v);
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
/* A preset's object (the built-in file's member, or a user file): name, why, its fields. */
Preset preset_from_json(const std::string& id, const prefab::Json& j)
{
    Preset p;
    p.id = id;
    for (const auto& f : j.members) {
        if (f.first == "name")
            p.name = f.second.string;
        else if (f.first == "why")
            p.why = f.second.string;
        else if (lighting::preset_field("fog", f.first))
            p.fields.emplace_back(f.first, lighting::canonical_value("fog", f.first, json_text(f.second)));
    }
    if (p.name.empty())
        p.name = p.id;
    return p;
}
/* The user's own presets (Save as prefab of a fog volume): one file each beside their light prefabs. */
std::string user_presets_dir() { return prefab::library().user_dir + "/fog"; }
long long user_presets_stamp = -1;
void load_presets()
{
    presets_loaded = true;
    ++presets_serial;
    presets.clear();
    std::string text, error;
    prefab::Json root;
    if (!read_text(presets_path(), text)) {
        presets_status = "no presets file (" + presets_path() + ")";
    } else if (!prefab::json_parse(text.c_str(), root, &error)) {
        presets_status = presets_path() + ": " + error;
        fprintf(stderr, "[atmosphere] %s\n", presets_status.c_str());
    } else {
        if (const prefab::Json* list = root.get("presets"))
            for (const auto& m : list->members)
                presets.push_back(preset_from_json(m.first, m.second));
        presets_status = std::to_string(presets.size()) + " presets (" + presets_path() + ")";
    }
    std::string dir = user_presets_dir();
    int mine = 0, shadowing = 0;
    for (const std::string& file : prefab::json_files(dir)) {
        std::string t, e, path = dir + "/" + file;
        prefab::Json j;
        if (!read_text(path, t) || !prefab::json_parse(t.c_str(), j, &e) || j.type != prefab::Json::Object) {
            fprintf(stderr, "[atmosphere] %s: %s\n", path.c_str(), e.empty() ? "not a preset object" : e.c_str());
            continue;
        }
        Preset p = preset_from_json(file.substr(0, file.size() - 5), j);
        p.user = true, p.path = path;
        if (std::any_of(presets.begin(), presets.end(), [&](const Preset& x) { return x.id == p.id; })) {
            /* A built-in keeps its id: a file of yours shadowing it would change every shipped room using it here. */
            fprintf(stderr, "[atmosphere] %s: skipped (a built-in fog preset is named %s; rename the file)\n", path.c_str(),
                p.id.c_str());
            shadowing++;
            continue;
        }
        presets.push_back(p);
        mine++;
    }
    user_presets_stamp = prefab::directory_stamp(dir);
    if (mine)
        presets_status += ", " + std::to_string(mine) + " of yours (" + dir + ")";
    if (shadowing)
        presets_status += ", " + std::to_string(shadowing) + " of yours skipped (a built-in has the same id)";
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
/* A volume's look as a preset object: its fields that are not the schema's defaults (sim always says). */
prefab::Json preset_json(const Volume& v, const std::string& name, const std::string& why)
{
    prefab::Json p = prefab::Json::object();
    p.set("name", prefab::Json::str(name));
    if (!why.empty())
        p.set("why", prefab::Json::str(why));
    for (const auto& f : volume_fields(v)) {
        if (f.second == lighting::canonical_value("fog", f.first, lighting::field_default("fog", f.first)) && f.first != "sim")
            continue;
        char* end;
        double x = strtod(f.second.c_str(), &end);
        p.set(f.first.c_str(), *end == 0 && !f.second.empty() ? prefab::Json::num(x) : prefab::Json::str(f.second));
    }
    return p;
}
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
    const Preset* old = find_preset(id);
    prefab::Json p = preset_json(v, name.empty() ? id : name, old ? old->why : "");
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

namespace {
/* A volume as its lighting item: with a preset, only what differs from it
 * (a preset edit updates every volume using it). */
lighting::Item item_of(Volume& v)
{
    if (v.id.empty())
        v.id = lighting::new_name();
    lighting::Item it;
    it.kind = "fog", it.name = v.id;
    if (!v.name.empty())
        it.set("name", v.name);
    it.set("pos", vec3(v.pos, 1));
    it.set("yaw", num(v.yaw, 2));
    it.set("enabled", v.enabled ? "1" : "0");
    it.set("confine", v.confine ? "1" : "0");
    /* An unknown preset keeps every value; so does one of the user's own (pc/user/prefabs/fog: on this
     * machine only), so the room's look never depends on a file that is not shipped with it. */
    const Preset* preset = v.preset.empty() ? nullptr : find_preset(v.preset);
    if (preset && preset->user)
        preset = nullptr;
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
    return it;
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
        v.confine = get1(it, "confine") != 0;
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
        lighting::Item it = item_of(v);
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
        if (!drawn(v) || shown >= PC_MEDIUM_VOLUMES)
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
    bool fog_here = fogs && fog_on();
    /* The floor grid: the ground fog lies on it, and with haze_interior the
     * haze and the fog keep to its cells with a floor (the room's interior,
     * not its bounds: the void past a cut-away wall stays clear). */
    static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    static unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    float corner[2], cell[2], floor_y;
    int flags = 0;
    /* Packed for the ground fog, for the haze with the interior on, and for haze_debug 2 (it shows the
     * interior); a haze without them needs no look-ups. */
    bool interior_on = pc_haze_interior > 0.5f;
    bool confined = false; /* a volume kept to the interior ("Confine to room") needs the cells too */
    if (fogs && interior_on)
        for (const Volume& v : volumes)
            confined |= drawn(v) && v.confine;
    bool grid = pc_fog_floor_grid && (fog_here || (m[0][0] > 0 && interior_on) || confined || pc_haze_debug == 2)
        && pc_scene_floor_grid(heights, corner, cell);
    bool interior = grid && interior_on && pc_scene_floor_cells(cells);
    if (grid) {
        float* g = m[PC_MEDIUM_FLOOR];
        float* q = m[PC_MEDIUM_FLOOR + 1];
        float lo = room_lo[1], span = fmaxf(room_hi[1] - room_lo[1], 1.0f);
        /* off: 12-bit heights, as before; on: 11-bit heights and the no-floor bit (PC_MEDIUM_FLOOR_NONE) */
        int top = interior ? PC_MEDIUM_FLOOR_STEPS_INTERIOR : 4095;
        g[0] = corner[0], g[1] = corner[1], g[2] = 1.0f / cell[0], g[3] = 1.0f / cell[1];
        q[0] = lo, q[1] = span / (float)top, q[2] = PC_MEDIUM_FLOOR_GRID;
        float* data = m[PC_MEDIUM_FLOOR_DATA];
        for (int k = 0; k < PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID; k += 2) {
            int step[2];
            for (int e = 0; e < 2; e++) {
                int s = (int)lroundf((heights[k + e] - lo) / span * (float)top);
                s = s < 0 ? 0 : s > top ? top : s;
                step[e] = interior ? s * 2 + (cells[k + e] == PC_FLOOR_EMPTY ? PC_MEDIUM_FLOOR_NONE : 0) : s;
            }
            data[k / 2] = (float)(step[0] * 4096 + step[1]); /* exact: below 2^24 */
        }
        flags |= PC_MEDIUM_GRID | (interior ? PC_MEDIUM_INTERIOR : 0);
        /* the room's lowest floor (y down: the largest height of the cells with a floor): the haze over the
         * cells without one reaches down to it, not to their grown heights */
        float lowest = room_hi[1];
        bool any = false;
        for (int k = 0; interior && k < PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID; k++)
            if (cells[k] != PC_FLOOR_EMPTY && (!any || heights[k] > lowest))
                lowest = heights[k], any = true;
        m[PC_MEDIUM_AIR][3] = lowest;
    }
    /* The room's air (record PC_MEDIUM_AIR): the haze's share over cells without a floor (above it), the
     * interior's edge (the bilinear floor share's smoothstep around a half). */
    {
        float e = fminf(fmaxf(pc_haze_interior_edge, 0.0f), 0.5f);
        m[PC_MEDIUM_AIR][0] = fminf(fmaxf(pc_haze_outside, 0.0f), 1.0f);
        m[PC_MEDIUM_AIR][1] = 0.5f - e, m[PC_MEDIUM_AIR][2] = 0.5f + e;
    }
    if (fog_here) {
        if (grid)
            m[1][2] = fog.offset; /* the grid's floor minus it (medium.glsl medium_room_air) */
        else {
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
    /* Void pixels (present passes: nothing drawn, or a surface the PS1 draws black) keep clear. */
    if (pc_haze_void > 0.5f)
        flags |= PC_MEDIUM_VOID;
    m[PC_MEDIUM_FLOOR + 1][3] = (float)flags;
    m[6][3] = fmaxf(pc_haze_under_floor, 0.0f);
    m[7][3] = (fminf(fmaxf(pc_haze_void_level, 0.0f), 255.0f) + 0.5f) / 255.0f;
    m[2][3] = 1.0f / fmaxf(fog.defined ? fog.scale : 160.0f, 8.0f);
    if (!fog_on())
        for (int k = 0; k < 3; k++)
            m[4][k] = Fog().wind[k];
    int n = 0;
    if (fogs)
        for (const Volume& v : volumes) {
            if (!drawn(v) || n >= PC_MEDIUM_VOLUMES)
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
            /* its field's layer of the texture + 1 (pc_fog_vsync; 0 none), + 8 for a sphere, + 16 kept to the
             * room's interior (Confine to room; medium.glsl medium_volume_confined) */
            bool field = v.sim && v.layer >= 0 && v.layer < PC_MEDIUM_TEX_LAYERS && layer_ids[v.layer] == v.id;
            b[3][3] = (float)(field ? v.layer + 1 : 0) + (v.sphere ? 8.0f : 0.0f) + (v.confine ? 16.0f : 0.0f);
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
    const Volume* drawn_list[PC_FOGVOL_MAX];
    int n = 0;
    for (const Volume& v : volumes)
        if (drawn(v) && n < count)
            drawn_list[n++] = &v;
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
        const Volume& v = *drawn_list[vi];
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
            float shape[16] = { v.pos[0], v.pos[1], v.pos[2], v.size[0], v.size[1], v.size[2], v.yaw, v.density, v.edge,
                v.shadow, v.detail, (float)v.sphere, v.taper, v.opacity, v.noise, (float)v.confine };
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
    char extra[96];
    snprintf(extra, sizeof extra, "%g %g %g %g %g %g %d", pc_haze_interior, pc_haze_interior_fill, pc_haze_under_floor,
        pc_haze_outside, pc_haze_interior_edge, pc_haze_interior_slope, pc_scene_serial());
    for (const char* c = extra; *c; c++)
        h = (h ^ (unsigned char)*c) * 16777619u;
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
/* The level floor around (x, z): the room's floor grid cells with a floor
 * (pc_scene_floor_cells: not the void past the walls, whose cells carry
 * heights grown from their neighbours) connected to the one under it within
 * 12 units of its height; the world x, z bounds of the largest box of cells
 * around it that lies on that area (at least 90 % of its cells), and that
 * height. Over a cell without a floor, the nearest one with a floor starts
 * it. False without a grid, outside it or with no cell with a floor. */
bool floor_area(float x, float z, float lo[2], float hi[2], float* height)
{
    const int n = PC_SCENE_FLOOR_GRID;
    static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    static unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    float corner[2], cell[2];
    if (!pc_scene_floor_grid(heights, corner, cell))
        return false;
    if (!pc_scene_floor_cells(cells))
        memset(cells, PC_FLOOR_SOLID, sizeof cells);
    int i0 = (int)floorf((x - corner[0]) / cell[0]), j0 = (int)floorf((z - corner[1]) / cell[1]);
    if (i0 < 0 || j0 < 0 || i0 >= n || j0 >= n)
        return false;
    if (cells[j0 * n + i0] == PC_FLOOR_EMPTY) { /* the nearest cell with a floor (cell centres) */
        int best = -1;
        float best_d = 1e30f;
        for (int k = 0; k < n * n; k++) {
            if (cells[k] == PC_FLOOR_EMPTY)
                continue;
            float dx = (k % n - i0) * cell[0], dz = (k / n - j0) * cell[1], d = dx * dx + dz * dz;
            if (d < best_d)
                best_d = d, best = k;
        }
        if (best < 0)
            return false;
        i0 = best % n, j0 = best / n;
    }
    float h0 = heights[j0 * n + i0];
    std::vector<char> seen(n * n, 0);
    std::vector<int> stack = { j0 * n + i0 };
    seen[j0 * n + i0] = 1;
    int a[2] = { i0, j0 }, b[2] = { i0, j0 };
    while (!stack.empty()) {
        int k = stack.back();
        stack.pop_back();
        int i = k % n, j = k / n;
        a[0] = std::min(a[0], i), a[1] = std::min(a[1], j), b[0] = std::max(b[0], i), b[1] = std::max(b[1], j);
        const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
        for (int e = 0; e < 4; e++) {
            int u = i + di[e], w = j + dj[e];
            if (u < 0 || w < 0 || u >= n || w >= n || seen[w * n + u] || cells[w * n + u] == PC_FLOOR_EMPTY
                || fabsf(heights[w * n + u] - h0) > 12)
                continue;
            seen[w * n + u] = 1;
            stack.push_back(w * n + u);
        }
    }
    /* A box over that area: the rectangle of cells around the start cell covering the most of the area's
     * cells with at least FLOOR_AREA_SHARE of its own cells in it (an L-shaped or ragged floor: its
     * bounding box would hang over the void past the walls); the smaller one on a tie. */
    const float FLOOR_AREA_SHARE = 0.9f;
    std::vector<int> sum((n + 1) * (n + 1), 0); /* prefix sums of the area's cells */
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            sum[(j + 1) * (n + 1) + i + 1] = seen[j * n + i] + sum[j * (n + 1) + i + 1] + sum[(j + 1) * (n + 1) + i]
                - sum[j * (n + 1) + i];
    auto count = [&](int x0, int z0, int x1, int z1) {
        return sum[(z1 + 1) * (n + 1) + x1 + 1] - sum[z0 * (n + 1) + x1 + 1] - sum[(z1 + 1) * (n + 1) + x0]
            + sum[z0 * (n + 1) + x0];
    };
    int best[4] = { i0, j0, i0, j0 }, best_cells = 1, best_area = 1;
    for (int x0 = a[0]; x0 <= i0; x0++)
        for (int x1 = i0; x1 <= b[0]; x1++)
            for (int z0 = a[1]; z0 <= j0; z0++)
                for (int z1 = j0; z1 <= b[1]; z1++) {
                    int area = (x1 - x0 + 1) * (z1 - z0 + 1), in = count(x0, z0, x1, z1);
                    if ((float)in < FLOOR_AREA_SHARE * (float)area)
                        continue;
                    if (in > best_cells || (in == best_cells && area < best_area))
                        best[0] = x0, best[1] = z0, best[2] = x1, best[3] = z1, best_cells = in, best_area = area;
                }
    lo[0] = corner[0] + best[0] * cell[0], hi[0] = corner[0] + (best[2] + 1) * cell[0];
    lo[1] = corner[1] + best[1] * cell[1], hi[1] = corner[1] + (best[3] + 1) * cell[1];
    *height = h0;
    return true;
}
/* The x, z extent of the room's floor grid cells with a floor (the room's
 * interior, not its bounds box); false without a grid or a floor. */
bool floor_extent(float lo[2], float hi[2])
{
    const int n = PC_SCENE_FLOOR_GRID;
    static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    static unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    float corner[2], cell[2];
    if (!pc_scene_floor_grid(heights, corner, cell) || !pc_scene_floor_cells(cells))
        return false;
    int a[2] = { n, n }, b[2] = { -1, -1 };
    for (int k = 0; k < n * n; k++)
        if (cells[k] != PC_FLOOR_EMPTY) {
            int i = k % n, j = k / n;
            a[0] = std::min(a[0], i), a[1] = std::min(a[1], j), b[0] = std::max(b[0], i), b[1] = std::max(b[1], j);
        }
    if (b[0] < 0)
        return false;
    lo[0] = corner[0] + a[0] * cell[0], hi[0] = corner[0] + (b[0] + 1) * cell[0];
    lo[1] = corner[1] + a[1] * cell[1], hi[1] = corner[1] + (b[1] + 1) * cell[1];
    return true;
}
/* Fit to floor area: the volume's footprint becomes the level floor around
 * its centre (floor_area), turned square to the room, dropped onto it. */
bool fit_to_floor(Volume& v)
{
    float lo[2], hi[2], h0;
    if (!floor_area(v.pos[0], v.pos[2], lo, hi, &h0))
        return false;
    v.yaw = 0;
    v.pos[0] = (lo[0] + hi[0]) * 0.5f, v.pos[2] = (lo[1] + hi[1]) * 0.5f;
    v.size[0] = hi[0] - lo[0], v.size[2] = hi[1] - lo[1];
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
    rood_selection::set(rood_scene::volume_ref((int)volumes.size() - 1), rood_selection::FROM_CODE);
    rood_scene::publish();
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
            "updates every volume using it). (none) keeps the values and writes them all; so does one of\n"
            "your own presets (Mine), so the room never depends on a file only this machine has.");
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
    tooltip("Writes this volume's look into pc/assets/environment/fog_presets.json (the shipped presets: an\n"
            "authoring tool). Save as prefab... keeps one of your own instead.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save as prefab..."))
        rood_library::request_save();
    tooltip("This volume's look as a fog preset of yours (the Library's Mine; pc/user/prefabs/fog)");
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
int extras_frame = -100; /* the selected volume's Details tools last drawn (the ghost preview) */
/* The interactive fog's preview tools (its sliders are Details fields:
 * editor_schema.cpp): its field in the view, the ghost at the mouse,
 * stamps at Ashley, its state. */
void preview_tools(Volume& v)
{
    ImGui::SeparatorText("Interactive fog preview");
    ImGui::BeginDisabled(!v.sim);
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
    tooltip("A box of mist on the surface under the view's centre, selected: its fields are in Details.");
    ImGui::SameLine();
    ImGui::Checkbox("Show in the view", &show_volumes);
    ImGui::TextDisabled("%d in this room: in the Outliner (Fog volumes); click one there or in the view\n"
                        "to edit it in Details.",
        (int)volumes.size());
    global_interaction_ui();
    bool alone = pc_haze_debug == 1;
    if (ImGui::Checkbox("Show the haze alone", &alone))
        pc_haze_debug = alone;
    tooltip("The light the haze, fog and volumes scatter toward the eye, over black (RPC graphics {haze_debug}).");
    bool where = pc_haze_debug == 2;
    if (ImGui::Checkbox("Show where the room's air is", &where))
        pc_haze_debug = where ? 2 : 0;
    tooltip("Red: pixels that show the void around the room (no haze or ground fog there); green: the room's interior "
            "at the surface (room settings haze_void, haze_interior; RPC graphics {haze_debug: 2}).");
    bool global = pc_fog_global != 0;
    if (ImGui::Checkbox("Ground fog and volumes on (all rooms)", &global))
        pc_fog_global = global;
    tooltip("Switches every room's ground fog and fog volumes off for comparison (settings key fog).");
    bool own = pc_fog_volume_render != 0;
    if (ImGui::Checkbox("Volumes' own renderer", &own))
        pc_fog_volume_render = own;
    tooltip("Volumes ray marched per pixel with their own lights, shadows and ambient light (settings\n"
            "fog_volume_render); off: inside the room's haze as before (coarser, under its limits).\n"
            "It draws the room's first 8 volumes; those set to Confine to room (the default) keep to the\n"
            "room's interior and off the void around it, as the haze does (room settings haze_interior,\n"
            "haze_void). The ones past the first 8 go through the haze, which keeps off void pixels past\n"
            "where their ray leaves the room.");
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

/* The room's fog volumes in the editor view (Show > Fog volumes): outlines,
 * the selected ones highlighted, each centre an anchor for box select and
 * the RPC (a click picks a volume by a ray against its shape:
 * editor_scene.cpp); the primary's gizmo when no light or actor is
 * selected. Returns true while the gizmo has the mouse. */
bool pc_atmosphere_viewport(const float view[16], const float projection[16], const float rect[4], void* draw_list)
{
    bool tools = ImGui::GetFrameCount() - extras_frame <= 1; /* the selected volume's Details tools show */
    if (selected >= (int)volumes.size())
        selected = -1;
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
            if (rood_scene::volume_hidden_id(v.id))
                continue;
            rood_selection::Ref ref = rood_scene::volume_ref(i);
            bool chosen = rood_selection::contains(ref);
            ImU32 col = i == selected ? IM_COL32(255, 230, 120, 230)
                : chosen             ? IM_COL32(255, 230, 120, 140)
                                     : IM_COL32(170, 200, 255, v.enabled ? 120 : 50);
            float thick = chosen ? 2.0f : 1.0f;
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
                rood_scene::icon(ref, c.x, c.y, 0, true); /* box select and the RPC; a click picks it by a ray */
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
        if (ghost_mode && tools && mouse.x >= rect[0] && mouse.y >= rect[1] && mouse.x < rect[0] + rect[2]
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
    /* The editor's tools: W moves, E turns, R sizes (scale semantics: its size per axis). */
    int operation = rood_lights::gizmo_operation;
    ImGuizmo::OPERATION op = operation == 2 ? ImGuizmo::SCALE : operation == 1 ? ImGuizmo::ROTATE_Y : ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mode = operation == 0 && !pc_lights_gizmo_space ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    if (ImGuizmo::Manipulate(view, projection, op, mode, model)) {
        float x[3] = { model[0], model[1], model[2] }, z[3] = { model[8], model[9], model[10] };
        float sx = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]), sz = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
        float sy = sqrtf(model[4] * model[4] + model[5] * model[5] + model[6] * model[6]);
        v.pos[0] = model[12], v.pos[1] = model[13], v.pos[2] = model[14];
        if (operation == 2)
            v.size[0] = fmaxf(sx, 2), v.size[1] = fmaxf(sy, 2), v.size[2] = fmaxf(sz, 2);
        if (operation == 1 && sx > 1e-6f)
            v.yaw = atan2f(-x[2], x[0]) / DEG;
        pc_lights_mark_dirty();
        pc_undo_hint(operation == 0 ? "Move fog volume" : operation == 2 ? "Size fog volume" : "Turn fog volume");
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

/* ---- the room's fog volumes for the editor (environment_internal.h) ---- */

int rood_atmo::volume_count() { return (int)volumes.size(); }
bool rood_atmo::volume_size(int index, float out[3])
{
    if (index < 0 || index >= (int)volumes.size())
        return false;
    memcpy(out, volumes[index].size, sizeof volumes[index].size);
    return true;
}
void rood_atmo::set_volume_size(int index, const float size[3])
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    memcpy(volumes[index].size, size, sizeof volumes[index].size);
    pc_lights_mark_dirty();
}
std::string rood_atmo::volume_id(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return "";
    if (volumes[index].id.empty())
        volumes[index].id = lighting::new_name();
    return "fog:" + volumes[index].id;
}
int rood_atmo::volume_index(const std::string& item_id)
{
    if (item_id.size() <= 4 || item_id.compare(0, 4, "fog:"))
        return -1;
    for (int i = 0; i < (int)volumes.size(); i++)
        if (!item_id.compare(4, std::string::npos, volumes[i].id))
            return i;
    return -1;
}
std::string rood_atmo::volume_label(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return "";
    const Volume& v = volumes[index];
    char label[128];
    snprintf(label, sizeof label, "%s %d%s%s", v.sphere ? "Sphere" : "Box", index + 1, v.name.empty() ? "" : ": ",
        v.name.c_str());
    return label;
}
void rood_atmo::set_selected(int index) { selected = index >= 0 && index < (int)volumes.size() ? index : -1; }
bool rood_atmo::volume_centre(int index, float out[3], float* radius)
{
    if (index < 0 || index >= (int)volumes.size())
        return false;
    const Volume& v = volumes[index];
    memcpy(out, v.pos, sizeof v.pos);
    *radius = 0.5f * sqrtf(v.size[0] * v.size[0] + v.size[1] * v.size[1] + v.size[2] * v.size[2]);
    return true;
}
float rood_atmo::volume_hit(int index, const float origin[3], const float dir[3])
{
    if (index < 0 || index >= (int)volumes.size() || !show_volumes)
        return -1;
    const Volume& v = volumes[index];
    if (rood_scene::volume_hidden_id(v.id)) /* hidden in the editor: not pickable */
        return -1;
    return rood_pick::volume_hit(origin, dir, v.pos, v.size, v.yaw, v.sphere);
}
bool rood_atmo::volumes_shown() { return show_volumes; }
extern "C" bool* pc_atmosphere_show_volumes(void) { return &show_volumes; }
lighting::Item rood_atmo::volume_item(int index) { return item_of(volumes[index]); }
int rood_atmo::add_volume_item(const lighting::Item& item, const float offset[3])
{
    if (item.kind != "fog")
        return -1;
    lighting::Item copy = item;
    copy.name = lighting::new_name();
    if (!apply_item(copy))
        return -1;
    Volume& v = volumes.back();
    for (int k = 0; k < 3; k++)
        v.pos[k] += offset[k];
    return (int)volumes.size() - 1;
}
void rood_atmo::remove_volume(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    volumes.erase(volumes.begin() + index);
    if (selected == index)
        selected = -1;
    else if (selected > index)
        --selected;
}
/* Outliner rows for the room's fog volumes: the enabled box, click to
 * select (Shift adds, Ctrl toggles), the layer badge. */
void rood_atmo::outliner(const char* filter)
{
    if (volumes.empty()) {
        ImGui::TextDisabled("None in this room (Details > Atmosphere: Add volume)");
        return;
    }
    for (int i = 0; i < (int)volumes.size(); i++) {
        Volume& v = volumes[i];
        std::string label = volume_label(i);
        if (filter && filter[0] && !strcasestr(label.c_str(), filter))
            continue;
        ImGui::PushID(i);
        if (ImGui::Checkbox("##on", &v.enabled)) {
            pc_lights_mark_dirty();
            pc_undo_hint(v.enabled ? "Enable fog volume" : "Disable fog volume");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enabled");
        ImGui::SameLine();
        rood_selection::Ref ref = rood_scene::volume_ref(i);
        bool hidden = rood_selection::hidden(ref);
        bool silent = !v.enabled || hidden;
        if (silent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (hidden)
            label += "  [hidden]###" + volume_label(i);
        if (ImGui::Selectable(label.c_str(), rood_selection::contains(ref)))
            rood_scene::row_clicked(ref);
        rood_scene::row_drawn(ref);
        if (silent)
            ImGui::PopStyleColor();
        rood_lights::layer_row(ref.id); /* layer badge, revert / promote menu */
        ImGui::PopID();
    }
}

/* The fog volume's fields (editor_schema.h): defaults from the lighting
 * schema (lighting_layers.cpp), so the two agree; what reset restores is the
 * volume's preset's value where its preset has the field. */
void rood_atmo::schema_kind(rood_schema::Kind& k)
{
    namespace rs = rood_schema;
    using rs::Field;
    using rs::Value;
    k = rs::Kind();
    k.name = "fog";
    k.label = "Fog volume";
    k.object = rood_selection::KIND_VOLUME;
    k.closed = { "Look", "Motion & interaction", "Advanced" };
    k.extras = [](int i) { rood_atmo::details_extra(i); };
    k.header = [](int i) { rood_atmo::details_header(i); };
    auto add = [&k](Field f) {
        const char* text = lighting::field_default("fog", f.name);
        if (*text)
            f.def = rs::from_text(f.type, f.options, text);
        if (lighting::preset_field("fog", f.name)) {
            std::string key = f.name;
            rs::Type type = f.type;
            std::vector<std::string> options = f.options;
            f.base = [key, type, options](int i) {
                const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
                const std::string* x = p ? p->get(key) : nullptr;
                return rs::from_text(type, options, x ? *x : std::string(lighting::field_default("fog", key)));
            };
            f.base_source = [key](int i) -> const char* {
                const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
                return p && p->get(key) ? "preset" : "default";
            };
        }
        k.fields.push_back(f);
    };
    auto scalar = [&add](Field f, float Volume::*m) {
        f.get = [m](int i) { return Value::scalar(volumes[i].*m); };
        f.set = [m](int i, const Value& v) { volumes[i].*m = v.x(); };
        add(f);
    };
    auto triple = [&add](Field f, float (Volume::*m)[3]) {
        f.get = [m](int i) {
            const float* p = volumes[i].*m;
            return Value::vec(p[0], p[1], p[2]);
        };
        f.set = [m](int i, const Value& v) {
            float* p = volumes[i].*m;
            for (int c = 0; c < 3; c++)
                p[c] = v.at(c);
        };
        add(f);
    };
    auto flag = [&add](Field f, bool Volume::*m) {
        f.get = [m](int i) { return Value::scalar(volumes[i].*m ? 1.0f : 0.0f); };
        f.set = [m](int i, const Value& v) { volumes[i].*m = v.x() > 0.5f; };
        add(f);
    };
    /* Basics (open) */
    flag(rs::bool_field("enabled", "Enabled", "Basics", true), &Volume::enabled);
    flag(rs::bool_field("confine", "Confine to room", "Basics", true,
             "Keeps to the room's interior like the ground fog and haze: over its floor, not under it nor over\n"
             "the void past the cut-away walls (room setting haze_interior). Off for a volume meant to sit\n"
             "outside the room: over a pit, beyond a window"),
        &Volume::confine);
    {
        Field f = rs::enum_field("shape", "Shape", "Basics", { "Box", "Sphere" }, 0, "A box, or an ellipsoid of the same size");
        f.get = [](int i) { return Value::scalar(volumes[i].sphere ? 1.0f : 0.0f); };
        f.set = [](int i, const Value& v) { volumes[i].sphere = v.x() > 0.5f; };
        add(f);
    }
    scalar(rs::number_field("density", "Density", "Basics", 0, 0.05f, 0.0001f, 0.004f, "/u", true,
               "Extinction per world unit in its middle (its ring in the view)"),
        &Volume::density);
    triple(rs::colour_field("color", "Colour", "Basics", 1, Value::vec(1, 1, 1),
               "Albedo: the share of each colour it scatters (its swatch in the view)"),
        &Volume::color);
    scalar(rs::number_field("edge", "Soft edge", "Basics", 0.01f, 1, 0.01f, 0.5f, "", false,
               "The part of its half size over which it fades out (its inner outline in the view)"),
        &Volume::edge);
    triple(rs::colour_field("fill", "Fill light", "Basics", 4, Value::vec(0, 0, 0),
               "Light it scatters from everywhere (radiance): keeps it visible where no light reaches"),
        &Volume::fill);
    /* Look */
    scalar(rs::number_field("opacity", "Opacity", "Look", 0, 1, 0.01f, 1, "", false,
               "The most of the scene it may hide along a ray (1: solid)"),
        &Volume::opacity);
    scalar(rs::number_field("brightness", "Brightness", "Look", 0, 4, 0.01f, 1, "", false, "Scale on the light it scatters and emits"),
        &Volume::brightness);
    scalar(rs::number_field("phase", "Forward scattering", "Look", -0.9f, 0.9f, 0.01f, 0.3f, "", false,
               "How much brighter it is looking toward a light through it"),
        &Volume::phase);
    scalar(rs::number_field("ambient", "Ambient", "Look", 0, 4, 0.01f, 1, "", false,
               "Scale on its ambient light (the room's baked light around it, the sky)"),
        &Volume::ambient);
    triple(rs::colour_field("glow", "Glow", "Look", 16, Value::vec(0, 0, 0), "Light it emits per unit of density"), &Volume::glow);
    scalar(rs::number_field("glow_core", "Glow at its base", "Look", 0, 1, 0.01f, 0.5f), &Volume::glow_core);
    scalar(rs::number_field("taper", "Taper", "Look", 0, 1, 0.01f, 0, "", false, "0 an even density, 1 thinning toward its top"),
        &Volume::taper);
    scalar(rs::number_field("noise", "Wisps", "Look", 0, 1, 0.01f, 0.5f, "", false, "How much the drifting noise thins and thickens it"),
        &Volume::noise);
    /* Motion & interaction */
    flag(rs::bool_field("sim", "Characters part it", "Motion & interaction", false,
             "Interactive fog: characters, attacks and spells clear it and leave a wake"),
        &Volume::sim);
    scalar(rs::number_field("sim_strength", "Strength", "Motion & interaction", 0, 2, 0.01f, 1), &Volume::sim_strength);
    scalar(rs::number_field("sim_recover", "Recovery time", "Motion & interaction", 0.3f, 12, 0.1f, 3, "s"), &Volume::sim_recover);
    scalar(rs::number_field("sim_footprint", "Footprint", "Motion & interaction", 0.25f, 3, 0.01f, 1), &Volume::sim_footprint);
    scalar(rs::number_field("sim_trail", "Trail", "Motion & interaction", 0, 3, 0.01f, 1), &Volume::sim_trail);
    scalar(rs::number_field("sim_wind", "Wind", "Motion & interaction", 0, 4, 0.01f, 1), &Volume::sim_wind);
    scalar(rs::number_field("sim_swirl", "Swirl", "Motion & interaction", 0, 4, 0.01f, 0), &Volume::sim_swirl);
    static const char* const global_keys[4] = { "player", "actors", "attacks", "spells" };
    for (int s = 0; s < 4; s++) {
        /* Not through add(): the lighting schema's default is -1 (global); here the value shown is the weight in effect. */
        Field f = rs::number_field(DISTURB_KEYS[s], SOURCE_LABELS[s], "Motion & interaction", 0, 2, 0.01f, 1, "", false,
            SOURCE_TIPS[s]);
        f.global_tooltip = std::string("the weight every room uses (Atmosphere > Interactive fog: all rooms; settings key fog_disturb_")
            + global_keys[s] + ")";
        f.get = [s](int i) { return Value::scalar(volume_weight(volumes[i], s)); };
        f.set = [s](int i, const Value& v) { volumes[i].disturb[s] = fmaxf(v.x(), 0.0f); };
        f.uses_global = [s](int i) { return volumes[i].disturb[s] < 0; };
        f.set_uses_global = [s](int i, bool on) { volumes[i].disturb[s] = on ? -1 : fmaxf(pc_fog_disturb[s], 0.0f); };
        std::string key = DISTURB_KEYS[s];
        auto raw = [key](int i) { /* the preset's weight (-1: global), else the schema's -1 */
            const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
            const std::string* x = p ? p->get(key) : nullptr;
            float w = x ? (float)atof(x->c_str()) : -1.0f;
            return w < 0 ? -1.0f : w;
        };
        f.base = [raw, s](int i) {
            float w = raw(i);
            return Value::scalar(w < 0 ? fmaxf(pc_fog_disturb[s], 0.0f) : w);
        };
        f.base_source = [key](int i) -> const char* {
            const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
            return p && p->get(key) ? "preset" : "default";
        };
        f.overridden = [raw, s](int i) { return fabsf(volumes[i].disturb[s] - raw(i)) > 1e-4f; };
        f.reset = [raw, s](int i) { volumes[i].disturb[s] = raw(i); };
        k.fields.push_back(f);
    }
    /* Advanced */
    scalar(rs::number_field("detail", "Detail", "Advanced", 0, 1, 0.01f, 0.5f, "", false,
               "The detail noise at its soft edges and in the interactive field"),
        &Volume::detail);
    scalar(rs::number_field("detail_size", "Billow size", "Advanced", 0, 1024, 1, 0, "u", false,
               "Its billows' size (0: from the room's wisp size)"),
        &Volume::detail_size);
    scalar(rs::number_field("shadow", "Self-shadow", "Advanced", 0, 2, 0.01f, 0, "", false,
               "Its dense parts shade themselves from the main light"),
        &Volume::shadow);
    /* Transform (open) */
    {
        Field f = rs::vec3_field("pos", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "Its centre");
        f.resettable = false, f.shared = false;
        triple(f, &Volume::pos);
    }
    triple(rs::vec3_field("size", "Size", "Transform", 2, 8192, 1, Value::vec(256, 128, 256), "u",
               "Full extents (the scale tool, R)"),
        &Volume::size);
    scalar(rs::angle_field("yaw", "Yaw", "Transform", -180, 180, 0.5f, 0), &Volume::yaw);
}

/* Details > a fog volume's preset row, above its fields (preset first): pick
 * one, Reset to preset, Save as preset..., Reload presets. */
void rood_atmo::details_header(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    ImGui::PushID(volumes[index].id.c_str());
    if (preset_ui(volumes[index]))
        pc_lights_mark_dirty();
    ImGui::PopID();
}

/* Details > a fog volume's own tools under its fields (editor_schema.cpp
 * draws those): the gizmo's mode, Drop to floor and Fit to floor area, its
 * name, its preset, the interactive fog's preview, Delete. */
void rood_atmo::details_extra(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    extras_frame = ImGui::GetFrameCount();
    Volume& v = volumes[index];
    bool changed = false;
    ImGui::PushID(v.id.c_str());
    ImGui::SeparatorText("Tools");
    ImGui::RadioButton("Move (W)", &rood_lights::gizmo_operation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Turn (E)", &rood_lights::gizmo_operation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Size (R)", &rood_lights::gizmo_operation, 2);
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
    tooltip("Its footprint becomes the level floor around its centre (the room's floor grid: the cells with\n"
            "a floor connected to the one under it, within 12 units of its height; the largest box of them\n"
            "around it, never over the void past the walls), square to the room, on the floor.");
    char name[96];
    snprintf(name, sizeof name, "%s", v.name.c_str());
    if (ImGui::InputText("Name", name, sizeof name)) {
        v.name = name;
        changed = true;
    }
    preview_tools(v);
    if (ImGui::SmallButton("Delete volume")) {
        pc_undo_hint("Delete fog volume");
        remove_volume(index);
        rood_scene::refresh();
        changed = true;
    }
    ImGui::PopID();
    if (changed)
        pc_lights_mark_dirty();
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
    read_bool(o, "confine", &v.confine);
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
 * of {id (existing: changed; else added), remove, enabled, confine, shape, name,
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
        for (const char* key : { "density", "anisotropy", "glow", "max_opacity", "max_light", "shafts", "max_forward",
                 "interior", "interior_fill", "under_floor", "void", "void_level", "outside", "interior_edge",
                 "interior_slope" })
            if (const prefab::Json* j = hz->get(key)) {
                double v = j->type == prefab::Json::Bool ? (j->boolean ? 1.0 : 0.0) : j->number;
                char line[96];
                snprintf(line, sizeof line, "set haze_%s %.6f", key, v);
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
        int found = -1;
        for (int i = 0; i < (int)volumes.size(); i++)
            if (volumes[i].id == sel->string)
                found = i;
        rood_selection::set(rood_scene::volume_ref(found), rood_selection::FROM_RPC);
        rood_scene::publish();
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
    static unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    if (pc_scene_floor_grid(heights, corner, cell) && pc_scene_floor_cells(cells)) {
        std::string occupied;
        for (int k = 0; k < PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID; k++)
            occupied += (k ? "," : "") + std::to_string(cells[k]);
        grid = "{\"n\":" + std::to_string(PC_SCENE_FLOOR_GRID) + ",\"corner\":" + jvec(corner, 2) + ",\"cell\":"
            + jvec(cell, 2) + ",\"heights\":" + jvec(heights, PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID)
            + ",\"occupied\":[" + occupied + "]}";
    }
    std::string s = "\"haze\":{\"density\":" + num(pc_fog_density, 6) + ",\"anisotropy\":" + num(pc_fog_anisotropy, 3)
        + ",\"max_opacity\":" + num(pc_haze_max_opacity, 3) + ",\"max_light\":" + num(pc_haze_max_light, 3)
        + ",\"shafts\":" + num(pc_haze_shafts, 3) + ",\"max_forward\":" + num(pc_haze_max_forward, 3)
        + ",\"interior\":" + jbool(pc_haze_interior > 0.5f) + ",\"interior_fill\":" + num(pc_haze_interior_fill, 0)
        + ",\"under_floor\":" + num(pc_haze_under_floor, 1) + ",\"void\":" + jbool(pc_haze_void > 0.5f)
        + ",\"void_level\":" + num(pc_haze_void_level, 0) + ",\"outside\":" + num(pc_haze_outside, 3)
        + ",\"interior_edge\":" + num(pc_haze_interior_edge, 3) + ",\"interior_slope\":" + num(pc_haze_interior_slope, 0)
        + "},\"fog\":{\"defined\":" + jbool(fog.defined) + ",\"enabled\":" + jbool(fog.enabled)
        + ",\"density\":" + num(fog.density, 5) + ",\"height\":" + num(fog.height, 1) + ",\"offset\":"
        + num(fog.offset, 1) + ",\"noise\":" + num(fog.noise, 2) + ",\"scale\":" + num(fog.scale, 1)
        + ",\"wind\":" + jvec(fog.wind, 3) + ",\"color\":" + jvec(fog.color, 3) + ",\"fill\":" + jvec(fog.fill, 3)
        + ",\"sky\":" + num(fog.sky, 3) + ",\"floor\":" + (has_floor ? num(floor_y, 1) : std::string("null"))
        + ",\"floor_grid\":" + grid + "},\"bounds\":[" + jvec(lo, 3) + "," + jvec(hi, 3) + "],\"fog_global\":" + jbool(pc_fog_global)
        + ",\"fog_time\":" + num(pc_fog_time, 3) + ",\"volumes\":[";
    for (size_t i = 0; i < volumes.size(); i++) {
        const Volume& v = volumes[i];
        s += (i ? "," : "") + std::string("{\"id\":") + prefab::json_quote(v.id) + ",\"name\":"
            + prefab::json_quote(v.name) + ",\"enabled\":" + jbool(v.enabled) + ",\"confine\":" + jbool(v.confine)
            + ",\"shape\":\""
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

/* ---- fog presets for the library (environment_internal.h) ---- */

std::vector<rood_atmo::FogPreset> rood_atmo::fog_presets()
{
    if (!presets_loaded)
        load_presets();
    std::vector<FogPreset> out;
    for (const Preset& p : presets)
        out.push_back({ p.id, p.name, p.why, p.user });
    return out;
}
unsigned rood_atmo::fog_presets_serial()
{
    if (!presets_loaded)
        load_presets();
    static double checked;
    double t = now_seconds();
    if (t - checked > 0.5) {
        checked = t;
        if (prefab::directory_stamp(user_presets_dir()) != user_presets_stamp)
            load_presets();
    }
    return presets_serial;
}
bool rood_atmo::preset_look(const std::string& id, rood_thumbs::Fog& out)
{
    Volume v;
    if (!id.empty()) {
        const Preset* p = find_preset(id);
        if (!p)
            return false;
        apply_preset(v, *p);
    }
    out = rood_thumbs::Fog();
    out.on = true, out.sphere = v.sphere;
    for (int k = 0; k < 3; k++) {
        out.size[k] = v.size[k];
        out.color[k] = v.color[k], out.fill[k] = v.fill[k], out.glow[k] = v.glow[k];
    }
    out.density = v.density, out.edge = v.edge;
    out.noise = v.noise, out.taper = v.taper, out.glow_core = v.glow_core, out.phase = v.phase;
    out.opacity = v.opacity, out.brightness = v.brightness, out.ambient = v.ambient, out.shadow = v.shadow;
    return true;
}
bool rood_atmo::save_user_preset(int index, const std::string& name, std::string* id_out, std::string* message)
{
    if (index < 0 || index >= (int)volumes.size()) {
        *message = "Save as prefab: no such fog volume";
        return false;
    }
    if (!presets_loaded)
        load_presets();
    std::string base = prefab::make_id(name), id = base;
    for (int n = 2; find_preset(id); n++)
        id = base + "_" + std::to_string(n);
    Volume& v = volumes[index];
    std::string dir = user_presets_dir(), path = dir + "/" + id + ".json", temp = path + ".tmp";
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/')
            pc_mkdir(dir.substr(0, i).c_str());
    std::string text = prefab::json_write(preset_json(v, name, "")) + "\n";
    FILE* f = fopen(temp.c_str(), "wb");
    bool ok = f && fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f)
        fclose(f);
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        *message = "cannot write " + path;
        return false;
    }
    load_presets();
    v.preset = id; /* it names your preset; the room still writes all of its values (item_of) */
    pc_lights_mark_dirty();
    *id_out = id;
    *message = "Saved fog preset " + name + " (" + path + ")";
    return true;
}
std::string rood_atmo::user_preset_file(const std::string& id)
{
    const Preset* p = id.empty() ? nullptr : find_preset(id);
    return p && p->user ? p->path : std::string();
}
bool rood_atmo::rename_user_preset(const std::string& id, const std::string& name, std::string* message)
{
    std::string path = user_preset_file(id), text, error;
    prefab::Json j;
    if (path.empty() || name.empty()) {
        *message = path.empty() ? "Rename: not a fog preset of yours" : "Rename: give it a name";
        return false;
    }
    if (!read_text(path, text) || !prefab::json_parse(text.c_str(), j, &error) || j.type != prefab::Json::Object) {
        *message = path + ": " + (error.empty() ? "not a preset object" : error);
        return false;
    }
    j.set("name", prefab::Json::str(name));
    std::string out = prefab::json_write(j) + "\n", temp = path + ".tmp";
    FILE* f = fopen(temp.c_str(), "wb");
    bool ok = f && fwrite(out.data(), 1, out.size(), f) == out.size();
    if (f)
        fclose(f);
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        *message = "cannot write " + path;
        return false;
    }
    load_presets();
    *message = "Renamed fog preset " + id + " to " + name;
    return true;
}
bool rood_atmo::delete_user_preset(const std::string& id, std::string* message)
{
    std::string path = user_preset_file(id);
    if (path.empty()) {
        *message = "Delete: not a fog preset of yours";
        return false;
    }
    if (pc_replace_file(path.c_str(), (path + ".bak").c_str())) {
        *message = "cannot move " + path + " aside";
        return false;
    }
    load_presets();
    pc_lights_mark_dirty(); /* volumes naming it write all their values (they did already) */
    *message = "Deleted fog preset " + id + " (moved aside: " + path + ".bak)";
    return true;
}
bool rood_atmo::floor_area_at(float x, float z, float lo[2], float hi[2], float* height)
{
    return floor_area(x, z, lo, hi, height);
}
int rood_atmo::volumes_using(const std::string& preset)
{
    int n = 0;
    for (const Volume& v : volumes)
        n += !preset.empty() && v.preset == preset;
    return n;
}
bool rood_atmo::volume_drop(const std::string& preset, const float surface[3], const float normal[3], float pos[3],
    float size[3], bool* sphere)
{
    Volume v;
    if (!preset.empty()) {
        const Preset* p = find_preset(preset);
        if (!p)
            return false;
        apply_preset(v, *p);
    }
    float at[3] = { surface[0], surface[1], surface[2] };
    if (!(normal[1] < -0.5f)) { /* a wall or a ceiling (y down: a floor's normal points to -y) */
        float h = sqrtf(normal[0] * normal[0] + normal[2] * normal[2]);
        if (h > 1e-3f) { /* out of a wall by half its footprint */
            float reach = 0.5f * fmaxf(v.size[0], v.size[2]);
            at[0] += normal[0] / h * reach, at[2] += normal[2] / h * reach;
        }
        float y;
        if (floor_under(at[0], at[2], at[1] - 8, &y) == 1) /* the floor below */
            at[1] = y;
    }
    float centre[2] = { at[0], at[2] }, foot[2] = { v.size[0], v.size[2] };
    float lo[2], hi[2], h0;
    const float here[2] = { at[0], at[2] }, want[2] = { v.size[0], v.size[2] };
    if (floor_area(at[0], at[2], lo, hi, &h0) && fabsf(h0 - at[1]) <= 12) { /* that floor's level area */
        rood_math::fit_footprint(here, want, lo, hi, centre, foot);
    } else { /* no level area known there (a step, a raised floor): at least over the room's floor */
        float blo[3], bhi[3], rlo[2], rhi[2];
        if (!floor_extent(rlo, rhi)) {
            pc_scene_bounds(blo, bhi);
            rlo[0] = blo[0], rlo[1] = blo[2], rhi[0] = bhi[0], rhi[1] = bhi[2];
        }
        rood_math::fit_footprint(here, want, rlo, rhi, centre, foot);
    }
    pos[0] = centre[0], pos[1] = at[1] - v.size[1] * 0.5f, pos[2] = centre[1];
    size[0] = foot[0], size[1] = v.size[1], size[2] = foot[1];
    *sphere = v.sphere;
    return true;
}
int rood_atmo::place_volume(const std::string& preset, const float surface[3], const float normal[3])
{
    float pos[3], size[3];
    bool sphere;
    if (!volume_drop(preset, surface, normal, pos, size, &sphere))
        return -1;
    Volume v;
    if (const Preset* p = preset.empty() ? nullptr : find_preset(preset)) {
        apply_preset(v, *p);
        v.name = p->name;
    }
    v.id = lighting::new_name();
    memcpy(v.pos, pos, sizeof v.pos);
    memcpy(v.size, size, sizeof v.size);
    volumes.push_back(v);
    int index = (int)volumes.size() - 1;
    rood_selection::set(rood_scene::volume_ref(index), rood_selection::FROM_CODE);
    rood_scene::publish();
    pc_lights_mark_dirty();
    return index;
}
