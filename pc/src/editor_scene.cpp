// The editor's objects for its selection (editor_scene.h).
#include "editor_scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_picking.h"
#include "editor_math.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "glow_overrides.h"
#include "lights_internal.h"

extern "C" int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick); /* lights.cpp */
extern "C" int pc_overlay_visible(void); /* dev_overlay.cpp */

using rood_selection::Ref;
using namespace rood_lights;

namespace {
/* An object's icon in the view this frame (lights.cpp, atmosphere.cpp). */
struct Icon {
    Ref ref;
    float x, y, radius;
    bool anchor;
};
std::vector<Icon> icons;
/* The primary's Outliner row as last drawn (RPC). */
std::string row_target;
bool row_visible;
int row_frame = -1;
/* The selection serial last scrolled to, and a scroll waiting for its row. */
unsigned scrolled, waiting_serial;
int waiting_frames;

int frame_count() { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1; }
std::string glow_at_id(const float p[3])
{
    char text[64];
    snprintf(text, sizeof text, "glow:@%.0f,%.0f,%.0f", p[0], p[1], p[2]);
    return text;
}
/* A glow reference's glow now: by its key, or the one nearest its position. */
uint32_t glow_id_of(const Ref& r)
{
    if (r.kind != rood_selection::KIND_GLOW || r.id.size() <= 5)
        return 0;
    const char* key = r.id.c_str() + 5;
    float p[3];
    if (key[0] == '@')
        return sscanf(key + 1, "%f,%f,%f", &p[0], &p[1], &p[2]) == 3 ? pc_gi_glow_near(p, 96) : 0;
    return pc_gi_glow_by_key(key);
}
const Icon* anchor_of(const Ref& r)
{
    for (const Icon& c : icons)
        if (c.anchor && c.ref == r)
            return &c;
    return nullptr;
}
/* Every object of the room: lights, actors, fog volumes, the game's glows (not effects). */
std::vector<Ref> all_objects()
{
    std::vector<Ref> out;
    for (int i = 0; i < (int)lights.size(); i++)
        out.push_back(rood_scene::light_ref(i));
    for (int i = 0; i < (int)actors.size(); i++)
        out.push_back(rood_scene::actor_ref(i));
    for (int i = 0; i < rood_atmo::volume_count(); i++)
        out.push_back(rood_scene::volume_ref(i));
    if (rood_env::sun_state().defined)
        out.push_back(Ref { rood_selection::KIND_SUN, "env:sun" });
    static pc_glow_info glows[160];
    int n = pc_gi_glow_list(glows, 160);
    for (int i = 0; i < n; i++)
        if (glows[i].category != PC_GLOW_EFFECT)
            out.push_back(rood_scene::glow_ref_key(glows[i].key, glows[i].position));
    return out;
}
} // namespace

/* ---- references ---- */

