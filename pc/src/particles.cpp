// The port's particle system (particles.h, particles_internal.h; pc/docs/Particles.md):
// the room's emitters (lighting items, their assets resolved), the frame the
// renderers draw, OpenGL's CPU path, RPC particles.
#include "particles.h"
#include "particles_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <functional>
#include <string>
#include <vector>

#include "atmosphere.h"
#include "editor_scene.h"
#include "editor_selection.h"
#include "editor_undo.h"
#include "environment.h"
#include "light_prefabs.h"
#include "light_shape.h"
#include "lighting_layers.h"
#include "lights_internal.h"
#include "particle_assets.h"
#include "scene_capture.h"

extern "C" {
unsigned pc_vsync_count(void) __attribute__((weak)); /* game time */
int pc_gi_trace_cpu(const float o[3], const float d[3], float tmax, float* t_out, float centre[3]) __attribute__((weak));
int pc_gi_scene_cpu(void) __attribute__((weak)); /* ptgi.c: OpenGL's traced scene, built when GI has not */
extern int pc_gte_widescreen;
extern int pc_vk_active __attribute__((weak));
extern int pc_gl_view[4] __attribute__((weak));
void pc_lights_mark_dirty(void);
int pc_vk_particles_read(uint32_t* keys, int w, int h, float* colours, int count) __attribute__((weak)); /* vk_particles.inc */

int pc_dust = 0, pc_particles_lights = 2, pc_particles_glow_shadows = 1, pc_particles_cpu_rays = 1536, pc_particles_debug = 0;
int pc_particles_untraced = 0;
float pc_dust_low = 0.4f, pc_particles_glow = 1.0f, pc_particles_time = -1.0f;
}

namespace {
const float DEG = 3.14159265f / 180.0f;
using rood_particles::Asset;

/* Defaults: the lighting schema's (lighting_layers.cpp kind "emitter"). */
struct Emitter {
    std::string id, name, asset; /* id: the item's name */
    bool enabled = true;
    float pos[3] = { 0, 0, 0 }, yaw = 0;
    int seed = 1;
    /* from its asset unless written */
    int shape = 0; /* SHAPES */
    float size[3] = { 256, 192, 256 }, radius = 64;
    int count = 400;
    float lifetime = 10, jitter = 0.5f;
    float drift[3] = { 0, 0, 0 }, curl = 0, curl_scale = 64, curl_speed = 0.1f, gravity = 0, drag = 0;
    float fade_in = 0, fade_out = 0;
    int pixel_min = 1, pixel_max = 2;
    float color[3] = { 1, 1, 1 }, brightness = 1, variation = 0.5f;
    bool lit = false;
    float ambient = 0;
    std::string quality = "dust";
    int drawn = 0; /* motes last frame */
    /* With an asset that is not loaded (missing, or failing to parse): the asset
     * fields its item wrote, so item_of keeps exactly those and the rest follow
     * the asset once it loads (reload). */
    std::vector<std::string> written;
};
const char* const SHAPES[5] = { "box", "room", "point", "disc", "surface" };
std::vector<Emitter> emitters;
bool show_emitters = true;
int time_zone = -1, time_room = -1;
double time_origin;

std::string num(double v, int d) { return lighting::format_number(v, d); }
std::string vec3(const float* v, int d) { return num(v[0], d) + "," + num(v[1], d) + "," + num(v[2], d); }
const Asset* asset_of(const Emitter& e) { return e.asset.empty() ? nullptr : rood_particles::find_asset(e.asset); }

/* Every field an asset may give, as lighting text. */
lighting::Fields asset_fields(const Emitter& e)
{
    return { { "shape", SHAPES[e.shape] }, { "size", vec3(e.size, 1) }, { "radius", num(e.radius, 1) },
        { "count", std::to_string(e.count) }, { "lifetime", num(e.lifetime, 2) }, { "lifetime_jitter", num(e.jitter, 2) },
        { "drift", vec3(e.drift, 2) }, { "curl", num(e.curl, 2) }, { "curl_scale", num(e.curl_scale, 1) },
        { "curl_speed", num(e.curl_speed, 3) }, { "gravity", num(e.gravity, 3) }, { "drag", num(e.drag, 3) },
        { "fade_in", num(e.fade_in, 2) }, { "fade_out", num(e.fade_out, 2) }, { "render", "pixel" },
        { "pixel_min", std::to_string(e.pixel_min) }, { "pixel_max", std::to_string(e.pixel_max) },
        { "color", vec3(e.color, 3) }, { "brightness", num(e.brightness, 3) }, { "variation", num(e.variation, 2) },
        { "lit", e.lit ? "1" : "0" }, { "ambient", num(e.ambient, 4) }, { "quality", e.quality } };
}
void from_fields(Emitter& e, const std::function<std::string(const char*)>& get)
{
    auto f = [&](const char* k) { return (float)atof(get(k).c_str()); };
    auto v3 = [&](const char* k, float* out) {
        float x[3];
        if (sscanf(get(k).c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]) == 3)
            memcpy(out, x, sizeof x);
    };
    std::string shape = get("shape");
    e.shape = 0;
    for (int i = 0; i < 5; i++)
        if (shape == SHAPES[i])
            e.shape = i;
    v3("size", e.size);
    e.radius = fmaxf(f("radius"), 0);
    e.count = std::max(0, std::min(PC_PARTICLES_MAX, (int)lroundf(f("count"))));
    e.lifetime = fmaxf(f("lifetime"), 0.05f), e.jitter = fminf(fmaxf(f("lifetime_jitter"), 0), 0.95f);
    v3("drift", e.drift);
    e.curl = fmaxf(f("curl"), 0), e.curl_scale = fmaxf(f("curl_scale"), 1), e.curl_speed = f("curl_speed");
    e.gravity = f("gravity"), e.drag = fmaxf(f("drag"), 0);
    e.fade_in = fmaxf(f("fade_in"), 0), e.fade_out = fmaxf(f("fade_out"), 0);
    e.pixel_min = std::max(1, std::min(4, (int)lroundf(f("pixel_min"))));
    e.pixel_max = std::max(e.pixel_min, std::min(4, (int)lroundf(f("pixel_max"))));
    v3("color", e.color);
    e.brightness = fmaxf(f("brightness"), 0), e.variation = fminf(fmaxf(f("variation"), 0), 1);
    e.lit = f("lit") > 0.5f, e.ambient = fmaxf(f("ambient"), 0);
    e.quality = get("quality");
}
/* A field of an item: as written, else its asset's, else the schema's default. */
std::string item_field(const lighting::Item& it, const Asset* a, const char* key)
{
    if (const std::string* v = it.get(key))
        return *v;
    if (a)
        if (const std::string* v = a->get(key))
            return *v;
    return lighting::field_default("emitter", key);
}
std::string canonical(const char* key, const std::string& v) { return lighting::canonical_value("emitter", key, v); }
/* An emitter as its item: with an asset, only what differs from it (an asset edit updates every emitter). */
lighting::Item item_of(Emitter& e)
{
    if (e.id.empty())
        e.id = lighting::new_name();
    lighting::Item it;
    it.kind = "emitter", it.name = e.id;
    if (!e.name.empty())
        it.set("name", e.name);
    if (!e.asset.empty())
        it.set("asset", e.asset);
    it.set("pos", vec3(e.pos, 1));
    it.set("yaw", num(e.yaw, 2));
    it.set("enabled", e.enabled ? "1" : "0");
    it.set("seed", std::to_string(e.seed));
    /* No asset: every value. An asset: what differs from it. An asset not loaded: what
     * the item wrote and what differs from the default (the rest follows the asset once it loads). */
    const Asset* a = asset_of(e);
    bool missing = !a && !e.asset.empty();
    for (const auto& f : asset_fields(e)) {
        if (a || missing) {
            const std::string* p = a ? a->get(f.first) : nullptr;
            std::string base = p ? *p : lighting::field_default("emitter", f.first);
            bool kept = missing && std::find(e.written.begin(), e.written.end(), f.first) != e.written.end();
            if (!kept && canonical(f.first.c_str(), f.second) == canonical(f.first.c_str(), base))
                continue;
        }
        it.set(f.first.c_str(), f.second);
    }
    lighting::canonicalize(it);
    return it;
}
int index_of_id(const std::string& id)
{
    for (int i = 0; i < (int)emitters.size(); i++)
        if (emitters[i].id == id)
            return i;
    return -1;
}
double game_seconds()
{
    if (pc_vsync_count)
        return pc_vsync_count() / 59.94;
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
/* Seconds since entering the room (the fog's medium_time, unwrapped), or the fixed time. */
float particles_time()
{
    if (pc_particles_time >= 0)
        return pc_particles_time;
    if (time_zone != rood_lights::zone || time_room != rood_lights::room) {
        time_zone = rood_lights::zone, time_room = rood_lights::room;
        time_origin = game_seconds();
    }
    return (float)fmax(game_seconds() - time_origin, 0.0);
}
} // namespace

