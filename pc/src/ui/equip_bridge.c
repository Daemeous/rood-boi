/* Modern menus, Equipment screen: the game side (battle builds). Called by
 * menu_bridge.c on every tick of the shell's top level:
 *
 *   pc_ui_equip_begin()     the model reload's progress (once per tick), then
 *                           the snapshot (ui_model.h) for pc_ui_shell_set_equipment
 *   pc_ui_shell_tick()
 *   pc_ui_equip_requests()  drains pc_ui_shell_pop_request and executes (the
 *                           Abilities and Magick screens' requests go to
 *                           abilities_bridge.c and magick_bridge.c)
 *
 * Equipping is MENU3's (16C.c) own sequence on the resident BATTLE routines,
 * in its order:
 *   weapon (_equipWeaponMenu)   vs_mainMenu_unequipWeapon (equipWeapon(NULL),
 *                               every isEquipped cleared); the new one's flag;
 *                               two-handed: vs_mainMenu_unequipShield;
 *                               vs_battle_equipWeapon
 *   shield (_equipShieldMenu)   vs_mainMenu_unequipShield; the flag; the actor's
 *                               weapon two-handed: vs_mainMenu_unequipWeapon;
 *                               vs_battle_equipShield
 *   armor (_equipArmorMenu)     the worn one's limb = 0; limb = body part,
 *                               vs_battle_equipArmor(part - 1, a) (NULL: off)
 *   accessory                   the worn one's limb = 0; limb = 7,
 *                               vs_battle_equipAccessory(a) (NULL: off)
 * (vs_mainMenu_unequip* are MAINMENU's, which may not be loaded yet: their two
 * steps are repeated here.) Weapons and shields then reload actor 0's models
 * through weapon_slots.c (pc_weapon_slots_menu_models) where MENU3 runs
 * _loadObjectData; nothing reloads when neither model changed. While the
 * load runs the snapshot says busy and every equip is refused.
 *
 * Only BATTLE routines are called: MAINMENU.PRG is not loaded during the
 * menu's first ticks (BATTLE func_800C97BC runs the top level before it).
 * Weapon and shield equips still wait for it (pc_ui_equip_begin's resident:
 * the snapshot says busy until then): the model reload's queue would run
 * beside MAINMENU's own load, before its vs_mainMenu_initInventory. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/2842C.h"
#include "../../../src/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/SLUS_010.40/main.h"
#include "item_names.h"
#include "ui_shell.h"

/* ui_text.h (stdio in a game-header unit is avoided) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);
/* weapon_slots.c */
extern int pc_qol_weapon_slots; /* project_settings.cpp: the field controls (Quick swap's notice) */
int pc_weapon_slots_menu_models(vs_main_inventoryWeapon* w, vs_main_inventoryShield* shield);
int pc_weapon_slots_menu_busy(void);
int pc_weapon_slots_menu_can_load(void);
int pc_weapon_slots_get(int weapon_index);
int pc_weapon_slots_shield_for(int slot);
void pc_weapon_slots_set(int slot, int weapon_index, int shield_index);
void pc_weapon_slots_clear(int slot);

int pc_ui_abilities_request(const pc_ui_request* r); /* abilities_bridge.c */
int pc_ui_magick_request(const pc_ui_request* r);    /* magick_bridge.c */

int pc_ui_equip_begin(int resident);
void pc_ui_equip_requests(void);
int pc_ui_equip_busy(void);

#define N_WEAPONS 8
#define N_SHIELDS 8
#define N_ARMOR 16
#define ACCESSORY_LIMB 7

static pc_ui_equipment snap;
static int loading;  /* a model reload of ours is under way */
static int resident; /* MAINMENU.PRG drives the top level (menu_bridge.c mainmenu_resident) */

/* Armor category per body part (MENU3 _equipArmorMenu bodyPartCategories):
 * 1 right arm, 2 left arm, 3 head, 4 body, 5 legs, 7 accessory. */
static const char part_category[8] = { 0, 5, 5, 2, 3, 4, 6, 7 };

static vs_battle_actor2* ashley(void)
{
    return vs_battle_characterState ? psx_get(vs_battle_characterState->unk3C) : NULL;
}

/* vs_mainMenu_weaponHands: two-handed blade categories. */
static int two_handed_category(int category) { return category >= 1 && ((0x1D4 >> (category - 1)) & 1); }

