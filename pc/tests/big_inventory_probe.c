/* The bigger bag's saving and loading (src/BATTLE/BATTLE.PRG/mod/inventory.h), on random bags
 * and containers. Built with the game's headers and -DVS_BIG_INVENTORY=1 -DVS_CALLO.
 *
 * Each item carries a unique tag (a weapon its name, every other item a field the code never
 * changes), and every weapon and shield is described by its parts' tags, so a round trip can be
 * checked whatever slots things end up in. For many random cases:
 *   1. an old save (original bag, a container that may hold a weapon in the slots the bigger bag
 *      takes with its blade outside them, or the reverse) is loaded;
 *   2. nothing is lost or doubled: the bag plus what the container screen shows is exactly the
 *      save's bag plus its container, with every weapon and shield whole;
 *   3. the bag is changed (an item of the second half equipped), saved and loaded again: the
 *      same bag and container, the equipped ones in the first half;
 *   4. the save as a game without the bigger bag sees it is consistent: its bag (first half)
 *      and its whole container link only within themselves, every link both ways. */
#include "common.h"
#include "src/SLUS_010.40/main.h"
#include "src/BATTLE/BATTLE.PRG/mod/inventory.h"

int printf(const char*, ...);
void exit(int);

static u_int seed = 12345;
static u_int rnd(u_int n)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return n ? seed % n : 0;
}

static int failures;
#define CHECK(cond, ...)                                                                    \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            printf("FAIL line %d: ", __LINE__);                                             \
            printf(__VA_ARGS__);                                                            \
            printf("\n");                                                                   \
            if (++failures > 20)                                                            \
                exit(1);                                                                    \
        }                                                                                   \
    } while (0)

/* ---- Tags ---- */
static int tagOf(_invStore* s, int category, int slot)
{
    switch (category) {
    case _invWeapon:
        return (u_char)s->weapons[slot].name[0] | (u_char)s->weapons[slot].name[1] << 8;
    case _invShield:
        return s->shields[slot].base.material;
    case _invBlade:
        return s->blades[slot].material;
    case _invGrip:
        return s->grips[slot].id;
    case _invArmor:
        return s->armor[slot].material;
    default:
        return s->gems[slot].id;
    }
}

static int nextTag = 1;

/* ---- Making items ---- */
static int freeSlot(_invStore* s, int category, int first, int last)
{
    int tries = 0;
    while (tries++ < 400) {
        int slot = first + rnd(last - first);
        if (!_invOccupied(s, category, slot))
            return slot;
    }
    for (int slot = first; slot < last; ++slot)
        if (!_invOccupied(s, category, slot))
            return slot;
    return -1;
}

static int addPart(_invStore* s, int category, int first, int last)
{
    int slot = freeSlot(s, category, first, last);
    int tag = nextTag++;
    if (slot < 0)
        return -1;
    switch (category) {
    case _invBlade:
        s->blades[slot].id = 1 + rnd(80);
        s->blades[slot].material = tag;
        break;
    case _invGrip:
        s->grips[slot].id = tag;
        s->grips[slot].gemSlots = rnd(4);
        break;
    default:
        s->gems[slot].id = tag;
        break;
    }
    _invSetIndex(s, category, slot, slot);
    return slot;
}

/* where: a slot range for the owner, a range for each part (first/last) */
static void addWeapon(_invStore* s, int first, int last, int partFirst[3], int partLast[3])
{
    int slot = freeSlot(s, _invWeapon, first, last);
    int blade, grip, gems, i;
    int tag = nextTag++;
    vs_main_inventoryWeapon* w;
    if (slot < 0)
        return;
    blade = addPart(s, _invBlade, partFirst[0], partLast[0]);
    if (blade < 0)
        return;
    grip = addPart(s, _invGrip, partFirst[1], partLast[1]);
    if (grip < 0) {
        _invClear(s, _invBlade, blade);
        return;
    }
    w = &s->weapons[slot];
    w->blade = blade + 1;
    w->grip = grip + 1;
    w->name[0] = tag;
    w->name[1] = tag >> 8;
    w->gems[3] = rnd(9); /* a QoL weapon slot: must travel with the weapon */
    gems = s->grips[grip].gemSlots;
    for (i = 0; i < gems; ++i) {
        int g = rnd(2) ? addPart(s, _invGem, partFirst[2], partLast[2]) : -1;
        w->gems[i] = g + 1;
    }
    _invSetIndex(s, _invWeapon, slot, slot);
}

