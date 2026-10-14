/* Modern menus, Items tab: the game side (battle builds). menu_bridge.c calls
 * pc_ui_items_fill at the start of every tick of the shell's top level (and
 * when a session opens) and pc_ui_items_refresh after the shell's tick: the
 * snapshot is filled (and copied to the shell) once a tick, at its start,
 * only while the Items screen shows; after the tick only the details card
 * follows the focus, unless a request changed the game (refilled then) or the
 * screen came up on this tick. Every request but ITEM_CANCEL fills it again
 * first (an earlier request of the tick may have changed the game).
 * equip_bridge.c's request loop hands the Items requests to
 * pc_ui_items_request (after the Abilities and Magick bridges decline them);
 * menu_bridge.c then asks pc_ui_items_cast_taken whether a Use was accepted
 * and hands it to the game with pc_ui_items_cast.
 *
 * Everything here is MENU3's Items menu (src/MENU/items1.h, items2.h,
 * MENU3.PRG/16C.c) and the MAINMENU routines it calls (2D10.c), ported line
 * for line on the resident BATTLE and main data: MAINMENU may not be loaded
 * during the menu's first ticks, and MENU3 never is. The ports keep the
 * originals' types and expressions, so the inventory and its list order end
 * byte for byte as the original menu leaves them (ui_items_smoke checks it).
 *
 * The snapshot (ui_model.h pc_ui_items):
 *   lists      vs_main_inventoryIndices per category, rebuilt for all seven
 *              categories as MENU3 does whenever its list (re)starts
 *              (_itemNavigation init: vs_mainMenu_rebuildInventory 0..6,
 *              equip_bridge.c's port) - every tick while the screen shows
 *   rows       _populateItemRows: the rows up to the first empty entry;
 *              names as vs_mainMenu_set*Row take them (a weapon its own
 *              name, blades / grips / shields itemNames[id], armor, gems,
 *              misc itemNames[id]); the mark (0xCA00 equipped / 0xCC00 part
 *              of a weapon or shield in the bag: _getParentWeaponIndex,
 *              _getParentShieldIndex); misc counts; descriptions from
 *              ITEMHELP (item_names.c reads it from the disc)
 *   actions    _itemNavigation populateActions: Use (misc; greyed for ids
 *              above 457, keys and sigils, "cannotUse", or with
 *              vs_battle_getActionFlags(0, action) & 2, MENU3's
 *              "insufficientMp"); Search (blades, grips, gems; greyed
 *              outside a set); Discard (greyed: equipped, part of a set,
 *              keys and sigils: ITEMHELP cannotDiscardEquipped + rowType -
 *              1); Discard all (misc; greyed for keys and sigils); Sort. With
 *              the menu's disable mask D_800F4EA0 & 7 every row is greyed
 *              (the original list's rows are unselectable then).
 *   use action vs_mainMenu_miscItemToActionMap[id - 323] (2D10.c:1581,
 *              copied), a levelled grimoire (actions 106..133) moved up one
 *              per learned level, at most three (_populateItemRows)
 *   sort lists _sortItems' tables (items1.h:1077, copied with their names:
 *              sortStats[category] is shifted by one against the names, e.g.
 *              shields list gripSortStats) and the stat names ITEMHELP
 *              sortRange - 4 + stat * 2 (and the description + 1)
 *   details    the focused row's card: as equip_bridge.c computes equipment
 *              (vs_battle_apply*), blades / grips / gems through
 *              vs_battle_copyInventory*Stats as MENU3's status pages
 *
 * Requests (refused with the invalid sound while the mask greys the list, a
 * row is not selectable, the row holds another record than the screen saw
 * (ui_model.h), or MAINMENU is not resident yet: its
 * vs_mainMenu_initInventory copies the equipped items' current stats into
 * the bag first, which the original's Items menu always runs after; every
 * change is refused on a tick a Use, break art or spell was taken, which
 * ends the menu):
 *   ITEM_DISCARD {cat, row, count}  _discardMenu: vs_main_playSfxDefault(0x7E,
 *              28); a misc stack of 2 or more loses count (its quantity
 *              picker), the whole stack when count is all of it
 *              (vs_mainMenu_initItem); anything else vs_mainMenu_initItem
 *              (recursive: a weapon's blade, grip and gems, a shield's gems).
 *              A discarded weapon's quick-swap slot (weapon_slots.c) is
 *              cleared first: its record kept the slot, the shields keep the
 *              slot's shield bit, which would outlive the weapon.
 *   ITEM_DISCARD_ALL {misc, row}  _discardMenu's discard all: the stack
 *   ITEM_CANCEL {cat, row, what}  the picker left with none, or a Yes / No
 *              answered No: the leave sound (_discardMenu), nothing else
 *   ITEM_SORT {cat, row, stat}  _sortItems: the select sound, _sortWeapons /
 *              _sortShields / _sortMisc / _sortEquipment (repeated maximum,
 *              stable, negated keys for category, material, damage type, id)
 *   ITEM_MOVE {cat, from, to}  _itemNavigation state 6: misc stacks of the
 *              same item merge into the row moved (100 at most, the rest
 *              stays in the target row; all of it: the target row's record
 *              is initialised), anything else swaps the two rows' entries;
 *              the select sound (the list's confirm)
 * Cursor memories (D_800F4EE8, 58EC.c): each operation leaves the ones the
 * original's menus store for the same choice: the category's list (30 + c,
 * confirmed on the row; for Move on the row, then on the target), the
 * current category (unk3A.currentItemCategory = c + 1), the action menu
 * (38 + c: the action's row in populateActions' menu), the sort menu (45 + c:
 * the stat's row) and the Yes / No (4: Yes 0, No 1). A list or sort menu
 * opens where the original's opens (its memory normalised; the sort menu on
 * its first row without cursor memory) and the cursor is taken there
 * straight (cursor_at); a fresh entry to the Items screen without cursor
 * memory zeroes unk3A as MENU3's entry does. Exceptions, where the shell's
 * own cursors stand in for the original's: in a list or sort menu longer
 * than its screen the original's memory depends on the path its cursor took
 * (it scrolls with a row of margin and wraps), modelled as the straight one;
 * moving between categories, backing out with Cross or Triangle, Search and
 * a refused action store nothing (the original stores each menu's cursor as
 * it is left, a category's list on L1 / R1).
 *   ITEM_USE {misc, row}  populateActions' Use, then vs_menu3_exec: accepted,
 *              pc_ui_items_cast(1) (the shell returned no result on that tick)
 *              writes MENU3's hand-off: vs_battle_miscItemInvoked =
 *              indices[row] - 1, D_800F4E98.executeAbility.u16[0] = the action,
 *              u16[1] = the item id, executeAbilityType 13, unk2 8,
 *              returnState 4 | 0x40 (MENU3's currentMenu, opened from the top
 *              level); the cursor memories its lists leave (the misc list's
 *              row, the action menu on Use, the current category misc) and the
 *              top level's on Items. menu_bridge.c ends the menu as MENU3 does
 *              (close_for_cast: MENU3 returns currentMenu 4, func_800FAA5C makes
 *              it 0x7F) and BATTLE func_800735F8 runs it (unk2 8:
 *              func_80072EC4, targeting); the item is used up when the action
 *              completes (func_80070F28, vs_battle_decreaseMiscCount). A
 *              cancelled target reopens the shell on the item
 *              (pc_ui_menu_cast_cancelled), not MENU3 (returnState).
 * After each change the seven lists are rebuilt (MENU3 goes back to its init
 * state, which rebuilds them) and the snapshot refilled. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "build/assets/MENU/ITEMHELP.BIN.h"
#include "build/assets/MENU/MENU3.PRG/menuText.h"
#include "inventory_ops.h"
#include "item_names.h"
#include "ui_shell.h"
#include <limits.h>

/* ui_text.h (stdio in a game-header unit is avoided) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);

void pc_ui_items_session(int fresh);
void pc_ui_items_fill(int mainmenu);
void pc_ui_items_refresh(void);
int pc_ui_items_request(const pc_ui_request* r);
int pc_ui_items_cast_taken(void);
void pc_ui_items_cast(int go);
int pc_ui_abilities_cast_taken(void); /* abilities_bridge.c: one hand-off per tick */
int pc_ui_magick_cast_taken(void);    /* magick_bridge.c */
int pc_ui_equip_busy(void);           /* equip_bridge.c: a weapon / shield model reload runs */
int pc_ui_item_record_id(int category, int i);
void pc_ui_items_rebuild(int category);
void pc_ui_equip_describe(int kind, int index, pc_ui_item* out);
/* weapon_slots.c */
int pc_weapon_slots_get(int weapon_index);
void pc_weapon_slots_clear(int slot);

