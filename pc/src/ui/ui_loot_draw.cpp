// Modern menus: the Loot screen (pc/docs/Modern_Menus.md), drawn alone (no
// tab bar: ui_draw.cpp draws its title, "Loot" or "Chest", and the footer).
//
// Across the top: the bag's seven categories as small meters ("WEAPONS 3 / 8"),
// as the original's GADGET BAG column: the ones the focused drop would take
// a record of light up, red when it would not fit. The left column lists the
// drops (a ring with the Items list's icon, the name, material and kind,
// "×n" for misc, NO ROOM when the bag cannot take it, KEY for keys and
// sigils). The right card compares the focused drop with what is worn where
// it goes (the equipped weapon / shield, the armor or accessory of that
// part): DP / PP, STR / INT / AGL with the change, class and affinity bars
// with a tick where the worn piece stands; misc and parts show the game's
// description. Menus open as popovers by the focused row: Organize (the
// row's own action and Organize inventory, each with the game's line), the
// Yes / No questions (the game's text), the messages (keys, an empty chest).
// Organize inventory turns the list into the bag (the meters become its
// category chips): Circle on a row opens its actions (Discard, Discard all,
// Sort, Move) as the Items screen's, with the quantity picker, the sort list
// and the move's target as popovers / list marks.
//
// State comes from ui_loot.cpp (pc_ui_loot_view_get, the bridge's snapshot);
// nothing here changes it except through the pointer.
#include "ui/ui_draw_internal.h"
#include "button_prompts.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace uidraw {
namespace {

const char* const CAT_NAMES[PC_UI_ITEM_CATEGORIES] = {"Weapons", "Blades", "Grips", "Shields", "Armor", "Gems", "Misc"};
const char* const CAT_ONE[PC_UI_ITEM_CATEGORIES] = {"Weapon", "Blade", "Grip", "Shield", "Armor", "Gem", "Item"};
const char* const NEXT = " \xe2\x80\xba "; // " › "
const char* const TIMES = "\xc3\x97";      // "×"

const float BOTTOM = 720 - 64;
const float ROW_H = 48;
const float RING_R = 16;
const float MENU_ROW = 30;

struct Anim {
    float list_y = 0, scroll = 0, scroll_target = 0;
    int mode = -1, category = -1;
} anim;

bool is_key(int id) { return id >= 458; }

// The game's menu rows are capitals ("ORGANIZE INVENTORY"): sentence case, as the shell's own rows.
std::string sentence(const char* s)
{
    std::string out = s ? s : "";
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = (char)(out[i] - 'A' + 'a');
    return out;
}

void pop_panel(const Frame& f, const ui::Theme& t, float x0, float y0, float x1, float y1)
{
    f.dl->AddRectFilled(f.p(x0 + 3, y0 + 5), f.p(x1 + 6, y1 + 8), 0x50000000u);
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y1), t.panel);
    f.dl->AddRect(f.p(x0, y0), f.p(x1, y1), t.rule2, 0, 0, f.s(1));
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x1, y0 + 2), t.accent);
}
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
float para_h(const Frame& f, const ui::Theme& t, float w, const char* s, float size = 0)
{
    ImFont* body = fonts.name ? fonts.name : ImGui::GetFont();
    float px = f.s(size > 0 ? size : t.body_size);
    return body->CalcTextSizeA(px, 1e9f, f.s(w), s).y / f.k;
}
float note(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* s)
{
    f.dl->AddRectFilled(f.p(x, y + 1), f.p(x + 2, y + t.body_size + 3), t.gold);
    return paragraph(f, t, x + 10, y, w - 10, t.gold, s, 13.5f);
}

