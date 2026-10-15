// Modern menus: the Workshop, the Items tab's last sub-tab (ui_workshop.h).
//
// Below the Items chips (the WORKSHOP chip on): the left column holds the
// six commands (greyed with the original's reason under the focused one), a
// line saying which workshop this is, or that this is no workshop. The
// middle column is the open flow: its list (records as the Items list draws
// them: a ring with the line icon, name, material and kind, EQUIPPED / IN A
// SET; greyed ones say why when focused), the slot menus (Blade, Grip, gem
// slots, Next; First, Second, Combine), the name field over the game's
// character grid, the Yes / No with the game's question (Repair: every
// record's DP and PP now and after). The right card previews what the flow
// makes (the weapon assembled, the result of a combine, the target with its
// gems as they will be), or, at the commands, the workshop's materials.
//
// State comes from ui_workshop.cpp (pc_ui_workshop_view_get, the bridge's
// snapshot); nothing here changes it except through the pointer.
#include "ui/ui_draw_internal.h"
#include "ui/ui_workshop.h"
#include "button_prompts.h"
#include "imgui_internal.h" // ImTextCharFromUtf8
#include <cstdio>
#include <cstring>
#include <string>

namespace uidraw {
namespace {

const char* const NEXT = " \xe2\x80\xba "; // " › "
const char* const ARROW = " \xe2\x86\x92 "; // " → "
const float BOTTOM = 720 - 64;
const float ROW_H = 48;
const float CMD_H = 40;
const float RING_R = 15;
const float MENU_ROW = 30;

const char* const LABELS[PC_UI_WS_ACTIONS] = {"Assemble", "Gems", "Disassemble", "Rename", "Repair", "Combine"};
const int HELP[PC_UI_WS_ACTIONS] = {2, 4, 6, 8, 10, 12}; // MENU12 *Desc
const char* const BLADE_KINDS[10] = {"", "Dagger", "Sword", "Great Sword", "Axe & Mace", "Great Axe", "Staff",
    "Heavy Mace", "Polearm", "Crossbow"};
const char* const TYPES[3] = {"Blunt", "Edged", "Piercing"};

struct Anim {
    float list_y = 0, scroll = 0;
    int mode = -1, flow = -1;
} anim;

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
std::string sentence(const char* s)
{
    std::string out = s ? s : "";
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i] >= 'A' && out[i] <= 'Z' && out[i - 1] != ' ')
            out[i] = (char)(out[i] - 'A' + 'a');
    return out;
}
// The game's lines join their screen lines with a space; a trailing "▼" (more to read) is dropped.
std::string line(const char* s)
{
    std::string o = s ? s : "";
    const char* more = "\xe2\x96\xbc";
    size_t p = o.find(more);
    if (p != std::string::npos)
        o.erase(p);
    while (!o.empty() && o.back() == ' ')
        o.pop_back();
    return o;
}

int category_of(int list)
{
    static const int C[PC_UI_WSL_LISTS] = {PC_UI_IC_WEAPON, PC_UI_IC_BLADE, PC_UI_IC_GRIP, PC_UI_IC_SHIELD,
        PC_UI_IC_ARMOR, PC_UI_IC_GEM};
    return list >= 0 && list < PC_UI_WSL_LISTS ? C[list] : PC_UI_IC_MISC;
}

// a record row as the Items list draws one (x0..x1, height ROW_H at y); returns nothing
void record_row(const Frame& f, const ui::Theme& t, float x0, float x1, float y, int list, const pc_ui_ws_row* r,
    const char* label, bool on, bool enabled, const char* tag, const char* sub_override = nullptr)
{
    float cy = y + ROW_H * 0.5f, rx = x0 + 12 + RING_R, right = x1 - 12;
    unsigned ring = on ? t.accent : t.rule2;
    if (on)
        f.dl->AddCircle(f.p(rx, cy), f.s(RING_R + 2), t.accent_soft, 36, f.s(3));
    f.dl->AddCircle(f.p(rx, cy), f.s(RING_R), ring, 36, f.s(1));
    unsigned ic = !enabled ? t.dim : on ? t.bone : t.ash;
    if (r)
        bag_icon(f, category_of(list), r->id, r->sub, r->name, rx, cy, 15, ic);
    else
        f.dl->AddCircle(f.p(rx, cy), f.s(3), ic, 12, f.s(1));
    if (tag && *tag) {
        float tw = measure(f, fonts.label, 10, tag, 0.22f);
        float bx = right - tw - 12;
        unsigned tc = !strcmp(tag, "EQUIPPED") ? t.gold : t.accent;
        f.dl->AddRect(f.p(bx, cy - 9), f.p(right, cy + 9), tc, 0, 0, f.s(1));
        text(f, fonts.label, 10, bx + 6, cy - 6, tc, tag, 0.22f);
        right = bx - 10;
    } else if (r && r->mark != PC_UI_MARK_NONE) {
        const char* mk = r->mark == PC_UI_MARK_EQUIPPED ? "EQUIPPED" : "IN A SET";
        float mw = text_right(f, fonts.label, 10, right, cy - 6, r->mark == PC_UI_MARK_EQUIPPED ? t.gold : t.ash, mk,
            0.22f);
        right -= mw + 12;
    }
    float nx = rx + RING_R + 12, nw = right - nx;
    const char* sub = sub_override ? sub_override : r ? r->sub : "";
    std::string s = sub ? sub : "";
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    unsigned nc = !enabled ? t.dim : on ? t.bone : t.ash;
    bool two = !s.empty();
    text(f, fonts.name, 17, nx, two ? y + 6 : cy - 11, nc, fit(f, fonts.name, 17, label, nw).c_str());
    if (two)
        text(f, fonts.data, 11, nx, y + 28, enabled ? (on ? t.ash : t.dim) : fade(t.dim, 0.8f),
            fit(f, fonts.data, 11, s.c_str(), nw).c_str());
    f.dl->AddLine(f.p(x0, y + ROW_H), f.p(x1, y + ROW_H), t.rule, f.s(1));
}


