/* Modern menus: the Workshop, the Items tab's eighth sub-tab (after Misc;
 * L2 / R2 or a click on its chip). State and navigation: ui_workshop.cpp;
 * drawing: ui_workshop_draw.cpp; the game side: workshop_bridge.c (the
 * original's Setup screen, MENUC, and MENU8's rename).
 *
 * ROWS: the six commands (Assemble, Gems, Disassemble, Rename, Repair,
 * Combine) greyed as MENUC greys them, with its reason; Circle opens one
 * (greyed: the invalid sound).
 *   Assemble   ASSEMBLE: Blade, Grip, a row per gem slot of the grip (once
 *              both are chosen) and Next; Circle on a part opens PICK (its
 *              free records; one that does not fit the other part greyed,
 *              MENUC's reason; the part held now first: picking it takes it
 *              out), Next opens NAME (blank: the blade's name), whose Done
 *              sends PC_UI_REQ_WS_ASSEMBLE. The card previews the weapon
 *              (or the blade / grip alone), with a pick list's focused
 *              candidate in place.
 *   Gems       TARGET: weapons, then shields (no gem slots: greyed); SLOTS:
 *              the slots as they will be and Attach (PC_UI_REQ_WS_GEMS,
 *              PC_UI_WS_EDITED when a pick changed a slot on this visit);
 *              Circle on a slot opens PICK (its gem first, then the free
 *              ones). Leaving SLOTS (or its PICK) any other way (Cross,
 *              Triangle, a tab or category switch, a click on a command)
 *              sends PC_UI_REQ_WS_GEMS_LEAVE (the original's
 *              _attachGems(targetGems) on every way out: an equipped target
 *              is re-equipped).
 *   Disassemble TARGET: weapons, then shields (no gems: greyed); CONFIRM
 *              (opens on No; the equipped warning) sends
 *              PC_UI_REQ_WS_DISASSEMBLE.
 *   Rename     TARGET: weapons; NAME opens on the weapon's name (as MENU8
 *              does); Done sends PC_UI_REQ_WS_RENAME.
 *   Repair     CONFIRM (opens on Yes, as the original's) with every record's
 *              DP / PP now and after; Yes sends PC_UI_REQ_WS_REPAIR.
 *   Combine    TARGET: Blades, Shields, Armor (fewer than two: greyed);
 *              COMBINE: First, Second and Combine; PICK lists the category
 *              (a material the workshop does not work greyed); Combine opens
 *              CONFIRM (opens on No) over the result card
 *              (PC_UI_REQ_WS_COMBINE).
 * NAME: the field (20 places) over MENU8's character grid. D-pad: the grid
 * (Down past its last row: Delete and Done); Circle writes the focused
 * character over the one at the cursor and moves on (MENU8's grid; Delete
 * and Done act); Square deletes the character before the cursor; L1 / R1
 * move the cursor. The keyboard types into it, inserting at the cursor as
 * text fields do (pc_ui_workshop_text; Backspace, Delete, Left, Right, Home,
 * End, Enter = Done, Escape = back: pc_ui_workshop_key) while
 * pc_ui_workshop_typing. The request carries the field (pc_ui_request
 * name).
 * A refused change request (the bridge's pc_ui_workshop_refused, on the tick
 * it was sent) puts the sub-tab back on the step that sent it, its choices
 * and name kept, with a line for a moment.
 * Cross steps back one level (out of ROWS it closes, as the other sub-tabs);
 * Triangle closes the menu everywhere (a flow not confirmed changes
 * nothing). */
#ifndef PC_UI_WORKSHOP_H
#define PC_UI_WORKSHOP_H
#include "ui_model.h"
#include "ui_shell.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_UI_WS_ROWS, PC_UI_WS_TARGET, PC_UI_WS_ASSEMBLE, PC_UI_WS_SLOTS, PC_UI_WS_COMBINE, PC_UI_WS_PICK,
       PC_UI_WS_NAME, PC_UI_WS_CONFIRM };
/* What a PICK list fills. */
enum { PC_UI_WSP_BLADE, PC_UI_WSP_GRIP, PC_UI_WSP_GEM, PC_UI_WSP_COMBINE };
/* NAME's action row (the grid's row PC_UI_WS_GRID_ROWS): its columns */
enum { PC_UI_WSN_DELETE, PC_UI_WSN_DONE, PC_UI_WSN_ACTIONS };
/* Keys for pc_ui_workshop_key */
enum { PC_UI_WSK_BACKSPACE, PC_UI_WSK_DELETE, PC_UI_WSK_LEFT, PC_UI_WSK_RIGHT, PC_UI_WSK_HOME, PC_UI_WSK_END,
       PC_UI_WSK_ENTER, PC_UI_WSK_ESCAPE };
/* Mouse kinds (pc_ui_shell_mouse) */
enum { PC_UI_MOUSE_WS_ROW = 200, /* a command row */
       PC_UI_MOUSE_WS_ENTRY,     /* an entry of the open list (TARGET, PICK) */
       PC_UI_MOUSE_WS_SLOT,      /* a row of ASSEMBLE / SLOTS / COMBINE */
       PC_UI_MOUSE_WS_GRID,      /* NAME: a cell, row * PC_UI_WS_GRID_COLS + col (the action row's: past the grid) */
       PC_UI_MOUSE_WS_PLACE,     /* NAME: a place of the field 0..19 (moves the cursor there) */
       PC_UI_MOUSE_WS_CONFIRM }; /* CONFIRM: 0 Yes, 1 No */

