// Emissive texels (emissive.h, pc/docs/Emissive.md): the catalog, texture
// uses and their content keys, the slot map the draw shaders read, the CPU
// evaluation for glows, and the RPC. The editor panel is emissive_editor.cpp.
#include "emissive.h"
#include "emissive_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>

#include "pc_paths.h"
#include "platform.h"
#include "render.h"
#include "scene_capture.h"
#include "glow_overrides.h"
#include "dev_state.h"
#include "materials.h"
#include "frame_cost.h" /* ROOD_PERF: texels hashed */
#include "editor_undo.h"

extern "C" {
float pc_emissive = 1.0f;
/* Material response (emissive.h): the switch and the tunables. */
int pc_specular = 1;
float pc_specular_strength = 1.0f;
float pc_roughness_bias = 0.0f;
int pc_reflection_bounces = 1;
const uint16_t* pc_native_vram(void);
int pc_hw_present_all_lists(uint32_t* lists, int max) __attribute__((weak));
}

using prefab::Json;

namespace emissive {

/* ---------------------------------------------------------------- MD5 */
/* RFC 1321, for the fixture catalog's content keys (fixture_lib.py hashes
 * the same bytes with hashlib.md5). */
namespace {
struct Md5 {
    uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
    uint64_t length = 0;
    uint8_t block[64];
    int used = 0;
    static uint32_t rotl(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }
    void step(const uint8_t* p)
    {
        static const uint32_t k[64] = { 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
            0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
            0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
            0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
            0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9,
            0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5,
            0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
            0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235,
            0x2ad7d2bb, 0xeb86d391 };
        static const int r[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20,
            5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6,
            10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
        uint32_t w[16];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16
                | (uint32_t)p[i * 4 + 3] << 24;
        uint32_t A = a, B = b, C = c, D = d;
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16)
                f = (B & C) | (~B & D), g = i;
            else if (i < 32)
                f = (D & B) | (~D & C), g = (5 * i + 1) & 15;
            else if (i < 48)
                f = B ^ C ^ D, g = (3 * i + 5) & 15;
            else
                f = C ^ (B | ~D), g = (7 * i) & 15;
            uint32_t t = D;
            D = C;
            C = B;
            B = B + rotl(A + f + k[i] + w[g], r[i]);
            A = t;
        }
        a += A, b += B, c += C, d += D;
    }
    void add(const void* data, size_t n)
    {
        const uint8_t* p = (const uint8_t*)data;
        length += n;
        while (n) {
            size_t take = std::min<size_t>(n, 64 - (size_t)used);
            memcpy(block + used, p, take);
            used += (int)take, p += take, n -= take;
            if (used == 64) {
                step(block);
                used = 0;
            }
        }
    }
    void hex(char* out, int digits)
    {
        uint64_t bits = length * 8;
        uint8_t pad = 0x80;
        add(&pad, 1);
        pad = 0;
        while (used != 56)
            add(&pad, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; i++)
            l[i] = (uint8_t)(bits >> (8 * i));
        add(l, 8);
        uint32_t v[4] = { a, b, c, d };
        static const char* hexd = "0123456789abcdef";
        for (int i = 0; i < digits; i++) {
            uint8_t byte = (uint8_t)(v[i / 8] >> ((i / 2 % 4) * 8));
            out[i] = hexd[i & 1 ? byte & 15 : byte >> 4];
        }
        out[digits] = 0;
    }
};
} // namespace

/* ---------------------------------------------------------------- VRAM */

uint16_t vram_at(int x, int y)
{
    const uint16_t* v = pc_native_vram();
    return v ? v[(y & 511) * 1024 + (x & 1023)] : 0;
}
/* Texel (u, v) of a page: its colour and CLUT index (16-bit pages: the colour). */
uint16_t texel(unsigned u, unsigned v, unsigned tpage, unsigned clut, unsigned* index)
{
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
    int cx = (int)(clut & 63) * 16, cy = (int)((clut >> 6) & 511);
    unsigned depth = (tpage >> 7) & 3;
    u &= 255, v &= 255;
    if (depth == 0) {
        unsigned i = (vram_at(tx + (int)(u >> 2), ty + (int)v) >> ((u & 3) * 4)) & 15;
        if (index)
            *index = i;
        return vram_at(cx + (int)i, cy);
    }
    if (depth == 1) {
        unsigned i = (vram_at(tx + (int)(u >> 1), ty + (int)v) >> ((u & 1) * 8)) & 255;
        if (index)
            *index = i;
        return vram_at(cx + (int)i, cy);
    }
    uint16_t t = vram_at(tx + (int)u, ty + (int)v);
    if (index)
        *index = t;
    return t;
}
/* fixture_lib.py's ikey: md5 of the texel indices under the box (rows of v,
 * u along each row, uint16 little-endian) and the box's extents. */
std::string content_key(unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1)
{
    Md5 m;
    uint8_t row[512];
    for (int v = v0; v <= v1; v++) {
        int n = 0;
        for (int u = u0; u <= u1; u++) {
            unsigned i = 0;
            texel((unsigned)u, (unsigned)v, tpage, clut, &i);
            row[n++] = (uint8_t)i, row[n++] = (uint8_t)(i >> 8);
        }
        m.add(row, (size_t)n);
    }
    uint8_t size[2] = { (uint8_t)(u1 - u0), (uint8_t)(v1 - v0) };
    m.add(size, 2);
    char hex[11];
    m.hex(hex, 10);
    return hex;
}
/* The CLUT's colours (16 or 256 entries, uint16 little-endian): md5, 10 digits. */
std::string clut_key(unsigned tpage, unsigned clut)
{
    unsigned depth = (tpage >> 7) & 3;
    if (depth >= 2)
        return "";
    int n = depth == 0 ? 16 : 256, cx = (int)(clut & 63) * 16, cy = (int)((clut >> 6) & 511);
    Md5 m;
    for (int i = 0; i < n; i++) {
        uint16_t c = vram_at(cx + i, cy);
        uint8_t b[2] = { (uint8_t)c, (uint8_t)(c >> 8) };
        m.add(b, 2);
    }
    char hex[11];
    m.hex(hex, 10);
    return hex;
}

/* ---------------------------------------------------------------- catalog */

namespace {
Catalog curated, user, merged;
bool loaded;
long long curated_stamp, user_stamp;
int frames_since_check;
uint32_t catalog_generation = 1;
std::string status_text;
} // namespace

std::string catalog_path()
{
    const char* e = getenv("ROOD_EMISSIVE");
    return e && *e ? e : "pc/assets/emissive/catalog.json";
}
std::string user_path()
{
    const char* e = getenv("ROOD_EMISSIVE_USER");
    return e && *e ? e : pc_path_join("ROOD_LIGHTS", "pc/user/lighting", "emissive.json");
}
static long long stamp(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        return 0;
    return PC_MTIME_NS(st) + st.st_size;
}
static bool read_text(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buffer[8192];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof buffer, f)) > 0)
        out.append(buffer, n);
    fclose(f);
    return true;
}

