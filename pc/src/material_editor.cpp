// Material response editor: the dev overlay's Details > Material panel and
// RPC material (pc/docs/Materials.md). Modelled on the emissive panel
// (emissive_editor.cpp), whose texture picker it shares: pick a texture in
// the view, see and edit its catalog entry (preset, roughness, specular,
// metal, relief) live, paint its roughness, height and metal maps, preview
// the height estimate and look at the debug views (relief normals,
// cavities), save the entry into the user layer (materials.json) or copy it
// as JSON for the curated catalog.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

#include "imgui.h"
#include "emissive.h"
#include "emissive_internal.h"
#include "materials.h"
#include "material_survey.h"

using prefab::Json;
using emissive::TexturePick;

extern "C" int pc_gl_view_mode; /* the present pass's debug views (scaled.frag) */

namespace {

TexturePick picked;
bool pick_armed;
std::string selected; /* entry id */
char status_line[512];

/* The brush (the emissive editor's tools) on one channel at a time. */
enum Channel { ROUGHNESS, HEIGHT, METAL, CHANNEL_COUNT };
const char* const channel_names[CHANNEL_COUNT] = { "roughness", "height", "metal" };
int channel = ROUGHNESS;
enum class Tool { Pencil, Brush, Eraser, Rectangle, Fill };
const char* const tool_names[] = { "pencil", "brush", "eraser", "rectangle", "fill" };
Tool tool = Tool::Pencil;
float brush_value = 128, brush_radius = 1.5f, brush_hardness = 0.5f, brush_opacity = 1.0f, reference = 0.7f;
struct Stroke {
    std::string key, channel;
    int w = 0, h = 0;
    std::vector<uint8_t> before;
};
std::vector<Stroke> undo_stack;
bool stroking;
int stroke_from[2];
float zoom = 0; /* pixels per texel (0: fit) */

const materials::Entry* find_entry(const std::string& id, std::vector<materials::Entry>& all)
{
    all = materials::catalog_entries();
    for (const materials::Entry& e : all)
        if (e.id == id)
            return &e;
    return nullptr;
}
std::string new_id(const std::vector<materials::Entry>& all)
{
    for (int n = 1;; n++) {
        char id[16];
        snprintf(id, sizeof id, "U%03d", n);
        bool used = false;
        for (const materials::Entry& e : all)
            used |= e.id == id;
        if (!used)
            return id;
    }
}
/* A new user entry for a picked texture: this palette, this room, a fully
 * rough dielectric (no visible change until edited). */
materials::Entry entry_for(const TexturePick& p, const std::vector<materials::Entry>& all)
{
    materials::Entry e;
    e.id = new_id(all);
    e.name = "New material";
    e.key = p.key, e.w = p.w(), e.h = p.h();
    e.clut = p.clut_key;
    e.zone = p.zone, e.room = p.room;
    char where[96];
    snprintf(where, sizeof where, "zone %d room %d", p.zone, p.room);
    e.where = where;
    e.review = "new";
    e.value = materials::Surface();
    e.value.specular = 0.35f;
    e.value.roughness = 0.8f;
    e.explicit_fields = 7;
    e.user = true;
    return e;
}
/* The entry's fractions from its preset again (explicit fields dropped). */
void apply_preset(materials::Entry& e, const std::string& name)
{
    e.preset = name;
    for (const auto& p : materials::catalog_presets())
        if (p.first == name) {
            e.value = p.second;
            e.explicit_fields = 0;
        }
}

/* ---------------------------------------------------------------- the maps */

/* What a texel is without a map: the entry's roughness or metal (x 255), height 128 (flat). */
uint8_t channel_default(int ch, const materials::Entry* e)
{
    float v = ch == ROUGHNESS ? (e ? e->value.roughness : 1.0f) : ch == METAL ? (e ? e->value.metal : 0.0f) : -1.0f;
    return v < 0 ? materials::blank_value(channel_names[ch]) : (uint8_t)std::clamp((int)lroundf(v * 255), 0, 255);
}
std::vector<uint8_t> map_or_default(const std::string& key, int w, int h, uint8_t fill, int ch = -1)
{
    const materials::Channel* c = materials::channel(key, channel_names[ch < 0 ? channel : ch]);
    if (c && c->w == w && c->h == h)
        return c->values;
    return std::vector<uint8_t>((size_t)w * h, fill);
}
bool write_map(const std::string& key, int w, int h, const std::vector<uint8_t>& values, bool undoable,
    const char* name = nullptr)
{
    if (!name)
        name = channel_names[channel];
    if (undoable) {
        Stroke s;
        s.key = key, s.channel = name, s.w = w, s.h = h;
        const materials::Channel* c = materials::channel(key, name);
        if (c && c->w == w && c->h == h)
            s.before = c->values; /* else empty: undo leaves no map again */
        undo_stack.push_back(s);
        if (undo_stack.size() > 64)
            undo_stack.erase(undo_stack.begin());
    }
    std::string error;
    if (!materials::write_channel(key, name, w, h, values.data(), &error)) {
        snprintf(status_line, sizeof status_line, "%s", error.c_str());
        return false;
    }
    return true;
}
bool undo_map()
{
    if (undo_stack.empty())
        return false;
    Stroke s = undo_stack.back();
    undo_stack.pop_back();
    if (s.before.empty()) {
        std::string path = materials::user_dir() + "/" + s.key + "/" + s.channel + ".png";
        remove(path.c_str());
        materials::forget();
        return true;
    }
    return write_map(s.key, s.w, s.h, s.before, false, s.channel.c_str());
}
void dab(std::vector<uint8_t>& v, int w, int h, float cx, float cy, float target)
{
    int r = (int)ceilf(brush_radius);
    for (int y = (int)floorf(cy) - r; y <= (int)floorf(cy) + r; y++)
        for (int x = (int)floorf(cx) - r; x <= (int)floorf(cx) + r; x++) {
            if (x < 0 || y < 0 || x >= w || y >= h)
                continue;
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy))
                / std::max(brush_radius, 0.5f);
            if (d > 1.0f)
                continue;
            float k = brush_opacity
                * (d <= brush_hardness ? 1.0f : (1.0f - d) / std::max(1.0f - brush_hardness, 1e-3f));
            uint8_t& o = v[(size_t)y * w + x];
            o = (uint8_t)std::clamp((int)lroundf(o + (target - o) * k), 0, 255);
        }
}
/* One tool operation from texel (x, y) to (x1, y1) (local texels). */
bool paint(const TexturePick& t, uint8_t fill, int x, int y, int x1, int y1, int value, bool undoable)
{
    int w = t.w(), h = t.h();
    if (x < 0 || y < 0 || x >= w || y >= h)
        return false;
    x1 = std::clamp(x1, 0, w - 1), y1 = std::clamp(y1, 0, h - 1);
    std::vector<uint8_t> v = map_or_default(t.key, w, h, fill);
    /* The eraser paints the channel's neutral value (rough, flat, not metal). */
    uint8_t target = (uint8_t)std::clamp(tool == Tool::Eraser ? (int)materials::blank_value(channel_names[channel]) : value, 0, 255);
    if (tool == Tool::Pencil || tool == Tool::Eraser || tool == Tool::Brush) {
        int steps = std::max(abs(x1 - x), abs(y1 - y));
        for (int s = 0; s <= steps; s++) {
            float f = steps ? (float)s / steps : 0;
            int px = (int)lroundf(x + (x1 - x) * f), py = (int)lroundf(y + (y1 - y) * f);
            if (tool == Tool::Brush)
                dab(v, w, h, px + 0.5f, py + 0.5f, target);
            else
                v[(size_t)py * w + px] = target;
        }
    } else if (tool == Tool::Rectangle) {
        for (int py = std::min(y, y1); py <= std::max(y, y1); py++)
            for (int px = std::min(x, x1); px <= std::max(x, x1); px++)
                v[(size_t)py * w + px] = target;
    } else if (tool == Tool::Fill) { /* connected texels of the same palette index (or colour) */
        auto index_at = [&](int px, int py) {
            unsigned i = 0;
            uint16_t c = emissive::texel((unsigned)(t.box[0] + px), (unsigned)(t.box[1] + py), t.tpage, t.clut, &i);
            return ((t.tpage >> 7) & 3) >= 2 ? (unsigned)c : i;
        };
        unsigned want = index_at(x, y);
        std::vector<int> pending = { y * w + x };
        std::vector<bool> seen(v.size(), false);
        while (!pending.empty()) {
            int p = pending.back();
            pending.pop_back();
            if (seen[(size_t)p])
                continue;
            seen[(size_t)p] = true;
            int px = p % w, py = p / w;
            if (index_at(px, py) != want)
                continue;
            v[(size_t)p] = target;
            if (px > 0) pending.push_back(p - 1);
            if (px + 1 < w) pending.push_back(p + 1);
            if (py > 0) pending.push_back(p - w);
            if (py + 1 < h) pending.push_back(p + w);
        }
    }
    return write_map(t.key, w, h, v, undoable);
}

