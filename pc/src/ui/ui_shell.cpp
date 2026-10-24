// Modern menu shell: tabs, rows, focus and the game's result protocol
// (pc/docs/Modern_Menus.md). Pure state; drawing is ui_draw.cpp.
#include "ui/ui_shell.h"
#include "ui/map_frame.h"
#include "ui/data_frame.h"
#include "ui/container_frame.h"
#include "ui/ui_manual.h"
#include "ui/ui_score.h"
#include "ui/ui_settings.h"
#include "display_confirm.h"
#include "ui/ui_theme.h"
#include "ui/ui_workshop.h"
#include "quit_options.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

extern "C" const char* pc_settings_status(void);
// Quit to title / Quit to desktop (quit_options.h): weak, the probes run without it.
extern "C" int pc_quit_pending(void) __attribute__((weak));
extern "C" int pc_quit_ask(int) __attribute__((weak));
extern "C" void pc_quit_answer(int) __attribute__((weak));
extern "C" void pc_quit_cancel(void) __attribute__((weak));

namespace {

enum RowKind { ROW_COMMAND };
struct RowDef {
    const char* id;
    const char* label;
    const char* help;
    RowKind kind;
    int command; // ROW_COMMAND: the original command index
};
enum Screen { SCREEN_COMMANDS = PC_UI_SCREEN_COMMANDS, SCREEN_EQUIPMENT = PC_UI_SCREEN_EQUIPMENT,
    SCREEN_ASHLEY = PC_UI_SCREEN_ASHLEY, SCREEN_SYSTEM = PC_UI_SCREEN_SYSTEM, SCREEN_ABILITIES = PC_UI_SCREEN_ABILITIES,
    SCREEN_MAGICK = PC_UI_SCREEN_MAGICK, SCREEN_ITEMS = PC_UI_SCREEN_ITEMS };
struct TabDef {
    const char* id;
    const char* title;
    RowDef rows[6];
    int count;
    Screen screen = SCREEN_COMMANDS; // SCREEN_COMMANDS: the rows; else a native screen with its own state
    int command = -1; // native screen: the original command that gates it (menuFlags, unselectable); -1 none
};

const TabDef TABS[] = {
    // native summary; Square opens the original Status (command 4, which gates the tab)
    {"ashley", "Ashley", {}, 0, SCREEN_ASHLEY, 4},
    // native screen; the original equip page stays reachable through Items (same command)
    {"equipment", "Equipment", {}, 0, SCREEN_EQUIPMENT, 3},
    // native screen: the bag by category (command 3, which gates the tab, as for Equipment); the original Items
    // menu stays reachable through the action menu's "Original screen"
    {"items", "Items", {}, 0, SCREEN_ITEMS, 3},
    // native screen: sub-tabs Chain and Defense (command 2, Battle Abilities) and Break Arts (command 1),
    // each shown once the game has unlocked its command (menuFlags); Square opens the original screen
    {"abilities", "Abilities", {}, 0, SCREEN_ABILITIES, -1},
    // native screen: sub-tabs per school learned and Teleport; Square opens the original Magick (command 0,
    // which gates the tab)
    {"magick", "Magick", {}, 0, SCREEN_MAGICK, 0},
    {"lea_monde", "Le\xc3\xa1 Monde", {{"map", "Map", "Where you are and where you have been.", ROW_COMMAND, 5}}, 1},
    // native screen: the game's own System screens and the settings pages (ui_settings.cpp)
    {"system", "System", {}, 0, SCREEN_SYSTEM, -1},
};
const int TAB_COUNT = sizeof TABS / sizeof TABS[0];

// Equipment screen: sub-tabs Gear and Quick swap, each with a pick list.
enum EquipMode { M_GEAR = PC_UI_EQ_GEAR, M_PICK = PC_UI_EQ_PICK, M_QUICK = PC_UI_EQ_QUICK,
    M_QUICK_WEAPON = PC_UI_EQ_QUICK_WEAPON, M_QUICK_SHIELD = PC_UI_EQ_QUICK_SHIELD };
const char* const MODE_IDS[] = {"gear", "pick", "quick", "quick_weapon", "quick_shield"};
const char* const SLOT_IDS[PC_UI_EQUIP_SLOTS] = {
    "weapon", "shield", "right_arm", "left_arm", "head", "body", "legs", "accessory"};
// quick-swap slots in weapon_slots.c order: 1-4 face buttons, 5-8 D-pad
const char* const QUICK_IDS[PC_UI_QUICK] = {"circle", "triangle", "square", "cross", "up", "down", "right", "left"};
struct Equip {
    int sub = 0;        // 0 Gear, 1 Quick swap
    int mode = M_GEAR;
    int slot = 0;       // focused gear slot (PC_UI_SLOT_*)
    int quick = 0;      // focused quick slot 0..7
    int cursor = -1;    // pick list row; -1 = not placed yet (the list has not arrived)
    int pending_weapon = -1; // quick: the weapon picked before the shield step
    int prefer = -2;    // where the cursor goes when the list arrives (inventory); -2 = what is in place
    bool has_pending = false; // quick shield step: pending_item is the weapon picked (for drawing)
    pc_ui_item pending_item = {};
};
// The pointer between two ticks (pc_ui_shell_mouse).
struct Mouse {
    int hover_kind = PC_UI_MOUSE_NONE, hover_index = 0;
    int click_kind = PC_UI_MOUSE_NONE, click_index = 0;
    float wheel = 0;
    // System: a value the pointer changed (pc_ui_shell_system_value)
    bool value = false, value_set = false, value_preview = false;
    int value_row = 0, value_delta = 0;
    double value_number = 0;
};

// Abilities screen: sub-tabs Chain / Defense / Break Arts (PC_UI_AB_*), modes PC_UI_ABM_*.
struct Abil {
    int kind = PC_UI_AB_CHAIN;            // the sub-tab (kept as its kind: the shown set changes)
    int mode = PC_UI_ABM_SLOTS;
    int slot[2] = {0, 0};                 // chain / defense: focused button 0..2
    int cursor[2] = {0, 0};               // chain / defense: list row
    int category = 0;                     // Break Arts: focused category 1..10, 0 = the equipped one when placed
    int art = 0;                          // focused art 0..3
};

// Magick screen: sub-tabs PC_UI_SCHOOL_* / PC_UI_MG_TELEPORT, one list each.
struct Magic {
    int kind = PC_UI_SCHOOL_WARLOCK;      // the sub-tab (kept as its kind: the shown set changes)
    int cursor[PC_UI_SCHOOLS + 1] = {};   // each list's row
    int level = 0;                        // the LV picker's level (one for every levelled spell)
    bool placed = false;                  // Teleport's cursor was put on the current circle since the open
    bool seeded = false;                  // level was taken from the game's (level_memory) since the open
};

// Items screen: the category, each category's row, the mode and its menus' state.
struct It {
    int category = PC_UI_IC_WEAPON;
    int cursor[PC_UI_ITEM_CATEGORIES] = {};
    int mode = PC_UI_IT_LIST;
    int action = 0;                       // the action menu's row
    int confirm_kind = PC_UI_ITA_DISCARD; // what the Yes / No confirms
    int confirm = 1;                      // 0 Yes, 1 No (it opens on No, as _discardMenu's Yes / No does)
    int count = 0, count_max = 0, step = 0; // the quantity picker
    int sort = 0;                         // the sort list's row
    int move_from = -1;                   // MOVE: the row being moved
    bool ws = false;                      // the Workshop sub-tab (after misc; ui_workshop.cpp) is the current one
};

const int QUEUE = 16;
struct State {
    bool visible = false;
    bool holding = false; // closed, a result held by the bridge: still drawn
    int tab = 0;
    int row[TAB_COUNT] = {};
    pc_ui_commands commands = {0, 0, 0};
    Equip eq;
    pc_ui_equipment model = {};
    pc_ui_request queue[QUEUE] = {};
    int q_head = 0, q_count = 0;
    Mouse mouse;
    pc_ui_ashley ash = {};
    int ash_row = 0;         // PC_UI_ASH_*
    int ash_limb = -1;       // on the Condition row: the body part focused, -1 the panel (pc_ui_shell_ashley_limb)
    struct {
        int item = 0;  // focused list entry
        int page = -1; // the entered settings page; -1: the list has the focus
        std::map<std::string, int> rows; // each page's focused row, by page id
        // The pages as last seen (pc_ui_settings_generation): a plugin's page coming
        // or going renumbers them; the focus stays on its page by id.
        unsigned generation = 0;
        std::string page_id, item_page_id;
        int quit_focus = 1;       // "Unsaved progress will be lost": 0 Yes, 1 No (it opens on No)
        bool quit_seen = false;   // the question is up (its focus starts on No each time)
        int confirm_focus = 0;    // "Keep these settings?": 0 Keep, 1 Revert
        bool confirm_seen = false; // the modal is up (its focus starts on Keep each time)
    } sys;
    Abil ab;
    pc_ui_abilities ab_model = {};
    Magic mg;
    pc_ui_magick mg_model = {};
    It it;
    pc_ui_items it_model = {};
    int ash_shown = -1;      // effect chips the drawing fits, -1 all
    float drag_x = 0, drag_y = 0; // model drag since the bridge last took it
} s;

void push_request(const pc_ui_request& r)
{
    if (s.q_count == QUEUE)
        return; // the bridge drains every tick; a full queue drops the newest
    s.queue[(s.q_head + s.q_count++) % QUEUE] = r;
}
void push(int kind, int slot, int item, int shield, int count = 0)
{
    pc_ui_request r = {};
    r.kind = kind, r.slot = slot, r.item = item, r.shield = shield, r.count = count;
    push_request(r);
}

bool row_shown(const RowDef& r)
{
    return r.kind != ROW_COMMAND || ((s.commands.enabled >> r.command) & 1);
}
bool row_enabled(const RowDef& r)
{
    return r.kind != ROW_COMMAND || !((s.commands.unselectable >> r.command) & 1);
}
// Why a greyed row is off now, in our own words. The game picks its message
// the same way (func_800C930C): the lowest bit of the disable mask
// (func_800CABE0) that the command cares about, one message per bit (BATTLE
// menuStrings 12 + bit: cube, hanging, battle mode, MP, HP, silence, paralysis).
const char* row_reason(const RowDef& r)
{
    if (row_enabled(r))
        return nullptr;
    static const char* const by_bit[9] = {
        nullptr,
        "Not while carrying a cube.",
        "Not while hanging from a ledge.",
        "Only in battle mode: draw your weapon first.",
        nullptr,
        "Not enough MP.",
        "Not enough HP.",
        "Silenced: spells cannot be cast.",
        "Paralysed: break arts cannot be used.",
    };
    unsigned care = r.command == 0 ? 0xB7u : r.command == 1 ? 0x15Fu : r.command == 3 ? 7u : 0u;
    unsigned bits = s.commands.mask & care;
    for (int b = 0; b < 9; ++b)
        if (((bits >> b) & 1) && by_bit[b])
            return by_bit[b];
    if (r.command == 5)
        return "The map is not available here.";
    return "Not usable right now: the menu inside shows why.";
}
// Shown rows of the current tab, as indices into TABS[tab].rows.
int shown_rows(int tab, int* out)
{
    int n = 0;
    for (int i = 0; i < TABS[tab].count; ++i)
        if (row_shown(TABS[tab].rows[i]))
            out[n++] = i;
    return n;
}
int ab_subs(int* out);
bool tab_has_rows(int tab)
{
    if (TABS[tab].screen == SCREEN_ABILITIES) {
        int subs[3];
        return ab_subs(subs) > 0;
    }
    if (TABS[tab].screen != SCREEN_COMMANDS)
        return TABS[tab].command < 0 || ((s.commands.enabled >> TABS[tab].command) & 1);
    int rows[6];
    return shown_rows(tab, rows) > 0;
}
// The next tab with rows from `tab` in direction `dir` (tab itself when all others are empty).
int step_tab(int tab, int dir)
{
    for (int i = 1; i <= TAB_COUNT; ++i) {
        int t = (tab + dir * i + TAB_COUNT * i) % TAB_COUNT;
        if (tab_has_rows(t))
            return t;
    }
    return tab;
}
int clamp_row(int tab)
{
    int rows[6];
    int n = shown_rows(tab, rows);
    if (s.row[tab] >= n)
        s.row[tab] = n ? n - 1 : 0;
    return n;
}
std::string escape(const char* text)
{
    std::string out;
    for (const char* p = text; *p; ++p) {
        if ((unsigned char)*p < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned char)*p);
            out += buf;
            continue;
        }
        if (*p == '"' || *p == '\\')
            out += '\\';
        out += *p;
    }
    return out;
}

