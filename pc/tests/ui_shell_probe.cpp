// The modern menu shell's navigation (pc/src/ui/ui_shell.cpp) without the game.
#include "ui/ui_shell.h"
#include "ui/map_frame.h"
#include "ui/data_frame.h"
#include "ui/container_frame.h"
#include "ui/data_plan.h"
#include "ui/ui_settings.h"
#include "ui/ui_workshop.h"
#include "ui/ui_manual.h"
#include "ui/manual_parse.h"
#include "ui/ui_theme.h"
#include "ui/ui_tint.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int pc_ui_menus = 1;
extern "C" float pc_ui_menu_opacity;
float pc_ui_menu_opacity = 0.94f; // project_settings.cpp (ui_theme.cpp theme())
extern "C" int pc_ui_menu_tint, pc_ui_menu_tint_custom[3];
int pc_ui_menu_tint = 0, pc_ui_menu_tint_custom[3] = {8, 7, 7}; // project_settings.cpp (ui_tint.h)
extern "C" int pc_ui_menu_backdrop(float rgba[4]);              // ui_theme.cpp
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
// Menu tint (ui_theme.cpp tint_theme / theme()): sRGB relative luminance and contrast of ImU32 colours.
static float lum(unsigned c)
{
    float l = 0;
    const float w[3] = {0.2126f, 0.7152f, 0.0722f};
    for (int i = 0; i < 3; ++i) {
        float v = (float)((c >> (8 * i)) & 0xFF) / 255.0f;
        l += w[i] * (v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f));
    }
    return l;
}
static float contrast_of(unsigned a, unsigned b)
{
    float x = lum(a), y = lum(b);
    return (std::fmax(x, y) + 0.05f) / (std::fmin(x, y) + 0.05f);
}
// How far the selection wash (accent_soft over the opaque panel) stands from the panel (sRGB distance).
static float wash_of(unsigned soft, unsigned panel)
{
    float a = (float)(soft >> 24) / 255.0f, d = 0;
    for (int i = 0; i < 3; ++i) {
        float x = ((float)((soft >> (8 * i)) & 0xFF) - (float)((panel >> (8 * i)) & 0xFF)) * a;
        d += x * x;
    }
    return std::sqrt(d);
}
static bool near_rgb(unsigned c, int r, int g, int b)
{
    return std::abs((int)(c & 0xFF) - r) <= 1 && std::abs((int)((c >> 8) & 0xFF) - g) <= 1
        && std::abs((int)((c >> 16) & 0xFF) - b) <= 1;
}
static void tint_tests()
{
    float saved_opacity = pc_ui_menu_opacity;
    pc_ui_menu_opacity = 0.94f;
    pc_ui_menu_tint = PC_UI_TINT_LEA_MONDE;
    ui::Theme file; // the theme file's colours (theme.json matches the defaults)
    const ui::Theme& lea = ui::theme();
    // Leá Monde: the theme untouched (the menus' look before the setting)
    CHECK(lea.ground == 0xF00E0F11u && lea.panel == file.panel && lea.rule == file.rule && lea.rule2 == file.rule2);
    CHECK(lea.dim == file.dim && lea.ash == file.ash && lea.stage == 0xFF0E0F11u);
    CHECK(lea.accent == file.accent && lea.accent_soft == file.accent_soft);
    int lea_rgb[3]; // Leá Monde's sliders: the theme file's ground (#110f0e) as v * 31 / 64
    pc_ui_tint_rgb(PC_UI_TINT_LEA_MONDE, lea_rgb);
    CHECK(lea_rgb[0] == 8 && lea_rgb[1] == 7 && lea_rgb[2] == 7);
    // Classic: deep navy #081134, the shades above it (spec values within 1)
    pc_ui_menu_tint = PC_UI_TINT_CLASSIC;
    const ui::Theme& c = ui::theme();
    CHECK((c.ground & 0xFFFFFFu) == 0x341108u && (c.ground >> 24) == 0xF0u);
    CHECK(near_rgb(c.panel, 14, 23, 60) && near_rgb(c.rule, 29, 38, 82) && near_rgb(c.rule2, 42, 51, 99));
    CHECK(c.dim == file.dim && c.ash == file.ash && c.bone == file.bone && c.accent == file.accent);
    CHECK(c.accent_soft == file.accent_soft);
    CHECK((c.stage & 0xFFFFFFu) == (c.ground & 0xFFFFFFu) && (c.panel >> 24) == (file.panel >> 24));
    std::string j;
    ui::theme_json(j);
    CHECK(j.find("\"tint\":\"classic\",\"tint_rgb\":[4,8,25]") != std::string::npos);
    CHECK(j.find("\"ground\":\"#081134f0\"") != std::string::npos);
    // the backdrop the presenters draw behind the Ashley model
    float rgba[4];
    CHECK(pc_ui_menu_backdrop(rgba) == 1);
    CHECK(std::fabs(rgba[0] - 8 / 255.0f) < 1e-6f && std::fabs(rgba[2] - 52 / 255.0f) < 1e-6f);
    CHECK(std::fabs(rgba[3] - 240 / 255.0f) < 1e-6f);
    // opacity: the ground's alpha; stage the tinted ground, opaque
    pc_ui_menu_opacity = 0.5f;
    CHECK((ui::theme().ground >> 24) == 128u && (ui::theme().ground & 0xFFFFFFu) == 0x341108u);
    CHECK(ui::theme().stage == (0x341108u | ((unsigned)(255.0f * 0.5f * 255.0f / 240.0f + 0.5f) << 24)));
    pc_ui_menu_opacity = 0.94f;
    // every preset and the corners: ground < panel < rule < rule2, readable dim and ash
    const int customs[][3] = {{0, 0, 0}, {31, 31, 31}, {31, 0, 0}, {0, 0, 31}, {0, 31, 0}, {31, 31, 0}, {16, 2, 30}};
    const float dim0 = contrast_of(file.dim, file.panel | 0xFF000000u);
    const float ash0 = contrast_of(file.ash, file.panel | 0xFF000000u);
    const float accent0 = contrast_of(file.accent, file.panel | 0xFF000000u);
    const float wash0 = wash_of(file.accent_soft, file.panel | 0xFF000000u);
    for (int k = 1; k < PC_UI_TINTS + 7; ++k) {
        if (k < PC_UI_TINT_CUSTOM)
            pc_ui_menu_tint = k;
        else if (k > PC_UI_TINT_CUSTOM) {
            pc_ui_menu_tint = PC_UI_TINT_CUSTOM;
            for (int ch = 0; ch < 3; ++ch)
                pc_ui_menu_tint_custom[ch] = customs[k - PC_UI_TINT_CUSTOM - 1][ch];
        } else
            continue;
        const ui::Theme& x = ui::theme(); // the cache follows the custom colour
        bool order = lum(x.ground) < lum(x.panel) && lum(x.panel) < lum(x.rule) && lum(x.rule) < lum(x.rule2);
        float dim = contrast_of(x.dim, x.panel | 0xFF000000u), ash = contrast_of(x.ash, x.panel | 0xFF000000u);
        bool readable = dim >= std::fmin(2.4f, dim0) - 0.01f && ash >= std::fmin(4.5f, ash0) - 0.01f;
        bool dark = lum(x.ground) <= 0.0215f; // the cap (0.02) and rounding
        // the selection: the accent's marks and the wash stand out as on the theme's own panel (Crimson,
        // 31 0 0: a red wash on a red panel lifts toward bone)
        float accent = contrast_of(x.accent, x.panel | 0xFF000000u), wash = wash_of(x.accent_soft, x.panel | 0xFF000000u);
        bool selected = accent >= std::fmin(2.5f, accent0) - 0.01f && wash >= wash0 - 0.5f;
        CHECK(order && readable && dark && selected);
        if (!(order && readable && dark && selected))
            std::printf("  tint %d custom %d %d %d: ground %08x panel %08x dim %.2f ash %.2f accent %.2f wash %.1f\n",
                k, pc_ui_menu_tint_custom[0], pc_ui_menu_tint_custom[1], pc_ui_menu_tint_custom[2], x.ground, x.panel,
                (double)dim, (double)ash, (double)accent, (double)wash);
    }
    // Crimson and 31 0 0: the wash lifted (it was a red on red), its alpha kept; the accent itself unchanged
    pc_ui_menu_tint = PC_UI_TINT_CRIMSON;
    CHECK(ui::theme().accent_soft != file.accent_soft && (ui::theme().accent_soft >> 24) == (file.accent_soft >> 24));
    CHECK(ui::theme().accent == file.accent);
    pc_ui_menu_tint = PC_UI_TINT_CUSTOM;
    pc_ui_menu_tint_custom[0] = 31, pc_ui_menu_tint_custom[1] = 0, pc_ui_menu_tint_custom[2] = 0;
    CHECK(ui::theme().accent_soft != file.accent_soft
        && wash_of(ui::theme().accent_soft, ui::theme().panel | 0xFF000000u) >= wash0 - 0.5f);
    // the cache: a custom channel changes the theme on the next call
    pc_ui_menu_tint = PC_UI_TINT_CUSTOM;
    pc_ui_menu_tint_custom[0] = 0, pc_ui_menu_tint_custom[1] = 0, pc_ui_menu_tint_custom[2] = 20;
    unsigned before = ui::theme().ground;
    pc_ui_menu_tint_custom[0] = 20;
    CHECK(ui::theme().ground != before && (ui::theme().ground & 0xFF) > (before & 0xFF));
    // white: capped dark grey
    pc_ui_menu_tint_custom[0] = pc_ui_menu_tint_custom[1] = pc_ui_menu_tint_custom[2] = 31;
    CHECK(near_rgb(ui::theme().ground, 39, 39, 39));
    // back to the default: identical again
    pc_ui_menu_tint = PC_UI_TINT_LEA_MONDE;
    pc_ui_menu_tint_custom[0] = 8, pc_ui_menu_tint_custom[1] = 7, pc_ui_menu_tint_custom[2] = 7;
    CHECK(ui::theme().ground == 0xF00E0F11u && ui::theme().panel == file.panel);
    pc_ui_menu_opacity = saved_opacity;
}

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
    pc_ui_request r = {};
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
    // Condition: the panel as a whole is titled "Condition · <the worst part's state>" with a summary, never
    // a part (before: the worst part's name, "R.ARM · Damaged", whatever was focused); a part (the pointer on
    // it, or Left / Right) is titled with its name, its line its HP
    CHECK(json_has("\"limb\":-1,\"details\":\"Condition \xc2\xb7 Damaged\",\"details_line\":\"1 of 2 parts hurt; "
                   "the worst is R.ARM (Damaged).\""));
    tick(PC_UI_RIGHT);
    CHECK(pc_ui_shell_ashley_limb() == 0 && json_has("\"limb\":0,\"details\":\"R.ARM \xc2\xb7 Damaged\""));
    tick(PC_UI_RIGHT);
    CHECK(json_has("\"details\":\"HEAD \xc2\xb7 Excellent\",\"details_line\":\"HEAD: 40 / 40 HP, Excellent.\""));
    tick(PC_UI_RIGHT); // past the last part: the panel again
    CHECK(pc_ui_shell_ashley_limb() == -1 && json_has("\"details\":\"Condition \xc2\xb7 Damaged\""));
    tick(PC_UI_LEFT);
    CHECK(pc_ui_shell_ashley_limb() == 1);
    pc_ui_shell_mouse(PC_UI_MOUSE_ROW, PC_UI_ASH_CONDITION, 0, 0); // the pointer on the panel, off the parts
    tick(0);
    CHECK(pc_ui_shell_ashley_limb() == -1 && json_has("\"details\":\"Condition \xc2\xb7 Damaged\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_LIMB, 0, 0, 0); // on a part
    tick(0);
    CHECK(pc_ui_shell_ashley_limb() == 0 && json_has("\"details\":\"R.ARM \xc2\xb7 Damaged\""));
    tick(PC_UI_UP); // another row: no part, no Condition text
    CHECK(pc_ui_shell_ashley_limb() == -1 && json_has("\"row\":\"shield\",\"limb\":-1,\"details\":\"\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_LIMB, 1, 0, 0); // a part from another row: Condition, that part
    tick(0);
    CHECK(pc_ui_shell_ashley_row() == PC_UI_ASH_CONDITION && pc_ui_shell_ashley_limb() == 1);
    pc_ui_shell_mouse(PC_UI_MOUSE_LIMB, 7, 0, 0); // past the parts: nothing
    tick(0);
    CHECK(pc_ui_shell_ashley_limb() == 1);
    tick(PC_UI_UP);
    tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"condition\",\"limb\":-1"));
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
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, 1);
    tick(0);
    CHECK(json_has("\"row\":\"score\""));
    // a click on a game entry opens its command (Score = 8 -> 9; the Quick manual is native: see manual_tests)
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 2, 1, 0);
    CHECK(tick(0) == 9);
    // closed: the pointer is ignored, nothing is left over at the next open
    pc_ui_shell_mouse(PC_UI_MOUSE_PAGE, 0, 1, 0);
    pc_ui_shell_open(&all, 1);
    CHECK(tick(0) == 0);
    CHECK(json_has("\"row\":\"score\""));
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

