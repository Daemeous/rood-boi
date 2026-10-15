/* Modern menus, the Workshop (the Items tab's Workshop sub-tab): the game
 * side (battle builds). menu_bridge.c calls pc_ui_workshop_fill at the start
 * of every tick of the shell's top level (after items_bridge.c's fill, which
 * rebuilds every list while the Items tab shows) and pc_ui_workshop_refresh
 * after the shell's tick; equip_bridge.c's request loop hands the Workshop
 * requests to pc_ui_workshop_request.
 *
 * Everything here is the original's Setup screen, MENUC (src/MENU/MENUC.PRG/
 * 168.c), which MENU3's Items menu opens (16C.c state 7: returnState =
 * currentMenu, currentMenu 13, D_800F4EA0 |= 0x200), and MENU8's rename
 * (src/MENU/MENU8.PRG/21A0.c), ported line for line on the resident BATTLE
 * and main data with the originals' types and expressions, so the bag ends
 * byte for byte as the original screens leave it (ui_workshop_smoke checks
 * it). The flows' own state (what is picked, the name typed) is the shell's
 * (ui_workshop.cpp); a request carries the whole choice and is carried out
 * at once, as the original's state ends after its confirm:
 *
 *   ASSEMBLE    _assembleMenu (1057): the record vs_mainMenu_getFirstEmptyItemSlot
 *               (2D10.c:1334) gives, _assembleWeapon (309; twice: the
 *               original runs it on every frame of its slot menu once blade
 *               and grip are chosen, the second run zeroes the record through
 *               _disassembleWeapon as the original's later frames do), then
 *               MENU8's name (the rename hand-off, stringBuf[0] 0: the field
 *               starts blank): _copyNormalizedString (21A0.c:89), blank: the
 *               blade's ITEMNAME record (21A0.c:806); MENU8's Yes sound
 *   GEMS        _attachGemsMenu (1541) as it ends on its confirm: the gems
 *               chosen per slot (the live edits of its state 6 end the same
 *               as _attachGems (1498) with the final set: every gem of the
 *               slots out, the chosen ones in, setItemIndex = record + 1 (+
 *               0x80 a shield)); an equipped weapon / shield re-equipped
 *               (vs_battle_equipWeapon / Shield); sfx 0x7E, 0x18. The
 *               re-equip matters beyond the record: it ends the actor's
 *               enchantments (func_8006B214 / B270 / B2D4 clear their
 *               statuses), so it happens exactly when the original's does:
 *               the original attaches each pick at once and re-equips then
 *               (state 6, 168.c:1820-1860), its Yes changes nothing more
 *               (1912), and every way out of the slots without Yes (state 5's
 *               Cross and Triangle, the pick list's Triangle, the Yes / No's
 *               Cancel: 1855, 1873, 1928) is _attachGems(targetGems): the
 *               gems as they were, re-equipped. The shell attaches nothing
 *               before Attach, so: a visit with a pick (PC_UI_WS_EDITED, even
 *               one that ends on the same gems) attaches and re-equips; one
 *               without a pick changes nothing, not even the actor (the sound
 *               and memories as Yes); leaving the slots any other way sends
 *               GEMS_LEAVE: the record's gems put back as they are and an
 *               equipped target re-equipped, silently (its leave sound is
 *               the shell's back step's), never refused (a gone or changed
 *               target, a locked or busy bag: nothing). Closing the shell
 *               from the game's side (the bridge's abandon) sends nothing.
 *   DISASSEMBLE _disassembleMenu (1963) confirmed: _disassembleWeapon (281) /
 *               _disassembleShield (344); equipped: vs_mainMenu_unequipWeapon
 *               / Shield after it (2121) and the actor's models reloaded
 *               (equip_bridge.c's busy hold, where MENUC runs _loadObject);
 *               sfx 0x7E, 0x19
 *   RENAME      _renameWeaponMenu (2226) then MENU8 (vs_menu8_execRename,
 *               21A0.c:820): the field as MENU8 opens it on the name
 *               (stringBuf[0] 1: spaces kept, a spacing code skipped), the
 *               name typed through _copyNormalizedString, blank: the blade's
 *               name; equipped: re-equipped; sfx 0x7E, 0xD (MENU8's Yes)
 *   REPAIR      _repairMenu (2355) state 2: every listed blade, shield and
 *               armor but accessories: currentPp -= (maxDp - currentDp) / 100
 *               (0 at least), currentDp = maxDp; the equipped re-equipped;
 *               sfx 0x7E, 0xD
 *   COMBINE     _combineBladeMenu (2750) / _combineShieldMenu (3346) /
 *               _combineArmorMenu (3903) confirmed: the result of
 *               _combineBlades / _combineShields / _combineArmor (4657, 4727,
 *               4768: BLADE.SYD / SHIELD.SYD / ARMOR.SYD read from the disc
 *               through item_names.c, never written anywhere; the tables'
 *               index formulas, vs_main_materialModifiers, _setClassAffinities
 *               4497, _setTypeValues 4595) computed before the commit (3068,
 *               3663, 4151): the weapon a blade is part of disassembled (the
 *               equipped one unequipped first, models reloaded), an equipped
 *               shield unequipped (models), worn armor taken off
 *               (vs_battle_equipArmor(limb - 1, NULL)), both sources
 *               vs_mainMenu_initItem'd, the result copied into the first one's
 *               record (its index kept), the category's list rebuilt; sfx
 *               0x7E, 0x18
 * After each change every list is rebuilt (MENUC goes back to its state 1,
 * vs_mainMenu_rebuildInventory 0..6). The workshop (vs_battle_inWorkshop,
 * 146C.c:16710) and its materials (_workshopMaterials, 1055) grey what MENUC
 * greys: outside a workshop Repair, Combine and Assemble (notInWorkshop:
 * its loop from row 3 stops when it wraps to row 0, so Gems, Disassemble
 * and Rename stay usable anywhere, as in the game),
 * Assemble with eight weapons (weaponsFull), Rename with none (noWeapons);
 * the shell greys the lists' rows from the snapshot as MENUC does (no gem
 * slots, a shield without gems, a blade / grip that does not fit the other
 * (_validGripFlags), a material the workshop does not work, a combine
 * category with fewer than two items: noItems / oneItem).
 *
 * Cursor memories (D_800F4EE8.cursorMemories, ids 53..61 as MENUC's lists
 * set them up: vs_mainmenu_setMenuRows' rowInfo & 0xFF): each request stores
 * what the original's lists store when the same choice is confirmed there
 * and the player leaves from where the original comes back to (its lists
 * reopen with the memory forced on, after MENU8 too: MENUC resumes at its
 * state 10, not 0): the command list (53: the command's row), Gems' and
 * Disassemble's
 * Weapons / Shields (54, 57) and their lists (55 + kind, 58 + kind: the
 * row), Rename's list (60), Combine's categories (61). Every list fits its
 * screen (at most eight rows), so the memory is {row, 0} whatever path the
 * cursor took. Exceptions: backing out, a refused choice and the pick lists
 * (MENUC's own _populateItemsList keeps no memory) store nothing; MENU3's
 * own memories on the way to Setup (its options page) are not the shell's.
 * Divergence (documented, not modelled): the original's lists also store
 * their memory when backed out of (MAINMENU 58EC.c:1012, 1046: Triangle and
 * Cross keep the row the cursor is on), so after a back-out the original
 * reopens a list where it was left; the shell's flows keep their own
 * cursors and write MENUC's memories only for what they carry out.
 *
 * Requests carry the whole choice, a name included (pc_ui_request name: the
 * 20-code field as typed, so two requests in a tick cannot mix names up).
 * Refused (the invalid sound, nothing changed; the shell goes back to the
 * step that sent it, pc_ui_workshop_refused): the Items menu's mask
 * (D_800F4EA0 & 7) set, MAINMENU not resident, a model reload under way, a
 * record that is not what the screen saw or no longer fits the choice (the
 * same checks MENUC's lists make), or, for a change that unequips the
 * weapon / shield, no model load possible now
 * (pc_weapon_slots_menu_can_load). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "build/assets/MENU/ITEMHELP.BIN.h"
#include "build/assets/MENU/MENU12.BIN.h"
#include "build/assets/MENU/MENU8.PRG/rename.h"
#include "vs_string.h"
#include "inventory_ops.h"
#include "item_names.h"
#include "ui_shell.h"
#include "ui_workshop.h"
#include <stdlib.h>
#include <string.h>

/* ui_text.h (stdio in a game-header unit is avoided) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);

void pc_ui_workshop_fill(int mainmenu);
void pc_ui_workshop_refresh(void);
int pc_ui_workshop_request(const pc_ui_request* r);
/* items_bridge.c / equip_bridge.c */
void pc_ui_items_rebuild(int category);
void pc_ui_items_describe(int c, int index, pc_ui_item_details* d);
int pc_ui_items_cast_taken(void);
int pc_ui_abilities_cast_taken(void);
int pc_ui_magick_cast_taken(void);
int pc_ui_equip_busy(void);
void pc_ui_equip_unequip_weapon(void);
void pc_ui_equip_unequip_shield(void);
void pc_ui_equip_models_mark(void);
void pc_ui_equip_models_reload(void);
int pc_ui_item_record_id(int category, int i);
/* weapon_slots.c */
int pc_weapon_slots_menu_can_load(void);
int pc_weapon_slots_get(int weapon_index);
void pc_weapon_slots_clear(int slot);

/* MENUC's text (MENU12.BIN) and MENU8's (its rename grid), under our own names */
static u_short menu12_text[] = {
#include "build/assets/MENU/MENU12.BIN.vsString"
};
static u_short rename_text[] = {
#include "build/assets/MENU/MENU8.PRG/rename.vsString"
};

#define SETUP_MEMORY 53        /* vs_menuC_exec's command list (0x135) */
#define GEMS_MEMORY 54         /* _attachGemsTopMenu (566 = 0x236), its lists 55 + isShield */
#define DISASSEMBLE_MEMORY 57  /* _disassembleTopMenu (0x239), its lists 58 + isShield */
#define RENAME_MEMORY 60       /* _renameWeaponMenu (0x1913C) */
#define COMBINE_MEMORY 61      /* _combineTopMenu (0x23D) */
#define ITEM_MASK 7            /* D_800F4EA0 bits that grey the Items menu (and so Setup) */

