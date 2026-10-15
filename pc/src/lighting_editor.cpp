// The light editor's side of lighting packs (lighting_layers.h): the room's
// layers loaded into the editor's lights, actors and overrides; saving to
// the user layer; Promote to pack; the Outliner's layer badges, revert and
// problem report; live reload of every layer file; RPC `lighting`.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_undo.h"
#include "editor_scene.h"
#include "editor_selection.h"
#include "environment_internal.h"
#include "glow_overrides.h"
#include "lighting_layers.h"
#include "lights_internal.h"
#include "room_effects.h"
#include "platform.h"

extern "C" {
void pc_room_settings_reset(void);
int pc_room_settings_parse(const char* line);
void pc_room_settings_write(FILE* f);
int pc_room_settings_overrides(void);
}

using namespace rood_lights;
using lighting::Item;
using lighting::State;

namespace {
lighting::Room room_layers;
int loaded_serial = -1;
unsigned load_count; /* every read of the room's layers (room entry, live reload, RPC reload) */
std::vector<std::string> loaded_problem_ids; /* reported once per load */

std::string capture(void (*write)(FILE*))
{
    size_t size = 0;
    char* buffer = pc_capture_file(write, &size);
    if (!buffer)
        return std::string();
    std::string text(buffer, size);
    free(buffer);
    return text;
}
std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        out.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

/* The editor's room as items. */
State current_state()
{
    ensure_ids();
    State s;
    for (const Light& l : lights) {
        Item it = lighting::item_from_light(l);
        s[it.id()] = it;
    }
    for (const prefab::Actor& a : actors) {
        Item it = lighting::item_from_actor(a);
        s[it.id()] = it;
    }
    /* Room settings, glow and texture effect overrides go through their old
     * one-line forms (the modules' own writers), so nothing is missed. */
    std::string text = capture(pc_room_settings_write) + capture(pc_uv_effects_write);
    static pc_glow_override glows[256];
    int n = pc_gi_glows_get(glows, 256);
    for (int i = 0; i < n; i++) {
        const pc_glow_override& o = glows[i];
        char line[256];
        const char* shape = o.shape == PC_GLOW_SHAPE_SPHERE ? " shape=sphere" : o.shape == PC_GLOW_SHAPE_COLUMN ? " shape=column" : "";
        snprintf(line, sizeof line, "glow %.1f %.1f %.1f %d %.3f %.3f %.3f %.3f %.2f %.1f%s%s%s%s%s\n", o.position[0],
            o.position[1], o.position[2], o.muted, o.intensity, o.tint[0], o.tint[1], o.tint[2], o.size, o.reach,
            o.key[0] ? " key=" : "", o.key, o.fixture_type[0] ? " fixture_type=" : "", o.fixture_type, shape);
        text += line;
    }
    std::vector<Item> items;
    lighting::legacy_items(text, items);
    for (Item& it : items)
        s[it.id()] = it;
    rood_env::add_items(s); /* the room's sun and sky (environment.cpp) */
    return s;
}

/* Put a state into the editor. With keep_order, lights and actors still
 * present stay where they are in the lists (a revert, a rebase); new ones
 * follow in id order. */
void apply_state(const State& s, bool keep_order)
{
    std::vector<Light> new_lights;
    std::vector<prefab::Actor> new_actors;
    std::set<std::string> placed;
    if (keep_order) {
        for (const Light& l : lights) {
            auto it = s.find("light:" + l.id);
            Light x;
            if (it != s.end() && lighting::light_from_item(it->second, x)) {
                new_lights.push_back(x);
                placed.insert(it->first);
            }
        }
        for (const prefab::Actor& a : actors) {
            auto it = s.find("actor:" + a.id);
            prefab::Actor x;
            if (it != s.end() && lighting::actor_from_item(it->second, x)) {
                /* Values that follow the game stay as they are. */
                if (x.glow_linked && a.glow_linked)
                    memcpy(x.glow, a.glow, sizeof x.glow);
                new_actors.push_back(x);
                placed.insert(it->first);
            }
        }
    }
    pc_room_settings_reset();
    pc_uv_effects_reset();
    rood_env::reset();
    static pc_glow_override old[256], glows[256];
    int old_count = pc_gi_glows_get(old, 256), count = 0;
    for (const auto& kv : s) {
        const Item& it = kv.second;
        if (placed.count(kv.first))
            continue;
        if (it.kind == "light") {
            Light x;
            if (lighting::light_from_item(it, x))
                new_lights.push_back(x);
        } else if (it.kind == "actor") {
            prefab::Actor x;
            if (lighting::actor_from_item(it, x))
                new_actors.push_back(x);
        } else if (it.kind == "set") {
            const std::string* v = it.get("value");
            if (v)
                pc_room_settings_parse(("set " + it.name + " " + *v).c_str());
        } else if (it.kind == "env" || it.kind == "fog" || it.kind == "post") {
            rood_env::apply_item(it);
        } else if (it.kind == "uv") {
            const std::string* speed = it.get("speed");
            const std::string* freeze = it.get("freeze");
            pc_uv_effect_override(atoi(it.name.c_str()), speed ? (float)atof(speed->c_str()) : 1.0f,
                freeze ? atoi(freeze->c_str()) : 0);
        } else if (it.kind == "glow" && count < 256) {
            pc_glow_override o;
            memset(&o, 0, sizeof o);
            o.intensity = 1;
            o.tint[0] = o.tint[1] = o.tint[2] = 1;
            auto num = [&](const char* key, float* out, int n) {
                const std::string* v = it.get(key);
                if (!v)
                    return;
                const char* p = v->c_str();
                for (int i = 0; i < n; i++) {
                    char* end;
                    out[i] = strtof(p, &end);
                    p = *end == ',' ? end + 1 : end;
                }
            };
            num("pos", o.position, 3);
            float muted = 0;
            num("muted", &muted, 1);
            o.muted = (int)muted;
            num("intensity", &o.intensity, 1);
            num("tint", o.tint, 3);
            num("size", &o.size, 1);
            num("reach", &o.reach, 1);
            if (const std::string* t = it.get("fixture_type"))
                snprintf(o.fixture_type, sizeof o.fixture_type, "%s", t->c_str());
            if (const std::string* sh = it.get("shape"))
                o.shape = *sh == "sphere" ? PC_GLOW_SHAPE_SPHERE : *sh == "column" ? PC_GLOW_SHAPE_COLUMN : PC_GLOW_SHAPE_AUTO;
            if (it.name[0] != '@') {
                snprintf(o.key, sizeof o.key, "%s", it.name.c_str());
                for (int i = 0; i < old_count; i++)
                    if (!strcmp(old[i].key, o.key)) {
                        o.bound = old[i].bound; /* already found this visit */
                        memcpy(o.position, old[i].position, sizeof o.position);
                    }
            }
            glows[count++] = o;
        }
    }
    pc_gi_glows_set(glows, count);
    lights = new_lights;
    actors = new_actors;
    rood_scene::refresh(); /* the selection is by id: what is still here stays selected */
    if (solo >= (int)lights.size())
        solo = -1;
    if (solo_actor >= (int)actors.size())
        solo_actor = -1;
}

std::string layer_names(const std::string& id)
{
    std::string s;
    auto p = room_layers.provenance.find(id);
    if (p != room_layers.provenance.end() && p->second.defined >= 0) {
        s = "from " + room_layers.layers[p->second.defined].label();
        for (int i : p->second.changed)
            s += ", changed in " + room_layers.layers[i].label();
    }
    return s;
}

State frame_state; /* the Outliner's rows share one export per frame */
int frame_state_frame = -1;
const State& outliner_state()
{
    int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -2;
    if (frame != frame_state_frame) {
        frame_state = current_state();
        frame_state_frame = frame;
    }
    return frame_state;
}

bool revert(const std::string& id)
{
    State s = current_state();
    auto b = room_layers.base.find(id);
    if (b != room_layers.base.end())
        s[id] = b->second;
    else
        s.erase(id);
    apply_state(s, true);
    dirty = true;
    frame_state_frame = -1;
    pc_undo_hint(("Revert " + id).c_str());
    return true;
}

std::string pack_name()
{
    return room_layers.active >= 0 ? room_layers.layers[room_layers.active].pack : std::string();
}

bool promote(const std::vector<std::string>& ids)
{
    State s = current_state();
    /* The user layer first (Promote moves what it holds). */
    std::string message;
    if (!lighting::save_user(room_layers, s, &message)) {
        snprintf(status, sizeof status, "%s", message.c_str());
        return false;
    }
    bool ok = lighting::promote(room_layers, s, ids, &message);
    snprintf(status, sizeof status, "%s", message.c_str());
    if (ok)
        dirty = false;
    frame_state_frame = -1;
    return ok;
}

void report_problems()
{
    for (const lighting::Problem& p : room_layers.problems) {
        std::string where = p.layer >= 0 ? room_layers.layers[p.layer].file.path : std::string();
        fprintf(stderr, "[lighting] %s:%d: %s\n", where.c_str(), p.line, p.message.c_str());
    }
}
} // namespace