static int equipped_weapon(void)
{
    int i;
    for (i = 0; i < N_WEAPONS; ++i)
        if (vs_main_inventory.weapons[i].isEquipped && vs_main_inventory.weapons[i].blade)
            return i;
    return -1;
}
static int equipped_shield(void)
{
    int i;
    for (i = 0; i < N_SHIELDS; ++i)
        if (vs_main_inventory.shields[i].isEquipped && vs_main_inventory.shields[i].base.id)
            return i;
    return -1;
}
/* The armor worn on limb (1..5, 7 accessory): the first in the game's list
 * order, as MENU3 finds it. */
static int worn_on(int limb)
{
    int i, index;
    for (i = 0; i < N_ARMOR; ++i) {
        index = vs_main_inventoryIndices.armor[i];
        if (index && index <= N_ARMOR && vs_main_inventory.armor[index - 1].limb == limb)
            return index - 1;
    }
    return -1;
}

/* DP as the menus show it (MAINMENU vs_mainMenu_setDpPp: hundredths rounded
 * up); PP is shown as stored. */
static int dp_shown(int dp) { return (dp + 99) / 100; }

/* UTF-8 text into out, cut at a code point boundary when it does not fit. */
static void copy_name(char* out, int size, const char* text)
{
    int i;
    for (i = 0; i + 1 < size && text[i]; ++i)
        out[i] = text[i];
    if (((unsigned char)text[i] & 0xC0) == 0x80) { /* cut inside a code point: drop its first bytes */
        while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80)
            --i;
        if (i > 0)
            --i; /* its lead byte */
    }
    out[i] = 0;
}

static void blank(pc_ui_item* it)
{
    vs_main_bzero(it, sizeof *it);
    it->inventory = -1;
    it->damage_type = -1;
}

static void class_affinity(pc_ui_item* it, const vs_battle_classAffinityCurrent* ca)
{
    int i;
    for (i = 0; i < PC_UI_CLASSES; ++i)
        it->classes[i] = ca->class[0][i];
    for (i = 0; i < PC_UI_AFFINITIES; ++i)
        it->affinities[i] = (short)ca->affinity[0][i];
}

/* Weapon inventory index i; the equipped one from Ashley's record (current
 * values), the others computed (vs_battle_applyWeapon into a local). */
static void weapon_item(pc_ui_item* it, int i, vs_battle_actor2* actor)
{
    vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[i];
    vs_battle_uiWeapon local;
    vs_battle_uiWeapon* u = &local;
    vs_main_inventoryBlade* blade = &vs_main_inventory.blades[w->blade - 1];

    blank(it);
    if (w->isEquipped && actor)
        u = &actor->weapon;
    else
        vs_battle_applyWeapon(&local, w);
    it->inventory = i;
    pc_ui_text_decode((const unsigned char*)w->name, sizeof w->name, it->name, sizeof it->name);
    copy_name(it->material, sizeof it->material, pc_ui_material_name(blade->material));
    it->dp = dp_shown(u->currentDp);
    it->dp_max = dp_shown(u->maxDp);
    it->pp = u->currentPp;
    it->pp_max = u->maxPp;
    it->str = u->currentStr;
    it->intel = u->currentInt;
    it->agl = u->currentAgility;
    class_affinity(it, &u->classAffinityCurrent);
    /* the game's 1 blunt, 2 edged, 3 piercing (MAINMENU _renderStatPage: damageType - 1) */
    it->damage_type = u->damageType >= 1 && u->damageType <= 3 ? u->damageType - 1 : -1;
    it->two_handed = two_handed_category((u_char)blade->category);
    it->quick = pc_weapon_slots_get(i);
}

static void shield_item(pc_ui_item* it, int i, vs_battle_actor2* actor)
{
    vs_main_inventoryShield* sh = &vs_main_inventory.shields[i];
    vs_battle_uiShield local;
    vs_battle_uiShield* u = &local;

    blank(it);
    if (sh->isEquipped && actor)
        u = &actor->shield;
    else
        vs_battle_applyShield(&local, sh);
    it->inventory = i;
    copy_name(it->name, sizeof it->name, pc_ui_item_name(sh->base.id));
    copy_name(it->material, sizeof it->material, pc_ui_material_name(sh->base.material));
    it->dp = dp_shown(u->currentDp);
    it->dp_max = dp_shown(u->maxDp);
    it->pp = u->currentPp;
    it->pp_max = u->maxPp;
    it->str = u->currentStr;
    it->intel = u->currentInt;
    it->agl = u->currentAgility;
    class_affinity(it, &u->classAffinityCurrent);
}

