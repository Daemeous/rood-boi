// Modern menus: the Workshop sub-tab's state and navigation (ui_workshop.h).
// Pure state; the bridge (workshop_bridge.c) carries out the requests and
// fills the snapshot, ui_workshop_draw.cpp draws it.
#include "ui/ui_workshop.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const unsigned char SPACE = 0x8F; // vs_char_space
const int NAME = PC_UI_WS_NAME_LEN;
const int GRID_ROWS = PC_UI_WS_GRID_ROWS, GRID_COLS = PC_UI_WS_GRID_COLS;
const int MESSAGE_TICKS = 120;

// MENU12.BIN indices (build/assets/MENU/MENU12.BIN.h VS_MENU12_BIN_INDEX_*) the screen shows
enum {
    T_ASSEMBLE_DESC = 2, T_GEMS_DESC = 4, T_DISASSEMBLE_DESC = 6, T_RENAME_DESC = 8, T_REPAIR_DESC = 10,
    T_COMBINE_DESC = 12, T_INVALID_GRIP = 18, T_INVALID_BLADE = 19, T_INVALID_BLADE_MATERIAL = 20,
    T_INVALID_SHIELD_MATERIAL = 21, T_INVALID_ARMOR_MATERIAL = 22, T_NO_GEMS = 25, T_DISASSEMBLE_WEAPON_CONFIRM = 39,
    T_WEAPON_EQUIPPED_WARN = 40, T_DISASSEMBLE_SHIELD_PROMPT = 43, T_SHIELD_EQUIPPED_WARN = 44, T_REPAIR_CONFIRM = 45,
    T_ONE_ITEM = 49, T_NO_ITEMS = 53, T_WEAPON_NO_GEM_SLOTS = 59, T_SHIELD_NO_GEM_SLOTS = 61, T_BLADES_DESC = 64,
    T_GRIPS_DESC = 66, T_GEMS_PICK_DESC = 68, T_ATTACHMENT_CONFIRM = 69, T_SELECT_BLADE_TO_COMBINE = 76,
    T_SELECT_SHIELD = 77, T_SELECT_ARMOR = 78, T_CANCEL_COMBINE_CONFIRM = 86
};

struct Ws {
    int mode = PC_UI_WS_ROWS;
    int flow = -1;
    int row = 0;                // the command list
    int cursor = 0;             // the open list's or slot menu's row; CONFIRM: 0 Yes, 1 No
    int target_cursor = 0;      // TARGET's row, kept while deeper
    int target_list = -1, target = -1; // the record a flow works on (SLOTS, NAME, CONFIRM)
    int blade = -1, grip = -1, gems[3] = {-1, -1, -1}; // ASSEMBLE: records
    int combine = 0, first = -1, second = -1;          // COMBINE: category, records
    int pending[3] = {-1, -1, -1};                     // SLOTS: gem records per slot
    int pick = PC_UI_WSP_BLADE, pick_slot = 0, owner_cursor = 0; // PICK: what it fills, where it came from
    int confirm_kind = -1;
    unsigned char field[NAME] = {};
    int name_cursor = 0, grid_row = 0, grid_col = 0;
    bool keyboard = false, assembling = false;
    bool gems_edited = false;   // SLOTS: a slot was changed on this visit (the original attaches and re-equips live)
    int message = 0;            // ticks left of the refused line
} w;
// the state before the last change request was sent: a refused request comes back to it (the flow, its
// choices and the name as they were), as the original's screen stays where it was on a refusal
Ws before;
bool before_valid = false;

pc_ui_workshop model;
bool model_valid = false;

// ---- the snapshot's lists ----
const pc_ui_ws_row* rows_of(int list, int* n)
{
    static const int caps[PC_UI_WSL_LISTS] = {8, 16, 16, 8, 16, 48};
    const pc_ui_ws_row* base = nullptr;
    switch (list) {
    case PC_UI_WSL_WEAPONS: base = model.weapons; break;
    case PC_UI_WSL_BLADES: base = model.blades; break;
    case PC_UI_WSL_GRIPS: base = model.grips; break;
    case PC_UI_WSL_SHIELDS: base = model.shields; break;
    case PC_UI_WSL_ARMOR: base = model.armor; break;
    case PC_UI_WSL_GEMS: base = model.gems; break;
    default: *n = 0; return nullptr;
    }
    int c = model_valid ? model.count[list] : 0;
    *n = c < 0 ? 0 : c > caps[list] ? caps[list] : c;
    return base;
}
const pc_ui_ws_row* find(int list, int inv)
{
    int n;
    const pc_ui_ws_row* r = rows_of(list, &n);
    for (int i = 0; i < n && inv >= 0; ++i)
        if (r[i].inventory == inv)
            return &r[i];
    return nullptr;
}
int row_of(int list, int inv)
{
    int n;
    const pc_ui_ws_row* r = rows_of(list, &n);
    for (int i = 0; i < n && inv >= 0; ++i)
        if (r[i].inventory == inv)
            return i;
    return -1;
}
const char* text(int index)
{
    return model_valid && index >= 0 && index < PC_UI_WS_TEXTS && model.text[index][0] ? model.text[index] : nullptr;
}
const int COMBINE_LISTS[3] = {PC_UI_WSL_BLADES, PC_UI_WSL_SHIELDS, PC_UI_WSL_ARMOR};
const char* const COMBINE_LABELS[3] = {"Blades", "Shields", "Armor"};
const int COMBINE_MATERIAL_TEXT[3] = {T_INVALID_BLADE_MATERIAL, T_INVALID_SHIELD_MATERIAL, T_INVALID_ARMOR_MATERIAL};

// the assembly's grip (its gem slots count once blade and grip are chosen)
int assembly_gem_slots()
{
    const pc_ui_ws_row* g = find(PC_UI_WSL_GRIPS, w.grip);
    return w.blade >= 0 && g ? (g->gem_slots < 0 ? 0 : g->gem_slots > 3 ? 3 : g->gem_slots) : 0;
}
const pc_ui_ws_row* target_row() { return find(w.target_list, w.target); }
int target_gem_slots()
{
    const pc_ui_ws_row* t = target_row();
    return t ? (t->gem_slots < 0 ? 0 : t->gem_slots > 3 ? 3 : t->gem_slots) : 0;
}

// ---- the open list (TARGET, PICK) ----
pc_ui_ws_entry entries[PC_UI_WS_ENTRIES];

void entry(int& n, int list, const pc_ui_ws_row* rec, const char* label, bool enabled, const char* reason,
    bool current = false, int category = -1, int count = 0)
{
    if (n >= PC_UI_WS_ENTRIES)
        return;
    pc_ui_ws_entry& e = entries[n++];
    e.list = list;
    e.rec = rec;
    e.label = label ? label : rec ? rec->name : "";
    e.enabled = enabled;
    e.reason = enabled ? nullptr : reason;
    e.current = current;
    e.category = category;
    e.count = count;
}

