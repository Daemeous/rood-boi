// The model studio, the host side (studio.h; pc/docs/plans/2026-11-22-model-studio-and-paint-channels-design.md, P2):
// a neutral workshop in which any model can be summoned and looked at from every side.
//
// It is a client of the photo mode (photo.h): entering holds the game at a vsync boundary M ("studio" owns the pause, the
// camera session and the hidden HUD), and every picture is a photo mode render. A render starts at M and runs the game a few
// frames from it, then rewinds to M with the picture kept. studio_render (the photo Part's `render` callback) is called as a
// render begins, with the game at M and about to run: it hands studio.c (the game side) the stage and the model to build, and
// everything built there is gone again when the render ends. So the studio changes nothing of the game it holds, and leaving
// it (releasing the pause) goes on from M exactly.
//
// Everything that is a look or a feel is data: pc/assets/studio/studio.json (the stage, the camera, the looks, the catalog's
// paths), with the user's overlay (pc/user/studio/studio.json, ROOD_STUDIO) deep-merged over it; RPC `studio` reads and sets
// any member by its path.
#include "photo.h"
#include "photo_internal.h"
#include "studio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "atmosphere.h"
#include "occlusion.h"
#include "editor_shell.h"
#include "imgui.h"
#include "cutscene_sequencer.h"
#include "light_prefabs.h"
#include "platform.h"
#include "ui/item_names.h"
#include "ui/mon_parse.h"
#include "ui_text.h"
#include "editor_overlays.h"
#include "emissive_internal.h"
#include "render.h"
#include "studio_locate.h"

extern "C" {
extern int pc_hw_interpolate __attribute__((weak));
extern int pc_interp_holds __attribute__((weak)); /* actor_native.c: a held pose's next change is spread over the hold */
int pc_studio_hide_bodies;
int pc_env_studio(const char* json);
void pc_glows_keep(void);
void pc_glows_give_back(void);
void pc_editor_photo_set(const float eye[3], float yaw, float pitch, float roll, float fov) __attribute__((weak));
int pc_studio_scene(const pc_studio_scene_cfg* cfg, pc_studio_scene_out* out) __attribute__((weak));
void pc_studio_bounds_reset(void) __attribute__((weak));
void pc_studio_bounds_stop(void) __attribute__((weak));
int pc_studio_bounds_get(float lo[3], float hi[3]) __attribute__((weak));
int pc_studio_meshes(unsigned* mesh, unsigned long long* key, int max) __attribute__((weak));
int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick) __attribute__((weak));
int render_mesh_posed(render_mesh mesh, render_posed_triangle* out, int max) __attribute__((weak));
}

namespace {
using prefab::Json;
constexpr const char* OWNER = "studio";
constexpr float PI_F = 3.14159265f;

// ---------------------------------------------------------------- the data

Json config;      // studio.json with the user's overlay merged over it
bool config_loaded;
Json catalog;     // catalog.json
bool catalog_loaded;
std::string load_error;

std::string read_text(const std::string& path)
{
    std::string text;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    fclose(f);
    return text;
}
const char* env_or(const char* name, const char* fallback)
{
    const char* e = getenv(name);
    return e && *e ? e : fallback;
}
std::string shipped_path() { return env_or("ROOD_STUDIO_SHIPPED", "pc/assets/studio/studio.json"); }
std::string user_path() { return env_or("ROOD_STUDIO", "pc/user/studio/studio.json"); }
std::string catalog_path() { return env_or("ROOD_STUDIO_CATALOG", "pc/assets/studio/catalog.json"); }

void merge(Json& into, const Json& over)
{
    if (into.type != Json::Object || over.type != Json::Object) {
        into = over;
        return;
    }
    for (const auto& m : over.members) {
        Json* mine = into.get(m.first.c_str());
        if (mine && mine->type == Json::Object && m.second.type == Json::Object)
            merge(*mine, m.second);
        else
            into.set(m.first.c_str(), m.second);
    }
}
bool parse_file(const std::string& path, Json& out, bool required)
{
    std::string text = read_text(path);
    if (text.empty()) {
        if (required)
            load_error = path + ": cannot read";
        return !required;
    }
    std::string error;
    if (!prefab::json_parse(text.c_str(), out, &error) || out.type != Json::Object) {
        load_error = path + ": " + (error.empty() ? "not an object" : error);
        return false;
    }
    return true;
}
void load_config()
{
    config_loaded = true;
    load_error.clear();
    Json base = Json::object(), user = Json::object();
    if (!parse_file(shipped_path(), base, true))
        return;
    if (!parse_file(user_path(), user, false))
        user = Json::object();
    merge(base, user);
    config = base;
}
Json& cfg()
{
    if (!config_loaded)
        load_config();
    return config;
}
// A member by its dotted path ("looks.neutral.sun.intensity"); null when absent.
const Json* at(const std::string& path)
{
    const Json* j = &cfg();
    size_t from = 0;
    while (j && from <= path.size()) {
        size_t dot = path.find('.', from);
        std::string key = path.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
        j = j->type == Json::Object ? j->get(key.c_str()) : nullptr;
        if (dot == std::string::npos)
            break;
        from = dot + 1;
    }
    return j;
}
float num(const std::string& path, float fallback)
{
    const Json* j = at(path);
    return j && j->type == Json::Number ? (float)j->number : fallback;
}
bool flag(const std::string& path)
{
    const Json* j = at(path);
    return j && (j->type == Json::Bool ? j->boolean : j->type == Json::Number && j->number != 0);
}
bool set_at(const std::string& path, const Json& value)
{
    Json* j = &cfg();
    size_t from = 0;
    for (;;) {
        size_t dot = path.find('.', from);
        std::string key = path.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
        if (j->type != Json::Object)
            return false;
        if (dot == std::string::npos) {
            j->set(key.c_str(), value);
            return true;
        }
        Json* next = j->get(key.c_str());
        if (!next) {
            j->set(key.c_str(), Json::object());
            next = j->get(key.c_str());
        }
        j = next;
        from = dot + 1;
    }
}
bool save_config(std::string* error)
{
    std::string text = prefab::json_write(cfg()) + "\n", path = user_path(), temp = path + ".tmp";
    for (size_t i = 1; i < path.size(); i++)
        if (path[i] == '/')
            pc_mkdir(path.substr(0, i).c_str());
    FILE* f = fopen(temp.c_str(), "wb");
    bool ok = f && fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f)
        fclose(f);
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    return true;
}
void load_catalog()
{
    catalog_loaded = true;
    catalog = Json::object();
    std::string text = read_text(catalog_path()), error;
    if (text.empty() || !prefab::json_parse(text.c_str(), catalog, &error) || catalog.type != Json::Object) {
        load_error = catalog_path() + ": " + (error.empty() ? "cannot read" : error);
        catalog = Json::object();
    }
}

// ---------------------------------------------------------------- the catalog

struct Item {
    std::string kind; // "character", "enemy", "weapon"
    int id = -1;      // SHP / WEP model number, or the Bestiary's ZUD number
    std::string file; // the file's name on the disc ("OBJ/00.SHP", "MAP/Z003U00.ZUD")
    std::string name;
};
std::vector<Item> items;
bool items_built;