/* ---------------------------------------------------------------- JSON */

Json entry_info(const materials::Entry& e)
{
    Json j;
    std::string error;
    prefab::json_parse(materials::entry_json(e).c_str(), j, &error);
    j.set("origin", Json::str(e.user ? "user" : "curated"));
    Json v = Json::object();
    v.set("roughness", Json::num(e.value.roughness));
    v.set("specular", Json::num(e.value.specular));
    v.set("metal", Json::num(e.value.metal));
    v.set("auto_height", Json::boolean_(e.value.auto_height));
    v.set("bump", Json::num(e.value.bump));
    j.set("value", v);
    return j;
}
Json pick_json(const TexturePick& p)
{
    Json j = Json::object();
    j.set("valid", Json::boolean_(p.valid));
    if (!p.valid)
        return j;
    j.set("key", Json::str(p.key));
    j.set("clut", Json::str(p.clut_key));
    Json size = Json::array();
    size.items.push_back(Json::num(p.w()));
    size.items.push_back(Json::num(p.h()));
    j.set("size", size);
    j.set("tpage", Json::num(p.tpage));
    j.set("palette", Json::num(p.clut));
    Json texel = Json::array();
    texel.items.push_back(Json::num(p.u - p.box[0]));
    texel.items.push_back(Json::num(p.v - p.box[1]));
    j.set("texel", texel);
    Json room = Json::array();
    room.items.push_back(Json::num(p.zone));
    room.items.push_back(Json::num(p.room));
    j.set("room", room);
    const materials::Entry* e = materials::find_match(p.key, p.w(), p.h(), p.clut_key, p.zone, p.room);
    j.set("entry", Json::str(e ? e->id : ""));
    int m[PC_MATERIAL_PLANES];
    if (emissive::material_at((unsigned)p.u, (unsigned)p.v, p.tpage, m)) {
        Json at = Json::array(); /* roughness, specular, metal, slope u, slope v, cavity */
        for (int k = 0; k < PC_MATERIAL_PLANES; k++)
            at.items.push_back(Json::num(m[k]));
        j.set("material", at);
    }
    return j;
}
Json list_json()
{
    Json j = Json::object();
    j.set("active", Json::boolean_(pc_material_active() != 0));
    j.set("specular", Json::boolean_(pc_specular != 0));
    j.set("specular_strength", Json::num(pc_specular_strength));
    j.set("roughness_bias", Json::num(pc_roughness_bias));
    j.set("reflection_bounces", Json::num(pc_reflection_bounces));
    j.set("bump_strength", Json::num(pc_bump_strength));
    j.set("auto_height", Json::boolean_(pc_auto_height != 0));
    j.set("auto_height_strength", Json::num(pc_auto_height_strength));
    j.set("bump_occlusion", Json::num(pc_bump_occlusion));
    j.set("relief_aniso", Json::num(pc_relief_aniso));
    j.set("relief_footprint", Json::num(pc_relief_footprint));
    j.set("env_specular", Json::num(pc_env_specular));
    j.set("baked_energy", Json::num(pc_baked_energy));
    j.set("preview", Json::boolean_(pc_auto_height_preview != 0));
    j.set("cells", Json::num(emissive::material_cell_count()));
    j.set("unsaved", Json::boolean_(materials::pending_dirty()));
    j.set("user_file", Json::str(materials::catalog_user_file()));
    Json uses = Json::array();
    for (const emissive::MaterialUse& u : emissive::material_use_list()) {
        Json x = Json::object();
        x.set("key", Json::str(u.key));
        x.set("entry", Json::str(u.entry));
        Json size = Json::array();
        size.items.push_back(Json::num(u.box[2] - u.box[0] + 1));
        size.items.push_back(Json::num(u.box[3] - u.box[1] + 1));
        x.set("size", size);
        x.set("tpage", Json::num(u.page));
        x.set("painted", Json::boolean_(u.painted));
        x.set("painted_metal", Json::boolean_(u.painted_metal));
        x.set("relief", Json::str(u.relief == 1 ? "painted" : u.relief == 2 ? "estimated" : ""));
        uses.items.push_back(x);
    }
    j.set("uses", uses);
    Json entries = Json::array();
    for (const materials::Entry& e : materials::catalog_entries())
        entries.items.push_back(entry_info(e));
    j.set("entries", entries);
    Json presets = Json::object();
    for (const auto& p : materials::catalog_presets()) {
        Json v = Json::object();
        v.set("roughness", Json::num(p.second.roughness));
        v.set("specular", Json::num(p.second.specular));
        v.set("metal", Json::num(p.second.metal));
        v.set("auto_height", Json::boolean_(p.second.auto_height));
        v.set("bump", Json::num(p.second.bump));
        presets.set(p.first.c_str(), v);
    }
    j.set("presets", presets);
    if (picked.valid)
        j.set("picked", pick_json(picked));
    j.set("selected", Json::str(selected));
    return j;
}
int fail(const std::string& m, char* out, int size)
{
    snprintf(out, (size_t)size, "%s", m.c_str());
    return -1;
}
int reply(const Json& j, char* out, int size)
{
    std::string text = prefab::json_write(j);
    for (char& c : text)
        if (c == '\n')
            c = ' ';
    if ((int)text.size() >= size)
        return fail("material: reply too long", out, size);
    memcpy(out, text.c_str(), text.size() + 1);
    return 0;
}
bool number(const Json& j, const char* name, float* out)
{
    const Json* v = j.get(name);
    if (!v || v->type != Json::Number)
        return false;
    *out = (float)v->number;
    return true;
}
/* Fields of a set request onto an entry: name, where, review, preset (its
 * values), roughness, specular, metal, auto_height (bool), bump (0..4), room
 * (bool: this room only), clut (bool: this palette only; needs the texture
 * in view). */
