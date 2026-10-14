// Light actors in the editor: the Place Actors panel (UE-style: drag a
// light or a prefab into the view, or double-click to place it at the
// view's centre), actor rows in the Outliner (expanding to their lights),
// actor Details with the prefab's exposed parameters (overridden ones
// highlighted, with a reset arrow), the prefab editor, glow attachment and
// suggestions, and the RPC methods actors, prefabs and editor_place.
// The data model is light_prefabs.h; the room and viewport are lights.cpp.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_overlays.h"
#include "glow_overrides.h"
#include "lights_internal.h"
#include "editor_undo.h"

using namespace rood_lights;
using prefab::Actor;
using prefab::Json;
using prefab::Prefab;
using prefab::Value;

extern "C" void pc_lights_save(void);
extern "C" void pc_lights_set_time(double t);
extern "C" int pc_fixtures_rpc(const char* params, char* out, int size); /* fixture_editor.cpp */
namespace rood_lights {
bool fixture_type_details(int index); /* fixture_editor.cpp */
}
extern "C" int pc_lights_sample_actor(int index, double t, char* out, int size);
extern "C" int pc_lights_dropped(void); /* lights.cpp: the renderer budget last frame */
extern "C" int pc_lights_wanted(void);
extern "C" int pc_lights_budget;

namespace {
const ImVec4 overridden_colour(1.0f, 0.78f, 0.3f, 1); /* as room settings' overrides */

/* ------------------------------------------------------------ icons */

ImU32 rgba(const float c[3], float alpha, float scale = 1)
{
    auto ch = [&](float v) { return (int)(fminf(fmaxf(v * scale, 0), 1) * 255); };
    return IM_COL32(ch(c[0]), ch(c[1]), ch(c[2]), (int)(alpha * 255));
}
void flame(ImDrawList* d, ImVec2 c, float r, ImU32 col)
{
    d->AddCircleFilled(c, r, col, 12);
    d->AddTriangleFilled(ImVec2(c.x - r * 0.95f, c.y - r * 0.3f), ImVec2(c.x + r * 0.95f, c.y - r * 0.3f),
        ImVec2(c.x, c.y - r * 2.4f), col);
}
} // namespace

/* A small drawn icon for a light type or a prefab ("prefab:<id>" uses the
 * prefab's icon field), centred at (x, y), `size` across, tinted by the
 * light colour. */
void rood_lights::draw_icon(void* list, float x, float y, float size, const std::string& item, const float color[3],
    float alpha)
{
    ImDrawList* d = (ImDrawList*)list;
    float h = size * 0.5f;
    std::string icon = item;
    if (item.rfind("prefab:", 0) == 0) {
        const Prefab* p = prefab::library().find(item.substr(7));
        icon = p && !p->icon.empty() ? p->icon : "light:point";
    }
    ImU32 col = rgba(color, alpha), line = IM_COL32(220, 220, 220, (int)(alpha * 255));
    ImU32 wood = IM_COL32(140, 95, 60, (int)(alpha * 255)), wax = IM_COL32(235, 225, 200, (int)(alpha * 255));
    ImVec2 c(x, y);
    if (icon == "light:spot") {
        d->AddTriangleFilled(ImVec2(x, y - h * 0.8f), ImVec2(x - h * 0.75f, y + h * 0.8f), ImVec2(x + h * 0.75f, y + h * 0.8f),
            rgba(color, alpha * 0.55f));
        d->AddCircleFilled(ImVec2(x, y - h * 0.7f), h * 0.28f, col);
    } else if (icon == "light:rect" || icon == "window") {
        ImVec2 lo(x - h * 0.7f, y - h * 0.8f), hi(x + h * 0.7f, y + h * 0.8f);
        d->AddRectFilled(lo, hi, rgba(color, alpha * 0.8f));
        if (icon == "window") {
            d->AddLine(ImVec2(x, lo.y), ImVec2(x, hi.y), IM_COL32(40, 40, 50, (int)(alpha * 255)), 1.5f);
            d->AddLine(ImVec2(lo.x, y), ImVec2(hi.x, y), IM_COL32(40, 40, 50, (int)(alpha * 255)), 1.5f);
        }
        d->AddRect(lo, hi, line, 0.0f, ImDrawFlags_None, 1.5f);
    } else if (icon == "candle") {
        d->AddRectFilled(ImVec2(x - h * 0.22f, y - h * 0.1f), ImVec2(x + h * 0.22f, y + h * 0.9f), wax, 1.5f);
        flame(d, ImVec2(x, y - h * 0.35f), h * 0.24f, col);
    } else if (icon == "candelabra") {
        d->AddLine(ImVec2(x - h * 0.75f, y + h * 0.15f), ImVec2(x + h * 0.75f, y + h * 0.15f), line, 1.5f);
        d->AddLine(ImVec2(x, y + h * 0.15f), ImVec2(x, y + h * 0.9f), line, 1.5f);
        for (int k = -1; k <= 1; k++) {
            float cx = x + k * h * 0.7f;
            d->AddRectFilled(ImVec2(cx - h * 0.1f, y - h * 0.2f), ImVec2(cx + h * 0.1f, y + h * 0.15f), wax);
            flame(d, ImVec2(cx, y - h * 0.38f), h * 0.15f, col);
        }
    } else if (icon == "lantern") {
        d->AddCircleFilled(ImVec2(x, y + h * 0.1f), h * 0.45f, rgba(color, alpha * 0.9f));
        d->AddRect(ImVec2(x - h * 0.5f, y - h * 0.45f), ImVec2(x + h * 0.5f, y + h * 0.75f), line, 2.0f, ImDrawFlags_None, 1.5f);
        d->AddLine(ImVec2(x - h * 0.6f, y - h * 0.45f), ImVec2(x + h * 0.6f, y - h * 0.45f), line, 2.0f);
        d->AddBezierQuadratic(ImVec2(x - h * 0.3f, y - h * 0.45f), ImVec2(x, y - h * 1.1f), ImVec2(x + h * 0.3f, y - h * 0.45f),
            line, 1.5f);
    } else if (icon == "torch") {
        d->AddLine(ImVec2(x - h * 0.35f, y + h * 0.9f), ImVec2(x + h * 0.05f, y - h * 0.05f), wood, h * 0.22f);
        flame(d, ImVec2(x + h * 0.08f, y - h * 0.2f), h * 0.32f, col);
    } else if (icon == "brazier") {
        d->PathArcTo(ImVec2(x, y + h * 0.05f), h * 0.7f, 0.0f, 3.14159f);
        d->PathFillConvex(IM_COL32(110, 100, 95, (int)(alpha * 255)));
        d->AddLine(ImVec2(x - h * 0.4f, y + h * 0.6f), ImVec2(x - h * 0.55f, y + h * 0.95f), line, 1.5f);
        d->AddLine(ImVec2(x + h * 0.4f, y + h * 0.6f), ImVec2(x + h * 0.55f, y + h * 0.95f), line, 1.5f);
        flame(d, ImVec2(x - h * 0.22f, y - h * 0.15f), h * 0.25f, col);
        flame(d, ImVec2(x + h * 0.25f, y - h * 0.05f), h * 0.2f, col);
        flame(d, ImVec2(x, y - h * 0.3f), h * 0.28f, col);
    } else if (icon == "fireplace") {
        d->AddRectFilled(ImVec2(x - h * 0.9f, y - h * 0.8f), ImVec2(x + h * 0.9f, y + h * 0.9f), IM_COL32(95, 85, 80, (int)(alpha * 255)), 2);
        d->AddRectFilled(ImVec2(x - h * 0.55f, y - h * 0.3f), ImVec2(x + h * 0.55f, y + h * 0.9f), IM_COL32(20, 15, 15, (int)(alpha * 255)));
        flame(d, ImVec2(x - h * 0.15f, y + h * 0.55f), h * 0.22f, col);
        flame(d, ImVec2(x + h * 0.18f, y + h * 0.6f), h * 0.18f, col);
    } else if (icon == "magic") {
        ImVec2 star[8];
        for (int k = 0; k < 8; k++) {
            float a = k * 3.14159265f / 4, r = (k & 1) ? h * 0.3f : h * 0.95f;
            star[k] = ImVec2(x + sinf(a) * r, y - cosf(a) * r);
        }
        for (int k = 0; k < 8; k += 2) /* four points (non-convex star as triangles) */
            d->AddTriangleFilled(c, star[(k + 7) % 8], star[k], col), d->AddTriangleFilled(c, star[k], star[(k + 1) % 8], col);
        d->AddCircle(c, h * 0.55f, rgba(color, alpha * 0.7f), 16, 1.2f);
    } else { /* point light: a bulb with rays */
        d->AddCircleFilled(c, h * 0.42f, col, 16);
        for (int k = 0; k < 8; k++) {
            float a = k * 3.14159265f / 4;
            d->AddLine(ImVec2(x + cosf(a) * h * 0.6f, y + sinf(a) * h * 0.6f), ImVec2(x + cosf(a) * h * 0.92f, y + sinf(a) * h * 0.92f),
                col, 1.5f);
        }
    }
}

namespace {

/* ------------------------------------------------------------ helpers */

/* The colour an item shows (its first light's, at its defaults). */
void item_colour(const std::string& item, float out[3])
{
    out[0] = 1, out[1] = 0.75f, out[2] = 0.45f; /* a new light's default */
    if (item.rfind("prefab:", 0) != 0)
        return;
    static std::map<std::string, std::vector<float>> cache;
    static int serial = -1;
    if (serial != prefab::library().serial) {
        cache.clear();
        serial = prefab::library().serial;
    }
    auto it = cache.find(item);
    if (it == cache.end()) {
        std::vector<float> c = { 1, 0.75f, 0.45f };
        if (const Prefab* p = prefab::library().find(item.substr(7))) {
            Actor a;
            a.prefab = p->id;
            std::vector<prefab::EvalLight> ls;
            prefab::evaluate(*p, a, 0, false, ls);
            if (!ls.empty())
                c = { ls[0].color[0], ls[0].color[1], ls[0].color[2] };
        }
        it = cache.emplace(item, c).first;
    }
    memcpy(out, it->second.data(), 3 * sizeof(float));
}

std::string actor_label(int i)
{
    const Prefab* p = prefab::library().find(actors[i].prefab);
    char text[128];
    snprintf(text, sizeof text, "%s %d%s%s%s", p ? p->name.c_str() : actors[i].prefab.c_str(), i + 1,
        p ? "" : " (missing prefab)", actors[i].fixture_type.empty() ? "" : " ", actors[i].fixture_type.c_str());
    return text;
}

void value_text(const Value& v, char* out, size_t size)
{
    if (v.n == 3)
        snprintf(out, size, "%g, %g, %g", v.v[0], v.v[1], v.v[2]);
    else
        snprintf(out, size, "%g", v.v[0]);
}

std::string format_param(const prefab::Param& q, const Value& v)
{
    char text[96];
    if (q.type == prefab::PARAM_BOOL)
        return v.x() > 0.5f ? "on" : "off";
    if (q.type == prefab::PARAM_ENUM) {
        int i = (int)lroundf(v.x());
        return i >= 0 && i < (int)q.options.size() ? q.options[i] : std::to_string(i);
    }
    value_text(v, text, sizeof text);
    return text + (q.unit.empty() ? std::string() : " " + q.unit);
}

/* Drop the actor at `index`, keeping selection and solo consistent. */
void remove_actor(int index)
{
    actors.erase(actors.begin() + index);
    if (solo_actor == index)
        solo_actor = -1;
    else if (solo_actor > index)
        --solo_actor;
    select_none();
    dirty = true;
}

/* The prefab's JSON with each parameter's default replaced by `a`'s value. */
Json with_actor_values(const Prefab& p, const Actor& a)
{
    Json json = p.source;
    if (Json* params = json.get("params"))
        for (Json& q : params->items) {
            const Json* key = q.get("key");
            if (!key || key->type != Json::String)
                continue;
            const Value* v = a.override_of(key->string);
            const prefab::Param* param = p.param(key->string);
            if (!v || !param)
                continue;
            if (param->type == prefab::PARAM_COLOR) {
                Json c = Json::array();
                for (int k = 0; k < 3; k++)
                    c.items.push_back(Json::num(roundf(v->at(k) * 1000) / 1000));
                q.set("default", c);
            } else if (param->type == prefab::PARAM_BOOL) {
                q.set("default", Json::boolean_(v->x() > 0.5f));
            } else if (param->type == prefab::PARAM_ENUM) {
                int i = (int)lroundf(v->x());
                q.set("default", i >= 0 && i < (int)param->options.size() ? Json::str(param->options[i]) : Json::num(i));
            } else {
                q.set("default", Json::num(roundf(v->x() * 10000) / 10000));
            }
        }
    return json;
}

std::string unique_id(const std::string& name)
{
    std::string base = prefab::make_id(name), id = base;
    for (int n = 2; prefab::library().find(id); n++)
        id = base + "_" + std::to_string(n);
    return id;
}

} // namespace

