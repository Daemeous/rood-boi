/* Callo's menu module (ENDING/CALLOMNU.BIN): the code only menus use, loaded while they're open
 * (see callo.h). The resident mod calls the entry points listed in its header (vs_menu_entries,
 * in callo.h's order). Links against the resident mod's symbols. */
#include "common.h"
#include "../146C.h"
#include "../2842C.h"
#include "../30DB0.h"
#include "callo.h"

/* MERLOSE equip menu: the game's own EQUIP screens are reused. They only list items whose
 * inventory slot appears in vs_main_inventoryIndices, so while they are open the other
 * character's items are removed from those lists (and restored on leaving): items can only be
 * on one character at a time. For MERLOSE, Ashley's actor is pointed at Callo's stats (unk3C)
 * while the screen is open, so the screens show and equip her real stats and gear, and the
 * preview shows her model (as the status screen's L1 does). Ashley's "equipped" markers are
 * swapped for her loadout on entry; on leaving, her new loadout is read from the markers and
 * everything of Ashley's is put back. */

static u_char _savedWeapons[8];
static u_char _savedShields[8];
static u_char _savedArmor[16];
static char _ashleyWeaponEquipped[8];
static char _ashleyShieldEquipped[8];
static u_char _ashleyArmorLimb[16];
static vs_battle_actor2* _ashleyStats = NULL; /* initialised: see _calloSlot in callo.c */
static u_char _ashleyWeaponCategory;


void func_800F9A24(int); /* MAINMENU: build the menu preview from a character */
void func_800F9E0C(void); /* MAINMENU: free the menu preview */
#define MAINMENU_SET_MENU_ROWS 0x801005E0 /* vs_mainmenu_setMenuRows */

/* Show her in the preview: a copy of her in-game model, weapon models included */
static void _previewCallo(void)
{
    func_800F9E0C();
    func_800F9A24(_calloSlot);
}

static void _hideSlot(u_char* list, int count, int slot)
{
    int i;

    for (i = 0; i < count; ++i) {
        if (list[i] == slot) {
            list[i] = 0;
        }
    }
}

/* Mark a loadout armour piece as worn on body part `limb` (1-6) or as the accessory (7) */
static void _markLoadoutArmor(int offset, int limb)
{
    u_char* loadout = LOADOUT;
    int slot = loadout[offset];

    if ((slot != 0) && (slot <= 16)
        && (vs_main_inventory.armor[slot - 1].id == loadout[offset + 1])) {
        vs_main_inventory.armor[slot - 1].limb = limb;
    }
}

static void _equipEnter(vs_battle_actor* callo)
{
    u_char* loadout = LOADOUT;
    vs_main_inventoryWeapon* weapon;
    vs_main_inventoryShield* shield;
    vs_main_inventoryBlade* blade;
    int i;

    _dropSharedItems();

    for (i = 0; i < 8; ++i) {
        _savedWeapons[i] = vs_main_inventoryIndices.weapons[i];
        _savedShields[i] = vs_main_inventoryIndices.shields[i];
    }
    for (i = 0; i < 16; ++i) {
        _savedArmor[i] = vs_main_inventoryIndices.armor[i];
    }

    if (callo == NULL) {
        /* Ashley's EQUIP: hide Callo's items */
        _menuMode = 1;
        if (loadout[loadoutVersion] != LOADOUT_VERSION) {
            return;
        }
        _hideSlot(vs_main_inventoryIndices.weapons, 8, loadout[loadoutWeapon]);
        _hideSlot(vs_main_inventoryIndices.shields, 8, loadout[loadoutShield]);
        for (i = 0; i < 6; ++i) {
            _hideSlot(vs_main_inventoryIndices.armor, 16, loadout[loadoutArmor + i * 2]);
        }
        _hideSlot(vs_main_inventoryIndices.armor, 16, loadout[loadoutAccessory]);
        return;
    }

    _menuMode = 2;
    _saveCalloGear(callo);

    /* Swap Ashley's markers out, hiding his items and weapons she has no animations for */
    for (i = 0; i < 8; ++i) {
        weapon = &vs_main_inventory.weapons[i];
        _ashleyWeaponEquipped[i] = weapon->isEquipped;
        _ashleyShieldEquipped[i] = vs_main_inventory.shields[i].isEquipped;
        if (_ashleyWeaponEquipped[i]) {
            _hideSlot(vs_main_inventoryIndices.weapons, 8, i + 1);
        } else if (weapon->blade != 0) {
            blade = &vs_main_inventory.blades[weapon->blade - 1];
            if (!(CALLO_WEAPON_CATEGORIES & (1 << blade->category))) {
                _hideSlot(vs_main_inventoryIndices.weapons, 8, i + 1);
            }
        }
        if (_ashleyShieldEquipped[i]) {
            _hideSlot(vs_main_inventoryIndices.shields, 8, i + 1);
        }
        weapon->isEquipped = 0;
        vs_main_inventory.shields[i].isEquipped = 0;
    }
    for (i = 0; i < 16; ++i) {
        _ashleyArmorLimb[i] = vs_main_inventory.armor[i].limb;
        if (_ashleyArmorLimb[i]) {
            _hideSlot(vs_main_inventoryIndices.armor, 16, i + 1);
        }
        vs_main_inventory.armor[i].limb = 0;
    }

    /* ...and Callo's in */
    weapon = _loadoutWeapon();
    if (weapon != NULL) {
        weapon->isEquipped = 1;
    }
    shield = _loadoutShield();
    if (shield != NULL) {
        shield->isEquipped = 1;
    }
    if (loadout[loadoutVersion] == LOADOUT_VERSION) {
        for (i = 0; i < 6; ++i) {
            _markLoadoutArmor(loadoutArmor + i * 2, i + 1);
        }
        _markLoadoutArmor(loadoutAccessory, 7);
    }

    /* The EQUIP screens work on vs_battle_characterState: give it her stats */
    _ashleyStats = psx_get(vs_battle_characterState->unk3C);
    _ashleyWeaponCategory = vs_battle_characterState->equippedWeaponCategory;
    psx_copy(vs_battle_characterState->unk3C, callo->unk3C);
    _previewCallo();
}

