// Modern menus: the System tab (pc/docs/Modern_Menus.md). Left: the list, the
// game's own screens (Data, Options, Score, Quick manual) then the settings
// pages (ui_settings.cpp). Right: a card with the page's rows (label, value as
// "< value >", sliders as bars, toggles as Off / On, bindings as key caps and
// the gamepad element), the focused row's help and the save status at the
// bottom; a game entry's card says what it opens. A binding capture covers
// the screen with its prompt. State comes from the shell
// (pc_ui_shell_system_view) and the settings model; nothing here changes it
// except through the pointer (Pointer hits and pc_ui_shell_system_value,
// applied at the shell's next tick).
#include "ui/ui_draw_internal.h"
#include "ui/ui_settings.h"
#include "button_prompts.h"
#include "display_confirm.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" const char* pc_settings_status(void);

namespace uidraw {
namespace {

// layout units
const float BOTTOM = 720 - 56;  // the body's bottom (above the footer)
const float ENTRY_H = 36;       // a list entry
const float ROW_H = 42;         // a settings row
const float HEAD_H = 62;        // the card's title
const float HELP_H = 112;       // the card's help, reason and status at its bottom

float list_width(float W) { return W < 1100 ? 236.0f : 300.0f; }

struct Anim {
    float scroll = 0;
    int page = -1;
    // a slider being dragged: its row, from the mouse or a test pointer
    int drag_row = -1;
    double drag_value = 0; // the dragged slider's last value (applied when the drag ends)
    bool dragged = false;
    int drag_page = -1, shown_page = -1; // the page the drag began on; the page drawn this frame
    bool drag_rpc = false;
    float wheel = 0; // wheel notches not turned into steps yet
    float list_scroll = 0, list_max = 0; // the list of pages (more than fit: plugins' pages)
} anim;

// "Saved settings to /a/b/settings.cfg" -> "Saved settings to settings.cfg": the
// status line shows file names only.
std::string short_status(const char* status)
{
    std::string out, word;
    for (const char* p = status;; ++p) {
        if (*p && *p != ' ') {
            word += *p;
            continue;
        }
        size_t slash = word.find_last_of("/\\");
        if (slash != std::string::npos && slash + 1 < word.size())
            word = (word[0] == '(' ? "(" : "") + word.substr(slash + 1);
        out += word;
        word.clear();
        if (!*p)
            break;
        out += ' ';
    }
    return out;
}

// Split "Z, Return" into its keys.
std::vector<std::string> split_keys(const char* s)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = s;; ++p) {
        if (*p && *p != ',') {
            if (!(cur.empty() && *p == ' '))
                cur += *p;
            continue;
        }
        while (!cur.empty() && cur.back() == ' ')
            cur.pop_back();
        if (!cur.empty())
            out.push_back(cur);
        cur.clear();
        if (!*p)
            break;
    }
    return out;
}

// A chevron (dir -1 left, +1 right) centred at (cx, cy).
void chevron(const Frame& f, float cx, float cy, int dir, unsigned col, float size = 1)
{
    float w = 2.5f * dir * size, h = 5 * size;
    ImVec2 pts[3] = {f.p(cx - w, cy - h), f.p(cx + w, cy), f.p(cx - w, cy + h)};
    f.dl->AddPolyline(pts, 3, col, 0, f.s(1.6f));
}

// A key cap right-aligned at x1, centred on cy; returns its width. outline:
// the gamepad element (an outlined cap), else a filled key.
float cap(const Frame& f, const ui::Theme& t, float x1, float cy, const char* str, bool enabled, bool outline)
{
    float size = 12.5f, pad = 8, h = 22;
    float w = measure(f, fonts.data, size, str) + pad * 2;
    float x0 = x1 - w;
    if (outline)
        f.dl->AddRect(f.p(x0, cy - h * 0.5f), f.p(x1, cy + h * 0.5f), enabled ? t.ash : t.dim, f.s(4), 0, f.s(1));
    else {
        f.dl->AddRectFilled(f.p(x0, cy - h * 0.5f), f.p(x1, cy + h * 0.5f), t.rule2, f.s(4));
        f.dl->AddLine(f.p(x0 + 3, cy + h * 0.5f), f.p(x1 - 3, cy + h * 0.5f), t.rule, f.s(1.5f));
    }
    text(f, fonts.data, size, x0 + pad, cy - size * 0.5f - 1, enabled ? t.bone : t.dim, str);
    return w;
}

