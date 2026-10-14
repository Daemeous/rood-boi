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
       PC_UI_REQ_ITEM_MOVE, PC_UI_REQ_ITEM_CANCEL,
       /* the Loot screen (ui_shell.h): item = the loot row, shield = its record as seen (-1 not checked) */
       PC_UI_REQ_LOOT_TAKE, PC_UI_REQ_LOOT_TAKE_ALL, PC_UI_REQ_LOOT_DISASSEMBLE, PC_UI_REQ_LOOT_CONSOLIDATE,
       PC_UI_REQ_LOOT_LEAVE,  /* count: 1 the Yes / No answered Yes (0: Leave pressed) */
       PC_UI_REQ_LOOT_BAG,    /* slot: 1 the Organize panel opens (category item), 0 it closes */
       PC_UI_REQ_LOOT_SOUND,  /* slot: 0 the leave sound (a menu backed out), 1 the select sound */
       /* the Workshop (Items > Workshop, ui_shell.h): records are 0-based inventory indices, "seen" checks as
        * the Items requests' (-1: not checked) */
       PC_UI_REQ_WS_ASSEMBLE,    /* slot: blade, item: grip, shield: gems (gem record + 1 per byte, slot 0 low),
                                  * name: MENU8's 20-code buffer as typed */
       PC_UI_REQ_WS_GEMS,        /* slot: 0 a weapon, 1 a shield; item: its record; shield: the gems as for
                                  * ASSEMBLE; count: its gems as seen (the same packing); flags: PC_UI_WS_EDITED
                                  * when a slot was changed on the screen (the original attaches live) */
       PC_UI_REQ_WS_DISASSEMBLE, /* slot: 0 a weapon, 1 a shield; item: its record; shield: its row seen */
       PC_UI_REQ_WS_RENAME,      /* item: the weapon; name: as ASSEMBLE's */
       PC_UI_REQ_WS_REPAIR,
       PC_UI_REQ_WS_COMBINE,     /* slot: 0 blades, 1 shields, 2 armor; item: the first; shield: the second */
       PC_UI_REQ_WS_SOUND,       /* slot: 0 the leave sound, 1 the select sound, 2 the invalid sound */
       PC_UI_REQ_WS_GEMS_LEAVE   /* Gems' slots left without Attach (backed out, closed): slot, item as GEMS;
                                  * count: its gems as seen. The original's _attachGems(targetGems): the
                                  * gems put back and an equipped target re-equipped (silent) */ };
enum { PC_UI_WS_EDITED = 1 };
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
    int flags;     /* WS_GEMS: PC_UI_WS_EDITED */
    unsigned char name[20]; /* WS_ASSEMBLE / WS_RENAME: the name field (PC_UI_WS_NAME_LEN game codes) */
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

/* The Loot screen (loot_bridge.c fills it every tick of a loot session: the
 * game's MENUB, opened after a battle with drops or at a chest; a direct
 * session, no tabs). The rows are MENUB's loot list in its order (_lootList:
 * the drops for the current difficulty, copied into the loot's own
 * inventory, _applyAllLootLists); per row whether the bag takes it now
 * (MENUB's lootState: a vs_mainMenu_copyItem probe, _updateLootList) and
 * what the original's Organize menu offers for it when not. */
enum { PC_UI_LOOT_ROWS = 160 }; /* MENUB's _lootList entries: every row shows */
enum { PC_UI_LOOT_ORG_NONE, PC_UI_LOOT_ORG_DISASSEMBLE, PC_UI_LOOT_ORG_CONSOLIDATE };
typedef struct {
    int category;           /* PC_UI_IC_* */
    int record;             /* the record in the loot's own inventory (0-based; the request checks it) */
    int id;                 /* item id (a weapon: its blade's) */
    char name[48];          /* UTF-8 (a weapon: its own name) */
    char sub[64];           /* UTF-8: material and kind, as the Items list's sub line */
    char help[200];         /* UTF-8: the game's description (blades, grips, gems, accessories, misc) */
    int count;              /* misc: the stack's count */
    int material;           /* 1.., 0 none */
    int fits;               /* the bag takes it now (Take); else Take opens the Organize menu */
    int key;                /* a key or sigil (misc id above 457): taking it sets its flag; never left behind */
    int organize;           /* PC_UI_LOOT_ORG_*: the Organize menu's first row (_organizeInventory) */
    int needs[PC_UI_ITEM_CATEGORIES]; /* bag records it takes per category (_itemIncludesItemsInCategory) */
} pc_ui_loot_row;
typedef struct {
    int valid;
    int chest;              /* from a chest (D_800EB9C8), else after a battle */
    int count;              /* rows (MENUB's list holds at most PC_UI_LOOT_ROWS) */
    int claimed;            /* items taken so far (_lootClaimed) */
    int keys;               /* a key or sigil is in the list: Leave is refused (_discardItems' invalidDiscard) */
    pc_ui_loot_row rows[PC_UI_LOOT_ROWS];
    int bag_count[PC_UI_ITEM_CATEGORIES], bag_capacity[PC_UI_ITEM_CATEGORIES];
    pc_ui_item_details details;   /* the focused row (as the Items screen describes a bag record) */
    pc_ui_item equipped;          /* what it compares with: the equipped weapon / shield, the armor or accessory
                                   * worn where it goes (inventory -1: nothing to compare) */
    int equipped_slot;            /* PC_UI_SLOT_* of that, -1 none */
    /* the original's texts (MENUB menuText; the focused row's name put in) */
    char empty[160];              /* emptyChest */
    char leave_untouched[160];    /* discardAllConfirm: Leave with nothing taken */
    char leave_rest[160];         /* itemsRemaining: Leave with items left */
    char keys_text[160];          /* invalidDiscard, the first key's name */
    char disassemble_ask[160];    /* disassembleConfirm, the focused row's name */
    char consolidate_ask[160];    /* organizeConfirm, the focused row's name */
    char org_label[3][40];        /* the Organize menu's rows: [PC_UI_LOOT_ORG_*] (NONE: Organize inventory) */
    char org_info[3][160];        /* and their information lines */
} pc_ui_loot;