// ---- The bag's meters (the Organize panel: its category chips) ----
float meters(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_loot& g,
    const pc_ui_loot_view& v, Pointer& m)
{
    bool bag = PC_UI_LOOT_IN_BAG(v.mode);
    const pc_ui_items* b = pc_ui_loot_bag();
    float cw = (x1 - x0) / PC_UI_ITEM_CATEGORIES;
    for (int c = 0; c < PC_UI_ITEM_CATEGORIES; ++c) {
        float cx = x0 + c * cw, w = cw - 10;
        int n = bag && b->valid ? b->count[c] : g.bag_count[c], cap = g.bag_capacity[c];
        int need = !bag && v.row ? v.row->needs[c] : 0;
        bool over = bag && m.hit(PC_UI_MOUSE_SUB, c, cx - 4, y - 6, cx + w + 4, y + 36);
        bool on = bag ? c == v.bag_category : need > 0;
        bool overflow = need > 0 && n + need > cap;
        unsigned name_col = on ? t.bone : over ? t.ash : t.dim;
        std::string name = upper(CAT_NAMES[c]);
        text(f, fonts.label, 10, cx, y, name_col, fit(f, fonts.label, 10, name.c_str(), w, 0.2f).c_str(), 0.2f);
        std::string count = num(n) + " / " + num(cap);
        if (need > 0)
            count = num(n) + "+" + num(need) + " / " + num(cap);
        text(f, fonts.data, 12, cx, y + 13, overflow ? t.down : on ? t.bone : t.ash,
            fit(f, fonts.data, 12, count.c_str(), w).c_str());
        float by = y + 31;
        f.dl->AddRectFilled(f.p(cx, by), f.p(cx + w, by + 3), t.rule);
        float a = cap > 0 ? (float)(n < 0 ? 0 : n > cap ? cap : n) / cap : 0;
        if (a > 0)
            f.dl->AddRectFilled(f.p(cx, by), f.p(cx + w * a, by + 3), n >= cap ? t.gold : on ? t.bone : t.ash);
        if (need > 0) { // where the drop would go
            float a2 = cap > 0 ? (float)(n + need > cap ? cap : n + need) / cap : 0;
            f.dl->AddRectFilled(f.p(cx + w * a, by - 1), f.p(cx + w * a2, by + 4), overflow ? t.down : t.up);
        }
        if (bag && on)
            f.dl->AddRectFilled(f.p(cx, by + 6), f.p(cx + w, by + 8), t.accent);
    }
    return y + 44;
}

// ---- The list (the drops, or the bag's category) ----
struct Row {
    int category, id, count;
    const char* name;
    const char* sub;
    const char* tag; // NO ROOM, KEY, EQUIPPED ...
    unsigned tag_col;
    bool dim;
    bool marked;     // the row being moved (Move)
};

