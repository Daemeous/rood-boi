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
 *   func_800CAB40             a target cancelled: back to the top level
 *                             instead of the original screen when the shell
 *                             handed the action over (pc_ui_menu_cast_cancelled)
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

/* The modern menus don't know the bigger bag (VS_BIG_INVENTORY) yet: its builds keep the
 * original menus whatever System > Menus says. */
#ifdef VS_BIG_INVENTORY
#define MODERN_MENUS 0
#else
#define MODERN_MENUS (pc_ui_menus != 0)
#endif

static int shell_open; /* the shell owns the top level (ticks in func_800C930C(0)) */
static int closing;    /* the shell closed the menu; set until currentMenu is 0x3F */
static int session;    /* ui_menus as it was when this menu session opened (mode 2) */
static int settle;     /* ticks left that ignore the pad after an open (the opening press) */
static int hud_hidden; /* the field HUD is slid out for the shell (D_800EB9AF) */
static char hud_saved; /* D_800EB9AF before the shell set it */
static int held;       /* a shell result waiting for a model reload (pc_ui_menu_top), 0 none */
static unsigned frames;    /* battle frames (pc_ui_menu_heartbeat) */
static unsigned top_frame; /* frames at the last open or top-level tick */
static int cast_shell; /* the action in the game's hands came from the shell (close_for_cast) */

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

/* abilities_bridge.c: the Abilities screen's game side */
void pc_ui_abilities_fill(int mainmenu);
void pc_ui_abilities_refresh(void);
int pc_ui_abilities_cast_taken(void);
void pc_ui_abilities_cast(int go);

/* magick_bridge.c: the Magick screen's game side */
void pc_ui_magick_fill(int mainmenu);
void pc_ui_magick_refresh(void);
int pc_ui_magick_cast_taken(void);
void pc_ui_magick_cast(int go);

/* items_bridge.c: the Items screen's game side */
void pc_ui_items_session(int fresh);
void pc_ui_items_fill(int mainmenu);
void pc_ui_items_refresh(void);
int pc_ui_items_cast_taken(void);
void pc_ui_items_cast(int go);

/* workshop_bridge.c: the Items tab's Workshop sub-tab */
void pc_ui_workshop_fill(int mainmenu);
void pc_ui_workshop_refresh(void);

/* ashley_bridge.c: the Ashley tab's game side */
void pc_ui_ashley_open(void);
void pc_ui_ashley_fill(void);
void pc_ui_ashley_model_open(int fresh);
void pc_ui_ashley_model_tick(int resident, int held);
void pc_ui_ashley_model_close(void);
void pc_ui_ashley_model_forget(void);

/* loot_bridge.c: the Loot screen's game side (direct entry 12) */
int pc_ui_loot_begin(void);
int pc_ui_loot_frame(const pc_ui_input* in);
void pc_ui_loot_abandon(int reset);

/* map_bridge.c: the Map frame around the game's map screen (MENU5) */
void pc_ui_map_arm(void);
int pc_ui_map_on(void);
int pc_ui_map_end(void);
void pc_ui_map_forget(void);

/* data_bridge.c: the native Data screen around the game's save / load screen (MENU7) */
void pc_ui_data_arm(void);
int pc_ui_data_on(void);
int pc_ui_data_end(void);
void pc_ui_data_forget(void);

/* manual_bridge.c: the System tab's Quick manual (ui_manual.h) */
void pc_ui_manual_bridge_fill(int settling);
void pc_ui_manual_bridge_refresh(void);
void pc_ui_manual_bridge_abandon(void);
void pc_ui_manual_bridge_reset(void);

/* A direct entry the shell runs (pc_ui_direct_tick): its menu (0 none),
 * ticks left that ignore the pad. */
static struct {
    int menu;
    int settle;
} direct;

/* The field HUD back after a direct session (its end or an abandon): only if
 * the flag is still the one the shell set (whatever set it since owns it
 * now), as abandon() does for the top level. */
static void direct_show_hud(void)
{
    if (hud_hidden && D_800EB9AF == 1)
        D_800EB9AF = hud_saved;
    hud_hidden = 0;
}

