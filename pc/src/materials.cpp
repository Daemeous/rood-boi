// Material channel maps (materials.h, pc/docs/Materials.md).
#include "materials.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL.h>

#include "light_prefabs.h"
#include "pc_paths.h"
#include "platform.h"

namespace fs = std::filesystem;

int pc_materials_baked = 1;

namespace materials {

std::string user_dir()
{
    const char* e = getenv("ROOD_MATERIALS");
    return e && *e ? e : pc_path_join("ROOD_LIGHTS", "pc/user/lighting", "materials");
}
std::string shipped_dir()
{
    const char* e = getenv("ROOD_MATERIALS_SHIPPED");
    return e && *e ? e : "pc/assets/materials";
}
std::string baked_dir()
{
    const char* e = getenv("ROOD_MATERIALS_BAKED");
    return e && *e ? e : "pc/build/materials";
}
std::string export_dir()
{
    const char* e = getenv("ROOD_MATERIAL_EXPORTS");
    return e && *e ? e : "pc/exports/materials";
}
bool known_channel(const std::string& name)
{
    return name == "emissive" || name == "roughness" || name == "height" || name == "metal";
}
uint8_t blank_value(const std::string& name) { return name == "roughness" ? 255 : name == "height" ? 128 : 0; }

static bool valid_key(const std::string& key)
{
    return key.size() == 10 && key.find_first_not_of("0123456789abcdef") == std::string::npos;
}
static bool valid_name(const std::string& name)
{
    if (name.empty() || name.size() > 32)
        return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}
static long long stamp(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        return 0;
    return PC_MTIME_NS(st) + st.st_size;
}
static void mkdirs(const std::string& path)
{
    std::error_code ec;
    fs::create_directories(fs::path(path), ec);
}

/* ---------------------------------------------------------------- PNG */

namespace {
uint32_t crc_table[256];
void crc_init()
{
    if (crc_table[1])
        return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xffffffffu)
{
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
void put32(std::string& s, uint32_t v)
{
    s += (char)(v >> 24), s += (char)(v >> 16), s += (char)(v >> 8), s += (char)v;
}
void chunk(std::string& out, const char* type, const std::string& data)
{
    put32(out, (uint32_t)data.size());
    std::string body = std::string(type, 4) + data;
    out += body;
    put32(out, crc((const uint8_t*)body.data(), body.size()) ^ 0xffffffffu);
}
} // namespace

/* 8-bit grayscale (channels 1) or RGBA (4), deflate in stored blocks (the
 * maps are small: a texture's box is at most 256 x 256). */
bool write_png(const std::string& path, int w, int h, int channels, const uint8_t* data, std::string* error)
{
    if (w <= 0 || h <= 0 || (channels != 1 && channels != 4)) {
        if (error)
            *error = "bad image size";
        return false;
    }
    crc_init();
    std::string raw;
    for (int y = 0; y < h; y++) {
        raw += '\0'; /* filter: none */
        raw.append((const char*)data + (size_t)y * w * channels, (size_t)w * channels);
    }
    std::string z;
    z += (char)0x78, z += (char)0x01;
    size_t at = 0;
    do {
        size_t n = std::min<size_t>(65535, raw.size() - at);
        bool last = at + n == raw.size();
        z += (char)(last ? 1 : 0);
        z += (char)(n & 255), z += (char)(n >> 8), z += (char)(~n & 255), z += (char)((~n >> 8) & 255);
        z.append(raw, at, n);
        at += n;
    } while (at < raw.size());
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw)
        a = (a + c) % 65521, b = (b + a) % 65521;
    put32(z, b << 16 | a);
    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    put32(ihdr, (uint32_t)w);
    put32(ihdr, (uint32_t)h);
    ihdr += (char)8, ihdr += (char)(channels == 1 ? 0 : 6), ihdr += (char)0, ihdr += (char)0, ihdr += (char)0;
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", "");
    mkdirs(fs::path(path).parent_path().string());
    std::string temp = path + ".tmp";
    FILE* f = fopen(temp.c_str(), "wb");
    if (!f) {
        if (error)
            *error = "cannot write " + temp;
        return false;
    }
    bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        remove(temp.c_str());
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    return true;
}
bool read_png_gray(const std::string& path, int* w, int* h, std::vector<uint8_t>& out, std::string* error)
{
    SDL_Surface* loaded = SDL_LoadPNG(path.c_str());
    if (!loaded) {
        if (error)
            *error = path + ": " + SDL_GetError();
        return false;
    }
    SDL_Surface* s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!s) {
        if (error)
            *error = path + ": " + SDL_GetError();
        return false;
    }
    *w = s->w, *h = s->h;
    out.assign((size_t)s->w * s->h, 0);
    bool alpha = false;
    for (int y = 0; y < s->h && !alpha; y++) {
        const uint8_t* row = (const uint8_t*)s->pixels + (size_t)y * s->pitch;
        for (int x = 0; x < s->w && !alpha; x++)
            alpha = row[x * 4 + 3] != 255;
    }
    /* A mask painted into the alpha (an alpha-only layer) is its alpha;
     * otherwise its brightness (grayscale; colour images by luminance). */
    for (int y = 0; y < s->h; y++) {
        const uint8_t* row = (const uint8_t*)s->pixels + (size_t)y * s->pitch;
        for (int x = 0; x < s->w; x++) {
            const uint8_t* p = row + x * 4;
            out[(size_t)y * s->w + x] = alpha ? p[3] : (uint8_t)((p[0] * 299 + p[1] * 587 + p[2] * 114 + 500) / 1000);
        }
    }
    SDL_DestroySurface(s);
    return true;
}

/* ---------------------------------------------------------------- the layers */

namespace {
/* The layers by precedence: the user's, the shipped one, the baked one. */
enum { USER, SHIPPED, BAKED, LAYERS };
struct Cached {
    long long stamps[LAYERS] = { -1, -1, -1 };
    bool present = false;
    Channel channel;
};
std::map<std::pair<std::string, std::string>, Cached> cache;
uint32_t files_generation = 1;
int applied_baked = -1; /* pc_materials_baked the cache was loaded with */

std::string channel_path(const std::string& dir, const std::string& key, const std::string& name)
{
    return dir + "/" + key + "/" + name + ".png";
}
std::string layer_dir(int layer) { return layer == USER ? user_dir() : layer == SHIPPED ? shipped_dir() : baked_dir(); }
bool layer_on(int layer) { return layer != BAKED || pc_materials_baked; }
long long layer_stamp(int layer, const std::string& key, const std::string& name)
{
    return layer_on(layer) ? stamp(channel_path(layer_dir(layer), key, name)) : 0;
}
void load(const std::string& key, const std::string& name, Cached& c)
{
    c.present = false;
    c.channel = Channel();
    for (int layer = 0; layer < LAYERS; layer++)
        c.stamps[layer] = layer_stamp(layer, key, name);
    for (int layer = 0; layer < LAYERS && !c.present; layer++) {
        if (!c.stamps[layer])
            continue;
        std::string path = channel_path(layer_dir(layer), key, name), error;
        int w = 0, h = 0;
        if (!read_png_gray(path, &w, &h, c.channel.values, &error)) {
            fprintf(stderr, "[materials] %s\n", error.c_str());
            continue;
        }
        c.channel.w = w, c.channel.h = h, c.channel.path = path;
        c.channel.shipped = layer == SHIPPED, c.channel.baked = layer == BAKED;
        c.present = true;
    }
}
/* The baked layer switched: everything reloads. */
void check_baked()
{
    if (applied_baked != (pc_materials_baked != 0)) {
        applied_baked = pc_materials_baked != 0;
        cache.clear();
        ++files_generation;
    }
}
} // namespace

