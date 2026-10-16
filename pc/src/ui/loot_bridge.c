/* Modern menus, the Loot screen: the game side (battle builds).
 *
 * The game opens its loot menu itself: after a battle with drops
 * (BATTLE 146C.c:3539, 8423, 8554 func_800CB158(_lootListHead): the list of
 * drops, D_800EB9C4) and at a chest (146C.c:5354 func_800CB18C: the chest's
 * loot, D_800EB9C8); both func_800CACD0(0xC, 2), so MAINMENU's
 * vs_mainMenu_exec runs MENUB's vs_menuB_exec as direct entry 12. With
 * ui_menus modern the direct-entry hook (menu_bridge.c pc_ui_direct_tick)
 * hands that session to this file instead: pc_ui_loot_begin on its first tick
 * (*state 0), pc_ui_loot_frame every tick after, which speaks vs_menuB_exec's
 * protocol (0 stay, 1 done).
 *
 * Everything that changes the game is MENUB (src/MENU/MENUB.PRG/260.c),
 * ported with its types and expressions and called in the original's order,
 * on the original's data: the loot's own inventory and lists in the game's
 * heap (vs_menuB_exec 1834: _inventory, _inventoryBackup, _lootList,
 * _lootListWorkingCopy, the same allocation), MAINMENU's
 * vs_mainMenu_copyItem / vs_mainMenu_rebuildInventory called directly
 * (MAINMENU.PRG is resident: this runs inside its vs_mainMenu_exec, which
 * only runs once MAINMENU has loaded, and vs_mainMenu_exec only reaches its
 * direct entries with the submenu loaded). So the bag, its index lists, the
 * key flags and D_800F4E98.unk2 end byte for byte as the original menu leaves
 * them (ui_loot_smoke). What is the original's presentation only (the rows'
 * animations, the information box, the item names MENUB loads for its rows,
 * cursor and scroll) is left out; the shell has its own.
 *
 *   open    vs_menuB_exec state 0 (1825): vs_battle_playSfx10, the base stats
 *           (vs_mainMenu_populateBaseStats(16)), every bag list rebuilt, the
 *           allocation, _applyAllLootLists(D_800EB9C4) (1077: the chest's
 *           single loot when the list pointer is null; per loot the drops of
 *           the current difficulty, _applyLootList 1031, each copied into the
 *           loot's inventory, _copy*ToInventory 681-834, then
 *           _updateLootList 836). An empty list shows the game's emptyChest
 *           line (state 1, 1863); any button ends it (state 4; unk2 is left
 *           as func_800CACD0 set it, 0).
 *   Take    _processLootMenu processInput Circle (1587): the row fits (its
 *           lootState): sfx 0x7E 0xD, a key's flag (_setKeyFlag 1966:
 *           vs_battle_setStateFlag(id + 438, 1)), vs_mainMenu_copyItem(c |
 *           0x10) into the bag, claimed; then state updateLoot (1770): the
 *           claimed rows out, _updateLootList, and with nothing left the
 *           session ends (_discardItems 1264: an empty list returns 1).
 *   Take all  Square (1548): every row copied that fits at its turn, keys'
 *           flags; sfx 0x7E 0xD when something was taken, else the invalid
 *           sound.
 *   Organize  Circle on a row that does not fit (select sound): MENUB's
 *           _organizeInventory (1389) offers Disassemble (weapons, shields),
 *           Consolidate (misc but keys) and Organize inventory:
 *     Disassemble  _disassembleItem (216): the weapon's blade, grip and gems
 *           (a shield's gems) join the end of the list as rows of their own,
 *           taken out of it in the loot's inventory (inventory_ops.c), the
 *           weapon's row gone; sfx 0x7E 0x19.
 *     Consolidate  _consolidateMiscItems (433): every row of that misc item
 *           merged and split again: first into the room the bag's stacks of
 *           it have (rows that fit), the rest in stacks of 100; the select
 *           sound.
 *     Organize inventory  _topLevelMenuTransition (53): the bag in the Items
 *           menu (items2.h, the same as MENU3's): the world frozen and the
 *           HUD slid out as it does (func_8008A4DC(0); when D_800EB9AF was
 *           0: func_800CB654(1), D_800EB9B0 0x200000; undone on the way
 *           back; D_800EB9AF as the game has it under the shell's own
 *           slide-out, menu_bridge.c), the current category set and, without cursor memory,
 *           unk3A cleared (_itemNavigation(1)); a category change sets
 *           the current category (its L1 / R1, the change sound); Discard
 *           (the quantity picker for a stack), Discard all, Sort and Move
 *           are the Items screen's own (items_bridge.c
 *           pc_ui_items_request). Deliberate difference: MENUB's copy of
 *           the Items menu differs from MENU3's only in a Move that merges
 *           two misc stacks into one, where it initialises the stack moved,
 *           the one holding the sum (the moved stack is lost); here the
 *           merge is MENU3's (the sum kept, never an item lost); back:
 *           _updateLootList (waitForTopMenu 1762).
 *   Leave   Cross (1537, the leave sound), _discardItems (1264): nothing left
 *           ends with 1, a chest nothing was taken from ends at once with 2;
 *           a key or sigil in the list cannot be left (invalidDiscard: the
 *           shell says so); else the Yes / No (on No): Yes sfx 0x7E 0x1C,
 *           ends with 2 if nothing was taken, else 1.
 *   end     vs_menuB_exec state 3 / 5 (1895, 1907): D_800F4E98.unk2 = (the
 *           result is 1); then, once text box 7 is dismissed
 *           (vs_battle_dismissTextBox(7)), the allocation freed, the drops
 *           list freed (func_8008A6FC) when the loot came from it; *state 0, return 1
 *           (vs_mainMenu_exec's func_800FAA5C then sets currentMenu 0x7F and
 *           gives the menu's resources back, as after MENUB).
 *
 * Each request is refused (the invalid sound) when it does not name the row
 * the screen saw (its record), as the other screens' requests. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/30DB0.h"
#include "../../../src/BATTLE/BATTLE.PRG/4A0A8.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "../../../src/MENU/MAINMENU.PRG/2D10.h"
#include "../../../src/MENU/MAINMENU.PRG/C48.h"
#include "build/assets/MENU/ITEMHELP.BIN.h"
#include "build/assets/MENU/MENUB.PRG/menuText.h"
#include "inventory_ops.h"
#include "item_names.h"
#include "ui_shell.h"

/* ui_text.h (stdio in a game-header unit is avoided) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);

int pc_ui_loot_begin(void);
int pc_ui_loot_frame(const pc_ui_input* in);
void pc_ui_loot_abandon(int reset);
int pc_ui_loot_debug_open(const unsigned char* data, int nodes, int chest);
int pc_ui_loot_debug_chest(int index, int out[12]);
int pc_ui_loot_debug_chest_open(int index);
int pc_ui_container_debug_open(int location);
/* menu_bridge.c: the HUD flag under the shell's slide-out */
char pc_ui_menu_hud_game(void);
void pc_ui_menu_hud_keep(void);
/* items_bridge.c / equip_bridge.c */
const pc_ui_items* pc_ui_items_bag(int mainmenu);
int pc_ui_items_request(const pc_ui_request* r);
void pc_ui_items_describe(int c, int index, pc_ui_item_details* d);
void pc_ui_equip_describe(int kind, int index, pc_ui_item* out);
int pc_ui_item_record_id(int category, int i);