bool apply_fields(materials::Entry& e, const Json& f, std::string* error)
{
    if (const Json* v = f.get("name"); v && v->type == Json::String) e.name = v->string;
    if (const Json* v = f.get("where"); v && v->type == Json::String) e.where = v->string;
    if (const Json* v = f.get("review"); v && v->type == Json::String) e.review = v->string;
    if (const Json* v = f.get("preset"); v && v->type == Json::String) {
        if (!v->string.empty()) {
            bool found = false;
            for (const auto& p : materials::catalog_presets())
                found |= p.first == v->string;
            if (!found) {
                *error = "no preset " + v->string;
                return false;
            }
        }
        apply_preset(e, v->string);
    }
    const char* names[3] = { "roughness", "specular", "metal" };
    float* fields[3] = { &e.value.roughness, &e.value.specular, &e.value.metal };
    for (int k = 0; k < 3; k++) {
        float x;
        if (number(f, names[k], &x)) {
            if (!(x >= 0 && x <= 1)) {
                *error = std::string(names[k]) + " must be in [0, 1]";
                return false;
            }
            *fields[k] = x;
            e.explicit_fields |= 1u << k;
        }
    }
    if (const Json* v = f.get("auto_height"); v && v->type == Json::Bool)
        e.value.auto_height = v->boolean, e.explicit_fields |= 8;
    float bump;
    if (number(f, "bump", &bump)) {
        if (!(bump >= 0 && bump <= 4)) {
            *error = "bump must be in [0, 4]";
            return false;
        }
        e.value.bump = bump, e.explicit_fields |= 16;
    }
    if (const Json* v = f.get("room"); v && v->type == Json::Bool) {
        if (v->boolean) {
            if (!emissive::current_room(&e.zone, &e.room)) e.zone = e.room = -1;
        } else
            e.zone = e.room = -1;
    }
    if (const Json* v = f.get("clut"); v && v->type == Json::Bool) {
        if (!v->boolean)
            e.clut.clear();
        else {
            TexturePick t;
            if (!emissive::texture_where(e.key, t)) {
                *error = "the texture is not in view (its palette is unknown)";
                return false;
            }
            e.clut = t.clut_key;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- the panel */

void texture_canvas(const TexturePick& t, const materials::Entry* e)
{
    int w = t.w(), h = t.h();
    ImGui::SeparatorText("Maps");
    static const char* channel_labels[CHANNEL_COUNT] = { "Roughness", "Height", "Metal" };
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (i)
            ImGui::SameLine();
        if (ImGui::RadioButton(channel_labels[i], channel == i))
            channel = i;
    }
    const char* name = channel_names[channel];
    const materials::Channel* map = materials::channel(t.key, name);
    bool has_map = map && map->w == w && map->h == h;
    uint8_t fill = channel_default(channel, e);
    if (map && !has_map)
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Its %s.png is %d x %d; the texture is %d x %d (unused).", name,
            map->w, map->h, w, h);
    std::string would = materials::user_dir() + "/" + t.key + "/" + name + ".png";
    if (has_map)
        ImGui::TextDisabled("%s", map->path.c_str());
    else if (channel == HEIGHT)
        ImGui::TextDisabled("No map: %s. Painting creates %s",
            e && e->value.auto_height ? "the estimate from the texture's colours" : "flat", would.c_str());
    else
        ImGui::TextDisabled("No map: the entry's %s everywhere. Painting creates %s", name, would.c_str());
    static const char* labels[] = { "Pencil", "Brush", "Eraser", "Rectangle", "Fill" };
    for (int i = 0; i < 5; i++) {
        if (i)
            ImGui::SameLine();
        if (ImGui::RadioButton(labels[i], tool == (Tool)i))
            tool = (Tool)i;
    }
    static const char* value_labels[CHANNEL_COUNT] = { "Value (roughness x 255)", "Value (height)", "Value (metal x 255)" };
    static const char* value_tips[CHANNEL_COUNT] = { "0 mirror smooth, 255 fully rough. The eraser paints 255.",
        "0 lowest, 255 highest (bump_strength texels of depth over the range). The eraser paints 128 (flat).\n"
        "A painted height.png replaces the estimate entirely.",
        "0 dielectric (stone, wood), 255 bare metal (tinted reflections, no diffuse light). The eraser paints 0." };
    ImGui::SliderFloat(value_labels[channel], &brush_value, 0, 255, "%.0f");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", value_tips[channel]);
    if (tool == Tool::Brush) {
        ImGui::SliderFloat("Radius", &brush_radius, 0.5f, 16.0f);
        ImGui::SliderFloat("Hardness", &brush_hardness, 0, 1);
        ImGui::SliderFloat("Opacity", &brush_opacity, 0, 1);
    }
    ImGui::SliderFloat("Texture underneath", &reference, 0, 1);
    if (ImGui::SmallButton("Undo stroke") && undo_map())
        snprintf(status_line, sizeof status_line, "Undid a stroke");
    ImGui::SameLine();
    char fill_label[64];
    snprintf(fill_label, sizeof fill_label, "Fill with %d", (int)fill);
    if (ImGui::SmallButton(fill_label)) {
        std::vector<uint8_t> v((size_t)w * h, fill);
        write_map(t.key, w, h, v, true);
    }
    bool preview = pc_auto_height_preview != 0;
    if (ImGui::Checkbox("Preview the height estimate", &preview))
        pc_auto_height_preview = preview;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Every material in view without a height.png gets the estimate from its colours\n"
                          "(as if its entry had auto_height), until switched off. Not saved.");
    static const char* views = "Lit\0Material (roughness, specular, metal)\0Specular alone\0Traced reflections\0"
                               "Relief normals\0Cavities\0";
    int view = pc_gl_view_mode >= 6 && pc_gl_view_mode <= 10 ? pc_gl_view_mode - 5 : 0;
    if (ImGui::Combo("Debug view", &view, views))
        pc_gl_view_mode = view ? view + 5 : 0;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("The present pass's material views (Vulkan): material pixels only, the rest black.\n"
                          "Relief normals: the shading normal with the relief, world xyz as rgb.");
    float cw = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
    float fit = std::max(1.0f, floorf(std::min(cw / w, 320.0f / h)));
    float z = zoom > 0 ? zoom : fit;
    ImGui::SliderFloat("Zoom", &z, 1, 24, "%.0f px per texel");
    zoom = z;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size(std::min(cw, w * z), h * z);
    ImGui::InvisibleButton("##canvas", size, ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    std::vector<uint8_t> values = map_or_default(t.key, w, h, fill);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t c = emissive::texel((unsigned)(t.box[0] + x), (unsigned)(t.box[1] + y), t.tpage, t.clut, nullptr);
            /* shown: smooth, high or metal bright */
            float r = values[(size_t)y * w + x] / 255.0f;
            float g = (channel == ROUGHNESS ? 1.0f - r : r) * (1.0f - reference);
            auto mix = [&](int ch) { return (int)std::clamp(ch * 255 / 31 * reference + g * 255, 0.0f, 255.0f); };
            ImU32 col = c ? IM_COL32(mix(c & 31), mix((c >> 5) & 31), mix((c >> 10) & 31), 255)
                          : IM_COL32(40, 0, 40, 255);
            ImVec2 a(origin.x + x * z, origin.y + y * z), b(origin.x + (x + 1) * z, origin.y + (y + 1) * z);
            dl->AddRectFilled(a, b, col);
        }
    dl->PopClipRect();
    if (hovered) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int x = (int)floorf((m.x - origin.x) / z), y = (int)floorf((m.y - origin.y) / z);
        if (x >= 0 && y >= 0 && x < w && y < h) {
            ImGui::SetTooltip("texel %d,%d  %s %d", x, y, name, values[(size_t)y * w + x]);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                stroking = true;
                stroke_from[0] = x, stroke_from[1] = y;
                if (tool != Tool::Rectangle)
                    paint(t, fill, x, y, x, y, (int)brush_value, true);
            } else if (stroking && ImGui::IsMouseDown(ImGuiMouseButton_Left) && tool != Tool::Rectangle
                && tool != Tool::Fill && (x != stroke_from[0] || y != stroke_from[1])) {
                paint(t, fill, stroke_from[0], stroke_from[1], x, y, (int)brush_value, false);
                stroke_from[0] = x, stroke_from[1] = y;
            }
            if (stroking && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && tool == Tool::Rectangle)
                paint(t, fill, stroke_from[0], stroke_from[1], x, y, (int)brush_value, true);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            undo_map();
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        stroking = false;
}

