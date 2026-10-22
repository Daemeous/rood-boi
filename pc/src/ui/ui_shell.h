/* Modern menus (pc/docs/Modern_Menus.md): the shell between the game's menu
 * (menu_bridge.c, battle builds), the drawing (ui_draw.cpp) and the RPC. */
#ifndef PC_UI_SHELL_H
#define PC_UI_SHELL_H
#include "ui_model.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Settings key ui_menus: 0 the game's own menus (default), 1 modern. */
extern int pc_ui_menus;

/* Buttons in the shell's own order (bridge maps libetc PAD bits to these). */
enum {
    PC_UI_UP = 1 << 0, PC_UI_DOWN = 1 << 1, PC_UI_LEFT = 1 << 2, PC_UI_RIGHT = 1 << 3,
    PC_UI_CONFIRM = 1 << 4, PC_UI_BACK = 1 << 5, PC_UI_EXIT = 1 << 6, PC_UI_ALT = 1 << 7,
    PC_UI_TAB_PREV = 1 << 8, PC_UI_TAB_NEXT = 1 << 9, PC_UI_SUB_PREV = 1 << 10, PC_UI_SUB_NEXT = 1 << 11,
};
typedef struct {
    unsigned pressed; /* edges this frame */
    unsigned repeat;  /* pressed plus key repeat (lists) */
} pc_ui_input;

/* The game's original commands (top-level rows of func_800C930C). */
enum { PC_UI_CMD_COUNT = 10 };
typedef struct {
    unsigned enabled;      /* bit i: command i exists (menuFlags) */
    unsigned unselectable; /* bit i: shown but not allowed now (it still opens: the
                            * game's own screen shows what is off, as the original does) */
    unsigned mask;         /* the game's disable mask D_800F4EA0 (func_800CABE0), for the reason */
} pc_ui_commands;

/* The game opened its top-level menu (cursor memory: remember=1). */
void pc_ui_shell_open(const pc_ui_commands* commands, int remember);
/* One game frame of the top-level menu. Returns 0 to stay, -1 to close the
 * menu, n > 0 to open original command n - 1 (the game's own protocol). */
int pc_ui_shell_tick(const pc_ui_input* input);
/* Hide the shell without a result (the game's menu session is not the top level). */
void pc_ui_shell_close(void);
/* The shell is up (opened and not closed). */
int pc_ui_shell_visible(void);
/* The right-stick camera's menu context for the tab shown (camera_stick.h
 * names), NULL while the shell is not up. */
const char* pc_ui_shell_camera_context(void);
/* The focused screen takes Circle, Triangle and Square as the buttons to put
 * an ability on (Abilities' learned list, as the original's own screen):
 * button_roles.c leaves Circle and Cross as they are there. */
int pc_ui_shell_face_buttons(void);
/* The open shell as JSON (RPC ui_menu). */
void pc_ui_shell_json(char* out, unsigned size);
/* Equipment screen data (the bridge sets it each tick before pc_ui_shell_tick;
 * the shell copies it). */
void pc_ui_shell_set_equipment(const pc_ui_equipment* e);
/* The next queued request (the bridge executes them after the tick); 0 when none. */
int pc_ui_shell_pop_request(pc_ui_request* out);
/* The slot whose candidates the screen wants (PC_UI_SLOT_*), -1 when none:
 * the open pick list's, or every weapon on the Quick swap overview (its card
 * shows the slot's weapon). The bridge fills candidates for it every tick. */
int pc_ui_shell_wanted_candidates(void);
/* Editor: the Menus row of Graphics > 2D / UI (ImGui window open). */
void pc_ui_menus_editor_row(void);
/* Drawing, from pc_overlay_frame (ImGui frame open). */
void pc_ui_draw_frame(float dt);
/* The last drawn frame had nothing in motion: every glide (the focus, a
 * list's scroll, a popover following its row, a card's width, the tab ink;
 * ui_draw.cpp approach) at its target. They run on real time, not vsyncs:
 * tests wait for this (RPC ui_menu "at_rest") before pointing at a layout. */
int pc_ui_draw_at_rest(void);
/* The shell is drawn and the pointer is over it (the game picture, which its
 * backdrop covers): the editor's viewport mouse (fly, pick, orbit) leaves
 * the pointer to the shell, as io.WantCaptureMouse does for ImGui windows. */
int pc_ui_shell_wants_mouse(void);
/* Tests (RPC ui_menu {pointer}): the next drawn frame takes this pointer, in
 * layout units (1280 or 960 x 720), as moved there, instead of ImGui's; click
 * / right: a button went down; wheel in notches (positive = up); down: the
 * left button is held (a drag over the Ashley model turns it: pointers sent
 * with down, the first inside the model, then moved); middle: the middle
 * button is held (a drag over the Map frame's picture pans it); shift: Shift
 * is held (a left drag over the Map frame's picture started with it pans). */
