// Modern menu drawing: the shell (ui_shell.cpp) on ImGui's background draw
// list, inside the game picture (pc_gl_view), in 1280x720 / 960x720 layout units.
#include "ui/ui_shell.h"
#include "ui/ui_theme.h"
#include "button_prompts.h"
#include "imgui.h"
#include "imgui_internal.h" /* ImTextCharFromUtf8 */
#include <cmath>
#include <cstdio>
#include <string>
#include <sys/stat.h>

extern "C" int pc_gl_view[4];
extern "C" int pc_gte_widescreen;

namespace {

ImFont* f_name;
ImFont* f_label;
ImFont* f_data;
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
    load_font(&f_name, &loaded_name, t.font_name);
    load_font(&f_label, &loaded_label, t.font_label);
    load_font(&f_data, &loaded_data, t.font_data);
}

struct Frame {
    ImDrawList* dl;
    float ox, oy, k; // window = origin + layout * k
    float W;         // layout width (1280 or 960)
    ImVec2 p(float x, float y) const { return ImVec2(ox + x * k, oy + y * k); }
    float s(float v) const { return v * k; }
};

// Letter-spaced text (tracking in em); returns the advance in layout units.
float text(const Frame& f, ImFont* font, float size, float x, float y, unsigned col, const char* str,
    float tracking = 0, bool measure_only = false)
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

// ASCII and Latin-1 letters (UTF-8 C3 A0..BE, not the division sign) to capitals: "Léa Monde".
void upper(std::string& s)
{
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z')
            s[i] = (char)(c - 32);
        else if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = (unsigned char)s[i + 1];
            if (d >= 0xA0 && d <= 0xBE && d != 0xB7)
                s[i + 1] = (char)(d - 0x20);
            ++i;
        }
    }
}

float approach(float cur, float target, float dt, float seconds)
{
    if (seconds <= 0)
        return target;
    float a = 1.0f - std::exp(-dt / (seconds / 3.0f));
    return cur + (target - cur) * a;
}

float ink_x, ink_w, focus_y;
int last_tab = -1;

} // namespace

