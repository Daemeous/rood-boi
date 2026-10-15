// Modern menus: the Loot screen's state (ui_shell.h, "The Loot screen").
// Pure state like ui_shell.cpp: the bridge (loot_bridge.c) sets the snapshot
// and carries out the requests; drawing is ui_loot_draw.cpp.
#include "ui/ui_shell.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const int QUEUE = 16;
const char* const CATEGORY_IDS[PC_UI_ITEM_CATEGORIES] = {"weapons", "blades", "grips", "shields", "armor", "gems",
    "misc"};
const char* const MODE_IDS[] = {"list", "organize", "confirm", "message", "bag", "bag_confirm", "bag_actions",
    "bag_quantity", "bag_sort", "bag_move"};
const char* const BAG_ACTION_IDS[PC_UI_ITA_COUNT] = {"use", "search", "discard", "discard_all", "sort", "move",
    "original"};
const int STEPS[3] = {1, 10, 100}; // _discardMenu discardSteps
const char* const ASK_IDS[] = {"disassemble", "consolidate", "leave", "keys", "empty"};
const char* const ORG_IDS[] = {"organize", "disassemble", "consolidate"};

struct Mouse {
    int hover_kind = PC_UI_MOUSE_NONE, hover_index = 0;
    int click_kind = PC_UI_MOUSE_NONE, click_index = 0;
    float wheel = 0;
};

struct State {
    bool visible = false;
    int mode = PC_UI_LOOT_LIST;
    int cursor = 0;
    int menu = 0;          // ORGANIZE entry / CONFIRM 0 Yes 1 No
    int ask = PC_UI_LOOT_ASK_LEAVE;
    int bag_category = PC_UI_IC_WEAPON;
    int bag_cursor[PC_UI_ITEM_CATEGORIES] = {};
    int bag_record = -1;   // BAG_CONFIRM: the record asked about
    int bag_menu = 0;      // BAG_ACTIONS: the focused action
    int confirm_kind = PC_UI_ITA_DISCARD; // BAG_CONFIRM: what it confirms
    int count = 0, count_max = 0, step = 0; // BAG_QUANTITY: the quantity picker
    int sort = 0;          // BAG_SORT: the stat's row
    int move_from = -1;    // BAG_MOVE: the row being moved
    bool emptied = false;  // the empty message was shown (the list came empty)
    pc_ui_request queue[QUEUE] = {};
    int q_head = 0, q_count = 0;
    Mouse mouse;
} s;
pc_ui_loot model;
pc_ui_items bag;

void push(int kind, int slot, int item, int shield, int count = 0)
{
    if (s.q_count == QUEUE)
        return;
    pc_ui_request& r = s.queue[(s.q_head + s.q_count++) % QUEUE];
    r = pc_ui_request();
    r.kind = kind, r.slot = slot, r.item = item, r.shield = shield, r.count = count;
}

int rows() { return model.valid ? (model.count < PC_UI_LOOT_ROWS ? model.count : PC_UI_LOOT_ROWS) : 0; }
const pc_ui_loot_row* row()
{
    int n = rows();
    return n > 0 && s.cursor >= 0 && s.cursor < n ? &model.rows[s.cursor] : nullptr;
}
void clamp()
{
    int n = rows();
    if (s.cursor >= n)
        s.cursor = n - 1;
    if (s.cursor < 0)
        s.cursor = 0;
}

// ORGANIZE's entries: the row's own action (if any), then Organize inventory.
int org_entries(int* kinds)
{
    const pc_ui_loot_row* r = row();
    int n = 0;
    if (r && r->organize != PC_UI_LOOT_ORG_NONE)
        kinds[n++] = r->organize;
    kinds[n++] = PC_UI_LOOT_ORG_NONE;
    return n;
}

