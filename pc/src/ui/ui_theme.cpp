#include "ui/ui_theme.h"
#include "light_prefabs.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <time.h>

namespace ui {
namespace {

bool parse_color(const std::string& s, unsigned* out)
{
    if (s.size() != 7 && s.size() != 9)
        return false;
    if (s[0] != '#')
        return false;
    // strtoul alone would take "#+12345" or "# 12345": every digit must be hex
    for (size_t i = 1; i < s.size(); ++i)
        if (!std::isxdigit((unsigned char)s[i]))
            return false;
    unsigned long v = std::strtoul(s.c_str() + 1, nullptr, 16);
    unsigned r, g, b, a = 255;
    if (s.size() == 7) {
        r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
    } else {
        r = (v >> 24) & 255, g = (v >> 16) & 255, b = (v >> 8) & 255, a = v & 255;
    }
    *out = (a << 24) | (b << 16) | (g << 8) | r;
    return true;
}

} // namespace

bool theme_parse(const char* text, Theme& t, std::string* error)
{
    prefab::Json root;
    if (!prefab::json_parse(text, root, error))
        return false;
    struct { const char* key; unsigned* v; } colors[] = {
        {"ground", &t.ground}, {"panel", &t.panel}, {"rule", &t.rule}, {"rule2", &t.rule2},
        {"bone", &t.bone}, {"ash", &t.ash}, {"dim", &t.dim}, {"accent", &t.accent},
        {"accent_soft", &t.accent_soft}, {"gold", &t.gold}, {"hp", &t.hp}, {"mp", &t.mp}, {"risk", &t.risk},
    };
    if (const prefab::Json* c = root.get("colors"))
        for (auto& e : colors)
            if (const prefab::Json* v = c->get(e.key)) {
                if (v->type != prefab::Json::String || !parse_color(v->string, e.v)) {
                    if (error)
                        *error = std::string("colors.") + e.key + ": expected \"#rrggbb\" or \"#rrggbbaa\"";
                    return false;
                }
            }
    // Numbers: finite, clamped to a range the drawing copes with (text 4..96,
    // layout 0..2000 with rows at least 16 high, motion 0..5 s).
    struct Num { const char* key; float* v; float lo, hi; };
    const Num nums[] = {
        {"tab", &t.tab_size, 4, 96}, {"label", &t.label_size, 4, 96}, {"name", &t.name_size, 4, 96},
        {"body", &t.body_size, 4, 96}, {"hint", &t.hint_size, 4, 96}, {"margin_x", &t.margin_x, 0, 2000},
        {"top", &t.top, 0, 2000}, {"tab_height", &t.tab_height, 0, 2000}, {"row_height", &t.row_height, 16, 2000},
        {"rail_width", &t.rail_width, 0, 2000},
    };
    const Num times[] = {
        {"tab_slide", &t.tab_slide, 0, 5}, {"focus_grow", &t.focus_grow, 0, 5}, {"screen_fade", &t.screen_fade, 0, 5}};
    struct Group { const char* name; const Num* nums; size_t count; const char* expected; };
    const Group groups[] = {{"sizes", nums, sizeof nums / sizeof nums[0], "a number"},
        {"motion", times, sizeof times / sizeof times[0], "seconds"}};
    for (const Group& g : groups)
        if (const prefab::Json* obj = root.get(g.name))
            for (size_t i = 0; i < g.count; ++i)
                if (const prefab::Json* v = obj->get(g.nums[i].key)) {
                    if (v->type != prefab::Json::Number || !std::isfinite(v->number)) {
                        if (error)
                            *error = std::string(g.name) + "." + g.nums[i].key + ": expected " + g.expected;
                        return false;
                    }
                    *g.nums[i].v = (float)std::fmin(std::fmax(v->number, (double)g.nums[i].lo), (double)g.nums[i].hi);
                }
    struct { const char* key; std::string* v; } fonts[] = {
        {"name", &t.font_name}, {"label", &t.font_label}, {"data", &t.font_data}};
    if (const prefab::Json* f = root.get("fonts"))
        for (auto& e : fonts)
            if (const prefab::Json* v = f->get(e.key)) {
                if (v->type != prefab::Json::String) {
                    if (error)
                        *error = std::string("fonts.") + e.key + ": expected a file path";
                    return false;
                }
                *e.v = v->string;
            }
    return true;
}

const Theme& theme()
{
    static Theme current;
    // the file's stamp (mtime in ns + size) as last loaded; only a good parse records it, so a
    // bad edit is retried at the next poll and two saves in the same second both reload
    static long long stamp_ns = -1, stamp_size = -1, failed_ns = -1, failed_size = -1;
    static double next_check;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = now.tv_sec + now.tv_nsec * 1e-9;
    if (t < next_check)
        return current;
    next_check = t + 0.5;
    const char* path = getenv("ROOD_UI_THEME");
    if (!path)
        path = "pc/assets/ui/theme.json";
    struct stat st;
    if (stat(path, &st) != 0)
        return current;
#ifdef _WIN32
    long long ns = (long long)st.st_mtime * 1000000000LL; /* MinGW: whole seconds (the size check helps) */
#else
    long long ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
    if (ns == stamp_ns && (long long)st.st_size == stamp_size)
        return current;
    FILE* f = fopen(path, "rb");
    if (!f)
        return current;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    fclose(f);
    Theme next;
    std::string error;
    if (theme_parse(text.c_str(), next, &error)) {
        current = next;
        stamp_ns = ns, stamp_size = (long long)st.st_size;
    } else if (ns != failed_ns || (long long)st.st_size != failed_size) {
        fprintf(stderr, "[ui] %s: %s (theme kept)\n", path, error.c_str()); /* once per version */
        failed_ns = ns, failed_size = (long long)st.st_size;
    }
    return current;
}

} // namespace ui
