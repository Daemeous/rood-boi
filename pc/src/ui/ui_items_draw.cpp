// Modern menus: the Items tab (pc/docs/Modern_Menus.md).
//
// Across the top: the seven bag categories as chips (name and how many; the
// L2 / R2 bumpers at the right). The left column: the category's name and a
// bag meter ("3 / 8", gold when full), the game's lock reason once when the
// menu's mask greys everything, then the list: a ring with a line icon per
// kind, the name, a sub line (material and kind, the set it is part of; a
// misc item that cannot be used says why, faintly), "×n" for misc stacks and
// an EQUIPPED / IN A SET mark. Long lists scroll with the focused row kept in
// view.
//
// The right card explains the focused row: kind, name, material; DP / PP
// meters, STR / INT / AGL, range / RISK / damage type (weapons, blades) or
// the blunt / edged / piercing type values (the original's TYPE page:
// grips, shields, armor, gems, accessories), the weapon's parts and gems, the
// game's description (ITEMHELP) and why a misc item cannot be used or
// discarded. Square (DETAILS) widens it into two columns with the class,
// affinity and type bars (the Equipment compare card's bars).
//
// The menus open as popovers next to the focused row: the actions (greyed
// with the game's reason), the quantity picker (count, step 100 / 10 / 1,
// arrows a click presses) and the Yes / No confirm with the game's question.
// Sort lists the category's stats on the card with the focused one's
// description. Move marks the row moved and tags the target Swap or Merge
// (the same misc item), the card saying what will happen.
//
// State comes from the shell (pc_ui_shell_items_view, the bridge's snapshot);
// nothing here changes it except through the pointer (Pointer hits, applied
// at the shell's next tick).
#include "ui/ui_draw_internal.h"
#include "button_prompts.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace uidraw {
namespace {

const char* const CAT_NAMES[PC_UI_ITEM_CATEGORIES] = {"Weapons", "Blades", "Grips", "Shields", "Armor", "Gems", "Misc"};
const char* const CAT_ONE[PC_UI_ITEM_CATEGORIES] = {"Weapon", "Blade", "Grip", "Shield", "Armor", "Gem", "Item"};
// ITEMHELP's blade categories (dagger ..) and armor categories (armorCategory ..), and the grips' kinds
const char* const BLADE_KINDS[10] = {"", "Dagger", "Sword", "Great Sword", "Axe & Mace", "Great Axe", "Staff",
    "Heavy Mace", "Polearm", "Crossbow"};
const char* const GRIP_KINDS[5] = {"", "Short grip", "Long grip", "Polearm grip", "Crossbow bolt"};
const char* const ARMOR_KINDS[8] = {"", "Shield", "Helm", "Breast armor", "Leggings", "Glove", "Armor", "Accessory"};
const char* const TYPES[3] = {"Blunt", "Edged", "Piercing"};
const char* const NEXT = " \xe2\x80\xba "; // " › "
const char* const TIMES = "\xc3\x97";      // "×"

// layout units
const float BOTTOM = 720 - 64; // the body's bottom (above the footer)
const float ROW_H = 48;        // a list row
const float RING_R = 16;       // a row's icon ring
const float MENU_ROW = 30;     // a popover's or the sort list's row

float details_width(float W) { return W < 1100 ? 572.0f : 660.0f; }

struct Anim {
    float list_y = 0, scroll = 0, scroll_target = 0, card_w = 0;
    float sort_scroll = 0;
    int category = -1, mode = -1;
} anim;

// ---- Icons (our own line icons, as the Equipment tab's) ----
enum ItemIcon { II_BLADE = 100, II_GRIP, II_GEM, II_FLASK, II_BOOK, II_KEY, II_SIGIL };

struct Pen {
    const Frame& f;
    float cx, cy, u;
    unsigned col;
    float th;
    ImVec2 P(float x, float y) const { return f.p(cx + (x - 10) * u, cy + (y - 10) * u); }
    void line(const float* xy, int n, bool closed) const
    {
        for (int i = 0; i < n; ++i)
            f.dl->PathLineTo(P(xy[2 * i], xy[2 * i + 1]));
        f.dl->PathStroke(col, closed ? ImDrawFlags_Closed : 0, th);
    }
    void circle(float x, float y, float r) const { f.dl->AddCircle(P(x, y), r * u * f.k, col, 20, th); }
};

void item_icon(const Frame& f, int ic, float cx, float cy, float size, unsigned col)
{
    if (ic < II_BLADE) {
        icon(f, (Icon)ic, cx, cy, size, col);
        return;
    }
    Pen p{f, cx, cy, size / 20.0f, col, f.s(1.3f * size / 20.0f)};
    switch (ic) {
    case II_BLADE: { // a blade and its guard, no grip
        const float blade[] = {6.5f, 12.0f, 14.0f, 4.5f, 16.0f, 4.0f, 15.5f, 6.0f, 8.0f, 13.5f};
        const float guard[] = {4.5f, 10.5f, 9.5f, 15.5f};
        p.line(blade, 5, true);
        p.line(guard, 2, false);
        break;
    }
    case II_GRIP: { // a wrapped handle with a pommel
        const float handle[] = {5.0f, 13.5f, 6.5f, 15.0f, 14.0f, 7.5f, 12.5f, 6.0f};
        const float guard[] = {11.0f, 4.0f, 16.0f, 9.0f};
        const float wrap1[] = {7.5f, 11.0f, 9.0f, 12.5f}, wrap2[] = {9.5f, 9.0f, 11.0f, 10.5f};
        p.line(handle, 4, true);
        p.line(guard, 2, false);
        p.line(wrap1, 2, false);
        p.line(wrap2, 2, false);
        p.circle(4.3f, 15.7f, 1.6f);
        break;
    }
    case II_GEM: { // a cut stone
        const float out[] = {6.0f, 4.5f, 14.0f, 4.5f, 17.0f, 8.5f, 10.0f, 16.5f, 3.0f, 8.5f};
        const float crown[] = {3.0f, 8.5f, 17.0f, 8.5f};
        const float facets[] = {6.0f, 4.5f, 8.0f, 8.5f, 10.0f, 16.5f, 12.0f, 8.5f, 14.0f, 4.5f};
        p.line(out, 5, true);
        p.line(crown, 2, false);
        p.line(facets, 5, false);
        break;
    }
    case II_FLASK: { // a phial
        const float body[] = {8.5f, 3.5f, 8.5f, 7.5f, 4.5f, 13.0f, 4.5f, 15.0f, 6.0f, 16.5f, 14.0f, 16.5f, 15.5f, 15.0f,
            15.5f, 13.0f, 11.5f, 7.5f, 11.5f, 3.5f};
        const float lip[] = {7.5f, 3.5f, 12.5f, 3.5f};
        const float level[] = {5.5f, 12.0f, 14.5f, 12.0f};
        p.line(body, 10, false);
        p.line(lip, 2, false);
        p.line(level, 2, false);
        break;
    }
    case II_BOOK: { // a grimoire
        const float cover[] = {5.0f, 3.5f, 15.0f, 3.5f, 15.0f, 16.5f, 5.0f, 16.5f};
        const float spine[] = {7.0f, 3.5f, 7.0f, 16.5f};
        const float mark[] = {9.5f, 7.0f, 13.0f, 7.0f};
        p.line(cover, 4, true);
        p.line(spine, 2, false);
        p.line(mark, 2, false);
        break;
    }
    case II_KEY: {
        const float shaft[] = {9.5f, 10.0f, 17.0f, 10.0f};
        const float bit1[] = {14.0f, 10.0f, 14.0f, 13.0f}, bit2[] = {16.5f, 10.0f, 16.5f, 12.5f};
        p.circle(6.5f, 10.0f, 3.0f);
        p.line(shaft, 2, false);
        p.line(bit1, 2, false);
        p.line(bit2, 2, false);
        break;
    }
    case II_SIGIL: { // a seal
        const float tri[] = {10.0f, 5.0f, 14.3f, 12.5f, 5.7f, 12.5f};
        p.circle(10.0f, 10.0f, 6.8f);
        p.line(tri, 3, true);
        break;
    }
    default:
        break;
    }
}

// misc ids: keys 458..465, sigils 466..470 (neither usable nor discardable)
bool is_key(int id) { return id >= 458 && id <= 465; }
bool is_sigil(int id) { return id >= 466 && id <= 470; }
bool is_grimoire(const pc_ui_item_row& e) { return std::strncmp(e.name, "Grimoire", 8) == 0; }

int row_icon(int c, const pc_ui_item_row& e)
{
    switch (c) {
    case PC_UI_IC_WEAPON:
        return IC_SWORD;
    case PC_UI_IC_BLADE:
        return II_BLADE;
    case PC_UI_IC_GRIP:
        return II_GRIP;
    case PC_UI_IC_SHIELD:
        return IC_SHIELD;
    case PC_UI_IC_ARMOR:
        // the row's kind (ITEMHELP armorCategory: Helm, Breast Armor, Leggings, Glove; accessories)
        return std::strstr(e.sub, "Helm")       ? IC_HEAD
            : std::strstr(e.sub, "Legging")     ? IC_LEGS
            : std::strstr(e.sub, "Glove")       ? IC_ARM
            : std::strstr(e.sub, "Accessory")   ? IC_RING
            : std::strstr(e.sub, "Shield")      ? IC_SHIELD
                                                : IC_BODY;
    case PC_UI_IC_GEM:
        return II_GEM;
    default:
        return is_key(e.id) ? II_KEY : is_sigil(e.id) ? II_SIGIL : is_grimoire(e) ? II_BOOK : II_FLASK;
    }
}

const char* misc_kind(const pc_ui_item_row& e)
{
    return is_key(e.id) ? "Key" : is_sigil(e.id) ? "Sigil" : is_grimoire(e) ? "Grimoire" : "";
}

// ---- Small pieces ----
float para_h(const Frame& f, const ui::Theme& t, float w, const char* s, float size = 0)
{
    ImFont* body = fonts.name ? fonts.name : ImGui::GetFont();
    float px = f.s(size > 0 ? size : t.body_size);
    return body->CalcTextSizeA(px, 1e9f, f.s(w), s).y / f.k;
}

// A gold bar and a gold paragraph (why something cannot be done); returns its height.
float note(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* s)
{
    f.dl->AddRectFilled(f.p(x, y + 1), f.p(x + 2, y + t.body_size + 3), t.gold);
    return paragraph(f, t, x + 10, y, w - 10, t.gold, s, 13.5f);
}
float note_h(const Frame& f, const ui::Theme& t, float w, const char* s) { return para_h(f, t, w - 10, s, 13.5f); }

// "Max DP", "Blade Type": the stat's ITEMHELP name (capitals) in title case,
// abbreviations kept; the category order (stat 0) and the count (56) named
// for what they do.
std::string stat_label(const pc_ui_items& g, int st)
{
    if (st == 0)
        return "Default order";
    if (st == 56)
        return "Quantity";
    const char* s = st >= 0 && st < PC_UI_STAT_NAMES ? g.stat_name[st] : "";
    std::string out, word;
    auto flush = [&]() {
        if (word.empty())
            return;
        bool keep = word == "DP" || word == "PP" || word == "STR" || word == "INT" || word == "AGL" || word == "RISK"
            || word == "MP" || word == "HP";
        if (!keep)
            for (size_t i = 1; i < word.size(); ++i)
                if (word[i] >= 'A' && word[i] <= 'Z')
                    word[i] = (char)(word[i] - 'A' + 'a');
        out += word;
        word.clear();
    };
    for (const char* c = s;; ++c) {
        if (!*c || *c == ' ' || *c == '/' || *c == '-') {
            flush();
            if (!*c)
                break;
            out += *c;
        } else {
            word += *c;
        }
    }
    return out;
}

// The panel behind a popover (soft shadow, panel, rule, the crimson top edge).
void pop_panel(const Frame& f, const ui::Theme& t, float x0, float y0, float x1, float y1)
{
    f.dl->AddRectFilled(f.p(x0 + 3, y0 + 5), f.p(x1 + 6, y1 + 8), 0x50000000u);
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y1), t.panel);
    f.dl->AddRect(f.p(x0, y0), f.p(x1, y1), t.rule2, 0, 0, f.s(1));
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y0 + 2), t.accent);
}

