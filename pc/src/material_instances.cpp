// Master materials and instances (pc/docs/plans/2026-10-30-master-materials-plan.md): the Materials
// panel (Window > Materials) and RPC material_instance. An instance is a master (this part: the
// standard surface) with the parameter values it sets itself over its parent's; the panel is driven
// by the master's parameter list (a Houdini-style row per parameter: label, control, reset to
// parent), the assignment (a texture or a palette, scoped as entries are, or the part-2 selection's
// texture), and the unsaved layer's Save / Revert. The catalog itself lives in materials.cpp.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_inspect.h"
#include "editor_selection.h"
#include "editor_shell.h"
#include "emissive_internal.h"
#include "light_prefabs.h" /* prefab::Json */
#include "material_editor.h"
#include "materials.h"

using prefab::Json;

bool pc_material_instances_open = false;
extern "C" void pc_material_preview_select(const char* id) __attribute__((weak)); /* material_preview.cpp */
extern "C" int pc_overlay_set_details_tab(const char* name) __attribute__((weak)); /* dev_overlay.cpp */

namespace {
std::string selected; /* the instance shown in the panel */
char status_line[256];
char filter_text[64];
char new_id[64];

int fail(const std::string& m, char* out, int size)
{
    snprintf(out, size, "%s", m.c_str());
    return -1;
}
int reply(const Json& j, char* out, int size)
{
    std::string text = prefab::json_write(j, 0);
    text.erase(std::remove(text.begin(), text.end(), '\n'), text.end()); /* one line: the RPC is line-based */
    if ((int)text.size() >= size) return fail("reply too large", out, size);
    memcpy(out, text.c_str(), text.size() + 1);
    return 0;
}
std::string str(const Json& j, const char* name, const std::string& def = "")
{
    const Json* v = j.get(name);
    return v && v->type == Json::String ? v->string : def;
}

Json scope_json(const materials::Instance& i)
{
    Json s = Json::object();
    if (!i.rooms.empty()) {
        Json rooms = Json::array();
        for (const auto& p : i.rooms) {
            Json r = Json::array();
            r.items.push_back(Json::num(p.first)), r.items.push_back(Json::num(p.second));
            rooms.items.push_back(r);
        }
        s.set("rooms", rooms);
    } else if (i.zone >= 0) {
        Json r = Json::array();
        r.items.push_back(Json::num(i.zone)), r.items.push_back(Json::num(i.room));
        s.set("room", r);
    }
    return s;
}
Json instance_info(const materials::Instance& i)
{
    Json j = Json::object();
    j.set("id", Json::str(i.id));
    j.set("name", Json::str(i.name));
    j.set("where", Json::str(i.where));
    j.set("review", Json::str(i.review));
    j.set("master", Json::str(i.master));
    j.set("parent", Json::str(i.parent));
    j.set("root", Json::boolean_(i.root));
    j.set("user", Json::boolean_(i.user));
    j.set("pending", Json::boolean_(i.pending));
    j.set("preview", Json::boolean_(i.preview));
    j.set("assigned", Json::boolean_(i.assigned));
    Json own = Json::object();
    for (const auto& p : i.own) own.set(p.first.c_str(), Json::num(p.second));
    j.set("own", own);
    std::map<std::string, float> values;
    std::vector<std::string> chain;
    std::string error;
    if (materials::resolve_instance(i, values, &chain, &error)) {
        Json v = Json::object(), c = Json::array(), from = Json::object();
        for (const auto& p : values) v.set(p.first.c_str(), Json::num(p.second));
        for (const std::string& s : chain) c.items.push_back(Json::str(s));
        for (const auto& p : values) {
            std::string src;
            for (const std::string& s : chain) {
                const materials::Instance* a = s == i.id ? &i : materials::find_instance(s);
                if (a && a->own.count(p.first)) { src = s; break; }
            }
            from.set(p.first.c_str(), Json::str(src));
        }
        j.set("values", v), j.set("chain", c), j.set("from", from);
    } else
        j.set("error", Json::str(error));
    if (i.assigned) {
        Json m = Json::object();
        if (!i.key.empty()) m.set("key", Json::str(i.key));
        if (i.w > 0) {
            Json size = Json::array();
            size.items.push_back(Json::num(i.w)), size.items.push_back(Json::num(i.h));
            m.set("size", size);
        }
        if (!i.clut.empty()) m.set("clut", Json::str(i.clut));
        j.set("match", m);
        j.set("scope", scope_json(i));
    }
    Json children = Json::array();
    for (const materials::Instance& c : materials::instances())
        if (c.parent == i.id) children.items.push_back(Json::str(c.id));
    j.set("children", children);
    return j;
}
Json master_json(const materials::Master& m)
{
    Json j = Json::object();
    j.set("name", Json::str(m.name));
    j.set("shader", Json::str(m.shader));
    j.set("path", Json::str(m.path));
    Json params = Json::array();
    for (const materials::MasterParam& p : m.params) {
        Json q = Json::object();
        q.set("name", Json::str(p.name)), q.set("label", Json::str(p.label)), q.set("tip", Json::str(p.tip));
        q.set("group", Json::str(p.group)), q.set("type", Json::str(p.is_bool ? "bool" : "number"));
        q.set("default", Json::num(p.def)), q.set("min", Json::num(p.lo)), q.set("max", Json::num(p.hi));
        params.items.push_back(q);
    }
    j.set("parameters", params);
    Json slots = Json::array();
    for (const materials::MasterSlot& s : m.slots) {
        Json q = Json::object();
        q.set("name", Json::str(s.name)), q.set("label", Json::str(s.label)), q.set("channel", Json::str(s.channel));
        slots.items.push_back(q);
    }
    j.set("slots", slots);
    return j;
}
/* The list: `offset` and `limit` page it (a layer of hundreds of entries with
 * long room lists does not fit one reply); `masters` false leaves them out. */
Json list_json(int offset = 0, int limit = 0, bool with_masters = true)
{
    Json j = Json::object();
    if (with_masters) {
        Json masters = Json::array();
        for (const std::string& n : materials::master_names())
            if (const materials::Master* m = materials::master(n)) masters.items.push_back(master_json(*m));
        j.set("masters", masters);
    }
    Json list = Json::array();
    int roots = 0, assigned = 0, user = 0, n = 0;
    std::vector<materials::Instance> all = materials::instances();
    for (const materials::Instance& i : all) {
        if (n >= offset && (limit <= 0 || n < offset + limit)) list.items.push_back(instance_info(i));
        n++;
        roots += i.root, assigned += i.assigned, user += i.user;
    }
    j.set("instances", list);
    j.set("count", Json::num((double)all.size()));
    j.set("offset", Json::num(offset));
    j.set("roots", Json::num(roots)), j.set("assigned", Json::num(assigned)), j.set("user", Json::num(user));
    j.set("selected", Json::str(selected));
    j.set("dirty", Json::boolean_(materials::pending_dirty()));
    j.set("user_file", Json::str(materials::catalog_user_file()));
    j.set("masters_dir", Json::str(materials::masters_dir()));
    Json problems = Json::array();
    for (const std::string& p : materials::catalog_problems()) problems.items.push_back(Json::str(p));
    j.set("problems", problems);
    return j;
}

/* ---- edits (the panel and the RPC share them; every edit goes through the unsaved layer) ---- */

bool set_params(materials::Instance& i, const Json& params, std::string* error)
{
    const materials::Master* m = materials::master(i.master);
    if (!m) { *error = "unknown master " + i.master; return false; }
    for (const auto& pair : params.members) {
        const materials::MasterParam* p = m->param(pair.first);
        if (!p) { *error = "no parameter " + pair.first + " on " + i.master; return false; }
        if (pair.second.type == Json::Null) i.own.erase(pair.first); /* reset to parent */
        else if (pair.second.type == Json::Bool) i.own[pair.first] = pair.second.boolean ? 1.0f : 0.0f;
        else if (pair.second.type == Json::Number && std::isfinite(pair.second.number)) {
            if (pair.second.number < p->lo || pair.second.number > p->hi) {
                *error = pair.first + " outside " + std::to_string(p->lo) + ".." + std::to_string(p->hi);
                return false;
            }
            i.own[pair.first] = (float)pair.second.number;
        } else { *error = "bad value for " + pair.first; return false; }
    }
    return true;
}
bool commit(const materials::Instance& i, std::string* error)
{
    std::map<std::string, float> values;
    if (!materials::resolve_instance(i, values, nullptr, error)) return false;
    materials::set_pending_instance(i);
    return true;
}
bool set_parent(materials::Instance& i, const std::string& parent, std::string* error)
{
    if (!parent.empty() && !materials::find_instance(parent)) { *error = "unknown parent " + parent; return false; }
    if (parent == i.id) { *error = "an instance cannot be its own parent"; return false; }
    std::string old = i.parent;
    i.parent = parent;
    std::map<std::string, float> values;
    if (!materials::resolve_instance(i, values, nullptr, error)) { i.parent = old; return false; }
    return true;
}
std::string fresh_id(const std::string& base)
{
    std::set<std::string> ids;
    for (const materials::Instance& i : materials::instances()) ids.insert(i.id);
    if (!base.empty() && !ids.count(base)) return base;
    for (int n = 1;; n++) {
        std::string id = (base.empty() ? std::string("I") : base + " ") + std::to_string(n);
        if (!ids.count(id)) return id;
    }
}
/* A new instance under `parent` (a copy of it when `duplicate`): unassigned until assigned. */
materials::Instance make_instance(const std::string& id, const std::string& parent, bool duplicate)
{
    materials::Instance i;
    const materials::Instance* p = parent.empty() ? nullptr : materials::find_instance(parent);
    if (p && duplicate) {
        i = *p;
        i.root = false, i.user = true, i.pending = false, i.preview = false, i.review.clear();
    } else {
        i.parent = parent;
        if (p) i.master = p->master;
    }
    i.id = fresh_id(id);
    i.user = true;
    return i;
}
bool assign(materials::Instance& i, const std::string& key, int w, int h, const std::string& clut, const Json* scope,
    std::string* error)
{
    i.assigned = true;
    i.key = key, i.w = w, i.h = h, i.clut = clut;
    i.zone = i.room = -1, i.rooms.clear();
    if (scope && scope->type == Json::Object) {
        if (const Json* r = scope->get("room"); r && r->type == Json::Array && r->items.size() == 2)
            i.zone = (int)r->items[0].number, i.room = (int)r->items[1].number;
        if (const Json* rs = scope->get("rooms"); rs && rs->type == Json::Array)
            for (const Json& r : rs->items)
                if (r.type == Json::Array && r.items.size() == 2) i.rooms.push_back({ (int)r.items[0].number, (int)r.items[1].number });
        if (i.rooms.size() == 1) i.zone = i.rooms[0].first, i.room = i.rooms[0].second, i.rooms.clear();
        else if (!i.rooms.empty()) i.zone = i.rooms[0].first, i.room = i.rooms[0].second;
    }
    if (i.key.empty() && i.clut.empty()) { *error = "assign needs a texture key or a palette"; return false; }
    return true;
}
/* The part-2 selection's texture: the primary (inspected surface or model). */
bool selection_texture(emissive::TexturePick& out, std::string* error)
{
    const rood_inspect::Item* it = rood_inspect::item(rood_selection::primary());
    if (!it || !it->pick.valid) { *error = "select a surface or model in the view first"; return false; }
    out = it->pick;
    return true;
}
bool assign_selection(materials::Instance& i, bool palette, std::string* error)
{
    emissive::TexturePick p;
    if (!selection_texture(p, error)) return false;
    if (palette && p.clut_key.empty()) { *error = "the selected texture has no palette"; return false; }
    i.assigned = true;
    i.key = palette ? "" : p.key, i.clut = p.clut_key, i.w = palette ? 0 : p.w(), i.h = palette ? 0 : p.h();
    i.set_room(p.zone, p.room);
    return true;
}
} // namespace

