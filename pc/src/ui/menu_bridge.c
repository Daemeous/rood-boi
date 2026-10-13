/* Modern menus: the game's top-level menu (BATTLE func_800C930C) handed to
 * the shell (ui_shell.cpp) when ui_menus is 1. Battle builds.
 *
 * The src hooks (ROOD_PC, BATTLE 5BF94.c):
 *   func_800C930C(mode != 0)  the top level (re)opens: pc_ui_menu_open
 *   func_800C930C(0)          one frame of the top level: pc_ui_menu_top
 *                             while pc_ui_menu_active (both callers: BATTLE
 *                             func_800C97BC before MAINMENU.PRG is loaded,
 *                             MAINMENU vs_mainMenu_exec after)
 *   func_800C97BC             hides the original rows (pc_ui_menu_hide_rows)
 *
 * The top level opens with func_800C930C(2) when the menu opens (BATTLE
 * func_800CACD0, cursor memory per the game's option) and with
 * func_800C930C(1) when an original screen returns to it (MAINMENU
 * func_800FAA5C, cursor kept). In shell mode the menu rows stay zeroed (the
 * game's memzero runs, the rows are never set up), so nothing of the
 * original top level draws. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "ui_shell.h"
#include <libetc.h>

static int shell_open; /* the shell owns the top level (ticks in func_800C930C(0)) */
static int closing;    /* the shell closed the menu; set until currentMenu is 0x3F */
static int session;    /* ui_menus as it was when this menu session opened (mode 2) */
static int settle;     /* ticks left that ignore the pad after an open (the opening press) */

/* dev_overlay.cpp: Dear ImGui runs (the shell draws through it). Weak: a
 * build without the overlay keeps the original menus. */
extern int pc_overlay_active(void) __attribute__((weak));

static unsigned map_buttons(unsigned pad)
{
    unsigned b = 0;
    if (pad & PADLup) b |= PC_UI_UP;
    if (pad & PADLdown) b |= PC_UI_DOWN;
    if (pad & PADLleft) b |= PC_UI_LEFT;
    if (pad & PADLright) b |= PC_UI_RIGHT;
    if (pad & PADRright) b |= PC_UI_CONFIRM; /* Circle */
    if (pad & PADRdown) b |= PC_UI_BACK;     /* Cross */
    if (pad & PADRup) b |= PC_UI_EXIT;       /* Triangle */
    if (pad & PADRleft) b |= PC_UI_ALT;      /* Square */
    if (pad & PADL1) b |= PC_UI_TAB_PREV;
    if (pad & PADR1) b |= PC_UI_TAB_NEXT;
    if (pad & PADL2) b |= PC_UI_SUB_PREV;
    if (pad & PADR2) b |= PC_UI_SUB_NEXT;
    return b;
}

static pc_ui_commands commands(void)
{
    pc_ui_commands c;
    u_int mask = (u_int)D_800F4EA0; /* the menu's disable mask (func_800CABE0) */
    c.enabled = vs_main_settings.menuFlags & 0x3FF;
    c.mask = mask;
    c.unselectable = ((mask & 0xB7) ? 1u : 0) | ((mask & 0x15F) ? 2u : 0) | ((mask & 7) ? 8u : 0)
        | (vs_battle_getCurrentSceneId() == 0 ? 0x20u : 0);
    return c;
}

/* func_800C930C(mode != 0), after the game cleared the rows: mode 2 = the
 * menu opens (cursor memory per the game's option), mode 1 = back from an
 * original screen (cursor kept). Returns 1 when the shell takes over.
 *
 * func_800CACD0 calls func_800C930C(2) for every menu session that allocates
 * the menu rows, not only the top level: L2 shortcuts (currentMenu 1-4, misc
 * items 4 with vs_battle_shortcutInvoked 5), ability learned (3), save
 * container (7), loot (0xC), func_800CB23C (0xF), script menus. Those never
 * tick func_800C930C(0), so the shell takes only the top level: currentMenu 0
 * (func_800CACD0 stores menuState before allocating; MAINMENU func_800FAA5C
 * stores 0 before func_800C930C(1)) and not a direct entry (D_800F4EA0 0x200,
 * func_800CABE0(2)). */
int pc_ui_menu_open(int mode)
{
    pc_ui_commands c;
    int top = (vs_battle_menuState.currentMenu & 0x3F) == 0 && !((u_int)D_800F4EA0 & 0x200)
        && vs_battle_shortcutInvoked == 0;
    closing = 0;
    if (mode == 2)
        session = pc_ui_menus != 0; /* System > Menus applies from the next open */
    shell_open = top && session && pc_overlay_active && pc_overlay_active();
    if (!shell_open) {
        pc_ui_shell_close();
        return 0;
    }
    c = commands();
    pc_ui_shell_open(&c, mode != 2 || vs_main_settings.cursorMemory);
    settle = 2; /* the press that opened (or returned to) the menu is not the shell's */
    return 1;
}

/* func_800C930C(0) while the shell owns the top level: the game's protocol
 * (0 stay, -1 close, n > 0 open command n - 1). */
int pc_ui_menu_top(void)
{
    pc_ui_input in;
    int r;
    in.pressed = map_buttons(vs_main_buttonsPressed.all);
    in.repeat = map_buttons(vs_main_buttonRepeat) | in.pressed;
    if (settle > 0) {
        --settle;
        in.pressed = in.repeat = 0;
    }
    r = pc_ui_shell_tick(&in);
    if (r > 0) {
        int cmd = r - 1;
        u_int mask = (u_int)D_800F4EA0;
        shell_open = 0;
        vs_battle_submenuStates[0] = cmd; /* the game's cursor (Status etc. read it) */
        vs_battle_playMenuSelectSfx();
        vs_battle_textBoxes[7].state = 0;
        /* The original screen slides its title in from the top-level row,
         * which the shell never set up: set it off screen as the game's
         * shortcut path does (vs_mainMenu_exec, !(arg0 & 0x40)). */
        vs_battle_setMenuItem(cmd, 320, 18, 140, 8,
            (char*)&vs_battle_menuStrings[vs_battle_menuStrings[cmd]])
            ->unselectable
            = (cmd == 0 && (mask & 0xB7)) || (cmd == 1 && (mask & 0x15F));
    } else if (r < 0) {
        vs_battle_playMenuLeaveSfx();
        vs_battle_textBoxes[7].state = 0;
        shell_open = 0;
        closing = 1;
    }
    return r;
}

int pc_ui_menu_active(void) { return shell_open; }

/* func_800C97BC: the original top-level rows stay hidden while the shell
 * owns this menu session, the close frames included. */
int pc_ui_menu_hide_rows(void)
{
    if (closing && (vs_battle_menuState.currentMenu & 0x3F) == 0x3F)
        closing = 0;
    return shell_open || closing;
}
