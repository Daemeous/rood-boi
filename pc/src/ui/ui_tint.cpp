// Modern menus: the menu tint's presets and colours (ui_tint.h).
#include "ui/ui_tint.h"
#include <cstring>

namespace {
const char* const IDS[PC_UI_TINTS] = {"lea_monde", "classic", "slate", "crimson", "verdant", "custom"};
const char* const NAMES[PC_UI_TINTS] = {"Le\xc3\xa1 Monde", "Classic", "Slate", "Crimson", "Verdant", "Custom"};
// Our own values (0..31 per channel). Leá Monde's row is unused: its colour is the theme file's ground, read live
// (pc_ui_tint_rgb); the menus keep the theme file's colours.
const int PRESETS[PC_UI_TINTS - 1][3] = {{8, 7, 7}, {4, 8, 25}, {7, 8, 10}, {14, 3, 4}, {3, 10, 6}};
int clamp31(int v) { return v < 0 ? 0 : v > PC_UI_TINT_MAX ? PC_UI_TINT_MAX : v; }
} // namespace

// ui_theme.cpp: the theme file's ground (untinted, live-reloaded), 0..255 per channel.
extern "C" void pc_ui_theme_file_ground_rgb(int* r, int* g, int* b);

extern "C" const char* pc_ui_tint_id(int tint) { return tint >= 0 && tint < PC_UI_TINTS ? IDS[tint] : nullptr; }
extern "C" const char* pc_ui_tint_name(int tint) { return tint >= 0 && tint < PC_UI_TINTS ? NAMES[tint] : nullptr; }
extern "C" const char* const* pc_ui_tint_names(void) { return NAMES; }

extern "C" int pc_ui_tint_from_name(const char* s)
{
    if (!s)
        return -1;
    for (int i = 0; i < PC_UI_TINTS; ++i)
        if (!std::strcmp(s, IDS[i]) || (s[0] == '0' + i && !s[1]))
            return i;
    return -1;
}

extern "C" int pc_ui_tint_current(void)
{
    return pc_ui_menu_tint >= 0 && pc_ui_menu_tint < PC_UI_TINTS ? pc_ui_menu_tint : PC_UI_TINT_LEA_MONDE;
}

extern "C" void pc_ui_tint_rgb(int tint, int out[3])
{
    if (tint < 0 || tint >= PC_UI_TINTS)
        tint = PC_UI_TINT_LEA_MONDE;
    if (tint == PC_UI_TINT_LEA_MONDE) { // the inverse of pc_ui_tint_ground, rounded: v * 31 / 64
        int g[3];
        pc_ui_theme_file_ground_rgb(&g[0], &g[1], &g[2]);
        for (int c = 0; c < 3; ++c)
            out[c] = clamp31((g[c] * 31 + 32) / 64);
        return;
    }
    for (int c = 0; c < 3; ++c)
        out[c] = tint == PC_UI_TINT_CUSTOM ? clamp31(pc_ui_menu_tint_custom[c]) : PRESETS[tint][c];
}

extern "C" void pc_ui_tint_effective(int out[3]) { pc_ui_tint_rgb(pc_ui_tint_current(), out); }

extern "C" void pc_ui_tint_swatch(const int rgb31[3], unsigned char out[3])
{
    for (int c = 0; c < 3; ++c)
        out[c] = (unsigned char)((clamp31(rgb31[c]) * 255 + 15) / 31);
}

extern "C" void pc_ui_tint_ground(const int rgb31[3], float out[3])
{
    for (int c = 0; c < 3; ++c)
        out[c] = (float)clamp31(rgb31[c]) * 64.0f / 31.0f;
}