void rood_lights::ensure_ids()
{
    std::set<std::string> seen;
    for (Light& l : lights) {
        if (l.id.empty() || !seen.insert(l.id).second) {
            l.id = lighting::new_name();
            seen.insert(l.id);
        }
    }
    seen.clear();
    for (prefab::Actor& a : actors) {
        if (a.id.empty() || !seen.insert(a.id).second) {
            a.id = lighting::new_name();
            seen.insert(a.id);
        }
    }
}
std::string rood_lights::light_item_id(int i)
{
    return i >= 0 && i < (int)lights.size() ? "light:" + lights[i].id : std::string();
}
std::string rood_lights::actor_item_id(int i)
{
    return i >= 0 && i < (int)actors.size() ? "actor:" + actors[i].id : std::string();
}

/* Unsaved edits of the rooms left this session (layers_stash), by room. */
static std::map<std::pair<int, int>, std::vector<lighting::Entry>> stashed;

void rood_lights::layers_stash()
{
    if (zone < 0)
        return;
    std::vector<lighting::Entry> edits = lighting::diff(room_layers.base, current_state());
    for (const lighting::Entry& e : room_layers.user_dangling)
        edits.push_back(e);
    if (!edits.empty()) {
        stashed[{ zone, room }] = edits;
        fprintf(stderr, "[lights] zone %d room %d: %zu unsaved edits kept for when you come back\n", zone, room,
            edits.size());
    }
}

