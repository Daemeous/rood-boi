// The modern menu shell's navigation (pc/src/ui/ui_shell.cpp) without the game.
#include "ui/ui_shell.h"
#include "ui/ui_settings.h"
#include "ui/ui_theme.h"
#include <cstdio>
#include <cstring>
#include <string>

int pc_ui_menus = 1;
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// ---- A stand-in settings model (ui_settings.h) ----
// Pages of rows "p<page>_<row>"; Interface row 0 is the Menus choice (pc_ui_menus),
// Display row 0 a toggle that shows row 2 (a "shown while" row), Bindings rows are
// bindings (Circle starts a capture), the last Controls row an action.
namespace fake {
int rows[PC_UI_SETTINGS_PAGES] = {5, 2, 1, 3, 4, 3, 2};
int value[PC_UI_SETTINGS_PAGES][8];
bool extra = true;   // Display: the third row shows
int capture = -1;    // PS1 button
int capture_done_kind = -9;
int flushes, saves, changes, sets, activations, previews;
double preview_value;
bool dirty;
double set_value;
const char* const IDS[PC_UI_SETTINGS_PAGES] = {"graphics", "display", "audio", "interface", "controls", "bindings",
    "gameplay"};
int kind(int page, int row)
{
    if (page == PC_UI_SETTINGS_BINDINGS)
        return PC_UI_SET_BINDING;
    if (page == PC_UI_SETTINGS_CONTROLS && row == rows[page] - 1)
        return PC_UI_SET_ACTION;
    if (page == PC_UI_SETTINGS_GRAPHICS && row == 1)
        return PC_UI_SET_SLIDER;
    return page == PC_UI_SETTINGS_DISPLAY && row == 0 ? PC_UI_SET_TOGGLE : PC_UI_SET_CHOICE;
}
int count(int page) { return rows[page] + (page == PC_UI_SETTINGS_DISPLAY && extra ? 1 : 0); }
void reset()
{
    std::memset(value, 0, sizeof value);
    extra = true;
    capture = -1;
    capture_done_kind = -9;
    flushes = saves = changes = sets = activations = previews = 0;
    dirty = false;
}
} // namespace fake
extern "C" {
int pc_ui_settings_pages(void) { return PC_UI_SETTINGS_PAGES; }
const char* pc_ui_settings_page_title(int page) { return fake::IDS[page]; }
const char* pc_ui_settings_page_id(int page) { return fake::IDS[page]; }
int pc_ui_settings_rows(int page) { return page >= 0 && page < PC_UI_SETTINGS_PAGES ? fake::count(page) : 0; }
int pc_ui_settings_row(int page, int row, pc_ui_setting_row* out)
{
    static char ids[PC_UI_SETTINGS_PAGES][8][16];
    if (row < 0 || row >= pc_ui_settings_rows(page))
        return 0;
    std::memset(out, 0, sizeof *out);
    std::snprintf(ids[page][row], sizeof ids[page][row], "p%d_%d", page, row);
    out->id = page == PC_UI_SETTINGS_INTERFACE && row == 0 ? "ui_menus" : ids[page][row];
    out->label = out->id;
    out->help = "";
    out->kind = fake::kind(page, row);
    out->enabled = !(page == PC_UI_SETTINGS_GRAPHICS && row == 4); // greyed
    if (!out->enabled)
        std::snprintf(out->reason, sizeof out->reason, "Needs the hardware renderer");
    out->button = page == PC_UI_SETTINGS_BINDINGS ? row : -1;
    out->index = page == PC_UI_SETTINGS_INTERFACE && row == 0 ? pc_ui_menus : fake::value[page][row];
    std::snprintf(out->value, sizeof out->value, "%d", out->index);
    return 1;
}
int pc_ui_settings_change(int page, int row, int delta)
{
    ++fake::changes;
    if (page == PC_UI_SETTINGS_GRAPHICS && row == 4)
        return 0;
    if (page == PC_UI_SETTINGS_INTERFACE && row == 0)
        pc_ui_menus = !pc_ui_menus;
    else
        fake::value[page][row] += delta;
    if (page == PC_UI_SETTINGS_DISPLAY && row == 0)
        fake::extra = !fake::extra;
    fake::dirty = true;
    return 1;
}
int pc_ui_settings_saves(void) { return fake::saves; }
const char* pc_ui_settings_note(void) { return nullptr; }
const char* pc_ui_settings_capture_note(void) { return nullptr; }
int pc_ui_settings_preview(int page, int row, double value)
{
    ++fake::previews;
    fake::preview_value = value;
    (void)page, (void)row;
    return 1;
}
int pc_ui_settings_set(int page, int row, double value)
{
    ++fake::sets;
    fake::set_value = value;
    fake::value[page][row] = (int)value;
    fake::dirty = true;
    return 1;
}
int pc_ui_settings_activate(int page, int row)
{
    ++fake::activations;
    if (fake::kind(page, row) == PC_UI_SET_BINDING)
        fake::capture = row;
    return 1;
}
void pc_ui_settings_flush(void)
{
    ++fake::flushes;
    if (fake::dirty)
        ++fake::saves;
    fake::dirty = false;
}
int pc_ui_settings_dirty(void) { return fake::dirty; }
int pc_ui_settings_capture_button(void) { return fake::capture; }
int pc_ui_settings_capture_done(int kind, int)
{
    fake::capture_done_kind = kind;
    fake::capture = -1;
    return -1;
}
const char* pc_settings_status(void) { return "Saved settings to settings.cfg"; }
}

static int tick(unsigned buttons)
{
    pc_ui_input in = {buttons, buttons};
    return pc_ui_shell_tick(&in);
}
static bool json_has(const char* needle)
{
    static char buf[65536]; // as large as a full candidate list's JSON gets, with room
    pc_ui_shell_json(buf, sizeof buf);
    return std::strstr(buf, needle) != nullptr;
}

static pc_ui_item item(const char* name, int inventory, int str, int dp, int two_handed = 0)
{
    pc_ui_item it = {};
    std::snprintf(it.name, sizeof it.name, "%s", name);
    it.inventory = inventory;
    it.str = str;
    it.dp = dp;
    it.two_handed = two_handed;
    it.damage_type = -1;
    return it;
}
static pc_ui_equipment equipment;
static void no_candidates()
{
    equipment.slot = -1;
    equipment.count = 0;
}
static void weapons() // what the bridge fills for slot 0: every weapon in the bag
{
    equipment.slot = PC_UI_SLOT_WEAPON;
    equipment.count = 2;
    equipment.candidates[0] = item("Sword A", 0, 10, 50);
    equipment.candidates[1] = item("Axe B", 1, 14, 40, 1);
}
static void shields()
{
    equipment.slot = PC_UI_SLOT_SHIELD;
    equipment.count = 1;
    equipment.candidates[0] = item("Shield A", 0, 2, 30);
}
static pc_ui_request pop()
{
    pc_ui_request r = {PC_UI_REQ_NONE, 0, 0, 0};
    if (!pc_ui_shell_pop_request(&r))
        r.kind = PC_UI_REQ_NONE;
    return r;
}
static bool req(int kind, int slot, int item_, int shield = -1)
{
    pc_ui_request r = pop();
    if (r.kind != kind || r.slot != slot)
        return false;
    if (kind == PC_UI_REQ_EQUIP || kind == PC_UI_REQ_QUICK_SET)
        if (r.item != item_)
            return false;
    if (kind == PC_UI_REQ_QUICK_SET && r.shield != shield)
        return false;
    return true;
}
static bool queue_empty() { return pop().kind == PC_UI_REQ_NONE; }

