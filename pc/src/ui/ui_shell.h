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
/* The shell is drawn and the pointer is over it (the game picture, which its
 * backdrop covers): the editor's viewport mouse (fly, pick, orbit) leaves
 * the pointer to the shell, as io.WantCaptureMouse does for ImGui windows. */
int pc_ui_shell_wants_mouse(void);
/* Tests (RPC ui_menu {pointer}): the next drawn frame takes this pointer, in
 * layout units (1280 or 960 x 720), as moved there, instead of ImGui's; click
 * / right: a button went down; wheel in notches (positive = up); down: the
 * left button is held (a drag over the Ashley model turns it: pointers sent
 * with down, the first inside the model, then moved). */
void pc_ui_draw_pointer(float x, float y, int click, int right, float wheel, int down);
/* The Ashley screen's model column in layout units (x, y, w, h), for the
 * current aspect (pc_gte_widescreen): where ashley_bridge.c centres the model. */
void pc_ui_draw_model_column(float out[4]);
/* The theme's ground colour (ui_theme.cpp), 0..255 per channel. */
void pc_ui_theme_ground_rgb(int* r, int* g, int* b);


/* Read-only views for drawing (ui_draw.cpp). */
int pc_ui_shell_tab(void);
int pc_ui_shell_tab_count(void);
const char* pc_ui_shell_tab_title(int tab);
int pc_ui_shell_row(int i, const char** label, const char** help, const char** value, int* enabled,
    const char** reason);
int pc_ui_shell_focus(void);
/* What the current tab shows: PC_UI_SCREEN_COMMANDS (rows above), or a native
 * screen: PC_UI_SCREEN_EQUIPMENT, PC_UI_SCREEN_ASHLEY. */
enum { PC_UI_SCREEN_COMMANDS = 0, PC_UI_SCREEN_EQUIPMENT = 1, PC_UI_SCREEN_ASHLEY = 2 };
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
    PC_UI_MOUSE_QUICK, PC_UI_MOUSE_BUTTON
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

#ifdef __cplusplus
}
#endif
#endif
