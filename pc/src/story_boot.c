/* Story scenes for the cutscene viewer, game side (host side: cutscene.cpp;
 * pc/docs/plans/2026-10-22-cutscene-viewer.md). Compiled with the game
 * headers; the src hooks (ROOD_PC) call these.
 *
 * A story scene starts the way loading a save starts the game: the story
 * flags (vs_main_stateFlags) are set to what they are just before the scene,
 * the room to start in is the "last door" (D_80061068), and the battle
 * engine starts afresh in that room (vs_main_jumpToBattle, as the load menu
 * does: MENU7 260.c), whose entry script then plays the scene as in the
 * story. From the title it is New Game with the same two things set when the
 * title exits (main.c vs_main_execTitle). The sound effect sets the scripts
 * before it loaded and left loaded (ops 0x85 / 0x88; the scenes after them
 * play those sounds without loading them) are loaded with the room. A
 * trigger's scene (a cell stepped on, something examined) starts the same
 * way and, once the room's entry script is done, its event is started as
 * the trigger starts one (func_800BEC58, func_80073898). An EVT file a
 * script chains to is reached by playing its script (cutscene.cpp follows
 * it). The flags, the room and the sounds come from
 * pc/assets/cutscenes/story.json (pc/tools/story_scan.py). */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/146C.h"
#include "host_format.h"
#include <string.h>

extern char D_800F4C34;         /* the event runner's state (0: none) */
extern short D_800F4BE2;        /* a script's room change under way */
extern char D_800F4BA8;         /* the running event blocks the triggers */
extern u_short D_800F4BE0;      /* the event's source: 0 EVT file, 1 room script, 2 door script */
extern short _evtFile;          /* the EVT file / door script index */
void func_80073898(void);
extern int pc_game_phase; /* boot_runtime.c: 0 the game, 1 the title, 2 an idle demo */
extern _mpdRoomDoorSection_t D_800F1AB0; /* where the player is (the last door's destination) */

enum { STORY_IDLE, STORY_PENDING, STORY_BOOTING, STORY_LOADED, STORY_STARTED };

static int phase;
static unsigned char door[12];
static signed char flags[sizeof(vs_main_flags_t)];
static int event = -1; /* (source << 12) | file to start once the room is in; -1: its entry script plays */
static int sounds[4];  /* the sound effect set (SE file) in slots 1-3 (0: none), the current slot */
static int boots;

#define story_log(...)                                                                                       \
    do {                                                                                                      \
        char line_[160];                                                                                      \
        pc_format(line_, (int)sizeof line_, __VA_ARGS__);                                                     \
        pc_log_line(line_);                                                                                   \
    } while (0)

/* Ask for a scene: the door to start at (12 bytes, _mpdRoomDoorSection_t),
 * the flags (count bytes from 0; the rest 0), the event to start once the
 * room's entry script is done (-1: none), the sound effect sets (slots 1-3,
 * the current slot; NULL none). Taken at the next field frame or title exit. */
int pc_story_request(const unsigned char start[12], const signed char* values, int count, int start_event,
    const int sfx[4])
{
    if (phase == STORY_BOOTING)
        return 0;
    memcpy(door, start, sizeof door);
    memset(flags, 0, sizeof flags);
    if (count > (int)sizeof flags)
        count = (int)sizeof flags;
    if (count > 0)
        memcpy(flags, values, (size_t)count);
    event = start_event;
    memset(sounds, 0, sizeof sounds);
    if (sfx)
        memcpy(sounds, sfx, sizeof sounds);
    phase = STORY_PENDING;
    return 1;
}
int pc_story_phase(void) { return phase; }
/* The room the game is in (the last door's destination). */
void pc_story_where(int* zone, int* room)
{
    *zone = D_800F1AB0.zndId;
    *room = D_800F1AB0.mpdId;
}
int pc_story_boots(void) { return boots; }
void pc_story_cancel(void)
{
    if (phase == STORY_PENDING)
        phase = STORY_IDLE;
}
/* The take is under way (or it gave up): idle again. */
void pc_story_done(void) { phase = STORY_IDLE; }