/* The session ends without its end: the game left its menu (reset 0: the
 * game heap stays, the bridge gives its allocation back) or the game was
 * reset (1: the heap is the next game's). */
static void direct_abandon(int reset)
{
    if (direct.menu == 12)
        pc_ui_loot_abandon(reset);
    direct.menu = 0;
    direct_show_hud();
}

/* The HUD flag D_800EB9AF as the game has it (the shell's slide-out aside):
 * loot_bridge.c's Organize panel decides on it as MENUB does. */
char pc_ui_menu_hud_game(void) { return hud_hidden ? hud_saved : D_800EB9AF; }

/* After the game's own HUD calls of a direct session (loot_bridge.c, the
 * Organize panel closing: func_800CB654(0), D_800EB9B0 0): what they set is
 * the game's value now, and the HUD stays out while the shell shows. */
void pc_ui_menu_hud_keep(void)
{
    if (hud_hidden && D_800EB9AF != 1) {
        hud_saved = D_800EB9AF;
        D_800EB9AF = 1;
        D_800EB9B0 = 0x200000;
    }
}

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
    pc_ui_manual_bridge_abandon(); /* the Quick manual's store and its world freeze back */
    if (hud_hidden && D_800EB9AF == 1)
        D_800EB9AF = hud_saved;
    hud_hidden = 0;
    shell_open = closing = held = settle = cast_shell = 0;
}

/* Every battle frame (BATTLE vs_battle_exec's pad read, through
 * weapon_slots.c pc_weapon_slots_process_pad, before the menu runs): a
 * session whose top level has not been ticked for STALE_FRAMES frames, or
 * whose menu is no longer at the top level, has ended abnormally. A direct
 * session ends abnormally only when the game left its menu: one that is not
 * ticked for a while (the after-battle or chest flow not calling the menu
 * on some frames) is the same session when the ticks come back (abandoning
 * it would let its next tick, *state still 0, start it over: the drops
 * copied again and the first allocation lost). */
#define STALE_FRAMES 4
void pc_ui_menu_heartbeat(void)
{
    ++frames;
    /* the game left a direct session's menu */
    if (direct.menu && (vs_battle_menuState.currentMenu & 0x3F) != direct.menu)
        direct_abandon(0);
    if (direct.menu)
        return; /* the top level is not this session's (its HUD flag is the direct session's) */
    /* the Data frame ends once the game left its Data screen (Close: 7, then
     * 0x7F; anything else): then the HUD the shell held out comes back as an
     * abandoned session's (below). While it runs the HUD stays out (deliver). */
    if (pc_ui_data_on()) {
        if ((vs_battle_menuState.currentMenu & 0x3F) == 7)
            return;
        pc_ui_data_end();
    }
    /* the Map frame ends once the game left its map screen (Triangle: 6, then
     * 0x7F; anything else). A stale stamp never ends it: it only stops the
     * drawing (ui_map.cpp pc_ui_map_visible). */
    if (pc_ui_map_on() && (vs_battle_menuState.currentMenu & 0x3F) != 6)
        pc_ui_map_end();
    /* the shell's action ended (its target confirmed, or no target): the
     * game frees the menu's rows (func_800CB114) only then, a cancel keeps
     * them */
    if (cast_shell && vs_battle_menuItems == 0)
        cast_shell = 0;
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
    pc_ui_manual_bridge_reset(); /* nothing written: the game's memory is the next game's */
    pc_ui_ashley_model_forget(); /* the heap is the next game's: nothing to free */
    pc_ui_map_forget();
    pc_ui_data_forget(); /* (a load already ended it before its loading screen: 260.c) */
    if (direct.menu)
        direct_abandon(1);
    if (shell_open || held || hud_hidden || pc_ui_shell_drawn())
        abandon();
    pc_weapon_slots_left_battle();
    cast_shell = 0;
}

/* dev_overlay.cpp: Dear ImGui runs (the shell draws through it). Weak: a
 * build without the overlay keeps the original menus. */