// A grid character drawn with the first of our fonts that has it; a few of
// the game's symbols none of them has stand in as their nearest look-alikes
// (the code typed stays the game's).
bool font_has(ImFont* font, const char* s)
{
    if (!font)
        return false;
    for (const char* p = s; *p;) {
        unsigned c = 0;
        int n = ImTextCharFromUtf8(&c, p, nullptr);
        ImFontGlyph* glyph = n > 0 ? font->GetFontBaked(16)->FindGlyphNoFallback((ImWchar)c) : nullptr;
        if (!glyph || (!glyph->Visible && c != ' '))
            return false;
        p += n;
    }
    return true;
}
void glyph_text(const Frame& f, float size, float x, float w, float y, unsigned col, const char* g)
{
    static const char* const SUBS[][3] = {{"\xe2\x80\xbc", "!!", "!!"}, {"\xe2\x8b\xaf", "\xe2\x80\xa6", "..."},
        {"\xe2\x89\xa6", "\xe2\x89\xa4", "<="}, {"\xe2\x89\xa7", "\xe2\x89\xa5", ">="}, {"\xe2\x89\xa0", "\xe2\x89\xa0", "!="},
        {"\xc3\xb7", "\xc3\xb7", "/"}, {"\xe2\x99\xaa", "\xe2\x99\xab", "~"}};
    std::string s = g;
    ImFont* order[3] = {fonts.name, fonts.data, fonts.label};
    for (int pass = 0; pass < 3; ++pass) {
        for (ImFont* font : order)
            if (font_has(font, s.c_str())) {
                float tw = measure(f, font, size, s.c_str());
                text(f, font, size, x + (w - tw) * 0.5f, y, col, s.c_str());
                return;
            }
        for (const auto& sub : SUBS)
            if (s == g && !std::strcmp(g, sub[0]))
                s = sub[1];
            else if (pass == 1 && !std::strcmp(g, sub[0]))
                s = sub[2];
    }
    float tw = measure(f, fonts.name, size, s.c_str());
    text(f, fonts.name, size, x + (w - tw) * 0.5f, y, col, s.c_str());
}

// ---- the left column: the commands ----
float commands(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_workshop& g,
    const pc_ui_workshop_view& v, Pointer& m)
{
    // which workshop
    std::string where = g.workshop ? "Workshop " + num(g.workshop) : std::string("No workshop here");
    label(f, t, x0, y, where.c_str(), g.workshop ? t.ash : t.gold);
    y += 13 + 12;
    bool at_rows = v.mode == PC_UI_WS_ROWS;
    for (int i = 0; i < PC_UI_WS_ACTIONS; ++i) {
        bool on = i == v.row, en = g.action_enabled[i] != 0;
        bool active = !at_rows && v.flow == i;
        bool over = m.hit(PC_UI_MOUSE_WS_ROW, i, x0, y, x1, y + CMD_H);
        if (on && at_rows)
            focus_band(f, t, x0, y, x1, y + CMD_H);
        else if (active)
            f.dl->AddRectFilled(f.p(x0, y), f.p(x0 + 3, y + CMD_H), t.accent);
        unsigned col = !en ? t.dim : (on && at_rows) || active ? t.bone : over ? t.bone : t.ash;
        text(f, fonts.name, 18, x0 + 16, y + 9, col, LABELS[i]);
        f.dl->AddLine(f.p(x0, y + CMD_H), f.p(x1, y + CMD_H), t.rule, f.s(1));
        y += CMD_H;
    }
    return y;
}

// ---- the middle column ----
const char* entry_tag(const pc_ui_ws_entry& e) { return e.current ? "HOLDS" : nullptr; }