void pc_ui_draw_pointer(float x, float y, int click, int right, float wheel, int down, int middle, int shift);
/* The Ashley screen's model column in layout units (x, y, w, h), for the
 * current aspect (pc_gte_widescreen): where ashley_bridge.c centres the model. */
void pc_ui_draw_model_column(float out[4]);
/* The theme's ground colour (ui_theme.cpp), 0..255 per channel. */
void pc_ui_theme_ground_rgb(int* r, int* g, int* b);


/* The shell reopens on `tab` (the Map frame's tab click, menu_bridge.c),
 * on that tab's remembered row; a tab with nothing to show is ignored. */
void pc_ui_shell_set_tab(int tab);
/* The tab L1 (dir -1) / R1 (+1) goes to from the current one. */
int pc_ui_shell_step_tab(int dir);

/* Read-only views for drawing (ui_draw.cpp). */
int pc_ui_shell_tab(void);
int pc_ui_shell_tab_count(void);
const char* pc_ui_shell_tab_title(int tab);
int pc_ui_shell_row(int i, const char** label, const char** help, const char** value, int* enabled,
    const char** reason);
int pc_ui_shell_focus(void);
/* What the current tab shows: PC_UI_SCREEN_COMMANDS (rows above), or a native
 * screen: PC_UI_SCREEN_EQUIPMENT, PC_UI_SCREEN_ASHLEY, PC_UI_SCREEN_SYSTEM,
 * PC_UI_SCREEN_ABILITIES, PC_UI_SCREEN_MAGICK, PC_UI_SCREEN_ITEMS. */
enum { PC_UI_SCREEN_COMMANDS = 0, PC_UI_SCREEN_EQUIPMENT = 1, PC_UI_SCREEN_ASHLEY = 2, PC_UI_SCREEN_SYSTEM = 3,
       PC_UI_SCREEN_ABILITIES = 4, PC_UI_SCREEN_MAGICK = 5, PC_UI_SCREEN_ITEMS = 6 };
int pc_ui_shell_screen(void);

/* The Ashley screen: a summary with rows weapon, shield, condition, then one
 * per status effect (PC_UI_ASH_EFFECT + i). Circle on weapon / shield jumps to
 * Equipment > Gear on that slot; Square anywhere opens the original Status
 * (result 5). */
enum { PC_UI_ASH_WEAPON, PC_UI_ASH_SHIELD, PC_UI_ASH_CONDITION, PC_UI_ASH_EFFECT };
/* The bridge sets the snapshot each tick before pc_ui_shell_tick (copied). */
void pc_ui_shell_set_ashley(const pc_ui_ashley* a);
const pc_ui_ashley* pc_ui_shell_ashley(void);
/* Focused row (PC_UI_ASH_*) and the number of rows. */
int pc_ui_shell_ashley_row(void);
int pc_ui_shell_ashley_rows(void);
/* On the Condition row: the body part focused (a limb index: hovered with the
 * pointer, PC_UI_MOUSE_LIMB, or stepped to with Left / Right), -1 the panel as
 * a whole. */
int pc_ui_shell_ashley_limb(void);
/* The card's text for the Condition row: the panel as a whole (limb -1:
 * "Condition · <the worst part's state>", a summary of the parts) or one part
 * ("<part> · <state>", its HP). The game's terms (vs_battle_getLimbStatus):
 * Critical, Damaged, Wounded, Good, Excellent. */
const char* pc_ui_limb_state_name(int state);
void pc_ui_ashley_condition_text(const pc_ui_ashley* a, int limb, char* title, int title_size, char* line,
    int line_size);
/* The drawing reports how many effect chips fit (the rest are a "+N"): only
 * those are rows. -1 (the default, also after open): all of them. */
void pc_ui_shell_ashley_effects_shown(int n);
/* Ashley's model should show: the Ashley tab is current and the shell visible. */
int pc_ui_shell_wants_model(void);
/* Model view input: a pointer drag over the model, in layout units (ui_draw);
 * kept only while the model is wanted. */
void pc_ui_shell_model_drag(float dx, float dy);
/* Consumes the drag accumulated since the last call (layout units: x turns
 * yaw, y pitch; the bridge scales it and adds the stick itself). */
void pc_ui_shell_model_turn(float* yaw_delta, float* pitch_delta);

/* Held hand-off (menu_bridge.c): the shell returned -1 or n > 0 while a model
 * reload runs and the bridge delivers that result once the load is over. The
 * shell has closed (pc_ui_shell_visible is 0, it is not ticked) but keeps
 * drawing, with a loading indicator, until the bridge clears it. Open and
 * close clear it too. */
void pc_ui_shell_set_holding(int holding);
int pc_ui_shell_holding(void);
/* What ui_draw draws: visible or held. */
int pc_ui_shell_drawn(void);

