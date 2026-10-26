/* Player state for Lua and the RPC, the game side (player_query.h; battle
 * builds, compiled with the game's headers).
 *
 *   the bag     vs_main_inventory in the menus' list order
 *               (vs_main_inventoryIndices as vs_mainMenu_rebuildInventory
 *               leaves it, worked out on a copy), as the menus see it
 *               (MAINMENU's vs_mainMenu_initInventory copy of the battle
 *               record's current values run, then undone: sync_begin); each
 *               record described as the Items screen's card describes it
 *               (items_bridge.c pc_ui_items_describe: the game's own values;
 *               a weapon or shield not equipped is computed with
 *               vs_battle_applyWeapon / Shield, the equipped one is read from
 *               Ashley's battle record); what is equipped where as the
 *               Equipment screen finds it (equip_bridge.c). The container's
 *               records are not here: they live in MENUD's storage, filled
 *               only while the Container screen runs.
 *   workshop    vs_battle_inWorkshop and MENUC's _workshopMaterials
 *               (pc_menuC_workshop_materials, src/MENU/MENUC.PRG/168.c).
 *   combine     MENUC's own _combineBlades / _combineShields / _combineArmor
 *               (pc_menuC_combine, 168.c) on copies of the two records, into
 *               a buffer prepared as _setCombineBladeUi / _initCombineShields
 *               / _selectShields prepare theirs (the records copied as the
 *               menus see them), with the SYD file read from
 *               the disc into host memory (as workshop_bridge.c reads it).
 *               Those functions write only the result and the overlay's own
 *               table pointers, which pc_menuC_combine puts back. Nothing of
 *               the game changes (player_state_smoke.py: RPC snapshot hash
 *               before and after).
 *
 * Read-only. vs_battle_applyWeapon / Shield / Armor / Accessory (through the
 * Items screen's description) take a scratch block from the game's heap and
 * give it back, which leaves their scratch bytes in its free space: the heap
 * (0x8010C000, 0xF2000 bytes, as vs_main_initHeap sets it up) and its head
 * are copied before and put back after, so a query leaves the game's memory
 * byte for byte as it was. This unit's own statics are host state
 * (.data.rood_host): a query changes no save state either. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "../../../src/MENU/MAINMENU.PRG/C48.h"
#include "build/assets/MENU/ITEMHELP.BIN.h"
#include "build/assets/MENU/ITEMNAME.BIN.h"
#include "build/assets/MENU/MENU12.BIN.h"
#include "../ui/item_names.h"
#include "../ui/ui_model.h"
#include "game_query.h"
#include "player_query.h"
#include <stdlib.h>
#include <string.h>

#define HOST __attribute__((section(".data.rood_host")))

int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */
void pc_ui_items_describe(int c, int index, pc_ui_item_details* d);                       /* items_bridge.c */
int pc_ui_item_record_id(int category, int i);                                            /* equip_bridge.c */
int pc_weapon_slots_get(int weapon_index);                                                /* weapon_slots.c */
/* src/MENU/MENUC.PRG/168.c (ROOD_PC) */
void* pc_menuC_combine(int kind, void* first, void* second, void* result, void* sydData);
int pc_menuC_workshop_materials(int w);

extern _mpdRoomDoorSection_t D_800F1AB0; /* the live room (dev_state.c) */
extern vs_main_HeapHeader heapA;

/* MENUC's text (MENU12.BIN), under our own name */
static u_short menu12_text[] = {
#include "build/assets/MENU/MENU12.BIN.vsString"
};

#define HEAP_START 0x8010C000u
#define HEAP_SIZE 0xF2000u
#define ACCESSORY_LIMB 7

/* MAINMENU vs_mainMenu_inventoryItemCapacities (2D10.c:1595) */
static const u_char capacities[PC_PQ_BAG_CATEGORIES] = { 8, 16, 16, 8, 16, 48, 64 };

/* A pointer into guest RAM (as query_bridge.c checks: during room changes and the
 * attract demo the slots can hold stale values). */
static int guest(const void* p, unsigned size)
{
    __UINTPTR_TYPE__ a = (__UINTPTR_TYPE__)p;
    return a >= 0x80010000ul && a + size <= 0x80200000ul && !(a & 3);
}

