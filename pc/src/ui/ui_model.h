/* Modern menus: what the screens show and ask for, as plain C (filled and
 * executed by the battle-side bridges; no game headers here). */
#ifndef PC_UI_MODEL_H
#define PC_UI_MODEL_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_UI_CLASSES = 6, PC_UI_AFFINITIES = 7, PC_UI_EQUIP_SLOTS = 8, PC_UI_CANDIDATES = 32, PC_UI_QUICK = 8 };
enum { PC_UI_SLOT_WEAPON, PC_UI_SLOT_SHIELD, PC_UI_SLOT_RARM, PC_UI_SLOT_LARM, PC_UI_SLOT_HEAD, PC_UI_SLOT_BODY,
       PC_UI_SLOT_LEGS, PC_UI_SLOT_ACCESSORY };

typedef struct {
    char name[48];      /* UTF-8; "" = nothing */
    char material[24];  /* UTF-8; "" = none */
    int inventory;      /* index into the game's inventory array of that kind, -1 = none */
    int dp, dp_max, pp, pp_max;  /* as the game's menus show them (DP rounded up from hundredths) */
    int str, intel, agl;
    int classes[PC_UI_CLASSES];
    int affinities[PC_UI_AFFINITIES];
    int damage_type;    /* weapons: 0 blunt, 1 edged, 2 piercing; else -1 */
    int two_handed;     /* weapons */
    int quick;          /* weapons: quick-swap slot 1..8, 0 none */
} pc_ui_item;

typedef struct {
    int valid;                                  /* the bridge filled it this session */
    int str, intel, agl;                        /* Ashley's own (the record's, with buffs; no equipment: only the accessory adds to them, see ui_equip_draw.cpp shown()) */
    pc_ui_item equipped[PC_UI_EQUIP_SLOTS];
    /* candidates for one slot (the one the shell asked for: slot field, -1 none) */
    int slot;
    int count;
    pc_ui_item candidates[PC_UI_CANDIDATES];
    /* quick swap: weapon and shield inventory indices per slot (-1 none), and
     * their names ("" none) so the screen can show them without a candidate list */
    int quick_weapon[PC_UI_QUICK], quick_shield[PC_UI_QUICK];
    char quick_weapon_name[PC_UI_QUICK][48], quick_shield_name[PC_UI_QUICK][48];
    int busy;                                   /* no equip now: a model load runs, or the game's menu is still loading */
    int quick_enabled;                          /* the field's quick swap controls are on (settings key qol_weapon_slots) */
} pc_ui_equipment;

enum { PC_UI_REQ_NONE, PC_UI_REQ_EQUIP, PC_UI_REQ_QUICK_SET, PC_UI_REQ_QUICK_CLEAR, PC_UI_REQ_CANDIDATES,
       PC_UI_REQ_ABIL_MAP, PC_UI_REQ_ART_CAST, PC_UI_REQ_SPELL_CAST, PC_UI_REQ_TELEPORT,
       PC_UI_REQ_ITEM_USE, PC_UI_REQ_ITEM_DISCARD, PC_UI_REQ_ITEM_DISCARD_ALL, PC_UI_REQ_ITEM_SORT,
       PC_UI_REQ_ITEM_MOVE, PC_UI_REQ_ITEM_CANCEL };
typedef struct {
    int kind;
    int slot;      /* EQUIP/CANDIDATES: PC_UI_SLOT_*; QUICK_*: 0..7; ABIL_MAP: button 0 Circle, 1 Triangle, 2 Square;
                    * TELEPORT: the save point 0..47; ITEM_*: the category PC_UI_IC_* */
    int item;      /* EQUIP: inventory index, -1 = take off; QUICK_SET: weapon inventory index;
                    * ABIL_MAP, ART_CAST, SPELL_CAST: the action id (vs_main_actions index; a levelled
                    * spell's level: its own id, base + 0..3); ITEM_USE / _DISCARD / _DISCARD_ALL /
                    * _SORT / _CANCEL: the list row (position in the category's list) the actions are
                    * for; ITEM_MOVE: the row moved (from) */
    int shield;    /* QUICK_SET: shield inventory index, -1 none; ABIL_MAP: PC_UI_ABIL_CHAIN / _DEFENSE;
                    * ITEM_USE / _DISCARD / _DISCARD_ALL / _SORT / _CANCEL: the row's inventory index as
                    * the screen saw it (refused when the row holds another record now; -1: not
                    * checked); ITEM_MOVE: the target row (to) */
    int count;     /* ITEM_DISCARD: how many (misc with a count of 2 or more: 1..count; else ignored);
                    * ITEM_SORT: the stat id (one of the category's sort_stats); ITEM_MOVE: the moved
                    * row's inventory index as the screen saw it (as shield above); ITEM_CANCEL: what
                    * was left: 0 the quantity picker with none, 1 Discard's Yes / No on No, 2 Discard
                    * all's on No (the leave sound and the cursor memories the original leaves) */
} pc_ui_request;

