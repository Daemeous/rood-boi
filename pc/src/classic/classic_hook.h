/* The classic look drawn in Lua (plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md
 * §2): the game side's hook, under ROOD_PC only.
 *
 * PC_CLASSIC wraps the call that draws one of the game's classic elements:
 * the packets the statement allocates from the scratchpad's packet pointer
 * (0x1F800000), its place and the game's record go to pc_classic_site
 * (pc/src/classic/classic_bridge.c), which
 *   - keeps them as the kind's capture (RPC packets, the parity gate),
 *   - and, when a plugin's classic claim on the kind is honoured, turns them
 *     into no-ops and has the claim emit its own in the same ordering-table
 *     slot (emit-only suppression: the statement still runs, so whatever
 *     game state it steps is stepped as before).
 * Without a claim nothing changes: the game's own drawing is the fallback.
 * The kinds are classic_parity.h's PC_CLASSIC_* (the HUD layer's kinds keep
 * their numbers: hud_hook.h's PC_HUD is this hook). */
#ifndef ROOD_CLASSIC_HOOK_H
#define ROOD_CLASSIC_HOOK_H
#ifdef ROOD_PC
#include "classic_parity.h"
#define PC_CLASSIC_SITE_GAUGES 8       /* classic_parity.h PC_CLASSIC_GAUGES: func_800CA2DC's gauges */
#define PC_CLASSIC_SITE_FIGURE 9       /* Ashley's five limb sprites */
/* Snapshot at the status carousel's emit boundary; no game pointers escape. */
typedef struct {
    int previous, current, next, count, selected, offset, slide;
} pc_classic_status_icons;
#define PC_CLASSIC_SITE_STATUS_ICONS 10
typedef struct { int step, slide, pulse; } pc_classic_battle_mode;
#define PC_CLASSIC_SITE_BATTLE_MODE 11
typedef struct { int animation, offset, group, availability; } pc_classic_ability_icons;
#define PC_CLASSIC_SITE_ABILITY_ICONS 12
/* A word over an actor at the label loop's call (2842C.c): its record (identity only), which draw the game chose
 * (MISS, or the combat label), and that draw's inputs, copied before it runs. */
typedef struct { const void* label; int miss, age, index, lowered; } pc_classic_actor_word;
#define PC_CLASSIC_SITE_ACTOR_WORD 13
#define PC_CLASSIC_SITE_TIMER 14 /* _renderTimer: its record the string it composed (D_800EC258) */
/* An emote over an actor (2842C.c func_80095C18), as the game decided it inside the function: draw 1 the
 * balloon and its marks (kind, side, the screen place, z the depth slot), 2 the off-screen arrow
 * (func_8009651C: direction, the clamped place), 3 a far actor's sprite (frame); 0 nothing. */
typedef struct { int draw, kind, side, x, y, z, direction, frame; } pc_classic_emote;
#define PC_CLASSIC_SITE_EMOTE 15
#define PC_CLASSIC_SITE_TEXT_FRAME 16 /* func_800CD3E4: its record the text box (read only), x its id */
#define PC_CLASSIC_SITE_TEXT_BAND 17 /* func_800CD158: likewise */
#define PC_CLASSIC_SITE_BOX_TEXT 18 /* _printVariableWidthFont: likewise (its glyphs recorded as it sets them) */
/* An image (func_800CCA90: the portraits and stills): its index (0..2), drawer (D_800EC25C), CLUT word and slot
 * (bytes from vs_scratch.unk4: the table's far end, or after unk8). */
typedef struct {
    int image, mode, clut, slot;
    void* gim; /* its record (gim_t: identity, for the drawer) */
} pc_classic_image;
#define PC_CLASSIC_SITE_IMAGE 19
/* A target row (58578.c func_800C1DC4: a part's name and its plate), as the game reads it before it draws: the row
 * (identity only), its name, place (x the row's, y with the list's offset), selected, the plate's fade step
 * (before the call steps it), its colour bank and its ordering-table slot (bytes from vs_scratch.unk4). */
typedef struct {
    const void* row;
    const char* text;
    int x, y, selected, fade, bank, slot;
} pc_classic_target_row;
#define PC_CLASSIC_SITE_TARGET_ROW 20
/* A selected row's leader lines (func_800C1DC4's func_800C16FC / func_800C1A40 calls): the row (identity only), the
 * line's start (its 3D point projected), the row's place and the line's age (unk3: the lines' sweep). */
typedef struct {
    const void* row;
    int x, y, row_x, row_y, age;
} pc_classic_target_line;
#define PC_CLASSIC_SITE_TARGET_LINE 21
/* The area target's marker (58578.c func_800C28AC): its place (the point projected), lock (the call's arg1: the
 * pitch locked, the brackets' quads off and the other sprites) and its brightness step (D_800EA6AC, stepped). */