// The bag (BAG): the category's rows.
int bag_rows(int c) { return bag.valid && c >= 0 && c < PC_UI_ITEM_CATEGORIES ? bag.count[c] : 0; }
const pc_ui_item_row* bag_row(int c, int r)
{
    if (r < 0 || r >= bag_rows(c) || bag.first[c] + r >= PC_UI_ITEM_ROWS)
        return nullptr;
    return &bag.rows[bag.first[c] + r];
}
void bag_clamp()
{
    int c = s.bag_category, n = bag_rows(c);
    if (s.bag_cursor[c] >= n)
        s.bag_cursor[c] = n - 1;
    if (s.bag_cursor[c] < 0)
        s.bag_cursor[c] = 0;
}
int bag_sorts(int c)
{
    int n = bag.valid && c >= 0 && c < PC_UI_ITEM_CATEGORIES ? bag.sort_count[c] : 0;
    return n < 0 ? 0 : n > PC_UI_SORT_STATS ? PC_UI_SORT_STATS : n;
}
// The focused bag row's actions, as MENUB's Items menu offers them (items2.h populateActions without
// _ITEMMENU: its Use, never selectable there, and Search left out): Discard, Discard all (misc), Sort; Move
// (its list's second pick, state 6) as an action like the Items screen's.
struct BagAction {
    int kind;
    bool enabled;
    const char* reason;
};
int bag_actions(BagAction* out)
{
    int c = s.bag_category, n = 0;
    const pc_ui_item_row* e = bag_row(c, s.bag_cursor[c]);
    if (!e)
        return 0;
    out[n++] = {PC_UI_ITA_DISCARD, e->discard != 0, e->discard ? nullptr : e->discard_reason};
    if (c == PC_UI_IC_MISC)
        out[n++] = {PC_UI_ITA_DISCARD_ALL, e->discard_all != 0, e->discard_all ? nullptr : e->discard_all_reason};
    out[n++] = {PC_UI_ITA_SORT, bag_sorts(c) > 0, nullptr};
    bool two = bag_rows(c) >= 2;
    out[n++] = {PC_UI_ITA_MOVE, two, two ? nullptr : "Nothing else here to move it to."};
    return n;
}
int bag_inventory()
{
    const pc_ui_item_row* e = bag_row(s.bag_category, s.bag_cursor[s.bag_category]);
    return e ? e->inventory : -1;
}

const char* text()
{
    switch (s.mode) {
    case PC_UI_LOOT_CONFIRM:
    case PC_UI_LOOT_MESSAGE:
        switch (s.ask) {
        case PC_UI_LOOT_ASK_DISASSEMBLE: return model.disassemble_ask;
        case PC_UI_LOOT_ASK_CONSOLIDATE: return model.consolidate_ask;
        case PC_UI_LOOT_ASK_KEYS: return model.keys_text;
        case PC_UI_LOOT_ASK_EMPTY: return model.empty;
        default: return model.claimed ? model.leave_rest : model.leave_untouched;
        }
    case PC_UI_LOOT_BAG_CONFIRM:
        return bag.discard_prompt[s.confirm_kind == PC_UI_ITA_DISCARD_ALL ? 2 : 0];
    case PC_UI_LOOT_BAG_QUANTITY:
        return bag.discard_prompt[1];
    default:
        return nullptr;
    }
}

// Cross on the list (the original's _processLootMenu: the leave sound, then
// _discardItems): nothing left or a chest untouched ends at once (the
// bridge), a key or sigil in the list says so, else the Yes / No on No.
void leave()
{
    push(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 0);
    if (rows() == 0 || (!model.claimed && model.chest))
        return;
    s.ask = model.keys ? PC_UI_LOOT_ASK_KEYS : PC_UI_LOOT_ASK_LEAVE;
    s.mode = model.keys ? PC_UI_LOOT_MESSAGE : PC_UI_LOOT_CONFIRM;
    s.menu = 1;
}

void list_tick(const pc_ui_input* in)
{
    int n = rows();
    if (model.valid && n == 0 && !model.claimed) { // the list came empty: the game's line, any button leaves
        s.mode = PC_UI_LOOT_MESSAGE;
        s.ask = PC_UI_LOOT_ASK_EMPTY;
        s.emptied = true;
        return;
    }
    if (n > 0) {
        if (in->repeat & PC_UI_DOWN)
            s.cursor = (s.cursor + 1) % n;
        if (in->repeat & PC_UI_UP)
            s.cursor = (s.cursor + n - 1) % n;
    }
    if (in->pressed & PC_UI_BACK) {
        leave();
        return;
    }
    if (in->pressed & PC_UI_ALT) {
        push(PC_UI_REQ_LOOT_TAKE_ALL, 0, 0, -1);
        return;
    }
    const pc_ui_loot_row* r = row();
    if (!(in->pressed & PC_UI_CONFIRM) || !r)
        return;
    if (r->fits) {
        push(PC_UI_REQ_LOOT_TAKE, r->category, s.cursor, r->record);
        return;
    }
    // no room: the Organize menu (the select sound), on the row's own action when it has one
    push(PC_UI_REQ_LOOT_SOUND, 1, 0, 0);
    s.mode = PC_UI_LOOT_ORGANIZE;
    s.menu = 0;
}

