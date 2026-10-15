/* The native Container screen's data operations (pc/src/ui/container_ops.c,
 * MENUD 234.c ported) without the game: golden vectors for the misc merge
 * and pour (to 100, the overflow, both directions), the weapon and shield
 * remaps (assembledWeaponIndex, setItemIndex, | 0x80), gems[3] never taken
 * for a gem, defragment (append, idempotence), the capacities (container
 * 32/64/64/32/64/192/256, bag 8/16/16/8/16/48/64, the 64-entry transfer
 * list), the misc full-merge exception, sorts (ties stable, negated keys),
 * the three discards, staging round trips and the commit. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "../../src/BATTLE/BATTLE.PRG/func_8006B57C_t.h"
#include "container_ops.h"

static int failures;
#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            ++failures;                                                                                      \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                             \
            fprintf(stderr, __VA_ARGS__);                                                                    \
            fputc('\n', stderr);                                                                             \
        }                                                                                                    \
    } while (0)

static const u_char bag_caps[7] = { 8, 16, 16, 8, 16, 48, 64 };
static vs_main_inventoryIndices_t bag_idx;
static u_char* bag_indices[7];
static vs_main_inventory_t bag;
static vs_menu_inventoryStorage_t* st;

/* a fake stat: the weapon's name[0], a shield's base.strength, an equipment record's own first byte */
static int weapon_stat(void* u, vs_menu_containerData* c, int slot, int stat)
{
    (void)u;
    return stat == 0 ? -(int)(u_char)c->weapons[slot].name[0] : (u_char)c->weapons[slot].name[1];
}
static int shield_stat(void* u, vs_menu_containerData* c, int slot, int stat)
{
    (void)u, (void)stat;
    return c->shields[slot].base.strength;
}
static int equip_stat(void* u, vs_menu_containerData* c, int category, int slot, int stat)
{
    (void)u, (void)stat;
    if (category == 1)
        return c->blades[slot].strength;
    if (category == 5)
        return -c->gems[slot].id; /* negated: ascending */
    return 0;
}

static pc_ctr ctx(int mode)
{
    pc_ctr c;
    memset(&c, 0, sizeof c);
    c.st = st;
    c.mode = mode;
    c.src = mode ? &st->unk105B0 : &st->unkC430;
    c.dst = mode ? &st->unkC430 : &st->unk105B0;
    c.bag_caps = bag_caps;
    c.bag_indices = bag_indices;
    c.bag = &bag;
    c.weapon_stat = weapon_stat;
    c.shield_stat = shield_stat;
    c.equip_stat = equip_stat;
    return c;
}

static void reset(void)
{
    memset(st, 0, sizeof *st);
    memset(&bag, 0, sizeof bag);
    memset(&bag_idx, 0, sizeof bag_idx);
}

static void misc(vs_menu_container* k, int slot, int id, int count)
{
    k->data.misc[slot].id = id;
    k->data.misc[slot].count = count;
    k->data.misc[slot].index = slot + 1;
    pc_ctr_defragment(6, k);
}

/* a weapon in container slots: weapon w, blade b, grip g, gems at gem slots g0, g1 (0: none) */
static void weapon(vs_menu_container* k, int w, int b, int g, int g0, int g1, int slots)
{
    vs_menu_containerData* d = &k->data;
    d->weapons[w].index = w + 1;
    d->weapons[w].blade = b + 1;
    d->weapons[w].grip = g + 1;
    d->blades[b].id = 3;
    d->blades[b].assembledWeaponIndex = w + 1;
    d->blades[b].index = b + 1;
    d->grips[g].id = 40;
    d->grips[g].gemSlots = slots;
    d->grips[g].assembledWeaponIndex = w + 1;
    d->grips[g].index = g + 1;
    if (g0) {
        d->weapons[w].gems[0] = g0 + 1;
        d->gems[g0].id = 270;
        d->gems[g0].setItemIndex = w + 1;
    }
    if (g1) {
        d->weapons[w].gems[1] = g1 + 1;
        d->gems[g1].id = 271;
        d->gems[g1].setItemIndex = w + 1;
    }
    memcpy(d->weapons[w].name, "Sword", 6);
}