const Channel* channel(const std::string& key, const std::string& name)
{
    if (!valid_key(key) || !valid_name(name))
        return nullptr;
    check_baked();
    auto found = cache.find({ key, name });
    if (found == cache.end()) {
        found = cache.emplace(std::make_pair(key, name), Cached()).first;
        load(key, name, found->second);
    }
    return found->second.present ? &found->second.channel : nullptr;
}
std::vector<Present> channels(const std::string& key)
{
    std::vector<Present> out;
    if (!valid_key(key))
        return out;
    for (int layer = 0; layer < LAYERS; layer++) {
        if (!layer_on(layer))
            continue;
        std::error_code ec;
        fs::path dir = fs::path(layer_dir(layer)) / key;
        for (const auto& f : fs::directory_iterator(dir, ec)) {
            if (f.path().extension() != ".png")
                continue;
            std::string name = f.path().stem().string();
            if (name == "reference" || !valid_name(name))
                continue;
            bool seen = false;
            for (const Present& p : out)
                seen |= p.name == name;
            if (!seen)
                out.push_back({ name, f.path().string(), layer == USER, layer == BAKED });
        }
    }
    return out;
}
uint32_t generation()
{
    check_baked();
    return files_generation;
}
void poll()
{
    check_baked();
    /* A slice of the files at a time (three stat calls each), at most every
     * 50 ms: a large catalog looks at thousands, and a sweep of them all on
     * every call cost a millisecond, several times a frame. */
    static uint64_t last;
    static std::pair<std::string, std::string> next; /* where the last slice stopped */
    uint64_t now = SDL_GetTicks();
    if (now - last < 50)
        return;
    last = now;
    auto it = cache.lower_bound(next);
    for (size_t n = 0; n < 256 && n < cache.size(); n++, ++it) {
        if (it == cache.end())
            it = cache.begin();
        const std::string &key = it->first.first, &name = it->first.second;
        bool changed = false;
        for (int layer = 0; layer < LAYERS; layer++)
            changed |= layer_stamp(layer, key, name) != it->second.stamps[layer];
        if (changed) {
            load(key, name, it->second);
            ++files_generation;
        }
    }
    next = it == cache.end() ? std::pair<std::string, std::string>() : it->first;
}
void forget()
{
    cache.clear();
    ++files_generation;
}

bool write_channel(const std::string& key, const std::string& name, int w, int h, const uint8_t* values,
    std::string* error)
{
    if (!valid_key(key) || !valid_name(name)) {
        if (error)
            *error = "bad texture key or channel name";
        return false;
    }
    std::string path = channel_path(user_dir(), key, name);
    if (!write_png(path, w, h, 1, values, error))
        return false;
    auto found = cache.find({ key, name });
    if (found != cache.end())
        load(key, name, found->second);
    ++files_generation;
    return true;
}
bool import_channel(const std::string& key, const std::string& name, const std::string& source, int w, int h,
    std::string* error)
{
    int iw = 0, ih = 0;
    std::vector<uint8_t> values;
    if (!read_png_gray(source, &iw, &ih, values, error))
        return false;
    if (iw != w || ih != h) {
        if (error) {
            char text[96];
            snprintf(text, sizeof text, " is %d x %d; the texture is %d x %d", iw, ih, w, h);
            *error = source + text;
        }
        return false;
    }
    return write_channel(key, name, w, h, values.data(), error);
}

/* Whether `dir` lies inside `root` (both resolved, the parts that do not
 * exist yet taken as written). */
static bool inside(const fs::path& dir, const fs::path& root)
{
    std::error_code ec;
    fs::path a = fs::weakly_canonical(fs::absolute(dir, ec), ec), b = fs::weakly_canonical(fs::absolute(root, ec), ec);
    auto ai = a.begin(), bi = b.begin();
    for (; bi != b.end() && !bi->empty(); ++ai, ++bi)
        if (ai == a.end() || *ai != *bi)
            return false;
    return true;
}
bool export_reference(const std::string& key, int w, int h, const uint8_t* rgba, const std::string& dir_in,
    const std::string& channel_name, std::vector<std::string>* written, std::string* error)
{
    if (!valid_key(key) || !valid_name(channel_name)) {
        if (error)
            *error = "bad texture key or channel name";
        return false;
    }
    std::string dir = dir_in.empty() ? export_dir() : dir_in;
    /* The reference is the game's art: never where the repository ships
     * files (pc/assets, the shipped layer). */
    if (inside(dir, "pc/assets") || inside(dir, shipped_dir())) {
        if (error)
            *error = "refusing to export a texture into " + dir + ": the shipped assets never hold game art";
        return false;
    }
    std::string reference = dir + "/" + key + "/reference.png";
    if (!write_png(reference, w, h, 4, rgba, error))
        return false;
    if (written)
        written->push_back(reference);
    std::string user = channel_path(user_dir(), key, channel_name);
    if (!stamp(user)) {
        std::vector<uint8_t> empty((size_t)w * h, blank_value(channel_name));
        if (!write_channel(key, channel_name, w, h, empty.data(), error))
            return false;
        if (written)
            written->push_back(user);
    }
    return true;
}

/* Catalog entries are keyed by content (never by a VRAM location). The user
 * file replaces shipped entries by id, just as the emissive catalog does.
 * Both files are read as instances (materials.h, "Master materials and
 * instances"); the Entry list is derived from the assigned ones. */