// ---- Equipment ----
const RowDef EQUIP_GATE = {"equipment", "Equipment", "", ROW_COMMAND, 3};
bool equip_locked() { return !row_enabled(EQUIP_GATE); }
bool picking() { return s.eq.mode == M_PICK || s.eq.mode == M_QUICK_WEAPON || s.eq.mode == M_QUICK_SHIELD; }
int wanted_slot()
{
    switch (s.eq.mode) {
    case M_PICK: return s.eq.slot;
    case M_QUICK_WEAPON: return PC_UI_SLOT_WEAPON;
    case M_QUICK_SHIELD: return PC_UI_SLOT_SHIELD;
    default: return -1;
    }
}
// The open list has arrived (the bridge filled candidates for the slot asked for).
bool list_ready()
{
    int w = wanted_slot();
    return w >= 0 && s.model.valid && s.model.slot == w;
}
int cand_count()
{
    int n = s.model.count;
    return n < 0 ? 0 : n > PC_UI_CANDIDATES ? PC_UI_CANDIDATES : n;
}
// Rows of the open list: the candidates, and "No shield" first in the quick shield step.
int list_rows()
{
    if (!list_ready())
        return 0;
    return cand_count() + (s.eq.mode == M_QUICK_SHIELD ? 1 : 0);
}
// Row r of the open list: the candidate, or null for "No shield".
const pc_ui_item* list_item(int r)
{
    if (s.eq.mode == M_QUICK_SHIELD)
        return r == 0 ? nullptr : &s.model.candidates[r - 1];
    return &s.model.candidates[r];
}
int list_inventory(int r)
{
    const pc_ui_item* it = list_item(r);
    return it ? it->inventory : -1;
}
// Put the cursor on what is in place now once the list arrives; keep it in range.
void sync_cursor()
{
    int n = list_rows();
    if (!n) {
        s.eq.cursor = -1;
        return;
    }
    if (s.eq.cursor < 0) {
        int current = s.eq.prefer != -2 ? s.eq.prefer : s.eq.mode == M_PICK ? s.model.equipped[s.eq.slot].inventory
            : s.eq.mode == M_QUICK_WEAPON ? s.model.quick_weapon[s.eq.quick]
                                          : s.model.quick_shield[s.eq.quick];
        s.eq.cursor = 0;
        for (int r = 0; r < n; ++r)
            if (list_inventory(r) == current && (current >= 0 || s.eq.mode == M_QUICK_SHIELD)) {
                s.eq.cursor = r;
                break;
            }
    }
    if (s.eq.cursor >= n)
        s.eq.cursor = n - 1;
}
void open_list(int mode)
{
    s.eq.mode = mode;
    s.eq.cursor = -1;
    s.eq.prefer = -2;
    push(PC_UI_REQ_CANDIDATES, wanted_slot(), -1, -1);
    sync_cursor();
}
void equip_base() // out of any pick list, onto the sub-tab's own mode
{
    s.eq.mode = s.eq.sub ? M_QUICK : M_GEAR;
    s.eq.cursor = -1;
    s.eq.pending_weapon = -1;
    s.eq.has_pending = false;
}
// Cross in a pick list: one step back. Returns false at a sub-tab's top (the shell closes).
bool equip_back()
{
    switch (s.eq.mode) {
    case M_PICK:
    case M_QUICK_WEAPON:
        equip_base();
        return true;
    case M_QUICK_SHIELD: {
        int w = s.eq.pending_weapon;
        open_list(M_QUICK_WEAPON);
        s.eq.pending_weapon = -1;
        s.eq.has_pending = false;
        s.eq.prefer = w; // back on the weapon just picked once the list is there
        s.eq.cursor = -1;
        sync_cursor();
        return true;
    }
    default:
        return false;
    }
}
void equip_tick(const pc_ui_input* in)
{
    Equip& e = s.eq;
    if (in->pressed & (PC_UI_SUB_NEXT | PC_UI_SUB_PREV)) { // two sub-tabs: either way swaps
        e.sub = !e.sub;
        equip_base();
        return;
    }
    sync_cursor();
    bool busy = s.model.busy != 0;
    switch (e.mode) {
    case M_GEAR:
        if (in->repeat & PC_UI_DOWN)
            e.slot = (e.slot + 1) % PC_UI_EQUIP_SLOTS;
        if (in->repeat & PC_UI_UP)
            e.slot = (e.slot + PC_UI_EQUIP_SLOTS - 1) % PC_UI_EQUIP_SLOTS;
        if ((in->pressed & PC_UI_CONFIRM) && !equip_locked())
            open_list(M_PICK);
        else if ((in->pressed & PC_UI_ALT) && !equip_locked() && !busy && s.model.valid
            && s.model.equipped[e.slot].inventory >= 0)
            push(PC_UI_REQ_EQUIP, e.slot, -1, -1); // take off (the game allows no weapon too)
        break;
    case M_QUICK:
        if (in->repeat & PC_UI_DOWN)
            e.quick = (e.quick + 1) % PC_UI_QUICK;
        if (in->repeat & PC_UI_UP)
            e.quick = (e.quick + PC_UI_QUICK - 1) % PC_UI_QUICK;
        if (in->repeat & (PC_UI_LEFT | PC_UI_RIGHT)) // face buttons <-> D-pad group
            e.quick = (e.quick + PC_UI_QUICK / 2) % PC_UI_QUICK;
        if (in->pressed & PC_UI_CONFIRM)
            open_list(M_QUICK_WEAPON);
        else if ((in->pressed & PC_UI_ALT) && s.model.valid && s.model.quick_weapon[e.quick] >= 0)
            push(PC_UI_REQ_QUICK_CLEAR, e.quick, -1, -1);
        break;
    default: { // pick lists
        int n = list_rows();
        if (n && (in->repeat & PC_UI_DOWN))
            e.cursor = (e.cursor + 1) % n;
        if (n && (in->repeat & PC_UI_UP))
            e.cursor = (e.cursor + n - 1) % n;
        if (!(in->pressed & PC_UI_CONFIRM) || !n || busy)
            break;
        if (e.mode == M_PICK) {
            if (equip_locked())
                break;
            int inv = list_inventory(e.cursor);
            if (inv != s.model.equipped[e.slot].inventory)
                push(PC_UI_REQ_EQUIP, e.slot, inv, -1);
            equip_base();
        } else if (e.mode == M_QUICK_WEAPON) {
            const pc_ui_item* w = list_item(e.cursor);
            if (w->two_handed) {
                push(PC_UI_REQ_QUICK_SET, e.quick, w->inventory, -1);
                equip_base();
            } else {
                e.pending_item = *w; // before open_list: the list may be replaced
                e.has_pending = true;
                e.pending_weapon = w->inventory;
                open_list(M_QUICK_SHIELD);
            }
        } else {
            push(PC_UI_REQ_QUICK_SET, e.quick, e.pending_weapon, list_inventory(e.cursor));
            equip_base();
        }
        break;
    }
    }
}
void item_json(std::string& j, const pc_ui_item& it)
{
    char buf[160];
    j += "\"name\":\"" + escape(it.name) + "\",\"material\":\"" + escape(it.material) + "\"";
    std::snprintf(buf, sizeof buf, ",\"inventory\":%d,\"dp\":%d,\"dp_max\":%d,\"str\":%d,\"int\":%d,\"agl\":%d",
        it.inventory, it.dp, it.dp_max, it.str, it.intel, it.agl);
    j += buf;
}
void equip_json(std::string& j)
{
    const Equip& e = s.eq;
    const pc_ui_equipment& m = s.model;
    char buf[192];
    j += ",\"equipment\":{\"sub\":\"";
    j += e.sub ? "quick" : "gear";
    j += "\",\"mode\":\"" + std::string(MODE_IDS[e.mode]) + "\"";
    std::snprintf(buf, sizeof buf,
        ",\"valid\":%s,\"busy\":%s,\"locked\":%s,\"quick_enabled\":%s,\"slot\":\"%s\",\"quick_slot\":\"%s\"",
        m.valid ? "true" : "false", m.busy ? "true" : "false", equip_locked() ? "true" : "false",
        m.quick_enabled ? "true" : "false", SLOT_IDS[e.slot], QUICK_IDS[e.quick]);
    j += buf;
    if (equip_locked())
        j += ",\"reason\":\"" + escape(row_reason(EQUIP_GATE)) + "\"";
    std::snprintf(buf, sizeof buf, ",\"str\":%d,\"int\":%d,\"agl\":%d,\"slots\":[", m.str, m.intel, m.agl);
    j += buf;
    for (int i = 0; i < PC_UI_EQUIP_SLOTS; ++i) {
        j += i ? ",{\"id\":\"" : "{\"id\":\"";
        j += SLOT_IDS[i];
        j += "\",";
        item_json(j, m.equipped[i]);
        j += "}";
    }
    j += "],\"quick\":[";
    for (int i = 0; i < PC_UI_QUICK; ++i) {
        std::snprintf(buf, sizeof buf, "%s{\"id\":\"%s\",\"weapon\":%d,\"shield\":%d", i ? "," : "", QUICK_IDS[i],
            m.quick_weapon[i], m.quick_shield[i]);
        j += buf;
        j += ",\"weapon_name\":\"" + escape(m.quick_weapon_name[i]) + "\",\"shield_name\":\"" +
            escape(m.quick_shield_name[i]) + "\"}";
    }
    j += "]";
    if (picking()) {
        int n = list_rows();
        std::snprintf(buf, sizeof buf, ",\"loading\":%s,\"cursor\":%d,\"candidates\":[", list_ready() ? "false" : "true",
            e.cursor);
        j += buf;
        // deltas against what the list would replace: the slot's item (quick lists: the gear slot too)
        const pc_ui_item& cur = m.equipped[wanted_slot()];
        for (int r = 0; r < n; ++r) {
            j += r ? ",{" : "{";
            const pc_ui_item* it = list_item(r);
            if (!it) {
                j += "\"name\":\"No shield\",\"inventory\":-1}";
                continue;
            }
            item_json(j, *it);
            std::snprintf(buf, sizeof buf, ",\"two_handed\":%s,\"dstr\":%d,\"dint\":%d,\"dagl\":%d,\"ddp\":%d}",
                it->two_handed ? "true" : "false", it->str - cur.str, it->intel - cur.intel, it->agl - cur.agl,
                it->dp - cur.dp);
            j += buf;
        }
        j += "]";
    }
    j += "}";
}

// ---- Ashley ----
const int STATUS_COMMAND = 4;
int ash_effects()
{
    int n = s.ash.effect_count;
    return n < 0 ? 0 : n > PC_UI_EFFECTS ? PC_UI_EFFECTS : n;
}
// effect rows: the chips on screen
int ash_rows() { return PC_UI_ASH_EFFECT + (s.ash_shown >= 0 && s.ash_shown < ash_effects() ? s.ash_shown : ash_effects()); }
int ash_limbs() { return s.ash.limb_count < 0 ? 0 : s.ash.limb_count > PC_UI_LIMBS ? PC_UI_LIMBS : s.ash.limb_count; }
void ash_clamp()
{
    int n = ash_rows();
    if (s.ash_row >= n)
        s.ash_row = n - 1;
    if (s.ash_row < 0)
        s.ash_row = 0;
    if (s.ash_row != PC_UI_ASH_CONDITION || s.ash_limb >= ash_limbs())
        s.ash_limb = -1;
}
bool on_ashley() { return s.visible && TABS[s.tab].screen == SCREEN_ASHLEY; }
int tab_of(Screen screen)
{
    for (int t = 0; t < TAB_COUNT; ++t)
        if (TABS[t].screen == screen)
            return t;
    return -1;
}
// Circle on weapon / shield: Equipment > Gear on that slot (when the tab is there).
void ash_confirm()
{
    int slot = s.ash_row == PC_UI_ASH_WEAPON ? PC_UI_SLOT_WEAPON
        : s.ash_row == PC_UI_ASH_SHIELD      ? PC_UI_SLOT_SHIELD
                                             : -1;
    int t = tab_of(SCREEN_EQUIPMENT);
    if (slot < 0 || t < 0 || !tab_has_rows(t))
        return;
    s.tab = t;
    s.eq.sub = 0;
    equip_base();
    s.eq.slot = slot;
    s.drag_x = s.drag_y = 0;
}
// One tick of the Ashley screen; the game's result (0 stay, 5 original Status).
int ashley_tick(const pc_ui_input* in)
{
    if (in->pressed & PC_UI_ALT) {
        s.visible = false; // the game shows Status; it reopens the shell after
        s.drag_x = s.drag_y = 0;
        return STATUS_COMMAND + 1;
    }
    ash_clamp();
    int n = ash_rows();
    if (in->repeat & PC_UI_DOWN)
        s.ash_row = (s.ash_row + 1) % n;
    if (in->repeat & PC_UI_UP)
        s.ash_row = (s.ash_row + n - 1) % n;
    // Condition: Left / Right step through the body parts and back to the panel as a whole (-1)
    if (s.ash_row == PC_UI_ASH_CONDITION && ash_limbs() > 0 && (in->repeat & (PC_UI_LEFT | PC_UI_RIGHT))) {
        int k = ash_limbs() + 1, at = s.ash_limb + 1;
        at = (in->repeat & PC_UI_RIGHT) ? (at + 1) % k : (at + k - 1) % k;
        s.ash_limb = at - 1;
    }
    ash_clamp();
    if (in->pressed & PC_UI_CONFIRM)
        ash_confirm();
    return 0;
}
const char* ash_row_id(int row, char* buf, int size)
{
    static const char* const ids[PC_UI_ASH_EFFECT] = {"weapon", "shield", "condition"};
    if (row < PC_UI_ASH_EFFECT)
        return ids[row];
    std::snprintf(buf, size, "effect%d", row - PC_UI_ASH_EFFECT);
    return buf;
}
void ashley_json(std::string& j)
{
    const pc_ui_ashley& a = s.ash;
    char buf[512];
    std::snprintf(buf, sizeof buf,
        ",\"ashley\":{\"valid\":%s,\"hp\":%d,\"hp_max\":%d,\"mp\":%d,\"mp_max\":%d,\"risk\":%d,\"str\":%d,"
        "\"int\":%d,\"agl\":%d,",
        a.valid ? "true" : "false", a.hp, a.hp_max, a.mp, a.mp_max, a.risk, a.str, a.intel, a.agl);
    j += buf;
    // the weapon and shield rows (the equipment snapshot's worn items, "" none)
    j += "\"weapon\":\"" + escape(s.model.equipped[PC_UI_SLOT_WEAPON].name) + "\",\"shield\":\""
        + escape(s.model.equipped[PC_UI_SLOT_SHIELD].name) + "\",\"limbs\":[";
    int limbs = a.limb_count < 0 ? 0 : a.limb_count > PC_UI_LIMBS ? PC_UI_LIMBS : a.limb_count;
    for (int i = 0; i < limbs; ++i) {
        const pc_ui_limb& l = a.limbs[i];
        j += i ? ",{\"name\":\"" : "{\"name\":\"";
        j += escape(l.name);
        std::snprintf(buf, sizeof buf, "\",\"hp\":%d,\"hp_max\":%d,\"state\":%d}", l.hp, l.hp_max, l.state);
        j += buf;
    }
    j += "],\"effects\":[";
    for (int i = 0; i < ash_effects(); ++i) {
        const pc_ui_effect& e = a.effects[i];
        j += i ? ",{\"name\":\"" : "{\"name\":\"";
        j += escape(e.name);
        std::snprintf(buf, sizeof buf, "\",\"buff\":%s,\"seconds\":%d}", e.buff ? "true" : "false", e.seconds);
        j += buf;
    }
    j += "],\"area\":\"" + escape(a.area) + "\",\"room\":\"" + escape(a.room) + "\"";
    char id[24];
    std::snprintf(buf, sizeof buf,
        ",\"play\":\"%d:%02d:%02d\",\"model\":%d,\"model_rect\":[%g,%g,%g,%g],\"model_yaw\":%d,"
        "\"model_pitch\":%d,\"model_blocked\":\"%s\",\"heap_free\":%d,\"heap_need\":%d,\"effects_shown\":%d,"
        "\"row\":\"%s\",\"limb\":%d",
        a.play_h, a.play_m, a.play_s, a.model, a.model_rect[0], a.model_rect[1], a.model_rect[2], a.model_rect[3],
        a.model_yaw, a.model_pitch,
        a.model_blocked == PC_UI_MODEL_NO_HEAP ? "heap" : a.model_blocked == PC_UI_MODEL_FREE_CAMERA ? "free_camera" : "",
        a.heap_free, a.heap_need, ash_rows() - PC_UI_ASH_EFFECT, ash_row_id(s.ash_row, id, sizeof id),
        pc_ui_shell_ashley_limb());
    j += buf;
    // the Condition card's title and first line (pc_ui_ashley_condition_text; "" on other rows)
    char title[96] = "", line[192] = "";
    if (s.ash_row == PC_UI_ASH_CONDITION)
        pc_ui_ashley_condition_text(&a, pc_ui_shell_ashley_limb(), title, sizeof title, line, sizeof line);
    j += ",\"details\":\"" + escape(title) + "\",\"details_line\":\"" + escape(line) + "\"}";
}

