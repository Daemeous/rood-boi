/* Encounter lab, game side: a chosen fight in a chosen room, for testing
 * (RPC encounter in dev_rpc.c; the tool is pc/tools/encounter.py).
 *
 * A request holds spawn records in the room's own format (the MPD enemy
 * section: 0x28 bytes, mpdEnemySpawn in 146C.c), normally copied from a real
 * record of the same zone so the enemy keeps its AI settings and drops. The
 * Room Browser's warp takes Ashley there (room_goto, enemies on, scripts
 * off); once the room's sections are loaded (pc_room_warp_room_loaded) its
 * enemy records are replaced by the lab's and the game's own spawn
 * (func_80089114) creates them. Their models load from the zone's ZUD files
 * on demand, so any enemy of the zone's list (ZONEnnn.ZND) can appear in any
 * of its rooms. The room's objects (chests, crates: records of type 1) stay.
 * Merlose joins as she does in any room.
 *
 * A record is checked before it is used, never moved: an enemy whose tile is
 * blocked or off the room's floor is left out, with the reason in the status.
 * The room's memory of its enemies (D_80061078: positions, the defeated) is
 * cleared, and a seed makes the game's random numbers repeat. */
#include "common.h"
#include <stdio.h>
#include "../../src/BATTLE/BATTLE.PRG/146C.h"

extern void pc_log(const char*);
extern int randArr[97];
extern int randIndex;
short func_8008DC7C(int x, int z);

#define LAB_MAX 8
#define RECORD 0x28
#define TABLE 32 /* func_80089114 reads 32 records whatever the count */

/* mpdEnemySpawn (146C.c) offsets */
enum {
    R_TYPE = 0x00, R_INDEX = 0x01, R_HIDDEN = 0x02, R_ID = 0x04, R_SLOT = 0x05, R_FLAG_TEST = 0x06,
    R_STAT_TEST = 0x07, R_X = 0x0C, R_DIRECTION = 0x0D, R_Z = 0x0E, R_HEIGHT = 0x0F, R_FACING = 0x23,
    R_SCENE_TEST = 0x27,
};

static struct {
    int armed;
    int zone, room;
    int count;
    u_char records[LAB_MAX][RECORD];
    int seeded;
    u_int seed;
} lab;

static char status[600]; /* the last arrival, as JSON members */
static int arrivals;
static u_short lab_slots; /* actors the room's spawn created: the lab's enemies (and Merlose, who joins there) */
static u_short slots_before;

int pc_encounter_set(int zone, int room, const u_char* records, int count, int seeded, u_int seed)
{
    if (count < 0 || count > LAB_MAX)
        return 0;
    lab.zone = zone, lab.room = room, lab.count = count;
    vs_main_memcpy(lab.records, (void*)records, count * RECORD);
    lab.seeded = seeded, lab.seed = seed;
    lab.armed = 1;
    return 1;
}

void pc_encounter_cancel(void) { lab.armed = 0; }

static u_short actor_slots(void)
{
    u_short mask = 0;
    int i;
    for (i = 0; i < 16; ++i)
        if (vs_battle_actors[i] != NULL)
            mask |= 1 << i;
    return mask;
}

/* The floor at a tile's centre (func_8008DC7C, as Merlose's spawn uses it):
 * 0x7FFF where it is blocked or outside the room. */
static int floor_at(int x, int z)
{
    int height = func_8008DC7C((x << 7) | 64, (z << 7) | 64);
    if (height & 0x8000)
        return 0x7FFF;
    return (height << 17) >> 17;
}

/* The game's random numbers (vs_main_getRand: a table of 97 and an index)
 * from a seed, as vs_main_initRand fills it from rand(). */
static void seed_random(u_int seed)
{
    int i;
    for (i = 0; i < 97; ++i) {
        seed = seed * 1103515245u + 12345u;
        randArr[i] = (seed >> 16) & 0x7FFF;
    }
    randIndex = 96;
}

/* pc_room_warp_room_loaded: the warp arrived and the room's sections are
 * loaded; its spawn table runs next. */
