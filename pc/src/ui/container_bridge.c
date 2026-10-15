/* Modern menus, the native Container screen: the game side (battle builds;
 * container_frame.h has the design).
 *
 * Two layers, one screen:
 *
 * 1. The MENUD session (direct entry 14; menu_bridge.c pc_ui_direct_tick).
 *    vs_menuD_exec (src/MENU/MENUD.PRG/234.c) ported: its data operations
 *    are container_ops.c's, its modes the original's 1:1, read from the pad
 *    as the original reads it (vs_main_buttonsPressed / _buttonRepeat /
 *    _buttonsState) and from the pointer's requests (a click is the pad
 *    sequence that does the same: the cursor there, then Circle). Every
 *    write to game RAM is the original's expression at the original's point:
 *    the storage (vs_menu_inventoryStorage: unkC430, unk105B0, unk87B0,
 *    itemsToTransfer, unk4BB0), vs_main_inventory and its index lists,
 *    vs_main_settings.containerEmpty, vs_battle_menuState, vs_menuD_containerData
 *    and the six vs_mainMenu_* pointers (234.c:3149), the cursor memories
 *    (D_800F4EE8: unk9E, unkA0, cursorMemories by the MAINMENU row and
 *    action lists' own arithmetic, below) and vs_battle_rowTypeBuf. The
 *    original's presentation (menu items, the information box, the item
 *    texts vs_mainMenu_loadItemText loads, as Loot leaves MENUB's out) is
 *    not run: the shell draws its own.
 *
 *      begin     234.c state 0 (3159): unkFB0 into unkC430.data,
 *                _copyInventoryToContainer; the top menu (func_801089BC(1))
 *      TOP       func_801089BC state 3 (3074)
 *      LIST ...  func_80107490 (2396): mode init (2429), the rebuild (state
 *                1), input (2, 3), the second pick (6), detail (7, 1241),
 *                actions (8, 9), search (10), discard (11, 1686), sort (12,
 *                1607), the commit (13, 14, 15, 2335)
 *      hand-back 234.c state 6 (3218): _copyContainerToInventory, unkC430
 *                defragmented, containerEmpty, currentMenu = returnState,
 *                returnState = code + 2 (OK 3: 5 save, Cross -1: 1 the
 *                cancel warning, Triangle -2: 0 revert)
 *
 * 2. The MENU7 frame (260.c's wrapper of vs_menu7_saveContainerMenu, ROOD_PC):
 *    the latch at entry 0, the drawing gate, one injected press at a time
 *    (the prompts' answers, only ever the player's), the publish, the
 *    phases; the save list is the Data screen's model in container mode
 *    (data_bridge.c). MAINMENU's vs_mainMenu_hideMenu is 1 while the run is
 *    latched (MENU7's own CONTAINER / YES / NO rows), as it was before after.
 */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "../../../src/MENU/MAINMENU.PRG/C48.h"
#include "../../../src/MENU/MAINMENU.PRG/2D10.h"
#include "../../../src/MENU/MAINMENU.PRG/58EC.h"
#include "../../../src/MENU/MENUD.PRG/234.h"
#include "build/assets/MENU/ITEMHELP.BIN.h"
#include "build/assets/MENU/MENUD.PRG/menuText.h"
#include "build/assets/MENU/MENU7.PRG/container.h"
#include "build/assets/MENU/MCMAN.BIN.h"
#include "container_ops.h"
#include "container_frame.h"
#include "data_menu7.h"
#include "data_plan.h"
#include "item_names.h"
#include "ui_shell.h"
#include <libetc.h>

extern unsigned pc_vsync_count(void) __attribute__((weak));
extern int pc_overlay_active(void) __attribute__((weak));
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size); /* ui_text.c */
/* menu_bridge.c: the field HUD under the shell's slide-out */
void pc_ui_menu_frame_hud(int hide);
/* data_bridge.c: the save list in container mode */
void pc_ui_data_container_begin(int run);
void pc_ui_data_container_end(void);
int pc_ui_data_container_on(void);
void pc_ui_data_menu7_publish(const pc_ui_data_menu7* m, int state);

/* MENUD's own strings (234.c _menuText) and MENU7's container strings (260.c _containerStrings) */
static u_short menuD_text[] = {
#include "build/assets/MENU/MENUD.PRG/menuText.vsString"
};
static u_short m7_text[] = {
#include "build/assets/MENU/MENU7.PRG/container.vsString"
};

static unsigned now(void) { return pc_vsync_count ? pc_vsync_count() : 0; }

static void zero(void* p, unsigned n)
{
    unsigned char* b = p;
    while (n--)
        *b++ = 0;
}

static void decode(const void* text, char* out, int size)
{
    out[0] = 0;
    if (text)
        pc_ui_text_decode((const unsigned char*)text, 512, out, size);
}

static void copy_text(char* out, int size, const char* text)
{
    int i;
    for (i = 0; i + 1 < size && text && text[i]; ++i)
        out[i] = text[i];
    if (text && ((unsigned char)text[i] & 0xC0) == 0x80) { /* never half a UTF-8 sequence */
        while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80)
            --i;
        if (i > 0)
            --i;
    }
    out[i] = 0;
}

/* MENUD menuText: by offset, or by its index table (_menuText[_menuText[i]]) */
static void menud_at(char* out, int size, int offset) { decode(&menuD_text[offset], out, size); }
static void menud(char* out, int size, int index) { menud_at(out, size, menuD_text[index]); }

/* the cursor memories as the original indexes them (cursorMemories[id * 2], past its 52 declared bytes) */
static char* cm(void) { return (char*)&D_800F4EE8; }

#define SEL_SFX() vs_battle_playMenuSelectSfx()
#define LEAVE_SFX() vs_battle_playMenuLeaveSfx()
#define CHANGE_SFX() vs_battle_playMenuChangeSfx()
#define INVALID_SFX() vs_battle_playInvalidSfx()

/* ---- the MENUD session's state (MENUD's statics, the MAINMENU lists') ---- */

typedef struct {
    unsigned pressed, repeat, held;
} pad;

/* vs_mainmenu_setMenuRows / vs_mainMenu_renderItemsList's statics, rowInfo 2 (8 rows) */
typedef struct {
    int active, count, sel, page, id;
} row_list;

/* vs_mainMenu_addMenuActions / vs_mainMenu_renderItemActionMenu's statics (7 rows) */
typedef struct {
    int active, count, sel, off, id;
    int enabled[PC_UI_CTR_SORTS];
} action_list;

static struct {
    int active;
    int first_top;     /* D_80109A2E: the top menu's first entry this MENUD load */
    int mode;          /* PC_UI_CTR_* */
    int dir;           /* D_80109A7A */
    int built;         /* D_80109A6A: the list's first build (its cursor memory per the option) */
    int empty;         /* D_80109A69 */
    int picked;        /* D_80109A6C: the row picked (1-based) */
    int switching;     /* func_80107490 state 3: a tab changed, the rebuild waits for the buttons */
    int misc_after;    /* D_80109A82 */
    row_list list;
    action_list act;
    int act_kind[8];   /* D_80109A60 */
    int sort_cat;      /* _displaySortMenu's D_80109A4D */
    int disc_type, disc_cat, disc_slot, disc_step, disc_count, disc_max, disc_multi;
    int detail_cat, detail_sel;
    int dirty;
    int focus_seen;    /* the card's focus (cursor, tab, mode) when last filled */
} S;

static int tab_now(void) { return (D_800F4EE8.unkA0[0] - 1) & 7; }

/* the sorts' stats (234.c _getWeaponStat 1279, _getShieldStatValue 1361, _getEquipmentStat 1489) */
static int weapon_stat(void* u, vs_menu_containerData* c, int slot, int type)
{
    vs_battle_uiWeapon weapon;
    (void)u;
    vs_menuD_initUiWeapon(&weapon, &c->weapons[slot], c);
    switch (type) {
    case 0: return -weapon.blade.category;
    case 1: return -weapon.blade.material;
    case 2: return weapon.range.range;
    case 3: return -weapon.damageType;
    case 4: return weapon.currentDp;
    case 5: return weapon.maxDp;
    case 6: return weapon.currentPp;
    case 7: return weapon.maxPp;
    case 8: return weapon.currentStr;
    case 9: return weapon.currentInt;
    case 10: return weapon.currentAgility;
    default:
        if (type >= 17)
            return weapon.classAffinityCurrent.affinity[0][type - 17];
        return weapon.classAffinityCurrent.class[0][type - 11];
    }
}

static int shield_stat(void* u, vs_menu_containerData* c, int slot, int arg0)
{
    vs_battle_uiShield shield;
    (void)u;
    vs_menuD_initUiShield(&shield, &c->shields[slot], c);
    switch (arg0) {
    case 1: return -shield.base.material;
    case 4: return shield.currentDp;
    case 5: return shield.maxDp;
    case 6: return shield.currentPp;
    case 7: return shield.maxPp;
    case 8: return shield.currentStr;
    case 9: return shield.currentInt;
    case 10: return shield.currentAgility;
    default:
        if (arg0 < 33)
            return shield.classAffinityCurrent.class[0][arg0 - 27];
        if (arg0 >= 40)
            return shield.types[arg0 - 39];
        return shield.classAffinityCurrent.affinity[0][arg0 - 33];
    }
}

