// Dynamic lights for the hardware renderer, per room, with an overlay editor.
//
// Lights live in world space (the room capture's space, as Ashley's position).
// A room's lights, light actors (prefab instances, light_prefabs.h) and
// overrides come from its lighting layers (lighting_layers.h: the packs in
// lighting/ and the user's pc/user/lighting; format in lighting/README.md),
// merged by id when the room is entered; Save area writes the user layer
// (lighting_editor.cpp). Colours are linear 0..1; intensity scales them. The
// present shader adds albedo * sum(light) on top of the game's baked
// lighting (scaled by `baked`). "Suggest from vertex colours" dumps the
// captured room and runs pc/tools/estimate_lights.py, which writes the old
// one-line light format read here.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include "pc_paths.h"
#include "light_list.h"
#include "window_pattern.h"
#include "emissive.h"
#include "emissive_internal.h"
#include "light_shape.h"
#include <vector>

#include "imgui.h"
#include "imgui_internal.h" /* the hovered window, for drops on the view */
#include "ui/ui_shell.h" /* pc_ui_shell_wants_mouse */
#include "dev_state.h"
#include "ImGuizmo.h"
#include "editor_overlays.h"
#include "probe_gi.h"
bool pc_atmosphere_viewport(const float view[16], const float projection[16], const float rect[4], void* draw_list);
bool pc_particles_viewport(const float view[16], const float projection[16], const float rect[4], void* draw_list);
#include "fixture_catalog.h"
#include "editor_fit_window.h"
#include "editor_library.h"
#include "lights_internal.h"
#include "lighting_quality.h"
#include "editor_undo.h"
#include "editor_scene.h"
#include "editor_selection.h"
#include "editor_math.h"
#include "editor_transform.h"
#include "editor_handles.h"
#include "editor_colour.h"
#include "environment_internal.h"
#include "particles_internal.h"

extern "C" {
int pc_scene_camera_get(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy);
int pc_scene_camera_inverse(float inverse[3][3]);
int pc_scene_dump(const char* path);
int pc_scene_triangles(void);
int pc_hw_pick(float vram_x, float vram_y, float* sz, float normal[3]);
extern int pc_gl_view[4], pc_gl_display[4];
extern int pc_gte_widescreen;
}

namespace rood_lights {
std::vector<Light> lights;
std::vector<prefab::Actor> actors;
int zone = -1, room = -1;
int selected = -1;
int selected_actor = -1;
std::vector<int> multi;
int solo = -1, solo_actor = -1; // only this light / actor lights the scene (editor solo)
int gizmo_operation; // 0 move (W), 1 rotate (E: aims spot and rect lights, turns actors), 2 scale (R: shape only)
bool glow_selected; // a glow (game emitter) is selected instead of a light
uint32_t glow_id; // its id (glows.c: stable while in the room)
float glow_at[3]; // its position (follows the glow)
bool dirty;
char status[256];
std::vector<Ghost> ghosts;
std::string place_item = "light:point";
int animate = 1;
} // namespace rood_lights
using namespace rood_lights;

namespace {
/* Inspector probe: a surface point picked in the view. */
bool probe_mode, probe_valid, probe_game_view, probe_gi_ok;
float probe_world[3], probe_normal[3], probe_uv[2], probe_gi[8];
struct Source {
    char name[48];
    float position[3], color[3];
    float weight; // falloff times N.L at the probe
    float distance;
    int blocker; // -1 clear, else hit flags (8: character or prop)
    float hit_t, hit[3];
};
std::vector<Source> probe_sources;
} // namespace

bool pc_lights_show_gizmos = true; /* light and glow icons in the view (settings editor_show_lights) */
extern "C" float pc_shadow_softness;
extern "C" int pc_drop_shadow_mode __attribute__((weak)); // battle builds (actor_shadow.c)
extern "C" float pc_normal_smoothing;
extern "C" {
int pc_lights_shadows_active; /* a shadow-casting light is lit this frame */
}
extern "C" {
int pc_lights_enabled = 1;
float pc_lights_baked = 1.0f; // scale on the game's baked lighting
float pc_lights_master = 1.0f;
}

#include "glow_overrides.h"
#include "room_effects.h"
#include "scene_capture.h"
extern "C" int pc_gi_probe(float u, float v, float out[8]) __attribute__((weak));
extern "C" int pc_gi_trace_cpu(const float o[3], const float d[3], float tmax, float* t_out, float centre[3])
    __attribute__((weak));
extern "C" void pc_room_settings_reset(void);
extern "C" int pc_room_settings_parse(const char* line);
extern "C" void pc_room_settings_write(FILE* f);
extern "C" int pc_room_settings_overrides(void);
static void load_room(int z, int r)
{
    lights.clear();
    actors.clear();
    ghosts.clear();
    place_drag_cancel();
    select_none();
    solo = solo_actor = -1;
    dirty = false;
    pc_undo_reset(); /* the history belongs to the room's files as loaded */
    pc_room_settings_reset();
    pc_gi_glows_reset();
    pc_uv_effects_reset();
    layers_load(z, r); /* lighting_editor.cpp: every layer, merged */
}

static void save_room() { layers_save(); }

/* "Suggest from vertex colours": the fit (estimate_lights.py, ~1-2 s) runs
 * on a worker thread so the game keeps running; finish_suggest() adds its
 * lights on a later frame, unless the room changed meanwhile. */
static std::atomic<int> suggest_state { 0 }; /* 0 idle, 1 running, 2 done ok, 3 failed */
static int suggest_zone = -1, suggest_room = -1;
static std::string suggest_out;

static void finish_suggest()
{
    int state = suggest_state.load();
    if (state < 2)
        return;
    suggest_state = 0;
    if (state == 3) {
        snprintf(status, sizeof status, "estimate_lights.py failed (python3 with numpy/scipy?)");
        return;
    }
    if (suggest_zone != zone || suggest_room != room) {
        snprintf(status, sizeof status, "Suggestion discarded: the room changed");
        return;
    }
    // Read suggestions through the room loader's parser.
    std::vector<Light> keep = lights;
    FILE* f = fopen(suggest_out.c_str(), "r");
    if (!f)
        return;
    char line[512];
    int added = 0;
    while (fgets(line, sizeof line, f)) {
        Light l;
        char type[16];
        if (sscanf(line, "light %15s %f %f %f %f %f %f %f %f", type, &l.position[0], &l.position[1],
                &l.position[2], &l.color[0], &l.color[1], &l.color[2], &l.intensity, &l.radius)
            == 9) {
            keep.push_back(l);
            ++added;
        }
    }
    fclose(f);
    lights = keep;
    dirty = true;
    snprintf(status, sizeof status, "Added %d suggested lights (unsaved)", added);
}

