/* Modern menus: inventory operations the native screens share (battle
 * builds), ported line for line from the original menus so the bag ends as
 * the original screen leaves it. Categories as PC_UI_IC_* / the game's
 * itemCategory* (weapons 0 .. misc 6). Game headers: include after common.h
 * and func_8006B57C_t.h (vs_main_inventory_t). */
#ifndef ROOD_UI_INVENTORY_OPS_H
#define ROOD_UI_INVENTORY_OPS_H

/* MAINMENU vs_mainMenu_initItem (2D10.c:1043) on vs_main_inventory: record
 * `index` (1-based, as the original takes it) zeroed but for its own index;
 * a weapon's blade, grip and gems first, a shield's gems. */
void pc_inv_init_item(int itemCategory, int index);

/* The data half of taking a weapon apart (MENUB 260.c _disassembleItem's
 * disassemble state, 369; MENUC 168.c _disassembleWeapon, 281, does the same
 * before it clears the weapon's own record): in `inventory`, weapon `weapon`
 * (0-based)'s blade and grip no longer name it (assembledWeaponIndex 0) and
 * each gem in its grip's slots is taken out (the weapon's gem index 0, the
 * gem's setItemIndex 0). The weapon record itself is left as it is. */
void pc_inv_release_weapon_parts(vs_main_inventory_t* inventory, int weapon);

/* The same for shield `shield` (0-based): every gem in its slots taken out
 * (MENUB _disassembleItem, MENUC _disassembleShield, 344). */
void pc_inv_release_shield_gems(vs_main_inventory_t* inventory, int shield);

#endif