void entries_list(const Frame& f, const ui::Theme& t, float x0, float x1, float y0, const pc_ui_workshop_view& v,
    float dt, bool reset, Pointer& m)
{
    int n = v.entries;
    // the focused greyed entry's reason takes room at the bottom
    const pc_ui_ws_entry* fe = v.cursor >= 0 && v.cursor < n ? &v.entry[v.cursor] : nullptr;
    const char* why = fe && !fe->enabled && fe->reason ? fe->reason : nullptr;
    std::string why_s = line(why);
    float why_h = why ? para_h(f, t, x1 - x0 - 18, why_s.c_str(), 13.5f) + 18 : 0;
    int vis = (int)((BOTTOM - why_h - y0) / ROW_H);
    vis = vis < 1 ? 1 : vis;
    float y1 = y0 + vis * ROW_H;
    int cur = v.cursor < 0 ? 0 : v.cursor >= n ? (n ? n - 1 : 0) : v.cursor;
    float st = reset ? 0 : anim.scroll;
    if (cur < st)
        st = (float)cur;
    if (cur > st + vis - 1)
        st = (float)(cur - vis + 1);
    float max_scroll = (float)(n > vis ? n - vis : 0);
    st = st < 0 ? 0 : st > max_scroll ? max_scroll : st;
    anim.scroll = st;
    float target = y0 + (cur - st) * ROW_H;
    anim.list_y = reset ? target : approach(anim.list_y, target, dt, t.focus_grow);
    f.dl->PushClipRect(f.p(x0, y0), f.p(x1 + 1, y1), true);
    if (n)
        focus_band(f, t, x0, anim.list_y, x1, anim.list_y + ROW_H);
    for (int i = 0; i < n; ++i) {
        float ry = y0 + (i - st) * ROW_H;
        if (ry + ROW_H <= y0 || ry >= y1)
            continue;
        const pc_ui_ws_entry& e = v.entry[i];
        m.hit(PC_UI_MOUSE_WS_ENTRY, i, x0, ry, x1, ry + ROW_H);
        std::string sub;
        const char* so = nullptr;
        if (e.category >= 0) { // a combine category
            sub = num(e.count) + (e.count == 1 ? " in the bag" : " in the bag");
            so = sub.c_str();
        } else if (!e.rec) { // a placeholder: the slot is empty
            so = "Nothing chosen";
        }
        record_row(f, t, x0, x1, ry, e.list, e.rec, e.label, i == cur, e.enabled != 0, entry_tag(e), so);
    }
    f.dl->PopClipRect();
    if (n > vis) {
        float track = y1 - y0, thumb = track * vis / n, ty = y0 + (track - thumb) * (st / max_scroll);
        f.dl->AddRectFilled(f.p(x1 + 6, y0), f.p(x1 + 8, y1), t.rule);
        f.dl->AddRectFilled(f.p(x1 + 6, ty), f.p(x1 + 8, ty + thumb), t.ash);
    }
    if (!n)
        paragraph(f, t, x0 + 14, y0 + 10, x1 - x0 - 28, t.dim, "Nothing here.");
    if (why) // under the last row shown
        note(f, t, x0 + 8, y0 + (n < vis ? n : vis) * ROW_H + 12, x1 - x0 - 16, why_s.c_str());
}

// a slot menu row: its name (Blade, Grip, Gem 1, ...) above what it holds
void slot_row(const Frame& f, const ui::Theme& t, float x0, float x1, float y, int i, const char* name, int list,
    const pc_ui_ws_row* r, bool on, Pointer& m)
{
    m.hit(PC_UI_MOUSE_WS_SLOT, i, x0, y, x1, y + ROW_H);
    if (on)
        focus_band(f, t, x0, y, x1, y + ROW_H);
    float cy = y + ROW_H * 0.5f, rx = x0 + 12 + RING_R;
    f.dl->AddCircle(f.p(rx, cy), f.s(RING_R), on ? t.accent : t.rule2, 36, f.s(1));
    if (r)
        bag_icon(f, category_of(list), r->id, r->sub, r->name, rx, cy, 15, on ? t.bone : t.ash);
    else
        f.dl->AddCircle(f.p(rx, cy), f.s(4), t.rule2, 16, f.s(1));
    float nx = rx + RING_R + 12, nw = x1 - 12 - nx;
    text(f, fonts.label, 10, nx, y + 6, t.dim, upper(name).c_str(), 0.22f);
    text(f, fonts.name, 17, nx, y + 20, r ? (on ? t.bone : t.ash) : t.dim,
        fit(f, fonts.name, 17, r ? r->name : "Choose", nw).c_str());
    f.dl->AddLine(f.p(x0, y + ROW_H), f.p(x1, y + ROW_H), t.rule, f.s(1));
}
// the finishing row (Next, Attach, Combine): a button
void finish_row(const Frame& f, const ui::Theme& t, float x0, float x1, float y, int i, const char* name, bool on,
    Pointer& m)
{
    m.hit(PC_UI_MOUSE_WS_SLOT, i, x0, y, x1, y + ROW_H);
    float bx0 = x0 + 12, bx1 = x1 - 12, by0 = y + 8, by1 = y + ROW_H - 6;
    f.dl->AddRectFilled(f.p(bx0, by0), f.p(bx1, by1), on ? t.accent_soft : 0);
    f.dl->AddRect(f.p(bx0, by0), f.p(bx1, by1), on ? t.accent : t.rule2, 0, 0, f.s(1));
    std::string s = upper(name);
    float w = measure(f, fonts.label, 13, s.c_str(), 0.26f);
    text(f, fonts.label, 13, (bx0 + bx1 - w) * 0.5f, by0 + 9, on ? t.bone : t.ash, s.c_str(), 0.26f);
}