static void equipment_of(vs_battle_uiEquipment* item, vs_menu_containerData* c, int type, int index)
{
    switch (type) { /* _sortEquipment 1530 */
    case itemCategoryBlade: vs_battle_copyInventoryBladeStats(item, &c->blades[index]); return;
    case itemCategoryGrip: vs_battle_copyInventoryGripStats(item, &c->grips[index]); return;
    case itemCategoryArmor: vs_battle_copyInventoryArmorStats(item, &c->armor[index]); return;
    case itemCategoryGem: vs_battle_copyInventoryGemStats(item, &c->gems[index]); return;
    }
}

static int equip_stat(void* u, vs_menu_containerData* c, int category, int slot, int stat)
{
    vs_battle_uiEquipment sp10;
    vs_battle_uiEquipment* item = &sp10;
    (void)u;
    equipment_of(item, c, category, slot);
    switch (stat) {
    case 0: return -item->category;
    case 1: return -item->material;
    case 2: return item->range.range;
    case 3: return -item->damageType;
    case 4: return item->currentDp;
    case 5: return item->maxDp;
    case 6: return item->currentPp;
    case 7: return item->maxPp;
    case 8: return item->strength;
    case 9: return item->intelligence;
    case 10: return item->agility;
    default:
        if (stat >= 27)
            stat -= 16;
        if (stat < 17)
            return item->classes[stat - 11];
        if (stat >= 24)
            return item->types[stat - 23];
        return item->affinities[stat - 17];
    }
}

/* This call's pc_ctr: the storage and the pointers read fresh (nothing cached across frames). */
static pc_ctr ctx(void)
{
    pc_ctr c;
    vs_menu_inventoryStorage_t* st = vs_menu_inventoryStorage;
    zero(&c, sizeof c);
    c.st = st;
    c.src = vs_menuD_containerData;
    c.dst = S.dir ? &st->unkC430 : &st->unk105B0; /* D_80109A88 (func_80107490's init) */
    c.mode = S.dir;
    c.bag_caps = vs_mainMenu_inventoryItemCapacities;
    c.bag_indices = vs_mainMenu_inventoryIndices;
    c.bag = &vs_main_inventory;
    c.weapon_stat = weapon_stat;
    c.shield_stat = shield_stat;
    c.equip_stat = equip_stat;
    return c;
}

/* ---- the MAINMENU row list (58EC.c), its cursor memory ---- */

static void list_set(int count, int id, int memory) /* vs_mainmenu_setMenuRows(count, id | 0x19200) */
{
    int i = 0, page = 0, t, v;
    if (memory) {
        i = (u_char)cm()[id * 2];
        page = (u_char)cm()[id * 2 + 1];
    }
    t = count - 10 + 2;
    if (t < 0) {
        i += page;
        page = 0;
        if (i >= count)
            i = 0;
    } else {
        if (t < page) {
            if ((i + page) >= count) {
                i = 0;
                page = 0;
            } else {
                i += page - t;
                page = t;
            }
        }
        v = page < t;
        if ((page > 0) && (i == 0)) {
            i = 1;
            page -= 1;
            v = page < t;
        }
        if (v != 0 && i == (9 - 2)) {
            i -= 1;
            page += 1;
        }
    }
    S.list.sel = i;
    S.list.page = (u_char)page;
    S.list.count = count;
    S.list.id = id;
    S.list.active = 1;
}

static void list_store(void) /* vs_mainMenu_storeCursor */
{
    S.list.active = 0;
    cm()[S.list.id * 2] = S.list.sel;
    cm()[S.list.id * 2 + 1] = S.list.page;
}

/* The lock is decided from the storage itself (pc_ctr_rows), not the host model: the model is not
 * saved with a snapshot, the session is. */
static int row_selectable(int abs)
{
    static int category[PC_UI_CTR_ROWS], slot[PC_UI_CTR_ROWS], flags[PC_UI_CTR_ROWS];
    pc_ctr c;
    int n;
    if (abs < 0)
        return 1;
    c = ctx();
    n = pc_ctr_rows(&c, tab_now(), category, slot, flags, PC_UI_CTR_ROWS);
    return abs < n && !(flags[abs] & PC_CTR_ROW_LOCKED);
}

/* vs_mainMenu_renderItemsList case 17: -1 still open, else getSelectedRow's: the row (Circle), -2
 * (Cross), -3 (Triangle) */
static int list_input(const pad* in)
{
    int absRow = S.list.sel + S.list.page, n = S.list.count, info = 2;
    if (in->pressed & PADRup) {
        LEAVE_SFX();
        list_store();
        return -3;
    }
    if (in->pressed & PADRright) {
        if (row_selectable(absRow)) {
            SEL_SFX();
            list_store();
            return absRow;
        }
        INVALID_SFX();
    }
    if (in->pressed & PADRdown) {
        LEAVE_SFX();
        list_store();
        return -2;
    }
    if (in->repeat & PADLup) {
        if (n <= (10 - info)) {
            if (S.list.sel == 0)
                S.list.sel = n - 1;
            else
                --S.list.sel;
        } else if (S.list.page == 0) {
            if (S.list.sel == 0) {
                if (in->pressed & PADLup) {
                    S.list.sel = 9 - info;
                    S.list.page = (u_char)(info + (n + 246));
                }
            } else {
                --S.list.sel;
            }
        } else if (S.list.sel == 1) {
            --S.list.page;
        } else {
            --S.list.sel;
        }
    }
    if (in->repeat & PADLdown) {
        if (n <= (10 - info)) {
            if (S.list.sel == (n - 1))
                S.list.sel = 0;
            else
                ++S.list.sel;
        } else if (S.list.page == (n + (info - 10))) {
            if (S.list.sel == (9 - info)) {
                if (in->pressed & PADLdown) {
                    S.list.sel = 0;
                    S.list.page = 0;
                }
            } else {
                ++S.list.sel;
            }
        } else if (S.list.sel == (8 - info)) {
            ++S.list.page;
        } else {
            ++S.list.sel;
        }
    }
    if (absRow != (S.list.sel + S.list.page))
        CHANGE_SFX();
    return -1;
}

/* ---- the MAINMENU action list (58EC.c) ---- */

static void act_set(int count, int id) /* vs_mainMenu_addMenuActions */
{
    int i = 0, off = 0, t;
    if (id == actionMenuDiscard) {
        i = 1;
    } else if ((vs_main_settings.cursorMemory != 0) && (id != 3)) {
        i = (u_char)cm()[id * 2];
        off = (u_char)cm()[id * 2 + 1];
    }
    t = count - 7;
    if (t < 0) {
        i += off;
        off = 0;
        if (i >= count)
            i = 0;
    } else if (t < off) {
        if ((i + off) >= count) {
            i = 0;
            off = 0;
        } else {
            i += off - t;
            off = t;
        }
    }
    if ((off > 0) && (i == 0)) {
        i = 1;
        --off;
    }
    if ((off < t) && (i == 6)) {
        i = 5;
        ++off;
    }
    S.act.sel = i;
    S.act.off = off;
    S.act.count = count;
    S.act.id = id;
    S.act.active = 1;
}

static void act_store(void)
{
    S.act.active = 0;
    cm()[S.act.id * 2] = S.act.sel;
    cm()[S.act.id * 2 + 1] = S.act.off;
}

/* vs_mainMenu_renderItemActionMenu case 17 (no sounds but the cursor's and an unselectable's):
 * -1 still open, else actionInputProcessed()'s: the entry (Circle), -2 (Cross), -3 (Triangle) */
static int act_input(const pad* in)
{
    int selected = S.act.sel + S.act.off, n = S.act.count;
    if (in->pressed & PADRup) {
        act_store();
        return -3;
    }
    if (in->pressed & PADRright) {
        if (S.act.enabled[selected]) {
            act_store();
            return selected;
        }
        INVALID_SFX();
    }
    if (in->pressed & PADRdown) {
        act_store();
        return -2;
    }
    if (in->repeat & PADLup) {
        if (n < 8) {
            if (S.act.sel == 0)
                S.act.sel = n - 1;
            else
                --S.act.sel;
        } else if (S.act.off == 0) {
            if (S.act.sel == 0) {
                if (in->pressed & PADLup) {
                    S.act.sel = 6;
                    S.act.off = n - 7;
                }
            } else {
                --S.act.sel;
            }
        } else if (S.act.sel == 1) {
            --S.act.off;
        } else {
            --S.act.sel;
        }
    }
    if (in->repeat & PADLdown) {
        if (n < 8) {
            if (S.act.sel == (n - 1))
                S.act.sel = 0;
            else
                ++S.act.sel;
        } else if (S.act.off == (n - 7)) {
            if (S.act.sel == 6) {
                if (in->pressed & PADLdown) {
                    S.act.sel = 0;
                    S.act.off = 0;
                }
            } else {
                ++S.act.sel;
            }
        } else if (S.act.sel == 5) {
            ++S.act.off;
        } else {
            ++S.act.sel;
        }
    }
    if (selected != (S.act.sel + S.act.off))
        CHANGE_SFX();
    return -1;
}

/* ---- MENUD ---- */