/* MENUB.PRG menuText (its own strings; MENU3's has the same names). */
static u_short menuB_text[] = {
#include "build/assets/MENU/MENUB.PRG/menuText.vsString"
};
#define MENU_TEXT(offset) ((const unsigned char*)&menuB_text[offset])

/* MENUB's lootItemState and _lootListItem (260.c:15). */
enum { lootStateNone, lootStateCopied, lootStateNoIndent, lootStateIndent1, lootStateIndent2, lootStateIndent3 };
typedef struct {
    char itemCategory;
    u_char itemIndex;
    char unk2;
    char state;
} loot_item;

#define LIST_ENTRIES 160 /* _lootList / _lootListWorkingCopy entries (vs_menuB_exec's allocation) */
#define LAST_USABLE 457  /* misc ids above: keys and sigils */
static const u_char capacities[PC_UI_ITEM_CATEGORIES] = { 8, 16, 16, 8, 16, 48, 64 };

static struct {
    int active;
    vs_main_inventory_t* inventory; /* _inventory: the loot's own records */
    vs_main_inventory_t* backup;    /* _inventoryBackup */
    loot_item* list;                /* _lootList */
    loot_item* working;             /* _lootListWorkingCopy */
    u_char count;                   /* _lootListCount */
    char claimed;                   /* _lootClaimed */
    int empty;                      /* the list came empty (vs_menuB_exec state 4) */
    int bag;                        /* the Organize inventory panel is up (_topLevelMenuTransition) */
    char hud_saved;                 /* its D_8010A681: D_800EB9AF as it was */
    int closing;                    /* the result is in, state 5 runs (vs_battle_dismissTextBox(7)) */
    int dirty;                      /* the list, the loot's inventory or the bag changed: rows refilled */
    int focus;                      /* the focus the focused row's texts and card were filled for */
} L;

static pc_ui_loot snap;
static vs_main_inventory_t swap_buf; /* _populateLootItem's swap, host side */

/* ---- MENUB, ported (260.c) ---- */

static int copy_blade(vs_main_inventoryBlade* source, int weapon) /* _copyBladeToInventory 681 */
{
    int index = 1;
    vs_main_inventoryBlade* blade = L.inventory->blades;

    while (blade->id != 0) {
        ++blade;
        ++index;
        if (index == 16) {
            return 0;
        }
    }

    vs_battle_copyAligned(blade, source, sizeof *blade);

    blade->assembledWeaponIndex = weapon;
    blade->index = index;
    return index;
}

static int copy_grip(vs_main_inventoryGrip* source, int weaponIndex) /* _copyGripToInventory 701 */
{
    int index = 1;
    vs_main_inventoryGrip* grip = L.inventory->grips;

    while (grip->id != 0) {
        ++grip;
        ++index;
        if (index == 16) {
            return 0;
        }
    }

    vs_battle_copyAligned(grip, source, sizeof *grip);

    grip->assembledWeaponIndex = weaponIndex;
    grip->index = index;
    return index;
}

static int copy_gem(vs_main_inventoryGem* source, int item) /* _copyGemToInventory 721 */
{
    int index = 1;
    vs_main_inventoryGem* gem = L.inventory->gems;

    while (gem->id != 0) {
        ++gem;
        ++index;
        if (index == 0x30) {
            return 0;
        }
    }

    vs_battle_copyAligned(gem, source, sizeof *gem);

    gem->setItemIndex = item;
    gem->index = index;
    return index;
}

static int copy_weapon(vs_battle_lootedWeapon* arg0) /* _copyWeaponToInventory 741 */
{
    int i;
    vs_main_inventoryWeapon* slot = L.inventory->weapons;
    int index = 1;

    while (slot->blade != 0) {
        ++slot;
        if (++index == 8) {
            return 0;
        }
    }

    vs_battle_rMemzero(slot, sizeof *slot);

    slot->index = index;
    slot->blade = copy_blade(&arg0->blade, index);
    slot->grip = copy_grip(&arg0->grip, index);

    for (i = 0; i < 3; i++) {
        if (arg0->gems[i].id != 0)
            slot->gems[i] = copy_gem(&arg0->gems[i], index);
    }

    vs_battle_rMemcpy(slot->name, arg0->name, sizeof slot->name);

    return index;
}

static int copy_armor(vs_main_inventoryArmor* source) /* _copyArmorToInventory 770 */
{
    int index = 1;
    vs_main_inventoryArmor* armor = L.inventory->armor;

    while (armor->id != 0) {
        ++armor;
        ++index;
        if (index == 16) {
            return 0;
        }
    }

    vs_battle_copyAligned(armor, source, sizeof *armor);

    armor->index = index;
    return index;
}

static int copy_shield(vs_battle_lootedShield* arg0) /* _copyShieldToInventory 789 */
{
    int i;

    vs_main_inventoryShield* shield = L.inventory->shields;
    int index = 1;

    while (shield->base.id != 0) {
        ++shield;
        ++index;
        if (index == 8) {
            return 0;
        }
    }

    vs_battle_rMemzero(shield, sizeof *shield);
    vs_battle_copyAligned(&shield->base, &arg0->shield, sizeof shield->base);

    shield->base.limb = index;

    for (i = 0; i < 3; ++i) {
        if (arg0->gems[i].id != 0) {
            shield->gems[i] = copy_gem(&arg0->gems[i], index | 0x80);
        }
    }

    return index;
}

static int copy_misc(vs_battle_lootedMisc* arg0) /* _copyMiscToInventory 818 */
{
    int index = 1;
    vs_main_inventoryMisc* item = L.inventory->misc;

    while (item->id != 0) {
        ++index;
        ++item;
        if (index == 64) {
            return 0;
        }
    }

    item->id = arg0->id;
    item->count = arg0->count;
    return index;
}

/* _updateLootList (836): every row's lootState from a vs_mainMenu_copyItem
 * probe (which rebuilds the misc list for a misc row). The cursor's part is
 * the shell's. */
static void update_list(void)
{
    int i;
    loot_item* loot = L.list;

    for (i = 0; i < L.count; ++i, ++loot) {
        loot->state = vs_mainMenu_copyItem(loot->itemCategory, &vs_main_inventory, loot->itemIndex, L.inventory) != 0;
    }
}