static bool numbers(const Json* j, float* out, int n)
{
    if (!j || j->type != Json::Array || (int)j->items.size() != n)
        return false;
    for (int i = 0; i < n; i++) {
        if (j->items[i].type != Json::Number)
            return false;
        out[i] = (float)j->items[i].number;
    }
    return true;
}
static Json extensions(const Json& object, std::initializer_list<const char*> known)
{
    Json result = Json::object();
    for (const auto& field : object.members) {
        bool owned = false;
        for (const char* key : known) owned |= field.first == key;
        if (!owned) result.set(field.first.c_str(), field.second);
    }
    return result;
}
bool entry_from_json(const Json& j, Entry& e, std::string* error)
{
    e = Entry();
    if (j.type != Json::Object) {
        if (error)
            *error = "an entry is not an object";
        return false;
    }
    auto str = [&](const char* k) -> std::string {
        const Json* v = j.get(k);
        return v && v->type == Json::String ? v->string : "";
    };
    auto num = [&](const char* k, float d) -> float {
        const Json* v = j.get(k);
        return v && v->type == Json::Number ? (float)v->number : v && v->type == Json::Bool ? (v->boolean ? 1.0f : 0.0f) : d;
    };
    /* Preserve generator and future extension fields through editor round trips. */
    e.extra = extensions(j, { "id", "name", "where", "note", "review", "match", "mask", "scope",
        "color", "strength", "light", "light_reach", "light_lift", "enabled", "origin", "marked" });
    e.id = str("id");
    e.name = str("name");
    e.where = str("where");
    e.note = str("note");
    e.review = str("review");
    const Json* en = j.get("enabled");
    e.enabled = !en || (en->type == Json::Bool ? en->boolean : en->type == Json::Number ? en->number != 0 : true);
    numbers(j.get("color"), e.color, 3);
    e.strength = num("strength", 1.0f);
    e.light = num("light", 0.0f);
    e.light_reach = num("light_reach", 0.0f);
    e.light_lift = num("light_lift", 0.0f);
    if (const Json* ms = j.get("match"); ms && ms->type == Json::Array)
        for (const Json& mj : ms->items) {
            if (mj.type != Json::Object)
                continue;
            Match m;
            m.extra = extensions(mj, { "key", "size", "clut", "offset" });
            const Json* k = mj.get("key");
            if (!k || k->type != Json::String || k->string.size() != 10)
                continue;
            m.key = k->string;
            float size[2];
            if (numbers(mj.get("size"), size, 2))
                m.w = (int)size[0], m.h = (int)size[1];
            if (const Json* c = mj.get("clut"); c && c->type == Json::String)
                m.clut = c->string;
            float off[2];
            if (numbers(mj.get("offset"), off, 2))
                m.ox = (int)off[0], m.oy = (int)off[1];
            e.match.push_back(m);
        }
    if (const Json* mask = j.get("mask"); mask && mask->type == Json::Object) {
        e.mask.extra = extensions(*mask, { "rects", "polygons", "indices", "color_min", "color_max", "luma", "map" });
        if (const Json* rs = mask->get("rects"); rs && rs->type == Json::Array)
            for (const Json& r : rs->items) {
                float v[4];
                if (numbers(&r, v, 4))
                    e.mask.rects.push_back({ (int)v[0], (int)v[1], (int)v[2], (int)v[3] });
            }
        if (const Json* ps = mask->get("polygons"); ps && ps->type == Json::Array)
            for (const Json& p : ps->items) {
                if (p.type != Json::Array)
                    continue;
                std::vector<std::array<float, 2>> poly;
                for (const Json& q : p.items) {
                    float v[2];
                    if (numbers(&q, v, 2))
                        poly.push_back({ v[0], v[1] });
                }
                if (poly.size() >= 3)
                    e.mask.polygons.push_back(poly);
            }
        if (const Json* is = mask->get("indices"); is && is->type == Json::Array) {
            e.mask.has_indices = true;
            memset(e.mask.indices, 0, sizeof e.mask.indices);
            for (const Json& i : is->items)
                if (i.type == Json::Number && i.number >= 0 && i.number < 256)
                    e.mask.indices[(int)i.number >> 3] |= (uint8_t)(1 << ((int)i.number & 7));
        }
        float lo[3], hi[3], luma[2];
        if (numbers(mask->get("color_min"), lo, 3)) {
            e.mask.has_color = true;
            for (int k = 0; k < 3; k++)
                e.mask.cmin[k] = (int)lo[k];
        }
        if (numbers(mask->get("color_max"), hi, 3)) {
            e.mask.has_color = true;
            for (int k = 0; k < 3; k++)
                e.mask.cmax[k] = (int)hi[k];
        }
        if (numbers(mask->get("luma"), luma, 2)) {
            e.mask.has_color = true;
            e.mask.lmin = luma[0], e.mask.lmax = luma[1];
        }
        if (const Json* m = mask->get("map"); m && m->type == Json::String)
            e.mask.map = m->string;
    }
    if (const Json* scope = j.get("scope"); scope && scope->type == Json::Object) {
        e.scope.extra = extensions(*scope, { "rooms", "surfaces", "models" });
        if (const Json* rs = scope->get("rooms"); rs && rs->type == Json::Array)
            for (const Json& r : rs->items) {
                float v[2];
                if (numbers(&r, v, 2))
                    e.scope.rooms.push_back({ (int)v[0], (int)v[1] });
            }
        if (const Json* ss = scope->get("surfaces"); ss && ss->type == Json::Array)
            for (const Json& s : ss->items) {
                float room[2];
                const Json* ps = s.type == Json::Object ? s.get("polygons") : nullptr;
                if (!numbers(s.type == Json::Object ? s.get("room") : nullptr, room, 2) || !ps || ps->type != Json::Array)
                    continue;
                e.scope.surface_groups.push_back(s);
                for (const Json& p : ps->items)
                    if (p.type == Json::Number && p.number >= 0) {
                        Surface f;
                        f.extra = extensions(s, { "room", "polygons" });
                        f.zone = (int)room[0], f.room = (int)room[1], f.polygon = (uint32_t)p.number;
                        if (std::find(e.scope.surfaces.begin(), e.scope.surfaces.end(), f) == e.scope.surfaces.end())
                            e.scope.surfaces.push_back(f);
                    }
            }
        if (const Json* ms = scope->get("models"); ms && ms->type == Json::Array)
            for (const Json& m : ms->items) {
                uint32_t model;
                if (m.type == Json::String && parse_model_key(m.string, &model))
                    e.scope.models.push_back(model);
            }
    }
    if (e.id.empty()) {
        if (error)
            *error = "an entry has no id";
        return false;
    }
    return true;
}
static Json array_of(std::initializer_list<double> v)
{
    Json a = Json::array();
    for (double x : v)
        a.items.push_back(Json::num(x));
    return a;
}
static double tidy(float v) { return round((double)v * 1000.0) / 1000.0; }
Json entry_to_json(const Entry& e)
{
    Json j = e.extra;
    j.set("id", Json::str(e.id));
    if (!e.name.empty())
        j.set("name", Json::str(e.name));
    if (!e.where.empty())
        j.set("where", Json::str(e.where));
    Json ms = Json::array();
    for (const Match& m : e.match) {
        Json mj = m.extra;
        mj.set("key", Json::str(m.key));
        if (m.w > 0)
            mj.set("size", array_of({ (double)m.w, (double)m.h }));
        if (!m.clut.empty())
            mj.set("clut", Json::str(m.clut));
        if (m.ox || m.oy)
            mj.set("offset", array_of({ (double)m.ox, (double)m.oy }));
        ms.items.push_back(mj);
    }
    j.set("match", ms);
    Json mask = e.mask.extra;
    if (!e.mask.rects.empty()) {
        Json rs = Json::array();
        for (const auto& r : e.mask.rects)
            rs.items.push_back(array_of({ (double)r[0], (double)r[1], (double)r[2], (double)r[3] }));
        mask.set("rects", rs);
    }
    if (!e.mask.polygons.empty()) {
        Json ps = Json::array();
        for (const auto& p : e.mask.polygons) {
            Json pj = Json::array();
            for (const auto& q : p)
                pj.items.push_back(array_of({ tidy(q[0]), tidy(q[1]) }));
            ps.items.push_back(pj);
        }
        mask.set("polygons", ps);
    }
    if (e.mask.has_indices) {
        Json is = Json::array();
        for (int i = 0; i < 256; i++)
            if (e.mask.indices[i >> 3] & (1 << (i & 7)))
                is.items.push_back(Json::num(i));
        mask.set("indices", is);
    }
    if (e.mask.has_color) {
        mask.set("color_min", array_of({ (double)e.mask.cmin[0], (double)e.mask.cmin[1], (double)e.mask.cmin[2] }));
        mask.set("color_max", array_of({ (double)e.mask.cmax[0], (double)e.mask.cmax[1], (double)e.mask.cmax[2] }));
        mask.set("luma", array_of({ tidy(e.mask.lmin), tidy(e.mask.lmax) }));
    }
    if (!e.mask.map.empty())
        mask.set("map", Json::str(e.mask.map));
    j.set("mask", mask);
    if (!e.scope.empty() || !e.scope.extra.members.empty() || !e.scope.surface_groups.empty()) {
        Json scope = e.scope.extra;
        if (!e.scope.rooms.empty()) {
            Json rs = Json::array();
            for (const auto& r : e.scope.rooms)
                rs.items.push_back(array_of({ (double)r[0], (double)r[1] }));
            scope.set("rooms", rs);
        }
        if (!e.scope.surfaces.empty() || !e.scope.surface_groups.empty()) {
            Json ss = Json::array();
            std::vector<Surface> represented;
            for (const Json& original : e.scope.surface_groups) {
                float room[2];
                const Json* polygons = original.get("polygons");
                if (!numbers(original.get("room"), room, 2) || !polygons || polygons->type != Json::Array)
                    continue;
                Json group = original, ps = Json::array();
                for (const Json& p : polygons->items) {
                    if (p.type != Json::Number || p.number < 0) continue;
                    Surface f;
                    f.zone = (int)room[0], f.room = (int)room[1], f.polygon = (uint32_t)p.number;
                    if (std::find(e.scope.surfaces.begin(), e.scope.surfaces.end(), f) != e.scope.surfaces.end()) {
                        ps.items.push_back(p);
                        represented.push_back(f);
                    }
                }
                /* Keep originally empty extension-bearing groups, but do not
                 * retain a nonempty group whose polygons were explicitly removed. */
                if (!ps.items.empty() || polygons->items.empty()) {
                    group.set("polygons", ps);
                    ss.items.push_back(group);
                }
            }
            /* Newly added surfaces still get the canonical room grouping. */
            std::vector<Surface> sorted;
            for (const Surface& f : e.scope.surfaces)
                if (std::find(represented.begin(), represented.end(), f) == represented.end())
                    sorted.push_back(f);
            std::sort(sorted.begin(), sorted.end(), [](const Surface& a, const Surface& b) {
                return std::make_tuple(a.zone, a.room, a.polygon) < std::make_tuple(b.zone, b.room, b.polygon);
            });
            for (size_t i = 0; i < sorted.size();) {
                Json s = sorted[i].extra, ps = Json::array();
                s.set("room", array_of({ (double)sorted[i].zone, (double)sorted[i].room }));
                size_t k = i;
                for (; k < sorted.size() && sorted[k].zone == sorted[i].zone && sorted[k].room == sorted[i].room
                     && prefab::json_write(sorted[k].extra) == prefab::json_write(sorted[i].extra); k++)
                    ps.items.push_back(Json::num(sorted[k].polygon));
                s.set("polygons", ps);
                ss.items.push_back(s);
                i = k;
            }
            scope.set("surfaces", ss);
        }
        if (!e.scope.models.empty()) {
            Json ms = Json::array();
            for (uint32_t m : e.scope.models)
                ms.items.push_back(Json::str(model_key(m)));
            scope.set("models", ms);
        }
        j.set("scope", scope);
    }
    j.set("color", array_of({ tidy(e.color[0]), tidy(e.color[1]), tidy(e.color[2]) }));
    j.set("strength", Json::num(tidy(e.strength)));
    j.set("light", Json::num(tidy(e.light)));
    if (e.light_reach > 0)
        j.set("light_reach", Json::num(tidy(e.light_reach)));
    if (e.light_lift > 0)
        j.set("light_lift", Json::num(tidy(e.light_lift)));
    if (!e.enabled)
        j.set("enabled", Json::boolean_(false));
    if (!e.review.empty())
        j.set("review", Json::str(e.review));
    if (!e.note.empty())
        j.set("note", Json::str(e.note));
    return j;
}
static bool load_file(const std::string& path, Catalog& out, std::string& error)
{
    out = Catalog();
    std::string text;
    if (!read_text(path, text))
        return true; /* no file: empty */
    Json j;
    if (!prefab::json_parse(text.c_str(), j, &error) || j.type != Json::Object) {
        if (error.empty())
            error = "expected an object";
        error = path + ": " + error;
        return false;
    }
    if (const Json* es = j.get("entries"); es && es->type == Json::Array)
        for (const Json& ej : es->items) {
            Entry e;
            std::string why;
            if (entry_from_json(ej, e, &why))
                out.entries.push_back(e);
            else
                fprintf(stderr, "[emissive] %s: %s\n", path.c_str(), why.c_str());
        }
    return true;
}
static void merge()
{
    merged.entries = curated.entries;
    for (Entry& e : merged.entries)
        e.origin = ORIGIN_CURATED;
    for (const Entry& u : user.entries) {
        bool replaced = false;
        for (Entry& e : merged.entries)
            if (e.id == u.id) {
                e = u;
                e.origin = ORIGIN_OVERRIDE;
                replaced = true;
            }
        if (!replaced) {
            merged.entries.push_back(u);
            merged.entries.back().origin = ORIGIN_USER;
        }
    }
    if (merged.entries.size() > MAX_SLOTS - 1)
        merged.entries.resize(MAX_SLOTS - 1);
    ++catalog_generation;
}
namespace {
bool dirty_user;
std::string saved_user; /* the user layer as on disk (catalog_text, no comment): undo back to it is not unsaved */
}
void reload()
{
    bool again = loaded;
    std::string error;
    materials::forget(); /* painted maps read again too */
    if (!load_file(catalog_path(), curated, error))
        fprintf(stderr, "[emissive] %s\n", error.c_str());
    std::string uerror;
    if (!load_file(user_path(), user, uerror))
        fprintf(stderr, "[emissive] %s\n", uerror.c_str());
    curated_stamp = stamp(catalog_path());
    user_stamp = stamp(user_path());
    merge();
    loaded = true;
    dirty_user = false;
    saved_user = catalog_text(user, nullptr);
    if (again) /* read again (a hand edit, Reload): not an edit of the editor's */
        pc_undo_external_emissive();
    char line[256];
    snprintf(line, sizeof line, "%d curated, %d of yours%s%s", (int)curated.entries.size(), (int)user.entries.size(),
        error.empty() && uerror.empty() ? "" : "; error: ", (error + uerror).c_str());
    status_text = line;
}
static void ensure_loaded()
{
    if (!loaded)
        reload();
    /* Files edited by hand reload live (unsaved editor changes win). */
    if (++frames_since_check >= 60) {
        frames_since_check = 0;
        materials::poll();
        if (!user_dirty() && (stamp(catalog_path()) != curated_stamp || stamp(user_path()) != user_stamp))
            reload();
    }
}
const Catalog& catalog()
{
    if (!loaded)
        reload();
    return merged;
}
const Catalog& curated_catalog()
{
    if (!loaded)
        reload();
    return curated;
}
const std::string& status() { return status_text; }
uint32_t generation() { return catalog_generation; }