void organize_tick(const pc_ui_input* in)
{
    int kinds[2], n = org_entries(kinds);
    const pc_ui_loot_row* r = row();
    if (!r || (in->pressed & (PC_UI_BACK | PC_UI_EXIT))) {
        push(PC_UI_REQ_LOOT_SOUND, 0, 0, 0);
        s.mode = PC_UI_LOOT_LIST;
        return;
    }
    if (s.menu >= n)
        s.menu = n - 1;
    if (n > 1 && (in->repeat & (PC_UI_UP | PC_UI_DOWN)))
        s.menu = 1 - s.menu;
    if (!(in->pressed & PC_UI_CONFIRM))
        return;
    push(PC_UI_REQ_LOOT_SOUND, 1, 0, 0);
    switch (kinds[s.menu]) {
    case PC_UI_LOOT_ORG_DISASSEMBLE:
    case PC_UI_LOOT_ORG_CONSOLIDATE:
        s.ask = kinds[s.menu] == PC_UI_LOOT_ORG_DISASSEMBLE ? PC_UI_LOOT_ASK_DISASSEMBLE : PC_UI_LOOT_ASK_CONSOLIDATE;
        s.mode = PC_UI_LOOT_CONFIRM;
        s.menu = 0; // these Yes / No open on Yes (_disassembleItem, _consolidateMiscItems)
        break;
    default:
        s.bag_category = r->category;
        s.mode = PC_UI_LOOT_BAG;
        bag_clamp();
        push(PC_UI_REQ_LOOT_BAG, 1, r->category, -1);
        break;
    }
}

void confirm_tick(const pc_ui_input* in)
{
    if (in->repeat & (PC_UI_UP | PC_UI_DOWN))
        s.menu = 1 - s.menu;
    bool back = (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) != 0;
    if (!back && !(in->pressed & PC_UI_CONFIRM))
        return;
    const pc_ui_loot_row* r = row();
    if (back || s.menu != 0 || (s.ask != PC_UI_LOOT_ASK_LEAVE && !r)) {
        push(PC_UI_REQ_LOOT_SOUND, 0, 0, 0);
        s.mode = PC_UI_LOOT_LIST;
        return;
    }
    if (s.ask == PC_UI_LOOT_ASK_LEAVE)
        push(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 1);
    else
        push(s.ask == PC_UI_LOOT_ASK_DISASSEMBLE ? PC_UI_REQ_LOOT_DISASSEMBLE : PC_UI_REQ_LOOT_CONSOLIDATE,
            r->category, s.cursor, r->record);
    s.mode = PC_UI_LOOT_LIST;
}

void message_tick(const pc_ui_input* in)
{
    if (!in->pressed)
        return;
    if (s.ask == PC_UI_LOOT_ASK_EMPTY) {
        push(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 0);
        return; // the bridge ends the session
    }
    push(PC_UI_REQ_LOOT_SOUND, 0, 0, 0);
    s.mode = PC_UI_LOOT_LIST;
}

// Back on the panel's list (a category change, a finished action).
void bag_base()
{
    s.mode = PC_UI_LOOT_BAG;
    s.move_from = -1;
}

void bag_tick(const pc_ui_input* in)
{
    if (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) {
        push(PC_UI_REQ_LOOT_BAG, 0, s.bag_category, -1);
        s.mode = PC_UI_LOOT_LIST;
        return;
    }
    int was = s.bag_category;
    if (in->pressed & (PC_UI_SUB_NEXT | PC_UI_TAB_NEXT) || in->repeat & PC_UI_RIGHT)
        s.bag_category = (s.bag_category + 1) % PC_UI_ITEM_CATEGORIES;
    if (in->pressed & (PC_UI_SUB_PREV | PC_UI_TAB_PREV) || in->repeat & PC_UI_LEFT)
        s.bag_category = (s.bag_category + PC_UI_ITEM_CATEGORIES - 1) % PC_UI_ITEM_CATEGORIES;
    if (s.bag_category != was) // the game's current category follows (MENUB's L1 / R1)
        push(PC_UI_REQ_LOOT_BAG, 1, s.bag_category, -1);
    int c = s.bag_category, n = bag_rows(c);
    bag_clamp();
    if (n > 0) {
        if (in->repeat & PC_UI_DOWN)
            s.bag_cursor[c] = (s.bag_cursor[c] + 1) % n;
        if (in->repeat & PC_UI_UP)
            s.bag_cursor[c] = (s.bag_cursor[c] + n - 1) % n;
    }
    if (!(in->pressed & PC_UI_CONFIRM) || !bag_row(c, s.bag_cursor[c]))
        return;
    s.mode = PC_UI_LOOT_BAG_ACTIONS;
    s.bag_menu = 0;
}