void list(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const Row* rows, int count, int cursor,
    int mouse_kind, float dt, bool reset, Pointer& m)
{
    int vis = (int)((BOTTOM - y0) / ROW_H);
    vis = vis < 1 ? 1 : vis;
    float y1 = y0 + vis * ROW_H;
    cursor = cursor < 0 ? 0 : cursor >= count ? count - 1 : cursor;
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
    f.dl->PushClipRect(f.p(x0, y0), f.p(x1 + 1, y1), true);
    if (count > 0)
        focus_band(f, t, x0, anim.list_y, x1, anim.list_y + ROW_H);
    for (int i = 0; i < count; ++i) {
        float ry = y0 + (i - anim.scroll) * ROW_H;
        if (ry + ROW_H < y0 || ry > y1)
            continue;
        const Row& e = rows[i];
        bool on = i == cursor;
        float hy0 = ry < y0 ? y0 : ry, hy1 = ry + ROW_H > y1 ? y1 : ry + ROW_H;
        if (hy1 > hy0)
            m.hit(mouse_kind, i, x0, hy0, x1, hy1);
        float cy = ry + ROW_H * 0.5f, right = x1 - 12;
        float rx = x0 + 14 + RING_R;
        if (on)
            f.dl->AddCircle(f.p(rx, cy), f.s(RING_R + 2), t.accent_soft, 36, f.s(3));
        f.dl->AddCircle(f.p(rx, cy), f.s(RING_R), on ? t.accent : t.rule2, 36, f.s(1));
        if (e.marked) // the row being moved
            f.dl->AddRectFilled(f.p(x0, ry + 4), f.p(x0 + 3, ry + ROW_H - 4), t.gold);
        bag_icon(f, e.category, e.id, e.sub, e.name, rx, cy, 16, e.dim ? t.dim : on ? t.bone : t.ash);
        if (e.tag) {
            float tw = measure(f, fonts.label, 10, e.tag, 0.22f);
            float bx = right - tw - 12;
            f.dl->AddRect(f.p(bx, cy - 9), f.p(right, cy + 9), e.tag_col, 0, 0, f.s(1));
            text(f, fonts.label, 10, bx + 6, cy - 6, e.tag_col, e.tag, 0.22f);
            right = bx - 10;
        }
        if (e.category == PC_UI_IC_MISC && e.count > 0) {
            std::string n = std::string(TIMES) + num(e.count);
            right -= text_right(f, fonts.data, 15, right, cy - 9, on ? t.bone : t.ash, n.c_str()) + 12;
        }
        std::string sub = e.sub ? e.sub : "";
        if (!sub.empty() && sub.back() == '.')
            sub.pop_back();
        float nx = rx + RING_R + 14, nw = right - nx;
        bool two = !sub.empty();
        unsigned nc = e.dim ? t.dim : on ? t.bone : t.ash;
        text(f, fonts.name, 18, nx, two ? ry + 6 : cy - 11, nc, fit(f, fonts.name, 18, e.name, nw).c_str());
        if (two)
            text(f, fonts.data, 11, nx, ry + 29, on ? t.ash : t.dim, fit(f, fonts.data, 11, sub.c_str(), nw).c_str());
        f.dl->AddLine(f.p(x0, ry + ROW_H), f.p(x1, ry + ROW_H), t.rule, f.s(1));
    }
    f.dl->PopClipRect();
    if (count > vis) {
        float track = y1 - y0, thumb = track * vis / count, ty = y0 + (track - thumb) * (anim.scroll / max_scroll);
        f.dl->AddRectFilled(f.p(x1 + 6, y0), f.p(x1 + 8, y1), t.rule);
        f.dl->AddRectFilled(f.p(x1 + 6, ty), f.p(x1 + 8, ty + thumb), t.ash);
    }
}

// ---- The card ----
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

const char* const SLOT_NAMES[PC_UI_EQUIP_SLOTS] = {"Weapon", "Shield", "Right arm", "Left arm", "Head", "Body",
    "Legs", "Accessory"};