float slots(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_workshop_view& v,
    Pointer& m)
{
    if (v.mode == PC_UI_WS_ASSEMBLE) {
        slot_row(f, t, x0, x1, y, 0, "Blade", PC_UI_WSL_BLADES, v.blade, v.cursor == 0, m);
        y += ROW_H;
        slot_row(f, t, x0, x1, y, 1, "Grip", PC_UI_WSL_GRIPS, v.grip, v.cursor == 1, m);
        y += ROW_H;
        if (v.finish) {
            for (int k = 0; k < v.gem_slots; ++k) {
                std::string n = "Gem " + num(k + 1);
                slot_row(f, t, x0, x1, y, 2 + k, n.c_str(), PC_UI_WSL_GEMS, v.gems[k], v.cursor == 2 + k, m);
                y += ROW_H;
            }
            finish_row(f, t, x0, x1, y, v.slots - 1, "Next: name it", v.cursor == v.slots - 1, m);
            y += ROW_H;
        }
    } else if (v.mode == PC_UI_WS_SLOTS) {
        for (int k = 0; k < v.gem_slots; ++k) {
            std::string n = "Gem slot " + num(k + 1);
            slot_row(f, t, x0, x1, y, k, n.c_str(), PC_UI_WSL_GEMS, v.gems[k], v.cursor == k, m);
            y += ROW_H;
        }
        finish_row(f, t, x0, x1, y, v.slots - 1, "Attach", v.cursor == v.slots - 1, m);
        y += ROW_H;
    } else {
        static const int L[3] = {PC_UI_WSL_BLADES, PC_UI_WSL_SHIELDS, PC_UI_WSL_ARMOR};
        int l = L[v.combine < 0 || v.combine > 2 ? 0 : v.combine];
        slot_row(f, t, x0, x1, y, 0, "First", l, v.first, v.cursor == 0, m);
        y += ROW_H;
        slot_row(f, t, x0, x1, y, 1, "Second", l, v.second, v.cursor == 1, m);
        y += ROW_H;
        if (v.finish) {
            finish_row(f, t, x0, x1, y, 2, "Combine", v.cursor == 2, m);
            y += ROW_H;
        }
    }
    return y;
}