/* MENU3.PRG menuText, under our own name (its insufficientMp line). */
static u_short menu3_text[] = {
#include "build/assets/MENU/MENU3.PRG/menuText.vsString"
};

/* MAINMENU vs_mainMenu_miscItemToActionMap (2D10.c:1581): misc ids 323..456. */
static const u_char misc_item_actions[] = { 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xF3, 0xF4, 0xF5,
    0xF6, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEF, 0xEE, 0xF0, 0xF2, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xF8,
    0xF9, 0xFA, 0xFB, 0xFC, 0xF1, 0x00, 0xFE, 0xFD, 0xFB, 0xF4, 0xFE, 0xFB, 0xF6, 0xF3, 0xF3, 0xFC, 0xFD, 0xFF,
    0xFB, 0xF5, 0xFE, 0xFD, 0xFF, 0xFD, 0xFD, 0xFF, 0xFD, 0xFE, 0xFE, 0xFF, 0xFE, 0xFC, 0xFF, 0xFD, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x00, 0x00, 0x00, 0x6E, 0x00, 0x00, 0x00, 0x72, 0x00, 0x00,
    0x00, 0x76, 0x00, 0x00, 0x00, 0x7A, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x82, 0x00, 0x00, 0x00, 0x5E,
    0x5F, 0x60, 0x57, 0x58, 0x5A, 0x5B, 0x4D, 0x3C, 0x36, 0x3D, 0x37, 0x3E, 0x38, 0x3F, 0x39, 0x4B, 0x4C, 0x52,
    0x53, 0x55, 0x4F, 0x50, 0x5C, 0x5D, 0x3A, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49 };

/* MAINMENU vs_mainMenu_inventoryItemCapacities and vs_mainMenu_inventoryIndices (2D10.c:1595). */
static const u_char capacities[PC_UI_ITEM_CATEGORIES] = { 8, 16, 16, 8, 16, 48, 64 };
static u_char* const lists[PC_UI_ITEM_CATEGORIES] = { vs_main_inventoryIndices.weapons,
    vs_main_inventoryIndices.blades, vs_main_inventoryIndices.grips, vs_main_inventoryIndices.shields,
    vs_main_inventoryIndices.armor, vs_main_inventoryIndices.gems, vs_main_inventoryIndices.misc };

/* MENU3 items1.h _sortItems (1077): the stat lists, under the original names;
 * sortStats[category] is one off against them (blades list weaponSortStats,
 * grips bladeSortStats, shields gripSortStats, armor shieldSortStats, gems
 * equipmentSortStats). */
static const u_char weaponSortStats[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23 };
static const u_char bladeSortStats[] = { 0, 8, 9, 10, 24, 25, 26 };
static const u_char gripSortStats[] = { 1, 4, 5, 6, 7, 8, 9, 10, 40, 41, 42, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 39 };
static const u_char shieldSortStats[] = { 0, 1, 4, 5, 8, 9, 10, 40, 41, 42, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 39 };
static const u_char equipmentSortStats[] = { 8, 9, 10, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 };
static const u_char miscSortStats[] = { 0, 56 };
static const u_char sortStatSizes[] = { sizeof weaponSortStats, sizeof weaponSortStats, sizeof bladeSortStats,
    sizeof gripSortStats, sizeof shieldSortStats, sizeof equipmentSortStats, sizeof miscSortStats };
static const u_char* const sortStats[] = { weaponSortStats, weaponSortStats, bladeSortStats, gripSortStats,
    shieldSortStats, equipmentSortStats, miscSortStats };

#define ITEM_MASK 7          /* D_800F4EA0 bits that grey the whole list (MENU3 _itemNavigation init) */
#define TOP_LEVEL_ITEMS (4 | 0x40) /* MENU3's currentMenu from the top level (returnState of a Use) */
#define ITEMS_COMMAND 3      /* the top level's row (vs_battle_submenuStates[0]) */
#define LAST_USABLE 457      /* misc ids above: keys and sigils */
#define FIRST_MISC 323
#define LIST_MEMORY 30       /* cursor memory id of category c's list: 30 + c (unk3A.unk2[c]) */
#define ACTION_MEMORY 38     /* of its action menu: 38 + c (vs_mainMenu_addMenuActions) */
#define LIST_INFO 2          /* the list's rowInfo ((0x19200 >> 8) & 0xF): 10 - 2 rows on screen */
#define SFX_DISCARD 0x7E, 28 /* _discardMenu's confirm */
#define ACTION_SHOWN 7       /* vs_mainMenu_addMenuActions: rows on screen */
#define DISCARD_MEMORY 4     /* actionMenuDiscard: _discardMenu's Yes / No */
#define SORT_MEMORY 45       /* actionMenuSort: the sort menu of category c, 45 + c */
/* A list entry the sorts take. Divergence: an entry above the capacity (a
 * corrupt save) is skipped (and so dropped from the sorted list), where the
 * original reads past the category's records; nothing changes for a valid
 * save. */
#define LISTED(index, capacity) ((index) != 0 && (u_char)(index) <= (capacity))

static pc_ui_items snap;
static struct {
    int taken;     /* a Use was accepted on this tick (pc_ui_items_cast hands it over) */
    int row, rows; /* its row in the misc list, the list's rows */
    int index;     /* the record (vs_battle_miscItemInvoked) */
    int action, id;
} use;
static int resident; /* MAINMENU.PRG drives the top level (menu_bridge.c mainmenu_resident) */
static int filled;   /* the snapshot was filled on this tick */

static vs_battle_actor2* ashley(void)
{
    return vs_battle_characterState ? psx_get(vs_battle_characterState->unk3C) : NULL;
}

/* UTF-8 text into out, cut at a code point boundary (as equip_bridge.c). */
static void copy_text(char* out, int size, const char* text)
{
    int i;
    for (i = 0; i + 1 < size && text[i]; ++i)
        out[i] = text[i];
    if (((unsigned char)text[i] & 0xC0) == 0x80) {
        while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80)
            --i;
        if (i > 0)
            --i;
    }
    out[i] = 0;
}

static void help_text(char* out, int size, int index) { copy_text(out, size, pc_ui_item_help(index)); }

/* ---- MENU3 / MAINMENU, ported ---- */