int build_target()
{
    int n = 0, k;
    const pc_ui_ws_row* r;
    switch (w.flow) {
    case PC_UI_WSA_GEMS:
    case PC_UI_WSA_DISASSEMBLE: {
        r = rows_of(PC_UI_WSL_WEAPONS, &k);
        for (int i = 0; i < k; ++i)
            entry(n, PC_UI_WSL_WEAPONS, &r[i], nullptr, w.flow != PC_UI_WSA_GEMS || r[i].gem_slots > 0,
                text(T_WEAPON_NO_GEM_SLOTS));
        r = rows_of(PC_UI_WSL_SHIELDS, &k);
        for (int i = 0; i < k; ++i) {
            bool any = false;
            for (int s = 0; s < 3 && s < r[i].gem_slots; ++s)
                any = any || r[i].gems[s] >= 0;
            if (w.flow == PC_UI_WSA_GEMS)
                entry(n, PC_UI_WSL_SHIELDS, &r[i], nullptr, r[i].gem_slots > 0, text(T_SHIELD_NO_GEM_SLOTS));
            else
                entry(n, PC_UI_WSL_SHIELDS, &r[i], nullptr, any, text(T_NO_GEMS));
        }
        break;
    }
    case PC_UI_WSA_RENAME:
        r = rows_of(PC_UI_WSL_WEAPONS, &k);
        for (int i = 0; i < k; ++i)
            entry(n, PC_UI_WSL_WEAPONS, &r[i], nullptr, true, nullptr);
        break;
    case PC_UI_WSA_COMBINE:
        for (int c = 0; c < 3; ++c) {
            rows_of(COMBINE_LISTS[c], &k);
            entry(n, -1, nullptr, COMBINE_LABELS[c], k >= 2, text(k == 0 ? T_NO_ITEMS : T_ONE_ITEM), false, c, k);
        }
        break;
    default:
        break;
    }
    return n;
}

bool in(const int* a, int count, int v)
{
    for (int i = 0; i < count; ++i)
        if (a[i] == v && v >= 0)
            return true;
    return false;
}

int build_pick()
{
    int n = 0, k;
    const pc_ui_ws_row* r;
    switch (w.pick) {
    case PC_UI_WSP_BLADE: {
        const pc_ui_ws_row* cur = find(PC_UI_WSL_BLADES, w.blade);
        int gr = row_of(PC_UI_WSL_GRIPS, w.grip);
        if (cur)
            entry(n, PC_UI_WSL_BLADES, cur, nullptr, true, nullptr, true);
        else
            entry(n, -1, nullptr, "Blades", false, text(T_BLADES_DESC));
        r = rows_of(PC_UI_WSL_BLADES, &k);
        for (int i = 0; i < k; ++i)
            if (r[i].parent < 0 && r[i].inventory != w.blade)
                entry(n, PC_UI_WSL_BLADES, &r[i], nullptr, gr < 0 || ((r[i].grips_ok >> gr) & 1u),
                    text(T_INVALID_BLADE));
        break;
    }
    case PC_UI_WSP_GRIP: {
        const pc_ui_ws_row* cur = find(PC_UI_WSL_GRIPS, w.grip);
        const pc_ui_ws_row* blade = find(PC_UI_WSL_BLADES, w.blade);
        if (cur)
            entry(n, PC_UI_WSL_GRIPS, cur, nullptr, true, nullptr, true);
        else
            entry(n, -1, nullptr, "Grips", false, text(T_GRIPS_DESC));
        r = rows_of(PC_UI_WSL_GRIPS, &k);
        for (int i = 0; i < k; ++i)
            if (r[i].parent < 0 && r[i].inventory != w.grip)
                entry(n, PC_UI_WSL_GRIPS, &r[i], nullptr, !blade || ((blade->grips_ok >> i) & 1u),
                    text(T_INVALID_GRIP));
        break;
    }
    case PC_UI_WSP_GEM: {
        bool assembling = w.flow == PC_UI_WSA_ASSEMBLE;
        const int* slots = assembling ? w.gems : w.pending;
        int s = w.pick_slot < 0 || w.pick_slot > 2 ? 0 : w.pick_slot;
        const pc_ui_ws_row* cur = find(PC_UI_WSL_GEMS, slots[s]);
        const pc_ui_ws_row* t = target_row();
        if (cur)
            entry(n, PC_UI_WSL_GEMS, cur, nullptr, true, nullptr, true);
        else
            entry(n, -1, nullptr, "Gems", false, text(T_GEMS_PICK_DESC));
        r = rows_of(PC_UI_WSL_GEMS, &k);
        for (int i = 0; i < k; ++i) {
            // free now: in no set, or (Gems) taken out of this one; and not chosen for another slot
            bool free = r[i].parent < 0
                || (!assembling && t && r[i].parent == t->inventory
                    && (r[i].parent_shield != 0) == (w.target_list == PC_UI_WSL_SHIELDS));
            if (free && !in(slots, 3, r[i].inventory))
                entry(n, PC_UI_WSL_GEMS, &r[i], nullptr, true, nullptr);
        }
        break;
    }
    case PC_UI_WSP_COMBINE: {
        int c = w.combine < 0 || w.combine > 2 ? 0 : w.combine;
        int l = COMBINE_LISTS[c];
        int s = w.pick_slot ? 1 : 0;
        const pc_ui_ws_row* cur = find(l, s ? w.second : w.first);
        if (cur)
            entry(n, l, cur, nullptr, true, nullptr, true);
        else
            entry(n, -1, nullptr, c == 0 ? "Blade" : c == 1 ? "Shield" : "Armor", false,
                text(c == 0 ? T_SELECT_BLADE_TO_COMBINE : c == 1 ? T_SELECT_SHIELD : T_SELECT_ARMOR));
        r = rows_of(l, &k);
        for (int i = 0; i < k; ++i)
            if (r[i].inventory != w.first && r[i].inventory != w.second)
                entry(n, l, &r[i], nullptr, r[i].material_ok != 0, text(COMBINE_MATERIAL_TEXT[c]));
        break;
    }
    default:
        break;
    }
    return n;
}

int build_entries()
{
    if (w.mode == PC_UI_WS_TARGET)
        return build_target();
    if (w.mode == PC_UI_WS_PICK)
        return build_pick();
    return 0;
}

// rows of the slot menus (the last one finishes)
int slot_rows(bool* finish)
{
    bool f = false;
    int n = 0;
    switch (w.mode) {
    case PC_UI_WS_ASSEMBLE:
        f = w.blade >= 0 && w.grip >= 0;
        n = 2 + (f ? assembly_gem_slots() + 1 : 0);
        break;
    case PC_UI_WS_SLOTS:
        f = true;
        n = target_gem_slots() + 1;
        break;
    case PC_UI_WS_COMBINE:
        f = w.first >= 0 && w.second >= 0;
        n = 2 + (f ? 1 : 0);
        break;
    default:
        break;
    }
    if (finish)
        *finish = f;
    return n;
}

