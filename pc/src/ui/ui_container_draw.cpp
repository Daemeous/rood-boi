// Modern menus, the native Container screen (container_frame.h), drawn from
// the model container_bridge.c publishes: nothing of MENU7's or MENUD's own
// screens shows (MENU7's drawing is gated, its menu rows hidden by
// vs_mainMenu_hideMenu; MENUD is not run). The shell's backdrop at the
// menus' opacity and tint; a header (Container, the mode or the save room);
// TOP: Remove / Store / End and the summary table (the bag and the container,
// count / capacity per category); LIST: the tab strip (8: the categories,
// then the transfer list), the source list (the original's rows and marks),
// the right panel (the destination's room with what is staged, the focused
// row's card); menus as popovers by the row, the amount and the Yes / No's
// as centred cards; MENU7's prompts and its card wait as cards too. The
// save list is the Data screen's (ui_data_draw.cpp, container mode). Layout
// units 1280 x 720 (16:9) or 960 x 720 (4:3).
#include "ui/ui_draw_internal.h"
#include "ui/container_frame.h"
#include "button_prompts.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" int pc_gl_view[4];

namespace uidraw {
namespace {

const char* const CAT_NAMES[7] = {"Weapon", "Blade", "Grip", "Shield", "Armor", "Gem", "Misc"};
const char* const TIMES = "\xc3\x97"; // "×"

const int VISIBLE = 13; // list rows shown (the original shows 8; its window is kept for the cursor memory)

struct Layout {
    float W;
    bool wide;
    float list_x0, list_x1, list_y0, list_y1, row_h;
    float panel_x0, panel_x1;
    float top_x0, top_x1, table_x0, table_x1;
};

Layout layout(float W)
{
    Layout L;
    L.W = W;
    L.wide = W >= 1100;
    L.list_x0 = L.wide ? 64 : 32, L.list_x1 = L.wide ? 736 : 560;
    L.list_y0 = 144, L.list_y1 = 144 + VISIBLE * 36, L.row_h = 36;
    // the list's panel ends at list_x1 + 14 (its scrollbar inside), the right panels start at
    // panel_x0 - 10: a 16 / 18 gutter between them at 4:3 / 16:9
    L.panel_x0 = L.wide ? 768 : 600, L.panel_x1 = L.wide ? 1216 : 928;
    L.top_x0 = L.wide ? 96 : 48, L.top_x1 = L.wide ? 520 : 400;
    L.table_x0 = L.wide ? 600 : 420, L.table_x1 = L.wide ? 1184 : 912;
    return L;
}

struct Anim {
    float scroll = 0;
    int scroll_target = 0;
    int tab = -1, mode = -1;
} anim;

bool hit(Pointer& m, int kind, int index, float x0, float y0, float x1, float y1)
{
    pc_ui_ctr_hit_record(kind, index, x0, y0, x1, y1);
    return m.hit(kind, index, x0, y0, x1, y1);
}

bool hovered(int kind, int index)
{
    int k, i;
    pc_ui_ctr_hover(&k, &i);
    return k == kind && i == index;
}

void panel(const Frame& f, const ui::Theme& t, float x0, float y0, float x1, float y1, unsigned edge = 0)
{
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y1), t.panel, f.s(6));
    f.dl->AddRect(f.p(x0, y0), f.p(x1, y1), edge ? edge : t.rule2, f.s(6), 0, f.s(1));
}

// a popover: a shadow, the panel, an accent line on top
void pop(const Frame& f, const ui::Theme& t, float x0, float y0, float x1, float y1)
{
    f.dl->AddRectFilled(f.p(x0 + 3, y0 + 5), f.p(x1 + 6, y1 + 8), fade(0xFF000000u, 0.3f));
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y1), t.panel);
    f.dl->AddRect(f.p(x0, y0), f.p(x1, y1), t.rule2, 0, 0, f.s(1));
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y0 + 2), t.accent);
}

std::string count_cap(int n, int cap)
{
    char b[32];
    std::snprintf(b, sizeof b, "%d / %d", n, cap);
    return b;
}

// a count against its capacity, gold while under half of it (MENUD func_80106464's tint)
float count_text(const Frame& f, const ui::Theme& t, float xr, float y, int n, int cap, float size = 16)
{
    std::string c = count_cap(n, cap);
    return text_right(f, fonts.data, size, xr, y, n * 2 < cap ? t.gold : t.bone, c.c_str());
}

// ---- MENU7's prompts and its wait ----