static pc_ui_workshop snap;
static int resident;
static int filled;   /* the snapshot was filled on this tick */
static int shown;    /* the sub-tab showed on the last fill */

static vs_battle_actor2* ashley(void)
{
    return vs_battle_characterState ? psx_get(vs_battle_characterState->unk3C) : NULL;
}

/* UTF-8 text into out, cut at a code point boundary (as items_bridge.c). */
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

static void remember(int id, int sel, int page)
{
    char* memories = (char*)&D_800F4EE8;
    memories[id * 2] = sel;
    memories[id * 2 + 1] = page;
}

/* ---- MENUC / MENU8 / MAINMENU, ported ---- */

/* MAINMENU vs_mainMenu_getFirstEmptyItemSlot (2D10.c:1334) for weapons:
 * _getItemId is the weapon's blade. */
static int first_empty_weapon(void)
{
    int i;

    for (i = 0; i < 8; ++i) {
        if (vs_main_inventory.weapons[i].blade == 0) {
            break;
        }
    }

    return i;
}

/* MAINMENU vs_mainMenu_getItemCount (2D10.c:1320). */
static int item_count(int itemCategory)
{
    static const u_char capacities[7] = { 8, 16, 16, 8, 16, 48, 64 };
    int i;
    int count = 0;

    for (i = 0; i < capacities[itemCategory]; ++i) {
        if (pc_ui_item_record_id(itemCategory, i) != 0) {
            ++count;
        }
    }

    return count;
}

/* _disassembleWeapon (281). */
static void disassemble_weapon(int weaponIndex)
{
    int i;
    int index;
    vs_main_inventoryWeapon* weapon = &vs_main_inventory.weapons[weaponIndex];

    if (weapon->blade == 0) {
        return;
    }

    for (i = 0; i < vs_main_inventory.grips[weapon->grip - 1].gemSlots; ++i) {
        index = weapon->gems[i];
        if (index != 0) {
            vs_main_inventory.gems[index - 1].setItemIndex = 0;
            weapon->gems[i] = 0;
        }
    }

    vs_main_inventory.grips[weapon->grip - 1].assembledWeaponIndex = 0;
    vs_main_inventory.blades[weapon->blade - 1].assembledWeaponIndex = 0;

    index = weapon->index;
    vs_battle_rMemzero(weapon, sizeof *weapon);
    weapon->index = index;

    pc_ui_items_rebuild(0);
}

/* _assembleWeapon (309); combiningItem: MENUC's _combiningItem. */
static void assemble_weapon(int combiningItem, int bladeIndex, int gripIndex, int gemInfo)
{
    int i;
    int weaponIndex;

    vs_main_inventoryWeapon* weapon = &vs_main_inventory.weapons[combiningItem];
    vs_main_inventoryBlade* blade = &vs_main_inventory.blades[bladeIndex];
    vs_main_inventoryGrip* grip = &vs_main_inventory.grips[gripIndex];
    int gemSlots = grip->gemSlots;

    disassemble_weapon(combiningItem);

    weapon->blade = bladeIndex + 1;
    weapon->grip = gripIndex + 1;

    for (i = 0; i < gemSlots; ++i) {
        weapon->gems[i] = gemInfo;
        gemInfo >>= 8;
    }

    weaponIndex = combiningItem + 1;
    blade->assembledWeaponIndex = weaponIndex;
    grip->assembledWeaponIndex = weaponIndex;

    for (i = 0; i < gemSlots; ++i) {
        gemInfo = weapon->gems[i];
        if (gemInfo != 0) {
            vs_main_inventory.gems[gemInfo - 1].setItemIndex = weaponIndex;
        }
    }

    memset(weapon->name, vs_char_terminator, sizeof weapon->name);
    pc_ui_items_rebuild(0);
}

/* _disassembleShield (344). */
static void disassemble_shield(int index)
{
    int i;
    vs_main_inventoryShield* shield = &vs_main_inventory.shields[index];
    int slots = shield->base.gemSlots;

    for (i = 0; i < slots; ++i) {
        int gemIndex = shield->gems[i];
        if (gemIndex != 0) {
            vs_main_inventory.gems[gemIndex - 1].setItemIndex = 0;
            shield->gems[i] = 0;
        }
    }
}

/* _isValidGrip (359). */
static int valid_grip(vs_main_inventoryBlade* blade, vs_main_inventoryGrip* grip)
{
    static u_short _validGripFlags[] = { 0, 0xE, 0xF0, 0x100, 0x200 };

    if ((blade == NULL) || (grip == NULL)) {
        return 1;
    }
    /* Divergence: a grip category past the table (a corrupt save) fits nothing */
    if (grip->category >= sizeof _validGripFlags / sizeof _validGripFlags[0]) {
        return 0;
    }
    /* the shift by the blade's category as the R3000's sllv takes it (its low five bits) */
    return (1 << (blade->category & 0x1F)) & _validGripFlags[grip->category];
}

/* _attachGems (1498) without its menu: combiningItem the weapon / shield. */
static void attach_gems(int combiningItem, const char* gems, int isShield)
{
    int i;
    int gem;
    u_char* attachedGems;
    vs_main_inventoryShield* shield;
    vs_main_inventoryWeapon* weapon;

    weapon = &vs_main_inventory.weapons[combiningItem];
    shield = &vs_main_inventory.shields[combiningItem];

    if (isShield != 0) {
        attachedGems = shield->gems;
    } else {
        attachedGems = weapon->gems;
    }

    for (i = 0; i < 3; ++i) {
        gem = attachedGems[i];
        if (gem != 0) {
            vs_main_inventory.gems[gem - 1].setItemIndex = 0;
            attachedGems[i] = 0;
        }
    }

    for (i = 0; i < 3; ++i) {
        gem = gems[i];
        if (gem != 0) {
            vs_main_inventory.gems[gem - 1].setItemIndex = combiningItem + ((isShield << 7) + 1);
            attachedGems[i] = gem;
        }
    }
    if (isShield != 0) {
        if (shield->isEquipped != 0) {
            vs_battle_equipShield(shield);
        }
    } else if (weapon->isEquipped != 0) {
        vs_battle_equipWeapon(weapon);
    }
}

/* _repairMenu (2355) state 2. */
static void repair(void)
{
    int ppCost;
    int i;
    int new_var;

    for (i = 0; i < 16; ++i) {
        new_var = vs_main_inventoryIndices.blades[i];
        if (new_var != 0) {
            vs_main_inventoryBlade* blade = &vs_main_inventory.blades[new_var - 1];

            ppCost = blade->maxDp - blade->currentDp;
            ppCost = blade->currentPp - (ppCost / 100);
            blade->currentDp = blade->maxDp;

            if (ppCost >= 0) {
                blade->currentPp = ppCost;
            } else {
                blade->currentPp = 0;
            }
        }
    }

    for (i = 0; i < 8; ++i) {
        new_var = vs_main_inventoryIndices.weapons[i];
        if (new_var != 0) {
            vs_main_inventoryWeapon* weapon = &vs_main_inventory.weapons[new_var - 1];
            if (weapon->isEquipped != 0) {
                vs_battle_equipWeapon(weapon);
            }
        }
    }

    for (i = 0; i < 8; ++i) {
        new_var = vs_main_inventoryIndices.shields[i];
        if (new_var != 0) {
            vs_main_inventoryShield* shield = &vs_main_inventory.shields[new_var - 1];

            ppCost = shield->base.maxDp - shield->base.currentDp;
            ppCost = shield->base.currentPp - (ppCost / 100);
            shield->base.currentDp = shield->base.maxDp;

            if (ppCost >= 0) {
                shield->base.currentPp = ppCost;
            } else {
                shield->base.currentPp = 0;
            }

            if (shield->isEquipped != 0) {
                vs_battle_equipShield(shield);
            }
        }
    }

    for (i = 0; i < 16; ++i) {
        new_var = vs_main_inventoryIndices.armor[i];
        if (new_var != 0) {
            vs_main_inventoryArmor* armor = &vs_main_inventory.armor[new_var - 1];
            if (armor->category != 7) {

                ppCost = armor->maxDp - armor->currentDp;
                ppCost = armor->currentPp - (ppCost / 100);
                armor->currentDp = armor->maxDp;

                if (ppCost >= 0) {
                    armor->currentPp = ppCost;
                } else {
                    armor->currentPp = 0;
                }
                if (armor->limb != 0) {
                    vs_battle_equipArmor(armor->limb - 1, armor);
                }
            }
        }
    }
}