std::string hex2(int n)
{
    char b[8];
    snprintf(b, sizeof b, "%02X", n);
    return b;
}
// The Bestiary's names (SMALL/MON.BIN, as the Score screen reads them): zud number -> name.
std::string enemy_name(int zud)
{
    static bool read;
    static std::vector<std::pair<int, std::string>> names;
    if (!read) {
        read = true;
        unsigned char* data = nullptr;
        unsigned size = 0;
        if (pc_ui_disc_read("SMALL/MON.BIN", &data, &size)) {
            static const int all[PC_MON_FLAG_WORDS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
            static pc_mon_file parsed;
            pc_mon_parse(data, size, all, &parsed);
            for (int i = 0; parsed.ok && i < PC_MON_ENEMIES; i++) {
                char text[96];
                int at = parsed.e[i].name_at, len = parsed.e[i].name_len;
                if (at >= 0 && at + len <= (int)size && len > 0 && pc_ui_text_decode(data + at, len, text, sizeof text) > 0)
                    names.emplace_back(parsed.e[i].zud, text);
            }
            free(data);
        }
    }
    for (const auto& n : names)
        if (n.first == zud)
            return n.second;
    return "";
}
void build_items()
{
    if (items_built)
        return;
    items_built = true;
    if (!catalog_loaded)
        load_catalog();
    if (const Json* a = catalog.get("characters"))
        for (const Json& e : a->items) {
            int id = (int)e.number;
            items.push_back({ "character", id, "OBJ/" + hex2(id) + ".SHP", "Character " + hex2(id) });
        }
    if (const Json* a = catalog.get("enemies"))
        for (const Json& e : a->items) {
            const Json* id = e.get("id");
            const Json* file = e.get("file");
            if (!id || !file)
                continue;
            std::string name = enemy_name((int)id->number);
            items.push_back({ "enemy", (int)id->number, "MAP/" + file->string + ".ZUD",
                name.empty() ? "Enemy " + std::to_string((int)id->number) : name });
        }
    if (const Json* a = catalog.get("weapons"))
        for (const Json& e : a->items) {
            int id = (int)e.number;
            items.push_back({ "weapon", id, "OBJ/" + hex2(id) + ".WEP", "Weapon " + hex2(id) });
        }
}
const Item* find_item(const std::string& kind, int id)
{
    build_items();
    for (const Item& i : items)
        if (i.kind == kind && i.id == id)
            return &i;
    return nullptr;
}

// ---------------------------------------------------------------- the state

struct Camera {
    float yaw = 200, elevation = 12; // degrees; elevation above the horizon
    float zoom = 1;                  // times the fitting distance
    float fov = 40;
    float target[3] = { 0, 0, 0 };   // world
    float distance = 600;            // the fitting distance, world units (from the model's box)
};
bool dirty; // a change asked for while a render was running (photo.cpp drops such a request): asked again when it lands
void request()
{
    dirty = true;
    ::pc_photo_redraw();
}
float model_angle, light_angle; // the turntable's, degrees in [0, 360)
bool turn_was_running;
double turn_last, landed_at;
bool entered;
bool leaving;        // left, and waiting for the render that shows the game as it is (the stage was the last picture)
bool glows_kept;     // the room's glow tracks are saved for this visit
bool clean_seen;     // that render has begun
std::string look_name = "neutral";
const Item* current;
std::vector<unsigned char> file_data, rig_data, base_data;
int base_id = -1;
Camera camera;
bool again;           // a render is wanted once this one has landed (the camera is not fitted yet)
bool framed;          // the model's box has been measured and the camera fitted to it
int frame_attempts;
float centre[3];      // the stage's centre (the player's feet)
bool have_centre;
std::string scene_message, scene_info;
int scene_slot = -1;
unsigned scenes;      // renders that built a scene
// What entering changed that is not in the guest's memory (given back on leaving).
int saved_fog_global, saved_occlusion, saved_interpolate, saved_holds;
bool looks_applied;

// The model a SHP's textures belong to (its texture block holds just that model's number), or -1: the file's own
// (game/battle/shp_file.zig: the model at +0xF4, then the sequence block, then the texture block).
int shared_texture_of(const unsigned char* shp, size_t size)
{
    auto i32 = [&](size_t at) { return at + 4 <= size ? (long)(int)(shp[at] | shp[at + 1] << 8 | shp[at + 2] << 16 | (unsigned)shp[at + 3] << 24) : -1L; };
    long model = i32(0xF4);
    if (model < 0 || model > (long)size)
        return -1;
    long seq = 0xF8 + model, seq_length = i32((size_t)seq + 4);
    if (seq_length < 0)
        return -1;
    size_t block = (size_t)(seq + 8 + seq_length);
    return i32(block) == 4 ? (int)i32(block + 4) : -1;
}
bool read_disc(const std::string& path, std::vector<unsigned char>& out)
{
    unsigned char* data = nullptr;
    unsigned size = 0;
    if (!pc_ui_disc_read(path.c_str(), &data, &size))
        return false;
    out.assign(data, data + size);
    free(data);
    return true;
}

// ---------------------------------------------------------------- the look

// ---------------------------------------------------------------- the turntable

float wrap360(float a)
{
    a = fmodf(a, 360.0f);
    return a < 0 ? a + 360.0f : a;
}
bool turntable_running() { return (flag("turntable.model") || flag("turntable.light")) && !flag("turntable.paused"); }
// One step per render: the angles move by speed x the time since the last render (`turntable.fixed_dt` seconds when set: a
// test's clock), so the motion is the same however fast the renders come, starts from where it stands and wraps without a jump.
void advance_turntable()
{
    bool running = turntable_running();
    double now = photo::now_seconds();
    if (running) {
        float fixed = num("turntable.fixed_dt", 0);
        float dt = fixed > 0 ? fixed : turn_was_running ? (float)fmin(fmax(now - turn_last, 0.0), 0.1) : 0.0f;
        if (flag("turntable.model"))
            model_angle = wrap360(model_angle + num("turntable.model_speed", 30) * dt);
        if (flag("turntable.light"))
            light_angle = wrap360(light_angle + num("turntable.light_speed", 30) * dt);
    }
    turn_was_running = running;
    turn_last = now;
}

// The look's room settings (room_settings.cpp's keys) as a transient layer over the room's, the sun and sky, the fog.
void apply_look()
{
    const Json* look = at("looks." + look_name);
    if (!look) {
        scene_message = "no look called " + look_name;
        return;
    }
    Json env = Json::object();
    if (const Json* s = look->get("sun")) {
        Json sun = *s;
        if (const Json* az = sun.get("azimuth")) // the turntable turns the key light about the model
            sun.set("azimuth", Json::num(wrap360((float)az->number + light_angle)));
        env.set("sun", sun);
    }
    if (const Json* s = look->get("sky"))
        env.set("sky", *s);
    pc_env_studio(prefab::json_write(env).c_str());
    pc_room_look_clear();
    if (const Json* room = look->get("room"))
        for (const auto& m : room->members)
            if (m.second.type == Json::Number)
                pc_room_look_set(m.first.c_str(), (float)m.second.number);
    const Json* fog = look->get("fog");
    pc_fog_global = fog && fog->type == Json::Bool && fog->boolean ? 1 : 0;
    // what stands between the camera and a character fades in an ellipse about her (occlusion.h): on a stage that is a hole in
    // the floor about the model's feet
    const Json* occlusion = look->get("occlusion");
    pc_occlusion = occlusion && occlusion->type == Json::Bool && occlusion->boolean ? saved_occlusion : PC_OCCLUSION_OFF;
    if (&pc_interp_holds)
        pc_interp_holds = 0; // (nor a pose eased in over the renders that held the one before)
    if (&pc_hw_interpolate)
        pc_hw_interpolate = 0; // a render is a picture of its own: not a blend with the one before (a turned model would smear)
    looks_applied = true;
}
void remove_look()
{
    if (!looks_applied)
        return;
    pc_env_studio(nullptr);
    pc_room_look_clear();
    pc_fog_global = saved_fog_global;
    pc_occlusion = saved_occlusion;
    if (&pc_interp_holds)
        pc_interp_holds = saved_holds;
    if (&pc_hw_interpolate)
        pc_hw_interpolate = saved_interpolate;
    looks_applied = false;
}

// ---------------------------------------------------------------- the camera

void forward(float yaw, float pitch, float f[3])
{
    f[0] = cosf(pitch) * sinf(yaw);
    f[1] = -sinf(pitch);
    f[2] = cosf(pitch) * cosf(yaw);
}
// The eye for the orbit: the target, `zoom` x the fitting distance away, looking at it from yaw and elevation.
void put_camera()
{
    if (!pc_editor_photo_set)
        return;
    // camera.yaw 0 is the model's front: the model faces `model.facing` (4096 a turn) and the view looks the way it faces
    float yaw = (camera.yaw + num("model.facing", 0) * 360.0f / 4096) * PI_F / 180, pitch = -camera.elevation * PI_F / 180, f[3];
    forward(yaw, pitch, f);
    float d = camera.distance * camera.zoom, eye[3];
    for (int k = 0; k < 3; k++)
        eye[k] = camera.target[k] - f[k] * d;
    pc_editor_photo_set(eye, yaw, pitch, 0, camera.fov);
}
void read_camera_defaults()
{
    camera.yaw = current && current->kind == "weapon" ? num("camera.weapon_yaw", 90) : num("camera.yaw", 20);
    camera.elevation = num("camera.elevation", 12);
    camera.zoom = 1;
    camera.fov = num("camera.fov", 40);
}
// The camera's target and fitting distance from the model's box (measured by the render before this one); `framed` when there is one.
void fit_camera()
{
    float lo[3], hi[3];
    if (!pc_studio_bounds_get || pc_studio_bounds_get(lo, hi) <= 8)
        return;
    float half[3], mid[3];
    for (int k = 0; k < 3; k++)
        mid[k] = (lo[k] + hi[k]) * 0.5f, half[k] = (hi[k] - lo[k]) * 0.5f;
    float radius = sqrtf(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);
    float margin = num("camera.fit", 1.25f);
    camera.target[0] = mid[0], camera.target[1] = mid[1], camera.target[2] = mid[2];
    camera.distance = fmaxf(radius * margin / sinf(camera.fov * 0.5f * PI_F / 180), num("camera.min_distance", 40));
    framed = true;
}

// ---------------------------------------------------------------- the texel locator (studio_locate.h)
//
// Each render that lands leaves the model's triangles, posed, with the view the picture was drawn with (when no change is waiting:
// then the marker and the pick agree with the picture on screen exactly; a camera change hides the marker for the one render it
// takes). A texel is asked for by its texture's content key and its place in that texture (what a painted page's pixel is, through
// the sprite's slices: pc/tools/paint_live.py), or by the page, palette and page texel directly; every triangle that samples it
// gets a footprint, drawn over the picture by pc_studio_overlay. Hovering or clicking the model picks the texel under the cursor
// (the same maths run the other way) and leaves it for the bridge (RPC studio {action: picked}).

struct Located {
    bool on = false;
    std::string key;                 // the texture's content key ("" when asked by page)
    int lx = 0, ly = 0;              // the place in the texture
    int page = 0, clut = -1, x = 0, y = 0; // asked by page (clut -1: any palette)
    bool by_key = false;
    std::vector<studio_locate::Hit> hits;
    unsigned for_capture = ~0u;      // the capture the hits were made for
    int targets = 0;                 // (page, clut, page texel) triples the key resolved to
};
struct PickedTexel {
    unsigned serial = 0;
    bool hit = false;
    bool scroll = false; // a click: the painter's view goes to it (hover only selects)
    studio_locate::Pick pick;
    std::string key;     // its texture's content key and the place in the texture ("" when no use of the picture has it)
    int lx = 0, ly = 0;
    uint64_t model = 0;
    int polygon = -1, quad = 0;
    float screen[2] = { 0, 0 };
};
struct Capture {
    std::vector<studio_locate::Tri> tris;
    studio_locate::View view;
    unsigned long long keys[16];
    int meshes = 0;
    unsigned serial = 0;     // counts captures
    bool valid = false;      // the picture on screen is this capture's (no camera change waiting)
    bool uses_known = false; // `uses` is this capture's
    std::vector<emissive::UseInfo> uses;
};
Capture capture;
Located located;
PickedTexel picked;

// The content keys of the models the picture drew, as hex text ("61c3d37af4"): what a painter's export is named after.
std::vector<std::string> capture_model_keys()
{
    std::vector<std::string> out;
    for (int m = 0; m < capture.meshes && m < 16; m++) {
        char text[16];
        snprintf(text, sizeof text, "%010llx", capture.keys[m]);
        if (capture.keys[m] && std::find(out.begin(), out.end(), text) == out.end())
            out.push_back(text);
    }
    return out;
}

void capture_clear()
{
    capture.tris.clear();
    capture.valid = capture.uses_known = false;
    capture.uses.clear();
    ++capture.serial;
    located.hits.clear();
    located.for_capture = ~0u;
}
const std::vector<emissive::UseInfo>& capture_uses()
{
    if (!capture.uses_known) {
        capture.uses = emissive::use_list(true);
        capture.uses_known = true;
    }
    return capture.uses;
}
// The triangles of the model and the view of the picture that has just landed.
void capture_take(bool clean)
{
    capture.valid = false;
    capture.uses_known = false;
    ++capture.serial;
    located.for_capture = ~0u;
    if (!clean || !entered || !current)
        return;
    unsigned meshes[16];
    capture.meshes = pc_studio_meshes ? pc_studio_meshes(meshes, capture.keys, 16) : 0;
    capture.tris.clear();
    static std::vector<render_posed_triangle> buffer;
    buffer.resize(40000);
    for (int m = 0; m < capture.meshes; m++) {
        int n = render_mesh_posed ? render_mesh_posed(meshes[m], buffer.data(), (int)buffer.size()) : -1;
        for (int i = 0; i < n; i++) {
            studio_locate::Tri t;
            memcpy(t.p, buffer[i].position, sizeof t.p);
            memcpy(t.uv, buffer[i].uv, sizeof t.uv);
            t.page = (uint16_t)(buffer[i].tpage & 31), t.clut = buffer[i].clut;
            t.polygon = buffer[i].polygon, t.quad = buffer[i].quad, t.mesh = m;
            capture.tris.push_back(t);
        }
    }
    pc_overlay_view ov;
    if (capture.tris.empty() || !pc_lights_tools_view || !pc_lights_tools_view(&ov, nullptr))
        return;
    memcpy(capture.view.view, ov.view, sizeof ov.view);
    memcpy(capture.view.projection, ov.projection, sizeof ov.projection);
    memcpy(capture.view.rect, ov.rect, sizeof ov.rect);
    capture.view.valid = true;
    capture.valid = true;
}

// The (page, clut, page texel) triples a texel of a texture is, in this picture: one for each use of that texture.
struct PageTexel {
    int page, clut, x, y;
};
std::vector<PageTexel> texel_targets(const std::string& key, int lx, int ly)
{
    std::vector<PageTexel> out;
    for (const emissive::UseInfo& u : capture_uses()) {
        int w = u.box[2] - u.box[0] + 1, h = u.box[3] - u.box[1] + 1;
        if (u.key == key && lx >= 0 && ly >= 0 && lx < w && ly < h)
            out.push_back({ (int)(u.page & 31), (int)u.clut, u.box[0] + lx, u.box[1] + ly });
    }
    return out;
}
// The texture of a page texel of the picture (a use whose box has it; the smallest) and the place in it.
bool texture_of(int page, int clut, int x, int y, std::string* key, int* lx, int* ly)
{
    const emissive::UseInfo* best = nullptr;
    for (const emissive::UseInfo& u : capture_uses()) {
        if ((int)(u.page & 31) != page || (int)u.clut != clut || x < u.box[0] || x > u.box[2] || y < u.box[1] || y > u.box[3])
            continue;
        if (!best || (u.box[2] - u.box[0]) * (u.box[3] - u.box[1]) < (best->box[2] - best->box[0]) * (best->box[3] - best->box[1]))
            best = &u;
    }
    if (!best)
        return false;
    *key = best->key, *lx = x - best->box[0], *ly = y - best->box[1];
    return true;
}
float marker_tolerance() { return fmaxf(camera.distance * camera.zoom * num("locate.occlusion_tolerance", 0.002f), 1.0f); }
// The hits of the asked-for texel for the current capture (made once per capture and ask).
void locate_update()
{
    if (!located.on || located.for_capture == capture.serial)
        return;
    located.for_capture = capture.serial;
    located.hits.clear();
    located.targets = 0;
    if (!capture.valid)
        return;
    std::vector<PageTexel> targets;
    if (located.by_key)
        targets = texel_targets(located.key, located.lx, located.ly);
    else
        targets.push_back({ located.page, located.clut, located.x, located.y });
    located.targets = (int)targets.size();
    for (const PageTexel& t : targets) {
        std::vector<studio_locate::Hit> h = studio_locate::locate(capture.tris, t.page, t.clut, t.x, t.y);
        for (auto& x : h)
            located.hits.push_back(std::move(x));
    }
    studio_locate::shade(capture.tris, capture.view, located.hits, marker_tolerance());
}
// A pick at window pixel (x, y) of the picture: the texel there, with its texture.
PickedTexel pick_at(float px, float py)
{
    PickedTexel r;
    r.screen[0] = px, r.screen[1] = py;
    if (!capture.valid)
        return r;
    r.pick = studio_locate::pick(capture.tris, capture.view, px, py);
    r.hit = r.pick.hit;
    if (!r.hit)
        return r;
    const studio_locate::Tri& t = capture.tris[(size_t)r.pick.tri];
    r.polygon = t.polygon, r.quad = t.quad;
    r.model = t.mesh < capture.meshes ? capture.keys[t.mesh] : 0;
    texture_of(r.pick.page, r.pick.clut, r.pick.x, r.pick.y, &r.key, &r.lx, &r.ly);
    return r;
}
ImU32 marker_colour(const char* path, float alpha_scale, const char* alpha_path, float alpha_fallback)
{
    float c[3] = { 255, 60, 200 };
    if (const Json* j = at(path); j && j->type == Json::Array && j->items.size() >= 3)
        for (int k = 0; k < 3; k++)
            c[k] = (float)j->items[(size_t)k].number;
    float a = fminf(fmaxf(num(alpha_path, alpha_fallback) * alpha_scale, 0), 1);
    return IM_COL32((int)c[0], (int)c[1], (int)c[2], (int)(a * 255));
}
// One texel's footprints over the picture. `style`: "locate.marker" or "locate.pick".
void draw_hits(ImDrawList* d, const std::vector<studio_locate::Hit>& hits, const std::string& style, double time)
{
    float hz = num((style + ".pulse_hz").c_str(), 1.5f), depth = num((style + ".pulse_depth").c_str(), 0.35f);
    float pulse = hz > 0 ? 0.5f + 0.5f * sinf((float)(time * hz * 6.2831853)) : 1.0f; // 0..1
    float back = num((style + ".back_alpha").c_str(), 0.3f);
    float ring = num((style + ".ring_radius").c_str(), 14) * (1 + depth * (pulse - 0.5f));
    float ring_w = num((style + ".ring_width").c_str(), 2), line_w = num((style + ".line_width").c_str(), 1.5f);
    bool cross = flag((style + ".crosshair").c_str());
    for (const studio_locate::Hit& h : hits) {
        if (!h.on_screen)
            continue;
        float scale = h.visible ? 1.0f : back;
        float glow = 1.0f - depth * (1 - pulse); // the fill breathes with the ring
        std::vector<ImVec2> poly;
        bool ok = true;
        for (size_t i = 0; i + 2 < h.ring.size() && ok; i += 3) {
            float o[2];
            ok = studio_locate::project(capture.view, &h.ring[i], o, nullptr);
            poly.push_back(ImVec2(o[0], o[1]));
        }
        if (ok && poly.size() >= 3) {
            // (a footprint of either winding: ImGui's convex fill wants one)
            float s = 0;
            for (size_t i = 0; i < poly.size(); i++)
                s += poly[i].x * poly[(i + 1) % poly.size()].y - poly[(i + 1) % poly.size()].x * poly[i].y;
            if (s < 0)
                std::reverse(poly.begin(), poly.end());
            d->AddConvexPolyFilled(poly.data(), (int)poly.size(), marker_colour((style + ".color").c_str(), scale * glow, (style + ".fill_alpha").c_str(), 0.5f));
            d->AddPolyline(poly.data(), (int)poly.size(), marker_colour((style + ".color").c_str(), scale, (style + ".outline_alpha").c_str(), 1.0f),
                ImDrawFlags_Closed, line_w);
        }
        ImVec2 c(h.screen[0], h.screen[1]);
        ImU32 col = marker_colour((style + ".color").c_str(), scale, (style + ".outline_alpha").c_str(), 1.0f);
        if (ring > 0) {
            d->AddCircle(c, ring + 1, IM_COL32(0, 0, 0, (int)(255 * scale * 0.6f)), 24, ring_w + 2); // (a dark edge: it reads on any colour)
            d->AddCircle(c, ring, col, 24, ring_w);
        }
        if (cross && ring > 0) {
            d->AddLine(ImVec2(c.x - ring * 1.6f, c.y), ImVec2(c.x - ring * 0.5f, c.y), col, ring_w);
            d->AddLine(ImVec2(c.x + ring * 0.5f, c.y), ImVec2(c.x + ring * 1.6f, c.y), col, ring_w);
            d->AddLine(ImVec2(c.x, c.y - ring * 1.6f), ImVec2(c.x, c.y - ring * 0.5f), col, ring_w);
            d->AddLine(ImVec2(c.x, c.y + ring * 0.5f), ImVec2(c.x, c.y + ring * 1.6f), col, ring_w);
        }
    }
}

// ---------------------------------------------------------------- the render callbacks

void studio_render()
{
    dirty = false;
    if (leaving)
        clean_seen = true;
    if (!entered)
        return;
    again = false;
    advance_turntable();
    apply_look();
    if (!pc_studio_scene)
        return;
    if (!glows_kept) { // the room's glows are game-time state: kept while the stage replaces its scene, given back on leaving
        pc_glows_keep();
        glows_kept = true;
    }
    // The box the render before this one measured; the camera is fitted to it before the frames run.
    if (current && !framed)
        fit_camera();
    pc_studio_scene_cfg c;
    memset(&c, 0, sizeof c);
    // The stage grows with the camera's reach (a dragon is a model of a thousand units), never smaller than its data says.
    float radius = num("stage.radius", 900), fillet = num("stage.fillet", 450), height = num("stage.height", 1600);
    float reach = camera.distance * camera.zoom * num("stage.reach", 1.15f);
    float scale = fmaxf(1.0f, fminf(reach / (radius + fillet), num("stage.max_scale", 12)));
    c.stage = 1;
    c.radius = radius * scale, c.fillet = fillet * scale, c.height = height * scale;
    c.grey = (int)num("stage.grey", 95), c.facet = num("stage.facet", 400);
    c.floor_step = num("stage.floor_step", 450), c.wall_step = num("stage.wall_step", 800);
    c.actor_light = (int)num("stage.actor_light", 0x808080);
    c.hide_others = 1;
    if (const Json* b = at("backdrop.color")) { // a solid colour for the background, no stage
        if (at("backdrop.mode") && at("backdrop.mode")->string == "solid" && b->items.size() >= 3) {
            c.backdrop = 1;
            c.backdrop_rgb = ((int)b->items[0].number & 255) | ((int)b->items[1].number & 255) << 8 | ((int)b->items[2].number & 255) << 16;
        }
    }
    c.equipment = (int)num("model.equipment", 1);
    c.animation = (int)num("model.animation", 1);
    c.facing = (int)lroundf(num("model.facing", 0) + model_angle * 4096.0f / 360.0f) & 0xfff; // (the turntable turns the model)
    c.model_id = (int)num("model.loader_id", 200);
    if (current) {
        c.file = file_data.data(), c.size = (unsigned)file_data.size();
        if (base_id >= 0)
            c.base = base_data.data(), c.base_size = (unsigned)base_data.size(), c.base_id = base_id;
        if (current->kind == "character")
            c.kind = PC_STUDIO_SHP, c.material = (int)num("model.material", 0);
        else if (current->kind == "enemy")
            c.kind = PC_STUDIO_ZUD;
        else {
            c.kind = PC_STUDIO_WEP;
            c.rig = rig_data.data(), c.rig_size = (unsigned)rig_data.size();
            c.material = (int)num("model.weapon_material", 0);
        }
    }
    pc_studio_scene_out out;
    int status = pc_studio_scene(&c, &out);
    scene_message = out.message;
    scene_info = out.info;
    (void)status;
    scene_slot = out.slot;
    pc_studio_hide_bodies = (c.kind == PC_STUDIO_WEP && out.slot >= 0 ? 1 << out.slot : 0) | 0x30000;
    memcpy(centre, out.position, sizeof centre);
    have_centre = true;
    ++scenes;
    if (!current) { // the empty stage: about the centre
        camera.target[0] = centre[0], camera.target[1] = centre[1] - num("camera.guess_height", 220) * 0.5f;
        camera.target[2] = centre[2];
        camera.distance = num("camera.guess_distance", 600);
        framed = true;
    } else if (!framed) { // nothing measured yet: a guess about the centre, and another render once this one has landed
        camera.target[0] = centre[0], camera.target[1] = centre[1] - num("camera.guess_height", 220) * 0.5f;
        camera.target[2] = centre[2];
        camera.distance = num("camera.guess_distance", 600);
        if (++frame_attempts >= 4)
            framed = true;
        else
            again = true;
    }
    put_camera();
    if (pc_studio_bounds_reset)
        pc_studio_bounds_reset();
}

// A render has landed (the game is at M again): the camera was not fitted, so another.
void studio_landed()
{
    landed_at = photo::now_seconds();
    capture_take(entered && !(again || dirty));
    if (entered && (again || dirty)) {
        again = false;
        request();
    } else if (leaving) {
        if (clean_seen) {
            leaving = false;
            pc_photo_release(OWNER); // the pause, the camera (the game's own again) and the hidden HUD
        } else
            request(); // the render that was running when the studio was left drew the stage
    }
}

// Held and idle: the turntable asks for its next step (a render a poll later than the last, so that the RPC and the window are
// served between renders).
double asked_at;
void studio_idle()
{
    // a request the photo mode dropped (asked before the hold began, or while it could not render) is asked again
    if (entered && dirty && photo::now_seconds() - asked_at >= 0.2) {
        asked_at = photo::now_seconds();
        ::pc_photo_redraw();
    }
    if (entered && turntable_running() && photo::now_seconds() - landed_at >= num("turntable.interval", 0.02f))
        request();
}

// ---------------------------------------------------------------- enter, leave, summon

std::string leave();
std::string enter()
{
    if (entered)
        return "";
    if (leaving) { // still holding the game from the last visit: this one goes on from there
        leaving = false;
        entered = true;
        framed = false;
        frame_attempts = 0;
        read_camera_defaults();
        request();
        return "";
    }
    char why[256] = "";
    saved_fog_global = pc_fog_global;
    saved_occlusion = pc_occlusion;
    saved_interpolate = &pc_hw_interpolate ? pc_hw_interpolate : 0;
    saved_holds = &pc_interp_holds ? pc_interp_holds : 1;
    if (pc_photo_pause(OWNER, why, sizeof why))
        return why;
    if (pc_photo_camera_begin(OWNER, why, sizeof why)) {
        pc_photo_release(OWNER);
        return why;
    }
    pc_photo_ui_hide(OWNER, PC_PHOTO_HIDE_HUD | PC_PHOTO_HIDE_GAME2D);
    entered = true;
    framed = false;
    frame_attempts = 0;
    have_centre = false;
    read_camera_defaults();
    request();
    return "";
}
std::string leave()
{
    if (!entered)
        return "";
    entered = false;
    pc_studio_hide_bodies = 0;
    capture_clear();
    if (pc_studio_bounds_stop)
        pc_studio_bounds_stop();
    remove_look();
    current = nullptr;
    file_data.clear(), rig_data.clear(), base_data.clear();
    base_id = -1;
    // The renderers keep what they built from the last picture (the stage's ray-traced scene, its lighting): one render of the game
    // as it is rebuilds them from the room, the room's glows being the ones kept at the start of the visit and put back by that
    // render (they are game-time state: rebuilt, the sparks would pop away and grow back). Then the pause, the camera and the HUD
    // are given back.
    if (glows_kept)
        pc_glows_give_back();
    glows_kept = false;
    if (pc_photo_holding() || pc_photo_rendering) {
        leaving = true, clean_seen = false;
        request();
    } else
        pc_photo_release(OWNER);
    return "";
}
std::string summon(const std::string& kind, int id)
{
    const Item* item = find_item(kind, id);
    if (!item)
        return "no " + kind + " " + std::to_string(id) + " in the catalog";
    std::vector<unsigned char> data, rig;
    if (!read_disc(item->file, data))
        return "cannot read " + item->file + " from the disc";
    if (kind == "weapon" && !read_disc(("OBJ/" + hex2((int)num("model.rig", 0)) + ".SHP"), rig))
        return "cannot read the rig that holds a weapon";
    // The SHP is the file itself, or a ZUD's first part (its offset at +8); a shared texture needs its model.
    const unsigned char* shp = data.data();
    size_t shp_size = data.size();
    if (kind == "enemy" && data.size() > 0x10) {
        unsigned at = data[8] | data[9] << 8 | data[10] << 16 | (unsigned)data[11] << 24;
        if (at < data.size())
            shp += at, shp_size -= at;
    }
    int shared = kind == "weapon" ? -1 : shared_texture_of(shp, shp_size);
    std::vector<unsigned char> base;
    if (shared >= 0 && !read_disc("OBJ/" + hex2(shared) + ".SHP", base))
        return "cannot read the model " + hex2(shared) + " whose textures " + item->name + " shares";
    if (shared >= 0 && shared_texture_of(base.data(), base.size()) >= 0)
        return item->name + " shares textures through a chain of models";
    base_data = std::move(base);
    base_id = shared;
    current = item;
    file_data = std::move(data), rig_data = std::move(rig);
    if (pc_studio_bounds_reset)
        pc_studio_bounds_reset();
    framed = false;
    frame_attempts = 0;
    read_camera_defaults();
    if (entered)
        request();
    return "";
}
void clear()
{
    current = nullptr;
    file_data.clear(), rig_data.clear(), base_data.clear();
    base_id = -1;
    pc_studio_hide_bodies = 0;
    if (pc_studio_bounds_reset)
        pc_studio_bounds_reset();
    framed = false;
    frame_attempts = 0;
    if (entered)
        request();
}

// The photo mode's Part: a release by the safety, an engine reload or the owner itself.
void part_release(const std::string& owner)
{
    if (owner == OWNER && (entered || leaving)) {
        entered = leaving = false;
        if (glows_kept)
            pc_glows_give_back();
        glows_kept = false;
        pc_studio_hide_bodies = 0;
        capture_clear();
        remove_look();
        current = nullptr;
    }
}
void part_owners(std::vector<std::string>& out)
{
    if (entered)
        out.push_back(OWNER);
}
void part_reset()
{
    entered = leaving = false;
    pc_studio_hide_bodies = 0;
    capture_clear();
    remove_look();
    current = nullptr;
}
photo::Register part({ "studio", part_release, part_owners, part_reset, nullptr, nullptr, studio_render, studio_idle, studio_landed });

// ---------------------------------------------------------------- RPC

std::string jstr(const std::string& s) { return prefab::json_quote(s); }
std::string jnum(double v)
{
    char b[48];
    snprintf(b, sizeof b, "%.6g", v);
    return b;
}
std::string pick_json(const PickedTexel& r)
{
    if (!r.hit && !r.serial)
        return "{\"hit\":false,\"serial\":0}";
    char model[24];
    snprintf(model, sizeof model, "%010llx", (unsigned long long)r.model);
    return "{\"serial\":" + std::to_string(r.serial) + ",\"hit\":" + (r.hit ? "true" : "false") + ",\"scroll\":" + (r.scroll ? "true" : "false")
        + ",\"page\":" + std::to_string(r.pick.page) + ",\"clut\":" + std::to_string(r.pick.clut) + ",\"x\":" + std::to_string(r.pick.x)
        + ",\"y\":" + std::to_string(r.pick.y) + ",\"u\":" + jnum(r.pick.u) + ",\"v\":" + jnum(r.pick.v) + ",\"key\":" + jstr(r.key)
        + ",\"lx\":" + std::to_string(r.lx) + ",\"ly\":" + std::to_string(r.ly) + ",\"model\":" + jstr(r.model ? model : "")
        + ",\"label\":" + jstr(std::string(r.model ? "m" + std::string(model) : std::string()) + studio_locate::page_label(r.pick.page, r.pick.clut))
        + ",\"polygon\":" + std::to_string(r.polygon) + ",\"quad\":" + std::to_string(r.quad) + ",\"world\":[" + jnum(r.pick.world[0]) + ","
        + jnum(r.pick.world[1]) + "," + jnum(r.pick.world[2]) + "],\"screen\":[" + jnum(r.screen[0]) + "," + jnum(r.screen[1]) + "]}";
}

// What the Studio window drew last frame (rectangles in screen pixels; `visible`: inside what its parent clips to), for `state`.
struct WindowLayout {
    bool drawn = false;
    float window[4] = {}, status[4] = {}, controls[4] = {}, catalog[4] = {};
    bool leave_shown = false, leave_visible = false, window_scrolls = false;
    float leave[4] = {};
} wl;

std::string state_json()
{
    std::string j = "{\"entered\":";
    j += entered ? "true" : "false";
    j += ",\"settled\":";
    j += entered && photo::settled() ? "true" : "false";
    j += ",\"look\":" + jstr(look_name);
    j += ",\"scenes\":" + std::to_string(scenes);
    j += ",\"framed\":";
    j += framed ? "true" : "false";
    j += ",\"item\":";
    j += current ? "{\"kind\":" + jstr(current->kind) + ",\"id\":" + std::to_string(current->id) + ",\"name\":" + jstr(current->name) + "}"
                 : std::string("null");
    j += ",\"camera\":{\"yaw\":" + jnum(camera.yaw) + ",\"elevation\":" + jnum(camera.elevation) + ",\"zoom\":" + jnum(camera.zoom)
        + ",\"fov\":" + jnum(camera.fov) + ",\"distance\":" + jnum(camera.distance) + ",\"target\":[" + jnum(camera.target[0]) + ","
        + jnum(camera.target[1]) + "," + jnum(camera.target[2]) + "]}";
    float lo[3], hi[3];
    if (pc_studio_bounds_get && pc_studio_bounds_get(lo, hi))
        j += ",\"box\":{\"lo\":[" + jnum(lo[0]) + "," + jnum(lo[1]) + "," + jnum(lo[2]) + "],\"hi\":[" + jnum(hi[0]) + "," + jnum(hi[1])
            + "," + jnum(hi[2]) + "]}";
    j += ",\"turntable\":{\"model_angle\":" + jnum(model_angle) + ",\"light_angle\":" + jnum(light_angle) + ",\"running\":";
    j += turntable_running() ? "true}" : "false}";
    j += ",\"slot\":" + std::to_string(scene_slot);
    j += ",\"message\":" + jstr(scene_message);
    j += ",\"info\":" + jstr(scene_info);
    j += ",\"leaving\":";
    j += leaving ? "true" : "false";
    j += ",\"error\":" + jstr(load_error);
    {
        auto rect = [](const float r[4]) {
            char b[96];
            snprintf(b, sizeof b, "[%.0f,%.0f,%.0f,%.0f]", r[0], r[1], r[2], r[3]);
            return std::string(b);
        };
        j += std::string(",\"window\":{\"drawn\":") + (wl.drawn ? "true" : "false") + ",\"rect\":" + rect(wl.window)
            + ",\"status\":" + rect(wl.status) + ",\"controls\":" + rect(wl.controls) + ",\"catalog\":" + rect(wl.catalog)
            + ",\"leave\":" + rect(wl.leave) + ",\"leave_shown\":" + (wl.leave_shown ? "true" : "false")
            + ",\"leave_visible\":" + (wl.leave_visible ? "true" : "false") + ",\"scrolls\":" + (wl.window_scrolls ? "true" : "false") + "}";
    }
    j += ",\"models\":[";
    {
        const std::vector<std::string> keys = capture_model_keys();
        for (size_t i = 0; i < keys.size(); i++)
            j += (i ? ",\"" : "\"") + keys[i] + "\"";
    }
    j += "]";
    j += ",\"capture\":{\"triangles\":" + std::to_string(capture.tris.size()) + ",\"meshes\":" + std::to_string(capture.meshes)
        + ",\"valid\":" + (capture.valid ? "true" : "false") + ",\"serial\":" + std::to_string(capture.serial) + "}";
    return j + "}";
}
}