static void test_layout(void)
{
    CHECK(offsetof(vs_menu_inventoryStorage_t, itemsToTransfer) == 0x61D8 * 2,
        "itemsToTransfer at 0xC3B0 ((u_short*)storage + 0x61D8), got %#zx",
        offsetof(vs_menu_inventoryStorage_t, itemsToTransfer));
    CHECK(sizeof(vs_menu_containerData) == 0x3C00, "containerData 0x3C00 (%#zx)", sizeof(vs_menu_containerData));
    CHECK(sizeof(vs_main_inventory_t) == 0xF00, "the bag 0xF00 (%#zx)", sizeof(vs_main_inventory_t));
    CHECK(pc_ctr_indices(6, &st->unkC430) == st->unkC430.indices.misc, "the misc list's offset (32+64+64+32+64+192)");
    CHECK(pc_ctr_indices(3, &st->unkC430) == st->unkC430.indices.shields, "the shields' list follows the grips'");
}

static void test_misc(void)
{
    pc_ctr c;
    int r;
    /* merge to 100 with the overflow, both directions (state 6) */
    for (int order = 0; order < 2; ++order) {
        reset();
        c = ctx(0);
        misc(&st->unkC430, 0, 330, order ? 70 : 95);
        misc(&st->unkC430, 1, 330, order ? 95 : 70);
        pc_ctr_merge_or_swap(&c, 6, 1, 2); /* row 1 picked first, row 2 second */
        CHECK(st->unkC430.data.misc[0].count == 100 && st->unkC430.data.misc[1].count == 65,
            "merge %d: first 100, second the overflow 65 (%d, %d)", order, st->unkC430.data.misc[0].count,
            st->unkC430.data.misc[1].count);
    }
    reset();
    c = ctx(0);
    misc(&st->unkC430, 0, 330, 30);
    misc(&st->unkC430, 1, 330, 40);
    pc_ctr_merge_or_swap(&c, 6, 2, 1);
    CHECK(st->unkC430.data.misc[1].count == 70 && st->unkC430.data.misc[0].id == 0
              && st->unkC430.data.misc[0].index == 1,
        "merge under 100: the first picked holds 70, the second cleared (%d / id %d)", st->unkC430.data.misc[1].count,
        st->unkC430.data.misc[0].id);
    /* different ids swap the index entries */
    reset();
    c = ctx(0);
    misc(&st->unkC430, 0, 330, 3);
    misc(&st->unkC430, 1, 331, 4);
    pc_ctr_merge_or_swap(&c, 6, 1, 2);
    CHECK(st->unkC430.indices.misc[0] == 2 && st->unkC430.indices.misc[1] == 1 && st->unkC430.data.misc[0].count == 3,
        "different items swap (indices %d %d)", st->unkC430.indices.misc[0], st->unkC430.indices.misc[1]);
    /* pour: 70 onto a 60 stack -> 100 and a new stack of 30 */
    reset();
    misc(&st->unk105B0, 0, 330, 60);
    misc(&st->unkC430, 4, 330, 70);
    r = pc_ctr_copy_item(PC_CTR_COPY | 6, &st->unk105B0.data, 4, &st->unkC430.data);
    CHECK(r == 2 && st->unk105B0.data.misc[0].count == 100 && st->unk105B0.data.misc[1].count == 30
              && st->unk105B0.data.misc[1].id == 330,
        "pour 70 onto 60: 100 + 30 in slot 2 (r %d, %d, %d)", r, st->unk105B0.data.misc[0].count,
        st->unk105B0.data.misc[1].count);
    /* the test mode changes nothing and says the remainder needs a slot */
    reset();
    misc(&st->unk105B0, 0, 330, 60);
    misc(&st->unkC430, 4, 330, 30);
    r = pc_ctr_copy_item(6, &st->unk105B0.data, 4, &st->unkC430.data);
    CHECK(r == 1 && st->unk105B0.data.misc[0].count == 60, "test: 30 onto 60 merges whole (r %d), nothing written", r);
    r = pc_ctr_copy_item(PC_CTR_COPY | 6, &st->unk105B0.data, 4, &st->unkC430.data);
    CHECK(r == 1 && st->unk105B0.data.misc[0].count == 90, "copy: 30 onto 60 -> 90 (r %d)", r);
    /* 256 misc stacks: a new stack has no room */
    reset();
    for (int i = 0; i < 256; ++i)
        st->unk105B0.data.misc[i].id = 400 + (i & 7), st->unk105B0.data.misc[i].count = 100;
    misc(&st->unkC430, 0, 330, 5);
    CHECK(pc_ctr_copy_item(PC_CTR_COPY | 6, &st->unk105B0.data, 0, &st->unkC430.data) == 0,
        "misc cap 256: no room");
}