void prompt_card(const Frame& f, const ui::Theme& t, const pc_ui_ctr_model& c, Pointer& m, bool warn)
{
    float w = 560, h = 220;
    if (w > f.W - 48)
        w = f.W - 48;
    float x = (f.W - w) * 0.5f, y = 360 - h * 0.5f;
    hit(m, PC_UI_MOUSE_CTR_PANEL, 0, x, y, x + w, y + h);
    panel(f, t, x, y, x + w, y + h, warn ? t.down : 0);
    if (warn)
        f.dl->AddRectFilled(f.p(x, y + 12), f.p(x + 3, y + h - 12), t.down);
    const char* title = c.phase == PC_UI_CTR_PH_PROMPT_CHECK ? "The container"
        : c.phase == PC_UI_CTR_PH_PROMPT_WARN               ? "No container data"
                                                             : "Not saved yet";
    text(f, fonts.name, 26, x + 24, y + 18, t.bone, title);
    paragraph(f, t, x + 24, y + 58, w - 48, t.ash, c.prompt_text, 16);
    float bw = 120, bh = 40, gy = y + h - bh - 20, gx = x + w - 24 - bw * 2 - 12;
    for (int i = 0; i < 2; ++i) {
        float x0 = gx + i * (bw + 12);
        int kind = i == 0 ? PC_UI_MOUSE_CTR_YES : PC_UI_MOUSE_CTR_NO;
        bool o = hit(m, kind, 0, x0, gy, x0 + bw, gy + bh) || hovered(kind, 0);
        bool focus = (i == 0) == (c.prompt_yes != 0);
        f.dl->AddRectFilled(f.p(x0, gy), f.p(x0 + bw, gy + bh), focus ? t.accent : o ? t.rule2 : t.rule, f.s(5));
        const char* s = i == 0 ? "YES" : "NO";
        float tw = measure(f, fonts.label, 15, s, 0.26f);
        text(f, fonts.label, 15, x0 + (bw - tw) * 0.5f, gy + (bh - 15) * 0.5f - 1, c.prompt_ready ? t.bone : t.ash, s,
            0.26f);
    }
}

void wait_card(const Frame& f, const ui::Theme& t, const pc_ui_ctr_model& c, Pointer& m)
{
    float w = 560, h = 170;
    if (w > f.W - 48)
        w = f.W - 48;
    float x = (f.W - w) * 0.5f, y = 360 - h * 0.5f;
    bool o = hit(m, PC_UI_MOUSE_CTR_STATUS, 0, x, y, x + w, y + h);
    panel(f, t, x, y, x + w, y + h, t.down);
    text(f, fonts.name, 24, x + 24, y + 18, t.bone, "Memory card");
    paragraph(f, t, x + 24, y + 56, w - 48, t.ash, c.wait_text[0] ? c.wait_text : "\xe2\x80\xa6", 16);
    float gw = glyph(f, PC_PROMPT_CIRCLE, 18, 0, 0, true);
    float tw = measure(f, fonts.label, 13, "CONTINUE", 0.22f);
    float ry = y + h - 30;
    text(f, fonts.label, 13, x + w - 24 - tw, ry, o ? t.bone : t.ash, "CONTINUE", 0.22f);
    glyph(f, PC_PROMPT_CIRCLE, 18, x + w - 24 - tw - 6 - gw, ry + 7);
}

void busy_line(const Frame& f, const ui::Theme& t, const char* what)
{
    float w = measure(f, fonts.name, 22, what);
    float cx = f.W * 0.5f, cy = 340;
    spinner(f, cx - w * 0.5f - 18, cy + 11, 6, t.gold);
    text(f, fonts.name, 22, cx - w * 0.5f, cy, t.ash, what);
}

// ---- TOP ----

void draw_top(const Frame& f, const ui::Theme& t, const Layout& L, const pc_ui_ctr_model& c, Pointer& m)
{
    float y0 = 160, rh = 56;
    panel(f, t, L.top_x0, y0 - 8, L.top_x1, y0 + 3 * rh + 8);
    for (int i = 0; i < 3; ++i) {
        float ry = y0 + i * rh;
        bool on = c.top == i;
        bool o = hit(m, PC_UI_MOUSE_CTR_TOP, i, L.top_x0, ry, L.top_x1, ry + rh) || hovered(PC_UI_MOUSE_CTR_TOP, i);
        if (on)
            focus_band(f, t, L.top_x0, ry, L.top_x1, ry + rh);
        else if (o)
            f.dl->AddRectFilled(f.p(L.top_x0, ry), f.p(L.top_x1, ry + rh), t.accent_soft);
        std::string label = i == 2 ? "End transfer" : c.top_label[i];
        if (i < 2 && !label.empty()) // the game's capitals in sentence case
            for (size_t k = 1; k < label.size(); ++k)
                if (label[k] >= 'A' && label[k] <= 'Z')
                    label[k] = (char)(label[k] - 'A' + 'a');
        text(f, fonts.name, 21, L.top_x0 + 20, ry + 6, on ? t.bone : t.ash, label.c_str());
        std::string d = fit(f, fonts.label, 13, c.top_desc[i], L.top_x1 - L.top_x0 - 40, 0.02f);
        text(f, fonts.label, 13, L.top_x0 + 20, ry + 33, on ? t.ash : t.dim, d.c_str(), 0.02f);
        if (i < 2)
            f.dl->AddLine(f.p(L.top_x0 + 12, ry + rh), f.p(L.top_x1 - 12, ry + rh), t.rule, f.s(1));
    }
    // the summary table: per category the bag's and the container's count / capacity
    float tx0 = L.table_x0, tx1 = L.table_x1, ty = 160, row = 40;
    panel(f, t, tx0, ty - 8, tx1, ty + 8 * row);
    float c1 = tx0 + (tx1 - tx0) * 0.62f, c2 = tx1 - 20;
    label(f, t, tx0 + 20, ty + 12, "Category");
    text_right(f, fonts.label, 11, c1, ty + 12, t.ash, "GADGET BAG", 0.26f);
    text_right(f, fonts.label, 11, c2, ty + 12, t.ash, "CONTAINER", 0.26f);
    f.dl->AddLine(f.p(tx0 + 12, ty + row), f.p(tx1 - 12, ty + row), t.rule2, f.s(1));
    for (int i = 0; i < 7; ++i) {
        float ry = ty + row * (i + 1);
        bag_icon(f, i, 0, "", "", tx0 + 30, ry + row * 0.5f, 16, t.ash);
        text(f, fonts.name, 18, tx0 + 52, ry + 9, t.bone, CAT_NAMES[i]);
        count_text(f, t, c1, ry + 11, c.bag_count[i], c.bag_cap[i]);
        count_text(f, t, c2, ry + 11, c.box_count[i], c.box_cap[i]);
    }
}