namespace {
using prefab::Json;

/* ---- masters ---- */
const char* const STANDARD = "standard_surface";
Master standard_master()
{
    Master m;
    m.name = STANDARD, m.shader = "builtin:standard_surface";
    m.params = {
        { "roughness", "Roughness", "0 mirror, 1 fully rough (a roughness map overrides it per texel)", "Surface", false, 1, 0, 1 },
        { "specular", "Specular", "Fraction of a dielectric's reflectance (4% at normal incidence); keep it subtle", "Surface", false, 0, 0, 1 },
        { "metal", "Metal", "0 stone, wood, glass; 1 bare metal (tinted reflections, no diffuse light)", "Surface", false, 0, 0, 1 },
        { "auto_height", "Estimate the height from the colours", "auto_height: bumps from the texture's luminance (dark low, bright high) where it has no height.png", "Relief", true, 0, 0, 1 },
        { "bump", "Relief", "bump: this surface's multiple of the global relief depth (bump_strength)", "Relief", false, 1, 0, 4 },
    };
    m.slots = { { "texel", "Game texel", "" }, { "roughness", "Roughness map", "roughness" }, { "height", "Height map", "height" },
        { "metal", "Metal map", "metal" }, { "emissive", "Emissive map", "emissive" } };
    return m;
}
std::map<std::string, Master> masters_cache;
long long masters_stamp = -2;
std::string read_text(const std::string& path)
{
    std::string text;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return text;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f))) text.append(buf, n);
    fclose(f);
    return text;
}
bool read_master(const std::string& path, Master& m)
{
    Json j; std::string error;
    std::string text = read_text(path);
    if (text.empty() || !prefab::json_parse(text.c_str(), j, &error) || j.type != Json::Object) return false;
    const Json* name = j.get("name");
    if (!name || name->type != Json::String || name->string.empty()) return false;
    m = Master();
    m.name = name->string, m.path = path;
    if (const Json* s = j.get("shader"); s && s->type == Json::String) m.shader = s->string;
    const Json* params = j.get("parameters");
    if (!params || params->type != Json::Array) return false;
    for (const Json& item : params->items) {
        if (item.type != Json::Object) return false;
        MasterParam p;
        const Json* n = item.get("name");
        if (!n || n->type != Json::String || n->string.empty()) return false;
        p.name = n->string;
        if (const Json* v = item.get("type"); v && v->type == Json::String) p.is_bool = v->string == "bool", p.is_color = v->string == "color";
        if (const Json* v = item.get("default")) {
            if (v->type == Json::Number) p.def = (float)v->number;
            else if (v->type == Json::Bool) p.def = v->boolean ? 1.0f : 0.0f;
            else if (v->type == Json::Array && p.is_color)
                for (int c = 0; c < 3 && c < (int)v->items.size(); c++)
                    if (v->items[c].type == Json::Number) p.color[c] = (float)v->items[c].number;
        }
        if (const Json* v = item.get("min"); v && v->type == Json::Number) p.lo = (float)v->number;
        if (const Json* v = item.get("max"); v && v->type == Json::Number) p.hi = (float)v->number;
        if (const Json* v = item.get("label"); v && v->type == Json::String) p.label = v->string;
        if (const Json* v = item.get("tip"); v && v->type == Json::String) p.tip = v->string;
        if (const Json* v = item.get("group"); v && v->type == Json::String) p.group = v->string;
        if (p.label.empty()) p.label = p.name;
        m.params.push_back(p);
    }
    if (const Json* slots = j.get("slots"); slots && slots->type == Json::Array)
        for (const Json& item : slots->items) {
            if (item.type != Json::Object) continue;
            MasterSlot sl;
            const Json* n = item.get("name");
            if (!n || n->type != Json::String) continue;
            sl.name = n->string;
            if (const Json* v = item.get("label"); v && v->type == Json::String) sl.label = v->string;
            if (const Json* v = item.get("channel"); v && v->type == Json::String) sl.channel = v->string;
            if (sl.label.empty()) sl.label = sl.name;
            m.slots.push_back(sl);
        }
    return true;
}
/* Both master folders' stamps (and their files': a graph saved in place
 * changes its file, not always its folder). */
long long masters_stamp_last_at = -1; /* (masters_changed resets it) */
long long masters_stamp_now()
{
    long long& last_at = masters_stamp_last_at;
    /* Asked every frame (the catalog's generation): the folders' files are stat'ed at most four times a second. */
    static long long last_st;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (last_at >= 0 && now_ms - last_at < 250) return last_st;
    last_at = now_ms;
    long long st = stamp(masters_dir()) * 31 + stamp(graphs_dir());
    for (const std::string& dir : { masters_dir(), graphs_dir() }) {
        std::error_code ec;
        for (const auto& ent : fs::directory_iterator(dir, ec))
            if (ent.path().extension() == ".json") st = st * 1000003 + stamp(ent.path().string());
    }
    last_st = st;
    return st;
}
/* The masters: the built-in standard surface, then every file in masters_dir()
 * (a file named standard_surface replaces the built-in one, provided it
 * keeps the five standard parameters: the planes read those by name). */
const std::map<std::string, Master>& current_masters()
{
    long long st = masters_stamp_now();
    if (st == masters_stamp) return masters_cache;
    masters_stamp = st;
    masters_cache.clear();
    masters_cache[STANDARD] = standard_master();
    /* The shipped masters, then the user's graphs (part 4: the same name wins). */
    std::vector<std::string> files;
    for (const std::string& dir : { masters_dir(), graphs_dir() }) {
        std::error_code ec;
        std::vector<std::string> here;
        for (const auto& ent : fs::directory_iterator(dir, ec))
            if (ent.path().extension() == ".json") here.push_back(ent.path().string());
        std::sort(here.begin(), here.end());
        files.insert(files.end(), here.begin(), here.end());
    }
    for (const std::string& path : files) {
        Master m;
        if (!read_master(path, m)) {
            fprintf(stderr, "[materials] %s: not a master\n", path.c_str());
            continue;
        }
        if (m.name == STANDARD) {
            bool complete = true;
            for (const char* n : { "roughness", "specular", "metal", "auto_height", "bump" }) complete &= m.param(n) != nullptr;
            if (!complete) {
                fprintf(stderr, "[materials] %s: the standard surface must keep its five parameters\n", m.path.c_str());
                continue;
            }
        }
        masters_cache[m.name] = m;
    }
    return masters_cache;
}

/* ---- the catalog ---- */
struct CatalogState {
    std::vector<Instance> instances;
    std::vector<Entry> entries; /* derived from the assigned instances, in order */
    std::unordered_map<std::string, size_t> by_id;
    std::map<int, bool> sizes; /* (w - 1) << 8 | (h - 1) */
    bool palette_entries = false; /* any entry matches by palette alone (every size is hashed) */
    /* find_match's index: the entries naming each key, and the palette
     * entries by palette, in catalog order; what needs a palette's key. */
    std::unordered_map<std::string, std::vector<size_t>> by_key, by_clut;
    std::unordered_set<std::string> clut_keys; /* keys some entry names with a palette */
    bool palette_clut = false; /* a palette entry with a palette (then every key needs it) */
    std::vector<std::string> problems;
    std::unordered_map<std::string, size_t> entry_at; /* instance id -> its entry in `entries` */
    uint32_t generation = 0, structure = 0; /* any change; a change of more than values (structure) */
};
CatalogState catalog_cache;
/* The two files as read (instances and their problems): read again only when
 * a file's stamp or the masters changed, on Revert and on reload; an edit is
 * laid over this copy in memory (pending_list), never by reading the files. */
struct FileLayer {
    std::vector<Instance> instances;
    std::vector<std::string> problems;
    long long shipped_stamp = -1, user_stamp = -1, masters_seen = -3;
    bool stale = true;
};
FileLayer file_layer;
unsigned catalog_reads = 0; /* catalog files read (RPC material_instance status) */
std::vector<Instance> pending_list; /* unsaved editor instances (set_pending) */
std::vector<std::string> pending_removed;
std::vector<Instance> preview_list; /* set_preview: temporary, never saved */
uint32_t pending_serial = 1, applied_pending = 0;
std::string catalog_path()
{
    const char* e = getenv("ROOD_MATERIAL_CATALOG");
    return e && *e ? e : shipped_dir() + "/catalog.json";
}
std::string catalog_user_path()
{
    const char* e = getenv("ROOD_MATERIAL_CATALOG_USER");
    return e && *e ? e : pc_path_join("ROOD_LIGHTS", "pc/user/lighting", "materials.json");
}
std::string string_field(const Json& j, const char* name)
{
    const Json* v = j.get(name);
    return v && v->type == Json::String ? v->string : "";
}
bool number_field(const Json& j, const char* name, float* out)
{
    const Json* v = j.get(name);
    if (!v || v->type != Json::Number || !std::isfinite(v->number) || v->number < 0 || v->number > 1)
        return false;
    *out = (float)v->number;
    return true;
}
/* The relief fields (entries and presets): auto_height (a boolean) and bump (a
 * multiplier of the global relief, 0..4). Absent: left as they are. */
bool relief_fields(const Json& j, std::map<std::string, float>& own)
{
    if (const Json* v = j.get("auto_height")) {
        if (v->type != Json::Bool) return false;
        own["auto_height"] = v->boolean ? 1.0f : 0.0f;
    }
    if (const Json* v = j.get("bump")) {
        if (v->type != Json::Number || !std::isfinite(v->number) || v->number < 0 || v->number > 4) return false;
        own["bump"] = (float)v->number;
    }
    return true;
}
/* A parameter of another master, written under "params" {name: value}:
 * numbers within the master's range, booleans. */
