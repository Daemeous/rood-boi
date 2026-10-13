// Modern menu theme: colours, type sizes and timings (pc/assets/ui/theme.json).
#pragma once
#include <string>

namespace ui {

struct Theme {
    // colours as ImU32 (0xAABBGGRR)
    unsigned ground = 0xF00E0F11u, panel = 0xFF141619u, rule = 0xFF24272Cu, rule2 = 0xFF31363Du;
    unsigned bone = 0xFFD6E4EBu, ash = 0xFF86929Bu, dim = 0xFF535C64u;
    unsigned accent = 0xFF3C36B5u, accent_soft = 0x293C36B5u, gold = 0xFF4EA1C9u;
    unsigned hp = 0xFF5AA76Au, mp = 0xFFC4824Du, risk = 0xFF3C4ACFu;
    // sizes in layout units (1280 x 720 or 960 x 720)
    float tab_size = 15, label_size = 11, name_size = 19, body_size = 14.5f, hint_size = 12;
    float margin_x = 56, top = 34, tab_height = 44, row_height = 46, rail_width = 448;
    // seconds
    float tab_slide = 0.24f, focus_grow = 0.16f, screen_fade = 0.2f;
    // font files (relative to the repository root)
    std::string font_name = "pc/third_party/fonts/Alegreya/Alegreya[wght].ttf";
    std::string font_label = "pc/third_party/fonts/AsapCondensed/AsapCondensed-Bold.ttf";
    std::string font_data = "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf";
};

// Parse theme JSON over `t` (missing keys keep their values). False on a syntax or value error.
bool theme_parse(const char* text, Theme& t, std::string* error);
// The live theme: pc/assets/ui/theme.json, reloaded when it changes (checked at most twice a second).
const Theme& theme();

} // namespace ui