// ---- System ----
// The game's own screens at the top of the list (original commands 6-9).
const RowDef SYSTEM_GAME[] = {
    {"data", "Data", "Save and load.", ROW_COMMAND, 6},
    {"options", "Options (original)", "The game's own Options screen.", ROW_COMMAND, 7},
    {"score", "Score", "Titles, gazette, records, encyclopaedia.", ROW_COMMAND, 8},
    {"manual", "Quick manual", "The game's own guide: controls, battle, magic and maps.", ROW_COMMAND, 9},
};
const int SYSTEM_GAMES = sizeof SYSTEM_GAME / sizeof SYSTEM_GAME[0];
// The last entries of the list, after the settings pages: Quit to title and Quit to desktop
// (quit_options.h; the same question the F1 overlay's File menu and the classic Option screen ask).
struct QuitDef {
    const char* id;
    const char* label;
    const char* help;
    int to; // PC_QUIT_*
};
const QuitDef SYSTEM_QUIT[] = {
    {"quit_title", "Quit to title", "Back to the title screen, as a soft reset does (L1 + L2 + R1 + R2 + Select + Start).",
        PC_QUIT_TITLE},
    {"quit_desktop", "Quit to desktop", "Close the game. Settings are saved, as when the window is closed.",
        PC_QUIT_DESKTOP},
};
const int SYSTEM_QUITS = sizeof SYSTEM_QUIT / sizeof SYSTEM_QUIT[0];
bool on_system() { return TABS[s.tab].screen == SCREEN_SYSTEM; }
// The game entries shown now (menuFlags), as indices into SYSTEM_GAME.
int sys_games(int* out)
{
    int n = 0;
    for (int i = 0; i < SYSTEM_GAMES; ++i)
        if (row_shown(SYSTEM_GAME[i]))
            out[n++] = i;
    return n;
}
int sys_items()
{
    int g[SYSTEM_GAMES];
    return sys_games(g) + pc_ui_settings_pages() + SYSTEM_QUITS;
}
// The first quit entry's list index.
int sys_quit_first()
{
    int g[SYSTEM_GAMES];
    return sys_games(g) + pc_ui_settings_pages();
}
// The settings page of list entry `item`, -1 for a game entry.
int sys_item_page(int item)
{
    int g[SYSTEM_GAMES];
    int games = sys_games(g);
    return item >= games && item < games + pc_ui_settings_pages() ? item - games : -1;
}
std::string page_key(int page)
{
    const char* id = page >= 0 ? pc_ui_settings_page_id(page) : nullptr;
    return id ? id : "";
}
// The pages changed (a plugin's page defined or gone): the entered page and the
// focused entry follow their page by id; a page that is gone leaves to the list.
void sys_sync()
{
    unsigned generation = pc_ui_settings_generation();
    if (generation == s.sys.generation)
        return;
    s.sys.generation = generation;
    int g[SYSTEM_GAMES];
    int games = sys_games(g);
    if (!s.sys.item_page_id.empty()) {
        int page = pc_ui_settings_page_index(s.sys.item_page_id.c_str());
        if (page >= 0)
            s.sys.item = games + page;
    }
    if (s.sys.page >= 0) {
        s.sys.page = pc_ui_settings_page_index(s.sys.page_id.c_str());
        if (s.sys.page < 0)
            s.sys.page_id.clear();
    }
}
// The page's focused row (kept per page id).
int& sys_row(int page) { return s.sys.rows[page_key(page)]; }
// The page the card shows: the entered one, else the focused entry's (-1: a game entry).
int sys_shown_page()
{
    sys_sync();
    return s.sys.page >= 0 ? s.sys.page : sys_item_page(s.sys.item);
}
// Headers and notes are not focused (the classic screen skips them too).
bool sys_selectable(int page, int row)
{
    pc_ui_setting_row r;
    return pc_ui_settings_row(page, row, &r) && r.kind != PC_UI_SET_HEADER && r.kind != PC_UI_SET_NOTE;
}
bool capturing() { return pc_ui_settings_capture_button() >= 0; }
// Keep the focus on the list and the page's rows (rows come and go with other settings).
int sys_clamp()
{
    sys_sync();
    int n = sys_items();
    if (s.sys.item >= n)
        s.sys.item = n ? n - 1 : 0;
    if (s.sys.item < 0)
        s.sys.item = 0;
    int item_page = sys_item_page(s.sys.item);
    s.sys.item_page_id = page_key(item_page);
    int page = sys_shown_page();
    if (page < 0)
        return 0;
    int rows = pc_ui_settings_rows(page);
    int& r = sys_row(page);
    if (r >= rows)
        r = rows ? rows - 1 : 0;
    if (r < 0)
        r = 0;
    for (int k = 0; k < rows && !sys_selectable(page, r); k++) // off a header or note
        r = (r + 1) % rows;
    return rows;
}
// Out of the page (Cross, another tab, the menu closing, an original screen):
// a capture that listens is cancelled and what changed is saved.
// The Option pages' preview (option_peek.h): weak, the probes run without it.
extern "C" void pc_option_peek(int on, const char* label, const char* value) __attribute__((weak));
extern "C" int pc_option_peek_active(void) __attribute__((weak));
extern "C" unsigned pc_ui_menu_held(void) __attribute__((weak)); // menu_bridge.c
void peek(bool on, const pc_ui_setting_row* r = nullptr)
{
    if (pc_option_peek)
        pc_option_peek(on, on && r ? r->label : nullptr, on && r ? r->value : nullptr);
}
void sys_leave()
{
    peek(false);
    pc_ui_manual_leave(); // out of the Quick manual too (its store events and the freeze back)
    pc_ui_score_leave();  // and the Score screen (its gazetteRow and the freeze back)
    if (capturing())
        pc_ui_settings_capture_done(-1, 0);
    pc_ui_settings_flush();
    s.sys.page = -1;
    s.sys.page_id.clear();
}
void sys_enter(int page)
{
    s.sys.page = page;
    s.sys.page_id = page_key(page);
    sys_clamp();
}
// Circle (or Right) on the list. Returns the game's result (n > 0: an original screen).
int sys_list_confirm()
{
    int page = sys_item_page(s.sys.item);
    if (page >= 0) {
        sys_enter(page);
        return 0;
    }
    if (s.sys.item >= sys_quit_first()) { // Quit to title / Quit to desktop: the question, or at once on the title
        if (pc_quit_ask)
            pc_quit_ask(SYSTEM_QUIT[s.sys.item - sys_quit_first()].to);
        return 0;
    }
    int g[SYSTEM_GAMES];
    sys_games(g);
    if (!std::strcmp(SYSTEM_GAME[g[s.sys.item]].id, "manual")) {
        pc_ui_manual_enter(); // the native Quick manual (ui_manual.cpp); Square opens the original
        return 0;
    }
    if (!std::strcmp(SYSTEM_GAME[g[s.sys.item]].id, "score")) {
        pc_ui_score_enter(); // the native Score screen (ui_score.cpp); Square opens the original (MENU9)
        return 0;
    }
    sys_leave();
    s.visible = false; // the game shows the original screen; it reopens the shell after
    return SYSTEM_GAME[g[s.sys.item]].command + 1;
}
// Circle on a row: actions run, bindings start a capture, choices and toggles step,
// links open their page (its entry focused in the list).
void sys_row_confirm(int page, int row)
{
    pc_ui_setting_row r;
    if (!pc_ui_settings_row(page, row, &r))
        return;
    if (r.kind == PC_UI_SET_LINK) {
        int target = pc_ui_settings_link(page, row);
        if (target >= 0) {
            pc_ui_settings_flush();
            int g[SYSTEM_GAMES];
            s.sys.item = sys_games(g) + target;
            sys_enter(target);
        }
        return;
    }
    if (r.kind == PC_UI_SET_ACTION || r.kind == PC_UI_SET_BINDING)
        pc_ui_settings_activate(page, row);
    else if (r.kind == PC_UI_SET_CHOICE || r.kind == PC_UI_SET_TOGGLE)
        pc_ui_settings_change(page, row, 1);
    sys_clamp();
}
// The System list's entry is the Quick manual's row.
bool sys_on_manual()
{
    int g[SYSTEM_GAMES];
    int games = sys_games(g);
    return s.sys.page < 0 && s.sys.item < games && !std::strcmp(SYSTEM_GAME[g[s.sys.item]].id, "manual");
}
// The System list's entry is the Score row.
bool sys_on_score()
{
    int g[SYSTEM_GAMES];
    int games = sys_games(g);
    return s.sys.page < 0 && s.sys.item < games && !std::strcmp(SYSTEM_GAME[g[s.sys.item]].id, "score");
}
int system_tick(const pc_ui_input* in)
{
    if (pc_ui_score_mode()) { // the Score screen has the view
        int r = pc_ui_score_tick(in);
        if (r) { // Square: the original MENU9 (its cursor memory written at the leave)
            sys_leave();
            s.visible = false;
        }
        return r;
    }
    if (pc_ui_manual_mode()) { // the Quick manual has the screen
        int r = pc_ui_manual_tick(in);
        if (r) { // Square: the original MENUE on the manual's topic
            sys_leave();
            s.visible = false;
        }
        return r;
    }
    int n = sys_items();
    sys_clamp();
    if (s.sys.page < 0) {
        if ((in->pressed & PC_UI_ALT) && sys_on_manual()) { // Square: the original Quick manual (MENUE)
            pc_ui_manual_original();
            sys_leave();
            s.visible = false;
            return 10;
        }
        if ((in->pressed & PC_UI_ALT) && sys_on_score()) { // Square: the original Score screen (MENU9)
            sys_leave();
            s.visible = false;
            return 9;
        }
        if (in->repeat & PC_UI_DOWN)
            s.sys.item = (s.sys.item + 1) % n;
        if (in->repeat & PC_UI_UP)
            s.sys.item = (s.sys.item + n - 1) % n;
        if ((in->pressed & PC_UI_CONFIRM) || ((in->pressed & PC_UI_RIGHT) && sys_item_page(s.sys.item) >= 0))
            return sys_list_confirm();
        return 0;
    }
    int page = s.sys.page;
    int rows = sys_clamp();
    if (!rows)
        return 0;
    int& row = sys_row(page);
    // The preview: Square held on a row that offers one: the menu steps aside for the live 3D view
    // (option_peek.h); Left / Right still change the value, nothing else moves.
    pc_ui_setting_row focused;
    bool previewing = pc_ui_menu_held && (pc_ui_menu_held() & PC_UI_ALT) && !capturing()
        && pc_ui_settings_row(page, row, &focused) && focused.preview && focused.enabled;
    if (previewing) {
        int step = ((in->repeat & PC_UI_RIGHT) ? 1 : 0) - ((in->repeat & PC_UI_LEFT) ? 1 : 0);
        if (step)
            pc_ui_settings_change(page, row, step);
        pc_ui_settings_row(page, row, &focused);
        peek(true, &focused);
        return 0;
    }
    peek(false);
    for (int k = 0; k < rows && (in->repeat & PC_UI_DOWN); k++) { // past headers and notes
        row = (row + 1) % rows;
        if (sys_selectable(page, row))
            break;
    }
    for (int k = 0; k < rows && (in->repeat & PC_UI_UP); k++) {
        row = (row + rows - 1) % rows;
        if (sys_selectable(page, row))
            break;
    }
    int delta = ((in->repeat & PC_UI_RIGHT) ? 1 : 0) - ((in->repeat & PC_UI_LEFT) ? 1 : 0);
    if (delta) {
        pc_ui_settings_change(page, row, delta);
        sys_clamp();
    }
    if (in->pressed & PC_UI_CONFIRM)
        sys_row_confirm(page, row);
    return 0;
}
// "Keep these settings?" (display_confirm.h): a risky display change waits for an answer; the
// modal takes the input until it is answered or reverts by itself.
bool confirm_up()
{
    bool up = pc_display_confirm_pending() != 0;
    if (up && !s.sys.confirm_seen)
        s.sys.confirm_focus = 0;
    s.sys.confirm_seen = up;
    return up;
}
void confirm_answer(bool keep)
{
    if (keep)
        pc_display_confirm_keep();
    else
        pc_display_confirm_revert();
    s.sys.confirm_seen = false;
}
int confirm_tick(const pc_ui_input* in)
{
    if (in->repeat & (PC_UI_LEFT | PC_UI_RIGHT | PC_UI_UP | PC_UI_DOWN))
        s.sys.confirm_focus ^= 1;
    if (in->pressed & PC_UI_CONFIRM)
        confirm_answer(s.sys.confirm_focus == 0);
    else if (in->pressed & (PC_UI_BACK | PC_UI_EXIT))
        confirm_answer(false);
    return 0;
}
void confirm_json(std::string& j)
{
    char buf[160];
    std::snprintf(buf, sizeof buf, ",\"confirm\":{\"pending\":%s,\"remaining\":%.2f,\"focus\":\"%s\"}",
        pc_display_confirm_pending() ? "true" : "false", pc_display_confirm_remaining(),
        s.sys.confirm_focus ? "revert" : "keep");
    j += buf;
}

// "Unsaved progress will be lost" (quit_options.h): Quit to title / Quit to desktop wait for Yes or No;
// the modal takes the input. It opens on No.
bool quit_up()
{
    bool up = pc_quit_pending && pc_quit_pending() != 0;
    if (up && !s.sys.quit_seen)
        s.sys.quit_focus = 1;
    s.sys.quit_seen = up;
    return up;
}
void quit_answer(bool yes)
{
    s.sys.quit_seen = false;
    if (pc_quit_answer)
        pc_quit_answer(yes ? 1 : 0);
}
int quit_tick(const pc_ui_input* in)
{
    if (in->repeat & (PC_UI_LEFT | PC_UI_RIGHT | PC_UI_UP | PC_UI_DOWN))
        s.sys.quit_focus ^= 1;
    if (in->pressed & PC_UI_CONFIRM)
        quit_answer(s.sys.quit_focus == 0);
    else if (in->pressed & (PC_UI_BACK | PC_UI_EXIT))
        quit_answer(false);
    return 0;
}
void quit_json(std::string& j)
{
    int to = pc_quit_pending ? pc_quit_pending() : 0;
    j += ",\"quit\":{\"pending\":\"";
    j += to == PC_QUIT_TITLE ? "title" : to == PC_QUIT_DESKTOP ? "desktop" : "";
    j += s.sys.quit_focus ? "\",\"focus\":\"no\"}" : "\",\"focus\":\"yes\"}";
}

// The PS1 button's binding row id ("bind_triangle"), "" when none.
const char* capture_row_id()
{
    int button = pc_ui_settings_capture_button();
    const char* id = nullptr;
    if (button < 0 || pc_ui_settings_binding(button, &id, nullptr) < 0)
        return "";
    return id;
}
void system_json(std::string& j)
{
    int page = sys_shown_page();
    char buf[160];
    j += ",\"system\":{\"mode\":\"";
    j += s.sys.page >= 0 ? "page" : "list";
    j += "\",\"item\":\"";
    const char *id, *label, *help, *reason;
    int enabled;
    if (pc_ui_shell_system_item(s.sys.item, &id, &label, &help, &enabled, &reason))
        j += id;
    j += "\",\"page\":\"";
    j += page >= 0 ? pc_ui_settings_page_id(page) : "";
    j += "\",\"row\":\"";
    int rows = page >= 0 ? pc_ui_settings_rows(page) : 0;
    pc_ui_setting_row r;
    if (rows && pc_ui_settings_row(page, sys_row(page), &r))
        j += r.id;
    j += "\",\"rows\":[";
    static const char* const kinds[] = {"choice", "toggle", "slider", "action", "binding", "info", "header", "note",
        "link"};
    for (int i = 0; i < rows; ++i) {
        if (!pc_ui_settings_row(page, i, &r))
            break;
        j += i ? ",{\"id\":\"" : "{\"id\":\"";
        j += escape(r.id) + "\",\"label\":\"" + escape(r.label) + "\",\"kind\":\"";
        j += r.kind >= 0 && r.kind <= PC_UI_SET_LINK ? kinds[r.kind] : "";
        j += "\",\"value\":\"" + escape(r.value) + "\",\"enabled\":";
        j += r.enabled ? "true" : "false";
        if (r.reason[0])
            j += ",\"reason\":\"" + escape(r.reason) + "\"";
        if (r.swatch) {
            std::snprintf(buf, sizeof buf, ",\"swatch\":\"#%02x%02x%02x\"", r.swatch_rgb[0], r.swatch_rgb[1],
                r.swatch_rgb[2]);
            j += buf;
        }
        if (r.preview)
            j += ",\"preview\":true";
        j += "}";
    }
    std::snprintf(buf, sizeof buf, "],\"peek\":%s,\"capture\":\"%s\",\"dirty\":%s,\"saves\":%d,",
        pc_option_peek_active && pc_option_peek_active() ? "true" : "false", capture_row_id(),
        pc_ui_settings_dirty() ? "true" : "false", pc_ui_settings_saves());
    j += buf;
    double quiet, quiet_before;
    pc_ui_settings_debounce(&quiet, &quiet_before);
    std::snprintf(buf, sizeof buf, "\"debounce\":{\"quiet\":%.4f,\"quiet_before\":%.4f},", quiet, quiet_before);
    j += buf;
    const char* note = pc_ui_settings_note();               // a binding swap's (3 s)
    const char* refused = pc_ui_settings_capture_note();    // a reserved key pressed during the capture
    j += "\"note\":\"" + escape(note ? note : "") + "\",\"capture_note\":\"" + escape(refused ? refused : "")
        + "\",\"status\":\"";
    const char* status = pc_settings_status();
    j += escape(status ? status : "") + "\"";
    pc_ui_manual_json(j);
    pc_ui_score_json(j);
    confirm_json(j);
    quit_json(j);
    j += "}";
}

