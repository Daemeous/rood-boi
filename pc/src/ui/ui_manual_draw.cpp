// Modern menus: the Quick manual's drawing (ui_manual.h), the System tab's
// body while it shows. Left: the topics (the rail, as the System list).
// Right: a card with the topic's title, its line, and the page canvas: the
// original's 300 x 130 content box scaled by k (one original page, ten
// 13-pixel lines), its words placed at the original's x (each word anchored
// where the game's proportional font put it, so tables and the illustrations
// stay aligned), the face-button pictographs as the player's prompts, the
// illustrations as the game's own pixels (nearest-scaled textures; their
// PlayStation button art as the player's prompts on another device), the
// rules in their colours. Below it the page row and a scroll bar. At 4:3 the rail
// steps aside while reading so the canvas keeps the 16:9 scale.
#include "ui/ui_draw_internal.h"
#include "ui/ui_manual.h"
#include "button_prompts.h"
#include "imgui_internal.h" // RegisterUserTexture, ImTextureDataQueueUpload
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace uidraw {
namespace {

const float BOTTOM = 720 - 56;
const float ENTRY_H = 36;
const int TOPICS = PC_UI_MANUAL_TOPICS;
float list_width(float W) { return W < 1100 ? 236.0f : 300.0f; }

// ---- the illustrations: the shown topic's images in one atlas, pre-scaled (nearest) ----
const int ATLAS = 2048;
struct Atlas {
    ImTextureData* tex = nullptr;
    ImGuiContext* owner = nullptr;
    int topic = -1, scale = 0;
    unsigned generation = 0;
    struct Rect {
        int x, y, w, h;
    };
    std::vector<Rect> rects; // per image of the topic (w 0: not packed)
} atlas;

// Pack the topic's images at `scale` into `rects`; false when they do not fit.
bool pack(const pc_ui_manual_topic* t, int scale, std::vector<Atlas::Rect>& rects)
{
    int sx = 0, sy = 0, sh = 0;
    rects.assign((size_t)t->ntex, Atlas::Rect{0, 0, 0, 0});
    for (int i = 0; i < t->ntex; ++i) {
        int w = t->tex_w[i] * scale, h = t->tex_h[i] * scale;
        if (w <= 0 || h <= 0)
            continue;
        if (w > ATLAS)
            return false;
        if (sx + w + 1 > ATLAS)
            sx = 0, sy += sh + 1, sh = 0;
        if (sy + h > ATLAS)
            return false;
        rects[(size_t)i] = {sx, sy, w, h};
        sx += w + 1;
        sh = h > sh ? h : sh;
    }
    return true;
}

// The atlas for topic `topic` at `scale` (rebuilt when either or the bridge's generation changed).
ImTextureData* textures(const pc_ui_manual_source* src, int topic, const pc_ui_manual_topic* t, int scale)
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context || !t || !t->ntex)
        return nullptr;
    if (!atlas.tex || atlas.owner != context) {
        atlas.tex = IM_NEW(ImTextureData)();
        atlas.tex->Create(ImTextureFormat_RGBA32, ATLAS, ATLAS);
        ImGui::RegisterUserTexture(atlas.tex);
        atlas.owner = context;
        atlas.topic = -1;
    }
    unsigned gen = src->generation();
    if (atlas.topic == topic && atlas.scale == scale && atlas.generation == gen)
        return atlas.tex;
    // packed aside: a topic that does not fit leaves the cached atlas (pixels and rects) as it was
    std::vector<Atlas::Rect> rects;
    int s = scale;
    while (s > 1 && !pack(t, s, rects))
        --s;
    if (!pack(t, s, rects))
        return nullptr;
    atlas.rects.swap(rects);
    std::memset(atlas.tex->Pixels, 0, (size_t)atlas.tex->GetSizeInBytes());
    int used = 0;
    for (int i = 0; i < t->ntex; ++i) {
        const Atlas::Rect& r = atlas.rects[(size_t)i];
        if (!r.w)
            continue;
        const unsigned char* src_px = t->rgba[i];
        int w0 = t->tex_w[i];
        for (int y = 0; y < r.h; ++y) {
            unsigned char* row = (unsigned char*)atlas.tex->GetPixelsAt(r.x, r.y + y);
            const unsigned char* in = src_px + (size_t)(y / s) * (size_t)w0 * 4;
            for (int x = 0; x < r.w; ++x)
                std::memcpy(row + x * 4, in + (x / s) * 4, 4);
        }
        used = r.y + r.h > used ? r.y + r.h : used;
    }
    if (atlas.tex->Status != ImTextureStatus_WantCreate)
        ImTextureDataQueueUpload(atlas.tex, 0, 0, ATLAS, used > 0 ? used : 1);
    atlas.topic = topic, atlas.scale = scale, atlas.generation = gen;
    return atlas.tex;
}

