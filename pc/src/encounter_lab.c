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
 * cleared, and a seed makes the game's random numbers repeat. Their models
 * load for as many frames as the disc takes, so the fight's start is the
 * first frame they are all in the room (ready): the seed is set again there
 * and Merlose's interval timers restart, so a seeded fight repeats frame for
 * frame whatever the loading took.
 *
 * Also here: the fight trace's view of the game (pc_fight_snapshot, the file is
 * fight_trace.c's) and Ashley setups for a test (pc_ashley_setup), both after
 * JEschete's VS-NativePort Combat Lab (CC0-1.0). */
#include "common.h"
#include <stdio.h>
#include "../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../src/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../src/SLUS_010.40/main.h"

extern void pc_log(const char*);
extern int randArr[97];
extern int randIndex;
short func_8008DC7C(int x, int z);
void srand(unsigned int);
void vs_mod_reset_timers(void) __attribute__((weak)); /* mod/merlose.h, the Merlose build */
int pc_battle_action(int* actor, int* action, int* phase, int* chain, int* type, int* plain, int aim[3],
    int* target); /* scene_submit.inc */

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
static int ready = 1; /* the arrival's characters are all in (0: waiting for their models) */

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
 * from a seed, as vs_main_initRand fills it from rand(), and rand() itself,
 * which refills the table as it is used. */
static void seed_random(u_int seed)
{
    int i;
    for (i = 0; i < 97; ++i) {
        seed = seed * 1103515245u + 12345u;
        randArr[i] = (seed >> 16) & 0x7FFF;
    }
    randIndex = 96;
    srand(seed);
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
    ready = 0;
}

/* Every frame (fight_trace.c's vsync hook): the first frame the arrival's
 * characters (its enemies and Merlose) all have their models in the room. */
void pc_encounter_vsync(void)
{
    char line[80];
    int i;
    if (ready)
        return;
    for (i = 0; i < 16; ++i)
        if ((lab_slots & (1 << i)) && (vs_battle_actors[i] == NULL || D_800F4538[i] == NULL))
            return;
    ready = 1;
    if (lab.seeded) {
        seed_random(lab.seed);
        if (vs_mod_reset_timers)
            vs_mod_reset_timers();
    }
    sprintf(line, "[encounter] all in%s", lab.seeded ? ", seeded again" : "");
    pc_log(line);
}

/* The fight trace's view of one frame (fight_trace.c writes it): the room,
 * the pad (vs_main_buttonsState held, vs_main_buttonsPressed new), the
 * action under way (pc_battle_action: performer, action id, camera phase,
 * chain step, first target) and each character's numbers (objects aside):
 * slot, HP, MP, Risk, total STR/INT/AGI, statuses, weapon drawn, defeated,
 * position. A slot's name (the game's text, hex) comes with its first line,
 * an action's (vs_main_actions) with the first line it is under way. */
static vs_battle_actor* trace_seen[16];
static int trace_action = -1;

void pc_fight_snapshot_reset(void)
{
    vs_main_bzero(trace_seen, sizeof trace_seen);
    trace_action = -1;
}

static int hex_name(char* out, const char* name)
{
    int k, used = sprintf(out, ",\"name\":\"");
    for (k = 0; k < 24; ++k)
        used += sprintf(out + used, "%02x", (u_char)name[k]);
    out[used++] = '"';
    return used;
}

