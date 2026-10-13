// The editor's undo history (editor_undo.h): transactions of snapshots.
//
// A snapshot is everything the editor edits: the room's lights and light
// actors, its settings overrides ("set" lines), glow overrides, texture
// effect overrides, the Outliner's room group visibility and the settings
// file's keys (graphics, post, lighting quality), plus the selection. They
// are small (a room has tens of lights and actors), so taking one twice a
// frame costs microseconds.
//
// How a transaction forms, once per overlay frame:
// - before the editor's panels run (pc_undo_frame_begin) the state is the
//   base; a change to the room's data since the last frame (an RPC edit, a
//   suggestion finishing) is committed first;
// - after them (pc_undo_frame_end) a difference from the base opens a
//   transaction, which stays open while an item or the gizmo is held (a
//   slider or gizmo drag, a value being typed, a drag from Place Actors) and
//   commits on release: one drag is one transaction, and a typed value that
//   Escape cancelled leaves nothing (after == before is dropped).
// Settings only count when they changed while the panels ran (RPC graphics,
// the settings file changing on disk, presets settling are not editor
// edits); the editor's own view switches (editor_* keys: overlays, snapping,
// the cutaway) are preferences, never transactions, as in Unreal.
//
// Undo applies only the parts the transaction changed, so it never reverts
// an unrelated change made in between (a glow drifting, a setting from the
// RPC). Values that follow the game (an actor's linked glow position, a glow
// override's tracked position and binding) are not compared.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImGuizmo.h"
#include "editor_undo.h"
#include "glow_overrides.h"
#include "lights_internal.h"
#include "room_editor.h"
#include "room_effects.h"
#include "environment_internal.h"
#include "platform.h"

extern "C" {
void pc_room_settings_reset(void);
int pc_room_settings_parse(const char* line);
void pc_room_settings_write(FILE* f);
void pc_settings_write(FILE* f);
void pc_settings_apply(const char* text);
void pc_lighting_sync(void);
}

using namespace rood_lights;

namespace {
enum Part { LIGHTS = 1, ACTORS = 2, ROOM = 4, GLOWS = 8, UV = 16, GROUPS = 32, SETTINGS = 64, ROOM_PARTS = 63 };

struct Snapshot {
    std::vector<Light> lights;
    std::vector<prefab::Actor> actors;
    std::string room_settings, uv;
    std::string environment; /* the room's sun and sky (part ROOM) */
    std::vector<pc_glow_override> glows;
    std::vector<uint8_t> groups;
    unsigned groups_id = 0; /* the room block the groups belong to */
    std::map<std::string, std::string> settings; /* key -> the rest of its line */
    int selected = -1, selected_actor = -1;
    bool glow_selected = false;
    uint32_t glow_id = 0;
    std::vector<int> multi;
};
struct Transaction {
    std::string name;
    unsigned parts;
    Snapshot before, after;
};

std::vector<Transaction> history; /* done: [0, done); undone (redo): [done, size) */
size_t done;
Snapshot base, pre_ui;
bool have_base, rebase = true, txn_open, in_frame;
Snapshot open_before;
int hold;
std::string hint, hold_name;

std::string capture_text(void (*write)(FILE*))
{
    size_t size = 0;
    char* buffer = pc_capture_file(write, &size);
    if (!buffer)
        return std::string();
    std::string text(buffer, size);
    free(buffer);
    return text;
}
void parse_settings(const std::string& text, std::map<std::string, std::string>& out)
{
    out.clear();
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        size_t space = line.find(' ');
        if (line.empty() || line[0] == '#')
            continue;
        out[line.substr(0, space)] = space == std::string::npos ? "" : line.substr(space + 1);
    }
}
/* The editor's own view switches: preferences, not edits. */
bool preference(const std::string& key) { return key.rfind("editor_", 0) == 0 || key == "probe_show"; }