/* ------------------------------------------------------- prefab files */

bool rood_lights::create_prefab_from_selection(const std::string& name)
{
    std::vector<int> picked;
    if (selected >= 0 && selected < (int)lights.size())
        picked.push_back(selected);
    for (int i : multi)
        if (i >= 0 && i < (int)lights.size() && std::find(picked.begin(), picked.end(), i) == picked.end())
            picked.push_back(i);
    if (picked.empty()) {
        snprintf(status, sizeof status, "Create prefab: select one or more lights first");
        return false;
    }
    /* The pivot: below the lights' centre, on the lowest one's height
     * (y grows downward). */
    float pivot[3] = { 0, -1e9f, 0 };
    for (int i : picked) {
        pivot[0] += lights[i].position[0] / picked.size();
        pivot[2] += lights[i].position[2] / picked.size();
        pivot[1] = fmaxf(pivot[1], lights[i].position[1]);
    }
    Json json = Json::object();
    json.set("name", Json::str(name));
    json.set("category", Json::str("My prefabs"));
    json.set("icon", Json::str(lights[picked[0]].type == 1 ? "light:spot" : lights[picked[0]].type == 2 ? "light:rect" : "light:point"));
    json.set("description", Json::str("Made from " + std::to_string(picked.size()) + " light(s) with Create prefab."));
    Json placement = Json::object();
    placement.set("align", Json::str("up"));
    placement.set("surface_offset", Json::num(0));
    json.set("placement", placement);
    auto param = [](const char* key, const char* label, const char* type, Json def, float lo, float hi, bool log) {
        Json q = Json::object();
        q.set("key", Json::str(key));
        q.set("label", Json::str(label));
        q.set("type", Json::str(type));
        q.set("default", def);
        if (!strcmp(type, "float")) {
            q.set("min", Json::num(lo));
            q.set("max", Json::num(hi));
            if (log)
                q.set("log", Json::boolean_(true));
        }
        return q;
    };
    Json white = Json::array();
    for (int k = 0; k < 3; k++)
        white.items.push_back(Json::num(1));
    Json params = Json::array();
    params.items.push_back(param("tint", "Tint", "color", white, 0, 1, false));
    params.items.push_back(param("intensity", "Intensity", "float", Json::num(1), 0, 8, false));
    params.items.push_back(param("radius_scale", "Radius scale", "float", Json::num(1), 0.1f, 8, true));
    params.items.push_back(param("flicker", "Flicker amount", "float", Json::num(0), 0, 1, false));
    params.items.push_back(param("flicker_speed", "Flicker speed", "float", Json::num(6), 0.1f, 30, true));
    params.items.push_back(param("shadows", "Casts shadows", "bool", Json::boolean_(lights[picked[0]].shadows), 0, 1, false));
    json.set("params", params);
    Json out = Json::array();
    auto r3 = [](float v) { return Json::num(roundf(v * 1000) / 1000); };
    int index = 0;
    for (int i : picked) {
        const Light& l = lights[i];
        Json j = Json::object();
        j.set("name", Json::str(std::string(light_type_name(l.type)) + " " + std::to_string(++index)));
        j.set("type", Json::str(l.type == 1 ? "spot" : l.type == 2 ? "rect" : "point"));
        Json offset = Json::array(), direction = Json::array();
        for (int k = 0; k < 3; k++) {
            offset.items.push_back(r3(l.position[k] - pivot[k]));
            direction.items.push_back(r3(l.direction[k]));
        }
        j.set("offset", offset);
        char text[160];
        snprintf(text, sizeof text, "vec(%.3f, %.3f, %.3f) * tint", l.color[0], l.color[1], l.color[2]);
        j.set("color", Json::str(text));
        snprintf(text, sizeof text, "intensity * %.3f", l.intensity);
        j.set("intensity", Json::str(text));
        snprintf(text, sizeof text, "radius_scale * %.1f", l.radius);
        j.set("radius", Json::str(text));
        j.set("shadows", Json::str(l.shadows ? "shadows" : "0"));
        if (l.type != 0)
            j.set("direction", direction);
        if (l.type == 1)
            j.set("cone", Json::num(l.cone));
        if (l.type == 2) {
            Json size = Json::array();
            size.items.push_back(r3(l.size[0]));
            size.items.push_back(r3(l.size[1]));
            j.set("size", size);
        }
        j.set("flicker", Json::str("flicker"));
        j.set("flicker_speed", Json::str("flicker_speed"));
        j.set("phase", Json::num(index * 0.37));
        out.items.push_back(j);
    }
    json.set("lights", out);
    std::string id = unique_id(name), error;
    if (!prefab::library().save_user(id, json, &error)) {
        snprintf(status, sizeof status, "%s", error.c_str());
        return false;
    }
    /* Replace the lights with an actor of the new prefab. */
    std::sort(picked.begin(), picked.end());
    for (int k = (int)picked.size() - 1; k >= 0; k--)
        lights.erase(lights.begin() + picked[k]);
    solo = -1;
    Actor a;
    a.prefab = id;
    memcpy(a.position, pivot, sizeof pivot);
    a.seed = prefab::hash32((uint32_t)actors.size() + 12345u) % 1000000u + 1;
    actors.push_back(a);
    select_actor((int)actors.size() - 1);
    dirty = true;
    snprintf(status, sizeof status, "Created prefab %s (%s/%s.json); the room has unsaved changes", name.c_str(),
        prefab::library().user_dir.c_str(), id.c_str());
    return true;
}

bool rood_lights::save_actor_as_prefab(int index, const std::string& name)
{
    if (index < 0 || index >= (int)actors.size())
        return false;
    const Prefab* p = prefab::library().find(actors[index].prefab);
    if (!p) {
        snprintf(status, sizeof status, "Save as prefab: the actor's prefab is missing");
        return false;
    }
    Json json = with_actor_values(*p, actors[index]);
    json.set("name", Json::str(name));
    std::string id = unique_id(name), error;
    if (!prefab::library().save_user(id, json, &error)) {
        snprintf(status, sizeof status, "%s", error.c_str());
        return false;
    }
    actors[index].prefab = id;
    actors[index].overrides.clear();
    dirty = true;
    snprintf(status, sizeof status, "Saved prefab %s (%s/%s.json)", name.c_str(), prefab::library().user_dir.c_str(), id.c_str());
    return true;
}

bool rood_lights::apply_actor_to_prefab(int index)
{
    if (index < 0 || index >= (int)actors.size())
        return false;
    const Prefab* p = prefab::library().find(actors[index].prefab);
    if (!p || actors[index].overrides.empty())
        return false;
    std::string id = p->id, error;
    Json json = with_actor_values(*p, actors[index]);
    if (!prefab::library().save_user(id, json, &error)) {
        snprintf(status, sizeof status, "%s", error.c_str());
        return false;
    }
    actors[index].overrides.clear();
    dirty = true;
    snprintf(status, sizeof status, "Applied to prefab %s (%s/%s.json): every instance follows", id.c_str(),
        prefab::library().user_dir.c_str(), id.c_str());
    return true;
}

/* ------------------------------------------------------- glow attach */

/* Link an actor to the glow at p (its key: the link survives the glow
 * drifting, reloads and other glows nearby). */
static void link_glow(Actor& a, const float p[3])
{
    a.glow_linked = true;
    memcpy(a.glow, p, sizeof a.glow);
    pc_glow_info g;
    uint32_t id = pc_gi_glow_near(p, 96);
    a.glow_key = id && pc_gi_glow_get(id, &g) ? g.key : "";
}

int rood_lights::create_fixture_actor(const std::string& prefab_id, const float flame[3], const float* glow, const float* normal)
{
    const Prefab* p = prefab::library().find(prefab_id);
    if (!p)
        return -1;
    track_room();
    Actor a;
    a.prefab = p->id;
    if (glow)
        link_glow(a, glow);
    a.fixture = true;
    memcpy(a.fixture_at, flame, sizeof a.fixture_at);
    a.fixture_radius = p->fixture_radius;
    a.seed = prefab::hash32((uint32_t)(flame[0] * 7 + flame[1] * 13 + flame[2] * 17) + (uint32_t)actors.size()) % 1000000u + 1;
    if (normal && p->align == "normal")
        prefab::rotation_facing(normal, a.rotation);
    /* The pivot puts the prefab's first light on the flame. */
    std::vector<prefab::EvalLight> ls;
    prefab::evaluate(*p, a, 0, false, ls);
    for (int k = 0; k < 3; k++)
        a.position[k] = flame[k] - (ls.empty() ? 0 : ls[0].position[k]);
    actors.push_back(a);
    dirty = true;
    return (int)actors.size() - 1;
}

int rood_lights::attach_to_glow(const std::string& prefab_id, const float glow[3])
{
    return create_fixture_actor(prefab_id, glow, glow, nullptr);
}