// the name field and the character grid
void name_panel(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_workshop& g,
    const pc_ui_workshop_view& v, Pointer& m)
{
    float w = x1 - x0;
    // the field: 20 places
    float place = w / PC_UI_WS_NAME_LEN;
    place = place > 22 ? 22 : place;
    float fw = place * PC_UI_WS_NAME_LEN, fx = x0 + (w - fw) * 0.5f, fy = y;
    f.dl->AddRectFilled(f.p(fx - 8, fy), f.p(fx + fw + 8, fy + 40), t.panel);
    f.dl->AddRect(f.p(fx - 8, fy), f.p(fx + fw + 8, fy + 40), t.rule2, 0, 0, f.s(1));
    bool blank = true;
    for (int i = 0; i < PC_UI_WS_NAME_LEN; ++i) {
        unsigned char c = v.field[i];
        float px = fx + i * place;
        m.hit(PC_UI_MOUSE_WS_PLACE, i, px, fy, px + place, fy + 40);
        if (c != 0x8F) {
            blank = false;
            glyph_text(f, 20, px, place, fy + 8, t.bone, g.glyph[c]);
        }
        f.dl->AddLine(f.p(px + 3, fy + 34), f.p(px + place - 3, fy + 34), fade(t.rule2, 0.8f), f.s(1));
        if (i == v.name_cursor) // the caret: blinking
            f.dl->AddRectFilled(f.p(px + 1, fy + 7), f.p(px + 3, fy + 33),
                fade(t.accent, 0.55f + 0.45f * (float)((int)(ImGui::GetTime() * 2.5) & 1)));
    }
    if (blank && v.assembling)
        text(f, fonts.data, 11, fx, fy + 46, t.dim, "Left blank: the weapon takes its blade's name.");
    else if (blank)
        text(f, fonts.data, 11, fx, fy + 46, t.dim, "Left blank: the weapon takes its blade's name again.");
    y = fy + 66;
    // the grid
    float cell = w / PC_UI_WS_GRID_COLS;
    cell = cell > 30 ? 30 : cell;
    float gw = cell * PC_UI_WS_GRID_COLS, gx = x0 + (w - gw) * 0.5f, ch = 28;
    for (int r = 0; r < PC_UI_WS_GRID_ROWS; ++r)
        for (int c = 0; c < PC_UI_WS_GRID_COLS; ++c) {
            float cx0 = gx + c * cell, cy0 = y + r * ch;
            bool on = v.grid_row == r && v.grid_col == c;
            bool over = m.hit(PC_UI_MOUSE_WS_GRID, r * PC_UI_WS_GRID_COLS + c, cx0, cy0, cx0 + cell, cy0 + ch);
            unsigned char code = g.grid[r][c];
            if (on) {
                f.dl->AddRectFilled(f.p(cx0 + 1, cy0 + 1), f.p(cx0 + cell - 1, cy0 + ch - 1), t.accent_soft);
                f.dl->AddRect(f.p(cx0 + 1, cy0 + 1), f.p(cx0 + cell - 1, cy0 + ch - 1), t.accent, 0, 0, f.s(1));
            }
            if (code == 0x8F) { // a blank: a space
                f.dl->AddLine(f.p(cx0 + cell * 0.3f, cy0 + ch - 9), f.p(cx0 + cell * 0.7f, cy0 + ch - 9),
                    on ? t.bone : fade(t.dim, 0.6f), f.s(1));
                continue;
            }
            glyph_text(f, 17, cx0, cell, cy0 + 4, on || over ? t.bone : t.ash, g.glyph[code]);
        }
    y += PC_UI_WS_GRID_ROWS * ch + 10;
    // Delete and Done
    const char* acts[PC_UI_WSN_ACTIONS] = {"Delete", "Done"};
    float bw = (gw - 12) / 2;
    for (int i = 0; i < PC_UI_WSN_ACTIONS; ++i) {
        float bx0 = gx + i * (bw + 12), bx1 = bx0 + bw;
        bool on = v.grid_row == PC_UI_WS_GRID_ROWS && v.grid_col == i;
        m.hit(PC_UI_MOUSE_WS_GRID, PC_UI_WS_GRID_ROWS * PC_UI_WS_GRID_COLS + i, bx0, y, bx1, y + 32);
        f.dl->AddRectFilled(f.p(bx0, y), f.p(bx1, y + 32), on ? t.accent_soft : 0);
        f.dl->AddRect(f.p(bx0, y), f.p(bx1, y + 32), on ? t.accent : t.rule2, 0, 0, f.s(1));
        std::string s = upper(acts[i]);
        float tw = measure(f, fonts.label, 12, s.c_str(), 0.26f);
        text(f, fonts.label, 12, (bx0 + bx1 - tw) * 0.5f, y + 9, on ? t.bone : t.ash, s.c_str(), 0.26f);
    }
}

// the Yes / No and its question
float confirm(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_workshop& g,
    const pc_ui_workshop_view& v, Pointer& m)
{
    float w = x1 - x0;
    const char* heads[6] = {"", "", "Disassemble", "", "Repair", "Combine"};
    label(f, t, x0, y, heads[v.confirm_kind >= 0 && v.confirm_kind < 6 ? v.confirm_kind : 0]);
    y += 13 + 10;
    if (v.confirm_kind == PC_UI_WSA_DISASSEMBLE && v.target) {
        text(f, fonts.name, 22, x0, y, t.bone, fit(f, fonts.name, 22, v.target->name, w).c_str());
        y += 30;
        // what comes apart
        std::string parts;
        if (v.target_list == PC_UI_WSL_WEAPONS) {
            for (int k = 0; k < g.count[PC_UI_WSL_BLADES] && k < 16; ++k)
                if (g.blades[k].parent == v.target->inventory)
                    parts += g.blades[k].name;
            for (int k = 0; k < g.count[PC_UI_WSL_GRIPS] && k < 16; ++k)
                if (g.grips[k].parent == v.target->inventory)
                    parts += std::string(parts.empty() ? "" : ", ") + g.grips[k].name;
        }
        for (int k = 0; k < 3 && k < v.target->gem_slots; ++k)
            for (int j = 0; j < g.count[PC_UI_WSL_GEMS] && j < 48; ++j)
                if (v.target->gems[k] >= 0 && g.gems[j].inventory == v.target->gems[k])
                    parts += std::string(parts.empty() ? "" : ", ") + g.gems[j].name;
        if (!parts.empty())
            y += paragraph(f, t, x0, y, w, t.ash, ("Comes apart into " + parts + ".").c_str(), 13.5f) + 10;
    }
    if (v.confirm_kind == PC_UI_WSA_REPAIR) { // every record: DP and PP now and after
        int shown = 0;
        float ly = y;
        for (int i = 0; i < g.repair_count && i < PC_UI_WS_REPAIRS; ++i) {
            const pc_ui_ws_repair& r = g.repairs[i];
            if (r.dp == r.dp_after && r.pp == r.pp_after)
                continue;
            if (ly + 22 > BOTTOM - 140) {
                ++shown;
                continue;
            }
            text(f, fonts.name, 15, x0, ly, r.equipped ? t.bone : t.ash,
                fit(f, fonts.name, 15, r.name, w * 0.46f).c_str());
            std::string dp = "DP " + num(r.dp) + ARROW + num(r.dp_after);
            std::string pp = "PP " + num(r.pp) + ARROW + num(r.pp_after);
            text(f, fonts.data, 12, x0 + w * 0.48f, ly + 2, t.hp, dp.c_str());
            text_right(f, fonts.data, 12, x1, ly + 2, r.pp_after < r.pp ? t.down : t.dim, pp.c_str());
            ly += 22;
        }
        if (ly == y)
            ly += paragraph(f, t, x0, y, w, t.dim, "Nothing is damaged.", 13.5f);
        if (shown) {
            text(f, fonts.data, 11, x0, ly + 2, t.dim, ("and " + num(shown) + " more").c_str());
            ly += 18;
        }
        y = ly + 10;
    }
    if (v.prompt) {
        y += paragraph(f, t, x0, y, w, t.ash, line(v.prompt).c_str()) + 12;
    }
    const char* labels[2] = {"Yes", "No"};
    for (int i = 0; i < 2; ++i) {
        bool on = v.cursor == i;
        bool over = m.hit(PC_UI_MOUSE_WS_CONFIRM, i, x0, y, x1, y + MENU_ROW);
        if (on)
            focus_band(f, t, x0, y, x1, y + MENU_ROW);
        text(f, fonts.name, 16.5f, x0 + 16, y + 5, on || over ? t.bone : t.ash, labels[i]);
        y += MENU_ROW;
    }
    return y;
}

