// Modern menus: the Abilities tab (pc/docs/Modern_Menus.md). Sub-tabs Chain /
// Defense / Break Arts with the L2 / R2 bumpers.
//
// Chain / Defense: the left column has the three buttons' slot cards (Circle,
// Triangle, Square: the prompt icon, the ability on it, its RISK cost) above
// the learned list (name, the button it is on, RISK cost). The right card
// explains the focused ability (the focused slot's, or the list row's).
//
// Break Arts: a rail of the ten weapon categories (the equipped one first and
// marked; arts learned of four; kills to the next art), the focused
// category's four arts (name or "???"; HP cost, kills to go, or learned after
// the next battle) and a card: the category, or the focused art with its
// description and why it cannot be used now. 16:9: the card on the right;
// 4:3: under the arts.
//
// State comes from the shell (pc_ui_shell_abilities_view, the bridge's
// snapshot); nothing here changes it except through the pointer (Pointer hits,
// applied at the shell's next tick).
#include "ui/ui_draw_internal.h"
#include "button_prompts.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace uidraw {
namespace {

const char* const SUB_NAMES[3] = {"Chain", "Defense", "Break Arts"};
const int BUTTONS[PC_UI_ABIL_BUTTONS] = {PC_PROMPT_CIRCLE, PC_PROMPT_TRIANGLE, PC_PROMPT_SQUARE};
const char* const BUTTON_NAMES[PC_UI_ABIL_BUTTONS] = {"Circle", "Triangle", "Square"};
const char* const NEXT = " \xe2\x80\xba "; // " › "

// layout units
const float BOTTOM = 720 - 64;   // the body's bottom (above the footer)
const float SLOT_H = 84;         // a button's slot card
const float ROW_H = 38;          // a learned ability
const float CAT_H = 48;          // a weapon category on the rail
const float ART_H = 44;          // an art

float left_width(float W) { return W < 1100 ? 448.0f : 560.0f; } // Chain / Defense column
float rail_width(float W) { return W < 1100 ? 260.0f : 300.0f; } // Break Arts categories

struct Anim {
    float list_y = 0, scroll = 0, scroll_target = 0, cat_y = 0, art_y = 0;
    int kind = -1, mode = -1;
} anim;

// "GREAT SWORD" -> "Great Sword" (ASCII and Latin-1 capitals)
std::string title_case(const char* str)
{
    std::string s = str;
    bool start = true;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = (unsigned char)s[i + 1];
            if (!start && d >= 0x80 && d <= 0x9E && d != 0x97) // not the multiplication sign
                s[i + 1] = (char)(d + 0x20);
            ++i;
            start = false;
            continue;
        }
        if (c >= 'A' && c <= 'Z') {
            if (!start)
                s[i] = (char)(c + 32);
            start = false;
        } else if (c >= 'a' && c <= 'z') {
            if (start)
                s[i] = (char)(c - 32);
            start = false;
        } else {
            start = c == ' ' || c == '-' || c == '/';
        }
    }
    return s;
}
std::string lower(const char* str)
{
    std::string s = str;
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
    return s;
}
std::string kills(int n, const char* tail)
{
    return num(n) + (n == 1 ? " kill" : " kills") + tail;
}
std::string risk(int cost) { return "RISK +" + num(cost); }

// A gold bar and a gold paragraph (why something cannot be done); returns its height.
float note(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* s)
{
    f.dl->AddRectFilled(f.p(x, y + 1), f.p(x + 2, y + t.body_size + 3), t.gold);
    return paragraph(f, t, x + 10, y, w - 10, t.gold, s, 13.5f);
}

// Sub-tabs (the shown ones) with the L2 / R2 bumpers; returns the y below them.
float subtabs(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_abilities_view& v,
    Pointer& m)
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

// ---- Chain / Defense ----