bool extra_params(const Json& j, const Master& m, std::map<std::string, float>& own)
{
    const Json* params = j.get("params");
    if (!params) return true;
    if (params->type != Json::Object) return false;
    for (const auto& pair : params->members) {
        const MasterParam* p = m.param(pair.first);
        if (!p) return false;
        if (p->is_color) {
            /* [r, g, b], each 0..1 */
            if (pair.second.type != Json::Array || pair.second.items.size() != 3) return false;
            for (int c = 0; c < 3; c++) {
                const Json& v = pair.second.items[c];
                if (v.type != Json::Number || !std::isfinite(v.number) || v.number < 0 || v.number > 1) return false;
                own[pair.first + COLOR_PARTS[c]] = (float)v.number;
            }
        } else if (pair.second.type == Json::Bool) own[pair.first] = pair.second.boolean ? 1.0f : 0.0f;
        else if (pair.second.type == Json::Number && std::isfinite(pair.second.number) && pair.second.number >= p->lo
            && pair.second.number <= p->hi) own[pair.first] = (float)pair.second.number;
        else return false;
    }
    return true;
}
bool parse_scope(const Json& j, Instance& e);
/* An entry of either file as an instance. `presets`: the preset names known
 * so far (an entry naming an unknown one is invalid, as before). */
bool parse_entry(const Json& j, const std::map<std::string, Master>& masters, Instance& e)
{
    if (j.type != Json::Object) return false;
    e = Instance();
    e.id = string_field(j, "id"), e.name = string_field(j, "name");
    e.where = string_field(j, "where"), e.review = string_field(j, "review");
    if (const Json* v = j.get("master"); v) {
        if (v->type != Json::String || !masters.count(v->string)) return false;
        e.master = v->string;
    }
    const Master& m = masters.at(e.master);
    std::string preset = string_field(j, "preset"), parent = string_field(j, "parent");
    e.parent = parent.empty() ? preset : parent;
    float v;
    if (j.get("roughness")) { if (!number_field(j, "roughness", &v)) return false; e.own["roughness"] = v; }
    if (j.get("specular")) { if (!number_field(j, "specular", &v)) return false; e.own["specular"] = v; }
    if (j.get("metal")) { if (!number_field(j, "metal", &v)) return false; e.own["metal"] = v; }
    if (!relief_fields(j, e.own) || !extra_params(j, m, e.own)) return false;
    if (e.id.empty()) return false;
    const Json* match = j.get("match");
    if (!match) return parse_scope(j, e); /* an unassigned instance (format 2) */
    if (match->type != Json::Object) return false;
    e.assigned = true;
    e.key = string_field(*match, "key"), e.clut = string_field(*match, "clut");
    const Json* size = match->get("size");
    /* A palette entry (no key): every texture drawn with the palette (of
     * that size, when it names one). A texture's own entry wins over it. */
    bool palette_entry = !match->get("key");
    if (palette_entry && !valid_key(e.clut)) return false;
    if (palette_entry && !size) return parse_scope(j, e);
    if (!size || size->type != Json::Array || size->items.size() != 2
        || size->items[0].type != Json::Number || size->items[1].type != Json::Number) return false;
    if (!std::isfinite(size->items[0].number) || !std::isfinite(size->items[1].number)
        || size->items[0].number < 1 || size->items[0].number > 256
        || size->items[1].number < 1 || size->items[1].number > 256) return false;
    e.w = (int)size->items[0].number, e.h = (int)size->items[1].number;
    if ((!palette_entry && !valid_key(e.key)) || (e.clut.size() && !valid_key(e.clut))
        || size->items[0].number != e.w || size->items[1].number != e.h) return false;
    return parse_scope(j, e);
}
/* The entry's optional scope: {"room": [zone, room]} or {"rooms": [[zone,
 * room], ...]} (one or more rooms, no repeats). */