// ---- animation and the fitted text size ----
struct Anim {
    float line = 0;      // the drawn scroll (lines), approaching the shell's
    float card_x = -1;   // the card's left (4:3: it slides over the rail while reading)
    int topic = -1, mode = -1;
    int frame = -10; // ImGui's frame count when last drawn
    // the text size fitted per topic, redone when the prompts (their glyph widths) or the bridge's
    // generation change
    int fit_topic = -1;
    unsigned fit_prompts = 0, fit_gen = 0;
    bool fit_pending = false; // a glyph had no width yet: fitted again (twice a second) until it has one
    int fit_frame = 0;
    float fit_size = 0; // in units of k
    int fit_over = 0;
} anim;

// Text sizes are in units of k (the canvas' scale): everything on the canvas scales with it.
const float BASE = 0.8f * 13, FLOOR = 0.64f * 13, PROBE = 20, GLYPH = 11;
const int BUTTONS[4] = {PC_PROMPT_TRIANGLE, PC_PROMPT_SQUARE, PC_PROMPT_CIRCLE, PC_PROMPT_CROSS};
const char* const ARROWS[4] = {"\xe2\x86\x90", "\xe2\x86\x92", "\xe2\x86\x91", "\xe2\x86\x93"};

// The illustrations' button art (PC_UI_MANUAL_ART_*) as the player's prompts.
const int ART_PROMPT[PC_UI_MANUAL_ARTS] = {PC_PROMPT_TRIANGLE, PC_PROMPT_CIRCLE, PC_PROMPT_CROSS, PC_PROMPT_SQUARE,
    PC_PROMPT_L1, PC_PROMPT_L2, PC_PROMPT_R1, PC_PROMPT_R2, PC_PROMPT_START, PC_PROMPT_SELECT, -1};
