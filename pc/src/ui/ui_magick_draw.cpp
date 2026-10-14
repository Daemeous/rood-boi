// Modern menus: the Magick tab (pc/docs/Modern_Menus.md). Sub-tabs with the
// L2 / R2 bumpers: the schools with a spell learned (Warlock, Shaman,
// Sorcerer, Enchanter) and Teleport once the game has it.
//
// The left column has the MP meter (the focused cost marked on it) above the
// list. A school's list: spell name, the LV chips of a levelled spell (the
// focused one's chosen level lit; a click picks a level) and the MP cost (red
// when it is more than Ashley has). Teleport's list: destination, a "Here"
// mark on the circle Ashley stands on, MP cost. A row that cannot be used is
// greyed with why under its name; when every row is blocked for the same
// reason (silence, a menu mask, Teleport off) that reason shows once above the
// list instead. The right card explains the focused spell (school and level,
// MP cost, the game's description with its element / kind / target tags, why
// it cannot be cast) or destination; the school's own line when the list is
// empty.
//
// State comes from the shell (pc_ui_shell_magick_view, the bridge's snapshot);
// nothing here changes it except through the pointer (Pointer hits, applied at
// the shell's next tick).
#include "ui/ui_draw_internal.h"
#include "button_prompts.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace uidraw {
namespace {

const char* const SUB_NAMES[PC_UI_SCHOOLS + 1] = {"Warlock", "Shaman", "Sorcerer", "Enchanter", "Teleport"};
const char* const NEXT = " \xe2\x80\xba "; // " › "

// layout units
const float BOTTOM = 720 - 64; // the body's bottom (above the footer)
const float ROW_H = 48;        // a spell or a destination
const float CHIP_W = 34, CHIP_H = 20, CHIP_GAP = 4; // a LV chip

struct Anim {
    float list_y = 0, scroll = 0, scroll_target = 0;
    int kind = -2;
} anim;

// One row of the list, whichever sub-tab.
struct Row {
    const char* name;
    int mp;
    bool usable;
    const char* reason;
    bool here;      // Teleport: the circle Ashley stands on
    int levels;     // a levelled spell's levels (1: not levelled)
    const pc_ui_spell* spell;
};

Row row_of(const pc_ui_magick& g, int kind, int r, int level)
{
    if (kind == PC_UI_MG_TELEPORT) {
        const pc_ui_destination& d = g.destinations[r];
        return {d.name, d.cost, d.usable != 0, d.reason, d.current != 0, 1, nullptr};
    }
    const pc_ui_spell& e = g.schools[kind].spells[r];
    int n = e.levels < 1 ? 1 : e.levels > PC_UI_SPELL_LEVELS ? PC_UI_SPELL_LEVELS : e.levels;
    int l = level < 0 ? 0 : level >= n ? n - 1 : level;
    const pc_ui_spell_level& lv = e.level[l];
    return {e.name, lv.mp, lv.usable != 0, lv.reason, false, n, &e};
}

// A gold bar and a gold paragraph (why something cannot be done); returns its height.
float note(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* s)
{
    f.dl->AddRectFilled(f.p(x, y + 1), f.p(x + 2, y + t.body_size + 3), t.gold);
    return paragraph(f, t, x + 10, y, w - 10, t.gold, s, 13.5f);
}

// Sub-tabs (the shown ones) with the L2 / R2 bumpers; returns the y below them.
float subtabs(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_magick_view& v, Pointer& m)
{
    float x = x0, size = 12;
    for (int i = 0; i < v.subs; ++i) {
        std::string name = upper(SUB_NAMES[v.sub_kind[i]]);
        float w = measure(f, fonts.label, size, name.c_str(), 0.26f);
        bool over = m.hit(PC_UI_MOUSE_SUB, i, x - 6, y - 8, x + w + 6, y + 23);
        text(f, fonts.label, size, x, y, i == v.sub ? t.bone : over ? t.ash : t.dim, name.c_str(), 0.26f);
        if (i == v.sub)
            f.dl->AddRectFilled(f.p(x, y + 21), f.p(x + w, y + 23), t.accent);
        x += w + 26;
    }
    f.dl->AddLine(f.p(x0, y + 22), f.p(x1, y + 22), t.rule, f.s(1));
    if (v.subs > 1) {
        float cy = y + 6, gx = x1;
        float rw = glyph(f, PC_PROMPT_R2, 18, 0, 0, true), lw = glyph(f, PC_PROMPT_L2, 18, 0, 0, true);
        gx -= rw;
        m.hit(PC_UI_MOUSE_BUTTON, PC_UI_SUB_NEXT, gx - 3, y - 8, gx + rw + 3, y + 20);
        glyph(f, PC_PROMPT_R2, 18, gx, cy);
        gx -= 6 + lw;
        m.hit(PC_UI_MOUSE_BUTTON, PC_UI_SUB_PREV, gx - 3, y - 8, gx + lw + 3, y + 20);
        glyph(f, PC_PROMPT_L2, 18, gx, cy);
    }
    return y + 23;
}

// "MP ▬▬▬▬ 36 / 50" right-aligned at x1, the focused cost (spend > 0) lighter
// at the fill's end; returns the meter's left edge.
float mp_meter(const Frame& f, const ui::Theme& t, float x1, float y, int mp, int max, int spend)
{
    float tw = f.W < 1100 ? 120.0f : 160.0f;
    float rx = x1 - text_right(f, fonts.data, 13, x1, y - 1, t.bone, num(max).c_str());
    rx -= text_right(f, fonts.data, 13, rx, y - 1, t.dim, " / ");
    rx -= text_right(f, fonts.data, 13, rx, y - 1, t.bone, num(mp).c_str()) + 10;
    float bx0 = rx - tw, by = y + 5, bh = 4;
    f.dl->AddRectFilled(f.p(bx0, by), f.p(rx, by + bh), t.rule);
    float a = max > 0 ? (float)(mp < 0 ? 0 : mp > max ? max : mp) / max : 0;
    float keep = spend > 0 && spend <= mp && max > 0 ? (float)(mp - spend) / max : a;
    if (keep > 0)
        f.dl->AddRectFilled(f.p(bx0, by), f.p(bx0 + tw * keep, by + bh), t.mp);
    if (a > keep)
        f.dl->AddRectFilled(f.p(bx0 + tw * keep, by), f.p(bx0 + tw * a, by + bh), fade(t.mp, 0.4f));
    float lx = bx0 - 8 - measure(f, fonts.label, 12, "MP", 0.16f);
    text(f, fonts.label, 12, lx, y, t.bone, "MP", 0.16f);
    return lx;
}

// The LV chips of a levelled spell, right-aligned at xr, centred on cy; the
// chosen one lit when `chosen` >= 0. Hit tests them when hits. Returns their width.
float level_chips(const Frame& f, const ui::Theme& t, float xr, float cy, const pc_ui_spell& e, int levels,
    int chosen, int have_mp, bool hits, Pointer& m)
{
    float w = levels * CHIP_W + (levels - 1) * CHIP_GAP, x = xr - w;
    for (int k = 0; k < levels; ++k, x += CHIP_W + CHIP_GAP) {
        const pc_ui_spell_level& lv = e.level[k];
        bool on = k == chosen, poor = lv.mp > have_mp;
        float y0 = cy - CHIP_H * 0.5f, y1 = cy + CHIP_H * 0.5f;
        bool over = hits && m.hit(PC_UI_MOUSE_SPELL_LEVEL, k, x, y0 - 4, x + CHIP_W, y1 + 4);
        if (on)
            f.dl->AddRectFilled(f.p(x, y0), f.p(x + CHIP_W, y1), t.accent_soft);
        f.dl->AddRect(f.p(x, y0), f.p(x + CHIP_W, y1), on ? t.accent : over ? t.ash : t.rule2, 0, 0, f.s(1));
        std::string s = "LV" + num(k + 1);
        float sw = measure(f, fonts.data, 11, s.c_str());
        unsigned col = poor ? (on ? t.down : fade(t.down, 0.6f)) : on ? t.bone : chosen >= 0 ? t.ash : t.dim;
        text(f, fonts.data, 11, x + (CHIP_W - sw) * 0.5f, cy - 7, col, s.c_str());
    }
    return w;
}

// The list's rows from y0 (as many whole rows as fit above BOTTOM), scrolled so the
// focused row shows.
void list(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_magick& g,
    const pc_ui_magick_view& v, bool shared_block, float dt, bool reset, Pointer& m)
{
    int count = v.rows;
    int vis = (int)((BOTTOM - y0) / ROW_H);
    vis = vis < 1 ? 1 : vis;
    float y1 = y0 + vis * ROW_H; // whole rows only
    int cursor = v.cursor < 0 ? 0 : v.cursor >= count ? count - 1 : v.cursor;
    float st = reset ? 0 : anim.scroll_target;
    if (cursor < st)
        st = (float)cursor;
    if (cursor > st + vis - 1)
        st = (float)(cursor - vis + 1);
    float max_scroll = (float)(count > vis ? count - vis : 0);
    st = st < 0 ? 0 : st > max_scroll ? max_scroll : st;
    anim.scroll_target = st;
    anim.scroll = reset ? st : approach(anim.scroll, st, dt, t.focus_grow);
    float target = y0 + (cursor - anim.scroll) * ROW_H;
    anim.list_y = reset ? target : approach(anim.list_y, target, dt, t.focus_grow);
    // the chips sit in one column (the widest levelled spell's) left of the costs
    float cost_w = measure(f, fonts.data, 13, "MP 888");
    int max_levels = 1;
    if (v.kind != PC_UI_MG_TELEPORT)
        for (int i = 0; i < count; ++i) {
            int n = row_of(g, v.kind, i, 0).levels;
            max_levels = n > max_levels ? n : max_levels;
        }
    float chips_r = x1 - 12 - cost_w - 14;
    float chips_w = max_levels > 1 ? max_levels * CHIP_W + (max_levels - 1) * CHIP_GAP : 0;
    f.dl->PushClipRect(f.p(x0, y0), f.p(x1 + 1, y1), true);
    focus_band(f, t, x0, anim.list_y, x1, anim.list_y + ROW_H);
    for (int i = 0; i < count; ++i) {
        float ry = y0 + (i - anim.scroll) * ROW_H;
        if (ry + ROW_H < y0 || ry > y1)
            continue;
        bool on = i == cursor;
        Row r = row_of(g, v.kind, i, on ? v.level : 0);
        float hy0 = ry < y0 ? y0 : ry, hy1 = ry + ROW_H > y1 ? y1 : ry + ROW_H;
        if (hy1 > hy0)
            m.hit(PC_UI_MOUSE_SPELL, i, x0, hy0, x1, hy1);
        float cy = ry + ROW_H * 0.5f, right = x1 - 12;
        std::string cost = "MP " + num(r.mp);
        unsigned cc = r.mp > g.mp ? t.down : !r.usable ? t.dim : on ? t.bone : t.ash;
        text_right(f, fonts.data, 13, right, cy - 7, cc, cost.c_str());
        right = x1 - 12 - cost_w - 14;
        if (r.levels > 1) {
            float lr = chips_r - (chips_w - (r.levels * CHIP_W + (r.levels - 1) * CHIP_GAP)); // left-aligned column
            level_chips(f, t, lr, cy, *r.spell, r.levels, on ? v.level : -1, g.mp, on && hy1 > hy0, m);
        }
        if (chips_w > 0)
            right = chips_r - chips_w - 14;
        bool sub = !r.usable && r.reason[0] && !shared_block;
        float nx = x0 + 14, ny = sub ? ry + 6 : cy - 11, nw = right - nx;
        unsigned nc = !r.usable ? t.dim : on ? t.bone : t.ash;
        if (r.here) { // the "Here" mark after the name
            float hw = 8 + 6 + measure(f, fonts.label, 10, "HERE", 0.22f);
            std::string name = fit(f, fonts.name, 18, r.name, nw - hw - 10);
            float w = text(f, fonts.name, 18, nx, ny, nc, name.c_str());
            float hx = nx + w + 12;
            f.dl->AddCircleFilled(f.p(hx + 3, ny + 11), f.s(3), t.gold);
            text(f, fonts.label, 10, hx + 12, ny + 5, t.gold, "HERE", 0.22f);
        } else {
            text(f, fonts.name, 18, nx, ny, nc, fit(f, fonts.name, 18, r.name, nw).c_str());
        }
        if (sub)
            text(f, fonts.data, 11, nx, ry + 29, fade(t.gold, on ? 1.0f : 0.7f),
                fit(f, fonts.data, 11, r.reason, nw).c_str());
        f.dl->AddLine(f.p(x0, ry + ROW_H), f.p(x1, ry + ROW_H), t.rule, f.s(1));
    }
    f.dl->PopClipRect();
    if (count > vis) { // scroll position
        float track = y1 - y0, thumb = track * vis / count, ty = y0 + (track - thumb) * (anim.scroll / max_scroll);
        f.dl->AddRectFilled(f.p(x1 + 6, y0), f.p(x1 + 8, y1), t.rule);
        f.dl->AddRectFilled(f.p(x1 + 6, ty), f.p(x1 + 8, ty + thumb), t.ash);
    }
}

// MENU0's descriptions start with tags ("Physical Attack; Conjuration; Single
// Target.") before the effect: split there when the first sentence has a ';'.
void split_help(const char* help, std::string& tags, std::string& body)
{
    const char* dot = std::strstr(help, ". ");
    const char* semi = std::strchr(help, ';');
    if (dot && semi && semi < dot) {
        std::string head(help, dot);
        for (size_t p; (p = head.find("; ")) != std::string::npos;)
            head.replace(p, 2, DOT);
        tags = head;
        body = dot + 2;
    } else {
        tags.clear();
        body = help;
    }
}

void spell_card(const Frame& f, const ui::Theme& t, const Card& c, const pc_ui_magick& g, const pc_ui_magick_view& v,
    const pc_ui_school& school)
{
    float x = c.x + 20, xr = c.x + c.w - 20, w = xr - x, y = c.y + 18;
    card_begin(f);
    if (v.rows <= 0) { // nothing to focus: the school
        label(f, t, x, y, "School of magick");
        y += 13 + 8;
        text(f, fonts.name, 25, x, y, t.bone, fit(f, fonts.name, 25, school.name[0] ? school.name : SUB_NAMES[v.kind], w).c_str());
        y += 31 + 8;
        if (school.help[0])
            y += paragraph(f, t, x, y, w, t.ash, school.help);
        card_end(f, t, c, y + 18);
        return;
    }
    const pc_ui_spell& e = school.spells[v.cursor];
    Row r = row_of(g, v.kind, v.cursor, v.level);
    std::string head = std::string(school.name[0] ? school.name : SUB_NAMES[v.kind]) + " spell";
    if (r.levels > 1)
        head += std::string(DOT) + "LV " + num(v.level + 1) + " of " + num(r.levels);
    label(f, t, x, y, head.c_str());
    y += 13 + 8;
    std::string name = e.name;
    if (r.levels > 1)
        name += " LV" + num(v.level + 1);
    text(f, fonts.name, 25, x, y, r.usable ? t.bone : t.ash, fit(f, fonts.name, 25, name.c_str(), w).c_str());
    y += 31 + 8;
    // MP n · MP a / b
    bool poor = r.mp > g.mp;
    float mx = x + text(f, fonts.label, t.label_size, x, y, poor ? t.down : t.ash, ("MP " + num(r.mp)).c_str(), 0.2f);
    std::string have = DOT + std::string("HAVE ") + num(g.mp) + " / " + num(g.mp_max);
    text(f, fonts.label, t.label_size, mx, y, t.dim, have.c_str(), 0.2f);
    y += 13 + 12;
    std::string tags, body;
    split_help(e.help, tags, body);
    if (!tags.empty()) {
        y += paragraph(f, t, x, y, w, t.bone, tags.c_str(), 13) + 6;
    }
    if (!body.empty())
        y += paragraph(f, t, x, y, w, t.ash, body.c_str());
    if (!r.usable && r.reason[0]) {
        y += 12;
        y += note(f, t, x, y, w, r.reason);
    } else if (r.usable) {
        y += 16;
        f.dl->AddLine(f.p(x, y), f.p(xr, y), t.rule, f.s(1));
        y += 12;
        std::string left = "Leaves " + num(g.mp - r.mp) + " MP";
        text(f, fonts.data, 12, x, y, t.dim, left.c_str());
        y += 16;
    }
    card_end(f, t, c, y + 18);
}

void teleport_card(const Frame& f, const ui::Theme& t, const Card& c, const pc_ui_magick& g,
    const pc_ui_magick_view& v)
{
    float x = c.x + 20, xr = c.x + c.w - 20, w = xr - x, y = c.y + 18;
    card_begin(f);
    if (v.rows <= 0) {
        label(f, t, x, y, "Magick");
        y += 13 + 8;
        text(f, fonts.name, 25, x, y, t.bone, "Teleport");
        y += 31 + 8;
        if (g.teleport_help[0])
            y += paragraph(f, t, x, y, w, t.ash, g.teleport_help);
        if (g.teleport_reason[0]) {
            y += 12;
            y += note(f, t, x, y, w, g.teleport_reason);
        }
        card_end(f, t, c, y + 18);
        return;
    }
    const pc_ui_destination& d = g.destinations[v.cursor];
    label(f, t, x, y, d.current ? "Teleport \xc2\xb7 You are here" : "Teleport \xc2\xb7 Magic circle");
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, d.usable ? t.bone : t.ash, fit(f, fonts.name, 25, d.name, w).c_str());
    y += 31 + 8;
    bool poor = d.cost > g.mp;
    float mx = x + text(f, fonts.label, t.label_size, x, y, poor ? t.down : t.ash, ("MP " + num(d.cost)).c_str(), 0.2f);
    std::string have = DOT + std::string("HAVE ") + num(g.mp) + " / " + num(g.mp_max);
    text(f, fonts.label, t.label_size, mx, y, t.dim, have.c_str(), 0.2f);
    y += 13 + 12;
    if (d.help[0])
        y += paragraph(f, t, x, y, w, t.ash, d.help);
    const char* why = !g.teleport_enabled && g.teleport_reason[0] ? g.teleport_reason : d.reason;
    if (!d.usable && why[0] && std::strcmp(why, d.help)) { // (a paled circle's help already says why)
        y += 12;
        y += note(f, t, x, y, w, why);
    } else if (d.usable) {
        y += 16;
        f.dl->AddLine(f.p(x, y), f.p(xr, y), t.rule, f.s(1));
        y += 12;
        text(f, fonts.data, 12, x, y, t.dim, ("Leaves " + num(g.mp - d.cost) + " MP").c_str());
        y += 16;
    }
    card_end(f, t, c, y + 18);
}