// The Magick screen: sub-tabs per school learned and Teleport, the LV picker, casting and teleporting.
static pc_ui_magick magick_model()
{
    pc_ui_magick m = {};
    m.valid = 1;
    m.mp = 30, m.mp_max = 40;
    // Warlock: Solid Shock (97), Explosion (106) with LV1-3; Shaman: Heal (130); Sorcerer, Enchanter: nothing
    m.schools[0].unlocked = 1;
    m.schools[0].count = 2;
    pc_ui_spell& a = m.schools[0].spells[0];
    a.id = 97, a.mp = 5, a.levels = 1;
    std::snprintf(a.name, sizeof a.name, "Solid Shock");
    a.level[0] = {97, 5, 1, ""};
    pc_ui_spell& b = m.schools[0].spells[1];
    b.id = 106, b.mp = 8, b.levels = 3;
    std::snprintf(b.name, sizeof b.name, "Explosion");
    b.level[0] = {106, 8, 1, ""};
    b.level[1] = {107, 16, 1, ""};
    b.level[2] = {108, 32, 0, "Not enough MP."};
    m.schools[1].unlocked = 1;
    m.schools[1].count = 1;
    pc_ui_spell& c = m.schools[1].spells[0];
    c.id = 130, c.mp = 6, c.levels = 1;
    std::snprintf(c.name, sizeof c.name, "Heal");
    c.level[0] = {130, 6, 0, "Silenced: spells cannot be cast."};
    // Teleport: three circles found, the player on the second (cost 15)
    m.teleport = 1, m.teleport_enabled = 1;
    m.destination_count = 3;
    const int points[3] = {0, 3, 7}, costs[3] = {27, 15, 31};
    for (int i = 0; i < 3; ++i) {
        pc_ui_destination& d = m.destinations[i];
        d.point = points[i], d.cost = costs[i], d.current = costs[i] == 15, d.usable = costs[i] <= 30;
        std::snprintf(d.name, sizeof d.name, "Circle%d", points[i]);
    }
    std::snprintf(m.destinations[2].reason, sizeof m.destinations[2].reason, "Not enough MP.");
    return m;
}
static void magick_tests()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_magick m = magick_model();
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_set_magick(&m);
    for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT); // Ashley Equipment Items Abilities Magick
    CHECK(json_has("\"tab\":\"magick\"") && json_has("\"screen\":\"magick\""));
    CHECK(pc_ui_shell_screen() == PC_UI_SCREEN_MAGICK);
    CHECK(json_has("\"subs\":[\"warlock\",\"shaman\",\"teleport\"],\"sub\":\"warlock\",\"cursor\":0,\"rows\":2"));
    CHECK(json_has("\"focus\":97"));
    pc_ui_magick_view v;
    pc_ui_shell_magick_view(&v);
    CHECK(v.subs == 3 && v.kind == PC_UI_SCHOOL_WARLOCK && v.cursor == 0 && v.rows == 2 && v.level == 0);
    // Left/Right on a spell without levels: nothing
    tick(PC_UI_RIGHT);
    CHECK(json_has("\"level\":0,\"focus\":97"));
    // Circle: a cast request, the shell stays until the bridge takes it
    CHECK(tick(PC_UI_CONFIRM) == 0 && pc_ui_shell_visible());
    pc_ui_request r = pop();
    CHECK(r.kind == PC_UI_REQ_SPELL_CAST && r.item == 97);
    CHECK(queue_empty());
    // the levelled spell: Right picks LV2, LV3, stops at LV3; Left back; Circle casts the level's id
    tick(PC_UI_DOWN);
    CHECK(json_has("\"cursor\":1,\"rows\":2,\"level\":0,\"focus\":106"));
    tick(PC_UI_RIGHT);
    tick(PC_UI_RIGHT);
    tick(PC_UI_RIGHT);
    CHECK(json_has("\"level\":2,\"focus\":108"));
    tick(PC_UI_LEFT);
    CHECK(json_has("\"level\":1,\"focus\":107"));
    tick(PC_UI_CONFIRM);
    r = pop();
    CHECK(r.kind == PC_UI_REQ_SPELL_CAST && r.item == 107);
    // the level is kept on the other row but shown as LV1 there (no levels); Down wraps
    tick(PC_UI_DOWN);
    CHECK(json_has("\"cursor\":0,\"rows\":2,\"level\":0,\"focus\":97"));
    tick(PC_UI_UP);
    CHECK(json_has("\"level\":1,\"focus\":107"));
    // a greyed level is still asked (the bridge buzzes)
    tick(PC_UI_RIGHT);
    tick(PC_UI_CONFIRM);
    r = pop();
    CHECK(r.kind == PC_UI_REQ_SPELL_CAST && r.item == 108);
    CHECK(json_has("\"reason\":\"Not enough MP.\""));
    // the cursor survives a reopen (back after a cancelled target); the level is the game's LV
    // picker memory (cursorMemories[12], which the bridge wrote while the spell was focused)
    pc_ui_shell_close();
    m.level_memory = 2;
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_set_magick(&m);
    CHECK(json_has("\"sub\":\"warlock\",\"cursor\":1,\"rows\":2,\"level\":2,\"focus\":108"));
    // the game's memory wins at an open (the original screen may have moved it), once per open
    pc_ui_shell_close();
    m.level_memory = 0;
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_set_magick(&m);
    CHECK(json_has("\"cursor\":1,\"rows\":2,\"level\":0,\"focus\":106"));
    tick(PC_UI_RIGHT);
    pc_ui_shell_set_magick(&m);
    CHECK(json_has("\"level\":1,\"focus\":107"));
    pc_ui_shell_close();
    m.level_memory = 2;
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_set_magick(&m);
    CHECK(json_has("\"level\":2,\"focus\":108"));
    // a focused spell with fewer levels lowers the level and keeps it (levelledSpellInit)
    {
        pc_ui_magick two = m;
        two.schools[0].spells[1].levels = 2;
        pc_ui_shell_set_magick(&two);
        CHECK(json_has("\"level\":1,\"focus\":107"));
        pc_ui_shell_magick_view(&v);
        CHECK(v.level == 1);
        pc_ui_shell_set_magick(&m);
        CHECK(json_has("\"level\":1,\"focus\":107"));
        tick(PC_UI_RIGHT);
    }
    CHECK(json_has("\"level\":2,\"focus\":108"));
    // R2: Shaman, its own cursor; Circle on a silenced spell is still asked
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"sub\":\"shaman\",\"cursor\":0,\"rows\":1"));
    tick(PC_UI_CONFIRM);
    r = pop();
    CHECK(r.kind == PC_UI_REQ_SPELL_CAST && r.item == 130);
    // R2: Teleport, on the current circle; Circle asks for that save point
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"sub\":\"teleport\",\"cursor\":1,\"rows\":3,\"level\":0,\"focus\":3"));
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    r = pop();
    CHECK(r.kind == PC_UI_REQ_TELEPORT && r.slot == 7);
    CHECK(queue_empty());
    // R2 wraps to Warlock, L2 back to Teleport
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"sub\":\"warlock\""));
    tick(PC_UI_SUB_PREV);
    CHECK(json_has("\"sub\":\"teleport\",\"cursor\":2"));
    // a new open puts Teleport's cursor on the current circle again
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_set_magick(&m);
    CHECK(json_has("\"sub\":\"teleport\",\"cursor\":1"));
    // Square: the original Magick (command 0 -> 1); Cross closes
    CHECK(tick(PC_UI_ALT) == 1 && !pc_ui_shell_visible());
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_set_magick(&m);
    CHECK(tick(PC_UI_BACK) == -1 && !pc_ui_shell_visible());
    // the sub-tabs follow the snapshot: no Teleport, a school learned later
    pc_ui_magick fewer = m;
    fewer.teleport = 0;
    fewer.schools[1].unlocked = 0;
    fewer.schools[3].unlocked = 1;
    fewer.schools[3].count = 1;
    fewer.schools[3].spells[0] = m.schools[1].spells[0];
    pc_ui_shell_open(&all, 1);
    pc_ui_shell_set_magick(&fewer);
    CHECK(json_has("\"subs\":[\"warlock\",\"enchanter\"],\"sub\":\"warlock\""));
    // nothing learned and no Teleport: no sub-tab, Circle asks for nothing, Square still opens the original
    pc_ui_magick none = {};
    none.valid = 1;
    pc_ui_shell_set_magick(&none);
    CHECK(json_has("\"subs\":[],\"sub\":\"\"") && json_has("\"focus\":-1"));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    CHECK(queue_empty());
    CHECK(tick(PC_UI_ALT) == 1);
    // the tab follows menuFlags bit 0 (the Magick command)
    pc_ui_commands no_magic = {0x3FF & ~1u, 0, 0};
    pc_ui_shell_open(&no_magic, 0);
    for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"lea_monde\""));
    // mouse: a sub-tab click (index among the shown), hover a row focuses, a click casts, a level click picks
    m.level_memory = 0; // the game's LV picker on LV1
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_set_magick(&m);
    for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"magick\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, 0, 1, 0);
    tick(0);
    CHECK(json_has("\"sub\":\"warlock\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_SPELL, 0, 0, 0);
    tick(0);
    CHECK(json_has("\"cursor\":0,") && queue_empty());
    pc_ui_shell_mouse(PC_UI_MOUSE_SPELL, 1, 1, 0);
    tick(0);
    r = pop();
    CHECK(r.kind == PC_UI_REQ_SPELL_CAST && r.item == 106 && json_has("\"cursor\":1,"));
    pc_ui_shell_mouse(PC_UI_MOUSE_SPELL_LEVEL, 2, 1, 0);
    tick(0);
    CHECK(json_has("\"level\":2,\"focus\":108") && queue_empty());
    pc_ui_shell_mouse(PC_UI_MOUSE_SPELL_LEVEL, 3, 1, 0); // past its levels: nothing
    tick(0);
    CHECK(json_has("\"level\":2,\"focus\":108") && queue_empty());
    pc_ui_shell_mouse(PC_UI_MOUSE_SPELL, 5, 1, 0); // past the end: nothing
    tick(0);
    CHECK(queue_empty());
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, 2, 1, 0);
    tick(0);
    pc_ui_shell_mouse(PC_UI_MOUSE_SPELL, 0, 1, 0);
    tick(0);
    r = pop();
    CHECK(r.kind == PC_UI_REQ_TELEPORT && r.slot == 0);
    pc_ui_shell_close();
}


// ---- Items ----
static pc_ui_items items_model_storage;
static pc_ui_items& items_model()
{
    pc_ui_items& m = items_model_storage;
    m = pc_ui_items();
    m.valid = 1;
    static const int caps[PC_UI_ITEM_CATEGORIES] = {8, 16, 16, 8, 16, 48, 64};
    static const int counts[PC_UI_ITEM_CATEGORIES] = {2, 1, 0, 0, 0, 1, 3};
    int n = 0;
    for (int c = 0; c < PC_UI_ITEM_CATEGORIES; ++c) {
        m.capacity[c] = caps[c];
        m.first[c] = n;
        m.count[c] = counts[c];
        for (int r = 0; r < counts[c]; ++r) {
            pc_ui_item_row& e = m.rows[n++];
            e.inventory = r + 2;
            e.id = 100 + c * 10 + r;
            e.parent = e.parent_category = -1;
            std::snprintf(e.name, sizeof e.name, "c%dr%d", c, r);
            e.discard = 1;
        }
        m.sort_count[c] = c == PC_UI_IC_MISC ? 2 : 3;
        m.sort_stats[c][0] = 0;
        m.sort_stats[c][1] = c == PC_UI_IC_MISC ? 56 : 8;
        m.sort_stats[c][2] = 9;
    }
    // weapons: row 0 equipped (not discardable); blades: row 0 in weapon 1 (row 1 of weapons)
    m.rows[0].mark = PC_UI_MARK_EQUIPPED;
    m.rows[0].discard = 0;
    std::snprintf(m.rows[0].discard_reason, sizeof m.rows[0].discard_reason, "Equipped.");
    pc_ui_item_row& blade = m.rows[m.first[PC_UI_IC_BLADE]];
    blade.mark = PC_UI_MARK_SET;
    blade.discard = 0;
    blade.parent_category = PC_UI_IC_WEAPON;
    blade.parent = m.rows[1].inventory;
    // misc: a stack of 25 (usable), a single (not usable: MP), a key (nothing)
    pc_ui_item_row* misc = &m.rows[m.first[PC_UI_IC_MISC]];
    misc[0].count = 25, misc[0].usable = 1, misc[0].discard_all = 1, misc[0].action = 0xE0;
    misc[1].count = 1, misc[1].discard_all = 1;
    std::snprintf(misc[1].use_reason, sizeof misc[1].use_reason, "Not enough MP.");
    misc[2].count = 1, misc[2].discard = 0;
    std::snprintf(misc[2].use_reason, sizeof misc[2].use_reason, "Cannot use.");
    std::snprintf(misc[2].discard_reason, sizeof misc[2].discard_reason, "Cannot discard.");
    std::snprintf(misc[2].discard_all_reason, sizeof misc[2].discard_all_reason, "Cannot discard.");
    std::snprintf(m.stat_name[0], sizeof m.stat_name[0], "Kind");
    std::snprintf(m.stat_name[56], sizeof m.stat_name[56], "Quantity");
    std::snprintf(m.no_search, sizeof m.no_search, "Not part of anything.");
    for (int i = 0; i < 3; ++i)
        std::snprintf(m.discard_prompt[i], sizeof m.discard_prompt[i], "Discard %d?", i);
    return m;
}
static bool item_req(int kind, int cat, int item_, int shield, int count = 0)
{
    pc_ui_request r = pop();
    bool ok = r.kind == kind && r.slot == cat && r.item == item_ && r.shield == shield && r.count == count;
    if (!ok)
        std::printf("  item request: kind %d slot %d item %d shield %d count %d\n", r.kind, r.slot, r.item, r.shield,
            r.count);
    return ok;
}

// ---- The Loot screen (ui_loot.cpp; a direct session, no tabs) ----
static void loot_tick(unsigned buttons)
{
    pc_ui_input in = {buttons, buttons};
    pc_ui_loot_tick(&in);
}
static pc_ui_request loot_pop()
{
    pc_ui_request r = {};
    if (!pc_ui_loot_pop_request(&r))
        r.kind = PC_UI_REQ_NONE;
    return r;
}
static bool loot_req(int kind, int slot, int item_, int shield, int count = -99)
{
    pc_ui_request r = loot_pop();
    return r.kind == kind && r.slot == slot && r.item == item_ && r.shield == shield && (count == -99 || r.count == count);
}
static bool loot_empty_queue() { return loot_pop().kind == PC_UI_REQ_NONE; }
static pc_ui_loot loot_model(int keys, int claimed, int chest)
{
    static pc_ui_loot m;
    m = pc_ui_loot();
    m.valid = 1;
    m.chest = chest;
    m.claimed = claimed;
    m.keys = keys;
    m.count = 3;
    const char* names[3] = {"Tovarisch", "Cure Root", "Iron Key"};
    const int cats[3] = {PC_UI_IC_WEAPON, PC_UI_IC_MISC, PC_UI_IC_MISC};
    for (int i = 0; i < 3; ++i) {
        pc_ui_loot_row& e = m.rows[i];
        e.category = cats[i];
        e.record = i == 2 ? 1 : 0;
        std::snprintf(e.name, sizeof e.name, "%s", names[i]);
        e.fits = i != 0; // the weapon has no room
        e.key = i == 2;
        e.organize = i == 0 ? PC_UI_LOOT_ORG_DISASSEMBLE : i == 1 ? PC_UI_LOOT_ORG_CONSOLIDATE : PC_UI_LOOT_ORG_NONE;
    }
    std::snprintf(m.leave_rest, sizeof m.leave_rest, "Items remain. Leave them?");
    std::snprintf(m.leave_untouched, sizeof m.leave_untouched, "Discard all items?");
    std::snprintf(m.keys_text, sizeof m.keys_text, "You cannot discard Iron Key.");
    std::snprintf(m.disassemble_ask, sizeof m.disassemble_ask, "Disassemble Tovarisch?");
    for (int c = 0; c < PC_UI_ITEM_CATEGORIES; ++c)
        m.bag_capacity[c] = 8;
    return m;
}
static bool loot_has(const char* needle)
{
    static char buf[1 << 16];
    pc_ui_loot_json(buf, sizeof buf);
    return std::strstr(buf, needle) != nullptr;
}

// ---- The Workshop (Items > Workshop: ui_workshop.cpp) ----
static pc_ui_workshop ws_storage;
static pc_ui_workshop& ws_model()
{
    pc_ui_workshop& m = ws_storage;
    m = pc_ui_workshop();
    m.valid = 1;
    m.workshop = 6;
    m.materials = 0xFE;
    for (int i = 0; i < PC_UI_WS_ACTIONS; ++i)
        m.action_enabled[i] = 1, m.action_reason[i] = -1;
    auto row = [](pc_ui_ws_row& r, int inv, const char* name) {
        r = pc_ui_ws_row();
        r.inventory = inv;
        r.id = inv + 1;
        r.parent = -1;
        r.gems[0] = r.gems[1] = r.gems[2] = -1;
        std::snprintf(r.name, sizeof r.name, "%s", name);
    };
    m.count[PC_UI_WSL_WEAPONS] = 2;
    row(m.weapons[0], 0, "Fandango");
    m.weapons[0].mark = PC_UI_MARK_EQUIPPED;
    m.weapons[0].gem_slots = 2;
    row(m.weapons[1], 2, "Tovarisch");
    for (int i = 0; i < PC_UI_WS_NAME_LEN; ++i)
        m.weapons[1].field[i] = i < 2 ? (unsigned char)(10 + i) : 0x8F; // "AB"
    m.count[PC_UI_WSL_BLADES] = 2;
    row(m.blades[0], 0, "Broad Sword");
    m.blades[0].parent = 0;
    m.blades[0].material_ok = 1;
    row(m.blades[1], 1, "Hand Axe");
    m.blades[1].material_ok = 1;
    m.blades[1].grips_ok = 1; // grip row 0 fits
    m.count[PC_UI_WSL_GRIPS] = 1;
    row(m.grips[0], 1, "Wooden Grip");
    m.grips[0].gem_slots = 1;
    m.count[PC_UI_WSL_SHIELDS] = 1;
    row(m.shields[0], 0, "Buckler");
    m.shields[0].gem_slots = 1; // no gem: not to disassemble
    m.count[PC_UI_WSL_ARMOR] = 2;
    row(m.armor[0], 6, "Leather Glove");
    m.armor[0].material_ok = 1;
    row(m.armor[1], 7, "Silver Glove"); // a material this workshop does not work
    m.count[PC_UI_WSL_GEMS] = 2;
    row(m.gems[0], 0, "Feldspar");
    row(m.gems[1], 1, "Pyrite");
    std::snprintf(m.text[25], sizeof m.text[25], "No gems attached to this shield.");
    std::snprintf(m.text[49], sizeof m.text[49], "Only one item of this category is listed.");
    std::snprintf(m.text[51], sizeof m.text[51], "You cannot carry any more weapons.");
    std::snprintf(m.text[22], sizeof m.text[22], "Armor of this material cannot be altered at this workshop.");
    m.grid[0][0] = 10, m.grid[0][1] = 11, m.grid[2][1] = 37;
    std::snprintf(m.glyph[10], sizeof m.glyph[10], "A");
    std::snprintf(m.glyph[11], sizeof m.glyph[11], "B");
    std::snprintf(m.glyph[37], sizeof m.glyph[37], "b");
    std::snprintf(m.glyph[0x8F], sizeof m.glyph[0x8F], " ");
    return m;
}
static bool ws_req(int kind, int slot, int item_, int shield, int count = 0)
{
    pc_ui_request r = pop();
    bool ok = r.kind == kind && r.slot == slot && r.item == item_ && r.shield == shield && r.count == count;
    if (!ok)
        std::printf("  workshop request: kind %d slot %d item %d shield %d count %d\n", r.kind, r.slot, r.item,
            r.shield, r.count);
    return ok;
}

