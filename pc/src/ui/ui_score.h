/* Modern menus: the native Score screen, a sub-screen of the System tab
 * (System > Score). State and navigation: ui_score.cpp; drawing:
 * ui_score_draw.cpp (the whole view, no tab strip); the numbers: score_format.c;
 * the list cursors: score_nav.c; the Bestiary's file: mon_parse.c, score_mon.c;
 * the game side: score_bridge.c (MENU9's table: score_menu9.h). The original
 * is MENU9 (src/MENU/MENU9.PRG/238.c), still reachable with Square on the row
 * (and anywhere in the screen), and the only one when ui_menus is off.
 *
 * Modes:
 *   off       the System list has the focus
 *   home      the summary and the four sections (the original's top menu: its
 *             row is cursorMemories[2] / [3] and menu9CursorMemory.gazetteRow)
 *   titles, records, trials, bestiary
 *             a section's list (the original's pages: the cursor is
 *             menu9CursorMemory's pair, stepped by score_nav.c's rules)
 * Home: Up / Down move (the main menu's change sound), Circle or Right enters
 * (select), Cross back to the System list (leave), Triangle closes, Square the
 * original. A section: Up / Down step (key repeat; sound 4 a move), L1 / R1
 * the previous / next section (wrapping, sound 4), the Bestiary's L2 / R2 a
 * page (theme page_rows), Cross back to Home (sound 6), Triangle closes (sound
 * 6), Square the original. The Bestiary's Circle: an unlocked foe shows the
 * hint "Original screen (Encyclopaedia)" (Square opens MENU9 on its top menu,
 * the Encyclopaedia row selected; the viewer is the original's in pass 1); a
 * locked one plays sound 7.
 * Pointer: hovering highlights (nothing written); a click focuses a row (its
 * canonical pair: score_nav_canon) or enters a section; the wheel steps the
 * list; a right click is Cross.
 *
 * The game's state (MENU9's open through its own _calculateProgress, the cursor
 * memories, the world freeze, the sounds) is the bridge's, through the events
 * below (pc_ui_score_take), drained after every shell tick. */