// Circle on an action.
void bag_do(const BagAction& a)
{
    int c = s.bag_category, row = s.bag_cursor[c];
    const pc_ui_item_row* e = bag_row(c, row);
    if (!e)
        return;
    switch (a.kind) {
    case PC_UI_ITA_DISCARD:
        if (!a.enabled) { // the bridge buzzes (the original's unselectable row)
            push(PC_UI_REQ_ITEM_DISCARD, c, row, e->inventory, 0);
            return;
        }
        s.confirm_kind = PC_UI_ITA_DISCARD;
        s.bag_record = e->inventory;
        s.count_max = c == PC_UI_IC_MISC ? e->count : 1;
        s.count = s.count_max >= 2 ? 0 : 1;
        s.step = 0;
        s.menu = 1; // on No, as _discardMenu's
        s.mode = s.count_max >= 2 ? PC_UI_LOOT_BAG_QUANTITY : PC_UI_LOOT_BAG_CONFIRM;
        return;
    case PC_UI_ITA_DISCARD_ALL:
        if (!a.enabled) {
            push(PC_UI_REQ_ITEM_DISCARD_ALL, c, row, e->inventory);
            return;
        }
        s.confirm_kind = PC_UI_ITA_DISCARD_ALL;
        s.bag_record = e->inventory;
        s.count_max = s.count = e->count;
        s.menu = 1;
        s.mode = PC_UI_LOOT_BAG_CONFIRM;
        return;
    case PC_UI_ITA_SORT:
        if (!a.enabled) {
            push(PC_UI_REQ_LOOT_SOUND, 2, 0, 0);
            return;
        }
        s.mode = PC_UI_LOOT_BAG_SORT;
        s.sort = 0;
        return;
    case PC_UI_ITA_MOVE:
        if (!a.enabled) {
            push(PC_UI_REQ_LOOT_SOUND, 2, 0, 0);
            return;
        }
        s.mode = PC_UI_LOOT_BAG_MOVE;
        s.move_from = row;
        return;
    default:
        return;
    }
}

void bag_actions_tick(const pc_ui_input* in)
{
    BagAction a[PC_UI_LOOT_BAG_ACTION_MAX];
    int n = bag_actions(a);
    if (!n || (in->pressed & (PC_UI_BACK | PC_UI_EXIT))) {
        bag_base();
        return;
    }
    if (s.bag_menu >= n || s.bag_menu < 0)
        s.bag_menu = 0;
    if (in->repeat & PC_UI_DOWN)
        s.bag_menu = (s.bag_menu + 1) % n;
    if (in->repeat & PC_UI_UP)
        s.bag_menu = (s.bag_menu + n - 1) % n;
    if (in->pressed & PC_UI_CONFIRM)
        bag_do(a[s.bag_menu]);
}

// _discardMenu determineDiscardAmount's keys (items1.h), as the Items screen's picker: Up/Down by the step
// when it stays in 0..max; Left a bigger step (past 100: all), Right a smaller one (past 1: none).
void bag_quantity_tick(const pc_ui_input* in)
{
    int c = s.bag_category;
    if (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) {
        s.mode = PC_UI_LOOT_BAG_ACTIONS;
        return;
    }
    if (in->pressed & PC_UI_CONFIRM) {
        if (s.count == 0) { // none: the original leaves the picker (the leave sound)
            push(PC_UI_REQ_ITEM_CANCEL, c, s.bag_cursor[c], s.bag_record, 0);
            bag_base();
        } else {
            s.mode = PC_UI_LOOT_BAG_CONFIRM;
            s.menu = 1;
        }
        return;
    }
    int dir = ((in->repeat & PC_UI_UP) ? 1 : 0) - ((in->repeat & PC_UI_DOWN) ? 1 : 0);
    int next = s.count + dir * STEPS[s.step];
    if (dir && next >= 0 && next <= s.count_max)
        s.count = next;
    dir = ((in->repeat & PC_UI_LEFT) ? 1 : 0) - ((in->repeat & PC_UI_RIGHT) ? 1 : 0);
    if (dir == 1) {
        if (s.step == 2)
            s.count = s.count_max;
        else
            ++s.step;
    } else if (dir == -1) {
        if (s.step == 0)
            s.count = 0;
        else
            --s.step;
    }
}

