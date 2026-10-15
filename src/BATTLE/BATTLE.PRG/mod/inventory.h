/* The bigger bag (VS_BIG_INVENTORY): twice the weapons, shields, blades, grips, armour and gems.
 *
 * In RAM the bag is vs_main_bigInventory (main.h): the original categories, each twice as long.
 * Saves keep the original layout, so any version of the game can load them:
 *   - the bag's first half is the save's bag, its order lists those slots' order;
 *   - its second half goes in the container's last slots (VS_CONTAINER_BAG_*), numbered as
 *     container items, so a game without the bigger bag shows them in its container. With
 *     VS_BIG_INVENTORY_SPARE weapons, shields and blades go in save space instead (not done yet).
 * The container keeps the slots the bag doesn't use (VS_CONTAINER_*).
 *
 * Items refer to each other by slot (a weapon to its blade, grip and gems, a shield to its gems,
 * and back), so a weapon must stay in one half with its parts. Before it is written the saved
 * copy is put in order: Ashley's equipped weapon, shield and armour move to the first half (the
 * Merlose loadout's slot numbers follow), then blades, grips and gems move to their weapon's or
 * shield's half. Loading puts a container in order the same way before its last slots are taken
 * into the bag, so a weapon that an old save keeps in the container with its blade in those slots
 * stays whole; and the container screen does it again before it hides those slots. The order
 * only depends on the container's contents, so all three agree.
 *
 * Only for TITLE.PRG and MENU7.PRG (and the PC build), each with its own copy: static functions,
 * no data. */
#pragma once
#ifdef VS_BIG_INVENTORY

#include "../func_8006B57C_t.h"

#if VS_BIG_INVENTORY == VS_BIG_INVENTORY_SPARE
#error "VS_BIG_INVENTORY_SPARE: saving weapons, shields and blades in save space isn't written yet"
#endif
_Static_assert(sizeof(vs_main_inventory_t) == VS_INVENTORY_BYTES, "VS_INVENTORY_BYTES");

enum { _invWeapon, _invShield, _invBlade, _invGrip, _invArmor, _invGem, _invCategories };

/* One store of items: the bag, its saved copy or the container */
typedef struct {
    vs_main_inventoryWeapon* weapons;
    vs_main_inventoryShield* shields;
    vs_main_inventoryBlade* blades;
    vs_main_inventoryGrip* grips;
    vs_main_inventoryArmor* armor;
    vs_main_inventoryGem* gems;
    u_char* order[_invCategories]; /* the bag's order lists (NULL for the container) */
    short count[_invCategories];
    short split[_invCategories]; /* slots below it are the first half */
} _invStore;

static int _invBagHalf(int category)
{
    static const u_char halves[] = { VS_INVENTORY_WEAPONS / 2, VS_INVENTORY_SHIELDS / 2,
        VS_INVENTORY_BLADES / 2, VS_INVENTORY_GRIPS / 2, VS_INVENTORY_ARMOR / 2,
        VS_INVENTORY_GEMS / 2 };
    return halves[category];
}

/* How many of the container's last slots hold the bag's second half */
static int _invInContainer(int category)
{
    static const u_char counts[] = { VS_CONTAINER_BAG_WEAPONS, VS_CONTAINER_BAG_SHIELDS,
        VS_CONTAINER_BAG_BLADES, VS_CONTAINER_BAG_GRIPS, VS_CONTAINER_BAG_ARMOR,
        VS_CONTAINER_BAG_GEMS };
    return counts[category];
}

static void _invBag(_invStore* store, vs_main_inventory_t* bag, vs_main_inventoryIndices_t* order)
{
    int i;

    store->weapons = bag->weapons;
    store->shields = bag->shields;
    store->blades = bag->blades;
    store->grips = bag->grips;
    store->armor = bag->armor;
    store->gems = bag->gems;
    store->order[_invWeapon] = order ? order->weapons : NULL;
    store->order[_invShield] = order ? order->shields : NULL;
    store->order[_invBlade] = order ? order->blades : NULL;
    store->order[_invGrip] = order ? order->grips : NULL;
    store->order[_invArmor] = order ? order->armor : NULL;
    store->order[_invGem] = order ? order->gems : NULL;
    store->count[_invWeapon] = VS_INVENTORY_WEAPONS;
    store->count[_invShield] = VS_INVENTORY_SHIELDS;
    store->count[_invBlade] = VS_INVENTORY_BLADES;
    store->count[_invGrip] = VS_INVENTORY_GRIPS;
    store->count[_invArmor] = VS_INVENTORY_ARMOR;
    store->count[_invGem] = VS_INVENTORY_GEMS;
    for (i = 0; i < _invCategories; ++i) {
        store->split[i] = _invBagHalf(i);
    }
}