// The art is the PlayStation pad's: another device's prompts are drawn in its place (the original glyphs and
// a PlayStation pad keep the game's pixels).
bool art_as_prompts()
{
    int device = pc_prompt_device();
    return device == PC_PROMPTS_XBOX || device == PC_PROMPTS_NINTENDO || device == PC_PROMPTS_KEYBOARD;
}
// One glyph centred on (cx, cy), `size` high, no wider than max_w (a wide key cap shrinks to fit); false:
// no glyph (an unbound key).
bool art_glyph(const Frame& f, int button, float size, float max_w, float cx, float cy, unsigned tint)
{
    float w = glyph(f, button, size, 0, 0, true);
    if (w <= 0)
        return false;
    if (w > max_w) {
        size *= max_w / w;
        w = glyph(f, button, size, 0, 0, true);
    }
    glyph(f, button, size, cx - w * 0.5f, cy, false, tint);
    return true;
}
// Button art sprite s (its rect at (x, y), scale k) as the device's prompt(s); false: not drawn, the game's
// pixels stay (no glyph; a pad's D-pad, the same shape on every pad).
bool draw_art(const Frame& f, int art, const pc_ui_manual_sprite& s, float x, float y, float k, unsigned tint)
{
    float cx = x + s.w * k * 0.5f, cy = y + s.h * k * 0.5f;
    if (art == PC_UI_MANUAL_ART_DPAD) {
        if (pc_prompt_device() != PC_PROMPTS_KEYBOARD)
            return false;
        // the four arrows' keys in a cross
        float size = s.h * k * 0.36f, d = s.h * k * 0.32f;
        if (glyph(f, PC_PROMPT_UP, size, 0, 0, true) <= 0 || glyph(f, PC_PROMPT_LEFT, size, 0, 0, true) <= 0
            || glyph(f, PC_PROMPT_RIGHT, size, 0, 0, true) <= 0 || glyph(f, PC_PROMPT_DOWN, size, 0, 0, true) <= 0)
            return false;
        art_glyph(f, PC_PROMPT_UP, size, size * 1.6f, cx, cy - d, tint);
        art_glyph(f, PC_PROMPT_LEFT, size, size * 1.6f, cx - d, cy, tint);
        art_glyph(f, PC_PROMPT_RIGHT, size, size * 1.6f, cx + d, cy, tint);
        art_glyph(f, PC_PROMPT_DOWN, size, size * 1.6f, cx, cy + d, tint);
        return true;
    }
    if (art < 0 || art >= PC_UI_MANUAL_ARTS || ART_PROMPT[art] < 0)
        return false;
    float side = (s.w > s.h ? s.w : s.h) * k;
    return art_glyph(f, ART_PROMPT[art], side * 0.92f, side * 1.5f, cx, cy, tint);
}

// A run's width at text size `size` (canvas k = 1 for the fit).
float run_width(const Frame& f, const pc_ui_manual_source* src, int topic, const pc_ui_manual_run& run, int r,
    float size, float k)
{
    switch (run.kind) {
    case PC_UI_MANUAL_WORD:
        return measure(f, fonts.label, size, src->run_text(topic, r));
    case PC_UI_MANUAL_ARROW:
        return measure(f, fonts.label, size, ARROWS[run.glyph & 3]);
    case PC_UI_MANUAL_GLYPH: {
        float w = glyph(f, BUTTONS[run.glyph & 3], GLYPH * k, 0, 0, true);
        return w > 0 ? w : GLYPH * k;
    }
    default:
        return run.w * k;
    }
}

// The text flows as the reader's font sets it inside a segment (runs joined by a space or touching) and
// each segment starts where the original put it (the line's first run, a column after an advance or a
// wider gap), so sentences read naturally and tables keep their columns. The size is the largest (from
// BASE down to FLOOR) at which every segment ends before the next one (or the box's edge); segments still
// wider at FLOOR are counted.
void fit_text(const Frame& f, const pc_ui_manual_source* src, int topic, const pc_ui_manual_topic* t)
{
    unsigned prompts = pc_button_prompts_generation(), gen = src->generation();
    int frame = ImGui::GetFrameCount();
    if (anim.fit_topic == topic && anim.fit_prompts == prompts && anim.fit_gen == gen
        && !(anim.fit_pending && frame - anim.fit_frame >= 30))
        return;
    bool pending = false;
    float space = measure(f, fonts.label, PROBE, " ") / PROBE, size = BASE;
    int over = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int l = 0; l < t->lines; ++l) {
            int r0 = t->line_first[l], r1 = t->line_first[l + 1];
            for (int a = r0; a < r1;) {
                int b = a + 1;
                while (b < r1 && t->runs[b].gap != PC_UI_MANUAL_GAP_APART)
                    ++b;
                // width = per_size * size + fixed (k = 1)
                float per_size = 0, fixed = 0;
                for (int r = a; r < b; ++r) {
                    const pc_ui_manual_run& run = t->runs[r];
                    if (r > a && run.gap == PC_UI_MANUAL_GAP_SPACE)
                        per_size += space;
                    if (run.kind == PC_UI_MANUAL_WORD || run.kind == PC_UI_MANUAL_ARROW)
                        per_size += run_width(f, src, topic, run, r, PROBE, 1) / PROBE;
                    else
                        fixed += run_width(f, src, topic, run, r, PROBE, 1);
                    if (run.kind == PC_UI_MANUAL_GLYPH && glyph(f, BUTTONS[run.glyph & 3], GLYPH, 0, 0, true) <= 0)
                        pending = true;
                }
                // a segment ends at least half a space (3 original pixels) before the next one
                float room = (b < r1 ? t->runs[b].x - 3.0f : PC_UI_MANUAL_WIDTH_PX - 0.5f) - t->runs[a].x;
                if (pass == 0 && per_size > 0) {
                    float s = (room - fixed) / per_size;
                    if (s < size)
                        size = s < FLOOR ? FLOOR : s;
                } else if (pass == 1 && per_size * size + fixed > room + 0.01f) {
                    ++over;
                }
                a = b;
            }
        }
    anim.fit_topic = topic, anim.fit_size = size, anim.fit_over = over;
    anim.fit_prompts = prompts, anim.fit_gen = gen, anim.fit_pending = pending, anim.fit_frame = frame;
}