int pack(const int* gems, int slots)
{
    int v = 0;
    for (int k = 0; k < 3 && k < slots; ++k)
        if (gems[k] >= 0)
            v |= (gems[k] + 1) << (k * 8);
    return v;
}

// the gems a target holds now (packed as a GEMS request's "seen")
int target_gems_now()
{
    const pc_ui_ws_row* t = target_row();
    return t ? pack(t->gems, target_gem_slots()) : 0;
}

void sound(int which) { pc_ui_shell_push(PC_UI_REQ_WS_SOUND, which, 0, 0, 0); }

// A change request (the bag changes when the bridge accepts it). `saved`: the state before the step
// that sent it (restored if the bridge refuses it).
void send(const Ws& saved, int kind, int slot, int item, int shield, int count, int flags = 0,
    const unsigned char* name = nullptr)
{
    pc_ui_request r = {};
    r.kind = kind, r.slot = slot, r.item = item, r.shield = shield, r.count = count, r.flags = flags;
    if (name)
        std::memcpy(r.name, name, NAME);
    pc_ui_shell_push_request(&r);
    before = saved;
    before_valid = true;
}

// Gems' slot menu (or a gem pick list over it) is left without Attach: the original's every way out
// of it (Cross, Triangle, the pick list's Triangle) is _attachGems(targetGems), which puts the gems
// back as they were and re-equips an equipped target (its enchantments end). The shell attaches
// nothing until Attach, so only the re-equip is left to do: PC_UI_REQ_WS_GEMS_LEAVE.
void leave_gems()
{
    if (w.flow != PC_UI_WSA_GEMS || (w.mode != PC_UI_WS_SLOTS && w.mode != PC_UI_WS_PICK) || !target_row())
        return;
    pc_ui_shell_push(PC_UI_REQ_WS_GEMS_LEAVE, w.target_list == PC_UI_WSL_SHIELDS, w.target, 0, target_gems_now());
}

// ---- flow steps ----
void to_rows()
{
    w.mode = PC_UI_WS_ROWS;
    w.flow = -1;
    w.cursor = 0;
}
void to_target(int flow, bool keep_cursor)
{
    w.mode = PC_UI_WS_TARGET;
    w.flow = flow;
    if (!keep_cursor)
        w.target_cursor = 0;
    w.cursor = w.target_cursor;
}
void open_name(bool assembling, const unsigned char* start)
{
    w.mode = PC_UI_WS_NAME;
    w.assembling = assembling;
    for (int i = 0; i < NAME; ++i)
        w.field[i] = start ? start[i] : SPACE;
    w.name_cursor = 0;
    w.grid_row = w.grid_col = 0;
    w.keyboard = false;
}
void open_confirm(int kind, int yes)
{
    w.mode = PC_UI_WS_CONFIRM;
    w.confirm_kind = kind;
    w.cursor = yes ? 0 : 1;
}

void command(int row)
{
    if (!model_valid || row < 0 || row >= PC_UI_WS_ACTIONS)
        return;
    if (!model.action_enabled[row]) {
        sound(2);
        return;
    }
    w.row = row;
    switch (row) {
    case PC_UI_WSA_ASSEMBLE:
        w.flow = row;
        w.mode = PC_UI_WS_ASSEMBLE;
        w.blade = w.grip = -1;
        w.gems[0] = w.gems[1] = w.gems[2] = -1;
        w.cursor = 0;
        break;
    case PC_UI_WSA_REPAIR:
        w.flow = row;
        open_confirm(PC_UI_WSA_REPAIR, 1);
        break;
    default:
        to_target(row, false);
        break;
    }
}

// TARGET: Circle on entry i
void target_pick(int i)
{
    int n = build_target();
    if (i < 0 || i >= n)
        return;
    const pc_ui_ws_entry& e = entries[i];
    if (!e.enabled) {
        sound(2);
        return;
    }
    w.target_cursor = i;
    switch (w.flow) {
    case PC_UI_WSA_GEMS:
        w.target_list = e.list;
        w.target = e.rec->inventory;
        for (int k = 0; k < 3; ++k)
            w.pending[k] = k < e.rec->gem_slots ? e.rec->gems[k] : -1;
        w.gems_edited = false;
        w.mode = PC_UI_WS_SLOTS;
        w.cursor = 0;
        break;
    case PC_UI_WSA_DISASSEMBLE:
        w.target_list = e.list;
        w.target = e.rec->inventory;
        open_confirm(PC_UI_WSA_DISASSEMBLE, 0);
        break;
    case PC_UI_WSA_RENAME:
        w.target_list = e.list;
        w.target = e.rec->inventory;
        open_name(false, e.rec->field);
        break;
    case PC_UI_WSA_COMBINE:
        w.combine = e.category;
        w.first = w.second = -1;
        w.mode = PC_UI_WS_COMBINE;
        w.cursor = 0;
        break;
    default:
        break;
    }
}

void open_pick(int pick, int slot)
{
    w.owner_cursor = w.cursor;
    w.pick = pick;
    w.pick_slot = slot;
    w.mode = PC_UI_WS_PICK;
    w.cursor = 0;
}

// PICK: Circle on entry i; back to the slot menu with its cursor where the original's goes
void pick_choose(int i)
{
    int n = build_pick();
    if (i < 0 || i >= n)
        return;
    const pc_ui_ws_entry& e = entries[i];
    if (!e.enabled || !e.rec) {
        sound(2);
        return;
    }
    int inv = e.rec->inventory;
    int owner = w.flow == PC_UI_WSA_ASSEMBLE ? PC_UI_WS_ASSEMBLE
        : w.flow == PC_UI_WSA_GEMS            ? PC_UI_WS_SLOTS
                                              : PC_UI_WS_COMBINE;
    int cur = w.owner_cursor;
    switch (w.pick) {
    case PC_UI_WSP_BLADE: // _assembleMenu state 3
        w.gems[0] = w.gems[1] = w.gems[2] = -1;
        if (e.current) {
            w.blade = -1;
        } else {
            w.blade = inv;
            cur = 1;
        }
        break;
    case PC_UI_WSP_GRIP: // state 4
        w.gems[0] = w.gems[1] = w.gems[2] = -1;
        if (e.current) {
            w.grip = -1;
        } else {
            w.grip = inv;
            cur = w.blade >= 0 ? 2 : 0;
        }
        break;
    case PC_UI_WSP_GEM: { // state 5 / _attachGemsMenu state 6
        int* slots = w.flow == PC_UI_WSA_ASSEMBLE ? w.gems : w.pending;
        int s = w.pick_slot < 0 || w.pick_slot > 2 ? 0 : w.pick_slot;
        if (w.flow == PC_UI_WSA_GEMS) // _attachGemsMenu state 6: every pick changes the slot (and re-equips)
            w.gems_edited = true;
        if (e.current) {
            slots[s] = -1;
        } else {
            slots[s] = inv;
            ++cur;
        }
        break;
    }
    case PC_UI_WSP_COMBINE: { // _combineBladeMenu state 3
        int s = w.pick_slot ? 1 : 0;
        int& slot = s ? w.second : w.first;
        if (e.current) {
            slot = -1;
            cur = s;
        } else {
            slot = inv;
            cur = s + 1;
            if (s == 1 && w.first < 0)
                cur = 0;
        }
        break;
    }
    default:
        break;
    }
    w.mode = owner;
    bool f;
    int rows = slot_rows(&f);
    w.cursor = cur < 0 ? 0 : cur >= rows ? rows - 1 : cur;
}

