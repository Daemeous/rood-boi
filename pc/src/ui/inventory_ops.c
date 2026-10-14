/* Modern menus: shared inventory operations (inventory_ops.h; battle builds).
 * The ports keep the originals' types and expressions (the bag ends byte for
 * byte as the original screen leaves it: ui_items_smoke, ui_loot_smoke). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "inventory_ops.h"
#include "ui_model.h"

/* MAINMENU vs_mainMenu_initItem (2D10.c:1043). */
void pc_inv_init_item(int itemCategory, int index)
{
    int i;

    switch (itemCategory) {
    case PC_UI_IC_WEAPON: {
        vs_main_inventoryWeapon* weapon = &vs_main_inventory.weapons[index - 1];

        /* Divergence: a blade or grip index 0 (a corrupt save; never in a
         * valid one) is skipped, MAINMENU would clear the record before the
         * first. */
        if (weapon->blade != 0) {
            pc_inv_init_item(PC_UI_IC_BLADE, weapon->blade);
        }
        if (weapon->grip != 0) {
            pc_inv_init_item(PC_UI_IC_GRIP, weapon->grip);
        }

        for (i = 0; i < 3; ++i) {
            int gem = weapon->gems[i];

            if (gem != 0) {
                pc_inv_init_item(PC_UI_IC_GEM, gem);
            }
        }

        vs_battle_rMemzero(weapon, sizeof *weapon);

        weapon->index = index;
        break;
    }

    case PC_UI_IC_BLADE: {
        vs_main_inventoryBlade* blade = &vs_main_inventory.blades[index - 1];

        vs_battle_rMemzero(blade, sizeof *blade);

        blade->index = index;
        break;
    }

    case PC_UI_IC_GRIP: {
        vs_main_inventoryGrip* grip = &vs_main_inventory.grips[index - 1];

        vs_battle_rMemzero(grip, sizeof *grip);

        grip->index = index;
        break;
    }

    case PC_UI_IC_SHIELD: {
        vs_main_inventoryShield* shield = &vs_main_inventory.shields[index - 1];

        for (i = 0; i < 3; ++i) {
            int gem = shield->gems[i];

            if (gem != 0) {
                pc_inv_init_item(PC_UI_IC_GEM, gem);
            }
        }

        vs_battle_rMemzero(shield, sizeof *shield);

        shield->index = index;
        break;
    }

    case PC_UI_IC_ARMOR: {
        vs_main_inventoryArmor* armor = &vs_main_inventory.armor[index - 1];

        vs_battle_rMemzero(armor, sizeof *armor);

        armor->index = index;
        break;
    }

    case PC_UI_IC_GEM: {
        vs_main_inventoryGem* gem = &vs_main_inventory.gems[index - 1];

        vs_battle_rMemzero(gem, sizeof *gem);

        gem->index = index;
        break;
    }

    case PC_UI_IC_MISC: {
        vs_main_inventoryMisc* misc = &vs_main_inventory.misc[index - 1];

        vs_battle_rMemzero(misc, sizeof *misc);

        misc->index = index;
        break;
    }
    }
}

/* MENUB 260.c _disassembleItem, state disassemble (378-397), the weapon
 * branch (MENUC 168.c _disassembleWeapon 281 takes the gems out first and
 * the blade and grip after: the same fields, the same values). */
void pc_inv_release_weapon_parts(vs_main_inventory_t* inventory, int weapon_index)
{
    int i;
    vs_main_inventoryWeapon* weapon = &inventory->weapons[weapon_index];

    inventory->blades[weapon->blade - 1].assembledWeaponIndex = 0;
    inventory->grips[weapon->grip - 1].assembledWeaponIndex = 0;

    for (i = 0; i < inventory->grips[weapon->grip - 1].gemSlots; ++i) {
        int gem = weapon->gems[i];
        if (gem != 0) {
            weapon->gems[i] = 0;
            inventory->gems[gem - 1].setItemIndex = 0;
        }
    }
}

/* MENUB 260.c _disassembleItem, the shield branch (377-385). */
void pc_inv_release_shield_gems(vs_main_inventory_t* inventory, int shield_index)
{
    int i;
    vs_main_inventoryShield* shield = &inventory->shields[shield_index];

    for (i = 0; i < shield->base.gemSlots; ++i) {
        int gem = shield->gems[i];
        if (gem != 0) {
            shield->gems[i] = 0;
            inventory->gems[gem - 1].setItemIndex = 0;
        }
    }
}