// ---- the card ----
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

void preview_card(const Frame& f, const ui::Theme& t, const Card& card, const pc_ui_workshop_view& v, const char* head)
{
    const pc_ui_item_details& d = *v.preview;
    const pc_ui_item& s = d.stats;
    float x = card.x + 20, xr = card.x + card.w - 20, w = xr - x, y = card.y + 18;
    card_begin(f);
    label(f, t, x, y, head);
    y += 13 + 8;
    text(f, fonts.name, 24, x, y, t.bone, fit(f, fonts.name, 24, s.name, w).c_str());
    y += 30 + 4;
    std::string meta;
    int c = v.preview_category;
    if (c == PC_UI_IC_WEAPON || c == PC_UI_IC_BLADE)
        meta = kind_line(s);
    else if (s.material[0])
        meta = s.material;
    if ((c == PC_UI_IC_WEAPON || c == PC_UI_IC_BLADE) && d.blade_category >= 1 && d.blade_category <= 9)
        meta += (meta.empty() ? std::string("") : std::string(DOT)) + BLADE_KINDS[d.blade_category];
    if (!meta.empty()) {
        text(f, fonts.label, t.label_size, x, y, t.ash,
            fit(f, fonts.label, t.label_size, upper(meta.c_str()).c_str(), w, 0.2f).c_str(), 0.2f);
        y += 13 + 12;
    }
    if (s.dp_max > 0 || s.pp_max > 0) {
        float hw = (w - 20) * 0.5f;
        meter(f, t, x, y, hw, "DP", s.dp, s.dp_max, t.hp);
        if (s.pp_max > 0)
            meter(f, t, x + hw + 20, y, hw, "PP", s.pp, s.pp_max, t.gold);
        y += 40;
    }
    static const char* const SN[3] = {"STR", "INT", "AGL"};
    std::string sv[3] = {signed_num(s.str), signed_num(s.intel), signed_num(s.agl)};
    unsigned scol[3] = {s.str < 0 ? t.down : t.bone, s.intel < 0 ? t.down : t.bone, s.agl < 0 ? t.down : t.bone};
    y += cells(f, t, x, y, w, SN, sv, scol) + 6;
    if (c == PC_UI_IC_WEAPON || c == PC_UI_IC_BLADE) {
        static const char* const RN[3] = {"Range", "RISK", "Damage"};
        std::string rv[3] = {num(d.range), num(d.risk),
            s.damage_type >= 0 && s.damage_type < 3 ? DAMAGE[s.damage_type] : "\xe2\x80\x94"};
        y += cells(f, t, x, y, w, RN, rv, nullptr) + 6;
    } else if (c != PC_UI_IC_GEM) {
        std::string tv[3] = {signed_num(d.types[0]), signed_num(d.types[1]), signed_num(d.types[2])};
        y += cells(f, t, x, y, w, TYPES, tv, nullptr) + 6;
    }
    // class and affinity bars, as far as they fit
    if (y + 13 + 8 + PC_UI_CLASSES * 16 < BOTTOM - 30) {
        y += 6;
        label(f, t, x, y, "Class");
        y += 13 + 8;
        y += bars(f, t, x, xr, y, CLASSES, s.classes, s.classes, PC_UI_CLASSES, true, t.gold);
    }
    if (y + 12 + 13 + 8 + PC_UI_AFFINITIES * 16 < BOTTOM - 30) {
        y += 12;
        label(f, t, x, y, "Affinity");
        y += 13 + 8;
        y += bars(f, t, x, xr, y, AFFINITIES, s.affinities, s.affinities, PC_UI_AFFINITIES, true, t.mp);
    }
    card_end(f, t, card, y + 18);
}