/* The Workshop (workshop_bridge.c fills it every tick while the Items tab's
 * Workshop sub-tab shows): the original's Setup screen (MENUC, reached from
 * MENU3's Items > Setup). Its six commands with MENUC's greying, the bag as
 * MENUC lists it (vs_main_inventoryIndices order; armor without accessories)
 * with what each flow needs per record, MENUC's texts (MENU12.BIN, by its
 * index: build/assets/MENU/MENU12.BIN.h VS_MENU12_BIN_INDEX_*), the rename
 * character grid (MENU8's rename charTable) and the previews the shell asks
 * for (pc_ui_shell_workshop_wanted). */
enum { PC_UI_WS_ACTIONS = 6, PC_UI_WS_TEXTS = 96, PC_UI_WS_TEXT = 160, PC_UI_WS_GRID_ROWS = 9,
       PC_UI_WS_GRID_COLS = 14, PC_UI_WS_NAME_LEN = 20, PC_UI_WS_REPAIRS = 40 };
enum { PC_UI_WSA_ASSEMBLE, PC_UI_WSA_GEMS, PC_UI_WSA_DISASSEMBLE, PC_UI_WSA_RENAME, PC_UI_WSA_REPAIR,
       PC_UI_WSA_COMBINE };
enum { PC_UI_WSL_WEAPONS, PC_UI_WSL_BLADES, PC_UI_WSL_GRIPS, PC_UI_WSL_SHIELDS, PC_UI_WSL_ARMOR, PC_UI_WSL_GEMS,
       PC_UI_WSL_LISTS };
typedef struct {
    int inventory;          /* the record (0-based) */
    int id;                 /* item id (a weapon: its blade's) */
    char name[64];          /* UTF-8 (a weapon: its own name, 20 codes of up to 3 bytes) */
    char sub[64];           /* UTF-8: material and kind */
    int material;           /* 1.., 0 none */
    int kind;               /* weapons, blades: the blade category 1..9; grips: 1..4; armor: the category */
    int mark;               /* PC_UI_MARK_* (equipped / worn, part of a set) */
    int parent;             /* blades, grips: the weapon (0-based) they are in; gems: the weapon or shield; -1 */
    int parent_shield;      /* gems: the parent is a shield */
    int gem_slots;          /* weapons (their grip), grips, shields */
    int gems[3];            /* weapons, shields: the gem record (0-based) in each slot, -1 empty */
    int material_ok;        /* blades, shields, armor: this workshop works the material (_workshopMaterials) */
    unsigned grips_ok;      /* blades: bit g set when grip row g of the grips list fits it (_isValidGrip) */
    unsigned char field[PC_UI_WS_NAME_LEN]; /* weapons: MENU8's name buffer as its rename opens on the name */
} pc_ui_ws_row;
typedef struct {
    int category;           /* PC_UI_IC_BLADE, _SHIELD or _ARMOR */
    int inventory;
    char name[64];          /* UTF-8 (blades: the weapon's name when part of one) */
    int dp, dp_max, pp, pp_max; /* as the menus show them (DP rounded up) */
    int dp_after, pp_after; /* after _repairMenu */
    int equipped;
} pc_ui_ws_repair;
typedef struct {
    int valid;
    int workshop;           /* vs_battle_inWorkshop(): 0 none, 1..6 */
    unsigned materials;     /* its _workshopMaterials mask (bit = material) */
    char material[8][24];   /* the materials' names (UTF-8; [0] none) */
    int locked;             /* the Items menu's disable mask greys everything (browse only) */
    char locked_reason[96];
    int busy;               /* a model reload runs or MAINMENU is not resident yet: no change now */
    int action_enabled[PC_UI_WS_ACTIONS];
    int action_reason[PC_UI_WS_ACTIONS]; /* greyed: the text index of why (-1 none: locked_reason) */
    int count[PC_UI_WSL_LISTS];
    pc_ui_ws_row weapons[8], blades[16], grips[16], shields[8], armor[16], gems[48];
    int weapon_slot;        /* Assemble: the record the new weapon takes (vs_mainMenu_getFirstEmptyItemSlot), 8 full */
    int repair_count;
    pc_ui_ws_repair repairs[PC_UI_WS_REPAIRS];
    /* the preview the shell asked for (pc_ui_shell_workshop_wanted): key as asked, valid once computed */
    int preview_key[6];
    int preview_valid;
    pc_ui_item_details preview;
    int preview_category;   /* PC_UI_IC_* of what the preview describes */
    /* MENU12.BIN by index (UTF-8) */
    char text[PC_UI_WS_TEXTS][PC_UI_WS_TEXT];
    /* MENU8's rename grid (game codes; 0x8F a blank cell) and every code's UTF-8 ("" unprintable) */
    unsigned char grid[PC_UI_WS_GRID_ROWS][PC_UI_WS_GRID_COLS];
    char glyph[256][8];
} pc_ui_workshop;

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
