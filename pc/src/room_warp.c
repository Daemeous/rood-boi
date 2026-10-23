/* The Room Browser's warp, game side (host side and design: room_catalog.c).
 * Compiled with the game headers; the src hooks (ROOD_PC) call these. */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/146C.h"

extern char D_800F4C34; /* an event script is running */
extern short D_800F4BE2;
extern short _mpdScriptSection; /* 1: the room's entry script runs on entering */
extern void* _mpdClearedSection; /* the room's trigger table (u16 offsets, 0xFFFF ends it) */
extern _mpdRoomDoorSection_t D_800F1AB0; /* where the player is (the last door's destination) */
int func_8007C4E0(_mpdRoomDoorSection_t* door, int effect, int speed);
int func_8007C5C0(_mpdRoomDoorSection_t* door, int effect, int speed);
void func_800A0ABC(int actor);
void func_800A087C(int actorId, int flags);
u_int func_800A0BE0(int actorId);

int pc_room_warp_pending(void);
int pc_room_warp_take(int zone, int room, unsigned char door[12]);
int pc_room_warp_arrived(void);
int pc_room_warp_finish(int zone, int room);
void pc_room_warp_benched(int slot, int script_id);
extern int pc_room_warp_enemies, pc_room_warp_scripts;
int pc_fast_travel_poll(void); /* fast_travel.c */
void pc_fast_travel_room_loaded(void);
int pc_room_warp_warped(int* zone, int* room); /* room_catalog.c */
int pc_story_field_poll(void); /* story_boot.c */
void pc_story_room_loaded(void);

/* savePointStates when a chain of warps started. A warp with scripts on runs
 * the room's entry script, and a Circle's room script records the Circle
 * (op 0x50: savePointStates[i] = 1), which would make a Room Browser visit a
 * fast travel destination. Put back once the warps end: back where they
 * started, or the warped state cleared ("keep this room" keeps the room, not
 * the Circles found while warped). The snapshot is host state beside the
 * warped flag (room_catalog.c), dropped when the game's state is replaced. */
void pc_room_warp_points_hold(const unsigned char* states);
int pc_room_warp_points_release(unsigned char* states);

static void points_restore(void)
{
    pc_room_warp_points_release((unsigned char*)vs_main_stateFlags.savePointStates);
}

static _mpdRoomDoorSection_t door; /* what func_8007C4E0 / func_8007C5C0 copy */
static vs_main_mapStatus_t map_before;

/* Enemies off. The room's enemies still spawn: its scripts (the entry event,
 * triggers, door scripts) address them by id or slot, and an id with no
 * actor resolves to slot 1 (func_8007CF18), which is empty, so a script's
 * actor ops (func_800A97EC, func_800A9988, ...) dereferenced NULL. Instead
 * they are benched whenever no event script runs, with the two halves of
 * what the scripts' own op 0x23 (func_800B770C) does to take an actor out
 * of a scene: unlinked from the combatants (func_8007D1A8: nothing targets
 * them and they engage no one) and not drawn (func_800A087C with their
 * display flags less bit 1). Whether they are active and frozen stays the
 * game's: a script that moves them waits for them to arrive (13/6's door
 * script hung on benched actors that were also made inactive). A script
 * that shows one (op 0x23) has it shown until it ends; then it is benched
 * again. */
static int bench; /* this room was warped to with its enemies off */
static u_short occupied; /* actor slots in use before the room's spawn table ran */
static u_short benched; /* the slots its spawn table filled */

static int linked(vs_battle_actor* actor)
{
    int i;
    for (i = 0; i < 16; ++i)
        if (vs_battle_actors[i] != NULL && psx_get(vs_battle_actors[i]->next) == actor)
            return 1;
    return 0;
}

static void bench_enemies(void)
{
    int i;
    for (i = 1; i < 16; ++i) {
        vs_battle_actor* actor = vs_battle_actors[i];
        u_int flags;
        if (!(benched & (1 << i)) || actor == NULL || D_800F4538[i] == NULL)
            continue;
        /* 1: active, 2: drawn, 4: frozen, 0x1800: weapon models drawn; 0x40
         * (unk9_7) is not reported, so it is carried over as it is */
        flags = func_800A0BE0(i);
        if (!(flags & 2) && !linked(actor))
            continue;
        pc_room_warp_benched(i, actor->unk18);
        func_8007D1A8(i, 1);
        func_800A087C(i, (flags & 0x1805) | (D_800F4538[i]->unk0.unk9_7 << 6));
    }
}

/* RPC ending (developer jump): play the ending from the field, as the final
 * event script's special op 1 does (vs_battle_script_specialOp). It loads
 * ENDING.PRG over BATTLE and returns to the title, so it does not return. */
volatile int pc_ending_request;

