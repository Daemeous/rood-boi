// Modern menus: the Equipment tab (pc/docs/Modern_Menus.md). Left rail (448
// wide): sub-tabs Gear / Quick swap, the eight slots or a pick list, the two
// quick-swap diamonds. Right card: compare, or the quick slot's detail. The
// centre column stays empty (the live model goes there). State comes from the
// shell (pc_ui_shell_equip_view, the bridge's snapshot); nothing here changes it
// except through the pointer (Pointer hits, applied at the shell's next tick).
#include "ui/ui_draw_internal.h"
#include "button_prompts.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace uidraw {

// shared with the other screens (ui_draw_internal.h)
const char* const CLASSES[PC_UI_CLASSES] = {"Human", "Beast", "Undead", "Phantom", "Dragon", "Evil"};
const char* const AFFINITIES[PC_UI_AFFINITIES] = {"Physical", "Air", "Fire", "Earth", "Water", "Light", "Dark"};
const char* const DAMAGE[3] = {"Blunt", "Edged", "Piercing"};
const char* const DOT = " \xc2\xb7 "; // " · "

namespace {

const char* const SLOT_LABELS[PC_UI_EQUIP_SLOTS] = {
    "Weapon", "Shield", "Right arm", "Left arm", "Head", "Body", "Legs", "Accessory"};
// quick slots in weapon_slots.c order: 1-4 face buttons, 5-8 D-pad
const int QUICK_BUTTONS[PC_UI_QUICK] = {PC_PROMPT_CIRCLE, PC_PROMPT_TRIANGLE, PC_PROMPT_SQUARE, PC_PROMPT_CROSS,
    PC_PROMPT_UP, PC_PROMPT_DOWN, PC_PROMPT_RIGHT, PC_PROMPT_LEFT};
const char* const QUICK_NAMES[PC_UI_QUICK] = {"Circle", "Triangle", "Square", "Cross", "Up", "Down", "Right", "Left"};
// each slot's cell (column, row) in its 3 x 3 diamond
const int DIAMOND[PC_UI_QUICK][2] = {{2, 1}, {1, 0}, {0, 1}, {1, 2}, {1, 0}, {1, 2}, {2, 1}, {0, 1}};
const char* const STATS[3] = {"STR", "INT", "AGL"};
const char* const NEXT = " \xe2\x80\xba "; // " › "

// layout units
const float RAIL = 448, BODY_BOTTOM = 720 - 64;
const float GEAR_H = 51, PICK_H = 56, RING_R = 19;

const Icon SLOT_ICONS[PC_UI_EQUIP_SLOTS] = {IC_SWORD, IC_SHIELD, IC_ARM, IC_ARM, IC_HEAD, IC_BODY, IC_LEGS, IC_RING};

struct Pen {
    const Frame& f;
    float cx, cy, u;
    bool mirror;
    unsigned col;
    float th;
    ImVec2 P(float x, float y) const { return f.p(cx + ((mirror ? 20 - x : x) - 10) * u, cy + (y - 10) * u); }
    void line(const float* xy, int n, bool closed) const
    {
        for (int i = 0; i < n; ++i)
            f.dl->PathLineTo(P(xy[2 * i], xy[2 * i + 1]));
        f.dl->PathStroke(col, closed ? ImDrawFlags_Closed : 0, th);
    }
};

} // namespace

void icon(const Frame& f, Icon ic, float cx, float cy, float size, unsigned col, bool mirror)
{
    Pen p{f, cx, cy, size / 20.0f, mirror, col, f.s(1.3f * size / 20.0f)};
    ImDrawList* dl = f.dl;
    switch (ic) {
    case IC_SWORD: {
        const float blade[] = {5.5f, 13.0f, 14.0f, 4.5f, 16.0f, 4.0f, 15.5f, 6.0f, 7.0f, 14.5f};
        const float guard[] = {4.0f, 11.5f, 8.5f, 16.0f};
        const float grip[] = {5.5f, 14.5f, 3.0f, 17.0f};
        p.line(blade, 5, true);
        p.line(guard, 2, false);
        p.line(grip, 2, false);
        break;
    }
    case IC_SHIELD: {
        dl->PathLineTo(p.P(10, 3));
        dl->PathLineTo(p.P(16, 5.2f));
        dl->PathLineTo(p.P(16, 9.5f));
        dl->PathBezierCubicCurveTo(p.P(16, 13.3f), p.P(13.4f, 15.9f), p.P(10, 17));
        dl->PathBezierCubicCurveTo(p.P(6.6f, 15.9f), p.P(4, 13.3f), p.P(4, 9.5f));
        dl->PathLineTo(p.P(4, 5.2f));
        dl->PathStroke(col, ImDrawFlags_Closed, p.th);
        const float mid[] = {10, 6, 10, 14};
        p.line(mid, 2, false);
        break;
    }
    case IC_HEAD: {
        dl->PathArcTo(p.P(10, 11), 5.5f * p.u * f.k, 3.14159265f, 2 * 3.14159265f, 16);
        dl->PathLineTo(p.P(15.5f, 13.5f));
        dl->PathLineTo(p.P(4.5f, 13.5f));
        dl->PathStroke(col, ImDrawFlags_Closed, p.th);
        const float l[] = {4.5f, 13.5f, 3.5f, 15.0f}, r[] = {15.5f, 13.5f, 16.5f, 15.0f}, crest[] = {10, 5.5f, 10, 3};
        p.line(l, 2, false);
        p.line(r, 2, false);
        p.line(crest, 2, false);
        break;
    }
    case IC_BODY: {
        const float b[] = {6, 4, 10, 6, 14, 4, 16.5f, 6.5f, 14.5f, 8.7f, 14.5f, 16, 5.5f, 16, 5.5f, 8.7f, 3.5f, 6.5f};
        p.line(b, 9, true);
        break;
    }
    case IC_ARM: {
        const float a[] = {6, 3.5f, 6, 9.5f, 4.5f, 12.5f, 7, 16.5f, 12, 16.5f, 13.5f, 12, 12, 9.5f, 12, 3.5f};
        const float cuff[] = {6, 9.5f, 12, 9.5f};
        p.line(a, 8, false);
        p.line(cuff, 2, false);
        break;
    }
    case IC_LEGS: {
        const float l[] = {6, 3.5f, 14, 3.5f, 13.3f, 9.5f, 14, 16.5f, 11, 16.5f, 10.2f, 10, 9.8f, 10, 9, 16.5f, 6,
            16.5f, 6.7f, 9.5f};
        p.line(l, 10, true);
        break;
    }
    case IC_RING: {
        dl->AddCircle(p.P(10, 12), 4.5f * p.u * f.k, col, 20, p.th);
        const float gem[] = {8, 6.5f, 10, 3.5f, 12, 6.5f};
        p.line(gem, 3, true);
        break;
    }
    }
}

