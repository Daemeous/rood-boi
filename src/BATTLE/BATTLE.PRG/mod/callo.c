/* Callo Merlose companion mod.
 *
 * Decomp edition (`make CALLO=1`): linked into BATTLE.PRG and called from the game's own code
 * (see hooks.h for the call sites). Patch edition (tools/mod/build_mod.sh on callo-preview): built
 * into its own file on the disc (ENDING/CALLO.BIN) and loaded into free RAM above BATTLE.PRG,
 * with vs_mod_spawnCallo first in this file as its entry point.
 */
#include "common.h"
#include "../146C.h"
#include "../2842C.h"
#include "../30DB0.h"
#include "callo.h"
#include "hooks.h"

void vs_mod_spawnCallo(void);
/* AI state setup / teardown (6E644.c, not yet decompiled) */
int func_800E6178(vs_battle_actor*, int);
void func_800E7454(vs_battle_actor*);
void func_8009DF3C(int, int);
/* Per-slot AI states (0x484 bytes each). +0x1A4 + id * 4 is this AI's attitude to actor id:
 * top two bits of the first byte, 3 = hostile (attack candidate), 2 = friendly, 1 = ignore. */
extern u_char* D_800F5878[16];
#define AI_ATTITUDES 0x1A4

static vs_battle_charInitData _calloInitData;
static vs_battle_weaponIntermediate _vkpWeapon;
static void _makeEnemiesHostile(int calloSlot);
static void _copyAshleyBaseStats(vs_battle_actor2* callo);
static void _applyLoadout(vs_battle_actor2* callo);
static void _giveStartingArmor(void);
static void _giveStartingWeapon(void);
static void _placeBesideAshley(vs_battle_objectData_flags* position);

#ifndef CALLO_MODEL
#define CALLO_MODEL 8
#endif
/* Spawn param bits 0-1 = 1: hostile spawn (team 4, weapon drawn); also makes
 * func_800771E0 load the weapon-type animation set for slots >= 2. */
#define CALLO_SPAWN_PARAM 1
/* AI mode lives in bits 21-22 of vs_battle_actor.unk38 (copied from the template's
 * int at 0x2C for room spawns). Mode 3 is Sydney's: fight the player's enemies. This
 * is Sydney's full value; the other bits are the usual humanoid AI parameters. */
#define CALLO_AI_PARAMS 0xC07C7F1C
#define CALLO_ENTRY_INDEX 15
#define CALLO_WEAPON_MODEL 83    /* Gastraph Bow */
#define CALLO_WEAPON_MATERIAL 3  /* Bronze */

/* Her actor slot, in order of preference: room scripts move characters by slot, 7 and 6 least
 * and 2-4 most (Wine Cellar count), mostly in cutscenes, which she sits out. Rooms with
 * movable objects can fill 6 and 7 (team 16), hence the others. Never 9: see below. */
static const u_char _calloSlots[] = { 7, 6, 8, 5, 4, 3, 2 };
/* She joins once the prologue is over: the progress flag the game also gates loot and the
 * bonus wheel on (set to 3 early in the Wine Cellar). The prologue's cutscene characters need
 * her slots' palette rows (VRAM row 0x16 + slot): sharing them corrupted their textures. */
#define PROGRESS_FLAG 0xAB
#define PROLOGUE_OVER 3
char vs_battle_getStateFlag(short);
extern u_char _awayForScene; /* merlose.h */
/* Word-sized statics need an initialiser: that puts them in .data, which is aligned. maspsx
 * packs uninitialised (.bss) statics byte by byte, as ASPSX did, so an int there can land on
 * an odd address (DuckStation then rounds the access down onto a neighbouring variable). */
int _calloSlot = 0; /* where she last spawned (0: not yet) */
/* The room loaded during the prologue: the Wine Cellar's first room sets the flag in its
 * opening scene, after the load, so she joins there once it is set (vs_mod_frame). */
u_char _lateJoin;