static void equipment_tests()
{
    equipment = {};
    equipment.valid = 1;
    equipment.str = 20;
    for (int i = 0; i < PC_UI_EQUIP_SLOTS; ++i)
        equipment.equipped[i] = item("", -1, 0, 0);
    equipment.equipped[PC_UI_SLOT_WEAPON] = item("Sword A", 0, 10, 50);
    equipment.equipped[PC_UI_SLOT_SHIELD] = item("Shield A", 0, 2, 30);
    for (int i = 0; i < PC_UI_QUICK; ++i)
        equipment.quick_weapon[i] = equipment.quick_shield[i] = -1;
    no_candidates();
    while (!queue_empty()) {}

    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_set_equipment(&equipment);
    CHECK(pc_ui_shell_screen() == PC_UI_SCREEN_ASHLEY);
    tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"equipment\""));
    CHECK(pc_ui_shell_screen() == PC_UI_SCREEN_EQUIPMENT);
    CHECK(json_has("\"screen\":\"equipment\""));
    CHECK(json_has("\"mode\":\"gear\""));
    CHECK(json_has("\"slot\":\"weapon\""));
    CHECK(json_has("{\"id\":\"accessory\""));
    CHECK(json_has("\"name\":\"Sword A\""));
    CHECK(!json_has("\"candidates\""));
    CHECK(pc_ui_shell_wanted_candidates() == -1);
    // no command rows here: ui_draw's row loop draws nothing
    {
        const char *label, *help, *value, *reason;
        int enabled;
        CHECK(!pc_ui_shell_row(0, &label, &help, &value, &enabled, &reason));
    }
    // Circle: the pick list for the weapon asks for candidates; until they arrive it is loading
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(json_has("\"mode\":\"pick\""));
    CHECK(req(PC_UI_REQ_CANDIDATES, PC_UI_SLOT_WEAPON, 0));
    CHECK(queue_empty());
    CHECK(pc_ui_shell_wanted_candidates() == PC_UI_SLOT_WEAPON);
    CHECK(json_has("\"loading\":true"));
    tick(PC_UI_CONFIRM); // nothing to pick yet
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"pick\""));
    weapons();
    pc_ui_shell_set_equipment(&equipment);
    CHECK(json_has("\"loading\":false"));
    CHECK(json_has("\"cursor\":0")); // on the equipped one
    CHECK(json_has("\"name\":\"Axe B\""));
    CHECK(json_has("\"two_handed\":true,\"dstr\":4,\"dint\":0,\"dagl\":0,\"ddp\":-10"));
    // Circle on the equipped one: back without a request
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"gear\""));
    CHECK(queue_empty());
    // again: Down + Circle equips Axe B
    tick(PC_UI_CONFIRM);
    CHECK(req(PC_UI_REQ_CANDIDATES, PC_UI_SLOT_WEAPON, 0));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"cursor\":1"));
    tick(PC_UI_CONFIRM);
    CHECK(req(PC_UI_REQ_EQUIP, PC_UI_SLOT_WEAPON, 1));
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"gear\""));
    // pick + Cross: back to gear, no request, the shell stays
    tick(PC_UI_CONFIRM);
    pop();
    CHECK(tick(PC_UI_BACK) == 0);
    CHECK(pc_ui_shell_visible());
    CHECK(json_has("\"mode\":\"gear\""));
    CHECK(queue_empty());
    // busy (a model load runs): confirm in the pick list queues nothing and stays
    equipment.busy = 1;
    pc_ui_shell_set_equipment(&equipment);
    tick(PC_UI_CONFIRM);
    pop();
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"pick\""));
    CHECK(json_has("\"busy\":true"));
    tick(PC_UI_BACK);
    // busy: no take off either
    tick(PC_UI_ALT);
    CHECK(queue_empty());
    equipment.busy = 0;
    pc_ui_shell_set_equipment(&equipment);
    CHECK(json_has("\"quick_enabled\":false")); // the field's quick swap option, as the bridge reports it
    equipment.quick_enabled = 1;
    pc_ui_shell_set_equipment(&equipment);
    CHECK(json_has("\"quick_enabled\":true"));
    // Square takes off (the weapon too: the game allows none); an empty slot queues nothing
    tick(PC_UI_ALT);
    CHECK(req(PC_UI_REQ_EQUIP, PC_UI_SLOT_WEAPON, -1));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"slot\":\"shield\""));
    tick(PC_UI_ALT);
    CHECK(req(PC_UI_REQ_EQUIP, PC_UI_SLOT_SHIELD, -1));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"slot\":\"right_arm\""));
    tick(PC_UI_ALT);
    CHECK(queue_empty());
    // Up wraps through the eight slots
    tick(PC_UI_UP); tick(PC_UI_UP); tick(PC_UI_UP);
    CHECK(json_has("\"slot\":\"accessory\""));
    tick(PC_UI_DOWN);
    // the shield pick asks for slot 1
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(req(PC_UI_REQ_CANDIDATES, PC_UI_SLOT_SHIELD, 0));
    CHECK(json_has("\"loading\":true")); // the weapon list is not the shield's
    tick(PC_UI_BACK);

    // R2: Quick swap; L2/R2 wrap between the two sub-tabs
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"sub\":\"quick\""));
    CHECK(json_has("\"mode\":\"quick\""));
    CHECK(json_has("\"quick_slot\":\"circle\""));
    CHECK(json_has("{\"id\":\"left\",\"weapon\":-1,\"shield\":-1"));
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"mode\":\"gear\""));
    tick(PC_UI_SUB_PREV);
    CHECK(json_has("\"mode\":\"quick\""));
    // Up/Down linear, Left/Right jump between the face buttons and the D-pad
    tick(PC_UI_DOWN);
    CHECK(json_has("\"quick_slot\":\"triangle\""));
    tick(PC_UI_RIGHT);
    CHECK(json_has("\"quick_slot\":\"down\""));
    tick(PC_UI_LEFT);
    CHECK(json_has("\"quick_slot\":\"triangle\""));
    tick(PC_UI_UP); tick(PC_UI_UP);
    CHECK(json_has("\"quick_slot\":\"left\""));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"quick_slot\":\"circle\""));
    // Circle: every weapon (weapon candidates); a one-handed one asks for a shield
    no_candidates();
    pc_ui_shell_set_equipment(&equipment);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"quick_weapon\""));
    CHECK(req(PC_UI_REQ_CANDIDATES, PC_UI_SLOT_WEAPON, 0));
    weapons();
    pc_ui_shell_set_equipment(&equipment);
    tick(PC_UI_CONFIRM); // Sword A (one-handed)
    CHECK(json_has("\"mode\":\"quick_shield\""));
    CHECK(req(PC_UI_REQ_CANDIDATES, PC_UI_SLOT_SHIELD, 0));
    CHECK(queue_empty());
    CHECK(json_has("\"loading\":true"));
    shields();
    pc_ui_shell_set_equipment(&equipment);
    CHECK(json_has("\"candidates\":[{\"name\":\"No shield\",\"inventory\":-1},{\"name\":\"Shield A\""));
    CHECK(json_has("\"cursor\":0")); // the slot has no shield: on "No shield"
    // Cross goes back one step: the weapon list, on the weapon picked
    CHECK(tick(PC_UI_BACK) == 0);
    CHECK(json_has("\"mode\":\"quick_weapon\""));
    CHECK(req(PC_UI_REQ_CANDIDATES, PC_UI_SLOT_WEAPON, 0));
    weapons();
    pc_ui_shell_set_equipment(&equipment);
    CHECK(json_has("\"cursor\":0"));
    tick(PC_UI_CONFIRM);
    pop();
    shields();
    pc_ui_shell_set_equipment(&equipment);
    tick(PC_UI_DOWN); // Shield A
    tick(PC_UI_CONFIRM);
    CHECK(req(PC_UI_REQ_QUICK_SET, 0, 0, 0));
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"quick\""));
    // "No shield"
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    pop();
    weapons();
    pc_ui_shell_set_equipment(&equipment);
    tick(PC_UI_CONFIRM);
    pop();
    shields();
    pc_ui_shell_set_equipment(&equipment);
    tick(PC_UI_CONFIRM);
    CHECK(req(PC_UI_REQ_QUICK_SET, 1, 0, -1));
    // a two-handed weapon saves at once with no shield
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    pop();
    weapons();
    pc_ui_shell_set_equipment(&equipment);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(req(PC_UI_REQ_QUICK_SET, 2, 1, -1));
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"quick\""));
    // the list opens on what the slot holds
    equipment.quick_weapon[2] = 1;
    equipment.quick_shield[2] = -1;
    std::snprintf(equipment.quick_weapon_name[2], sizeof equipment.quick_weapon_name[2], "Axe B");
    pc_ui_shell_set_equipment(&equipment);
    CHECK(json_has("{\"id\":\"square\",\"weapon\":1,\"shield\":-1,\"weapon_name\":\"Axe B\""));
    tick(PC_UI_CONFIRM);
    pop();
    CHECK(json_has("\"cursor\":1"));
    tick(PC_UI_BACK);
    CHECK(json_has("\"mode\":\"quick\""));
    // Square clears an assigned slot; an empty one queues nothing
    tick(PC_UI_ALT);
    CHECK(req(PC_UI_REQ_QUICK_CLEAR, 2, 0));
    tick(PC_UI_DOWN);
    tick(PC_UI_ALT);
    CHECK(queue_empty());
    // Cross at the top closes the shell; reopening drops any pick list but keeps the sub-tab
    tick(PC_UI_CONFIRM);
    pop();
    CHECK(tick(PC_UI_EXIT) == -1);
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"mode\":\"quick\""));
    // the overview's card shows the slot's weapon: every weapon is wanted
    CHECK(pc_ui_shell_wanted_candidates() == PC_UI_SLOT_WEAPON);
    CHECK(tick(PC_UI_BACK) == -1);
    // switching tabs drops a pick list
    pc_ui_shell_open(&all, 1);
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_CONFIRM);
    pop();
    tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"items\""));
    CHECK(pc_ui_shell_wanted_candidates() == -1);
    tick(PC_UI_TAB_PREV);
    CHECK(json_has("\"mode\":\"gear\""));
    // locked (carrying a cube: equipment command unselectable): browse only
    pc_ui_commands cube = {0x3FF, 8, 0x02};
    pc_ui_shell_open(&cube, 1);
    CHECK(json_has("\"locked\":true"));
    CHECK(json_has("\"reason\":\"Not while carrying a cube.\""));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_ALT);
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"gear\""));
    // not unlocked (menuFlags bit 3 clear): the tab is skipped
    pc_ui_commands no_equip = {0x3FF & ~8u, 0, 0};
    pc_ui_shell_open(&no_equip, 0);
    tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"abilities\""));
    pc_ui_shell_close();
}

