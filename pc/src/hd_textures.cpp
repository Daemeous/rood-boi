#include "hd_textures.h"
#include "gl_api.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#define XXH_INLINE_ALL
#include "../third_party/texture_formats/xxhash.h"
#include "hd_dds.h"
#ifdef ROOD_VULKAN
#include "vk_core.h"
#endif

/* A word retains its original upload coordinates even after a VRAM copy.
 * The two CRCs are the pack's on-disk IDs, not VRAM addresses. No game bytes
 * or save data are replaced. Thread: game/render thread only. */
extern "C" {
int pc_hd_enabled = 1;
}
namespace
{
int last_enabled = 1;
constexpr int side = 8192, slots = 4096;
struct Word {
    uint32_t hash, offset, width, height;
};
struct Image {
    int x, y, w, h;
};
struct Pending {
    Image rect;
    std::vector<uint8_t> pixels;
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
std::map<unsigned, uint64_t> used;
std::vector<Pending> pending;
uint64_t generation = 1;
int cursor_x, cursor_y, row_height;
bool initialized, configured, dirty, full;
bool words_dirty = true, palettes_dirty = true, table_dirty = true;
unsigned matched_uploads, loaded_total;
unsigned gl_tex[4];
#ifdef ROOD_VULKAN
vk_image vk_tex[4];
#endif
extern "C" const uint16_t *pc_native_vram(void);
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
    fprintf(stderr, "[hd] %u replacements indexed (%zu classic, %zu page, %zu DuckStation)\n", indexed_count,
            files.size(), page_files.size(), duck.size());
}
void insert(uint64_t key, Image im, unsigned kind = 0, bool valid = true)
{
    uint32_t a = key >> 32, b = uint32_t(key);
    for (int n = 0, i = bucket(a, b); n < slots; ++n, i = (i + 1) & (slots - 1)) {
        uint32_t *t = table.data() + i * 4;
        uint32_t *d = t + slots * 4;
        if (!d[0] || (t[0] == a && t[1] == b && d[2] == kind)) {
            t[0] = a;
            t[1] = b;
            t[2] = im.x;
            t[3] = im.y;
            d[0] = im.w;
            d[1] = im.h;
            d[2] = kind;
            d[3] = valid;
            dirty = table_dirty = true;
            return;
        }
    }
}
bool place(SDL_Surface *original, const char *label, Image &result)
{
    if (original->w <= 0 || original->h <= 0 || original->w > side || original->h > side) {
        fprintf(stderr, "[hd] unsupported image size %dx%d: %s\n", original->w, original->h, label);
        SDL_DestroySurface(original);

        return false;
    }
    int w = original->w, h = original->h;
    if (cursor_x + w > side) {
        cursor_x = 0;
        cursor_y += row_height;
        row_height = 0;
    }
    if (cursor_y + h > side || resident.size() + page_resident.size() >= slots / 2) {
        SDL_DestroySurface(original);
        full = true;
        fprintf(stderr,
                "[hd] atlas full (%zu images); unmatched textures use originals "
                "until next frame\n",
                resident.size());
        return false;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(original, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(original);
    if (!rgba) {

        return false;
    }
    Pending p;
    p.rect = {cursor_x, cursor_y, w, h};
    p.pixels.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        memcpy(p.pixels.data() + size_t(y) * w * 4, (uint8_t *)rgba->pixels + size_t(y) * rgba->pitch,
               size_t(w) * 4);
    SDL_DestroySurface(rgba);
    ++loaded_total;
    result = p.rect;
    pending.push_back(std::move(p));
    cursor_x += w;
    row_height = std::max(row_height, h);
    if (loaded_total <= 8)
        fprintf(stderr, "[hd] loaded %s (%dx%d)\n", label, w, h);
    return true;
}
void load(uint64_t key)
{
    if (resident.count(key) || bad.count(key))
        return;
    auto f = files.find(key);
    if (f == files.end() || full)
        return;
    std::string ext = std::filesystem::path(f->second).extension().string();
    for (char &c : ext)
        c = std::tolower((unsigned char)c);
    SDL_Surface *original = ext == ".dds" ? hd_load_dds(f->second.c_str()) : SDL_LoadPNG(f->second.c_str());
    if (!original) {
        fprintf(stderr, "[hd] cannot read %s: %s\n", f->second.c_str(), SDL_GetError());
        bad.insert(key);
        return;
    }
    Image im;
    if (!place(original, f->second.c_str(), im)) {
        if (!full)
            bad.insert(key);
        return;
    }
    resident.emplace(key, im);
    insert(key, im);
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
SDL_Surface *read_image(const std::string &path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    for (char &c : ext)
        c = std::tolower((unsigned char)c);
    SDL_Surface *s = ext == ".dds" ? hd_load_dds(path.c_str()) : SDL_LoadPNG(path.c_str());
    if (!s)
        return nullptr;
    if (s->w > side || s->h > side) {
        SDL_DestroySurface(s);
        return nullptr;
    }
    auto result = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    return result;
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
        insert(binding, {0, 0, 1, 1}, 1, false);
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
        return;
    }
    if (full) {
        insert(binding, {0, 0, 1, 1}, 1, false);
        return;
    }
    struct Decoded {
        Layer layer;
        SDL_Surface *image;
    };
    std::vector<Decoded> decoded;
    unsigned scale_x = 1, scale_y = 1;
    SDL_Surface *base = beetle == page_files.end() ? nullptr : read_image(beetle->second);
    if (base) {
        scale_x = std::max(1, (base->w + 255) / 256);
        scale_y = std::max(1, (base->h + 255) / 256);
    }
    for (const auto &layer : layers) {
        SDL_Surface *image = read_image(layer.file->path);
        if (!image)
            continue;
        scale_x = std::max(scale_x, unsigned((image->w + layer.file->w - 1) / layer.file->w));
        scale_y = std::max(scale_y, unsigned((image->h + layer.file->h - 1) / layer.file->h));
        decoded.push_back({layer, image});
    }
    if (!base && decoded.empty()) {
        insert(binding, {0, 0, 1, 1}, 1, false);
        return;
    }
    // Bound temporary composition as well as the GPU atlas. Arbitrary scales use
    // nearest sampling.
    scale_x = std::min(scale_x, 32u);
    scale_y = std::min(scale_y, 32u);
    unsigned w = 256 * scale_x, h = 256 * scale_y;
    SDL_Surface *out = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!out) {
        if (base)
            SDL_DestroySurface(base);
        for (auto &d : decoded)
            SDL_DestroySurface(d.image);
        return;
    }
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            auto dst = (uint8_t *)out->pixels + y * out->pitch + x * 4;
            if (base) {
                memcpy(dst,
                       (uint8_t *)base->pixels + (uint64_t(y) * base->h / h) * base->pitch +
                           (uint64_t(x) * base->w / w) * 4,
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
    if (base)
        SDL_DestroySurface(base);
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
                unsigned sx = uint64_t(x - layer.x * int(scale_x)) * d.image->w / (file.w * scale_x);
                unsigned sy = uint64_t(y - layer.y * int(scale_y)) * d.image->h / (file.h * scale_y);
                uint8_t *dst = (uint8_t *)out->pixels + y * out->pitch + x * 4;
                memcpy(dst, (uint8_t *)d.image->pixels + sy * d.image->pitch + sx * 4, 4);
                if (dst[3])
                    dst[3] = !file.semi || dst[3] == 255 ? 255 : 128;
            }
        SDL_DestroySurface(d.image);
    }
    Image result;
    if (place(out, "composed page", result)) {
        ++matched_uploads;
        page_resident.emplace(name.str(), result);
        insert(binding, result, 1);
    } else
        insert(binding, {0, 0, 1, 1}, 1, false);
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
        dirty = words_dirty = true;
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
        dirty = words_dirty = true;
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
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            words[((dy + j) & 511) * 1024 + ((dx + i) & 1023)] = copy[j * w + i];
    ++generation;
    dirty = words_dirty = true;
}
extern "C" void pc_hd_use(unsigned tpage, unsigned clut)
{
    init();
    if (!configured || !pc_hd_enabled)
        return;
    tpage &= 0x19f;
    clut &= 0x7fff;
    unsigned key = (tpage << 15) | clut;
    if (used[key] == generation)
        return;
    used[key] = generation;
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
                    if (im != resident.end())
                        insert(key, im->second);
                    continue;
                }
            }
        }
        load(key);
    }
    resolve_page(tpage, clut, depth, ppw, px, py, colors, pal);
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
    if (!full)
        return;
    /* Atlas storage is bounded to 256 MiB; reclaim at a game-frame boundary.
     * GPU updates are ordered after earlier draws, including frame replays. */
    resident.clear();
    page_resident.clear();
    table.fill(0);
    used.clear();
    pending.clear();
    cursor_x = cursor_y = row_height = 0;
    full = false;
    dirty = table_dirty = true;
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
    if (!gl_tex[0]) {
        gl_GenTextures(4, gl_tex);
        for (int i = 0; i < 4; ++i) {
            int w, h;
            dimensions(i, w, h);
            gl_ActiveTexture(GL_TEXTURE8 + i);
            gl_BindTexture(GL_TEXTURE_2D, gl_tex[i]);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl_TexImage2D(GL_TEXTURE_2D, 0, formats[i], w, h, 0,
                          i == 1   ? GL_RED_INTEGER
                          : i == 3 ? GL_RGBA
                                   : GL_RGBA_INTEGER,
                          i == 3 ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT, i == 3 ? nullptr : data(i));
        }
        dirty = false;
    }
    for (int i = 0; i < 4; ++i) {
        gl_ActiveTexture(GL_TEXTURE8 + i);
        gl_BindTexture(GL_TEXTURE_2D, gl_tex[i]);
        gl_Uniform1i(gl_GetUniformLocation(program, names[i]), 8 + i);
        if (i < 3 && (i == 0 ? words_dirty : i == 1 ? palettes_dirty : table_dirty)) {
            int w, h;
            dimensions(i, w, h);
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, i == 1 ? GL_RED_INTEGER : GL_RGBA_INTEGER,
                             GL_UNSIGNED_INT, data(i));
        }
    }
    for (const auto &p : pending)
        gl_TexSubImage2D(GL_TEXTURE_2D, 0, p.rect.x, p.rect.y, p.rect.w, p.rect.h, GL_RGBA, GL_UNSIGNED_BYTE,
                         p.pixels.data());
    dirty = words_dirty = palettes_dirty = table_dirty = false;
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
    if (created)
        for (int i = 0; i < 4; ++i) {
            int w, h;
            dimensions(i, w, h);
            if (!vk_image_create(&vk_tex[i], w, h, formats[i],
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
                return;
        }
    if (!dirty && !created && pending.empty())
        return;
    vk_end_rendering();
    vk_barrier();
    auto upload = [](int i, int x, int y, int w, int h, const void *p) {
        size_t bytes = size_t(w) * h * (i == 0 || i == 2 ? 16 : 4);
        vk_buffer staging{};
        if (!vk_buffer_create(&staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 2))
            return;
        memcpy(staging.mapped, p, bytes);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {x, y, 0};
        region.imageExtent = {uint32_t(w), uint32_t(h), 1};
        vkCmdCopyBufferToImage(vk_cmd(), staging.buffer, vk_tex[i].image, VK_IMAGE_LAYOUT_GENERAL, 1,
                               &region);
        vk_buffer_destroy_later(&staging);
    };
    for (int i = 0; i < 3; ++i) {
        if (!created && !(i == 0 ? words_dirty : i == 1 ? palettes_dirty : table_dirty))
            continue;
        int w, h;
        dimensions(i, w, h);
        upload(i, 0, 0, w, h, data(i));
    }
    for (const auto &p : pending)
        upload(3, p.rect.x, p.rect.y, p.rect.w, p.rect.h, p.pixels.data());
    vk_barrier();
    dirty = words_dirty = palettes_dirty = table_dirty = false;
    pending.clear();
}
extern "C" void *pc_hd_vk_view(int i) { return (void *)vk_tex[i].view; }
#endif

extern "C" void pc_hd_status(char *out, int size)
{
    init();
    snprintf(out, size,
             "{\"enabled\":%s,\"indexed\":%zu,\"matched_uploads\":%u,"
             "\"resident\":%zu,\"loaded_total\":%u,\"bad_"
             "files\":%zu,\"atlas_full\":%s}",
             (configured && pc_hd_enabled) ? "true" : "false", size_t(indexed_count), matched_uploads,
             resident.size() + page_resident.size(), loaded_total, bad.size(), full ? "true" : "false");
}
