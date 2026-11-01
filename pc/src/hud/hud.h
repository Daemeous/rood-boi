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
    PC_HUD_NAME_WINDOW, PC_HUD_GAUGES, PC_HUD_FIGURE, PC_HUD_STATUS_ICONS, PC_HUD_BATTLE_MODE, PC_HUD_ABILITY_ICONS, PC_HUD_ACTOR_WORD, PC_HUD_TIMER, PC_HUD_EMOTE, PC_HUD_TEXT_FRAME, PC_HUD_TEXT_BAND, PC_HUD_BOX_TEXT, PC_HUD_IMAGE, PC_HUD_TARGET_ROW, PC_HUD_TARGET_LINE, PC_HUD_TARGET_MARKER, PC_HUD_MENU_BACKGROUND, PC_HUD_MENU_COMMAND, PC_HUD_MENU_ROW, PC_HUD_MENU_CURSOR, PC_HUD_MENU_TABS, PC_HUD_OPTION_VALUE, PC_HUD_ITEMS_CATEGORY, PC_HUD_ITEMS_STATUS_BUTTON, PC_HUD_EQUIP_STATS, PC_HUD_DP_PP_BARS, PC_HUD_GEM_SLOTS, PC_HUD_STAT_PAGE, PC_HUD_OK_MARK, PC_HUD_STORAGE_BUTTONS, PC_HUD_STORAGE_COUNTS, PC_HUD_LOOT_CAPACITIES, PC_HUD_DISCARD_COUNT, PC_HUD_RENAME_GRID, PC_HUD_RENAME_BOX, PC_HUD_FILE_MENU, PC_HUD_FILE_ELEMENT, PC_HUD_MENUF, PC_HUD_MENUF_LOAD, PC_HUD_KINDS };