static vs_battle_actor2* ashley(void)
{
    vs_battle_actor2* a;
    if (!vs_battle_characterState || !guest(vs_battle_characterState, sizeof *vs_battle_characterState))
        return NULL;
    a = psx_get(vs_battle_characterState->unk3C);
    return a && guest(a, sizeof *a) ? a : NULL;
}

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

static unsigned hash_bytes(unsigned h, const void* data, unsigned size)
{
    const unsigned char* p = data;
    while (size--)
        h = (h ^ *p++) * 16777619u; /* FNV-1a */
    return h;
}

/* ---- the heap guard ---- */

static unsigned char* heap_copy HOST = NULL;
static vs_main_HeapHeader heap_head HOST;

static int guard_begin(void)
{
    if (!heap_copy)
        heap_copy = malloc(HEAP_SIZE);
    if (!heap_copy)
        return 0;
    memcpy(heap_copy, (void*)(__UINTPTR_TYPE__)HEAP_START, HEAP_SIZE);
    heap_head = heapA;
    return 1;
}

static void guard_end(void)
{
    memcpy((void*)(__UINTPTR_TYPE__)HEAP_START, heap_copy, HEAP_SIZE);
    heapA = heap_head;
}

/* ---- the bag as the menus see it ----
 * Ashley's battle record holds the equipped items' current values (DP, PP,
 * ...); MAINMENU's vs_mainMenu_initInventory copies them into the bag's
 * records as every menu opens (and copies the blade's DP and PP inside the
 * record first). The bag is read, and the combine's records copied, between
 * sync_begin and sync_end: the game's own copy run, then the bag and the
 * battle record put back as they were. */

static unsigned char saved_inventory[sizeof vs_main_inventory] HOST;
static unsigned char saved_actor[sizeof(vs_battle_actor2)] HOST;
static vs_battle_actor2* synced HOST = NULL;

static void sync_begin(void)
{
    synced = ashley();
    memcpy(saved_inventory, &vs_main_inventory, sizeof saved_inventory);
    memcpy(saved_actor, synced, sizeof saved_actor);
    vs_mainMenu_initInventory();
}

static void sync_end(void)
{
    memcpy(&vs_main_inventory, saved_inventory, sizeof saved_inventory);
    memcpy(synced, saved_actor, sizeof saved_actor);
}

/* ---- the game's words ---- */

static const char* help(int index) { return pc_ui_item_help(index); }

/* MENU3's sort stats (items_bridge.c: stat i's name is ITEMHELP sortRange - 4 + 2i):
 * classes are stats 11..16, affinities 17..23, damage types 24..26. */
const char* pc_player_label(int group, int i)
{
    static const int first[3] = { 11, 17, 24 }, count[3] = { PC_PQ_CLASSES, PC_PQ_AFFINITIES, PC_PQ_TYPES };
    if (group < 0 || group > 2 || i < 0 || i >= count[group])
        return "";
    return help((first[group] + i) * 2 + VS_ITEMHELP_BIN_INDEX_sortRange - 4);
}

static int two_handed_category(int category) { return category >= 1 && ((0x1D4 >> (category - 1)) & 1); }

/* ---- a record ---- */

static void blank(pc_pq_part* p, int kind, int record)
{
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->record = record;
    p->damage_type = -1;
    p->slot = -1;
}

/* The Items screen's card of record i (0-based) of bag category c. */
static void from_details(pc_pq_part* p, int c, int i)
{
    pc_ui_item_details d;
    int k;
    pc_ui_items_describe(c, i, &d);
    copy_text(p->name, sizeof p->name, d.stats.name);
    copy_text(p->material, sizeof p->material, d.stats.material);
    p->dp = d.stats.dp;
    p->dp_max = d.stats.dp_max;
    p->pp = d.stats.pp;
    p->pp_max = d.stats.pp_max;
    p->str = d.stats.str;
    p->intel = d.stats.intel;
    p->agl = d.stats.agl;
    for (k = 0; k < PC_PQ_CLASSES; ++k)
        p->classes[k] = d.stats.classes[k];
    for (k = 0; k < PC_PQ_AFFINITIES; ++k)
        p->affinities[k] = d.stats.affinities[k];
    for (k = 0; k < PC_PQ_TYPES; ++k)
        p->types[k] = d.types[k];
    p->damage_type = d.stats.damage_type;
    p->two_handed = d.stats.two_handed;
    p->range = d.range;
    p->risk = d.risk;
    p->gem_slots = d.gem_slots;
}

