// The room's environment: sun (or moon) and sky (environment.h). Per room,
// from the lighting layers' `env sun` and `env sky` items; the editor's
// Atmosphere tab edits them (with a sun direction gizmo in the view), RPC
// `environment` reads and sets them, the renderers read pc_env_gpu_get.
#include "environment.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "imgui.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lighting_layers.h"
#include "lighting_quality.h"
#include "lights_internal.h"

extern "C" {
extern int pc_lights_enabled;
void pc_lights_mark_dirty(void);
}

int pc_env_global = 1;

namespace {
const float DEG = 3.14159265f / 180.0f;

/* Defaults: the schema's (lighting_layers.cpp kind "env"). */
struct Sun {
    bool defined = false, enabled = true;
    float azimuth = 180, elevation = 45; /* degrees: azimuth from +z toward +x, elevation above the horizon */
    float color[3] = { 1, 1, 1 };
    float intensity = 1;
    float angle = 0.53f; /* angular diameter, degrees (the sun's own: 0.53) */
    bool shadows = true;
};
struct Sky {
    bool defined = false, enabled = true;
    float zenith[3] = { 0.1f, 0.12f, 0.2f }, horizon[3] = { 0.25f, 0.25f, 0.3f }, ground[3] = { 0.05f, 0.05f, 0.05f };
    float intensity = 1;
    float sun_tint = 0;
    float ambient = 0.5f; /* the non-traced fallback's strength */
};
Sun sun;
Sky sky;
int page_frame = -100; /* the Atmosphere tab was drawn this frame (the gizmo shows) */
bool gizmo = true;

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
void direction(float azimuth, float elevation, float out[3])
{
    float a = azimuth * DEG, e = elevation * DEG;
    out[0] = cosf(e) * sinf(a);
    out[1] = -sinf(e); /* y is down */
    out[2] = cosf(e) * cosf(a);
}
} // namespace

namespace rood_env {

void reset()
{
    sun = Sun();
    sky = Sky();
    rood_atmo::reset();
    rood_post::reset();
}

bool apply_item(const lighting::Item& it)
{
    if (rood_atmo::apply_item(it)) /* env fog, fog volumes */
        return true;
    if (rood_post::apply_item(it)) /* post room, post cutscene, post event.<id> */
        return true;
    if (it.kind != "env")
        return false;
    if (it.name == "sun") {
        sun = Sun();
        sun.defined = true;
        sun.enabled = get1(it, "enabled") != 0;
        sun.azimuth = get1(it, "azimuth");
        sun.elevation = get1(it, "elevation");
        get3(it, "color", sun.color);
        sun.intensity = get1(it, "intensity");
        sun.angle = get1(it, "angle");
        sun.shadows = get1(it, "shadows") != 0;
        return true;
    }
    if (it.name == "sky") {
        sky = Sky();
        sky.defined = true;
        sky.enabled = get1(it, "enabled") != 0;
        get3(it, "zenith", sky.zenith);
        get3(it, "horizon", sky.horizon);
        get3(it, "ground", sky.ground);
        sky.intensity = get1(it, "intensity");
        sky.sun_tint = get1(it, "sun_tint");
        sky.ambient = get1(it, "ambient");
        return true;
    }
    return false;
}

void add_items(lighting::State& s)
{
    if (sun.defined) {
        lighting::Item it;
        it.kind = "env", it.name = "sun";
        it.set("enabled", sun.enabled ? "1" : "0");
        it.set("azimuth", num(sun.azimuth, 2));
        it.set("elevation", num(sun.elevation, 2));
        it.set("color", vec3(sun.color, 3));
        it.set("intensity", num(sun.intensity, 3));
        it.set("angle", num(sun.angle, 2));
        it.set("shadows", sun.shadows ? "1" : "0");
        lighting::canonicalize(it);
        s[it.id()] = it;
    }
    if (sky.defined) {
        lighting::Item it;
        it.kind = "env", it.name = "sky";
        it.set("enabled", sky.enabled ? "1" : "0");
        it.set("zenith", vec3(sky.zenith, 3));
        it.set("horizon", vec3(sky.horizon, 3));
        it.set("ground", vec3(sky.ground, 3));
        it.set("intensity", num(sky.intensity, 3));
        it.set("sun_tint", num(sky.sun_tint, 3));
        it.set("ambient", num(sky.ambient, 3));
        lighting::canonicalize(it);
        s[it.id()] = it;
    }
    rood_atmo::add_items(s);
    rood_post::add_items(s);
}

/* Undo snapshots (editor_undo.cpp): the items as lines, and back. */
std::string text()
{
    lighting::State s;
    add_items(s);
    std::string out;
    for (const auto& kv : s) {
        lighting::Entry e;
        e.verb = lighting::DEFINE;
        e.id = kv.first;
        e.item = kv.second;
        out += lighting::format_entry(e) + "\n";
    }
    return out;
}
void set_text(const std::string& t)
{
    reset();
    lighting::File f;
    if (!lighting::parse_text("rood-lighting 2\n" + t, f))
        return;
    for (const lighting::Entry& e : f.entries)
        if (e.verb == lighting::DEFINE)
            apply_item(e.item);
}

} // namespace rood_env