// A popover's top: below the focused row when it fits, else above it.
float pop_top(float row_y, float h, float y0)
{
    float y = row_y + ROW_H + 4;
    if (y + h > BOTTOM) {
        y = row_y - 4 - h;
        if (y < y0)
            y = BOTTOM - h < y0 ? y0 : BOTTOM - h;
    }
    return y;
}

// A small triangle button (up / down / left / right) centred at (cx, cy) that a click presses `bits` with.
void arrow(const Frame& f, const ui::Theme& t, int dir, float cx, float cy, unsigned bits, bool enabled, Pointer& m)
{
    float r = 5.5f;
    bool over = m.hit(PC_UI_MOUSE_BUTTON, (int)bits, cx - 12, cy - 11, cx + 12, cy + 11);
    unsigned col = !enabled ? t.dim : over ? t.bone : t.ash;
    ImVec2 a, b, c;
    switch (dir) {
    case 0: a = f.p(cx, cy - r), b = f.p(cx + r, cy + r * 0.6f), c = f.p(cx - r, cy + r * 0.6f); break;
    case 1: a = f.p(cx - r, cy - r * 0.6f), b = f.p(cx + r, cy - r * 0.6f), c = f.p(cx, cy + r); break;
    case 2: a = f.p(cx - r, cy), b = f.p(cx + r * 0.6f, cy - r), c = f.p(cx + r * 0.6f, cy + r); break;
    default: a = f.p(cx + r, cy), b = f.p(cx - r * 0.6f, cy + r), c = f.p(cx - r * 0.6f, cy - r); break;
    }
    f.dl->AddTriangleFilled(a, b, c, col);
}