static void blade_word(pc_pq_part* p, int category)
{
    if (category >= 1 && category <= 9)
        copy_text(p->category, sizeof p->category, help(VS_ITEMHELP_BIN_INDEX_dagger + category - 1));
}

static void armor_word(pc_pq_part* p, int category)
{
    if (category >= 1 && category <= 7)
        copy_text(p->category, sizeof p->category, help(VS_ITEMHELP_BIN_INDEX_armorCategory + category - 1));
}

static void blade_part(pc_pq_part* p, int i)
{
    vs_main_inventoryBlade* b = &vs_main_inventory.blades[i];
    blank(p, PC_PQ_BLADE, i + 1);
    p->id = b->id;
    from_details(p, 1, i);
    blade_word(p, (u_char)b->category);
    p->weapon = (u_char)b->assembledWeaponIndex;
}

static void grip_part(pc_pq_part* p, int i)
{
    vs_main_inventoryGrip* g = &vs_main_inventory.grips[i];
    blank(p, PC_PQ_GRIP, i + 1);
    p->id = g->id;
    from_details(p, 2, i);
    copy_text(p->name, sizeof p->name, pc_ui_item_name(g->id));
    if (g->category >= 1 && g->category <= 4)
        copy_text(p->category, sizeof p->category, help(VS_ITEMHELP_BIN_INDEX_shortGrip + g->category - 1));
    p->gem_slots = g->gemSlots;
    p->weapon = g->assembledWeaponIndex;
}

static void gem_part(pc_pq_part* p, int i)
{
    vs_main_inventoryGem* g = &vs_main_inventory.gems[i];
    blank(p, PC_PQ_GEM, i + 1);
    p->id = g->id;
    from_details(p, 5, i);
    copy_text(p->name, sizeof p->name, pc_ui_item_name(g->id));
    if (g->setItemIndex & 0x80)
        p->shield = g->setItemIndex & 0x7F;
    else
        p->weapon = g->setItemIndex;
}

static void armor_part(pc_pq_part* p, int i)
{
    vs_main_inventoryArmor* a = &vs_main_inventory.armor[i];
    blank(p, a->category == 7 ? PC_PQ_ACCESSORY : PC_PQ_ARMOR, i + 1);
    p->id = a->id;
    from_details(p, 4, i);
    if (a->category != 7) /* an accessory's row names no body part (MENU3's "Accessory" is the port's word) */
        armor_word(p, (u_char)a->category);
    if (a->category == 7) {
        p->material[0] = 0;
        p->dp = p->dp_max = 0;
    }
    p->equipped = a->limb != 0;
    if (a->limb >= 1 && a->limb <= 5)
        p->slot = PC_PQ_SLOT_RIGHT_ARM + a->limb - 1;
    else if (a->limb == ACCESSORY_LIMB)
        p->slot = PC_PQ_SLOT_ACCESSORY;
}

static void misc_part(pc_pq_part* p, int i)
{
    vs_main_inventoryMisc* m = &vs_main_inventory.misc[i];
    blank(p, PC_PQ_MISC, i + 1);
    p->id = m->id;
    p->count = m->count;
    copy_text(p->name, sizeof p->name, pc_ui_item_name(m->id));
    p->misc_kind = m->id >= VS_ITEMNAME_BIN_INDEX_chamomileSigil ? PC_PQ_SIGIL
        : m->id >= VS_ITEMNAME_BIN_INDEX_bronzeKey               ? PC_PQ_KEY
                                                                 : PC_PQ_ITEM;
}