static void mirror_pointers(void) /* 234.c:3149 (each tick, before the state's work) */
{
    vs_mainMenu_weapons = vs_menuD_containerData->data.weapons;
    vs_mainMenu_blades = vs_menuD_containerData->data.blades;
    vs_mainMenu_grips = vs_menuD_containerData->data.grips;
    vs_mainMenu_shields = vs_menuD_containerData->data.shields;
    vs_mainMenu_armor = vs_menuD_containerData->data.armor;
    vs_mainMenu_gems = vs_menuD_containerData->data.gems;
}

static void top_enter(void) /* func_801089BC(1) */
{
    if (S.first_top != 0) {
        S.first_top = 0;
        if (vs_main_settings.cursorMemory == 0)
            D_800F4EE8.unk9E = 0;
    }
    S.mode = PC_UI_CTR_TOP;
    S.dirty = 1;
}

static int handed;   /* this tick handed back */

static void hand_back(int code) /* 234.c state 6 (3218) */
{
    pc_ctr c = ctx();
    int i;
    pc_ctr_container_to_bag(&c);
    for (i = 0; i < 7; ++i)
        pc_ctr_defragment(i, &c.st->unkC430);
    for (i = 0; i < 7; ++i) {
        if (pc_ctr_count(i, &c.st->unkC430.data) != 0)
            break;
    }
    vs_main_settings.containerEmpty = i == 7;
    vs_mainMenu_clearMenuExcept(0);
    vs_battle_menuState.currentMenu = vs_battle_menuState.returnState;
    vs_battle_menuState.returnState = (u_int)code + 2;
    S.active = 0;
    handed = 1;
}

/* func_80107490 state 1: the rebuild (state 0's initInformationBox is the shell's) */
static void fill_rows(void);
static void rebuild(void)
{
    pc_ctr c = ctx();
    int i, tab = tab_now(), rows, memory;
    for (i = 0; i < 7; ++i)
        pc_ctr_defragment(i, c.src);
    pc_ctr_transfer_clean(&c);
    fill_rows();
    rows = pc_ui_ctr_state()->rows;
    S.empty = rows == 0;
    S.list.active = 0;
    S.switching = 0;
    S.mode = PC_UI_CTR_LIST;
    S.picked = 0;
    if (!S.empty) {
        memory = vs_main_settings.cursorMemory != 0 || S.built == 0;
        S.built = 0;
        list_set(rows, tab + 0x51, memory);
    }
    S.dirty = 1;
}

static void mode_init(int arg0) /* func_80107490(arg0 != 0) (2429) */
{
    vs_menu_inventoryStorage_t* st = vs_menu_inventoryStorage;
    pc_ctr c;
    S.built = 1;
    if (vs_main_settings.cursorMemory == 0)
        vs_main_bzero(D_800F4EE8.unkA0, sizeof D_800F4EE8.unkA0);
    vs_battle_rMemzero(&st->unk87B0, sizeof st->unk87B0);
    vs_battle_rMemzero(st->itemsToTransfer, sizeof st->itemsToTransfer);
    S.dir = arg0 - 1;
    if (S.dir)
        vs_menuD_containerData = &st->unk105B0;
    else
        vs_menuD_containerData = &st->unkC430;
    /* the right panel slides in (D_80109A7D 1, D_80109A80 0): func_80103270 */
    c = ctx();
    S.misc_after = pc_ctr_misc_count(&c);
    rebuild();
}

/* the commit's Yes / No (func_801072B0(1)) */
static void commit_open(void)
{
    S.act.enabled[0] = S.act.enabled[1] = 1;
    act_set(2, 3);
    S.mode = PC_UI_CTR_COMMIT_CONFIRM;
    S.dirty = 1;
}

/* Cross with something staged on another tab: unkA0 0 (tab 7), states 13 / 14 */
static void commit_from_tab(void)
{
    int rows;
    D_800F4EE8.unkA0[0] = 0;
    fill_rows();
    rows = pc_ui_ctr_state()->rows;
    S.empty = rows == 0;
    if (!S.empty)
        list_set(rows, 7 + 0x51, vs_main_settings.cursorMemory != 0);
    list_store(); /* state 14's vs_mainMenu_storeCursor */
    commit_open();
}

/* Cross in the list or its empty line (state 2): -1 back to the top, else the commit asked */
static int cross(int tab)
{
    if (vs_menu_inventoryStorage->itemsToTransfer[0] == 0)
        return -1;
    if (tab != 7)
        commit_from_tab();
    else
        commit_open();
    return 0;
}

static void actions_open(void) /* state 8 (2693) */
{
    pc_ctr c = ctx();
    pc_ctr_actions a;
    int i, tab = tab_now(), k;
    int* rb = vs_battle_rowTypeBuf;
    pc_ctr_build_actions(&c, tab, S.picked, &a);
    vs_battle_rMemzero(rb, 0x10);
    /* vs_battle_rowTypeBuf as the original fills it: the transfer's and search's only when off, the
     * discard's always, discard all's when off */
    for (i = 0; i < a.count; ++i) {
        k = a.kind[i];
        if (k == PC_CTR_ACT_DISCARD || !a.enabled[i])
            if (k != PC_CTR_ACT_SORT)
                rb[i] = !a.enabled[i];
        S.act_kind[i + 1] = k;
        S.act.enabled[i] = a.enabled[i];
    }
    act_set(a.count, tab + 0x59);
    S.mode = PC_UI_CTR_ACTIONS;
    S.dirty = 1;
}

static void search(void) /* state 10 (2894) */
{
    pc_ctr c = ctx();
    vs_menu_containerData* d = &c.src->data;
    int var_s4 = tab_now();
    int slot = pc_ctr_indices(var_s4, c.src)[S.picked - 1] - 1;
    int temp_s3, temp_s2, temp_v0_17, temp_a0;
    temp_s3 = pc_ctr_parent(var_s4, slot, d);
    if (temp_s3 != 0) {
        var_s4 = 0;
    } else {
        temp_s3 = pc_ctr_set_shield(var_s4, slot, d);
        var_s4 = 3;
    }
    temp_s2 = pc_ctr_count(var_s4, d);
    temp_v0_17 = pc_ctr_index_of(var_s4, temp_s3 - 1, pc_ctr_indices(var_s4, c.src));
    temp_s3 = temp_v0_17 - 1;
    if ((temp_s2 < 9) || (temp_s3 < 8)) {
        temp_s2 = 0;
    } else {
        temp_a0 = (temp_s2 - 8);
        if (temp_a0 < temp_s3) {
            temp_s2 = temp_a0;
            temp_s3 -= temp_s2;
        } else {
            temp_s2 = temp_v0_17 - 2;
            temp_s3 = 1;
        }
    }
    D_800F4EE8.unkA0[0] = (var_s4 + 1) & 7;
    cm()[(var_s4 + 0x51) * 2] = temp_s3;
    cm()[(var_s4 + 0x51) * 2 + 1] = temp_s2;
    rebuild();
}

static void discard_open(int arg0) /* _discardMenu(arg0 != 0) (1724) */
{
    S.disc_type = arg0 >> 0x10;
    S.disc_cat = (arg0 >> 0xC) & 0xF;
    S.disc_slot = (u_char)(arg0 - 1);
    if (S.disc_type & 1) { /* discardMultipleInit */
        S.disc_max = (u_char)vs_menuD_containerData->data.misc[S.disc_slot].count;
        S.disc_step = 0;
        S.disc_count = 0;
        S.disc_multi = 0;
        S.mode = PC_UI_CTR_DISCARD_QTY;
    } else { /* discardOneInit */
        S.act.enabled[0] = S.act.enabled[1] = 1;
        act_set(2, actionMenuDiscard);
        S.disc_multi = 0;
        S.mode = PC_UI_CTR_DISCARD_CONFIRM;
    }
    S.dirty = 1;
}

static void sort_open(int arg0) /* _displaySortMenu(arg0 != 0) */
{
    const char* stats;
    int i, n;
    S.sort_cat = arg0 - 1;
    n = pc_ctr_sort_stats(S.sort_cat, &stats);
    for (i = 0; i < n; ++i)
        S.act.enabled[i] = 1;
    act_set(n, S.sort_cat + 96);
    S.mode = PC_UI_CTR_SORT;
    S.dirty = 1;
}

static void detail_open(int tab, int index) /* func_80105008(tab | (storeCursor + 1) << 4) */
{
    S.detail_cat = tab;
    S.detail_sel = (u_char)index;
    S.mode = PC_UI_CTR_DETAIL;
    S.dirty = 1;
}

/* The tab change (state 3, 2575); in_list: from state 2 (the list's cursor stored). */
static void tabs(const pad* in, int in_list)
{
    int tab = tab_now();
    int s3 = in->held & 0x8004, s2 = in->held & 0x2008, a2;
    int t0 = s2 != 0 ? (s3 != 0) ^ 1 : (s3 != 0) & 1;
    if (t0 != 0) {
        a2 = 0x8004;
        if (s3 == 0x8004)
            a2 = 0x8000;
        if (in->repeat & a2)
            tab = (tab - 1) & 7;
        a2 = 0x2008;
        if (s2 == 0x2008)
            a2 = 0x2000;
        if (in->repeat & a2)
            tab = (tab + 1) & 7;
        if (tab != tab_now()) {
            CHANGE_SFX();
            D_800F4EE8.unkA0[0] = (tab + 1) & 7;
            if (in_list) {
                if (!S.empty)
                    list_store();
                S.switching = 1;
                S.dirty = 1;
            }
        }
    }
    if (S.switching) {
        /* the rebuild once the menu is ready and the buttons are let go */
        if ((in->held & 0xA00C) && t0 != 0)
            return;
        rebuild();
    }
}