void vs_mod_spawnCallo(void)
{
    vs_battle_objectData_flags position;
    _mpdEnemy* anchor;
    vs_battle_actor* callo;
    u_char* raw;
    int i;
    int slot;
    vs_main_inventoryWeapon* weapon;
    vs_main_inventoryBlade* blade;
    vs_main_inventoryShield* shield;

    _lateJoin = 0;
    if (vs_battle_getStateFlag(PROGRESS_FLAG) < PROLOGUE_OVER) {
        _lateJoin = 1;
        return;
    }
    if (_awayForScene) {
        return;
    }

    /* An enemy slot (0-1 get player-only handling, creation allows < 10), the first free one
     * in _calloSlots. Slot 9 is unusable: a character's palette goes to VRAM row 0x16 + slot,
     * and row 0x1F (slot 9) holds room objects' alternate palettes. */
    for (i = 0; i < sizeof _calloSlots; ++i) {
        slot = _calloSlots[i];
        if (vs_battle_actors[slot] == NULL) {
            break;
        }
    }
    if (i >= sizeof _calloSlots) {
        return;
    }

    /* Next to Ashley: his tile is already set for the new room at this point.
     * Live tile block is {x, facing, z, height}, the same layout as the spawn flags. */
    *(int*)&position = *(int*)((u_char*)D_800F4538[0] + 0x5C);
    _placeBesideAshley(&position);

    _dropSharedItems();
    _giveStartingArmor();
    _giveStartingWeapon();
    shield = _loadoutShield();
    weapon = _loadoutWeapon();
    if (weapon != NULL) {
        blade = &vs_main_inventory.blades[weapon->blade - 1];
        callo = func_800774FC(slot, CALLO_MODEL, blade->wepId, blade->material,
            shield ? shield->base.wepId : 0, shield ? shield->base.material : 0, &position,
            CALLO_SPAWN_PARAM);
    } else {
        /* No weapon: bare-handed */
        callo = func_800774FC(slot, CALLO_MODEL, 0, 0,
            shield ? shield->base.wepId : 0, shield ? shield->base.material : 0, &position,
            CALLO_SPAWN_PARAM);
    }
    if (callo == NULL) {
        return;
    }
    _calloSlot = slot;
    /* No 0x80: it sets actor->unk26, which room entries use for characters that can
     * only die by script (Grissom, Sydney), and made her immortal */
    func_80076F24(slot, &_calloInitData, 0x10, 0, CALLO_SPAWN_PARAM, 0);

    _copyAshleyBaseStats(psx_get(callo->unk3C));
    _applyLoadout(psx_get(callo->unk3C));
    _learnSpells(psx_get(callo->unk3C));
    psx_get(callo->unk3C)->isAnalyzed = 1; /* status screen shows her stats, not "???" */

    /* Fields func_800775C0 (enemy room spawn) copies from the room entry and Ashley's
     * spawn path leaves zeroed. Settings byte 0 matches the entry's index, so use an
     * index no real entry has, to keep saved room state (defeated/hidden) untouched. */
    raw = (u_char*)callo;
    if (_zoneContext.mpdEnemyCount != 0) {
        anchor = &psx_get(_zoneContext.mpdEnemies)[0];
        for (i = 0; i < 8; ++i) {
            raw[0x30 + i] = MPD_ENEMY_SETTINGS(anchor)[i];
        }
        callo->unk1F = MPD_ENEMY_UNK26(anchor);
    }
    raw[0x30] = CALLO_ENTRY_INDEX;
    callo->unk1E = 2; /* room entry team byte for a hostile placement */
    callo->unk27 = CALLO_ENTRY_INDEX;
    callo->defeated = 0;
    callo->unk29 = 0;

    /* Team 4 with weapon drawn comes from CALLO_SPAWN_PARAM, like Sydney's room spawn;
     * the AI mode below makes her fight the other enemies */
    /* The AI state is built from unk30-unk38 by func_800E6178, which the creation path
     * already ran with unk38 = 0 (mode 0). Room spawns run it last; rebuild it the same
     * way: tear down (as _removeActorAtIndex does) while she is still on the team the
     * creation path registered her under, change her fields, then initialise. */
    func_800E7454(callo);
#ifndef CALLO_NO_AI
    callo->unk38 = CALLO_AI_PARAMS;
#endif
    /* She must stay on the enemy team (4): team 1 is driven by the controller (no AI)
     * and crashed when entering/leaving rooms with enemies (tests L1/L2). */
    func_8009DF3C(slot, func_800E6178(callo, -1) ? 0 : callo->unk1F);
    _makeEnemiesHostile(slot);
}

/* Where she appears: a tile beside Ashley she can stand on, and not in front of him (where he is
 * heading and enemies are). The room's collision map gives a point's floor height, with bit 15
 * set where it is blocked (wall, outside the map); one whose floor is more than a step from his
 * is a ledge or a pit. Before, the tile left of him was the only choice: on a narrow ledge (the
 * Minotaur's arena) that put her inside a wall. */
short func_8008DC7C(int x, int z);

#define FLOOR_BLOCKED 0x7FFF
#define SPAWN_STEP 64 /* floor height difference allowed, in model units */