static void _invContainer(_invStore* store, vs_menu_containerData* container)
{
    static const u_char counts[] = { 32, 32, 64, 64, 64, 192 };
    int i;

    store->weapons = container->weapons;
    store->shields = container->shields;
    store->blades = container->blades;
    store->grips = container->grips;
    store->armor = container->armor;
    store->gems = container->gems;
    for (i = 0; i < _invCategories; ++i) {
        store->order[i] = NULL;
        store->count[i] = counts[i];
        store->split[i] = counts[i] - _invInContainer(i);
    }
}

/* ---- Slots and links ---- */

static int _invOccupied(_invStore* s, int category, int slot)
{
    switch (category) {
    case _invWeapon:
        return s->weapons[slot].blade != 0;
    case _invShield:
        return s->shields[slot].base.id != 0;
    case _invBlade:
        return s->blades[slot].id != 0;
    case _invGrip:
        return s->grips[slot].id != 0;
    case _invArmor:
        return s->armor[slot].id != 0;
    default:
        return s->gems[slot].id != 0;
    }
}

static int _invRecordSize(int category)
{
    static const u_char sizes[] = { sizeof(vs_main_inventoryWeapon),
        sizeof(vs_main_inventoryShield), sizeof(vs_main_inventoryBlade),
        sizeof(vs_main_inventoryGrip), sizeof(vs_main_inventoryArmor),
        sizeof(vs_main_inventoryGem) };
    return sizes[category];
}

static char* _invRecord(_invStore* s, int category, int slot)
{
    switch (category) {
    case _invWeapon:
        return (char*)&s->weapons[slot];
    case _invShield:
        return (char*)&s->shields[slot];
    case _invBlade:
        return (char*)&s->blades[slot];
    case _invGrip:
        return (char*)&s->grips[slot];
    case _invArmor:
        return (char*)&s->armor[slot];
    default:
        return (char*)&s->gems[slot];
    }
}

/* A record's own slot number (1-based) */
static void _invSetIndex(_invStore* s, int category, int slot, int oldSlot)
{
    switch (category) {
    case _invWeapon:
        s->weapons[slot].index = slot + 1;
        break;
    case _invShield: {
        vs_main_inventoryShield* shield = &s->shields[slot];
        shield->index = slot + 1;
        /* the loot screen also numbers a shield's base by its slot */
        if (shield->base.limb == oldSlot + 1) {
            shield->base.limb = slot + 1;
        }
        if (shield->base.index == oldSlot + 1) {
            shield->base.index = slot + 1;
        }
        break;
    }
    case _invBlade:
        s->blades[slot].index = slot + 1;
        break;
    case _invGrip:
        s->grips[slot].index = slot + 1;
        break;
    case _invArmor:
        s->armor[slot].index = slot + 1;
        break;
    default:
        s->gems[slot].index = slot + 1;
        break;
    }
}

/* A gem's owner: a weapon's slot, or a shield's slot | 0x80 (gemTargetShield) */
#define _INV_SHIELD_GEM 0x80

/* Every link to slot `from` of a category now goes to `to` (both 1-based; 0 for none). Links
 * are swapped when `swap`: links to `to` then go to `from`. */
