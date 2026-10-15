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
#include <unordered_map>
#include <unordered_set>

#include "pc_paths.h"
#include "platform.h"
#include "render.h"
#include "scene_capture.h"
#include "glow_overrides.h"

extern "C" {
float pc_emissive = 1.0f;
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
    if (const Json* ms = j.get("match"); ms && ms->type == Json::Array)
        for (const Json& mj : ms->items) {
            if (mj.type != Json::Object)
                continue;
            Match m;
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
    Json j = Json::object();
    j.set("id", Json::str(e.id));
    if (!e.name.empty())
        j.set("name", Json::str(e.name));
    if (!e.where.empty())
        j.set("where", Json::str(e.where));
    Json ms = Json::array();
    for (const Match& m : e.match) {
        Json mj = Json::object();
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
    Json mask = Json::object();
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
    j.set("mask", mask);
    j.set("color", array_of({ tidy(e.color[0]), tidy(e.color[1]), tidy(e.color[2]) }));
    j.set("strength", Json::num(tidy(e.strength)));
    j.set("light", Json::num(tidy(e.light)));
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
void reload()
{
    std::string error;
    if (!load_file(catalog_path(), curated, error))
        fprintf(stderr, "[emissive] %s\n", error.c_str());
    std::string uerror;
    if (!load_file(user_path(), user, uerror))
        fprintf(stderr, "[emissive] %s\n", uerror.c_str());
    curated_stamp = stamp(catalog_path());
    user_stamp = stamp(user_path());
    merge();
    loaded = true;
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

namespace {
bool dirty_user;
}
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
        memcpy(s + 16, e.mask.indices, 32);
    }
}
static void write_marks()
{
    memset(the_map.data(), 0, (size_t)512 * PC_EMISSIVE_MAP_W);
    const Catalog& c = catalog();
    marked_uses = marked_cells = 0;
    any_marks = false;
    if (!marks_on)
        return; /* the setting is 0: nothing glows (matches are kept) */
    /* Entry order: a later entry wins where two cover the same texel. */
    std::vector<const Use*> order;
    for (auto& kv : uses)
        if (kv.second.entry >= 0)
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
        ++marked_uses;
        for (int v = u->box[1]; v <= u->box[3]; v++)
            for (int x = u->box[0]; x <= u->box[2]; x++) {
                if (!mask_covers(e.mask, x - u->box[0] + m.ox, v - u->box[1] + m.oy))
                    continue;
                uint8_t& cell = the_map[(size_t)(ty + v) * PC_EMISSIVE_MAP_W + ((tx * 4 + (x << depth)) & 4095)];
                marked_cells += cell == 0;
                cell = slot;
            }
    }
    any_marks = marked_uses > 0;
}
static void match_use(Use& u)
{
    u.state = 1;
    u.entry = u.item = -1;
    int w1 = u.box[2] - u.box[0], h1 = u.box[3] - u.box[1];
    if (!sizes.count(w1 << 8 | h1)) {
        u.key.clear();
        return;
    }
    u.key = content_key(u.page, u.clut, u.box[0], u.box[1], u.box[2], u.box[3]);
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

uint32_t map_serial() { return map_generation; }
const uint8_t* map_data() { return the_map.data(); }

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
            uses.erase(it);
        }
        instance_uses.erase(ii);
    }
    mesh_instances.erase(mi);
}
extern "C" void pc_emissive_vram_dirty(uint32_t tiles) { dirty_tiles |= tiles; }