/* _setClassAffinities (4497). */
static void set_class_affinities(signed char* first, signed char* second, signed char* result, int materialsDifferent,
    int setAffinities)
{
    static char _classFactorLookup[][6] = { { 2, 0, 1, 1, 1, 0 }, { 0, 2, 0, 1, 1, 1 }, { 1, 0, 2, 0, 1, 1 },
        { 1, 1, 0, 2, 0, 1 }, { 1, 1, 1, 0, 2, 0 }, { 0, 1, 1, 1, 0, 2 } };

    static char _affinityFactorLookup[][7] = { { 2, 1, 1, 1, 1, 1, 1 }, { 1, 2, 1, 0, 1, 1, 1 },
        { 1, 1, 2, 1, 0, 1, 1 }, { 1, 0, 1, 2, 1, 1, 1 }, { 1, 1, 0, 1, 2, 1, 1 }, { 1, 1, 1, 1, 1, 2, 0 },
        { 1, 1, 1, 1, 1, 0, 2 } };

    static short _sameMaterialStatFactors[] = { 4, 6, 8 };
    static short _differentMaterialStatFactors[] = { 3, 5, 7 };

    short statSum;
    short statValue;
    int i;
    int nStats;
    u_int firstMaxStat;
    u_int secondMaxStat;
    signed char firstMaxVal;
    signed char secondMaxVal;
    signed char minStatVal;
    signed char maxStatValue;
    short factorLookup;

    nStats = 6;
    if (setAffinities != 0) {
        nStats = 7;
    }
    secondMaxVal = -128;
    firstMaxVal = -128;
    maxStatValue = -128;
    secondMaxStat = 0;
    firstMaxStat = 0;

    for (i = 0; i < nStats; ++i) {
        if (firstMaxVal < first[i]) {
            firstMaxVal = first[i];
            firstMaxStat = i;
        }
        if (maxStatValue < first[i]) {
            maxStatValue = first[i];
        }
    }

    for (i = 0; i < nStats; ++i) {
        if (secondMaxVal < second[i]) {
            secondMaxVal = second[i];
            secondMaxStat = i;
        }
        if (maxStatValue < second[i]) {
            maxStatValue = second[i];
        }
    }

    minStatVal = 127;

    for (i = 0; i < nStats; ++i) {
        if (first[i] < minStatVal) {
            minStatVal = first[i];
        }
    }

    for (i = 0; i < nStats; ++i) {
        if (second[i] < minStatVal) {
            minStatVal = second[i];
        }
    }

    for (i = 0; i < nStats; ++i) {
        if ((first[i] == maxStatValue) || ((second[i] == maxStatValue))) {
            result[i] = maxStatValue;
        } else if ((first[i] == minStatVal) || (second[i] == minStatVal)) {
            result[i] = minStatVal;
        } else {
            if (setAffinities == 0) {
                factorLookup = _classFactorLookup[firstMaxStat][secondMaxStat];
            } else {
                factorLookup = _affinityFactorLookup[firstMaxStat][secondMaxStat];
            }
            statSum = first[i] + second[i];
            if (materialsDifferent == 0) {
                statValue = (statSum * _sameMaterialStatFactors[factorLookup]) / 10;
            } else {
                statValue = (statSum * *(factorLookup + _differentMaterialStatFactors)) / 10;
            }
            if (statValue > 100) {
                statValue = 100;
            } else if (statValue < -100) {
                statValue = -100;
            }
            result[i] = statValue;
        }
    }
}

/* _setTypeValues (4595). */
static void set_type_values(signed char* first, signed char* second, signed char* result, int materialsDifferent)
{
    static short _sameMaterialTypeFactors[] = { 6, 8 };
    static short _differentMaterialTypeFactors[] = { 4, 6 };

    short typeValue;
    short typeValueSum;
    int sameMaxType;
    int i;
    u_int firstMaxType;
    u_int secondMaxType;
    signed char firstMaxVal;
    signed char secondMaxVal;

    secondMaxVal = -128;
    firstMaxVal = -128;
    secondMaxType = 1;
    firstMaxType = 1;

    for (i = 1; i < 4; ++i) {
        if (firstMaxVal < first[i]) {
            firstMaxVal = first[i];
            firstMaxType = i;
        }
    }

    for (i = 1; i < 4; ++i) {
        if (secondMaxVal < second[i]) {
            secondMaxVal = second[i];
            secondMaxType = i;
        }
    }

    for (i = 1; i < 4; ++i) {
        sameMaxType = firstMaxType == secondMaxType;
        typeValueSum = first[i] + second[i];
        if (materialsDifferent == 0) {
            typeValue = (typeValueSum * _sameMaterialTypeFactors[sameMaxType]) / 10;
        } else {
            typeValue = (typeValueSum * _differentMaterialTypeFactors[sameMaxType]) / 10;
        }
        if (typeValue > 100) {
            typeValue = 100;
        } else if (typeValue < -100) {
            typeValue = -100;
        }
        result[i] = typeValue;
    }
}

/* The SYD files (BLADE, SHIELD, ARMOR: _loadSyd's), read from the disc once
 * (host memory only). Their header (_syd, 4646): three little-endian u_longs,
 * the offsets of the combinations, materials and init data. */
static unsigned char* syd_data[3];
static unsigned syd_size[3];
static int syd_state[3]; /* 0 not read, 1 read, -1 failed */

static const unsigned char* syd(int kind)
{
    static const char* const paths[3] = { "MENU/BLADE.SYD", "MENU/SHIELD.SYD", "MENU/ARMOR.SYD" };
    if (!syd_state[kind]) {
        syd_state[kind] = pc_ui_disc_read(paths[kind], &syd_data[kind], &syd_size[kind]) ? 1 : -1;
        if (syd_state[kind] > 0 && syd_size[kind] < 12) { /* no header: not a SYD file */
            free(syd_data[kind]);
            syd_data[kind] = NULL;
            syd_size[kind] = 0;
            syd_state[kind] = -1;
        }
    }
    return syd_state[kind] > 0 ? syd_data[kind] : NULL;
}

static u_int syd_offset(const unsigned char* p, int i)
{
    p += i * 4;
    return p[0] | p[1] << 8 | p[2] << 16 | (u_int)p[3] << 24;
}

/* A table entry inside the file, a plain char as the original's char*
 * tables give it (the original reads its heap copy unchecked; a short file
 * reads 0 here). */
static int syd_byte(int kind, u_int offset)
{
    return offset < syd_size[kind] ? (char)syd_data[kind][offset] : 0;
}

/* Init record idx of `size` bytes at initData lies inside the file (no
 * unsigned wrap for a corrupt offset or index). */
static int syd_record_ok(int kind, u_int initData, u_int idx, u_int size)
{
    return initData <= syd_size[kind] && (syd_size[kind] - initData) / size > idx;
}

/* Divergence: a material the tables give past vs_main_materialModifiers'
 * eight rows (a corrupt file; the original indexes it unchecked) refuses
 * the combine. */
static int material_in_range(int material) { return material >= 0 && material < 8; }

/* _combineBlades (4657). 0 when BLADE.SYD cannot be read. */
static int combine_blades(vs_main_inventoryBlade* first, vs_main_inventoryBlade* second, vs_main_inventoryBlade* result)
{
    struct {
        char subId;
        char wepId;
        char category;
        char damageType;
        char costType;
        char cost;
        char unk6;
        char unk7;
        char strength;
        char intelligence;
        char agility;
        char _;
        vs_battle_range_t range;
    } info;
    const unsigned char* sydData = syd(0);
    u_int combinations, materials, initData;

    if (sydData == NULL) {
        return 0;
    }
    combinations = syd_offset(sydData, 0);
    materials = syd_offset(sydData, 1);
    initData = syd_offset(sydData, 2);

    vs_main_memcpy(result, first, sizeof *result);

    result->material = syd_byte(0, materials + (first->material - 3) * 500 + first->category * 50
            + (second->material - 3) * 10 + second->category);
    result->subId = syd_byte(0, combinations + (first->id * 96) + second->id);
    result->id = result->subId;
    /* the original's records are 16 bytes apart (info is 16 bytes) */
    if (!syd_record_ok(0, initData, (u_char)result->id, 16) || !material_in_range(result->material)) {
        return 0;
    }
    memcpy(&info, sydData + initData + (u_char)result->id * 16, sizeof info);
    result->wepId = info.wepId;
    result->category = info.category;
    result->cost = info.cost;
    result->damageType = info.damageType & 3;
    result->costType = info.costType & 7;
    result->unk12 = info.unk6;
    result->range = info.range;
    result->strength = info.strength + vs_main_materialModifiers[result->material][24];
    result->intelligence = info.intelligence + vs_main_materialModifiers[result->material][26];
    result->agility = info.agility + vs_main_materialModifiers[result->material][28];
    result->maxDp = result->currentDp = (first->maxDp + second->maxDp) >> 1;
    result->maxPp = (first->maxPp + second->maxPp) >> 1;
    result->currentPp = (first->currentPp + second->currentPp) >> 1;

    if (first->material == second->material) {
        set_class_affinities(first->classes, second->classes, result->classes, 0, 0);
        set_class_affinities(first->affinities, second->affinities, result->affinities, 0, 1);
    } else {
        set_class_affinities(first->classes, second->classes, result->classes, 1, 0);
        set_class_affinities(first->affinities, second->affinities, result->affinities, 1, 1);
    }
    return 1;
}

/* _armorInfo (4714): the shields' and armor's init records. */
typedef struct {
    char subId;
    char wepId;
    char category;
    char gemSlots;
    char strength;
    char intelligence;
    char agility;
    char _;
} armor_info;

/* _combineShields (4727). */
static int combine_shields(vs_main_inventoryArmor* first, vs_main_inventoryArmor* second, vs_main_inventoryArmor* result)
{
    armor_info info;
    const unsigned char* sydData = syd(1);
    u_int combinations, materials, initData;

    if (sydData == NULL) {
        return 0;
    }
    combinations = syd_offset(sydData, 0);
    materials = syd_offset(sydData, 1);
    initData = syd_offset(sydData, 2);

    vs_main_memcpy(result, first, sizeof *result);

    result->material = syd_byte(1, materials + (first->material * 8) + second->material);
    result->subId = syd_byte(1, combinations + (first->subId * 17) + second->subId);
    result->id = result->subId + 126;
    if (!syd_record_ok(1, initData, (u_char)result->subId, sizeof info) || !material_in_range(result->material)) {
        return 0;
    }
    memcpy(&info, sydData + initData + (u_char)result->subId * sizeof info, sizeof info);
    result->wepId = info.wepId;
    result->category = info.category;
    result->gemSlots = info.gemSlots;
    result->strength = info.strength + vs_main_materialModifiers[result->material][24];
    result->intelligence = info.intelligence + vs_main_materialModifiers[result->material][26];
    result->agility = info.agility + vs_main_materialModifiers[result->material][28];
    result->maxDp = result->currentDp = (first->maxDp + second->maxDp) / 2;
    result->maxPp = ((first->maxPp + second->maxPp) >> 1);
    result->currentPp = ((first->currentPp + second->currentPp) >> 1);
    if (first->material == second->material) {
        set_class_affinities(first->classes, second->classes, result->classes, 0, 0);
        set_class_affinities(first->affinities, second->affinities, result->affinities, 0, 1);
        set_type_values(first->types, second->types, result->types, 0);
    } else {
        set_class_affinities(first->classes, second->classes, result->classes, 1, 0);
        set_class_affinities(first->affinities, second->affinities, result->affinities, 1, 1);
        set_type_values(first->types, second->types, result->types, 1);
    }
    return 1;
}

