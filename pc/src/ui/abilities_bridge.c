/* Modern menus, Abilities tab: the game side (battle builds). menu_bridge.c
 * calls pc_ui_abilities_fill at the start of every tick of the shell's top
 * level (and when a session opens) and pc_ui_abilities_refresh after the
 * shell's tick: the snapshot is filled only while the Abilities screen shows,
 * and before every request. equip_bridge.c's request loop hands the
 * Abilities requests to pc_ui_abilities_request; menu_bridge.c then asks
 * pc_ui_abilities_cast_taken whether an art was accepted and hands it to the
 * game with pc_ui_abilities_cast.
 *
 * What the snapshot (ui_model.h pc_ui_abilities) holds and where it comes from:
 *   chain / defense     BATTLE vs_battle_chainAbilityOffsets /
 *                       defenseAbilityOffsets (14 action ids each, the game's
 *                       list order), those with vs_main_actions[id].unlocked,
 *                       as MENU2 (_chainAbilityMenu, _defenseAbilityMenu)
 *                       lists them; name = the action's, description = MENU2's
 *                       battleAbilities string chain/defenseAbilityEffect0 + the
 *                       position in the offsets table; RISK cost = the action's
 *                       cost (MENU2 _setRiskCost)
 *   buttons             vs_main_settings.mappedChainAbilities /
 *                       mappedDefenseAbilities [0..2] (Circle, Triangle,
 *                       Square; the action id, 0 none)
 *   next ability        vs_main_battleAbilitiesPointsRequirements
 *                       [kills.battleAbilitiesUnlocked] - kills.total, none
 *                       once 22 are unlocked (MENU2
 *                       _setNextAbilityUnlockPointsRemaining)
 *   break arts          category cat 1..10 (10 bare hands), art i: action
 *                       184 + (cat - 1) * 4 + i (MENU1 _weaponArtsMenu); name
 *                       = the action's, description = MENU1's string
 *                       daggerArt1 + (cat - 1) * 4 + i, the category's name,
 *                       description and "no arts" line (cat - 1) * 3 + 0/1/2;
 *                       learned / kills by c = cat % 10 (artsLearned[c],
 *                       kills.weaponCategories[c]); a locked art needs
 *                       vs_main_artsPointsRequirements[c][i] kills (MENU1
 *                       _setNextArtUnlockPointsRemaining: the next one is
 *                       [c][artsLearned[c]]); HP cost = the action's
 *   usable              MENU1's row checks: the category is the equipped
 *                       weapon's (vs_battle_characterState->
 *                       equippedWeaponCategory), vs_battle_getActionFlags(0,
 *                       id) == 0 (1 busy, 2 HP too low, 4 sheathed / status)
 *                       and the menu's disable mask D_800F4EA0 & 0x15F clear
 *
 * Requests:
 *   ABIL_MAP   MENU2 _mapAbility: the button already holds the ability ->
 *              cleared (leave sound); else the set sound, the ability taken
 *              off every button and put on this one. The battle reads the
 *              settings live: nothing else to do.
 *   ART_CAST   the same checks again, and MAINMENU resident (the snapshot
 *              says "Loading..." meanwhile; refused: the invalid sound).
 *              Accepted, pc_ui_abilities_cast(1) (the shell returned no
 *              result on that tick) writes MENU1's hand-off (vs_menu1_exec
 *              artsMenu + executeArt): D_800F4E98.executeAbility.s32 = id,
 *              executeAbilityType 5, D_800F4E98.unk2 = 6, returnState = the
 *              top level as MENU1's own currentMenu (2 | 0x40, command 1
 *              opened from the menu), the list's cursor memory on the art's
 *              row and the top level's cursor on Break Arts.
 *              menu_bridge.c closes the shell and ends the menu as MENU1
 *              does (close_for_cast: 0x7F at once, no _quitDelay) and BATTLE
 *              func_800735F8 runs the art (unk2 6: func_80072EC4,
 *              targeting). Cancelling the target (func_800CAB40) returns
 *              to the top level, not to returnState (MENU1's list): the
 *              shell again on Abilities > Break Arts with the art focused
 *              (menu_bridge.c pc_ui_menu_cast_cancelled). A cast from the
 *              original list (Square) is MENU1's own and goes back there.
 * Only BATTLE and main routines and data are used (MAINMENU may not be
 * loaded yet). The description tables are static copies of MENU1's and
 * MENU2's generated string assets (the overlays' own copies are their data,
 * restarted on each load). */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "../../../src/SLUS_010.40/sfx.h"