static void _invRelink(_invStore* s, int category, int from, int to, int swap)
{
    int i;
    int j;

#define _INV_MOVE(field)                                                                    \
    do {                                                                                    \
        if ((field) == from) {                                                              \
            (field) = to;                                                                   \
        } else if (swap && ((field) == to)) {                                               \
            (field) = from;                                                                 \
        }                                                                                   \
    } while (0)

    switch (category) {
    case _invWeapon:
        for (i = 0; i < s->count[_invBlade]; ++i) {
            _INV_MOVE(s->blades[i].assembledWeaponIndex);
        }
        for (i = 0; i < s->count[_invGrip]; ++i) {
            _INV_MOVE(s->grips[i].assembledWeaponIndex);
        }
        for (i = 0; i < s->count[_invGem]; ++i) {
            if (!(s->gems[i].setItemIndex & _INV_SHIELD_GEM)) {
                _INV_MOVE(s->gems[i].setItemIndex);
            }
        }
        break;
    case _invShield:
        for (i = 0; i < s->count[_invGem]; ++i) {
            if (s->gems[i].setItemIndex & _INV_SHIELD_GEM) {
                int owner = s->gems[i].setItemIndex & ~_INV_SHIELD_GEM;
                _INV_MOVE(owner);
                s->gems[i].setItemIndex = owner | _INV_SHIELD_GEM;
            }
        }
        break;
    case _invBlade:
        for (i = 0; i < s->count[_invWeapon]; ++i) {
            _INV_MOVE(s->weapons[i].blade);
        }
        break;
    case _invGrip:
        for (i = 0; i < s->count[_invWeapon]; ++i) {
            _INV_MOVE(s->weapons[i].grip);
        }
        break;
    case _invGem:
        /* gems[3] isn't a gem: no grip has four slots (the QoL weapon slots keep data there) */
        for (i = 0; i < s->count[_invWeapon]; ++i) {
            for (j = 0; j < 3; ++j) {
                _INV_MOVE(s->weapons[i].gems[j]);
            }
        }
        for (i = 0; i < s->count[_invShield]; ++i) {
            for (j = 0; j < 3; ++j) {
                _INV_MOVE(s->shields[i].gems[j]);
            }
        }
        break;
    }
#undef _INV_MOVE
}

/* Exchanges two slots of a category, with everything that refers to them */
static void _invSwap(_invStore* s, int category, int a, int b)
{
    int size = _invRecordSize(category);
    char* ra = _invRecord(s, category, a);
    char* rb = _invRecord(s, category, b);
    u_char* order = s->order[category];
    int i;

    for (i = 0; i < size; ++i) {
        char c = ra[i];
        ra[i] = rb[i];
        rb[i] = c;
    }
    _invSetIndex(s, category, a, b);
    _invSetIndex(s, category, b, a);
    _invRelink(s, category, a + 1, b + 1, 1);
    if (order != NULL) {
        for (i = 0; i < s->count[category]; ++i) {
            if (order[i] == a + 1) {
                order[i] = b + 1;
            } else if (order[i] == b + 1) {
                order[i] = a + 1;
            }
        }
    }
}

/* ---- Putting a store in order ---- */

/* Which half each blade, grip or gem must be in: its weapon's or shield's, -1 for a loose one.
 * A link to an empty slot (a damaged save) is ignored. */
static void _invRequiredHalves(_invStore* s, int category, signed char* half)
{
    int i;
    int j;
    int part;

    for (i = 0; i < s->count[category]; ++i) {
        half[i] = -1;
    }
    for (i = 0; i < s->count[_invWeapon]; ++i) {
        int weaponHalf = i >= s->split[_invWeapon];
        if (!_invOccupied(s, _invWeapon, i)) {
            continue;
        }
        if (category == _invBlade) {
            part = s->weapons[i].blade;
        } else if (category == _invGrip) {
            part = s->weapons[i].grip;
        } else {
            for (j = 0; j < 3; ++j) {
                part = s->weapons[i].gems[j];
                if ((part > 0) && (part <= s->count[_invGem])
                    && _invOccupied(s, _invGem, part - 1)) {
                    half[part - 1] = weaponHalf;
                }
            }
            continue;
        }
        if ((part > 0) && (part <= s->count[category]) && _invOccupied(s, category, part - 1)) {
            half[part - 1] = weaponHalf;
        }
    }
    if (category == _invGem) {
        for (i = 0; i < s->count[_invShield]; ++i) {
            if (!_invOccupied(s, _invShield, i)) {
                continue;
            }
            for (j = 0; j < 3; ++j) {
                part = s->shields[i].gems[j];
                if ((part > 0) && (part <= s->count[_invGem])
                    && _invOccupied(s, _invGem, part - 1)) {
                    half[part - 1] = i >= s->split[_invShield];
                }
            }
        }
    }
}