bool user_dirty() { return dirty_user; }
const Entry* find(const std::string& id)
{
    for (const Entry& e : catalog().entries)
        if (e.id == id)
            return &e;
    return nullptr;
}
int index_of(const std::string& id)
{
    const Catalog& c = catalog();
    for (size_t i = 0; i < c.entries.size(); i++)
        if (c.entries[i].id == id)
            return (int)i;
    return -1;
}
/* An edit: the entry goes into the user layer (an override of a curated one
 * keeps its id). */
void put(const Entry& e)
{
    catalog();
    bool found = false;
    for (Entry& u : user.entries)
        if (u.id == e.id) {
            u = e;
            found = true;
        }
    if (!found)
        user.entries.push_back(e);
    dirty_user = true;
    merge();
}
/* Drops the user's entry: a curated one comes back as shipped. */
bool remove(const std::string& id)
{
    catalog();
    for (size_t i = 0; i < user.entries.size(); i++)
        if (user.entries[i].id == id) {
            user.entries.erase(user.entries.begin() + (long)i);
            dirty_user = true;
            merge();
            return true;
        }
    return false;
}
std::string new_id()
{
    catalog();
    for (int n = 1;; n++) {
        char id[16];
        snprintf(id, sizeof id, "U%03d", n);
        if (index_of(id) < 0)
            return id;
    }
}
std::string catalog_text(const Catalog& c, const char* comment)
{
    Json j = Json::object();
    j.set("format", Json::num(1));
    if (comment)
        j.set("about", Json::str(comment));
    Json es = Json::array();
    for (const Entry& e : c.entries)
        es.items.push_back(entry_to_json(e));
    j.set("entries", es);
    return prefab::json_write(j) + "\n";
}
std::string user_text()
{
    catalog();
    static uint32_t made_for = ~0u;
    static std::string text;
    if (made_for != catalog_generation) { /* merge() bumps it on every change */
        text = catalog_text(user, nullptr);
        made_for = catalog_generation;
    }
    return text;
}
void set_user_text(const std::string& text)
{
    if (text == user_text())
        return;
    Catalog c;
    Json j;
    if (prefab::json_parse(text.c_str(), j) && j.type == Json::Object)
        if (const Json* es = j.get("entries"); es && es->type == Json::Array)
            for (const Json& ej : es->items) {
                Entry e;
                if (entry_from_json(ej, e, nullptr))
                    c.entries.push_back(e);
            }
    user = c;
    merge();
    dirty_user = user_text() != saved_user; /* undone back to what is saved: nothing unsaved */
}
static void mkdirs(const std::string& path)
{
    for (size_t i = 1; i < path.size(); i++)
        if (path[i] == '/')
            pc_mkdir(path.substr(0, i).c_str());
}
bool save_user(std::string* error)
{
    std::string path = user_path(), temp = path + ".tmp";
    mkdirs(path);
    FILE* f = fopen(temp.c_str(), "wb");
    if (!f) {
        if (error)
            *error = "cannot write " + temp;
        return false;
    }
    std::string text = catalog_text(user, "Your emissive surfaces (pc/docs/Emissive.md): entries with a curated id "
                                           "replace it, others are added.");
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    if (pc_replace_file(temp.c_str(), path.c_str())) {
        if (error)
            *error = "cannot replace " + path;
        return false;
    }
    user_stamp = stamp(path);
    dirty_user = false;
    saved_user = catalog_text(user, nullptr);
    return true;
}

/* ---------------------------------------------------------------- uses */

namespace {
struct Use {
    uint16_t page, clut; /* tpage & 0x19f (page and colour depth), CLUT */
    uint8_t box[4]; /* u0, v0, u1, v1 */
    int refs = 0;
    int state = 0; /* 0 to compute, 1 computed */
    std::string key; /* "" when no catalog size matches (not hashed) */
    int entry = -1, item = -1;
    uint32_t tiles = 0; /* VRAM tiles under its texels */
    /* The VRAM its key and CLUT key were taken from (its texels' halfwords,
     * row by row, then the CLUT's): an unmatched use is hashed again only
     * when this changed (same VRAM, same keys, same result). */
    std::vector<uint16_t> seen;
};
std::unordered_map<uint64_t, Use> uses;
std::unordered_map<uint64_t, std::vector<uint64_t>> instance_uses; /* instance key -> uses */
std::unordered_map<uint32_t, std::vector<uint64_t>> mesh_instances; /* mesh -> instance keys */
std::unordered_set<int> sizes; /* (w - 1) << 8 | (h - 1) of every match */
uint32_t applied_generation, dirty_tiles;
float applied_strength = -1;
bool marks_changed = true, params_changed = true, any_marks, marks_on;
std::vector<uint8_t> the_map(PC_EMISSIVE_MAP_W * PC_EMISSIVE_MAP_H);
int dirty_row0 = 0, dirty_row1 = PC_EMISSIVE_MAP_H; /* the first upload clears the GPU image */
uint32_t map_generation = 1;
int marked_uses, marked_cells;

/* Native meshes the scopes know (room_native.c, actor_native.c): a mesh of
 * the current room's geometry (its block's byte offset and triangle count:
 * polygon i's offset follows), or a model's (its hash). `applied`: the
 * scope bits per polygon render.c has (empty: none). */
struct MeshInfo {
    int kind = SURFACE_OTHER;
    unsigned offset = 0, tris = 0, total = 0;
    uint32_t model = 0;
    std::vector<uint16_t> applied;
};
std::unordered_map<uint32_t, MeshInfo> mesh_info;
/* Per room mesh (a group), whether the scene capture lacks any of its
 * polygons (object lights, below): only those of the room's list are
 * walked (the rest, most of the room, cost nothing). Per capture; a mesh
 * noted again is decided again. */
std::unordered_map<uint32_t, bool> mesh_uncaptured;
std::unordered_set<uint32_t> scope_pending; /* meshes noted since the bits were last written */
uint16_t slot_scope_bit[MAX_SLOTS]; /* per slot: its scope bit + 1 (0: everywhere it is marked) */
int scope_zone = -1, scope_room = -1; /* the room the marks were written for */
int scope_overflow; /* scoped entries left unmarked (more than 16 at once) */
uint32_t light_dirty_tiles; /* VRAM tiles changed since the lights' emission was last integrated */
uint32_t applied_materials; /* materials::generation() the marks were written with */
std::vector<MapProblem> problems;
uint32_t light_pages, marked_pages; /* bit per texture page (tpage & 31): a light entry's / any marked use */
/* Material response (materials.h): three planes of 512 rows (roughness,
 * specular, metal; emissive.h PC_MATERIAL_*), cell for cell like the slots. */
std::vector<uint8_t> material_planes;
bool materials_changed = true, any_material, material_dirty = true;
uint32_t applied_material_catalog, applied_material_maps;
int material_zone = -1, material_room = -1, material_on = -1;
int material_uses, material_cells;
std::vector<MaterialUse> material_list;

uint64_t use_key(unsigned page, unsigned clut, const uint8_t box[4])
{
    return (uint64_t)page << 48 | (uint64_t)clut << 32 | (uint64_t)box[0] << 24 | (uint64_t)box[1] << 16
        | (uint64_t)box[2] << 8 | box[3];
}
uint32_t tiles_of(unsigned page, const uint8_t box[4])
{
    int tx = (int)(page & 15) * 64, ty = (int)((page >> 4) & 1) * 256, depth = (int)((page >> 7) & 3);
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int x0 = tx + (box[0] >> shift), x1 = tx + (box[2] >> shift);
    uint32_t mask = 0;
    for (int x = x0 & ~63; x <= x1; x += 64)
        mask |= 1u << (((ty >> 8) & 1) * 16 + ((x & 1023) >> 6));
    return mask;
}
} // namespace

/* ---------------------------------------------------------------- scopes */

bool current_room(int* zone, int* room)
{
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    if (pc_dev_state)
        pc_dev_state(&s);
    *zone = s.valid ? s.zone : -1, *room = s.valid ? s.room : -1;
    return s.valid != 0;
}
bool in_room(const Scope& s, int zone, int room)
{
    if (s.rooms.empty())
        return true;
    for (const auto& r : s.rooms)
        if (r[0] == zone && r[1] == room)
            return true;
    return false;
}
bool scope_allows(const Entry& e, int zone, int room, int kind, uint32_t polygon, uint32_t model)
{
    if (!in_room(e.scope, zone, room))
        return false;
    if (!e.scope.per_surface())
        return true;
    if (kind == SURFACE_ROOM)
        for (const Surface& s : e.scope.surfaces)
            if (s.polygon == polygon && s.zone == zone && s.room == room)
                return true;
    if (kind == SURFACE_MODEL)
        for (uint32_t m : e.scope.models)
            if (m == model)
                return true;
    return false;
}
int mesh_surface(uint32_t mesh, int index, uint32_t* polygon, uint32_t* model)
{
    *polygon = 0, *model = 0;
    auto it = mesh_info.find(mesh);
    if (it == mesh_info.end())
        return SURFACE_OTHER;
    const MeshInfo& m = it->second;
    if (m.kind == SURFACE_MODEL) {
        *model = m.model;
        return SURFACE_MODEL;
    }
    if (m.kind == SURFACE_ROOM && index >= 0 && (unsigned)index < m.total) {
        unsigned i = (unsigned)index;
        *polygon = m.offset + 8 + (i < m.tris ? i * 32 : m.tris * 32 + (i - m.tris) * 40);
        return SURFACE_ROOM;
    }
    return SURFACE_OTHER;
}
std::string model_key(uint32_t model)
{
    char text[16];
    snprintf(text, sizeof text, "%08x", model);
    return text;
}
bool parse_model_key(const std::string& s, uint32_t* model)
{
    if (s.size() != 8 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    *model = (uint32_t)strtoul(s.c_str(), nullptr, 16);
    return true;
}

const std::vector<UseInfo> use_list(bool compute_keys)
{
    std::vector<UseInfo> out;
    for (auto& [k, u] : uses) {
        UseInfo i;
        i.page = u.page, i.clut = u.clut;
        memcpy(i.box, u.box, 4);
        i.key = u.key;
        if (compute_keys && i.key.empty())
            i.key = content_key(u.page, u.clut, u.box[0], u.box[1], u.box[2], u.box[3]);
        i.entry = u.entry >= 0 && u.entry < (int)catalog().entries.size() ? catalog().entries[u.entry].id : "";
        i.refs = u.refs;
        out.push_back(i);
    }
    std::sort(out.begin(), out.end(), [](const UseInfo& a, const UseInfo& b) {
        return std::make_tuple(a.page, a.clut, a.box[1], a.box[0], a.box[3], a.box[2])
            < std::make_tuple(b.page, b.clut, b.box[1], b.box[0], b.box[3], b.box[2]);
    });
    return out;
}
int marked_use_count() { return marked_uses; }
int marked_cell_count() { return marked_cells; }
int scope_overflow_count() { return scope_overflow; }

/* Is texture-space texel (x, y) inside the entry's shapes (no shapes: the
 * whole texture)? */
bool mask_covers(const Mask& m, int x, int y)
{
    if (m.rects.empty() && m.polygons.empty())
        return true;
    for (const auto& r : m.rects)
        if (x >= r[0] && y >= r[1] && x < r[2] && y < r[3])
            return true;
    float px = (float)x + 0.5f, py = (float)y + 0.5f;
    for (const auto& p : m.polygons) {
        bool in = false;
        for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
            if ((p[i][1] > py) != (p[j][1] > py)
                && px < (p[j][0] - p[i][0]) * (py - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0])
                in = !in;
        if (in)
            return true;
    }
    return false;
}
/* The slot filters (emissive.glsl's), on a texel's index and colour. */
bool filters_pass(const Mask& m, unsigned index, uint16_t t, unsigned depth)
{
    if (m.has_indices && depth < 2 && !(m.indices[(index >> 3) & 31] & (1 << (index & 7))))
        return false;
    if (m.has_color) {
        int c[3] = { t & 31, (t >> 5) & 31, (t >> 10) & 31 };
        for (int k = 0; k < 3; k++)
            if (c[k] < m.cmin[k] || c[k] > m.cmax[k])
                return false;
        float luma = (0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]) / 31.0f;
        if (luma < m.lmin - 0.5f / 255.0f || luma > m.lmax + 0.5f / 255.0f)
            return false;
    }
    return true;
}

