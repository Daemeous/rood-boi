// Modern menus: the Ashley tab (pc/docs/Modern_Menus.md). Left rail (448
// wide): HP / MP / RISK, the weapon and shield rows, STR / INT / AGL, the
// condition per limb and the status effect chips. The model column
// (model_column in ui_draw.cpp) holds Ashley's live model, drawn by the game
// under this layer (ashley_bridge.c). The inspector card explains the focused
// row: right of the model in 16:9, under it (compact) in 4:3. State comes from
// the shell (pc_ui_shell_ashley, pc_ui_shell_equipment); nothing here changes
// it except through the pointer (Pointer hits, applied at the shell's next tick).
#include "ui/ui_draw_internal.h"
#include "button_prompts.h"
#include "camera_stick.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <string>

namespace uidraw {
namespace {

// layout units
const float BOTTOM = 720 - 64;       // the body's bottom (above the footer)
const float GEAR_H = 74;             // a weapon / shield row
const float LIMB_ROW = 31;           // a limb cell's height plus the gap below it
const float CHIP_H = 26, CHIP_GAP = 8;

const char* const LIMB_STATES[5] = {"Critical", "Damaged", "Wounded", "Good", "Excellent"};
unsigned limb_colour(const ui::Theme& t, int state)
{
    return state <= PC_UI_LIMB_DAMAGED ? t.risk : state == PC_UI_LIMB_WOUNDED ? t.gold : t.hp;
}
int clamp_state(int s) { return s < 0 ? 0 : s > 4 ? 4 : s; }
float frac(int v, int max)
{
    float a = max > 0 ? (float)v / max : 0;
    return a < 0 ? 0 : a > 1 ? 1 : a;
}
// "1:17" or "45s" ("" without a timer)
std::string time_left(int seconds)
{
    char buf[16] = "";
    if (seconds >= 60)
        std::snprintf(buf, sizeof buf, "%d:%02d", seconds / 60, seconds % 60);
    else if (seconds >= 0)
        std::snprintf(buf, sizeof buf, "%ds", seconds);
    return buf;
}

struct Anim {
    float band0 = 0, band1 = 0; // the focus band (weapon / shield / condition rows)
} anim;

// A thin bar: track and fill, 3 high, from x0 to x1 at y.
void track(const Frame& f, const ui::Theme& t, float x0, float x1, float y, float a, unsigned col, float h = 3)
{
    f.dl->AddRectFilled(f.p(x0, y), f.p(x1, y + h), t.rule);
    if (a > 0)
        f.dl->AddRectFilled(f.p(x0, y), f.p(x0 + (x1 - x0) * a, y + h), col);
}

// HP / MP / RISK in three columns; returns the height.
float meters(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_ashley& a)
{
    const float gap = 18, w = (x1 - x0 - 2 * gap) / 3;
    const char* names[3] = {"HP", "MP", "RISK"};
    unsigned cols[3] = {t.hp, t.mp, t.risk};
    for (int i = 0; i < 3; ++i) {
        float x = x0 + i * (w + gap), xr = x + w;
        text(f, fonts.label, 12, x, y, t.bone, names[i], 0.16f);
        if (i < 2) {
            int v = i ? a.mp : a.hp, max = i ? a.mp_max : a.hp_max;
            float rx = xr - text_right(f, fonts.data, 13, xr, y - 1, t.bone, num(max).c_str());
            rx -= text_right(f, fonts.data, 13, rx, y - 1, t.dim, " / ");
            text_right(f, fonts.data, 13, rx, y - 1, t.bone, num(v).c_str());
            track(f, t, x, xr, y + 20, frac(v, max), cols[i]);
        } else {
            text_right(f, fonts.data, 13, xr, y - 1, t.bone, (num(a.risk) + "%").c_str());
            track(f, t, x, xr, y + 20, frac(a.risk, 100), cols[i]);
        }
    }
    return 23;
}

// A label, a short bar and its value ("DP ▬▬ 41"); returns the width.
float mini(const Frame& f, const ui::Theme& t, float x, float y, const char* name, int v, int max, unsigned col)
{
    float x0 = x;
    x += text(f, fonts.data, 11, x, y, t.ash, name) + 7;
    track(f, t, x, x + 56, y + 6, frac(v, max), col);
    x += 56 + 7;
    x += text(f, fonts.data, 11, x, y, t.ash, num(v).c_str());
    return x - x0;
}

// The weapon or shield row: icon, name, DP / PP, damage type, material.
void gear_row(const Frame& f, const ui::Theme& t, float x0, float x1, float y, int row, const pc_ui_item& it, Pointer& m)
{
    bool shield = row == PC_UI_ASH_SHIELD, worn = it.inventory >= 0;
    m.hit(PC_UI_MOUSE_ROW, row, x0, y, x1, y + GEAR_H);
    bool on = pc_ui_shell_ashley_row() == row;
    icon(f, shield ? IC_SHIELD : IC_SWORD, x0 + 26, y + 26, 20, on ? t.bone : t.ash);
    float tx = x0 + 46, right = x1 - 12;
    const char* slot = shield ? "SHIELD" : it.two_handed ? "WEAPON \xc2\xb7 TWO-HANDED" : "WEAPON";
    float sw = text_right(f, fonts.label, 10, right, y + 19, t.dim, slot, 0.22f);
    std::string name = fit(f, fonts.name, 22, worn ? it.name : shield ? "No shield" : "No weapon", right - sw - 14 - tx);
    text(f, fonts.name, 22, tx, y + 11, worn ? t.bone : t.dim, name.c_str());
    if (worn) {
        float sy = y + 46, x = tx;
        x += mini(f, t, x, sy, "DP", it.dp, it.dp_max, t.hp) + 20;
        x += mini(f, t, x, sy, "PP", it.pp, it.pp_max, t.mp) + 20;
        std::string kind;
        if (it.damage_type >= 0 && it.damage_type < 3)
            kind = DAMAGE[it.damage_type];
        if (it.material[0])
            kind += (kind.empty() ? "" : DOT) + std::string(it.material);
        text(f, fonts.data, 11, x, sy, t.ash, fit(f, fonts.data, 11, kind.c_str(), right - x).c_str());
    }
    f.dl->AddLine(f.p(x0, y + GEAR_H), f.p(x1, y + GEAR_H), t.rule, f.s(1));
}

// STR / INT / AGL: a bone diamond with the value, its name in a tag; returns the height.
float attributes(const Frame& f, const ui::Theme& t, float x, float y, const pc_ui_ashley& a)
{
    const char* names[3] = {"STR", "INT", "AGL"};
    int vals[3] = {a.str, a.intel, a.agl};
    const float r = 22, cy = y + r;
    for (int i = 0; i < 3; ++i) {
        float cx = x + r, tw = measure(f, fonts.label, 11, names[i], 0.18f);
        float t0 = cx + r - 8, t1 = t0 + 18 + tw + 10;
        f.dl->AddRect(f.p(t0, cy - 10), f.p(t1, cy + 10), t.rule2, 0, 0, f.s(1));
        text(f, fonts.label, 11, t0 + 18, cy - 7, t.ash, names[i], 0.18f);
        f.dl->AddQuadFilled(f.p(cx, cy - r), f.p(cx + r, cy), f.p(cx, cy + r), f.p(cx - r, cy), t.bone);
        std::string v = num(vals[i]);
        float vw = measure(f, fonts.data, 12, v.c_str());
        text(f, fonts.data, 12, cx - vw * 0.5f, cy - 8, t.ground | 0xFF000000u, v.c_str());
        x = t1 + 24;
    }
    return 2 * r;
}

// The worst limb (lowest state, then lowest share of its HP), -1 none.
int worst_limb(const pc_ui_ashley& a)
{
    int w = -1;
    for (int i = 0; i < a.limb_count && i < PC_UI_LIMBS; ++i) {
        const pc_ui_limb& l = a.limbs[i];
        if (w < 0 || l.state < a.limbs[w].state
            || (l.state == a.limbs[w].state && frac(l.hp, l.hp_max) < frac(a.limbs[w].hp, a.limbs[w].hp_max)))
            w = i;
    }
    return w;
}

// The condition block: overall state, then a two-column grid of limb bars; returns the height.
float condition(const Frame& f, const ui::Theme& t, float x0, float x1, float y, const pc_ui_ashley& a, Pointer& m)
{
    int n = a.limb_count < 0 ? 0 : a.limb_count > PC_UI_LIMBS ? PC_UI_LIMBS : a.limb_count;
    int rows = (n + 1) / 2;
    float h = 10 + 14 + 12 + rows * LIMB_ROW - 9 + 12;
    m.hit(PC_UI_MOUSE_ROW, PC_UI_ASH_CONDITION, x0, y, x1, y + h);
    float x = x0 + 14, xr = x1 - 12, cy = y + 10;
    label(f, t, x, cy, "Condition", t.bone);
    int w = worst_limb(a);
    if (w >= 0) {
        int s = clamp_state(a.limbs[w].state);
        text_right(f, fonts.label, 11, xr, cy, limb_colour(t, s), upper(LIMB_STATES[s]).c_str(), 0.2f);
    }
    cy += 14 + 12;
    const float gap = 22, cw = (xr - x - gap) / 2;
    int focused = pc_ui_shell_ashley_limb();
    for (int i = 0; i < n; ++i) {
        const pc_ui_limb& l = a.limbs[i];
        int s = clamp_state(l.state);
        float lx = x + (i % 2) * (cw + gap), ly = cy + (i / 2) * LIMB_ROW;
        m.hit(PC_UI_MOUSE_LIMB, i, lx - 4, ly - 4, lx + cw + 4, ly + LIMB_ROW - 6); // the part (over the panel's hit)
        float sw = text_right(f, fonts.data, 11.5f, lx + cw, ly, limb_colour(t, s), LIMB_STATES[s]);
        text(f, fonts.label, 12, lx, ly - 1, i == focused ? t.bone : t.ash,
            fit(f, fonts.label, 12, l.name, cw - sw - 8, 0.05f).c_str(), 0.05f);
        track(f, t, lx, lx + cw, ly + 17, frac(l.hp, l.hp_max), limb_colour(t, s));
    }
    f.dl->AddLine(f.p(x0, y + h), f.p(x1, y + h), t.rule, f.s(1));
    return h;
}

// The effects: a label and one chip per effect (name, time left), wrapped
// within the rail and cut at `bottom`: the chips that fit, then "+N" for the
// rest on the last line (chips give way to it). Tells the shell how many
// chips show (only those are rows); returns the height.
float effects(const Frame& f, const ui::Theme& t, float x0, float x1, float y, float bottom, const pc_ui_ashley& a,
    Pointer& m)
{
    float lx = x0 + 14;
    float lw = text(f, fonts.label, t.label_size, lx, y + (CHIP_H - 13) * 0.5f, t.ash, "EFFECTS", 0.26f);
    float cx0 = lx + lw + 14;
    int n = a.effect_count < 0 ? 0 : a.effect_count > PC_UI_EFFECTS ? PC_UI_EFFECTS : a.effect_count;
    if (n == 0) {
        pc_ui_shell_ashley_effects_shown(0);
        text(f, fonts.data, 12, cx0, y + 6, t.dim, "None");
        return CHIP_H;
    }
    // layout: each chip's place, until a line would pass the bottom
    struct Chip {
        float x, y, w;
        std::string name, tl;
    } chips[PC_UI_EFFECTS];
    int shown = 0;
    float x = cx0, cy = y;
    for (int i = 0; i < n; ++i) {
        Chip& c = chips[i];
        c.tl = time_left(a.effects[i].seconds);
        c.name = fit(f, fonts.data, 12, a.effects[i].name, x1 - cx0 - 22 - (c.tl.empty() ? 0 : 40));
        c.w = 12 + measure(f, fonts.data, 12, c.name.c_str()) + 10;
        if (!c.tl.empty())
            c.w += 8 + measure(f, fonts.data, 12, c.tl.c_str());
        if (x > cx0 && x + c.w > x1)
            x = cx0, cy += CHIP_H + CHIP_GAP;
        if (cy + CHIP_H > bottom && i > 0)
            break;
        c.x = x, c.y = cy;
        x += c.w + 10;
        shown = i + 1;
    }
    // "+N" after the last chip shown, on its line: drop chips until it fits
    std::string more;
    float more_x = cx0, more_y = y;
    while (shown < n) {
        more = "+" + num(n - shown);
        float mw = measure(f, fonts.data, 12, more.c_str());
        const Chip* last = shown > 0 ? &chips[shown - 1] : nullptr;
        more_x = last ? last->x + last->w + 10 : cx0;
        more_y = last ? last->y : y;
        if (!last || more_x + mw <= x1)
            break;
        --shown;
    }
    pc_ui_shell_ashley_effects_shown(shown);
    int focus = pc_ui_shell_ashley_row() - PC_UI_ASH_EFFECT;
    float end = y + CHIP_H;
    for (int i = 0; i < shown; ++i) {
        const pc_ui_effect& e = a.effects[i];
        const Chip& c = chips[i];
        bool on = i == focus;
        m.hit(PC_UI_MOUSE_ROW, PC_UI_ASH_EFFECT + i, c.x, c.y, c.x + c.w, c.y + CHIP_H);
        if (on)
            f.dl->AddRectFilled(f.p(c.x, c.y), f.p(c.x + c.w, c.y + CHIP_H), t.accent_soft);
        f.dl->AddRect(f.p(c.x, c.y), f.p(c.x + c.w, c.y + CHIP_H), on ? t.accent : t.rule2, 0, 0, f.s(1));
        f.dl->AddRectFilled(f.p(c.x, c.y), f.p(c.x + 2, c.y + CHIP_H), e.buff ? t.mp : t.risk);
        float tx = c.x + 12 + text(f, fonts.data, 12, c.x + 12, c.y + 6, t.bone, c.name.c_str());
        if (!c.tl.empty())
            text(f, fonts.data, 12, tx + 8, c.y + 6, t.ash, c.tl.c_str());
        end = c.y + CHIP_H;
    }
    if (shown < n)
        text(f, fonts.data, 12, more_x, more_y + 6, t.ash, more.c_str());
    return end - y;
}

// "Beast 4 · Human 2 · Undead 1": the three largest (by size) non-zero values, "None" without.
std::string top3(const char* const* names, const int* v, int n)
{
    int idx[8];
    for (int i = 0; i < n; ++i)
        idx[i] = i;
    std::stable_sort(idx, idx + n, [&](int a, int b) { return std::abs(v[a]) > std::abs(v[b]); });
    std::string s;
    for (int k = 0; k < n && k < 3; ++k) {
        if (v[idx[k]] == 0)
            break;
        if (!s.empty())
            s += DOT;
        s += names[idx[k]] + std::string(" ") + num(v[idx[k]]);
    }
    return s.empty() ? "None" : s;
}

// The inspector card for the focused row, at (c.x, c.y), c.w wide, cut at bottom.
void inspector(const Frame& f, const ui::Theme& t, const Card& c, float bottom, bool compact, const pc_ui_ashley& a,
    const pc_ui_equipment& e)
{
    int row = pc_ui_shell_ashley_row();
    float x = c.x + 20, xr = c.x + c.w - 20, w = xr - x, y = c.y + (compact ? 14 : 18);
    float title = compact ? 22 : 25;
    card_begin(f);
    f.dl->PushClipRect(f.p(c.x, c.y), f.p(c.x + c.w, bottom), true);
    if (row == PC_UI_ASH_WEAPON || row == PC_UI_ASH_SHIELD) {
        bool shield = row == PC_UI_ASH_SHIELD;
        const pc_ui_item& it = e.equipped[shield ? PC_UI_SLOT_SHIELD : PC_UI_SLOT_WEAPON];
        bool worn = e.valid && it.inventory >= 0;
        label(f, t, x, y, shield ? "Shield" : it.two_handed ? "Weapon \xc2\xb7 Two-handed" : "Weapon");
        y += 13 + 8;
        text(f, fonts.name, title, x, y, worn ? t.bone : t.dim,
            fit(f, fonts.name, title, worn ? it.name : "Nothing equipped", w).c_str());
        y += title + 6 + 8;
        if (worn) {
            std::string kind = kind_line(it);
            if (!kind.empty()) {
                text(f, fonts.label, t.label_size, x, y, t.ash, fit(f, fonts.label, t.label_size, upper(kind.c_str()).c_str(), w, 0.2f).c_str(), 0.2f);
                y += 13 + 12;
            }
            // DP / PP, then the item's own STR / INT / AGL
            float sx = x;
            sx += text(f, fonts.data, 13, sx, y, t.ash, "DP ");
            sx += text(f, fonts.data, 13, sx, y, t.bone, (num(it.dp) + " / " + num(it.dp_max)).c_str()) + 22;
            sx += text(f, fonts.data, 13, sx, y, t.ash, "PP ");
            text(f, fonts.data, 13, sx, y, t.bone, (num(it.pp) + " / " + num(it.pp_max)).c_str());
            y += 22;
            const char* stats[3] = {"STR ", "INT ", "AGL "};
            int vals[3] = {it.str, it.intel, it.agl};
            sx = x;
            for (int k = 0; k < 3; ++k) {
                sx += text(f, fonts.data, 13, sx, y, t.ash, stats[k]);
                sx += text(f, fonts.data, 13, sx, y, t.bone, signed_num(vals[k]).c_str()) + 22;
            }
            y += 22;
            if (compact) {
                // one line each: the three strongest
                y += 4;
                float lw = 80;
                label(f, t, x, y + 1, "Class");
                text(f, fonts.data, 12, x + lw, y, t.ash, fit(f, fonts.data, 12, top3(CLASSES, it.classes, PC_UI_CLASSES).c_str(), w - lw).c_str());
                y += 20;
                label(f, t, x, y + 1, "Affinity");
                text(f, fonts.data, 12, x + lw, y, t.ash, fit(f, fonts.data, 12, top3(AFFINITIES, it.affinities, PC_UI_AFFINITIES).c_str(), w - lw).c_str());
                y += 20;
            } else {
                y += 10;
                label(f, t, x, y, "Class");
                y += 13 + 8;
                y += bars(f, t, x, xr, y, CLASSES, it.classes, it.classes, PC_UI_CLASSES, true, t.gold);
                y += 10;
                label(f, t, x, y, "Affinity");
                y += 13 + 8;
                y += bars(f, t, x, xr, y, AFFINITIES, it.affinities, it.affinities, PC_UI_AFFINITIES, true, t.mp);
            }
        } else {
            y += paragraph(f, t, x, y, w, t.ash, shield ? "Equipment can put a shield on." : "Equipment can put a weapon on.",
                13.5f);
        }
    } else if (row == PC_UI_ASH_CONDITION) {
        // the panel as a whole, or the body part focused in it (the shell's text: its JSON says the same)
        int limb = pc_ui_shell_ashley_limb();
        char head[96], line[192];
        pc_ui_ashley_condition_text(&a, limb, head, sizeof head, line, sizeof line);
        label(f, t, x, y, limb >= 0 ? "Condition \xc2\xb7 Body part" : "Condition \xc2\xb7 Overall");
        y += 13 + 8;
        text(f, fonts.name, title, x, y, t.bone, fit(f, fonts.name, title, head, w).c_str());
        y += title + 6 + 8;
        y += paragraph(f, t, x, y, w, t.ash, line, 13.5f) + 8;
        // every limb: name, HP, state (two columns when compact); the focused one in the bright colour
        int n = a.limb_count < 0 ? 0 : a.limb_count > PC_UI_LIMBS ? PC_UI_LIMBS : a.limb_count;
        int cols = compact ? 2 : 1;
        float gap = 24, cw = (w - (cols - 1) * gap) / cols, rh = compact ? 20 : 23;
        for (int i = 0; i < n; ++i) {
            const pc_ui_limb& l = a.limbs[i];
            int s = clamp_state(l.state);
            float lx = x + (i % cols) * (cw + gap), ly = y + (i / cols) * rh;
            float sw = text_right(f, fonts.data, 12, lx + cw, ly, limb_colour(t, s), LIMB_STATES[s]);
            float hw = text_right(f, fonts.data, 12, lx + cw - sw - (compact ? 10 : 18), ly, t.bone,
                (num(l.hp) + " / " + num(l.hp_max)).c_str());
            text(f, fonts.data, 12, lx, ly, i == limb ? t.bone : t.ash, fit(f, fonts.data, 12, l.name, cw - sw - hw - 28).c_str());
        }
        y += ((n + cols - 1) / cols) * rh;
        if (!compact) {
            y += 8;
            y += paragraph(f, t, x, y, w, t.ash,
                "Each part of the body has its own HP and is hurt on its own; the bars on the left show how much "
                "each part has left.",
                13.5f);
        }
    } else if (row >= PC_UI_ASH_EFFECT && row - PC_UI_ASH_EFFECT < a.effect_count) {
        const pc_ui_effect& fx = a.effects[row - PC_UI_ASH_EFFECT];
        std::string head = fx.buff ? "Buff" : "Ailment";
        std::string tl = time_left(fx.seconds);
        if (!tl.empty())
            head += DOT + tl + " left";
        float lx = x;
        f.dl->AddRectFilled(f.p(lx, y), f.p(lx + 2, y + 12), fx.buff ? t.mp : t.risk);
        label(f, t, lx + 10, y, head.c_str());
        y += 13 + 8;
        text(f, fonts.name, title, x, y, t.bone, fit(f, fonts.name, title, fx.name, w).c_str());
        y += title + 6 + 8;
        if (fx.help[0])
            y += paragraph(f, t, x, y, w, t.ash, fx.help, compact ? 13.5f : t.body_size);
        if (fx.seconds < 0) {
            y += 8;
            y += paragraph(f, t, x, y, w, t.dim, "No timer.", 13);
        }
    }
    f.dl->PopClipRect();
    float end = y + (compact ? 12 : 16);
    card_end(f, t, c, end < bottom ? end : bottom);
}

// "Turn" under the model: the right stick's icon (keyboard: J and L caps).
void turn_hint(const Frame& f, const ui::Theme& t, float cx, float y)
{
    const char* verb = "TURN";
    float vw = measure(f, fonts.label, 10, verb, 0.3f), size = 18, cy = y + 6;
    bool keys = pc_prompt_device() == PC_PROMPTS_KEYBOARD;
    if (keys && !pc_camera_stick_keys) { // the mouse only
        verb = "DRAG TO TURN";
        vw = measure(f, fonts.label, 10, verb, 0.3f);
        text(f, fonts.label, 10, cx - vw * 0.5f, y, t.dim, verb, 0.3f);
        return;
    }
    float gw = 0;
    if (keys) {
        gw = 2 * 16 + 4;
    } else {
        gw = glyph(f, PC_PROMPT_R3, size, 0, 0, true);
    }
    float x = cx - (gw + 8 + vw) * 0.5f;
    if (keys) {
        const char* caps[2] = {"J", "L"};
        for (int i = 0; i < 2; ++i) {
            float kx = x + i * 20;
            f.dl->AddRect(f.p(kx, cy - 8), f.p(kx + 16, cy + 8), t.ash, f.s(2), 0, f.s(1));
            float w = measure(f, fonts.data, 10, caps[i]);
            text(f, fonts.data, 10, kx + (16 - w) * 0.5f, cy - 6.5f, t.bone, caps[i]);
        }
    } else if (gw > 0) {
        glyph(f, PC_PROMPT_R3, size, x, cy);
    }
    text(f, fonts.label, 10, x + gw + 8, y, t.dim, verb, 0.3f);
}

} // namespace

void draw_ashley(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, bool model_shown, Pointer& m,
    ScreenOut& out)
{
    const pc_ui_ashley& a = *pc_ui_shell_ashley();
    const pc_ui_equipment& e = *pc_ui_shell_equipment();
    float x0 = t.margin_x, x1 = x0 + t.rail_width;
    int row = pc_ui_shell_ashley_row();
    out.hint(PC_PROMPT_SQUARE, -1, "Status (original)", PC_UI_ALT);
    out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
    out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
    if (!a.valid) {
        spinner(f, x0 + 22, top + 10, 6, t.ash);
        text(f, fonts.label, t.label_size, x0 + 38, top + 4, t.ash, "LOADING", 0.26f);
        out.crumb = "Ashley";
        return;
    }
    if (row == PC_UI_ASH_WEAPON || row == PC_UI_ASH_SHIELD) { // Circle first
        Hint h = {PC_PROMPT_CIRCLE, -1, "Equipment", PC_UI_CONFIRM};
        for (int i = out.hint_count; i > 0; --i)
            out.hints[i] = out.hints[i - 1];
        out.hints[0] = h;
        ++out.hint_count;
    }

    // left rail
    float y = top;
    y += meters(f, t, x0, x1, y, a) + 16;
    float gear_y = y, cond_y = gear_y + 2 * GEAR_H + 16 + 44 + 16;
    // the focus band (drawn first, under the rows)
    float b0 = row == PC_UI_ASH_SHIELD ? gear_y + GEAR_H : row == PC_UI_ASH_CONDITION ? cond_y : gear_y;
    int n = a.limb_count < 0 ? 0 : a.limb_count > PC_UI_LIMBS ? PC_UI_LIMBS : a.limb_count;
    float cond_h = 10 + 14 + 12 + ((n + 1) / 2) * LIMB_ROW - 9 + 12;
    float b1 = row == PC_UI_ASH_CONDITION ? cond_y + cond_h : b0 + GEAR_H;
    if (fresh || anim.band1 <= anim.band0)
        anim.band0 = b0, anim.band1 = b1;
    anim.band0 = approach(anim.band0, b0, dt, t.focus_grow);
    anim.band1 = approach(anim.band1, b1, dt, t.focus_grow);
    if (row < PC_UI_ASH_EFFECT)
        focus_band(f, t, x0, anim.band0, x1, anim.band1);
    gear_row(f, t, x0, x1, y, PC_UI_ASH_WEAPON, e.equipped[PC_UI_SLOT_WEAPON], m);
    y += GEAR_H;
    gear_row(f, t, x0, x1, y, PC_UI_ASH_SHIELD, e.equipped[PC_UI_SLOT_SHIELD], m);
    y += GEAR_H + 16;
    y += attributes(f, t, x0 + 14, y, a) + 16;
    y += condition(f, t, x0, x1, y, a, m) + 14;
    effects(f, t, x0, x1, y, BOTTOM, a, m);

    // the model column: the model shows through (no panel there); "Turn" under it
    float r[4];
    model_column(t, f.W, r);
    if (model_shown) {
        turn_hint(f, t, r[0] + r[2] * 0.5f, r[1] + r[3] * 0.92f);
    } else if (a.model_blocked != PC_UI_MODEL_OK) {
        const char* note = a.model_blocked == PC_UI_MODEL_NO_HEAP ? "MODEL UNAVAILABLE" : "MODEL HIDDEN (FREE CAMERA)";
        float nw = measure(f, fonts.label, 10, note, 0.3f);
        text(f, fonts.label, 10, r[0] + (r[2] - nw) * 0.5f, r[1] + r[3] * 0.5f - 5, t.dim, note, 0.3f);
    } else if (a.model == 1) {
        spinner(f, r[0] + r[2] * 0.5f, r[1] + r[3] * 0.5f, 8, t.ash);
    }

    // the inspector card: right (16:9), or under the model (4:3)
    bool compact = f.W < 1100;
    Card c;
    float bottom;
    if (compact) {
        c = {x1 + 16, r[1] + r[3] + 18, f.W - t.margin_x - (x1 + 16)};
        bottom = BOTTOM;
    } else {
        c = {f.W - t.margin_x - card_width(f.W), top, card_width(f.W)};
        bottom = BOTTOM;
    }
    inspector(f, t, c, bottom, compact, a, e);

    // crumb: area · room, then the play time (never cut)
    std::string crumb = a.area;
    if (a.room[0])
        crumb += (crumb.empty() ? "" : DOT) + std::string(a.room);
    out.crumb = crumb.empty() ? "Ashley" : crumb;
    char play[32];
    std::snprintf(play, sizeof play, "%d:%02d:%02d", a.play_h, a.play_m, a.play_s);
    out.crumb_tail = std::string(DOT) + play;
}

} // namespace uidraw
