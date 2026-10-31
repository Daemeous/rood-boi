// Click-to-inspect: see editor_inspect.h.
#include "editor_inspect.h"

#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "editor_commands.h"
#include "editor_math.h"
#include "editor_scene.h"
#include "light_prefabs.h" /* prefab::Json */
#include "materials.h"
#include "render.h"
#include "room_warp.h"

extern "C" {
int pc_hw_present_all_lists(uint32_t* lists, int max) __attribute__((weak));
int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick); /* lights.cpp */
int pc_overlay_set_details_tab(const char* name); /* dev_overlay.cpp */
int pc_material_rpc(const char* params, char* out, int size); /* material_editor.cpp */
extern int pc_gl_view_mode, pc_gl_show_depth;
extern int pc_gi_debug __attribute__((weak));
extern int pc_vk_active __attribute__((weak));
extern int pc_room_warp_enemies __attribute__((weak)), pc_room_warp_scripts __attribute__((weak));
}

using prefab::Json;
using rood_selection::Ref;

namespace {

/* ---- the remembered items ---- */

std::map<std::string, rood_inspect::Item> items; /* by ref id */
int item_zone = -1, item_room = -1;
std::unordered_map<uint32_t, int> mesh_actor; /* model mesh → actor slot (actor_native.c) */
struct DrawnModel {
    uint32_t mesh = 0, model = 0;
    int slot = -1;
};
std::vector<DrawnModel> drawn; /* the object lists' models in the presented frame */
int drawn_frame = -1;

bool vulkan() { return &pc_vk_active && pc_vk_active; }

std::string surface_id(const emissive::TexturePick& p)
{
    char text[64];
    snprintf(text, sizeof text, "surface:%d-%d:%x", p.zone, p.room, p.polygon);
    return text;
}
std::string model_id(uint32_t model)
{
    char text[32];
    snprintf(text, sizeof text, "model:%08x", model);
    return text;
}
Ref ref_for(const emissive::TexturePick& p)
{
    if (p.kind == emissive::SURFACE_ROOM)
        return Ref { rood_selection::KIND_SURFACE, surface_id(p) };
    if (p.kind == emissive::SURFACE_MODEL)
        return Ref { rood_selection::KIND_MODEL, model_id(p.model) };
    return Ref();
}

/* The entry that applies to the pick, and which layer it came from. */
void resolve_entry(rood_inspect::Item& it)
{
    const emissive::TexturePick& p = it.pick;
    const materials::Entry* e = materials::find_match(p.key, p.w(), p.h(),
        emissive::pick_palettes(p.clut_key, p.tpage, p.clut), p.zone, p.room);
    it.entry.clear(), it.entry_source = "none", it.entry_name.clear(), it.entry_preset.clear();
    if (!e)
        return;
    it.entry = e->id, it.entry_name = e->name, it.entry_preset = e->preset;
    bool pending = false;
    for (const materials::Entry& q : materials::pending_entries())
        pending |= q.id == e->id;
    it.entry_source = e->preview ? "review preview" : pending ? "unsaved edit" : e->user ? "user layer" : "shipped catalog";
}

/* The polygon's triangles (a room polygon: the quad's two; a model: the picked one) for the outline. */
struct OutlineSearch {
    uint32_t mesh;
    int index;
    std::vector<float>* out;
};
int keep_mesh(void* context, uint32_t mesh) { return ((OutlineSearch*)context)->mesh == mesh; }
void outline_sink(void* context, const float w[9], const uint8_t*, const uint8_t*, unsigned, unsigned, unsigned)
{
    OutlineSearch* s = (OutlineSearch*)context;
    if (s->index >= 0 && render_emit_polygon_index() != s->index)
        return;
    s->out->insert(s->out->end(), w, w + 9);
}
void gather_outline(rood_inspect::Item& it, bool whole_mesh)
{
    it.outline.clear();
    uint32_t lists[64];
    int n = pc_hw_present_all_lists ? pc_hw_present_all_lists(lists, 64) : 0;
    OutlineSearch s { it.pick.mesh, whole_mesh ? -1 : it.pick.polygon_index, &it.outline };
    for (int i = 0; i < n && it.outline.size() < 9 * 4096; i++)
        if (render_list_triangles_where(lists[i], keep_mesh, outline_sink, &s) > 0)
            it.list = render_list_objects(lists[i]);
    if (it.outline.empty())
        it.outline.assign(it.pick.triangle, it.pick.triangle + 9);
}

bool fill(const emissive::TexturePick& pick, rood_inspect::Item& it)
{
    emissive::TexturePick p = pick; /* a copy: the caller may pass it.pick */
    it = rood_inspect::Item();
    it.pick = p;
    it.ref = ref_for(p);
    if (!it.ref)
        return false;
    int kind;
    unsigned offset;
    uint32_t model;
    if (emissive::mesh_detail(p.mesh, &kind, &offset, &it.tris, &it.total, &model) && kind == emissive::SURFACE_MODEL)
        it.tris = it.total = (unsigned)std::max(0, render_mesh_polygons(p.mesh));
    it.list = p.object_list ? 1 : 0; /* gather_outline refines it from the list that has the mesh */
    auto a = mesh_actor.find(p.mesh);
    it.actor_slot = a == mesh_actor.end() ? -1 : a->second;
    resolve_entry(it);
    gather_outline(it, p.kind == emissive::SURFACE_MODEL);
    item_zone = p.zone, item_room = p.room;
    items[it.ref.id] = it;
    return true;
}

/* ---- the models drawn now ---- */

int note_model(void* context, uint32_t mesh)
{
    std::vector<DrawnModel>* out = (std::vector<DrawnModel>*)context;
    for (const DrawnModel& d : *out)
        if (d.mesh == mesh)
            return 0;
    int kind;
    unsigned offset, tris, total;
    uint32_t model = 0;
    if (!emissive::mesh_detail(mesh, &kind, &offset, &tris, &total, &model) || kind != emissive::SURFACE_MODEL)
        return 0;
    auto a = mesh_actor.find(mesh);
    out->push_back({ mesh, model, a == mesh_actor.end() ? -1 : a->second });
    return 0; /* nothing transformed */
}
void drop_sink(void*, const float*, const uint8_t*, const uint8_t*, unsigned, unsigned, unsigned) { }
void refresh_drawn()
{
    int f = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    if (f == drawn_frame && f >= 0)
        return;
    drawn_frame = f;
    drawn.clear();
    uint32_t lists[64];
    int n = pc_hw_present_all_lists ? pc_hw_present_all_lists(lists, 64) : 0;
    for (int i = 0; i < n; i++)
        if (render_list_objects(lists[i]) == 1)
            render_list_triangles_where(lists[i], note_model, drop_sink, &drawn);
}
std::string model_label(uint32_t model, int slot)
{
    char text[48];
    if (slot == 0)
        return "Ashley";
    if (slot > 0)
        snprintf(text, sizeof text, "Actor %d", slot);
    else
        snprintf(text, sizeof text, "Model %08x", model);
    return text;
}

/* ---- the texture preview: nearest-sampled texel rectangles (no renderer upload: the same on GL and Vulkan) ---- */

struct Preview {
    std::string key;
    unsigned tpage = 0, clut = 0;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
} preview;
void draw_preview(const emissive::TexturePick& p, float width)
{
    if (preview.key != p.key || preview.tpage != p.tpage || preview.clut != p.clut) {
        preview.key = p.key, preview.tpage = p.tpage, preview.clut = p.clut, preview.w = p.w(), preview.h = p.h();
        preview.rgba = emissive::texture_rgba_box(p.tpage, p.clut, p.box);
    }
    if (preview.w <= 0 || preview.h <= 0 || preview.rgba.size() < (size_t)(preview.w * preview.h * 4))
        return;
    int cells_w = std::min(preview.w, 64), cells_h = std::min(preview.h, 64);
    float cell = floorf(width / (float)cells_w);
    if (cell < 1)
        cell = 1;
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(at, ImVec2(at.x + cell * cells_w, at.y + cell * cells_h), IM_COL32(40, 40, 40, 255));
    for (int y = 0; y < cells_h; y++)
        for (int x = 0; x < cells_w; x++) {
            int sx = x * preview.w / cells_w, sy = y * preview.h / cells_h; /* nearest */
            const uint8_t* t = &preview.rgba[(size_t)(sy * preview.w + sx) * 4];
            if (t[3] < 8)
                continue;
            d->AddRectFilled(ImVec2(at.x + x * cell, at.y + y * cell), ImVec2(at.x + (x + 1) * cell, at.y + (y + 1) * cell),
                IM_COL32(t[0], t[1], t[2], 255));
        }
    ImGui::Dummy(ImVec2(cell * cells_w, cell * cells_h));
}

/* ---- where used: the rollout survey's rooms ---- */

struct SurveyRoom {
    int zone, room;
    std::string name;
    std::vector<std::pair<std::string, float>> keys, cluts; /* texture key → share, palette key → share */
};
std::vector<SurveyRoom> survey;
bool survey_loaded = false;
std::string survey_status;
std::string rollout_dir()
{
    const char* e = getenv("ROOD_MATERIAL_ROLLOUT");
    if (e && *e)
        return e;
    const char* r = getenv("ROOD_MATERIAL_REVIEW");
    if (r && *r)
        return std::filesystem::path(r).parent_path().string();
    return "pc/build/material_rollout";
}
bool read_text(const std::string& path, std::string& text)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    fclose(f);
    return true;
}
void load_survey()
{
    if (survey_loaded)
        return;
    survey_loaded = true;
    std::string dir = rollout_dir();
    std::map<std::pair<int, int>, std::string> names;
    std::string text;
    Json j;
    if (read_text(dir + "/rooms.json", text) && prefab::json_parse(text.c_str(), j) && j.type == Json::Array)
        for (const Json& r : j.items) {
            const Json *z = r.get("zone"), *m = r.get("room"), *n = r.get("name"), *a = r.get("area");
            if (z && m)
                names[{ (int)z->number, (int)m->number }] = n && !n->string.empty() ? n->string : a ? a->string : "";
        }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir + "/rooms", ec)) {
        if (entry.path().extension() != ".json")
            continue;
        text.clear();
        if (!read_text(entry.path().string(), text) || !prefab::json_parse(text.c_str(), j) || j.type != Json::Object)
            continue;
        const Json* room = j.get("room");
        const Json* textures = j.get("textures");
        if (!room || room->type != Json::Array || room->items.size() != 2 || !textures)
            continue;
        SurveyRoom s;
        s.zone = (int)room->items[0].number, s.room = (int)room->items[1].number;
        auto nm = names.find({ s.zone, s.room });
        if (nm != names.end())
            s.name = nm->second;
        std::map<std::string, float> cluts;
        for (const Json& t : textures->items) {
            const Json *k = t.get("key"), *c = t.get("clut"), *sh = t.get("share");
            float share = sh ? (float)sh->number : 0;
            if (k && k->type == Json::String)
                s.keys.push_back({ k->string, share });
            if (c && c->type == Json::String && !c->string.empty())
                cluts[c->string] += share;
        }
        for (auto& [c, share] : cluts)
            s.cluts.push_back({ c, share });
        survey.push_back(std::move(s));
    }
    std::sort(survey.begin(), survey.end(), [](const SurveyRoom& a, const SurveyRoom& b) {
        return a.zone != b.zone ? a.zone < b.zone : a.room < b.room;
    });
    survey_status = survey.empty() ? "no survey data in " + dir + " (pc/tools/material_rollout.py writes it)" : "";
}

