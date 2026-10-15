/* Modern menus, Magick tab: the game side (battle builds). menu_bridge.c
 * calls pc_ui_magick_fill at the start of every tick of the shell's top level
 * (and when a session opens) and pc_ui_magick_refresh after the shell's tick:
 * the snapshot is filled only while the Magick screen shows, and before every
 * request. equip_bridge.c's request loop hands the Magick requests to
 * pc_ui_magick_request (after abilities_bridge.c declines them);
 * menu_bridge.c then asks pc_ui_magick_cast_taken whether a spell or a
 * teleport was accepted and hands it to the game with pc_ui_magick_cast.
 *
 * What the snapshot (ui_model.h pc_ui_magick) holds and where it comes from
 * (MENU0.PRG 84.c / 15AC.c):
 *   schools        unlocked = vs_battle_isSpellClassUnlocked(school) (MENU0's
 *                  row is greyed otherwise); name ours, description MENU0's
 *                  magic string <school>Desc, or <school>Locked
 *   spells         BATTLE vs_battle_<school>SpellIds (18 / 6 / 18 / 8), those
 *                  with vs_main_actions[id].unlocked, in that order (MENU0
 *                  _<school>MagicMenu); name: Warlock MENU0's warlockSpells +
 *                  i, the others the action's; description MENU0's
 *                  <school>SpellDescs + i; MP = the action's cost (_setMPCost)
 *   levels         the seven levelled Warlock spells (_warlockMagicMenu
 *                  levelledSpells): with base + 1 learned the original opens
 *                  its LV picker; LV1..LV4 = base + 0..3, as many as learned
 *                  in a row (2, 3 with base + 2, 4 with base + 3 too)
 *   usable         the row: vs_battle_getActionFlags(0, id) == 0 (1 busy, 2 MP
 *                  too low, 4 not learned / silenced) and the menu's disable
 *                  mask D_800F4EA0 & 0xB7 clear; a level after LV1 also its
 *                  own getActionFlags == 0 (the picker's Circle)
 *   teleport       the row exists with vs_main_stateFlags.unkB5 != 0; it is
 *                  selectable when a savePointStates[0..47] entry is set,
 *                  unkB5 != 2 (the spirit paling) and func_8008A4FC() != 0
 *                  (standing on a Circle of Magic); destinations = the save
 *                  points with a state, names and descriptions MENU0's
 *                  teleportation locations[i * 2] / [i * 2 + 1] (state 2: the
 *                  disabled line), cost _getTeleportCost (15 + 4 per save
 *                  point between this room's and the target, MENU0's tables,
 *                  copied here), current = cost 15 (the cursor's start);
 *                  usable: state != 2, MP >= cost, the mask clear
 *   level_memory   cursorMemories[12], the LV picker's level: the shell
 *                  seeds its one level from it at an open, and while a
 *                  levelled spell is focused pc_ui_magick_refresh writes
 *                  the shell's level back (clamped to the spell's levels, as
 *                  levelledSpellInit does; Left/Right as levelledSpell does)
 *
 * Requests (the snapshot's usable flags are MENU0's checks; MAINMENU must be
 * resident, as for a break art: the snapshot says "Loading..." meanwhile):
 *   SPELL_CAST {item = action id}  Accepted, pc_ui_magick_cast(1) writes
 *              MENU0's hand-off (vs_menu0_exec <school>Menu + executeMagic):
 *              D_800F4E98.executeAbility.s32 = id, executeAbilityType 6 / 8
 *              / 10 / 12 (Warlock .. Enchanter: MENU0's init<School>Menu
 *              state, where a cancelled target would return), unk2 = 7,
 *              returnState = MENU0's currentMenu from the top level (1 |
 *              0x40); the cursor memories MENU0's lists leave (the school
 *              row, the spell row, the LV picker's level and count) and the
 *              top level's cursor on Magick. menu_bridge.c ends the menu as
 *              MENU0 does (close_for_cast: its exit sets currentMenu 1,
 *              func_800FAA5C makes it 0x7F) and BATTLE func_800735F8 runs the
 *              spell (unk2 7: func_80072EC4, targeting). A cancelled target
 *              reopens the shell on the same spell (pc_ui_menu_cast_cancelled).
 *   TELEPORT {slot = save point}  Accepted: D_800F4E98.unk2 = 0xA and
 *              executeAbility.s32 = MENU0's packing (room = save point data &
 *              0x1FF, bits 16-20 at 9, bits 24-28 at 14, the cost at 19;
 *              _teleportMenu returns it + 1, vs_menu0_exec stores it - 1),
 *              the school list's cursor memory (row 4; the destinations
 *              list's _teleportMenu rewrites at every open); the menu ends
 *              the same way and
 *              func_800735F8 teleports (unk2 10). No target: nothing to
 *              cancel back to.
 * vs_battle_submenuStates[1] (MENU0's own state) is left alone: MENU0 does
 * not run. Only BATTLE and main routines and data are used; the text tables
 * are static copies of MENU0's generated string assets. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "build/assets/MENU/MENU0.PRG/magic.h"
#include "build/assets/MENU/MENU0.PRG/teleportation.h"
#include "ui_shell.h"

/* ui_text.h (stdio in a game-header unit is avoided) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);

void pc_ui_magick_fill(int mainmenu);
void pc_ui_magick_refresh(void);
int pc_ui_magick_request(const pc_ui_request* r);
int pc_ui_magick_cast_taken(void);
void pc_ui_magick_cast(int go);
int pc_ui_abilities_cast_taken(void); /* abilities_bridge.c: one hand-off per tick */
int pc_ui_items_cast_taken(void);     /* items_bridge.c */
int pc_ui_equip_busy(void);           /* equip_bridge.c: a weapon / shield model reload runs */