// ---- the name ----
// MENU8's Circle on a grid character (21A0.c:637): written over the character at the cursor, the cursor
// one on (not past the last place)
void name_put(unsigned char c)
{
    int p = w.name_cursor < 0 ? 0 : w.name_cursor > NAME - 1 ? NAME - 1 : w.name_cursor;
    w.field[p] = c;
    if (p < NAME - 1)
        ++p;
    w.name_cursor = p;
}
// The keyboard types as text fields do (a divergence MENU8 has no keyboard for): inserted at the cursor,
// the last place's character pushed out
void name_insert(unsigned char c)
{
    int p = w.name_cursor < 0 ? 0 : w.name_cursor > NAME - 1 ? NAME - 1 : w.name_cursor;
    for (int i = NAME - 1; i > p; --i)
        w.field[i] = w.field[i - 1];
    w.field[p] = c;
    if (p < NAME - 1)
        ++p;
    w.name_cursor = p;
}
// MENU8's Cross: the character before the cursor out
void name_backspace()
{
    if (w.name_cursor <= 0)
        return;
    --w.name_cursor;
    for (int i = w.name_cursor; i < NAME - 1; ++i)
        w.field[i] = w.field[i + 1];
    w.field[NAME - 1] = SPACE;
}
// MENU8's Square: the character at the cursor out
void name_delete()
{
    for (int i = w.name_cursor; i < NAME - 1; ++i)
        w.field[i] = w.field[i + 1];
    w.field[NAME - 1] = SPACE;
}
void name_done()
{
    Ws saved = w;
    if (w.assembling) {
        send(saved, PC_UI_REQ_WS_ASSEMBLE, w.blade, w.grip, pack(w.gems, assembly_gem_slots()), 0, 0, w.field);
        to_rows();
        w.row = PC_UI_WSA_ASSEMBLE;
    } else {
        send(saved, PC_UI_REQ_WS_RENAME, 0, w.target, -1, 0, 0, w.field);
        to_target(PC_UI_WSA_RENAME, true);
    }
}
void name_tick(const pc_ui_input* in)
{
    if (in->pressed & PC_UI_TAB_PREV)
        w.name_cursor = w.name_cursor > 0 ? w.name_cursor - 1 : 0;
    if (in->pressed & PC_UI_TAB_NEXT)
        w.name_cursor = w.name_cursor < NAME - 1 ? w.name_cursor + 1 : NAME - 1;
    if (in->repeat & PC_UI_UP)
        w.grid_row = w.grid_row == 0 ? GRID_ROWS : w.grid_row - 1;
    if (in->repeat & PC_UI_DOWN)
        w.grid_row = w.grid_row == GRID_ROWS ? 0 : w.grid_row + 1;
    int cols = w.grid_row == GRID_ROWS ? PC_UI_WSN_ACTIONS : GRID_COLS;
    if (w.grid_col >= cols)
        w.grid_col = cols - 1;
    if (in->repeat & PC_UI_LEFT)
        w.grid_col = w.grid_col == 0 ? cols - 1 : w.grid_col - 1;
    if (in->repeat & PC_UI_RIGHT)
        w.grid_col = w.grid_col == cols - 1 ? 0 : w.grid_col + 1;
    if (in->repeat & PC_UI_ALT) {
        w.keyboard = false;
        name_backspace();
    }
    if (in->pressed & PC_UI_CONFIRM) {
        w.keyboard = false;
        if (w.grid_row < GRID_ROWS)
            name_put(model.grid[w.grid_row][w.grid_col]);
        else if (w.grid_col == PC_UI_WSN_DELETE)
            name_backspace();
        else
            name_done();
    }
}

// the field as UTF-8 (trailing blanks cut)
std::string field_text(const unsigned char* field)
{
    std::string s;
    size_t keep = 0;
    for (int i = 0; i < NAME; ++i) {
        unsigned char c = field[i];
        const char* g = model_valid ? model.glyph[c] : "";
        s += c == SPACE ? " " : g;
        if (c != SPACE)
            keep = s.size();
    }
    s.resize(keep);
    return s;
}

// a grid code for one UTF-8 character ("" none)
int grid_code(const char* ch, int len)
{
    if (len == 1 && ch[0] == ' ')
        return SPACE;
    for (int r = 0; r < GRID_ROWS; ++r)
        for (int c = 0; c < GRID_COLS; ++c) {
            unsigned char code = model.grid[r][c];
            const char* g = model.glyph[code];
            if (code != SPACE && (int)std::strlen(g) == len && !std::memcmp(g, ch, len))
                return code;
        }
    return -1;
}

void clamp()
{
    if (w.row < 0 || w.row >= PC_UI_WS_ACTIONS)
        w.row = 0;
    if (!model_valid)
        return;
    // a record a flow works on is gone (the bridge changed the bag): back out
    if ((w.mode == PC_UI_WS_SLOTS || (w.mode == PC_UI_WS_NAME && !w.assembling)
            || (w.mode == PC_UI_WS_CONFIRM && w.confirm_kind == PC_UI_WSA_DISASSEMBLE))
        && !target_row())
        to_target(w.flow, true);
    if (w.mode == PC_UI_WS_PICK && w.pick == PC_UI_WSP_GEM && w.flow == PC_UI_WSA_GEMS && !target_row())
        to_target(w.flow, true);
    if (w.flow == PC_UI_WSA_ASSEMBLE) {
        if (w.blade >= 0 && !find(PC_UI_WSL_BLADES, w.blade))
            w.blade = -1;
        if (w.grip >= 0 && !find(PC_UI_WSL_GRIPS, w.grip))
            w.grip = -1;
    }
    if (w.flow == PC_UI_WSA_COMBINE) {
        int l = COMBINE_LISTS[w.combine < 0 || w.combine > 2 ? 0 : w.combine];
        if (w.first >= 0 && !find(l, w.first))
            w.first = -1;
        if (w.second >= 0 && !find(l, w.second))
            w.second = -1;
    }
    int n = 0;
    if (w.mode == PC_UI_WS_TARGET || w.mode == PC_UI_WS_PICK) {
        n = build_entries();
        if (w.cursor >= n)
            w.cursor = n ? n - 1 : 0;
    } else if (w.mode == PC_UI_WS_ASSEMBLE || w.mode == PC_UI_WS_SLOTS || w.mode == PC_UI_WS_COMBINE) {
        n = slot_rows(nullptr);
        if (w.cursor >= n)
            w.cursor = n ? n - 1 : 0;
    } else if (w.mode == PC_UI_WS_CONFIRM) {
        w.cursor = w.cursor ? 1 : 0;
    }
    if (w.cursor < 0)
        w.cursor = 0;
}