static void suggest(bool wait = false)
{
    if (suggest_state.load() == 1) {
        if (wait)
            while (suggest_state.load() == 1)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        finish_suggest();
        return;
    }
    finish_suggest();
    if (!pc_scene_triangles()) {
        snprintf(status, sizeof status, "No room geometry captured yet");
        return;
    }
    std::string scene = pc_out_path("lights-scene.obj");
    suggest_out = pc_out_path("lights-suggested.txt");
    if (pc_scene_dump(scene.c_str())) {
        snprintf(status, sizeof status, "Scene dump failed");
        return;
    }
    // One BLAS thread: the fit's matrices are small, and NumPy's default
    // thread pool made it ~3x slower while saturating every core.
    std::string command = "OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 python3 "
                          "pc/tools/estimate_lights.py '"
        + scene + "' --rood '" + suggest_out + "' > /dev/null";
    suggest_zone = zone, suggest_room = room;
    suggest_state = 1;
    snprintf(status, sizeof status, "Suggesting lights from vertex colours...");
    // Detached: a joinable static std::thread would abort the game at exit
    // if a fit were still running.
    std::thread([command] { suggest_state = system(command.c_str()) == 0 ? 2 : 3; }).detach();
    if (wait) {
        while (suggest_state.load() == 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        finish_suggest();
    }
}

/* Follow room changes (called every presented frame). */
void rood_lights::track_room()
{
    if (!pc_dev_state) {
        finish_suggest();
        return;
    }
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    pc_dev_state(&s);
    if (s.zone != zone || s.room != room) {
        if (dirty && zone >= 0)
            layers_stash(); /* unsaved edits survive walking out and back in */
        zone = s.zone;
        room = s.room;
        load_room(zone, room);
    }
    ensure_ids(); /* new and copied lights and actors get their ids at once */
    finish_suggest(); /* after the room check: a fit for the old room is discarded */
}

/* ---- animation time and selection ---- */

/* Light animation is a function of this time: sampled once per presented
 * frame (gpu_hw.c calls pc_lights_new_frame after each present), so every
 * renderer pass of a frame (present shader, path tracer, haze) sees the
 * same lights; RPC actors {time} fixes it. */
static bool frame_time_stale = true;
static double frame_time, fixed_time = -1, last_sample;
extern "C" void pc_lights_frame_tick(void);
extern "C" void pc_lights_new_frame(void)
{
    frame_time_stale = true;
    pc_lights_frame_tick();
}
extern "C" void pc_lights_set_time(double t) { fixed_time = t; } /* < 0: the clock */
extern "C" double pc_lights_time(void) { return rood_lights::light_time(); }
double rood_lights::light_time()
{
    if (fixed_time >= 0)
        return fixed_time;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = now.tv_sec + now.tv_nsec * 1e-9;
    /* A path that never presents (or a long pause) still moves on. */
    if (frame_time_stale || t - last_sample > 0.1) {
        frame_time = t;
        frame_time_stale = false;
    }
    last_sample = t;
    return frame_time;
}

/* The selection lives in editor_selection.h (one set of typed references the
 * viewport, Outliner, Details, undo and the RPC share); these keep their old
 * meaning by replacing it, and editor_scene.cpp's publish keeps selected,
 * selected_actor, multi and the glow's state in step with it. */
void rood_lights::select_none()
{
    rood_selection::clear(rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_light(int index)
{
    rood_selection::set(rood_scene::light_ref(index), rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_actor(int index)
{
    rood_selection::set(rood_scene::actor_ref(index), rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_glow(const float position[3])
{
    uint32_t id = pc_gi_glow_near(position, 96);
    rood_selection::set(id ? rood_scene::glow_ref(id) : rood_scene::glow_ref_key("", position), rood_selection::FROM_CODE);
    rood_scene::publish();
}
void rood_lights::select_glow_id(uint32_t id)
{
    rood_selection::Ref ref = rood_scene::glow_ref(id);
    if (!ref)
        return;
    rood_selection::set(ref, rood_selection::FROM_CODE);
    rood_scene::publish();
}
const char* rood_lights::light_type_name(int type)
{
    return type == 1 ? "Spot light" : type == 2 ? "Rect light" : "Point light";
}
void rood_lights::actor_lights(const prefab::Actor& a, std::vector<prefab::EvalLight>& out, bool animated)
{
    out.clear();
    const prefab::Prefab* p = prefab::library().find(a.prefab);
    if (p)
        prefab::evaluate(*p, a, light_time(), animated && animate, out);
}

/* RPC actors {action "sample"}: an actor's lights at time t (seconds),
 * animated as they would be then, as JSON. */
extern "C" int pc_lights_sample_actor(int index, double t, char* out, int size)
{
    if (index < 0 || index >= (int)actors.size())
        return -1;
    const prefab::Prefab* p = prefab::library().find(actors[index].prefab);
    if (!p)
        return -1;
    std::vector<prefab::EvalLight> ls;
    prefab::evaluate(*p, actors[index], t, true, ls);
    int used = snprintf(out, size, "[");
    for (size_t i = 0; i < ls.size() && used < size - 256; i++)
        used += snprintf(out + used, size - used,
            "%s{\"position\":[%.4f,%.4f,%.4f],\"intensity\":%.6f,\"factor\":%.6f,\"color\":[%.4f,%.4f,%.4f]}", i ? "," : "",
            ls[i].position[0], ls[i].position[1], ls[i].position[2], ls[i].intensity, ls[i].factor, ls[i].color[0],
            ls[i].color[1], ls[i].color[2]);
    used += snprintf(out + used, size - used, "]");
    return used;
}

/* Linked glows (glows.c asks for every glow each update, with its key): an
 * actor linked to the glow drives its emission with the actor's flicker
 * (returns 1, *scale) or suppresses it (2); 0 leaves it. Links are by the
 * glow's key; an actor saved before keys takes the key of the glow nearest
 * its link position. */
extern "C" int pc_lights_glow_drive(const char* key, const float p[3], float* scale)
{
    int best = -1;
    for (size_t i = 0; i < actors.size() && best < 0; i++) {
        prefab::Actor& a = actors[i];
        if (!a.glow_linked)
            continue;
        if (a.glow_key.empty()) {
            pc_glow_info g;
            uint32_t id = pc_gi_glow_near(a.glow, 96);
            if (id && pc_gi_glow_get(id, &g))
                a.glow_key = g.key;
        }
        if (a.glow_key == key)
            best = (int)i;
        else if (uint32_t mine = pc_gi_glow_by_key(key)) {
            if (pc_gi_glow_by_key(a.glow_key.c_str()) == mine) { /* the same source in another run: its key now */
                a.glow_key = key;
                best = (int)i;
            }
        }
    }
    if (best < 0)
        return 0;
    prefab::Actor& a = actors[best];
    memcpy(a.glow, p, sizeof a.glow);
    const prefab::Prefab* pf = prefab::library().find(a.prefab);
    if (!pf || (solo >= 0 || (solo_actor >= 0 && solo_actor != best)))
        return 0;
    return prefab::glow_drive(*pf, a, light_time(), animate != 0, scale);
}

/* Orthonormal frame for an aimed light: forward = direction, right and up
 * span the rect (world up is -y). */
static void light_frame(const Light& l, float forward[3], float right[3], float up[3])
{
    rood_math::light_frame(l.direction, forward, right, up);
}

extern "C" int pc_lights_world(float position[][4], float color[][4], float direction[][4],
    float extent_right[][4], float extent_up[][4], int shadows[], int max);
extern "C" int pc_lights_last_dropped(void);
extern "C" void pc_lights_set_dropped(int n);
/* Lights for the present shader, in world space (the shader lights world
 * positions reconstructed from depth): position+radius, colour+type,
 * direction+cos(cone), rect half extents; `world` repeats the positions for
 * the shadow lookups. Returns the count. */
extern "C" int pc_lights_view(float position[][4], float color[][4], float direction[][4],
    float world[][4], int shadows[], float extent_right[][4], float extent_up[][4], int max)
{
    track_room();
    pc_lights_shadows_active = 0;
    float r[3][3], t[3], h, ox, oy;
    if (!pc_scene_camera_get(r, t, &h, &ox, &oy))
        return 0;
    int n = pc_lights_world(position, color, direction, extent_right, extent_up, shadows, max);
    pc_lights_set_dropped(pc_lights_last_dropped());
    for (int i = 0; i < n; i++) {
        memcpy(world[i], position[i], sizeof world[i]);
        pc_lights_shadows_active |= shadows[i];
    }
    return n;
}

/* RPC: light list summary as JSON; suggestion and save by request. */
extern "C" int pc_lights_describe(char* out, int size)
{
    track_room();
    int used = snprintf(out, size,
        "{\"zone\":%d,\"room\":%d,\"enabled\":%s,\"baked\":%g,\"dirty\":%s,\"loads\":%u,\"lights\":[", zone,
        room, pc_lights_enabled ? "true" : "false", pc_lights_baked, dirty ? "true" : "false", layers_loads());
    for (size_t i = 0; i < lights.size() && used < size - 256; i++) {
        const Light& l = lights[i];
        used += snprintf(out + used, size - used,
            "%s{\"id\":\"light:%s\",\"type\":\"%s\",\"position\":[%g,%g,%g],\"color\":[%g,%g,%g],\"intensity\":%g,"
            "\"radius\":%g,\"enabled\":%s}",
            i ? "," : "", l.id.c_str(), l.type == 1 ? "spot" : l.type == 2 ? "rect" : "point", l.position[0], l.position[1], l.position[2],
            l.color[0], l.color[1], l.color[2], l.intensity, l.radius, l.enabled ? "true" : "false");
    }
    used += snprintf(out + used, size - used, "],\"status\":\"%s\"}", status);
    return used;
}
extern "C" void pc_lights_suggest(void) { track_room(); suggest(true); }
extern "C" void pc_lights_save(void) { save_room(); }
extern "C" void pc_lights_reload_disk(void)
{
    track_room(); /* the live room first (a room change loads it anyway) */
    layers_reload();
}

/* ---- in-scene editing: icons, selection, gizmo, place on surface ---- */

namespace {
/* The game's projection, from the captured camera: view = R w + T (R carries
 * the camera scale), sx = ofx + hx X / Z, sy = ofy + h Y / Z in native pixels
 * of the display area, which pc_gl_view maps onto the window. */
struct Camera {
    float r[3][3], t[3], h, hx, ox, oy;
    float view[4]; // window rect x, y, w, h
    float display[2]; // native display width, height
};
bool camera(Camera& c)
{
    if (!pc_scene_camera_get(c.r, c.t, &c.h, &c.ox, &c.oy) || pc_gl_view[2] <= 0 || pc_gl_display[2] <= 0)
        return false;
    c.hx = c.h * (pc_gte_widescreen ? 0.75f : 1.0f);
    for (int i = 0; i < 4; i++)
        c.view[i] = (float)pc_gl_view[i];
    c.display[0] = (float)pc_gl_display[2];
    c.display[1] = (float)pc_gl_display[3];
    return true;
}
void to_view(const Camera& c, const float w[3], float v[3])
{
    for (int i = 0; i < 3; i++)
        v[i] = c.r[i][0] * w[0] + c.r[i][1] * w[1] + c.r[i][2] * w[2] + c.t[i];
}
/* Window pixel of a world point; false when behind the camera. */
bool project(const Camera& c, const float w[3], ImVec2& out, float* z = nullptr)
{
    float v[3];
    to_view(c, w, v);
    if (v[2] < 16)
        return false;
    out.x = c.view[0] + (c.ox + c.hx * v[0] / v[2]) * c.view[2] / c.display[0];
    out.y = c.view[1] + (c.oy + c.h * v[1] / v[2]) * c.view[3] / c.display[1];
    if (z)
        *z = v[2];
    return true;
}
/* View-space point/direction back to world (R's rows have different scales). */
void to_world(const Camera& c, const float v[3], float w[3], bool direction)
{
    float inverse[3][3], d[3];
    pc_scene_camera_inverse(inverse);
    for (int i = 0; i < 3; i++)
        d[i] = v[i] - (direction ? 0 : c.t[i]);
    for (int i = 0; i < 3; i++)
        w[i] = inverse[i][0] * d[0] + inverse[i][1] * d[1] + inverse[i][2] * d[2];
}
/* Column-major view and projection for ImGuizmo that reproduce the mapping
 * above in normalized device coordinates (y up, depth 16..65536). */
void gizmo_matrices(const Camera& c, float view[16], float projection[16])
{
    memset(view, 0, 16 * sizeof(float));
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            view[j * 4 + i] = c.r[i][j];
        view[12 + i] = c.t[i];
    }
    view[15] = 1;
    float n = 16, f = 65536, w = c.display[0], h = c.display[1];
    memset(projection, 0, 16 * sizeof(float));
    projection[0] = 2 * c.hx / w;
    projection[8] = 2 * c.ox / w - 1;
    projection[5] = -2 * c.h / h;
    projection[9] = 1 - 2 * c.oy / h;
    projection[10] = (f + n) / (f - n);
    projection[14] = -2 * f * n / (f - n);
    projection[11] = 1;
}
/* A view for the editing tools: column-major view and projection matrices
 * (clip w > 0 in front), its window rectangle, and a surface picker. The game
 * camera and the editor's free camera both provide one. */
struct View {
    float view[16], projection[16], rect[4];
    bool (*pick)(ImVec2 mouse, float world[3], float normal[3]);
};
bool view_project(const View& v, const float w[3], ImVec2& out, float* depth = nullptr)
{
    float e[4] = { w[0], w[1], w[2], 1 }, eye[4], clip[4];
    for (int r = 0; r < 4; r++)
        eye[r] = v.view[r] * e[0] + v.view[4 + r] * e[1] + v.view[8 + r] * e[2] + v.view[12 + r] * e[3];
    for (int r = 0; r < 4; r++)
        clip[r] = v.projection[r] * eye[0] + v.projection[4 + r] * eye[1] + v.projection[8 + r] * eye[2]
            + v.projection[12 + r] * eye[3];
    if (clip[3] < 1e-3f)
        return false;
    out.x = v.rect[0] + (clip[0] / clip[3] * 0.5f + 0.5f) * v.rect[2];
    out.y = v.rect[1] + (0.5f - clip[1] / clip[3] * 0.5f) * v.rect[3];
    if (depth)
        *depth = clip[3];
    return true;
}
/* World point and normal of the lit surface under a window pixel. */
bool surface_at(const Camera& c, ImVec2 mouse, float world[3], float normal_world[3])
{
    float sx = (mouse.x - c.view[0]) * c.display[0] / c.view[2];
    float sy = (mouse.y - c.view[1]) * c.display[1] / c.view[3];
    float sz, n[3];
    if (!pc_hw_pick(pc_gl_display[0] + sx, pc_gl_display[1] + sy, &sz, n))
        return false;
    float v[3] = { (sx - c.ox) * sz / c.hx, (sy - c.oy) * sz / c.h, sz };
    to_world(c, v, world, false);
    to_world(c, n, normal_world, true);
    float length = sqrtf(normal_world[0] * normal_world[0] + normal_world[1] * normal_world[1]
        + normal_world[2] * normal_world[2]);
    for (int k = 0; k < 3; k++)
        normal_world[k] /= length > 1e-6f ? length : 1;
    return true;
}
Camera game_camera_for_pick;
bool game_pick(ImVec2 mouse, float world[3], float normal[3])
{
    return surface_at(game_camera_for_pick, mouse, world, normal);
}
} // namespace

extern "C" int pc_emissive_viewport_click(float x, float y) __attribute__((weak));
/* The editor view (editor_view.cpp) supplies its own view while active. */
extern "C" int pc_editor_separate(void) __attribute__((weak));
extern "C" int pc_editor_view(float view[16], float projection[16], float rect[4]) __attribute__((weak));
extern "C" int pc_editor_pick(float x, float y, float world[3], float normal[3]) __attribute__((weak));
static bool editor_pick(ImVec2 mouse, float world[3], float normal[3])
{
    return pc_editor_pick(mouse.x, mouse.y, world, normal) != 0;
}
/* The selected glow's position (it follows the glow, by id). */
static void follow_glow()
{
    if (!glow_selected)
        return;
    pc_glow_info g;
    if (!glow_id)
        glow_id = pc_gi_glow_near(glow_at, 96);
    if (glow_id && pc_gi_glow_get(glow_id, &g))
        memcpy(glow_at, g.position, sizeof glow_at);
}

/* Every light that can reach the probe: the room's lights and the path
 * tracer's emitters, with their falloff at the point and the first thing
 * a ray to them hits (CPU trace of the path tracer's scene). */
static void update_probe()
{
    probe_sources.clear();
    float origin[3];
    for (int k = 0; k < 3; k++)
        origin[k] = probe_world[k] + probe_normal[k] * 2;
    auto add = [&](const char* name, const float* position, const float* color, float reach, float size) {
        Source src;
        snprintf(src.name, sizeof src.name, "%s", name);
        memcpy(src.position, position, sizeof src.position);
        memcpy(src.color, color, sizeof src.color);
        float d[3] = { position[0] - origin[0], position[1] - origin[1], position[2] - origin[2] };
        float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        src.distance = dist;
        float x = reach > 0 ? fminf(dist / reach, 1.0f) : 1.0f;
        float ndl = dist > 1e-3f ? (d[0] * probe_normal[0] + d[1] * probe_normal[1] + d[2] * probe_normal[2]) / dist : 0;
        src.weight = (1 - x * x) * (1 - x * x) * fmaxf(ndl, 0.0f);
        src.blocker = -1;
        if (pc_gi_trace_cpu && dist > 1e-3f) {
            float dir[3] = { d[0] / dist, d[1] / dist, d[2] / dist };
            src.blocker = pc_gi_trace_cpu(origin, dir, dist - fabsf(size), &src.hit_t, src.hit);
        }
        probe_sources.push_back(src);
    };
    for (size_t i = 0; i < lights.size(); i++) {
        const Light& l = lights[i];
        if (!l.enabled)
            continue;
        char name[48];
        snprintf(name, sizeof name, "%s light %zu", l.type == 1 ? "Spot" : l.type == 2 ? "Rect" : "Point", i + 1);
        add(name, l.position, l.color, l.radius, 0);
    }
    std::vector<prefab::EvalLight> evaluated;
    for (size_t i = 0; i < actors.size(); i++) {
        if (!actors[i].enabled)
            continue;
        actor_lights(actors[i], evaluated);
        const prefab::Prefab* p = prefab::library().find(actors[i].prefab);
        for (const prefab::EvalLight& e : evaluated) {
            char name[48];
            snprintf(name, sizeof name, "%s %zu: %s", p ? p->name.c_str() : "?", i + 1, e.name.c_str());
            add(name, e.position, e.color, e.radius, e.light_size);
        }
    }
    {
        static pc_glow_info glows[160];
        int n = pc_gi_glow_list(glows, 160);
        for (int i = 0; i < n; i++) {
            if (glows[i].muted || !glows[i].traced)
                continue;
            add(glows[i].name, glows[i].position, glows[i].color, glows[i].position[3], glows[i].color[3]);
        }
    }
    probe_gi_ok = probe_game_view && pc_gi_probe && pc_gi_probe(probe_uv[0], probe_uv[1], probe_gi);
}
static void draw_probe(const View& v, ImDrawList* draw)
{
    update_probe();
    ImVec2 p;
    if (!view_project(v, probe_world, p))
        return;
    for (const Source& src : probe_sources) {
        if (src.weight <= 0)
            continue;
        ImVec2 q;
        if (!view_project(v, src.position, q))
            continue;
        ImU32 col = src.blocker < 0 ? IM_COL32(80, 255, 120, 200) : IM_COL32(255, 70, 60, 200);
        if (src.blocker >= 0) {
            float hit[3];
            for (int k = 0; k < 3; k++)
                hit[k] = probe_world[k] + (src.position[k] - probe_world[k]) * (src.hit_t / fmaxf(src.distance, 1e-3f));
            ImVec2 h;
            if (view_project(v, hit, h)) {
                draw->AddLine(p, h, col, 2.0f);
                draw->AddLine(h, q, IM_COL32(255, 70, 60, 60), 1.0f);
                draw->AddCircleFilled(h, 4, col);
                continue;
            }
        }
        draw->AddLine(p, q, col, 2.0f);
    }
    draw->AddCircle(p, 7, IM_COL32(255, 255, 255, 255), 16, 2.0f);
    draw->AddCircleFilled(p, 3, IM_COL32(255, 255, 255, 255));
}

extern "C" {
float pc_lights_snap_move = 8, pc_lights_snap_angle = 15; /* grid step: Unreal's default size */
int pc_lights_snap; /* grid snapping of moves and rotations (settings editor_snap_grid) */
/* The move and rotate tools' axes: 0 the world's, 1 the object's own (toolbar
 * World / Local; settings editor_gizmo_space). Scale is always the object's. */
int pc_lights_gizmo_space;
}

/* The tools' view this frame: the separate renderer's own, else the game
 * camera's (the live free camera drives the game's). */
static bool tools_view(View& v)
{
    if (pc_editor_separate && pc_editor_separate()) {
        if (!pc_editor_view(v.view, v.projection, v.rect))
            return false;
        v.pick = editor_pick;
        return true;
    }
    Camera c;
    if (!camera(c))
        return false;
    gizmo_matrices(c, v.view, v.projection);
    for (int i = 0; i < 4; i++)
        v.rect[i] = c.view[i];
    game_camera_for_pick = c;
    v.pick = game_pick;
    return true;
}
static bool (*snap_pick)(ImVec2, float*, float*);
static int pick_for_snap(float x, float y, float world[3], float normal[3])
{
    return snap_pick && snap_pick(ImVec2(x, y), world, normal);
}
static pc_overlay_view overlay_view(const View& v)
{
    pc_overlay_view o;
    memcpy(o.view, v.view, sizeof o.view);
    memcpy(o.projection, v.projection, sizeof o.projection);
    memcpy(o.rect, v.rect, sizeof o.rect);
    snap_pick = v.pick;
    return o;
}
/* The overlays' view for the RPC (editor_snap, editor_overlay). */
extern "C" int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick)
{
    View v;
    if (!tools_view(v))
        return 0;
    *out = overlay_view(v);
    if (pick)
        *pick = pick_for_snap;
    return 1;
}
/* Snap at window pixel (x, y) with `modes`; with `apply`, the selected light
 * moves there as a drag would take it (RPC editor_snap). */
extern "C" int pc_lights_snap_at(float x, float y, unsigned modes, int apply, pc_snap_result* out)
{
    pc_overlay_view v;
    pc_overlay_pick pick;
    memset(out, 0, sizeof *out);
    if (!pc_lights_tools_view(&v, &pick))
        return PC_SNAP_NONE;
    int type = pc_overlay_snap(&v, x, y, modes, pick, out);
    if (type && apply && selected >= 0 && selected < (int)lights.size()) {
        memcpy(lights[selected].position, out->position, sizeof out->position);
        dirty = true;
    } else if (type && apply && selected_actor >= 0 && selected_actor < (int)actors.size()) {
        memcpy(actors[selected_actor].position, out->position, sizeof out->position);
        dirty = true;
    }
    return type;
}
static Light from_eval(const prefab::EvalLight& e);
/* RPC lights {action "add"}: a point light at `position`, selected. */
extern "C" int pc_lights_add(const float position[3])
{
    track_room();
    Light l;
    memcpy(l.position, position, sizeof l.position);
    lights.push_back(l);
    select_light((int)lights.size() - 1);
    dirty = true;
    return selected;
}

/* ---- placement: the Place Actors panel's drag and drop, double-click,
 * Ctrl+click and the RPC ---- */

static const unsigned geometry_modes = PC_SNAP_MODE_POINT | PC_SNAP_MODE_EDGE | PC_SNAP_MODE_MIDPOINT;

/* "light:point|spot|rect" (light_type 0-2), "prefab:<id>" (*p), or another
 * library item (editor_library.h; light_type -1): a fixture class gives its
 * prefab in *p (its placement and lit preview), a fog preset none. */
static bool parse_item(const std::string& item, int* light_type, const prefab::Prefab** p)
{
    *p = nullptr;
    *light_type = -1;
    if (item.rfind("light:", 0) == 0) {
        std::string t = item.substr(6);
        *light_type = t == "spot" ? 1 : t == "rect" ? 2 : 0;
        return t == "point" || t == "spot" || t == "rect";
    }
    if (item.rfind("prefab:", 0) == 0) {
        *p = prefab::library().find(item.substr(7));
        return *p != nullptr;
    }
    if (rood_library::is_item(item)) {
        *p = rood_library::item_prefab(item);
        return true;
    }
    return false;
}
std::string rood_lights::item_label(const std::string& item)
{
    int type;
    const prefab::Prefab* p;
    if (!parse_item(item, &type, &p))
        return item;
    if (type < 0 && rood_library::is_item(item))
        return rood_library::label(item);
    return p ? p->name : light_type_name(type);
}
bool rood_lights::view_rect(float rect[4])
{
    View v;
    if (!tools_view(v))
        return false;
    memcpy(rect, v.rect, sizeof v.rect);
    return true;
}

static pc_snap_result last_place_snap; /* of the last place_compute (highlight) */
bool rood_lights::place_compute(const std::string& item, float x, float y, unsigned modes, Placement& out)
{
    out = Placement();
    int type;
    const prefab::Prefab* p;
    pc_overlay_view ov;
    pc_overlay_pick pick;
    if (!parse_item(item, &type, &p) || !pc_lights_tools_view(&ov, &pick))
        return false;
    if (!modes)
        modes = pc_snap_modes(0);
    /* Points and edges as the editor snaps; the surface always as the fallback. */
    modes = (modes & geometry_modes) | PC_SNAP_MODE_SURFACE;
    pc_snap_result r;
    int snap = pc_overlay_snap(&ov, x, y, modes, pick, &r);
    if (!snap)
        return false;
    last_place_snap = r;
    out.ok = true;
    out.snap = snap;
    memcpy(out.surface, r.surface, sizeof out.surface);
    memcpy(out.normal, r.normal, sizeof out.normal);
    bool grid = snap == PC_SNAP_SURFACE && pc_lights_snap;
    for (int k = 0; k < 3; k++) {
        /* A light: off the surface by the snap offset (r.position); an
         * actor's pivot: on it, by the prefab's own offset; another library
         * item (a fog volume): the surface point itself. */
        float at = snap != PC_SNAP_SURFACE ? r.position[k]
            : p                            ? r.surface[k] + r.normal[k] * p->surface_offset
            : type < 0                     ? r.surface[k]
                                           : r.position[k];
        out.position[k] = grid ? roundf(at / pc_lights_snap_move) * pc_lights_snap_move : at;
    }
    if (p && p->align == "normal" && snap == PC_SNAP_SURFACE) {
        prefab::rotation_facing(r.normal, out.rotation);
    } else if (p && snap == PC_SNAP_SURFACE && fabsf(r.normal[1]) < 0.5f) {
        /* An upright prefab on a wall turns to face out of it (yaw only). */
        float out_of_wall[3] = { r.normal[0], 0, r.normal[2] };
        prefab::rotation_facing(out_of_wall, out.rotation);
    }
    return true;
}

static uint32_t new_seed()
{
    static uint32_t counter;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t s = prefab::hash32((uint32_t)now.tv_nsec ^ prefab::hash32(++counter + (uint32_t)actors.size() * 977u));
    return s ? s % 1000000u : 1;
}

int rood_lights::place_commit(const std::string& item, const Placement& where)
{
    int type;
    const prefab::Prefab* p;
    if (!where.ok || !parse_item(item, &type, &p))
        return -1;
    track_room();
    int index;
    if (type < 0 && rood_library::is_item(item)) { /* a library item: its source places and selects it (editor_library.cpp) */
        index = rood_library::place(item, where);
        if (index < 0) {
            snprintf(status, sizeof status, "Could not place %s there", item_label(item).c_str());
            return -1;
        }
    } else if (!p) {
        Light l;
        l.type = type;
        memcpy(l.position, where.position, sizeof l.position);
        if (type != 0 && where.snap == PC_SNAP_SURFACE) /* aim away from the surface it was dropped on */
            memcpy(l.direction, where.normal, sizeof l.direction);
        lights.push_back(l);
        index = (int)lights.size() - 1;
        select_light(index);
    } else {
        prefab::Actor a;
        a.prefab = p->id;
        memcpy(a.position, where.position, sizeof a.position);
        memcpy(a.rotation, where.rotation, sizeof a.rotation);
        a.seed = new_seed();
        actors.push_back(a);
        index = (int)actors.size() - 1;
        select_actor(index);
    }
    gizmo_operation = 0;
    dirty = true;
    pc_undo_hint(("Place " + item_label(item)).c_str());
    static const char* snaps[] = { "", "on a point", "on an edge midpoint", "on an edge", "on the surface" };
    snprintf(status, sizeof status, "Placed %s %s (unsaved)", item_label(item).c_str(), snaps[where.snap & 7 ? where.snap : 0]);
    return index;
}

int rood_lights::place_at_centre(const std::string& item)
{
    float rect[4];
    Placement where;
    if (!view_rect(rect)
        || !place_compute(item, rect[0] + rect[2] * 0.5f, rect[1] + rect[3] * 0.5f, PC_SNAP_MODE_SURFACE, where)) {
        snprintf(status, sizeof status, "Nothing under the view's centre to place %s on", item_label(item).c_str());
        return -1;
    }
    return place_commit(item, where);
}

namespace {
struct Drag {
    bool active = false, forced = false, over = false;
    std::string item;
    float x = 0, y = 0;
    Placement where;
    pc_snap_result snap;
} drag;
bool drag_suppressed; /* cancelled with the button still held: no new drag until it is released */
std::vector<Ghost> drag_ghost;
/* A box select begun on empty space (or over a fog volume only): where it
 * began, with which modifiers, and the volume under it then. */
struct Marquee {
    bool pending = false, dragging = false;
    float x = 0, y = 0;
    bool shift = false, ctrl = false;
    rood_selection::Ref volume;
} marquee;
} // namespace

static void update_drag()
{
    drag_ghost.clear();
    drag.over = false;
    drag.where = Placement();
    if (!drag.active)
        return;
    float rect[4];
    if (!view_rect(rect))
        return;
    bool inside = drag.x >= rect[0] && drag.y >= rect[1] && drag.x < rect[0] + rect[2] && drag.y < rect[1] + rect[3];
    /* Over the view itself, not over a panel or window on top of it. */
    if (!drag.forced && GImGui && GImGui->HoveredWindow)
        inside = false;
    drag.over = inside;
    if (!inside || !place_compute(drag.item, drag.x, drag.y, 0, drag.where))
        return;
    drag.snap = last_place_snap;
    int type;
    const prefab::Prefab* p;
    parse_item(drag.item, &type, &p);
    if (!p && type < 0)
        return; /* a library item without a lit preview (a fog volume): draw_place_drag draws its outline */
    Ghost g;
    g.actor = p != nullptr;
    if (p) {
        g.a.prefab = p->id;
        memcpy(g.a.position, drag.where.position, sizeof g.a.position);
        memcpy(g.a.rotation, drag.where.rotation, sizeof g.a.rotation);
        g.a.seed = 7;
    } else {
        g.light.type = type;
        memcpy(g.light.position, drag.where.position, sizeof g.light.position);
        if (type != 0 && drag.where.snap == PC_SNAP_SURFACE)
            memcpy(g.light.direction, drag.where.normal, sizeof g.light.direction);
    }
    drag_ghost.push_back(g);
}
void rood_lights::place_drag_begin(const std::string& item, bool forced, float x, float y)
{
    if (drag_suppressed && !forced)
        return;
    drag = Drag();
    drag.active = true;
    drag.forced = forced;
    drag.item = item;
    drag.x = x, drag.y = y;
    place_item = item;
    update_drag();
}
void rood_lights::place_drag_move(float x, float y)
{
    drag.x = x, drag.y = y;
    update_drag();
}
int rood_lights::place_drag_drop()
{
    if (!drag.active)
        return -1;
    update_drag();
    int index = drag.over && drag.where.ok ? place_commit(drag.item, drag.where) : -1;
    if (index < 0)
        snprintf(status, sizeof status, "Place %s cancelled (dropped off the view)", item_label(drag.item).c_str());
    drag.active = false;
    drag_ghost.clear();
    return index;
}
bool rood_lights::place_drag_cancel()
{
    bool was = drag.active;
    if (was && !drag.forced)
        drag_suppressed = true;
    drag.active = false;
    drag_ghost.clear();
    if (was)
        snprintf(status, sizeof status, "Place %s cancelled", item_label(drag.item).c_str());
    return was;
}
bool rood_lights::place_drag_active(std::string* item, Placement* where, bool* over_view)
{
    if (item)
        *item = drag.item;
    if (where)
        *where = drag.where;
    if (over_view)
        *over_view = drag.over;
    return drag.active;
}
/* Escape while dragging from the panel cancels the drag (dev_overlay.cpp). */
extern "C" int pc_lights_place_cancel(void) { return place_drag_cancel(); }

/* A radius as a wireframe sphere: three great circles. */
static void draw_sphere(const View& v, ImDrawList* draw, const float centre[3], float radius, ImU32 color, float thickness)
{
    for (int axis = 0; axis < 3; axis++) {
        ImVec2 points[49];
        int n = 0;
        for (int k = 0; k <= 48; k++) {
            float a = k * 6.2831853f / 48, w[3] = { centre[0], centre[1], centre[2] };
            w[(axis + 1) % 3] += cosf(a) * radius;
            w[(axis + 2) % 3] += sinf(a) * radius;
            ImVec2 q;
            if (view_project(v, w, q))
                points[n++] = q;
            else if (n > 1) {
                draw->AddPolyline(points, n, color, 0, thickness);
                n = 0;
            }
        }
        if (n > 1)
            draw->AddPolyline(points, n, color, 0, thickness);
    }
}
/* A light's shape: spot cone edges or rect outline and its facing. */
static void draw_light_shape(const View& v, ImDrawList* draw, const Light& l, ImU32 shape)
{
    if (l.type == 0)
        return;
    ImVec2 p;
    if (!view_project(v, l.position, p))
        return;
    float forward[3], right[3], up[3];
    light_frame(l, forward, right, up);
    if (l.type == 2) { /* rect outline */
        ImVec2 corner[4];
        bool all = true;
        for (int k = 0; k < 4; k++) {
            float sx = (k == 1 || k == 2) ? 0.5f : -0.5f, sy = k >= 2 ? 0.5f : -0.5f, w[3];
            for (int a = 0; a < 3; a++)
                w[a] = l.position[a] + right[a] * l.size[0] * sx + up[a] * l.size[1] * sy;
            all &= view_project(v, w, corner[k]);
        }
        if (all)
            draw->AddPolyline(corner, 4, shape, ImDrawFlags_Closed, 2.0f);
    }
    float len = l.type == 1 ? l.radius : 96, tip[3];
    for (int a = 0; a < 3; a++)
        tip[a] = l.position[a] + forward[a] * len;
    if (l.type == 1) { /* spot cone edges */
        float spread = tanf(l.cone * 0.5f * 3.14159265f / 180) * l.radius;
        for (int k = 0; k < 4; k++) {
            float a = k * 1.5707963f, w[3];
            for (int d = 0; d < 3; d++)
                w[d] = tip[d] + (right[d] * cosf(a) + up[d] * sinf(a)) * spread;
            ImVec2 q;
            if (view_project(v, w, q))
                draw->AddLine(p, q, shape, 1.5f);
        }
    } else {
        ImVec2 q;
        if (view_project(v, tip, q))
            draw->AddLine(p, q, shape, 2.0f);
    }
}
static ImU32 light_colour(const float c[3], float alpha)
{
    return IM_COL32((int)(fminf(c[0], 1) * 255), (int)(fminf(c[1], 1) * 255), (int)(fminf(c[2], 1) * 255), (int)(alpha * 255));
}
/* An actor in the view: its prefab's icon at the pivot, lines to its
 * lights (small dots), the lights' shapes, and their radii when selected.
 * With a reference, its pivot and its lights are its icons for picking
 * (editor_scene.cpp; the pivot is its anchor). */
static void draw_actor(const View& v, ImDrawList* draw, const prefab::Actor& a, bool is_selected, float alpha,
    const char* label, const rood_selection::Ref* ref)
{
    ImVec2 pivot;
    if (!view_project(v, a.position, pivot))
        return;
    std::vector<prefab::EvalLight> ls;
    actor_lights(a, ls);
    const prefab::Prefab* p = prefab::library().find(a.prefab);
    float em = ImGui::GetStyle().FontScaleMain, icon = 11 * em;
    float colour[3] = { 1, 0.8f, 0.5f };
    if (!ls.empty())
        memcpy(colour, ls[0].color, sizeof colour);
    for (const prefab::EvalLight& e : ls) {
        Light l = from_eval(e);
        ImVec2 q;
        if (!view_project(v, l.position, q))
            continue;
        draw->AddLine(pivot, q, IM_COL32(255, 255, 255, (int)(110 * alpha)), 1.0f);
        draw_light_shape(v, draw, l, IM_COL32(120, 220, 255, (int)((is_selected ? 200 : 80) * alpha)));
        if (is_selected)
            draw_sphere(v, draw, l.position, l.radius, IM_COL32(255, 230, 120, (int)(150 * alpha)), 1.5f);
        float r = 5 * em;
        draw->AddCircleFilled(q, r, light_colour(e.color, (e.enabled ? 1.0f : 0.35f) * alpha));
        draw->AddCircle(q, r + 1.5f, is_selected ? IM_COL32(255, 255, 0, (int)(255 * alpha)) : IM_COL32(0, 0, 0, (int)(180 * alpha)), 16, 1.5f);
        if (ref)
            rood_scene::icon(*ref, q.x, q.y, r + 4, false);
    }
    /* The pivot: a rounded square holding the prefab's icon. */
    ImVec2 lo(pivot.x - icon, pivot.y - icon), hi(pivot.x + icon, pivot.y + icon);
    draw->AddRectFilled(lo, hi, IM_COL32(30, 30, 36, (int)((a.enabled ? 220 : 120) * alpha)), 4 * em);
    draw_icon(draw, pivot.x, pivot.y, icon * 1.6f, "prefab:" + a.prefab, colour, (a.enabled ? 1.0f : 0.4f) * alpha);
    draw->AddRect(lo, hi, is_selected ? IM_COL32(255, 255, 0, (int)(255 * alpha)) : p ? IM_COL32(0, 0, 0, (int)(200 * alpha)) : IM_COL32(255, 60, 60, 255), 4 * em, ImDrawFlags_None, 2.0f);
    if (label)
        draw->AddText(ImVec2(hi.x + 4, lo.y), IM_COL32(255, 255, 255, (int)(230 * alpha)), label);
    if (ref)
        rood_scene::icon(*ref, pivot.x, pivot.y, icon + 3, true);
}

/* The drag from the Place Actors panel: the ghost where it would land
 * (lit by the renderers through pc_lights_world), the snap highlight, and
 * a label at the cursor. */
static void draw_place_drag(const View& v, const pc_overlay_view& ov, ImDrawList* draw)
{
    ImDrawList* front = ImGui::GetForegroundDrawList();
    std::string label = item_label(drag.item);
    float colour[3] = { 1, 0.85f, 0.6f };
    bool outline = drag_ghost.empty() && rood_library::is_item(drag.item); /* drawn by its source (a fog volume's box) */
    if (drag.over && drag.where.ok && (!drag_ghost.empty() || outline)) {
        const Ghost& g = outline ? Ghost() : drag_ghost[0];
        if (drag.snap.type != PC_SNAP_SURFACE)
            pc_overlay_draw_snap(&ov, draw, &drag.snap);
        else { /* the surface point and its normal */
            ImVec2 s, n;
            float tip[3];
            for (int k = 0; k < 3; k++)
                tip[k] = drag.where.surface[k] + drag.where.normal[k] * 48;
            if (view_project(v, drag.where.surface, s) && view_project(v, tip, n)) {
                draw->AddLine(s, n, IM_COL32(120, 255, 160, 220), 2.0f);
                draw->AddCircle(s, 6, IM_COL32(120, 255, 160, 220), 16, 2.0f);
            }
        }
        if (outline)
            rood_library::draw_ghost(drag.item, ov, drag.where, draw);
        else if (g.actor)
            draw_actor(v, draw, g.a, true, 0.75f, nullptr, nullptr);
        else {
            ImVec2 p;
            if (view_project(v, g.light.position, p)) {
                draw_light_shape(v, draw, g.light, IM_COL32(120, 220, 255, 200));
                draw_sphere(v, draw, g.light.position, g.light.radius, IM_COL32(255, 230, 120, 170), 1.5f);
                float icon = 9 * ImGui::GetStyle().FontScaleMain;
                draw->AddCircleFilled(p, icon, light_colour(g.light.color, 0.85f));
                draw->AddCircle(p, icon + 2, IM_COL32(255, 255, 0, 220), 24, 2);
            }
            memcpy(colour, g.light.color, sizeof colour);
        }
        static const char* snaps[] = { "", "point", "midpoint", "edge", "surface" };
        char text[160];
        snprintf(text, sizeof text, "%s  (%s %.0f %.0f %.0f)", label.c_str(), snaps[drag.where.snap & 7 ? drag.where.snap : 0],
            drag.where.position[0], drag.where.position[1], drag.where.position[2]);
        label = text;
    } else {
        label += drag.over ? "  (nothing under the cursor)" : "  (drop on the view; Esc cancels)";
    }
    ImVec2 at(drag.x + 18, drag.y + 14);
    ImVec2 size = ImGui::CalcTextSize(label.c_str());
    float em = ImGui::GetFontSize();
    front->AddRectFilled(ImVec2(at.x - 4, at.y - 3), ImVec2(at.x + size.x + em * 1.6f + 6, at.y + size.y + 3),
        IM_COL32(20, 20, 24, 220), 4);
    draw_icon(front, at.x + em * 0.6f, at.y + size.y * 0.5f, em * 1.1f, drag.item, colour, 1);
    front->AddText(ImVec2(at.x + em * 1.4f, at.y), drag.over ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 190, 120, 255),
        label.c_str());
}

/* The cutaway's focus (See into rooms > Cut to focus; C): a ring with a
 * cross where the cut aims, solid while room surfaces hide it (they are
 * drawn as ghosts then), dashed while it is in plain view. */
extern "C" int pc_editor_cutaway_focus(float out[3]) __attribute__((weak));
extern "C" int pc_editor_cutaway_state(int* hidden, float* amount) __attribute__((weak));
extern "C" int pc_editor_cutaway __attribute__((weak));
static void draw_cutaway_focus(const View& v, ImDrawList* draw)
{
    float focus[3], amount = 0;
    int hidden = 0;
    if (!&pc_editor_cutaway || !pc_editor_cutaway || !pc_editor_cutaway_focus || !pc_editor_cutaway_state
        || !pc_editor_cutaway_state(&hidden, &amount) || !pc_editor_cutaway_focus(focus))
        return;
    ImVec2 p;
    if (!view_project(v, focus, p))
        return;
    float em = ImGui::GetStyle().FontScaleMain, r = 10 * em;
    ImU32 ring = hidden ? IM_COL32(120, 230, 255, 230) : IM_COL32(120, 230, 255, 120);
    if (hidden) {
        draw->AddCircle(p, r, IM_COL32(0, 0, 0, 160), 32, 3.5f * em);
        draw->AddCircle(p, r, ring, 32, 1.8f * em);
    } else {
        for (int k = 0; k < 12; k += 2) /* dashed */
            draw->PathArcTo(p, r, k * 0.5235988f, (k + 1) * 0.5235988f, 4), draw->PathStroke(ring, 0, 1.5f * em);
    }
    for (int k = 0; k < 4; k++) {
        float a = k * 1.5707963f, c = cosf(a), s = sinf(a);
        draw->AddLine(ImVec2(p.x + c * r * 0.45f, p.y + s * r * 0.45f), ImVec2(p.x + c * r * 1.5f, p.y + s * r * 1.5f), ring,
            1.5f * em);
    }
    if (hidden)
        draw->AddText(ImVec2(p.x + r * 1.6f, p.y - r * 1.6f), IM_COL32(170, 240, 255, 220), "focus (ghosting what hides it)");
}

/* A click on nothing in the view: Ctrl adds the Place panel's item there,
 * Shift moves the selected light or actor there (the snapped point or edge,
 * else off the surface), a plain click selects nothing. */
static void empty_click(float x, float y, bool shift, bool ctrl)
{
    bool have_light = selected >= 0 && selected < (int)lights.size();
    bool have_actor = selected_actor >= 0 && selected_actor < (int)actors.size();
    Placement where;
    if (ctrl) {
        if (place_compute(place_item, x, y, 0, where))
            place_commit(place_item, where);
        return;
    }
    if (shift) {
        std::string item = have_actor ? "prefab:" + actors[selected_actor].prefab : "light:point";
        if (!(have_light || have_actor) || !place_compute(item, x, y, 0, where))
            return;
        pc_undo_hint(have_light ? "Move light to surface" : "Move actor to surface");
        if (have_light)
            memcpy(lights[selected].position, where.position, sizeof where.position);
        else
            memcpy(actors[selected_actor].position, where.position, sizeof where.position);
        dirty = true;
        return;
    }
    rood_selection::clear(rood_selection::FROM_VIEWPORT);
    rood_scene::publish();
}

/* R through the scale table (editor_transform.h): from the shape as the drag
 * began (`from_light` / `from_actor`; a fog volume from its size now), one
 * undo step named after the object. */
static void scale_object(const rood_selection::Ref& r, const float ratio[3], const Light& from_light,
    const prefab::Actor& from_actor)
{
    int i = rood_scene::index_of(r);
    if (i < 0)
        return;
    if (r.kind == rood_selection::KIND_LIGHT) {
        rood_transform::scale_light(from_light, ratio, lights[i]);
        pc_undo_hint((std::string("Scale ") + light_type_name(lights[i].type)).c_str());
    } else if (r.kind == rood_selection::KIND_ACTOR) {
        rood_transform::scale_actor(from_actor, ratio, actors[i]);
        pc_undo_hint(("Scale " + rood_scene::label(r)).c_str());
    } else if (r.kind == rood_selection::KIND_VOLUME) {
        float size[3], out[3];
        if (!rood_atmo::volume_size(i, size))
            return;
        rood_transform::scale_volume(size, ratio, out);
        rood_atmo::set_volume_size(i, out);
        pc_undo_hint("Scale fog volume");
    } else if (r.kind == rood_selection::KIND_EMITTER) {
        float size[3], out[3];
        if (!rood_particles::emitter_size(i, size))
            return;
        std::string shape = rood_particles::field(i, "shape");
        if (shape == "point" || shape == "disc") { /* a ball: its radius (as the viewport's R) */
            float k = fmaxf(fmaxf(fabsf(ratio[0]), fabsf(ratio[1])), fabsf(ratio[2]));
            char text[32];
            snprintf(text, sizeof text, "%.1f", fmaxf((float)atof(rood_particles::field(i, "radius").c_str()) * k, 0.0f));
            rood_particles::set_field(i, "radius", text);
        } else {
            rood_transform::scale_volume(size, ratio, out);
            rood_particles::set_emitter_size(i, out);
        }
        pc_undo_hint("Scale emitter");
    } else {
        return;
    }
    dirty = true;
}
namespace {
Light scale_from_light; /* the shape as a scale drag began */
prefab::Actor scale_from_actor;
/* The light / actor manipulator as last drawn (RPC editor_transform). */
struct GizmoShown {
    int frame = -100;
    std::string subject;
    const char* operation = "";
    const char* mode = "";
} gizmo_shown;
} // namespace
/* Draw and edit lights and actors in the scene (called with the overlay
 * open), over the viewport overlays (editor_overlays.cpp). Click an icon
 * to select (an actor's pivot or any of its lights selects the actor); drag
 * the gizmo to move (an actor turns with the rotate tool), snapping to the
 * grid, points, edges or surfaces (V held toggles point snapping);
 * Shift+click a surface to move the selection there, Ctrl+click to add the
 * Place panel's item there (pc_snap_offset off it for a light, or on the
 * point or edge snapped to). A drag from the Place panel shows a lit ghost
 * where it would land. */
extern "C" void pc_lights_viewport(void)
{
    View v;
    if (!tools_view(v))
        return;
    pc_overlay_view ov = overlay_view(v);
    rood_scene::icons_begin();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    bool v_held = !io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_V);
    unsigned modes = pc_snap_modes(v_held);
    static bool was_using;
    bool snap_drag = (was_using && gizmo_operation == 0 && (modes & geometry_modes)) || (drag.active && (modes & geometry_modes));
    pc_overlay_draw(&ov, draw, snap_drag || (v_held && (modes & geometry_modes)));
    pc_probe_draw(ov.view, ov.projection, ov.rect, draw); /* probe GI visualisation (probe_overlay.cpp) */
    draw_cutaway_focus(v, draw);
    bool in_view = io.MousePos.x >= v.rect[0] && io.MousePos.y >= v.rect[1] && io.MousePos.x < v.rect[0] + v.rect[2]
        && io.MousePos.y < v.rect[1] + v.rect[3];
    /* V held over the view (no drag): where a drag would snap. */
    if (v_held && !was_using && !drag.active && in_view && !io.WantCaptureMouse && modes) {
        pc_snap_result preview;
        if (pc_overlay_snap(&ov, io.MousePos.x, io.MousePos.y, modes, pick_for_snap, &preview))
            pc_overlay_draw_snap(&ov, draw, &preview);
    }
    /* A drag from the Place Actors panel: follow the mouse, drop on release. */
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        drag_suppressed = false;
    if (drag.active) {
        if (!drag.forced) {
            drag.x = io.MousePos.x, drag.y = io.MousePos.y;
            update_drag();
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                place_drag_drop();
                was_using = false;
                return;
            }
        }
        draw_place_drag(v, ov, draw);
    }
    rood_fit::hint(draw); /* armed: the pane under the pointer */
    if (!pc_lights_show_gizmos || !pc_lights_enabled) {
        was_using = false;
        return;
    }
    for (size_t i = 0; i < lights.size(); i++) {
        const Light& l = lights[i];
        ImVec2 p;
        if (rood_scene::light_hidden((int)i) || !view_project(v, l.position, p))
            continue;
        rood_selection::Ref ref = rood_scene::light_ref((int)i);
        bool is_selected = rood_selection::contains(ref);
        draw_light_shape(v, draw, l, is_selected ? IM_COL32(120, 220, 255, 220) : IM_COL32(120, 220, 255, 90));
        draw_sphere(v, draw, l.position, l.radius, is_selected ? IM_COL32(255, 230, 120, 200) : IM_COL32(255, 255, 255, 70),
            is_selected ? 2.0f : 1.0f);
        float icon = 9 * ImGui::GetStyle().FontScaleMain;
        draw->AddCircleFilled(p, icon, light_colour(l.color, l.enabled ? 1.0f : 0.35f));
        draw->AddCircle(p, icon + 2, is_selected ? IM_COL32(255, 255, 0, 255) : IM_COL32(0, 0, 0, 200), 24, 2);
        char label[16];
        snprintf(label, sizeof label, "%zu", i + 1);
        draw->AddText(ImVec2(p.x + icon + 4, p.y - icon), IM_COL32(255, 255, 255, 230), label);
        rood_scene::icon(ref, p.x, p.y, icon + 4, true);
    }
    /* Actors, then suggestion ghosts (dimmed). */
    for (size_t i = 0; i < actors.size(); i++) {
        if (rood_scene::actor_hidden((int)i))
            continue;
        const prefab::Prefab* p = prefab::library().find(actors[i].prefab);
        char label[96];
        snprintf(label, sizeof label, "%s %zu", p ? p->name.c_str() : actors[i].prefab.c_str(), i + 1);
        rood_selection::Ref ref = rood_scene::actor_ref((int)i);
        draw_actor(v, draw, actors[i], rood_selection::contains(ref), 1.0f, label, &ref);
    }
    for (const Ghost& g : ghosts)
        if (g.actor)
            draw_actor(v, draw, g.a, false, 0.5f, nullptr, nullptr);
    /* The selected actor's fixture: its polygons around the flame, outlined. */
    if (selected_actor >= 0 && selected_actor < (int)actors.size() && actors[selected_actor].fixture) {
        const prefab::Actor& a = actors[selected_actor];
        static float tris[512][9];
        int n = pc_gi_fixture_triangles(a.fixture_at, fmaxf(a.fixture_radius * 1.5f, 24), tris, 512);
        ImU32 edge = a.fixture_shadows ? IM_COL32(255, 160, 60, 200) : IM_COL32(80, 230, 255, 200);
        for (int t = 0; t < n; t++) {
            ImVec2 q[3];
            if (view_project(v, tris[t], q[0]) && view_project(v, tris[t] + 3, q[1]) && view_project(v, tris[t] + 6, q[2]))
                draw->AddPolyline(q, 3, edge, ImDrawFlags_Closed, 1.2f);
        }
        ImVec2 c;
        if (view_project(v, a.fixture_at, c))
            draw->AddCircle(c, 5, edge, 12, 2.0f);
    }
    /* The game's glows: diamonds in their colour at their smoothed centre
     * (they hold still while the sprites flicker); click to select (by id).
     * Glows that are out (their source drew nothing lately) are hollow. */
    static pc_glow_info glows[160];
    int glow_count = pc_gi_glow_list(glows, 160);
    follow_glow();
    for (int i = 0; i < glow_count; i++) {
        const pc_glow_info& g = glows[i];
        ImVec2 p;
        if (g.color[3] < 0 || g.category == PC_GLOW_EFFECT || rood_scene::glow_hidden(g.key, g.position)
            || !view_project(v, g.position, p))
            continue; /* effects: not selectable one by one */
        rood_selection::Ref ref = rood_scene::glow_ref_key(g.key, g.position);
        bool is_selected = rood_selection::contains(ref);
        if (g.presence < 0.02f && !is_selected && !g.overridden)
            continue;
        float r = 8 * ImGui::GetStyle().FontScaleMain;
        ImVec2 d[4] = { ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y) };
        float c = g.muted ? 0.35f : 1.0f, lit = fmaxf(g.presence, 0.0f), shown[3];
        for (int k = 0; k < 3; k++) /* the glow's colour before it faded */
            shown[k] = fminf(lit > 0.02f ? g.color[k] / lit : g.raw_color[k], 1.0f) * 255 * c;
        if (g.presence >= 0.02f)
            draw->AddConvexPolyFilled(d, 4, IM_COL32((int)shown[0], (int)shown[1], (int)shown[2], 230));
        draw->AddPolyline(d, 4, is_selected ? IM_COL32(255, 255, 0, 255) : IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, 2.0f);
        rood_scene::icon(ref, p.x, p.y, r + 4, true);
    }
    if (probe_valid)
        draw_probe(v, draw);
    /* The selection's handles (editor_handles.cpp) have the mouse before the
     * gizmo and the view's clicks; the gizmo is off under a handle. */
    bool handles_busy = !drag.active && rood_handles::frame(ov, pick_for_snap, draw);
    ImGuizmo::Enable(!handles_busy);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(v.rect[0], v.rect[1], v.rect[2], v.rect[3]);
    bool gizmo_busy = false;
    bool have_light = selected >= 0 && selected < (int)lights.size();
    bool have_actor = selected_actor >= 0 && selected_actor < (int)actors.size();
    if ((have_light || have_actor) && !drag.active) {
        float model[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float right[3], second[3], forward[3], *position;
        bool rotate, scale = gizmo_operation == 2;
        if (have_light) {
            Light& l = lights[selected];
            light_frame(l, forward, right, second); /* columns: right, up, forward */
            position = l.position;
            rotate = gizmo_operation == 1 && l.type != 0;
        } else {
            prefab::Actor& a = actors[selected_actor];
            prefab::rotation_basis(a.rotation, right, second, forward); /* right, down, forward */
            position = a.position;
            rotate = gizmo_operation == 1;
        }
        /* Unit columns every frame: a scale drag's columns come back as its ratios. */
        for (int k = 0; k < 3; k++)
            model[k] = right[k], model[4 + k] = second[k], model[8 + k] = forward[k];
        model[12] = position[0], model[13] = position[1], model[14] = position[2];
        float snap[3] = { pc_lights_snap_move, pc_lights_snap_move, pc_lights_snap_move };
        if (rotate)
            snap[0] = snap[1] = snap[2] = pc_lights_snap_angle;
        if (scale)
            snap[0] = snap[1] = snap[2] = 0.1f; /* ratio steps */
        Light before_light = have_light ? lights[selected] : Light();
        prefab::Actor before_actor = have_actor ? actors[selected_actor] : prefab::Actor();
        if (!was_using) /* the shape as a scale drag begins (R maps the ratios onto it) */
            scale_from_light = before_light, scale_from_actor = before_actor;
        bool snapping = (pc_lights_snap != 0) != io.KeyCtrl; /* Ctrl inverts grid snapping while dragging */
        ImGuizmo::OPERATION op = scale ? ImGuizmo::SCALE : rotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;
        /* The toolbar's World / Local for move and rotate; scale always on the object's own axes. */
        ImGuizmo::MODE mode = scale || pc_lights_gizmo_space ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        rood_selection::Ref subject = have_light ? rood_scene::light_ref(selected) : rood_scene::actor_ref(selected_actor);
        gizmo_shown.frame = ImGui::GetFrameCount();
        gizmo_shown.subject = subject.id;
        gizmo_shown.operation = scale ? "scale" : rotate ? "rotate" : "move";
        gizmo_shown.mode = mode == ImGuizmo::LOCAL ? "local" : "world";
        if (ImGuizmo::Manipulate(v.view, v.projection, op, mode, model, nullptr, snapping ? snap : nullptr)) {
            if (scale) {
                float ratio[3];
                for (int col = 0; col < 3; col++)
                    ratio[col] = sqrtf(model[col * 4] * model[col * 4] + model[col * 4 + 1] * model[col * 4 + 1]
                        + model[col * 4 + 2] * model[col * 4 + 2]);
                scale_object(subject, ratio, scale_from_light, scale_from_actor);
            } else {
                position[0] = model[12], position[1] = model[13], position[2] = model[14];
                if (rotate) {
                    float c[3][3];
                    for (int col = 0; col < 3; col++) {
                        float len = sqrtf(model[col * 4] * model[col * 4] + model[col * 4 + 1] * model[col * 4 + 1]
                            + model[col * 4 + 2] * model[col * 4 + 2]);
                        for (int k = 0; k < 3; k++)
                            c[col][k] = model[col * 4 + k] / (len > 1e-6f ? len : 1);
                    }
                    if (have_light)
                        memcpy(lights[selected].direction, c[2], sizeof c[2]);
                    else
                        prefab::rotation_from_basis(c[0], c[1], c[2], actors[selected_actor].rotation);
                }
            }
            dirty = true;
        }
        /* Point, edge and surface snapping take the selection to the target
         * under the cursor, wherever the handle would have gone. */
        pc_snap_result target;
        if (ImGuizmo::IsUsing() && !rotate && !scale && modes
            && pc_overlay_snap(&ov, io.MousePos.x, io.MousePos.y, modes, pick_for_snap, &target)) {
            if (memcmp(position, target.position, sizeof target.position))
                dirty = true;
            memcpy(position, target.position, sizeof target.position);
            pc_overlay_draw_snap(&ov, draw, &target);
        }
        gizmo_busy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        /* Alt+drag (as in UE): a copy stays where the light or actor was
         * (moving or turning it; a scale has no copy). */
        if (ImGuizmo::IsUsing() && !was_using && io.KeyAlt && !scale) {
            pc_undo_hint(have_light ? "Duplicate light (Alt+drag)" : "Duplicate actor (Alt+drag)");
            /* The copy gets its own id (the selection, by id, stays on the
             * one being dragged; editor_scene.cpp assigns the new id). */
            if (have_light) {
                before_light.id.clear();
                lights.push_back(before_light);
            } else {
                before_actor.id.clear();
                before_actor.seed = new_seed();
                actors.push_back(before_actor);
            }
            dirty = true;
        }
        was_using = ImGuizmo::IsUsing();
    } else {
        was_using = false;
    }
    gizmo_busy |= handles_busy;
    /* Fog volumes (atmosphere.cpp): outlines, the selected one's gizmo (off while a handle has the mouse). */
    if (pc_atmosphere_viewport(v.view, v.projection, v.rect, draw))
        gizmo_busy = true;
    ImGuizmo::Enable(true);
    /* Particle emitters (particles_editor.cpp): outlines, icons, the selected one's gizmo. */
    if (pc_particles_viewport(v.view, v.projection, v.rect, draw))
        gizmo_busy = true;
    /* A box select: drawn while the button is held; on release it selects
     * what it encloses (Shift adds, Ctrl toggles), or, when the mouse hardly
     * moved, it was a click on empty space. */
    if (marquee.pending) {
        ImVec2 a(marquee.x, marquee.y), b = io.MousePos;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            marquee.dragging = true;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (marquee.dragging) {
                ImDrawList* front = ImGui::GetForegroundDrawList();
                ImVec2 lo(fminf(a.x, b.x), fminf(a.y, b.y)), hi(fmaxf(a.x, b.x), fmaxf(a.y, b.y));
                front->AddRectFilled(lo, hi, IM_COL32(120, 180, 255, 40));
                front->AddRect(lo, hi, IM_COL32(120, 180, 255, 220), 0, 0, 1.5f);
            }
            return;
        }
        Marquee m = marquee;
        marquee = Marquee();
        if (m.dragging) {
            rood_selection::box(rood_scene::in_box(m.x, m.y, b.x, b.y), m.shift, m.ctrl, rood_selection::FROM_VIEWPORT);
            rood_scene::publish();
        } else if (m.volume && (!m.ctrl || rood_selection::contains(m.volume))) {
            /* A click inside a fog volume picks it (Shift adds; Ctrl takes a
             * selected one out, else Ctrl+click still places on the surface). */
            rood_selection::click(m.volume, m.shift, m.ctrl, rood_selection::FROM_VIEWPORT);
            rood_scene::publish();
        } else {
            empty_click(m.x, m.y, m.shift, m.ctrl);
        }
        return;
    }
    /* pc_ui_shell_wants_mouse: the modern menus over the game picture take the click */
    if (io.WantCaptureMouse || pc_ui_shell_wants_mouse() || gizmo_busy || drag.active
        || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || io.KeyAlt)
        return;
    float world[3], normal[3];
    /* Fit rect light to window (editor_fit_window.cpp), when armed, takes the click. */
    if (rood_fit::viewport_click(io.MousePos.x, io.MousePos.y))
        return;
    /* A spot waiting to be aimed (its handle) or the colour popover's eyedropper takes the click. */
    if (rood_handles::viewport_click(io.MousePos.x, io.MousePos.y) || rood_colour::viewport_click(io.MousePos.x, io.MousePos.y))
        return;
    /* The emissive panel's texture picker (emissive_editor.cpp) takes the click when armed. */
    if (pc_emissive_viewport_click && pc_emissive_viewport_click(io.MousePos.x, io.MousePos.y))
        return;
    if (probe_mode) {
        if (v.pick(io.MousePos, world, normal)) {
            probe_valid = true;
            memcpy(probe_world, world, sizeof world);
            memcpy(probe_normal, normal, sizeof normal);
            probe_game_view = !(pc_editor_separate && pc_editor_separate());
            probe_uv[0] = (io.MousePos.x - v.rect[0]) / v.rect[2];
            probe_uv[1] = (io.MousePos.y - v.rect[1]) / v.rect[3];
        }
        return;
    }
    /* What is under the pointer: an icon (a light, an actor's pivot or one of
     * its lights, a glow). */
    rood_selection::Ref under = rood_scene::object_at(io.MousePos.x, io.MousePos.y);
    bool on_light = under.kind == rood_selection::KIND_LIGHT || under.kind == rood_selection::KIND_ACTOR;
    if (fixture_mode && !on_light) {
        /* The Light fixture tool: one click on a lantern, candle or glow. */
        light_fixture_at_pixel(io.MousePos.x, io.MousePos.y, "");
    } else if (rood_scene::ctrl_places(under, io.KeyCtrl)) {
        /* Ctrl+click on a glow places the current item there (glows sit on the surfaces one lights). */
        empty_click(io.MousePos.x, io.MousePos.y, false, true);
    } else if (under) {
        /* Unreal's: a click selects, Shift adds, Ctrl toggles. */
        rood_selection::click(under, io.KeyShift, io.KeyCtrl, rood_selection::FROM_VIEWPORT);
        rood_scene::publish();
    } else {
        /* Empty space, or only a fog volume (a misty room's floor is inside
         * one): a drag is a box select; on release without a drag, the fog
         * volume a ray from the pixel enters first is picked, else it was a
         * click on nothing (Ctrl places, Shift moves the selection there). */
        marquee = Marquee();
        marquee.pending = true;
        marquee.x = io.MousePos.x, marquee.y = io.MousePos.y;
        marquee.shift = io.KeyShift, marquee.ctrl = io.KeyCtrl;
        marquee.volume = rood_scene::volume_at(ov, pick_for_snap, io.MousePos.x, io.MousePos.y);
    }
}

/* The selection's primary for the editor's focus key (F) and the cutaway's focus. */
extern "C" int pc_lights_selected_position(float out[3], float* radius)
{
    return rood_scene::position(rood_selection::primary(), out, radius) ? 1 : 0;
}

/* Lights for the renderers, world space: the room's lights, then its
 * actors' lights evaluated for this frame (animated), then the editor's
 * ghosts (a drag's preview, suggestions), in that (stable) order.
 *
 * The Vulkan ray-traced path takes every light (PC_LIGHTS_MAX: lights in a
 * storage buffer, shadows sampled per pixel; pc/docs/Vulkan_Renderer.md).
 * Paths with a fixed budget (`max`: OpenGL, shadow maps, probes, the
 * editor's GL view) keep plain lights and ghosts and then the most
 * important actor lights: importance = brightness x (radius / distance)^2
 * from the camera (1 inside the light's reach) x 1 in view / 0.3 behind,
 * with hysteresis (lights kept last frame count 1.5x) so the choice does not
 * flicker as the camera moves. extent_right.w carries a point or spot
 * light's own emitting radius for soft shadows (0: the room's light size). */
namespace {
struct Candidate {
    Light l;
    float light_size;
    float importance; /* higher first; < 0: always kept */
    float shadow_skip; /* extent_up.w: shadow rays stop this short of the light (its fixture) */
    std::string key; /* actor id / light index: the hysteresis */
};
std::vector<Candidate> candidates;
int dropped_lights; /* left out by the present path's budget last frame */
int wanted_lights; /* lights the room wants (before any budget) */
/* Per budget: the lights it kept last frame (hysteresis). */
struct Kept {
    std::vector<std::string> keys;
    unsigned frame;
};
std::vector<std::pair<int, Kept>> kept_by_budget;
unsigned lights_frame = 1;
/* The last pc_lights_world output's keys and importance (shadow map picks). */
std::vector<std::string> output_keys;
std::vector<float> output_importance;
Kept* kept_for(int budget)
{
    for (auto& kb : kept_by_budget)
        if (kb.first == budget)
            return &kb.second;
    kept_by_budget.push_back({ budget, Kept { {}, 0 } });
    return &kept_by_budget.back().second;
}
}
static Light from_eval(const prefab::EvalLight& e)
{
    Light l;
    l.type = e.type;
    memcpy(l.position, e.position, sizeof l.position);
    memcpy(l.color, e.color, sizeof l.color);
    l.intensity = e.intensity;
    l.radius = e.radius;
    memcpy(l.direction, e.direction, sizeof l.direction);
    l.cone = e.cone;
    l.inner = e.inner;
    l.size[0] = e.size[0], l.size[1] = e.size[1];
    l.barn_angle = e.barn_angle, l.barn_length = e.barn_length;
    l.shadows = e.shadows;
    l.enabled = e.enabled;
    return l;
}
extern "C" int pc_lights_dropped(void) { return dropped_lights; }
extern "C" int pc_lights_wanted(void) { return wanted_lights; }
static int last_dropped;
extern "C" int pc_lights_last_dropped(void) { return last_dropped; }
extern "C" void pc_lights_set_dropped(int n) { dropped_lights = n; }
extern "C" void pc_lights_frame_tick(void) { ++lights_frame; }
extern "C" int pc_lights_world(float position[][4], float color[][4], float direction[][4],
    float extent_right[][4], float extent_up[][4], int shadows[], int max)
{
    last_dropped = 0;
    if (!pc_lights_enabled)
        return 0;
    candidates.clear();
    bool soloing = solo >= 0 || solo_actor >= 0;
    for (size_t i = 0; i < lights.size(); i++)
        if (lights[i].enabled && (!soloing || (int)i == solo) && !rood_scene::light_hidden((int)i))
            candidates.push_back({ lights[i], 0, -1, 0, std::string() });
    /* The eye and view direction (the scene camera) for the importance. */
    float eye[3] = { 0, 0, 0 }, forward[3] = { 0, 0, 1 }, r[3][3], t[3], h, ox, oy, inverse[3][3];
    bool have_eye = pc_scene_camera_get(r, t, &h, &ox, &oy) && pc_scene_camera_inverse(inverse);
    float tan_half = 2.0f;
    if (have_eye) {
        for (int k = 0; k < 3; k++)
            eye[k] = -(inverse[k][0] * t[0] + inverse[k][1] * t[1] + inverse[k][2] * t[2]);
        float fl = sqrtf(r[2][0] * r[2][0] + r[2][1] * r[2][1] + r[2][2] * r[2][2]);
        for (int k = 0; k < 3; k++)
            forward[k] = fl > 0 ? r[2][k] / fl : forward[k];
        if (h > 1)
            tan_half = 1.4f * 200.0f / h; /* the screen's half diagonal (widescreen too), generous */
    }
    float cos_half = 1.0f / sqrtf(1 + tan_half * tan_half), sin_half = tan_half * cos_half;
    std::vector<prefab::EvalLight> evaluated;
    for (size_t i = 0; i < actors.size(); i++) {
        if (!actors[i].enabled || (soloing && (int)i != solo_actor) || rood_scene::actor_hidden((int)i))
            continue;
        actor_lights(actors[i], evaluated);
        int k = 0;
        for (const prefab::EvalLight& e : evaluated) {
            ++k;
            if (!e.enabled)
                continue;
            float importance = 1;
            if (have_eye) {
                float v[3] = { e.position[0] - eye[0], e.position[1] - eye[1], e.position[2] - eye[2] };
                float d = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                float reach = fmaxf(e.radius, 1);
                float near = reach / fmaxf(d, reach);
                float along = v[0] * forward[0] + v[1] * forward[1] + v[2] * forward[2];
                float across = sqrtf(fmaxf(d * d - along * along, 0));
                bool in_view = d < reach || across * cos_half - along * sin_half < reach;
                float brightness = (0.3f * e.color[0] + 0.6f * e.color[1] + 0.1f * e.color[2]) * e.intensity;
                importance = fmaxf(brightness, 1e-4f) * near * near * (in_view ? 1.0f : 0.3f);
            }
            std::string key = (actors[i].id.empty() ? "#" + std::to_string(i) : actors[i].id) + "/" + std::to_string(k);
            candidates.push_back({ from_eval(e), e.light_size, importance, e.shadow_skip, key });
        }
    }
    std::vector<const Ghost*> shown;
    for (const Ghost& g : ghosts)
        shown.push_back(&g);
    for (const Ghost& g : drag_ghost)
        shown.push_back(&g);
    for (const Ghost* gp : shown) {
        const Ghost& g = *gp;
        if (!g.actor) {
            candidates.push_back({ g.light, 0, -1, 0, std::string() });
            continue;
        }
        actor_lights(g.a, evaluated);
        for (const prefab::EvalLight& e : evaluated)
            if (e.enabled)
                candidates.push_back({ from_eval(e), e.light_size, -1, e.shadow_skip, std::string() });
    }
    wanted_lights = (int)candidates.size();
    if ((int)candidates.size() > max) {
        /* Over this path's budget: the most important, with hysteresis. */
        Kept* kept = kept_for(max);
        std::vector<int> order(candidates.size());
        for (size_t i = 0; i < order.size(); i++)
            order[i] = (int)i;
        auto score = [&](const Candidate& c) {
            if (c.importance < 0)
                return 1e30f;
            bool was = std::find(kept->keys.begin(), kept->keys.end(), c.key) != kept->keys.end();
            return c.importance * (was ? 1.5f : 1.0f);
        };
        std::vector<float> scores(candidates.size());
        for (size_t i = 0; i < candidates.size(); i++)
            scores[i] = score(candidates[i]);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
        order.resize(max);
        std::sort(order.begin(), order.end()); /* back to the stable order */
        std::vector<Candidate> chosen;
        chosen.reserve(max);
        for (int i : order)
            chosen.push_back(candidates[i]);
        last_dropped = (int)candidates.size() - max;
        candidates.swap(chosen);
        if (kept->frame != lights_frame) { /* once per frame per budget */
            kept->frame = lights_frame;
            kept->keys.clear();
            for (const Candidate& c : candidates)
                if (!c.key.empty())
                    kept->keys.push_back(c.key);
        }
    }
    int n = 0;
    output_keys.clear();
    output_importance.clear();
    for (const Candidate& c : candidates) {
        output_keys.push_back(c.key);
        output_importance.push_back(c.importance < 0 ? 1e30f : c.importance);
        const Light& l = c.l;
        float forward[3], right[3], up[3];
        light_frame(l, forward, right, up);
        for (int i = 0; i < 3; i++) {
            position[n][i] = l.position[i];
            color[n][i] = l.color[i] * l.intensity * pc_lights_master;
            direction[n][i] = forward[i];
            extent_right[n][i] = right[i] * l.size[0] * 0.5f;
            extent_up[n][i] = up[i] * l.size[1] * 0.5f;
        }
        position[n][3] = l.radius;
        color[n][3] = (float)l.type;
        direction[n][3] = cosf(l.cone * 0.5f * 3.14159265f / 180.0f);
        extent_right[n][3] = l.type != 2 ? c.light_size : 0;
        extent_up[n][3] = c.shadow_skip;
        if (l.type == 1) /* the spot's inner cone (light_shape.h; its right axis is unused) */
            extent_right[n][0] = pc_spot_cos_inner(l.cone, l.inner);
        if (l.type == 2) /* the rect's barn doors (light_shape.h; its cone slot and own size are unused) */
            pc_barn_pack(l.barn_angle, l.barn_length, &direction[n][3], &extent_right[n][3]);
        shadows[n] = l.shadows && pc_shadow_mode != PC_SHADOWS_OFF; /* lighting quality: Shadows Off */
        ++n;
    }
    return n;
}

/* The Vulkan paths' list: every light, packed (light_list.h).
 * pc_lights_budget > 0 caps it as the fixed-budget paths are (comparisons,
 * RPC actors {budget}). */
extern "C" {
int pc_lights_budget = 0;
}
extern "C" int pc_lights_list(pc_light_gpu* out, int max)
{
    track_room();
    pc_lights_shadows_active = 0;
    if (pc_lights_budget > 0 && max > pc_lights_budget)
        max = pc_lights_budget;
    static float p[PC_LIGHTS_MAX][4], c[PC_LIGHTS_MAX][4], d[PC_LIGHTS_MAX][4], r[PC_LIGHTS_MAX][4], u[PC_LIGHTS_MAX][4];
    static int sh[PC_LIGHTS_MAX];
    if (max > PC_LIGHTS_MAX)
        max = PC_LIGHTS_MAX;
    int n = pc_lights_world(p, c, d, r, u, sh, max);
    bool has_patterns = n > 0 && std::any_of(candidates.begin(), candidates.end(), [](const Candidate& c) {
        return c.l.type == 2 && !c.l.pattern_faces.empty();
    });
    const auto texture_uses = has_patterns ? emissive::use_list(false) : std::vector<emissive::UseInfo>();
    std::unordered_map<std::string, const emissive::UseInfo*> by_key;
    std::unordered_map<unsigned, std::string> palette_keys;
    for (const auto& use : texture_uses) {
        if (use.refs <= 0 || use.entry.empty()) continue;
        std::string identity = use.entry + ":" + use.key;
        auto inserted = by_key.emplace(identity, &use);
        if (!inserted.second && inserted.first->second && inserted.first->second->clut != use.clut)
            inserted.first->second = nullptr; // unqualified references must not choose an arbitrary palette
        unsigned palette_id = ((use.page >> 7) << 16) | use.clut;
        auto palette = palette_keys.find(palette_id);
        if (palette == palette_keys.end())
            palette = palette_keys.emplace(palette_id, emissive::clut_key(use.page, use.clut)).first;
        if (!palette->second.empty()) by_key.emplace(identity + "@" + palette->second, &use);
    }
    struct PatternCache { std::string pixels; float cdf[256] = {}, mean = 0; };
    static std::unordered_map<std::string, PatternCache> pattern_cache;
    static uint32_t cached_map = 0, cached_catalog = 0;
    static int cached_zone = -1, cached_room = -1;
    uint32_t map = emissive::map_serial(), catalog = emissive::generation();
    if (map != cached_map || catalog != cached_catalog || zone != cached_zone || room != cached_room
        || pattern_cache.size() > PC_LIGHTS_MAX) {
        pattern_cache.clear();
        cached_map = map; cached_catalog = catalog; cached_zone = zone; cached_room = room;
    }
    std::unordered_map<const emissive::UseInfo*, std::string> content_keys;
    auto pattern_texel = [&](const rood_window::Face& face, float x, float y, float rgb[3], const emissive::Entry* entry) {
        if (!entry || !entry->enabled || !emissive::scope_allows(*entry, zone, room,
                emissive::SURFACE_ROOM, face.polygon, 0)) return 0;
        auto found = by_key.find(entry->id + ":" + face.key);
        if (found == by_key.end() || !found->second) return 0;
        const auto& use = *found->second;
        int w = use.box[2] - use.box[0] + 1, h = use.box[3] - use.box[1] + 1;
        unsigned tu = use.box[0] + std::min(w - 1, (int)(std::clamp(x, 0.0f, 1.0f) * w));
        unsigned tv = use.box[1] + std::min(h - 1, (int)(std::clamp(y, 0.0f, 1.0f) * h));
        return pc_emissive_texel(tu, tv, use.page, use.clut, rgb, nullptr);
    };
    pc_lights_set_dropped(pc_lights_last_dropped());
    for (int i = 0; i < n; i++) {
        memcpy(out[i].pos, p[i], sizeof p[i]);
        memcpy(out[i].col, c[i], sizeof c[i]);
        memcpy(out[i].dir, d[i], sizeof d[i]);
        memcpy(out[i].right, r[i], sizeof r[i]);
        memcpy(out[i].up, u[i], sizeof u[i]);
        memcpy(out[i].world, p[i], sizeof p[i]);
        out[i].info[0] = sh[i];
        pc_lights_shadows_active |= sh[i];
        out[i].info[1] = out[i].info[2] = out[i].info[3] = 0;
        memset(out[i].pattern_cdf, 0, sizeof out[i].pattern_cdf);
        Light source = candidates[i].l;
        /* User emissive edits are live, not generator input. Curated defaults
         * remain the generated/light-layer values, so per-light overrides work. */
        if (const auto* entry = emissive::find(source.pattern_entry);
            entry && entry->origin != emissive::ORIGIN_CURATED) {
            const auto* rect = entry->extra.get("rect_light");
            auto value = [&](const char* key, float fallback) {
                const auto* v = rect ? rect->get(key) : nullptr;
                if (!v) return fallback;
                return v->type == prefab::Json::Number ? (float)v->number
                    : v->type == prefab::Json::Bool ? (v->boolean ? 1.0f : 0.0f) : fallback;
            };
            source.pattern = value("pattern", (float)source.pattern) != 0;
            source.pattern_mode = value("pattern_mode", (float)source.pattern_mode) != 0;
            source.pattern_grid = (int)std::clamp(value("pattern_grid", (float)source.pattern_grid), 1.0f, 16.0f);
            source.pattern_elevation = std::clamp(value("pattern_elevation", source.pattern_elevation), -80.0f, 80.0f);
            source.pattern_spread = std::clamp(value("pattern_spread", source.pattern_spread), 0.01f, 15.0f);
        }
        memset(out[i].pattern_dir, 0, sizeof out[i].pattern_dir);
        if (source.type == 2 && source.pattern_mode) {
            float forward[3], right[3], up[3];
            light_frame(source, forward, right, up);
            float elevation = source.pattern_elevation * (3.14159265f / 180.0f);
            for (int k = 0; k < 3; ++k)
                out[i].pattern_dir[k] = forward[k] * cosf(elevation) - up[k] * sinf(elevation);
            out[i].pattern_dir[3] = tanf(source.pattern_spread * (3.14159265f / 360.0f));
            out[i].info[2] = 1;
            out[i].info[1] = 1;
            out[i].pattern_cdf[0] = 1; // pattern=0: same angular source, uniform aperture
        }
        if (source.type == 2 && source.pattern && !source.pattern_faces.empty()) {
            auto fs = rood_window::faces(source.pattern_faces);
            std::string pixels;
            for (auto& face : fs) {
                auto found = by_key.find(source.pattern_entry + ":" + face.key);
                if (found != by_key.end() && found->second) {
                    face.width = found->second->box[2] - found->second->box[0] + 1;
                    face.height = found->second->box[3] - found->second->box[1] + 1;
                    auto content = content_keys.find(found->second);
                    if (content == content_keys.end()) {
                        const auto& use = *found->second;
                        content = content_keys.emplace(found->second, emissive::content_key(use.page, use.clut,
                            use.box[0], use.box[1], use.box[2], use.box[3])).first;
                    }
                    pixels += content->second;
                } else pixels += "missing";
                pixels += ';';
            }
            out[i].info[1] = std::max(1, std::min(16, source.pattern_grid));
            const auto* entry = emissive::find(source.pattern_entry);
            auto sample = [&](const rood_window::Face& face, float x, float y, float rgb[3]) {
                return pattern_texel(face, x, y, rgb, entry);
            };
            std::string cache_key = source.pattern_entry + ":" + std::to_string(out[i].info[1]) + ":" + source.pattern_faces;
            auto& cached = pattern_cache[cache_key];
            if (cached.pixels != pixels) {
                cached.mean = rood_window::distribution(fs, out[i].info[1], cached.cdf, sample);
                cached.pixels = pixels;
            }
            memcpy(out[i].pattern_cdf, cached.cdf, sizeof cached.cdf);
            float mean = cached.mean;
            for (int k = 0; k < 3; ++k) out[i].col[k] *= mean;
            out[i].info[3] = (int)std::lround(mean * 65535.0f);
            if (mean <= 0) {
                /* Missing/disabled masks must not become a glowing solid rectangle. */
                out[i].info[1] = 0;
            }
        }
    }
    return n;
}
/* Shadow maps for the most important shadow casters only (light_list.h). */
extern "C" void pc_lights_shadow_pick(int count, int* wants, int slots)
{
    if ((int)output_keys.size() != count)
        return;
    int casters = 0;
    for (int i = 0; i < count; i++)
        casters += wants[i] != 0;
    if (casters <= slots)
        return;
    Kept* kept = kept_for(-slots);
    std::vector<int> order;
    std::vector<float> score(count, 0);
    for (int i = 0; i < count; i++)
        if (wants[i]) {
            order.push_back(i);
            bool was = !output_keys[i].empty()
                && std::find(kept->keys.begin(), kept->keys.end(), output_keys[i]) != kept->keys.end();
            score[i] = output_importance[i] * (was ? 1.5f : 1.0f);
        }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return score[a] > score[b]; });
    for (size_t k = slots; k < order.size(); k++)
        wants[order[k]] = 0;
    if (kept->frame != lights_frame) {
        kept->frame = lights_frame;
        kept->keys.clear();
        for (int k = 0; k < slots && k < (int)order.size(); k++)
            kept->keys.push_back(output_keys[order[k]]);
    }
}

