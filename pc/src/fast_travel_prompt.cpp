// The Travel prompt (fast travel, pc/docs/Fast_Travel.md): while Ashley stands
// on a Circle of Magic and the Travel button would open the destinations
// (fast_travel.c pc_fast_travel_prompt), the Travel binding's icon for the
// device used last (pc_prompt_glyph) and "TRAVEL" on a small plate over the
// game picture (pc_gl_view: not the window's side bars), drawn as the modern
// menus draw their button hints (ui/ui_draw_internal.h: the theme's label
// font, letter-spaced capitals, a panel with its rule). Placed by
// qol_fast_travel_prompt_anchor (a corner, edge or the centre of the
// picture; the plate's same corner or edge goes there; 2 top right) moved by
// qol_fast_travel_prompt_offset_x / _y (fractions of the picture, + right and
// down), qol_fast_travel_prompt_scale its size (1: the menus' hints). Nothing
// when that device has nothing bound to Travel. Called from dev_overlay.cpp's
// frame.
// Modern UI only: with ui_menus off the classic look keeps the game's own
// button sprites and layout (a classic prompt comes with the action bar).
#include "button_prompts.h"
#include "fast_travel.h"
#include "ui/ui_draw_internal.h"
#include <cmath>

extern "C" int pc_fast_travel_prompt(void) __attribute__((weak)); // fast_travel.c (battle builds)
extern "C" unsigned pc_fast_travel_prompt_drawn __attribute__((weak));
extern "C" float pc_fast_travel_prompt_box[4] __attribute__((weak)), pc_fast_travel_prompt_picture[4] __attribute__((weak));
extern "C" int pc_fast_travel_prompt_font __attribute__((weak));
extern "C" int pc_ui_menus; // project_settings.cpp
extern "C" int pc_gl_view[4];
extern "C" int pc_gte_widescreen;

extern "C" void pc_fast_travel_prompt_draw(void)
{
    using namespace uidraw;
    if (!pc_ui_menus || !pc_fast_travel_prompt || !pc_fast_travel_prompt() || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0)
        return;
    const ui::Theme& t = ui::theme();
    ensure_fonts(t);
    // the menus' layout units (1280 or 960 x 720 fitted into the picture), so the plate is a hint's size
    Frame f;
    f.dl = ImGui::GetForegroundDrawList();
    f.W = pc_gte_widescreen ? 1280.0f : 960.0f;
    f.k = std::fmin(pc_gl_view[2] / f.W, pc_gl_view[3] / 720.0f);
    f.ox = f.oy = 0;
    float s = pc_qol_fast_travel_prompt_scale > 0.1f ? pc_qol_fast_travel_prompt_scale : 0.1f;
    float size = t.hint_size * s, icon = t.hint_size * 1.6f * s;
    float pad_x = 12 * s, pad_y = 6 * s, gap = 9 * s;
    float gw = glyph(f, PC_PROMPT_TRAVEL, icon, 0, 0, true);
    if (gw <= 0)
        return;
    const char* verb = "TRAVEL";
    float vw = measure(f, fonts.label, size, verb, 0.22f);
    float w = pad_x + gw + gap + vw + pad_x, h = icon + pad_y * 2;
    // the anchor point in the picture, the plate's same point on it (window pixels)
    int anchor = pc_qol_fast_travel_prompt_anchor < 0 ? 0 : pc_qol_fast_travel_prompt_anchor > 8 ? 8
                                                                                                  : pc_qol_fast_travel_prompt_anchor;
    float ax = (anchor % 3) * 0.5f, ay = (anchor / 3) * 0.5f;
    float px = pc_gl_view[0] + pc_gl_view[2] * (ax + pc_qol_fast_travel_prompt_offset_x);
    float py = pc_gl_view[1] + pc_gl_view[3] * (ay + pc_qol_fast_travel_prompt_offset_y);
    f.ox = std::round(px - f.s(w) * ax), f.oy = std::round(py - f.s(h) * ay);
    f.dl->AddRectFilled(f.p(0, 0), f.p(w, h), t.panel);
    f.dl->AddRect(f.p(0, 0), f.p(w, h), t.rule, 0.0f, 0, f.s(1));
    glyph(f, PC_PROMPT_TRAVEL, icon, pad_x, h * 0.5f);
    text(f, fonts.label, size, pad_x + gw + gap, (h - size) * 0.5f, t.bone, verb, 0.22f);
    if (&pc_fast_travel_prompt_drawn)
        ++pc_fast_travel_prompt_drawn;
    if (&pc_fast_travel_prompt_box) {
        ImVec2 a = f.p(0, 0), b = f.p(w, h);
        float box[4] = { a.x, a.y, b.x, b.y };
        for (int i = 0; i < 4; ++i) {
            pc_fast_travel_prompt_box[i] = box[i];
            pc_fast_travel_prompt_picture[i] = (float)pc_gl_view[i];
        }
        pc_fast_travel_prompt_font = fonts.label != nullptr;
    }
}
