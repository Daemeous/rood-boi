// The HUD layer's internals shared by hud_elements.cpp (the pool, held
// moments), lua_api_hud.cpp (claims, the draw API, rood.dev, the pass, RPC
// hud) and hud_draw.cpp (the renderer). No allocation per frame: the pool,
// the styles and the command buffer are fixed arrays.
#pragma once
#include "hud/hud.h"
#include <cstddef>
#include <string>

namespace hud {

// ---- the pool (hud_elements.cpp) ----
struct Element {
    bool used = false;
    unsigned id = 0;           // this appearance (a new one when the game's age starts over)
    pc_hud_fields f = {};
    unsigned first_seen = 0;   // vsyncs
    unsigned last_seen = 0;
    bool synthetic = false;    // made by rood.dev.show away from the field: no game packets
    bool held = false;         // rood.dev.show's hold: kept until rood.dev.clear
    int hold_age = 10;         // the age it is held at
    unsigned respawn_vsync = 0; // a held game element that went: spawned again from then on
    bool suppressed = false;   // its packets were turned into no-ops this frame
    unsigned drawn = 0;        // presents a claim drew it in
};
Element* pool(); // PC_HUD_POOL
unsigned vsync(); // pc_vsync_count, or the ticks seen (lua_probe)
// Shown in the presented picture: seen within the last few vsyncs, or synthetic.
bool live(const Element& e);
const char* kind_name(int kind);  // "status", "chain", "buff", "damage", "travel"; "" for none
int kind_of(const char* name);    // PC_HUD_*, 0 unknown
#define HUD_KIND_LIST "status, chain, buff, damage, travel, notice, name_window, gauges"
// Status: "too_fast", "fast", "success", "slow", "too_slow" (0..4); -1 unknown.
int timing_of(const char* name);
const char* timing_name(int timing);
void status_text(int timing, int variant, char* out, size_t size); // the game's word ("SLOW!")
// Buff: the equipment's word (0..6: "WEAPON" ..) and the class's or affinity's (0..12: "HUMAN" ..
// "DARKNESS"); damage: the pool's name ("hp", "mp", "stat"). Names in lower case for args.
const char* slot_word(int slot);
const char* stat_word(int stat);
const char* pool_name(int pool);
int word_index(const char* name, const char* (*word)(int), int count); // either case; -1 unknown

struct ShowArgs {
    int timing = 3, variant = 0, age = 10; // name_window: variant its message (11 a technique's name)
    bool hold = true;
    int value = -1;            // chain: the count (3); buff: 1; damage: 123; name_window: the action (184)
    char text[PC_HUD_TEXT] = ""; // name_window away from the field: its text
    int sign = 0;              // buff +1, damage -1 (a hit) unless given
    int pool = 0, stat = 0, slot = 0;
};
// rood.dev.show: the game's own spawner in the field, else a synthetic element;
// held until clear() when args.hold. False (and why) for a kind without a show.
bool show(int kind, const ShowArgs& args, std::string* error);
void clear(int kind); // 0: every kind
// The held moments (rood.dev.bench saves them beside its save state: they are the host's, not the game's).
struct HeldMoment {
    int kind;
    ShowArgs args;
};
int held_moments(HeldMoment* out, int max);
// Hold again after a restart resumed a save state: the game's element is in it (held at its age), so it
// is found, not spawned (spawned again if it is not there); away from the field a synthetic one.
void adopt(int kind, const ShowArgs& args);

struct KindStats {
    unsigned long long seen = 0;       // pc_hud_record calls
    unsigned long long suppressed = 0; // ... whose packets were no-ops (a claim honoured)
    unsigned long long fallback = 0;   // ... drawn by the game (no claim honoured)
    unsigned long long drawn = 0;      // claim draws that finished
    unsigned long long faults = 0;     // claim draws that failed (the claim then stops being honoured)
};
KindStats& stats(int kind);

// New appearances (pc_hud_record's and the synthetic ones), for the hud.<kind> events: taken at each tick
// (lua_api_hud.cpp); past 64 between two ticks the oldest are dropped.
struct Appearance {
    int slot;    // in pool()
    unsigned id; // the element's (gone or reused: not emitted)
};
int take_appearances(Appearance* out, int max);

// The element pc_hud_record was last called for (the classic flavour draws it at once).
Element* last_recorded();
// The classic look (ui_menus 0): claims draw only in the classic flavour, at the game's hooks.
bool classic_look();

// ---- claims (lua_api_hud.cpp) ----
bool claimed(int kind); // a claim stands and has not failed (the owner for the look now)
bool claimed_classic(int kind); // ... and it was made for the classic flavour only (rood.classic's)

// ---- the command buffer (lua_api_hud.cpp fills it, hud_draw.cpp draws it) ----
enum Align { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT };
enum VAnchor { V_TOP, V_MIDDLE, V_BASELINE, V_BOTTOM };
struct Style {
    int font = 5;        // a ui_text style's font (PC_UI_TEXT_*): labels
    float size = 10;     // em height in the game's pixels
    float cap = 0;       // > 0: the size that makes a capital H this tall (the game's pixels) instead
    unsigned color = 0xFFFFFF;
    float alpha = 1;
    float stroke = 0;    // the outline's width in the game's pixels (0: none)
    unsigned stroke_color = 0;
    float stroke_alpha = 1;
    float tracking = 0;  // added letter spacing in em
    float stretch = 1;   // horizontal scale
    int align = ALIGN_LEFT;
    int anchor = V_TOP;
    bool operator==(const Style& o) const;
};
#define HUD_STYLES 256
#define HUD_COMMANDS 1024
#define HUD_TEXT_ARENA (16 << 10)
struct StyleSlot {
    bool used = false;
    unsigned serial = 0; // bumped when freed: handles to it go stale
    unsigned refs = 0;   // live handles (a collected handle lets go; the last frees the slot)
    std::string owner;   // the plugin
    Style style;
};
StyleSlot* styles(); // HUD_STYLES
enum CommandType { CMD_TEXT, CMD_RECT, CMD_GLYPH, CMD_GRADIENT };
#define HUD_NO_STYLE 0xFFFF
struct Command {
    unsigned char type;
    unsigned char anchor;  // the element's (pc_hud_fields.anchor)
    unsigned short style;  // CMD_TEXT; CMD_GLYPH: its outline's (HUD_NO_STYLE: none)
    float x, y, w, h;      // the game's screen, 320 x 224 in the field (CMD_TEXT with fit: its box;
                           // CMD_GLYPH: x its left, y its middle, h its height)
    unsigned color;        // 0xRRGGBB (CMD_TEXT: the style's unless given; CMD_GLYPH: a tint)
    unsigned color2;       // CMD_GRADIENT: the far end's (right, or bottom when vertical)
    bool vertical;         // CMD_GRADIENT: top to bottom
    float alpha;
    unsigned text, length; // CMD_TEXT: in text_arena()
    bool fit;              // CMD_TEXT: fitted inside w x h (w 0: any width), the outline kept in
    int button;            // CMD_GLYPH: PC_PROMPT_*
};
Command* commands(); // HUD_COMMANDS
int command_count();
const char* text_arena();

// ---- the renderer (hud_draw.cpp; absent in lua_probe) ----
// Draws the command buffer over the presented picture (the foreground draw list).
extern void (*render)(const Command* commands, int count, const char* text);
// The text's advance and height in the game's pixels (inside the ImGui frame);
// without a renderer an estimate (lua_probe).
extern void (*measure)(const Style& style, const char* text, size_t length, float* w, float* h);
// A prompt icon's width `h` high in the game's pixels (inside the ImGui frame; 0: no icon);
// without a renderer h (lua_probe).
extern float (*glyph_width)(int button, float h);

} // namespace hud