void confirm_yes()
{
    Ws saved = w;
    switch (w.confirm_kind) {
    case PC_UI_WSA_DISASSEMBLE: {
        const pc_ui_ws_row* t = target_row();
        bool shield = w.target_list == PC_UI_WSL_SHIELDS;
        send(saved, PC_UI_REQ_WS_DISASSEMBLE, shield, w.target, t ? row_of(w.target_list, w.target) : -1, 0);
        to_target(PC_UI_WSA_DISASSEMBLE, true);
        break;
    }
    case PC_UI_WSA_REPAIR:
        send(saved, PC_UI_REQ_WS_REPAIR, 0, 0, 0, 0);
        to_rows();
        w.row = PC_UI_WSA_REPAIR;
        break;
    case PC_UI_WSA_COMBINE:
        send(saved, PC_UI_REQ_WS_COMBINE, w.combine, w.first, w.second, 0);
        to_rows();
        w.row = PC_UI_WSA_COMBINE;
        break;
    default:
        to_rows();
        break;
    }
}
void confirm_no()
{
    switch (w.confirm_kind) {
    case PC_UI_WSA_DISASSEMBLE:
        to_target(PC_UI_WSA_DISASSEMBLE, true);
        break;
    case PC_UI_WSA_COMBINE:
        w.mode = PC_UI_WS_COMBINE;
        w.cursor = 2;
        break;
    default:
        to_rows();
        w.row = PC_UI_WSA_REPAIR;
        break;
    }
}

// the record or candidate a pick list's focused entry puts in (-1: what is there now)
int hovered()
{
    if (w.mode != PC_UI_WS_PICK)
        return -2;
    int n = build_pick();
    if (w.cursor < 0 || w.cursor >= n || !entries[w.cursor].rec || entries[w.cursor].current)
        return -2;
    return entries[w.cursor].rec->inventory;
}

} // namespace

extern "C" void pc_ui_workshop_set(const pc_ui_workshop* m)
{
    model = *m;
    model_valid = m->valid != 0;
    clamp();
}
extern "C" const pc_ui_workshop* pc_ui_workshop_model(void) { return &model; }

extern "C" void pc_ui_ws_reset(void)
{
    to_rows();
    w.message = 0;
    before_valid = false;
}
extern "C" void pc_ui_ws_leave(void)
{
    if (model_valid)
        leave_gems();
    pc_ui_ws_reset();
}
extern "C" int pc_ui_ws_base(void) { return w.mode == PC_UI_WS_ROWS; }
extern "C" int pc_ui_ws_takes_tabs(void) { return w.mode == PC_UI_WS_NAME; }

extern "C" int pc_ui_ws_back(void)
{
    switch (w.mode) {
    case PC_UI_WS_ROWS:
        return 0;
    case PC_UI_WS_TARGET:
    case PC_UI_WS_ASSEMBLE:
        to_rows();
        break;
    case PC_UI_WS_SLOTS: // the original's Cross puts the gems back as they were (_attachGems(targetGems))
        leave_gems();
        to_target(PC_UI_WSA_GEMS, true);
        break;
    case PC_UI_WS_COMBINE:
        to_target(PC_UI_WSA_COMBINE, true);
        break;
    case PC_UI_WS_PICK:
        w.mode = w.flow == PC_UI_WSA_ASSEMBLE ? PC_UI_WS_ASSEMBLE
            : w.flow == PC_UI_WSA_GEMS        ? PC_UI_WS_SLOTS
                                              : PC_UI_WS_COMBINE;
        w.cursor = w.owner_cursor;
        break;
    case PC_UI_WS_NAME:
        if (w.assembling) {
            w.mode = PC_UI_WS_ASSEMBLE;
            bool f;
            w.cursor = slot_rows(&f) - 1;
        } else {
            to_target(PC_UI_WSA_RENAME, true);
        }
        break;
    case PC_UI_WS_CONFIRM:
        confirm_no();
        break;
    default:
        to_rows();
        break;
    }
    sound(0);
    clamp();
    return 1;
}

extern "C" void pc_ui_ws_tick(const pc_ui_input* in)
{
    if (w.message > 0)
        --w.message;
    clamp();
    if (!model_valid)
        return;
    switch (w.mode) {
    case PC_UI_WS_ROWS:
        if (in->repeat & PC_UI_DOWN)
            w.row = (w.row + 1) % PC_UI_WS_ACTIONS;
        if (in->repeat & PC_UI_UP)
            w.row = (w.row + PC_UI_WS_ACTIONS - 1) % PC_UI_WS_ACTIONS;
        if (in->pressed & PC_UI_CONFIRM)
            command(w.row);
        break;
    case PC_UI_WS_TARGET:
    case PC_UI_WS_PICK: {
        int n = build_entries();
        if (n && (in->repeat & PC_UI_DOWN))
            w.cursor = (w.cursor + 1) % n;
        if (n && (in->repeat & PC_UI_UP))
            w.cursor = (w.cursor + n - 1) % n;
        if (w.mode == PC_UI_WS_TARGET)
            w.target_cursor = w.cursor;
        if (in->pressed & PC_UI_CONFIRM) {
            if (w.mode == PC_UI_WS_TARGET)
                target_pick(w.cursor);
            else
                pick_choose(w.cursor);
        }
        break;
    }
    case PC_UI_WS_ASSEMBLE:
    case PC_UI_WS_SLOTS:
    case PC_UI_WS_COMBINE: {
        bool finish;
        int n = slot_rows(&finish);
        if (n && (in->repeat & PC_UI_DOWN))
            w.cursor = (w.cursor + 1) % n;
        if (n && (in->repeat & PC_UI_UP))
            w.cursor = (w.cursor + n - 1) % n;
        if (!(in->pressed & PC_UI_CONFIRM))
            break;
        bool last = finish && w.cursor == n - 1;
        if (w.mode == PC_UI_WS_ASSEMBLE) {
            if (last)
                open_name(true, nullptr);
            else if (w.cursor == 0)
                open_pick(PC_UI_WSP_BLADE, 0);
            else if (w.cursor == 1)
                open_pick(PC_UI_WSP_GRIP, 0);
            else
                open_pick(PC_UI_WSP_GEM, w.cursor - 2);
        } else if (w.mode == PC_UI_WS_SLOTS) {
            if (last) {
                bool shield = w.target_list == PC_UI_WSL_SHIELDS;
                send(w, PC_UI_REQ_WS_GEMS, shield, w.target, pack(w.pending, target_gem_slots()), target_gems_now(),
                    w.gems_edited ? PC_UI_WS_EDITED : 0);
                to_target(PC_UI_WSA_GEMS, true);
            } else {
                open_pick(PC_UI_WSP_GEM, w.cursor);
            }
        } else {
            if (last)
                open_confirm(PC_UI_WSA_COMBINE, 0);
            else
                open_pick(PC_UI_WSP_COMBINE, w.cursor);
        }
        break;
    }
    case PC_UI_WS_NAME:
        name_tick(in);
        break;
    case PC_UI_WS_CONFIRM:
        if (in->repeat & (PC_UI_UP | PC_UI_DOWN))
            w.cursor = !w.cursor;
        if (in->pressed & PC_UI_CONFIRM) {
            if (w.cursor == 0)
                confirm_yes();
            else {
                sound(0);
                confirm_no();
            }
        }
        break;
    default:
        to_rows();
        break;
    }
    clamp();
}