/* Mouse (ui_draw hit tests what is under the pointer; the shell applies it at
 * its next tick). kind/index: what the pointer is on (PC_UI_MOUSE_NONE: nothing
 * or not moved, so keyboard/pad focus stays; PC_UI_MOUSE_ROW on the Ashley
 * screen: its row PC_UI_ASH_*); clicked: a left click on it;
 * wheel: notches (positive = up), one list step per tick. Hover focuses; a
 * click focuses and confirms (TAB / SUB switch, BUTTON presses index's
 * PC_UI_* bits, as a hint does). Calls between two ticks: the last hover and
 * the last click count, wheels add up. */
enum {
    PC_UI_MOUSE_NONE, PC_UI_MOUSE_TAB, PC_UI_MOUSE_SUB, PC_UI_MOUSE_ROW, PC_UI_MOUSE_SLOT, PC_UI_MOUSE_LIST,
    PC_UI_MOUSE_QUICK, PC_UI_MOUSE_BUTTON,
    PC_UI_MOUSE_PAGE,   /* System: an entry of the list (pc_ui_shell_system_item) */
    PC_UI_MOUSE_SETTING, /* System: a row of the page on the card */
    PC_UI_MOUSE_ABIL_SLOT, /* Abilities, Chain / Defense: a button's slot 0..2 */
    PC_UI_MOUSE_ABIL_ROW,  /* Abilities, Chain / Defense: a row of the learned list */
    PC_UI_MOUSE_ART_CATEGORY, /* Abilities, Break Arts: a category (1..10, not the rail row) */
    PC_UI_MOUSE_ART,       /* Abilities, Break Arts: an art 0..3 of the focused category */
    PC_UI_MOUSE_SPELL,     /* Magick: a row of the sub-tab's list (a spell, or a Teleport destination) */
    PC_UI_MOUSE_SPELL_LEVEL, /* Magick: a level 0..3 of the focused levelled spell (a click picks it, no cast) */
    PC_UI_MOUSE_ITEM,      /* Items: a row of the category's list (hover focuses it where the list has the
                            * focus: list, move, details; a click elsewhere goes back to the list on it) */
    PC_UI_MOUSE_ITEM_MENU, /* Items: a row of the open menu (actions, Yes / No, sort stats) */
    PC_UI_MOUSE_LIMB,      /* Ashley: a body part of the Condition panel (pc_ui_shell_ashley_limb) */
    PC_UI_MOUSE_CONFIRM    /* "Keep these settings?": 0 Keep, 1 Revert, 2 the rest of the screen (nothing) */
};
void pc_ui_shell_mouse(int kind, int index, int clicked, float wheel);

/* The Equipment screen for drawing. */
enum { PC_UI_EQ_GEAR, PC_UI_EQ_PICK, PC_UI_EQ_QUICK, PC_UI_EQ_QUICK_WEAPON, PC_UI_EQ_QUICK_SHIELD };
typedef struct {
    int sub;       /* 0 Gear, 1 Quick swap */
    int mode;      /* PC_UI_EQ_* */
    int slot;      /* focused gear slot (PC_UI_SLOT_*) */
    int quick;     /* focused quick slot 0..7 */
    int cursor;    /* pick list row, -1 none */
    int rows;      /* pick list rows (0 while loading) */
    int loading;   /* a pick list is open and its candidates have not arrived */
    int locked;    /* browse only */
    const char* reason;          /* why locked, or null */
    const pc_ui_item* pending;   /* quick shield step: the weapon picked, else null */
} pc_ui_equip_view;
void pc_ui_shell_equip_view(pc_ui_equip_view* v);
/* The snapshot the bridge set last. */
const pc_ui_equipment* pc_ui_shell_equipment(void);
/* Row r of the open pick list: the candidate, or null ("No shield" first in the
 * quick shield step, or past the end). */
const pc_ui_item* pc_ui_shell_list_item(int r);

/* The Abilities screen. Sub-tabs (L2/R2; a click on PC_UI_MOUSE_SUB index i
 * = the i-th shown): Chain and Defense once the game has unlocked Battle
 * Abilities (menuFlags bit 2), Break Arts once Break Arts is (bit 1).
 *
 * Chain / Defense: the three buttons' slots (Circle, Triangle, Square) above
 * the learned list. Slots: Left/Right move, Circle opens the list as a pick
 * for that button (PICK: Circle puts the ability there, back to the slots;
 * the one already there takes it off, as the original toggles), Down goes
 * into the list, Square opens the original Battle Abilities screen (result
 * 3). List (LIST, the original's own style): Circle / Triangle / Square put
 * the focused ability on that button (Triangle does not close the menu
 * here), Up on the first row goes back to the slots. Every assignment is a
 * PC_UI_REQ_ABIL_MAP {slot = button, item = action id, shield =
 * PC_UI_ABIL_CHAIN / _DEFENSE}; the bridge does what MENU2's _mapAbility does.
 *
 * Break Arts: the categories rail (the equipped category first) and the
 * focused category's four arts. Circle / Right enters the arts, Left / Cross
 * goes back; Circle on a learned art queues PC_UI_REQ_ART_CAST {item = id}:
 * the bridge checks it as MENU1 does and, accepted, hands it to the game and
 * ends the menu as MENU1 does (menu_bridge.c close_for_cast, after
 * pc_ui_shell_close), else buzzes. Square opens the original Break Arts (result 2). Cross steps back
 * out of a list; at a sub-tab's base it closes as everywhere. */