static void add_loot(int itemCategory, int itemIndex) /* _addLootToList 874 */
{
    loot_item* item = &L.list[L.count++];
    item->itemCategory = itemCategory;
    item->itemIndex = itemIndex - 1;
    item->unk2 = 0;
}

/* _itemIncludesItemsInCategory (885). */
static int includes(int arg0, loot_item* arg1)
{
    int i;
    vs_main_inventoryShield* shield;
    vs_main_inventoryWeapon* weapon;

    int category = arg1->itemCategory;
    int count = 0;

    switch (arg0) {
    case itemCategoryBlade:
    case itemCategoryGrip:
        count += category == itemCategoryWeapon;
        // Fallthrough

    case itemCategoryWeapon:
    case itemCategoryShield:
    case itemCategoryArmor:
    case itemCategoryMisc:
        count += category == arg0;
        break;

    case itemCategoryGem:
        count += category == itemCategoryGem;

        if (category == itemCategoryWeapon) {
            weapon = &L.inventory->weapons[arg1->itemIndex];
            for (i = 0; i < L.inventory->grips[weapon->grip - 1].gemSlots; ++i) {
                count += weapon->gems[i] != 0;
            }
        }

        if (category == itemCategoryShield) {
            shield = &L.inventory->shields[arg1->itemIndex];
            for (i = 0; i < shield->base.gemSlots; ++i) {
                count += shield->gems[i] != 0;
            }
        }

        break;
    }
    return count;
}

static int droppable(int arg0) { return arg0 & (vs_main_stateFlags.difficulty + 1); } /* 1026 */

static void apply_list(vs_battle_loot* arg0) /* _applyLootList 1031 */
{
    int i;

    if (droppable(arg0->weapon.difficultyFlags) != 0) {
        add_loot(itemCategoryWeapon, copy_weapon(&arg0->weapon));
    }

    if (droppable(arg0->blade.difficultyFlags) != 0) {
        add_loot(itemCategoryBlade, copy_blade(&arg0->blade.blade, 0));
    }

    if (droppable(arg0->grip.difficultyFlags) != 0) {
        add_loot(itemCategoryGrip, copy_grip(&arg0->grip.grip, 0));
    }

    if (droppable(arg0->shield.difficultyFlags) != 0) {
        add_loot(itemCategoryShield, copy_shield(&arg0->shield));
    }

    for (i = 0; i < 2; ++i) {
        if (droppable(arg0->armor[i].difficultyFlags) != 0) {
            add_loot(itemCategoryArmor, copy_armor(&arg0->armor[i].armor));
        }
    }

    if (droppable(arg0->accessory.difficultyFlags) != 0) {
        add_loot(itemCategoryArmor, copy_armor(&arg0->accessory.accessory));
    }

    if (droppable(arg0->gem.difficultyFlags) != 0) {
        add_loot(itemCategoryGem, copy_gem(&arg0->gem.gem, 0));
    }

    for (i = 0; i < 3; ++i) {
        if ((droppable(arg0->misc[i].difficultyFlags) != 0) && (arg0->misc[i].id != 0)
            && (arg0->misc[i].count != 0)) {
            add_loot(itemCategoryMisc, copy_misc(&arg0->misc[i]));
        }
    }

    update_list();
}

static void apply_all(vs_battle_lootListNode* node) /* _applyAllLootLists 1077 */
{
    vs_battle_rMemzero(L.inventory, sizeof *L.inventory);

    if (node != NULL) {
        while (node != NULL) {
            apply_list(&node->loot);
            node = psx_get(node->next);
        }
    } else {
        apply_list(D_800EB9C8);
    }
}

static void set_key_flag(int arg0) /* _setKeyFlag 1966 */
{
    if (arg0 > 457) {
        vs_battle_setStateFlag(arg0 + 438, 1);
    }
}

/* ---- The end (vs_menuB_exec 1895 / 1907) ---- */

/* result 1 or 2 (_processLootMenu's; state 3 writes unk2 and goes to 5), 0:
 * the empty list's end (state 4 to 5, unk2 kept). The shell hides at once;
 * state 5 (close) runs from this tick on. */
static void finish(int result)
{
    if (result != 0) {
        D_800F4E98.unk2 = result == 1;
    }
    L.closing = 1;
    pc_ui_loot_close();
}

/* State 5: the information box (text box 7) dismissed first, as MENUB waits
 * for vs_battle_dismissTextBox(7) (whatever left it up: the shell never
 * opens it); then the allocation freed, the drops list freed when the loot
 * came from it. The item names MENUB loaded are its own (the shell has its
 * own), so vs_mainMenu_ensureItemTextUnloaded has nothing to wait for.
 * Returns 1 when the session is over. */
static int close_step(void)
{
    if (vs_battle_dismissTextBox(7) != 0) {
        return 0;
    }
    vs_main_freeHeapR(L.inventory);
    if (D_800EB9C4 != 0) {
        func_8008A6FC();
    }
    L.inventory = NULL;
    L.active = 0;
    L.closing = 0;
    return 1;
}

/* updateLoot (1770): the claimed rows out, _updateLootList; with nothing left
 * _discardItems(1) returns 1 at once (state 4) and the session ends. */
static void remove_claimed(void)
{
    int i;
    while (1) {
        for (i = 0; i < L.count; ++i) {
            if (L.list[i].unk2 != 0) {
                break;
            }
        }

        if (i == L.count) {
            break;
        }

        --L.count;

        for (; i < L.count; ++i) {
            L.list[i] = L.list[i + 1];
        }
    }

    update_list();

    if (L.count == 0) {
        finish(1);
    }
}

/* processInput, Square (1548). */
static void take_all(void)
{
    int i;
    int temp_s2 = L.claimed;
    loot_item* loot;

    for (i = 0; i < L.count; ++i) {

        loot = &L.list[i];

        if (vs_mainMenu_copyItem(loot->itemCategory | copyItemFlagsWrite, &vs_main_inventory, loot->itemIndex,
                L.inventory)
            != 0) {
            if (loot->itemCategory == itemCategoryMisc) {

                int miscId = L.inventory->misc[loot->itemIndex].id;

                if (miscId > 457) {
                    set_key_flag(miscId);
                }
            }

            loot->unk2 = 1;
            ++L.claimed;
        }
    }

    if (temp_s2 != L.claimed) {
        vs_main_playSfxDefault(0x7E, 0xD);
        remove_claimed();
        return;
    }

    vs_battle_playInvalidSfx();
}

/* processInput, Circle on a row that fits (1597). */
static void take(loot_item* loot)
{
    int i;

    vs_main_playSfxDefault(0x7E, 0xD);

    if (loot->itemCategory == itemCategoryMisc) {

        i = L.inventory->misc[loot->itemIndex].id;

        if (i > 457) {
            set_key_flag(i);
        }
    }

    vs_mainMenu_copyItem(loot->itemCategory | 0x10, &vs_main_inventory, loot->itemIndex, L.inventory);

    loot->unk2 = 1;
    ++L.claimed;
    remove_claimed();
}