void loot_card(const Frame& f, const ui::Theme& t, const Card& card, const pc_ui_loot& g, const pc_ui_loot_row& e)
{
    const int c = e.category;
    const pc_ui_item_details* d = g.details.valid && c != PC_UI_IC_MISC ? &g.details : nullptr;
    bool compare = g.equipped_slot >= 0 && g.equipped.inventory >= 0 && d
        && (c == PC_UI_IC_WEAPON || c == PC_UI_IC_SHIELD || c == PC_UI_IC_ARMOR);
    bool wears = d && (c == PC_UI_IC_WEAPON || c == PC_UI_IC_SHIELD || c == PC_UI_IC_ARMOR);
    float x = card.x + 20, xr = card.x + card.w - 20, w = xr - x, y = card.y + 18;
    card_begin(f);
    std::string head = c >= 0 && c < PC_UI_ITEM_CATEGORIES ? CAT_ONE[c] : "";
    if (e.key)
        head = "Key item";
    label(f, t, x, y, head.c_str());
    if (!e.fits)
        text_right(f, fonts.label, t.label_size, xr, y, t.gold, "NO ROOM", 0.26f);
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, t.bone, fit(f, fonts.name, 25, e.name, w).c_str());
    y += 31 + 6;
    std::string meta = d && (c == PC_UI_IC_WEAPON || c == PC_UI_IC_BLADE) ? kind_line(d->stats) : e.sub;
    if (c == PC_UI_IC_MISC)
        meta = std::string(TIMES) + num(e.count);
    if (!meta.empty()) {
        text(f, fonts.label, t.label_size, x, y, t.ash,
            fit(f, fonts.label, t.label_size, upper(meta.c_str()).c_str(), w, 0.2f).c_str(), 0.2f);
        y += 13 + 12;
    }
    if (wears) { // what it is compared with
        std::string vs = compare ? std::string("Against ") + g.equipped.name
                                 : std::string("Nothing worn there");
        if (compare && g.equipped_slot >= 0 && g.equipped_slot < PC_UI_EQUIP_SLOTS && c == PC_UI_IC_ARMOR)
            vs += DOT + std::string(SLOT_NAMES[g.equipped_slot]);
        text(f, fonts.data, 11, x, y, t.dim, fit(f, fonts.data, 11, vs.c_str(), w).c_str());
        y += 20;
    }
    if (d) {
        const pc_ui_item& s = d->stats;
        if (s.dp_max > 0 || s.pp_max > 0) {
            float hw = (w - 20) * 0.5f;
            meter(f, t, x, y, hw, "DP", s.dp, s.dp_max, t.hp);
            if (s.pp_max > 0)
                meter(f, t, x + hw + 20, y, hw, "PP", s.pp, s.pp_max, t.gold);
            y += 30 + 10;
        }
        static const char* const SN[3] = {"STR", "INT", "AGL"};
        const int now[3] = {s.str, s.intel, s.agl};
        const int was[3] = {g.equipped.str, g.equipped.intel, g.equipped.agl};
        float cw = w / 3;
        for (int k = 0; k < 3; ++k) {
            float cx = x + k * cw;
            text(f, fonts.label, 10, cx, y, t.dim, SN[k], 0.22f);
            text(f, fonts.data, 16, cx, y + 14, now[k] < 0 ? t.down : t.bone, signed_num(now[k]).c_str());
            if (compare)
                delta(f, t, cx + cw - 12, y + 17, now[k] - was[k], 12);
        }
        y += 40 + 8;
        if (c != PC_UI_IC_GRIP) {
            label(f, t, x, y, "Class");
            y += 13 + 8;
            y += bars(f, t, x, xr, y, CLASSES, compare ? g.equipped.classes : s.classes, s.classes, PC_UI_CLASSES,
                !compare, t.gold);
            y += 10;
            label(f, t, x, y, "Affinity");
            y += 13 + 8;
            y += bars(f, t, x, xr, y, AFFINITIES, compare ? g.equipped.affinities : s.affinities, s.affinities,
                PC_UI_AFFINITIES, !compare, t.mp);
        }
    }
    if (e.help[0] && y + para_h(f, t, w, e.help) + 12 < BOTTOM - 40) {
        y += 8;
        y += paragraph(f, t, x, y, w, t.ash, e.help);
    }
    if (e.key) {
        y += 12;
        y += note(f, t, x, y, w, "A key or sigil: taking it opens its way. It cannot be left behind.");
    }
    if (!e.fits) {
        y += 12;
        y += note(f, t, x, y, w, "The bag has no room for it. Circle opens Organize.");
    }
    card_end(f, t, card, y + 18);
}

void bag_card(const Frame& f, const ui::Theme& t, const Card& card, const pc_ui_items& b, int c,
    const pc_ui_item_row* e, const pc_ui_loot_view& v)
{
    float x = card.x + 20, xr = card.x + card.w - 20, w = xr - x, y = card.y + 18;
    card_begin(f);
    label(f, t, x, y, "Organize inventory");
    y += 13 + 8;
    text(f, fonts.name, 25, x, y, e ? t.bone : t.dim, fit(f, fonts.name, 25, e ? e->name : "Nothing here", w).c_str());
    y += 31 + 6;
    if (e) {
        if (e->sub[0]) {
            text(f, fonts.label, t.label_size, x, y, t.ash,
                fit(f, fonts.label, t.label_size, upper(e->sub).c_str(), w, 0.2f).c_str(), 0.2f);
            y += 13 + 12;
        }
        if (c == PC_UI_IC_MISC && e->count > 0) {
            text(f, fonts.data, 15, x, y, t.bone, (std::string(TIMES) + num(e->count)).c_str());
            y += 24;
        }
        if (e->help[0]) {
            y += paragraph(f, t, x, y, w, t.ash, e->help) + 10;
        }
        if (!e->discard && e->discard_reason[0] && v.mode == PC_UI_LOOT_BAG)
            y += note(f, t, x, y, w, e->discard_reason) + 6;
    }
    card_end(f, t, card, y + 18);
}