// ---------------------------------------------------------------- the locator's picture and the cursor

// The picture takes clicks (dev_overlay.cpp hands them to ImGui when the editor is hidden): the studio is open and picks.
extern "C" int pc_studio_wants_mouse(void)
{
    const Json* m = at("locate.pick_on");
    return entered && capture.valid && !(m && m->type == Json::String && m->string == "off");
}
extern "C" void pc_studio_overlay(void)
{
    if (!entered || !capture.valid || !capture.view.valid)
        return;
    ImGuiIO& io = ImGui::GetIO();
    const float* r = capture.view.rect;
    bool in_view = io.MousePos.x >= r[0] && io.MousePos.y >= r[1] && io.MousePos.x < r[0] + r[2] && io.MousePos.y < r[1] + r[3];
    std::string mode = at("locate.pick_on") && at("locate.pick_on")->type == Json::String ? at("locate.pick_on")->string : "hover";
    bool clicked = in_view && !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (in_view && !io.WantCaptureMouse && (clicked || mode == "hover") && mode != "off") {
        PickedTexel p = pick_at(io.MousePos.x, io.MousePos.y);
        bool same = p.hit == picked.hit && p.pick.page == picked.pick.page && p.pick.clut == picked.pick.clut && p.pick.x == picked.pick.x
            && p.pick.y == picked.pick.y && picked.serial != 0;
        if ((p.hit && (!same || clicked)) || (clicked && !p.hit)) {
            p.serial = picked.serial + 1;
            p.scroll = clicked && flag("locate.click_scrolls");
            picked = p;
        }
    }
    ImDrawList* d = ImGui::GetForegroundDrawList();
    locate_update();
    if (located.on)
        draw_hits(d, located.hits, "locate.marker", ImGui::GetTime());
    if (picked.hit && flag("locate.pick.show") && capture.valid) {
        // the picked texel's own footprint (made now for this capture; the cursor's texel is cheap)
        static std::vector<studio_locate::Hit> shown;
        static unsigned shown_serial = ~0u, shown_capture = ~0u;
        if (shown_serial != picked.serial || shown_capture != capture.serial) {
            shown_serial = picked.serial, shown_capture = capture.serial;
            shown = studio_locate::locate(capture.tris, picked.pick.page, picked.pick.clut, picked.pick.x, picked.pick.y);
            studio_locate::shade(capture.tris, capture.view, shown, marker_tolerance());
        }
        draw_hits(d, shown, "locate.pick", ImGui::GetTime());
    }
}