// ---- Abilities ----
// Chain and Defense: the Battle Abilities command (2); Break Arts: its command (1).
const int BATTLE_ABILITIES_COMMAND = 2, BREAK_ARTS_COMMAND = 1;
const char* const AB_SUB_IDS[3] = {"chain", "defense", "arts"};
const char* const AB_MODE_IDS[] = {"slots", "list", "pick", "categories", "arts"};
const char* const AB_BUTTON_IDS[PC_UI_ABIL_BUTTONS] = {"circle", "triangle", "square"};
const char* const ART_STATE_IDS[] = {"locked", "pending", "learned"};
// The sub-tabs the game has unlocked, in order (PC_UI_AB_*).
int ab_subs(int* out)
{
    int n = 0;
    if ((s.commands.enabled >> BATTLE_ABILITIES_COMMAND) & 1)
        out[n++] = PC_UI_AB_CHAIN, out[n++] = PC_UI_AB_DEFENSE;
    if ((s.commands.enabled >> BREAK_ARTS_COMMAND) & 1)
        out[n++] = PC_UI_AB_ARTS;
    return n;
}
bool on_abilities() { return TABS[s.tab].screen == SCREEN_ABILITIES; }
int ab_base_mode(int kind) { return kind == PC_UI_AB_ARTS ? PC_UI_ABM_CATEGORIES : PC_UI_ABM_SLOTS; }
// The current sub-tab's index among the shown ones; puts the kind on a shown one.
int ab_sub_index()
{
    int subs[3], n = ab_subs(subs);
    for (int i = 0; i < n; ++i)
        if (subs[i] == s.ab.kind)
            return i;
    if (n) {
        s.ab.kind = subs[0];
        s.ab.mode = ab_base_mode(s.ab.kind);
    }
    return 0;
}
bool ab_chain_kind() { return s.ab.kind == PC_UI_AB_CHAIN || s.ab.kind == PC_UI_AB_DEFENSE; }
// The list of the current chain / defense sub-tab (PC_UI_ABIL_CHAIN / _DEFENSE = the kind).
const pc_ui_ability_list& ab_list() { return s.ab_model.lists[s.ab.kind == PC_UI_AB_DEFENSE ? 1 : 0]; }
int ab_count()
{
    int n = ab_list().count;
    return n < 0 ? 0 : n > PC_UI_ABILITIES ? PC_UI_ABILITIES : n;
}
// The rail's categories (1..10): the equipped one first, then the game's order.
int ab_order(int* out)
{
    int eq = s.ab_model.equipped_category, n = 0;
    if (eq >= 1 && eq <= PC_UI_ART_CATEGORIES)
        out[n++] = eq;
    for (int c = 1; c <= PC_UI_ART_CATEGORIES; ++c)
        if (c != eq)
            out[n++] = c;
    return n;
}
int ab_category()
{
    if (s.ab.category < 1 || s.ab.category > PC_UI_ART_CATEGORIES) {
        int order[PC_UI_ART_CATEGORIES];
        ab_order(order);
        s.ab.category = order[0];
    }
    return s.ab.category;
}
int ab_category_row()
{
    int order[PC_UI_ART_CATEGORIES], c = ab_category();
    ab_order(order);
    for (int i = 0; i < PC_UI_ART_CATEGORIES; ++i)
        if (order[i] == c)
            return i;
    return 0;
}
const pc_ui_art_category& ab_cat() { return s.ab_model.categories[ab_category() - 1]; }
void ab_clamp()
{
    ab_sub_index();
    for (int t = 0; t < 2; ++t) {
        int n = s.ab_model.lists[t].count;
        n = n < 0 ? 0 : n > PC_UI_ABILITIES ? PC_UI_ABILITIES : n;
        if (s.ab.cursor[t] >= n)
            s.ab.cursor[t] = n ? n - 1 : 0;
        if (s.ab.cursor[t] < 0)
            s.ab.cursor[t] = 0;
        if (s.ab.slot[t] < 0 || s.ab.slot[t] >= PC_UI_ABIL_BUTTONS)
            s.ab.slot[t] = 0;
    }
    if (ab_chain_kind() && (s.ab.mode == PC_UI_ABM_LIST || s.ab.mode == PC_UI_ABM_PICK) && !ab_count())
        s.ab.mode = PC_UI_ABM_SLOTS; // nothing learned (or the list went away)
    if (s.ab.art < 0 || s.ab.art >= PC_UI_ARTS)
        s.ab.art = 0;
}
// Out of a list onto the sub-tab's base.
void ab_base() { s.ab.mode = ab_base_mode(s.ab.kind); }
// A pick list (for one button) never survives the tab or the shell closing.
void ab_unpick()
{
    if (s.ab.mode == PC_UI_ABM_PICK)
        s.ab.mode = PC_UI_ABM_SLOTS;
}
// The Abilities screen takes Triangle itself: the learned list maps it (the original's direct presses).
bool ab_takes_triangle() { return on_abilities() && ab_chain_kind() && s.ab.mode == PC_UI_ABM_LIST; }
// Cross: one step back; false at a sub-tab's base (the shell closes).
bool ab_back()
{
    if (s.ab.mode == PC_UI_ABM_LIST || s.ab.mode == PC_UI_ABM_PICK || s.ab.mode == PC_UI_ABM_ARTS) {
        ab_base();
        return true;
    }
    return false;
}
void ab_map(int button)
{
    const pc_ui_ability_list& l = ab_list();
    int t = s.ab.kind == PC_UI_AB_DEFENSE ? 1 : 0;
    if (!ab_count())
        return;
    push(PC_UI_REQ_ABIL_MAP, button, l.entries[s.ab.cursor[t]].id, t == 1 ? PC_UI_ABIL_DEFENSE : PC_UI_ABIL_CHAIN);
}
int ab_original() // Square: the original screen of the sub-tab (results: command + 1)
{
    s.visible = false; // the game shows its screen; it reopens the shell after
    s.drag_x = s.drag_y = 0;
    return (ab_chain_kind() ? BATTLE_ABILITIES_COMMAND : BREAK_ARTS_COMMAND) + 1;
}
int abilities_tick(const pc_ui_input* in)
{
    int subs[3], n = ab_subs(subs);
    if (!n)
        return 0;
    ab_clamp();
    if (in->pressed & (PC_UI_SUB_NEXT | PC_UI_SUB_PREV)) {
        int i = ab_sub_index(), dir = (in->pressed & PC_UI_SUB_NEXT) ? 1 : -1;
        s.ab.kind = subs[(i + dir + n) % n];
        ab_base();
        return 0;
    }
    Abil& a = s.ab;
    if (ab_chain_kind()) {
        int t = a.kind == PC_UI_AB_DEFENSE ? 1 : 0, count = ab_count();
        int& slot = a.slot[t];
        int& cur = a.cursor[t];
        switch (a.mode) {
        case PC_UI_ABM_SLOTS:
            if (in->repeat & PC_UI_RIGHT)
                slot = (slot + 1) % PC_UI_ABIL_BUTTONS;
            if (in->repeat & PC_UI_LEFT)
                slot = (slot + PC_UI_ABIL_BUTTONS - 1) % PC_UI_ABIL_BUTTONS;
            if ((in->pressed & PC_UI_DOWN) && count)
                a.mode = PC_UI_ABM_LIST;
            if (in->pressed & PC_UI_ALT)
                return ab_original();
            if ((in->pressed & PC_UI_CONFIRM) && count) {
                // the pick list opens on what the button holds
                const pc_ui_ability_list& l = ab_list();
                if (l.slots[slot] >= 0 && l.slots[slot] < count)
                    cur = l.slots[slot];
                a.mode = PC_UI_ABM_PICK;
            }
            break;
        case PC_UI_ABM_LIST: // the original's style: Circle / Triangle / Square put the focused ability on that button
            if (in->repeat & PC_UI_DOWN)
                cur = (cur + 1) % count;
            if (in->repeat & PC_UI_UP) {
                if (cur == 0 && (in->pressed & PC_UI_UP))
                    a.mode = PC_UI_ABM_SLOTS;
                else if (cur > 0)
                    --cur;
            }
            if (in->pressed & PC_UI_CONFIRM)
                ab_map(0);
            else if (in->pressed & PC_UI_EXIT)
                ab_map(1);
            else if (in->pressed & PC_UI_ALT)
                ab_map(2);
            break;
        case PC_UI_ABM_PICK: // for the focused button
            if (in->repeat & PC_UI_DOWN)
                cur = (cur + 1) % count;
            if (in->repeat & PC_UI_UP)
                cur = (cur + count - 1) % count;
            if (in->pressed & PC_UI_CONFIRM) {
                ab_map(slot);
                a.mode = PC_UI_ABM_SLOTS;
            }
            break;
        default:
            ab_base();
            break;
        }
        return 0;
    }
    // Break Arts
    if (in->pressed & PC_UI_ALT)
        return ab_original();
    int order[PC_UI_ART_CATEGORIES];
    ab_order(order);
    int row = ab_category_row();
    switch (a.mode) {
    case PC_UI_ABM_CATEGORIES:
        if (in->repeat & PC_UI_DOWN)
            a.category = order[(row + 1) % PC_UI_ART_CATEGORIES];
        if (in->repeat & PC_UI_UP)
            a.category = order[(row + PC_UI_ART_CATEGORIES - 1) % PC_UI_ART_CATEGORIES];
        if (in->pressed & (PC_UI_CONFIRM | PC_UI_RIGHT)) {
            a.mode = PC_UI_ABM_ARTS;
            a.art = 0;
        }
        break;
    case PC_UI_ABM_ARTS:
        if (in->repeat & PC_UI_DOWN)
            a.art = (a.art + 1) % PC_UI_ARTS;
        if (in->repeat & PC_UI_UP)
            a.art = (a.art + PC_UI_ARTS - 1) % PC_UI_ARTS;
        if (in->pressed & PC_UI_LEFT)
            ab_base();
        else if (in->pressed & PC_UI_CONFIRM) {
            // learned: the bridge checks it as MENU1 does (an unusable art buzzes) and closes the menu to cast
            const pc_ui_art& art = ab_cat().arts[a.art];
            if (s.ab_model.valid && art.state == PC_UI_ART_LEARNED)
                push(PC_UI_REQ_ART_CAST, 0, art.id, 0);
        }
        break;
    default:
        ab_base();
        break;
    }
    return 0;
}
void ability_json(std::string& j, const pc_ui_ability& e)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "{\"id\":%d,\"cost\":%d,\"button\":\"%s\",\"name\":\"", e.id, e.cost,
        e.button >= 0 && e.button < PC_UI_ABIL_BUTTONS ? AB_BUTTON_IDS[e.button] : "");
    j += buf;
    j += escape(e.name) + "\",\"help\":\"" + escape(e.help) + "\"}";
}
void abilities_json(std::string& j)
{
    const pc_ui_abilities& m = s.ab_model;
    char buf[256];
    int subs[3], n = ab_subs(subs);
    ab_clamp();
    j += ",\"abilities\":{\"valid\":";
    j += m.valid ? "true" : "false";
    j += ",\"subs\":[";
    for (int i = 0; i < n; ++i)
        j += std::string(i ? ",\"" : "\"") + AB_SUB_IDS[subs[i]] + "\"";
    std::snprintf(buf, sizeof buf,
        "],\"sub\":\"%s\",\"mode\":\"%s\",\"slot\":\"%s\",\"cursor\":%d,\"category\":%d,\"art\":%d,"
        "\"next_ability_kills\":%d,\"equipped_category\":%d,\"weapon_drawn\":%s,\"risk\":%d,\"hp\":%d,\"hp_max\":%d",
        n ? AB_SUB_IDS[s.ab.kind] : "", AB_MODE_IDS[s.ab.mode],
        AB_BUTTON_IDS[s.ab.slot[s.ab.kind == PC_UI_AB_DEFENSE ? 1 : 0]],
        s.ab.cursor[s.ab.kind == PC_UI_AB_DEFENSE ? 1 : 0], ab_category(), s.ab.art, m.next_ability_kills,
        m.equipped_category, m.weapon_drawn ? "true" : "false", m.risk, m.hp, m.hp_max);
    j += buf;
    for (int t = 0; t < 2; ++t) {
        const pc_ui_ability_list& l = m.lists[t];
        int count = l.count < 0 ? 0 : l.count > PC_UI_ABILITIES ? PC_UI_ABILITIES : l.count;
        j += t ? ",\"defense\":{\"slots\":[" : ",\"chain\":{\"slots\":[";
        for (int b = 0; b < PC_UI_ABIL_BUTTONS; ++b) {
            int e = l.slots[b];
            std::snprintf(buf, sizeof buf, "%s{\"button\":\"%s\",\"id\":%d,\"entry\":%d,\"name\":\"", b ? "," : "",
                AB_BUTTON_IDS[b], l.slot_ids[b], e >= 0 && e < count ? e : -1);
            j += buf;
            j += (e >= 0 && e < count ? escape(l.entries[e].name) : std::string()) + "\"}";
        }
        j += "],\"entries\":[";
        for (int i = 0; i < count; ++i) {
            if (i)
                j += ",";
            ability_json(j, l.entries[i]);
        }
        j += "]}";
    }
    int order[PC_UI_ART_CATEGORIES];
    ab_order(order);
    j += ",\"order\":[";
    for (int i = 0; i < PC_UI_ART_CATEGORIES; ++i)
        j += (i ? "," : "") + std::to_string(order[i]);
    j += "],\"categories\":[";
    for (int c = 0; c < PC_UI_ART_CATEGORIES; ++c) {
        const pc_ui_art_category& k = m.categories[c];
        std::snprintf(buf, sizeof buf, "%s{\"category\":%d,\"learned\":%d,\"kills\":%d,\"next_kills\":%d,\"name\":\"",
            c ? "," : "", c + 1, k.learned, k.kills, k.next_kills);
        j += buf;
        j += escape(k.name) + "\",\"help\":\"" + escape(k.help) + "\",\"arts\":[";
        for (int i = 0; i < PC_UI_ARTS; ++i) {
            const pc_ui_art& a = k.arts[i];
            int st = a.state >= 0 && a.state <= PC_UI_ART_LEARNED ? a.state : 0;
            std::snprintf(buf, sizeof buf,
                "%s{\"id\":%d,\"cost\":%d,\"state\":\"%s\",\"kills_to_go\":%d,\"usable\":%s,\"name\":\"", i ? "," : "",
                a.id, a.cost, ART_STATE_IDS[st], a.kills_to_go, a.usable ? "true" : "false");
            j += buf;
            j += escape(a.name) + "\",\"help\":\"" + escape(a.help) + "\",\"reason\":\"" + escape(a.reason) + "\"}";
        }
        j += "]}";
    }
    j += "]}";
}

// ---- Magick ----
const int MAGICK_COMMAND = 0;
const char* const MG_SUB_IDS[PC_UI_SCHOOLS + 1] = {"warlock", "shaman", "sorcerer", "enchanter", "teleport"};
bool on_magick() { return TABS[s.tab].screen == SCREEN_MAGICK; }
// The sub-tabs shown, in order: the schools with a spell learned, then Teleport.
int mg_subs(int* out)
{
    int n = 0;
    if (!s.mg_model.valid)
        return 0;
    for (int k = 0; k < PC_UI_SCHOOLS; ++k)
        if (s.mg_model.schools[k].unlocked)
            out[n++] = k;
    if (s.mg_model.teleport)
        out[n++] = PC_UI_MG_TELEPORT;
    return n;
}
// The current sub-tab's index among the shown ones; puts the kind on a shown one.
int mg_sub_index()
{
    int subs[PC_UI_SCHOOLS + 1], n = mg_subs(subs);
    for (int i = 0; i < n; ++i)
        if (subs[i] == s.mg.kind)
            return i;
    if (n)
        s.mg.kind = subs[0];
    return 0;
}
int mg_rows(int kind)
{
    const pc_ui_magick& m = s.mg_model;
    int n = kind == PC_UI_MG_TELEPORT                ? m.destination_count
        : kind >= 0 && kind < PC_UI_SCHOOLS ? m.schools[kind].count
                                            : 0;
    int max = kind == PC_UI_MG_TELEPORT ? PC_UI_SAVE_POINTS : PC_UI_SCHOOL_SPELLS;
    return n < 0 ? 0 : n > max ? max : n;
}
// The focused spell (null on Teleport or an empty list).
const pc_ui_spell* mg_spell()
{
    int k = s.mg.kind;
    if (k < 0 || k >= PC_UI_SCHOOLS || s.mg.cursor[k] >= mg_rows(k))
        return nullptr;
    return &s.mg_model.schools[k].spells[s.mg.cursor[k]];
}
int mg_levels(const pc_ui_spell* e)
{
    return !e ? 1 : e->levels < 1 ? 1 : e->levels > PC_UI_SPELL_LEVELS ? PC_UI_SPELL_LEVELS : e->levels;
}
// The focused spell's level shown (the kept level, within its levels).
int mg_level()
{
    int n = mg_levels(mg_spell());
    return n > 1 ? (s.mg.level < 0 ? 0 : s.mg.level >= n ? n - 1 : s.mg.level) : 0;
}
void mg_clamp()
{
    mg_sub_index();
    for (int k = 0; k <= PC_UI_SCHOOLS; ++k) {
        int n = mg_rows(k);
        if (s.mg.cursor[k] >= n)
            s.mg.cursor[k] = n ? n - 1 : 0;
        if (s.mg.cursor[k] < 0)
            s.mg.cursor[k] = 0;
    }
    if (s.mg.level < 0 || s.mg.level >= PC_UI_SPELL_LEVELS)
        s.mg.level = 0;
    // a focused levelled spell keeps the level within its own and writes it
    // back, as the original's picker does when it opens (levelledSpellInit)
    int n = mg_levels(mg_spell());
    if (n > 1 && s.mg.level >= n)
        s.mg.level = n - 1;
}
// Teleport's list starts on the circle the player stands on (cost 15), once per open.
void mg_place()
{
    const pc_ui_magick& m = s.mg_model;
    if (s.mg.placed || !m.valid || !m.teleport)
        return;
    s.mg.placed = true;
    for (int i = 0; i < mg_rows(PC_UI_MG_TELEPORT); ++i)
        if (m.destinations[i].current) {
            s.mg.cursor[PC_UI_MG_TELEPORT] = i;
            break;
        }
}
// Circle on the focused row: a cast or teleport request (the bridge checks it).
void mg_confirm()
{
    int k = s.mg.kind;
    if (!s.mg_model.valid || !mg_rows(k))
        return;
    if (k == PC_UI_MG_TELEPORT) {
        push(PC_UI_REQ_TELEPORT, s.mg_model.destinations[s.mg.cursor[k]].point, 0, 0);
        return;
    }
    const pc_ui_spell* e = mg_spell();
    if (e)
        push(PC_UI_REQ_SPELL_CAST, 0, e->level[mg_level()].id, 0);
}
int magick_tick(const pc_ui_input* in)
{
    if (in->pressed & PC_UI_ALT) { // the original Magick screen (command 0 -> result 1)
        s.visible = false; // the game shows its screen; it reopens the shell after
        s.drag_x = s.drag_y = 0;
        return MAGICK_COMMAND + 1;
    }
    int subs[PC_UI_SCHOOLS + 1], n = mg_subs(subs);
    if (!n)
        return 0;
    mg_clamp();
    if (in->pressed & (PC_UI_SUB_NEXT | PC_UI_SUB_PREV)) {
        int i = mg_sub_index(), dir = (in->pressed & PC_UI_SUB_NEXT) ? 1 : -1;
        s.mg.kind = subs[(i + dir + n) % n];
        return 0;
    }
    int k = s.mg.kind, rows = mg_rows(k);
    int& cur = s.mg.cursor[k];
    if (rows && (in->repeat & PC_UI_DOWN))
        cur = (cur + 1) % rows;
    if (rows && (in->repeat & PC_UI_UP))
        cur = (cur + rows - 1) % rows;
    int levels = mg_levels(mg_spell());
    if (levels > 1) { // the original's LV picker: Left/Right, no wrap
        int l = mg_level();
        if ((in->repeat & PC_UI_RIGHT) && l < levels - 1)
            s.mg.level = l + 1;
        if ((in->repeat & PC_UI_LEFT) && l > 0)
            s.mg.level = l - 1;
    }
    if (in->pressed & PC_UI_CONFIRM)
        mg_confirm();
    return 0;
}
void magick_json(std::string& j)
{
    const pc_ui_magick& m = s.mg_model;
    char buf[256];
    int subs[PC_UI_SCHOOLS + 1], n = mg_subs(subs);
    mg_clamp();
    int k = s.mg.kind;
    j += ",\"magick\":{\"valid\":";
    j += m.valid ? "true" : "false";
    j += ",\"subs\":[";
    for (int i = 0; i < n; ++i)
        j += std::string(i ? ",\"" : "\"") + MG_SUB_IDS[subs[i]] + "\"";
    const pc_ui_spell* e = n ? mg_spell() : nullptr;
    std::snprintf(buf, sizeof buf,
        "],\"sub\":\"%s\",\"cursor\":%d,\"rows\":%d,\"level\":%d,\"focus\":%d,\"mp\":%d,\"mp_max\":%d,\"mask\":%u",
        n ? MG_SUB_IDS[k] : "", s.mg.cursor[k], n ? mg_rows(k) : 0, mg_level(),
        !n || !mg_rows(k)        ? -1
            : k == PC_UI_MG_TELEPORT ? m.destinations[s.mg.cursor[k]].point
            : e                      ? e->level[mg_level()].id
                                     : -1,
        m.mp,
        m.mp_max, m.mask);
    j += buf;
    j += ",\"schools\":[";
    for (int c = 0; c < PC_UI_SCHOOLS; ++c) {
        const pc_ui_school& sc = m.schools[c];
        j += std::string(c ? ",{\"id\":\"" : "{\"id\":\"") + MG_SUB_IDS[c] + "\",\"unlocked\":";
        j += sc.unlocked ? "true" : "false";
        j += ",\"name\":\"" + escape(sc.name) + "\",\"help\":\"" + escape(sc.help) + "\",\"spells\":[";
        for (int i = 0; i < mg_rows(c); ++i) {
            const pc_ui_spell& sp = sc.spells[i];
            std::snprintf(buf, sizeof buf, "%s{\"id\":%d,\"mp\":%d,\"levels\":%d,\"usable\":%s,\"name\":\"", i ? "," : "",
                sp.id, sp.mp, mg_levels(&sp), sp.level[0].usable ? "true" : "false");
            j += buf;
            j += escape(sp.name) + "\",\"help\":\"" + escape(sp.help) + "\",\"reason\":\"" + escape(sp.level[0].reason)
                + "\",\"level\":[";
            for (int l = 0; l < mg_levels(&sp); ++l) {
                const pc_ui_spell_level& v = sp.level[l];
                std::snprintf(buf, sizeof buf, "%s{\"id\":%d,\"mp\":%d,\"usable\":%s,\"reason\":\"", l ? "," : "", v.id,
                    v.mp, v.usable ? "true" : "false");
                j += buf;
                j += escape(v.reason) + "\"}";
            }
            j += "]}";
        }
        j += "]}";
    }
    std::snprintf(buf, sizeof buf, "],\"teleport\":{\"exists\":%s,\"enabled\":%s,\"help\":\"",
        m.teleport ? "true" : "false", m.teleport_enabled ? "true" : "false");
    j += buf;
    j += escape(m.teleport_help) + "\",\"reason\":\"" + escape(m.teleport_reason) + "\",\"destinations\":[";
    for (int i = 0; i < mg_rows(PC_UI_MG_TELEPORT); ++i) {
        const pc_ui_destination& d = m.destinations[i];
        std::snprintf(buf, sizeof buf, "%s{\"point\":%d,\"cost\":%d,\"current\":%s,\"usable\":%s,\"name\":\"",
            i ? "," : "", d.point, d.cost, d.current ? "true" : "false", d.usable ? "true" : "false");
        j += buf;
        j += escape(d.name) + "\",\"help\":\"" + escape(d.help) + "\",\"reason\":\"" + escape(d.reason) + "\"}";
    }
    j += "]}}";
}