// ---- Popovers ----
void organize_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, float row_y, const pc_ui_loot& g,
    const pc_ui_loot_view& v, Pointer& m)
{
    float w = 300, px1 = x1 - 12, px0 = px1 - w;
    if (px0 < x0 + 20)
        px0 = x0 + 20, px1 = px0 + w;
    int k = v.menu >= 0 && v.menu < v.menu_rows ? v.menu_kind[v.menu] : 0;
    const char* info = g.org_info[k];
    float ih = para_h(f, t, w - 36, info, 13);
    float h = 14 + 13 + 10 + v.menu_rows * MENU_ROW + 10 + ih + 14;
    float py = pop_top(row_y, h, y0);
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_NONE, 0, px0, py, px1, py + h);
    float y = py + 14;
    label(f, t, px0 + 18, y, "No room in the bag");
    y += 13 + 10;
    for (int i = 0; i < v.menu_rows; ++i) {
        bool on = i == v.menu;
        bool over = m.hit(PC_UI_MOUSE_LOOT_MENU, i, px0, y, px1, y + MENU_ROW);
        if (on)
            focus_band(f, t, px0, y, px1, y + MENU_ROW);
        text(f, fonts.name, 16.5f, px0 + 18, y + 5, on || over ? t.bone : t.ash,
            sentence(g.org_label[v.menu_kind[i]]).c_str());
        y += MENU_ROW;
    }
    y += 10;
    paragraph(f, t, px0 + 18, y, w - 36, t.dim, info, 13);
}

void confirm_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, float row_y, const char* head,
    const char* text_, int yes, Pointer& m)
{
    float w = 300, px1 = x1 - 12, px0 = px1 - w;
    if (px0 < x0 + 20)
        px0 = x0 + 20, px1 = px0 + w;
    float x = px0 + 18, xr = px1 - 18;
    const char* q = text_ && text_[0] ? text_ : "Are you sure?";
    float ph = para_h(f, t, xr - x, q);
    float h = 14 + 13 + 10 + ph + 12 + 2 * MENU_ROW + 10;
    float py = pop_top(row_y, h, y0);
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_NONE, 0, px0, py, px1, py + h);
    float y = py + 14;
    label(f, t, x, y, head);
    y += 13 + 10;
    y += paragraph(f, t, x, y, xr - x, t.bone, q) + 12;
    const char* labels[2] = {"Yes", "No"};
    for (int i = 0; i < 2; ++i) {
        bool on = (yes ? 0 : 1) == i;
        bool over = m.hit(PC_UI_MOUSE_LOOT_MENU, i, px0, y, px1, y + MENU_ROW);
        if (on)
            focus_band(f, t, px0, y, px1, y + MENU_ROW);
        text(f, fonts.name, 16.5f, px0 + 18, y + 5, on || over ? t.bone : t.ash, labels[i]);
        y += MENU_ROW;
    }
}

// A popover list by the focused row: a head, rows (greyed when not selectable), the focused row's line.
void list_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, float row_y, const char* head,
    const char* const* labels, const bool* enabled, int n, int focus, const char* info, Pointer& m)
{
    float w = 300, px1 = x1 - 12, px0 = px1 - w;
    if (px0 < x0 + 20)
        px0 = x0 + 20, px1 = px0 + w;
    float ih = info && info[0] ? para_h(f, t, w - 36, info, 13) + 10 : 0;
    float h = 14 + 13 + 10 + n * MENU_ROW + 10 + ih + 4;
    float py = pop_top(row_y, h, y0);
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_NONE, 0, px0, py, px1, py + h);
    float y = py + 14;
    label(f, t, px0 + 18, y, head);
    y += 13 + 10;
    for (int i = 0; i < n; ++i) {
        bool on = i == focus;
        bool over = m.hit(PC_UI_MOUSE_LOOT_MENU, i, px0, y, px1, y + MENU_ROW);
        if (on)
            focus_band(f, t, px0, y, px1, y + MENU_ROW);
        unsigned col = !enabled[i] ? t.dim : on || over ? t.bone : t.ash;
        text(f, fonts.name, 16.5f, px0 + 18, y + 5, col, fit(f, fonts.name, 16.5f, labels[i], w - 36).c_str());
        y += MENU_ROW;
    }
    if (ih > 0)
        paragraph(f, t, px0 + 18, y + 10, w - 36, t.dim, info, 13);
}