typedef struct { int x, y, lock, step; } pc_classic_target_marker;
#define PC_CLASSIC_SITE_TARGET_MARKER 22
/* MAINMENU's shared frame (58EC.c), every menu screen's: the background (its fade step, the opaque brightness, its
 * slot: words from vs_scratch.unk4, the frame buffer), the command title's sprite (the action shown, its slide),
 * a row of vs_battle_menuItems (its emit after its slide is stepped: the row, identity only; its id; a copy of its
 * record, 0x40 bytes, before the emit steps its gradient), the pointing hand (vs_mainMenu_renderCursor: its
 * step, packed place, colour) and the tab arrows (vs_mainMenu_renderTabNavigation: mode, the step just taken,
 * lit). */
typedef struct { int fade, brightness, slot, frame; } pc_classic_menu_background;
#define PC_CLASSIC_SITE_MENU_BACKGROUND 23
typedef struct { int action, y; } pc_classic_menu_command;
#define PC_CLASSIC_SITE_MENU_COMMAND 24
typedef struct {
    const void* row;
    int id;
    unsigned char item[0x40];
} pc_classic_menu_row;
#define PC_CLASSIC_SITE_MENU_ROW 25
/* The hand: MAINMENU's (58EC.c vs_mainMenu_renderCursor) or BATTLE's (5BF94.c vs_battle_drawCursor: the same packets,
 * colour 0; `battle` 1), the Yes / No and small menus of Setup (MENUC), Loot (MENUB), Data (MENU7), Battle Abilities
 * (MENU2), the rename grid (MENU8) and the battle command list use. */
typedef struct { int step, xy, color, battle; } pc_classic_menu_cursor;
#define PC_CLASSIC_SITE_MENU_CURSOR 26
typedef struct { int mode, step, lit; } pc_classic_menu_tabs;
#define PC_CLASSIC_SITE_MENU_TABS 27
/* A row's value in our Option pages (pc/src/ui/classic_options.c draw_values: not the game's, but drawn with its
 * printers): the page's row (its index), kind (PC_UI_SET_*), enabled, focused, the choice's index; the column's x
 * and the window's right end; the colours (current, other); the toggle's gap; the slider's gauge (colour, width,
 * fill); a link's target there; capitals; the window's x and the row's y; the texts (value, the toggle's two
 * names, a note's label: the page model's, read only). */
typedef struct {
    int row, kind, enabled, focused, index, x, right, lit, other, gap, bar_colour, bar_width, fill, link, upper,
        window_x, y;
    const char *value, *on, *off, *label;
} pc_classic_option_value;
#define PC_CLASSIC_SITE_OPTION_VALUE 28
/* The Items screen's own panels (MENU3): its category icons (items0.h _renderItemCategory's loop: the step as the
 * loop uses it, (animationStep * 8) - 16; the highlight; the selected category; on_row the icon the loop hands to row
 * 31 instead of drawing, -1 none) and the STATUS strip (16C.c: its slide, row, and its state: lit when 1). */
typedef struct { int step, highlight, selected, on_row, slot; } pc_classic_items_category; /* slot: the dim icons'
 * ordering-table slot, bytes from vs_scratch.unk4 (items0.h's 0; MENUD's copy, 234.c func_80103E24, -12) */
#define PC_CLASSIC_SITE_ITEMS_CATEGORY 29
typedef struct { int row, state; } pc_classic_items_status_button;
#define PC_CLASSIC_SITE_ITEMS_STATUS_BUTTON 30
/* MAINMENU's equipment panels (C48.c), the Items screen's status views and its Equip menus: the stats panel
 * (vs_mainMenu_renderEquipStats, after its slide is stepped: its packed place; whether the values are known
 * (vs_mainMenu_strIntAgi[3]); RANGE / RISK shown (_rangeRiskData[3]); the range's equipment delta (the actor's
 * currentRange - weapon range); the stat names' subtype (vs_mainMenu_equipmentSubtype); RANGE, RISK; STR / INT /
 * AGI, their thresholds (strIntAgi[4..6]), the base and total columns (D_80102488[0..2], [4..6]) and the accessory's
 * (D_80102498)) and the DP / PP bars (vs_mainMenu_renderDpPpBars after its step: the step, target (1 DP, 2 PP),
 * current and max DP (hundreds), PP). */
