// The HUD layer's renderer (hud_internal.h): the command buffer claims filled
// (lua_api_hud.cpp) drawn over the presented picture (pc_gl_view: not the
// window's side bars) in the foreground draw list, under the modern menus
// (dev_overlay.cpp calls pc_hud_frame before them). Coordinates are the
// game's screen (its display area: 320 x 224 in the field); in widescreen
// the game's 2D is drawn 3/4 as wide about the screen's centre, or about
// the edge its element is anchored to (pc_hw_anchor_packets), and so are
// these; PC_HUD_ANCHOR_PICTURE (the Travel prompt) maps them over the whole picture with square pixels. Text: a
// ui_text style's font file loaded into the dynamic atlas (as the Travel
// prompt's, fast_travel_prompt.cpp), letter-spaced, stretched, and outlined
// by copies around two rings under the fill; fitted text shrinks to keep that
// outline inside its box. Prompt icons: button_prompts.h's images, outlined
// the same way (tinted copies).
#include "hud/hud_internal.h"
#include "button_prompts.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ui_text.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

extern "C" int pc_gl_view[4];
extern "C" int pc_gte_widescreen;
extern "C" void pc_display_size(int* w, int* h) __attribute__((weak)); // boot_runtime.c

namespace {
using hud::Command;
using hud::Style;

// A font file in the dynamic atlas (NULL when missing: ImGui's own font).
ImFont* font_file(const char* path)
{
    struct Loaded {
        std::string path;
        ImFont* font;
    };
    static Loaded loaded[8];
    static int count;
    for (int i = 0; i < count; ++i)
        if (loaded[i].path == path)
            return loaded[i].font;
    ImFont* font = nullptr;
    struct stat st;
    if (stat(path, &st) == 0) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.Flags |= ImFontFlags_NoLoadError;
        font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 0.0f, &cfg);
    } else {
        std::fprintf(stderr, "[hud] font %s missing (ImGui's own used)\n", path);
    }
    if (count < 8)
        loaded[count++] = { path, font };
    return font;
}
ImFont* font_of(const Style& st)
{
    int i = st.font >= 0 && st.font < PC_UI_TEXT_STYLES ? st.font : PC_UI_TEXT_LABELS;
    ImFont* f = font_file(pc_ui_text_styles[i].font);
    return f ? f : ImGui::GetFont();
}

// The game's screen in the window: its display area (the field's 320 x 224) is the picture;
// PC_HUD_ANCHOR_PICTURE's coordinates are fractions of it in 320 x 240 with square pixels.
struct Map {
    float vx, vy, vw, vh, gw, gh;
    bool wide;
    Map()
    {
        vx = (float)pc_gl_view[0], vy = (float)pc_gl_view[1], vw = (float)pc_gl_view[2], vh = (float)pc_gl_view[3];
        int w = 320, h = 224;
        if (pc_display_size)
            pc_display_size(&w, &h);
        gw = w >= 64 && w <= 1024 ? (float)w : 320.0f;
        gh = h >= 64 && h <= 512 ? (float)h : 224.0f;
        wide = pc_gte_widescreen != 0;
    }
    float x(float px, int anchor) const
    {
        if (anchor == PC_HUD_ANCHOR_SCREEN)
            return vx + px * vh / 240.0f;
        if (anchor == PC_HUD_ANCHOR_PICTURE)
            return vx + px / 320.0f * vw;
        float f = px / gw;
        if (wide)
            f = anchor == 1 ? f * 0.75f : anchor == 2 ? 1.0f - (1.0f - f) * 0.75f : 0.5f + (f - 0.5f) * 0.75f;
        return vx + f * vw;
    }
    static bool square(int anchor) { return anchor == PC_HUD_ANCHOR_PICTURE || anchor == PC_HUD_ANCHOR_SCREEN; }
    float y(float py, int anchor) const { return vy + py / (square(anchor) ? 240.0f : gh) * vh; }
    // window pixels per game pixel down, and across (widescreen's 3/4 for the game's 2D)
    float down(int anchor) const { return vh / (square(anchor) ? 240.0f : gh); }
    float across(int anchor) const { return square(anchor) ? down(anchor) : vw / gw * (wide ? 0.75f : 1.0f); }
};