/* The Abilities tab (abilities_bridge.c fills it every tick while the shell
 * shows). Chain and defense abilities: the learned ones in the game's list
 * order (BATTLE vs_battle_chainAbilityOffsets / defenseAbilityOffsets) and
 * what the three buttons hold (vs_main_settings.mappedChainAbilities /
 * mappedDefenseAbilities: 0 Circle, 1 Triangle, 2 Square). Break arts: the
 * ten weapon categories in the game's order (1 dagger .. 9 crossbow, 10 bare
 * hands), four arts each. */
enum { PC_UI_ABILITIES = 14, PC_UI_ABIL_BUTTONS = 3, PC_UI_ART_CATEGORIES = 10, PC_UI_ARTS = 4 };
enum { PC_UI_ABIL_CHAIN, PC_UI_ABIL_DEFENSE };
enum { PC_UI_ART_LOCKED, PC_UI_ART_PENDING, PC_UI_ART_LEARNED };
typedef struct {
    int id;                 /* action id */
    char name[32];          /* UTF-8 */
    char help[200];         /* UTF-8 description (the original menu's) */
    int cost;               /* RISK */
    int button;             /* the button it is on: -1 none, 0 Circle, 1 Triangle, 2 Square */
} pc_ui_ability;
typedef struct {
    int count;
    pc_ui_ability entries[PC_UI_ABILITIES];
    int slots[PC_UI_ABIL_BUTTONS];      /* per button: index into entries, -1 empty (or not a learned one) */
    int slot_ids[PC_UI_ABIL_BUTTONS];   /* per button: the action id the settings hold, 0 none */
} pc_ui_ability_list;
typedef struct {
    int id;                 /* action id (184 + (category - 1) * 4 + i) */
    char name[32];          /* UTF-8; "" while locked (the screen shows "???") */
    char help[200];         /* UTF-8 description; "" while locked */
    int cost;               /* HP */
    int state;              /* PC_UI_ART_*: pending = enough kills, learned after the next battle */
    int kills_to_go;        /* locked: kills of this weapon type still needed (0 pending) */
    int usable;             /* learned and castable now (MENU1's checks) */
    char reason[64];        /* learned and not usable: why ("" otherwise) */
} pc_ui_art;
typedef struct {
    char name[24];          /* UTF-8 ("Dagger") */
    char help[160];         /* UTF-8: the category's description, or the game's "no arts" line */
    int learned;            /* arts learned 0..4 */
    int kills;              /* kills with this weapon type */
    int next_kills;         /* kills to the next art, -1 all learned */
    pc_ui_art arts[PC_UI_ARTS];
} pc_ui_art_category;
typedef struct {
    int valid;
    pc_ui_ability_list lists[2];        /* PC_UI_ABIL_CHAIN, PC_UI_ABIL_DEFENSE */
    int next_ability_kills;             /* kills to the next battle ability, -1 all learned */
    int equipped_category;              /* the drawn/equipped weapon's category 1..10, 0 unknown */
    int weapon_drawn;
    int risk, hp, hp_max;
    pc_ui_art_category categories[PC_UI_ART_CATEGORIES];
} pc_ui_abilities;

/* The Magick tab (magick_bridge.c fills it every tick while it shows). The
 * four schools in the game's order (Warlock, Shaman, Sorcerer, Enchanter:
 * BATTLE vs_battle_<school>SpellIds), each with the spells learned
 * (vs_main_actions[id].unlocked) in that order, as MENU0 lists them; then
 * Teleport (MENU0 _teleportMenu): the save points found, in save point order. */
