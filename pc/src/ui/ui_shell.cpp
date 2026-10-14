// Modern menu shell: tabs, rows, focus and the game's result protocol
// (pc/docs/Modern_Menus.md). Pure state; drawing is ui_draw.cpp.
#include "ui/ui_shell.h"
#include "ui/ui_settings.h"
#include <cstdio>
#include <cstring>
#include <string>

extern "C" const char* pc_settings_status(void);

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
    SCREEN_ASHLEY = PC_UI_SCREEN_ASHLEY, SCREEN_SYSTEM = PC_UI_SCREEN_SYSTEM, SCREEN_ABILITIES = PC_UI_SCREEN_ABILITIES };
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
    {"items", "Items", {{"items", "Items", "Use, sort and discard what you carry.", ROW_COMMAND, 3}}, 1},
    // native screen: sub-tabs Chain and Defense (command 2, Battle Abilities) and Break Arts (command 1),
    // each shown once the game has unlocked its command (menuFlags); Square opens the original screen
    {"abilities", "Abilities", {}, 0, SCREEN_ABILITIES, -1},
    {"magick", "Magick", {{"magick", "Magick", "Cast spells from your grimoires.", ROW_COMMAND, 0}}, 1},
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
    struct {
        int item = 0;  // focused list entry
        int page = -1; // the entered settings page; -1: the list has the focus
        int row[PC_UI_SETTINGS_PAGES] = {};
    } sys;
    Abil ab;
    pc_ui_abilities ab_model = {};
    int ash_shown = -1;      // effect chips the drawing fits, -1 all
    float drag_x = 0, drag_y = 0; // model drag since the bridge last took it
} s;

void push(int kind, int slot, int item, int shield)
{
    if (s.q_count == QUEUE)
        return; // the bridge drains every tick; a full queue drops the newest
    s.queue[(s.q_head + s.q_count++) % QUEUE] = {kind, slot, item, shield};
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
void ash_clamp()
{
    int n = ash_rows();
    if (s.ash_row >= n)
        s.ash_row = n - 1;
    if (s.ash_row < 0)
        s.ash_row = 0;
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
        "\"row\":\"%s\"}",
        a.play_h, a.play_m, a.play_s, a.model, a.model_rect[0], a.model_rect[1], a.model_rect[2], a.model_rect[3],
        a.model_yaw, a.model_pitch,
        a.model_blocked == PC_UI_MODEL_NO_HEAP ? "heap" : a.model_blocked == PC_UI_MODEL_FREE_CAMERA ? "free_camera" : "",
        a.heap_free, a.heap_need, ash_rows() - PC_UI_ASH_EFFECT, ash_row_id(s.ash_row, id, sizeof id));
    j += buf;
}