// Every row blocked for one reason: that reason (else null).
const char* shared_reason(const pc_ui_magick& g, const pc_ui_magick_view& v)
{
    if (v.kind == PC_UI_MG_TELEPORT && !g.teleport_enabled)
        return g.teleport_reason[0] ? g.teleport_reason : nullptr;
    if (v.rows < 2)
        return nullptr;
    const char* first = nullptr;
    for (int i = 0; i < v.rows; ++i) {
        Row r = row_of(g, v.kind, i, 0);
        if (r.usable || !r.reason[0])
            return nullptr;
        if (!first)
            first = r.reason;
        else if (std::strcmp(first, r.reason))
            return nullptr;
    }
    return first;
}

} // namespace

void draw_magick(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    const pc_ui_magick& g = *pc_ui_shell_magick();
    pc_ui_magick_view v;
    pc_ui_shell_magick_view(&v);
    float cw = card_width(f.W);
    float x0 = t.margin_x, x1 = f.W - t.margin_x - cw - 24;
    Card card = {f.W - t.margin_x - cw, top, cw};
    bool reset = fresh || anim.kind != v.kind;
    anim.kind = v.kind;
    float y = top;
    if (v.subs > 0)
        y = subtabs(f, t, x0, x1, top, v, m);
    if (!g.valid || v.subs <= 0 || v.kind < 0) {
        if (!g.valid) {
            spinner(f, x0 + 22, y + 30, 6, t.ash);
            text(f, fonts.label, t.label_size, x0 + 38, y + 24, t.ash, "LOADING", 0.26f);
        } else {
            mp_meter(f, t, x1, y + 2, g.mp, g.mp_max, 0);
            f.dl->AddLine(f.p(x0, y + 22), f.p(x1, y + 22), t.rule, f.s(1));
            paragraph(f, t, x0 + 14, y + 40, x1 - x0 - 28, t.dim, "No spells learned yet.");
            float x = card.x + 20, w = card.w - 40, cy = card.y + 18;
            card_begin(f);
            label(f, t, x, cy, "Magick");
            cy += 13 + 8;
            text(f, fonts.name, 25, x, cy, t.bone, "No spells yet");
            cy += 31 + 8;
            cy += paragraph(f, t, x, cy, w, t.ash,
                "Spells are learned from grimoires. Once one is read, its school shows here.");
            card_end(f, t, card, cy + 18);
            out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
        }
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        out.crumb = "Magick";
        return;
    }
    bool tele = v.kind == PC_UI_MG_TELEPORT;
    const char* sub_name = SUB_NAMES[v.kind];
    // the header: what the list is, and MP with the focused cost marked
    y += 14;
    Row focus = {};
    bool has_focus = v.rows > 0 && v.cursor >= 0 && v.cursor < v.rows;
    if (has_focus)
        focus = row_of(g, v.kind, v.cursor, v.level);
    label(f, t, x0, y, tele ? "Magic circles" : (std::string(sub_name) + " spells").c_str());
    mp_meter(f, t, x1, y, g.mp, g.mp_max, has_focus && focus.usable ? focus.mp : 0);
    y += 13 + 10;
    const char* shared = shared_reason(g, v);
    if (shared) {
        y += note(f, t, x0 + 14, y, x1 - x0 - 28, shared) + 12;
    }
    if (v.rows > 0)
        list(f, t, x0, x1, y, g, v, shared != nullptr, dt, reset, m);
    else
        paragraph(f, t, x0 + 14, y + 10, x1 - x0 - 28, t.dim,
            tele ? "No magic circle found yet." : "No spells of this school learned yet.");
    if (tele)
        teleport_card(f, t, card, g, v);
    else
        spell_card(f, t, card, g, v, g.schools[v.kind]);
    // the crumb and the hints
    out.crumb = "Magick" + std::string(NEXT) + sub_name;
    if (has_focus) {
        std::string name = focus.name;
        if (focus.levels > 1)
            name += " LV" + num(v.level + 1);
        out.crumb += NEXT + name;
        if (focus.usable)
            out.hint(PC_PROMPT_CIRCLE, -1, tele ? "Teleport" : "Cast", PC_UI_CONFIRM);
        if (focus.levels > 1)
            out.hint(PC_PROMPT_LEFT, PC_PROMPT_RIGHT, "Level", v.level + 1 < focus.levels ? PC_UI_RIGHT : PC_UI_LEFT);
    }
    out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
    out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
}

} // namespace uidraw