void slot_cards(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_ability_list& l,
    const pc_ui_abilities_view& v, int count, Pointer& m)
{
    float gap = f.W < 1100 ? 8.0f : 12.0f, w = (x1 - x0 - 2 * gap) / 3;
    for (int b = 0; b < PC_UI_ABIL_BUTTONS; ++b) {
        float x = x0 + b * (w + gap);
        int e = l.slots[b];
        bool has = e >= 0 && e < count;
        bool on = (v.mode == PC_UI_ABM_SLOTS || v.mode == PC_UI_ABM_PICK) && v.slot == b;
        m.hit(PC_UI_MOUSE_ABIL_SLOT, b, x, y, x + w, y + SLOT_H);
        if (on)
            f.dl->AddRectFilledMultiColor(f.p(x, y), f.p(x + w, y + SLOT_H), t.accent_soft,
                t.accent_soft & 0x00FFFFFFu, t.accent_soft & 0x00FFFFFFu, t.accent_soft);
        f.dl->AddRect(f.p(x, y), f.p(x + w, y + SLOT_H), on ? t.accent : t.rule2, 0, 0, f.s(1));
        float gw = glyph(f, BUTTONS[b], 22, x + 12, y + 22);
        text(f, fonts.label, 10, x + 12 + gw + 8, y + 17, on ? t.ash : t.dim, upper(BUTTON_NAMES[b]).c_str(), 0.24f);
        float tw = w - 24;
        std::string name = fit(f, fonts.name, 17, has ? l.entries[e].name : "Empty", tw);
        text(f, fonts.name, 17, x + 12, y + 38, has ? t.bone : t.dim, name.c_str());
        std::string sub = has ? risk(l.entries[e].cost) : count ? "Choose an ability" : "None learned yet";
        text(f, fonts.data, 11.5f, x + 12, y + 62, has ? t.ash : t.dim, fit(f, fonts.data, 11.5f, sub.c_str(), tw).c_str());
    }
}

void ability_list(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_ability_list& l,
    const pc_ui_abilities_view& v, int count, float dt, bool reset, Pointer& m)
{
    float y1 = BOTTOM;
    if (!count) {
        paragraph(f, t, x0 + 14, y0 + 10, x1 - x0 - 28, t.dim,
            "None learned yet. Battle abilities come with kills: the next one is on the card.");
        return;
    }
    bool listing = v.mode == PC_UI_ABM_LIST || v.mode == PC_UI_ABM_PICK;
    int vis = (int)((y1 - y0) / ROW_H);
    vis = vis < 1 ? 1 : vis;
    int cursor = v.cursor < 0 ? 0 : v.cursor >= count ? count - 1 : v.cursor;
    float st = reset ? 0 : anim.scroll_target;
    if (listing) {
        if (cursor < st)
            st = (float)cursor;
        if (cursor > st + vis - 1)
            st = (float)(cursor - vis + 1);
    }
    float max_scroll = (float)(count > vis ? count - vis : 0);
    st = st < 0 ? 0 : st > max_scroll ? max_scroll : st;
    anim.scroll_target = st;
    anim.scroll = reset ? st : approach(anim.scroll, st, dt, t.focus_grow);
    float target = y0 + (cursor - anim.scroll) * ROW_H;
    anim.list_y = reset ? target : approach(anim.list_y, target, dt, t.focus_grow);
    f.dl->PushClipRect(f.p(x0, y0), f.p(x1 + 1, y1), true);
    if (listing)
        focus_band(f, t, x0, anim.list_y, x1, anim.list_y + ROW_H);
    for (int i = 0; i < count; ++i) {
        float ry = y0 + (i - anim.scroll) * ROW_H;
        if (ry + ROW_H < y0 || ry > y1)
            continue;
        const pc_ui_ability& e = l.entries[i];
        bool on = listing && i == cursor;
        float hy0 = ry < y0 ? y0 : ry, hy1 = ry + ROW_H > y1 ? y1 : ry + ROW_H;
        if (hy1 > hy0)
            m.hit(PC_UI_MOUSE_ABIL_ROW, i, x0, hy0, x1, hy1);
        float cy = ry + ROW_H * 0.5f, right = x1 - 12;
        std::string cost = risk(e.cost);
        right -= text_right(f, fonts.data, 13, right, cy - 7, on ? t.bone : t.ash, cost.c_str()) + 12;
        if (e.button >= 0 && e.button < PC_UI_ABIL_BUTTONS) {
            float gw = glyph(f, BUTTONS[e.button], 18, 0, 0, true);
            if (gw > 0) {
                glyph(f, BUTTONS[e.button], 18, right - gw, cy);
                right -= gw + 12;
            }
        }
        std::string name = fit(f, fonts.name, 18, e.name, right - (x0 + 14));
        text(f, fonts.name, 18, x0 + 14, cy - 11, on || !listing ? t.bone : t.ash, name.c_str());
        f.dl->AddLine(f.p(x0, ry + ROW_H), f.p(x1, ry + ROW_H), t.rule, f.s(1));
    }
    f.dl->PopClipRect();
    if (count > vis) { // scroll position
        float track = y1 - y0, thumb = track * vis / count, ty = y0 + (track - thumb) * (anim.scroll / max_scroll);
        f.dl->AddRectFilled(f.p(x1 + 6, y0), f.p(x1 + 8, y1), t.rule);
        f.dl->AddRectFilled(f.p(x1 + 6, ty), f.p(x1 + 8, ty + thumb), t.ash);
    }
}