/* _disassembleItem (216), confirmed on Yes: init (the parts' rows, 261),
 * disassemble (369) and finalize (407). */
static void disassemble(int itemIndex)
{
    char lootCount;
    u_char targetItemIndex;
    char disassembleItemCategory;
    int i;

    /* itemIndex != 0 */
    vs_battle_memcpy(L.working, L.list, LIST_ENTRIES * sizeof *L.working);
    lootCount = L.count;
    targetItemIndex = itemIndex - 1;
    disassembleItemCategory = L.list[targetItemIndex].itemCategory;

    i = L.working[targetItemIndex].itemIndex;

    /* init */
    vs_battle_rMemzero(L.list, 0x280);

    L.list->itemCategory = L.working[targetItemIndex].itemCategory;
    L.list->itemIndex = i;
    L.list->state = lootStateNoIndent;

    if (disassembleItemCategory != itemCategoryWeapon) {

        vs_main_inventoryShield* shield = &L.inventory->shields[i];
        L.list[1].itemCategory = itemCategoryShield;
        L.list[1].itemIndex = i;
        L.list[1].state = lootStateIndent1;
        L.count = 2;

        for (i = 0; i < shield->base.gemSlots; ++i) {
            if (shield->gems[i] != 0) {
                L.list[L.count].itemCategory = itemCategoryGem;
                L.list[L.count].itemIndex = shield->gems[i] - 1;
                L.list[L.count].state = lootStateIndent3;
                ++L.count;
            }
        }
    } else {
        vs_main_inventoryWeapon* weapon = &L.inventory->weapons[i];
        L.list[1].itemCategory = itemCategoryBlade;
        L.list[1].itemIndex = weapon->blade - 1;
        L.list[1].state = lootStateIndent2;
        L.list[2].itemCategory = itemCategoryGrip;
        L.list[2].itemIndex = weapon->grip - 1;
        L.list[2].state = lootStateIndent2;
        L.count = 3;

        for (i = 0; i < L.inventory->grips[weapon->grip - 1].gemSlots; ++i) {
            if (weapon->gems[i] != 0) {
                L.list[L.count].itemCategory = itemCategoryGem;
                L.list[L.count].itemIndex = weapon->gems[i] - 1;
                L.list[L.count].state = lootStateIndent3;
                ++L.count;
            }
        }
    }

    /* processCommand, Yes */
    vs_main_playSfxDefault(0x7E, 0x19);

    /* disassemble: i is the record again (the top of the state machine) */
    i = L.working[targetItemIndex].itemIndex;

    vs_battle_rMemcpy(&L.working[lootCount], &L.list[1], (L.count - 1) * sizeof L.list[1]);

    if (disassembleItemCategory != itemCategoryWeapon) {
        pc_inv_release_shield_gems(L.inventory, i);
    } else {
        pc_inv_release_weapon_parts(L.inventory, i);
    }

    lootCount = L.count + (lootCount + 254);

    for (i = targetItemIndex; i < lootCount; ++i) {
        L.working[i] = L.working[i + 1];
    }

    /* finalize */
    L.count = lootCount;

    vs_battle_memcpy(L.list, L.working, LIST_ENTRIES * sizeof *L.working);
    update_list();
}

/* _consolidateMiscItems (433), confirmed on Yes: state 0 (the merged rows,
 * 468), 5 (into the working copies, 611) and 6 (back, 657). */
static void consolidate(int lootIndex)
{
    u_short totalCount;
    u_short amountToAdd;
    u_short itemId;
    u_char lootCount;

    loot_item* loot;
    int i;
    int j;
    vs_main_inventoryMisc* item;

    /* lootIndex != 0 */
    vs_battle_memcpy(L.working, L.list, LIST_ENTRIES * sizeof *L.working);
    vs_battle_memcpy(L.backup, L.inventory, sizeof *L.inventory);

    lootCount = L.count;
    itemId = L.inventory->misc[L.list[lootIndex - 1].itemIndex].id;

    /* state 0 */
    amountToAdd = 0;
    item = vs_main_inventory.misc;

    for (i = 0; i < 64; ++i, ++item) {
        if (item->id == itemId) {
            int v0 = amountToAdd + 100;
            v0 -= item->count;
            amountToAdd = v0;
        }
    }

    totalCount = 0;

    for (i = 0; i < L.count; ++i) {
        if (L.list[i].itemCategory == itemCategoryMisc) {
            item = &L.inventory->misc[L.list[i].itemIndex];
            if (item->id == itemId) {

                totalCount += item->count;

                vs_battle_rMemzero(item, sizeof *item);

                --L.count;

                for (j = i; j < L.count; ++j) {
                    L.list[j] = L.list[j + 1];
                }
                --i;
            }
        }
    }

    L.count = 0;

    while (totalCount != 0) {

        for (i = 0; i < 64; ++i) {
            if (L.inventory->misc[i].id == 0) {
                break;
            }
        }

        if (i == 64) {
            break;
        }

        loot = &L.list[L.count++];
        loot->itemCategory = itemCategoryMisc;
        loot->itemIndex = i;
        loot->unk2 = 0;
        L.inventory->misc[i].id = itemId;
        loot->state = amountToAdd != 0;

        if (loot->state != lootStateNone) {
            if (amountToAdd > 100) {
                L.inventory->misc[i].count = 100;
                totalCount -= 100;
                amountToAdd -= 100;
            } else {
                L.inventory->misc[i].count = amountToAdd;
                totalCount = totalCount - amountToAdd;
                amountToAdd = 0;
            }
        } else if (totalCount > 100) {
            L.inventory->misc[i].count = 100;
            totalCount -= 100;
        } else {
            L.inventory->misc[i].count = totalCount;
            totalCount = 0;
        }
    }

    /* state 4, Yes */
    vs_battle_playMenuSelectSfx();

    /* state 5 */
    for (i = 0; i < lootCount; ++i) {
        if (L.working[i].itemCategory == 6) {

            item = &L.backup->misc[L.working[i].itemIndex];

            if (item->id == itemId) {

                vs_battle_rMemzero(item, 4);

                --lootCount;

                for (j = i; j < lootCount; ++j) {
                    L.working[j] = L.working[j + 1];
                }
                --i;
            }
        }
    }

    for (i = 0; i < L.count; ++i) {
        for (j = 0; j < 64; ++j) {
            if (L.backup->misc[j].id == 0) {
                break;
            }
        }

        if (j == 0x40) {
            break;
        }

        loot = &L.working[lootCount++];
        loot->itemCategory = itemCategoryMisc;
        loot->itemIndex = j;
        loot->unk2 = 0;
        loot->state = L.list[i].state;
        item = &L.backup->misc[j];
        item->id = itemId;
        item->count = L.inventory->misc[L.list[i].itemIndex].count;
    }

    /* state 6 */
    L.count = lootCount;

    vs_battle_memcpy(L.list, L.working, sizeof *L.list * LIST_ENTRIES);
    vs_battle_memcpy(L.inventory, L.backup, sizeof *L.inventory);

    /* waitForTopMenu (1762) */
    update_list();
}