extern "C" int pc_ui_ws_mouse_focus(int kind, int index)
{
    if (!model_valid)
        return 0;
    switch (kind) {
    case PC_UI_MOUSE_WS_ROW:
        if (w.mode != PC_UI_WS_ROWS || index < 0 || index >= PC_UI_WS_ACTIONS)
            return 0;
        w.row = index;
        return 1;
    case PC_UI_MOUSE_WS_ENTRY:
        if ((w.mode != PC_UI_WS_TARGET && w.mode != PC_UI_WS_PICK) || index < 0 || index >= build_entries())
            return 0;
        w.cursor = index;
        if (w.mode == PC_UI_WS_TARGET)
            w.target_cursor = index;
        return 1;
    case PC_UI_MOUSE_WS_SLOT:
        if ((w.mode != PC_UI_WS_ASSEMBLE && w.mode != PC_UI_WS_SLOTS && w.mode != PC_UI_WS_COMBINE) || index < 0
            || index >= slot_rows(nullptr))
            return 0;
        w.cursor = index;
        return 1;
    case PC_UI_MOUSE_WS_GRID: {
        if (w.mode != PC_UI_WS_NAME || index < 0)
            return 0;
        int r = index / GRID_COLS, c = index % GRID_COLS;
        if (r > GRID_ROWS || (r == GRID_ROWS && c >= PC_UI_WSN_ACTIONS))
            return 0;
        w.grid_row = r, w.grid_col = c;
        return 1;
    }
    case PC_UI_MOUSE_WS_CONFIRM:
        if (w.mode != PC_UI_WS_CONFIRM || index < 0 || index > 1)
            return 0;
        w.cursor = index;
        return 1;
    default:
        return 0;
    }
}

extern "C" int pc_ui_ws_mouse_click(int kind, int index)
{
    if (!model_valid)
        return 1;
    if (kind == PC_UI_MOUSE_WS_PLACE) {
        if (w.mode == PC_UI_WS_NAME && index >= 0 && index < NAME)
            w.name_cursor = index;
        return 1;
    }
    // a command clicked while a flow is open: back to the commands on it (no confirm)
    if (kind == PC_UI_MOUSE_WS_ROW && w.mode != PC_UI_WS_ROWS) {
        if (index >= 0 && index < PC_UI_WS_ACTIONS) {
            leave_gems();
            to_rows();
            w.row = index;
        }
        return 1;
    }
    return 0;
}

extern "C" int pc_ui_shell_workshop_wanted(int key[6])
{
    for (int k = 0; k < 6; ++k)
        key[k] = 0;
    if (!pc_ui_shell_workshop_shown() || !model_valid)
        return 0;
    int h = hovered();
    switch (w.flow) {
    case PC_UI_WSA_ASSEMBLE: {
        if (w.mode != PC_UI_WS_ASSEMBLE && w.mode != PC_UI_WS_PICK && w.mode != PC_UI_WS_NAME)
            return 0;
        int b = w.blade, g = w.grip, gems[3] = {w.gems[0], w.gems[1], w.gems[2]};
        if (h >= -1 && w.mode == PC_UI_WS_PICK) {
            if (w.pick == PC_UI_WSP_BLADE)
                b = h, gems[0] = gems[1] = gems[2] = -1;
            else if (w.pick == PC_UI_WSP_GRIP)
                g = h, gems[0] = gems[1] = gems[2] = -1;
            else if (w.pick == PC_UI_WSP_GEM && w.pick_slot >= 0 && w.pick_slot < 3)
                gems[w.pick_slot] = h;
        }
        if (b < 0 && g < 0)
            return 0;
        const pc_ui_ws_row* grip = find(PC_UI_WSL_GRIPS, g);
        key[0] = PC_UI_WSA_ASSEMBLE, key[1] = b, key[2] = g;
        key[3] = b >= 0 && grip ? pack(gems, grip->gem_slots) : 0;
        return 1;
    }
    case PC_UI_WSA_GEMS: {
        if (w.mode != PC_UI_WS_SLOTS && w.mode != PC_UI_WS_PICK)
            return 0;
        int gems[3] = {w.pending[0], w.pending[1], w.pending[2]};
        if (h >= -1 && w.mode == PC_UI_WS_PICK && w.pick_slot >= 0 && w.pick_slot < 3)
            gems[w.pick_slot] = h;
        if (!target_row())
            return 0;
        key[0] = PC_UI_WSA_GEMS, key[1] = w.target_list == PC_UI_WSL_SHIELDS, key[2] = w.target;
        key[3] = pack(gems, target_gem_slots());
        return 1;
    }
    case PC_UI_WSA_COMBINE: {
        if (w.mode != PC_UI_WS_COMBINE && w.mode != PC_UI_WS_PICK && w.mode != PC_UI_WS_CONFIRM)
            return 0;
        int a = w.first, b = w.second;
        if (h >= -1 && w.mode == PC_UI_WS_PICK)
            (w.pick_slot ? b : a) = h;
        if (a < 0 || b < 0)
            return 0;
        key[0] = PC_UI_WSA_COMBINE, key[1] = w.combine, key[2] = a, key[3] = b;
        return 1;
    }
    default:
        return 0;
    }
}

// The bridge refused the last change request (on the tick it was sent: requests are carried out right
// after the shell's tick): back to the step that sent it, with the line
extern "C" void pc_ui_workshop_refused(void)
{
    if (before_valid) {
        w = before;
        before_valid = false;
        clamp();
    }
    w.message = MESSAGE_TICKS;
}