#include "build/assets/MENU/MENU1.PRG/strings.h"
#include "build/assets/MENU/MENU2.PRG/battleAbilities.h"
#include "ui_shell.h"

/* ui_text.h (stdio in a game-header unit is avoided) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);

void pc_ui_abilities_fill(int mainmenu);
void pc_ui_abilities_refresh(void);
int pc_ui_abilities_request(const pc_ui_request* r);
int pc_ui_abilities_cast_taken(void);
void pc_ui_abilities_cast(int go);
int pc_ui_equip_busy(void); /* equip_bridge.c: a weapon / shield model reload runs */
int pc_ui_magick_cast_taken(void); /* magick_bridge.c: one hand-off per tick */

/* MENU2.PRG battleAbilities and MENU1.PRG strings, under our own names. */
static u_short ability_text[] = {
#include "build/assets/MENU/MENU2.PRG/battleAbilities.vsString"
};
static u_short arts_text[] = {
#include "build/assets/MENU/MENU1.PRG/strings.vsString"
};

#define ABILITY_TYPES 2
#define FIRST_ART vs_main_actions_daggerArt1 /* 184 */
#define ART_MASK 0x15F /* D_800F4EA0 bits that grey Break Arts (func_800C930C, MENU1) */
#define ALL_ABILITIES 22 /* kills.battleAbilitiesUnlocked when none is left (MENU2) */
#define SETTINGS_CHAIN 0
#define TOP_LEVEL_BREAK_ARTS (2 | 0x40) /* currentMenu while MENU1 runs from the top level */
#define BREAK_ARTS_COMMAND 1           /* the top level's row (vs_battle_submenuStates[0]) */

static pc_ui_abilities snap;
static int cast_id;  /* the art accepted on this tick (pc_ui_abilities_cast hands it over), 0 none */
static int resident; /* MAINMENU.PRG drives the top level (menu_bridge.c mainmenu_resident) */
static int filled;   /* the snapshot was filled on this tick */
static int text_decoded;
static char ability_help[ABILITY_TYPES][PC_UI_ABILITIES][200];
static char art_help[PC_UI_ART_CATEGORIES][PC_UI_ARTS][200];
static char category_name[PC_UI_ART_CATEGORIES][24];
static char category_help[PC_UI_ART_CATEGORIES][2][160]; /* the description, the "no arts" line */

static const u_char* table_string(const u_short* table, int i) { return (const u_char*)&table[table[i]]; }

static void decode_text(void)
{
    int t, i, c;
    if (text_decoded)
        return;
    for (t = 0; t < ABILITY_TYPES; ++t)
        for (i = 0; i < PC_UI_ABILITIES; ++i)
            pc_ui_text_decode(table_string(ability_text,
                                  (t ? VS_battleAbilities_INDEX_defenseAbilityEffect0
                                     : VS_battleAbilities_INDEX_chainAbilityEffect0)
                                      + i),
                512, ability_help[t][i], sizeof ability_help[t][i]);
    for (c = 0; c < PC_UI_ART_CATEGORIES; ++c) {
        pc_ui_text_decode(table_string(arts_text, c * 3), 128, category_name[c], sizeof category_name[c]);
        pc_ui_text_decode(table_string(arts_text, c * 3 + 1), 512, category_help[c][0], sizeof category_help[c][0]);
        pc_ui_text_decode(table_string(arts_text, c * 3 + 2), 512, category_help[c][1], sizeof category_help[c][1]);
        for (i = 0; i < PC_UI_ARTS; ++i)
            pc_ui_text_decode(table_string(arts_text, VS_strings_INDEX_daggerArt1 + c * PC_UI_ARTS + i), 512,
                art_help[c][i], sizeof art_help[c][i]);
    }
    text_decoded = 1;
}

/* UTF-8 text into out, cut at a code point boundary (as equip_bridge.c). */
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

static vs_battle_actor2* ashley(void)
{
    return vs_battle_characterState ? psx_get(vs_battle_characterState->unk3C) : NULL;
}

