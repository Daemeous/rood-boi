/* The classic menus' model (menu_model.h): MAINMENU's shared frame copied
 * from the game's records and 58EC.c's statics (pc_classic_mainmenu_read).
 * Battle builds, compiled with the game headers. */
#include "common.h"
#include "../../../pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../pc/src/game/MENU/MAINMENU.PRG/58EC.h"
#include "classic_hook.h"
#include "menu_model.h"
#include <string.h>

extern signed char _loadedSubMenu;           /* 5BF94.c: the sub-menu overlay loaded */
extern u_int* _menuBgUnpackedBuf;            /* MAINMENU's load still unpacking */
extern char _backgroundFadeState;            /* 58EC.c: the background fading in (1) or out */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */

/* vs_battle_menuState.currentMenu & 0x3F (camera_stick.c's names) */
static const char* const SCREENS[16] = { "command", "magic", "arts", "abilities", "items", "status", "map", "data",
    "options", "score", "manual", "menu11", "loot", "setup", "storage", "menu15" };
static const char* const STATES[6] = { "inactive", "shown", "slide_x", "slide_y", "slide_xy", "dismiss" };

#define GLYPH_SPACING 0xFA
#define GLYPH_END 0xE7

/* A row's text as setMenuItem stored it: the glyphs up to its 0xFF end (a spacing keeps its operand). */
static void row_text(const vs_battle_menuItem_t* item, pc_menu_model_row* r)
{
    unsigned char g[PC_MENU_MODEL_GLYPHS + 1];
    int n = 0;
    for (int i = 0; i < (int)sizeof item->text && (u_char)item->text[i] != 0xFF; i++) {
        g[n++] = (u_char)item->text[i];
        if ((u_char)item->text[i] == GLYPH_SPACING && i + 1 < (int)sizeof item->text)
            g[n++] = (u_char)item->text[++i];
    }
    memcpy(r->glyphs, g, (size_t)n);
    r->glyph_count = n;
    g[n] = GLYPH_END;
    pc_ui_text_decode(g, n + 1, r->text, sizeof r->text);
}

/* The raw printer's text (vs_battle_renderTextRaw: plain bytes to its end), printable ASCII kept. */
static void raw_text(const char* s, char* out, int size)
{
    int n = 0;
    for (; s && *s && n < size - 1; s++)
        if ((u_char)*s >= 32 && (u_char)*s < 127)
            out[n++] = *s;
    out[n] = 0;
}

int pc_menu_model_fill(pc_menu_model* m)
{
    pc_classic_mainmenu mm;
    vs_battle_menuItem_t* items = vs_battle_menuItems;
    memset(m, 0, sizeof *m);
    m->menu = (u_char)vs_battle_menuState.currentMenu;
    m->screen = SCREENS[m->menu & 0xF];
    if (!items)
        return 0;
    m->items = 1;
    m->submenu = _loadedSubMenu;
    m->loaded = _loadedSubMenu > 0 || (_loadedSubMenu == 0 && !vs_battle_menuState.loading && !_menuBgUnpackedBuf);
    m->hidden = vs_mainMenu_hideMenu != 0;
    for (int i = 0; i < PC_MENU_MODEL_ROWS; i++) {
        const vs_battle_menuItem_t* item = &items[i];
        pc_menu_model_row* r;
        if (!item->state)
            continue;
        r = &m->rows[m->row_count++];
        r->id = i;
        r->state = item->state;
        r->x = item->x, r->y = item->y, r->w = item->w, r->background = item->backgroundWidth;
        r->target_x = item->targetPosition0, r->target_y = item->targetPosition1;
        r->selected = item->selected, r->unselectable = item->unselectable, r->invert = item->invertGradient;
        r->scrollable = item->isScrollable, r->gradient = item->gradientState, r->anim = item->animationStep;
        r->button = item->buttonId, r->icon = item->rowIcon, r->outset = item->outsetIcon;
        r->material = item->material, r->color = item->fontColor;
        r->count = item->count, r->max = item->max, r->page = item->itemPage;
        row_text(item, r);
        raw_text(item->subText, r->sub, sizeof r->sub);
    }
    pc_classic_mainmenu_read(&mm);
    m->cursor.row = mm.row, m->cursor.page = mm.page, m->cursor.sub_menu = mm.sub_menu;
    m->cursor.row_info = mm.row_info, m->cursor.rows = mm.row_count, m->cursor.list_open = mm.list_open;
    m->cursor.list_row = vs_mainMenu_itemsListRow, m->cursor.row_offset = mm.row_offset;
    if (mm.sub_menu * 2 + 1 < (int)sizeof vs_menu_frameworkMenuMemory.cursorMemories) {
        m->cursor.memory_row = vs_menu_frameworkMenuMemory.cursorMemories[mm.sub_menu * 2];
        m->cursor.memory_page = vs_menu_frameworkMenuMemory.cursorMemories[mm.sub_menu * 2 + 1];
    }
    m->command = mm.command, m->command_next = mm.command_next;
    m->fade = vs_mainMenu_backgroundFadeStep, m->fade_in = _backgroundFadeState != 0;
    if (vs_battle_textBoxes[7].state > 0 && vs_battle_textBoxes[7].string) {
        m->info_shown = 1;
        pc_ui_text_decode((const u_char*)vs_battle_textBoxes[7].string, 200, m->info, sizeof m->info);
    }
    return 1;
}

const char* pc_menu_model_state_name(int state) { return state >= 0 && state < 6 ? STATES[state] : "?"; }
