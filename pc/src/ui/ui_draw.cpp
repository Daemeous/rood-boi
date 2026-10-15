// Modern menu drawing: the shell (ui_shell.cpp) on ImGui's background draw
// list, inside the game picture (pc_gl_view), in 1280x720 / 960x720 layout
// units. The tabs, the command rows and their card, the hints and the pointer
// are here; screens with their own layout draw in their own files
// (ui_equip_draw.cpp, ui_ashley_draw.cpp, ui_system_draw.cpp, ui_abilities_draw.cpp, ui_magick_draw.cpp).
#include "ui/ui_draw_internal.h"
#include "ui/map_frame.h"
#include "ui/ui_settings.h"
#include "button_prompts.h"
#include "imgui_internal.h" /* ImTextCharFromUtf8 */
#include <cmath>
#include <cstdio>
#include <string>
#include <sys/stat.h>

extern "C" int pc_gl_view[4];
extern "C" int pc_gte_widescreen;
extern "C" unsigned pc_vsync_count(void) __attribute__((weak));
// gpu_hw.c: the presented picture is the Ashley tab's model view (the presenter shows the world behind it)
extern "C" int pc_hw_present_menu_model(void) __attribute__((weak));

namespace uidraw {

Fonts fonts;

float text(const Frame& f, ImFont* font, float size, float x, float y, unsigned col, const char* str, float tracking,
    bool measure_only)
{
    if (!font)
        font = ImGui::GetFont();
    float px = f.s(size), adv = 0;
    for (const char* c = str; *c;) {
        unsigned cp;
        int n = ImTextCharFromUtf8(&cp, c, nullptr);
        if (n <= 0)
            break;
        char one[8] = {};
        for (int i = 0; i < n; ++i)
            one[i] = c[i];
        if (!measure_only)
            f.dl->AddText(font, px, f.p(x + adv, y), col, one);
        adv += font->CalcTextSizeA(px, 1e9f, 0, one).x / f.k + tracking * size;
        c += n;
    }
    return adv;
}

float text_right(const Frame& f, ImFont* font, float size, float x, float y, unsigned col, const char* str,
    float tracking)
{
    float w = measure(f, font, size, str, tracking);
    text(f, font, size, x - w, y, col, str, tracking);
    return w;
}

std::string fit(const Frame& f, ImFont* font, float size, const char* str, float max_w, float tracking)
{
    if (measure(f, font, size, str, tracking) <= max_w)
        return str;
    std::string s = str;
    const char* dots = "\xe2\x80\xa6";
    while (!s.empty()) {
        // drop one UTF-8 character
        size_t n = s.size() - 1;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
            --n;
        s.erase(n);
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        if (measure(f, font, size, (s + dots).c_str(), tracking) <= max_w)
            break;
    }
    return s + dots;
}

std::string upper(const char* str)
{
    std::string s = str;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z')
            s[i] = (char)(c - 32);
        else if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = (unsigned char)s[i + 1];
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7) // not the division sign
                s[i + 1] = (char)(d - 0x20);
            ++i;
        }
    }
    return s;
}

float approach(float cur, float target, float dt, float seconds)
{
    if (seconds <= 0)
        return target;
    float a = 1.0f - std::exp(-dt / (seconds / 3.0f));
    return cur + (target - cur) * a;
}

float glyph(const Frame& f, int button, float size, float x, float cy, bool measure_only, unsigned tint)
{
    pc_prompt_image g = pc_prompt_glyph(button, f.s(size));
    if (!g.texture || g.h <= 0)
        return 0;
    float w = g.w / f.k, h = g.h / f.k;
    if (!measure_only)
        // ImTextureRef has no ImTextureData* constructor (a pointer would
        // become a raw ImTextureID through the legacy void* one): GetTexRef.
        f.dl->AddImage(((ImTextureData*)g.texture)->GetTexRef(), f.p(x, cy - h * 0.5f), f.p(x + w, cy + h * 0.5f),
            ImVec2(g.uv0[0], g.uv0[1]), ImVec2(g.uv1[0], g.uv1[1]), tint);
    return w;
}

