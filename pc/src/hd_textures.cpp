#include "hd_textures.h"
#include "gl_api.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
#define XXH_INLINE_ALL
#include "../third_party/texture_formats/xxhash.h"
#include "hd_dds.h"
#ifdef ROOD_VULKAN
#include "vk_core.h"
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_OUT_OF_MEMORY
#define GL_OUT_OF_MEMORY 0x0505
#endif

/* A word retains its original upload coordinates even after a VRAM copy.
 * The two CRCs are the pack's on-disk IDs, not VRAM addresses. No game bytes
 * or save data are replaced. Thread: game/render thread, except the image
 * decoding workers (decode_file), which share only the Loader queues. */
extern "C" {
int pc_hd_enabled = 1;
}
namespace
{
int last_enabled = 1;
int side = 8192; /* one atlas layer: 256 MiB of RGBA */
constexpr int slots = 4096;
struct Word {
    uint32_t hash, offset, width, height;
};
struct Image {
    int x, y, w, h, layer;
};
using Pixels = std::shared_ptr<const std::vector<uint8_t>>;
struct Pending {
    Image rect;
    Pixels pixels;
};
std::vector<Word> words;
std::array<uint32_t, 64 * 1024> palettes{};
std::array<uint32_t, slots * 8> table{};
std::map<uint64_t, std::string> files, page_files;
struct Upload {
    int w, h;
    std::vector<uint16_t> pixels;
    uint64_t hash;
};
std::map<uint32_t, Upload> uploads;
struct Duck {
    std::string path;
    bool page, semi;
    unsigned depth;
    uint64_t hash, palette;
    unsigned sw, sh, x, y, w, h, lo, hi;
};
std::vector<Duck> duck;
std::set<std::string> duck_names;
std::map<uint64_t, std::vector<size_t>> duck_upload_index;
std::map<std::string, Image> page_resident;
unsigned indexed_count;
bool parse_duck(const std::string &name, const std::string &path)
{
    static const std::regex pattern(
        R"((texupload|texpage)-(STP4|STP8|STC16|P4|P8|C16)-([0-9A-Fa-f]{16})-(?:([0-9A-Fa-f]{16})-)?([0-9]+)x([0-9]+)-([0-9]+)-([0-9]+)-([0-9]+)x([0-9]+)(?:-P([0-9]+)-([0-9]+))?)");
    std::smatch m;
    if (!std::regex_match(name, m, pattern))
        return false;
    try {
        Duck d{};
        d.path = path;
        d.page = m[1] == "texpage";
        std::string mode = m[2];
        d.semi = mode[0] == 'S';
        d.depth = mode.back() == '4' ? 0 : mode.back() == '8' ? 1 : 2;
        d.hash = std::stoull(m[3], nullptr, 16);
        d.palette = m[4].matched ? std::stoull(m[4], nullptr, 16) : 0;
        d.sw = std::stoul(m[5]);
        d.sh = std::stoul(m[6]);
        d.x = std::stoul(m[7]);
        d.y = std::stoul(m[8]);
        d.w = std::stoul(m[9]);
        d.h = std::stoul(m[10]);
        d.lo = m[11].matched ? std::stoul(m[11]) : 0;
        d.hi = m[12].matched ? std::stoul(m[12]) : 0;
        unsigned ppw = 4 >> d.depth;
        if (!d.sw || !d.sh || d.sw > 1024 || d.sh > 512 || !d.w || !d.h || d.x + d.w > d.sw * ppw ||
            d.y + d.h > d.sh || d.lo > d.hi ||
            (d.depth < 2 && (!m[4].matched || !m[11].matched || d.hi >= (d.depth ? 256u : 16u))) ||
            (d.depth == 2 && m[4].matched))
            return false;
        if (!duck_names.insert(name).second)
            return false;
        duck.push_back(d);
        return true;
    } catch (...) {
        return false;
    }
}
bool parse_pair(const std::string &name, uint64_t &key)
{
    auto dash = name.find('-');
    if (dash == std::string::npos || dash < 1 || dash > 8 || name.size() <= dash + 1 ||
        name.size() - dash - 1 > 8)
        return false;
    auto a = name.substr(0, dash), b = name.substr(dash + 1);
    if (a.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos ||
        b.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    key = (uint64_t(strtoul(a.c_str(), nullptr, 16)) << 32) | strtoul(b.c_str(), nullptr, 16);
    return true;
}
void index_folder(const std::filesystem::path &dir, bool pages)
{
    std::error_code ec;
    std::vector<std::filesystem::path> paths;
    for (auto it = std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() > 5) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec) &&
            (it->path().filename() == "dumps" || it->path().filename() == "texture-dump"))
            it.disable_recursion_pending();
        if (it->is_regular_file(ec))
            paths.push_back(it->path());
    }
    // PNG wins over DDS when both represent the same ID.
    std::sort(paths.begin(), paths.end(), [](const auto &a, const auto &b) {
        bool ap = a.extension() == ".png", bp = b.extension() == ".png";
        return ap != bp ? ap > bp : a < b;
    });
    for (const auto &path : paths) {
        std::string ext = path.extension().string();
        for (char &c : ext)
            c = std::tolower((unsigned char)c);
        if (ext != ".png" && ext != ".dds")
            continue;
        std::string name = path.stem().string();
        uint64_t key;
        if (parse_pair(name, key)) {
            bool page = pages;
            for (const auto &part : path.parent_path())
                if (part.string().find("texture-replacements-pages") != std::string::npos)
                    page = true;
            if ((page ? page_files : files).emplace(key, path.string()).second)
                ++indexed_count;
        } else if (parse_duck(name, path.string()))
            ++indexed_count;
    }
    // Simple YAML Aliases entries (inline or literal block), as used by
    // DuckStation.
    for (const auto &path : paths)
        if (path.filename() == "config.yaml") {
            std::ifstream input(path);
            std::string line, pending_name;
            bool aliases = false;
            while (std::getline(input, line)) {
                if (line.find("Aliases:") == 0) {
                    aliases = true;
                    continue;
                }
                if (!aliases)
                    continue;
                if (!line.empty() && !std::isspace((unsigned char)line[0]) && line[0] != '#')
                    break;
                auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos || line[first] == '#')
                    continue;
                auto colon = line.find(':', first);
                std::string name, value;
                if (colon != std::string::npos) {
                    name = line.substr(first, colon - first);
                    value = line.substr(colon + 1);
                } else if (!pending_name.empty()) {
                    name = pending_name;
                    value = line.substr(first);
                    pending_name.clear();
                } else
                    continue;
                auto begin = value.find_first_not_of(" \t\r");
                if (begin == std::string::npos)
                    continue;
                value = value.substr(begin);
                while (!value.empty() && std::isspace((unsigned char)value.back()))
                    value.pop_back();
                if (value == "|") {
                    pending_name = name;
                    continue;
                }
                if (value.size() > 1 && (value.front() == '\"' || value.front() == '\''))
                    value = value.substr(1, value.size() - 2);
                auto target = (path.parent_path() / "replacements" / value).lexically_normal();
                auto relative = target.lexically_relative(path.parent_path());
                if (relative.empty() || *relative.begin() == "..")
                    continue;
                if (parse_duck(name, target.string()))
                    ++indexed_count;
            }
        }
}
std::map<uint64_t, Image> resident;
std::set<uint64_t> bad;
struct Use {
    uint64_t generation;
    uint32_t layers; /* atlas layers its replacements live on */
};
std::map<unsigned, Use> used;
std::vector<Pending> pending;
uint64_t generation = 1;
bool initialized, configured, dirty, full;
bool words_dirty = true, palettes_dirty = true, table_dirty = true;
int words_y0 = 0, words_y1 = 512; /* rows of the word map to send to the GPU */
unsigned matched_uploads, loaded_total, evictions;
unsigned gl_tex[4];
int gl_layers;
#ifdef ROOD_VULKAN
vk_image vk_tex[4];
int vk_layers;
#endif
extern "C" const uint16_t *pc_native_vram(void);

