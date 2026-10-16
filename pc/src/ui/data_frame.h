/* Modern menus, the native Data screen (pc/docs/Modern_Menus.md "Data", plan
 * pc/docs/plans/2026-10-15-modern-menus-data.md): System > Data runs the
 * game's own save / load screen (MENU7, src/MENU/MENU7.PRG/260.c) unchanged
 * as the only state machine: its pad, decisions, sounds, cursor memories,
 * card I/O and every write to the game's settings. While a run is framed the
 * screen differs in three things only (260.c, ROOD_PC):
 *   - MENU7's GPU and text output is gated (all its logic still runs);
 *   - a click is a goal: the planner (data_plan.c) turns it into at most one
 *     synthetic pad press a frame, injected at the top of vs_menu7_dataMenu
 *     while the real pad is idle (the real values back after the call);
 *   - after every call MENU7's statics are published by value
 *     (pc_ui_data_game) into the model ImGui draws from (ui_data_draw.cpp).
 * The Save / Load chooser (MENU7 states 5 and 6, a MAINMENU row list) is
 * the bridge's own list (data_bridge.c pc_ui_data_frame_op).
 *
 *   260.c (ROOD_PC)  the hooks only: the drawing gate, the wrapper, the
 *                    chooser's list call, the load hand-off, and a table of
 *                    pointers to its statics (data_menu7.h)
 *   data_bridge.c    the game side: the latch at state 3, the stamp, the
 *                    planner's press, the publish (through that table),
 *                    lifecycle, the chooser list
 *   data_plan.c      pure: phase, cursor decode, goal -> press
 *   ui_data.cpp      pure: the model, the mailbox (one goal), the pointer,
 *                    JSON (RPC ui_menu "data")
 *
 * C, included by 260.c, C48.c (game flags: no system headers), the bridge
 * and the C++ side. */