#ifndef PC_UI_SCORE_H
#define PC_UI_SCORE_H
#include "ui_shell.h"
#include "score_menu9.h"
#include "mon_parse.h"
#ifdef __cplusplus
#include <string>
extern "C" {
#endif

enum { PC_UI_SCORE_OFF, PC_UI_SCORE_HOME, PC_UI_SCORE_TITLES, PC_UI_SCORE_RECORDS, PC_UI_SCORE_TRIALS,
       PC_UI_SCORE_BESTIARY };
/* Mouse kinds (pc_ui_shell_mouse) */
enum { PC_UI_MOUSE_SCORE_SECTION = 240, /* a section: Home's row or the strip's tab (index 0..3) */
       PC_UI_MOUSE_SCORE_ROW,           /* an entry of the shown list (index: the entry) */
       PC_UI_MOUSE_SCORE_PANEL };       /* the cards (a click does nothing) */

/* What the bridge does to the game (score_bridge.c). */
enum { PC_UI_SEV_NONE, PC_UI_SEV_OPEN, /* freeze, _calculateProgress, the snapshot, the select sound */
       PC_UI_SEV_CURSOR,   /* list a (PC_SCORE_LIST_*): its menu9CursorMemory pair = (b, c) */
       PC_UI_SEV_HOME,     /* cursorMemories[2] = a, [3] = 0 (_setRows with 4 rows: Home shown; and
                            * Home's exits, as MAINMENU's list stores its cursor) */
       PC_UI_SEV_GAZETTE,  /* menu9CursorMemory.gazetteRow = cursorMemories[2] + [3] */
       PC_UI_SEV_SFX,      /* vs_main_playSfxDefault(0x7E, a) */
       PC_UI_SEV_MENU_SFX, /* a: 0 leave, 1 select, 2 change (vs_battle_playMenu*Sfx) */
       PC_UI_SEV_CLOSE,    /* the screen closed: the world freeze back */
       PC_UI_SEV_ORIGINAL };/* the original screen next (deliver(9)) */
typedef struct {
    int kind, a, b, c;
} pc_ui_score_event;

enum { PC_UI_SCORE_NAME = 48, PC_UI_SCORE_DESC = 192 };

/* The snapshot of one open (score_bridge.c fills it in place at OPEN: MENU9's statics after
 * _calculateProgress, vs_main_scoredata, MENU9's text decoded, the Records formatted, MON.BIN parsed). */
typedef struct {
    int valid;                           /* filled for this open */
    int tables_ok;                       /* MENU9's text tables have the counts expected */
    unsigned score;                      /* _score */
    int rank;                            /* _characterRank (0 best .. 15) */
    unsigned titles;                     /* scoredata.titles */
    unsigned clears;                     /* _clearCount */
    unsigned map_now, map_best;          /* _mapCompletion, scoredata.mapCompletion */
    unsigned chests_now, chests_best;    /* _openedChestCount, scoredata.openedChestCount */
    unsigned max_chain;
    unsigned kills[6];                   /* scoredata.enemyKills */
    unsigned char cursor[8];             /* vs_battle_menu9CursorMemory after the calculation */
    char section[4][PC_UI_SCORE_NAME];   /* MENU9's menuText: title, gazette, recordTime, encyclopaedia */
    char rank_text[PC_UI_SCORE_NAME + 16];
    char title_name[PC_UI_SCORE_N_TITLES][PC_UI_SCORE_NAME];
    char title_desc[PC_UI_SCORE_N_TITLES][PC_UI_SCORE_DESC];
    char stat_name[PC_UI_SCORE_N_STATS][PC_UI_SCORE_NAME];
    char stat_desc[PC_UI_SCORE_N_STATS][PC_UI_SCORE_DESC];
    char stat_value[PC_UI_SCORE_N_STATS][24]; /* "" with stat_none: MENU9's noneClear */
    unsigned char stat_none[PC_UI_SCORE_N_STATS];
    int trial_has[PC_UI_SCORE_N_TRIALS];   /* bossTimeTrialScores[i][0].time != 0x800000 */
    char trial_name[PC_UI_SCORE_N_TRIALS][PC_UI_SCORE_NAME];
    char trial_desc[PC_UI_SCORE_N_TRIALS][PC_UI_SCORE_DESC];
    char trial_time[PC_UI_SCORE_N_TRIALS][3][16]; /* "HH:MM:SS" or "--:--:--" */
    int trial_round[PC_UI_SCORE_N_TRIALS][3];     /* round + 1 (0: no time) */
    char trial_ref[PC_UI_SCORE_N_TRIALS][16];
    /* MENU9's miscInfo: noneClear, conditionsNotMet, enemyNotDefeated, enemyNameNone, noneBossRush,
     * noneBossRushDesc */
    char misc[PC_UI_SCORE_N_MISC][PC_UI_SCORE_DESC];
    /* the Bestiary (MON.BIN; mon_ok 0: why) */
    int mon_ok;
    char mon_why[64];
    int unlocked;
    struct {
        int zud, cls, unlocked, prev, next;
        char name[PC_UI_SCORE_NAME];
        char desc[2 * PC_UI_SCORE_DESC];
    } mon[PC_MON_ENEMIES];
} pc_ui_score;

/* ---- the bridge's side ---- */
/* The snapshot to fill (at OPEN), then pc_ui_score_model_ready (the focus seeded from it). */
pc_ui_score* pc_ui_score_model_edit(void);
void pc_ui_score_model_ready(void);
const pc_ui_score* pc_ui_score_model(void);
/* The text a Titles row shows (desc 0: the name, 1: the description): the title's own when its bit is set
 * in s->titles, else miscInfo's 0 / 1 (MENU9's locked rows). The drawing and the JSON share it. */
const char* pc_ui_score_title_text(const pc_ui_score* s, int i, int desc);
/* The next event (1), 0 when none. */
int pc_ui_score_take(pc_ui_score_event* e);
/* A soft reset: off, the events dropped (nothing written to the next game). */
void pc_ui_score_forget(void);
/* The screen is open (not off). */
int pc_ui_score_on(void);

/* ---- the shell's side (ui_shell.cpp) ---- */
int pc_ui_score_mode(void);
/* Circle on the System row: Home. */
void pc_ui_score_enter(void);
/* Cross: a section -> Home, Home -> off (the System list). */
void pc_ui_score_back(const pc_ui_input* in);
/* Out of the screen any other way (a tab, the menu closing, Triangle, the original): the cursor stores and
 * CLOSE; nothing when off. */
void pc_ui_score_leave(void);
/* Triangle in a section: its sound (the original's 0x7E / 6) before the shell closes. */
void pc_ui_score_exit_step(const pc_ui_input* in);
/* One tick of input while on. Returns 9 when Square asks for the original (the shell then leaves and
 * hides), else 0. */
int pc_ui_score_tick(const pc_ui_input* in);
/* The pointer: hovering is the drawing's alone (nothing written: focus returns 0); a click. */
int pc_ui_score_mouse_focus(int kind, int index);
int pc_ui_score_mouse_click(int kind, int index);

/* ---- the drawing's view ---- */
typedef struct {
    int mode;            /* PC_UI_SCORE_* */
    int home_row;        /* Home's focus (0..3) */
    int section;         /* the shown section (0..3; Home: home_row) */
    int focus;           /* the section's focused entry */
    int page, row;       /* its cursor pair (the time attack: page = the boss) */
    int hint;            /* the Bestiary's "View model (original)" shows */
} pc_ui_score_view;
void pc_ui_score_view_get(pc_ui_score_view* v);
/* The drawing's pointer targets this frame (JSON "hits", so tests click what is drawn). */
void pc_ui_score_hits(const char* json);

#ifdef __cplusplus
}
/* ",\"score\":{...}" or ",\"score\":null" */
void pc_ui_score_json(std::string& j);
#endif
#endif