enum { PC_UI_AB_CHAIN, PC_UI_AB_DEFENSE, PC_UI_AB_ARTS };
enum { PC_UI_ABM_SLOTS, PC_UI_ABM_LIST, PC_UI_ABM_PICK, PC_UI_ABM_CATEGORIES, PC_UI_ABM_ARTS };
typedef struct {
    int subs;          /* sub-tabs shown */
    int sub_kind[3];   /* PC_UI_AB_* of each */
    int sub;           /* the current one's index */
    int kind;          /* PC_UI_AB_* */
    int mode;          /* PC_UI_ABM_* */
    int slot;          /* Chain / Defense: the focused button 0..2 */
    int cursor;        /* Chain / Defense: the list row */
    int category;      /* Break Arts: the focused category 1..10 */
    int category_row;  /* its row in the rail */
    int art;           /* the focused art 0..3 */
    int order[PC_UI_ART_CATEGORIES]; /* the rail: categories, the equipped one first */
} pc_ui_abilities_view;
void pc_ui_shell_abilities_view(pc_ui_abilities_view* v);
/* The bridge sets the snapshot each tick before pc_ui_shell_tick (copied). */
void pc_ui_shell_set_abilities(const pc_ui_abilities* a);
const pc_ui_abilities* pc_ui_shell_abilities(void);

/* The Magick screen. Sub-tabs (L2/R2; a click on PC_UI_MOUSE_SUB index i =
 * the i-th shown): the schools with a spell learned (Warlock, Shaman,
 * Sorcerer, Enchanter, the snapshot's unlocked) and Teleport once the game
 * has it (the snapshot's teleport); the tab itself shows while the game has
 * the Magick command (menuFlags bit 0), with no sub-tab when nothing is
 * learned yet. One list per sub-tab: Up/Down move (wrapping); on a levelled
 * spell Left/Right pick its level (LV1..LVn, no wrap; one level kept for all
 * of them, as the original's LV cursor: taken from the snapshot's
 * level_memory at every open, lowered to a focused spell's last level and
 * kept so; the bridge writes it back to the game). Circle queues
 * PC_UI_REQ_SPELL_CAST {item = the level's action id} or
 * PC_UI_REQ_TELEPORT {slot = the save point}: the bridge checks it as MENU0
 * does and, accepted, hands it to the
 * game and ends the menu (menu_bridge.c close_for_cast), else buzzes. Square
 * opens the original Magick screen (result 1). Cross closes. Teleport's
 * cursor starts on the circle the player stands on at every open (as the
 * original's list does); the spell lists keep theirs (a cancelled target
 * reopens the shell on the spell). */
enum { PC_UI_MG_TELEPORT = PC_UI_SCHOOLS }; /* sub-tab kinds: PC_UI_SCHOOL_* or this */
typedef struct {
    int subs;                            /* sub-tabs shown */
    int sub_kind[PC_UI_SCHOOLS + 1];     /* PC_UI_SCHOOL_* or PC_UI_MG_TELEPORT, each */
    int sub;                             /* the current one's index */
    int kind;                            /* its kind (-1: none shown) */
    int cursor;                          /* the list's row */
    int rows;                            /* the list's rows */
    int level;                           /* the focused spell's level 0..levels-1 (0: not levelled) */
} pc_ui_magick_view;
void pc_ui_shell_magick_view(pc_ui_magick_view* v);
/* The bridge sets the snapshot each tick before pc_ui_shell_tick (copied). */
void pc_ui_shell_set_magick(const pc_ui_magick* m);
const pc_ui_magick* pc_ui_shell_magick(void);