void bag_confirm_tick(const pc_ui_input* in)
{
    if (in->repeat & (PC_UI_UP | PC_UI_DOWN))
        s.menu = 1 - s.menu;
    bool back = (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) != 0;
    if (!back && !(in->pressed & PC_UI_CONFIRM))
        return;
    int c = s.bag_category, r = s.bag_cursor[c];
    bool all = s.confirm_kind == PC_UI_ITA_DISCARD_ALL;
    if (back || s.menu != 0)
        push(PC_UI_REQ_ITEM_CANCEL, c, r, s.bag_record, all ? 2 : 1);
    else if (all)
        push(PC_UI_REQ_ITEM_DISCARD_ALL, c, r, s.bag_record);
    else
        push(PC_UI_REQ_ITEM_DISCARD, c, r, s.bag_record, s.count);
    bag_base();
}

void bag_sort_tick(const pc_ui_input* in)
{
    int c = s.bag_category, k = bag_sorts(c);
    if (!k || (in->pressed & (PC_UI_BACK | PC_UI_EXIT))) {
        s.mode = PC_UI_LOOT_BAG_ACTIONS;
        return;
    }
    if (s.sort >= k || s.sort < 0)
        s.sort = 0;
    if (in->repeat & PC_UI_DOWN)
        s.sort = (s.sort + 1) % k;
    if (in->repeat & PC_UI_UP)
        s.sort = (s.sort + k - 1) % k;
    if (in->pressed & PC_UI_CONFIRM) {
        push(PC_UI_REQ_ITEM_SORT, c, s.bag_cursor[c], bag_inventory(), bag.sort_stats[c][s.sort]);
        bag_base();
    }
}

void bag_move_tick(const pc_ui_input* in)
{
    int c = s.bag_category, n = bag_rows(c);
    if (s.move_from < 0 || s.move_from >= n || (in->pressed & (PC_UI_BACK | PC_UI_EXIT))) {
        s.mode = PC_UI_LOOT_BAG_ACTIONS;
        s.move_from = -1;
        return;
    }
    if (in->repeat & PC_UI_DOWN)
        s.bag_cursor[c] = (s.bag_cursor[c] + 1) % n;
    if (in->repeat & PC_UI_UP)
        s.bag_cursor[c] = (s.bag_cursor[c] + n - 1) % n;
    if (in->pressed & PC_UI_CONFIRM) {
        const pc_ui_item_row* from = bag_row(c, s.move_from);
        if (s.bag_cursor[c] != s.move_from && from)
            push(PC_UI_REQ_ITEM_MOVE, c, s.move_from, s.bag_cursor[c], from->inventory);
        bag_base(); // the cursor stays on the target row, as the original's list does
    }
}