void capture(Snapshot& s)
{
    s.lights = lights;
    s.actors = actors;
    s.room_settings = capture_text(pc_room_settings_write);
    s.environment = rood_env::text();
    s.uv = capture_text(pc_uv_effects_write);
    static pc_glow_override glows[256];
    int n = pc_gi_glows_get(glows, 256);
    s.glows.assign(glows, glows + n);
    static pc_room_group_info groups[4096];
    int count = pc_room_editor_groups(groups, 4096);
    s.groups.resize(count);
    unsigned id = 2166136261u ^ (unsigned)count;
    for (int i = 0; i < count; i++) {
        s.groups[i] = (uint8_t)groups[i].hidden;
        id = (id ^ (unsigned)(groups[i].position[0] * 31 + groups[i].position[1] * 17 + groups[i].position[2])) * 16777619u;
    }
    s.groups_id = id;
    parse_settings(capture_text(pc_settings_write), s.settings);
    s.selected = selected, s.selected_actor = selected_actor;
    s.glow_selected = glow_selected, s.glow_id = glow_id;
    s.multi = multi;
}

bool same_light(const Light& a, const Light& b)
{
    return a.type == b.type && !memcmp(a.position, b.position, sizeof a.position) && !memcmp(a.color, b.color, sizeof a.color)
        && a.intensity == b.intensity && a.radius == b.radius && !memcmp(a.direction, b.direction, sizeof a.direction)
        && a.cone == b.cone && !memcmp(a.size, b.size, sizeof a.size) && a.shadows == b.shadows && a.enabled == b.enabled;
}
/* Light fields other than where it is and where it points. */
bool same_light_params(const Light& a, const Light& b)
{
    Light x = a;
    memcpy(x.position, b.position, sizeof x.position);
    memcpy(x.direction, b.direction, sizeof x.direction);
    return same_light(x, b);
}
bool same_overrides(const prefab::Actor& a, const prefab::Actor& b)
{
    if (a.overrides.size() != b.overrides.size())
        return false;
    for (size_t i = 0; i < a.overrides.size(); i++) {
        const auto &x = a.overrides[i], &y = b.overrides[i];
        if (x.first != y.first || x.second.n != y.second.n || memcmp(x.second.v, y.second.v, sizeof x.second.v))
            return false;
    }
    return true;
}
/* An actor as the user edits it (its linked glow's drifting position and
 * lazily found key follow the game). */
bool same_actor(const prefab::Actor& a, const prefab::Actor& b)
{
    return a.prefab == b.prefab && !memcmp(a.position, b.position, sizeof a.position)
        && !memcmp(a.rotation, b.rotation, sizeof a.rotation) && a.enabled == b.enabled && a.seed == b.seed
        && same_overrides(a, b) && a.glow_linked == b.glow_linked && a.fixture == b.fixture
        && a.fixture_shadows == b.fixture_shadows && !memcmp(a.fixture_at, b.fixture_at, sizeof a.fixture_at)
        && a.fixture_radius == b.fixture_radius && a.fixture_radius_set == b.fixture_radius_set
        && a.fixture_type == b.fixture_type && a.flame == b.flame;
}
bool same_glow(const pc_glow_override& a, const pc_glow_override& b)
{
    return a.muted == b.muted && a.intensity == b.intensity && !memcmp(a.tint, b.tint, sizeof a.tint) && a.size == b.size
        && a.reach == b.reach && (!a.key[0] || !b.key[0] || !strcmp(a.key, b.key));
}
template <typename T, typename F> bool same_list(const std::vector<T>& a, const std::vector<T>& b, F same)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (!same(a[i], b[i]))
            return false;
    return true;
}
/* Setting keys whose values differ (preferences left out). */
std::vector<std::string> changed_settings(const Snapshot& a, const Snapshot& b)
{
    std::vector<std::string> keys;
    for (const auto& kv : b.settings) {
        if (preference(kv.first))
            continue;
        auto it = a.settings.find(kv.first);
        if (it == a.settings.end() || it->second != kv.second)
            keys.push_back(kv.first);
    }
    return keys;
}
unsigned difference(const Snapshot& a, const Snapshot& b, unsigned parts)
{
    unsigned d = 0;
    if ((parts & LIGHTS) && !same_list(a.lights, b.lights, same_light))
        d |= LIGHTS;
    if ((parts & ACTORS) && !same_list(a.actors, b.actors, same_actor))
        d |= ACTORS;
    if ((parts & ROOM) && (a.room_settings != b.room_settings || a.environment != b.environment))
        d |= ROOM;
    if ((parts & GLOWS) && !same_list(a.glows, b.glows, same_glow))
        d |= GLOWS;
    if ((parts & UV) && a.uv != b.uv)
        d |= UV;
    /* Another room block (the geometry changed): not an edit. */
    if ((parts & GROUPS) && a.groups_id == b.groups_id && a.groups != b.groups)
        d |= GROUPS;
    if ((parts & SETTINGS) && !changed_settings(a, b).empty())
        d |= SETTINGS;
    return d;
}

