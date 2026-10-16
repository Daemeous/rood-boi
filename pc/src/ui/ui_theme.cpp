#include "ui/ui_theme.h"
#include "light_prefabs.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <sys/stat.h>
#include <time.h>

#include "ui/menu_light_fields.h"
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

// The Score group's numbers and rects (ScoreTheme), with their clamps.
struct ScoreNum {
    const char* key;
    float ScoreTheme::*v;
    float lo, hi;
};
const ScoreNum SCORE_NUMS[] = {{"score_size", &ScoreTheme::score_size, 8, 120}, {"rank_size", &ScoreTheme::rank_size, 8, 120},
    {"row_h", &ScoreTheme::row_h, 16, 160}, {"visible_rows", &ScoreTheme::visible_rows, 3, 24},
    {"page_rows", &ScoreTheme::page_rows, 1, 78}, {"locked_alpha", &ScoreTheme::locked_alpha, 0, 1},
    {"title_size", &ScoreTheme::title_size, 8, 96}, {"name_size", &ScoreTheme::name_size, 8, 96},
    {"number_size", &ScoreTheme::number_size, 8, 96}, {"section_size", &ScoreTheme::section_size, 8, 96},
    {"help_size", &ScoreTheme::help_size, 8, 96}, {"value_size", &ScoreTheme::value_size, 8, 96},
    {"record_size", &ScoreTheme::record_size, 8, 96}, {"record_value_size", &ScoreTheme::record_value_size, 8, 96},
    {"big_value_size", &ScoreTheme::big_value_size, 8, 120}, {"time_size", &ScoreTheme::time_size, 8, 96},
    {"note_size", &ScoreTheme::note_size, 8, 96}, {"class_size", &ScoreTheme::class_size, 8, 96},
    {"hint_size", &ScoreTheme::hint_size, 8, 96}, {"glyph_size", &ScoreTheme::glyph_size, 8, 96},
    {"chip_size", &ScoreTheme::chip_size, 6, 48}, {"pad", &ScoreTheme::pad, 0, 120},
    {"inset", &ScoreTheme::inset, 0, 120}, {"name_x", &ScoreTheme::name_x, 0, 400},
    {"name_x3", &ScoreTheme::name_x3, 0, 400}, {"line_gap", &ScoreTheme::line_gap, 0, 200},
    {"place_gap", &ScoreTheme::place_gap, 0, 200}, {"head_rule", &ScoreTheme::head_rule, 0, 200},
    {"body_gap", &ScoreTheme::body_gap, 0, 200}, {"hint_y", &ScoreTheme::hint_y, 0, 400},
    {"label_top", &ScoreTheme::label_top, 0, 200}, {"card_top", &ScoreTheme::card_top, 0, 200}};
struct ScoreRect {
    const char* key;
    float (ScoreLayout::*v)[2];
};
const ScoreRect SCORE_RECTS[] = {{"header", &ScoreLayout::header}, {"strip", &ScoreLayout::strip},
    {"content", &ScoreLayout::content}, {"home_card", &ScoreLayout::home_card}, {"home_list", &ScoreLayout::home_list},
    {"titles_list", &ScoreLayout::titles_list}, {"titles_detail", &ScoreLayout::titles_detail},
    {"records_table", &ScoreLayout::records_table}, {"records_detail", &ScoreLayout::records_detail},
    {"trials_list", &ScoreLayout::trials_list}, {"trials_panel", &ScoreLayout::trials_panel},
    {"bestiary_list", &ScoreLayout::bestiary_list}, {"bestiary_detail", &ScoreLayout::bestiary_detail}};
float clampf(double v, float lo, float hi) { return (float)std::fmin(std::fmax(v, (double)lo), (double)hi); }

// "score": numbers, then "layout": {"wide": {rect: [a, b]}, "narrow": {...}}.
bool parse_score(const prefab::Json& g, ScoreTheme& s, std::string* error)
{
    for (const ScoreNum& n : SCORE_NUMS)
        if (const prefab::Json* v = g.get(n.key)) {
            if (v->type != prefab::Json::Number || !std::isfinite(v->number)) {
                if (error)
                    *error = std::string("score.") + n.key + ": expected a number";
                return false;
            }
            s.*n.v = clampf(v->number, n.lo, n.hi);
        }
    const prefab::Json* layout = g.get("layout");
    if (!layout)
        return true;
    for (int w = 0; w < 2; ++w) {
        const char* name = w ? "narrow" : "wide";
        const prefab::Json* l = layout->get(name);
        if (!l)
            continue;
        ScoreLayout& out = w ? s.narrow : s.wide;
        for (const ScoreRect& r : SCORE_RECTS)
            if (const prefab::Json* v = l->get(r.key)) {
                if (v->type != prefab::Json::Array || v->items.size() != 2 || v->items[0].type != prefab::Json::Number
                    || v->items[1].type != prefab::Json::Number || !std::isfinite(v->items[0].number)
                    || !std::isfinite(v->items[1].number)) {
                    if (error)
                        *error = std::string("score.layout.") + name + "." + r.key + ": expected [a, b]";
                    return false;
                }
                float a = clampf(v->items[0].number, 0, 2000), b = clampf(v->items[1].number, 0, 2000);
                (out.*r.v)[0] = a;
                (out.*r.v)[1] = b > a ? b : a + 1;
            }
    }
    return true;
}

} // namespace