extern int pc_overlay_active(void) __attribute__((weak));

static unsigned map_buttons(unsigned pad);
/* map_bridge.c: the Map frame's area list reads the pad as the shell does */
unsigned pc_ui_map_buttons(unsigned pad) { return map_buttons(pad); }

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
    /* back from the map (Cross, or a tab clicked on its frame: that tab) */
    int map_tab = pc_ui_map_end();
    /* back from Data (Cross, or a tab clicked / L1 / R1 on its frame: that
     * tab; else the System list on Data, kept as a return keeps it) */
    int data_tab = pc_ui_data_end();
    closing = 0;
    held = 0;
    if (mode == 2)
        session = MODERN_MENUS; /* System > Menus applies from the next open */
    /* only a return (mode 1: a cancelled target's func_800CAB40, an original
     * screen's func_800FAA5C) can follow a shell hand-off; any other open is a
     * new menu session with no action of the last one out */
    if (mode != 1)
        cast_shell = 0;
    shell_open = top && session && pc_overlay_active && pc_overlay_active();
    if (!shell_open) {
        pc_ui_ashley_model_close();
        pc_ui_shell_close();
        pc_ui_manual_bridge_abandon(); /* the close's manual events (store, world back) applied now */
        show_hud();
        return 0;
    }
    hide_hud();
    top_frame = frames;
    pc_ui_ashley_open();
    pc_ui_ashley_model_open(mode == 2);
    c = commands();
    pc_ui_shell_open(&c, mode != 2 || vs_main_settings.cursorMemory);
    if (map_tab >= 0)
        pc_ui_shell_set_tab(map_tab);
    if (data_tab >= 0)
        pc_ui_shell_set_tab(data_tab);
    pc_ui_abilities_fill(mainmenu_resident()); /* the shell may open on the Abilities screen */
    pc_ui_magick_fill(mainmenu_resident());    /* or on Magick */
    pc_ui_items_session(mode == 2);            /* a new menu: the next Items screen is an entry */
    pc_ui_items_fill(mainmenu_resident());     /* or on Items */
    pc_ui_workshop_fill(mainmenu_resident());  /* (its Workshop) */
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
    /* the save / load screen (MENU7) under the native Data frame (data_bridge.c) */
    int data = r == 7 && session && pc_overlay_active && pc_overlay_active();
    if (r != 0) {
        pc_ui_ashley_model_close(); /* before an original screen builds its own clone */
        /* original screens and the field manage the HUD themselves; the Data
         * frame keeps it out (MENU7 leaves it as the menu had it: the frame's
         * backdrop would show it; back with the session, pc_ui_menu_heartbeat) */
        if (!data)
            show_hud();
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
        /* the map screen (MENU5): the shell draws its frame around it
         * (map_bridge.c; MENU5 manages the HUD flag itself, as on the
         * original path) */
        if (r == 6 && session && pc_overlay_active && pc_overlay_active())
            pc_ui_map_arm();
        if (data)
            pc_ui_data_arm();
    } else if (r < 0) {
        vs_battle_playMenuLeaveSfx();
        vs_battle_textBoxes[7].state = 0;
        shell_open = 0;
        closing = 1;
    }
    return r;
}