std::string actor_name(const prefab::Actor& a)
{
    const prefab::Prefab* p = prefab::library().find(a.prefab);
    return p ? p->name : a.prefab;
}
/* A name for what changed between two snapshots (when no hint gave one). */
std::string describe(const Snapshot& a, const Snapshot& b, unsigned parts)
{
    char text[160];
    if (parts & LIGHTS) {
        if (b.lights.size() > a.lights.size()) {
            size_t added = b.lights.size() - a.lights.size();
            if (added == 1)
                return std::string("Add ") + light_type_name(b.lights.back().type);
            snprintf(text, sizeof text, "Add %zu lights", added);
            return text;
        }
        if (b.lights.size() < a.lights.size())
            return a.lights.size() - b.lights.size() == 1 ? "Delete light" : "Delete lights";
        for (size_t i = 0; i < a.lights.size(); i++) {
            const Light &x = a.lights[i], &y = b.lights[i];
            if (same_light(x, y))
                continue;
            const char* verb = !same_light_params(x, y) ? "Edit"
                : memcmp(x.position, y.position, sizeof x.position) ? "Move"
                                                                   : "Rotate";
            snprintf(text, sizeof text, "%s %s %zu", verb, light_type_name(y.type), i + 1);
            return text;
        }
    }
    if (parts & ACTORS) {
        if (b.actors.size() > a.actors.size()) {
            size_t added = b.actors.size() - a.actors.size();
            if (added == 1)
                return "Add " + actor_name(b.actors.back());
            snprintf(text, sizeof text, "Add %zu actors", added);
            return text;
        }
        if (b.actors.size() < a.actors.size()) {
            for (size_t i = 0; i < b.actors.size() + 1 && i < a.actors.size(); i++)
                if (i == b.actors.size() || !same_actor(a.actors[i], b.actors[i]))
                    return "Delete " + actor_name(a.actors[i]);
            return "Delete actors";
        }
        for (size_t i = 0; i < a.actors.size(); i++) {
            const prefab::Actor &x = a.actors[i], &y = b.actors[i];
            if (same_actor(x, y))
                continue;
            prefab::Actor moved = x;
            memcpy(moved.position, y.position, sizeof moved.position);
            const char* verb = !same_actor(moved, y) ? (memcmp(x.position, y.position, sizeof x.position) ? "Edit" : nullptr)
                                                     : "Move";
            if (!verb) {
                prefab::Actor turned = x;
                memcpy(turned.rotation, y.rotation, sizeof turned.rotation);
                verb = same_actor(turned, y) ? "Rotate" : "Edit";
            }
            snprintf(text, sizeof text, "%s %s %zu", verb, actor_name(y).c_str(), i + 1);
            return text;
        }
    }
    if (parts & ROOM)
        return "Change room settings";
    if (parts & GLOWS)
        return a.glows.size() > b.glows.size() ? "Reset glow" : "Edit glow";
    if (parts & UV)
        return "Edit texture effect";
    if (parts & GROUPS)
        return "Change room group visibility";
    if (parts & SETTINGS) {
        std::vector<std::string> keys = changed_settings(a, b);
        for (const std::string& k : keys)
            if (k == "lighting_preset") {
                auto it = b.settings.find(k);
                return "Lighting quality " + (it != b.settings.end() ? it->second : std::string());
            }
        if (keys.size() == 1)
            return "Change " + keys[0];
        return "Change settings";
    }
    return "Edit";
}

