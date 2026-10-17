// The editor's library (editor_library.h): its sources (basic lights, light
// prefabs, fixtures, fog presets; more by add_source), placing their items,
// settings and RPC editor_library. The panel and thumbnails: Task 17; Save
// as prefab: Task 18.
#include "editor_library.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <set>

#include "imgui.h"
#include "imgui_internal.h" // RegisterUserTexture, ImTextureDataQueueUpload
#include "editor_math.h"
#include "editor_scene.h"
#include "editor_selection.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "fixture_catalog.h"
#include "particles_internal.h"

using prefab::Json;
using prefab::Value;
using rood_library::Entry;
using rood_library::Source;
using rood_lights::Placement;

namespace {
std::vector<Source>& sources()
{
    static std::vector<Source> s;
    return s;
}
unsigned sources_serial = 1; /* bumps when a source is added or replaced */
void ensure_builtins();

std::vector<Entry> all_entries;
std::vector<unsigned> seen_serials;
unsigned seen_sources;
rood_library::Filter filter;
std::set<std::string> favourites;
bool grid_view = true, in_column = true;
float window_rect[4] = { -1, -1, -1, -1 }; /* the floating window: x, y, w, h (-1: not placed yet) */

const Source* source_of(const std::string& kind)
{
    ensure_builtins();
    for (const Source& s : sources())
        if (s.kind == kind)
            return &s;
    return nullptr;
}
std::string kind_of_item(const std::string& item)
{
    size_t colon = item.find(':');
    return colon == std::string::npos ? std::string() : item.substr(0, colon);
}
std::string upper_first(std::string s)
{
    for (char& c : s)
        if (c == '_')
            c = ' ';
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
        s[0] = (char)(s[0] - 'a' + 'A');
    return s;
}

/* ---- thumbnails' scenes ---- */
void wall_facing(prefab::Actor& a, const rood_thumbs::Scene& s, float height)
{
    const float out[3] = { 0, 0, -1 };
    prefab::rotation_facing(out, a.rotation);
    a.position[0] = 0, a.position[1] = -height, a.position[2] = s.wall_z;
}
bool prefab_scene(const prefab::Prefab& p, const std::vector<std::pair<std::string, Value>>& overrides, rood_thumbs::Scene& s)
{
    prefab::Actor a;
    a.prefab = p.id;
    a.overrides = overrides;
    if (p.align == "normal")
        wall_facing(a, s, 96);
    prefab::evaluate(p, a, 0, false, s.lights);
    return !s.lights.empty();
}

/* ---- the built-in sources ---- */
Source light_source()
{
    Source s;
    s.kind = "light";
    s.serial = [] { return 1u; };
    s.list = [](std::vector<Entry>& out) {
        static const char* const types[3][3] = { { "point", "Point light", "Light in every direction from a point." },
            { "spot", "Spot light", "A cone of light (it aims away from the surface it is dropped on)." },
            { "rect", "Rect light", "An area light from a rectangle's front face (it faces away from the surface)." } };
        for (const auto& t : types) {
            Entry e;
            e.kind = "light", e.id = t[0], e.item = std::string("light:") + t[0];
            e.name = t[1], e.category = "Lights", e.description = t[2];
            e.tags = { "light", t[0] };
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) { return rood_lights::place_commit(e.item, w); };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) {
        prefab::EvalLight l;
        const rood_lights::Light d = rood_lights::Light();
        for (int k = 0; k < 3; k++)
            l.color[k] = d.color[k];
        l.intensity = 1.5f;
        if (e.id == "spot") {
            l.type = prefab::LIGHT_SPOT, l.position[1] = -220, l.direction[0] = 0, l.direction[1] = 1, l.direction[2] = 0;
            l.cone = 50, l.radius = 420;
        } else if (e.id == "rect") {
            l.type = prefab::LIGHT_RECT, l.position[1] = -110, l.position[2] = sc.wall_z - 1;
            l.direction[0] = 0, l.direction[1] = 0, l.direction[2] = -1;
            l.size[0] = 120, l.size[1] = 80, l.radius = 420;
        } else {
            l.position[1] = -64, l.radius = 320;
        }
        sc.lights.push_back(l);
        return true;
    };
    return s;
}
Source prefab_source()
{
    Source s;
    s.kind = "prefab";
    s.serial = [] { return (unsigned)prefab::library().serial; };
    s.list = [](std::vector<Entry>& out) {
        for (const prefab::Prefab& p : prefab::library().prefabs) {
            Entry e;
            e.kind = "prefab", e.id = p.id, e.item = "prefab:" + p.id;
            e.name = p.name, e.category = p.category, e.description = p.description, e.error = p.error;
            e.tags = { p.icon, p.align == "normal" ? "wall" : "upright" };
            e.mine = p.user;
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) { return rood_lights::place_commit(e.item, w); };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) {
        const prefab::Prefab* p = prefab::library().find(e.id);
        return p && prefab_scene(*p, {}, sc);
    };
    s.prefab = [](const Entry& e) { return prefab::library().find(e.id); };
    s.file = [](const Entry& e) {
        const prefab::Prefab* p = prefab::library().find(e.id);
        return p && p->user ? p->path : std::string();
    };
    s.rename = [](const Entry& e, const std::string& name, std::string* message) {
        const prefab::Prefab* p = prefab::library().find(e.id);
        if (!p || !p->user || name.empty()) {
            *message = !p || !p->user ? "Rename: not a prefab of yours" : "Rename: give it a name";
            return false;
        }
        Json j = p->source;
        j.set("name", Json::str(name));
        std::string error, id = p->id;
        if (!prefab::library().save_user(id, j, &error)) {
            *message = error;
            return false;
        }
        *message = "Renamed prefab " + id + " to " + name;
        return true;
    };
    s.remove = [](const Entry& e, std::string* message) {
        const prefab::Prefab* p = prefab::library().find(e.id);
        if (!p || !p->user) {
            *message = "Delete: not a prefab of yours";
            return false;
        }
        std::string error, id = p->id, path = p->path;
        bool builtin = p->overrides_builtin;
        if (!prefab::library().revert_user(id, &error)) {
            *message = error;
            return false;
        }
        *message = (builtin ? "Reverted prefab " + id + " to the built-in" : "Deleted prefab " + id)
            + " (moved aside: " + path + ".bak)";
        return true;
    };
    return s;
}
/* A fixture class (pc/assets/fixtures/catalog.json "classes") that is a light: its prefab and parameters. */
const Json* class_json(const std::string& cls)
{
    const Json* classes = fixtures::catalog().json.get("classes");
    return classes ? classes->get(cls.c_str()) : nullptr;
}
std::string class_prefab(const Json* c)
{
    const Json* p = c ? c->get("prefab") : nullptr;
    return p && p->type == Json::String ? p->string : std::string();
}
std::vector<std::pair<std::string, Value>> class_params(const Json* c, const prefab::Prefab& p)
{
    std::vector<std::pair<std::string, Value>> out;
    const Json* params = c ? c->get("params") : nullptr;
    for (size_t i = 0; params && i < params->members.size(); i++) {
        const auto& m = params->members[i];
        if (!p.param(m.first))
            continue;
        if (m.second.type == Json::Number)
            out.push_back({ m.first, Value::scalar((float)m.second.number) });
        else if (m.second.type == Json::Array && m.second.items.size() == 3)
            out.push_back({ m.first, Value::vec((float)m.second.items[0].number, (float)m.second.items[1].number,
                                         (float)m.second.items[2].number) });
        else if (m.second.type == Json::Bool)
            out.push_back({ m.first, Value::scalar(m.second.boolean ? 1.0f : 0.0f) });
    }
    return out;
}
Source fixture_source()
{
    Source s;
    s.kind = "fixture";
    s.serial = [] { return (unsigned)fixtures::catalog().serial * 7919u + (unsigned)prefab::library().serial; };
    s.list = [](std::vector<Entry>& out) {
        const Json* classes = fixtures::catalog().json.get("classes");
        for (size_t i = 0; classes && i < classes->members.size(); i++) {
            const auto& m = classes->members[i];
            std::string id = class_prefab(&m.second);
            if (id.empty() || id == "none" || !prefab::library().find(id))
                continue; /* not a light (a cobweb, a decal) */
            const Json* d = m.second.get("description");
            const Json* fam = m.second.get("family");
            Entry e;
            e.kind = "fixture", e.id = m.first, e.item = "fixture:" + m.first;
            e.name = upper_first(m.first);
            e.category = fam && fam->type == Json::String ? upper_first(fam->string) : "Fixtures";
            e.description = (d && d->type == Json::String ? d->string : m.first)
                + ". Dropped on one of the room's fixtures it lights it (as the Light fixture tool); elsewhere it "
                  "places its prefab, " + id + ".";
            e.tags = { id, "fixture" };
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) {
        const Json* c = class_json(e.id);
        const prefab::Prefab* p = prefab::library().find(class_prefab(c));
        if (!p)
            return -1;
        int index = rood_lights::light_fixture_at(w.surface, w.normal, p->id); /* a fixture there: lit */
        if (index < 0)
            index = rood_lights::place_commit("prefab:" + p->id, w); /* none: the prefab, placed */
        if (index < 0 || index >= (int)rood_lights::actors.size())
            return -1;
        for (const auto& kv : class_params(c, *p))
            rood_lights::actors[index].set_override(kv.first, kv.second);
        rood_lights::select_actor(index);
        return index;
    };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) {
        const Json* c = class_json(e.id);
        const prefab::Prefab* p = prefab::library().find(class_prefab(c));
        return p && prefab_scene(*p, class_params(c, *p), sc);
    };
    s.prefab = [](const Entry& e) { return prefab::library().find(class_prefab(class_json(e.id))); };
    return s;
}
/* Where a drop lands for a fog volume: the surface and its normal, or a snapped point (as a floor). */
void drop_point(const Placement& w, float at[3], float n[3])
{
    bool surface = w.snap == PC_SNAP_SURFACE;
    for (int k = 0; k < 3; k++)
        at[k] = surface ? w.surface[k] : w.position[k], n[k] = surface ? w.normal[k] : (k == 1 ? -1.0f : 0.0f);
}
Source fog_source()
{
    Source s;
    s.kind = "fog";
    s.serial = [] { return rood_atmo::fog_presets_serial(); };
    s.list = [](std::vector<Entry>& out) {
        Entry plain;
        plain.kind = "fog", plain.id = "", plain.item = "fog:";
        plain.name = "Fog volume", plain.category = "Fog volumes";
        plain.description = "A new fog volume with the default look (pick a preset in Details after).";
        plain.tags = { "volume", "mist" };
        out.push_back(plain);
        for (const rood_atmo::FogPreset& p : rood_atmo::fog_presets()) {
            Entry e;
            e.kind = "fog", e.id = p.id, e.item = "fog:" + p.id;
            e.name = p.name, e.category = p.user ? "My presets" : "Fog presets", e.description = p.why;
            e.tags = { "volume", "preset" };
            e.mine = p.user;
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) {
        float at[3], n[3];
        drop_point(w, at, n);
        return rood_atmo::place_volume(e.id, at, n);
    };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) { return rood_atmo::preset_look(e.id, sc.fog); };
    s.file = [](const Entry& e) { return rood_atmo::user_preset_file(e.id); };
    s.rename = [](const Entry& e, const std::string& name, std::string* message) {
        return rood_atmo::rename_user_preset(e.id, name, message);
    };
    s.remove = [](const Entry& e, std::string* message) { return rood_atmo::delete_user_preset(e.id, message); };
    s.ghost = [](const Entry& e, const pc_overlay_view& v, const Placement& w, void* draw_list) {
        float pos[3], size[3], at[3], n[3];
        bool sphere;
        drop_point(w, at, n);
        if (!rood_atmo::volume_drop(e.id, at, n, pos, size, &sphere))
            return;
        ImDrawList* draw = (ImDrawList*)draw_list;
        static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        float q[8][2];
        bool ok[8];
        for (int c = 0; c < 8; c++) {
            float p[3] = { pos[0] + (c & 1 ? 0.5f : -0.5f) * size[0], pos[1] + (c & 2 ? 0.5f : -0.5f) * size[1],
                pos[2] + (c & 4 ? 0.5f : -0.5f) * size[2] };
            ok[c] = rood_math::project(v.view, v.projection, v.rect, p, q[c]);
        }
        for (const auto& ed : edges)
            if (ok[ed[0]] && ok[ed[1]])
                draw->AddLine(ImVec2(q[ed[0]][0], q[ed[0]][1]), ImVec2(q[ed[1]][0], q[ed[1]][1]), IM_COL32(170, 200, 255, 220),
                    1.5f);
    };
    return s;
}
/* The emitters (their files: name, category, ...); a drop puts one of the asset with its box on the surface. */
Source emitter_source()
{
    Source s = rood_library::json_dir_source("emitter", rood_library::emitters_dir(), prefab::library().user_dir + "/emitters",
        "Emitters");
    s.place = [](const Entry& e, const rood_lights::Placement& where) {
        return rood_particles::add_on_surface(e.id, where.surface);
    };
    return s;
}
void ensure_builtins()
{
    static bool added;
    if (added)
        return;
    added = true;
    std::vector<Source> builtin = { light_source(), prefab_source(), fixture_source(), fog_source(), emitter_source() };
    for (Source& s : builtin) /* before any registered earlier: a registered kind of the same name wins */
        if (std::none_of(sources().begin(), sources().end(), [&](const Source& x) { return x.kind == s.kind; }))
            sources().push_back(s);
}
} // namespace