/* A break art accepted (pc_ui_abilities_cast wrote MENU1's hand-off): the
 * menu ends as MENU1 ends it for a cast, not through the top level's -1.
 * MENU1 returns to MAINMENU with currentMenu 2, whose func_800FAA5C(2) gives
 * back the menu's resources (func_8007E0A8(26, 3, 6) unless a shortcut
 * opened it) and sets 0x7F: BATTLE func_800C97BC then hands D_800F4E98 to
 * func_800735F8 (the art) on the next frame. The top level's -1 would go
 * through 0x1F and MAINMENU's _quitDelay instead, whose counter the
 * original never advances on a cast: a target cancelled reopens the menu
 * (func_800CAB40) without reloading MAINMENU, so the next close would wait
 * for a count it has passed. The select sound (MENU1's confirmed row), the
 * shell's own cleanup as deliver's, then 0 (no result: currentMenu is set;
 * MAINMENU vs_mainMenu_exec then skips that frame's render as after MENU1's
 * return, ROOD_PC in C48.c). Only while MAINMENU is resident (the bridge
 * refuses a cast before): 0x7F skips vs_battle_loadMenuPrg, whose pending
 * load and background release would be left behind.
 *
 * The hand-off's returnState names the original screen (MENU1 for an art);
 * cast_shell records that the shell made it instead, so a target cancelled
 * reopens the shell where it was (pc_ui_menu_cast_cancelled). Every
 * hand-off of the shell (Break Arts, Magick's spells and teleport; Items with
 * unk2 8) ends the menu through here; target 0 for a hand-off without a
 * target (teleport: MENU0's exit is the same 0x7F, nothing to cancel). */
static int close_for_cast(int target)
{
    pc_ui_ashley_model_close();
    show_hud();
    vs_battle_playMenuSelectSfx();
    vs_battle_textBoxes[7].state = 0;
    shell_open = 0;
    closing = 1;
    if (D_800F4FDB == 0)
        func_8007E0A8(26, 3, 6);
    vs_battle_menuState.currentMenu = 0x7F;
    cast_shell = target;
    return 0;
}

/* BATTLE func_800CAB40, a target cancelled (not the misc items shortcut's):
 * 1 when the action came from the shell, which then gets the top level
 * back instead of the hand-off's original screen: func_800CAB40 sets
 * currentMenu 0 and calls func_800C930C(1), as an original screen does when
 * it returns there (MAINMENU func_800FAA5C), and pc_ui_menu_open(1) shows
 * the shell on the tab, sub-tab and row it closed on (a return keeps them).
 * The rest is the game's own cancel: the menu's resources back
 * (func_8007DFF0, the counterpart of close_for_cast's func_8007E0A8), its
 * rows still allocated (func_800CB114 runs only when an action ends),
 * MAINMENU resident (nothing loaded while targeting: vs_battle_loadMenuPrg(0)
 * passes at once), the disable mask D_800F4EA0 kept from the menu's open as
 * for any return to the top level. The flag is spent here either way. */
int pc_ui_menu_cast_cancelled(void)
{
    int from_shell = cast_shell && vs_battle_menuItems != 0
        && (vs_battle_menuState.currentMenu & 0x3F) == 0x3F;
    cast_shell = 0;
    return from_shell;
}

/* func_800C930C(0) while the shell owns the top level: the game's protocol
 * (0 stay, -1 close, n > 0 open command n - 1).
 *
 * Per tick: the Ashley snapshot (pc_ui_ashley_fill), the Abilities snapshot
 * (pc_ui_abilities_fill, while that screen shows) and the Equipment snapshot
 * (pc_ui_equip_begin; weapon and shield equips and break arts wait until
 * MAINMENU is resident, see mainmenu_resident) before the shell's tick, its
 * requests after it (drained every tick, also on the tick the shell closed;
 * pc_ui_abilities_refresh for a tab switched to Abilities), then Ashley's model
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
    int resident = mainmenu_resident();
    top_frame = frames;
    busy = pc_ui_equip_begin(resident);
    pc_ui_ashley_fill();
    pc_ui_abilities_fill(resident);
    pc_ui_magick_fill(resident);
    pc_ui_items_fill(resident);
    pc_ui_workshop_fill(resident);
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
    pc_ui_manual_bridge_fill(settle > 0); /* the Quick manual: the game's state, Square held and Start */
    if (settle > 0) {
        --settle;
        in.pressed = in.repeat = 0;
    }
    r = pc_ui_shell_tick(&in);
    pc_ui_manual_bridge_refresh(); /* its events (an ORIGINAL before deliver(10) runs MENUE) */
    pc_ui_equip_requests();
    pc_ui_abilities_refresh();
    pc_ui_magick_refresh();
    pc_ui_items_refresh();
    pc_ui_workshop_refresh();
    /* a break art, a spell, a teleport or an item's Use was accepted (refused while a result
     * is out, a model reload runs or MAINMENU is not resident, so never held;
     * one per tick, the bridges refuse a second): handed to the game and the
     * menu closes for it. A result on the same tick (it cannot happen: the
     * bridges refuse once the shell has hidden) drops it. */
    if (pc_ui_abilities_cast_taken() || pc_ui_magick_cast_taken() || pc_ui_items_cast_taken()) {
        int teleport = pc_ui_magick_cast_taken() == 2;
        pc_ui_abilities_cast(r == 0);
        pc_ui_magick_cast(r == 0);
        pc_ui_items_cast(r == 0);
        if (r == 0) {
            pc_ui_shell_close();
            pc_ui_ashley_model_tick(resident, 0); /* torn down: the shell is gone */
            vs_mainMenu_setMenuCommand(menuActionNone);
            return close_for_cast(!teleport);
        }
    }
    /* Ashley's model: built, drawn or torn down for what the shell shows now
     * (a result hid the shell: torn down here, before delivery) */
    pc_ui_ashley_model_tick(resident, 0);
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

