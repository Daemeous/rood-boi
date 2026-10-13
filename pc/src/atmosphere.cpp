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
#include <time.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImGuizmo.h"
#include "editor_overlays.h"
#include "editor_undo.h"
#include "environment.h"
#include "environment_internal.h"
#include "fog_sim.h"
#include "lighting_layers.h"
#include "lighting_quality.h"
#include "lights_internal.h"
#include "scene_capture.h"

extern "C" {
extern int pc_lights_enabled;
unsigned pc_vsync_count(void) __attribute__((weak)); /* game time */
extern float pc_fog_anisotropy, pc_fog_density;
int pc_room_settings_parse(const char* line);
void pc_lights_mark_dirty(void);
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

static_assert(PC_FOG_SIM_GRID * PC_FOG_SIM_GRID == PC_FOG_SIM_CELLS, "atmosphere.h: the field's size");
static_assert(PC_MEDIUM_VEC4S == 250, "the shaders' medium_[] arrays");
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
    /* The interactive fog (fog_sim.h, prototype): the characters part it. */
    bool sim = false;
    float sim_strength = 1, sim_recover = 3;
};
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
        const std::string* shape = it.get("shape");
        v.sphere = shape && *shape == "sphere";
        get3(it, "pos", v.pos);
        get3(it, "size", v.size);
        v.yaw = get1(it, "yaw");
        v.density = get1(it, "density");
        v.edge = get1(it, "edge");
        get3(it, "color", v.color);
        get3(it, "fill", v.fill);
        v.noise = get1(it, "noise");
        v.sim = get1(it, "sim") != 0;
        v.sim_strength = get1(it, "sim_strength");
        v.sim_recover = get1(it, "sim_recover");
        for (Volume& o : volumes)
            if (o.id == v.id) {
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
        it.set("shape", v.sphere ? "sphere" : "box");
        it.set("pos", vec3(v.pos, 1));
        it.set("size", vec3(v.size, 1));
        it.set("yaw", num(v.yaw, 2));
        it.set("density", num(v.density, 5));
        it.set("edge", num(v.edge, 2));
        it.set("color", vec3(v.color, 3));
        it.set("fill", vec3(v.fill, 3));
        it.set("noise", num(v.noise, 2));
        it.set("enabled", v.enabled ? "1" : "0");
        it.set("sim", v.sim ? "1" : "0");
        it.set("sim_strength", num(v.sim_strength, 2));
        it.set("sim_recover", num(v.sim_recover, 2));
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
    bool simulated = false;
    if (fogs)
        for (const Volume& v : volumes) {
            if (!v.enabled || v.density <= 0 || n >= PC_MEDIUM_VOLUMES)
                continue;
            /* The first simulated volume carries the field (prototype: one per room). */
            if (v.sim && !simulated) {
                simulated = true;
                rood_fog_sim::Frame f;
                for (int k = 0; k < 3; k++)
                    f.pos[k] = v.pos[k], f.half[k] = fmaxf(v.size[k] * 0.5f, 1.0f),
                    f.wind[k] = fog_on() ? fog.wind[k] : Fog().wind[k];
                f.yaw_cos = cosf(v.yaw * DEG), f.yaw_sin = sinf(v.yaw * DEG);
                f.strength = v.sim_strength, f.recover = v.sim_recover;
                rood_fog_sim::step(v.id.c_str(), f, pc_vsync_count ? pc_vsync_count() : 0);
                if (const float* field = rood_fog_sim::field(v.id.c_str())) {
                    m[n * 5 + 8 + 3][3] = 1; /* this volume samples the field */
                    m[PC_MEDIUM_SIM][0] = PC_FOG_SIM_GRID;
                    float* data = m[PC_MEDIUM_SIM_DATA];
                    for (int k = 0; k < PC_FOG_SIM_GRID * PC_FOG_SIM_GRID; k += 3) {
                        int q[3] = { 0, 0, 0 };
                        for (int s = 0; s < 3 && k + s < PC_FOG_SIM_GRID * PC_FOG_SIM_GRID; s++)
                            q[s] = (int)lroundf(fminf(fmaxf(field[k + s] * 0.5f, 0.0f), 1.0f) * 255.0f);
                        data[k / 3] = (float)(q[0] * 65536 + q[1] * 256 + q[2]); /* exact: below 2^24 */
                    }
                }
            }
            float(*b)[4] = m + 8 + 5 * n;
            for (int k = 0; k < 3; k++) {
                b[0][k] = v.pos[k];
                b[1][k] = fmaxf(v.size[k] * 0.5f, 1.0f);
                b[2][k] = fmaxf(v.color[k], 0.0f);
                b[3][k] = fmaxf(v.fill[k], 0.0f);
            }
            b[0][3] = v.density;
            b[1][3] = fminf(fmaxf(v.edge, 0.001f), 1.0f);
            b[2][3] = fminf(fmaxf(v.noise, 0.0f), 1.0f);
            b[4][0] = cosf(v.yaw * DEG), b[4][1] = sinf(v.yaw * DEG), b[4][2] = v.sphere ? 1.0f : 0.0f;
            n++;
        }
    m[0][3] = (float)n;
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
    if (pc_env_gpu_get(&env) && env.zenith[3] > 0.5f)
        for (int k = 0; k < 3; k++)
            m[5][k] = (env.zenith[k] + 2 * env.horizon[k] + env.ground[k]) * 0.25f * sky * 0.5f;
    return m[0][0] > 0 || m[1][0] > 0 || n > 0;
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

/* Returns true when something changed (environment.cpp's page marks the room). */
bool pc_atmosphere_page(void)
{
    page_frame = ImGui::GetFrameCount();
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
        changed |= ImGui::Checkbox("Characters part it (prototype)", &v.sim);
        tooltip("Interactive fog: Ashley and the enemies clear it around their feet and leave a soft wake\n"
                "that drifts with the wind and settles back (one volume per room; off by default).");
        if (v.sim) {
            changed |= ImGui::SliderFloat("Parting", &v.sim_strength, 0, 2, "%.2f");
            tooltip("How much a character clears (1: its footprint at a fifth of the fog standing still).");
            changed |= ImGui::SliderFloat("Settles in", &v.sim_recover, 0.3f, 12, "%.1f s");
        }
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
    bool alone = pc_haze_debug != 0;
    if (ImGui::Checkbox("Show the haze alone", &alone))
        pc_haze_debug = alone;
    tooltip("The light the haze, fog and volumes scatter toward the eye, over black (RPC graphics {haze_debug}).");
    bool global = pc_fog_global != 0;
    if (ImGui::Checkbox("Ground fog and volumes on (all rooms)", &global))
        pc_fog_global = global;
    tooltip("Switches every room's ground fog and fog volumes off for comparison (settings key fog).");
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
    if (!page && selected < 0)
        return false;
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
            + ",\"sim_recover\":" + num(v.sim_recover, 2) + "}";
    }
    float least, mean, at_player;
    int actors;
    rood_fog_sim::stats(&least, &mean, &at_player, &actors);
    std::string sim_id;
    for (const Volume& v : volumes)
        if (v.sim && v.enabled && rood_fog_sim::field(v.id.c_str())) {
            sim_id = v.id;
            break;
        }
    s += "],\"sim\":";
    if (sim_id.empty())
        return s + "null";
    return s + "{\"id\":" + prefab::json_quote(sim_id) + ",\"n\":" + std::to_string(PC_FOG_SIM_GRID)
        + ",\"least\":" + num(least, 3) + ",\"mean\":" + num(mean, 3) + ",\"at_player\":" + num(at_player, 3)
        + ",\"actors\":" + std::to_string(actors)
        + (sim_field_reply ? ",\"field\":" + jvec(rood_fog_sim::field(sim_id.c_str()), PC_FOG_SIM_GRID * PC_FOG_SIM_GRID) : "")
        + "}";
}
} // namespace rood_atmo
