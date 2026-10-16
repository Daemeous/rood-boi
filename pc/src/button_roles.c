/* Button roles: Cross confirms (menus) and Cross attacks (field), from
 * eremtsov's Vagrant Story QoL mod (CC0; https://github.com/eremtsov/rood-reverse,
 * branch qol-mod). Two options, off by default and independent:
 *   qol_confirm_cross       Cross confirms and Circle cancels where the game
 *                           confirms with Circle and cancels with Cross
 *   qol_field_cross_attack  in the field Cross draws the weapon and attacks,
 *                           Circle interacts and sheathes
 * Both on are the mod's default controls.
 *
 * The game's code is untouched: the pad word the game reads (boot_window.c
 * pc_window_buttons: keys, gamepad and RPC input) passes through
 * pc_button_roles_buttons, which exchanges the Circle and Cross bits when
 * the game is in a context the options cover. The context comes from the
 * game's own state, read here (battle builds; this file is compiled with
 * the game headers):
 *
 *   field          D_800F196C 1 (the field, Ashley free to act): the field
 *                  option. vs_battle_exec's case 1 tests Circle (draw,
 *                  attack: the targeting sphere) and Cross (interact,
 *                  sheathe).
 *   targeting      D_800F196C 2 with _cameraMode 1..3 (func_80078248,
 *                  func_800782E4, func_80078364: the target list and the
 *                  area target of an attack, a spell or an item): either
 *                  option. It is a menu confirm and the attack's second
 *                  press, so it follows Cross whenever one option is on.
 *   the game's     buttons stay the game's own:
 *                  - D_800F196C 2 with any other _cameraMode: the action
 *                    runs (func_80078AB4 reads the chain and defense
 *                    abilities on Circle, Triangle and Square as the player
 *                    put them there);
 *                  - D_800F196C 6 (the L2 shortcut overlay: Circle and Cross
 *                    pick slots) and 7 (the first-person view: Cross, R3 or
 *                    Start leave it);
 *                  - the Battle Abilities lists (MENU2 vs_menu2_exec state
 *                    chain 6 or defense 7, in vs_battle_submenuStates[3]
 *                    while currentMenu is 3: Circle, Triangle and Square put
 *                    the ability on that button, Cross leaves), and the
 *                    modern Abilities tab's learned list (the same, see
 *                    pc_ui_shell_face_buttons);
 *   menus          everything else (menus and their screens, events and
 *                  dialogues, the title, saving and loading, the battle
 *                  program's other modes): the confirm option.
 * Outside the battle loop (the title, loading, the ending: no battle frame
 * for a few vsyncs) it is the menus' context.
 *
 * A button keeps the role it was pressed with until it is released: the
 * context often changes on the press itself (Circle opens the targeting
 * sphere), and exchanging a held button would make the game see a new press
 * of the other one.
 *
 * Prompt icons: pc_button_roles_shown maps a PS1 button to the one whose
 * binding presses it now (pc_prompt_glyph, the modern menus' icons). The
 * game's own button sprites and the ○ / × in its text are left to the button
 * prompts (button_prompts.c). */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../src/SLUS_010.40/main.h"
#include <libetc.h>

extern int sprintf(char*, const char*, ...);

extern int pc_qol_confirm_cross, pc_qol_field_cross_attack; /* project_settings.cpp */
extern u_int D_800F196C;  /* field mode (weapon_slots.c lists them) */
extern u_int _cameraMode; /* BATTLE 146C.c: in field mode 2, the action's phase */
extern unsigned pc_vsync_count(void);
extern unsigned pc_battle_frame_count(void);
extern int pc_ui_shell_face_buttons(void) __attribute__((weak)); /* ui/ui_shell.cpp */

enum { CONTEXT_MENUS, CONTEXT_FIELD, CONTEXT_TARGETING, CONTEXT_GAME };
static const char* const CONTEXT_NAMES[] = { "menus", "field", "targeting", "game" };