static void addShield(_invStore* s, int first, int last, int gemFirst, int gemLast)
{
    int slot = freeSlot(s, _invShield, first, last);
    vs_main_inventoryShield* sh;
    int i;
    if (slot < 0)
        return;
    sh = &s->shields[slot];
    sh->base.id = 1 + rnd(40);
    sh->base.material = nextTag++;
    sh->base.gemSlots = rnd(4);
    for (i = 0; i < sh->base.gemSlots; ++i) {
        int g = rnd(2) ? addPart(s, _invGem, gemFirst, gemLast) : -1;
        sh->gems[i] = g + 1;
    }
    sh->gems[3] = rnd(256); /* QoL shield slot bits */
    _invSetIndex(s, _invShield, slot, slot);
}

static void addArmor(_invStore* s, int first, int last)
{
    int slot = freeSlot(s, _invArmor, first, last);
    if (slot < 0)
        return;
    s->armor[slot].id = 1 + rnd(60);
    s->armor[slot].material = nextTag++;
    _invSetIndex(s, _invArmor, slot, slot);
}

/* ---- Checks ---- */

/* Every link inside the store, both ways */
static void checkLinks(_invStore* s, const char* what)
{
    int i, j;
    for (i = 0; i < s->count[_invWeapon]; ++i) {
        vs_main_inventoryWeapon* w = &s->weapons[i];
        if (!_invOccupied(s, _invWeapon, i))
            continue;
        CHECK(w->blade >= 1 && w->blade <= s->count[_invBlade] && _invOccupied(s, _invBlade, w->blade - 1)
                && s->blades[w->blade - 1].assembledWeaponIndex == i + 1,
            "%s: weapon %d blade %d", what, i + 1, w->blade);
        CHECK(w->grip >= 1 && w->grip <= s->count[_invGrip] && _invOccupied(s, _invGrip, w->grip - 1)
                && s->grips[w->grip - 1].assembledWeaponIndex == i + 1,
            "%s: weapon %d grip %d", what, i + 1, w->grip);
        CHECK(w->index == i + 1, "%s: weapon %d index %d", what, i + 1, w->index);
        for (j = 0; j < 3; ++j)
            if (w->gems[j])
                CHECK(w->gems[j] <= s->count[_invGem] && _invOccupied(s, _invGem, w->gems[j] - 1)
                        && s->gems[w->gems[j] - 1].setItemIndex == i + 1,
                    "%s: weapon %d gem %d", what, i + 1, w->gems[j]);
    }
    for (i = 0; i < s->count[_invShield]; ++i) {
        if (!_invOccupied(s, _invShield, i))
            continue;
        CHECK(s->shields[i].index == i + 1, "%s: shield %d index", what, i + 1);
        for (j = 0; j < 3; ++j) {
            int g = s->shields[i].gems[j];
            if (g)
                CHECK(g <= s->count[_invGem] && _invOccupied(s, _invGem, g - 1)
                        && s->gems[g - 1].setItemIndex == ((i + 1) | 0x80),
                    "%s: shield %d gem %d (its owner %#x)", what, i + 1, g,
                    g <= s->count[_invGem] ? s->gems[g - 1].setItemIndex : -1);
        }
    }
    /* back-links point at an owner that links back */
    for (i = 0; i < s->count[_invBlade]; ++i) {
        int o = s->blades[i].assembledWeaponIndex;
        if (_invOccupied(s, _invBlade, i) && o)
            CHECK(o <= s->count[_invWeapon] && s->weapons[o - 1].blade == i + 1, "%s: blade %d owner %d", what, i + 1, o);
    }
    for (i = 0; i < s->count[_invGrip]; ++i) {
        int o = s->grips[i].assembledWeaponIndex;
        if (_invOccupied(s, _invGrip, i) && o)
            CHECK(o <= s->count[_invWeapon] && s->weapons[o - 1].grip == i + 1, "%s: grip %d owner %d", what, i + 1, o);
    }
    for (i = 0; i < s->count[_invGem]; ++i) {
        int o = s->gems[i].setItemIndex;
        int ok = 0;
        if (!_invOccupied(s, _invGem, i) || !o)
            continue;
        if (o & 0x80) {
            o &= 0x7F;
            if (o <= s->count[_invShield])
                for (j = 0; j < 3; ++j)
                    ok |= s->shields[o - 1].gems[j] == i + 1;
        } else if (o <= s->count[_invWeapon]) {
            for (j = 0; j < 3; ++j)
                ok |= s->weapons[o - 1].gems[j] == i + 1;
        }
        CHECK(ok, "%s: gem %d owner %d", what, i + 1, s->gems[i].setItemIndex);
    }
}