// ---- Category chips ----
float chips(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_items& g,
    const pc_ui_items_view& v, Pointer& m)
{
    float x = x0, size = 12;
    for (int c = 0; c < PC_UI_ITEM_CATEGORIES; ++c) {
        std::string name = upper(CAT_NAMES[c]);
        std::string n = g.valid ? num(g.count[c]) : "";
        float w = measure(f, fonts.label, size, name.c_str(), 0.26f);
        float nw = n.empty() ? 0 : 6 + measure(f, fonts.data, 11, n.c_str());
        bool on = c == v.category;
        bool over = m.hit(PC_UI_MOUSE_SUB, c, x - 8, y - 8, x + w + nw + 8, y + 23);
        text(f, fonts.label, size, x, y, on ? t.bone : over ? t.ash : t.dim, name.c_str(), 0.26f);
        if (!n.empty())
            text(f, fonts.data, 11, x + w + 6, y + 1, on ? t.ash : g.count[c] ? t.dim : fade(t.dim, 0.6f), n.c_str());
        if (on)
            f.dl->AddRectFilled(f.p(x, y + 21), f.p(x + w + nw, y + 23), t.accent);
        x += w + nw + 24;
    }
    f.dl->AddLine(f.p(x0, y + 22), f.p(x1, y + 22), t.rule, f.s(1));
    float cy = y + 6, gx = x1;
    float rw = glyph(f, PC_PROMPT_R2, 18, 0, 0, true), lw = glyph(f, PC_PROMPT_L2, 18, 0, 0, true);
    gx -= rw;
    m.hit(PC_UI_MOUSE_BUTTON, PC_UI_SUB_NEXT, gx - 3, y - 8, gx + rw + 3, y + 20);
    glyph(f, PC_PROMPT_R2, 18, gx, cy);
    gx -= 6 + lw;
    m.hit(PC_UI_MOUSE_BUTTON, PC_UI_SUB_PREV, gx - 3, y - 8, gx + lw + 3, y + 20);
    glyph(f, PC_PROMPT_L2, 18, gx, cy);
    return y + 23;
}

// "BAG ▬▬▬ 3 / 8" right-aligned at x1 (gold when full).
void bag_meter(const Frame& f, const ui::Theme& t, float x1, float y, int n, int cap, bool narrow)
{
    bool full = cap > 0 && n >= cap;
    float tw = f.W < 1100 ? 72.0f : 110.0f;
    float rx = x1 - text_right(f, fonts.data, 13, x1, y - 1, t.ash, num(cap).c_str());
    rx -= text_right(f, fonts.data, 13, rx, y - 1, t.dim, " / ");
    rx -= text_right(f, fonts.data, 13, rx, y - 1, full ? t.gold : t.bone, num(n).c_str()) + 10;
    float bx0 = rx - tw, by = y + 5;
    f.dl->AddRectFilled(f.p(bx0, by), f.p(rx, by + 3), t.rule);
    float a = cap > 0 ? (float)(n < 0 ? 0 : n > cap ? cap : n) / cap : 0;
    if (a > 0)
        f.dl->AddRectFilled(f.p(bx0, by), f.p(bx0 + tw * a, by + 3), full ? t.gold : t.ash);
    if (!narrow)
        text_right(f, fonts.label, 10, bx0 - 8, y + 1, t.dim, "IN THE BAG", 0.22f);
}

// The name of the weapon / shield a part is set in ("" none).
const char* parent_name(const pc_ui_items& g, const pc_ui_item_row& e)
{
    int pc = e.parent_category;
    if (pc < 0 || pc >= PC_UI_ITEM_CATEGORIES || e.parent < 0)
        return "";
    for (int k = 0; k < g.count[pc] && g.first[pc] + k < PC_UI_ITEM_ROWS; ++k) {
        const pc_ui_item_row& r = g.rows[g.first[pc] + k];
        if (r.inventory == e.parent)
            return r.name;
    }
    return "";
}

// The two rows of a move can merge: the same misc item (a stack each).
bool merges(int c, const pc_ui_item_row& a, const pc_ui_item_row& b)
{
    return c == PC_UI_IC_MISC && a.id == b.id;
}