void spinner(const Frame& f, float cx, float cy, float r, unsigned col)
{
    float a = (float)ImGui::GetTime() * 5.0f;
    f.dl->PathArcTo(f.p(cx, cy), f.s(r), a, a + 4.4f, 20);
    f.dl->PathStroke(col, 0, f.s(1.5f));
}

unsigned fade(unsigned col, float a)
{
    unsigned alpha = (unsigned)((col >> 24) * (a < 0 ? 0 : a > 1 ? 1 : a) + 0.5f);
    return (col & 0x00FFFFFFu) | (alpha << 24);
}

// The model column of the Ashley screen (ui_ashley_draw.cpp lays the screen
// out around it; ashley_bridge.c centres the model on it and sizes it to the
// height). 16:9: between the left rail and the right card, 3/4 of the body's
// height, centred in it: the model is 0.8 of that tall and, a weapon held out
// at her side, about half that wide on each side of her axis, which keeps the
// weapon off the rail's text. 4:3: everything right of the rail, its upper 60%
// (the card goes below it).
void model_column(const ui::Theme& t, float W, float out[4])
{
    float x0 = t.margin_x + t.rail_width, y0 = t.top + t.tab_height + 22, x1, y1;
    if (W >= 1100) {
        x1 = W - t.margin_x - card_width(W);
        y1 = 720 - 40 - 16;
        float h = (y1 - y0) * 0.75f;
        y0 += (y1 - y0 - h) * 0.5f;
        y1 = y0 + h;
    } else {
        x1 = W - t.margin_x;
        y1 = y0 + (720 - 64 - y0) * 0.6f;
    }
    out[0] = x0, out[1] = y0, out[2] = x1 - x0, out[3] = y1 - y0;
}

} // namespace uidraw

namespace {
using namespace uidraw;

std::string loaded_name, loaded_label, loaded_data;

// The renderer backends set ImGuiBackendFlags_RendererHasTextures, so the
// atlas is dynamic: fonts load here, inside the frame, at size 0 (any size
// is baked on first use). A theme naming another file swaps that font; a
// missing file keeps ImGui's default font (no assert: NoLoadError).
void load_font(ImFont** font, std::string* loaded, const std::string& path)
{
    if (*loaded == path)
        return;
    *loaded = path;
    ImGuiIO& io = ImGui::GetIO();
    if (*font)
        io.Fonts->RemoveFont(*font);
    *font = nullptr;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        fprintf(stderr, "[ui] font %s missing (default font used)\n", path.c_str());
        return;
    }
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.Flags |= ImFontFlags_NoLoadError;
    *font = io.Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &cfg);
}
void load_fonts(const ui::Theme& t)
{
    load_font(&fonts.name, &loaded_name, t.font_name);
    load_font(&fonts.label, &loaded_label, t.font_label);
    load_font(&fonts.data, &loaded_data, t.font_data);
}

float ink_x, ink_w, focus_y;
int last_tab = -1;
bool forced = false; // pc_ui_draw_pointer: the next frame's pointer
Pointer forced_pointer;

} // namespace