// ---- Items ----
const int ITEMS_COMMAND = 3;
const RowDef ITEMS_GATE = {"items", "Items", "", ROW_COMMAND, ITEMS_COMMAND};
const char* const IT_CATEGORY_IDS[PC_UI_ITEM_CATEGORIES] = {"weapons", "blades", "grips", "shields", "armor", "gems",
    "misc"};
const char* const IT_MODE_IDS[] = {"list", "actions", "quantity", "confirm", "sort", "move", "details"};
const char* const IT_ACTION_IDS[PC_UI_ITA_COUNT] = {"use", "search", "discard", "discard_all", "sort", "move",
    "original"};
const char* const IT_ACTION_LABELS[PC_UI_ITA_COUNT] = {"Use", "Search", "Discard", "Discard all", "Sort", "Move",
    "Original screen"};
const int IT_STEPS[3] = {1, 10, 100}; // _discardMenu discardSteps
bool on_items() { return TABS[s.tab].screen == SCREEN_ITEMS; }
int it_category()
{
    if (s.it.category < 0 || s.it.category >= PC_UI_ITEM_CATEGORIES)
        s.it.category = 0;
    return s.it.category;
}
int it_rows(int c)
{
    const pc_ui_items& m = s.it_model;
    if (!m.valid || c < 0 || c >= PC_UI_ITEM_CATEGORIES)
        return 0;
    int n = m.count[c], first = m.first[c];
    if (n < 0 || first < 0 || first > PC_UI_ITEM_ROWS)
        return 0;
    return n > PC_UI_ITEM_ROWS - first ? PC_UI_ITEM_ROWS - first : n;
}
const pc_ui_item_row* it_row(int c, int r)
{
    if (r < 0 || r >= it_rows(c))
        return nullptr;
    return &s.it_model.rows[s.it_model.first[c] + r];
}
// Browse only: the snapshot says the mask greys the list (or, before it arrives, the command row does).
bool it_locked() { return s.it_model.valid ? s.it_model.locked != 0 : !row_enabled(ITEMS_GATE); }
const char* it_lock_reason()
{
    if (!it_locked())
        return nullptr;
    return s.it_model.valid && s.it_model.locked_reason[0] ? s.it_model.locked_reason : row_reason(ITEMS_GATE);
}
int it_sorts(int c)
{
    int n = s.it_model.valid && c >= 0 && c < PC_UI_ITEM_CATEGORIES ? s.it_model.sort_count[c] : 0;
    return n < 0 ? 0 : n > PC_UI_SORT_STATS ? PC_UI_SORT_STATS : n;
}
// The focused row's action menu, as the original offers it (populateActions) plus Move and the original screen.
struct ItAction {
    int kind;
    bool enabled;
    const char* reason;
};
int it_actions(ItAction* out)
{
    int c = it_category(), n = 0;
    const pc_ui_item_row* r = it_row(c, s.it.cursor[c]);
    const char* lock = it_lock_reason();
    if (r) {
        if (c == PC_UI_IC_MISC)
            out[n++] = {PC_UI_ITA_USE, r->usable != 0, r->usable ? nullptr : r->use_reason};
        if (c == PC_UI_IC_BLADE || c == PC_UI_IC_GRIP || c == PC_UI_IC_GEM) {
            bool ok = !lock && r->parent >= 0;
            out[n++] = {PC_UI_ITA_SEARCH, ok, ok ? nullptr : lock ? lock : s.it_model.no_search};
        }
        out[n++] = {PC_UI_ITA_DISCARD, r->discard != 0, r->discard ? nullptr : r->discard_reason};
        if (c == PC_UI_IC_MISC)
            out[n++] = {PC_UI_ITA_DISCARD_ALL, r->discard_all != 0, r->discard_all ? nullptr : r->discard_all_reason};
        out[n++] = {PC_UI_ITA_SORT, !lock && it_sorts(c) > 0, lock};
        bool two = it_rows(c) >= 2;
        out[n++] = {PC_UI_ITA_MOVE, !lock && two, lock ? lock : two ? nullptr : "Nothing else here to move it to."};
    }
    out[n++] = {PC_UI_ITA_ORIGINAL, true, nullptr};
    return n;
}
void it_clamp()
{
    It& t = s.it;
    int c = it_category();
    for (int k = 0; k < PC_UI_ITEM_CATEGORIES; ++k) {
        int n = it_rows(k);
        if (t.cursor[k] >= n)
            t.cursor[k] = n ? n - 1 : 0;
        if (t.cursor[k] < 0)
            t.cursor[k] = 0;
    }
    int rows = it_rows(c);
    if (!rows && t.mode != PC_UI_IT_ACTIONS)
        t.mode = PC_UI_IT_LIST; // the list emptied under a sub-mode
    if (t.mode == PC_UI_IT_MOVE && (t.move_from < 0 || t.move_from >= rows))
        t.mode = PC_UI_IT_LIST;
    ItAction a[PC_UI_ITA_COUNT];
    int n = it_actions(a);
    if (t.action >= n || t.action < 0)
        t.action = 0;
    if (t.sort >= it_sorts(c) || t.sort < 0)
        t.sort = 0;
}
// Out of any menu onto the list (a tab switch, a category switch, the shell closing; leave: the player
// leaves, so a Workshop flow left tells the bridge what the original does then; an open only resets).
void it_base(bool leave = true)
{
    s.it.mode = PC_UI_IT_LIST;
    s.it.move_from = -1;
    if (leave)
        pc_ui_ws_leave(); // a flow never survives a tab switch, a category switch or the shell closing
    else
        pc_ui_ws_reset();
}
// The focused row's inventory index as the screen sees it (the bridge refuses a stale request).
int it_inventory()
{
    const pc_ui_item_row* r = it_row(it_category(), s.it.cursor[it_category()]);
    return r ? r->inventory : -1;
}
// The Yes / No's question: _discardMenu's discardConfirm + discardType for a single item (0) or discard all
// (2); after the quantity picker its discardMultipleConfirm asks discardConfirm (0) too (discardConfirm + 1 is
// the picker's own message).
int it_prompt() { return s.it.confirm_kind == PC_UI_ITA_DISCARD_ALL ? 2 : 0; }
// Cross: one step back; false in the list (the shell closes). Divergence: the original's quantity picker and
// Yes / No go back to the list on Cross; here they go back one level (the action menu, the picker).
bool it_back()
{
    It& t = s.it;
    if (t.ws)
        return pc_ui_ws_back() != 0;
    switch (t.mode) {
    case PC_UI_IT_ACTIONS:
    case PC_UI_IT_DETAILS:
        it_base();
        return true;
    case PC_UI_IT_QUANTITY:
    case PC_UI_IT_SORT:
    case PC_UI_IT_MOVE:
        t.mode = PC_UI_IT_ACTIONS;
        t.move_from = -1;
        return true;
    case PC_UI_IT_CONFIRM:
        t.mode = t.confirm_kind == PC_UI_ITA_DISCARD && t.count_max >= 2 ? PC_UI_IT_QUANTITY : PC_UI_IT_ACTIONS;
        return true;
    default:
        return false;
    }
}
// Circle on an action. Returns a result for the game (the original screen), else 0.
int it_do(const ItAction& a)
{
    It& t = s.it;
    int c = it_category(), row = t.cursor[c];
    const pc_ui_item_row* r = it_row(c, row);
    switch (a.kind) {
    case PC_UI_ITA_ORIGINAL:
        s.visible = false; // the game shows its screen; it reopens the shell after
        s.drag_x = s.drag_y = 0;
        it_base();
        return ITEMS_COMMAND + 1;
    case PC_UI_ITA_USE:
        // usable or not, the bridge checks it as MENU3 does and closes the menu to use it; refused, it buzzes
        // and the action menu stays with the reason, as the original's unselectable Use row
        push(PC_UI_REQ_ITEM_USE, c, row, it_inventory());
        return 0;
    case PC_UI_ITA_SEARCH:
        if (!a.enabled || !r)
            return 0;
        for (int k = 0; k < it_rows(r->parent_category); ++k)
            if (it_row(r->parent_category, k)->inventory == r->parent) {
                t.category = r->parent_category;
                t.cursor[t.category] = k;
                break;
            }
        it_base();
        return 0;
    case PC_UI_ITA_DISCARD:
        if (!a.enabled) { // the bridge buzzes
            push(PC_UI_REQ_ITEM_DISCARD, c, row, it_inventory(), 0);
            return 0;
        }
        t.confirm_kind = PC_UI_ITA_DISCARD;
        t.confirm = 1;
        t.count_max = c == PC_UI_IC_MISC && r ? r->count : 1;
        t.count = t.count_max >= 2 ? 0 : 1;
        t.step = 0;
        t.mode = t.count_max >= 2 ? PC_UI_IT_QUANTITY : PC_UI_IT_CONFIRM;
        return 0;
    case PC_UI_ITA_DISCARD_ALL:
        if (!a.enabled) {
            push(PC_UI_REQ_ITEM_DISCARD_ALL, c, row, it_inventory());
            return 0;
        }
        t.confirm_kind = PC_UI_ITA_DISCARD_ALL;
        t.confirm = 1;
        t.count_max = t.count = r ? r->count : 0;
        t.mode = PC_UI_IT_CONFIRM;
        return 0;
    case PC_UI_ITA_SORT:
        if (a.enabled) {
            t.mode = PC_UI_IT_SORT;
            t.sort = 0;
        }
        return 0;
    case PC_UI_ITA_MOVE:
        if (a.enabled) {
            t.mode = PC_UI_IT_MOVE;
            t.move_from = row;
        }
        return 0;
    default:
        return 0;
    }
}
// _discardMenu determineDiscardAmount's keys (items1.h): Up/Down by the step when it stays in 0..max;
// Left a bigger step (past 100: all), Right a smaller one (past 1: none).
void it_quantity(const pc_ui_input* in)
{
    It& t = s.it;
    int dir = ((in->repeat & PC_UI_UP) ? 1 : 0) - ((in->repeat & PC_UI_DOWN) ? 1 : 0);
    int next = t.count + dir * IT_STEPS[t.step];
    if (dir && next >= 0 && next <= t.count_max)
        t.count = next;
    dir = ((in->repeat & PC_UI_LEFT) ? 1 : 0) - ((in->repeat & PC_UI_RIGHT) ? 1 : 0);
    if (dir == 1) {
        if (t.step == 2)
            t.count = t.count_max;
        else
            ++t.step;
    } else if (dir == -1) {
        if (t.step == 0)
            t.count = 0;
        else
            --t.step;
    }
}
int items_tick(const pc_ui_input* in)
{
    It& t = s.it;
    it_clamp();
    int c = it_category(), rows = it_rows(c);
    // L2 / R2: the categories and the Workshop after misc (wrapping); inside a Workshop flow they are its own
    if ((in->pressed & (PC_UI_SUB_NEXT | PC_UI_SUB_PREV)) && (!t.ws || pc_ui_ws_base())) {
        int dir = (in->pressed & PC_UI_SUB_NEXT) ? 1 : -1;
        int k = (t.ws ? PC_UI_ITEM_CATEGORIES : c) + dir;
        k = (k + PC_UI_ITEM_CATEGORIES + 1) % (PC_UI_ITEM_CATEGORIES + 1);
        t.ws = k == PC_UI_ITEM_CATEGORIES;
        if (!t.ws)
            t.category = k;
        it_base();
        return 0;
    }
    if (t.ws) {
        pc_ui_ws_tick(in);
        return 0;
    }
    int& cur = t.cursor[c];
    ItAction acts[PC_UI_ITA_COUNT];
    int n = it_actions(acts);
    switch (t.mode) {
    case PC_UI_IT_LIST:
    case PC_UI_IT_DETAILS:
        if (rows && (in->repeat & PC_UI_DOWN))
            cur = (cur + 1) % rows;
        if (rows && (in->repeat & PC_UI_UP))
            cur = (cur + rows - 1) % rows;
        if (in->pressed & PC_UI_ALT) {
            t.mode = t.mode == PC_UI_IT_DETAILS || !rows ? PC_UI_IT_LIST : PC_UI_IT_DETAILS;
            return 0;
        }
        if (in->pressed & PC_UI_CONFIRM) {
            t.mode = PC_UI_IT_ACTIONS;
            t.action = 0;
        }
        return 0;
    case PC_UI_IT_ACTIONS:
        if (in->repeat & PC_UI_DOWN)
            t.action = (t.action + 1) % n;
        if (in->repeat & PC_UI_UP)
            t.action = (t.action + n - 1) % n;
        if (in->pressed & PC_UI_CONFIRM)
            return it_do(acts[t.action]);
        return 0;
    case PC_UI_IT_QUANTITY:
        if (in->pressed & PC_UI_CONFIRM) {
            if (t.count == 0) { // none: the original leaves the picker (back to the list, the leave sound)
                push(PC_UI_REQ_ITEM_CANCEL, c, cur, it_inventory(), 0);
                it_base();
            } else {
                t.mode = PC_UI_IT_CONFIRM;
                t.confirm = 1;
            }
            return 0;
        }
        it_quantity(in);
        return 0;
    case PC_UI_IT_CONFIRM:
        if (in->repeat & (PC_UI_DOWN | PC_UI_UP))
            t.confirm = !t.confirm;
        if (in->pressed & PC_UI_CONFIRM) {
            if (t.confirm == 0) {
                if (t.confirm_kind == PC_UI_ITA_DISCARD_ALL)
                    push(PC_UI_REQ_ITEM_DISCARD_ALL, c, cur, it_inventory());
                else
                    push(PC_UI_REQ_ITEM_DISCARD, c, cur, it_inventory(), t.count);
            } else { // No: the leave sound (the bridge), back to the list as the original
                push(PC_UI_REQ_ITEM_CANCEL, c, cur, it_inventory(), t.confirm_kind == PC_UI_ITA_DISCARD_ALL ? 2 : 1);
            }
            it_base();
        }
        return 0;
    case PC_UI_IT_SORT: {
        int k = it_sorts(c);
        if (k && (in->repeat & PC_UI_DOWN))
            t.sort = (t.sort + 1) % k;
        if (k && (in->repeat & PC_UI_UP))
            t.sort = (t.sort + k - 1) % k;
        if ((in->pressed & PC_UI_CONFIRM) && k) {
            push(PC_UI_REQ_ITEM_SORT, c, cur, it_inventory(), s.it_model.sort_stats[c][t.sort]);
            it_base();
        }
        return 0;
    }
    case PC_UI_IT_MOVE:
        if (rows && (in->repeat & PC_UI_DOWN))
            cur = (cur + 1) % rows;
        if (rows && (in->repeat & PC_UI_UP))
            cur = (cur + rows - 1) % rows;
        if (in->pressed & PC_UI_CONFIRM) {
            const pc_ui_item_row* from = it_row(c, t.move_from);
            if (cur != t.move_from && from)
                push(PC_UI_REQ_ITEM_MOVE, c, t.move_from, cur, from->inventory);
            it_base(); // the cursor stays on the target row, as the original's list does
        }
        return 0;
    default:
        it_base();
        return 0;
    }
}
void items_json(std::string& j)
{
    const pc_ui_items& m = s.it_model;
    const It& t = s.it;
    char buf[320];
    it_clamp();
    int c = it_category(), rows = it_rows(c);
    if (t.ws) { // the Workshop: its own members
        static char ws[1 << 17];
        std::snprintf(buf, sizeof buf, ",\"items\":{\"valid\":%s,\"locked\":%s,\"category\":\"workshop\"",
            m.valid ? "true" : "false", it_locked() ? "true" : "false");
        j += buf;
        pc_ui_ws_json(ws, sizeof ws);
        j += ws;
        j += "}";
        return;
    }
    std::snprintf(buf, sizeof buf,
        ",\"items\":{\"valid\":%s,\"locked\":%s,\"category\":\"%s\",\"mode\":\"%s\",\"cursor\":%d,\"rows\":%d,"
        "\"capacity\":%d,\"inventory\":%d,\"mask\":%u",
        m.valid ? "true" : "false", it_locked() ? "true" : "false", IT_CATEGORY_IDS[c], IT_MODE_IDS[t.mode],
        t.cursor[c], rows, m.valid ? m.capacity[c] : 0, it_inventory(), m.mask);
    j += buf;
    if (const char* why = it_lock_reason())
        j += ",\"reason\":\"" + escape(why) + "\"";
    j += ",\"categories\":[";
    for (int k = 0; k < PC_UI_ITEM_CATEGORIES; ++k) {
        std::snprintf(buf, sizeof buf, "%s{\"id\":\"%s\",\"count\":%d,\"capacity\":%d}", k ? "," : "",
            IT_CATEGORY_IDS[k], it_rows(k), m.valid ? m.capacity[k] : 0);
        j += buf;
    }
    j += "],\"list\":[";
    for (int r = 0; r < rows; ++r) {
        const pc_ui_item_row& e = *it_row(c, r);
        std::snprintf(buf, sizeof buf,
            "%s{\"row\":%d,\"inventory\":%d,\"id\":%d,\"count\":%d,\"mark\":%d,\"material\":%d,\"parent_category\":%d,"
            "\"parent\":%d,\"discard\":%s,\"discard_all\":%s,\"usable\":%s,\"action\":%d,\"name\":\"",
            r ? "," : "", r, e.inventory, e.id, e.count, e.mark, e.material, e.parent_category, e.parent,
            e.discard ? "true" : "false", e.discard_all ? "true" : "false", e.usable ? "true" : "false", e.action);
        j += buf;
        j += escape(e.name) + "\",\"sub\":\"" + escape(e.sub) + "\",\"help\":\"" + escape(e.help) +
            "\",\"discard_reason\":\"" + escape(e.discard_reason) + "\",\"discard_all_reason\":\"" +
            escape(e.discard_all_reason) + "\",\"use_reason\":\"" + escape(e.use_reason) + "\"}";
    }
    j += "]";
    if (t.mode != PC_UI_IT_LIST && t.mode != PC_UI_IT_DETAILS) {
        ItAction a[PC_UI_ITA_COUNT];
        int n = it_actions(a);
        std::snprintf(buf, sizeof buf, ",\"action\":%d,\"actions\":[", t.action);
        j += buf;
        for (int i = 0; i < n; ++i) {
            j += std::string(i ? "," : "") + "{\"id\":\"" + IT_ACTION_IDS[a[i].kind] + "\",\"enabled\":" +
                (a[i].enabled ? "true" : "false");
            if (a[i].reason)
                j += ",\"reason\":\"" + escape(a[i].reason) + "\"";
            j += "}";
        }
        j += "]";
    }
    if (t.mode == PC_UI_IT_QUANTITY || t.mode == PC_UI_IT_CONFIRM) {
        std::snprintf(buf, sizeof buf, ",\"quantity\":{\"count\":%d,\"max\":%d,\"step\":%d}", t.count, t.count_max,
            IT_STEPS[t.step]);
        j += buf;
    }
    if (t.mode == PC_UI_IT_CONFIRM) {
        std::snprintf(buf, sizeof buf, ",\"confirm\":{\"kind\":\"%s\",\"yes\":%s,\"prompt\":\"",
            IT_ACTION_IDS[t.confirm_kind], t.confirm == 0 ? "true" : "false");
        j += buf;
        j += escape(m.discard_prompt[it_prompt()]) + "\"}";
    }
    if (t.mode == PC_UI_IT_SORT) {
        std::snprintf(buf, sizeof buf, ",\"sort\":{\"cursor\":%d,\"stats\":[", t.sort);
        j += buf;
        for (int i = 0; i < it_sorts(c); ++i) {
            int st = m.sort_stats[c][i];
            std::snprintf(buf, sizeof buf, "%s{\"stat\":%d,\"name\":\"", i ? "," : "", st);
            j += buf;
            j += (st < PC_UI_STAT_NAMES ? escape(m.stat_name[st]) : std::string()) + "\"}";
        }
        j += "]}";
    }
    if (t.mode == PC_UI_IT_MOVE) {
        std::snprintf(buf, sizeof buf, ",\"move_from\":%d", t.move_from);
        j += buf;
    }
    const pc_ui_item_details& d = m.details;
    if (d.valid && rows && d.category == c && d.inventory == it_inventory()) {
        std::snprintf(buf, sizeof buf,
            ",\"details\":{\"inventory\":%d,\"dp\":%d,\"dp_max\":%d,\"pp\":%d,\"pp_max\":%d,\"str\":%d,\"int\":%d,"
            "\"agl\":%d,\"types\":[%d,%d,%d],\"range\":%d,\"risk\":%d,\"blade_category\":%d,\"grip_category\":%d,"
            "\"armor_category\":%d,\"gem_slots\":%d,\"gems\":%d",
            d.inventory, d.stats.dp, d.stats.dp_max, d.stats.pp, d.stats.pp_max, d.stats.str, d.stats.intel,
            d.stats.agl, d.types[0], d.types[1], d.types[2], d.range, d.risk, d.blade_category, d.grip_category,
            d.armor_category, d.gem_slots, d.gem_count);
        j += buf;
        j += ",\"name\":\"" + escape(d.stats.name) + "\",\"material\":\"" + escape(d.stats.material) + "\"}";
    }
    j += "}";
}

