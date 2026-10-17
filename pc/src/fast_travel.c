/* Fast travel, game side (fast_travel.h; the rules: fast_travel_rules.c;
 * pc/docs/Fast_Travel.md). Compiled with the game headers (battle builds).
 * The src hooks (ROOD_PC) call:
 *   MENU0 15AC.c     pc_fast_travel_unlock (for unkB5), _cost, _mask,
 *                    _blocked; vs_battle_shortcutInvoked PC_SHORTCUT_TRAVEL
 *   BATTLE 5BF94.c   func_800C930C: pc_fast_travel_menu_bits, _mask
 *   BATTLE 146C.c    func_800735F8 case 10: pc_fast_travel_begin
 * and room_warp.c calls pc_fast_travel_poll from its field poll (vs_battle_exec
 * case 1, after the event trigger, before doors and Triangle) and
 * pc_fast_travel_room_loaded from its room hook. Nothing here writes unkB5,
 * menuFlags, savePointStates or magicUsed. */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../src/SLUS_010.40/main.h"
#include "fast_travel.h"
#include "room_warp.h"

extern char D_800F4C34; /* an event script is running */
extern u_int D_800F196C; /* the field loop's mode (1: Ashley walks, 5: a menu) */
extern short D_800F4BE2; /* a script room change is pending */
extern char vs_main_locationZoneMapIds[]; /* 512 x (zone, room) */
void func_8007357C(int arg0, int arg1);
u_int func_800A0BE0(int actorId); /* (as room_warp.c declares it) */
int VSync(int mode);
int pc_window_travel_held(void);        /* boot_window.c: the Travel binding is held (focused) */
unsigned pc_window_travel_shared(void); /* boot_window.c: PS1 buttons on the same key or pad element */

/* Host state, not the game's: the input, the counters, the prompt (save
 * states, pc/src/snapshot.c, keep the battle units' statics; these follow
 * the host's frames and presses and stay out, .data.rood_host). The trip in
 * progress is the game's (a save state taken during one resumes it). */
#define HOST __attribute__((section(".data.rood_host")))
unsigned pc_fast_travel_prompt_drawn HOST; /* fast_travel_prompt.cpp counts the frames it drew */

/* A field frame lasts 2 or 3 vsyncs (more in a heavy room): the poll runs
 * once per frame, and not at all while an action plays (an enemy's attack:
 * field mode 2). An RPC press waits for the next poll (dropped when none
 * comes within 4 s); the prompt follows the last poll. */
#define PRESS_VSYNCS 240
#define PROMPT_VSYNCS 8
static int held_before HOST;
static int rpc_press HOST; /* RPC fast_travel {press}: VSync(-1) + 1 when asked */
static unsigned presses HOST, opened HOST, ignored HOST;
static int prompt_ok HOST, prompt_vsync HOST;
static struct {
    int pending, loaded;
    pc_ft_trip t;
} trip;
static pc_ft_trip last;

static pc_ft_settings settings(void)
{
    pc_ft_settings c;
    c.enabled = pc_qol_fast_travel;
    c.cost = pc_qol_fast_travel_cost;
    c.flat = pc_qol_fast_travel_flat;
    c.scale = pc_qol_fast_travel_scale;
    return c;
}

/* Ashley in a loaded room, else NULL (and nothing below reads the room). The
 * RPC report can come at any vsync, inside a room load's wait loop too
 * (func_80089DC0): the room's sections are not there then (a room change
 * frees and clears vs_battle_roomData, func_8008ABF0; _loadMpdRoomSection
 * sets them all and unk0 = 1 once the room is copied), and func_8008A4FC
 * reads the collision map under Ashley. The src hooks call from the menus and
 * the field, where a room is always loaded. */
/* 0 from a jump to the title or into BATTLE (boot_sdk.c: the old game's room
 * data and actors are stale, freed with its heap) until the next room's
 * sections are loaded (game state: a save state taken in the field has it 1). */