namespace {
bool warm_colour(const float c[3]) { return c[0] >= c[1] && c[0] > c[2] * 1.25f; }
bool cool_colour(const float c[3]) { return c[2] > c[0] * 1.1f && c[2] >= c[1] * 0.85f; }
/* The prefab a glow (colour, emitting size) or a texture flame (strength)
 * suggests, by the prefabs' glow.suggest rules; empty when none fits. */
std::string prefab_for(bool flame, const float color[3], float size, float weight)
{
    float top = fmaxf(color[0], fmaxf(color[1], color[2]));
    const Prefab* best = nullptr;
    for (const Prefab& p : prefab::library().prefabs) {
        const prefab::GlowSuggest& s = p.suggest;
        if (!s.enabled || !p.error.empty())
            continue;
        if (flame) {
            if (!s.flames || weight < s.flame_min || weight > s.flame_max)
                continue;
        } else {
            bool hue = s.hue == "any" || (s.hue == "warm" && warm_colour(color)) || (s.hue == "cool" && cool_colour(color));
            if (!hue || size < s.min_size || size > s.max_size || top < s.min_brightness)
                continue;
        }
        if (!best || s.priority > best->suggest.priority)
            best = &p;
    }
    return best ? best->id : "";
}
/* An actor already lights the flame at p (its fixture, glow or a light near it). */
bool flame_taken(const float p[3], float radius)
{
    std::vector<prefab::EvalLight> ls;
    auto near = [&](const float q[3], float r) {
        float d[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
        return d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < r * r;
    };
    for (const Actor& a : actors) {
        if ((a.glow_linked && near(a.glow, radius)) || (a.fixture && near(a.fixture_at, radius)))
            return true;
        actor_lights(a, ls, false);
        for (const prefab::EvalLight& e : ls)
            if (near(e.position, radius * 0.75f))
                return true;
    }
    return false;
}
} // namespace

std::vector<Suggestion> rood_lights::suggest_for_glows()
{
    std::vector<Suggestion> out;
    float pos[32][4], col[32][4];
    int muted[32];
    int n = pc_gi_emitters(pos, col, muted, 32);
    for (int i = 0; i < n; i++) {
        if (col[i][3] < 0 || flame_taken(pos[i], 64)) /* a baked-light estimate, or lit already */
            continue;
        /* Part of a glow already suggested for (a sigil's several clusters). */
        bool merged = false;
        for (const Suggestion& s : out) {
            const Prefab* q = prefab::library().find(s.prefab);
            float d[3] = { s.glow[0] - pos[i][0], s.glow[1] - pos[i][1], s.glow[2] - pos[i][2] };
            merged |= q && q->suggest.merge > 0 && d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < q->suggest.merge * q->suggest.merge;
        }
        std::string id = prefab_for(false, col[i], col[i][3], 0);
        if (merged || id.empty())
            continue;
        Suggestion s;
        memcpy(s.glow, pos[i], sizeof s.glow);
        memcpy(s.color, col[i], sizeof s.color);
        s.size = col[i][3];
        s.prefab = id;
        out.push_back(s);
    }
    /* Flames painted in the room's textures (candles, lanterns without a glow). */
    float flames[64][4];
    int m = pc_gi_flames(flames, 64);
    for (int i = 0; i < m; i++) {
        bool near_glow = false;
        for (int g = 0; g < n; g++) {
            float d[3] = { pos[g][0] - flames[i][0], pos[g][1] - flames[i][1], pos[g][2] - flames[i][2] };
            near_glow |= col[g][3] >= 0 && d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 64.0f * 64.0f;
        }
        float warm[3] = { 1, 0.6f, 0.2f };
        std::string id = prefab_for(true, warm, 0, flames[i][3]);
        if (near_glow || flame_taken(flames[i], 48) || id.empty())
            continue;
        Suggestion s;
        memcpy(s.glow, flames[i], sizeof s.glow);
        memcpy(s.color, warm, sizeof s.color);
        s.size = 0;
        s.flame = true;
        s.accept = false; /* texture flames are less certain than glows: ticked by the user */
        s.weight = flames[i][3];
        s.prefab = id;
        out.push_back(s);
    }
    return out;
}

bool rood_lights::fixture_mode;

int rood_lights::light_fixture_at(const float hit[3], const float normal[3], const std::string& prefab_id)
{
    track_room();
    /* A glow near the click is the fixture's flame (and gets linked). */
    float pos[32][4], col[32][4];
    int muted[32], glow = -1;
    int n = pc_gi_emitters(pos, col, muted, 32);
    float best = 128.0f * 128.0f;
    for (int i = 0; i < n; i++) {
        float d[3] = { pos[i][0] - hit[0], pos[i][1] - hit[1], pos[i][2] - hit[2] };
        float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (col[i][3] >= 0 && dd < best)
            best = dd, glow = i;
    }
    float flame[4];
    bool painted = !(glow >= 0) && pc_gi_flame_near(hit, 80, flame);
    if (glow < 0 && !painted) {
        snprintf(status, sizeof status, "No flame here: no glow within 128 units and no bright warm texels within 80");
        return -1;
    }
    float at[3];
    memcpy(at, glow >= 0 ? pos[glow] : flame, sizeof at);
    for (size_t i = 0; i < actors.size(); i++)
        if (actors[i].fixture || actors[i].glow_linked) {
            const float* q = actors[i].fixture ? actors[i].fixture_at : actors[i].glow;
            float d[3] = { q[0] - at[0], q[1] - at[1], q[2] - at[2] };
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 24.0f * 24.0f) {
                select_actor((int)i);
                snprintf(status, sizeof status, "This fixture is lit already (actor %zu selected)", i + 1);
                return (int)i;
            }
        }
    std::string id = prefab_id;
    if (id.empty()) {
        float warm[3] = { 1, 0.6f, 0.2f };
        id = glow >= 0 ? prefab_for(false, col[glow], col[glow][3], 0) : prefab_for(true, warm, 0, flame[3]);
        if (id.empty())
            id = glow >= 0 && cool_colour(col[glow]) ? "magic_glow" : "candle";
    }
    pc_undo_hint("Light fixture");
    int index = create_fixture_actor(id, at, glow >= 0 ? pos[glow] : nullptr, normal);
    if (index < 0) {
        snprintf(status, sizeof status, "Unknown prefab %s", id.c_str());
        return -1;
    }
    select_actor(index);
    gizmo_operation = 0;
    snprintf(status, sizeof status, "Lit the fixture at %.0f %.0f %.0f with a %s%s (unsaved)", at[0], at[1], at[2],
        prefab::library().find(id)->name.c_str(), glow >= 0 ? ", linked to its glow" : " (flame found in its texture)");
    return index;
}

int rood_lights::light_fixture_at_pixel(float x, float y, const std::string& prefab_id)
{
    Placement where;
    if (!place_compute("light:point", x, y, PC_SNAP_MODE_SURFACE, where)) {
        snprintf(status, sizeof status, "Nothing under the cursor to light");
        return -1;
    }
    return light_fixture_at(where.surface, where.normal, prefab_id);
}
/* Delete (the key, Edit > Delete): the selected actor, or the selected
 * lights (with the ones Ctrl+clicked in the Outliner). Glows come from the
 * game: they can be muted, not deleted. Returns how many went. */
extern "C" int pc_lights_delete_selection(void)
{
    track_room();
    if (selected_actor >= 0 && selected_actor < (int)actors.size()) {
        pc_undo_hint(("Delete " + actor_label(selected_actor)).c_str());
        remove_actor(selected_actor);
        snprintf(status, sizeof status, "Deleted the actor (unsaved; Ctrl+Z undoes)");
        return 1;
    }
    if (selected >= 0 && selected < (int)lights.size()) {
        std::vector<int> gone = multi;
        gone.push_back(selected);
        std::sort(gone.begin(), gone.end());
        gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
        std::string name = gone.size() == 1
            ? std::string("Delete ") + light_type_name(lights[selected].type) + " " + std::to_string(selected + 1)
            : "Delete " + std::to_string(gone.size()) + " lights";
        pc_undo_hint(name.c_str());
        for (int k = (int)gone.size() - 1; k >= 0; k--) {
            int i = gone[k];
            if (i < 0 || i >= (int)lights.size())
                continue;
            lights.erase(lights.begin() + i);
            if (solo == i)
                solo = -1;
            else if (solo > i)
                --solo;
        }
        select_none();
        dirty = true;
        snprintf(status, sizeof status, "Deleted %zu light%s (unsaved; Ctrl+Z undoes)", gone.size(), gone.size() == 1 ? "" : "s");
        return (int)gone.size();
    }
    if (glow_selected)
        snprintf(status, sizeof status, "Glows come from the game: untick one in the Outliner to mute it");
    return 0;
}
extern "C" int pc_lights_has_selection(void)
{
    return (selected_actor >= 0 && selected_actor < (int)actors.size()) || (selected >= 0 && selected < (int)lights.size());
}
/* L over the view (dev_overlay.cpp). */
extern "C" int pc_lights_light_fixture(float x, float y) { return light_fixture_at_pixel(x, y, ""); }
extern "C" bool* pc_lights_fixture_mode(void) { return &fixture_mode; }

namespace {
std::vector<Suggestion> pending; /* the Suggestions window's list */
bool suggestions_open;
void refresh_ghosts()
{
    ghosts.clear();
    for (const Suggestion& s : pending) {
        if (!s.accept)
            continue;
        const Prefab* p = prefab::library().find(s.prefab);
        if (!p)
            continue;
        Ghost g;
        g.actor = true;
        g.a.prefab = p->id;
        g.a.seed = 3;
        std::vector<prefab::EvalLight> ls;
        prefab::evaluate(*p, g.a, 0, false, ls);
        for (int k = 0; k < 3; k++)
            g.a.position[k] = s.glow[k] - (ls.empty() ? 0 : ls[0].position[k]);
        ghosts.push_back(g);
    }
}
int accept_pending()
{
    int added = 0;
    for (const Suggestion& s : pending)
        if (s.accept && (s.flame ? create_fixture_actor(s.prefab, s.glow, nullptr, nullptr) : attach_to_glow(s.prefab, s.glow)) >= 0)
            ++added;
    if (added)
        pc_undo_hint("Light all fixtures in room");
    pending.clear();
    ghosts.clear();
    suggestions_open = false;
    snprintf(status, sizeof status, "Added %d actors linked to glows (unsaved)", added);
    return added;
}
const char* prefab_combo_items(void* data, int i)
{
    (void)data;
    return prefab::library().prefabs[i].name.c_str();
}
} // namespace

/* Light all fixtures in room (Outliner, Glows; the toolbar's Light fixture
 * menu): an actor for every flame not lit yet, to review. */
extern "C" void pc_lights_light_all_fixtures(void)
{
    pending = suggest_for_glows();
    suggestions_open = true;
    refresh_ghosts();
    if (pending.empty())
        snprintf(status, sizeof status, "No unlit fixtures found (glows or flame texels)");
}
void rood_lights::glows_outliner_actions()
{
    if (ImGui::SmallButton("Light all fixtures in room"))
        pc_lights_light_all_fixtures();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A candle, lantern, torch or magic glow actor at every flame in the room (its glow, or the\n"
                          "bright warm texels of a lantern or candle), shown as ghosts for you to review and add");
}