/* The atlas is a stack of side x side layers in VRAM, added as they are
 * needed up to ROOD_HD_VRAM_MB (default 2048: eight 256 MiB layers). When
 * every layer is full, the layer drawn from least recently is cleared and
 * reused at a game-frame boundary; images still on the other layers stay.
 * A layer drawn from in the last two frames is never cleared: if a scene
 * needs more than the budget, the excess keeps its original textures. */
struct AtlasLayer {
    int cursor_x, cursor_y, row_height;
    uint64_t last_used;
};
std::vector<AtlasLayer> atlas;
int current_layer = -1, max_layers = 8;
uint32_t frame_layers; /* layers drawn from this frame */
uint64_t frame_number = 1;
unsigned cooldown; /* frames before the next try when every layer is in use */
void touch(int layer)
{
    if (layer < 0 || layer >= int(atlas.size()))
        return;
    frame_layers |= 1u << layer;
    atlas[layer].last_used = frame_number;
}

/* Decoded images (RGBA rows, no padding), shared by the RAM cache and the
 * pending uploads. Decoding happens once per file while it stays cached. */
struct Decoded {
    int w = 0, h = 0;
    Pixels pixels;
};
Pixels to_rgba(SDL_Surface *original)
{
    SDL_Surface *rgba = SDL_ConvertSurface(original, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(original);
    if (!rgba)
        return nullptr;
    auto out = std::make_shared<std::vector<uint8_t>>(size_t(rgba->w) * rgba->h * 4);
    for (int y = 0; y < rgba->h; ++y)
        memcpy(out->data() + size_t(y) * rgba->w * 4, (uint8_t *)rgba->pixels + size_t(y) * rgba->pitch,
               size_t(rgba->w) * 4);
    SDL_DestroySurface(rgba);
    return out;
}
/* Any thread: reads and decodes one replacement file. */
Decoded decode_file(const std::string &path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    for (char &c : ext)
        c = std::tolower((unsigned char)c);
    SDL_Surface *s = ext == ".dds" ? hd_load_dds(path.c_str()) : SDL_LoadPNG(path.c_str());
    Decoded d;
    if (!s)
        return d;
    d.w = s->w;
    d.h = s->h;
    d.pixels = to_rgba(s);
    return d;
}

/* RAM tier (ROOD_HD_RAM_MB, default 256; 0 keeps none): what refills a
 * cleared layer without the disk. Least recently used first out. */
struct Cached {
    Decoded image;
    uint64_t last_used;
};
std::map<std::string, Cached> ram;
size_t ram_bytes, ram_budget = size_t(256) << 20;
const Decoded *ram_get(const std::string &path)
{
    auto it = ram.find(path);
    if (it == ram.end())
        return nullptr;
    it->second.last_used = frame_number;
    return &it->second.image;
}
void ram_put(const std::string &path, const Decoded &d)
{
    size_t bytes = d.pixels ? d.pixels->size() : 0;
    if (!bytes || bytes > ram_budget || ram.count(path))
        return;
    ram[path] = {d, frame_number};
    ram_bytes += bytes;
    while (ram_bytes > ram_budget) {
        auto oldest = ram.end();
        for (auto it = ram.begin(); it != ram.end(); ++it)
            if (it->first != path && (oldest == ram.end() || it->second.last_used < oldest->second.last_used))
                oldest = it;
        if (oldest == ram.end())
            break;
        ram_bytes -= oldest->second.image.pixels->size();
        ram.erase(oldest);
    }
}
Decoded decode_cached(const std::string &path)
{
    if (const Decoded *d = ram_get(path))
        return *d;
    Decoded d = decode_file(path);
    ram_put(path, d);
    return d;
}

/* Disk reads and PNG decodes run on two worker threads, so a frame never
 * waits for them: a replacement seen for the first time keeps its original
 * texture until its image arrives (usually the next frame). ROOD_HD_SYNC_LOAD=1
 * decodes on the game thread instead (the unit probe does too). */
struct Loader {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::pair<uint64_t, std::string>> requests;
    std::vector<std::tuple<uint64_t, std::string, Decoded>> done;
};
Loader *loader; /* never freed: the detached workers may outlive static destructors at exit */
bool async_loads = true;
std::set<uint64_t> requested;
void request(uint64_t key, const std::string &path)
{
    if (!requested.insert(key).second)
        return;
    if (!loader) {
        loader = new Loader;
        for (int i = 0; i < 2; ++i)
            std::thread([] {
                for (;;) {
                    std::pair<uint64_t, std::string> job;
                    {
                        std::unique_lock<std::mutex> lock(loader->mutex);
                        loader->wake.wait(lock, [] { return !loader->requests.empty(); });
                        job = std::move(loader->requests.front());
                        loader->requests.pop_front();
                    }
                    Decoded d = decode_file(job.second);
                    std::lock_guard<std::mutex> lock(loader->mutex);
                    loader->done.emplace_back(job.first, std::move(job.second), std::move(d));
                }
            }).detach();
    }
    std::lock_guard<std::mutex> lock(loader->mutex);
    loader->requests.emplace_back(key, path);
    loader->wake.notify_one();
}

uint32_t crc(const uint16_t *data, size_t n, uint32_t c = 0xffffffffu)
{
    static uint32_t lut[256];
    if (!lut[1])
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int b = 0; b < 8; ++b)
                v = (v >> 1) ^ ((v & 1) ? 0xedb88320u : 0);
            lut[i] = v;
        }
    for (size_t i = 0; i < n; ++i) {
        /* Explicit little endian, matching PS1 upload bytes. */
        c = lut[(c ^ data[i]) & 255] ^ (c >> 8);
        c = lut[(c ^ (data[i] >> 8)) & 255] ^ (c >> 8);
    }
    return c;
}
uint32_t bucket(uint32_t a, uint32_t b) { return (a * 0x9e3779b1u ^ b * 0x85ebca6bu) & (slots - 1); }
int env_mb(const char *name, int fallback)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : fallback;
}
void init()
{
    if (initialized)
        return;
    initialized = true;
    const char *dir = getenv("ROOD_TEXTURE_PACK");
    if (dir && *dir)
        index_folder(dir, false);
    else {
        index_folder("texture-replacements", false);
        index_folder("textures/SLUS-01040/replacements", false);
    }
    const char *pages = getenv("ROOD_TEXTURE_PACK_PAGES");
    index_folder(pages && *pages ? pages : "texture-replacements-pages", true);
    for (size_t i = 0; i < duck.size(); ++i)
        if (!duck[i].page)
            duck_upload_index[duck[i].hash].push_back(i);
    configured = indexed_count != 0;
    if (configured)
        words.resize(1024 * 512);
    int layer_mb = int((uint64_t(side) * side * 4) >> 20);
    max_layers = std::clamp(env_mb("ROOD_HD_VRAM_MB", 2048) / std::max(layer_mb, 1), 1, 32);
    ram_budget = size_t(std::max(env_mb("ROOD_HD_RAM_MB", 256), 0)) << 20;
    if (getenv("ROOD_HD_SYNC_LOAD") && atoi(getenv("ROOD_HD_SYNC_LOAD")))
        async_loads = false;
    fprintf(stderr, "[hd] %u replacements indexed (%zu classic, %zu page, %zu DuckStation)\n", indexed_count,
            files.size(), page_files.size(), duck.size());
    if (configured)
        fprintf(stderr, "[hd] up to %d atlas layers (%d MB of VRAM), %zu MB RAM cache, %s loading\n", max_layers,
                max_layers * layer_mb, ram_budget >> 20, async_loads ? "background" : "synchronous");
}
void insert(uint64_t key, Image im, unsigned kind = 0, bool valid = true)
{
    uint32_t a = key >> 32, b = uint32_t(key);
    uint32_t where = uint32_t(im.y) | (uint32_t(im.layer) << 16);
    for (int n = 0, i = bucket(a, b); n < slots; ++n, i = (i + 1) & (slots - 1)) {
        uint32_t *t = table.data() + i * 4;
        uint32_t *d = t + slots * 4;
        if (!d[0] || (t[0] == a && t[1] == b && d[2] == kind)) {
            if (d[0] && t[2] == uint32_t(im.x) && t[3] == where && d[0] == uint32_t(im.w) &&
                d[1] == uint32_t(im.h) && d[3] == uint32_t(valid))
                return; /* already there */
            t[0] = a;
            t[1] = b;
            t[2] = im.x;
            t[3] = where;
            d[0] = im.w;
            d[1] = im.h;
            d[2] = kind;
            d[3] = valid;
            dirty = table_dirty = true;
            return;
        }
    }
}
bool fits(const AtlasLayer &l, int w, int h)
{
    int y = l.cursor_x + w > side ? l.cursor_y + l.row_height : l.cursor_y;
    return y + h <= side;
}
bool place(int w, int h, Pixels pixels, const char *label, Image &result)
{
    if (!pixels || w <= 0 || h <= 0 || w > side || h > side) {
        fprintf(stderr, "[hd] unsupported image size %dx%d: %s\n", w, h, label);
        return false;
    }
    if (resident.size() + page_resident.size() >= slots / 2 ||
        ((current_layer < 0 || !fits(atlas[current_layer], w, h)) && int(atlas.size()) >= max_layers)) {
        if (!full)
            fprintf(stderr, "[hd] atlas full (%zu images on %zu layers); reusing the least recently used layer\n",
                    resident.size() + page_resident.size(), atlas.size());
        full = true;
        return false;
    }
    if (current_layer < 0 || !fits(atlas[current_layer], w, h)) {
        atlas.push_back({});
        current_layer = int(atlas.size()) - 1;
        if (atlas.size() > 1)
            fprintf(stderr, "[hd] atlas layer %d of %d\n", current_layer + 1, max_layers);
    }
    AtlasLayer &l = atlas[current_layer];
    if (l.cursor_x + w > side) {
        l.cursor_x = 0;
        l.cursor_y += l.row_height;
        l.row_height = 0;
    }
    result = {l.cursor_x, l.cursor_y, w, h, current_layer};
    l.cursor_x += w;
    l.row_height = std::max(l.row_height, h);
    touch(current_layer);
    ++loaded_total;
    pending.push_back({result, std::move(pixels)});
    if (loaded_total <= 8)
        fprintf(stderr, "[hd] loaded %s (%dx%d)\n", label, w, h);
    return true;
}
void place_resident(uint64_t key, const Decoded &d, const std::string &path)
{
    Image im;
    if (!place(d.w, d.h, d.pixels, path.c_str(), im)) {
        if (!full)
            bad.insert(key);
        return;
    }
    resident.emplace(key, im);
    insert(key, im);
}
void load(uint64_t key)
{
    auto r = resident.find(key);
    if (r != resident.end()) {
        insert(key, r->second);
        touch(r->second.layer);
        return;
    }
    if (bad.count(key) || full)
        return;
    auto f = files.find(key);
    if (f == files.end())
        return;
    if (const Decoded *cached = ram_get(f->second)) {
        place_resident(key, *cached, f->second);
        return;
    }
    if (async_loads) {
        request(key, f->second);
        return;
    }
    Decoded d = decode_file(f->second);
    if (!d.pixels) {
        fprintf(stderr, "[hd] cannot read %s: %s\n", f->second.c_str(), SDL_GetError());
        bad.insert(key);
        return;
    }
    ram_put(f->second, d);
    place_resident(key, d, f->second);
}
/* Layers at and past `first` are gone (a GPU allocation failed). */
void drop_layers_from(int first)
{
    for (auto it = resident.begin(); it != resident.end();)
        it = it->second.layer >= first ? resident.erase(it) : std::next(it);
    for (auto it = page_resident.begin(); it != page_resident.end();)
        it = it->second.layer >= first ? page_resident.erase(it) : std::next(it);
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [first](const Pending &p) { return p.rect.layer >= first; }),
                  pending.end());
    if (int(atlas.size()) > first)
        atlas.resize(first);
    max_layers = std::max(first, 1);
    if (atlas.empty())
        current_layer = -1;
    else
        current_layer = std::min(current_layer, int(atlas.size()) - 1);
    table.fill(0);
    used.clear();
    dirty = table_dirty = true;
}
/* The atlas lost its contents (grown without a GPU copy): start empty;
 * images are placed again from the RAM cache or disk as they are drawn. */