/* _combineArmor (4768). */
static int combine_armor(vs_main_inventoryArmor* first, vs_main_inventoryArmor* second, vs_main_inventoryArmor* result)
{
    armor_info info;
    const unsigned char* sydData = syd(2);
    u_int combinations, materials, initData;

    if (sydData == NULL) {
        return 0;
    }
    combinations = syd_offset(sydData, 0);
    materials = syd_offset(sydData, 1);
    initData = syd_offset(sydData, 2);

    vs_main_memcpy(result, first, sizeof *result);

    result->material = syd_byte(2, materials + first->material * 128 + (first->category - 2) * 32
            + second->material * 4 + (second->category - 2));
    result->subId = syd_byte(2, combinations + (first->subId - 16) * 65 + (second->subId - 16));
    result->id = result->subId + 126;
    if (!syd_record_ok(2, initData, (u_char)result->subId, sizeof info) || !material_in_range(result->material)) {
        return 0;
    }
    memcpy(&info, sydData + initData + (u_char)result->subId * sizeof info, sizeof info);
    result->category = info.category;
    result->strength = info.strength + vs_main_materialModifiers[result->material][24];
    result->intelligence = info.intelligence + vs_main_materialModifiers[result->material][26];
    result->agility = info.agility + vs_main_materialModifiers[result->material][28];
    result->maxDp = result->currentDp = (first->maxDp + second->maxDp) / 2;
    if (first->material == second->material) {
        set_class_affinities(first->classes, second->classes, result->classes, 0, 0);
        set_class_affinities(first->affinities, second->affinities, result->affinities, 0, 1);
        set_type_values(first->types, second->types, result->types, 0);
    } else {
        set_class_affinities(first->classes, second->classes, result->classes, 1, 0);
        set_class_affinities(first->affinities, second->affinities, result->affinities, 1, 1);
        set_type_values(first->types, second->types, result->types, 1);
    }
    return 1;
}

/* The combine's result as _setCombineBladeUi (2561) / _initCombineShields
 * (3155) / _initCombineArmor (3725) leave it in their buffers for both
 * sources set: zeroed, combined, assembledWeaponIndex / isEquipped / limb 0.
 * a, b: records (0-based). 0 when the SYD file cannot be read. */
static vs_main_inventoryBlade blade_buf;
static vs_main_inventoryShield shield_buf;
static vs_main_inventoryArmor armor_buf;

static int combine_result(int category, int a, int b)
{
    switch (category) {
    case 0:
        vs_battle_rMemzero(&blade_buf, sizeof blade_buf);
        if (!combine_blades(&vs_main_inventory.blades[a], &vs_main_inventory.blades[b], &blade_buf))
            return 0;
        blade_buf.assembledWeaponIndex = 0;
        return 1;
    case 1:
        vs_battle_rMemzero(&shield_buf, sizeof shield_buf);
        if (!combine_shields(&vs_main_inventory.shields[a].base, &vs_main_inventory.shields[b].base, &shield_buf.base))
            return 0;
        shield_buf.isEquipped = 0;
        return 1;
    default:
        vs_battle_rMemzero(&armor_buf, sizeof armor_buf);
        if (!combine_armor(&vs_main_inventory.armor[a], &vs_main_inventory.armor[b], &armor_buf))
            return 0;
        armor_buf.limb = 0;
        return 1;
    }
}

/* MENU8 _copyNormalizedString (21A0.c:89): the 20-code buffer into the
 * 24-byte name. Returns 1 when it is blank. */
static int copy_normalized_string(const unsigned char* _stringBuf, char* stringBuf)
{
    int pos;
    int i;

    for (pos = 0, i = 0; i < 20; ++pos, ++i) {
        if (_stringBuf[i] == vs_char_terminator) {
            break;
        }
        stringBuf[pos] = _stringBuf[i];
    }

    stringBuf[pos--] = vs_char_terminator;

    while (pos >= 0 && (u_char)stringBuf[pos] == vs_char_space) {
        stringBuf[pos--] = vs_char_terminator;
        if (pos < 0) {
            return 1;
        }
    }
    /* Divergence: an empty buffer (a terminator first; MENU8's field never
     * holds one) is blank here, where the original reads before its buffer */
    if (pos < 0) {
        return 1;
    }

    pos += 2;

    while (pos < 24) {
        stringBuf[pos++] = vs_char_zero;
    }

    return 0;
}

/* MENU8 _renameMenuInput's initConfirmationScreen and confirmationScreen on
 * Yes (21A0.c:802, 865): the name buffer (blank: the blade's ITEMNAME
 * record) into weapon weaponId, re-equipped when equipped. 0 when the
 * blade's name cannot be read (nothing written). */
static int write_name(int weaponId, const unsigned char* field)
{
    char _nameBuffer[24];
    vs_main_inventoryWeapon* weapon = &vs_main_inventory.weapons[weaponId];

    if (copy_normalized_string(field, _nameBuffer) != 0) {
        const unsigned char* name =
            pc_ui_item_name_bytes(vs_main_inventory.blades[vs_main_inventory.weapons[weaponId].blade - 1].id);
        if (name == NULL) {
            return 0;
        }
        vs_battle_rMemcpy(_nameBuffer, name, 0x18);
    }

    vs_main_playSfxDefault(0x7E, 0xD);
    vs_battle_rMemcpy(weapon->name, _nameBuffer, sizeof _nameBuffer);

    if (weapon->isEquipped != 0) {
        vs_battle_equipWeapon(weapon);
    }
    return 1;
}

/* vs_menu8_execRename state 0 (21A0.c:820) with stringBuf[0] 1: the field as
 * the rename opens on weapon w's name. */
static void name_field(const vs_main_inventoryWeapon* w, unsigned char* out)
{
    int i;
    const char* var_v1 = w->name;
    const char* end = w->name + sizeof w->name;

    for (i = 0; i < 20; ++i) {
        out[i] = vs_char_space;
    }
    for (i = 0; i < 20 && var_v1 < end; ++i) {
        int c = (u_char)*var_v1++;
        if (c == vs_char_terminator) {
            break;
        } else if (c == vs_char_space) {
            continue;
        } else if (c == vs_char_spacing) {
            ++var_v1;
        } else {
            out[i] = c;
        }
    }
}

/* ---- The snapshot ---- */

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

/* "Iron Sword" (material and kind; the Items list's sub line) */
static void sub_line(char* out, int size, int material, const char* kind)
{
    char both[128];
    int n = 0;
    const char* mat = pc_ui_material_name(material);
    if (!*mat) {
        copy_text(out, size, kind);
        return;
    }
    while (*mat && n < 60)
        both[n++] = *mat++;
    if (kind[0]) {
        both[n++] = ' ';
        copy_text(both + n, (int)sizeof both - n, kind);
    } else {
        both[n] = 0;
    }
    copy_text(out, size, both);
}

static const char* blade_kind(int category)
{
    return category >= 1 && category <= 9 ? pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_dagger + category - 1) : "";
}

static void blank_row(pc_ui_ws_row* e, int index)
{
    vs_main_bzero(e, sizeof *e);
    e->inventory = index;
    e->parent = -1;
    e->gems[0] = e->gems[1] = e->gems[2] = -1;
}

static int material_ok(int material) { return material >= 0 && material < 8 && ((snap.materials >> material) & 1); }

static void weapon_row(pc_ui_ws_row* e, int i)
{
    vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[i];
    vs_main_inventoryBlade* b = &vs_main_inventory.blades[w->blade - 1];
    int k;
    blank_row(e, i);
    e->id = w->blade;
    pc_ui_text_decode((const unsigned char*)w->name, sizeof w->name, e->name, sizeof e->name);
    e->material = b->material;
    e->kind = (u_char)b->category;
    sub_line(e->sub, sizeof e->sub, e->material, blade_kind(e->kind));
    e->mark = w->isEquipped ? PC_UI_MARK_EQUIPPED : PC_UI_MARK_NONE;
    e->gem_slots = w->grip ? vs_main_inventory.grips[w->grip - 1].gemSlots : 0;
    for (k = 0; k < 3 && k < e->gem_slots; ++k)
        e->gems[k] = w->gems[k] ? w->gems[k] - 1 : -1;
    name_field(w, e->field);
}

static void shield_row(pc_ui_ws_row* e, int i)
{
    vs_main_inventoryShield* sh = &vs_main_inventory.shields[i];
    int k;
    blank_row(e, i);
    e->id = sh->base.id;
    copy_text(e->name, sizeof e->name, pc_ui_item_name(sh->base.id));
    e->material = sh->base.material;
    sub_line(e->sub, sizeof e->sub, e->material, "");
    e->mark = sh->isEquipped ? PC_UI_MARK_EQUIPPED : PC_UI_MARK_NONE;
    e->gem_slots = sh->base.gemSlots;
    for (k = 0; k < 3 && k < e->gem_slots; ++k)
        e->gems[k] = sh->gems[k] ? sh->gems[k] - 1 : -1;
    e->material_ok = material_ok(e->material);
}