/* gauges: the status display's HP / MP / RISK gauges with their digits and labels, or RISK ZERO (5BF94.c
 * func_800CA2DC's _renderStatBar calls). figure: its five limb sprites, with the game's stance and
 * condition indices. actor_word: a word over an actor (2842C.c's label loop: MISS, func_8009506C; a combat
 * label, func_80095258: the ailments' and buffs' words, a lowered stat's arrow). timer: the time attack's
 * (5BF94.c _renderTimer, while vs_battle_lowerScreenUiState is 1). emote: a balloon over an actor (2842C.c
 * func_80095C18: the dots, '!', '?', the heart; an off-screen actor's arrow; a far actor's sprite), linked
 * into the actor's depth-sorted slot. text_frame: a text box's frame (5BF94.c func_800CD3E4: the talk
 * bubbles' and the information box's shapes), in the box's entries of the text boxes' own ordering table
 * (D_800F51B8). text_band: a text box's band across the screen (func_800CD158: the name window's; the picture
 * behind darkened in steps, black past it, the action's icon), likewise. box_text: a text box's text as
 * _printVariableWidthFont sets it this frame (its glyphs: code, font table, colour style, place, brightness; the
 * typewriter, pages, arrow, sounds and buttons stay the game's), in the box's entry 3. image: a portrait or still
 * (func_800CCA90; its tiled drawer, D_800EC25C[0]: the others stay the game's). target_row: a row of the
 * targeting list (58578.c func_800C1DC4: a part's name by the raw printer, its plate; its fade stepped by the
 * game), in the slot the game linked it into. target_line: a selected row's leader lines (func_800C16FC, func_800C1A40:
 * from the part's point to the row, swept in by their age), in vs_scratch.unk8. target_marker: the area target's
 * marker (func_800C28AC: the brackets' quads, its sprites, one pulsing), in vs_scratch.unk4 - 4. menu_background,
 * menu_command, menu_row, menu_cursor, menu_tabs: MAINMENU's shared frame (58EC.c; every menu screen's): the
 * background's four sprites (its own slot), the command title's sprite, a row of vs_battle_menuItems (its window,
 * label, counts and icons in vs_scratch.unk8 - 4 .. + 12), the pointing hand and the tab arrows (vs_scratch.unk8).
 * option_value: a row's value in our Option pages (pc/src/ui/classic_options.c, the game's printers and gauge, in
 * vs_scratch.unk8 - 4 and + 8). items_category, items_status_button: the Items screen's own (MENU3): its eight
 * category icons (items0.h _renderItemCategory: vs_scratch.unk4 and, the selected one raised, unk8) and the STATUS
 * strip (16C.c: the button's window, vs_scratch.unk4 - 12; the Square button and the word, unk8). equip_stats,
 * dp_pp_bars: MAINMENU's equipment panels (C48.c) as Items draws them (its status views, its Equip menus): the stats
 * panel (vs_mainMenu_renderEquipStats: its words and numbers in vs_scratch.unk4 - 24) and the DP / PP bars
 * (vs_mainMenu_renderDpPpBars: words, digits and gauges in unk4 - 12). gem_slots: a grip's status view's gem icon
 * and slot count (items1.h: unk8 + 4, unk8). stat_page: the class / affinity / type table (C48.c _renderStatPage:
 * its rows' numbers, names, gauges and tiles, the shaded lines, vs_scratch.unk4 - 12; the Square button and the
 * header's glyphs, unk4 - 16). ok_mark: the pulsing OK beside a menu's last choice (MENUC _drawOk, MENUD's top menu:
 * vs_scratch.unk4 - 12). storage_buttons, storage_counts: Storage's (MENUD) button strip (its windows, unk4 - 12; the
 * buttons and words, unk8) and its category counts (the strip's or the top menu's, unk8). loot_capacities: Loot's
 * (MENUB) capacities strip (the bag's counts, its windows, unk4 - 12, buttons and words, unk8). discard_count: a misc
 * item's discard amount (items1.h _discardMenu, MENUD's copy: three digits in the text boxes' font, unk8). rename_grid,
 * rename_box: the rename grid (MENU8 _renderRenameMenu: its buttons, bars, words, name, arrows, table and background;
 * unk4 - 4, unk4 - 8, unk8, unk8 + 4) and a highlight box on it (_highlightSelection: unk8). file_menu,
 * file_element: the file menu (MENU7 260.c _drawFileMenu: the save list, Data's SAVE / LOAD, the game over's LOAD;
 * TITLE 22C.c's copy: the title's LOAD, the clear-data save), drawn at once (no ordering table: PC_CLASSIC_IMMEDIATE):
 * its frame (part 0 the band's two sprites, part 1 the rest, part 2 the title's and game over's background) and each
 * card's or file's panel (_drawFileMenuElement). menuf, menuf_load: MENUF's screens (3B8.c, currentMenu 15: the congratulations,
 * the time attack's start and end, the cube puzzle's start, end and quit; menuf also ENDING's MISSION COMPLETE, D4.c:
 * the same leaves, screen 16): a frame's sprite leaves, draw areas and
 * PRESS BUTTON prompt (unk4 - 28; the prompt's words unk8), and its VRAM uploads (the TIMs _initMenu loads, the CLUT row
 * the end screens pulse: at once, PC_CLASSIC_IMMEDIATE). The kinds
 * from gauges on
 * are classic-only (no modern drawing: the game's own there); visibility stays the game's. */
#define PC_HUD_CLASSIC_ONLY(kind) ((kind) >= PC_HUD_GAUGES && (kind) < PC_HUD_KINDS)
#define PC_HUD_CLAIMABLE(kind) ((kind) > PC_HUD_NONE && (kind) < PC_HUD_KINDS && (kind) != PC_HUD_NAME_WINDOW)
#define PC_HUD_TEXT 32
#define PC_HUD_PART_TEXT 12
#define PC_HUD_PARTS 4
#define PC_HUD_POOL 96
/* pc_hud_fields.anchor: where widescreen puts it. The game's 2D is drawn 3/4 as wide about
 * the picture's centre or the edge it is anchored to (pc_hw_anchor_packets); PICTURE:
 * fractions of the whole picture (x / 320, y / 240), sizes in the picture's own pixels
 * (the Travel prompt, which is ours); SCREEN: the picture's own square pixels from its top-left, 240 high
 * (a plugin's panel, rood.draw.panel: its width 240 times the picture's aspect). */