/* func_80107490's return for the exec (0 stay, 1 back to the top, -1 back (Cross), -2 out) */
static int list_step(const pad* in)
{
    int tab = tab_now(), r;
    if (S.switching) {
        tabs(in, 0);
        return 0;
    }
    if (S.empty) {
        if (in->pressed & PADRup) {
            LEAVE_SFX();
            return -2;
        }
        if (in->pressed & PADRdown) {
            LEAVE_SFX();
            return cross(tab);
        }
        if (in->pressed & (PADRright | PADRleft))
            INVALID_SFX();
        tabs(in, 1);
        return 0;
    }
    if (S.list.active) {
        if (in->pressed & PADRleft) { /* Square: the details (tabs 0..5) */
            if (tab < 6) {
                SEL_SFX();
                list_store();
                detail_open(tab, S.list.sel + S.list.page);
                return 0;
            }
            INVALID_SFX();
        }
        r = list_input(in);
        if (r == -1) {
            tabs(in, 1);
            return 0;
        }
        if (r >= 0) {
            S.picked = r + 1;
            if (tab == 7) {
                pc_ctr c = ctx();
                pc_ctr_unstage(&c, S.picked - 1);
                S.misc_after = pc_ctr_misc_count(&c);
                rebuild();
            } else { /* the second pick: the same rows, this one marked, the cursor from its memory */
                list_set(S.list.count, tab + 0x51, 1);
                S.mode = PC_UI_CTR_PICK2;
                S.dirty = 1;
            }
            return 0;
        }
        if (r == -3)
            return -2;
        return cross(tab);
    }
    return 0;
}

static int pick2_step(const pad* in) /* state 6 */
{
    int r, row;
    if (in->pressed & PADRleft)
        INVALID_SFX();
    r = list_input(in);
    if (r == -1)
        return 0;
    if (r >= 0) {
        row = r + 1;
        if (row == S.picked) {
            actions_open();
            return 0;
        }
        {
            pc_ctr c = ctx();
            pc_ctr_merge_or_swap(&c, tab_now(), S.picked, row);
        }
        rebuild();
        return 0;
    }
    if (r == -3)
        return -2;
    rebuild(); /* Cross: state 0 */
    return 0;
}

static int actions_step(const pad* in) /* state 9 */
{
    int r = act_input(in), v, tab = tab_now();
    pc_ctr c;
    u_short* list;
    if (r == -1)
        return 0;
    v = r + 1;
    c = ctx();
    list = pc_ctr_indices(tab, c.src);
    if (v > 0) {
        SEL_SFX();
        switch (S.act_kind[v]) {
        case PC_CTR_ACT_TRANSFER:
            pc_ctr_stage(&c, tab, list[S.picked - 1] - 1);
            rebuild();
            break;
        case PC_CTR_ACT_SEARCH:
            search();
            break;
        case PC_CTR_ACT_DISCARD: {
            int arg = list[S.picked - 1] | (tab << 0xC);
            if ((tab == 6) && (c.src->data.misc[list[S.picked - 1] - 1].count >= 2))
                arg |= 0x10000;
            discard_open(arg);
            break;
        }
        case PC_CTR_ACT_DISCARD_ALL:
            discard_open(list[S.picked - 1] | (tab << 0xC) | 0x20000);
            break;
        case PC_CTR_ACT_SORT:
            sort_open(tab + 1);
            break;
        }
        return 0;
    }
    LEAVE_SFX();
    if (v == -2)
        return -2;
    rebuild();
    return 0;
}

static int discard_confirm_step(const pad* in) /* _discardMenu discardOne / discardMultiple */
{
    int r = act_input(in), i;
    if (r == -1)
        return 0;
    i = r + 1;
    if (i == 1) {
        pc_ctr c = ctx();
        vs_main_playSfxDefault(0x7E, 0x1C);
        pc_ctr_discard(&c, S.disc_cat, S.disc_slot, S.disc_multi ? 1 : S.disc_type, S.disc_count);
    } else {
        LEAVE_SFX();
    }
    if (i == -2)
        return -2;
    rebuild(); /* state 11 -> 4 */
    return 0;
}

static int discard_qty_step(const pad* in) /* _discardMenu determineDiscardAmount (1813) */
{
    static const char steps[] = { 1, 10, 100 };
    int i, a0, a2;
    if (in->pressed & (PADRup | PADRright | PADRdown)) {
        if (in->pressed & (PADRup | PADRdown)) {
            LEAVE_SFX();
            if (in->pressed & PADRup)
                return -2;
            rebuild();
            return 0;
        }
        if (S.disc_count == 0) {
            LEAVE_SFX();
            rebuild();
            return 0;
        }
        SEL_SFX();
        S.act.enabled[0] = S.act.enabled[1] = 1;
        act_set(2, actionMenuDiscard);
        S.disc_multi = 1;
        S.mode = PC_UI_CTR_DISCARD_CONFIRM;
        S.dirty = 1;
        return 0;
    }
    i = 0;
    a0 = a2 = S.disc_count;
    if (in->repeat & PADLup)
        i = 1;
    if (in->repeat & PADLdown)
        --i;
    if (i == 1)
        a0 = S.disc_count + steps[S.disc_step];
    else if (i == -1)
        a0 = S.disc_count - steps[S.disc_step];
    if (a0 >= 0) {
        if (S.disc_max >= a0)
            S.disc_count = (u_char)a0;
    }
    i = 0;
    a0 = S.disc_step;
    if (in->repeat & PADLright)
        i = -1;
    if (in->repeat & PADLleft)
        ++i;
    if (i == 1) {
        if ((a0 & 0xFF) == 2) {
            if (S.disc_count != S.disc_max)
                S.disc_count = S.disc_max;
        } else {
            S.disc_step = a0 + 1;
        }
    } else if (i == -1) {
        if (!(a0 & 0xFF)) {
            if (S.disc_count != 0)
                S.disc_count = 0;
        } else {
            S.disc_step = a0 - 1;
        }
    }
    if ((a0 != S.disc_step) || (a2 != S.disc_count)) {
        CHANGE_SFX();
        S.dirty = 1;
    }
    return 0;
}

static int sort_step(const pad* in)
{
    int r = act_input(in);
    if (r == -1)
        return 0;
    if (r >= 0) {
        pc_ctr c = ctx();
        SEL_SFX();
        pc_ctr_sort(&c, S.sort_cat, r);
    } else {
        LEAVE_SFX();
    }
    if (r + 1 == -2)
        return -2;
    rebuild(); /* state 12 -> 4 */
    return 0;
}

static int detail_step(const pad* in) /* the navigation's handleInput (_pollItemNavigation 677) */
{
    int prev, count, sel;
    if (in->pressed & (PADRup | PADRdown)) {
        LEAVE_SFX(); /* func_80104170 */
        if (in->pressed & PADRup)
            return -2;
        rebuild(); /* state 7 -> 1 */
        return 0;
    }
    sel = S.detail_sel;
    if ((in->held & (PADL1 | PADR1)) != (PADL1 | PADR1)) {
        prev = sel;
        count = pc_ctr_count(S.detail_cat, &vs_menuD_containerData->data);
        if (in->repeat & PADL1)
            sel = sel - 1 + count;
        if (in->repeat & PADR1)
            ++sel;
        if (sel >= count)
            sel -= count;
        if ((in->pressed & (PADL1 | PADR1)) && (sel == prev))
            INVALID_SFX();
    }
    sel = (u_char)sel;
    if (sel != S.detail_sel) {
        S.detail_sel = sel;
        cm()[(S.detail_cat + 0x51) * 2] = 0; /* func_80104114 */
        cm()[(S.detail_cat + 0x51) * 2 + 1] = sel;
        CHANGE_SFX(); /* func_80104078 */
        S.dirty = 1;
    }
    return 0;
}

static int commit_step(const pad* in) /* func_801072B0 state 1 and func_80107490 state 15 */
{
    int r = act_input(in), v;
    if (r == -1)
        return 0;
    v = r + 1;
    if (v == 1) {
        pc_ctr c = ctx();
        SEL_SFX();
        pc_ctr_commit(&c);
        return 1;
    }
    LEAVE_SFX();
    if (v == -2)
        return -2;
    rebuild(); /* -1: state 5 -> 0 */
    return 0;
}