// ---- The list ----
void list(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_items& g,
    const pc_ui_items_view& v, float dt, bool reset, bool compact, Pointer& m)
{
    const int c = v.category, count = v.rows;
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
    bool moving = v.mode == PC_UI_IT_MOVE && v.move_from >= 0 && v.move_from < count;
    const pc_ui_item_row* from = moving ? &g.rows[g.first[c] + v.move_from] : nullptr;
    f.dl->PushClipRect(f.p(x0, y0), f.p(x1 + 1, y1), true);
    focus_band(f, t, x0, anim.list_y, x1, anim.list_y + ROW_H);
    for (int i = 0; i < count; ++i) {
        float ry = y0 + (i - anim.scroll) * ROW_H;
        if (ry + ROW_H < y0 || ry > y1)
            continue;
        const pc_ui_item_row& e = g.rows[g.first[c] + i];
        bool on = i == cursor;
        float hy0 = ry < y0 ? y0 : ry, hy1 = ry + ROW_H > y1 ? y1 : ry + ROW_H;
        if (hy1 > hy0)
            m.hit(PC_UI_MOUSE_ITEM, i, x0, hy0, x1, hy1);
        float cy = ry + ROW_H * 0.5f, right = x1 - 12;
        bool src = moving && i == v.move_from;
        if (src) { // the row being moved: outlined, its place kept
            f.dl->AddRectFilled(f.p(x0 + 1, ry + 2), f.p(x1 - 1, ry + ROW_H - 2), fade(t.accent_soft, 0.7f));
            f.dl->AddRect(f.p(x0 + 1, ry + 2), f.p(x1 - 1, ry + ROW_H - 2), fade(t.accent, 0.8f), 0, 0, f.s(1));
        }
        // the ring and its icon
        float rx = x0 + 14 + RING_R;
        if (on)
            f.dl->AddCircle(f.p(rx, cy), f.s(RING_R + 2), t.accent_soft, 36, f.s(3));
        f.dl->AddCircle(f.p(rx, cy), f.s(RING_R), on ? t.accent : t.rule2, 36, f.s(1));
        item_icon(f, row_icon(c, e), rx, cy, 16, on ? t.bone : t.ash);
        // the right side: a tag (move), the count, the mark
        const char* tag = nullptr;
        unsigned tag_col = t.accent;
        if (src)
            tag = "MOVING", tag_col = fade(t.bone, 0.8f);
        else if (moving && on)
            tag = merges(c, *from, e) ? "MERGE" : "SWAP";
        if (tag) {
            float tw = measure(f, fonts.label, 10, tag, 0.22f);
            float bx = right - tw - 12;
            f.dl->AddRect(f.p(bx, cy - 9), f.p(right, cy + 9), tag_col, 0, 0, f.s(1));
            text(f, fonts.label, 10, bx + 6, cy - 6, tag_col, tag, 0.22f);
            right = bx - 10;
        }
        if (c == PC_UI_IC_MISC && e.count > 0) {
            std::string n = std::string(TIMES) + num(e.count);
            right -= text_right(f, fonts.data, 15, right, cy - 9, on ? t.bone : t.ash, n.c_str()) + 12;
        }
        if (!compact && e.mark != PC_UI_MARK_NONE && !tag) {
            const char* mk = e.mark == PC_UI_MARK_EQUIPPED ? "EQUIPPED" : "IN A SET";
            unsigned mc = e.mark == PC_UI_MARK_EQUIPPED ? t.gold : t.ash;
            float mw = text_right(f, fonts.label, 10, right, cy - 6, mc, mk, 0.22f);
            right -= mw + 12;
        } else if (e.mark != PC_UI_MARK_NONE && !tag) { // narrow list: a gold dot (equipped), a ring (in a set)
            if (e.mark == PC_UI_MARK_EQUIPPED)
                f.dl->AddCircleFilled(f.p(right - 3, cy), f.s(3), t.gold);
            else
                f.dl->AddCircle(f.p(right - 3, cy), f.s(3), t.ash, 12, f.s(1));
            right -= 16;
        }
        // name and sub line (a grip's kind is ITEMHELP's sentence: no full stop in a sub line)
        std::string sub = e.sub;
        if (!sub.empty() && sub.back() == '.')
            sub.pop_back();
        bool reason = false;
        if (c == PC_UI_IC_MISC) {
            sub = misc_kind(e);
            if (!e.usable && e.use_reason[0] && !v.locked && !is_key(e.id) && !is_sigil(e.id)) {
                sub = e.use_reason;
                reason = true;
            }
        }
        const char* pn = parent_name(g, e);
        if (*pn && !compact)
            sub += (sub.empty() ? std::string("") : std::string(DOT)) + "in " + pn;
        float nx = rx + RING_R + 14, nw = right - nx;
        bool two = !sub.empty();
        unsigned nc = on ? t.bone : t.ash;
        text(f, fonts.name, 18, nx, two ? ry + 6 : cy - 11, nc, fit(f, fonts.name, 18, e.name, nw).c_str());
        if (two)
            text(f, fonts.data, 11, nx, ry + 29,
                reason ? fade(t.gold, on ? 0.9f : 0.55f) : on ? t.ash : t.dim,
                fit(f, fonts.data, 11, sub.c_str(), nw).c_str());
        f.dl->AddLine(f.p(x0, ry + ROW_H), f.p(x1, ry + ROW_H), t.rule, f.s(1));
    }
    f.dl->PopClipRect();
    if (count > vis) { // scroll position
        float track = y1 - y0, thumb = track * vis / count, ty = y0 + (track - thumb) * (anim.scroll / max_scroll);
        f.dl->AddRectFilled(f.p(x1 + 6, y0), f.p(x1 + 8, y1), t.rule);
        f.dl->AddRectFilled(f.p(x1 + 6, ty), f.p(x1 + 8, ty + thumb), t.ash);
    }
}

// ---- The card ----
// "DP ▬▬▬ 101 / 126" in a column w wide; returns its height.
float meter(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* name, int now, int max,
    unsigned col)
{
    text(f, fonts.label, 10, x, y + 1, t.dim, name, 0.22f);
    float rx = x + w;
    rx -= text_right(f, fonts.data, 12, rx, y, t.dim, num(max).c_str());
    rx -= text_right(f, fonts.data, 12, rx, y, t.dim, " / ");
    text_right(f, fonts.data, 12, rx, y, t.bone, num(now).c_str());
    float by = y + 18;
    f.dl->AddRectFilled(f.p(x, by), f.p(x + w, by + 3), t.rule);
    float a = max > 0 ? (float)(now < 0 ? 0 : now > max ? max : now) / max : 0;
    if (a > 0)
        f.dl->AddRectFilled(f.p(x, by), f.p(x + w * a, by + 3), col);
    return 30;
}

// Three label / value cells across w; returns their height.
float cells(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* const* names,
    const std::string* values, const unsigned* cols)
{
    float cw = w / 3;
    for (int k = 0; k < 3; ++k) {
        text(f, fonts.label, 10, x + k * cw, y, t.dim, upper(names[k]).c_str(), 0.22f);
        text(f, fonts.data, 16, x + k * cw, y + 14, cols ? cols[k] : t.bone,
            fit(f, fonts.data, 16, values[k].c_str(), cw - 8).c_str());
    }
    return 40;
}

bool has_stats(int c) { return c != PC_UI_IC_MISC; }
bool accessory(int c, const pc_ui_item_details* d, const pc_ui_item_row& e)
{
    return c == PC_UI_IC_ARMOR && (d ? d->armor_category == 7 : row_icon(c, e) == IC_RING);
}

// The header label: the kind ("Weapon · Sword").
std::string kind_head(int c, const pc_ui_item_details* d, const pc_ui_item_row& e)
{
    std::string h = CAT_ONE[c];
    const char* k = "";
    if (d && (c == PC_UI_IC_WEAPON || c == PC_UI_IC_BLADE) && d->blade_category >= 1 && d->blade_category <= 9)
        k = BLADE_KINDS[d->blade_category];
    else if (d && c == PC_UI_IC_GRIP && d->grip_category >= 1 && d->grip_category <= 4)
        k = GRIP_KINDS[d->grip_category];
    else if (c == PC_UI_IC_ARMOR) {
        if (accessory(c, d, e))
            return "Accessory";
        if (d && d->armor_category >= 1 && d->armor_category <= 7)
            k = ARMOR_KINDS[d->armor_category];
    } else if (c == PC_UI_IC_MISC)
        k = misc_kind(e);
    if (*k)
        h += DOT, h += k;
    return h;
}

// The parts block (weapons: blade, grip; weapons, grips, shields: the gem slots); returns its height.
float parts(const Frame& f, const ui::Theme& t, float x, float y, float w, int c, const pc_ui_item_details& d)
{
    float y0 = y;
    if (c == PC_UI_IC_WEAPON) {
        label(f, t, x, y, "Parts");
        y += 13 + 8;
        const char* names[2] = {"Blade", "Grip"};
        const char* parts_[2] = {d.blade, d.grip};
        for (int k = 0; k < 2; ++k) {
            text(f, fonts.data, 12, x, y + 2, t.dim, names[k]);
            text(f, fonts.name, 15, x + 56, y, parts_[k][0] ? t.bone : t.dim,
                fit(f, fonts.name, 15, parts_[k][0] ? parts_[k] : "None", w - 56).c_str());
            y += 22;
        }
        y += 8;
    }
    if (c == PC_UI_IC_WEAPON || c == PC_UI_IC_GRIP || c == PC_UI_IC_SHIELD) {
        std::string head = "Gems";
        if (d.gem_slots > 0)
            head += DOT + num(d.gem_count) + " of " + num(d.gem_slots);
        label(f, t, x, y, head.c_str());
        y += 13 + 8;
        if (d.gem_slots <= 0) {
            text(f, fonts.data, 12, x, y + 2, t.dim, c == PC_UI_IC_WEAPON ? "Its grip has no gem slots" : "No gem slots");
            y += 22;
        }
        int shown = 0;
        for (int k = 0; k < d.gem_slots && k < 3; ++k) {
            bool set = k < d.gem_count && d.gems[k][0];
            float cx = x + 6, cy = y + 10;
            if (set) {
                item_icon(f, II_GEM, cx, cy, 14, t.mp);
                text(f, fonts.name, 15, x + 20, y, t.bone, fit(f, fonts.name, 15, d.gems[k], w - 20).c_str());
            } else {
                f.dl->AddCircle(f.p(cx, cy), f.s(4), t.rule2, 16, f.s(1));
                text(f, fonts.data, 12, x + 20, y + 2, t.dim, "Empty slot");
            }
            y += 22;
            ++shown;
        }
        (void)shown;
    }
    return y - y0;
}