// The tab strip (the shell's, and the Map frame's with its own pointer
// kinds: a click is a request for the map screen). steps_lit false: the
// L1 / R1 icons dimmed (the pad's L1 / R1 do something else there; the
// clicks still step). Returns its base line.
float uidraw::draw_tabs(const Frame& f, const ui::Theme& t, int cur, Pointer& m, float dt, bool map, bool steps_lit)
{
    float left = t.margin_x + 44, right = f.W - t.margin_x - 44, y = t.top + 13;
    enum { MAX_TABS = 16 };
    int n = pc_ui_shell_tab_count();
    if (n > MAX_TABS)
        n = MAX_TABS;
    std::string titles[MAX_TABS];
    float widths[MAX_TABS], total = 0;
    for (int i = 0; i < n; ++i) {
        titles[i] = upper(pc_ui_shell_tab_title(i));
        total += widths[i] = measure(f, fonts.label, t.tab_size, titles[i].c_str(), 0.3f);
    }
    float gap = n > 1 ? ((right - left) - total) / (n - 1) : 0, x = left;
    for (int i = 0; i < n; ++i) {
        bool over = m.hit(map ? (int)PC_UI_MOUSE_MAP_TAB : (int)PC_UI_MOUSE_TAB, i, x - gap * 0.5f, t.top,
            x + widths[i] + gap * 0.5f, t.top + t.tab_height);
        if (map && i != cur)
            pc_ui_map_hit_record(PC_UI_MOUSE_MAP_TAB, i, x - gap * 0.5f, t.top, x + widths[i] + gap * 0.5f,
                t.top + t.tab_height);
        if (map && i == cur && over)
            m.kind = PC_UI_MOUSE_NONE; // the tab the map is on: nothing to do
        text(f, fonts.label, t.tab_size, x, y, i == cur ? t.bone : over ? t.ash : t.dim, titles[i].c_str(), 0.3f);
        if (i == cur) {
            if (last_tab < 0)
                ink_x = x, ink_w = widths[i];
            ink_x = approach(ink_x, x, dt, t.tab_slide);
            ink_w = approach(ink_w, widths[i], dt, t.tab_slide);
        }
        x += widths[i] + gap;
    }
    float base = t.top + t.tab_height;
    f.dl->AddLine(f.p(t.margin_x, base), f.p(f.W - t.margin_x, base), t.rule, f.s(1));
    f.dl->AddRectFilled(f.p(ink_x, base - 1), f.p(ink_x + ink_w, base + 1), t.accent);
    float tab_cy = y + t.tab_size * 0.5f + 1;
    unsigned step_tint = steps_lit ? 0xFFFFFFFFu : (t.dim | 0xFF000000u), step_text = steps_lit ? t.ash : t.dim;
    float lw = glyph(f, PC_PROMPT_L1, 20, 0, 0, true);
    if (map)
        m.hit(PC_UI_MOUSE_MAP_TAB, pc_ui_shell_step_tab(-1), t.margin_x - 6, t.top, t.margin_x + 34, t.top + t.tab_height);
    else
        m.hit(PC_UI_MOUSE_BUTTON, PC_UI_TAB_PREV, t.margin_x - 6, t.top, t.margin_x + 34, t.top + t.tab_height);
    if (lw > 0)
        glyph(f, PC_PROMPT_L1, 20, t.margin_x + (34 - lw) * 0.5f, tab_cy, false, step_tint);
    else
        text(f, fonts.data, 10, t.margin_x, y + 3, step_text, "L1");
    float rw = glyph(f, PC_PROMPT_R1, 20, 0, 0, true);
    if (map)
        m.hit(PC_UI_MOUSE_MAP_TAB, pc_ui_shell_step_tab(1), f.W - t.margin_x - 34, t.top, f.W - t.margin_x + 6,
            t.top + t.tab_height);
    else
        m.hit(PC_UI_MOUSE_BUTTON, PC_UI_TAB_NEXT, f.W - t.margin_x - 34, t.top, f.W - t.margin_x + 6,
            t.top + t.tab_height);
    if (rw > 0)
        glyph(f, PC_PROMPT_R1, 20, f.W - t.margin_x - 34 + (34 - rw) * 0.5f, tab_cy, false, step_tint);
    else
        text(f, fonts.data, 10, f.W - t.margin_x - 14, y + 3, step_text, "R1");
    return base;
}