// The value column of row i: x0..x1, centred on cy.
void draw_value(const Frame& f, const ui::Theme& t, const pc_ui_setting_row& r, int i, float x0, float x1, float cy,
    bool focused, bool capture, bool hit_ok, Pointer& m)
{
    unsigned col = r.enabled ? t.bone : t.dim;
    switch (r.kind) {
    case PC_UI_SET_CHOICE: {
        float size = 16, aw = 22;
        unsigned arrow = !r.enabled ? t.dim : focused ? t.bone : t.ash;
        bool left_over = hit_ok && r.enabled && m.over(x0 - 4, cy - 18, x0 + aw, cy + 18);
        bool right_over = hit_ok && r.enabled && m.over(x1 - aw, cy - 18, x1 + 4, cy + 18);
        chevron(f, x0 + 8, cy, -1, left_over ? t.accent : arrow);
        chevron(f, x1 - 8, cy, 1, right_over ? t.accent : arrow);
        float chip = r.swatch ? 16 : 0, gap = r.swatch ? 8 : 0; // a colour row's chip before the value
        std::string v = fit(f, fonts.name, size, r.value, x1 - x0 - aw * 2 - 8 - (r.swatch ? chip + gap + 2 : 0));
        float w = measure(f, fonts.name, size, v.c_str());
        float tx = (x0 + x1 - w + chip + gap) * 0.5f;
        if (r.swatch) {
            // the vivid tint, as classic window-colour screens show it (the menus show the dark result)
            unsigned c = 0xFF000000u | (unsigned)r.swatch_rgb[0] | ((unsigned)r.swatch_rgb[1] << 8)
                | ((unsigned)r.swatch_rgb[2] << 16);
            unsigned edge = focused ? t.ash : t.rule2;
            if (!r.enabled)
                c = fade(c, 0.5f), edge = fade(edge, 0.5f);
            float cx0 = tx - gap - chip;
            f.dl->AddRectFilled(f.p(cx0, cy - chip * 0.5f), f.p(cx0 + chip, cy + chip * 0.5f), c, f.s(3));
            f.dl->AddRect(f.p(cx0, cy - chip * 0.5f), f.p(cx0 + chip, cy + chip * 0.5f), edge, f.s(3), 0, f.s(1));
        }
        text(f, fonts.name, size, tx, cy - size * 0.5f - 2, col, v.c_str());
        if ((left_over || right_over) && m.clicked) {
            pc_ui_shell_system_value(i, left_over ? -1 : 1, 0, 0);
            m.clicked = false;
        }
        break;
    }
    case PC_UI_SET_TOGGLE: {
        float size = 15, gap = 20;
        float on_w = measure(f, fonts.label, size, "ON", 0.2f), off_w = measure(f, fonts.label, size, "OFF", 0.2f);
        float on_x = x1 - on_w, off_x = on_x - gap - off_w, y = cy - size * 0.5f - 1;
        const char* names[2] = {"OFF", "ON"};
        float xs[2] = {off_x, on_x}, ws[2] = {off_w, on_w};
        for (int k = 0; k < 2; ++k) {
            bool sel = r.index == k;
            bool over = hit_ok && r.enabled && m.over(xs[k] - 6, cy - 16, xs[k] + ws[k] + 6, cy + 16);
            unsigned c = !r.enabled ? (sel ? t.ash : t.dim) : sel ? t.bone : over ? t.ash : t.dim;
            text(f, fonts.label, size, xs[k], y, c, names[k], 0.2f);
            if (sel)
                f.dl->AddRectFilled(f.p(xs[k], cy + 11), f.p(xs[k] + ws[k], cy + 13), r.enabled ? t.accent : t.dim);
            if (over && m.clicked && !sel) {
                pc_ui_shell_system_value(i, 0, k, 1);
                m.clicked = false;
            }
        }
        break;
    }
    case PC_UI_SET_SLIDER: {
        float size = 14, vt = 86;
        text_right(f, fonts.data, size, x1, cy - size * 0.5f - 1, col, r.value);
        float b0 = x0 + 4, b1 = x1 - vt - 10;
        float span = r.max - r.min;
        float k = span > 0 ? (r.number - r.min) / span : 0;
        k = k < 0 ? 0 : k > 1 ? 1 : k;
        f.dl->AddRectFilled(f.p(b0, cy - 2), f.p(b1, cy + 2), t.rule2, f.s(2));
        f.dl->AddRectFilled(f.p(b0, cy - 2), f.p(b0 + (b1 - b0) * k, cy + 2), r.enabled ? t.accent : t.dim, f.s(2));
        float kx = b0 + (b1 - b0) * k;
        f.dl->AddRectFilled(f.p(kx - 3, cy - 8), f.p(kx + 3, cy + 8), r.enabled ? (focused ? t.bone : t.ash) : t.dim,
            f.s(1.5f));
        // click or drag on the bar sets the value (the shell snaps it to the step)
        bool over = hit_ok && r.enabled && m.over(b0 - 8, cy - 14, b1 + 8, cy + 14);
        if (over && (m.clicked || (m.rpc && m.down && anim.drag_row < 0))) {
            anim.drag_row = i;
            anim.drag_rpc = m.rpc;
            anim.drag_page = anim.shown_page;
        }
        if (anim.drag_row == i && m.ok && m.down && r.enabled && b1 > b0) {
            float v = (m.x - b0) / (b1 - b0);
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            anim.drag_value = r.min + v * span, anim.dragged = true;
            pc_ui_shell_system_value(i, 0, anim.drag_value, 2); // previewed; applied when the drag ends
            m.clicked = false;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        break;
    }
    case PC_UI_SET_ACTION:
        if (focused && r.enabled) {
            float gw = glyph(f, PC_PROMPT_CIRCLE, 24, 0, 0, true);
            glyph(f, PC_PROMPT_CIRCLE, 24, x1 - gw, cy);
        } else
            chevron(f, x1 - 8, cy, 1, r.enabled ? t.ash : t.dim);
        break;
    case PC_UI_SET_BINDING: {
        if (capture) {
            float a = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 6.0f);
            text_right(f, fonts.label, 12, x1, cy - 7, fade(t.gold, a), "LISTENING\xe2\x80\xa6", 0.22f);
            break;
        }
        float x = x1;
        bool pad_none = !r.pad[0] || !std::strcmp(r.pad, "None");
        if (pad_none)
            x -= text_right(f, fonts.data, 12.5f, x, cy - 7, t.dim, "None");
        else
            x -= cap(f, t, x, cy, r.pad, r.enabled, true);
        x -= 14;
        std::vector<std::string> keys = split_keys(r.keys);
        if (keys.empty() || (keys.size() == 1 && keys[0] == "None"))
            text_right(f, fonts.data, 12.5f, x, cy - 7, t.dim, "None");
        else
            for (size_t k = keys.size(); k-- > 0;) {
                if (x - measure(f, fonts.data, 12.5f, keys[k].c_str()) - 16 < x0)
                    break; // what does not fit is in the help text's JSON / the editor
                x -= cap(f, t, x, cy, keys[k].c_str(), r.enabled, false) + 6;
            }
        break;
    }
    case PC_UI_SET_LINK: // another page: its title and an arrow
        text_right(f, fonts.name, 15, x1 - 22, cy - 9.5f, r.enabled ? t.ash : t.dim, r.value);
        chevron(f, x1 - 8, cy, 1, r.enabled ? (focused ? t.bone : t.ash) : t.dim);
        break;
    case PC_UI_SET_INFO: {
        std::string v = fit(f, fonts.name, 15, r.value, x1 - x0);
        text_right(f, fonts.name, 15, x1, cy - 9.5f, t.ash, v.c_str());
        break;
    }
    default:
        break;
    }
}