unsigned rgba(unsigned rgb, float a)
{
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    return ((rgb >> 16) & 255u) | (rgb & 0xFF00u) | ((rgb & 255u) << 16) | ((unsigned)(a * 255.0f + 0.5f) << 24);
}

// The text's advance at `px` (tracking included between letters), in pixels.
float advance(ImFont* font, float px, const char* s, size_t n, float tracking)
{
    float w = 0;
    int letters = 0;
    for (const char* c = s; c < s + n;) {
        unsigned cp;
        int k = ImTextCharFromUtf8(&cp, c, s + n);
        if (k <= 0)
            break;
        w += font->CalcTextSizeA(px, 1e9f, 0, c, c + k).x;
        ++letters;
        c += k;
    }
    return w + (letters > 1 ? (float)(letters - 1) * tracking * px : 0.0f);
}

// One copy of the text with its top-left at (x, y), letter by letter.
void letters(ImDrawList* dl, ImFont* font, float px, float x, float y, unsigned col, const char* s, size_t n,
    float tracking)
{
    for (const char* c = s; c < s + n;) {
        unsigned cp;
        int k = ImTextCharFromUtf8(&cp, c, s + n);
        if (k <= 0)
            break;
        dl->AddText(font, px, ImVec2(x, y), col, c, c + k);
        x += font->CalcTextSizeA(px, 1e9f, 0, c, c + k).x + tracking * px;
        c += k;
    }
}

// A capital H's ink height per pixel of font size (cap: a style's size from a cap height).
float cap_ratio(ImFont* font)
{
    ImFontBaked* baked = font->GetFontBaked(64.0f);
    ImFontGlyph* h = baked ? baked->FindGlyphNoFallback('H') : nullptr;
    return h && h->Y1 > h->Y0 ? (h->Y1 - h->Y0) / 64.0f : 0.7f;
}

// Copies of draw(dx, dy) around two rings r wide (squeezed across), for an outline under the fill.
template <class Draw> void rings(float r, float squeeze, Draw draw)
{
    const int n = 16;
    for (float ring : { r, r * 0.5f })
        for (int i = 0; i < n; ++i) {
            float a = (float)i * 6.2831853f / n;
            draw(std::cos(a) * ring / squeeze, std::sin(a) * ring);
        }
}