/* Armor (limb 1..5 in the record) or an accessory (category 7). */
static void armor_item(pc_ui_item* it, int i, vs_battle_actor2* actor)
{
    vs_main_inventoryArmor* a = &vs_main_inventory.armor[i];
    int k;

    blank(it);
    it->inventory = i;
    copy_name(it->name, sizeof it->name, pc_ui_item_name(a->id));
    copy_name(it->material, sizeof it->material, pc_ui_material_name(a->material));
    if (a->category == 7) {
        vs_battle_uiAccessory local;
        vs_battle_uiAccessory* u = &local;
        if (a->limb == ACCESSORY_LIMB && actor)
            u = &actor->accessory;
        else
            vs_battle_applyAccessory(&local, a);
        it->str = u->currentStr;
        it->intel = u->currentInt;
        it->agl = u->currentAgility;
        for (k = 0; k < PC_UI_CLASSES; ++k)
            it->classes[k] = (short)u->classes[k];
        for (k = 0; k < PC_UI_AFFINITIES; ++k)
            it->affinities[k] = (short)u->affinities[k];
    } else {
        vs_battle_uiArmor local;
        vs_battle_uiArmor* u = &local;
        if (a->limb >= 1 && a->limb <= 5 && actor)
            u = &actor->limbs[a->limb - 1].armor;
        else
            vs_battle_applyArmor(&local, a);
        it->dp = dp_shown(u->currentDp);
        it->dp_max = dp_shown(u->maxDp);
        it->str = u->currentStr;
        it->intel = u->currentInt;
        it->agl = u->currentAgility;
        class_affinity(it, &u->classAffinityCurrent);
    }
}

/* What may go on slot (PC_UI_SLOT_*): armor inventory record a. */
static int armor_fits(int slot, vs_main_inventoryArmor* a)
{
    int limb = slot == PC_UI_SLOT_ACCESSORY ? ACCESSORY_LIMB : slot - 1;
    return a->id && a->category == part_category[limb] && (a->limb == 0 || a->limb == limb);
}

/* Candidates for slot in the game's list order (vs_main_inventoryIndices,
 * 1-based): every weapon / shield; armor of the slot's category that is worn
 * nowhere or on this slot. */
static void fill_candidates(int slot, vs_battle_actor2* actor)
{
    int i, index, n = 0;

    snap.slot = slot;
    if (slot == PC_UI_SLOT_WEAPON) {
        for (i = 0; i < N_WEAPONS && n < PC_UI_CANDIDATES; ++i) {
            index = vs_main_inventoryIndices.weapons[i];
            if (index && index <= N_WEAPONS && vs_main_inventory.weapons[index - 1].blade)
                weapon_item(&snap.candidates[n++], index - 1, actor);
        }
    } else if (slot == PC_UI_SLOT_SHIELD) {
        for (i = 0; i < N_SHIELDS && n < PC_UI_CANDIDATES; ++i) {
            index = vs_main_inventoryIndices.shields[i];
            if (index && index <= N_SHIELDS && vs_main_inventory.shields[index - 1].base.id)
                shield_item(&snap.candidates[n++], index - 1, actor);
        }
    } else if (slot > PC_UI_SLOT_SHIELD && slot < PC_UI_EQUIP_SLOTS) {
        for (i = 0; i < N_ARMOR && n < PC_UI_CANDIDATES; ++i) {
            index = vs_main_inventoryIndices.armor[i];
            if (index && index <= N_ARMOR && armor_fits(slot, &vs_main_inventory.armor[index - 1]))
                armor_item(&snap.candidates[n++], index - 1, actor);
        }
    } else {
        snap.slot = -1;
    }
    snap.count = n;
}

/* The candidate list costs a vs_battle_apply* (heap allocations) per item:
 * it is recomputed only when the wanted slot or what it is computed from
 * changes (the inventory, its list order, Ashley's equipment records). */
static struct {
    int valid, wanted, slot, count;
    unsigned key;
} cache;

static unsigned hash_bytes(unsigned h, const void* data, unsigned size)
{
    const unsigned char* p = data;
    while (size--)
        h = (h ^ *p++) * 16777619u; /* FNV-1a */
    return h;
}