static void action_name(int id, char* out, int size)
{
    pc_ui_text_decode((const u_char*)vs_main_actions[id].name, sizeof vs_main_actions[id].name, out, size);
}

static char* mapped(int type) /* the settings' three buttons */
{
    return type == SETTINGS_CHAIN ? vs_main_settings.mappedChainAbilities : vs_main_settings.mappedDefenseAbilities;
}

static const char* offsets(int type)
{
    return type == SETTINGS_CHAIN ? vs_battle_chainAbilityOffsets : vs_battle_defenseAbilityOffsets;
}

/* The learned position of action id in the type's list, -1 when it is not a learned one. */
static int learned_index(int type, int id)
{
    int i;
    const char* table = offsets(type);
    for (i = 0; i < PC_UI_ABILITIES; ++i) {
        int action = (u_char)table[i];
        if (action == id)
            return vs_main_actions[action].unlocked ? i : -1;
    }
    return -1;
}

static void fill_list(int type)
{
    pc_ui_ability_list* l = &snap.lists[type];
    const char* table = offsets(type);
    char* buttons = mapped(type);
    int i, b;
    for (b = 0; b < PC_UI_ABIL_BUTTONS; ++b) {
        l->slots[b] = -1;
        l->slot_ids[b] = (u_char)buttons[b];
    }
    for (i = 0; i < PC_UI_ABILITIES; ++i) {
        int action = (u_char)table[i];
        pc_ui_ability* e;
        if (!vs_main_actions[action].unlocked)
            continue;
        e = &l->entries[l->count];
        e->id = action;
        action_name(action, e->name, sizeof e->name);
        copy_text(e->help, sizeof e->help, ability_help[type][i]);
        e->cost = vs_main_actions[action].cost;
        e->button = -1;
        for (b = 0; b < PC_UI_ABIL_BUTTONS; ++b)
            if ((u_char)buttons[b] == action) {
                if (e->button < 0)
                    e->button = b;
                l->slots[b] = l->count;
            }
        ++l->count;
    }
}

/* The weapon a category's arts need, in our own words (MENU1's category
 * names are capitals and the game never says this: its row is just greyed). */
static const char* const category_reason[PC_UI_ART_CATEGORIES] = {
    "Needs a dagger in hand.",
    "Needs a sword in hand.",
    "Needs a great sword in hand.",
    "Needs an axe or a mace in hand.",
    "Needs a great axe in hand.",
    "Needs a staff in hand.",
    "Needs a heavy mace in hand.",
    "Needs a polearm in hand.",
    "Needs a crossbow in hand.",
    "Fight with bare hands to use it.",
};

/* Why a learned art cannot be cast now ("" when it can): MENU1's checks, the
 * menu's disable mask first (in the game's message order, as the shell's
 * greyed rows), then the weapon type, then the action's own flags. */
static void art_reason(int category, int id, char* out, int size)
{
    static const char* const by_bit[9] = {
        0,
        "Not while carrying a cube.",
        "Not while hanging from a ledge.",
        "Only in battle mode: draw your weapon first.",
        0,
        0,
        "Not enough HP.",
        0,
        "Paralysed: break arts cannot be used.",
    };
    u_int mask = (u_int)D_800F4EA0 & ART_MASK;
    int b, flags;
    out[0] = 0;
    for (b = 0; b < 9; ++b)
        if (((mask >> b) & 1) && by_bit[b]) {
            copy_text(out, size, by_bit[b]);
            return;
        }
    if (mask) {
        copy_text(out, size, "Not usable right now.");
        return;
    }
    if (category != vs_battle_characterState->equippedWeaponCategory) {
        copy_text(out, size, category_reason[category - 1]);
        return;
    }
    flags = vs_battle_getActionFlags(0, id);
    if (flags & 2)
        copy_text(out, size, "Not enough HP.");
    else if ((flags & 4) && !vs_battle_actors[0]->weaponDrawn)
        copy_text(out, size, "Draw your weapon first.");
    else if (flags & 4)
        copy_text(out, size, "Paralysed: break arts cannot be used.");
    else if (flags)
        copy_text(out, size, "Not right now.");
}