/* ---- renderers ---- */

extern "C" int pc_env_gpu_get(pc_env_gpu* out)
{
    memset(out, 0, sizeof *out);
    rood_lights::track_room(); /* the current room's items */
    if (!pc_env_global || !pc_lights_enabled)
        return 0;
    int on = 0;
    float d[3];
    direction(sun.azimuth, sun.elevation, d);
    memcpy(out->sun_dir, d, sizeof d);
    out->sun_dir[3] = fmaxf(sun.angle, 0.0f) * 0.5f * DEG;
    if (sun.defined && sun.enabled && sun.intensity > 0) {
        for (int k = 0; k < 3; k++)
            out->sun_col[k] = fmaxf(sun.color[k], 0.0f) * sun.intensity;
        out->sun_col[3] = sun.shadows && pc_shadow_mode != PC_SHADOWS_OFF ? 1.0f : 0.0f; /* quality: Shadows Off */
        on = 1;
    }
    if (sky.defined && sky.enabled && sky.intensity > 0) {
        for (int k = 0; k < 3; k++) {
            out->zenith[k] = fmaxf(sky.zenith[k], 0.0f) * sky.intensity;
            out->horizon[k] = fmaxf(sky.horizon[k], 0.0f) * sky.intensity;
            out->ground[k] = fmaxf(sky.ground[k], 0.0f) * sky.intensity;
        }
        out->zenith[3] = 1;
        out->horizon[3] = fmaxf(sky.sun_tint, 0.0f);
        out->ground[3] = fmaxf(sky.ambient, 0.0f);
        on = 1;
    }
    return on;
}

