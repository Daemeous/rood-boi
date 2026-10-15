/* Shared by the resident mod (callo.c, merlose.h) and its menu module (menu.c). */
#pragma once

/* rood-boi (the native PC port) keeps PS1 structures' pointers as 4-byte guest addresses,
 * read and written through these (pc/src/psx_types.h); on the PS1 they are plain accesses. */
#ifndef psx_get
#define psx_get(field) (field)
#define psx_set(field, pointer) ((field) = (pointer))
#define psx_copy(dst, src) ((dst) = (src))
#define psx_to_ptr(T, value) ((T)(value))
#define psx_to_int(T, pointer) ((T)(pointer))
#endif

/* A room's enemy entry (_mpdEnemy) by byte offset: rood-boi names fewer of its fields */
#define MPD_ENEMY_SETTINGS(entry) ((u_char*)(entry) + 0x10) /* settings[8] */
#define MPD_ENEMY_UNK26(entry) (((u_char*)(entry))[0x26])

/* Her loadout lives in save flags 0x420-0x43A (in vs_main_stateFlags.unk400): saved and loaded
 * with the game. No script can address flags >= 0x400, but chests' opened flags are 0x3C0 + the
 * chest's number, and the maps' chests go up to 75 (0x40B), so the first 0x20 are off limits.
 * Older versions kept it at 0x400, where it marked chests 64-75 as opened: _moveOldLoadout.
 * A new game zeroes them, meaning "use the defaults". Each piece is an inventory slot (1-based,
 * 0 = default) plus the item's id, so a slot that was emptied or reused is ignored. */
#define LOADOUT ((u_char*)&vs_main_stateFlags + 0x420)
#define OLD_LOADOUT ((u_char*)&vs_main_stateFlags + 0x400)
#define LOADOUT_VERSION 'C'
#define LOADOUT_EMPTY 0xFF /* armour/accessory slot left empty on purpose (0 = her default) */
enum {
    loadoutVersion = 0,
    loadoutWeapon = 1,    /* slot in weapons[], blade id */
    loadoutShield = 3,    /* slot in shields[], base id */
    loadoutArmor = 5,     /* 6 x (slot in armor[], id), one per body part */
    loadoutAccessory = 17, /* slot in armor[], id */
    loadoutFlags = 19,     /* LOADOUT_*; not cleared with the slots above */
    loadoutSpells = 20     /* bit per spell in the MAGIC lists (_spellIndex): she may cast it */
};
#define LOADOUT_BOW_GIVEN 1 /* VKP Standard Issue has been put in the inventory */
#define LOADOUT_SPELLS_CHOSEN 2 /* loadoutSpells set (else _defaultSpells) */
/* Weapon categories she can use: dagger, sword, axe/mace, crossbow (her own animation sets),
 * and staff, which uses her bare-hands set (she has no 08_BT6) */
#define STAFF_CATEGORY 6
#define UNARMED_CATEGORY 10
#define CALLO_WEAPON_CATEGORIES ((1 << 1) | (1 << 2) | (1 << 4) | (1 << STAFF_CATEGORY) | (1 << 9))
#define CALLO_ANIM_SET(category) (((category) == STAFF_CATEGORY) ? UNARMED_CATEGORY : (category))
extern u_char _calloSpawning; /* the slot she's being created in, 0 otherwise */

/* vs_main_actions[] as it is in memory. Zenith shortens the names to 20 characters, so each
 * entry is 48 bytes instead of 52 and the table starts 4 bytes later (it fixes the game's own
 * code up to match); the fields used here keep their offsets. Tell them apart by where action
 * 2 is. Use these rather than vs_main_actions[]. */
#define ZENITH_ACTIONS (*psx_to_ptr(u_char*, 0x8004BA40) == 2)
#define ACTION(id) \
    psx_to_ptr(u_char*, ZENITH_ACTIONS ? 0x8004B9E0 + (id) * 48 : 0x8004B9DC + (id) * 52)
#define ACTION_UNLOCKED(id) (ACTION(id)[13] >> 7)   /* .unlocked: learned */
#define ACTION_TARGET_KIND(id) (ACTION(id)[2] >> 4) /* .unk2_4 */

/* Spells she may cast (callo.c: _learnSpells; chosen in the MAGIC menu) */
#define MAX_CALLO_SPELLS 8
#define CALLO_HEAL 96
#define SPELL_COUNT (18 + 6 + 18 + 8)
#define FIRST_SPELL_ID 54 /* lowest id in the school lists */
#define LAST_SPELL_ID 140

extern int _calloSlot;
extern u_char _menuMode; /* 0 closed, 1 Ashley's EQUIP, 2 MERLOSE */
extern u_char _respawnPending;
extern u_char* const _schoolSpells[4];
extern const u_char _schoolSizes[4];

vs_battle_actor* _calloActor(void);
vs_main_inventoryWeapon* _loadoutWeapon(void);
vs_main_inventoryShield* _loadoutShield(void);
void _dropSharedItems(void);
void _saveCalloGear(vs_battle_actor* callo);
void _learnSpells(vs_battle_actor2* callo);
int _spellIndex(int id);
void _initSpellChoice(void);
int _spellChosen(int index);
void _setSpellChosen(int index, int chosen);
int _spellId(int index);

/* The menu module (menu.c -> ENDING/CALLOMNU.BIN): only menus use it, so it's loaded into the
 * gap between MENU3.PRG/MENU0.PRG's end and the heap (0x8010C000) while they're open. Larger
 * menu overlays reuse that gap, so it's checked (magic + checksum) and reloaded as needed. Its
 * header is the same as CALLO.BIN's: magic, the size of its code and constants, the entry points
 * below, then a checksum of that code and constants (its variables change while it runs). */
#define MENU_MODULE ((u_int*)0x80109800)
#define MENU_MODULE_MAGIC 0x554E4D43 /* "CMNU" */
#define MENU_MODULE_LOAD_SIZE 0x2800 /* up to the heap */
#define MENU_MODULE_LBA 303288       /* tools/mod/build_mod.sh MENU_LBA */
enum {
    menuEquipEnter,  /* void (vs_battle_actor* callo): NULL = Ashley's EQUIP */
    menuEquipLeave,  /* void (void) */
    menuEquipCallo,  /* int (vs_battle_actor* callo, int category, int wepId, int material) */
    menuMarkSpells,  /* void (int school, int* rowTypes) */
    menuToggleSpell, /* void (int spellId) */
};
#ifdef ROOD_PC
/* Natively menu.c is linked in like the rest of the mod: nothing to load, and the entry points
 * are its functions (vs_menu_entries, menu.c). */
extern void* const vs_menu_entries[];
#define MENU_ENTRY(i) (vs_menu_entries[i])
#define MENU_MODULE_PRESENT() 1
#else
#define MENU_ENTRY(i) (((void**)MENU_MODULE)[2 + (i)])
#define MENU_MODULE_PRESENT() (MENU_MODULE[0] == MENU_MODULE_MAGIC)
#endif