static void workshop_tests()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_set_items(&items_model());
    pc_ui_workshop_set(&ws_model());
    for (int i = 0; i < 2; ++i) tick(PC_UI_TAB_NEXT);
    // L2 from weapons: the Workshop (after misc); R2 back to weapons; a click on its chip
    tick(PC_UI_SUB_PREV);
    CHECK(pc_ui_shell_workshop_shown() && json_has("\"category\":\"workshop\"") && json_has("\"mode\":\"rows\""));
    int cat, inv;
    CHECK(!pc_ui_shell_items_focus(&cat, &inv));
    tick(PC_UI_SUB_NEXT);
    CHECK(!pc_ui_shell_workshop_shown() && json_has("\"category\":\"weapons\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, PC_UI_ITEM_CATEGORIES, 1, 0);
    tick(0);
    CHECK(pc_ui_shell_workshop_shown());
    // a greyed command buzzes, with its reason
    pc_ui_workshop& m = ws_storage;
    m.action_enabled[PC_UI_WSA_ASSEMBLE] = 0;
    m.action_reason[PC_UI_WSA_ASSEMBLE] = 51;
    pc_ui_workshop_set(&m);
    CHECK(json_has("{\"id\":\"assemble\",\"label\":\"Assemble\",\"enabled\":false,\"reason\":\"You cannot carry"));
    tick(PC_UI_CONFIRM);
    CHECK(ws_req(PC_UI_REQ_WS_SOUND, 2, 0, 0) && json_has("\"mode\":\"rows\""));
    m.action_enabled[PC_UI_WSA_ASSEMBLE] = 1;
    pc_ui_workshop_set(&m);
    // Assemble: blade, grip, gem, Next, the name (typed), Done
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"assemble\",\"flow\":\"assemble\"") && json_has("\"slots\":2,\"finish\":false"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"pick\"") && json_has("\"pick\":\"blade\""));
    CHECK(json_has("{\"entry\":0,\"list\":\"\",\"inventory\":-1,\"enabled\":false")); // the placeholder
    CHECK(json_has("{\"entry\":1,\"list\":\"blades\",\"inventory\":1,\"enabled\":true")); // the free blade only
    tick(PC_UI_DOWN);
    int key[6];
    CHECK(pc_ui_shell_workshop_wanted(key) && key[0] == PC_UI_WSA_ASSEMBLE && key[1] == 1 && key[2] == -1);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"assemble\"") && json_has("\"cursor\":1") && json_has("\"blade\":1,"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"pick\":\"grip\"") && json_has("{\"entry\":1,\"list\":\"grips\",\"inventory\":1,\"enabled\":true"));
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"slots\":4,\"finish\":true") && json_has("\"cursor\":2") && json_has("\"grip\":1,"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"pick\":\"gem\"") && json_has("{\"entry\":2,\"list\":\"gems\",\"inventory\":1"));
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"gems\":[0,-1,-1]") && json_has("\"cursor\":3"));
    CHECK(pc_ui_shell_workshop_wanted(key) && key[3] == 1); // gem record 0 + 1 in slot 0
    // the picked gem again: it comes out (the slot's own first in its list)
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("{\"entry\":0,\"list\":\"gems\",\"inventory\":0,\"enabled\":true,\"current\":true"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"gems\":[-1,-1,-1]") && json_has("\"cursor\":2"));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM); // gem 0 again
    tick(PC_UI_CONFIRM); // Next
    CHECK(json_has("\"mode\":\"name\"") && json_has("\"assembling\":true,\"text\":\"\""));
    CHECK(pc_ui_workshop_typing());
    pc_ui_workshop_text("A\xe2\x98\x83" "b"); // a snowman is not in the grid
    CHECK(json_has("\"cursor\":2,\"grid\":[0,0],\"assembling\":true,\"text\":\"Ab\""));
    pc_ui_workshop_key(PC_UI_WSK_LEFT);
    pc_ui_workshop_key(PC_UI_WSK_BACKSPACE);
    CHECK(json_has("\"cursor\":0,") && json_has("\"text\":\"b\""));
    tick(PC_UI_CONFIRM); // the grid's 'A' over the 'b' at the cursor (MENU8 overwrites), the cursor on
    CHECK(json_has("\"cursor\":1,\"grid\":[0,0],\"assembling\":true,\"text\":\"A\""));
    pc_ui_workshop_text("b"); // the keyboard inserts
    CHECK(json_has("\"cursor\":2,") && json_has("\"text\":\"Ab\""));
    tick(PC_UI_TAB_PREV); // L1: the cursor, not the tab
    tick(PC_UI_TAB_PREV);
    tick(PC_UI_CONFIRM); // 'A' over 'A'
    CHECK(json_has("\"tab\":\"items\"") && json_has("\"cursor\":1,") && json_has("\"text\":\"Ab\""));
    for (int i = 0; i < 25; ++i) // the last place: the cursor stays, each character overwrites it
        tick(PC_UI_TAB_NEXT);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"cursor\":19,") && json_has(("\"text\":\"Ab" + std::string(17, ' ') + "A\"").c_str()));
    pc_ui_workshop_key(PC_UI_WSK_DELETE); // the character at the cursor out
    CHECK(json_has("\"cursor\":19,") && json_has("\"text\":\"Ab\""));
    tick(PC_UI_UP); // past the grid's top: Delete, Done
    tick(PC_UI_RIGHT);
    CHECK(json_has("\"grid\":[9,1]"));
    tick(PC_UI_CONFIRM);
    {
        // the request carries the name (MENU8's buffer)
        pc_ui_request ar = pop();
        CHECK(ar.kind == PC_UI_REQ_WS_ASSEMBLE && ar.slot == 1 && ar.item == 1 && ar.shield == 1
              && ar.name[0] == 10 && ar.name[1] == 37 && ar.name[2] == 0x8F && ar.name[19] == 0x8F);
        CHECK(json_has("\"mode\":\"rows\""));
        // refused: back on the name, as typed
        pc_ui_workshop_refused();
        CHECK(json_has("\"mode\":\"name\"") && json_has("\"text\":\"Ab\"") && json_has("\"message\":\""));
        pc_ui_workshop_key(PC_UI_WSK_ENTER);
        ar = pop();
        CHECK(ar.kind == PC_UI_REQ_WS_ASSEMBLE && ar.name[0] == 10 && json_has("\"mode\":\"rows\""));
    }
    // Rename: the field opens on the weapon's name; Escape backs out to the list
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"target\",\"flow\":\"rename\""));
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"assembling\":false,\"text\":\"AB\""));
    pc_ui_workshop_key(PC_UI_WSK_ESCAPE);
    CHECK(json_has("\"mode\":\"target\"") && ws_req(PC_UI_REQ_WS_SOUND, 0, 0, 0));
    tick(PC_UI_CONFIRM);
    pc_ui_workshop_key(PC_UI_WSK_ENTER);
    CHECK(ws_req(PC_UI_REQ_WS_RENAME, 0, 2, -1) && json_has("\"mode\":\"target\""));
    tick(PC_UI_BACK);
    CHECK(ws_req(PC_UI_REQ_WS_SOUND, 0, 0, 0) && json_has("\"mode\":\"rows\""));
    // Disassemble: weapons, then the shield (no gems: greyed); Yes / No opens on No
    tick(PC_UI_UP);
    CHECK(json_has("\"row\":2"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("{\"entry\":2,\"list\":\"shields\",\"inventory\":0,\"enabled\":false") &&
          json_has("\"reason\":\"No gems attached to this shield.\""));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"confirm\"") && json_has("\"confirm\":{\"kind\":\"disassemble\",\"yes\":false"));
    tick(PC_UI_CONFIRM); // No
    CHECK(ws_req(PC_UI_REQ_WS_SOUND, 0, 0, 0) && json_has("\"mode\":\"target\""));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(ws_req(PC_UI_REQ_WS_DISASSEMBLE, 0, 0, 0) && json_has("\"mode\":\"target\""));
    tick(PC_UI_BACK);
    pop();
    // Combine: shields greyed (one); armor: a material not worked here buzzes
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"flow\":\"combine\"") && json_has("\"category\":1,\"count\":1,\"label\":\"Shields\",\"reason\":"
                                                        "\"Only one item"));
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"combine\"") && json_has("\"combine\":2"));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    CHECK(json_has("{\"entry\":2,\"list\":\"armor\",\"inventory\":7,\"enabled\":false"));
    tick(PC_UI_CONFIRM);
    CHECK(ws_req(PC_UI_REQ_WS_SOUND, 2, 0, 0) && json_has("\"mode\":\"pick\""));
    tick(PC_UI_BACK);
    pop();
    tick(PC_UI_BACK);
    pop();
    // blades: both picked, Combine asks on No
    tick(PC_UI_UP);
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"combine\":0"));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"first\":0,") && json_has("\"cursor\":1"));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"second\":1") && json_has("\"finish\":true") && json_has("\"cursor\":2"));
    CHECK(pc_ui_shell_workshop_wanted(key) && key[0] == PC_UI_WSA_COMBINE && key[1] == 0 && key[2] == 0 && key[3] == 1);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"confirm\":{\"kind\":\"combine\",\"yes\":false"));
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(ws_req(PC_UI_REQ_WS_COMBINE, 0, 0, 1) && json_has("\"mode\":\"rows\",\"flow\":\"\",\"row\":5"));
    // Repair opens on Yes; a refused request shows a line
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"confirm\":{\"kind\":\"repair\",\"yes\":true"));
    tick(PC_UI_CONFIRM);
    CHECK(ws_req(PC_UI_REQ_WS_REPAIR, 0, 0, 0) && json_has("\"mode\":\"rows\""));
    pc_ui_workshop_refused(); // back on the Yes / No that sent it
    CHECK(json_has("\"message\":\"") && json_has("\"confirm\":{\"kind\":\"repair\",\"yes\":true"));
    tick(PC_UI_BACK);
    CHECK(ws_req(PC_UI_REQ_WS_SOUND, 0, 0, 0) && json_has("\"mode\":\"rows\""));
    // a click on a command while a flow is open: back on it, no confirm
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"target\",\"flow\":\"rename\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_WS_ROW, PC_UI_WSA_REPAIR, 1, 0);
    tick(0);
    CHECK(json_has("\"mode\":\"rows\",\"flow\":\"\",\"row\":4"));
    pc_ui_shell_mouse(PC_UI_MOUSE_WS_ROW, PC_UI_WSA_REPAIR, 1, 0);
    tick(0);
    CHECK(json_has("\"confirm\":{\"kind\":\"repair\""));
    // Cross: back, then the shell closes from the commands
    tick(PC_UI_BACK);
    pop();
    // Gems on the equipped Fandango (2 slots): Attach with no pick changes nothing (flags 0)
    while (!json_has("\"mode\":\"rows\",\"flow\":\"\",\"row\":1"))
        tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"target\",\"flow\":\"gems\""));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"slots\"") && json_has("\"slots\":3"));
    tick(PC_UI_UP); // Attach
    tick(PC_UI_CONFIRM);
    {
        pc_ui_request g = pop();
        CHECK(g.kind == PC_UI_REQ_WS_GEMS && g.slot == 0 && g.item == 0 && g.shield == 0 && g.count == 0
              && g.flags == 0 && json_has("\"mode\":\"target\""));
    }
    // a pick (and out again: the same gems) still counts as an edit: the original attached it live
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM); // slot 0: its pick list
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM); // Feldspar in
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM); // the slot's own: out again
    CHECK(json_has("\"gems\":[-1,-1,-1]"));
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM); // Attach
    {
        pc_ui_request g = pop();
        CHECK(g.kind == PC_UI_REQ_WS_GEMS && g.shield == 0 && g.flags == PC_UI_WS_EDITED);
    }
    // Cross out of the slots: the leave sound and GEMS_LEAVE (the original's _attachGems(targetGems))
    tick(PC_UI_CONFIRM);
    tick(PC_UI_BACK);
    CHECK(ws_req(PC_UI_REQ_WS_GEMS_LEAVE, 0, 0, 0, 0) && ws_req(PC_UI_REQ_WS_SOUND, 0, 0, 0)
          && json_has("\"mode\":\"target\""));
    // ... from a gem pick list over them, a click on a command: the same
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"pick\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_WS_ROW, PC_UI_WSA_REPAIR, 1, 0);
    tick(0);
    CHECK(ws_req(PC_UI_REQ_WS_GEMS_LEAVE, 0, 0, 0, 0) && json_has("\"mode\":\"rows\""));
    // ... Triangle closing the menu from the slots: the same, before the shell hides
    while (!json_has("\"mode\":\"rows\",\"flow\":\"\",\"row\":1"))
        tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"slots\""));
    CHECK(tick(PC_UI_EXIT) == -1 && !pc_ui_shell_visible());
    CHECK(ws_req(PC_UI_REQ_WS_GEMS_LEAVE, 0, 0, 0, 0) && pop().kind == PC_UI_REQ_NONE);
    pc_ui_shell_close();
    while (pop().kind != PC_UI_REQ_NONE) {
    }
}