// ---- LIST ----

const char* tab_name(const pc_ui_ctr_model& c, int tab)
{
    if (tab < 7)
        return CAT_NAMES[tab];
    return c.dir ? "To store" : "To remove"; // staged until End is confirmed
}

void tab_strip(const Frame& f, const ui::Theme& t, const Layout& L, const pc_ui_ctr_model& c, Pointer& m)
{
    float y0 = 96, y1 = 132, x0 = L.list_x0, x1 = L.list_x1;
    if (L.wide) {
        float w = (x1 - x0) / 8;
        for (int i = 0; i < 8; ++i) {
            float cx = x0 + i * w;
            bool on = c.tab == i;
            bool o = hit(m, PC_UI_MOUSE_CTR_TAB, i, cx, y0, cx + w - 4, y1) || hovered(PC_UI_MOUSE_CTR_TAB, i);
            std::string name = fit(f, fonts.label, 11, upper(tab_name(c, i)).c_str(), w - 8, 0.22f);
            text(f, fonts.label, 11, cx + 4, y0 + 1, on ? t.bone : o ? t.ash : t.dim, name.c_str(), 0.22f);
            std::string n = count_cap(c.tab_count[i], c.tab_cap[i]);
            text(f, fonts.data, 12, cx + 4, y0 + 16, on ? t.bone : t.dim, fit(f, fonts.data, 12, n.c_str(), w - 8).c_str());
            f.dl->AddRectFilled(f.p(cx + 2, y1 - 2), f.p(cx + w - 6, y1), on ? t.accent : t.rule);
        }
    } else { // 4:3: icons, the current tab's label after its icon
        float x = x0;
        for (int i = 0; i < 8; ++i) {
            bool on = c.tab == i;
            std::string name = upper(tab_name(c, i));
            float w = on ? 40 + measure(f, fonts.label, 11, name.c_str(), 0.22f) + 70 : 44;
            bool o = hit(m, PC_UI_MOUSE_CTR_TAB, i, x, y0, x + w - 4, y1) || hovered(PC_UI_MOUSE_CTR_TAB, i);
            unsigned col = on ? t.bone : o ? t.ash : t.dim;
            if (i < 7)
                bag_icon(f, i, 0, "", "", x + 18, y0 + 16, 16, col);
            else
                for (int k = 0; k < 3; ++k) // the transfer list: three lines
                    f.dl->AddLine(f.p(x + 10, y0 + 10 + k * 6), f.p(x + 26, y0 + 10 + k * 6), col, f.s(1.6f));
            if (on) {
                text(f, fonts.label, 11, x + 36, y0 + 3, t.bone, name.c_str(), 0.22f);
                text(f, fonts.data, 12, x + 36, y0 + 17, t.ash, count_cap(c.tab_count[i], c.tab_cap[i]).c_str());
            }
            f.dl->AddRectFilled(f.p(x + 2, y1 - 2), f.p(x + w - 6, y1), on ? t.accent : t.rule);
            x += w;
        }
    }
}

// the row's marks: EQUIPPED (worn), SET (part of an assembly or set in a shield), the picked one
float marks(const Frame& f, const ui::Theme& t, const pc_ui_ctr_row& r, bool picked, float right, float cy)
{
    auto tag = [&](const char* s, unsigned col) {
        float tw = measure(f, fonts.label, 10, s, 0.22f);
        float bx = right - tw - 12;
        f.dl->AddRect(f.p(bx, cy - 9), f.p(right, cy + 9), col, 0, 0, f.s(1));
        text(f, fonts.label, 10, bx + 6, cy - 6, col, s, 0.22f);
        right = bx - 8;
    };
    if (picked)
        tag("PICKED", t.gold);
    if (r.flags & PC_CTR_ROW_EQUIPPED)
        tag("EQUIPPED", t.accent);
    else if (r.flags & PC_CTR_ROW_ATTACHED)
        tag("SET", t.ash);
    return right;
}