typedef struct {
    int position, known, range_risk_shown, range_delta, subtype, range_risk[2];
    int stat[3], threshold[3], base[3], total[3], accessory[3];
} pc_classic_equip_stats;
#define PC_CLASSIC_SITE_EQUIP_STATS 31
typedef struct { int step, target, dp, max_dp, pp, max_pp; } pc_classic_dp_pp_bars;
#define PC_CLASSIC_SITE_DP_PP_BARS 32
/* A grip's gem slots in its Items status view (items1.h _gripStatusView: the gem icon and "X     n"): the count. */
typedef struct { int count; } pc_classic_gem_slots;
#define PC_CLASSIC_SITE_GEM_SLOTS 33
/* The class / affinity / type table (C48.c _renderStatPage, as vs_mainMenu_drawClassAffinityType calls it): its slide
 * (x_offset, 0 or less), the page shown (vs_mainMenu_itemStatPage), the pages enabled, the equipment subtype and a
 * copy of vs_mainMenu_equipmentStats (the rows, the compared item's, the equipped's, [36] and [63] its flags). */
typedef struct {
    int x_offset, page, enabled, subtype;
    short stats[64];
} pc_classic_stat_page;
#define PC_CLASSIC_SITE_STAT_PAGE 34
/* The OK mark beside a menu's last choice, pulsing (MENUC 168.c _drawOk: Setup's Assemble, Gems, Combine; MENUD 234.c
 * func_801089BC: Storage's top menu): its place and its level ((rsin(step * 8) >> 5) + 64: the grey it is drawn in). */
typedef struct { int x, y, level; } pc_classic_ok_mark;
#define PC_CLASSIC_SITE_OK_MARK 35
/* Storage's button strip (MENUD 234.c vs_menuD_exec: two windows, Square / Triangle / Cross and STATUS / CANCEL / END):
 * its slide (x) and its state (D_80109A7D: STATUS lit by bits 0..1, END dimmed by bit 2). */
typedef struct { int x, state; } pc_classic_storage_buttons;
#define PC_CLASSIC_SITE_STORAGE_BUTTONS 36
/* Storage's counts (func_80106464's "count / capacity" per category, MENUD 234.c): layout 0 the strip's
 * (vs_menuD_exec: the bag's or the container's name, its categories and counts from x, the last row the transfer
 * list's), 1 the top menu's (func_80106504: GADGET BAG and CONTAINER, both columns); container (D_80109A7A); each
 * column's capacities and counts (layout 0: column 0 only, row 6 the transfer list's). */
typedef struct { int layout, x, container, capacity[2][7], count[2][7]; } pc_classic_storage_counts;
#define PC_CLASSIC_SITE_STORAGE_COUNTS 37
/* Loot's capacities strip (MENUB 260.c _displayInventoryCapacities): its slide (x), whether the controls are on
 * (_controlsEnabled), the row looked at (0 none, 1 the list's leave row, 2 an item), ALL lit (var_fp), and per category
 * the bag's capacity, its count and how many the item looked at adds (_itemIncludesItemsInCategory: an item's only). */
typedef struct { int x, controls, looking, all, capacity[7], count[7], incoming[7]; } pc_classic_loot_capacities;
#define PC_CLASSIC_SITE_LOOT_CAPACITIES 38
/* A misc item's discard amount (items1.h _discardMenu's picker: Items; MENUD 234.c's copy: Storage): its three
 * digits in the text boxes' font (style 5) right to left from x (6 apart), the count (0..999). */
typedef struct { int count, x, y; } pc_classic_discard_count;
#define PC_CLASSIC_SITE_DISCARD_COUNT 39
/* The rename grid (MENU8 21A0.c _renderRenameMenu: Setup's Rename, a new weapon's name after Assemble): its slide
 * (x_offset, 0 at rest), the four command bars' fades (Insert, Delete, Back, OK from the bottom: the counters as its
 * loop reads them, 8 the one just chosen), the L1 / R1 arrows' light and step (as it steps them; drawn at slide 0
 * only); the name typed (20 codes) and the game's words: the commands (Insert first) and the character table (9 rows
 * of 14 and its row end), each in the menu charset, the words ended by 0xE7. */
typedef struct {
    int x_offset, fade[4], lit, step;
    unsigned char name[20], commands[4][16], table[9][15];
} pc_classic_rename_grid;
#define PC_CLASSIC_SITE_RENAME_GRID 40
/* A highlight box on the rename grid (21A0.c _highlightSelection: the character under the hand, the name's pen): its
 * place and size. */
typedef struct { int x, y, w, h; } pc_classic_rename_box;
#define PC_CLASSIC_SITE_RENAME_BOX 41
/* MENU7's file menu (260.c _drawFileMenu: the save list after Storage, Data's SAVE / LOAD), drawn at once (DrawPrim and
 * LoadImage, no ordering table: PC_CLASSIC_IMMEDIATE below). Its frame in two parts: part 0 the bottom band's two
 * sprites (before the elements), part 1 after them: the band's three shaded rows (x the frame buffer's, 0 or 320), the
 * caution sprite, the message's tile, the hand (cursor_xy packed, 0 none; cursor_color its saturation step before the
 * draw steps it), the message (has_message; its codes as the game keeps them, to 0xE7; arrow: _printString's blink step
 * before the draw) and the screen's fade. */
