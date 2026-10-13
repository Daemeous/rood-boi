// Modern menu shell: tabs, rows, focus and the game's result protocol
// (pc/docs/Modern_Menus.md). Pure state; drawing is ui_draw.cpp.
#include "ui/ui_shell.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

enum RowKind { ROW_COMMAND, ROW_MENUS_SETTING };
struct RowDef {
    const char* id;
    const char* label;
    const char* help;
    RowKind kind;
    int command; // ROW_COMMAND: the original command index
};
enum Screen { SCREEN_COMMANDS = PC_UI_SCREEN_COMMANDS, SCREEN_EQUIPMENT = PC_UI_SCREEN_EQUIPMENT,
    SCREEN_ASHLEY = PC_UI_SCREEN_ASHLEY };
struct TabDef {
    const char* id;
    const char* title;
    RowDef rows[6];
    int count;
    Screen screen = SCREEN_COMMANDS; // SCREEN_COMMANDS: the rows; else a native screen with its own state
    int command = -1; // native screen: the original command that gates it (menuFlags, unselectable)
};

const TabDef TABS[] = {
    // native summary; Square opens the original Status (command 4, which gates the tab)
    {"ashley", "Ashley", {}, 0, SCREEN_ASHLEY, 4},
    // native screen; the original equip page stays reachable through Items (same command)
    {"equipment", "Equipment", {}, 0, SCREEN_EQUIPMENT, 3},
    {"items", "Items", {{"items", "Items", "Use, sort and discard what you carry.", ROW_COMMAND, 3}}, 1},
    {"abilities", "Abilities",
        {{"battle_abilities", "Battle abilities", "Put chain and defense abilities on buttons.", ROW_COMMAND, 2},
         {"break_arts", "Break arts", "Weapon techniques paid for with HP.", ROW_COMMAND, 1}}, 2},
    {"magick", "Magick", {{"magick", "Magick", "Cast spells from your grimoires.", ROW_COMMAND, 0}}, 1},
    {"lea_monde", "Le\xc3\xa1 Monde", {{"map", "Map", "Where you are and where you have been.", ROW_COMMAND, 5}}, 1},
    {"system", "System",
        {{"data", "Data", "Save and load.", ROW_COMMAND, 6},
         {"options", "Options", "The game's options.", ROW_COMMAND, 7},
         {"score", "Score", "Titles, gazette, records, encyclopaedia.", ROW_COMMAND, 8},
         {"manual", "Quick manual", "How chains, risk and the workshop work.", ROW_COMMAND, 9},
         {"menus", "Menus", "Modern or the game's original menus (from the next time the menu opens).",
          ROW_MENUS_SETTING, -1}}, 5},
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
bool tab_has_rows(int tab)
{
    if (TABS[tab].screen != SCREEN_COMMANDS)
        return (s.commands.enabled >> TABS[tab].command) & 1;
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
        s.tab = m.click_index;
        equip_base();
        s.drag_x = s.drag_y = 0;
        return true;
    case PC_UI_MOUSE_SUB:
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
    default:
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
    if (wheel == wheel) { // not NaN
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
    if (!remember) {
        s.tab = 0;
        std::memset(s.row, 0, sizeof s.row);
        s.eq = Equip();
        s.ash_row = 0;
    }
    equip_base(); // a pick list never survives the shell closing
    for (int t = 0; t < TAB_COUNT; ++t)
        clamp_row(t);
    if (!tab_has_rows(s.tab))
        s.tab = step_tab(s.tab, 1);
}

extern "C" void pc_ui_shell_close(void)
{
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
    bool equipment = TABS[s.tab].screen == SCREEN_EQUIPMENT;
    // Cross steps back out of a pick list; at the top it closes, as Triangle does
    if ((in->pressed & PC_UI_BACK) && !(in->pressed & PC_UI_EXIT) && equipment && equip_back())
        return 0;
    if (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) {
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
        equip_base(); // leaving (or entering) the Equipment tab drops its pick list
        s.drag_x = s.drag_y = 0;
        return 0;
    }
    if (TABS[s.tab].screen == SCREEN_ASHLEY)
        return ashley_tick(in);
    if (TABS[s.tab].screen == SCREEN_EQUIPMENT) {
        equip_tick(in);
        return 0;
    }
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
    if (r.kind == ROW_MENUS_SETTING) {
        pc_ui_menus = !pc_ui_menus;
        return 0;
    }
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
    int rows[6];
    int n = shown_rows(s.tab, rows);
    j += "],\"rows\":[";
    for (int i = 0; i < n; ++i) {
        const RowDef& r = t.rows[rows[i]];
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s{\"id\":\"%s\",\"enabled\":%s", i ? "," : "", r.id,
            row_enabled(r) ? "true" : "false");
        j += buf;
        j += ",\"label\":\"" + escape(r.label) + "\"";
        if (const char* why = row_reason(r))
            j += ",\"reason\":\"" + escape(why) + "\"";
        if (r.kind == ROW_MENUS_SETTING)
            j += std::string(",\"value\":\"") + (pc_ui_menus ? "Modern" : "Original") + "\"";
        j += "}";
    }
    j += "],\"row\":\"";
    j += n ? t.rows[rows[s.row[s.tab]]].id : "";
    j += "\",\"screen\":\"";
    j += t.screen == SCREEN_EQUIPMENT ? "equipment" : t.screen == SCREEN_ASHLEY ? "ashley" : "commands";
    j += "\"";
    if (t.screen == SCREEN_EQUIPMENT)
        equip_json(j);
    else if (t.screen == SCREEN_ASHLEY)
        ashley_json(j);
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
    *value = r.kind == ROW_MENUS_SETTING ? (pc_ui_menus ? "Modern" : "Original") : nullptr;
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