/* The Items screen (gated on the Items command, menuFlags bit 3; the
 * original Items menu is MENU3). Categories (L2/R2, wrapping; a click on
 * PC_UI_MOUSE_SUB index = the category): weapons, blades, grips, shields,
 * armor, gems, misc (PC_UI_IC_*), each with its capacity. LIST: Up/Down move
 * (wrapping); Circle opens the row's action menu; Square shows the row's
 * details (DETAILS: Up/Down still move, Circle opens the actions, Square or
 * Cross go back); an empty category's Circle offers the original screen only.
 * ACTIONS, as the original offers them per category (greyed with the
 * original's reason; with the menu's disable mask everything but the
 * original screen is greyed): Use (misc: PC_UI_REQ_ITEM_USE {misc, row}; the
 * bridge hands it to the game and ends the menu, a cancelled target reopens
 * the shell on the item), Search (blades, grips, gems in a set: the parent
 * weapon / shield focused in its category; the shell's own), Discard (count
 * 2 or more: QUANTITY first: Up/Down change the count by the step, Left/Right
 * pick the step 1 / 10 / 100, Left past 100 jumps to all, Right past 1 to
 * none, Circle goes on (none: back to the list, PC_UI_REQ_ITEM_CANCEL), then
 * CONFIRM Yes / No, opening on No as the original's: Yes
 * PC_UI_REQ_ITEM_DISCARD {cat, row, count}, No PC_UI_REQ_ITEM_CANCEL), Discard
 * all (misc: CONFIRM, then PC_UI_REQ_ITEM_DISCARD_ALL), Sort (SORT: the
 * category's stats, Circle PC_UI_REQ_ITEM_SORT {cat, row, stat}), Move (MOVE:
 * Up/Down pick the target row, Circle PC_UI_REQ_ITEM_MOVE {cat, from, to}:
 * swap, or a misc merge), and Original screen (result 4: the original Items
 * menu, also the way to its Options > Setup). A refused Use (greyed, or the
 * bridge refuses it) keeps the action menu with its reason. Cross steps back
 * one level (out of a menu: to ACTIONS, the picker or the list; the
 * original's picker and Yes / No go straight back to the list); in the list
 * it closes, as Triangle does everywhere (in the picker too, as the
 * original's Triangle). Every request but Move carries the row's inventory
 * index as seen, Move the moved row's (ui_model.h; the bridge refuses a stale
 * one), and the bridge plays the original's sounds (refused: the invalid
 * sound) and leaves the cursor memories the original leaves. */
enum { PC_UI_IT_LIST, PC_UI_IT_ACTIONS, PC_UI_IT_QUANTITY, PC_UI_IT_CONFIRM, PC_UI_IT_SORT, PC_UI_IT_MOVE,
       PC_UI_IT_DETAILS };
enum { PC_UI_ITA_USE, PC_UI_ITA_SEARCH, PC_UI_ITA_DISCARD, PC_UI_ITA_DISCARD_ALL, PC_UI_ITA_SORT, PC_UI_ITA_MOVE,
       PC_UI_ITA_ORIGINAL, PC_UI_ITA_COUNT };
typedef struct {
    int category;                 /* PC_UI_IC_* */
    int mode;                     /* PC_UI_IT_* */
    int cursor;                   /* the list's row (MOVE: the target row); 0 when empty */
    int rows;                     /* the category's rows */
    int capacity;                 /* the category's capacity */
    const pc_ui_item_row* row;    /* the focused row, or null (empty) */
    int locked;                   /* the menu's disable mask greys the list (browse only) */
    const char* reason;           /* why locked, or null */
    int actions;                  /* ACTIONS (and the modes after it): the menu's entries */
    int action_kind[PC_UI_ITA_COUNT];       /* PC_UI_ITA_* */
    int action_enabled[PC_UI_ITA_COUNT];
    const char* action_label[PC_UI_ITA_COUNT];
    const char* action_reason[PC_UI_ITA_COUNT]; /* greyed: why, else null */
    int action;                   /* the focused entry */
    int confirm_kind;             /* CONFIRM: PC_UI_ITA_DISCARD or PC_UI_ITA_DISCARD_ALL */
    int confirm;                  /* CONFIRM: 0 Yes, 1 No */
    const char* prompt;           /* CONFIRM: the original's question */
    int count, count_max, step;   /* QUANTITY (and CONFIRM after it): the count, the stack, step 0..2 (1/10/100) */
    int sort;                     /* SORT: the focused stat's row in the category's list */
    int move_from;                /* MOVE: the row being moved, -1 otherwise */
} pc_ui_items_view;
void pc_ui_shell_items_view(pc_ui_items_view* v);
/* The bridge sets the snapshot each tick before pc_ui_shell_tick (copied). */
void pc_ui_shell_set_items(const pc_ui_items* m);
/* Only the details card (the rest of the snapshot is unchanged since the
 * last pc_ui_shell_set_items: the bridge's refresh after the tick). */
void pc_ui_shell_set_items_details(const pc_ui_item_details* d);
const pc_ui_items* pc_ui_shell_items(void);
/* The row the details card is for (the bridge fills pc_ui_items.details for
 * it): the focused row's category and inventory index while the Items screen
 * is up and the list has rows. Returns 0 otherwise. */
int pc_ui_shell_items_focus(int* category, int* inventory);