static bool room_pair(const Json& v, int* zone, int* room)
{
    if (v.type != Json::Array || v.items.size() != 2 || v.items[0].type != Json::Number
        || v.items[1].type != Json::Number) return false;
    for (const Json& n : v.items)
        if (!std::isfinite(n.number) || n.number < 0 || n.number > 255 || n.number != (int)n.number) return false;
    *zone = (int)v.items[0].number, *room = (int)v.items[1].number;
    return true;
}
bool parse_scope(const Json& j, Instance& e)
{
    const Json* scope = j.get("scope");
    if (!scope) return true;
    if (scope->type != Json::Object) return false;
    const Json* room = scope->get("room");
    const Json* rooms = scope->get("rooms");
    if ((room != nullptr) == (rooms != nullptr)) return false;
    if (room) return room_pair(*room, &e.zone, &e.room);
    if (rooms->type != Json::Array || rooms->items.empty()) return false;
    for (const Json& item : rooms->items) {
        int z, r;
        if (!room_pair(item, &z, &r)) return false;
        if (std::find(e.rooms.begin(), e.rooms.end(), std::make_pair(z, r)) != e.rooms.end()) return false;
        e.rooms.push_back({ z, r });
    }
    e.zone = e.rooms[0].first, e.room = e.rooms[0].second;
    if (e.rooms.size() == 1) e.rooms.clear();
    return true;
}
void upsert(std::vector<Instance>& list, const Instance& e)
{
    auto existing = std::find_if(list.begin(), list.end(), [&](const Instance& old) { return old.id == e.id; });
    if (existing == list.end()) list.push_back(e);
    else *existing = e;
}
void read_catalog(const std::string& path, std::vector<Instance>& out, bool user, std::vector<std::string>& problems)
{
    ++catalog_reads;
    std::string text = read_text(path);
    if (text.empty()) return;
    Json j; std::string error;
    if (!prefab::json_parse(text.c_str(), j, &error) || j.type != Json::Object) {
        fprintf(stderr, "[materials] %s: %s\n", path.c_str(), error.c_str());
        problems.push_back(path + ": " + error);
        return;
    }
    const std::map<std::string, Master>& masters = current_masters();
    if (const Json* p = j.get("presets"); p && p->type == Json::Object)
        for (const auto& pair : p->members) {
            if (pair.second.type != Json::Object) continue;
            Instance r;
            float v;
            r.id = pair.first, r.root = true, r.user = user;
            if (!number_field(pair.second, "roughness", &v)) continue;
            r.own["roughness"] = v;
            if (!number_field(pair.second, "specular", &v)) continue;
            r.own["specular"] = v;
            if (!number_field(pair.second, "metal", &v)) continue;
            r.own["metal"] = v;
            if (!relief_fields(pair.second, r.own)) continue;
            if (const Json* m = pair.second.get("master"); m && m->type == Json::String && masters.count(m->string))
                r.master = m->string;
            upsert(out, r);
        }
    if (const Json* arr = j.get("entries"); arr && arr->type == Json::Array)
        for (const Json& item : arr->items) {
            Instance e;
            if (!parse_entry(item, masters, e)) {
                fprintf(stderr, "[materials] %s: invalid catalog entry\n", path.c_str());
                problems.push_back(path + ": invalid catalog entry " + string_field(item, "id"));
                continue;
            }
            e.user = user;
            auto existing = std::find_if(out.begin(), out.end(), [&](const Instance& old) { return old.id == e.id; });
            if (existing != out.end() && existing->root)
                problems.push_back(path + ": entry " + e.id + " has a preset's name (the entry wins)");
            upsert(out, e);
        }
}
/* Resolution over a list (the catalog being built, or one with a candidate added). */
bool resolve_in(const std::vector<Instance>& list, const std::unordered_map<std::string, size_t>& by_id,
    const Instance& start, std::map<std::string, float>& out, std::vector<std::string>* chain, std::string* error)
{
    const std::map<std::string, Master>& masters = current_masters();
    auto m = masters.find(start.master);
    if (m == masters.end()) {
        if (error) *error = start.id + ": unknown master " + start.master;
        return false;
    }
    out.clear();
    if (chain) chain->clear();
    std::vector<const Instance*> walk;
    const Instance* cur = &start;
    std::unordered_set<std::string> seen;
    for (;;) {
        walk.push_back(cur);
        if (chain) chain->push_back(cur->id);
        if (!seen.insert(cur->id).second) {
            if (error) *error = start.id + ": the parents loop at " + cur->id;
            return false;
        }
        if (cur->parent.empty()) break;
        auto next = by_id.find(cur->parent);
        if (next == by_id.end()) {
            if (error) *error = start.id + ": unknown parent " + cur->parent;
            return false;
        }
        cur = &list[next->second];
    }
    for (const MasterParam& p : m->second.params)
        if (p.is_color)
            for (int c = 0; c < 3; c++) out[p.name + COLOR_PARTS[c]] = p.color[c];
        else
            out[p.name] = p.def;
    for (auto it = walk.rbegin(); it != walk.rend(); ++it)
        for (const auto& pair : (*it)->own) out[pair.first] = pair.second;
    return true;
}
unsigned own_mask(const Instance& i)
{
    return (i.own.count("roughness") ? 1u : 0u) | (i.own.count("specular") ? 2u : 0u) | (i.own.count("metal") ? 4u : 0u)
        | (i.own.count("auto_height") ? 8u : 0u) | (i.own.count("bump") ? 16u : 0u);
}
bool derive_entry(const std::vector<Instance>& list, const std::unordered_map<std::string, size_t>& by_id,
    const Instance& i, Entry& e, std::string* error)
{
    std::map<std::string, float> values;
    std::vector<std::string> chain;
    if (!resolve_in(list, by_id, i, values, &chain, error)) return false;
    e = Entry();
    e.id = i.id, e.name = i.name, e.where = i.where, e.review = i.review;
    e.key = i.key, e.clut = i.clut, e.w = i.w, e.h = i.h, e.zone = i.zone, e.room = i.room, e.rooms = i.rooms;
    e.value = surface_of(values);
    e.explicit_fields = own_mask(i);
    e.user = i.user, e.preview = i.preview;
    /* preset: the root preset at the top of the chain (what old readers
     * see); parent: the instance's parent when that is not the preset. */
    const Instance* top = &list[by_id.at(chain.back())];
    if (chain.size() == 1) top = &i;
    if (top->root && top != &i) e.preset = top->id;
    if (!i.parent.empty() && i.parent != e.preset) e.parent = i.parent;
    return true;
}
/* The catalog from the files' copy and the overlays (no file read). */
void rebuild_catalog()
{
    CatalogState& cc = catalog_cache;
    cc.instances = file_layer.instances;
    cc.problems = file_layer.problems;
    cc.entries.clear(), cc.sizes.clear(), cc.entry_at.clear();
    for (const std::string& id : pending_removed)
        cc.instances.erase(std::remove_if(cc.instances.begin(), cc.instances.end(),
            [&](const Instance& e) { return e.id == id && e.user; }), cc.instances.end());
    for (const Instance& p : pending_list) upsert(cc.instances, p);
    for (const Instance& p : preview_list) upsert(cc.instances, p);
    cc.by_id.clear();
    for (size_t i = 0; i < cc.instances.size(); i++) cc.by_id[cc.instances[i].id] = i;
    /* The entries: every assigned instance that resolves (one that does
     * not is dropped, as an entry naming an unknown preset was). */
    for (const Instance& i : cc.instances) {
        if (!i.assigned) continue;
        Entry e; std::string error;
        if (!derive_entry(cc.instances, cc.by_id, i, e, &error)) {
            fprintf(stderr, "[materials] %s\n", error.c_str());
            cc.problems.push_back(error);
            continue;
        }
        cc.entry_at[i.id] = cc.entries.size();
        cc.entries.push_back(e);
    }
    cc.palette_entries = cc.palette_clut = false;
    cc.by_key.clear(), cc.by_clut.clear(), cc.clut_keys.clear();
    for (size_t i = 0; i < cc.entries.size(); i++) {
        const Entry& e = cc.entries[i];
        if (e.w > 0) cc.sizes[(e.w - 1) << 8 | (e.h - 1)] = true;
        cc.palette_entries |= e.key.empty();
        (e.key.empty() ? cc.by_clut[e.clut] : cc.by_key[e.key]).push_back(i);
        if (!e.clut.empty() && e.key.empty()) cc.palette_clut = true;
        else if (!e.clut.empty()) cc.clut_keys.insert(e.key);
    }
    ++cc.generation, ++cc.structure;
}
const std::vector<Entry>& current_catalog()
{
    /* Asked for every draw batch and, when a palette changes, for every texture it
     * touches: the files are stat'ed at most four times a second, as the masters are.
     * stat is cheap on Linux but not on Windows, where this took much of the game
     * thread's frame in the targeting sphere's many batches and held the game near
     * 12 updates a second as it opened. An edit in the game still shows at once. */
    static long long looked_at = -1;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const long long now_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    FileLayer& f = file_layer;
    bool files = false;
    if (f.stale || looked_at < 0 || now_ms - looked_at >= 250) {
        looked_at = now_ms;
        const long long a = stamp(catalog_path()), b = stamp(catalog_user_path()), c = masters_stamp_now();
        files = f.stale || a != f.shipped_stamp || b != f.user_stamp || c != f.masters_seen;
        if (files) {
            f.stale = false;
            f.shipped_stamp = a, f.user_stamp = b, f.masters_seen = c;
            f.instances.clear(), f.problems.clear();
            read_catalog(catalog_path(), f.instances, false, f.problems);
            read_catalog(catalog_user_path(), f.instances, true, f.problems);
        }
    }
    if (files || applied_pending != pending_serial) {
        applied_pending = pending_serial;
        rebuild_catalog();
    }
    return catalog_cache.entries;
}
/* Whether `edited` differs from the catalog's copy in its values only (the
 * same parent, master, assignment and curation: the entries' order, their
 * matching and the graph slots stay). */
bool values_only(const Instance& old, const Instance& edited)
{
    return old.parent == edited.parent && old.master == edited.master && old.root == edited.root
        && old.assigned == edited.assigned && old.key == edited.key && old.clut == edited.clut && old.w == edited.w
        && old.h == edited.h && old.zone == edited.zone && old.room == edited.room && old.rooms == edited.rooms
        && old.name == edited.name && old.where == edited.where && old.review == edited.review;
}
/* A value edit laid over the catalog in place: the instance's own values, then
 * the entries of it and of every instance below it derived again. False when
 * it cannot be (a structural change, a preview over the id, a removal): the
 * caller rebuilds. */
