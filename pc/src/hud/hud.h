/* HUD elements for the Lua visible layer (plan
 * pc/docs/plans/2026-10-18-lua-visible-layer.md). The game decides when and
 * what; a plugin's claim decides how it looks.
 *
 *   game frame: src's PC_HUD hook (hud_hook.h) -> pc_hud_element
 *               (hud_bridge.c, battle builds: the game's record read into
 *               pc_hud_fields) -> pc_hud_record (hud_elements.cpp: a copy in
 *               a fixed pool; 1 when a claim is honoured: the bridge turns
 *               the game's packets into no-ops)
 *   presented frame: dev_overlay.cpp -> pc_hud_frame (lua_api_hud.cpp: each
 *               live element's claim draws into a fixed command buffer) ->
 *               the renderer (hud_draw.cpp, picture space)
 *   presented frame, the Travel prompt: fast_travel_prompt.cpp ->
 *               pc_hud_record (its C++ drawing skipped when honoured); a
 *               notice the same way (savestate_notice.cpp)
 *   game vsync: pc_lua_tick -> held rood.dev.show moments kept, expiry
 *
 * Elements are copies, never pointers into the game. With no claim, a
 * claim that failed, or the classic look (ui_menus 0) without
 * hud_classic_override, nothing is suppressed: the game's own drawing (the
 * fallback). Every call is on the game thread. */