/* MAINMENU vs_mainMenu_exec, the direct entries (D_800F4EA0 & 0x200; C48.c,
 * ROOD_PC, once the submenu has loaded): the game opened one of its menus
 * straight (no top level): 12 the loot after a battle or at a chest (MENUB).
 * Returns 1 when the shell runs it, with *result the submenu's protocol (0
 * stay, 1 done: vs_mainMenu_exec's func_800FAA5C then ends the menu as after
 * the original); 0 leaves the original to run (ui_menus off when the session
 * opened, no overlay, an entry the shell has no screen for).
 *
 * The session is the shell's from its first tick (*state 0, as the
 * original's start) to its end; ui_menus is read only there (a session never
 * changes hands). *state stays 0 while the shell runs it (the original's
 * would restart cleanly from there) and is 0 at the end as the original
 * leaves it. Everything game-side is the entry's bridge (loot_bridge.c),
 * which does what the original's submenu does to the game's state, including
 * its world freeze and HUD (MENUB's loot list touches neither: only its
 * Organize inventory panel does, see loot_bridge.c). The shell slides the
 * field HUD out while it shows, as for the top level (hide_hud: presentation
 * only, the flag back at the end; the bridge decides on the game's own value,
 * pc_ui_menu_hud_game; back at the end only while it is still the shell's,
 * direct_show_hud). A session whose menu the game left is abandoned
 * (pc_ui_menu_heartbeat; one not ticked for a while resumes as it was), a
 * soft reset drops it (pc_ui_menu_reset). Room for
 * more entries: 13 (MENUC, the workshop) and 14 (MENUD) get a bridge each and
 * a case here. */
#define DIRECT_SETTLE 8 /* the press that opened the menu (and the original's slide-in) */
int pc_ui_direct_tick(int selectedMenu, char* state, int* result)
{
    pc_ui_input in;
    if (direct.menu && direct.menu != selectedMenu)
        direct_abandon(0);
    if (!direct.menu) {
        if (*state != 0 || selectedMenu != 12)
            return 0;
        if (!MODERN_MENUS || !pc_overlay_active || !pc_overlay_active())
            return 0;
        if (!pc_ui_loot_begin())
            return 0;
        direct.menu = selectedMenu;
        direct.settle = DIRECT_SETTLE;
        /* the field HUD slides out under the screen's backdrop, as for the
         * shell (the original's loot list leaves it up: back at the end) */
        hide_hud();
    }
    in.pressed = map_buttons(vs_main_buttonsPressed.all);
    in.repeat = map_buttons(vs_main_buttonRepeat) | in.pressed;
    if (direct.settle > 0) {
        --direct.settle;
        in.pressed = in.repeat = 0;
    }
    *state = 0;
    *result = pc_ui_loot_frame(&in);
    if (*result) {
        direct.menu = 0;
        direct_show_hud();
    }
    return 1;
}

int pc_ui_direct_active(void) { return direct.menu; }