static void fill_lists(void)
{
    int i, n, index;
    pc_ui_ws_row* e;

    /* weapons, shields, blades, grips, gems: every listed record (MENUC's lists) */
    for (i = n = 0; i < 8; ++i)
        if ((index = vs_main_inventoryIndices.weapons[i]) && index <= 8 && vs_main_inventory.weapons[index - 1].blade)
            weapon_row(&snap.weapons[n++], index - 1);
    snap.count[PC_UI_WSL_WEAPONS] = n;
    for (i = n = 0; i < 8; ++i)
        if ((index = vs_main_inventoryIndices.shields[i]) && index <= 8 && vs_main_inventory.shields[index - 1].base.id)
            shield_row(&snap.shields[n++], index - 1);
    snap.count[PC_UI_WSL_SHIELDS] = n;
    for (i = n = 0; i < 16; ++i) {
        vs_main_inventoryGrip* g;
        if (!(index = vs_main_inventoryIndices.grips[i]) || index > 16 || !vs_main_inventory.grips[index - 1].id)
            continue;
        g = &vs_main_inventory.grips[index - 1];
        e = &snap.grips[n++];
        blank_row(e, index - 1);
        e->id = g->id;
        copy_text(e->name, sizeof e->name, pc_ui_item_name(g->id));
        e->kind = g->category;
        copy_text(e->sub, sizeof e->sub,
            g->category >= 1 && g->category <= 4 ? pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_shortGrip + g->category - 1)
                                                 : "");
        e->gem_slots = g->gemSlots;
        e->parent = g->assembledWeaponIndex ? g->assembledWeaponIndex - 1 : -1;
        if (e->parent >= 0 && e->parent < 8)
            e->mark = vs_main_inventory.weapons[e->parent].isEquipped ? PC_UI_MARK_EQUIPPED : PC_UI_MARK_SET;
    }
    snap.count[PC_UI_WSL_GRIPS] = n;
    for (i = n = 0; i < 16; ++i) {
        vs_main_inventoryBlade* b;
        int k;
        if (!(index = vs_main_inventoryIndices.blades[i]) || index > 16 || !vs_main_inventory.blades[index - 1].id)
            continue;
        b = &vs_main_inventory.blades[index - 1];
        e = &snap.blades[n++];
        blank_row(e, index - 1);
        e->id = b->id;
        copy_text(e->name, sizeof e->name, pc_ui_item_name(b->id));
        e->material = b->material;
        e->kind = (u_char)b->category;
        sub_line(e->sub, sizeof e->sub, e->material, blade_kind(e->kind));
        e->parent = b->assembledWeaponIndex ? b->assembledWeaponIndex - 1 : -1;
        if (e->parent >= 0 && e->parent < 8)
            e->mark = vs_main_inventory.weapons[e->parent].isEquipped ? PC_UI_MARK_EQUIPPED : PC_UI_MARK_SET;
        e->material_ok = material_ok(e->material);
        for (k = 0; k < snap.count[PC_UI_WSL_GRIPS]; ++k)
            if (valid_grip(b, &vs_main_inventory.grips[snap.grips[k].inventory]))
                e->grips_ok |= 1u << k;
    }
    snap.count[PC_UI_WSL_BLADES] = n;
    /* armor: _initUiArmor lists every record but accessories */
    for (i = n = 0; i < 16; ++i) {
        vs_main_inventoryArmor* a;
        if (!(index = vs_main_inventoryIndices.armor[i]) || index > 16 || !vs_main_inventory.armor[index - 1].id)
            continue;
        a = &vs_main_inventory.armor[index - 1];
        if (a->category == 7)
            continue;
        e = &snap.armor[n++];
        blank_row(e, index - 1);
        e->id = a->id;
        copy_text(e->name, sizeof e->name, pc_ui_item_name(a->id));
        e->material = a->material;
        e->kind = a->category;
        sub_line(e->sub, sizeof e->sub, e->material,
            a->category >= 1 && a->category <= 7 ? pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_armorCategory + a->category - 1)
                                                 : "");
        e->mark = a->limb ? PC_UI_MARK_EQUIPPED : PC_UI_MARK_NONE;
        e->material_ok = material_ok(e->material);
    }
    snap.count[PC_UI_WSL_ARMOR] = n;
    for (i = n = 0; i < 48; ++i) {
        vs_main_inventoryGem* g;
        int set;
        if (!(index = vs_main_inventoryIndices.gems[i]) || index > 48 || !vs_main_inventory.gems[index - 1].id)
            continue;
        g = &vs_main_inventory.gems[index - 1];
        e = &snap.gems[n++];
        blank_row(e, index - 1);
        e->id = g->id;
        copy_text(e->name, sizeof e->name, pc_ui_item_name(g->id));
        copy_text(e->sub, sizeof e->sub, pc_ui_item_help(g->id - 261 + VS_ITEMHELP_BIN_INDEX_gemFeldspar));
        set = g->setItemIndex;
        if (set) {
            e->parent_shield = (set & 0x80) != 0;
            e->parent = (set & 0x7F) - 1;
            if (e->parent >= 0 && e->parent < 8)
                e->mark = (e->parent_shield ? vs_main_inventory.shields[e->parent].isEquipped
                                            : vs_main_inventory.weapons[e->parent].isEquipped)
                    ? PC_UI_MARK_EQUIPPED
                    : PC_UI_MARK_SET;
        }
    }
    snap.count[PC_UI_WSL_GEMS] = n;
}

static int dp_shown(int dp) { return (dp + 99) / 100; }

/* _repairMenu's change per record, in its order (blades, shields, armor but accessories). */
static void fill_repairs(void)
{
    int i, index, n = 0;
    pc_ui_ws_repair* e;
    for (i = 0; i < 16 && n < PC_UI_WS_REPAIRS; ++i) {
        vs_main_inventoryBlade* b;
        int cost;
        if (!(index = vs_main_inventoryIndices.blades[i]) || index > 16 || !vs_main_inventory.blades[index - 1].id)
            continue;
        b = &vs_main_inventory.blades[index - 1];
        e = &snap.repairs[n++];
        vs_main_bzero(e, sizeof *e);
        e->category = PC_UI_IC_BLADE;
        e->inventory = index - 1;
        if (b->assembledWeaponIndex >= 1 && b->assembledWeaponIndex <= 8) {
            vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[b->assembledWeaponIndex - 1];
            pc_ui_text_decode((const unsigned char*)w->name, sizeof w->name, e->name, sizeof e->name);
            e->equipped = w->isEquipped != 0;
        } else {
            copy_text(e->name, sizeof e->name, pc_ui_item_name(b->id));
        }
        cost = b->currentPp - (b->maxDp - b->currentDp) / 100;
        e->dp = dp_shown(b->currentDp), e->dp_max = dp_shown(b->maxDp);
        e->pp = b->currentPp, e->pp_max = b->maxPp;
        e->dp_after = dp_shown(b->maxDp);
        e->pp_after = cost >= 0 ? cost : 0;
    }
    for (i = 0; i < 8 && n < PC_UI_WS_REPAIRS; ++i) {
        vs_main_inventoryShield* sh;
        int cost;
        if (!(index = vs_main_inventoryIndices.shields[i]) || index > 8 || !vs_main_inventory.shields[index - 1].base.id)
            continue;
        sh = &vs_main_inventory.shields[index - 1];
        e = &snap.repairs[n++];
        vs_main_bzero(e, sizeof *e);
        e->category = PC_UI_IC_SHIELD;
        e->inventory = index - 1;
        copy_text(e->name, sizeof e->name, pc_ui_item_name(sh->base.id));
        e->equipped = sh->isEquipped != 0;
        cost = sh->base.currentPp - (sh->base.maxDp - sh->base.currentDp) / 100;
        e->dp = dp_shown(sh->base.currentDp), e->dp_max = dp_shown(sh->base.maxDp);
        e->pp = sh->base.currentPp, e->pp_max = sh->base.maxPp;
        e->dp_after = dp_shown(sh->base.maxDp);
        e->pp_after = cost >= 0 ? cost : 0;
    }
    for (i = 0; i < 16 && n < PC_UI_WS_REPAIRS; ++i) {
        vs_main_inventoryArmor* a;
        int cost;
        if (!(index = vs_main_inventoryIndices.armor[i]) || index > 16 || !vs_main_inventory.armor[index - 1].id
            || vs_main_inventory.armor[index - 1].category == 7)
            continue;
        a = &vs_main_inventory.armor[index - 1];
        e = &snap.repairs[n++];
        vs_main_bzero(e, sizeof *e);
        e->category = PC_UI_IC_ARMOR;
        e->inventory = index - 1;
        copy_text(e->name, sizeof e->name, pc_ui_item_name(a->id));
        e->equipped = a->limb != 0;
        cost = a->currentPp - (a->maxDp - a->currentDp) / 100;
        e->dp = dp_shown(a->currentDp), e->dp_max = dp_shown(a->maxDp);
        e->pp = a->currentPp, e->pp_max = a->maxPp;
        e->dp_after = dp_shown(a->maxDp);
        e->pp_after = cost >= 0 ? cost : 0;
    }
    snap.repair_count = n;
}

/* The texts, the grid and the glyphs (once: they never change). */
static pc_ui_workshop statics;
static int statics_ready;

static void fill_statics(void)
{
    int i, j;
    const unsigned char* table;
    if (statics_ready)
        return;
    statics_ready = 1;
    for (i = 0; i < PC_UI_WS_TEXTS && i <= VS_MENU12_BIN_INDEX_cancelCombineConfirm; ++i)
        pc_ui_text_decode((const unsigned char*)&menu12_text[menu12_text[i]], 512, statics.text[i], PC_UI_WS_TEXT);
    /* MENU8 _renderRenameMenu: charTable rows of 15 codes (14 cells and a newline) */
    table = (const unsigned char*)(rename_text + VS_rename_OFFSET_charTable);
    for (i = 0; i < PC_UI_WS_GRID_ROWS; ++i)
        for (j = 0; j < PC_UI_WS_GRID_COLS; ++j)
            statics.grid[i][j] = table[i * 15 + j];
    for (i = 0; i < 256; ++i) {
        unsigned char one[2];
        one[0] = (unsigned char)i;
        one[1] = vs_char_terminator;
        statics.glyph[i][0] = 0;
        if (i < 0xE5 && i != vs_char_space)
            pc_ui_text_decode(one, 2, statics.glyph[i], sizeof statics.glyph[i]);
    }
    copy_text(statics.glyph[vs_char_space], sizeof statics.glyph[0], " ");
    {
        /* the grid's symbols the text decoder leaves out (it skips the
         * charset's pictographs), as MENU8's grid shows them
         * (build/assets/MENU/MENU8.PRG/rename.vsString.yaml charTable) */
        static const struct {
            u_char code;
            const char* utf8;
        } symbols[] = { { 0x87, "\xe2\x80\xbc" }, { 0x8C, "\xc2\xb7" }, { 0x8E, "\xe2\x8b\xaf" },
            { 0x8D, "\xe2\x80\x94" }, { 0xAD, "\xe2\x99\xaa" }, { 0x8B, "\xc3\xb7" }, { 0x88, "\xe2\x89\xa0" },
            { 0x89, "\xe2\x89\xa6" }, { 0x8A, "\xe2\x89\xa7" } };
        for (i = 0; i < (int)(sizeof symbols / sizeof symbols[0]); ++i)
            if (!statics.glyph[symbols[i].code][0])
                copy_text(statics.glyph[symbols[i].code], sizeof statics.glyph[0], symbols[i].utf8);
    }
    for (i = 1; i < 8; ++i)
        copy_text(statics.material[i], sizeof statics.material[i], pc_ui_material_name(i));
}