static void _equipLeave(void)
{
    u_char* loadout = LOADOUT;
    vs_battle_actor2* stats;
    int i;
    int part;

    if (_menuMode == 2) {
        stats = psx_get(vs_battle_characterState->unk3C);
        for (i = 0; i <= loadoutAccessory + 1; ++i) {
            loadout[i] = 0;
        }
        loadout[loadoutVersion] = LOADOUT_VERSION;

        /* Read her new loadout from the markers. Nothing marked: her default weapon (the
         * bow) and armour, unless she wore none of it any more (taken off here). */
        for (i = 0; i < 8; ++i) {
            if (vs_main_inventory.weapons[i].isEquipped) {
                loadout[loadoutWeapon] = i + 1;
                loadout[loadoutWeapon + 1] =
                    vs_main_inventory.blades[vs_main_inventory.weapons[i].blade - 1].id;
            }
            if (vs_main_inventory.shields[i].isEquipped) {
                loadout[loadoutShield] = i + 1;
                loadout[loadoutShield + 1] = vs_main_inventory.shields[i].base.id;
            }
        }
        for (part = 0; part < 6; ++part) {
            if (stats->limbs[part].armor.armor.id == 0) {
                loadout[loadoutArmor + part * 2] = LOADOUT_EMPTY;
            }
        }
        if (stats->accessory.accessory.id == 0) {
            loadout[loadoutAccessory] = LOADOUT_EMPTY;
        }
        for (i = 0; i < 16; ++i) {
            part = vs_main_inventory.armor[i].limb;
            if ((part >= 1) && (part <= 6)) {
                loadout[loadoutArmor + (part - 1) * 2] = i + 1;
                loadout[loadoutArmor + (part - 1) * 2 + 1] = vs_main_inventory.armor[i].id;
            } else if (part == 7) {
                loadout[loadoutAccessory] = i + 1;
                loadout[loadoutAccessory + 1] = vs_main_inventory.armor[i].id;
            }
        }

        /* Give Ashley his markers, stats and preview back */
        for (i = 0; i < 8; ++i) {
            vs_main_inventory.weapons[i].isEquipped = _ashleyWeaponEquipped[i];
            vs_main_inventory.shields[i].isEquipped = _ashleyShieldEquipped[i];
        }
        for (i = 0; i < 16; ++i) {
            vs_main_inventory.armor[i].limb = _ashleyArmorLimb[i];
        }
        psx_set(vs_battle_characterState->unk3C, _ashleyStats);
        vs_battle_characterState->equippedWeaponCategory = _ashleyWeaponCategory;
        func_800F9E0C();
        func_800F9A24(0);
        _respawnPending = 1;
    }

    if (_menuMode != 0) {
        for (i = 0; i < 8; ++i) {
            vs_main_inventoryIndices.weapons[i] = _savedWeapons[i];
            vs_main_inventoryIndices.shields[i] = _savedShields[i];
        }
        for (i = 0; i < 16; ++i) {
            vs_main_inventoryIndices.armor[i] = _savedArmor[i];
        }
    }
    _menuMode = 0;
}