extern "C" int pc_ui_workshop_typing(void)
{
    return pc_ui_shell_visible() && pc_ui_shell_workshop_shown() && w.mode == PC_UI_WS_NAME && model_valid;
}
extern "C" void pc_ui_workshop_text(const char* utf8)
{
    if (!pc_ui_workshop_typing() || !utf8)
        return;
    for (const char* p = utf8; *p;) {
        int len = 1;
        unsigned char b = (unsigned char)*p;
        if (b >= 0xF0)
            len = 4;
        else if (b >= 0xE0)
            len = 3;
        else if (b >= 0xC0)
            len = 2;
        int got = 0;
        while (got < len && p[got])
            ++got;
        int code = grid_code(p, got);
        if (code >= 0) {
            name_insert((unsigned char)code);
            w.keyboard = true;
        }
        p += got;
    }
}
extern "C" void pc_ui_workshop_key(int key)
{
    if (!pc_ui_workshop_typing())
        return;
    w.keyboard = true;
    switch (key) {
    case PC_UI_WSK_BACKSPACE:
        name_backspace();
        break;
    case PC_UI_WSK_DELETE:
        name_delete();
        break;
    case PC_UI_WSK_LEFT:
        w.name_cursor = w.name_cursor > 0 ? w.name_cursor - 1 : 0;
        break;
    case PC_UI_WSK_RIGHT:
        w.name_cursor = w.name_cursor < NAME - 1 ? w.name_cursor + 1 : NAME - 1;
        break;
    case PC_UI_WSK_HOME:
        w.name_cursor = 0;
        break;
    case PC_UI_WSK_END: { // after the last character
        int last = -1;
        for (int i = 0; i < NAME; ++i)
            if (w.field[i] != SPACE)
                last = i;
        w.name_cursor = last + 1 > NAME - 1 ? NAME - 1 : last + 1;
        break;
    }
    case PC_UI_WSK_ENTER:
        name_done();
        break;
    case PC_UI_WSK_ESCAPE:
        pc_ui_ws_back();
        break;
    default:
        break;
    }
}

extern "C" void pc_ui_workshop_view_get(pc_ui_workshop_view* v)
{
    clamp();
    std::memset(v, 0, sizeof *v);
    v->valid = model_valid;
    v->mode = w.mode;
    v->flow = w.mode == PC_UI_WS_ROWS ? -1 : w.flow;
    v->row = w.row;
    v->cursor = w.cursor;
    v->entries = build_entries();
    for (int i = 0; i < v->entries; ++i)
        v->entry[i] = entries[i];
    bool finish = false;
    v->slots = slot_rows(&finish);
    v->finish = finish;
    v->target = target_row();
    v->target_list = w.target_list;
    if (w.flow == PC_UI_WSA_ASSEMBLE) {
        v->blade = find(PC_UI_WSL_BLADES, w.blade);
        v->grip = find(PC_UI_WSL_GRIPS, w.grip);
        v->gem_slots = assembly_gem_slots();
        for (int k = 0; k < 3; ++k)
            v->gems[k] = k < v->gem_slots ? find(PC_UI_WSL_GEMS, w.gems[k]) : nullptr;
    } else if (w.flow == PC_UI_WSA_GEMS) {
        v->gem_slots = target_gem_slots();
        for (int k = 0; k < 3; ++k)
            v->gems[k] = k < v->gem_slots ? find(PC_UI_WSL_GEMS, w.pending[k]) : nullptr;
    }
    v->combine = w.combine;
    if (w.flow == PC_UI_WSA_COMBINE) {
        int l = COMBINE_LISTS[w.combine < 0 || w.combine > 2 ? 0 : w.combine];
        v->first = find(l, w.first);
        v->second = find(l, w.second);
    }
    v->pick = w.pick;
    v->pick_slot = w.pick_slot;
    v->confirm_kind = w.confirm_kind;
    v->prompt = nullptr;
    if (w.mode == PC_UI_WS_CONFIRM) {
        switch (w.confirm_kind) {
        case PC_UI_WSA_DISASSEMBLE: {
            bool shield = w.target_list == PC_UI_WSL_SHIELDS;
            bool equipped = v->target && v->target->mark == PC_UI_MARK_EQUIPPED;
            v->prompt = text(shield ? (equipped ? T_SHIELD_EQUIPPED_WARN : T_DISASSEMBLE_SHIELD_PROMPT)
                                    : (equipped ? T_WEAPON_EQUIPPED_WARN : T_DISASSEMBLE_WEAPON_CONFIRM));
            break;
        }
        case PC_UI_WSA_REPAIR:
            v->prompt = text(T_REPAIR_CONFIRM);
            break;
        case PC_UI_WSA_COMBINE: { // _combine*Menu state 4: a part of a set, equipped or worn warns
            bool warn = (v->first && v->first->mark != PC_UI_MARK_NONE)
                || (v->second && v->second->mark != PC_UI_MARK_NONE);
            if (w.combine == 1) // shields: any gem set in them too
                for (const pc_ui_ws_row* r : {v->first, v->second})
                    for (int k = 0; r && k < 3 && k < r->gem_slots; ++k)
                        warn = warn || r->gems[k] >= 0;
            v->prompt = text(warn ? T_CANCEL_COMBINE_CONFIRM : T_ATTACHMENT_CONFIRM);
            break;
        }
        default:
            break;
        }
    }
    std::string name = field_text(w.field);
    std::snprintf(v->name, sizeof v->name, "%s", name.c_str());
    std::memcpy(v->field, w.field, NAME);
    v->name_cursor = w.name_cursor;
    v->grid_row = w.grid_row;
    v->grid_col = w.grid_col;
    v->keyboard = w.keyboard;
    v->assembling = w.assembling;
    int key[6];
    int kind = pc_ui_shell_workshop_wanted(key);
    bool same = kind != 0;
    for (int k = 0; k < 6 && same; ++k)
        same = model.preview_key[k] == key[k];
    v->preview = same && model.preview_valid ? &model.preview : nullptr;
    v->preview_category = model.preview_category;
    v->message = w.message > 0 ? "That cannot be done right now." : nullptr;
}

namespace {
std::string esc(const char* s)
{
    std::string o;
    for (; s && *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            o += '\\', o += (char)c;
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else
            o += (char)c;
    }
    return o;
}
const char* const MODE_IDS[] = {"rows", "target", "assemble", "slots", "combine", "pick", "name", "confirm"};
const char* const FLOW_IDS[] = {"assemble", "gems", "disassemble", "rename", "repair", "combine"};
const char* const PICK_IDS[] = {"blade", "grip", "gem", "combine"};
const char* const LIST_IDS[] = {"weapons", "blades", "grips", "shields", "armor", "gems"};
int inv_of(const pc_ui_ws_row* r) { return r ? r->inventory : -1; }
} // namespace