/* _topLevelMenuTransition (53): the Organize inventory panel opens (state 1)
 * or closes (state 2), category (0-based) its first category. */
static void bag_open(int category)
{
    if (L.bag) {
        /* another category (MENUB's Items menu, L1 / R1: the change sound,
         * the current category) */
        vs_battle_playMenuChangeSfx();
        D_800F4EE8.unk3A.currentItemCategory = (category + 1) & 7;
        return;
    }
    D_800F4EE8.unk3A.currentItemCategory = category + 1;
    func_8008A4DC(0);
    L.hud_saved = pc_ui_menu_hud_game(); /* D_800EB9AF (the shell's slide-out aside) */
    if (!L.hud_saved) {
        func_800CB654(1);
        D_800EB9B0 = 0x200000;
    }
    /* _itemNavigation(1): the memories start over without cursor memory */
    if (vs_main_settings.cursorMemory == 0) {
        vs_main_bzero(&D_800F4EE8.unk3A, sizeof D_800F4EE8.unk3A);
    }
    L.bag = 1;
}

static void bag_close(void)
{
    if (!L.bag) {
        return;
    }
    func_8008A4DC(1);
    if (L.hud_saved == 0) {
        func_800CB654(0);
        D_800EB9B0 = 0;
    }
    pc_ui_menu_hud_keep(); /* the HUD stays out under the shell (menu_bridge.c) */
    L.bag = 0;
}

/* ---- The snapshot ---- */

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

/* A MENUB line as UTF-8, its string insert (0xFF n: vs_battle_stringContext
 * strings[n]) replaced by `name`. */
static void menu_line(char* out, int size, int offset, const char* name)
{
    const unsigned char* text = MENU_TEXT(offset);
    int k, n;
    for (k = 0; k < 512 && text[k] != 0xE7 && text[k] != 0xFF; ++k) {
        if (text[k] >= 0xEC && text[k] != 0xFC)
            ++k; /* an operand */
    }
    n = pc_ui_text_decode(text, k, out, size);
    if (k < 512 && text[k] == 0xFF) {
        char rest[256];
        if (n > 0 && n + 1 < size)
            out[n++] = ' ';
        copy_text(out + n, size - n, name ? name : "");
        while (n < size && out[n])
            ++n;
        pc_ui_text_decode(text + k + 2, 512, rest, sizeof rest);
        if (rest[0] && rest[0] != '.' && rest[0] != '?' && rest[0] != '!' && rest[0] != ',' && n + 1 < size)
            out[n++] = ' ';
        copy_text(out + n, size - n, rest);
    }
}

static void weapon_name(char* out, int size, vs_main_inventoryWeapon* w)
{
    pc_ui_text_decode((const unsigned char*)w->name, sizeof w->name, out, size);
}

/* The row's name, kind and description from the loot's own record. */
static void row_text(pc_ui_loot_row* e, int c, int index)
{
    vs_main_inventory_t* inv = L.inventory;
    const char* kind = "";
    switch (c) {
    case itemCategoryWeapon: {
        vs_main_inventoryWeapon* w = &inv->weapons[index];
        vs_main_inventoryBlade* b = &inv->blades[w->blade - 1];
        weapon_name(e->name, sizeof e->name, w);
        e->id = b->id;
        e->material = b->material;
        kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_dagger + (u_char)b->category - 1);
        break;
    }
    case itemCategoryBlade: {
        vs_main_inventoryBlade* b = &inv->blades[index];
        e->id = b->id;
        e->material = b->material;
        kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_dagger + (u_char)b->category - 1);
        copy_text(e->help, sizeof e->help, pc_ui_item_help(b->id - 1 + VS_ITEMHELP_BIN_INDEX_battleKnifeDesc));
        break;
    }
    case itemCategoryGrip: {
        vs_main_inventoryGrip* g = &inv->grips[index];
        e->id = g->id;
        kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_shortGrip + g->category - 1);
        copy_text(e->help, sizeof e->help, pc_ui_item_help(g->id - 6));
        break;
    }
    case itemCategoryShield: {
        vs_main_inventoryShield* sh = &inv->shields[index];
        e->id = sh->base.id;
        e->material = sh->base.material;
        kind = "Shield";
        break;
    }
    case itemCategoryArmor: {
        vs_main_inventoryArmor* a = &inv->armor[index];
        e->id = a->id;
        if (a->category == 7) {
            kind = "Accessory";
            copy_text(e->help, sizeof e->help, pc_ui_item_help(a->id - 223 + VS_ITEMHELP_BIN_INDEX_roodNecklace));
        } else {
            kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_armorCategory + a->category - 1);
            e->material = a->material;
        }
        break;
    }
    case itemCategoryGem: {
        vs_main_inventoryGem* g = &inv->gems[index];
        e->id = g->id;
        copy_text(e->help, sizeof e->help, pc_ui_item_help(g->id - 261 + VS_ITEMHELP_BIN_INDEX_gemFeldspar));
        break;
    }
    default: {
        vs_main_inventoryMisc* m = &inv->misc[index];
        e->id = m->id;
        e->count = m->count;
        e->key = m->id > LAST_USABLE;
        copy_text(e->help, sizeof e->help, pc_ui_item_help(m->id - 323 + VS_ITEMHELP_BIN_INDEX_cureRootDesc));
        break;
    }
    }
    if (c != itemCategoryWeapon)
        copy_text(e->name, sizeof e->name, pc_ui_item_name(e->id));
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

/* A record of the loot's inventory that a row may name (a copy that failed
 * leaves record 255: MENUB lists it anyway, the shell shows nothing for it). */
static int record_ok(int c, int index)
{
    return c >= 0 && c < PC_UI_ITEM_CATEGORIES && index >= 0 && index < capacities[c];
}

static int equipped_index(int kind, int armor_category, int* slot)
{
    int i;
    *slot = -1;
    if (kind == itemCategoryWeapon) {
        for (i = 0; i < 8; ++i)
            if (vs_main_inventory.weapons[i].isEquipped && vs_main_inventory.weapons[i].blade) {
                *slot = PC_UI_SLOT_WEAPON;
                return i;
            }
    } else if (kind == itemCategoryShield) {
        for (i = 0; i < 8; ++i)
            if (vs_main_inventory.shields[i].isEquipped && vs_main_inventory.shields[i].base.id) {
                *slot = PC_UI_SLOT_SHIELD;
                return i;
            }
    } else if (kind == itemCategoryArmor) {
        /* the worn record of the same kind, in the bag's list order (arms: the first of the two) */
        for (i = 0; i < 16; ++i) {
            int k = vs_main_inventoryIndices.armor[i];
            vs_main_inventoryArmor* a;
            if (k == 0 || k > 16)
                continue;
            a = &vs_main_inventory.armor[k - 1];
            if (a->id && a->limb && a->category == armor_category) {
                *slot = a->limb == 7 ? PC_UI_SLOT_ACCESSORY
                    : a->limb >= 1 && a->limb <= 5 ? PC_UI_SLOT_RARM + a->limb - 1 : -1;
                return *slot >= 0 ? k - 1 : -1;
            }
        }
    }
    return -1;
}