/* ---- Previews ---- */

static void details_blade(pc_ui_item_details* d, vs_main_inventoryBlade* blade)
{
    vs_battle_uiEquipment e;
    int k;
    vs_main_bzero(d, sizeof *d);
    d->category = PC_UI_IC_BLADE;
    d->inventory = -1;
    d->stats.inventory = -1;
    vs_battle_copyInventoryBladeStats(&e, blade);
    copy_text(d->stats.name, sizeof d->stats.name, pc_ui_item_name(e.id));
    copy_text(d->stats.material, sizeof d->stats.material, pc_ui_material_name(e.material));
    d->stats.dp = dp_shown(e.currentDp);
    d->stats.dp_max = dp_shown(e.maxDp);
    d->stats.pp = e.currentPp;
    d->stats.pp_max = e.maxPp;
    d->stats.str = e.strength;
    d->stats.intel = e.intelligence;
    d->stats.agl = e.agility;
    for (k = 0; k < PC_UI_CLASSES; ++k)
        d->stats.classes[k] = e.classes[k];
    for (k = 0; k < PC_UI_AFFINITIES; ++k)
        d->stats.affinities[k] = e.affinities[k];
    d->stats.damage_type = e.damageType >= 1 && e.damageType <= 3 ? e.damageType - 1 : -1;
    d->stats.two_handed = (u_char)e.category >= 1 && ((0x1D4 >> ((u_char)e.category - 1)) & 1);
    d->range = e.range.range;
    d->risk = e.cost;
    d->blade_category = (u_char)e.category;
    d->valid = 1;
}

static void class_affinity(pc_ui_item* it, const vs_battle_classAffinityCurrent* ca)
{
    int i;
    for (i = 0; i < PC_UI_CLASSES; ++i)
        it->classes[i] = ca->class[0][i];
    for (i = 0; i < PC_UI_AFFINITIES; ++i)
        it->affinities[i] = (short)ca->affinity[0][i];
}

static void details_shield(pc_ui_item_details* d, vs_main_inventoryShield* sh)
{
    vs_battle_uiShield u;
    int k;
    vs_main_bzero(d, sizeof *d);
    d->category = PC_UI_IC_SHIELD;
    d->inventory = -1;
    d->stats.inventory = -1;
    d->stats.damage_type = -1;
    vs_battle_applyShield(&u, sh);
    copy_text(d->stats.name, sizeof d->stats.name, pc_ui_item_name(sh->base.id));
    copy_text(d->stats.material, sizeof d->stats.material, pc_ui_material_name(sh->base.material));
    d->stats.dp = dp_shown(u.currentDp);
    d->stats.dp_max = dp_shown(u.maxDp);
    d->stats.pp = u.currentPp;
    d->stats.pp_max = u.maxPp;
    d->stats.str = u.currentStr;
    d->stats.intel = u.currentInt;
    d->stats.agl = u.currentAgility;
    class_affinity(&d->stats, &u.classAffinityCurrent);
    for (k = 0; k < 3; ++k)
        d->types[k] = (signed char)u.types[k + 1];
    d->armor_category = (u_char)sh->base.category;
    d->gem_slots = sh->base.gemSlots;
    d->valid = 1;
}

static void details_armor(pc_ui_item_details* d, vs_main_inventoryArmor* a)
{
    vs_battle_uiArmor u;
    int k;
    vs_main_bzero(d, sizeof *d);
    d->category = PC_UI_IC_ARMOR;
    d->inventory = -1;
    d->stats.inventory = -1;
    d->stats.damage_type = -1;
    vs_battle_applyArmor(&u, a);
    copy_text(d->stats.name, sizeof d->stats.name, pc_ui_item_name(a->id));
    copy_text(d->stats.material, sizeof d->stats.material, pc_ui_material_name(a->material));
    d->stats.dp = dp_shown(u.currentDp);
    d->stats.dp_max = dp_shown(u.maxDp);
    d->stats.str = u.currentStr;
    d->stats.intel = u.currentInt;
    d->stats.agl = u.currentAgility;
    class_affinity(&d->stats, &u.classAffinityCurrent);
    for (k = 0; k < 3; ++k)
        d->types[k] = (short)u.types[k + 1];
    d->armor_category = (u_char)a->category;
    d->valid = 1;
}

/* Record r of category c (0-based) exists. */
static int exists(int c, int r)
{
    static const u_char capacities[7] = { 8, 16, 16, 8, 16, 48, 64 };
    return c >= 0 && c < 7 && r >= 0 && r < capacities[c] && pc_ui_item_record_id(c, r) != 0;
}

/* The weapon assembled from blade b, grip g and gems (packed as the
 * request's) in the record Assemble takes, described as the Items card
 * describes a weapon; the bag is put back byte for byte after. */
static int assemble_preview(pc_ui_item_details* d, int b, int g, int gems)
{
    static vs_main_inventory_t saved;
    static vs_main_inventoryIndices_t saved_indices;
    int slot = first_empty_weapon();
    if (slot >= 8)
        return 0;
    memcpy(&saved, &vs_main_inventory, sizeof saved);
    memcpy(&saved_indices, &vs_main_inventoryIndices, sizeof saved_indices);
    assemble_weapon(slot, b, g, gems);
    pc_ui_items_describe(PC_UI_IC_WEAPON, slot, d);
    memcpy(&vs_main_inventory, &saved, sizeof saved);
    memcpy(&vs_main_inventoryIndices, &saved_indices, sizeof saved_indices);
    /* the new weapon's name is its blade's until it is named */
    copy_text(d->stats.name, sizeof d->stats.name, pc_ui_item_name(vs_main_inventory.blades[b].id));
    d->inventory = d->stats.inventory = -1;
    return 1;
}

/* A gems value as the requests pack it is valid for weapon / shield `target`
 * (0-based; -1: the weapon being assembled) of `slots` slots: every gem a
 * record that is free or already in this target, none twice. */
static int gems_ok(int gems, int slots, int target, int isShield)
{
    int k, j, g[3];
    for (k = 0; k < 3; ++k) {
        g[k] = (gems >> (k * 8)) & 0xFF;
        if (!g[k])
            continue;
        if (k >= slots || !exists(PC_UI_IC_GEM, g[k] - 1))
            return 0;
        {
            int set = vs_main_inventory.gems[g[k] - 1].setItemIndex;
            if (set != 0 && (target < 0 || set != target + 1 + (isShield << 7)))
                return 0;
        }
        for (j = 0; j < k; ++j)
            if (g[j] == g[k])
                return 0;
    }
    return 1;
}

static void preview(void)
{
    int key[6];
    int kind = pc_ui_shell_workshop_wanted(key);
    pc_ui_item_details* d = &snap.preview;
    int k;
    snap.preview_valid = 0;
    for (k = 0; k < 6; ++k)
        snap.preview_key[k] = kind ? key[k] : 0;
    if (!kind)
        return;
    if (key[0] == PC_UI_WSA_ASSEMBLE) {
        int b = key[1], g = key[2], gems = key[3];
        if (b >= 0 && g >= 0) {
            if (!exists(PC_UI_IC_BLADE, b) || !exists(PC_UI_IC_GRIP, g)
                || !valid_grip(&vs_main_inventory.blades[b], &vs_main_inventory.grips[g])
                || !gems_ok(gems, vs_main_inventory.grips[g].gemSlots, -1, 0) || !assemble_preview(d, b, g, gems))
                return;
            snap.preview_category = PC_UI_IC_WEAPON;
        } else if (b >= 0 && exists(PC_UI_IC_BLADE, b)) {
            pc_ui_items_describe(PC_UI_IC_BLADE, b, d);
            snap.preview_category = PC_UI_IC_BLADE;
        } else if (g >= 0 && exists(PC_UI_IC_GRIP, g)) {
            pc_ui_items_describe(PC_UI_IC_GRIP, g, d);
            snap.preview_category = PC_UI_IC_GRIP;
        } else {
            return;
        }
        snap.preview_valid = 1;
        return;
    }
    if (key[0] == PC_UI_WSA_GEMS) {
        /* the target with the gems as they will be (the bag put back after;
         * not equipped meanwhile: described from the record, as after the
         * re-equip) */
        static vs_main_inventory_t saved;
        int isShield = key[1] != 0, t = key[2], slots;
        u_char* gems;
        if (!exists(isShield ? PC_UI_IC_SHIELD : PC_UI_IC_WEAPON, t))
            return;
        if (isShield) {
            slots = vs_main_inventory.shields[t].base.gemSlots;
            gems = vs_main_inventory.shields[t].gems;
        } else {
            slots = vs_main_inventory.grips[vs_main_inventory.weapons[t].grip - 1].gemSlots;
            gems = vs_main_inventory.weapons[t].gems;
        }
        if (!gems_ok(key[3], slots, t, isShield))
            return;
        memcpy(&saved, &vs_main_inventory, sizeof saved);
        for (k = 0; k < 3 && k < slots; ++k)
            gems[k] = (key[3] >> (k * 8)) & 0xFF;
        if (isShield)
            vs_main_inventory.shields[t].isEquipped = 0;
        else
            vs_main_inventory.weapons[t].isEquipped = 0;
        pc_ui_items_describe(isShield ? PC_UI_IC_SHIELD : PC_UI_IC_WEAPON, t, d);
        memcpy(&vs_main_inventory, &saved, sizeof saved);
        snap.preview_category = isShield ? PC_UI_IC_SHIELD : PC_UI_IC_WEAPON;
        snap.preview_valid = 1;
        return;
    }
    if (key[0] == PC_UI_WSA_COMBINE) {
        int c = key[1], a = key[2], b = key[3];
        static const int cats[3] = { PC_UI_IC_BLADE, PC_UI_IC_SHIELD, PC_UI_IC_ARMOR };
        if (c < 0 || c > 2 || a == b || !exists(cats[c], a) || !exists(cats[c], b) || !combine_result(c, a, b))
            return;
        if (c == 0)
            details_blade(d, &blade_buf);
        else if (c == 1)
            details_shield(d, &shield_buf);
        else
            details_armor(d, &armor_buf);
        snap.preview_category = cats[c];
        snap.preview_valid = 1;
    }
}