enum { PC_UI_SCHOOLS = 4, PC_UI_SCHOOL_SPELLS = 18, PC_UI_SPELL_LEVELS = 4, PC_UI_SAVE_POINTS = 48 };
enum { PC_UI_SCHOOL_WARLOCK, PC_UI_SCHOOL_SHAMAN, PC_UI_SCHOOL_SORCERER, PC_UI_SCHOOL_ENCHANTER };
typedef struct {
    int id;                 /* action id (a levelled spell's LVn: base + n - 1) */
    int mp;                 /* MP cost (the action's) */
    int usable;             /* castable now (MENU0's checks for this level) */
    char reason[64];        /* not usable: why ("" otherwise) */
} pc_ui_spell_level;
typedef struct {
    int id;                 /* action id (LV1 of a levelled spell) */
    char name[32];          /* UTF-8 */
    char help[200];         /* UTF-8 description (MENU0's: element; kind; targets, then the effect) */
    int mp;                 /* LV1's MP cost */
    int levels;             /* 1, or a levelled Warlock spell's levels learned 2..4 (MENU0's LV picker) */
    pc_ui_spell_level level[PC_UI_SPELL_LEVELS]; /* [0..levels-1]; [0] is the row's own state */
} pc_ui_spell;
typedef struct {
    int unlocked;           /* a spell of the school is learned (vs_battle_isSpellClassUnlocked) */
    char name[24];          /* UTF-8 ("Warlock") */
    char help[160];         /* UTF-8: MENU0's school description, or its "not learned yet" line */
    int count;
    pc_ui_spell spells[PC_UI_SCHOOL_SPELLS];
} pc_ui_school;
typedef struct {
    int point;              /* the save point 0..47 (vs_main_stateFlags.savePointStates index) */
    char name[32];          /* UTF-8 */
    char help[96];          /* UTF-8: where it is, or the game's spirit paling line (state 2) */
    int cost;               /* MP (MENU0 _getTeleportCost) */
    int current;            /* the nearest one (cost 15: the circle the player stands on) */
    int usable;
    char reason[64];
} pc_ui_destination;
typedef struct {
    int valid;
    int mp, mp_max;
    unsigned mask;          /* the menu's disable mask D_800F4EA0 & 0xB7 (greys every spell) */
    int level_memory;       /* MENU0's LV picker level, cursorMemories[12] (seeds the shell's level at an open) */
    pc_ui_school schools[PC_UI_SCHOOLS];
    int teleport;           /* the Teleport row exists (vs_main_stateFlags.unkB5 != 0) */
    int teleport_enabled;   /* MENU0's row is selectable */
    char teleport_help[96]; /* UTF-8: MENU0's row description */
    char teleport_reason[64]; /* not enabled: why */
    int destination_count;
    pc_ui_destination destinations[PC_UI_SAVE_POINTS];
} pc_ui_magick;

/* The Items tab (items_bridge.c fills it every tick while it shows). The
 * seven bag categories in the game's order (MENU3 / MAINMENU itemCategory*:
 * weapons, blades, grips, shields, armor, gems, misc) and each one's list in
 * the game's order (vs_main_inventoryIndices, rebuilt as MENU3's Items menu
 * rebuilds it whenever it lists); per row what the original list and its
 * action menu show (MENU3 items2.h _populateItemRows / _itemNavigation
 * populateActions). One flat rows array: category c's rows are
 * rows[first[c] .. first[c] + count[c] - 1]. */
enum { PC_UI_ITEM_CATEGORIES = 7, PC_UI_ITEM_ROWS = 8 + 16 + 16 + 8 + 16 + 48 + 64, PC_UI_SORT_STATS = 24,
       PC_UI_STAT_NAMES = 57 };