void info_card(const Frame& f, const ui::Theme& t, const Card& card, const pc_ui_workshop& g, const char* head,
    const char* title, const char* body, bool materials)
{
    float x = card.x + 20, xr = card.x + card.w - 20, w = xr - x, y = card.y + 18;
    card_begin(f);
    label(f, t, x, y, head);
    y += 13 + 8;
    text(f, fonts.name, 24, x, y, t.bone, fit(f, fonts.name, 24, title, w).c_str());
    y += 30 + 8;
    if (body && *body)
        y += paragraph(f, t, x, y, w, t.ash, body) + 12;
    if (materials) {
        label(f, t, x, y, "Materials worked here");
        y += 13 + 10;
        bool any = false;
        for (int m = 1; m < 8; ++m) {
            bool ok = (g.materials >> m) & 1;
            any = any || ok;
            unsigned col = ok ? t.bone : t.dim;
            if (ok)
                f.dl->AddCircleFilled(f.p(x + 4, y + 9), f.s(3), t.accent);
            else
                f.dl->AddCircle(f.p(x + 4, y + 9), f.s(3), t.rule2, 12, f.s(1));
            text(f, fonts.name, 15, x + 16, y, col, g.material[m]);
            y += 21;
        }
        if (!any) {
            y += 4;
            y += paragraph(f, t, x, y, w, t.dim, "Away from a workshop: Gems, Disassemble and Rename only.", 13.5f);
        }
    }
    card_end(f, t, card, y + 18);
}

} // namespace

