// The HUD layer's renderer (hud_internal.h): the command buffer claims filled
// (lua_api_hud.cpp) drawn over the presented picture (pc_gl_view: not the
// window's side bars) in the foreground draw list, under the modern menus
// (dev_overlay.cpp calls pc_hud_frame before them). Coordinates are the
// game's 320 x 240 screen; in widescreen the game's 2D is drawn 3/4 as wide
// about the screen's centre, or about the edge its element is anchored to
// (pc_hw_anchor_packets), and so are these. Text: a ui_text style's font
// file loaded into the dynamic atlas (as the Travel prompt's,
// fast_travel_prompt.cpp), letter-spaced, stretched, and outlined by copies
// around two rings under the fill.
#include "hud/hud_internal.h"
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

// The game's screen in the window.
struct Map {
    float vx, vy, vw, vh, kx, ky;
    bool wide;
    Map()
    {
        vx = (float)pc_gl_view[0], vy = (float)pc_gl_view[1], vw = (float)pc_gl_view[2], vh = (float)pc_gl_view[3];
        wide = pc_gte_widescreen != 0;
        kx = vw / 320.0f * (wide ? 0.75f : 1.0f);
        ky = vh / 240.0f;
    }
    float x(float gx, int anchor) const
    {
        float f = gx / 320.0f;
        if (wide)
            f = anchor == 1 ? f * 0.75f : anchor == 2 ? 1.0f - (1.0f - f) * 0.75f : 0.5f + (f - 0.5f) * 0.75f;
        return vx + f * vw;
    }
    float y(float gy) const { return vy + gy / 240.0f * vh; }
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

void draw_text(ImDrawList* dl, const Map& m, const Command& c, const Style& st, const char* s)
{
    ImFont* font = font_of(st);
    float px = st.size * m.ky;
    if (px < 1)
        return;
    float w = advance(font, px, s, c.length, st.tracking); // at ky in both directions
    float squeeze = st.stretch * m.kx / m.ky;              // then across: the stretch and widescreen's 3/4
    float gw = w / m.ky * st.stretch;                       // its width in the game's pixels
    float gx = c.x - (st.align == hud::ALIGN_CENTRE ? gw * 0.5f : st.align == hud::ALIGN_RIGHT ? gw : 0.0f);
    float left = m.x(gx, c.anchor), top = m.y(c.y);
    ImFontBaked* baked = font->GetFontBaked(px);
    float ascent = baked ? baked->Ascent : px * 0.8f, line = baked ? baked->Ascent - baked->Descent : px;
    // middle: the capitals' middle (an H's ink), not the line's
    ImFontGlyph* cap = baked ? baked->FindGlyphNoFallback('H') : nullptr;
    float middle = cap && cap->Y1 > cap->Y0 ? (cap->Y0 + cap->Y1) * 0.5f : line * 0.5f;
    top -= st.anchor == hud::V_MIDDLE ? middle : st.anchor == hud::V_BASELINE ? ascent
        : st.anchor == hud::V_BOTTOM                ? line
                                                    : 0.0f;
    left = std::round(left), top = std::round(top);
    int start = dl->VtxBuffer.Size;
    float r = st.stroke * m.ky;
    if (r > 0 && st.stroke_alpha > 0) {
        unsigned sc = rgba(st.stroke_color, st.stroke_alpha * c.alpha);
        const int n = 16;
        for (float ring : { r, r * 0.5f })
            for (int i = 0; i < n; ++i) {
                float a = (float)i * 6.2831853f / n;
                letters(dl, font, px, left + std::cos(a) * ring / squeeze, top + std::sin(a) * ring, sc, s, c.length,
                    st.tracking);
            }
    }
    letters(dl, font, px, left, top, rgba(c.color, c.alpha), s, c.length, st.tracking);
    for (int i = start; i < dl->VtxBuffer.Size; ++i)
        dl->VtxBuffer[i].pos.x = left + (dl->VtxBuffer[i].pos.x - left) * squeeze;
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
        else if (c.type == hud::CMD_RECT && c.w > 0 && c.h > 0)
            dl->AddRectFilled(ImVec2(m.x(c.x, c.anchor), m.y(c.y)), ImVec2(m.x(c.x, c.anchor) + c.w * m.kx,
                                  m.y(c.y + c.h)), rgba(c.color, c.alpha));
    }
    dl->PopClipRect();
}

void measure(const Style& st, const char* s, size_t n, float* w, float* h)
{
    if (!GImGui || !GImGui->WithinFrameScope) { // (an RPC eval between frames: the atlas is not ours then)
        *w = (float)n * st.size * (0.55f + st.tracking) * st.stretch;
        *h = st.size;
        return;
    }
    ImFont* font = font_of(st);
    float px = 64.0f; // measured large, given in the game's pixels
    *w = advance(font, px, s, n, st.tracking) * (st.size / px) * st.stretch;
    ImFontBaked* baked = font->GetFontBaked(px);
    *h = (baked ? baked->Ascent - baked->Descent : px) * (st.size / px);
}

struct Register {
    Register()
    {
        hud::render = ::render;
        hud::measure = ::measure;
    }
} register_renderer;
} // namespace
