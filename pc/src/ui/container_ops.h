/* Modern menus, the native Container screen: MENUD's data operations
 * (src/MENU/MENUD.PRG/234.c) ported with their types and expressions, in
 * the original's order, on the original's data (MENU7's
 * vs_menu_inventoryStorage, passed in and read fresh on every call; nothing
 * is cached across frames). Each function names its original and line.
 *
 * Pure C: no game functions and no game globals (the bridge passes them in
 * a pc_ctr), so pc/tests/container_ops_probe.c runs it without the game.
 * Game types: include after the u_char / u_short typedefs (common.h, or
 * <sys/types.h> in the probe) and BATTLE.PRG/func_8006B57C_t.h. Built with
 * -funsigned-char as the game is (a misc count is a char). */
#ifndef ROOD_UI_CONTAINER_OPS_H
#define ROOD_UI_CONTAINER_OPS_H

#include "container_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PC_CONTAINER_OPS_PROBE
/* the probe has no main.h: the bag's layout (main.h vs_main_inventory_t, 0xF00 bytes) */
typedef struct {
    vs_main_inventoryWeapon weapons[8];
    vs_main_inventoryShield shields[8];
    vs_main_inventoryBlade blades[16];
    vs_main_inventoryGrip grips[16];
    vs_main_inventoryArmor armor[16];
    vs_main_inventoryGem gems[48];
    vs_main_inventoryMisc misc[64];
} vs_main_inventory_t;
#endif

enum { PC_CTR_CATEGORIES = 7, PC_CTR_TRANSFER = 64, PC_CTR_STACK = 100 };

/* MENUD's statics and the game globals its operations read, as the bridge
 * has them this call. */
typedef struct pc_ctr {
    vs_menu_inventoryStorage_t* st; /* vs_menu_inventoryStorage */
    vs_menu_container* src;         /* vs_menuD_containerData (the list's side) */
    vs_menu_container* dst;         /* D_80109A88 (the other side) */
    int mode;                       /* D_80109A7A: 0 Remove (container to bag), 1 Store */
    const u_char* bag_caps;         /* vs_mainMenu_inventoryItemCapacities {8,16,16,8,16,48,64} */
    u_char* const* bag_indices;     /* vs_mainMenu_inventoryIndices[7] (vs_main_inventoryIndices) */
    vs_main_inventory_t* bag;       /* &vs_main_inventory */
    /* the sorts' stat of a record (vs_menuD_initUiWeapon / _initUiShield and the
     * vs_battle_copyInventory*Stats the original sorts read through) */
    void* user;
    int (*weapon_stat)(void* user, vs_menu_containerData* c, int slot, int stat); /* _getWeaponStat 1279 */
    int (*shield_stat)(void* user, vs_menu_containerData* c, int slot, int stat); /* _getShieldStatValue 1361 */
    int (*equip_stat)(void* user, vs_menu_containerData* c, int category, int slot, int stat); /* 1489 */
} pc_ctr;

/* _containerItemCapacities (83): {32, 64, 64, 32, 64, 192, 256, 2}. */
extern const u_short pc_ctr_caps[8];

/* 85-120 */
u_short* pc_ctr_indices(int category, vs_menu_container* container);
int pc_ctr_index_of(int category, int index, u_short* indices);
void pc_ctr_index_add(int category, int index, u_short* indices);
/* 121: a record cleared (weapons and shields with their parts), index = slot + 1 */
void pc_ctr_init_object(int category, int index, vs_menu_containerData* container);
/* 159, 191, 207 */
int pc_ctr_item_id(int category, int index, vs_menu_containerData* container);
int pc_ctr_count(int category, vs_menu_containerData* container);
int pc_ctr_empty_slot(int category, vs_menu_containerData* container);
/* 283: flags & 0xF the category, 0x10 copy (else a test), flags >> 8 the
 * parent; the new slot + 1, 1 for a misc that merged whole, 0 no room. */