static void write_params(float strength)
{
    uint8_t* p = &the_map[(size_t)512 * PC_EMISSIVE_MAP_W];
    memset(p, 0, (size_t)4 * PC_EMISSIVE_MAP_W);
    const Catalog& c = catalog();
    for (size_t i = 0; i < c.entries.size(); i++) {
        const Entry& e = c.entries[i];
        uint8_t* s = p + (i + 1) * 64;
        for (int k = 0; k < 3; k++)
            s[k] = (uint8_t)std::clamp((int)lroundf(e.color[k] * 255.0f), 0, 255);
        float st = e.enabled ? e.strength * strength : 0.0f;
        s[3] = (uint8_t)std::clamp((int)lroundf(st * 63.75f), 0, 255);
        for (int k = 0; k < 3; k++)
            s[4 + k] = (uint8_t)std::clamp(e.mask.has_color ? e.mask.cmin[k] : 0, 0, 31),
                  s[7 + k] = (uint8_t)std::clamp(e.mask.has_color ? e.mask.cmax[k] : 31, 0, 31);
        s[10] = (uint8_t)std::clamp((int)lroundf((e.mask.has_color ? e.mask.lmin : 0.0f) * 255.0f), 0, 255);
        s[11] = (uint8_t)std::clamp((int)lroundf((e.mask.has_color ? e.mask.lmax : 1.0f) * 255.0f), 0, 255);
        s[12] = (uint8_t)((e.mask.has_indices ? 1 : 0) | (e.mask.has_color ? 2 : 0) | (e.light > 0 ? 4 : 0));
        s[13] = (uint8_t)slot_scope_bit[i + 1]; /* 0: everywhere marked; else bit + 1 (emissive.glsl) */
        memcpy(s + 16, e.mask.indices, 32);
    }
}
/* Which entries are marked in this room, and the scope bits of those limited
 * to some surfaces or models (16 at once; more are left unmarked). */
static void assign_scopes(std::vector<char>& active)
{
    const Catalog& c = catalog();
    active.assign(c.entries.size(), 0);
    memset(slot_scope_bit, 0, sizeof slot_scope_bit);
    scope_overflow = 0;
    std::vector<char> used(c.entries.size(), 0);
    for (auto& kv : uses)
        if (kv.second.entry >= 0 && kv.second.entry < (int)c.entries.size())
            used[(size_t)kv.second.entry] = 1;
    int bits = 0;
    for (size_t i = 0; i < c.entries.size(); i++) {
        const Entry& e = c.entries[i];
        if (!used[i] || !in_room(e.scope, scope_zone, scope_room))
            continue;
        if (e.scope.per_surface()) {
            bool here = !e.scope.models.empty();
            for (const Surface& s : e.scope.surfaces)
                here |= s.zone == scope_zone && s.room == scope_room;
            if (!here)
                continue; /* only surfaces of other rooms */
            if (bits == 16) {
                ++scope_overflow;
                continue;
            }
            slot_scope_bit[i + 1] = (uint16_t)(++bits);
        }
        active[i] = 1;
    }
}
static void write_marks()
{
    memset(the_map.data(), 0, (size_t)512 * PC_EMISSIVE_MAP_W);
    memset(&the_map[(size_t)PC_EMISSIVE_AMOUNT_ROW * PC_EMISSIVE_MAP_W], 0, (size_t)512 * PC_EMISSIVE_MAP_W);
    problems.clear();
    applied_materials = materials::generation();
    const Catalog& c = catalog();
    marked_uses = marked_cells = 0;
    any_marks = false;
    light_pages = marked_pages = 0;
    current_room(&scope_zone, &scope_room);
    std::vector<char> active;
    assign_scopes(active);
    if (!marks_on)
        return; /* the setting is 0: nothing glows (matches are kept) */
    /* Entry order: a later entry wins where two cover the same texel. */
    std::vector<const Use*> order;
    for (auto& kv : uses)
        if (kv.second.entry >= 0 && kv.second.entry < (int)active.size() && active[(size_t)kv.second.entry])
            order.push_back(&kv.second);
    std::sort(order.begin(), order.end(), [](const Use* a, const Use* b) {
        return a->entry != b->entry ? a->entry < b->entry
                                    : std::make_tuple(a->page, a->clut, a->box[0], a->box[1], a->box[2], a->box[3])
                < std::make_tuple(b->page, b->clut, b->box[0], b->box[1], b->box[2], b->box[3]);
    });
    for (const Use* u : order) {
        const Entry& e = c.entries[u->entry];
        const Match& m = e.match[u->item];
        int tx = (int)(u->page & 15) * 64, ty = (int)((u->page >> 4) & 1) * 256;
        int depth = std::min((int)((u->page >> 7) & 3), 2);
        uint8_t slot = (uint8_t)(u->entry + 1);
        /* A painted map: the texture's channel, texel for texel over its box. */
        const materials::Channel* painted = nullptr;
        int w = u->box[2] - u->box[0] + 1, h = u->box[3] - u->box[1] + 1;
        if (!e.mask.map.empty()) {
            painted = materials::channel(u->key, e.mask.map);
            if (!painted || painted->w != w || painted->h != h) {
                char text[96];
                if (painted)
                    snprintf(text, sizeof text, "its %s map is %d x %d, the texture %d x %d", e.mask.map.c_str(),
                        painted->w, painted->h, w, h);
                else
                    snprintf(text, sizeof text, "no %s map", e.mask.map.c_str());
                problems.push_back({ e.id, u->key, text });
                continue; /* nothing glows without its map */
            }
        }
        ++marked_uses;
        marked_pages |= 1u << (u->page & 31);
        if (e.light > 0)
            light_pages |= 1u << (u->page & 31);
        for (int v = u->box[1]; v <= u->box[3]; v++)
            for (int x = u->box[0]; x <= u->box[2]; x++) {
                if (!mask_covers(e.mask, x - u->box[0] + m.ox, v - u->box[1] + m.oy))
                    continue;
                uint8_t amount = painted ? painted->values[(size_t)(v - u->box[1]) * w + (x - u->box[0])] : 255;
                if (!amount)
                    continue;
                size_t at = (size_t)(ty + v) * PC_EMISSIVE_MAP_W + ((tx * 4 + (x << depth)) & 4095);
                uint8_t& cell = the_map[at];
                marked_cells += cell == 0;
                cell = slot;
                the_map[(size_t)PC_EMISSIVE_AMOUNT_ROW * PC_EMISSIVE_MAP_W + at] = amount;
            }
    }
    any_marks = marked_uses > 0;
}
/* The VRAM a use's keys read (Use::seen), visited as runs of halfwords:
 * the rows of its texels, then the CLUT (16 or 256 entries; none at 16 bits). */
template <typename F> static void use_vram(const Use& u, F run)
{
    const uint16_t* vram = pc_native_vram();
    if (!vram)
        return;
    int tx = (int)(u.page & 15) * 64, ty = (int)((u.page >> 4) & 1) * 256, depth = (int)((u.page >> 7) & 3);
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int x0 = tx + (u.box[0] >> shift), x1 = tx + (u.box[2] >> shift);
    for (int v = u.box[1]; v <= u.box[3]; v++) {
        const uint16_t* row = vram + (size_t)((ty + v) & 511) * 1024;
        for (int x = x0; x <= x1;) { /* (a run stops at VRAM's right edge: it wraps) */
            int at = x & 1023, n = std::min(x1 - x + 1, 1024 - at);
            run(row + at, n);
            x += n;
        }
    }
    if (depth < 2) {
        int cx = (int)(u.clut & 63) * 16, cy = (int)((u.clut >> 6) & 511), n = depth == 0 ? 16 : 256;
        const uint16_t* row = vram + (size_t)cy * 1024;
        int first = std::min(n, 1024 - cx);
        run(row + cx, first);
        if (first < n)
            run(row, n - first);
    }
}
static void remember_vram(Use& u)
{
    u.seen.clear();
    use_vram(u, [&](const uint16_t* p, int n) { u.seen.insert(u.seen.end(), p, p + n); });
}
static bool vram_unchanged(const Use& u)
{
    if (u.seen.empty())
        return false;
    size_t at = 0;
    bool same = true;
    use_vram(u, [&](const uint16_t* p, int n) {
        if (same && (at + (size_t)n > u.seen.size() || memcmp(p, u.seen.data() + at, (size_t)n * 2)))
            same = false;
        at += (size_t)n;
    });
    return same && at == u.seen.size();
}
static void match_use(Use& u)
{
    u.state = 1;
    u.entry = u.item = -1;
    int w1 = u.box[2] - u.box[0], h1 = u.box[3] - u.box[1];
    if (!sizes.count(w1 << 8 | h1)) {
        if (!u.key.empty())
            materials_changed = true;
        u.key.clear();
        u.seen.clear();
        return;
    }
    std::string was = u.key;
    u.key = content_key(u.page, u.clut, u.box[0], u.box[1], u.box[2], u.box[3]);
    if (u.key != was && materials::catalog_has_size(w1 + 1, h1 + 1))
        materials_changed = true; /* (the material planes: write_materials) */
    remember_vram(u);
    pc_cost_count(PC_COUNT_HASHED, (unsigned)((w1 + 1) * (h1 + 1)));
    const Catalog& c = catalog();
    std::string ck;
    for (size_t i = 0; i < c.entries.size(); i++) {
        const Entry& e = c.entries[i];
        if (!e.enabled)
            continue;
        for (size_t k = 0; k < e.match.size(); k++) {
            const Match& m = e.match[k];
            if (m.key != u.key)
                continue;
            if (!m.clut.empty()) {
                if (ck.empty())
                    ck = clut_key(u.page, u.clut);
                if (ck != m.clut)
                    continue;
            }
            u.entry = (int)i, u.item = (int)k; /* the last matching entry wins */
        }
    }
}