void quantity_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, float row_y, const char* line,
    const pc_ui_loot_view& v, Pointer& m)
{
    static const int steps[3] = {1, 10, 100};
    float w = 300, px1 = x1 - 12, px0 = px1 - w;
    if (px0 < x0 + 20)
        px0 = x0 + 20, px1 = px0 + w;
    float ph = line && line[0] ? para_h(f, t, w - 36, line) + 12 : 0;
    float h = 14 + 13 + 10 + ph + 44 + 22 + 10;
    float py = pop_top(row_y, h, y0);
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_NONE, 0, px0, py, px1, py + h);
    float y = py + 14;
    label(f, t, px0 + 18, y, "Discard how many");
    y += 13 + 10;
    if (ph > 0)
        y += paragraph(f, t, px0 + 18, y, w - 36, t.ash, line) + 12;
    std::string n = num(v.count) + " / " + num(v.count_max);
    text(f, fonts.data, 30, px0 + 18, y, v.count ? t.bone : t.dim, n.c_str());
    y += 44;
    std::string step = std::string("STEP ") + num(steps[v.step < 0 ? 0 : v.step > 2 ? 2 : v.step]);
    text(f, fonts.label, 10, px0 + 18, y, t.dim, step.c_str(), 0.22f);
}

void message_pop(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const char* head, const char* s,
    Pointer& m)
{
    float w = 380, cx = (x0 + x1) * 0.5f, px0 = cx - w * 0.5f, px1 = cx + w * 0.5f;
    if (px0 < x0)
        px0 = x0, px1 = x0 + w;
    float ph = para_h(f, t, w - 36, s ? s : "");
    float h = 14 + 13 + 12 + ph + 22;
    float py = y0 + 40;
    pop_panel(f, t, px0, py, px1, py + h);
    m.hit(PC_UI_MOUSE_LOOT_MENU, 0, px0, py, px1, py + h);
    label(f, t, px0 + 18, py + 14, head);
    paragraph(f, t, px0 + 18, py + 14 + 13 + 12, w - 36, t.bone, s ? s : "");
}

} // namespace