void entry_editor(const materials::Entry& original)
{
    materials::Entry e = original;
    bool changed = false;
    ImGui::PushID(e.id.c_str());
    ImGui::SeparatorText(e.id.c_str());
    if (e.key.empty())
        ImGui::TextDisabled("%s, every texture with palette %s%s", e.user ? "your entry" : "curated", e.clut.c_str(),
            e.zone >= 0 ? ", one room" : ", every room");
    else
        ImGui::TextDisabled("%s, texture %s %d x %d%s%s", e.user ? "your entry" : "curated", e.key.c_str(), e.w, e.h,
            e.clut.empty() ? "" : ", this palette", e.zone >= 0 ? ", one room" : ", every room");
    char name[128], where[256];
    snprintf(name, sizeof name, "%s", e.name.c_str());
    snprintf(where, sizeof where, "%s", e.where.c_str());
    if (ImGui::InputText("Name", name, sizeof name))
        e.name = name, changed = true;
    if (ImGui::InputText("Where", where, sizeof where))
        e.where = where, changed = true;
    std::vector<std::pair<std::string, materials::Surface>> presets = materials::catalog_presets();
    if (ImGui::BeginCombo("Preset", e.preset.empty() ? "(none)" : e.preset.c_str())) {
        if (ImGui::Selectable("(none)", e.preset.empty()))
            e.preset.clear(), e.explicit_fields |= 7, changed = true;
        for (const auto& p : presets) {
            char label[160];
            snprintf(label, sizeof label, "%s  (rough %.2f, spec %.2f, metal %.2f)", p.first.c_str(),
                p.second.roughness, p.second.specular, p.second.metal);
            if (ImGui::Selectable(label, e.preset == p.first))
                apply_preset(e, p.first), changed = true;
        }
        ImGui::EndCombo();
    }
    const char* labels[3] = { "Roughness", "Specular", "Metal" };
    const char* tips[3] = { "0 mirror, 1 fully rough (a roughness map overrides it per texel)",
        "Fraction of a dielectric's reflectance (4% at normal incidence); keep it subtle",
        "0 stone, wood, glass; 1 bare metal (tinted reflections, no diffuse light)" };
    float* fields[3] = { &e.value.roughness, &e.value.specular, &e.value.metal };
    for (int k = 0; k < 3; k++) {
        bool own = (e.explicit_fields >> k) & 1;
        if (!own)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::SliderFloat(labels[k], fields[k], 0, 1, "%.2f"))
            e.explicit_fields |= 1u << k, changed = true;
        if (!own)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s%s", tips[k], own ? "" : "\n(from the preset)");
    }
    {
        bool own = (e.explicit_fields & 8) != 0;
        if (ImGui::Checkbox("Estimate the height from the colours", &e.value.auto_height))
            e.explicit_fields |= 8, changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("auto_height: bumps from the texture's luminance (dark low, bright high) where it has\n"
                              "no height.png. Global switch and strength: Graphics > Lighting quality.%s",
                own ? "" : "\n(from the preset)");
        own = (e.explicit_fields & 16) != 0;
        if (!own)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::SliderFloat("Relief", &e.value.bump, 0, 4, "x %.2f"))
            e.explicit_fields |= 16, changed = true;
        if (!own)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("bump: this surface's multiple of the global relief depth (bump_strength)%s",
                own ? "" : "\n(from the preset)");
    }
    bool room = e.zone >= 0;
    if (ImGui::Checkbox("This room only", &room)) {
        if (room)
            emissive::current_room(&e.zone, &e.room);
        else
            e.zone = e.room = -1;
        changed = true;
    }
    bool palette = !e.clut.empty();
    if (!e.key.empty())
        ImGui::SameLine();
    if (!e.key.empty() && ImGui::Checkbox("This palette only", &palette)) {
        TexturePick t;
        if (!palette)
            e.clut.clear(), changed = true;
        else if (emissive::texture_where(e.key, t))
            e.clut = t.clut_key, changed = true;
    }
    bool review = e.review == "new";
    if (ImGui::Checkbox("Needs review", &review))
        e.review = review ? "new" : "", changed = true;
    if (changed) {
        materials::set_pending(e); /* live: the view shows it at once */
        selected = e.id;
    }
    if (ImGui::Button("Save")) {
        std::string error;
        if (materials::save_pending(&error))
            snprintf(status_line, sizeof status_line, "Saved into %s", materials::catalog_user_file().c_str());
        else
            snprintf(status_line, sizeof status_line, "%s", error.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        materials::revert_pending();
        snprintf(status_line, sizeof status_line, "Unsaved edits dropped");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy JSON for the catalog")) {
        std::string text = materials::entry_json(e);
        ImGui::SetClipboardText(text.c_str());
        snprintf(status_line, sizeof status_line, "Copied %s (paste into pc/assets/materials/catalog.json)", e.id.c_str());
    }
    if (e.user) {
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            materials::remove_pending(e.id);
            snprintf(status_line, sizeof status_line, "%s removed (Save to remove it from your file)", e.id.c_str());
        }
    }
    ImGui::PopID();
}


