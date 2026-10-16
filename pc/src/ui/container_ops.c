/* Modern menus, the native Container screen: MENUD's data operations
 * (container_ops.h). Ported from src/MENU/MENUD.PRG/234.c with its types
 * and expressions, in its order; line numbers are 234.c's. The game's
 * vs_battle_rMemzero / vs_battle_memcpy / vs_battle_copyAligned are byte
 * copies: zero() and copy() here. */
#ifdef PC_CONTAINER_OPS_PROBE
#include <sys/types.h>
#include "../../../src/BATTLE/BATTLE.PRG/func_8006B57C_t.h"
#else
#include "common.h"
#include "../../../src/SLUS_010.40/main.h"
#endif
#include "container_ops.h"

/* the game's char is unsigned (-funsigned-char): a misc count of 165 is 165 */
typedef char pc_ctr_unsigned_char[(char)-1 > 0 ? 1 : -1];

static void zero(void* p, int n)
{
    unsigned char* b = p;
    while (n-- > 0)
        *b++ = 0;
}

static void copy(void* dst, const void* src, int n)
{
    unsigned char* d = dst;
    const unsigned char* s = src;
    while (n-- > 0)
        *d++ = *s++;
}

const u_short pc_ctr_caps[8] = { 32, 64, 64, 32, 64, 192, 256, 2 }; /* _containerItemCapacities 83 */

u_short* pc_ctr_indices(int itemCategory, vs_menu_container* container) /* _getContainerIndicesOffset 85 */
{
    int i;
    int offset;

    offset = 0;
    for (i = 0; i < itemCategory; ++i) {
        offset += pc_ctr_caps[i];
    }
    return (u_short*)&container->indices + offset;
}

int pc_ctr_index_of(int itemCategory, int index, u_short* indices) /* _getContainerItemIndex 97 */
{
    int i;

    for (i = 0; i < pc_ctr_caps[itemCategory]; ++i) {
        if (indices[i] == (index + 1)) {
            return i + 1;
        }
    }
    return 0;
}

void pc_ctr_index_add(int itemCategory, int index, u_short* indices) /* _setContainerItemIndex 109 */
{
    int i;

    for (i = 0; i < pc_ctr_caps[itemCategory]; ++i) {
        if (indices[i] == 0) {
            indices[i] = index + 1;
            return;
        }
    }
}

void pc_ctr_init_object(int itemCategory, int index, vs_menu_containerData* container) /* _initContainerObject 121 */
{
    int i;

    switch (itemCategory) {
    case itemCategoryWeapon: {
        vs_main_inventoryWeapon* weapon = &container->weapons[index];

        pc_ctr_init_object(1, weapon->blade - 1, container);
        pc_ctr_init_object(2, weapon->grip - 1, container);

        for (i = 0; i < 3; ++i) {
            int gem = weapon->gems[i];
            if (gem != 0) {
                pc_ctr_init_object(itemCategoryGem, gem - 1, container);
            }
        }

        zero(weapon, sizeof *weapon);
        weapon->index = index + 1;
        break;
    }
    case itemCategoryBlade: {
        vs_main_inventoryBlade* blade = &container->blades[index];
        zero(blade, sizeof *blade);
        blade->index = index + 1;
        break;
    }
    case itemCategoryGrip: {
        vs_main_inventoryGrip* grip = &container->grips[index];
        zero(grip, sizeof *grip);
        grip->index = index + 1;
        break;
    }
    case itemCategoryShield: {
        vs_main_inventoryShield* shield = &container->shields[index];

        for (i = 0; i < 3; ++i) {
            int gem = shield->gems[i];
            if (gem != 0) {
                pc_ctr_init_object(itemCategoryGem, gem - 1, container);
            }
        }

        zero(shield, sizeof *shield);
        shield->index = index + 1;
        break;
    }
    case itemCategoryArmor: {
        vs_main_inventoryArmor* armor = &container->armor[index];
        zero(armor, sizeof *armor);
        armor->index = index + 1;
        break;
    }
    case itemCategoryGem: {
        vs_main_inventoryGem* gem = &container->gems[index];
        zero(gem, sizeof *gem);
        gem->index = index + 1;
        break;
    }
    case itemCategoryMisc: {
        vs_main_inventoryMisc* item = &container->misc[index];
        zero(item, sizeof *item);
        item->index = index + 1;
        break;
    }
    }
}

int pc_ctr_item_id(int type, int index, vs_menu_containerData* container) /* _getContainerItemId 159 */
{
    int ret = 0;

    switch (type) {
    case itemCategoryWeapon:
        ret = container->weapons[index].blade;
        break;
    case itemCategoryBlade:
        ret = container->blades[index].id;
        break;
    case itemCategoryGrip:
        ret = container->grips[index].id;
        break;
    case itemCategoryShield:
        ret = container->shields[index].base.id;
        break;
    case itemCategoryArmor:
        ret = container->armor[index].id;
        break;
    case itemCategoryGem:
        ret = container->gems[index].id;
        break;
    case itemCategoryMisc:
        ret = container->misc[index].id;
        break;
    }
    return ret;
}

int pc_ctr_count(int itemCategory, vs_menu_containerData* container) /* _countContainerItems 191 */
{
    int i;
    int count = 0;

    for (i = 0; i < pc_ctr_caps[itemCategory]; ++i) {
        if (pc_ctr_item_id(itemCategory, i, container) != 0) {
            ++count;
        }
    }
    return count;
}