namespace {

// The round frame around a slot icon; focused: crimson with a soft halo.
void ring(const Frame& f, const ui::Theme& t, Icon ic, float cx, float cy, bool on, bool mirror = false,
    bool dim = false)
{
    if (on)
        f.dl->AddCircle(f.p(cx, cy), f.s(RING_R + 2), t.accent_soft, 40, f.s(3));
    f.dl->AddCircle(f.p(cx, cy), f.s(RING_R), on ? t.accent : t.rule2, 40, f.s(1));
    icon(f, ic, cx, cy, 18, on ? t.bone : dim ? t.dim : t.ash, mirror);
}

} // namespace

void focus_band(const Frame& f, const ui::Theme& t, float x0, float y0, float x1, float y1)
{
    float xm = x0 + (x1 - x0) * 0.85f;
    f.dl->AddRectFilledMultiColor(f.p(x0, y0), f.p(xm, y1), t.accent_soft, t.accent_soft & 0x00FFFFFFu,
        t.accent_soft & 0x00FFFFFFu, t.accent_soft);
    f.dl->AddRectFilled(f.p(x0, y0 + 8), f.p(x0 + 2, y1 - 8), t.accent);
}

void label(const Frame& f, const ui::Theme& t, float x, float y, const char* s, unsigned col)
{
    text(f, fonts.label, t.label_size, x, y, col ? col : t.ash, upper(s).c_str(), 0.26f);
}

void star(const Frame& f, float cx, float cy, float r, unsigned col)
{
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + i * 0.62831853f, rr = i % 2 ? r * 0.42f : r;
        pts[i] = f.p(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
    }
    f.dl->AddConcavePolyFilled(pts, 10, col);
}

namespace {

int stat(const pc_ui_item& it, int k) { return k == 0 ? it.str : k == 1 ? it.intel : it.agl; }
bool has(const pc_ui_item* it) { return it && it->inventory >= 0; }
// What the equip page shows (MAINMENU vs_mainMenu_renderEquipStats, EQP
// column): weapon, shield and armor STR/INT/AGL are attack (weapon) or
// defense values on top of Ashley's own (the record's, plus the accessory's,
// which is the only piece that adds to her stats, as Status shows them);
// they are not summed into her stats. Value of `it` on `slot` (nothing worn:
// Ashley's own).
int shown(const pc_ui_equipment& e, int slot, const pc_ui_item* it, int k)
{
    int v = k == 0 ? e.str : k == 1 ? e.intel : e.agl;
    if (slot != PC_UI_SLOT_ACCESSORY && has(&e.equipped[PC_UI_SLOT_ACCESSORY]))
        v += stat(e.equipped[PC_UI_SLOT_ACCESSORY], k);
    return has(it) ? v + stat(*it, k) : v;
}
// What a value changes by with `cand` on `slot` (nothing: taken off).
int change(const pc_ui_equipment& e, int slot, const pc_ui_item* cand, int k)
{
    return shown(e, slot, cand, k) - shown(e, slot, &e.equipped[slot], k);
}
// Row labels per slot, as the equip page's ("ATTACK/STR", "DEFENSE/STR", Ashley's for the accessory).
const char* stat_label(int slot, int k)
{
    static const char* const attack[3] = {"ATK STR", "ATK INT", "AGL"};
    static const char* const defense[3] = {"DEF STR", "DEF INT", "AGL"};
    return slot == PC_UI_SLOT_WEAPON ? attack[k] : slot == PC_UI_SLOT_ACCESSORY ? STATS[k] : defense[k];
}

} // namespace

float delta(const Frame& f, const ui::Theme& t, float x, float y, int d, float size)
{
    char buf[16];
    if (d > 0)
        std::snprintf(buf, sizeof buf, "+%d", d);
    else if (d < 0)
        std::snprintf(buf, sizeof buf, "%d", d);
    else
        std::snprintf(buf, sizeof buf, "\xc2\xb7");
    return text_right(f, fonts.data, size, x, y, d > 0 ? t.up : d < 0 ? t.down : t.dim, buf);
}
std::string num(int v)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", v);
    return buf;
}
std::string signed_num(int v)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, v > 0 ? "+%d" : "%d", v);
    return buf;
}