enum { PC_HUD_ANCHOR_CENTRE, PC_HUD_ANCHOR_LEFT, PC_HUD_ANCHOR_RIGHT, PC_HUD_ANCHOR_PICTURE, PC_HUD_ANCHOR_SCREEN,
    PC_HUD_ANCHOR_GAME /* classic: placed as the game's own 2D is (no anchor of its own: widescreen's edge
                        * extension and cover guesses as for the game's packets) */ };

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
    int rise;             /* buff: how far it was pushed up by the buffs after it (the record's unk12, 10 each) */
    float unit;           /* travel, notice: game pixels per menus' layout unit (720 high) */
    int call_x, call_y;   /* status, chain, buff, damage: the place the game's call was given (its hook's x, y:
                           * the timing word at 44, 48 before its slide) */
    int hp, max_hp, mp, max_mp, risk; /* gauges: Ashley's (the HUD record func_800CA2DC draws) */
    int ability_animation, ability_offset, ability_group, ability_availability;
    int battle_step, battle_pulse; /* BATTLE MODE: draw-time inputs, before phase advance */
    int minutes, seconds, hundredths; /* timer: as the game's string shows them (59:59:99 at most) */
    int box_id, box_x, box_y, box_w, box_h, box_shape, box_flip, box_tail; /* text_frame: the box's (read only) */
    int image_index, image_mode, image_clut; /* image: which (0..2), its drawer, its CLUT word */
    int band_top, band_edge, band_lines, band_settled, band_icon; /* text_band: the box's (read only) */
    int emote_draw, emote_kind, emote_side, emote_z, emote_direction, emote_frame; /* emote: pc_classic_emote's */
    int row_selected, row_fade, row_bank; /* target_row: pc_classic_target_row's (the fade before the game steps it) */
    int row_x, row_y;     /* target_line: the row's place (call_x, call_y the line's start; age the lines' sweep) */
    int marker_lock, marker_step; /* target_marker: the call's lock (arg1), the brightness step (call_x, call_y its place) */
    int menu_id, menu_fade, menu_brightness, menu_slot, menu_frame; /* menu_row: the row (0..39); menu_background:
                                                                     * the fade step, brightness, slot, frame buffer */
    int menu_action, menu_step, menu_color, menu_mode, menu_lit; /* menu_command: the action (call_y its slide);
                                                                  * menu_cursor: step, colour; menu_tabs: mode, step, lit */
    unsigned char menu_item[0x40]; /* menu_row: a copy of its record (vs_battle_menuItem_t) before the emit */
    int opt_row, opt_kind, opt_enabled, opt_focused, opt_index, opt_right, opt_lit, opt_other, opt_gap, opt_bar_colour,
        opt_bar_width, opt_fill, opt_link, opt_upper, opt_window_x; /* option_value: pc_classic_option_value's (call_x
                                                                    * the column's x, call_y the row's y) */
    char opt_value[64], opt_on[24], opt_off[24], opt_label[96];
    struct { /* equip_stats: pc_classic_equip_stats's (classic_hook.h) */
        int position, known, range_risk_shown, range_delta, subtype, range_risk[2];
        int stat[3], threshold[3], base[3], total[3], accessory[3];
    } equip;
    struct { int step, target, dp, max_dp, pp, max_pp; } bars; /* dp_pp_bars: pc_classic_dp_pp_bars's */
    int gem_count;                /* gem_slots: the grip's slots */
    struct { /* stat_page: pc_classic_stat_page's */
        int x_offset, page, enabled, subtype;
        short stats[64];
    } stat_page;
    struct { int x, y, level; } ok;                   /* ok_mark: pc_classic_ok_mark's */
    struct { int x, state; } storage_buttons;         /* storage_buttons: pc_classic_storage_buttons's */
    struct { int layout, x, container, capacity[2][7], count[2][7]; } storage_counts; /* pc_classic_storage_counts's */
    struct { int x, controls, looking, all, capacity[7], count[7], incoming[7]; } loot; /* loot_capacities' */
    struct { int count, x, y; } discard;              /* discard_count: pc_classic_discard_count's */
    struct {
        int x_offset, fade[4], lit, step;
        unsigned char name[20], commands[4][16], table[9][15];
    } rename;                                         /* rename_grid: pc_classic_rename_grid's */
    struct { int x, y, w, h; } rename_box;            /* rename_box: pc_classic_rename_box's */
    struct {
        int part, x, cursor_xy, cursor_color, fade, arrow, has_message;
        unsigned char message[256];
    } file_menu;                                      /* file_menu: pc_classic_file_menu's */
    struct {
        int id, x, y, w, h, selected, unavailable, outer, inner, slot, location, saving, arrow;
        int slot_state, map, saves, clears, hours, minutes, seconds, hp, hp_max, mp, mp_max;
        int progress, progress_x, progress_colour;
        unsigned char text[32], place[48], in_use[32], empty[32], fresh[32];
        unsigned short cluts[2][256];
    } file;                                           /* file_element: pc_classic_file_element's */
    struct { int screen, count; unsigned char prompt[2][8]; } menuf; /* menuf: pc_classic_menuf's (its calls:
                                                                       * pc_classic_menuf_calls, classic_parity.c) */
    struct { int screen, phase, uploads; } menuf_load; /* menuf_load: pc_classic_menuf_load's, the uploads recorded */
    int items_slot; /* items_category: the dim icons' slot (bytes from vs_scratch.unk4) */
    int items_step, items_highlight, items_selected, items_on_row; /* items_category: pc_classic_items_category's;
                                                                    * items_status_button: items_step its slide (row),
                                                                    * items_highlight its state */
    int word_miss, word_label, word_lowered; /* actor_word: MISS (1) or a combat label (D_800690B4's index; lowered:
                                              * a lowered stat's, with the arrow) */
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
    pc_hud_classic_gauges, pc_hud_classic_figure, pc_hud_classic_status_icons, pc_hud_classic_battle_mode, pc_hud_classic_ability_icons,
    pc_hud_classic_actor_word, pc_hud_classic_timer, pc_hud_classic_emote,
    pc_hud_classic_text_frame, pc_hud_classic_text_band, pc_hud_classic_box_text, pc_hud_classic_image,
    pc_hud_classic_target_row, pc_hud_classic_target_line, pc_hud_classic_target_marker,
    pc_hud_classic_menu_background, pc_hud_classic_menu_command, pc_hud_classic_menu_row, pc_hud_classic_menu_cursor,
    pc_hud_classic_menu_tabs, pc_hud_classic_option_value, pc_hud_classic_items_category,
    pc_hud_classic_items_status_button, pc_hud_classic_equip_stats, pc_hud_classic_dp_pp_bars, pc_hud_classic_gem_slots,
    pc_hud_classic_stat_page, pc_hud_classic_ok_mark, pc_hud_classic_storage_buttons, pc_hud_classic_storage_counts,
    pc_hud_classic_loot_capacities, pc_hud_classic_discard_count, pc_hud_classic_rename_grid, pc_hud_classic_rename_box,
    pc_hud_classic_file_menu, pc_hud_classic_file_element, pc_hud_classic_menuf, pc_hud_classic_menuf_load;