// The Map frame's pure state (ui_map.cpp): the mailbox MENU5 empties at its
// input point, the pointer's requests, the JSON block, the shell's set_tab.
// The vsync counter the frame's freshness reads (boot_runtime.c in the game).
static unsigned probe_vsync;
extern "C" unsigned pc_vsync_count(void) { return probe_vsync; }
static bool map_json_has(const char* needle)
{
    static char buf[1 << 15];
    pc_ui_map_json(buf, sizeof buf);
    return std::strstr(buf, needle) != nullptr;
}
// The native Data screen's pure state (ui_data.cpp): the goal mailbox by
// phase (busy drops, refusals), the pointer's goals and soft highlight,
// visibility by the stamp, MENU7's cursor decoded, the JSON block (files,
// chips, phase, message).
static bool data_json_has(const char* needle)
{
    static char buf[1 << 14];
    pc_ui_data_json(buf, sizeof buf);
    return std::strstr(buf, needle) != nullptr;
}
// The native Container screen's pure state (ui_container.cpp): visibility, the
// pointer's requests and goals by phase (busy drops, prompts as goals, a
// click as FOCUS + Circle), hover writing nothing, the JSON block, Data's
// container mode refusing tabs.
static bool container_json_has(const char* needle)
{
    static char buf[1 << 16];
    pc_ui_container_json(buf, sizeof buf);
    return std::strstr(buf, needle) != nullptr;
}
static void container_tests()
{
    pc_ui_ctr_model* c = pc_ui_ctr_state();
    pc_ui_ctr_req r;
    std::memset(c, 0, sizeof *c);
    pc_ui_ctr_reset_mailbox();
    CHECK(container_json_has(",\"container\":null") && !pc_ui_container_visible());
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_YES, 0, 1, 0, 0); // nothing latched: nothing
    CHECK(!pc_ui_ctr_pop(&r));
    probe_vsync = 500;
    c->latched = 1, c->run = 3, c->stamp = 500, c->phase = PC_UI_CTR_PH_BUSY;
    CHECK(pc_ui_container_visible() && container_json_has("\"phase\":\"busy\""));
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_YES, 0, 1, 0, 0); // busy: dropped and counted
    CHECK(!pc_ui_ctr_pop(&r) && c->busy_drops == 1);
    // a prompt: Yes / No / a right click are goals for MENU7 (never a press of their own)
    c->phase = PC_UI_CTR_PH_PROMPT_CHECK;
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_YES, 0, 1, 0, 0);
    CHECK(pc_ui_ctr_pop(&r) && r.kind == PC_UI_CTR_REQ_GOAL && r.a == PC_UI_CTR_G_YES);
    pc_ui_ctr_mouse(PC_UI_MOUSE_NONE, 0, 0, 1, 0);
    CHECK(pc_ui_ctr_pop(&r) && r.kind == PC_UI_CTR_REQ_GOAL && r.a == PC_UI_CTR_G_NO);
    // hover: only the soft highlight
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_NO, 0, 0, 0, 0);
    int hk, hi;
    pc_ui_ctr_hover(&hk, &hi);
    CHECK(hk == PC_UI_MOUSE_CTR_NO && !pc_ui_ctr_pop(&r));
    c->phase = PC_UI_CTR_PH_WAIT; // the card wait: any click dismisses
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_PANEL, 0, 1, 0, 0);
    CHECK(pc_ui_ctr_pop(&r) && r.a == PC_UI_CTR_G_DISMISS);
    // the transfer: a row click is the cursor there, then Circle; a tab click walks the tabs
    c->phase = PC_UI_CTR_PH_TRANSFER, c->session = 1, c->mode = PC_UI_CTR_LIST, c->list_active = 1;
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_ROW, 4, 1, 0, 0);
    CHECK(pc_ui_ctr_pop(&r) && r.kind == PC_UI_CTR_REQ_FOCUS && r.a == PC_UI_CTR_F_ROW && r.b == 4);
    CHECK(pc_ui_ctr_pop(&r) && r.kind == PC_UI_CTR_REQ_PRESS && r.a == PC_UI_CONFIRM);
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_TAB, 6, 1, 0, 0);
    CHECK(pc_ui_ctr_pop(&r) && r.kind == PC_UI_CTR_REQ_TAB && r.a == 6);
    pc_ui_ctr_mouse(PC_UI_MOUSE_NONE, 0, 0, 0, -2); // the wheel: Down a notch at a time
    CHECK(pc_ui_ctr_pop(&r) && r.a == PC_UI_DOWN && pc_ui_ctr_pop(&r) && r.a == PC_UI_DOWN && !pc_ui_ctr_pop(&r));
    pc_ui_ctr_mouse(PC_UI_MOUSE_NONE, 0, 0, 1, 0); // right: Cross
    CHECK(pc_ui_ctr_pop(&r) && r.kind == PC_UI_CTR_REQ_PRESS && r.a == PC_UI_BACK);
    c->mode = PC_UI_CTR_ACTIONS; // a menu entry: the cursor there, then Circle
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_MENU, 2, 1, 0, 0);
    CHECK(pc_ui_ctr_pop(&r) && r.a == PC_UI_CTR_F_MENU && r.b == 2 && pc_ui_ctr_pop(&r) && r.a == PC_UI_CONFIRM);
    c->mode = PC_UI_CTR_DISCARD_QTY;
    pc_ui_ctr_mouse(PC_UI_MOUSE_CTR_QTY, 2, 1, 0, 0);
    CHECK(pc_ui_ctr_pop(&r) && r.a == PC_UI_LEFT);
    CHECK(container_json_has("\"mode\":\"discard_qty\"") && container_json_has("\"phase\":\"transfer\""));
    // the save list is the Data screen's: the container's own screen hides
    c->phase = PC_UI_CTR_PH_SAVE;
    CHECK(!pc_ui_container_visible());
    probe_vsync = 500 + 121;
    c->phase = PC_UI_CTR_PH_BUSY;
    CHECK(!pc_ui_container_visible()); // a stale stamp
    // Data's container mode: no tabs, no close
    pc_ui_data_model* d = pc_ui_data_state();
    std::memset(d, 0, sizeof *d);
    pc_ui_data_reset_mailbox();
    d->framed = 1, d->container = 1, d->published = 1, d->stamped = 1, d->stamp = probe_vsync;
    d->phase = PC_UI_DATA_PH_CARD;
    pc_ui_data_post(PC_UI_DATA_GOAL_TAB, 2);
    pc_ui_data_post(PC_UI_DATA_GOAL_CLOSE, 0);
    CHECK(pc_ui_data_goal_slot()->kind == PC_UI_DATA_GOAL_NONE && d->rejected == 2);
    pc_ui_data_post(PC_UI_DATA_GOAL_CARD, 1);
    CHECK(pc_ui_data_goal_slot()->kind == PC_UI_DATA_GOAL_CARD);
    std::memset(d, 0, sizeof *d);
    pc_ui_data_reset_mailbox();
    std::memset(c, 0, sizeof *c);
    pc_ui_ctr_reset_mailbox();
}

static void data_tests()
{
    pc_ui_data_model* d = pc_ui_data_state();
    pc_ui_data_goal* g = pc_ui_data_goal_slot();
    std::memset(d, 0, sizeof *d);
    pc_ui_data_reset_mailbox();
    CHECK(data_json_has(",\"data\":null"));
    CHECK(!pc_ui_data_visible());
    pc_ui_data_post(PC_UI_DATA_GOAL_OP, 1); // nothing framed: nothing
    CHECK(g->kind == PC_UI_DATA_GOAL_NONE);
    // armed: visible for 30 vsyncs without a stamp, busy (posts dropped)
    probe_vsync = 100;
    d->armed = 1, d->armed_vsync = 100;
    CHECK(pc_ui_data_visible());
    CHECK(data_json_has("\"phase\":\"busy\""));
    pc_ui_data_post(PC_UI_DATA_GOAL_OP, 1);
    CHECK(g->kind == 0 && d->busy_drops == 1);
    probe_vsync = 131;
    CHECK(!pc_ui_data_visible());
    // framed, stamped: visible while the stamp is at most 8 vsyncs old
    d->armed = 0, d->framed = 1, d->run = 3, d->stamped = 1, d->stamp = 131, d->published = 1;
    d->state = 6;
    d->phase = pc_ui_data_phase(6, 0, 0, 0, 0, 0);
    CHECK(d->phase == PC_UI_DATA_PH_OPS);
    CHECK(pc_ui_data_visible());
    probe_vsync = 140;
    CHECK(!pc_ui_data_visible());
    probe_vsync = 135;
    // the chooser: OP, BACK, CLOSE, TAB taken; a file refused
    pc_ui_data_post(PC_UI_DATA_GOAL_FILE, 2);
    CHECK(g->kind == 0 && d->rejected == 1);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_OP, 2, 1, 0, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_OP && g->a == 2);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_OP, 1, 1, 0, 0); // a new post replaces it
    CHECK(g->kind == PC_UI_DATA_GOAL_OP && g->a == 1);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_TAB, 4, 1, 0, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_TAB && g->a == 4);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_BUTTON, PC_UI_EXIT, 1, 0, 0); // the Close hint
    CHECK(g->kind == PC_UI_DATA_GOAL_CLOSE);
    pc_ui_data_mouse(PC_UI_MOUSE_NONE, 0, 0, 0, 1); // a right click: back
    CHECK(g->kind == PC_UI_DATA_GOAL_BACK);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_PANEL, 0, 1, 0, 0); // the panel itself: nothing
    CHECK(g->kind == PC_UI_DATA_GOAL_BACK);
    // hover is visual only
    pc_ui_data_reset_mailbox();
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_OP, 2, 0, 0, 0);
    int hk, hi;
    pc_ui_data_hover(&hk, &hi);
    CHECK(hk == PC_UI_MOUSE_DATA_OP && hi == 2 && g->kind == 0);
    // the file list: MENU7's cursor (24, slot * 40 + 62) on page 1 is file 2
    d->state = 8;
    d->cursor_xy = (102 << 16) | 24;
    pc_ui_data_decode(d->cursor_xy, 72 - 40, &d->card, &d->file, &d->cancelled);
    CHECK(d->file == 2 && d->card == -1 && d->cancelled == -1);
    d->phase = pc_ui_data_phase(8, 1, 0, 0, d->cursor_xy, PC_UI_DATA_MSG_SELECT_FILE);
    CHECK(d->phase == PC_UI_DATA_PH_FILES);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_FILE, 4, 1, 0, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_FILE && g->a == 4);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_YES, 0, 1, 0, 0); // no prompt: refused, the file goal kept
    CHECK(g->kind == PC_UI_DATA_GOAL_FILE && d->rejected == 2);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_BUTTON, PC_UI_CONFIRM, 1, 0, 0); // the Confirm hint: MENU7's own file
    CHECK(g->kind == PC_UI_DATA_GOAL_FILE && g->a == 2);
    // the wheel walks MENU7's own cursor (MOVE: nothing confirmed; notches add up, clamped), no soft highlight
    d->files_valid = 1;
    pc_ui_data_reset_mailbox();
    pc_ui_data_mouse(PC_UI_MOUSE_NONE, 0, 0, -1, 0);
    pc_ui_data_hover(&hk, &hi);
    CHECK(g->kind == PC_UI_DATA_GOAL_MOVE && g->a == 3 && hk == 0);
    for (int i = 0; i < 4; ++i)
        pc_ui_data_mouse(PC_UI_MOUSE_NONE, 0, 0, -1, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_MOVE && g->a == 4);
    pc_ui_data_mouse(PC_UI_MOUSE_NONE, 0, 0, 1, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_MOVE && g->a == 3);
    CHECK(data_json_has("\"goal\":{\"kind\":\"move\",\"a\":3"));
    pc_ui_data_reset_mailbox();
    d->cursor_xy = (62 << 16) | 24; // file 0 on page 0: the wheel up has nowhere to go
    pc_ui_data_decode(d->cursor_xy, 72, &d->card, &d->file, &d->cancelled);
    pc_ui_data_mouse(PC_UI_MOUSE_NONE, 0, 0, 1, 0);
    CHECK(d->file == 0 && g->kind == 0);
    d->phase = PC_UI_DATA_PH_CARD; // the wheel elsewhere: nothing
    pc_ui_data_mouse(PC_UI_MOUSE_NONE, 0, 0, -1, 0);
    CHECK(g->kind == 0);
    d->phase = PC_UI_DATA_PH_FILES;
    // a prompt: the dialog's focus is MENU7's cancelled; Yes / No are answers
    d->cursor_xy = 26 << 16;
    pc_ui_data_decode(d->cursor_xy, 72, &d->card, &d->file, &d->cancelled);
    d->msg = PC_UI_DATA_MSG_OVERWRITE_PROMPT;
    d->phase = pc_ui_data_phase(8, 1, 0, 0, d->cursor_xy, d->msg);
    d->prompt = pc_ui_data_msg_prompt(d->msg);
    CHECK(d->phase == PC_UI_DATA_PH_PROMPT && d->cancelled == 1 && d->prompt == PC_UI_DATA_PROMPT_OVERWRITE);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_YES, 0, 1, 0, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_ANSWER && g->a == 1);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_NO, 0, 1, 0, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_ANSWER && g->a == 0);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_TAB, 2, 1, 0, 0); // a tab at a prompt: refused (never answered for the player)
    CHECK(g->kind == PC_UI_DATA_GOAL_ANSWER);
    // busy (saving): every post dropped and counted
    unsigned drops = d->busy_drops;
    d->progress_counter = 12, d->cursor_xy = 0;
    d->phase = pc_ui_data_phase(8, 1, 12, 0, 0, PC_UI_DATA_MSG_SAVING);
    CHECK(d->phase == PC_UI_DATA_PH_BUSY);
    pc_ui_data_reset_mailbox();
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_FILE, 1, 1, 0, 0);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_FILE, 1, 1, 0, 0);
    CHECK(g->kind == 0 && d->busy_drops == drops + 2);
    // a message waiting: Continue (the status bar) dismisses
    d->progress_counter = 0, d->msg = PC_UI_DATA_MSG_SAVED;
    d->phase = pc_ui_data_phase(8, 1, 0, 0, 0, d->msg);
    CHECK(d->phase == PC_UI_DATA_PH_WAIT);
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_STATUS, 0, 1, 0, 0);
    CHECK(g->kind == PC_UI_DATA_GOAL_DISMISS);
    // the files and their chips in the JSON
    std::memset(d->files, 0, sizeof d->files);
    d->files[0].state = 0x17385CA9, d->files[0].generation = 5, d->files[0].current = 1;
    d->files[0].h = 1, d->files[0].m = 2, d->files[0].s = 3, d->files[0].saves = 5, d->files[0].map = 3;
    std::snprintf(d->files[0].name, sizeof d->files[0].name, "%s", "Blackmarket");
    d->files[1].state = 1;
    d->files[2].state = 2;
    d->files[3].state = 0x17385CA9, d->files[3].generation = 7, d->files[3].newest = 1;
    std::snprintf(d->msg_text, sizeof d->msg_text, "%s", "Saved \"here\".");
    CHECK(data_json_has("\"phase\":\"wait\""));
    CHECK(data_json_has("\"msg\":\"saved\""));
    CHECK(data_json_has("\"msg_text\":\"Saved \\\"here\\\".\""));
    CHECK(data_json_has("{\"i\":0,\"state\":389569705,\"generation\":5,\"time\":\"1:02:03\",\"saves\":5"));
    CHECK(data_json_has("\"current\":true,\"newest\":false,\"name\":\"Blackmarket\"}"));
    CHECK(data_json_has("{\"i\":3,\"state\":389569705,\"generation\":7"));
    CHECK(data_json_has("\"goal\":{\"kind\":\"dismiss\""));
    CHECK(data_json_has("\"cursor\":[0,0]"));
    // finished: hidden, nothing taken
    d->finished = 1;
    CHECK(!pc_ui_data_visible());
    pc_ui_data_reset_mailbox();
    pc_ui_data_mouse(PC_UI_MOUSE_DATA_STATUS, 0, 1, 0, 0);
    CHECK(g->kind == 0);
    std::memset(d, 0, sizeof *d);
    pc_ui_data_reset_mailbox();
    CHECK(data_json_has(",\"data\":null"));
    probe_vsync = 0;
}