/* Tiles to try, as (side, ahead) from where he faces: beside him, behind, behind diagonally, two
 * away, and in front only when nothing else will do */
static const signed char _spawnOffsets[][2] = {
    { 1, 0 }, { -1, 0 }, { 0, -1 }, { 1, -1 }, { -1, -1 }, { 2, 0 }, { -2, 0 }, { 0, -2 },
    { 1, 1 }, { -1, 1 }, { 0, 1 }, { 0, 2 },
};

static int _floorAt(int tileX, int tileZ)
{
    int height = func_8008DC7C((tileX << 7) | 64, (tileZ << 7) | 64);

    if (height & 0x8000) {
        return FLOOR_BLOCKED;
    }
    return (height << 17) >> 17;
}

/* Whether she could stand on a tile: inside the map, not blocked, within a step of `floor` */
static int _standable(int tileX, int tileZ, int floor)
{
    int height;

    if ((tileX < 0) || (tileZ < 0) || (tileX > 255) || (tileZ > 255)) {
        return 0;
    }
    height = _floorAt(tileX, tileZ);
    return (height != FLOOR_BLOCKED) && (height - floor <= SPAWN_STEP)
        && (floor - height <= SPAWN_STEP);
}

/* Open floor around a tile, not counting Ashley's: a doorway, alcove or corridor has at most two
 * ways out, and her standing there can wall him in (seen at the Minotaur's sealed door, where she
 * came back into the alcove behind him and neither could get past) */
#define OPEN_TILE 3 /* standable neighbours, of 8 */

static int _openAround(int tileX, int tileZ, int ashleyX, int ashleyZ, int floor)
{
    int open = 0;
    int dx;
    int dz;

    for (dx = -1; dx <= 1; ++dx) {
        for (dz = -1; dz <= 1; ++dz) {
            if (((dx != 0) || (dz != 0))
                && ((tileX + dx != ashleyX) || (tileZ + dz != ashleyZ))
                && _standable(tileX + dx, tileZ + dz, floor)) {
                ++open;
            }
        }
    }
    return open;
}

/* The tile direction Ashley faces. His facing (0-4095) points away from where he walks
 * (measured: facing 0 walks towards -z, 1024 towards -x): ahead is (-sin, -cos), to the nearest
 * quarter turn. */
static void _aheadOfAshley(int* aheadX, int* aheadZ)
{
    int quarter = ((D_800F4538[0]->unk0.facing + 512) >> 10) & 3;

    *aheadX = (quarter == 1) ? -1 : (quarter == 3) ? 1 : 0;
    *aheadZ = (quarter == 0) ? -1 : (quarter == 2) ? 1 : 0;
}

static void _placeBesideAshley(vs_battle_objectData_flags* position)
{
    int x = position->unk0_0;
    int z = position->unk0_16;
    int floor = _floorAt(x, z);
    int aheadX;
    int aheadZ;
    int firstX = -1;
    int firstZ = -1;
    int tileX;
    int tileZ;
    int i;

    _aheadOfAshley(&aheadX, &aheadZ);
    /* The first tile in order with open floor around it; else the first she can stand on */
    for (i = 0; (floor != FLOOR_BLOCKED) && (i < sizeof _spawnOffsets / sizeof _spawnOffsets[0]); ++i) {
        /* side is ahead turned a quarter: (-aheadZ, aheadX) */
        tileX = x - _spawnOffsets[i][0] * aheadZ + _spawnOffsets[i][1] * aheadX;
        tileZ = z + _spawnOffsets[i][0] * aheadX + _spawnOffsets[i][1] * aheadZ;
        if (!_standable(tileX, tileZ, floor)) {
            continue;
        }
        if (_openAround(tileX, tileZ, x, z, floor) >= OPEN_TILE) {
            position->unk0_0 = tileX;
            position->unk0_16 = tileZ;
            return;
        }
        if (firstX < 0) {
            firstX = tileX;
            firstZ = tileZ;
        }
    }
    if (firstX >= 0) {
        position->unk0_0 = firstX;
        position->unk0_16 = firstZ;
        return;
    }
    /* Nothing better known: left of him, as before */
    position->unk0_0 = x + ((x != 0) ? -1 : 1);
}