void rood_lights::layers_load(int z, int r)
{
    lighting::load_room(room_layers, z, r);
    loaded_serial = lighting::config_serial();
    load_count++;
    auto kept = stashed.find({ z, r });
    if (kept != stashed.end()) {
        /* Back in a room left with unsaved edits: they go on top of its
         * layers as read now, still unsaved. */
        State s = room_layers.base;
        std::vector<lighting::Entry> dangling;
        for (const lighting::Entry& e : kept->second) {
            std::string problem;
            if (!lighting::apply_entry(s, e, &problem))
                dangling.push_back(e);
        }
        room_layers.user_dangling = dangling;
        size_t count = kept->second.size();
        stashed.erase(kept);
        apply_state(s, false);
        dirty = true;
        frame_state_frame = -1;
        report_problems();
        snprintf(status, sizeof status, "Your unsaved edits here (%zu) are back (Save area keeps them)", count);
        fprintf(stderr, "[lights] zone %d room %d: %zu unsaved edits put back\n", z, r, count);
        return;
    }
    apply_state(room_layers.loaded, false);
    frame_state_frame = -1;
    report_problems();
    const lighting::Layer& user = room_layers.layers.back();
    int items = (int)room_layers.loaded.size();
    if (!items && room_layers.problems.empty())
        snprintf(status, sizeof status, "No lighting for zone %d room %d yet", z, r);
    else
        snprintf(status, sizeof status, "Loaded %zu lights, %zu actors and %d settings (%d layer%s%s)%s", lights.size(),
            actors.size(), pc_room_settings_overrides(), (int)room_layers.layers.size(),
            room_layers.layers.size() == 1 ? "" : "s", user.file.version == 0 && user.file.exists ? ", old format" : "",
            room_layers.problems.empty() ? "" : "; problems: see the Outliner");
}

bool rood_lights::layers_save()
{
    if (zone < 0)
        return false;
    std::string message;
    bool ok = lighting::save_user(room_layers, current_state(), &message);
    if (ok)
        dirty = false;
    snprintf(status, sizeof status, "%s", message.c_str());
    frame_state_frame = -1;
    return ok;
}

/* RPC lights / lighting {action: "reload"}: read every layer of the room from disk
 * now, as the live reload does but without waiting for its wall-clock poll
 * (scripts write a room file, reload, then count frames). Unsaved edits are
 * dropped. The layers' stored mtimes are the files' as read, so the poll
 * does not load the same files again (the renderers would see the same
 * lights anyway: their history resets follow a hash of the lights). */