/* ---- editor panels (dev_overlay.cpp lays them out) ---- */

extern "C" int pc_lights_room(int* z, int* r)
{
    track_room();
    *z = zone, *r = room;
    return dirty;
}
extern "C" void pc_lights_mark_dirty(void) { dirty = true; }
extern "C" void pc_lights_reload(void) { load_room(zone, room); }
extern "C" const char* pc_lights_status(void) { return status; }
extern "C" int* pc_lights_gizmo_operation(void) { return &gizmo_operation; }
/* RPC editor_transform (dev_rpc.c hands over the params object's text):
 * action state (default): the tool (0 move, 1 rotate, 2 scale), the space,
 * and the light / actor manipulator as last drawn (its subject, operation,
 * mode); scale {ratio [x, y, z], id (default the selection's primary)}: R as
 * the gizmo applies it (editor_transform.h), one undo step. */
extern "C" int pc_editor_transform_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_scene::frame();
    const prefab::Json* a = params.get("action");
    std::string action = a && a->type == prefab::Json::String ? a->string : "state";
    if (action == "scale") {
        rood_selection::Ref r = rood_selection::primary();
        if (const prefab::Json* id = params.get("id"))
            r = rood_selection::ref_of(id->string);
        const prefab::Json* ratio = params.get("ratio");
        int i = rood_scene::index_of(r);
        if (i < 0 || r.kind == rood_selection::KIND_GLOW || !ratio || ratio->type != prefab::Json::Array
            || ratio->items.size() != 3) {
            snprintf(out, size, "scale needs ratio [x, y, z] and a light, light actor, fog volume or emitter (id or the selection)");
            return -1;
        }
        float k[3] = { (float)ratio->items[0].number, (float)ratio->items[1].number, (float)ratio->items[2].number };
        scale_object(r, k, r.kind == rood_selection::KIND_LIGHT ? lights[i] : Light(),
            r.kind == rood_selection::KIND_ACTOR ? actors[i] : prefab::Actor());
    } else if (action != "state") {
        snprintf(out, size, "action: state or scale");
        return -1;
    }
    bool shown = ImGui::GetCurrentContext() && gizmo_shown.frame >= ImGui::GetFrameCount() - 1;
    std::string s = "{\"operation\":" + std::to_string(gizmo_operation) + ",\"space\":\""
        + (pc_lights_gizmo_space ? "local" : "world") + "\",\"gizmo\":{\"shown\":" + (shown ? "true" : "false")
        + ",\"subject\":" + (shown ? prefab::json_quote(gizmo_shown.subject) : std::string("null")) + ",\"operation\":\""
        + (shown ? gizmo_shown.operation : "") + "\",\"mode\":\"" + (shown ? gizmo_shown.mode : "") + "\"}}";
    snprintf(out, size, "%s", s.c_str());
    return (int)s.size() < size ? 0 : -1;
}
extern "C" bool* pc_lights_show(void) { return &pc_lights_show_gizmos; }
extern "C" int pc_lights_selection(void) { return selected >= 0 && selected < (int)lights.size() ? selected : -1; }
extern "C" void pc_lights_select(int i) { select_light(i); }