/* The material planes: every use whose texture a material entry names (by
 * content key, size, palette and room), its box's cells written with the
 * entry's fractions (x 255) and, for roughness, its painted map's values
 * when the map is the box's size. Unnamed cells: roughness 255, specular and
 * metal 0, which the draw shaders treat as no material. */
static void write_materials()
{
    material_planes.assign((size_t)PC_MATERIAL_MAP_H * PC_EMISSIVE_MAP_W, 0);
    memset(material_planes.data(), 255, (size_t)512 * PC_EMISSIVE_MAP_W);
    material_uses = material_cells = 0;
    material_list.clear();
    any_material = false;
    applied_material_maps = materials::generation();
    current_room(&material_zone, &material_room);
    material_on = pc_specular != 0;
    if (!material_on)
        return;
    std::vector<std::pair<const Use*, const materials::Entry*>> order;
    const std::vector<materials::Entry> entries = materials::catalog_entries();
    for (auto& kv : uses) {
        const Use& u = kv.second;
        if (u.key.empty())
            continue;
        int w = u.box[2] - u.box[0] + 1, h = u.box[3] - u.box[1] + 1;
        if (!materials::catalog_has_size(w, h))
            continue;
        const materials::Entry* e = materials::find_match(u.key, w, h, "", material_zone, material_room);
        bool needs_clut = false;
        for (const materials::Entry& x : entries)
            needs_clut |= x.key == u.key && !x.clut.empty();
        if (needs_clut)
            e = materials::find_match(u.key, w, h, clut_key(u.page, u.clut), material_zone, material_room);
        if (e)
            order.push_back({ &u, e });
    }
    /* A stable order (two boxes over the same texels: the same result every time). */
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
        const Use *x = a.first, *y = b.first;
        return std::make_tuple(x->page, x->clut, x->box[0], x->box[1], x->box[2], x->box[3])
            < std::make_tuple(y->page, y->clut, y->box[0], y->box[1], y->box[2], y->box[3]);
    });
    uint8_t* rough = material_planes.data();
    uint8_t* spec = rough + (size_t)512 * PC_EMISSIVE_MAP_W;
    uint8_t* metal = spec + (size_t)512 * PC_EMISSIVE_MAP_W;
    for (const auto& item : order) {
        const Use* u = item.first;
        const materials::Surface& v = item.second->value;
        int w = u->box[2] - u->box[0] + 1, h = u->box[3] - u->box[1] + 1;
        const materials::Channel* map = materials::channel(u->key, "roughness");
        if (map && (map->w != w || map->h != h))
            map = nullptr; /* never stretched (pc/docs/Materials.md) */
        uint8_t s = (uint8_t)std::clamp((int)lroundf(v.specular * 255.0f), 0, 255);
        uint8_t m = (uint8_t)std::clamp((int)lroundf(v.metal * 255.0f), 0, 255);
        uint8_t r = (uint8_t)std::clamp((int)lroundf(v.roughness * 255.0f), 0, 255);
        if (!s && !m)
            continue;
        ++material_uses;
        MaterialUse info;
        info.key = u->key, info.entry = item.second->id, info.page = u->page, info.clut = u->clut;
        memcpy(info.box, u->box, 4);
        info.painted = map != nullptr;
        material_list.push_back(info);
        int tx = (int)(u->page & 15) * 64, ty = (int)((u->page >> 4) & 1) * 256;
        int depth = std::min((int)((u->page >> 7) & 3), 2);
        for (int y = u->box[1]; y <= u->box[3]; y++)
            for (int x = u->box[0]; x <= u->box[2]; x++) {
                size_t at = (size_t)(ty + y) * PC_EMISSIVE_MAP_W + ((tx * 4 + (x << depth)) & 4095);
                material_cells += spec[at] == 0 && metal[at] == 0;
                rough[at] = map ? map->values[(size_t)(y - u->box[1]) * w + (x - u->box[0])] : r;
                spec[at] = s, metal[at] = m;
            }
    }
    any_material = material_uses > 0;
}

uint32_t map_serial() { return map_generation; }
const uint8_t* map_data() { return the_map.data(); }
std::vector<MaterialUse> material_use_list() { return material_list; }
int material_cell_count() { return material_cells; }
bool material_at(unsigned u, unsigned v, unsigned tpage, int out[3])
{
    if (material_planes.empty())
        return false;
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
    int depth = std::min((int)((tpage >> 7) & 3), 2);
    size_t at = (size_t)(ty + (int)(v & 255)) * PC_EMISSIVE_MAP_W + ((tx * 4 + (int)((u & 255) << depth)) & 4095);
    size_t plane = (size_t)512 * PC_EMISSIVE_MAP_W;
    out[0] = material_planes[at], out[1] = material_planes[plane + at], out[2] = material_planes[2 * plane + at];
    return out[1] || out[2];
}

} // namespace emissive

using namespace emissive;

extern "C" void pc_emissive_note_instance(uint32_t mesh, const void* polygons, int count, int override,
    unsigned tpage, unsigned clut)
{
    if (!mesh || !polygons || count <= 0)
        return;
    if (!loaded)
        reload();
    int texture = (override & RENDER_OVERRIDE_TEXTURE) != 0;
    uint64_t ik = (uint64_t)mesh << 32 | (texture ? ((uint64_t)(tpage & 0xffff) << 16 | (clut & 0xffff)) : 0xffffffffu);
    if (instance_uses.count(ik))
        return;
    std::vector<uint64_t>& list = instance_uses[ik];
    mesh_instances[mesh].push_back(ik);
    scope_pending.insert(mesh);
    const render_polygon* p = (const render_polygon*)polygons;
    std::unordered_set<uint64_t> seen;
    for (int i = 0; i < count; i++) {
        if (!(p[i].code & 4))
            continue;
        unsigned pt = p[i].tpage | (texture ? tpage : 0);
        unsigned pc = texture ? (clut + p[i].clut) & 0xffff : p[i].clut;
        unsigned page = pt & 0x19f;
        int n = p[i].flags & RENDER_POLY_QUAD ? 4 : 3;
        uint8_t box[4] = { 255, 255, 0, 0 };
        for (int v = 0; v < n; v++) {
            box[0] = std::min(box[0], p[i].uv[v][0]), box[1] = std::min(box[1], p[i].uv[v][1]);
            box[2] = std::max(box[2], p[i].uv[v][0]), box[3] = std::max(box[3], p[i].uv[v][1]);
        }
        uint64_t k = use_key(page, pc, box);
        if (!seen.insert(k).second)
            continue;
        list.push_back(k);
        auto it = uses.find(k);
        if (it == uses.end()) {
            Use u;
            u.page = (uint16_t)page, u.clut = (uint16_t)pc;
            memcpy(u.box, box, 4);
            u.tiles = tiles_of(page, box);
            it = uses.emplace(k, u).first;
        }
        it->second.refs++;
    }
}
extern "C" void pc_emissive_mesh_gone(uint32_t mesh)
{
    mesh_info.erase(mesh);
    scope_pending.erase(mesh);
    auto mi = mesh_instances.find(mesh);
    if (mi == mesh_instances.end())
        return;
    for (uint64_t ik : mi->second) {
        auto ii = instance_uses.find(ik);
        if (ii == instance_uses.end())
            continue;
        for (uint64_t k : ii->second) {
            auto it = uses.find(k);
            if (it == uses.end() || --it->second.refs > 0)
                continue;
            if (it->second.entry >= 0)
                marks_changed = true;
            if (!it->second.key.empty())
                materials_changed = true;
            uses.erase(it);
        }
        instance_uses.erase(ii);
    }
    mesh_instances.erase(mi);
}
extern "C" void pc_emissive_vram_dirty(uint32_t tiles) { dirty_tiles |= tiles, light_dirty_tiles |= tiles; }
extern "C" void pc_emissive_room_mesh(uint32_t mesh, unsigned offset, unsigned tris, unsigned quads)
{
    if (!mesh)
        return;
    MeshInfo& m = mesh_info[mesh];
    m.kind = SURFACE_ROOM;
    m.offset = offset, m.tris = tris, m.total = tris + quads;
    mesh_uncaptured.erase(mesh);
    m.applied.clear();
    scope_pending.insert(mesh);
}
extern "C" void pc_emissive_model_mesh(uint32_t mesh, uint32_t model)
{
    if (!mesh)
        return;
    MeshInfo& m = mesh_info[mesh];
    m.kind = SURFACE_MODEL;
    m.model = model;
    m.applied.clear();
    scope_pending.insert(mesh);
}

/* The scope bits of a mesh's polygons (empty: none set). */
static void scope_bits(uint32_t mesh, const MeshInfo& info, std::vector<uint16_t>& out)
{
    out.clear();
    const Catalog& c = catalog();
    int n = render_mesh_polygons(mesh);
    if (n <= 0)
        return;
    for (size_t i = 0; i < c.entries.size(); i++) {
        if (!slot_scope_bit[i + 1])
            continue;
        uint16_t bit = (uint16_t)(1u << (slot_scope_bit[i + 1] - 1));
        const Entry& e = c.entries[i];
        if (info.kind == SURFACE_MODEL
            && std::find(e.scope.models.begin(), e.scope.models.end(), info.model) != e.scope.models.end()) {
            out.resize((size_t)n, 0);
            for (uint16_t& b : out)
                b |= bit;
        }
        if (info.kind != SURFACE_ROOM)
            continue;
        for (const Surface& f : e.scope.surfaces) {
            if (f.zone != scope_zone || f.room != scope_room || f.polygon < info.offset + 8)
                continue;
            unsigned at = f.polygon - (info.offset + 8), index, quads = info.total - info.tris;
            if (at < info.tris * 32 && !(at % 32))
                index = at / 32;
            else if (at >= info.tris * 32 && !((at - info.tris * 32) % 40) && (at - info.tris * 32) / 40 < quads)
                index = info.tris + (at - info.tris * 32) / 40;
            else
                continue;
            if ((int)index >= n)
                continue;
            out.resize((size_t)n, 0);
            out[index] |= bit;
        }
    }
}
/* Writes the scope bits of every known mesh (all) or of those noted since. */
static void apply_scopes(bool all)
{
    std::vector<uint16_t> bits;
    auto apply = [&](uint32_t mesh) {
        auto found = mesh_info.find(mesh);
        if (found == mesh_info.end())
            return;
        scope_bits(mesh, found->second, bits);
        if (bits != found->second.applied && render_mesh_emissive_scope(mesh, bits.empty() ? nullptr : bits.data()))
            found->second.applied = bits;
    };
    if (all)
        for (auto& kv : mesh_info)
            apply(kv.first);
    else
        for (uint32_t mesh : scope_pending)
            apply(mesh);
    scope_pending.clear();
}