/* An entry of the open list (TARGET, PICK). */
typedef struct {
    int list;                  /* PC_UI_WSL_* of the record, -1 none (a placeholder or a category) */
    const pc_ui_ws_row* rec;   /* the record's row in the snapshot, or null */
    const char* label;         /* the entry's text (a record's name) */
    int enabled;
    const char* reason;        /* greyed: why (MENUC's line), or null */
    int current;               /* PICK: what the slot holds now (picking it takes it out) */
    int category;              /* TARGET of Combine: 0 blades, 1 shields, 2 armor; else -1 */
    int count;                 /* TARGET of Combine: the category's items */
} pc_ui_ws_entry;

enum { PC_UI_WS_ENTRIES = 64 };
typedef struct {
    int valid;                 /* the snapshot arrived */
    int mode;                  /* PC_UI_WS_* */
    int flow;                  /* PC_UI_WSA_* of the open flow, -1 at the rows */
    int row;                   /* the command list's row */
    int cursor;                /* TARGET / PICK: the entry; ASSEMBLE / SLOTS / COMBINE: the row; CONFIRM: 0 Yes 1 No */
    int entries;               /* TARGET / PICK */
    pc_ui_ws_entry entry[PC_UI_WS_ENTRIES];
    int slots;                 /* ASSEMBLE / SLOTS / COMBINE: rows (the last finishes: Next, Attach, Combine) */
    int finish;                /* that row can be chosen now */
    const pc_ui_ws_row* target; /* SLOTS, NAME (rename), CONFIRM (disassemble): the record */
    int target_list;           /* its list (PC_UI_WSL_*) */
    const pc_ui_ws_row* blade; /* ASSEMBLE: the parts chosen (null none) */
    const pc_ui_ws_row* grip;
    const pc_ui_ws_row* gems[3]; /* ASSEMBLE, SLOTS: per slot (null empty) */
    int gem_slots;             /* ASSEMBLE (the grip's), SLOTS (the target's) */
    int combine;               /* COMBINE / its PICK / CONFIRM: 0 blades, 1 shields, 2 armor */
    const pc_ui_ws_row* first; /* COMBINE: the two chosen (null none) */
    const pc_ui_ws_row* second;
    int pick;                  /* PICK: PC_UI_WSP_* */
    int pick_slot;             /* PICK: the slot (gem k, combine 0 / 1) */
    int confirm_kind;          /* CONFIRM: PC_UI_WSA_DISASSEMBLE, _REPAIR or _COMBINE */
    const char* prompt;        /* CONFIRM: the original's question */
    char name[96];             /* NAME: the field as UTF-8 (trailing blanks cut) */
    unsigned char field[PC_UI_WS_NAME_LEN]; /* NAME: the field (game codes) */
    int name_cursor;           /* NAME: the cursor 0..19 */
    int grid_row, grid_col;    /* NAME: the grid's focus (grid_row PC_UI_WS_GRID_ROWS: the action row) */
    int keyboard;              /* NAME: the last edit came from the keyboard */
    int assembling;            /* NAME: for a new weapon (blank keeps the blade's name) */
    const pc_ui_item_details* preview; /* the card's preview (the bridge's, for this view's choice), or null */
    int preview_category;
    const char* message;       /* a refused request's line for a moment, or null */
} pc_ui_workshop_view;

/* The bridge's snapshot (copied) and the sub-tab's state. */
void pc_ui_workshop_set(const pc_ui_workshop* m);
const pc_ui_workshop* pc_ui_workshop_model(void);
void pc_ui_workshop_view_get(pc_ui_workshop_view* v);
/* The Workshop sub-tab is the Items tab's current one (the bridge fills while it shows). */
int pc_ui_shell_workshop_shown(void);
/* The preview the card wants: 0 none; else key[0] PC_UI_WSA_ASSEMBLE {blade, grip, gems packed} (records,
 * -1 none), PC_UI_WSA_GEMS {isShield, record, gems packed}, PC_UI_WSA_COMBINE {category, first, second}. */
int pc_ui_shell_workshop_wanted(int key[6]);
/* The bridge refused the last change request (back to the step that sent it; a line shows for a moment). */
void pc_ui_workshop_refused(void);
/* Keyboard (dev_overlay.cpp, RPC ui_menu {text}): while the name field shows. */
int pc_ui_workshop_typing(void);
void pc_ui_workshop_text(const char* utf8);
void pc_ui_workshop_key(int key);

/* For ui_shell.cpp: the Items tab hands the sub-tab its input. */
void pc_ui_ws_reset(void);                 /* out of any flow onto the commands, nothing sent (open, abandon) */
void pc_ui_ws_leave(void);                 /* the same, the player leaving (tab switch, close): Gems' slots
                                            * left send PC_UI_REQ_WS_GEMS_LEAVE */
int pc_ui_ws_base(void);                   /* at the commands (category switches allowed) */
int pc_ui_ws_back(void);                   /* Cross: one step back; 0 at the commands */
void pc_ui_ws_tick(const pc_ui_input* in); /* one tick of input (shown) */
int pc_ui_ws_takes_tabs(void);             /* L1 / R1 are the sub-tab's (NAME's cursor) */
int pc_ui_ws_mouse_focus(int kind, int index); /* hover: focus it; 1 when it did */
int pc_ui_ws_mouse_click(int kind, int index); /* a click: 1 when it did all (no confirm to add) */
void pc_ui_ws_json(char* out, unsigned size);  /* ",\"workshop\":{...}" */
void pc_ui_shell_push(int kind, int slot, int item, int shield, int count);
void pc_ui_shell_push_request(const pc_ui_request* r);

#ifdef __cplusplus
}
#endif
#endif
