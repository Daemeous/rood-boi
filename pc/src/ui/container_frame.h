/* Modern menus, the native Container screen (pc/docs/Modern_Menus.md
 * "Container", plan pc/docs/plans/2026-10-15-modern-menus-container.md).
 *
 * The save room's container is two overlays in sequence:
 *   - MENU7's vs_menu7_saveContainerMenu (260.c) runs unchanged and framed,
 *     as the Data screen's MENU7 run does: its drawing gated, its statics
 *     published, the player's native answers injected as single pad presses
 *     (container_bridge.c pc_ui_container_call_begin / _end). Its prompts
 *     (check, containerWarn, cancelWarning) and its card wait are this
 *     screen's; its save list (_showSaveMenu(2)) is the Data screen's model in
 *     a container mode (data_bridge.c).
 *   - MENUD's vs_menuD_exec (direct entry 14) is ported natively on MENU7's
 *     vs_menu_inventoryStorage (container_bridge.c, container_ops.c): the
 *     original's modes 1:1 (TOP, LIST and its sub-modes, the commit's
 *     CONFIRM), the pad read as the original reads it, the RAM written with
 *     the original's expressions in the original's order.
 *
 *   container_bridge.c  the game side: both layers (writes RAM as the originals do)
 *   ui_container.cpp    pure: the model (filled by the bridge), the pointer's
 *                       requests and goals, JSON (RPC ui_menu "container")
 *   ui_container_draw.cpp  ImGui drawing from the model
 *
 * C, included by data_menu7.h (260.c), the bridge and the C++ side: no
 * game headers. */
