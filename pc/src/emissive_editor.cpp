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
#include "materials.h"
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
    /* The surface, for scopes: SURFACE_ROOM (polygon: its byte offset in the
     * room data), SURFACE_MODEL (model: its hash) or SURFACE_OTHER; and the
     * room it was picked in. */
    int kind = SURFACE_OTHER;
    uint32_t polygon = 0, model = 0;
    int zone_id = -1, room_id = -1;
    float triangle[9] = { 0 }; /* the picked triangle (world space) */
};
Picked picked;
char status_line[512];

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
    o.kind = mesh_surface(o.mesh, render_emit_polygon_index(), &o.polygon, &o.model);
    memcpy(o.triangle, w, sizeof o.triangle);
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
    current_room(&s.out.zone_id, &s.out.room_id);
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
    /* The surface (scopes) and whether the entry's scope lets it glow there. */
    Json surface = Json::object();
    static const char* kinds[] = { "room", "model", "other" };
    surface.set("kind", Json::str(kinds[p.kind]));
    Json room = Json::array();
    room.items.push_back(Json::num(p.zone_id));
    room.items.push_back(Json::num(p.room_id));
    surface.set("room", room);
    if (p.kind == SURFACE_ROOM)
        surface.set("polygon", Json::num(p.polygon));
    if (p.kind == SURFACE_MODEL)
        surface.set("model", Json::str(model_key(p.model)));
    j.set("surface", surface);
    bool here = glows && s > 0 && s <= (int)c.entries.size()
        && scope_allows(c.entries[(size_t)s - 1], p.zone_id, p.room_id, p.kind, p.polygon, p.model);
    j.set("glows_here", Json::boolean_(here));
    return j;
}

/* The picked surface into (or out of) an entry's scope: a room polygon or a
 * model. Returns whether it is in the scope now. */
