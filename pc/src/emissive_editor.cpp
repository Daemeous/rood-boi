// Emissive texels: picking a texture in the view, the editor panel (dev
// overlay Details > Emissive) and RPC emissive. Design: pc/docs/Emissive.md.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>

#include "imgui.h"
#include "editor_overlays.h"
#include "emissive.h"
#include "emissive_internal.h"
#include "render.h"

using prefab::Json;
using namespace emissive;

extern "C" {
int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick) __attribute__((weak));
int pc_hw_present_all_lists(uint32_t* lists, int max) __attribute__((weak));
extern int pc_hw_active;
}

namespace {

/* ---------------------------------------------------------------- picking */

struct Picked {
    bool valid = false;
    float world[3] = { 0, 0, 0 };
    unsigned tpage = 0, clut = 0, code = 0;
    uint8_t box[4] = { 0, 0, 0, 0 };
    int u = 0, v = 0; /* the texel under the point (page coordinates) */
    std::string key, clut_key;
    uint32_t mesh = 0;
    int room = 0; /* a room list's triangle (else a character, weapon or prop) */
};
Picked picked;

struct PickSearch {
    float p[3];
    float best = 1e30f;
    Picked out;
    int room;
};
void pick_sink(void* context, const float w[9], const uint8_t* /*rgb*/, const uint8_t uv[6], unsigned code,
    unsigned tpage, unsigned clut)
{
    PickSearch* s = (PickSearch*)context;
    if (!(code & 4))
        return;
    float e1[3], e2[3], n[3], d[3];
    for (int k = 0; k < 3; k++)
        e1[k] = w[3 + k] - w[k], e2[k] = w[6 + k] - w[k], d[k] = s->p[k] - w[k];
    n[0] = e1[1] * e2[2] - e1[2] * e2[1], n[1] = e1[2] * e2[0] - e1[0] * e2[2], n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-6f)
        return;
    float dist = fabsf((n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / len);
    if (dist >= s->best || dist > 12.0f)
        return;
    /* Barycentric of the point's projection on the plane. */
    float d00 = e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2], d01 = e1[0] * e2[0] + e1[1] * e2[1] + e1[2] * e2[2];
    float d11 = e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2];
    float d20 = d[0] * e1[0] + d[1] * e1[1] + d[2] * e1[2], d21 = d[0] * e2[0] + d[1] * e2[1] + d[2] * e2[2];
    float den = d00 * d11 - d01 * d01;
    if (fabsf(den) < 1e-9f)
        return;
    float b1 = (d11 * d20 - d01 * d21) / den, b2 = (d00 * d21 - d01 * d20) / den, b0 = 1 - b1 - b2;
    const float slack = -0.01f;
    if (b0 < slack || b1 < slack || b2 < slack)
        return;
    s->best = dist;
    Picked& o = s->out;
    o.valid = true;
    memcpy(o.world, s->p, sizeof o.world);
    o.tpage = tpage, o.clut = clut, o.code = code;
    o.u = (int)floorf(b0 * uv[0] + b1 * uv[2] + b2 * uv[4]);
    o.v = (int)floorf(b0 * uv[1] + b1 * uv[3] + b2 * uv[5]);
    uint32_t source;
    render_emit_tag(&source, &o.mesh);
    const render_polygon* poly = render_emit_polygon();
    o.box[0] = o.box[1] = 255, o.box[2] = o.box[3] = 0;
    int corners = poly && (poly->flags & RENDER_POLY_QUAD) ? 4 : 3;
    for (int c = 0; c < corners; c++) {
        const uint8_t* q = poly ? poly->uv[c] : &uv[c * 2];
        o.box[0] = std::min(o.box[0], q[0]), o.box[1] = std::min(o.box[1], q[1]);
        o.box[2] = std::max(o.box[2], q[0]), o.box[3] = std::max(o.box[3], q[1]);
    }
    o.u = std::clamp(o.u, (int)o.box[0], (int)o.box[2]);
    o.v = std::clamp(o.v, (int)o.box[1], (int)o.box[3]);
    o.room = s->room;
}
/* The textured native triangle at world point p (room, characters, props). */
bool pick_world(const float p[3], Picked& out)
{
    uint32_t lists[64];
    int n = pc_hw_present_all_lists ? pc_hw_present_all_lists(lists, 64) : 0;
    PickSearch s;
    memcpy(s.p, p, sizeof s.p);
    for (int i = 0; i < n; i++) {
        s.room = render_list_objects(lists[i]) != 1;
        render_list_triangles(lists[i], pick_sink, &s);
    }
    if (!s.out.valid)
        return false;
    s.out.tpage &= 0x19f;
    s.out.key = content_key(s.out.tpage, s.out.clut, s.out.box[0], s.out.box[1], s.out.box[2], s.out.box[3]);
    s.out.clut_key = clut_key(s.out.tpage, s.out.clut);
    out = s.out;
    return true;
}
bool pick_pixel(float x, float y, Picked& out)
{
    pc_overlay_view view;
    pc_overlay_pick pick = nullptr;
    float world[3], normal[3];
    if (!pc_lights_tools_view || !pc_lights_tools_view(&view, &pick) || !pick || !pick(x, y, world, normal))
        return false;
    return pick_world(world, out);
}