Ref rood_scene::light_ref(int index)
{
    if (index < 0 || index >= (int)lights.size())
        return Ref();
    if (lights[index].id.empty())
        ensure_ids();
    return Ref { rood_selection::KIND_LIGHT, light_item_id(index) };
}
Ref rood_scene::actor_ref(int index)
{
    if (index < 0 || index >= (int)actors.size())
        return Ref();
    if (actors[index].id.empty())
        ensure_ids();
    return Ref { rood_selection::KIND_ACTOR, actor_item_id(index) };
}
Ref rood_scene::volume_ref(int index)
{
    std::string id = rood_atmo::volume_id(index);
    return id.empty() ? Ref() : Ref { rood_selection::KIND_VOLUME, id };
}
Ref rood_scene::glow_ref_key(const char* key, const float position[3])
{
    return Ref { rood_selection::KIND_GLOW, key && key[0] ? std::string("glow:") + key : glow_at_id(position) };
}
Ref rood_scene::glow_ref(uint32_t id)
{
    pc_glow_info g;
    return id && pc_gi_glow_get(id, &g) ? glow_ref_key(g.key, g.position) : Ref();
}
int rood_scene::index_of(const Ref& r)
{
    switch (r.kind) {
    case rood_selection::KIND_LIGHT:
        for (size_t i = 0; i < lights.size(); i++)
            if (r.id.size() > 6 && !r.id.compare(6, std::string::npos, lights[i].id))
                return (int)i;
        return -1;
    case rood_selection::KIND_ACTOR:
        for (size_t i = 0; i < actors.size(); i++)
            if (r.id.size() > 6 && !r.id.compare(6, std::string::npos, actors[i].id))
                return (int)i;
        return -1;
    case rood_selection::KIND_VOLUME:
        return rood_atmo::volume_index(r.id);
    case rood_selection::KIND_GLOW: {
        uint32_t id = glow_id_of(r);
        return id ? (int)id : -1;
    }
    default:
        return -1;
    }
}
bool rood_scene::exists(const Ref& r)
{
    if (r.kind == rood_selection::KIND_SUN)
        return rood_env::sun_state().defined;
    return r.kind == rood_selection::KIND_GLOW || index_of(r) >= 0;
}
std::string rood_scene::label(const Ref& r)
{
    int i = index_of(r);
    char text[160];
    switch (r.kind) {
    case rood_selection::KIND_LIGHT:
        if (i < 0)
            break;
        snprintf(text, sizeof text, "%s %d", light_type_name(lights[i].type), i + 1);
        return text;
    case rood_selection::KIND_ACTOR: {
        if (i < 0)
            break;
        const prefab::Prefab* p = prefab::library().find(actors[i].prefab);
        snprintf(text, sizeof text, "%s %d", p ? p->name.c_str() : actors[i].prefab.c_str(), i + 1);
        return text;
    }
    case rood_selection::KIND_VOLUME:
        if (i >= 0)
            return rood_atmo::volume_label(i);
        break;
    case rood_selection::KIND_GLOW: {
        pc_glow_info g;
        if (i > 0 && pc_gi_glow_get((uint32_t)i, &g))
            return g.name;
        return "Glow";
    }
    case rood_selection::KIND_SUN:
        return "Sun";
    default:
        break;
    }
    return r.id;
}
bool rood_scene::position(const Ref& r, float out[3], float* radius)
{
    int i = index_of(r);
    switch (r.kind) {
    case rood_selection::KIND_LIGHT:
        if (i < 0)
            return false;
        memcpy(out, lights[i].position, sizeof lights[i].position);
        *radius = lights[i].radius;
        return true;
    case rood_selection::KIND_ACTOR: {
        if (i < 0)
            return false;
        memcpy(out, actors[i].position, sizeof actors[i].position);
        std::vector<prefab::EvalLight> ls;
        actor_lights(actors[i], ls, false);
        *radius = 200;
        for (const prefab::EvalLight& e : ls)
            *radius = fmaxf(*radius, fminf(e.radius, 600));
        return true;
    }
    case rood_selection::KIND_VOLUME:
        return rood_atmo::volume_centre(i, out, radius);
    case rood_selection::KIND_GLOW: { /* a game glow: where it is now, a nominal radius */
        pc_glow_info g;
        if (i > 0 && pc_gi_glow_get((uint32_t)i, &g))
            memcpy(out, g.position, 3 * sizeof(float));
        else if (r.id.size() <= 6 || r.id[5] != '@' || sscanf(r.id.c_str() + 6, "%f,%f,%f", &out[0], &out[1], &out[2]) != 3)
            return false;
        *radius = 200;
        return true;
    }
    default:
        return false;
    }
}

/* ---- in step ---- */

void rood_scene::publish()
{
    Ref p = rood_selection::primary();
    selected = p.kind == rood_selection::KIND_LIGHT ? index_of(p) : -1;
    selected_actor = p.kind == rood_selection::KIND_ACTOR ? index_of(p) : -1;
    multi.clear(); /* the other selected lights (Create prefab) */
    for (const Ref& r : rood_selection::items())
        if (r.kind == rood_selection::KIND_LIGHT && r != p) {
            int i = index_of(r);
            if (i >= 0)
                multi.push_back(i);
        }
    glow_selected = p.kind == rood_selection::KIND_GLOW;
    glow_id = glow_selected ? glow_id_of(p) : 0;
    if (glow_selected) {
        pc_glow_info g;
        if (glow_id && pc_gi_glow_get(glow_id, &g))
            memcpy(glow_at, g.position, sizeof glow_at);
        else if (p.id.size() > 6 && p.id[5] == '@')
            sscanf(p.id.c_str() + 6, "%f,%f,%f", &glow_at[0], &glow_at[1], &glow_at[2]);
    }
    rood_atmo::set_selected(p.kind == rood_selection::KIND_VOLUME ? index_of(p) : -1);
}
void rood_scene::refresh()
{
    rood_selection::retain([](const Ref& r) { return exists(r); }, rood_selection::FROM_CODE);
    Ref pinned = rood_selection::locked();
    if (pinned && !exists(pinned))
        rood_selection::lock(false);
    publish();
}
void rood_scene::frame()
{
    track_room();
    rood_selection::set_room(zone, room);
    refresh();
    /* A scroll to a row the Outliner never draws (filtered out, its tab
     * hidden) gives up after a few frames: its section may close again. */
    unsigned s = rood_selection::serial();
    if (s != scrolled) {
        if (s != waiting_serial)
            waiting_serial = s, waiting_frames = 0;
        else if (++waiting_frames > 3)
            scrolled = s;
    }
}

