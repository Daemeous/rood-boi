/* Modern menus, the Quick manual: the game side (battle builds; ui_manual.h).
 *
 * The original (MENUE, src/MENU/MENUE.PRG/494.c) never runs for the native
 * screen; this file does to the game what it does, from the shell's events
 * (ui_manual.cpp), drained after every shell tick (pc_ui_menu_top):
 *   OPEN        _initMenuState: vs_battle_manualDisplayState zeroed without
 *               cursor memory; the world frozen (func_8008A4DC(0), as
 *               vs_menuE_exec's initMenu); the game's cursor on command 9
 *               (vs_battle_submenuStates[0], as deliver sets it); the select
 *               sound (deliver's)
 *   LIST_STORE  the list's memories (58EC.c: cursorMemories[4] / [5]) and
 *               currentManual = their sum (494.c getSelection)
 *   SCROLL      scrollPositions[topic] (494.c showSubMenu, every frame)
 *   SOUND       vs_battle_playMenu{Leave,Select,Change}Sfx (0x7E: 6, 5, 4)
 *   CLOSE       the world back (func_8008A4DC(1), vs_menuE_exec's showMenu)
 *   ORIGINAL    currentManual = the topic, before deliver(10) runs MENUE
 *               (which seeds its list from it)
 * Not touched (the original's own resources, which nothing native uses): the
 * VRAM reservation (tpages 29-31), text box 0, the information box, the
 * background fade, the HF heap buffers. The HUD slide-out is the shell's.
 *
 * The page files and the shown topic's animations are manual_cache.c's
 * (host memory, outside the game's state sections); this file hands it the
 * game's character advances and MENUE's own menuText (compiled in). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "manual_parse.h"
#include "ui_manual.h"
#include <libetc.h>

extern int D_800F190C; /* BATTLE 146C.c: the world runs (func_8008A4DC) */

void pc_ui_manual_bridge_fill(int settling);
void pc_ui_manual_bridge_refresh(void);
void pc_ui_manual_bridge_abandon(void);
void pc_ui_manual_bridge_reset(void);

/* MENUE's menuText (494.c _menuText): 0-13 the topics, 14-27 their lines. */
static u_short menuText[] = {
#include "build/assets/MENU/MENUE.PRG/menuText.vsString"
};

#define TOPICS PC_UI_MANUAL_TOPICS
#define WIDTHS 378 /* vs_battle_characterWidths: two font tables of 189 */

static int owned; /* OPEN applied, CLOSE not yet: the world is frozen by the manual */

static void apply(const pc_ui_manual_event* e)
{
    switch (e->kind) {
    case PC_UI_MEV_OPEN:
        if (e->a)
            vs_main_bzero(&vs_battle_manualDisplayState, sizeof vs_battle_manualDisplayState);
        if (!owned) {
            owned = 1;
            func_8008A4DC(0);
        }
        vs_battle_submenuStates[0] = 9;
        vs_battle_playMenuSelectSfx();
        break;
    case PC_UI_MEV_LIST_STORE:
        D_800F4EE8.cursorMemories[4] = (char)e->a;
        D_800F4EE8.cursorMemories[5] = (char)e->b;
        vs_battle_manualDisplayState.currentManual = (u_short)e->c;
        break;
    case PC_UI_MEV_SCROLL:
        if (e->a >= 0 && e->a < TOPICS)
            vs_battle_manualDisplayState.scrollPositions[e->a] = (u_short)e->b;
        break;
    case PC_UI_MEV_SOUND:
        if (e->a == 0)
            vs_battle_playMenuLeaveSfx();
        else if (e->a == 1)
            vs_battle_playMenuSelectSfx();
        else
            vs_battle_playMenuChangeSfx();
        break;
    case PC_UI_MEV_CLOSE:
        if (owned) {
            func_8008A4DC(1);
            owned = 0;
        }
        break;
    case PC_UI_MEV_ORIGINAL:
        if (e->a >= 0 && e->a < TOPICS)
            vs_battle_manualDisplayState.currentManual = (u_short)e->a;
        break;
    default:
        break;
    }
}

static void drain(void)
{
    pc_ui_manual_event e;
    while (pc_ui_manual_take(&e))
        apply(&e);
}

/* After a drain the world is frozen exactly while the manual is on. `owned` is game state (saved and
 * restored with a snapshot) and the manual's mode is host state (not): a snapshot taken mid-manual and
 * resumed in a new process (the shell closed, no CLOSE ever queued), or one taken outside it and loaded
 * while reading, would leave them apart; this puts the world back in step. */
static void reconcile(void)
{
    int on = pc_ui_manual_focus_topic() >= 0;
    if (owned && !on) {
        func_8008A4DC(1);
        owned = 0;
    } else if (!owned && on) {
        owned = 1;
        func_8008A4DC(0);
    }
}

/* Before the shell's tick: the game's state, the files' counts, the buttons the shell does not map. */
void pc_ui_manual_bridge_fill(int settling)
{
    int line_count[TOPICS], loaded[TOPICS], i;
    int focus = pc_ui_manual_focus_topic();
    pc_ui_manual_set_source(pc_ui_manual_cache_source());
    pc_ui_manual_cache_names(menuText);
    if (focus >= 0)
        pc_ui_manual_cache_load(focus, (const unsigned char*)vs_battle_characterWidths, WIDTHS);
    for (i = 0; i < TOPICS; ++i) {
        line_count[i] = pc_ui_manual_cache_line_count(i);
        loaded[i] = pc_ui_manual_cache_state(i);
    }
    pc_ui_manual_fill(vs_main_settings.cursorMemory, vs_battle_manualDisplayState.currentManual,
        vs_battle_manualDisplayState.scrollPositions, line_count, loaded);
    pc_ui_manual_buttons(!settling && (vs_main_buttonsPreviousState & PADRleft) != 0,
        !settling && (vs_main_buttonsPressed.all & PADstart) != 0);
}

/* After the shell's tick: its events applied, the shown topic read, its animations stepped and its text
 * published for the JSON. */
void pc_ui_manual_bridge_refresh(void)
{
    pc_ui_manual_view v;
    drain();
    reconcile();
    pc_ui_manual_view_get(&v);
    if (v.mode != PC_UI_MANUAL_OFF)
        pc_ui_manual_cache_load(v.topic, (const unsigned char*)vs_battle_characterWidths, WIDTHS);
    pc_ui_manual_cache_show(v.mode != PC_UI_MANUAL_OFF ? v.topic : -1, v.mode, v.line);
}

/* The session ended without its close (menu_bridge.c abandon, after pc_ui_shell_close left the manual):
 * its store events and the world back. */
void pc_ui_manual_bridge_abandon(void)
{
    pc_ui_manual_leave();
    drain();
    if (owned) {
        func_8008A4DC(1);
        owned = 0;
    }
    pc_ui_manual_cache_show(-1, 0, 0);
}

/* A soft reset: the game's memory is the next game's; nothing is written. */
void pc_ui_manual_bridge_reset(void)
{
    pc_ui_manual_forget();
    owned = 0;
    pc_ui_manual_cache_reset();
}
