// Modern menu drawing helpers shared by ui_draw.cpp (the shell: tabs, rows,
// hints) and the screens (ui_equip_draw.cpp). Layout units: 1280 x 720 (16:9)
// or 960 x 720 (4:3), scaled into the game picture.
#pragma once
#include "ui/ui_shell.h"
#include "ui/ui_theme.h"
#include "imgui.h"
#include <string>

namespace uidraw {

struct Fonts {
    ImFont* name = nullptr;  // item names, card titles, descriptions
    ImFont* label = nullptr; // tabs, labels, hints
    ImFont* data = nullptr;  // numbers
};
extern Fonts fonts;

struct Frame {
    ImDrawList* dl;
    float ox, oy, k; // window = origin + layout * k
    float W;         // layout width (1280 or 960)
    ImVec2 p(float x, float y) const { return ImVec2(ox + x * k, oy + y * k); }
    float s(float v) const { return v * k; }
};

// Letter-spaced text (tracking in em) with its top-left at (x, y); returns the
// advance in layout units. measure_only draws nothing.
float text(const Frame& f, ImFont* font, float size, float x, float y, unsigned col, const char* str,
    float tracking = 0, bool measure_only = false);
inline float measure(const Frame& f, ImFont* font, float size, const char* str, float tracking = 0)
{
    return text(f, font, size, 0, 0, 0, str, tracking, true);
}
// Right-aligned at x; returns the width.
float text_right(const Frame& f, ImFont* font, float size, float x, float y, unsigned col, const char* str,
    float tracking = 0);
// str cut to max_w with an ellipsis when it is wider.
std::string fit(const Frame& f, ImFont* font, float size, const char* str, float max_w, float tracking = 0);
// ASCII and Latin-1 letters to capitals ("Léa Monde" -> "LÉA MONDE").
std::string upper(const char* s);
float approach(float cur, float target, float dt, float seconds);
// The prompt icon of PS1 button `button` (PC_PROMPT_*) `size` high with its
// left edge at x, centred on cy; returns its width (0: no icon). measure_only draws nothing.
float glyph(const Frame& f, int button, float size, float x, float cy, bool measure_only = false,
    unsigned tint = 0xFFFFFFFFu);
// A small rotating arc (loading) centred at (cx, cy).
void spinner(const Frame& f, float cx, float cy, float r, unsigned col);
// colour with its alpha multiplied by a (0..1)
unsigned fade(unsigned col, float a);

// The pointer this frame, in layout units, and what it is on (hit tests in
// drawing order; the last hit wins).
struct Pointer {
    bool ok = false;    // the shell may take it (no editor window under it, not held)
    float x = 0, y = 0;
    bool moved = false, clicked = false, right = false;
    bool down = false; // the left button is held (drags)
    bool middle = false; // the middle button is held (the Map frame's pan drag)
    bool shift = false;  // Shift is held (the Map frame: a left drag started with it pans)
    bool rpc = false;  // a test pointer (pc_ui_draw_pointer)
    float wheel = 0;
    int kind = PC_UI_MOUSE_NONE, index = 0;
    bool over(float x0, float y0, float x1, float y1) const { return ok && x >= x0 && x < x1 && y >= y0 && y < y1; }
    bool hit(int k, int i, float x0, float y0, float x1, float y1)
    {
        if (!over(x0, y0, x1, y1))
            return false;
        kind = k, index = i;
        return true;
    }
};

// A hint at the bottom right: one or two prompt icons and a verb; a click
// presses `bits` (PC_UI_*).
struct Hint {
    int b0, b1; // PC_PROMPT_*, b1 -1 for none
    const char* verb; // "": an icon only, grouped with the hint after it
    unsigned bits;
};
struct ScreenOut {
    Hint hints[6];
    int hint_count = 0;
    std::string crumb; // bottom left ("Equipment > Weapon"), cut to fit left of the hints
    std::string crumb_tail; // after the crumb, never cut ("· 1:02:33")
    bool busy = false; // a loading indicator by the crumb
    float fade = 0;    // a black over the whole view after the footer (the Data screen's load fade), 0..1
    void hint(int b0, int b1, const char* verb, unsigned bits)
    {
        if (hint_count < 6)
            hints[hint_count++] = {b0, b1, verb, bits};
    }
};

// ---- Pieces the screens share (ui_equip_draw.cpp) ----
extern const char* const CLASSES[PC_UI_CLASSES];
extern const char* const AFFINITIES[PC_UI_AFFINITIES];
extern const char* const DAMAGE[3]; // pc_ui_item.damage_type
extern const char* const DOT;       // " · "
// Our own line icons on a 20 x 20 grid, `size` wide, centred at (cx, cy).
enum Icon { IC_SWORD, IC_SHIELD, IC_HEAD, IC_BODY, IC_ARM, IC_LEGS, IC_RING };
void icon(const Frame& f, Icon ic, float cx, float cy, float size, unsigned col, bool mirror = false);
// The focused row's band (as the command rows): soft crimson fading right, a bar on the left.
void focus_band(const Frame& f, const ui::Theme& t, float x0, float y0, float x1, float y1);
// A small capitals label (letter-spaced), ash unless col.
void label(const Frame& f, const ui::Theme& t, float x, float y, const char* s, unsigned col = 0);
// A five-pointed star centred at (cx, cy), radius r (the bundled fonts have no U+2605).
void star(const Frame& f, float cx, float cy, float r, unsigned col);
// "+4" (better), "-2" (worse), "·" (same), right-aligned at x; returns the width.
float delta(const Frame& f, const ui::Theme& t, float x, float y, int d, float size = 13);
std::string num(int v);
std::string signed_num(int v);
// The item's kind line: damage type (weapons), material, two-handed.
std::string kind_line(const pc_ui_item& it);
// Body text wrapped to max_w; returns its height.
float paragraph(const Frame& f, const ui::Theme& t, float x, float y, float max_w, unsigned col, const char* str,
    float size = 0);
// A card: begin, draw its content, end with its bottom (the panel goes behind).
struct Card {
    float x, y, w;
};
void card_begin(const Frame& f);
void card_end(const Frame& f, const ui::Theme& t, const Card& c, float bottom);
// Class or affinity bars from x to xr: one row per entry, a tick at `now` when
// comparing (!same); returns their height.
float bars(const Frame& f, const ui::Theme& t, float x, float xr, float y, const char* const* names, const int* now,
    const int* after, int count, bool same, unsigned col);

// The Equipment tab's body (ui_equip_draw.cpp), below the tabs from `top`.
// fresh: the tab was just entered (place animations at once).
void draw_equipment(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The Ashley tab's body (ui_ashley_draw.cpp). model_shown: the live model is
// drawn in pc_ui_draw_model_column's rect (the backdrop leaves that rect open then).
void draw_ashley(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, bool model_shown, Pointer& m,
    ScreenOut& out);
// The System tab's body (ui_system_draw.cpp): the list, the page's card, a capture's prompt.
void draw_system(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The System tab's Quick manual (ui_manual_draw.cpp): the topics, the page card (draw_system hands it over).
void draw_manual(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The Abilities tab's body (ui_abilities_draw.cpp): Chain / Defense (slots, learned list, card) and Break Arts
// (categories, arts, card).
void draw_abilities(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The Magick tab's body (ui_magick_draw.cpp): school / Teleport sub-tabs, the list, the card.
void draw_magick(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The Items tab's body (ui_items_draw.cpp): category chips, the list, the open menu, the card.
void draw_items(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The Items tab's Workshop sub-tab (ui_workshop_draw.cpp), below the chips from `top`: the commands, the open
// flow, the preview card.
void draw_workshop(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The Loot screen's body (ui_loot_draw.cpp), below its title from `top`: the
// bag's capacities, the loot list, the compare card, the menus and the
// Organize inventory panel.
void draw_loot(const Frame& f, const ui::Theme& t, float top, float dt, bool fresh, Pointer& m, ScreenOut& out);
// The Items list's icon for a row of `category` (ui_items_draw.cpp): its id, sub line and name pick it.
void bag_icon(const Frame& f, int category, int id, const char* sub, const char* name, float cx, float cy, float size,
    unsigned col);
// The tab strip with `cur` lit; returns its base line. mode 0: the shell's; 1: the Map frame's (its clicks
// are requests for the map screen); 2: the Data frame's (goals for MENU7: PC_UI_MOUSE_DATA_TAB).
float draw_tabs(const Frame& f, const ui::Theme& t, int cur, Pointer& m, float dt, int mode, bool steps_lit = true);
// The Map frame (ui_map_draw.cpp): the chrome around the game's map screen (MENU5), its pointer included.
void draw_map_frame(const Frame& f, const ui::Theme& t, float dt, Pointer& m);
// The native Data screen (ui_data_draw.cpp): everything but the footer, whose hints it fills.
void draw_data_frame(const Frame& f, const ui::Theme& t, float dt, Pointer& m, ScreenOut& out);
// The Ashley screen's model column (layout units x, y, w, h) on a W-wide layout.
void model_column(const ui::Theme& t, float W, float out[4]);
// The right card's width (16:9: 396, 4:3: 344).
inline float card_width(float W) { return W < 1100 ? 344.0f : 396.0f; }

} // namespace uidraw