/* MAINMENU vs_mainMenu_initItem: inventory_ops.c pc_inv_init_item. */
#define init_item pc_inv_init_item

/* MENU3 items2.h _getParentWeaponIndex (11): the weapon (1-based) a blade,
 * grip or gem is part of, 0 none. */
static int parent_weapon(int itemCategory, int index)
{
    int parentIndex = 0;

    if (itemCategory == PC_UI_IC_BLADE) {
        parentIndex = vs_main_inventory.blades[index].assembledWeaponIndex;
    }

    if (itemCategory == PC_UI_IC_GRIP) {
        parentIndex = vs_main_inventory.grips[index].assembledWeaponIndex;
    }

    if (itemCategory == PC_UI_IC_GEM) {
        parentIndex = vs_main_inventory.gems[index].setItemIndex;
        if (parentIndex & 0x80) { /* gemTargetShield */
            parentIndex = 0;
        }
    }

    return parentIndex;
}

/* _getParentShieldIndex (38): the shield (1-based) a gem is set in, 0 none. */
static int parent_shield(int itemCategory, int itemIndex)
{
    u_short index = 0;

    if (itemCategory == PC_UI_IC_GEM) {

        index = vs_main_inventory.gems[itemIndex].setItemIndex;

        if (!(index & 0x80)) {
            index = 0;
        }
    }

    return index & 0x7F;
}

/* _populateItemRows' misc action (items2.h:124, _ITEMMENU): the map's
 * action, a levelled grimoire's moved up per learned level. The map covers
 * ids 323..456; the original reads id 457 (listed, so usable unless its
 * action's flags grey it) from the byte after the table, which is padding: 0
 * in MAINMENU.PRG (file offset 0x8A14 + 134), so 0 here matches. Ids above
 * 457 (keys and sigils, never usable) get 0 too, where the original reads
 * further past the table (unused: their Use is greyed). */
static u_int misc_action(int id)
{
    u_int actionId = id >= FIRST_MISC && id - FIRST_MISC < (int)sizeof misc_item_actions
        ? misc_item_actions[id - FIRST_MISC]
        : 0;

    if ((actionId - 106) < 28) {
        int j;
        for (j = 0; j < 3; ++j) {
            if (vs_main_actions[actionId].unlocked) {
                ++actionId;
            }
        }
    }
    return actionId;
}

/* The discard row's rowType (_itemNavigation populateActions, items2.h:687):
 * 0 selectable; 1 equipped (or in the equipped weapon / shield), 2 part of a
 * weapon / shield in the bag, 3 a key or sigil. index: 0-based. */
static int discard_type(int itemCategory, int index)
{
    int rowType = 0;
    int var_a1_2;

    switch (itemCategory) {
    case PC_UI_IC_WEAPON:
        rowType = vs_main_inventory.weapons[index].isEquipped != 0;
        break;
    case PC_UI_IC_SHIELD:
        rowType = vs_main_inventory.shields[index].isEquipped != 0;
        break;
    case PC_UI_IC_ARMOR:
        rowType = vs_main_inventory.armor[index].limb != 0;
        break;
    case PC_UI_IC_MISC:
        if (vs_main_inventory.misc[index].id > LAST_USABLE) {
            rowType = 3;
        }
        break;
    }

    var_a1_2 = parent_weapon(itemCategory, index);

    if (var_a1_2 != 0) {
        rowType = 1;
        if (vs_main_inventory.weapons[var_a1_2 - 1].isEquipped == 0) {
            rowType = 2;
        }
    }

    var_a1_2 = parent_shield(itemCategory, index);

    if (var_a1_2 != 0) {
        rowType = 1;
        if (vs_main_inventory.shields[var_a1_2 - 1].isEquipped == 0) {
            rowType = 2;
        }
    }
    return rowType;
}

/* items1.h _getWeaponStatValue (695). */
static int weapon_stat(int stat, vs_battle_uiWeapon* weapon)
{
    switch (stat) {
    case 0:
        return -weapon->blade.category;
    case 1:
        return -weapon->blade.material;
    case 2:
        return weapon->range.range;
    case 3:
        return -weapon->damageType;
    case 4:
        return weapon->currentDp;
    case 5:
        return weapon->maxDp;
    case 6:
        return weapon->currentPp;
    case 7:
        return weapon->maxPp;
    case 8:
        return weapon->currentStr;
    case 9:
        return weapon->currentInt;
    case 10:
        return weapon->currentAgility;
    default:
        if (stat >= 17) {
            return weapon->classAffinityCurrent.affinity[0][stat - 17];
        }
        return weapon->classAffinityCurrent.class[0][stat - 11];
    }
}

/* _sortWeapons (731): repeated maximum; the rows with it, in list order. */
static void sort_weapons(int stat)
{
    vs_battle_uiWeapon uiWeapon;
    char sortedIndices[8];
    int i;
    int sortedPos;

    vs_main_inventoryWeapon* weapon = vs_main_inventory.weapons;
    char* indices = (char*)vs_main_inventoryIndices.weapons;

    vs_battle_rMemzero(sortedIndices, 8);

    sortedPos = 0;

    while (1) {
        int index;
        int maxValue = -INT_MAX - 1;

        for (i = 0; i < 8; ++i) {
            index = indices[i];

            if (LISTED(index, 8)) {
                int value;

                vs_battle_applyWeapon(&uiWeapon, &weapon[index - 1]);

                value = weapon_stat(stat, &uiWeapon);

                if (maxValue < value) {
                    maxValue = value;
                }
            }
        }

        if (maxValue == -INT_MAX - 1) {
            break;
        }

        for (i = 0; i < 8; ++i) {
            index = indices[i];

            if (LISTED(index, 8)) {

                vs_battle_applyWeapon(&uiWeapon, &weapon[index - 1]);

                if (weapon_stat(stat, &uiWeapon) == maxValue) {
                    sortedIndices[sortedPos++] = index;
                    indices[i] = 0;
                }
            }
        }
    }

    vs_battle_memcpy(indices, sortedIndices, 8);
}

/* _getShieldStatValue (792). */
static int shield_stat(int stat, vs_battle_uiShield* shield)
{
    switch (stat) {
    case 1:
        return -shield->base.material;
    case 4:
        return shield->currentDp;
    case 5:
        return shield->maxDp;
    case 6:
        return shield->currentPp;
    case 7:
        return shield->maxPp;
    case 8:
        return shield->currentStr;
    case 9:
        return shield->currentInt;
    case 10:
        return shield->currentAgility;
    default:
        if (stat < 33) {
            return shield->classAffinityCurrent.class[0][stat - 27];
        }

        if (stat > 39) {
            return shield->types[stat - 39];
        }

        return shield->classAffinityCurrent.affinity[0][stat - 33];
    }
}

/* _sortShields (827). */
static void sort_shields(int stat)
{
    vs_battle_uiShield uiShield;
    char sortedIndices[8];
    int i;
    int sortedPos;

    vs_main_inventoryShield* shield = &vs_main_inventory.shields[0];
    char* indices = (char*)vs_main_inventoryIndices.shields;

    vs_battle_rMemzero(sortedIndices, sizeof sortedIndices);

    sortedPos = 0;

    while (1) {
        int index;
        int maxValue = -INT_MAX - 1;

        for (i = 0; i < 8; ++i) {
            int value;
            index = indices[i];

            if (LISTED(index, 8)) {

                vs_battle_applyShield(&uiShield, &shield[index - 1]);

                value = shield_stat(stat, &uiShield);

                if (maxValue < value) {
                    maxValue = value;
                }
            }
        }

        if (maxValue == -INT_MAX - 1) {
            break;
        }

        for (i = 0; i < 8; ++i) {
            index = indices[i];

            if (LISTED(index, 8)) {

                vs_battle_applyShield(&uiShield, &shield[index - 1]);

                if (shield_stat(stat, &uiShield) == maxValue) {
                    sortedIndices[sortedPos++] = index;
                    indices[i] = 0;
                }
            }
        }
    }

    vs_battle_memcpy(indices, sortedIndices, 8);
}