/* ---------------------------------------------------------------- the survey */

/* A texture drawn now, by its key and (optionally) its palette key. */
bool texture_in_view(const std::string& key, const std::string& clut_key, TexturePick& out)
{
    for (const emissive::UseInfo& u : emissive::use_list(true)) {
        if (u.key != key)
            continue;
        std::string ck = emissive::clut_key(u.page, u.clut);
        if (!clut_key.empty() && ck != clut_key)
            continue;
        out = TexturePick();
        out.valid = true, out.key = key, out.clut_key = ck, out.tpage = u.page, out.clut = u.clut;
        memcpy(out.box, u.box, 4);
        emissive::current_room(&out.zone, &out.room);
        return true;
    }
    return false;
}
/* A new user entry for a texture with a preset (the survey's one click):
 * this room and this palette (a texture reused elsewhere keeps its look
 * there), review "new", where the survey found it. */
materials::Entry entry_with_preset(const TexturePick& t, const std::string& preset, float share,
    const std::vector<materials::Entry>& all, bool whole_palette = false)
{
    materials::Entry e = entry_for(t, all);
    apply_preset(e, preset);
    if (whole_palette && !t.clut_key.empty())
        e.key.clear(), e.w = e.h = 0; /* a palette entry: every texture drawn with it here */
    std::string name = preset;
    if (!name.empty())
        name[0] = (char)toupper((unsigned char)name[0]);
    e.name = name;
    char where[160];
    if (share > 0)
        snprintf(where, sizeof where, "zone %d room %d; %s%.1f%% of the view when added (survey)", t.zone, t.room,
            e.key.empty() ? "every texture with this palette, " : "", share * 100);
    else
        snprintf(where, sizeof where, "zone %d room %d", t.zone, t.room);
    e.where = where;
    return e;
}
bool preset_exists(const std::string& name)
{
    for (const auto& p : materials::catalog_presets())
        if (p.first == name)
            return true;
    return false;
}
Json survey_json(const material_survey::Result& r, bool with_map)
{
    Json j = Json::object();
    j.set("valid", Json::boolean_(r.valid));
    if (!r.valid)
        return j;
    auto pair = [](double a, double b) {
        Json x = Json::array();
        x.items.push_back(Json::num(a));
        x.items.push_back(Json::num(b));
        return x;
    };
    auto triple = [](const float* v) {
        Json x = Json::array();
        for (int k = 0; k < 3; k++)
            x.items.push_back(Json::num(round(v[k] * 100) / 100));
        return x;
    };
    j.set("grid", pair(r.grid_w, r.grid_h));
    j.set("room", pair(r.zone, r.room));
    j.set("eye", triple(r.eye));
    j.set("forward", triple(r.forward));
    double cells = (double)r.grid_w * r.grid_h;
    j.set("textured", Json::num(r.textured / cells));
    j.set("untextured", Json::num(r.untextured / cells));
    Json list = Json::array();
    for (const material_survey::Texture& t : r.textures) {
        Json x = Json::object();
        x.set("key", Json::str(t.key));
        x.set("size", pair(t.w, t.h));
        x.set("clut", Json::str(t.clut_key));
        x.set("tpage", Json::num(t.tpage));
        x.set("palette", Json::num(t.clut));
        Json boxes = Json::array();
        for (const auto& b : t.boxes) {
            Json bx = Json::array();
            for (uint8_t v : b)
                bx.items.push_back(Json::num(v));
            boxes.items.push_back(bx);
        }
        x.set("boxes", boxes);
        x.set("cells", Json::num(t.cells));
        x.set("share", Json::num(round(t.cells / cells * 1e5) / 1e5));
        x.set("entry", Json::str(t.entry));
        list.items.push_back(x);
    }
    j.set("textures", list);
    if (with_map) {
        Json m = Json::array();
        for (int v : r.map)
            m.items.push_back(Json::num(v));
        j.set("map", m);
    }
    return j;
}
std::string base64(const uint8_t* data, size_t n)
{
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    s.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (i + 1 < n ? (uint32_t)data[i + 1] << 8 : 0) | (i + 2 < n ? data[i + 2] : 0);
        s += abc[v >> 18 & 63];
        s += abc[v >> 12 & 63];
        s += i + 1 < n ? abc[v >> 6 & 63] : '=';
        s += i + 2 < n ? abc[v & 63] : '=';
    }
    return s;
}

/* The panel's "Textures in this room": the survey of the view, refreshed
 * about once a second while shown, with each texture's thumbnail (from
 * VRAM), its share of the view and its entry, or a preset to give it one. */
material_survey::Result room_survey;
double room_survey_time = -1e9;
bool room_survey_auto = true;
std::unordered_map<uint64_t, std::vector<uint8_t>> thumbnails; /* page, clut, box -> RGBA */
bool survey_whole_palette = true; /* presets make palette entries (else the texture's own) */