/* The loadout's weapon, if it is still in its slot and is a type she can animate */
vs_main_inventoryWeapon* _loadoutWeapon(void)
{
    u_char* loadout = LOADOUT;
    vs_main_inventoryWeapon* weapon;
    vs_main_inventoryBlade* blade;
    int slot = loadout[loadoutWeapon];

    if ((loadout[loadoutVersion] != LOADOUT_VERSION) || (slot == 0) || (slot > 8)) {
        return NULL;
    }
    weapon = &vs_main_inventory.weapons[slot - 1];
    if (weapon->blade == 0) {
        return NULL;
    }
    blade = &vs_main_inventory.blades[weapon->blade - 1];
    if ((blade->id != loadout[loadoutWeapon + 1])
        || !(CALLO_WEAPON_CATEGORIES & (1 << blade->category))) {
        return NULL;
    }
    return weapon;
}

/* The loadout's shield, if it is still in its slot */
vs_main_inventoryShield* _loadoutShield(void)
{
    u_char* loadout = LOADOUT;
    int slot = loadout[loadoutShield];

    if ((loadout[loadoutVersion] != LOADOUT_VERSION) || (slot == 0) || (slot > 8)
        || (vs_main_inventory.shields[slot - 1].base.id != loadout[loadoutShield + 1])) {
        return NULL;
    }
    return &vs_main_inventory.shields[slot - 1];
}

/* Her default armour isn't in the inventory, so the menus showed her with none. Each armour
 * slot still on the default (0) gets a copy of the piece Ashley wears there, as a real item,
 * if the inventory has room. Slots emptied on purpose (LOADOUT_EMPTY) stay empty. */
static void _giveStartingArmor(void)
{
    u_char* loadout = LOADOUT;
    vs_main_inventoryArmor* armor = vs_main_inventory.armor;
    int part;
    int from;
    int to;
    int entry;

    if (loadout[loadoutVersion] != LOADOUT_VERSION) {
        for (part = 0; part <= loadoutAccessory + 1; ++part) {
            loadout[part] = 0;
        }
        loadout[loadoutVersion] = LOADOUT_VERSION;
    }
    for (part = 0; part < 6; ++part) {
        if (loadout[loadoutArmor + part * 2] != 0) {
            continue;
        }
        for (from = 0; (from < 16) && (armor[from].limb != part + 1); ++from)
            ;
        for (to = 0; (to < 16) && (armor[to].id != 0); ++to)
            ;
        for (entry = 0; (entry < 16) && (vs_main_inventoryIndices.armor[entry] != 0); ++entry)
            ;
        if ((from >= 16) || (to >= 16) || (entry >= 16)) {
            continue;
        }
        armor[to] = armor[from];
        armor[to].limb = 0;
        armor[to].index = to + 1;
        vs_main_inventoryIndices.armor[entry] = to + 1;
        loadout[loadoutArmor + part * 2] = to + 1;
        loadout[loadoutArmor + part * 2 + 1] = armor[to].id;
    }
}

/* Her default weapon (VKP Standard Issue) as a real inventory weapon, equipped on her: a blade
 * and a grip assembled into a weapon, as the game stores one. Done once (LOADOUT_BOW_GIVEN), so
 * an empty weapon slot later doesn't keep making copies; she is then bare-handed. */
static void _giveStartingWeapon(void)
{
    u_char* loadout = LOADOUT;
    vs_main_inventoryWeapon* weapon;
    int w;
    int b;
    int g;
    int i;

    if ((loadout[loadoutFlags] & LOADOUT_BOW_GIVEN) || (loadout[loadoutWeapon] != 0)) {
        return;
    }
    for (w = 0; (w < 8) && (vs_main_inventory.weapons[w].blade != 0); ++w)
        ;
    for (b = 0; (b < 16) && (vs_main_inventory.blades[b].id != 0); ++b)
        ;
    for (g = 0; (g < 16) && (vs_main_inventory.grips[g].id != 0); ++g)
        ;
    if ((w >= 8) || (b >= 16) || (g >= 16)) {
        return; /* no room: try again at the next spawn */
    }

    vs_battle_copyUiBladeStats(&vs_main_inventory.blades[b], &_vkpWeapon.blade);
    vs_main_inventory.blades[b].index = b + 1;
    vs_main_inventory.blades[b].assembledWeaponIndex = w + 1;
    vs_battle_copyUiGripStats(&vs_main_inventory.grips[g], &_vkpWeapon.grip);
    vs_main_inventory.grips[g].index = g + 1;
    vs_main_inventory.grips[g].assembledWeaponIndex = w + 1;

    weapon = &vs_main_inventory.weapons[w];
    weapon->index = w + 1;
    weapon->blade = b + 1;
    weapon->grip = g + 1;
    weapon->isEquipped = 0;
    for (i = 0; i < 4; ++i) {
        weapon->gems[i] = 0;
    }
    for (i = 0; i < 24; ++i) {
        weapon->name[i] = _vkpWeapon.name[i];
    }
    for (i = 0; (i < 8) && (vs_main_inventoryIndices.weapons[i] != 0); ++i)
        ;
    if (i < 8) {
        vs_main_inventoryIndices.weapons[i] = w + 1;
    }

    loadout[loadoutWeapon] = w + 1;
    loadout[loadoutWeapon + 1] = vs_main_inventory.blades[b].id;
    loadout[loadoutFlags] |= LOADOUT_BOW_GIVEN;
}