void draw_text(ImDrawList* dl, const Map& m, const Command& c, const Style& st, const char* s)
{
    ImFont* font = font_of(st);
    float ratio = cap_ratio(font);
    float ky = m.down(c.anchor), kx = m.across(c.anchor);
    float px = (st.cap > 0 ? st.cap / ratio : st.size) * ky;
    float r = st.stroke * ky; // the outline's width
    if (c.fit && c.h > 0) // the capitals and the outline inside the box's height
        px = std::fmin(px, (c.h * ky - 2.0f * r) / ratio);
    if (px < 1)
        return;
    float w = advance(font, px, s, c.length, st.tracking); // at ky in both directions
    float squeeze = st.stretch * kx / ky;                 // then across: the stretch and widescreen's 3/4
    if (c.fit && c.w > 0 && w > 0) // ... and its width (condensed, never wider)
        squeeze = std::fmin(squeeze, std::fmax(c.w * kx - 2.0f * r, 1.0f) / w);
    ImFontBaked* baked = font->GetFontBaked(px);
    float ascent = baked ? baked->Ascent : px * 0.8f, line = baked ? baked->Ascent - baked->Descent : px;
    // middle: the capitals' middle (an H's ink), not the line's
    ImFontGlyph* cap = baked ? baked->FindGlyphNoFallback('H') : nullptr;
    float middle = cap && cap->Y1 > cap->Y0 ? (cap->Y0 + cap->Y1) * 0.5f : line * 0.5f;
    float left, top;
    if (c.fit) { // placed in the box: across by the style's align, the capitals' middle on its middle
        float x0 = m.x(c.x, c.anchor) + r, x1 = m.x(c.x, c.anchor) + c.w * kx - r, ww = w * squeeze;
        left = st.align == hud::ALIGN_CENTRE ? (x0 + x1 - ww) * 0.5f : st.align == hud::ALIGN_RIGHT ? x1 - ww : x0;
        top = m.y(c.y + c.h * 0.5f, c.anchor) - middle;
    } else {
        float gw = w * squeeze / kx; // its width in the game's pixels
        float gx = c.x - (st.align == hud::ALIGN_CENTRE ? gw * 0.5f : st.align == hud::ALIGN_RIGHT ? gw : 0.0f);
        left = m.x(gx, c.anchor), top = m.y(c.y, c.anchor);
        top -= st.anchor == hud::V_MIDDLE ? middle : st.anchor == hud::V_BASELINE ? ascent
            : st.anchor == hud::V_BOTTOM                ? line
                                                        : 0.0f;
    }
    left = std::round(left), top = std::round(top);
    int start = dl->VtxBuffer.Size;
    if (r > 0 && st.stroke_alpha > 0) {
        unsigned sc = rgba(st.stroke_color, st.stroke_alpha * c.alpha);
        rings(r, squeeze, [&](float dx, float dy) {
            letters(dl, font, px, left + dx, top + dy, sc, s, c.length, st.tracking);
        });
    }
    letters(dl, font, px, left, top, rgba(c.color, c.alpha), s, c.length, st.tracking);
    for (int i = start; i < dl->VtxBuffer.Size; ++i)
        dl->VtxBuffer[i].pos.x = left + (dl->VtxBuffer[i].pos.x - left) * squeeze;
}

// A prompt icon: left at x, middle at y, h high; the style's stroke outlines it (tinted copies).
void draw_glyph(ImDrawList* dl, const Map& m, const Command& c, const Style* st)
{
    float ky = m.down(c.anchor);
    pc_prompt_image g = pc_prompt_glyph(c.button, c.h * ky);
    if (!g.texture || g.h <= 0)
        return;
    ImTextureRef tex = ((ImTextureData*)g.texture)->GetTexRef();
    float left = std::round(m.x(c.x, c.anchor)), top = std::round(m.y(c.y, c.anchor) - g.h * 0.5f);
    ImVec2 uv0(g.uv0[0], g.uv0[1]), uv1(g.uv1[0], g.uv1[1]);
    float r = st ? st->stroke * ky : 0.0f;
    if (r > 0 && st->stroke_alpha > 0) {
        unsigned sc = rgba(st->stroke_color, st->stroke_alpha * c.alpha);
        rings(r, 1.0f, [&](float dx, float dy) {
            dl->AddImage(tex, ImVec2(left + dx, top + dy), ImVec2(left + dx + g.w, top + dy + g.h), uv0, uv1, sc);
        });
    }
    dl->AddImage(tex, ImVec2(left, top), ImVec2(left + g.w, top + g.h), uv0, uv1, rgba(c.color, c.alpha));
}