// The pointer: focus what it is on; a click confirms there (true: the click was used).
bool apply_mouse(pc_ui_input& in)
{
    Mouse m = s.mouse;
    s.mouse = Mouse();
    if (m.wheel >= 1 || m.wheel <= -1) {
        unsigned b = m.wheel > 0 ? PC_UI_UP : PC_UI_DOWN;
        in.repeat |= b;
    }
    auto focus = [&](int kind, int index) -> bool {
        switch (kind) {
        case PC_UI_MOUSE_LOOT_ROW:
            if (s.mode != PC_UI_LOOT_LIST || index < 0 || index >= rows())
                return false;
            s.cursor = index;
            return true;
        case PC_UI_MOUSE_LOOT_MENU: {
            int kinds[2];
            if (s.mode == PC_UI_LOOT_BAG_ACTIONS || s.mode == PC_UI_LOOT_BAG_SORT) {
                BagAction a[PC_UI_LOOT_BAG_ACTION_MAX];
                int k = s.mode == PC_UI_LOOT_BAG_ACTIONS ? bag_actions(a) : bag_sorts(s.bag_category);
                if (index < 0 || index >= k)
                    return false;
                (s.mode == PC_UI_LOOT_BAG_ACTIONS ? s.bag_menu : s.sort) = index;
                return true;
            }
            int n = s.mode == PC_UI_LOOT_ORGANIZE ? org_entries(kinds)
                : s.mode == PC_UI_LOOT_CONFIRM || s.mode == PC_UI_LOOT_BAG_CONFIRM ? 2 : 0;
            if (index < 0 || index >= n)
                return false;
            s.menu = index;
            return true;
        }
        case PC_UI_MOUSE_LOOT_BAG:
            if ((s.mode != PC_UI_LOOT_BAG && s.mode != PC_UI_LOOT_BAG_MOVE) || index < 0
                || index >= bag_rows(s.bag_category))
                return false;
            s.bag_cursor[s.bag_category] = index;
            return true;
        case PC_UI_MOUSE_SUB:
            if (s.mode != PC_UI_LOOT_BAG || index < 0 || index >= PC_UI_ITEM_CATEGORIES)
                return false;
            if (s.bag_category != index)
                push(PC_UI_REQ_LOOT_BAG, 1, index, -1);
            s.bag_category = index;
            bag_clamp();
            return true;
        default:
            return false;
        }
    };
    if (m.hover_kind != PC_UI_MOUSE_NONE)
        focus(m.hover_kind, m.hover_index);
    if (m.click_kind == PC_UI_MOUSE_BUTTON) {
        in.pressed |= (unsigned)m.click_index;
        in.repeat |= (unsigned)m.click_index;
        return false;
    }
    if (m.click_kind != PC_UI_MOUSE_NONE) {
        if (m.click_kind == PC_UI_MOUSE_SUB) {
            focus(m.click_kind, m.click_index);
            return true;
        }
        if (focus(m.click_kind, m.click_index))
            in.pressed |= PC_UI_CONFIRM, in.repeat |= PC_UI_CONFIRM;
        else if (s.mode == PC_UI_LOOT_MESSAGE)
            in.pressed |= PC_UI_CONFIRM; // a message: any click
    }
    return false;
}