/* An item can't be on both characters; loadouts from before the MERLOSE menu could share one.
 * Ashley keeps it. Called while his "equipped" markers are in place. */
void _dropSharedItems(void)
{
    u_char* loadout = LOADOUT;
    int slot;
    int i;

    if (loadout[loadoutVersion] != LOADOUT_VERSION) {
        return;
    }
    slot = loadout[loadoutWeapon];
    if ((slot != 0) && (slot <= 8) && vs_main_inventory.weapons[slot - 1].isEquipped) {
        loadout[loadoutWeapon] = 0;
    }
    slot = loadout[loadoutShield];
    if ((slot != 0) && (slot <= 8) && vs_main_inventory.shields[slot - 1].isEquipped) {
        loadout[loadoutShield] = 0;
    }
    /* The six armour pieces, then the accessory (loadoutAccessory follows them) */
    for (i = 0; i < 7; ++i) {
        slot = loadout[loadoutArmor + i * 2];
        if ((slot != 0) && (slot <= 16) && vs_main_inventory.armor[slot - 1].limb) {
            loadout[loadoutArmor + i * 2] = 0;
        }
    }
}

/* Equip her loadout on top of the template's gear (bare hands, starting armour), the
 * way INITBTL.PRG equips Ashley. Pieces that are unset or no longer valid keep the template's. */
static void _applyLoadout(vs_battle_actor2* callo)
{
    u_char* loadout = LOADOUT;
    vs_main_inventoryWeapon* weapon;
    vs_main_inventoryArmor* armor;
    int slot;
    int i;

    if (loadout[loadoutVersion] != LOADOUT_VERSION) {
        return;
    }

    weapon = _loadoutWeapon();
    if (weapon != NULL) {
        vs_battle_applyWeapon(&callo->weapon, weapon);
        callo->currentRange = callo->weapon.range.range + callo->reach;
        callo->unk39 = callo->weapon.range.unk1 + callo->reach;
        callo->unk3A = callo->weapon.range.unk2 + callo->reach;
        callo->currentAttackShapeAngle = callo->weapon.range.attackShapeAngle;
    }

    if (_loadoutShield() != NULL) {
        vs_battle_applyShield(&callo->shield, _loadoutShield());
    }

    for (i = 0; i < 6; ++i) {
        slot = loadout[loadoutArmor + i * 2];
        if (slot == LOADOUT_EMPTY) {
            vs_battle_applyArmor(&callo->limbs[i].armor, NULL);
        } else if ((slot != 0) && (slot <= 16)) {
            armor = &vs_main_inventory.armor[slot - 1];
            if (armor->id == loadout[loadoutArmor + i * 2 + 1]) {
                vs_battle_applyArmor(&callo->limbs[i].armor, armor);
            }
        }
    }

    slot = loadout[loadoutAccessory];
    if (slot == LOADOUT_EMPTY) {
        vs_battle_applyAccessory(&callo->accessory, NULL);
    } else if ((slot != 0) && (slot <= 16)) {
        armor = &vs_main_inventory.armor[slot - 1];
        if (armor->id == loadout[loadoutAccessory + 1]) {
            vs_battle_applyAccessory(&callo->accessory, armor);
        }
    }
}

/* She knows the spells Ashley has learned, and the player picks which of them she may cast in
 * the MAGIC menu (Square toggles, see merlose.h); the choice is saved in the loadout. Her AI
 * picks from the action table in her stats (6 body parts x 4 actions, entry.index = its slot;
 * the template uses three: an attack on each arm and Heal, which she always keeps), so the
 * chosen spells go in its free entries. Ids are vs_main_actions[]; Ashley's are the ones in the
 * four school lists (several spells exist twice: e.g. Fireball 81 and 99, he learns 99). The AI
 * already casts ids from those lists (Grissom: Clearance, Drain Mind). */