bool toggle_surface(Entry& e, const Picked& p)
{
    if (p.kind == SURFACE_ROOM) {
        Surface f;
        f.zone = p.zone_id, f.room = p.room_id, f.polygon = p.polygon;
        auto it = std::find(e.scope.surfaces.begin(), e.scope.surfaces.end(), f);
        if (it != e.scope.surfaces.end()) {
            e.scope.surfaces.erase(it);
            return false;
        }
        e.scope.surfaces.push_back(f);
        return true;
    }
    if (p.kind == SURFACE_MODEL) {
        auto it = std::find(e.scope.models.begin(), e.scope.models.end(), p.model);
        if (it != e.scope.models.end()) {
            e.scope.models.erase(it);
            return false;
        }
        e.scope.models.push_back(p.model);
        return true;
    }
    return false;
}
bool same_surface(const SurfaceUse& u, const Picked& p)
{
    return p.valid && u.kind == p.kind
        && (u.kind == SURFACE_ROOM ? u.polygon == p.polygon : u.kind == SURFACE_MODEL ? u.model == p.model : u.mesh == p.mesh);
}
struct Projector {
    float clip[16];
    float rect[4];
    bool valid = false;
};
Projector projector()
{
    Projector p;
    pc_overlay_view v;
    if (!pc_lights_tools_view || !pc_lights_tools_view(&v, nullptr))
        return p;
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float sum = 0;
            for (int k = 0; k < 4; k++)
                sum += v.projection[k * 4 + r] * v.view[c * 4 + k];
            p.clip[c * 4 + r] = sum;
        }
    memcpy(p.rect, v.rect, sizeof p.rect);
    p.valid = true;
    return p;
}
bool project(const Projector& p, const float w[3], ImVec2& out)
{
    float c[4];
    for (int r = 0; r < 4; r++)
        c[r] = p.clip[r] * w[0] + p.clip[4 + r] * w[1] + p.clip[8 + r] * w[2] + p.clip[12 + r];
    if (c[3] < 8.0f)
        return false;
    out = ImVec2(p.rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * p.rect[2], p.rect[1] + (0.5f - c[1] / c[3] * 0.5f) * p.rect[3]);
    return true;
}
bool in_view(const Projector& p, const SurfaceUse& u)
{
    ImVec2 s;
    for (const auto& t : u.triangles)
        for (int k = 0; k < 3; k++)
            if (project(p, &t[(size_t)k * 3], s) && s.x >= p.rect[0] && s.y >= p.rect[1] && s.x < p.rect[0] + p.rect[2]
                && s.y < p.rect[1] + p.rect[3])
                return true;
    return false;
}
Json surfaces_json(const std::string& id, const Picked& p)
{
    std::vector<SurfaceUse> list = surface_uses(id);
    Projector view = projector();
    Json j = Json::object(), items = Json::array();
    int affected = 0, other = 0, excluded = 0;
    static const char* kinds[] = { "room", "model", "other" };
    for (const SurfaceUse& u : list) {
        bool mine = u.listed || same_surface(u, p);
        affected += u.affected();
        other += u.affected() && !mine;
        excluded += !u.allowed && u.glowing > 0;
        Json uj = Json::object();
        uj.set("kind", Json::str(kinds[u.kind]));
        if (u.kind == SURFACE_ROOM)
            uj.set("polygon", Json::num(u.polygon));
        if (u.kind == SURFACE_MODEL)
            uj.set("model", Json::str(model_key(u.model)));
        uj.set("texels", Json::num(u.texels));
        uj.set("covered", Json::num(u.covered));
        uj.set("glowing", Json::num(u.glowing));
        uj.set("allowed", Json::boolean_(u.allowed));
        uj.set("listed", Json::boolean_(u.listed));
        uj.set("picked", Json::boolean_(same_surface(u, p)));
        Json c = Json::array();
        for (float v : u.centre)
            c.items.push_back(Json::num(round(v * 10) / 10));
        uj.set("centre", c);
        /* where it shows in the view (window pixels): its triangles' centres on screen */
        if (view.valid) {
            Json pts = Json::array();
            for (const auto& t : u.triangles) {
                float m[3] = { (t[0] + t[3] + t[6]) / 3, (t[1] + t[4] + t[7]) / 3, (t[2] + t[5] + t[8]) / 3 };
                ImVec2 sp;
                if (project(view, m, sp) && sp.x >= view.rect[0] && sp.y >= view.rect[1]
                    && sp.x < view.rect[0] + view.rect[2] && sp.y < view.rect[1] + view.rect[3]) {
                    Json q = Json::array();
                    q.items.push_back(Json::num(round(sp.x)));
                    q.items.push_back(Json::num(round(sp.y)));
                    pts.items.push_back(q);
                }
            }
            uj.set("screen", pts);
        }
        items.items.push_back(uj);
    }
    j.set("surfaces", items);
    j.set("affected", Json::num(affected)); /* surfaces it lights now */
    j.set("other", Json::num(other)); /* ... besides the picked one and the scope's own */
    j.set("excluded", Json::num(excluded)); /* surfaces its texels are on that its scope leaves out */
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

/* ---------------------------------------------------------------- painted maps */

/* Where a texture (by key) is in VRAM now: the last pick, else a live use. */
struct TextureAt {
    bool valid = false;
    unsigned tpage = 0, clut = 0;
    uint8_t box[4] = { 0, 0, 0, 0 };
    int w() const { return box[2] - box[0] + 1; }
    int h() const { return box[3] - box[1] + 1; }
};
TextureAt texture_at(const std::string& key)
{
    TextureAt t;
    if (picked.valid && picked.key == key) {
        t.valid = true, t.tpage = picked.tpage, t.clut = picked.clut;
        memcpy(t.box, picked.box, 4);
        return t;
    }
    for (const UseInfo& u : use_list(true))
        if (u.key == key) {
            t.valid = true, t.tpage = u.page, t.clut = u.clut;
            memcpy(t.box, u.box, 4);
            return t;
        }
    return t;
}
/* The texture's texels as RGBA (the reference to paint over: game art, for
 * the user's own folder only). */
std::vector<uint8_t> texture_rgba(const TextureAt& t)
{
    std::vector<uint8_t> rgba((size_t)t.w() * t.h() * 4);
    for (int y = 0; y < t.h(); y++)
        for (int x = 0; x < t.w(); x++) {
            uint16_t c = texel((unsigned)(t.box[0] + x), (unsigned)(t.box[1] + y), t.tpage, t.clut, nullptr);
            uint8_t* p = &rgba[((size_t)y * t.w() + x) * 4];
            p[0] = (uint8_t)((c & 31) * 255 / 31), p[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
            p[2] = (uint8_t)(((c >> 10) & 31) * 255 / 31), p[3] = c ? 255 : 0;
        }
    return rgba;
}
/* A texture's map values (zeros when it has none yet). */
std::vector<uint8_t> map_values(const std::string& key, const std::string& channel, int w, int h)
{
    const materials::Channel* c = materials::channel(key, channel);
    if (c && c->w == w && c->h == h)
        return c->values;
    return std::vector<uint8_t>((size_t)w * h, 0);
}

/* The brush (texture view): painting into the displayed texture's map. */
struct Brush {
    bool on = false;
    float radius = 1.0f, hardness = 1.0f, value = 255.0f, opacity = 1.0f;
    float reference = 0.6f; /* the texture underneath, its opacity */
    std::string key; /* the stroke's texture */
    std::vector<uint8_t> values; /* the map being painted (written on release) */
    std::vector<std::vector<uint8_t>> undo; /* before each stroke (this texture) */
    std::string undo_key;
};
Brush brush;
void dab(int w, int h, float cx, float cy, float target)
{
    int r = (int)ceilf(brush.radius);
    for (int y = (int)floorf(cy) - r; y <= (int)floorf(cy) + r; y++)
        for (int x = (int)floorf(cx) - r; x <= (int)floorf(cx) + r; x++) {
            if (x < 0 || y < 0 || x >= w || y >= h)
                continue;
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / std::max(brush.radius, 0.5f);
            if (d > 1.0f)
                continue;
            float k = brush.opacity * (d <= brush.hardness ? 1.0f : (1.0f - d) / std::max(1.0f - brush.hardness, 1e-3f));
            uint8_t& v = brush.values[(size_t)y * w + x];
            v = (uint8_t)std::clamp((int)lroundf(v + (target - v) * k), 0, 255);
        }
}
bool save_map(const std::string& key, const std::string& channel, int w, int h, const std::vector<uint8_t>& values)
{
    std::string error;
    if (!materials::write_channel(key, channel, w, h, values.data(), &error)) {
        snprintf(status_line, sizeof status_line, "%s", error.c_str());
        return false;
    }
    return true;
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
/* Scopes: clicks in the view add or remove surfaces of the selected entry;
 * "Show other uses" outlines the surfaces it lights in the view. */
bool surface_pick_armed;
bool show_uses;
std::vector<SurfaceUse> uses_shown;
std::string uses_for;
int uses_age = 1000;

/* ---------------------------------------------------------------- view overlay */

/* The selected entry's surfaces in the view: orange the other surfaces it
 * lights (besides the picked one and its scope's own), green its scope's,
 * grey dashed-ish those its scope leaves out. Returns the other ones in view. */
int draw_uses(const Entry& e)
{
    if (uses_for != e.id || ++uses_age > 15) {
        uses_shown = surface_uses(e.id);
        uses_for = e.id;
        uses_age = 0;
    }
    Projector p = projector();
    if (!p.valid)
        return 0;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float pulse = 0.6f + 0.4f * sinf((float)ImGui::GetTime() * 5.0f);
    int other = 0;
    for (const SurfaceUse& u : uses_shown) {
        bool mine = u.listed || same_surface(u, picked);
        ImU32 fill, line;
        if (u.affected() && !mine) {
            fill = IM_COL32(255, 140, 0, (int)(90 * pulse));
            line = IM_COL32(255, 160, 40, 255);
        } else if (u.affected()) {
            fill = IM_COL32(60, 255, 120, 50);
            line = IM_COL32(60, 255, 120, 220);
        } else if (u.glowing > 0) {
            fill = 0;
            line = IM_COL32(150, 150, 170, 160); /* left out by the scope */
        } else
            continue;
        bool shown = false;
        for (const auto& t : u.triangles) {
            ImVec2 s[3];
            if (!project(p, &t[0], s[0]) || !project(p, &t[3], s[1]) || !project(p, &t[6], s[2]))
                continue;
            if (fill)
                dl->AddTriangleFilled(s[0], s[1], s[2], fill);
            dl->AddTriangle(s[0], s[1], s[2], line, 2.0f);
            shown = true;
        }
        other += shown && u.affected() && !mine && in_view(p, u);
    }
    return other;
}

void texture_view(const Entry* e, const Picked* p)
{
    /* The texture: the picked one, else the first live use of the entry. */
    unsigned tpage = 0, clut = 0;
    uint8_t box[4];
    int ox = 0, oy = 0;
    bool have = false;
    std::string key;
    if (p && p->valid && (!e || std::any_of(e->match.begin(), e->match.end(), [&](const Match& m) { return m.key == p->key; }))) {
        tpage = p->tpage, clut = p->clut;
        memcpy(box, p->box, 4);
        have = true;
        key = p->key;
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
                key = u.key;
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
    /* A painted map: its values over the texture (the brush paints them). */
    bool painted = e && !e->mask.map.empty();
    const materials::Channel* map = painted ? materials::channel(key, e->mask.map) : nullptr;
    bool painting = painted && brush.on;
    if (painting && (brush.key != key || brush.values.size() != (size_t)w * h)) {
        brush.key = key;
        brush.values = map_values(key, e->mask.map, w, h);
    }
    const std::vector<uint8_t>* values = painting ? &brush.values
        : map && map->w == w && map->h == h ? &map->values
                                            : nullptr;
    float cell = std::clamp(256.0f / (float)std::max(w, h), 2.0f, 12.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float time = (float)ImGui::GetTime();
    float pulse = 0.5f + 0.5f * sinf(time * 4.0f);
    float under = painted ? brush.reference : 1.0f;
    for (int y = 0; y < h && y < 256; y++)
        for (int x = 0; x < w && x < 256; x++) {
            unsigned index = 0;
            uint16_t t = texel((unsigned)(box[0] + x), (unsigned)(box[1] + y), tpage, clut, &index);
            ImVec2 a(origin.x + x * cell, origin.y + y * cell), b(a.x + cell, a.y + cell);
            ImU32 col = t ? IM_COL32((int)((t & 31) * 255 / 31 * under), (int)(((t >> 5) & 31) * 255 / 31 * under),
                                (int)(((t >> 10) & 31) * 255 / 31 * under), 255)
                          : IM_COL32(40, 0, 40, 255);
            dl->AddRectFilled(a, b, col);
            int amount = values ? (*values)[(size_t)y * w + x] : 255;
            if (values && amount)
                dl->AddRectFilled(a, b, IM_COL32(255, 230, 120, amount * 3 / 4));
            if (e && amount && mask_covers(e->mask, x + ox, y + oy)) {
                bool pass = filters_pass(e->mask, index, t, (tpage >> 7) & 3);
                if (pass && t && !painted)
                    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (int)(60 * pulse)));
                if (!painted || cell >= 6)
                    dl->AddRect(a, b, pass ? IM_COL32(255, 220, 64, 200) : IM_COL32(120, 120, 255, 120));
            }
        }
    if (p && p->valid && p->tpage == tpage && p->clut == clut && !memcmp(p->box, box, 4)) {
        int x = p->u - p->box[0], y = p->v - p->box[1];
        ImVec2 a(origin.x + x * cell, origin.y + y * cell);
        dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
    }
    ImGui::InvisibleButton("##texture", ImVec2(w * cell, h * cell),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (painting) {
        /* The brush: left paints the value, right erases; one stroke from
         * press to release, written into the map (your layer) on release. */
        ImVec2 m = ImGui::GetIO().MousePos;
        float cx = (m.x - origin.x) / cell, cy = (m.y - origin.y) / cell;
        if (ImGui::IsItemHovered())
            dl->AddCircle(m, brush.radius * cell, IM_COL32(255, 255, 255, 200));
        if (ImGui::IsItemActivated()) {
            if (brush.undo_key != key)
                brush.undo.clear(), brush.undo_key = key;
            brush.undo.push_back(brush.values);
            if (brush.undo.size() > 32)
                brush.undo.erase(brush.undo.begin());
        }
        if (ImGui::IsItemActive())
            dab(w, h, cx, cy, ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 0.0f : brush.value);
        if (ImGui::IsItemDeactivated() && save_map(key, e->mask.map, w, h, brush.values))
            snprintf(status_line, sizeof status_line, "Painted %s's %s map", key.c_str(), e->mask.map.c_str());
        ImGui::TextDisabled("%d x %d texels. Left: paint %d, right: erase.", w, h, (int)brush.value);
        return;
    }
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

/* The painted-map part of the Mask section: per texture of the entry, its
 * map (yours, shipped, none), export / import, the brush. */
void painted_map_section(Entry& e)
{
    static char import_path[512];
    for (const MapProblem& mp : map_problems())
        if (mp.entry == e.id)
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s: %s (it does not glow)", mp.key.c_str(), mp.problem.c_str());
    for (size_t i = 0; i < e.match.size(); i++) {
        const std::string& key = e.match[i].key;
        ImGui::PushID((int)i + 3000);
        const materials::Channel* c = materials::channel(key, e.mask.map);
        if (c)
            ImGui::Text("%s: %s map %d x %d", key.c_str(), c->shipped ? "shipped" : "your", c->w, c->h);
        else
            ImGui::Text("%s: no %s map yet", key.c_str(), e.mask.map.c_str());
        TextureAt t = texture_at(key);
        if (t.valid) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Export reference")) {
                std::vector<uint8_t> rgba = texture_rgba(t);
                std::vector<std::string> written;
                std::string error;
                if (materials::export_reference(key, t.w(), t.h(), rgba.data(), "", e.mask.map, &written, &error)) {
                    std::string list;
                    for (const std::string& f : written)
                        list += (list.empty() ? "" : ", ") + f;
                    snprintf(status_line, sizeof status_line,
                        "Exported %s (your own folder: never commit the reference, it is the game's art)", list.c_str());
                } else
                    snprintf(status_line, sizeof status_line, "%s", error.c_str());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The texture as reference.png in %s/%s (local only, never committed),\n"
                                  "and an empty %s.png in your materials folder to paint in any editor:\n"
                                  "it reloads when the file changes.",
                    materials::export_dir().c_str(), key.c_str(), e.mask.map.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Import")) {
                std::string error;
                if (materials::import_channel(key, e.mask.map, import_path, t.w(), t.h(), &error))
                    snprintf(status_line, sizeof status_line, "Imported %s as %s's %s map", import_path, key.c_str(),
                        e.mask.map.c_str());
                else
                    snprintf(status_line, sizeof status_line, "%s", error.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy path")) {
            std::string path = materials::user_dir() + "/" + key + "/" + e.mask.map + ".png";
            ImGui::SetClipboardText(path.c_str());
            snprintf(status_line, sizeof status_line, "Copied %s", path.c_str());
        }
        ImGui::PopID();
    }
    ImGui::InputText("PNG to import", import_path, sizeof import_path);
    ImGui::TextDisabled("Grayscale (or its alpha), the texture's size; white glows fully.");
    ImGui::Checkbox("Paint", &brush.on);
    if (brush.on) {
        ImGui::SliderFloat("Brush radius (texels)", &brush.radius, 0.5f, 16.0f, "%.1f");
        ImGui::SliderFloat("Hardness", &brush.hardness, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Opacity", &brush.opacity, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("Value", &brush.value, 0.0f, 255.0f, "%.0f");
        ImGui::SliderFloat("Texture underneath", &brush.reference, 0.0f, 1.0f, "%.2f");
        TextureAt t = texture_at(brush.key);
        if (!brush.key.empty() && t.valid && brush.values.size() == (size_t)t.w() * t.h()) {
            auto edit = [&](const char* what, auto&& change) {
                if (brush.undo_key != brush.key)
                    brush.undo.clear(), brush.undo_key = brush.key;
                brush.undo.push_back(brush.values);
                change();
                if (save_map(brush.key, e.mask.map, t.w(), t.h(), brush.values))
                    snprintf(status_line, sizeof status_line, "%s: %s's %s map", what, brush.key.c_str(), e.mask.map.c_str());
            };
            uint8_t value = (uint8_t)brush.value;
            if (ImGui::SmallButton("Fill"))
                edit("Filled", [&] { std::fill(brush.values.begin(), brush.values.end(), value); });
            ImGui::SameLine();
            if (ImGui::SmallButton("Invert"))
                edit("Inverted", [&] {
                    for (uint8_t& v : brush.values)
                        v = (uint8_t)(255 - v);
                });
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
                edit("Cleared", [&] { std::fill(brush.values.begin(), brush.values.end(), 0); });
            /* Magic wands, to start from: the texels the entry's shapes and
             * filters pass, or those with the picked texel's CLUT index. */
            if (ImGui::SmallButton("Wand: the filters' texels"))
                edit("Wand", [&] {
                    for (int y = 0; y < t.h(); y++)
                        for (int x = 0; x < t.w(); x++) {
                            unsigned index = 0;
                            uint16_t c = texel((unsigned)(t.box[0] + x), (unsigned)(t.box[1] + y), t.tpage, t.clut, &index);
                            if (c && mask_covers(e.mask, x, y) && filters_pass(e.mask, index, c, (t.tpage >> 7) & 3))
                                brush.values[(size_t)y * t.w() + x] = value;
                        }
                });
            if (picked.valid && picked.key == brush.key) {
                unsigned want = 0;
                texel((unsigned)picked.u, (unsigned)picked.v, picked.tpage, picked.clut, &want);
                ImGui::SameLine();
                char label[64];
                snprintf(label, sizeof label, "Wand: CLUT index %u (picked)", want);
                if (ImGui::SmallButton(label))
                    edit("Wand", [&] {
                        for (int y = 0; y < t.h(); y++)
                            for (int x = 0; x < t.w(); x++) {
                                unsigned index = 0;
                                texel((unsigned)(t.box[0] + x), (unsigned)(t.box[1] + y), t.tpage, t.clut, &index);
                                if (index == want)
                                    brush.values[(size_t)y * t.w() + x] = value;
                            }
                    });
            }
            if (!brush.undo.empty() && brush.undo_key == brush.key) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Undo stroke")) {
                    brush.values = brush.undo.back();
                    brush.undo.pop_back();
                    save_map(brush.key, e.mask.map, t.w(), t.h(), brush.values);
                }
            }
        }
    }
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
    bool painted = !e.mask.map.empty();
    if (ImGui::Checkbox("Painted map (materials/<texture key>/emissive.png)", &painted))
        e.mask.map = painted ? "emissive" : "", changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Each texel glows by the painted value (0 none, 255 fully), times the shapes and filters above.\n"
                          "Paint here, or export the texture as a reference and paint the map in any editor.");
    if (painted)
        painted_map_section(e);
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
    ImGui::SeparatorText("Scope");
    {
        int zone = -1, room = -1;
        current_room(&zone, &room);
        if (e.scope.empty())
            ImGui::TextDisabled("Everywhere: every surface that samples these texels glows.");
        for (size_t i = 0; i < e.scope.rooms.size(); i++) {
            ImGui::PushID((int)i + 2000);
            ImGui::Text("Room %d/%d", e.scope.rooms[i][0], e.scope.rooms[i][1]);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                e.scope.rooms.erase(e.scope.rooms.begin() + (long)i);
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        int here = 0;
        for (const Surface& f : e.scope.surfaces)
            here += f.zone == zone && f.room == room;
        if (!e.scope.surfaces.empty())
            ImGui::Text("%d picked surface(s), %d in this room", (int)e.scope.surfaces.size(), here);
        if (!e.scope.models.empty()) {
            std::string models;
            for (uint32_t m : e.scope.models)
                models += (models.empty() ? "" : ", ") + model_key(m);
            ImGui::Text("Models: %s", models.c_str());
        }
        bool limited = std::find(e.scope.rooms.begin(), e.scope.rooms.end(), std::array<int, 2> { zone, room }) != e.scope.rooms.end();
        if (zone >= 0 && ImGui::Checkbox("Limit to this room", &limited)) {
            std::array<int, 2> r = { zone, room };
            auto it = std::find(e.scope.rooms.begin(), e.scope.rooms.end(), r);
            if (limited && it == e.scope.rooms.end())
                e.scope.rooms.push_back(r);
            if (!limited && it != e.scope.rooms.end())
                e.scope.rooms.erase(it);
            changed = true;
        }
        if (ImGui::Button(surface_pick_armed ? "Picking surfaces: click to add / remove (done)" : "Limit to picked surfaces"))
            surface_pick_armed = !surface_pick_armed;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click surfaces in the view: a room polygon, or a character or prop (its model).\n"
                              "Click a listed one again to remove it. The entry then glows only on them.");
        if (picked.valid && picked.kind != SURFACE_OTHER) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Add / remove the picked surface"))
                toggle_surface(e, picked), changed = true;
        }
        if (!e.scope.surfaces.empty() || !e.scope.models.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear surfaces"))
                e.scope.surfaces.clear(), e.scope.models.clear(), changed = true;
        }
        ImGui::Checkbox("Show other uses", &show_uses);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Outline in the view every other surface these texels light (orange), the scope's own "
                              "(green) and those the scope leaves out (grey).");
        if (show_uses) {
            int in_view_other = draw_uses(e);
            int affected = 0, other = 0, excluded = 0;
            for (const SurfaceUse& u : uses_shown) {
                bool mine = u.listed || same_surface(u, picked);
                affected += u.affected(), other += u.affected() && !mine, excluded += !u.allowed && u.glowing > 0;
            }
            ImGui::SameLine();
            if (other)
                ImGui::TextColored(ImVec4(1, 0.6f, 0.15f, 1), "%d other surface(s) glow too (%d in view)", other, in_view_other);
            else
                ImGui::TextDisabled("no other surface glows");
            ImGui::TextDisabled("Lights %d surface(s) in this room; the scope leaves out %d.", affected, excluded);
        }
        if (scope_overflow_count() > 0)
            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%d limited entries over the 16 shown at once: not drawn",
                scope_overflow_count());
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
    if (surface_pick_armed) {
        const Entry* e = find(selected);
        if (!e) {
            surface_pick_armed = false;
            return 0;
        }
        Picked p;
        if (!pick_pixel(x, y, p) || p.kind == SURFACE_OTHER) {
            snprintf(status_line, sizeof status_line, "No room polygon or model there");
            return 1;
        }
        picked = p;
        Entry copy = *e;
        bool added = toggle_surface(copy, p);
        put(copy);
        uses_age = 1000;
        if (p.kind == SURFACE_ROOM)
            snprintf(status_line, sizeof status_line, "%s room polygon %u (room %d/%d)", added ? "Added" : "Removed",
                p.polygon, p.zone_id, p.room_id);
        else
            snprintf(status_line, sizeof status_line, "%s model %s", added ? "Added" : "Removed", model_key(p.model).c_str());
        return 1;
    }
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
    if (action == "surfaces") {
        /* The surfaces an entry's texels are on now (room polygons of the
         * room, models and other blocks drawn): whether each glows, is left
         * out by the scope, is the scope's own or the last pick; counts. */
        const Json* id = params.get("id");
        if (!id || id->type != Json::String || !find(id->string))
            return fail("emissive surfaces: unknown id", out, size);
        return reply(surfaces_json(id->string, picked), out, size);
    }
    if (action == "materials" || action == "export_reference" || action == "import_map" || action == "paint") {
        /* Painted maps (materials.h): a texture's channels; its reference
         * exported (a local folder, never pc/assets); a PNG imported; texels
         * painted (rects [x0, y0, x1, y1, value], x1 y1 excluded; fill). The
         * texture: `key`, else the last pick (it must be in view). */
        const Json* k = params.get("key");
        std::string key = k && k->type == Json::String ? k->string : picked.valid ? picked.key : "";
        const Json* cj = params.get("channel");
        std::string channel = cj && cj->type == Json::String ? cj->string : "emissive";
        TextureAt t = texture_at(key);
        if (key.empty())
            return fail("emissive " + action + ": needs key or a pick", out, size);
        Json j = Json::object();
        j.set("key", Json::str(key));
        if (action == "export_reference") {
            if (!t.valid)
                return fail("emissive export_reference: texture " + key + " is not in view", out, size);
            const Json* dir = params.get("dir");
            std::vector<uint8_t> rgba = texture_rgba(t);
            std::vector<std::string> written;
            if (!materials::export_reference(key, t.w(), t.h(), rgba.data(), dir && dir->type == Json::String ? dir->string : "",
                    channel, &written, &error))
                return fail("emissive export_reference: " + error, out, size);
            Json files = Json::array();
            for (const std::string& f : written)
                files.items.push_back(Json::str(f));
            j.set("written", files);
        } else if (action == "import_map") {
            const Json* path = params.get("path");
            if (!path || path->type != Json::String || !t.valid)
                return fail("emissive import_map: needs path and the texture in view", out, size);
            if (!materials::import_channel(key, channel, path->string, t.w(), t.h(), &error))
                return fail("emissive import_map: " + error, out, size);
        } else if (action == "paint") {
            if (!t.valid)
                return fail("emissive paint: texture " + key + " is not in view", out, size);
            std::vector<uint8_t> values = map_values(key, channel, t.w(), t.h());
            if (const Json* f = params.get("fill"); f && f->type == Json::Number)
                std::fill(values.begin(), values.end(), (uint8_t)std::clamp((int)f->number, 0, 255));
            if (const Json* rs = params.get("rects"); rs && rs->type == Json::Array)
                for (const Json& r : rs->items) {
                    if (r.type != Json::Array || r.items.size() != 5)
                        continue;
                    int v[5];
                    for (int i = 0; i < 5; i++)
                        v[i] = (int)r.items[(size_t)i].number;
                    for (int y = std::max(0, v[1]); y < std::min(t.h(), v[3]); y++)
                        for (int x = std::max(0, v[0]); x < std::min(t.w(), v[2]); x++)
                            values[(size_t)y * t.w() + x] = (uint8_t)std::clamp(v[4], 0, 255);
                }
            if (!materials::write_channel(key, channel, t.w(), t.h(), values.data(), &error))
                return fail("emissive paint: " + error, out, size);
        }
        /* the texture's channels now */
        Json chans = Json::array();
        for (const materials::Present& c : materials::channels(key)) {
            Json cj2 = Json::object();
            cj2.set("name", Json::str(c.name));
            cj2.set("path", Json::str(c.path));
            cj2.set("user", Json::boolean_(c.user));
            if (const materials::Channel* m = materials::channel(key, c.name)) {
                cj2.set("width", Json::num(m->w));
                cj2.set("height", Json::num(m->h));
            }
            chans.items.push_back(cj2);
        }
        j.set("channels", chans);
        if (t.valid) {
            Json size2 = Json::array();
            size2.items.push_back(Json::num(t.w()));
            size2.items.push_back(Json::num(t.h()));
            j.set("size", size2);
        }
        Json probs = Json::array();
        for (const MapProblem& mp : map_problems()) {
            Json pj = Json::object();
            pj.set("entry", Json::str(mp.entry));
            pj.set("key", Json::str(mp.key));
            pj.set("problem", Json::str(mp.problem));
            probs.items.push_back(pj);
        }
        j.set("problems", probs);
        j.set("user_dir", Json::str(materials::user_dir()));
        j.set("export_dir", Json::str(materials::export_dir()));
        return reply(j, out, size);
    }
    if (action == "lights") {
        /* The room triangles that are lights (light entries' texels under
         * them, in scope): the glow colour each gives (glows.c), its texels
         * and how many glow. */
        Json j = Json::object(), items = Json::array();
        double total = 0;
        for (const LightInfo& l : light_list()) {
            Json lj = Json::object();
            lj.set("triangle", Json::num(l.index));
            lj.set("polygon", Json::num(l.polygon));
            Json c = Json::array();
            for (float v : l.rgb)
                c.items.push_back(Json::num(round(v * 10000) / 10000));
            lj.set("rgb", c);
            lj.set("texels", Json::num(l.texels));
            lj.set("glowing", Json::num(round(l.amount * 100) / 100));
            total += 0.299 * l.rgb[0] + 0.587 * l.rgb[1] + 0.114 * l.rgb[2];
            items.items.push_back(lj);
        }
        j.set("triangles", items);
        j.set("luminance", Json::num(round(total * 10000) / 10000)); /* summed over them */
        return reply(j, out, size);
    }
    if (action == "scope_pick") {
        /* The last pick's surface into (or out of) an entry's scope, as a
         * click with "Limit to picked surfaces" does. */
        const Json* id = params.get("id");
        const Entry* old = id && id->type == Json::String ? find(id->string) : nullptr;
        if (!old || !picked.valid || picked.kind == SURFACE_OTHER)
            return fail("emissive scope_pick: needs id and a picked room polygon or model", out, size);
        Entry e = *old;
        bool added = toggle_surface(e, picked);
        put(e);
        Json j = entry_to_json(e);
        j.set("added", Json::boolean_(added));
        return reply(j, out, size);
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
    return fail("emissive: action list, uses, pick, new, set, remove, save, reload, export, texel, surfaces, scope_pick, lights, materials, export_reference, import_map or paint", out, size);
}