/* hud_bridge.c: the glyphs the last box_text element set (6 ints each: code, font table, colour style, x, y,
 * brightness), their count. */
int pc_hud_box_text_glyphs(const int** out) __attribute__((weak));
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
    PC_HUD_CMD_DIGIT,    /* one digit (_renderDigit): a the font (2: the separator), b the digit */
    PC_HUD_CMD_FRAME,    /* a text box's frame by the game's routine (func_800CD3E4) on a copy of the box: a its id
                          * (its ordering-table entries), b the shape, c the flip, d the tail; x, y, w, h */
    PC_HUD_CMD_BAND,     /* a text box's band by the game's routine (func_800CD158) on a copy of the box: a its id, b
                          * top (1: from the top), c its edge (rows grown), d its lines; font settled (its icon),
                          * align the icon (the box's unk0_8) */
    PC_HUD_CMD_CHAR,     /* a glyph of the text boxes' font by the game's printer (vs_battle_printVariableWidthFontChar)
                          * in the element's box: a the code, b the font table, c the colour style, d the brightness;
                          * x, y */
    PC_HUD_CMD_IMAGE,    /* an image by the game's drawer (D_800EC25C[b]) as func_800CCA90 calls it: a the image
                          * (0..2), c its CLUT word; in the kind's slot */
    PC_HUD_CMD_PLATE,    /* a target row's plate as func_800C1DC4 builds it (one packet: a black tile, then a POLY_G4
                          * across): x, y, w x h the gradient's, color its left colour word, color2 its right (PS1 BGR);
                          * a, b the tile's place, c, d its size */
    PC_HUD_CMD_LEADER,   /* a target row's leader line by the game's routine (func_800C16FC; b 1: func_800C1A40, the left
                          * half's) as func_800C1DC4 calls it: x, y its start, c, d where it meets the row, a the count
                          * (its sweep); in the kind's slot (the routine's vs_scratch.unk8) */
    PC_HUD_CMD_QUAD,     /* a textured four-point polygon as the label loop's draws build theirs (POLY_FT4: the
                          * numbers, buffs): a the page word (with its blending), b the CLUT word, c, d its u, v;
                          * x, y, w x h on the screen; font, align the texels' w x h; color the colour word (PS1
                          * BGR, 0x80 full); anchor 1 raw (unmodulated); alpha below 1 semi-transparent;
                          * fit 1: its corners and texels given one by one (corner: from x, y; uv) */
    PC_HUD_CMD_OPTION_TEXT, /* our Option pages' value text (classic_options.c pc_options_text_draw: encoded as the page
                             * draws it, a: capitals, w: the room it is cut to, 0 none; color the font colour 0..3) */
    PC_HUD_CMD_OPTION_BAR,  /* their slider's gauge (pc_options_bar_draw: a the colour, b the fill) */
    PC_HUD_CMD_MENU_INT, /* a number by MAINMENU's printer (vs_mainMenu_renderIntColor: its digits right to left from
                          * x, a '-' before a negative): a the value, color the colour word */
    PC_HUD_CMD_STAT_BAR, /* the game's gauge (vs_battle_renderStatBar): a the colour (0..3), b the fill; x, y */
    PC_HUD_CMD_PACKET,   /* a primitive of the game's words as given (w of them, 1..15: a, b, c, d, then text's words
                          * (aligned, in the draw's arena); the tag its own),
                          * linked into the slot as the game's vs_battle_addTile / vs_battle_insertTpage do */
    PC_HUD_CMD_FONT_TEXT, /* the text boxes' font by the game's printer (vs_battle_printVariableWidthFontChar) after
                           * vs_battle_setFontStyle(a): text its glyph codes (length of them), x, y, b a fixed step
                           * (0: each glyph's width); linked at the slot (the printer's ot + 3) */
    PC_HUD_CMD_BUTTON_BACKGROUND, /* a button's window as MAINMENU's vs_mainmenu_renderButtonBackground builds it (h
                                   * shaded lines, each a draw mode, a POLY_G2 and a draw mode): x, y, w x h */
    PC_HUD_CMD_IMAGE_STORE,   /* VRAM read (x, y, w x h) into the draw's own buffer (StoreImage: d.image_store) */
    PC_HUD_CMD_IMAGE_LOAD,    /* VRAM upload (LoadImage: d.image_load): x, y, w x h, text its pixels (w * h * 2 bytes,
                               * in the draw's arena); with b 1 the draw's buffer (d.image_restore: the last read) */
    PC_HUD_CMD_TEXT_GLYPH,    /* the next packet is a printer's glyph (d.text_glyph): a its code (-1 a space: reported at
                               * once), x, y its pen, b the printer's advance (ui_text's pc_text_glyph; immediate draws) */
    PC_HUD_CMD_MENU_ROW  /* a menu row by the game's own emit (58EC.c pc_mainmenu_render_row) on a copy of the row
                          * the menu_row element recorded: x, y its place, w its window's width (a: which of them
                          * are set, bits 1 x, 2 y, 4 w; else the record's) */
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
    short corner[8];         /* quad with fit 1: x0, y0 .. x3, y3 from x, y, in the packet's vertex order */
    unsigned char uv[8];     /* and their texels u0, v0 .. u3, v3 */
    int layer;               /* the ordering-table slot (bytes from vs_scratch.unk4, d.layer); PC_HUD_LAYER_KIND: the
                              * kind's own */
} pc_hud_classic_cmd;
#define PC_HUD_LAYER_KIND (-0x7FFF)
#define PC_HUD_LAYER_UNK8 (-0x7FFE) /* resolve the actual scratchpad OT pointer each draw */
#define PC_HUD_LAYER_ELEMENT (-0x7FFD) /* the element's own slot, as the game linked it (an emote's depth) */
#define PC_HUD_LAYER_UNK8_AT(bytes) (-0x7F00 + (bytes)) /* vs_scratch.unk8 + bytes (-64..64), resolved each draw */
#define PC_HUD_LAYER_IS_UNK8_AT(layer) ((layer) >= -0x7F40 && (layer) <= -0x7EC0)
#define PC_HUD_CLASSIC_COMMANDS 512 /* (MENUF's congratulations: about 270, the stat wheel's lines) */
/* lua_api_hud.cpp: the claim on the element just recorded draws in the classic flavour;
 * its commands (in drawing order) in *out, their count, or -1 when it failed (the game
 * draws it: its packets stay). */