/* ---- view modes ---- */

const std::vector<rood_inspect::ViewMode>& modes()
{
    static const std::vector<rood_inspect::ViewMode> m = {
        { "lit", "Lit", false },
        { "unlit", "Unlit", false },
        { "lighting", "Lighting only", false },
        { "base_colour", "Base colour", false },
        { "vertex_colours", "Vertex colours", false },
        { "normals", "World normals", false },
        { "roughness", "Roughness", true },
        { "metal", "Metal", true },
        { "emissive", "Emissive", true },
        { "ao", "Ambient occlusion", true },
        { "wireframe", "Wireframe", false },
    };
    return m;
}
/* The shader's value per name (scaled.frag view_mode; GL has 0-5 and 11). */
int shader_value(const std::string& name)
{
    static const struct {
        const char* name;
        int value;
    } table[] = { { "lit", 0 }, { "unlit", 1 }, { "lighting", 2 }, { "base_colour", 3 }, { "vertex_colours", 4 },
        { "normals", 5 }, { "roughness", 13 }, { "metal", 14 }, { "emissive", 15 }, { "ao", 16 }, { "wireframe", 0 } };
    for (const auto& t : table)
        if (name == t.name)
            return t.value;
    return -1;
}

/* ---- JSON ---- */

std::string quote(const std::string& s) { return prefab::json_quote(s); }
Json item_json(const rood_inspect::Item& it)
{
    Json j = Json::object();
    j.set("valid", Json::boolean_(true));
    j.set("id", Json::str(it.ref.id));
    j.set("kind", Json::str(rood_selection::kind_name(it.ref.kind)));
    j.set("label", Json::str(rood_inspect::label(it.ref)));
    Json room = Json::array();
    room.items.push_back(Json::num(it.pick.zone)), room.items.push_back(Json::num(it.pick.room));
    j.set("room", room);
    if (it.ref.kind == rood_selection::KIND_SURFACE)
        j.set("polygon", Json::num(it.pick.polygon));
    else
        j.set("model", Json::str(emissive::model_key(it.pick.model)));
    j.set("actor_slot", Json::num(it.actor_slot));
    j.set("triangles", Json::num(it.tris));
    j.set("polygons", Json::num(it.total));
    j.set("list", Json::str(it.list == 1 ? "object" : it.list == 2 ? "block" : "room"));
    Json w = Json::array();
    for (float v : it.pick.world)
        w.items.push_back(Json::num(round(v * 10) / 10));
    j.set("world", w);
    j.set("key", Json::str(it.pick.key));
    Json size = Json::array();
    size.items.push_back(Json::num(it.pick.w())), size.items.push_back(Json::num(it.pick.h()));
    j.set("size", size);
    j.set("clut_key", Json::str(it.pick.clut_key));
    j.set("tpage", Json::num(it.pick.tpage)), j.set("clut", Json::num(it.pick.clut));
    Json box = Json::array();
    for (uint8_t b : it.pick.box)
        box.items.push_back(Json::num(b));
    j.set("box", box);
    j.set("entry", Json::str(it.entry));
    j.set("entry_source", Json::str(it.entry_source));
    j.set("entry_name", Json::str(it.entry_name));
    j.set("entry_preset", Json::str(it.entry_preset));
    Json maps = Json::array();
    for (const materials::Present& p : materials::channels(it.pick.key))
        maps.items.push_back(Json::str(p.name + (p.user ? " (user)" : p.baked ? " (baked)" : " (shipped)")));
    j.set("maps", maps);
    j.set("shader", Json::str(vulkan() ? "scaled.frag (Vulkan)" : "gl_present.c scaled program (GL)"));
    j.set("selected", Json::boolean_(rood_selection::contains(it.ref)));
    return j;
}
int reply(const Json& j, char* out, int size)
{
    std::string s = prefab::json_write(j);
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end()); /* one line: the RPC is line-based */
    if ((int)s.size() >= size)
        return snprintf(out, size, "reply too long"), -1;
    memcpy(out, s.c_str(), s.size() + 1);
    return 0;
}
int fail(const std::string& m, char* out, int size)
{
    snprintf(out, size, "%s", m.c_str());
    return -1;
}
} // namespace