// ---- Mouse ----
// Focus what the pointer is on (hover, or the click's target). False when that
// target is not on screen now (another mode or tab, past the end).
bool mouse_focus(int kind, int index)
{
    bool equipment = TABS[s.tab].screen == SCREEN_EQUIPMENT;
    Equip& e = s.eq;
    switch (kind) {
    case PC_UI_MOUSE_ROW: {
        if (TABS[s.tab].screen == SCREEN_ASHLEY) {
            if (index < 0 || index >= ash_rows())
                return false;
            s.ash_row = index;
            s.ash_limb = -1; // (the Condition panel as a whole)
            return true;
        }
        if (equipment || index < 0 || index >= clamp_row(s.tab))
            return false;
        s.row[s.tab] = index;
        return true;
    }
    case PC_UI_MOUSE_LIMB:
        if (TABS[s.tab].screen != SCREEN_ASHLEY || index < 0 || index >= ash_limbs())
            return false;
        s.ash_row = PC_UI_ASH_CONDITION;
        s.ash_limb = index;
        return true;
    case PC_UI_MOUSE_SLOT:
        if (!equipment || e.mode != M_GEAR || index < 0 || index >= PC_UI_EQUIP_SLOTS)
            return false;
        e.slot = index;
        return true;
    case PC_UI_MOUSE_QUICK:
        if (!equipment || e.mode != M_QUICK || index < 0 || index >= PC_UI_QUICK)
            return false;
        e.quick = index;
        return true;
    case PC_UI_MOUSE_LIST:
        if (!equipment || !picking() || index < 0 || index >= list_rows())
            return false;
        e.cursor = index;
        return true;
    case PC_UI_MOUSE_PAGE: // hover moves the list's focus only while it has it
        if (!on_system() || s.sys.page >= 0 || index < 0 || index >= sys_items())
            return false;
        s.sys.item = index;
        return true;
    case PC_UI_MOUSE_SETTING: {
        int page = s.sys.page;
        if (!on_system() || page < 0 || index < 0 || index >= pc_ui_settings_rows(page))
            return false;
        sys_row(page) = index;
        return true;
    }
    case PC_UI_MOUSE_ABIL_SLOT: // hover moves the slots' focus while they have it
        if (!on_abilities() || !ab_chain_kind() || s.ab.mode != PC_UI_ABM_SLOTS || index < 0
            || index >= PC_UI_ABIL_BUTTONS)
            return false;
        s.ab.slot[s.ab.kind == PC_UI_AB_DEFENSE] = index;
        return true;
    case PC_UI_MOUSE_ABIL_ROW: // and the list's while it has it (browse or pick)
        if (!on_abilities() || !ab_chain_kind() || (s.ab.mode != PC_UI_ABM_LIST && s.ab.mode != PC_UI_ABM_PICK)
            || index < 0 || index >= ab_count())
            return false;
        s.ab.cursor[s.ab.kind == PC_UI_AB_DEFENSE] = index;
        return true;
    case PC_UI_MOUSE_ART_CATEGORY:
        if (!on_abilities() || s.ab.kind != PC_UI_AB_ARTS || s.ab.mode != PC_UI_ABM_CATEGORIES || index < 1
            || index > PC_UI_ART_CATEGORIES)
            return false;
        s.ab.category = index;
        return true;
    case PC_UI_MOUSE_ART:
        if (!on_abilities() || s.ab.kind != PC_UI_AB_ARTS || s.ab.mode != PC_UI_ABM_ARTS || index < 0
            || index >= PC_UI_ARTS)
            return false;
        s.ab.art = index;
        return true;
    case PC_UI_MOUSE_SPELL: {
        int subs[PC_UI_SCHOOLS + 1];
        if (!on_magick() || !mg_subs(subs) || index < 0 || index >= mg_rows(s.mg.kind))
            return false;
        s.mg.cursor[s.mg.kind] = index;
        return true;
    }
    case PC_UI_MOUSE_WS_ROW:
    case PC_UI_MOUSE_WS_ENTRY:
    case PC_UI_MOUSE_WS_SLOT:
    case PC_UI_MOUSE_WS_GRID:
    case PC_UI_MOUSE_WS_CONFIRM:
        return on_items() && s.it.ws && pc_ui_ws_mouse_focus(kind, index);
    case PC_UI_MOUSE_MANUAL_TOPIC:
    case PC_UI_MOUSE_MANUAL_CANVAS:
    case PC_UI_MOUSE_MANUAL_PAGE:
    case PC_UI_MOUSE_MANUAL_SCROLL:
        return on_system() && pc_ui_manual_mouse_focus(kind, index);
    case PC_UI_MOUSE_SCORE_SECTION:
    case PC_UI_MOUSE_SCORE_ROW:
    case PC_UI_MOUSE_SCORE_PANEL:
        return on_system() && pc_ui_score_mouse_focus(kind, index); // hovering writes nothing
    case PC_UI_MOUSE_ITEM: // where the list has the focus (the list, details, a move's target)
        if (!on_items() || s.it.ws || index < 0 || index >= it_rows(it_category())
            || (s.it.mode != PC_UI_IT_LIST && s.it.mode != PC_UI_IT_DETAILS && s.it.mode != PC_UI_IT_MOVE))
            return false;
        s.it.cursor[it_category()] = index;
        return true;
    case PC_UI_MOUSE_ITEM_MENU: {
        if (!on_items() || s.it.ws || index < 0)
            return false;
        It& t = s.it;
        if (t.mode == PC_UI_IT_ACTIONS) {
            ItAction a[PC_UI_ITA_COUNT];
            if (index >= it_actions(a))
                return false;
            t.action = index;
        } else if (t.mode == PC_UI_IT_CONFIRM) {
            if (index > 1)
                return false;
            t.confirm = index;
        } else if (t.mode == PC_UI_IT_SORT) {
            if (index >= it_sorts(it_category()))
                return false;
            t.sort = index;
        } else {
            return false;
        }
        return true;
    }
    default:
        return false;
    }
}
// A click on an Abilities target outside the zone that has the focus moves
// the focus there (and onto the target) without confirming; true when it did.
bool ab_click_zone(int kind, int index)
{
    if (!on_abilities())
        return false;
    Abil& a = s.ab;
    int t = a.kind == PC_UI_AB_DEFENSE;
    switch (kind) {
    case PC_UI_MOUSE_ABIL_SLOT:
        if (!ab_chain_kind() || a.mode == PC_UI_ABM_SLOTS || index < 0 || index >= PC_UI_ABIL_BUTTONS)
            return false;
        a.mode = PC_UI_ABM_SLOTS;
        a.slot[t] = index;
        return true;
    case PC_UI_MOUSE_ABIL_ROW:
        if (!ab_chain_kind() || a.mode != PC_UI_ABM_SLOTS || index < 0 || index >= ab_count())
            return false;
        a.mode = PC_UI_ABM_LIST;
        a.cursor[t] = index;
        return true;
    case PC_UI_MOUSE_ART_CATEGORY: // another category while the arts have the focus: its arts
        if (a.kind != PC_UI_AB_ARTS || a.mode != PC_UI_ABM_ARTS || index < 1 || index > PC_UI_ART_CATEGORIES)
            return false;
        if (index != a.category)
            a.category = index, a.art = 0;
        return true;
    case PC_UI_MOUSE_ART:
        if (a.kind != PC_UI_AB_ARTS || a.mode != PC_UI_ABM_CATEGORIES || index < 0 || index >= PC_UI_ARTS)
            return false;
        a.mode = PC_UI_ABM_ARTS;
        a.art = index;
        return true;
    default:
        return false;
    }
}
// Items: a click on a list row while a menu is open goes back to the list on that row (no confirm); true when
// it did.
bool it_click_zone(int kind, int index)
{
    if (kind >= PC_UI_MOUSE_WS_ROW && kind <= PC_UI_MOUSE_WS_CONFIRM) // the Workshop's: a click that does it all
        return on_items() && s.it.ws && pc_ui_ws_mouse_click(kind, index);
    if (kind != PC_UI_MOUSE_ITEM || !on_items() || s.it.ws || s.it.mode == PC_UI_IT_LIST || s.it.mode == PC_UI_IT_DETAILS
        || s.it.mode == PC_UI_IT_MOVE || index < 0 || index >= it_rows(it_category()))
        return false;
    it_base();
    s.it.cursor[it_category()] = index;
    return true;
}
// A click on a level of the focused levelled spell picks it (no cast); true when it did.
bool mg_click_level(int kind, int index)
{
    if (kind != PC_UI_MOUSE_SPELL_LEVEL || !on_magick())
        return false;
    int subs[PC_UI_SCHOOLS + 1];
    int levels = mg_subs(subs) ? mg_levels(mg_spell()) : 1;
    if (levels > 1 && index >= 0 && index < levels)
        s.mg.level = index;
    return true;
}
// Fold the pointer since the last tick into this tick's input. True when a
// click switched a tab or sub-tab (that is the tick's whole action).
bool apply_mouse(pc_ui_input& in)
{
    Mouse m = s.mouse;
    s.mouse.hover_kind = s.mouse.click_kind = PC_UI_MOUSE_NONE;
    s.mouse.value = false;
    if (confirm_up()) { // only the modal's buttons take the pointer
        s.mouse.wheel = 0;
        if (m.hover_kind == PC_UI_MOUSE_CONFIRM && m.hover_index < 2)
            s.sys.confirm_focus = m.hover_index;
        if (m.click_kind == PC_UI_MOUSE_CONFIRM && m.click_index < 2) {
            confirm_answer(m.click_index == 0);
            return true;
        }
        return false;
    }
    if (quit_up()) { // the same: only Yes and No take the pointer
        s.mouse.wheel = 0;
        if (m.hover_kind == PC_UI_MOUSE_CONFIRM && m.hover_index < 2)
            s.sys.quit_focus = m.hover_index;
        if (m.click_kind == PC_UI_MOUSE_CONFIRM && m.click_index < 2) {
            quit_answer(m.click_index == 0);
            return true;
        }
        return false;
    }
    if (capturing()) { // a capture listens: only a right click (Back) or a click on the prompt cancels it
        s.mouse.wheel = 0;
        if (m.click_kind == PC_UI_MOUSE_BUTTON && ((unsigned)m.click_index & PC_UI_BACK))
            pc_ui_settings_capture_done(-1, 0);
        return true;
    }
    if (m.value && on_system()) { // a value under the pointer: the tick's whole action
        int page = sys_shown_page();
        if (page < 0 || m.value_row < 0 || m.value_row >= pc_ui_settings_rows(page))
            return true;
        if (s.sys.page < 0)
            sys_enter(page);
        sys_row(page) = m.value_row;
        if (m.value_set && m.value_preview)
            pc_ui_settings_preview(page, m.value_row, m.value_number);
        else if (m.value_set)
            pc_ui_settings_set(page, m.value_row, m.value_number);
        else if (m.value_delta)
            pc_ui_settings_change(page, m.value_row, m.value_delta);
        sys_clamp();
        s.mouse.wheel = 0;
        return true;
    }
    // one list step per tick; what is left (fast wheels, trackpad fractions) waits
    if (s.mouse.wheel >= 1)
        in.repeat |= PC_UI_UP, s.mouse.wheel -= 1;
    else if (s.mouse.wheel <= -1)
        in.repeat |= PC_UI_DOWN, s.mouse.wheel += 1;
    if (m.hover_kind != PC_UI_MOUSE_NONE)
        mouse_focus(m.hover_kind, m.hover_index);
    switch (m.click_kind) {
    case PC_UI_MOUSE_NONE:
        return false;
    case PC_UI_MOUSE_TAB:
        if (m.click_index < 0 || m.click_index >= TAB_COUNT || m.click_index == s.tab || !tab_has_rows(m.click_index))
            return false;
        sys_leave();
        s.tab = m.click_index;
        equip_base();
        ab_unpick();
        it_base();
        s.drag_x = s.drag_y = 0;
        return true;
    case PC_UI_MOUSE_SUB:
        if (on_items()) { // a category, or the Workshop (index PC_UI_ITEM_CATEGORIES)
            if (m.click_index < 0 || m.click_index > PC_UI_ITEM_CATEGORIES)
                return false;
            bool ws = m.click_index == PC_UI_ITEM_CATEGORIES;
            if (ws ? s.it.ws && pc_ui_ws_base()
                   : !s.it.ws && m.click_index == s.it.category && s.it.mode == PC_UI_IT_LIST)
                return false;
            s.it.ws = ws;
            if (!ws)
                s.it.category = m.click_index;
            it_base();
            return true;
        }
        if (on_magick()) { // the i-th shown sub-tab
            int subs[PC_UI_SCHOOLS + 1], n = mg_subs(subs);
            if (m.click_index < 0 || m.click_index >= n || subs[m.click_index] == s.mg.kind)
                return false;
            s.mg.kind = subs[m.click_index];
            return true;
        }
        if (on_abilities()) { // the i-th shown sub-tab
            int subs[3], n = ab_subs(subs);
            if (m.click_index < 0 || m.click_index >= n || (subs[m.click_index] == s.ab.kind
                    && s.ab.mode == ab_base_mode(s.ab.kind)))
                return false;
            s.ab.kind = subs[m.click_index];
            ab_base();
            return true;
        }
        if (TABS[s.tab].screen != SCREEN_EQUIPMENT || (m.click_index != 0 && m.click_index != 1))
            return false;
        if (m.click_index == s.eq.sub && !picking())
            return false;
        s.eq.sub = m.click_index;
        equip_base(); // the sub-tab itself: out of its pick list too
        return true;
    case PC_UI_MOUSE_BUTTON: {
        unsigned bits = (unsigned)m.click_index & 0xFFFu;
        in.pressed |= bits;
        in.repeat |= bits;
        return false;
    }
    case PC_UI_MOUSE_PAGE: { // another entry while a page has the focus: out of it, then as Circle there
        if (!on_system() || m.click_index < 0 || m.click_index >= sys_items())
            return false;
        if (s.sys.page >= 0) {
            if (sys_item_page(m.click_index) == s.sys.page)
                return false; // the page already entered
            sys_leave();
        }
        s.sys.item = m.click_index;
        in.pressed |= PC_UI_CONFIRM, in.repeat |= PC_UI_CONFIRM;
        return false;
    }
    case PC_UI_MOUSE_SETTING: // a row of the page shown: enters it (the list had the focus) or confirms it
        if (!on_system() || s.sys.page < 0) {
            int page = on_system() ? sys_shown_page() : -1;
            if (page >= 0 && m.click_index >= 0 && m.click_index < pc_ui_settings_rows(page)) {
                sys_enter(page);
                sys_row(page) = m.click_index;
            }
            return true;
        }
        if (mouse_focus(m.click_kind, m.click_index))
            in.pressed |= PC_UI_CONFIRM, in.repeat |= PC_UI_CONFIRM;
        return false;
    case PC_UI_MOUSE_MANUAL_TOPIC: // the Quick manual's: a click that does it all (or nothing)
    case PC_UI_MOUSE_MANUAL_CANVAS:
    case PC_UI_MOUSE_MANUAL_PAGE:
    case PC_UI_MOUSE_MANUAL_SCROLL:
        if (on_system())
            pc_ui_manual_mouse_click(m.click_kind, m.click_index);
        return true;
    case PC_UI_MOUSE_SCORE_SECTION: // the Score screen's: a click that does it all (or nothing)
    case PC_UI_MOUSE_SCORE_ROW:
    case PC_UI_MOUSE_SCORE_PANEL:
        if (on_system())
            pc_ui_score_mouse_click(m.click_kind, m.click_index);
        return true;
    default:
        if (ab_click_zone(m.click_kind, m.click_index) || mg_click_level(m.click_kind, m.click_index)
            || it_click_zone(m.click_kind, m.click_index))
            return true;
        if (mouse_focus(m.click_kind, m.click_index))
            in.pressed |= PC_UI_CONFIRM, in.repeat |= PC_UI_CONFIRM;
        return false;
    }
}

} // namespace