// ---------------------------------------------------------------- the Studio window (dev overlay: Window > Studio)

extern "C" {
bool pc_studio_open;
}
namespace {
// Every number, colour and switch under `node`, as rows (the data is the Details: a change is made at once and shown).
bool rows(Json& node, const std::string& path, int depth)
{
    bool changed = false;
    for (auto& m : node.members) {
        Json& v = m.second;
        const char* key = m.first.c_str();
        if (v.type == Json::Object) {
            if (ImGui::TreeNodeEx(key, depth < 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                changed |= rows(v, path + key + ".", depth + 1);
                ImGui::TreePop();
            }
        } else if (v.type == Json::Number) {
            float f = (float)v.number;
            if (ImGui::DragFloat(key, &f, fabsf(f) > 20 ? 1.0f : 0.01f))
                v.number = f, changed = true;
        } else if (v.type == Json::Bool) {
            changed |= ImGui::Checkbox(key, &v.boolean);
        } else if (v.type == Json::Array && v.items.size() == 3 && v.items[0].type == Json::Number) {
            float c[3] = { (float)v.items[0].number, (float)v.items[1].number, (float)v.items[2].number };
            if (ImGui::ColorEdit3(key, c, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
                for (int k = 0; k < 3; k++)
                    v.items[k].number = c[k];
                changed = true;
            }
        } else if (v.type == Json::String && m.first == "about") {
            ImGui::TextDisabled("%s", v.string.c_str());
        }
    }
    return changed;
}
}

extern "C" void pc_studio_window(void)
{
    if (!pc_studio_open)
        return;
    rood_shell::dock_tool_window("Studio");
    wl = WindowLayout();
    if (!ImGui::Begin("Studio", &pc_studio_open)) {
        ImGui::End();
        return;
    }
    auto item_rect = [](float r[4]) {
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        r[0] = a.x, r[1] = a.y, r[2] = b.x - a.x, r[3] = b.y - a.y;
    };
    wl.drawn = true;
    wl.window[0] = ImGui::GetWindowPos().x, wl.window[1] = ImGui::GetWindowPos().y;
    wl.window[2] = ImGui::GetWindowSize().x, wl.window[3] = ImGui::GetWindowSize().y;
    // The state first, always in sight: in the studio (and the way out) or not. Below it two columns, each scrolling on its own
    // (the window is docked short and wide): the controls left, the catalog right.
    if (entered) {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1), "In the studio");
        ImGui::SameLine();
        bool left = ImGui::Button("Leave");
        item_rect(wl.leave);
        wl.leave_shown = true, wl.leave_visible = ImGui::IsItemVisible();
        if (left)
            leave();
        ImGui::SameLine();
        ImGui::TextDisabled("the game is held; leaving gives it back exactly");
    } else {
        ImGui::TextDisabled("Not in the studio: the game is running");
        ImGui::SameLine();
        bool go = ImGui::Button("Enter the studio");
        item_rect(wl.leave);
        wl.leave_visible = ImGui::IsItemVisible();
        if (go) {
            std::string why = enter();
            if (!why.empty())
                scene_message = why;
        }
        if (!scene_message.empty())
            ImGui::TextWrapped("%s", scene_message.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(or pick a model)");
    }
    wl.status[0] = wl.window[0], wl.status[1] = ImGui::GetCursorScreenPos().y;
    const float column = fmaxf(ImGui::GetContentRegionAvail().x * num("window.controls_share", 0.55f), 260.0f);
    ImGui::BeginChild("##controls", ImVec2(column, 0), ImGuiChildFlags_Borders);
    if (!entered) {
        ImGui::TextDisabled("Holds the game and shows its models on a neutral stage.");
    } else {
        if (ImGui::Button("Clear"))
            clear();
        ImGui::SameLine();
        if (ImGui::Button("Save looks")) {
            std::string why;
            if (!save_config(&why))
                scene_message = why;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload data")) {
            load_config();
            catalog_loaded = items_built = false;
            items.clear();
            request();
        }
        if (!scene_message.empty())
            ImGui::TextWrapped("%s", scene_message.c_str());
        {
            const std::vector<std::string> keys = capture_model_keys();
            if (!keys.empty()) {
                std::string text = "Model key:";
                for (const std::string& k : keys)
                    text += " " + k;
                ImGui::TextUnformatted(text.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy"))
                    ImGui::SetClipboardText(keys[0].c_str());
            }
        }
        // the look
        if (const Json* looks = at("looks")) {
            std::string names;
            int selected = 0, n = 0;
            for (const auto& m : looks->members) {
                names += m.first + '\0';
                if (m.first == look_name)
                    selected = n;
                n++;
            }
            if (ImGui::Combo("Look", &selected, names.c_str()) && selected < n) {
                look_name = looks->members[(size_t)selected].first;
                request();
            }
        }
        // the camera
        bool moved = false;
        moved |= ImGui::SliderFloat("Yaw", &camera.yaw, -360, 360, "%.0f deg");
        moved |= ImGui::SliderFloat("Elevation", &camera.elevation, -85, 85, "%.0f deg");
        moved |= ImGui::SliderFloat("Zoom", &camera.zoom, 0.1f, 4, "%.2f x", ImGuiSliderFlags_Logarithmic);
        moved |= ImGui::SliderFloat("Field of view", &camera.fov, 10, 90, "%.0f deg");
        if (ImGui::Button("Frame the model")) {
            framed = false, frame_attempts = 0, camera.zoom = 1;
            moved = true;
        }
        if (moved) {
            put_camera();
            request();
        }
        ImGui::Separator();
        bool changed = false;
        if (ImGui::CollapsingHeader("Stage, camera and look values")) {
            Json& root = cfg();
            for (const char* group : { "window", "stage", "camera", "model", "turntable", "backdrop", "locate" })
                if (Json* g = root.get(group))
                    if (ImGui::TreeNode(group)) {
                        changed |= rows(*g, std::string(group) + ".", 1);
                        ImGui::TreePop();
                    }
            if (Json* l = root.get("looks"))
                if (Json* look = l->get(look_name.c_str()))
                    if (ImGui::TreeNodeEx(look_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        changed |= rows(*look, "looks." + look_name + ".", 1);
                        ImGui::TreePop();
                    }
        }
        if (ImGui::Button("Turntable: stop and reset")) {
            set_at("turntable.model", Json::boolean_(false)), set_at("turntable.light", Json::boolean_(false));
            model_angle = light_angle = 0;
            changed = true;
        }
        ImGui::SameLine();
        for (const char* preset : { "black", "grey", "white" }) {
            if (ImGui::SmallButton((std::string("Backdrop ") + preset).c_str())) {
                if (const Json* c = at(std::string("backdrop.presets.") + preset))
                    set_at("backdrop.color", *c), set_at("backdrop.mode", Json::str("solid")), changed = true;
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Backdrop: stage"))
            set_at("backdrop.mode", Json::str("stage")), changed = true;
        if (changed)
            request();
    }
    ImGui::EndChild();
    item_rect(wl.controls);
    ImGui::SameLine();
    // the catalog
    ImGui::BeginChild("##catalog", ImVec2(0, 0), ImGuiChildFlags_Borders);
    build_items();
    static char filter[64];
    static int kind = 0;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    ImGui::Combo("##kind", &kind, "All\0Characters\0Enemies\0Weapons\0");
    ImGui::SameLine();
    ImGui::InputTextWithHint("##filter", "filter", filter, sizeof filter);
    static const char* const kinds[] = { "", "character", "enemy", "weapon" };
    std::string low = filter;
    for (char& ch : low)
        ch = (char)tolower((unsigned char)ch);
    if (ImGui::BeginChild("##models", ImVec2(0, 0), ImGuiChildFlags_None)) {
        for (const Item& i : items) {
            if (kind && i.kind != kinds[kind])
                continue;
            std::string name = i.name;
            for (char& ch : name)
                ch = (char)tolower((unsigned char)ch);
            if (!low.empty() && name.find(low) == std::string::npos)
                continue;
            std::string label = i.name + "##" + i.kind + std::to_string(i.id);
            bool on = current == &i;
            if (ImGui::Selectable(label.c_str(), on)) {
                if (!entered) {
                    std::string why = enter();
                    if (!why.empty())
                        scene_message = why;
                }
                if (entered) {
                    std::string why = summon(i.kind, i.id);
                    if (!why.empty())
                        scene_message = why;
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    item_rect(wl.catalog);
    wl.window_scrolls = ImGui::GetScrollMaxY() > 0;
    ImGui::End();
}

extern "C" int pc_studio_rpc(const char* params_text, char* out, int size)
{
    Json p;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", p, &error) || p.type != Json::Object) {
        snprintf(out, (size_t)size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    auto text = [&](const char* key, const char* fallback = "") {
        const Json* j = p.get(key);
        return j && j->type == Json::String ? j->string : std::string(fallback);
    };
    auto number = [&](const char* key, float* into) {
        const Json* j = p.get(key);
        if (!j || j->type != Json::Number)
            return false;
        *into = (float)j->number;
        return true;
    };
    std::string action = text("action", "state"), result, why;
    if (action == "enter") {
        why = enter();
    } else if (action == "leave") {
        why = leave();
    } else if (action == "summon") {
        float id = -1;
        number("id", &id);
        why = summon(text("kind"), (int)id);
    } else if (action == "clear") {
        clear();
    } else if (action == "camera") {
        float v;
        if (number("yaw", &v))
            camera.yaw = v;
        if (number("elevation", &v))
            camera.elevation = fmaxf(-85, fminf(85, v));
        if (number("zoom", &v))
            camera.zoom = fmaxf(0.05f, fminf(20, v));
        if (number("fov", &v))
            camera.fov = fmaxf(5, fminf(120, v));
        if (number("orbit_yaw", &v))
            camera.yaw += v;
        if (number("orbit_elevation", &v))
            camera.elevation = fmaxf(-85, fminf(85, camera.elevation + v));
        if (number("zoom_by", &v))
            camera.zoom = fmaxf(0.05f, fminf(20, camera.zoom * v));
        const Json* f = p.get("frame");
        if (f && (f->type == Json::Bool ? f->boolean : f->number != 0)) {
            framed = false, frame_attempts = 0, camera.zoom = 1;
            read_camera_defaults();
        }
        if (entered) {
            put_camera();
            request();
        }
    } else if (action == "look") {
        std::string name = text("name");
        if (!name.empty()) {
            if (!at("looks." + name))
                why = "no look called " + name;
            else
                look_name = name;
        }
        std::string path = text("set");
        if (why.empty() && !path.empty()) {
            const Json* value = p.get("value");
            if (!value || !set_at(path, *value))
                why = "set needs a path and a value";
        }
        if (why.empty() && p.get("save") && !save_config(&why)) {
        }
        if (why.empty() && p.get("reload")) {
            load_config();
            catalog_loaded = items_built = false;
            items.clear();
        }
        if (why.empty() && entered)
            request();
    } else if (action == "turntable") {
        // {model, light: on/off; model_speed, light_speed: degrees a second (negative: the other way); paused; fixed_dt;
        //  reset: both angles to 0; model_angle, light_angle: set}; each is the data `turntable.*`
        static const char* const flags[] = { "model", "light", "paused" };
        for (const char* k : flags)
            if (const Json* v = p.get(k))
                set_at(std::string("turntable.") + k, Json::boolean_(v->type == Json::Bool ? v->boolean : v->number != 0));
        static const char* const numbers[] = { "model_speed", "light_speed", "fixed_dt" };
        for (const char* k : numbers)
            if (const Json* v = p.get(k))
                set_at(std::string("turntable.") + k, *v);
        float v;
        if (const Json* r = p.get("reset"); r && (r->type == Json::Bool ? r->boolean : r->number != 0))
            model_angle = light_angle = 0;
        if (number("model_angle", &v))
            model_angle = wrap360(v);
        if (number("light_angle", &v))
            light_angle = wrap360(v);
        if (entered)
            request();
    } else if (action == "backdrop") {
        // {mode: stage | solid; color: [r, g, b] 0..255; preset: a name of backdrop.presets}
        if (const Json* m = p.get("mode"); m && m->type == Json::String)
            set_at("backdrop.mode", *m);
        if (const Json* c = p.get("color"))
            set_at("backdrop.color", *c);
        std::string preset = text("preset");
        if (!preset.empty()) {
            const Json* c = at("backdrop.presets." + preset);
            if (!c)
                why = "no backdrop preset called " + preset;
            else
                set_at("backdrop.color", *c);
        }
        if (why.empty() && entered)
            request();
    } else if (action == "get") {
        const Json* j = at(text("path"));
        result = ",\"value\":" + (j ? prefab::json_write(*j) : std::string("null"));
    } else if (action == "list") {
        build_items();
        std::string kind = text("kind"), filter = text("filter");
        for (char& c : filter)
            c = (char)tolower((unsigned char)c);
        std::string list = "[";
        int count = 0;
        for (const Item& i : items) {
            std::string low = i.name;
            for (char& c : low)
                c = (char)tolower((unsigned char)c);
            if ((!kind.empty() && i.kind != kind) || (!filter.empty() && low.find(filter) == std::string::npos))
                continue;
            list += (count++ ? "," : "") + std::string("{\"kind\":") + jstr(i.kind) + ",\"id\":" + std::to_string(i.id)
                + ",\"name\":" + jstr(i.name) + ",\"file\":" + jstr(i.file) + "}";
        }
        result = ",\"count\":" + std::to_string(count) + ",\"items\":" + list + "]";
    } else if (action == "locate") {
        // {key, x, y}: texel (x, y) of the texture with that content key; {page, clut, x, y}: a texel of a texture page (clut omitted: any
        // palette); {label: "m<model>_p<page>_c<clut>", x, y}: a painted page's pixel; {clear: true}: no marker. The reply lists where it is.
        float x = 0, y = 0, v;
        if (const Json* c = p.get("clear"); c && (c->type == Json::Bool ? c->boolean : c->number != 0)) {
            located = Located();
        } else if (!number("x", &x) || !number("y", &y)) {
            why = "locate needs x and y with a key, a label or a page";
        } else {
            Located l;
            l.on = true;
            l.lx = l.x = (int)x, l.ly = l.y = (int)y;
            std::string key = text("key"), label = text("label");
            int page = 0, clut = 0;
            uint64_t model = 0;
            if (!label.empty()) {
                if (!studio_locate::parse_label(label, &page, &clut, &model))
                    why = "label: m<model>_p<page>_c<clut>";
                l.page = page, l.clut = clut;
            } else if (!key.empty()) {
                l.by_key = true, l.key = key;
            } else if (number("page", &v)) {
                l.page = (int)v;
                l.clut = number("clut", &v) ? (int)v : -1;
            } else {
                why = "locate needs a key, a label or a page";
            }
            if (why.empty())
                located = l;
        }
        if (why.empty()) {
            located.for_capture = ~0u;
            locate_update();
            std::string hits = "[";
            int n = 0;
            for (const studio_locate::Hit& h : located.hits) {
                const studio_locate::Tri& t = capture.tris[(size_t)h.tri];
                hits += (n++ ? "," : "") + std::string("{\"polygon\":") + std::to_string(t.polygon) + ",\"quad\":" + std::to_string(t.quad)
                    + ",\"mesh\":" + std::to_string(t.mesh) + ",\"centre\":[" + jnum(h.centre[0]) + "," + jnum(h.centre[1]) + ","
                    + jnum(h.centre[2]) + "],\"screen\":" + (h.on_screen ? "[" + jnum(h.screen[0]) + "," + jnum(h.screen[1]) + "]" : "null")
                    + ",\"visible\":" + (h.visible ? "true" : "false") + ",\"area\":" + jnum(h.area) + "}";
            }
            result = ",\"locate\":{\"on\":" + std::string(located.on ? "true" : "false") + ",\"captured\":" + (capture.valid ? "true" : "false")
                + ",\"textures\":" + std::to_string(located.targets) + ",\"triangles\":" + std::to_string(located.hits.size()) + ",\"hits\":" + hits
                + "]}";
        }
    } else if (action == "pick" || action == "picked") {
        // pick {x, y}: the texel under window pixel (x, y) of the picture, and (tell, default true) left for the bridge as the studio's pick;
        // picked: the last pick (a hover or a click in the window, or this action), by its serial
        if (action == "pick") {
            float x = 0, y = 0;
            if (!number("x", &x) || !number("y", &y)) {
                why = "pick needs x and y (window pixels)";
            } else {
                PickedTexel r = pick_at(x, y);
                const Json* tell = p.get("tell");
                if (r.hit && (!tell || (tell->type == Json::Bool ? tell->boolean : tell->number != 0))) {
                    r.serial = picked.serial + 1;
                    r.scroll = true;
                    picked = r;
                } else if (!r.hit) {
                    result = ",\"pick\":{\"hit\":false,\"captured\":" + std::string(capture.valid ? "true" : "false") + "}";
                }
                if (r.hit)
                    result = ",\"pick\":" + pick_json(r);
            }
        } else {
            result = ",\"pick\":" + pick_json(picked);
        }
    } else if (action != "state") {
        why = "action: state, enter, leave, summon, clear, camera, look, turntable, backdrop, locate, pick, picked, get, list";
    }
    if (!why.empty()) {
        snprintf(out, (size_t)size, "%s", why.c_str());
        return -1;
    }
    std::string reply = state_json();
    reply.pop_back();
    reply += result + "}";
    if ((int)reply.size() >= size)
        return -1;
    memcpy(out, reply.c_str(), reply.size() + 1);
    return 0;
}