static void add_light_at_ashley()
{
    if (!pc_dev_state)
        return;
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    pc_dev_state(&s);
    Light l;
    l.position[0] = (float)s.x;
    l.position[1] = (float)s.y - 160; // above his head (y grows downward)
    l.position[2] = (float)s.z;
    lights.push_back(l);
    select_light((int)lights.size() - 1);
    dirty = true;
}

/* Outliner rows for the room's lights and actors: click to select (Ctrl
 * adds lights to the selection, for Create prefab), eye to mute, S to solo
 * (only that light or actor lights the scene); an actor expands to its
 * lights. */
extern "C" void pc_lights_outliner(const char* filter)
{
    track_room();
    layers_outliner_header();
    for (size_t i = 0; i < lights.size(); i++) {
        Light& l = lights[i];
        char label[48];
        snprintf(label, sizeof label, "%s %zu", l.type == 1 ? "Spot light" : l.type == 2 ? "Rect light" : "Point light",
            i + 1);
        if (filter && filter[0] && !strcasestr(label, filter))
            continue;
        ImGui::PushID((int)i);
        if (ImGui::Checkbox("##on", &l.enabled))
            dirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enabled (mute)");
        ImGui::SameLine();
        bool soloed = solo == (int)i;
        if (soloed)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.6f, 0.1f, 1));
        if (ImGui::SmallButton("S")) {
            solo = soloed ? -1 : (int)i;
            solo_actor = -1;
        }
        if (soloed)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Solo: light the scene with this light only");
        ImGui::SameLine();
        ImGui::ColorButton("##swatch", ImVec4(l.color[0], l.color[1], l.color[2], 1), ImGuiColorEditFlags_NoTooltip,
            ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
        ImGui::SameLine();
        rood_selection::Ref ref = rood_scene::light_ref((int)i);
        bool hidden = rood_selection::hidden(ref);
        bool silent = !l.enabled || (solo >= 0 && solo != (int)i) || solo_actor >= 0 || hidden; /* not lighting the scene */
        if (silent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        std::string shown = hidden ? std::string(label) + "  [hidden]###" + label : std::string(label);
        if (ImGui::Selectable(shown.c_str(), rood_selection::contains(ref)))
            rood_scene::row_clicked(ref);
        rood_scene::row_drawn(ref);
        if (silent)
            ImGui::PopStyleColor();
        layer_row(light_item_id((int)i)); /* layer badge, revert / promote menu */
        ImGui::PopID();
    }
    actors_outliner(filter);
    extern int pc_lights_dropped(void);
    if (pc_lights_dropped() > 0)
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%d lights left out by this renderer (OpenGL / no ray tracing: 16, the most important kept; Vulkan ray tracing takes all)",
            pc_lights_dropped());
    if (ImGui::SmallButton("+ Light at Ashley"))
        add_light_at_ashley();
    ImGui::SameLine();
    if (suggest_state.load() == 1)
        ImGui::TextDisabled("Suggesting...");
    else if (ImGui::SmallButton("Suggest from vertex colours"))
        suggest();
}

