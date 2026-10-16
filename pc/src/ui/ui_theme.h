// Modern menu theme: colours, type sizes and timings (pc/assets/ui/theme.json).
#pragma once
#include <string>

#include "ui/menu_light.h"

namespace ui {

// The native Score screen's look (theme.json "score"; ui_score_draw.cpp): sizes, the list's rows and every
// rect, per layout width (wide: 1280, narrow: 960). Rects are [x0, x1] (or [y0, y1] for the bands), layout units.
struct ScoreLayout {
    float header[2], strip[2], content[2];          // y bands: the score and rank, the section strip, the pages
    float home_card[2], home_list[2];               // Home: the summary card, the four sections
    float titles_list[2], titles_detail[2];
    float records_table[2], records_detail[2];
    float trials_list[2], trials_panel[2];
    float bestiary_list[2], bestiary_detail[2];
};
struct ScoreTheme {
    float score_size = 40, rank_size = 44; // the header's score (Chakra Petch) and rank (Alegreya)
    float row_h = 44, visible_rows = 10;   // the lists' rows
    float page_rows = 8;                   // the Bestiary's L2 / R2 page
    float locked_alpha = 0.45f;            // locked rows (titles, foes) and no-record bosses
    // per-element type sizes (layout units)
    float title_size = 25;        // a detail card's title
    float name_size = 19;         // the lists' names, Journey's names
    float number_size = 15;       // the lists' numbers, Time Attack's row time and places
    float section_size = 21;      // Home's section names
    float help_size = 14;         // Home's section help, Journey's "best"
    float value_size = 20;        // Journey's values
    float record_size = 18;       // the Records' names
    float record_value_size = 17; // the Records' values
    float big_value_size = 34;    // the Records card's value
    float time_size = 22;         // Time Attack's times
    float note_size = 16;         // Time Attack's rounds, the Bestiary's kills line
    float class_size = 18;        // the Bestiary's class
    float hint_size = 17;         // the Bestiary's hint
    float glyph_size = 22;        // its button glyph
    float chip_size = 12;         // the count chips
    // spacings (layout units)
    float pad = 22;               // a panel's inner margin
    float inset = 16;             // a row's left / right inset
    float name_x = 56;            // a numbered row's name, from the list's left (two digits)
    float name_x3 = 62;           // the Bestiary's (three digits)
    float line_gap = 30;          // detail lines (Journey's label, the Bestiary's class)
    float place_gap = 34;         // Time Attack's places
    float head_rule = 42;         // a card's title to its rule
    float body_gap = 14;          // the rule to the body
    float hint_y = 48;            // the Bestiary's hint above the panel's bottom
    float label_top = 16;         // a panel's small label below its top
    float card_top = 40;          // a detail card's title below the panel's top
    // the rects' outer edges at the header's margin_x (56): 56..1224 wide, 56..904 narrow
    ScoreLayout wide = {{32, 112}, {120, 168}, {184, 640}, {56, 700}, {732, 1224}, {56, 700}, {732, 1224},
        {56, 820}, {852, 1224}, {56, 520}, {552, 1224}, {56, 560}, {592, 1224}};
    ScoreLayout narrow = {{32, 112}, {120, 168}, {184, 640}, {56, 500}, {524, 904}, {56, 500}, {524, 904},
        {56, 600}, {624, 904}, {56, 420}, {444, 904}, {56, 440}, {464, 904}};
    const ScoreLayout& layout(float W) const { return W < 1100 ? narrow : wide; }
};
// The Score group's keys, for the RPC (ui_menu score_tune): a number ("row_h") or a rect's end
// ("wide.titles_list", index 0 or 1). False: no such key (or index).
// TODO(editor): the group in the dev tools through rood_schema::add_kind / rood_details::draw_kind once on main.
bool score_key_set(ScoreTheme& s, const char* key, int index, float value);
// An override over the file's values until reset (key null) or the file's next edit: applied to theme()
// from the next call; one per key (and rect end), a later one replaces it.
bool score_tune(const char* key, int index, float value);
// The group as shown, for the RPC: appends ,"tunables":{...},"tune_overrides":["key[index]", ...].
void score_json(std::string& j);
inline pc_menu_light menu_light_defaults()
{
    pc_menu_light l;
    pc_ui_menu_model_light_defaults(&l);
    return l;
}

struct Theme {
    // colours as ImU32 (0xAABBGGRR)
    unsigned ground = 0xF00E0F11u, panel = 0xFF141619u, rule = 0xFF24272Cu, rule2 = 0xFF31363Du;
    unsigned bone = 0xFFD6E4EBu, ash = 0xFF86929Bu, dim = 0xFF535C64u;
    unsigned accent = 0xFF3C36B5u, accent_soft = 0x293C36B5u, gold = 0xFF4EA1C9u;
    unsigned hp = 0xFF5AA76Au, mp = 0xFFC4824Du, risk = 0xFF3C4ACFu;
    unsigned up = 0xFF6AB879u, down = 0xFF4F61D0u; // stat deltas: better / worse
    // the ground colour as a panel's fill (opaque at the theme's own opacity; theme() sets it)
    unsigned stage = 0xFF0E0F11u;
    // sizes in layout units (1280 x 720 or 960 x 720)
    float tab_size = 15, label_size = 11, name_size = 19, body_size = 14.5f, hint_size = 12;
    float margin_x = 56, top = 34, tab_height = 44, row_height = 46, rail_width = 448;
    // seconds
    float tab_slide = 0.24f, focus_grow = 0.16f, screen_fade = 0.2f;
    // font files (relative to the repository root)
    std::string font_name = "pc/third_party/fonts/Alegreya/Alegreya[wght].ttf";
    std::string font_label = "pc/third_party/fonts/AsapCondensed/AsapCondensed-Bold.ttf";
    std::string font_data = "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf";
    ScoreTheme score;
    // the menu model's studio light ("menu_model_light"; menu_light.h)
    pc_menu_light menu_light = menu_light_defaults();
};

// Parse theme JSON over `t` (missing keys keep their values). False on a syntax or value error.
bool theme_parse(const char* text, Theme& t, std::string* error);
// The live theme: pc/assets/ui/theme.json, reloaded when it changes (checked at most twice a second),
// tinted (settings ui_menu_tint, tint_theme; Lea Monde keeps the file's colours) and with the menus' opacity
// applied (settings ui_menu_opacity, 0..1: the ground's alpha, the backdrop over the game; panels' fills
// scale with it from the theme's own, opaque at the theme's ground alpha or above).
const Theme& theme();
// The theme file as last parsed (untinted; reloaded as theme() is); *serial counts the parses that changed it.
const Theme& theme_file(int* serial);
// The theme file's path ($ROOD_UI_THEME, else pc/assets/ui/theme.json).
const char* theme_path();
// Recolour the theme's backgrounds toward a tint (0..31 per channel; ui_tint.h): the ground becomes the tint
// (v * 64 / 31, luminance capped so text keeps its contrast); panel, rule and rule2 keep their offsets above
// the ground (the hierarchy), leaning lightly toward the tint's hue; dim and ash lift toward bone only where
// their contrast on the panel would drop; the accent (marks) and accent_soft (the selection wash) likewise lift
// toward bone only where the tint would hide them (contrast under 2.5, a wash nearer the panel than on the
// theme's own). Alphas, text, gold and bars are kept.
void tint_theme(Theme& t, const int rgb31[3]);
// The theme as shown (tint, colours) for the RPC: appends ,"theme":{...}.
void theme_json(std::string& j);
// The opacity the theme file's ground gives (its alpha / 255): ui_menu_opacity's default.
float theme_opacity();

} // namespace ui