// ---- System ----
// The game's own screens at the top of the list (original commands 6-9).
const RowDef SYSTEM_GAME[] = {
    {"data", "Data", "Save and load.", ROW_COMMAND, 6},
    {"options", "Options (original)", "The game's own Options screen.", ROW_COMMAND, 7},
    {"score", "Score", "Titles, gazette, records, encyclopaedia.", ROW_COMMAND, 8},
    {"manual", "Quick manual", "How chains, risk and the workshop work.", ROW_COMMAND, 9},
};
const int SYSTEM_GAMES = sizeof SYSTEM_GAME / sizeof SYSTEM_GAME[0];
const char* const PAGE_HELP[PC_UI_SETTINGS_PAGES] = {
    "Lighting, resolution, anti-aliasing and the 3D picture.",
    "Widescreen, scaling, the window and brightness.",
    "Volume and the game's sound output.",
    "Menus, text, 2D upscaling and button prompts.",
    "The right stick camera and vibration.",
    "Keys and gamepad buttons for each PlayStation button.",
    "Quality-of-life options and the game's own options.",
};
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
    return sys_games(g) + pc_ui_settings_pages();
}
// The settings page of list entry `item`, -1 for a game entry.
int sys_item_page(int item)
{
    int g[SYSTEM_GAMES];
    int games = sys_games(g);
    return item >= games ? item - games : -1;
}
// The page the card shows: the entered one, else the focused entry's (-1: a game entry).
int sys_shown_page() { return s.sys.page >= 0 ? s.sys.page : sys_item_page(s.sys.item); }
bool capturing() { return pc_ui_settings_capture_button() >= 0; }
// Keep the focus on the list and the page's rows (rows come and go with other settings).
int sys_clamp()
{
    int n = sys_items();
    if (s.sys.item >= n)
        s.sys.item = n ? n - 1 : 0;
    if (s.sys.item < 0)
        s.sys.item = 0;
    int page = sys_shown_page();
    if (page < 0)
        return 0;
    int rows = pc_ui_settings_rows(page);
    int& r = s.sys.row[page];
    if (r >= rows)
        r = rows ? rows - 1 : 0;
    if (r < 0)
        r = 0;
    return rows;
}
// Out of the page (Cross, another tab, the menu closing, an original screen):
// a capture that listens is cancelled and what changed is saved.
void sys_leave()
{
    if (capturing())
        pc_ui_settings_capture_done(-1, 0);
    pc_ui_settings_flush();
    s.sys.page = -1;
}
void sys_enter(int page)
{
    s.sys.page = page;
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
    int g[SYSTEM_GAMES];
    sys_games(g);
    sys_leave();
    s.visible = false; // the game shows the original screen; it reopens the shell after
    return SYSTEM_GAME[g[s.sys.item]].command + 1;
}
// Circle on a row: actions run, bindings start a capture, choices and toggles step.
void sys_row_confirm(int page, int row)
{
    pc_ui_setting_row r;
    if (!pc_ui_settings_row(page, row, &r))
        return;
    if (r.kind == PC_UI_SET_ACTION || r.kind == PC_UI_SET_BINDING)
        pc_ui_settings_activate(page, row);
    else if (r.kind == PC_UI_SET_CHOICE || r.kind == PC_UI_SET_TOGGLE)
        pc_ui_settings_change(page, row, 1);
    sys_clamp();
}
int system_tick(const pc_ui_input* in)
{
    int n = sys_items();
    sys_clamp();
    if (s.sys.page < 0) {
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
    int& row = s.sys.row[page];
    if (in->repeat & PC_UI_DOWN)
        row = (row + 1) % rows;
    if (in->repeat & PC_UI_UP)
        row = (row + rows - 1) % rows;
    int delta = ((in->repeat & PC_UI_RIGHT) ? 1 : 0) - ((in->repeat & PC_UI_LEFT) ? 1 : 0);
    if (delta) {
        pc_ui_settings_change(page, row, delta);
        sys_clamp();
    }
    if (in->pressed & PC_UI_CONFIRM)
        sys_row_confirm(page, row);
    return 0;
}
// The PS1 button's binding row id ("bind_triangle"), "" when none.
const char* capture_row_id()
{
    int button = pc_ui_settings_capture_button();
    if (button < 0)
        return "";
    pc_ui_setting_row r;
    for (int i = 0, n = pc_ui_settings_rows(PC_UI_SETTINGS_BINDINGS); i < n; ++i)
        if (pc_ui_settings_row(PC_UI_SETTINGS_BINDINGS, i, &r) && r.kind == PC_UI_SET_BINDING && r.button == button)
            return r.id;
    return "";
}
void system_json(std::string& j)
{
    int page = sys_shown_page();
    char buf[96];
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
    if (rows && pc_ui_settings_row(page, s.sys.row[page], &r))
        j += r.id;
    j += "\",\"rows\":[";
    static const char* const kinds[] = {"choice", "toggle", "slider", "action", "binding", "info"};
    for (int i = 0; i < rows; ++i) {
        if (!pc_ui_settings_row(page, i, &r))
            break;
        j += i ? ",{\"id\":\"" : "{\"id\":\"";
        j += escape(r.id) + "\",\"label\":\"" + escape(r.label) + "\",\"kind\":\"";
        j += r.kind >= 0 && r.kind <= PC_UI_SET_INFO ? kinds[r.kind] : "";
        j += "\",\"value\":\"" + escape(r.value) + "\",\"enabled\":";
        j += r.enabled ? "true" : "false";
        if (r.reason[0])
            j += ",\"reason\":\"" + escape(r.reason) + "\"";
        j += "}";
    }
    std::snprintf(buf, sizeof buf, "],\"capture\":\"%s\",\"dirty\":%s,\"saves\":%d,", capture_row_id(),
        pc_ui_settings_dirty() ? "true" : "false", pc_ui_settings_saves());
    j += buf;
    const char* note = pc_ui_settings_note();               // a binding swap's (3 s)
    const char* refused = pc_ui_settings_capture_note();    // a reserved key pressed during the capture
    j += "\"note\":\"" + escape(note ? note : "") + "\",\"capture_note\":\"" + escape(refused ? refused : "")
        + "\",\"status\":\"";
    const char* status = pc_settings_status();
    j += escape(status ? status : "") + "\"}";
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
            return true;
        }
        if (equipment || index < 0 || index >= clamp_row(s.tab))
            return false;
        s.row[s.tab] = index;
        return true;
    }
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
        s.sys.row[page] = index;
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
// Fold the pointer since the last tick into this tick's input. True when a
// click switched a tab or sub-tab (that is the tick's whole action).
bool apply_mouse(pc_ui_input& in)
{
    Mouse m = s.mouse;
    s.mouse.hover_kind = s.mouse.click_kind = PC_UI_MOUSE_NONE;
    s.mouse.value = false;
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
        s.sys.row[page] = m.value_row;
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
        s.drag_x = s.drag_y = 0;
        return true;
    case PC_UI_MOUSE_SUB:
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
                s.sys.row[page] = m.click_index;
            }
            return true;
        }
        if (mouse_focus(m.click_kind, m.click_index))
            in.pressed |= PC_UI_CONFIRM, in.repeat |= PC_UI_CONFIRM;
        return false;
    default:
        if (ab_click_zone(m.click_kind, m.click_index))
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
    s.sys.page = -1; // the System screen opens on its list
    if (!remember) {
        s.tab = 0;
        std::memset(s.row, 0, sizeof s.row);
        s.eq = Equip();
        s.ash_row = 0;
        s.sys.item = 0;
        std::memset(s.sys.row, 0, sizeof s.sys.row);
        s.ab = Abil();
    }
    equip_base(); // a pick list never survives the shell closing
    ab_unpick();
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
    s.visible = false;
    s.drag_x = s.drag_y = 0;
    s.holding = false;
}