extern "C" int pc_emissive_sync(int* row0, int* row1)
{
    ensure_loaded();
    float strength = std::clamp(pc_emissive, 0.0f, 4.0f);
    /* Uses are noted and matched whatever the setting (cheap: only textures
     * of a catalogued size are hashed), so switching it on shows the marks at
     * once, even in a paused game; with 0 nothing is written into the map. */
    uint32_t material_catalog = materials::catalog_generation();
    if (applied_generation != catalog_generation || applied_material_catalog != material_catalog) {
        applied_generation = catalog_generation;
        applied_material_catalog = material_catalog;
        materials_changed = true;
        sizes.clear();
        for (const Entry& e : catalog().entries)
            for (const Match& m : e.match)
                if (m.w > 0 && m.h > 0)
                    sizes.insert((m.w - 1) << 8 | (m.h - 1));
        for (const materials::Entry& e : materials::catalog_entries())
            sizes.insert((e.w - 1) << 8 | (e.h - 1));
        for (auto& kv : uses)
            kv.second.state = 0;
        marks_changed = params_changed = true;
    }
    if (strength != applied_strength)
        params_changed = true;
    if ((strength > 0) != marks_on)
        marks_changed = true;
    {
        int zone, room;
        current_room(&zone, &room);
        if (zone != scope_zone || room != scope_room)
            marks_changed = true; /* scoped entries follow the room */
    }
    if (materials::generation() != applied_materials)
        marks_changed = true; /* a painted map changed on disk */
    /* A use that did not match yet is tried again when textures are
     * (re)loaded under it, and every frame when it has a catalogued size
     * (VRAM animations change its content: the floating blocks' circle):
     * then only when the VRAM its keys read changed since (text and menus
     * drawn elsewhere, or nothing at all, cost a comparison). */
    for (auto& kv : uses)
        if (kv.second.entry < 0 && kv.second.state == 1
            && (!kv.second.key.empty() ? !vram_unchanged(kv.second) : (kv.second.tiles & dirty_tiles) != 0))
            kv.second.state = 0;
    dirty_tiles = 0;
    long budget = 1 << 21; /* texels hashed per frame */
    for (auto& kv : uses) {
        Use& u = kv.second;
        if (u.state)
            continue;
        if (budget <= 0)
            break;
        int was = u.entry;
        match_use(u);
        budget -= (long)(u.box[2] - u.box[0] + 1) * (u.box[3] - u.box[1] + 1);
        if (u.entry != was)
            marks_changed = true;
    }
    if (marks_changed) {
        marks_on = strength > 0;
        write_marks();
        dirty_row0 = std::min(dirty_row0, 0), dirty_row1 = std::max(dirty_row1, PC_EMISSIVE_MAP_H);
        ++map_generation;
        params_changed = true; /* the slots' scope bits */
        apply_scopes(true);
    } else if (!scope_pending.empty())
        apply_scopes(false);
    if (params_changed || strength != applied_strength) {
        write_params(strength);
        dirty_row0 = std::min(dirty_row0, 512), dirty_row1 = std::max(dirty_row1, PC_EMISSIVE_AMOUNT_ROW);
        if (applied_strength != strength)
            ++map_generation;
        applied_strength = strength;
    }
    {
        int zone, room;
        current_room(&zone, &room);
        if (zone != material_zone || room != material_room || (pc_specular != 0) != material_on
            || materials::generation() != applied_material_maps)
            materials_changed = true;
    }
    if (materials_changed) {
        write_materials();
        material_dirty = true;
        materials_changed = false;
    }
    marks_changed = params_changed = false;
    *row0 = dirty_row0, *row1 = dirty_row1;
    dirty_row0 = PC_EMISSIVE_MAP_H, dirty_row1 = 0;
    return any_marks && strength > 0;
}
extern "C" const uint8_t* pc_emissive_map(void) { return the_map.data(); }
extern "C" int pc_emissive_pending(void)
{
    float strength = std::clamp(pc_emissive, 0.0f, 4.0f);
    if (strength != applied_strength)
        return 1;
    static int calls;
    if (loaded && ++calls >= 30) {
        calls = 0;
        materials::poll(); /* maps painted outside (a paused game's redraws show them) */
    }
    if (loaded && materials::generation() != applied_materials)
        return 1;
    if (loaded && ((pc_specular != 0) != material_on || materials::generation() != applied_material_maps))
        return 1;
    /* (the material catalog: two stat calls; edits in the editor bump it at once) */
    if (loaded && materials::catalog_generation() != applied_material_catalog)
        return 1;
    return loaded && (applied_generation != catalog_generation || (strength > 0) != marks_on);
}
extern "C" int pc_emissive_active(void) { return any_marks && pc_emissive > 0; }
extern "C" int pc_material_rows(const uint8_t** planes)
{
    if (material_planes.empty())
        write_materials();
    *planes = material_planes.data();
    bool dirty = material_dirty;
    material_dirty = false;
    return dirty;
}
extern "C" int pc_material_active(void) { return any_material && pc_specular != 0; }

namespace emissive {
/* The slot of a texel position, 0 none. */
int slot_at(unsigned u, unsigned v, unsigned tpage)
{
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
    int depth = std::min((int)((tpage >> 7) & 3), 2);
    return the_map[(size_t)(ty + (int)(v & 255)) * PC_EMISSIVE_MAP_W + ((tx * 4 + (int)((u & 255) << depth)) & 4095)];
}
int amount_at(unsigned u, unsigned v, unsigned tpage)
{
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
    int depth = std::min((int)((tpage >> 7) & 3), 2);
    return the_map[(size_t)(PC_EMISSIVE_AMOUNT_ROW + ty + (int)(v & 255)) * PC_EMISSIVE_MAP_W
        + ((tx * 4 + (int)((u & 255) << depth)) & 4095)];
}
std::vector<MapProblem> map_problems() { return problems; }
} // namespace emissive

/* ---------------------------------------------------------------- lights */

/* A light entry's glow is its triangles' emission, integrated over their
 * texture area: every texel whose centre lies inside the triangle's UV
 * triangle is looked at (its slot, the entry's scope, the per-texel
 * filters), so a mask narrowed by its filters (or a few painted texels) to a
 * small part of a large polygon still lights. The glow's colour is the
 * glowing texels' mean emission times the light weight times their share of
 * the triangle, counted as at least LIGHT_MIN_SHARE (a narrow slit of light
 * is still a light; the weight scales it further). Sampling a fixed grid of
 * 15 points per triangle, as this did before, found no glowing texel at all
 * under a narrow filter: no light. */

namespace emissive {
namespace {
/* Texels (u, v) of a UV triangle: those whose centres lie inside; a thin
 * triangle (no texel centre inside) its 15-point barycentric grid. */
template <typename F> void for_texels(const float uv[6], F&& visit)
{
    float u0 = uv[0], v0 = uv[1], u1 = uv[2], v1 = uv[3], u2 = uv[4], v2 = uv[5];
    float area = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0);
    int seen = 0;
    if (fabsf(area) >= 0.5f) {
        int x0 = std::max(0, (int)floorf(std::min({ u0, u1, u2 })));
        int x1 = std::min(255, (int)ceilf(std::max({ u0, u1, u2 })));
        int y0 = std::max(0, (int)floorf(std::min({ v0, v1, v2 })));
        int y1 = std::min(255, (int)ceilf(std::max({ v0, v1, v2 })));
        float inv = 1.0f / area;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = (float)x + 0.5f, py = (float)y + 0.5f;
                float b0 = ((u1 - px) * (v2 - py) - (u2 - px) * (v1 - py)) * inv;
                float b1 = ((u2 - px) * (v0 - py) - (u0 - px) * (v2 - py)) * inv;
                if (b0 >= -1e-4f && b1 >= -1e-4f && 1.0f - b0 - b1 >= -1e-4f) {
                    visit(x, y);
                    ++seen;
                }
            }
    }
    if (seen)
        return;
    static const unsigned char grid[15][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 1, 0 }, { 1, 1 },
        { 1, 2 }, { 1, 3 }, { 2, 0 }, { 2, 1 }, { 2, 2 }, { 3, 0 }, { 3, 1 }, { 4, 0 } };
    for (const auto& g : grid) {
        float w0 = g[0] / 4.0f, w1 = g[1] / 4.0f, w2 = 1.0f - w0 - w1;
        visit((int)(w0 * u0 + w1 * u1 + w2 * u2) & 255, (int)(w0 * v0 + w1 * v1 + w2 * v2) & 255);
    }
}
/* VRAM tiles a triangle's texels and CLUT lie in (pc_emissive_vram_dirty's). */
uint32_t triangle_tiles(const float uv[6], unsigned tpage, unsigned clut)
{
    int box_i[4] = { 255, 255, 0, 0 };
    for (int k = 0; k < 3; k++) {
        int u = std::clamp((int)uv[k * 2], 0, 255), v = std::clamp((int)uv[k * 2 + 1], 0, 255);
        box_i[0] = std::min(box_i[0], u), box_i[1] = std::min(box_i[1], v);
        box_i[2] = std::max(box_i[2], std::min(255, u + 1)), box_i[3] = std::max(box_i[3], std::min(255, v + 1));
    }
    uint8_t box[4] = { (uint8_t)box_i[0], (uint8_t)box_i[1], (uint8_t)box_i[2], (uint8_t)box_i[3] };
    uint32_t tiles = tiles_of(tpage & 0x19f, box);
    int cx = (int)(clut & 63) * 16, cy = (int)((clut >> 6) & 511), width = ((tpage >> 7) & 3) == 1 ? 256 : 16;
    for (int x = cx & ~63; x < cx + width && x < 1024; x += 64)
        tiles |= 1u << (((cy >> 8) & 1) * 16 + (x >> 6));
    return tiles;
}
} // namespace

/* The emission of texel (u, v) under slot s (its entry's filters; the scope
 * not tested): 1 when it glows. */
int emission_at(unsigned u, unsigned v, unsigned tpage, unsigned clut, int s, float rgb[3])
{
    const Catalog& c = catalog();
    if (s <= 0 || s > (int)c.entries.size())
        return 0;
    const Entry& e = c.entries[(size_t)s - 1];
    unsigned index = 0;
    uint16_t t = texel(u, v, tpage, clut, &index);
    if (!t || !e.enabled || !filters_pass(e.mask, index, t, (tpage >> 7) & 3))
        return 0;
    /* As the shader: the strength byte (the setting folded in). */
    const uint8_t* p = &the_map[(size_t)512 * PC_EMISSIVE_MAP_W + (size_t)s * 64];
    float k = p[3] / 63.75f * amount_at(u, v, tpage) / 255.0f;
    if (k <= 0)
        return 0;
    rgb[0] = (t & 31) / 31.0f * p[0] / 255.0f * k;
    rgb[1] = ((t >> 5) & 31) / 31.0f * p[1] / 255.0f * k;
    rgb[2] = ((t >> 10) & 31) / 31.0f * p[2] / 255.0f * k;
    return 1;
}
/* A triangle's light texels (entries with light > 0 that `allowed`), with
 * their emission when `emission`. */