int pc_ctr_empty_slot(int itemCategory, vs_menu_containerData* container) /* _getEmptyContainerSlot 207 */
{
    int i;

    for (i = 0; i < pc_ctr_caps[itemCategory]; ++i) {
        if (pc_ctr_item_id(itemCategory, i, container) == 0) {
            break;
        }
    }
    return i;
}

/* _copyContainerItem 283 */
int pc_ctr_copy_item(int itemCategory, vs_menu_containerData* targetContainer, int sourceSlot,
    vs_menu_containerData* sourceContainer)
{
    int copyFlag;
    int targetSlot;
    int parentIndex;
    int itemCount;
    int i;
    int subItemCount;
    int index;

    parentIndex = itemCategory >> 8;
    copyFlag = (itemCategory >> 4) & 1;
    itemCategory = itemCategory & 0xF;
    index = 0;
    itemCount = pc_ctr_count(itemCategory, targetContainer);
    targetSlot = pc_ctr_empty_slot(itemCategory, targetContainer);

    switch (itemCategory) {
    case itemCategoryWeapon: {
        vs_main_inventoryWeapon* source = &sourceContainer->weapons[sourceSlot];
        vs_main_inventoryWeapon* target = &targetContainer->weapons[targetSlot];

        if (((itemCount != 32) && (pc_ctr_count(itemCategoryBlade, targetContainer) != 64))
            && (pc_ctr_count(itemCategoryGrip, targetContainer) != 64)) {

            subItemCount = 0;

            for (i = 0; i < 3; ++i) {
                if (source->gems[i] != 0) {
                    ++subItemCount;
                }
            }

            if ((pc_ctr_count(itemCategoryGem, targetContainer) + subItemCount) > 192) {
                break;
            }

            index = targetSlot + 1;

            if (copyFlag != 0) {
                zero(target, sizeof *target);
                target->index = index;
                target->blade = pc_ctr_copy_item((index << 8) | PC_CTR_COPY | itemCategoryBlade, targetContainer,
                    source->blade - 1, sourceContainer);
                target->grip = pc_ctr_copy_item((index << 8) | PC_CTR_COPY | itemCategoryGrip, targetContainer,
                    source->grip - 1, sourceContainer);
                /* 3 gem bytes: gems[3] is the QoL weapon slots' byte (weapon_slots.c), never a gem */
                for (i = 0; i < 3; ++i) {
                    if (source->gems[i] != 0) {
                        target->gems[i] = pc_ctr_copy_item((index << 8) | PC_CTR_COPY | itemCategoryGem,
                            targetContainer, source->gems[i] - 1, sourceContainer);
                    }
                }
                copy(target->name, source->name, sizeof target->name);
            }
        }
        break;
    }
    case itemCategoryBlade: {
        vs_main_inventoryBlade* source = &sourceContainer->blades[sourceSlot];
        vs_main_inventoryBlade* target = &targetContainer->blades[targetSlot];

        if (itemCount == 64) {
            break;
        }

        index = targetSlot + 1;

        if (copyFlag != 0) {
            copy(target, source, sizeof *target);
            target->index = index;
            target->assembledWeaponIndex = parentIndex;
        }
        break;
    }
    case itemCategoryGrip: {
        vs_main_inventoryGrip* source = &sourceContainer->grips[sourceSlot];
        vs_main_inventoryGrip* target = &targetContainer->grips[targetSlot];

        if (itemCount == 64) {
            break;
        }

        index = targetSlot + 1;

        if (copyFlag != 0) {
            copy(target, source, sizeof *target);
            target->index = index;
            target->assembledWeaponIndex = parentIndex;
        }
        break;
    }
    case itemCategoryShield: {
        vs_main_inventoryShield* source = &sourceContainer->shields[sourceSlot];
        vs_main_inventoryShield* target = &targetContainer->shields[targetSlot];

        if (itemCount == 32) {
            break;
        }

        subItemCount = 0;

        for (i = 0; i < 3; ++i) {
            if (source->gems[i] != 0) {
                ++subItemCount;
            }
        }

        if ((pc_ctr_count(itemCategoryGem, targetContainer) + subItemCount) > 192) {
            break;
        }

        index = targetSlot + 1;

        if (copyFlag != 0) {
            zero(target, sizeof *target);
            copy(&target->base, &source->base, sizeof target->base);
            target->index = index;
            for (i = 0; i < 3; ++i) {
                if (source->gems[i] != 0) {
                    target->gems[i] = pc_ctr_copy_item(((index | gemTargetShield) << 8) | PC_CTR_COPY
                            | itemCategoryGem,
                        targetContainer, source->gems[i] - 1, sourceContainer);
                }
            }
        }
        break;
    }
    case itemCategoryArmor: {
        vs_main_inventoryArmor* source = &sourceContainer->armor[sourceSlot];
        vs_main_inventoryArmor* target = &targetContainer->armor[targetSlot];

        if (itemCount == 64) {
            break;
        }

        index = targetSlot + 1;

        if (copyFlag != 0) {
            copy(target, source, sizeof *target);
            target->index = index;
        }
        break;
    }
    case itemCategoryGem: {
        vs_main_inventoryGem* source = &sourceContainer->gems[sourceSlot];
        vs_main_inventoryGem* target = &targetContainer->gems[targetSlot];

        if (itemCount == 192) {
            break;
        }

        index = targetSlot + 1;

        if (copyFlag != 0) {
            copy(target, source, sizeof *target);
            target->index = index;
            target->setItemIndex = parentIndex;
        }
        break;
    }
    case itemCategoryMisc: {
        vs_main_inventoryMisc* target = targetContainer->misc;
        vs_main_inventoryMisc* source = &sourceContainer->misc[sourceSlot];

        subItemCount = source->count;

        for (i = 0; i < 256; ++i) {
            if (target[i].id == source->id) {
                if (copyFlag != 0) {
                    if ((100 - target[i].count) >= subItemCount) {
                        target[i].count = target[i].count + subItemCount;
                        subItemCount = 0;
                    } else {
                        int v0 = subItemCount - 100;
                        subItemCount = v0 + target[i].count;
                        target[i].count = 100;
                    }
                } else {
                    int v0 = subItemCount - 100;
                    subItemCount = v0 + target[i].count;
                    if (subItemCount < 0) {
                        subItemCount = 0;
                    }
                }
            }
        }

        if (subItemCount == 0) {
            index = 1;
            break;
        }
        if (itemCount == 256) {
            break;
        }

        index = targetSlot + 1;

        if (copyFlag != 0) {
            target[targetSlot].id = source->id;
            target[targetSlot].count = subItemCount;
        }
        break;
    }
    }
    return index;
}