/* The focused row's card: described as the Items screen describes a bag
 * record, with the loot's inventory in vs_main_inventory's place for the
 * moment (as MENUB's _populateLootItem does to describe its rows). */
static void details(void)
{
    int focus = pc_ui_loot_focus(), c, index, eq, armor_category = 0;
    vs_main_bzero(&snap.details, sizeof snap.details);
    vs_main_bzero(&snap.equipped, sizeof snap.equipped);
    snap.equipped.inventory = -1;
    snap.equipped_slot = -1;
    if (focus < 0 || focus >= L.count)
        return;
    c = L.list[focus].itemCategory;
    index = L.list[focus].itemIndex;
    if (!record_ok(c, index) || c == itemCategoryMisc)
        return;
    if (c == itemCategoryArmor)
        armor_category = L.inventory->armor[index].category;
    vs_battle_memcpy(&swap_buf, &vs_main_inventory, sizeof swap_buf);
    vs_battle_memcpy(&vs_main_inventory, L.inventory, sizeof vs_main_inventory);
    pc_ui_items_describe(c, index, &snap.details);
    vs_battle_memcpy(&vs_main_inventory, &swap_buf, sizeof vs_main_inventory);
    eq = equipped_index(c, armor_category, &snap.equipped_slot);
    if (eq >= 0)
        pc_ui_equip_describe(c == itemCategoryWeapon ? 0 : c == itemCategoryShield ? 1 : 2, eq, &snap.equipped);
    else
        snap.equipped_slot = -1;
}

/* The texts that do not depend on the list (the game's lines), once per
 * session (pc_ui_loot_begin). */
static void fill_texts(void)
{
    menu_line(snap.empty, sizeof snap.empty, VS_menuText_OFFSET_emptyChest, NULL);
    menu_line(snap.leave_untouched, sizeof snap.leave_untouched, VS_menuText_OFFSET_discardAllConfirm, NULL);
    menu_line(snap.leave_rest, sizeof snap.leave_rest, VS_menuText_OFFSET_itemsRemaining, NULL);
    menu_line(snap.org_label[PC_UI_LOOT_ORG_NONE], sizeof snap.org_label[0], VS_menuText_OFFSET_organizeInventory,
        NULL);
    menu_line(snap.org_label[PC_UI_LOOT_ORG_DISASSEMBLE], sizeof snap.org_label[0], VS_menuText_OFFSET_disassemble,
        NULL);
    /* the original's row says ORGANIZE, which reads as Organize inventory's twin: named for what it does */
    copy_text(snap.org_label[PC_UI_LOOT_ORG_CONSOLIDATE], sizeof snap.org_label[0], "Consolidate");
    menu_line(snap.org_info[PC_UI_LOOT_ORG_NONE], sizeof snap.org_info[0], VS_menuText_OFFSET_organizeInventoryInfo,
        NULL);
    menu_line(snap.org_info[PC_UI_LOOT_ORG_DISASSEMBLE], sizeof snap.org_info[0],
        VS_menuText_OFFSET_disassembleInfo, NULL);
    menu_line(snap.org_info[PC_UI_LOOT_ORG_CONSOLIDATE], sizeof snap.org_info[0], VS_menuText_OFFSET_organizeInfo,
        NULL);
}

/* The rows and the bag's counts (L.dirty: after a change). */
static void fill_rows(void)
{
    int i, c;
    const char* key_name = "";
    snap.valid = 1;
    snap.chest = D_800EB9C4 == NULL && D_800EB9C8 != NULL;
    snap.count = L.count;
    snap.claimed = L.claimed;
    snap.keys = 0;
    for (c = 0; c < PC_UI_ITEM_CATEGORIES; ++c) {
        snap.bag_capacity[c] = capacities[c];
        snap.bag_count[c] = 0;
        for (i = 0; i < capacities[c]; ++i)
            snap.bag_count[c] += pc_ui_item_record_id(c, i) != 0;
    }
    for (i = 0; i < L.count && i < PC_UI_LOOT_ROWS; ++i) {
        pc_ui_loot_row* e = &snap.rows[i];
        loot_item* loot = &L.list[i];
        int k;
        vs_main_bzero(e, sizeof *e);
        e->category = loot->itemCategory;
        e->record = loot->itemIndex;
        e->fits = loot->state != 0;
        if (record_ok(e->category, e->record))
            row_text(e, e->category, e->record);
        for (k = 0; k < PC_UI_ITEM_CATEGORIES; ++k)
            e->needs[k] = record_ok(e->category, e->record) ? includes(k, loot) : 0;
        /* _organizeInventory's availableAction (1408) */
        if (e->category == itemCategoryWeapon || e->category == itemCategoryShield)
            e->organize = PC_UI_LOOT_ORG_DISASSEMBLE;
        else if (e->category == itemCategoryMisc && record_ok(e->category, e->record)
                 && L.inventory->misc[e->record].id < 458)
            e->organize = PC_UI_LOOT_ORG_CONSOLIDATE;
    }
    /* _discardItems(1)'s key check (1291): the last key or sigil's name */
    for (i = 0; i < L.count; ++i)
        if (L.list[i].itemCategory == itemCategoryMisc && record_ok(itemCategoryMisc, L.list[i].itemIndex)
            && L.inventory->misc[L.list[i].itemIndex].id > 457) {
            snap.keys = 1;
            key_name = pc_ui_item_name(L.inventory->misc[L.list[i].itemIndex].id);
        }
    menu_line(snap.keys_text, sizeof snap.keys_text, VS_menuText_OFFSET_invalidDiscard, key_name);
}

/* The focused row's questions and card (L.focus: the focus they are for). */
static void fill_focus(int focus)
{
    snap.disassemble_ask[0] = snap.consolidate_ask[0] = 0;
    if (focus >= 0 && focus < L.count && focus < PC_UI_LOOT_ROWS) {
        /* disassembleConfirm names a weapon by its own name, a shield by its item name (336) */
        menu_line(snap.disassemble_ask, sizeof snap.disassemble_ask, VS_menuText_OFFSET_disassembleConfirm,
            snap.rows[focus].name);
        menu_line(snap.consolidate_ask, sizeof snap.consolidate_ask, VS_menuText_OFFSET_organizeConfirm,
            snap.rows[focus].name);
    }
    details();
}