/* One frame of MENUD's input. */
static void step(const pad* in)
{
    int r = 0;
    if (!S.active)
        return;
    switch (S.mode) {
    case PC_UI_CTR_TOP: {
        int var_s1 = D_800F4EE8.unk9E;
        if (in->pressed & PADRup) {
            LEAVE_SFX();
            hand_back(-2);
            return;
        }
        if (in->pressed & PADRdown) {
            LEAVE_SFX();
            hand_back(-1);
            return;
        }
        if (in->pressed & PADRright) {
            SEL_SFX();
            if (var_s1 + 1 < 3)
                mode_init(var_s1 + 1);
            else
                hand_back(var_s1 + 1);
            return;
        }
        if (in->repeat & PADLup)
            var_s1 += 2;
        if (in->repeat & PADLdown)
            ++var_s1;
        if (var_s1 >= 3)
            var_s1 -= 3;
        if (var_s1 != D_800F4EE8.unk9E) {
            CHANGE_SFX();
            D_800F4EE8.unk9E = var_s1;
            S.dirty = 1;
        }
        return;
    }
    case PC_UI_CTR_LIST: r = list_step(in); break;
    case PC_UI_CTR_PICK2: r = pick2_step(in); break;
    case PC_UI_CTR_ACTIONS: r = actions_step(in); break;
    case PC_UI_CTR_DISCARD_CONFIRM: r = discard_confirm_step(in); break;
    case PC_UI_CTR_DISCARD_QTY: r = discard_qty_step(in); break;
    case PC_UI_CTR_SORT: r = sort_step(in); break;
    case PC_UI_CTR_DETAIL: r = detail_step(in); break;
    case PC_UI_CTR_COMMIT_CONFIRM: r = commit_step(in); break;
    }
    S.dirty |= in->pressed != 0 || in->repeat != 0;
    if (r == -2) {
        hand_back(-2); /* 234.c state 5: -2 -> state 6 */
    } else if (r != 0) {
        top_enter(); /* 1 (committed) or -1 (Cross, nothing staged): back to the top menu */
    }
}

/* ---- the model ---- */

static const char* const KIND_SHIELD = "Shield";

static void row_text(pc_ui_ctr_row* e, vs_menu_containerData* d, int c, int index)
{
    const char* kind = "";
    e->help[0] = 0;
    switch (c) {
    case itemCategoryWeapon: {
        vs_main_inventoryWeapon* w = &d->weapons[index];
        vs_main_inventoryBlade* b = &d->blades[w->blade - 1];
        decode(w->name, e->name, sizeof e->name);
        e->id = b->id;
        e->material = b->material;
        kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_dagger + (u_char)b->category - 1);
        break;
    }
    case itemCategoryBlade: {
        vs_main_inventoryBlade* b = &d->blades[index];
        e->id = b->id;
        e->material = b->material;
        kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_dagger + (u_char)b->category - 1);
        copy_text(e->help, sizeof e->help, pc_ui_item_help(b->id - 1 + VS_ITEMHELP_BIN_INDEX_battleKnifeDesc));
        break;
    }
    case itemCategoryGrip: {
        vs_main_inventoryGrip* g = &d->grips[index];
        e->id = g->id;
        kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_shortGrip + g->category - 1);
        copy_text(e->help, sizeof e->help, pc_ui_item_help(g->id - 6));
        break;
    }
    case itemCategoryShield: {
        vs_main_inventoryShield* sh = &d->shields[index];
        e->id = sh->base.id;
        e->material = sh->base.material;
        kind = KIND_SHIELD;
        break;
    }
    case itemCategoryArmor: {
        vs_main_inventoryArmor* a = &d->armor[index];
        e->id = a->id;
        if (a->category == 7) {
            kind = "Accessory";
            copy_text(e->help, sizeof e->help, pc_ui_item_help(a->id - 223 + VS_ITEMHELP_BIN_INDEX_roodNecklace));
        } else {
            kind = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_armorCategory + a->category - 1);
            e->material = a->material;
        }
        break;
    }
    case itemCategoryGem: {
        vs_main_inventoryGem* g = &d->gems[index];
        e->id = g->id;
        copy_text(e->help, sizeof e->help, pc_ui_item_help(g->id - 261 + VS_ITEMHELP_BIN_INDEX_gemFeldspar));
        break;
    }
    default: {
        vs_main_inventoryMisc* m = &d->misc[index];
        e->id = m->id;
        e->count = (u_char)m->count;
        copy_text(e->help, sizeof e->help, pc_ui_item_help(m->id - 323 + VS_ITEMHELP_BIN_INDEX_cureRootDesc));
        break;
    }
    }
    if (c != itemCategoryWeapon)
        copy_text(e->name, sizeof e->name, pc_ui_item_name(e->id));
    if (e->material > 0 && kind[0]) {
        char both[128];
        int n = 0;
        const char* mat = pc_ui_material_name(e->material);
        while (*mat && n < 60)
            both[n++] = *mat++;
        both[n++] = ' ';
        copy_text(both + n, (int)sizeof both - n, kind);
        copy_text(e->sub, sizeof e->sub, both);
    } else {
        copy_text(e->sub, sizeof e->sub, e->material > 0 ? pc_ui_material_name(e->material) : kind);
    }
    if (!e->help[0] && c != itemCategoryWeapon && c != itemCategoryShield)
        copy_text(e->help, sizeof e->help, e->sub);
}

/* the rows of the current tab (func_80106C64's) into the model, with their texts */
static void fill_rows(void)
{
    static int category[PC_UI_CTR_ROWS], slot[PC_UI_CTR_ROWS], flags[PC_UI_CTR_ROWS];
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    pc_ctr c = ctx();
    int tab = tab_now(), i, n;
    vs_menu_containerData* d = tab == 7 ? &c.st->unk87B0 : &c.src->data;
    n = pc_ctr_rows(&c, tab, category, slot, flags, PC_UI_CTR_ROWS);
    m->rows = n;
    for (i = 0; i < n; ++i) {
        pc_ui_ctr_row* e = &m->row[i];
        zero(e, sizeof *e);
        e->category = category[i];
        e->slot = slot[i];
        e->flags = flags[i];
        row_text(e, d, category[i], slot[i]);
    }
    m->empty_text[0] = 0;
    if (n == 0)
        menud(m->empty_text, sizeof m->empty_text, S.dir + 27 + (tab == 7) * 2);
}

static void parent_name(char* out, int size, vs_menu_containerData* d, int c, int index)
{
    int p = pc_ctr_parent(c, index, d), s = pc_ctr_set_shield(c, index, d);
    out[0] = 0;
    if (p)
        decode(d->weapons[p - 1].name, out, size);
    else if (s)
        copy_text(out, size, pc_ui_item_name(d->shields[s - 1].base.id));
}

/* DP as the game shows it (hundredths, rounded up: equip_bridge.c dp_shown) */
static int dp_shown(int dp) { return (dp + 99) / 100; }

/* the focused record's card (a weapon / shield through the original's own UI builders) */
static void fill_card(pc_ui_ctr_card* k, vs_menu_containerData* d, int c, int index)
{
    zero(k, sizeof *k);
    if (c < 0 || c > 6 || index < 0)
        return;
    k->valid = 1;
    k->category = c;
    switch (c) {
    case itemCategoryWeapon: {
        vs_battle_uiWeapon w;
        vs_menuD_initUiWeapon(&w, &d->weapons[index], d);
        k->dp = w.currentDp, k->dp_max = w.maxDp, k->pp = w.currentPp, k->pp_max = w.maxPp;
        k->has_dp = k->has_pp = 1;
        k->str = w.currentStr, k->intel = w.currentInt, k->agl = w.currentAgility;
        k->gem_slots = d->grips[d->weapons[index].grip - 1].gemSlots;
        for (int i = 0; i < 3; ++i)
            k->gems += d->weapons[index].gems[i] != 0;
        break;
    }
    case itemCategoryShield: {
        vs_battle_uiShield s;
        vs_menuD_initUiShield(&s, &d->shields[index], d);
        k->dp = s.currentDp, k->dp_max = s.maxDp, k->pp = s.currentPp, k->pp_max = s.maxPp;
        k->has_dp = k->has_pp = 1;
        k->str = s.currentStr, k->intel = s.currentInt, k->agl = s.currentAgility;
        k->gem_slots = d->shields[index].base.gemSlots;
        for (int i = 0; i < 3; ++i)
            k->gems += d->shields[index].gems[i] != 0;
        break;
    }
    case itemCategoryBlade: {
        vs_main_inventoryBlade* b = &d->blades[index];
        k->dp = b->currentDp, k->dp_max = b->maxDp, k->pp = b->currentPp, k->pp_max = b->maxPp;
        k->has_dp = k->has_pp = 1;
        k->str = b->strength, k->intel = b->intelligence, k->agl = b->agility;
        break;
    }
    case itemCategoryGrip: {
        vs_main_inventoryGrip* g = &d->grips[index];
        k->str = g->strength, k->intel = g->intelligence, k->agl = g->agility;
        k->gem_slots = g->gemSlots;
        break;
    }
    case itemCategoryArmor: {
        vs_main_inventoryArmor* a = &d->armor[index];
        k->dp = a->currentDp, k->dp_max = a->maxDp;
        k->has_dp = a->category != 7;
        k->str = a->strength, k->intel = a->intelligence, k->agl = a->agility;
        break;
    }
    case itemCategoryGem: {
        vs_main_inventoryGem* g = &d->gems[index];
        k->str = g->strength, k->intel = g->intelligence, k->agl = g->agility;
        break;
    }
    default:
        k->valid = 0;
        return;
    }
    k->dp = dp_shown(k->dp);
    k->dp_max = dp_shown(k->dp_max);
    parent_name(k->parent, sizeof k->parent, d, c, index);
}