int pc_fight_snapshot(char* out, int size)
{
    int used, i, actor, action, phase, chain, type, plain, aim[3], target;
    if (size < 16 * 400 + 300)
        return 0;
    used = sprintf(out, "{\"zone\":%d,\"room\":%d,\"held\":%d,\"pressed\":%d,\"action\":", _zoneContext.zndId,
        _zoneContext.mapId, vs_main_buttonsState & 0xFFFF, vs_main_buttonsPressed.all & 0xFFFF);
    if (pc_battle_action(&actor, &action, &phase, &chain, &type, &plain, aim, &target)) {
        used += sprintf(out + used, "{\"actor\":%d,\"id\":%d,\"phase\":%d,\"chain\":%d,\"target\":%d", actor,
            action, phase, chain, target);
        if (action != trace_action && action > 0 && action < 256)
            used += hex_name(out + used, vs_main_actions[action].name);
        trace_action = action;
        out[used++] = '}';
    } else {
        used += sprintf(out + used, "null");
        trace_action = -1;
    }
    used += sprintf(out + used, ",\"actors\":[");
    for (i = 0; i < 16; ++i) {
        vs_battle_actor* a = vs_battle_actors[i];
        vs_battle_actor2* s;
        if (a == NULL || a->unk1C == 0x10 || (s = psx_get(a->unk3C)) == NULL) {
            trace_seen[i] = NULL;
            continue;
        }
        used += sprintf(out + used,
            "%s{\"slot\":%d,\"hp\":%d,\"max_hp\":%d,\"mp\":%d,\"max_mp\":%d,\"risk\":%d,\"str\":%d,\"int\":%d,"
            "\"agi\":%d,\"statuses\":%d,\"drawn\":%d,\"defeated\":%d",
            out[used - 1] == '[' ? "" : ",", i, s->currentHP, s->maxHP, s->currentMP, s->maxMP, s->risk,
            s->totalStrength, s->totalIntelligence, s->totalAgility, s->statuses, a->weaponDrawn, a->defeated);
        if (D_800F4538[i] != NULL)
            used += sprintf(out + used, ",\"pos\":[%d,%d,%d]", D_800F4538[i]->unk0.position.vx,
                D_800F4538[i]->unk0.position.vy, D_800F4538[i]->unk0.position.vz);
        if (trace_seen[i] != a) {
            used += hex_name(out + used, s->name);
            trace_seen[i] = a;
        }
        out[used++] = '}';
    }
    used += sprintf(out + used, "]}");
    return used;
}

/* Ashley setups for a test (VS-NativePort's Combat Lab profiles, CC0: its
 * Native/Game/src/Boot/Encounter.cpp), applied to the running game. They only
 * add, and what they add stays: a save made afterwards keeps it.
 *   1 post-prologue: the Map and Score menus as after the prologue
 *     (introState 3, menuFlags 0x120; 5BF94.c)
 *   2 post-minotaur: that, the first three chain and defense abilities (set
 *     on a free button), the first spell of each class and the first Break
 *     Art of the equipped weapon's family
 *   3 all-spells: post-prologue and every spell of the four classes, each
 *     rank of the ranked ones (four consecutive action ids)
 *   4 all-abilities: post-prologue and the 14 chain and 14 defense abilities
 *   5 all-break-arts: post-prologue and the 40 arts (0xB8..0xDF: four per
 *     weapon family and bare hands; only the equipped family's can be used)
 * stats (max HP, max MP, STR, INT, AGI; 0 leaves one as it is, else 1..999)
 * go to Ashley's record: HP and MP refilled to the new maximum, a base stat
 * set and its total (with equipment) moved by the same amount. */
static int learned;

static void learn(int action)
{
    if (action > 0 && action < 256 && !vs_main_actions[action].unlocked) {
        vs_main_actions[action].unlocked = 1;
        ++learned;
    }
}

static void map_ability(char* buttons, int action)
{
    int i;
    for (i = 0; i < 3; ++i)
        if (buttons[i] == action)
            return;
    for (i = 0; i < 3; ++i)
        if (buttons[i] == 0) {
            buttons[i] = action;
            return;
        }
}

static void learn_abilities(int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        learn(vs_battle_chainAbilityOffsets[i]);
        learn(vs_battle_defenseAbilityOffsets[i]);
    }
    for (i = 0; i < 3; ++i) {
        map_ability(vs_main_settings.mappedChainAbilities, vs_battle_chainAbilityOffsets[i]);
        map_ability(vs_main_settings.mappedDefenseAbilities, vs_battle_defenseAbilityOffsets[i]);
    }
}

