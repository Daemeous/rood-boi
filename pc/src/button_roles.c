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
 * binding presses it now (pc_prompt_glyph, the modern menus' icons).
 *
 * The game's own ○ and × follow it too, wherever pc_button_roles_shown
 * exchanges them (the context now; not where the game's words name buttons
 * the roles never move: symbols_swap), before anything draws them, so the
 * original pictures, ui_text's font and the button prompts' icons
 * (button_prompts.c) all show the exchanged button:
 *   text     the font's ○ and × (glyphs 0xB0 and 0xB1 of either table, side
 *            by side in a row of 12 x 12 cells): the printers report each
 *            character as they set it (pc_text_glyph, ui_text.c), which
 *            moves its sprites' texels (or the fixed font's quad's) one cell
 *            over and keeps the other glyph (pc_button_roles_glyph);
 *   sprites  the 16 x 16 buttons of the field UI page (768, 0), CLUT
 *            (944, 223), ○ at (32, 128) and × at (48, 128): the GPU walker
 *            (boot_runtime.c, sprites and polygons) samples the other cell
 *            (pc_button_roles_texels); the packet in guest memory stays as
 *            the game built it. Only when both cells hold a picture (the
 *            page can hold something else). */
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

/* The game's ○ and × as drawn (RPC button_roles "drawn"; tests): [text,
 * sprite][○, ×] after the exchange, and how many were exchanged [text, sprite]. */
static unsigned long symbols_drawn[2][2], symbols_swapped[2];

/* The game's ○ and × exchanged now: as the prompt icons, except where the
 * game's words name buttons the roles never move, even where that menu
 * itself confirms with Cross:
 *   - anywhere in Battle Abilities (MENU2): the Chain / Defense choice's
 *     information, "Assign Abilities to the ○, △, and □ buttons" (the
 *     ability buttons);
 *   - the Quick Manual's text (MENUE): "Shortcuts to Magic (Using △,○,×,
 *     and □ buttons)" (the L2 shortcut's slots); its page pictures are the
 *     manual's own (original controls) and stay too. Its ○ MENU sprite is
 *     its own control and follows.
 * The English data has no other ○ or × in its text. */
#define MANUAL_COMMAND 10 /* Quick Manual: currentMenu 10 */
static int symbols_exceptions = 1; /* RPC button_roles symbol_exceptions (tests: 0 exchanges those too) */
void pc_button_roles_symbol_exceptions(int on) { symbols_exceptions = on != 0; }
static int symbols_swap(int text)
{
    int menu = vs_battle_menuState.currentMenu & 0x3F, open = battle_running() && !(D_800F4EA0 & 0x200);
    return pc_button_roles_shown(5) != 5
        && !(symbols_exceptions && open && (menu == MENU2_COMMAND || (text && menu == MANUAL_COMMAND)));
}

int pc_button_roles_glyph(int glyph)
{
    int symbol = glyph % 189 - 0xB0; /* 0 ○, 1 × */
    if (glyph < 0 || symbol < 0 || symbol > 1)
        return glyph;
    int swap = symbols_swap(1);
    symbols_swapped[0] += (unsigned long)swap;
    ++symbols_drawn[0][symbol ^ swap];
    return glyph - symbol + (symbol ^ swap);
}

#define UI_PAGE 12
#define UI_CLUT (223u << 6 | 944 / 16)
#define BUTTON_ROW 128 /* ○ at u 32, × at u 48 */
static int cell_inked(const unsigned short* vram, int u0)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x += 4) /* 4-bit: four texels a word */
            if (vram[(BUTTON_ROW + y) * 1024 + UI_PAGE * 64 + (u0 + x) / 4])
                return 1;
    return 0;
}

int pc_button_roles_texels(const unsigned short* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1)
{
    /* 4-bit, page 12 at y 0, the buttons' CLUT, inside the ○ or × cell. */
    if ((tpage & 0x19f) != UI_PAGE || (clut & 0x7fff) != UI_CLUT || v0 < BUTTON_ROW || v1 >= BUTTON_ROW + 16
        || u0 < 32 || u1 >= 64 || u0 / 16 != u1 / 16)
        return 0;
    int symbol = u0 / 16 - 2; /* 0 ○, 1 × */
    int swap = symbols_swap(0) && vram && cell_inked(vram, 32) && cell_inked(vram, 48);
    symbols_swapped[1] += (unsigned long)swap;
    ++symbols_drawn[1][symbol ^ swap];
    return swap ? (symbol ? -16 : 16) : 0;
}

/* RPC button_roles: the options, the context now and the button each role is on there. */
void pc_button_roles_json(char* out)
{
    int context = pc_button_roles_context(), swap = swap_now();
    sprintf(out,
        "{\"confirm_cross\":%d,\"field_cross_attack\":%d,\"context\":\"%s\",\"swap\":%d,\"confirm\":\"%s\","
        "\"cancel\":\"%s\",\"attack\":\"%s\",\"interact\":\"%s\",\"target\":\"%s\","
        "\"drawn\":{\"text\":{\"circle\":%lu,\"cross\":%lu,\"swapped\":%lu},"
        "\"sprite\":{\"circle\":%lu,\"cross\":%lu,\"swapped\":%lu}},\"symbol_exceptions\":%d}",
        pc_qol_confirm_cross != 0, pc_qol_field_cross_attack != 0, CONTEXT_NAMES[context], swap,
        pc_qol_confirm_cross ? "cross" : "circle", pc_qol_confirm_cross ? "circle" : "cross",
        pc_qol_field_cross_attack ? "cross" : "circle", pc_qol_field_cross_attack ? "circle" : "cross",
        pc_qol_confirm_cross || pc_qol_field_cross_attack ? "cross" : "circle", symbols_drawn[0][0],
        symbols_drawn[0][1], symbols_swapped[0], symbols_drawn[1][0], symbols_drawn[1][1], symbols_swapped[1],
        symbols_exceptions);
}