extern "C" int pc_emissive_sync(int* row0, int* row1)
{
    ensure_loaded();
    float strength = std::clamp(pc_emissive, 0.0f, 4.0f);
    /* Uses are noted and matched whatever the setting (cheap: only textures
     * of a catalogued size are hashed), so switching it on shows the marks at
     * once, even in a paused game; with 0 nothing is written into the map. */
    if (applied_generation != catalog_generation) {
        applied_generation = catalog_generation;
        sizes.clear();
        for (const Entry& e : catalog().entries)
            for (const Match& m : e.match)
                if (m.w > 0 && m.h > 0)
                    sizes.insert((m.w - 1) << 8 | (m.h - 1));
        for (auto& kv : uses)
            kv.second.state = 0;
        marks_changed = params_changed = true;
    }
    if (strength != applied_strength)
        params_changed = true;
    if ((strength > 0) != marks_on)
        marks_changed = true;
    /* A use that did not match yet is tried again when textures are
     * (re)loaded under it, and every frame when it has a catalogued size
     * (VRAM animations change its content: the floating blocks' circle). */
    for (auto& kv : uses)
        if (kv.second.entry < 0 && kv.second.state == 1
            && (!kv.second.key.empty() || (kv.second.tiles & dirty_tiles)))
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
        dirty_row0 = std::min(dirty_row0, 0), dirty_row1 = std::max(dirty_row1, 512);
        ++map_generation;
    }
    if (params_changed || strength != applied_strength) {
        write_params(strength);
        dirty_row0 = std::min(dirty_row0, 512), dirty_row1 = std::max(dirty_row1, PC_EMISSIVE_MAP_H);
        if (applied_strength != strength)
            ++map_generation;
        applied_strength = strength;
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
    return loaded && (applied_generation != catalog_generation || (strength > 0) != marks_on);
}
extern "C" int pc_emissive_active(void) { return any_marks && pc_emissive > 0; }

namespace emissive {
/* The slot of a texel position, 0 none. */
int slot_at(unsigned u, unsigned v, unsigned tpage)
{
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
    int depth = std::min((int)((tpage >> 7) & 3), 2);
    return the_map[(size_t)(ty + (int)(v & 255)) * PC_EMISSIVE_MAP_W + ((tx * 4 + (int)((u & 255) << depth)) & 4095)];
}
} // namespace emissive

extern "C" int pc_emissive_texel(unsigned u, unsigned v, unsigned tpage, unsigned clut, float rgb[3], float* light)
{
    if (!any_marks)
        return 0;
    int s = slot_at(u, v, tpage);
    if (!s || s > (int)catalog().entries.size())
        return 0;
    const Entry& e = catalog().entries[s - 1];
    unsigned index = 0;
    uint16_t t = texel(u, v, tpage, clut, &index);
    if (!t || !e.enabled || !filters_pass(e.mask, index, t, (tpage >> 7) & 3))
        return 0;
    /* As the shader: the strength byte (the setting folded in). */
    const uint8_t* p = &the_map[(size_t)512 * PC_EMISSIVE_MAP_W + (size_t)s * 64];
    float k = p[3] / 63.75f;
    if (k <= 0)
        return 0;
    if (rgb) {
        rgb[0] = (t & 31) / 31.0f * p[0] / 255.0f * k;
        rgb[1] = ((t >> 5) & 31) / 31.0f * p[1] / 255.0f * k;
        rgb[2] = ((t >> 10) & 31) / 31.0f * p[2] / 255.0f * k;
    }
    if (light)
        *light = e.light;
    return 1;
}

/* ---------------------------------------------------------------- room lights */

namespace {
int lights_scene = -1;
uint32_t lights_map = 0, lights_serial = 1;
std::vector<int> light_triangles; /* sorted scene triangle indices with light texels */
std::vector<uint8_t> is_light_triangle;
const unsigned char GRID[15][2] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 1, 0 }, { 1, 1 }, { 1, 2 },
    { 1, 3 }, { 2, 0 }, { 2, 1 }, { 2, 2 }, { 3, 0 }, { 3, 1 }, { 4, 0 } };
void grid_uv(const scene_triangle* t, int j, unsigned* u, unsigned* v)
{
    float w0 = GRID[j][0] / 4.0f, w1 = GRID[j][1] / 4.0f, w2 = 1.0f - w0 - w1;
    *u = (unsigned)(w0 * t->vertex[0].u + w1 * t->vertex[1].u + w2 * t->vertex[2].u);
    *v = (unsigned)(w0 * t->vertex[0].v + w1 * t->vertex[1].v + w2 * t->vertex[2].v);
}
void refresh_lights()
{
    int scene = pc_scene_serial();
    uint32_t map = map_generation;
    if (scene == lights_scene && map == lights_map)
        return;
    lights_scene = scene, lights_map = map;
    int count = 0;
    const scene_triangle* tris = pc_scene_triangle_data(&count);
    std::vector<int> found;
    is_light_triangle.assign((size_t)(count > 0 ? count : 0), 0);
    const Catalog& c = catalog();
    for (int i = 0; any_marks && i < count; i++) {
        const scene_triangle* t = &tris[i];
        unsigned mode = (t->tpage >> 5) & 3;
        if ((t->code & 2) && (mode == 1 || mode == 3))
            continue; /* additive: a glow already */
        for (int j = 0; j < 15; j++) {
            unsigned u, v;
            grid_uv(t, j, &u, &v);
            int s = slot_at(u, v, t->tpage);
            if (s && s <= (int)c.entries.size() && c.entries[s - 1].light > 0 && c.entries[s - 1].enabled) {
                found.push_back(i);
                is_light_triangle[(size_t)i] = 1;
                break;
            }
        }
    }
    if (found != light_triangles) {
        light_triangles.swap(found);
        ++lights_serial;
    }
}
} // namespace