extern "C" void material_instances_select(const char* id) { selected = id ? id : ""; }

/* ---------------------------------------------------------------- the panel */

namespace {
void parameter_rows(materials::Instance& i, bool& changed)
{
    const materials::Master* m = materials::master(i.master);
    if (!m) { ImGui::TextDisabled("unknown master %s", i.master.c_str()); return; }
    std::map<std::string, float> values;
    std::vector<std::string> chain;
    std::string error;
    if (!materials::resolve_instance(i, values, &chain, &error)) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error.c_str());
        return;
    }
    std::string group;
    for (const materials::MasterParam& p : m->params) {
        if (p.group != group) {
            group = p.group;
            if (!group.empty()) ImGui::SeparatorText(group.c_str());
        }
        bool own = i.own.count(p.name) > 0;
        std::string from;
        if (!own)
            for (const std::string& s : chain) {
                const materials::Instance* a = materials::find_instance(s);
                if (a && a != nullptr && a->id != i.id && a->own.count(p.name)) { from = s; break; }
            }
        ImGui::PushID(p.name.c_str());
        float v = values[p.name];
        if (!own) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        float width = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 1.5f;
        ImGui::SetNextItemWidth(width * 0.55f);
        if (p.is_bool) {
            bool b = v >= 0.5f;
            if (ImGui::Checkbox(p.label.c_str(), &b)) i.own[p.name] = b ? 1.0f : 0.0f, changed = true;
        } else if (ImGui::SliderFloat(p.label.c_str(), &v, p.lo, p.hi, "%.2f"))
            i.own[p.name] = v, changed = true;
        if (!own) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s%s%s", p.tip.c_str(), own ? "\n(set here)" : from.empty() ? "\n(the master's default)" : "\n(from ",
                own || from.empty() ? "" : (from + ")").c_str());
        if (own) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) i.own.erase(p.name), changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to the parent's value");
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", from.empty() ? "default" : from.c_str());
        }
        ImGui::PopID();
    }
    if (!m->slots.empty() && ImGui::TreeNode("Texture slots")) {
        std::string key = i.key;
        if (key.empty() && !i.clut.empty()) ImGui::TextDisabled("a palette's textures: their maps are per texture key");
        for (const materials::MasterSlot& s : m->slots) {
            std::string layer = "-";
            if (!key.empty() && !s.channel.empty())
                for (const materials::Present& c : materials::channels(key))
                    if (c.name == s.channel) layer = c.user ? "user" : c.baked ? "baked" : "shipped";
            ImGui::Text("%s: %s", s.label.c_str(), s.channel.empty() ? "the texture as drawn" : layer.c_str());
        }
        ImGui::TreePop();
    }
}
void tree_rows(const std::vector<materials::Instance>& all, const std::string& parent, int depth)
{
    for (const materials::Instance& i : all) {
        if (i.parent != parent || i.preview) continue;
        if (depth == 0 && !i.root && !i.parent.empty()) continue;
        bool has_children = false;
        for (const materials::Instance& c : all) has_children |= c.parent == i.id && !c.preview;
        bool shown = !filter_text[0] || i.id.find(filter_text) != std::string::npos || i.name.find(filter_text) != std::string::npos;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
            | (has_children ? 0 : ImGuiTreeNodeFlags_Leaf) | (selected == i.id ? ImGuiTreeNodeFlags_Selected : 0)
            | (filter_text[0] ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        char label[256];
        snprintf(label, sizeof label, "%s%s%s%s%s", i.user ? "* " : "", i.id.c_str(), i.name.empty() ? "" : "  ",
            i.name.c_str(), i.assigned ? "" : i.root ? "  (preset)" : "  (unassigned)");
        bool open = true;
        if (shown) {
            open = ImGui::TreeNodeEx(i.id.c_str(), flags, "%s", label);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                selected = i.id;
                if (pc_material_preview_select) pc_material_preview_select(selected.c_str());
            }
        }
        if (open) {
            tree_rows(all, i.id, depth + 1);
            if (shown) ImGui::TreePop();
        }
    }
}
} // namespace