/* Moves blades, grips and gems into their weapon's or shield's half. Room always exists: a half
 * holds as many blades and grips as weapons, and three gems per weapon and shield. A loose part
 * is moved out only when its half has no free slot. Always the same result for the same
 * contents. */
static void _invSortParts(_invStore* s)
{
    static const u_char parts[] = { _invBlade, _invGrip, _invGem };
    signed char half[192];
    int k;

    for (k = 0; k < 3; ++k) {
        int category = parts[k];
        int count = s->count[category];
        int split = s->split[category];
        int moves = 0;
        int slot;

        _invRequiredHalves(s, category, half);

        for (slot = 0; slot < count; ++slot) {
            int need = half[slot];
            int first;
            int last;
            int pass;
            int target = -1;

            if ((need < 0) || ((slot >= split) == need)) {
                continue;
            }
            first = need ? split : 0;
            last = need ? count : split;
            /* an empty slot, else one whose part belongs in the other half, else a loose part */
            for (pass = 0; (pass < 3) && (target < 0); ++pass) {
                int i;
                for (i = first; i < last; ++i) {
                    int occupied = _invOccupied(s, category, i);
                    if ((pass == 0 && !occupied) || (pass == 1 && occupied && half[i] == !need)
                        || (pass == 2 && occupied && half[i] < 0)) {
                        target = i;
                        break;
                    }
                }
            }
            if ((target >= 0) && (++moves <= 2 * count)) { /* bounded whatever a save holds */
                signed char h = half[target];
                _invSwap(s, category, slot, target);
                half[target] = need;
                half[slot] = h;
                if ((h >= 0) && ((slot >= split) != h)) {
                    --slot; /* the part swapped in belongs in the other half too: look again */
                }
            }
        }
    }
}

#ifdef VS_CALLO
/* The Merlose loadout's slot numbers (mod/callo.h, vs_main_stateFlags + 0x400: version, weapon
 * slot and blade id, shield slot and id, 6 armour slots and ids, accessory slot and id) */
static void _invRelinkLoadout(u_char* stateFlags, int category, int a, int b)
{
    u_char* loadout = stateFlags + 0x400;
    int i;

    if (loadout[0] == 0) {
        return;
    }
    for (i = 1; i < 19; i += 2) {
        int slotCategory = i == 1 ? _invWeapon : i == 3 ? _invShield : _invArmor;
        if (slotCategory != category) {
            continue;
        }
        if (loadout[i] == a + 1) {
            loadout[i] = b + 1;
        } else if (loadout[i] == b + 1) {
            loadout[i] = a + 1;
        }
    }
}
#endif

/* Ashley's equipped weapon, shield and armour into the first half, so a game without the bigger
 * bag finds them equipped */
static void _invEquippedFirst(_invStore* s, u_char* stateFlags)
{
    static const u_char owners[] = { _invWeapon, _invShield, _invArmor };
    int k;

    for (k = 0; k < 3; ++k) {
        int category = owners[k];
        int split = s->split[category];
        int slot;

        for (slot = split; slot < s->count[category]; ++slot) {
            int equipped = category == _invWeapon ? s->weapons[slot].isEquipped
                : category == _invShield          ? s->shields[slot].isEquipped
                                                  : s->armor[slot].limb;
            int i;
            if (!equipped || !_invOccupied(s, category, slot)) {
                continue;
            }
            for (i = 0; i < split; ++i) {
                int other = category == _invWeapon ? s->weapons[i].isEquipped
                    : category == _invShield       ? s->shields[i].isEquipped
                                                   : s->armor[i].limb;
                if (!_invOccupied(s, category, i) || !other) {
                    _invSwap(s, category, slot, i);
#ifdef VS_CALLO
                    _invRelinkLoadout(stateFlags, category, slot, i);
#endif
                    break;
                }
            }
        }
    }
    (void)stateFlags;
}