unsigned rood_lights::layers_reload()
{
    if (zone < 0)
        return load_count;
    dirty = false;
    stashed.clear(); /* every room's kept edits too */
    layers_load(zone, room);
    pc_undo_reset();
    snprintf(status + strlen(status), sizeof status - strlen(status), " (reloaded)");
    return load_count;
}
unsigned rood_lights::layers_loads() { return load_count; }

void rood_lights::layers_poll()
{
    if (zone < 0)
        return;
    bool config = lighting::config_serial() != loaded_serial;
    if (!config && !lighting::changed_on_disk(room_layers))
        return;
    const lighting::Layer& user = room_layers.layers.back();
    bool user_changed = lighting::file_mtime(user.path) != (user.file.path == user.path ? user.file.mtime : 0);
    if (!dirty) {
        layers_load(zone, room);
        pc_undo_reset();
        snprintf(status + strlen(status), sizeof status - strlen(status), config ? " (packs changed)" : " (changed on disk)");
        return;
    }
    if (user_changed) {
        /* Our unsaved edits and the user layer's new content cannot both
         * win: say so, and stop asking until it changes again. */
        snprintf(status, sizeof status, "%s changed on disk; Reload area to take it (unsaved edits here)",
            user.path.c_str());
        for (lighting::Layer& l : room_layers.layers)
            l.file.mtime = lighting::file_mtime(l.file.path);
        return;
    }
    /* A pack changed (a pull, a regeneration): keep the unsaved edits on top
     * of the new layers. */
    State mine = current_state();
    std::vector<lighting::Entry> edits = lighting::diff(room_layers.base, mine);
    for (const lighting::Entry& e : room_layers.user_dangling)
        edits.push_back(e);
    lighting::Room again;
    lighting::load_room(again, zone, room);
    loaded_serial = lighting::config_serial();
    State s = again.base;
    std::vector<lighting::Entry> dangling;
    for (const lighting::Entry& e : edits) {
        std::string problem;
        if (!lighting::apply_entry(s, e, &problem)) {
            dangling.push_back(e);
            again.problems.push_back(lighting::Problem { (int)again.layers.size() - 1, 0, e.id, problem });
        }
    }
    again.user_dangling = dangling;
    room_layers = again;
    apply_state(s, true);
    pc_undo_reset();
    frame_state_frame = -1;
    report_problems();
    snprintf(status, sizeof status, "Lighting packs changed on disk; your unsaved edits are kept on top");
}

/* ---- Outliner ---- */

void rood_lights::layer_row(const std::string& id)
{
    if (id.empty())
        return;
    /* The context menu belongs to the row's item (drawn just before). */
    std::string b = lighting::badge(room_layers, outliner_state(), id);
    bool can_revert = !b.empty() && (b == "U" || (b.size() >= 2 && b.compare(b.size() - 2, 2, ">U") == 0));
    if (ImGui::BeginPopupContextItem(("layers " + id).c_str())) {
        ImGui::TextDisabled("%s", id.c_str());
        std::string where = layer_names(id);
        if (!where.empty())
            ImGui::TextDisabled("%s", where.c_str());
        bool in_base = room_layers.base.count(id) != 0;
        if (ImGui::MenuItem(in_base ? "Revert to the layers below" : "Remove (only in the user layer)", nullptr, false,
                can_revert))
            revert(id);
        std::string pack = pack_name();
        if (ImGui::MenuItem(("Promote to pack '" + pack + "'").c_str(), nullptr, false, can_revert && !pack.empty()))
            promote({ id });
        if (ImGui::MenuItem("Copy id"))
            ImGui::SetClipboardText(id.c_str());
        ImGui::EndPopup();
    }
    if (b.empty())
        return;
    ImGui::SameLine();
    ImVec4 colour = b == "U" ? ImVec4(0.55f, 0.85f, 1, 1)
        : can_revert          ? ImVec4(1, 0.78f, 0.3f, 1)
                              : ImVec4(0.6f, 0.6f, 0.6f, 1);
    ImGui::TextColored(colour, "[%s]", b.c_str());
    if (ImGui::IsItemHovered()) {
        std::string where = layer_names(id);
        ImGui::SetTooltip("%s\n%s%s\nG generated, P pack (authored), U yours (pc/user)\nRight-click the row: revert, promote",
            id.c_str(), where.empty() ? "only in your user layer" : where.c_str(),
            can_revert && b != "U" ? "\nchanged by you (the user layer)" : "");
    }
}