extern "C" int pc_ui_shell_visible(void) { return s.visible; }

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
    bool equipment = TABS[s.tab].screen == SCREEN_EQUIPMENT;
    // Cross steps back out of a pick list; at the top it closes, as Triangle does
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && equipment && equip_back())
        return 0;
    // Abilities: Cross steps back out of a list; in the learned list Triangle is a button to map
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && on_abilities() && ab_back())
        return 0;
    if (ab_takes_triangle() && (in->pressed & PC_UI_EXIT) && !(in->pressed & PC_UI_BACK))
        return abilities_tick(in);
    // Cross steps back out of a settings page to the System list
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && on_system() && s.sys.page >= 0) {
        sys_leave();
        return 0;
    }
    if (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) {
        sys_leave();
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
    j += "}";
    std::snprintf(out, size, "%s", j.c_str());
}

/* For ui_draw.cpp: read-only views of the state. */
extern "C" int pc_ui_shell_tab(void) { return s.tab; }
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

extern "C" void pc_ui_shell_system_view(pc_ui_system_view* v)
{
    int g[SYSTEM_GAMES];
    v->rows = sys_clamp();
    v->items = sys_items();
    v->games = sys_games(g);
    v->item = s.sys.item;
    v->page = sys_shown_page();
    v->entered = s.sys.page >= 0;
    v->row = v->page >= 0 ? s.sys.row[v->page] : 0;
    v->capture = pc_ui_settings_capture_button();
}
extern "C" int pc_ui_shell_system_item(int i, const char** id, const char** label, const char** help, int* enabled,
    const char** reason)
{
    int g[SYSTEM_GAMES];
    int games = sys_games(g);
    if (i < 0 || i >= games + pc_ui_settings_pages())
        return 0;
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
    *help = PAGE_HELP[page];
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