/* ---- Between the bag and a save or container ---- */

/* A link from one numbering to another: the second half (from slot `fromSplit`) maps to the
 * other store's slots from `toSplit`; the first half is the same in both */
static int _invMapLink(int link, int fromSplit, int toSplit)
{
    if (link > fromSplit) {
        return link - fromSplit + toSplit;
    }
    return link;
}

/* Copies one record between stores, renumbering its own slot and its links */
static void _invCopy(_invStore* to, int category, int toSlot, _invStore* from, int fromSlot)
{
    char* t = _invRecord(to, category, toSlot);
    char* f = _invRecord(from, category, fromSlot);
    int size = _invRecordSize(category);
    int i;

#define _INV_MAP(field, c) ((field) = _invMapLink((field), from->split[c], to->split[c]))

    for (i = 0; i < size; ++i) {
        t[i] = f[i];
    }
    _invSetIndex(to, category, toSlot, fromSlot);
    switch (category) {
    case _invWeapon: {
        vs_main_inventoryWeapon* w = &to->weapons[toSlot];
        _INV_MAP(w->blade, _invBlade);
        _INV_MAP(w->grip, _invGrip);
        for (i = 0; i < 3; ++i) {
            _INV_MAP(w->gems[i], _invGem);
        }
        break;
    }
    case _invShield:
        for (i = 0; i < 3; ++i) {
            _INV_MAP(to->shields[toSlot].gems[i], _invGem);
        }
        break;
    case _invBlade:
        _INV_MAP(to->blades[toSlot].assembledWeaponIndex, _invWeapon);
        break;
    case _invGrip:
        _INV_MAP(to->grips[toSlot].assembledWeaponIndex, _invWeapon);
        break;
    case _invGem: {
        vs_main_inventoryGem* g = &to->gems[toSlot];
        if (g->setItemIndex & _INV_SHIELD_GEM) {
            int owner = g->setItemIndex & ~_INV_SHIELD_GEM;
            g->setItemIndex = _invMapLink(owner, from->split[_invShield], to->split[_invShield])
                | _INV_SHIELD_GEM;
        } else {
            _INV_MAP(g->setItemIndex, _invWeapon);
        }
        break;
    }
    }
#undef _INV_MAP
}

/* An empty slot, as the container screens leave one (MENUD _initContainerObject) */
static void _invClear(_invStore* s, int category, int slot)
{
    char* r = _invRecord(s, category, slot);
    int size = _invRecordSize(category);
    int i;

    for (i = 0; i < size; ++i) {
        r[i] = 0;
    }
    _invSetIndex(s, category, slot, slot);
}

/* Every blade, grip and gem's link back to its weapon or shield, from the weapons' and shields'
 * links (they're the ones the game reads) */
static void _invRelinkParts(_invStore* s)
{
    int i;
    int j;

    for (i = 0; i < s->count[_invBlade]; ++i) {
        s->blades[i].assembledWeaponIndex = 0;
    }
    for (i = 0; i < s->count[_invGrip]; ++i) {
        s->grips[i].assembledWeaponIndex = 0;
    }
    for (i = 0; i < s->count[_invGem]; ++i) {
        s->gems[i].setItemIndex = 0;
    }
    for (i = 0; i < s->count[_invWeapon]; ++i) {
        vs_main_inventoryWeapon* w = &s->weapons[i];
        if (!_invOccupied(s, _invWeapon, i)) {
            continue;
        }
        if ((w->blade > 0) && (w->blade <= s->count[_invBlade])) {
            s->blades[w->blade - 1].assembledWeaponIndex = i + 1;
        }
        if ((w->grip > 0) && (w->grip <= s->count[_invGrip])) {
            s->grips[w->grip - 1].assembledWeaponIndex = i + 1;
        }
        for (j = 0; j < 3; ++j) {
            if ((w->gems[j] > 0) && (w->gems[j] <= s->count[_invGem])) {
                s->gems[w->gems[j] - 1].setItemIndex = i + 1;
            }
        }
    }
    for (i = 0; i < s->count[_invShield]; ++i) {
        if (!_invOccupied(s, _invShield, i)) {
            continue;
        }
        for (j = 0; j < 3; ++j) {
            int gem = s->shields[i].gems[j];
            if ((gem > 0) && (gem <= s->count[_invGem])) {
                s->gems[gem - 1].setItemIndex = (i + 1) | _INV_SHIELD_GEM;
            }
        }
    }
}