/* --- Equipping in MERLOSE ----------------------------------------------------------------------
 * The weapon and shield screens call MENU3's _loadObjectData(category, wepId, material) each
 * frame until it returns 0. For Ashley it re-arms him in game (weapon type's animation set,
 * weapon/shield model), sheathes and draws in the preview, and rebuilds the preview from him.
 * In MERLOSE the same is done to Callo, as MENU3 does it but for her slot. The preview (a copy
 * of a non-Ashley character) shares her animation set rather than copying it, so her old set is
 * only replaced once the preview has finished sheathing, and the preview lets go of it first.
 * (category 0 = shield; wepId < 96 is a weapon model, >= 96 a shield model.) */

void func_800F9A78(int); /* MAINMENU: build the preview from a character (no draw) */
void func_800F9CB0(void); /* MAINMENU: place the preview */
int func_800AD494(void*, int, u_short**);
void func_800AD008(void*, void*);
void func_800AFA28(void*, void*, int);

#define PREVIEW 1
#define UNARMED_CATEGORY 10
#define ANIM_IDLE 1         /* sheathed */
#define ANIM_IDLE_DRAWN 101 /* weapon drawn */

#define ANIM_WAIT_FRAMES 120 /* if she lacks a sheathe/draw animation, carry on anyway */

static u_char _equipStep;
static u_char _equipWait;
static u_char _newAnimSet; /* category whose animation set to load after sheathing, 0 none */

/* MENU3's func_80109320: weapon types held in both hands */
static int _twoHanded(int category)
{
    if (category < 3) {
        return 0;
    }
    if (category == 8) {
        return 1;
    }
    return category & 1;
}

/* MAINMENU's func_800FA2CC for her: restart her animation with the current set */
static void _resetCalloAnimation(void)
{
    D_800F4538_t* model = D_800F4538[_calloSlot];
    vs_battle_wepModels_t* wep;
    u_short* frames;
    int i;
    int anim;

    for (i = 0; i < 2; ++i) {
        wep = vs_battle_wepModels[_calloSlot * 2 + i];
        if (wep != NULL) {
            if (model->unk0.weaponDrawn) {
                wep->unk11 = 0x40;
                wep->unk8_4 = 1;
            } else {
                wep->unk11 = 0;
            }
        }
    }
    func_8009E634(model);
    anim = model->unk0.weaponDrawn ? ANIM_IDLE_DRAWN : ANIM_IDLE;
    model->animationId = anim;
    model->unk5AC_20 = 0;
    model->unk187C = func_800AD494(model, anim, &frames);
    model->unk6E3 = D_800F49DC;
    model->unk5BC = *frames;
    func_800AD008(model, &model->unkC54);
    func_800AFA28(model, &model->unkC54, 1);
    vs_main_memcpy(&model->unk704, &model->unkC54, sizeof model->unk704);
    model->unkC54.unk540 = 0;
    model->unk6E0 = 0;
    model->unk5CD = 0;
    model->unk5CC = 0;
}

static void _loadCalloAnimSet(int category)
{
    vs_battle_objectData objData;

    func_8009CC20(_calloSlot, 0);
    objData.dataType = 7;
    objData.index = _calloSlot;
    objData.modelId = CALLO_ANIM_SET(category);
    objData.actorId = 0;
    vs_battle_populateDataSlot(&objData);
}

/* Remove the weapon (0) or shield (1) model from her, and queue the new one if any */
static void _queueCalloModel(vs_battle_actor* callo, int wepId, int material)
{
    vs_battle_objectData objData;
    int isShield = wepId >= 96;

    if (wepId == 0) {
        return;
    }
    func_8009AA84(_calloSlot * 2 + isShield);
    objData.dataType = 3;
    objData.index = _calloSlot * 2 + isShield;
    objData.modelId = wepId;
    psx_copy(objData.unk4, callo->unk48[isShield]);
    objData.actorId = _calloSlot;
    objData.unk11 = isShield ? 0xF1 : 0xF0;
    objData.material = material;
    vs_battle_populateDataSlot(&objData);
}