/* ---- picking and the items ---- */

bool rood_inspect::pick_world(const float p[3], Item& out)
{
    emissive::TexturePick t;
    return emissive::pick_texture_world(p, t) && fill(t, out);
}
bool rood_inspect::pick(float x, float y, Item& out)
{
    emissive::TexturePick t;
    return emissive::pick_texture(x, y, t) && fill(t, out);
}
Ref rood_inspect::click(float x, float y, rood_selection::Source s)
{
    Item it;
    if (!pick(x, y, it))
        return Ref();
    rood_selection::set(it.ref, s);
    rood_scene::publish();
    return it.ref;
}
const rood_inspect::Item* rood_inspect::item(const Ref& r)
{
    auto it = items.find(r.id);
    return it == items.end() ? nullptr : &it->second;
}
bool rood_inspect::exists(const Ref& r)
{
    if (r.kind == rood_selection::KIND_MODEL && !item(r)) {
        /* A model drawn now (an Outliner row, RPC set by id) that was never picked: its item from the mesh. */
        refresh_drawn();
        for (const DrawnModel& m : drawn)
            if (model_id(m.model) == r.id) {
                emissive::TexturePick t;
                t.valid = true, t.kind = emissive::SURFACE_MODEL, t.model = m.model, t.mesh = m.mesh, t.object_list = 1;
                emissive::current_room(&t.zone, &t.room);
                Item it;
                fill(t, it);
                break;
            }
    }
    return (r.kind == rood_selection::KIND_SURFACE || r.kind == rood_selection::KIND_MODEL) && item(r);
}
std::string rood_inspect::label(const Ref& r)
{
    const Item* it = item(r);
    if (!it)
        return r.id;
    if (r.kind == rood_selection::KIND_MODEL)
        return model_label(it->pick.model, it->actor_slot);
    char text[64];
    snprintf(text, sizeof text, "Surface 0x%x", it->pick.polygon);
    return text;
}
bool rood_inspect::position(const Ref& r, float out[3], float* radius)
{
    const Item* it = item(r);
    if (!it)
        return false;
    for (int k = 0; k < 3; k++)
        out[k] = (it->pick.triangle[k] + it->pick.triangle[3 + k] + it->pick.triangle[6 + k]) / 3;
    float d = 0;
    for (int k = 0; k < 3; k++)
        d = fmaxf(d, fabsf(it->pick.triangle[k] - out[k]));
    *radius = fmaxf(d * 2, r.kind == rood_selection::KIND_MODEL ? 200.0f : 100.0f);
    return true;
}
std::vector<Ref> rood_inspect::refs()
{
    std::vector<Ref> out;
    for (auto& [id, it] : items)
        out.push_back(it.ref);
    return out;
}
bool rood_inspect::escape_clears()
{
    Ref p = rood_selection::primary();
    if (p.kind != rood_selection::KIND_SURFACE && p.kind != rood_selection::KIND_MODEL)
        return false;
    rood_selection::clear(rood_selection::FROM_VIEWPORT);
    rood_scene::publish();
    return true;
}
void rood_inspect::frame(int zone, int room)
{
    if (!items.empty() && (zone != item_zone || room != item_room)) {
        items.clear();
        item_zone = zone, item_room = room;
    }
}
extern "C" void pc_inspect_model_actor(uint32_t mesh, int slot)
{
    if (slot >= 0)
        mesh_actor[mesh] = slot;
    else
        mesh_actor.erase(mesh);
}