/* Details > a light's own actions under its fields (editor_schema.cpp
 * draws those): Duplicate, Delete, and Create prefab from the selected
 * lights (they are replaced by an actor of it). */
void rood_lights::light_details_extra(int index)
{
    if (index < 0 || index >= (int)lights.size())
        return;
    Light& l = lights[index];
    ImGui::PushID(index);
    if (ImGui::Button("Duplicate")) {
        pc_undo_hint((std::string("Duplicate ") + light_type_name(l.type) + " " + std::to_string(index + 1)).c_str());
        Light copy = l;
        copy.id.clear(); /* its own id: the selection (by id) goes to the copy */
        copy.position[0] += 64;
        lights.push_back(copy);
        select_light((int)lights.size() - 1);
        dirty = true;
        ImGui::PopID();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        pc_undo_hint((std::string("Delete ") + light_type_name(l.type) + " " + std::to_string(index + 1)).c_str());
        lights.erase(lights.begin() + index);
        if (solo == index)
            solo = -1;
        else if (solo > index)
            --solo;
        select_none();
        dirty = true;
        ImGui::PopID();
        return;
    }
    /* Create prefab from selection: this light (and the other selected
     * lights) become a prefab, replaced here by an actor of it. */
    ImGui::SameLine();
    static char name[64];
    if (ImGui::Button("Create prefab..."))
        ImGui::OpenPopup("create prefab");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Make a prefab of the selected light%s (Shift or Ctrl+click adds more);\n"
                          "they are replaced by an actor of it",
            multi.empty() ? "" : "s");
    if (ImGui::BeginPopup("create prefab")) {
        ImGui::Text("Prefab from %zu light%s", multi.size() + 1, multi.empty() ? "" : "s");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputTextWithHint("##name", "Name (e.g. Chandelier)", name, sizeof name,
            ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Create") || enter) && name[0]) {
            if (create_prefab_from_selection(name))
                name[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (l.type == 2) {
        if (ImGui::Button("Fit to window"))
            rood_fit::arm(rood_scene::light_ref(index));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Click a window pane in the view: this rect light takes its size and sits in front of it,\n"
                              "facing out (Esc cancels)");
        ImGui::SameLine();
        if (ImGui::SmallButton("...##fit"))
            ImGui::OpenPopup("fit options");
        if (ImGui::BeginPopup("fit options")) {
            rood_fit::options();
            ImGui::EndPopup();
        }
    }
    ImGui::PopID();
}

/* Settings shared by every room (not saved per room). */
extern "C" void pc_lights_global_settings(void)
{
    bool enabled = pc_lights_enabled != 0;
    if (ImGui::Checkbox("Dynamic lights", &enabled))
        pc_lights_enabled = enabled;
    ImGui::SliderFloat("Shadow map softness", &pc_shadow_softness, 0, 0.08f, "%.3f");
    ImGui::SliderFloat("Normal smoothing", &pc_normal_smoothing, 0, 12, "%.1f px");
    static const char* drop_modes[] = { "Auto (off with traced shadows)", "Original", "Contact only", "Off" };
    if (&pc_drop_shadow_mode)
        ImGui::Combo("Drop shadows", &pc_drop_shadow_mode, drop_modes, 4);
    bool animated = animate != 0;
    if (ImGui::Checkbox("Animate lights (flicker, pulse)", &animated))
        animate = animated;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Light actors' flicker and pulse (settings key light_animation)");
}

extern "C" bool* pc_lights_probe_mode(void) { return &probe_mode; }

/* Details > Inspector: the room texture effect (animated material) of the
 * probed polygon (room_effects.c): what the game runs, what is drawn, and
 * the room's overrides (saved with the room). */
static void texture_effect_details()
{
    int count = 0;
    const pc_uv_effect* effects = pc_uv_effects(&count);
    int triangle = pc_scene_triangle_at(probe_world, 4.0f), n = 0;
    const scene_triangle* tris = pc_scene_triangle_data(&n);
    int index = -1;
    for (int i = 0; triangle >= 0 && triangle < n && i < count && index < 0; i++)
        if (effects[i].polygon == tris[triangle].polygon)
            index = i;
    if (index < 0) {
        if (count)
            ImGui::TextDisabled("Texture effect: none here (%d in the room)", count);
        return;
    }
    const pc_uv_effect* e = &effects[index];
    ImGui::SeparatorText("Texture effect (animated material)");
    ImGui::Text("Effect %d of %d: %s, game slot %d, polygon at 0x%x (%s)", index + 1, count,
        e->type == 2 ? "UV scroll" : "UV atlas frames", e->slot, e->polygon, e->quad ? "quad" : "triangle");
    ImGui::Text("Game: %s, steps every %d ticks (tick %d)", e->running < 0 ? "state unknown" : e->running ? "running" : "stopped",
        e->delay + 1, e->tick);
    if (e->type == 2) {
        ImGui::Text("Window %dx%d in region %d,%d %dx%d", e->window[0], e->window[1], e->base[0], e->base[1], e->size[0],
            e->size[1]);
        ImGui::Text("Step %+d,%+d texels; wraps every %d,%d", e->step[0], e->step[1], e->period[0], e->period[1]);
    } else {
        ImGui::Text("%d frames of %dx%d in %d columns from %d,%d (frame %d)", e->frames, e->frame_size[0],
            e->frame_size[1], e->columns, e->base[0], e->base[1], e->frame);
    }
    ImGui::Text("UV offset: game %d,%d, drawn %d,%d (moving %.0f,%.0f per frame)", e->game_offset[0], e->game_offset[1],
        e->offset[0], e->offset[1], e->shift[0], e->shift[1]);
    if (!e->modelled) {
        ImGui::TextDisabled("Not modelled: its polygon is re-decoded when the game rewrites it.");
        return;
    }
    float speed = e->speed;
    bool freeze = e->freeze != 0;
    bool overridden = speed != 1 || freeze;
    if (overridden)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.3f, 1));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    bool changed = e->type == 2 && ImGui::SliderFloat("Speed (this room)", &speed, 0.0f, 4.0f, "%.2f");
    changed |= ImGui::Checkbox("Freeze (this room)", &freeze);
    if (overridden)
        ImGui::PopStyleColor();
    if (overridden) {
        ImGui::SameLine();
        if (ImGui::SmallButton("reset##uv")) {
            speed = 1, freeze = false;
            changed = true;
        }
    }
    if (changed) {
        pc_uv_effect_override(index + 1, speed, freeze);
        dirty = true;
    }
}