void render(const Command* cmds, int count, const char* text)
{
    if (pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0)
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    Map m;
    dl->PushClipRect(ImVec2(m.vx, m.vy), ImVec2(m.vx + m.vw, m.vy + m.vh), true);
    const hud::StyleSlot* styles = hud::styles();
    for (int i = 0; i < count; ++i) {
        const Command& c = cmds[i];
        if (c.type == hud::CMD_TEXT)
            draw_text(dl, m, c, styles[c.style].style, text + c.text);
        else if (c.type == hud::CMD_GLYPH)
            draw_glyph(dl, m, c, c.style < HUD_STYLES ? &styles[c.style].style : nullptr);
        else if (c.type == hud::CMD_RECT && c.w > 0 && c.h > 0)
            dl->AddRectFilled(ImVec2(m.x(c.x, c.anchor), m.y(c.y, c.anchor)),
                ImVec2(m.x(c.x, c.anchor) + c.w * m.across(c.anchor), m.y(c.y + c.h, c.anchor)),
                rgba(c.color, c.alpha));
        else if (c.type == hud::CMD_GRADIENT && c.w > 0 && c.h > 0) {
            unsigned a = rgba(c.color, c.alpha), b = rgba(c.color2, c.alpha);
            dl->AddRectFilledMultiColor(ImVec2(m.x(c.x, c.anchor), m.y(c.y, c.anchor)),
                ImVec2(m.x(c.x, c.anchor) + c.w * m.across(c.anchor), m.y(c.y + c.h, c.anchor)), a,
                c.vertical ? a : b, b, c.vertical ? b : a);
        }
    }
    dl->PopClipRect();
}

void measure(const Style& st, const char* s, size_t n, float* w, float* h)
{
    if (!GImGui || !GImGui->WithinFrameScope) { // (an RPC eval between frames: the atlas is not ours then)
        float size = st.cap > 0 ? st.cap / 0.7f : st.size;
        *w = (float)n * size * (0.55f + st.tracking) * st.stretch;
        *h = size;
        return;
    }
    ImFont* font = font_of(st);
    float size = st.cap > 0 ? st.cap / cap_ratio(font) : st.size;
    float px = 64.0f; // measured large, given in the game's pixels
    *w = advance(font, px, s, n, st.tracking) * (size / px) * st.stretch;
    ImFontBaked* baked = font->GetFontBaked(px);
    *h = (baked ? baked->Ascent - baked->Descent : px) * (size / px);
}

// The icon's width in the game's pixels (square: the picture's).
float glyph_width(int button, float h)
{
    if (!GImGui || !GImGui->WithinFrameScope || pc_gl_view[3] <= 0)
        return h;
    float ky = (float)pc_gl_view[3] / 240.0f;
    pc_prompt_image g = pc_prompt_glyph(button, h * ky);
    return g.texture && g.h > 0 ? g.w / ky : 0.0f;
}

struct Register {
    Register()
    {
        hud::render = ::render;
        hud::measure = ::measure;
        hud::glyph_width = ::glyph_width;
    }
} register_renderer;
} // namespace

// ---- the quick menu's own text (weapon_slots.c) ----
// The weapon slots' labels and hint are text the port adds (no baked image behind them). With ui_text font they are
// set in the labels style, the face the L2 overlay's own words (ITEMS, CHAIN, ...) have: its font, stretch, tracking
// and outline come from the style (pc_ui_text_styles, settings ui_text_labels_*), the cap height is the original
// label's (7) times its size, and the colours are qol_weapon_slots_color0..3. weapon_slots.c queues each line as the
// game draws the overlay; the lines are drawn in the next present (pc_weapon_slots_text_draw, dev_overlay.cpp) and
// last a few vsyncs, so a present between two game frames keeps them.
extern "C" {
extern float pc_qol_weapon_slots_text_cap, pc_qol_weapon_slots_text_dy;
extern int pc_qol_weapon_slots_color[4][3];
unsigned pc_vsync_count(void) __attribute__((weak));
}
namespace {
struct Line {
    char text[64];
    int x, y, align, palette;
    unsigned stamp;
};
Line lines[16];
unsigned drawn_total; // presents that drew the lines
int line_count;
unsigned line_stamp;
const unsigned LINE_LIFE = 3; // vsyncs a line stays drawn after the game last set it
unsigned now() { return pc_vsync_count ? pc_vsync_count() : 0; }
// The labels style's typography (ui_text_labels_*) in the HUD renderer's terms.
hud::Style text_style()
{
    const pc_ui_text_style& labels = pc_ui_text_styles[PC_UI_TEXT_LABELS];
    hud::Style st;
    st.font = PC_UI_TEXT_LABELS;
    st.cap = pc_qol_weapon_slots_text_cap * (labels.size > 0.1f ? labels.size : 0.1f);
    st.stroke = labels.outline ? labels.stroke : 0.0f;
    st.stroke_color = 0;
    st.tracking = labels.tracking;
    st.stretch = labels.stretch > 0.25f ? labels.stretch : 1.0f;
    st.anchor = hud::V_MIDDLE;
    return st;
}
} // namespace