/* The saved bag's order lists: the first half's slots in the bag's order */
static void _invSavedOrder(u_char* to, int toCount, const u_char* from, int fromCount, int split)
{
    int i;
    int n = 0;

    for (i = 0; i < toCount; ++i) {
        to[i] = 0;
    }
    for (i = 0; i < fromCount; ++i) {
        if ((from[i] != 0) && (from[i] <= split) && (n < toCount)) {
            to[n++] = from[i];
        }
    }
}

static void _invSavedStore(_invStore* store, vs_main_savedInventory_t* bag)
{
    int i;

    store->weapons = bag->weapons;
    store->shields = bag->shields;
    store->blades = bag->blades;
    store->grips = bag->grips;
    store->armor = bag->armor;
    store->gems = bag->gems;
    for (i = 0; i < _invCategories; ++i) {
        store->order[i] = NULL;
        store->count[i] = _invBagHalf(i);
        store->split[i] = _invBagHalf(i);
    }
}

/* ---- What the save, load and container code calls ---- */

/* Puts a container (from a save) in order and empties the slots that hold the bag's second
 * half: the container screens work on what remains */
static void vs_inventory_containerView(vs_menu_containerData* container)
{
    _invStore c;
    int category;
    int slot;

    _invContainer(&c, container);
    _invSortParts(&c);
    for (category = 0; category < _invCategories; ++category) {
        for (slot = c.split[category]; slot < c.count[category]; ++slot) {
            _invClear(&c, category, slot);
        }
    }
}

/* The bag from a save: its bag, order lists and container (whose last slots hold the bag's
 * second half; they are put in order first, which the save's container keeps) */
static void vs_inventory_load(vs_main_inventory_t* bag, vs_main_inventoryIndices_t* order,
    const vs_main_savedInventory_t* savedBag, const vs_main_savedInventoryIndices_t* savedOrder,
    vs_menu_containerData* container)
{
    _invStore b;
    _invStore saved;
    _invStore c;
    int category;
    int k;
    int i;

    for (i = 0; i < (int)sizeof *bag; ++i) {
        ((char*)bag)[i] = 0;
    }
    for (i = 0; i < (int)sizeof *order; ++i) {
        ((char*)order)[i] = 0;
    }
    _invBag(&b, bag, order);
    _invSavedStore(&saved, (vs_main_savedInventory_t*)savedBag);
    for (category = 0; category < _invCategories; ++category) {
        for (k = 0; k < _invBagHalf(category); ++k) {
            char* t = _invRecord(&b, category, k);
            char* f = _invRecord(&saved, category, k);
            for (i = 0; i < _invRecordSize(category); ++i) {
                t[i] = f[i];
            }
        }
    }
    for (i = 0; i < (int)sizeof bag->misc; ++i) {
        ((char*)bag->misc)[i] = ((const char*)savedBag->misc)[i];
    }
    for (i = 0; i < 8; ++i) {
        order->weapons[i] = savedOrder->weapons[i];
        order->shields[i] = savedOrder->shields[i];
    }
    for (i = 0; i < 16; ++i) {
        order->blades[i] = savedOrder->blades[i];
        order->grips[i] = savedOrder->grips[i];
        order->armor[i] = savedOrder->armor[i];
    }
    for (i = 0; i < 48; ++i) {
        order->gems[i] = savedOrder->gems[i];
    }
    for (i = 0; i < VS_INVENTORY_MISC; ++i) {
        order->misc[i] = savedOrder->misc[i];
    }

    if (container != NULL) {
        _invContainer(&c, container);
        _invSortParts(&c);
        for (category = 0; category < _invCategories; ++category) {
            int inContainer = _invInContainer(category);
            int n = 0;
            u_char* list = b.order[category];

            while ((n < b.count[category]) && (list[n] != 0)) {
                ++n;
            }
            for (k = 0; k < inContainer; ++k) {
                int from = c.split[category] + k;
                int to = b.split[category] + k;
                if (!_invOccupied(&c, category, from)) {
                    continue;
                }
                _invCopy(&b, category, to, &c, from);
                if (category == _invWeapon) {
                    b.weapons[to].isEquipped = 0;
                } else if (category == _invShield) {
                    b.shields[to].isEquipped = 0;
                } else if (category == _invArmor) {
                    b.armor[to].limb = 0;
                }
                if (n < b.count[category]) {
                    list[n++] = to + 1; /* listed after the first half's items */
                }
            }
        }
    }
    _invRelinkParts(&b);
}