extern u_char vs_battle_warlockSpellIds[];
extern u_char vs_battle_shamanSpellIds[];
extern u_char vs_battle_sorcererSpellIds[];
extern u_char vs_battle_enchanterSpellIds[];

/* The schools in MAGIC menu order, with how many spells each menu lists */
u_char* const _schoolSpells[4] = { vs_battle_warlockSpellIds, vs_battle_shamanSpellIds,
    vs_battle_sorcererSpellIds, vs_battle_enchanterSpellIds };
const u_char _schoolSizes[4] = { 18, 6, 18, 8 };

/* Until the player chooses: attack and cure spells (the AI re-casts buffs constantly, as
 * Sydney did with Prostasia) */
static const u_char _defaultSpells[] = { 98, 99, 100, 101, 97, 87, 88, 91 };

/* Position of a spell in the four lists, or -1 */
int _spellIndex(int id)
{
    int school;
    int i;
    int index = 0;

    for (school = 0; school < 4; ++school) {
        for (i = 0; i < _schoolSizes[school]; ++i, ++index) {
            if (_schoolSpells[school][i] == id) {
                return index;
            }
        }
    }
    return -1;
}

static int _spellId(int index)
{
    int school;

    for (school = 0; school < 4; ++school) {
        if (index < _schoolSizes[school]) {
            return _schoolSpells[school][index];
        }
        index -= _schoolSizes[school];
    }
    return 0;
}

void _initSpellChoice(void)
{
    u_char* loadout = LOADOUT;
    int i;

    if (loadout[loadoutFlags] & LOADOUT_SPELLS_CHOSEN) {
        return;
    }
    for (i = 0; i < (SPELL_COUNT + 7) / 8; ++i) {
        loadout[loadoutSpells + i] = 0;
    }
    for (i = 0; i < sizeof _defaultSpells; ++i) {
        _setSpellChosen(_spellIndex(_defaultSpells[i]), 1);
    }
    loadout[loadoutFlags] |= LOADOUT_SPELLS_CHOSEN;
}

int _spellChosen(int index)
{
    if (index < 0) {
        return 0;
    }
    return (LOADOUT[loadoutSpells + (index >> 3)] >> (index & 7)) & 1;
}

void _setSpellChosen(int index, int chosen)
{
    u_char* bits = &LOADOUT[loadoutSpells + (index >> 3)];

    if (index < 0) {
        return;
    }
    if (chosen) {
        *bits |= 1 << (index & 7);
    } else {
        *bits &= ~(1 << (index & 7));
    }
}

/* Only spells Ashley has learned count towards her limit: the MAGIC menu lists only those, so
 * a chosen spell he hasn't learned yet (the defaults, early on) could never be taken off. */
int _chosenSpellCount(void)
{
    int count = 0;
    int i;

    for (i = 0; i < SPELL_COUNT; ++i) {
        if (_spellChosen(i) && ACTION_UNLOCKED(_spellId(i))) {
            ++count;
        }
    }
    return count;
}

/* Choosing a spell in the MAGIC menu makes the choice the player's: the defaults Ashley hasn't
 * learned yet are dropped, so they can't turn up later on top of the player's 8. */
void _dropUnlearnedSpells(void)
{
    int i;

    for (i = 0; i < SPELL_COUNT; ++i) {
        if (_spellChosen(i) && !ACTION_UNLOCKED(_spellId(i))) {
            _setSpellChosen(i, 0);
        }
    }
}

/* Where spells go in her action table (limb * 4 + slot): not her arms (limbs 0 and 1, which
 * hold her weapon attack). The AI cast buffs only from the other limbs: placed on an arm,
 * Prostasia etc. were never used while attack spells were. Template Heal is limb 3 slot 0. */
static const u_char _spellSlots[] = { 13, 14, 15, 8, 9, 10, 11, 16, 17, 18, 19, 20, 21, 22, 23 };
#define SPELL_SLOTS (sizeof _spellSlots)

/* (Re)fill her action table with the chosen spells Ashley knows. Levelled spells (Explosion
 * etc.) are listed by level 1; she casts the highest level he has. */