std::string kind_line(const pc_ui_item& it)
{
    std::string s;
    auto add = [&](const char* part) {
        if (!part || !*part)
            return;
        if (!s.empty())
            s += DOT;
        s += part;
    };
    if (it.damage_type >= 0 && it.damage_type < 3)
        add(DAMAGE[it.damage_type]);
    add(it.material);
    if (it.two_handed)
        add("Two-handed");
    return s;
}

float paragraph(const Frame& f, const ui::Theme& t, float x, float y, float max_w, unsigned col, const char* str,
    float size)
{
    ImFont* body = fonts.name ? fonts.name : ImGui::GetFont();
    float px = f.s(size > 0 ? size : t.body_size);
    f.dl->AddText(body, px, f.p(x, y), col, str, nullptr, f.s(max_w));
    return body->CalcTextSizeA(px, 1e9f, f.s(max_w), str).y / f.k;
}

namespace {

// A sentence of words and prompt icons, wrapped; returns its height.
struct Piece {
    std::string word; // empty: icon
    int button;
};
float flow(const Frame& f, float x, float y, float max_w, unsigned col, float size, const std::vector<Piece>& pieces)
{
    ImFont* body = fonts.name ? fonts.name : ImGui::GetFont();
    float line = size * 1.45f, cx = x, cy = y, space = measure(f, body, size, " ");
    for (const Piece& p : pieces) {
        float w = p.word.empty() ? glyph(f, p.button, size * 1.35f, 0, 0, true) : measure(f, body, size, p.word.c_str());
        if (cx > x && cx + w > x + max_w)
            cx = x, cy += line;
        if (p.word.empty())
            glyph(f, p.button, size * 1.35f, cx, cy + size * 0.62f);
        else
            text(f, body, size, cx, cy, col, p.word.c_str());
        cx += w + (p.word.empty() ? 4 : space);
    }
    return cy + line - y;
}
void words(std::vector<Piece>& out, const char* s)
{
    std::string w;
    for (const char* c = s;; ++c) {
        if (*c == ' ' || !*c) {
            if (!w.empty())
                out.push_back({w, 0});
            w.clear();
            if (!*c)
                break;
        } else {
            w += *c;
        }
    }
}

Card card_box(const Frame& f, const ui::Theme& t, float top)
{
    float w = f.W < 1100 ? 344.0f : 396.0f;
    return {f.W - t.margin_x - w, top, w};
}
} // namespace

void card_begin(const Frame& f)
{
    f.dl->ChannelsSplit(2);
    f.dl->ChannelsSetCurrent(1);
}
void card_end(const Frame& f, const ui::Theme& t, const Card& c, float bottom)
{
    f.dl->ChannelsSetCurrent(0);
    f.dl->AddRectFilled(f.p(c.x, c.y), f.p(c.x + c.w, bottom), t.panel);
    f.dl->AddRect(f.p(c.x, c.y), f.p(c.x + c.w, bottom), t.rule, 0.0f, f.s(1));
    f.dl->ChannelsMerge();
}