/* _getMiscStatValue (888): 0 = id (negated), else the count. */
static int misc_stat(int stat, vs_main_inventoryMisc* misc)
{
    if (stat == 0) {
        return -misc->id;
    }
    return misc->count;
}

/* _sortMisc (899). */
static void sort_misc(int stat)
{
    char sortedIndices[64];
    int i;
    int sortedPos;

    vs_main_inventoryMisc* misc = vs_main_inventory.misc;
    char* indices = (char*)vs_main_inventoryIndices.misc;

    vs_battle_rMemzero(sortedIndices, sizeof sortedIndices);
    sortedPos = 0;

    while (1) {
        int index;
        int maxValue = -INT_MAX - 1;

        for (i = 0; i < 0x40; ++i) {
            index = indices[i];

            if (LISTED(index, 64)) {
                int value = misc_stat(stat, &misc[index - 1]);

                if (maxValue < value) {
                    maxValue = value;
                }
            }
        }

        if (maxValue == -INT_MAX - 1) {
            break;
        }

        for (i = 0; i < 0x40; ++i) {
            index = indices[i];

            if (LISTED(index, 64) && (misc_stat(stat, &misc[index - 1]) == maxValue)) {
                sortedIndices[sortedPos++] = index;
                indices[i] = 0;
            }
        }
    }

    vs_battle_memcpy(indices, sortedIndices, sizeof sortedIndices);
}

/* _getEquipmentStatValue (949). */
static int equipment_stat(int stat, vs_battle_uiEquipment* equipment)
{
    switch (stat) {
    case 0:
        return -equipment->category;
    case 1:
        return -equipment->material;
    case 2:
        return equipment->range.range;
    case 3:
        return -equipment->damageType;
    case 4:
        return equipment->currentDp;
    case 5:
        return equipment->maxDp;
    case 6:
        return equipment->currentPp;
    case 7:
        return equipment->maxPp;
    case 8:
        return equipment->strength;
    case 9:
        return equipment->intelligence;
    case 10:
        return equipment->agility;
    default:
        if (stat >= 27) {
            stat -= 16;
        }

        if (stat < 17) {
            return equipment->classes[stat - 11];
        }

        if (stat > 23) {
            return equipment->types[stat - 23];
        }

        return equipment->affinities[stat - 17];
    }
}

/* _copyEquipment (994). */
static void copy_equipment(vs_battle_uiEquipment* equipment, int itemCategory, int itemIndex)
{
    switch (itemCategory) {
    case PC_UI_IC_BLADE:
        vs_battle_copyInventoryBladeStats(equipment, &vs_main_inventory.blades[itemIndex]);
        return;
    case PC_UI_IC_GRIP:
        vs_battle_copyInventoryGripStats(equipment, &vs_main_inventory.grips[itemIndex]);
        return;
    case PC_UI_IC_ARMOR:
        vs_battle_copyInventoryArmorStats(equipment, &vs_main_inventory.armor[itemIndex]);
        return;
    case PC_UI_IC_GEM:
        vs_battle_copyInventoryGemStats(equipment, &vs_main_inventory.gems[itemIndex]);
        return;
    }
}

/* _sortEquipment (1017). */
static void sort_equipment(int itemCategory, int stat)
{
    vs_battle_uiEquipment uiEquipment;
    int i;
    int sortedPos = 0;
    int capacity = capacities[itemCategory];
    {
        char sortedIndices[64];
        char* indices = (char*)lists[itemCategory];

        vs_battle_rMemzero(sortedIndices, capacity);

        while (1) {
            int index;
            int maxValue = -INT_MAX - 1;

            for (i = 0; i < capacity; ++i) {
                index = indices[i];

                if (LISTED(index, capacity)) {
                    int value;

                    copy_equipment(&uiEquipment, itemCategory, index - 1);

                    value = equipment_stat(stat, &uiEquipment);

                    if (maxValue < value) {
                        maxValue = value;
                    }
                }
            }

            if (maxValue == -INT_MAX - 1) {
                break;
            }

            for (i = 0; i < capacity; ++i) {
                index = indices[i];

                if (LISTED(index, capacity)) {

                    copy_equipment(&uiEquipment, itemCategory, index - 1);

                    if (equipment_stat(stat, &uiEquipment) == maxValue) {
                        sortedIndices[sortedPos++] = index;
                        indices[i] = 0;
                    }
                }
            }
        }

        vs_battle_memcpy(indices, sortedIndices, capacity);
    }
}

/* _sortItems' sort state (1155): the stat of row i of the category's list. */
static void sort_items(int itemCategory, int i)
{
    switch (itemCategory) {
    case PC_UI_IC_WEAPON:
        sort_weapons(weaponSortStats[i]);
        break;

    case PC_UI_IC_SHIELD:
        sort_shields(gripSortStats[i]);
        break;

    case PC_UI_IC_MISC:
        sort_misc(miscSortStats[i]);
        break;

    default:
        sort_equipment(itemCategory, sortStats[itemCategory][i]);
        break;
    }
}

/* _itemNavigation state 6 (items2.h:579): rows rowCount (the row moved,
 * 1-based) and i (the target, 1-based). */
static void move_item(int itemCategory, int rowCount, int i)
{
    u_char* indices = lists[itemCategory];
    int row = indices[i - 1];
    int var_a1_2 = indices[rowCount - 1];

    if ((itemCategory == PC_UI_IC_MISC)
        && (vs_main_inventory.misc[row - 1].id == vs_main_inventory.misc[var_a1_2 - 1].id)) {

        vs_main_inventory.misc[var_a1_2 - 1].count += vs_main_inventory.misc[row - 1].count;

        if (vs_main_inventory.misc[var_a1_2 - 1].count > 100) {
            vs_main_inventory.misc[row - 1].count = vs_main_inventory.misc[var_a1_2 - 1].count - 100;
            vs_main_inventory.misc[var_a1_2 - 1].count = 100;
        } else {
            /* _ITEMMENU (MENU3): the target's record; the moved one keeps the
             * sum. The Loot screen's Organize panel uses this too, where
             * MENUB's copy (items2.h without _ITEMMENU) initialises the moved
             * record, the one holding the sum (the original loses the moved
             * stack): a deliberate difference, loot_bridge.c */
            init_item(PC_UI_IC_MISC, row);
        }
    } else {
        indices[i - 1] = var_a1_2;
        indices[rowCount - 1] = row;
    }
}

/* ---- The snapshot ---- */

/* Why the menu's disable mask greys the list ("" when clear): the lowest bit
 * the Items command cares about, in the game's message order (as the shell's
 * rows). */
static void mask_reason(char* out, int size)
{
    u_int mask = (u_int)D_800F4EA0 & ITEM_MASK;
    out[0] = 0;
    if (mask & 2)
        copy_text(out, size, "Not while carrying a cube.");
    else if (mask & 4)
        copy_text(out, size, "Not while hanging from a ledge.");
    else if (mask)
        copy_text(out, size, "Not usable right now.");
}