void rood_lights::layers_outliner_header()
{
    const State& now = outliner_state();
    int mine = (int)lighting::diff(room_layers.base, now).size();
    std::vector<std::string> gone; /* deleted here (in the base, not now) */
    for (const auto& kv : room_layers.base)
        if (!now.count(kv.first) && kv.second.kind != "set" && kv.second.kind != "uv" && kv.second.kind != "env"
            && kv.second.kind != "fog")
            gone.push_back(kv.first);
    int problems = (int)room_layers.problems.size();
    char title[160];
    std::string packs;
    for (const std::string& p : lighting::packs())
        packs += (packs.empty() ? "" : ", ") + p;
    snprintf(title, sizeof title, "Layers: %s + user (%d change%s)%s###layers", packs.empty() ? "no packs" : packs.c_str(),
        mine, mine == 1 ? "" : "s", problems ? "  !" : "");
    if (problems)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.25f, 1));
    bool open = ImGui::TreeNodeEx(title, problems ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    if (problems)
        ImGui::PopStyleColor();
    if (!open)
        return;
    for (size_t i = 0; i < room_layers.layers.size(); i++) {
        const lighting::Layer& l = room_layers.layers[i];
        bool active = (int)i == room_layers.active;
        ImGui::TextColored(l.file.exists ? ImVec4(0.85f, 0.85f, 0.85f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1), "%c %s%s: %s%s",
            l.badge(), l.label().c_str(), active ? " (Promote target)" : "",
            l.file.exists ? std::to_string(l.file.entries.size()).c_str() : "no file",
            l.file.exists ? " entries" : "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", l.file.path.empty() ? l.path.c_str() : l.file.path.c_str(),
                l.file.exists && l.file.version == 0 ? "\n(old pc/lights format: saving migrates it)" : "");
    }
    std::string pack = pack_name();
    ImGui::BeginDisabled(!mine || pack.empty());
    if (ImGui::SmallButton(("Promote room to '" + (pack.empty() ? std::string("no pack") : pack) + "'").c_str()))
        promote({});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Move every change in your user layer into the pack's authored layer\n(lighting/%s/rooms: "
                          "tracked, for pull requests). Saves the room first.",
            pack.c_str());
    if (!gone.empty()) {
        ImGui::TextDisabled("Deleted here (%zu):", gone.size());
        for (const std::string& id : gone) {
            ImGui::PushID(id.c_str());
            if (ImGui::SmallButton("Restore"))
                revert(id);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", id.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", layer_names(id).c_str());
            ImGui::PopID();
        }
    }
    if (problems) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.25f, 1), "%d problem%s:", problems, problems == 1 ? "" : "s");
        for (const lighting::Problem& p : room_layers.problems) {
            std::string where = p.layer >= 0 ? room_layers.layers[p.layer].label() : "";
            ImGui::BulletText("%s%s%d: %s", where.c_str(), p.line ? " line " : "", p.line, p.message.c_str());
        }
        if (!room_layers.user_dangling.empty()) {
            if (ImGui::SmallButton("Discard dangling user entries")) {
                room_layers.user_dangling.clear();
                dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("They are kept in your user layer until you discard them (saved on the next Save)");
        }
    }
    ImGui::TreePop();
}

/* ---- File menu entries (dev_overlay.cpp) ---- */
extern "C" void pc_lighting_file_menu(void)
{
    std::string pack = pack_name();
    std::vector<std::string> sel;
    for (const rood_selection::Ref& r : rood_selection::items())
        sel.push_back(r.id);
    std::string label = "Promote room to pack '" + (pack.empty() ? std::string("(none)") : pack) + "'";
    if (ImGui::MenuItem(label.c_str(), nullptr, false, !pack.empty()))
        promote({});
    if (ImGui::MenuItem("Promote selection to pack", nullptr, false, !pack.empty() && !sel.empty()))
        promote(sel);
}

/* ---- RPC lighting ---- */
namespace {
std::string jq(const std::string& s) { return prefab::json_quote(s); }
std::string entries_json(const std::vector<lighting::Entry>& entries)
{
    std::string s = "[";
    for (size_t i = 0; i < entries.size(); i++)
        s += (i ? "," : "") + jq(lighting::format_entry(entries[i]));
    return s + "]";
}
} // namespace