namespace {

// ---- Animation state (layout units) ----
struct Anim {
    float gear_y = 0, list_y = 0, scroll = 0, scroll_target = 0, quick_x = 0, quick_y = 0;
    int mode = -1, sub = -1;
} anim;

// Sub-tabs Gear / Quick swap with the L2 / R2 bumpers; returns the y below them.
float subtabs(const Frame& f, const ui::Theme& t, float x0, float x1, float y, int sub, Pointer& m)
{
    const char* names[2] = {"GEAR", "QUICK SWAP"};
    float x = x0, size = 12;
    for (int i = 0; i < 2; ++i) {
        float w = measure(f, fonts.label, size, names[i], 0.26f);
        bool over = m.hit(PC_UI_MOUSE_SUB, i, x - 6, y - 8, x + w + 6, y + 23);
        text(f, fonts.label, size, x, y, i == sub ? t.bone : over ? t.ash : t.dim, names[i], 0.26f);
        if (i == sub)
            f.dl->AddRectFilled(f.p(x, y + 21), f.p(x + w, y + 23), t.accent);
        x += w + 26;
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

// ---- Gear: the eight slots ----
void gear_rows(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_equipment& e,
    const pc_ui_equip_view& v, float dt, bool fresh, Pointer& m)
{
    float target = y0 + v.slot * GEAR_H;
    anim.gear_y = fresh ? target : approach(anim.gear_y, target, dt, t.focus_grow);
    focus_band(f, t, x0, anim.gear_y, x1, anim.gear_y + GEAR_H);
    for (int i = 0; i < PC_UI_EQUIP_SLOTS; ++i) {
        float ry = y0 + i * GEAR_H, cy = ry + GEAR_H * 0.5f;
        const pc_ui_item& it = e.equipped[i];
        bool on = i == v.slot, worn = has(&it);
        m.hit(PC_UI_MOUSE_SLOT, i, x0, ry, x1, ry + GEAR_H);
        ring(f, t, SLOT_ICONS[i], x0 + 14 + RING_R, cy, on, i == PC_UI_SLOT_LARM);
        float tx = x0 + 14 + 2 * RING_R + 14, right = x1 - 12;
        // DP (accessories have none)
        float dp_w = 72, avail_right = right;
        if (worn && it.dp_max > 0) {
            float bx = right - dp_w, by = ry + 18;
            f.dl->AddRectFilled(f.p(bx, by), f.p(right, by + 3), t.rule);
            float frac = it.dp_max > 0 ? (float)it.dp / it.dp_max : 0;
            frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
            f.dl->AddRectFilled(f.p(bx, by), f.p(bx + dp_w * frac, by + 3), t.hp);
            std::string dp = "DP " + num(it.dp);
            text_right(f, fonts.data, 12, right, ry + 25, t.ash, dp.c_str());
            avail_right = bx - 14;
        }
        // quick-swap badge: the equipped weapon is on a field swap button
        if (i == PC_UI_SLOT_WEAPON && worn && it.quick >= 1 && it.quick <= PC_UI_QUICK) {
            float gw = glyph(f, QUICK_BUTTONS[it.quick - 1], 18, 0, 0, true);
            if (gw > 0) {
                glyph(f, QUICK_BUTTONS[it.quick - 1], 18, avail_right - gw, cy);
                avail_right -= gw + 10;
            }
        }
        text(f, fonts.label, 10, tx, ry + 7, t.dim, upper(SLOT_LABELS[i]).c_str(), 0.24f);
        std::string name = fit(f, fonts.name, t.name_size, worn ? it.name : "Empty", avail_right - tx);
        text(f, fonts.name, t.name_size, tx, ry + 19, worn ? t.bone : t.dim, name.c_str());
        f.dl->AddLine(f.p(x0, ry + GEAR_H), f.p(x1, ry + GEAR_H), t.rule, f.s(1));
    }
}

// ---- Pick lists (gear candidates, quick weapon / shield) ----
void pick_list(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_equipment& e,
    const pc_ui_equip_view& v, float dt, bool reset, Pointer& m)
{
    float y1 = BODY_BOTTOM;
    if (v.loading) {
        spinner(f, x0 + 22, y0 + 26, 6, t.ash);
        text(f, fonts.label, t.label_size, x0 + 38, y0 + 20, t.ash, "LOADING", 0.26f);
        return;
    }
    int n = v.rows;
    if (n == 0) {
        paragraph(f, t, x0 + 14, y0 + 16, x1 - x0 - 28, t.dim, "Nothing in the bag goes here.");
        return;
    }
    int vis = (int)((y1 - y0) / PICK_H);
    if (vis < 1)
        vis = 1;
    int cursor = v.cursor < 0 ? 0 : v.cursor;
    // keep the focused row in view
    float st = anim.scroll_target;
    if (reset)
        st = 0;
    if (cursor < st)
        st = (float)cursor;
    if (cursor > st + vis - 1)
        st = (float)(cursor - vis + 1);
    float max_scroll = (float)(n > vis ? n - vis : 0);
    st = st < 0 ? 0 : st > max_scroll ? max_scroll : st;
    anim.scroll_target = st;
    anim.scroll = reset ? st : approach(anim.scroll, st, dt, t.focus_grow);
    float target = y0 + (cursor - anim.scroll) * PICK_H;
    anim.list_y = reset ? target : approach(anim.list_y, target, dt, t.focus_grow);
    // the band follows the scrolled row: it is placed relative to the scroll
    f.dl->PushClipRect(f.p(x0, y0), f.p(x1, y1), true);
    if (v.cursor >= 0)
        focus_band(f, t, x0, anim.list_y, x1, anim.list_y + PICK_H);
    bool quick_list = v.mode == PC_UI_EQ_QUICK_WEAPON || v.mode == PC_UI_EQ_QUICK_SHIELD;
    int slot = v.mode == PC_UI_EQ_PICK ? v.slot : v.mode == PC_UI_EQ_QUICK_WEAPON ? PC_UI_SLOT_WEAPON : PC_UI_SLOT_SHIELD;
    const pc_ui_item& cur = e.equipped[slot];
    for (int r = 0; r < n; ++r) {
        float ry = y0 + (r - anim.scroll) * PICK_H;
        if (ry + PICK_H < y0 || ry > y1)
            continue;
        float cy = ry + PICK_H * 0.5f;
        const pc_ui_item* it = pc_ui_shell_list_item(r);
        bool on = r == v.cursor;
        float hy0 = ry < y0 ? y0 : ry, hy1 = ry + PICK_H > y1 ? y1 : ry + PICK_H;
        if (hy1 > hy0)
            m.hit(PC_UI_MOUSE_LIST, r, x0, hy0, x1, hy1);
        ring(f, t, SLOT_ICONS[slot], x0 + 14 + RING_R, cy, on, slot == PC_UI_SLOT_LARM, !it);
        float tx = x0 + 14 + 2 * RING_R + 14, right = x1 - 12;
        if (!it) { // quick shield step: "No shield" first
            text(f, fonts.name, t.name_size, tx, ry + 8, on ? t.ash : t.dim, "No shield");
            text(f, fonts.data, 12, tx, ry + 32, t.dim, "The swap takes the shield off");
            f.dl->AddLine(f.p(x0, ry + PICK_H), f.p(x1, ry + PICK_H), t.rule, f.s(1));
            continue;
        }
        bool equipped = it->inventory == cur.inventory && cur.inventory >= 0;
        float name_right = right;
        // STR / INT / AGL against what is on the slot now (gear pick only)
        if (v.mode == PC_UI_EQ_PICK && !equipped) {
            float x = right;
            for (int k = 2; k >= 0; --k) {
                x -= delta(f, t, x, ry + 9, change(e, slot, it, k));
                x -= 4;
                x -= text_right(f, fonts.label, 10, x, ry + 11, t.dim, STATS[k], 0.1f);
                x -= 10;
            }
            name_right = x;
        }
        // a weapon on a field swap button: its icon (quick list: when it is another button)
        if (it->quick >= 1 && it->quick <= PC_UI_QUICK && !(quick_list && it->quick - 1 == v.quick)) {
            float gw = glyph(f, QUICK_BUTTONS[it->quick - 1], 18, 0, 0, true);
            if (gw > 0) {
                float gx = v.mode == PC_UI_EQ_PICK && !equipped ? name_right - gw : right - gw;
                glyph(f, QUICK_BUTTONS[it->quick - 1], 18, gx, v.mode == PC_UI_EQ_PICK && !equipped ? ry + 17 : cy);
                name_right = gx - 10;
            }
        }
        std::string name = fit(f, fonts.name, t.name_size, it->name, name_right - tx);
        text(f, fonts.name, t.name_size, tx, ry + 8, t.bone, name.c_str());
        std::string sub = kind_line(*it);
        float sx = tx;
        if (!sub.empty())
            sx += text(f, fonts.data, 12, sx, ry + 32, t.ash, fit(f, fonts.data, 12, sub.c_str(), right - tx - 80).c_str());
        if (equipped) {
            if (!sub.empty())
                sx += text(f, fonts.data, 12, sx, ry + 32, t.ash, DOT);
            text(f, fonts.data, 12, sx, ry + 32, t.gold, "Equipped");
        }
        f.dl->AddLine(f.p(x0, ry + PICK_H), f.p(x1, ry + PICK_H), t.rule, f.s(1));
    }
    f.dl->PopClipRect();
    if (n > vis) { // scroll position
        float track = y1 - y0, thumb = track * vis / n, ty = y0 + (track - thumb) * (anim.scroll / max_scroll);
        f.dl->AddRectFilled(f.p(x1 + 6, y0), f.p(x1 + 8, y1), t.rule);
        f.dl->AddRectFilled(f.p(x1 + 6, ty), f.p(x1 + 8, ty + thumb), t.ash);
    }
}

} // namespace

float bars(const Frame& f, const ui::Theme& t, float x, float xr, float y, const char* const* names, const int* now,
    const int* after, int count, bool same, unsigned col)
{
    int range = 10;
    bool negative = false;
    for (int i = 0; i < count; ++i) {
        int a = std::abs(after[i]), b = std::abs(now[i]);
        range = a > range ? a : range;
        range = b > range ? b : range;
        negative = negative || after[i] < 0 || now[i] < 0;
    }
    range = (range + 9) / 10 * 10;
    float tx0 = x + 74, tx1 = xr - 46, row = 18;
    auto at = [&](int v) {
        float frac = negative ? 0.5f + 0.5f * v / range : (float)v / range;
        return tx0 + (tx1 - tx0) * (frac < 0 ? 0 : frac > 1 ? 1 : frac);
    };
    for (int i = 0; i < count; ++i) {
        float ry = y + i * row, cy = ry + 7.5f;
        text(f, fonts.data, 11.5f, x, ry, t.ash, names[i]);
        f.dl->AddRectFilled(f.p(tx0, cy - 2), f.p(tx1, cy + 2), t.rule);
        float zero = at(0), end = at(after[i]);
        f.dl->AddRectFilled(f.p(zero < end ? zero : end, cy - 2), f.p(zero < end ? end : zero, cy + 2), col);
        if (negative)
            f.dl->AddRectFilled(f.p(zero - 0.5f, cy - 3), f.p(zero + 0.5f, cy + 3), t.rule2);
        if (same) {
            text_right(f, fonts.data, 11.5f, xr, ry, t.ash, num(after[i]).c_str());
        } else {
            float g = at(now[i]);
            f.dl->AddRectFilled(f.p(g - 0.5f, cy - 4), f.p(g + 0.5f, cy + 4), t.ash);
            delta(f, t, xr, ry, after[i] - now[i], 11.5f);
        }
    }
    return count * row;
}

namespace {

// The compare card: what the slot has (gear) or what the focused candidate would change (pick).
void compare_card(const Frame& f, const ui::Theme& t, float top, const pc_ui_equipment& e, const pc_ui_equip_view& v)
{
    int slot = v.slot;
    bool picking = v.mode == PC_UI_EQ_PICK;
    const pc_ui_item& cur = e.equipped[slot];
    const pc_ui_item* cand = &cur;
    if (picking && !v.loading && v.cursor >= 0)
        cand = pc_ui_shell_list_item(v.cursor);
    bool same = !picking || v.loading || !cand || cand->inventory == cur.inventory;
    if (!cand)
        cand = &cur;
    Card c = card_box(f, t, top);
    float x = c.x + 20, xr = c.x + c.w - 20, w = xr - x, y = top + 18;
    card_begin(f);
    std::string head = SLOT_LABELS[slot];
    if (!same)
        head += DOT, head += "compare";
    label(f, t, x, y, head.c_str());
    if (e.busy) // a model load runs
        spinner(f, xr - 6, y + 6, 5, t.gold);
    y += 13 + 8;
    bool worn = has(cand);
    std::string name = fit(f, fonts.name, 25, worn ? cand->name : "Nothing equipped", w);
    text(f, fonts.name, 25, x, y, worn ? t.bone : t.dim, name.c_str());
    y += 31 + 8;
    // meta: kind, material, two-handed, what it replaces
    if (worn) {
        float mx = x;
        std::string kind = kind_line(*cand);
        if (!kind.empty())
            mx += text(f, fonts.label, t.label_size, mx, y, t.ash, upper(kind.c_str()).c_str(), 0.2f) + 18;
        if (!same && has(&cur)) {
            std::string vs = fit(f, fonts.label, t.label_size, upper(("vs " + std::string(cur.name)).c_str()).c_str(),
                xr - mx, 0.2f);
            text(f, fonts.label, t.label_size, mx, y, t.dim, vs.c_str(), 0.2f);
        }
        y += 13 + 14;
    }
    // Stat / Now / After / delta
    float c4 = xr, c3 = c4 - 54, c2 = c3 - 64;
    text(f, fonts.label, 10, x, y, t.dim, "STAT", 0.22f);
    if (!same)
        text_right(f, fonts.label, 10, c2, y, t.dim, "NOW", 0.22f);
    text_right(f, fonts.label, 10, c3, y, t.dim, same ? "TOTAL" : "AFTER", 0.22f);
    y += 12 + 8;
    const float row = 23;
    for (int k = 0; k < 3; ++k) {
        int now = shown(e, slot, &cur, k), after = same ? now : shown(e, slot, cand, k);
        text(f, fonts.data, 13, x, y, t.ash, stat_label(slot, k));
        if (!same)
            text_right(f, fonts.data, 13, c2, y, t.dim, num(now).c_str());
        text_right(f, fonts.data, 13, c3, y, t.bone, num(after).c_str());
        if (!same)
            delta(f, t, c4, y, after - now);
        y += row;
    }
    if ((worn && cand->dp_max > 0) || (!same && cur.dp_max > 0)) {
        int now = has(&cur) ? cur.dp : 0, after = worn ? cand->dp : 0;
        text(f, fonts.data, 13, x, y, t.ash, "DP");
        if (!same)
            text_right(f, fonts.data, 13, c2, y, t.dim, num(now).c_str());
        text_right(f, fonts.data, 13, c3, y, t.bone, num(after).c_str());
        if (!same)
            delta(f, t, c4, y, after - now);
        y += row;
    }
    // class and affinity (nothing on the slot and nothing to compare: none)
    if (worn || !same) {
        static const int zero[8] = {};
        const int* now_c = has(&cur) ? cur.classes : zero;
        const int* now_a = has(&cur) ? cur.affinities : zero;
        const int* new_c = worn ? cand->classes : zero;
        const int* new_a = worn ? cand->affinities : zero;
        y += 10;
        label(f, t, x, y, "Class");
        y += 13 + 8;
        y += bars(f, t, x, xr, y, CLASSES, now_c, new_c, PC_UI_CLASSES, same, t.gold);
        y += 10;
        label(f, t, x, y, "Affinity");
        y += 13 + 8;
        y += bars(f, t, x, xr, y, AFFINITIES, now_a, new_a, PC_UI_AFFINITIES, same, t.mp);
    }
    // what else the equip takes off (MENU3: a two-handed weapon and a shield exclude each other)
    const char* off = nullptr;
    if (picking && !same && slot == PC_UI_SLOT_WEAPON && cand->two_handed && has(&e.equipped[PC_UI_SLOT_SHIELD]))
        off = "Two-handed: the shield comes off.";
    else if (picking && !same && slot == PC_UI_SLOT_SHIELD && has(cand) && e.equipped[PC_UI_SLOT_WEAPON].two_handed
             && has(&e.equipped[PC_UI_SLOT_WEAPON]))
        off = "Two-handed weapon comes off.";
    if (off) {
        y += 6;
        f.dl->AddRectFilled(f.p(x, y + 1), f.p(x + 2, y + t.body_size + 3), t.gold);
        y += paragraph(f, t, x + 10, y, w - 10, t.gold, off, 13.5f);
    }
    card_end(f, t, c, y + 14);
}

// ---- Quick swap ----
// The weapon item for a quick slot, from the snapshot's weapon list (the
// overview asks for every weapon), or null.
const pc_ui_item* quick_weapon_item(const pc_ui_equipment& e, int q)
{
    int inv = e.quick_weapon[q];
    if (inv < 0 || e.slot != PC_UI_SLOT_WEAPON)
        return nullptr;
    int n = e.count < 0 ? 0 : e.count > PC_UI_CANDIDATES ? PC_UI_CANDIDATES : e.count;
    for (int i = 0; i < n; ++i)
        if (e.candidates[i].inventory == inv)
            return &e.candidates[i];
    return nullptr;
}

void diamond(const Frame& f, const ui::Theme& t, float x0, float y0, int first, const pc_ui_equipment& e,
    const pc_ui_equip_view& v, Pointer& m)
{
    const float W = 440, H = 50, gap = 6, cw = (W - 2 * gap) / 3;
    unsigned stage = t.stage; // the ground as a panel (opaque at the theme's opacity; ui_menu_opacity)
    for (int i = first; i < first + 4; ++i) {
        float x = x0 + DIAMOND[i][0] * (cw + gap), y = y0 + DIAMOND[i][1] * (H + gap);
        bool on = v.mode == PC_UI_EQ_QUICK && v.quick == i, empty = e.quick_weapon[i] < 0;
        m.hit(PC_UI_MOUSE_QUICK, i, x, y, x + cw, y + H);
        f.dl->AddRectFilled(f.p(x, y), f.p(x + cw, y + H), stage);
        if (on)
            f.dl->AddRectFilledMultiColor(f.p(x, y), f.p(x + cw, y + H), t.accent_soft, t.accent_soft & 0x00FFFFFFu,
                t.accent_soft & 0x00FFFFFFu, t.accent_soft);
        f.dl->AddRect(f.p(x, y), f.p(x + cw, y + H), on ? t.accent : t.rule2, 0, 0, f.s(1));
        float gw = glyph(f, QUICK_BUTTONS[i], 22, x + 10, y + H * 0.5f);
        float tx = x + 10 + gw + 9, tw = x + cw - 14 - tx;
        const pc_ui_item* w = quick_weapon_item(e, i);
        std::string name = empty ? "Empty" : e.quick_weapon_name[i];
        std::string sub = empty ? "Choose a weapon"
            : w && w->two_handed ? "Two-handed"
            : e.quick_shield[i] >= 0 ? e.quick_shield_name[i]
                                      : "No shield";
        text(f, fonts.name, 15.5f, tx, y + 7, empty ? t.dim : t.bone, fit(f, fonts.name, 15.5f, name.c_str(), tw).c_str());
        text(f, fonts.data, 11, tx, y + 29, empty ? t.dim : t.ash, fit(f, fonts.data, 11, sub.c_str(), tw).c_str());
        if (!empty && e.quick_weapon[i] == e.equipped[PC_UI_SLOT_WEAPON].inventory)
            f.dl->AddCircleFilled(f.p(x + cw - 10, y + 10), f.s(3), t.gold);
    }
}

void quick_card(const Frame& f, const ui::Theme& t, float top, const pc_ui_equipment& e, const pc_ui_equip_view& v)
{
    int q = v.quick;
    const pc_ui_item* show = nullptr;
    std::string shield_line;
    bool picking_shield = v.mode == PC_UI_EQ_QUICK_SHIELD;
    if (picking_shield) {
        show = v.pending;
        if (!v.loading && v.cursor >= 0) {
            const pc_ui_item* s = pc_ui_shell_list_item(v.cursor);
            shield_line = s ? "With " + std::string(s->name) : "No shield";
        }
    } else if (v.mode == PC_UI_EQ_QUICK_WEAPON) {
        if (!v.loading && v.cursor >= 0)
            show = pc_ui_shell_list_item(v.cursor);
    } else {
        show = quick_weapon_item(e, q);
        shield_line = e.quick_shield[q] >= 0 ? "With " + std::string(e.quick_shield_name[q]) : "No shield";
    }
    Card c = card_box(f, t, top);
    float x = c.x + 20, xr = c.x + c.w - 20, w = xr - x, y = top + 18;
    card_begin(f);
    float lx = x + text(f, fonts.label, t.label_size, x, y, t.ash, "QUICK SWAP", 0.26f) + 8;
    lx += glyph(f, QUICK_BUTTONS[q], 18, lx, y + 6) + 6;
    text(f, fonts.label, t.label_size, lx, y, t.ash, upper(QUICK_NAMES[q]).c_str(), 0.26f);
    if (e.busy)
        spinner(f, xr - 6, y + 6, 5, t.gold);
    y += 13 + 10;
    // the overview without its weapon list yet: the name from the slot
    std::string name = show ? show->name : e.quick_weapon[q] >= 0 && v.mode == PC_UI_EQ_QUICK ? e.quick_weapon_name[q] : "Empty";
    bool any = show || (v.mode == PC_UI_EQ_QUICK && e.quick_weapon[q] >= 0);
    text(f, fonts.name, 25, x, y, any ? t.bone : t.dim, fit(f, fonts.name, 25, name.c_str(), w).c_str());
    y += 31 + 8;
    if (any) {
        std::string meta = show ? kind_line(*show) : "";
        std::string hands = show && show->two_handed ? "" : picking_shield ? shield_line : v.mode == PC_UI_EQ_QUICK ? shield_line : "One-handed";
        if (!hands.empty())
            meta += (meta.empty() ? "" : DOT) + hands;
        text(f, fonts.label, t.label_size, x, y, t.ash, fit(f, fonts.label, t.label_size, upper(meta.c_str()).c_str(), w, 0.2f).c_str(), 0.2f);
        y += 13 + 14;
        if (show) {
            for (int k = 0; k < 4; ++k) {
                text(f, fonts.data, 13, x, y, t.ash, k < 3 ? STATS[k] : "DP");
                std::string val = k < 3 ? signed_num(stat(*show, k)) : num(show->dp);
                text_right(f, fonts.data, 13, xr - 54, y, t.bone, val.c_str()); // the compare card's column
                y += 23;
            }
        }
    } else {
        y += paragraph(f, t, x, y, w, t.ash,
            "No weapon on this button yet. Any weapon in the bag can go here; it does not need to be equipped.");
    }
    y += 12;
    std::vector<Piece> s;
    words(s, "In the field: hold");
    s.push_back({"", PC_PROMPT_L2});
    s.push_back({"", PC_PROMPT_R2});
    words(s, "and press");
    s.push_back({"", QUICK_BUTTONS[q]});
    words(s, "to swap.");
    y += flow(f, x, y, w, t.ash, 13.5f, s);
    if (!e.quick_enabled) {
        // assigning still works (the slot bytes are the unmodded game's too); the field controls are off
        // (System > Gameplay comes with the System screen: until then the editor holds the option)
        y += 10;
        f.dl->AddRectFilled(f.p(x, y + 1), f.p(x + 2, y + t.body_size + 3), t.gold);
        y += paragraph(f, t, x + 10, y, w - 10, t.gold,
            "Quick weapon slots are off: turn them on in the editor (Graphics \xe2\x80\xba Gameplay, "
            "settings key qol_weapon_slots) to swap in the field.",
            13.5f);
    }
    card_end(f, t, c, y + 14);
}

} // namespace

void draw_equipment(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    pc_ui_equip_view v;
    pc_ui_shell_equip_view(&v);
    const pc_ui_equipment& e = *pc_ui_shell_equipment();
    float x0 = t.margin_x, x1 = x0 + RAIL;
    bool reset = fresh || anim.mode != v.mode || anim.sub != v.sub;
    float y = subtabs(f, t, x0, x1, top, v.sub, m);
    out.busy = e.busy != 0;
    if (!e.valid) {
        spinner(f, x0 + 22, y + 30, 6, t.ash);
        text(f, fonts.label, t.label_size, x0 + 38, y + 24, t.ash, "LOADING", 0.26f);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
        out.crumb = "Equipment";
        anim.mode = v.mode, anim.sub = v.sub;
        return;
    }
    switch (v.mode) {
    case PC_UI_EQ_GEAR:
    case PC_UI_EQ_PICK: {
        bool pick = v.mode == PC_UI_EQ_PICK;
        std::string head = pick ? "Choose" + std::string(DOT) + SLOT_LABELS[v.slot] : "Equipped";
        label(f, t, x0, y + 10, head.c_str());
        float ly = y + 10 + 13 + 4;
        if (pick)
            pick_list(f, t, x0, x1, ly, e, v, dt, reset, m);
        else
            gear_rows(f, t, x0, x1, ly, e, v, dt, reset, m);
        if (!pick && v.locked && v.reason) {
            float ry = ly + PC_UI_EQUIP_SLOTS * GEAR_H + 16;
            f.dl->AddRectFilled(f.p(x0 + 14, ry + 1), f.p(x0 + 16, ry + t.body_size + 3), t.gold);
            paragraph(f, t, x0 + 24, ry, RAIL - 38, t.gold, v.reason);
        }
        compare_card(f, t, top, e, v);
        out.crumb = pick ? "Equipment" + std::string(NEXT) + SLOT_LABELS[v.slot] : "Equipment";
        if (pick) {
            if (!v.locked)
                out.hint(PC_PROMPT_CIRCLE, -1, "Equip", PC_UI_CONFIRM);
            out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        } else {
            if (!v.locked) {
                out.hint(PC_PROMPT_CIRCLE, -1, "Change", PC_UI_CONFIRM);
                if (has(&e.equipped[v.slot]))
                    out.hint(PC_PROMPT_SQUARE, -1, "Remove", PC_UI_ALT);
            }
            out.hint(PC_PROMPT_L2, PC_PROMPT_R2, "Quick swap", PC_UI_SUB_NEXT);
            out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
        }
        break;
    }
    case PC_UI_EQ_QUICK: {
        label(f, t, x0, y + 12, "Face buttons");
        diamond(f, t, x0, y + 12 + 13 + 8, 0, e, v, m);
        float dy = y + 12 + 13 + 8 + 3 * 50 + 2 * 6 + 14;
        label(f, t, x0, dy, "D-pad");
        diamond(f, t, x0, dy + 13 + 8, 4, e, v, m);
        quick_card(f, t, top, e, v);
        out.crumb = "Equipment" + std::string(NEXT) + "Quick swap";
        out.hint(PC_PROMPT_CIRCLE, -1, "Set", PC_UI_CONFIRM);
        if (e.quick_weapon[v.quick] >= 0)
            out.hint(PC_PROMPT_SQUARE, -1, "Clear", PC_UI_ALT);
        out.hint(PC_PROMPT_L2, PC_PROMPT_R2, "Gear", PC_UI_SUB_NEXT);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
        break;
    }
    default: { // quick weapon / shield lists
        bool shield = v.mode == PC_UI_EQ_QUICK_SHIELD;
        float lx = x0;
        float hy = y + 10;
        if (shield && v.pending) {
            std::string head = upper(("Shield with " + std::string(v.pending->name)).c_str());
            head = fit(f, fonts.label, t.label_size, head.c_str(), RAIL - 40, 0.26f);
            lx += text(f, fonts.label, t.label_size, lx, hy, t.ash, head.c_str(), 0.26f);
        } else {
            lx += text(f, fonts.label, t.label_size, lx, hy, t.ash, shield ? "SHIELD FOR" : "WEAPON FOR", 0.26f);
        }
        glyph(f, QUICK_BUTTONS[v.quick], 16, lx + 8, hy + 6);
        pick_list(f, t, x0, x1, hy + 13 + 6, e, v, dt, reset, m);
        quick_card(f, t, top, e, v);
        out.crumb = "Equipment" + std::string(NEXT) + "Quick swap" + NEXT + QUICK_NAMES[v.quick];
        out.hint(PC_PROMPT_CIRCLE, -1, shield ? "Save" : "Choose", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    }
    }
    anim.mode = v.mode, anim.sub = v.sub;
}

} // namespace uidraw