std::string escape(const char* text)
{
    std::string out;
    for (const char* p = text ? text : ""; *p; ++p) {
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

} // namespace

extern "C" void pc_ui_loot_open(void)
{
    s.visible = true;
    s.mode = PC_UI_LOOT_LIST;
    s.cursor = 0;
    s.menu = 0;
    s.bag_category = PC_UI_IC_WEAPON;
    std::memset(s.bag_cursor, 0, sizeof s.bag_cursor);
    s.emptied = false;
    s.bag_menu = s.sort = s.count = s.count_max = s.step = 0;
    s.move_from = -1;
    s.q_head = s.q_count = 0;
    s.mouse = Mouse();
    model = pc_ui_loot();
    bag = pc_ui_items();
}

extern "C" void pc_ui_loot_close(void)
{
    s.visible = false;
    s.q_head = s.q_count = 0;
    s.mouse = Mouse();
}

extern "C" int pc_ui_loot_visible(void) { return s.visible; }

extern "C" void pc_ui_loot_tick(const pc_ui_input* input)
{
    if (!s.visible)
        return;
    pc_ui_input in = *input;
    if (apply_mouse(in))
        return;
    clamp();
    switch (s.mode) {
    case PC_UI_LOOT_ORGANIZE: organize_tick(&in); break;
    case PC_UI_LOOT_CONFIRM: confirm_tick(&in); break;
    case PC_UI_LOOT_MESSAGE: message_tick(&in); break;
    case PC_UI_LOOT_BAG: bag_tick(&in); break;
    case PC_UI_LOOT_BAG_CONFIRM: bag_confirm_tick(&in); break;
    case PC_UI_LOOT_BAG_ACTIONS: bag_actions_tick(&in); break;
    case PC_UI_LOOT_BAG_QUANTITY: bag_quantity_tick(&in); break;
    case PC_UI_LOOT_BAG_SORT: bag_sort_tick(&in); break;
    case PC_UI_LOOT_BAG_MOVE: bag_move_tick(&in); break;
    default: list_tick(&in); break;
    }
    clamp();
}

extern "C" void pc_ui_loot_set(const pc_ui_loot* m)
{
    model = *m;
    clamp();
}
extern "C" const pc_ui_loot* pc_ui_loot_model(void) { return &model; }
extern "C" void pc_ui_loot_set_bag(const pc_ui_items* b)
{
    bag = *b;
    bag_clamp();
}
extern "C" const pc_ui_items* pc_ui_loot_bag(void) { return &bag; }

extern "C" void pc_ui_loot_view_get(pc_ui_loot_view* v)
{
    std::memset(v, 0, sizeof *v);
    clamp();
    v->mode = s.mode;
    v->cursor = s.cursor;
    v->rows = rows();
    v->row = row();
    v->menu = s.menu;
    if (s.mode == PC_UI_LOOT_ORGANIZE)
        v->menu_rows = org_entries(v->menu_kind);
    v->ask = s.ask;
    v->text = text();
    v->bag_category = s.bag_category;
    v->bag_cursor = s.bag_cursor[s.bag_category];
    v->bag_rows = bag_rows(s.bag_category);
    BagAction a[PC_UI_LOOT_BAG_ACTION_MAX];
    v->bag_actions = PC_UI_LOOT_IN_BAG(s.mode) ? bag_actions(a) : 0;
    for (int i = 0; i < v->bag_actions; ++i) {
        v->bag_action_kind[i] = a[i].kind;
        v->bag_action_enabled[i] = a[i].enabled;
        v->bag_action_reason[i] = a[i].reason;
    }
    if (s.mode == PC_UI_LOOT_BAG_ACTIONS)
        v->menu = s.bag_menu < v->bag_actions ? s.bag_menu : 0;
    v->confirm_kind = s.confirm_kind;
    v->count = s.count;
    v->count_max = s.count_max;
    v->step = s.step;
    v->sort = s.sort;
    v->move_from = s.mode == PC_UI_LOOT_BAG_MOVE ? s.move_from : -1;
}

extern "C" int pc_ui_loot_focus(void)
{
    clamp();
    return s.visible && row() ? s.cursor : -1;
}

extern "C" int pc_ui_loot_pop_request(pc_ui_request* out)
{
    if (!s.q_count)
        return 0;
    *out = s.queue[s.q_head];
    s.q_head = (s.q_head + 1) % QUEUE;
    --s.q_count;
    return 1;
}

extern "C" void pc_ui_loot_mouse(int kind, int index, int clicked, float wheel)
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

extern "C" void pc_ui_loot_json(char* out, unsigned size)
{
    if (!size)
        return;
    out[0] = 0;
    if (!s.visible)
        return;
    pc_ui_loot_view v;
    pc_ui_loot_view_get(&v);
    char buf[400];
    std::string j;
    std::snprintf(buf, sizeof buf,
        ",\"loot\":{\"valid\":%s,\"chest\":%s,\"mode\":\"%s\",\"cursor\":%d,\"rows\":%d,\"claimed\":%d,\"keys\":%s",
        model.valid ? "true" : "false", model.chest ? "true" : "false", MODE_IDS[v.mode], v.cursor, v.rows,
        model.claimed, model.keys ? "true" : "false");
    j += buf;
    j += ",\"list\":[";
    for (int r = 0; r < v.rows; ++r) {
        const pc_ui_loot_row& e = model.rows[r];
        std::snprintf(buf, sizeof buf,
            "%s{\"row\":%d,\"category\":\"%s\",\"record\":%d,\"id\":%d,\"count\":%d,\"fits\":%s,\"key\":%s,"
            "\"organize\":\"%s\",\"name\":\"",
            r ? "," : "", r, e.category >= 0 && e.category < PC_UI_ITEM_CATEGORIES ? CATEGORY_IDS[e.category] : "",
            e.record, e.id, e.count, e.fits ? "true" : "false", e.key ? "true" : "false",
            ORG_IDS[e.organize >= 0 && e.organize <= 2 ? e.organize : 0]);
        j += buf;
        j += escape(e.name) + "\"}";
    }
    j += "],\"bag\":[";
    for (int c = 0; c < PC_UI_ITEM_CATEGORIES; ++c) {
        std::snprintf(buf, sizeof buf, "%s{\"id\":\"%s\",\"count\":%d,\"capacity\":%d}", c ? "," : "",
            CATEGORY_IDS[c], model.bag_count[c], model.bag_capacity[c]);
        j += buf;
    }
    j += "]";
    if (v.mode == PC_UI_LOOT_ORGANIZE) {
        std::snprintf(buf, sizeof buf, ",\"menu\":%d,\"entries\":[", v.menu);
        j += buf;
        for (int i = 0; i < v.menu_rows; ++i)
            j += std::string(i ? "," : "") + "\"" + ORG_IDS[v.menu_kind[i]] + "\"";
        j += "]";
    }
    if (v.mode == PC_UI_LOOT_BAG_ACTIONS) {
        std::snprintf(buf, sizeof buf, ",\"menu\":%d,\"actions\":[", v.menu);
        j += buf;
        for (int i = 0; i < v.bag_actions; ++i) {
            std::snprintf(buf, sizeof buf, "%s{\"id\":\"%s\",\"enabled\":%s}", i ? "," : "",
                BAG_ACTION_IDS[v.bag_action_kind[i]], v.bag_action_enabled[i] ? "true" : "false");
            j += buf;
        }
        j += "]";
    }
    if (v.mode == PC_UI_LOOT_BAG_QUANTITY || v.mode == PC_UI_LOOT_BAG_CONFIRM) {
        std::snprintf(buf, sizeof buf, ",\"count\":%d,\"count_max\":%d,\"step\":%d", v.count, v.count_max, v.step);
        j += buf;
    }
    if (v.mode == PC_UI_LOOT_BAG_SORT) {
        int c = v.bag_category;
        std::snprintf(buf, sizeof buf, ",\"sort\":%d,\"stats\":[", v.sort);
        j += buf;
        for (int i = 0; i < bag_sorts(c); ++i) {
            std::snprintf(buf, sizeof buf, "%s%d", i ? "," : "", bag.sort_stats[c][i]);
            j += buf;
        }
        j += "]";
    }
    if (v.mode == PC_UI_LOOT_BAG_MOVE) {
        std::snprintf(buf, sizeof buf, ",\"move_from\":%d", v.move_from);
        j += buf;
    }
    if (v.mode == PC_UI_LOOT_CONFIRM || v.mode == PC_UI_LOOT_MESSAGE || v.mode == PC_UI_LOOT_BAG_CONFIRM) {
        std::snprintf(buf, sizeof buf, ",\"ask\":\"%s\",\"yes\":%s,\"text\":\"",
            v.mode == PC_UI_LOOT_BAG_CONFIRM ? (v.confirm_kind == PC_UI_ITA_DISCARD_ALL ? "discard_all" : "discard")
                                             : ASK_IDS[v.ask],
            v.menu == 0 ? "true" : "false");
        j += buf;
        j += escape(v.text) + "\"";
    }
    if (PC_UI_LOOT_IN_BAG(v.mode)) {
        int c = v.bag_category;
        std::snprintf(buf, sizeof buf, ",\"bag_panel\":{\"category\":\"%s\",\"cursor\":%d,\"rows\":%d,\"list\":[",
            CATEGORY_IDS[c], v.bag_cursor, v.bag_rows);
        j += buf;
        for (int r = 0; r < v.bag_rows; ++r) {
            const pc_ui_item_row* e = bag_row(c, r);
            if (!e)
                break;
            std::snprintf(buf, sizeof buf,
                "%s{\"row\":%d,\"inventory\":%d,\"id\":%d,\"count\":%d,\"discard\":%s,\"name\":\"", r ? "," : "", r,
                e->inventory, e->id, e->count, e->discard ? "true" : "false");
            j += buf;
            j += escape(e->name) + "\"}";
        }
        j += "]}";
    }
    const pc_ui_item_details& d = model.details;
    if (d.valid && v.row) {
        std::snprintf(buf, sizeof buf,
            ",\"details\":{\"dp\":%d,\"dp_max\":%d,\"pp\":%d,\"pp_max\":%d,\"str\":%d,\"int\":%d,\"agl\":%d,"
            "\"equipped_slot\":%d,\"equipped\":\"",
            d.stats.dp, d.stats.dp_max, d.stats.pp, d.stats.pp_max, d.stats.str, d.stats.intel, d.stats.agl,
            model.equipped_slot);
        j += buf;
        j += escape(model.equipped_slot >= 0 ? model.equipped.name : "") + "\"}";
    }
    j += "}";
    std::snprintf(out, size, "%s", j.c_str());
}