/* Row text and kind (vs_mainMenu_set*Row, 2D10.c:130-360). */
static void row_text(pc_ui_item_row* e, int c, int index)
{
    char kind[64] = "";
    switch (c) {
    case PC_UI_IC_WEAPON: {
        vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[index];
        vs_main_inventoryBlade* b = &vs_main_inventory.blades[w->blade - 1];
        pc_ui_text_decode((const unsigned char*)w->name, sizeof w->name, e->name, sizeof e->name);
        copy_text(kind, sizeof kind, pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_dagger + (u_char)b->category - 1));
        e->material = b->material;
        break;
    }
    case PC_UI_IC_BLADE: {
        vs_main_inventoryBlade* b = &vs_main_inventory.blades[index];
        copy_text(e->name, sizeof e->name, pc_ui_item_name(b->id));
        copy_text(kind, sizeof kind, pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_dagger + (u_char)b->category - 1));
        help_text(e->help, sizeof e->help, b->id - 1 + VS_ITEMHELP_BIN_INDEX_battleKnifeDesc);
        e->material = b->material;
        break;
    }
    case PC_UI_IC_GRIP: {
        vs_main_inventoryGrip* g = &vs_main_inventory.grips[index];
        copy_text(e->name, sizeof e->name, pc_ui_item_name(g->id));
        copy_text(kind, sizeof kind, pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_shortGrip + g->category - 1));
        help_text(e->help, sizeof e->help, g->id - 6);
        break;
    }
    case PC_UI_IC_SHIELD: {
        vs_main_inventoryShield* sh = &vs_main_inventory.shields[index];
        copy_text(e->name, sizeof e->name, pc_ui_item_name(sh->base.id));
        e->material = sh->base.material;
        break;
    }
    case PC_UI_IC_ARMOR: {
        vs_main_inventoryArmor* a = &vs_main_inventory.armor[index];
        copy_text(e->name, sizeof e->name, pc_ui_item_name(a->id));
        if (a->category == 7) {
            help_text(e->help, sizeof e->help, a->id - 223 + VS_ITEMHELP_BIN_INDEX_roodNecklace);
            copy_text(kind, sizeof kind, "Accessory");
        } else {
            copy_text(kind, sizeof kind, pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_armorCategory + a->category - 1));
            e->material = a->material;
        }
        break;
    }
    case PC_UI_IC_GEM: {
        vs_main_inventoryGem* g = &vs_main_inventory.gems[index];
        copy_text(e->name, sizeof e->name, pc_ui_item_name(g->id));
        help_text(e->help, sizeof e->help, g->id - 261 + VS_ITEMHELP_BIN_INDEX_gemFeldspar);
        break;
    }
    default: {
        vs_main_inventoryMisc* m = &vs_main_inventory.misc[index];
        copy_text(e->name, sizeof e->name, pc_ui_item_name(m->id));
        help_text(e->help, sizeof e->help, m->id - FIRST_MISC + VS_ITEMHELP_BIN_INDEX_cureRootDesc);
        e->count = m->count;
        break;
    }
    }
    if (e->material > 0 && kind[0]) {
        char both[128];
        int n = 0;
        const char* mat = pc_ui_material_name(e->material);
        while (*mat && n < 60)
            both[n++] = *mat++;
        both[n++] = ' ';
        copy_text(both + n, (int)sizeof both - n, kind);
        copy_text(e->sub, sizeof e->sub, both);
    } else {
        copy_text(e->sub, sizeof e->sub, e->material > 0 ? pc_ui_material_name(e->material) : kind);
    }
}

static void fill_row(pc_ui_item_row* e, int c, int index)
{
    int t, p;
    e->inventory = index;
    e->id = pc_ui_item_record_id(c, index);
    e->parent_category = e->parent = -1;
    row_text(e, c, index);

    /* the row's icon (_populateItemRows): worn / equipped, or part of a set */
    if ((c == PC_UI_IC_WEAPON && vs_main_inventory.weapons[index].isEquipped)
        || (c == PC_UI_IC_SHIELD && vs_main_inventory.shields[index].isEquipped)
        || (c == PC_UI_IC_ARMOR && vs_main_inventory.armor[index].limb))
        e->mark = PC_UI_MARK_EQUIPPED;
    if ((p = parent_weapon(c, index)) != 0) {
        e->mark = vs_main_inventory.weapons[p - 1].isEquipped ? PC_UI_MARK_EQUIPPED : PC_UI_MARK_SET;
        e->parent_category = PC_UI_IC_WEAPON;
        e->parent = p - 1;
    }
    if ((p = parent_shield(c, index)) != 0) {
        e->mark = vs_main_inventory.shields[p - 1].isEquipped ? PC_UI_MARK_EQUIPPED : PC_UI_MARK_SET;
        e->parent_category = PC_UI_IC_SHIELD;
        e->parent = p - 1;
    }

    /* the action menu (populateActions) */
    t = discard_type(c, index);
    e->discard = !t && !snap.locked;
    if (snap.locked)
        copy_text(e->discard_reason, sizeof e->discard_reason, snap.locked_reason);
    else if (t)
        help_text(e->discard_reason, sizeof e->discard_reason, t + VS_ITEMHELP_BIN_INDEX_cannotDiscardEquipped - 1);
    if (c == PC_UI_IC_MISC) {
        int id = vs_main_inventory.misc[index].id;
        e->discard_all = id <= LAST_USABLE && !snap.locked;
        if (snap.locked)
            copy_text(e->discard_all_reason, sizeof e->discard_all_reason, snap.locked_reason);
        else if (id > LAST_USABLE)
            help_text(e->discard_all_reason, sizeof e->discard_all_reason, VS_ITEMHELP_BIN_INDEX_cannotDiscard);
        e->action = (int)misc_action(id);
        if (snap.locked)
            copy_text(e->use_reason, sizeof e->use_reason, snap.locked_reason);
        else if (id > LAST_USABLE)
            help_text(e->use_reason, sizeof e->use_reason, VS_ITEMHELP_BIN_INDEX_cannotUse);
        else if (vs_battle_getActionFlags(0, e->action) & 2)
            pc_ui_text_decode((const unsigned char*)&menu3_text[VS_menuText_OFFSET_insufficientMp], 256,
                e->use_reason, sizeof e->use_reason);
        else if (!resident) /* MAINMENU still loading: refused until it is */
            copy_text(e->use_reason, sizeof e->use_reason, "Loading\xE2\x80\xA6");
        e->usable = e->use_reason[0] == 0;
    }
}

/* ---- Details (the focused row's card) ---- */

static unsigned hash_bytes(unsigned h, const void* data, unsigned size)
{
    const unsigned char* p = data;
    while (size--)
        h = (h ^ *p++) * 16777619u; /* FNV-1a */
    return h;
}

static struct {
    int valid, category, inventory;
    unsigned key;
    pc_ui_item_details d;
} details_cache;

static void gem_names(pc_ui_item_details* d, const u_char* gems, int slots)
{
    int i;
    d->gem_slots = slots;
    for (i = 0; i < 3 && i < slots; ++i)
        if (gems[i]) {
            copy_text(d->gems[d->gem_count], sizeof d->gems[0],
                pc_ui_item_name(vs_main_inventory.gems[gems[i] - 1].id));
            ++d->gem_count;
        }
}