static int room_live = 1;
void pc_fast_travel_reset(void) { room_live = 0, trip.pending = 0; } /* (a trip under way ends with its game) */

static vs_battle_actor2* ashley(void)
{
    if (!room_live || vs_battle_roomData.unk0 != 1 || psx_get(vs_battle_roomData.collisionMapSection) == NULL
        || vs_battle_characterState == NULL || vs_battle_actors[0] == NULL)
        return NULL;
    return psx_get(vs_battle_characterState->unk3C);
}

static void fill(pc_ft_state* s)
{
    vs_battle_actor2* a = ashley();
    int i, phase = pc_room_warp_phase ? pc_room_warp_phase() : 0;
    u_int status = a ? func_800A0BE0(0) : 0;
    s->game = vs_main_stateFlags.unkB5;
    s->intro = vs_main_stateFlags.introState;
    s->warped = (pc_room_warp_warped && pc_room_warp_warped(NULL, NULL)) || (phase >= 1 && phase <= 3);
    s->no_save = vs_main_stateFlags.unkA7 != 0;
    s->circle = a ? func_8008A4FC() : 0;
    s->here = a ? pc_ft_here(vs_battle_getCurrentRoomId()) : PC_FT_POINTS;
    s->event = D_800F4C34 != 0 || D_800F4BE2 != 0 || vs_battle_screenTransitionStep != 0
        || vs_main_stateFlags.gameOver != 0;
    s->busy = !a || a->unk954 != 0 || (status & 0x4000) || (vs_main_buttonsState & 3); /* L2, R2 */
    s->mask = a ? (unsigned)func_800CABE0(0) : ~0u;
    s->mp = a ? a->currentMP : 0;
    for (i = 0; i < PC_FT_POINTS; ++i)
        s->states[i] = (unsigned char)vs_main_stateFlags.savePointStates[i];
}

/* ---- the src hooks ---- */

int pc_fast_travel_unlock(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_unlock(&c, &s);
}

int pc_fast_travel_cost(int raw)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_cost(&c, &s, raw);
}

unsigned pc_fast_travel_mask(unsigned bits)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_mask(&c, &s, bits);
}

unsigned pc_fast_travel_menu_bits(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_menu_bits(&c, &s);
}

const char* pc_fast_travel_why(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_blocked(&c, &s);
}

int pc_fast_travel_blocked(void) { return pc_fast_travel_why() != NULL; }

/* func_800735F8 case 10, before func_8007C694 and the MP charge: the trip to
 * check on arrival; whether it counts as magic (the game's unlock only). */
int pc_fast_travel_begin(int packed)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    trip.t.valid = 1;
    trip.t.location = packed & 0x1FF;
    trip.t.to = pc_ft_here(trip.t.location);
    trip.t.from = s.here;
    trip.t.whose = pc_ft_whose(&c, &s);
    trip.t.counted = pc_ft_counts_as_magic(&s);
    trip.t.arrived = trip.t.circle = trip.t.room_ok = 0;
    trip.t.mp_before = s.mp;
    trip.t.mp_after = 0;
    trip.pending = 1;
    trip.loaded = trip.t.location == vs_battle_getCurrentRoomId(); /* the same room: nothing loads */
    pc_ft_log_trip(&trip.t);
    return trip.t.counted;
}

void pc_fast_travel_room_loaded(void)
{
    room_live = 1;
    if (trip.pending)
        trip.loaded = 1;
}

/* The first field frame after the trip's room load with no event running:
 * where Ashley stands (logged, reported; nothing retried or written). */
static void arrival(void)
{
    vs_battle_actor2* a = ashley();
    if (!trip.pending || !trip.loaded || D_800F4C34 != 0 || vs_battle_screenTransitionStep != 0)
        return;
    trip.pending = 0;
    trip.t.circle = a ? func_8008A4FC() : 0;
    trip.t.room_ok = vs_battle_getCurrentRoomId() == trip.t.location;
    trip.t.arrived = trip.t.room_ok && trip.t.circle == trip.t.to + 1;
    trip.t.mp_after = a ? a->currentMP : 0;
    last = trip.t;
    pc_ft_log_arrival(&last);
}