static void weapon_set(pc_pq_set* s, int i)
{
    vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[i];
    int k;
    memset(s, 0, sizeof *s);
    blank(&s->whole, PC_PQ_WEAPON, i + 1);
    s->whole.id = w->blade;
    from_details(&s->whole, 0, i);
    blade_word(&s->whole, (u_char)vs_main_inventory.blades[w->blade - 1].category);
    s->whole.equipped = w->isEquipped != 0;
    s->whole.slot = w->isEquipped ? PC_PQ_SLOT_WEAPON : -1;
    s->whole.quick = pc_weapon_slots_get(i);
    blade_part(&s->blade, w->blade - 1);
    if (w->grip)
        grip_part(&s->grip, w->grip - 1);
    else
        blank(&s->grip, PC_PQ_GRIP, 0);
    for (k = 0; k < PC_PQ_GEM_SLOTS && k < s->whole.gem_slots; ++k)
        if (w->gems[k] && w->gems[k] <= capacities[5])
            gem_part(&s->gems[s->gem_count++], w->gems[k] - 1);
    s->blade.equipped = s->grip.equipped = s->whole.equipped;
    for (k = 0; k < s->gem_count; ++k)
        s->gems[k].equipped = s->whole.equipped;
}

static void shield_set(pc_pq_set* s, int i)
{
    vs_main_inventoryShield* sh = &vs_main_inventory.shields[i];
    int k;
    memset(s, 0, sizeof *s);
    blank(&s->whole, PC_PQ_SHIELD, i + 1);
    s->whole.id = sh->base.id;
    from_details(&s->whole, 3, i);
    armor_word(&s->whole, (u_char)sh->base.category);
    s->whole.gem_slots = sh->base.gemSlots;
    s->whole.equipped = sh->isEquipped != 0;
    s->whole.slot = sh->isEquipped ? PC_PQ_SLOT_SHIELD : -1;
    for (k = 0; k < PC_PQ_GEM_SLOTS && k < sh->base.gemSlots; ++k)
        if (sh->gems[k] && sh->gems[k] <= capacities[5]) {
            gem_part(&s->gems[s->gem_count], sh->gems[k] - 1);
            s->gems[s->gem_count++].equipped = s->whole.equipped;
        }
}

/* ---- the bag ---- */

static pc_pq_bag bag HOST;
static int bag_valid HOST = 0;

static u_char* list_of(int c)
{
    switch (c) {
    case 0:
        return vs_main_inventoryIndices.weapons;
    case 1:
        return vs_main_inventoryIndices.blades;
    case 2:
        return vs_main_inventoryIndices.grips;
    case 3:
        return vs_main_inventoryIndices.shields;
    case 4:
        return vs_main_inventoryIndices.armor;
    case 5:
        return vs_main_inventoryIndices.gems;
    default:
        return vs_main_inventoryIndices.misc;
    }
}

/* Category c's records (1-based) in the order the menus list them: the list as
 * vs_mainMenu_rebuildInventory leaves it (equip_bridge.c rebuild_indices, which
 * MENU3's Items menu runs as it starts: empty records' entries out, records the
 * list lacks appended), worked out on a copy (the game's list is not written). */
static int menu_order(int c, int* rows)
{
    u_char* list = list_of(c);
    int cap = capacities[c], n = 0, i, j;
    for (i = 0; i < cap; ++i)
        if (list[i] && list[i] <= cap && pc_ui_item_record_id(c, list[i] - 1)) {
            for (j = 0; j < n && rows[j] != list[i]; ++j)
                ;
            if (j == n)
                rows[n++] = list[i];
        }
    for (i = 0; i < cap; ++i)
        if (pc_ui_item_record_id(c, i)) {
            for (j = 0; j < n && rows[j] != i + 1; ++j)
                ;
            if (j == n)
                rows[n++] = i + 1;
        }
    return n;
}

static unsigned bag_key(vs_battle_actor2* a)
{
    unsigned h = hash_bytes(2166136261u, &vs_main_inventory, sizeof vs_main_inventory);
    int k;
    h = hash_bytes(h, &vs_main_inventoryIndices, sizeof vs_main_inventoryIndices);
    h = hash_bytes(h, &a->weapon, sizeof a->weapon);
    h = hash_bytes(h, &a->shield, sizeof a->shield);
    h = hash_bytes(h, &a->accessory, sizeof a->accessory);
    for (k = 0; k < 6; ++k)
        h = hash_bytes(h, &a->limbs[k].armor, sizeof a->limbs[k].armor);
    for (k = 0; k < 8; ++k) {
        int q = pc_weapon_slots_get(k);
        h = hash_bytes(h, &q, sizeof q);
    }
    return h;
}

