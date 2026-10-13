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
#include "../../../src/MENU/MAINMENU.PRG/58EC.h"
#include "ui_shell.h"
#include <libetc.h>

static int shell_open; /* the shell owns the top level (ticks in func_800C930C(0)) */
static int closing;    /* the shell closed the menu; set until currentMenu is 0x3F */
static int session;    /* ui_menus as it was when this menu session opened (mode 2) */
static int settle;     /* ticks left that ignore the pad after an open (the opening press) */
static int hud_hidden; /* the field HUD is slid out for the shell (D_800EB9AF) */
static char hud_saved; /* D_800EB9AF before the shell set it */
static int held;       /* a shell result waiting for a model reload (pc_ui_menu_top), 0 none */
static unsigned frames;    /* battle frames (pc_ui_menu_heartbeat) */
static unsigned top_frame; /* frames at the last open or top-level tick */

/* MAINMENU.PRG is resident and drives the top level (vs_battle_loadMenuPrg,
 * 5BF94.c): BATTLE func_800C97BC ticks the top level itself until MAINMENU
 * (submenu 0, overlay slot 1) has loaded, the frame after that frees the
 * unpacked menu background and runs vs_mainMenu_initInventory, and only
 * then calls vs_mainMenu_exec (a later submenu in slot 2 keeps MAINMENU). */
extern signed char _loadedSubMenu;
extern u_int* _menuBgUnpackedBuf;
static int mainmenu_resident(void)
{
    return _loadedSubMenu > 0
        || (_loadedSubMenu == 0 && !vs_battle_menuState.loading && _menuBgUnpackedBuf == NULL);
}

/* The field HUD (HP/MP/RISK, limb silhouette; BATTLE func_800CA2DC) slides
 * out while D_800EB9AF is set, as the game's own screens do for themselves:
 * the shell's backdrop then shows only the dimmed scene. */
static void hide_hud(void)
{
    if (!hud_hidden) {
        hud_saved = D_800EB9AF;
        D_800EB9AF = 1;
        hud_hidden = 1;
    }
}
static void show_hud(void)
{
    if (hud_hidden) {
        D_800EB9AF = hud_saved;
        hud_hidden = 0;
    }
}

/* ashley_bridge.c: the Ashley tab's game side */
void pc_ui_ashley_open(void);
void pc_ui_ashley_fill(void);
void pc_ui_ashley_model_open(int fresh);
void pc_ui_ashley_model_tick(int resident, int held);
void pc_ui_ashley_model_close(void);
void pc_ui_ashley_model_forget(void);

/* weapon_slots.c: the menu's model reload */
void pc_weapon_slots_left_battle(void);

/* The session ends without the game's close path (soft reset, the game
 * leaving the top level, the top level no longer ticked): the shell hides
 * (holding cleared), a held result is dropped, the HUD flag goes back only if
 * it is still the one the shell set (whatever set it since owns it now). */
static void abandon(void)
{
    pc_ui_ashley_model_close(); /* the world back before anything else runs */
    pc_ui_shell_close();
    if (hud_hidden && D_800EB9AF == 1)
        D_800EB9AF = hud_saved;
    hud_hidden = 0;
    shell_open = closing = held = settle = 0;
}

/* Every battle frame (BATTLE vs_battle_exec's pad read, through
 * weapon_slots.c pc_weapon_slots_process_pad, before the menu runs): a
 * session whose top level has not been ticked for STALE_FRAMES frames, or
 * whose menu is no longer at the top level, has ended abnormally. */
#define STALE_FRAMES 4
void pc_ui_menu_heartbeat(void)
{
    ++frames;
    if (!shell_open && !held && !hud_hidden && !pc_ui_shell_drawn())
        return;
    if (frames - top_frame < STALE_FRAMES && (vs_battle_menuState.currentMenu & 0x3F) == 0)
        return;
    abandon();
}

/* boot_sdk.c: the game jumps to the title or a new battle (vs_main_resetGame,
 * the soft reset; vs_overlay_jumpToTitle/Battle). The battle loop is gone:
 * the session and a queued model reload's finish are not the next scene's. */
