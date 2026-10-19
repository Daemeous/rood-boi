// The Travel prompt (fast travel, pc/docs/Fast_Travel.md): while Ashley stands
// on a Circle of Magic and the Travel button would open the destinations
// (fast_travel.c pc_fast_travel_prompt), the Travel binding's icon for the
// device used last (pc_prompt_glyph) and "TRAVEL" over the game picture
// (pc_gl_view: not the window's side bars), drawn like the HUD's own words:
// the HUD labels' typography (settings ui_text_labels_font, _stretch) with
// an outline around the verb and the icon, no plate. Its look is settings
// (project_settings.cpp): qol_fast_travel_prompt_size, _stroke,
// _stroke_color, _color, _stretch, _backdrop (the menus' hint plate again:
// _pad_x, _pad_y, _border), and ui_prompt_icon, _gap, _tracking, which the
// menus' hint bar shares. Placed by qol_fast_travel_prompt_anchor (a corner, edge or
// the centre of the picture; the prompt's same corner or edge goes there; 2
// top right) moved by qol_fast_travel_prompt_offset_x / _y (fractions of the
// picture, + right and down), qol_fast_travel_prompt_scale its size. Nothing
// when that device has nothing bound to Travel. Called from dev_overlay.cpp's
// frame.
// Modern UI only: with ui_menus off the classic look keeps the game's own
// button sprites and layout (a classic prompt comes with the action bar).
// The HUD layer (pc/src/hud): each presented frame the prompt shows is a
// "travel" element (pc_fast_travel_prompt_hud, before the HUD pass); while a
// plugin's claim on it is honoured the claim draws it and this does not.
#include "button_prompts.h"
#include "fast_travel.h"
#include "hud/hud.h"
#include "ui/ui_draw_internal.h"
#include "ui_text.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

extern "C" int pc_fast_travel_prompt(void) __attribute__((weak)); // fast_travel.c (battle builds)
extern "C" unsigned pc_fast_travel_prompt_drawn __attribute__((weak));
extern "C" float pc_fast_travel_prompt_box[4] __attribute__((weak)), pc_fast_travel_prompt_picture[4] __attribute__((weak));
extern "C" int pc_fast_travel_prompt_font __attribute__((weak));
extern "C" int pc_fast_travel_prompt_backdrop __attribute__((weak));
extern "C" float pc_fast_travel_prompt_stroke __attribute__((weak));
extern "C" int pc_ui_menus; // project_settings.cpp
extern "C" int pc_gl_view[4];
extern "C" int pc_gte_widescreen;
extern "C" int pc_hud_record(const pc_hud_fields* f) __attribute__((weak)); // hud_elements.cpp

namespace {
using namespace uidraw;

// The verb's font file, loaded into the dynamic atlas as the menus' fonts are (ui_draw.cpp load_font); NULL
// when it is missing.
ImFont* verb_font(const std::string& path)
{
    static ImFont* font;
    static std::string loaded;
    static bool tried;
    if (tried && loaded == path)
        return font;
    tried = true;
    loaded = path;
    ImGuiIO& io = ImGui::GetIO();
    if (font)
        io.Fonts->RemoveFont(font);
    font = nullptr;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        std::fprintf(stderr, "[ui] travel prompt: font %s missing (the menus' label font used)\n", path.c_str());
        return nullptr;
    }
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.Flags |= ImFontFlags_NoLoadError;
    font = io.Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &cfg);
    return font;
}

// Copies at offsets around a circle `r` wide (two rings), for an outline drawn under the fill.
template <class Draw> void outline(float r, Draw draw)
{
    if (r <= 0)
        return;
    const int n = 16;
    for (float ring : {r, r * 0.5f})
        for (int i = 0; i < n; ++i) {
            float a = (float)i * 6.2831853f / n;
            draw(std::cos(a) * ring, std::sin(a) * ring);
        }
}

// The verb with its top-left at (x, y), `stretch` times as wide (its vertices scaled about its left edge).
void verb_text(const Frame& f, ImFont* font, float size, float x, float y, unsigned col, const char* verb,
    float tracking, float stretch)
{
    int start = f.dl->VtxBuffer.Size;
    text(f, font, size, x, y, col, verb, tracking);
    float left = f.p(x, 0).x;
    for (int i = start; i < f.dl->VtxBuffer.Size; ++i)
        f.dl->VtxBuffer[i].pos.x = left + (f.dl->VtxBuffer[i].pos.x - left) * stretch;
}

