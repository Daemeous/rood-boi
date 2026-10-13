# Modern Menus — Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** With `ui_menus modern`, Triangle opens a native tabbed menu shell (drawn with Dear ImGui draw lists over the game picture) that replaces the game's top-level command list; every original command stays reachable from the shell's tabs, and `ui_menus original` (the default) leaves the game unchanged.

**Architecture:** The game still opens and closes its menu; one `#ifdef ROOD_PC` hook in `func_800C97BC` (BATTLE `5BF94.c`) replaces the top-level list (`func_800C930C(0)` and its row rendering) with `pc_ui_menu_top()` from `pc/src/ui/menu_bridge.c` (battle builds). The bridge feeds the game's pad state into a pure C++ shell (`pc/src/ui/ui_shell.cpp`: tabs, rows, focus, results), and the shell's result uses the original protocol (0 stay, -1 close, n > 0 open original command n-1). Drawing (`ui_draw.cpp`) runs in `pc_overlay_frame` on ImGui's background draw list inside the game picture rect (`pc_gl_view`), styled by `pc/assets/ui/theme.json` (live reload). The design is in `pc/docs/Modern_Menus.md`.

**Tech Stack:** C (game side, gcc -m32/-m64 via `pc/tools/build_boot.py`), C++17 (shell, draw; Dear ImGui 1.93 vendored), Python RPC tests (`pc/tools/rood_rpc.py`, `pc/tests/run.py`), CMake probes (`pc/CMakeLists.txt`, ctest).

**Scope:** This is plan 1 of the modern menus (foundation). It ships a usable modern top level whose tab rows open the original screens. Plan 2 (Equipment: Gear + Quick swap, live model, parity tests, mouse) is written after this lands, because its equip path needs a parity spike against MENU3 (`_equipWeaponMenu`, `pc/src/weapon_slots.c begin_swap`).

**Ground rules (from the repo):**
- `src/` changes only under `#ifdef ROOD_PC`; `make commit-check` must print "All files match" (Docker image `rood-reverse-dev:local`; in a worktree see `pc/README.md` / the commit-check command in the project memory).
- Settings additions in their own block of `pc/src/project_settings.cpp` (the main session edits that file too).
- Never commit game data or anything derived from it (screenshots, dumps). Test output goes to `pc/build/...` or `--out`.
- Before code: this worktree's branch `claude/musing-haslett-8e55eb` is rebased on main 73bd22db (button prompts API `pc/src/button_prompts.h`, key/pad rebinding, deterministic test helpers). Rebase again on the latest main before merging.
- Always build with `--battle` (tests run the battle binary). Run suites at `-j 2`, one run at a time (thermal limit). Hint glyphs come from `pc_prompt_glyph` (main session); no prompt setting of our own.

---

## File structure

| File | Responsibility |
| --- | --- |
| `pc/src/ui/ui_shell.h` | C API between game side, shell, draw and RPC: input struct, command ids, `pc_ui_shell_*` functions |
| `pc/src/ui/ui_shell.cpp` | Pure navigation state: tabs, rows per tab, focus, enable/reason, result codes, JSON dump. No ImGui, no game headers (unit-testable) |
| `pc/src/ui/ui_theme.h/.cpp` | Theme tokens (colours, sizes, timings) with defaults, loaded from `pc/assets/ui/theme.json`, live reload |
| `pc/src/ui/ui_draw.cpp` | ImGui draw-list rendering of the shell (fonts, tabs, rows, hints) in the game rect |
| `pc/src/ui/menu_bridge.c` | Battle-build glue: reads the game's pad/menu state, calls the shell, maps results to the game's protocol |
| `pc/assets/ui/theme.json` | Default theme (our own values) |
| `src/BATTLE/BATTLE.PRG/5BF94.c` | `#ifdef ROOD_PC` hook in `func_800C930C`'s caller `func_800C97BC` |
| `pc/src/project_settings.cpp` | `ui_menus` setting (own block) |
| `pc/src/dev_rpc.c` | RPC `ui_menu {modern, dump}` |
| `pc/src/dev_overlay.cpp` | Call `pc_ui_draw_frame()` before `ImGui::Render()` |
| `pc/tools/build_boot.py` | Compile the new files |
| `pc/CMakeLists.txt`, `pc/tests/ui_shell_probe.cpp` | ctest for the shell logic and theme parsing |
| `pc/tests/ui_menus_smoke.py`, `pc/tests/run.py` | Game test: off = unchanged, on = shell opens, tabs, original screens, close |
| `pc/README.md`, `pc/docs/Modern_Menus.md` | Docs |

Original command indices (row order of `func_800C930C`, `assets/BATTLE/BATTLE.PRG/menuStrings.yaml`): 0 Magic, 1 Break Arts, 2 Battle Abilities, 3 Items, 4 Status, 5 Map, 6 Data, 7 Options, 8 Score, 9 Quick Manual. Enabled when `(vs_main_settings.menuFlags >> i) & 1`. Unselectable: Magic when `D_800F4EA0 & 0xB7`, Break Arts when `& 0x15F`, Items when `& 7`, Map when `vs_battle_getCurrentSceneId() == 0`.

Shell tabs → rows (each row opens an original command until its native screen exists):

| Tab | Rows (command) |
| --- | --- |
| Ashley | Status (4) |
| Equipment | Equip & items (3) |
| Items | Items (3) |
| Abilities | Battle abilities (2), Break arts (1) |
| Magick | Magick (0) |
| Leá Monde | Map (5) |
| System | Data (6), Options (7), Score (8), Quick manual (9), Menus: Modern/Original (setting row) |

---

### Task 1: The `ui_menus` setting and the shell API header

**Files:**
- Create: `pc/src/ui/ui_shell.h`
- Modify: `pc/src/project_settings.cpp` (own block after the gameplay options near line 34; entry after `I("qol_weapon_slots", ...)` near line 144)

- [ ] **Step 1: Write `pc/src/ui/ui_shell.h`**