void draw_rows(const Frame& f, const ui::Theme& t, const Layout& L, const pc_ui_ctr_model& c, float dt, Pointer& m)
{
    float x0 = L.list_x0, x1 = L.list_x1, y0 = L.list_y0, y1 = L.list_y1, rh = L.row_h;
    panel(f, t, x0 - 8, y0 - 6, x1 + 14, y1 + 6);
    if (c.rows == 0) {
        float w = (x1 - x0) - 60;
        paragraph(f, t, x0 + 24, y0 + 40, w, t.ash, c.empty_text, 17);
        return;
    }
    int cursor = c.cursor < 0 ? 0 : c.cursor >= c.rows ? c.rows - 1 : c.cursor;
    bool reset = anim.tab != c.tab;
    int st = reset ? 0 : anim.scroll_target;
    if (cursor < st + 1)
        st = cursor - 1;
    if (cursor > st + VISIBLE - 2)
        st = cursor - VISIBLE + 2;
    int max_scroll = c.rows > VISIBLE ? c.rows - VISIBLE : 0;
    st = st < 0 ? 0 : st > max_scroll ? max_scroll : st;
    anim.scroll_target = st;
    anim.scroll = reset ? (float)st : approach(anim.scroll, (float)st, dt, t.focus_grow);
    bool focusing = c.mode != PC_UI_CTR_TOP;
    f.dl->PushClipRect(f.p(x0, y0), f.p(x1 + 1, y1), true);
    for (int i = 0; i < c.rows; ++i) {
        float ry = y0 + (i - anim.scroll) * rh;
        if (ry + rh < y0 || ry > y1)
            continue;
        const pc_ui_ctr_row& r = c.row[i];
        bool on = focusing && i == cursor;
        bool picked = c.picked == i;
        float hy0 = ry < y0 ? y0 : ry, hy1 = ry + rh > y1 ? y1 : ry + rh;
        if (hy1 > hy0) {
            bool o = hit(m, PC_UI_MOUSE_CTR_ROW, i, x0, hy0, x1, hy1) || hovered(PC_UI_MOUSE_CTR_ROW, i);
            if (o && !on)
                f.dl->AddRectFilled(f.p(x0, hy0), f.p(x1, hy1), t.accent_soft);
        }
        if (on)
            focus_band(f, t, x0, ry, x1, ry + rh);
        if (picked)
            f.dl->AddRectFilled(f.p(x0, ry + 4), f.p(x0 + 3, ry + rh - 4), t.gold);
        bool locked = (r.flags & PC_CTR_ROW_LOCKED) != 0;
        unsigned col = locked ? t.dim : on ? t.bone : t.ash;
        float cy = ry + rh * 0.5f;
        bag_icon(f, r.category, r.id, r.sub, r.name, x0 + 22, cy, 15, col);
        float right = marks(f, t, r, picked, x1 - 10, cy);
        if (r.category == 6 && r.count > 0) {
            std::string n = std::string(TIMES) + num(r.count);
            right -= text_right(f, fonts.data, 15, right, cy - 9, on ? t.bone : t.ash, n.c_str()) + 12;
        }
        float nx = x0 + 44, nw = right - nx;
        std::string sub = r.sub;
        if (!sub.empty() && sub.back() == '.')
            sub.pop_back();
        float name_w = measure(f, fonts.name, 18, r.name);
        if (name_w > nw * 0.62f && !sub.empty())
            name_w = nw * 0.62f;
        text(f, fonts.name, 18, nx, cy - 11, col, fit(f, fonts.name, 18, r.name, sub.empty() ? nw : nw * 0.62f).c_str());
        if (!sub.empty() && nw - name_w - 16 > 40)
            text(f, fonts.label, 12, nx + name_w + 14, cy - 6, on ? t.ash : t.dim,
                fit(f, fonts.label, 12, sub.c_str(), nw - name_w - 16, 0.04f).c_str(), 0.04f);
        f.dl->AddLine(f.p(x0, ry + rh), f.p(x1, ry + rh), t.rule, f.s(1));
    }
    f.dl->PopClipRect();
    if (c.rows > VISIBLE) {
        float track = y1 - y0, thumb = track * VISIBLE / c.rows;
        float ty = y0 + (track - thumb) * (max_scroll > 0 ? anim.scroll / max_scroll : 0);
        f.dl->AddRectFilled(f.p(x1 + 6, y0), f.p(x1 + 8, y1), t.rule);
        f.dl->AddRectFilled(f.p(x1 + 6, ty), f.p(x1 + 8, ty + thumb), t.ash);
    }
}

float bar(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* name, int cur, int max,
    unsigned col)
{
    char b[32];
    text(f, fonts.label, 12, x, y, t.ash, name, 0.2f);
    float bx = x + 30;
    float bw = w - 30 - 96;
    f.dl->AddRectFilled(f.p(bx, y + 5), f.p(bx + bw, y + 10), t.rule2);
    float k = max > 0 ? (float)cur / (float)max : 0;
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    f.dl->AddRectFilled(f.p(bx, y + 5), f.p(bx + bw * k, y + 10), col);
    std::snprintf(b, sizeof b, "%d/%d", cur, max);
    text_right(f, fonts.data, 13, x + w, y - 1, t.bone, b);
    return 22;
}