static unsigned candidates_key(vs_battle_actor2* actor)
{
    unsigned h = hash_bytes(2166136261u, &vs_main_inventory, sizeof vs_main_inventory);
    int k;
    h = hash_bytes(h, &vs_main_inventoryIndices, sizeof vs_main_inventoryIndices);
    h = hash_bytes(h, &actor->weapon, sizeof actor->weapon);
    h = hash_bytes(h, &actor->shield, sizeof actor->shield);
    h = hash_bytes(h, &actor->accessory, sizeof actor->accessory);
    for (k = 0; k < 6; ++k)
        h = hash_bytes(h, &actor->limbs[k].armor, sizeof actor->limbs[k].armor);
    return h;
}

static void candidates(int wanted, vs_battle_actor2* actor)
{
    unsigned key = candidates_key(actor);
    if (!cache.valid || cache.wanted != wanted || cache.key != key) {
        fill_candidates(wanted, actor);
        cache.valid = 1;
        cache.wanted = wanted;
        cache.key = key;
        cache.slot = snap.slot;
        cache.count = snap.count;
    } else { /* snap.candidates still holds them */
        snap.slot = cache.slot;
        snap.count = cache.count;
    }
}

static void fill(void)
{
    vs_battle_actor2* actor = ashley();
    int i, k;

    for (i = 0; i < PC_UI_EQUIP_SLOTS; ++i)
        blank(&snap.equipped[i]);
    snap.slot = -1;
    snap.count = 0;
    snap.valid = actor != NULL;
    /* busy: no equip now (the shell refuses Confirm and Square): a model
     * reload runs, or MAINMENU is not resident yet (its load and
     * vs_mainMenu_initInventory still to come; weapons and shields are
     * refused below too, and the first few ticks of a session lose nothing) */
    snap.busy = loading || !resident;
    snap.quick_enabled = pc_qol_weapon_slots != 0;
    if (!actor)
        return;
    snap.str = actor->strength;
    snap.intel = actor->intelligence;
    snap.agl = actor->agility;

    if ((k = equipped_weapon()) >= 0)
        weapon_item(&snap.equipped[PC_UI_SLOT_WEAPON], k, actor);
    if ((k = equipped_shield()) >= 0)
        shield_item(&snap.equipped[PC_UI_SLOT_SHIELD], k, actor);
    for (i = PC_UI_SLOT_RARM; i <= PC_UI_SLOT_ACCESSORY; ++i)
        if ((k = worn_on(i == PC_UI_SLOT_ACCESSORY ? ACCESSORY_LIMB : i - 1)) >= 0)
            armor_item(&snap.equipped[i], k, actor);

    for (i = 0; i < PC_UI_QUICK; ++i) {
        snap.quick_weapon[i] = snap.quick_shield[i] = -1;
        snap.quick_weapon_name[i][0] = snap.quick_shield_name[i][0] = 0;
    }
    for (k = 0; k < N_WEAPONS; ++k) {
        int q = pc_weapon_slots_get(k);
        if (q >= 1 && q <= PC_UI_QUICK && snap.quick_weapon[q - 1] < 0) {
            vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[k];
            snap.quick_weapon[q - 1] = k;
            pc_ui_text_decode((const unsigned char*)w->name, sizeof w->name, snap.quick_weapon_name[q - 1],
                sizeof snap.quick_weapon_name[q - 1]);
        }
    }
    for (i = 0; i < PC_UI_QUICK; ++i) {
        k = snap.quick_weapon[i] >= 0 ? pc_weapon_slots_shield_for(i + 1) : -1;
        snap.quick_shield[i] = k;
        if (k >= 0)
            copy_name(snap.quick_shield_name[i], sizeof snap.quick_shield_name[i],
                pc_ui_item_name(vs_main_inventory.shields[k].base.id));
    }

    i = pc_ui_shell_wanted_candidates();
    if (i >= 0)
        candidates(i, actor);
    else
        cache.valid = 0; /* each list computes afresh when it opens */
}

/* ---- The bag's list order ---- */

/* MAINMENU _getItemId: the record's id (0 = an empty record). */
static int item_id(int category, int i)
{
    switch (category) {
    case 0: return vs_main_inventory.weapons[i].blade;
    case 1: return vs_main_inventory.blades[i].id;
    case 2: return vs_main_inventory.grips[i].id;
    case 3: return vs_main_inventory.shields[i].base.id;
    case 4: return vs_main_inventory.armor[i].id;
    case 5: return vs_main_inventory.gems[i].id;
    default: return vs_main_inventory.misc[i].id;
    }
}