namespace {

// The pointer from ImGui's IO in layout units; nothing while an editor
// window or widget has it, or while the shell is held (not ticked).
Pointer read_pointer(const Frame& f)
{
    Pointer m;
    ImGuiIO& io = ImGui::GetIO();
    bool shown = pc_ui_loot_visible() || pc_ui_map_visible() || (pc_ui_shell_visible() && !pc_ui_shell_holding());
    if (forced) {
        forced = false;
        if (!shown)
            return m;
        return forced_pointer;
    }
    if (!shown || !ImGui::IsMousePosValid()
        || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::IsAnyItemActive())
        return m;
    m.ok = true;
    m.x = (io.MousePos.x - f.ox) / f.k;
    m.y = (io.MousePos.y - f.oy) / f.k;
    m.moved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    m.clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    m.down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    m.middle = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    m.shift = io.KeyShift;
    m.right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    m.wheel = io.MouseWheel;
    return m;
}

// What the pointer did this frame, for the shell's next tick. Hover only
// counts when the pointer moved: a still pointer does not take the focus
// from the keyboard or pad (or follow a list scrolling under it).
void send_pointer(const Pointer& m)
{
    if (!m.ok)
        return;
    // the Loot screen (a direct session) has the pointer while it shows
    void (*mouse)(int, int, int, float) = pc_ui_loot_visible() ? pc_ui_loot_mouse : pc_ui_shell_mouse;
    if (m.kind != PC_UI_MOUSE_NONE)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (m.right) { // right click: back, as Cross
        mouse(PC_UI_MOUSE_BUTTON, PC_UI_BACK, 1, 0);
        return;
    }
    if (m.clicked && m.kind != PC_UI_MOUSE_NONE)
        mouse(m.kind, m.index, 1, m.wheel);
    else if (m.moved && m.kind != PC_UI_MOUSE_NONE)
        mouse(m.kind, m.index, 0, m.wheel);
    else if (m.wheel != 0)
        mouse(PC_UI_MOUSE_NONE, 0, 0, m.wheel);
}

// The Loot screen (a direct session: no tabs): the backdrop, its title where
// the tabs go, its body (ui_loot_draw.cpp) and the footer.
void draw_footer(const Frame& f, const ui::Theme& t, const ScreenOut& out, bool held, Pointer& m);
bool direct_shown = false; // the Loot screen was drawn last frame
void draw_direct(const Frame& f, const ui::Theme& t, float dt)
{
    Pointer m = read_pointer(f);
    ImVec2 v0((float)pc_gl_view[0], (float)pc_gl_view[1]);
    ImVec2 v1((float)(pc_gl_view[0] + pc_gl_view[2]), (float)(pc_gl_view[1] + pc_gl_view[3]));
    f.dl->AddRectFilled(v0, v1, t.ground);
    const char* title = pc_ui_loot_model()->chest ? "CHEST" : "LOOT";
    float y = t.top + 13;
    float w = text(f, fonts.label, t.tab_size, t.margin_x, y, t.bone, title, 0.3f);
    float base = t.top + t.tab_height;
    f.dl->AddLine(f.p(t.margin_x, base), f.p(f.W - t.margin_x, base), t.rule, f.s(1));
    f.dl->AddRectFilled(f.p(t.margin_x, base - 1), f.p(t.margin_x + w, base + 1), t.accent);
    text_right(f, fonts.label, t.label_size, f.W - t.margin_x, y + 3, t.dim, "GADGET BAG", 0.26f);
    ScreenOut out;
    draw_loot(f, t, base + 22, dt, !direct_shown, m, out);
    draw_footer(f, t, out, false, m);
    send_pointer(m);
    direct_shown = true;
    last_tab = -1; // the shell after it places its ink at once
}

// The command rows (left rail) and the focused row's card (right).
void draw_commands(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out)
{
    float rail = t.rail_width;
    int focus = pc_ui_shell_focus();
    if (fresh)
        focus_y = top + focus * t.row_height;
    focus_y = approach(focus_y, top + focus * t.row_height, dt, t.focus_grow);
    f.dl->AddRectFilledMultiColor(f.p(t.margin_x, focus_y), f.p(t.margin_x + rail, focus_y + t.row_height),
        t.accent_soft, t.accent_soft & 0x00FFFFFFu, t.accent_soft & 0x00FFFFFFu, t.accent_soft);
    f.dl->AddRectFilled(f.p(t.margin_x, focus_y + 8), f.p(t.margin_x + 2, focus_y + t.row_height - 8), t.accent);
    const char *label, *help, *value, *reason, *focus_label = "", *focus_help = "", *focus_reason = nullptr;
    int enabled;
    for (int i = 0; pc_ui_shell_row(i, &label, &help, &value, &enabled, &reason); ++i) {
        float ry = top + i * t.row_height;
        m.hit(PC_UI_MOUSE_ROW, i, t.margin_x, ry, t.margin_x + rail, ry + t.row_height);
        text(f, fonts.name, t.name_size, t.margin_x + 16, ry + (t.row_height - t.name_size) * 0.5f - 2,
            enabled ? t.bone : t.dim, label);
        if (value)
            text(f, fonts.data, 13, t.margin_x + rail - 90, ry + (t.row_height - 13) * 0.5f, t.bone, value);
        f.dl->AddLine(f.p(t.margin_x, ry + t.row_height), f.p(t.margin_x + rail, ry + t.row_height), t.rule, f.s(1));
        if (i == focus)
            focus_label = label, focus_help = help ? help : "", focus_reason = reason;
    }
    float cw = card_width(f.W), cx = f.W - t.margin_x - cw;
    float ch = focus_reason ? 200.0f : 150.0f;
    ImFont* body = fonts.name ? fonts.name : ImGui::GetFont();
    f.dl->AddRectFilled(f.p(cx, top), f.p(cx + cw, top + ch), t.panel);
    f.dl->AddRect(f.p(cx, top), f.p(cx + cw, top + ch), t.rule, 0.0f, f.s(1));
    text(f, fonts.name, 25, cx + 20, top + 18, t.bone, focus_label);
    f.dl->AddText(body, f.s(t.body_size), f.p(cx + 20, top + 58), t.ash, focus_help, nullptr, f.s(cw - 40));
    if (focus_reason) {
        // a greyed row still opens (the game's screen greys what is off); say why it is greyed
        f.dl->AddLine(f.p(cx + 20, top + 140), f.p(cx + cw - 20, top + 140), t.rule, f.s(1));
        f.dl->AddRectFilled(f.p(cx + 20, top + 156), f.p(cx + 22, top + 156 + t.body_size + 4), t.gold);
        f.dl->AddText(body, f.s(t.body_size), f.p(cx + 30, top + 156), t.gold, focus_reason, nullptr, f.s(cw - 50));
    }
    out.hint(PC_PROMPT_CIRCLE, -1, "Select", PC_UI_CONFIRM);
    out.hint(PC_PROMPT_CROSS, -1, "Close", PC_UI_BACK);
    out.hint(PC_PROMPT_TRIANGLE, -1, "Exit menu", PC_UI_EXIT);
}

// Turning the model: a left drag that starts over it (the mouse), or test
// pointers sent with the button down (one drag per source).
struct Drag {
    bool on = false;
    float x = 0, y = 0;
} mouse_drag, rpc_drag;
void drag_model(const Pointer& m, const float r[4])
{
    Drag& d = m.rpc ? rpc_drag : mouse_drag;
    bool inside = m.ok && m.x >= r[0] && m.x < r[0] + r[2] && m.y >= r[1] && m.y < r[1] + r[3];
    if (inside || d.on)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (!m.ok || !m.down) {
        if (m.ok || m.rpc)
            d.on = false;
        return;
    }
    if (!d.on) {
        if (inside && (m.rpc || m.clicked))
            d.on = true, d.x = m.x, d.y = m.y;
        return;
    }
    pc_ui_shell_model_drag(m.x - d.x, m.y - d.y);
    d.x = m.x, d.y = m.y;
}

// Hints (bottom right, the main session's prompt icons and our own verbs) and
// the crumb (bottom left).
void draw_footer(const Frame& f, const ui::Theme& t, const ScreenOut& out, bool held, Pointer& m)
{
    float hx = f.W - t.margin_x, hy = 720 - 40, icon = t.hint_size * 1.6f, cy = hy + t.hint_size * 0.5f;
    unsigned col = held ? t.dim : t.ash;
    for (int i = out.hint_count - 1; i >= 0; --i) {
        const Hint& h = out.hints[i];
        // a hint without a verb is an icon of the group on its right ("○ △ □ SET ON THAT BUTTON")
        std::string verb = upper(h.verb);
        float right = hx;
        float vw = verb.empty() ? 0 : measure(f, fonts.label, t.hint_size, verb.c_str(), 0.22f);
        float sp = verb.empty() ? 0 : 9;
        float gw1 = h.b1 >= 0 ? glyph(f, h.b1, icon, 0, 0, true) : 0;
        float gw0 = glyph(f, h.b0, icon, 0, 0, true);
        float left = hx - vw - sp - gw1 - (gw1 > 0 ? 3 : 0) - gw0;
        bool over = m.over(left - 4, hy - 8, right + 4, hy + t.hint_size + 8);
        unsigned c = over ? t.bone : col;
        if (over)
            m.hit(PC_UI_MOUSE_BUTTON, (int)h.bits, left - 4, hy - 8, right + 4, hy + t.hint_size + 8);
        hx -= vw;
        if (!verb.empty())
            text(f, fonts.label, t.hint_size, hx, hy, c, verb.c_str(), 0.22f);
        hx -= sp;
        if (h.b1 >= 0) {
            hx -= gw1;
            glyph(f, h.b1, icon, hx, cy);
            hx -= 3;
        }
        hx -= gw0;
        glyph(f, h.b0, icon, hx, cy);
        hx -= i > 0 && !out.hints[i - 1].verb[0] ? 8 : 26;
    }
    // the crumb: what fits left of the hints (the tail always shows)
    float x = t.margin_x;
    if (!out.crumb.empty() || !out.crumb_tail.empty()) {
        std::string tail = upper(out.crumb_tail.c_str());
        float tw = tail.empty() ? 0 : measure(f, fonts.label, t.hint_size, tail.c_str(), 0.18f);
        float room = hx + 26 - 24 - x - tw - (held || out.busy ? 90 : 0);
        std::string c = fit(f, fonts.label, t.hint_size, upper(out.crumb.c_str()).c_str(), room, 0.18f);
        x += text(f, fonts.label, t.hint_size, x, hy, t.dim, c.c_str(), 0.18f);
        x += text(f, fonts.label, t.hint_size, x, hy, t.dim, tail.c_str(), 0.18f) + 18;
    }
    if (held || out.busy) {
        spinner(f, x + 6, cy, 5.5f, t.gold);
        text(f, fonts.label, t.hint_size, x + 18, hy, t.gold, "LOADING", 0.22f);
    }
}

} // namespace