enum { PC_UI_IC_WEAPON, PC_UI_IC_BLADE, PC_UI_IC_GRIP, PC_UI_IC_SHIELD, PC_UI_IC_ARMOR, PC_UI_IC_GEM, PC_UI_IC_MISC };
enum { PC_UI_MARK_NONE, PC_UI_MARK_EQUIPPED, PC_UI_MARK_SET }; /* the original row's icon (0xCA00 / 0xCC00) */
typedef struct {
    int inventory;          /* the record (0-based index into the category's inventory array) */
    int id;                 /* the record's item id (a weapon: its blade's record index + 1, MAINMENU _getItemId) */
    char name[48];          /* UTF-8 (a weapon: its own name) */
    char sub[64];           /* UTF-8: what kind of thing it is (material, category, grip kind, body part; "" misc) */
    char help[200];         /* UTF-8: the original's description (blades, grips, gems, accessories, misc; ""
                             * for weapons, shields and armor, whose original line is stats: see details) */
    int count;              /* misc: the stack's count; else 0 */
    int mark;               /* PC_UI_MARK_*: equipped (worn, or part of the equipped weapon / shield) or part
                             * of a weapon / shield in the bag */
    int material;           /* 1.. (the original row's material icon), 0 none */
    int parent_category;    /* blades, grips, gems in a set: PC_UI_IC_WEAPON or PC_UI_IC_SHIELD; -1 none (Search) */
    int parent;             /* its inventory index, -1 none */
    int discard;            /* Discard is selectable */
    char discard_reason[96];/* not: the original's line (equipped, part of a set, cannot be discarded) */
    int discard_all;        /* misc: Discard all is selectable */
    char discard_all_reason[96];
    int action;             /* misc: the action Use hands over (MAINMENU vs_mainMenu_miscItemToActionMap, a
                             * levelled grimoire's highest learned level); else 0 */
    int usable;             /* misc: Use is selectable */
    char use_reason[96];    /* misc, not usable: why (the original's line) */
} pc_ui_item_row;
typedef struct {
    int valid;              /* filled for the focused row (the shell's pc_ui_shell_items_focus) */
    int category, inventory;
    pc_ui_item stats;       /* name, material, DP / PP, STR / INT / AGL, classes, affinities, damage type and
                             * hands (weapons, blades), as the original status page computes them
                             * (vs_battle_apply* / vs_battle_copyInventory*Stats) */
    int types[3];           /* blunt, edged, piercing (grips, shields, armor, gems) */
    int range;              /* weapons, blades */
    int risk;               /* weapons, blades: the RISK cost */
    int blade_category;     /* weapons, blades: 1 dagger .. 9 crossbow */
    int grip_category;      /* grips: 1 short .. 4 crossbow */
    int armor_category;     /* armor: 1 shield .. 7 accessory (the record's category) */
    int gem_slots;          /* weapons (their grip), grips, shields */
    int gem_count;          /* gems set in it */
    char gems[3][48];       /* their names (UTF-8) */
    char blade[48], grip[48]; /* weapons: their parts' names (UTF-8) */
} pc_ui_item_details;
typedef struct {
    int valid;
    unsigned mask;          /* the menu's disable mask D_800F4EA0 & 7 (the original greys every row) */
    int locked;             /* mask set: browse only */
    char locked_reason[96]; /* why */
    int count[PC_UI_ITEM_CATEGORIES];
    int capacity[PC_UI_ITEM_CATEGORIES];
    int first[PC_UI_ITEM_CATEGORIES];
    pc_ui_item_row rows[PC_UI_ITEM_ROWS];
    /* Sort: per category the original's stat list (MENU3 items1.h _sortItems sortStats[category], in its
     * menu order), and every stat's name and description (ITEMHELP sortRange + (stat - 2) * 2) */
    int sort_count[PC_UI_ITEM_CATEGORIES];
    unsigned char sort_stats[PC_UI_ITEM_CATEGORIES][PC_UI_SORT_STATS];
    char stat_name[PC_UI_STAT_NAMES][32];
    char stat_help[PC_UI_STAT_NAMES][96];
    char no_search[96];     /* the original's Search line outside a set (ITEMHELP invalidSearch) */
    char discard_prompt[3][128]; /* its confirm questions: one (0), a quantity (1), all (2) (discardConfirm + n) */
    pc_ui_item_details details;
} pc_ui_items;

/* The Ashley tab (ashley_bridge.c fills it every tick). */
enum { PC_UI_LIMBS = 6, PC_UI_EFFECTS = 32 };
enum { PC_UI_LIMB_CRITICAL, PC_UI_LIMB_DAMAGED, PC_UI_LIMB_WOUNDED, PC_UI_LIMB_GOOD, PC_UI_LIMB_EXCELLENT };
typedef struct {
    char name[32];          /* UTF-8 */
    int hp, hp_max;
    int state;              /* PC_UI_LIMB_* (the game's vs_battle_getLimbStatus) */
} pc_ui_limb;
typedef struct {
    char name[32];          /* UTF-8 */
    char help[160];         /* UTF-8 description */
    int buff;               /* 1 buff, 0 ailment */
    int seconds;            /* remaining, -1 = no timer */
} pc_ui_effect;
typedef struct {
    int valid;
    int hp, hp_max, mp, mp_max, risk;   /* risk 0..100 */
    int str, intel, agl;                /* as the original Status shows them (its EQP column) */
    int limb_count;
    pc_ui_limb limbs[PC_UI_LIMBS];
    int effect_count;
    pc_ui_effect effects[PC_UI_EFFECTS]; /* in the game's display order */
    char area[64], room[96];            /* UTF-8 location ("" unknown) */
    int play_h, play_m, play_s;
    int model;                          /* 0 none, 1 loading, 2 shown */
    float model_rect[4];                /* layout units: where the model is drawn (x, y, w, h) */
    int model_yaw, model_pitch;         /* the view's orbit (4096 = a turn; pitch 0..896, 0 level) */
    int model_blocked;                  /* why no model: PC_UI_MODEL_* */
    int heap_free, heap_need;           /* bytes: the game heap's largest free block, what the clone needs (0 unknown) */
    unsigned tick;                      /* pc_vsync_count at the fill (the drawing trusts `model` only while fresh) */
} pc_ui_ashley;
enum { PC_UI_MODEL_OK, PC_UI_MODEL_NO_HEAP, PC_UI_MODEL_FREE_CAMERA };

#ifdef __cplusplus
}
#endif
#endif