#ifndef PC_UI_DATA_FRAME_H
#define PC_UI_DATA_FRAME_H
#ifdef __cplusplus
extern "C" {
#endif

/* Phases (derived from one publish only; data_plan.c pc_ui_data_phase). */
enum {
    PC_UI_DATA_PH_OFF,    /* no framed run */
    PC_UI_DATA_PH_BUSY,   /* MENU7 is not taking input (animating, card I/O, progress, fade) */
    PC_UI_DATA_PH_OPS,    /* the Save / Load chooser (MENU7 state 6, the bridge's list) */
    PC_UI_DATA_PH_CARD,   /* the memory card rows (cursor x 180) */
    PC_UI_DATA_PH_FILES,  /* the file list (cursor x 24) */
    PC_UI_DATA_PH_PROMPT, /* a Yes / No prompt (cursor x 0) */
    PC_UI_DATA_PH_WAIT,   /* a message waiting for a press (no cursor) */
    PC_UI_DATA_PHASES
};

/* Goals (the mailbox holds one; a new post replaces it). a by kind: OP 1 save
 * 2 load; CARD 1 / 2; FILE 0..4; ANSWER 1 yes 0 no; TAB the shell tab. */
enum {
    PC_UI_DATA_GOAL_NONE, PC_UI_DATA_GOAL_OP, PC_UI_DATA_GOAL_CARD, PC_UI_DATA_GOAL_FILE,
    PC_UI_DATA_GOAL_ANSWER, PC_UI_DATA_GOAL_DISMISS, PC_UI_DATA_GOAL_BACK, PC_UI_DATA_GOAL_CLOSE,
    PC_UI_DATA_GOAL_TAB, PC_UI_DATA_GOAL_MOVE, PC_UI_DATA_GOALS
};

/* MENU7's message (_memoryCardMessage, by its MCMAN offset; data_bridge.c maps). */
enum {
    PC_UI_DATA_MSG_NONE, PC_UI_DATA_MSG_SELECT_SLOT, PC_UI_DATA_MSG_CHECKING, PC_UI_DATA_MSG_INSERT_ERROR,
    PC_UI_DATA_MSG_EMPTY_CARD, PC_UI_DATA_MSG_SAVE_FAILED, PC_UI_DATA_MSG_LOAD_FAILED, PC_UI_DATA_MSG_SELECT_FILE,
    PC_UI_DATA_MSG_FILE_CORRUPT, PC_UI_DATA_MSG_SAVING, PC_UI_DATA_MSG_LOADING, PC_UI_DATA_MSG_SAVED,
    PC_UI_DATA_MSG_LOADED, PC_UI_DATA_MSG_REMOVED, PC_UI_DATA_MSG_ACCESSING0, PC_UI_DATA_MSG_ACCESSING1,
    PC_UI_DATA_MSG_CONTAINER_WARN, PC_UI_DATA_MSG_FORMAT_PROMPT, PC_UI_DATA_MSG_FORMATTING,
    PC_UI_DATA_MSG_FORMAT_FAILED, PC_UI_DATA_MSG_FORMAT_CANCELLED, PC_UI_DATA_MSG_OVERWRITE_PROMPT,
    PC_UI_DATA_MSG_NO_DATA, PC_UI_DATA_MSG_CARD_FULL, PC_UI_DATA_MSG_CONTAINER_LOSS, PC_UI_DATA_MSG_OTHER,
    PC_UI_DATA_MSGS
};

/* The prompt shown (PROMPT phase), by its message. */
enum {
    PC_UI_DATA_PROMPT_NONE, PC_UI_DATA_PROMPT_OVERWRITE, PC_UI_DATA_PROMPT_FORMAT, PC_UI_DATA_PROMPT_CONTAINER,
    PC_UI_DATA_PROMPT_CONTAINER_LOSS
};

/* How a framed run ended (pc_ui_data_frame_finish). */
enum { PC_UI_DATA_RUNNING, PC_UI_DATA_BACK, PC_UI_DATA_CLOSED, PC_UI_DATA_LOADED };

/* MENU7's cursor (_selectCursorXy = y << 16 | x) by screen: the card rows
 * (180, (card + 1) * 16 + 10), the file list (24, slot * 40 + 62, the file
 * slot + page with the window's page from element 5's y = 72 - page * 40),
 * the prompt (0, cancelled * 16 + 10). */
enum {
    PC_UI_DATA_CURSOR_CARD_X = 180, PC_UI_DATA_CURSOR_FILE_X = 24, PC_UI_DATA_CURSOR_PROMPT_X = 0,
    PC_UI_DATA_FILES = 5, PC_UI_DATA_NAME = 64, PC_UI_DATA_TEXT = 256
};

/* One file of the scanned card (MENU7's _saveFileInfo[i]). */
typedef struct {
    int state;              /* slotState: 0 in use (no room), 1 free, 2 temporary, 3+ a save */
    unsigned generation;
    int h, m, s;            /* stats.gameTime */
    int saves;              /* stats.cumulativeSaveCount */
    int hp, hp_max, mp, mp_max;
    int location;           /* stats.saveLocation (1..63 a save point, >= 0x40 a container) */
    int clear;              /* stats.clearCount */
    int map;                /* stats.mapCompletion */
    const unsigned char* name; /* the location's MCMAN string (decoded by the bridge, never kept) */
} pc_ui_data_file_game;

/* MENU7's statics after a vs_menu7_dataMenu call (data_bridge.c menu7_publish). */
typedef struct {
    int state;              /* *state after the call */
    int save_ok, at_point, in_battle; /* the chooser's Save row (state 5) */
    int data_not_saved;     /* _dataNotSaved */
    int container_empty;    /* vs_main_settings bit 0x10 */
    int cursor_xy;          /* _selectCursorXy (0 outside the flows) */
    int elements_active;    /* _fileMenuElementsActive(): nothing animating */
    int list_up;            /* the file list's elements (5..9) are set up */
    int list_y0;            /* element 5's y (the window's page) */
    int msg;                /* PC_UI_DATA_MSG_* (0 outside the flows) */
    int msg_off;            /* _memoryCardMessage - _textTable, or -1 */
    const unsigned char* msg_text; /* (decoded by the bridge, never kept) */
    int progress_counter;   /* _fileProgressCounter (< 0: the completion flash) */
    int progress_x;         /* the sweep's x as _drawFileMenuElement draws it (80 .. 320) */
    int progress_target;    /* _fileProgressTarget */
    int fade;               /* _fileMenuScreenFade (0..30) */
    int files_valid;        /* files[] hold a completed scan (list_up) */
    pc_ui_data_file_game files[PC_UI_DATA_FILES];
    int current;            /* _findCurrentSaveOnActiveMemcard(): 1..5, 0 none */
    int newest;             /* _getNewestSaveFile(): 0..4 */
    int location;           /* D_800F4E6B: this room's save point */
    const unsigned char* location_name;
    int card_memory;        /* D_800F4EE8.unk85[3] + 1 where MENU7 starts its card rows */
    int gated;              /* drawing calls MENU7 made and the gate skipped this call */
} pc_ui_data_game;

/* The goal slot (ui_data.cpp's mailbox; the planner advances it). */
typedef struct {
    int kind, a;
    int automatic;          /* the auto-advance after an OP click (stops at WAIT / PROMPT) */
    int age;                /* frames in a phase that takes it (expires at 60) */
    int total;              /* frames since posted (expires at 600) */
    int cool;               /* a press went out last frame: this one has none */
    int walking;            /* CLOSE / TAB: backing out toward the chooser */
} pc_ui_data_goal;

/* 260.c -> data_bridge.c (and data_menu7.h's calls) */
/* vs_menu7_dataMenu state 3: 1 when the shell armed the frame (this run is framed). */
int pc_ui_data_frame_begin(int run);
/* 1 while the bridge frames run `run` (a save state restores MENU7's latch without the host's model). */
int pc_ui_data_framed(int run);
/* Every framed call, before MENU7 runs (the frame's freshness). */
void pc_ui_data_frame_stamp(int state);
/* The planner's press for this call, written over the pad (only called while the real pad is idle). */
void pc_ui_data_frame_inject(unsigned* pressed, int* repeat);
/* State 5: the chooser's Save row (enabled, at a save point, in battle). */
void pc_ui_data_frame_ops(int save_ok, int at_point, int in_battle);
/* State 6: the chooser's result in getSelectedRow() + 1's protocol: 0 stay, 1 Save, 2 Load, -1 back,
 * -2 close. */
int pc_ui_data_frame_op(void);
void pc_ui_data_frame_publish(const pc_ui_data_game* g);
/* The run ended (PC_UI_DATA_BACK, _CLOSED, _LOADED). */
void pc_ui_data_frame_finish(int code);

/* container_bridge.c -> data_bridge.c: the Container's save list (MENU7 states 10 / 11,
 * _showSaveMenu(2)) on this model in container mode, published as the save flow (state 8). */
void pc_ui_data_container_begin(int run);
void pc_ui_data_container_end(void);
int pc_ui_data_container_on(void);

/* menu_bridge.c -> data_bridge.c */
void pc_ui_data_arm(void);   /* deliver(7): the shell hid for Data */
int pc_ui_data_armed(void);  /* armed, no MENU7 run yet (C48.c: the title off screen) */
int pc_ui_data_on(void);     /* armed or framed */
int pc_ui_data_end(void);    /* the frame ends: the pending tab or -1 */
void pc_ui_data_forget(void); /* a soft reset / jump (idempotent) */
int pc_ui_data_busy(void);   /* a framed run in card I/O (PC resets and quits may defer) */

/* The model (data_bridge.c fills it, ui_data.cpp and ui_data_draw.cpp read it). */
typedef struct {
    int state;              /* as pc_ui_data_file_game */
    unsigned generation;
    int h, m, s, saves, hp, hp_max, mp, mp_max, location, clear, map;
    int current, newest;    /* this game's own save; the newest generation */
    char name[PC_UI_DATA_NAME];
} pc_ui_data_file;
typedef struct {
    int armed;
    int container;          /* the Container's save list (container_bridge.c): no tabs, no chooser */
    unsigned armed_vsync;
    int framed, run, stamped;
    unsigned stamp;
    int finished, finish_code;
    int published;
    int state, op;          /* op: 1 save, 2 load, 0 the chooser */
    int save_ok, at_point, in_battle, warp_blocked, data_not_saved, container_empty;
    int cursor_xy, elements_active, list_up, page;
    int settling;           /* this publish's call carried a press: MENU7 lays the list out from the new
                             * page next call (until then the page and the file read one page behind) */
    int card, file, cancelled; /* the cursor decoded (-1: not on that screen) */
    int msg, msg_off, prompt;
    char msg_text[PC_UI_DATA_TEXT];
    int progress_counter, progress_x, progress_target, fade;
    int files_valid;
    pc_ui_data_file files[PC_UI_DATA_FILES];
    int current, newest;
    int phase;
    int card_sel;           /* the card MENU7 checks / lists (1, 2), 0 none yet */
    int card_status[2];     /* per card: -1 not checked, -2 no card, else its save count */
    int ops_row;            /* the chooser's cursor (0 Save, 1 Load) */
    int ops_open;           /* the chooser takes input */
    int play_h, play_m, play_s;
    int location;           /* this room's save point (D_800F4E6B) */
    char location_name[PC_UI_DATA_NAME];
    int hp, hp_max, mp, mp_max, map, saves; /* this journey (live, read only) */
    unsigned gated, presses, injected_frames;
    unsigned busy_drops, rejected, expired;
} pc_ui_data_model;

/* ui_data.cpp */
pc_ui_data_model* pc_ui_data_state(void);
pc_ui_data_goal* pc_ui_data_goal_slot(void);
/* The frame draws: framed with a stamp at most 8 vsyncs old, or armed and not stamped yet for up to 30;
 * or the Container screen does (MENU7's other framed run, container_frame.h): a native MENU7 screen
 * shows (the overlay hands it the mouse's clicks). */
int pc_ui_data_visible(void);
/* A goal (dropped and counted while MENU7 is busy, refused where the phase does not take it). */
void pc_ui_data_post(int kind, int a);
/* The mailbox empty (a new run, the frame's end). */
void pc_ui_data_reset_mailbox(void);
/* The pointer (ui_draw.cpp): kind PC_UI_MOUSE_DATA_*, index; click; wheel in notches; right a right click. */
void pc_ui_data_mouse(int kind, int index, int click, float wheel, int right);
/* The soft highlight (hover, wheel): kind PC_UI_MOUSE_DATA_* and index, or 0. */
void pc_ui_data_hover(int* kind, int* index);
void pc_ui_data_hits_clear(void);
void pc_ui_data_hit_record(int kind, int index, float x0, float y0, float x1, float y1);
/* The open frame as ",\"data\":{...}" (or ",\"data\":null"). */
void pc_ui_data_json(char* out, unsigned size);

#ifdef __cplusplus
}
#endif
#endif