void pc_ctr_defragment(int itemCategory, vs_menu_container* container) /* _defragmentContainerItems 498 */
{
    int i;
    int j;
    int var_a1;
    int source;
    int capacity;
    u_short* indexOffset;

    indexOffset = pc_ctr_indices(itemCategory, container);
    capacity = pc_ctr_caps[itemCategory];

    for (i = 0; i < capacity; ++i) {
        source = indexOffset[i];
        if ((source != 0) && (pc_ctr_item_id(itemCategory, source - 1, &container->data) == 0)) {
            indexOffset[i] = 0;
        }
    }

    i = 0;

    while (1) {
        if (indexOffset[i] != 0) {
            ++i;
            if (i == (capacity - 1)) {
                break;
            }
        } else {
            for (var_a1 = i + 1; var_a1 < capacity; ++var_a1) {
                if (indexOffset[var_a1] != 0) {
                    break;
                }
            }

            if (var_a1 == capacity) {
                break;
            }

            for (j = var_a1; j < capacity; ++j) {
                indexOffset[(j + i) - var_a1] = indexOffset[j];
            }

            for (j = j + (i - var_a1); j < capacity; ++j) {
                indexOffset[j] = 0;
            }
        }
    }

    for (i = 0; i < capacity; ++i) {
        if ((pc_ctr_item_id(itemCategory, i, &container->data) != 0)
            && (pc_ctr_index_of(itemCategory, i, indexOffset) == 0)) {
            pc_ctr_index_add(itemCategory, i, indexOffset);
        }
    }
}

void pc_ctr_bag_to_container(const pc_ctr* c) /* _copyInventoryToContainer 555 */
{
    int i;

    vs_main_inventory_t* inventory = c->bag;
    vs_menu_container* container = &c->st->unk105B0;

    zero(&container->data, sizeof container->data);
    zero(&c->st->unk105B0.indices, sizeof c->st->unk105B0.indices);

    for (i = 0; i < 7; ++i) {
        int j;
        u_char* sourceIndex = c->bag_indices[i];
        u_short* targetIndex = pc_ctr_indices(i, container);

        for (j = 0; j < c->bag_caps[i]; ++j) {
            targetIndex[j] = sourceIndex[j];
        }
    }

    copy(container->data.weapons, inventory->weapons, sizeof container->data.weapons / 4);
    copy(container->data.blades, inventory->blades, sizeof container->data.blades / 4);
    copy(container->data.grips, inventory->grips, sizeof container->data.grips / 4);
    copy(container->data.shields, inventory->shields, sizeof container->data.shields / 4);
    copy(container->data.armor, inventory->armor, sizeof container->data.armor / 4);
    copy(container->data.gems, inventory->gems, sizeof container->data.gems / 4);
    copy(container->data.misc, inventory->misc, sizeof container->data.misc / 4);
}

void pc_ctr_container_to_bag(const pc_ctr* c) /* _copyContainerToInventory 592 */
{
    int i;

    vs_menu_container* container = &c->st->unk105B0;
    vs_main_inventory_t* inventory = c->bag;

    for (i = 0; i < 7; ++i) {
        int j;
        int k;
        u_char* targetIndex = c->bag_indices[i];
        u_short* sourceIndex = pc_ctr_indices(i, container);

        for (j = 0; j < 7; ++j) {
            pc_ctr_defragment(j, container);
        }

        for (k = 0; k < c->bag_caps[i]; ++k) {
            targetIndex[k] = sourceIndex[k];
        }
    }

    copy(inventory->weapons, container->data.weapons, sizeof inventory->weapons);
    copy(inventory->blades, container->data.blades, sizeof inventory->blades);
    copy(inventory->grips, container->data.grips, sizeof inventory->grips);
    copy(inventory->shields, container->data.shields, sizeof inventory->shields);
    copy(inventory->armor, container->data.armor, sizeof inventory->armor);
    copy(inventory->gems, container->data.gems, sizeof inventory->gems);
    copy(inventory->misc, container->data.misc, sizeof inventory->misc);
}