#define CIRCLE PADRright
#define CROSS PADRdown
#define MENU2_COMMAND 3 /* Battle Abilities: currentMenu 3, its state in submenuStates[3] */
enum { MENU2_CHAIN = 6, MENU2_DEFENSE = 7 }; /* vs_menu2_exec's states with the lists */

static struct {
    unsigned frames, frames_vsync; /* the battle loop's last frame and when it ran */
    unsigned down, swapped;        /* physical Circle / Cross held; those pressed with the roles exchanged */
} s;

static int battle_running(void)
{
    unsigned frames = pc_battle_frame_count(), now = pc_vsync_count();
    if (frames != s.frames) {
        s.frames = frames;
        s.frames_vsync = now;
    }
    return s.frames && now - s.frames_vsync <= 8;
}

static int ability_lists(void)
{
    int state = (u_char)vs_battle_submenuStates[MENU2_COMMAND];
    return (vs_battle_menuState.currentMenu & 0x3F) == MENU2_COMMAND && !(D_800F4EA0 & 0x200)
        && (state == MENU2_CHAIN || state == MENU2_DEFENSE);
}

int pc_button_roles_context(void)
{
    if (!battle_running())
        return CONTEXT_MENUS;
    if (ability_lists() || (pc_ui_shell_face_buttons && pc_ui_shell_face_buttons()))
        return CONTEXT_GAME;
    switch (D_800F196C) {
    case 1:
        return CONTEXT_FIELD;
    case 2:
        return _cameraMode >= 1 && _cameraMode <= 3 ? CONTEXT_TARGETING : CONTEXT_GAME;
    case 6:
    case 7:
        return CONTEXT_GAME;
    default:
        return CONTEXT_MENUS;
    }
}

/* Circle and Cross exchanged for a press now. */
static int swap_now(void)
{
    switch (pc_button_roles_context()) {
    case CONTEXT_MENUS:
        return pc_qol_confirm_cross != 0;
    case CONTEXT_FIELD:
        return pc_qol_field_cross_attack != 0;
    case CONTEXT_TARGETING:
        return pc_qol_confirm_cross || pc_qol_field_cross_attack;
    default:
        return 0;
    }
}

unsigned pc_button_roles_buttons(unsigned buttons)
{
    unsigned down = buttons & (CIRCLE | CROSS), pressed = down & ~s.down, result = buttons & ~(CIRCLE | CROSS);
    if (pressed) {
        if (swap_now())
            s.swapped |= pressed;
        else
            s.swapped &= ~pressed;
    }
    s.swapped &= down;
    s.down = down;
    if (down & CIRCLE)
        result |= s.swapped & CIRCLE ? CROSS : CIRCLE;
    if (down & CROSS)
        result |= s.swapped & CROSS ? CIRCLE : CROSS;
    return result;
}

int pc_button_roles_shown(int button)
{
    /* PS1 button indices (libetc bit numbers, button_prompts.h PC_PROMPT_*) */
    if ((button == 5 || button == 6) && swap_now())
        return 11 - button;
    return button;
}

/* RPC button_roles: the options, the context now and the button each role is on there. */
void pc_button_roles_json(char* out)
{
    int context = pc_button_roles_context(), swap = swap_now();
    sprintf(out,
        "{\"confirm_cross\":%d,\"field_cross_attack\":%d,\"context\":\"%s\",\"swap\":%d,\"confirm\":\"%s\","
        "\"cancel\":\"%s\",\"attack\":\"%s\",\"interact\":\"%s\",\"target\":\"%s\"}",
        pc_qol_confirm_cross != 0, pc_qol_field_cross_attack != 0, CONTEXT_NAMES[context], swap,
        pc_qol_confirm_cross ? "cross" : "circle", pc_qol_confirm_cross ? "circle" : "cross",
        pc_qol_field_cross_attack ? "cross" : "circle", pc_qol_field_cross_attack ? "circle" : "cross",
        pc_qol_confirm_cross || pc_qol_field_cross_attack ? "cross" : "circle");
}