// Class, affinity and type bars (DETAILS' right column); returns the height.
float class_bars(const Frame& f, const ui::Theme& t, float x, float xr, float y, int c, const pc_ui_item_details& d)
{
    float y0 = y;
    label(f, t, x, y, "Class");
    y += 13 + 8;
    y += bars(f, t, x, xr, y, CLASSES, d.stats.classes, d.stats.classes, PC_UI_CLASSES, true, t.gold);
    y += 12;
    label(f, t, x, y, "Affinity");
    y += 13 + 8;
    y += bars(f, t, x, xr, y, AFFINITIES, d.stats.affinities, d.stats.affinities, PC_UI_AFFINITIES, true, t.mp);
    if (c != PC_UI_IC_BLADE && c != PC_UI_IC_GEM) { // the original's TYPE page (blades and gems have none): blunt / edged / piercing (a weapon: its grip's)
        y += 12;
        std::string head = "Type";
        if (c == PC_UI_IC_WEAPON)
            head += DOT + std::string("from the grip");
        label(f, t, x, y, head.c_str());
        y += 13 + 8;
        y += bars(f, t, x, xr, y, TYPES, d.types, d.types, 3, true, t.ash);
    }
    return y - y0;
}

void item_card(const Frame& f, const ui::Theme& t, const Card& card, const pc_ui_items& g,
    const pc_ui_items_view& v, const pc_ui_item_row& e, bool wide)
{
    const int c = v.category;
    const pc_ui_item_details* d = g.details.valid && g.details.category == c && g.details.inventory == e.inventory
        ? &g.details
        : nullptr;
    float x = card.x + 20, xr = card.x + card.w - 20, w = xr - x, y = card.y + 18;
    float lw = wide ? (w - 28) * 0.5f : w; // the left column (wide: two columns)
    card_begin(f);
    // the kind and the mark
    std::string head = kind_head(c, d, e);
    label(f, t, x, y, head.c_str());
    if (e.mark != PC_UI_MARK_NONE) {
        const char* mk = e.mark == PC_UI_MARK_EQUIPPED ? "EQUIPPED" : "IN A SET";
        text_right(f, fonts.label, t.label_size, xr, y, e.mark == PC_UI_MARK_EQUIPPED ? t.gold : t.ash, mk, 0.26f);
    }
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, t.bone, fit(f, fonts.name, 25, e.name, w).c_str());
    y += 31 + 6;
    // the meta line: damage type, material, hands (weapons, blades); else the material / kind; a part's set
    std::string meta;
    if (d && (c == PC_UI_IC_WEAPON || c == PC_UI_IC_BLADE))
        meta = kind_line(d->stats);
    else if (c == PC_UI_IC_MISC)
        meta = e.count > 0 ? std::string(TIMES) + num(e.count) + " in the bag" : "";
    else if (c == PC_UI_IC_ARMOR && d && d->stats.material[0])
        meta = d->stats.material;
    else if (c != PC_UI_IC_GRIP && !accessory(c, d, e))
        meta = e.sub;
    const char* pn = parent_name(g, e);
    if (*pn)
        meta += (meta.empty() ? std::string("") : std::string(DOT)) + "Set in " + pn;
    if (d && d->stats.quick >= 1 && d->stats.quick <= PC_UI_QUICK)
        meta += (meta.empty() ? std::string("") : std::string(DOT)) + "Quick swap " + num(d->stats.quick);
    if (!meta.empty()) {
        text(f, fonts.label, t.label_size, x, y, t.ash,
            fit(f, fonts.label, t.label_size, upper(meta.c_str()).c_str(), w, 0.2f).c_str(), 0.2f);
        y += 13 + 14;
    } else {
        y += 4;
    }
    float top_cols = y;
    if (has_stats(c) && d) {
        const pc_ui_item& s = d->stats;
        if (s.dp_max > 0 || s.pp_max > 0) {
            float hw = (lw - 20) * 0.5f;
            meter(f, t, x, y, hw, "DP", s.dp, s.dp_max, t.hp);
            if (s.pp_max > 0)
                meter(f, t, x + hw + 20, y, hw, "PP", s.pp, s.pp_max, t.gold);
            y += 30 + 10;
        }
        static const char* const SN[3] = {"STR", "INT", "AGL"};
        std::string sv[3] = {signed_num(s.str), signed_num(s.intel), signed_num(s.agl)};
        unsigned scol[3] = {s.str < 0 ? t.down : t.bone, s.intel < 0 ? t.down : t.bone, s.agl < 0 ? t.down : t.bone};
        y += cells(f, t, x, y, lw, SN, sv, scol) + 6;
        if (c == PC_UI_IC_WEAPON || c == PC_UI_IC_BLADE) {
            static const char* const RN[3] = {"Range", "RISK", "Damage"};
            std::string rv[3] = {num(d->range), num(d->risk),
                s.damage_type >= 0 && s.damage_type < 3 ? DAMAGE[s.damage_type] : "\xe2\x80\x94"};
            y += cells(f, t, x, y, lw, RN, rv, nullptr) + 6;
        } else if (!wide && c != PC_UI_IC_GEM) { // the TYPE page's values (wide: as bars on the right; gems have none)
            std::string tv[3] = {signed_num(d->types[0]), signed_num(d->types[1]), signed_num(d->types[2])};
            unsigned tc[3] = {d->types[0] ? t.bone : t.dim, d->types[1] ? t.bone : t.dim, d->types[2] ? t.bone : t.dim};
            y += cells(f, t, x, y, lw, TYPES, tv, tc) + 6;
        }
        if (c == PC_UI_IC_WEAPON || c == PC_UI_IC_GRIP || c == PC_UI_IC_SHIELD) {
            y += 6;
            y += parts(f, t, x, y, lw, c, *d);
        }
    } else if (has_stats(c)) {
        spinner(f, x + 6, y + 6, 5, t.ash);
        y += 24;
    }
    // the game's description (blades, grips, gems, accessories, misc)
    if (e.help[0]) {
        y += 6;
        y += paragraph(f, t, x, y, lw, t.ash, e.help);
    }
    // why it cannot be used / discarded (misc; locked: the list says it once)
    if (c == PC_UI_IC_MISC && !v.locked) {
        if (!e.usable && e.use_reason[0]) {
            y += 12;
            y += note(f, t, x, y, lw, e.use_reason);
        }
        if (!e.discard && e.discard_reason[0] && std::strcmp(e.discard_reason, e.use_reason)) {
            y += 8;
            y += note(f, t, x, y, lw, e.discard_reason);
        }
    }
    float bottom = y;
    if (wide && d) {
        float rx = x + lw + 28;
        float ry = top_cols + class_bars(f, t, rx, xr, top_cols, c, *d);
        f.dl->AddLine(f.p(rx - 14, top_cols), f.p(rx - 14, (ry > y ? ry : y) - 4), t.rule, f.s(1));
        bottom = ry > bottom ? ry : bottom;
    } else if (!wide && d && has_stats(c)) {
        y += 14;
        float gx = x;
        const char* more = c == PC_UI_IC_GEM || c == PC_UI_IC_BLADE ? "Class and affinity:" : "Class, affinity and type:";
        text(f, fonts.data, 11, gx, y, t.dim, more);
        gx += measure(f, fonts.data, 11, more) + 6;
        gx += glyph(f, PC_PROMPT_SQUARE, 15, gx, y + 7) + 4;
        bottom = y + 16;
    }
    card_end(f, t, card, bottom + 18);
}