#ifndef PC_UI_CONTAINER_FRAME_H
#define PC_UI_CONTAINER_FRAME_H
#ifdef __cplusplus
extern "C" {
#endif

/* Phases, from MENU7's state (and the MENUD session). */
enum {
    PC_UI_CTR_PH_OFF,
    PC_UI_CTR_PH_PROMPT_CHECK,  /* state 1: "Check the container?" */
    PC_UI_CTR_PH_BUSY,          /* 2, 3, 6, 8, 9; a prompt animating */
    PC_UI_CTR_PH_WAIT,          /* 4: emptyCard / insertError, any button */
    PC_UI_CTR_PH_PROMPT_WARN,   /* 5: containerWarn */
    PC_UI_CTR_PH_TRANSFER,      /* 7, 14 (MENUD): the native screen's modes */
    PC_UI_CTR_PH_PROMPT_CANCEL, /* 12 (and 11's re-ask): cancelWarning */
    PC_UI_CTR_PH_SAVE,          /* 10, 11: the Data screen's save list (container mode) */
    PC_UI_CTR_PH_CLOSING,       /* 13..16 */
    PC_UI_CTR_PHASES
};

/* The MENUD port's modes (TRANSFER). */
enum {
    PC_UI_CTR_TOP,             /* func_801089BC: Remove / Store / End */
    PC_UI_CTR_LIST,            /* func_80107490 state 2 (a list or its empty line) */
    PC_UI_CTR_PICK2,           /* state 6: the second pick */
    PC_UI_CTR_ACTIONS,         /* state 9 */
    PC_UI_CTR_DISCARD_CONFIRM, /* _discardMenu's Yes / No */
    PC_UI_CTR_DISCARD_QTY,     /* _discardMenu's amount */
    PC_UI_CTR_SORT,            /* _displaySortMenu */
    PC_UI_CTR_DETAIL,          /* func_80105008 */
    PC_UI_CTR_COMMIT_CONFIRM,  /* func_801072B0 */
    PC_UI_CTR_MODES
};

/* A row's marks (func_80106C64's row types): EQUIPPED 0xCA00 (worn, or a part of
 * a worn weapon / shield), ATTACHED 0xCC00 (a part of an assembly or set in a shield),
 * LOCKED (tab 7's parts, 0xCC01: not selectable). */
enum { PC_CTR_ROW_EQUIPPED = 1, PC_CTR_ROW_ATTACHED = 2, PC_CTR_ROW_LOCKED = 4 };

enum { PC_UI_CTR_ROWS = 256, PC_UI_CTR_TABS = 8, PC_UI_CTR_NAME = 48, PC_UI_CTR_TEXT = 192, PC_UI_CTR_SORTS = 24 };

/* A row of the list (tab 0..6: the source's category in its list order; 7:
 * the transfer list). */
typedef struct {
    int category, slot;  /* the record (vs_menu_containerData) */
    int flags;           /* container_ops.h PC_CTR_ROW_* (EQUIPPED, ATTACHED, LOCKED) */
    int id, material, count; /* the Items list's icon picks; count: misc */
    char name[PC_UI_CTR_NAME];
    char sub[64];        /* material and kind */
    char help[PC_UI_CTR_TEXT]; /* the info line (the game's description) */
} pc_ui_ctr_row;

/* The focused row's card. */
typedef struct {
    int valid, category;
    int dp, dp_max, pp, pp_max, has_dp, has_pp;
    int str, intel, agl;
    int gem_slots, gems;   /* weapon (grip's slots), shield, grip; gems set */
    char kind[64];
    char parent[PC_UI_CTR_NAME]; /* attached: the weapon / shield it is set in */
} pc_ui_ctr_card;

/* A menu (actions, sort, the Yes / No's): entries, focus. */
typedef struct {
    int count, focus, offset;  /* offset: the original's scroll (7 visible) */
    int kind[PC_UI_CTR_SORTS];
    int enabled[PC_UI_CTR_SORTS];
    char label[PC_UI_CTR_SORTS][40];
    char desc[PC_UI_CTR_SORTS][PC_UI_CTR_TEXT];
} pc_ui_ctr_menu;

/* The model (container_bridge.c fills it, ui_container.cpp / _draw read it). */
typedef struct {
    /* the MENU7 frame */
    int latched, run;          /* this run is framed (MENU7 entry 0's latch) */
    unsigned stamp;            /* the last framed MENU7 call or MENUD tick (vsync) */
    int m7_state;              /* MENU7's *state after the call */
    int phase;                 /* PC_UI_CTR_PH_* */
    int prompt_yes;            /* the prompt's highlight: 1 Yes, 0 No (MENU7's menu items 0x1E / 0x1F) */
    int prompt_ready;          /* _promptYesNo's handleInput took this call (it drew its gated cursor) */
    char prompt_text[256];
    char wait_text[256];
    int location;              /* D_800F4E6B */
    char location_name[64];
    unsigned gated, presses, busy_drops, rejected, requests;
    char hide_saved;           /* vs_mainMenu_hideMenu before the latch */
    /* the MENUD session */
    int session;               /* the native MENUD session runs */
    int settling;              /* its first ticks (the press that answered MENU7's Yes): input dropped */
    int handed_back;           /* this run's MENUD returned (MENU7 resumes at state 8) */
    int mode;                  /* PC_UI_CTR_* */
    int dir;                   /* D_80109A7A: 0 Remove, 1 Store; -1 before the first choice */
    int top;                   /* the top menu's cursor (D_800F4EE8.unk9E): 0 Remove, 1 Store, 2 End */
    char top_label[3][32];     /* MENUD's REMOVE / STORE, and End */
    char top_desc[3][PC_UI_CTR_TEXT]; /* removeDesc, storeDesc, "End item transfer." */
    int tab;                   /* (unkA0[0] - 1) & 7 */
    int rows, cursor, page, sel; /* the list; cursor = sel + page (the original's 8-row window) */
    int list_active;           /* the row list takes input (else the empty line) */
    int picked;                /* PICK2: the first pick's row, -1 */
    pc_ui_ctr_row row[PC_UI_CTR_ROWS];
    char empty_text[PC_UI_CTR_TEXT];
    char info[PC_UI_CTR_TEXT]; /* the info line */
    int staged;                /* the transfer list's entries */
    int tab_count[PC_UI_CTR_TABS], tab_cap[PC_UI_CTR_TABS];
    int bag_count[7], bag_cap[7], box_count[7], box_cap[7]; /* the summary table (TOP) */
    int dest_count[7], dest_staged[7], dest_cap[7];          /* the right panel: destination + staged / cap */
    int misc_after, misc_cap;                                /* misc after stacking (D_80109A82) / 256 or 64 */
    int misc_live;             /* the same for what is staged now (host scratch: the panel's live count) */
    pc_ui_ctr_card card;
    pc_ui_ctr_menu menu;       /* ACTIONS, SORT, the Yes / No's */
    int discard_type, discard_count, discard_max, discard_step;
    char confirm_text[PC_UI_CTR_TEXT];
    int detail_index, detail_count, detail_slot, detail_category;
    unsigned ticks;
} pc_ui_ctr_model;

/* The bridge's results for menu_bridge.c's direct tick. */
enum { PC_UI_CTR_STAY = 0, PC_UI_CTR_HANDED_BACK = 1 };

/* Requests (the pointer -> the bridge; queued, applied at the bridge's next
 * tick in order). PRESS a: PC_UI_* bits as a pad press of that frame; FOCUS
 * a = what (PC_UI_CTR_F_*), b = the index: the cursor moved there (the top
 * menu writes unk9E as Up / Down do); TAB a: the tab, walked one step a
 * press; GOAL a = PC_UI_CTR_G_*, b: MENU7's prompts and wait. */
enum { PC_UI_CTR_REQ_PRESS = 1, PC_UI_CTR_REQ_FOCUS, PC_UI_CTR_REQ_TAB, PC_UI_CTR_REQ_GOAL };
enum { PC_UI_CTR_F_TOP, PC_UI_CTR_F_ROW, PC_UI_CTR_F_MENU };
enum { PC_UI_CTR_G_YES = 1, PC_UI_CTR_G_NO, PC_UI_CTR_G_DISMISS };
typedef struct {
    int kind, a, b;
} pc_ui_ctr_req;

/* ui_container.cpp */
pc_ui_ctr_model* pc_ui_ctr_state(void);
/* The screen draws: latched and fresh (8 vsyncs), not in the save list (the Data screen draws that). */
int pc_ui_container_visible(void);
int pc_ui_ctr_post(int kind, int a, int b);   /* 0: refused (busy, full) */
int pc_ui_ctr_pop(pc_ui_ctr_req* out);
void pc_ui_ctr_reset_mailbox(void);
/* The pointer (ui_draw.cpp): kind PC_UI_MOUSE_CTR_*, index; click; right; wheel in notches. */
void pc_ui_ctr_mouse(int kind, int index, int click, int right, float wheel);
void pc_ui_ctr_hover(int* kind, int* index);
void pc_ui_ctr_hits_clear(void);
void pc_ui_ctr_hit_record(int kind, int index, float x0, float y0, float x1, float y1);
/* The open screen as ",\"container\":{...}" (or null). */
void pc_ui_container_json(char* out, unsigned size);

/* container_bridge.c -> menu_bridge.c */
int pc_ui_container_framed(void);     /* this MENU7 run is latched: MENUD is native */
int pc_ui_container_begin(void);      /* MENUD state 0 (refused without the storage) */
int pc_ui_container_frame(int settling); /* one MENUD tick: PC_UI_CTR_STAY / _HANDED_BACK */
void pc_ui_container_abandon(int reset); /* the session dropped: nothing written, nothing freed */
int pc_ui_container_on(void);         /* the frame is latched (the heartbeat's) */
void pc_ui_container_end(void);       /* the frame ends (hideMenu back, the HUD back) */
void pc_ui_container_forget(void);    /* a soft reset: everything forgotten, hideMenu 0 */

#ifdef __cplusplus
}
#endif
#endif