void pc_ui_menu_reset(void)
{
    pc_ui_ashley_model_forget(); /* the heap is the next game's: nothing to free */
    if (shell_open || held || hud_hidden || pc_ui_shell_drawn())
        abandon();
    pc_weapon_slots_left_battle();
}

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
    held = 0;
    if (mode == 2)
        session = pc_ui_menus != 0; /* System > Menus applies from the next open */
    shell_open = top && session && pc_overlay_active && pc_overlay_active();
    if (!shell_open) {
        pc_ui_ashley_model_close();
        pc_ui_shell_close();
        show_hud();
        return 0;
    }
    hide_hud();
    top_frame = frames;
    pc_ui_ashley_open();
    pc_ui_ashley_model_open(mode == 2);
    c = commands();
    pc_ui_shell_open(&c, mode != 2 || vs_main_settings.cursorMemory);
    settle = 2; /* the press that opened (or returned to) the menu is not the shell's */
    return 1;
}

/* equip_bridge.c: the Equipment screen's game side */
int pc_ui_equip_begin(int resident);
void pc_ui_equip_requests(void);
int pc_ui_equip_busy(void);

/* The shell's result (-1 or n > 0) for the game: its sounds and the row the
 * original screen slides its title from. */
static int deliver(int r)
{
    if (r != 0) {
        pc_ui_ashley_model_close(); /* before an original screen builds its own clone */
        show_hud(); /* original screens and the field manage the HUD themselves */
    }
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

/* func_800C930C(0) while the shell owns the top level: the game's protocol
 * (0 stay, -1 close, n > 0 open command n - 1).
 *
 * Per tick: the Ashley snapshot (pc_ui_ashley_fill) and the Equipment
 * snapshot (pc_ui_equip_begin; weapon and shield
 * equips wait until MAINMENU is resident, see mainmenu_resident) before the
 * shell's tick, its requests after it (drained
 * every tick, also on the tick the shell closed), then Ashley's model
 * (pc_ui_ashley_model_tick: drawn while the Ashley tab shows, torn down
 * otherwise, so before any result is delivered).
 *
 * Busy hand-off: while a weapon/shield model reload of the Equipment screen
 * runs, no original screen may start and the menu may not close (the load
 * queue must drain and weapon_slots.c must run its finish, which it does from
 * its per-frame hook whether or not this runs).
 * The shell has already hidden itself when it returns -1 or n > 0, so that
 * result is held: the top level stays (returns 0, shell_open kept so the
 * original rows stay hidden and this function keeps being called, the HUD
 * stays out, the pad is ignored) and the held result is delivered on the
 * first tick the load is over. Meanwhile the shell keeps drawing with a
 * loading indicator (pc_ui_shell_set_holding), so the screen does not drop to
 * the dimmed scene for the ~30 vsyncs of the load. A session that ends any
 * other way (soft reset, the game leaving the top level) is cleaned up by
 * pc_ui_menu_heartbeat and pc_ui_menu_reset. */
int pc_ui_menu_top(void)
{
    pc_ui_input in;
    int r;
    int busy;
    top_frame = frames;
    busy = pc_ui_equip_begin(mainmenu_resident());
    pc_ui_ashley_fill();
    if (held) {
        pc_ui_ashley_model_tick(0, 1);
        vs_mainMenu_setMenuCommand(menuActionNone);
        if (busy)
            return 0;
        r = held;
        held = 0;
        pc_ui_shell_set_holding(0);
        return deliver(r);
    }
    in.pressed = map_buttons(vs_main_buttonsPressed.all);
    in.repeat = map_buttons(vs_main_buttonRepeat) | in.pressed;
    if (settle > 0) {
        --settle;
        in.pressed = in.repeat = 0;
    }
    r = pc_ui_shell_tick(&in);
    pc_ui_equip_requests();
    /* Ashley's model: built, drawn or torn down for what the shell shows now
     * (a result hid the shell: torn down here, before delivery) */
    pc_ui_ashley_model_tick(mainmenu_resident(), 0);
    /* MAINMENU sets its "COMMAND" title every frame before calling the top
     * level; the shell has its own tabs, so the title slides away. */
    vs_mainMenu_setMenuCommand(menuActionNone);
    if (r != 0 && pc_ui_equip_busy()) {
        held = r;
        pc_ui_shell_set_holding(1); /* the shell keeps drawing (loading) until delivery */
        return 0;
    }
    return deliver(r);
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