/* Details > Inspector: the probed point and every light that reaches it. */
extern "C" void pc_lights_inspector(void)
{
    ImGui::Checkbox("Inspect (I): click the view to probe a surface", &probe_mode);
    if (!probe_valid) {
        ImGui::TextDisabled("No point probed yet.");
        return;
    }
    ImGui::Text("Position %.0f %.0f %.0f", probe_world[0], probe_world[1], probe_world[2]);
    ImGui::Text("Normal %.2f %.2f %.2f", probe_normal[0], probe_normal[1], probe_normal[2]);
    texture_effect_details();
    if (probe_gi_ok) {
        ImGui::SeparatorText("Path tracer at this pixel");
        ImGui::Text("Indirect light %.3f %.3f %.3f", probe_gi[0], probe_gi[1], probe_gi[2]);
        ImGui::Text("Shadow visibility (lights) %.2f", probe_gi[3]);
        ImGui::Text("Baked light visibility %.2f", probe_gi[4]);
        ImGui::Text("Haze light %.3f %.3f %.3f", probe_gi[5], probe_gi[6], probe_gi[7]);
    } else {
        ImGui::TextDisabled(probe_game_view ? "No path tracer result (PTGI off?)"
                                            : "Path tracer values are read in the game view.");
    }
    ImGui::SeparatorText("Lights reaching this point");
    if (ImGui::BeginTable("sources", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Light");
        ImGui::TableSetupColumn("Distance");
        ImGui::TableSetupColumn("Falloff x N.L");
        ImGui::TableSetupColumn("Ray");
        ImGui::TableHeadersRow();
        for (const Source& src : probe_sources) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::ColorButton("##c", ImVec4(src.color[0], src.color[1], src.color[2], 1), ImGuiColorEditFlags_NoTooltip,
                ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            ImGui::SameLine();
            ImGui::TextUnformatted(src.name);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", src.distance);
            ImGui::TableNextColumn();
            if (src.weight > 0)
                ImGui::Text("%.3f", src.weight);
            else
                ImGui::TextDisabled("out of reach");
            ImGui::TableNextColumn();
            if (src.blocker < 0)
                ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "clear");
            else
                ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "blocked by %s at %.0f",
                    src.blocker & 8 ? "character/prop" : "room", src.hit_t);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Rays test geometry only (texture cut-outs such as fences are not tested).");
}

/* The room file changed on disk (edited by hand or by a script): reload it,
 * unless the editor has unsaved changes for the room (then say so). */
extern "C" void pc_lights_poll(void)
{
    /* Prefab files (built-in and the user's) reload live too: every
     * instance follows its prefab. */
    if (prefab::library().poll())
        snprintf(status, sizeof status, "Prefabs reloaded (%zu)", prefab::library().prefabs.size());
    /* The fixture catalog too: every instance of a type follows it. */
    if (fixtures::poll())
        snprintf(status, sizeof status, "Fixture catalog reloaded (%zu types)", fixtures::catalog().types.size());
    layers_poll(); /* every layer of the room (lighting_editor.cpp) */
}

/* Outliner rows for the game's glows (and baked estimates), by id with a
 * readable name: click to select, checkbox to mute (saved with the room),
 * S to solo. Glows whose source is out (nothing drawn lately) are greyed. */
extern "C" void pc_lights_glows_outliner(const char* filter)
{
    follow_glow();
    static pc_glow_info glows[160];
    int n = pc_gi_glow_list(glows, 160);
    glows_outliner_actions();
    /* Transient effects (attacks, spells, sparks): one row for the kind. */
    int effects = 0;
    for (int i = 0; i < n; i++)
        effects += glows[i].category == PC_GLOW_EFFECT && glows[i].presence >= 0.02f;
    {
        ImGui::PushID("effects");
        bool on = pc_glow_category[PC_GLOW_EFFECT][0] > 0.5f;
        if (ImGui::Checkbox("##on", &on))
            pc_glow_category[PC_GLOW_EFFECT][0] = on ? 1.0f : 0.0f; /* live; Details sets it for the room */
        ImGui::SameLine();
        ImGui::TextDisabled("Effect glows (%d active)", effects);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Attacks, spells, hit sparks and other short-lived effects: controlled as a kind\n"
                              "(Details > Lighting > Game glows by kind); they never cast shadows.");
        ImGui::PopID();
    }
    if (!n)
        ImGui::TextDisabled("None in this room");
    for (int i = 0; i < n; i++) {
        const pc_glow_info& g = glows[i];
        if (g.category == PC_GLOW_EFFECT)
            continue;
        char label[96];
        snprintf(label, sizeof label, "%s  (%.0f %.0f %.0f)", g.name, g.position[0], g.position[1], g.position[2]);
        if (filter && filter[0] && !strcasestr(label, filter))
            continue;
        ImGui::PushID((int)g.id);
        bool on = g.muted != 1;
        if (ImGui::Checkbox("##on", &on)) {
            if (pc_glow_override* o = pc_gi_glow_override_id(g.id, 1))
                o->muted = !on;
            dirty = true;
        }
        ImGui::SameLine();
        bool soloed = pc_gi_glow_soloed_id(g.id);
        if (soloed)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.6f, 0.1f, 1));
        if (ImGui::SmallButton("S"))
            pc_gi_glow_solo_id(g.id);
        if (soloed)
            ImGui::PopStyleColor();
        ImGui::SameLine();
        float lit = g.presence > 0.02f ? g.presence : 1.0f;
        ImGui::ColorButton("##c", ImVec4(fminf(g.color[0] / lit, 1), fminf(g.color[1] / lit, 1), fminf(g.color[2] / lit, 1), 1),
            ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
        ImGui::SameLine();
        rood_selection::Ref ref = rood_scene::glow_ref_key(g.key, g.position);
        bool is_selected = rood_selection::contains(ref);
        bool silent = g.muted != 0 || g.presence < 0.02f;
        if (silent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Selectable(label, is_selected))
            rood_scene::row_clicked(ref);
        rood_scene::row_drawn(ref);
        if (silent)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Glow %u, %s\n%d glowing triangles this frame%s%s%s%s%s", g.id, g.key, g.triangles,
                g.presence < 0.02f ? "\nout: its source drew nothing lately" : "",
                g.traced ? "" : "\nnot among the glows the tracers use this frame",
                g.fixture_type[0] ? "\nfixture type " : "", g.fixture_type,
                g.muted == 4 ? ": not a light (suppressed by its type)" : "");
        if (g.overridden)
            layer_row(std::string("glow:") + g.key); /* its override's layers */
        ImGui::PopID();
    }
}

