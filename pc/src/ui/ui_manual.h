/* Modern menus: the Quick manual, a sub-screen of the System tab (System >
 * Game > Quick manual). State and navigation: ui_manual.cpp; drawing:
 * ui_manual_draw.cpp; the page files and the original's rules:
 * manual_parse.c; the game side: manual_bridge.c. The original is MENUE
 * (src/MENU/MENUE.PRG/494.c), still reachable with Square on the row (and
 * in the manual), and the only one when ui_menus is off.
 *
 * Modes:
 *   off      the System list has the focus
 *   topics   the rail of the 14 topics has the focus (the original's list:
 *            its 9-row window kept as cursorMemories[4] / [5], Up / Down
 *            wrap on a press only); the card previews the focused topic
 *   reading  a topic's pages (the original's page screen: Up / Down a line,
 *            Square held + Up / Down or Left / Right a page, wrapping on a
 *            press; Start the top)
 * Circle (or Right) reads; Cross steps back (reading -> topics -> the
 * System list); Circle in reading goes back to the topics too (the
 * original's select sound); Triangle closes the menu; Square on the topics
 * (or on the System row) opens the original screen.
 *
 * The game's state (vs_battle_manualDisplayState, the cursor memories, the
 * world freeze, the sounds) is the bridge's, through the events below
 * (pc_ui_manual_take), drained after every shell tick. */