Json picked_json(const Picked& p)
{
    Json j = Json::object();
    j.set("valid", Json::boolean_(p.valid));
    if (!p.valid)
        return j;
    Json w = Json::array();
    for (float v : p.world)
        w.items.push_back(Json::num(round(v * 10) / 10));
    j.set("world", w);
    j.set("key", Json::str(p.key));
    Json size = Json::array();
    size.items.push_back(Json::num(p.box[2] - p.box[0] + 1));
    size.items.push_back(Json::num(p.box[3] - p.box[1] + 1));
    j.set("size", size);
    j.set("clut_key", Json::str(p.clut_key));
    j.set("tpage", Json::num(p.tpage));
    j.set("clut", Json::num(p.clut));
    Json box = Json::array();
    for (uint8_t b : p.box)
        box.items.push_back(Json::num(b));
    j.set("box", box);
    j.set("texel", [&] {
        Json t = Json::array();
        t.items.push_back(Json::num(p.u - p.box[0]));
        t.items.push_back(Json::num(p.v - p.box[1]));
        return t;
    }());
    unsigned index = 0;
    uint16_t t = texel((unsigned)p.u, (unsigned)p.v, p.tpage, p.clut, &index);
    j.set("index", Json::num(index));
    Json rgb = Json::array();
    rgb.items.push_back(Json::num(t & 31));
    rgb.items.push_back(Json::num((t >> 5) & 31));
    rgb.items.push_back(Json::num((t >> 10) & 31));
    j.set("color", rgb);
    j.set("room", Json::boolean_(p.room != 0));
    int s = slot_at((unsigned)p.u, (unsigned)p.v, p.tpage);
    const Catalog& c = catalog();
    j.set("entry", Json::str(s > 0 && s <= (int)c.entries.size() ? c.entries[s - 1].id : ""));
    float e[3] = { 0, 0, 0 }, light = 0;
    int glows = pc_emissive_texel((unsigned)p.u, (unsigned)p.v, p.tpage, p.clut, e, &light);
    Json em = Json::array();
    for (float v : e)
        em.items.push_back(Json::num(round(v * 1000) / 1000));
    j.set("emission", em);
    j.set("glows", Json::boolean_(glows != 0));
    return j;
}