static void equipment_stats(pc_ui_item_details* d, vs_battle_uiEquipment* e, int dp)
{
    int k;
    if (dp) {
        d->stats.dp = (e->currentDp + 99) / 100;
        d->stats.dp_max = (e->maxDp + 99) / 100;
        d->stats.pp = e->currentPp;
        d->stats.pp_max = e->maxPp;
    }
    d->stats.str = e->strength;
    d->stats.intel = e->intelligence;
    d->stats.agl = e->agility;
    for (k = 0; k < PC_UI_CLASSES; ++k)
        d->stats.classes[k] = e->classes[k];
    for (k = 0; k < PC_UI_AFFINITIES; ++k)
        d->stats.affinities[k] = e->affinities[k];
    for (k = 0; k < 3; ++k)
        d->types[k] = e->types[k + 1];
}

static void fill_details(pc_ui_item_details* d, int c, int index)
{
    int k;
    vs_main_bzero(d, sizeof *d);
    d->category = c;
    d->inventory = index;
    d->stats.inventory = index;
    d->stats.damage_type = -1;
    switch (c) {
    case PC_UI_IC_WEAPON: {
        vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[index];
        vs_battle_uiWeapon u;
        pc_ui_equip_describe(0, index, &d->stats);
        vs_battle_applyWeapon(&u, w);
        d->range = u.range.range;
        d->risk = u.risk;
        d->blade_category = (u_char)u.blade.category;
        copy_text(d->blade, sizeof d->blade, pc_ui_item_name(vs_main_inventory.blades[w->blade - 1].id));
        if (w->grip)
            copy_text(d->grip, sizeof d->grip, pc_ui_item_name(vs_main_inventory.grips[w->grip - 1].id));
        for (k = 0; k < 3; ++k)
            d->types[k] = u.grip.types[k + 1];
        gem_names(d, w->gems, w->grip ? vs_main_inventory.grips[w->grip - 1].gemSlots : 0);
        break;
    }
    case PC_UI_IC_SHIELD: {
        vs_main_inventoryShield* sh = &vs_main_inventory.shields[index];
        vs_battle_uiShield u;
        pc_ui_equip_describe(1, index, &d->stats);
        vs_battle_applyShield(&u, sh);
        for (k = 0; k < 3; ++k)
            d->types[k] = (u_char)u.types[k + 1];
        d->armor_category = (u_char)sh->base.category;
        gem_names(d, sh->gems, sh->base.gemSlots);
        break;
    }
    case PC_UI_IC_ARMOR: {
        vs_main_inventoryArmor* a = &vs_main_inventory.armor[index];
        pc_ui_equip_describe(2, index, &d->stats);
        d->armor_category = (u_char)a->category;
        if (a->category == 7) {
            vs_battle_uiAccessory u;
            vs_battle_applyAccessory(&u, a);
            for (k = 0; k < 3; ++k)
                d->types[k] = (short)u.types[k + 1];
        } else {
            vs_battle_uiArmor u;
            vs_battle_applyArmor(&u, a);
            for (k = 0; k < 3; ++k)
                d->types[k] = (short)u.types[k + 1];
        }
        break;
    }
    default: { /* blades, grips, gems: MENU3's status pages (vs_mainMenu_setStatsFrom*) */
        vs_battle_uiEquipment e;
        if (c == PC_UI_IC_MISC)
            break;
        copy_equipment(&e, c, index);
        copy_text(d->stats.name, sizeof d->stats.name, pc_ui_item_name(e.id));
        if (c == PC_UI_IC_BLADE) {
            copy_text(d->stats.material, sizeof d->stats.material, pc_ui_material_name(e.material));
            equipment_stats(d, &e, 1);
            d->stats.damage_type = e.damageType >= 1 && e.damageType <= 3 ? e.damageType - 1 : -1;
            d->stats.two_handed = (u_char)e.category >= 1 && ((0x1D4 >> ((u_char)e.category - 1)) & 1);
            d->range = e.range.range;
            d->risk = e.cost;
            d->blade_category = (u_char)e.category;
        } else {
            equipment_stats(d, &e, 0);
            if (c == PC_UI_IC_GRIP) {
                d->grip_category = (u_char)e.category;
                d->gem_slots = e.gemSlots;
            }
        }
        break;
    }
    }
    d->valid = 1;
}

static void details(void)
{
    int c, inv;
    unsigned key;
    if (!pc_ui_shell_items_focus(&c, &inv) || c < 0 || c >= PC_UI_ITEM_CATEGORIES || inv < 0
        || inv >= capacities[c] || !pc_ui_item_record_id(c, inv)) {
        details_cache.valid = 0;
        return;
    }
    key = hash_bytes(2166136261u, &vs_main_inventory, sizeof vs_main_inventory);
    if (ashley())
        key = hash_bytes(key, &ashley()->weapon, sizeof ashley()->weapon);
    if (!details_cache.valid || details_cache.category != c || details_cache.inventory != inv
        || details_cache.key != key) {
        fill_details(&details_cache.d, c, inv);
        details_cache.valid = 1;
        details_cache.category = c;
        details_cache.inventory = inv;
        details_cache.key = key;
    }
    snap.details = details_cache.d;
}

static void fill(void)
{
    vs_battle_actor2* a = ashley();
    int c, i, n = 0;

    vs_main_bzero(&snap, sizeof snap);
    filled = 1;
    if (!a) {
        pc_ui_shell_set_items(&snap);
        return;
    }
    snap.valid = 1;
    snap.mask = (u_int)D_800F4EA0 & ITEM_MASK;
    snap.locked = snap.mask != 0;
    mask_reason(snap.locked_reason, sizeof snap.locked_reason);
    for (c = 0; c < PC_UI_ITEM_CATEGORIES; ++c) {
        u_char* indices = lists[c];
        snap.capacity[c] = capacities[c];
        snap.first[c] = n;
        /* _populateItemRows: the list up to its first empty entry. Divergence:
         * an entry above the capacity (a corrupt save) ends it too. */
        for (i = 0; i < capacities[c] && indices[i] && indices[i] <= capacities[c]; ++i)
            fill_row(&snap.rows[n++], c, indices[i] - 1);
        snap.count[c] = n - snap.first[c];
        snap.sort_count[c] = sortStatSizes[c];
        for (i = 0; i < sortStatSizes[c]; ++i)
            snap.sort_stats[c][i] = sortStats[c][i];
    }
    for (i = 0; i < PC_UI_STAT_NAMES; ++i) {
        help_text(snap.stat_name[i], sizeof snap.stat_name[i], i * 2 + VS_ITEMHELP_BIN_INDEX_sortRange - 4);
        help_text(snap.stat_help[i], sizeof snap.stat_help[i], i * 2 + VS_ITEMHELP_BIN_INDEX_sortRangeDesc - 4);
    }
    help_text(snap.no_search, sizeof snap.no_search, VS_ITEMHELP_BIN_INDEX_invalidSearch);
    for (i = 0; i < 3; ++i)
        help_text(snap.discard_prompt[i], sizeof snap.discard_prompt[i], VS_ITEMHELP_BIN_INDEX_discardConfirm + i);
    details();
    pc_ui_shell_set_items(&snap);
}

/* ---- Cursor memories ----
 *
 * MAINMENU keeps each menu's cursor in D_800F4EE8.cursorMemories as {row on
 * screen, page} at 2 * id (58EC.c): a list (vs_mainmenu_setMenuRows) or an
 * action menu (vs_mainMenu_addMenuActions) opens from it, normalised to its
 * row count, and stores its cursor when it is confirmed or left. The Items
 * lists' memories (30 + category) lie past cursorMemories[52], in
 * D_800F4EE8.unk3A.unk2, as the original addresses them. */