extern "C" void pc_ui_draw_frame(float dt)
{
    if (!pc_ui_shell_visible() || pc_gl_view[2] <= 0 || pc_gl_view[3] <= 0) {
        last_tab = -1; // the next open places the ink and focus at once
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

    // backdrop over the game picture
    f.dl->AddRectFilled(ImVec2((float)pc_gl_view[0], (float)pc_gl_view[1]),
        ImVec2((float)(pc_gl_view[0] + pc_gl_view[2]), (float)(pc_gl_view[1] + pc_gl_view[3])), t.ground);

    // tabs
    float left = t.margin_x + 44, right = f.W - t.margin_x - 44, y = t.top + 13;
    enum { MAX_TABS = 16 };
    int n = pc_ui_shell_tab_count(), cur = pc_ui_shell_tab();
    if (n > MAX_TABS)
        n = MAX_TABS;
    std::string titles[MAX_TABS];
    float widths[MAX_TABS], total = 0;
    for (int i = 0; i < n; ++i) {
        titles[i] = pc_ui_shell_tab_title(i);
        upper(titles[i]);
        total += widths[i] = text(f, f_label, t.tab_size, 0, 0, 0, titles[i].c_str(), 0.3f, true);
    }
    float gap = n > 1 ? ((right - left) - total) / (n - 1) : 0, x = left;
    for (int i = 0; i < n; ++i) {
        text(f, f_label, t.tab_size, x, y, i == cur ? t.bone : t.dim, titles[i].c_str(), 0.3f);
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
    text(f, f_data, 10, t.margin_x, y + 3, t.ash, "L1");
    text(f, f_data, 10, f.W - t.margin_x - 14, y + 3, t.ash, "R1");

    // rows (left rail) and the focused row's card (right)
    float top = base + 22, rail = t.rail_width;
    int focus = pc_ui_shell_focus();
    if (cur != last_tab)
        focus_y = top + focus * t.row_height;
    focus_y = approach(focus_y, top + focus * t.row_height, dt, t.focus_grow);
    f.dl->AddRectFilledMultiColor(f.p(t.margin_x, focus_y), f.p(t.margin_x + rail, focus_y + t.row_height),
        t.accent_soft, t.accent_soft & 0x00FFFFFFu, t.accent_soft & 0x00FFFFFFu, t.accent_soft);
    f.dl->AddRectFilled(f.p(t.margin_x, focus_y + 8), f.p(t.margin_x + 2, focus_y + t.row_height - 8), t.accent);
    const char *label, *help, *value, *reason, *focus_label = "", *focus_help = "", *focus_reason = nullptr;
    int enabled;
    for (int i = 0; pc_ui_shell_row(i, &label, &help, &value, &enabled, &reason); ++i) {
        float ry = top + i * t.row_height;
        text(f, f_name, t.name_size, t.margin_x + 16, ry + (t.row_height - t.name_size) * 0.5f - 2,
            enabled ? t.bone : t.dim, label);
        if (value)
            text(f, f_data, 13, t.margin_x + rail - 90, ry + (t.row_height - 13) * 0.5f, t.bone, value);
        f.dl->AddLine(f.p(t.margin_x, ry + t.row_height), f.p(t.margin_x + rail, ry + t.row_height), t.rule, f.s(1));
        if (i == focus)
            focus_label = label, focus_help = help ? help : "", focus_reason = reason;
    }
    float cw = 396, cx = f.W - t.margin_x - cw;
    if (f.W < 1100)
        cw = 344, cx = f.W - t.margin_x - cw;
    float ch = focus_reason ? 200.0f : 150.0f;
    ImFont* body = f_name ? f_name : ImGui::GetFont();
    f.dl->AddRectFilled(f.p(cx, top), f.p(cx + cw, top + ch), t.panel);
    f.dl->AddRect(f.p(cx, top), f.p(cx + cw, top + ch), t.rule, 0.0f, f.s(1));
    text(f, f_name, 25, cx + 20, top + 18, t.bone, focus_label);
    f.dl->AddText(body, f.s(t.body_size), f.p(cx + 20, top + 58), t.ash, focus_help, nullptr, f.s(cw - 40));
    if (focus_reason) {
        // a greyed row still opens (the game's screen greys what is off); say why it is greyed
        f.dl->AddLine(f.p(cx + 20, top + 140), f.p(cx + cw - 20, top + 140), t.rule, f.s(1));
        f.dl->AddRectFilled(f.p(cx + 20, top + 156), f.p(cx + 22, top + 156 + t.body_size + 4), t.gold);
        f.dl->AddText(body, f.s(t.body_size), f.p(cx + 30, top + 156), t.gold, focus_reason, nullptr, f.s(cw - 50));
    }

    // hints (bottom right): the main session's prompt icons (button_prompts.h), our own verbs
    struct Hint { int button; const char* verb; } hints[] = {
        {PC_PROMPT_CIRCLE, "SELECT"}, {PC_PROMPT_CROSS, "CLOSE"}, {PC_PROMPT_TRIANGLE, "EXIT MENU"}};
    float hx = f.W - t.margin_x, hy = 720 - 40, icon = t.hint_size * 1.6f;
    for (int i = (int)(sizeof hints / sizeof hints[0]) - 1; i >= 0; --i) {
        float vw = text(f, f_label, t.hint_size, 0, 0, 0, hints[i].verb, 0.22f, true);
        hx -= vw;
        text(f, f_label, t.hint_size, hx, hy, t.ash, hints[i].verb, 0.22f);
        pc_prompt_image g = pc_prompt_glyph(hints[i].button, f.s(icon));
        float gw = g.texture ? g.w / f.k : 0;
        hx -= gw + 8;
        if (g.texture)
            // ImTextureRef has no ImTextureData* constructor (a pointer would
            // become a raw ImTextureID through the legacy void* one): GetTexRef.
            f.dl->AddImage(((ImTextureData*)g.texture)->GetTexRef(), f.p(hx, hy - (icon - t.hint_size) * 0.5f),
                f.p(hx + gw, hy - (icon - t.hint_size) * 0.5f + g.h / f.k), ImVec2(g.uv0[0], g.uv0[1]),
                ImVec2(g.uv1[0], g.uv1[1]));
        hx -= 26;
    }
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