unsigned run_colour(const ui::Theme& t, int c)
{
    return c == 0 ? t.bone : c == 1 ? t.gold : c == 2 ? t.accent : t.ash;
}

// A five-pointed star (the manual's note marker) centred at (cx, cy).
void star(const Frame& f, float cx, float cy, float r, unsigned col)
{
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + i * 0.62831853f, rr = i % 2 ? r * 0.42f : r;
        pts[i] = f.p(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
    }
    f.dl->AddConcavePolyFilled(pts, 10, col);
}

void chevron(const Frame& f, float cx, float cy, int dir, unsigned col)
{
    float w = 2.5f * dir, h = 5;
    ImVec2 pts[3] = {f.p(cx - w, cy - h), f.p(cx + w, cy), f.p(cx - w, cy + h)};
    f.dl->AddPolyline(pts, 3, col, 0, f.s(1.6f));
}

struct Hits {
    std::string json = "[";
    void add(const char* kind, int index, float x0, float y0, float x1, float y1)
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s{\"kind\":\"%s\",\"index\":%d,\"x0\":%.1f,\"y0\":%.1f,\"x1\":%.1f,\"y1\":%.1f}",
            json.size() > 1 ? "," : "", kind, index, x0, y0, x1, y1);
        json += buf;
    }
};

} // namespace