/* with_preview 0: the request path's fill (the checks need the bag, not the
 * card's preview; changed() fills with it after a change). */
static void fill_snapshot(int with_preview)
{
    int i, weapons;
    filled = 1;
    fill_statics();
    memcpy(snap.text, statics.text, sizeof snap.text);
    memcpy(snap.grid, statics.grid, sizeof snap.grid);
    memcpy(snap.glyph, statics.glyph, sizeof snap.glyph);
    memcpy(snap.material, statics.material, sizeof snap.material);
    snap.valid = ashley() != NULL;
    {
        /* _workshopMaterials (1055) by vs_battle_inWorkshop (146C.c:16710) */
        static char _workshopMaterials[] = { 0, 0xE, 0x18, 0x38, 0xC0, 0x3E, 0xFE };
        u_char _currentWorkshop = psx_get(vs_battle_roomData.sectionB) != NULL ? vs_battle_inWorkshop() : 0;
        snap.workshop = _currentWorkshop;
        /* Divergence: an id past the table (none in the game's rooms) works nothing */
        snap.materials = _currentWorkshop < sizeof _workshopMaterials ? (u_char)_workshopMaterials[_currentWorkshop] : 0;
    }
    snap.locked = ((u_int)D_800F4EA0 & ITEM_MASK) != 0;
    mask_reason(snap.locked_reason, sizeof snap.locked_reason);
    snap.busy = !resident || pc_ui_equip_busy();
    fill_lists();
    fill_repairs();
    snap.weapon_slot = first_empty_weapon();
    /* vs_menuC_exec state 2 (4322): the commands' greying */
    for (i = 0; i < PC_UI_WS_ACTIONS; ++i) {
        snap.action_enabled[i] = 1;
        snap.action_reason[i] = -1;
    }
    weapons = item_count(0);
    if (weapons == 8) {
        snap.action_enabled[PC_UI_WSA_ASSEMBLE] = 0;
        snap.action_reason[PC_UI_WSA_ASSEMBLE] = VS_MENU12_BIN_INDEX_weaponsFull;
    }
    if (weapons == 0) {
        snap.action_enabled[PC_UI_WSA_RENAME] = 0;
        snap.action_reason[PC_UI_WSA_RENAME] = VS_MENU12_BIN_INDEX_noWeapons;
    }
    if (!snap.workshop) {
        for (i = 3; i != 0;) {
            if (++i == 6) {
                i = 0;
            }
            snap.action_enabled[i] = 0;
            snap.action_reason[i] = VS_MENU12_BIN_INDEX_notInWorkshop;
        }
    }
    if (snap.locked)
        for (i = 0; i < PC_UI_WS_ACTIONS; ++i) {
            snap.action_enabled[i] = 0;
            snap.action_reason[i] = -1;
        }
    if (with_preview)
        preview();
    pc_ui_workshop_set(&snap);
}

static void fill(void) { fill_snapshot(1); }

static int ws_showing(void) { return pc_ui_shell_visible() && pc_ui_shell_workshop_shown(); }

void pc_ui_workshop_fill(int mainmenu)
{
    resident = mainmenu;
    filled = 0;
    shown = ws_showing();
    if (shown)
        fill();
}

void pc_ui_workshop_refresh(void)
{
    if (!ws_showing())
        return;
    if (!filled) {
        fill();
        return;
    }
    {
        /* only the preview follows the shell after its tick */
        int key[6], k, same = 1;
        int kind = pc_ui_shell_workshop_wanted(key);
        for (k = 0; k < 6; ++k)
            same = same && snap.preview_key[k] == (kind ? key[k] : 0);
        if (!same) {
            preview();
            pc_ui_workshop_set(&snap);
        }
    }
}

/* ---- Requests ---- */

/* The position of record r in category c's list (vs_main_inventoryIndices), -1 none. */
static int list_row(int c, int r)
{
    static u_char* const lists[7] = { vs_main_inventoryIndices.weapons, vs_main_inventoryIndices.blades,
        vs_main_inventoryIndices.grips, vs_main_inventoryIndices.shields, vs_main_inventoryIndices.armor,
        vs_main_inventoryIndices.gems, vs_main_inventoryIndices.misc };
    static const u_char capacities[7] = { 8, 16, 16, 8, 16, 48, 64 };
    int i, n = 0;
    for (i = 0; i < capacities[c]; ++i) {
        if (lists[c][i] == 0)
            continue;
        if (lists[c][i] == r + 1)
            return n;
        ++n;
    }
    return -1;
}

static void changed(void)
{
    int c;
    for (c = 0; c < 7; ++c)
        pc_ui_items_rebuild(c);
    fill();
}

/* A weapon disassembled (or about to be): its quick-swap slot cleared first
 * (weapon_slots.c keeps it in the record, the shields keep the slot's bit;
 * as items_bridge.c's discard). */
static void drop_quick_slot(int weapon)
{
    int slot = pc_weapon_slots_get(weapon);
    if (slot)
        pc_weapon_slots_clear(slot);
}

static int do_assemble(const pc_ui_request* r)
{
    int b = r->slot, g = r->item, gems = r->shield, combining;
    if (!snap.action_enabled[PC_UI_WSA_ASSEMBLE] || !exists(PC_UI_IC_BLADE, b) || !exists(PC_UI_IC_GRIP, g)
        || vs_main_inventory.blades[b].assembledWeaponIndex || vs_main_inventory.grips[g].assembledWeaponIndex
        || !valid_grip(&vs_main_inventory.blades[b], &vs_main_inventory.grips[g])
        || !gems_ok(gems, vs_main_inventory.grips[g].gemSlots, -1, 0))
        return 0;
    combining = first_empty_weapon();
    if (combining >= 8 || !pc_ui_item_name_bytes(vs_main_inventory.blades[b].id))
        return 0;
    remember(SETUP_MEMORY, PC_UI_WSA_ASSEMBLE, 0);
    vs_battle_playMenuSelectSfx(); /* _assembleMenu's confirm */
    /* the original's slot menu assembles on every frame (state 2): at least twice */
    assemble_weapon(combining, b, g, gems);
    assemble_weapon(combining, b, g, gems);
    write_name(combining, r->name);
    return 1;
}

/* The gems weapon / shield `target` holds now (packed as the requests'), its
 * slot count in *slots; -1 when the request's target is not one. */
static int target_gems(const pc_ui_request* r, int* slots)
{
    int isShield = r->slot != 0, target = r->item, k, seen = 0;
    if (r->slot < 0 || r->slot > 1 || !exists(isShield ? PC_UI_IC_SHIELD : PC_UI_IC_WEAPON, target))
        return -1;
    if (isShield) {
        *slots = vs_main_inventory.shields[target].base.gemSlots;
        for (k = 0; k < 3 && k < *slots; ++k)
            seen |= vs_main_inventory.shields[target].gems[k] << (k * 8);
    } else {
        vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[target];
        *slots = vs_main_inventory.grips[w->grip - 1].gemSlots;
        for (k = 0; k < 3 && k < *slots; ++k)
            seen |= w->gems[k] << (k * 8);
    }
    return seen;
}

/* _attachGemsMenu confirmed (state 8, Yes). The original attaches every pick
 * at once (state 6: the record changed and an equipped target re-equipped,
 * vs_battle_equipWeapon / Shield, which ends its enchantments: func_8006B214
 * / B270 / B2D4); Yes itself changes nothing more. So: a visit with a pick
 * (PC_UI_WS_EDITED) attaches the final set and re-equips (the same end as
 * the original's last live edit); a visit without one changes nothing, the
 * equipped target is not re-equipped (its enchantments stay); the sound and
 * the memories either way. */
static int do_gems(const pc_ui_request* r)
{
    int isShield = r->slot != 0, target = r->item, slots = 0, k, seen, list;
    char gems[3];
    if (!snap.action_enabled[PC_UI_WSA_GEMS] || (seen = target_gems(r, &slots)) < 0)
        return 0;
    if (slots <= 0 || (r->count >= 0 && r->count != seen) || !gems_ok(r->shield, slots, target, isShield))
        return 0;
    for (k = 0; k < 3; ++k)
        gems[k] = (char)((r->shield >> (k * 8)) & 0xFF);
    list = list_row(isShield ? PC_UI_IC_SHIELD : PC_UI_IC_WEAPON, target);
    remember(SETUP_MEMORY, PC_UI_WSA_GEMS, 0);
    remember(GEMS_MEMORY, isShield, 0);
    if (list >= 0)
        remember(GEMS_MEMORY + 1 + isShield, list, 0);
    vs_main_playSfxDefault(0x7E, 0x18);
    if ((r->flags & PC_UI_WS_EDITED) || r->shield != seen)
        attach_gems(target, gems, isShield);
    return 1;
}

/* Gems' slots left without Yes: the original's _attachGems(targetGems)
 * (168.c:1855 Triangle, 1873 a pick list's Triangle, 1928 Cancel; state 5's
 * Cross the same): the gems as they were when the slots opened (the shell
 * attached nothing, so the record's own) and an equipped target re-equipped.
 * Its leave sound is the shell's (the back step's); nothing else is
 * remembered. Silent and never refused: a target that is gone or not as
 * seen, or a bag the shell may not change now, is left alone. */
static void do_gems_leave(const pc_ui_request* r)
{
    int isShield = r->slot != 0, slots = 0, k, seen;
    char gems[3];
    if (!snap.valid || snap.locked || snap.busy || (seen = target_gems(r, &slots)) < 0 || slots <= 0
        || (r->count >= 0 && r->count != seen))
        return;
    for (k = 0; k < 3; ++k)
        gems[k] = (char)(k < slots ? (seen >> (k * 8)) & 0xFF : 0);
    attach_gems(r->item, gems, isShield);
}