/* The open's normalisation (setMenuRows and addMenuActions alike): `rows`
 * rows, `shown` on screen. */
static void cursor_open(int* sel, int* page, int rows, int shown)
{
    int scrollable = rows - shown;
    int i = *sel, p = *page;

    if (scrollable < 0) {
        i += p;
        p = 0;
        if (i >= rows) {
            i = 0;
        }
    } else {
        if (scrollable < p) {
            if ((i + p) >= rows) {
                i = 0;
                p = 0;
            } else {
                i += p - scrollable;
                p = scrollable;
            }
        }
        if ((p > 0) && (i == 0)) {
            i = 1;
            --p;
        }
        if ((p < scrollable) && (i == shown - 1)) {
            --i;
            ++p;
        }
    }
    *sel = i;
    *page = p;
}

/* Down and Up (vs_mainMenu_renderItemsList, vs_mainMenu_actionInputProcessed):
 * a list that fits wraps; a longer one keeps a row above / below the cursor
 * while it can scroll and wraps (on a press) from its last / first row. */
static void cursor_down(int* sel, int* page, int rows, int shown)
{
    if (rows <= shown) {
        *sel = *sel == rows - 1 ? 0 : *sel + 1;
    } else if (*page == rows - shown) {
        if (*sel == shown - 1) {
            *sel = 0;
            *page = 0;
        } else {
            ++*sel;
        }
    } else if (*sel == shown - 2) {
        ++*page;
    } else {
        ++*sel;
    }
}

static void cursor_up(int* sel, int* page, int rows, int shown)
{
    if (rows <= shown) {
        *sel = *sel == 0 ? rows - 1 : *sel - 1;
    } else if (*page == 0) {
        if (*sel == 0) {
            *sel = shown - 1;
            *page = rows - shown;
        }  else {
            --*sel;
        }
    } else if (*sel == 1) {
        --*page;
    } else {
        --*sel;
    }
}

static void remember(int id, int sel, int page)
{
    char* memories = (char*)&D_800F4EE8;
    memories[id * 2] = sel;
    memories[id * 2 + 1] = page;
}

/* Menu `id` of `rows` rows (`shown` on screen) opened (from its memory, or
 * on its first row) and its cursor moved straight to row `target`, as the
 * original stores it on the confirm. The one modelled path: the shell's
 * cursor is its own, so where the original's would scroll on the way the
 * walk is the straight one from where the original's menu opens (Down to a
 * row below it, Up to one above, no wrap); in a list that fits on screen
 * every path ends the same. */
static void cursor_at(int id, int rows, int shown, int target, int from_memory)
{
    char* memories = (char*)&D_800F4EE8;
    int sel = 0, page = 0, n;

    if (rows <= 0 || target < 0 || target >= rows) {
        return;
    }
    if (from_memory) {
        sel = memories[id * 2];
        page = memories[id * 2 + 1];
    }
    cursor_open(&sel, &page, rows, shown);
    for (n = 0; n <= 2 * rows && sel + page != target; ++n) {
        if (target > sel + page) {
            cursor_down(&sel, &page, rows, shown);
        } else {
            cursor_up(&sel, &page, rows, shown);
        }
    }
    remember(id, sel, page);
}

/* Category c's list confirmed on `row` (of `rows`): MENU3 sets its lists up
 * with the memory forced on (_itemNavigation init; the first list of an
 * Items entry without cursor memory opens on row 0, which the zeroed
 * memories give too, see pc_ui_items_fill) and the category is the current
 * one. */
static void list_cursor(int c, int row, int rows)
{
    D_800F4EE8.unk3A.currentItemCategory = (c + 1) & 7;
    cursor_at(LIST_MEMORY + c, rows, 10 - LIST_INFO, row, 1);
}

/* An action of populateActions' menu confirmed: its row there (Use for
 * misc; Search for blades, grips, gems; Discard; Discard all for misc; Sort). */
static void action_cursor(int c, int action)
{
    int row = 0;

    if (c == PC_UI_IC_MISC) {
        if (action == PC_UI_ITA_USE) {
            goto found;
        }
        ++row;
    }
    if (c == PC_UI_IC_BLADE || c == PC_UI_IC_GRIP || c == PC_UI_IC_GEM) {
        if (action == PC_UI_ITA_SEARCH) {
            goto found;
        }
        ++row;
    }
    if (action == PC_UI_ITA_DISCARD) {
        goto found;
    }
    ++row;
    if (c == PC_UI_IC_MISC) {
        if (action == PC_UI_ITA_DISCARD_ALL) {
            goto found;
        }
        ++row;
    }
found: /* at most four rows: no page */
    remember(ACTION_MEMORY + c, row, 0);
}

/* ---- The tick ---- */

static int items_shown; /* the Items screen showed on the last tick of this session */

/* A menu session opens (menu_bridge.c): fresh (not a return from a cancelled
 * target or an original screen), the next Items screen is a new entry. */
void pc_ui_items_session(int fresh)
{
    if (fresh) {
        items_shown = 0;
    }
}

/* The Items screen shows: entered now, without cursor memory, its memories
 * start over as MENU3's do at each entry from the top level (_itemNavigation
 * with initialize 1: unk3A zeroed when vs_main_settings.cursorMemory is 0). */
static void items_enter(void)
{
    if (!items_shown && vs_main_settings.cursorMemory == 0) {
        vs_main_bzero(&D_800F4EE8.unk3A, sizeof D_800F4EE8.unk3A);
    }
    items_shown = 1;
}

static int items_showing(void) { return pc_ui_shell_screen() == PC_UI_SCREEN_ITEMS && pc_ui_shell_visible(); }

/* Every category list ends rebuilt as MENU3's init state leaves it, and the
 * snapshot is filled (and handed to the shell). */
static void changed(void)
{
    int c;
    for (c = 0; c < PC_UI_ITEM_CATEGORIES; ++c)
        pc_ui_items_rebuild(c);
    fill();
}

/* The tick starts (menu_bridge.c, also when a session opens): no hand-off
 * yet, MAINMENU's residency for this tick; while the Items screen shows, the
 * list order as MENU3's Items menu keeps it and a fresh snapshot (the one
 * full fill and copy of the tick). */
void pc_ui_items_fill(int mainmenu)
{
    use.taken = 0;
    resident = mainmenu;
    filled = 0;
    if (!items_showing()) {
        if (pc_ui_shell_visible()) /* another tab: the next Items is an entry (hidden: kept) */
            items_shown = 0;
        return;
    }
    items_enter();
    changed();
}

/* After the shell's tick and its requests: a tab switched to Items on this
 * tick gets its snapshot before it draws; otherwise only the details card
 * follows a moved focus (the rows are this tick's already). */
void pc_ui_items_refresh(void)
{
    if (!items_showing()) {
        if (pc_ui_shell_visible())
            items_shown = 0;
        return;
    }
    if (!filled) {
        items_enter();
        changed();
        return;
    }
    details();
    pc_ui_shell_set_items_details(&snap.details);
}