static void menu_entry(pc_ui_ctr_menu* mu, int i, const char* label, const char* desc, int enabled, int kind)
{
    copy_text(mu->label[i], sizeof mu->label[i], label);
    copy_text(mu->desc[i], sizeof mu->desc[i], desc);
    mu->enabled[i] = enabled;
    mu->kind[i] = kind;
}

/* the open menu's entries (labels and descriptions, the reasons) */
static void fill_menu(pc_ui_ctr_model* m)
{
    pc_ui_ctr_menu* mu = &m->menu;
    char a[PC_UI_CTR_TEXT], b[PC_UI_CTR_TEXT];
    int i;
    zero(mu, sizeof *mu);
    if (!S.act.active)
        return;
    mu->count = S.act.count;
    mu->focus = S.act.sel + S.act.off;
    mu->offset = S.act.off;
    if (S.mode == PC_UI_CTR_ACTIONS) {
        pc_ctr c = ctx();
        pc_ctr_actions x;
        pc_ctr_build_actions(&c, tab_now(), S.picked, &x);
        for (i = 0; i < x.count; ++i) {
            const char* label = "";
            const char* desc = "";
            a[0] = b[0] = 0;
            switch (x.kind[i]) {
            case PC_CTR_ACT_TRANSFER:
                menud(a, sizeof a, S.dir * 2 + 0x1F);
                label = a;
                menud(b, sizeof b, S.dir * 2 + 0x20);
                desc = b;
                break;
            case PC_CTR_ACT_SEARCH:
                label = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_searchItem);
                desc = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_searchItemDesc);
                break;
            case PC_CTR_ACT_DISCARD:
                label = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_discardItem);
                desc = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_discardItemDesc);
                break;
            case PC_CTR_ACT_DISCARD_ALL:
                label = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_discardAll);
                desc = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_discardAllDesc);
                break;
            case PC_CTR_ACT_SORT:
                label = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_sortItem);
                desc = pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_sortItemDesc);
                break;
            }
            if (x.reason[i] >= 0) {
                if (x.reason_menu[i] == 1)
                    menud_at(b, sizeof b, x.reason[i]);
                else if (x.reason_menu[i] == 2)
                    menud(b, sizeof b, x.reason[i]);
                else
                    copy_text(b, sizeof b, pc_ui_item_help(x.reason[i]));
                desc = b;
            }
            menu_entry(mu, i, label, desc, x.enabled[i], x.kind[i]);
        }
    } else if (S.mode == PC_UI_CTR_SORT) {
        const char* stats;
        int n = pc_ctr_sort_stats(S.sort_cat, &stats);
        for (i = 0; i < n && i < PC_UI_CTR_SORTS; ++i)
            menu_entry(mu, i, pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_sortMenu + stats[i] * 2),
                pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_sortMenuDesc + stats[i] * 2), 1, stats[i]);
    } else { /* the Yes / No's */
        for (i = 0; i < 2; ++i) {
            if (S.mode == PC_UI_CTR_COMMIT_CONFIRM) {
                menud(a, sizeof a, 8 + i);
                menud(b, sizeof b, S.dir + 10);
            } else {
                copy_text(a, sizeof a, pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_confirmYes + i));
                copy_text(b, sizeof b, pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_discardConfirm + (S.disc_multi ? 1 : S.disc_type)));
            }
            menu_entry(mu, i, a, b, 1, i);
        }
    }
}

static void fill(void)
{
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    vs_menu_inventoryStorage_t* st = vs_menu_inventoryStorage;
    pc_ctr c;
    int i, tab, focus_key, mode_rows;
    if (!st)
        return;
    c = ctx();
    tab = tab_now();
    m->session = S.active;
    m->stamp = now();
    m->mode = S.mode;
    m->dir = S.mode == PC_UI_CTR_TOP && !S.list.id ? -1 : S.dir;
    m->top = D_800F4EE8.unk9E;
    if (!m->top_label[0][0]) { /* the top menu's texts (func_801089BC's) */
        for (i = 0; i < 2; ++i) {
            menud(m->top_label[i], sizeof m->top_label[i], i * 2 + 12);
            menud(m->top_desc[i], sizeof m->top_desc[i], i * 2 + 13);
        }
        copy_text(m->top_label[2], sizeof m->top_label[2], "END");
        menud_at(m->top_desc[2], sizeof m->top_desc[2], VS_menuText_OFFSET_endTransfer);
    }
    m->tab = tab;
    m->list_active = S.list.active || S.mode == PC_UI_CTR_PICK2 || S.mode == PC_UI_CTR_ACTIONS
        || S.mode == PC_UI_CTR_DISCARD_CONFIRM || S.mode == PC_UI_CTR_DISCARD_QTY || S.mode == PC_UI_CTR_SORT;
    m->sel = S.list.sel;
    m->page = S.list.page;
    m->cursor = S.list.sel + S.list.page;
    if (S.mode == PC_UI_CTR_DETAIL)
        m->cursor = S.detail_sel;
    m->picked = S.mode == PC_UI_CTR_PICK2 || S.mode == PC_UI_CTR_ACTIONS ? S.picked - 1 : -1;
    m->staged = pc_ctr_transfer_count(&c);
    for (i = 0; i < 7; ++i) {
        m->bag_cap[i] = vs_mainMenu_inventoryItemCapacities[i];
        m->box_cap[i] = pc_ctr_caps[i];
        m->bag_count[i] = pc_ctr_count(i, &st->unk105B0.data);
        m->box_count[i] = pc_ctr_count(i, &st->unkC430.data);
        m->tab_count[i] = S.mode == PC_UI_CTR_TOP ? 0 : pc_ctr_count(i, &c.src->data);
        m->tab_cap[i] = S.dir ? vs_mainMenu_inventoryItemCapacities[i] : pc_ctr_caps[i];
        m->dest_count[i] = pc_ctr_count(i, &c.dst->data);
        m->dest_staged[i] = pc_ctr_count(i, &st->unk87B0);
        m->dest_cap[i] = S.dir ? pc_ctr_caps[i] : vs_mainMenu_inventoryItemCapacities[i];
    }
    m->tab_count[7] = m->staged;
    m->tab_cap[7] = 64;
    m->misc_after = S.misc_after;
    if (S.dirty) { /* the panel's live count: the destination's misc with the staged misc poured in, on a
                    * host copy (D_80109A82 / unk4BB0 are recomputed only where the original does) */
        static vs_menu_containerData scratch;
        int e;
        vs_battle_memcpy(scratch.misc, c.dst->data.misc, sizeof scratch.misc);
        for (i = 0; i < 64 && (e = st->itemsToTransfer[i]) != 0; ++i)
            if ((e >> 8) == itemCategoryMisc)
                pc_ctr_copy_item(PC_CTR_COPY | itemCategoryMisc, &scratch, (e - 1) & 0xFF, &st->unk87B0);
        m->misc_live = pc_ctr_count(itemCategoryMisc, &scratch);
    }
    m->misc_cap = S.dir ? 0x100 : 0x40;
    m->discard_type = S.disc_multi ? 1 : S.disc_type;
    m->discard_count = S.disc_count;
    m->discard_max = S.disc_max;
    m->discard_step = S.disc_step;
    m->detail_index = S.detail_sel;
    m->detail_category = S.detail_cat;
    m->detail_count = S.mode == PC_UI_CTR_DETAIL ? pc_ctr_count(S.detail_cat, &c.src->data) : 0;
    m->detail_slot = S.mode == PC_UI_CTR_DETAIL ? pc_ctr_indices(S.detail_cat, c.src)[S.detail_sel] - 1 : -1;
    fill_menu(m);
    /* the confirm's question and the info line */
    m->confirm_text[0] = 0;
    if (S.mode == PC_UI_CTR_COMMIT_CONFIRM)
        menud(m->confirm_text, sizeof m->confirm_text, S.dir + 10);
    else if (S.mode == PC_UI_CTR_DISCARD_CONFIRM || S.mode == PC_UI_CTR_DISCARD_QTY)
        copy_text(m->confirm_text, sizeof m->confirm_text,
            pc_ui_item_help(VS_ITEMHELP_BIN_INDEX_discardConfirm + (S.disc_multi ? 1 : S.disc_type)));
    m->info[0] = 0;
    mode_rows = S.mode != PC_UI_CTR_TOP;
    if (S.mode == PC_UI_CTR_TOP) {
        if (m->top != 2)
            menud(m->info, sizeof m->info, m->top * 2 + 13);
        else
            menud_at(m->info, sizeof m->info, VS_menuText_OFFSET_endTransfer);
    } else if (m->menu.count > 0 && m->menu.focus >= 0 && m->menu.focus < m->menu.count) {
        copy_text(m->info, sizeof m->info, m->menu.desc[m->menu.focus]);
    } else if (S.mode == PC_UI_CTR_DETAIL && m->detail_slot >= 0) {
        pc_ui_ctr_row e;
        zero(&e, sizeof e);
        row_text(&e, &c.src->data, S.detail_cat, m->detail_slot);
        copy_text(m->info, sizeof m->info, e.help);
    } else if (mode_rows && m->rows > 0 && m->cursor >= 0 && m->cursor < m->rows) {
        copy_text(m->info, sizeof m->info, m->row[m->cursor].help);
    } else if (mode_rows) {
        copy_text(m->info, sizeof m->info, m->empty_text);
    }
    /* the card: refilled when the focus or the data changed */
    focus_key = (S.mode << 24) ^ (tab << 20) ^ (m->cursor << 8) ^ S.detail_sel;
    if (S.dirty || focus_key != S.focus_seen) {
        S.focus_seen = focus_key;
        if (S.mode == PC_UI_CTR_DETAIL && m->detail_slot >= 0)
            fill_card(&m->card, &c.src->data, S.detail_cat, m->detail_slot);
        else if (mode_rows && m->rows > 0 && m->cursor >= 0 && m->cursor < m->rows)
            fill_card(&m->card, tab == 7 ? &st->unk87B0 : &c.src->data, m->row[m->cursor].category,
                m->row[m->cursor].slot);
        else
            zero(&m->card, sizeof m->card);
    }
    S.dirty = 0;
    ++m->ticks;
}