void ability_card(const Frame& f, const ui::Theme& t, float top, const pc_ui_abilities& a, const pc_ui_ability_list& l,
    const pc_ui_abilities_view& v, int count)
{
    bool chain = v.kind == PC_UI_AB_CHAIN;
    const pc_ui_ability* e = nullptr;
    if (v.mode == PC_UI_ABM_SLOTS) {
        int i = l.slots[v.slot];
        e = i >= 0 && i < count ? &l.entries[i] : nullptr;
    } else if (count) {
        e = &l.entries[v.cursor < 0 ? 0 : v.cursor >= count ? count - 1 : v.cursor];
    }
    float cw = card_width(f.W);
    Card c = {f.W - t.margin_x - cw, top, cw};
    float x = c.x + 20, xr = c.x + c.w - 20, w = xr - x, y = top + 18;
    card_begin(f);
    std::string head = chain ? "Chain ability" : "Defense ability";
    if (v.mode == PC_UI_ABM_SLOTS || v.mode == PC_UI_ABM_PICK)
        head += std::string(DOT) + BUTTON_NAMES[v.slot];
    label(f, t, x, y, head.c_str());
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, e ? t.bone : t.dim,
        fit(f, fonts.name, 25, e ? e->name : count ? "Empty slot" : "None learned", w).c_str());
    y += 31 + 8;
    if (e) {
        std::string meta = risk(e->cost) + DOT + (chain ? "Press on the hit" : "Press as you are hit");
        text(f, fonts.label, t.label_size, x, y, t.ash, fit(f, fonts.label, t.label_size, upper(meta.c_str()).c_str(), w, 0.2f).c_str(), 0.2f);
        y += 13 + 12;
        if (e->help[0])
            y += paragraph(f, t, x, y, w, t.ash, e->help);
        if (e->button >= 0 && e->button < PC_UI_ABIL_BUTTONS && v.mode != PC_UI_ABM_SLOTS) {
            y += 12;
            float lx = x + text(f, fonts.data, 12, x, y, t.ash, "On") + 6;
            lx += glyph(f, BUTTONS[e->button], 16, lx, y + 7) + 6;
            text(f, fonts.data, 12, lx, y, t.ash, BUTTON_NAMES[e->button]);
            y += 16;
        }
    } else {
        y += paragraph(f, t, x, y, w, t.ash,
            count ? "Nothing on this button. Pick a learned ability to put on it."
                  : "No ability of this kind learned yet: they come with kills.");
    }
    if (a.next_ability_kills >= 0) {
        y += 16;
        f.dl->AddLine(f.p(x, y), f.p(xr, y), t.rule, f.s(1));
        y += 12;
        std::string next = "Next ability in " + kills(a.next_ability_kills, "");
        text(f, fonts.data, 12, x, y, t.dim, next.c_str());
        y += 16;
    }
    card_end(f, t, c, y + 18);
}

