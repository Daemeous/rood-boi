/* The classic menus' model (plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md
 * §2.3): MAINMENU's shared frame as plain values, copied (never pointers),
 * for rood.game.menu() (pc/src/lua/lua_api_game.cpp). Filled by
 * menu_model.c in battle builds; the Lua side links it weakly (nil without).
 *
 *   menu, screen   vs_battle_menuState.currentMenu and its name (the
 *                  camera_stick.c names: "command", "magic", ..., "options")
 *   loaded         MAINMENU resident (its routines callable); submenu the
 *                  sub-menu overlay loaded (_loadedSubMenu, 0 none)
 *   rows           vs_battle_menuItems' live rows (state != 0), by id 0..39:
 *                  the record's fields; text decoded (UTF-8) and its glyphs
 *                  as the game set them (the codes up to the 0xFF end, 0xFA
 *                  spacings with their operand); sub the raw printer's text
 *   cursor         58EC.c's list statics (pc_classic_mainmenu) and the
 *                  sub-menu's cursor memory (vs_menu_frameworkMenuMemory.cursorMemories)
 *   info           text box 7 (vs_mainmenu_setInformationMessage), decoded,
 *                  while it is up
 *   command        the title sprite's current and next (_renderMenuCommand)
 *   fade           the background's fade step (0..16) and its direction
 * Read-only: the model never writes the game. */
#ifndef PC_CLASSIC_MENU_MODEL_H
#define PC_CLASSIC_MENU_MODEL_H
#ifdef __cplusplus
extern "C" {
#endif

#define PC_MENU_MODEL_ROWS 40
#define PC_MENU_MODEL_GLYPHS 32
#define PC_MENU_MODEL_TEXT 128

typedef struct {
    int id, state;
    int x, y, w, background, target_x, target_y;
    int selected, unselectable, invert, scrollable, gradient, anim;
    int button, icon, outset, material, color;
    int count, max, page;
    int glyph_count;
    unsigned char glyphs[PC_MENU_MODEL_GLYPHS];
    char text[PC_MENU_MODEL_TEXT], sub[PC_MENU_MODEL_TEXT];
} pc_menu_model_row;

typedef struct {
    int menu, loaded, submenu, items, hidden;
    const char* screen;
    int row_count;
    pc_menu_model_row rows[PC_MENU_MODEL_ROWS];
    struct {
        int row, page, sub_menu, row_info, rows, list_open, list_row, row_offset, memory_row, memory_page;
    } cursor;
    int command, command_next, fade, fade_in;
    int info_shown;
    char info[256];
} pc_menu_model;

/* Fills `m` from the game now; 0 (and `m` cleared) when there is no menu frame
 * (vs_battle_menuItems not allocated). */
int pc_menu_model_fill(pc_menu_model* m);
/* A row state's name: "inactive", "shown", "slide_x", "slide_y", "slide_xy", "dismiss". */
const char* pc_menu_model_state_name(int state);

#ifdef __cplusplus
}
#endif
#endif