// Returns the help text the card showed (the info line then leaves it out), or nullptr.
const char* draw_panel(const Frame& f, const ui::Theme& t, const Layout& L, const pc_ui_ctr_model& c)
{
    float x0 = L.panel_x0, x1 = L.panel_x1, y = 144;
    // the destination's room: what is there + what is staged / its capacity
    const char* dest = c.dir ? "Container" : "Gadget bag";
    label(f, t, x0, y, c.dir ? "Into the container" : "Into the gadget bag");
    text(f, fonts.name, 22, x0, y + 16, t.bone, dest);
    y += 50;
    float rows_bottom = y + 7 * 24;
    panel(f, t, x0 - 10, y - 6, x1 + 10, rows_bottom + 4);
    for (int i = 0; i < 7; ++i) {
        float ry = y + i * 24;
        text(f, fonts.label, 13, x0 + 4, ry + 3, t.ash, upper(CAT_NAMES[i]).c_str(), 0.18f);
        char b[48];
        int n = c.dest_count[i], s = c.dest_staged[i], cap = c.dest_cap[i];
        if (i == 6) // misc after stacking (as D_80109A82, live) against 256 or 64
            n = c.misc_live, s = 0, cap = c.misc_cap;
        if (s > 0)
            std::snprintf(b, sizeof b, "%d + %d / %d", n, s, cap);
        else
            std::snprintf(b, sizeof b, "%d / %d", n, cap);
        bool over = n + s > cap;
        text_right(f, fonts.data, 14, x1 - 4, ry + 2, over ? t.down : s > 0 ? t.up : t.bone, b);
    }
    // the focused row's card (collapses under the table)
    float cy = rows_bottom + 18, cb = L.list_y1 + 6;
    const pc_ui_ctr_card& k = c.card;
    const pc_ui_ctr_row* r = c.rows > 0 && c.cursor >= 0 && c.cursor < c.rows ? &c.row[c.cursor] : nullptr;
    if (!k.valid && !(r && r->category == 6))
        return nullptr;
    panel(f, t, x0 - 10, cy, x1 + 10, cb);
    float w = x1 - x0;
    y = cy + 12;
    if (r) {
        text(f, fonts.name, 20, x0, y, t.bone, fit(f, fonts.name, 20, r->name, w).c_str());
        y += 26;
        if (r->sub[0]) {
            text(f, fonts.label, 13, x0, y, t.ash, fit(f, fonts.label, 13, r->sub, w, 0.04f).c_str(), 0.04f);
            y += 20;
        }
    }
    if (k.valid) {
        if (k.has_dp)
            y += bar(f, t, x0, y, w, "DP", k.dp, k.dp_max, t.hp);
        if (k.has_pp)
            y += bar(f, t, x0, y, w, "PP", k.pp, k.pp_max, t.mp);
        char b[96];
        std::snprintf(b, sizeof b, "STR %d   INT %d   AGL %d", k.str, k.intel, k.agl);
        if (y + 20 < cb) {
            text(f, fonts.data, 14, x0, y + 2, t.bone, b);
            y += 24;
        }
        if (k.gem_slots > 0 && y + 20 < cb) {
            std::snprintf(b, sizeof b, "GEM SLOTS %d   SET %d", k.gem_slots, k.gems);
            text(f, fonts.label, 13, x0, y + 2, t.ash, b, 0.08f);
            y += 22;
        }
        if (k.parent[0] && y + 20 < cb) {
            std::string p = std::string("Set in ") + k.parent;
            text(f, fonts.label, 13, x0, y + 2, t.gold, fit(f, fonts.label, 13, p.c_str(), w, 0.02f).c_str(), 0.02f);
            y += 22;
        }
    }
    if (r && r->help[0] && y + 24 < cb) {
        paragraph(f, t, x0, y + 6, w, t.ash, r->help, 13.5f);
        return r->help;
    }
    return nullptr;
}