/* ---- the pointer's requests (ui_container.cpp) ---- */

static unsigned bits_to_pad(unsigned b)
{
    unsigned p = 0;
    if (b & PC_UI_UP) p |= PADLup;
    if (b & PC_UI_DOWN) p |= PADLdown;
    if (b & PC_UI_LEFT) p |= PADLleft;
    if (b & PC_UI_RIGHT) p |= PADLright;
    if (b & PC_UI_CONFIRM) p |= PADRright;
    if (b & PC_UI_BACK) p |= PADRdown;
    if (b & PC_UI_EXIT) p |= PADRup;
    if (b & PC_UI_ALT) p |= PADRleft;
    if (b & PC_UI_TAB_PREV) p |= PADL1;
    if (b & PC_UI_TAB_NEXT) p |= PADR1;
    return p;
}

static void press(unsigned padbits)
{
    pad in;
    in.pressed = in.repeat = in.held = padbits;
    step(&in);
    if (S.active && S.switching) { /* the buttons let go: the rebuild */
        pad none = { 0, 0, 0 };
        step(&none);
    }
}

/* the cursor of what shows moved to index (no game write but the top menu's unk9E, as Up / Down) */
static void focus(int what, int index)
{
    if (what == PC_UI_CTR_F_TOP && S.mode == PC_UI_CTR_TOP && index >= 0 && index < 3) {
        if (index != D_800F4EE8.unk9E) {
            CHANGE_SFX();
            D_800F4EE8.unk9E = index;
        }
    } else if (what == PC_UI_CTR_F_ROW && S.list.active && index >= 0 && index < S.list.count) {
        int abs0 = S.list.sel + S.list.page;
        if (index < S.list.page || index > S.list.page + 7) { /* off the original's window: scroll it there */
            int page = index - 6;
            S.list.page = page < 0 ? 0 : page > S.list.count - 8 ? S.list.count - 8 : page;
            if (S.list.count <= 8)
                S.list.page = 0;
        }
        S.list.sel = index - S.list.page;
        if (S.list.sel + S.list.page != abs0)
            CHANGE_SFX();
    } else if (what == PC_UI_CTR_F_MENU && S.act.active && index >= 0 && index < S.act.count) {
        int abs0 = S.act.sel + S.act.off;
        if (index < S.act.off || index > S.act.off + 6) {
            int off = index - 5;
            S.act.off = off < 0 ? 0 : off > S.act.count - 7 ? S.act.count - 7 : off;
            if (S.act.count <= 7)
                S.act.off = 0;
        }
        S.act.sel = index - S.act.off;
        if (S.act.sel + S.act.off != abs0)
            CHANGE_SFX();
    }
    S.dirty = 1;
}

static void walk_tab(int target)
{
    int guard;
    if (target < 0 || target > 7)
        return;
    for (guard = 0; guard < 8 && S.active && tab_now() != target
         && (S.mode == PC_UI_CTR_LIST || S.mode == PC_UI_CTR_PICK2); ++guard) {
        int fwd = (target - tab_now()) & 7;
        if (S.mode != PC_UI_CTR_LIST || S.switching)
            break;
        press(fwd <= 4 ? PADR1 : PADL1);
    }
}

static void requests(void)
{
    pc_ui_ctr_req r;
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    while (pc_ui_ctr_pop(&r)) {
        if (!S.active)
            continue;
        ++m->requests;
        switch (r.kind) {
        case PC_UI_CTR_REQ_PRESS: press(bits_to_pad((unsigned)r.a)); break;
        case PC_UI_CTR_REQ_FOCUS: focus(r.a, r.b); break;
        case PC_UI_CTR_REQ_TAB: walk_tab(r.a); break;
        default: break;
        }
    }
}

/* ---- the session (menu_bridge.c's direct tick) ---- */

static struct {
    int latched, run, hide_set;
    char hide_saved;
    const pc_ui_data_menu7* m7;
    unsigned real_pressed;
    int real_repeat;
    int call_press;
    int save;            /* the Data screen's save list is up (container mode) */
    pc_ui_data_goal goal; /* a prompt's answer (the player's click) */
} F;

int pc_ui_container_framed(void) { return F.latched; }

int pc_ui_container_begin(void)
{
    vs_menu_inventoryStorage_t* st = vs_menu_inventoryStorage;
    pc_ctr c;
    if (st == NULL || !F.latched)
        return 0;
    zero(&S, sizeof S);
    S.active = 1;
    S.first_top = 1;
    S.dir = 0;
    mirror_pointers();
    /* 234.c state 0 */
    vs_battle_memcpy(&st->unkC430.data, &st->unkFB0, sizeof st->unkC430.data);
    c = ctx();
    pc_ctr_bag_to_container(&c);
    top_enter();
    pc_ui_ctr_reset_mailbox();
    {
        pc_ui_ctr_model* m = pc_ui_ctr_state();
        m->session = 1;
        m->handed_back = 0;
        m->phase = PC_UI_CTR_PH_TRANSFER;
        m->rows = 0;
    }
    fill();
    return 1;
}

int pc_ui_container_frame(int settling)
{
    pad in;
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    if (!S.active)
        return PC_UI_CTR_HANDED_BACK;
    handed = 0;
    mirror_pointers();
    if (F.latched && !m->latched) { /* a snapshot brought the session back, not the host model */
        m->latched = 1;
        m->run = F.run;
        m->hide_saved = F.hide_saved;
        m->handed_back = 0;
        fill_rows();
    }
    m->phase = PC_UI_CTR_PH_TRANSFER;
    m->settling = settling;
    if (!settling) {
        requests();
        if (S.active && !handed) {
            in.pressed = vs_main_buttonsPressed.all;
            in.repeat = (unsigned)vs_main_buttonRepeat;
            in.held = vs_main_buttonsState;
            step(&in);
        }
    } else {
        pc_ui_ctr_req r;
        while (pc_ui_ctr_pop(&r)) /* the opening press's frames: nothing taken */
            ;
    }
    if (handed) {
        m->session = 0;
        m->handed_back = 1;
        m->phase = PC_UI_CTR_PH_BUSY;
        m->stamp = now();
        return PC_UI_CTR_HANDED_BACK;
    }
    fill();
    return PC_UI_CTR_STAY;
}

void pc_ui_container_abandon(int reset)
{
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    (void)reset;
    S.active = 0;
    m->session = 0;
}

/* ---- the MENU7 frame (260.c's wrapper) ---- */

static void hide_menu(int on)
{
    if (on && !F.hide_set) {
        F.hide_saved = vs_mainMenu_hideMenu;
        vs_mainMenu_hideMenu = 1;
        F.hide_set = 1;
    } else if (!on && F.hide_set) {
        vs_mainMenu_hideMenu = F.hide_saved;
        F.hide_set = 0;
    }
}

int pc_ui_container_on(void) { return F.latched; }

void pc_ui_container_end(void)
{
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    if (!F.latched && !F.hide_set)
        return;
    hide_menu(0);
    if (pc_ui_data_container_on())
        pc_ui_data_container_end();
    pc_ui_menu_frame_hud(0);
    F.latched = 0;
    F.save = 0;
    S.active = 0;
    zero(m, sizeof *m);
    pc_ui_ctr_reset_mailbox();
}

void pc_ui_container_forget(void)
{
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    if (pc_ui_data_container_on())
        pc_ui_data_container_end();
    if (F.latched || F.hide_set)
        vs_mainMenu_hideMenu = 0;
    zero(&F, sizeof F);
    zero(&S, sizeof S);
    zero(m, sizeof *m);
    pc_ui_ctr_reset_mailbox();
}

static void latch(int run)
{
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    zero(m, sizeof *m);
    zero(&S, sizeof S);
    F.latched = 1;
    F.run = run;
    F.save = 0;
    zero(&F.goal, sizeof F.goal);
    pc_ui_ctr_reset_mailbox();
    m->latched = 1;
    m->run = run;
    m->dir = -1;
    m->phase = PC_UI_CTR_PH_BUSY;
    m->stamp = now();
    hide_menu(1);
    m->hide_saved = F.hide_saved;
    pc_ui_menu_frame_hud(1);
}