extern "C" uint32_t pc_emissive_light_serial(void)
{
    refresh_lights();
    return light_triangles.empty() ? 0 : lights_serial;
}
extern "C" int pc_emissive_room_light(int index, float rgb[3])
{
    refresh_lights();
    if (index < 0 || index >= (int)is_light_triangle.size() || !is_light_triangle[(size_t)index])
        return 0;
    if (!rgb)
        return 1;
    int count = 0;
    const scene_triangle* t = &pc_scene_triangle_data(&count)[index];
    float sum[3] = { 0, 0, 0 };
    for (int j = 0; j < 15; j++) {
        unsigned u, v;
        grid_uv(t, j, &u, &v);
        float e[3], light = 0;
        if (pc_emissive_texel(u, v, t->tpage, t->clut, e, &light) && light > 0)
            for (int k = 0; k < 3; k++)
                sum[k] += e[k] * light;
    }
    for (int k = 0; k < 3; k++)
        rgb[k] = sum[k] / 15.0f;
    return 1;
}
namespace emissive {
int light_triangle_count()
{
    refresh_lights();
    return (int)light_triangles.size();
}
} // namespace emissive

/* ---------------------------------------------------------------- object lights */

namespace {
struct ObjectSink {
    pc_glow_fragment* out;
    int n, max;
};
void object_sink(void* context, const float w[9], const uint8_t* /*rgb*/, const uint8_t uv[6], unsigned code,
    unsigned tpage, unsigned clut)
{
    ObjectSink* s = (ObjectSink*)context;
    if (s->n >= s->max || !(code & 4))
        return;
    /* The corners and the centre; a texel of a light entry under any. */
    static const float bary[4][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1 / 3.0f, 1 / 3.0f, 1 / 3.0f } };
    float sum[3] = { 0, 0, 0 };
    int lit = 0;
    for (int j = 0; j < 4; j++) {
        unsigned u = (unsigned)(bary[j][0] * uv[0] + bary[j][1] * uv[2] + bary[j][2] * uv[4]);
        unsigned v = (unsigned)(bary[j][0] * uv[1] + bary[j][1] * uv[3] + bary[j][2] * uv[5]);
        float e[3], light = 0;
        if (pc_emissive_texel(u, v, tpage, clut, e, &light) && light > 0) {
            for (int k = 0; k < 3; k++)
                sum[k] += e[k] * light;
            lit++;
        }
    }
    if (!lit)
        return;
    pc_glow_fragment* f = &s->out[s->n++];
    float e1[3], e2[3], c[3];
    for (int k = 0; k < 3; k++) {
        e1[k] = w[3 + k] - w[k], e2[k] = w[6 + k] - w[k];
        f->p[k] = (w[k] + w[3 + k] + w[6 + k]) / 3.0f;
        f->rgb[k] = sum[k] / 4.0f;
    }
    c[0] = e1[1] * e2[2] - e1[2] * e2[1], c[1] = e1[2] * e2[0] - e1[0] * e2[2], c[2] = e1[0] * e2[1] - e1[1] * e2[0];
    f->area = 0.5f * sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    f->weight = f->area * (0.299f * f->rgb[0] + 0.587f * f->rgb[1] + 0.114f * f->rgb[2]);
    uint32_t source, mesh;
    render_emit_tag(&source, &mesh);
    f->source = (uint32_t)PC_GLOW_ORIGIN_OBJECT << 28 | ((mesh * 0x9e3779b1u) & 0x0fffffffu);
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
    uint32_t lists[64];
    int n = pc_hw_present_all_lists(lists, 64);
    ObjectSink s = { out, 0, max };
    for (int i = 0; i < n && s.n < max; i++)
        if (render_list_objects(lists[i]) > 0) /* objects (1) and other room blocks (2); the room's are scene triangles */
            render_list_triangles(lists[i], object_sink, &s);
    return s.n;
}