int pc_ctr_parent(int itemCategory, int index, vs_menu_containerData* container) /* _getParentItemIndex 625 */
{
    int parentIndex = 0;

    if (itemCategory == itemCategoryBlade) {
        parentIndex = container->blades[index].assembledWeaponIndex;
    }
    if (itemCategory == itemCategoryGrip) {
        parentIndex = container->grips[index].assembledWeaponIndex;
    }
    if (itemCategory == itemCategoryGem) {
        parentIndex = container->gems[index].setItemIndex;
        if (parentIndex & gemTargetShield) {
            parentIndex = 0;
        }
    }
    return parentIndex;
}

int pc_ctr_set_shield(int itemCategory, int index, vs_menu_containerData* container) /* _getSetShieldIndex 645 */
{
    int parentIndex = 0;

    if (itemCategory == itemCategoryGem) {
        parentIndex = container->gems[index].setItemIndex;
        if (!(container->gems[index].setItemIndex & gemTargetShield)) {
            parentIndex = 0;
        }
    }
    return parentIndex & 0x7F;
}

/* ---- staging ---- */

void pc_ctr_misc_sim(const pc_ctr* c) /* func_801031A0 259 */
{
    int i;
    int var_a2;
    vs_menu_containerData* container = &c->st->unk4BB0;

    copy(&container->misc, c->dst->data.misc, sizeof container->misc);

    for (var_a2 = c->st->itemsToTransfer[0], i = 0; var_a2 != 0; ++i, var_a2 = c->st->itemsToTransfer[i]) {
        if ((var_a2 >> 8) == itemCategoryMisc) {
            pc_ctr_copy_item(PC_CTR_COPY | itemCategoryMisc, container, (var_a2 - 1) & 0xFF, &c->st->unk87B0);
        }
    }
}

int pc_ctr_misc_count(const pc_ctr* c) /* func_80103270 276 (D_80109A82) */
{
    pc_ctr_misc_sim(c);
    return (u_short)pc_ctr_count(itemCategoryMisc, &c->st->unk4BB0);
}

int pc_ctr_weapon_parts(const pc_ctr* c, int arg0) /* _getWeaponsubItemCount 1963 */
{
    int i;
    vs_main_inventoryWeapon* temp = &c->src->data.weapons[arg0];
    int count = 0;

    for (i = 0; i < c->src->data.grips[temp->grip - 1].gemSlots; ++i) {
        count += temp->gems[i] != 0;
    }
    return count;
}

int pc_ctr_shield_parts(const pc_ctr* c, int arg0) /* _getShieldsubItemCount 1976 */
{
    int i;
    vs_main_inventoryShield* shield = &c->src->data.shields[arg0];
    int count = 0;

    for (i = 0; i < shield->base.gemSlots; ++i) {
        count += shield->gems[i] != 0;
    }
    return count;
}

int pc_ctr_over(const pc_ctr* c, int arg0, int arg1) /* func_801066DC 1989 */
{
    int temp_s0 = pc_ctr_count(arg0, &c->dst->data);
    int temp_s1 = arg1 + (temp_s0 + pc_ctr_count(arg0, &c->st->unk87B0));

    if (c->mode != 0) {
        return pc_ctr_caps[arg0] < temp_s1;
    }
    return c->bag_caps[arg0] < temp_s1;
}

int pc_ctr_fits(const pc_ctr* c, int arg0, int arg1) /* func_80106784 2001 */
{
    int i;

    for (i = 0; i < 64; ++i) {
        if (c->st->itemsToTransfer[i] == 0) {
            break;
        }
    }

    if (arg0 == 0) {
        i += 2 + pc_ctr_weapon_parts(c, arg1);
    }

    if (arg0 == 3) {
        i += pc_ctr_shield_parts(c, arg1);
    }

    if (i >= 64) {
        return 1;
    }

    if (pc_ctr_over(c, arg0, 1) != 0) {
        return 2;
    }

    if (arg0 == 0) {
        if (pc_ctr_over(c, 1, 1) != 0)
            return 2;
        if (pc_ctr_over(c, 2, 1) != 0)
            return 2;
        if (pc_ctr_over(c, 5, pc_ctr_weapon_parts(c, arg1)) != 0)
            return 2;
    }

    if (arg0 == 3 && pc_ctr_over(c, 5, pc_ctr_shield_parts(c, arg1)) != 0) {
        return 2;
    }

    return 0;
}

void pc_ctr_unstage(const pc_ctr* c, int arg0) /* func_801068BC 2043 */
{
    int temp_s0_2;
    int temp_s2;
    int temp_s0;

    temp_s0 = c->st->itemsToTransfer[arg0];
    temp_s2 = temp_s0 >> 8;
    temp_s0_2 = (temp_s0 - 1) & 0xFF;
    pc_ctr_copy_item(temp_s2 | 0x10, &c->src->data, temp_s0_2, &c->st->unk87B0);
    pc_ctr_init_object(temp_s2, temp_s0_2, &c->st->unk87B0);
}

void pc_ctr_stage(const pc_ctr* c, int arg0, int arg1) /* func_80106948 2057 */
{
    pc_ctr_copy_item(arg0 | 0x10, &c->st->unk87B0, arg1, &c->src->data);
    pc_ctr_init_object(arg0, arg1, &c->src->data);
}