/* The armor worn on limb (1..5, 7): the first in the list order, as MENU3 finds it. */
static int worn_on(int limb)
{
    int rows[16], n = menu_order(4, rows), i;
    for (i = 0; i < n; ++i)
        if (vs_main_inventory.armor[rows[i] - 1].limb == limb)
            return rows[i];
    return 0;
}

static void fill_bag(void)
{
    int c, i, index;
    memset(&bag, 0, sizeof bag);
    bag.valid = 1;
    for (c = 0; c < PC_PQ_BAG_CATEGORIES; ++c) {
        int rows[64], n = menu_order(c, rows);
        bag.capacity[c] = capacities[c];
        for (i = 0; i < n; ++i) {
            index = rows[i] - 1;
            ++bag.count[c];
            switch (c) {
            case 0:
                weapon_set(&bag.weapons[bag.weapon_count++], index);
                break;
            case 1:
                blade_part(&bag.blades[bag.blade_count++], index);
                break;
            case 2:
                grip_part(&bag.grips[bag.grip_count++], index);
                break;
            case 3:
                shield_set(&bag.shields[bag.shield_count++], index);
                break;
            case 4:
                armor_part(&bag.armor[bag.armor_count++], index);
                break;
            case 5:
                gem_part(&bag.gems[bag.gem_count++], index);
                break;
            default:
                misc_part(&bag.misc[bag.misc_count++], index);
                break;
            }
        }
    }
    /* parts of an equipped weapon / shield */
    for (i = 0; i < bag.blade_count; ++i)
        if (bag.blades[i].weapon && vs_main_inventory.weapons[bag.blades[i].weapon - 1].isEquipped)
            bag.blades[i].equipped = 1;
    for (i = 0; i < bag.grip_count; ++i)
        if (bag.grips[i].weapon && vs_main_inventory.weapons[bag.grips[i].weapon - 1].isEquipped)
            bag.grips[i].equipped = 1;
    for (i = 0; i < bag.gem_count; ++i)
        if ((bag.gems[i].weapon && vs_main_inventory.weapons[bag.gems[i].weapon - 1].isEquipped)
            || (bag.gems[i].shield && vs_main_inventory.shields[bag.gems[i].shield - 1].isEquipped))
            bag.gems[i].equipped = 1;
    /* the Equipment screen's slots (equip_bridge.c) */
    for (i = 0; i < 8; ++i)
        if (vs_main_inventory.weapons[i].isEquipped && vs_main_inventory.weapons[i].blade) {
            bag.equipped[PC_PQ_SLOT_WEAPON] = i + 1;
            break;
        }
    for (i = 0; i < 8; ++i)
        if (vs_main_inventory.shields[i].isEquipped && vs_main_inventory.shields[i].base.id) {
            bag.equipped[PC_PQ_SLOT_SHIELD] = i + 1;
            break;
        }
    for (i = PC_PQ_SLOT_RIGHT_ARM; i <= PC_PQ_SLOT_LEGS; ++i)
        bag.equipped[i] = worn_on(i - 1);
    bag.equipped[PC_PQ_SLOT_ACCESSORY] = worn_on(ACCESSORY_LIMB);
}

const pc_pq_bag* pc_player_bag(void)
{
    vs_battle_actor2* a = ashley();
    unsigned key;
    if (!a || vs_battle_roomData.unk0 != 1)
        return NULL;
    key = bag_key(a);
    if (!bag_valid || bag.key != key) {
        if (!guard_begin())
            return NULL;
        sync_begin();
        fill_bag();
        sync_end();
        guard_end();
        bag.key = key;
        bag_valid = 1;
    }
    return &bag;
}

/* ---- the workshop ---- */

static int workshop_now(void)
{
    if (vs_battle_roomData.unk0 != 1 || psx_get(vs_battle_roomData.sectionB) == NULL)
        return 0;
    return vs_battle_inWorkshop();
}