/* The snapshot for the shell: refilled only where something changed (a
 * request: the rows; a moved focus: its texts and card, whose details swap
 * the loot's inventory in), handed over only then. */
static void fill(void)
{
    int focus = pc_ui_loot_focus(), changed = 0;
    if (L.dirty) {
        fill_rows();
        changed = 1;
    }
    if (changed || focus != L.focus) {
        fill_focus(focus);
        L.focus = focus;
        changed = 1;
    }
    if (changed)
        pc_ui_loot_set(&snap);
    if (L.bag && L.dirty)
        pc_ui_loot_set_bag(pc_ui_items_bag(1));
    L.dirty = 0;
}

/* ---- Requests ---- */

/* The row a request names, or null (stale: another record now). */
static loot_item* named_row(const pc_ui_request* r)
{
    loot_item* loot;
    if (r->item < 0 || r->item >= L.count)
        return NULL;
    loot = &L.list[r->item];
    if (r->shield >= 0 && (loot->itemIndex != r->shield || loot->itemCategory != r->slot))
        return NULL;
    return loot;
}

static void request(const pc_ui_request* r)
{
    loot_item* loot;
    switch (r->kind) {
    case PC_UI_REQ_LOOT_SOUND:
        if (r->slot == 0)
            vs_battle_playMenuLeaveSfx();
        else if (r->slot == 1)
            vs_battle_playMenuSelectSfx();
        else
            vs_battle_playInvalidSfx();
        return;
    case PC_UI_REQ_LOOT_TAKE:
        loot = named_row(r);
        if (!loot || L.bag || loot->state == 0)
            break;
        take(loot);
        return;
    case PC_UI_REQ_LOOT_TAKE_ALL:
        if (L.bag)
            break;
        take_all();
        return;
    case PC_UI_REQ_LOOT_DISASSEMBLE:
        loot = named_row(r);
        if (!loot || L.bag || loot->state != 0
            || (loot->itemCategory != itemCategoryWeapon && loot->itemCategory != itemCategoryShield)
            || !record_ok(loot->itemCategory, loot->itemIndex))
            break;
        disassemble(r->item + 1);
        return;
    case PC_UI_REQ_LOOT_CONSOLIDATE:
        loot = named_row(r);
        if (!loot || L.bag || loot->state != 0 || loot->itemCategory != itemCategoryMisc
            || !record_ok(itemCategoryMisc, loot->itemIndex) || L.inventory->misc[loot->itemIndex].id >= 458)
            break;
        consolidate(r->item + 1);
        return;
    case PC_UI_REQ_LOOT_BAG:
        if (r->slot) {
            if (r->item < 0 || r->item >= PC_UI_ITEM_CATEGORIES)
                break;
            bag_open(r->item);
        } else {
            bag_close();
            update_list(); /* waitForTopMenu */
        }
        return;
    case PC_UI_REQ_LOOT_LEAVE:
        if (L.bag)
            break;
        if (r->count == 0) {
            if (L.empty) { /* state 4: any button, no sound, unk2 kept */
                finish(0);
                return;
            }
            vs_battle_playMenuLeaveSfx(); /* processInput's Cross */
            /* _discardItems(1): nothing left (state 4), a chest untouched (5) */
            if (L.count == 0)
                finish(1);
            else if (L.claimed == 0 && D_800EB9C8 != NULL)
                finish(2);
            return;
        }
        /* the Yes / No on Yes (state 3); never with a key or sigil in the list (state 6) */
        {
            int i;
            for (i = 0; i < L.count; ++i)
                if (L.list[i].itemCategory == itemCategoryMisc && L.inventory->misc[L.list[i].itemIndex].id > 457)
                    break;
            if (i < L.count || L.count == 0)
                break;
        }
        vs_main_playSfxDefault(0x7E, 0x1C);
        finish(L.claimed == 0 ? 2 : 1);
        return;
    case PC_UI_REQ_ITEM_DISCARD:
    case PC_UI_REQ_ITEM_DISCARD_ALL:
    case PC_UI_REQ_ITEM_CANCEL:
    case PC_UI_REQ_ITEM_SORT:
    case PC_UI_REQ_ITEM_MOVE:
        /* the Items screen's own ports (items_bridge.c), a Move's merge
         * MENU3's: no stack lost (MENUB's loses the moved one; see above) */
        if (!L.bag)
            break;
        pc_ui_items_request(r);
        return;
    default:
        break;
    }
    vs_battle_playInvalidSfx();
}

/* ---- The session ---- */

/* vs_menuB_exec state 0 (1825). Returns 0 when the game's heap has no room
 * for the loot's inventory (the original menu then runs: it would fail the
 * same way, as the original does). */
int pc_ui_loot_begin(void)
{
    int i;
    void* buf = vs_main_allocHeapR(sizeof *L.inventory + sizeof *L.backup + sizeof *L.list * LIST_ENTRIES * 2);
    if (buf == NULL)
        return 0;

    vs_battle_playSfx10();
    vs_mainMenu_populateBaseStats(16);

    for (i = 0; i < 7; ++i) {
        vs_mainMenu_rebuildInventory(i);
    }

    vs_main_bzero(&L, sizeof L);
    vs_main_bzero(&snap, sizeof snap);
    L.inventory = buf;
    L.backup = (vs_main_inventory_t*)((char*)buf + sizeof *L.inventory);
    L.list = (loot_item*)((char*)buf + sizeof *L.inventory + sizeof *L.backup);
    L.working = (loot_item*)((char*)buf + sizeof *L.inventory + sizeof *L.backup + sizeof *L.list * LIST_ENTRIES);
    L.claimed = 0;
    L.count = 0;
    L.active = 1;

    apply_all(D_800EB9C4);
    L.empty = L.count == 0;

    pc_ui_loot_open();
    fill_texts();
    L.dirty = 1;
    L.focus = -2;
    fill();
    fill(); /* the focus the first snapshot gave the shell */
    return 1;
}

/* One tick: the shell's tick (on the snapshot the last tick left), its
 * requests, the snapshot refilled where they or the focus changed it.
 * Returns 1 when the session ended (vs_menuB_exec's 1). */
int pc_ui_loot_frame(const pc_ui_input* in)
{
    pc_ui_request r;
    if (!L.active)
        return 1;
    if (L.closing)
        return close_step();
    pc_ui_loot_tick(in);
    while (pc_ui_loot_pop_request(&r)) {
        if (!L.closing) {
            request(&r);
            L.dirty = 1;
        }
    }
    if (L.closing)
        return close_step();
    fill();
    return 0;
}

/* The session ended without its end (reset 1: a soft reset; 0: the game left
 * the menu): the shell hides; the Organize panel's freeze is undone; the
 * allocation goes back to the game heap unless the heap is the next game's
 * (a reset starts it over). The drops list is the game's (left as it is). */