bool patch_values(const Instance& edited)
{
    CatalogState& cc = catalog_cache;
    auto at = cc.by_id.find(edited.id);
    if (at == cc.by_id.end() || !values_only(cc.instances[at->second], edited)) return false;
    for (const Instance& p : preview_list)
        if (p.id == edited.id) return false;
    if (std::find(pending_removed.begin(), pending_removed.end(), edited.id) != pending_removed.end()) return false;
    cc.instances[at->second] = edited;
    std::vector<size_t> touched;
    for (size_t k = 0; k < cc.instances.size(); k++) {
        const Instance* cur = &cc.instances[k];
        for (size_t depth = 0; depth <= cc.instances.size(); depth++) {
            if (cur->id == edited.id) { touched.push_back(k); break; }
            auto next = cur->parent.empty() ? cc.by_id.end() : cc.by_id.find(cur->parent);
            if (next == cc.by_id.end()) break;
            cur = &cc.instances[next->second];
        }
    }
    std::vector<std::pair<size_t, Entry>> fresh;
    for (size_t k : touched) {
        const Instance& i = cc.instances[k];
        auto e = cc.entry_at.find(i.id);
        if (!i.assigned) continue;
        if (e == cc.entry_at.end()) return false; /* (did not resolve before: the rebuild says why) */
        Entry d;
        if (!derive_entry(cc.instances, cc.by_id, i, d, nullptr)) return false;
        fresh.push_back({ e->second, d });
    }
    for (auto& pair : fresh) cc.entries[pair.first] = pair.second; /* (in place: find_match's pointers stay) */
    ++cc.generation;
    return true;
}
} // namespace

bool Instance::in_scope(int z, int r) const
{
    if (!rooms.empty()) {
        for (const auto& p : rooms)
            if (p.first == z && p.second == r) return true;
        return false;
    }
    return zone < 0 || (zone == z && room == r);
}
const MasterParam* Master::param(const std::string& n) const
{
    for (const MasterParam& p : params)
        if (p.name == n) return &p;
    return nullptr;
}
std::string masters_dir()
{
    /* Beside the shipped catalog (not the shipped maps' layer, which tests
     * point at scratch folders): $ROOD_MATERIAL_MASTERS, else masters/ next
     * to $ROOD_MATERIAL_CATALOG, else pc/assets/materials/masters. */
    const char* e = getenv("ROOD_MATERIAL_MASTERS");
    if (e && *e) return e;
    const char* c = getenv("ROOD_MATERIAL_CATALOG");
    if (c && *c) return fs::path(c).parent_path().string() + "/masters";
    return "pc/assets/materials/masters";
}
void masters_changed()
{
    masters_stamp = -2;
    masters_stamp_last_at = -1;
}
std::string graphs_dir()
{
    /* The user's graph masters (part 4): $ROOD_MATERIAL_GRAPHS, else masters/ in the user maps layer. */
    const char* e = getenv("ROOD_MATERIAL_GRAPHS");
    return e && *e ? e : user_dir() + "/masters";
}
const Master* master(const std::string& name)
{
    const auto& all = current_masters();
    auto it = all.find(name);
    return it == all.end() ? nullptr : &it->second;
}
std::vector<std::string> master_names()
{
    std::vector<std::string> names = { STANDARD };
    for (const auto& pair : current_masters())
        if (pair.first != STANDARD) names.push_back(pair.first);
    return names;
}
std::vector<Instance> instances()
{
    current_catalog();
    return catalog_cache.instances;
}
const Instance* find_instance(const std::string& id)
{
    current_catalog();
    auto it = catalog_cache.by_id.find(id);
    return it == catalog_cache.by_id.end() ? nullptr : &catalog_cache.instances[it->second];
}
bool resolve(const std::string& id, std::map<std::string, float>& out, std::vector<std::string>* chain, std::string* error)
{
    const Instance* i = find_instance(id);
    if (!i) {
        if (error) *error = "unknown instance " + id;
        return false;
    }
    return resolve_in(catalog_cache.instances, catalog_cache.by_id, *i, out, chain, error);
}
bool resolve_instance(const Instance& i, std::map<std::string, float>& out, std::vector<std::string>* chain,
    std::string* error)
{
    current_catalog();
    /* A candidate with the catalog's id is itself: its parents are looked up
     * in the catalog, where its own old copy is reached only through a loop
     * (which the walk refuses at that copy, the id being seen already), so
     * the catalog is walked as it is, without a copy with the candidate in. */
    return resolve_in(catalog_cache.instances, catalog_cache.by_id, i, out, chain, error);
}
const char* const COLOR_PARTS[3] = { ".r", ".g", ".b" };
std::string value_source(const std::string& id, const std::string& name)
{
    std::map<std::string, float> values;
    std::vector<std::string> chain;
    if (!resolve(id, values, &chain, nullptr)) return "";
    for (const std::string& c : chain) {
        const Instance* i = find_instance(c);
        if (i && (i->own.count(name) || i->own.count(name + COLOR_PARTS[0]))) return c; /* (a colour by its .r) */
    }
    return "";
}
Surface surface_of(const std::map<std::string, float>& v)
{
    Surface s;
    auto get = [&](const char* n, float def) { auto it = v.find(n); return it == v.end() ? def : it->second; };
    s.roughness = get("roughness", 1), s.specular = get("specular", 0), s.metal = get("metal", 0);
    s.auto_height = get("auto_height", 0) >= 0.5f, s.bump = get("bump", 1);
    return s;
}
bool entry_of(const Instance& i, Entry& out, std::string* error)
{
    current_catalog();
    std::vector<Instance> list = catalog_cache.instances;
    upsert(list, i);
    std::unordered_map<std::string, size_t> by_id;
    for (size_t k = 0; k < list.size(); k++) by_id[list[k].id] = k;
    return derive_entry(list, by_id, i, out, error);
}
Instance instance_of(const Entry& e)
{
    Instance i;
    i.id = e.id, i.name = e.name, i.where = e.where, i.review = e.review;
    i.parent = e.parent.empty() ? e.preset : e.parent;
    if (e.explicit_fields & 1) i.own["roughness"] = e.value.roughness;
    if (e.explicit_fields & 2) i.own["specular"] = e.value.specular;
    if (e.explicit_fields & 4) i.own["metal"] = e.value.metal;
    if (e.explicit_fields & 8) i.own["auto_height"] = e.value.auto_height ? 1.0f : 0.0f;
    if (e.explicit_fields & 16) i.own["bump"] = e.value.bump;
    i.assigned = true;
    i.key = e.key, i.clut = e.clut, i.w = e.w, i.h = e.h, i.zone = e.zone, i.room = e.room, i.rooms = e.rooms;
    i.user = e.user, i.preview = e.preview;
    /* A pending edit of a root (a preset edited in Details > Material is
     * never an Entry: roots are not assigned), so nothing to keep here. */
    return i;
}
std::vector<std::string> catalog_problems()
{
    current_catalog();
    return catalog_cache.problems;
}
std::vector<Entry> catalog_entries() { return current_catalog(); }
const Entry* find_match(const std::string& key, int w, int h, const std::string& clut, int zone, int room)
{
    /* The last matching entry naming the texture, else the last palette
     * entry (only where no entry names the texture itself). */
    const std::vector<Entry>& entries = current_catalog();
    if (auto found = catalog_cache.by_key.find(key); found != catalog_cache.by_key.end())
        for (auto i = found->second.rbegin(); i != found->second.rend(); ++i) {
            const Entry& e = entries[*i];
            if (e.in_scope(zone, room) && e.w == w && e.h == h && (e.clut.empty() || e.clut == clut)) return &e;
        }
    if (clut.empty()) return nullptr;
    if (auto found = catalog_cache.by_clut.find(clut); found != catalog_cache.by_clut.end())
        for (auto i = found->second.rbegin(); i != found->second.rend(); ++i) {
            const Entry& e = entries[*i];
            if (e.in_scope(zone, room) && (e.w == 0 || (e.w == w && e.h == h))) return &e;
        }
    return nullptr;
}
const Entry* find_match(const std::string& key, int w, int h, const std::vector<std::string>& cluts, int zone,
    int room)
{
    /* As above, a palette being any of `cluts` (an animated palette's
     * variants, the colours now first): the texture's own entries first
     * (with any of them), then the palette entries, variant by variant. */
    if (cluts.size() <= 1) return find_match(key, w, h, cluts.empty() ? std::string() : cluts[0], zone, room);
    const std::vector<Entry>& entries = current_catalog();
    if (auto found = catalog_cache.by_key.find(key); found != catalog_cache.by_key.end())
        for (const std::string& clut : cluts)
            for (auto i = found->second.rbegin(); i != found->second.rend(); ++i) {
                const Entry& e = entries[*i];
                if (e.in_scope(zone, room) && e.w == w && e.h == h && (e.clut.empty() || e.clut == clut)) return &e;
            }
    for (const std::string& clut : cluts)
        if (auto found = catalog_cache.by_clut.find(clut); found != catalog_cache.by_clut.end())
            for (auto i = found->second.rbegin(); i != found->second.rend(); ++i) {
                const Entry& e = entries[*i];
                if (e.in_scope(zone, room) && (e.w == 0 || (e.w == w && e.h == h))) return &e;
            }
    return nullptr;
}
bool catalog_needs_clut(const std::string& key)
{
    return catalog_cache.palette_clut || catalog_cache.clut_keys.count(key) > 0;
}
bool catalog_has_palette_entries()
{
    current_catalog();
    return catalog_cache.palette_entries;
}
Surface match(const std::string& key, int w, int h, const std::string& clut, int zone, int room)
{
    const Entry* e = find_match(key, w, h, clut, zone, room);
    return e ? e->value : Surface();
}
bool catalog_has_size(int w, int h)
{
    if (w < 1 || h < 1 || w > 256 || h > 256) return false;
    return catalog_cache.palette_entries || catalog_cache.sizes.count((w - 1) << 8 | (h - 1)) > 0;
}
uint32_t catalog_generation()
{
    current_catalog();
    return catalog_cache.generation;
}
uint32_t catalog_structure_generation()
{
    current_catalog();
    return catalog_cache.structure;
}
unsigned catalog_file_reads() { return catalog_reads; }
/* The presets: every root instance (a file's `presets`), resolved, in name order. */
std::vector<std::pair<std::string, Surface>> catalog_presets()
{
    current_catalog();
    std::map<std::string, Surface> out;
    for (const Instance& i : catalog_cache.instances) {
        if (!i.root) continue;
        std::map<std::string, float> values;
        if (resolve_in(catalog_cache.instances, catalog_cache.by_id, i, values, nullptr, nullptr))
            out[i.id] = surface_of(values);
    }
    return std::vector<std::pair<std::string, Surface>>(out.begin(), out.end());
}
std::string catalog_user_file() { return catalog_user_path(); }