std::string rood_library::emitters_dir()
{
    const char* e = getenv("ROOD_EMITTERS");
    return e && *e ? e : "pc/assets/emitters";
}
Source rood_library::json_dir_source(const std::string& kind, const std::string& builtin_dir, const std::string& user_dir,
    const std::string& category)
{
    struct Dirs {
        std::string builtin, user;
        long long stamps[2] = { -2, -2 };
        unsigned serial = 1;
        std::chrono::steady_clock::time_point looked;
        bool ever = false;
    };
    auto d = std::make_shared<Dirs>();
    d->builtin = builtin_dir, d->user = user_dir;
    Source s;
    s.kind = kind;
    s.serial = [d] { /* the directories looked at twice a second at most (a frame's entries() asks often) */
        auto now_time = std::chrono::steady_clock::now();
        if (!d->ever || now_time - d->looked >= std::chrono::milliseconds(500)) {
            d->ever = true, d->looked = now_time;
            long long now[2] = { prefab::directory_stamp(d->builtin), prefab::directory_stamp(d->user) };
            if (now[0] != d->stamps[0] || now[1] != d->stamps[1])
                d->stamps[0] = now[0], d->stamps[1] = now[1], ++d->serial;
        }
        return d->serial;
    };
    s.list = [d, kind, category](std::vector<Entry>& out) {
        for (int mine = 0; mine < 2; mine++) {
            const std::string& dir = mine ? d->user : d->builtin;
            for (const std::string& file : prefab::json_files(dir)) {
                Entry e;
                e.kind = kind, e.id = file.substr(0, file.size() - 5), e.item = kind + ":" + e.id;
                e.name = upper_first(e.id), e.category = mine ? "Mine" : category, e.mine = mine != 0;
                std::string text, error;
                Json j;
                FILE* f = fopen((dir + "/" + file).c_str(), "rb");
                if (f) {
                    char buf[4096];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
                        text.append(buf, n);
                    fclose(f);
                }
                if (!prefab::json_parse(text.c_str(), j, &error) || j.type != Json::Object) {
                    e.error = file + ": " + (error.empty() ? "not an object" : error);
                } else {
                    auto str = [&](const char* key, std::string* into) {
                        const Json* v = j.get(key);
                        if (v && v->type == Json::String && !v->string.empty())
                            *into = v->string;
                    };
                    str("name", &e.name), str("category", &e.category), str("description", &e.description);
                    if (const Json* t = j.get("tags"))
                        for (const Json& tag : t->items)
                            if (tag.type == Json::String)
                                e.tags.push_back(tag.string);
                }
                auto same = std::find_if(out.begin(), out.end(), [&](const Entry& o) { return o.item == e.item; });
                if (same != out.end())
                    *same = e; /* yours of the same name wins */
                else
                    out.push_back(e);
            }
        }
    };
    s.place = [kind](const Entry& e, const Placement&) {
        snprintf(rood_lights::status, sizeof rood_lights::status, "%s: placing a %s needs its own code (not in this build)",
            e.name.c_str(), kind.c_str());
        return -1;
    };
    s.file = [d](const Entry& e) {
        std::string path = (e.mine ? d->user : d->builtin) + "/" + e.id + ".json";
        return e.mine ? path : std::string();
    };
    return s;
}
void rood_library::add_source(Source s)
{
    for (Source& old : sources())
        if (old.kind == s.kind) {
            old = std::move(s);
            ++sources_serial;
            return;
        }
    sources().push_back(std::move(s));
    ++sources_serial;
}
std::vector<std::string> rood_library::source_kinds()
{
    ensure_builtins();
    std::vector<std::string> out;
    for (const Source& s : sources())
        out.push_back(s.kind);
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return kind_rank(a) < kind_rank(b); });
    return out;
}
const std::vector<Entry>& rood_library::entries()
{
    ensure_builtins();
    std::vector<unsigned> now;
    for (const Source& s : sources())
        now.push_back(s.serial ? s.serial() : 0);
    if (now != seen_serials || seen_sources != sources_serial) {
        all_entries.clear();
        for (const Source& s : sources())
            if (s.list) {
                size_t first = all_entries.size();
                s.list(all_entries);
                for (size_t i = first; i < all_entries.size(); i++)
                    all_entries[i].kind = s.kind; /* the source's kind, whatever it wrote */
            }
        sort_entries(all_entries);
        seen_serials = now, seen_sources = sources_serial;
    }
    return all_entries;
}
const Entry* rood_library::find(const std::string& item)
{
    for (const Entry& e : entries())
        if (e.item == item)
            return &e;
    return nullptr;
}
bool rood_library::is_item(const std::string& item)
{
    return item.rfind("light:", 0) != 0 && item.rfind("prefab:", 0) != 0 && find(item) != nullptr;
}
const prefab::Prefab* rood_library::item_prefab(const std::string& item)
{
    const Entry* found = find(item);
    const Source* s = found ? source_of(found->kind) : nullptr;
    if (!s || !s->prefab)
        return nullptr;
    Entry e = *found; /* a copy, as place() */
    return s->prefab(e);
}
std::string rood_library::label(const std::string& item)
{
    const Entry* e = find(item);
    return e ? e->name : item;
}
int rood_library::place(const std::string& item, const Placement& where)
{
    const Entry* found = find(item);
    if (!found)
        return -1;
    Entry e = *found; /* placing may reload a source (its entries rebuilt) */
    const Source* s = source_of(e.kind);
    return s && s->place ? s->place(e, where) : -1;
}
void rood_library::draw_ghost(const std::string& item, const pc_overlay_view& v, const Placement& where, void* draw_list)
{
    const Entry* found = find(item);
    const Source* s = found ? source_of(found->kind) : nullptr;
    if (s && s->ghost) {
        Entry e = *found; /* a copy, as place() */
        s->ghost(e, v, where, draw_list);
    }
}