/* The System screen (pc/src/ui/ui_settings.h has the pages): a list on the
 * left, the game's own screens first (Data, Options, Score, Quick manual;
 * Circle opens them, as the command rows do), then the settings pages. Circle
 * (or Right) on a page enters it: Up/Down move between its rows, Left/Right
 * change the focused row (key repeat), Circle runs an action, starts a
 * binding capture or steps a choice, Cross goes back to the list. While a
 * capture listens (pc_ui_settings_capture_button() >= 0) the shell ignores
 * its input (a right click or a click on the prompt cancels it). Leaving a
 * page, the tab or the menu (any way: close, an original screen, abandon)
 * cancels a capture and saves what changed (pc_ui_settings_flush). */
typedef struct {
    int items;    /* list entries */
    int games;    /* the first `games` entries are the game's screens; the rest pages 0.. */
    int item;     /* focused entry */
    int page;     /* the settings page the card shows (entered, or the focused entry's); -1: a game entry */
    int entered;  /* the page's rows have the focus (else the list) */
    int row;      /* the page's focused row (kept while in the list) */
    int rows;     /* the page's rows (pc_ui_settings_rows) */
    int capture;  /* the PS1 button a capture listens for, -1 none */
} pc_ui_system_view;
void pc_ui_shell_system_view(pc_ui_system_view* v);
/* "Keep these settings?" (display_confirm.h) while a risky display change waits:
 * the shell's modal takes the input (Left/Right/Up/Down move between Keep and
 * Revert, Circle answers, Cross and Triangle revert); its focus, 0 Keep, 1 Revert. */
int pc_ui_shell_confirm_focus(void);
/* The System tab shows the Quick manual (ui_manual.h): ui_system_draw.cpp hands it the screen. */
int pc_ui_shell_manual_shown(void);
/* The System tab shows the Score screen (ui_score.h): ui_draw.cpp gives it the whole view. */
int pc_ui_shell_score_shown(void);
/* List entry i: id ("data", "graphics"), label, help, enabled, reason (greyed: why).
 * Returns 0 past the end. */
int pc_ui_shell_system_item(int i, const char** id, const char** label, const char** help, int* enabled,
    const char** reason);
/* Pointer on a value (applied at the next tick, the last call wins): row
 * `row` of the page the card shows changes by `delta` steps (arrows, wheel),
 * or, with set, to `value` (a slider drag: its number; a toggle or choice
 * clicked: its index). set 2: a drag still in progress
 * (pc_ui_settings_preview); the drag's end sends its last value with set 1.
 * Enters the page when the list had the focus. */
void pc_ui_shell_system_value(int row, int delta, double value, int set);

/* The Loot screen (ui_loot.cpp): a direct session, not a tab. The game opens
 * MENUB itself (after a battle with drops, at a chest: vs_mainMenu_exec's
 * direct entry 12); menu_bridge.c's pc_ui_direct_tick hands it to
 * loot_bridge.c, which opens this screen, sets its snapshot every tick, ticks
 * it and carries out its requests. Drawn alone (title "Loot" or "Chest", no
 * tab bar).
 *
 * LIST: Up/Down move (wrapping); Circle takes the focused row
 * (PC_UI_REQ_LOOT_TAKE) when the bag has room for it, else opens ORGANIZE
 * (the select sound): the row's own action first when it has one
 * (Disassemble: weapons and shields; Consolidate: misc but keys), then
 * Organize inventory; Circle on Disassemble / Consolidate asks (CONFIRM, on
 * Yes as the original's) and sends PC_UI_REQ_LOOT_DISASSEMBLE / _CONSOLIDATE
 * on Yes; Organize inventory opens BAG (PC_UI_REQ_LOOT_BAG 1, item the
 * category): the bag by category (L2/R2, L1/R1 or Left/Right: each change
 * PC_UI_REQ_LOOT_BAG 1 with the new category; Up/Down). Circle on a row
 * opens BAG_ACTIONS, the actions MENUB's Items menu offers (items2.h
 * populateActions, its Use never selectable left out): Discard, Discard all
 * (misc), Sort, Move. Discard asks (BAG_CONFIRM, on No as the Items
 * screen's; PC_UI_REQ_ITEM_DISCARD), a misc stack of two or more first
 * through the quantity picker (BAG_QUANTITY: Up/Down by the step, Left/Right
 * the step; none: PC_UI_REQ_ITEM_CANCEL); Discard all asks for the stack
 * (PC_UI_REQ_ITEM_DISCARD_ALL); No: PC_UI_REQ_ITEM_CANCEL. Sort lists the
 * category's stats (BAG_SORT: PC_UI_REQ_ITEM_SORT). Move (BAG_MOVE): Up/Down
 * to the target, Circle (PC_UI_REQ_ITEM_MOVE; misc stacks of the same item
 * merge as the Items screen's do, the sum kept: MENUB's own merge loses the
 * moved stack, see loot_bridge.c). Cross backs
 * out one level; in BAG it closes the panel (PC_UI_REQ_LOOT_BAG 0). Square
 * takes everything the bag has room for (PC_UI_REQ_LOOT_TAKE_ALL). Cross
 * (PC_UI_REQ_LOOT_LEAVE 0; the bridge plays the leave sound and ends the
 * session at once when nothing is left, or at a chest nothing was taken
 * from) asks to leave the rest (CONFIRM on No: Yes PC_UI_REQ_LOOT_LEAVE 1),
 * or, with a key or sigil in the list, says it cannot be left (MESSAGE, any
 * button). A list that came empty shows the game's line (MESSAGE): any
 * button leaves. Cross backs out of every menu (the leave sound:
 * PC_UI_REQ_LOOT_SOUND 0). The bridge ends the session (the shell closes);
 * the shell never returns a result. */