```c
/* Modern menus (pc/docs/Modern_Menus.md): the shell between the game's menu
 * (menu_bridge.c, battle builds), the drawing (ui_draw.cpp) and the RPC. */
#ifndef PC_UI_SHELL_H
#define PC_UI_SHELL_H
#ifdef __cplusplus
extern "C" {
#endif

/* Settings key ui_menus: 0 the game's own menus (default), 1 modern. */
extern int pc_ui_menus;

/* Buttons in the shell's own order (bridge maps libetc PAD bits to these). */
enum {
    PC_UI_UP = 1 << 0, PC_UI_DOWN = 1 << 1, PC_UI_LEFT = 1 << 2, PC_UI_RIGHT = 1 << 3,
    PC_UI_CONFIRM = 1 << 4, PC_UI_BACK = 1 << 5, PC_UI_EXIT = 1 << 6, PC_UI_ALT = 1 << 7,
    PC_UI_TAB_PREV = 1 << 8, PC_UI_TAB_NEXT = 1 << 9, PC_UI_SUB_PREV = 1 << 10, PC_UI_SUB_NEXT = 1 << 11,
};
typedef struct {
    unsigned pressed; /* edges this frame */
    unsigned repeat;  /* pressed plus key repeat (lists) */
} pc_ui_input;

/* The game's original commands (top-level rows of func_800C930C). */
enum { PC_UI_CMD_COUNT = 10 };
typedef struct {
    unsigned enabled;      /* bit i: command i exists (menuFlags) */
    unsigned unselectable; /* bit i: shown but not allowed now */
} pc_ui_commands;

/* The game opened its top-level menu (cursor memory: remember=1). */
void pc_ui_shell_open(const pc_ui_commands* commands, int remember);
/* One game frame of the top-level menu. Returns 0 to stay, -1 to close the
 * menu, n > 0 to open original command n - 1 (the game's own protocol). */
int pc_ui_shell_tick(const pc_ui_input* input);
/* The shell is up (opened and not closed). */
int pc_ui_shell_visible(void);
/* The open shell as JSON (RPC ui_menu). */
void pc_ui_shell_json(char* out, unsigned size);
/* Drawing, from pc_overlay_frame (ImGui frame open). */
void pc_ui_draw_frame(float dt);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Add the setting in its own block of `pc/src/project_settings.cpp`**

After the `pc_qol_weapon_slots` definition (line ~34) add:

```cpp
/* Modern menus (pc/src/ui, pc/docs/Modern_Menus.md): 0 the game's own
 * menus, 1 the modern shell. Off by default until the screens are verified. */
int pc_ui_menus;
```

In `entries[]`, after `I("qol_weapon_slots", pc_qol_weapon_slots),` add:

```cpp
    I("ui_menus", pc_ui_menus),
```

(`pc_ui_menus` is inside the file's `extern "C"` block like `pc_qol_weapon_slots`; check that block's braces and put the definition inside it.)

- [ ] **Step 3: Build**

Run: `python3 pc/tools/build_boot.py --battle` (32-bit; tests use the battle binary `pc/build/boot/rood_battle_probe`; always pass `--battle`).
Expected: builds; `grep ui_menus pc/build/boot/report.txt` is not needed — just a clean build.

- [ ] **Step 4: Commit**

```bash
git add pc/src/ui/ui_shell.h pc/src/project_settings.cpp
git commit -m "ui: ui_menus setting and the modern menu shell API"
```

---

### Task 2: Shell navigation logic (pure C++) with a ctest probe

**Files:**
- Create: `pc/src/ui/ui_shell.cpp`
- Create: `pc/tests/ui_shell_probe.cpp`
- Modify: `pc/CMakeLists.txt` (after the fixture_probe block, line ~45)

- [ ] **Step 1: Write the failing probe `pc/tests/ui_shell_probe.cpp`**

```cpp
// The modern menu shell's navigation (pc/src/ui/ui_shell.cpp) without the game.
#include "ui/ui_shell.h"
#include <cstdio>
#include <cstring>

int pc_ui_menus = 1;
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static int tick(unsigned buttons)
{
    pc_ui_input in = {buttons, buttons};
    return pc_ui_shell_tick(&in);
}
static bool json_has(const char* needle)
{
    static char buf[16384];
    pc_ui_shell_json(buf, sizeof buf);
    return std::strstr(buf, needle) != nullptr;
}

int main()
{
    pc_ui_commands all = {0x3FF, 0};
    pc_ui_shell_open(&all, 0);
    CHECK(pc_ui_shell_visible());
    CHECK(json_has("\"tab\":\"ashley\""));
    CHECK(tick(0) == 0);
    // Ashley: Status is command 4 -> result 5
    CHECK(tick(PC_UI_CONFIRM) == 5);
    // the game shows Status; when it returns, the shell is opened again (remember = 1) on the same tab
    pc_ui_shell_open(&all, 1);
    CHECK(json_has("\"tab\":\"ashley\""));
    // R1 -> Equipment, L1 back, L1 wraps to System
    tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"equipment\""));
    tick(PC_UI_TAB_PREV);
    tick(PC_UI_TAB_PREV);
    CHECK(json_has("\"tab\":\"system\""));
    // System rows: Data, Options, Score, Quick manual, Menus; Down x1 = Options (7) -> 8
    tick(PC_UI_DOWN);
    CHECK(tick(PC_UI_CONFIRM) == 8);
    pc_ui_shell_open(&all, 1);
    // the Menus row switches the setting, the shell stays
    tick(PC_UI_DOWN); tick(PC_UI_DOWN); tick(PC_UI_DOWN);
    CHECK(json_has("\"row\":\"menus\""));
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(pc_ui_menus == 0);
    tick(PC_UI_CONFIRM);
    CHECK(pc_ui_menus == 1);
    // not remembered: back to Ashley
    pc_ui_shell_open(&all, 0);
    CHECK(json_has("\"tab\":\"ashley\""));
    // Magick unselectable: confirm stays and the dump carries the reason
    pc_ui_commands no_magic = {0x3FF, 1};
    pc_ui_shell_open(&no_magic, 0);
    for (int i = 0; i < 4; ++i) tick(PC_UI_TAB_NEXT); // Ashley Equipment Items Abilities Magick
    CHECK(json_has("\"tab\":\"magick\""));
    CHECK(tick(PC_UI_CONFIRM) == 0);
    CHECK(json_has("\"enabled\":false"));
    // a command the game has not unlocked (menuFlags bit clear) hides its row: Abilities without Break arts
    pc_ui_commands no_arts = {0x3FF & ~2u, 0};
    pc_ui_shell_open(&no_arts, 0);
    for (int i = 0; i < 3; ++i) tick(PC_UI_TAB_NEXT);
    CHECK(json_has("\"tab\":\"abilities\""));
    CHECK(!json_has("break_arts"));
    // Back and Exit close
    CHECK(tick(PC_UI_BACK) == -1);
    CHECK(!pc_ui_shell_visible());
    pc_ui_shell_open(&all, 0);
    CHECK(tick(PC_UI_EXIT) == -1);
    std::printf(failures ? "ui_shell_probe: %d failures\n" : "ui_shell_probe: ok\n", failures);
    return failures != 0;
}
```

Add to `pc/CMakeLists.txt` after the fixture_catalog test block:

```cmake
# Modern menus: the shell's navigation (pc/src/ui/ui_shell.cpp) without the game.
add_executable(ui_shell_probe tests/ui_shell_probe.cpp src/ui/ui_shell.cpp)
target_include_directories(ui_shell_probe PRIVATE src)
target_compile_features(ui_shell_probe PRIVATE cxx_std_17)
target_compile_options(ui_shell_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME ui_shell_probe COMMAND ui_shell_probe)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S pc -B pc/build && cmake --build pc/build --target ui_shell_probe`
Expected: FAIL (link error: `pc/src/ui/ui_shell.cpp` missing).

- [ ] **Step 3: Write `pc/src/ui/ui_shell.cpp`**

```cpp
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
    pc_ui_commands commands = {0, 0};
} s;