static void test_weapon_shield(void)
{
    pc_ctr c;
    int r;
    reset();
    weapon(&st->unkC430, 2, 5, 3, 7, 9, 3);
    st->unkC430.data.weapons[2].gems[3] = 0x42; /* the QoL slot byte: never a gem index */
    r = pc_ctr_copy_item(PC_CTR_COPY | 0, &st->unk105B0.data, 2, &st->unkC430.data);
    vs_menu_containerData* t = &st->unk105B0.data;
    CHECK(r == 1 && t->weapons[0].index == 1 && t->weapons[0].blade == 1 && t->weapons[0].grip == 1,
        "weapon copied to slot 1 with its blade and grip (r %d, blade %d grip %d)", r, t->weapons[0].blade,
        t->weapons[0].grip);
    CHECK(t->blades[0].assembledWeaponIndex == 1 && t->grips[0].assembledWeaponIndex == 1,
        "blade / grip assembledWeaponIndex = the new weapon (%d %d)", t->blades[0].assembledWeaponIndex,
        t->grips[0].assembledWeaponIndex);
    CHECK(t->weapons[0].gems[0] == 1 && t->weapons[0].gems[1] == 2 && t->gems[0].setItemIndex == 1
              && t->gems[1].setItemIndex == 1 && t->gems[0].id == 270 && t->gems[1].id == 271,
        "gems copied, setItemIndex = the weapon (%d %d / %d %d)", t->weapons[0].gems[0], t->weapons[0].gems[1],
        t->gems[0].setItemIndex, t->gems[1].setItemIndex);
    CHECK(t->weapons[0].gems[3] == 0 && pc_ctr_count(5, t) == 2,
        "gems[3] is not a gem (3 gem bytes): %d, gems %d", t->weapons[0].gems[3], pc_ctr_count(5, t));
    CHECK(memcmp(t->weapons[0].name, "Sword", 6) == 0, "the name copied");
    /* the weapon cleared with its parts; the QoL byte's gem 0x41 untouched */
    st->unkC430.data.gems[0x41].id = 299;
    pc_ctr_init_object(0, 2, &st->unkC430.data);
    CHECK(pc_ctr_count(0, &st->unkC430.data) == 0 && pc_ctr_count(1, &st->unkC430.data) == 0
              && pc_ctr_count(2, &st->unkC430.data) == 0 && pc_ctr_count(5, &st->unkC430.data) == 1
              && st->unkC430.data.weapons[2].index == 3,
        "init: the weapon, its blade, grip and gems cleared (gem 0x41 kept), index = slot + 1");
    /* shield: gems set with | 0x80 */
    reset();
    st->unk105B0.data.shields[1].base.id = 100;
    st->unk105B0.data.shields[1].base.gemSlots = 2;
    st->unk105B0.data.shields[1].gems[0] = 4;
    st->unk105B0.data.gems[3].id = 272;
    st->unk105B0.data.gems[3].setItemIndex = 2 | 0x80;
    r = pc_ctr_copy_item(PC_CTR_COPY | 3, &st->unkC430.data, 1, &st->unk105B0.data);
    t = &st->unkC430.data;
    CHECK(r == 1 && t->shields[0].gems[0] == 1 && t->gems[0].setItemIndex == (1 | 0x80) && t->gems[0].id == 272,
        "shield copied, its gem's setItemIndex = 1 | 0x80 (%#x)", t->gems[0].setItemIndex);
    CHECK(pc_ctr_parent(5, 0, t) == 0 && pc_ctr_set_shield(5, 0, t) == 1, "a shield's gem: no parent weapon, shield 1");
    /* capacities (container): 32 weapons, 64 blades / grips, 192 gems */
    reset();
    weapon(&st->unk105B0, 0, 0, 0, 1, 0, 3);
    for (int i = 0; i < 32; ++i)
        st->unkC430.data.weapons[i].blade = 1;
    CHECK(pc_ctr_copy_item(PC_CTR_COPY | 0, &st->unkC430.data, 0, &st->unk105B0.data) == 0, "32 weapons: no room");
    memset(&st->unkC430, 0, sizeof st->unkC430);
    for (int i = 0; i < 64; ++i)
        st->unkC430.data.blades[i].id = 1;
    CHECK(pc_ctr_copy_item(0, &st->unkC430.data, 0, &st->unk105B0.data) == 0, "64 blades: no room for a weapon");
    memset(&st->unkC430, 0, sizeof st->unkC430);
    for (int i = 0; i < 192; ++i)
        st->unkC430.data.gems[i].id = 262;
    CHECK(pc_ctr_copy_item(0, &st->unkC430.data, 0, &st->unk105B0.data) == 0, "192 gems: no room for a gemmed weapon");
    st->unkC430.data.gems[191].id = 0;
    CHECK(pc_ctr_copy_item(0, &st->unkC430.data, 0, &st->unk105B0.data) == 1, "191 gems: room for one more gem");
    (void)c;
}