/* ---- the Outliner ---- */

void rood_scene::row_clicked(const Ref& r)
{
    ImGuiIO& io = ImGui::GetIO();
    rood_selection::click(r, io.KeyShift, io.KeyCtrl, rood_selection::FROM_OUTLINER);
    publish();
}
void rood_scene::row_drawn(const Ref& r)
{
    if (r != rood_selection::primary())
        return;
    if (rood_selection::serial() != scrolled) {
        if (rood_selection::last_source() != rood_selection::FROM_OUTLINER)
            ImGui::SetScrollHereY(0.5f); /* the row just drawn, centred */
        scrolled = rood_selection::serial();
    }
    row_target = r.id;
    row_visible = ImGui::IsItemVisible();
    row_frame = ImGui::GetFrameCount();
}
bool rood_scene::scroll_due(rood_selection::Kind a, rood_selection::Kind b)
{
    Ref p = rood_selection::primary();
    return p && rood_selection::serial() != scrolled && rood_selection::last_source() != rood_selection::FROM_OUTLINER
        && (p.kind == a || p.kind == b);
}

/* ---- the viewport ---- */

void rood_scene::icons_begin() { icons.clear(); }
void rood_scene::icon(const Ref& r, float x, float y, float radius, bool anchor) { icons.push_back({ r, x, y, radius, anchor }); }
Ref rood_scene::object_at(float x, float y)
{
    const Icon* best = nullptr;
    float best_d = 0;
    int best_rank = 9;
    for (const Icon& c : icons) {
        float dx = x - c.x, dy = y - c.y, d = dx * dx + dy * dy;
        if (c.radius <= 0 || d >= c.radius * c.radius)
            continue;
        int rank = c.ref.kind == rood_selection::KIND_LIGHT ? 0 : c.ref.kind == rood_selection::KIND_ACTOR ? 1 : 2;
        if (!best || rank < best_rank || (rank == best_rank && d < best_d))
            best = &c, best_rank = rank, best_d = d;
    }
    return best ? best->ref : Ref();
}
Ref rood_scene::volume_at(const pc_overlay_view& v, pc_overlay_pick pick, float x, float y)
{
    float o[3], d[3];
    if (!rood_pick::ray(v.view, v.projection, v.rect, x, y, o, d))
        return Ref();
    float best = 1e30f;
    int found = -1;
    for (int i = 0; i < rood_atmo::volume_count(); i++) {
        float t = rood_atmo::volume_hit(i, o, d);
        if (t >= 0 && t < best)
            best = t, found = i;
    }
    if (found < 0)
        return Ref();
    float world[3], normal[3];
    if (pick && pick(x, y, world, normal)) {
        float along = (world[0] - o[0]) * d[0] + (world[1] - o[1]) * d[1] + (world[2] - o[2]) * d[2];
        if (along + 1 < best)
            return Ref(); /* a room surface is in front of it */
    }
    return volume_ref(found);
}
std::vector<Ref> rood_scene::in_box(float x0, float y0, float x1, float y1)
{
    std::vector<Ref> out;
    for (const Icon& c : icons)
        if (c.anchor && c.ref.kind != rood_selection::KIND_GLOW && c.ref.kind != rood_selection::KIND_SUN && rood_pick::in_rect(c.x, c.y, x0, y0, x1, y1)
            && std::find(out.begin(), out.end(), c.ref) == out.end())
            out.push_back(c.ref);
    return out;
}
Ref rood_scene::click_at(float x, float y, bool shift, bool ctrl, rood_selection::Source s)
{
    Ref under = object_at(x, y);
    pc_overlay_view v;
    pc_overlay_pick pick = nullptr;
    if (!under && pc_lights_tools_view(&v, &pick))
        under = volume_at(v, pick, x, y);
    if (under)
        rood_selection::click(under, shift, ctrl, s);
    else if (!shift && !ctrl)
        rood_selection::clear(s);
    publish();
    return under;
}