extern "C" void pc_ui_draw_pointer(float x, float y, int click, int right, float wheel, int down, int middle,
    int shift)
{
    forced = true;
    forced_pointer = Pointer();
    forced_pointer.ok = true;
    forced_pointer.rpc = true;
    forced_pointer.down = down != 0;
    forced_pointer.middle = middle != 0;
    forced_pointer.shift = shift != 0;
    forced_pointer.x = x, forced_pointer.y = y;
    forced_pointer.moved = true;
    forced_pointer.clicked = click != 0;
    forced_pointer.right = right != 0;
    forced_pointer.wheel = wheel;
}

extern "C" void pc_ui_draw_model_column(float out[4])
{
    model_column(ui::theme(), pc_gte_widescreen ? 1280.0f : 960.0f, out);
}

extern "C" int pc_ui_shell_wants_mouse(void)
{
    if ((!pc_ui_shell_drawn() && !pc_ui_loot_visible() && !pc_ui_map_visible()) || pc_gl_view[2] <= 0
        || pc_gl_view[3] <= 0 || !ImGui::IsMousePosValid())
        return 0;
    ImVec2 p = ImGui::GetIO().MousePos;
    return p.x >= pc_gl_view[0] && p.y >= pc_gl_view[1] && p.x < pc_gl_view[0] + pc_gl_view[2]
        && p.y < pc_gl_view[1] + pc_gl_view[3];
}