#ifndef PC_UI_MANUAL_H
#define PC_UI_MANUAL_H
#include "ui_shell.h"
#include "manual_parse.h"
#ifdef __cplusplus
#include <string>
extern "C" {
#endif

enum { PC_UI_MANUAL_OFF, PC_UI_MANUAL_LIST, PC_UI_MANUAL_READ };
/* Mouse kinds (pc_ui_shell_mouse) */
enum { PC_UI_MOUSE_MANUAL_TOPIC = 220, /* a topic of the rail (index: the topic) */
       PC_UI_MOUSE_MANUAL_CANVAS,      /* the page in the card while the topics have the focus: read it */
       PC_UI_MOUSE_MANUAL_PAGE,        /* the page row's chevrons: index -1 / +1 */
       PC_UI_MOUSE_MANUAL_SCROLL };    /* the scroll bar's track: index the line */

/* What the bridge does to the game (manual_bridge.c refresh). */
enum { PC_UI_MEV_NONE, PC_UI_MEV_OPEN, /* a = 1: zero the display state (cursor memory off) */
       PC_UI_MEV_LIST_STORE,  /* cursorMemories[4] = a, [5] = b, currentManual = c */
       PC_UI_MEV_SCROLL,      /* scrollPositions[a] = b */
       PC_UI_MEV_SOUND,       /* a: 0 leave, 1 select, 2 change */
       PC_UI_MEV_CLOSE,       /* the manual closed: the world freeze back */
       PC_UI_MEV_ORIGINAL };  /* the original screen next: currentManual = a */
typedef struct {
    int kind, a, b, c;
} pc_ui_manual_event;

/* ---- the bridge's side ---- */
/* Every tick before the shell's: the game's state and the files' line counts (loaded: -1 unreadable, 0 not
 * yet, 1 read). */
void pc_ui_manual_fill(int cursor_memory, int current_manual, const unsigned short scroll[14],
    const int line_count[14], const int loaded[14]);
/* Square held (vs_main_buttonsPreviousState, the page mode) and Start pressed this tick. */
void pc_ui_manual_buttons(int square_held, int start_pressed);
/* The next event (1), 0 when none. */
int pc_ui_manual_take(pc_ui_manual_event* e);
/* A soft reset: off, the events dropped (nothing written to the next game). */
void pc_ui_manual_forget(void);
/* The topic shown (-1: off): the bridge reads its files. */
int pc_ui_manual_focus_topic(void);
/* The shown topic's text for the JSON (the bridge; raw labels, the page's lines as a JSON array). */
void pc_ui_manual_set_text(const char* label, const char* desc, int lines, const char* page_lines_json,
    int sprites, int rules, int hidden, int unknown);
/* What the drawing fitted (k, the text size, words over the size floor), the topic's button-art sprites and
 * whether they are drawn as the device's prompts now (1) or as the game's pixels (0), and its hits (a JSON
 * array). */
void pc_ui_manual_set_draw(float k, float size, int overflow, int art, int art_prompts, const char* hits_json);

typedef struct {
    int mode;        /* PC_UI_MANUAL_* */
    int topic;       /* the focused (topics) or open (reading) topic */
    int row, page;   /* the list's window (cursorMemories[4], [5]) */
    int line;        /* the topic's scroll */
    int line_count;  /* its last scroll position */
    int loaded;      /* its files: -1, 0, 1 */
    int square_held; /* the page mode cue */
} pc_ui_manual_view;
void pc_ui_manual_view_get(pc_ui_manual_view* v);
/* The scroll remembered for topic t (the topics' preview). */
int pc_ui_manual_line_of(int t);

/* ---- the bridge's model, for the drawing ----
 * manual_bridge.c (battle builds) hands its getters over at its first fill; the drawing reads them through
 * pc_ui_manual_source (null: no bridge, nothing to show). */
typedef struct {
    const char* (*name)(int topic);   /* sentence case ("Basic controls") */
    const char* (*desc)(int topic);   /* the topic's line (the original's information box) */
    const pc_ui_manual_topic* (*model)(int topic); /* null until read (never changes once read) */
    const char* (*run_text)(int topic, int run);   /* a word run's UTF-8, "" otherwise */
    int (*hidden)(int topic, int rule, int index); /* a blinking sprite / rule is off now */
    unsigned (*generation)(void);     /* bumped by a soft reset: textures rebuilt */
} pc_ui_manual_source;
void pc_ui_manual_set_source(const pc_ui_manual_source* source);
const pc_ui_manual_source* pc_ui_manual_source_get(void);

/* manual_cache.c (host code; the bridge's helpers): the topics' names from MENUE's menuText, a topic's
 * files (state: 0 not read, 1 read, -1 unreadable), the shown topic's animations and JSON text. */
void pc_ui_manual_cache_names(const unsigned short* menu_text);
void pc_ui_manual_cache_load(int topic, const unsigned char* widths, unsigned nwidths);
int pc_ui_manual_cache_state(int topic);
int pc_ui_manual_cache_line_count(int topic);
void pc_ui_manual_cache_show(int topic, int mode, int line);
void pc_ui_manual_cache_reset(void);
const pc_ui_manual_source* pc_ui_manual_cache_source(void);

/* ---- the shell's side (ui_shell.cpp) ---- */
int pc_ui_manual_mode(void);
/* Circle on the System row: the topics, on the remembered one. */
void pc_ui_manual_enter(void);
/* Cross: reading -> topics (the frame's step first), topics -> off. With Circle in the same frame Circle
 * wins, as in the original: reading -> topics with the select sound, topics -> reading. */
void pc_ui_manual_back(const pc_ui_input* in);
/* Out of the manual any other way (a tab, the menu closing, Triangle, an original screen): the store
 * events and CLOSE; nothing when off. */
void pc_ui_manual_leave(void);
/* Triangle while reading: the frame's step still runs (the original's order) before the shell closes. */
void pc_ui_manual_exit_step(const pc_ui_input* in);
/* One tick of input while on. Returns 10 when Square asks for the original screen (the shell then leaves
 * and hides), else 0. */
int pc_ui_manual_tick(const pc_ui_input* in);
/* Square on the System list's row (not in the manual): the original screen on the game's topic. */
void pc_ui_manual_original(void);
int pc_ui_manual_mouse_focus(int kind, int index);
int pc_ui_manual_mouse_click(int kind, int index);

#ifdef __cplusplus
}
/* ",\"manual\":{...}" or ",\"manual\":null" */
void pc_ui_manual_json(std::string& j);
#endif
#endif