/* MENU0.PRG magic and teleportation, under our own names. */
static u_short magic_text[] = {
#include "build/assets/MENU/MENU0.PRG/magic.vsString"
};
static u_short teleport_text[] = {
#include "build/assets/MENU/MENU0.PRG/teleportation.vsString"
};

/* MENU0 84.c _savePointData and _getTeleportCost's savePointCosts. */
static const int save_point_data[PC_UI_SAVE_POINTS] = { 0x0305001B, 0x0801000E, 0x04030199, 0x0C0C001C,
    0x070C0028, 0x040200CD, 0x0F070035, 0x08060093, 0x060000CE, 0x09080106, 0x09080111, 0x0C030098, 0x090800E0,
    0x060900C8, 0x050800CA, 0x050F00CC, 0x0109009B, 0x0A07007C, 0x000400CF, 0x080800EC, 0x020400F1, 0x0705011D,
    0x06080120, 0x04080139, 0x04080147, 0x0904014F, 0x010300A2, 0x150C00A3, 0x030200D0, 0x060600D1, 0x1007008B,
    0x010400AB, 0x05020055, 0x07060063, 0x0A0400D2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const char save_point_costs[PC_UI_SAVE_POINTS] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0 };

#define MAGIC_MASK 0xB7      /* D_800F4EA0 bits that grey every spell and destination (MENU0) */
#define TOP_LEVEL_MAGIC (1 | 0x40) /* currentMenu while MENU0 runs from the top level */
#define MAGIC_COMMAND 0      /* the top level's row (vs_battle_submenuStates[0]) */
#define TELEPORT_ROW 4       /* MENU0's school list: the four schools, then Teleport */
#define SCHOOL_LIST 5        /* MENU0's cursor memory ids: the school list (rows from 1) */
#define SPELL_LIST 7         /* Warlock's list; Shaman 8, Sorcerer 9, Enchanter 10 (rows from 2) */
#define LEVEL_MEMORY 12      /* the LV picker: [12] the level, [13] how many */
#define CURRENT_COST 15      /* _getTeleportCost of the circle the player stands on */
#define PALING 2             /* savePointStates: found, the spirit paling blocks it */

static const char* const school_names[PC_UI_SCHOOLS] = { "Warlock", "Shaman", "Sorcerer", "Enchanter" };
static const int school_sizes[PC_UI_SCHOOLS] = { 18, 6, 18, 8 };
static const int school_descs[PC_UI_SCHOOLS] = { VS_magic_INDEX_warlockSpellDescs, VS_magic_INDEX_shamanSpellDescs,
    VS_magic_INDEX_sorcererSpellDescs, VS_magic_INDEX_enchanterSpellDescs };
/* _warlockMagicMenu levelledSpells */
static const u_char levelled[7] = { vs_battle_spell_explosion, vs_battle_spell_thunderburst,
    vs_battle_spell_flameSphere, vs_battle_spell_gaeaStrike, vs_battle_spell_avalanche, vs_battle_spell_radialSurge,
    vs_battle_spell_meteor };