// 1 when the line is queued (the caller does not draw the game's glyphs): ui_text font and a picture to draw in.
// align: 0 left, 1 centre, 2 right; palette 0..3 (white, yellow, orange, dim).
extern "C" int pc_weapon_slots_text_add(const char* utf8, int x, int y, int align, int palette)
{
    if (pc_ui_text != PC_UI_TEXT_FONT || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0 || !utf8)
        return 0;
    unsigned t = now();
    if (t != line_stamp) { // a new game frame: the lines of the last one are replaced
        line_count = 0;
        line_stamp = t;
    }
    if (line_count == (int)(sizeof lines / sizeof lines[0]))
        return 1;
    Line& l = lines[line_count++];
    std::snprintf(l.text, sizeof l.text, "%s", utf8);
    l.x = x, l.y = y, l.align = align, l.palette = palette < 0 ? 0 : palette > 3 ? 3 : palette;
    l.stamp = t;
    return 1;
}

extern "C" void pc_weapon_slots_text_draw(void)
{
    if (!line_count || pc_ui_text != PC_UI_TEXT_FONT || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0)
        return;
    if (now() - line_stamp > LINE_LIFE)
        return;
    hud::Style st = text_style();
    ++drawn_total;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    Map m;
    dl->PushClipRect(ImVec2(m.vx, m.vy), ImVec2(m.vx + m.vw, m.vy + m.vh), true);
    for (int i = 0; i < line_count; ++i) {
        const Line& l = lines[i];
        hud::Command c = {};
        c.type = hud::CMD_TEXT;
        c.anchor = 0; // the shortcut overlay is anchored with the centre (BATTLE 5BF94.c)
        c.x = (float)l.x, c.y = (float)l.y + pc_qol_weapon_slots_text_dy;
        c.alpha = 1.0f;
        c.length = (unsigned)std::strlen(l.text);
        const int* rgb = pc_qol_weapon_slots_color[l.palette];
        c.color = (unsigned)(rgb[0] & 255) << 16 | (unsigned)(rgb[1] & 255) << 8 | (unsigned)(rgb[2] & 255);
        st.align = l.align == 1 ? hud::ALIGN_CENTRE : l.align == 2 ? hud::ALIGN_RIGHT : hud::ALIGN_LEFT;
        draw_text(dl, m, c, st, l.text);
    }
    dl->PopClipRect();
}

// RPC weapon_slots: {drawn, font, stretch, tracking, stroke, cap, lines}: the labels-style typography the lines are set in
// now (tests: the same as ui_text's labels style), the lines the game set last, and how many presents drew them. Returns
// the length.
extern "C" int pc_weapon_slots_text_json(char* out, int size)
{
    hud::Style st = text_style();
    int n = std::snprintf(out, (size_t)size,
        "{\"drawn\":%u,\"font\":\"%s\",\"stretch\":%.4f,\"tracking\":%.4f,\"stroke\":%.4f,\"cap\":%.4f,\"lines\":[",
        drawn_total, pc_ui_text_styles[PC_UI_TEXT_LABELS].font, st.stretch, st.tracking, st.stroke, st.cap);
    for (int i = 0; i < line_count && now() - line_stamp <= LINE_LIFE && n < size - 100; ++i)
        n += std::snprintf(out + n, (size_t)(size - n), "%s\"%s\"", i ? "," : "", lines[i].text);
    n += std::snprintf(out + n, (size_t)(size - n), "]}");
    return n;
}