extern "C" unsigned pc_env_hash(void)
{
    pc_env_gpu e;
    pc_env_gpu_get(&e);
    const unsigned char* b = (const unsigned char*)&e;
    unsigned h = 2166136261u;
    for (size_t i = 0; i < sizeof e; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

/* ---- editor: the Atmosphere tab ---- */

namespace {
/* A colour edit plus its intensity as one row. */
bool color_row(const char* label, float* rgb)
{
    return ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
}
void tooltip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}
} // namespace

bool pc_atmosphere_page(void); /* atmosphere.cpp */
/* Returns true when something changed (the room has unsaved edits). */
extern "C" bool pc_environment_page(void)
{
    page_frame = ImGui::GetFrameCount();
    rood_lights::track_room();
    bool changed = false;
    ImGui::PushID("environment");
    ImGui::SeparatorText("Sun and sky (this room)");
    if (!sun.defined && !sky.defined) {
        ImGui::TextDisabled("No sun or sky here (a roofed room; the generator gives\n"
                            "rooms open to the sky a moon and an overcast night sky).");
        if (ImGui::Button("Add sun and sky")) {
            sun = Sun();
            sky = Sky();
            sun.defined = sky.defined = true;
            sun.azimuth = 205, sun.elevation = 38, sun.intensity = 0.55f, sun.angle = 1.5f;
            sun.color[0] = 0.62f, sun.color[1] = 0.70f, sun.color[2] = 0.95f;
            changed = true;
            pc_undo_hint("Add sun and sky");
        }
    } else {
        if (!sun.defined) {
            if (ImGui::Button("Add a sun")) {
                sun = Sun();
                sun.defined = changed = true;
            }
        } else {
            ImGui::PushID("sun");
            changed |= ImGui::Checkbox("Sun / moon", &sun.enabled);
            tooltip("A directional light from far away: parallel rays, soft shadows from its angular size.\n"
                    "Ray-traced shadows (Vulkan ray tracing, OpenGL compute); where nothing is traced it lights\n"
                    "unshadowed (pc/docs/Vulkan_Renderer.md, \"Sun and sky\").");
            ImGui::BeginDisabled(!sun.enabled);
            changed |= ImGui::SliderFloat("Azimuth", &sun.azimuth, 0, 360, "%.1f deg");
            tooltip("Compass direction it shines from: 0 = from +z, 90 = from +x. Drag the sun in the view too.");
            changed |= ImGui::SliderFloat("Elevation", &sun.elevation, 0, 90, "%.1f deg");
            changed |= color_row("Colour##sun", sun.color);
            changed |= ImGui::SliderFloat("Intensity##sun", &sun.intensity, 0, 4, "%.3f");
            changed |= ImGui::SliderFloat("Angular size", &sun.angle, 0, 10, "%.2f deg");
            tooltip("Angular diameter: 0.53 is the real sun's; larger gives softer shadow edges (an overcast moon).");
            changed |= ImGui::Checkbox("Casts shadows", &sun.shadows);
            ImGui::Checkbox("Direction gizmo in the view", &gizmo);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (!sky.defined) {
            if (ImGui::Button("Add a sky")) {
                sky = Sky();
                sky.defined = changed = true;
            }
        } else {
            ImGui::PushID("sky");
            changed |= ImGui::Checkbox("Sky", &sky.enabled);
            tooltip("Light from the open sky: rays of the indirect light that leave the room bring the sky's\n"
                    "radiance (zenith -> horizon -> ground gradient); what blocks it occludes it.");
            ImGui::BeginDisabled(!sky.enabled);
            changed |= color_row("Zenith", sky.zenith);
            changed |= color_row("Horizon", sky.horizon);
            changed |= color_row("Ground", sky.ground);
            changed |= ImGui::SliderFloat("Intensity##sky", &sky.intensity, 0, 4, "%.3f");
            changed |= ImGui::SliderFloat("Sun tint", &sky.sun_tint, 0, 1, "%.2f");
            tooltip("How much the sky around the sun takes its colour.");
            changed |= ImGui::SliderFloat("Ambient (no ray tracing)", &sky.ambient, 0, 1, "%.2f");
            tooltip("Where nothing traces the sky (shadow maps without GI), a hemisphere ambient term of this\n"
                    "strength stands in for it: upward faces get the sky, downward ones the ground.");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Remove sun and sky")) {
            sun = Sun();
            sky = Sky();
            changed = true;
            pc_undo_hint("Remove sun and sky");
        }
        tooltip("Deletes them from this room (saved as delete lines when a pack defines them).");
    }
    bool global = pc_env_global != 0;
    if (ImGui::Checkbox("Sun and sky on (all rooms)", &global))
        pc_env_global = global;
    tooltip("Switches every room's sun and sky off for comparison (not saved; RPC environment {global}).");
    ImGui::PopID();
    changed |= pc_atmosphere_page(); /* ground fog and fog volumes (atmosphere.cpp) */
    if (changed)
        pc_lights_mark_dirty();
    return changed;
}

/* ---- editor: the sun direction gizmo ----
 * In the editor view while the Atmosphere tab shows: an arrow from a point
 * ahead of the camera toward the sun, a disc at its end; dragging the disc
 * turns the sun (horizontal: azimuth, vertical: elevation). */
extern "C" void pc_env_draw(const float view[16], const float projection[16], const float rect[4])
{
    if (!gizmo || !sun.defined || !sun.enabled || ImGui::GetFrameCount() - page_frame > 1)
        return;
    /* The eye (R e + t = 0, R not orthonormal: Cramer's rule) and the view's forward row. */
    float m[3][3], eye[3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[r][c] = view[c * 4 + r];
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabsf(det) < 1e-12f)
        return;
    for (int k = 0; k < 3; k++) {
        float a[3][3];
        memcpy(a, m, sizeof a);
        for (int r = 0; r < 3; r++)
            a[r][k] = -view[12 + r];
        eye[k] = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                     + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0])) / det;
    }
    float f[3] = { m[2][0], m[2][1], m[2][2] }, fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f)
        return;
    float centre[3], d[3], tip[3];
    direction(sun.azimuth, sun.elevation, d);
    for (int k = 0; k < 3; k++) {
        centre[k] = eye[k] + f[k] / fl * 900.0f;
        tip[k] = centre[k] + d[k] * 300.0f;
    }
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
    ImVec2 a, b;
    if (!project(centre, a) || !project(tip, b))
        return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(rect[0], rect[1]), ImVec2(rect[0] + rect[2], rect[1] + rect[3]), true);
    int rgb[3];
    for (int k = 0; k < 3; k++)
        rgb[k] = (int)(255.0f * fminf(1.0f, fmaxf(0.0f, sun.color[k])));
    ImU32 col = IM_COL32(rgb[0], rgb[1], rgb[2], 255);
    draw->AddLine(a, b, IM_COL32(255, 220, 120, 220), 2.0f);
    draw->AddCircle(a, 4.0f, IM_COL32(255, 220, 120, 200), 12, 1.5f);
    static bool dragging;
    ImGuiIO& io = ImGui::GetIO();
    float dx = io.MousePos.x - b.x, dy = io.MousePos.y - b.y;
    bool over = dx * dx + dy * dy < 14.0f * 14.0f;
    if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse)
        dragging = true;
    if (dragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            dragging = false;
        else if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
            sun.azimuth = fmodf(sun.azimuth + io.MouseDelta.x * 0.5f + 360.0f, 360.0f);
            sun.elevation = fminf(90.0f, fmaxf(0.0f, sun.elevation - io.MouseDelta.y * 0.3f));
            pc_lights_mark_dirty();
            pc_undo_hint("Turn the sun");
        }
    }
    draw->AddCircleFilled(b, 10.0f, col, 24);
    draw->AddCircle(b, 10.0f, over || dragging ? IM_COL32(255, 255, 255, 255) : IM_COL32(30, 30, 30, 200), 24, 2.0f);
    for (int i = 0; i < 8; i++) { /* rays */
        float t = i * 0.785398f;
        draw->AddLine(ImVec2(b.x + cosf(t) * 13, b.y + sinf(t) * 13), ImVec2(b.x + cosf(t) * 18, b.y + sinf(t) * 18), col,
            1.5f);
    }
    char label[64];
    snprintf(label, sizeof label, "sun %.0f / %.0f deg", sun.azimuth, sun.elevation);
    draw->AddText(ImVec2(b.x + 20, b.y - 7), IM_COL32(255, 240, 200, 230), label);
    draw->PopClipRect();
}