void misc_empty_card(const Frame& f, const ui::Theme& t, const Card& card, int c, bool valid)
{
    float x = card.x + 20, w = card.w - 40, y = card.y + 18;
    card_begin(f);
    label(f, t, x, y, CAT_NAMES[c]);
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, t.dim, valid ? "Nothing here" : "Loading");
    y += 31 + 8;
    if (valid)
        y += paragraph(f, t, x, y, w, t.ash,
            c == PC_UI_IC_GEM ? "Gems found go here; set them in a weapon's grip or a shield at a workshop."
                              : "Nothing of this kind in the bag.");
    card_end(f, t, card, y + 18);
}

// ---- Popovers ----
// The action menu next to the focused row.
void actions_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, float row_y,
    const pc_ui_items_view& v, Pointer& m)
{
    float w = 236, px1 = x1 - 12, px0 = px1 - w;
    if (px0 < x0 + 40)
        px0 = x0 + 40, px1 = px0 + w;
    const char* why = v.action >= 0 && v.action < v.actions && !v.action_enabled[v.action]
        ? v.action_reason[v.action]
        : nullptr;
    float reason_h = why ? note_h(f, t, w - 28, why) + 14 : 0;
    float h = 10 + v.actions * MENU_ROW + 8 + reason_h;
    float py = pop_top(row_y, h, y0);
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_NONE, 0, px0, py, px1, py + h);
    float y = py + 10;
    for (int i = 0; i < v.actions; ++i) {
        bool on = i == v.action, en = v.action_enabled[i] != 0;
        bool over = m.hit(PC_UI_MOUSE_ITEM_MENU, i, px0, y, px1, y + MENU_ROW);
        if (on)
            focus_band(f, t, px0, y, px1, y + MENU_ROW);
        bool orig = v.action_kind[i] == PC_UI_ITA_ORIGINAL;
        if (orig) // the original screen apart from the item's actions
            f.dl->AddLine(f.p(px0 + 14, y), f.p(px1 - 14, y), t.rule, f.s(1));
        unsigned col = !en ? t.dim : on ? t.bone : over ? t.bone : t.ash;
        text(f, fonts.name, 16.5f, px0 + 16, y + 5, col, v.action_label[i]);
        y += MENU_ROW;
    }
    if (why) {
        y += 8;
        f.dl->AddLine(f.p(px0 + 14, y - 2), f.p(px1 - 14, y - 2), t.rule, f.s(1));
        y += 6;
        note(f, t, px0 + 14, y, w - 28, why);
    }
}

// The quantity picker: the count, the stack, the step (Left: bigger, past 100 all; Right: smaller, past 1 none).
void quantity_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, float row_y,
    const pc_ui_item_row& e, const pc_ui_items_view& v, Pointer& m)
{
    float w = 300, px1 = x1 - 12, px0 = px1 - w;
    if (px0 < x0 + 20)
        px0 = x0 + 20, px1 = px0 + w;
    float h = 196;
    float py = pop_top(row_y, h, y0);
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_NONE, 0, px0, py, px1, py + h);
    float x = px0 + 18, xr = px1 - 18, y = py + 14;
    std::string head = "Discard" + std::string(DOT) + e.name;
    text(f, fonts.label, t.label_size, x, y, t.ash, fit(f, fonts.label, t.label_size, upper(head.c_str()).c_str(), xr - x, 0.26f).c_str(), 0.26f);
    y += 13 + 10;
    // the count, big, and the stack
    float cw = text(f, fonts.data, 40, x, y, v.count ? t.bone : t.dim, num(v.count).c_str());
    text(f, fonts.data, 16, x + cw + 8, y + 22, t.dim, ("of " + num(v.count_max)).c_str());
    // up / down
    float ax = xr - 10;
    arrow(f, t, 0, ax, y + 12, PC_UI_UP, v.count < v.count_max, m);
    arrow(f, t, 1, ax, y + 36, PC_UI_DOWN, v.count > 0, m);
    y += 54;
    // the share discarded
    f.dl->AddRectFilled(f.p(x, y), f.p(xr, y + 3), t.rule);
    float a = v.count_max > 0 ? (float)v.count / v.count_max : 0;
    if (a > 0)
        f.dl->AddRectFilled(f.p(x, y), f.p(x + (xr - x) * a, y + 3), t.accent);
    text_right(f, fonts.data, 11, xr, y + 8, t.dim, (num(v.count_max - v.count) + " stay").c_str());
    y += 26;
    // the step: 100 10 1 (Left goes to the bigger one)
    text(f, fonts.label, 10, x, y + 4, t.dim, "STEP", 0.22f);
    float sx = x + 46;
    arrow(f, t, 2, sx, y + 9, PC_UI_LEFT, true, m);
    sx += 14;
    static const int STEPS[3] = {100, 10, 1};
    for (int k = 0; k < 3; ++k) {
        int step = 2 - k; // v.step: 0 = 1, 1 = 10, 2 = 100
        bool on = v.step == step;
        std::string s = num(STEPS[k]);
        float bw = 40;
        if (on)
            f.dl->AddRectFilled(f.p(sx, y - 1), f.p(sx + bw, y + 19), t.accent_soft);
        f.dl->AddRect(f.p(sx, y - 1), f.p(sx + bw, y + 19), on ? t.accent : t.rule2, 0, 0, f.s(1));
        float tw = measure(f, fonts.data, 12, s.c_str());
        text(f, fonts.data, 12, sx + (bw - tw) * 0.5f, y + 2, on ? t.bone : t.dim, s.c_str());
        sx += bw + 4;
    }
    sx += 10;
    arrow(f, t, 3, sx, y + 9, PC_UI_RIGHT, true, m);
    y += 30;
    const char* tip = v.count == 0 ? "None chosen: Circle goes back."
                                   : "Left past 100 takes all; Right past 1, none.";
    text(f, fonts.data, 11, x, y, t.dim, fit(f, fonts.data, 11, tip, xr - x).c_str());
}