void forget_uploaded()
{
    resident.clear();
    page_resident.clear();
    pending.clear();
    atlas.clear();
    current_layer = -1;
    table.fill(0);
    used.clear();
    dirty = table_dirty = true;
}
void clear_layer(int layer)
{
    for (auto it = resident.begin(); it != resident.end();)
        it = it->second.layer == layer ? resident.erase(it) : std::next(it);
    for (auto it = page_resident.begin(); it != page_resident.end();)
        it = it->second.layer == layer ? page_resident.erase(it) : std::next(it);
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [layer](const Pending &p) { return p.rect.layer == layer; }),
                  pending.end());
    atlas[layer] = {};
    atlas[layer].last_used = frame_number;
    current_layer = layer;
    /* The table is rebuilt from what stays resident as pages are drawn. */
    table.fill(0);
    used.clear();
    dirty = table_dirty = true;
    ++evictions;
}
void mark_rows(int y, int h)
{
    if (h <= 0)
        return;
    if (h >= 512 || (y & 511) + h > 512) {
        words_y0 = 0;
        words_y1 = 512;
    } else {
        words_y0 = std::min(words_y0, y & 511);
        words_y1 = std::max(words_y1, (y & 511) + h);
    }
    dirty = words_dirty = true;
}

uint32_t reduced_palette(const std::vector<uint16_t> &pixels, unsigned depth, const uint16_t *colors)
{
    unsigned lo = depth ? 255 : 15, hi = 0, mask = depth ? 255 : 15, step = depth ? 8 : 4;
    for (uint16_t word : pixels)
        for (unsigned shift = 0; shift < 16; shift += step) {
            unsigned v = (word >> shift) & mask;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    return crc(colors + lo, hi - lo + 1) ^ 0xffffffffu;
}
struct Layer {
    const Duck *file;
    int x, y;
    uint32_t upload;
};
void resolve_page(unsigned tpage, unsigned clut, unsigned depth, unsigned ppw, unsigned px, unsigned py,
                  const uint16_t *colors, uint32_t pal)
{
    if (page_files.empty() && duck.empty())
        return;
    const uint16_t *vr = pc_native_vram();
    unsigned pw = 256 / ppw;
    std::vector<Word> origins;
    origins.reserve(pw * 256);
    std::vector<uint16_t> page;
    page.reserve(pw * 256);
    for (unsigned y = 0; y < 256; ++y)
        for (unsigned x = 0; x < pw; ++x) {
            page.push_back(vr[(py + y) * 1024 + ((px + x) & 1023)]);
            origins.push_back(words[(py + y) * 1024 + ((px + x) & 1023)]);
        }
    uint32_t hash = crc(page.data(), page.size()) ^ 0xffffffffu;
    uint64_t binding = (uint64_t(tpage) << 32) | pal;
    uint32_t effective = depth < 2 ? reduced_palette(page, depth, colors) : pal;
    auto beetle = page_files.find((uint64_t(hash) << 32) | effective);
    if (beetle == page_files.end())
        beetle = page_files.find((uint64_t(hash) << 32) | pal);
    struct Origin {
        uint32_t crc;
        int x, y;
        const Upload *upload;
    };
    std::vector<Origin> sources;
    std::set<uint32_t> seen;
    if (!duck_upload_index.empty())
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned x = 0; x < pw; ++x) {
                const Word &word = origins[y * pw + x];
                if (!word.width || !seen.insert(word.hash).second)
                    continue;
                auto it = uploads.find(word.hash);
                if (it == uploads.end())
                    continue;
                sources.push_back({word.hash, int(x * ppw) - int((word.offset & 65535) * ppw),
                                   int(y) - int(word.offset >> 16), &it->second});
            }
    std::vector<Layer> layers;
    for (const auto &d : duck) {
        if (d.depth != depth)
            continue;
        if (depth < 2 && (((clut & 63) * 16 + d.hi >= 1024) ||
                          XXH3_64bits(colors + d.lo, (d.hi - d.lo + 1) * 2) != d.palette))
            continue;
        if (d.page) {
            if (d.sw != pw || d.sh != 256 || d.x + d.w > 256 || d.y + d.h > 256)
                continue;
            std::vector<uint16_t> rect;
            unsigned left = d.x / ppw, right = (d.x + d.w) / ppw;
            if (left == right)
                continue;
            for (unsigned y = d.y; y < d.y + d.h; ++y)
                rect.insert(rect.end(), page.begin() + y * pw + left, page.begin() + y * pw + right);
            if (XXH3_64bits(rect.data(), rect.size() * 2) == d.hash)
                layers.push_back({&d, int(d.x), int(d.y), 0});
        } else {
            for (const auto &source : sources) {
                if (source.upload->hash != d.hash || source.upload->w != int(d.sw) ||
                    source.upload->h != int(d.sh))
                    continue;
                layers.push_back({&d, source.x + int(d.x), source.y + int(d.y), source.crc});
            }
        }
    }
    // Prefer semi-transparent dumps when an author supplied both draw modes.
    std::stable_sort(layers.begin(), layers.end(),
                     [](const Layer &a, const Layer &b) { return a.file->semi < b.file->semi; });
    if (beetle == page_files.end() && layers.empty()) {
        insert(binding, {0, 0, 1, 1, 0}, 1, false);
        return;
    }
    std::ostringstream name;
    name << depth;
    bool uses_uploads = false;
    for (const auto &layer : layers)
        uses_uploads |= layer.upload != 0;
    if (uses_uploads)
        name << ':' << XXH3_64bits(origins.data(), origins.size() * sizeof(Word));
    if (beetle != page_files.end())
        name << ':' << beetle->second;
    for (const auto &layer : layers)
        name << ':' << layer.file->path << ':' << layer.x << ':' << layer.y << ':' << layer.upload;
    auto cached = page_resident.find(name.str());
    if (cached != page_resident.end()) {
        insert(binding, cached->second, 1);
        touch(cached->second.layer);
        return;
    }
    if (full) {
        insert(binding, {0, 0, 1, 1, 0}, 1, false);
        return;
    }
    struct Source {
        Layer layer;
        Decoded image;
    };
    std::vector<Source> decoded;
    unsigned scale_x = 1, scale_y = 1;
    Decoded base;
    if (beetle != page_files.end())
        base = decode_cached(beetle->second);
    if (base.pixels) {
        scale_x = std::max(1, (base.w + 255) / 256);
        scale_y = std::max(1, (base.h + 255) / 256);
    }
    for (const auto &layer : layers) {
        Decoded image = decode_cached(layer.file->path);
        if (!image.pixels)
            continue;
        scale_x = std::max(scale_x, unsigned((image.w + layer.file->w - 1) / layer.file->w));
        scale_y = std::max(scale_y, unsigned((image.h + layer.file->h - 1) / layer.file->h));
        decoded.push_back({layer, image});
    }
    if (!base.pixels && decoded.empty()) {
        insert(binding, {0, 0, 1, 1, 0}, 1, false);
        return;
    }
    // Bound temporary composition as well as the GPU atlas. Arbitrary scales use
    // nearest sampling.
    scale_x = std::min(scale_x, 32u);
    scale_y = std::min(scale_y, 32u);
    unsigned w = 256 * scale_x, h = 256 * scale_y;
    auto out = std::make_shared<std::vector<uint8_t>>(size_t(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            uint8_t *dst = out->data() + (size_t(y) * w + x) * 4;
            if (base.pixels) {
                memcpy(dst,
                       base.pixels->data() + ((uint64_t(y) * base.h / h) * base.w + uint64_t(x) * base.w / w) * 4,
                       4);
                if (dst[3] && dst[3] < 255)
                    dst[3] = 128;
            } else {
                // Alpha 1 is an internal uncovered-page marker; replacement semi-alpha
                // is normalized to 128.
                dst[0] = dst[1] = dst[2] = 0;
                dst[3] = 1;
            }
        }
    for (auto &d : decoded) {
        const auto &layer = d.layer;
        const Duck &file = *layer.file;
        int x0 = std::max(0, layer.x), y0 = std::max(0, layer.y);
        int x1 = std::min(256, layer.x + int(file.w)), y1 = std::min(256, layer.y + int(file.h));
        for (int y = y0 * int(scale_y); y < y1 * int(scale_y); ++y)
            for (int x = x0 * int(scale_x); x < x1 * int(scale_x); ++x) {
                if (layer.upload) {
                    unsigned u = x / scale_x, v = y / scale_y;
                    const Word &word = words[(py + v) * 1024 + ((px + u / ppw) & 1023)];
                    int source_x = int(u) - layer.x + int(file.x), source_y = int(v) - layer.y + int(file.y);
                    if (word.hash != layer.upload || int(word.offset & 65535) != source_x / int(ppw) ||
                        int(word.offset >> 16) != source_y)
                        continue;
                }
                unsigned sx = uint64_t(x - layer.x * int(scale_x)) * d.image.w / (file.w * scale_x);
                unsigned sy = uint64_t(y - layer.y * int(scale_y)) * d.image.h / (file.h * scale_y);
                uint8_t *dst = out->data() + (size_t(y) * w + x) * 4;
                memcpy(dst, d.image.pixels->data() + (size_t(sy) * d.image.w + sx) * 4, 4);
                if (dst[3])
                    dst[3] = !file.semi || dst[3] == 255 ? 255 : 128;
            }
    }
    Image result;
    if (place(int(w), int(h), std::move(out), "composed page", result)) {
        ++matched_uploads;
        page_resident.emplace(name.str(), result);
        insert(binding, result, 1);
    } else
        insert(binding, {0, 0, 1, 1, 0}, 1, false);
}

void dimensions(int i, int &w, int &h)
{
    w = !configured ? 1 : i == 0 ? 1024 : i == 1 ? 64 : i == 2 ? slots : side;
    h = !configured ? 1 : i == 0 ? 512 : i == 1 ? 1024 : i == 2 ? 2 : side;
}
const void *data(int i)
{
    static uint32_t zero[4];
    if (!configured)
        return zero;
    static const std::array<uint32_t, slots * 8> empty_table{};
    if (i == 2 && !pc_hd_enabled)
        return empty_table.data();
    return i == 0   ? (const void *)words.data()
           : i == 1 ? (const void *)palettes.data()
                    : (const void *)table.data();
}
/* The rows of texture `i` to send: the word map's changed rows, else all. */
void upload_rows(int i, bool all, int &y0, int &rows)
{
    int w, h;
    dimensions(i, w, h);
    y0 = 0;
    rows = h;
    if (i == 0 && configured && !all) {
        y0 = std::min(words_y0, words_y1);
        rows = words_y1 - y0;
    }
}
int wanted_layers() { return configured ? std::max(1, int(atlas.size())) : 1; }
} // namespace
extern "C" void pc_hd_upload(int x, int y, int w, int h, const uint16_t *vram)
{
    init();
    if (!configured || w <= 0 || h <= 0)
        return;
    uint32_t c = 0xffffffffu;
    for (int j = 0; j < h; ++j)
        c = crc(vram + (y + j) * 1024 + x, w, c);
    c ^= 0xffffffffu;
    auto candidate = files.lower_bound(uint64_t(c) << 32);
    bool known = candidate != files.end() && uint32_t(candidate->first >> 32) == c;
    std::vector<uint16_t> snapshot;
    bool has_duck_upload = !duck_upload_index.empty();
    if (known || has_duck_upload) {
        snapshot.reserve(size_t(w) * h);
        for (int j = 0; j < h; ++j)
            snapshot.insert(snapshot.end(), vram + (y + j) * 1024 + x, vram + (y + j) * 1024 + x + w);
        uint64_t hash = XXH3_64bits(snapshot.data(), snapshot.size() * 2);
        auto matches = duck_upload_index.find(hash);
        if (matches != duck_upload_index.end())
            for (size_t index : matches->second) {
                const auto &d = duck[index];
                if (d.sw == unsigned(w) && d.sh == unsigned(h))
                    known = true;
            }
        if (known)
            uploads[c] = Upload{w, h, std::move(snapshot), hash};
    }
    if (known)
        ++matched_uploads;
    bool changed = false;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            Word next = known ? Word{c, uint32_t(i) | (uint32_t(j) << 16), uint32_t(w), uint32_t(h)} : Word{};
            Word &old = words[(y + j) * 1024 + x + i];
            if (memcmp(&old, &next, sizeof old)) {
                old = next;
                changed = true;
            }
        }
    ++generation;
    if (changed)
        mark_rows(y, h);
}
extern "C" void pc_hd_clear(int x, int y, int w, int h)
{
    init();
    if (!configured || w <= 0 || h <= 0)
        return;
    bool changed = false;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            Word &t = words[((y + j) & 511) * 1024 + ((x + i) & 1023)];
            if (t.width) {
                t = {};
                changed = true;
            }
        }
    ++generation;
    if (changed)
        mark_rows(y, h);
}
extern "C" void pc_hd_copy(int sx, int sy, int dx, int dy, int w, int h, int masked)
{
    init();
    if (!configured)
        return;
    if (masked) {
        pc_hd_clear(dx, dy, w, h);
        ++generation;
        return;
    }
    std::vector<Word> copy(size_t(w) * h);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            copy[j * w + i] = words[((sy + j) & 511) * 1024 + ((sx + i) & 1023)];
    bool changed = false;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            Word &to = words[((dy + j) & 511) * 1024 + ((dx + i) & 1023)];
            if (memcmp(&to, &copy[j * w + i], sizeof to)) {
                to = copy[j * w + i];
                changed = true;
            }
        }
    ++generation;
    if (changed)
        mark_rows(dy, h);
}
extern "C" void pc_hd_use(unsigned tpage, unsigned clut)
{
    init();
    if (!configured || !pc_hd_enabled)
        return;
    tpage &= 0x19f;
    clut &= 0x7fff;
    unsigned key = (tpage << 15) | clut;
    Use &use = used[key];
    if (use.generation == generation) {
        frame_layers |= use.layers; /* still drawn from: keep those layers */
        for (int l = 0; l < int(atlas.size()); ++l)
            if (use.layers & (1u << l))
                atlas[l].last_used = frame_number;
        return;
    }
    use.generation = generation;
    uint32_t before = frame_layers;
    frame_layers = 0;
    unsigned depth = (tpage >> 7) & 3, ppw = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    uint32_t pal = 0;
    uint16_t colors[256]{};
    if (depth < 2) {
        const uint16_t *vr = pc_native_vram();
        int count = depth == 0 ? 16 : 256;
        for (int i = 0; i < count; ++i)
            colors[i] = vr[(clut >> 6) * 1024 + (((clut & 63) * 16 + i) & 1023)];
        pal = crc(colors, count) ^ 0xffffffffu;
        auto &dst = palettes[clut + (depth ? 32768 : 0)];
        if (dst != pal) {
            dst = pal;
            dirty = palettes_dirty = true;
        }
    }
    std::set<uint32_t> hashes;
    unsigned px = (tpage & 15) * 64, py = ((tpage >> 4) & 1) * 256;
    for (unsigned j = 0; j < 256; ++j)
        for (unsigned i = 0; i < 256 / ppw; ++i) {
            const Word &t = words[(py + j) * 1024 + ((px + i) & 1023)];
            if (t.width)
                hashes.insert(t.hash);
        }
    for (uint32_t c : hashes) {
        uint64_t key = (uint64_t(c) << 32) | pal;
        if (depth < 2) {
            auto u = uploads.find(c);
            if (u != uploads.end()) {
                uint32_t reduced = reduced_palette(u->second.pixels, depth, colors);
                uint64_t rk = (uint64_t(c) << 32) | reduced;
                if (files.count(rk)) {
                    load(rk);
                    auto im = resident.find(rk);
                    if (im != resident.end()) {
                        insert(key, im->second);
                        touch(im->second.layer);
                    }
                    continue;
                }
            }
        }
        load(key);
    }
    resolve_page(tpage, clut, depth, ppw, px, py, colors, pal);
    /* (clear_layer inside may have emptied `used`; the entry is then gone) */
    auto again = used.find(key);
    if (again != used.end())
        again->second.layers = frame_layers;
    frame_layers |= before;
}
extern "C" void pc_hd_frame(void)
{
    if (uploads.size() > 2048) {
        std::set<uint32_t> live;
        for (const auto &word : words)
            if (word.width)
                live.insert(word.hash);
        for (auto it = uploads.begin(); it != uploads.end();) {
            if (!live.count(it->first))
                it = uploads.erase(it);
            else
                ++it;
        }
    }
    /* Images the workers finished: cache them and place them now, then look
     * the drawn pages up again so their table entries appear. */
    if (loader) {
        std::vector<std::tuple<uint64_t, std::string, Decoded>> done;
        {
            std::lock_guard<std::mutex> lock(loader->mutex);
            done.swap(loader->done);
        }
        for (auto &[key, path, image] : done) {
            requested.erase(key);
            if (!image.pixels) {
                fprintf(stderr, "[hd] cannot read %s\n", path.c_str());
                bad.insert(key);
                continue;
            }
            ram_put(path, image);
            if (!full && !resident.count(key))
                place_resident(key, image, path);
        }
        if (!done.empty())
            used.clear();
    }
    ++frame_number;
    frame_layers = 0;
    if (!full)
        return;
    if (cooldown && --cooldown)
        return;
    /* At a game-frame boundary (GPU updates are ordered after earlier draws,
     * frame replays included): reuse the layer drawn from least recently. */
    int victim = -1;
    for (int l = 0; l < int(atlas.size()); ++l)
        if (atlas[l].last_used + 2 < frame_number &&
            (victim < 0 || atlas[l].last_used < atlas[victim].last_used))
            victim = l;
    if (victim < 0) {
        static unsigned reported;
        if (!reported++)
            fprintf(stderr, "[hd] every atlas layer is in use (ROOD_HD_VRAM_MB raises the budget); "
                            "the rest keep their original textures\n");
        cooldown = 120;
        return;
    }
    clear_layer(victim);
    full = false;
}
extern "C" void pc_hd_gl_bind(unsigned program)
{
    init();
    if (last_enabled != pc_hd_enabled) {
        last_enabled = pc_hd_enabled;
        dirty = table_dirty = true;
        used.clear();
    }
    gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    const GLenum formats[4] = {GL_RGBA32UI, GL_R32UI, GL_RGBA32UI, GL_RGBA8};
    const char *names[4] = {"hd_words", "hd_palettes", "hd_table", "hd_atlas"};
    bool created = !gl_tex[0];
    if (created) {
        gl_GenTextures(4, gl_tex);
        for (int i = 0; i < 4; ++i) {
            int w, h;
            dimensions(i, w, h);
            GLenum target = i == 3 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
            gl_ActiveTexture(GL_TEXTURE8 + i);
            gl_BindTexture(target, gl_tex[i]);
            gl_TexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl_TexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            if (i == 3) {
                gl_layers = 1;
                gl_TexImage3D(target, 0, GL_RGBA8, w, h, gl_layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            } else
                gl_TexImage2D(target, 0, formats[i], w, h, 0, i == 1 ? GL_RED_INTEGER : GL_RGBA_INTEGER,
                              GL_UNSIGNED_INT, data(i));
        }
        dirty = false;
    }
    if (wanted_layers() > gl_layers) {
        int want = wanted_layers();
        GLuint grown = 0;
        gl_GenTextures(1, &grown);
        gl_ActiveTexture(GL_TEXTURE11);
        gl_BindTexture(GL_TEXTURE_2D_ARRAY, grown);
        gl_TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        for (int drain = 0; drain < 16 && gl_GetError() != GL_NO_ERROR; ++drain) {
        }
        gl_TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, side, side, want, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        if (gl_GetError() == GL_OUT_OF_MEMORY) {
            gl_DeleteTextures(1, &grown);
            fprintf(stderr, "[hd] no VRAM for %d atlas layers; keeping %d\n", want, gl_layers);
            drop_layers_from(gl_layers);
        } else {
            if (pc_gl_compute && gl_CopyImageSubData)
                gl_CopyImageSubData(gl_tex[3], GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, grown, GL_TEXTURE_2D_ARRAY, 0, 0, 0,
                                    0, side, side, gl_layers);
            else
                forget_uploaded(); /* (no GL 4.3) placed again from the RAM cache or disk */
            gl_DeleteTextures(1, &gl_tex[3]);
            gl_tex[3] = grown;
            gl_layers = want;
        }
    }
    for (int i = 0; i < 4; ++i) {
        GLenum target = i == 3 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
        gl_ActiveTexture(GL_TEXTURE8 + i);
        gl_BindTexture(target, gl_tex[i]);
        gl_Uniform1i(gl_GetUniformLocation(program, names[i]), 8 + i);
        if (i < 3 && (i == 0 ? words_dirty : i == 1 ? palettes_dirty : table_dirty)) {
            int w, h, y0, rows;
            dimensions(i, w, h);
            upload_rows(i, created, y0, rows);
            if (rows > 0)
                gl_TexSubImage2D(target, 0, 0, y0, w, rows, i == 1 ? GL_RED_INTEGER : GL_RGBA_INTEGER,
                                 GL_UNSIGNED_INT, (const uint8_t *)data(i) + size_t(y0) * w * (i == 1 ? 4 : 16));
        }
    }
    for (const auto &p : pending)
        gl_TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, p.rect.x, p.rect.y, p.rect.layer, p.rect.w, p.rect.h, 1, GL_RGBA,
                         GL_UNSIGNED_BYTE, p.pixels->data());
    dirty = words_dirty = palettes_dirty = table_dirty = false;
    words_y0 = 512;
    words_y1 = 0;
    pending.clear();
    gl_ActiveTexture(GL_TEXTURE0);
    static int checks;
    if (configured && checks < 3 && getenv("ROOD_HD_DEBUG")) {
        ++checks;
        fprintf(stderr,
                "[hd-debug] program %u, uniforms %d %d %d %d, GL error %x, "
                "resident %zu\n",
                program, gl_GetUniformLocation(program, names[0]), gl_GetUniformLocation(program, names[1]),
                gl_GetUniformLocation(program, names[2]), gl_GetUniformLocation(program, names[3]),
                gl_GetError(), resident.size());
        using ReadTexture = void(APIENTRY *)(GLenum, GLint, GLenum, GLenum, void *);
        auto read = (ReadTexture)SDL_GL_GetProcAddress("glGetTexImage");
        if (read) {
            std::vector<Word> gpu(words.size());
            gl_ActiveTexture(GL_TEXTURE8);
            gl_BindTexture(GL_TEXTURE_2D, gl_tex[0]);
            read(GL_TEXTURE_2D, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, gpu.data());
            fprintf(stderr, "[hd-debug] word map readback %s, error %x\n",
                    memcmp(gpu.data(), words.data(), words.size() * sizeof(Word)) ? "MISMATCH" : "matches",
                    gl_GetError());
            gl_ActiveTexture(GL_TEXTURE0);
        }
    }
}
#ifdef ROOD_VULKAN
extern "C" void pc_hd_vk_sync(void)
{
    init();
    if (last_enabled != pc_hd_enabled) {
        last_enabled = pc_hd_enabled;
        dirty = table_dirty = true;
        used.clear();
    }
    bool created = !vk_tex[0].image;
    const VkFormat formats[4] = {VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32_UINT, VK_FORMAT_R32G32B32A32_UINT,
                                 VK_FORMAT_R8G8B8A8_UNORM};
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (created) {
        for (int i = 0; i < 4; ++i) {
            int w, h;
            dimensions(i, w, h);
            if (i == 3 ? !vk_image_create_array(&vk_tex[i], w, h, 1, formats[i],
                                                usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                       : !vk_image_create(&vk_tex[i], w, h, formats[i], usage))
                return;
        }
        vk_layers = 1;
    }
    bool grew = false;
    if (wanted_layers() > vk_layers) {
        int want = wanted_layers();
        vk_image grown{};
        if (!vk_image_create_array(&grown, side, side, want, formats[3], usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
            fprintf(stderr, "[hd] no VRAM for %d atlas layers; keeping %d\n", want, vk_layers);
            drop_layers_from(vk_layers);
        } else {
            VkImageCopy region{};
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, uint32_t(vk_layers)};
            region.dstSubresource = region.srcSubresource;
            region.extent = {uint32_t(side), uint32_t(side), 1};
            vkCmdCopyImage(vk_cmd(), vk_tex[3].image, VK_IMAGE_LAYOUT_GENERAL, grown.image, VK_IMAGE_LAYOUT_GENERAL,
                           1, &region);
            vk_image_destroy_later(&vk_tex[3]);
            vk_tex[3] = grown;
            vk_layers = want;
            grew = true;
        }
    }
    if (!dirty && !created && !grew && pending.empty())
        return;
    vk_end_rendering();
    vk_barrier();
    /* Everything in one staging buffer: one allocation per sync, not one per image. */
    struct Copy {
        int image;
        VkBufferImageCopy region;
        const void *source;
        size_t bytes;
    };
    std::vector<Copy> copies;
    size_t total = 0;
    auto add = [&](int i, int x, int y, int w, int h, int layer, const void *p) {
        if (w <= 0 || h <= 0)
            return;
        Copy c{};
        c.image = i;
        c.source = p;
        c.bytes = size_t(w) * h * (i == 0 || i == 2 ? 16 : 4);
        c.region.bufferOffset = total;
        c.region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, uint32_t(layer), 1};
        c.region.imageOffset = {x, y, 0};
        c.region.imageExtent = {uint32_t(w), uint32_t(h), 1};
        total += (c.bytes + 15) & ~size_t(15);
        copies.push_back(c);
    };
    for (int i = 0; i < 3; ++i) {
        if (!created && !(i == 0 ? words_dirty : i == 1 ? palettes_dirty : table_dirty))
            continue;
        int w, h, y0, rows;
        dimensions(i, w, h);
        upload_rows(i, created, y0, rows);
        add(i, 0, y0, w, rows, 0, (const uint8_t *)data(i) + size_t(y0) * w * (i == 1 ? 4 : 16));
    }
    for (const auto &p : pending)
        add(3, p.rect.x, p.rect.y, p.rect.w, p.rect.h, p.rect.layer, p.pixels->data());
    vk_buffer staging{};
    if (total && vk_buffer_create(&staging, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 2)) {
        for (const auto &c : copies) {
            memcpy((uint8_t *)staging.mapped + c.region.bufferOffset, c.source, c.bytes);
            vkCmdCopyBufferToImage(vk_cmd(), staging.buffer, vk_tex[c.image].image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                   &c.region);
        }
        vk_buffer_destroy_later(&staging);
    }
    vk_barrier();
    dirty = words_dirty = palettes_dirty = table_dirty = false;
    words_y0 = 512;
    words_y1 = 0;
    pending.clear();
}
extern "C" void *pc_hd_vk_view(int i) { return (void *)vk_tex[i].view; }
#endif

extern "C" void pc_hd_status(char *out, int size)
{
    init();
    size_t queued = 0;
    if (loader) {
        std::lock_guard<std::mutex> lock(loader->mutex);
        queued = loader->requests.size();
    }
    snprintf(out, size,
             "{\"enabled\":%s,\"indexed\":%zu,\"matched_uploads\":%u,"
             "\"resident\":%zu,\"loaded_total\":%u,\"bad_files\":%zu,\"atlas_full\":%s,"
             "\"layers\":%zu,\"max_layers\":%d,\"evictions\":%u,\"ram_cache_mb\":%zu,\"queued\":%zu}",
             (configured && pc_hd_enabled) ? "true" : "false", size_t(indexed_count), matched_uploads,
             resident.size() + page_resident.size(), loaded_total, bad.size(), full ? "true" : "false",
             atlas.size(), max_layers, evictions, ram_bytes >> 20, queued);
}