extern "C" void pc_material_instances_window(void)
{
    if (!pc_material_instances_open) return;
    rood_shell::dock_tool_window("Materials");
    ImGui::SetNextWindowSize(ImVec2(760, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Materials", &pc_material_instances_open)) {
        ImGui::End();
        return;
    }
    std::vector<materials::Instance> all = materials::instances();
    ImGui::TextWrapped("Instances of the standard surface master: presets are roots, entries their children; "
                       "a child sets only the values it changes (the rest follow its parent).");
    if (materials::pending_dirty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "unsaved");
    }
    if (ImGui::Button("Save")) {
        std::string error;
        snprintf(status_line, sizeof status_line, "%s",
            materials::save_pending(&error) ? ("Saved into " + materials::catalog_user_file()).c_str() : error.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) materials::revert_pending(), snprintf(status_line, sizeof status_line, "Unsaved edits dropped");
    ImGui::SameLine();
    if (ImGui::Button("Preview")) {
        extern bool pc_material_preview_open;
        pc_material_preview_open = true;
        rood_shell::focus_window("Material preview");
        if (pc_material_preview_select) pc_material_preview_select(selected.c_str());
    }
    if (status_line[0]) ImGui::TextWrapped("%s", status_line);
    float left = ImGui::GetContentRegionAvail().x * 0.4f;
    ImGui::BeginChild("tree", ImVec2(left, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter", filter_text, sizeof filter_text);
    tree_rows(all, "", 0);
    /* Instances whose parent is not in the catalog (an old entry naming a preset that went): listed so
     * they can be re-parented; they make no entry until then. */
    std::set<std::string> ids;
    for (const materials::Instance& i : all) ids.insert(i.id);
    bool header = false;
    for (const materials::Instance& i : all) {
        if (i.parent.empty() || ids.count(i.parent) || i.preview) continue;
        if (!header) ImGui::SeparatorText("Unresolved (parent missing)"), header = true;
        char label[256];
        snprintf(label, sizeof label, "%s  (parent %s)", i.id.c_str(), i.parent.c_str());
        if (ImGui::Selectable(label, selected == i.id)) selected = i.id;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("params", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const materials::Instance* found = selected.empty() ? nullptr : materials::find_instance(selected);
    if (!found) {
        ImGui::TextDisabled("Select an instance, or make one:");
        ImGui::SetNextItemWidth(160);
        ImGui::InputTextWithHint("##newid", "id", new_id, sizeof new_id);
        ImGui::SameLine();
        if (ImGui::Button("New root instance")) {
            materials::Instance n = make_instance(new_id, "", false);
            n.root = true;
            std::string error;
            if (commit(n, &error)) selected = n.id, new_id[0] = 0;
            else snprintf(status_line, sizeof status_line, "%s", error.c_str());
        }
    } else {
        materials::Instance i = *found;
        bool changed = false;
        ImGui::PushID(i.id.c_str());
        ImGui::SeparatorText(i.id.c_str());
        ImGui::TextDisabled("%s%s, master %s%s", i.user ? "yours" : "curated", i.root ? ", a preset" : "", i.master.c_str(),
            i.pending ? ", unsaved" : "");
        char name[128], where[256];
        snprintf(name, sizeof name, "%s", i.name.c_str());
        snprintf(where, sizeof where, "%s", i.where.c_str());
        if (ImGui::InputText("Name", name, sizeof name)) i.name = name, changed = true;
        if (ImGui::InputText("Where", where, sizeof where)) i.where = where, changed = true;
        if (!i.root) {
            if (ImGui::BeginCombo("Parent", i.parent.empty() ? "(none: the master's defaults)" : i.parent.c_str())) {
                if (ImGui::Selectable("(none)", i.parent.empty())) {
                    std::string error;
                    if (set_parent(i, "", &error)) changed = true;
                }
                for (const materials::Instance& c : all) {
                    if (c.id == i.id || c.preview) continue;
                    if (ImGui::Selectable(c.id.c_str(), i.parent == c.id)) {
                        std::string error;
                        if (set_parent(i, c.id, &error)) changed = true;
                        else snprintf(status_line, sizeof status_line, "%s", error.c_str());
                    }
                }
                ImGui::EndCombo();
            }
        }
        parameter_rows(i, changed);
        ImGui::SeparatorText("Assignment");
        if (!i.assigned)
            ImGui::TextDisabled("Not assigned: a preset other instances inherit from.");
        else if (i.key.empty())
            ImGui::Text("Every texture with palette %s%s", i.clut.c_str(), i.w > 0 ? " of its size" : "");
        else
            ImGui::Text("Texture %s %d x %d%s", i.key.c_str(), i.w, i.h, i.clut.empty() ? "" : " (this palette only)");
        if (i.assigned) {
            if (!i.rooms.empty()) ImGui::Text("Scope: %d rooms", (int)i.rooms.size());
            else if (i.zone >= 0) ImGui::Text("Scope: room %d/%d", i.zone, i.room);
            else ImGui::Text("Scope: everywhere");
            bool room = i.zone >= 0;
            if (ImGui::Checkbox("This room only", &room)) {
                int z = -1, r = -1;
                if (room && !emissive::current_room(&z, &r)) z = r = -1;
                i.set_room(z, r), changed = true;
            }
        }
        if (!i.root) {
            if (ImGui::Button("Assign to selection's texture")) {
                std::string error;
                if (assign_selection(i, false, &error)) changed = true;
                else snprintf(status_line, sizeof status_line, "%s", error.c_str());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The texture under the selected surface or model, scoped to this room");
            ImGui::SameLine();
            if (ImGui::Button("...its palette")) {
                std::string error;
                if (assign_selection(i, true, &error)) changed = true;
                else snprintf(status_line, sizeof status_line, "%s", error.c_str());
            }
            if (i.assigned) {
                ImGui::SameLine();
                if (ImGui::Button("Unassign")) i.assigned = false, i.key.clear(), i.clut.clear(), i.w = i.h = 0, changed = true;
            }
        }
        bool review = i.review == "new";
        if (ImGui::Checkbox("Needs review", &review)) i.review = review ? "new" : "", changed = true;
        if (changed) {
            std::string error;
            if (!commit(i, &error)) snprintf(status_line, sizeof status_line, "%s", error.c_str());
            else if (pc_material_preview_select) pc_material_preview_select(i.id.c_str());
        }
        ImGui::SeparatorText("Instances");
        ImGui::SetNextItemWidth(160);
        ImGui::InputTextWithHint("##newid", "id (blank: numbered)", new_id, sizeof new_id);
        ImGui::SameLine();
        if (ImGui::Button("New child")) {
            materials::Instance n = make_instance(new_id, i.id, false);
            std::string error;
            if (commit(n, &error)) selected = n.id, new_id[0] = 0;
            else snprintf(status_line, sizeof status_line, "%s", error.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            materials::Instance n = make_instance(new_id, i.id, true);
            std::string error;
            if (commit(n, &error)) selected = n.id, new_id[0] = 0;
            else snprintf(status_line, sizeof status_line, "%s", error.c_str());
        }
        if (i.user) {
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                materials::remove_pending(i.id);
                snprintf(status_line, sizeof status_line, "%s removed (Save to remove it from your file)", i.id.c_str());
            }
        }
        if (i.assigned) {
            ImGui::SameLine();
            if (ImGui::Button("Open in Details > Material")) {
                material_editor_select(i.id);
                rood_shell::focus_window("Details");
                if (pc_overlay_set_details_tab) pc_overlay_set_details_tab("material");
            }
        }
        if (ImGui::Button("Copy JSON for the catalog")) {
            ImGui::SetClipboardText(materials::instance_json(i).c_str());
            snprintf(status_line, sizeof status_line, "Copied %s", i.id.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

/* RPC material_instance: {action: list (default) | masters | get {id} | set {id, params {name: value |
 * null}, name, where, review} | parent {id, parent} | new {id, parent} | duplicate {id, of} | assign {id,
 * key, size, clut, scope} | assign_selection {id, palette} | unassign {id} | remove {id} | select {id} |
 * save | revert | validate | migrate {out}}. */
extern "C" int pc_material_instance_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    std::string action = str(params, "action", "list");
    std::string id = str(params, "id", selected);
    if (action == "list") {
        int offset = 0, limit = 0;
        if (const Json* v = params.get("offset"); v && v->type == Json::Number) offset = (int)v->number;
        if (const Json* v = params.get("limit"); v && v->type == Json::Number) limit = (int)v->number;
        const Json* m = params.get("masters");
        return reply(list_json(offset, limit, !(m && m->type == Json::Bool && !m->boolean)), out, size);
    }
    if (action == "masters") {
        Json j = Json::object(), masters = Json::array();
        for (const std::string& n : materials::master_names())
            if (const materials::Master* m = materials::master(n)) masters.items.push_back(master_json(*m));
        j.set("masters", masters), j.set("dir", Json::str(materials::masters_dir()));
        return reply(j, out, size);
    }
    if (action == "select") {
        if (!id.empty() && !materials::find_instance(id)) return fail("unknown instance " + id, out, size);
        selected = id;
        if (pc_material_preview_select) pc_material_preview_select(id.c_str());
        return reply(list_json(), out, size);
    }
    if (action == "save") {
        if (!materials::save_pending(&error)) return fail(error, out, size);
        return reply(list_json(), out, size);
    }
    if (action == "revert") { materials::revert_pending(); return reply(list_json(), out, size); }
    if (action == "validate") {
        Json j = Json::object(), problems = Json::array(), bad = Json::array();
        int ok = 0;
        for (const materials::Instance& i : materials::instances()) {
            std::map<std::string, float> v;
            std::string e;
            if (materials::resolve(i.id, v, nullptr, &e)) ok++;
            else bad.items.push_back(Json::str(e));
        }
        for (const std::string& p : materials::catalog_problems()) problems.items.push_back(Json::str(p));
        j.set("resolved", Json::num(ok)), j.set("unresolved", bad), j.set("problems", problems);
        j.set("entries", Json::num((double)materials::catalog_entries().size()));
        return reply(j, out, size);
    }
    if (action == "migrate") {
        std::string path = str(params, "out");
        if (path.empty()) return fail("migrate needs out (a path; never the user layer itself)", out, size);
        int np = 0, ne = 0;
        if (!materials::write_migrated(path, &np, &ne, &error)) return fail(error, out, size);
        Json j = Json::object();
        j.set("out", Json::str(path)), j.set("presets", Json::num(np)), j.set("entries", Json::num(ne));
        return reply(j, out, size);
    }
    if (action == "new" || action == "duplicate") {
        std::string parent = str(params, action == "new" ? "parent" : "of", action == "new" ? "" : id);
        if (!parent.empty() && !materials::find_instance(parent)) return fail("unknown instance " + parent, out, size);
        materials::Instance n = make_instance(str(params, "id"), parent, action == "duplicate");
        if (action == "new" && parent.empty()) n.root = params.get("root") && params.get("root")->type == Json::Bool
            && params.get("root")->boolean;
        if (const Json* p = params.get("params"); p && p->type == Json::Object && !set_params(n, *p, &error))
            return fail(error, out, size);
        if (!commit(n, &error)) return fail(error, out, size);
        selected = n.id;
        return reply(instance_info(*materials::find_instance(n.id)), out, size);
    }
    const materials::Instance* found = materials::find_instance(id);
    if (!found) return fail(id.empty() ? "no instance selected" : "unknown instance " + id, out, size);
    if (action == "get") return reply(instance_info(*found), out, size);
    materials::Instance i = *found;
    if (action == "remove") {
        if (!i.user) return fail(id + " is curated (edit pc/assets/materials/catalog.json)", out, size);
        materials::remove_pending(id);
        if (selected == id) selected.clear();
        return reply(list_json(), out, size);
    }
    if (action == "set") {
        if (const Json* p = params.get("params"); p && p->type == Json::Object && !set_params(i, *p, &error))
            return fail(error, out, size);
        if (params.get("name")) i.name = str(params, "name");
        if (params.get("where")) i.where = str(params, "where");
        if (params.get("review")) i.review = str(params, "review");
    } else if (action == "parent") {
        if (i.root) return fail("a preset has no parent", out, size);
        if (!set_parent(i, str(params, "parent"), &error)) return fail(error, out, size);
    } else if (action == "assign") {
        int w = 0, h = 0;
        if (const Json* s = params.get("size"); s && s->type == Json::Array && s->items.size() == 2)
            w = (int)s->items[0].number, h = (int)s->items[1].number;
        if (!assign(i, str(params, "key"), w, h, str(params, "clut"), params.get("scope"), &error)) return fail(error, out, size);
    } else if (action == "assign_selection") {
        const Json* p = params.get("palette");
        if (!assign_selection(i, p && p->type == Json::Bool && p->boolean, &error)) return fail(error, out, size);
    } else if (action == "unassign") {
        i.assigned = false, i.key.clear(), i.clut.clear(), i.w = i.h = 0;
    } else
        return fail("unknown action " + action, out, size);
    if (i.root && (action == "assign" || action == "assign_selection")) return fail("a preset is not assigned; make a child", out, size);
    if (!commit(i, &error)) return fail(error, out, size);
    if (pc_material_preview_select && selected == i.id) pc_material_preview_select(i.id.c_str());
    return reply(instance_info(*materials::find_instance(i.id)), out, size);
}