void draw_chain(const Frame& f, const ui::Theme& t, float top, float y, const pc_ui_abilities& a,
    const pc_ui_abilities_view& v, float dt, bool reset, Pointer& m, ScreenOut& out)
{
    float x0 = t.margin_x, x1 = x0 + left_width(f.W);
    const pc_ui_ability_list& l = a.lists[v.kind == PC_UI_AB_DEFENSE ? 1 : 0];
    int count = l.count < 0 ? 0 : l.count > PC_UI_ABILITIES ? PC_UI_ABILITIES : l.count;
    y += 14;
    slot_cards(f, t, x0, x1, y, l, v, count, m);
    y += SLOT_H + 18;
    if (v.mode == PC_UI_ABM_PICK) {
        float lx = x0 + text(f, fonts.label, t.label_size, x0, y, t.ash, "ASSIGN TO", 0.26f) + 8;
        glyph(f, BUTTONS[v.slot], 16, lx, y + 6);
    } else {
        label(f, t, x0, y, "Learned");
    }
    y += 13 + 6;
    ability_list(f, t, x0, x1, y, l, v, count, dt, reset, m);
    ability_card(f, t, top, a, l, v, count);
    std::string crumb = "Abilities" + std::string(NEXT) + SUB_NAMES[v.kind];
    switch (v.mode) {
    case PC_UI_ABM_LIST:
        // the original's direct presses: the button pressed is the button it goes on
        out.hint(PC_PROMPT_CIRCLE, -1, "", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_TRIANGLE, -1, "", PC_UI_EXIT);
        out.hint(PC_PROMPT_SQUARE, -1, "Set on that button", PC_UI_ALT);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_ABM_PICK:
        crumb += std::string(NEXT) + BUTTON_NAMES[v.slot];
        out.hint(PC_PROMPT_CIRCLE, -1, "Set", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    default:
        if (count)
            out.hint(PC_PROMPT_CIRCLE, -1, "Assign", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        break;
    }
    out.crumb = crumb;
}

// ---- Break Arts ----

void category_rail(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_abilities& a,
    const pc_ui_abilities_view& v, float dt, bool reset, Pointer& m)
{
    bool focus = v.mode == PC_UI_ABM_CATEGORIES;
    float target = y0 + v.category_row * CAT_H;
    anim.cat_y = reset ? target : approach(anim.cat_y, target, dt, t.focus_grow);
    if (focus)
        focus_band(f, t, x0, anim.cat_y, x1, anim.cat_y + CAT_H);
    else // the category whose arts are open: its bar only
        f.dl->AddRectFilled(f.p(x0, anim.cat_y + 8), f.p(x0 + 2, anim.cat_y + CAT_H - 8), t.accent);
    for (int r = 0; r < PC_UI_ART_CATEGORIES; ++r) {
        int c = v.order[r];
        if (c < 1 || c > PC_UI_ART_CATEGORIES)
            continue;
        const pc_ui_art_category& k = a.categories[c - 1];
        float ry = y0 + r * CAT_H, right = x1 - 12;
        bool on = c == v.category, equipped = c == a.equipped_category;
        m.hit(PC_UI_MOUSE_ART_CATEGORY, c, x0, ry, x1, ry + CAT_H);
        std::string learned = num(k.learned) + "/4";
        right -= text_right(f, fonts.data, 13, right, ry + 9, k.learned ? (on ? t.bone : t.ash) : t.dim, learned.c_str());
        if (equipped) {
            f.dl->AddCircleFilled(f.p(right - 12, ry + 16), f.s(3), t.gold);
            right -= 20;
        }
        std::string name = fit(f, fonts.name, 17, title_case(k.name).c_str(), right - 8 - (x0 + 14));
        text(f, fonts.name, 17, x0 + 14, ry + 5, on ? t.bone : k.learned || equipped ? t.ash : t.dim, name.c_str());
        // the sub line: equipped, then kills to the next art
        float sx = x0 + 14;
        if (equipped) {
            sx += text(f, fonts.data, 11, sx, ry + 28, t.gold, "Equipped");
            sx += text(f, fonts.data, 11, sx, ry + 28, t.dim, DOT);
        }
        std::string next = k.next_kills < 0 ? "All arts learned"
            : k.next_kills == 0           ? "Next art after the next battle"
                                          : "Next art in " + kills(k.next_kills, "");
        text(f, fonts.data, 11, sx, ry + 28, t.dim, fit(f, fonts.data, 11, next.c_str(), x1 - 12 - sx).c_str());
        f.dl->AddLine(f.p(x0, ry + CAT_H), f.p(x1, ry + CAT_H), t.rule, f.s(1));
    }
}

std::string art_value(const pc_ui_art& art)
{
    if (art.state == PC_UI_ART_LEARNED)
        return "HP " + num(art.cost);
    if (art.state == PC_UI_ART_PENDING)
        return "Learned after the next battle";
    return kills(art.kills_to_go, " to go");
}

void art_rows(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_abilities& a,
    const pc_ui_art_category& k, const pc_ui_abilities_view& v, float dt, bool reset, Pointer& m)
{
    bool focus = v.mode == PC_UI_ABM_ARTS;
    float target = y0 + v.art * ART_H;
    anim.art_y = reset ? target : approach(anim.art_y, target, dt, t.focus_grow);
    if (focus)
        focus_band(f, t, x0, anim.art_y, x1, anim.art_y + ART_H);
    for (int i = 0; i < PC_UI_ARTS; ++i) {
        const pc_ui_art& art = k.arts[i];
        float ry = y0 + i * ART_H, cy = ry + ART_H * 0.5f, right = x1 - 12;
        bool on = focus && i == v.art, learned = art.state == PC_UI_ART_LEARNED;
        m.hit(PC_UI_MOUSE_ART, i, x0, ry, x1, ry + ART_H);
        std::string value = art_value(art);
        unsigned vc = !learned ? (art.state == PC_UI_ART_PENDING ? t.gold : t.dim)
            : art.cost >= a.hp     ? t.down
            : on                   ? t.bone
                                   : t.ash;
        right -= text_right(f, fonts.data, 13, right, cy - 7, vc, value.c_str()) + 14;
        unsigned nc = !learned ? t.dim : art.usable ? (on || !focus ? t.bone : t.ash) : t.ash;
        std::string name = fit(f, fonts.name, 18, learned ? art.name : "???", right - (x0 + 14));
        text(f, fonts.name, 18, x0 + 14, cy - 11, nc, name.c_str());
        f.dl->AddLine(f.p(x0, ry + ART_H), f.p(x1, ry + ART_H), t.rule, f.s(1));
    }
}

void arts_card(const Frame& f, const ui::Theme& t, const Card& c, const pc_ui_abilities& a,
    const pc_ui_art_category& k, const pc_ui_abilities_view& v)
{
    float x = c.x + 20, xr = c.x + c.w - 20, w = xr - x, y = c.y + 18;
    std::string cat = title_case(k.name);
    card_begin(f);
    if (v.mode != PC_UI_ABM_ARTS) { // the category
        std::string head = "Weapon type";
        if (v.category == a.equipped_category)
            head += std::string(DOT) + "Equipped";
        label(f, t, x, y, head.c_str());
        y += 13 + 8;
        text(f, fonts.name, 25, x, y, t.bone, fit(f, fonts.name, 25, cat.c_str(), w).c_str());
        y += 31 + 8;
        std::string meta = num(k.learned) + " of 4 arts" + DOT + kills(k.kills, "");
        text(f, fonts.label, t.label_size, x, y, t.ash, upper(meta.c_str()).c_str(), 0.2f);
        y += 13 + 12;
        if (k.help[0])
            y += paragraph(f, t, x, y, w, t.ash, k.help);
        y += 16;
        f.dl->AddLine(f.p(x, y), f.p(xr, y), t.rule, f.s(1));
        y += 12;
        std::string next = k.next_kills < 0 ? "All four arts learned"
            : k.next_kills == 0           ? "Next art: learned after the next battle"
                                          : "Next art in " + kills(k.next_kills, "");
        text(f, fonts.data, 12, x, y, t.dim, next.c_str());
        y += 16;
        card_end(f, t, c, y + 18);
        return;
    }
    const pc_ui_art& art = k.arts[v.art < 0 ? 0 : v.art >= PC_UI_ARTS ? PC_UI_ARTS - 1 : v.art];
    bool learned = art.state == PC_UI_ART_LEARNED;
    label(f, t, x, y, ("Break art" + std::string(DOT) + cat).c_str());
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, learned ? t.bone : t.dim,
        fit(f, fonts.name, 25, learned ? art.name : "Not yet learned", w).c_str());
    y += 31 + 8;
    std::string meta = learned ? "Costs " + num(art.cost) + " HP" + DOT + "HP " + num(a.hp) + " / " + num(a.hp_max)
        : art.state == PC_UI_ART_PENDING ? std::string("Learned after the next battle")
                                         : num(art.kills_to_go) + " more " + lower(k.name) + " kills";
    text(f, fonts.label, t.label_size, x, y, art.state == PC_UI_ART_PENDING ? t.gold : t.ash,
        fit(f, fonts.label, t.label_size, upper(meta.c_str()).c_str(), w, 0.2f).c_str(), 0.2f);
    y += 13 + 12;
    if (learned && art.help[0])
        y += paragraph(f, t, x, y, w, t.ash, art.help);
    else if (!learned)
        y += paragraph(f, t, x, y, w, t.ash,
            art.state == PC_UI_ART_PENDING
                ? "Enough kills with this weapon type: the art is learned when the next battle is over."
                : "Learned with more kills using this weapon type.");
    if (learned && !art.usable && art.reason[0]) {
        y += 12;
        y += note(f, t, x, y, w, art.reason);
    }
    card_end(f, t, c, y + 18);
}

void draw_arts(const Frame& f, const ui::Theme& t, float top, float y, const pc_ui_abilities& a,
    const pc_ui_abilities_view& v, float dt, bool reset, Pointer& m, ScreenOut& out)
{
    float x0 = t.margin_x, x1 = x0 + rail_width(f.W);
    bool wide = f.W >= 1100;
    y += 12;
    label(f, t, x0, y, "Weapon types");
    float ly = y + 13 + 6;
    category_rail(f, t, x0, x1, ly, a, v, dt, reset, m);
    int c = v.category < 1 || v.category > PC_UI_ART_CATEGORIES ? 1 : v.category;
    const pc_ui_art_category& k = a.categories[c - 1];
    float cw = card_width(f.W);
    float ax0 = x1 + 24, ax1 = wide ? f.W - t.margin_x - cw - 24 : f.W - t.margin_x;
    label(f, t, ax0, y, (title_case(k.name) + " arts").c_str());
    art_rows(f, t, ax0, ax1, ly, a, k, v, dt, reset, m);
    Card card = wide ? Card{f.W - t.margin_x - cw, top, cw} : Card{ax0, ly + PC_UI_ARTS * ART_H + 18, ax1 - ax0};
    arts_card(f, t, card, a, k, v);
    out.crumb = "Abilities" + std::string(NEXT) + SUB_NAMES[PC_UI_AB_ARTS] + NEXT + title_case(k.name);
    if (v.mode == PC_UI_ABM_ARTS) {
        const pc_ui_art& art = k.arts[v.art < 0 ? 0 : v.art >= PC_UI_ARTS ? PC_UI_ARTS - 1 : v.art];
        if (art.state == PC_UI_ART_LEARNED && art.usable)
            out.hint(PC_PROMPT_CIRCLE, -1, "Use", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
    } else {
        out.hint(PC_PROMPT_CIRCLE, -1, "Arts", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
    }
}

} // namespace

void draw_abilities(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    pc_ui_abilities_view v;
    pc_ui_shell_abilities_view(&v);
    const pc_ui_abilities& a = *pc_ui_shell_abilities();
    float x0 = t.margin_x;
    float x1 = x0 + (v.kind == PC_UI_AB_ARTS ? f.W - 2 * t.margin_x : left_width(f.W));
    if (v.kind == PC_UI_AB_ARTS && f.W >= 1100)
        x1 = f.W - t.margin_x - card_width(f.W) - 24;
    // the bands and the scroll are placed at once when the tab opens, the sub-tab or the mode changes
    bool reset = fresh || anim.kind != v.kind || anim.mode != v.mode;
    float y = top;
    if (v.subs > 0)
        y = subtabs(f, t, x0, x1, top, v, m);
    if (!a.valid || v.subs <= 0) {
        if (!a.valid) {
            spinner(f, x0 + 22, y + 30, 6, t.ash);
            text(f, fonts.label, t.label_size, x0 + 38, y + 24, t.ash, "LOADING", 0.26f);
        } else {
            paragraph(f, t, x0 + 14, y + 20, left_width(f.W) - 28, t.dim, "No abilities yet.");
        }
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        out.crumb = "Abilities";
    } else if (v.kind == PC_UI_AB_ARTS) {
        draw_arts(f, t, top, y, a, v, dt, reset, m, out);
    } else {
        draw_chain(f, t, top, y, a, v, dt, reset, m, out);
    }
    anim.kind = v.kind, anim.mode = v.mode;
}

} // namespace uidraw
