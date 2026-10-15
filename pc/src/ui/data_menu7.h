/* Modern menus, the native Data screen: MENU7's side of the frame
 * (src/MENU/MENU7.PRG/260.c, ROOD_PC; data_frame.h has the design).
 *
 * 260.c keeps only the hooks: its drawing gate (four statics it owns, so a
 * save state restores them with the rest of the overlay's statics), the
 * vs_menu7_dataMenu and vs_menu7_saveContainerMenu wrappers, the chooser's list
 * in states 5 / 6 and the hand-off before a load's jump. Everything else, the
 * latch, the pad injection and the publish of MENU7's statics, is data_bridge.c's
 * and container_bridge.c's, reading those statics through the table of pointers
 * below (one const table in 260.c, by address: no copies, no writes but the four
 * flags no_draw, framed, gated and run).
 *
 * The Data and Container runs share _pcRun (the runs latched). container_bridge.c
 * writes it too: ++run at its latch, and after the MENU7 reload in the Container's
 * state 8 (the reload zeroes the overlay's statics, _pcRun back to 0) it restores
 * run and framed to re-latch the run it had.
 *
 * Included by 260.c (after its own types) and data_bridge.c. */
#ifndef PC_UI_DATA_MENU7_H
#define PC_UI_DATA_MENU7_H
#include "data_frame.h"
#ifdef __cplusplus
extern "C" {
#endif

/* MENU7's saveFileInfo_t (0x80 bytes, no pointers: the same on LP64; 260.c
 * checks the size against its own). */
typedef struct {
    unsigned key;
    int slot_state;                 /* unk4.base.slotState (enum slotState_e) */
    unsigned generation;            /* unk4.base.generation */
    int base_unk8;
    union {
        struct {
            signed char f, s, m, h;
        } t;
        unsigned all;
    } game_time;                    /* unk4.stats.gameTime (vs_Gametime_t) */
    unsigned short saves;           /* cumulativeSaveCount */
    unsigned short game_saves;      /* currentGameSaveCount */
    unsigned short hp, hp_max;
    char location, clear, map, stats_unk1b; /* saveLocation, clearCount, mapCompletion */
    unsigned short mp, mp_max;
    unsigned short unk20[16];
    char unk44[0x1C];
    char unk60[0x20];
} pc_ui_data_menu7_file;

/* MENU7's fileMenuElements_t (52 bytes). */
typedef struct {
    char state, slot_id, outer_blend, inner_blend, selected, slot_unavailable, save_location, unk7;
    short target_position, unk_a, x, y, w, h;
    char text[32];
} pc_ui_data_menu7_element;

/* MENU7's file-scope statics and helpers, by address (260.c _pcStatics). */
typedef struct {
    /* 260.c's own (ROOD_PC): the bridge writes these */
    int* no_draw;                   /* _pcNoDraw: the drawing gate */
    int* framed;                    /* _pcFramed: this run is framed */
    unsigned* gated;                /* _pcGated: draws the gate skipped since the last publish */
    int* run;                       /* _pcRun: the runs latched (a save state brings back an old one) */
    /* MENU7's (read only) */
    unsigned short* const* text_table;  /* _textTable (MCMAN, heap) */
    const void* files;              /* &_saveFileInfo (pc_ui_data_menu7_file*, heap) */
    const void* elements;           /* _fileMenuElements[10] (pc_ui_data_menu7_element) */
    char* const* message;           /* _memoryCardMessage */
    const int* cursor_xy;           /* _selectCursorXy */
    const char* data_not_saved;     /* _dataNotSaved */
    const char* fade;               /* _fileMenuScreenFade */
    const char* error_offset;       /* _loadSaveDataErrorOffset */
    const int* progress_counter;    /* _fileProgressCounter */
    const unsigned short* progress_target;   /* _fileProgressTarget */
    const unsigned short* progress_previous; /* _filePreviousProgressCounter */
    const unsigned short* progress_position; /* _fileProgressPosition */
    int (*elements_active)(void);   /* _fileMenuElementsActive */
    int (*current_save)(void);      /* _findCurrentSaveOnActiveMemcard */
    unsigned (*newest_save)(void);  /* _getNewestSaveFile */
    int (*progress_div)(int, int);  /* _progressDiv (MIPS divide by zero) */
} pc_ui_data_menu7;

/* 260.c's check that MENU7's types match the two above (one line there). */
#define PC_UI_DATA_MENU7_LAYOUT(file_t, element_t)                                               \
    typedef char _pcLayout[sizeof(file_t) == sizeof(pc_ui_data_menu7_file)                       \
                && sizeof(element_t) == sizeof(pc_ui_data_menu7_element)                         \
            ? 1                                                                                  \
            : -1]

/* MENU7's menus as 260.c's wrappers call them: begin, the original (`menu`), end. */
typedef int (*pc_ui_menu7_fn)(unsigned char* state);
int pc_ui_data_call(const pc_ui_data_menu7* m7, unsigned char* state, pc_ui_menu7_fn menu);
int pc_ui_container_call(const pc_ui_data_menu7* m7, unsigned char* state, pc_ui_menu7_fn menu);

/* vs_menu7_dataMenu's wrapper, before the original runs (entry: *state): the
 * latch at state 3, the gate, the stamp, the planner's press over the pad. */
void pc_ui_data_call_begin(const pc_ui_data_menu7* m7, int entry);
/* After it: the real pad back, the publish, the run's end when it returned
 * nonzero (r). Returns r. */
int pc_ui_data_call_end(int entry, int state, int r);
/* State 13's load, before the jump that never returns: the frame ends, the
 * real pad back, the gate off. */
void pc_ui_data_call_loading(void);
/* MENU7's statics published into the Data model as if MENU7 were in `state`
 * (container_bridge.c: the Container's save list as the save flow, 8). */
void pc_ui_data_menu7_publish(const pc_ui_data_menu7* m, int state);

/* The Container (container_bridge.c, container_frame.h): vs_menu7_saveContainerMenu's
 * wrapper, before and after the original runs (the latch at entry 0, the gate, the
 * prompts' injected answers, the publish; r returned). */
void pc_ui_container_call_begin(const pc_ui_data_menu7* m7, int entry);
int pc_ui_container_call_end(int entry, int state, int r);

#ifdef __cplusplus
}
#endif
#endif