int pc_ctr_transfer_find(const pc_ctr* c, int arg0, int arg1) /* func_801069B0 2072 */
{
    int i;

    for (i = 0; i < 64; ++i) {
        if (c->st->itemsToTransfer[i] == ((arg0 << 8) | (arg1 + 1))) {
            return i + 1;
        }
    }
    return 0;
}

void pc_ctr_transfer_add(const pc_ctr* c, int arg0, int arg1) /* func_80106A04 2083 */
{
    int i;
    /* (u_short*)vs_menu_inventoryStorage + 0x61D8: itemsToTransfer (offset 0xC3B0, the probe checks it) */
    u_short* var_v1 = c->st->itemsToTransfer;

    for (i = 0; i < 64; ++i) {
        if (var_v1[i] == 0) {
            var_v1[i] = ((arg0 << 8) | (arg1 + 1));
            return;
        }
    }
}

void pc_ctr_transfer_clean(const pc_ctr* c) /* func_80106A50 2091 */
{
    int j;
    int var_a1;
    int i;
    int k;
    u_short* temp_s2 = c->st->itemsToTransfer;

    for (i = 0; i < 64; ++i) {
        int source = temp_s2[i];
        if ((source != 0) && (pc_ctr_item_id((source >> 8), (source - 1) & 0xFF, &c->st->unk87B0) == 0)) {
            temp_s2[i] = 0;
        }
    }

    i = 0;

    while (1) {
        if (temp_s2[i] != 0) {
            ++i;
            if (i == 0x3F) {
                break;
            }
        } else {
            for (var_a1 = i + 1; var_a1 < 0x40; ++var_a1) {
                if (temp_s2[var_a1] != 0) {
                    break;
                }
            }

            if (var_a1 == 0x40) {
                break;
            }

            for (j = var_a1; j < 0x40; ++j) {
                temp_s2[j + i - var_a1] = temp_s2[j];
            }

            for (j = j + (i - var_a1); j < 64; ++j) {
                temp_s2[j] = 0;
            }
        }
    }

    for (k = 0; k < 7; ++k) {
        for (i = 0; i < pc_ctr_caps[k]; ++i) {
            if ((pc_ctr_item_id(k, i, &c->st->unk87B0) != 0) && (pc_ctr_transfer_find(c, k, i) == 0)) {
                pc_ctr_transfer_add(c, k, i);
            }
        }
    }
}

int pc_ctr_transfer_count(const pc_ctr* c)
{
    int i;
    for (i = 0; i < 64; ++i) {
        if (c->st->itemsToTransfer[i] == 0) {
            break;
        }
    }
    return i;
}

void pc_ctr_commit(const pc_ctr* c) /* func_80107490 state 15, YES (3010-3026) */
{
    int temp_s2;
    int temp_s3;

    while ((temp_s2 = c->st->itemsToTransfer[0])) {
        pc_ctr_copy_item((temp_s2 >> 8) | 0x10, &c->dst->data, (temp_s2 - 1) & 0xFF, &c->st->unk87B0);
        pc_ctr_init_object((temp_s2 >> 8), (temp_s2 - 1) & 0xFF, &c->st->unk87B0);
        pc_ctr_transfer_clean(c);
    }
    for (temp_s3 = 0; temp_s3 < 7; ++temp_s3) {
        pc_ctr_defragment(temp_s3, c->dst);
    }
}

void pc_ctr_merge_or_swap(const pc_ctr* c, int var_s4, int temp_s2, int temp_s3) /* state 6 (2740) */
{
    u_short* temp_s1 = pc_ctr_indices(var_s4, c->src);
    int temp_s0 = temp_s1[temp_s3 - 1];
    int var_a2 = temp_s1[temp_s2 - 1];
    vs_menu_containerData* d = &c->src->data;

    if ((var_s4 == 6) && (d->misc[temp_s0 - 1].id == d->misc[var_a2 - 1].id)) {
        d->misc[var_a2 - 1].count += d->misc[temp_s0 - 1].count;
        if (d->misc[var_a2 - 1].count > 0x64) {
            d->misc[temp_s0 - 1].count = d->misc[var_a2 - 1].count - 0x64;
            d->misc[var_a2 - 1].count = 0x64;
        } else {
            pc_ctr_init_object(6, temp_s0 - 1, d);
        }
    } else {
        temp_s1[temp_s3 - 1] = var_a2;
        temp_s1[temp_s2 - 1] = temp_s0;
    }
}

/* ---- the action menu (state 8, 2693) ---- */