/* A new entry for the picked texture: the whole texture, its colour. */
Entry entry_for(const Picked& p)
{
    Entry e;
    e.id = new_id();
    e.name = "New emissive surface";
    Match m;
    m.key = p.key;
    m.w = p.box[2] - p.box[0] + 1, m.h = p.box[3] - p.box[1] + 1;
    e.match.push_back(m);
    e.review = "new";
    return e;
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
        return fail("emissive: reply too long", out, size);
    memcpy(out, text.c_str(), text.size() + 1);
    return 0;
}
Json list_json()
{
    Json j = Json::object();
    j.set("strength", Json::num(pc_emissive));
    j.set("active", Json::boolean_(pc_emissive_active() != 0));
    j.set("catalog", Json::str(catalog_path()));
    j.set("user", Json::str(user_path()));
    j.set("unsaved", Json::boolean_(user_dirty()));
    j.set("marked_uses", Json::num(marked_use_count()));
    j.set("marked_texels", Json::num(marked_cell_count()));
    j.set("light_triangles", Json::num(light_triangle_count()));
    Json es = Json::array();
    std::vector<UseInfo> uses = use_list(false);
    for (const Entry& e : catalog().entries) {
        Json ej = entry_to_json(e);
        static const char* origins[] = { "curated", "override", "user" };
        ej.set("origin", Json::str(origins[e.origin]));
        int marked = 0;
        for (const UseInfo& u : uses)
            marked += u.entry == e.id;
        ej.set("marked", Json::num(marked));
        es.items.push_back(ej);
    }
    j.set("entries", es);
    return j;
}

/* ---------------------------------------------------------------- panel */

bool pick_armed;
std::string selected;
char status_line[256];

void texture_view(const Entry* e, const Picked* p)
{
    /* The texture: the picked one, else the first live use of the entry. */
    unsigned tpage = 0, clut = 0;
    uint8_t box[4];
    int ox = 0, oy = 0;
    bool have = false;
    if (p && p->valid && (!e || std::any_of(e->match.begin(), e->match.end(), [&](const Match& m) { return m.key == p->key; }))) {
        tpage = p->tpage, clut = p->clut;
        memcpy(box, p->box, 4);
        have = true;
        if (e)
            for (const Match& m : e->match)
                if (m.key == p->key)
                    ox = m.ox, oy = m.oy;
    } else if (e) {
        for (const UseInfo& u : use_list(false))
            if (u.entry == e->id) {
                tpage = u.page, clut = u.clut;
                memcpy(box, u.box, 4);
                have = true;
                for (const Match& m : e->match)
                    if (m.key == u.key)
                        ox = m.ox, oy = m.oy;
                break;
            }
    }
    if (!have) {
        ImGui::TextDisabled("Not in view: pick a surface that shows this texture.");
        return;
    }
    int w = box[2] - box[0] + 1, h = box[3] - box[1] + 1;
    float cell = std::clamp(256.0f / (float)std::max(w, h), 2.0f, 12.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float time = (float)ImGui::GetTime();
    float pulse = 0.5f + 0.5f * sinf(time * 4.0f);
    for (int y = 0; y < h && y < 256; y++)
        for (int x = 0; x < w && x < 256; x++) {
            unsigned index = 0;
            uint16_t t = texel((unsigned)(box[0] + x), (unsigned)(box[1] + y), tpage, clut, &index);
            ImVec2 a(origin.x + x * cell, origin.y + y * cell), b(a.x + cell, a.y + cell);
            ImU32 col = t ? IM_COL32((t & 31) * 255 / 31, ((t >> 5) & 31) * 255 / 31, ((t >> 10) & 31) * 255 / 31, 255)
                          : IM_COL32(40, 0, 40, 255);
            dl->AddRectFilled(a, b, col);
            if (e && mask_covers(e->mask, x + ox, y + oy)) {
                bool pass = filters_pass(e->mask, index, t, (tpage >> 7) & 3);
                if (pass && t)
                    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (int)(60 * pulse)));
                dl->AddRect(a, b, pass ? IM_COL32(255, 220, 64, 200) : IM_COL32(120, 120, 255, 120));
            }
        }
    if (p && p->valid && p->tpage == tpage && p->clut == clut && !memcmp(p->box, box, 4)) {
        int x = p->u - p->box[0], y = p->v - p->box[1];
        ImVec2 a(origin.x + x * cell, origin.y + y * cell);
        dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
    }
    ImGui::InvisibleButton("##texture", ImVec2(w * cell, h * cell));
    /* Drag a rectangle on the texture: the mask's first rect. */
    static bool dragging;
    static int drag0[2];
    if (e && ImGui::IsItemActivated()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        drag0[0] = (int)((m.x - origin.x) / cell), drag0[1] = (int)((m.y - origin.y) / cell);
        dragging = true;
    }
    if (e && dragging && ImGui::IsItemActive()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int x1 = (int)((m.x - origin.x) / cell), y1 = (int)((m.y - origin.y) / cell);
        int r[4] = { std::min(drag0[0], x1), std::min(drag0[1], y1), std::max(drag0[0], x1) + 1,
            std::max(drag0[1], y1) + 1 };
        dl->AddRect(ImVec2(origin.x + r[0] * cell, origin.y + r[1] * cell),
            ImVec2(origin.x + r[2] * cell, origin.y + r[3] * cell), IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
    }
    if (e && dragging && ImGui::IsItemDeactivated()) {
        dragging = false;
        ImVec2 m = ImGui::GetIO().MousePos;
        int x1 = (int)((m.x - origin.x) / cell), y1 = (int)((m.y - origin.y) / cell);
        Entry copy = *e;
        std::array<int, 4> r = { std::clamp(std::min(drag0[0], x1), 0, w) + ox, std::clamp(std::min(drag0[1], y1), 0, h) + oy,
            std::clamp(std::max(drag0[0], x1) + 1, 0, w) + ox, std::clamp(std::max(drag0[1], y1) + 1, 0, h) + oy };
        if (ImGui::GetIO().KeyShift || copy.mask.rects.empty())
            copy.mask.rects.push_back(r);
        else
            copy.mask.rects[0] = r;
        put(copy);
    }
    ImGui::TextDisabled("%d x %d texels. Drag: the mask's rectangle (Shift: add one).", w, h);
}

