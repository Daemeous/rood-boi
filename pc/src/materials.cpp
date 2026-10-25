// Material channel maps (materials.h, pc/docs/Materials.md).
#include "materials.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <system_error>

#include <SDL3/SDL.h>

#include "light_prefabs.h"
#include "pc_paths.h"
#include "platform.h"

namespace fs = std::filesystem;

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
struct Cached {
    long long user_stamp = -1, shipped_stamp = -1;
    bool present = false;
    Channel channel;
};
std::map<std::pair<std::string, std::string>, Cached> cache;
uint32_t files_generation = 1;

std::string channel_path(const std::string& dir, const std::string& key, const std::string& name)
{
    return dir + "/" + key + "/" + name + ".png";
}
void load(const std::string& key, const std::string& name, Cached& c)
{
    std::string user = channel_path(user_dir(), key, name), shipped = channel_path(shipped_dir(), key, name);
    c.user_stamp = stamp(user), c.shipped_stamp = stamp(shipped);
    c.present = false;
    c.channel = Channel();
    for (int layer = 0; layer < 2 && !c.present; layer++) {
        const std::string& path = layer == 0 ? user : shipped;
        if (!(layer == 0 ? c.user_stamp : c.shipped_stamp))
            continue;
        std::string error;
        int w = 0, h = 0;
        if (!read_png_gray(path, &w, &h, c.channel.values, &error)) {
            fprintf(stderr, "[materials] %s\n", error.c_str());
            continue;
        }
        c.channel.w = w, c.channel.h = h, c.channel.path = path, c.channel.shipped = layer == 1;
        c.present = true;
    }
}
} // namespace