/* vs_mainMenu_rebuildInventory (MAINMENU 2D10.c), MAINMENU's own steps: list
 * entries of empty records out, the list packed, records missing from it
 * appended. MENU3's Items menu runs it for all seven categories each time it
 * starts (items2.h _itemNavigation), before its equip page lists anything:
 * a record the game added without listing it (e.g. a shield in the bag of a
 * save) shows from then on, and the indices stay that way in RAM. */
static void rebuild_indices(int category)
{
    static u_char* const lists[7] = { vs_main_inventoryIndices.weapons, vs_main_inventoryIndices.blades,
        vs_main_inventoryIndices.grips, vs_main_inventoryIndices.shields, vs_main_inventoryIndices.armor,
        vs_main_inventoryIndices.gems, vs_main_inventoryIndices.misc };
    static const u_char capacities[7] = { 8, 16, 16, 8, 16, 48, 64 };
    u_char* indices = lists[category];
    int capacity = capacities[category];
    int i, j, k;

    /* Divergence: an entry above the capacity (a corrupt save) is left as it
     * is; MAINMENU would read past the category's records for it. */
    for (i = 0; i < capacity; ++i)
        if (indices[i] && indices[i] <= capacity && !item_id(category, indices[i] - 1))
            indices[i] = 0;
    i = 0;
    for (;;) {
        if (indices[i]) {
            if (++i == capacity - 1)
                break;
        } else {
            for (j = i + 1; j < capacity && !indices[j]; ++j)
                ;
            if (j == capacity)
                break;
            for (k = j; k < capacity; ++k)
                indices[k + i - j] = indices[k];
            for (k = k + (i - j); k < capacity; ++k)
                indices[k] = 0;
        }
    }
    for (i = 0; i < capacity; ++i) {
        if (item_id(category, i)) {
            for (j = 0; j < capacity && indices[j] != i + 1; ++j)
                ;
            if (j == capacity) {
                for (k = 0; k < capacity; ++k)
                    if (!indices[k]) {
                        indices[k] = i + 1;
                        break;
                    }
            }
        }
    }
}

int pc_ui_equip_begin(int mainmenu)
{
    pc_ui_equip_view view;
    int i;

    resident = mainmenu;

    if (loading)
        loading = pc_weapon_slots_menu_busy();
    /* The Equipment screen stands for MENU3's Items > Equip: the list order
     * as the Items menu leaves it (idempotent: every tick while it shows,
     * not when it is locked, as the Items command is then unselectable). */
    if (pc_ui_shell_visible() && pc_ui_shell_screen() == PC_UI_SCREEN_EQUIPMENT) {
        pc_ui_shell_equip_view(&view);
        if (!view.locked)
            for (i = 0; i < 7; ++i)
                rebuild_indices(i);
    }
    fill();
    pc_ui_shell_set_equipment(&snap);
    return loading;
}

int pc_ui_equip_busy(void) { return loading; }

/* ---- Requests ---- */

/* vs_mainMenu_unequipWeapon / vs_mainMenu_unequipShield (MAINMENU 2D10.c). */
static void unequip_weapon(void)
{
    int i;
    vs_battle_equipWeapon(NULL);
    for (i = 0; i < N_WEAPONS; ++i)
        vs_main_inventory.weapons[i].isEquipped = 0;
}
static void unequip_shield(void)
{
    int i;
    vs_battle_equipShield(NULL);
    for (i = 0; i < N_SHIELDS; ++i)
        vs_main_inventory.shields[i].isEquipped = 0;
}

/* What the weapon and shield model slots hold: the blade's model, category
 * and material, the shield's model and material (all 0 = none). */
typedef struct {
    int wep, category, material, shield, shield_material;
} models_t;

static models_t models_now(void)
{
    models_t m = { 0, 10, 0, 0, 0 };
    int k = equipped_weapon();
    if (k >= 0) {
        vs_main_inventoryBlade* b = &vs_main_inventory.blades[vs_main_inventory.weapons[k].blade - 1];
        m.wep = (u_char)b->wepId;
        m.category = (u_char)b->category;
        m.material = b->material;
    }
    k = equipped_shield();
    if (k >= 0) {
        m.shield = (u_char)vs_main_inventory.shields[k].base.wepId;
        m.shield_material = vs_main_inventory.shields[k].base.material;
    }
    return m;
}

/* MENU3 _equipWeaponMenu / _equipShieldMenu, then the models. item: inventory
 * index, -1 take off. Returns 1 when done. */