/* A store's contents, independent of slots: per category the sorted tags; weapons as "tag:
 * blade grip gem gem gem qol", shields "tag: gems qol" */
#define MAXITEMS 512
typedef struct {
    int n[_invCategories];
    unsigned long long items[_invCategories][MAXITEMS];
} Contents;

static void addContents(Contents* c, _invStore* s)
{
    int k, i, j;
    for (k = 0; k < _invCategories; ++k) {
        for (i = 0; i < s->count[k]; ++i) {
            unsigned long long v;
            if (!_invOccupied(s, k, i))
                continue;
            v = tagOf(s, k, i);
            if (k == _invWeapon) {
                vs_main_inventoryWeapon* w = &s->weapons[i];
                v = v * 100003LL + tagOf(s, _invBlade, w->blade - 1);
                v = v * 100003LL + tagOf(s, _invGrip, w->grip - 1);
                for (j = 0; j < 3; ++j)
                    v = v * 1009 + (w->gems[j] ? tagOf(s, _invGem, w->gems[j] - 1) % 1009 : 0);
                v = v * 11 + w->gems[3];
            } else if (k == _invShield) {
                for (j = 0; j < 3; ++j)
                    v = v * 1009 + (s->shields[i].gems[j] ? tagOf(s, _invGem, s->shields[i].gems[j] - 1) % 1009 : 0);
                v = v * 257 + s->shields[i].gems[3];
            }
            c->items[k][c->n[k]++] = v;
        }
    }
}

static void sortContents(Contents* c)
{
    int k, i, j;
    for (k = 0; k < _invCategories; ++k)
        for (i = 1; i < c->n[k]; ++i)
            for (j = i; j > 0 && c->items[k][j - 1] > c->items[k][j]; --j) {
                unsigned long long t = c->items[k][j];
                c->items[k][j] = c->items[k][j - 1];
                c->items[k][j - 1] = t;
            }
}

static int sameContents(Contents* a, Contents* b)
{
    int k, i;
    sortContents(a);
    sortContents(b);
    for (k = 0; k < _invCategories; ++k) {
        if (a->n[k] != b->n[k]) {
            printf("  category %d: %d vs %d items\n", k, a->n[k], b->n[k]);
            return 0;
        }
        for (i = 0; i < a->n[k]; ++i)
            if (a->items[k][i] != b->items[k][i]) {
                printf("  category %d item %d differs\n", k, i);
                return 0;
            }
    }
    return 1;
}

/* Every item listed exactly once in the bag's order lists */
static void checkOrder(_invStore* s, const char* what)
{
    int k, i, j;
    for (k = 0; k < _invCategories; ++k) {
        u_char* list = s->order[k];
        int count = 0;
        for (i = 0; i < s->count[k]; ++i) {
            int listed = 0;
            for (j = 0; j < s->count[k]; ++j)
                listed += list[j] == i + 1;
            if (_invOccupied(s, k, i)) {
                CHECK(listed == 1, "%s: category %d slot %d listed %d times", what, k, i + 1, listed);
                ++count;
            } else {
                CHECK(listed == 0, "%s: category %d empty slot %d listed", what, k, i + 1);
            }
        }
        for (i = 0; i < count; ++i)
            CHECK(list[i] != 0, "%s: category %d list has a gap", what, k);
    }
}

static void bagOrder(_invStore* s)
{
    int k, i, n;
    for (k = 0; k < _invCategories; ++k) {
        for (i = 0; i < s->count[k]; ++i)
            s->order[k][i] = 0;
        n = 0;
        for (i = 0; i < s->count[k]; ++i)
            if (_invOccupied(s, k, i))
                s->order[k][n++] = i + 1;
        for (i = n - 1; i > 0; --i) { /* shuffled */
            int j = rnd(i + 1);
            u_char t = s->order[k][i];
            s->order[k][i] = s->order[k][j];
            s->order[k][j] = t;
        }
    }
}

static vs_main_inventory_t bag, bag2;
static vs_main_inventoryIndices_t order, order2;
static vs_main_savedInventory_t savedBag;
static vs_main_savedInventoryIndices_t savedOrder;
static vs_menu_containerData container, savedContainer, view;
static char scratch[sizeof(vs_main_inventory_t) + sizeof(vs_main_inventoryIndices_t)];
static u_char stateFlags[0x440];