static pc_ui_magick snap;
static struct {
    int kind;   /* 0 none, 1 spell, 2 teleport (handed over by pc_ui_magick_cast) */
    int id;     /* spell: action id; teleport: the packed executeAbility */
    int school; /* spell: 0..3 */
    int row;    /* spell: its LV1 row in MENU0's list */
    int rows;   /* that list's rows */
    int level;  /* spell: the LV picker's level 0..3, -1 no picker */
    int levels;
} cast;
static int resident; /* MAINMENU.PRG drives the top level (menu_bridge.c mainmenu_resident) */
static int filled;   /* the snapshot was filled on this tick */
static int text_decoded;
static char school_help[PC_UI_SCHOOLS][2][160]; /* the description, the "not learned yet" line */
static char spell_help[PC_UI_SCHOOLS][PC_UI_SCHOOL_SPELLS][200];
static char warlock_name[PC_UI_SCHOOL_SPELLS][32];
static char teleport_desc[96], paling[96];
static char point_name[PC_UI_SAVE_POINTS][32], point_help[PC_UI_SAVE_POINTS][96];

static const u_char* table_string(const u_short* table, int i) { return (const u_char*)&table[table[i]]; }

static void decode_text(void)
{
    int s, i;
    if (text_decoded)
        return;
    for (s = 0; s < PC_UI_SCHOOLS; ++s) {
        pc_ui_text_decode(table_string(magic_text, s * 3 + VS_magic_INDEX_warlockDesc), 512, school_help[s][0],
            sizeof school_help[s][0]);
        pc_ui_text_decode(table_string(magic_text, s * 3 + VS_magic_INDEX_warlockLocked), 512, school_help[s][1],
            sizeof school_help[s][1]);
        for (i = 0; i < school_sizes[s]; ++i)
            pc_ui_text_decode(table_string(magic_text, school_descs[s] + i), 512, spell_help[s][i],
                sizeof spell_help[s][i]);
    }
    for (i = 0; i < school_sizes[0]; ++i)
        pc_ui_text_decode(table_string(magic_text, VS_magic_INDEX_warlockSpells + i), 128, warlock_name[i],
            sizeof warlock_name[i]);
    pc_ui_text_decode((const u_char*)(teleport_text + VS_teleportation_OFFSET_teleportationDesc), 512, teleport_desc,
        sizeof teleport_desc);
    pc_ui_text_decode((const u_char*)(teleport_text + VS_teleportation_OFFSET_teleportationDisabled), 512, paling,
        sizeof paling);
    for (i = 0; i < PC_UI_SAVE_POINTS; ++i) {
        pc_ui_text_decode(table_string(teleport_text, i * 2 + VS_teleportation_INDEX_locations), 128, point_name[i],
            sizeof point_name[i]);
        pc_ui_text_decode(table_string(teleport_text, i * 2 + VS_teleportation_INDEX_locations + 1), 512,
            point_help[i], sizeof point_help[i]);
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

static const char* spell_table(int school)
{
    switch (school) {
    case PC_UI_SCHOOL_WARLOCK: return vs_battle_warlockSpellIds;
    case PC_UI_SCHOOL_SHAMAN: return vs_battle_shamanSpellIds;
    case PC_UI_SCHOOL_SORCERER: return vs_battle_sorcererSpellIds;
    default: return vs_battle_enchanterSpellIds;
    }
}

/* How many levels MENU0's LV picker offers for a learned spell (1: no picker). */
static int spell_levels(int id)
{
    int i;
    for (i = 0; i < 7; ++i)
        if (id == levelled[i]) {
            if (!vs_main_actions[id + 1].unlocked)
                return 1;
            if (!vs_main_actions[id + 2].unlocked)
                return 2;
            return vs_main_actions[id + 3].unlocked ? 4 : 3;
        }
    return 1;
}

/* Why the menu's disable mask greys everything ("" when clear): the lowest
 * bit MENU0 cares about, in the game's message order (as the shell's rows). */
static int mask_reason(char* out, int size)
{
    static const char* const by_bit[8] = {
        0,
        "Not while carrying a cube.",
        "Not while hanging from a ledge.",
        0,
        0,
        "Not enough MP.",
        0,
        "Silenced: spells cannot be cast.",
    };
    u_int mask = (u_int)D_800F4EA0 & MAGIC_MASK;
    int b;
    out[0] = 0;
    if (!mask)
        return 0;
    for (b = 0; b < 8; ++b)
        if (((mask >> b) & 1) && by_bit[b]) {
            copy_text(out, size, by_bit[b]);
            return 1;
        }
    copy_text(out, size, "Not usable right now.");
    return 1;
}

/* Why spell `id` cannot be cast now ("" when it can): the mask first, then
 * the action's own flags (vs_battle_getActionFlags), then MAINMENU loading. */
static void spell_reason(int id, char* out, int size)
{
    int flags;
    if (mask_reason(out, size))
        return;
    flags = vs_battle_getActionFlags(0, id);
    if (flags & 2)
        copy_text(out, size, "Not enough MP.");
    else if ((flags & 4) && (ashley()->statuses & 0x1000))
        copy_text(out, size, "Silenced: spells cannot be cast.");
    else if (flags & 4)
        copy_text(out, size, "Not learned yet.");
    else if (flags)
        copy_text(out, size, "Not right now.");
    else if (!resident) /* MAINMENU still loading: refused until it is */
        copy_text(out, size, "Loading\xE2\x80\xA6");
}

static void action_name(int id, char* out, int size)
{
    pc_ui_text_decode((const u_char*)vs_main_actions[id].name, sizeof vs_main_actions[id].name, out, size);
}

static void fill_school(int s)
{
    pc_ui_school* k = &snap.schools[s];
    const char* table = spell_table(s);
    int i, l;
    copy_text(k->name, sizeof k->name, school_names[s]);
    k->unlocked = vs_battle_isSpellClassUnlocked(s) != 0;
    copy_text(k->help, sizeof k->help, school_help[s][k->unlocked ? 0 : 1]);
    for (i = 0; i < school_sizes[s]; ++i) {
        int id = (u_char)table[i];
        pc_ui_spell* e;
        if (!vs_main_actions[id].unlocked)
            continue;
        e = &k->spells[k->count++];
        e->id = id;
        if (s == PC_UI_SCHOOL_WARLOCK)
            copy_text(e->name, sizeof e->name, warlock_name[i]);
        else
            action_name(id, e->name, sizeof e->name);
        copy_text(e->help, sizeof e->help, spell_help[s][i]);
        e->mp = vs_main_actions[id].cost;
        e->levels = spell_levels(id);
        for (l = 0; l < e->levels; ++l) {
            pc_ui_spell_level* v = &e->level[l];
            v->id = id + l;
            v->mp = vs_main_actions[id + l].cost;
            /* a level after LV1: the row must be selectable to reach the picker */
            if (l > 0 && !e->level[0].usable)
                copy_text(v->reason, sizeof v->reason, e->level[0].reason);
            else
                spell_reason(id + l, v->reason, sizeof v->reason);
            v->usable = v->reason[0] == 0;
        }
    }
}

/* MENU0 _getTeleportCost: 15, plus the save points between this room's
 * (48 when this room has none) and the target. */
static int teleport_cost(int target)
{
    int room = vs_battle_getCurrentRoomId(), here, i, j, cost = 15;
    for (i = 0; i < PC_UI_SAVE_POINTS; ++i)
        if ((save_point_data[i] & 0xFFFF) == room)
            break;
    here = i;
    if (target < i) {
        i = target;
        target = here;
    }
    for (j = i; j < target; ++j)
        cost += save_point_costs[j];
    return cost;
}

static void fill_teleport(void)
{
    int any = 0, circle, i, mp = snap.mp;
    char row_reason[64];
    snap.teleport = vs_main_stateFlags.unkB5 != 0;
    if (!snap.teleport)
        return;
    for (i = 0; i < PC_UI_SAVE_POINTS; ++i)
        if (vs_main_stateFlags.savePointStates[i])
            any = 1;
    circle = func_8008A4FC() != 0;
    snap.teleport_enabled = any && vs_main_stateFlags.unkB5 != 2 && circle;
    copy_text(snap.teleport_help, sizeof snap.teleport_help,
        circle && vs_main_stateFlags.unkB5 == 2 ? paling : teleport_desc);
    row_reason[0] = 0;
    if (!any)
        copy_text(row_reason, sizeof row_reason, "No Circle of Magic found yet.");
    else if (vs_main_stateFlags.unkB5 == 2)
        copy_text(row_reason, sizeof row_reason, "The spirit paling prevents teleportation.");
    else if (!circle)
        copy_text(row_reason, sizeof row_reason, "Stand on a Circle of Magic to teleport.");
    copy_text(snap.teleport_reason, sizeof snap.teleport_reason, row_reason);
    for (i = 0; i < PC_UI_SAVE_POINTS; ++i) {
        int state = vs_main_stateFlags.savePointStates[i];
        pc_ui_destination* d;
        if (!state)
            continue;
        d = &snap.destinations[snap.destination_count++];
        d->point = i;
        copy_text(d->name, sizeof d->name, point_name[i]);
        copy_text(d->help, sizeof d->help, state == PALING ? paling : point_help[i]);
        d->cost = teleport_cost(i);
        d->current = d->cost == CURRENT_COST;
        if (row_reason[0])
            copy_text(d->reason, sizeof d->reason, row_reason);
        else if (state == PALING)
            copy_text(d->reason, sizeof d->reason, "The spirit paling prevents teleportation.");
        else if (mp < d->cost)
            copy_text(d->reason, sizeof d->reason, "Not enough MP.");
        else if (!mask_reason(d->reason, sizeof d->reason) && !resident)
            copy_text(d->reason, sizeof d->reason, "Loading\xE2\x80\xA6");
        d->usable = d->reason[0] == 0;
    }
}

static void fill(void)
{
    vs_battle_actor2* a = ashley();
    int s;

    vs_main_bzero(&snap, sizeof snap);
    if (a) {
        decode_text();
        snap.valid = 1;
        snap.mp = a->currentMP;
        snap.mp_max = a->maxMP;
        snap.mask = (u_int)D_800F4EA0 & MAGIC_MASK;
        snap.level_memory = D_800F4EE8.cursorMemories[LEVEL_MEMORY];
        for (s = 0; s < PC_UI_SCHOOLS; ++s)
            fill_school(s);
        fill_teleport();
    }
    pc_ui_shell_set_magick(&snap);
    filled = 1;
}

/* The tick starts (menu_bridge.c, also when a session opens): no hand-off
 * yet, MAINMENU's residency for this tick, and a fresh snapshot while the
 * Magick screen shows (nothing reads it otherwise). */
void pc_ui_magick_fill(int mainmenu)
{
    cast.kind = 0;
    resident = mainmenu;
    filled = 0;
    if (pc_ui_shell_screen() == PC_UI_SCREEN_MAGICK)
        fill();
}

/* After the shell's tick and its requests: a tab switched to Magick on this
 * tick gets its snapshot before it draws. */
void pc_ui_magick_refresh(void)
{
    pc_ui_magick_view v;
    if (pc_ui_shell_screen() != PC_UI_SCREEN_MAGICK)
        return;
    if (!filled)
        fill();
    /* the LV picker's level is the game's: a focused levelled spell shows
     * it clamped to its levels and writes that back (_warlockMagicMenu
     * levelledSpellInit), Left/Right move it (levelledSpell) */
    if (!snap.valid || !pc_ui_shell_visible())
        return;
    pc_ui_shell_magick_view(&v);
    if (v.kind >= 0 && v.kind < PC_UI_SCHOOLS && v.cursor < snap.schools[v.kind].count
        && snap.schools[v.kind].spells[v.cursor].levels > 1)
        D_800F4EE8.cursorMemories[LEVEL_MEMORY] = v.level;
}

/* Row `row` of `rows` in one of MENU0's lists (vs_mainmenu_setMenuRows,
 * `info` rows of titles above: 10 - info rows on screen) as its cursor memory
 * keeps it: the row on screen and the scroll. The list scrolls a row before
 * its edges; the scroll a cursor walked down from the top would leave. */
static void list_memory(int id, int row, int rows, int info)
{
    int shown = 10 - info, page = 0;
    if (rows > shown) {
        page = row - (shown - 2);
        if (page < 0)
            page = 0;
        if (page > rows - shown)
            page = rows - shown;
    }
    D_800F4EE8.cursorMemories[id * 2] = row - page;
    D_800F4EE8.cursorMemories[id * 2 + 1] = page;
}

/* MENU0 _teleportMenu's return (+ 1, which vs_menu0_exec takes back). */
static int teleport_packed(int point, int cost)
{
    int data = save_point_data[point];
    return (data & 0x1FF) | ((data >> 16) & 0x1F) << 9 | ((data >> 24) & 0x1F) << 14 | cost << 19;
}

/* Returns 1 when the request was one of ours. */
int pc_ui_magick_request(const pc_ui_request* r)
{
    int s, i, l;
    if (r->kind != PC_UI_REQ_SPELL_CAST && r->kind != PC_UI_REQ_TELEPORT)
        return 0;
    fill(); /* the snapshot as the game is now, whichever screen shows */
    /* the shell still up (it returned no result this tick), MAINMENU resident
     * (in the usable flags), no model reload of the Equipment screen in
     * flight, one hand-off per tick */
    if (cast.kind || pc_ui_abilities_cast_taken() || pc_ui_items_cast_taken() || !pc_ui_shell_visible() || pc_ui_equip_busy() || !snap.valid) {
        vs_battle_playInvalidSfx();
        return 1;
    }
    if (r->kind == PC_UI_REQ_SPELL_CAST) {
        for (s = 0; s < PC_UI_SCHOOLS; ++s)
            for (i = 0; i < snap.schools[s].count; ++i) {
                const pc_ui_spell* e = &snap.schools[s].spells[i];
                for (l = 0; l < e->levels; ++l)
                    if (e->level[l].id == r->item) {
                        if (!e->level[l].usable)
                            goto refused;
                        cast.kind = 1;
                        cast.id = r->item;
                        cast.school = s;
                        cast.row = i;
                        cast.rows = snap.schools[s].count;
                        cast.level = e->levels > 1 ? l : -1;
                        cast.levels = e->levels;
                        return 1;
                    }
            }
    } else {
        for (i = 0; i < snap.destination_count; ++i) {
            const pc_ui_destination* d = &snap.destinations[i];
            if (d->point != r->slot)
                continue;
            if (!snap.teleport_enabled || !d->usable)
                goto refused;
            cast.kind = 2;
            cast.id = teleport_packed(d->point, d->cost);
            return 1;
        }
    }
refused:
    vs_battle_playInvalidSfx();
    return 1;
}

/* A hand-off was accepted on this tick (menu_bridge.c, after the requests):
 * 0 none, 1 a spell (a target follows, which may be cancelled), 2 a teleport. */
int pc_ui_magick_cast_taken(void) { return cast.kind; }

/* menu_bridge.c, the accepted hand-off: go (the shell returned no result, the
 * menu closes for it) writes MENU0's, otherwise it is dropped and nothing of
 * the game's state was touched. The game state is MENU0's hand-off byte for
 * byte (the cursor memories as its lists leave them when the same row is
 * confirmed); only a cancelled target differs (the shell comes back). */
void pc_ui_magick_cast(int go)
{
    int kind = cast.kind;
    cast.kind = 0;
    if (!go || !kind)
        return;
    vs_battle_submenuStates[0] = MAGIC_COMMAND;
    if (kind == 2) {
        /* _teleportMenu: the school list's Teleport row; vs_menu0_exec
         * teleportMenu: unk2 0xA, exit (currentMenu 1) */
        list_memory(SCHOOL_LIST, TELEPORT_ROW, TELEPORT_ROW + 1, 1);
        /* the destinations list's memory ([22]/[23]) is not written:
         * _teleportMenu overwrites it on every open (the cursor starts on
         * the nearest circle), so nothing ever reads what a confirm leaves */
        D_800F4E98.unk2 = 0xA;
        D_800F4E98.executeAbility.s32 = cast.id;
        return;
    }
    list_memory(SCHOOL_LIST, cast.school, TELEPORT_ROW + (vs_main_stateFlags.unkB5 != 0), 1);
    list_memory(SPELL_LIST + cast.school, cast.row, cast.rows, 2);
    if (cast.level >= 0) { /* _warlockMagicMenu levelledSpellInit / levelledSpell */
        D_800F4EE8.cursorMemories[LEVEL_MEMORY] = cast.level;
        D_800F4EE8.cursorMemories[LEVEL_MEMORY + 1] = cast.levels;
    }
    /* <school>Menu, then executeMagic */
    D_800F4E98.executeAbility.s32 = cast.id;
    vs_battle_menuState.executeAbilityType = 6 + cast.school * 2;
    D_800F4E98.unk2 = 7;
    vs_battle_menuState.returnState = TOP_LEVEL_MAGIC;
}