template <typename A>
static TriangleLight integrate(const float uv[6], unsigned tpage, unsigned clut, bool emission, A&& allowed)
{
    TriangleLight out;
    const Catalog& c = catalog();
    if (!any_marks || !(light_pages & (1u << (tpage & 31))))
        return out;
    for_texels(uv, [&](int x, int y) {
        ++out.texels;
        int s = slot_at((unsigned)x, (unsigned)y, tpage);
        if (!s || s > (int)c.entries.size())
            return;
        const Entry& e = c.entries[(size_t)s - 1];
        if (e.light <= 0 || !e.enabled || !allowed(e))
            return;
        out.candidate = true;
        out.reach = std::max(out.reach, e.light_reach);
        out.lift = std::max(out.lift, e.light_lift);
        float rgb[3];
        if (emission && emission_at((unsigned)x, (unsigned)y, tpage, clut, s, rgb)) {
            for (int k = 0; k < 3; k++)
                out.sum[k] += rgb[k] * e.light;
            out.amount += amount_at((unsigned)x, (unsigned)y, tpage) / 255.0f; /* a painted map's share */
        }
    });
    return out;
}
void light_rgb(const TriangleLight& t, float rgb[3])
{
    if (t.amount <= 0 || t.texels <= 0) {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
    }
    float share = std::min(std::max(t.amount / (float)t.texels, LIGHT_MIN_SHARE), 1.0f);
    for (int k = 0; k < 3; k++)
        rgb[k] = t.sum[k] / t.amount * share;
}
} // namespace emissive

extern "C" int pc_emissive_texel(unsigned u, unsigned v, unsigned tpage, unsigned clut, float rgb[3], float* light)
{
    if (!any_marks)
        return 0;
    int s = slot_at(u, v, tpage);
    float e[3];
    if (!emission_at(u, v, tpage, clut, s, e))
        return 0;
    if (rgb)
        memcpy(rgb, e, sizeof e);
    if (light)
        *light = catalog().entries[(size_t)s - 1].light;
    return 1;
}

extern "C" int pc_emissive_key_texel(const char* key, float u, float v, float rgb[3])
{
    if (rgb) rgb[0] = rgb[1] = rgb[2] = 0;
    if (!key || !*key || !std::isfinite(u) || !std::isfinite(v) || !any_marks)
        return 0;
    /* Uses have already been matched by the frame's emissive sync. Avoid
     * allocating a use_list for every sample of every rect-light cell. */
    for (const auto& item : uses) {
        const Use& use = item.second;
        if (use.refs <= 0 || use.key != key) continue;
        int w = use.box[2] - use.box[0] + 1, h = use.box[3] - use.box[1] + 1;
        unsigned x = use.box[0] + std::min(w - 1, (int)(std::clamp(u, 0.0f, 1.0f) * w));
        unsigned y = use.box[1] + std::min(h - 1, (int)(std::clamp(v, 0.0f, 1.0f) * h));
        return pc_emissive_texel(x, y, use.page, use.clut, rgb, nullptr);
    }
    return 0;
}

namespace {
/* Object triangles' emission by texture area and scope (characters, props,
 * other room blocks: no stable index), cleared when the map or room changes,
 * an area when VRAM under it does. */
struct ObjectKey {
    uint64_t a, b;
    bool operator==(const ObjectKey& o) const { return a == o.a && b == o.b; }
};
struct ObjectKeyHash {
    size_t operator()(const ObjectKey& k) const { return (size_t)(k.a * 0x9e3779b97f4a7c15ull ^ k.b); }
};
struct ObjectLight {
    uint32_t tiles;
    float rgb[3];
    bool lit;
    float reach, lift; /* its entries' light_reach, light_lift */
};
std::unordered_map<ObjectKey, ObjectLight, ObjectKeyHash> object_cache;
uint32_t object_cache_map;
int object_cache_zone = -1, object_cache_room = -1;

/* ---- room triangles (the captured scene) */
struct RoomLight {
    int index; /* scene triangle */
    uint32_t tiles;
    bool fresh; /* rgb is current */
    float rgb[3];
    int texels; /* its texels, and the glowing share's amount */
    float amount;
    float reach, lift; /* its entries' light_reach, light_lift */
};
int lights_scene = -1, lights_uv = -1;
uint32_t lights_map = 0, lights_serial = 1;
std::vector<RoomLight> room_lights; /* sorted by index */
std::vector<int> light_at; /* scene triangle -> room_lights entry, -1 */

void scene_uv(const scene_triangle* t, float uv[6])
{
    for (int j = 0; j < 3; j++)
        uv[j * 2] = t->vertex[j].u, uv[j * 2 + 1] = t->vertex[j].v;
}
bool room_allows(const scene_triangle* t, const Entry& e)
{
    return scope_allows(e, scope_zone, scope_room, SURFACE_ROOM, t->polygon, 0);
}
void refresh_lights()
{
    int scene = pc_scene_serial(), uvs = pc_scene_uv_serial();
    uint32_t map = map_generation;
    /* Emission again where the texels changed: VRAM under them (palette
     * and VRAM animations) or their UVs (room texture effects). */
    if (light_dirty_tiles) {
        for (RoomLight& l : room_lights)
            if (l.tiles & light_dirty_tiles)
                l.fresh = false;
        for (auto it = object_cache.begin(); it != object_cache.end();)
            it = it->second.tiles & light_dirty_tiles ? object_cache.erase(it) : std::next(it);
        light_dirty_tiles = 0;
    }
    if (uvs != lights_uv) {
        lights_uv = uvs;
        for (RoomLight& l : room_lights)
            l.fresh = false;
    }
    if (scene == lights_scene && map == lights_map)
        return;
    lights_scene = scene, lights_map = map;
    int count = 0;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    std::vector<RoomLight> found;
    light_at.assign((size_t)(count > 0 ? count : 0), -1);
    for (int i = 0; any_marks && light_pages && i < count; i++) {
        const scene_triangle* t = &tris[i];
        unsigned mode = (t->tpage >> 5) & 3;
        if ((t->code & 2) && (mode == 1 || mode == 3))
            continue; /* additive: a glow already */
        float uv[6];
        scene_uv(t, uv);
        TriangleLight tl = integrate(uv, t->tpage, t->clut, false, [&](const Entry& e) { return room_allows(t, e); });
        if (!tl.candidate)
            continue;
        light_at[(size_t)i] = (int)found.size();
        found.push_back({ i, triangle_tiles(uv, t->tpage, t->clut), false, { 0, 0, 0 }, 0, 0, tl.reach, tl.lift });
    }
    /* Another set of light triangles, or another lift (glows.c puts its
     * groups' centres in front of them): the groups again. */
    bool same = found.size() == room_lights.size();
    for (size_t k = 0; same && k < found.size(); k++)
        same = found[k].index == room_lights[k].index && found[k].lift == room_lights[k].lift;
    room_lights.swap(found);
    if (!same)
        ++lights_serial;
}
} // namespace

extern "C" uint32_t pc_emissive_light_serial(void)
{
    refresh_lights();
    return room_lights.empty() ? 0 : lights_serial;
}
extern "C" int pc_emissive_room_light_shape(int index, float* reach, float* lift)
{
    refresh_lights();
    if (index < 0 || index >= (int)light_at.size() || light_at[(size_t)index] < 0)
        return 0;
    const RoomLight& l = room_lights[(size_t)light_at[(size_t)index]];
    if (reach)
        *reach = l.reach;
    if (lift)
        *lift = l.lift;
    return 1;
}
extern "C" int pc_emissive_room_light(int index, float rgb[3])
{
    refresh_lights();
    if (index < 0 || index >= (int)light_at.size() || light_at[(size_t)index] < 0)
        return 0;
    if (!rgb)
        return 1;
    RoomLight& l = room_lights[(size_t)light_at[(size_t)index]];
    if (!l.fresh) {
        int count = 0;
        const scene_triangle* t = &pc_scene_triangle_data(&count)[index];
        float uv[6];
        scene_uv(t, uv);
        TriangleLight tl = integrate(uv, t->tpage, t->clut, true, [&](const Entry& e) { return room_allows(t, e); });
        light_rgb(tl, l.rgb);
        l.texels = tl.texels, l.amount = tl.amount;
        l.fresh = true;
    }
    memcpy(rgb, l.rgb, sizeof l.rgb);
    return 1;
}
namespace emissive {
int light_triangle_count()
{
    refresh_lights();
    return (int)room_lights.size();
}
std::vector<LightInfo> light_list()
{
    refresh_lights();
    std::vector<LightInfo> out;
    int count = 0;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    for (const RoomLight& l : room_lights) {
        LightInfo i;
        pc_emissive_room_light(l.index, i.rgb);
        const RoomLight& now = room_lights[(size_t)light_at[(size_t)l.index]];
        i.index = l.index;
        i.polygon = l.index < count ? tris[l.index].polygon : 0;
        i.texels = now.texels, i.amount = now.amount;
        out.push_back(i);
    }
    return out;
}
} // namespace emissive

/* ---------------------------------------------------------------- object lights */