void pc_ctr_build_actions(const pc_ctr* c, int var_s4, int row, pc_ctr_actions* out)
{
    /* MENUD menuText's index table entries the original reads (_menuText[4], [5], [6], [7]) are
     * offsets: the bridge resolves them; here a reason is the menuText index (reason_menu 2) or
     * a raw menuText offset (reason_menu 1) or an ITEMHELP index (0) */
    vs_menu_containerData* d = &c->src->data;
    int temp_s3 = pc_ctr_indices(var_s4, c->src)[row - 1] - 1;
    int temp_s0 = 0, temp_s2 = 0x33, temp_s2_index = -1, var_a2, n;

    zero(out, sizeof *out);
    switch (var_s4) {
    case 0:
        temp_s0 = d->weapons[temp_s3].isEquipped;
        temp_s2 = 0x4A;
        break;
    case 3:
        temp_s0 = d->shields[temp_s3].isEquipped;
        temp_s2 = 0x4A;
        break;
    case 4:
        temp_s0 = d->armor[temp_s3].limb;
        temp_s2 = 0x4A;
        break;
    case 6:
        temp_s0 = (d->misc[temp_s3].id < 0x1CA) ^ 1;
        break;
    }
    var_a2 = pc_ctr_parent(var_s4, temp_s3, d);
    if (var_a2 != 0) {
        temp_s0 = 1;
        temp_s2_index = d->weapons[var_a2 - 1].isEquipped == 0 ? 5 : 4;
    }
    var_a2 = pc_ctr_set_shield(var_s4, temp_s3, d);
    if (var_a2 != 0) {
        temp_s0 = 1;
        temp_s2_index = d->shields[var_a2 - 1].isEquipped == 0 ? 5 : 4;
    }
    out->kind[0] = PC_CTR_ACT_TRANSFER;
    if (temp_s0 != 0) {
        var_a2 = 1;
        if (temp_s2_index >= 0) {
            out->reason[0] = temp_s2_index;
            out->reason_menu[0] = 2;
        } else {
            out->reason[0] = temp_s2;
            out->reason_menu[0] = 1;
        }
    } else if (var_s4 == 6) {
        int staged = 0;
        for (temp_s2 = 0; temp_s2 < 7; ++temp_s2) {
            staged += pc_ctr_count(temp_s2, &c->st->unk87B0);
        }
        pc_ctr_misc_sim(c);
        var_a2 = pc_ctr_copy_item(6, &c->st->unk4BB0, temp_s3, d);
        if ((c->mode == 0) && (var_a2 > 0x40)) {
            var_a2 = 0;
        }
        temp_s2 = 0xAB;
        if (var_a2 != 0) {
            if (staged == 0x40) {
                int i;
                int temp_a0 = d->misc[temp_s3].count;
                for (i = 0; i < 256; ++i) {
                    if (c->st->unk87B0.misc[i].id == d->misc[temp_s3].id) {
                        temp_a0 += c->st->unk87B0.misc[i].count - 0x64;
                    }
                }
                var_a2 = temp_a0 < 1;
                temp_s2 = 0x82;
            }
        }
        var_a2 = var_a2 == 0;
        out->reason[0] = temp_s2;
        out->reason_menu[0] = 1;
    } else {
        var_a2 = pc_ctr_fits(c, var_s4, temp_s3);
        out->reason[0] = var_a2 + 5;
        out->reason_menu[0] = 2;
    }
    out->enabled[0] = var_a2 == 0;
    if (var_a2 == 0)
        out->reason[0] = -1, out->reason_menu[0] = 0;
    n = 1;
    if (((u_int)(var_s4 - 1) < 2) || (var_s4 == 5)) {
        out->kind[n] = PC_CTR_ACT_SEARCH;
        out->enabled[n] = 1;
        out->reason[n] = -1;
        if ((pc_ctr_parent(var_s4, temp_s3, d) == 0) && (pc_ctr_set_shield(var_s4, temp_s3, d) == 0)) {
            out->reason[n] = 677; /* VS_ITEMHELP_BIN_INDEX_invalidSearch (+0x35B2) */
            out->enabled[n] = 0;
        }
        ++n;
    }
    temp_s0 = 0;
    switch (var_s4) {
    case 0:
        temp_s0 = d->weapons[temp_s3].isEquipped != 0;
        break;
    case 3:
        temp_s0 = d->shields[temp_s3].isEquipped != 0;
        break;
    case 4:
        temp_s0 = d->armor[temp_s3].limb != 0;
        break;
    case 6:
        if (d->misc[temp_s3].id >= 0x1CA) {
            temp_s0 = 3;
        }
        break;
    }
    var_a2 = pc_ctr_parent(var_s4, temp_s3, d);
    if (var_a2 != 0) {
        temp_s0 = 1;
        if (d->weapons[var_a2 - 1].isEquipped == 0) {
            temp_s0 = 2;
        }
    }
    var_a2 = pc_ctr_set_shield(var_s4, temp_s3, d);
    if (var_a2 != 0) {
        temp_s0 = 1;
        if (d->shields[var_a2 - 1].isEquipped == 0) {
            temp_s0 = 2;
        }
    }
    out->kind[n] = PC_CTR_ACT_DISCARD;
    out->enabled[n] = temp_s0 == 0;
    out->reason[n] = temp_s0 != 0 ? temp_s0 + 663 : -1; /* itemHelp[itemHelp[temp_s0 + 663]] */
    ++n;
    if (var_s4 == 6) {
        out->kind[n] = PC_CTR_ACT_DISCARD_ALL;
        out->enabled[n] = 1;
        out->reason[n] = -1;
        if (d->misc[temp_s3].id >= 0x1CA) {
            out->reason[n] = 666; /* VS_ITEMHELP_BIN_INDEX_cannotDiscard (+0x350E) */
            out->enabled[n] = 0;
        }
        ++n;
    }
    out->kind[n] = PC_CTR_ACT_SORT;
    out->enabled[n] = 1;
    out->reason[n] = -1;
    ++n;
    out->count = n;
}

/* ---- sorts (1279-1606) ---- */