static void test_defragment(void)
{
    reset();
    vs_menu_container* k = &st->unkC430;
    k->data.armor[5].id = 1;
    k->data.armor[2].id = 2;
    k->data.armor[9].id = 3;
    k->indices.armor[0] = 10; /* listed */
    k->indices.armor[3] = 8;  /* a dead entry: armor 8 is empty */
    pc_ctr_defragment(4, k);
    CHECK(k->indices.armor[0] == 10 && k->indices.armor[1] == 3 && k->indices.armor[2] == 6 && k->indices.armor[3] == 0,
        "defragment: dead out, compacted, the missing appended in slot order (%d %d %d %d)", k->indices.armor[0],
        k->indices.armor[1], k->indices.armor[2], k->indices.armor[3]);
    u_short before[64];
    memcpy(before, k->indices.armor, sizeof before);
    pc_ctr_defragment(4, k);
    CHECK(!memcmp(before, k->indices.armor, sizeof before), "defragment is idempotent");
}

static void test_staging(void)
{
    pc_ctr c;
    reset();
    /* Remove: container weapon (2 gems) staged, then misc */
    weapon(&st->unkC430, 3, 6, 2, 10, 11, 3);
    pc_ctr_defragment(0, &st->unkC430);
    misc(&st->unkC430, 7, 330, 70);
    misc(&st->unk105B0, 0, 330, 60);
    c = ctx(0);
    CHECK(pc_ctr_fits(&c, 0, 3) == 0, "a weapon fits an empty bag (%d)", pc_ctr_fits(&c, 0, 3));
    pc_ctr_stage(&c, 0, 3);
    pc_ctr_transfer_clean(&c);
    CHECK(pc_ctr_transfer_count(&c) == 5, "staged weapon: 1 + blade + grip + 2 gems entries (%d)",
        pc_ctr_transfer_count(&c));
    CHECK(pc_ctr_count(0, &st->unkC430.data) == 0 && pc_ctr_count(0, &st->unk87B0) == 1,
        "the source's weapon moved to staging");
    pc_ctr_stage(&c, 6, 7);
    pc_ctr_transfer_clean(&c);
    CHECK(pc_ctr_misc_count(&c) == 2 && st->unk4BB0.misc[0].count == 100 && st->unk4BB0.misc[1].count == 30,
        "the misc simulation: 60 + 70 -> 100 and 30, 2 stacks (D_80109A82 %d)", pc_ctr_misc_count(&c));
    /* unstage the misc (entry 5) and stage it again */
    pc_ctr_unstage(&c, 5);
    pc_ctr_transfer_clean(&c);
    CHECK(pc_ctr_transfer_count(&c) == 5 && pc_ctr_count(6, &st->unkC430.data) == 1, "unstaged: back in the source");
    pc_ctr_defragment(6, &st->unkC430);
    pc_ctr_stage(&c, 6, st->unkC430.indices.misc[0] - 1);
    pc_ctr_transfer_clean(&c);
    /* rows of the transfer list: the weapon's parts locked */
    int cat[64], slot[64], flags[64];
    int n = pc_ctr_rows(&c, 7, cat, slot, flags, 64);
    int locked = 0;
    for (int i = 0; i < n; ++i)
        locked += (flags[i] & PC_CTR_ROW_LOCKED) != 0;
    CHECK(n == 6 && locked == 4, "tab 7: 6 rows, the weapon's 4 parts locked (%d, %d)", n, locked);
    pc_ctr_commit(&c);
    vs_menu_containerData* b = &st->unk105B0.data;
    CHECK(st->itemsToTransfer[0] == 0 && pc_ctr_count(0, b) == 1 && pc_ctr_count(1, b) == 1 && pc_ctr_count(5, b) == 2
              && b->misc[0].count == 100 && b->misc[1].count == 30 && b->weapons[0].blade == 1
              && b->blades[0].assembledWeaponIndex == 1,
        "commit: the weapon with its parts and the misc in the bag, the list empty");
    CHECK(st->unk105B0.indices.weapons[0] == 1 && st->unk105B0.indices.misc[1] == 2, "commit: the destination defragmented");
    /* bag caps in Remove mode: 8 weapons */
    reset();
    for (int i = 0; i < 8; ++i)
        st->unk105B0.data.weapons[i].blade = 1;
    weapon(&st->unkC430, 0, 0, 0, 0, 0, 2);
    c = ctx(0);
    CHECK(pc_ctr_fits(&c, 0, 0) == 2, "Remove: the bag's 8 weapons: no room (2)");
    c = ctx(1); /* Store: the container's 32 */
    CHECK(pc_ctr_over(&c, 0, 1) == 0, "Store: the container's 32 weapons have room");
    /* the 64-entry transfer list */
    reset();
    for (int i = 0; i < 63; ++i)
        st->itemsToTransfer[i] = 0x401;
    weapon(&st->unkC430, 0, 0, 0, 0, 0, 2);
    c = ctx(0);
    CHECK(pc_ctr_fits(&c, 0, 0) == 1, "63 entries + a weapon (2 + parts) passes 64: 1");
    st->unkC430.data.armor[0].id = 5;
    CHECK(pc_ctr_fits(&c, 4, 0) == 0, "63 entries + an armor fits");
}