/* A save's bag, order lists and container from the bag: the bag is put in order on a copy
 * (`scratch`, sizeof(vs_main_inventory_t) + sizeof(vs_main_inventoryIndices_t) bytes), its first
 * half written to the save's bag and the second to the container's last slots. stateFlags: the
 * save's copy (the Merlose loadout follows the slots that move). */
static void vs_inventory_save(vs_main_savedInventory_t* savedBag,
    vs_main_savedInventoryIndices_t* savedOrder, vs_menu_containerData* container,
    u_char* stateFlags, const vs_main_inventory_t* bag, const vs_main_inventoryIndices_t* order,
    void* scratch)
{
    vs_main_inventory_t* copy = scratch;
    vs_main_inventoryIndices_t* copyOrder = (void*)(copy + 1);
    _invStore b;
    _invStore saved;
    _invStore c;
    int category;
    int k;
    int i;

    for (i = 0; i < (int)sizeof *copy; ++i) {
        ((char*)copy)[i] = ((const char*)bag)[i];
    }
    for (i = 0; i < (int)sizeof *copyOrder; ++i) {
        ((char*)copyOrder)[i] = ((const char*)order)[i];
    }
    _invBag(&b, copy, copyOrder);
    _invEquippedFirst(&b, stateFlags);
    _invSortParts(&b);

    _invSavedStore(&saved, savedBag);
    for (category = 0; category < _invCategories; ++category) {
        for (k = 0; k < _invBagHalf(category); ++k) {
            char* t = _invRecord(&saved, category, k);
            char* f = _invRecord(&b, category, k);
            for (i = 0; i < _invRecordSize(category); ++i) {
                t[i] = f[i];
            }
        }
    }
    for (i = 0; i < (int)sizeof savedBag->misc; ++i) {
        ((char*)savedBag->misc)[i] = ((char*)copy->misc)[i];
    }
    _invSavedOrder(savedOrder->weapons, 8, copyOrder->weapons, VS_INVENTORY_WEAPONS, 8);
    _invSavedOrder(savedOrder->shields, 8, copyOrder->shields, VS_INVENTORY_SHIELDS, 8);
    _invSavedOrder(savedOrder->blades, 16, copyOrder->blades, VS_INVENTORY_BLADES, 16);
    _invSavedOrder(savedOrder->grips, 16, copyOrder->grips, VS_INVENTORY_GRIPS, 16);
    _invSavedOrder(savedOrder->armor, 16, copyOrder->armor, VS_INVENTORY_ARMOR, 16);
    _invSavedOrder(savedOrder->gems, 48, copyOrder->gems, VS_INVENTORY_GEMS, 48);
    for (i = 0; i < VS_INVENTORY_MISC; ++i) {
        savedOrder->misc[i] = copyOrder->misc[i];
    }

    _invContainer(&c, container);
    _invSortParts(&c);
    for (category = 0; category < _invCategories; ++category) {
        for (k = 0; k < _invInContainer(category); ++k) {
            int to = c.split[category] + k;
            int from = b.split[category] + k;
            if (_invOccupied(&b, category, from)) {
                _invCopy(&c, category, to, &b, from);
            } else {
                _invClear(&c, category, to);
            }
        }
    }
}

/* A new game's bag: the starting bag in the first half */
static void vs_inventory_newGame(vs_main_inventory_t* bag, const vs_main_savedInventory_t* start)
{
    static const vs_main_savedInventoryIndices_t noOrder;
    vs_main_inventoryIndices_t order;

    vs_inventory_load(bag, &order, start, &noOrder, NULL);
}

#endif