extern "C" void pc_ui_draw_frame(float dt)
{
    // the System pages' save debounce (ui_settings.cpp): every frame, shell up or not
    pc_ui_settings_tick(ImGui::GetTime());
    if ((!pc_ui_shell_drawn() && !pc_ui_loot_visible() && !pc_ui_map_visible()) || pc_gl_view[2] <= 0
        || pc_gl_view[3] <= 0) {
        last_tab = -1; // the next open places the ink and focus at once
        direct_shown = false;
        forced = false; // a test pointer is for a shown shell only
        return;
    }
    const ui::Theme& t = ui::theme();
    load_fonts(t);
    Frame f;
    f.dl = ImGui::GetBackgroundDrawList();
    f.W = pc_gte_widescreen ? 1280.0f : 960.0f;
    // the whole layout fits the picture (either axis may be the tighter one), centred
    f.k = std::fmin(pc_gl_view[2] / f.W, pc_gl_view[3] / 720.0f);
    f.ox = (float)pc_gl_view[0] + (pc_gl_view[2] - f.W * f.k) * 0.5f;
    f.oy = (float)pc_gl_view[1] + (pc_gl_view[3] - 720.0f * f.k) * 0.5f;
    if (pc_ui_loot_visible()) {
        draw_direct(f, t, dt);
        return;
    }
    direct_shown = false;
    Pointer m = read_pointer(f);
    // the Map frame: the game's map screen with the shell's chrome around it
    // (ui_map_draw.cpp), while its MENU5 run is fresh
    if (pc_ui_map_visible() && !pc_ui_shell_drawn()) {
        int tab = pc_ui_shell_tab();
        draw_map_frame(f, t, dt, m);
        last_tab = tab;
        return;
    }
    bool held = pc_ui_shell_holding() && !pc_ui_shell_visible();

    // backdrop over the game picture: the ground colour at the menus' opacity (ui_menu_opacity; ui_theme.cpp).
    // The Ashley tab's model view: the bridge freezes the world and has the game draw the model alone
    // over a flat tile (ashley_bridge.c clear_behind); the presenter puts the picture of the world it
    // kept before under this same ground, in place of the tile, around the model (gl_present.c "The
    // modern menu's backdrop"; the post stack runs neutral on those pictures, so the model is never
    // graded). While the presented picture is one of those (gpu_hw.c pc_hw_present_menu_model) the
    // ground is already there: none here, the model at full strength, the tab as see-through as the
    // others. The drag works only while the bridge's snapshot is fresh (filled within the last few
    // vsyncs: the game ticks every 1 to 4).
    const pc_ui_ashley* ash = pc_ui_shell_ashley();
    bool fresh_snapshot = !pc_vsync_count || pc_vsync_count() - ash->tick <= 8;
    bool model = pc_ui_shell_visible() && pc_ui_shell_screen() == PC_UI_SCREEN_ASHLEY && ash->model == 2
        && fresh_snapshot && ash->model_rect[2] > 0 && ash->model_rect[3] > 0;
    ImVec2 v0((float)pc_gl_view[0], (float)pc_gl_view[1]);
    ImVec2 v1((float)(pc_gl_view[0] + pc_gl_view[2]), (float)(pc_gl_view[1] + pc_gl_view[3]));
    if (!(pc_hw_present_menu_model && pc_hw_present_menu_model()))
        f.dl->AddRectFilled(v0, v1, t.ground);
    if (model)
        drag_model(m, ash->model_rect);
    else
        mouse_drag.on = rpc_drag.on = false;

    // tabs
    int cur = pc_ui_shell_tab();
    float base = draw_tabs(f, t, cur, m, dt, false);
    float top = base + 22;
    bool fresh = cur != last_tab;
    ScreenOut out;
    if (pc_ui_shell_screen() == PC_UI_SCREEN_EQUIPMENT)
        draw_equipment(f, t, top, dt, fresh, m, out);
    else if (pc_ui_shell_screen() == PC_UI_SCREEN_ASHLEY)
        draw_ashley(f, t, top, dt, fresh, model, m, out);
    else if (pc_ui_shell_screen() == PC_UI_SCREEN_SYSTEM)
        draw_system(f, t, top, dt, fresh, m, out);
    else if (pc_ui_shell_screen() == PC_UI_SCREEN_ABILITIES)
        draw_abilities(f, t, top, dt, fresh, m, out);
    else if (pc_ui_shell_screen() == PC_UI_SCREEN_MAGICK)
        draw_magick(f, t, top, dt, fresh, m, out);
    else if (pc_ui_shell_screen() == PC_UI_SCREEN_ITEMS)
        draw_items(f, t, top, dt, fresh, m, out);
    else
        draw_commands(f, t, top, dt, fresh, m, out);
    draw_footer(f, t, out, held, m);
    send_pointer(m);
    last_tab = cur;
}

extern "C" void pc_ui_menus_editor_row(void)
{
    static const char* modes[] = {"Original", "Modern (in progress)"};
    int mode = pc_ui_menus ? 1 : 0;
    if (ImGui::Combo("Menus", &mode, modes, 2))
        pc_ui_menus = mode;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Original: the game's own menus. Modern: Triangle opens the tabbed menu (pc/src/ui,\n"
                          "pc/docs/Modern_Menus.md); its rows still open the game's screens for now. Takes\n"
                          "effect the next time the menu opens. Settings key ui_menus.");
}