static const char sort_weapons[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23 };                                                             /* D_80109970 */
static const char sort_grips[] = { 0, 8, 9, 10, 24, 25, 26 };          /* D_80109988 */
static const char sort_shields[] = { 1, 4, 5, 6, 7, 8, 9, 10, 40, 41, 42, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39 };                                                         /* D_80109990 */
static const char sort_armor[] = { 0, 1, 4, 5, 8, 9, 10, 40, 41, 42, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
    39 };                                                             /* D_801099A8 */
static const char sort_gems[] = { 8, 9, 10, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 }; /* D_801099C0 */
static const char sort_misc[] = { 0, 56 };                            /* D_801099D0 */
static const char sort_counts[] = { sizeof sort_weapons, sizeof sort_weapons, sizeof sort_grips, sizeof sort_shields,
    sizeof sort_armor, sizeof sort_gems, sizeof sort_misc };          /* D_801099D4 */
static const char* const sort_lists[] = { sort_weapons, sort_weapons, sort_grips, sort_shields, sort_armor, sort_gems,
    sort_misc };                                                      /* D_801099DC */

int pc_ctr_sort_stats(int category, const char** stats)
{
    if (category < 0 || category > 6) {
        *stats = 0;
        return 0;
    }
    *stats = sort_lists[category];
    return sort_counts[category];
}

static void sort_weapons_by(const pc_ctr* c, int stat) /* _sortWeaponsByStat 1312 */
{
    u_short sortedIndices[32];
    int i;
    int maxStatValue;
    int sortedIndex;
    int sourceIndex;
    u_short* targetIndices = pc_ctr_indices(itemCategoryWeapon, c->src);

    zero(sortedIndices, sizeof sortedIndices);
    sortedIndex = 0;

    while (1) {
        maxStatValue = -0x7FFFFFFF - 1;

        for (i = 0; i < 32; ++i) {
            int statValue;
            sourceIndex = targetIndices[i];
            if (sourceIndex == 0) {
                continue;
            }
            statValue = c->weapon_stat(c->user, &c->src->data, sourceIndex - 1, stat);
            if (maxStatValue < statValue) {
                maxStatValue = statValue;
            }
        }

        if (maxStatValue == (-0x7FFFFFFF - 1)) {
            break;
        }

        for (i = 0; i < 32; ++i) {
            sourceIndex = targetIndices[i];
            if (sourceIndex == 0) {
                continue;
            }
            if (c->weapon_stat(c->user, &c->src->data, sourceIndex - 1, stat) == maxStatValue) {
                sortedIndices[sortedIndex++] = sourceIndex;
                targetIndices[i] = 0;
            }
        }
    }
    copy(targetIndices, &sortedIndices, sizeof sortedIndices);
}

static void sort_shields_by(const pc_ctr* c, int arg0) /* _sortShieldsByStat 1394 */
{
    u_short sp178[0x20];
    int temp_v0;
    int i;
    int var_s3;
    int temp_s0;
    u_short* temp_s6 = pc_ctr_indices(3, c->src);
    int var_s4 = 0;

    zero(&sp178, sizeof sp178);

    while (1) {
        var_s3 = (int)0x80000000;

        for (i = 0; i < 32; ++i) {
            temp_s0 = temp_s6[i];
            if (temp_s0 != 0) {
                temp_v0 = c->shield_stat(c->user, &c->src->data, temp_s0 - 1, arg0);
                if (var_s3 < temp_v0) {
                    var_s3 = temp_v0;
                }
            }
        }

        if (var_s3 == (int)0x80000000) {
            break;
        }

        for (i = 0; i < 32; ++i) {
            temp_s0 = temp_s6[i];
            if (temp_s0 != 0) {
                if (c->shield_stat(c->user, &c->src->data, temp_s0 - 1, arg0) == var_s3) {
                    sp178[var_s4++] = temp_s0;
                    temp_s6[i] = 0;
                }
            }
        }
    }
    copy(temp_s6, &sp178, sizeof sp178);
}

static int item_stat(int arg0, vs_main_inventoryMisc* item) /* _getItemStat 1443 */
{
    if (arg0 == 0) {
        return -item->id;
    }
    return item->count;
}

static void sort_items_by(const pc_ctr* c, int arg0) /* _sortItemsByStat 1451 */
{
    u_short sp10[0x100];
    int temp_v0;
    int i;
    int var_s4;
    int var_s5;
    int temp_s0;
    u_short* temp_s7;
    vs_main_inventoryMisc* item;

    item = c->src->data.misc;
    temp_s7 = pc_ctr_indices(6, c->src);
    zero(&sp10, sizeof sp10);
    var_s5 = 0;

    while (1) {
        var_s4 = (int)0x80000000;

        for (i = 0; i < 256; ++i) {
            temp_s0 = temp_s7[i];
            if (temp_s0 != 0) {
                temp_v0 = item_stat(arg0, &item[temp_s0 - 1]);
                if (var_s4 < temp_v0) {
                    var_s4 = temp_v0;
                }
            }
        }

        if (var_s4 == (int)0x80000000) {
            break;
        }

        for (i = 0; i < 256; ++i) {
            temp_s0 = temp_s7[i];
            if ((temp_s0 != 0) && (item_stat(arg0, &item[temp_s0 - 1]) == var_s4)) {
                sp10[var_s5++] = temp_s0;
                temp_s7[i] = 0;
            }
        }
    }
    copy(temp_s7, &sp10, sizeof sp10);
}