// The capture's prompt over the whole layout; a click anywhere cancels it.
void draw_capture(const Frame& f, const ui::Theme& t, int button, const char* label, Pointer& m)
{
    f.dl->AddRectFilled(f.p(0, 0), f.p(f.W, 720), fade(t.ground | 0xFF000000u, 0.72f));
    float w = 460, h = 150, x0 = (f.W - w) * 0.5f, y0 = 260;
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x0 + w, y0 + h), t.panel);
    f.dl->AddRect(f.p(x0, y0), f.p(x0 + w, y0 + h), t.rule2, 0, 0, f.s(1));
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x0 + w, y0 + 2), t.accent);
    {
        const char* head = "REBIND";
        float hw = measure(f, fonts.label, t.label_size, head, 0.26f);
        text(f, fonts.label, t.label_size, (f.W - hw) * 0.5f, y0 + 20, t.ash, head, 0.26f);
    }
    std::string line = std::string("Press a key or button for ") + label + "\xe2\x80\xa6";
    float size = 22;
    float gw = glyph(f, button, 30, 0, 0, true);
    float lw = measure(f, fonts.name, size, line.c_str());
    float x = (f.W - lw - (gw > 0 ? gw + 12 : 0)) * 0.5f, cy = y0 + 66;
    if (gw > 0) {
        glyph(f, button, 30, x, cy);
        x += gw + 12;
    }
    text(f, fonts.name, size, x, cy - size * 0.5f - 3, t.bone, line.c_str());
    // a reserved key pressed: why it cannot be used (the capture goes on)
    const char* refused = pc_ui_settings_capture_note();
    std::string sub = refused ? fit(f, fonts.name, 14, refused, w - 24)
                              : std::string("(Esc cancels; so does a click, or 5 s without the gamepad)");
    float sw = measure(f, fonts.name, 14, sub.c_str());
    text(f, fonts.name, 14, (f.W - sw) * 0.5f, y0 + 104, refused ? t.gold : t.ash, sub.c_str());
    m.hit(PC_UI_MOUSE_BUTTON, PC_UI_BACK, 0, 0, f.W, 720);
}

} // namespace