static void zero(void* p, int n)
{
    for (int i = 0; i < n; ++i)
        ((char*)p)[i] = 0;
}
static void copy(void* to, const void* from, int n)
{
    for (int i = 0; i < n; ++i)
        ((char*)to)[i] = ((const char*)from)[i];
}

static void oneCase(int n)
{
    _invStore b, s, c, v;
    Contents before, after;
    int i, k;

    /* 1. an old save: an original bag and a container */
    nextTag = 1;
    zero(&savedBag, sizeof savedBag);
    zero(&savedOrder, sizeof savedOrder);
    zero(&container, sizeof container);
    _invSavedStore(&s, &savedBag);
    _invContainer(&c, &container);
    {
        int pf[3] = { 0, 0, 0 }, pl[3] = { 16, 16, 48 };
        for (i = rnd(9); i > 0; --i)
            addWeapon(&s, 0, 8, pf, pl);
        for (i = rnd(9); i > 0; --i)
            addShield(&s, 0, 8, 0, 48);
        for (i = rnd(17); i > 0; --i)
            addArmor(&s, 0, 16);
        for (i = rnd(10); i > 0; --i)
            addPart(&s, _invBlade, 0, 16), addPart(&s, _invGrip, 0, 16), addPart(&s, _invGem, 0, 48);
        if (s.weapons[0].blade)
            s.weapons[0].isEquipped = 1;
        _invRelinkParts(&s);
    }
    {
        /* anywhere in the container, parts too: weapons straddle the last slots */
        int pf[3] = { 0, 0, 0 }, pl[3] = { 64, 64, 192 };
        int fill = rnd(4); /* 0 empty .. 3 nearly full */
        for (i = rnd(1 + fill * 10); i > 0; --i)
            addWeapon(&c, 0, 32, pf, pl);
        for (i = rnd(1 + fill * 10); i > 0; --i)
            addShield(&c, 0, 32, 0, 192);
        for (i = rnd(1 + fill * 21); i > 0; --i)
            addArmor(&c, 0, 64);
        for (i = rnd(1 + fill * 12); i > 0; --i)
            addPart(&c, _invBlade, 0, 64), addPart(&c, _invGrip, 0, 64), addPart(&c, _invGem, 0, 192);
        _invRelinkParts(&c);
    }
    /* the original order lists: the occupied slots */
    {
        u_char* lists[] = { savedOrder.weapons, savedOrder.shields, savedOrder.blades,
            savedOrder.grips, savedOrder.armor, savedOrder.gems };
        for (k = 0; k < _invCategories; ++k) {
            int m = 0;
            for (i = 0; i < s.count[k]; ++i)
                if (_invOccupied(&s, k, i))
                    lists[k][m++] = i + 1;
        }
    }
    checkLinks(&s, "old save's bag");
    checkLinks(&c, "old save's container");
    zero(&before, sizeof before);
    addContents(&before, &s);
    addContents(&before, &c);

    /* 2. load: the bag, plus what the container screen shows, is the save */
    vs_inventory_load(&bag, &order, &savedBag, &savedOrder, &container);
    copy(&view, &container, sizeof view);
    vs_inventory_containerView(&view);
    _invBag(&b, &bag, &order);
    _invContainer(&v, &view);
    checkLinks(&b, "loaded bag");
    checkLinks(&v, "container screen");
    checkOrder(&b, "loaded bag");
    for (k = 0; k < _invCategories; ++k)
        for (i = v.split[k]; i < v.count[k]; ++i)
            CHECK(!_invOccupied(&v, k, i), "container screen shows reserved slot %d of %d", i + 1, k);
    zero(&after, sizeof after);
    addContents(&after, &b);
    addContents(&after, &v);
    CHECK(sameContents(&before, &after), "case %d: load lost, doubled or broke items", n);

    /* 3. change it: equip an item of the second half, then save and load */
    for (i = b.split[_invWeapon]; i < b.count[_invWeapon]; ++i)
        if (_invOccupied(&b, _invWeapon, i)) {
            for (k = 0; k < b.count[_invWeapon]; ++k)
                b.weapons[k].isEquipped = 0;
            b.weapons[i].isEquipped = 1;
            stateFlags[0x400] = 'C';
            stateFlags[0x401] = i + 1; /* Merlose had it too: her loadout must follow */
            stateFlags[0x402] = b.blades[b.weapons[i].blade - 1].id;
            break;
        }
    for (i = b.split[_invArmor]; i < b.count[_invArmor]; ++i)
        if (_invOccupied(&b, _invArmor, i)) {
            b.armor[i].limb = 3;
            break;
        }
    bagOrder(&b);
    zero(&before, sizeof before);
    addContents(&before, &b);
    addContents(&before, &v);

    copy(&savedContainer, &container, sizeof container);
    vs_inventory_save(&savedBag, &savedOrder, &savedContainer, stateFlags, &bag, &order, scratch);

    /* 4. what a game without the bigger bag sees */
    _invSavedStore(&s, &savedBag);
    _invContainer(&c, &savedContainer);
    for (k = 0; k < _invCategories; ++k)
        c.split[k] = c.count[k]; /* the whole container */
    checkLinks(&s, "saved bag");
    checkLinks(&c, "saved container, as the original game reads it");
    {
        int equipped = 0;
        for (i = 0; i < 8; ++i)
            equipped += savedBag.weapons[i].isEquipped && savedBag.weapons[i].blade;
        for (i = 0; i < 32; ++i)
            CHECK(!(c.weapons[i].isEquipped && c.weapons[i].blade), "an equipped weapon in the container");
        if (stateFlags[0x401]) {
            CHECK(equipped == 1, "the equipped weapon isn't in the saved bag");
            CHECK(stateFlags[0x401] <= 8 && savedBag.weapons[stateFlags[0x401] - 1].isEquipped,
                "Merlose's loadout didn't follow her weapon (slot %d)", stateFlags[0x401]);
        }
    }

    vs_inventory_load(&bag2, &order2, &savedBag, &savedOrder, &savedContainer);
    copy(&view, &savedContainer, sizeof view);
    vs_inventory_containerView(&view);
    _invBag(&b, &bag2, &order2);
    _invContainer(&v, &view);
    checkLinks(&b, "reloaded bag");
    checkLinks(&v, "reloaded container screen");
    checkOrder(&b, "reloaded bag");
    zero(&after, sizeof after);
    addContents(&after, &b);
    addContents(&after, &v);
    CHECK(sameContents(&before, &after), "case %d: save + load lost, doubled or broke items", n);
    {
        int equipped = 0;
        for (i = 0; i < b.count[_invWeapon]; ++i)
            equipped += (b.weapons[i].isEquipped != 0) && _invOccupied(&b, _invWeapon, i);
        if (stateFlags[0x401])
            CHECK(equipped == 1, "case %d: %d equipped weapons after reloading", n, equipped);
    }
    zero(stateFlags, sizeof stateFlags);
}