void draw_manual(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    pc_ui_manual_view v;
    pc_ui_manual_view_get(&v);
    const pc_ui_manual_source* src = pc_ui_manual_source_get();
    const pc_ui_manual_topic* model = src ? src->model(v.topic) : nullptr;
    bool reading = v.mode == PC_UI_MANUAL_READ;
    bool wide = f.W >= 1100;
    Hits hits;

    // ---- the rail ----
    float lx = t.margin_x, lw = list_width(f.W);
    float rail_cx = lx + lw + 28;
    float target_cx = reading && !wide ? t.margin_x : rail_cx;
    // a first frame (the tab, or the manual, not drawn on the last one): everything in place at once
    int frame = ImGui::GetFrameCount();
    fresh = fresh || anim.frame != frame - 1;
    anim.frame = frame;
    if (fresh)
        anim.card_x = target_cx;
    anim.card_x = approach(anim.card_x, target_cx, dt, t.focus_grow);
    if (std::fabs(anim.card_x - target_cx) < 0.25f)
        anim.card_x = target_cx;
    float rail_a = wide ? 1.0f : (anim.card_x - t.margin_x) / (rail_cx - t.margin_x);
    bool rail_hits = wide || !reading;
    if (rail_a > 0.01f) {
        float y0 = top + 24, eh = ENTRY_H;
        if (y0 + TOPICS * eh > BOTTOM)
            eh = std::fmax(30.0f, (BOTTOM - y0) / TOPICS);
        uidraw::label(f, t, lx + 16, top + 4, "Topics", fade(t.dim, rail_a));
        for (int i = 0; i < TOPICS; ++i) {
            float y = y0 + i * eh;
            bool focus = i == v.topic;
            if (focus && !reading)
                focus_band(f, t, lx, y, lx + lw, y + eh);
            else if (focus)
                f.dl->AddRectFilled(f.p(lx, y + 8), f.p(lx + 2, y + eh - 8), fade(t.accent, rail_a));
            if (rail_hits) {
                m.hit(PC_UI_MOUSE_MANUAL_TOPIC, i, lx, y, lx + lw, y + eh);
                hits.add("topic", i, lx, y, lx + lw, y + eh);
            }
            unsigned c = reading && !focus ? t.ash : t.bone;
            const char* name = src ? src->name(i) : "";
            text(f, fonts.name, 18, lx + 16, y + (eh - 18) * 0.5f - 2, fade(c, rail_a), name);
        }
    }

    // ---- the card ----
    float cx = anim.card_x, cw = f.W - t.margin_x - cx;
    Card card = {cx, top, cw};
    card_begin(f);
    float x0 = cx + 22, x1 = cx + cw - 22;
    const char* title = src ? src->name(v.topic) : "";
    text(f, fonts.name, 25, x0, top + 18, t.bone, title);
    float h = paragraph(f, t, x0, top + 60, x1 - x0, t.ash, src ? src->desc(v.topic) : "");
    float by = top + 60 + h + 14;
    f.dl->AddLine(f.p(x0, by), f.p(x1, by), t.rule, f.s(1));
    float k = std::fmin(2.6f, (x1 - x0) / PC_UI_MANUAL_WIDTH_PX);
    float cwc = PC_UI_MANUAL_WIDTH_PX * k, chc = PC_UI_MANUAL_PAGE_LINES * PC_UI_MANUAL_LINE_PX * k;
    float X = x0 + ((x1 - x0) - cwc) * 0.5f, Y = by + 16;
    float alpha = reading ? 1.0f : 0.7f;

    // the drawn scroll: snaps on a new topic, a fresh screen or a jump of more than a page
    float target = (float)v.line;
    if (fresh || anim.topic != v.topic || std::fabs(anim.line - target) > 10)
        anim.line = target;
    anim.line = approach(anim.line, target, dt, t.focus_grow);
    if (std::fabs(anim.line - target) < 0.002f)
        anim.line = target;
    anim.topic = v.topic, anim.mode = v.mode;
    float al = anim.line;

    float size = 0;
    int over = 0, lc = v.line_count, art = 0;
    bool art_prompts = false;
    f.dl->PushClipRect(f.p(X, Y), f.p(X + cwc, Y + chc), true);
    if (!model) {
        const char* what = v.loaded < 0 ? "Not available" : "Loading\xe2\x80\xa6";
        float w = measure(f, fonts.name, 16, what);
        text(f, fonts.name, 16, X + (cwc - w) * 0.5f, Y + chc * 0.5f - 10, t.dim, what);
    } else {
        fit_text(f, src, v.topic, model);
        size = anim.fit_size * k, over = anim.fit_over;
        float lh = PC_UI_MANUAL_LINE_PX * k;
        // the illustrations (494.c _drawSprite: content y, the page's scroll). The atlas is scaled for the
        // reading layout's k (fixed per width): the 4:3 slide between list and page only resamples on the GPU
        float read_cx = wide ? rail_cx : t.margin_x;
        float k_read = std::fmin(2.6f, (f.W - t.margin_x - read_cx - 44) / PC_UI_MANUAL_WIDTH_PX);
        int scale = (int)std::ceil(std::fmax(k, k_read) * f.k - 0.01f);
        scale = scale < 1 ? 1 : scale > 8 ? 8 : scale;
        ImTextureData* tex = textures(src, v.topic, model, scale);
        unsigned tint = fade(0xFFFFFFFFu, alpha);
        art_prompts = art_as_prompts();
        for (int i = 0; i < model->nsprites; ++i)
            if (model->tex_art && model->sprites[i].tex >= 0 && model->sprites[i].tex < model->ntex
                && model->tex_art[model->sprites[i].tex] >= 0)
                ++art;
        for (int i = 0; tex && i < model->nsprites; ++i) {
            const pc_ui_manual_sprite& s = model->sprites[i];
            if (s.tex < 0 || (size_t)s.tex >= atlas.rects.size() || src->hidden(v.topic, 0, i))
                continue;
            const Atlas::Rect& r = atlas.rects[(size_t)s.tex];
            if (!r.w)
                continue;
            float sx = X + s.x * k, sy = Y + (s.y - al * PC_UI_MANUAL_LINE_PX) * k;
            if (sy > Y + chc || sy + s.h * k < Y)
                continue;
            if (art_prompts && model->tex_art && s.tex < model->ntex && model->tex_art[s.tex] >= 0
                && draw_art(f, model->tex_art[s.tex], s, sx, sy, k, tint))
                continue;
            f.dl->AddImage(tex->GetTexRef(), f.p(sx, sy), f.p(sx + s.w * k, sy + s.h * k),
                ImVec2((float)r.x / ATLAS, (float)r.y / ATLAS), ImVec2((float)(r.x + r.w) / ATLAS, (float)(r.y + r.h) / ATLAS),
                tint);
        }
        // the text: segments where the original put them, the runs of a segment as our font sets them
        float space = measure(f, fonts.label, size, " ");
        int first = (int)std::floor(al) - 1, last = (int)std::ceil(al) + PC_UI_MANUAL_PAGE_LINES + 1;
        for (int l = first < 0 ? 0 : first; l < model->lines && l < last; ++l) {
            float ly = Y + (l - al) * lh, cy = ly + lh * 0.5f, ty = ly + (lh - size) * 0.5f - size * 0.06f;
            float cx_ = X;
            for (int r = model->line_first[l]; r < model->line_first[l + 1]; ++r) {
                const pc_ui_manual_run& run = model->runs[r];
                if (r == model->line_first[l] || run.gap == PC_UI_MANUAL_GAP_APART)
                    cx_ = X + run.x * k;
                else if (run.gap == PC_UI_MANUAL_GAP_SPACE)
                    cx_ += space;
                unsigned col = fade(run_colour(t, run.colour), alpha);
                float w = run_width(f, src, v.topic, run, r, size, k);
                if (run.kind == PC_UI_MANUAL_WORD)
                    text(f, fonts.label, size, cx_, ty, col, src->run_text(v.topic, r));
                else if (run.kind == PC_UI_MANUAL_ARROW)
                    text(f, fonts.label, size, cx_, ty, col, ARROWS[run.glyph & 3]);
                else if (run.kind == PC_UI_MANUAL_GLYPH)
                    glyph(f, BUTTONS[run.glyph & 3], GLYPH * k, cx_, cy, false, tint);
                else if (run.kind == PC_UI_MANUAL_STAR)
                    star(f, cx_ + w * 0.5f, cy, 4.2f * k, fade(t.accent, alpha));
                cx_ += w;
            }
        }
        // the rules (494.c _drawContentLines), in their own colours
        for (int i = 0; i < model->nrules; ++i) {
            const pc_ui_manual_rule& r = model->rules[i];
            if (src->hidden(v.topic, 1, i))
                continue;
            float ya = Y + (r.y0 - al * PC_UI_MANUAL_LINE_PX) * k, yb = Y + (r.y1 - al * PC_UI_MANUAL_LINE_PX) * k;
            // their own colours, a little quieter than the original's full-strength lines
            unsigned col = fade(IM_COL32(r.r, r.g, r.b, 255), alpha * 0.6f);
            f.dl->AddLine(f.p(X + r.x0 * k, ya), f.p(X + r.x1 * k, yb), col, f.s(std::fmax(1.0f, 0.5f * k)));
        }
        // the edges fade where there is more
        unsigned solid = t.panel | 0xFF000000u, clear = t.panel & 0x00FFFFFFu;
        if (al > 0.01f)
            f.dl->AddRectFilledMultiColor(f.p(X, Y), f.p(X + cwc, Y + 16), solid, solid, clear, clear);
        if (al < lc - 0.01f)
            f.dl->AddRectFilledMultiColor(f.p(X, Y + chc - 16), f.p(X + cwc, Y + chc), clear, clear, solid, solid);
    }
    f.dl->PopClipRect();
    if (!reading) { // the preview: a click reads it
        m.hit(PC_UI_MOUSE_MANUAL_CANVAS, 0, X, Y, X + cwc, Y + chc);
        hits.add("canvas", 0, X, Y, X + cwc, Y + chc);
    }

    // the page row: progress at the left, "‹ 3 / 7 ›" at the right
    float py = Y + chc + 14;
    int page = v.line / 10 + 1, pages = lc / 10 + 1;
    float frac = lc > 0 ? al / lc : 1.0f;
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    f.dl->AddRectFilled(f.p(X, py + 9), f.p(X + cwc * 0.5f, py + 11), t.rule);
    f.dl->AddRectFilled(f.p(X, py + 9), f.p(X + cwc * 0.5f * frac, py + 11), t.ash);
    char label[32];
    std::snprintf(label, sizeof label, "%d / %d", page, pages);
    unsigned pc = v.square_held && reading ? t.bone : t.ash;
    float rx = X + cwc;
    bool next_over = reading && m.over(rx - 28, py - 4, rx, py + 24);
    chevron(f, rx - 14, py + 10, 1, next_over ? t.accent : pc);
    float lw2 = measure(f, fonts.data, 16, label);
    text(f, fonts.data, 16, rx - 28 - 6 - lw2, py + 1, pc, label);
    float px0 = rx - 28 - 6 - lw2 - 6 - 28;
    bool prev_over = reading && m.over(px0, py - 4, px0 + 28, py + 24);
    chevron(f, px0 + 14, py + 10, -1, prev_over ? t.accent : pc);
    if (reading && model) {
        m.hit(PC_UI_MOUSE_MANUAL_PAGE, -1, px0, py - 4, px0 + 28, py + 24);
        m.hit(PC_UI_MOUSE_MANUAL_PAGE, 1, rx - 28, py - 4, rx, py + 24);
        hits.add("page", -1, px0, py - 4, px0 + 28, py + 24);
        hits.add("page", 1, rx - 28, py - 4, rx, py + 24);
    }
    // the scroll bar
    if (lc > 0 && model) {
        float tx = x1 + 10, th = chc * 10.0f / (lc + 10.0f);
        float ty = Y + (chc - th) * frac;
        f.dl->AddRectFilled(f.p(tx, Y), f.p(tx + 2, Y + chc), t.rule);
        f.dl->AddRectFilled(f.p(tx, ty), f.p(tx + 2, ty + th), t.ash);
        if (reading) {
            if (m.over(tx - 8, Y, tx + 10, Y + chc)) {
                float u = (m.y - Y - th * 0.5f) / (chc - th);
                u = u < 0 ? 0 : u > 1 ? 1 : u;
                m.hit(PC_UI_MOUSE_MANUAL_SCROLL, (int)std::lround(u * lc), tx - 8, Y, tx + 10, Y + chc);
            }
            hits.add("scroll", lc, tx - 8, Y, tx + 10, Y + chc);
        }
    }
    card_end(f, t, card, py + 40);
    hits.json += "]";
    pc_ui_manual_set_draw(k, size, over, art, art_prompts, hits.json.c_str());

    // footer
    out.crumb = "System > Quick manual";
    if (*title)
        out.crumb += std::string(" > ") + title; // the focused topic, as the other lists name their row
    if (reading) {
        out.hint(PC_PROMPT_UP, PC_PROMPT_DOWN, "Scroll", PC_UI_DOWN);
        out.hint(PC_PROMPT_LEFT, PC_PROMPT_RIGHT, "Page", PC_UI_RIGHT);
    } else {
        out.hint(PC_PROMPT_CIRCLE, -1, "Read", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
    }
    out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
    out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
}

} // namespace uidraw