enum { PC_CTR_COPY = 0x10 };
int pc_ctr_copy_item(int flags, vs_menu_containerData* target, int slot, vs_menu_containerData* source);
/* 498 */
void pc_ctr_defragment(int category, vs_menu_container* container);
/* 555 / 592: the bag widened into st->unk105B0, and back */
void pc_ctr_bag_to_container(const pc_ctr* c);
void pc_ctr_container_to_bag(const pc_ctr* c);
/* 625 / 645 */
int pc_ctr_parent(int category, int index, vs_menu_containerData* container);
int pc_ctr_set_shield(int category, int index, vs_menu_containerData* container);

/* Staging. 259 / 276: the destination's misc with the staged misc poured in
 * (st->unk4BB0), and its count (D_80109A82, returned). */
void pc_ctr_misc_sim(const pc_ctr* c);
int pc_ctr_misc_count(const pc_ctr* c);
/* 1989, 2001: 1 the transfer list would pass 64 entries, 2 the destination
 * has no room, 0 fits */
int pc_ctr_over(const pc_ctr* c, int category, int n);
int pc_ctr_fits(const pc_ctr* c, int category, int slot);
/* 1963 / 1976 */
int pc_ctr_weapon_parts(const pc_ctr* c, int slot);
int pc_ctr_shield_parts(const pc_ctr* c, int slot);
/* 2043: transfer entry `entry` back to the source; 2057: slot staged */
void pc_ctr_unstage(const pc_ctr* c, int entry);
void pc_ctr_stage(const pc_ctr* c, int category, int slot);
/* 2072, 2083, 2091 */
int pc_ctr_transfer_find(const pc_ctr* c, int category, int slot);
void pc_ctr_transfer_add(const pc_ctr* c, int category, int slot);
void pc_ctr_transfer_clean(const pc_ctr* c);
int pc_ctr_transfer_count(const pc_ctr* c);
/* func_80107490 state 15 YES (3010-3026): every staged entry into the
 * destination, then the destination defragmented */
void pc_ctr_commit(const pc_ctr* c);
/* state 6 (2740-2766): rows a and b (1-based entries of the source's list of
 * `category`): two misc stacks of one item merge, anything else swaps */
void pc_ctr_merge_or_swap(const pc_ctr* c, int category, int row_a, int row_b);

/* func_80107490 state 8 (2693): the action menu of row `row` (1-based) of
 * tab `tab` (0..6). Entries in order: kind (PC_CTR_ACT_*), enabled, reason
 * (a MENUD menuText offset when menu_text, else an ITEMHELP index; -1 none). */
enum { PC_CTR_ACT_TRANSFER = 1, PC_CTR_ACT_SEARCH, PC_CTR_ACT_DISCARD, PC_CTR_ACT_DISCARD_ALL, PC_CTR_ACT_SORT };
typedef struct {
    int count;
    int kind[5];
    int enabled[5];
    int reason[5];     /* -1: none (the action's own description) */
    int reason_menu[5]; /* reason is a menuText offset (MENUD) rather than an ITEMHELP index */
} pc_ctr_actions;
void pc_ctr_build_actions(const pc_ctr* c, int tab, int row, pc_ctr_actions* out);

/* Sorts (_displaySortMenu 1607): the category's stat list, then a sort by entry i. */
int pc_ctr_sort_stats(int category, const char** stats);
void pc_ctr_sort(const pc_ctr* c, int category, int stat);

/* _discardMenu's YES (1773, 1910): type 0 / 2 the record cleared, type 1
 * `amount` taken from the stack (cleared when it is the whole stack). */
void pc_ctr_discard(const pc_ctr* c, int category, int slot, int type, int amount);

/* func_80106C64 (2148): the rows of tab `tab` (0..6 the source's category,
 * 7 the transfer list): per row the category, slot and its flags
 * (PC_CTR_ROW_*). Returns the row count. */
/* (PC_CTR_ROW_* in container_frame.h) */
int pc_ctr_rows(const pc_ctr* c, int tab, int* category, int* slot, int* flags, int max);

#ifdef __cplusplus
}
#endif
#endif