// "Keep these settings?" over the whole menu (display_confirm.h): what changed, the seconds
// left, Keep and Revert (the shell's modal: pc_ui_shell_confirm_focus, PC_UI_MOUSE_CONFIRM).
void draw_display_confirm(const Frame& f, const ui::Theme& t, Pointer& m, ScreenOut& out)
{
    f.dl->AddRectFilled(f.p(0, 0), f.p(f.W, 720), fade(t.ground | 0xFF000000u, 0.72f));
    m.hit(PC_UI_MOUSE_CONFIRM, 2, 0, 0, f.W, 720); // the rest of the screen takes nothing
    float w = 520, h = 180, x0 = (f.W - w) * 0.5f, y0 = 260;
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x0 + w, y0 + h), t.panel);
    f.dl->AddRect(f.p(x0, y0), f.p(x0 + w, y0 + h), t.rule2, 0, 0, f.s(1));
    f.dl->AddRectFilled(f.p(x0, y0), f.p(x0 + w, y0 + 2), t.accent);
    text(f, fonts.name, 25, x0 + 24, y0 + 18, t.bone, "Keep these settings?");
    std::string what;
    for (int i = 0; i < pc_display_confirm_count(); ++i) {
        pc_ui_setting_row r;
        int c = pc_ui_settings_catalog_index(pc_display_confirm_key(i));
        what += i ? ", " : "";
        what += c >= 0 && pc_ui_settings_catalog_row(c, &r) ? r.label : pc_display_confirm_key(i);
    }
    int secs = (int)std::ceil(pc_display_confirm_remaining());
    char line[256];
    std::snprintf(line, sizeof line, "%s changed. %s back in %d second%s unless you keep %s.", what.c_str(),
        pc_display_confirm_count() > 1 ? "They go" : "It goes", secs, secs == 1 ? "" : "s",
        pc_display_confirm_count() > 1 ? "them" : "it");
    paragraph(f, t, x0 + 24, y0 + 60, w - 48, t.ash, line);
    float bw = 140, bh = 40, by = y0 + h - 60, gx = x0 + w - 24 - bw * 2 - 12;
    int focus = pc_ui_shell_confirm_focus();
    for (int i = 0; i < 2; ++i) {
        float bx = gx + i * (bw + 12);
        bool o = m.hit(PC_UI_MOUSE_CONFIRM, i, bx, by, bx + bw, by + bh);
        f.dl->AddRectFilled(f.p(bx, by), f.p(bx + bw, by + bh), focus == i ? t.accent : o ? t.rule2 : t.rule, f.s(5));
        const char* s = i == 0 ? "KEEP" : "REVERT";
        float tw = measure(f, fonts.label, 15, s, 0.26f);
        text(f, fonts.label, 15, bx + (bw - tw) * 0.5f, by + (bh - 15) * 0.5f - 1, t.bone, s, 0.26f);
    }
    out.hint_count = 0;
    out.hint(PC_PROMPT_CIRCLE, -1, focus == 0 ? "Keep" : "Revert", PC_UI_CONFIRM);
    out.hint(PC_PROMPT_CROSS, -1, "Revert", PC_UI_BACK);
}