/* ---- hidden in the editor ---- */

namespace {
bool hiding() { return rood_selection::any_hidden() && pc_overlay_visible(); }
} // namespace
bool rood_scene::light_hidden(int index)
{
    return hiding() && index >= 0 && index < (int)lights.size()
        && rood_selection::hidden(Ref { rood_selection::KIND_LIGHT, "light:" + lights[index].id });
}
bool rood_scene::actor_hidden(int index)
{
    return hiding() && index >= 0 && index < (int)actors.size()
        && rood_selection::hidden(Ref { rood_selection::KIND_ACTOR, "actor:" + actors[index].id });
}
bool rood_scene::volume_hidden_id(const std::string& id)
{
    return hiding() && rood_selection::hidden(Ref { rood_selection::KIND_VOLUME, "fog:" + id });
}
bool rood_scene::glow_hidden(const char* key, const float position[3])
{
    return hiding() && rood_selection::hidden(glow_ref_key(key, position));
}
void rood_scene::hide_selection()
{
    std::vector<Ref> refs = rood_selection::items();
    if (refs.empty())
        return;
    rood_selection::hide(refs);
    rood_selection::clear(rood_selection::FROM_CODE);
    publish();
    snprintf(status, sizeof status, "Hid %zu object%s in the editor (Ctrl+H shows everything)", refs.size(),
        refs.size() == 1 ? "" : "s");
}
void rood_scene::isolate_selection()
{
    if (rood_selection::empty())
        return;
    std::vector<Ref> others;
    for (const Ref& r : all_objects())
        if (!rood_selection::contains(r))
            others.push_back(r);
    rood_selection::hide(others);
    snprintf(status, sizeof status, "Hid %zu other object%s in the editor (Ctrl+H shows everything)", others.size(),
        others.size() == 1 ? "" : "s");
}
void rood_scene::unhide_all()
{
    rood_selection::unhide_all();
    snprintf(status, sizeof status, "Everything shows again");
}

/* ---- operations ---- */