static void fill_arts(void)
{
    int cat, i;
    for (cat = 1; cat <= PC_UI_ART_CATEGORIES; ++cat) {
        pc_ui_art_category* k = &snap.categories[cat - 1];
        int c = cat % 10, learned = vs_main_artsStatus.artsLearned[c], kills = vs_main_artsStatus.kills.weaponCategories[c];
        int any = 0;
        copy_text(k->name, sizeof k->name, category_name[cat - 1]);
        k->learned = learned;
        k->kills = kills;
        k->next_kills = -1;
        if (learned < PC_UI_ARTS) {
            k->next_kills = vs_main_artsPointsRequirements[c][learned] - kills;
            if (k->next_kills < 0)
                k->next_kills = 0;
        }
        for (i = 0; i < PC_UI_ARTS; ++i) {
            pc_ui_art* a = &k->arts[i];
            int id = FIRST_ART + (cat - 1) * PC_UI_ARTS + i;
            a->id = id;
            a->cost = vs_main_actions[id].cost;
            if (vs_main_actions[id].unlocked) {
                any = 1;
                a->state = PC_UI_ART_LEARNED;
                action_name(id, a->name, sizeof a->name);
                copy_text(a->help, sizeof a->help, art_help[cat - 1][i]);
                art_reason(cat, id, a->reason, sizeof a->reason);
                if (!a->reason[0] && !resident) /* MAINMENU still loading: refused until it is */
                    copy_text(a->reason, sizeof a->reason, "Loading\xE2\x80\xA6");
                a->usable = a->reason[0] == 0;
            } else {
                a->kills_to_go = vs_main_artsPointsRequirements[c][i] - kills;
                if (a->kills_to_go < 0)
                    a->kills_to_go = 0;
                a->state = a->kills_to_go ? PC_UI_ART_LOCKED : PC_UI_ART_PENDING;
            }
        }
        copy_text(k->help, sizeof k->help, category_help[cat - 1][any ? 0 : 1]);
    }
}

static void fill(void)
{
    vs_battle_actor2* a = ashley();
    int unlocked;

    vs_main_bzero(&snap, sizeof snap);
    snap.next_ability_kills = -1;
    if (a) {
        decode_text();
        snap.valid = 1;
        snap.risk = a->risk;
        snap.hp = a->currentHP;
        snap.hp_max = a->maxHP;
        snap.equipped_category = vs_battle_characterState->equippedWeaponCategory;
        snap.weapon_drawn = vs_battle_characterState->weaponDrawn & 1;
        fill_list(PC_UI_ABIL_CHAIN);
        fill_list(PC_UI_ABIL_DEFENSE);
        unlocked = vs_main_artsStatus.kills.battleAbilitiesUnlocked;
        if (unlocked < ALL_ABILITIES) {
            snap.next_ability_kills = vs_main_battleAbilitiesPointsRequirements[unlocked] - vs_main_artsStatus.kills.total;
            if (snap.next_ability_kills < 0)
                snap.next_ability_kills = 0;
        }
        fill_arts();
    }
    pc_ui_shell_set_abilities(&snap);
    filled = 1;
}

/* The tick starts (menu_bridge.c, also when a session opens): no cast yet,
 * MAINMENU's residency for this tick, and a fresh snapshot while the
 * Abilities screen shows (nothing reads it otherwise). */
void pc_ui_abilities_fill(int mainmenu)
{
    cast_id = 0;
    resident = mainmenu;
    filled = 0;
    if (pc_ui_shell_screen() == PC_UI_SCREEN_ABILITIES)
        fill();
}

/* After the shell's tick and its requests: the screen it shows now (a tab
 * switched to it on this tick) gets its snapshot before it draws. */
void pc_ui_abilities_refresh(void)
{
    if (!filled && pc_ui_shell_screen() == PC_UI_SCREEN_ABILITIES)
        fill();
}

/* MENU2 _mapAbility (its abilityId already the action id). */
static void map_ability(int type, int button, int id)
{
    char* buttons = mapped(type);
    int i;
    if ((u_char)buttons[button] == id) {
        vs_battle_playMenuLeaveSfx();
        buttons[button] = 0;
        return;
    }
    vs_main_playSfxDefault(0x7E, VS_SFX_SET);
    for (i = 0; i < PC_UI_ABIL_BUTTONS; ++i)
        if ((u_char)buttons[i] == id)
            buttons[i] = 0;
    buttons[button] = id;
}