/* vs_battle_exec, the field (case 1), where doors are checked: start a
 * requested warp as a door does (1: the loop breaks out, as after a door). */
int pc_room_warp_poll(void)
{
    if (pc_story_field_poll()) /* the cutscene viewer's scene: its event (story_boot.c) */
        return 1;
    if (pc_ending_request && vs_battle_screenTransitionStep == 0 && D_800F4C34 == 0 && D_800F4BE2 == 0
        && !vs_main_stateFlags.gameOver) {
        pc_ending_request = 0;
        vs_main_showEndingAndReturnToTitle();
    }
    if (bench && D_800F4C34 == 0 && D_800F4BE2 == 0)
        bench_enemies(); /* (again, after an event script) */
    points_restore(); /* a chain of warps ended: the Circles as before it */
    if (pc_room_warp_finish(D_800F1AB0.zndId, D_800F1AB0.mpdId)) {
        /* The first field frame there: the map screen keeps no trace of it. */
        vs_main_mapStatus = map_before;
        return 0;
    }
    if (pc_fast_travel_poll()) /* the Travel button on a Circle of Magic (fast_travel.c) */
        return 1;
    if (!pc_room_warp_pending())
        return 0;
    if (vs_battle_screenTransitionStep != 0 || D_800F4C34 != 0 || D_800F4BE2 != 0 || vs_main_stateFlags.gameOver)
        return 0;
    {
        int chain = pc_room_warp_warped(NULL, NULL); /* (before the take: a new chain or the next hop) */
        if (!pc_room_warp_take(D_800F1AB0.zndId, D_800F1AB0.mpdId, (unsigned char*)&door))
            return 0;
        if (!chain)
            pc_room_warp_points_hold((const unsigned char*)vs_main_stateFlags.savePointStates);
    }
    map_before = vs_main_mapStatus;
    if (door.zndId == _zoneContext.zndId)
        func_8007C4E0(&door, 0, 8);
    else
        func_8007C5C0(&door, 0, 8); /* another zone: its file loads first */
    func_800A0ABC(0);
    return 1;
}

/* Scripts off: the trigger table keeps only the door scripts (event id bit
 * 0x8000: a script of the room's door section, func_800BF850 case 2), such
 * as 13/6's rusted door that won't budge; without them a scripted door can let
 * Ashley straight through. The room's own events go. */
static void keep_door_scripts(void)
{
    u_short* table = (u_short*)_mpdClearedSection;
    int i, kept = 0;
    if (table == NULL)
        return;
    for (i = 0; i < 16 && table[i] != 0xFFFF; ++i) { /* (func_800BEC58 reads 16 at most) */
        u_char* entry = (u_char*)table + table[i]; /* type, event id, conditions */
        if ((entry[1] | entry[2] << 8) & 0x8000)
            table[kept++] = table[i];
    }
    if (kept < 16)
        table[kept] = 0xFFFF;
}

static u_short actor_slots(void)
{
    u_short mask = 0;
    int i;
    for (i = 0; i < 16; ++i)
        if (vs_battle_actors[i] != NULL)
            mask |= 1 << i;
    return mask;
}

/* func_80089DC0: the room's sections are loaded; its enemies spawn
 * (func_80089114, then pc_room_warp_room_spawned) and its entry trigger
 * runs next. */
void pc_room_warp_room_loaded(void)
{
    pc_story_room_loaded(); /* the cutscene viewer's scene (story_boot.c) */
    pc_fast_travel_room_loaded(); /* a fast travel trip arrives (fast_travel.c) */
    bench = 0;
    benched = 0;
    if (!pc_room_warp_arrived())
        return;
    bench = !pc_room_warp_enemies;
    occupied = actor_slots();
    if (!pc_room_warp_scripts) {
        _mpdScriptSection = 0; /* no entry script (cutscene) */
        keep_door_scripts();
    }
}

/* A trigger of the entry kind (func_800BEC58 type 1) in the table. */
static int entry_trigger(void)
{
    u_short* table = (u_short*)_mpdClearedSection;
    int i;
    for (i = 0; table != NULL && i < 16 && table[i] != 0xFFFF; ++i)
        if (*((u_char*)table + table[i]) & 1)
            return 1;
    return 0;
}

/* func_80089DC0, right after func_80089114 spawned the room's enemies and
 * objects. With no entry event to come they are benched now (before a
 * field frame runs); otherwise once the event ends. */
void pc_room_warp_room_spawned(void)
{
    int i;
    if (!bench)
        return;
    benched = actor_slots() & ~occupied;
    for (i = 0; i < 16; ++i) /* the room's objects (mpdObjectSpawn) stay */
        if ((benched & (1 << i)) && vs_battle_actors[i]->unk1C == 0x10)
            benched &= ~(1 << i);
    if (!(_mpdScriptSection & 1) && !entry_trigger())
        bench_enemies();
}