void rood_lights::suggestions_window()
{
    if (!suggestions_open)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.35f, io.DisplaySize.y * 0.3f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, ImGui::GetFontSize() * 18), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Light all fixtures in room", &open)) {
        ImGui::End();
        return;
    }
    if (pending.empty())
        ImGui::TextWrapped("Every flame in this room already has an actor near it, or none fits a prefab's suggestion rule "
                           "(glow.suggest in the prefab files).");
    bool changed = false;
    if (ImGui::BeginTable("suggestions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Flame");
        ImGui::TableSetupColumn("Found", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
        ImGui::TableSetupColumn("Actor");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < pending.size(); i++) {
            Suggestion& s = pending[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##accept", &s.accept);
            ImGui::SameLine();
            ImGui::ColorButton("##c", ImVec4(fminf(s.color[0], 1), fminf(s.color[1], 1), fminf(s.color[2], 1), 1),
                ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            ImGui::SameLine();
            if (ImGui::Selectable("##pick", glow_selected && fabsf(glow_at[0] - s.glow[0]) < 1 && fabsf(glow_at[2] - s.glow[2]) < 1,
                    ImGuiSelectableFlags_AllowOverlap))
                select_glow(s.glow);
            ImGui::SameLine();
            ImGui::Text("%.0f %.0f %.0f", s.glow[0], s.glow[1], s.glow[2]);
            ImGui::TableNextColumn();
            if (s.flame)
                ImGui::Text("texture %.1f", s.weight);
            else
                ImGui::Text("glow %.1f", s.size);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(s.flame ? "A flame painted in the fixture's texture (strength of its bright warm texels)"
                                          : "A glow (additive flame sprite); its emitting size. The actor links it.");
            ImGui::TableNextColumn();
            int current = 0;
            const auto& all = prefab::library().prefabs;
            for (size_t k = 0; k < all.size(); k++)
                if (all[k].id == s.prefab)
                    current = (int)k;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##prefab", &current, prefab_combo_items, nullptr, (int)all.size())) {
                s.prefab = all[current].id;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (changed)
        refresh_ghosts();
    int count = 0;
    for (const Suggestion& s : pending)
        count += s.accept;
    char label[48];
    snprintf(label, sizeof label, "Add %d actor%s", count, count == 1 ? "" : "s");
    if (count == 0)
        ImGui::BeginDisabled();
    if (ImGui::Button(label))
        accept_pending();
    if (count == 0)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        open = false;
    ImGui::TextDisabled("Ghosts in the view show where they go; each links its glow (Drive: the glow flickers with it).");
    ImGui::End();
    if (!open) {
        suggestions_open = false;
        pending.clear();
        ghosts.clear();
    }
}

/* Glow Details: light its fixture (the prefab chosen from the glow), or
 * add a chosen prefab here; both linked to the glow. */
void rood_lights::glow_details_actions()
{
    ImGui::SeparatorText("Light it");
    if (ImGui::Button("Light this fixture (L)")) {
        float up[3] = { 0, -1, 0 };
        light_fixture_at(glow_at, up, "");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A light actor at this flame (candle, lantern, torch or magic glow by the glow's colour and size),\n"
                          "linked to the glow; the fixture's own polygons cast no shadow from it");
    static std::string choice = "candle";
    const auto& all = prefab::library().prefabs;
    int current = 0;
    for (size_t k = 0; k < all.size(); k++)
        if (all[k].id == choice)
            current = (int)k;
    if (all.empty()) {
        ImGui::TextDisabled("No prefabs (pc/assets/prefabs)");
        return;
    }
    if (current >= (int)all.size())
        current = 0;
    char label[96];
    snprintf(label, sizeof label, "Add %s here", all[current].name.c_str());
    if (ImGui::Button(label)) {
        int index = attach_to_glow(all[current].id, glow_at);
        if (index >= 0) {
            select_actor(index);
            snprintf(status, sizeof status, "Added %s linked to the glow (unsaved)", all[current].name.c_str());
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A light actor with its light on this glow, linked: the glow's emission follows its flicker");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    if (ImGui::Combo("##attach prefab", &current, prefab_combo_items, nullptr, (int)all.size()))
        choice = all[current].id;
}

/* ------------------------------------------------------------ Outliner */

void rood_lights::actors_outliner(const char* filter)
{
    std::vector<prefab::EvalLight> ls;
    for (int i = 0; i < (int)actors.size(); i++) {
        Actor& a = actors[i];
        std::string label = actor_label(i);
        if (filter && filter[0] && !strcasestr(label.c_str(), filter) && !strcasestr(a.prefab.c_str(), filter))
            continue;
        ImGui::PushID(10000 + i);
        if (ImGui::Checkbox("##on", &a.enabled))
            dirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enabled (mute)");
        ImGui::SameLine();
        bool soloed = solo_actor == i;
        if (soloed)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.6f, 0.1f, 1));
        if (ImGui::SmallButton("S")) {
            solo_actor = soloed ? -1 : i;
            solo = -1;
        }
        if (soloed)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Solo: light the scene with this actor only");
        ImGui::SameLine();
        float colour[3];
        item_colour("prefab:" + a.prefab, colour);
        ImVec2 at = ImGui::GetCursorScreenPos();
        float em = ImGui::GetFontSize();
        ImGui::Dummy(ImVec2(em, em));
        draw_icon(ImGui::GetWindowDrawList(), at.x + em * 0.5f, at.y + em * 0.5f, em, "prefab:" + a.prefab, colour, 1);
        ImGui::SameLine();
        actor_lights(a, ls, false);
        bool silent = !a.enabled || (solo_actor >= 0 && solo_actor != i) || solo >= 0;
        if (silent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | (selected_actor == i ? ImGuiTreeNodeFlags_Selected : 0) | (ls.empty() ? ImGuiTreeNodeFlags_Leaf : 0);
        bool open = ImGui::TreeNodeEx("##actor", flags, "%s%s%s", label.c_str(), a.overrides.empty() ? "" : " *",
            a.fixture ? "  [fixture]" : "");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            select_actor(i);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s at %.0f %.0f %.0f%s%s", a.prefab.c_str(), a.position[0], a.position[1], a.position[2],
                a.overrides.empty() ? "" : "\n* overrides prefab parameters", a.glow_linked ? "\nlinked to a glow" : "");
        if (silent)
            ImGui::PopStyleColor();
        layer_row(actor_item_id(i)); /* layer badge, revert / promote menu */
        if (open) {
            if (a.fixture) {
                ImGui::TreeNodeEx("##fixture", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                    "Fixture at %.0f %.0f %.0f%s", a.fixture_at[0], a.fixture_at[1], a.fixture_at[2],
                    a.fixture_shadows ? "" : " (casts no shadow)");
                if (ImGui::IsItemClicked())
                    select_actor(i);
            }
            for (size_t k = 0; k < ls.size(); k++) {
                ImGui::PushID((int)k);
                ImGui::ColorButton("##c", ImVec4(fminf(ls[k].color[0], 1), fminf(ls[k].color[1], 1), fminf(ls[k].color[2], 1), 1),
                    ImGuiColorEditFlags_NoTooltip, ImVec2(em * 0.8f, em * 0.8f));
                ImGui::SameLine();
                ImGui::TreeNodeEx("##light", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
                    "%s (%s)", ls[k].name.c_str(), light_type_name(ls[k].type));
                if (ImGui::IsItemClicked())
                    select_actor(i);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

/* ------------------------------------------------------------ Details */

namespace {
/* One exposed parameter: its widget, highlighted when overridden, with a
 * reset arrow; right-click for options. Returns true when it changed. */
bool param_row(const Prefab& p, Actor& a, const prefab::Param& q)
{
    const Value* override_value = a.override_of(q.key);
    bool from_type = false;
    Value base = prefab::inherited_param(p, a, q.key, &from_type); /* its fixture type's, else the prefab's */
    Value v = override_value ? *override_value : base;
    bool overridden = override_value != nullptr, changed = false;
    ImGui::PushID(q.key.c_str());
    if (overridden)
        ImGui::PushStyleColor(ImGuiCol_Text, overridden_colour);
    float width = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 1.6f;
    ImGui::SetNextItemWidth(width * 0.55f);
    std::string format = "%.2f";
    if (q.type == prefab::PARAM_FLOAT) {
        float span = q.max - q.min;
        format = span >= 200 ? "%.0f" : span >= 20 ? "%.1f" : "%.2f";
        if (!q.unit.empty())
            format += " " + q.unit;
        float x = v.x();
        if (ImGui::SliderFloat(q.label.c_str(), &x, q.min, q.max, format.c_str(),
                q.logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None)) {
            v = Value::scalar(x);
            changed = true;
        }
    } else if (q.type == prefab::PARAM_COLOR) {
        float c[3] = { v.at(0), v.at(1), v.at(2) };
        if (ImGui::ColorEdit3(q.label.c_str(), c)) {
            v = Value::vec(c[0], c[1], c[2]);
            changed = true;
        }
    } else if (q.type == prefab::PARAM_BOOL) {
        bool b = v.x() > 0.5f;
        if (ImGui::Checkbox(q.label.c_str(), &b)) {
            v = Value::scalar(b ? 1.0f : 0.0f);
            changed = true;
        }
    } else {
        int i = (int)lroundf(v.x());
        std::string items;
        for (const std::string& o : q.options)
            items += o + '\0';
        items += '\0';
        if (ImGui::Combo(q.label.c_str(), &i, items.c_str())) {
            v = Value::scalar((float)i);
            changed = true;
        }
    }
    if (overridden)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s%s%s: %s%s", q.tooltip.c_str(), q.tooltip.empty() ? "" : "\n",
            from_type ? ("Fixture type " + a.fixture_type).c_str() : "Prefab default", format_param(q, base).c_str(),
            overridden ? " (overridden here)" : "\nChange it to override it for this actor");
    if (changed) {
        a.set_override(q.key, v);
        dirty = true;
    }
    if (ImGui::BeginPopupContextItem("param")) {
        if (ImGui::MenuItem(from_type ? "Reset to the fixture type's value" : "Reset to the prefab default", nullptr, false,
                overridden)) {
            a.clear_override(q.key);
            dirty = true;
        }
        ImGui::EndPopup();
    }
    if (a.override_of(q.key)) {
        ImGui::SameLine();
        if (ImGui::ArrowButton("##reset", ImGuiDir_Left)) { /* UE's yellow reset arrow */
            a.clear_override(q.key);
            dirty = true;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Reset to the %s (%s)", from_type ? "fixture type's value" : "prefab default",
                format_param(q, base).c_str());
    }
    ImGui::PopID();
    return changed;
}

std::string editing_prefab; /* the prefab editor's prefab (empty: closed) */
} // namespace

bool rood_lights::actor_details()
{
    if (selected_actor < 0 || selected_actor >= (int)actors.size())
        return false;
    int index = selected_actor;
    Actor& a = actors[index];
    const Prefab* p = prefab::library().find(a.prefab);
    ImGui::PushID(20000 + index);
    ImGui::SeparatorText((actor_label(index) + "  (light actor)").c_str());
    if (!p) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Prefab '%s' not found (pc/assets/prefabs, %s)", a.prefab.c_str(),
            prefab::library().user_dir.c_str());
    } else {
        ImGui::TextDisabled("Prefab %s: %s%s", p->id.c_str(), p->user ? (p->overrides_builtin ? "yours, over the built-in, " : "yours, ") : "built-in, ",
            p->path.c_str());
        if (!p->error.empty())
            ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", p->error.c_str());
    }
    dirty |= ImGui::Checkbox("Enabled", &a.enabled);
    ImGui::SeparatorText("Transform");
    dirty |= ImGui::DragFloat3("Position", a.position, 2.0f, 0, 0, "%.1f");
    dirty |= ImGui::DragFloat3("Rotation", a.rotation, 1.0f, -360, 360, "%.1f deg");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Yaw, pitch, roll (degrees); the rotate tool (E) turns it in the view");
    if (p) {
        /* Parameters by group, in the prefab's order. */
        std::vector<std::string> groups;
        for (const prefab::Param& q : p->params)
            if (std::find(groups.begin(), groups.end(), q.group) == groups.end())
                groups.push_back(q.group);
        for (const std::string& g : groups) {
            ImGui::SeparatorText(g.empty() ? "Parameters" : g.c_str());
            for (const prefab::Param& q : p->params)
                if (q.group == g)
                    param_row(*p, a, q);
        }
        if (!a.overrides.empty()) {
            if (ImGui::SmallButton("Reset all to the prefab")) {
                a.overrides.clear();
                dirty = true;
            }
            ImGui::SameLine();
        }
        ImGui::TextDisabled("Highlighted values override the prefab for this actor.");
    }
    ImGui::SeparatorText("Fixture");
    if (a.fixture) {
        static float tris[512][9];
        int polys = pc_gi_fixture_triangles(a.fixture_at, fmaxf(a.fixture_radius * 1.5f, 24), tris, 512);
        ImGui::Text("Lights the fixture at %.0f %.0f %.0f (%d polygons, outlined in the view)", a.fixture_at[0],
            a.fixture_at[1], a.fixture_at[2], polys);
        if (ImGui::Checkbox("Fixture casts shadow", &a.fixture_shadows))
            dirty = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Off (default): the fixture's own polygons (cage, glass, candle, niche) near the flame\n"
                              "cast no shadow from its light. On: an open cage's bars throw shadows on the wall.");
        if (a.fixture_shadows)
            ImGui::BeginDisabled();
        float radius = prefab::actor_fixture_radius(a);
        if (ImGui::SliderFloat("Fixture radius", &radius, 0, 128, "%.0f u")) {
            a.fixture_radius = radius;
            a.fixture_radius_set = true;
            dirty = true;
        }
        if (a.fixture_shadows)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Shadow rays stop this far short of the light: everything this close to the flame\n"
                              "(the fixture) lets its light through");
        if (ImGui::SmallButton("Detach from fixture")) {
            a.fixture = false;
            dirty = true;
        }
    } else {
        ImGui::TextDisabled("Not on a room fixture (Light fixture tool, or L over a lantern or candle).");
    }
    fixture_type_details(index);
    ImGui::SeparatorText("Glow");
    if (a.glow_linked) {
        uint32_t linked = pc_gi_glow_by_key(a.glow_key.c_str());
        pc_glow_info g;
        if (linked && pc_gi_glow_get(linked, &g))
            ImGui::Text("Linked to %s (glow %u) at %.0f %.0f %.0f", g.name, g.id, g.position[0], g.position[1], g.position[2]);
        else
            ImGui::Text("Linked to the glow at %.0f %.0f %.0f", a.glow[0], a.glow[1], a.glow[2]);
        if (ImGui::SmallButton("Select glow")) {
            if (linked)
                select_glow_id(linked);
            else
                select_glow(a.glow);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Unlink")) {
            a.glow_linked = false;
            a.glow_key.clear();
            dirty = true;
        }
    } else {
        ImGui::TextDisabled("No glow linked.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Link nearest glow")) {
            std::vector<prefab::EvalLight> ls;
            actor_lights(a, ls, false);
            float at[3] = { a.position[0], a.position[1], a.position[2] };
            if (!ls.empty())
                memcpy(at, ls[0].position, sizeof at);
            float pos[32][4], col[32][4];
            int muted[32], best = -1;
            int n = pc_gi_emitters(pos, col, muted, 32);
            float best_d = 256.0f * 256.0f;
            for (int i = 0; i < n; i++) {
                float d[3] = { pos[i][0] - at[0], pos[i][1] - at[1], pos[i][2] - at[2] };
                float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                if (col[i][3] >= 0 && dd < best_d)
                    best_d = dd, best = i;
            }
            if (best >= 0) {
                link_glow(a, pos[best]);
                dirty = true;
            } else {
                snprintf(status, sizeof status, "No glow within 256 units of the actor's light");
            }
        }
    }
    /* Its lights now (read-only: they come from the prefab). */
    std::vector<prefab::EvalLight> ls;
    actor_lights(a, ls);
    if (ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen, "Lights (%zu, from the prefab)", ls.size())) {
        for (const prefab::EvalLight& e : ls) {
            ImGui::ColorButton(e.name.c_str(), ImVec4(fminf(e.color[0], 1), fminf(e.color[1], 1), fminf(e.color[2], 1), 1),
                ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            ImGui::SameLine();
            ImGui::Text("%s: %s, intensity %.2f (x%.2f now), radius %.0f%s", e.name.c_str(), light_type_name(e.type),
                e.intensity, e.factor, e.radius, e.shadows ? ", shadows" : "");
        }
        ImGui::TreePop();
    }
    ImGui::SeparatorText("Actor");
    if (ImGui::Button("Duplicate")) {
        pc_undo_hint(("Duplicate " + actor_label(index)).c_str());
        Actor copy = a;
        copy.position[0] += 64;
        copy.glow_linked = false;
        copy.glow_key.clear();
        copy.seed = prefab::hash32(copy.seed + 1) % 1000000u + 1;
        actors.push_back(copy);
        select_actor((int)actors.size() - 1);
        dirty = true;
        ImGui::PopID();
        return true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Break into lights")) {
        pc_undo_hint(("Break " + actor_label(index) + " into lights").c_str());
        for (const prefab::EvalLight& e : ls) {
            Light l;
            l.type = e.type;
            memcpy(l.position, e.position, sizeof l.position);
            memcpy(l.color, e.color, sizeof l.color);
            l.intensity = e.intensity / (e.factor > 0 ? e.factor : 1);
            l.radius = e.radius;
            memcpy(l.direction, e.direction, sizeof l.direction);
            l.cone = e.cone;
            l.size[0] = e.size[0], l.size[1] = e.size[1];
            l.shadows = e.shadows;
            lights.push_back(l);
        }
        remove_actor(index);
        snprintf(status, sizeof status, "Replaced the actor by %zu plain lights (no animation)", ls.size());
        ImGui::PopID();
        return true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Replace the actor by plain lights with its current values (no parameters or animation)");
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        pc_undo_hint(("Delete " + actor_label(index)).c_str());
        remove_actor(index);
        ImGui::PopID();
        return true;
    }
    if (p) {
        ImGui::SeparatorText("Prefab");
        if (a.overrides.empty())
            ImGui::BeginDisabled();
        if (ImGui::Button("Apply to prefab"))
            apply_actor_to_prefab(index);
        if (a.overrides.empty())
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Make this actor's overridden values the prefab's defaults (saved to %s/%s.json);\n"
                              "every instance without its own override follows",
                prefab::library().user_dir.c_str(), p->id.c_str());
        ImGui::SameLine();
        static char name[64];
        if (ImGui::Button("Save as prefab..."))
            ImGui::OpenPopup("save as prefab");
        if (ImGui::BeginPopup("save as prefab")) {
            if (ImGui::IsWindowAppearing()) {
                snprintf(name, sizeof name, "%s variant", p->name.c_str());
                ImGui::SetKeyboardFocusHere();
            }
            bool enter = ImGui::InputText("Name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((ImGui::Button("Save") || enter) && name[0]) {
                save_actor_as_prefab(index, name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A new prefab with this actor's values as its defaults (in %s)", prefab::library().user_dir.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Edit prefab"))
            editing_prefab = p->id;
    }
    ImGui::PopID();
    return true;
}

/* ------------------------------------------------------- prefab editor */

namespace {
std::string expression_text(const Json* j)
{
    if (!j || j->type == Json::Null)
        return "";
    if (j->type == Json::String)
        return j->string;
    if (j->type == Json::Number || j->type == Json::Bool)
        return prefab::json_write(*j);
    if (j->type == Json::Array) {
        std::string out = "vec(";
        for (size_t i = 0; i < j->items.size(); i++)
            out += (i ? ", " : "") + expression_text(&j->items[i]);
        if (j->items.size() == 2)
            out += ", 0";
        return out + ")";
    }
    return "";
}
Json expression_json(const std::string& text)
{
    char* end;
    double v = strtod(text.c_str(), &end);
    if (!text.empty() && end != text.c_str() && !*end)
        return Json::num(v);
    return text.empty() ? Json() : Json::str(text);
}
bool edit_string(const char* label, Json& obj, const char* key, bool multiline = false)
{
    const Json* j = obj.get(key);
    char text[512];
    snprintf(text, sizeof text, "%s", j && j->type == Json::String ? j->string.c_str() : "");
    bool changed = multiline ? ImGui::InputTextMultiline(label, text, sizeof text, ImVec2(-1, ImGui::GetFontSize() * 3))
                             : ImGui::InputText(label, text, sizeof text);
    if (changed)
        obj.set(key, Json::str(text));
    return changed;
}
bool edit_number(const char* label, Json& obj, const char* key, float speed)
{
    const Json* j = obj.get(key);
    float v = j && j->type == Json::Number ? (float)j->number : 0;
    if (!ImGui::DragFloat(label, &v, speed))
        return false;
    obj.set(key, Json::num(roundf(v * 10000) / 10000));
    return true;
}
} // namespace

extern "C" void pc_lights_edit_prefab(const char* id) { editing_prefab = id ? id : ""; }

void rood_lights::prefab_editor_window()
{
    if (editing_prefab.empty())
        return;
    prefab::Library& lib = prefab::library();
    Prefab* p = nullptr;
    for (Prefab& q : lib.prefabs)
        if (q.id == editing_prefab)
            p = &q;
    if (!p) {
        editing_prefab.clear();
        return;
    }
    static std::string modified_id;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.2f, io.DisplaySize.y * 0.15f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 40, ImGui::GetFontSize() * 34), ImGuiCond_FirstUseEver);
    bool open = true;
    char title[160];
    snprintf(title, sizeof title, "Prefab: %s%s###prefab editor", p->name.c_str(), modified_id == p->id ? " (unsaved)" : "");
    if (!ImGui::Begin(title, &open)) {
        ImGui::End();
        if (!open)
            editing_prefab.clear();
        return;
    }
    Json& j = p->source;
    bool changed = false;
    ImGui::TextDisabled("%s  (%s)", p->path.c_str(), p->user ? "yours" : "built-in: Save writes your copy");
    if (!p->error.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", p->error.c_str());
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 9);
    changed |= edit_string("Name", j, "name");
    changed |= edit_string("Category", j, "category");
    changed |= edit_string("Icon", j, "icon");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("candle, candelabra, lantern, torch, brazier, fireplace, window, magic, light:point, light:spot, light:rect");
    changed |= edit_string("Description", j, "description", true);
    Json placement = j.get("placement") ? *j.get("placement") : Json::object();
    int align = placement.get("align") && placement.get("align")->string == "normal" ? 1 : 0;
    if (ImGui::Combo("Placement", &align, "Upright\0Facing the surface normal\0")) {
        placement.set("align", Json::str(align ? "normal" : "up"));
        changed = true;
    }
    changed |= edit_number("Surface offset", placement, "surface_offset", 0.5f);
    if (changed)
        j.set("placement", placement);
    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        Json* params = j.get("params");
        if (!params)
            params = &j.set("params", Json::array());
        int remove = -1;
        for (size_t i = 0; i < params->items.size(); i++) {
            Json& q = params->items[i];
            ImGui::PushID((int)i);
            const Json* key = q.get("key");
            const prefab::Param* param = key ? p->param(key->string) : nullptr;
            if (ImGui::TreeNode("##param", "%s  (%s)", key ? key->string.c_str() : "?",
                    param ? format_param(*param, param->def).c_str() : "")) {
                changed |= edit_string("Key", q, "key");
                changed |= edit_string("Label", q, "label");
                changed |= edit_string("Group", q, "group");
                const char* types[] = { "float", "color", "bool", "enum" };
                const Json* t = q.get("type");
                int type = 0;
                for (int k = 0; k < 4; k++)
                    if (t && t->string == types[k])
                        type = k;
                if (ImGui::Combo("Type", &type, types, 4)) {
                    q.set("type", Json::str(types[type]));
                    changed = true;
                }
                if (type == 0) {
                    changed |= edit_number("Default", q, "default", 0.01f);
                    changed |= edit_number("Min", q, "min", 0.01f);
                    changed |= edit_number("Max", q, "max", 0.01f);
                    changed |= edit_string("Unit", q, "unit");
                    bool log = q.get("log") && q.get("log")->boolean;
                    if (ImGui::Checkbox("Logarithmic slider", &log)) {
                        q.set("log", Json::boolean_(log));
                        changed = true;
                    }
                } else if (type == 1 && param) {
                    float c[3] = { param->def.at(0), param->def.at(1), param->def.at(2) };
                    if (ImGui::ColorEdit3("Default", c)) {
                        Json a = Json::array();
                        for (float x : c)
                            a.items.push_back(Json::num(roundf(x * 1000) / 1000));
                        q.set("default", a);
                        changed = true;
                    }
                } else if (type == 2) {
                    bool b = param && param->def.x() > 0.5f;
                    if (ImGui::Checkbox("Default on", &b)) {
                        q.set("default", Json::boolean_(b));
                        changed = true;
                    }
                }
                changed |= edit_string("Tooltip", q, "tooltip");
                if (ImGui::SmallButton("Remove parameter"))
                    remove = (int)i;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove >= 0) {
            params->items.erase(params->items.begin() + remove);
            changed = true;
        }
        if (ImGui::SmallButton("+ Parameter")) {
            Json q = Json::object();
            q.set("key", Json::str("param" + std::to_string(params->items.size() + 1)));
            q.set("label", Json::str("New parameter"));
            q.set("type", Json::str("float"));
            q.set("default", Json::num(1));
            q.set("min", Json::num(0));
            q.set("max", Json::num(4));
            params->items.push_back(q);
            changed = true;
        }
    }
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Fields are numbers or expressions of the parameters: + - * / ( ), vec(x, y, z),\n"
                            "kelvin(K), rgb(r, g, b), min, max, clamp, mix, select(c, a, b), pow, sqrt, abs.\n"
                            "Offsets: actor frame, x right, y DOWN (above is negative y), z forward.");
        Json* lights_json = j.get("lights");
        if (!lights_json)
            lights_json = &j.set("lights", Json::array());
        int remove = -1;
        static const char* fields[] = { "offset", "direction", "color", "intensity", "radius", "cone", "size", "shadows",
            "light_size", "flicker", "flicker_speed", "jitter", "pulse", "pulse_speed", "enabled" };
        for (size_t i = 0; i < lights_json->items.size(); i++) {
            Json& l = lights_json->items[i];
            ImGui::PushID(100 + (int)i);
            const Json* n = l.get("name");
            if (ImGui::TreeNodeEx("##light", ImGuiTreeNodeFlags_DefaultOpen, "%s", n ? n->string.c_str() : "Light")) {
                changed |= edit_string("Name", l, "name");
                const char* types[] = { "point", "spot", "rect" };
                const Json* t = l.get("type");
                int type = 0;
                for (int k = 0; k < 3; k++)
                    if (t && t->string == types[k])
                        type = k;
                if (ImGui::Combo("Type", &type, types, 3)) {
                    l.set("type", Json::str(types[type]));
                    changed = true;
                }
                for (const char* f : fields) {
                    char text[256];
                    snprintf(text, sizeof text, "%s", expression_text(l.get(f)).c_str());
                    if (ImGui::InputTextWithHint(f, "(default)", text, sizeof text)) {
                        Json value = expression_json(text);
                        if (value.type == Json::Null)
                            l.remove(f);
                        else
                            l.set(f, value);
                        changed = true;
                    }
                }
                changed |= edit_number("Phase", l, "phase", 0.01f);
                if (ImGui::SmallButton("Remove light"))
                    remove = (int)i;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove >= 0) {
            lights_json->items.erase(lights_json->items.begin() + remove);
            changed = true;
        }
        if (ImGui::SmallButton("+ Light")) {
            Json l = Json::object();
            l.set("name", Json::str("Light " + std::to_string(lights_json->items.size() + 1)));
            l.set("type", Json::str("point"));
            l.set("intensity", Json::num(1));
            l.set("radius", Json::num(400));
            lights_json->items.push_back(l);
            changed = true;
        }
    }
    ImGui::PopItemWidth();
    if (changed) {
        /* Live: every instance follows the edited prefab at once. */
        Prefab rebuilt = prefab::prefab_from_json(p->id, j);
        rebuilt.path = p->path;
        rebuilt.user = p->user;
        rebuilt.overrides_builtin = p->overrides_builtin;
        *p = rebuilt;
        modified_id = p->id;
        ++lib.serial;
    }
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        std::string error, id = editing_prefab;
        Json copy = prefab::library().find(id)->source;
        if (lib.save_user(id, copy, &error)) {
            modified_id.clear();
            snprintf(status, sizeof status, "Saved prefab %s to %s/%s.json", id.c_str(), lib.user_dir.c_str(), id.c_str());
        } else
            snprintf(status, sizeof status, "%s", error.c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Write %s/%s.json (yours; built-ins in pc/assets/prefabs stay as they are)", lib.user_dir.c_str(),
            editing_prefab.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        lib.load();
        modified_id.clear();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Reload every prefab from its file (drops unsaved edits)");
    const Prefab* now = lib.find(editing_prefab);
    if (now && now->overrides_builtin) {
        ImGui::SameLine();
        if (ImGui::Button("Back to the built-in")) {
            std::string error;
            if (!lib.revert_user(editing_prefab, &error))
                snprintf(status, sizeof status, "%s", error.c_str());
            else
                snprintf(status, sizeof status, "Your %s.json moved aside (.bak); the built-in is back", editing_prefab.c_str());
            modified_id.clear();
        }
    }
    ImGui::End();
    if (!open)
        editing_prefab.clear();
}

/* ------------------------------------------------------- Place Actors */

namespace {
struct Entry {
    std::string item, name, category, description;
};
std::vector<Entry> entries()
{
    std::vector<Entry> out = {
        { "light:point", "Point light", "Lights", "Light in every direction from a point." },
        { "light:spot", "Spot light", "Lights", "A cone of light (it aims away from the surface it is dropped on)." },
        { "light:rect", "Rect light", "Lights", "An area light from a rectangle's front face (it faces away from the surface)." },
    };
    for (const Prefab& p : prefab::library().prefabs)
        out.push_back({ "prefab:" + p.id, p.name, p.category, p.description + (p.error.empty() ? "" : "\nProblem: " + p.error) });
    return out;
}
} // namespace

void rood_lights::place_panel()
{
    static char search[64];
    static std::string category = "All";
    float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##placesearch", "Search lights and actors", search, sizeof search);
    std::vector<Entry> all = entries();
    std::vector<std::string> categories = { "All", "Lights" };
    for (const Entry& e : all)
        if (std::find(categories.begin(), categories.end(), e.category) == categories.end())
            categories.push_back(e.category);
    /* Category chips (wrapping). */
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    for (size_t i = 0; i < categories.size(); i++) {
        const std::string& c = categories[i];
        float w = ImGui::CalcTextSize(c.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < right)
            ImGui::SameLine();
        bool on = category == c;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(c.c_str()))
            category = c;
        if (on)
            ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::BeginChild("##placelist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.2f));
    float row = em * 2.4f;
    std::string last_category;
    for (const Entry& e : all) {
        if (category != "All" && e.category != category)
            continue;
        if (search[0] && !strcasestr(e.name.c_str(), search) && !strcasestr(e.category.c_str(), search)
            && !strcasestr(e.description.c_str(), search))
            continue;
        if (category == "All" && e.category != last_category) {
            ImGui::TextDisabled("%s", e.category.c_str());
            last_category = e.category;
        }
        ImGui::PushID(e.item.c_str());
        ImVec2 at = ImGui::GetCursorScreenPos();
        bool is_current = place_item == e.item;
        if (ImGui::Selectable("##row", is_current, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, row))) {
            place_item = e.item;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                place_at_centre(e.item);
        }
        /* Drag it out into the view (UE's Place Actors). */
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, em * 0.4f) && !place_drag_active())
            place_drag_begin(e.item, false, ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !place_drag_active())
            ImGui::SetTooltip("%s\n\nDrag into the view to place (snapping as set in the toolbar),\n"
                              "double-click to place at the view's centre; Ctrl+click in the view adds it.",
                e.description.c_str());
        ImDrawList* d = ImGui::GetWindowDrawList();
        float colour[3];
        item_colour(e.item, colour);
        ImVec2 box(at.x + row * 0.1f, at.y + row * 0.1f), box_hi(at.x + row * 0.9f, at.y + row * 0.9f);
        d->AddRectFilled(box, box_hi, IM_COL32(28, 28, 34, 255), 4);
        d->AddRectFilled(ImVec2(box.x, box_hi.y - row * 0.1f), box_hi, rgba(colour, 1), 2); /* swatch */
        draw_icon(d, at.x + row * 0.5f, at.y + row * 0.45f, row * 0.62f, e.item, colour, 1);
        d->AddText(ImVec2(at.x + row + em * 0.3f, at.y + row * 0.5f - em * 0.55f), ImGui::GetColorU32(ImGuiCol_Text),
            e.name.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Ctrl+click adds: %s", item_label(place_item).c_str());
    if (place_item.rfind("prefab:", 0) == 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit prefab"))
            editing_prefab = place_item.substr(7);
    }
}

/* The toolbar's quick-add menu: the same items; a click places at the
 * view's centre. */
extern "C" void pc_lights_place_menu(void)
{
    std::string last;
    for (const Entry& e : entries()) {
        if (e.category != last) {
            ImGui::SeparatorText(e.category.c_str());
            last = e.category;
        }
        float em = ImGui::GetFontSize(), colour[3];
        item_colour(e.item, colour);
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(em * 1.2f, em));
        draw_icon(ImGui::GetWindowDrawList(), at.x + em * 0.6f, at.y + em * 0.5f, em * 1.1f, e.item, colour, 1);
        ImGui::SameLine();
        if (ImGui::MenuItem(e.name.c_str())) {
            place_item = e.item;
            place_at_centre(e.item);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s\nPlaced at the view's centre (drag from Place Actors to put it anywhere).", e.description.c_str());
    }
}
extern "C" void pc_lights_place_panel(void) { place_panel(); }
extern "C" void pc_lights_editor_windows(void)
{
    prefab_editor_window();
    suggestions_window();
}

/* ------------------------------------------------------------- RPC */

namespace {
bool vec3_of(const Json* j, float out[3])
{
    if (!j || j->type != Json::Array || j->items.size() != 3)
        return false;
    for (int k = 0; k < 3; k++) {
        if (j->items[k].type != Json::Number)
            return false;
        out[k] = (float)j->items[k].number;
    }
    return true;
}
std::string num(double v)
{
    char text[32];
    snprintf(text, sizeof text, "%.6g", v);
    return text;
}
std::string vec_json(const float v[3])
{
    return "[" + num(v[0]) + "," + num(v[1]) + "," + num(v[2]) + "]";
}
std::string value_json(const Value& v)
{
    return v.n == 3 ? vec_json(v.v) : num(v.v[0]);
}
std::string actor_json(int i)
{
    const Actor& a = actors[i];
    const Prefab* p = prefab::library().find(a.prefab);
    std::string out = "{\"index\":" + std::to_string(i) + ",\"id\":" + prefab::json_quote(actor_item_id(i))
        + ",\"prefab\":" + prefab::json_quote(a.prefab)
        + ",\"name\":" + prefab::json_quote(actor_label(i)) + ",\"missing\":" + (p ? "false" : "true")
        + ",\"position\":" + vec_json(a.position) + ",\"rotation\":" + vec_json(a.rotation)
        + ",\"enabled\":" + (a.enabled ? "true" : "false") + ",\"seed\":" + std::to_string(a.seed)
        + ",\"glow\":" + (a.glow_linked ? vec_json(a.glow) : "null")
        + ",\"glow_key\":" + (a.glow_linked && !a.glow_key.empty() ? prefab::json_quote(a.glow_key) : "null")
        + ",\"fixture\":" + (a.fixture ? vec_json(a.fixture_at) : "null")
        + ",\"fixture_radius\":" + num(prefab::actor_fixture_radius(a))
        + ",\"fixture_shadows\":" + (a.fixture_shadows ? "true" : "false")
        + ",\"fixture_type\":" + (a.fixture_type.empty() ? "null" : prefab::json_quote(a.fixture_type))
        + ",\"flame\":" + std::to_string(a.flame) + ",\"overrides\":{";
    for (size_t k = 0; k < a.overrides.size(); k++)
        out += (k ? "," : "") + prefab::json_quote(a.overrides[k].first) + ":" + value_json(a.overrides[k].second);
    out += "},\"params\":{";
    if (p)
        for (size_t k = 0; k < p->params.size(); k++)
            out += (k ? "," : "") + prefab::json_quote(p->params[k].key) + ":" + value_json(prefab::actor_param(*p, a, p->params[k].key));
    out += "},\"lights\":[";
    std::vector<prefab::EvalLight> ls;
    actor_lights(a, ls);
    for (size_t k = 0; k < ls.size(); k++) {
        const prefab::EvalLight& e = ls[k];
        out += std::string(k ? "," : "") + "{\"name\":" + prefab::json_quote(e.name) + ",\"type\":\""
            + (e.type == 1 ? "spot" : e.type == 2 ? "rect" : "point") + "\",\"position\":" + vec_json(e.position)
            + ",\"color\":" + vec_json(e.color) + ",\"direction\":" + vec_json(e.direction) + ",\"intensity\":" + num(e.intensity)
            + ",\"factor\":" + num(e.factor) + ",\"radius\":" + num(e.radius) + ",\"light_size\":" + num(e.light_size)
            + ",\"shadow_skip\":" + num(e.shadow_skip)
            + ",\"shadows\":" + (e.shadows ? "true" : "false") + ",\"enabled\":" + (e.enabled ? "true" : "false") + "}";
    }
    return out + "]}";
}
/* {key: number | [r, g, b] | bool} onto the actor's overrides. */
std::string apply_params(Actor& a, const Json* params)
{
    if (!params)
        return "";
    if (params->type != Json::Object)
        return "params must be an object";
    const Prefab* p = prefab::library().find(a.prefab);
    for (const auto& m : params->members) {
        const prefab::Param* q = p ? p->param(m.first) : nullptr;
        if (!q)
            return "unknown parameter " + m.first;
        float v[3];
        if (m.second.type == Json::Null)
            a.clear_override(m.first);
        else if (vec3_of(&m.second, v))
            a.set_override(m.first, Value::vec(v[0], v[1], v[2]));
        else if (m.second.type == Json::Number)
            a.set_override(m.first, q->type == prefab::PARAM_COLOR ? Value::vec((float)m.second.number, (float)m.second.number, (float)m.second.number)
                                                                  : Value::scalar((float)m.second.number));
        else if (m.second.type == Json::Bool)
            a.set_override(m.first, Value::scalar(m.second.boolean ? 1.0f : 0.0f));
        else if (m.second.type == Json::String && q->type == prefab::PARAM_ENUM) {
            auto it = std::find(q->options.begin(), q->options.end(), m.second.string);
            if (it == q->options.end())
                return "unknown option " + m.second.string + " for " + m.first;
            a.set_override(m.first, Value::scalar((float)(it - q->options.begin())));
        } else
            return "bad value for " + m.first;
    }
    return "";
}
unsigned modes_of(const Json* list)
{
    if (!list || list->type != Json::Array)
        return 0;
    unsigned modes = 0;
    for (const Json& m : list->items) {
        modes |= m.string == "point" ? PC_SNAP_MODE_POINT : m.string == "edge" ? PC_SNAP_MODE_EDGE
            : m.string == "midpoint"                                         ? PC_SNAP_MODE_MIDPOINT
            : m.string == "surface"                                          ? PC_SNAP_MODE_SURFACE
                                                                             : 0;
    }
    return modes ? modes : PC_SNAP_MODE_SURFACE;
}
/* The window pixel from x, y or u, v (of the view), default its centre. */
bool cursor_of(const Json& params, float* x, float* y)
{
    float rect[4];
    if (!view_rect(rect))
        return false;
    *x = rect[0] + rect[2] * 0.5f, *y = rect[1] + rect[3] * 0.5f;
    if (const Json* u = params.get("u"))
        *x = rect[0] + rect[2] * (float)u->number;
    if (const Json* v = params.get("v"))
        *y = rect[1] + rect[3] * (float)v->number;
    if (const Json* j = params.get("x"))
        *x = (float)j->number;
    if (const Json* j = params.get("y"))
        *y = (float)j->number;
    return true;
}
std::string placement_json(const Placement& p)
{
    static const char* snaps[] = { "none", "point", "midpoint", "edge", "surface" };
    return std::string("{\"ok\":") + (p.ok ? "true" : "false") + ",\"snap\":\"" + snaps[p.snap >= 0 && p.snap <= 4 ? p.snap : 0]
        + "\",\"position\":" + vec_json(p.position) + ",\"rotation\":" + vec_json(p.rotation) + ",\"surface\":"
        + vec_json(p.surface) + ",\"normal\":" + vec_json(p.normal) + "}";
}
std::string suggestion_json(const std::vector<Suggestion>& list)
{
    std::string out = "[";
    for (size_t i = 0; i < list.size(); i++)
        out += std::string(i ? "," : "") + "{\"glow\":" + vec_json(list[i].glow) + ",\"color\":" + vec_json(list[i].color)
            + ",\"size\":" + num(list[i].size) + ",\"prefab\":" + prefab::json_quote(list[i].prefab)
            + ",\"flame\":" + (list[i].flame ? "true" : "false") + ",\"weight\":" + num(list[i].weight)
            + ",\"accept\":" + (list[i].accept ? "true" : "false") + "}";
    return out + "]";
}
/* The room's glows (glows.c): id, key, name, smoothed and raw values. */
std::string glow_json(const pc_glow_info& g)
{
    static const char* origins[] = { "?", "room", "object", "particles", "packet", "baked" };
    return "{\"id\":" + std::to_string(g.id) + ",\"key\":" + prefab::json_quote(g.key) + ",\"name\":"
        + prefab::json_quote(g.name) + ",\"origin\":\"" + origins[g.origin >= 1 && g.origin <= 5 ? g.origin : 0]
        + "\",\"position\":" + vec_json(g.position) + ",\"color\":" + vec_json(g.color) + ",\"size\":"
        + num(g.color[3]) + ",\"reach\":" + num(g.position[3]) + ",\"raw_position\":" + vec_json(g.raw_position)
        + ",\"raw_color\":" + vec_json(g.raw_color) + ",\"presence\":" + num(g.presence) + ",\"observed\":"
        + (g.observed ? "true" : "false") + ",\"triangles\":" + std::to_string(g.triangles) + ",\"muted\":"
        + std::to_string(g.muted) + ",\"overridden\":" + (g.overridden ? "true" : "false") + ",\"traced\":"
        + (g.traced ? "true" : "false") + ",\"category\":\""
        + (g.category == PC_GLOW_FIXTURE ? "fixture" : g.category == PC_GLOW_MAGIC ? "magic" : "effect")
        + "\",\"casts_shadows\":" + (g.casts_shadows ? "true" : "false")
        + ",\"fixture_type\":" + (g.fixture_type[0] ? prefab::json_quote(g.fixture_type) : "null")
        + ",\"shape\":{\"kind\":\"" + (g.shape[3] > 0 ? "column" : g.shape[3] < 0 ? "solid" : "sphere")
        + "\",\"centre\":" + vec_json(g.shape_centre) + ",\"half_axis\":" + vec_json(g.shape)
        + ",\"radius\":" + num(g.shape[3] != 0 ? fabsf(g.shape[3]) : g.color[3]) + "}}";
}
std::string glows_json()
{
    static pc_glow_info glows[160];
    int n = pc_gi_glow_list(glows, 160);
    std::string out = "[";
    for (int i = 0; i < n; i++)
        out += std::string(i ? "," : "") + glow_json(glows[i]);
    return out + "]";
}
int copy_out(const std::string& text, char* out, int size)
{
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
int failure(const std::string& message, char* out, int size)
{
    snprintf(out, size, "%s", message.c_str());
    return -1;
}

/* RPC glows: list (default), select {id}, set {id, enabled, intensity, tint,
 * size, reach}, reset {id}, solo {id}, smoothing (seconds), fragments
 * (this update's glowing triangles and their glows). */
int glows_method(const Json& params, char* out, int size)
{
    track_room();
    auto id_of = [&](uint32_t* id) {
        const Json* j = params.get("id");
        if (!j || j->type != Json::Number)
            return false;
        *id = (uint32_t)j->number;
        pc_glow_info g;
        return pc_gi_glow_get(*id, &g) != 0;
    };
    std::string action = params.get("action") && params.get("action")->type == Json::String ? params.get("action")->string : "list";
    uint32_t id = 0;
    if (const Json* s = params.get("shapes")) /* enclosing glows: 0 spheres, 1 columns, 2 solid columns */
        pc_glow_shapes = s->number < 0 ? 0 : s->number > 2 ? 2 : (int)s->number;
    if (const Json* s = params.get("smoothing")) /* every category's */
        for (int c = 0; c < 3; c++)
            pc_glow_category[c][4] = (float)s->number;
    auto persistent = [&]() {
        pc_glow_info g;
        return pc_gi_glow_get(id, &g) && g.category != PC_GLOW_EFFECT;
    };
    if (action == "select") {
        if (!id_of(&id))
            return failure("select needs the id of a glow in this room", out, size);
        if (!persistent())
            return failure("effect glows are not selectable one by one (their kind's settings control them)", out, size);
        select_glow_id(id);
    } else if (action == "set") {
        if (!id_of(&id))
            return failure("set needs the id of a glow in this room", out, size);
        if (!persistent())
            return failure("effect glows have no overrides of their own (their kind's settings control them)", out, size);
        pc_glow_override* o = pc_gi_glow_override_id(id, 1);
        if (!o)
            return failure("too many glow overrides", out, size);
        if (const Json* e = params.get("enabled"))
            o->muted = !(e->type == Json::Bool ? e->boolean : e->number != 0);
        if (const Json* v = params.get("intensity"))
            o->intensity = (float)v->number;
        float tint[3];
        if (vec3_of(params.get("tint"), tint))
            memcpy(o->tint, tint, sizeof tint);
        if (const Json* v = params.get("size"))
            o->size = (float)v->number;
        if (const Json* v = params.get("reach"))
            o->reach = (float)v->number;
        if (const Json* v = params.get("shape"); v && v->type == Json::String)
            o->shape = v->string == "sphere" ? PC_GLOW_SHAPE_SPHERE
                : v->string == "column"      ? PC_GLOW_SHAPE_COLUMN
                                             : PC_GLOW_SHAPE_AUTO;
        dirty = true;
    } else if (action == "reset") {
        if (!id_of(&id))
            return failure("reset needs the id of a glow in this room", out, size);
        pc_gi_glow_remove_id(id);
        dirty = true;
    } else if (action == "solo") {
        if (!id_of(&id))
            return failure("solo needs the id of a glow in this room", out, size);
        pc_gi_glow_solo_id(id);
    } else if (action != "list")
        return failure("unknown action " + action, out, size);
    static const char* kinds[3] = { "fixtures", "magic", "effects" };
    std::string categories = "{";
    for (int c = 0; c < 3; c++)
        categories += std::string(c ? "," : "") + "\"" + kinds[c] + "\":{\"traced\":"
            + std::to_string(pc_glow_category_count[c]) + ",\"energy\":" + num(pc_glow_category_energy[c])
            + ",\"enabled\":" + num(pc_glow_category[c][0]) + ",\"intensity\":" + num(pc_glow_category[c][1])
            + ",\"reach\":" + num(pc_glow_category[c][2]) + ",\"shadows\":" + num(c == 2 ? 0 : pc_glow_category[c][3])
            + ",\"smoothing\":" + num(pc_glow_category[c][4]) + "}";
    categories += "}";
    std::string text = "{\"selected\":" + (glow_selected && glow_id ? std::to_string(glow_id) : std::string("null"))
        + ",\"energy\":" + num(pc_glow_energy) + ",\"raw_energy\":" + num(pc_glow_raw_energy) + ",\"casts\":"
        + std::to_string(pc_glow_casts) + ",\"categories\":" + categories + ",\"glows\":" + glows_json();
    if (const Json* f = params.get("fragments"); f && (f->type == Json::Bool ? f->boolean : f->number != 0)) {
        static char fragments[1 << 20];
        int room_left = size - (int)text.size() - 32; /* what the reply can hold */
        pc_glows_fragments_json(fragments, room_left < (int)sizeof fragments ? (room_left > 256 ? room_left : 256) : (int)sizeof fragments);
        text += std::string(",\"fragments\":") + fragments;
    }
    text += "}";
    return copy_out(text, out, size);
}
int actors_method(const Json& params, char* out, int size)
{
    track_room();
    std::string action = params.get("action") && params.get("action")->type == Json::String ? params.get("action")->string : "list";
    if (const Json* t = params.get("time"))
        pc_lights_set_time(t->type == Json::Number ? t->number : -1);
    if (const Json* a = params.get("animate"))
        animate = a->type == Json::Bool ? a->boolean : a->number != 0;
    if (const Json* b = params.get("budget")) /* the Vulkan paths' light cap (0: every light) */
        pc_lights_budget = (int)b->number;
    std::string extra;
    auto index_of = [&](int* index) {
        const Json* j = params.get("index");
        *index = j && j->type == Json::Number ? (int)j->number : selected_actor;
        return *index >= 0 && *index < (int)actors.size();
    };
    int index = -1;
    if (action == "list") {
    } else if (action == "add") {
        const Json* id = params.get("prefab");
        if (!id || id->type != Json::String || !prefab::library().find(id->string))
            return failure("add needs a known prefab (prefabs lists them)", out, size);
        float position[3];
        Placement where;
        if (vec3_of(params.get("position"), position)) {
            where.ok = true;
            memcpy(where.position, position, sizeof position);
            index = place_commit("prefab:" + id->string, where);
        } else {
            float x, y;
            if (!cursor_of(params, &x, &y) || !place_compute("prefab:" + id->string, x, y, modes_of(params.get("modes")), where))
                return failure("nothing to place on under the cursor (no editor view or room)", out, size);
            index = place_commit("prefab:" + id->string, where);
            extra += ",\"placement\":" + placement_json(where);
        }
        if (index < 0)
            return failure("place failed", out, size);
        float rotation[3];
        if (vec3_of(params.get("rotation"), rotation))
            memcpy(actors[index].rotation, rotation, sizeof rotation);
        float glow[3];
        if (vec3_of(params.get("glow"), glow))
            link_glow(actors[index], glow);
        if (const Json* seed = params.get("seed"))
            actors[index].seed = (uint32_t)seed->number;
        std::string error = apply_params(actors[index], params.get("params"));
        if (!error.empty())
            return failure(error, out, size);
        extra += ",\"index\":" + std::to_string(index);
    } else if (action == "set") {
        if (!index_of(&index))
            return failure("set needs an actor index", out, size);
        Actor& a = actors[index];
        float v[3];
        if (vec3_of(params.get("position"), v))
            memcpy(a.position, v, sizeof v);
        if (vec3_of(params.get("rotation"), v))
            memcpy(a.rotation, v, sizeof v);
        if (const Json* e = params.get("enabled"))
            a.enabled = e->type == Json::Bool ? e->boolean : e->number != 0;
        if (const Json* g = params.get("glow")) {
            float p[3];
            if (vec3_of(g, p))
                link_glow(a, p);
            else
                a.glow_linked = false, a.glow_key.clear();
        }
        if (const Json* f = params.get("fixture"))
            a.fixture = vec3_of(f, a.fixture_at);
        if (const Json* r = params.get("fixture_radius"))
            a.fixture_radius = (float)r->number;
        if (const Json* s = params.get("fixture_shadows"))
            a.fixture_shadows = s->type == Json::Bool ? s->boolean : s->number != 0;
        if (const Json* id = params.get("prefab"))
            a.prefab = id->string;
        if (const Json* reset = params.get("reset")) {
            if (reset->type == Json::Bool && reset->boolean)
                a.overrides.clear();
            for (const Json& k : reset->items)
                a.clear_override(k.string);
        }
        std::string error = apply_params(a, params.get("params"));
        if (!error.empty())
            return failure(error, out, size);
        dirty = true;
    } else if (action == "remove") {
        if (!index_of(&index))
            return failure("remove needs an actor index", out, size);
        remove_actor(index);
    } else if (action == "select") {
        if (!index_of(&index))
            return failure("select needs an actor index", out, size);
        select_actor(index);
    } else if (action == "sample") {
        if (!index_of(&index))
            return failure("sample needs an actor index", out, size);
        const Json* t = params.get("time");
        static char lights_text[16384];
        if (pc_lights_sample_actor(index, t && t->type == Json::Number ? t->number : light_time(), lights_text, sizeof lights_text) < 0)
            return failure("the actor's prefab is missing", out, size);
        extra += ",\"sample\":" + std::string(lights_text);
    } else if (action == "suggest") {
        std::vector<Suggestion> list = suggest_for_glows();
        extra += ",\"suggestions\":" + suggestion_json(list);
        if (params.get("accept") && (params.get("accept")->boolean || params.get("accept")->type == Json::String)) {
            int added = 0;
            bool all = params.get("accept")->type == Json::String && params.get("accept")->string == "all";
            for (const Suggestion& s : list)
                if (s.accept || all)
                    added += (s.flame ? create_fixture_actor(s.prefab, s.glow, nullptr, nullptr) : attach_to_glow(s.prefab, s.glow)) >= 0;
            extra += ",\"added\":" + std::to_string(added);
        }
    } else if (action == "light_fixture") {
        /* As a click with the Light fixture tool (or L): at position, else
         * the surface under x, y / u, v (default the view's centre). */
        std::string id = params.get("prefab") ? params.get("prefab")->string : "";
        float at[3];
        if (vec3_of(params.get("position"), at)) {
            float up[3] = { 0, -1, 0 };
            index = light_fixture_at(at, up, id);
        } else {
            float x, y;
            if (!cursor_of(params, &x, &y))
                return failure("no editor view", out, size);
            index = light_fixture_at_pixel(x, y, id);
        }
        if (index < 0)
            return failure(status, out, size);
        extra += ",\"index\":" + std::to_string(index);
    } else if (action == "fixture_triangles") {
        if (!index_of(&index) || !actors[index].fixture)
            return failure("fixture_triangles needs an actor lighting a fixture", out, size);
        static float tris[512][9];
        int n = pc_gi_fixture_triangles(actors[index].fixture_at, fmaxf(actors[index].fixture_radius * 1.5f, 24), tris, 512);
        extra += ",\"fixture_triangles\":" + std::to_string(n);
    } else if (action == "attach") {
        float glow[3];
        if (!vec3_of(params.get("glow"), glow))
            return failure("attach needs glow [x, y, z]", out, size);
        std::string id = params.get("prefab") ? params.get("prefab")->string : "candle";
        index = attach_to_glow(id, glow);
        if (index < 0)
            return failure("unknown prefab " + id, out, size);
        select_actor(index);
        extra += ",\"index\":" + std::to_string(index);
    } else if (action == "save") {
        pc_lights_save();
    } else if (action == "save_prefab") {
        const Json* name = params.get("name");
        if (!index_of(&index) || !name || !save_actor_as_prefab(index, name->string))
            return failure(std::string("save_prefab needs index and name: ") + status, out, size);
        extra += ",\"prefab\":" + prefab::json_quote(actors[index].prefab);
    } else if (action == "apply_prefab") {
        if (!index_of(&index) || !apply_actor_to_prefab(index))
            return failure("apply_prefab needs an actor with overrides", out, size);
    } else if (action == "from_lights") {
        const Json* name = params.get("name");
        const Json* list = params.get("lights");
        if (!name || !list || list->items.empty())
            return failure("from_lights needs name and lights [indices]", out, size);
        select_light((int)list->items[0].number);
        for (size_t k = 1; k < list->items.size(); k++)
            multi.push_back((int)list->items[k].number);
        if (!create_prefab_from_selection(name->string))
            return failure(status, out, size);
        extra += ",\"index\":" + std::to_string(selected_actor) + ",\"prefab\":" + prefab::json_quote(actors[selected_actor].prefab);
    } else {
        return failure("unknown action " + action, out, size);
    }
    std::string text = "{\"zone\":" + std::to_string(zone) + ",\"room\":" + std::to_string(room) + ",\"dirty\":"
        + (dirty ? "true" : "false") + ",\"time\":" + num(light_time()) + ",\"animate\":" + (animate ? "true" : "false")
        + ",\"selected\":" + std::to_string(selected_actor) + ",\"light_count\":" + std::to_string(lights.size())
        + ",\"lights_wanted\":" + std::to_string(pc_lights_wanted()) + ",\"dropped\":" + std::to_string(pc_lights_dropped()) + ",\"budget\":" + std::to_string(pc_lights_budget)
        + ",\"actors\":[";
    for (int i = 0; i < (int)actors.size(); i++)
        text += (i ? "," : "") + actor_json(i);
    text += "]" + extra;
    if (params.get("glows") && params.get("glows")->boolean)
        text += ",\"glows\":" + glows_json();
    text += ",\"status\":" + prefab::json_quote(status) + "}";
    return copy_out(text, out, size);
}

int prefabs_method(const Json& params, char* out, int size)
{
    prefab::Library& lib = prefab::library();
    if (params.get("reload") && params.get("reload")->boolean)
        lib.load();
    else
        lib.poll();
    std::string text = "{\"builtin_dir\":" + prefab::json_quote(lib.builtin_dir) + ",\"user_dir\":" + prefab::json_quote(lib.user_dir)
        + ",\"serial\":" + std::to_string(lib.serial) + ",\"prefabs\":[";
    for (size_t i = 0; i < lib.prefabs.size(); i++) {
        const Prefab& p = lib.prefabs[i];
        text += std::string(i ? "," : "") + "{\"id\":" + prefab::json_quote(p.id) + ",\"name\":" + prefab::json_quote(p.name)
            + ",\"category\":" + prefab::json_quote(p.category) + ",\"icon\":" + prefab::json_quote(p.icon)
            + ",\"source\":\"" + (p.user ? "user" : "builtin") + "\",\"overrides_builtin\":" + (p.overrides_builtin ? "true" : "false")
            + ",\"path\":" + prefab::json_quote(p.path) + ",\"error\":" + prefab::json_quote(p.error) + ",\"align\":"
            + prefab::json_quote(p.align) + ",\"lights\":" + std::to_string(p.lights.size()) + ",\"suggest\":"
            + (p.suggest.enabled ? "{\"hue\":" + prefab::json_quote(p.suggest.hue) + "}" : "null") + ",\"params\":[";
        for (size_t k = 0; k < p.params.size(); k++) {
            const prefab::Param& q = p.params[k];
            static const char* types[] = { "float", "color", "bool", "enum" };
            text += std::string(k ? "," : "") + "{\"key\":" + prefab::json_quote(q.key) + ",\"label\":" + prefab::json_quote(q.label)
                + ",\"type\":\"" + types[q.type] + "\",\"default\":" + value_json(q.def) + ",\"min\":" + num(q.min)
                + ",\"max\":" + num(q.max) + ",\"group\":" + prefab::json_quote(q.group) + "}";
        }
        text += "]}";
    }
    return copy_out(text + "]}", out, size);
}

int place_method(const Json& params, char* out, int size)
{
    track_room();
    std::string item;
    if (const Json* j = params.get("item"))
        item = j->string;
    else if (const Json* p = params.get("prefab"))
        item = "prefab:" + p->string;
    else if (const Json* l = params.get("light"))
        item = "light:" + l->string;
    std::string phase = params.get("phase") ? params.get("phase")->string : "move";
    float x = 0, y = 0;
    bool have_cursor = cursor_of(params, &x, &y);
    int index = -1;
    if (phase == "begin") {
        if (item.empty() || !have_cursor)
            return failure("begin needs item (or prefab / light) and an editor view", out, size);
        place_drag_begin(item, true, x, y);
    } else if (phase == "move") {
        if (!place_drag_active())
            return failure("no drag in progress", out, size);
        place_drag_move(x, y);
    } else if (phase == "drop") {
        if (!place_drag_active())
            return failure("no drag in progress", out, size);
        if (have_cursor && (params.get("x") || params.get("u")))
            place_drag_move(x, y);
        index = place_drag_drop();
    } else if (phase == "cancel") {
        place_drag_cancel();
    } else if (phase == "double_click") {
        if (item.empty())
            return failure("double_click needs item", out, size);
        index = place_at_centre(item);
    } else if (phase != "state") {
        return failure("phase is begin, move, drop, cancel, double_click or state", out, size);
    }
    std::string current;
    Placement where;
    bool over = false;
    bool active = place_drag_active(&current, &where, &over);
    std::string text = std::string("{\"active\":") + (active ? "true" : "false") + ",\"item\":" + prefab::json_quote(current)
        + ",\"over_view\":" + (over ? "true" : "false") + ",\"placement\":" + placement_json(where) + ",\"index\":"
        + std::to_string(index) + ",\"selected_light\":" + std::to_string(selected) + ",\"selected_actor\":"
        + std::to_string(selected_actor) + ",\"lights\":" + std::to_string(lights.size()) + ",\"actors\":"
        + std::to_string(actors.size()) + ",\"status\":" + prefab::json_quote(status) + "}";
    return copy_out(text, out, size);
}
} // namespace

/* RPC methods actors, prefabs and editor_place (dev_rpc.c hands over the
 * params object's text). Returns 0 with the result JSON in `out`, or -1
 * with an error message. */
extern "C" int pc_actors_rpc(const char* method, const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return failure("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    std::string m = method;
    if (m == "actors")
        return actors_method(params, out, size);
    if (m == "prefabs")
        return prefabs_method(params, out, size);
    if (m == "editor_place")
        return place_method(params, out, size);
    if (m == "glows")
        return glows_method(params, out, size);
    if (m == "fixtures")
        return pc_fixtures_rpc(params_text, out, size);
    return failure("unknown method", out, size);
}