static double tidy(float v) { return std::round((double)v * 1000.0) / 1000.0; }
/* An instance as a catalog entry (format 1 where it can be: the five
 * standard values at the top level, `preset` the root preset; `parent`,
 * `master`, `params` and a missing `match` are format 2). */
static Json instance_to_json(const Instance& i, bool* format2)
{
    Json j = Json::object();
    j.set("id", Json::str(i.id));
    if (!i.name.empty()) j.set("name", Json::str(i.name));
    if (!i.where.empty()) j.set("where", Json::str(i.where));
    if (i.master != STANDARD) {
        j.set("master", Json::str(i.master));
        if (format2) *format2 = true;
    }
    if (i.assigned) {
        Json m = Json::object();
        if (!i.key.empty()) m.set("key", Json::str(i.key));
        if (i.w > 0) {
            Json size = Json::array();
            size.items.push_back(Json::num(i.w));
            size.items.push_back(Json::num(i.h));
            m.set("size", size);
        }
        if (!i.clut.empty()) m.set("clut", Json::str(i.clut));
        j.set("match", m);
    } else if (format2)
        *format2 = true;
    if (!i.rooms.empty()) {
        Json rooms = Json::array();
        for (const auto& p : i.rooms) {
            Json room = Json::array();
            room.items.push_back(Json::num(p.first));
            room.items.push_back(Json::num(p.second));
            rooms.items.push_back(room);
        }
        Json scope = Json::object();
        scope.set("rooms", rooms);
        j.set("scope", scope);
    } else if (i.zone >= 0) {
        Json room = Json::array();
        room.items.push_back(Json::num(i.zone));
        room.items.push_back(Json::num(i.room));
        Json scope = Json::object();
        scope.set("room", room);
        j.set("scope", scope);
    }
    /* preset: the root preset the chain ends in (old readers); parent: the
     * actual parent when it is not that preset. */
    if (!i.parent.empty()) {
        std::map<std::string, float> values;
        std::vector<std::string> chain;
        std::string root;
        bool resolved = resolve_instance(i, values, &chain, nullptr);
        if (resolved && chain.size() > 1) {
            const Instance* top = find_instance(chain.back());
            if (top && top->root) root = top->id;
        }
        if (!resolved) root = i.parent; /* an unknown parent: written back as it was read (a preset's name) */
        if (!root.empty()) j.set("preset", Json::str(root));
        if (i.parent != root) {
            j.set("parent", Json::str(i.parent));
            if (format2) *format2 = true;
        }
    }
    Json params = Json::object();
    for (const auto& pair : i.own) {
        const std::string& n = pair.first;
        if (n == "roughness" || n == "specular" || n == "metal" || n == "bump") j.set(n.c_str(), Json::num(tidy(pair.second)));
        else if (n == "auto_height") j.set("auto_height", Json::boolean_(pair.second >= 0.5f));
        else {
            const Master* m = master(i.master);
            const MasterParam* p = m ? m->param(n) : nullptr;
            /* A colour's component: the three written once, as [r, g, b] under the colour's name. */
            const MasterParam* c = nullptr;
            if (!p && n.size() > 2 && n[n.size() - 2] == '.' && m) c = m->param(n.substr(0, n.size() - 2));
            if (c && c->is_color) {
                if (n.back() != 'r' && i.own.count(c->name + COLOR_PARTS[0])) continue; /* (written with .r) */
                Json rgb = Json::array();
                for (int k = 0; k < 3; k++) {
                    auto it = i.own.find(c->name + COLOR_PARTS[k]);
                    rgb.items.push_back(Json::num(tidy(it == i.own.end() ? c->color[k] : it->second)));
                }
                params.set(c->name.c_str(), rgb);
                continue;
            }
            params.set(n.c_str(), p && p->is_bool ? Json::boolean_(pair.second >= 0.5f) : Json::num(tidy(pair.second)));
        }
    }
    if (!params.members.empty()) {
        j.set("params", params);
        if (format2) *format2 = true;
    }
    if (!i.review.empty()) j.set("review", Json::str(i.review));
    return j;
}
/* A root instance as a preset (all three standard numbers, as the preset
 * reader requires; the rest only when set). */
