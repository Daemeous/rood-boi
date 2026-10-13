/* Modern menus (pc/docs/Modern_Menus.md): the shell between the game's menu
 * (menu_bridge.c, battle builds), the drawing (ui_draw.cpp) and the RPC. */
#ifndef PC_UI_SHELL_H
#define PC_UI_SHELL_H
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
/* Editor: the Menus row of Graphics > 2D / UI (ImGui window open). */
void pc_ui_menus_editor_row(void);
/* Drawing, from pc_overlay_frame (ImGui frame open). */
void pc_ui_draw_frame(float dt);


/* Read-only views for drawing (ui_draw.cpp). */
int pc_ui_shell_tab(void);
int pc_ui_shell_tab_count(void);
const char* pc_ui_shell_tab_title(int tab);
int pc_ui_shell_row(int i, const char** label, const char** help, const char** value, int* enabled,
    const char** reason);
int pc_ui_shell_focus(void);

#ifdef __cplusplus
}
#endif
#endif
