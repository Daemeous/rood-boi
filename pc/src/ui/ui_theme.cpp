#include "ui/ui_theme.h"
#include "light_prefabs.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <sys/stat.h>
#include <time.h>

#include "ui/ui_tint.h"

extern "C" float pc_ui_menu_opacity; // project_settings.cpp: settings ui_menu_opacity

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
        {"up", &t.up}, {"down", &t.down},
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

namespace {
// The theme file as last parsed; *serial counts the parses that changed it.
const Theme& loaded(int* serial)
{
    static Theme current;
    static int parses;
    *serial = parses;
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
        *serial = ++parses;
        stamp_ns = ns, stamp_size = (long long)st.st_size;
    } else if (ns != failed_ns || (long long)st.st_size != failed_size) {
        fprintf(stderr, "[ui] %s: %s (theme kept)\n", path, error.c_str()); /* once per version */
        failed_ns = ns, failed_size = (long long)st.st_size;
    }
    return current;
}
} // namespace

namespace {
// The tint's derivation (tint_theme): how much the rules and panels lean toward the tint's hue, and the
// brightest ground allowed (relative luminance; a white tint becomes a dark grey near #282828).
const float TINT_CHROMA = 0.35f, TINT_MAX_LUMINANCE = 0.02f;
const float DIM_CONTRAST = 2.4f, ASH_CONTRAST = 4.5f; // at least these on the panel (or the theme's own, if lower)
const float ACCENT_CONTRAST = 2.5f; // the accent's marks (focus bars, chevrons) on the panel, likewise

float to_linear(float c)
{
    c /= 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
float to_srgb(float l)
{
    float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    return c * 255.0f;
}
float luminance(const float rgb[3])
{
    return 0.2126f * to_linear(rgb[0]) + 0.7152f * to_linear(rgb[1]) + 0.0722f * to_linear(rgb[2]);
}
void unpack(unsigned c, float out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = (float)((c >> (8 * i)) & 0xFF);
}
unsigned pack(const float rgb[3], unsigned alpha_of)
{
    unsigned c = alpha_of & 0xFF000000u;
    for (int i = 0; i < 3; ++i) {
        float v = std::fmin(std::fmax(rgb[i], 0.0f), 255.0f);
        c |= (unsigned)(v + 0.5f) << (8 * i);
    }
    return c;
}
float contrast(unsigned a, unsigned b)
{
    float x[3], y[3];
    unpack(a, x), unpack(b, y);
    float la = luminance(x), lb = luminance(y);
    return (std::fmax(la, lb) + 0.05f) / (std::fmin(la, lb) + 0.05f);
}
unsigned mix(unsigned a, unsigned b, float t)
{
    float x[3], y[3];
    unpack(a, x), unpack(b, y);
    for (int i = 0; i < 3; ++i)
        x[i] += (y[i] - x[i]) * t;
    return pack(x, a);
}
// Lift `c` toward `to` in tenths until its contrast on `panel` reaches min(target, before's).
unsigned guard(unsigned c, unsigned to, unsigned panel, float target, float before)
{
    float need = std::fmin(target, before);
    unsigned out = c;
    for (int step = 1; step <= 10 && contrast(out, panel) < need; ++step)
        out = mix(c, to, (float)step / 10.0f);
    return out;
}
// The selection wash (`soft`, translucent, over the opaque `panel`) as drawn, and how far it stands from the
// panel (sRGB distance): a wash near the tint's hue loses its cue though its luminance barely changes.
float wash_distance(unsigned soft, unsigned panel)
{
    float s[3], p[3], a = (float)(soft >> 24) / 255.0f, d = 0;
    unpack(soft, s), unpack(panel, p);
    for (int i = 0; i < 3; ++i)
        d += (s[i] - p[i]) * a * ((s[i] - p[i]) * a);
    return std::sqrt(d);
}
// Lift the wash's colour toward `to` in tenths (its alpha kept) until it stands `need` from `panel`.
unsigned guard_wash(unsigned soft, unsigned to, unsigned panel, float need)
{
    unsigned out = soft;
    for (int step = 1; step <= 10 && wash_distance(out, panel) < need; ++step)
        out = mix(soft, to, (float)step / 10.0f);
    return out;
}
} // namespace

void tint_theme(Theme& t, const int rgb31[3])
{
    float g0[3], g1[3];
    unpack(t.ground, g0);
    pc_ui_tint_ground(rgb31, g1);
    float l = luminance(g1);
    if (l > TINT_MAX_LUMINANCE) { // same hue, darker: scale in linear light
        float k = TINT_MAX_LUMINANCE / l;
        for (float& c : g1)
            c = to_srgb(to_linear(c) * k);
    }
    float mean = (float)(rgb31[0] + rgb31[1] + rgb31[2]) / 3.0f, w[3];
    for (int c = 0; c < 3; ++c)
        w[c] = mean > 0 ? std::fmin(std::fmax(1.0f + TINT_CHROMA * ((float)rgb31[c] / mean - 1.0f), 0.6f), 1.4f)
                        : 1.0f;
    const unsigned panel0 = t.panel;
    for (unsigned* x : {&t.panel, &t.rule, &t.rule2}) {
        float x0[3], x1[3];
        unpack(*x, x0);
        for (int c = 0; c < 3; ++c)
            x1[c] = g1[c] + (x0[c] - g0[c]) * w[c];
        *x = pack(x1, *x);
    }
    t.ground = pack(g1, t.ground);
    // contrast against the panel as drawn opaque
    unsigned panel1 = t.panel | 0xFF000000u, opaque0 = panel0 | 0xFF000000u;
    t.dim = guard(t.dim, t.ash, panel1, DIM_CONTRAST, contrast(t.dim, opaque0));
    t.ash = guard(t.ash, t.bone, panel1, ASH_CONTRAST, contrast(t.ash, opaque0));
    // the accent's marks and the selection wash: lifted toward bone only where the tint would hide them
    t.accent = guard(t.accent, t.bone, panel1, ACCENT_CONTRAST, contrast(t.accent, opaque0));
    t.accent_soft = guard_wash(t.accent_soft, t.bone, panel1, wash_distance(t.accent_soft, opaque0));
}

float theme_opacity()
{
    int serial;
    return (float)(loaded(&serial).ground >> 24) / 255.0f;
}

const Theme& theme()
{
    static Theme shown;
    static float shown_opacity = -1;
    static int shown_serial = -1, shown_tint = -1, shown_rgb[3];
    int serial;
    const Theme& t = loaded(&serial);
    float o = pc_ui_menu_opacity;
    o = std::isfinite(o) ? std::fmin(std::fmax(o, 0.0f), 1.0f) : 1.0f;
    int tint = pc_ui_tint_current(), rgb[3];
    pc_ui_tint_effective(rgb);
    if (serial == shown_serial && o == shown_opacity && tint == shown_tint && rgb[0] == shown_rgb[0]
        && rgb[1] == shown_rgb[1] && rgb[2] == shown_rgb[2])
        return shown;
    shown = t;
    if (tint != PC_UI_TINT_LEA_MONDE)
        tint_theme(shown, rgb);
    shown_opacity = o;
    shown_serial = serial;
    shown_tint = tint;
    for (int c = 0; c < 3; ++c)
        shown_rgb[c] = rgb[c];
    unsigned own = t.ground >> 24; // the theme's ground alpha (240: the menus' look before the setting)
    float k = own ? o * 255.0f / (float)own : 1.0f;
    auto alpha = [](unsigned c, float a) {
        a = std::fmin(std::fmax(a, 0.0f), 255.0f);
        return (c & 0x00FFFFFFu) | ((unsigned)(a + 0.5f) << 24);
    };
    shown.ground = alpha(shown.ground, o * 255.0f);
    shown.panel = alpha(shown.panel, (float)(t.panel >> 24) * k);
    shown.stage = alpha(shown.ground, 255.0f * k);
    return shown;
}

void theme_json(std::string& j)
{
    const Theme& t = theme();
    int rgb[3];
    pc_ui_tint_effective(rgb);
    auto hex = [&](const char* key, unsigned c, bool with_alpha) {
        char buf[48];
        std::snprintf(buf, sizeof buf, ",\"%s\":\"#%02x%02x%02x", key, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
        j += buf;
        if (with_alpha) {
            std::snprintf(buf, sizeof buf, "%02x", c >> 24);
            j += buf;
        }
        j += "\"";
    };
    char buf[96];
    std::snprintf(buf, sizeof buf, ",\"theme\":{\"tint\":\"%s\",\"tint_rgb\":[%d,%d,%d]",
        pc_ui_tint_id(pc_ui_tint_current()), rgb[0], rgb[1], rgb[2]);
    j += buf;
    hex("ground", t.ground, true);
    hex("panel", t.panel, true);
    hex("stage", t.stage, true);
    hex("rule", t.rule, false);
    hex("rule2", t.rule2, false);
    hex("dim", t.dim, false);
    hex("ash", t.ash, false);
    j += "}";
}

} // namespace ui

// The modern menus' backdrop for the presenters (gl_present.c / vk_present.c, the Ashley tab's model
// view): the ground colour (0..1) and its alpha, the menu's opacity. 1 when there is one.
extern "C" int pc_ui_menu_backdrop(float rgba[4])
{
    unsigned c = ui::theme().ground; // 0xAABBGGRR
    for (int i = 0; i < 4; ++i)
        rgba[i] = (float)((c >> (8 * i)) & 0xFF) / 255.0f;
    return 1;
}

// The theme file's own ground (untinted, live-reloaded), 0..255 per channel: Lea Monde's colour
// (ui_tint.cpp pc_ui_tint_rgb, for the Menu tint row's chip and sliders).
extern "C" void pc_ui_theme_file_ground_rgb(int* r, int* g, int* b)
{
    int serial;
    unsigned c = ui::loaded(&serial).ground; // 0xAABBGGRR
    *r = (int)(c & 0xFF), *g = (int)((c >> 8) & 0xFF), *b = (int)((c >> 16) & 0xFF);
}

// The ground colour's red, green and blue (0..255) for C (ashley_bridge.c
// paints the model column's ground in it).
extern "C" void pc_ui_theme_ground_rgb(int* r, int* g, int* b)
{
    unsigned c = ui::theme().ground; // 0xAABBGGRR
    *r = (int)(c & 0xFF), *g = (int)((c >> 8) & 0xFF), *b = (int)((c >> 16) & 0xFF);
}