/* ---- the view ---- */

void rood_inspect::draw_highlight(const pc_overlay_view& v)
{
    Ref locked = rood_selection::locked();
    Ref p = locked ? locked : rood_selection::primary();
    Item* it = nullptr;
    if (auto f = items.find(p.id); p && f != items.end())
        it = &f->second;
    if (!it)
        return;
    if (it->ref.kind == rood_selection::KIND_MODEL)
        gather_outline(*it, true); /* it moves: this frame's pose */
    ImDrawList* d = ImGui::GetForegroundDrawList();
    const ImU32 line = IM_COL32(255, 160, 40, 230), fill = IM_COL32(255, 160, 40, 50);
    for (size_t t = 0; t + 9 <= it->outline.size(); t += 9) {
        float px[3][2];
        bool ok = true;
        for (int c = 0; c < 3 && ok; c++)
            ok = rood_math::project(v.view, v.projection, v.rect, &it->outline[t + c * 3], px[c]);
        if (!ok)
            continue;
        ImVec2 a(px[0][0], px[0][1]), b(px[1][0], px[1][1]), cpt(px[2][0], px[2][1]);
        if (it->ref.kind == rood_selection::KIND_SURFACE)
            d->AddTriangleFilled(a, b, cpt, fill);
        d->AddTriangle(a, b, cpt, line, 1.5f);
    }
}