// A settings colour (r, g, b, a in 0..1) as ImU32.
unsigned rgba(const float c[4])
{
    unsigned out = 0;
    for (int i = 0; i < 4; ++i) {
        float v = std::fmin(std::fmax(c[i], 0.0f), 1.0f);
        out |= (unsigned)(v * 255.0f + 0.5f) << (8 * i);
    }
    return out;
}
float clamped(float v, float lo, float hi) { return std::isfinite(v) ? std::fmin(std::fmax(v, lo), hi) : lo; }

bool claimed_now; // this frame's prompt is a plugin's to draw (pc_fast_travel_prompt_hud)

} // namespace

// The prompt as a HUD element (hud.h): the point of the picture it is anchored to, the picture
// as 320 x 240 (x, y; PC_HUD_ANCHOR_PICTURE), which of its corners goes there (variant 0..8),
// game pixels per layout unit (unit), the button and the verb, the settings' colour.
extern "C" void pc_fast_travel_prompt_fields(pc_hud_fields* f)
{
    int anchor = pc_qol_fast_travel_prompt_anchor < 0 ? 0 : pc_qol_fast_travel_prompt_anchor > 8
        ? 8
        : pc_qol_fast_travel_prompt_anchor;
    float ax = (anchor % 3) * 0.5f, ay = (anchor / 3) * 0.5f;
    f->kind = PC_HUD_TRAVEL;
    std::snprintf(f->text, sizeof f->text, "TRAVEL");
    f->x = 320.0f * (ax + pc_qol_fast_travel_prompt_offset_x);
    f->y = 240.0f * (ay + pc_qol_fast_travel_prompt_offset_y);
    f->variant = anchor;
    f->anchor = PC_HUD_ANCHOR_PICTURE;
    f->glyph = PC_PROMPT_TRAVEL;
    // (the menus' layout: 1280 or 960 x 720 fitted into the picture)
    float W = pc_gte_widescreen ? 1280.0f : 960.0f;
    f->unit = pc_gl_view[2] > 0 && pc_gl_view[3] > 0
        ? std::fmin(pc_gl_view[2] / W, pc_gl_view[3] / 720.0f) * 240.0f / (float)pc_gl_view[3]
        : 1.0f / 3.0f;
    unsigned c = rgba(pc_qol_fast_travel_prompt_color);
    f->rgb = (c & 255u) << 16 | (c & 0xFF00u) | (c >> 16 & 255u);
}

// Before the HUD pass (dev_overlay.cpp): the prompt shown now is recorded; claimed, this frame's
// drawing below is the plugin's.
extern "C" void pc_fast_travel_prompt_hud(void)
{
    claimed_now = false;
    if (!pc_ui_menus || !pc_fast_travel_prompt || !pc_fast_travel_prompt() || !pc_hud_record || pc_gl_view[2] <= 0
        || pc_gl_view[3] <= 0)
        return;
    static int key; // (one prompt)
    static pc_hud_fields f;
    std::memset(&f, 0, sizeof f);
    f.key = &key;
    pc_fast_travel_prompt_fields(&f);
    claimed_now = pc_hud_record(&f) != 0;
}