/* ---- the frame ---- */

namespace {
struct Frame {
    pc_particles_gpu gpu; /* the records (Vulkan: the pass's UBO) */
    std::vector<pc_light_gpu> lights;
    int light_count = 0, motes = 0, w = 0, h = 0, floor_n = 0;
    float t = 0;
    float heights[PC_PARTICLE_FLOOR_N * PC_PARTICLE_FLOOR_N];
    float floor_info[4] = { 0, 0, 1, 1 };
    std::vector<std::string> ids; /* the packed emitters' ids */
};
Frame frame;
/* CPU time a frame with motes (RPC perf "cpu_sections": "particles"), until read. */
double perf_sum;
int perf_frames;
void perf_add(std::chrono::steady_clock::time_point start)
{
    perf_sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ++perf_frames;
}
struct Stats {
    const char* path = "off"; /* "traced", "untraced", "off" */
    const char* scene = "none"; /* what traced: "gi" (Vulkan: the path tracer's), "own" (Vulkan: built for them), "cpu" (OpenGL) */
    int motes = 0;
    double cpu_ms = 0; /* smoothed */
} stats;

/* The share of an emitter's count the graphics settings draw. */
float share(const Emitter& e)
{
    if (e.quality == "dust")
        return pc_dust >= 2 ? 1.0f : pc_dust == 1 ? fminf(fmaxf(pc_dust_low, 0), 1) : 0.0f;
    return 1.0f;
}
void pack(const Emitter& e, int first, int count, const float lo[3], const float hi[3], pc_emitter_gpu& out)
{
    memset(&out, 0, sizeof out);
    float* v0 = out.v[0];
    float* v1 = out.v[1];
    memcpy(v0, e.pos, sizeof e.pos);
    v0[3] = e.yaw * DEG;
    memcpy(v1, e.size, sizeof e.size);
    v1[3] = PC_SPAWN_BOX;
    if (e.shape == 1) { /* room: its bounds as a box */
        for (int k = 0; k < 3; k++)
            v0[k] = (lo[k] + hi[k]) * 0.5f, v1[k] = fmaxf(hi[k] - lo[k], 1);
        v0[3] = 0;
    } else if (e.shape == 2 || e.shape == 3) {
        v1[0] = e.radius, v1[3] = e.shape == 2 ? PC_SPAWN_POINT : PC_SPAWN_DISC;
    } else if (e.shape == 4) {
        v1[3] = PC_SPAWN_SURFACE;
    }
    memcpy(out.v[2], e.drift, sizeof e.drift);
    out.v[2][3] = e.gravity;
    out.v[3][0] = e.curl, out.v[3][1] = e.curl_scale, out.v[3][2] = e.curl_speed, out.v[3][3] = e.drag;
    out.v[4][0] = e.lifetime, out.v[4][1] = e.jitter, out.v[4][2] = e.fade_in, out.v[4][3] = e.fade_out;
    memcpy(out.v[5], e.color, sizeof e.color);
    out.v[5][3] = e.brightness;
    out.v[6][0] = (float)e.pixel_min, out.v[6][1] = (float)e.pixel_max, out.v[6][2] = e.variation, out.v[6][3] = e.lit ? 1.0f : 0.0f;
    out.v[7][0] = (float)(e.seed & 0xffffff), out.v[7][1] = e.ambient, out.v[7][2] = (float)first, out.v[7][3] = (float)count;
}
/* This present's motes for a display w x h: false when there are none (Dust Off, nothing enabled, no camera). */
bool gather(Frame& f, int w, int h)
{
    rood_lights::track_room();
    f.motes = 0;
    f.ids.clear();
    float r[3][3], tr[3], hh, ox, oy, lo[3], hi[3];
    bool camera = w > 0 && h > 0 && pc_scene_camera_get(r, tr, &hh, &ox, &oy);
    pc_scene_bounds(lo, hi);
    memset(&f.gpu, 0, sizeof f.gpu);
    int n = 0, total = 0;
    for (Emitter& e : emitters) {
        e.drawn = 0;
        if (!camera || n >= PC_PARTICLE_EMITTERS || !e.enabled || rood_scene::emitter_hidden_id(e.id))
            continue;
        int count = std::min((int)lroundf((float)e.count * share(e)), PC_PARTICLES_MAX - total);
        if (count <= 0)
            continue;
        pack(e, total, count, lo, hi, f.gpu.emitters[n++]);
        f.ids.push_back(e.id);
        e.drawn = count;
        total += count;
    }
    if (!total)
        return false;
    f.motes = total, f.w = w, f.h = h, f.t = particles_time();
    pc_particles_gpu& g = f.gpu;
    g.info[0] = total, g.info[1] = n, g.info[2] = w, g.info[3] = h;
    for (int row = 0; row < 3; row++) {
        for (int k = 0; k < 3; k++)
            g.rotation[row][k] = r[row][k];
        g.rotation[row][3] = tr[row];
    }
    g.projection[0] = hh * (pc_gte_widescreen ? 0.75f : 1.0f), g.projection[1] = hh, g.projection[2] = ox, g.projection[3] = oy;
    pc_env_gpu env;
    static_assert(sizeof env == sizeof g.env, "pc_particles_gpu env: pc_env_gpu");
    pc_env_gpu_get(&env);
    memcpy(g.env, &env, sizeof env);
    float corner[2] = { 0, 0 }, cell[2] = { 1, 1 };
    f.floor_n = pc_scene_floor_grid(f.heights, corner, cell) ? PC_PARTICLE_FLOOR_N : 0;
    f.floor_info[0] = corner[0], f.floor_info[1] = corner[1];
    f.floor_info[2] = 1.0f / fmaxf(cell[0], 1e-3f), f.floor_info[3] = 1.0f / fmaxf(cell[1], 1e-3f);
    memcpy(g.floor_info, f.floor_info, sizeof f.floor_info);
    g.floor_n[0] = (float)f.floor_n;
    static_assert(sizeof g.floor == sizeof f.heights, "the floor grid's size");
    memcpy(g.floor, f.heights, sizeof f.heights);
    f.lights.resize(PC_LIGHTS_MAX);
    f.light_count = pc_lights_list(f.lights.data(), PC_LIGHTS_MAX);
    float shapes[PC_PARTICLE_GLOWS][4];
    int glows = pc_fog_glows(g.glow_pos, g.glow_col, shapes, PC_PARTICLE_GLOWS);
    g.info2[0] = f.light_count, g.info2[1] = glows, g.info2[2] = std::max(1, std::min(4, pc_particles_lights)), g.info2[3] = 0;
    g.time[0] = f.t, g.time[1] = pc_particles_glow, g.time[2] = pc_particles_debug ? 1.0f : 0.0f;
    g.time[3] = pc_particles_glow_shadows ? 1.0f : 0.0f;
    return true;
}

float lum(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
bool blocked(const float o[3], const float d[3], float t)
{
    float hit, centre[3];
    return t > 0 && pc_gi_trace_cpu && pc_gi_trace_cpu(o, d, t, &hit, centre) >= 0;
}
/* One source of a mote's light (particles_main.glsl mote_light the same). */
struct Source {
    float rgb[3], dir[3], reach, weight;
    bool casts;
};
/* The light reaching p: its strongest sources (lights, glows) with a shadow
 * ray each when `trace`, and the sun; `open` the same without shadows.
 * Without a tracer (`trace` false) a source that casts shadows gives none:
 * a mote in light only never glints where it could be in shadow. */
void light_at(const Frame& f, const float p[3], bool trace, float out[3], float open[3])
{
    const pc_particles_gpu& g = f.gpu;
    int keep = g.info2[2], kept = 0;
    Source best[4];
    auto offer = [&](const Source& s) {
        int at = kept < keep ? kept++ : keep;
        if (at == keep && !(s.weight > best[keep - 1].weight))
            return;
        if (at == keep)
            at = keep - 1;
        while (at > 0 && best[at - 1].weight < s.weight)
            best[at] = best[at - 1], --at;
        best[at] = s;
    };
    for (int i = 0; i < f.light_count; i++) {
        const pc_light_gpu& L = f.lights[i];
        float c[3] = { L.pos[0], L.pos[1], L.pos[2] };
        if (L.col[3] > 1.5f) { /* a rect: its nearest point */
            float rel[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
            for (const float* ax : { L.right, L.up }) {
                float a2 = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2];
                float t = a2 > 1e-6f ? fminf(fmaxf((rel[0] * ax[0] + rel[1] * ax[1] + rel[2] * ax[2]) / a2, -1.0f), 1.0f) : 0;
                for (int k = 0; k < 3; k++)
                    c[k] += ax[k] * t;
            }
        }
        float to[3] = { c[0] - p[0], c[1] - p[1], c[2] - p[2] };
        float d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        float l[3] = { to[0] / fmaxf(d, 1e-3f), to[1] / fmaxf(d, 1e-3f), to[2] / fmaxf(d, 1e-3f) };
        float x = fminf(d / fmaxf(L.pos[3], 1e-3f), 1.0f), a = (1 - x * x) * (1 - x * x);
        a *= pc_light_shape(L.col[3], L.pos, L.dir, L.right, L.up, p, l);
        if (!(a > 0))
            continue;
        Source s = { { L.col[0] * a, L.col[1] * a, L.col[2] * a }, { l[0], l[1], l[2] }, d - 2.0f, 0, L.info[0] != 0 };
        s.weight = lum(s.rgb);
        offer(s);
    }
    for (int j = 0; j < g.info2[1]; j++) {
        const float* gp = g.glow_pos[j];
        float to[3] = { gp[0] - p[0], gp[1] - p[1], gp[2] - p[2] };
        float d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        float x = fminf(d / fmaxf(gp[3], 1e-3f), 1.0f), a = (1 - x * x) * (1 - x * x) * g.time[1];
        if (!(a > 0))
            continue;
        Source s = { { g.glow_col[j][0] * a, g.glow_col[j][1] * a, g.glow_col[j][2] * a },
            { to[0] / fmaxf(d, 1e-3f), to[1] / fmaxf(d, 1e-3f), to[2] / fmaxf(d, 1e-3f) },
            d - fmaxf(fabsf(g.glow_col[j][3]), 2.0f), 0, g.time[3] > 0.5f };
        s.weight = lum(s.rgb);
        offer(s);
    }
    for (int k = 0; k < 3; k++)
        out[k] = open[k] = 0;
    for (int i = 0; i < kept; i++) {
        bool lit = !(best[i].casts && (!trace || blocked(p, best[i].dir, best[i].reach)));
        for (int k = 0; k < 3; k++)
            open[k] += best[i].rgb[k], out[k] += lit ? best[i].rgb[k] : 0;
    }
    const float* sun = g.env[1];
    if (sun[0] + sun[1] + sun[2] > 0) { /* environment.glsl env_sun_on */
        bool lit = !(sun[3] > 0.5f && (!trace || blocked(p, g.env[0], 30000.0f))); /* ENV_SUN_DISTANCE */
        for (int k = 0; k < 3; k++)
            open[k] += sun[k], out[k] += lit ? sun[k] : 0;
    }
}
/* A mote's colour from the light reaching it (rgb), its light's luminance (a). */
void colour_of(const pc_emitter_gpu& e, const pc_mote& m, const float light[3], bool debug, float out[4])
{
    float l[3] = { light[0], light[1], light[2] };
    if (debug)
        l[0] = l[1] = l[2] = 1;
    if (e.v[6][3] > 0.5f) /* lit: its ambient everywhere */
        for (int k = 0; k < 3; k++)
            l[k] += e.v[7][1];
    for (int k = 0; k < 3; k++)
        out[k] = l[k] * e.v[5][k] * e.v[5][3] * m.fade * m.gain;
    out[3] = lum(l);
}
/* A coloured mote into the key image. */
void splat(std::vector<uint32_t>& keys, int w, int h, const pc_particles_gpu& g, const pc_mote& m, int index)
{
    float s[3];
    if (!pc_particle_project(g.rotation, g.projection, m.pos, s))
        return;
    uint32_t key = pc_particle_key(s[2], index);
    int ax = pc_particle_anchor(s[0], m.size), ay = pc_particle_anchor(s[1], m.size);
    for (int dy = 0; dy < m.size; dy++)
        for (int dx = 0; dx < m.size; dx++) {
            int x = ax + dx, y = ay + dy;
            if (x >= 0 && y >= 0 && x < w && y < h) {
                uint32_t& k = keys[(size_t)y * w + x];
                k = key < k ? key : k;
            }
        }
}
/* OpenGL's buffers and its per-mote light cache (refreshed round-robin within the ray budget). */
std::vector<uint32_t> cpu_keys;
std::vector<float> cpu_colours, cached;
std::vector<int> cached_cycle;
int cursor;
} // namespace

extern "C" int pc_particles_cpu_frame(int w, int h, const uint32_t** keys, const float** colours, int* count)
{
    auto start = std::chrono::steady_clock::now();
    if (!gather(frame, w, h)) {
        stats.motes = 0, stats.path = "off";
        return 0;
    }
    const pc_particles_gpu& g = frame.gpu;
    bool trace = !pc_particles_untraced && pc_gi_scene_cpu && pc_gi_trace_cpu && pc_gi_scene_cpu();
    int n = frame.motes;
    cpu_keys.assign((size_t)w * h, PC_PARTICLE_NONE);
    cpu_colours.assign((size_t)n * 4, 0.0f);
    if ((int)cached_cycle.size() != n)
        cached.assign((size_t)n * 3, 0.0f), cached_cycle.assign(n, INT_MIN), cursor = 0;
    /* each mote's light costs one ray per source and one for the sun */
    int per = trace ? std::max(1, std::min(n, pc_particles_cpu_rays / (g.info2[2] + 1))) : n;
    int from = cursor % n;
    bool debug = g.time[2] > 0.5f;
    for (int e = 0; e < g.info[1]; e++) {
        const pc_emitter_gpu& em = g.emitters[e];
        int first = (int)em.v[7][2], motes = (int)em.v[7][3];
        for (int i = 0; i < motes; i++) {
            int gi = first + i;
            pc_mote m;
            pc_particle_mote(&em, i, frame.t, frame.floor_n ? frame.heights : nullptr, frame.floor_info, frame.floor_n, &m);
            float* light = &cached[(size_t)gi * 3];
            if ((gi - from + n) % n < per) { /* due */
                float open[3];
                light_at(frame, m.pos, trace, light, open);
                cached_cycle[gi] = m.cycle;
            } else if (cached_cycle[gi] != m.cycle) { /* respawned: dark until it is lit at its new place */
                light[0] = light[1] = light[2] = 0;
                cached_cycle[gi] = m.cycle;
            }
            float* c = &cpu_colours[(size_t)gi * 4];
            colour_of(em, m, light, debug, c);
            if (c[0] + c[1] + c[2] > 1e-4f)
                splat(cpu_keys, w, h, g, m, gi);
        }
    }
    cursor = (from + per) % n;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stats.cpu_ms = stats.cpu_ms > 0 ? stats.cpu_ms * 0.9 + ms * 0.1 : ms;
    perf_add(start);
    stats.motes = n, stats.path = trace ? "traced" : "untraced", stats.scene = trace ? "cpu" : "none";
    *keys = cpu_keys.data(), *colours = cpu_colours.data(), *count = n;
    return 1;
}
extern "C" int pc_particles_gpu_frame(int w, int h, pc_particles_gpu* out, pc_light_gpu* lights, int max, int* light_count)
{
    auto start = std::chrono::steady_clock::now();
    if (!gather(frame, w, h)) {
        stats.motes = 0, stats.path = "off";
        return 0;
    }
    *out = frame.gpu;
    int n = std::min(frame.light_count, max);
    memcpy(lights, frame.lights.data(), (size_t)n * sizeof(pc_light_gpu));
    *light_count = n;
    out->info2[0] = n;
    stats.motes = frame.motes;
    perf_add(start); /* gathering the records (the pass itself: the GPU section "particles") */
    return frame.motes;
}
extern "C" int pc_particles_cpu_perf(double* ms, int reset)
{
    int frames = perf_frames;
    *ms = frames ? perf_sum / frames : 0.0;
    if (reset)
        perf_sum = 0, perf_frames = 0;
    return frames;
}
extern "C" void pc_particles_gpu_traced(int traced)
{
    stats.path = traced ? "traced" : "untraced";
    stats.scene = traced == 1 ? "gi" : traced == 2 ? "own" : "none";
}

/* ---- the room's emitters (particles_internal.h) ---- */

void rood_particles::reset() { emitters.clear(); }
bool rood_particles::apply_item(const lighting::Item& it)
{
    if (it.kind != "emitter")
        return false;
    Emitter e;
    e.id = it.name;
    if (const std::string* n = it.get("name"))
        e.name = *n;
    if (const std::string* a = it.get("asset"))
        e.asset = *a;
    const Asset* a = asset_of(e);
    if (!e.asset.empty() && !a) {
        fprintf(stderr, "[particles] emitter %s: unknown asset '%s' (its fields as written)\n", e.id.c_str(), e.asset.c_str());
        for (const auto& f : it.fields)
            if (lighting::preset_field("emitter", f.first))
                e.written.push_back(f.first);
    }
    auto get = [&](const char* key) { return item_field(it, a, key); };
    e.enabled = atoi(get("enabled").c_str()) != 0;
    sscanf(get("pos").c_str(), "%f,%f,%f", &e.pos[0], &e.pos[1], &e.pos[2]);
    e.yaw = (float)atof(get("yaw").c_str());
    e.seed = std::max(1, atoi(get("seed").c_str()));
    from_fields(e, get);
    int i = index_of_id(e.id);
    if (i >= 0)
        emitters[i] = e;
    else
        emitters.push_back(e);
    return true;
}
/* The assets read again and every emitter resolved again from them: what each
 * item writes stays, the rest follows its (new) asset. */
void rood_particles::reload()
{
    std::vector<lighting::Item> items;
    for (Emitter& e : emitters)
        items.push_back(item_of(e)); /* against the assets they were resolved from */
    reload_assets();
    for (const lighting::Item& it : items)
        apply_item(it);
}
void rood_particles::add_items(lighting::State& s)
{
    for (Emitter& e : emitters) {
        lighting::Item it = item_of(e);
        s[it.id()] = it;
    }
}
int rood_particles::emitter_count() { return (int)emitters.size(); }
std::string rood_particles::emitter_id(int index)
{
    if (index < 0 || index >= (int)emitters.size())
        return "";
    if (emitters[index].id.empty())
        emitters[index].id = lighting::new_name();
    return "emitter:" + emitters[index].id;
}
int rood_particles::emitter_index(const std::string& item_id)
{
    if (item_id.compare(0, 8, "emitter:") || item_id.size() <= 8)
        return -1;
    return index_of_id(item_id.substr(8));
}
std::string rood_particles::emitter_label(int index)
{
    if (index < 0 || index >= (int)emitters.size())
        return "";
    const Emitter& e = emitters[index];
    const Asset* a = asset_of(e);
    std::string what = !e.name.empty() ? e.name : a ? a->name : "Emitter";
    return "Emitter " + std::to_string(index + 1) + ": " + what;
}
bool rood_particles::emitter_centre(int index, float out[3], float* radius)
{
    if (index < 0 || index >= (int)emitters.size())
        return false;
    const Emitter& e = emitters[index];
    memcpy(out, e.pos, sizeof e.pos);
    *radius = e.shape == 2 || e.shape == 3 ? fmaxf(e.radius, 32)
                                           : 0.5f * sqrtf(e.size[0] * e.size[0] + e.size[1] * e.size[1] + e.size[2] * e.size[2]);
    return true;
}
bool rood_particles::emitter_size(int index, float out[3])
{
    if (index < 0 || index >= (int)emitters.size())
        return false;
    memcpy(out, emitters[index].size, sizeof emitters[index].size);
    return true;
}
void rood_particles::set_emitter_size(int index, const float size[3])
{
    if (index < 0 || index >= (int)emitters.size())
        return;
    for (int k = 0; k < 3; k++)
        emitters[index].size[k] = fmaxf(size[k], 2);
    pc_lights_mark_dirty();
}
lighting::Item rood_particles::emitter_item(int index) { return item_of(emitters[index]); }
int rood_particles::add_emitter_item(const lighting::Item& item, const float offset[3])
{
    if (item.kind != "emitter")
        return -1;
    lighting::Item copy = item;
    copy.name = lighting::new_name();
    if (!apply_item(copy))
        return -1;
    Emitter& e = emitters.back();
    for (int k = 0; k < 3; k++)
        e.pos[k] += offset[k];
    return (int)emitters.size() - 1;
}
void rood_particles::remove_emitter(int index)
{
    if (index >= 0 && index < (int)emitters.size())
        emitters.erase(emitters.begin() + index);
}
int rood_particles::add_emitter(const std::string& asset, const float pos[3])
{
    lighting::Item it;
    it.kind = "emitter", it.name = lighting::new_name();
    if (!asset.empty())
        it.set("asset", asset);
    it.set("pos", vec3(pos, 1));
    it.set("seed", std::to_string(1 + (int)(prefab::hash32((uint32_t)emitters.size() * 977u + 13u) % 999999u)));
    apply_item(it);
    int i = (int)emitters.size() - 1;
    rood_selection::set(rood_scene::emitter_ref(i), rood_selection::FROM_CODE);
    rood_scene::publish();
    pc_lights_mark_dirty();
    return i;
}
std::string rood_particles::field(int index, const char* key)
{
    lighting::Item it = item_of(emitters[index]);
    return item_field(it, asset_of(emitters[index]), key);
}
void rood_particles::set_field(int index, const char* key, const std::string& value)
{
    lighting::Item it = item_of(emitters[index]);
    it.set(key, canonical(key, value));
    apply_item(it);
    pc_lights_mark_dirty();
}
void rood_particles::clear_field(int index, const char* key)
{
    lighting::Item it = item_of(emitters[index]);
    it.erase(key);
    apply_item(it);
    pc_lights_mark_dirty();
}
std::string rood_particles::asset_value(int index, const char* key)
{
    const Asset* a = asset_of(emitters[index]);
    const std::string* v = a ? a->get(key) : nullptr;
    return v ? *v : std::string(lighting::field_default("emitter", key));
}
/* An item writes an asset field only where it differs from its asset (item_of; without an asset, from the default). */
bool rood_particles::overrides(int index, const char* key)
{
    return lighting::preset_field("emitter", key) && item_of(emitters[index]).get(key) != nullptr;
}
rood_particles::Own rood_particles::own(int index)
{
    const Emitter& e = emitters[index];
    Own o;
    o.name = e.name, o.asset = e.asset, o.enabled = e.enabled, o.yaw = e.yaw, o.seed = e.seed;
    memcpy(o.pos, e.pos, sizeof o.pos);
    return o;
}
void rood_particles::set_own(int index, const Own& o)
{
    lighting::Item it = item_of(emitters[index]);
    if (o.asset != emitters[index].asset) { /* a new asset: the fields it writes stay, the rest follow the new one */
        it.erase("asset");
        if (!o.asset.empty())
            it.set("asset", o.asset);
    }
    it.erase("name");
    if (!o.name.empty())
        it.set("name", o.name);
    it.set("pos", vec3(o.pos, 1));
    it.set("yaw", num(o.yaw, 2));
    it.set("enabled", o.enabled ? "1" : "0");
    it.set("seed", std::to_string(std::max(1, o.seed)));
    lighting::canonicalize(it);
    apply_item(it);
    pc_lights_mark_dirty();
}
bool rood_particles::emitters_shown() { return show_emitters; }
bool* rood_particles::emitters_shown_flag() { return &show_emitters; }
int rood_particles::drawn(int index) { return index >= 0 && index < (int)emitters.size() ? emitters[index].drawn : 0; }

/* ---- RPC particles ---- */

namespace {
using prefab::Json;
std::string jstr(const std::string& s) { return prefab::json_quote(s); }
std::string jvec(const float* v, int n)
{
    std::string s = "[";
    for (int i = 0; i < n; i++)
        s += (i ? "," : "") + num(v[i], 3);
    return s + "]";
}
/* A JSON value as lighting text (numbers, vectors, words, booleans). */
std::string text_of(const Json& j)
{
    if (j.type == Json::String)
        return j.string;
    if (j.type == Json::Bool)
        return j.boolean ? "1" : "0";
    if (j.type == Json::Number)
        return num(j.number, 6);
    std::string s;
    for (size_t i = 0; i < j.items.size(); i++)
        s += (i ? "," : "") + num(j.items[i].number, 6);
    return s;
}
std::string bare(const std::string& id) { return id.compare(0, 8, "emitter:") ? id : id.substr(8); }
std::string emitter_json(int i)
{
    const Emitter& e = emitters[i];
    lighting::Item it = item_of(emitters[i]);
    const Asset* a = asset_of(e);
    std::string fields = "{";
    bool first = true;
    for (const auto& f : asset_fields(e)) {
        fields += (first ? "" : ",") + jstr(f.first) + ":" + jstr(item_field(it, a, f.first.c_str()));
        first = false;
    }
    return "{\"id\":" + jstr(e.id) + ",\"ref\":" + jstr("emitter:" + e.id) + ",\"name\":" + jstr(e.name)
        + ",\"asset\":" + jstr(e.asset) + ",\"enabled\":" + (e.enabled ? "true" : "false") + ",\"pos\":[" + num(e.pos[0], 1)
        + "," + num(e.pos[1], 1) + "," + num(e.pos[2], 1) + "],\"yaw\":" + num(e.yaw, 2) + ",\"seed\":" + std::to_string(e.seed)
        + ",\"shape\":" + jstr(SHAPES[e.shape]) + ",\"size\":" + jvec(e.size, 3) + ",\"count\":" + std::to_string(e.count)
        + ",\"drawn\":" + std::to_string(e.drawn) + ",\"fields\":" + fields + "}}";
}
} // namespace

/* params: level (dust 0..2), low (dust_low), time (seconds; -1 live), debug,
 * untraced (tests: no tracer, as on a device without one), show (Show > Emitters),
 * settings {glow, lights, glow_shadows, cpu_rays}, reload_assets (and every emitter
 * resolved again), emitters [{id (existing: changed; else added), remove, name, asset,
 * pos, yaw, enabled, seed, or any asset field}], add {asset, pos} or {asset, here: true}
 * (a new emitter, selected: "added"; here: as Details > Atmosphere's Add emitter, on the
 * surface under the view's centre), select (an id: the editor's selection), motes
 * {emitter, offset, max}. Everything is checked first: an error changes nothing. The
 * reply: the settings, the path ("traced", "untraced", "off") and its scene ("gi",
 * "own", "cpu", "none"), the motes drawn, cpu_ms, the view, the assets, the emitters
 * (each with its resolved fields). */
extern "C" int pc_particles_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_lights::track_room();
    /* Everything is checked before anything changes: an error leaves the room as it was. */
    const Json* edits = params.get("emitters");
    if (edits)
        for (const Json& o : edits->items)
            for (const auto& m : o.members)
                if (m.first != "id" && m.first != "remove" && !lighting::known_field("emitter", m.first)) {
                    snprintf(out, size, "emitters: no field %s (lighting/README.md \"Emitters\")", m.first.c_str());
                    return -1;
                }
    const Json* add = params.get("add");
    const Json* here = add ? add->get("here") : nullptr;
    bool add_here = here && (here->type == Json::Bool ? here->boolean : here->number != 0);
    if (add && !add_here) {
        const Json* p = add->get("pos");
        if (!p || p->items.size() != 3) {
            snprintf(out, size, "add needs pos [x, y, z] (or here: true)");
            return -1;
        }
    }
    bool changed = false;
    if (const Json* j = params.get("level"))
        pc_dust = std::max(0, std::min(2, (int)j->number));
    if (const Json* j = params.get("low"))
        pc_dust_low = fminf(fmaxf((float)j->number, 0), 1);
    if (const Json* j = params.get("time"))
        pc_particles_time = (float)j->number;
    if (const Json* j = params.get("debug"))
        pc_particles_debug = j->type == Json::Bool ? j->boolean : j->number != 0;
    if (const Json* j = params.get("untraced"))
        pc_particles_untraced = j->type == Json::Bool ? j->boolean : j->number != 0;
    if (const Json* st = params.get("settings")) {
        if (const Json* j = st->get("glow"))
            pc_particles_glow = fmaxf((float)j->number, 0);
        if (const Json* j = st->get("lights"))
            pc_particles_lights = std::max(1, std::min(4, (int)j->number));
        if (const Json* j = st->get("glow_shadows"))
            pc_particles_glow_shadows = j->type == Json::Bool ? j->boolean : j->number != 0;
        if (const Json* j = st->get("cpu_rays"))
            pc_particles_cpu_rays = std::max(16, (int)j->number);
    }
    if (const Json* j = params.get("reload_assets"))
        if (j->type == Json::Bool ? j->boolean : j->number != 0)
            rood_particles::reload();
    std::string extra;
    if (edits)
        for (const Json& o : edits->items) {
            const Json* id = o.get("id");
            std::string name = id ? bare(id->string) : lighting::new_name();
            int i = index_of_id(name);
            const Json* rm = o.get("remove");
            if (rm && (rm->type == Json::Bool ? rm->boolean : rm->number != 0)) {
                if (i >= 0) {
                    emitters.erase(emitters.begin() + i);
                    changed = true;
                }
                continue;
            }
            lighting::Item it;
            if (i >= 0)
                it = item_of(emitters[i]);
            else
                it.kind = "emitter", it.name = name;
            for (const auto& m : o.members) {
                if (m.first == "id" || m.first == "remove")
                    continue;
                it.set(m.first.c_str(), canonical(m.first.c_str(), text_of(m.second)));
            }
            if (!it.get("pos"))
                it.set("pos", "0,0,0");
            rood_particles::apply_item(it);
            changed = true;
        }
    if (const Json* j = params.get("show"))
        *rood_particles::emitters_shown_flag() = j->type == Json::Bool ? j->boolean : j->number != 0;
    if (add) {
        const Json* a = add->get("asset");
        std::string asset = a ? a->string : "dust_motes";
        int i = -1;
        if (add_here) {
            i = rood_particles::add_here(asset);
        } else {
            const Json* p = add->get("pos");
            float pos[3] = { 0, 0, 0 };
            for (int k = 0; k < 3; k++)
                pos[k] = (float)p->items[k].number;
            i = rood_particles::add_emitter(asset, pos);
            pc_undo_hint("Add emitter");
        }
        extra += ",\"added\":" + (i >= 0 ? jstr(emitters[i].id) : std::string("null"));
        changed |= i >= 0;
    }
    if (const Json* sel = params.get("select")) {
        int i = index_of_id(bare(sel->string));
        rood_selection::set(i >= 0 ? rood_scene::emitter_ref(i) : rood_selection::Ref(), rood_selection::FROM_RPC);
        rood_scene::publish();
    }
    if (const Json* mq = params.get("motes")) {
        /* The last frame's motes (the time frozen: exactly what it drew): where each is, its
         * game pixels, its light as drawn (OpenGL: the CPU path's; Vulkan: read back) and
         * without shadows (the CPU's), and whether it owns its anchor pixel. */
        const Json* who = mq->get("emitter");
        const Json* lim = mq->get("max");
        const Json* off = mq->get("offset");
        int limit = std::min(lim ? (int)lim->number : 256, 600), skip = off ? (int)off->number : 0; /* 600: the reply's 256 KB */
        std::vector<uint32_t> keys;
        std::vector<float> colours;
        bool vulkan = &pc_vk_active && pc_vk_active;
        if (frame.motes > 0) {
            if (vulkan && pc_vk_particles_read) {
                keys.resize((size_t)frame.w * frame.h), colours.resize((size_t)frame.motes * 4);
                if (!pc_vk_particles_read(keys.data(), frame.w, frame.h, colours.data(), frame.motes))
                    keys.clear(), colours.clear();
            } else if (!vulkan && cpu_keys.size() == (size_t)frame.w * frame.h) {
                keys = cpu_keys, colours = cpu_colours;
            }
        }
        std::string list = ",\"mote_list\":[";
        int listed = 0;
        const pc_particles_gpu& g = frame.gpu;
        for (int e = 0; e < g.info[1] && frame.motes > 0; e++) {
            if (who && bare(who->string) != frame.ids[e])
                continue;
            const pc_emitter_gpu& em = g.emitters[e];
            int first = (int)em.v[7][2], count = (int)em.v[7][3];
            for (int i = 0; i < count && listed < limit; i++) {
                if (skip > 0) {
                    --skip;
                    continue;
                }
                int gi = first + i;
                pc_mote m;
                pc_particle_mote(&em, i, frame.t, frame.floor_n ? frame.heights : nullptr, frame.floor_info, frame.floor_n, &m);
                float lit[3], open[3], s[3] = { -1, -1, 0 };
                light_at(frame, m.pos, false, lit, open);
                bool seen = pc_particle_project(g.rotation, g.projection, m.pos, s);
                int ax = seen ? pc_particle_anchor(s[0], m.size) : -1, ay = seen ? pc_particle_anchor(s[1], m.size) : -1;
                bool won = seen && ax >= 0 && ay >= 0 && ax < frame.w && ay < frame.h && !keys.empty()
                    && (keys[(size_t)ay * frame.w + ax] & 0xffffu) == (uint32_t)gi;
                const float* c = colours.empty() ? nullptr : &colours[(size_t)gi * 4];
                char text[512];
                snprintf(text, sizeof text,
                    "%s{\"g\":%d,\"emitter\":\"%s\",\"i\":%d,\"cycle\":%d,\"pos\":[%.3f,%.3f,%.3f],\"screen\":[%.3f,%.3f],"
                    "\"cell\":[%d,%d],\"size\":%d,\"sz\":%.2f,\"fade\":%.4f,\"colour\":[%.5f,%.5f,%.5f],\"light\":%.5f,"
                    "\"open\":%.5f,\"won\":%s}",
                    listed ? "," : "", gi, frame.ids[e].c_str(), i, m.cycle, m.pos[0], m.pos[1], m.pos[2], s[0], s[1], ax, ay,
                    m.size, s[2], m.fade, c ? c[0] : 0, c ? c[1] : 0, c ? c[2] : 0, c ? c[3] : 0, lum(open),
                    won ? "true" : "false");
                list += text;
                ++listed;
            }
        }
        extra += list + "]";
    }
    extra += ",\"display\":[" + std::to_string(frame.w) + "," + std::to_string(frame.h) + "]";
    if (&pc_gl_view)
        extra += ",\"view\":[" + std::to_string(pc_gl_view[0]) + "," + std::to_string(pc_gl_view[1]) + ","
            + std::to_string(pc_gl_view[2]) + "," + std::to_string(pc_gl_view[3]) + "]";
    if (changed) {
        pc_lights_mark_dirty();
        rood_scene::refresh();
    }
    std::string s = "{\"level\":" + std::to_string(pc_dust) + ",\"low\":" + num(pc_dust_low, 3) + ",\"time\":"
        + num(pc_particles_time, 3) + ",\"debug\":" + std::to_string(pc_particles_debug) + ",\"settings\":{\"glow\":"
        + num(pc_particles_glow, 3) + ",\"lights\":" + std::to_string(pc_particles_lights) + ",\"glow_shadows\":"
        + std::to_string(pc_particles_glow_shadows) + ",\"cpu_rays\":" + std::to_string(pc_particles_cpu_rays)
        + "},\"renderer\":\"" + (&pc_vk_active && pc_vk_active ? "vulkan" : "gl") + "\",\"path\":\"" + stats.path
        + "\",\"scene\":\"" + stats.scene + "\",\"untraced\":" + std::to_string(pc_particles_untraced) + ",\"motes\":" + std::to_string(stats.motes) + ",\"cpu_ms\":" + num(stats.cpu_ms, 4) + ",\"assets_status\":"
        + jstr(rood_particles::assets_status()) + ",\"assets\":[";
    const auto& list = rood_particles::assets();
    for (size_t i = 0; i < list.size(); i++) {
        std::string tags = "[";
        for (size_t t = 0; t < list[i].tags.size(); t++)
            tags += (t ? "," : "") + jstr(list[i].tags[t]);
        s += std::string(i ? "," : "") + "{\"id\":" + jstr(list[i].id) + ",\"name\":" + jstr(list[i].name)
            + ",\"category\":" + jstr(list[i].category) + ",\"icon\":" + jstr(list[i].icon) + ",\"tags\":" + tags + "]}";
    }
    s += "],\"emitters\":[";
    for (int i = 0; i < (int)emitters.size(); i++)
        s += (i ? "," : "") + emitter_json(i);
    s += "]" + extra + "}";
    if ((int)s.size() >= size) {
        snprintf(out, size, "reply too large");
        return -1;
    }
    snprintf(out, size, "%s", s.c_str());
    return 0;
}