#ifndef ROOD_HUD_H
#define ROOD_HUD_H
#include "../classic/typeset.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The kinds (hud_hook.h repeats the game's): status, the timing word (BATTLE.PRG
 * func_800958A4); chain, "3 CHAINS" (func_80095A4C); buff, an equipment's class or
 * affinity change (func_8009695C: WEAPON HUMAN +1); damage, a number over an actor
 * (func_80094B0C: damage, healing, a stat's change); travel, the Travel prompt on a
 * Circle of Magic (fast_travel_prompt.cpp); notice, a line of the port's own news (the save
 * state slots': "Saved to slot 3", savestate_notice.cpp); name_window, the action's name in
 * text box 7 (5BF94.c vs_battle_displaySceneMessage: an event, not claimed: the game draws it). */
enum { PC_HUD_NONE = 0, PC_HUD_STATUS = 1, PC_HUD_CHAIN, PC_HUD_BUFF, PC_HUD_DAMAGE, PC_HUD_TRAVEL, PC_HUD_NOTICE,
    PC_HUD_NAME_WINDOW, PC_HUD_GAUGES, PC_HUD_FIGURE, PC_HUD_STATUS_ICONS, PC_HUD_BATTLE_MODE, PC_HUD_ABILITY_ICONS, PC_HUD_KINDS };
/* gauges: the status display's HP / MP / RISK gauges with their digits and labels, or RISK ZERO (5BF94.c
 * func_800CA2DC's _renderStatBar calls). figure: its five limb sprites, with the game's stance and
 * condition indices. Both are classic-only (no modern drawing); visibility stays in func_800CA2DC. */
#define PC_HUD_CLAIMABLE(kind) ((kind) > PC_HUD_NONE && (kind) < PC_HUD_KINDS && (kind) != PC_HUD_NAME_WINDOW)
#define PC_HUD_TEXT 32
#define PC_HUD_PART_TEXT 12
#define PC_HUD_PARTS 4
#define PC_HUD_POOL 64
/* pc_hud_fields.anchor: where widescreen puts it. The game's 2D is drawn 3/4 as wide about
 * the picture's centre or the edge it is anchored to (pc_hw_anchor_packets); PICTURE:
 * fractions of the whole picture (x / 320, y / 240), sizes in the picture's own pixels
 * (the Travel prompt, which is ours); SCREEN: the picture's own square pixels from its top-left, 240 high
 * (a plugin's panel, rood.draw.panel: its width 240 times the picture's aspect). */
enum { PC_HUD_ANCHOR_CENTRE, PC_HUD_ANCHOR_LEFT, PC_HUD_ANCHOR_RIGHT, PC_HUD_ANCHOR_PICTURE, PC_HUD_ANCHOR_SCREEN };

/* A piece of an element as the game drew it (the buff's words, sign and digits). */
typedef struct {
    float x, y, w, h; /* its box (x, y the top-left) */
    char text[PC_HUD_PART_TEXT];
} pc_hud_part;

/* One element as the game drew it this frame. */
typedef struct {
    int kind;             /* PC_HUD_* */
    const void* key;      /* the game's record: identity only, never read through */
    char text[PC_HUD_TEXT];
    float x, y, w, h;     /* the game's drawing's box in its screen (320 x 224 in the field): x, y its centre
                           * (damage: x, y where the game placed it, before its bounce; travel:
                           * the picture's point the prompt is anchored to) */
    unsigned rgb;         /* 0xRRGGBB: the game's colour (its 0x80 = full) */
    int timing;           /* status: 0 too fast, 1 fast, 2 success, 3 slow, 4 too slow */
    int variant;          /* status: which success word (COOL!, GOOD!, ...); travel: the
                           * prompt's anchor (0..8: top left .. bottom right); name_window: its
                           * message (the scene message template: 11 a technique's name, 12 a
                           * spell mastered, ...) */
    int value;            /* chain: the count; buff, damage: the amount; notice: the save state
                           * slot it is about (0: none); name_window: the action (vs_main_actions'
                           * index) */
    int sign;             /* buff, damage: +1 a gain (a heal), -1 a loss (damage) */
    int pool;             /* damage: 0 HP, 1 MP, 2 a stat (stat: which) */
    int stat;             /* buff: the class or affinity (0..12: HUMAN .. DARKNESS); damage: the stat
                           * changed (0..5: its icon HP, MP, STR, INT, AGL, RISK) */
    int slot;             /* buff: the equipment (0..6: WEAPON, SHIELD, R.ARM, L.ARM, HEAD, BODY, LEGS) */
    float bounce;         /* damage: its digits' rise this frame (game pixels, - up) */
    float unit;           /* travel, notice: game pixels per menus' layout unit (720 high) */
    int call_x, call_y;   /* status, chain, buff, damage: the place the game's call was given (its hook's x, y:
                           * the timing word at 44, 48 before its slide) */
    int hp, max_hp, mp, max_mp, risk; /* gauges: Ashley's (the HUD record func_800CA2DC draws) */
    int ability_animation, ability_offset, ability_group, ability_availability;
    int battle_step, battle_pulse; /* BATTLE MODE: draw-time inputs, before phase advance */
    int icon_previous, icon_current, icon_next, icon_count, icon_selected, icon_offset;
    int stance, limb_status[5]; /* figure: stance 0/1, game's limb condition indices (read-only) */
    int slide;            /* gauges: D_800EB9B0, the display's slide up (0x80000 a step; packed into its y) */
    int glyph;            /* travel: the button whose icon it shows (PC_PROMPT_*) */
    int age, life;        /* game frames since it appeared, and when it goes (travel: 0, 0) */
    int anchor;           /* PC_HUD_ANCHOR_* */
    int part_count;
    pc_hud_part parts[PC_HUD_PARTS];
} pc_hud_fields;

/* ---- hud_elements.cpp ---- */
/* The game drew an element (hud_bridge.c): returns 1 when the game's
 * packets are to be suppressed (a claim on its kind is honoured; the modern
 * pass draws it), 2 when the claim draws it now in the classic flavour
 * (pc_hud_classic_draw, then the bridge's packets), 0 for the game's own. */
int pc_hud_record(const pc_hud_fields* f);
/* settings hud_classic_override (project_settings.cpp): with the classic look
 * (ui_menus 0) every claim is honoured with it (0, default). Without it a kind's
 * claim is honoured there only when it was made for the classic flavour
 * (rood.hud.claim(kind, fn, {flavour = "classic"}): rood.classic's) and the kind's
 * own setting is on: hud_classic_status, _chain, _buff, _damage (on for the kinds
 * whose Lua draw passes the parity gate, pc/src/classic/classic_parity.h). With the
 * parity mode on nothing is honoured there: the game draws and the Lua draw is
 * compared. */
extern int pc_hud_classic_override;
extern int pc_hud_classic_status, pc_hud_classic_chain, pc_hud_classic_buff, pc_hud_classic_damage,
    pc_hud_classic_gauges, pc_hud_classic_figure, pc_hud_classic_status_icons, pc_hud_classic_battle_mode, pc_hud_classic_ability_icons;
int pc_hud_classic_default(int kind); /* the kind's hud_classic_<kind> */
/* Whether claims on `kind` are honoured now (a claim, the look, the overlay drawing). */
int pc_hud_honoured(int kind);
/* Game vsync (pc_lua_tick): held moments kept, gone elements freed. */
void pc_hud_tick(void);

/* ---- hud_bridge.c (battle builds; absent elsewhere: weak) ---- */
/* The field is live (a room loaded, the battle overlay's HUD running). */
int pc_hud_bridge_field(void) __attribute__((weak));
/* What rood.dev.show asks for (hud_internal.h ShowArgs, as plain C). */
typedef struct {
    int timing, variant, age, value, sign, pool, stat, slot; /* name_window: variant the message, value the action */
} pc_hud_spawn;
/* The name window as it is now (text box 7 open with the scene message's text): 1 and its fields. */
int pc_hud_bridge_name_window(pc_hud_fields* f) __attribute__((weak));
/* The game's own spawner for kind (status: vs_battle_renderBattleAbilityTimingResult,
 * chain: func_80093F24, buff: func_80096768, damage: func_80093B68 on Ashley, travel:
 * the forced prompt, fast_travel.c); 0 when it could not. */
int pc_hud_bridge_spawn(int kind, const pc_hud_spawn* s) __attribute__((weak));
/* Keeps the game's element of kind (the one spawned) at s->age (each vsync); 0 when it has none. */
int pc_hud_bridge_hold(int kind, const pc_hud_spawn* s) __attribute__((weak));
/* Lets it go: its last frames play out. */
void pc_hud_bridge_release(int kind, const pc_hud_spawn* s) __attribute__((weak));

/* ---- the classic flavour (ui_menus 0 with hud_classic_override) ----
 * A claim's draw runs inside the game's frame, at the element's hook, into
 * these commands; the bridge turns them into PS1 packets in the HUD labels'
 * ordering-table slot, anchored as the element is in widescreen: the game's
 * sprites, window frames and text printers (ui_text restyles their text as
 * it does the game's own). Colours are 0xRRGGBB (0xFF full; textured
 * primitives modulate the picture: white leaves it as it is). */
enum {
    PC_HUD_CMD_TEXT,     /* the game's font: font (PC_UI_TEXT_*), align, anchor; fit: d inside w x h */
    PC_HUD_CMD_RECT,     /* a tile */
    PC_HUD_CMD_GRADIENT, /* color to color2, across (a 0) or down (a 1) */
    PC_HUD_CMD_GLYPH,    /* the game's button sprite (a: PC_PROMPT_*), its middle at y */
    PC_HUD_CMD_SPRITE,   /* VRAM: a the texture page word, b the CLUT word, c, d its u, v; w x h */
    PC_HUD_CMD_WINDOW,   /* the text boxes' window frame (a: its brightness, 0x80 full) */
    PC_HUD_CMD_CURSOR,   /* the menus' pointing hand (a: its brightness step 0..15) */
    PC_HUD_CMD_BAR,      /* the HP / MP / RISK bar: a value, b max, c pool (0 HP, 1 MP, 2 RISK) */
    PC_HUD_CMD_TEXT_RAW, /* the game's raw printer (vs_battle_renderTextRawColor) as the game calls it: text as
                          * given ('$' centres, '#' right-aligns, its blanks), x, y, a the colour word (PS1
                          * BGR, 0x80 full); typeset: ui_text's status layout for it (font 4) */
    PC_HUD_CMD_VALUE,    /* the HUD's digits (vs_battle_renderValue): a the font (0 small, 1 large), b the value,
                          * x, y its rightmost digit */
    PC_HUD_CMD_DIGIT     /* one digit (_renderDigit): a the font (2: the separator), b the digit */
};
typedef struct {
    int type;
    int x, y, w, h;
    unsigned color, color2;
    float alpha;
    int font, align, anchor; /* text: align 0 left, 1 centre, 2 right; anchor 0 top, 1 middle, 2 baseline, 3 bottom */
    int fit;                 /* text: placed inside the box x, y, w, h (the game's font does not scale) */
    const char* text;
    int length;
    int a, b, c, d;
    pc_text_typeset typeset; /* text_raw with font 4 (the status style) */
    int layer;               /* the ordering-table slot (bytes from vs_scratch.unk4, d.layer); PC_HUD_LAYER_KIND: the
                              * kind's own */
} pc_hud_classic_cmd;
#define PC_HUD_LAYER_KIND (-0x7FFF)
#define PC_HUD_LAYER_UNK8 (-0x7FFE) /* resolve the actual scratchpad OT pointer each draw */
#define PC_HUD_CLASSIC_COMMANDS 256
/* lua_api_hud.cpp: the claim on the element just recorded draws in the classic flavour;
 * its commands (in drawing order) in *out, their count, or -1 when it failed (the game
 * draws it: its packets stay). */
int pc_hud_classic_draw(const pc_hud_classic_cmd** out) __attribute__((weak));
/* The parity gate (pc/src/classic/classic_parity.h): the classic claim of the element just recorded draws
 * although the game draws it (the parity mode); its commands, or -1 (none made for the classic flavour, it
 * failed: why in *why). */
int pc_hud_classic_parity_draw(const pc_hud_classic_cmd** out, const char** why) __attribute__((weak));
/* The anchor the last classic draw placed its commands by (d.anchor: PC_HUD_ANCHOR_*), -1: the element's. */
int pc_hud_classic_anchor(void) __attribute__((weak));
/* hud_bridge.c: the game's font's width and height of text (font PC_UI_TEXT_*), and a
 * button sprite's width (0: the game has none for it). */
void pc_hud_classic_measure(int font, const char* text, int length, int* w, int* h) __attribute__((weak));
int pc_hud_classic_glyph(int button) __attribute__((weak));

/* ---- lua_api_hud.cpp ---- */
/* Presented frame (dev_overlay.cpp, the ImGui frame open): claims draw the
 * live elements, then the renderer draws their commands. */
void pc_hud_frame(void);
/* Then, whatever rood.ui.hide hides: plugins' panels (rood.draw.panel) over the picture (its size in pixels). */
void pc_hud_panels(float dt, int view_w, int view_h);
/* RPC hud (dev_rpc.c): the params object's text; JSON in out, or < 0 with the error. */
int pc_hud_rpc(const char* params, char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