typedef struct {
    int part, x, cursor_xy, cursor_color, fade, arrow, has_message;
    unsigned char message[256];
} pc_classic_file_menu;
#define PC_CLASSIC_SITE_FILE_MENU 42
/* One element of it (_drawFileMenuElement: a card's or a file's panel), after the menu stepped its slide: id (0..9),
 * place and size, selected, unavailable, the blend factors as the draw uses them (outer after its step, inner before),
 * slot (its file 0..4; 255 none), its location (saveLocation), saving (_isSaving), arrow (_printString's step before);
 * the file's (slot under 5): slot_state, map %, saves, clears, the play time, HP / MP; the progress animation the draw
 * shows (0 none, 1 processing at progress_x, 2 complete in progress_colour); the codes of its words (text: the panel's,
 * with its 0xFA spacing pairs, to 0xFF; place: the location's name; in_use, empty, fresh: the slot's words), and the
 * two location CLUTs the draw uploads (_mcData's: the pictures' palettes, data for d.image_load). */
typedef struct {
    int id, x, y, w, h, selected, unavailable, outer, inner, slot, location, saving, arrow;
    int slot_state, map, saves, clears, hours, minutes, seconds, hp, hp_max, mp, mp_max;
    int progress, progress_x, progress_colour;
    unsigned char text[32], place[48], in_use[32], empty[32], fresh[32];
    unsigned short cluts[2][256];
} pc_classic_file_element;
#define PC_CLASSIC_SITE_FILE_ELEMENT 43
/* MENUF's screens: pc_classic_menuf (classic_parity.h, with its calls' ops). */
#define PC_CLASSIC_SITE_MENUF 44
/* MENUF's VRAM uploads, at once (PC_CLASSIC_IMMEDIATE below): the TIMs and CLUTs _initMenu loads (its pictures,
 * LoadImage) and the CLUT row the end screens pulse each frame (_pulseTimeAttackEnd, _pulseCubePuzzleEnd). The record:
 * the submenu and which call (0 _initMenu, 1 _execMenu); the uploads themselves are the recorder's (the rectangles
 * and pixels, kept for the claim: classic_parity.c). */
typedef struct { int screen, phase; } pc_classic_menuf_load;
#define PC_CLASSIC_SITE_MENUF_LOAD 45
#define PC_CLASSIC_SITE_STATUS_PANEL 46 /* classic_parity.h PC_CLASSIC_STATUS_PANEL: func_800CA2DC */
/* PC_CLASSIC_IMMEDIATE wraps a call that draws at once (DrawPrim, LoadImage; StoreImage read): between begin and site
 * the recorder (classic_parity.c pc_classic_immediate_*) keeps every draw and upload the statement makes, in order,
 * as the kind's capture; when a classic claim on the kind is honoured it holds them back, and site (hud_bridge.c
 * pc_hud_immediate) has the claim draw instead, at once, in the same place in the frame (or does the held ones
 * after all: no claim, or it failed). The statement always runs: whatever state it steps is stepped as before.
 * The record is filled by the site's caller (before and after the statement, as the draw uses it). */
void pc_classic_immediate_begin(int kind) __attribute__((weak));
void pc_classic_immediate_site(int kind, void* record) __attribute__((weak));
/* Whether the open range holds the game's draws back (a claim draws instead): the game's own glyph reports to ui_text
 * are then left out (the claim's packets report theirs, d.text_glyph). classic_parity.c. */
int pc_classic_immediate_held(void) __attribute__((weak));
/* MAINMENU's shared frame (58EC.c), read for the menu model (pc/src/classic/menu_model.c, rood.game.menu()): the
 * statics its list and command title keep (the list's cursor row and page, the sub-menu's id, row info and row
 * count, whether its rows take input (_itemsText), the rows' y offset; the command title's current and next). */
typedef struct {
    int row, page, sub_menu, row_info, row_count, list_open, row_offset, command, command_next;
} pc_classic_mainmenu;
void pc_classic_mainmenu_read(pc_classic_mainmenu* out); /* 58EC.c */
void pc_classic_site(int kind, void* record, int x, int y, void* first, void* end) __attribute__((weak));
#define PC_CLASSIC(kind, record, x, y, statement) \
    do { \
        void* pc_classic_first = psx_load(void*, (u_int*)0x1F800000); \
        statement; \
        if (pc_classic_site) \
            pc_classic_site(kind, record, x, y, pc_classic_first, psx_load(void*, (u_int*)0x1F800000)); \
    } while (0)
#endif
#endif