static void map_tests()
{
    pc_ui_map_model* m = pc_ui_map_state();
    pc_ui_map_req r;
    std::memset(m, 0, sizeof *m);
    CHECK(map_json_has(",\"map\":null"));
    CHECK(!pc_ui_map_visible());
    pc_ui_map_post(PC_UI_MAP_REQ_RESET, 0, 0); // nothing framed: dropped
    CHECK(!pc_ui_map_take(&r, 1));
    CHECK(map_json_has("\"busy\":1") == false); // no block to show it in
    // armed: the "loading" block
    m->armed = 1;
    CHECK(map_json_has("\"state\":\"loading\""));
    CHECK(pc_ui_map_visible()); // armed, no stamp yet (no vsync counter here: 0 vsyncs ago)
    // a framed run at processInput
    m->armed = 0, m->framed = 1, m->run = 3, m->stamped = 1, m->published = 1, m->exec_state = 7, m->menu_state = 1;
    CHECK(map_json_has("\"state\":\"ready\""));
    CHECK(map_json_has("\"focus\":\"map\""));
    CHECK(pc_ui_map_visible());
    // freshness: chrome only while MENU5 stamped at most 8 vsyncs ago (a stalled
    // run never leaves chrome over the world); armed: 30 vsyncs before a stamp
    m->stamp = 100, probe_vsync = 108;
    CHECK(pc_ui_map_visible());
    probe_vsync = 109;
    CHECK(!pc_ui_map_visible());
    m->framed = 0, m->armed = 1, m->armed_vsync = 100, probe_vsync = 130;
    CHECK(pc_ui_map_visible());
    probe_vsync = 131;
    CHECK(!pc_ui_map_visible());
    m->framed = 1, m->armed = 0, m->stamp = probe_vsync = 0;
    pc_ui_map_post(PC_UI_MAP_REQ_AREA, 4, 0);
    pc_ui_map_post(PC_UI_MAP_REQ_RESET, 0, 0); // one discrete request at a time: busy
    CHECK(map_json_has("\"busy\":2,")); // the request before any run, and this one
    CHECK(pc_ui_map_take(&r, 1) && r.kind == PC_UI_MAP_REQ_AREA && r.a == 4);
    CHECK(!pc_ui_map_take(&r, 1));
    // rotate / zoom / pan add up; one kind per take
    pc_ui_map_post(PC_UI_MAP_REQ_ORBIT, 16, -8);
    pc_ui_map_post(PC_UI_MAP_REQ_ORBIT, 8, 0);
    pc_ui_map_post(PC_UI_MAP_REQ_ZOOM, -64, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ORBIT && r.a == 24 && r.b == -8);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ZOOM && r.a == -64);
    CHECK(!pc_ui_map_take(&r, 0));
    CHECK(map_json_has("\"req_seq\":4,\"applied_seq\":4"));
    // the pointer: a left drag rotates 8 a layout unit, the wheel zooms 64 a
    // notch (up: closer), a middle drag pans a game pixel per 3 units, handed
    // to MENU5 in its own steps of 4 (the rest kept)
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, 0, 2, -1, 1, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ORBIT && r.a == 16 && r.b == -8);
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, 1, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ZOOM && r.a == -64);
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, 0, 12, 21, 2, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_PAN && r.a == 4 && r.b == 4);
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, 0, -3, 3, 2, 0); // what was left over adds up
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_PAN && r.a == 0 && r.b == 4);
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, 0, -6, 0, 2, 0); // -2 kept: less than a step
    CHECK(!pc_ui_map_take(&r, 0));
    pc_ui_map_drop(); // a remainder under a step is not a dropped request
    CHECK(map_json_has("\"busy\":2,"));
    // the shown area's row: nothing; another area's: a switch
    m->scene = 6;
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_AREA, 6, 1, 0, 0, 0, 0, 0);
    CHECK(!pc_ui_map_take(&r, 1));
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_AREA, 7, 1, 0, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 1) && r.kind == PC_UI_MAP_REQ_AREA && r.a == 7);
    // the Zoom prompt: in until the near clamp, then out until the far one
    m->distance = 768;
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_PROMPT, PC_UI_MAP_P_ZOOM, 1, 0, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ZOOM && r.a == -64);
    m->distance = 512;
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_PROMPT, PC_UI_MAP_P_ZOOM, 1, 0, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ZOOM && r.a == 64);
    m->distance = 768;
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_PROMPT, PC_UI_MAP_P_ZOOM, 1, 0, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ZOOM && r.a == 64);
    m->distance = 1024;
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_PROMPT, PC_UI_MAP_P_ZOOM, 1, 0, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 0) && r.kind == PC_UI_MAP_REQ_ZOOM && r.a == -64);
    // framed: the bridge's frame of that run (MENU5's latch alone is not)
    CHECK(pc_ui_map_framed(3) && !pc_ui_map_framed(2));
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_ROOM, 5, 1, 0, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 1) && r.kind == PC_UI_MAP_REQ_ROOM && r.a == 5);
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_PROMPT, PC_UI_MAP_P_CLOSE, 1, 0, 0, 0, 0, 0);
    CHECK(pc_ui_map_take(&r, 1) && r.kind == PC_UI_MAP_REQ_QUIT && r.a == 2);
    pc_ui_map_mouse(PC_UI_MOUSE_NONE, 0, 0, 0, 0, 0, 0, 1); // right click: back to the shell
    CHECK(pc_ui_map_take(&r, 1) && r.kind == PC_UI_MAP_REQ_QUIT && r.a == 1);
    // a paled area takes no camera request
    m->paled = 1;
    pc_ui_map_mouse(PC_UI_MOUSE_MAP_VIEW, 0, 0, 0, 2, 2, 1, 0);
    CHECK(!pc_ui_map_take(&r, 0));
    m->paled = 0;
    // not at processInput (an area switching): dropped
    m->menu_state = 4;
    CHECK(map_json_has("\"state\":\"switching\""));
    pc_ui_map_post(PC_UI_MAP_REQ_STEP, 1, 0);
    CHECK(!pc_ui_map_take(&r, 1));
    // the list: its own requests (pad bits, an area, a tab); Right click = its Cross
    m->menu_state = 2, m->list.open = 1, m->list.count = 2;
    CHECK(map_json_has("\"state\":\"list\""));
    CHECK(map_json_has("\"focus\":\"list\""));
    pc_ui_map_mouse(PC_UI_MOUSE_NONE, 0, 0, 0, 0, 0, 0, 1);
    CHECK(pc_ui_map_take(&r, 1) && r.kind == PC_UI_MAP_REQ_PAD && r.a == (int)PC_UI_BACK);
    pc_ui_map_post(PC_UI_MAP_REQ_RESET, 0, 0); // the map's own: not while the list has the focus
    CHECK(!pc_ui_map_take(&r, 1));
    // a request of an earlier run is never applied
    m->menu_state = 1, m->list.open = 0;
    pc_ui_map_post(PC_UI_MAP_REQ_HIDE, 0, 0);
    m->run = 4;
    CHECK(!pc_ui_map_take(&r, 1));
    // closing
    m->finished = 1;
    CHECK(map_json_has("\"state\":\"closing\""));
    std::memset(m, 0, sizeof *m);
    pc_ui_map_reset_mailbox();
    CHECK(map_json_has(",\"map\":null"));
    // the shell reopens on a tab (a tab clicked on the frame): that tab, its remembered row
    for (int i = 0; i < pc_ui_shell_tab_count(); ++i) {
        pc_ui_shell_set_tab(i);
        CHECK(pc_ui_shell_tab() == i);
    }
    int keep = pc_ui_shell_tab();
    pc_ui_shell_set_tab(-1);
    pc_ui_shell_set_tab(99);
    CHECK(pc_ui_shell_tab() == keep);
    CHECK(pc_ui_shell_step_tab(1) == (keep + 1) % pc_ui_shell_tab_count());
}

static void loot_tests()
{
    pc_ui_loot_open();
    CHECK(pc_ui_loot_visible() && pc_ui_loot_focus() == -1);
    pc_ui_loot lm = loot_model(1, 0, 0);
    pc_ui_loot_set(&lm);
    CHECK(loot_has(",\"loot\":{\"valid\":true,\"chest\":false,\"mode\":\"list\",\"cursor\":0,\"rows\":3"));
    CHECK(pc_ui_loot_focus() == 0);
    // Up wraps to the last row, Down back
    loot_tick(PC_UI_UP);
    CHECK(pc_ui_loot_focus() == 2);
    loot_tick(PC_UI_DOWN);
    CHECK(pc_ui_loot_focus() == 0);
    // Circle on a row with no room: Organize (the select sound), its own action first
    loot_tick(PC_UI_CONFIRM);
    CHECK(loot_req(PC_UI_REQ_LOOT_SOUND, 1, 0, 0) && loot_empty_queue());
    CHECK(loot_has("\"mode\":\"organize\"") && loot_has("\"menu\":0,\"entries\":[\"disassemble\",\"organize\"]"));
    // Disassemble asks on Yes; Yes sends it for the row and its record
    loot_tick(PC_UI_CONFIRM);
    CHECK(loot_req(PC_UI_REQ_LOOT_SOUND, 1, 0, 0));
    CHECK(loot_has("\"mode\":\"confirm\"") && loot_has("\"ask\":\"disassemble\",\"yes\":true,\"text\":\"Disassemble Tovarisch?\""));
    loot_tick(PC_UI_CONFIRM);
    CHECK(loot_req(PC_UI_REQ_LOOT_DISASSEMBLE, PC_UI_IC_WEAPON, 0, 0) && loot_empty_queue());
    CHECK(loot_has("\"mode\":\"list\""));
    // Organize inventory: the bag panel (BAG 1 for the row's category); Cross closes it (BAG 0)
    loot_tick(PC_UI_CONFIRM);
    loot_pop();
    loot_tick(PC_UI_DOWN);
    loot_tick(PC_UI_CONFIRM);
    CHECK(loot_req(PC_UI_REQ_LOOT_SOUND, 1, 0, 0) && loot_req(PC_UI_REQ_LOOT_BAG, 1, PC_UI_IC_WEAPON, -1));
    CHECK(loot_has("\"mode\":\"bag\"") && loot_has("\"bag_panel\":{\"category\":\"weapons\",\"cursor\":0,\"rows\":0"));
    {
        static pc_ui_items bag;
        bag = pc_ui_items();
        bag.valid = 1;
        bag.count[PC_UI_IC_MISC] = 2;
        bag.first[PC_UI_IC_MISC] = 0;
        std::snprintf(bag.rows[0].name, sizeof bag.rows[0].name, "Cure Root");
        bag.rows[0].inventory = 4, bag.rows[0].count = 12, bag.rows[0].discard = 1, bag.rows[0].discard_all = 1;
        std::snprintf(bag.rows[1].name, sizeof bag.rows[1].name, "Silver Key");
        bag.rows[1].inventory = 7, bag.rows[1].count = 1;
        std::snprintf(bag.discard_prompt[2], sizeof bag.discard_prompt[2], "Discard all?");
        std::snprintf(bag.discard_prompt[1], sizeof bag.discard_prompt[1], "How many?");
        pc_ui_loot_set_bag(&bag);
        loot_tick(PC_UI_SUB_PREV); // weapons -> misc: the game's current category follows
        CHECK(loot_req(PC_UI_REQ_LOOT_BAG, 1, PC_UI_IC_MISC, -1) && loot_empty_queue());
        CHECK(loot_has("\"bag_panel\":{\"category\":\"misc\",\"cursor\":0,\"rows\":2"));
        // Circle: the row's actions (MENUB's Items menu: Discard, Discard all, Sort; Move)
        loot_tick(PC_UI_DOWN);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_has("\"mode\":\"bag_actions\"") && loot_has("\"actions\":[{\"id\":\"discard\",\"enabled\":false},"
                                                                 "{\"id\":\"discard_all\",\"enabled\":false},"
                                                                 "{\"id\":\"sort\",\"enabled\":false},"
                                                                 "{\"id\":\"move\",\"enabled\":true}]"));
        // a key: Discard not selectable (sent: the bridge buzzes), the menu stays
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_req(PC_UI_REQ_ITEM_DISCARD, PC_UI_IC_MISC, 1, 7, 0) && loot_has("\"mode\":\"bag_actions\""));
        loot_tick(PC_UI_BACK);
        CHECK(loot_empty_queue() && loot_has("\"mode\":\"bag\""));
        // a stack: Discard all asks on No; No cancels (what 2), Yes discards it
        loot_tick(PC_UI_UP);
        loot_tick(PC_UI_CONFIRM);
        loot_tick(PC_UI_DOWN);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_has("\"mode\":\"bag_confirm\"") && loot_has("\"ask\":\"discard_all\",\"yes\":false,\"text\":\"Discard all?\""));
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_req(PC_UI_REQ_ITEM_CANCEL, PC_UI_IC_MISC, 0, 4, 2) && loot_has("\"mode\":\"bag\""));
        loot_tick(PC_UI_CONFIRM);
        loot_tick(PC_UI_DOWN);
        loot_tick(PC_UI_CONFIRM);
        loot_tick(PC_UI_UP);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_req(PC_UI_REQ_ITEM_DISCARD_ALL, PC_UI_IC_MISC, 0, 4) && loot_has("\"mode\":\"bag\""));
        // Discard on a stack: the quantity picker (Up by the step, Left a bigger step, past the stack refused)
        loot_tick(PC_UI_CONFIRM);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_has("\"mode\":\"bag_quantity\"") && loot_has("\"count\":0,\"count_max\":12,\"step\":0"));
        loot_tick(PC_UI_UP);
        loot_tick(PC_UI_UP);
        loot_tick(PC_UI_UP);
        loot_tick(PC_UI_LEFT);
        loot_tick(PC_UI_UP);
        CHECK(loot_has("\"count\":3,\"count_max\":12,\"step\":1"));
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_has("\"mode\":\"bag_confirm\"") && loot_has("\"ask\":\"discard\",\"yes\":false"));
        loot_tick(PC_UI_UP);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_req(PC_UI_REQ_ITEM_DISCARD, PC_UI_IC_MISC, 0, 4, 3) && loot_has("\"mode\":\"bag\""));
        // none: the picker left (ITEM_CANCEL what 0)
        loot_tick(PC_UI_CONFIRM);
        loot_tick(PC_UI_CONFIRM);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_req(PC_UI_REQ_ITEM_CANCEL, PC_UI_IC_MISC, 0, 4, 0) && loot_has("\"mode\":\"bag\""));
        // Move: the row, then its target (the moved row's record checked)
        loot_tick(PC_UI_CONFIRM);
        loot_tick(PC_UI_UP);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_has("\"mode\":\"bag_move\"") && loot_has("\"move_from\":0"));
        loot_tick(PC_UI_DOWN);
        loot_tick(PC_UI_CONFIRM);
        CHECK(loot_req(PC_UI_REQ_ITEM_MOVE, PC_UI_IC_MISC, 0, 1, 4) && loot_has("\"mode\":\"bag\""));
        loot_tick(PC_UI_UP);
    }
    loot_tick(PC_UI_BACK);
    CHECK(loot_req(PC_UI_REQ_LOOT_BAG, 0, PC_UI_IC_MISC, -1) && loot_has("\"mode\":\"list\""));
    // Circle on a row that fits takes it; Square takes all
    loot_tick(PC_UI_DOWN);
    loot_tick(PC_UI_CONFIRM);
    CHECK(loot_req(PC_UI_REQ_LOOT_TAKE, PC_UI_IC_MISC, 1, 0));
    loot_tick(PC_UI_ALT);
    CHECK(loot_req(PC_UI_REQ_LOOT_TAKE_ALL, 0, 0, -1) && loot_empty_queue());
    // Cross with a key in the list: Leave pressed, then the message; any button back (the leave sound)
    loot_tick(PC_UI_BACK);
    CHECK(loot_req(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 0));
    CHECK(loot_has("\"mode\":\"message\"") && loot_has("\"ask\":\"keys\""));
    loot_tick(PC_UI_ALT);
    CHECK(loot_req(PC_UI_REQ_LOOT_SOUND, 0, 0, 0) && loot_has("\"mode\":\"list\""));
    // no key, something taken: Leave asks (itemsRemaining) on No; Yes leaves
    lm = loot_model(0, 1, 0);
    pc_ui_loot_set(&lm);
    loot_tick(PC_UI_BACK);
    CHECK(loot_req(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 0));
    CHECK(loot_has("\"ask\":\"leave\",\"yes\":false,\"text\":\"Items remain. Leave them?\""));
    loot_tick(PC_UI_BACK); // No (Cross): back
    CHECK(loot_req(PC_UI_REQ_LOOT_SOUND, 0, 0, 0) && loot_has("\"mode\":\"list\""));
    loot_tick(PC_UI_BACK);
    loot_pop();
    loot_tick(PC_UI_UP);
    loot_tick(PC_UI_CONFIRM);
    CHECK(loot_req(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 1));
    // nothing taken: discardAllConfirm; a chest untouched: no question (the bridge ends it)
    lm = loot_model(0, 0, 0);
    pc_ui_loot_set(&lm);
    loot_tick(PC_UI_BACK);
    loot_pop();
    CHECK(loot_has("\"text\":\"Discard all items?\""));
    loot_tick(PC_UI_BACK);
    loot_pop();
    lm = loot_model(0, 0, 1);
    pc_ui_loot_set(&lm);
    loot_tick(PC_UI_BACK);
    CHECK(loot_req(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 0) && loot_has("\"mode\":\"list\""));
    // an empty list: the game's line, any button leaves
    lm = pc_ui_loot();
    lm.valid = 1;
    std::snprintf(lm.empty, sizeof lm.empty, "Nothing in the chest.");
    pc_ui_loot_set(&lm);
    loot_tick(0);
    CHECK(loot_has("\"mode\":\"message\"") && loot_has("\"ask\":\"empty\""));
    loot_tick(PC_UI_EXIT);
    CHECK(loot_req(PC_UI_REQ_LOOT_LEAVE, 0, 0, 0, 0));
    // the mouse: a row click focuses and takes; a click elsewhere on a message is any button
    lm = loot_model(0, 0, 0);
    pc_ui_loot_open();
    pc_ui_loot_set(&lm);
    pc_ui_loot_mouse(PC_UI_MOUSE_LOOT_ROW, 1, 1, 0);
    loot_tick(0);
    CHECK(pc_ui_loot_focus() == 1 && loot_req(PC_UI_REQ_LOOT_TAKE, PC_UI_IC_MISC, 1, 0));
    pc_ui_loot_mouse(PC_UI_MOUSE_BUTTON, PC_UI_ALT, 1, 0);
    loot_tick(0);
    CHECK(loot_req(PC_UI_REQ_LOOT_TAKE_ALL, 0, 0, -1));
    // the shell's JSON carries the screen while it shows; closed: gone
    CHECK(json_has(",\"loot\":{"));
    pc_ui_loot_close();
    CHECK(!pc_ui_loot_visible() && !json_has(",\"loot\":{"));
}