bool row_shown(const RowDef& r)
{
    return r.kind != ROW_COMMAND || ((s.commands.enabled >> r.command) & 1);
}
bool row_enabled(const RowDef& r)
{
    return r.kind != ROW_COMMAND || !((s.commands.unselectable >> r.command) & 1);
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
}

extern "C" int pc_ui_shell_visible(void) { return s.visible; }

extern "C" int pc_ui_shell_tick(const pc_ui_input* in)
{
    if (!s.visible)
        return -1;
    if (in->pressed & (PC_UI_BACK | PC_UI_EXIT)) {
        s.visible = false;
        return -1;
    }
    if (in->pressed & PC_UI_TAB_NEXT)
        s.tab = (s.tab + 1) % TAB_COUNT;
    if (in->pressed & PC_UI_TAB_PREV)
        s.tab = (s.tab + TAB_COUNT - 1) % TAB_COUNT;
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
    if (!row_enabled(r))
        return 0;
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
/* Row i of the current tab (shown rows only): label, help, value (or null), enabled. Returns 0 past the end. */
extern "C" int pc_ui_shell_row(int i, const char** label, const char** help, const char** value, int* enabled)
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
    return 1;
}
extern "C" int pc_ui_shell_focus(void) { return s.row[s.tab]; }
```

Add the draw-side accessors to `ui_shell.h` inside the `extern "C"` block:

```c
/* Read-only views for drawing (ui_draw.cpp). */
int pc_ui_shell_tab(void);
int pc_ui_shell_tab_count(void);
const char* pc_ui_shell_tab_title(int tab);
int pc_ui_shell_row(int i, const char** label, const char** help, const char** value, int* enabled);
int pc_ui_shell_focus(void);
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build pc/build --target ui_shell_probe && ctest --test-dir pc/build -R ui_shell_probe --output-on-failure`
Expected: `ui_shell_probe: ok`, test passed.

- [ ] **Step 5: Compile it into the game**

In `pc/tools/build_boot.py`, in the ImGui/C++ source list (after `root / "pc/src/ui_upscale_panel.cpp",`, line ~601) add:

```python
    root / "pc/src/ui/ui_shell.cpp",
```

The object name is `"imgui_" + source.stem`, so `imgui_ui_shell.o` (no clash). Add `"-I" + str(root / "pc/src")` to that compile command's include flags if `#include "ui/ui_shell.h"` does not resolve (the existing command passes `-I` for imgui and ImGuizmo only; check `cxx_flags` for `-Ipc/src` first).

Run: `python3 pc/tools/build_boot.py --battle`
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add pc/src/ui/ui_shell.cpp pc/src/ui/ui_shell.h pc/tests/ui_shell_probe.cpp pc/CMakeLists.txt pc/tools/build_boot.py
git commit -m "ui: modern menu shell navigation (tabs, rows, original-command results) with a ctest probe"
```

---

### Task 3: The game hook and the bridge

**Files:**
- Create: `pc/src/ui/menu_bridge.c`
- Modify: `src/BATTLE/BATTLE.PRG/5BF94.c` (`func_800C930C` mode != 0 branch, line ~2680; `func_800C97BC` state-0 branch, lines ~2813-2833)
- Modify: `pc/tools/build_boot.py` (battle C list, after `("pc/src/weapon_slots.c", "weapon_slots"),` line ~389)

- [ ] **Step 1: Write `pc/src/ui/menu_bridge.c`**

```c
/* Modern menus: the game's top-level menu (BATTLE func_800C97BC state 0)
 * handed to the shell (ui_shell.cpp) when ui_menus is 1. Battle builds. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/SLUS_010.40/main.h"
#include "ui_shell.h"
#include <libetc.h>

extern u_int D_800F4EA0; /* the menu's disable mask (func_800CABE0) */

static int shell_open; /* the shell owns the top level this time the menu is open */

static unsigned map_buttons(unsigned pad)
{
    unsigned b = 0;
    if (pad & PADLup) b |= PC_UI_UP;
    if (pad & PADLdown) b |= PC_UI_DOWN;
    if (pad & PADLleft) b |= PC_UI_LEFT;
    if (pad & PADLright) b |= PC_UI_RIGHT;
    if (pad & PADRright) b |= PC_UI_CONFIRM;  /* Circle */
    if (pad & PADRdown) b |= PC_UI_BACK;      /* Cross */
    if (pad & PADRup) b |= PC_UI_EXIT;        /* Triangle */
    if (pad & PADRleft) b |= PC_UI_ALT;       /* Square */
    if (pad & PADL1) b |= PC_UI_TAB_PREV;
    if (pad & PADR1) b |= PC_UI_TAB_NEXT;
    if (pad & PADL2) b |= PC_UI_SUB_PREV;
    if (pad & PADR2) b |= PC_UI_SUB_NEXT;
    return b;
}

static pc_ui_commands commands(void)
{
    pc_ui_commands c;
    u_int mask = D_800F4EA0;
    c.enabled = vs_main_settings.menuFlags & 0x3FF;
    c.unselectable = ((mask & 0xB7) ? 1u : 0) | ((mask & 0x15F) ? 2u : 0) | ((mask & 7) ? 8u : 0)
        | (vs_battle_getCurrentSceneId() == 0 ? 0x20u : 0);
    return c;
}

/* func_800C930C(mode != 0): the top level (re)opens. mode 2 = back from a
 * submenu (cursor memory applies). Returns 1 when the shell takes over. */
int pc_ui_menu_open(int mode)
{
    pc_ui_commands c;
    shell_open = pc_ui_menus != 0;
    if (!shell_open)
        return 0;
    c = commands();
    pc_ui_shell_open(&c, mode == 2);
    return 1;
}

/* func_800C930C(0) while the shell owns the top level: the game's protocol
 * (0 stay, -1 close, n > 0 open command n - 1). */
int pc_ui_menu_top(void)
{
    pc_ui_input in;
    int r;
    in.pressed = map_buttons(vs_main_buttonsPressed.all);
    in.repeat = map_buttons(vs_main_buttonRepeat) | in.pressed;
    r = pc_ui_shell_tick(&in);
    if (r > 0) {
        vs_battle_submenuStates[0] = r - 1; /* the game's cursor (Status etc. read it) */
        vs_battle_playMenuSelectSfx();
    } else if (r < 0) {
        vs_battle_playMenuLeaveSfx();
        shell_open = 0;
    }
    return r;
}