namespace {
struct ObjectSink {
    pc_glow_fragment* out;
    int n, max;
    bool room_list; /* the room's own list: only what the captured scene lacks */
};
/* The captured room's triangles by polygon (their centres): a triangle of the
 * room's own list is a scene triangle (the room lights' business) when one
 * of its polygon's captured triangles lies where it is drawn. Per capture. */
std::unordered_multimap<uint32_t, std::array<float, 3>> captured_at;
int captured_scene = -1;
void refresh_captured()
{
    if (captured_scene == pc_scene_serial())
        return;
    captured_scene = pc_scene_serial();
    captured_at.clear();
    mesh_uncaptured.clear();
    int count = 0;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    for (int i = 0; i < count; i++) {
        std::array<float, 3> c;
        for (int k = 0; k < 3; k++)
            c[k] = (tris[i].vertex[0].position[k] + tris[i].vertex[1].position[k] + tris[i].vertex[2].position[k])
                / 3.0f;
        captured_at.emplace(tris[i].polygon, c);
    }
}
int room_mesh_uncaptured(void* /*context*/, uint32_t mesh)
{
    auto found = mesh_uncaptured.find(mesh);
    if (found != mesh_uncaptured.end())
        return found->second;
    bool lacks = false;
    auto it = mesh_info.find(mesh);
    if (it != mesh_info.end() && it->second.kind == SURFACE_ROOM)
        for (unsigned i = 0; i < it->second.total && !lacks; i++) {
            uint32_t polygon, model;
            mesh_surface(mesh, (int)i, &polygon, &model);
            lacks = captured_at.find(polygon) == captured_at.end();
        }
    mesh_uncaptured.emplace(mesh, lacks);
    return lacks;
}
bool captured(uint32_t polygon, const float w[9])
{
    float c[3];
    for (int k = 0; k < 3; k++)
        c[k] = (w[k] + w[3 + k] + w[6 + k]) / 3.0f;
    auto range = captured_at.equal_range(polygon);
    for (auto it = range.first; it != range.second; ++it)
        if (fabsf(it->second[0] - c[0]) < 1.0f && fabsf(it->second[1] - c[1]) < 1.0f
            && fabsf(it->second[2] - c[2]) < 1.0f)
            return true;
    return false;
}
void object_sink(void* context, const float w[9], const uint8_t* /*rgb*/, const uint8_t uv[6], unsigned code,
    unsigned tpage, unsigned clut)
{
    ObjectSink* s = (ObjectSink*)context;
    if (s->n >= s->max || !(code & 4) || !(light_pages & (1u << (tpage & 31))))
        return;
    uint32_t source, mesh, polygon, model;
    render_emit_tag(&source, &mesh);
    int kind = mesh_surface(mesh, render_emit_polygon_index(), &polygon, &model);
    /* The room's own groups the scene capture left out (disabled when the
     * room loaded: the floating blocks of Rue Vermillion, shown later by a
     * script, moving with their own transforms) light as objects; the rest
     * are scene triangles, the room light triangles' (pc_emissive_room_light). */
    if (s->room_list && (kind != SURFACE_ROOM || captured(polygon, w)))
        return;
    ObjectKey key = { (uint64_t)tpage | (uint64_t)clut << 16 | (uint64_t)uv[0] << 32 | (uint64_t)uv[1] << 40
            | (uint64_t)uv[2] << 48 | (uint64_t)uv[3] << 56,
        (uint64_t)uv[4] | (uint64_t)uv[5] << 8 | (uint64_t)kind << 16
            | (uint64_t)(kind == SURFACE_ROOM ? polygon : model) << 24 }; /* a room polygon: its own scope */
    auto found = object_cache.find(key);
    if (found == object_cache.end()) {
        float fuv[6];
        for (int k = 0; k < 6; k++)
            fuv[k] = uv[k];
        TriangleLight tl = integrate(fuv, tpage, clut, true, [&](const Entry& e) {
            return scope_allows(e, scope_zone, scope_room, kind, polygon, model);
        });
        ObjectLight l;
        l.tiles = triangle_tiles(fuv, tpage, clut);
        l.lit = tl.amount > 0;
        l.reach = tl.reach, l.lift = tl.lift;
        light_rgb(tl, l.rgb);
        found = object_cache.emplace(key, l).first;
    }
    if (!found->second.lit)
        return;
    float e1[3], e2[3], c[3];
    for (int k = 0; k < 3; k++)
        e1[k] = w[3 + k] - w[k], e2[k] = w[6 + k] - w[k];
    c[0] = e1[1] * e2[2] - e1[2] * e2[1], c[1] = e1[2] * e2[0] - e1[0] * e2[2], c[2] = e1[0] * e2[1] - e1[1] * e2[0];
    float len = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    const render_polygon* poly = render_emit_polygon();
    bool sided = len > 1e-6f && !(poly && (poly->flags & RENDER_POLY_DOUBLE_SIDED));
    /* A face turned to the floor (a crate's bottom: its outward normal
     * -(e1 x e2) mostly down, +y) lights nothing but the floor under it,
     * and would take one of the 32 traced glows. */
    if (sided && -c[1] / len > 0.7f)
        return;
    pc_glow_fragment* f = &s->out[s->n++];
    for (int k = 0; k < 3; k++) {
        f->p[k] = (w[k] + w[3 + k] + w[6 + k]) / 3.0f;
        f->rgb[k] = found->second.rgb[k];
    }
    f->area = 0.5f * len;
    f->reach = found->second.reach;
    f->weight = f->area * (0.299f * f->rgb[0] + 0.587f * f->rgb[1] + 0.114f * f->rgb[2]);
    /* In front of its surface, one glow per side it faces. A glow is a
     * sphere at its triangles' mean position, and an object is solid (a
     * caster in the tracers' scene): the faces of a glowing crate averaged
     * to a point inside it, every shadow ray to it hit the crate and its
     * light was gone (and the baked light around it darkened: its shadow
     * rays were blocked too). The light now sits pc_glow_emissive_lift (or
     * the entry's light_lift) out of the face along its front (objects are single sided: the front is
     * where NCLIP > 0, render.h, det(p0, p1, p2) > 0 from the camera, so
     * the outward normal is -(e1 x e2)), and each of the six directions it
     * may face is its own source (the faces of a box no longer average back
     * inside it; glows.c makes it a disc facing that side). Double-sided
     * polygons have no front: on the surface, as before. Its tag marks it
     * an emissive light (PC_GLOW_EMISSIVE): the room's baked light never
     * came from it, so it casts no shadows of that light (the crates in
     * the middle of the room darkened the floor around them). */
    unsigned side = 0;
    if (sided) {
        float n[3] = { -c[0] / len, -c[1] / len, -c[2] / len };
        int axis = fabsf(n[0]) >= fabsf(n[1]) && fabsf(n[0]) >= fabsf(n[2]) ? 0 : fabsf(n[1]) >= fabsf(n[2]) ? 1 : 2;
        side = 1 + (unsigned)axis * 2 + (n[axis] < 0);
        float lift = found->second.lift > 0 ? found->second.lift : pc_glow_emissive_lift; /* the entry's, else the room's */
        for (int k = 0; k < 3; k++)
            f->p[k] += n[k] * lift;
    }
    f->source = (uint32_t)PC_GLOW_ORIGIN_OBJECT << 28 | PC_GLOW_EMISSIVE | side << PC_GLOW_SIDE_SHIFT
        | ((mesh * 0x9e3779b1u) & ((1u << PC_GLOW_SIDE_SHIFT) - 1));
}
bool any_object_lights()
{
    const Catalog& c = catalog();
    for (const auto& kv : uses)
        if (kv.second.entry >= 0 && kv.second.entry < (int)c.entries.size() && c.entries[kv.second.entry].light > 0)
            return true;
    return false;
}
} // namespace

/* Light texels on characters, props and other room blocks (the floating
 * blocks' magic circle): object glows (glows.c groups them by mesh, as the
 * additive object polygons gpu_hw.c reports). Returns the fragments written. */
extern "C" int pc_emissive_object_lights(void* fragments, int max)
{
    pc_glow_fragment* out = (pc_glow_fragment*)fragments;
    if (!any_marks || !pc_hw_present_all_lists || !any_object_lights())
        return 0;
    refresh_lights(); /* VRAM changes drop cached areas */
    if (object_cache_map != map_generation || object_cache_zone != scope_zone || object_cache_room != scope_room
        || object_cache.size() > 65536) {
        object_cache.clear();
        object_cache_map = map_generation, object_cache_zone = scope_zone, object_cache_room = scope_room;
    }
    uint32_t lists[64];
    int n = pc_hw_present_all_lists(lists, 64);
    ObjectSink s = { out, 0, max, false };
    for (int i = 0; i < n && s.n < max; i++) {
        /* objects (1) and other room blocks (2); of the room's own list (0)
         * only the groups the scene capture lacks (a full walk of it cost
         * ~18 us a frame, these a few) */
        int kind = render_list_objects(lists[i]);
        s.room_list = kind == 0;
        if (s.room_list) {
            refresh_captured();
            render_list_triangles_where(lists[i], room_mesh_uncaptured, object_sink, &s);
        } else if (kind > 0)
            render_list_triangles(lists[i], object_sink, &s);
    }
    return s.n;
}

/* ---------------------------------------------------------------- surfaces of an entry */

namespace emissive {
namespace {
struct UsesSearch {
    int slot;
    const Entry* e;
    std::vector<SurfaceUse>* out;
    std::unordered_map<uint64_t, size_t>* at;
};
void add_use(UsesSearch& s, int kind, uint32_t polygon, uint32_t model, uint32_t mesh, const float w[9],
    const float uv[6], unsigned tpage, unsigned clut)
{
    int texels = 0, covered = 0, glowing = 0;
    for_texels(uv, [&](int x, int y) {
        ++texels;
        if (slot_at((unsigned)x, (unsigned)y, tpage) != s.slot)
            return;
        ++covered;
        float rgb[3];
        glowing += emission_at((unsigned)x, (unsigned)y, tpage, clut, s.slot, rgb);
    });
    if (!covered)
        return;
    uint64_t key = kind == SURFACE_ROOM ? (uint64_t)polygon : (uint64_t)1 << 40 | mesh;
    auto found = s.at->find(key);
    if (found == s.at->end()) {
        SurfaceUse u;
        u.kind = kind, u.polygon = polygon, u.model = model, u.mesh = mesh;
        for (int k = 0; k < 3; k++)
            u.centre[k] = (w[k] + w[3 + k] + w[6 + k]) / 3.0f;
        u.allowed = scope_allows(*s.e, scope_zone, scope_room, kind, polygon, model);
        if (kind == SURFACE_ROOM)
            u.listed = std::find(s.e->scope.surfaces.begin(), s.e->scope.surfaces.end(),
                           Surface { scope_zone, scope_room, polygon })
                != s.e->scope.surfaces.end();
        else if (kind == SURFACE_MODEL)
            u.listed = std::find(s.e->scope.models.begin(), s.e->scope.models.end(), model) != s.e->scope.models.end();
        found = s.at->emplace(key, s.out->size()).first;
        s.out->push_back(u);
    }
    SurfaceUse& u = (*s.out)[found->second];
    u.texels += texels, u.covered += covered, u.glowing += glowing;
    if (u.triangles.size() < 64) {
        std::array<float, 9> t;
        memcpy(t.data(), w, sizeof(float) * 9);
        u.triangles.push_back(t);
    }
}
void uses_sink(void* context, const float w[9], const uint8_t* /*rgb*/, const uint8_t uv[6], unsigned code,
    unsigned tpage, unsigned clut)
{
    UsesSearch* s = (UsesSearch*)context;
    if (!(code & 4) || !(marked_pages & (1u << (tpage & 31))))
        return;
    uint32_t source, mesh, polygon, model;
    render_emit_tag(&source, &mesh);
    int kind = mesh_surface(mesh, render_emit_polygon_index(), &polygon, &model);
    float fuv[6];
    for (int k = 0; k < 6; k++)
        fuv[k] = uv[k];
    add_use(*s, kind, polygon, model, mesh, w, fuv, tpage, clut);
}
} // namespace

/* Every room polygon of the captured room and every object triangle drawn
 * now whose texels lie under the entry's marks (its slot). */
std::vector<SurfaceUse> surface_uses(const std::string& id)
{
    std::vector<SurfaceUse> out;
    int index = index_of(id);
    if (index < 0 || !any_marks)
        return out;
    std::unordered_map<uint64_t, size_t> at;
    UsesSearch s = { index + 1, &catalog().entries[(size_t)index], &out, &at };
    int count = 0;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    for (int i = 0; i < count; i++) {
        const scene_triangle* t = &tris[i];
        if (!(marked_pages & (1u << (t->tpage & 31))))
            continue;
        float uv[6], w[9];
        for (int j = 0; j < 3; j++) {
            uv[j * 2] = t->vertex[j].u, uv[j * 2 + 1] = t->vertex[j].v;
            memcpy(w + j * 3, t->vertex[j].position, sizeof(float) * 3);
        }
        add_use(s, SURFACE_ROOM, t->polygon, 0, 0, w, uv, t->tpage, t->clut);
    }
    uint32_t lists[64];
    int n = pc_hw_present_all_lists ? pc_hw_present_all_lists(lists, 64) : 0;
    for (int i = 0; i < n; i++)
        if (render_list_objects(lists[i]) > 0) /* the room's polygons are the scene's */
            render_list_triangles(lists[i], uses_sink, &s);
    return out;
}
} // namespace emissive