/* Returns 1 when the request was one of ours. */
int pc_ui_items_request(const pc_ui_request* r)
{
    int c = r->slot, row = r->item, index;
    const pc_ui_item_row* e;
    if (r->kind < PC_UI_REQ_ITEM_USE || r->kind > PC_UI_REQ_ITEM_CANCEL)
        return 0;
    if (r->kind == PC_UI_REQ_ITEM_CANCEL) {
        /* the picker left with none, or a Yes / No on No (_discardMenu): the
         * leave sound, and the memories the original's menus stored on the
         * way (its list, its action menu, the Yes / No on No) */
        vs_battle_playMenuLeaveSfx();
        if (snap.valid && !snap.locked && c >= 0 && c < PC_UI_ITEM_CATEGORIES && row >= 0 && row < snap.count[c]
            && (r->shield < 0 || r->shield == snap.rows[snap.first[c] + row].inventory)) {
            list_cursor(c, row, snap.count[c]);
            action_cursor(c, r->count == 2 ? PC_UI_ITA_DISCARD_ALL : PC_UI_ITA_DISCARD);
            if (r->count != 0)
                remember(DISCARD_MEMORY, 1, 0);
        }
        return 1;
    }
    fill(); /* the snapshot as the game is now (an earlier request of this tick may have changed it) */
    if (!snap.valid || snap.locked || !resident || c < 0 || c >= PC_UI_ITEM_CATEGORIES)
        goto refused;
    /* a Use, break art or spell taken on this tick ends the menu: nothing
     * else changes the game on the same tick */
    if (r->kind != PC_UI_REQ_ITEM_USE && (use.taken || pc_ui_abilities_cast_taken() || pc_ui_magick_cast_taken()))
        goto refused;
    if (row < 0 || row >= snap.count[c])
        goto refused;
    e = &snap.rows[snap.first[c] + row];
    if (r->kind == PC_UI_REQ_ITEM_MOVE) {
        if (r->count >= 0 && r->count != e->inventory)
            goto refused;
        if (r->shield < 0 || r->shield >= snap.count[c] || r->shield == row)
            goto refused;
        /* _itemNavigation state 6: the list confirmed on the row, then on the target */
        list_cursor(c, row, snap.count[c]);
        list_cursor(c, r->shield, snap.count[c]);
        vs_battle_playMenuSelectSfx();
        move_item(c, row + 1, r->shield + 1);
        changed();
        return 1;
    }
    if (r->shield >= 0 && r->shield != e->inventory)
        goto refused;
    index = e->inventory;
    switch (r->kind) {
    case PC_UI_REQ_ITEM_SORT: {
        int i;
        for (i = 0; i < sortStatSizes[c]; ++i)
            if (sortStats[c][i] == r->count)
                break;
        if (i == sortStatSizes[c])
            goto refused;
        list_cursor(c, row, snap.count[c]);
        action_cursor(c, PC_UI_ITA_SORT);
        cursor_at(SORT_MEMORY + c, sortStatSizes[c], ACTION_SHOWN, i, vs_main_settings.cursorMemory != 0);
        vs_battle_playMenuSelectSfx();
        sort_items(c, i);
        changed();
        return 1;
    }
    case PC_UI_REQ_ITEM_USE:
        /* the shell still up (it returned no result this tick), no model
         * reload of the Equipment screen in flight, one hand-off per tick */
        if (c != PC_UI_IC_MISC || !e->usable || use.taken || pc_ui_abilities_cast_taken() || pc_ui_magick_cast_taken()
            || !pc_ui_shell_visible() || pc_ui_equip_busy())
            goto refused;
        use.taken = 1;
        use.row = row;
        use.rows = snap.count[c];
        use.index = index;
        use.action = e->action;
        use.id = e->id;
        return 1;
    case PC_UI_REQ_ITEM_DISCARD:
        if (!e->discard)
            goto refused;
        if (c == PC_UI_IC_MISC && vs_main_inventory.misc[index].count >= 2) {
            /* _discardMenu discardMultiple (the picker's none is ITEM_CANCEL) */
            if (r->count < 1 || r->count > vs_main_inventory.misc[index].count)
                goto refused;
            list_cursor(c, row, snap.count[c]);
            action_cursor(c, PC_UI_ITA_DISCARD);
            remember(DISCARD_MEMORY, 0, 0); /* Yes */
            vs_main_playSfxDefault(SFX_DISCARD);
            if (vs_main_inventory.misc[index].count == r->count) {
                init_item(c, index + 1);
            } else {
                vs_main_inventory.misc[index].count = vs_main_inventory.misc[index].count - r->count;
            }
        } else {
            /* discardOne */
            list_cursor(c, row, snap.count[c]);
            action_cursor(c, PC_UI_ITA_DISCARD);
            remember(DISCARD_MEMORY, 0, 0);
            vs_main_playSfxDefault(SFX_DISCARD);
            if (c == PC_UI_IC_WEAPON) {
                int slot = pc_weapon_slots_get(index);
                if (slot)
                    pc_weapon_slots_clear(slot);
            }
            init_item(c, index + 1);
        }
        changed();
        return 1;
    case PC_UI_REQ_ITEM_DISCARD_ALL:
        if (c != PC_UI_IC_MISC || !e->discard_all)
            goto refused;
        list_cursor(c, row, snap.count[c]);
        action_cursor(c, PC_UI_ITA_DISCARD_ALL);
        remember(DISCARD_MEMORY, 0, 0);
        vs_main_playSfxDefault(SFX_DISCARD);
        init_item(c, index + 1);
        changed();
        return 1;
    }
refused:
    vs_battle_playInvalidSfx();
    return 1;
}

/* A Use was accepted on this tick (menu_bridge.c, after the requests). */
int pc_ui_items_cast_taken(void) { return use.taken; }

/* menu_bridge.c, the accepted Use: go (the shell returned no result, the
 * menu closes for it) writes MENU3's hand-off, otherwise it is dropped and
 * nothing of the game's state was touched. The game state is MENU3's
 * hand-off byte for byte (the cursor memories as its lists leave them when
 * the same row is confirmed: the list, then Use in the action menu); only a
 * cancelled target differs (the shell comes back). */
void pc_ui_items_cast(int go)
{
    int taken = use.taken;
    use.taken = 0;
    if (!go || !taken)
        return;
    vs_battle_submenuStates[0] = ITEMS_COMMAND;
    list_cursor(PC_UI_IC_MISC, use.row, use.rows);
    action_cursor(PC_UI_IC_MISC, PC_UI_ITA_USE);
    /* _itemNavigation processAction useItem */
    vs_battle_miscItemInvoked = use.index;
    D_800F4E98.executeAbility.u16[0] = (short)(u_char)use.action;
    D_800F4E98.executeAbility.u16[1] = (short)vs_main_inventory.misc[vs_battle_miscItemInvoked].id;
    /* vs_menu3_exec: row 3, then state 12 */
    vs_battle_menuState.executeAbilityType = 13;
    D_800F4E98.unk2 = 8;
    vs_battle_menuState.returnState = TOP_LEVEL_ITEMS;
}

/* loot_bridge.c (the Loot screen's Organize panel: MENUB hands the bag to
 * the same Items menu, items2.h _itemNavigation, whose entry rebuilds every
 * list): the bag as the Items screen lists it, every list rebuilt first,
 * MAINMENU's residency for the requests that follow (pc_ui_items_request:
 * ITEM_DISCARD, ITEM_DISCARD_ALL, ITEM_CANCEL as the Items screen sends
 * them). */
const pc_ui_items* pc_ui_items_bag(int mainmenu)
{
    use.taken = 0;
    resident = mainmenu;
    changed();
    return &snap;
}


/* loot_bridge.c: the details card of record `index` of category c of
 * vs_main_inventory (the Items screen's, as fill_details computes it). */
void pc_ui_items_describe(int c, int index, pc_ui_item_details* d) { fill_details(d, c, index); }