void pc_ui_loot_abandon(int reset)
{
    if (L.active) {
        bag_close();
        if (!reset && L.inventory)
            vs_main_freeHeapR(L.inventory);
    }
    L.active = L.closing = 0;
    L.inventory = NULL;
    pc_ui_loot_close();
}

/* RPC loot (tests, dev_rpc.c): the loot menu opened as the game opens it.
 * nodes loot lists of sizeof (vs_battle_loot) bytes each: chest 0 links them
 * to the drops list (_lootListHead, as 146C.c builds it after a battle: each
 * node from the game's heap, actorId 0xFF) and calls func_800CB158; chest 1
 * opens the first as a chest's (func_800CB18C on a copy kept here). Then the
 * field's menu mode, as the field's Triangle opens the menu (146C.c:7614
 * func_8007357C: the field ticks the menu, func_800C97BC, and ends it when
 * it closes, func_800735F8; the after-battle and chest flows tick it in
 * their own modes, which need a battle's or a chest's state: see RPC chest
 * for the real chest flow). Only in the field with Ashley free to act, as
 * the field's Triangle checks it (field mode 1, no screen transition, no
 * 0x4000 status, unk954 0) and while no menu is open (func_800CACD0 refuses
 * otherwise). A node the heap has no room for gives back the ones allocated
 * before it (the drops list as it was). Returns 1 when opened. */
void func_8007357C(int arg0, int arg1); /* BATTLE 146C.c */
extern u_int D_800F196C;                 /* the field's mode: 1 free to act, 9 a chest */
static vs_battle_loot debug_chest;
static int field_free(void)
{
    return D_800F196C == 1 && vs_battle_screenTransitionStep != 1 && vs_battle_screenTransitionStep != 2
        && vs_battle_screenTransitionStep != 4 && vs_battle_characterState != NULL
        && !(func_800A0BE0(0) & 0x4000) && psx_get(vs_battle_characterState->unk3C)->unk954 == 0
        && vs_battle_menuState.currentMenu == 0x3F;
}
int pc_ui_loot_debug_open(const unsigned char* data, int nodes, int chest)
{
    int i;
    vs_battle_lootListNode* head = _lootListHead;
    if (nodes < 1 || !field_free())
        return 0;
    if (chest) {
        vs_battle_memcpy(&debug_chest, data, sizeof debug_chest);
        func_800CB18C(&debug_chest);
        if (vs_battle_menuState.currentMenu != 0xC)
            return 0;
        func_8007357C(0, 0);
        return 1;
    }
    for (i = nodes - 1; i >= 0; --i) {
        vs_battle_lootListNode* node = vs_main_allocHeapR(sizeof *node);
        if (node == NULL) {
            while (_lootListHead != head) {
                node = _lootListHead;
                _lootListHead = psx_get(node->next);
                vs_main_freeHeapR(node);
            }
            return 0;
        }
        vs_main_bzero(node, sizeof *node);
        vs_battle_memcpy(&node->loot, data + i * sizeof node->loot, sizeof node->loot);
        node->actorId = 0xFF;
        psx_set(node->next, _lootListHead);
        _lootListHead = node;
    }
    func_800CB158(_lootListHead);
    if (vs_battle_menuState.currentMenu != 0xC)
        return 0;
    func_8007357C(0, 0);
    return 1;
}

/* RPC chest (tests): the current room's chests (MPD section 13: position,
 * the opened flag's index unk10, its state flag 0x3C0 + unk10, the loot) as
 * JSON members, and a chest opened through the field's own flow: what the
 * field's Cross does at a chest (146C.c:7648, func_80074050 on the record
 * func_80074950 finds in front of Ashley), so field mode 9 runs as in play:
 * Ashley faces it, opens it (func_800743E0: func_800CB18C on its loot, the
 * menu), and func_800744B8 ticks the menu and, when it closes, sets the
 * chest's flag when something was taken (D_800F4E98.unk2) or closes the lid
 * (func_8008D5FC). Only in the field with Ashley free to act and the chest
 * not opened before. */
void func_80074050(_mpdRoomSectionA* arg0, int arg1); /* BATTLE 146C.c */
static _mpdRoomSection13* room_chest(int index, int* count)
{
    _mpdRoomSection13* chests = psx_get(vs_battle_roomData.section13);
    *count = chests != NULL ? (int)(vs_battle_roomData.header.section13Len / sizeof *chests) : 0;
    return index >= 0 && index < *count ? &chests[index] : NULL;
}
/* Chest `index` of the room: out[] = x, z, floor, the flag's index, opened
 * (its state flag), key, weapon blade id, shield id, armor id, gem id, misc
 * items; returns the room's chest count (out untouched past it). out[11]
 * is the field's free-to-act state. */
int pc_ui_loot_debug_chest(int index, int out[12])
{
    int n, k;
    _mpdRoomSection13* c = room_chest(index, &n);
    out[11] = field_free();
    if (c == NULL)
        return n;
    out[0] = c->unk0;
    out[1] = c->unk2;
    out[2] = c->unk4;
    out[3] = c->unk10;
    out[4] = vs_battle_getStateFlag(c->unk10 + 0x3C0);
    out[5] = c->unkC;
    out[6] = c->unk14.weapon.blade.id;
    out[7] = c->unk14.shield.shield.id;
    out[8] = c->unk14.armor[0].armor.id;
    out[9] = c->unk14.gem.gem.id;
    out[10] = 0;
    for (k = 0; k < 3; ++k)
        out[10] += c->unk14.misc[k].id != 0;
    return n;
}
int pc_ui_loot_debug_chest_open(int index)
{
    int n;
    _mpdRoomSection13* c = room_chest(index, &n);
    /* a container (flag >= 64: func_800743E0 calls func_800CB1C0(flag - 64), MENU7's
     * container) is never "opened": its flag's state is not checked */
    if (c == NULL || !field_free() || (c->unk10 < 0x40 && vs_battle_getStateFlag(c->unk10 + 0x3C0)))
        return 0;
    func_80074050((_mpdRoomSectionA*)c, 0);
    return 1;
}

/* RPC container (tests, the fallback where a room has no container object): the save
 * room's container opened as the field opens it (BATTLE func_800CB1C0(location):
 * func_800CACD0(7, 2), D_800F4E6B = location + 0x40, D_800F4EA0 |= 0x400), then the
 * field's menu mode as for RPC loot (func_8007357C). location < 0: the room's save
 * point (func_8008A4FC()). Only in the field with Ashley free to act. */
int pc_ui_container_debug_open(int location)
{
    if (!field_free())
        return 0;
    if (location < 0)
        location = func_8008A4FC();
    func_800CB1C0(location);
    if ((vs_battle_menuState.currentMenu & 0x3F) != 7)
        return 0;
    func_8007357C(0, 0);
    return 1;
}