static int equip_hands(int slot, int item)
{
    models_t before, after;
    int k;

    if (!pc_weapon_slots_menu_can_load())
        return 0;
    if (item >= 0) {
        if (slot == PC_UI_SLOT_WEAPON ? item >= N_WEAPONS || !vs_main_inventory.weapons[item].blade
                                      : item >= N_SHIELDS || !vs_main_inventory.shields[item].base.id)
            return 0;
    }
    before = models_now();
    if (slot == PC_UI_SLOT_WEAPON) {
        unequip_weapon();
        if (item >= 0) {
            vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[item];
            w->isEquipped = 1;
            if (two_handed_category((u_char)vs_main_inventory.blades[w->blade - 1].category))
                unequip_shield();
            vs_battle_equipWeapon(w);
        }
    } else {
        vs_battle_actor2* actor;
        unequip_shield();
        if (item >= 0) {
            vs_main_inventoryShield* sh = &vs_main_inventory.shields[item];
            sh->isEquipped = 1;
            actor = ashley();
            if (actor->weapon.blade.id && two_handed_category((u_char)actor->weapon.blade.category))
                unequip_weapon();
            vs_battle_equipShield(sh);
        }
    }
    after = models_now();
    if (before.wep != after.wep || before.category != after.category || before.material != after.material
        || before.shield != after.shield || before.shield_material != after.shield_material) {
        k = equipped_weapon();
        item = equipped_shield();
        if (pc_weapon_slots_menu_models(k >= 0 ? &vs_main_inventory.weapons[k] : NULL,
                item >= 0 ? &vs_main_inventory.shields[item] : NULL)
            == 0)
            loading = 1;
    }
    return 1;
}

/* MENU3 _equipArmorMenu / _equipAccessoryMenu. */
static int equip_armor(int slot, int item)
{
    int limb = slot == PC_UI_SLOT_ACCESSORY ? ACCESSORY_LIMB : slot - 1;
    int worn = worn_on(limb);
    vs_main_inventoryArmor* a = NULL;

    if (item >= 0) {
        if (item >= N_ARMOR || !armor_fits(slot, &vs_main_inventory.armor[item]))
            return 0;
        a = &vs_main_inventory.armor[item];
    }
    if (worn >= 0)
        vs_main_inventory.armor[worn].limb = 0;
    if (a)
        a->limb = limb;
    if (slot == PC_UI_SLOT_ACCESSORY)
        vs_battle_equipAccessory(a);
    else
        vs_battle_equipArmor(limb - 1, a);
    return 1;
}

static int equip(const pc_ui_request* r)
{
    if (loading || !ashley() || r->item < -1)
        return 0;
    if (r->slot == PC_UI_SLOT_WEAPON || r->slot == PC_UI_SLOT_SHIELD)
        return resident && equip_hands(r->slot, r->item);
    if (r->slot > PC_UI_SLOT_SHIELD && r->slot < PC_UI_EQUIP_SLOTS)
        return equip_armor(r->slot, r->item);
    return 0;
}

void pc_ui_equip_requests(void)
{
    pc_ui_request r;
    int changed = 0;

    while (pc_ui_shell_pop_request(&r)) {
        switch (r.kind) {
        case PC_UI_REQ_EQUIP:
            if (equip(&r)) {
                vs_battle_playMenuSelectSfx();
                changed = 1;
            } else {
                vs_battle_playInvalidSfx();
            }
            break;
        case PC_UI_REQ_QUICK_SET:
            if (r.slot >= 0 && r.slot < PC_UI_QUICK && r.item >= 0 && r.item < N_WEAPONS
                && vs_main_inventory.weapons[r.item].blade) {
                pc_weapon_slots_set(r.slot + 1, r.item, r.shield);
                vs_battle_playMenuSelectSfx();
                changed = 1;
            } else {
                vs_battle_playInvalidSfx();
            }
            break;
        case PC_UI_REQ_QUICK_CLEAR:
            if (r.slot >= 0 && r.slot < PC_UI_QUICK) {
                pc_weapon_slots_clear(r.slot + 1);
                vs_battle_playMenuLeaveSfx();
                changed = 1;
            }
            break;
        default: /* PC_UI_REQ_CANDIDATES: the list is filled every tick; the Abilities and Magick screens'
                  * are their bridges' */
            if (!pc_ui_abilities_request(&r))
                pc_ui_magick_request(&r);
            break;
        }
    }
    if (changed) { /* the shell sees the result on this tick already */
        fill();
        pc_ui_shell_set_equipment(&snap);
    }
}