int pc_hud_classic_draw(const pc_hud_classic_cmd** out) __attribute__((weak));
/* The parity gate (pc/src/classic/classic_parity.h): the classic claim of the element just recorded draws
 * although the game draws it (the parity mode); its commands, or -1 (none made for the classic flavour, it
 * failed: why in *why). */
int pc_hud_classic_parity_draw(const pc_hud_classic_cmd** out, const char** why) __attribute__((weak));
/* Plugins' classic panels (rood.draw.panel(fn, {classic = true}), the classic look): the panel from *cursor on
 * draws now, at the game's frame (hud_bridge.c pc_hud_panels_classic); its commands, their count (or -1: no more),
 * *cursor moved past it, *anchor the range's placement (PC_HUD_ANCHOR_*). */
int pc_hud_classic_panel(int* cursor, const pc_hud_classic_cmd** out, int* anchor) __attribute__((weak));
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
/* hud_bridge.c, the game's frame just before it draws the ordering table `otag` (BATTLE func_8007629C): the
 * classic panels' packets, linked into it. */
void pc_hud_panels_classic(void* otag);
/* Their packets' guest range in the last frame ([first, last), 0, 0: none): RPC hud stats panel_packets. */
void pc_hud_panel_range(unsigned* first, unsigned* last) __attribute__((weak));
/* Then, whatever rood.ui.hide hides: plugins' panels (rood.draw.panel) over the picture (its size in pixels). */
void pc_hud_panels(float dt, int view_w, int view_h);
/* RPC hud (dev_rpc.c): the params object's text; JSON in out, or < 0 with the error. */
int pc_hud_rpc(const char* params, char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