// The Yes / No question (the game's text).
void confirm_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, float row_y,
    const pc_ui_item_row* e, const pc_ui_items_view& v, Pointer& m)
{
    float w = 300, px1 = x1 - 12, px0 = px1 - w;
    if (px0 < x0 + 20)
        px0 = x0 + 20, px1 = px0 + w;
    float x = px0 + 18, xr = px1 - 18;
    const char* prompt = v.prompt && v.prompt[0] ? v.prompt : "Discard it?";
    float ph = para_h(f, t, xr - x, prompt);
    float h = 14 + 13 + 10 + 24 + 8 + ph + 12 + 2 * MENU_ROW + 10;
    float py = pop_top(row_y, h, y0);
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_NONE, 0, px0, py, px1, py + h);
    float y = py + 14;
    label(f, t, x, y, v.confirm_kind == PC_UI_ITA_DISCARD_ALL ? "Discard all" : "Discard");
    y += 13 + 10;
    std::string name = e ? e->name : "";
    if (e && (v.confirm_kind == PC_UI_ITA_DISCARD_ALL || v.count_max >= 2))
        name += "  " + std::string(TIMES) + num(v.confirm_kind == PC_UI_ITA_DISCARD_ALL ? v.count_max : v.count);
    text(f, fonts.name, 19, x, y, t.bone, fit(f, fonts.name, 19, name.c_str(), xr - x).c_str());
    y += 24 + 8;
    y += paragraph(f, t, x, y, xr - x, t.ash, prompt) + 12;
    const char* labels[2] = {"Yes", "No"};
    for (int i = 0; i < 2; ++i) {
        bool on = v.confirm == i;
        bool over = m.hit(PC_UI_MOUSE_ITEM_MENU, i, px0, y, px1, y + MENU_ROW);
        if (on)
            focus_band(f, t, px0, y, px1, y + MENU_ROW);
        text(f, fonts.name, 16.5f, px0 + 18, y + 5, on || over ? t.bone : t.ash, labels[i]);
        y += MENU_ROW;
    }
}

// The sort stats on the card, the focused one's description under them.
void sort_card(const Frame& f, const ui::Theme& t, const Card& card, const pc_ui_items& g, const pc_ui_items_view& v,
    float dt, bool reset, Pointer& m)
{
    int c = v.category;
    int n = g.sort_count[c] < 0 ? 0 : g.sort_count[c] > PC_UI_SORT_STATS ? PC_UI_SORT_STATS : g.sort_count[c];
    float x = card.x + 20, xr = card.x + card.w - 20, w = xr - x, y = card.y + 18;
    card_begin(f);
    label(f, t, x, y, ("Sort " + std::string(CAT_NAMES[c]) + " by").c_str());
    y += 13 + 12;
    int cur = v.sort < 0 ? 0 : v.sort >= n ? (n ? n - 1 : 0) : v.sort;
    int st = n ? g.sort_stats[c][cur] : 0;
    const char* help = st < PC_UI_STAT_NAMES ? g.stat_help[st] : "";
    float help_h = help[0] ? para_h(f, t, w, help) : 0;
    float list_y0 = y, avail = BOTTOM - 18 - (help_h + 28) - list_y0;
    int vis = (int)(avail / MENU_ROW);
    vis = vis < 3 ? 3 : vis > n ? n : vis;
    float list_y1 = list_y0 + vis * MENU_ROW;
    float target = (float)anim.sort_scroll;
    if (reset)
        target = 0;
    if (cur < target)
        target = (float)cur;
    if (cur > target + vis - 1)
        target = (float)(cur - vis + 1);
    float max_scroll = (float)(n > vis ? n - vis : 0);
    target = target < 0 ? 0 : target > max_scroll ? max_scroll : target;
    anim.sort_scroll = target; // whole rows: the list steps with the focus
    f.dl->PushClipRect(f.p(card.x, list_y0), f.p(card.x + card.w, list_y1), true);
    for (int i = 0; i < n; ++i) {
        float ry = list_y0 + (i - target) * MENU_ROW;
        if (ry + MENU_ROW <= list_y0 || ry >= list_y1)
            continue;
        bool on = i == cur;
        bool over = m.hit(PC_UI_MOUSE_ITEM_MENU, i, card.x, ry, card.x + card.w - 12, ry + MENU_ROW);
        if (on)
            focus_band(f, t, card.x + 8, ry, card.x + card.w - 12, ry + MENU_ROW);
        std::string s = stat_label(g, g.sort_stats[c][i]);
        text(f, fonts.name, 16.5f, x + 4, ry + 5, on || over ? t.bone : t.ash, fit(f, fonts.name, 16.5f, s.c_str(), w - 8).c_str());
    }
    f.dl->PopClipRect();
    if (n > vis) {
        float track = list_y1 - list_y0, thumb = track * vis / n, ty = list_y0 + (track - thumb) * (target / max_scroll);
        f.dl->AddRectFilled(f.p(xr + 8, list_y0), f.p(xr + 10, list_y1), t.rule);
        f.dl->AddRectFilled(f.p(xr + 8, ty), f.p(xr + 10, ty + thumb), t.ash);
    }
    y = list_y1 + 12;
    f.dl->AddLine(f.p(x, y), f.p(xr, y), t.rule, f.s(1));
    y += 14;
    if (help[0])
        y += paragraph(f, t, x, y, w, t.ash, help);
    (void)dt;
    card_end(f, t, card, y + 18);
}