int pc_player_workshop(pc_pq_workshop* out)
{
    const _mpdRoomDoorSection_t* location;
    char area[64];
    int m;
    memset(out, 0, sizeof *out);
    out->workshop = workshop_now();
    if (!out->workshop)
        return 0;
    out->materials = (unsigned)pc_menuC_workshop_materials(out->workshop);
    location = D_800F1AB0.zndId ? &D_800F1AB0 : &D_80061068;
    if (pc_game_query_room_names)
        pc_game_query_room_names(location->zndId, location->mpdId, area, sizeof area, out->name, sizeof out->name);
    for (m = 1; m < 8; ++m)
        if ((out->materials >> m) & 1)
            copy_text(out->material_names[out->material_count++], sizeof out->material_names[0],
                pc_ui_material_name(m));
    return out->workshop;
}

/* ---- the combine ---- */

static unsigned char* syd_data[3] HOST = { NULL, NULL, NULL };
static unsigned syd_size[3] HOST = { 0, 0, 0 };
static int syd_state[3] HOST = { 0, 0, 0 }; /* 0 not read, 1 read, -1 failed */

static const unsigned char* syd(int kind)
{
    static const char* const paths[3] = { "MENU/BLADE.SYD", "MENU/SHIELD.SYD", "MENU/ARMOR.SYD" };
    if (!syd_state[kind]) {
        syd_state[kind] = pc_ui_disc_read(paths[kind], &syd_data[kind], &syd_size[kind]) && syd_size[kind] >= 12 ? 1 : -1;
    }
    return syd_state[kind] > 0 ? syd_data[kind] : NULL;
}

static u_int syd_word(const unsigned char* p, int i)
{
    p += i * 4;
    return p[0] | p[1] << 8 | p[2] << 16 | (u_int)p[3] << 24;
}

/* The table entries the game's combine will read lie in the file, and give a
 * material its modifiers table has (the game indexes them unchecked; a
 * corrupt save must not make us read past them). Only bounds: the combine
 * itself is the game's. */
static int syd_ok(int kind, long material_at, long combination_at, int record_size)
{
    const unsigned char* p = syd_data[kind];
    u_int size = syd_size[kind], init = syd_word(p, 2);
    int material, record;
    if (material_at < 0 || combination_at < 0 || (u_long)material_at >= size || (u_long)combination_at >= size)
        return 0;
    material = (char)p[material_at];
    record = (u_char)p[combination_at];
    return material >= 0 && material < 8 && init <= size && (size - init) / record_size > (u_int)record;
}

static void result_words(pc_pq_part* p)
{
    p->dp = (p->dp + 99) / 100;
    p->dp_max = (p->dp_max + 99) / 100;
}

static void blade_result(pc_pq_part* p, const vs_main_inventoryBlade* b, int record)
{
    int k;
    blank(p, PC_PQ_BLADE, record);
    p->id = b->id;
    copy_text(p->name, sizeof p->name, pc_ui_item_name(b->id));
    copy_text(p->material, sizeof p->material, pc_ui_material_name(b->material));
    blade_word(p, (u_char)b->category);
    p->dp = b->currentDp;
    p->dp_max = b->maxDp;
    p->pp = b->currentPp;
    p->pp_max = b->maxPp;
    result_words(p);
    p->str = b->strength;
    p->intel = b->intelligence;
    p->agl = b->agility;
    for (k = 0; k < PC_PQ_CLASSES; ++k)
        p->classes[k] = b->classes[k];
    for (k = 0; k < PC_PQ_AFFINITIES; ++k)
        p->affinities[k] = b->affinities[k];
    p->damage_type = b->damageType >= 1 && b->damageType <= 3 ? b->damageType - 1 : -1;
    p->range = (u_char)b->range.range;
    p->risk = (u_char)b->cost;
    p->two_handed = two_handed_category((u_char)b->category);
}