extern "C" void pc_ui_shell_mouse(int kind, int index, int clicked, float wheel)
{
    if (!s.visible)
        return;
    if (kind != PC_UI_MOUSE_NONE) {
        if (clicked)
            s.mouse.click_kind = kind, s.mouse.click_index = index;
        else
            s.mouse.hover_kind = kind, s.mouse.hover_index = index;
    }
    if (wheel == wheel && !capturing()) { // not NaN
        s.mouse.wheel += wheel;
        if (s.mouse.wheel > 8)
            s.mouse.wheel = 8;
        if (s.mouse.wheel < -8)
            s.mouse.wheel = -8;
    }
}

extern "C" void pc_ui_shell_set_holding(int holding) { s.holding = holding != 0; }
extern "C" int pc_ui_shell_holding(void) { return s.holding; }
extern "C" int pc_ui_shell_drawn(void) { return s.visible || s.holding; }

extern "C" void pc_ui_shell_equip_view(pc_ui_equip_view* v)
{
    v->sub = s.eq.sub;
    v->mode = s.eq.mode; // EquipMode is in PC_UI_EQ_* order
    v->slot = s.eq.slot;
    v->quick = s.eq.quick;
    v->cursor = s.eq.cursor;
    v->rows = list_rows();
    v->loading = picking() && !list_ready();
    v->locked = equip_locked();
    v->reason = equip_locked() ? row_reason(EQUIP_GATE) : nullptr;
    v->pending = s.eq.mode == M_QUICK_SHIELD && s.eq.has_pending ? &s.eq.pending_item : nullptr;
}
extern "C" const pc_ui_equipment* pc_ui_shell_equipment(void) { return &s.model; }
extern "C" const pc_ui_item* pc_ui_shell_list_item(int r)
{
    if (r < 0 || r >= list_rows())
        return nullptr;
    return list_item(r);
}

extern "C" void pc_ui_shell_open(const pc_ui_commands* commands, int remember)
{
    s.commands = *commands;
    s.visible = true;
    s.holding = false;
    s.mouse = Mouse();
    s.drag_x = s.drag_y = 0;
    s.ash_shown = -1; // until the Ashley screen is drawn again
    pc_ui_manual_leave(); // a manual left open by a session that ended without its close (nothing when off)
    pc_ui_score_leave();  // likewise the Score screen
    s.sys.page = -1; // the System screen opens on its list
    s.sys.page_id.clear();
    if (!remember) {
        s.tab = 0;
        std::memset(s.row, 0, sizeof s.row);
        s.eq = Equip();
        s.ash_row = 0;
        s.ash_limb = -1;
        s.sys.item = 0;
        s.sys.rows.clear();
        s.ab = Abil();
        s.mg = Magic();
        s.it = It();
    }
    s.mg.placed = false; // Teleport starts on the current circle at every open (the original's list does)
    s.mg.seeded = false; // the level is the game's LV picker memory at every open (it may have moved it)
    equip_base(); // a pick list never survives the shell closing
    ab_unpick();
    it_base(false); // nor an Items menu (a cancelled Use comes back on the item's row)
    for (int t = 0; t < TAB_COUNT; ++t)
        clamp_row(t);
    if (!tab_has_rows(s.tab))
        s.tab = step_tab(s.tab, 1);
}

extern "C" void pc_ui_shell_close(void)
{
    // the bridge's close and abandon paths (menu_bridge.c abandon, pc_ui_menu_reset) come here; a
    // shell that is not up has no page open (a capture the RPC started stays)
    if (s.visible || s.holding)
        sys_leave();
    if (pc_quit_cancel) // (a question left open)
        pc_quit_cancel();
    s.sys.quit_seen = false;
    s.visible = false;
    s.drag_x = s.drag_y = 0;
    s.holding = false;
}

extern "C" int pc_ui_shell_visible(void) { return s.visible; }
extern "C" int pc_ui_shell_face_buttons(void) { return s.visible && ab_takes_triangle(); }

extern "C" int pc_ui_shell_tick(const pc_ui_input* input)
{
    if (!s.visible)
        return -1;
    pc_ui_input mixed = *input;
    const pc_ui_input* in = &mixed;
    if (apply_mouse(mixed))
        return 0;
    if (capturing()) // a binding capture listens: no navigation until it ends
        return 0;
    if (confirm_up()) // "Keep these settings?" waits for its answer
        return confirm_tick(in);
    if (quit_up()) // so does "Unsaved progress will be lost"
        return quit_tick(in);
    bool equipment = TABS[s.tab].screen == SCREEN_EQUIPMENT;
    // Cross steps back out of a pick list; at the top it closes, as Triangle does
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && equipment && equip_back())
        return 0;
    // Abilities: Cross steps back out of a list; in the learned list Triangle is a button to map
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && on_abilities() && ab_back())
        return 0;
    if (ab_takes_triangle() && (in->pressed & PC_UI_EXIT) && !(in->pressed & PC_UI_BACK))
        return abilities_tick(in);
    // Items: Cross steps back out of a menu or the details (the Workshop: out of its flow)
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && on_items() && it_back())
        return 0;
    // the Workshop's name field: L1 / R1 move its cursor (no tab switch)
    if (on_items() && s.it.ws && pc_ui_ws_takes_tabs() && !(in->pressed & (PC_UI_BACK | PC_UI_EXIT)))
        return items_tick(in);
    // the Quick manual: Cross steps back (reading -> topics -> the System list); Triangle closes, the
    // page's step of that frame first (the original's order)
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && on_system() && pc_ui_manual_mode()) {
        pc_ui_manual_back(in);
        return 0;
    }
    if ((in->pressed & PC_UI_EXIT) && on_system())
        pc_ui_manual_exit_step(in);
    // the Score screen: Cross steps back (a section -> Home -> the System list); Triangle closes (a
    // section's sound first); everything else is its own (L1 / R1 switch its sections, not the tabs)
    if (on_system() && pc_ui_score_mode()) {
        if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT)) {
            pc_ui_score_back(in);
            return 0;
        }
        if (in->pressed & PC_UI_EXIT)
            pc_ui_score_exit_step(in);
        else
            return system_tick(in);
    }
    // Cross steps back out of a settings page to the System list
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && on_system() && s.sys.page >= 0) {
        sys_leave();
        return 0;
    }
    if (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) {
        sys_leave();
        if (on_items() && s.it.ws)
            pc_ui_ws_leave(); // Triangle out of a Workshop flow (the bridge drains after this tick)
        s.visible = false;
        s.drag_x = s.drag_y = 0;
        return -1;
    }
    // tabs without rows (no command unlocked yet, e.g. Magick early on) are skipped
    int was = s.tab;
    if (in->pressed & PC_UI_TAB_NEXT)
        s.tab = step_tab(s.tab, 1);
    if (in->pressed & PC_UI_TAB_PREV)
        s.tab = step_tab(s.tab, -1);
    if (s.tab != was) {
        sys_leave();  // out of a settings page: saved
        equip_base(); // leaving (or entering) the Equipment tab drops its pick list
        ab_unpick();
        it_base();
        s.drag_x = s.drag_y = 0;
        return 0;
    }
    if (TABS[s.tab].screen == SCREEN_ASHLEY)
        return ashley_tick(in);
    if (TABS[s.tab].screen == SCREEN_EQUIPMENT) {
        equip_tick(in);
        return 0;
    }
    if (TABS[s.tab].screen == SCREEN_SYSTEM)
        return system_tick(in);
    if (on_abilities())
        return abilities_tick(in);
    if (on_magick())
        return magick_tick(in);
    if (on_items())
        return items_tick(in);
    int rows[6];
    int n = clamp_row(s.tab);
    shown_rows(s.tab, rows);
    if (n == 0)
        return 0;
    if (in->repeat & PC_UI_DOWN)
        s.row[s.tab] = (s.row[s.tab] + 1) % n;
    if (in->repeat & PC_UI_UP)
        s.row[s.tab] = (s.row[s.tab] + n - 1) % n;
    if (!(in->pressed & PC_UI_CONFIRM))
        return 0;
    const RowDef& r = TABS[s.tab].rows[rows[s.row[s.tab]]];
    // Greyed rows open too, as in the original top level: the game's own
    // screen greys what is off inside (MENU0, MENU1...) and the player can browse.
    s.visible = false; // the game shows the original screen; it reopens the shell after
    return r.command + 1;
}