int rood_scene::delete_selection()
{
    track_room();
    std::vector<int> gone_lights, gone_actors, gone_volumes;
    int glows = 0;
    std::string single;
    for (const Ref& r : rood_selection::items()) {
        if (r.kind == rood_selection::KIND_GLOW) {
            ++glows;
            continue;
        }
        int i = index_of(r);
        if (i < 0)
            continue;
        (r.kind == rood_selection::KIND_LIGHT ? gone_lights : r.kind == rood_selection::KIND_ACTOR ? gone_actors : gone_volumes)
            .push_back(i);
        single = label(r);
    }
    int count = (int)(gone_lights.size() + gone_actors.size() + gone_volumes.size());
    if (!count) {
        if (glows)
            snprintf(status, sizeof status, "Glows come from the game: untick one in the Outliner to mute it");
        return 0;
    }
    pc_undo_hint((count == 1 ? "Delete " + single : "Delete " + std::to_string(count) + " objects").c_str());
    auto descending = [](std::vector<int>& v) {
        std::sort(v.rbegin(), v.rend());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    descending(gone_lights);
    descending(gone_actors);
    descending(gone_volumes);
    for (int i : gone_lights) {
        lights.erase(lights.begin() + i);
        if (solo == i)
            solo = -1;
        else if (solo > i)
            --solo;
    }
    for (int i : gone_actors) {
        actors.erase(actors.begin() + i);
        if (solo_actor == i)
            solo_actor = -1;
        else if (solo_actor > i)
            --solo_actor;
    }
    for (int i : gone_volumes)
        rood_atmo::remove_volume(i);
    rood_selection::clear(rood_selection::FROM_CODE);
    publish();
    dirty = true;
    snprintf(status, sizeof status, "Deleted %d object%s (unsaved; Ctrl+Z undoes)", count, count == 1 ? "" : "s");
    return count;
}

/* ---- RPC editor_select ---- */

namespace {
using prefab::Json;
std::string quote(const std::string& s) { return prefab::json_quote(s); }
int rpc_fail(const std::string& message, char* out, int size)
{
    snprintf(out, size, "%s", message.c_str());
    return -1;
}
bool flag(const Json& params, const char* key)
{
    const Json* j = params.get(key);
    return j && (j->type == Json::Bool ? j->boolean : j->number != 0);
}
/* The window pixel from x, y or u, v (of the editor's view). */
bool cursor_of(const Json& params, float* x, float* y)
{
    pc_overlay_view v;
    if (!pc_lights_tools_view(&v, nullptr))
        return false;
    *x = v.rect[0] + v.rect[2] * 0.5f, *y = v.rect[1] + v.rect[3] * 0.5f;
    if (const Json* u = params.get("u"))
        *x = v.rect[0] + v.rect[2] * (float)u->number;
    if (const Json* j = params.get("v"))
        *y = v.rect[1] + v.rect[3] * (float)j->number;
    if (const Json* j = params.get("x"))
        *x = (float)j->number;
    if (const Json* j = params.get("y"))
        *y = (float)j->number;
    return true;
}
std::string refs_json(const std::vector<Ref>& refs)
{
    std::string s = "[";
    for (size_t i = 0; i < refs.size(); i++)
        s += (i ? "," : "") + quote(refs[i].id);
    return s + "]";
}
/* The selection, the Outliner's row, the hidden set, the view and every object (no closing brace). */
std::string state_json()
{
    Ref p = rood_selection::primary(), pinned = rood_selection::locked();
    std::string s = "{\"zone\":" + std::to_string(zone) + ",\"room\":" + std::to_string(room)
        + ",\"selection\":" + refs_json(rood_selection::items()) + ",\"primary\":" + (p ? quote(p.id) : "null")
        + ",\"serial\":" + std::to_string(rood_selection::serial()) + ",\"source\":\""
        + rood_selection::source_name(rood_selection::last_source()) + "\",\"locked\":" + (pinned ? quote(pinned.id) : "null")
        + ",\"outliner\":{\"target\":" + (row_frame >= 0 && row_frame == frame_count() ? quote(row_target) : "null")
        + ",\"visible\":" + (row_visible ? "true" : "false") + "},\"hidden\":" + refs_json(rood_selection::hidden_items())
        + ",\"hidden_setting\":" + quote(rood_selection::hidden_line(zone, room));
    pc_overlay_view v;
    if (pc_lights_tools_view(&v, nullptr)) {
        char rect[128];
        snprintf(rect, sizeof rect, ",\"view\":[%.1f,%.1f,%.1f,%.1f]", v.rect[0], v.rect[1], v.rect[2], v.rect[3]);
        s += rect;
    }
    s += ",\"objects\":[";
    bool first = true;
    for (const Ref& r : all_objects()) {
        const Icon* c = anchor_of(r);
        char screen[64] = "null";
        if (c)
            snprintf(screen, sizeof screen, "[%.1f,%.1f]", c->x, c->y);
        s += std::string(first ? "" : ",") + "{\"id\":" + quote(r.id) + ",\"kind\":\"" + rood_selection::kind_name(r.kind)
            + "\",\"label\":" + quote(rood_scene::label(r)) + ",\"screen\":" + screen
            + ",\"hidden\":" + (rood_selection::hidden(r) ? "true" : "false")
            + ",\"selected\":" + (rood_selection::contains(r) ? "true" : "false") + "}";
        first = false;
    }
    return s + "],\"status\":" + quote(status);
}
} // namespace

/* RPC editor_select (dev_rpc.c hands over the params object's text):
 * action list (default), set / add / toggle / remove {ids}, clear, click
 * {x, y | u, v, shift, ctrl} (as a click in the view: "under" is what it
 * hit), box {x0, y0, x1, y1, shift, ctrl}, lock {on} (Details' lock), hide
 * {ids} (default the selection), isolate, unhide_all, copy (replies the
 * clipboard), paste {text}, duplicate, delete; project {point} (replies
 * pixel), pick {x, y | u, v} (replies world, normal); source
 * "viewport" | "outliner" | "details" | "rpc" (default) says where the
 * change counts as coming from. Replies with the selection, its primary,
 * serial and source, the Details lock, the Outliner's row of the primary,
 * the room's hidden set (and its settings line), the view and every object
 * with its window pixel. Returns 0, or -1 with an error message. */
extern "C" int pc_editor_select_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return rpc_fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    rood_scene::frame();
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "list";
    rood_selection::Source source = rood_selection::FROM_RPC;
    if (const Json* s = params.get("source"))
        source = s->string == "viewport" ? rood_selection::FROM_VIEWPORT
            : s->string == "outliner"    ? rood_selection::FROM_OUTLINER
            : s->string == "details"     ? rood_selection::FROM_DETAILS
                                         : rood_selection::FROM_RPC;
    std::vector<Ref> refs;
    if (const Json* ids = params.get("ids"))
        for (const Json& j : ids->items) {
            Ref r = rood_selection::ref_of(j.string);
            if (!r || !rood_scene::exists(r))
                return rpc_fail("no object " + j.string + " in this room", out, size);
            refs.push_back(r);
        }
    std::string extra;
    if (action == "list") {
    } else if (action == "set") {
        rood_selection::set_all(refs, source);
    } else if (action == "add") {
        for (const Ref& r : refs)
            rood_selection::add(r, source);
    } else if (action == "toggle") {
        for (const Ref& r : refs)
            rood_selection::toggle(r, source);
    } else if (action == "remove") {
        for (const Ref& r : refs)
            rood_selection::remove(r, source);
    } else if (action == "clear") {
        rood_selection::clear(source);
    } else if (action == "click") {
        float x, y;
        if (!cursor_of(params, &x, &y))
            return rpc_fail("click needs an editor view (hardware renderer and a room)", out, size);
        Ref under = rood_scene::click_at(x, y, flag(params, "shift"), flag(params, "ctrl"), source);
        extra += ",\"under\":" + (under ? quote(under.id) : std::string("null"));
    } else if (action == "box") {
        const Json *x0 = params.get("x0"), *y0 = params.get("y0"), *x1 = params.get("x1"), *y1 = params.get("y1");
        if (!x0 || !y0 || !x1 || !y1)
            return rpc_fail("box needs x0, y0, x1, y1 (window pixels)", out, size);
        rood_selection::box(rood_scene::in_box((float)x0->number, (float)y0->number, (float)x1->number, (float)y1->number),
            flag(params, "shift"), flag(params, "ctrl"), source);
    } else if (action == "lock") {
        rood_selection::lock(flag(params, "on")); /* Details' lock, on the primary */
    } else if (action == "hide") {
        if (refs.empty())
            rood_scene::hide_selection();
        else {
            rood_selection::hide(refs);
            for (const Ref& r : refs)
                rood_selection::remove(r, source);
        }
    } else if (action == "unhide_all") {
        rood_scene::unhide_all();
    } else if (action == "isolate") {
        rood_scene::isolate_selection();
    } else if (action == "copy") {
        int n = rood_scene::copy_selection();
        extra += ",\"count\":" + std::to_string(n) + ",\"clipboard\":" + quote(rood_scene::clipboard());
    } else if (action == "paste") {
        const float here[3] = { 0, 0, 0 };
        const Json* t = params.get("text");
        int n = t ? rood_scene::paste_text(t->string, here, "Paste") : rood_scene::paste_clipboard();
        extra += ",\"count\":" + std::to_string(n);
    } else if (action == "duplicate") {
        extra += ",\"count\":" + std::to_string(rood_scene::duplicate_selection());
    } else if (action == "delete") {
        extra += ",\"count\":" + std::to_string(rood_scene::delete_selection());
    } else if (action == "project") { /* a world point's window pixel (null behind the camera) */
        const Json* p = params.get("point");
        pc_overlay_view v;
        if (!p || p->type != Json::Array || p->items.size() != 3 || !pc_lights_tools_view(&v, nullptr))
            return rpc_fail("project needs point [x, y, z] and an editor view", out, size);
        float w[3] = { (float)p->items[0].number, (float)p->items[1].number, (float)p->items[2].number }, px[2];
        char text[96];
        if (rood_math::project(v.view, v.projection, v.rect, w, px))
            snprintf(text, sizeof text, ",\"pixel\":[%.2f,%.2f]", px[0], px[1]);
        else
            snprintf(text, sizeof text, ",\"pixel\":null");
        extra += text;
    } else if (action == "pick") { /* the surface under a window pixel {x, y | u, v} */
        float x, y, w[3], n[3];
        pc_overlay_view v;
        pc_overlay_pick pick = nullptr;
        if (!cursor_of(params, &x, &y) || !pc_lights_tools_view(&v, &pick) || !pick)
            return rpc_fail("pick needs an editor view", out, size);
        char text[160];
        if (pick(x, y, w, n))
            snprintf(text, sizeof text, ",\"world\":[%.2f,%.2f,%.2f],\"normal\":[%.4f,%.4f,%.4f]", w[0], w[1], w[2], n[0],
                n[1], n[2]);
        else
            snprintf(text, sizeof text, ",\"world\":null,\"normal\":null");
        extra += text;
    } else {
        return rpc_fail("unknown action " + action, out, size);
    }
    rood_scene::publish();
    std::string text = state_json() + extra + "}";
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