static void armor_result(pc_pq_part* p, const vs_main_inventoryArmor* a, int kind, int record)
{
    int k;
    blank(p, kind, record);
    p->id = a->id;
    copy_text(p->name, sizeof p->name, pc_ui_item_name(a->id));
    copy_text(p->material, sizeof p->material, pc_ui_material_name(a->material));
    armor_word(p, (u_char)a->category);
    p->dp = a->currentDp;
    p->dp_max = a->maxDp;
    if (kind == PC_PQ_SHIELD) {
        p->pp = a->currentPp;
        p->pp_max = a->maxPp;
        p->gem_slots = a->gemSlots;
    }
    result_words(p);
    p->str = a->strength;
    p->intel = a->intelligence;
    p->agl = a->agility;
    for (k = 0; k < PC_PQ_CLASSES; ++k)
        p->classes[k] = a->classes[k];
    for (k = 0; k < PC_PQ_AFFINITIES; ++k)
        p->affinities[k] = a->affinities[k];
    for (k = 0; k < PC_PQ_TYPES; ++k)
        p->types[k] = a->types[k + 1];
}

static int material_of(int kind, int r)
{
    switch (kind) {
    case PC_PQ_BLADE:
        return vs_main_inventory.blades[r].material;
    case PC_PQ_SHIELD:
        return vs_main_inventory.shields[r].base.material;
    default:
        return vs_main_inventory.armor[r].material;
    }
}

static void say(char* why, int size, const char* text)
{
    if (why && size > 0)
        copy_text(why, size, text);
}

static void game_line(char* out, int size, int offset)
{
    pc_ui_text_decode((const unsigned char*)&menu12_text[offset], 512, out, size);
}