// What the move will do (MOVE's card).
void move_card(const Frame& f, const ui::Theme& t, const Card& card, const pc_ui_items& g, const pc_ui_items_view& v)
{
    int c = v.category;
    const pc_ui_item_row& a = g.rows[g.first[c] + v.move_from];
    const pc_ui_item_row* b = v.cursor >= 0 && v.cursor < v.rows ? &g.rows[g.first[c] + v.cursor] : nullptr;
    float x = card.x + 20, xr = card.x + card.w - 20, w = xr - x, y = card.y + 18;
    card_begin(f);
    bool same = !b || v.cursor == v.move_from;
    bool merge = b && !same && merges(c, a, *b);
    std::string head = same ? std::string("Move") : "Move" + std::string(DOT) + (merge ? "merge" : "swap");
    label(f, t, x, y, head.c_str());
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, t.bone, fit(f, fonts.name, 25, a.name, w).c_str());
    y += 31 + 8;
    std::string line;
    if (same)
        line = "Choose where it goes with Up and Down. Here, it stays where it is.";
    else if (merge) {
        int sum = a.count + b->count;
        line = sum <= 100 ? "The two stacks become one of " + std::string(TIMES) + num(sum) + "."
                          : "The stacks merge up to " + std::string(TIMES) + "100; " + std::string(TIMES)
                + num(sum - 100) + " stay in the other row.";
    } else
        line = std::string("Trades places with ") + b->name + ".";
    y += paragraph(f, t, x, y, w, t.ash, line.c_str());
    if (!same) {
        y += 16;
        float rx = x;
        std::string l = num(v.move_from + 1);
        if (merge) {
            rx += text(f, fonts.data, 13, rx, y, t.dim, ("Row " + l).c_str());
            rx += text(f, fonts.data, 13, rx + 6, y, t.accent, "\xe2\x86\x92") + 12; // "→"
            text(f, fonts.data, 13, rx, y, t.dim, ("row " + num(v.cursor + 1)).c_str());
        } else {
            text(f, fonts.data, 13, rx, y, t.dim, ("Rows " + l + " and " + num(v.cursor + 1)).c_str());
        }
        y += 18;
    }
    card_end(f, t, card, y + 18);
}

} // namespace

void draw_items(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    const pc_ui_items& g = *pc_ui_shell_items();
    pc_ui_items_view v;
    pc_ui_shell_items_view(&v);
    const int c = v.category;
    bool reset = fresh || anim.category != c;
    // the card widens in DETAILS for things with stats
    bool wide = v.mode == PC_UI_IT_DETAILS && v.row && has_stats(c);
    float cw_target = wide ? details_width(f.W) : card_width(f.W);
    anim.card_w = fresh || anim.card_w <= 0 ? cw_target : approach(anim.card_w, cw_target, dt, t.focus_grow);
    if (anim.card_w > cw_target - 0.5f && anim.card_w < cw_target + 0.5f)
        anim.card_w = cw_target;
    float cw = anim.card_w;
    float x0 = t.margin_x, xr = f.W - t.margin_x, x1 = xr - cw - 24;
    float y = chips(f, t, x0, xr, top, g, v, m) + 14;
    Card card = {xr - cw, y, cw};
    std::string crumb = "Items" + std::string(NEXT) + CAT_NAMES[c];
    if (!g.valid) {
        spinner(f, x0 + 22, y + 16, 6, t.ash);
        text(f, fonts.label, t.label_size, x0 + 38, y + 10, t.ash, "LOADING", 0.26f);
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        out.crumb = "Items";
        anim.category = c, anim.mode = v.mode;
        return;
    }
    // the list's header: what it is, how full the bag is
    std::string head = CAT_NAMES[c];
    if (v.mode == PC_UI_IT_MOVE && v.move_from >= 0 && v.move_from < v.rows)
        head = "Move" + std::string(DOT) + "choose a place";
    label(f, t, x0, y, head.c_str());
    bag_meter(f, t, x1, y, v.rows, v.capacity, x1 - x0 < 400);
    y += 13 + 10;
    if (v.locked && v.reason) {
        y += note(f, t, x0 + 14, y, x1 - x0 - 28, v.reason) + 12;
    }
    float list_y0 = y;
    bool compact = x1 - x0 < 400;
    if (v.rows > 0)
        list(f, t, x0, x1, list_y0, g, v, dt, reset, compact, m);
    else {
        paragraph(f, t, x0 + 14, y + 10, x1 - x0 - 28, t.dim, "Nothing here.");
        anim.list_y = list_y0 - ROW_H; // a popover sits at the list's top
    }
    // the card
    if (v.mode == PC_UI_IT_SORT)
        sort_card(f, t, card, g, v, dt, anim.mode != PC_UI_IT_SORT, m);
    else if (v.mode == PC_UI_IT_MOVE && v.move_from >= 0 && v.move_from < v.rows)
        move_card(f, t, card, g, v);
    else if (v.row)
        item_card(f, t, card, g, v, *v.row, wide && cw > card_width(f.W) + 40);
    else
        misc_empty_card(f, t, card, c, g.valid != 0);
    // the popovers next to the focused row
    float row_y = anim.list_y;
    if (v.mode == PC_UI_IT_ACTIONS)
        actions_pop(f, t, x0, x1, list_y0, row_y, v, m);
    else if (v.mode == PC_UI_IT_QUANTITY && v.row)
        quantity_pop(f, t, x0, x1, list_y0, row_y, *v.row, v, m);
    else if (v.mode == PC_UI_IT_CONFIRM)
        confirm_pop(f, t, x0, x1, list_y0, row_y, v.row, v, m);
    // the crumb and the hints
    if (v.row)
        crumb += NEXT + std::string(v.row->name);
    switch (v.mode) {
    case PC_UI_IT_LIST:
    case PC_UI_IT_DETAILS:
        if (v.mode == PC_UI_IT_DETAILS)
            crumb += NEXT + std::string("Details");
        out.hint(PC_PROMPT_CIRCLE, -1, v.rows ? "Actions" : "Options", PC_UI_CONFIRM);
        if (v.rows)
            out.hint(PC_PROMPT_SQUARE, -1, v.mode == PC_UI_IT_DETAILS ? "Less" : "Details", PC_UI_ALT);
        out.hint(PC_PROMPT_L2, PC_PROMPT_R2, "Category", PC_UI_SUB_NEXT);
        out.hint(PC_PROMPT_CROSS, -1, v.mode == PC_UI_IT_DETAILS ? "Back" : "Close", PC_UI_BACK);
        break;
    case PC_UI_IT_ACTIONS:
        out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_IT_QUANTITY:
        crumb += NEXT + std::string("Discard");
        out.hint(PC_PROMPT_UP, PC_PROMPT_DOWN, "Count", PC_UI_UP);
        out.hint(PC_PROMPT_LEFT, PC_PROMPT_RIGHT, "Step", PC_UI_LEFT);
        out.hint(PC_PROMPT_CIRCLE, -1, v.count ? "Next" : "Back", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_IT_CONFIRM:
        crumb += NEXT + std::string(v.confirm_kind == PC_UI_ITA_DISCARD_ALL ? "Discard all" : "Discard");
        out.hint(PC_PROMPT_CIRCLE, -1, v.confirm == 0 ? "Discard" : "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_IT_SORT:
        crumb = "Items" + std::string(NEXT) + CAT_NAMES[c] + NEXT + "Sort";
        out.hint(PC_PROMPT_CIRCLE, -1, "Sort", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_IT_MOVE: {
        crumb += NEXT + std::string("Move");
        const char* verb = "Leave";
        if (v.move_from >= 0 && v.move_from < v.rows && v.cursor != v.move_from && v.row)
            verb = merges(c, g.rows[g.first[c] + v.move_from], *v.row) ? "Merge" : "Swap";
        out.hint(PC_PROMPT_CIRCLE, -1, verb, PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    }
    default:
        break;
    }
    out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
    out.crumb = crumb;
    anim.category = c, anim.mode = v.mode;
}

} // namespace uidraw