/* ---- an entry's management (its right-click menu, RPC) ---- */

namespace {
/* The entry (a copy) and its source; null when there is no such item. */
const Source* entry_and_source(const std::string& item, Entry* out)
{
    const Entry* found = rood_library::find(item);
    if (!found)
        return nullptr;
    *out = *found;
    return source_of(out->kind);
}
} // namespace

bool rood_library::edit_prefab(const std::string& item, std::string* message)
{
    const prefab::Prefab* p = item_prefab(item);
    if (!p) {
        *message = "Edit prefab: " + item + " is not a prefab (nor a fixture of one)";
        return false;
    }
    rood_lights::edit_prefab(p->id);
    *message = "Editing prefab " + p->name + " (" + p->id + ")";
    return true;
}
bool rood_library::rename(const std::string& item, const std::string& name, std::string* message)
{
    Entry e;
    const Source* s = entry_and_source(item, &e);
    if (!s || !e.mine || !s->rename) {
        *message = "Rename: " + item + " is not one of yours";
        return false;
    }
    return s->rename(e, name, message);
}
bool rood_library::remove(const std::string& item, std::string* message)
{
    Entry e;
    const Source* s = entry_and_source(item, &e);
    if (!s || !e.mine || !s->remove) {
        *message = "Delete: " + item + " is not one of yours";
        return false;
    }
    if (!s->remove(e, message))
        return false;
    if (!find(item)) { /* gone (not a built-in coming back): nothing points at it */
        favourites.erase(item);
        if (rood_lights::place_item == item)
            rood_lights::place_item = "light:point";
    }
    return true;
}
std::string rood_library::file_of(const std::string& item)
{
    Entry e;
    const Source* s = entry_and_source(item, &e);
    return s && s->file ? s->file(e) : std::string();
}

/* ---- settings ---- */

bool rood_library::parse_setting(const char* key, const char* rest)
{
    if (!strcmp(key, "editor_library_view")) {
        grid_view = strncmp(rest, "list", 4) != 0;
        return true;
    }
    if (!strcmp(key, "editor_library_docked")) {
        in_column = atoi(rest) != 0;
        return true;
    }
    if (!strcmp(key, "editor_library_window")) {
        sscanf(rest, "%f %f %f %f", &window_rect[0], &window_rect[1], &window_rect[2], &window_rect[3]);
        return true;
    }
    if (!strcmp(key, "editor_library_favourites")) {
        favourites = parse_favourites(rest);
        return true;
    }
    return false;
}
void rood_library::write_settings(FILE* f)
{
    fprintf(f, "editor_library_view %s\n", grid_view ? "grid" : "list");
    fprintf(f, "editor_library_docked %d\n", in_column ? 1 : 0);
    if (window_rect[2] > 0)
        fprintf(f, "editor_library_window %.0f %.0f %.0f %.0f\n", window_rect[0], window_rect[1], window_rect[2], window_rect[3]);
    std::string line = favourites_line(favourites);
    if (!line.empty())
        fprintf(f, "%s\n", line.c_str());
}

/* ---- thumbnails ---- */