static int _equipCallo(vs_battle_actor* callo, int category, int wepId, int material)
{
    D_800F4538_t* model = D_800F4538[_calloSlot];
    D_800F4538_t* preview = D_800F4538[PREVIEW];
    int i;

    switch (_equipStep) {
    case 0:
        _newAnimSet = 0;
        if (category == 0) {
            func_8009AA84(_calloSlot * 2 + 1);
            if (wepId && _twoHanded(model->unk5B1)) {
                func_8009AA84(_calloSlot * 2);
                _newAnimSet = UNARMED_CATEGORY;
            }
        } else {
            if (CALLO_ANIM_SET(category) != model->unk5B1) {
                _newAnimSet = CALLO_ANIM_SET(category);
            }
            func_8009AA84(_calloSlot * 2);
            if ((vs_battle_wepModels[_calloSlot * 2 + 1] != NULL) && _twoHanded(category)) {
                func_8009AA84(_calloSlot * 2 + 1);
            }
        }
        _queueCalloModel(callo, wepId, material);
        func_8009D934(PREVIEW, 0, 1); /* sheathe */
        _equipWait = 0;
        _equipStep = 1;
        break;

    case 1:
        if (vs_battle_processObjectDataQueue() != 0) {
            break;
        }
        if ((preview->animationId != ANIM_IDLE) && (++_equipWait < ANIM_WAIT_FRAMES)) {
            break;
        }
        if (_newAnimSet != 0) {
            /* The preview shares her set, but func_8009CC20 only looks for other users from
             * slot 2 up, so it would free it under the preview (then freed again when the
             * preview is rebuilt). The preview is idle (sheathed set) until it's rebuilt. */
            psx_set(preview->unk5D8, NULL);
            _loadCalloAnimSet(_newAnimSet);
        }
        _equipStep = 2;
        break;

    case 2:
        if (vs_battle_processObjectDataQueue() != 0) {
            break;
        }
        _resetCalloAnimation();
        func_800F9E0C();
        func_800F9A78(_calloSlot);
        func_800F9CB0();
        preview = D_800F4538[PREVIEW];
        preview->unk0.weaponDrawn = 0;
        func_800A0204(PREVIEW, ANIM_IDLE, 0, 0);
        func_800AFA28(preview, &preview->unkC54, 1);
        vs_main_memcpy(&preview->unk704, &preview->unkC54, sizeof preview->unk704);
        for (i = 0; i < 2; ++i) {
            if (D_800F4590[i] != NULL) {
                D_800F4590[i]->unk11 = 0;
            }
        }
        func_8009D934(PREVIEW, 1, 1); /* draw */
        _equipWait = 0;
        _equipStep = 3;
        break;

    case 3:
        if ((preview->animationId == ANIM_IDLE_DRAWN) || (++_equipWait >= ANIM_WAIT_FRAMES)) {
            /* Only her crossbow draw shows the new models by itself; make sure they're shown */
            func_8009D934(PREVIEW, 1, 2);
            _equipStep = 0;
            return 0;
        }
        break;
    }
    return 1;
}

/* --- MAGIC menu (MENU0.PRG): choosing her spells ------------------------------------------------
 * In each school's list her spells are shown in the other font colour, and Square on a spell
 * gives it to her or takes it back (up to MAX_CALLO_SPELLS; Heal she always has). Called from
 * merlose.h's vs_mod_magicRows / vs_mod_magicCost. */
void vs_battle_playInvalidSfx(void);
void vs_battle_playMenuSelectSfx(void);
u_char* vs_battle_getMenuItem(int id);

#ifdef ROOD_PC
/* Natively MAINMENU is linked in: its statics through accessors (MAINMENU.PRG/58EC.c) */
int vs_mainMenu_getConfirmedRow(void);
u_short* vs_mod_mainmenuItemsText(void);
int vs_mod_mainmenuRowInfo(void);
int vs_mod_mainmenuPage(void);
#define MAINMENU_GET_CONFIRMED_ROW() vs_mainMenu_getConfirmedRow()
#define MAINMENU_ITEMS_TEXT ((u_char*)vs_mod_mainmenuItemsText())
#define MAINMENU_ROW_INFO vs_mod_mainmenuRowInfo()
#define MAINMENU_PAGE vs_mod_mainmenuPage()
#else
#define MAINMENU_GET_CONFIRMED_ROW() ((int (*)(void))0x801008B0)() /* vs_mainMenu_getConfirmedRow */
#define MAINMENU_ITEMS_TEXT (*(u_char**)0x801023D4) /* _itemsText: the list's rows */
#define MAINMENU_ROW_INFO (*(char*)0x801023DD)      /* _subMenuRowInfo */
#define MAINMENU_PAGE (*(char*)0x801023DE)          /* _subMenuPage */
#endif
#define ROW_SIZE 128       /* _itemsText_t */
#define ROW_FLAGS 28       /* _itemsText_t.menuRowFlags */
#define ROW_FONT_COLOR 4   /* row flag: the other font colour */
#define ROW_INVERT 8       /* row flag that also sets it */
#define ITEM_FONT_COLOR 10 /* vs_battle_menuItem_t.fontColor */