/* ---- Details ---- */

bool rood_inspect::open_material(const Ref& r, std::string* error)
{
    const Item* it = item(r);
    if (!it) {
        *error = "nothing inspected";
        return false;
    }
    char params[160], out[4096];
    snprintf(params, sizeof params, "{\"action\":\"pick\",\"world\":[%g,%g,%g]}", it->pick.world[0], it->pick.world[1],
        it->pick.world[2]);
    if (pc_material_rpc(params, out, sizeof out) != 0) {
        *error = out;
        return false;
    }
    pc_overlay_set_details_tab("material");
    return true;
}
namespace {
void row(const char* name, const char* fmt, ...)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", name);
    ImGui::TableNextColumn();
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    ImGui::TextWrapped("%s", text);
}
std::string where_status;
} // namespace
void rood_inspect::details(const Ref& subject)
{
    const Item* it = item(subject);
    if (!it) {
        ImGui::TextDisabled("Not in this room any more");
        return;
    }
    const emissive::TexturePick& p = it->pick;
    ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp;
    if (ImGui::CollapsingHeader("What", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("what", 2, tf)) {
        row("Kind", "%s", subject.kind == rood_selection::KIND_SURFACE ? "Room surface" : it->actor_slot >= 0 ? "Character" : "Model (prop, weapon or character)");
        row("Room", "%d/%d", p.zone, p.room);
        if (subject.kind == rood_selection::KIND_SURFACE)
            row("Polygon", "0x%x in the room data", p.polygon);
        else
            row("Model", "%s%s", emissive::model_key(p.model).c_str(), it->actor_slot == 0 ? " (Ashley)" : "");
        if (it->actor_slot >= 0)
            row("Actor slot", "%d", it->actor_slot);
        row("Picked at", "%.0f %.0f %.0f", p.world[0], p.world[1], p.world[2]);
        ImGui::EndTable();
    }
    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("mesh", 2, tf)) {
        row("List", "%s", it->list == 1 ? "object (characters, weapons, props)" : it->list == 2 ? "layered room block" : "room");
        if (subject.kind == rood_selection::KIND_SURFACE)
            row("Group", "%u triangles, %u polygons", it->tris, it->total - it->tris);
        else
            row("Polygons", "%u", it->total);
        ImGui::EndTable();
    }
    if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("texture", 2, tf)) {
            row("Key", "%s", p.key.c_str());
            row("Size", "%d x %d", p.w(), p.h());
            row("Palette", "%s", p.clut_key.empty() ? "(15-bit page)" : p.clut_key.c_str());
            row("VRAM", "page %u, CLUT %u, box %u %u %u %u", p.tpage, p.clut, p.box[0], p.box[1], p.box[2], p.box[3]);
            ImGui::EndTable();
        }
        draw_preview(p, fminf(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 10));
    }
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("material", 2, tf)) {
            if (it->entry.empty())
                row("Entry", "none (the default surface)");
            else {
                row("Entry", "%s%s%s", it->entry.c_str(), it->entry_name.empty() ? "" : " ", it->entry_name.c_str());
                row("Preset", "%s", it->entry_preset.empty() ? "-" : it->entry_preset.c_str());
            }
            row("From", "%s", it->entry_source.c_str());
            std::string maps;
            for (const materials::Present& m : materials::channels(p.key))
                maps += (maps.empty() ? "" : ", ") + m.name + (m.user ? " (user)" : m.baked ? " (baked)" : "");
            row("Maps", "%s", maps.empty() ? "none" : maps.c_str());
            row("Shader", "%s", vulkan() ? "scaled.frag (Vulkan)" : "scaled program, gl_present.c (GL)");
            ImGui::EndTable();
        }
        if (ImGui::Button(it->entry.empty() ? "New material for this texture" : "Open in Material editor")) {
            std::string error;
            if (!open_material(subject, &error))
                where_status = error;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Details > Material on this texture");
    }
    if (ImGui::CollapsingHeader("Where used", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string status;
        std::vector<Use> uses = where_used(p.key, p.clut_key, &status);
        if (!status.empty())
            ImGui::TextWrapped("%s", status.c_str());
        if (ImGui::BeginTable("uses", 3, tf | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Share");
            ImGui::TableSetupColumn("##go", ImGuiTableColumnFlags_WidthFixed);
            for (const Use& u : uses) {
                ImGui::PushID(u.zone * 1000 + u.room);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d/%d %s%s", u.zone, u.room, u.name.c_str(), u.live ? " (here)" : "");
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", u.share * 100);
                ImGui::TableNextColumn();
                bool here = u.zone == p.zone && u.room == p.room;
                ImGui::BeginDisabled(here);
                if (ImGui::SmallButton("Go")) {
                    std::string why;
                    if (!go(u.zone, u.room, &why))
                        where_status = why;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!where_status.empty())
            ImGui::TextWrapped("%s", where_status.c_str());
    }
}

/* ---- the Outliner ---- */

void rood_inspect::outliner(const char* filter)
{
    refresh_drawn();
    bool any = false;
    for (const DrawnModel& m : drawn) {
        Ref r { rood_selection::KIND_MODEL, model_id(m.model) };
        std::string text = model_label(m.model, m.slot);
        if (filter && *filter && text.find(filter) == std::string::npos)
            continue;
        any = true;
        ImGui::PushID(r.id.c_str());
        if (ImGui::Selectable(text.c_str(), rood_selection::contains(r))) {
            exists(r); /* not picked yet: its item from the mesh */
            rood_scene::row_clicked(r);
        }
        rood_scene::row_drawn(r);
        ImGui::PopID();
    }
    for (auto& [id, it] : items) {
        if (it.ref.kind != rood_selection::KIND_SURFACE)
            continue;
        std::string text = label(it.ref) + " (" + it.pick.key + ")";
        if (filter && *filter && text.find(filter) == std::string::npos)
            continue;
        any = true;
        ImGui::PushID(id.c_str());
        if (ImGui::Selectable(text.c_str(), rood_selection::contains(it.ref)))
            rood_scene::row_clicked(it.ref);
        rood_scene::row_drawn(it.ref);
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextDisabled("Click a surface or a character in the view");
}

/* ---- where used ---- */

std::vector<rood_inspect::Use> rood_inspect::where_used(const std::string& key, const std::string& clut, std::string* status)
{
    load_survey();
    if (status)
        *status = survey_status;
    std::vector<Use> out;
    for (const SurveyRoom& s : survey) {
        const auto& list = key.empty() ? s.cluts : s.keys;
        float share = 0;
        bool found = false;
        for (const auto& [k, sh] : list)
            if (k == (key.empty() ? clut : key))
                found = true, share += sh;
        if (found)
            out.push_back({ s.zone, s.room, s.name, share, false });
    }
    int z = -1, r = -1;
    bool live = false;
    emissive::current_room(&z, &r);
    for (const emissive::UseInfo& u : emissive::use_list(true))
        if (key.empty() ? emissive::palette_is(u.page, u.clut, clut) : u.key == key)
            live = true;
    if (live) {
        bool listed = false;
        for (Use& u : out)
            if (u.zone == z && u.room == r)
                u.live = true, listed = true;
        if (!listed)
            out.insert(out.begin(), Use { z, r, "", 0, true });
    }
    return out;
}
bool rood_inspect::go(int zone, int room, std::string* why)
{
    char text[200] = "";
    int enemies = &pc_room_warp_enemies ? pc_room_warp_enemies : 0, scripts = &pc_room_warp_scripts ? pc_room_warp_scripts : 0;
    if (!pc_room_browser_go_anywhere(zone, room, enemies, scripts, text, sizeof text)) {
        if (why)
            *why = text[0] ? text : "the warp was refused";
        return false;
    }
    return true;
}

/* ---- view modes ---- */

const std::vector<rood_inspect::ViewMode>& rood_inspect::view_modes() { return modes(); }
std::string rood_inspect::view_mode()
{
    if (pc_gl_show_depth || (&pc_gi_debug && pc_gi_debug))
        return "";
    if (pc_overlay_wire && pc_overlay_actor_wire && pc_gl_view_mode == 0)
        return "wireframe";
    for (const ViewMode& m : modes())
        if (shader_value(m.name) == pc_gl_view_mode && strcmp(m.name, "wireframe"))
            return m.name;
    return "";
}
bool rood_inspect::set_view_mode(const std::string& name)
{
    int value = shader_value(name);
    if (value < 0)
        return false;
    pc_gl_view_mode = value;
    pc_gl_show_depth = 0;
    if (&pc_gi_debug)
        pc_gi_debug = 0;
    bool wire = name == "wireframe";
    pc_overlay_wire = pc_overlay_actor_wire = wire;
    return true;
}
void rood_inspect::register_commands()
{
    for (const ViewMode& m : modes()) {
        std::string name = m.name;
        rood_commands::Command c;
        c.id = "view.mode." + name;
        c.title = std::string("View mode: ") + m.title + (m.vulkan_only ? " (Vulkan)" : "");
        c.category = "View";
        c.run = [name] { set_view_mode(name); };
        c.checked = [name] { return view_mode() == name; };
        rood_commands::add(c);
    }
}
void rood_inspect::view_mode_menu()
{
    if (!ImGui::BeginMenu("View mode"))
        return;
    std::string current = view_mode();
    for (const ViewMode& m : modes()) {
        std::string title = std::string(m.title) + (m.vulkan_only && !vulkan() ? " (Vulkan only)" : "");
        if (ImGui::MenuItem(title.c_str(), nullptr, current == m.name))
            set_view_mode(m.name);
    }
    ImGui::EndMenu();
}

/* ---- RPC ---- */

extern "C" int pc_editor_inspect_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    rood_scene::frame();
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "state";
    auto flag = [&](const char* key, bool def) {
        const Json* j = params.get(key);
        return j ? (j->type == Json::Bool ? j->boolean : j->number != 0) : def;
    };
    auto str = [&](const char* key) {
        const Json* j = params.get(key);
        return j && j->type == Json::String ? j->string : std::string();
    };
    if (action == "pick") {
        rood_inspect::Item it;
        bool ok;
        if (const Json* w = params.get("world"); w && w->type == Json::Array && w->items.size() == 3) {
            float p3[3] = { (float)w->items[0].number, (float)w->items[1].number, (float)w->items[2].number };
            ok = rood_inspect::pick_world(p3, it);
        } else {
            pc_overlay_view v;
            if (!pc_lights_tools_view(&v, nullptr))
                return fail("pick needs an editor view (hardware renderer and a room)", out, size);
            float x = v.rect[0] + v.rect[2] * 0.5f, y = v.rect[1] + v.rect[3] * 0.5f;
            if (const Json* u = params.get("u"))
                x = v.rect[0] + v.rect[2] * (float)u->number;
            if (const Json* j = params.get("v"))
                y = v.rect[1] + v.rect[3] * (float)j->number;
            if (const Json* j = params.get("x"))
                x = (float)j->number;
            if (const Json* j = params.get("y"))
                y = (float)j->number;
            ok = rood_inspect::pick(x, y, it);
        }
        if (!ok) {
            Json j = Json::object();
            j.set("valid", Json::boolean_(false));
            return reply(j, out, size);
        }
        if (flag("select", true)) {
            rood_selection::set(it.ref, rood_selection::FROM_RPC);
            rood_scene::publish();
        }
        return reply(item_json(*rood_inspect::item(it.ref)), out, size);
    }
    if (action == "state") {
        Ref locked = rood_selection::locked();
        Ref p = locked ? locked : rood_selection::primary();
        std::string id = str("id");
        if (!id.empty())
            p = rood_selection::ref_of(id);
        const rood_inspect::Item* it = rood_inspect::item(p);
        if (!it) {
            Json j = Json::object();
            j.set("valid", Json::boolean_(false));
            j.set("primary", p ? Json::str(p.id) : Json());
            return reply(j, out, size);
        }
        return reply(item_json(*it), out, size);
    }
    if (action == "items") {
        Json j = Json::object(), list = Json::array();
        for (const Ref& r : rood_inspect::refs())
            list.items.push_back(item_json(*rood_inspect::item(r)));
        j.set("items", list);
        return reply(j, out, size);
    }
    if (action == "outliner") {
        refresh_drawn();
        Json j = Json::object(), rows = Json::array();
        for (const DrawnModel& m : drawn) {
            Json r = Json::object();
            Ref ref { rood_selection::KIND_MODEL, model_id(m.model) };
            r.set("id", Json::str(ref.id)), r.set("label", Json::str(model_label(m.model, m.slot)));
            r.set("actor_slot", Json::num(m.slot));
            r.set("selected", Json::boolean_(rood_selection::contains(ref)));
            rows.items.push_back(r);
        }
        for (const Ref& ref : rood_inspect::refs()) {
            if (ref.kind != rood_selection::KIND_SURFACE)
                continue;
            Json r = Json::object();
            r.set("id", Json::str(ref.id)), r.set("label", Json::str(rood_inspect::label(ref)));
            r.set("selected", Json::boolean_(rood_selection::contains(ref)));
            rows.items.push_back(r);
        }
        j.set("rows", rows);
        return reply(j, out, size);
    }
    if (action == "where_used") {
        std::string key = str("key"), clut = str("clut");
        if (key.empty() && clut.empty()) {
            const rood_inspect::Item* it = rood_inspect::item(rood_selection::primary());
            if (!it)
                return fail("where_used needs key or clut, or an inspected selection", out, size);
            key = it->pick.key, clut = it->pick.clut_key;
        }
        std::string status;
        Json j = Json::object(), rooms = Json::array();
        for (const rood_inspect::Use& u : rood_inspect::where_used(key, clut, &status)) {
            Json r = Json::object();
            r.set("zone", Json::num(u.zone)), r.set("room", Json::num(u.room)), r.set("name", Json::str(u.name));
            r.set("share", Json::num(round(u.share * 10000) / 10000)), r.set("live", Json::boolean_(u.live));
            rooms.items.push_back(r);
        }
        j.set("rooms", rooms), j.set("status", Json::str(status)), j.set("key", Json::str(key)), j.set("clut", Json::str(clut));
        return reply(j, out, size);
    }
    if (action == "go") {
        const Json *z = params.get("zone"), *r = params.get("room");
        if (!z || !r)
            return fail("go needs zone and room", out, size);
        std::string why;
        if (!rood_inspect::go((int)z->number, (int)r->number, &why))
            return fail("go: " + why, out, size);
        Json j = Json::object();
        j.set("going", Json::boolean_(true));
        return reply(j, out, size);
    }
    if (action == "open_material") {
        Ref p = rood_selection::primary();
        std::string id = str("id");
        if (!id.empty())
            p = rood_selection::ref_of(id);
        if (!rood_inspect::open_material(p, &error))
            return fail("open_material: " + error, out, size);
        Json j = Json::object();
        j.set("opened", Json::boolean_(true));
        return reply(j, out, size);
    }
    if (action == "view_modes" || action == "view_mode") {
        std::string previous = rood_inspect::view_mode(), name = str("name");
        if (action == "view_mode" && !name.empty() && !rood_inspect::set_view_mode(name))
            return fail("no view mode " + name, out, size);
        Json j = Json::object(), list = Json::array();
        for (const rood_inspect::ViewMode& m : rood_inspect::view_modes()) {
            Json v = Json::object();
            v.set("name", Json::str(m.name)), v.set("title", Json::str(m.title)), v.set("vulkan_only", Json::boolean_(m.vulkan_only));
            v.set("available", Json::boolean_(!m.vulkan_only || vulkan()));
            list.items.push_back(v);
        }
        j.set("modes", list), j.set("previous", Json::str(previous)), j.set("current", Json::str(rood_inspect::view_mode()));
        j.set("shader_value", Json::num(pc_gl_view_mode));
        j.set("wireframe", Json::boolean_(pc_overlay_wire && pc_overlay_actor_wire));
        return reply(j, out, size);
    }
    return fail("unknown action " + action, out, size);
}