static void items_tests()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    pc_ui_shell_set_items(&items_model());
    for (int i = 0; i < 2; ++i) tick(PC_UI_TAB_NEXT); // Ashley Equipment Items
    CHECK(json_has("\"tab\":\"items\"") && json_has("\"screen\":\"items\""));
    CHECK(pc_ui_shell_screen() == PC_UI_SCREEN_ITEMS);
    CHECK(json_has("\"category\":\"weapons\",\"mode\":\"list\",\"cursor\":0,\"rows\":2,\"capacity\":8,\"inventory\":2"));
    int cat, inv;
    CHECK(pc_ui_shell_items_focus(&cat, &inv) && cat == PC_UI_IC_WEAPON && inv == 2);
    // Down / Up wrap
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    CHECK(json_has("\"cursor\":0,"));
    tick(PC_UI_UP);
    CHECK(json_has("\"cursor\":1,\"rows\":2"));
    // R2 / L2 categories (wrapping, empty ones too)
    tick(PC_UI_SUB_PREV); // weapons -> the Workshop (after misc) -> misc
    CHECK(pc_ui_shell_workshop_shown());
    tick(PC_UI_SUB_PREV);
    CHECK(json_has("\"category\":\"misc\",\"mode\":\"list\",\"cursor\":0,\"rows\":3,\"capacity\":64"));
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"category\":\"weapons\",\"mode\":\"list\",\"cursor\":1"));
    // the equipped weapon: Discard greyed with the reason (asked anyway: the bridge buzzes); no Use
    tick(PC_UI_UP);
    CHECK(tick(PC_UI_CONFIRM) == 0 && json_has("\"mode\":\"actions\""));
    CHECK(json_has("\"actions\":[{\"id\":\"discard\",\"enabled\":false,\"reason\":\"Equipped.\"},{\"id\":\"sort\","
                   "\"enabled\":true},{\"id\":\"move\",\"enabled\":true},{\"id\":\"original\",\"enabled\":true}]"));
    tick(PC_UI_CONFIRM);
    CHECK(item_req(PC_UI_REQ_ITEM_DISCARD, PC_UI_IC_WEAPON, 0, 2));
    CHECK(json_has("\"mode\":\"actions\""));
    // Move: the target row, Circle swaps (from 0 to 1); the cursor stays on the target
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    CHECK(json_has("\"action\":2"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"move\"") && json_has("\"move_from\":0"));
    pc_ui_items_view v;
    pc_ui_shell_items_view(&v);
    CHECK(v.mode == PC_UI_IT_MOVE && v.move_from == 0 && v.actions == 4 && v.action_kind[2] == PC_UI_ITA_MOVE);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(item_req(PC_UI_REQ_ITEM_MOVE, PC_UI_IC_WEAPON, 0, 1, 2)); // the moved row's inventory index as seen
    CHECK(json_has("\"mode\":\"list\",\"cursor\":1"));
    // Move onto itself: nothing
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM);
    CHECK(queue_empty() && json_has("\"mode\":\"list\""));
    // Sort: the category's stats; Circle sorts by the stat id (the row and its inventory index as seen)
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM); // weapon 1: discard (enabled), sort...: row 1 is Sort
    CHECK(json_has("\"mode\":\"sort\"") && json_has("\"sort\":{\"cursor\":0,\"stats\":[{\"stat\":0,\"name\":\"Kind\"}"));
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(item_req(PC_UI_REQ_ITEM_SORT, PC_UI_IC_WEAPON, 1, 3, 8));
    // Cross steps back: sort -> actions -> list; in the list it closes
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"sort\""));
    CHECK(tick(PC_UI_BACK) == 0 && json_has("\"mode\":\"actions\""));
    CHECK(tick(PC_UI_BACK) == 0 && json_has("\"mode\":\"list\""));
    // a non-misc discard: Yes / No (it opens on No, as the original's), Yes asks for it (count 1 is ignored by
    // the bridge); No asks for the leave sound
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"confirm\"") && json_has("\"confirm\":{\"kind\":\"discard\",\"yes\":false,\"prompt\":\"Discard 0?\"}"));
    tick(PC_UI_CONFIRM); // No
    CHECK(item_req(PC_UI_REQ_ITEM_CANCEL, PC_UI_IC_WEAPON, 1, 3, 1) && json_has("\"mode\":\"list\""));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_UP);
    CHECK(json_has("\"yes\":true"));
    tick(PC_UI_CONFIRM); // Yes
    CHECK(item_req(PC_UI_REQ_ITEM_DISCARD, PC_UI_IC_WEAPON, 1, 3, 1));
    // Square: details (Up/Down still move, Square back); the focus follows
    tick(PC_UI_ALT);
    CHECK(json_has("\"mode\":\"details\""));
    tick(PC_UI_UP);
    CHECK(pc_ui_shell_items_focus(&cat, &inv) && inv == 2);
    tick(PC_UI_ALT);
    CHECK(json_has("\"mode\":\"list\""));
    // Search: a blade in a weapon focuses the weapon
    tick(PC_UI_SUB_NEXT);
    CHECK(json_has("\"category\":\"blades\""));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"actions\":[{\"id\":\"search\",\"enabled\":true}"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"category\":\"weapons\",\"mode\":\"list\",\"cursor\":1"));
    // misc: Use (asks the bridge), the quantity picker, discard all
    tick(PC_UI_SUB_PREV); // the Workshop
    tick(PC_UI_SUB_PREV);
    CHECK(json_has("\"category\":\"misc\",\"mode\":\"list\",\"cursor\":0"));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"actions\":[{\"id\":\"use\",\"enabled\":true},{\"id\":\"discard\",\"enabled\":true},"
                   "{\"id\":\"discard_all\",\"enabled\":true},{\"id\":\"sort\",\"enabled\":true},{\"id\":\"move\","
                   "\"enabled\":true},{\"id\":\"original\",\"enabled\":true}]"));
    CHECK(tick(PC_UI_CONFIRM) == 0 && pc_ui_shell_visible());
    CHECK(item_req(PC_UI_REQ_ITEM_USE, PC_UI_IC_MISC, 0, 2));
    CHECK(json_has("\"mode\":\"actions\"")); // the bridge closes the menu for it; refused, the menu stays
    tick(PC_UI_BACK);
    // the quantity picker (_discardMenu): starts at 0 step 1
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"quantity\"") && json_has("\"quantity\":{\"count\":0,\"max\":25,\"step\":1}"));
    tick(PC_UI_DOWN); // below 0: nothing
    CHECK(json_has("\"count\":0,"));
    tick(PC_UI_UP);
    tick(PC_UI_UP);
    CHECK(json_has("\"count\":2,\"max\":25,\"step\":1"));
    tick(PC_UI_LEFT); // step 10
    tick(PC_UI_UP);
    tick(PC_UI_UP);
    CHECK(json_has("\"count\":22,\"max\":25,\"step\":10"));
    tick(PC_UI_UP); // 32 > 25: nothing
    CHECK(json_has("\"count\":22,"));
    tick(PC_UI_LEFT); // step 100
    tick(PC_UI_LEFT); // past 100: all
    CHECK(json_has("\"count\":25,\"max\":25,\"step\":100"));
    tick(PC_UI_RIGHT);
    tick(PC_UI_RIGHT);
    CHECK(json_has("\"count\":25,\"max\":25,\"step\":1"));
    tick(PC_UI_RIGHT); // past 1: none
    CHECK(json_has("\"count\":0,\"max\":25,\"step\":1"));
    tick(PC_UI_CONFIRM); // none: back to the list (the bridge plays the leave sound)
    CHECK(item_req(PC_UI_REQ_ITEM_CANCEL, PC_UI_IC_MISC, 0, 2, 0) && json_has("\"mode\":\"list\""));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_UP);
    tick(PC_UI_UP);
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    // the original's discardMultipleConfirm asks discardConfirm (0), not the picker's own message (1)
    CHECK(json_has("\"mode\":\"confirm\"") && json_has("\"prompt\":\"Discard 0?\""));
    CHECK(tick(PC_UI_BACK) == 0 && json_has("\"mode\":\"quantity\"") && json_has("\"count\":3,"));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(item_req(PC_UI_REQ_ITEM_DISCARD, PC_UI_IC_MISC, 0, 2, 3));
    // discard all
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"confirm\":{\"kind\":\"discard_all\",\"yes\":false,\"prompt\":\"Discard 2?\"}"));
    tick(PC_UI_UP);
    tick(PC_UI_CONFIRM);
    CHECK(item_req(PC_UI_REQ_ITEM_DISCARD_ALL, PC_UI_IC_MISC, 0, 2));
    // the single without MP: Use greyed with the reason (asked anyway); discard straight to Yes / No
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"actions\":[{\"id\":\"use\",\"enabled\":false,\"reason\":\"Not enough MP.\"}"));
    tick(PC_UI_CONFIRM);
    CHECK(item_req(PC_UI_REQ_ITEM_USE, PC_UI_IC_MISC, 1, 3));
    CHECK(json_has("\"mode\":\"actions\"") && json_has("\"action\":0")); // refused: the menu stays, the reason shown
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"confirm\"") && json_has("\"prompt\":\"Discard 0?\""));
    tick(PC_UI_BACK);
    CHECK(json_has("\"mode\":\"actions\""));
    tick(PC_UI_BACK);
    // a key: nothing selectable but the original screen
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("{\"id\":\"use\",\"enabled\":false,\"reason\":\"Cannot use.\"},{\"id\":\"discard\",\"enabled\":false,"
                   "\"reason\":\"Cannot discard.\"},{\"id\":\"discard_all\",\"enabled\":false,\"reason\":\"Cannot discard.\"}"));
    tick(PC_UI_BACK);
    // misc sort: by id or quantity (56)
    tick(PC_UI_CONFIRM);
    for (int i = 0; i < 3; ++i) tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(item_req(PC_UI_REQ_ITEM_SORT, PC_UI_IC_MISC, 2, 4, 56));
    // an empty category: only the original screen
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_SUB_NEXT); // misc -> the Workshop -> weapons -> blades -> grips
    CHECK(json_has("\"category\":\"grips\",\"mode\":\"list\",\"cursor\":0,\"rows\":0"));
    CHECK(!pc_ui_shell_items_focus(&cat, &inv));
    tick(PC_UI_ALT);
    CHECK(json_has("\"mode\":\"list\""));
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"actions\":[{\"id\":\"original\",\"enabled\":true}]"));
    CHECK(tick(PC_UI_CONFIRM) == 4 && !pc_ui_shell_visible());
    // reopened (a return): the category and row are kept, the menu is not; the misc cursor survives a
    // cancelled Use (the shell comes back on the item)
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"category\":\"grips\",\"mode\":\"list\""));
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_SUB_NEXT);
    tick(PC_UI_UP); // the key's row 2 -> 1
    tick(PC_UI_CONFIRM);
    pc_ui_shell_close();
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"category\":\"misc\",\"mode\":\"list\",\"cursor\":1"));
    // locked (the snapshot's mask): every action greyed with its reason but the original screen
    {
        pc_ui_items& m = items_model();
        m.locked = 1;
        m.mask = 2;
        std::snprintf(m.locked_reason, sizeof m.locked_reason, "Not while carrying a cube.");
        for (int i = 0; i < PC_UI_ITEM_ROWS; ++i) {
            m.rows[i].discard = m.rows[i].discard_all = m.rows[i].usable = 0;
            std::snprintf(m.rows[i].use_reason, sizeof m.rows[i].use_reason, "Not while carrying a cube.");
            std::snprintf(m.rows[i].discard_reason, sizeof m.rows[i].discard_reason, "Not while carrying a cube.");
        }
        pc_ui_shell_set_items(&m);
        CHECK(json_has("\"locked\":true") && json_has("\"reason\":\"Not while carrying a cube.\""));
        tick(PC_UI_CONFIRM);
        CHECK(json_has("{\"id\":\"sort\",\"enabled\":false,\"reason\":\"Not while carrying a cube.\"},{\"id\":\"move\","
                       "\"enabled\":false,\"reason\":\"Not while carrying a cube.\"},{\"id\":\"original\",\"enabled\":true}"));
        tick(PC_UI_BACK);
        pc_ui_shell_set_items(&items_model());
    }
    // mouse: a category chip, a row (hover focuses, click opens the actions), a menu row (click selects)
    pc_ui_shell_mouse(PC_UI_MOUSE_SUB, PC_UI_IC_WEAPON, 1, 0);
    tick(0);
    CHECK(json_has("\"category\":\"weapons\",\"mode\":\"list\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_ITEM, 1, 0, 0);
    tick(0);
    CHECK(json_has("\"cursor\":1,"));
    pc_ui_shell_mouse(PC_UI_MOUSE_ITEM, 0, 1, 0);
    tick(0);
    CHECK(json_has("\"mode\":\"actions\",\"cursor\":0"));
    pc_ui_shell_mouse(PC_UI_MOUSE_ITEM_MENU, 1, 1, 0); // Sort
    tick(0);
    CHECK(json_has("\"mode\":\"sort\""));
    pc_ui_shell_mouse(PC_UI_MOUSE_ITEM, 1, 1, 0); // a list row while a menu is open: back to the list on it
    tick(0);
    CHECK(json_has("\"mode\":\"list\",\"cursor\":1"));
    CHECK(queue_empty());
    // the list shrinks under the cursor (an item used up): clamped
    {
        pc_ui_items& m = items_model();
        m.count[PC_UI_IC_WEAPON] = 1;
        pc_ui_shell_set_items(&m);
        CHECK(json_has("\"category\":\"weapons\",\"mode\":\"list\",\"cursor\":0,\"rows\":1"));
    }
    // Triangle closes from a menu too
    tick(PC_UI_CONFIRM);
    CHECK(tick(PC_UI_EXIT) == -1);
    // and from the quantity picker, as the original's (Cross goes back one level instead)
    pc_ui_shell_open(&all, 1);
    tick(PC_UI_SUB_PREV); // the Workshop
    tick(PC_UI_SUB_PREV);
    tick(PC_UI_UP); // the stack of 25 (the misc cursor was kept on row 1)
    CHECK(json_has("\"category\":\"misc\",\"mode\":\"list\",\"cursor\":0"));
    tick(PC_UI_CONFIRM);
    tick(PC_UI_DOWN);
    tick(PC_UI_CONFIRM);
    CHECK(json_has("\"mode\":\"quantity\""));
    CHECK(tick(PC_UI_EXIT) == -1 && !pc_ui_shell_visible() && queue_empty());
}

// ---- The Quick manual (System > Quick manual: ui_manual.cpp, manual_parse.c) ----
// 494.c _setMenuRows' window over rowCount rows (9 shown), as written.
static void ref_seed(int rowCount, int highlightedRow, int* cm4, int* cm5)
{
    if ((rowCount < 9) || (highlightedRow < 8)) {
        *cm4 = highlightedRow, *cm5 = 0;
    } else if (highlightedRow >= (rowCount - 8)) {
        *cm4 = highlightedRow - (rowCount - 9), *cm5 = rowCount - 9;
    } else {
        *cm4 = 4, *cm5 = highlightedRow - 4;
    }
}
// 58EC.c vs_mainmenu_setMenuRows' normalisation of the memories (cursor memory on), rowInfo 1.
static void ref_normalise(int rowCount, int rowInfo, int* row, int* page)
{
    int i = *row, p = *page, t = rowCount - 10 + rowInfo;
    if (t < 0) {
        i += p, p = 0;
        if (i >= rowCount)
            i = 0;
    } else {
        if (t < p) {
            if (i + p >= rowCount)
                i = 0, p = 0;
            else
                i += p - t, p = t;
        }
        int v = p < t;
        if (p > 0 && i == 0)
            i = 1, p -= 1, v = p < t;
        if (v && i == 9 - rowInfo)
            i -= 1, p += 1;
    }
    *row = i, *page = p;
}
// 58EC.c case 17's Up / Down, as written (char page arithmetic included).
static void ref_list(int count, int info, int* sel, int* page, unsigned rep, unsigned press)
{
    if (rep & PC_UI_UP) {
        if (count <= 10 - info) {
            if (*sel == 0) *sel = count - 1; else --*sel;
        } else if (*page == 0) {
            if (*sel == 0) {
                if (press & PC_UI_UP)
                    *sel = 9 - info, *page = (signed char)(info + (count + 246));
            } else --*sel;
        } else if (*sel == 1) --*page;
        else --*sel;
    }
    if (rep & PC_UI_DOWN) {
        if (count <= 10 - info) {
            if (*sel == count - 1) *sel = 0; else ++*sel;
        } else if (*page == count + (info - 10)) {
            if (*sel == 9 - info) {
                if (press & PC_UI_DOWN)
                    *sel = 0, *page = 0;
            } else ++*sel;
        } else if (*sel == 8 - info) ++*page;
        else ++*sel;
    }
}

// Synthetic page files.
struct Bytes {
    std::string b;
    void u8(int v) { b += (char)(v & 0xFF); }
    void u16(int v) { u8(v), u8(v >> 8); }
    void u32(unsigned v) { u16((int)(v & 0xFFFF)), u16((int)(v >> 16)); }
    void pad4() { while (b.size() % 4) u8(0); }
    void put16(size_t at, int v) { b[at] = (char)(v & 0xFF), b[at + 1] = (char)((v >> 8) & 0xFF); }
    void put32(size_t at, unsigned v) { put16(at, (int)(v & 0xFFFF)), put16(at + 2, (int)(v >> 16)); }
};
static void anim(Bytes& o, int enabled, int mask, int duration, int repeat)
{
    o.u16(enabled), o.u16(mask), o.u16(duration), o.u16(repeat);
    for (int i = 0; i < 4; ++i) o.u16(0);
}
static std::string synth_hf0()
{
    const std::vector<std::vector<int>> lines = {
        {0x8F, 0x8F, 10, 37, 0x8F, 38, 0xE7},                                       // "  Ab c"
        {0xFB, 1, 33, 0xFA, 10, 34, 0xFB, 2, 0xAE, 0xB7, 0xB3, 0xEE, 5, 0xE9, 0xB6, 0xE7}, // X, Y, codes
        {0xE7},
    };
    Bytes o;
    o.u32(0), o.u32(0), o.u32(0), o.u32(0);
    // text at 16: n, offsets (halfwords from 16), strings
    size_t base = o.b.size();
    o.u16((int)lines.size());
    size_t offs = o.b.size();
    for (size_t i = 0; i < lines.size(); ++i) o.u16(0);
    for (size_t i = 0; i < lines.size(); ++i) {
        if ((o.b.size() - base) % 2) o.u8(0xEB);
        o.put16(offs + i * 2, (int)((o.b.size() - base) / 2));
        for (int c : lines[i]) o.u8(c);
    }
    o.pad4();
    unsigned spr = (unsigned)(o.b.size() - 16);
    o.put32(0, spr);
    // sprites: 8bpp 16 x 16 (blocks 0, 1; CLUT 0) blinking, 4bpp 16 x 16 (block 1; CLUT 1), the first again,
    // one with a bad block id
    size_t sb = o.b.size();
    o.u16(4);
    size_t soffs = o.b.size();
    for (int i = 0; i < 4; ++i) o.u16(0);
    struct S { int en, x, y, w, h, colors, clut; std::vector<int> blocks; };
    S ss[4] = {{1, 3, 4, 16, 16, 256, 0, {0, 1}}, {0, 40, 13, 16, 16, 16, 1, {1}}, {0, 80, 26, 16, 16, 256, 0, {0, 1}},
        {0, 0, 0, 8, 16, 256, 0, {99}}};
    for (int i = 0; i < 4; ++i) {
        o.put16(soffs + i * 2, (int)((o.b.size() - sb) / 2));
        anim(o, ss[i].en, 0x0005, 1, 1);
        o.u16(ss[i].x), o.u16(ss[i].y), o.u16(ss[i].w), o.u16(ss[i].h), o.u16((int)ss[i].blocks.size());
        o.u16(ss[i].colors), o.u16(ss[i].clut);
        for (int bl : ss[i].blocks) o.u16(bl);
    }
    o.pad4();
    unsigned lin = (unsigned)(o.b.size() - sb);
    o.put32(4, lin);
    o.u16(1);
    anim(o, 0, 0, 0, 0);
    o.u16(0), o.u16(6), o.u16(300), o.u16(6), o.u8(0), o.u8(128), o.u8(255), o.u8(0);
    o.put32(8, 28 + 2);
    return o.b;
}
static std::string synth_hf1()
{
    Bytes o;
    o.u32(2), o.u32(2);
    for (int i = 0; i < 128; ++i) o.u8(i == 1 ? 17 : 0); // block 0: pixel (1, 0) = 17 (CLUT line 1, entry 1)
    for (int i = 0; i < 128; ++i) o.u8(3);               // block 1: 3 (8bpp: line 0 entry 3; 4bpp: 3, 0)
    for (int i = 0; i < 16; ++i) o.u16(i == 3 ? 0x001F : 0); // line 0: 0 transparent, 3 red
    for (int i = 0; i < 16; ++i) o.u16(i == 1 ? 0x7C00 : i == 3 ? 0x03E0 : 0); // line 1: 1 blue, 3 green
    return o.b;
}

static std::vector<pc_ui_manual_event> manual_events()
{
    std::vector<pc_ui_manual_event> v;
    pc_ui_manual_event e;
    while (pc_ui_manual_take(&e))
        v.push_back(e);
    return v;
}
static int count_kind(const std::vector<pc_ui_manual_event>& v, int kind)
{
    int n = 0;
    for (auto& e : v) n += e.kind == kind;
    return n;
}
static const pc_ui_manual_event* find_kind(const std::vector<pc_ui_manual_event>& v, int kind)
{
    for (auto& e : v)
        if (e.kind == kind)
            return &e;
    return nullptr;
}
// The bridge's fill: lines per topic (40: line_count 30), the game's scroll and current topic.
static unsigned short manual_scroll[14];
static int manual_lc[14], manual_loaded[14];
static void manual_fill(int cursor_memory, int current)
{
    pc_ui_manual_fill(cursor_memory, current, manual_scroll, manual_lc, manual_loaded);
}
// What the bridge does with the events (the scroll memory), then a tick.
static void manual_apply(const std::vector<pc_ui_manual_event>& v, int* current)
{
    for (auto& e : v) {
        if (e.kind == PC_UI_MEV_SCROLL) manual_scroll[e.a] = (unsigned short)e.b;
        if (e.kind == PC_UI_MEV_LIST_STORE) *current = e.c;
        if (e.kind == PC_UI_MEV_OPEN && e.a) { std::memset(manual_scroll, 0, sizeof manual_scroll); *current = 0; }
        if (e.kind == PC_UI_MEV_ORIGINAL) *current = e.a;
    }
}
static int mtick(unsigned pressed, unsigned repeat = 0, int square = 0, int start = 0)
{
    pc_ui_manual_buttons(square, start);
    pc_ui_input in = {pressed, repeat | pressed};
    return pc_ui_shell_tick(&in);
}
static void manual_view(pc_ui_manual_view* v) { pc_ui_manual_view_get(v); }
// The shell on System > Quick manual's row (entry 3: Data, Options, Score, Quick manual).
static void manual_row()
{
    pc_ui_commands all = {0x3FF, 0, 0};
    pc_ui_shell_open(&all, 0);
    tick(PC_UI_TAB_PREV);
    for (int i = 0; i < 3; ++i) tick(PC_UI_DOWN);
    manual_events();
}

static void manual_tests()
{
    // ---- the original's rules ----
    for (int r = 0; r < 14; ++r) {
        int a4, a5, b4, b5;
        pc_ui_manual_seed(r, &a4, &a5);
        ref_seed(14, r, &b4, &b5);
        CHECK(a4 == b4 && a5 == b5);
        int n4 = b4, n5 = b5;
        ref_normalise(14, 1, &n4, &n5);
        CHECK(n4 == b4 && n5 == b5); // setMenuRows keeps the seed (14 rows)
        CHECK(a4 + a5 == r && a4 >= 0 && a4 <= 8 && a5 >= 0 && a5 <= 5);
    }
    {
        unsigned seed = 12345;
        int row = 0, page = 0, rr = 0, rp = 0;
        for (int i = 0; i < 1000; ++i) {
            seed = seed * 1103515245u + 12345u;
            unsigned bits = seed >> 16;
            unsigned rep = bits & 3, press = rep & (bits >> 2);
            int before = row + page;
            int changed = pc_ui_manual_list_step(&row, &page, rep & PC_UI_UP, press & PC_UI_UP, rep & PC_UI_DOWN,
                press & PC_UI_DOWN);
            ref_list(14, 1, &rr, &rp, rep, press);
            CHECK(row == rr && page == rp);
            CHECK(changed == (row + page != before));
            CHECK(row >= 0 && row <= 8 && page >= 0 && page <= 5);
        }
        row = page = 0;
        for (int i = 0; i < 8; ++i) pc_ui_manual_list_step(&row, &page, 0, 0, 1, 0);
        CHECK(row == 7 && page == 1);
        row = page = 0;
        CHECK(!pc_ui_manual_list_step(&row, &page, 1, 0, 0, 0) && row == 0 && page == 0); // a repeat clamps
        CHECK(pc_ui_manual_list_step(&row, &page, 1, 1, 0, 0) && row == 8 && page == 5);  // a press wraps
        CHECK(!pc_ui_manual_list_step(&row, &page, 0, 0, 1, 0) && row == 8 && page == 5);
        CHECK(pc_ui_manual_list_step(&row, &page, 0, 0, 1, 1) && row == 0 && page == 0);
    }
    const int ns[] = {0, 9, 10, 11, 37, 220}, lcs[] = {0, 0, 0, 10, 30, 210};
    for (int i = 0; i < 6; ++i)
        CHECK(pc_ui_manual_line_count(ns[i]) == lcs[i] && lcs[i] / 10 + 1 == (i < 3 ? 1 : i == 3 ? 2 : i == 4 ? 4 : 22));
    {
        int sc = 0;
        CHECK(!pc_ui_manual_step(&sc, 30, 0, 0, 1, 0, 0, 0) && sc == 0);  // a repeat at the top clamps
        CHECK(pc_ui_manual_step(&sc, 30, 0, 0, 1, 1, 0, 0) && sc == 30);  // a press wraps to the end
        CHECK(!pc_ui_manual_step(&sc, 30, 0, 0, 0, 0, 1, 0) && sc == 30);
        CHECK(pc_ui_manual_step(&sc, 30, 0, 0, 0, 0, 1, 1) && sc == 0);
        CHECK(pc_ui_manual_step(&sc, 30, 0, 0, 0, 0, 1, 0) && sc == 1);
        sc = 0;
        CHECK(!pc_ui_manual_step(&sc, 0, 0, 0, 1, 1, 0, 0) && sc == 0); // no wrap without a second page
        CHECK(!pc_ui_manual_step(&sc, 0, 0, 0, 0, 0, 1, 1) && sc == 0);
        CHECK(!pc_ui_manual_step(&sc, 0, 1, 0, 1, 1, 0, 0) && sc == 0);
        sc = 13; // Square from mid-page: page 1 -> 2 (snapped)
        CHECK(pc_ui_manual_step(&sc, 30, 1, 0, 0, 0, 1, 0) && sc == 20);
        sc = 3;
        CHECK(pc_ui_manual_step(&sc, 30, 1, 0, 0, 0, 1, 0) && sc == 10);
        sc = 3; // page 0, Up pressed: the last page
        CHECK(pc_ui_manual_step(&sc, 30, 1, 0, 1, 1, 0, 0) && sc == 30);
        CHECK(pc_ui_manual_step(&sc, 30, 1, 0, 0, 0, 1, 1) && sc == 0);
        sc = 35;
        CHECK(pc_ui_manual_step(&sc, 30, 1, 0, 1, 0, 0, 0) && sc == 20);
        sc = 17; // Start wins, no sound
        CHECK(!pc_ui_manual_step(&sc, 30, 1, 1, 1, 1, 1, 1) && sc == 0);
    }
    {
        pc_ui_manual_anim a = {1, 0x0005, 1, 0, 0, 0, 0, 0};
        int seq[18];
        for (int i = 0; i < 18; ++i) seq[i] = pc_ui_manual_anim_step(&a);
        CHECK(seq[0] == 0 && seq[1] == 1 && seq[2] == 0 && seq[3] == 1 && seq[15] == 1 && seq[16] == 1 && seq[17] == 1);
        pc_ui_manual_anim b = {1, (short)0x8001, 2, 1, 0, 0, 0, 0};
        int hid[40];
        for (int i = 0; i < 40; ++i) hid[i] = pc_ui_manual_anim_step(&b);
        CHECK(hid[0] == 0 && hid[1] == 0 && hid[2] == 1 && hid[29] == 1 && hid[30] == 0 && hid[31] == 0 && hid[32] == 0
              && hid[33] == 0 && hid[34] == 1); // frame 15 (0x8000) shown, then frame 0 again (repeat)
    }
    // ---- the files ----
    {
        std::string h0 = synth_hf0(), h1 = synth_hf1();
        unsigned char widths[378];
        std::memset(widths, 5, sizeof widths);
        widths[37] = 4; // 'b'
        pc_ui_manual_topic t;
        CHECK(pc_ui_manual_parse((const unsigned char*)h0.data(), (unsigned)h0.size(), (const unsigned char*)h1.data(),
            (unsigned)h1.size(), widths, 378, &t));
        CHECK(t.ok && t.lines == 3 && t.line_count == 0 && t.pages == 1);
        CHECK(t.line_first[0] == 0 && t.line_first[1] == 2 && t.line_first[2] == 7 && t.line_first[3] == 7);
        const pc_ui_manual_run* r = t.runs;
        CHECK(r[0].kind == PC_UI_MANUAL_WORD && r[0].x == 12 && r[0].w == 9 && r[0].len == 2 && h0[r[0].off] == 10);
        CHECK(r[1].kind == PC_UI_MANUAL_WORD && r[1].x == 27 && r[1].w == 5 && r[1].len == 1);
        CHECK(r[2].kind == PC_UI_MANUAL_WORD && r[2].x == 0 && r[2].colour == 1 && r[2].len == 1);
        CHECK(r[3].kind == PC_UI_MANUAL_WORD && r[3].x == 15 && r[3].colour == 1);
        CHECK(r[4].kind == PC_UI_MANUAL_GLYPH && r[4].glyph == 0 && r[4].x == 20 && r[4].colour == 2);
        CHECK(r[5].kind == PC_UI_MANUAL_STAR && r[5].x == 25);
        CHECK(r[6].kind == PC_UI_MANUAL_ARROW && r[6].glyph == 1 && r[6].x == 30);
        // joins: the line's first run and after two spaces or an FA advance apart, one space, touching
        CHECK(r[0].gap == PC_UI_MANUAL_GAP_APART && r[1].gap == PC_UI_MANUAL_GAP_SPACE);
        CHECK(r[2].gap == PC_UI_MANUAL_GAP_APART && r[3].gap == PC_UI_MANUAL_GAP_APART);
        CHECK(r[4].gap == PC_UI_MANUAL_GAP_NONE && r[5].gap == PC_UI_MANUAL_GAP_NONE && r[6].gap == PC_UI_MANUAL_GAP_NONE);
        CHECK(t.unknown == 3 + 1); // EE, E9, the Lv. sign; the bad block id
        CHECK(t.nsprites == 4 && t.ntex == 3 && t.sprites[2].tex == t.sprites[0].tex);
        CHECK(t.sprites[0].a.enabled == 1 && t.sprites[0].x == 3 && t.sprites[0].y == 4);
        const unsigned char* p8 = t.rgba[t.sprites[0].tex];
        CHECK(p8[3] == 0);                                    // (0, 0): 0 -> 0x0000, transparent
        CHECK(p8[4 + 2] == 255 && p8[4 + 0] == 0 && p8[4 + 3] == 255); // (1, 0): 17 -> line 1 entry 1, blue
        CHECK(p8[8 * 4] == 255 && p8[8 * 4 + 1] == 0 && p8[8 * 4 + 3] == 255); // (8, 0): block 1, 3 -> red
        const unsigned char* p4 = t.rgba[t.sprites[1].tex];
        CHECK(p4[1] == 255 && p4[0] == 0 && p4[3] == 255);    // 4bpp: nibble 3 -> line 1 entry 3, green
        CHECK(p4[4 + 3] == 0);                                // the high nibble 0: transparent
        CHECK(t.nrules == 1 && t.rules[0].x1 == 300 && t.rules[0].y0 == 6 && t.rules[0].g == 128 && t.rules[0].b == 255);
        // button art: none here (the retail images' hashes only); the hash is FNV-1a 64 of w, h and the pixels
        CHECK(t.tex_art && t.tex_art[0] == -1 && t.tex_art[1] == -1 && t.tex_art[2] == -1);
        const unsigned char px1[4] = {1, 2, 3, 255};
        CHECK(pc_ui_manual_art_hash(px1, 1, 1) == 0xd7b93253beb54dd4ull && pc_ui_manual_button_art(px1, 1, 1) == -1);
        CHECK(pc_ui_manual_button_art(nullptr, 16, 16) == -1);
        pc_ui_manual_free(&t);
        CHECK(!t.ok && !t.runs);
        // every truncation: parsed or refused, never read past the buffer (ASan builds check it)
        int refused = 0;
        for (size_t n = 0; n <= h0.size(); ++n) {
            std::vector<unsigned char> cut(h0.begin(), h0.begin() + (long)n);
            pc_ui_manual_topic c;
            if (!pc_ui_manual_parse(cut.data(), (unsigned)n, (const unsigned char*)h1.data(), (unsigned)h1.size(),
                    widths, 378, &c))
                ++refused;
            else
                CHECK(c.unknown > 0 || n == h0.size());
            pc_ui_manual_free(&c);
        }
        for (size_t n = 0; n <= h1.size(); n += 7) {
            std::vector<unsigned char> cut(h1.begin(), h1.begin() + (long)n);
            pc_ui_manual_topic c;
            CHECK(pc_ui_manual_parse((const unsigned char*)h0.data(), (unsigned)h0.size(), cut.data(), (unsigned)n,
                widths, 378, &c));
            pc_ui_manual_free(&c);
        }
        CHECK(refused > 0);
        pc_ui_manual_topic none;
        CHECK(!pc_ui_manual_parse(nullptr, 0, nullptr, 0, widths, 378, &none));
    }

    // ---- the shell ----
    for (int i = 0; i < 14; ++i)
        manual_lc[i] = 30, manual_loaded[i] = 1, manual_scroll[i] = 0;
    manual_scroll[5] = 12;
    int current = 5;
    pc_ui_manual_view v;
    manual_row();
    manual_fill(1, current);
    CHECK(json_has("\"row\":\"manual\"") && json_has("\"manual\":null"));
    // Circle: the topics on the remembered topic; the shell stays; OPEN (select) and the window
    CHECK(mtick(PC_UI_CONFIRM) == 0 && pc_ui_shell_visible() && pc_ui_shell_manual_shown());
    auto ev = manual_events();
    CHECK(ev.size() == 2 && ev[0].kind == PC_UI_MEV_OPEN && ev[0].a == 0 && ev[1].kind == PC_UI_MEV_LIST_STORE
          && ev[1].a == 5 && ev[1].b == 0 && ev[1].c == 5);
    CHECK(json_has("\"manual\":{\"mode\":\"topics\",\"topic\":5,\"row\":5,\"page_top\":0,\"line\":12,"));
    CHECK(json_has("\"sfx\":{\"change\":0,\"select\":1,\"leave\":0}"));
    for (const char* key : {"\"lines\":", "\"line_count\":30", "\"pages\":4", "\"loaded\":1", "\"k\":", "\"size\":",
             "\"overflow\":", "\"unknown\":", "\"page_lines\":[", "\"sprites\":", "\"rules\":", "\"hidden\":",
             "\"hits\":[", "\"label\":", "\"desc\":"})
        CHECK(json_has(key));
    // Down x3: rows 6, 7, then the window scrolls (row 7, page 1): topic 8, three change sounds
    for (int i = 0; i < 3; ++i) mtick(PC_UI_DOWN);
    manual_view(&v);
    CHECK(v.mode == PC_UI_MANUAL_LIST && v.topic == 8 && v.row == 7 && v.page == 1);
    ev = manual_events();
    CHECK(count_kind(ev, PC_UI_MEV_SOUND) == 3 && ev[0].a == 2);
    // Circle: reading topic 8 (its window stored, currentManual 8, the select sound)
    mtick(PC_UI_CONFIRM);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(ev.size() == 2 && ev[0].kind == PC_UI_MEV_LIST_STORE && ev[0].a == 7 && ev[0].b == 1 && ev[0].c == 8
          && ev[1].kind == PC_UI_MEV_SOUND && ev[1].a == 1 && current == 8);
    CHECK(json_has("\"mode\":\"reading\",\"topic\":8"));
    // Down: a line (change, the scroll written); Square held + Down: a page; Start: the top (no sound)
    mtick(PC_UI_DOWN);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(ev.size() == 2 && ev[0].kind == PC_UI_MEV_SOUND && ev[0].a == 2 && ev[1].kind == PC_UI_MEV_SCROLL
          && ev[1].a == 8 && ev[1].b == 1);
    mtick(PC_UI_DOWN, 0, 1);
    manual_view(&v);
    CHECK(v.line == 10 && v.square_held);
    mtick(0, 0, 0, 1);
    ev = manual_events();
    manual_apply(ev, &current);
    manual_view(&v);
    CHECK(v.line == 0 && count_kind(ev, PC_UI_MEV_SOUND) == 1); // Square + Down's change only
    // Up pressed at the top wraps to the last position; a repeat does not
    mtick(0, PC_UI_UP);
    manual_view(&v);
    CHECK(v.line == 0);
    mtick(PC_UI_UP);
    manual_view(&v);
    CHECK(v.line == 30 && json_has("\"line\":30,\"lines\":0,\"line_count\":30,\"page\":4,\"pages\":4"));
    // Right on the last page (pressed): the first; Left (a repeat): no wrap
    mtick(PC_UI_RIGHT);
    manual_view(&v);
    CHECK(v.line == 0);
    mtick(0, PC_UI_LEFT);
    manual_view(&v);
    CHECK(v.line == 0);
    mtick(0, PC_UI_RIGHT);
    manual_view(&v);
    CHECK(v.line == 10);
    // Square pressed while reading: the page mode only, never the original screen
    CHECK(mtick(PC_UI_ALT, 0, 1) == 0 && pc_ui_shell_visible());
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(!find_kind(ev, PC_UI_MEV_ORIGINAL));
    // Circle: back to the topics (select), the window re-seeded from topic 8 (row 3, page 5)
    mtick(PC_UI_CONFIRM);
    ev = manual_events();
    manual_apply(ev, &current);
    manual_view(&v);
    CHECK(v.mode == PC_UI_MANUAL_LIST && v.topic == 8 && v.row == 3 && v.page == 5);
    CHECK(find_kind(ev, PC_UI_MEV_SOUND) && find_kind(ev, PC_UI_MEV_SOUND)->a == 1);
    CHECK(find_kind(ev, PC_UI_MEV_LIST_STORE) && find_kind(ev, PC_UI_MEV_LIST_STORE)->a == 3);
    CHECK(manual_scroll[8] == 10);
    // the line is remembered per topic: read 8 again
    mtick(PC_UI_CONFIRM);
    manual_view(&v);
    CHECK(v.mode == PC_UI_MANUAL_READ && v.line == 10);
    manual_events();
    // Cross: reading -> topics (leave) -> the System list on the manual row (leave, CLOSE)
    mtick(PC_UI_BACK);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_LIST && find_kind(ev, PC_UI_MEV_SOUND)->a == 0
          && !find_kind(ev, PC_UI_MEV_CLOSE));
    mtick(PC_UI_BACK);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_OFF && pc_ui_shell_visible());
    CHECK(ev.size() == 3 && ev[0].kind == PC_UI_MEV_LIST_STORE && ev[0].c == 8 && ev[1].kind == PC_UI_MEV_SOUND
          && ev[1].a == 0 && ev[2].kind == PC_UI_MEV_CLOSE);
    CHECK(json_has("\"row\":\"manual\"") && json_has("\"manual\":null"));
    CHECK(json_has("\"mode\":\"list\",\"item\":\"manual\""));
    // Circle + Cross in one frame: Circle wins (58EC.c case 17 checks Circle before Cross; 494.c leaves the
    // page on either, with the select sound when Circle was pressed)
    manual_fill(1, current);
    mtick(PC_UI_CONFIRM);
    manual_events();
    mtick(PC_UI_CONFIRM | PC_UI_BACK);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_READ && pc_ui_shell_visible() && !find_kind(ev, PC_UI_MEV_CLOSE)
          && count_kind(ev, PC_UI_MEV_SOUND) == 1 && find_kind(ev, PC_UI_MEV_SOUND)->a == 1);
    mtick(PC_UI_CONFIRM | PC_UI_BACK);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_LIST && count_kind(ev, PC_UI_MEV_SOUND) == 1
          && find_kind(ev, PC_UI_MEV_SOUND)->a == 1 && !find_kind(ev, PC_UI_MEV_CLOSE));
    mtick(PC_UI_BACK);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_OFF && pc_ui_shell_visible() && find_kind(ev, PC_UI_MEV_CLOSE));
    // Triangle closes from both modes, no sound of the manual's (deliver -1 plays it)
    manual_fill(1, current);
    mtick(PC_UI_CONFIRM);
    manual_events();
    CHECK(mtick(PC_UI_EXIT) == -1 && !pc_ui_shell_visible());
    ev = manual_events();
    CHECK(count_kind(ev, PC_UI_MEV_SOUND) == 0 && count_kind(ev, PC_UI_MEV_CLOSE) == 1
          && count_kind(ev, PC_UI_MEV_LIST_STORE) == 1 && pc_ui_manual_mode() == PC_UI_MANUAL_OFF);
    manual_row();
    manual_fill(1, current);
    mtick(PC_UI_CONFIRM);
    mtick(PC_UI_CONFIRM);
    manual_events();
    CHECK(mtick(PC_UI_EXIT, PC_UI_DOWN) == -1);
    ev = manual_events();
    manual_apply(ev, &current);
    CHECK(count_kind(ev, PC_UI_MEV_SOUND) == 1 && ev[0].kind == PC_UI_MEV_SOUND && ev[0].a == 2); // the step's change
    CHECK(count_kind(ev, PC_UI_MEV_CLOSE) == 1 && manual_scroll[8] == 11);
    // Square on the System row: the original MENUE (result 10) on the game's topic
    manual_row();
    manual_fill(1, current);
    CHECK(mtick(PC_UI_ALT) == 10 && !pc_ui_shell_visible());
    ev = manual_events();
    CHECK(ev.size() == 1 && ev[0].kind == PC_UI_MEV_ORIGINAL && ev[0].a == 8);
    // Square on the topics: the store, CLOSE, then ORIGINAL on the focused topic
    manual_row();
    manual_fill(1, current);
    mtick(PC_UI_CONFIRM);
    mtick(PC_UI_UP);
    manual_events();
    CHECK(mtick(PC_UI_ALT) == 10 && !pc_ui_shell_visible() && pc_ui_manual_mode() == PC_UI_MANUAL_OFF);
    ev = manual_events();
    CHECK(ev.size() == 3 && ev[0].kind == PC_UI_MEV_LIST_STORE && ev[0].c == 7 && ev[1].kind == PC_UI_MEV_CLOSE
          && ev[2].kind == PC_UI_MEV_ORIGINAL && ev[2].a == 7);
    // cursor memory off: OPEN zeroes, the topics start on 0 with every line at 0
    manual_row();
    manual_fill(0, 9);
    mtick(PC_UI_CONFIRM);
    ev = manual_events();
    manual_view(&v);
    CHECK(ev[0].kind == PC_UI_MEV_OPEN && ev[0].a == 1 && v.topic == 0 && v.row == 0 && v.page == 0);
    for (int i = 0; i < 14; ++i) CHECK(pc_ui_manual_line_of(i) == 0);
    // a tab switch leaves (the store, CLOSE once); so does pc_ui_shell_close; forget drops everything
    mtick(PC_UI_CONFIRM);
    manual_events();
    mtick(PC_UI_TAB_NEXT);
    ev = manual_events();
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_OFF && count_kind(ev, PC_UI_MEV_CLOSE) == 1
          && count_kind(ev, PC_UI_MEV_SCROLL) == 1);
    manual_row();
    manual_fill(1, 2);
    mtick(PC_UI_CONFIRM);
    manual_events();
    pc_ui_shell_close();
    ev = manual_events();
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_OFF && count_kind(ev, PC_UI_MEV_CLOSE) == 1);
    pc_ui_shell_close();
    CHECK(manual_events().empty());
    manual_row();
    mtick(PC_UI_CONFIRM);
    pc_ui_manual_forget();
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_OFF && manual_events().empty());
    pc_ui_shell_close();
    CHECK(manual_events().empty());
    // ---- the pointer ----
    manual_row();
    for (int i = 0; i < 14; ++i) manual_scroll[i] = 0;
    manual_fill(1, 0);
    mtick(PC_UI_CONFIRM);
    manual_events();
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_TOPIC, 11, 0, 0); // hover: the focus walks there (the window follows)
    mtick(0);
    manual_view(&v);
    CHECK(v.topic == 11 && v.row + v.page == 11 && v.row >= 1 && v.row <= 8);
    ev = manual_events();
    CHECK(count_kind(ev, PC_UI_MEV_SOUND) == 1);
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_TOPIC, 2, 1, 0); // a click: that topic, read
    mtick(0);
    manual_view(&v);
    CHECK(v.mode == PC_UI_MANUAL_READ && v.topic == 2);
    manual_events();
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, -1); // the wheel: a line a notch
    mtick(0);
    manual_view(&v);
    CHECK(v.line == 1);
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, 1);
    mtick(0);
    pc_ui_shell_mouse(PC_UI_MOUSE_NONE, 0, 0, 1);
    mtick(0);
    manual_view(&v);
    CHECK(v.line == 0); // clamps (no wrap)
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_PAGE, 1, 1, 0);
    mtick(0);
    manual_view(&v);
    CHECK(v.line == 10);
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_PAGE, -1, 1, 0);
    mtick(0);
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_PAGE, -1, 1, 0);
    mtick(0);
    manual_view(&v);
    CHECK(v.line == 0);
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_SCROLL, 25, 1, 0);
    mtick(0);
    manual_view(&v);
    CHECK(v.line == 25);
    ev = manual_events();
    CHECK(count_kind(ev, PC_UI_MEV_SOUND) == 5); // wheel down, up (the second clamps), page +1, -1 (the second clamps), the track
    // not loaded: content clicks are ignored
    manual_loaded[2] = 0;
    manual_fill(1, 2);
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_PAGE, -1, 1, 0);
    mtick(0);
    manual_view(&v);
    CHECK(v.line == 25);
    manual_loaded[2] = 1;
    manual_fill(1, 2);
    // a rail click on another topic while reading: that topic
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_TOPIC, 6, 1, 0);
    mtick(0);
    manual_view(&v);
    CHECK(v.mode == PC_UI_MANUAL_READ && v.topic == 6);
    // a right click: back (Cross)
    pc_ui_shell_mouse(PC_UI_MOUSE_BUTTON, PC_UI_BACK, 1, 0);
    mtick(0);
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_LIST);
    // the canvas in the topics: read
    pc_ui_shell_mouse(PC_UI_MOUSE_MANUAL_CANVAS, 0, 1, 0);
    mtick(0);
    CHECK(pc_ui_manual_mode() == PC_UI_MANUAL_READ);
    pc_ui_shell_close();
    manual_events();
    CHECK(json_has("\"manual\":null") || !pc_ui_shell_visible());
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
    // Items unselectable (carrying a cube: mask bit 2): browse only with a reason (before the bridge's
    // snapshot: the command row's), and the original Items stays reachable (command 3 -> 4), as the
    // original top level opens it
    pc_ui_commands no_items = {0x3FF, 8, 0x02};
    pc_ui_shell_open(&no_items, 0);
    for (int i = 0; i < 2; ++i) tick(PC_UI_TAB_NEXT); // Ashley Equipment Items
    CHECK(json_has("\"tab\":\"items\""));
    CHECK(json_has("\"locked\":true"));
    CHECK(json_has("\"reason\":\"Not while carrying a cube.\""));
    CHECK(tick(PC_UI_CONFIRM) == 0 && json_has("\"mode\":\"actions\""));
    CHECK(json_has("\"actions\":[{\"id\":\"original\",\"enabled\":true}]"));
    CHECK(tick(PC_UI_CONFIRM) == 4);
    CHECK(!pc_ui_shell_visible());
    // the reason follows the game's order (lowest bit the command cares about): cube (2) before hanging (4)
    pc_ui_commands hanging = {0x3FF, 8, 0x06};
    pc_ui_shell_open(&hanging, 1);
    CHECK(json_has("\"reason\":\"Not while carrying a cube.\""));
    // a bit Items does not care about (8, battle mode) gives the general reason
    pc_ui_commands other = {0x3FF, 8, 0x08};
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
    tint_tests();
    equipment_tests();
    mouse_tests();
    system_tests();
    ashley_tests();
    abilities_tests();
    magick_tests();
    items_tests();
    loot_tests();
    workshop_tests();
    map_tests();
    data_tests();
    container_tests();
    manual_tests();
    std::printf(failures ? "ui_shell_probe: %d failures\n" : "ui_shell_probe: ok\n", failures);
    return failures != 0;
}