static void test_actions(void)
{
    pc_ctr c;
    pc_ctr_actions a;
    /* the misc full-merge exception: 64 staged, the item merges whole into staged stacks */
    reset();
    for (int i = 0; i < 64; ++i)
        st->unk87B0.misc[i].id = 330 + (i == 0 ? 0 : 1), st->unk87B0.misc[i].count = i == 0 ? 50 : 100;
    for (int i = 0; i < 64; ++i)
        st->itemsToTransfer[i] = (6 << 8) | (i + 1);
    misc(&st->unkC430, 0, 330, 40);
    c = ctx(1); /* Store: bag misc to the container */
    st->unkC430.data.misc[0].id = 0;
    misc(&st->unk105B0, 0, 330, 40);
    c = ctx(1);
    pc_ctr_build_actions(&c, 6, 1, &a);
    CHECK(a.kind[0] == PC_CTR_ACT_TRANSFER && a.enabled[0], "64 staged, 40 onto a staged 50: merges whole, allowed");
    st->unk105B0.data.misc[0].count = 60;
    pc_ctr_build_actions(&c, 6, 1, &a);
    CHECK(!a.enabled[0] && a.reason[0] == 0x82 && a.reason_menu[0] == 1,
        "64 staged, 60 onto a staged 50: maxItemsSelected (0x82) (%d %#x)", a.enabled[0], a.reason[0]);
    CHECK(a.count == 4 && a.kind[1] == PC_CTR_ACT_DISCARD && a.kind[2] == PC_CTR_ACT_DISCARD_ALL
              && a.kind[3] == PC_CTR_ACT_SORT,
        "misc: Store, Discard, Discard all, Sort (%d)", a.count);
    /* a key: storingProhibited, cannotDiscard */
    st->unk105B0.data.misc[0].id = 0x1CA;
    pc_ctr_build_actions(&c, 6, 1, &a);
    CHECK(!a.enabled[0] && a.reason[0] == 0x33 && !a.enabled[1] && a.reason[1] == 666 && !a.enabled[2],
        "a key: no transfer (0x33), no discard (666), no discard all");
    /* an equipped weapon; a blade of it (Search on, Discard off: equipped) */
    reset();
    weapon(&st->unk105B0, 0, 0, 0, 1, 0, 3);
    st->unk105B0.data.weapons[0].isEquipped = 1;
    pc_ctr_defragment(0, &st->unk105B0);
    pc_ctr_defragment(1, &st->unk105B0);
    c = ctx(1);
    pc_ctr_build_actions(&c, 0, 1, &a);
    CHECK(!a.enabled[0] && a.reason[0] == 0x4A && a.count == 3 && !a.enabled[1] && a.reason[1] == 664,
        "an equipped weapon: no transfer (0x4A), no discard (664)");
    pc_ctr_build_actions(&c, 1, 1, &a);
    CHECK(!a.enabled[0] && a.reason[0] == 4 && a.reason_menu[0] == 2 && a.kind[1] == PC_CTR_ACT_SEARCH
              && a.enabled[1] && !a.enabled[2] && a.reason[2] == 664,
        "an equipped weapon's blade: itemIsEquipped (index 4), Search, no discard");
    st->unk105B0.data.weapons[0].isEquipped = 0;
    pc_ctr_build_actions(&c, 1, 1, &a);
    CHECK(a.reason[0] == 5 && a.reason[2] == 665, "a combined blade: itemIsCombined (index 5), discard 665");
}