const Channel* channel(const std::string& key, const std::string& name)
{
    if (!valid_key(key) || !valid_name(name))
        return nullptr;
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
    for (int layer = 0; layer < 2; layer++) {
        std::error_code ec;
        fs::path dir = fs::path(layer == 0 ? user_dir() : shipped_dir()) / key;
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
                out.push_back({ name, f.path().string(), layer == 0 });
        }
    }
    return out;
}
uint32_t generation() { return files_generation; }
void poll()
{
    for (auto& kv : cache) {
        const std::string &key = kv.first.first, &name = kv.first.second;
        if (stamp(channel_path(user_dir(), key, name)) != kv.second.user_stamp
            || stamp(channel_path(shipped_dir(), key, name)) != kv.second.shipped_stamp) {
            load(key, name, kv.second);
            ++files_generation;
        }
    }
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
 * file replaces shipped entries by id, just as the emissive catalog does. */
namespace {
using prefab::Json;
struct CatalogState {
    std::vector<Entry> entries;
    std::map<std::string, Surface> presets;
    std::map<int, bool> sizes; /* (w - 1) << 8 | (h - 1) */
    long long shipped_stamp = -1, user_stamp = -1;
    uint32_t generation = 0;
};
CatalogState catalog_cache;
std::vector<Entry> pending_list; /* unsaved editor entries (set_pending) */
std::vector<std::string> pending_removed;
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
bool relief_fields(const Json& j, Surface& s)
{
    if (const Json* v = j.get("auto_height")) {
        if (v->type != Json::Bool) return false;
        s.auto_height = v->boolean;
    }
    if (const Json* v = j.get("bump")) {
        if (v->type != Json::Number || !std::isfinite(v->number) || v->number < 0 || v->number > 4) return false;
        s.bump = (float)v->number;
    }
    return true;
}
bool parse_entry(const Json& j, const std::map<std::string, Surface>& presets, Entry& e)
{
    if (j.type != Json::Object) return false;
    e = Entry();
    e.id = string_field(j, "id"), e.name = string_field(j, "name");
    e.where = string_field(j, "where"), e.review = string_field(j, "review");
    e.preset = string_field(j, "preset");
    if (!e.preset.empty()) {
        auto p = presets.find(e.preset);
        if (p == presets.end()) return false;
        e.value = p->second;
    }
    if (const Json* v = j.get("roughness"); v && !number_field(j, "roughness", &e.value.roughness)) return false;
    if (const Json* v = j.get("specular"); v && !number_field(j, "specular", &e.value.specular)) return false;
    if (const Json* v = j.get("metal"); v && !number_field(j, "metal", &e.value.metal)) return false;
    if (!relief_fields(j, e.value)) return false;
    e.explicit_fields = (j.get("roughness") ? 1u : 0u) | (j.get("specular") ? 2u : 0u) | (j.get("metal") ? 4u : 0u)
        | (j.get("auto_height") ? 8u : 0u) | (j.get("bump") ? 16u : 0u);
    const Json* m = j.get("match");
    if (!m || m->type != Json::Object) return false;
    e.key = string_field(*m, "key"), e.clut = string_field(*m, "clut");
    const Json* size = m->get("size");
    if (!size || size->type != Json::Array || size->items.size() != 2
        || size->items[0].type != Json::Number || size->items[1].type != Json::Number) return false;
    if (!std::isfinite(size->items[0].number) || !std::isfinite(size->items[1].number)
        || size->items[0].number < 1 || size->items[0].number > 256
        || size->items[1].number < 1 || size->items[1].number > 256) return false;
    e.w = (int)size->items[0].number, e.h = (int)size->items[1].number;
    if (!valid_key(e.key) || (e.clut.size() && !valid_key(e.clut))
        || size->items[0].number != e.w || size->items[1].number != e.h || e.id.empty()) return false;
    if (const Json* scope = j.get("scope")) {
        const Json* room = scope->type == Json::Object ? scope->get("room") : nullptr;
        if (!room || room->type != Json::Array || room->items.size() != 2
            || room->items[0].type != Json::Number || room->items[1].type != Json::Number) return false;
        if (!std::isfinite(room->items[0].number) || !std::isfinite(room->items[1].number)
            || room->items[0].number < 0 || room->items[0].number > 255
            || room->items[1].number < 0 || room->items[1].number > 255) return false;
        e.zone = (int)room->items[0].number, e.room = (int)room->items[1].number;
        if (e.zone != room->items[0].number || e.room != room->items[1].number) return false;
    }
    return true;
}
void read_catalog(const std::string& path, std::vector<Entry>& entries, std::map<std::string, Surface>& presets,
    bool user)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    std::string text;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f))) text.append(buf, n);
    fclose(f);
    Json j; std::string error;
    if (!prefab::json_parse(text.c_str(), j, &error) || j.type != Json::Object) {
        fprintf(stderr, "[materials] %s: %s\n", path.c_str(), error.c_str()); return;
    }
    if (const Json* p = j.get("presets"); p && p->type == Json::Object)
        for (const auto& pair : p->members) {
            if (pair.second.type != Json::Object) continue;
            Surface s;
            if (number_field(pair.second, "roughness", &s.roughness)
                && number_field(pair.second, "specular", &s.specular)
                && number_field(pair.second, "metal", &s.metal) && relief_fields(pair.second, s))
                presets[pair.first] = s;
        }
    if (const Json* arr = j.get("entries"); arr && arr->type == Json::Array)
        for (const Json& item : arr->items) {
            Entry e;
            if (!parse_entry(item, presets, e)) {
                fprintf(stderr, "[materials] %s: invalid catalog entry\n", path.c_str()); continue;
            }
            e.user = user;
            auto existing = std::find_if(entries.begin(), entries.end(), [&](const Entry& old) { return old.id == e.id; });
            if (existing == entries.end()) entries.push_back(e);
            else *existing = e;
        }
}
const std::vector<Entry>& current_catalog()
{
    const long long a = stamp(catalog_path()), b = stamp(catalog_user_path());
    if (a != catalog_cache.shipped_stamp || b != catalog_cache.user_stamp || applied_pending != pending_serial) {
        applied_pending = pending_serial;
        catalog_cache.shipped_stamp = a, catalog_cache.user_stamp = b;
        catalog_cache.entries.clear();
        catalog_cache.presets.clear();
        catalog_cache.sizes.clear();
        read_catalog(catalog_path(), catalog_cache.entries, catalog_cache.presets, false);
        read_catalog(catalog_user_path(), catalog_cache.entries, catalog_cache.presets, true);
        for (const std::string& id : pending_removed)
            catalog_cache.entries.erase(std::remove_if(catalog_cache.entries.begin(), catalog_cache.entries.end(),
                [&](const Entry& e) { return e.id == id && e.user; }), catalog_cache.entries.end());
        for (const Entry& p : pending_list) {
            auto old = std::find_if(catalog_cache.entries.begin(), catalog_cache.entries.end(),
                [&](const Entry& e) { return e.id == p.id; });
            if (old == catalog_cache.entries.end()) catalog_cache.entries.push_back(p);
            else *old = p;
        }
        for (const Entry& e : catalog_cache.entries)
            catalog_cache.sizes[(e.w - 1) << 8 | (e.h - 1)] = true;
        ++catalog_cache.generation;
    }
    return catalog_cache.entries;
}
} // namespace
static std::map<std::string, Surface> catalog_presets_map()
{
    current_catalog();
    return catalog_cache.presets;
}
std::vector<Entry> catalog_entries() { return current_catalog(); }
const Entry* find_match(const std::string& key, int w, int h, const std::string& clut, int zone, int room)
{
    const Entry* found = nullptr;
    for (const Entry& e : current_catalog())
        if (e.key == key && e.w == w && e.h == h && (e.clut.empty() || e.clut == clut)
            && (e.zone < 0 || (e.zone == zone && e.room == room))) found = &e;
    return found;
}
Surface match(const std::string& key, int w, int h, const std::string& clut, int zone, int room)
{
    const Entry* e = find_match(key, w, h, clut, zone, room);
    return e ? e->value : Surface();
}
bool catalog_has_size(int w, int h)
{
    if (w < 1 || h < 1 || w > 256 || h > 256) return false;
    return catalog_cache.sizes.count((w - 1) << 8 | (h - 1)) > 0;
}
uint32_t catalog_generation()
{
    current_catalog();
    return catalog_cache.generation;
}
std::vector<std::pair<std::string, Surface>> catalog_presets()
{
    current_catalog();
    return std::vector<std::pair<std::string, Surface>>(catalog_cache.presets.begin(), catalog_cache.presets.end());
}
std::string catalog_user_file() { return catalog_user_path(); }