// ACTIONS / SORT: a popover by the row (7 entries shown, the original's window)
void draw_menu(const Frame& f, const ui::Theme& t, const Layout& L, const pc_ui_ctr_model& c, Pointer& m)
{
    const pc_ui_ctr_menu& mu = c.menu;
    int shown = mu.count < 7 ? mu.count : 7;
    float rh = 32, w = 280, h = shown * rh + 12;
    float rowy = L.list_y0 + (c.cursor - anim.scroll) * L.row_h;
    float x = L.list_x1 - w - 20, y = rowy + L.row_h + 4;
    if (y + h > L.list_y1)
        y = rowy - h - 4;
    if (y < L.list_y0)
        y = L.list_y0;
    pop(f, t, x, y, x + w, y + h);
    hit(m, PC_UI_MOUSE_CTR_PANEL, 3, x, y, x + w, y + h); // its padding takes no click (not the row behind)
    for (int j = 0; j < shown; ++j) {
        int i = mu.offset + j;
        if (i >= mu.count)
            break;
        float ry = y + 6 + j * rh;
        bool on = i == mu.focus;
        bool o = hit(m, PC_UI_MOUSE_CTR_MENU, i, x, ry, x + w, ry + rh) || hovered(PC_UI_MOUSE_CTR_MENU, i);
        if (on)
            focus_band(f, t, x + 2, ry, x + w - 2, ry + rh);
        else if (o)
            f.dl->AddRectFilled(f.p(x + 2, ry), f.p(x + w - 2, ry + rh), t.accent_soft);
        std::string s = mu.label[i];
        for (size_t k = 1; k < s.size(); ++k) // the game's capitals in sentence case
            if (s[k] >= 'A' && s[k] <= 'Z' && s[k - 1] != ' ')
                s[k] = (char)(s[k] - 'A' + 'a');
        text(f, fonts.name, 17, x + 16, ry + 6, mu.enabled[i] ? (on ? t.bone : t.ash) : t.dim, s.c_str());
    }
    if (mu.offset > 0)
        f.dl->AddTriangleFilled(f.p(x + w - 20, y + 10), f.p(x + w - 14, y + 4), f.p(x + w - 8, y + 10), t.ash);
    if (mu.offset + shown < mu.count)
        f.dl->AddTriangleFilled(f.p(x + w - 20, y + h - 10), f.p(x + w - 8, y + h - 10), f.p(x + w - 14, y + h - 4),
            t.ash);
}

void yes_no(const Frame& f, const ui::Theme& t, const pc_ui_ctr_model& c, Pointer& m, float x, float y, float w)
{
    float bw = 120, bh = 40, gx = x + w - 24 - bw * 2 - 12;
    for (int i = 0; i < 2; ++i) {
        float x0 = gx + i * (bw + 12);
        int kind = i == 0 ? PC_UI_MOUSE_CTR_YES : PC_UI_MOUSE_CTR_NO;
        bool o = hit(m, kind, 0, x0, y, x0 + bw, y + bh) || hovered(kind, 0);
        bool focus = c.menu.focus == i;
        f.dl->AddRectFilled(f.p(x0, y), f.p(x0 + bw, y + bh), focus ? t.accent : o ? t.rule2 : t.rule, f.s(5));
        std::string s = upper(c.menu.count > i ? c.menu.label[i] : i == 0 ? "Yes" : "No");
        float tw = measure(f, fonts.label, 15, s.c_str(), 0.26f);
        text(f, fonts.label, 15, x0 + (bw - tw) * 0.5f, y + (bh - 15) * 0.5f - 1, t.bone, s.c_str(), 0.26f);
    }
}

void confirm_card(const Frame& f, const ui::Theme& t, const pc_ui_ctr_model& c, Pointer& m, const char* title)
{
    float w = 560, h = 210;
    float x = (f.W - w) * 0.5f, y = 360 - h * 0.5f;
    f.dl->AddRectFilled(f.p(0, 88), f.p(f.W, 628), fade(0xFF000000u, 0.35f));
    hit(m, PC_UI_MOUSE_CTR_PANEL, 1, x, y, x + w, y + h);
    panel(f, t, x, y, x + w, y + h);
    text(f, fonts.name, 25, x + 24, y + 18, t.bone, title);
    paragraph(f, t, x + 24, y + 58, w - 48, t.ash, c.confirm_text, 16);
    yes_no(f, t, c, m, x, y + h - 60, w);
}