extern "C" void pc_fast_travel_prompt_draw(void)
{
    if (!pc_ui_menus || !pc_fast_travel_prompt || !pc_fast_travel_prompt() || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0)
        return;
    if (claimed_now)
        return; // a plugin draws it (the HUD pass)
    const ui::Theme& t = ui::theme();
    ensure_fonts(t);
    // the HUD labels' typography (ui_text's labels style)
    const pc_ui_text_style& labels = pc_ui_text_styles[PC_UI_TEXT_LABELS];
    ImFont* font = verb_font(labels.font);
    int font_kind = font ? 1 : 0;
    if (!font)
        font = fonts.label;
    float stretch = pc_qol_fast_travel_prompt_stretch > 0 ? pc_qol_fast_travel_prompt_stretch : labels.stretch;
    stretch = clamped(stretch, 0.25f, 4.0f);
    bool backdrop = pc_qol_fast_travel_prompt_backdrop != 0;
    unsigned color = rgba(pc_qol_fast_travel_prompt_color), stroke_color = rgba(pc_qol_fast_travel_prompt_stroke_color);
    // the menus' layout units (1280 or 960 x 720 fitted into the picture)
    Frame f;
    f.dl = ImGui::GetForegroundDrawList();
    f.W = pc_gte_widescreen ? 1280.0f : 960.0f;
    f.k = std::fmin(pc_gl_view[2] / f.W, pc_gl_view[3] / 720.0f);
    f.ox = f.oy = 0;
    float s = pc_qol_fast_travel_prompt_scale > 0.1f ? pc_qol_fast_travel_prompt_scale : 0.1f;
    float size = clamped(pc_qol_fast_travel_prompt_size, 4, 96) * s;
    float icon = size * clamped(pc_ui_prompt_icon, 0.5f, 4), gap = clamped(pc_ui_prompt_gap, 0, 200) * s;
    float stroke = clamped(pc_qol_fast_travel_prompt_stroke, 0, 20) * s;
    float tracking = clamped(pc_ui_prompt_tracking, -0.5f, 2);
    // the plate's margins, or (no plate) room for the outline
    float pad_x = backdrop ? clamped(pc_qol_fast_travel_prompt_pad_x, 0, 200) * s : stroke;
    float pad_y = backdrop ? clamped(pc_qol_fast_travel_prompt_pad_y, 0, 200) * s : stroke;
    float gw = glyph(f, PC_PROMPT_TRAVEL, icon, 0, 0, true);
    if (gw <= 0)
        return;
    const char* verb = "TRAVEL";
    float vw = measure(f, font, size, verb, tracking) * stretch;
    float w = pad_x + gw + gap + vw + pad_x, h = std::fmax(icon, size) + pad_y * 2;
    // the anchor point in the picture, the prompt's same point on it (window pixels)
    int anchor = pc_qol_fast_travel_prompt_anchor < 0 ? 0 : pc_qol_fast_travel_prompt_anchor > 8 ? 8
                                                                                                  : pc_qol_fast_travel_prompt_anchor;
    float ax = (anchor % 3) * 0.5f, ay = (anchor / 3) * 0.5f;
    float px = pc_gl_view[0] + pc_gl_view[2] * (ax + pc_qol_fast_travel_prompt_offset_x);
    float py = pc_gl_view[1] + pc_gl_view[3] * (ay + pc_qol_fast_travel_prompt_offset_y);
    f.ox = std::round(px - f.s(w) * ax), f.oy = std::round(py - f.s(h) * ay);
    if (backdrop) {
        f.dl->AddRectFilled(f.p(0, 0), f.p(w, h), t.panel);
        float border = clamped(pc_qol_fast_travel_prompt_border, 0, 20);
        if (border > 0)
            f.dl->AddRect(f.p(0, 0), f.p(w, h), t.rule, 0.0f, 0, f.s(border * s));
    }
    float vx = pad_x + gw + gap, vy = (h - size) * 0.5f;
    bool outlined = stroke > 0 && (stroke_color >> 24);
    if (outlined) {
        outline(stroke, [&](float dx, float dy) { glyph(f, PC_PROMPT_TRAVEL, icon, pad_x + dx, h * 0.5f + dy, false,
                                                      stroke_color); });
        outline(stroke, [&](float dx, float dy) {
            verb_text(f, font, size, vx + dx, vy + dy, stroke_color, verb, tracking, stretch);
        });
    }
    glyph(f, PC_PROMPT_TRAVEL, icon, pad_x, h * 0.5f);
    verb_text(f, font, size, vx, vy, color, verb, tracking, stretch);
    if (&pc_fast_travel_prompt_drawn)
        ++pc_fast_travel_prompt_drawn;
    if (&pc_fast_travel_prompt_box) {
        ImVec2 a = f.p(0, 0), b = f.p(w, h);
        float box[4] = { a.x, a.y, b.x, b.y };
        for (int i = 0; i < 4; ++i) {
            pc_fast_travel_prompt_box[i] = box[i];
            pc_fast_travel_prompt_picture[i] = (float)pc_gl_view[i];
        }
        pc_fast_travel_prompt_font = font_kind;
        if (&pc_fast_travel_prompt_backdrop)
            pc_fast_travel_prompt_backdrop = backdrop;
        if (&pc_fast_travel_prompt_stroke)
            pc_fast_travel_prompt_stroke = outlined ? f.s(stroke) : 0.0f;
    }
}