/* Details of the selected glow: its overrides (saved with the room, by the
 * glow's key). */
extern "C" bool pc_lights_glow_details(void)
{
    if (!glow_selected)
        return false;
    follow_glow();
    pc_glow_info g;
    bool found = glow_id && pc_gi_glow_get(glow_id, &g);
    ImGui::SeparatorText(found ? g.name : "Glow (from the game)");
    if (!found) {
        ImGui::TextDisabled("Gone: its source no longer draws (last at %.0f %.0f %.0f).", glow_at[0], glow_at[1], glow_at[2]);
        return true;
    }
    static const char* origins[] = { "?", "room polygons", "an object", "a particle system", "effect packets", "baked light" };
    ImGui::TextDisabled("Glow %u from %s, at %.0f %.0f %.0f (smoothed)", g.id,
        origins[g.origin >= 1 && g.origin <= 5 ? g.origin : 0], g.position[0], g.position[1], g.position[2]);
    ImGui::TextDisabled("Key %s", g.key);
    ImGui::TextDisabled("%d triangles this frame, %s", g.triangles,
        g.presence < 0.02f ? "out" : g.traced ? "lighting the scene" : "not among the 32 traced");
    pc_glow_override* existing = pc_gi_glow_override_id(glow_id, 0);
    pc_glow_override shown = existing ? *existing : pc_glow_override { { 0, 0, 0 }, 0, 1, { 1, 1, 1 }, 0, 0, "", 0 };
    bool changed = false;
    bool enabled = !shown.muted;
    changed |= ImGui::Checkbox("Enabled", &enabled);
    shown.muted = !enabled;
    changed |= ImGui::SliderFloat("Intensity", &shown.intensity, 0, 8, "%.2f");
    changed |= ImGui::ColorEdit3("Tint", shown.tint);
    changed |= ImGui::SliderFloat("Emitting size", &shown.size, 0, 64, shown.size > 0 ? "%.1f" : "as found");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("The radius of its emitting sphere, or of its column");
    changed |= ImGui::SliderFloat("Reach", &shown.reach, 0, 4000, shown.reach > 0 ? "%.0f" : "Glow reach");
    {
        /* Its shape (glows.c fit_column): a column for a group a character
         * can stand in (the save point), so standing in it keeps its light. */
        static const char* shapes[] = { "As found", "Sphere", "Column" };
        changed |= ImGui::Combo("Shape", &shown.shape, shapes, 3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("As found: a column when its polygons enclose room for a character (the save point),\n"
                              "else a sphere at its centre. A character inside a column hides only the part behind them.");
        if (g.shape[3] != 0)
            ImGui::TextDisabled("Column: %.0f high, radius %.0f%s", 2 * sqrtf(g.shape[0] * g.shape[0] + g.shape[1] * g.shape[1]
                + g.shape[2] * g.shape[2]), fabsf(g.shape[3]), g.shape[3] < 0 ? " (solid)" : " (its side)");
    }
    if (changed) {
        if (pc_glow_override* o = pc_gi_glow_override_id(glow_id, 1)) {
            pc_glow_override keep = *o;
            *o = shown;
            memcpy(o->position, keep.position, sizeof keep.position);
            memcpy(o->key, keep.key, sizeof keep.key);
            o->bound = keep.bound;
        }
        dirty = true;
    }
    ImGui::TextDisabled("Kind: %s (%s). Its kind's switch, intensity, reach, shadows and smoothing:\n"
                        "Details > Lighting > Game glows by kind.",
        g.category == PC_GLOW_MAGIC ? "magic" : "fixture", g.casts_shadows ? "casts shadows" : "no shadows");
    if (existing && existing->fixture_type[0]) {
        /* A catalogued fixture's glow (generated layer): its type decides. */
        const fixtures::Type* t = fixtures::find(existing->fixture_type);
        ImGui::Text("Fixture type %s%s%s: its glow is %s", existing->fixture_type, t ? " " : "",
            t ? t->description.c_str() : " (not in the catalog)", t ? fixtures::glow_name(t->glow) : "left");
        if (g.muted == 4)
            ImGui::TextDisabled("Suppressed: not a light (every instance of the type; pc/assets/fixtures/catalog.json)");
        if (ImGui::SmallButton("Ignore its type here")) {
            existing->fixture_type[0] = 0;
            dirty = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("This glow only: the type's treatment no longer applies (saved in your layer by the glow's key)");
    }
    if (existing && ImGui::Button("Reset to the game's glow")) {
        pc_gi_glow_remove_id(glow_id);
        dirty = true;
    }
    glow_details_actions();
    return true;
}

/* An engine reload (engine_reload.c) waits for no helper: it is refused
 * while the light suggestion's background fit runs. */
extern "C" int pc_lights_busy(void) { return suggest_state.load() == 1; }