void qty_card(const Frame& f, const ui::Theme& t, const pc_ui_ctr_model& c, Pointer& m)
{
    float w = 440, h = 250;
    float x = (f.W - w) * 0.5f, y = 360 - h * 0.5f;
    f.dl->AddRectFilled(f.p(0, 88), f.p(f.W, 628), fade(0xFF000000u, 0.35f));
    hit(m, PC_UI_MOUSE_CTR_PANEL, 2, x, y, x + w, y + h);
    panel(f, t, x, y, x + w, y + h);
    text(f, fonts.name, 24, x + 24, y + 16, t.bone, "Discard how many?");
    paragraph(f, t, x + 24, y + 50, w - 48, t.ash, c.confirm_text, 14);
    // "000/max": three digits, the step's digit marked (1 / 10 / 100)
    char digits[8], maxs[8];
    std::snprintf(digits, sizeof digits, "%03d", c.discard_count);
    std::snprintf(maxs, sizeof maxs, "/%03d", c.discard_max);
    float dx = x + 80, dy = y + 100, dw = 30;
    for (int i = 0; i < 3; ++i) {
        char d[2] = {digits[i], 0};
        bool step = 2 - i == c.discard_step;
        text(f, fonts.data, 40, dx + i * dw, dy, step ? t.bone : t.ash, d);
        if (step)
            f.dl->AddRectFilled(f.p(dx + i * dw, dy + 48), f.p(dx + i * dw + 24, dy + 51), t.accent);
    }
    text(f, fonts.data, 26, dx + 3 * dw + 6, dy + 12, t.dim, maxs);
    // the steppers (a click is the pad's Up / Down / Left / Right, OK its Circle)
    struct B {
        int idx;
        const char* s;
        float bx;
    } bs[] = {{1, "+", x + w - 150}, {-1, "\xe2\x88\x92", x + w - 100}};
    // the step: a click on a digit's side moves it (Left: the next digit up, Right: down)
    for (int k = 0; k < 2; ++k) {
        int idx = k == 0 ? 2 : -2;
        float sx = k == 0 ? dx - 34 : dx + 3 * dw + 76, cx = sx + 13, cy = dy + 24, d = k == 0 ? -1.0f : 1.0f;
        bool o = hit(m, PC_UI_MOUSE_CTR_QTY, idx, sx, dy + 4, sx + 26, dy + 44) || hovered(PC_UI_MOUSE_CTR_QTY, idx);
        f.dl->AddRectFilled(f.p(sx, dy + 4), f.p(sx + 26, dy + 44), o ? t.rule2 : t.rule, f.s(5));
        f.dl->AddTriangleFilled(f.p(cx + 5 * d, cy), f.p(cx - 4 * d, cy + 7 * d), f.p(cx - 4 * d, cy - 7 * d), t.bone);
    }
    for (const B& b : bs) {
        bool o = hit(m, PC_UI_MOUSE_CTR_QTY, b.idx, b.bx, dy, b.bx + 40, dy + 40) || hovered(PC_UI_MOUSE_CTR_QTY, b.idx);
        f.dl->AddRectFilled(f.p(b.bx, dy), f.p(b.bx + 40, dy + 40), o ? t.rule2 : t.rule, f.s(5));
        float tw = measure(f, fonts.data, 22, b.s);
        text(f, fonts.data, 22, b.bx + (40 - tw) * 0.5f, dy + 7, t.bone, b.s);
    }
    float ok = x + w - 24 - 120, oy = y + h - 56;
    bool o = hit(m, PC_UI_MOUSE_CTR_QTY, 0, ok, oy, ok + 120, oy + 40) || hovered(PC_UI_MOUSE_CTR_QTY, 0);
    f.dl->AddRectFilled(f.p(ok, oy), f.p(ok + 120, oy + 40), c.discard_count > 0 ? t.accent : o ? t.rule2 : t.rule,
        f.s(5));
    float tw = measure(f, fonts.label, 15, "OK", 0.26f);
    text(f, fonts.label, 15, ok + (120 - tw) * 0.5f, oy + 12, t.bone, "OK", 0.26f);
}

const char* subtitle(const pc_ui_ctr_model& c, std::string& buf)
{
    if (c.phase == PC_UI_CTR_PH_TRANSFER && c.session && c.mode != PC_UI_CTR_TOP)
        return c.dir ? "Store \xe2\x80\x94 bag to container" : "Remove \xe2\x80\x94 container to bag";
    buf = c.location_name;
    return buf.c_str();
}

