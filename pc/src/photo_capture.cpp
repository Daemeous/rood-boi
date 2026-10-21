// rood.capture's host side (photo.h; pc/docs/Extensions.md "Photo mode foundation"): a photo is the next
// picture presented once the frozen moment is drawn as it is to be (no render waiting), post applied, read
// back by the presenters before the overlay (gl_present.c, vk_present.c), cropped to the format, written
// as a PNG (SDL_SavePNG, through a .part file) with a timestamped name in the photos folder.
#include "photo.h"
#include "photo_internal.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <direct.h> /* mkdir */
#endif

#include "platform.h"

namespace {
using photo::contains;
using photo::copy_out;
using photo::notify;
struct Capture {
    int id;
    std::string owner, path;
    int scale;
};
std::vector<Capture> captures;
std::vector<std::pair<int, pc_photo_capture_result>> results;
int next_capture = 1;

std::string stamp_name(const std::string& dir)
{
    time_t t = time(nullptr);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char base[64];
    strftime(base, sizeof base, "rood-boi-%Y%m%d-%H%M%S", &tm);
    for (int n = 1; n < 1000; n++) {
        std::string path = dir + "/" + base + (n > 1 ? "-" + std::to_string(n) : std::string()) + ".png";
        struct stat st;
        bool taken = stat(path.c_str(), &st) == 0;
        for (const Capture& c : captures)
            taken |= c.path == path;
        if (!taken)
            return path;
    }
    return dir + "/" + base + "-" + std::to_string(next_capture) + ".png";
}
bool make_dirs(const std::string& path)
{
    std::string part;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!part.empty() && part != "." && part.back() != ':') {
#ifdef _WIN32
                if (mkdir(part.c_str()) && errno != EEXIST)
#else
                if (mkdir(part.c_str(), 0755) && errno != EEXIST)
#endif
                    return false;
            }
        }
        if (i < path.size())
            part += path[i];
    }
    return true;
}
pc_photo_capture_result* result_of(int id)
{
    for (auto& r : results)
        if (r.first == id)
            return &r.second;
    return nullptr;
}
void finish_capture(const Capture& c, bool ok, int w, int h, const std::string& error)
{
    pc_photo_capture_result r;
    memset(&r, 0, sizeof r);
    r.done = 1, r.ok = ok, r.width = w, r.height = h, r.scale = 1;
    snprintf(r.path, sizeof r.path, "%s", c.path.c_str());
    snprintf(r.error, sizeof r.error, "%s", error.c_str());
    if (results.size() >= 64)
        results.erase(results.begin());
    results.emplace_back(c.id, r);
    fprintf(stderr, "[photo] capture %d: %s%s%s\n", c.id, ok ? c.path.c_str() : "failed: ", ok ? "" : error.c_str(),
        "");
}

} // namespace

/* ------------------------------------------------------------ capture */

extern "C" const char* pc_photo_dir(void)
{
    const char* d = getenv("ROOD_PHOTOS");
    return d && *d ? d : "pc/user/photos";
}
extern "C" int pc_photo_capture(const char* owner, const char* path, int scale, char* path_out, int path_size,
    char* why, int why_size)
{
    if (captures.size() >= 8) {
        copy_out(why, why_size, "8 captures are waiting already");
        return 0;
    }
    Capture c;
    c.id = next_capture++;
    c.owner = owner;
    c.scale = scale < 1 ? 1 : scale > 4 ? 4 : scale;
    if (path && *path) {
        c.path = path;
    } else {
        if (!make_dirs(pc_photo_dir())) {
            copy_out(why, why_size, std::string("cannot make ") + pc_photo_dir());
            return 0;
        }
        c.path = stamp_name(pc_photo_dir());
    }
    copy_out(path_out, path_size, c.path);
    captures.push_back(c);
    return c.id;
}
extern "C" int pc_photo_capture_result_get(int id, pc_photo_capture_result* out)
{
    if (pc_photo_capture_result* r = result_of(id)) {
        *out = *r;
        return 1;
    }
    for (const Capture& c : captures)
        if (c.id == id) {
            memset(out, 0, sizeof *out);
            snprintf(out->path, sizeof out->path, "%s", c.path.c_str());
            out->scale = 1;
            return 1;
        }
    return 0;
}
/* The next present shows the frozen moment as it is to be (no render waiting or running), or, running, a
 * frame drawn after the hidden layers last changed. */
extern "C" int pc_photo_capture_wanted(void)
{
    if (captures.empty() || !photo::settled())
        return 0;
    if (!photo::paused() && photo::game_frames() - photo::hide_changed_frame() < 2)
        return 0;
    return 1;
}
extern "C" void pc_photo_capture_pixels(const unsigned char* rgba, int w, int h, int stride, int bottom_up)
{
    if (captures.empty())
        return;
    Capture c = captures.front();
    captures.erase(captures.begin());
    /* The view in the window (top-left origin) cropped to the format. */
    int view[4] = { 0, 0, w, h }, r[4];
    photo::format_rect(view, r);
    std::vector<unsigned char> rgb((size_t)r[2] * r[3] * 3);
    for (int y = 0; y < r[3]; y++) {
        int row = r[1] + y;
        const unsigned char* src = rgba + (size_t)(bottom_up ? h - 1 - row : row) * stride + (size_t)r[0] * 4;
        unsigned char* dst = rgb.data() + (size_t)y * r[2] * 3;
        for (int x = 0; x < r[2]; x++)
            memcpy(dst + x * 3, src + x * 4, 3);
    }
    std::string error;
    size_t slash = c.path.find_last_of("/\\");
    if (slash != std::string::npos && !make_dirs(c.path.substr(0, slash)))
        error = "cannot make the folder of " + c.path;
    SDL_Surface* s = error.empty()
        ? SDL_CreateSurfaceFrom(r[2], r[3], SDL_PIXELFORMAT_RGB24, rgb.data(), r[2] * 3) : nullptr;
    std::string part = c.path + ".part";
    if (s) {
        if (!SDL_SavePNG(s, part.c_str()))
            error = std::string("cannot write ") + part + ": " + SDL_GetError();
        else if (pc_replace_file(part.c_str(), c.path.c_str()))
            error = "cannot rename to " + c.path;
        SDL_DestroySurface(s);
    } else if (error.empty())
        error = SDL_GetError();
    if (!error.empty())
        remove(part.c_str());
    finish_capture(c, error.empty(), r[2], r[3], error);
}

namespace {
void release(const std::string& owner)
{
    captures.erase(std::remove_if(captures.begin(), captures.end(), [&](const Capture& c) { return c.owner == owner; }),
        captures.end());
}
void owners(std::vector<std::string>&) { }
void reset() { captures.clear(); }
void json(std::string& out) { out += ",\"captures\":" + std::to_string(captures.size()); }
photo::Register part({ "capture", release, owners, reset, json, nullptr });
} // namespace