/* A damaged save: random bytes everywhere, links included. Loading, saving and the container
 * screen must still finish and stay inside their stores (run under -fsanitize=address). */
static void damagedCase(void)
{
    u_char* p;
    int i;
    for (p = (u_char*)&savedBag, i = 0; i < (int)sizeof savedBag; ++i)
        p[i] = rnd(4) ? 0 : rnd(256);
    for (p = (u_char*)&savedOrder, i = 0; i < (int)sizeof savedOrder; ++i)
        p[i] = rnd(256);
    for (p = (u_char*)&container, i = 0; i < (int)sizeof container; ++i)
        p[i] = rnd(4) ? 0 : rnd(256);
    vs_inventory_load(&bag, &order, &savedBag, &savedOrder, &container);
    copy(&view, &container, sizeof view);
    vs_inventory_containerView(&view);
    for (p = (u_char*)&bag, i = 0; i < (int)sizeof bag; ++i)
        if (!rnd(8))
            p[i] = rnd(256);
    copy(&savedContainer, &container, sizeof container);
    vs_inventory_save(&savedBag, &savedOrder, &savedContainer, stateFlags, &bag, &order, scratch);
    zero(stateFlags, sizeof stateFlags);
}

int main(void)
{
    int n;
    for (n = 0; n < 1000; ++n)
        damagedCase();
    printf("PASS: 1000 damaged saves loaded, viewed and saved without hanging\n");
    for (n = 0; n < 3000; ++n)
        oneCase(n);
    if (failures) {
        printf("FAIL: %d problems\n", failures);
        return 1;
    }
    printf("PASS: bigger bag: 3000 random old saves loaded, changed, saved and reloaded: nothing lost or "
           "doubled, weapons and shields whole, saves readable without it\n");
    return 0;
}