int pc_ui_menu_active(void) { return shell_open; }
```

Check the names against the headers before building: `vs_battle_submenuStates`, `vs_battle_playMenuSelectSfx`, `vs_battle_playMenuLeaveSfx`, `vs_battle_getCurrentSceneId` are used by `func_800C930C` in `5BF94.c` (declared in `5BF94.h`/`146C.h`); `PADL2`/`PADR2` in `include/psx/libetc.h`. If one is static or undeclared, add a file-scope `extern` prototype copied from its definition (file scope, not block scope: `import_types.py` must see it for the 64-bit build).

- [ ] **Step 2: Add the hook in `src/BATTLE/BATTLE.PRG/5BF94.c`**

In `func_800C930C`, at the start of the `if (mode) {` block (line ~2680):

```c
    if (mode) {
#ifdef ROOD_PC
        {
            extern int pc_ui_menu_open(int);
            if (pc_ui_menu_open(mode)) {
                D_800F4CC0 = 0;
                return 0;
            }
        }
#endif
```

In `func_800C97BC`, replace the state-0 handling (lines ~2813-2833) by wrapping it:

```c
        if (temp_s1 == 0) {
#ifdef ROOD_PC
            extern int pc_ui_menu_active(void);
            extern int pc_ui_menu_top(void);
            miscIndex = pc_ui_menu_active() ? pc_ui_menu_top() : func_800C930C(0);
#else
            miscIndex = func_800C930C(0);
#endif
```

and the row rendering:

```c
#ifdef ROOD_PC
        if (((var_s0 >= 0x1F) || (temp_s1 == 0)) && !pc_ui_menu_active()) {
#else
        if ((var_s0 >= 0x1F) || (temp_s1 == 0)) {
#endif
```

Declare the two `extern`s at file scope instead of block scope if the 64-bit import tool needs it (see the "block-scope extern" lesson in the project memory): put them under `#ifdef ROOD_PC` just above `func_800C930C`.

Note: when the shell closes (-1), `pc_ui_menu_active()` turns 0 in the same call, so the closing frames (`0x1F`) render the original rows' slide-out. If that flashes the old rows, keep a `closing` flag in the bridge that stays set until `vs_battle_menuState.currentMenu` is `0x3F`, and use it for the render guard. Verify in Task 6's test with a screenshot.

- [ ] **Step 3: Compile the bridge in battle builds**

In `pc/tools/build_boot.py` after `("pc/src/weapon_slots.c", "weapon_slots"),` add:

```python
        ("pc/src/ui/menu_bridge.c", "ui_menu_bridge"),
```

Run: `python3 pc/tools/build_boot.py --battle && python3 pc/tools/build_boot.py --battle --arch 64`
Expected: both build. Then `python3 pc/tools/decl_mismatch.py --review` reports HAZARD 0.

- [ ] **Step 4: commit-check**

Run (worktree form; `$MAIN` = the main checkout):
```bash
touch -d 2026-10-01 requirements.txt
docker run --rm -u $(id -u):$(id -g) -v $PWD:/work -v $MAIN:$MAIN -v $MAIN/tools/maspsx:/work/tools/maspsx rood-reverse-dev:local make commit-check
```
Expected: "All files match". (It runs `clean`; rebuild with build_boot.py afterwards.)

- [ ] **Step 5: Commit**

```bash
git add pc/src/ui/menu_bridge.c src/BATTLE/BATTLE.PRG/5BF94.c pc/tools/build_boot.py
git commit -m "ui: hand the game's top-level menu to the modern shell when ui_menus is on (ROOD_PC hook)"
```

---

### Task 4: Theme tokens from JSON (live reload)

**Files:**
- Create: `pc/src/ui/ui_theme.h`, `pc/src/ui/ui_theme.cpp`, `pc/assets/ui/theme.json`
- Modify: `pc/tests/ui_shell_probe.cpp`, `pc/CMakeLists.txt` (add `src/ui/ui_theme.cpp src/light_prefabs.cpp` to the probe)
- Modify: `pc/tools/build_boot.py` (C++ list)

- [ ] **Step 1: Write the failing test** (append to `main()` in `ui_shell_probe.cpp` before the summary; include `"ui/ui_theme.h"`)

```cpp
    {
        ui::Theme t;
        CHECK(t.accent == 0xFF3C36B5u); // default crimson #b5363c as ImU32 (ABGR)
        std::string err;
        CHECK(ui::theme_parse("{\"colors\":{\"accent\":\"#102030\"},\"sizes\":{\"tab\":17}}", t, &err));
        CHECK(t.accent == 0xFF302010u);
        CHECK(t.tab_size == 17.0f);
        CHECK(!ui::theme_parse("{\"colors\":{\"accent\":\"red\"}}", t, &err));
        CHECK(!err.empty());
    }
```

CMake: `add_executable(ui_shell_probe tests/ui_shell_probe.cpp src/ui/ui_shell.cpp src/ui/ui_theme.cpp src/light_prefabs.cpp)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build pc/build --target ui_shell_probe`
Expected: compile error (`ui/ui_theme.h` missing).

- [ ] **Step 3: Write `pc/src/ui/ui_theme.h`**

```cpp
// Modern menu theme: colours, type sizes and timings (pc/assets/ui/theme.json).
#pragma once
#include <string>

namespace ui {

struct Theme {
    // colours as ImU32 (0xAABBGGRR)
    unsigned ground = 0xF00E0F11u, panel = 0xFF141619u, rule = 0xFF24272Cu, rule2 = 0xFF31363Du;
    unsigned bone = 0xFFD6E4EBu, ash = 0xFF86929Bu, dim = 0xFF535C64u;
    unsigned accent = 0xFF3C36B5u, accent_soft = 0x293C36B5u, gold = 0xFF4EA1C9u;
    unsigned hp = 0xFF5AA76Au, mp = 0xFFC4824Du, risk = 0xFF3C4ACFu;
    // sizes in layout units (1280 x 720 or 960 x 720)
    float tab_size = 15, label_size = 11, name_size = 19, body_size = 14.5f, hint_size = 12;
    float margin_x = 56, top = 34, tab_height = 44, row_height = 46, rail_width = 448;
    // seconds
    float tab_slide = 0.24f, focus_grow = 0.16f, screen_fade = 0.2f;
    // font files (relative to the repository root)
    std::string font_name = "pc/third_party/fonts/Alegreya/Alegreya[wght].ttf";
    std::string font_label = "pc/third_party/fonts/AsapCondensed/AsapCondensed-Bold.ttf";
    std::string font_data = "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf";
};

// Parse theme JSON over `t` (missing keys keep their values). False on a syntax or value error.
bool theme_parse(const char* text, Theme& t, std::string* error);
// The live theme: pc/assets/ui/theme.json, reloaded when it changes (checked at most twice a second).
const Theme& theme();

} // namespace ui
```

Check the actual font file names: `ls pc/third_party/fonts/*/` and fix the three paths if they differ.

- [ ] **Step 4: Write `pc/src/ui/ui_theme.cpp`**

```cpp
#include "ui/ui_theme.h"
#include "light_prefabs.h"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <time.h>

namespace ui {
namespace {

bool parse_color(const std::string& s, unsigned* out)
{
    if (s.size() != 7 && s.size() != 9)
        return false;
    if (s[0] != '#')
        return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str() + 1, &end, 16);
    if (*end)
        return false;
    unsigned r, g, b, a = 255;
    if (s.size() == 7) {
        r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
    } else {
        r = (v >> 24) & 255, g = (v >> 16) & 255, b = (v >> 8) & 255, a = v & 255;
    }
    *out = (a << 24) | (b << 16) | (g << 8) | r;
    return true;
}

} // namespace

bool theme_parse(const char* text, Theme& t, std::string* error)
{
    prefab::Json root;
    if (!prefab::json_parse(text, root, error))
        return false;
    struct { const char* key; unsigned* v; } colors[] = {
        {"ground", &t.ground}, {"panel", &t.panel}, {"rule", &t.rule}, {"rule2", &t.rule2},
        {"bone", &t.bone}, {"ash", &t.ash}, {"dim", &t.dim}, {"accent", &t.accent},
        {"accent_soft", &t.accent_soft}, {"gold", &t.gold}, {"hp", &t.hp}, {"mp", &t.mp}, {"risk", &t.risk},
    };
    if (const prefab::Json* c = root.get("colors"))
        for (auto& e : colors)
            if (const prefab::Json* v = c->get(e.key)) {
                if (v->type != prefab::Json::String || !parse_color(v->string, e.v)) {
                    if (error)
                        *error = std::string("colors.") + e.key + ": expected \"#rrggbb\" or \"#rrggbbaa\"";
                    return false;
                }
            }
    struct { const char* key; float* v; } nums[] = {
        {"tab", &t.tab_size}, {"label", &t.label_size}, {"name", &t.name_size}, {"body", &t.body_size},
        {"hint", &t.hint_size}, {"margin_x", &t.margin_x}, {"top", &t.top}, {"tab_height", &t.tab_height},
        {"row_height", &t.row_height}, {"rail_width", &t.rail_width},
    };
    if (const prefab::Json* sz = root.get("sizes"))
        for (auto& e : nums)
            if (const prefab::Json* v = sz->get(e.key)) {
                if (v->type != prefab::Json::Number) {
                    if (error)
                        *error = std::string("sizes.") + e.key + ": expected a number";
                    return false;
                }
                *e.v = (float)v->number;
            }
    struct { const char* key; float* v; } times[] = {
        {"tab_slide", &t.tab_slide}, {"focus_grow", &t.focus_grow}, {"screen_fade", &t.screen_fade}};
    if (const prefab::Json* m = root.get("motion"))
        for (auto& e : times)
            if (const prefab::Json* v = m->get(e.key)) {
                if (v->type != prefab::Json::Number) {
                    if (error)
                        *error = std::string("motion.") + e.key + ": expected seconds";
                    return false;
                }
                *e.v = (float)v->number;
            }
    struct { const char* key; std::string* v; } fonts[] = {
        {"name", &t.font_name}, {"label", &t.font_label}, {"data", &t.font_data}};
    if (const prefab::Json* f = root.get("fonts"))
        for (auto& e : fonts)
            if (const prefab::Json* v = f->get(e.key))
                if (v->type == prefab::Json::String)
                    *e.v = v->string;
    return true;
}

const Theme& theme()
{
    static Theme current;
    static time_t mtime = -1;
    static double next_check;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = now.tv_sec + now.tv_nsec * 1e-9;
    if (t < next_check)
        return current;
    next_check = t + 0.5;
    const char* path = getenv("ROOD_UI_THEME");
    if (!path)
        path = "pc/assets/ui/theme.json";
    struct stat st;
    if (stat(path, &st) != 0 || st.st_mtime == mtime)
        return current;
    mtime = st.st_mtime;
    FILE* f = fopen(path, "rb");
    if (!f)
        return current;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    fclose(f);
    Theme next;
    std::string error;
    if (theme_parse(text.c_str(), next, &error))
        current = next;
    else
        fprintf(stderr, "[ui] %s: %s (theme kept)\n", path, error.c_str());
    return current;
}

} // namespace ui
```

- [ ] **Step 5: Write `pc/assets/ui/theme.json`** (the defaults, so editing the file is the way to tune)

```json
{
  "colors": {
    "ground": "#110f0ef0", "panel": "#191614", "rule": "#2c2724", "rule2": "#3d3631",
    "bone": "#ebe4d6", "ash": "#9b9286", "dim": "#645c53",
    "accent": "#b5363c", "accent_soft": "#b5363c29", "gold": "#c9a14e",
    "hp": "#6aa75a", "mp": "#4d82c4", "risk": "#cf4a3c"
  },
  "sizes": {
    "tab": 15, "label": 11, "name": 19, "body": 14.5, "hint": 12,
    "margin_x": 56, "top": 34, "tab_height": 44, "row_height": 46, "rail_width": 448
  },
  "motion": { "tab_slide": 0.24, "focus_grow": 0.16, "screen_fade": 0.2 },
  "fonts": {
    "name": "pc/third_party/fonts/Alegreya/Alegreya[wght].ttf",
    "label": "pc/third_party/fonts/AsapCondensed/AsapCondensed-Bold.ttf",
    "data": "pc/third_party/fonts/ChakraPetch/ChakraPetch-Bold.ttf"
  }
}
```

- [ ] **Step 6: Run the probe**

Run: `cmake --build pc/build --target ui_shell_probe && ctest --test-dir pc/build -R ui_shell_probe --output-on-failure`
Expected: `ui_shell_probe: ok`.

- [ ] **Step 7: Build into the game** — add `root / "pc/src/ui/ui_theme.cpp",` to the C++ list in `build_boot.py` (after `ui_shell.cpp`); run `python3 pc/tools/build_boot.py --battle`. Expected: clean build.

- [ ] **Step 8: Commit**

```bash
git add pc/src/ui/ui_theme.* pc/assets/ui/theme.json pc/tests/ui_shell_probe.cpp pc/CMakeLists.txt pc/tools/build_boot.py
git commit -m "ui: theme tokens from pc/assets/ui/theme.json with live reload"
```

---

### Task 5: Drawing the shell (fonts, tabs, rows, card, hints)

**Files:**
- Create: `pc/src/ui/ui_draw.cpp`
- Modify: `pc/src/dev_overlay.cpp` (call before `ImGui::Render();` in `pc_overlay_frame`, line ~1566), `pc/tools/build_boot.py` (C++ list)

- [ ] **Step 1: Write `pc/src/ui/ui_draw.cpp`**

```cpp
// Modern menu drawing: the shell (ui_shell.cpp) on ImGui's background draw
// list, inside the game picture (pc_gl_view), in 1280x720 / 960x720 layout units.
#include "ui/ui_shell.h"
#include "ui/ui_theme.h"
#include "button_prompts.h"
#include "imgui.h"
#include <cmath>
#include <string>

extern "C" int pc_gl_view[4];
extern "C" int pc_gte_widescreen;

namespace {

ImFont* f_name;
ImFont* f_label;
ImFont* f_data;
std::string loaded_name, loaded_label, loaded_data;

void load_fonts(const ui::Theme& t)
{
    if (loaded_name == t.font_name && loaded_label == t.font_label && loaded_data == t.font_data)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (!f_name || loaded_name != t.font_name)
        f_name = io.Fonts->AddFontFromFileTTF(t.font_name.c_str(), 0.0f, &cfg);
    if (!f_label || loaded_label != t.font_label)
        f_label = io.Fonts->AddFontFromFileTTF(t.font_label.c_str(), 0.0f, &cfg);
    if (!f_data || loaded_data != t.font_data)
        f_data = io.Fonts->AddFontFromFileTTF(t.font_data.c_str(), 0.0f, &cfg);
    loaded_name = t.font_name, loaded_label = t.font_label, loaded_data = t.font_data;
}

struct Frame {
    ImDrawList* dl;
    float ox, oy, k; // window = origin + layout * k
    float W;         // layout width (1280 or 960)
    ImVec2 p(float x, float y) const { return ImVec2(ox + x * k, oy + y * k); }
    float s(float v) const { return v * k; }
};

// Letter-spaced text (tracking in em); returns the advance in layout units.
float text(const Frame& f, ImFont* font, float size, float x, float y, unsigned col, const char* str,
    float tracking = 0, bool measure_only = false)
{
    if (!font)
        font = ImGui::GetFont();
    float px = f.s(size), adv = 0;
    for (const char* c = str; *c;) {
        unsigned cp;
        int n = ImTextCharFromUtf8(&cp, c, nullptr);
        if (n <= 0)
            break;
        char one[8] = {};
        for (int i = 0; i < n; ++i)
            one[i] = c[i];
        if (!measure_only)
            f.dl->AddText(font, px, f.p(x + adv, y), col, one);
        adv += font->CalcTextSizeA(px, 1e9f, 0, one).x / f.k + tracking * size;
        c += n;
    }
    return adv;
}

float approach(float cur, float target, float dt, float seconds)
{
    if (seconds <= 0)
        return target;
    float a = 1.0f - std::exp(-dt / (seconds / 3.0f));
    return cur + (target - cur) * a;
}

float ink_x, ink_w, focus_y;
int last_tab = -1;

} // namespace

extern "C" void pc_ui_draw_frame(float dt)
{
    if (!pc_ui_shell_visible())
        return;
    const ui::Theme& t = ui::theme();
    load_fonts(t);
    Frame f;
    f.dl = ImGui::GetBackgroundDrawList();
    f.W = pc_gte_widescreen ? 1280.0f : 960.0f;
    f.k = pc_gl_view[2] / f.W;
    f.ox = (float)pc_gl_view[0];
    f.oy = (float)pc_gl_view[1] + (pc_gl_view[3] - 720.0f * f.k) * 0.5f;

    // backdrop over the game picture
    f.dl->AddRectFilled(ImVec2((float)pc_gl_view[0], (float)pc_gl_view[1]),
        ImVec2((float)(pc_gl_view[0] + pc_gl_view[2]), (float)(pc_gl_view[1] + pc_gl_view[3])), t.ground);

    // tabs
    float left = t.margin_x + 44, right = f.W - t.margin_x - 44, y = t.top + 13;
    int n = pc_ui_shell_tab_count(), cur = pc_ui_shell_tab();
    float widths[16], total = 0;
    for (int i = 0; i < n; ++i)
        total += widths[i] = text(f, f_label, t.tab_size, 0, 0, 0, pc_ui_shell_tab_title(i), 0.3f, true);
    float gap = n > 1 ? ((right - left) - total) / (n - 1) : 0, x = left;
    for (int i = 0; i < n; ++i) {
        std::string up = pc_ui_shell_tab_title(i);
        for (char& c : up)
            if (c >= 'a' && c <= 'z')
                c -= 32;
        text(f, f_label, t.tab_size, x, y, i == cur ? t.bone : t.dim, up.c_str(), 0.3f);
        if (i == cur) {
            if (last_tab < 0)
                ink_x = x, ink_w = widths[i];
            ink_x = approach(ink_x, x, dt, t.tab_slide);
            ink_w = approach(ink_w, widths[i], dt, t.tab_slide);
        }
        x += widths[i] + gap;
    }
    float base = t.top + t.tab_height;
    f.dl->AddLine(f.p(t.margin_x, base), f.p(f.W - t.margin_x, base), t.rule, f.s(1));
    f.dl->AddRectFilled(f.p(ink_x, base - 1), f.p(ink_x + ink_w, base + 1), t.accent);
    text(f, f_data, 10, t.margin_x, y + 3, t.ash, "L1");
    text(f, f_data, 10, f.W - t.margin_x - 14, y + 3, t.ash, "R1");

    // rows (left rail) and the focused row's card (right)
    float top = base + 22, rail = t.rail_width;
    int focus = pc_ui_shell_focus();
    if (cur != last_tab)
        focus_y = top + focus * t.row_height;
    focus_y = approach(focus_y, top + focus * t.row_height, dt, t.focus_grow);
    f.dl->AddRectFilledMultiColor(f.p(t.margin_x, focus_y), f.p(t.margin_x + rail, focus_y + t.row_height),
        t.accent_soft, t.accent_soft & 0x00FFFFFFu, t.accent_soft & 0x00FFFFFFu, t.accent_soft);
    f.dl->AddRectFilled(f.p(t.margin_x, focus_y + 8), f.p(t.margin_x + 2, focus_y + t.row_height - 8), t.accent);
    const char *label, *help, *value, *focus_label = "", *focus_help = "";
    int enabled;
    for (int i = 0; pc_ui_shell_row(i, &label, &help, &value, &enabled); ++i) {
        float ry = top + i * t.row_height;
        text(f, f_name, t.name_size, t.margin_x + 16, ry + (t.row_height - t.name_size) * 0.5f - 2,
            enabled ? t.bone : t.dim, label);
        if (value)
            text(f, f_data, 13, t.margin_x + rail - 90, ry + (t.row_height - 13) * 0.5f, t.bone, value);
        f.dl->AddLine(f.p(t.margin_x, ry + t.row_height), f.p(t.margin_x + rail, ry + t.row_height), t.rule, f.s(1));
        if (i == focus)
            focus_label = label, focus_help = help;
    }
    float cw = 396, cx = f.W - t.margin_x - cw;
    if (f.W < 1100)
        cw = 344, cx = f.W - t.margin_x - cw;
    f.dl->AddRectFilled(f.p(cx, top), f.p(cx + cw, top + 150), t.panel);
    f.dl->AddRect(f.p(cx, top), f.p(cx + cw, top + 150), t.rule, 0, 0, f.s(1));
    text(f, f_name, 25, cx + 20, top + 18, t.bone, focus_label);
    f.dl->AddText(f_name, f.s(t.body_size), f.p(cx + 20, top + 58), t.ash, focus_help, nullptr, f.s(cw - 40));

    // hints (bottom right): the main session's prompt icons (button_prompts.h), our own verbs
    struct Hint { int button; const char* verb; } hints[] = {
        {PC_PROMPT_CIRCLE, "SELECT"}, {PC_PROMPT_CROSS, "BACK"}, {PC_PROMPT_TRIANGLE, "EXIT MENU"}};
    float hx = f.W - t.margin_x, hy = 720 - 40, icon = t.hint_size * 1.6f;
    for (int i = (int)(sizeof hints / sizeof hints[0]) - 1; i >= 0; --i) {
        float vw = text(f, f_label, t.hint_size, 0, 0, 0, hints[i].verb, 0.22f, true);
        hx -= vw;
        text(f, f_label, t.hint_size, hx, hy, t.ash, hints[i].verb, 0.22f);
        pc_prompt_image g = pc_prompt_glyph(hints[i].button, f.s(icon));
        float gw = g.texture ? g.w / f.k : 0;
        hx -= gw + 8;
        if (g.texture)
            f.dl->AddImage(ImTextureRef((ImTextureData*)g.texture), f.p(hx, hy - (icon - t.hint_size) * 0.5f),
                f.p(hx + gw, hy - (icon - t.hint_size) * 0.5f + g.h / f.k), ImVec2(g.uv0[0], g.uv0[1]),
                ImVec2(g.uv1[0], g.uv1[1]));
        hx -= 26;
    }
    last_tab = cur;
}
```

- [ ] **Step 2: Call it from `pc_overlay_frame`** in `pc/src/dev_overlay.cpp`, just before `ImGui::Render();` (line ~1566):

```cpp
    pc_ui_draw_frame((float)dt); /* modern menus (pc/src/ui) over the game picture */
```

and near the other includes at the top: `#include "ui/ui_shell.h"`.

The shell draws even while the editor is open (the game view is inset; `pc_gl_view` already follows it).

- [ ] **Step 3: Build**

Add `root / "pc/src/ui/ui_draw.cpp",` to the C++ list in `build_boot.py`. Run `python3 pc/tools/build_boot.py --battle && python3 pc/tools/build_boot.py --battle --arch 64`.
Expected: clean builds.

- [ ] **Step 4: Look at it** (local only; never commit the image)

```bash
ROOD_SETTINGS=/dev/null python3 - <<'EOF'
import sys; sys.path.insert(0, "pc/tools")
from rood_rpc import Game
# continue from a copy of the card into the save room, then:
EOF
```
Use the Task 6 test's `launch()` helper with `--out /tmp/...` to get `present_screenshot` PNGs of the shell at 4:3 and 16:9; inspect them for overlap/clipping.

- [ ] **Step 5: Commit**

```bash
git add pc/src/ui/ui_draw.cpp pc/src/dev_overlay.cpp pc/tools/build_boot.py
git commit -m "ui: draw the modern menu shell (tabs, rows, focus card, hints) with the open fonts"
```

---

### Task 6: RPC `ui_menu` and the game test

**Files:**
- Modify: `pc/src/dev_rpc.c` (method list comment at the top; handler next to `weapon_slots`, line ~704; weak externs near line 329)
- Create: `pc/tests/ui_menus_smoke.py`
- Modify: `pc/tests/run.py` (next to `weapon_slots_smoke`)

- [ ] **Step 1: RPC handler** in `dev_rpc.c`:

externs near line 329:
```c
extern int pc_ui_menus __attribute__((weak)); /* project_settings.cpp */
extern void pc_ui_shell_json(char* out, unsigned size) __attribute__((weak)); /* ui/ui_shell.cpp */
```

handler after the `weapon_slots` branch:
```c
    } else if (!strcmp(method, "ui_menu")) {
        /* Modern menus (pc/src/ui): {modern} sets ui_menus; the shell's state. */
        int flag;
        if (!pc_ui_shell_json || !&pc_ui_menus) {
            fail(fd, id, -32000, "ui_menu needs the overlay build");
            return;
        }
        if (params && bool_value(member(params, "modern"), &flag))
            pc_ui_menus = flag;
        pc_ui_shell_json(result, sizeof result);
        reply(fd, id, result);
```

and document it in the method list comment at the top of the file:
```c
 *   ui_menu {modern}             modern menus (pc/src/ui): sets ui_menus,
 *                                returns the shell (visible, tab, rows, row)
```

- [ ] **Step 2: Write `pc/tests/ui_menus_smoke.py`**

```python
#!/usr/bin/env python3
"""Modern menus foundation (pc/src/ui, pc/docs/Modern_Menus.md).

    python3 pc/tests/ui_menus_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one), in the save room:
  - ui_menus off: Triangle opens the game's own menu (currentMenu 0), the shell
    stays hidden; Triangle closes it
  - on: Triangle opens the shell on the Ashley tab; R1/L1 move between tabs
  - Circle on Ashley > Status opens the original Status (MENU4); Cross returns to
    the shell on the same tab; Triangle closes the menu (currentMenu 0x3F) and
    the field runs again
  - System > Menus switches ui_menus off from inside the shell."""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "ui_menus"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
    os.environ.pop("ROOD_SETTINGS", None)
    menu_state = symbols()["vs_battle_menuState"]
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    try:
        game.wait_title()
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        if (state["zone"], state["room"]) != (9, 17):
            print(f"FAIL: not in the save room after Continue: {state}")
            return 1

        def current_menu():
            return game.read(menu_state, 1)[0]

        def shell():
            return game.call("ui_menu")

        def shot(name):
            if args.out:
                game.present_screenshot(out / f"{name}.png")

        # off: the game's own menu
        check(shell()["menus"] == "original", "ui_menus is off by default")
        game.press("triangle", vsyncs=3, settle=40)
        check(current_menu() == 0, "off: Triangle opens the game's top-level menu")
        check(not shell()["visible"], "off: the shell stays hidden")
        game.press("triangle", vsyncs=3, settle=60)
        check(current_menu() == CLOSED, "off: Triangle closes it")

        # on: the shell
        game.call("ui_menu", modern=True)
        game.press("triangle", vsyncs=3, settle=40)
        s = shell()
        check(s["visible"] and s["tab"] == "ashley", f"on: the shell opens on Ashley ({s})")
        shot("ashley")
        game.press("r1", vsyncs=3, settle=10)
        check(shell()["tab"] == "equipment", "R1: Equipment")
        game.press("l1", vsyncs=3, settle=10)
        check(shell()["tab"] == "ashley", "L1: Ashley")

        # Ashley > Status = the original Status (command 4 -> currentMenu 5 | 0x40)
        game.press("circle", vsyncs=3, settle=120)
        check(current_menu() == (5 | 0x40), f"Circle opens the original Status ({current_menu():#x})")
        check(not shell()["visible"], "the shell hides while Status shows")
        shot("status")
        game.press("cross", vsyncs=3, settle=120)
        s = shell()
        check(current_menu() == 0 and s["visible"] and s["tab"] == "ashley",
              f"Cross returns to the shell on Ashley ({current_menu():#x}, {s})")

        # System > Menus switches ui_menus off from inside the shell
        game.press("l1", vsyncs=3, settle=10)
        check(shell()["tab"] == "system", "L1 wraps to System")
        for _ in range(4):
            game.press("down", vsyncs=3, settle=6)
        check(shell()["row"] == "menus", "the Menus row")
        game.press("circle", vsyncs=3, settle=10)
        check(shell()["menus"] == "original", "Circle on Menus: original from the next open")
        game.call("ui_menu", modern=True)

        # Triangle closes the menu; the field runs
        game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED, "Triangle closes the menu")
        check(not shell()["visible"], "the shell is hidden after closing")
        shot("closed")
    finally:
        game.close()
    print("ui_menus_smoke:", "FAIL" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

Before running, check in `rood_rpc.py`: the button names (`"r1"`, `"l1"`, `"triangle"`, `"circle"`, `"cross"`, `"down"`) and `game.read(address, size)` returning bytes; `symbols()` resolving `vs_battle_menuState` (it reads `nm` of the battle binary). Adjust names to what `press()` accepts.

- [ ] **Step 3: Register in `pc/tests/run.py`** next to `weapon_slots_smoke`:

```python
        *([Suite("ui_menus_smoke", [PY, str(TESTS / "ui_menus_smoke.py"), "--card", str(args.save)], 300,
                 note="modern menus: off = the game's menu; shell tabs, original Status and back, "
                      "System > Menus, close")]
          if args.save.exists() else []),
```

- [ ] **Step 4: Build and run**

Run: `python3 pc/tools/build_boot.py --battle && python3 pc/tests/ui_menus_smoke.py --out pc/build/ui_menus_out`
Expected: every line `ok:`, final `ui_menus_smoke: ok`. Look at `ashley.png`, `status.png`, `closed.png` (local only).

If the original rows flash while closing, implement the `closing` flag from Task 3 Step 2's note, rebuild, rerun.

- [ ] **Step 5: Run the affected suites**

Run: `python3 pc/tests/run.py ui_menus_smoke weapon_slots_smoke save_menu_smoke widescreen_smoke ui_text_smoke ctest`
Expected: all pass (the option is off by default, so the others see no change).

- [ ] **Step 6: Commit**

```bash
git add pc/src/dev_rpc.c pc/tests/ui_menus_smoke.py pc/tests/run.py
git commit -m "ui: RPC ui_menu and ui_menus_smoke (off unchanged; shell tabs, original Status round trip, close)"
```

---

### Task 7: 16:9, editor and the 64-bit/Vulkan path

**Files:**
- Modify: `pc/tests/ui_menus_smoke.py`

- [ ] **Step 1: Add a 16:9 pass to the test** (after the 4:3 checks, before closing): set `game.call("graphics", widescreen=True)`, open the shell, `shot("ashley_169")`, check `shell()["visible"]`, close, `game.call("graphics", widescreen=False)`. (Check the `graphics` RPC parameter name in `dev_rpc.c`.)

- [ ] **Step 2: Run on Vulkan 64-bit**

Run: `python3 pc/tools/build_boot.py --battle --arch 64 && python3 pc/tests/run.py --arch 64 --renderer vulkan ui_menus_smoke`
Expected: pass. Inspect the 16:9 screenshot: tabs span the 16:9 width, rows and card do not overlap.

- [ ] **Step 3: Commit**

```bash
git add pc/tests/ui_menus_smoke.py
git commit -m "ui_menus_smoke: 16:9 pass"
```

---

### Task 8: Docs and the full suite

**Files:**
- Modify: `pc/README.md` (new "Modern menus" section after the weapon slots section, line ~175; RPC table row for `ui_menu`; test table row)
- Modify: `pc/docs/Modern_Menus.md` (Status line)

- [ ] **Step 1: README section**

```markdown
## Modern menus (in progress)

`ui_menus 1` (settings, RPC `ui_menu {modern}`) replaces the game's top-level
command list with a tabbed shell drawn by the PC runtime (`pc/src/ui`,
design in `pc/docs/Modern_Menus.md`): L1/R1 tabs, Up/Down rows, Circle opens,
Cross/Triangle close. Every row still opens the game's own screen for now;
native screens replace them one by one. System > Menus switches back to the
original menus (from the next open). Theme: `pc/assets/ui/theme.json`
(colours, sizes, timings, fonts; reloaded live; `ROOD_UI_THEME` overrides the
path). Off by default.
```

Add `ui_menu` to the RPC table and `ui_menus_smoke` / `ui_shell_probe` to the test table, matching their formats.

- [ ] **Step 2: Update the status line** in `pc/docs/Modern_Menus.md`: "Status: foundation landed (shell, bridge, theme, drawing, tests); Equipment next (plan 2)."

- [ ] **Step 3: Full suite, both architectures**

Run: `python3 pc/tests/run.py -j 2` and then `python3 pc/tests/run.py --arch 64 --renderer vulkan -j 2` (one run at a time: the machine is thermally limited)
Expected: same pass/fail set as main before this branch (record main's result first with the same commands on main if any suite fails).

- [ ] **Step 4: commit-check again** (Task 3 Step 4 command). Expected: "All files match".

- [ ] **Step 5: Commit**

```bash
git add pc/README.md pc/docs/Modern_Menus.md
git commit -m "docs: modern menus foundation"
```

Then ask the user to playtest (`pc/run-game.sh`, F1 > settings or `ui_menus 1` in `pc/user/settings.cfg`) before merging to main.
