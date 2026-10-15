// Material channel maps (materials.h, pc/docs/Materials.md).
#include "materials.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>

#include <SDL3/SDL.h>

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
bool known_channel(const std::string& name) { return name == "emissive"; }

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
        std::vector<uint8_t> empty((size_t)w * h, 0);
        if (!write_channel(key, channel_name, w, h, empty.data(), error))
            return false;
        if (written)
            written->push_back(user);
    }
    return true;
}

} // namespace materials