void pc_fast_travel_press(void) { rpc_press = VSync(-1) + 1; }

/* room_warp.c's field poll (vs_battle_exec case 1): the arrival check, the
 * prompt's state, the Travel button. 1: the field loop breaks out (as after a
 * door): the Travel button opened MENU0 (15AC.c handleShortcut takes
 * PC_SHORTCUT_TRAVEL straight to the destinations list). */
int pc_fast_travel_poll(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    const char* why;
    int held = pc_window_travel_held(), now = VSync(-1);
    int pressed = (held && !held_before) || (rpc_press && now - (rpc_press - 1) <= PRESS_VSYNCS);
    held_before = held;
    rpc_press = 0;
    arrival();
    fill(&s);
    why = pc_ft_shortcut(&c, &s);
    prompt_ok = why == NULL;
    prompt_vsync = now;
    if (!pressed)
        return 0;
    ++presses;
    /* (func_800CACD0 plays the invalid-press sound when a menu is already
     * up: checked first, so a refused press stays silent) */
    if (why || vs_battle_menuState.currentMenu != 0x3F || !func_800CACD0(1, 0)) {
        ++ignored; /* nothing happens: no sound, Select (if shared) does what it always does */
        return 0;
    }
    /* as an L2 shortcut opens a school's list (vs_battle_validateShortcutSelection,
     * then func_8007357C): MENU0 straight into _teleportMenu (15AC.c handleShortcut) */
    vs_battle_shortcutInvoked = PC_SHORTCUT_TRAVEL;
    D_800F4FDB = 1;
    func_8007357C(0, 0);
    vs_main_buttonsPressed.all &= ~pc_window_travel_shared(); /* Select's zoom does not toggle too */
    ++opened;
    return 1;
}

int pc_fast_travel_prompt(void)
{
    /* (the field still: no menu opened since the poll) */
    return pc_qol_fast_travel_prompt && prompt_ok && VSync(-1) - prompt_vsync <= PROMPT_VSYNCS && D_800F196C == 1
        && vs_battle_menuState.currentMenu == 0x3F;
}

/* ---- RPC fast_travel ---- */

static void audit_points(pc_ft_report* r)
{
    int i, k, n = pc_rooms_count ? pc_rooms_count() : 0;
    for (i = 0; i < PC_FT_USED; ++i) {
        pc_ft_audit_point* a = &r->audit[i];
        int d = pc_ft_save_point_data[i], loc = d & 0x1FF;
        a->zone = (u_char)vs_main_locationZoneMapIds[loc * 2];
        a->room = (u_char)vs_main_locationZoneMapIds[loc * 2 + 1];
        a->circle_id = a->cell[0] = a->cell[1] = a->cell[2] = 0;
        for (k = 0; k < n; ++k) {
            const pc_room_info* room = pc_rooms_get(k);
            if (room && room->zone == a->zone && room->room == a->room) {
                a->circle_id = room->circle_id;
                a->cell[0] = room->circle_cell[0];
                a->cell[1] = room->circle_cell[1];
                a->cell[2] = room->circle_cell[2];
            }
        }
        a->ok = a->circle_id == i + 1 && a->cell[0] == ((d >> 16) & 0x1F) && a->cell[1] == ((d >> 24) & 0x1F)
            && a->cell[2] == 0;
    }
    r->audited = 1;
}

void pc_fast_travel_report(pc_ft_report* r, int audit)
{
    r->settings = settings();
    fill(&r->state);
    r->prompt = pc_fast_travel_prompt();
    r->prompt_on = pc_qol_fast_travel_prompt;
    r->presses = presses;
    r->opened = opened;
    r->ignored = ignored;
    r->prompt_drawn = pc_fast_travel_prompt_drawn;
    r->last = last;
    r->audited = 0;
    if (audit)
        audit_points(r);
}