/* ---- RPC environment ---- */

namespace {
std::string jbool(bool b) { return b ? "true" : "false"; }
std::string jvec(const float* v, int n)
{
    std::string s = "[";
    for (int i = 0; i < n; i++)
        s += (i ? "," : "") + lighting::format_number(v[i], 4);
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
} // namespace

/* params: {sun: {enabled, azimuth, elevation, color, intensity, angle,
 * shadows}, sky: {enabled, zenith, horizon, ground, intensity, sun_tint,
 * ambient}, remove: "sun" | "sky" | "all", global: bool}. Setting a field
 * of a sun or sky the room does not have adds it (from the defaults). The
 * reply: the room's sun and sky and the renderers' packed record. Save with
 * lighting {action: "save"}. */
extern "C" int pc_environment_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_lights::track_room();
    bool changed = false;
    if (const prefab::Json* g = params.get("global"))
        pc_env_global = g->type == prefab::Json::Bool ? g->boolean : g->number != 0;
    if (const prefab::Json* r = params.get("remove")) {
        if (r->string == "sun" || r->string == "all")
            sun = Sun(), changed = true;
        if (r->string == "sky" || r->string == "all")
            sky = Sky(), changed = true;
    }
    if (const prefab::Json* s = params.get("sun")) {
        if (!sun.defined)
            sun = Sun(), sun.defined = true;
        read_bool(*s, "enabled", &sun.enabled);
        read_num(*s, "azimuth", &sun.azimuth);
        read_num(*s, "elevation", &sun.elevation);
        read_vec(s->get("color"), sun.color, 3);
        read_num(*s, "intensity", &sun.intensity);
        read_num(*s, "angle", &sun.angle);
        read_bool(*s, "shadows", &sun.shadows);
        changed = true;
    }
    if (const prefab::Json* s = params.get("sky")) {
        if (!sky.defined)
            sky = Sky(), sky.defined = true;
        read_bool(*s, "enabled", &sky.enabled);
        read_vec(s->get("zenith"), sky.zenith, 3);
        read_vec(s->get("horizon"), sky.horizon, 3);
        read_vec(s->get("ground"), sky.ground, 3);
        read_num(*s, "intensity", &sky.intensity);
        read_num(*s, "sun_tint", &sky.sun_tint);
        read_num(*s, "ambient", &sky.ambient);
        changed = true;
    }
    changed |= rood_atmo::rpc(params); /* fog, volumes, fog_time (atmosphere.cpp) */
    if (changed)
        pc_lights_mark_dirty();
    pc_env_gpu g;
    int active = pc_env_gpu_get(&g);
    std::string s = "{\"zone\":" + std::to_string(rood_lights::zone) + ",\"room\":" + std::to_string(rood_lights::room)
        + ",\"global\":" + jbool(pc_env_global) + ",\"active\":" + jbool(active) + ",\"sun\":{\"defined\":"
        + jbool(sun.defined) + ",\"enabled\":" + jbool(sun.enabled) + ",\"azimuth\":" + num(sun.azimuth, 2)
        + ",\"elevation\":" + num(sun.elevation, 2) + ",\"color\":" + jvec(sun.color, 3)
        + ",\"intensity\":" + num(sun.intensity, 3) + ",\"angle\":" + num(sun.angle, 2)
        + ",\"shadows\":" + jbool(sun.shadows) + "},\"sky\":{\"defined\":" + jbool(sky.defined)
        + ",\"enabled\":" + jbool(sky.enabled) + ",\"zenith\":" + jvec(sky.zenith, 3) + ",\"horizon\":"
        + jvec(sky.horizon, 3) + ",\"ground\":" + jvec(sky.ground, 3) + ",\"intensity\":" + num(sky.intensity, 3)
        + ",\"sun_tint\":" + num(sky.sun_tint, 3) + ",\"ambient\":" + num(sky.ambient, 3) + "},\"gpu\":{\"sun_dir\":"
        + jvec(g.sun_dir, 4) + ",\"sun_col\":" + jvec(g.sun_col, 4) + ",\"zenith\":" + jvec(g.zenith, 4)
        + ",\"horizon\":" + jvec(g.horizon, 4) + ",\"ground\":" + jvec(g.ground, 4) + "},\"hash\":"
        + std::to_string(pc_env_hash()) + "," + rood_atmo::json() + "}";
    snprintf(out, size, "%s", s.c_str());
    return (int)s.size() < size ? 0 : -1;
}