void entry_editor(const Entry& original)
{
    Entry e = original;
    bool changed = false;
    static const char* origins[] = { "curated (shipped)", "curated, with your changes", "yours" };
    ImGui::Text("%s  (%s)", e.id.c_str(), origins[e.origin]);
    char name[128];
    snprintf(name, sizeof name, "%s", e.name.c_str());
    if (ImGui::InputText("Name", name, sizeof name))
        e.name = name, changed = true;
    changed |= ImGui::Checkbox("Enabled", &e.enabled);
    changed |= ImGui::ColorEdit3("Tint", e.color);
    changed |= ImGui::SliderFloat("Strength", &e.strength, 0.0f, 4.0f, "%.2f");
    bool light = e.light > 0;
    if (ImGui::Checkbox("Is a light (lights the room with ray tracing / GI)", &light))
        e.light = light ? 1.0f : 0.0f, changed = true;
    if (light)
        changed |= ImGui::SliderFloat("Light weight", &e.light, 0.05f, 4.0f, "%.2f");
    ImGui::SeparatorText("Mask");
    for (size_t i = 0; i < e.mask.rects.size(); i++) {
        ImGui::PushID((int)i);
        int r[4] = { e.mask.rects[i][0], e.mask.rects[i][1], e.mask.rects[i][2], e.mask.rects[i][3] };
        if (ImGui::InputInt4("Rect x0 y0 x1 y1", r)) {
            for (int k = 0; k < 4; k++)
                e.mask.rects[i][k] = r[k];
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            e.mask.rects.erase(e.mask.rects.begin() + (long)i);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (e.mask.rects.empty() && e.mask.polygons.empty())
        ImGui::TextDisabled("No shapes: the whole texture.");
    if (!e.mask.polygons.empty())
        ImGui::TextDisabled("%d polygon(s) (edit in the JSON)", (int)e.mask.polygons.size());
    if (ImGui::SmallButton("Add rect")) {
        int w = e.match.empty() ? 8 : e.match[0].w, h = e.match.empty() ? 8 : e.match[0].h;
        e.mask.rects.push_back({ 0, 0, w, h });
        changed = true;
    }
    bool indices = e.mask.has_indices;
    if (ImGui::Checkbox("CLUT index filter", &indices))
        e.mask.has_indices = indices, changed = true;
    if (indices) {
        char text[1024] = "";
        int n = 0;
        for (int i = 0; i < 256; i++)
            if (e.mask.indices[i >> 3] & (1 << (i & 7)))
                n += snprintf(text + n, sizeof text - (size_t)n, "%s%d", n ? " " : "", i);
        if (ImGui::InputText("Indices", text, sizeof text, ImGuiInputTextFlags_EnterReturnsTrue)) {
            memset(e.mask.indices, 0, sizeof e.mask.indices);
            for (char* s = strtok(text, " ,"); s; s = strtok(nullptr, " ,")) {
                int i = atoi(s);
                if (i >= 0 && i < 256)
                    e.mask.indices[i >> 3] |= (uint8_t)(1 << (i & 7));
            }
            changed = true;
        }
        if (picked.valid) {
            unsigned index = 0;
            texel((unsigned)picked.u, (unsigned)picked.v, picked.tpage, picked.clut, &index);
            ImGui::SameLine();
            char label[48];
            snprintf(label, sizeof label, "+ picked (%u)", index);
            if (ImGui::SmallButton(label)) {
                e.mask.indices[index >> 3] |= (uint8_t)(1 << (index & 7));
                changed = true;
            }
        }
    }
    bool colour = e.mask.has_color;
    if (ImGui::Checkbox("Colour range filter", &colour))
        e.mask.has_color = colour, changed = true;
    if (colour) {
        changed |= ImGui::SliderInt3("Min (5-bit RGB)", e.mask.cmin, 0, 31);
        changed |= ImGui::SliderInt3("Max (5-bit RGB)", e.mask.cmax, 0, 31);
        float l[2] = { e.mask.lmin, e.mask.lmax };
        if (ImGui::SliderFloat2("Luminance", l, 0.0f, 1.0f, "%.2f"))
            e.mask.lmin = l[0], e.mask.lmax = l[1], changed = true;
    }
    ImGui::SeparatorText("Textures");
    for (size_t i = 0; i < e.match.size(); i++) {
        ImGui::PushID((int)i + 1000);
        ImGui::Text("%s  %d x %d%s%s  at %d,%d", e.match[i].key.c_str(), e.match[i].w, e.match[i].h,
            e.match[i].clut.empty() ? "" : "  CLUT ", e.match[i].clut.c_str(), e.match[i].ox, e.match[i].oy);
        ImGui::SameLine();
        if (e.match.size() > 1 && ImGui::SmallButton("x")) {
            e.match.erase(e.match.begin() + (long)i);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (picked.valid && std::none_of(e.match.begin(), e.match.end(), [](const Match& m) { return m.key == picked.key; })
        && ImGui::SmallButton("Add the picked texture")) {
        Match m;
        m.key = picked.key;
        m.w = picked.box[2] - picked.box[0] + 1, m.h = picked.box[3] - picked.box[1] + 1;
        e.match.push_back(m);
        changed = true;
    }
    if (picked.valid) {
        for (Match& m : e.match)
            if (m.key == picked.key && !picked.clut_key.empty()) {
                bool only = m.clut == picked.clut_key;
                if (ImGui::Checkbox("Only with the picked texture's palette (CLUT)", &only))
                    m.clut = only ? picked.clut_key : "", changed = true;
            }
    }
    if (changed)
        put(e); /* live: the map is rebuilt before the next frame */
    texture_view(&e, &picked);
    ImGui::Separator();
    if (ImGui::Button("Save (yours)")) {
        std::string error;
        snprintf(status_line, sizeof status_line, "%s", save_user(&error) ? ("Saved " + user_path()).c_str() : error.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy JSON for the curated catalog")) {
        Entry copy = e;
        copy.review = "checked";
        ImGui::SetClipboardText(prefab::json_write(entry_to_json(copy)).c_str());
        snprintf(status_line, sizeof status_line, "Copied: paste it into %s's entries", catalog_path().c_str());
    }
    if (e.origin != ORIGIN_CURATED) {
        ImGui::SameLine();
        if (ImGui::Button(e.origin == ORIGIN_OVERRIDE ? "Revert to curated" : "Delete")) {
            remove(e.id);
            if (e.origin == ORIGIN_USER)
                selected.clear();
        }
    }
}

} // namespace

/* The view was clicked (lights.cpp, before its own tools): picks a texture
 * when the panel asked for one. Returns 1 when it took the click. */
extern "C" int pc_emissive_viewport_click(float x, float y)
{
    if (!pick_armed)
        return 0;
    pick_armed = false;
    Picked p;
    if (pick_pixel(x, y, p)) {
        picked = p;
        int s = slot_at((unsigned)p.u, (unsigned)p.v, p.tpage);
        if (s > 0 && s <= (int)catalog().entries.size())
            selected = catalog().entries[s - 1].id;
        else {
            selected.clear();
            for (const Entry& e : catalog().entries)
                for (const Match& m : e.match)
                    if (m.key == p.key)
                        selected = e.id;
        }
        snprintf(status_line, sizeof status_line, "Picked texture %s (%d x %d)", p.key.c_str(), p.box[2] - p.box[0] + 1,
            p.box[3] - p.box[1] + 1);
    } else
        snprintf(status_line, sizeof status_line, "Nothing textured there");
    return 1;
}

extern "C" void pc_emissive_panel(void)
{
    ImGui::SliderFloat("Glowing surfaces", &pc_emissive, 0.0f, 2.0f, pc_emissive <= 0 ? "Off" : "%.2f");
    ImGui::TextDisabled("Settings key emissive. 0: the original picture exactly.");
    ImGui::Text("%s. %d textures marked, %d texels, %d light triangles.", status().c_str(), marked_use_count(),
        marked_cell_count(), light_triangle_count());
    if (ImGui::Button(pick_armed ? "Click a surface in the view..." : "Pick texture"))
        pick_armed = !pick_armed;
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
        reload();
    if (user_dirty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "unsaved");
    }
    if (picked.valid) {
        ImGui::Text("Picked: %s %d x %d, page %u CLUT %u, texel %d,%d", picked.key.c_str(), picked.box[2] - picked.box[0] + 1,
            picked.box[3] - picked.box[1] + 1, picked.tpage, picked.clut, picked.u - picked.box[0],
            picked.v - picked.box[1]);
        if (ImGui::SmallButton("New entry for the picked texture")) {
            Entry e = entry_for(picked);
            put(e);
            selected = e.id;
        }
    }
    if (status_line[0])
        ImGui::TextWrapped("%s", status_line);
    if (ImGui::BeginListBox("##entries", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 7))) {
        std::vector<UseInfo> uses = use_list(false);
        for (const Entry& e : catalog().entries) {
            int marked = 0;
            for (const UseInfo& u : uses)
                marked += u.entry == e.id;
            char label[256];
            snprintf(label, sizeof label, "%s%s %s%s  [%d here]", e.origin == ORIGIN_CURATED ? "" : "* ", e.id.c_str(),
                e.name.c_str(), e.enabled ? "" : " (off)", marked);
            if (ImGui::Selectable(label, selected == e.id))
                selected = e.id;
        }
        ImGui::EndListBox();
    }
    if (const Entry* e = find(selected))
        entry_editor(*e);
    else if (picked.valid)
        texture_view(nullptr, &picked);
}

/* RPC emissive: {action: list (default) | uses {keys, matched} | pick {x, y}
 * or {world: [x, y, z]} | set {entry} or {id, fields...} | new {from the
 * last pick} | remove {id} | save | reload | export {id} | texel {u, v,
 * tpage, clut}}. */
extern "C" int pc_emissive_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != Json::Object)
        return fail("params: " + (error.empty() ? std::string("expected an object") : error), out, size);
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "list";
    auto number = [&](const char* k, double d) {
        const Json* v = params.get(k);
        return v && v->type == Json::Number ? v->number : d;
    };
    auto flag = [&](const char* k) {
        const Json* v = params.get(k);
        return v && ((v->type == Json::Bool && v->boolean) || (v->type == Json::Number && v->number != 0));
    };
    if (const Json* s = params.get("strength"); s && s->type == Json::Number)
        pc_emissive = (float)std::clamp(s->number, 0.0, 4.0);
    if (action == "list")
        return reply(list_json(), out, size);
    if (action == "uses") {
        bool keys = flag("keys"), matched = flag("matched");
        Json j = Json::object(), us = Json::array();
        int limit = (int)number("limit", 4000);
        for (const UseInfo& u : use_list(keys)) {
            if (matched && u.entry.empty())
                continue;
            if ((int)us.items.size() >= limit)
                break;
            Json uj = Json::object();
            uj.set("key", Json::str(u.key));
            uj.set("tpage", Json::num(u.page));
            uj.set("clut", Json::num(u.clut));
            Json box = Json::array();
            for (uint8_t b : u.box)
                box.items.push_back(Json::num(b));
            uj.set("box", box);
            uj.set("entry", Json::str(u.entry));
            if (flag("stats")) {
                /* Authoring: the texture's mean colour and its share of bright texels. */
                double sum[3] = { 0, 0, 0 };
                int n = 0, bright = 0;
                for (int v = u.box[1]; v <= u.box[3]; v++)
                    for (int x = u.box[0]; x <= u.box[2]; x++) {
                        uint16_t t = texel((unsigned)x, (unsigned)v, u.page, u.clut, nullptr);
                        if (!t)
                            continue;
                        double c[3] = { (t & 31) / 31.0, ((t >> 5) & 31) / 31.0, ((t >> 10) & 31) / 31.0 };
                        for (int k = 0; k < 3; k++)
                            sum[k] += c[k];
                        bright += 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2] > 0.6;
                        n++;
                    }
                Json mean = Json::array();
                for (double v : sum)
                    mean.items.push_back(Json::num(n ? round(v / n * 100) / 100 : 0));
                uj.set("mean", mean);
                uj.set("bright", Json::num(n ? round(bright * 100.0 / n) / 100 : 0));
                uj.set("refs", Json::num(u.refs));
            }
            us.items.push_back(uj);
        }
        j.set("uses", us);
        return reply(j, out, size);
    }
    if (action == "pick") {
        Picked p;
        float w[3];
        const Json* world = params.get("world");
        bool ok = false;
        if (world && world->type == Json::Array && world->items.size() == 3) {
            for (int k = 0; k < 3; k++)
                w[k] = (float)world->items[(size_t)k].number;
            ok = pick_world(w, p);
        } else
            ok = pick_pixel((float)number("x", 0), (float)number("y", 0), p);
        if (ok)
            picked = p;
        return reply(picked_json(ok ? p : Picked()), out, size);
    }
    if (action == "new") {
        if (!picked.valid)
            return fail("emissive new: pick a texture first", out, size);
        Entry e = entry_for(picked);
        put(e);
        selected = e.id;
        return reply(entry_to_json(e), out, size);
    }
    if (action == "set") {
        Entry e;
        if (const Json* ej = params.get("entry")) {
            Json copy = *ej;
            if (!copy.get("id") && copy.type == Json::Object)
                copy.set("id", Json::str(new_id()));
            if (!entry_from_json(copy, e, &error))
                return fail("emissive set: " + error, out, size);
        } else {
            const Json* id = params.get("id");
            if (!id || id->type != Json::String)
                return fail("emissive set: needs entry {...} or id", out, size);
            const Entry* old = find(id->string);
            if (!old)
                return fail("emissive set: unknown id " + id->string, out, size);
            Json merged = entry_to_json(*old);
            for (const auto& m : params.members)
                if (m.first != "action" && m.first != "id" && m.first != "strength")
                    merged.set(m.first.c_str(), m.second);
            if (!entry_from_json(merged, e, &error))
                return fail("emissive set: " + error, out, size);
        }
        put(e);
        Json j = entry_to_json(e);
        return reply(j, out, size);
    }
    if (action == "remove") {
        const Json* id = params.get("id");
        if (!id || id->type != Json::String || !remove(id->string))
            return fail("emissive remove: no such entry of yours", out, size);
        return reply(list_json(), out, size);
    }
    if (action == "save") {
        if (!save_user(&error))
            return fail("emissive save: " + error, out, size);
        return reply(list_json(), out, size);
    }
    if (action == "reload") {
        reload();
        return reply(list_json(), out, size);
    }
    if (action == "export") {
        const Json* id = params.get("id");
        const Entry* e = id && id->type == Json::String ? find(id->string) : nullptr;
        if (!e)
            return fail("emissive export: unknown id", out, size);
        Json j = Json::object();
        j.set("json", Json::str(prefab::json_write(entry_to_json(*e))));
        return reply(j, out, size);
    }
    if (action == "texture") {
        /* Authoring: a texture's CLUT indices (rows) and its palette, from
         * native VRAM (game data: for local tools, never for the repository). */
        unsigned tpage = (unsigned)number("tpage", 0), clut = (unsigned)number("clut", 0);
        const Json* b = params.get("box");
        if (!b || b->type != Json::Array || b->items.size() != 4)
            return fail("emissive texture: needs tpage, clut, box [u0, v0, u1, v1]", out, size);
        int box[4];
        for (int k = 0; k < 4; k++)
            box[k] = std::clamp((int)b->items[(size_t)k].number, 0, 255);
        Json j = Json::object(), rows = Json::array(), pal = Json::array();
        for (int v = box[1]; v <= box[3]; v++) {
            Json row = Json::array();
            for (int u = box[0]; u <= box[2]; u++) {
                unsigned index = 0;
                texel((unsigned)u, (unsigned)v, tpage, clut, &index);
                row.items.push_back(Json::num(index));
            }
            rows.items.push_back(row);
        }
        unsigned depth = (tpage >> 7) & 3;
        for (int i = 0; depth < 2 && i < (depth == 0 ? 16 : 256); i++) {
            uint16_t c = vram_at((int)(clut & 63) * 16 + i, (int)((clut >> 6) & 511));
            Json e = Json::array();
            e.items.push_back(Json::num(c & 31));
            e.items.push_back(Json::num((c >> 5) & 31));
            e.items.push_back(Json::num((c >> 10) & 31));
            pal.items.push_back(e);
        }
        j.set("indices", rows);
        j.set("palette", pal);
        j.set("key", Json::str(content_key(tpage, clut, box[0], box[1], box[2], box[3])));
        return reply(j, out, size);
    }
    if (action == "texel") {
        unsigned u = (unsigned)number("u", 0), v = (unsigned)number("v", 0);
        unsigned tpage = (unsigned)number("tpage", 0), clut = (unsigned)number("clut", 0);
        float rgb[3] = { 0, 0, 0 }, light = 0;
        int glows = pc_emissive_texel(u, v, tpage, clut, rgb, &light);
        Json j = Json::object();
        j.set("glows", Json::boolean_(glows != 0));
        Json c = Json::array();
        for (float x : rgb)
            c.items.push_back(Json::num(x));
        j.set("emission", c);
        j.set("light", Json::num(light));
        j.set("slot", Json::num(slot_at(u, v, tpage)));
        return reply(j, out, size);
    }
    return fail("emissive: action list, uses, pick, new, set, remove, save, reload, export or texel", out, size);
}