int pc_player_combine(int kind, int a, int b, pc_pq_combine* out, char* why, int why_size)
{
    int cap, w, k;
    const unsigned char* data;
    say(why, why_size, "");
    if (!ashley() || vs_battle_roomData.unk0 != 1) {
        say(why, why_size, "no player");
        return 0;
    }
    if (kind != PC_PQ_BLADE && kind != PC_PQ_SHIELD && kind != PC_PQ_ARMOR) {
        say(why, why_size, "the game combines blades, shields or armor");
        return 0;
    }
    cap = kind == PC_PQ_BLADE ? 16 : kind == PC_PQ_SHIELD ? 8 : 16;
    if (a < 1 || b < 1 || a > cap || b > cap) {
        say(why, why_size, "no such record");
        return 0;
    }
    if (a == b) {
        say(why, why_size, "the same record twice");
        return 0;
    }
    k = kind == PC_PQ_BLADE ? 0 : kind == PC_PQ_SHIELD ? 1 : 2;
    data = syd(k);
    if (!data) {
        say(why, why_size, "the combination tables cannot be read");
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->kind = kind;
    out->a = a;
    out->b = b;
    if (kind == PC_PQ_BLADE) {
        vs_main_inventoryBlade first, second;
        sync_begin();
        first = vs_main_inventory.blades[a - 1];
        second = vs_main_inventory.blades[b - 1];
        sync_end();
        vs_main_inventoryBlade buf;
        if (!first.id || !second.id) {
            say(why, why_size, "no such blade");
            return 0;
        }
        if (!syd_ok(0,
                (long)syd_word(data, 1) + ((int)first.material - 3) * 500 + (u_char)first.category * 50
                    + ((int)second.material - 3) * 10 + (u_char)second.category,
                (long)syd_word(data, 0) + first.id * 96 + second.id, 16)) {
            say(why, why_size, "not in the combination tables");
            return 0;
        }
        /* _setCombineBladeUi, both set (2585) */
        vs_battle_rMemzero(&buf, sizeof buf);
        pc_menuC_combine(0, &first, &second, &buf, (void*)data);
        buf.assembledWeaponIndex = 0;
        buf.index = a; /* the commit (3097): the first's record */
        blade_result(&out->result, &buf, a);
        memcpy(out->raw, &buf, sizeof buf);
        out->raw_size = sizeof buf;
    } else if (kind == PC_PQ_SHIELD) {
        vs_main_inventoryShield first, second;
        sync_begin();
        first = vs_main_inventory.shields[a - 1];
        second = vs_main_inventory.shields[b - 1];
        sync_end();
        vs_main_inventoryShield buf;
        if (!first.base.id || !second.base.id) {
            say(why, why_size, "no such shield");
            return 0;
        }
        if (!syd_ok(1, (long)syd_word(data, 1) + first.base.material * 8 + second.base.material,
                (long)syd_word(data, 0) + first.base.subId * 17 + second.base.subId, 8)) {
            say(why, why_size, "not in the combination tables");
            return 0;
        }
        /* _initCombineShields, both set (3180) */
        vs_battle_rMemzero(&buf, sizeof buf);
        pc_menuC_combine(1, &first.base, &second.base, &buf.base, (void*)data);
        buf.isEquipped = 0;
        buf.index = a; /* the commit (3688) */
        armor_result(&out->result, &buf.base, PC_PQ_SHIELD, a);
        memcpy(out->raw, &buf, sizeof buf);
        out->raw_size = sizeof buf;
    } else {
        vs_main_inventoryArmor first, second;
        sync_begin();
        first = vs_main_inventory.armor[a - 1];
        second = vs_main_inventory.armor[b - 1];
        sync_end();
        vs_main_inventoryArmor buf;
        if (!first.id || !second.id) {
            say(why, why_size, "no such armor");
            return 0;
        }
        if (first.category == 7 || second.category == 7) {
            say(why, why_size, "accessories are not combined");
            return 0;
        }
        if (!syd_ok(2,
                (long)syd_word(data, 1) + first.material * 128 + ((int)(u_char)first.category - 2) * 32
                    + second.material * 4 + ((int)(u_char)second.category - 2),
                (long)syd_word(data, 0) + ((int)first.subId - 16) * 65 + ((int)second.subId - 16), 8)) {
            say(why, why_size, "not in the combination tables");
            return 0;
        }
        /* _selectShields (the armor's), both set (3751) */
        vs_battle_rMemzero(&buf, sizeof buf);
        pc_menuC_combine(2, &first, &second, &buf, (void*)data);
        buf.limb = 0;
        buf.index = a; /* the commit (4171) */
        armor_result(&out->result, &buf, PC_PQ_ARMOR, a);
        memcpy(out->raw, &buf, sizeof buf);
        out->raw_size = sizeof buf;
    }
    /* MENUC's greying: outside a workshop no Combine (notInWorkshop); a material the
     * workshop does not work (invalid*Material) */
    w = workshop_now();
    out->allowed = 1;
    if (!w) {
        out->allowed = 0;
        game_line(out->why, sizeof out->why, VS_MENU12_BIN_OFFSET_notInWorkshop);
    } else {
        int works = pc_menuC_workshop_materials(w);
        if (!((works >> material_of(kind, a - 1)) & 1) || !((works >> material_of(kind, b - 1)) & 1)) {
            out->allowed = 0;
            game_line(out->why, sizeof out->why,
                kind == PC_PQ_BLADE        ? VS_MENU12_BIN_OFFSET_invalidBladeMaterial
                    : kind == PC_PQ_SHIELD ? VS_MENU12_BIN_OFFSET_invalidShieldMaterial
                                           : VS_MENU12_BIN_OFFSET_invalidArmorMaterial);
        }
    }
    return 1;
}

int pc_player_craftable(pc_pq_combine* out, int max)
{
    static const int kinds[3] = { PC_PQ_BLADE, PC_PQ_SHIELD, PC_PQ_ARMOR };
    static const int categories[3] = { 1, 3, 4 };
    int n = 0, k, i, j, w = workshop_now();
    if (!w || !ashley())
        return 0;
    for (k = 0; k < 3; ++k) {
        /* MENUC's lists: the category in the menus' order (armor: not accessories) */
        int listed[16], rows[16], count = 0, all = menu_order(categories[k], listed);
        for (i = 0; i < all; ++i)
            if (kinds[k] != PC_PQ_ARMOR || vs_main_inventory.armor[listed[i] - 1].category != 7)
                rows[count++] = listed[i];
        for (i = 0; i < count; ++i)
            for (j = 0; j < count; ++j) {
                char why[8];
                if (i == j || n >= max)
                    continue;
                if (pc_player_combine(kinds[k], rows[i], rows[j], &out[n], why, sizeof why) && out[n].allowed)
                    ++n;
            }
    }
    return n;
}