bool score_key_set(ScoreTheme& s, const char* key, int index, float value)
{
    if (!key || !std::isfinite(value))
        return false;
    for (const ScoreNum& n : SCORE_NUMS)
        if (!std::strcmp(key, n.key)) {
            s.*n.v = clampf(value, n.lo, n.hi);
            return true;
        }
    const char* dot = std::strchr(key, '.');
    if (!dot || index < 0 || index > 1)
        return false;
    std::string width(key, (size_t)(dot - key));
    if (width != "wide" && width != "narrow")
        return false;
    ScoreLayout& l = width == "narrow" ? s.narrow : s.wide;
    for (const ScoreRect& r : SCORE_RECTS)
        if (!std::strcmp(dot + 1, r.key)) {
            (l.*r.v)[index] = clampf(value, 0, 2000);
            if ((l.*r.v)[1] <= (l.*r.v)[0])
                (l.*r.v)[1] = (l.*r.v)[0] + 1;
            return true;
        }
    return false;
}

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
    if (const prefab::Json* g = root.get("score"))
        if (!parse_score(*g, t.score, error))
            return false;
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
    if (const prefab::Json* m = root.get("menu_model_light"))
        if (!menu_light_parse(*m, t.menu_light, error))
            return false;
    return true;
}

const char* theme_path()
{
    const char* path = getenv("ROOD_UI_THEME");
    return path && *path ? path : "pc/assets/ui/theme.json";
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
    const char* path = theme_path();
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

const Theme& theme_file(int* serial) { return loaded(serial); }

float theme_opacity()
{
    int serial;
    return (float)(loaded(&serial).ground >> 24) / 255.0f;
}

namespace {
// The RPC's Score overrides (score_tune), applied over the file's values: one per key and index (a
// number's index is 0), a later tune replacing it; cleared by a reset or the file's next edit (the file
// wins: a live reload is never masked by an earlier tune).
struct ScoreOverride {
    std::string key;
    int index;
    float value;
};
std::vector<ScoreOverride> score_overrides;
int score_override_serial;
bool score_number_key(const char* key)
{
    for (const ScoreNum& n : SCORE_NUMS)
        if (!std::strcmp(key, n.key))
            return true;
    return false;
}
} // namespace

bool score_tune(const char* key, int index, float value)
{
    if (!key) {
        score_overrides.clear();
        ++score_override_serial;
        return true;
    }
    ScoreTheme probe;
    if (!score_key_set(probe, key, index, value))
        return false;
    if (score_number_key(key))
        index = 0;
    ++score_override_serial;
    for (ScoreOverride& o : score_overrides)
        if (o.index == index && o.key == key) {
            o.value = value;
            return true;
        }
    score_overrides.push_back({key, index, value});
    return true;
}

const Theme& theme()
{
    static Theme shown;
    static float shown_opacity = -1;
    static int shown_serial = -1, shown_tint = -1, shown_rgb[3], shown_overrides = -1;
    int serial;
    const Theme& t = loaded(&serial);
    float o = pc_ui_menu_opacity;
    o = std::isfinite(o) ? std::fmin(std::fmax(o, 0.0f), 1.0f) : 1.0f;
    int tint = pc_ui_tint_current(), rgb[3];
    pc_ui_tint_effective(rgb);
    if (serial == shown_serial && o == shown_opacity && tint == shown_tint && rgb[0] == shown_rgb[0]
        && rgb[1] == shown_rgb[1] && rgb[2] == shown_rgb[2] && shown_overrides == score_override_serial)
        return shown;
    if (shown_serial != -1 && serial != shown_serial && !score_overrides.empty()) {
        score_overrides.clear(); // the file was edited: its values show
        ++score_override_serial;
    }
    shown = t;
    for (const ScoreOverride& v : score_overrides)
        score_key_set(shown.score, v.key.c_str(), v.index, v.value);
    shown_overrides = score_override_serial;
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

void score_json(std::string& j)
{
    const ScoreTheme& s = theme().score;
    char buf[160];
    j += ",\"tunables\":{";
    bool first = true;
    for (const ScoreNum& n : SCORE_NUMS) {
        std::snprintf(buf, sizeof buf, "%s\"%s\":%g", first ? "" : ",", n.key, (double)(s.*n.v));
        j += buf;
        first = false;
    }
    j += ",\"layout\":{";
    for (int w = 0; w < 2; ++w) {
        const ScoreLayout& l = w ? s.narrow : s.wide;
        j += w ? ",\"narrow\":{" : "\"wide\":{";
        for (size_t i = 0; i < sizeof SCORE_RECTS / sizeof SCORE_RECTS[0]; ++i) {
            std::snprintf(buf, sizeof buf, "%s\"%s\":[%g,%g]", i ? "," : "", SCORE_RECTS[i].key,
                (double)(l.*SCORE_RECTS[i].v)[0], (double)(l.*SCORE_RECTS[i].v)[1]);
            j += buf;
        }
        j += "}";
    }
    j += "}},\"tune_overrides\":[";
    for (size_t i = 0; i < score_overrides.size(); ++i) {
        const ScoreOverride& o = score_overrides[i];
        std::snprintf(buf, sizeof buf, "%s\"%s[%d]\"", i ? "," : "", o.key.c_str(), o.index);
        j += buf;
    }
    j += "]";
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

// The Score group's override for the RPC (dev_rpc.c ui_menu score_tune; key null: reset). 1 when known.
extern "C" int pc_ui_score_tune(const char* key, int index, double value)
{
    return ui::score_tune(key, index, (float)value) ? 1 : 0;
}