void thumbnail(const material_survey::Texture& t, float px)
{
    uint64_t k = (uint64_t)t.tpage << 48 | (uint64_t)t.clut << 32 | (uint64_t)t.box[0] << 24 | (uint64_t)t.box[1] << 16
        | (uint64_t)t.box[2] << 8 | t.box[3];
    auto it = thumbnails.find(k);
    if (it == thumbnails.end()) {
        if (thumbnails.size() > 512)
            thumbnails.clear();
        it = thumbnails.emplace(k, emissive::texture_rgba_box(t.tpage, t.clut, t.box)).first;
    }
    const std::vector<uint8_t>& rgba = it->second;
    int w = t.w, h = t.h;
    int step = std::max(1, (std::max(w, h) + 15) / 16); /* at most 16 x 16 cells */
    int cw = (w + step - 1) / step, ch = (h + step - 1) / step;
    float cell = px / std::max(cw, ch);
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + px, o.y + px), IM_COL32(40, 0, 40, 255));
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            const uint8_t* p = &rgba[((size_t)(y * step) * w + (size_t)(x * step)) * 4];
            if (!p[3])
                continue;
            ImVec2 a(o.x + x * cell, o.y + y * cell);
            dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(p[0], p[1], p[2], 255));
        }
    ImGui::Dummy(ImVec2(px, px));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::BeginTooltip();
        float z = std::max(1.0f, floorf(192.0f / std::max(w, h)));
        ImVec2 b = ImGui::GetCursorScreenPos();
        ImDrawList* tl = ImGui::GetWindowDrawList();
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t* p = &rgba[((size_t)y * w + x) * 4];
                ImVec2 a(b.x + x * z, b.y + y * z);
                tl->AddRectFilled(a, ImVec2(a.x + z, a.y + z), p[3] ? IM_COL32(p[0], p[1], p[2], 255) : IM_COL32(40, 0, 40, 255));
            }
        ImGui::Dummy(ImVec2(w * z, h * z));
        ImGui::Text("%s  %d x %d  palette %s  page %u", t.key.c_str(), w, h, t.clut_key.empty() ? "-" : t.clut_key.c_str(),
            t.tpage);
        ImGui::EndTooltip();
    }
}