namespace {
/* Thumbnails: rendered (or read from the cache) on demand, at most two
 * renders a frame, packed into one ImTextureData that the GL and Vulkan
 * backends upload themselves (as prompt_glyphs.cpp). 10 x 10 slots of 100
 * pixels (96 and a 2-pixel margin); the least recently drawn slot goes when full. */
const int ATLAS = 1024, SLOT = 100, PER_ROW = ATLAS / SLOT, MAX_RENDERS = 2;
/* Cache reads (or a miss's look for the file) a frame: only tiles on screen ask (tile()), so a long
 * library scrolled through refills its slots a few at a time, never every tile every frame. */
const int MAX_LOADS = 8;
ImTextureData* atlas;
ImGuiContext* atlas_owner;
struct Slot {
    std::string item, hash;
    int frame = -1;
};
Slot slots[PER_ROW * PER_ROW];
std::map<std::string, int> slot_of;
int render_frame = -1, renders, loads;
unsigned look_serial = 1; /* bumps when RPC changes the look (every hash changes) */
struct Hashed {
    unsigned serial = 0, look = 0;
    std::string hash;
    bool scene = false;
};
std::map<std::string, Hashed> hashes;

unsigned source_serial(const Source* s) { return s && s->serial ? s->serial() : 0; }
/* The item's scene and its hash (cached while its source and the look stay the same). */
bool scene_of(const Entry& e, rood_thumbs::Scene* scene, std::string* hash)
{
    const Source* src = source_of(e.kind);
    if (!src || !src->scene)
        return false;
    Hashed& h = hashes[e.item];
    unsigned serial = source_serial(src);
    if (!scene && h.serial == serial && h.look == look_serial && !h.hash.empty()) {
        *hash = h.hash;
        return h.scene;
    }
    rood_thumbs::Scene s;
    h.scene = src->scene(e, s);
    h.serial = serial, h.look = look_serial;
    h.hash = h.scene ? rood_thumbs::hash(s, rood_thumbs::SIZE) : std::string();
    *hash = h.hash;
    if (scene)
        *scene = s;
    return h.scene;
}
std::string thumb_path(const Entry& e, const std::string& hash)
{
    return rood_thumbs::cache_dir() + "/" + rood_thumbs::file_name(e.kind, e.id, hash);
}
/* The thumbnail's pixels: from the cache, else rendered (and cached) when `may_render`. */
bool pixels_of(const Entry& e, bool may_render, std::vector<uint8_t>& px, std::string* hash, bool* cached)
{
    rood_thumbs::Scene s;
    if (!scene_of(e, nullptr, hash))
        return false;
    std::string path = thumb_path(e, *hash);
    if (rood_thumbs::load(path, rood_thumbs::SIZE, px)) {
        *cached = true;
        return true;
    }
    if (!may_render)
        return false;
    scene_of(e, &s, hash);
    rood_thumbs::render(s, rood_thumbs::SIZE, px);
    std::string error;
    if (!rood_thumbs::save(path, rood_thumbs::SIZE, px, &error))
        fprintf(stderr, "[library] %s\n", error.c_str());
    *cached = false;
    return true;
}
bool ensure_atlas()
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context)
        return false;
    if (!atlas || atlas_owner != context) {
        /* A new context (the overlay made again): the old one is gone with its backend's texture;
         * its pixels are ours to free. */
        if (atlas)
            IM_DELETE(atlas);
        atlas = IM_NEW(ImTextureData)();
        atlas->Create(ImTextureFormat_RGBA32, ATLAS, ATLAS);
        memset(atlas->Pixels, 0, (size_t)atlas->GetSizeInBytes());
        ImGui::RegisterUserTexture(atlas);
        atlas_owner = context;
        slot_of.clear();
        for (Slot& s : slots)
            s = Slot();
    }
    return true;
}
/* Where the item's thumbnail is in the atlas this frame (uv0, uv1); false while it is not there yet. */
bool thumb_uv(const Entry& e, ImVec2* uv0, ImVec2* uv1)
{
    if (!ensure_atlas())
        return false;
    int now = ImGui::GetFrameCount();
    if (render_frame != now)
        render_frame = now, renders = 0, loads = 0;
    std::string hash;
    if (!scene_of(e, nullptr, &hash))
        return false;
    auto it = slot_of.find(e.item);
    int slot = it == slot_of.end() ? -1 : it->second;
    if (slot < 0 || slots[slot].hash != hash) {
        if (loads >= MAX_LOADS)
            return false; /* next frame */
        loads++;
        std::vector<uint8_t> px;
        bool cached = false;
        if (!pixels_of(e, renders < MAX_RENDERS, px, &hash, &cached))
            return false;
        if (!cached)
            renders++;
        if (slot < 0) { /* a free slot, else the least recently drawn */
            int best = 0;
            for (int k = 0; k < PER_ROW * PER_ROW; k++) {
                if (slots[k].item.empty()) {
                    best = k;
                    break;
                }
                if (slots[k].frame < slots[best].frame)
                    best = k;
            }
            slot = best;
            if (!slots[slot].item.empty())
                slot_of.erase(slots[slot].item);
        }
        int x = (slot % PER_ROW) * SLOT + 2, y = (slot / PER_ROW) * SLOT + 2, n = rood_thumbs::SIZE;
        for (int row = 0; row < n; row++)
            memcpy(atlas->GetPixelsAt(x, y + row), &px[(size_t)row * n * 4], (size_t)n * 4);
        if (atlas->Status != ImTextureStatus_WantCreate)
            ImTextureDataQueueUpload(atlas, x, y, n, n);
        slots[slot].item = e.item, slots[slot].hash = hash;
        slot_of[e.item] = slot;
    }
    slots[slot].frame = now;
    int x = (slot % PER_ROW) * SLOT + 2, y = (slot / PER_ROW) * SLOT + 2;
    *uv0 = ImVec2((float)x / ATLAS, (float)y / ATLAS);
    *uv1 = ImVec2((float)(x + rood_thumbs::SIZE) / ATLAS, (float)(y + rood_thumbs::SIZE) / ATLAS);
    return true;
}
} // namespace

/* ---- the panel ---- */