static int do_disassemble(const pc_ui_request* r)
{
    int isShield = r->slot != 0, target = r->item, equipped, list, k, any = 0;
    if (!snap.action_enabled[PC_UI_WSA_DISASSEMBLE] || r->slot < 0 || r->slot > 1
        || !exists(isShield ? PC_UI_IC_SHIELD : PC_UI_IC_WEAPON, target))
        return 0;
    list = list_row(isShield ? PC_UI_IC_SHIELD : PC_UI_IC_WEAPON, target);
    if (list < 0 || (r->shield >= 0 && r->shield != list))
        return 0;
    if (isShield) {
        vs_main_inventoryShield* sh = &vs_main_inventory.shields[target];
        for (k = 0; k < sh->base.gemSlots; ++k)
            any |= sh->gems[k];
        if (!any) /* _disassembleMenu: noGems, unselectable */
            return 0;
        equipped = sh->isEquipped;
    } else {
        equipped = vs_main_inventory.weapons[target].isEquipped;
    }
    if (equipped && !pc_weapon_slots_menu_can_load())
        return 0;
    remember(SETUP_MEMORY, PC_UI_WSA_DISASSEMBLE, 0);
    remember(DISASSEMBLE_MEMORY, isShield, 0);
    remember(DISASSEMBLE_MEMORY + 1 + isShield, list, 0);
    vs_main_playSfxDefault(0x7E, 0x19);
    if (equipped)
        pc_ui_equip_models_mark();
    if (isShield) {
        disassemble_shield(target);
    } else {
        drop_quick_slot(target);
        disassemble_weapon(target);
    }
    if (equipped) {
        if (isShield)
            pc_ui_equip_unequip_shield();
        else
            pc_ui_equip_unequip_weapon();
        pc_ui_equip_models_reload();
    }
    return 1;
}

static int do_rename(const pc_ui_request* r)
{
    int list;
    if (!snap.action_enabled[PC_UI_WSA_RENAME] || !exists(PC_UI_IC_WEAPON, r->item)
        || !pc_ui_item_name_bytes(vs_main_inventory.blades[vs_main_inventory.weapons[r->item].blade - 1].id))
        return 0;
    list = list_row(PC_UI_IC_WEAPON, r->item);
    remember(SETUP_MEMORY, PC_UI_WSA_RENAME, 0);
    if (list >= 0)
        remember(RENAME_MEMORY, list, 0);
    return write_name(r->item, r->name);
}

static int do_repair(void)
{
    if (!snap.action_enabled[PC_UI_WSA_REPAIR])
        return 0;
    remember(SETUP_MEMORY, PC_UI_WSA_REPAIR, 0);
    vs_main_playSfxDefault(0x7E, 0xD);
    repair();
    return 1;
}

static int do_combine(const pc_ui_request* r)
{
    static const int cats[3] = { PC_UI_IC_BLADE, PC_UI_IC_SHIELD, PC_UI_IC_ARMOR };
    int c = r->slot, ids[2], i, unload = 0, armor_count = 0;
    if (!snap.action_enabled[PC_UI_WSA_COMBINE] || c < 0 || c > 2 || r->item == r->shield
        || !exists(cats[c], r->item) || !exists(cats[c], r->shield))
        return 0;
    ids[0] = r->item + 1;
    ids[1] = r->shield + 1;
    /* the lists' checks: the material the workshop works; armor: no accessory */
    for (i = 0; i < 2; ++i) {
        int material = c == 0 ? vs_main_inventory.blades[ids[i] - 1].material
            : c == 1          ? vs_main_inventory.shields[ids[i] - 1].base.material
                              : vs_main_inventory.armor[ids[i] - 1].material;
        if (!material_ok(material) || (c == 2 && vs_main_inventory.armor[ids[i] - 1].category == 7))
            return 0;
        if (c == 0) {
            int w = vs_main_inventory.blades[ids[i] - 1].assembledWeaponIndex;
            unload |= w >= 1 && w <= 8 && vs_main_inventory.weapons[w - 1].isEquipped;
        } else if (c == 1) {
            unload |= vs_main_inventory.shields[ids[i] - 1].isEquipped != 0;
        }
    }
    /* _combineTopMenu: fewer than two in the category greys it */
    for (i = 0; i < 16; ++i)
        armor_count += vs_main_inventory.armor[i].id != 0 && vs_main_inventory.armor[i].category != 7;
    if ((c == 2 ? armor_count : item_count(cats[c])) < 2)
        return 0;
    if (unload && !pc_weapon_slots_menu_can_load())
        return 0;
    if (!combine_result(c, ids[0] - 1, ids[1] - 1))
        return 0;
    remember(SETUP_MEMORY, PC_UI_WSA_COMBINE, 0);
    remember(COMBINE_MEMORY, c, 0);
    vs_main_playSfxDefault(0x7E, 0x18);
    if (unload)
        pc_ui_equip_models_mark();
    switch (c) {
    case 0: { /* _combineBladeMenu state 5, Yes (3068) */
        vs_main_inventoryBlade* blade;
        int var_s1, itemId;
        for (i = 0; i < 2; ++i) {

            blade = &vs_main_inventory.blades[ids[i] - 1];
            var_s1 = blade->assembledWeaponIndex;

            if (var_s1 != 0) {
                if (vs_main_inventory.weapons[var_s1 - 1].isEquipped != 0) {
                    pc_ui_equip_unequip_weapon();
                }
                drop_quick_slot(var_s1 - 1);
                disassemble_weapon(var_s1 - 1);
            }

            pc_inv_init_item(PC_UI_IC_BLADE, ids[i]);
        }

        itemId = ids[0];
        blade = &vs_main_inventory.blades[itemId - 1];

        vs_battle_copyAligned(blade, &blade_buf, sizeof *blade);

        blade->index = itemId;

        pc_ui_items_rebuild(1);
        break;
    }
    case 1: { /* _combineShieldMenu state 5, Yes (3663) */
        vs_main_inventoryShield* shield1;
        int temp_s1, itemId_3, temp_s2;
        for (i = 0; i < 2; ++i) {

            temp_s1 = ids[i];
            itemId_3 = temp_s1 - 1;

            if (vs_main_inventory.shields[itemId_3].isEquipped != 0) {
                pc_ui_equip_unequip_shield();
            }

            disassemble_shield(itemId_3);
            pc_inv_init_item(PC_UI_IC_SHIELD, temp_s1);
        }

        temp_s2 = ids[0];
        shield1 = &vs_main_inventory.shields[temp_s2 - 1];

        vs_battle_copyAligned(shield1, &shield_buf, 0x30);

        shield1->index = temp_s2;

        pc_ui_items_rebuild(3);
        break;
    }
    default: { /* _combineArmorMenu state 5, Yes (4151) */
        vs_main_inventoryArmor* armor;
        int var_s1, itemId;
        for (i = 0; i < 2; ++i) {
            armor = &vs_main_inventory.armor[ids[i] - 1];
            var_s1 = armor->limb;
            if (var_s1 != 0) {
                vs_battle_equipArmor(var_s1 - 1, NULL);
                armor->limb = 0;
            }
            pc_inv_init_item(PC_UI_IC_ARMOR, ids[i]);
        }
        itemId = ids[0];
        armor = &vs_main_inventory.armor[itemId - 1];
        vs_battle_copyAligned(armor, &armor_buf, sizeof *armor);
        armor->index = itemId;
        pc_ui_items_rebuild(4);
        break;
    }
    }
    if (unload)
        pc_ui_equip_models_reload();
    return 1;
}

/* Returns 1 when the request was one of ours. */
int pc_ui_workshop_request(const pc_ui_request* r)
{
    int ok = 0;
    if (r->kind < PC_UI_REQ_WS_ASSEMBLE || r->kind > PC_UI_REQ_WS_GEMS_LEAVE)
        return 0;
    if (r->kind == PC_UI_REQ_WS_SOUND) {
        if (r->slot == 0)
            vs_battle_playMenuLeaveSfx();
        else if (r->slot == 1)
            vs_battle_playMenuSelectSfx();
        else
            vs_battle_playInvalidSfx();
        return 1;
    }
    /* the snapshot as the game is now (an earlier request of this tick may have
     * changed it); the preview stays as it was until changed() */
    fill_snapshot(0);
    if (r->kind == PC_UI_REQ_WS_GEMS_LEAVE) {
        do_gems_leave(r);
        changed();
        return 1;
    }
    /* nothing changes while the bag is locked, MAINMENU is not resident, a model reload runs, or the menu
     * ends on this tick for a hand-off */
    if (snap.valid && !snap.locked && !snap.busy && !pc_ui_items_cast_taken() && !pc_ui_abilities_cast_taken()
        && !pc_ui_magick_cast_taken()) {
        switch (r->kind) {
        case PC_UI_REQ_WS_ASSEMBLE:
            ok = do_assemble(r);
            break;
        case PC_UI_REQ_WS_GEMS:
            ok = do_gems(r);
            break;
        case PC_UI_REQ_WS_DISASSEMBLE:
            ok = do_disassemble(r);
            break;
        case PC_UI_REQ_WS_RENAME:
            ok = do_rename(r);
            break;
        case PC_UI_REQ_WS_REPAIR:
            ok = do_repair();
            break;
        case PC_UI_REQ_WS_COMBINE:
            ok = do_combine(r);
            break;
        }
    }
    if (!ok) {
        vs_battle_playInvalidSfx();
        pc_ui_workshop_refused();
        return 1;
    }
    changed();
    return 1;
}

/* Tests (RPC ui_menu {reequip}): the equipped weapon and shield re-equipped
 * from their records through the game's own path (vs_battle_equipWeapon /
 * Shield), so a record a test prepared (gem slots) and Ashley's battle
 * record agree before a menu opens (MAINMENU's vs_mainMenu_initInventory
 * copies the battle record back over the bag's). Only with no menu open.
 * Returns how many were re-equipped, -1 refused. */
int pc_ui_workshop_debug_reequip(void)
{
    int i, n = 0;
    if (ashley() == NULL || (vs_battle_menuState.currentMenu & 0x3F) != 0x3F)
        return -1;
    for (i = 0; i < 8; ++i) {
        if (vs_main_inventory.weapons[i].blade != 0 && vs_main_inventory.weapons[i].isEquipped != 0) {
            vs_battle_equipWeapon(&vs_main_inventory.weapons[i]);
            ++n;
        }
        if (vs_main_inventory.shields[i].base.id != 0 && vs_main_inventory.shields[i].isEquipped != 0) {
            vs_battle_equipShield(&vs_main_inventory.shields[i]);
            ++n;
        }
    }
    return n;
}