/* The four classes' lists (Warlock, Shaman, Sorcerer, Enchanter) lie one
 * after another; each ends at a zero or its room. */
static void learn_spells(int all)
{
    static char* lists[4] = { vs_battle_warlockSpellIds, vs_battle_shamanSpellIds, vs_battle_sorcererSpellIds,
        vs_battle_enchanterSpellIds };
    static const int room[4] = { 20, 8, 20, 8 };
    int c, i, rank;
    for (c = 0; c < 4; ++c)
        for (i = 0; i < (all ? room[c] : 1) && lists[c][i] != 0; ++i) {
            int a = (u_char)lists[c][i];
            learn(a);
            if (all && a >= vs_battle_spell_explosion && a <= vs_battle_spell_meteor
                && (a - vs_battle_spell_explosion) % 4 == 0)
                for (rank = 1; rank < 4; ++rank)
                    learn(a + rank);
        }
}

static void set_stat(u_short* base, u_short* total, int value)
{
    int delta = value - *base;
    *base = value;
    *total = *total + delta < 1 ? 1 : *total + delta;
}

int pc_ashley_setup(int profile, const int stats[5], char* out, int size)
{
    vs_battle_actor2* s;
    int i, category;
    if (size < 300 || profile < 0 || profile > 5)
        return 0;
    for (i = 0; i < 5; ++i)
        if (stats[i] < 0 || stats[i] > 999)
            return 0;
    if (vs_battle_actors[0] == NULL || (s = psx_get(vs_battle_actors[0]->unk3C)) == NULL)
        return sprintf(out, "{\"applied\":false,\"why\":\"Ashley is not in the field\"}");
    learned = 0;
    if (profile > 0) {
        if (vs_main_stateFlags.introState < 3)
            vs_main_stateFlags.introState = 3;
        vs_main_settings.menuFlags |= 0x120;
    }
    if (profile == 2 || profile == 4)
        learn_abilities(profile == 4 ? 14 : 3);
    if (profile == 2) {
        category = vs_battle_characterState->equippedWeaponCategory;
        if (category >= 1 && category <= 10)
            learn(0xB8 + (category - 1) * 4);
    }
    if (profile == 2 || profile == 3)
        learn_spells(profile == 3);
    if (profile == 5)
        for (i = 0; i < 40; ++i)
            learn(0xB8 + i);
    if (stats[0])
        s->maxHP = s->currentHP = stats[0];
    if (stats[1])
        s->maxMP = s->currentMP = stats[1];
    if (stats[2])
        set_stat(&s->strength, &s->totalStrength, stats[2]);
    if (stats[3])
        set_stat(&s->intelligence, &s->totalIntelligence, stats[3]);
    if (stats[4])
        set_stat(&s->agility, &s->totalAgility, stats[4]);
    return sprintf(out,
        "{\"applied\":true,\"profile\":%d,\"learned\":%d,\"hp\":%d,\"max_hp\":%d,\"mp\":%d,\"max_mp\":%d,"
        "\"str\":%d,\"int\":%d,\"agi\":%d}",
        profile, learned, s->currentHP, s->maxHP, s->currentMP, s->maxMP, s->totalStrength, s->totalIntelligence,
        s->totalAgility);
}

/* RPC encounter's reply: what is armed and the last arrival (spawned: its
 * characters exist; ready: their models are in and the fight has begun). */
int pc_encounter_status(char* out, int size)
{
    if (size < 900)
        return 0;
    return sprintf(out,
        "{\"armed\":%s,\"target\":[%d,%d],\"count\":%d,\"spawned\":%s,\"ready\":%s,\"slots\":%d,\"last\":%s%s%s}",
        lab.armed ? "true" : "false", lab.zone, lab.room, lab.count,
        status[0] && slots_before == 0xFFFF ? "true" : "false", status[0] && slots_before == 0xFFFF && ready ? "true" : "false", lab_slots, status[0] ? "{" : "null", status,
        status[0] ? "}" : "");
}
