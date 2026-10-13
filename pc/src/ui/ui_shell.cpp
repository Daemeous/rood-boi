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
struct TabDef {
    const char* id;
    const char* title;
    RowDef rows[6];
    int count;
};

const TabDef TABS[] = {
    {"ashley", "Ashley", {{"status", "Status", "Ashley's condition, equipment and statistics.", ROW_COMMAND, 4}}, 1},
    {"equipment", "Equipment", {{"equip", "Equipment", "Change weapons, shields and armour.", ROW_COMMAND, 3}}, 1},
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

struct State {
    bool visible = false;
    int tab = 0;
    int row[TAB_COUNT] = {};
    pc_ui_commands commands = {0, 0, 0};
} s;

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
        if (*p == '"' || *p == '\\')
            out += '\\';
        out += *p;
    }
    return out;
}

} // namespace

extern "C" void pc_ui_shell_open(const pc_ui_commands* commands, int remember)
{
    s.commands = *commands;
    s.visible = true;
    if (!remember) {
        s.tab = 0;
        std::memset(s.row, 0, sizeof s.row);
    }
    for (int t = 0; t < TAB_COUNT; ++t)
        clamp_row(t);
    if (!tab_has_rows(s.tab))
        s.tab = step_tab(s.tab, 1);
}

extern "C" void pc_ui_shell_close(void) { s.visible = false; }

extern "C" int pc_ui_shell_visible(void) { return s.visible; }

extern "C" int pc_ui_shell_tick(const pc_ui_input* in)
{
    if (!s.visible)
        return -1;
    if (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) {
        s.visible = false;
        return -1;
    }
    // tabs without rows (no command unlocked yet, e.g. Magick early on) are skipped
    if (in->pressed & PC_UI_TAB_NEXT)
        s.tab = step_tab(s.tab, 1);
    if (in->pressed & PC_UI_TAB_PREV)
        s.tab = step_tab(s.tab, -1);
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
    j += "\"}";
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