/* MENU1's checks for a learned art (its row is selectable). */
static int art_castable(int id)
{
    int category;
    if (id < FIRST_ART || id >= FIRST_ART + PC_UI_ART_CATEGORIES * PC_UI_ARTS || !ashley())
        return 0;
    category = (id - FIRST_ART) / PC_UI_ARTS + 1;
    return vs_main_actions[id].unlocked && category == vs_battle_characterState->equippedWeaponCategory
        && vs_battle_getActionFlags(0, id) == 0 && !((u_int)D_800F4EA0 & ART_MASK);
}

/* The row of a learned art in MENU1's list of its category (the learned arts
 * in order; at most 4 rows and View Arts: one page). */
static int art_row(int id)
{
    int first = FIRST_ART + (id - FIRST_ART) / PC_UI_ARTS * PC_UI_ARTS, i, row = 0;
    for (i = first; i < id; ++i)
        if (vs_main_actions[i].unlocked)
            ++row;
    return row;
}

/* Returns 1 when the request was one of ours. */
int pc_ui_abilities_request(const pc_ui_request* r)
{
    if (r->kind != PC_UI_REQ_ABIL_MAP && r->kind != PC_UI_REQ_ART_CAST)
        return 0;
    fill(); /* the snapshot as the game is now, whichever screen shows */
    if (r->kind == PC_UI_REQ_ABIL_MAP) {
        int type = r->shield;
        if ((type != PC_UI_ABIL_CHAIN && type != PC_UI_ABIL_DEFENSE) || r->slot < 0 || r->slot >= PC_UI_ABIL_BUTTONS
            || r->item <= 0 || learned_index(type, r->item) < 0) {
            vs_battle_playInvalidSfx();
            return 1;
        }
        map_ability(type, r->slot, r->item);
        fill(); /* the shell sees the result on this tick already */
        return 1;
    }
    /* ART_CAST: the shell still up (it returned no result this tick), MAINMENU
     * resident (the close below skips its load and the background's release,
     * which the menu's first frames still do), no model reload of the
     * Equipment screen in flight, one hand-off per tick (Magick's too) */
    if (cast_id || pc_ui_magick_cast_taken() || !pc_ui_shell_visible() || !resident || pc_ui_equip_busy()
        || !art_castable(r->item)) {
        vs_battle_playInvalidSfx();
        return 1;
    }
    cast_id = r->item; /* handed over by pc_ui_abilities_cast once the tick's result is known */
    return 1;
}

/* An art was accepted on this tick (menu_bridge.c, after the requests). */
int pc_ui_abilities_cast_taken(void) { return cast_id != 0; }

/* menu_bridge.c, the accepted art: go (the shell returned no result, the
 * menu closes for the cast) writes MENU1's hand-off, otherwise the art is
 * dropped and nothing of the game's state was touched.
 *
 * MENU1 vs_menu1_exec: artsMenu (executeAbility, executeAbilityType 5), then
 * executeArt (unk2 6, returnState = currentMenu 2 | 0x40); the list's
 * confirm keeps the row in its cursor memory (MAINMENU 58EC.c, submenu id
 * category + 11, row and page 0), which the original list reads when it
 * opens later with cursor memory on; the top level's cursor is on Break
 * Arts (command 1) as when MENU1 was opened from it. The game state is
 * MENU1's hand-off byte for byte; only the target cancel differs (the shell
 * comes back, menu_bridge.c pc_ui_menu_cast_cancelled). */
void pc_ui_abilities_cast(int go)
{
    int id = cast_id, category;
    cast_id = 0;
    if (!go || !id)
        return;
    category = (id - FIRST_ART) / PC_UI_ARTS + 1;
    D_800F4E98.executeAbility.s32 = id;
    vs_battle_menuState.executeAbilityType = 5;
    D_800F4E98.unk2 = 6;
    vs_battle_menuState.returnState = TOP_LEVEL_BREAK_ARTS;
    D_800F4EE8.cursorMemories[(category + 11) * 2] = art_row(id);
    D_800F4EE8.cursorMemories[(category + 11) * 2 + 1] = 0;
    vs_battle_submenuStates[0] = BREAK_ARTS_COMMAND;
}
