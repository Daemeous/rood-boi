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
const char* kind_name(int kind);  // "status"; "" for none
int kind_of(const char* name);    // PC_HUD_*, 0 unknown
// Status: "too_fast", "fast", "success", "slow", "too_slow" (0..4); -1 unknown.
int timing_of(const char* name);
const char* timing_name(int timing);
void status_text(int timing, int variant, char* out, size_t size); // the game's word ("SLOW!")

struct ShowArgs {
    int timing = 3, variant = 0, age = 10;
    bool hold = true;
};
// rood.dev.show: the game's own spawner in the field, else a synthetic element;
// held until clear() when args.hold. False (and why) for a kind without a show.
bool show(int kind, const ShowArgs& args, std::string* error);
void clear(int kind); // 0: every kind

struct KindStats {
    unsigned long long seen = 0;       // pc_hud_record calls
    unsigned long long suppressed = 0; // ... whose packets were no-ops (a claim honoured)
    unsigned long long fallback = 0;   // ... drawn by the game (no claim honoured)
    unsigned long long drawn = 0;      // claim draws that finished
    unsigned long long faults = 0;     // claim draws that failed (the claim then stops being honoured)
};
KindStats& stats(int kind);

// ---- claims (lua_api_hud.cpp) ----
bool claimed(int kind); // a claim stands and has not failed

// ---- the command buffer (lua_api_hud.cpp fills it, hud_draw.cpp draws it) ----
enum Align { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT };
enum VAnchor { V_TOP, V_MIDDLE, V_BASELINE, V_BOTTOM };
struct Style {
    int font = 5;        // a ui_text style's font (PC_UI_TEXT_*): labels
    float size = 10;     // em height in the game's pixels
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
    std::string owner;   // the plugin
    Style style;
};
StyleSlot* styles(); // HUD_STYLES
enum CommandType { CMD_TEXT, CMD_RECT };
struct Command {
    unsigned char type;
    unsigned char anchor;  // the element's (pc_hud_fields.anchor)
    unsigned short style;  // CMD_TEXT
    float x, y, w, h;      // the game's 320 x 240 screen
    unsigned color;        // 0xRRGGBB (CMD_TEXT: the style's unless given)
    float alpha;
    unsigned text, length; // CMD_TEXT: in text_arena()
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

} // namespace hud
