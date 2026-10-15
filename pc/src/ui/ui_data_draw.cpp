// Modern menus, the native Data screen (data_frame.h): everything drawn
// natively from the model MENU7 publishes (nothing of MENU7's own screen
// shows: its drawing is gated). The shell's backdrop and tabs, a content
// panel as see-through as the other tabs (ui_menu_opacity: the ground and
// the panels as the theme scales them): a header (Save / Load, the reason
// Save is off, the play time), the memory cards and this journey (16:9: a
// left rail; 4:3: a strip in the header), the five saves of the scanned
// card, the status line (MENU7's message), a Yes / No dialog for MENU7's
// prompts, and the load's fade (over the footer too). Focus mirrors MENU7's
// cursor; the pointer's hover is a separate soft highlight; clicks are goals
// and the wheel walks MENU7's cursor (ui_data.cpp). Layout units 1280 x 720 (16:9)
// or 960 x 720 (4:3).
#include "ui/ui_draw_internal.h"
#include "ui/data_frame.h"
#include "button_prompts.h"
#include <cmath>
#include <cstdio>
#include <string>

extern "C" int pc_gl_view[4];

namespace uidraw {
namespace {

unsigned rgb(int r, int g, int b, float a = 1) { return fade(0xFF000000u | (b << 16) | (g << 8) | r, a); }

// MENU7's own colours (260.c): the gauges (_drawHPMP), the progress sweeps (_fileProcessingAnim)
const unsigned HP = rgb(45, 224, 207), MP = rgb(220, 120, 159);
const unsigned SAVING = rgb(255, 0, 127), LOADING = rgb(224, 255, 0);

int active_file = -1; // the file MENU7's cursor was last on (its progress and flash after the cursor hides)

struct Layout {
    float W;
    bool wide;
    float px0, py0, px1, py1;           // the content panel
    float list_x, list_w, list_y, card_h, gap; // the saves
    float status_y, status_h;
    float rail_x, rail_w;               // 16:9: the rail
};

Layout layout(float W)
{
    Layout L;
    L.W = W;
    L.wide = W >= 1100;
    L.px0 = L.wide ? 48 : 24, L.px1 = W - L.px0, L.py0 = 88, L.py1 = 672;
    L.card_h = 76, L.gap = 12, L.list_y = 184;
    if (L.wide) {
        L.rail_x = 64, L.rail_w = 280;
        L.list_x = 376, L.list_w = W - 64 - 376;
    } else {
        L.rail_x = 40, L.rail_w = 0;
        L.list_x = 40, L.list_w = W - 80;
    }
    L.status_y = 624, L.status_h = 44;
    return L;
}

bool hit(Pointer& m, int kind, int index, float x0, float y0, float x1, float y1)
{
    pc_ui_data_hit_record(kind, index, x0, y0, x1, y1);
    return m.hit(kind, index, x0, y0, x1, y1);
}

std::string hms(int h, int m, int s)
{
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d:%02d", h, m, s);
    return b;
}

// A gauge: the bar w x 6 and "cur/max" right of a label.
void gauge(const Frame& f, const ui::Theme& t, float x, float y, float w, const char* name, int cur, int max,
    unsigned col, float a)
{
    char b[32];
    text(f, fonts.label, 13, x, y - 2, fade(t.ash, a), name, 0.2f);
    float bx = x + 26;
    f.dl->AddRectFilled(f.p(bx, y + 4), f.p(bx + w, y + 10), fade(t.rule2, a));
    float k = max > 0 ? (float)cur / (float)max : 0;
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    f.dl->AddRectFilled(f.p(bx, y + 4), f.p(bx + w * k, y + 10), fade(col, a));
    std::snprintf(b, sizeof b, "%d/%d", cur, max);
    text(f, fonts.data, 13, bx + w + 8, y - 1, fade(t.bone, a), b);
}

// A chip: a small capitals label in a rounded frame; returns its width.
float chip(const Frame& f, const ui::Theme& t, float xr, float y, const char* s, unsigned col, bool measure_only = false)
{
    std::string u = upper(s);
    float w = measure(f, fonts.label, 11, u.c_str(), 0.24f) + 16;
    if (!measure_only) {
        f.dl->AddRect(f.p(xr - w, y), f.p(xr, y + 20), col, f.s(3), 0, f.s(1));
        text(f, fonts.label, 11, xr - w + 8, y + 4, col, u.c_str(), 0.24f);
    }
    return w;
}

const char* phase_crumb(const pc_ui_data_model& d)
{
    return d.op == 1 ? "System \xe2\x80\xba Data \xe2\x80\xba Save" : d.op == 2 ? "System \xe2\x80\xba Data \xe2\x80\xba Load"
                                                                          : "System \xe2\x80\xba Data";
}

// Why Save is off (the chooser's grey row), or none.
const char* save_reason(const pc_ui_data_model& d)
{
    if (!d.published || d.state < 6)
        return nullptr;
    if (d.warp_blocked)
        return "Saving is off after a room warp";
    if (d.in_battle && d.at_point)
        return "Cannot save during battle";
    if (!d.at_point)
        return "Only at a save point";
    if (d.in_battle)
        return "Cannot save during battle";
    return nullptr;
}

int severity(int msg) // 0 info, 1 progress, 2 success, 3 error
{
    switch (msg) {
    case PC_UI_DATA_MSG_CHECKING:
    case PC_UI_DATA_MSG_SAVING:
    case PC_UI_DATA_MSG_LOADING:
    case PC_UI_DATA_MSG_ACCESSING0:
    case PC_UI_DATA_MSG_ACCESSING1:
    case PC_UI_DATA_MSG_FORMATTING:
        return 1;
    case PC_UI_DATA_MSG_SAVED:
    case PC_UI_DATA_MSG_LOADED:
        return 2;
    case PC_UI_DATA_MSG_INSERT_ERROR:
    case PC_UI_DATA_MSG_EMPTY_CARD:
    case PC_UI_DATA_MSG_SAVE_FAILED:
    case PC_UI_DATA_MSG_LOAD_FAILED:
    case PC_UI_DATA_MSG_FILE_CORRUPT:
    case PC_UI_DATA_MSG_NO_DATA:
    case PC_UI_DATA_MSG_CARD_FULL:
    case PC_UI_DATA_MSG_FORMAT_FAILED:
    case PC_UI_DATA_MSG_REMOVED:
        return 3;
    default:
        return 0;
    }
}

void sev_icon(const Frame& f, const ui::Theme& t, int sev, float cx, float cy)
{
    if (sev == 1) {
        spinner(f, cx, cy, 6, t.gold);
    } else if (sev == 2) {
        f.dl->AddCircle(f.p(cx, cy), f.s(8), t.up, 20, f.s(1.6f));
        f.dl->AddLine(f.p(cx - 4, cy), f.p(cx - 1, cy + 3), t.up, f.s(1.8f));
        f.dl->AddLine(f.p(cx - 1, cy + 3), f.p(cx + 4, cy - 3), t.up, f.s(1.8f));
    } else if (sev == 3) {
        f.dl->AddTriangle(f.p(cx, cy - 8), f.p(cx + 8, cy + 6), f.p(cx - 8, cy + 6), t.down, f.s(1.6f));
        f.dl->AddLine(f.p(cx, cy - 3), f.p(cx, cy + 1), t.down, f.s(1.6f));
        f.dl->AddCircleFilled(f.p(cx, cy + 4), f.s(1.1f), t.down);
    } else {
        f.dl->AddCircle(f.p(cx, cy), f.s(8), t.ash, 20, f.s(1.4f));
        f.dl->AddLine(f.p(cx, cy - 1), f.p(cx, cy + 4), t.ash, f.s(1.6f));
        f.dl->AddCircleFilled(f.p(cx, cy - 4), f.s(1.1f), t.ash);
    }
}

// The Save | Load control (a click: that operation).
void segmented(const Frame& f, const ui::Theme& t, const pc_ui_data_model& d, float x, float y, float w, float h,
    Pointer& m)
{
    int hk, hi;
    pc_ui_data_hover(&hk, &hi);
    bool ops = d.phase == PC_UI_DATA_PH_OPS;
    int lit = ops ? d.ops_row : d.op - 1;
    f.dl->AddRect(f.p(x, y), f.p(x + w, y + h), t.rule2, f.s(4), 0, f.s(1));
    for (int i = 0; i < 2; ++i) {
        float x0 = x + i * w * 0.5f, x1 = x0 + w * 0.5f;
        bool off = i == 0 && (!d.save_ok || d.warp_blocked) && d.published && d.state >= 6;
        bool over = hit(m, PC_UI_MOUSE_DATA_OP, i + 1, x0, y, x1, y + h);
        if (i == lit)
            f.dl->AddRectFilled(f.p(x0 + 2, y + 2), f.p(x1 - 2, y + h - 2), ops ? t.accent : t.rule2, f.s(3));
        else if (over || (hk == PC_UI_MOUSE_DATA_OP && hi == i + 1))
            f.dl->AddRectFilled(f.p(x0 + 2, y + 2), f.p(x1 - 2, y + h - 2), t.accent_soft, f.s(3));
        const char* s = i == 0 ? "SAVE" : "LOAD";
        float tw = measure(f, fonts.label, 14, s, 0.26f);
        text(f, fonts.label, 14, (x0 + x1 - tw) * 0.5f, y + (h - 14) * 0.5f - 1, off ? t.dim : t.bone, s, 0.26f);
    }
}

// A memory card pill (a click: that card).
void card_pill(const Frame& f, const ui::Theme& t, const pc_ui_data_model& d, int n, float x, float y, float w,
    float h, bool compact, Pointer& m)
{
    int hk, hi;
    pc_ui_data_hover(&hk, &hi);
    bool over = hit(m, PC_UI_MOUSE_DATA_CARD, n, x, y, x + w, y + h);
    bool cursor = d.phase == PC_UI_DATA_PH_CARD && d.card == n;
    bool active = d.card_sel == n && d.op;
    bool soft = over || (hk == PC_UI_MOUSE_DATA_CARD && hi == n);
    f.dl->AddRectFilled(f.p(x, y), f.p(x + w, y + h), soft ? t.rule2 : t.panel, f.s(6));
    f.dl->AddRect(f.p(x, y), f.p(x + w, y + h), cursor ? t.accent : t.rule2, f.s(6), 0, f.s(cursor ? 2.0f : 1.0f));
    char title[32];
    std::snprintf(title, sizeof title, compact ? "Card %d" : "Memory Card %d", n);
    text(f, fonts.name, compact ? 17 : 20, x + 14, y + (compact ? 4 : 7), cursor || active ? t.bone : t.ash, title);
    int st = d.card_status[n - 1];
    char sub[48];
    if (st == -2)
        std::snprintf(sub, sizeof sub, "No card");
    else if (st >= 0)
        std::snprintf(sub, sizeof sub, st == 1 ? "1 save" : "%d saves", st);
    else
        std::snprintf(sub, sizeof sub, "Not checked");
    text(f, fonts.label, compact ? 12 : 14, x + 14, y + h - (compact ? 18 : 22), st == -2 ? t.down : t.dim, sub, 0.04f);
    if (active) { // a check mark
        float cx = x + w - 20, cy = y + h * 0.5f;
        f.dl->AddLine(f.p(cx - 6, cy), f.p(cx - 2, cy + 4), t.gold, f.s(2));
        f.dl->AddLine(f.p(cx - 2, cy + 4), f.p(cx + 6, cy - 5), t.gold, f.s(2));
    }
}

// "This journey": what a save now records (live globals, read only).
float journey(const Frame& f, const ui::Theme& t, const pc_ui_data_model& d, float x, float y, float w, float a)
{
    label(f, t, x, y, "This journey", fade(t.ash, a));
    text(f, fonts.name, 22, x, y + 20, fade(t.bone, a), d.location_name[0] ? d.location_name : "Lea Monde");
    std::string play = "Play time " + hms(d.play_h, d.play_m, d.play_s);
    text(f, fonts.label, 15, x, y + 52, fade(t.ash, a), play.c_str(), 0.04f);
    float gw = w - 26 - 70;
    gauge(f, t, x, y + 80, gw, "HP", d.hp, d.hp_max, HP, a);
    gauge(f, t, x, y + 102, gw, "MP", d.mp, d.mp_max, MP, a);
    char b[64];
    std::snprintf(b, sizeof b, "MAP %d%%  \xc2\xb7  SAVES %04d", d.map, d.saves < 9999 ? d.saves : 9999);
    text(f, fonts.label, 14, x, y + 128, fade(t.ash, a), b, 0.08f);
    return 150;
}

bool selectable(const pc_ui_data_model& d, const pc_ui_data_file& file)
{
    return d.op == 1 ? file.state != 0 : file.state >= 3;
}

// One save of the scanned card.
void save_card(const Frame& f, const ui::Theme& t, const pc_ui_data_model& d, int i, float x, float y, float w,
    float h, Pointer& m)
{
    const pc_ui_data_file& file = d.files[i];
    int hk, hi;
    pc_ui_data_hover(&hk, &hi);
    bool over = hit(m, PC_UI_MOUSE_DATA_FILE, i, x, y, x + w, y + h);
    bool focus = (d.phase == PC_UI_DATA_PH_FILES && d.file == i) || (d.phase != PC_UI_DATA_PH_FILES && active_file == i
                                                                       && d.phase != PC_UI_DATA_PH_CARD);
    bool soft = over || (hk == PC_UI_MOUSE_DATA_FILE && hi == i);
    bool ok = selectable(d, file);
    float a = ok ? 1.0f : 0.45f;
    f.dl->AddRectFilled(f.p(x, y), f.p(x + w, y + h), soft && !focus ? t.rule : t.panel, f.s(6));
    if (focus)
        focus_band(f, t, x, y, x + w, y + h);
    f.dl->AddRect(f.p(x, y), f.p(x + w, y + h), focus ? t.accent : t.rule2, f.s(6), 0, f.s(focus ? 2.0f : 1.0f));
    // the progress sweep and the completion flash (MENU7's own, on its row)
    if (i == active_file && d.progress_counter > 0) {
        float k = (d.progress_x - 80) / 240.0f;
        k = k < 0 ? 0 : k > 1 ? 1 : k;
        unsigned c = d.op == 1 ? SAVING : LOADING;
        float xe = x + w * k;
        f.dl->AddRectFilledMultiColor(f.p(x, y + h - 5), f.p(xe, y + h - 1), c & 0x00FFFFFFu, c, c, c & 0x00FFFFFFu);
        f.dl->AddRectFilledMultiColor(f.p(x + (xe - x) * 0.6f, y), f.p(xe, y + h), c & 0x00FFFFFFu,
            fade(c, 0.22f), fade(c, 0.22f), c & 0x00FFFFFFu);
    } else if (i == active_file && d.progress_counter < 0) {
        float k = -d.progress_counter / 16.0f;
        f.dl->AddRectFilled(f.p(x, y), f.p(x + w, y + h), fade(0xFFFFFFFFu, 0.5f * k), f.s(6));
    }
    char no[16];
    std::snprintf(no, sizeof no, "No. %d", i + 1);
    text(f, fonts.data, 15, x + 16, y + 12, fade(t.ash, a), no);
    float nx = x + 84;
    // chips (right to left)
    float cx = x + w - 14, cy = y + 10;
    if (file.state >= 3) {
        if (file.newest)
            cx -= chip(f, t, cx, cy, "Newest", fade(t.gold, a)) + 6;
        if (file.current)
            cx -= chip(f, t, cx, cy, "Current", fade(t.up, a)) + 6;
    } else if (file.state == 2) {
        cx -= chip(f, t, cx, cy, "Temporary", fade(t.down, 1)) + 6;
    } else if (file.state == 1) {
        cx -= chip(f, t, cx, cy, d.op == 1 ? "New save" : "Empty", fade(d.op == 1 ? t.up : t.dim, 1)) + 6;
    } else {
        cx -= chip(f, t, cx, cy, "In use", fade(t.dim, 1)) + 6;
    }
    if (file.state >= 3) {
        std::string name = fit(f, fonts.name, 24, file.name[0] ? file.name : "Lea Monde", cx - nx - 12);
        text(f, fonts.name, 24, nx, y + 6, fade(t.bone, a), name.c_str());
        // the clear count's star is drawn (the bundled fonts have no U+2605): the line in three runs
        char head[64], tail[48];
        std::snprintf(head, sizeof head, "MAP %d%%  \xc2\xb7  SAVES %04d  \xc2\xb7  ", file.map, file.saves);
        std::snprintf(tail, sizeof tail, "TIME %s", hms(file.h, file.m, file.s).c_str());
        float lx = nx;
        text(f, fonts.label, 16, lx, y + 44, fade(t.ash, a), head, 0.04f);
        lx += measure(f, fonts.label, 16, head, 0.04f);
        if (file.clear > 0) {
            char clear[40];
            std::snprintf(clear, sizeof clear, "CLEAR %d  \xc2\xb7  ", file.clear);
            star(f, lx + 6, y + 44 + 8.5f, 6, fade(t.gold, a));
            lx += 16;
            text(f, fonts.label, 16, lx, y + 44, fade(t.ash, a), clear, 0.04f);
            lx += measure(f, fonts.label, 16, clear, 0.04f);
        }
        text(f, fonts.label, 16, lx, y + 44, fade(t.ash, a), tail, 0.04f);
        float gx = x + w - 14 - 26 - 120 - 70;
        if (gx > nx + 360) {
            gauge(f, t, gx, y + 36, 120, "HP", file.hp, file.hp_max, HP, a);
            gauge(f, t, gx, y + 54, 120, "MP", file.mp, file.mp_max, MP, a);
        }
    } else {
        const char* what = file.state == 2 ? "Unfinished save" : file.state == 1 ? (d.op == 1 ? "Empty slot" : "No data")
                                                                                 : "No room on the card";
        text(f, fonts.name, 22, nx, y + (h - 22) * 0.5f - 2, fade(t.dim, 1), what);
    }
}

void dialog(const Frame& f, const ui::Theme& t, const pc_ui_data_model& d, const Layout& L, Pointer& m)
{
    bool over = d.prompt == PC_UI_DATA_PROMPT_OVERWRITE;
    float w = over ? 640 : 520, h = over ? 300 : 220;
    if (w > L.W - 48)
        w = L.W - 48;
    float x = (L.W - w) * 0.5f, y = 360 - h * 0.5f;
    // the list dimmed behind
    f.dl->AddRectFilled(f.p(L.px0, L.py0), f.p(L.px1, L.py1), fade(0xFF000000u, 0.55f));
    hit(m, PC_UI_MOUSE_DATA_PANEL, 1, x, y, x + w, y + h);
    bool warn = d.prompt == PC_UI_DATA_PROMPT_CONTAINER || d.prompt == PC_UI_DATA_PROMPT_CONTAINER_LOSS
        || d.prompt == PC_UI_DATA_PROMPT_FORMAT;
    f.dl->AddRectFilled(f.p(x, y), f.p(x + w, y + h), t.panel, f.s(8));
    f.dl->AddRect(f.p(x, y), f.p(x + w, y + h), warn ? t.down : t.rule2, f.s(8), 0, f.s(warn ? 2.0f : 1.0f));
    if (warn)
        f.dl->AddRectFilled(f.p(x, y + 12), f.p(x + 3, y + h - 12), t.down);
    const char* title = d.prompt == PC_UI_DATA_PROMPT_OVERWRITE ? "Overwrite this save?"
        : d.prompt == PC_UI_DATA_PROMPT_FORMAT                  ? "Format the memory card?"
        : d.prompt == PC_UI_DATA_PROMPT_CONTAINER               ? "Container data"
        : d.prompt == PC_UI_DATA_PROMPT_CONTAINER_LOSS          ? "Discard the container's changes?"
                                                                : "Confirm";
    text(f, fonts.name, 26, x + 24, y + 18, t.bone, title);
    float by = y + 58;
    if (over && active_file >= 0 && active_file < PC_UI_DATA_FILES) {
        float cw = (w - 72) * 0.5f;
        journey(f, t, d, x + 24, by, cw, 1);
        const pc_ui_data_file& r = d.files[active_file];
        float rx = x + 48 + cw;
        f.dl->AddLine(f.p(rx - 12, by), f.p(rx - 12, by + 150), t.rule2, f.s(1));
        char no[32];
        std::snprintf(no, sizeof no, "Replaces No. %d", active_file + 1);
        label(f, t, rx, by, no, t.down);
        text(f, fonts.name, 22, rx, by + 20, t.bone, r.name[0] ? r.name : "Lea Monde");
        std::string play = "Play time " + hms(r.h, r.m, r.s);
        text(f, fonts.label, 15, rx, by + 52, t.ash, play.c_str(), 0.04f);
        gauge(f, t, rx, by + 80, cw - 96, "HP", r.hp, r.hp_max, HP, 1);
        gauge(f, t, rx, by + 102, cw - 96, "MP", r.mp, r.mp_max, MP, 1);
        char b[64];
        std::snprintf(b, sizeof b, "MAP %d%%  \xc2\xb7  SAVES %04d", r.map, r.saves);
        text(f, fonts.label, 14, rx, by + 128, t.ash, b, 0.08f);
        by += 162;
    } else {
        paragraph(f, t, x + 24, by, w - 48, t.ash, d.msg_text, 16);
    }
    // Yes / No: the focus is MENU7's cancelled (No first)
    float bw = 120, bh = 40, gy = y + h - bh - 20, gx = x + w - 24 - bw * 2 - 12;
    int hk, hi;
    pc_ui_data_hover(&hk, &hi);
    for (int i = 0; i < 2; ++i) {
        float x0 = gx + i * (bw + 12);
        int kind = i == 0 ? PC_UI_MOUSE_DATA_YES : PC_UI_MOUSE_DATA_NO;
        bool o = hit(m, kind, 0, x0, gy, x0 + bw, gy + bh) || hk == kind;
        bool focus = d.cancelled == i;
        f.dl->AddRectFilled(f.p(x0, gy), f.p(x0 + bw, gy + bh), focus ? t.accent : o ? t.rule2 : t.rule, f.s(5));
        const char* s = i == 0 ? "YES" : "NO";
        float tw = measure(f, fonts.label, 15, s, 0.26f);
        text(f, fonts.label, 15, x0 + (bw - tw) * 0.5f, gy + (bh - 15) * 0.5f - 1, t.bone, s, 0.26f);
    }
}

} // namespace

void draw_data_frame(const Frame& f, const ui::Theme& t, float dt, Pointer& m, ScreenOut& out)
{
    const pc_ui_data_model& d = *pc_ui_data_state();
    Layout L = layout(f.W);
    pc_ui_data_hits_clear();
    if (!d.files_valid)
        active_file = -1;
    else if (d.phase == PC_UI_DATA_PH_FILES && d.file >= 0)
        active_file = d.file;
    (void)dt;

    // the shell's backdrop, then the tabs (System lit) and the content panel's rule (the ground shows
    // through at the menus' opacity, as on the other tabs)
    ImVec2 v0((float)pc_gl_view[0], (float)pc_gl_view[1]);
    ImVec2 v1((float)(pc_gl_view[0] + pc_gl_view[2]), (float)(pc_gl_view[1] + pc_gl_view[3]));
    f.dl->AddRectFilled(v0, v1, t.ground);
    bool ops = d.phase == PC_UI_DATA_PH_OPS;
    draw_tabs(f, t, pc_ui_shell_tab(), m, dt, 2, ops);
    f.dl->AddRect(f.p(L.px0, L.py0), f.p(L.px1, L.py1), t.rule, f.s(8), 0, f.s(1));
    hit(m, PC_UI_MOUSE_DATA_PANEL, 0, L.px0, L.py0, L.px1, L.py1);

    bool loading = !d.framed || !d.published || d.state < 6;
    const char* title = d.op == 1 ? "Save game" : d.op == 2 ? "Load game" : "Data";
    const char* reason = save_reason(d);
    std::string play = "PLAY TIME " + hms(d.play_h, d.play_m, d.play_s);

    // ---- the header ----
    if (L.wide) {
        text(f, fonts.name, 34, 64, 100, t.bone, title);
        segmented(f, t, d, 300, 106, 280, 40, m);
        if (reason)
            text(f, fonts.label, 16, 300, 150, t.gold, reason, 0.02f);
        text_right(f, fonts.data, 18, L.W - 64, 116, t.ash, play.c_str());
    } else {
        text(f, fonts.name, 28, 40, 102, t.bone, title);
        segmented(f, t, d, 214, 104, 180, 36, m);
        if (reason)
            text(f, fonts.label, 13, 214, 144, t.gold, reason, 0.02f);
        card_pill(f, t, d, 1, 414, 100, 150, 46, true, m);
        card_pill(f, t, d, 2, 574, 100, 150, 46, true, m);
        text_right(f, fonts.data, 15, L.W - 40, 116, t.ash, play.c_str());
    }
    f.dl->AddLine(f.p(L.px0 + 16, 168), f.p(L.px1 - 16, 168), t.rule, f.s(1));

    // ---- 16:9: the rail (cards, this journey) ----
    if (L.wide) {
        card_pill(f, t, d, 1, L.rail_x, 184, L.rail_w, 56, false, m);
        card_pill(f, t, d, 2, L.rail_x, 248, L.rail_w, 56, false, m);
        f.dl->AddRectFilled(f.p(L.rail_x, 328), f.p(L.rail_x + L.rail_w, 528), t.panel, f.s(6));
        f.dl->AddRect(f.p(L.rail_x, 328), f.p(L.rail_x + L.rail_w, 528), t.rule2, f.s(6), 0, f.s(1));
        journey(f, t, d, L.rail_x + 16, 342, L.rail_w - 32, 1);
    }

    // ---- the saves, or what MENU7 is doing before a scan ----
    if (d.files_valid) {
        for (int i = 0; i < PC_UI_DATA_FILES; ++i)
            save_card(f, t, d, i, L.list_x, L.list_y + i * (L.card_h + L.gap), L.list_w, L.card_h, m);
    } else {
        const char* what = loading                      ? "Reading memory card\xe2\x80\xa6"
            : ops                                       ? "Choose Save or Load."
            : d.phase == PC_UI_DATA_PH_CARD             ? "Choose a memory card."
            : d.msg_text[0]                             ? d.msg_text
                                                        : "\xe2\x80\xa6";
        float cx = L.list_x + L.list_w * 0.5f, cy = 360;
        std::string s = fit(f, fonts.name, 22, what, L.list_w - 80);
        float w = measure(f, fonts.name, 22, s.c_str());
        if (loading || d.phase == PC_UI_DATA_PH_BUSY)
            spinner(f, cx - w * 0.5f - 18, cy + 11, 6, t.gold);
        text(f, fonts.name, 22, cx - w * 0.5f, cy, t.ash, s.c_str());
    }

    // ---- the status line (MENU7's message) ----
    if (d.published && d.state >= 7 && d.msg) {
        float sx = L.list_x, sy = L.status_y, sw = L.list_w, sh = L.status_h;
        bool wait = d.phase == PC_UI_DATA_PH_WAIT;
        bool o = hit(m, PC_UI_MOUSE_DATA_STATUS, 0, sx, sy, sx + sw, sy + sh);
        int sev = severity(d.msg);
        f.dl->AddRectFilled(f.p(sx, sy), f.p(sx + sw, sy + sh), o && wait ? t.rule2 : t.panel, f.s(6));
        f.dl->AddRect(f.p(sx, sy), f.p(sx + sw, sy + sh), sev == 3 ? t.down : sev == 2 ? t.up : t.rule2, f.s(6), 0,
            f.s(1));
        sev_icon(f, t, sev, sx + 22, sy + sh * 0.5f);
        float right = sx + sw - 14;
        if (wait) { // Continue (a click dismisses, as Circle)
            float gw = glyph(f, PC_PROMPT_CIRCLE, 18, 0, 0, true);
            float tw = measure(f, fonts.label, 13, "CONTINUE", 0.22f);
            text(f, fonts.label, 13, right - tw, sy + (sh - 13) * 0.5f - 1, t.bone, "CONTINUE", 0.22f);
            glyph(f, PC_PROMPT_CIRCLE, 18, right - tw - 6 - gw, sy + sh * 0.5f);
            right -= tw + gw + 20;
        }
        std::string msg = fit(f, fonts.label, 16, d.msg_text, right - (sx + 42), 0.02f);
        text(f, fonts.label, 16, sx + 42, sy + (sh - 16) * 0.5f - 1, t.bone, msg.c_str(), 0.02f);
    }

    // ---- MENU7's prompt ----
    if (d.phase == PC_UI_DATA_PH_PROMPT)
        dialog(f, t, d, L, m);

    // ---- the load's fade (MENU7's _fileMenuScreenFade, 0..30): ui_draw.cpp paints it over the footer
    // too (the original fades the whole screen) ----
    out.fade = d.fade > 0 ? (d.fade > 30 ? 1.0f : d.fade / 30.0f) : 0.0f;

    // ---- the footer ----
    out.crumb = phase_crumb(d);
    out.busy = loading || d.phase == PC_UI_DATA_PH_BUSY;
    if (ops) {
        out.hint(PC_PROMPT_CIRCLE, -1, "Confirm", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        out.hint(PC_PROMPT_TRIANGLE, -1, "Close", PC_UI_EXIT);
        out.hint(PC_PROMPT_L1, PC_PROMPT_R1, "Tabs", PC_UI_TAB_NEXT);
    } else if (d.phase == PC_UI_DATA_PH_WAIT) {
        out.hint(PC_PROMPT_CIRCLE, -1, "Continue", PC_UI_CONFIRM);
    } else if (d.phase == PC_UI_DATA_PH_PROMPT) {
        out.hint(PC_PROMPT_CIRCLE, -1, "Confirm", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "No", PC_UI_BACK);
    } else if (d.phase == PC_UI_DATA_PH_CARD || d.phase == PC_UI_DATA_PH_FILES) {
        out.hint(PC_PROMPT_CIRCLE, -1, "Confirm", PC_UI_CONFIRM);
        out.hint(PC_PROMPT_CROSS, -1, "Back", PC_UI_BACK);
        out.hint(PC_PROMPT_L1, PC_PROMPT_R1, "Tabs", PC_UI_TAB_NEXT); // backs out, then that tab (data_bridge.c)
    }
}

} // namespace uidraw
