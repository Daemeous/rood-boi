// The save state slots' notice ("Saved to slot 3", savestate_slots.c) over the game picture: the
// HUD element "notice" (pc/src/hud, hud.h). Each presented frame a notice shows is recorded before
// the HUD pass (pc_savestate_notice_hud); with the modern look and a plugin's claim on it honoured
// (rood.hud) the claim draws it, else this does (pc_savestate_notice_draw): the HUD labels'
// typography (ui_text's labels style), outlined, no plate, centred at the top of the picture below
// the HP and MP row (the game's own HUD keeps the top left), fading out at the end of its life. With the classic look it is not offered to claims (there is no game hook to
// draw it in the classic flavour): this draws it. Called from dev_overlay.cpp's frame.
#include "hud/hud.h"
#include "ui/ui_draw_internal.h"
#include "ui_text.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" int pc_savestate_notice(char* text, int size, int* slot, int* age, int* life) __attribute__((weak));
extern "C" int pc_ui_menus; // project_settings.cpp
extern "C" int pc_gl_view[4];
extern "C" int pc_gte_widescreen;

namespace {
using namespace uidraw;

// The look of the fallback (the menus' layout units, 720 high).
const float NOTICE_SIZE = 26.0f, NOTICE_STROKE = 2.5f, NOTICE_FADE = 30.0f;
const float NOTICE_TOP = 30.0f; // the picture's pixels (240 high): below the HP and MP bars
bool claimed_now;

// Copies at offsets around a circle `r` wide (two rings), for an outline drawn under the fill.
template <class Draw> void outline(float r, Draw draw)
{
    const int n = 16;
    for (float ring : {r, r * 0.5f})
        for (int i = 0; i < n; ++i) {
            float a = (float)i * 6.2831853f / n;
            draw(std::cos(a) * ring, std::sin(a) * ring);
        }
}
} // namespace

// A notice as a HUD element: the picture's point it is anchored to (x, y; the picture as
// 320 x 240), which of its corners goes there (variant 1: its top middle), game pixels per layout unit.
extern "C" void pc_savestate_notice_fields(pc_hud_fields* f)
{
    f->kind = PC_HUD_NOTICE;
    f->anchor = PC_HUD_ANCHOR_PICTURE;
    f->variant = 1;
    float W = pc_gte_widescreen ? 1280.0f : 960.0f;
    f->unit = pc_gl_view[2] > 0 && pc_gl_view[3] > 0
        ? std::fmin(pc_gl_view[2] / W, pc_gl_view[3] / 720.0f) * 240.0f / (float)pc_gl_view[3]
        : 1.0f / 3.0f;
    f->x = 160.0f, f->y = NOTICE_TOP;
    f->rgb = 0xFFFFFF;
}

// Before the HUD pass (dev_overlay.cpp): the notice shown now is recorded; claimed, this frame's
// drawing below is the plugin's.
extern "C" void pc_savestate_notice_hud(void)
{
    claimed_now = false;
    static pc_hud_fields f;
    int slot, age, life;
    char text[PC_HUD_TEXT];
    if (!pc_ui_menus || !pc_savestate_notice || !pc_savestate_notice(text, sizeof text, &slot, &age, &life)
        || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0)
        return;
    static int key; // (one notice)
    std::memset(&f, 0, sizeof f);
    f.key = &key;
    pc_savestate_notice_fields(&f);
    std::snprintf(f.text, sizeof f.text, "%s", text);
    f.value = slot;
    f.age = age, f.life = life;
    claimed_now = pc_hud_record(&f) != 0;
}

extern "C" void pc_savestate_notice_draw(void)
{
    int slot, age, life;
    char text[PC_HUD_TEXT];
    if (claimed_now || !pc_savestate_notice || !pc_savestate_notice(text, sizeof text, &slot, &age, &life)
        || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0)
        return;
    const ui::Theme& t = ui::theme();
    ensure_fonts(t);
    const pc_ui_text_style& labels = pc_ui_text_styles[PC_UI_TEXT_LABELS];
    float stretch = labels.stretch > 0.25f ? labels.stretch : 1.0f;
    Frame f;
    f.dl = ImGui::GetForegroundDrawList();
    f.W = pc_gte_widescreen ? 1280.0f : 960.0f;
    f.k = std::fmin(pc_gl_view[2] / f.W, pc_gl_view[3] / 720.0f);
    f.ox = pc_gl_view[0] + (pc_gl_view[2] - f.W * f.k) * 0.5f, f.oy = (float)pc_gl_view[1];
    float fade = life - age < NOTICE_FADE ? (life - age) / NOTICE_FADE : 1.0f;
    unsigned alpha = (unsigned)(std::fmax(0.0f, std::fmin(1.0f, fade)) * 255.0f + 0.5f) << 24;
    unsigned fill = 0x00FFFFFFu | alpha, edge = 0x00000000u | alpha;
    static char buf[PC_HUD_TEXT];
    std::snprintf(buf, sizeof buf, "%s", text);
    float x = (f.W - uidraw::measure(f, fonts.label, NOTICE_SIZE, buf) * stretch) * 0.5f, y = NOTICE_TOP * 3.0f;
    auto draw = [&](float dx, float dy, unsigned col) {
        int start = f.dl->VtxBuffer.Size;
        uidraw::text(f, fonts.label, NOTICE_SIZE, x + dx, y + dy, col, buf, 0.0f);
        float left = f.p(x + dx, 0).x;
        for (int i = start; i < f.dl->VtxBuffer.Size; ++i)
            f.dl->VtxBuffer[i].pos.x = left + (f.dl->VtxBuffer[i].pos.x - left) * stretch;
    };
    outline(NOTICE_STROKE, [&](float dx, float dy) { draw(dx, dy, edge); });
    draw(0, 0, fill);
}