enum { PC_UI_LOOT_LIST, PC_UI_LOOT_ORGANIZE, PC_UI_LOOT_CONFIRM, PC_UI_LOOT_MESSAGE, PC_UI_LOOT_BAG,
       PC_UI_LOOT_BAG_CONFIRM, PC_UI_LOOT_BAG_ACTIONS, PC_UI_LOOT_BAG_QUANTITY, PC_UI_LOOT_BAG_SORT,
       PC_UI_LOOT_BAG_MOVE };
/* The Organize panel's modes (BAG and the modes after it). */
#define PC_UI_LOOT_IN_BAG(mode) ((mode) == PC_UI_LOOT_BAG || (mode) >= PC_UI_LOOT_BAG_CONFIRM)
enum { PC_UI_LOOT_BAG_ACTION_MAX = 4 };
enum { PC_UI_LOOT_ASK_DISASSEMBLE, PC_UI_LOOT_ASK_CONSOLIDATE, PC_UI_LOOT_ASK_LEAVE, PC_UI_LOOT_ASK_KEYS,
       PC_UI_LOOT_ASK_EMPTY };
typedef struct {
    int mode;          /* PC_UI_LOOT_* */
    int cursor;        /* the list's row (0 when empty) */
    int rows;          /* the list's rows */
    const pc_ui_loot_row* row; /* the focused row, or null */
    int menu;          /* ORGANIZE: the focused entry; CONFIRM / BAG_CONFIRM: 0 Yes, 1 No */
    int menu_rows;     /* ORGANIZE: its entries (1 or 2) */
    int menu_kind[2];  /* ORGANIZE: each entry's PC_UI_LOOT_ORG_* (NONE: Organize inventory) */
    int ask;           /* CONFIRM / MESSAGE: PC_UI_LOOT_ASK_* */
    const char* text;  /* CONFIRM / MESSAGE / BAG_CONFIRM: the game's line */
    int bag_category;  /* BAG: PC_UI_IC_* */
    int bag_cursor;    /* BAG: the category's row */
    int bag_rows;      /* BAG: the category's rows */
    /* BAG_ACTIONS: the actions (PC_UI_ITA_DISCARD, _DISCARD_ALL, _SORT, _MOVE), whether each can be chosen
     * and why not (the bag row's reason, or null); menu is the focused one */
    int bag_actions;
    int bag_action_kind[PC_UI_LOOT_BAG_ACTION_MAX];
    int bag_action_enabled[PC_UI_LOOT_BAG_ACTION_MAX];
    const char* bag_action_reason[PC_UI_LOOT_BAG_ACTION_MAX];
    int confirm_kind;  /* BAG_CONFIRM: PC_UI_ITA_DISCARD or PC_UI_ITA_DISCARD_ALL */
    int count, count_max, step; /* BAG_QUANTITY / BAG_CONFIRM: the quantity, the stack, the step (0 1, 1 10, 2 100) */
    int sort;          /* BAG_SORT: the focused stat's row (pc_ui_items sort_stats) */
    int move_from;     /* BAG_MOVE: the row being moved (bag_cursor is the target), else -1 */
} pc_ui_loot_view;
void pc_ui_loot_open(void);
void pc_ui_loot_close(void);
int pc_ui_loot_visible(void);
/* One game frame of the screen (requests queued). */
void pc_ui_loot_tick(const pc_ui_input* input);
/* The bridge's snapshot (copied), and the bag for BAG (copied; the Items
 * screen's snapshot: pc_ui_items). */
void pc_ui_loot_set(const pc_ui_loot* m);
const pc_ui_loot* pc_ui_loot_model(void);
void pc_ui_loot_set_bag(const pc_ui_items* bag);
const pc_ui_items* pc_ui_loot_bag(void);
void pc_ui_loot_view_get(pc_ui_loot_view* v);
/* The focused row (for the bridge's details card); -1 when none. */
int pc_ui_loot_focus(void);
int pc_ui_loot_pop_request(pc_ui_request* out);
/* Mouse, as pc_ui_shell_mouse: PC_UI_MOUSE_LOOT_ROW (a list row), _LOOT_MENU
 * (an entry of the open menu or Yes / No), _LOOT_BAG (a bag row), _SUB (a bag
 * category), _BUTTON (bits). */