// The Ashley screen: summary rows, jumps to Equipment, Square = the original Status, model view input.
static pc_ui_ashley ashley_model(int effects)
{
    pc_ui_ashley a = {};
    a.valid = 1;
    a.hp = 120, a.hp_max = 150, a.mp = 30, a.mp_max = 40, a.risk = 12;
    a.str = 50, a.intel = 41, a.agl = 33;
    a.limb_count = 2;
    std::snprintf(a.limbs[0].name, sizeof a.limbs[0].name, "R.ARM");
    a.limbs[0].hp = 10, a.limbs[0].hp_max = 40, a.limbs[0].state = PC_UI_LIMB_DAMAGED;
    std::snprintf(a.limbs[1].name, sizeof a.limbs[1].name, "HEAD");
    a.limbs[1].hp = 40, a.limbs[1].hp_max = 40, a.limbs[1].state = PC_UI_LIMB_EXCELLENT;
    a.effect_count = effects;
    std::snprintf(a.effects[0].name, sizeof a.effects[0].name, "STR-up");
    std::snprintf(a.effects[0].help, sizeof a.effects[0].help, "Stronger for a while.");
    a.effects[0].buff = 1, a.effects[0].seconds = 42;
    std::snprintf(a.effects[1].name, sizeof a.effects[1].name, "Curse");
    a.effects[1].buff = 0, a.effects[1].seconds = -1;
    std::snprintf(a.area, sizeof a.area, "Area A");
    std::snprintf(a.room, sizeof a.room, "Room \"B\"");
    a.play_h = 1, a.play_m = 2, a.play_s = 3;
    return a;
}
static void ashley_tests()
{
    while (!queue_empty()) {}
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    pc_ui_ashley a = ashley_model(2);
    pc_ui_shell_set_ashley(&a);
    CHECK(pc_ui_shell_tab() == 0);
    CHECK(pc_ui_shell_screen() == PC_UI_SCREEN_ASHLEY);
    CHECK(json_has("\"screen\":\"ashley\""));
    CHECK(json_has("\"ashley\":{\"valid\":true,\"hp\":120,\"hp_max\":150,\"mp\":30,\"mp_max\":40,\"risk\":12,"
                   "\"str\":50,\"int\":41,\"agl\":33"));
    CHECK(json_has("\"agl\":33,\"weapon\":\""));  // the equipment snapshot's worn weapon and shield names
    CHECK(json_has("\"limbs\":[{\"name\":\"R.ARM\",\"hp\":10,\"hp_max\":40,\"state\":1},"
                   "{\"name\":\"HEAD\",\"hp\":40,\"hp_max\":40,\"state\":4}]"));
    CHECK(json_has("\"effects\":[{\"name\":\"STR-up\",\"buff\":true,\"seconds\":42},"
                   "{\"name\":\"Curse\",\"buff\":false,\"seconds\":-1}]"));
    CHECK(json_has("\"area\":\"Area A\",\"room\":\"Room \\\"B\\\"\",\"play\":\"1:02:03\",\"model\":0"));
    CHECK(json_has("\"row\":\"weapon\""));
    CHECK(pc_ui_shell_ashley_rows() == 5);
    CHECK(pc_ui_shell_ashley() && pc_ui_shell_ashley()->hp == 120);
    // no command rows: ui_draw's row loop draws nothing
    {
        const char *label, *help, *value, *reason;
        int enabled;
        CHECK(!pc_ui_shell_row(0, &label, &help, &value, &enabled, &reason));
    }
    // rows: weapon, shield, condition, one per effect; Up/Down wrap
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"shield\""));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"condition\"") && pc_ui_shell_ashley_row() == PC_UI_ASH_CONDITION);
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"effect0\""));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"effect1\"") && pc_ui_shell_ashley_row() == PC_UI_ASH_EFFECT + 1);
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"weapon\""));
    tick(PC_UI_UP);
    CHECK(json_has("\"row\":\"effect1\""));
    // the drawing fits one chip ("+1" for the other): one effect row, focus clamped
    pc_ui_shell_ashley_effects_shown(1);
    CHECK(pc_ui_shell_ashley_rows() == 4 && json_has("\"row\":\"effect0\"") && json_has("\"effects_shown\":1,"));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"weapon\""));
    tick(PC_UI_UP);
    CHECK(json_has("\"row\":\"effect0\""));
    pc_ui_shell_ashley_effects_shown(-1); // all again
    CHECK(pc_ui_shell_ashley_rows() == 5 && json_has("\"effects_shown\":2,"));
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"effect1\""));
    // the model's state in the JSON: why none, the heap
    a.model_blocked = PC_UI_MODEL_NO_HEAP, a.heap_free = 4096, a.heap_need = 50000;
    pc_ui_shell_set_ashley(&a);
    CHECK(json_has("\"model_blocked\":\"heap\",\"heap_free\":4096,\"heap_need\":50000,"));
    // an effect wears off under the cursor: the focus stays in range
    a = ashley_model(1);
    pc_ui_shell_set_ashley(&a);
    CHECK(json_has("\"row\":\"effect0\""));
    CHECK(pc_ui_shell_ashley_rows() == 4);
    // Circle on condition or an effect: nothing (the inspector shows it)
    CHECK(tick(PC_UI_CONFIRM) == 0);
    tick(PC_UI_UP);
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(json_has("\"tab\":\"ashley\"") && queue_empty());
    // the model: wanted on Ashley while visible; a drag accumulates, turn consumes it
    CHECK(pc_ui_shell_wants_model());
    float yaw = 1, pitch = 1;
    pc_ui_shell_model_turn(&yaw, &pitch);
    CHECK(yaw == 0 && pitch == 0);
    pc_ui_shell_model_drag(3, 4);
    pc_ui_shell_model_drag(2, -1);
    pc_ui_shell_model_turn(&yaw, &pitch);
    CHECK(yaw == 5 && pitch == 3);
    pc_ui_shell_model_turn(&yaw, &pitch);
    CHECK(yaw == 0 && pitch == 0);
    // Circle on weapon: Equipment > Gear on the weapon slot, no request
    tick(PC_UI_UP); tick(PC_UI_UP);
    CHECK(json_has("\"row\":\"weapon\""));
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(json_has("\"tab\":\"equipment\"") && json_has("\"mode\":\"gear\"") && json_has("\"slot\":\"weapon\""));
    CHECK(queue_empty());
    CHECK(!pc_ui_shell_wants_model());
    pc_ui_shell_model_drag(9, 9); // not on Ashley: dropped
    pc_ui_shell_model_turn(&yaw, &pitch);
    CHECK(yaw == 0 && pitch == 0);
    // back on Ashley (cursor kept): Down, Circle on shield -> the shield slot, from Quick swap too
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"mode\":\"quick\""));
    tick(PC_UI_TAB_PREV);
    CHECK(json_has("\"row\":\"weapon\""));
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"tab\":\"equipment\"") && json_has("\"mode\":\"gear\"") && json_has("\"slot\":\"shield\""));
    tick(PC_UI_TAB_PREV);
    // the pointer: hover focuses a row, a click confirms it
    pc_ui_shell_mouse(PC_UI_MOUSE_ROW, 2, 0, 0);
    tick(0);
    CHECK(json_has("\"row\":\"condition\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_ROW, 9, 0, 0); // past the end
    tick(0);
    CHECK(json_has("\"row\":\"condition\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_ROW, 0, 1, 0);
    tick(0);
    CHECK(json_has("\"tab\":\"equipment\"") && json_has("\"slot\":\"weapon\""));
    tick(PC_UI_TAB_PREV);
    // Square anywhere on Ashley: the original Status (command 4 -> 5); Circle never opens it
    tick(PC_UI_DOWN); tick(PC_UI_DOWN); tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"effect0\""));
    CHECK(tick(PC_UI_ALT) == 5);
    CHECK(!pc_ui_shell_visible() && !pc_ui_shell_wants_model());
    pc_ui_shell_open(&all, 1); // back from Status: same tab and row
    CHECK(json_has("\"tab\":\"ashley\"") && json_has("\"row\":\"effect0\""));
    CHECK(pc_ui_shell_wants_model());
    // Equipment not unlocked: Circle on weapon stays
    pc_ui_commands no_equip = {0x3FF & ~8u, 0, 0};
    pc_ui_shell_open(&no_equip, 0);
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(json_has("\"tab\":\"ashley\""));
    // closed: the model is not wanted, a drag is dropped
    pc_ui_shell_close();
    CHECK(!pc_ui_shell_wants_model());
    pc_ui_shell_model_drag(1, 1);
    pc_ui_shell_model_turn(&yaw, &pitch);
    CHECK(yaw == 0 && pitch == 0);
}

// The pointer (pc_ui_shell_mouse, applied at the next tick) and the held hand-off.
static void mouse_tests()
{
    equipment = {};
    equipment.valid = 1;
    for (int i = 0; i < PC_UI_EQUIP_SLOTS; ++i)
        equipment.equipped[i] = item("", -1, 0, 0);
    equipment.equipped[PC_UI_SLOT_WEAPON] = item("Sword A", 0, 10, 50);
    for (int i = 0; i < PC_UI_QUICK; ++i)
        equipment.quick_weapon[i] = equipment.quick_shield[i] = -1;
    no_candidates();
    pc_ui_shell_set_equipment(&equipment);
    while (!queue_empty()) {}
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    pc_ui_equip_view v;

    // a click on a tab switches to it (and is the whole tick)
    pc_ui_shell_mouse(PC_UI_MOUSE_TAB, 1, 1, 0);
    CHECK(json_has("\"tab\":\"ashley\"")); // not before the tick
    CHECK(tick(0) == 0);
    CHECK(json_has("\"tab\":\"equipment\""));
    // hover focuses a gear slot, nothing confirms
    pc_ui_shell_mouse(PC_UI_MOUSE_SLOT, 4, 0, 0);
    tick(0);
    CHECK(json_has("\"slot\":\"head\""));
    CHECK(json_has("\"mode\":\"gear\""));
    CHECK(queue_empty());
    // the last hover between two ticks counts
    pc_ui_shell_mouse(PC_UI_MOUSE_SLOT, 2, 0, 0);
    pc_ui_shell_mouse(PC_UI_MOUSE_SLOT, 0, 0, 0);
    tick(0);
    CHECK(json_has("\"slot\":\"weapon\""));
    // a target from another mode does nothing (a quick card while on Gear)
    pc_ui_shell_mouse(PC_UI_MOUSE_QUICK, 3, 1, 0);
    tick(0);
    CHECK(json_has("\"mode\":\"gear\""));
    CHECK(queue_empty());
    // a click on a slot focuses and confirms: its pick list
    pc_ui_shell_mouse(PC_UI_MOUSE_SLOT, 0, 1, 0);
    tick(0);
    CHECK(json_has("\"mode\":\"pick\""));
    CHECK(req(PC_UI_REQ_CANDIDATES, PC_UI_SLOT_WEAPON, 0));
    weapons();
    pc_ui_shell_set_equipment(&equipment);
    pc_ui_shell_equip_view(&v);
    CHECK(v.mode == PC_UI_EQ_PICK && v.rows == 2 && v.cursor == 0 && !v.loading);
    CHECK(pc_ui_shell_list_item(1) && !std::strcmp(pc_ui_shell_list_item(1)->name, "Axe B"));
    CHECK(pc_ui_shell_list_item(2) == nullptr);
    // the wheel: one step per tick (down = negative), the rest waits
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, -1);
    tick(0);
    CHECK(json_has("\"cursor\":1"));
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, 2);
    tick(0);
    CHECK(json_has("\"cursor\":0"));
    tick(0);
    CHECK(json_has("\"cursor\":1")); // wraps up
    tick(0);
    CHECK(json_has("\"cursor\":1")); // nothing left
    // half notches add up
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, 0.5f);
    tick(0);
    CHECK(json_has("\"cursor\":1"));
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, 0.5f);
    tick(0);
    CHECK(json_has("\"cursor\":0"));
    // a row past the end does nothing; a click on a candidate equips it
    pc_ui_shell_mouse(PC_UI_MOUSE_LIST, 5, 1, 0);
    tick(0);
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"pick\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_LIST, 1, 1, 0);
    tick(0);
    CHECK(req(PC_UI_REQ_EQUIP, PC_UI_SLOT_WEAPON, 1));
    CHECK(json_has("\"mode\":\"gear\""));
    // a hint is its buttons: Square (take off) and, from a pick list, Cross (back)
    pc_ui_shell_mouse(PC_UI_MOUSE_BUTTON, PC_UI_ALT, 1, 0);
    tick(0);
    CHECK(req(PC_UI_REQ_EQUIP, PC_UI_SLOT_WEAPON, -1));
    pc_ui_shell_mouse(PC_UI_MOUSE_SLOT, 0, 1, 0);
    tick(0);
    pop();
    pc_ui_shell_mouse(PC_UI_MOUSE_BUTTON, PC_UI_BACK, 1, 0);
    CHECK(tick(0) == 0);
    CHECK(json_has("\"mode\":\"gear\""));
    // sub-tab clicks: Quick swap; a quick card click opens its weapon list
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, 1, 1, 0);
    tick(0);
    CHECK(json_has("\"mode\":\"quick\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_QUICK, 6, 0, 0);
    tick(0);
    CHECK(json_has("\"quick_slot\":\"right\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_QUICK, 5, 1, 0);
    tick(0);
    CHECK(json_has("\"quick_slot\":\"down\""));
    CHECK(json_has("\"mode\":\"quick_weapon\""));
    pop();
    // the quick shield step remembers the weapon picked (for the card)
    weapons();
    pc_ui_shell_set_equipment(&equipment);
    pc_ui_shell_mouse(PC_UI_MOUSE_LIST, 0, 1, 0);
    tick(0);
    pop();
    pc_ui_shell_equip_view(&v);
    CHECK(v.mode == PC_UI_EQ_QUICK_SHIELD && v.pending && !std::strcmp(v.pending->name, "Sword A"));
    CHECK(v.loading); // the shield list is not there yet
    // clicking the active sub-tab leaves its pick list
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, 1, 1, 0);
    tick(0);
    pc_ui_shell_equip_view(&v);
    CHECK(v.mode == PC_UI_EQ_QUICK && !v.pending);
    // hover and the wheel on the System list
    pc_ui_shell_mouse(PC_UI_MOUSE_TAB, 6, 1, 0); // System
    tick(0);
    CHECK(json_has("\"tab\":\"system\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 2, 0, 0);
    tick(0);
    CHECK(json_has("\"row\":\"score\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, -1);
    tick(0);
    CHECK(json_has("\"row\":\"manual\""));
    // a click on a game entry opens its command (Quick manual = 9 -> 10)
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 3, 1, 0);
    CHECK(tick(0) == 10);
    // closed: the pointer is ignored, nothing is left over at the next open
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 0, 1, 0);
    pc_ui_shell_open(&all, 1);
    CHECK(tick(0) == 0);
    CHECK(json_has("\"row\":\"manual\""));
    // a tab without rows is not clicked into (no Magick unlocked: bit 0)
    pc_ui_commands early = {0x3FF & ~1u, 0, 0};
    pc_ui_shell_open(&early, 1);
    pc_ui_shell_mouse(PC_UI_MOUSE_TAB, 4, 1, 0);
    tick(0);
    CHECK(json_has("\"tab\":\"system\""));
    // right click (ui_draw sends it as Cross) closes at the top
    pc_ui_shell_mouse(PC_UI_MOUSE_BUTTON, PC_UI_BACK, 1, 0);
    CHECK(tick(0) == -1);

    // held hand-off: closed but drawn until the bridge delivers; open/close clear it
    pc_ui_shell_open(&all, 1);
    CHECK(pc_ui_shell_drawn() && !pc_ui_shell_holding());
    CHECK(tick(PC_UI_EXIT) == -1);
    CHECK(!pc_ui_shell_visible() && !pc_ui_shell_drawn());
    pc_ui_shell_set_holding(1);
    CHECK(pc_ui_shell_drawn() && pc_ui_shell_holding() && !pc_ui_shell_visible());
    CHECK(json_has("\"visible\":false,\"holding\":true"));
    pc_ui_shell_set_holding(0);
    CHECK(!pc_ui_shell_drawn());
    pc_ui_shell_set_holding(1);
    pc_ui_shell_open(&all, 1);
    CHECK(!pc_ui_shell_holding());
    pc_ui_shell_set_holding(1);
    pc_ui_shell_close();
    CHECK(!pc_ui_shell_drawn());
}

// The System screen: list, pages, focus, capture, saving.
static void system_tests()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    fake::reset();
    pc_ui_shell_open(&all, 0);
    tick(PC_UI_TAB_PREV); // Ashley wraps to System
    CHECK(json_has("\"tab\":\"system\""));
    CHECK(json_has("\"screen\":\"system\""));
    CHECK(pc_ui_shell_screen() == PC_UI_SCREEN_SYSTEM);
    pc_ui_system_view v;
    pc_ui_shell_system_view(&v);
    CHECK(v.items == 11 && v.games == 4 && v.item == 0 && v.page == -1 && !v.entered && v.capture == -1);
    CHECK(json_has("\"system\":{\"mode\":\"list\",\"item\":\"data\",\"page\":\"\""));
    CHECK(json_has("\"status\":\"Saved settings to settings.cfg\""));
    // the game's entries follow menuFlags (Score not unlocked: bit 8)
    {
        pc_ui_commands no_score = {0x3FF & ~(1u << 8), 0, 0};
        pc_ui_shell_open(&no_score, 1);
        pc_ui_shell_system_view(&v);
        CHECK(v.items == 10 && v.games == 3);
        CHECK(!json_has("\"id\":\"score\""));
        pc_ui_shell_open(&all, 1);
    }
    // Down to Graphics (entry 4): the card previews its rows; Right enters it
    for (int i = 0; i < 4; ++i) tick(PC_UI_DOWN);
    pc_ui_shell_system_view(&v);
    CHECK(v.page == PC_UI_SETTINGS_GRAPHICS && !v.entered && v.rows == 5);
    CHECK(json_has("\"page\":\"graphics\",\"row\":\"p0_0\",\"rows\":[{\"id\":\"p0_0\""));
    tick(PC_UI_RIGHT);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered && v.page == PC_UI_SETTINGS_GRAPHICS && v.row == 0);
    CHECK(fake::changes == 0); // entering changes nothing
    // Up wraps to the last row; Left/Right change it with key repeat (repeat bits)
    tick(PC_UI_UP);
    CHECK(json_has("\"row\":\"p0_4\""));
    CHECK(json_has("\"enabled\":false,\"reason\":\"Needs the hardware renderer\""));
    tick(PC_UI_UP);
    pc_ui_input rep = {0, PC_UI_RIGHT}; // a repeat, no new press
    pc_ui_shell_tick(&rep);
    pc_ui_shell_tick(&rep);
    CHECK(fake::value[0][3] == 2 && fake::changes == 2);
    tick(PC_UI_LEFT);
    CHECK(fake::value[0][3] == 1);
    // Circle on a choice steps it; Cross back to the list saves (once)
    tick(PC_UI_CONFIRM);
    CHECK(fake::value[0][3] == 2);
    CHECK(fake::saves == 0 && fake::dirty);
    int flushes = fake::flushes;
    tick(PC_UI_BACK);
    CHECK(fake::saves == 1 && fake::flushes == flushes + 1 && !fake::dirty);
    pc_ui_shell_system_view(&v);
    CHECK(!v.entered && v.page == PC_UI_SETTINGS_GRAPHICS && v.row == 3); // the row is remembered
    CHECK(pc_ui_shell_visible());
    // focus clamp: Display's third row goes away when its first row turns off
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    pc_ui_shell_system_view(&v);
    CHECK(v.page == PC_UI_SETTINGS_DISPLAY && v.row == 2 && v.rows == 3);
    fake::extra = false; // changed elsewhere (the editor): the focus follows
    pc_ui_shell_system_view(&v);
    CHECK(v.rows == 2 && v.row == 1);
    tick(PC_UI_UP);
    tick(PC_UI_RIGHT); // the toggle: the extra row comes back, the focus stays on row 0
    pc_ui_shell_system_view(&v);
    CHECK(v.rows == 3 && v.row == 0);
    tick(PC_UI_RIGHT);
    pc_ui_shell_system_view(&v);
    CHECK(v.rows == 2 && v.row == 0);
    // a tab change leaves the page: saved, back on the list at the next visit
    int saves = fake::saves;
    tick(PC_UI_TAB_NEXT);
    CHECK(fake::saves == saves + 1);
    CHECK(json_has("\"tab\":\"ashley\""));
    tick(PC_UI_TAB_PREV);
    pc_ui_shell_system_view(&v);
    CHECK(!v.entered && v.page == PC_UI_SETTINGS_DISPLAY);
    // Bindings: Circle starts a capture; navigation, tabs and closing are ignored while it listens
    tick(PC_UI_DOWN); tick(PC_UI_DOWN); tick(PC_UI_DOWN); tick(PC_UI_DOWN); // Audio Interface Controls Bindings
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN); // Cross binding row
    tick(PC_UI_CONFIRM);
    CHECK(fake::capture == 1);
    CHECK(json_has("\"capture\":\"p5_1\""));
    pc_ui_shell_system_view(&v);
    CHECK(v.capture == 1);
    CHECK(tick(PC_UI_DOWN | PC_UI_TAB_NEXT | PC_UI_EXIT | PC_UI_BACK) == 0);
    pc_ui_shell_mouse(PC_UI_MOUSE_TAB, 0, 1, 0);
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, -1);
    CHECK(tick(0) == 0);
    pc_ui_shell_system_view(&v);
    CHECK(json_has("\"tab\":\"system\"") && v.entered && v.row == 1 && pc_ui_shell_visible());
    // the capture ends (the window code delivered a key): navigation again
    fake::capture = -1;
    tick(PC_UI_DOWN);
    pc_ui_shell_system_view(&v);
    CHECK(v.row == 2);
    // a right click (Back) cancels a capture
    tick(PC_UI_CONFIRM);
    CHECK(fake::capture == 2);
    pc_ui_shell_mouse(PC_UI_MOUSE_BUTTON, PC_UI_BACK, 1, 0);
    CHECK(tick(0) == 0);
    CHECK(fake::capture == -1 && fake::capture_done_kind == -1);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered); // the cancel was the whole click
    // closing the menu cancels a capture and saves
    tick(PC_UI_CONFIRM);
    fake::dirty = true;
    saves = fake::saves;
    fake::capture_done_kind = -9;
    pc_ui_shell_close(); // the bridge's abandon path
    CHECK(fake::capture == -1 && fake::capture_done_kind == -1 && fake::saves == saves + 1);
    // Triangle (once the capture is over) closes and saves too
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_system_view(&v);
    CHECK(!v.entered && v.page == PC_UI_SETTINGS_BINDINGS); // opens on the list
    tick(PC_UI_CONFIRM);
    fake::dirty = true;
    saves = fake::saves;
    CHECK(tick(PC_UI_EXIT) == -1);
    CHECK(fake::saves == saves + 1);
    // an original screen from the list saves first
    pc_ui_shell_open(&all, 0);
    tick(PC_UI_TAB_PREV);
    fake::dirty = true;
    saves = fake::saves;
    CHECK(tick(PC_UI_CONFIRM) == 7); // Data (6)
    CHECK(fake::saves == saves + 1);
    // Controls' action runs on Circle
    pc_ui_shell_open(&all, 1);
    for (int i = 0; i < 8; ++i) tick(PC_UI_DOWN); // Data -> Controls (entry 8)
    tick(PC_UI_CONFIRM);
    tick(PC_UI_UP); // the last row: the action
    int acts = fake::activations;
    tick(PC_UI_CONFIRM);
    CHECK(fake::activations == acts + 1);
    tick(PC_UI_BACK);

    // ---- the pointer ----
    // hover on the list moves its focus; a click on a page enters it
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 4, 0, 0);
    tick(0);
    CHECK(json_has("\"item\":\"graphics\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 5, 1, 0);
    tick(0);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered && v.page == PC_UI_SETTINGS_DISPLAY);
    // hover on the list does nothing while a page is open; hover on a row focuses it
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 6, 0, 0);
    tick(0);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered && v.page == PC_UI_SETTINGS_DISPLAY && v.item == 5);
    pc_ui_shell_mouse(PC_UI_MOUSE_SETTING, 1, 0, 0);
    tick(0);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered && v.page == PC_UI_SETTINGS_DISPLAY && v.row == 1);
    // the wheel moves the row focus inside a page
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, 1);
    tick(0);
    pc_ui_shell_system_view(&v);
    CHECK(v.row == 0);
    // an arrow (value delta) or a slider drag (value set) changes the row and focuses it
    int changes = fake::changes;
    pc_ui_shell_system_value(1, -1, 0, 0);
    tick(0);
    CHECK(fake::changes == changes + 1 && fake::value[1][1] == -1);
    pc_ui_shell_system_view(&v);
    CHECK(v.row == 1);
    // a click on another page leaves this one (saved) and enters that one
    fake::dirty = true;
    saves = fake::saves;
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 4, 1, 0);
    tick(0);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered && v.page == PC_UI_SETTINGS_GRAPHICS && fake::saves == saves + 1);
    // from the list, a value on the previewed page enters it on that row
    tick(PC_UI_BACK);
    pc_ui_shell_system_value(1, 0, 42.5, 1);
    tick(0);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered && v.row == 1 && fake::sets == 1 && fake::set_value == 42.5);
    // a drag in progress previews (the model applies costly sliders when it ends, with set 1)
    pc_ui_shell_system_value(1, 0, 3.0, 2);
    tick(0);
    CHECK(fake::previews == 1 && fake::preview_value == 3.0 && fake::sets == 1);
    // a click on a row of the previewed page enters the page on that row (no change)
    tick(PC_UI_BACK);
    changes = fake::changes;
    pc_ui_shell_mouse(PC_UI_MOUSE_SETTING, 3, 1, 0);
    tick(0);
    pc_ui_shell_system_view(&v);
    CHECK(v.entered && v.row == 3 && fake::changes == changes);
    // inside the page, a click on a row confirms it (a choice steps)
    pc_ui_shell_mouse(PC_UI_MOUSE_SETTING, 3, 1, 0);
    tick(0);
    CHECK(fake::changes == changes + 1);
    // a value for a row past the end is ignored
    changes = fake::changes;
    pc_ui_shell_system_value(9, 1, 0, 0);
    tick(0);
    CHECK(fake::changes == changes);
    pc_ui_shell_close();
    fake::reset();
    pc_ui_menus = 1;
}