void survey_section()
{
    if (!ImGui::CollapsingHeader("Textures in this room", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    double now = ImGui::GetTime();
    if (ImGui::SmallButton("Refresh") || (room_survey_auto && now - room_survey_time > 1.0)) {
        material_survey::survey(room_survey);
        room_survey_time = now;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Live", &room_survey_auto);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Survey the view again about once a second (move the free camera around the room to see\n"
                          "the rest of it).");
    if (!room_survey.valid) {
        ImGui::TextDisabled("No view to survey.");
        return;
    }
    int with = 0;
    float with_share = 0, all_share = 0;
    double cells = (double)room_survey.grid_w * room_survey.grid_h;
    for (const material_survey::Texture& t : room_survey.textures) {
        all_share += (float)(t.cells / cells);
        if (!t.entry.empty())
            with++, with_share += (float)(t.cells / cells);
    }
    ImGui::Text("%d textures in view; %d with a material, %.0f%% of the textured view.",
        (int)room_survey.textures.size(), with, all_share > 0 ? with_share / all_share * 100 : 0.0f);
    ImGui::TextDisabled("Hover a thumbnail for the texture; pick a preset to give it an entry (yours, this room and\n"
                        "palette, needs review; Save to keep it).");
    ImGui::TextUnformatted("A preset applies to");
    ImGui::SameLine();
    if (ImGui::RadioButton("its whole palette", survey_whole_palette))
        survey_whole_palette = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A palette entry: every texture drawn with that palette in this room (a room's pieces of\n"
                          "one painted surface share a palette: the floor's flagstones, the walls' blocks).\n"
                          "A texture's own entry still wins over it.");
    ImGui::SameLine();
    if (ImGui::RadioButton("the texture", !survey_whole_palette))
        survey_whole_palette = false;
    /* The palettes' shares (what a palette entry would cover). */
    std::map<std::string, float> palette_share;
    for (const material_survey::Texture& t : room_survey.textures)
        palette_share[t.clut_key] += (float)(t.cells / cells);
    std::vector<materials::Entry> all = materials::catalog_entries();
    float thumb = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
    if (!ImGui::BeginTable("##survey", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,
            ImVec2(-1, thumb * 9)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, thumb);
    ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("100.0%").x);
    ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGuiListClipper clip;
    clip.Begin((int)room_survey.textures.size(), thumb + ImGui::GetStyle().CellPadding.y * 2);
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            const material_survey::Texture& t = room_survey.textures[(size_t)i];
            ImGui::PushID(i);
            ImGui::TableNextRow(0, thumb);
            ImGui::TableNextColumn();
            thumbnail(t, thumb);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", t.cells / cells * 100);
            ImGui::TableNextColumn();
            ImGui::Text("%s %dx%d", t.key.c_str(), t.w, t.h);
            if (!t.clut_key.empty())
                ImGui::TextDisabled("pal %s (%.0f%%)%s", t.clut_key.c_str(), palette_share[t.clut_key] * 100,
                    t.boxes.size() > 1 ? ", several boxes" : "");
            ImGui::TableNextColumn();
            if (!t.entry.empty()) {
                std::vector<materials::Entry> scratch;
                const materials::Entry* e = find_entry(t.entry, scratch);
                char label[160];
                snprintf(label, sizeof label, "%s %s", t.entry.c_str(), e ? e->name.c_str() : "");
                if (ImGui::Selectable(label, selected == t.entry))
                    selected = t.entry;
                if (e && !e->preset.empty())
                    ImGui::TextDisabled("%s", e->preset.c_str());
            } else {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##preset", "+ preset...")) {
                    for (const auto& p : materials::catalog_presets()) {
                        char label[160];
                        snprintf(label, sizeof label, "%s  (rough %.2f, spec %.2f, metal %.2f%s)", p.first.c_str(),
                            p.second.roughness, p.second.specular, p.second.metal, p.second.auto_height ? ", relief" : "");
                        if (ImGui::Selectable(label)) {
                            TexturePick where;
                            where.valid = true, where.key = t.key, where.clut_key = t.clut_key;
                            where.tpage = t.tpage, where.clut = t.clut;
                            memcpy(where.box, t.box, 4);
                            where.zone = room_survey.zone, where.room = room_survey.room;
                            bool pal = survey_whole_palette && !t.clut_key.empty();
                            materials::Entry e = entry_with_preset(where, p.first,
                                pal ? palette_share[t.clut_key] : (float)(t.cells / cells), all, pal);
                            materials::set_pending(e);
                            selected = e.id;
                            all = materials::catalog_entries();
                            room_survey_time = -1e9; /* show it at once */
                            snprintf(status_line, sizeof status_line, "%s: %s for %s %s (unsaved: Save keeps it)",
                                e.id.c_str(), p.first.c_str(), pal ? "palette" : "texture",
                                pal ? t.clut_key.c_str() : t.key.c_str());
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::PopID();
        }
    ImGui::EndTable();
}

} // namespace

/* The view was clicked (lights.cpp): picks a texture when the panel asked. */
extern "C" int pc_material_viewport_click(float x, float y)
{
    if (!pick_armed)
        return 0;
    pick_armed = false;
    TexturePick p;
    if (emissive::pick_texture(x, y, p)) {
        picked = p;
        const materials::Entry* e = materials::find_match(p.key, p.w(), p.h(), p.clut_key, p.zone, p.room);
        selected = e ? e->id : "";
        snprintf(status_line, sizeof status_line, "Picked texture %s (%d x %d)%s%s", p.key.c_str(), p.w(), p.h(),
            e ? ", entry " : ", no entry", e ? e->id.c_str() : "");
    } else
        snprintf(status_line, sizeof status_line, "Nothing textured there");
    return 1;
}

extern "C" void pc_material_panel(void)
{
    ImGui::TextWrapped("Material response: highlights and reflections on textures named in the catalog "
                       "(pc/assets/materials/catalog.json, yours in %s). Vulkan only.",
        materials::catalog_user_file().c_str());
    ImGui::Text("Specular %s, strength %.2f, roughness bias %+.2f, reflections %d (Graphics > Lighting quality).",
        pc_specular ? "on" : "off", pc_specular_strength, pc_roughness_bias, pc_reflection_bounces);
    ImGui::Text("Relief %.2f texels, estimate %s (x %.2f), cavities %.2f.", pc_bump_strength,
        pc_auto_height ? "on" : "off", pc_auto_height_strength, pc_bump_occlusion);
    ImGui::SliderFloat("Relief stretch limit", &pc_relief_aniso, 0, 16, pc_relief_aniso > 0 ? "%.1f x" : "No limit");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("relief_aniso: the relief fades to flat where a pixel's footprint in the texture is this many\n"
                          "times longer than wide (edge-on faces speckle); gone at twice.");
    ImGui::SliderFloat("Relief texel limit", &pc_relief_footprint, 0, 8, pc_relief_footprint > 0 ? "%.1f texels" : "No limit");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("relief_footprint: the relief fades to flat where a pixel spans this many texels; gone at twice.");
    ImGui::Text("%d textures with a material here, %d texels%s.", (int)emissive::material_use_list().size(),
        emissive::material_cell_count(), pc_material_active() ? "" : " (none lit)");
    if (ImGui::Button(pick_armed ? "Click a surface in the view..." : "Pick texture"))
        pick_armed = !pick_armed;
    if (materials::pending_dirty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "unsaved");
    }
    std::vector<materials::Entry> all = materials::catalog_entries();
    if (picked.valid) {
        ImGui::Text("Picked: %s %d x %d, palette %s, room %d/%d", picked.key.c_str(), picked.w(), picked.h(),
            picked.clut_key.empty() ? "-" : picked.clut_key.c_str(), picked.zone, picked.room);
        const materials::Entry* e = materials::find_match(picked.key, picked.w(), picked.h(), picked.clut_key,
            picked.zone, picked.room);
        if (!e && ImGui::SmallButton("New entry for the picked texture")) {
            materials::Entry n = entry_for(picked, all);
            materials::set_pending(n);
            selected = n.id;
            all = materials::catalog_entries();
        }
    }
    if (status_line[0])
        ImGui::TextWrapped("%s", status_line);
    survey_section();
    if (ImGui::BeginListBox("##materials", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 6))) {
        std::vector<emissive::MaterialUse> uses = emissive::material_use_list();
        for (const materials::Entry& e : all) {
            int here = 0;
            for (const emissive::MaterialUse& u : uses)
                here += u.entry == e.id;
            char label[256];
            snprintf(label, sizeof label, "%s%s %s%s  [%d here]", e.user ? "* " : "", e.id.c_str(), e.name.c_str(),
                e.review == "new" ? " (review)" : "", here);
            if (ImGui::Selectable(label, selected == e.id))
                selected = e.id;
        }
        ImGui::EndListBox();
    }
    std::vector<materials::Entry> scratch;
    const materials::Entry* e = selected.empty() ? nullptr : find_entry(selected, scratch);
    if (e) {
        materials::Entry copy = *e;
        entry_editor(copy);
    }
    /* The canvas: the picked texture, else the selected entry's where it is drawn. */
    TexturePick t = picked;
    if (e && (!t.valid || t.key != e->key))
        t.valid = emissive::texture_where(e->key, t);
    if (t.valid)
        texture_canvas(t, e);
    else if (e)
        ImGui::TextDisabled("The entry's texture is not drawn here (its maps can be painted where it is).");
}

/* RPC material: {action: list (default) | pick {x, y} or {world: [x, y, z]}
 * | new (for the last pick) | set {id, fields...} or {entry} | select {id} |
 * remove {id} | save | revert | copy {id} (the JSON for the catalog) | paint
 * {key (default: the last pick's), channel (roughness, height, metal;
 * default roughness), value, rects: [[x0, y0, x1, y1]...], tool (pencil,
 * rectangle, fill, eraser), at: [x, y]} | map {key, channel} (its map: size
 * and values' sum) | undo (the last map stroke) | preview {auto_height:
 * bool} (the height estimate on every material in view) | reload (both
 * catalogs and the maps read again from disk)}. */
extern "C" int pc_material_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != Json::Object)
        return fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "list";
    std::vector<materials::Entry> all = materials::catalog_entries();
    auto id_param = [&]() {
        const Json* v = params.get("id");
        return v && v->type == Json::String ? v->string : selected;
    };
    if (action == "list")
        return reply(list_json(), out, size);
    if (action == "pick") {
        TexturePick p;
        bool ok;
        if (const Json* w = params.get("world"); w && w->type == Json::Array && w->items.size() == 3) {
            float p3[3] = { (float)w->items[0].number, (float)w->items[1].number, (float)w->items[2].number };
            ok = emissive::pick_texture_world(p3, p);
        } else {
            float x = 0, y = 0;
            number(params, "x", &x), number(params, "y", &y);
            ok = emissive::pick_texture(x, y, p);
        }
        if (ok) {
            picked = p;
            const materials::Entry* e = materials::find_match(p.key, p.w(), p.h(), p.clut_key, p.zone, p.room);
            selected = e ? e->id : "";
        }
        return reply(pick_json(ok ? p : TexturePick()), out, size);
    }
    if (action == "survey") {
        float grid = 160, cull = 1;
        number(params, "grid", &grid), number(params, "cull", &cull);
        const Json* m = params.get("map");
        material_survey::Result r;
        material_survey::survey(r, (int)grid, (int)cull);
        return reply(survey_json(r, m && m->type == Json::Bool && m->boolean), out, size);
    }
    if (action == "texture") { /* a texture in view as RGBA (base64): the survey's contact sheet */
        const Json* k = params.get("key");
        const Json* c = params.get("palette");
        TexturePick t;
        if (!k || k->type != Json::String
            || !texture_in_view(k->string, c && c->type == Json::String ? c->string : "", t))
            return fail("material texture: no such texture in view", out, size);
        std::vector<uint8_t> rgba = emissive::texture_rgba_box(t.tpage, t.clut, t.box);
        Json j = Json::object();
        j.set("key", Json::str(t.key));
        j.set("clut", Json::str(t.clut_key));
        j.set("w", Json::num(t.w()));
        j.set("h", Json::num(t.h()));
        j.set("rgba", Json::str(base64(rgba.data(), rgba.size())));
        return reply(j, out, size);
    }
    if (action == "new") {
        /* For a texture in view by key (and palette key), else the last pick. */
        TexturePick from = picked;
        if (const Json* k = params.get("key"); k && k->type == Json::String) {
            const Json* c = params.get("palette");
            if (!texture_in_view(k->string, c && c->type == Json::String ? c->string : "", from))
                return fail("material new: texture " + k->string + " is not in view", out, size);
        } else if (!picked.valid)
            return fail("material new: pick a texture first", out, size);
        if (const Json* p = params.get("preset"); p && p->type == Json::String && !p->string.empty()) {
            if (!preset_exists(p->string))
                return fail("material new: no preset " + p->string, out, size);
            float share = 0;
            number(params, "share", &share);
            const Json* pal = params.get("whole_palette");
            materials::Entry e = entry_with_preset(from, p->string, share, all, pal && pal->type == Json::Bool && pal->boolean);
            if (!apply_fields(e, params, &error))
                return fail("material new: " + error, out, size);
            materials::set_pending(e);
            selected = e.id;
            return reply(entry_info(e), out, size);
        }
        materials::Entry e = entry_for(from, all);
        if (!apply_fields(e, params, &error))
            return fail("material new: " + error, out, size);
        materials::set_pending(e);
        selected = e.id;
        return reply(entry_info(e), out, size);
    }
    if (action == "set") {
        materials::Entry e;
        if (const Json* j = params.get("entry"); j && j->type == Json::Object) {
            const Json* id = j->get("id");
            if (!id || id->type != Json::String)
                return fail("material set: the entry needs an id", out, size);
            const materials::Entry* old = find_entry(id->string, all);
            e = old ? *old : materials::Entry();
            e.id = id->string;
            if (const Json* m = j->get("match"); m && m->type == Json::Object) {
                if (const Json* k = m->get("key"); k && k->type == Json::String) e.key = k->string;
                if (const Json* c = m->get("clut"); c && c->type == Json::String) e.clut = c->string;
                if (const Json* s = m->get("size"); s && s->type == Json::Array && s->items.size() == 2)
                    e.w = (int)s->items[0].number, e.h = (int)s->items[1].number;
            }
            if (!apply_fields(e, *j, &error))
                return fail("material set: " + error, out, size);
        } else {
            const materials::Entry* old = find_entry(id_param(), all);
            if (!old)
                return fail("material set: no entry " + id_param(), out, size);
            e = *old;
            if (!apply_fields(e, params, &error))
                return fail("material set: " + error, out, size);
        }
        if (e.key.size() != 10 || e.w < 1 || e.h < 1 || e.w > 256 || e.h > 256)
            return fail("material set: the entry needs match key and size", out, size);
        materials::set_pending(e);
        selected = e.id;
        return reply(entry_info(e), out, size);
    }
    if (action == "select") {
        selected = id_param();
        return reply(list_json(), out, size);
    }
    if (action == "remove") {
        materials::remove_pending(id_param());
        return reply(list_json(), out, size);
    }
    if (action == "save") {
        if (!materials::save_pending(&error))
            return fail("material save: " + error, out, size);
        return reply(list_json(), out, size);
    }
    if (action == "revert") {
        materials::revert_pending();
        return reply(list_json(), out, size);
    }
    if (action == "copy") {
        const materials::Entry* e = find_entry(id_param(), all);
        if (!e)
            return fail("material copy: no entry " + id_param(), out, size);
        Json j = Json::object();
        j.set("json", Json::str(materials::entry_json(*e)));
        return reply(j, out, size);
    }
    if (action == "reload") { /* both catalogs and every map read again from disk */
        materials::reload();
        return reply(list_json(), out, size);
    }
    if (action == "preview") {
        if (const Json* v = params.get("auto_height"); v && v->type == Json::Bool)
            pc_auto_height_preview = v->boolean;
        return reply(list_json(), out, size);
    }
    if (action == "undo") {
        Json j = Json::object();
        j.set("undone", Json::boolean_(undo_map()));
        return reply(j, out, size);
    }
    if (action == "paint" || action == "map") {
        TexturePick t = picked;
        if (const Json* k = params.get("key"); k && k->type == Json::String && k->string != t.key)
            t.valid = emissive::texture_where(k->string, t);
        if (!t.valid)
            return fail("material " + action + ": the texture is not in view", out, size);
        const materials::Entry* e = materials::find_match(t.key, t.w(), t.h(), t.clut_key, t.zone, t.room);
        int saved_channel = channel, ch = ROUGHNESS;
        if (const Json* c = params.get("channel"); c && c->type == Json::String) {
            ch = -1;
            for (int i = 0; i < CHANNEL_COUNT; i++)
                if (c->string == channel_names[i])
                    ch = i;
            if (ch < 0)
                return fail("material " + action + ": channel is roughness, height or metal", out, size);
        }
        channel = ch;
        uint8_t base = channel_default(ch, e);
        if (action == "paint") {
            Tool saved = tool;
            float value = brush_value;
            number(params, "value", &value);
            if (const Json* tl = params.get("tool"); tl && tl->type == Json::String)
                for (int i = 0; i < 5; i++)
                    if (tl->string == tool_names[i])
                        tool = (Tool)i;
            bool ok = true;
            if (const Json* rects = params.get("rects"); rects && rects->type == Json::Array) {
                Tool keep = tool;
                tool = Tool::Rectangle;
                for (const Json& r : rects->items)
                    if (r.type == Json::Array && r.items.size() >= 4)
                        ok = paint(t, base, (int)r.items[0].number, (int)r.items[1].number, (int)r.items[2].number,
                                 (int)r.items[3].number, r.items.size() >= 5 ? (int)r.items[4].number : (int)value,
                                 true)
                            && ok;
                tool = keep;
            }
            if (const Json* at = params.get("at"); at && at->type == Json::Array && at->items.size() == 2)
                ok = paint(t, base, (int)at->items[0].number, (int)at->items[1].number, (int)at->items[0].number,
                         (int)at->items[1].number, (int)value, true)
                    && ok;
            tool = saved;
            if (!ok) {
                channel = saved_channel;
                return fail("material paint: " + std::string(status_line[0] ? status_line : "outside the texture"),
                    out, size);
            }
        }
        channel = saved_channel;
        const materials::Channel* c = materials::channel(t.key, channel_names[ch]);
        Json j = Json::object();
        j.set("key", Json::str(t.key));
        j.set("channel", Json::str(channel_names[ch]));
        j.set("map", Json::boolean_(c != nullptr));
        if (c) {
            Json sz = Json::array();
            sz.items.push_back(Json::num(c->w));
            sz.items.push_back(Json::num(c->h));
            j.set("size", sz);
            double sum = 0;
            for (uint8_t v : c->values)
                sum += v;
            j.set("sum", Json::num(sum));
            j.set("path", Json::str(c->path));
        }
        return reply(j, out, size);
    }
    return fail("material: unknown action " + action, out, size);
}