/* RPC lighting: action layers (default), save, promote {ids}, revert {id},
 * reload, packs {packs, active}, discard_dangling. Replies with the room's
 * layers, the effective items with their badges, problems and the user
 * layer as it would be saved. */
extern "C" int pc_lighting_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    track_room();
    std::string action = "layers";
    if (const prefab::Json* a = params.get("action"))
        action = a->string;
    bool ok = true;
    if (action == "save")
        ok = layers_save();
    else if (action == "reload")
        layers_reload();
    else if (action == "revert") {
        const prefab::Json* id = params.get("id");
        if (!id || id->type != prefab::Json::String) {
            snprintf(out, size, "revert needs id");
            return -1;
        }
        ok = revert(id->string);
    } else if (action == "promote") {
        std::vector<std::string> ids;
        if (const prefab::Json* list = params.get("ids"))
            for (const prefab::Json& j : list->items)
                ids.push_back(j.string);
        ok = promote(ids);
    } else if (action == "packs") {
        if (const prefab::Json* p = params.get("packs"))
            lighting::set_packs(p->string);
        if (const prefab::Json* p = params.get("active"))
            lighting::set_active_pack(p->string);
        layers_poll();
    } else if (action == "discard_dangling") {
        room_layers.user_dangling.clear();
        dirty = true;
    } else if (action == "poll")
        layers_poll();
    else if (action != "layers") {
        snprintf(out, size, "unknown action %s", action.c_str());
        return -1;
    }
    State now = current_state();
    std::string s = "{\"ok\":" + std::string(ok ? "true" : "false") + ",\"zone\":" + std::to_string(zone)
        + ",\"room\":" + std::to_string(room) + ",\"dirty\":" + (dirty ? "true" : "false") + ",\"status\":" + jq(status)
        + ",\"root\":" + jq(lighting::packs_root()) + ",\"user_dir\":" + jq(lighting::user_dir()) + ",\"packs\":[";
    std::vector<std::string> packs = lighting::packs();
    for (size_t i = 0; i < packs.size(); i++)
        s += (i ? "," : "") + jq(packs[i]);
    s += "],\"active\":" + jq(pack_name()) + ",\"layers\":[";
    for (size_t i = 0; i < room_layers.layers.size(); i++) {
        const lighting::Layer& l = room_layers.layers[i];
        s += std::string(i ? "," : "") + "{\"label\":" + jq(l.label()) + ",\"badge\":\"" + l.badge()
            + "\",\"path\":" + jq(l.path) + ",\"read\":" + jq(l.file.path) + ",\"exists\":"
            + (l.file.exists ? "true" : "false") + ",\"version\":" + std::to_string(l.file.version)
            + ",\"entries\":" + entries_json(l.file.entries) + "}";
    }
    s += "],\"items\":[";
    bool first = true;
    for (const auto& kv : now) {
        lighting::Entry e;
        e.item = kv.second;
        e.id = kv.first;
        s += std::string(first ? "" : ",") + "{\"id\":" + jq(kv.first) + ",\"badge\":"
            + jq(lighting::badge(room_layers, now, kv.first)) + ",\"line\":" + jq(lighting::format_entry(e)) + "}";
        first = false;
    }
    s += "],\"deleted\":[";
    first = true;
    for (const std::string& id : room_layers.deleted) {
        s += std::string(first ? "" : ",") + jq(id);
        first = false;
    }
    s += "],\"problems\":[";
    for (size_t i = 0; i < room_layers.problems.size(); i++) {
        const lighting::Problem& p = room_layers.problems[i];
        s += std::string(i ? "," : "") + "{\"layer\":" + jq(p.layer >= 0 ? room_layers.layers[p.layer].label() : "")
            + ",\"line\":" + std::to_string(p.line) + ",\"id\":" + jq(p.id) + ",\"message\":" + jq(p.message) + "}";
    }
    s += "],\"user_layer\":" + jq(lighting::user_layer_text(room_layers, now)) + "}";
    if ((int)s.size() >= size) {
        snprintf(out, size, "reply too large (%zu bytes)", s.size());
        return -1;
    }
    memcpy(out, s.c_str(), s.size() + 1);
    return (int)s.size();
}