static void apply(void)
{
    signed char* state = (signed char*)&vs_main_stateFlags;
    /* bytes 0 and 1 (the difficulty, a cleared game) stay the player's; the rest are the scene's */
    memcpy(state + 2, flags + 2, sizeof flags - 2);
    vs_main_stateFlags.gameOver = 0;
    memcpy(&D_80061068, door, sizeof door);
    pc_game_phase = 0; /* (an idle demo is a game now) */
    story_log("[story] starting in zone %d room %d%s", door[0], door[1], event >= 0 ? " (then its event)" : "");
    phase = STORY_BOOTING;
    ++boots;
}

/* main.c vs_main_execTitle, after the title: New Game with a scene asked for
 * starts there. */
void pc_story_title_exit(int start_state)
{
    if (phase == STORY_PENDING && start_state == 0)
        apply();
}

/* 146C.c vs_battle_exec, the top of each frame: a scene asked for in the
 * game starts it afresh (does not return). */
static int idle_frames; /* frames the room has had no event (a trigger's scene waits for that) */
static void start_event(void);
void pc_story_frame(void)
{
    int slot;
    if (phase == STORY_LOADED && event >= 0) {
        /* (the field's own place, pc_story_field_poll, is not reached while a room only events lead to
         * is still dark: no script fades it in until the event runs) */
        idle_frames = D_800F4C34 == 0 && D_800F4BE2 == 0 ? idle_frames + 1 : 0;
        if (idle_frames >= 60)
            start_event();
        return;
    }
    if (phase != STORY_PENDING || D_800F4BE2 != 0)
        return;
    apply();
    /* A scene may be playing with its sound effect sets loaded: the heap they are in starts afresh, and a
     * set still marked loaded is not loaded again (vs_main_loadSfxSlot), so they go first. */
    for (slot = 1; slot <= 3; ++slot) {
        while (vs_main_freeSfxQueueSlot(slot) == 1)
            vs_main_gametimeUpdate(0);
        vs_main_freeSfx(slot);
    }
    vs_main_freeMusic(1);
    vs_main_jumpToBattle();
}

/* room_warp.c pc_room_warp_room_loaded: the room's sections are in, its
 * entry trigger is next. The sound effect sets are loaded now (waiting as
 * the room's own loads do). */
void pc_story_room_loaded(void)
{
    int slot;
    if (phase != STORY_BOOTING)
        return;
    for (slot = 1; slot <= 3; ++slot) {
        int waits = 0;
        if (sounds[slot - 1] <= 0)
            continue;
        vs_main_loadSfxSlot(sounds[slot - 1], slot);
        while (vs_main_freeSfxQueueSlot(slot) == 1 && ++waits < 600)
            vs_main_gametimeUpdate(0);
        story_log("[story] sound set %d in slot %d%s", sounds[slot - 1], slot, waits >= 600 ? ": not loaded" : "");
    }
    if (sounds[3] > 0)
        vs_main_setCurrentSfx(sounds[3]);
    phase = STORY_LOADED;
    story_log("[story] zone %d room %d loaded", D_800F1AB0.zndId, D_800F1AB0.mpdId);
}

/* room_warp.c pc_room_warp_poll (the field, where triggers start events):
 * start the scene's event as a trigger does. 1: the field loop breaks. */
int pc_story_field_poll(void)
{
    if (phase != STORY_LOADED || event < 0)
        return 0;
    if (vs_battle_screenTransitionStep != 0 || D_800F4C34 != 0 || D_800F4BE2 != 0)
        return 0;
    start_event();
    return 1;
}
static void start_event(void)
{
    _evtFile = (short)(event & 0xFFF);
    D_800F4BE0 = (u_short)(event >> 12);
    D_800F4C34 = 1;
    D_800F4BA8 = 0;
    phase = STORY_STARTED;
    idle_frames = 0;
    story_log("[story] event %#x started", event);
    func_80073898();
}