void draw_loot(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    const pc_ui_loot& g = *pc_ui_loot_model();
    const pc_ui_items& b = *pc_ui_loot_bag();
    pc_ui_loot_view v;
    pc_ui_loot_view_get(&v);
    bool bag = PC_UI_LOOT_IN_BAG(v.mode);
    float cw = card_width(f.W);
    float x0 = t.margin_x, xr = f.W - t.margin_x, x1 = xr - cw - 24;
    std::string crumb = g.chest ? "Chest" : "Loot";
    if (!g.valid) {
        spinner(f, x0 + 22, top + 16, 6, t.ash);
        text(f, fonts.label, t.label_size, x0 + 38, top + 10, t.ash, "LOADING", 0.26f);
        out.crumb = crumb;
        return;
    }
    float y = meters(f, t, x0, x1, top, g, v, m);
    Card card = {xr - cw, top, cw};
    int c = v.bag_category;
    bool reset = fresh || anim.mode < 0 || bag != PC_UI_LOOT_IN_BAG(anim.mode)
        || (bag && anim.category != c);
    static Row rows[PC_UI_ITEM_ROWS]; // the bag (176) or the drops (PC_UI_LOOT_ROWS, fewer)
    int n = 0;
    const pc_ui_item_row* focused_bag = nullptr;
    if (bag) {
        label(f, t, x0, y, (std::string("Organize") + DOT + CAT_NAMES[c]).c_str());
        y += 13 + 10;
        for (int r = 0; r < v.bag_rows && b.first[c] + r < PC_UI_ITEM_ROWS; ++r) {
            const pc_ui_item_row& e = b.rows[b.first[c] + r];
            const char* tag = e.mark == PC_UI_MARK_EQUIPPED ? "EQUIPPED" : e.mark == PC_UI_MARK_SET ? "IN A SET"
                : is_key(e.id) && c == PC_UI_IC_MISC                                          ? "KEY"
                                                                                               : nullptr;
            rows[n++] = {c, e.id, e.count, e.name, e.sub, tag, e.mark == PC_UI_MARK_EQUIPPED ? t.gold : t.ash, false,
                r == v.move_from};
        }
        if (v.bag_cursor >= 0 && v.bag_cursor < n)
            focused_bag = &b.rows[b.first[c] + v.bag_cursor];
        if (n)
            list(f, t, x0, x1, y, rows, n, v.bag_cursor, PC_UI_MOUSE_LOOT_BAG, dt, reset, m);
        else {
            paragraph(f, t, x0 + 14, y + 10, x1 - x0 - 28, t.dim, "Nothing here.");
            anim.list_y = y - ROW_H;
        }
        bag_card(f, t, card, b, c, focused_bag, v);
    } else {
        std::string head = g.chest ? "In the chest" : "Dropped";
        head += DOT + num(v.rows);
        label(f, t, x0, y, head.c_str());
        if (g.claimed)
            text_right(f, fonts.label, 10, x1, y + 1, t.dim, (num(g.claimed) + " TAKEN").c_str(), 0.22f);
        y += 13 + 10;
        for (int r = 0; r < v.rows; ++r) {
            const pc_ui_loot_row& e = g.rows[r];
            const char* tag = !e.fits ? "NO ROOM" : e.key ? "KEY" : nullptr;
            rows[n++] = {e.category, e.id, e.count, e.name, e.sub, tag, !e.fits ? t.gold : t.ash, false, false};
        }
        if (n)
            list(f, t, x0, x1, y, rows, n, v.cursor, PC_UI_MOUSE_LOOT_ROW, dt, reset, m);
        else
            anim.list_y = y - ROW_H;
        if (v.row)
            loot_card(f, t, card, g, *v.row);
    }
    float row_y = anim.list_y;
    switch (v.mode) {
    case PC_UI_LOOT_ORGANIZE:
        organize_pop(f, t, x0, x1, y, row_y, g, v, m);
        break;
    case PC_UI_LOOT_CONFIRM: {
        const char* head = v.ask == PC_UI_LOOT_ASK_DISASSEMBLE ? "Disassemble"
            : v.ask == PC_UI_LOOT_ASK_CONSOLIDATE              ? "Consolidate"
                                                               : "Leave";
        confirm_pop(f, t, x0, x1, y, row_y, head, v.text, v.menu == 0, m);
        break;
    }
    case PC_UI_LOOT_BAG_CONFIRM: {
        std::string q = v.text ? v.text : "";
        if (v.confirm_kind == PC_UI_ITA_DISCARD && v.count_max >= 2)
            q = std::string(TIMES) + num(v.count) + "  " + q;
        confirm_pop(f, t, x0, x1, y, row_y, v.confirm_kind == PC_UI_ITA_DISCARD_ALL ? "Discard all" : "Discard",
            q.c_str(), v.menu == 0, m);
        break;
    }
    case PC_UI_LOOT_BAG_ACTIONS: {
        static const char* const NAMES[PC_UI_ITA_COUNT] = {"Use", "Search", "Discard", "Discard all", "Sort", "Move",
            ""};
        const char* labels[PC_UI_LOOT_BAG_ACTION_MAX];
        bool en[PC_UI_LOOT_BAG_ACTION_MAX];
        for (int i = 0; i < v.bag_actions; ++i) {
            labels[i] = NAMES[v.bag_action_kind[i]];
            en[i] = v.bag_action_enabled[i] != 0;
        }
        const char* why = v.menu >= 0 && v.menu < v.bag_actions && !en[v.menu] ? v.bag_action_reason[v.menu] : nullptr;
        list_pop(f, t, x0, x1, y, row_y, focused_bag ? focused_bag->name : "", labels, en, v.bag_actions, v.menu, why,
            m);
        break;
    }
    case PC_UI_LOOT_BAG_QUANTITY:
        quantity_pop(f, t, x0, x1, y, row_y, v.text, v, m);
        break;
    case PC_UI_LOOT_BAG_SORT: {
        int k = b.valid ? b.sort_count[c] : 0;
        k = k < 0 ? 0 : k > PC_UI_SORT_STATS ? PC_UI_SORT_STATS : k;
        std::string names[PC_UI_SORT_STATS];
        const char* labels[PC_UI_SORT_STATS];
        bool en[PC_UI_SORT_STATS];
        for (int i = 0; i < k; ++i) {
            int st = b.sort_stats[c][i];
            names[i] = sentence(st >= 0 && st < PC_UI_STAT_NAMES ? b.stat_name[st] : "");
            labels[i] = names[i].c_str();
            en[i] = true;
        }
        int st = v.sort >= 0 && v.sort < k ? b.sort_stats[c][v.sort] : -1;
        list_pop(f, t, x0, x1, y, row_y, "Sort by", labels, en, k, v.sort,
            st >= 0 && st < PC_UI_STAT_NAMES ? b.stat_help[st] : nullptr, m);
        break;
    }
    case PC_UI_LOOT_MESSAGE:
        message_pop(f, t, x0, x1, y, v.ask == PC_UI_LOOT_ASK_EMPTY ? (g.chest ? "Chest" : "Loot") : "Leave", v.text, m);
        break;
    default:
        break;
    }
    if (v.row && !bag)
        crumb += NEXT + std::string(v.row->name);
    switch (v.mode) {
    case PC_UI_LOOT_LIST:
        if (v.row)
            out.hint(PC_PROMPT_CIRCLE, -1, v.row->fits ? "Take" : "Organize", PC_UI_CONFIRM);
        if (v.rows)
            out.hint(PC_PROMPT_SQUARE, -1, "Take all", PC_UI_ALT);
        out.hint(PC_PROMPT_CROSS, -1, "Leave", PC_UI_BACK);
        break;
    case PC_UI_LOOT_ORGANIZE:
        crumb += NEXT + std::string("Organize");
        out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_LOOT_CONFIRM:
        out.hint(PC_PROMPT_CIRCLE, -1, v.menu == 0 ? (v.ask == PC_UI_LOOT_ASK_LEAVE ? "Leave" : "Confirm") : "Select",
            PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_LOOT_MESSAGE:
        out.hint(PC_PROMPT_CIRCLE, -1, "OK", PC_UI_CONFIRM);
        break;
    case PC_UI_LOOT_BAG:
        crumb += NEXT + std::string("Organize") + NEXT + CAT_NAMES[c];
        if (focused_bag)
            out.hint(PC_PROMPT_CIRCLE, -1, "Actions", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_L2, PC_PROMPT_R2, "Category", PC_UI_SUB_NEXT);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_LOOT_BAG_ACTIONS:
        crumb += NEXT + std::string("Organize") + NEXT + CAT_NAMES[c];
        out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_LOOT_BAG_QUANTITY:
        crumb += NEXT + std::string("Organize") + NEXT + "Discard";
        out.hint(PC_PROMPT_UP, PC_PROMPT_DOWN, "Amount", PC_UI_UP);
        out.hint(PC_PROMPT_LEFT, PC_PROMPT_RIGHT, "Step", PC_UI_LEFT);
        out.hint(PC_PROMPT_CIRCLE, -1, v.count ? "Discard" : "None", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_LOOT_BAG_SORT:
        crumb += NEXT + std::string("Organize") + NEXT + "Sort";
        out.hint(PC_PROMPT_CIRCLE, -1, "Sort", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_LOOT_BAG_MOVE:
        crumb += NEXT + std::string("Organize") + NEXT + "Move";
        out.hint(PC_PROMPT_CIRCLE, -1, "Move here", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    case PC_UI_LOOT_BAG_CONFIRM:
        crumb += NEXT + std::string("Organize") + NEXT + "Discard";
        out.hint(PC_PROMPT_CIRCLE, -1, v.menu == 0 ? "Discard" : "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    default:
        break;
    }
    out.crumb = crumb;
    anim.mode = v.mode;
    anim.category = c;
}

} // namespace uidraw