namespace {
/* As last drawn (RPC): each shown entry's tile or row, the chips and buttons, the floating window. */
int drawn_frame = -100;
std::map<std::string, ImVec4> tile_rects, ui_rects;
std::set<std::string> thumbs_shown;
float window_drawn[4];
int window_frame = -100;
/* The right-click menu's Rename... or Delete..., opened at the panel's level after the items. */
enum Pending { PENDING_NONE, PENDING_RENAME, PENDING_DELETE };
Pending pending = PENDING_NONE;
Entry pending_entry;
std::string menu_item; /* the item whose menu was last drawn ("" none open) */
int menu_frame = -100;

void keep(std::map<std::string, ImVec4>& into, const std::string& name)
{
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    into[name] = ImVec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
}
/* A chip (wrapping to the next line when the row is full); true when clicked. */
bool chip(const std::string& text, bool on, const std::string& name, bool* first)
{
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float w = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    if (!*first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < right)
        ImGui::SameLine();
    *first = false;
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    bool clicked = ImGui::SmallButton((text + "##" + name).c_str());
    if (on)
        ImGui::PopStyleColor();
    keep(ui_rects, name);
    return clicked;
}
/* A kind's type badge: a disc in the kind's tint with a glyph no item icon uses (a bulb for basic
 * lights, stacked boxes for prefabs, a wall bracket for fixtures, a cloud for fog, sparks for
 * emitters), for scanning the grid by kind. */
void type_badge(ImDrawList* d, ImVec2 c, float r, const std::string& kind)
{
    ImU32 tint = kind == "light"     ? IM_COL32(200, 150, 40, 235)
        : kind == "prefab"           ? IM_COL32(60, 120, 210, 235)
        : kind == "fixture"          ? IM_COL32(150, 90, 50, 235)
        : kind == "fog"              ? IM_COL32(90, 140, 150, 235)
        : kind == "emitter"          ? IM_COL32(190, 70, 120, 235)
                                     : IM_COL32(90, 90, 100, 235);
    ImU32 ink = IM_COL32(255, 255, 255, 240);
    float t = fmaxf(r * 0.12f, 1.0f), g = r * 0.62f; /* line width; the glyph's half size */
    d->AddCircleFilled(c, r, tint, 20);
    d->AddCircle(c, r, IM_COL32(10, 10, 14, 230), 20, 1.0f);
    if (kind == "light") { /* a bulb: a globe on a screw base */
        d->AddCircle(ImVec2(c.x, c.y - g * 0.25f), g * 0.55f, ink, 14, t);
        d->AddLine(ImVec2(c.x - g * 0.28f, c.y + g * 0.45f), ImVec2(c.x + g * 0.28f, c.y + g * 0.45f), ink, t);
        d->AddLine(ImVec2(c.x - g * 0.22f, c.y + g * 0.7f), ImVec2(c.x + g * 0.22f, c.y + g * 0.7f), ink, t);
    } else if (kind == "prefab") { /* stacked boxes */
        d->AddRect(ImVec2(c.x - g * 0.8f, c.y - g * 0.1f), ImVec2(c.x + g * 0.1f, c.y + g * 0.8f), ink, 0, 0, t);
        d->AddRect(ImVec2(c.x - g * 0.1f, c.y - g * 0.8f), ImVec2(c.x + g * 0.8f, c.y + g * 0.1f), ink, 0, 0, t);
    } else if (kind == "fixture") { /* a bracket on a wall plate */
        d->AddLine(ImVec2(c.x - g * 0.7f, c.y - g * 0.8f), ImVec2(c.x - g * 0.7f, c.y + g * 0.8f), ink, t * 1.5f);
        d->AddLine(ImVec2(c.x - g * 0.7f, c.y + g * 0.2f), ImVec2(c.x + g * 0.2f, c.y + g * 0.2f), ink, t);
        d->AddLine(ImVec2(c.x + g * 0.2f, c.y + g * 0.2f), ImVec2(c.x + g * 0.2f, c.y - g * 0.35f), ink, t);
        d->AddRectFilled(ImVec2(c.x - g * 0.05f, c.y - g * 0.75f), ImVec2(c.x + g * 0.45f, c.y - g * 0.35f), ink);
    } else if (kind == "fog") { /* a cloud */
        d->AddCircleFilled(ImVec2(c.x - g * 0.4f, c.y + g * 0.15f), g * 0.38f, ink, 12);
        d->AddCircleFilled(ImVec2(c.x + g * 0.05f, c.y - g * 0.15f), g * 0.5f, ink, 12);
        d->AddCircleFilled(ImVec2(c.x + g * 0.5f, c.y + g * 0.2f), g * 0.33f, ink, 12);
        d->AddRectFilled(ImVec2(c.x - g * 0.4f, c.y + g * 0.15f), ImVec2(c.x + g * 0.5f, c.y + g * 0.53f), ink);
    } else if (kind == "emitter") { /* sparks: rays from a point */
        for (int k = 0; k < 6; k++) {
            float a = k * 1.0471976f + 0.3f;
            d->AddLine(ImVec2(c.x + cosf(a) * g * 0.25f, c.y + sinf(a) * g * 0.25f),
                ImVec2(c.x + cosf(a) * g * 0.85f, c.y + sinf(a) * g * 0.85f), ink, t);
        }
        d->AddCircleFilled(c, g * 0.16f, ink, 8);
    } else {
        d->AddCircleFilled(c, g * 0.3f, ink, 10);
    }
}
void star(ImDrawList* d, ImVec2 c, float r, bool on)
{
    ImVec2 p[10];
    for (int k = 0; k < 10; k++) {
        float a = -1.5707963f + k * 3.14159265f / 5, rr = k & 1 ? r * 0.45f : r;
        p[k] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    if (on)
        for (int k = 0; k < 10; k += 2)
            d->AddTriangleFilled(c, p[k], p[(k + 1) % 10], IM_COL32(255, 210, 60, 255)),
                d->AddTriangleFilled(c, p[(k + 9) % 10], p[k], IM_COL32(255, 210, 60, 255));
    d->AddPolyline(p, 10, on ? IM_COL32(255, 210, 60, 255) : IM_COL32(200, 200, 200, 200), ImDrawFlags_Closed, 1.2f);
}
/* The tile's or row's right-click menu: Edit prefab (a prefab, or a fixture's), favourite; for one of
 * yours Rename..., Delete... (their dialogs open at the panel's level) and the file's path. */
void item_menu(const Entry& e)
{
    if (!ImGui::BeginPopupContextItem("##item menu"))
        return;
    menu_item = e.item, menu_frame = ImGui::GetFrameCount();
    std::string message;
    const prefab::Prefab* p = rood_library::item_prefab(e.item);
    if (ImGui::MenuItem("Edit prefab", nullptr, false, p != nullptr)) {
        rood_library::edit_prefab(e.item, &message);
        snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
    }
    keep(ui_rects, "menu:edit_prefab");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(p ? "Its prefab's definition in the prefab editor (a built-in one saves your copy)"
                            : "Not a prefab");
    bool fav = favourites.count(e.item) != 0;
    if (ImGui::MenuItem("Favourite", nullptr, fav)) {
        if (!favourites.erase(e.item))
            favourites.insert(e.item);
    }
    keep(ui_rects, "menu:favourite");
    const Source* s = source_of(e.kind);
    std::string file = s && s->file ? s->file(e) : std::string();
    ImGui::Separator();
    if (ImGui::MenuItem("Rename...", nullptr, false, e.mine && s && s->rename))
        pending = PENDING_RENAME, pending_entry = e;
    keep(ui_rects, "menu:rename");
    if (ImGui::MenuItem(p && p->overrides_builtin ? "Revert to built-in..." : "Delete...", nullptr, false,
            e.mine && s && s->remove))
        pending = PENDING_DELETE, pending_entry = e;
    keep(ui_rects, "menu:delete");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(e.mine ? "Its file is moved aside (<file>.bak), not erased" : "Only yours (Mine) can be deleted");
    if (ImGui::MenuItem("Copy file path", nullptr, false, !file.empty())) {
        ImGui::SetClipboardText(file.c_str());
        snprintf(rood_lights::status, sizeof rood_lights::status, "Copied %s", file.c_str());
    }
    keep(ui_rects, "menu:file");
    if (!file.empty())
        ImGui::TextDisabled("%s", file.c_str());
    ImGui::EndPopup();
}
/* The tile's or row's clicks: current item (Ctrl+click in the view adds it), double-click at the view's
 * centre, a drag into the view, the favourite star; its tooltip. */
void item_actions(const Entry& e, bool pressed, const ImVec4& star_rect)
{
    ImGuiIO& io = ImGui::GetIO();
    float em = ImGui::GetFontSize();
    bool on_star = io.MousePos.x >= star_rect.x && io.MousePos.y >= star_rect.y && io.MousePos.x < star_rect.x + star_rect.z
        && io.MousePos.y < star_rect.y + star_rect.w;
    if (pressed && on_star) {
        if (!favourites.erase(e.item))
            favourites.insert(e.item);
        return;
    }
    if (pressed) {
        rood_lights::place_item = e.item;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            rood_lights::place_at_centre(e.item);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, em * 0.4f) && !rood_lights::place_drag_active())
        rood_lights::place_drag_begin(e.item, false, io.MousePos.x, io.MousePos.y);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !rood_lights::place_drag_active())
        ImGui::SetTooltip("%s  (%s, %s)\n%s%s%s\n\nDrag into the view to place it on the surface there; double-click: at the "
                          "view's centre;\nCtrl+click in the view adds it. The star: a favourite. Right-click: more.",
            e.name.c_str(), rood_library::kind_label(e.kind).c_str(), e.category.c_str(), e.description.c_str(),
            e.error.empty() ? "" : "\nProblem: ", e.error.c_str());
    item_menu(e);
}
void tile(const Entry& e, float side)
{
    ImGui::PushID(e.item.c_str());
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 lo = ImGui::GetCursorScreenPos(), hi(lo.x + side, lo.y + side);
    float line = ImGui::GetTextLineHeight(), em = ImGui::GetFontSize();
    bool pressed = ImGui::Selectable("##tile", rood_lights::place_item == e.item, ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(side, side + line + 4));
    keep(tile_rects, e.item);
    ImVec4 star_rect(hi.x - em * 1.1f, lo.y, em * 1.1f, em * 1.1f);
    item_actions(e, pressed, star_rect);
    if (!ImGui::IsRectVisible(lo, hi)) { /* scrolled out of view: no thumbnail asked for, nothing drawn */
        ImGui::PopID();
        return;
    }
    ImVec2 uv0, uv1;
    if (thumb_uv(e, &uv0, &uv1)) {
        d->AddImage(atlas->GetTexRef(), lo, hi, uv0, uv1);
        thumbs_shown.insert(e.item);
    } else {
        float c[3] = { 1, 0.85f, 0.6f };
        d->AddRectFilled(lo, hi, IM_COL32(28, 28, 34, 255), 4);
        rood_lights::draw_icon(d, (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, side * 0.5f, e.item, c, 1);
    }
    if (!e.error.empty())
        d->AddRect(lo, hi, IM_COL32(255, 90, 70, 255), 2.0f, ImDrawFlags_None, 2.0f);
    /* The type icon, bottom left: what kind of thing it is at a glance. */
    float b = side * 0.24f;
    ImVec2 bc(lo.x + b * 0.65f, hi.y - b * 0.65f);
    type_badge(d, bc, b * 0.58f, e.kind);
    bool fav = favourites.count(e.item) != 0;
    if (fav || ImGui::IsItemHovered())
        star(d, ImVec2(star_rect.x + star_rect.z * 0.5f, star_rect.y + star_rect.w * 0.5f), em * 0.42f, fav);
    std::string name = e.name;
    while (name.size() > 1 && ImGui::CalcTextSize(name.c_str()).x > side)
        name.pop_back();
    d->AddText(ImVec2(lo.x + 2, hi.y + 2), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    ImGui::PopID();
}
void row(const Entry& e)
{
    ImGui::PushID(e.item.c_str());
    float em = ImGui::GetFontSize(), h = em * 1.8f;
    ImVec2 at = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::Selectable("##row", rood_lights::place_item == e.item, ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(0, h));
    keep(tile_rects, e.item);
    float right = ImGui::GetItemRectMax().x;
    ImVec4 star_rect(right - em * 1.2f, at.y + (h - em) * 0.5f, em * 1.1f, em * 1.1f);
    item_actions(e, pressed, star_rect);
    ImDrawList* d = ImGui::GetWindowDrawList();
    float c[3] = { 1, 0.85f, 0.6f };
    rood_lights::draw_icon(d, at.x + h * 0.5f, at.y + h * 0.5f, h * 0.7f, e.item, c, 1);
    d->AddText(ImVec2(at.x + h + em * 0.3f, at.y + h * 0.5f - em * 0.55f), ImGui::GetColorU32(ImGuiCol_Text), e.name.c_str());
    std::string kind = rood_library::kind_label(e.kind);
    float kw = ImGui::CalcTextSize(kind.c_str()).x;
    d->AddText(ImVec2(right - em * 1.6f - kw, at.y + h * 0.5f - em * 0.55f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
        kind.c_str());
    type_badge(d, ImVec2(right - em * 2.3f - kw, at.y + h * 0.5f), em * 0.45f, e.kind);
    star(d, ImVec2(star_rect.x + star_rect.z * 0.5f, star_rect.y + star_rect.w * 0.5f), em * 0.42f, favourites.count(e.item) != 0);
    ImGui::PopID();
}
} // namespace

namespace {
/* Rename... and Delete... from an item's menu: modal, at the panel's level. */
void item_dialogs()
{
    static char new_name[64];
    if (pending == PENDING_RENAME) {
        snprintf(new_name, sizeof new_name, "%s", pending_entry.name.c_str());
        ImGui::OpenPopup("Rename##library");
    } else if (pending == PENDING_DELETE) {
        ImGui::OpenPopup("Delete##library");
    }
    pending = PENDING_NONE;
    std::string message;
    if (ImGui::BeginPopupModal("Rename##library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("Name", new_name, sizeof new_name, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled("Its id stays %s (rooms that use it keep it)", pending_entry.id.c_str());
        if ((ImGui::Button("Rename") || enter) && new_name[0]) {
            rood_library::rename(pending_entry.item, new_name, &message);
            snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
            ImGui::CloseCurrentPopup();
        }
        keep(ui_rects, "rename_ok");
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        keep(ui_rects, "rename_cancel");
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete##library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string file = rood_library::file_of(pending_entry.item);
        ImGui::Text("Delete %s?", pending_entry.name.c_str());
        ImGui::TextDisabled("Its file is moved aside, not erased:\n%s -> .bak", file.c_str());
        if (pending_entry.kind == "prefab") {
            int using_it = 0;
            for (const prefab::Actor& a : rood_lights::actors)
                using_it += a.prefab == pending_entry.id;
            if (using_it)
                ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%d actor%s in this room use%s it", using_it,
                    using_it == 1 ? "" : "s", using_it == 1 ? "s" : "");
        } else if (pending_entry.kind == "fog") {
            if (int n = rood_atmo::volumes_using(pending_entry.id))
                ImGui::TextDisabled("%d fog volume%s here use%s it: %s keep%s its look", n, n == 1 ? "" : "s",
                    n == 1 ? "s" : "", n == 1 ? "it" : "they", n == 1 ? "s" : "");
        }
        if (ImGui::Button("Delete")) {
            rood_library::remove(pending_entry.item, &message);
            snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
            ImGui::CloseCurrentPopup();
        }
        keep(ui_rects, "delete_yes");
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        keep(ui_rects, "delete_cancel");
        ImGui::EndPopup();
    }
}
} // namespace

namespace {
/* The thumbnail scene's look (rood_thumbs::look_fields): sliders, Save (its file), Reload. */
void thumbnails_popup()
{
    if (!ImGui::BeginPopup("library thumbnails"))
        return;
    rood_thumbs::Look& lk = rood_thumbs::look();
    bool changed = false;
    for (const rood_thumbs::LookField& f : rood_thumbs::look_fields()) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
        changed |= ImGui::SliderFloat(f.label, &(lk.*f.value), f.min, f.max, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    changed |= ImGui::ColorEdit3("Background", lk.background, ImGuiColorEditFlags_Float);
    if (changed)
        ++look_serial; /* every hash changes: rendered again, a few a frame */
    static std::string message;
    if (ImGui::SmallButton("Save")) {
        std::string error;
        message = rood_thumbs::save_look(&error) ? "Saved " + rood_thumbs::look_path() : error;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload")) {
        std::string error;
        message = rood_thumbs::load_look(&error) ? "Read " + rood_thumbs::look_path() : error;
        ++look_serial;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Defaults")) {
        lk = rood_thumbs::Look();
        ++look_serial;
    }
    ImGui::TextDisabled("%s", message.empty() ? rood_thumbs::look_path().c_str() : message.c_str());
    ImGui::EndPopup();
}
} // namespace

void rood_library::panel()
{
    float em = ImGui::GetFontSize();
    drawn_frame = ImGui::GetFrameCount();
    tile_rects.clear(), ui_rects.clear(), thumbs_shown.clear();
    const std::vector<Entry>& all = entries();
    static char search[64];
    static std::string synced;
    if (synced != filter.search) /* the RPC changed it */
        snprintf(search, sizeof search, "%s", filter.search.c_str()), synced = filter.search;
    ImGui::SetNextItemWidth(-em * 11);
    if (ImGui::InputTextWithHint("##libsearch", "Search the library", search, sizeof search))
        filter.search = synced = search;
    ImGui::SameLine();
    if (ImGui::SmallButton(grid_view ? "List" : "Grid"))
        grid_view = !grid_view;
    keep(ui_rects, "view");
    ImGui::SameLine();
    if (ImGui::SmallButton(in_column ? "Pop out" : "Dock"))
        in_column = !in_column;
    keep(ui_rects, "dock");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(in_column ? "Float the library in its own window" : "Back into the side column (beside the Outliner)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Thumbnails..."))
        ImGui::OpenPopup("library thumbnails");
    keep(ui_rects, "thumbnails");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("The thumbnails' neutral scene: its albedos, light, exposure, field of view");
    thumbnails_popup();
    ImGui::SameLine();
    ImGui::BeginDisabled(rood_selection::empty());
    if (ImGui::SmallButton("Save..."))
        request_save();
    ImGui::EndDisabled();
    keep(ui_rects, "save");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Save as prefab: the selected lights and light actors (one light prefab), one light actor\n"
                          "(a prefab with its values) or one fog volume (a fog preset), into your library (Mine).\n"
                          "Also in the Outliner's right-click menu and Edit.");
    /* Kinds, then the kind's categories, Favourites and Mine. */
    bool first = true;
    if (chip("All", filter.kind.empty(), "chip:all", &first))
        filter.kind.clear(), filter.category.clear();
    for (const std::string& k : kinds_present(all))
        if (chip(kind_label(k), filter.kind == k, "chip:" + k, &first))
            filter.kind = filter.kind == k ? std::string() : k, filter.category.clear();
    first = true;
    for (const std::string& c : categories(all, filter.kind))
        if (chip(c, filter.category == c, "category:" + c, &first))
            filter.category = filter.category == c ? std::string() : c;
    if (chip("Favourites", filter.favourites, "favourites", &first))
        filter.favourites = !filter.favourites;
    if (chip("Mine", filter.mine, "mine", &first))
        filter.mine = !filter.mine;
    ImGui::Separator();
    /* A copy of what is shown: the Save popup above, drawing an icon (a fixture's
     * prefab), a click or a drop may rebuild the entries partway through. */
    std::vector<Entry> list;
    size_t total = 0;
    {
        const std::vector<Entry>& now = entries(); /* the Save popup may have reloaded a source */
        total = now.size();
        for (const Entry* e : shown(now, filter, favourites))
            list.push_back(*e);
    }
    ImGui::BeginChild("##library items", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.2f));
    if (grid_view) {
        float side = em * 5, gap = ImGui::GetStyle().ItemSpacing.x;
        int columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (side + gap)));
        for (size_t i = 0; i < list.size(); i++) {
            if (i % columns)
                ImGui::SameLine();
            tile(list[i], side);
        }
    } else {
        for (const Entry& e : list)
            row(e);
    }
    if (list.empty())
        ImGui::TextDisabled(total == 0 ? "Nothing in the library" : "Nothing matches");
    ImGui::EndChild();
    item_dialogs();
    if (const prefab::Prefab* p = rood_library::item_prefab(rood_lights::place_item)) {
        if (ImGui::SmallButton("Edit prefab")) {
            std::string message;
            rood_library::edit_prefab(rood_lights::place_item, &message);
            snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
        }
        keep(ui_rects, "edit_prefab");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The prefab editor on %s (the current item's prefab)", p->id.c_str());
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%zu of %zu. Ctrl+click in the view adds: %s", list.size(), total,
        rood_lights::item_label(rood_lights::place_item).c_str());
}
bool rood_library::docked() { return in_column; }
namespace {
bool save_requested;
int save_frame = -100;
ImVec4 save_item_rect; /* a menu's "Save as prefab..." as last drawn (RPC: the Outliner's right-click menu) */
int save_item_frame = -100;
/* Save as prefab's dialog (the Library's Save..., the Outliner's menu, Edit, a fog volume's Details): modal,
 * at the overlay's top level so any of them can open it. */
void save_dialog()
{
    static char save_name[64];
    if (save_requested) {
        save_requested = false;
        save_name[0] = 0;
        ImGui::OpenPopup("Save as prefab##library");
    }
    if (!ImGui::BeginPopupModal("Save as prefab##library", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    save_frame = ImGui::GetFrameCount();
    size_t n = rood_selection::items().size();
    ImGui::Text("Save the selection (%zu object%s) into your library (Mine)", n, n == 1 ? "" : "s");
    ImGui::TextDisabled("Lights and light actors: a light prefab (they become one actor of it);\n"
                        "one light actor: a prefab with its values; one fog volume: a fog preset.");
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputTextWithHint("##name", "Name", save_name, sizeof save_name, ImGuiInputTextFlags_EnterReturnsTrue);
    keep(ui_rects, "save_name");
    if ((ImGui::Button("Save") || enter) && save_name[0]) {
        std::string message;
        rood_library::save_selection(save_name, &message);
        snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
        save_name[0] = 0;
        ImGui::CloseCurrentPopup();
    }
    keep(ui_rects, "save_ok");
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    keep(ui_rects, "save_cancel");
    ImGui::EndPopup();
}
} // namespace
void rood_library::request_save() { save_requested = true; }
bool rood_library::save_menu_item()
{
    bool any = !rood_selection::empty();
    bool picked = ImGui::MenuItem("Save as prefab...", nullptr, false, any);
    save_item_rect = ImVec4(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectSize().x,
        ImGui::GetItemRectSize().y);
    save_item_frame = ImGui::GetFrameCount();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(any ? "The selection into your library (Mine): lights and light actors as a light prefab,\n"
                                "one fog volume as a fog preset"
                              : "Select something first");
    if (picked)
        request_save();
    return picked;
}
void rood_library::window()
{
    save_dialog();
    if (in_column)
        return;
    ImGuiIO& io = ImGui::GetIO();
    float em = ImGui::GetFontSize();
    if (window_rect[2] <= 0)
        window_rect[0] = io.DisplaySize.x * 0.3f, window_rect[1] = io.DisplaySize.y * 0.15f, window_rect[2] = em * 28,
        window_rect[3] = em * 30;
    ImGui::SetNextWindowPos(ImVec2(window_rect[0], window_rect[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(window_rect[2], window_rect[3]), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin("Library##floating", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        panel();
        ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        window_rect[0] = window_drawn[0] = p.x, window_rect[1] = window_drawn[1] = p.y;
        window_rect[2] = window_drawn[2] = s.x, window_rect[3] = window_drawn[3] = s.y;
        window_frame = ImGui::GetFrameCount();
    }
    ImGui::End();
    if (!open) /* its close box docks it back */
        in_column = true;
}

/* ---- RPC editor_library ---- */

namespace {
std::string q(const std::string& s) { return prefab::json_quote(s); }
std::string entry_json(const Entry& e)
{
    bool now = ImGui::GetCurrentContext() && drawn_frame >= ImGui::GetFrameCount() - 1;
    auto it = tile_rects.find(e.item);
    char rect[96] = "null";
    if (now && it != tile_rects.end())
        snprintf(rect, sizeof rect, "[%.1f,%.1f,%.1f,%.1f]", it->second.x, it->second.y, it->second.z, it->second.w);
    return "{\"item\":" + q(e.item) + ",\"kind\":" + q(e.kind) + ",\"id\":" + q(e.id) + ",\"name\":" + q(e.name)
        + ",\"category\":" + q(e.category) + ",\"mine\":" + (e.mine ? "true" : "false") + ",\"favourite\":"
        + (favourites.count(e.item) ? "true" : "false") + ",\"error\":" + q(e.error) + ",\"tile\":" + rect + ",\"thumb\":"
        + (now && thumbs_shown.count(e.item) ? "true" : "false") + "}";
}
std::string list_json()
{
    const std::vector<Entry>& all = rood_library::entries();
    std::string s = "{\"sources\":[";
    std::vector<std::string> kinds = rood_library::source_kinds();
    for (size_t i = 0; i < kinds.size(); i++)
        s += (i ? "," : "") + q(kinds[i]);
    s += "],\"kinds\":[";
    kinds = rood_library::kinds_present(all);
    for (size_t i = 0; i < kinds.size(); i++)
        s += (i ? "," : "") + q(kinds[i]);
    s += "],\"categories\":[";
    std::vector<std::string> cats = rood_library::categories(all, filter.kind);
    for (size_t i = 0; i < cats.size(); i++)
        s += (i ? "," : "") + q(cats[i]);
    s += "],\"filter\":{\"kind\":" + q(filter.kind) + ",\"category\":" + q(filter.category) + ",\"search\":" + q(filter.search)
        + ",\"favourites\":" + (filter.favourites ? "true" : "false") + ",\"mine\":" + (filter.mine ? "true" : "false")
        + "},\"view\":" + q(grid_view ? "grid" : "list") + ",\"docked\":" + (in_column ? "true" : "false") + ",\"favourites\":[";
    bool first = true;
    for (const std::string& f : favourites)
        s += (first ? "" : ",") + q(f), first = false;
    s += "],\"count\":" + std::to_string(all.size()) + ",\"entries\":[";
    first = true;
    for (const Entry* e : rood_library::shown(all, filter, favourites))
        s += (first ? "" : ",") + entry_json(*e), first = false;
    bool now = ImGui::GetCurrentContext() && drawn_frame >= ImGui::GetFrameCount() - 1;
    s += "],\"drawn\":" + std::string(now ? "true" : "false") + ",\"rects\":{";
    bool first_rect = true;
    for (const auto& kv : ui_rects) {
        if (!now)
            break;
        char r[96];
        snprintf(r, sizeof r, "[%.1f,%.1f,%.1f,%.1f]", kv.second.x, kv.second.y, kv.second.z, kv.second.w);
        s += (first_rect ? "" : ",") + q(kv.first) + ":" + r;
        first_rect = false;
    }
    char w[96] = "null";
    if (ImGui::GetCurrentContext() && window_frame >= ImGui::GetFrameCount() - 1 && !in_column)
        snprintf(w, sizeof w, "[%.1f,%.1f,%.1f,%.1f]", window_drawn[0], window_drawn[1], window_drawn[2], window_drawn[3]);
    bool menu_now = ImGui::GetCurrentContext() && menu_frame >= ImGui::GetFrameCount() - 1;
    bool saving = ImGui::GetCurrentContext() && save_frame >= ImGui::GetFrameCount() - 1;
    char save_item[96] = "null";
    if (ImGui::GetCurrentContext() && save_item_frame >= ImGui::GetFrameCount() - 1)
        snprintf(save_item, sizeof save_item, "[%.1f,%.1f,%.1f,%.1f]", save_item_rect.x, save_item_rect.y, save_item_rect.z,
            save_item_rect.w);
    return s + "},\"window\":" + w + ",\"save_dialog\":" + (saving ? "true" : "false") + ",\"save_item\":" + save_item + ",\"menu\":" + q(menu_now ? menu_item : std::string()) + ",\"editing_prefab\":"
        + q(rood_lights::editing_prefab_id()) + "}";
}
bool json_bool(const Json* j, bool fallback) { return j ? (j->type == Json::Bool ? j->boolean : j->number != 0) : fallback; }
std::string lighting_number(float v)
{
    char t[32];
    snprintf(t, sizeof t, "%.6g", v);
    return t;
}
} // namespace

/* RPC editor_library (dev_rpc.c hands over the params object's text): action
 * list (default): the sources, the kinds present, the filter's categories,
 * the filter, view, docked, favourites and the entries it shows; filter
 * {kind, category, search, favourites, mine} (the keys given change); favourite
 * {item, on}; view {mode "grid" | "list", docked}; edit_prefab {item} (the
 * prefab editor on its prefab; item "" closes it); rename {item, name} and delete {item} (one
 * of yours: its display name; its file moved aside to .bak); file {item}
 * (its file, "" when not yours); floor {x, z} (the level floor area there a
 * fog volume's drop fits into: lo, hi (x, z), height; null none); thumbs {items (default: the
 * entries shown), look {floor, wall, grid, ambient, fog_ambient, exposure,
 * fov, background}, reload_look, save_look (pc/assets/editor/library_thumbs.json)}:
 * renders (or reads) their thumbnails now and replies their files and the look.
 * The others reply as list, with the panel's drawn tiles, chips and window,
 * the item whose right-click menu is open and the prefab editor's prefab. */
extern "C" int pc_editor_library_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_scene::frame();
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "list";
    if (action == "filter") {
        if (const Json* j = params.get("kind"))
            filter.kind = j->string;
        if (const Json* j = params.get("category"))
            filter.category = j->string;
        if (const Json* j = params.get("search"))
            filter.search = j->string;
        filter.favourites = json_bool(params.get("favourites"), filter.favourites);
        filter.mine = json_bool(params.get("mine"), filter.mine);
    } else if (action == "favourite") {
        const Json* item = params.get("item");
        if (!item || item->type != Json::String || !rood_library::find(item->string)) {
            snprintf(out, size, "favourite needs item (a library item)");
            return -1;
        }
        if (json_bool(params.get("on"), true))
            favourites.insert(item->string);
        else
            favourites.erase(item->string);
    } else if (action == "view") {
        if (const Json* m = params.get("mode"))
            grid_view = m->string != "list";
        in_column = json_bool(params.get("docked"), in_column);
    } else if (action == "thumbs") { /* render (or read) thumbnails now; look {field: value} changes the neutral scene */
        rood_thumbs::Look& lk = rood_thumbs::look();
        if (const Json* look = params.get("look")) {
            for (const rood_thumbs::LookField& f : rood_thumbs::look_fields())
                if (const Json* v = look->get(f.key))
                    lk.*f.value = fminf(fmaxf((float)v->number, f.min), f.max);
            if (const Json* b = look->get("background"))
                for (size_t k = 0; k < 3 && k < b->items.size(); k++)
                    lk.background[k] = fminf(fmaxf((float)b->items[k].number, 0.0f), 1.0f);
            ++look_serial;
        }
        if (json_bool(params.get("reload_look"), false)) {
            std::string error;
            if (!rood_thumbs::load_look(&error)) {
                snprintf(out, size, "%s", error.c_str());
                return -1;
            }
            ++look_serial;
        }
        if (json_bool(params.get("save_look"), false)) {
            std::string error;
            if (!rood_thumbs::save_look(&error)) {
                snprintf(out, size, "%s", error.c_str());
                return -1;
            }
        }
        std::vector<std::string> wanted;
        if (const Json* list = params.get("items"))
            for (const Json& i : list->items)
                wanted.push_back(i.string);
        else
            for (const Entry* e : rood_library::shown(rood_library::entries(), filter, favourites))
                wanted.push_back(e->item);
        std::string text = "{\"dir\":" + q(rood_thumbs::cache_dir()) + ",\"look_file\":" + q(rood_thumbs::look_path())
            + ",\"look\":{";
        for (const rood_thumbs::LookField& f : rood_thumbs::look_fields())
            text += q(f.key) + ":" + lighting_number(lk.*f.value) + ",";
        text += "\"background\":[" + lighting_number(lk.background[0]) + "," + lighting_number(lk.background[1]) + ","
            + lighting_number(lk.background[2]) + "]},\"thumbs\":[";
        for (size_t i = 0; i < wanted.size(); i++) {
            const Entry* found = rood_library::find(wanted[i]);
            Entry e = found ? *found : Entry(); /* a copy: rendering may rebuild the entries */
            std::vector<uint8_t> px;
            std::string hash;
            bool cached = false, ok = found && pixels_of(e, true, px, &hash, &cached);
            text += std::string(i ? "," : "") + "{\"item\":" + q(wanted[i]) + ",\"file\":"
                + (ok ? q(thumb_path(e, hash)) : std::string("null")) + ",\"hash\":" + q(hash) + ",\"cached\":"
                + (cached ? "true" : "false") + "}";
        }
        text += "]}";
        snprintf(out, size, "%s", text.c_str());
        return (int)text.size() < size ? 0 : -1;
    } else if (action == "save") { /* Save as prefab from the selection */
        const Json* name = params.get("name");
        std::string message;
        std::string item = name && name->type == Json::String ? rood_library::save_selection(name->string, &message) : "";
        if (item.empty()) {
            snprintf(out, size, "%s", message.empty() ? "save needs name" : message.c_str());
            return -1;
        }
        std::string text = "{\"item\":" + q(item) + ",\"message\":" + q(message) + "}";
        snprintf(out, size, "%s", text.c_str());
        return (int)text.size() < size ? 0 : -1;
    } else if (action == "floor") { /* the level floor area a fog volume dropped at (x, z) fits into */
        const Json* x = params.get("x");
        const Json* z = params.get("z");
        float lo[2], hi[2], h;
        std::string text = "{\"area\":null}";
        if (x && z && rood_atmo::floor_area_at((float)x->number, (float)z->number, lo, hi, &h)) {
            char t[160];
            snprintf(t, sizeof t, "{\"area\":{\"lo\":[%.2f,%.2f],\"hi\":[%.2f,%.2f],\"height\":%.2f}}", lo[0], lo[1], hi[0],
                hi[1], h);
            text = t;
        }
        snprintf(out, size, "%s", text.c_str());
        return 0;
    } else if (action == "edit_prefab" || action == "rename" || action == "delete" || action == "file") {
        const Json* item = params.get("item");
        if (action == "edit_prefab" && item && item->type == Json::String && item->string.empty()) { /* "": closed */
            rood_lights::edit_prefab("");
            std::string text = "{\"editing_prefab\":\"\"}";
            snprintf(out, size, "%s", text.c_str());
            return 0;
        }
        if (!item || item->type != Json::String || !rood_library::find(item->string)) {
            snprintf(out, size, "%s needs item (a library item)", action.c_str());
            return -1;
        }
        std::string message, file = rood_library::file_of(item->string);
        bool ok = true;
        if (action == "edit_prefab") {
            ok = rood_library::edit_prefab(item->string, &message);
        } else if (action == "rename") {
            const Json* name = params.get("name");
            ok = rood_library::rename(item->string, name && name->type == Json::String ? name->string : "", &message);
        } else if (action == "delete") {
            ok = rood_library::remove(item->string, &message);
        }
        if (!ok) {
            snprintf(out, size, "%s", message.c_str());
            return -1;
        }
        std::string text = "{\"item\":" + q(item->string) + ",\"file\":" + q(file) + ",\"message\":" + q(message)
            + ",\"editing_prefab\":" + q(rood_lights::editing_prefab_id()) + "}";
        snprintf(out, size, "%s", text.c_str());
        return (int)text.size() < size ? 0 : -1;
    } else if (action != "list") {
        snprintf(out, size, "action: list, filter, favourite, view, thumbs, save, edit_prefab, rename, delete or file");
        return -1;
    }
    std::string text = list_json();
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}

/* ---- Save as prefab (Task 18) ---- */
std::string rood_library::save_selection(const std::string& name, std::string* message)
{
    if (name.empty()) {
        *message = "Save as prefab: give it a name";
        return std::string();
    }
    const std::vector<rood_selection::Ref>& sel = rood_selection::items();
    int lights_n = 0, actors_n = 0, volumes_n = 0;
    for (const rood_selection::Ref& r : sel)
        lights_n += r.kind == rood_selection::KIND_LIGHT, actors_n += r.kind == rood_selection::KIND_ACTOR,
            volumes_n += r.kind == rood_selection::KIND_VOLUME;
    std::string item;
    if (volumes_n && volumes_n == (int)sel.size()) {
        if (volumes_n == 1) { /* a fog volume: a fog preset of the user's; it uses it */
            std::string id;
            if (rood_atmo::save_user_preset(rood_scene::index_of(sel[0]), name, &id, message))
                item = "fog:" + id;
        } else {
            *message = "Save as prefab: a fog preset is one volume's look; select one fog volume";
        }
    } else if (volumes_n) {
        *message = "Save as prefab: a fog volume saves as a fog preset on its own; select lights and light actors, "
                   "or one fog volume";
    } else if (actors_n == 1 && sel.size() == 1) { /* one actor: a prefab with its values (its parameters kept); it uses it */
        int i = rood_scene::index_of(sel[0]);
        if (i >= 0 && rood_lights::save_actor_as_prefab(i, name))
            item = "prefab:" + rood_lights::actors[i].prefab;
        *message = rood_lights::status;
    } else if (lights_n + actors_n == (int)sel.size() && !sel.empty()) {
        /* lights and light actors: one light prefab of them all (an actor's lights at rest, baked); they become
         * one actor of it */
        std::vector<int> li, ai;
        for (const rood_selection::Ref& r : sel) {
            int i = rood_scene::index_of(r);
            if (i >= 0)
                (r.kind == rood_selection::KIND_LIGHT ? li : ai).push_back(i);
        }
        if (rood_lights::create_prefab_from(li, ai, name) && rood_lights::selected_actor >= 0) {
            item = "prefab:" + rood_lights::actors[rood_lights::selected_actor].prefab;
            rood_scene::publish(); /* the selection follows the new actor */
        }
        *message = rood_lights::status;
    } else {
        *message = "Save as prefab: select lights and light actors, or one fog volume";
    }
    if (!item.empty())
        pc_undo_hint(("Save as prefab: " + name).c_str());
    return item;
}