void commit(const Snapshot& before, const Snapshot& after, unsigned parts, const std::string& name)
{
    if (!parts)
        return;
    history.resize(done); /* a new action drops what was undone */
    Transaction t;
    t.name = name.empty() ? describe(before, after, parts) : name;
    t.parts = parts;
    t.before = before;
    t.after = after;
    history.push_back(std::move(t));
    if (history.size() > PC_UNDO_LIMIT)
        history.erase(history.begin(), history.begin() + (history.size() - PC_UNDO_LIMIT));
    done = history.size();
}
/* Put back the parts of `to` that the transaction changed. */
void apply(const Snapshot& to, unsigned parts, const Snapshot& from)
{
    if (parts & LIGHTS)
        lights = to.lights;
    if (parts & ACTORS)
        actors = to.actors;
    if (parts & ROOM) {
        rood_env::set_text(to.environment);
        pc_room_settings_reset();
        size_t at = 0;
        while (at < to.room_settings.size()) {
            size_t end = to.room_settings.find('\n', at);
            if (end == std::string::npos)
                end = to.room_settings.size();
            pc_room_settings_parse(to.room_settings.substr(at, end - at).c_str());
            at = end + 1;
        }
    }
    if (parts & GLOWS)
        pc_gi_glows_set(to.glows.data(), (int)to.glows.size());
    if (parts & UV) {
        pc_uv_effects_reset();
        size_t at = 0;
        while (at < to.uv.size()) {
            size_t end = to.uv.find('\n', at);
            if (end == std::string::npos)
                end = to.uv.size();
            pc_uv_effects_parse(to.uv.substr(at, end - at).c_str());
            at = end + 1;
        }
    }
    if (parts & GROUPS) {
        static pc_room_group_info groups[4096];
        int count = pc_room_editor_groups(groups, 4096);
        if (count == (int)to.groups.size())
            for (int i = 0; i < count; i++)
                if (groups[i].hidden != to.groups[i])
                    pc_room_editor_set_hidden(i, to.groups[i]);
    }
    if (parts & SETTINGS) {
        /* The lines that differ, the preset first (it sets its keys). */
        std::string text;
        std::vector<std::string> keys = changed_settings(from, to);
        for (const std::string& k : keys)
            if (k == "lighting_preset")
                text = k + " " + to.settings.at(k) + "\n" + text;
            else
                text += k + " " + to.settings.at(k) + "\n";
        pc_settings_apply(text.c_str());
    }
    /* The selection the state had (when it still names something). */
    select_none();
    if (to.glow_selected)
        select_glow_id(to.glow_id);
    else if (to.selected_actor >= 0 && to.selected_actor < (int)actors.size())
        select_actor(to.selected_actor);
    else if (to.selected >= 0 && to.selected < (int)lights.size()) {
        select_light(to.selected);
        for (int i : to.multi)
            if (i >= 0 && i < (int)lights.size())
                multi.push_back(i);
    }
    if (solo >= (int)lights.size())
        solo = -1;
    if (solo_actor >= (int)actors.size())
        solo_actor = -1;
    if (parts & ROOM_PARTS)
        dirty = true;
}
/* The state now is the base (after an undo, redo or commit). */
void rebase_now()
{
    capture(base);
    have_base = true;
    rebase = false;
    if (in_frame)
        pre_ui = base;
}
bool busy()
{
    return hold > 0 || (ImGui::GetCurrentContext() && ImGui::IsAnyItemActive()) || ImGuizmo::IsUsing()
        || place_drag_active();
}
void close_open()
{
    if (!txn_open)
        return;
    txn_open = false;
    Snapshot now;
    capture(now);
    commit(open_before, now, difference(open_before, now, ~0u), hold_name.empty() ? hint : hold_name);
    hint.clear();
    hold_name.clear();
    base = now;
    if (in_frame)
        pre_ui = now;
}
} // namespace

extern "C" void pc_undo_hint(const char* name) { hint = name ? name : ""; }

extern "C" void pc_undo_reset(void)
{
    history.clear();
    done = 0;
    txn_open = false;
    hold = 0;
    hint.clear();
    hold_name.clear();
    rebase = true;
}

extern "C" void pc_undo_rebase(void)
{
    if (!txn_open)
        rebase = true;
}

extern "C" void pc_undo_frame_begin(void)
{
    in_frame = false;
    Snapshot now;
    capture(now);
    if (rebase || !have_base) {
        base = now;
        have_base = true;
        rebase = false;
    } else if (!txn_open) {
        /* The room's data changed between frames (RPC, a finished suggestion). */
        unsigned d = difference(base, now, ROOM_PARTS);
        if (d)
            commit(base, now, d, hint);
        if (d)
            hint.clear();
        base = now;
    }
    pre_ui = txn_open ? now : base;
    in_frame = true;
}