static double tidy(float v) { return std::round((double)v * 1000.0) / 1000.0; }
static Json entry_to_json(const Entry& e)
{
    Json j = Json::object();
    j.set("id", Json::str(e.id));
    if (!e.name.empty()) j.set("name", Json::str(e.name));
    if (!e.where.empty()) j.set("where", Json::str(e.where));
    Json m = Json::object();
    m.set("key", Json::str(e.key));
    Json size = Json::array();
    size.items.push_back(Json::num(e.w));
    size.items.push_back(Json::num(e.h));
    m.set("size", size);
    if (!e.clut.empty()) m.set("clut", Json::str(e.clut));
    j.set("match", m);
    if (e.zone >= 0) {
        Json room = Json::array();
        room.items.push_back(Json::num(e.zone));
        room.items.push_back(Json::num(e.room));
        Json scope = Json::object();
        scope.set("room", room);
        j.set("scope", scope);
    }
    if (!e.preset.empty()) j.set("preset", Json::str(e.preset));
    if (e.explicit_fields & 1) j.set("roughness", Json::num(tidy(e.value.roughness)));
    if (e.explicit_fields & 2) j.set("specular", Json::num(tidy(e.value.specular)));
    if (e.explicit_fields & 4) j.set("metal", Json::num(tidy(e.value.metal)));
    if (e.explicit_fields & 8) j.set("auto_height", Json::boolean_(e.value.auto_height));
    if (e.explicit_fields & 16) j.set("bump", Json::num(tidy(e.value.bump)));
    if (!e.review.empty()) j.set("review", Json::str(e.review));
    return j;
}
std::string entry_json(const Entry& e) { return prefab::json_write(entry_to_json(e), 0); }

/* The user file read as JSON (an empty catalog when it does not exist yet). */
static bool user_json(Json& j, std::string* error)
{
    j = Json::object();
    FILE* f = fopen(catalog_user_path().c_str(), "rb");
    if (f) {
        std::string text;
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, f))) text.append(buf, n);
        fclose(f);
        if (!prefab::json_parse(text.c_str(), j, error) || j.type != Json::Object) {
            if (error) *error = catalog_user_path() + ": not a catalog (fix or move it first)";
            return false;
        }
    }
    if (!j.get("format")) j.set("format", Json::num(1));
    if (!j.get("entries") || j.get("entries")->type != Json::Array) j.set("entries", Json::array());
    return true;
}
static bool write_user_json(const Json& j, std::string* error)
{
    std::string path = catalog_user_path(), temp = path + ".tmp";
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
    catalog_cache.user_stamp = -2; /* read again on the next look */
    return true;
}
bool put_user_entry(const Entry& e, std::string* error)
{
    std::map<std::string, Surface> presets = catalog_presets_map();
    Json j;
    if (!user_json(j, error)) return false;
    Json item = entry_to_json(e), *entries = j.get("entries");
    Entry check;
    if (!parse_entry(item, presets, check)) {
        if (error) *error = "the entry is not valid (key, size, preset or fractions)";
        return false;
    }
    bool replaced = false;
    for (Json& old : entries->items) {
        const Json* id = old.get("id");
        if (id && id->type == Json::String && id->string == e.id) { old = item; replaced = true; }
    }
    if (!replaced) entries->items.push_back(item);
    return write_user_json(j, error);
}
void set_pending(const Entry& e)
{
    Entry copy = e;
    copy.user = true;
    auto old = std::find_if(pending_list.begin(), pending_list.end(), [&](const Entry& x) { return x.id == e.id; });
    if (old == pending_list.end()) pending_list.push_back(copy);
    else *old = copy;
    pending_removed.erase(std::remove(pending_removed.begin(), pending_removed.end(), e.id), pending_removed.end());
    ++pending_serial;
}
void remove_pending(const std::string& id)
{
    pending_list.erase(std::remove_if(pending_list.begin(), pending_list.end(), [&](const Entry& x) { return x.id == id; }),
        pending_list.end());
    if (std::find(pending_removed.begin(), pending_removed.end(), id) == pending_removed.end())
        pending_removed.push_back(id);
    ++pending_serial;
}
std::vector<Entry> pending_entries() { return pending_list; }
void reload()
{
    forget();
    ++pending_serial; /* (the catalog is read again on the next look) */
}
bool pending_dirty() { return !pending_list.empty() || !pending_removed.empty(); }
void revert_pending()
{
    pending_list.clear();
    pending_removed.clear();
    ++pending_serial;
}
bool save_pending(std::string* error)
{
    for (const std::string& id : pending_removed)
        if (!remove_user_entry(id, error)) return false;
    for (const Entry& e : pending_list)
        if (!put_user_entry(e, error)) return false;
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
    if (entries->items.size() == before) return true;
    return write_user_json(j, error);
}

} // namespace materials