static unsigned pad_bits(unsigned press)
{
    unsigned p = 0;
    if (press & PC_UI_DATA_PRESS_UP)
        p |= PADLup;
    if (press & PC_UI_DATA_PRESS_DOWN)
        p |= PADLdown;
    if (press & PC_UI_DATA_PRESS_CIRCLE)
        p |= PADRright;
    if (press & PC_UI_DATA_PRESS_CROSS)
        p |= PADRdown;
    return p;
}

static int prompt_state(int s) { return s == 1 || s == 5 || s == 12; }

/* the goals the pointer posted for MENU7's prompts and wait (the newest wins) */
static void goals(void)
{
    pc_ui_ctr_req r;
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    while (pc_ui_ctr_pop(&r)) {
        int kind = 0, a = 0;
        if (r.kind != PC_UI_CTR_REQ_GOAL)
            continue;
        if (r.a == PC_UI_CTR_G_YES)
            kind = PC_UI_DATA_GOAL_ANSWER, a = 1;
        else if (r.a == PC_UI_CTR_G_NO)
            kind = PC_UI_DATA_GOAL_ANSWER, a = 0;
        else if (r.a == PC_UI_CTR_G_DISMISS)
            kind = PC_UI_DATA_GOAL_DISMISS;
        if (!kind)
            continue;
        if (F.goal.kind == kind && F.goal.a == a)
            continue; /* a double click goes on where it is */
        {
            int cool = F.goal.cool;
            zero(&F.goal, sizeof F.goal);
            F.goal.kind = kind;
            F.goal.a = a;
            F.goal.cool = cool;
        }
        ++m->requests;
    }
}

void pc_ui_container_call_begin(const pc_ui_data_menu7* m7, int entry)
{
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    F.m7 = m7;
    F.real_pressed = vs_main_buttonsPressed.all;
    F.real_repeat = vs_main_buttonRepeat;
    F.call_press = 0;
    if (entry == 0) {
        int framed = pc_ui_menus && pc_overlay_active && pc_overlay_active();
        if (F.latched) /* a run that never ended (no heartbeat since): its frame goes */
            pc_ui_container_end();
        ++*m7->run;
        *m7->framed = framed;
        if (framed)
            latch(*m7->run);
    } else if (*m7->framed == 0 && *m7->run == 0 && F.latched && m->handed_back) {
        /* MENU7 reloaded after MENUD (its statics restarted with the overlay): the latch back */
        *m7->run = F.run;
        *m7->framed = 1;
    }
    /* a save state that brings an old latch back without the host's frame runs the rest original */
    if (*m7->framed && !(F.latched && F.run == *m7->run)) {
        *m7->framed = 0;
        if (F.latched)
            pc_ui_container_end();
    }
    *m7->no_draw = *m7->framed;
    if (!*m7->framed)
        return;
    m->stamp = now();
    if (entry == 10 || entry == 11) {
        if (!F.save) {
            pc_ui_data_container_begin(F.run);
            F.save = 1;
        }
        pc_ui_data_frame_stamp(8);
        pc_ui_data_frame_inject(&vs_main_buttonsPressed.all, &vs_main_buttonRepeat);
    } else {
        unsigned press = 0;
        int ready = m->prompt_ready && prompt_state(entry) && m->m7_state == entry;
        int waiting = entry == 4 && m->phase == PC_UI_CTR_PH_WAIT;
        int real = vs_main_buttonsPressed.all != 0 || vs_main_buttonRepeat != 0;
        goals();
        if (pc_ui_data_plan_yesno(ready, m->prompt_yes, waiting, real, &F.goal, &press) == PC_UI_DATA_PLAN_PRESS) {
            vs_main_buttonsPressed.all = pad_bits(press);
            vs_main_buttonRepeat = (int)pad_bits(press);
            ++m->presses;
        }
    }
    F.call_press = vs_main_buttonsPressed.all != 0 || vs_main_buttonRepeat != 0;
}

/* the phase of MENU7's state after a call */
static int phase_of(int state, int ready)
{
    switch (state) {
    case 1: return ready ? PC_UI_CTR_PH_PROMPT_CHECK : PC_UI_CTR_PH_BUSY;
    case 4: return PC_UI_CTR_PH_WAIT;
    case 5: return ready ? PC_UI_CTR_PH_PROMPT_WARN : PC_UI_CTR_PH_BUSY;
    case 7: return PC_UI_CTR_PH_TRANSFER;
    case 10:
    case 11: return PC_UI_CTR_PH_SAVE;
    case 12: return ready ? PC_UI_CTR_PH_PROMPT_CANCEL : PC_UI_CTR_PH_BUSY;
    case 13:
    case 14:
    case 15:
    case 16: return PC_UI_CTR_PH_CLOSING;
    default: return PC_UI_CTR_PH_BUSY;
    }
}

/* MCMAN's text table is whole after this call: saveContainerMenu's own windows, not the Data
 * flow's state numbers. Loaded from state 2's _initMemcard(0) to state 7's _shutdownMemcard
 * (the call from 7 ends in 8, freed), again from state 9's _initMemcard(0) (8 and 9 still
 * loading) to state 15's _shutdownMemcard (16). */
static int mcman_loaded(const pc_ui_data_menu7* m7, int entry, int state)
{
    if (!*m7->text_table)
        return 0;
    return (state >= 3 && state <= 7 && entry != 7) || (state >= 10 && state <= 15);
}

int pc_ui_container_call_end(int entry, int state, int r)
{
    const pc_ui_data_menu7* m7 = F.m7;
    pc_ui_ctr_model* m = pc_ui_ctr_state();
    vs_main_buttonsPressed.all = F.real_pressed;
    vs_main_buttonRepeat = F.real_repeat;
    if (m7 && *m7->framed) {
        unsigned gated = 0;
        int was_prompt = m->phase == PC_UI_CTR_PH_PROMPT_CHECK || m->phase == PC_UI_CTR_PH_PROMPT_WARN
            || m->phase == PC_UI_CTR_PH_PROMPT_CANCEL;
        if (state == 10 || state == 11) {
            if (!F.save) {
                pc_ui_data_container_begin(F.run);
                F.save = 1;
            }
            pc_ui_data_menu7_publish(m7, 8); /* the save flow, as the Data screen's state 8 */
        } else {
            gated = *m7->gated;
            *m7->gated = 0;
            if (F.save) {
                pc_ui_data_container_end();
                F.save = 0;
            }
        }
        m->gated += gated;
        m->m7_state = state;
        /* _promptYesNo's handleInput drew its (gated) cursor this call: it takes the next press */
        m->prompt_ready = prompt_state(state) && entry == state && gated > 0 && !F.call_press;
        m->prompt_yes = vs_battle_getMenuItem(0x1E)->selected != 0;
        m->phase = phase_of(state, m->prompt_ready);
        if (was_prompt && !m->prompt_ready && prompt_state(state) && entry == state && F.call_press)
            m->phase = PC_UI_CTR_PH_BUSY; /* a press went in: its answer settles */
        m->location = (u_char)D_800F4E6B;
        m->prompt_text[0] = m->wait_text[0] = 0;
        if (state == 1)
            decode(&m7_text[VS_container_OFFSET_checkContainer], m->prompt_text, sizeof m->prompt_text);
        else if (state == 12)
            decode(&m7_text[VS_container_OFFSET_cancelWarning], m->prompt_text, sizeof m->prompt_text);
        else if ((state == 4 || state == 5) && mcman_loaded(m7, entry, state))
            decode(*m7->text_table + VS_MCMAN_BIN_OFFSET_containerWarn, m->prompt_text, sizeof m->prompt_text);
        if (state == 4 && mcman_loaded(m7, entry, state))
            decode(*m7->text_table + VS_MCMAN_BIN_OFFSET_emptyCard, m->wait_text, sizeof m->wait_text);
        /* decoded while MCMAN is loaded, kept for the rest of the run */
        if (mcman_loaded(m7, entry, state) && m->location >= 0x40) {
            const unsigned short* table = *m7->text_table;
            int loc = m->location - 0x40;
            if (loc < 32)
                decode(&table[table[loc + VS_MCMAN_BIN_INDEX_containerLocations]], m->location_name,
                    sizeof m->location_name);
        }
        m->stamp = now();
        vs_mainMenu_setMenuCommand(menuActionNone); /* no "MENU" title over the screen */
        /* MENU7's information box (text box 7, inited with the CAUTION header for its
         * messages, which the gate keeps empty) would show its header through the
         * backdrop: none while framed (presentation only; its init and dismiss are MENU7's) */
        if (vs_battle_textBoxes[7].unk0.unk0_8 == informationTextBoxHeaderCaution)
            vs_battle_textBoxes[7].unk0.unk0_8 = informationTextBoxHeaderNone;
        if (r != 0) {
            *m7->framed = 0;
            pc_ui_container_end();
        }
    }
    if (m7)
        *m7->no_draw = 0;
    return r;
}

int pc_ui_container_call(const pc_ui_data_menu7* m7, unsigned char* state, pc_ui_menu7_fn menu)
{
    int entry = *state, r;
    pc_ui_container_call_begin(m7, entry);
    r = menu(state);
    return pc_ui_container_call_end(entry, *state, r);
}