extern "C" void pc_undo_frame_end(void)
{
    if (!in_frame)
        return;
    in_frame = false;
    if (rebase) { /* the room changed while the panels ran */
        rebase_now();
        return;
    }
    Snapshot now;
    capture(now);
    if (!txn_open) {
        unsigned d = difference(base, now, ROOM_PARTS) | (difference(pre_ui, now, SETTINGS | GROUPS));
        if (d & SETTINGS) { /* a preset settles (Custom) before the state is kept */
            pc_lighting_sync();
            capture(now);
        }
        if (d) {
            txn_open = true;
            open_before = base;
            /* Settings the panels did not change keep their value from now. */
            open_before.settings = pre_ui.settings;
        }
    }
    if (txn_open && !busy())
        close_open();
    if (!txn_open) {
        hint.clear();
        base = now;
    }
}

extern "C" void pc_undo_prepare(void)
{
    if (txn_open || hold)
        return;
    capture(base);
    have_base = true;
    rebase = false;
}

extern "C" void pc_undo_flush(const char* name)
{
    if (txn_open || hold || rebase || !have_base)
        return;
    Snapshot now;
    capture(now);
    unsigned d = difference(base, now, ROOM_PARTS);
    std::string named = hint;
    if (named.empty() && d && name && describe(base, now, d) == "Edit")
        named = name; /* nothing better than the request's method */
    commit(base, now, d, named);
    if (d)
        hint.clear();
    base = now;
}

extern "C" void pc_undo_begin(const char* name)
{
    if (!hold++) {
        if (!txn_open) {
            rebase_now(); /* the state as the held transaction starts */
            txn_open = true;
            open_before = base;
        }
        hold_name = name ? name : "";
    }
}
extern "C" void pc_undo_end(void)
{
    if (hold > 0 && !--hold)
        close_open();
}

extern "C" int pc_undo_undo(void)
{
    if (busy() && !hold)
        return 0;
    close_open();
    if (!done)
        return 0;
    Transaction& t = history[--done];
    Snapshot now;
    capture(now);
    apply(t.before, t.parts, t.after);
    snprintf(status, sizeof status, "Undo: %s", t.name.c_str());
    rebase_now();
    return 1;
}
extern "C" int pc_undo_redo(void)
{
    if (busy() && !hold)
        return 0;
    close_open();
    if (done >= history.size())
        return 0;
    Transaction& t = history[done++];
    apply(t.after, t.parts, t.before);
    snprintf(status, sizeof status, "Redo: %s", t.name.c_str());
    rebase_now();
    return 1;
}
extern "C" const char* pc_undo_name(int redo)
{
    if (redo)
        return done < history.size() ? history[done].name.c_str() : nullptr;
    return done ? history[done - 1].name.c_str() : nullptr;
}
extern "C" int pc_undo_history(int* current)
{
    if (current)
        *current = (int)done;
    return (int)history.size();
}
extern "C" const char* pc_undo_entry(int i) { return i >= 0 && i < (int)history.size() ? history[i].name.c_str() : nullptr; }
extern "C" void pc_undo_jump(int target)
{
    while ((int)done > target && pc_undo_undo())
        ;
    while ((int)done < target && pc_undo_redo())
        ;
}

extern "C" int pc_undo_json(char* out, int size)
{
    std::string s = "{\"done\":" + std::to_string(done) + ",\"open\":" + (txn_open ? "true" : "false")
        + ",\"hold\":" + std::to_string(hold) + ",\"limit\":" + std::to_string(PC_UNDO_LIMIT) + ",\"history\":[";
    for (size_t i = 0; i < history.size(); i++) {
        s += i ? ",\"" : "\"";
        for (char c : history[i].name)
            if (c == '"' || c == '\\')
                s += '\\', s += c;
            else if ((unsigned char)c >= 32)
                s += c;
        s += '"';
    }
    s += "],\"undo\":";
    auto quoted = [&](const char* n) {
        if (!n)
            return std::string("null");
        std::string q = "\"";
        for (const char* c = n; *c; c++)
            if (*c == '"' || *c == '\\')
                q += '\\', q += *c;
            else
                q += *c;
        return q + "\"";
    };
    s += quoted(pc_undo_name(0)) + ",\"redo\":" + quoted(pc_undo_name(1)) + "}";
    snprintf(out, (size_t)size, "%s", s.c_str());
    return (int)s.size() < size ? (int)s.size() : size - 1;
}