void _learnSpells(vs_battle_actor2* callo)
{
    vs_battle_actor_unk8C0* entry = &callo->armor[0][0];
    int learned = 0;
    int index;
    int id;
    int level;
    int j;

    _initSpellChoice();
    for (j = 0; j < 6 * 4; ++j) {
        if ((entry[j].id >= FIRST_SPELL_ID) && (entry[j].id <= LAST_SPELL_ID)
            && (entry[j].id != CALLO_HEAL)) {
            entry[j].id = 0;
        }
    }
    for (index = 0; (index < SPELL_COUNT) && (learned < MAX_CALLO_SPELLS); ++index) {
        id = _spellId(index);
        if (!_spellChosen(index) || (id == CALLO_HEAL) || !ACTION_UNLOCKED(id)) {
            continue;
        }
        if ((index < 18) && (id >= 106) && (id <= 130)) { /* warlock, 4 levels each */
            for (level = 0; (level < 3) && ACTION_UNLOCKED(id + 1); ++level) {
                ++id;
            }
        }
        for (j = 0; (j < SPELL_SLOTS) && (entry[_spellSlots[j]].id != 0); ++j)
            ;
        if (j >= SPELL_SLOTS) {
            return;
        }
        j = _spellSlots[j];
        entry[j].id = id;
        entry[j].index = j & 3;
        entry[j].unk2_4 = 0;
        entry[j].unk3 = 1;
        ++learned;
    }
}

/* She grows with Ashley, as his counterpart: copy his base stats (total* = permanent values
 * incl. stat-up items, without temporary buffs; equipment is never part of these, it is added
 * at hit time) with STR and INT swapped, so a strength-built Ashley gets a caster Callo and
 * vice versa. Full HP/MP at spawn; her own gear then applies through the normal formulas. */
static void _copyAshleyBaseStats(vs_battle_actor2* callo)
{
    vs_battle_actor2* ashley = psx_get(vs_battle_actors[0]->unk3C);

    callo->maxHP = callo->currentHP = ashley->maxHP;
    callo->maxMP = callo->currentMP = ashley->maxMP;
    callo->strength = callo->totalStrength = ashley->totalIntelligence;
    callo->intelligence = callo->totalIntelligence = ashley->totalStrength;
    callo->agility = callo->totalAgility = ashley->totalAgility;
}

/* Enemies regard Callo as a teammate (she is on team 4), so they never pick her as a target.
 * Give every enemy AI the same attitude towards her as towards Ashley (actor 0): verified
 * live that they then fight back when she attacks them, and otherwise go for Ashley. */
static void _makeEnemiesHostile(int calloSlot)
{
    int i;
    u_char* ai;

    for (i = 2; i < 16; ++i) {
        if ((i == calloSlot) || (vs_battle_actors[i] == NULL)
            || (vs_battle_actors[i]->unk1C != 4)) {
            continue;
        }
        ai = D_800F5878[i];
        if (ai != NULL) {
            ai[AI_ATTITUDES + calloSlot * 4] = ai[AI_ATTITUDES];
        }
    }
}


/* Callo Merlose companion (mod). Stats template modelled on Ashley's (INITBTL.PRG),
 * bare-handed so her BTA animation set is used, with Sydney's Heal. */
