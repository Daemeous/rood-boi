/* Read-only player state for Lua and the RPC (rood.game.inventory, weapons,
 * shields, armour, accessories, equipment, workshop, combine, craftable;
 * RPC game_state and combine_preview; pc/docs/Extensions.md "Player state").
 * player_bridge.c (battle builds, the game's headers) copies what the game's
 * records and the menus' bridges say into these plain records;
 * lua_api_player.cpp turns them into Lua tables (once per game vsync) and
 * JSON. Copies, never pointers into the game. Every call is on the game
 * thread. Text is UTF-8, in the game's own words (its item names, materials,
 * categories, as the menus show them). */
#ifndef ROOD_PLAYER_QUERY_H
#define ROOD_PLAYER_QUERY_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_PQ_CLASSES = 6, PC_PQ_AFFINITIES = 7, PC_PQ_TYPES = 3, PC_PQ_GEM_SLOTS = 3 };

/* What a record is (the game's bag categories; accessories are armor records of
 * category 7). */
enum { PC_PQ_WEAPON, PC_PQ_BLADE, PC_PQ_GRIP, PC_PQ_SHIELD, PC_PQ_ARMOR, PC_PQ_ACCESSORY, PC_PQ_GEM, PC_PQ_MISC,
       PC_PQ_KINDS };

/* Equipment slots, as the Equipment screen lists them. */
enum { PC_PQ_SLOT_WEAPON, PC_PQ_SLOT_SHIELD, PC_PQ_SLOT_RIGHT_ARM, PC_PQ_SLOT_LEFT_ARM, PC_PQ_SLOT_HEAD,
       PC_PQ_SLOT_BODY, PC_PQ_SLOT_LEGS, PC_PQ_SLOT_ACCESSORY, PC_PQ_SLOTS };

/* Misc records are items, keys or sigils (the game's item id ranges). */
enum { PC_PQ_ITEM, PC_PQ_KEY, PC_PQ_SIGIL };

/* One record, as the Items screen's card shows it (items_bridge.c
 * pc_ui_items_describe: the game's own values, DP in whole points rounded up
 * as the menus show it). Which fields mean something depends on the kind
 * (lua_api_player.cpp emits only those; Extensions.md lists them). */
typedef struct {
    int kind;          /* PC_PQ_* */
    int record;        /* 1-based record in the game's array of that kind (accessories: armor's) */
    int id;            /* the game's item id (a weapon: its blade's record + 1, as MAINMENU's) */
    char name[64];
    char material[24]; /* "" none */
    char category[64]; /* the game's word: a blade's weapon class, a grip's kind, armor's body part */
    int dp, dp_max, pp, pp_max;
    int str, intel, agl;
    int classes[PC_PQ_CLASSES], affinities[PC_PQ_AFFINITIES], types[PC_PQ_TYPES];
    int damage_type;   /* blades, weapons: 0 blunt, 1 edged, 2 piercing; -1 none */
    int range, risk;   /* blades, weapons */
    int two_handed;    /* blades, weapons */
    int gem_slots;     /* grips, shields, weapons (their grip's) */
    int weapon;        /* blades, grips, gems: the weapon record (1-based) it is part of, 0 none */
    int shield;        /* gems: the shield record it is set in, 0 none */
    int equipped;      /* worn, equipped, or part of the equipped weapon / shield */
    int slot;          /* PC_PQ_SLOT_* it is equipped on, -1 none */
    int quick;         /* weapons: quick-swap slot 1..8 (the port's weapon slots), 0 none */
    int count;         /* misc: the stack's count */
    int misc_kind;     /* misc: PC_PQ_ITEM, _KEY, _SIGIL */
} pc_pq_part;

/* A weapon (blade, grip, gems) or a shield (its gems): the whole as the
 * Items screen computes it (vs_battle_applyWeapon / Shield, or Ashley's own
 * record for the equipped one) and its parts. */
typedef struct {
    pc_pq_part whole;
    pc_pq_part blade, grip; /* weapons */
    int gem_count;
    pc_pq_part gems[PC_PQ_GEM_SLOTS];
} pc_pq_set;

enum { PC_PQ_BAG_CATEGORIES = 7 }; /* weapons, blades, grips, shields, armor, gems, misc */

typedef struct {
    int valid;                         /* Ashley's records are there */
    int count[PC_PQ_BAG_CATEGORIES];   /* records in each bag category */
    int capacity[PC_PQ_BAG_CATEGORIES];
    int weapon_count, shield_count, blade_count, grip_count, armor_count, gem_count, misc_count;
    pc_pq_set weapons[8], shields[8];
    pc_pq_part blades[16], grips[16], armor[16], gems[48], misc[64]; /* armor: accessories too (kind) */
    int equipped[PC_PQ_SLOTS]; /* per slot the record (1-based) of weapons / shields / armor, 0 nothing */
    unsigned key;              /* a hash of what it was made from (a changed bag changes it) */
} pc_pq_bag;

typedef struct {
    int workshop;        /* the game's workshop number (vs_battle_inWorkshop), 0 none */
    unsigned materials;  /* what it works (MENUC _workshopMaterials: bit = material) */
    char name[96];       /* the room's name (the game's) */
    int material_count;
    char material_names[8][24];
} pc_pq_workshop;

/* A combine's result: what the game's Combine screen computes (MENUC
 * _combineBlades / _combineShields / _combineArmor, run on copies) and the
 * record it would store in the first's place. */
typedef struct {
    int kind;            /* PC_PQ_BLADE, _SHIELD, _ARMOR */
    int a, b;            /* the records (1-based) combined, first and second */
    int allowed;         /* the current workshop works both (and is a workshop) */
    char why[96];        /* not allowed: the game's line ("" allowed) */
    pc_pq_part result;
    unsigned char raw[48]; /* the record as the game would store it */
    int raw_size;
} pc_pq_combine;

enum { PC_PQ_COMBOS = 16 * 15 + 8 * 7 + 16 * 15 };

/* player_bridge.c (battle builds; absent elsewhere: weak). */
/* The bag now (a cached copy while nothing it is made from changes). NULL
 * without Ashley's records. */
const pc_pq_bag* pc_player_bag(void) __attribute__((weak));
/* The current workshop; 0 when not in one. */
int pc_player_workshop(pc_pq_workshop* out) __attribute__((weak));
/* kind PC_PQ_BLADE / _SHIELD / _ARMOR, records a, b (1-based): 1 and *out, or 0 and
 * why (records missing, the same record, the wrong kind, an accessory, the
 * game's tables unreadable). Nothing of the game changes. */
int pc_player_combine(int kind, int a, int b, pc_pq_combine* out, char* why, int why_size) __attribute__((weak));
/* Every combination the game's Combine screen allows with this bag at this
 * workshop (both orders: the first's record keeps the result), with results;
 * 0 outside a workshop. */
int pc_player_craftable(pc_pq_combine* out, int max) __attribute__((weak));
/* The game's words for the classes, affinities and damage types (its sort
 * menu's stat names), the slots' body parts. */
const char* pc_player_label(int group, int i) __attribute__((weak)); /* group 0 classes, 1 affinities, 2 types */

#ifdef __cplusplus
}
#endif
#endif