static void test_sort_discard(void)
{
    pc_ctr c;
    const char* stats;
    reset();
    c = ctx(0);
    misc(&st->unkC430, 0, 335, 5);
    misc(&st->unkC430, 1, 331, 9);
    misc(&st->unkC430, 2, 335, 7);
    misc(&st->unkC430, 3, 333, 9);
    CHECK(pc_ctr_sort_stats(6, &stats) == 2 && stats[1] == 56, "misc sorts: id, QUANTITY (56)");
    pc_ctr_sort(&c, 6, 0); /* by id: ascending (negated), ties in list order */
    u_short* m = st->unkC430.indices.misc;
    CHECK(m[0] == 2 && m[1] == 4 && m[2] == 1 && m[3] == 3, "by id: 331, 333, 335 (slot 1), 335 (slot 3): %d %d %d %d",
        m[0], m[1], m[2], m[3]);
    pc_ctr_sort(&c, 6, 1); /* quantity: descending, stable */
    CHECK(m[0] == 2 && m[1] == 4 && m[2] == 3 && m[3] == 1, "by quantity: 9 (331), 9 (333), 7, 5: %d %d %d %d", m[0],
        m[1], m[2], m[3]);
    /* weapons through the stat callback: stat 0 negated (ascending) */
    for (int i = 0; i < 3; ++i) {
        weapon(&st->unkC430, i, i, i, 0, 0, 2);
        st->unkC430.data.weapons[i].name[0] = "CAB"[i];
    }
    pc_ctr_defragment(0, &st->unkC430);
    pc_ctr_sort(&c, 0, 0);
    u_short* w = st->unkC430.indices.weapons;
    CHECK(w[0] == 2 && w[1] == 3 && w[2] == 1, "weapons by a negated key: A, B, C (%d %d %d)", w[0], w[1], w[2]);
    /* gems (equipment sort, entry 0 = STR) */
    st->unkC430.data.gems[4].id = 262, st->unkC430.data.gems[9].id = 262, st->unkC430.data.gems[2].id = 261;
    pc_ctr_defragment(5, &st->unkC430);
    pc_ctr_sort(&c, 5, 0);
    u_short* g = st->unkC430.indices.gems;
    CHECK(g[0] == 3 && g[1] == 5 && g[2] == 10, "gems by -id: 261, then the two 262 in list order (%d %d %d)", g[0],
        g[1], g[2]);
    /* discards */
    pc_ctr_discard(&c, 6, 0, 1, 2);
    CHECK(st->unkC430.data.misc[0].count == 3, "discard 2 of 5: 3 left");
    pc_ctr_discard(&c, 6, 0, 1, 3);
    CHECK(st->unkC430.data.misc[0].id == 0 && st->unkC430.data.misc[0].index == 1, "discard the whole stack: cleared");
    pc_ctr_discard(&c, 6, 1, 2, 0);
    CHECK(st->unkC430.data.misc[1].id == 0, "discard all: cleared");
    pc_ctr_discard(&c, 0, 1, 0, 0);
    CHECK(st->unkC430.data.weapons[1].blade == 0 && st->unkC430.data.blades[1].id == 0, "discard a weapon: its parts too");
}