#pragma vsstring(start)
static vs_battle_charInitData _calloInitData = {
    .unk2         = 8,
    .reach        = 7,
    .name         = "Callo|>6|Merlose\0",
    /* Placeholders: replaced by Ashley's base stats at spawn (_copyAshleyBaseStats) */
    .hp           = 200,
    .mp           = 120,
    .strength     = 80,
    .intelligence = 120,
    .agility      = 100,
    .unk23        = 2,
    .unk24        = 2,
    .walkSpeed    = 10,
    .unk26        = 4,
    .runSpeed     = 20,
    .unk30        = 4,
    .unk32        = 3,
    .unk28        = 0x6009D2BF, /* Sydney's value (unique to him); purpose unknown */
    .accessory = {
        .equip = {
            .id           = 223,
            .subId        = 97,
            .category     = 7,
            .intelligence = 1,
            .classes      = {0, 0, 5, -5, -5, 5},
            .affinities   = {5, 0, 0, 0, 0, 5, -5, 0},
        },
    },
    .limbs = {
        {
            .hp = 200,
            .actions = {
                {.id = 1},
                {.index = 1},
                {.index = 2},
                {.index = 3},
            },
            .unk20 = {
                .equip = {
                    .id           = 191,
                    .subId        = 65,
                    .category     = 5,
                    .strength     = 1,
                    .intelligence = 13,
                    .currentDp    = 3350,
                    .maxDp        = 3350,
                    .types        = {0, 1, 0, 0},
                    .affinities   = {2, 5, 5, -1, -1, -5, -5, 0},
                },
                .material = 2,
                .unk32    = 1,
            },
            .dpSplash = {100, 0, 0, 5, 0, 0},
        },
        {
            .hp = 200,
            .actions = {
                {.id = 1},
                {.index = 1},
                {.index = 2},
                {.index = 3},
            },
            .unk20 = {
                .equip = {
                    .id           = 191,
                    .subId        = 65,
                    .category     = 5,
                    .strength     = 1,
                    .intelligence = 13,
                    .currentDp    = 3350,
                    .maxDp        = 3350,
                    .types        = {0, 1, 0, 0},
                    .affinities   = {2, 5, 5, -1, -1, -5, -5, 0},
                 },
                .material = 2,
                .unk32    = 1,
            },
            .dpSplash = {0, 100, 0, 5, 0, 0},
        },
        {
            .hp = 200,
            .actions = {
                {0},
                {.index = 1},
                {.index = 2},
                {.index = 3},
            },
            .unk20 = {
                .equip = {
                    .id           = 143,
                    .subId        = 17,
                    .category     = 2,
                    .strength     = 1,
                    .intelligence = 8,
                    .currentDp    = 3800,
                    .maxDp        = 3800,
                    .affinities = {2, 5, 5, -1, -1, -5, -5, 0},
                },
                .material = 2,
                .unk32    = 1,
            },
            .dpSplash = {0, 0, 100, 10, 0, 0},
        },
        {
            .hp = 200,
            .actions = {
                {.id = 0x60}, /* Heal */
                {.index = 1}, /* Sydney's Prostasia (0x3F) removed: re-cast constantly */
                {.index = 2},
                {.index = 3},
            },
            .unk20 = {
                .equip = {
                    .id           = 159,
                    .subId        = 33,
                    .category     = 3,
                    .strength     = 5,
                    .intelligence = 10,
                    .currentDp    = 5500,
                    .maxDp        = 5500,
                    .types        = {0, 1, 1, 0},
                    .affinities   = {2, 5, 5, -1, -1, -5, -5, 0},
                },
                .material = 2,
                .unk32 = 1,
            },
            .dpSplash = {5, 5, 5, 100, 5, 0},
        },
        {
            .hp = 200,
            .actions = {
                {0},
                {.index = 1},
                {.index = 2},
                {.index = 3},
            },
            .unk20 = {
                .equip = {
                    .id           = 175,
                    .subId        = 49,
                    .category     = 4,
                    .strength     = 1,
                    .intelligence = 12,
                    .currentDp    = 4050,
                    .maxDp        = 4050,
                    .affinities   = {2, 5, 5, -1, -1, -5, -5, 0},
                },
                .material = 2,
                .unk32    = 1,
            },
            .dpSplash = {0, 0, 0, 5, 100, 0},
        },
        {
            .actions = {
                {0},
                {.index = 1},
                {.index = 2},
                {.index = 3},
            },
        },
    },
};

/* Her starting weapon, put in the inventory by _giveStartingWeapon. "VKP Standard Issue": a copy
 * of Seventh Heaven (The Reckoning Room's chest) - Bronze Gastraph Bow + Simple Bolt, stats as
 * stored in that chest (MAP018 loot). */
static vs_battle_weaponIntermediate _vkpWeapon = {
    .blade = {
        .id           = 83,
        .subId        = 83,
        .wepId        = CALLO_WEAPON_MODEL,
        .category     = 9, /* crossbow: her BT9 animation set */
        .strength     = 11,
        .intelligence = 1,
        .agility      = -4,
        .currentDp    = 6600,
        .maxDp        = 6600,
        .maxPp        = 150,
        .damageType   = 3,
        .costType     = 2,
        .cost         = 2,
        .material     = CALLO_WEAPON_MATERIAL,
        .range        = {
            .range       = 9,
            .unk1        = 8,
            .unk2        = 9,
            .attackShape = 1,
        },
        .classes      = {-1, -1, -2, -1, -1, -5, 0, 0},
        .affinities   = {8, 5, 5, -3, -3, 2, 2, 0},
    },
    .grip = {
        .id           = 121, /* Simple Bolt */
        .subId        = 26,
        .category     = 4,
        .strength     = 1,
        .agility      = -1,
        .types        = {0, 1, 0, 10},
    },
    .material = CALLO_WEAPON_MATERIAL,
    .name     = "VKP|>6|Standard|>6|Issue\0",
};

/* The MERLOSE row of ITEMS > OPTIONS (see merlose.h) */
static char _merloseRow[] = "MERLOSE\0";
static char _merloseRowDesc[] = "Equip|>6|Merlose\0";
static char _merloseRowAbsent[] = "Merlose|>6|is|>6|not|>6|here\0";
#pragma vsstring(end)

#include "merlose.h"
