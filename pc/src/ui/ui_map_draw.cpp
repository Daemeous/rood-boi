// Modern menus, the Map frame's chrome (map_frame.h): the game's map screen
// (MENU5) shows through a cut-out (pc_ui_map_column: its 3D map, markers,
// labels, the map backdrop and the paling warp); around it, opaque, the
// tabs (Leá Monde lit), a header (area, room, badges), the rail (compass,
// completion, the area list with the shown area's rooms, the legend) and the
// prompts. Layout units 960 x 720 (4:3) or 1280 x 720 (16:9); game x, y map to
// W/2 + 3 (x - 160), 3 y.
#include "ui/ui_draw_internal.h"
#include "ui/map_frame.h"
#include "button_prompts.h"
#include "camera_stick.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

extern "C" int pc_gl_view[4];
extern "C" int pc_qol_map_controls; // project_settings.cpp: 0 Modern, 1 Original (map_pad.h)

namespace uidraw {
namespace {

const float PI = 3.14159265f;
const int ROOM_ROWS = 5; // the shown area's rooms nested in the rail at once

// Drags over the picture: the mouse's, and test pointers sent with a button
// down (one drag per source and button).
struct Drag {
    bool on = false;
    bool pan = false; // a left drag started with Shift: it pans
    float x = 0, y = 0;
};
Drag drags[2][2]; // [rpc][left, middle]
float chrome = 1;  // the rail, header and prompts' strength (Select hides them)
int shown_room = -1, shown_scene = -1;

unsigned rgb(int r, int g, int b, float a = 1) { return fade(0xFF000000u | (b << 16) | (g << 8) | r, a); }

// The game's map colours (4D8.c _renderRoom): rooms' fill and the markers' crosses.
const unsigned ROOM_SELECTED = rgb(0, 128, 64), ROOM_OTHER = rgb(0, 64, 128), EDGE_SELECTED = rgb(0, 255, 128);
const unsigned EDGE_OTHER = rgb(0, 128, 255), DOOR = rgb(192, 192, 192), WAY_IN = rgb(255, 0, 128);
const unsigned EXIT = rgb(0, 192, 192), MARK = rgb(160, 160, 0);

void cross(const Frame& f, float cx, float cy, float r, unsigned col)
{
    f.dl->AddLine(f.p(cx - r, cy), f.p(cx + r + 1, cy), col, f.s(2));
    f.dl->AddLine(f.p(cx, cy - r), f.p(cx, cy + r + 1), col, f.s(2));
}

void padlock(const Frame& f, float cx, float cy, unsigned col)
{
    f.dl->AddRectFilled(f.p(cx - 6, cy - 1), f.p(cx + 6, cy + 8), col, f.s(1.5f));
    f.dl->PathArcTo(f.p(cx, cy - 1), f.s(4), PI, 2 * PI, 10);
    f.dl->PathStroke(col, 0, f.s(2));
}

void room_swatch(const Frame& f, float cx, float cy, unsigned fill, unsigned edge)
{
    ImVec2 a = f.p(cx - 9, cy - 5), b = f.p(cx + 3, cy - 8), c = f.p(cx + 9, cy - 1), d = f.p(cx - 3, cy + 3);
    f.dl->AddQuadFilled(a, b, c, d, fill);
    f.dl->AddQuad(a, b, c, d, edge, f.s(1.2f));
}

// A pointer target: hit-tested and recorded (ui_menu JSON "hits").
bool hit(Pointer& m, int kind, int index, float x0, float y0, float x1, float y1)
{
    pc_ui_map_hit_record(kind, index, x0, y0, x1, y1);
    return m.hit(kind, index, x0, y0, x1, y1);
}

struct Layout {
    float W;
    bool wide;
    float rail0, rail1;      // the rail's x range
    float list0, list1;      // the area list's y range
    float legend_x0, legend_x1, legend_y0, legend_y1;
    float col[4];            // the cut-out
};

// The rail and the legend start at the theme's margin, as every tab's
// columns do (under the tab rule); the cut-out is fixed by the picture.
Layout layout(float W, const ui::Theme& t)
{
    Layout L;
    L.W = W;
    L.wide = W >= 1100;
    pc_ui_map_column_for(W, L.col);
    if (L.wide) {
        L.rail0 = t.margin_x, L.rail1 = 424;
        L.list0 = 258, L.list1 = 648;
        L.legend_x0 = L.col[0] + L.col[2] + 16, L.legend_x1 = W - t.margin_x, L.legend_y0 = 160, L.legend_y1 = 400;
    } else {
        L.rail0 = t.margin_x, L.rail1 = 272;
        L.list0 = 258, L.list1 = 528; // nine area rows
        L.legend_x0 = t.margin_x, L.legend_x1 = 272, L.legend_y0 = 538, L.legend_y1 = 648;
    }
    return L;
}

// The compass (4D8.c _renderCompass, 4096 units a turn): the ring turns with
// the view's yaw, the needle points where the field camera looked when the
// map opened (yaw - yaw0).
void compass(const Frame& f, const ui::Theme& t, float cx, float cy, float r, int yaw, int yaw0, float a)
{
    f.dl->AddCircle(f.p(cx, cy), f.s(r), fade(t.rule2, a), 48, f.s(1.5f));
    f.dl->AddCircle(f.p(cx, cy), f.s(r - 7), fade(t.rule, a), 48, f.s(1));
    for (int i = 0; i < 16; ++i) {
        float th = (yaw + i * 256) * 2 * PI / 4096.0f, s = std::sin(th), c = std::cos(th);
        float r0 = i % 4 == 0 ? r - 9 : r - 5;
        f.dl->AddLine(f.p(cx + s * r0, cy - c * r0), f.p(cx + s * r, cy - c * r), fade(i == 0 ? t.accent : t.ash, a),
            f.s(i % 4 == 0 ? 2.0f : 1.0f));
    }
    float th = yaw * 2 * PI / 4096.0f;
    text(f, fonts.label, 11, cx + std::sin(th) * (r + 9) - 3.5f, cy - std::cos(th) * (r + 9) - 7, fade(t.bone, a), "N");
    float tn = (yaw - yaw0) * 2 * PI / 4096.0f, sn = std::sin(tn), cn = std::cos(tn);
    ImVec2 tip = f.p(cx + sn * (r - 12), cy - cn * (r - 12));
    ImVec2 l = f.p(cx - cn * 5, cy - sn * 5), rr = f.p(cx + cn * 5, cy + sn * 5);
    ImVec2 tail = f.p(cx - sn * (r - 20), cy + cn * (r - 20));
    f.dl->AddTriangleFilled(tip, l, rr, fade(t.gold, a));
    f.dl->AddTriangleFilled(tail, rr, l, fade(t.dim, a));
}

// The sticks have no button glyph (PC_PROMPT_*): a stick's top seen from
// above, its letter on it (L / R), in the prompts' tint.
enum { STICK_L = 1000, STICK_R };
float stick_glyph(const Frame& f, int which, float size, float x, float cy, bool measure_only, unsigned tint)
{
    float w = size * 0.92f;
    if (measure_only)
        return w;
    float cx = x + w * 0.5f, r = size * 0.4f;
    unsigned alpha = tint >> 24;
    f.dl->AddCircleFilled(f.p(cx, cy), f.s(r), (tint & 0x00FFFFFFu) | ((alpha * 2 / 7) << 24), 24);
    f.dl->AddCircle(f.p(cx, cy), f.s(r), tint, 24, f.s(1.4f));
    f.dl->AddCircle(f.p(cx, cy), f.s(r * 0.62f), (tint & 0x00FFFFFFu) | ((alpha / 2) << 24), 24, f.s(1));
    const char* letter = which == STICK_L ? "L" : "R";
    float ts = size * 0.42f, tw = measure(f, fonts.label, ts, letter);
    text(f, fonts.label, ts, cx - tw * 0.5f, cy - ts * 0.5f - 1, tint, letter);
    return w;
}
// The keyboard's camera keys (boot_window.c camera_keybit: J / L turn, I / K
// tilt) and the arrows (the D-pad: pan) as key caps in the prompts' tint:
// KEY_CAP + the letter, KEY_ARROWS the four arrows on one cap.
enum { KEY_CAP = 1100, KEY_ARROWS = 1300 };
float key_glyph(const Frame& f, int b, float size, float x, float cy, bool measure_only, unsigned tint)
{
    float w = size * 0.82f;
    if (measure_only)
        return w;
    float h = size * 0.82f, y0 = cy - h * 0.5f;
    unsigned alpha = tint >> 24;
    f.dl->AddRectFilled(f.p(x, y0), f.p(x + w, y0 + h), (tint & 0x00FFFFFFu) | ((alpha * 2 / 7) << 24), f.s(3));
    f.dl->AddRect(f.p(x, y0), f.p(x + w, y0 + h), tint, f.s(3), 0, f.s(1.2f));
    float cx = x + w * 0.5f;
    if (b == KEY_ARROWS) {
        float r = w * 0.32f, k = w * 0.11f;
        f.dl->AddTriangleFilled(f.p(cx, cy - r - k), f.p(cx - k, cy - r + k), f.p(cx + k, cy - r + k), tint);
        f.dl->AddTriangleFilled(f.p(cx, cy + r + k), f.p(cx + k, cy + r - k), f.p(cx - k, cy + r - k), tint);
        f.dl->AddTriangleFilled(f.p(cx - r - k, cy), f.p(cx - r + k, cy + k), f.p(cx - r + k, cy - k), tint);
        f.dl->AddTriangleFilled(f.p(cx + r + k, cy), f.p(cx + r - k, cy - k), f.p(cx + r - k, cy + k), tint);
        return w;
    }
    char letter[2] = {(char)(b - KEY_CAP), 0};
    float ts = size * 0.46f, tw = measure(f, fonts.data, ts, letter);
    text(f, fonts.data, ts, cx - tw * 0.5f, cy - ts * 0.5f - 1, tint, letter);
    return w;
}
float map_glyph(const Frame& f, int b, float size, float x, float cy, bool measure_only, unsigned tint = 0xFFFFFFFFu)
{
    if (b >= KEY_CAP)
        return key_glyph(f, b, size, x, cy, measure_only, tint);
    return b >= STICK_L ? stick_glyph(f, b, size, x, cy, measure_only, tint)
                        : glyph(f, b, size, x, cy, measure_only, tint);
}

// A prompt: glyph(s) and a verb; returns its width (measure only when !draw).
// The glyphs fade with the verb; an unlit prompt's are dimmed as its verb.
float prompt(const Frame& f, const ui::Theme& t, float x, float cy, int b0, int b1, const char* verb, unsigned col,
    bool draw = true, bool lit = true)
{
    float icon = t.hint_size * 1.6f, w = 0;
    unsigned tint = (lit ? 0x00FFFFFFu : (t.dim & 0x00FFFFFFu)) | (col & 0xFF000000u);
    w += map_glyph(f, b0, icon, x, cy, !draw, tint);
    if (b1 >= 0)
        w += 2 + map_glyph(f, b1, icon, x + w + 2, cy, !draw, tint);
    std::string v = upper(verb);
    w += 6;
    if (draw)
        text(f, fonts.label, t.hint_size, x + w, cy - t.hint_size * 0.5f - 1, col, v.c_str(), 0.22f);
    w += measure(f, fonts.label, t.hint_size, v.c_str(), 0.22f);
    return w;
}

struct Prompt {
    int b0, b1;
    const char* verb;
    int action;  // PC_UI_MAP_P_* (b0's half when action2 is set); -1: not a target
    bool lit;
    int action2; // b1's half (PC_UI_MAP_P_*), -1: one target
};

// The prompts row, right-aligned at x1 as the other screens' hints (a pair's
// halves are their own targets); returns its left edge. An unlit prompt
// stays clickable: MENU5 ignores what it would ignore from the pad.
float prompts(const Frame& f, const ui::Theme& t, float x0, float x1, float cy, const Prompt* ps, int n, Pointer& m,
    float a)
{
    float total = 0, gap = 22, ws[10];
    for (int i = 0; i < n; ++i)
        total += ws[i] = prompt(f, t, 0, cy, ps[i].b0, ps[i].b1, ps[i].verb, 0, false);
    if (n > 1 && total + gap * (n - 1) > x1 - x0) // too wide: the gaps give way first
        gap = std::fmax(8.0f, (x1 - x0 - total) / (n - 1));
    total += gap * (n - 1);
    float x = std::fmax(x0, x1 - total), left = x;
    for (int i = 0; i < n; ++i) {
        const Prompt& p = ps[i];
        float w = ws[i];
        bool over = false;
        if (p.action2 >= 0) { // L1 | R1, Up | Down: each half its own
            float half = map_glyph(f, p.b0, t.hint_size * 1.6f, 0, 0, true) + 1;
            over = m.over(x - 4, cy - 14, x + w + 4, cy + 14);
            hit(m, PC_UI_MOUSE_MAP_PROMPT, p.action, x - 4, cy - 14, x + half, cy + 14);
            hit(m, PC_UI_MOUSE_MAP_PROMPT, p.action2, x + half, cy - 14, x + w + 4, cy + 14);
        } else if (p.action >= 0) {
            over = hit(m, PC_UI_MOUSE_MAP_PROMPT, p.action, x - 4, cy - 14, x + w + 4, cy + 14);
        }
        unsigned col = over ? (p.lit ? t.bone : t.ash) : p.lit ? t.ash : t.dim;
        prompt(f, t, x, cy, p.b0, p.b1, p.verb, fade(col, a), true, p.lit);
        x += w + gap;
    }
    return left;
}

// A hint right-aligned at xr: glyphs and words in a row (b -1: the word).
float hint_right(const Frame& f, const ui::Theme& t, float xr, float cy, const int* bs, const char* const* words,
    int n, unsigned col)
{
    float icon = 16, w = 0, ws[8];
    for (int i = 0; i < n; ++i)
        w += ws[i] = (bs[i] >= 0 ? glyph(f, bs[i], icon, 0, 0, true) : measure(f, fonts.label, 10, words[i], 0.22f)) + 4;
    float x = xr - w + 4;
    unsigned tint = 0x00FFFFFFu | (col & 0xFF000000u);
    for (int i = 0; i < n; ++i) {
        if (bs[i] >= 0)
            glyph(f, bs[i], icon, x, cy, false, tint);
        else
            text(f, fonts.label, 10, x, cy - 6, col, words[i], 0.22f);
        x += ws[i];
    }
    return w;
}

} // namespace

void draw_map_frame(const Frame& f, const ui::Theme& t, float dt, Pointer& m)
{
    const pc_ui_map_model& mm = *pc_ui_map_state();
    Layout L = layout(f.W, t);
    pc_ui_map_hits_clear();
    float cx0 = L.col[0], cy0 = L.col[1], cx1 = L.col[0] + L.col[2], cy1 = L.col[1] + L.col[3];
    bool loading = !mm.framed || !mm.published || mm.exec_state < 7 || mm.menu_state == 0;
    bool closing = mm.framed && (mm.finished || mm.exec_state >= 8 || mm.menu_state == 3);
    bool list = mm.list.open && mm.menu_state == 2;
    bool switching = mm.menu_state == 4 || mm.menu_state == 5;
    bool hidden = mm.hide_ui && !loading && !closing && !list;
    chrome = approach(chrome, hidden ? 0.0f : 1.0f, dt, 0.12f);
    if (chrome < 0.02f || chrome > 0.98f) // settled: nothing left half-drawn
        chrome = hidden ? 0.0f : 1.0f;
    float a = chrome;
    unsigned solid = t.ground | 0xFF000000u;

    // backdrop: opaque around the cut-out (the whole picture while loading
    // or closing); hidden: the cut-out opens to the full picture
    ImVec2 v0((float)pc_gl_view[0], (float)pc_gl_view[1]);
    ImVec2 v1((float)(pc_gl_view[0] + pc_gl_view[2]), (float)(pc_gl_view[1] + pc_gl_view[3]));
    ImVec2 c0 = f.p(cx0, cy0), c1 = f.p(cx1, cy1);
    c0 = ImVec2(std::floor(c0.x), std::floor(c0.y)), c1 = ImVec2(std::floor(c1.x), std::floor(c1.y));
    if (loading || closing) {
        f.dl->AddRectFilled(v0, v1, solid);
        a = 1;
    } else if (a > 0.001f) {
        unsigned s = fade(solid, a);
        f.dl->AddRectFilled(v0, ImVec2(v1.x, c0.y), s);
        f.dl->AddRectFilled(ImVec2(v0.x, c1.y), v1, s);
        f.dl->AddRectFilled(ImVec2(v0.x, c0.y), ImVec2(c0.x, c1.y), s);
        f.dl->AddRectFilled(ImVec2(c1.x, c0.y), ImVec2(v1.x, c1.y), s);
        // the cut-out's rule and corner ticks
        f.dl->AddRect(f.p(cx0 - 1, cy0 - 1), f.p(cx1 + 1, cy1 + 1), fade(t.rule2, a), 0, 0, f.s(1));
        unsigned tick = fade(t.accent, a);
        float k = 14;
        f.dl->AddLine(f.p(cx0 - 1, cy0 - 1), f.p(cx0 + k, cy0 - 1), tick, f.s(2));
        f.dl->AddLine(f.p(cx0 - 1, cy0 - 1), f.p(cx0 - 1, cy0 + k), tick, f.s(2));
        f.dl->AddLine(f.p(cx1 + 1, cy1 + 1), f.p(cx1 - k, cy1 + 1), tick, f.s(2));
        f.dl->AddLine(f.p(cx1 + 1, cy1 + 1), f.p(cx1 + 1, cy1 - k), tick, f.s(2));
    }

    // the tabs (Leá Monde lit: the tab the map belongs to). The pad's L1 / R1
    // step rooms on the map and tabs in the area list: the strip's L1 / R1
    // icons are lit there only, and the hint under the strip says so.
    if (a > 0.5f) {
        float base = draw_tabs(f, t, pc_ui_shell_tab(), m, dt, true, list);
        if (list) {
            const int bs[] = {PC_PROMPT_L1, PC_PROMPT_R1, -1};
            const char* const ws[] = {"", "", "TABS"};
            hint_right(f, t, f.W - t.margin_x, base + 13, bs, ws, 3, fade(t.dim, a));
        } else {
            const int bs[] = {PC_PROMPT_CIRCLE, -1, PC_PROMPT_L1, PC_PROMPT_R1, -1};
            const char* const ws[] = {"", "\xe2\x80\xba", "", "", "TABS"};
            hint_right(f, t, f.W - t.margin_x, base + 13, bs, ws, 5, fade(t.dim, a));
        }
    }

    if (loading || closing) {
        float cx = (cx0 + cx1) * 0.5f, cy = (cy0 + cy1) * 0.5f;
        const char* what = closing ? "Closing" : "Loading map\xe2\x80\xa6";
        float w = measure(f, fonts.name, 22, what);
        spinner(f, cx - w * 0.5f - 18, cy + 11, 6, t.gold);
        text(f, fonts.name, 22, cx - w * 0.5f, cy, t.ash, what);
    }

    // the header: the area (and whether Ashley is in it), its badge; the room
    const pc_ui_map_area* area = nullptr;
    for (int i = 0; i < mm.area_count; ++i)
        if (mm.areas[i].id == mm.scene)
            area = &mm.areas[i];
    if (!loading && a > 0.001f) {
        float hy = 102, width = cx1 - cx0, gap = 24, aw = 16;
        const char* name = area ? area->name : "";
        const char* badge = closing ? nullptr : switching ? "SWITCHING" : mm.paled ? "JAMMED" : nullptr;
        float bw = badge ? measure(f, fonts.label, 10, badge, 0.26f) : 0, badge_w = badge ? bw + 24 : 0;
        bool here = mm.current_scene && mm.scene == mm.current_scene;
        float here_w = here ? 13 + measure(f, fonts.label, t.label_size, upper("Ashley is here").c_str(), 0.26f) : 0;
        float name_w = measure(f, fonts.name, 26, name);
        char seen[48];
        std::snprintf(seen, sizeof seen, "ROOMS SEEN %d / %d", mm.rooms_seen, mm.room_count);
        float seen_w = measure(f, fonts.label, 10, seen, 0.24f);
        // right first: < room > (the paling's line when jammed), cut to what
        // leaves the area name at least 40% of the width (or all it needs)
        bool room_row = !mm.paled && mm.room >= 0 && mm.room < mm.room_count;
        float right_max = width - (std::fmin(name_w, width * 0.4f) + 14 + badge_w) - gap;
        std::string rtext;
        float right_w = 0;
        if (mm.paled) {
            rtext = fit(f, fonts.name, 15, mm.paling_text, std::fmax(0.0f, right_max));
            right_w = measure(f, fonts.name, 15, rtext.c_str());
        } else if (room_row) {
            rtext = fit(f, fonts.name, 17, mm.rooms[mm.room].name, std::fmax(0.0f, right_max - 2 * aw - 10));
            right_w = measure(f, fonts.name, 17, rtext.c_str()) + 2 * aw + 10;
        }
        right_w = std::fmax(right_w, seen_w);
        // left: the area name, its badge, "Ashley is here" (dropped first)
        float left_room = width - right_w - gap;
        if (14 + name_w + badge_w + here_w > left_room)
            here = false;
        std::string nm = fit(f, fonts.name, 26, name, std::fmax(0.0f, left_room - 14 - badge_w));
        float x = cx0;
        x += text(f, fonts.name, 26, x, hy, fade(t.bone, a), nm.c_str()) + 14;
        if (badge) {
            unsigned bc = mm.paled && !switching ? t.risk : t.gold;
            f.dl->AddRect(f.p(x, hy + 9), f.p(x + bw + 12, hy + 27), fade(bc, a), 0, 0, f.s(1));
            text(f, fonts.label, 10, x + 6, hy + 12, fade(bc, a), badge, 0.26f);
            x += badge_w;
        }
        if (here) {
            f.dl->AddCircleFilled(f.p(x + 4, hy + 18), f.s(3.5f), fade(t.accent, a));
            label(f, t, x + 13, hy + 12, "Ashley is here", fade(t.ash, a));
        }
        float rx = cx1;
        if (mm.paled) {
            text_right(f, fonts.name, 15, rx, hy + 6, fade(t.ash, a), rtext.c_str());
        } else if (room_row) {
            bool over_r = hit(m, PC_UI_MOUSE_MAP_STEP, 1, rx - aw, hy, rx + 4, hy + 30);
            text_right(f, fonts.label, 18, rx, hy + 4, fade(over_r ? t.bone : t.ash, a), "\xe2\x80\xba");
            rx -= aw + 4;
            rx -= text_right(f, fonts.name, 17, rx, hy + 5, fade(t.bone, a), rtext.c_str()) + 6;
            bool over_l = hit(m, PC_UI_MOUSE_MAP_STEP, -1, rx - aw, hy, rx + 4, hy + 30);
            text_right(f, fonts.label, 18, rx, hy + 4, fade(over_l ? t.bone : t.ash, a), "\xe2\x80\xb9");
        }
        text_right(f, fonts.label, 10, cx1, hy + 28, fade(t.dim, a), seen, 0.24f);
    }

    // the rail: compass, completion, the areas (the shown one's rooms nested), the legend
    if (!loading && a > 0.001f) {
        float rc = (L.rail0 + L.rail1) * 0.5f;
        compass(f, t, rc, 182, 36, mm.yaw, mm.yaw0, a);
        float hy = 232;
        label(f, t, L.rail0, hy, "Areas", fade(t.ash, a));
        char pct[32];
        std::snprintf(pct, sizeof pct, "MAP %d%%", mm.completion);
        text_right(f, fonts.data, 12, L.rail1, hy - 2, fade(t.bone, a), pct);
        f.dl->AddRectFilled(f.p(L.rail0, hy + 18), f.p(L.rail1, hy + 20), fade(t.rule, a));
        f.dl->AddRectFilled(f.p(L.rail0, hy + 18), f.p(L.rail0 + (L.rail1 - L.rail0) * mm.completion / 100.0f, hy + 20),
            fade(t.gold, a));

        // lines: area rows (30) and, under the shown area, its visited rooms (22)
        struct Line {
            int area, room; // area index, or room index (area -1)
            float h;
        };
        std::vector<Line> lines;
        int focus_line = 0;
        bool nest = !list && !switching;
        for (int i = 0; i < mm.area_count; ++i) {
            const pc_ui_map_area& ar = mm.areas[i];
            if (ar.shown || (list && i == mm.list.cursor))
                focus_line = (int)lines.size();
            lines.push_back({i, -1, 30});
            // the shown area's visited rooms in L1 / R1's order: a window of
            // ROOM_ROWS around the selected one, so the other areas stay near
            if (nest && ar.shown && !mm.paled) {
                int at = 0;
                for (int k = 0; k < mm.ring_count; ++k)
                    if (mm.ring[k] == mm.room)
                        at = k;
                int k0 = at - ROOM_ROWS / 2, k1;
                if (k0 > mm.ring_count - ROOM_ROWS)
                    k0 = mm.ring_count - ROOM_ROWS;
                if (k0 < 0)
                    k0 = 0;
                k1 = k0 + ROOM_ROWS < mm.ring_count ? k0 + ROOM_ROWS : mm.ring_count;
                for (int k = k0; k < k1; ++k) {
                    if (mm.ring[k] == mm.room)
                        focus_line = (int)lines.size();
                    lines.push_back({-1, mm.ring[k], 22});
                }
            }
        }
        float room = L.list1 - L.list0;
        // the first line shown: the list's window (58EC's scroll) while it has
        // the focus, else the focused line kept in view; the wheel moves it
        int first = 0;
        if (list) {
            // the list's window, unless the rail shows more rows than its nine
            // (16:9): then the last rows that fit, the cursor still among them
            int fit_rows = (int)((room + 0.5f) / 30), max_first = (int)lines.size() - fit_rows;
            first = mm.list.scroll < max_first ? mm.list.scroll : max_first > 0 ? max_first : 0;
        } else {
            float hsum = 0;
            int lines_fit = 0;
            for (size_t i = 0; i < lines.size() && hsum + lines[i].h <= room; ++i)
                hsum += lines[i].h, ++lines_fit;
            int auto_first = focus_line - lines_fit / 2;
            int max_first = (int)lines.size() - lines_fit;
            if (max_first < 0)
                max_first = 0;
            if (auto_first > max_first)
                auto_first = max_first;
            if (auto_first < 0)
                auto_first = 0;
            if (mm.room != shown_room || mm.scene != shown_scene) // a new room in focus: the wheel's offset goes
                pc_ui_map_rail_scroll_reset();
            shown_room = mm.room, shown_scene = mm.scene;
            // the wheel moves the window from there (cosmetic: no game state)
            first = auto_first + (int)pc_ui_map_rail_scroll((float)-auto_first, (float)(max_first - auto_first));
        }
        hit(m, PC_UI_MOUSE_MAP_RAIL, 0, L.rail0, L.list0, L.rail1, L.list1);
        float y = L.list0;
        bool more_below = false;
        for (size_t li = first; li < lines.size(); ++li) {
            const Line& ln = lines[li];
            if (y + ln.h > L.list1 + 0.5f) {
                more_below = true;
                break;
            }
            if (ln.area >= 0) {
                const pc_ui_map_area& ar = mm.areas[ln.area];
                bool cursor = list && ln.area == mm.list.cursor;
                bool over = hit(m, PC_UI_MOUSE_MAP_AREA, ar.id, L.rail0, y, L.rail1, y + ln.h);
                if (cursor)
                    focus_band(f, t, L.rail0, y, L.rail1, y + ln.h);
                else if (ar.shown)
                    f.dl->AddRectFilled(f.p(L.rail0, y + 3), f.p(L.rail1, y + ln.h - 3), fade(t.panel, a));
                if (ar.current)
                    f.dl->AddCircleFilled(f.p(L.rail0 + 9, y + ln.h * 0.5f), f.s(3.5f), fade(t.accent, a));
                float tagw = ar.paling ? measure(f, fonts.label, 9, "JAMMED", 0.24f) + 8 : 0;
                std::string nm = fit(f, fonts.name, 16, ar.name, L.rail1 - L.rail0 - 26 - tagw);
                unsigned col = ar.shown || cursor || over ? t.bone : t.ash;
                text(f, fonts.name, 16, L.rail0 + 20, y + 6, fade(col, a), nm.c_str());
                if (ar.paling)
                    text_right(f, fonts.label, 9, L.rail1 - 4, y + 11, fade(t.risk, a), "JAMMED", 0.24f);
            } else {
                const pc_ui_map_room& r = mm.rooms[ln.room];
                bool sel = ln.room == mm.room;
                bool over = hit(m, PC_UI_MOUSE_MAP_ROOM, ln.room, L.rail0 + 20, y, L.rail1, y + ln.h);
                if (sel)
                    f.dl->AddRectFilled(f.p(L.rail0 + 26, y + 7), f.p(L.rail0 + 33, y + 14), fade(ROOM_SELECTED, a));
                else
                    f.dl->AddRect(f.p(L.rail0 + 26, y + 7), f.p(L.rail0 + 33, y + 14), fade(ROOM_OTHER, a), 0, 0,
                        f.s(1));
                std::string nm = fit(f, fonts.name, 14, r.name, L.rail1 - L.rail0 - 46);
                text(f, fonts.name, 14, L.rail0 + 40, y + 3, fade(sel || over ? t.bone : t.ash, a), nm.c_str());
            }
            y += ln.h;
        }
        // more above / below: small carets at the rail's right edge
        unsigned caret = fade(t.dim, a);
        if (first > 0)
            f.dl->AddTriangleFilled(f.p(L.rail1 - 8, L.list0 - 4), f.p(L.rail1, L.list0 - 4),
                f.p(L.rail1 - 4, L.list0 - 9), caret);
        if (more_below)
            f.dl->AddTriangleFilled(f.p(L.rail1 - 8, L.list1 + 3), f.p(L.rail1, L.list1 + 3),
                f.p(L.rail1 - 4, L.list1 + 8), caret);

        // the legend (the game's own colours and marks, 4D8.c _renderRoom)
        float lx = L.legend_x0, ly = L.legend_y0;
        float colw = L.wide ? L.legend_x1 - L.legend_x0 : (L.legend_x1 - L.legend_x0) * 0.5f;
        label(f, t, lx, ly, "Legend", fade(t.dim, a));
        ly += 18;
        // (4D8.c _renderRoom's markers: grey door, magenta (blinking) the
        // door Ashley came in by, cyan an exit to another area with its tag,
        // yellow a marked spot (the SAVE tag and the others cycling there),
        // the padlock a locked door (func_80107630))
        struct Entry {
            int kind;
            const char* what;
        } entries[] = {{0, "Room shown"}, {1, "Other room"}, {2, "Door"}, {3, "Way in"}, {5, "Other area"},
            {6, "Marked spot"}, {4, "Locked door"}};
        const int entry_count = (int)(sizeof entries / sizeof entries[0]);
        for (int i = 0; i < entry_count; ++i) {
            float ex = lx + (L.wide ? 0 : (i % 2) * colw), ey = ly + (L.wide ? i * 30 : (i / 2) * 22);
            float icx = ex + 10, icy = ey + 9;
            switch (entries[i].kind) {
            case 0: room_swatch(f, icx, icy, fade(ROOM_SELECTED, a), fade(EDGE_SELECTED, a)); break;
            case 1: room_swatch(f, icx, icy, fade(ROOM_OTHER, a), fade(EDGE_OTHER, a)); break;
            case 2: cross(f, icx, icy, 4, fade(DOOR, a)); break;
            case 3: cross(f, icx, icy, 4, fade(WAY_IN, a)); break;
            case 4: padlock(f, icx, icy, fade(t.ash, a)); break;
            case 5: cross(f, icx, icy, 4, fade(EXIT, a)); break;
            case 6: cross(f, icx, icy, 4, fade(MARK, a)); break;
            }
            text(f, fonts.name, 13, ex + 24, ey + 1, fade(t.ash, a), entries[i].what);
        }
    }

    // the prompts (MENU5's controls; dimmed as its help dims them by
    // _menuCommandsState: 0 all lit, 2 zoom and areas, 3 rotate and areas, 5 none)
    if (!loading && !closing && a > 0.001f) {
        float cy = 688;
        Prompt ps[10];
        int n;
        if (list) {
            const Prompt l[] = {{PC_PROMPT_UP, PC_PROMPT_DOWN, "Choose", PC_UI_MAP_P_LIST_UP, true, PC_UI_MAP_P_LIST_DOWN},
                {PC_PROMPT_L1, PC_PROMPT_R1, "Tab", PC_UI_MAP_P_LIST_TAB, true, -1},
                {PC_PROMPT_CIRCLE, -1, "Show", PC_UI_MAP_P_LIST_SHOW, true, -1},
                {PC_PROMPT_CROSS, -1, "Map", PC_UI_MAP_P_LIST_MAP, true, -1},
                {PC_PROMPT_TRIANGLE, -1, "Close", PC_UI_MAP_P_LIST_CLOSE, true, -1}};
            n = 5;
            for (int i = 0; i < n; ++i)
                ps[i] = l[i];
        } else {
            int cs = mm.cmd_state;
            bool all = cs == 0 && !mm.paled && !switching;
            bool moving = !mm.paled && !switching;
            // Map controls (System > Controls; map_pad.h): Modern, the right stick turns and tilts, L2 / R2
            // zoom, the left stick (and the D-pad) pans; Original, the PlayStation's L2 / R2 (+ D-pad or stick)
            const Prompt modern[] = {
                {PC_PROMPT_L1, PC_PROMPT_R1, "Room", PC_UI_MAP_P_ROOM_PREV, all, PC_UI_MAP_P_ROOM_NEXT},
                {STICK_R, -1, "Turn", PC_UI_MAP_P_ROTATE, moving && (cs == 0 || cs == 3), -1},
                {PC_PROMPT_L2, PC_PROMPT_R2, "Zoom", PC_UI_MAP_P_ZOOM, moving && (cs == 0 || cs == 2), -1},
                {STICK_L, -1, "Pan", -1, all, -1},
                {PC_PROMPT_START, -1, "Reset", PC_UI_MAP_P_RESET, all, -1},
                {PC_PROMPT_CIRCLE, -1, "Areas", PC_UI_MAP_P_AREAS, !switching && cs != 5, -1},
                {PC_PROMPT_SELECT, -1, "Hide", PC_UI_MAP_P_HIDE, !switching, -1},
                {PC_PROMPT_CROSS, -1, "Back", PC_UI_MAP_P_BACK, !switching, -1},
                {PC_PROMPT_TRIANGLE, -1, "Close", PC_UI_MAP_P_CLOSE, !switching, -1}};
            const Prompt original[] = {
                {PC_PROMPT_L1, PC_PROMPT_R1, "Room", PC_UI_MAP_P_ROOM_PREV, all, PC_UI_MAP_P_ROOM_NEXT},
                {PC_PROMPT_L2, -1, "Rotate", PC_UI_MAP_P_ROTATE, moving && (cs == 0 || cs == 3), -1},
                {PC_PROMPT_R2, -1, "Zoom", PC_UI_MAP_P_ZOOM, moving && (cs == 0 || cs == 2), -1},
                {PC_PROMPT_START, -1, "Reset", PC_UI_MAP_P_RESET, all, -1},
                {PC_PROMPT_CIRCLE, -1, "Areas", PC_UI_MAP_P_AREAS, !switching && cs != 5, -1},
                {PC_PROMPT_SELECT, -1, "Hide", PC_UI_MAP_P_HIDE, !switching, -1},
                {PC_PROMPT_CROSS, -1, "Back", PC_UI_MAP_P_BACK, !switching, -1},
                {PC_PROMPT_TRIANGLE, -1, "Close", PC_UI_MAP_P_CLOSE, !switching, -1}};
            // Modern on the keyboard (the prompts follow the device): no sticks; J / L turn and I / K tilt
            // (pc_window_stick, camera keys on), else L2 + the arrows (MENU5's own turn); the arrows pan
            bool keys_on = pc_camera_stick_keys != 0;
            const Prompt keyboard[] = {
                {PC_PROMPT_L1, PC_PROMPT_R1, "Room", PC_UI_MAP_P_ROOM_PREV, all, PC_UI_MAP_P_ROOM_NEXT},
                {keys_on ? KEY_CAP + 'J' : PC_PROMPT_L2, keys_on ? KEY_CAP + 'L' : KEY_ARROWS, "Turn",
                    PC_UI_MAP_P_ROTATE, moving && (cs == 0 || cs == 3), -1},
                {KEY_CAP + 'I', KEY_CAP + 'K', "Tilt", -1, moving && (cs == 0 || cs == 3), -1},
                {PC_PROMPT_L2, PC_PROMPT_R2, "Zoom", PC_UI_MAP_P_ZOOM, moving && (cs == 0 || cs == 2), -1},
                {KEY_ARROWS, -1, "Pan", -1, all, -1},
                {PC_PROMPT_START, -1, "Reset", PC_UI_MAP_P_RESET, all, -1},
                {PC_PROMPT_CIRCLE, -1, "Areas", PC_UI_MAP_P_AREAS, !switching && cs != 5, -1},
                {PC_PROMPT_SELECT, -1, "Hide", PC_UI_MAP_P_HIDE, !switching, -1},
                {PC_PROMPT_CROSS, -1, "Back", PC_UI_MAP_P_BACK, !switching, -1},
                {PC_PROMPT_TRIANGLE, -1, "Close", PC_UI_MAP_P_CLOSE, !switching, -1}};
            bool orig = pc_qol_map_controls == 1;
            bool keyboard_on = !orig && pc_prompt_device() == PC_PROMPTS_KEYBOARD;
            const Prompt* l = orig ? original : keyboard_on ? keyboard : modern;
            n = orig ? (int)(sizeof original / sizeof original[0])
                : keyboard_on ? (int)(sizeof keyboard / sizeof keyboard[0])
                              : (int)(sizeof modern / sizeof modern[0]);
            if (keyboard_on && !keys_on) { // no camera keys: no tilt prompt
                int k = 0;
                for (int i = 0; i < n; ++i)
                    if (l[i].verb != std::string("Tilt"))
                        ps[k++] = l[i];
                n = k;
            } else {
                for (int i = 0; i < n; ++i)
                    ps[i] = l[i];
            }
        }
        // the footer as the other screens': hints right-aligned at the
        // margin, the crumb left of them (what fits; 4:3 often has no room)
        float hy = 720 - 40;
        cy = hy + t.hint_size * 0.5f;
        float left = prompts(f, t, t.margin_x, f.W - t.margin_x, cy, ps, n, m, a);
        std::string crumb = std::string(pc_ui_shell_tab_title(pc_ui_shell_tab())) + " \xe2\x80\xba Map";
        if (list)
            crumb += " \xe2\x80\xba Areas";
        else if (area)
            crumb += std::string(" \xe2\x80\xba ") + area->name;
        float crumb_room = left - 24 - t.margin_x;
        if (crumb_room > 60) {
            std::string c = fit(f, fonts.label, t.hint_size, upper(crumb.c_str()).c_str(), crumb_room, 0.18f);
            text(f, fonts.label, t.hint_size, t.margin_x, hy, fade(t.dim, a), c.c_str(), 0.18f);
        }
    }
    // hidden: only a pill to bring the chrome back
    if (hidden && !loading) {
        float pw = prompt(f, t, 0, 0, PC_PROMPT_SELECT, -1, "Show", 0, false) + 24;
        float px1 = f.W - t.margin_x, px0 = px1 - pw, py0 = 666, py1 = 698;
        f.dl->AddRectFilled(f.p(px0, py0), f.p(px1, py1), fade(t.panel, 0.92f), f.s(16));
        bool over = hit(m, PC_UI_MOUSE_MAP_PROMPT, PC_UI_MAP_P_HIDE, px0, py0, px1, py1);
        prompt(f, t, px0 + 12, (py0 + py1) * 0.5f, PC_PROMPT_SELECT, -1, "Show", over ? t.bone : t.ash);
    }

    // the picture: drags (left: rotate; middle, or left started with Shift:
    // pan) and the wheel (zoom); the pointer over it shows how
    bool inside = m.over(cx0, cy0, cx1, cy1) && !loading && !closing && !list;
    if (inside && m.kind == PC_UI_MOUSE_NONE)
        m.kind = PC_UI_MOUSE_MAP_VIEW, m.index = 0;
    if (inside && a > 0.001f && !mm.paled)
        text(f, fonts.label, 10, cx0, cy1 + 7, fade(t.dim, a),
            "DRAG ROTATE \xc2\xb7 SHIFT OR MIDDLE DRAG PAN \xc2\xb7 WHEEL ZOOM", 0.22f);
    for (int b = 0; b < 2; ++b) {
        Drag& d = drags[m.rpc ? 1 : 0][b];
        bool held = b == 0 ? m.down : m.middle;
        if (!m.ok || !held) {
            if (m.ok || m.rpc)
                d.on = false;
            continue;
        }
        if (!d.on) {
            if (inside && (m.rpc || b == 1 || m.clicked))
                d.on = true, d.pan = b == 1 || m.shift, d.x = m.x, d.y = m.y;
            continue;
        }
        float dx = m.x - d.x, dy = m.y - d.y;
        d.x = m.x, d.y = m.y;
        if (dx != 0 || dy != 0)
            pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, 0, dx, dy, d.pan ? 2 : 1, 0);
    }
    if (inside || drags[0][0].on || drags[0][1].on)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if (m.kind != PC_UI_MOUSE_NONE)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // the rest of the pointer: right click (back), clicks, the wheel
    if (!m.ok)
        return;
    if (m.right) {
        pc_ui_map_mouse(PC_UI_MOUSE_NONE, 0, 0, 0, 0, 0, 0, 1);
        return;
    }
    if (m.kind == PC_UI_MOUSE_MAP_VIEW) {
        if (m.wheel != 0)
            pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, m.wheel, 0, 0, 0, 0);
        return;
    }
    if (m.kind != PC_UI_MOUSE_NONE && (m.clicked || m.wheel != 0))
        pc_ui_map_mouse(m.kind, m.index, m.clicked ? 1 : 0, m.wheel, 0, 0, 0, 0);
}

} // namespace uidraw