extern "C" void pc_ui_shell_json(char* out, unsigned size)
{
    std::string j = "{\"visible\":";
    j += s.visible ? "true" : "false";
    j += ",\"holding\":";
    j += s.holding ? "true" : "false";
    j += ",\"menus\":";
    j += pc_ui_menus ? "\"modern\"" : "\"original\"";
    const TabDef& t = TABS[s.tab];
    j += ",\"tab\":\"" + std::string(t.id) + "\",\"tabs\":[";
    for (int i = 0; i < TAB_COUNT; ++i)
        j += (i ? ",\"" : "\"") + std::string(TABS[i].id) + "\"";
    // rows: the command rows, or the System list's entries
    j += "],\"rows\":[";
    const char *id, *label, *help, *reason, *focus_id = "";
    int enabled;
    if (t.screen == SCREEN_SYSTEM) {
        sys_clamp();
        for (int i = 0; pc_ui_shell_system_item(i, &id, &label, &help, &enabled, &reason); ++i) {
            j += i ? ",{\"id\":\"" : "{\"id\":\"";
            j += std::string(id) + "\",\"enabled\":" + (enabled ? "true" : "false");
            j += ",\"label\":\"" + escape(label) + "\"";
            if (reason)
                j += ",\"reason\":\"" + escape(reason) + "\"";
            j += "}";
            if (i == s.sys.item)
                focus_id = id;
        }
    } else {
        int rows[6];
        int n = shown_rows(s.tab, rows);
        for (int i = 0; i < n; ++i) {
            const RowDef& r = t.rows[rows[i]];
            j += i ? ",{\"id\":\"" : "{\"id\":\"";
            j += std::string(r.id) + "\",\"enabled\":" + (row_enabled(r) ? "true" : "false");
            j += ",\"label\":\"" + escape(r.label) + "\"";
            if (const char* why = row_reason(r))
                j += ",\"reason\":\"" + escape(why) + "\"";
            j += "}";
        }
        if (n)
            focus_id = t.rows[rows[s.row[s.tab]]].id;
    }
    j += "],\"row\":\"";
    j += focus_id;
    j += "\",\"screen\":\"";
    j += t.screen == SCREEN_EQUIPMENT ? "equipment" : t.screen == SCREEN_ASHLEY ? "ashley"
        : t.screen == SCREEN_SYSTEM                                            ? "system"
        : t.screen == SCREEN_ABILITIES                                         ? "abilities"
        : t.screen == SCREEN_MAGICK                                            ? "magick"
        : t.screen == SCREEN_ITEMS                                             ? "items"
                                                                                : "commands";
    j += "\"";
    if (t.screen == SCREEN_EQUIPMENT)
        equip_json(j);
    else if (t.screen == SCREEN_ASHLEY)
        ashley_json(j);
    else if (t.screen == SCREEN_SYSTEM)
        system_json(j);
    else if (t.screen == SCREEN_ABILITIES)
        abilities_json(j);
    else if (t.screen == SCREEN_MAGICK)
        magick_json(j);
    else if (t.screen == SCREEN_ITEMS)
        items_json(j);
    // a direct session's screen (the Loot screen: ui_loot.cpp), while it shows
    if (pc_ui_loot_visible()) {
        static char loot[1 << 16];
        pc_ui_loot_json(loot, sizeof loot);
        j += loot;
    }
    // the Map frame (ui_map.cpp): its block while armed or framed, else null
    static char map[1 << 15];
    pc_ui_map_json(map, sizeof map);
    j += map;
    // the native Data screen (ui_data.cpp): its block while armed or framed, else null
    static char data[1 << 14];
    pc_ui_data_json(data, sizeof data);
    j += data;
    // the native Container screen (ui_container.cpp): its block while its MENU7 run is latched, else null
    static char container[1 << 16];
    pc_ui_container_json(container, sizeof container);
    j += container;
    ui::theme_json(j); // the colours shown (menu tint), every screen
    j += "}";
    std::snprintf(out, size, "%s", j.c_str());
}

extern "C" void pc_ui_shell_set_tab(int tab)
{
    if (tab < 0 || tab >= TAB_COUNT || !tab_has_rows(tab))
        return;
    s.tab = tab;
    clamp_row(tab);
}
extern "C" int pc_ui_shell_step_tab(int dir) { return step_tab(s.tab, dir < 0 ? -1 : 1); }

/* For ui_draw.cpp: read-only views of the state. */
extern "C" int pc_ui_shell_tab(void) { return s.tab; }
/* The right-stick camera's menu context for the tab shown (camera_stick.c
 * camera_stick_menus; the classic menus' names), NULL while the shell is
 * not up: its full-screen screens as the original's (Ashley: status,
 * Equipment: equip, Items, System: options), the lists over the scene as
 * the original's lists (Abilities, Magick, the Le\xc3\xa1 Monde rows: command). */
extern "C" const char* pc_ui_shell_camera_context(void)
{
    if (!s.visible)
        return nullptr;
    static const struct {
        const char *tab, *context;
    } contexts[] = { { "ashley", "status" }, { "equipment", "equip" }, { "items", "items" },
        { "abilities", "abilities" }, { "magick", "magic" }, { "lea_monde", "command" }, { "system", "options" } };
    for (const auto& c : contexts)
        if (!strcmp(TABS[s.tab].id, c.tab))
            return c.context;
    return "options"; /* (a tab added later: full screen until listed here) */
}
extern "C" int pc_ui_shell_tab_count(void) { return TAB_COUNT; }
extern "C" const char* pc_ui_shell_tab_title(int tab) { return TABS[tab].title; }
/* Row i of the current tab (shown rows only): label, help, value (or null),
 * enabled, reason (why it is greyed, or null). Returns 0 past the end. */
extern "C" int pc_ui_shell_row(int i, const char** label, const char** help, const char** value, int* enabled,
    const char** reason)
{
    int rows[6];
    int n = shown_rows(s.tab, rows);
    if (i >= n)
        return 0;
    const RowDef& r = TABS[s.tab].rows[rows[i]];
    *label = r.label;
    *help = r.help;
    *value = nullptr;
    *enabled = row_enabled(r);
    *reason = row_reason(r);
    return 1;
}
extern "C" int pc_ui_shell_focus(void) { return s.row[s.tab]; }
extern "C" int pc_ui_shell_screen(void) { return TABS[s.tab].screen; }

extern "C" void pc_ui_shell_set_equipment(const pc_ui_equipment* e)
{
    s.model = *e;
    if (picking())
        sync_cursor();
}

extern "C" int pc_ui_shell_pop_request(pc_ui_request* out)
{
    if (!s.q_count)
        return 0;
    *out = s.queue[s.q_head];
    s.q_head = (s.q_head + 1) % QUEUE;
    --s.q_count;
    return 1;
}

extern "C" int pc_ui_shell_wanted_candidates(void)
{
    if (!s.visible && !s.holding)
        return -1;
    // the Quick swap overview shows each slot's weapon stats: every weapon
    if (TABS[s.tab].screen == SCREEN_EQUIPMENT && s.eq.mode == M_QUICK)
        return PC_UI_SLOT_WEAPON;
    return wanted_slot();
}

extern "C" void pc_ui_shell_set_ashley(const pc_ui_ashley* a)
{
    s.ash = *a;
    ash_clamp();
}
extern "C" const pc_ui_ashley* pc_ui_shell_ashley(void) { return &s.ash; }
extern "C" int pc_ui_shell_ashley_row(void) { return s.ash_row; }
extern "C" int pc_ui_shell_ashley_limb(void) { return s.ash_row == PC_UI_ASH_CONDITION ? s.ash_limb : -1; }
extern "C" const char* pc_ui_limb_state_name(int state)
{
    static const char* const names[5] = {"Critical", "Damaged", "Wounded", "Good", "Excellent"};
    return names[state < 0 ? 0 : state > 4 ? 4 : state];
}
extern "C" void pc_ui_ashley_condition_text(const pc_ui_ashley* a, int limb, char* title, int title_size, char* line,
    int line_size)
{
    int n = a->limb_count < 0 ? 0 : a->limb_count > PC_UI_LIMBS ? PC_UI_LIMBS : a->limb_count;
    if (limb >= 0 && limb < n) {
        const pc_ui_limb& l = a->limbs[limb];
        std::snprintf(title, title_size, "%s \xc2\xb7 %s", l.name, pc_ui_limb_state_name(l.state));
        std::snprintf(line, line_size, "%s: %d / %d HP, %s.", l.name, l.hp, l.hp_max, pc_ui_limb_state_name(l.state));
        return;
    }
    // the panel: the worst part's state (lowest state, then lowest share of its HP), how many are hurt
    int worst = -1, hurt = 0;
    for (int i = 0; i < n; ++i) {
        const pc_ui_limb& l = a->limbs[i];
        hurt += l.state < PC_UI_LIMB_EXCELLENT;
        if (worst < 0 || l.state < a->limbs[worst].state
            || (l.state == a->limbs[worst].state
                && (long long)l.hp * (a->limbs[worst].hp_max > 0 ? a->limbs[worst].hp_max : 1)
                    < (long long)a->limbs[worst].hp * (l.hp_max > 0 ? l.hp_max : 1)))
            worst = i;
    }
    std::snprintf(title, title_size, "Condition \xc2\xb7 %s",
        pc_ui_limb_state_name(worst >= 0 ? a->limbs[worst].state : PC_UI_LIMB_EXCELLENT));
    if (!n)
        std::snprintf(line, line_size, "No body parts to show.");
    else if (!hurt)
        std::snprintf(line, line_size, "Every part of the body is unhurt.");
    else
        std::snprintf(line, line_size, "%d of %d parts hurt; the worst is %s (%s).", hurt, n, a->limbs[worst].name,
            pc_ui_limb_state_name(a->limbs[worst].state));
}
extern "C" int pc_ui_shell_ashley_rows(void) { return ash_rows(); }
extern "C" void pc_ui_shell_ashley_effects_shown(int n)
{
    s.ash_shown = n < 0 ? -1 : n;
    ash_clamp();
}
extern "C" int pc_ui_shell_wants_model(void) { return on_ashley(); }
extern "C" void pc_ui_shell_model_drag(float dx, float dy)
{
    if (!on_ashley() || dx != dx || dy != dy) // not shown, or NaN
        return;
    s.drag_x += dx;
    s.drag_y += dy;
}
extern "C" void pc_ui_shell_model_turn(float* yaw_delta, float* pitch_delta)
{
    *yaw_delta = on_ashley() ? s.drag_x : 0;
    *pitch_delta = on_ashley() ? s.drag_y : 0;
    s.drag_x = s.drag_y = 0;
}

extern "C" int pc_ui_shell_confirm_focus(void) { return s.sys.confirm_focus; }
extern "C" int pc_ui_shell_quit_focus(void) { return s.sys.quit_focus; }
extern "C" void pc_ui_shell_system_view(pc_ui_system_view* v)
{
    int g[SYSTEM_GAMES];
    v->rows = sys_clamp();
    v->items = sys_items();
    v->games = sys_games(g);
    v->quit_first = sys_quit_first();
    v->item = s.sys.item;
    v->page = sys_shown_page();
    v->entered = s.sys.page >= 0;
    v->row = v->page >= 0 ? sys_row(v->page) : 0;
    v->capture = pc_ui_settings_capture_button();
}
extern "C" int pc_ui_shell_system_item(int i, const char** id, const char** label, const char** help, int* enabled,
    const char** reason)
{
    int g[SYSTEM_GAMES];
    int games = sys_games(g);
    int pages = pc_ui_settings_pages();
    if (i < 0 || i >= games + pages + SYSTEM_QUITS)
        return 0;
    if (i >= games + pages) {
        const QuitDef& q = SYSTEM_QUIT[i - games - pages];
        *id = q.id, *label = q.label, *help = q.help;
        *enabled = 1;
        *reason = nullptr;
        return 1;
    }
    if (i < games) {
        const RowDef& r = SYSTEM_GAME[g[i]];
        *id = r.id, *label = r.label, *help = r.help;
        *enabled = row_enabled(r);
        *reason = row_reason(r);
        return 1;
    }
    int page = i - games;
    *id = pc_ui_settings_page_id(page);
    *label = pc_ui_settings_page_title(page);
    *help = pc_ui_settings_page_help(page);
    *enabled = 1;
    *reason = nullptr;
    return 1;
}
extern "C" void pc_ui_shell_system_value(int row, int delta, double value, int set)
{
    if (!s.visible || capturing() || value != value)
        return;
    s.mouse.value = true;
    s.mouse.value_row = row;
    s.mouse.value_delta = delta;
    s.mouse.value_number = value;
    s.mouse.value_set = set != 0;
    s.mouse.value_preview = set == 2;
}

extern "C" void pc_ui_shell_set_abilities(const pc_ui_abilities* a)
{
    s.ab_model = *a;
    ab_clamp();
}
extern "C" const pc_ui_abilities* pc_ui_shell_abilities(void) { return &s.ab_model; }
extern "C" void pc_ui_shell_abilities_view(pc_ui_abilities_view* v)
{
    ab_clamp();
    v->subs = ab_subs(v->sub_kind);
    v->sub = ab_sub_index();
    v->kind = s.ab.kind;
    v->mode = s.ab.mode;
    int t = s.ab.kind == PC_UI_AB_DEFENSE;
    v->slot = s.ab.slot[t];
    v->cursor = s.ab.cursor[t];
    v->category = ab_category();
    v->category_row = ab_category_row();
    v->art = s.ab.art;
    ab_order(v->order);
}

extern "C" void pc_ui_shell_set_magick(const pc_ui_magick* m)
{
    s.mg_model = *m;
    if (!s.mg.seeded && m->valid) {
        s.mg.seeded = true;
        s.mg.level = m->level_memory;
    }
    mg_place();
    mg_clamp();
}
extern "C" const pc_ui_magick* pc_ui_shell_magick(void) { return &s.mg_model; }
extern "C" void pc_ui_shell_magick_view(pc_ui_magick_view* v)
{
    mg_clamp();
    v->subs = mg_subs(v->sub_kind);
    v->sub = mg_sub_index();
    v->kind = v->subs ? s.mg.kind : -1;
    v->cursor = v->subs ? s.mg.cursor[s.mg.kind] : 0;
    v->rows = v->subs ? mg_rows(s.mg.kind) : 0;
    v->level = v->subs ? mg_level() : 0;
}

extern "C" void pc_ui_shell_set_items(const pc_ui_items* m)
{
    s.it_model = *m;
    it_clamp();
}
extern "C" void pc_ui_shell_set_items_details(const pc_ui_item_details* d) { s.it_model.details = *d; }
extern "C" const pc_ui_items* pc_ui_shell_items(void) { return &s.it_model; }
extern "C" int pc_ui_shell_items_focus(int* category, int* inventory)
{
    if (!s.visible || !on_items() || s.it.ws)
        return 0;
    int c = it_category(), inv = it_inventory();
    if (inv < 0)
        return 0;
    *category = c;
    *inventory = inv;
    return 1;
}
extern "C" void pc_ui_shell_items_view(pc_ui_items_view* v)
{
    it_clamp();
    const It& t = s.it;
    int c = it_category();
    std::memset(v, 0, sizeof *v);
    v->category = c;
    v->mode = t.mode;
    v->cursor = t.cursor[c];
    v->rows = it_rows(c);
    v->capacity = s.it_model.valid ? s.it_model.capacity[c] : 0;
    v->row = it_row(c, t.cursor[c]);
    v->locked = it_locked();
    v->reason = it_lock_reason();
    ItAction a[PC_UI_ITA_COUNT];
    v->actions = it_actions(a);
    for (int i = 0; i < v->actions; ++i) {
        v->action_kind[i] = a[i].kind;
        v->action_enabled[i] = a[i].enabled;
        v->action_label[i] = IT_ACTION_LABELS[a[i].kind];
        v->action_reason[i] = a[i].reason && a[i].reason[0] ? a[i].reason : nullptr;
    }
    v->action = t.action;
    v->confirm_kind = t.confirm_kind;
    v->confirm = t.confirm;
    v->prompt = s.it_model.discard_prompt[it_prompt()];
    v->count = t.count;
    v->count_max = t.count_max;
    v->step = t.step;
    v->sort = t.sort;
    v->move_from = t.mode == PC_UI_IT_MOVE ? t.move_from : -1;
}

extern "C" int pc_ui_shell_workshop_shown(void) { return on_items() && s.it.ws; }
extern "C" int pc_ui_shell_manual_shown(void) { return s.visible && on_system() && pc_ui_manual_mode() != 0; }
extern "C" int pc_ui_shell_score_shown(void) { return s.visible && on_system() && pc_ui_score_mode() != 0; }
extern "C" void pc_ui_shell_push(int kind, int slot, int item, int shield, int count)
{
    push(kind, slot, item, shield, count);
}
extern "C" void pc_ui_shell_push_request(const pc_ui_request* r) { push_request(*r); }