void draw_system(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    if (pc_ui_shell_manual_shown()) { // the Quick manual has the screen (ui_manual_draw.cpp)
        draw_manual(f, t, top, dt, fresh, m, out);
        return;
    }
    pc_ui_system_view v;
    pc_ui_shell_system_view(&v);
    bool capture = v.capture >= 0;
    bool hit_ok = !capture;
    // A drag ends when its source lets go (a test pointer lasts one frame per
    // call: its drag ends with a call that is not down) or the other source
    // takes the pointer; then its value applies.
    bool ended = anim.drag_rpc ? (m.rpc && !m.down) || (!m.rpc && m.ok && (m.down || m.moved))
                               : (!m.rpc && !m.down) || (m.rpc && m.ok);
    // (the screen or page changed under it: dropped; leaving the page applied it, pc_ui_settings_flush)
    bool stale = fresh || v.page != anim.drag_page;
    anim.shown_page = v.page;
    if (anim.drag_row >= 0 && (ended || stale)) {
        if (anim.dragged && !stale)
            pc_ui_shell_system_value(anim.drag_row, 0, anim.drag_value, 1);
        anim.drag_row = -1, anim.dragged = false;
    }

    // ---- the list ----
    // (scrolled when the entries are taller than the screen: many plugins' pages; the focused
    // entry stays in view with one entry of margin where there is one)
    float lx = t.margin_x, lw = list_width(f.W), y = top;
    const char *id, *label, *help, *reason, *focus_label = "", *focus_help = "", *focus_reason = nullptr,
                                               *focus_id = "";
    int enabled;
    {
        float h = 0, fy = 0;
        int n = 0;
        for (; pc_ui_shell_system_item(n, &id, &label, &help, &enabled, &reason); ++n) {
            if (n == 0 || n == v.games)
                h += (n ? 14 : 0) + 24;
            if (n == v.item)
                fy = h;
            h += ENTRY_H;
        }
        float room = BOTTOM - 8 - top, target = anim.list_scroll;
        float max_scroll = h > room ? h - room : 0;
        if (fy - ENTRY_H < target)
            target = fy - ENTRY_H;
        if (fy + ENTRY_H * 2 > target + room)
            target = fy + ENTRY_H * 2 - room;
        target = target < 0 ? 0 : target > max_scroll ? max_scroll : target;
        anim.list_scroll = fresh ? target : approach(anim.list_scroll, target, dt, t.focus_grow);
        anim.list_max = max_scroll;
        y -= anim.list_scroll;
        f.dl->PushClipRect(f.p(lx - 4, top - 4), f.p(lx + lw + 4, BOTTOM - 4), true);
        if (max_scroll > 0) { // where the list is: a thin bar at its right edge
            float th = room * room / h, tx = lx + lw + 6;
            float ty = top + (room - th) * (anim.list_scroll / max_scroll);
            f.dl->AddRectFilled(f.p(tx, top), f.p(tx + 2, top + room), t.rule);
            f.dl->AddRectFilled(f.p(tx, ty), f.p(tx + 2, ty + th), t.ash);
        }
    }
    float list_top = top, list_bottom = BOTTOM - 8;
    for (int i = 0; pc_ui_shell_system_item(i, &id, &label, &help, &enabled, &reason); ++i) {
        if (i == 0 || i == v.games) {
            if (i)
                y += 14;
            uidraw::label(f, t, lx + 16, y + 4, i < v.games ? "Game" : "Settings", t.dim);
            y += 24;
        }
        bool focus = i == v.item;
        if (focus && !v.entered)
            focus_band(f, t, lx, y, lx + lw, y + ENTRY_H);
        else if (focus) // the page that is open: its bar only
            f.dl->AddRectFilled(f.p(lx, y + 8), f.p(lx + 2, y + ENTRY_H - 8), t.accent);
        if (hit_ok && y >= list_top - 1 && y + ENTRY_H <= list_bottom + 1) // whole entries take the pointer
            m.hit(PC_UI_MOUSE_PAGE, i, lx, y, lx + lw, y + ENTRY_H);
        unsigned c = !enabled ? t.dim : focus ? t.bone : v.entered ? t.ash : t.bone;
        text(f, fonts.name, 18, lx + 16, y + (ENTRY_H - 18) * 0.5f - 2, c, label);
        if (i >= v.games)
            chevron(f, lx + lw - 16, y + ENTRY_H * 0.5f, 1, focus ? t.ash : t.dim, 0.8f);
        if (focus)
            focus_label = label, focus_help = help, focus_reason = reason, focus_id = id;
        y += ENTRY_H;
    }
    f.dl->PopClipRect();

    // ---- the card ----
    float cx = lx + lw + 28, cw = f.W - t.margin_x - cx;
    Card card = {cx, top, cw};
    card_begin(f);
    float x0 = cx + 22, x1 = cx + cw - 22;
    out.crumb = "System";
    if (v.page < 0) { // a game entry: what it opens
        text(f, fonts.name, 25, x0, top + 18, t.bone, focus_label);
        float h = paragraph(f, t, x0, top + 60, x1 - x0, t.ash, focus_help);
        float by = top + 60 + h + 14;
        f.dl->AddLine(f.p(x0, by), f.p(x1, by), t.rule, f.s(1));
        bool manual = !std::strcmp(focus_id, "manual"); // read here (ui_manual_draw.cpp); Square: the original
        uidraw::label(f, t, x0, by + 14, manual ? "Read here, or on the game's own screen" : "Opens the game's own screen",
            t.dim);
        by += 38;
        if (focus_reason) {
            f.dl->AddRectFilled(f.p(x0, by), f.p(x0 + 2, by + t.body_size + 4), t.gold);
            by += paragraph(f, t, x0 + 10, by, x1 - x0 - 10, t.gold, focus_reason) + 10;
        }
        card_end(f, t, card, by + 10);
        out.crumb += std::string(" > ") + focus_label;
        out.hint(PC_PROMPT_CIRCLE, -1, manual ? "Read" : "Open", PC_UI_CONFIRM);
        if (manual)
            out.hint(PC_PROMPT_SQUARE, -1, "Original screen", PC_UI_ALT);
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
        anim.page = -1;
        return;
    }

    const char* title = pc_ui_settings_page_title(v.page);
    out.crumb += std::string(" > ") + title;
    text(f, fonts.name, 25, x0, top + 16, t.bone, title);
    float ry0 = top + HEAD_H;
    f.dl->AddLine(f.p(cx + 1, ry0 - 6), f.p(cx + cw - 1, ry0 - 6), t.rule, f.s(1));

    // scroll: the focused row stays in view (a margin of one row where there is one); the
    // rows' area holds whole rows, the help takes what is left
    int visible = (int)((BOTTOM - HELP_H - ry0) / ROW_H);
    float ry1 = ry0 + visible * ROW_H;
    float max_scroll = v.rows > visible ? (v.rows - visible) * ROW_H : 0;
    if (fresh || anim.page != v.page)
        anim.scroll = v.entered ? anim.scroll : 0;
    float target = anim.scroll;
    float fy = v.row * ROW_H;
    if (fy - target < ROW_H && v.row > 0)
        target = fy - ROW_H;
    if (fy + ROW_H * 2 - target > visible * ROW_H && v.row < v.rows - 1)
        target = fy + ROW_H * 2 - visible * ROW_H;
    if (fy < target)
        target = fy;
    if (fy + ROW_H > target + visible * ROW_H)
        target = fy + ROW_H - visible * ROW_H;
    target = target < 0 ? 0 : target > max_scroll ? max_scroll : target;
    if (!v.entered && v.row == 0)
        target = 0;
    anim.scroll = anim.page != v.page ? target : approach(anim.scroll, target, dt, t.focus_grow);
    anim.page = v.page;

    // the rows
    float vw = std::fmin(f.W < 1100 ? 300.0f : 340.0f, (x1 - x0) * 0.47f);
    float vx0 = x1 - vw;
    pc_ui_setting_row focus_row = {};
    bool have_focus = false;
    const char* capture_label = "";
    f.dl->PushClipRect(f.p(cx, ry0 - 4), f.p(cx + cw, ry1 + 2), true);
    for (int i = 0; i < v.rows; ++i) {
        pc_ui_setting_row r;
        if (!pc_ui_settings_row(v.page, i, &r))
            break;
        bool focus = i == v.row;
        if (focus)
            focus_row = r, have_focus = true;
        float ry = ry0 + i * ROW_H - anim.scroll;
        if (ry + ROW_H < ry0 - 4 || ry > ry1 + 2)
            continue;
        bool shown = ry >= ry0 - 2 && ry + ROW_H <= ry1 + 2; // whole: takes the pointer
        float cy = ry + ROW_H * 0.5f;
        if (focus && v.entered)
            focus_band(f, t, cx + 1, ry, cx + cw - 1, ry + ROW_H);
        bool row_hit = hit_ok && shown && m.over(cx, ry, cx + cw, ry + ROW_H);
        if (row_hit)
            m.hit(PC_UI_MOUSE_SETTING, i, cx, ry, cx + cw, ry + ROW_H);
        if (r.kind == PC_UI_SET_HEADER || r.kind == PC_UI_SET_NOTE) { // a page's section title or help line
            std::string line = fit(f, fonts.name, r.kind == PC_UI_SET_HEADER ? 17 : 14, r.label, x1 - x0);
            if (r.kind == PC_UI_SET_HEADER)
                uidraw::label(f, t, x0, cy - 6, line.c_str(), t.gold);
            else
                text(f, fonts.name, 14, x0, cy - 9, t.ash, line.c_str());
            continue;
        }
        // label (a binding: the PS1 button's icon first)
        float lx0 = x0;
        if (r.kind == PC_UI_SET_BINDING && r.button >= 0) {
            float gw = glyph(f, r.button, 24, lx0, cy);
            lx0 += (gw > 0 ? gw : 0) + 10;
        }
        unsigned lc = r.enabled ? t.bone : t.dim;
        float lmax = vx0 - 16 - lx0;
        std::string lab = fit(f, fonts.name, 17, r.label, lmax);
        if (r.reason[0]) {
            text(f, fonts.name, 17, lx0, ry + 4, lc, lab.c_str());
            std::string why = fit(f, fonts.name, 12.5f, r.reason, lmax);
            text(f, fonts.name, 12.5f, lx0, ry + 24, r.enabled ? t.gold : fade(t.gold, 0.7f), why.c_str());
        } else
            text(f, fonts.name, 17, lx0, cy - 8.5f - 2, lc, lab.c_str());
        // wheel on a slider or choice of the open page changes it
        if (row_hit && v.entered && r.enabled && m.wheel != 0
            && (r.kind == PC_UI_SET_SLIDER || r.kind == PC_UI_SET_CHOICE || r.kind == PC_UI_SET_TOGGLE)) {
            anim.wheel += m.wheel;
            int steps = (int)anim.wheel;
            if (steps) {
                anim.wheel -= steps;
                pc_ui_shell_system_value(i, steps > 0 ? 1 : -1, 0, 0);
            }
            m.wheel = 0;
        }
        bool capturing_row = capture && r.kind == PC_UI_SET_BINDING && r.button == v.capture;
        draw_value(f, t, r, i, vx0, x1, cy, focus && v.entered, capturing_row, hit_ok && shown, m);
        if (i + 1 < v.rows && !(focus && v.entered))
            f.dl->AddLine(f.p(x0, ry + ROW_H), f.p(x1, ry + ROW_H), t.rule, f.s(1));
    }
    f.dl->PopClipRect();
    if (capture)
        pc_ui_settings_binding(v.capture, nullptr, &capture_label);
    // a scroll bar when the page is longer than the card
    if (max_scroll > 0) {
        float th = (ry1 - ry0) * visible / (float)v.rows, tx = cx + cw - 6;
        float ty = ry0 + (ry1 - ry0 - th) * (anim.scroll / max_scroll);
        f.dl->AddRectFilled(f.p(tx, ry0), f.p(tx + 2, ry1), t.rule);
        f.dl->AddRectFilled(f.p(tx, ty), f.p(tx + 2, ty + th), t.ash);
    }

    // help, reason and status
    float hy = ry1 + 6;
    f.dl->AddLine(f.p(cx + 1, hy), f.p(cx + cw - 1, hy), t.rule, f.s(1));
    const char* hint = v.entered && have_focus ? focus_row.help : focus_help;
    f.dl->PushClipRect(f.p(cx, hy + 1), f.p(cx + cw, BOTTOM - 26), true);
    float hh = paragraph(f, t, x0, hy + 12, x1 - x0, t.ash, hint ? hint : "");
    if (v.entered && have_focus && focus_row.reason[0]) {
        float ry = hy + 12 + hh + 6;
        f.dl->AddRectFilled(f.p(x0, ry + 1), f.p(x0 + 2, ry + 16), t.gold);
        std::string why = fit(f, fonts.name, 13.5f, focus_row.reason, x1 - x0 - 10);
        text(f, fonts.name, 13.5f, x0 + 10, ry, t.gold, why.c_str());
    }
    f.dl->PopClipRect();
    std::string status = pc_ui_settings_dirty() ? std::string("Saving\xe2\x80\xa6") : short_status(pc_settings_status());
    if (v.entered && have_focus && focus_row.game_option)
        status = "Saved with your game";
    const char* note = pc_ui_settings_note(); // a binding swap moved a key or button (3 s)
    if (note)
        status = note;
    std::string st = fit(f, fonts.name, 12, status.c_str(), x1 - x0);
    text(f, fonts.name, 12, x0, BOTTOM - 22, note ? t.gold : t.dim, st.c_str());
    card_end(f, t, card, BOTTOM);

    // hints
    if (capture) {
        draw_capture(f, t, v.capture, capture_label, m);
        return;
    }
    if (!v.entered) {
        out.hint(PC_PROMPT_CIRCLE, -1, "Enter", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
        return;
    }
    if (have_focus) {
        int k = focus_row.kind;
        if (focus_row.enabled && (k == PC_UI_SET_CHOICE || k == PC_UI_SET_TOGGLE || k == PC_UI_SET_SLIDER))
            out.hint(PC_PROMPT_LEFT, PC_PROMPT_RIGHT, "Change", PC_UI_RIGHT);
        else if (focus_row.enabled && k == PC_UI_SET_ACTION)
            out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
        else if (focus_row.enabled && k == PC_UI_SET_BINDING)
            out.hint(PC_PROMPT_CIRCLE, -1, "Rebind", PC_UI_CONFIRM);
        out.crumb += std::string(" > ") + focus_row.label;
    }
    out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
    out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
}

} // namespace uidraw