void hints(const pc_ui_ctr_model& c, ScreenOut& out)
{
    switch (c.phase) {
    case PC_UI_CTR_PH_PROMPT_CHECK:
    case PC_UI_CTR_PH_PROMPT_WARN:
    case PC_UI_CTR_PH_PROMPT_CANCEL:
        out.hint(PC_PROMPT_CIRCLE, -1, "Confirm", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "No", PC_UI_BACK);
        return;
    case PC_UI_CTR_PH_WAIT:
        out.hint(PC_PROMPT_CIRCLE, -1, "Continue", PC_UI_CONFIRM);
        return;
    case PC_UI_CTR_PH_TRANSFER:
        break;
    default:
        return;
    }
    if (!c.session)
        return;
    switch (c.mode) {
    case PC_UI_CTR_TOP:
        out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Leave", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Cancel all", PC_UI_EXIT);
        break;
    case PC_UI_CTR_LIST:
        if (c.rows > 0)
            out.hint(PC_PROMPT_CIRCLE, -1, c.tab == 7 ? "Put back" : "Pick", PC_UI_CONFIRM);
        if (c.rows > 0 && c.tab < 6)
            out.hint(PC_PROMPT_SQUARE, -1, "Details", PC_UI_ALT);
        out.hint(PC_PROMPT_CROSS, -1, c.staged ? "End" : "Back", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Cancel all", PC_UI_EXIT);
        out.hint(PC_PROMPT_L1, PC_PROMPT_R1, "Tabs", PC_UI_TAB_NEXT);
        break;
    case PC_UI_CTR_PICK2:
        out.hint(PC_PROMPT_CIRCLE, -1, "Actions / move here", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Cancel all", PC_UI_EXIT);
        break;
    case PC_UI_CTR_DETAIL:
        out.hint(PC_PROMPT_L1, PC_PROMPT_R1, "Cycle", PC_UI_TAB_NEXT);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Cancel all", PC_UI_EXIT);
        break;
    case PC_UI_CTR_DISCARD_QTY:
        out.hint(PC_PROMPT_UP, PC_PROMPT_DOWN, "Amount", 0);
        out.hint(PC_PROMPT_LEFT, PC_PROMPT_RIGHT, "Step", 0);
        out.hint(PC_PROMPT_CIRCLE, -1, "OK", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    default:
        out.hint(PC_PROMPT_CIRCLE, -1, "Confirm", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Cancel all", PC_UI_EXIT);
        break;
    }
}

} // namespace

// The Container's title where the tabs go (also the Data screen's save list in container mode).
void container_title(const Frame& f, const ui::Theme& t, const char* sub)
{
    float y = t.top + 13;
    float w = text(f, fonts.label, t.tab_size, t.margin_x, y, t.bone, "CONTAINER", 0.3f);
    float base = t.top + t.tab_height;
    f.dl->AddLine(f.p(t.margin_x, base), f.p(f.W - t.margin_x, base), t.rule, f.s(1));
    f.dl->AddRectFilled(f.p(t.margin_x, base - 1), f.p(t.margin_x + w, base + 1), t.accent);
    if (sub && sub[0]) {
        std::string s = upper(sub);
        text_right(f, fonts.label, t.label_size, f.W - t.margin_x, y + 3, t.dim, s.c_str(), 0.26f);
    }
}

void draw_container_frame(const Frame& f, const ui::Theme& t, float dt, Pointer& m, ScreenOut& out)
{
    const pc_ui_ctr_model& c = *pc_ui_ctr_state();
    Layout L = layout(f.W);
    pc_ui_ctr_hits_clear();
    ImVec2 v0((float)pc_gl_view[0], (float)pc_gl_view[1]);
    ImVec2 v1((float)(pc_gl_view[0] + pc_gl_view[2]), (float)(pc_gl_view[1] + pc_gl_view[3]));
    f.dl->AddRectFilled(v0, v1, t.ground);
    std::string buf;
    const char* shown = nullptr; // a text already on screen (the card's help, the empty tab's line)
    container_title(f, t, subtitle(c, buf));
    out.crumb = "Container";
    switch (c.phase) {
    case PC_UI_CTR_PH_PROMPT_CHECK:
    case PC_UI_CTR_PH_PROMPT_WARN:
    case PC_UI_CTR_PH_PROMPT_CANCEL:
        prompt_card(f, t, c, m, c.phase != PC_UI_CTR_PH_PROMPT_CHECK);
        break;
    case PC_UI_CTR_PH_WAIT:
        wait_card(f, t, c, m);
        break;
    case PC_UI_CTR_PH_TRANSFER:
        if (!c.session) {
            busy_line(f, t, "Opening the container\xe2\x80\xa6");
            out.busy = true;
            break;
        }
        if (c.mode == PC_UI_CTR_TOP) {
            draw_top(f, t, L, c, m);
            anim.tab = -1;
        } else {
            tab_strip(f, t, L, c, m);
            draw_rows(f, t, L, c, dt, m);
            shown = draw_panel(f, t, L, c);
            if (c.rows == 0)
                shown = c.empty_text;
            if (c.mode == PC_UI_CTR_ACTIONS || c.mode == PC_UI_CTR_SORT)
                draw_menu(f, t, L, c, m);
            else if (c.mode == PC_UI_CTR_DISCARD_QTY)
                qty_card(f, t, c, m);
            else if (c.mode == PC_UI_CTR_DISCARD_CONFIRM)
                confirm_card(f, t, c, m, "Discard");
            else if (c.mode == PC_UI_CTR_COMMIT_CONFIRM)
                confirm_card(f, t, c, m, c.dir ? "Store these items?" : "Remove these items?");
            anim.tab = c.tab;
        }
        out.crumb = c.mode == PC_UI_CTR_TOP ? "Container" : c.dir ? "Container \xe2\x80\xba Store" : "Container \xe2\x80\xba Remove";
        break;
    case PC_UI_CTR_PH_CLOSING:
        out.busy = true;
        break;
    default:
        busy_line(f, t, !c.handed_back && c.m7_state >= 7 && c.m7_state <= 8 ? "Opening the container\xe2\x80\xa6"
                : c.m7_state <= 3                                         ? "Checking the memory card\xe2\x80\xa6"
                                                                          : "Reading the memory card\xe2\x80\xa6");
        out.busy = true;
        break;
    }
    // the info line (the original's information box)
    if (c.phase == PC_UI_CTR_PH_TRANSFER && c.session && c.info[0] && !(shown && !std::strcmp(shown, c.info))) {
        float x0 = t.margin_x, x1 = f.W - t.margin_x, y = 636;
        f.dl->AddRectFilled(f.p(x0, y + 2), f.p(x0 + 2, y + 18), t.gold);
        text(f, fonts.label, 15, x0 + 12, y, t.ash, fit(f, fonts.label, 15, c.info, x1 - x0 - 12, 0.02f).c_str(), 0.02f);
    }
    anim.mode = c.mode;
    hints(c, out);
}

} // namespace uidraw