static int _calloCasts(int id)
{
    return (id == CALLO_HEAL) || _spellChosen(_spellIndex(id));
}

/* A school's list is being built: her spells in the other font colour */
static void _markSpells(int school, int* rowTypes)
{
    int row = 0;
    int id;
    int i;

    _initSpellChoice();
    for (i = 0; i < _schoolSizes[school]; ++i) {
        id = _schoolSpells[school][i];
        if (ACTION_UNLOCKED(id)) {
            if (_calloCasts(id)) {
                rowTypes[row] |= ROW_FONT_COLOR;
            }
            ++row;
        }
    }
}

/* Only spells Ashley has learned count towards her limit: the MAGIC menu lists only those, so
 * a chosen spell he hasn't learned yet (the defaults, early on) could never be taken off. */
static int _chosenSpellCount(void)
{
    int count = 0;
    int i;

    for (i = 0; i < SPELL_COUNT; ++i) {
        if (_spellChosen(i) && ACTION_UNLOCKED(_spellId(i))) {
            ++count;
        }
    }
    return count;
}

/* Choosing a spell in the MAGIC menu makes the choice the player's: the defaults Ashley hasn't
 * learned yet are dropped, so they can't turn up later on top of the player's 8. */
static void _dropUnlearnedSpells(void)
{
    int i;

    for (i = 0; i < SPELL_COUNT; ++i) {
        if (_spellChosen(i) && !ACTION_UNLOCKED(_spellId(i))) {
            _setSpellChosen(i, 0);
        }
    }
}

/* Square on a spell in a school's list (id < 0: a spell with levels) */
static void _toggleSpell(int id)
{
    vs_battle_actor* callo;
    int* flags;
    int index;
    int chosen;
    int row;

    if (id < 0) {
        id = -id;
    }
    index = _spellIndex(id);
    _initSpellChoice();
    chosen = !_spellChosen(index);
    if ((index < 0) || (id == CALLO_HEAL)
        || (chosen && (_chosenSpellCount() >= MAX_CALLO_SPELLS))) {
        vs_battle_playInvalidSfx();
        return;
    }
    if (chosen) {
        _dropUnlearnedSpells();
    }
    _setSpellChosen(index, chosen);
    vs_battle_playMenuSelectSfx();

    /* Recolour the highlighted row */
    row = MAINMENU_GET_CONFIRMED_ROW();
    if (MAINMENU_ITEMS_TEXT != NULL) {
        flags = (int*)(MAINMENU_ITEMS_TEXT + row * ROW_SIZE + ROW_FLAGS);
        if (chosen) {
            *flags |= ROW_FONT_COLOR;
        } else {
            *flags &= ~ROW_FONT_COLOR;
        }
        vs_battle_getMenuItem(row - MAINMENU_PAGE + MAINMENU_ROW_INFO * 10)[ITEM_FONT_COLOR] =
            ((*flags & ROW_INVERT) != 0) | chosen;
    }

    callo = _calloActor();
    if (callo != NULL) {
        _learnSpells(psx_get(callo->unk3C));
        /* Her AI reads its actions when it starts: respawn her once the menu has closed */
        _respawnPending = 1;
    }
}

/* Entry points, in callo.h's menu* order: the header's function table (build_mod.py pack) */
void vs_menu_equipEnter(vs_battle_actor* callo) { _equipEnter(callo); }
void vs_menu_equipLeave(void) { _equipLeave(); }
int vs_menu_equipCallo(vs_battle_actor* callo, int category, int wepId, int material)
{
    return _equipCallo(callo, category, wepId, material);
}
void vs_menu_markSpells(int school, int* rowTypes) { _markSpells(school, rowTypes); }
void vs_menu_toggleSpell(int id) { _toggleSpell(id); }

#ifdef ROOD_PC
/* The entry table (callo.h MENU_ENTRY), here instead of in a module header */
void* const vs_menu_entries[] = { vs_menu_equipEnter, vs_menu_equipLeave, vs_menu_equipCallo,
    vs_menu_markSpells, vs_menu_toggleSpell };
#endif