static void test_bag(void)
{
    pc_ctr c;
    reset();
    for (int i = 0; i < 7; ++i)
        bag_indices[i] = (u_char*[]){ bag_idx.weapons, bag_idx.blades, bag_idx.grips, bag_idx.shields, bag_idx.armor,
            bag_idx.gems, bag_idx.misc }[i];
    bag.misc[3].id = 330, bag.misc[3].count = 12, bag.misc[3].index = 4;
    bag_idx.misc[0] = 4;
    bag.armor[15].id = 9, bag_idx.armor[0] = 16;
    c = ctx(0);
    pc_ctr_bag_to_container(&c);
    CHECK(st->unk105B0.data.misc[3].count == 12 && st->unk105B0.indices.misc[0] == 4 && st->unk105B0.data.armor[15].id == 9
              && st->unk105B0.indices.armor[0] == 16,
        "the bag widened (1/4 of each array, the u_char lists to u_short)");
    st->unk105B0.data.misc[5].id = 331, st->unk105B0.data.misc[5].count = 1;
    pc_ctr_container_to_bag(&c);
    CHECK(bag.misc[5].id == 331 && bag_idx.misc[1] == 6 && bag_idx.misc[0] == 4, "back: defragmented, truncated (%d %d)",
        bag_idx.misc[0], bag_idx.misc[1]);
}

/* --layout: the record layout as JSON (pc/tests/card_fixtures.py with_container builds
 * containers on a card with it: no offsets hard-coded in the test) */