extern "C" void pc_ui_ws_json(char* out, unsigned size)
{
    static pc_ui_workshop_view v;
    pc_ui_workshop_view_get(&v);
    std::string j = ",\"workshop\":{";
    char buf[512];
    std::snprintf(buf, sizeof buf,
        "\"valid\":%s,\"workshop\":%d,\"materials\":%u,\"busy\":%s,\"locked\":%s,\"mode\":\"%s\",\"flow\":\"%s\","
        "\"row\":%d,\"cursor\":%d,\"weapon_slot\":%d",
        v.valid ? "true" : "false", model.workshop, model.materials, model.busy ? "true" : "false",
        model.locked ? "true" : "false", MODE_IDS[v.mode], v.flow >= 0 && v.flow < 6 ? FLOW_IDS[v.flow] : "",
        v.row, v.cursor, model.weapon_slot);
    j += buf;
    static const char* const LABELS[PC_UI_WS_ACTIONS] = {"Assemble", "Gems", "Disassemble", "Rename", "Repair",
        "Combine"};
    j += ",\"rows\":[";
    for (int i = 0; i < PC_UI_WS_ACTIONS; ++i) {
        int why = model.action_reason[i];
        const char* reason = !model.action_enabled[i] ? (why >= 0 ? text(why) : model.locked_reason) : nullptr;
        std::snprintf(buf, sizeof buf, "%s{\"id\":\"%s\",\"label\":\"%s\",\"enabled\":%s", i ? "," : "", FLOW_IDS[i],
            LABELS[i], model_valid && model.action_enabled[i] ? "true" : "false");
        j += buf;
        if (reason)
            j += ",\"reason\":\"" + esc(reason) + "\"";
        j += "}";
    }
    j += "]";
    if (v.mode == PC_UI_WS_TARGET || v.mode == PC_UI_WS_PICK) {
        if (v.mode == PC_UI_WS_PICK) {
            std::snprintf(buf, sizeof buf, ",\"pick\":\"%s\",\"pick_slot\":%d", PICK_IDS[v.pick], v.pick_slot);
            j += buf;
        }
        j += ",\"list\":[";
        for (int i = 0; i < v.entries; ++i) {
            const pc_ui_ws_entry& e = v.entry[i];
            std::snprintf(buf, sizeof buf,
                "%s{\"entry\":%d,\"list\":\"%s\",\"inventory\":%d,\"enabled\":%s,\"current\":%s,\"category\":%d,"
                "\"count\":%d,\"label\":\"",
                i ? "," : "", i, e.list >= 0 && e.list < 6 ? LIST_IDS[e.list] : "", inv_of(e.rec),
                e.enabled ? "true" : "false", e.current ? "true" : "false", e.category, e.count);
            j += buf;
            j += esc(e.label) + "\"";
            if (e.reason)
                j += ",\"reason\":\"" + esc(e.reason) + "\"";
            j += "}";
        }
        j += "]";
    }
    std::snprintf(buf, sizeof buf,
        ",\"slots\":%d,\"finish\":%s,\"target\":{\"list\":\"%s\",\"inventory\":%d},\"blade\":%d,\"grip\":%d,"
        "\"gems\":[%d,%d,%d],\"gem_slots\":%d,\"combine\":%d,\"first\":%d,\"second\":%d",
        v.slots, v.finish ? "true" : "false",
        v.target && v.target_list >= 0 && v.target_list < 6 ? LIST_IDS[v.target_list] : "", inv_of(v.target),
        inv_of(v.blade), inv_of(v.grip), inv_of(v.gems[0]), inv_of(v.gems[1]), inv_of(v.gems[2]), v.gem_slots,
        v.combine, inv_of(v.first), inv_of(v.second));
    j += buf;
    if (v.mode == PC_UI_WS_NAME) {
        std::snprintf(buf, sizeof buf, ",\"name\":{\"cursor\":%d,\"grid\":[%d,%d],\"assembling\":%s,\"text\":\"",
            v.name_cursor, v.grid_row, v.grid_col, v.assembling ? "true" : "false");
        j += buf;
        j += esc(v.name) + "\",\"field\":\"";
        for (int i = 0; i < NAME; ++i) {
            std::snprintf(buf, sizeof buf, "%02x", v.field[i]);
            j += buf;
        }
        j += "\",\"grid_codes\":\"";
        for (int r = 0; r < GRID_ROWS; ++r)
            for (int c = 0; c < GRID_COLS; ++c) {
                std::snprintf(buf, sizeof buf, "%02x", model.grid[r][c]);
                j += buf;
            }
        j += "\"}";
    }
    if (v.mode == PC_UI_WS_CONFIRM) {
        std::snprintf(buf, sizeof buf, ",\"confirm\":{\"kind\":\"%s\",\"yes\":%s,\"prompt\":\"",
            v.confirm_kind >= 0 && v.confirm_kind < 6 ? FLOW_IDS[v.confirm_kind] : "", v.cursor == 0 ? "true" : "false");
        j += buf;
        j += esc(v.prompt) + "\"}";
    }
    if (v.preview) {
        const pc_ui_item_details& d = *v.preview;
        std::snprintf(buf, sizeof buf,
            ",\"preview\":{\"category\":%d,\"dp\":%d,\"dp_max\":%d,\"pp\":%d,\"pp_max\":%d,\"str\":%d,\"int\":%d,"
            "\"agl\":%d,\"name\":\"",
            v.preview_category, d.stats.dp, d.stats.dp_max, d.stats.pp, d.stats.pp_max, d.stats.str, d.stats.intel,
            d.stats.agl);
        j += buf;
        j += esc(d.stats.name) + "\",\"material\":\"" + esc(d.stats.material) + "\"}";
    }
    if (model_valid && (v.mode == PC_UI_WS_CONFIRM && v.confirm_kind == PC_UI_WSA_REPAIR)) {
        j += ",\"repairs\":[";
        for (int i = 0; i < model.repair_count && i < PC_UI_WS_REPAIRS; ++i) {
            const pc_ui_ws_repair& r = model.repairs[i];
            std::snprintf(buf, sizeof buf,
                "%s{\"category\":%d,\"inventory\":%d,\"dp\":%d,\"dp_max\":%d,\"pp\":%d,\"pp_max\":%d,\"dp_after\":%d,"
                "\"pp_after\":%d}",
                i ? "," : "", r.category, r.inventory, r.dp, r.dp_max, r.pp, r.pp_max, r.dp_after, r.pp_after);
            j += buf;
        }
        j += "]";
    }
    if (v.message)
        j += ",\"message\":\"" + esc(v.message) + "\"";
    j += "}";
    std::snprintf(out, size, "%s", j.c_str());
}