void draw_workshop(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    const pc_ui_workshop& g = *pc_ui_workshop_model();
    static pc_ui_workshop_view v;
    pc_ui_workshop_view_get(&v);
    bool reset = fresh || anim.mode != v.mode || anim.flow != v.flow;
    float cw = card_width(f.W);
    float x0 = t.margin_x, xr = f.W - t.margin_x;
    float cmd_w = f.W < 1100 ? 190.0f : 250.0f;
    float mx0 = x0 + cmd_w + 24, mx1 = xr - cw - 24;
    Card card = {xr - cw, top, cw};
    std::string crumb = "Items" + std::string(NEXT) + "Workshop";
    if (!v.valid) {
        spinner(f, x0 + 22, top + 16, 6, t.ash);
        text(f, fonts.label, t.label_size, x0 + 38, top + 10, t.ash, "LOADING", 0.26f);
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        out.crumb = crumb;
        anim.mode = v.mode, anim.flow = v.flow;
        return;
    }
    commands(f, t, x0, x0 + cmd_w, top, g, v, m);
    f.dl->AddLine(f.p(mx0 - 12, top), f.p(mx0 - 12, BOTTOM - 8), t.rule, f.s(1));
    float y = top;
    if (v.flow >= 0)
        crumb += NEXT + std::string(LABELS[v.flow]);
    // the middle: the flow's header and its panel
    auto head = [&](const char* s) {
        label(f, t, mx0, y, s);
        y += 13 + 10;
    };
    switch (v.mode) {
    case PC_UI_WS_ROWS: {
        int i = v.row;
        head(LABELS[i]);
        y += paragraph(f, t, mx0, y, mx1 - mx0, t.ash, line(g.text[HELP[i]]).c_str()) + 14;
        if (!g.action_enabled[i]) {
            int why = g.action_reason[i];
            note(f, t, mx0, y, mx1 - mx0, line(why >= 0 && why < PC_UI_WS_TEXTS ? g.text[why] : g.locked_reason).c_str());
        } else if (i == PC_UI_WSA_REPAIR) {
            int n = 0;
            for (int k = 0; k < g.repair_count && k < PC_UI_WS_REPAIRS; ++k)
                n += g.repairs[k].dp != g.repairs[k].dp_after || g.repairs[k].pp != g.repairs[k].pp_after;
            paragraph(f, t, mx0, y, mx1 - mx0, t.dim,
                n ? (num(n) + (n == 1 ? " piece of equipment is" : " pieces of equipment are") + " damaged.").c_str()
                  : "Nothing is damaged.");
        }
        info_card(f, t, card, g, g.workshop ? "Workshop" : "Not a workshop",
            g.workshop ? ("Workshop " + num(g.workshop)).c_str() : "Away from a workshop", nullptr, true);
        out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_L2, PC_PROMPT_R2, "Category", PC_UI_SUB_NEXT);
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        break;
    }
    case PC_UI_WS_TARGET:
    case PC_UI_WS_PICK: {
        const char* h = "";
        if (v.mode == PC_UI_WS_TARGET)
            h = v.flow == PC_UI_WSA_COMBINE ? "Combine what" : v.flow == PC_UI_WSA_RENAME ? "Rename which weapon"
                : v.flow == PC_UI_WSA_GEMS                                               ? "Set gems in"
                                                                                         : "Take apart";
        else
            h = v.pick == PC_UI_WSP_BLADE ? "Choose a blade" : v.pick == PC_UI_WSP_GRIP ? "Choose a grip"
                : v.pick == PC_UI_WSP_GEM                                                ? "Choose a gem"
                : v.pick_slot                                                            ? "Choose the second"
                                                                                         : "Choose the first";
        head(h);
        entries_list(f, t, mx0, mx1, y, v, dt, reset, m);
        const pc_ui_ws_entry* e = v.cursor >= 0 && v.cursor < v.entries ? &v.entry[v.cursor] : nullptr;
        if (v.preview)
            preview_card(f, t, card, v, v.flow == PC_UI_WSA_COMBINE ? "Result" : v.flow == PC_UI_WSA_GEMS ? "With these gems"
                    : v.preview_category == PC_UI_IC_WEAPON                                                    ? "New weapon"
                                                                                                               : "Part");
        else if (e && e->rec)
            info_card(f, t, card, g, e->current ? "Holds now" : "Record", e->rec->name, e->rec->sub, false);
        else
            info_card(f, t, card, g, LABELS[v.flow >= 0 ? v.flow : 0], e ? e->label : "",
                e && e->reason ? line(e->reason).c_str() : "", false);
        bool en = e && e->enabled;
        out.hint(PC_PROMPT_CIRCLE, -1, v.mode == PC_UI_WS_PICK && e && e->current ? "Take out" : en ? "Select" : "Select",
            PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        if (e)
            crumb += NEXT + std::string(e->label);
        break;
    }
    case PC_UI_WS_ASSEMBLE:
    case PC_UI_WS_SLOTS:
    case PC_UI_WS_COMBINE: {
        if (v.mode == PC_UI_WS_ASSEMBLE)
            head("New weapon");
        else if (v.mode == PC_UI_WS_SLOTS)
            head(v.target ? v.target->name : "Gems");
        else
            head(v.combine == 0 ? "Combine blades" : v.combine == 1 ? "Combine shields" : "Combine armor");
        slots(f, t, mx0, mx1, y, v, m);
        if (v.preview)
            preview_card(f, t, card, v, v.mode == PC_UI_WS_COMBINE ? "Result" : v.mode == PC_UI_WS_SLOTS ? "With these gems"
                    : v.preview_category == PC_UI_IC_WEAPON                                                ? "New weapon"
                                                                                                           : "Part");
        else
            info_card(f, t, card, g, LABELS[v.flow >= 0 ? v.flow : 0],
                v.mode == PC_UI_WS_COMBINE ? "Choose two" : v.mode == PC_UI_WS_ASSEMBLE ? "Choose the parts" : "",
                line(g.text[HELP[v.flow >= 0 ? v.flow : 0]]).c_str(), v.mode == PC_UI_WS_COMBINE);
        out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    }
    case PC_UI_WS_NAME:
        head(v.assembling ? "Name the new weapon" : "Rename");
        name_panel(f, t, mx0, mx1, y, g, v, m);
        if (v.preview)
            preview_card(f, t, card, v, "New weapon");
        else if (v.target)
            info_card(f, t, card, g, "Weapon", v.target->name, v.target->sub, false);
        out.hint(PC_PROMPT_CIRCLE, -1, "Type", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_SQUARE, -1, "Delete", PC_UI_ALT);
        out.hint(PC_PROMPT_L1, PC_PROMPT_R1, "Cursor", PC_UI_TAB_NEXT);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        crumb += NEXT + std::string("Name");
        break;
    case PC_UI_WS_CONFIRM:
        confirm(f, t, mx0, mx1, y, g, v, m);
        if (v.preview)
            preview_card(f, t, card, v, "Result");
        else if (v.confirm_kind == PC_UI_WSA_DISASSEMBLE && v.target)
            info_card(f, t, card, g, "Disassemble", v.target->name, v.target->sub, false);
        else
            info_card(f, t, card, g, LABELS[v.flow >= 0 ? v.flow : 0], "Repair",
                line(g.text[HELP[PC_UI_WSA_REPAIR]]).c_str(), false);
        out.hint(PC_PROMPT_CIRCLE, -1, v.cursor == 0 ? "Confirm" : "Cancel", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        break;
    default:
        break;
    }
    if (g.busy && v.mode != PC_UI_WS_ROWS)
        out.busy = true;
    if (v.message) // a refused request: the line over the middle's bottom
        note(f, t, mx0, BOTTOM - 40, mx1 - mx0, v.message);
    out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
    out.crumb = crumb;
    anim.mode = v.mode, anim.flow = v.flow;
}

} // namespace uidraw