static int layout(void)
{
#define OFF(t, f) (int)offsetof(t, f)
    printf("{\"container\":%d,\"arrays\":{\"weapons\":%d,\"shields\":%d,\"blades\":%d,\"grips\":%d,"
           "\"armor\":%d,\"gems\":%d,\"misc\":%d},",
        (int)sizeof(vs_menu_containerData), OFF(vs_menu_containerData, weapons), OFF(vs_menu_containerData, shields),
        OFF(vs_menu_containerData, blades), OFF(vs_menu_containerData, grips), OFF(vs_menu_containerData, armor),
        OFF(vs_menu_containerData, gems), OFF(vs_menu_containerData, misc));
    printf("\"sizes\":{\"weapons\":%d,\"shields\":%d,\"blades\":%d,\"grips\":%d,\"armor\":%d,\"gems\":%d,"
           "\"misc\":%d},",
        (int)sizeof(vs_main_inventoryWeapon), (int)sizeof(vs_main_inventoryShield), (int)sizeof(vs_main_inventoryBlade),
        (int)sizeof(vs_main_inventoryGrip), (int)sizeof(vs_main_inventoryArmor), (int)sizeof(vs_main_inventoryGem),
        (int)sizeof(vs_main_inventoryMisc));
    printf("\"bag\":{\"size\":%d,\"weapons\":%d,\"shields\":%d,\"blades\":%d,\"grips\":%d,\"armor\":%d,"
           "\"gems\":%d,\"misc\":%d,\"indices\":%d},",
        (int)sizeof(vs_main_inventory_t), OFF(vs_main_inventory_t, weapons), OFF(vs_main_inventory_t, shields),
        OFF(vs_main_inventory_t, blades), OFF(vs_main_inventory_t, grips), OFF(vs_main_inventory_t, armor),
        OFF(vs_main_inventory_t, gems), OFF(vs_main_inventory_t, misc), (int)sizeof(vs_main_inventoryIndices_t));
    printf("\"fields\":{\"weapon_index\":%d,\"weapon_blade\":%d,\"weapon_grip\":%d,\"weapon_equipped\":%d,"
           "\"weapon_gems\":%d,\"blade_parent\":%d,\"blade_index\":%d,\"grip_slots\":%d,\"grip_parent\":%d,"
           "\"grip_index\":%d,\"shield_index\":%d,\"shield_equipped\":%d,\"shield_slots\":%d,\"shield_gems\":%d,"
           "\"armor_limb\":%d,\"armor_index\":%d,\"gem_id\":%d,\"gem_parent\":%d,\"gem_index\":%d,"
           "\"gem_str\":%d,\"misc_id\":%d,\"misc_count\":%d,\"misc_index\":%d}}\n",
        OFF(vs_main_inventoryWeapon, index), OFF(vs_main_inventoryWeapon, blade), OFF(vs_main_inventoryWeapon, grip),
        OFF(vs_main_inventoryWeapon, isEquipped), OFF(vs_main_inventoryWeapon, gems), OFF(vs_main_inventoryBlade,
                                                                                         assembledWeaponIndex),
        OFF(vs_main_inventoryBlade, index), OFF(vs_main_inventoryGrip, gemSlots),
        OFF(vs_main_inventoryGrip, assembledWeaponIndex), OFF(vs_main_inventoryGrip, index),
        OFF(vs_main_inventoryShield, index), OFF(vs_main_inventoryShield, isEquipped),
        OFF(vs_main_inventoryShield, base) + OFF(vs_main_inventoryArmor, gemSlots), OFF(vs_main_inventoryShield, gems),
        OFF(vs_main_inventoryArmor, limb), OFF(vs_main_inventoryArmor, index), OFF(vs_main_inventoryGem, id),
        OFF(vs_main_inventoryGem, setItemIndex), OFF(vs_main_inventoryGem, index), OFF(vs_main_inventoryGem, strength),
        OFF(vs_main_inventoryMisc, id), OFF(vs_main_inventoryMisc, count), OFF(vs_main_inventoryMisc, index));
#undef OFF
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && !strcmp(argv[1], "--layout"))
        return layout();
    st = calloc(1, sizeof *st);
    test_layout();
    test_misc();
    test_weapon_shield();
    test_defragment();
    test_staging();
    test_actions();
    test_sort_discard();
    test_bag();
    free(st);
    if (failures) {
        fprintf(stderr, "container_ops_probe: %d failure(s)\n", failures);
        return 1;
    }
    printf("container_ops_probe: ok\n");
    return 0;
}