void pc_encounter_room_loaded(void)
{
    _mpdEnemy* table;
    _mpdEnemy* old = psx_get(_zoneContext.mpdEnemies);
    u_char* out;
    char line[200];
    int i, n = 0, used, kept = 0;

    lab_slots = 0;
    if (!lab.armed || _zoneContext.zndId != lab.zone || _zoneContext.mapId != lab.room)
        return;
    lab.armed = 0;
    ++arrivals;

    /* The room's own enemy records, for reference (where the game puts them). */
    for (i = 0; i < (int)_zoneContext.mpdEnemyCount; ++i) {
        u_char* r = (u_char*)&old[i];
        if (r[R_TYPE] == 0) {
            sprintf(line, "[encounter] room record %d: id %d at (%d, %d) height %d, floor %d", i, r[R_ID],
                r[R_X], r[R_Z], r[R_HEIGHT], floor_at(r[R_X], r[R_Z]));
            pc_log(line);
        }
    }

    table = vs_main_allocHeap(TABLE * RECORD);
    vs_main_bzero(table, TABLE * RECORD);
    out = (u_char*)table;
    used = sprintf(status, "\"zone\":%d,\"room\":%d,\"arrival\":%d,\"rejected\":[", lab.zone, lab.room, arrivals);
    for (i = 0; i < lab.count && n < TABLE; ++i) {
        u_char* r = out + n * RECORD;
        const u_char* want = lab.records[i];
        int floor = floor_at(want[R_X], want[R_Z]);
        const char* why = NULL;
        if (want[R_TYPE] != 0)
            why = "not an enemy record";
        else if (want[R_ID] >= _zoneContext.zndEnemyCount)
            why = "no such enemy in this zone";
        else if (floor == 0x7FFF)
            why = "tile blocked or off the floor";
        if (why != NULL) {
            used += sprintf(status + used, "%s{\"enemy\":%d,\"why\":\"%s\"}", status[used - 1] != '[' ? "," : "", i, why);
            continue;
        }
        vs_main_memcpy(r, (void*)want, RECORD);
        r[R_INDEX] = n;
        r[R_HIDDEN] = 0;
        r[R_SLOT] = 0; /* the first free enemy slot */
        r[R_FLAG_TEST] = r[R_STAT_TEST] = r[R_SCENE_TEST] = 0; /* always there */
        ++n;
    }
    /* The room's objects stay (their saved state is by object, not record). */
    for (i = 0; i < (int)_zoneContext.mpdEnemyCount && n < TABLE; ++i) {
        u_char* r = (u_char*)&old[i];
        if (r[R_TYPE] != 0) {
            vs_main_memcpy(out + n * RECORD, r, RECORD);
            ++n, ++kept;
        }
    }
    if (old != NULL)
        vs_main_freeHeap(old);
    psx_set(_zoneContext.mpdEnemies, table);
    _zoneContext.mpdEnemyCount = n;

    /* No memory of this room's enemies: fresh positions, none defeated. */
    vs_main_bzero(D_80061078[D_80060064].unk8, sizeof D_80061078[D_80060064].unk8);

    if (lab.seeded)
        seed_random(lab.seed);
    used += sprintf(status + used, "],\"enemies\":%d,\"objects_kept\":%d,\"seed\":", n - kept, kept);
    if (lab.seeded)
        sprintf(status + used, "%u", lab.seed);
    else
        sprintf(status + used, "null");
    sprintf(line, "[encounter] zone %d room %d: %d enemies, %d objects kept%s", lab.zone, lab.room, n - kept, kept,
        lab.seeded ? ", seeded" : "");
    pc_log(line);
    slots_before = actor_slots();
}

/* pc_room_warp_room_spawned: which actor slots the spawn filled (objects
 * aside; Merlose's is among them: RPC state says which is hers). */
void pc_encounter_room_spawned(void)
{
    char line[120];
    int i;
    if (status[0] == 0 || slots_before == 0xFFFF)
        return;
    lab_slots = actor_slots() & ~slots_before;
    for (i = 0; i < 16; ++i)
        if ((lab_slots & (1 << i)) && vs_battle_actors[i]->unk1C == 0x10)
            lab_slots &= ~(1 << i); /* objects */
    sprintf(line, "[encounter] spawned in slots 0x%04X", lab_slots);
    pc_log(line);
    slots_before = 0xFFFF;
}

/* RPC encounter's reply: what is armed and the last arrival (spawned: its
 * enemies exist; the room loads their models between the two hooks). */
int pc_encounter_status(char* out, int size)
{
    if (size < 900)
        return 0;
    return sprintf(out,
        "{\"armed\":%s,\"target\":[%d,%d],\"count\":%d,\"spawned\":%s,\"slots\":%d,\"last\":%s%s%s}",
        lab.armed ? "true" : "false", lab.zone, lab.room, lab.count,
        status[0] && slots_before == 0xFFFF ? "true" : "false", lab_slots, status[0] ? "{" : "null", status,
        status[0] ? "}" : "");
}