static Json preset_to_json(const Instance& i)
{
    Json j = Json::object();
    auto get = [&](const char* n, float def) { auto it = i.own.find(n); return it == i.own.end() ? def : it->second; };
    j.set("roughness", Json::num(tidy(get("roughness", 1))));
    j.set("specular", Json::num(tidy(get("specular", 0))));
    j.set("metal", Json::num(tidy(get("metal", 0))));
    if (i.own.count("auto_height")) j.set("auto_height", Json::boolean_(i.own.at("auto_height") >= 0.5f));
    if (i.own.count("bump")) j.set("bump", Json::num(tidy(i.own.at("bump"))));
    if (i.master != STANDARD) j.set("master", Json::str(i.master));
    return j;
}
std::string instance_json(const Instance& i) { return prefab::json_write(instance_to_json(i, nullptr), 0); }
std::string entry_json(const Entry& e) { return instance_json(instance_of(e)); }

/* The user file read as JSON (an empty catalog when it does not exist yet). */
static bool user_json(Json& j, std::string* error)
{
    j = Json::object();
    ++catalog_reads;
    std::string text = read_text(catalog_user_path());
    if (!text.empty()) {
        if (!prefab::json_parse(text.c_str(), j, error) || j.type != Json::Object) {
            if (error) *error = catalog_user_path() + ": not a catalog (fix or move it first)";
            return false;
        }
    }
    if (!j.get("format")) j.set("format", Json::num(1));
    if (!j.get("entries") || j.get("entries")->type != Json::Array) j.set("entries", Json::array());
    return true;
}
static bool write_json_file(const Json& j, const std::string& path, std::string* error)
{
    std::string temp = path + ".tmp";
    mkdirs(fs::path(path).parent_path().string());
    FILE* f = fopen(temp.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot write " + temp;
        return false;
    }
    std::string text = prefab::json_write(j, 2) + "\n";
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        remove(temp.c_str());
        if (error) *error = "cannot write " + path;
        return false;
    }
    return true;
}
static bool write_user_json(const Json& j, std::string* error)
{
    if (!write_json_file(j, catalog_user_path(), error)) return false;
    file_layer.stale = true; /* read again on the next look, whatever the file's stamp */
    return true;
}
/* Put an instance into a catalog JSON (a root into presets, the rest into
 * entries; the same id replaces). Bumps the format to 2 when needed. */
static bool put_instance_json(Json& j, const Instance& i, std::string* error, bool strict = true)
{
    if (i.root) {
        Json* presets = j.get("presets");
        if (!presets || presets->type != Json::Object) { j.set("presets", Json::object()); presets = j.get("presets"); }
        presets->set(i.id.c_str(), preset_to_json(i));
        if (i.master != STANDARD) j.set("format", Json::num(2));
        return true;
    }
    bool format2 = false;
    Json item = instance_to_json(i, &format2);
    Instance check;
    if (!parse_entry(item, current_masters(), check)) {
        if (error) *error = "the entry is not valid (key, size, preset or fractions)";
        return false;
    }
    std::map<std::string, float> values;
    if (strict && !resolve_instance(i, values, nullptr, error)) return false;
    Json* entries = j.get("entries");
    bool replaced = false;
    for (Json& old : entries->items) {
        const Json* id = old.get("id");
        if (id && id->type == Json::String && id->string == i.id) { old = item; replaced = true; }
    }
    if (!replaced) entries->items.push_back(item);
    if (format2) j.set("format", Json::num(2));
    return true;
}
bool put_user_instance(const Instance& i, std::string* error)
{
    Json j;
    if (!user_json(j, error)) return false;
    if (!put_instance_json(j, i, error)) return false;
    return write_user_json(j, error);
}
bool put_user_entry(const Entry& e, std::string* error) { return put_user_instance(instance_of(e), error); }
bool write_migrated(const std::string& path, int* presets_out, int* entries_out, std::string* error)
{
    Json j = Json::object();
    j.set("format", Json::num(1));
    j.set("presets", Json::object());
    j.set("entries", Json::array());
    int np = 0, ne = 0;
    for (const Instance& i : instances()) {
        if (!i.user || i.pending || i.preview) continue;
        if (!put_instance_json(j, i, error, false)) return false; /* (an unresolved one is kept as read) */
        (i.root ? np : ne)++;
    }
    if (presets_out) *presets_out = np;
    if (entries_out) *entries_out = ne;
    if (j.get("presets")->members.empty()) {
        /* (a user layer without presets stays without the key) */
        Json k = Json::object();
        k.set("format", *j.get("format"));
        k.set("entries", *j.get("entries"));
        j = k;
    }
    return write_json_file(j, path, error);
}
void set_pending_instance(const Instance& e)
{
    Instance copy = e;
    copy.user = true, copy.pending = true, copy.preview = false;
    current_catalog(); /* (up to date first: the patch is over the current state) */
    bool patched = patch_values(copy);
    upsert(pending_list, copy);
    pending_removed.erase(std::remove(pending_removed.begin(), pending_removed.end(), e.id), pending_removed.end());
    ++pending_serial;
    if (patched) applied_pending = pending_serial; /* (the catalog already shows it: no rebuild) */
}
void set_pending(const Entry& e) { set_pending_instance(instance_of(e)); }
void remove_pending(const std::string& id)
{
    pending_list.erase(std::remove_if(pending_list.begin(), pending_list.end(), [&](const Instance& x) { return x.id == id; }),
        pending_list.end());
    if (std::find(pending_removed.begin(), pending_removed.end(), id) == pending_removed.end())
        pending_removed.push_back(id);
    ++pending_serial;
}
std::vector<Instance> pending_instances() { return pending_list; }
std::vector<Entry> pending_entries()
{
    std::vector<Entry> out;
    for (const Instance& i : pending_list) {
        Entry e;
        if (i.assigned && entry_of(i, e, nullptr)) out.push_back(e);
    }
    return out;
}
void set_preview(const std::vector<Entry>& entries)
{
    preview_list.clear();
    for (const Entry& e : entries) {
        Instance i = instance_of(e);
        i.preview = true;
        preview_list.push_back(i);
    }
    ++pending_serial;
}
std::vector<Entry> preview_entries()
{
    std::vector<Entry> out;
    for (const Instance& i : preview_list) {
        Entry e;
        if (entry_of(i, e, nullptr)) out.push_back(e);
    }
    return out;
}
void reload()
{
    forget();
    masters_stamp = -2;
    file_layer.stale = true;
    ++pending_serial; /* (the catalog is read again on the next look) */
}
bool pending_dirty() { return !pending_list.empty() || !pending_removed.empty(); }
void revert_pending()
{
    pending_list.clear();
    pending_removed.clear();
    file_layer.stale = true; /* (Revert reads the files again; Save ends here too) */
    ++pending_serial;
}
bool save_pending(std::string* error)
{
    for (const std::string& id : pending_removed)
        if (!remove_user_entry(id, error)) return false;
    for (const Instance& e : pending_list)
        if (!put_user_instance(e, error)) return false;
    revert_pending();
    return true;
}
bool remove_user_entry(const std::string& id, std::string* error)
{
    Json j;
    if (!user_json(j, error)) return false;
    Json* entries = j.get("entries");
    size_t before = entries->items.size();
    entries->items.erase(std::remove_if(entries->items.begin(), entries->items.end(), [&](const Json& old) {
        const Json* v = old.get("id");
        return v && v->type == Json::String && v->string == id;
    }), entries->items.end());
    bool changed = entries->items.size() != before;
    if (Json* presets = j.get("presets"); presets && presets->type == Json::Object) {
        size_t n = presets->members.size();
        presets->members.erase(std::remove_if(presets->members.begin(), presets->members.end(),
            [&](const std::pair<std::string, Json>& m) { return m.first == id; }), presets->members.end());
        changed |= presets->members.size() != n;
    }
    if (!changed) return true;
    return write_user_json(j, error);
}

} // namespace materials