void pc_ui_loot_mouse(int kind, int index, int clicked, float wheel);
enum { PC_UI_MOUSE_LOOT_ROW = 100, PC_UI_MOUSE_LOOT_MENU, PC_UI_MOUSE_LOOT_BAG };

/* The Map frame (map_frame.h, ui_map.cpp, ui_map_draw.cpp): the game's map
 * screen (MENU5) with the shell's chrome around its picture. Pointer kinds
 * for pc_ui_map_mouse: an area row (index: the scene id), a room row (the
 * room index), the header's < > (index -1 / +1), the picture (drags, the
 * wheel), a prompt (PC_UI_MAP_P_*), a tab (its index), the rail elsewhere
 * (its wheel scrolls it). */
enum { PC_UI_MOUSE_MAP_AREA = 120, PC_UI_MOUSE_MAP_ROOM, PC_UI_MOUSE_MAP_STEP, PC_UI_MOUSE_MAP_VIEW,
       PC_UI_MOUSE_MAP_PROMPT, PC_UI_MOUSE_MAP_TAB, PC_UI_MOUSE_MAP_RAIL };
/* The native Data screen's pointer targets (ui_data_draw.cpp, for
 * pc_ui_data_mouse; data_frame.h): Save / Load (index 1, 2), a card pill (1,
 * 2), a save (0..4), the prompt's Yes / No, the status bar (Continue), a tab
 * (the shell tab), a footer hint (index: its PC_UI_* bits), the panel itself
 * (a click there does nothing). */
enum { PC_UI_MOUSE_DATA_OP = 140, PC_UI_MOUSE_DATA_CARD, PC_UI_MOUSE_DATA_FILE, PC_UI_MOUSE_DATA_YES,
       PC_UI_MOUSE_DATA_NO, PC_UI_MOUSE_DATA_STATUS, PC_UI_MOUSE_DATA_TAB, PC_UI_MOUSE_DATA_BUTTON,
       PC_UI_MOUSE_DATA_PANEL };
/* The native Container screen's pointer targets (ui_container_draw.cpp, for
 * pc_ui_ctr_mouse; container_frame.h): a top menu row (0..2), a list row, a tab
 * (0..7), a menu entry, Yes / No, the amount's steppers (+1 / -1 the amount,
 * +2 / -2 the step, 0 OK), the wait's Continue, a footer hint (its PC_UI_*
 * bits), a panel (a click there does nothing). */
enum { PC_UI_MOUSE_CTR_TOP = 160, PC_UI_MOUSE_CTR_ROW, PC_UI_MOUSE_CTR_TAB, PC_UI_MOUSE_CTR_MENU, PC_UI_MOUSE_CTR_YES,
       PC_UI_MOUSE_CTR_NO, PC_UI_MOUSE_CTR_QTY, PC_UI_MOUSE_CTR_STATUS, PC_UI_MOUSE_CTR_BUTTON, PC_UI_MOUSE_CTR_PANEL };
enum { PC_UI_MAP_P_ROOM_PREV, PC_UI_MAP_P_ROOM_NEXT, PC_UI_MAP_P_ROTATE, PC_UI_MAP_P_ZOOM, PC_UI_MAP_P_RESET,
       PC_UI_MAP_P_AREAS, PC_UI_MAP_P_HIDE, PC_UI_MAP_P_BACK, PC_UI_MAP_P_CLOSE, PC_UI_MAP_P_LIST_UP,
       PC_UI_MAP_P_LIST_DOWN, PC_UI_MAP_P_LIST_TAB, PC_UI_MAP_P_LIST_SHOW, PC_UI_MAP_P_LIST_MAP,
       PC_UI_MAP_P_LIST_CLOSE };
/* The frame's picture (the cut-out MENU5's map shows through) in layout
 * units (x, y, w, h) for the current aspect, and for a W-wide layout. */
void pc_ui_map_column(float out[4]);
void pc_ui_map_column_for(float W, float out[4]);
/* The rail's wheel offset in lines from its automatic window, clamped to
 * lo..hi; reset when another room or area is in focus. */
float pc_ui_map_rail_scroll(float lo, float hi);
void pc_ui_map_rail_scroll_reset(void);
/* The frame's pointer targets as last drawn (JSON "hits", so tests click
 * what is drawn): cleared at each draw, one per target. */
void pc_ui_map_hits_clear(void);
void pc_ui_map_hit_record(int kind, int index, float x0, float y0, float x1, float y1);
/* The screen as JSON members (",\"loot\":{...}" appended to out; RPC ui_menu). */
void pc_ui_loot_json(char* out, unsigned size);

#ifdef __cplusplus
}
#endif
#endif
