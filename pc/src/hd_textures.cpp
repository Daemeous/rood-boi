#include "hd_textures.h"
#include "gl_api.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef ROOD_VULKAN
#include "vk_core.h"
#endif

/* A word retains its original upload coordinates even after a VRAM copy.
 * The two CRCs are the pack's on-disk IDs, not VRAM addresses. No game bytes
 * or save data are replaced. Thread: game/render thread only. */
extern "C" {
int pc_hd_enabled = 1;
}
namespace {
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
std::array<uint32_t, 64 * 1024> palettes {};
std::array<uint32_t, slots * 8> table {};
std::map<uint64_t, std::string> files;
std::map<uint64_t, Image> resident;
std::set<uint64_t> bad;
std::map<unsigned, uint64_t> used;
std::vector<Pending> pending;
uint64_t generation = 1;
int cursor_x, cursor_y, row_height;
bool initialized, configured, dirty, full;
unsigned matched_uploads, loaded_total;
unsigned gl_tex[4];
#ifdef ROOD_VULKAN
vk_image vk_tex[4];
#endif
extern "C" const uint16_t* pc_native_vram(void);
uint32_t crc(const uint16_t* data, size_t n, uint32_t c = 0xffffffffu)
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
    const char* dir = getenv("ROOD_TEXTURE_PACK");
    if (!dir || !*dir)
        return;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        std::string name = entry.path().filename().string();
        auto dash = name.find('-'), dot = name.rfind('.');
        if (dash == std::string::npos || dot == std::string::npos || dash < 1 || dash > 8 || dot <= dash + 1
            || dot - dash > 9 || name.substr(dot) != ".png")
            continue;
        std::string a = name.substr(0, dash), b = name.substr(dash + 1, dot - dash - 1);
        if (a.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos
            || b.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            continue;
        uint64_t key = (uint64_t(strtoul(a.c_str(), nullptr, 16)) << 32) | strtoul(b.c_str(), nullptr, 16);
        files.emplace(key, entry.path().string());
    }
    configured = !files.empty();
    if (configured)
        words.resize(1024 * 512);
    fprintf(stderr, "[hd] %zu Beetle PNG replacements indexed in %s%s\n", files.size(), dir,
        ec ? " (directory error)" : "");
}
void insert(uint64_t key, Image im)
{
    uint32_t a = key >> 32, b = uint32_t(key);
    for (int n = 0, i = bucket(a, b); n < slots; ++n, i = (i + 1) & (slots - 1)) {
        uint32_t* t = table.data() + i * 4;
        uint32_t* d = t + slots * 4;
        if (!d[0]) {
            t[0] = a;
            t[1] = b;
            t[2] = im.x;
            t[3] = im.y;
            d[0] = im.w;
            d[1] = im.h;
            dirty = true;
            return;
        }
    }
}
void load(uint64_t key)
{
    if (resident.count(key) || bad.count(key))
        return;
    auto f = files.find(key);
    if (f == files.end() || full)
        return;
    SDL_Surface* original = SDL_LoadPNG(f->second.c_str());
    if (!original) {
        fprintf(stderr, "[hd] cannot read %s: %s\n", f->second.c_str(), SDL_GetError());
        bad.insert(key);
        return;
    }
    if (original->w <= 0 || original->h <= 0 || original->w > side || original->h > side) {
        fprintf(
            stderr, "[hd] unsupported image size %dx%d: %s\n", original->w, original->h, f->second.c_str());
        SDL_DestroySurface(original);
        bad.insert(key);
        return;
    }
    int w = original->w, h = original->h;
    if (cursor_x + w > side) {
        cursor_x = 0;
        cursor_y += row_height;
        row_height = 0;
    }
    if (cursor_y + h > side || resident.size() >= slots / 2) {
        SDL_DestroySurface(original);
        full = true;
        fprintf(stderr, "[hd] atlas full (%zu images); unmatched textures use originals until next frame\n",
            resident.size());
        return;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(original, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(original);
    if (!rgba) {
        bad.insert(key);
        return;
    }
    Pending p;
    p.rect = { cursor_x, cursor_y, w, h };
    p.pixels.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        memcpy(p.pixels.data() + size_t(y) * w * 4, (uint8_t*)rgba->pixels + size_t(y) * rgba->pitch,
            size_t(w) * 4);
    SDL_DestroySurface(rgba);
    ++loaded_total;
    resident.emplace(key, p.rect);
    insert(key, p.rect);
    pending.push_back(std::move(p));
    cursor_x += w;
    row_height = std::max(row_height, h);
    if (resident.size() <= 8)
        fprintf(stderr, "[hd] loaded %s (%dx%d)\n", f->second.c_str(), w, h);
}
void dimensions(int i, int& w, int& h)
{
    w = !configured ? 1 : i == 0 ? 1024 : i == 1 ? 64 : i == 2 ? slots : side;
    h = !configured ? 1 : i == 0 ? 512 : i == 1 ? 1024 : i == 2 ? 2 : side;
}
const void* data(int i)
{
    static uint32_t zero[4];
    if (!configured)
        return zero;
    static const std::array<uint32_t, slots * 8> empty_table {};
    if (i == 2 && !pc_hd_enabled)
        return empty_table.data();
    return i == 0 ? (const void*)words.data()
        : i == 1  ? (const void*)palettes.data()
                  : (const void*)table.data();
}
}
extern "C" void pc_hd_upload(int x, int y, int w, int h, const uint16_t* vram)
{
    init();
    if (!configured || w <= 0 || h <= 0)
        return;
    uint32_t c = 0xffffffffu;
    for (int j = 0; j < h; ++j)
        c = crc(vram + (y + j) * 1024 + x, w, c);
    c ^= 0xffffffffu;
    /* Only track uploads named in this pack. Palettes still hash from VRAM. */
    bool known = files.lower_bound(uint64_t(c) << 32) != files.end()
        && uint32_t(files.lower_bound(uint64_t(c) << 32)->first >> 32) == c;
    if (known)
        ++matched_uploads;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            words[(y + j) * 1024 + x + i]
                = known ? Word { c, uint32_t(i) | (uint32_t(j) << 16), uint32_t(w), uint32_t(h) } : Word {};
    ++generation;
    dirty = true;
}
extern "C" void pc_hd_clear(int x, int y, int w, int h)
{
    init();
    if (!configured || w <= 0 || h <= 0)
        return;
    bool changed = false;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            Word& t = words[((y + j) & 511) * 1024 + ((x + i) & 1023)];
            if (t.width) {
                t = {};
                changed = true;
            }
        }
    ++generation;
    if (changed)
        dirty = true;
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
    dirty = true;
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
    if (depth < 2) {
        const uint16_t* vr = pc_native_vram();
        uint16_t colors[256];
        int count = depth == 0 ? 16 : 256;
        for (int i = 0; i < count; ++i)
            colors[i] = vr[(clut >> 6) * 1024 + (((clut & 63) * 16 + i) & 1023)];
        pal = crc(colors, count) ^ 0xffffffffu;
        auto& dst = palettes[clut + (depth ? 32768 : 0)];
        if (dst != pal) {
            dst = pal;
            dirty = true;
        }
    }
    std::set<uint32_t> hashes;
    unsigned px = (tpage & 15) * 64, py = ((tpage >> 4) & 1) * 256;
    for (unsigned j = 0; j < 256; ++j)
        for (unsigned i = 0; i < 256 / ppw; ++i) {
            const Word& t = words[(py + j) * 1024 + ((px + i) & 1023)];
            if (t.width)
                hashes.insert(t.hash);
        }
    for (uint32_t c : hashes)
        load((uint64_t(c) << 32) | pal);
}
extern "C" void pc_hd_frame(void)
{
    if (!full)
        return;
    /* Atlas storage is bounded to 256 MiB; reclaim at a game-frame boundary.
     * GPU updates are ordered after earlier draws, including frame replays. */
    resident.clear();
    table.fill(0);
    used.clear();
    pending.clear();
    cursor_x = cursor_y = row_height = 0;
    full = false;
    dirty = true;
}
extern "C" void pc_hd_gl_bind(unsigned program)
{
    init();
    if (last_enabled != pc_hd_enabled) {
        last_enabled = pc_hd_enabled;
        dirty = true;
        used.clear();
    }
    gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    const GLenum formats[4] = { GL_RGBA32UI, GL_R32UI, GL_RGBA32UI, GL_RGBA8 };
    const char* names[4] = { "hd_words", "hd_palettes", "hd_table", "hd_atlas" };
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
                i == 1       ? GL_RED_INTEGER
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
        if (dirty && i < 3) {
            int w, h;
            dimensions(i, w, h);
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, i == 1 ? GL_RED_INTEGER : GL_RGBA_INTEGER,
                GL_UNSIGNED_INT, data(i));
        }
    }
    for (const auto& p : pending)
        gl_TexSubImage2D(GL_TEXTURE_2D, 0, p.rect.x, p.rect.y, p.rect.w, p.rect.h, GL_RGBA, GL_UNSIGNED_BYTE,
            p.pixels.data());
    dirty = false;
    pending.clear();
    gl_ActiveTexture(GL_TEXTURE0);
    static int checks;
    if (configured && checks < 3 && getenv("ROOD_HD_DEBUG")) {
        ++checks;
        fprintf(stderr, "[hd-debug] program %u, uniforms %d %d %d %d, GL error %x, resident %zu\n", program,
            gl_GetUniformLocation(program, names[0]), gl_GetUniformLocation(program, names[1]),
            gl_GetUniformLocation(program, names[2]), gl_GetUniformLocation(program, names[3]), gl_GetError(),
            resident.size());
        using ReadTexture = void(APIENTRY*)(GLenum, GLint, GLenum, GLenum, void*);
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
        dirty = true;
        used.clear();
    }
    bool created = !vk_tex[0].image;
    const VkFormat formats[4] = { VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32_UINT,
        VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R8G8B8A8_UNORM };
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
    auto upload = [](int i, int x, int y, int w, int h, const void* p) {
        size_t bytes = size_t(w) * h * (i == 0 || i == 2 ? 16 : 4);
        vk_slice s = vk_arena(bytes, 16);
        if (!s.data)
            return;
        memcpy(s.data, p, bytes);
        VkBufferImageCopy region {};
        region.bufferOffset = s.offset;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset = { x, y, 0 };
        region.imageExtent = { uint32_t(w), uint32_t(h), 1 };
        vkCmdCopyBufferToImage(vk_cmd(), s.buffer, vk_tex[i].image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    };
    for (int i = 0; i < 3; ++i) {
        int w, h;
        dimensions(i, w, h);
        upload(i, 0, 0, w, h, data(i));
    }
    for (const auto& p : pending)
        upload(3, p.rect.x, p.rect.y, p.rect.w, p.rect.h, p.pixels.data());
    vk_barrier();
    dirty = false;
    pending.clear();
}
extern "C" void* pc_hd_vk_view(int i) { return (void*)vk_tex[i].view; }
#endif

extern "C" void pc_hd_status(char* out, int size)
{
    init();
    snprintf(out, size,
        "{\"enabled\":%s,\"indexed\":%zu,\"matched_uploads\":%u,\"resident\":%zu,\"loaded_total\":%u,\"bad_"
        "files\":%zu,\"atlas_full\":%s}",
        (configured && pc_hd_enabled) ? "true" : "false", files.size(), matched_uploads, resident.size(),
        loaded_total, bad.size(), full ? "true" : "false");
}