// The Abilities screen: sub-tabs by menuFlags, assigning chain / defense abilities, break arts.
static pc_ui_abilities abilities_model()
{
    pc_ui_abilities a = {};
    a.valid = 1;
    a.equipped_category = 2; // sword
    a.weapon_drawn = 1;
    const int chain_ids[3] = {22, 23, 28}, defense_ids[2] = {40, 43};
    for (int t = 0; t < 2; ++t) {
        pc_ui_ability_list& l = a.lists[t];
        l.count = t ? 2 : 3;
        for (int i = 0; i < l.count; ++i) {
            l.entries[i].id = t ? defense_ids[i] : chain_ids[i];
            std::snprintf(l.entries[i].name, sizeof l.entries[i].name, "%s%d", t ? "Def" : "Chain", i);
            l.entries[i].cost = i + 1;
            l.entries[i].button = -1;
        }
        for (int b = 0; b < PC_UI_ABIL_BUTTONS; ++b)
            l.slots[b] = -1, l.slot_ids[b] = 0;
    }
    // Chain1 on Triangle
    a.lists[0].slots[1] = 1, a.lists[0].slot_ids[1] = 23, a.lists[0].entries[1].button = 1;
    for (int c = 0; c < PC_UI_ART_CATEGORIES; ++c) {
        pc_ui_art_category& k = a.categories[c];
        std::snprintf(k.name, sizeof k.name, "CAT%d", c + 1);
        for (int i = 0; i < PC_UI_ARTS; ++i) {
            k.arts[i].id = 184 + c * 4 + i;
            k.arts[i].state = PC_UI_ART_LOCKED;
            k.arts[i].kills_to_go = 10 * (i + 1);
        }
    }
    // the sword's first art learned and usable, the second pending; the dagger's first learned, not usable
    a.categories[1].arts[0].state = PC_UI_ART_LEARNED, a.categories[1].arts[0].usable = 1;
    a.categories[1].arts[1].state = PC_UI_ART_PENDING, a.categories[1].arts[1].kills_to_go = 0;
    a.categories[0].arts[0].state = PC_UI_ART_LEARNED;
    std::snprintf(a.categories[0].arts[0].reason, sizeof a.categories[0].arts[0].reason, "Only with a DAGGER in hand.");
    return a;
}
static bool abil_req(int kind, int slot, int id, int type = 0)
{
    pc_ui_request r = pop();
    return r.kind == kind && r.item == id && (kind != PC_UI_REQ_ABIL_MAP || (r.slot == slot && r.shield == type));
}
static void abilities_tests()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_abilities a = abilities_model();
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_set_abilities(&a);
    for (int i = 0; i < 3; ++i) tick(PC_UI_TAB_NEXT); // Ashley Equipment Items Abilities
    CHECK(json_has("\"tab\":\"abilities\""));
    CHECK(json_has("\"screen\":\"abilities\""));
    CHECK(pc_ui_shell_screen() == PC_UI_SCREEN_ABILITIES);
    CHECK(json_has("\"subs\":[\"chain\",\"defense\",\"arts\"],\"sub\":\"chain\",\"mode\":\"slots\""));
    CHECK(json_has("{\"button\":\"triangle\",\"id\":23,\"entry\":1,\"name\":\"Chain1\"}"));
    pc_ui_abilities_view v;
    pc_ui_shell_abilities_view(&v);
    CHECK(v.subs == 3 && v.kind == PC_UI_AB_CHAIN && v.mode == PC_UI_ABM_SLOTS && v.slot == 0);
    // slots: Left/Right move, Circle opens the pick on what the button holds (Triangle: Chain1)
    tick(PC_UI_RIGHT);
    tick(PC_UI_CONFIRM);
    pc_ui_shell_abilities_view(&v);
    CHECK(v.mode == PC_UI_ABM_PICK && v.slot == 1 && v.cursor == 1);
    CHECK(queue_empty());
    tick(PC_UI_DOWN);
    CHECK(tick(PC_UI_CONFIRM) == 0); // Chain2 -> Triangle, back on the slots
    CHECK(abil_req(PC_UI_REQ_ABIL_MAP, 1, 28, PC_UI_ABIL_CHAIN));
    pc_ui_shell_abilities_view(&v);
    CHECK(v.mode == PC_UI_ABM_SLOTS && v.slot == 1);
    // pick: Cross goes back to the slots without a request
    tick(PC_UI_CONFIRM);
    CHECK(tick(PC_UI_BACK) == 0 && pc_ui_shell_visible());
    CHECK(queue_empty());
    CHECK(json_has("\"mode\":\"slots\""));
    // the list: the original's direct presses; Triangle maps (the menu stays), Square maps
    tick(PC_UI_DOWN); // the cursor where the last pick left it (on Triangle's Chain1)
    CHECK(json_has("\"mode\":\"list\",\"slot\":\"triangle\",\"cursor\":1"));
    tick(PC_UI_UP);
    CHECK(json_has("\"mode\":\"list\",\"slot\":\"triangle\",\"cursor\":0"));
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(abil_req(PC_UI_REQ_ABIL_MAP, 0, 22, PC_UI_ABIL_CHAIN));
    CHECK(tick(PC_UI_EXIT) == 0 && pc_ui_shell_visible());
    CHECK(abil_req(PC_UI_REQ_ABIL_MAP, 1, 22, PC_UI_ABIL_CHAIN));
    CHECK(tick(PC_UI_ALT) == 0 && pc_ui_shell_visible());
    CHECK(abil_req(PC_UI_REQ_ABIL_MAP, 2, 22, PC_UI_ABIL_CHAIN));
    // Down wraps in the list; Up on the first row goes back to the slots
    tick(PC_UI_DOWN); tick(PC_UI_DOWN); tick(PC_UI_DOWN);
    CHECK(json_has("\"mode\":\"list\",\"slot\":\"triangle\",\"cursor\":0"));
    tick(PC_UI_UP);
    CHECK(json_has("\"mode\":\"slots\""));
    // Cross: list -> slots -> closes
    tick(PC_UI_DOWN);
    CHECK(tick(PC_UI_BACK) == 0 && json_has("\"mode\":\"slots\""));
    CHECK(tick(PC_UI_BACK) == -1 && !pc_ui_shell_visible());
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"tab\":\"abilities\"") && json_has("\"sub\":\"chain\""));
    // Square on the slots: the original Battle Abilities (command 2 -> 3)
    CHECK(tick(PC_UI_ALT) == 3 && !pc_ui_shell_visible());
    pc_ui_shell_open(&all, 1);
    // a pick list does not survive a reopen
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"pick\""));
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"mode\":\"slots\""));
    // R2: Defense; mapping there says so
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"sub\":\"defense\",\"mode\":\"slots\""));
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(abil_req(PC_UI_REQ_ABIL_MAP, 0, 43, PC_UI_ABIL_DEFENSE));
    // nothing learned: no list, no pick
    pc_ui_abilities none = a;
    none.lists[1].count = 0;
    pc_ui_shell_set_abilities(&none);
    CHECK(json_has("\"sub\":\"defense\",\"mode\":\"slots\""));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    CHECK(json_has("\"mode\":\"slots\"") && queue_empty());
    pc_ui_shell_set_abilities(&a);

    // Break Arts: the rail starts with the equipped category
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"sub\":\"arts\",\"mode\":\"categories\""));
    CHECK(json_has("\"order\":[2,1,3,4,5,6,7,8,9,10]"));
    pc_ui_shell_abilities_view(&v);
    CHECK(v.category == 2 && v.category_row == 0 && v.order[1] == 1);
    CHECK(json_has("\"state\":\"pending\""));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"arts\""));
    CHECK(tick(PC_UI_CONFIRM) == 0); // the learned art: a cast request, the shell stays until the bridge takes it
    CHECK(abil_req(PC_UI_REQ_ART_CAST, 0, 188));
    CHECK(pc_ui_shell_visible());
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM); // pending: nothing
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM); // locked: nothing
    CHECK(queue_empty());
    tick(PC_UI_LEFT);
    CHECK(json_has("\"mode\":\"categories\""));
    tick(PC_UI_DOWN); // the dagger (rail row 1)
    CHECK(json_has("\"category\":1,"));
    tick(PC_UI_RIGHT);
    tick(PC_UI_CONFIRM); // learned, not usable: still asked (the bridge buzzes)
    CHECK(abil_req(PC_UI_REQ_ART_CAST, 0, 184));
    CHECK(json_has("\"reason\":\"Only with a DAGGER in hand.\""));
    CHECK(tick(PC_UI_BACK) == 0 && json_has("\"mode\":\"categories\""));
    // the arts survive a reopen (back from the original screen after a cancelled target)
    tick(PC_UI_CONFIRM);
    pc_ui_shell_close();
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"sub\":\"arts\",\"mode\":\"arts\""));
    // Square: the original Break Arts (command 1 -> 2)
    CHECK(tick(PC_UI_ALT) == 2 && !pc_ui_shell_visible());

    // sub-tabs follow menuFlags: no Break Arts (bit 1), only Break Arts (bit 2 clear)
    pc_ui_commands no_arts = {0x3FF & ~2u, 0, 0};
    pc_ui_shell_open(&no_arts, 1);
    CHECK(json_has("\"subs\":[\"chain\",\"defense\"],\"sub\":\"chain\",\"mode\":\"slots\""));
    pc_ui_commands only_arts = {0x3FF & ~4u, 0, 0};
    pc_ui_shell_open(&only_arts, 1);
    CHECK(json_has("\"subs\":[\"arts\"],\"sub\":\"arts\",\"mode\":\"categories\""));
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"sub\":\"arts\""));

    // mouse: sub-tab clicks (index among the shown), a list row from the slots focuses it first
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, 0, 1, 0);
    tick(0);
    CHECK(json_has("\"sub\":\"chain\",\"mode\":\"slots\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_ABIL_ROW, 2, 1, 0);
    tick(0);
    CHECK(json_has("\"mode\":\"list\",\"slot\":\"triangle\",\"cursor\":2") && queue_empty());
    pc_ui_shell_mouse(PC_UI_MOUSE_ABIL_ROW, 2, 1, 0); // again: Circle there
    tick(0);
    CHECK(abil_req(PC_UI_REQ_ABIL_MAP, 0, 28, PC_UI_ABIL_CHAIN));
    pc_ui_shell_mouse(PC_UI_MOUSE_ABIL_SLOT, 2, 1, 0); // a slot from the list: onto it
    tick(0);
    CHECK(json_has("\"mode\":\"slots\",\"slot\":\"square\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_ABIL_SLOT, 2, 1, 0); // again: its pick
    tick(0);
    CHECK(json_has("\"mode\":\"pick\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, 2, 1, 0);
    tick(0);
    CHECK(json_has("\"sub\":\"arts\",\"mode\":\"categories\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_ART_CATEGORY, 2, 1, 0); // the sword: its arts
    tick(0);
    CHECK(json_has("\"mode\":\"arts\"") && json_has("\"category\":2,\"art\":0"));
    pc_ui_shell_mouse(PC_UI_MOUSE_ART, 0, 1, 0);
    tick(0);
    CHECK(abil_req(PC_UI_REQ_ART_CAST, 0, 188));
    pc_ui_shell_mouse(PC_UI_MOUSE_ART_CATEGORY, 5, 1, 0); // another category while in the arts: its arts, no cast
    tick(0);
    CHECK(json_has("\"mode\":\"arts\"") && json_has("\"category\":5,\"art\":0") && queue_empty());
    pc_ui_shell_close();
}

int main()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    CHECK(pc_ui_shell_visible());
    CHECK(json_has("\"tab\":\"ashley\""));
    CHECK(tick(0) == 0);
    // Ashley: Square opens the original Status (command 4 -> result 5)
    CHECK(tick(PC_UI_ALT) == 5);
    // the game shows Status; when it returns, the shell is opened again (remember = 1) on the same tab
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"tab\":\"ashley\""));
    // R1 -> Equipment, L1 back, L1 wraps to System
    tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"equipment\""));
    tick(PC_UI_TAB_PREV);
    tick(PC_UI_TAB_PREV);
    CHECK(json_has("\"tab\":\"system\""));
    // System list: Data, Options, Score, Quick manual, then the pages; Down x1 = Options (7) -> 8
    tick(PC_UI_DOWN);
    CHECK(tick(PC_UI_CONFIRM) == 8);
    pc_ui_shell_open(&all, 1);
    // Interface > Menus switches the setting, the shell stays; leaving the page saves
    fake::reset();
    for (int i = 0; i < 6; ++i) tick(PC_UI_DOWN); // Options -> Interface (4 games, then 7 pages)
    CHECK(json_has("\"row\":\"interface\""));
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(json_has("\"mode\":\"page\""));
    CHECK(json_has("\"page\":\"interface\",\"row\":\"ui_menus\""));
    CHECK(tick(PC_UI_RIGHT) == 0);
    CHECK(pc_ui_menus == 0);
    CHECK(fake::dirty && fake::saves == 0);
    CHECK(tick(PC_UI_BACK) == 0); // out of the page: saved, the shell stays
    CHECK(fake::saves == 1 && pc_ui_shell_visible());
    CHECK(json_has("\"mode\":\"list\""));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_LEFT);
    CHECK(pc_ui_menus == 1);
    tick(PC_UI_BACK);
    CHECK(fake::saves == 2);
    // not remembered: back to Ashley
    pc_ui_shell_open(&all, 0);
    CHECK(json_has("\"tab\":\"ashley\""));
    // Magick unselectable (MP 0: mask bit 0x20): greyed with a reason, and confirm still opens
    // the game's Magick (command 0 -> 1), as the original top level does
    pc_ui_commands no_magic = {0x3FF, 1, 0x20};
    pc_ui_shell_open(&no_magic, 0);
    for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT); // Ashley Equipment Items Abilities Magick
    CHECK(json_has("\"tab\":\"magick\""));
    CHECK(json_has("\"enabled\":false"));
    CHECK(json_has("\"reason\":\"Not enough MP.\""));
    CHECK(tick(PC_UI_CONFIRM) == 1);
    CHECK(!pc_ui_shell_visible());
    // the reason follows the game's order (lowest bit the command cares about): hanging (4) before MP (0x20)
    pc_ui_commands hanging = {0x3FF, 1, 0x24};
    pc_ui_shell_open(&hanging, 1);
    CHECK(json_has("\"reason\":\"Not while hanging from a ledge.\""));
    // a bit Magick does not care about (8, battle mode) gives the general reason
    pc_ui_commands other = {0x3FF, 1, 0x08};
    pc_ui_shell_open(&other, 1);
    CHECK(json_has("\"reason\":\"Not usable right now"));
    // selectable rows carry no reason
    pc_ui_shell_open(&all, 1);
    CHECK(!json_has("\"reason\""));
    // a command the game has not unlocked (menuFlags bit clear) hides its part: Abilities without Break Arts
    pc_ui_commands no_arts = {0x3FF & ~2u, 0, 0};
    pc_ui_shell_open(&no_arts, 0);
    for (int i = 0; i < 3; ++i) tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"abilities\""));
    CHECK(json_has("\"subs\":[\"chain\",\"defense\"]"));
    // Back and Exit close
    CHECK(tick(PC_UI_BACK) == -1);
    CHECK(!pc_ui_shell_visible());
    pc_ui_shell_open(&all, 0);
    CHECK(tick(PC_UI_EXIT) == -1);
    // pc_ui_shell_close hides without a result (another menu session: loot, shortcuts...)
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_close();
    CHECK(!pc_ui_shell_visible());
    CHECK(tick(0) == -1);
    // tabs without rows are skipped: no Magick (bit 0), no Abilities (bits 1, 2)
    {
        pc_ui_commands early = {0x3FF & ~7u, 0, 0};
        pc_ui_shell_open(&early, 0);
        tick(PC_UI_TAB_NEXT); tick(PC_UI_TAB_NEXT); // Equipment, Items
        CHECK(json_has("\"tab\":\"items\""));
        tick(PC_UI_TAB_NEXT); // Abilities and Magick are empty
        CHECK(json_has("\"tab\":\"lea_monde\""));
        tick(PC_UI_TAB_PREV);
        CHECK(json_has("\"tab\":\"items\""));
        // reopened (cursor memory) on a tab that is empty now: the next tab with rows
        pc_ui_shell_open(&all, 0);
        for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT);
        CHECK(json_has("\"tab\":\"magick\""));
        pc_ui_shell_open(&early, 1);
        CHECK(json_has("\"tab\":\"lea_monde\""));
    }
    {
        ui::Theme t;
        CHECK(t.accent == 0xFF3C36B5u); // default crimson #b5363c as ImU32 (ABGR)
        std::string err;
        CHECK(ui::theme_parse("{\"colors\":{\"accent\":\"#102030\"},\"sizes\":{\"tab\":17}}", t, &err));
        CHECK(t.accent == 0xFF302010u);
        CHECK(t.tab_size == 17.0f);
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"red\"}}", t, &err));
        CHECK(!err.empty());
        // colours: every digit hex (strtoul alone takes a sign or spaces)
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"#12345g\"}}", t, &err));
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"#+12345\"}}", t, &err));
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"# 12345\"}}", t, &err));
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"#-1020304\"}}", t, &err));
        CHECK(ui::theme_parse("{\"colors\":{\"accent\":\"#A0b0C0ff\"}}", t, &err));
        CHECK(t.accent == 0xFFC0B0A0u);
        // numbers: non-finite rejected, out of range clamped
        CHECK(!ui::theme_parse("{\"sizes\":{\"tab\":inf}}", t, &err));
        CHECK(!ui::theme_parse("{\"sizes\":{\"row_height\":nan}}", t, &err));
        CHECK(!ui::theme_parse("{\"motion\":{\"tab_slide\":1e999}}", t, &err));
        CHECK(!ui::theme_parse("{\"sizes\":{\"tab\":\"big\"}}", t, &err));
        CHECK(ui::theme_parse("{\"sizes\":{\"tab\":500,\"body\":-3,\"row_height\":2,\"margin_x\":-10,"
                              "\"rail_width\":99999},\"motion\":{\"tab_slide\":60,\"focus_grow\":-1}}", t, &err));
        CHECK(t.tab_size == 96.0f && t.body_size == 4.0f && t.row_height == 16.0f && t.margin_x == 0.0f);
        CHECK(t.rail_width == 2000.0f && t.tab_slide == 5.0f && t.focus_grow == 0.0f);
        // fonts: a path string or an error
        CHECK(!ui::theme_parse("{\"fonts\":{\"name\":5}}", t, &err));
        CHECK(err.find("fonts.name") != std::string::npos);
        CHECK(ui::theme_parse("{\"fonts\":{\"name\":\"x.ttf\"}}", t, &err) && t.font_name == "x.ttf");
    }
    {
        ui::Theme t; // stat deltas: up #79b86a, down #d0614f
        CHECK(t.up == 0xFF6AB879u && t.down == 0xFF4F61D0u);
        std::string err;
        CHECK(ui::theme_parse("{\"colors\":{\"up\":\"#010203\",\"down\":\"#040506\"}}", t, &err));
        CHECK(t.up == 0xFF030201u && t.down == 0xFF060504u);
    }
    equipment_tests();
    mouse_tests();
    system_tests();
    ashley_tests();
    abilities_tests();
    std::printf(failures ? "ui_shell_probe: %d failures\n" : "ui_shell_probe: ok\n", failures);
    return failures != 0;
}