static void sort_equipment_by(const pc_ctr* c, int arg0, int arg1) /* _copyEquipmentByStat 1555 */
{
    int temp_v0;
    int i;
    int var_s4;
    int var_s6;
    int temp_s0;
    int temp_s5;
    u_short* temp_s7;

    var_s6 = 0;
    temp_s5 = pc_ctr_caps[arg0];

    {
        u_short sp40[192];
        temp_s7 = pc_ctr_indices(arg0, c->src);
        zero(sp40, temp_s5 * 2);

        while (1) {
            var_s4 = (int)0x80000000;

            for (i = 0; i < temp_s5; ++i) {
                temp_s0 = temp_s7[i];
                if (temp_s0 != 0) {
                    temp_v0 = c->equip_stat(c->user, &c->src->data, arg0, temp_s0 - 1, arg1);
                    if (var_s4 < temp_v0) {
                        var_s4 = temp_v0;
                    }
                }
            }

            if (var_s4 == (int)0x80000000) {
                break;
            }

            for (i = 0; i < temp_s5; ++i) {
                temp_s0 = temp_s7[i];
                if (temp_s0 != 0) {
                    if (c->equip_stat(c->user, &c->src->data, arg0, temp_s0 - 1, arg1) == var_s4) {
                        sp40[var_s6++] = temp_s0;
                        temp_s7[i] = 0;
                    }
                }
            }
        }
        copy(temp_s7, sp40, temp_s5 * 2);
    }
}

void pc_ctr_sort(const pc_ctr* c, int category, int i) /* _displaySortMenu's pick (1660) */
{
    switch (category) {
    case 0:
        sort_weapons_by(c, sort_weapons[i]);
        break;
    case 3:
        sort_shields_by(c, sort_shields[i]);
        break;
    case 6:
        sort_items_by(c, sort_misc[i]);
        break;
    default:
        sort_equipment_by(c, category, sort_lists[category][i]);
        break;
    }
}

void pc_ctr_discard(const pc_ctr* c, int itemCategory, int selectedItem, int type, int discardCount)
{
    /* _discardMenu discardOne (1773) / discardMultiple (1910) on YES */
    if (type == 1) {
        if (c->src->data.misc[selectedItem].count == discardCount) {
            pc_ctr_init_object(itemCategory, selectedItem, &c->src->data);
        } else {
            c->src->data.misc[selectedItem].count -= discardCount;
        }
        return;
    }
    pc_ctr_init_object(itemCategory, selectedItem, &c->src->data);
}

/* ---- the rows (func_80106C64 2148) ---- */

int pc_ctr_rows(const pc_ctr* c, int itemCategory, int* category, int* slot, int* flags, int max)
{
    int itemCount = 0;
    int i, itemIndex, parentIndex, rowType;

    if (itemCategory == 7) {
        vs_menu_containerData* container = &c->st->unk87B0;
        for (i = 0; i < 64 && itemCount < max; ++i) {
            int cat;
            itemIndex = c->st->itemsToTransfer[i];
            cat = itemIndex >> 8;
            itemIndex &= 0xFF;
            if (itemIndex == 0) {
                break;
            }
            --itemIndex;
            rowType = 0;
            if (cat == 5 && pc_ctr_set_shield(5, itemIndex, container) != 0) {
                rowType = PC_CTR_ROW_ATTACHED | PC_CTR_ROW_LOCKED; /* 0xCC01 */
            }
            if (pc_ctr_parent(cat, itemIndex, container) != 0) {
                rowType = PC_CTR_ROW_ATTACHED | PC_CTR_ROW_LOCKED;
            }
            category[itemCount] = cat;
            slot[itemCount] = itemIndex;
            flags[itemCount] = rowType;
            ++itemCount;
        }
        return itemCount;
    }
    {
        u_short* indices = pc_ctr_indices(itemCategory, c->src);
        vs_menu_containerData* d = &c->src->data;
        for (i = 0; i < pc_ctr_caps[itemCategory] && itemCount < max; ++i) {
            itemIndex = indices[i];
            if (itemIndex == 0) {
                break;
            }
            --itemIndex;
            rowType = 0;
            if (itemCategory != 6) {
                switch (itemCategory) {
                case 0:
                    if (d->weapons[itemIndex].isEquipped != 0)
                        rowType = PC_CTR_ROW_EQUIPPED; /* 0xCA00 */
                    break;
                case 3:
                    if (d->shields[itemIndex].isEquipped != 0)
                        rowType = PC_CTR_ROW_EQUIPPED;
                    break;
                case 4:
                    if (d->armor[itemIndex].limb != 0)
                        rowType = PC_CTR_ROW_EQUIPPED;
                    break;
                }
                parentIndex = pc_ctr_parent(itemCategory, itemIndex, d);
                if (parentIndex != 0) {
                    rowType = PC_CTR_ROW_EQUIPPED;
                    if (d->weapons[parentIndex - 1].isEquipped == 0)
                        rowType = PC_CTR_ROW_ATTACHED; /* 0xCC00 */
                }
                parentIndex = pc_ctr_set_shield(itemCategory, itemIndex, d);
                if (parentIndex != 0) {
                    rowType = PC_CTR_ROW_EQUIPPED;
                    if (d->shields[parentIndex - 1].isEquipped == 0)
                        rowType = PC_CTR_ROW_ATTACHED;
                }
            }
            category[itemCount] = itemCategory;
            slot[itemCount] = itemIndex;
            flags[itemCount] = rowType;
            ++itemCount;
        }
    }
    return itemCount;
}
