# Fast Travel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Fast travel between Circles of Magic for real play: the game's own Teleportation (MENU0 `_teleportMenu`, BATTLE `func_800735F8` case 10, `func_8007C694`), offered from the first Circle once the prologue is over (`introState >= 3`, setting `qol_fast_travel`, on by default), free by default before the game's own unlock (`qol_fast_travel_cost` free / flat / scaled / game), not counted as magic while the port's unlock is the one in effect, and a Travel button (default pad: Select on a Circle; keyboard unbound) that opens the destination list directly, with a prompt on the Circle. When the game itself unlocks Teleportation (`unkB5 != 0`) everything is the game's. Works with `ui_menus` off and on.

**Architecture:** The rules are plain C with no game headers (`pc/src/fast_travel_rules.c`, probe-tested): whose unlock is in effect, the cost, the menu mask, the menu bits, the port's refusals, the Travel button's guard, MENU0's packing, the RPC JSON. `pc/src/fast_travel.c` (battle builds, game headers) fills the rules' state from the game and is what the src hooks call: MENU0 15AC.c reads `pc_fast_travel_unlock()` where it read `unkB5`, applies `pc_fast_travel_cost()` at the compare / display / packing (the cost table and its `== 15` "current circle" sentinel stay untouched), takes the Travel button's shortcut id straight to `_teleportMenu`; BATTLE 5BF94.c `func_800C930C` ORs `pc_fast_travel_menu_bits()` into its local copy of `menuFlags` (shown, never stored); BATTLE 146C.c case 10 asks `pc_fast_travel_begin()` whether to set `magicUsed`. The Travel button is a binding slot beside the 16 PS1 buttons (`boot_controller.c`), polled from the existing `pc_room_warp_poll` field hook. `room_warp.c` snapshots and restores `savePointStates` around Room Browser warp chains. The modern Magick > Teleport page (`magick_bridge.c`) and the shell's commands (`menu_bridge.c`) use the same predicate, cost, mask and bits.

**Tech Stack:** C (game units, the decomp's headers; pure units with no game headers), C++17 (settings, Dear ImGui overlay), SDL3 bindings, Python 3 headless RPC tests (`pc/tests/run.py`, `rood_rpc.py`, `checkpoints.launch_at`, `card_fixtures.card_guard`), CTest probes (`pc/CMakeLists.txt`), `make commit-check` in Docker for the src hooks.

---

## Ground rules for every task

- `$MAIN` is the main checkout: read only. Never build, test or write there (nor in the verify checkout). Work only in the worktree made in Task 1.
- Heavy work (every build and every test run) runs under `flock "$ROOD_HEAVY_LOCK" <command>`, where `ROOD_HEAVY_LOCK` is the shared lock file the session gives you. Tests at `-j 2`. No step creates artificial CPU load; a test waits only with `game.run(n)` (vsyncs).
- Card safety: the game tests run inside `card_fixtures.card_guard` with the pinned card `pc/build/test-card1.mcd` (or temp copies). Never touch `pc/save`, `pc/user`, `pc/lights`. Decoding a card read-only is fine.
- `src/` changes only as small `#ifdef ROOD_PC` blocks (or identity macros on PS1), real logic in `pc/src`. After every src change: the commit-check in Step "commit-check" below, and "All files match" before committing.
- Settings are tunable: `project_settings.cpp` entries, the Gameplay page (`pc/src/ui/ui_settings.cpp`, with `pc/tests/ui_settings_probe.cpp` / `ui_settings_stubs.cpp` kept in sync), the dev overlay's Gameplay panel, RPC.
- Never commit game data or game-derived assets (no extracted strings, no scan dumps). Committed files never mention other game ports by name and never contain absolute home-directory or temp-directory paths.
- The port never writes `unkB5`, `menuFlags`, `savePointStates` (except restoring its own snapshot of them around a Room Browser warp) or `magicUsed`.
- Commit often; every message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Don't merge or push. Kill processes by PID only; bound every wait; leave no background processes.
- Windows: `windows.h` is force-included on MinGW, so no new identifier may be a Windows macro or typedef (`near`, `far`, `small`, `interface`, `DELETE`). New names are prefixed (`pc_ft_*`, `PC_FT_*`, `PC_BIND_*`).

**commit-check** (run from the worktree root after any `src/` change):

```bash
touch -d 2026-10-01 requirements.txt; touch tools/.sysdeps
flock "$ROOD_HEAVY_LOCK" docker run --rm -u $(id -u):$(id -g) -v $PWD:/work -v $MAIN:$MAIN \
    -v $MAIN/tools/maspsx:/work/tools/maspsx -w /work rood-reverse-dev:local make commit-check
rm -rf build && cp -a $MAIN/build build
git checkout requirements.txt tools/.sysdeps 2>/dev/null || true
```

Expected: the make output ends with `All files match`. Never run docker without `-u`.

## Verified code map (read in `$MAIN`, read only)

- MENU0 `src/MENU/MENU0.PRG/15AC.c`:
  - `_teleportMenu` (22-146): rows are every `i` with `savePointStates[i] != 0`; `teleportCosts[rowCount] = _getTeleportCost(i)` (59); `rowTypes = (state == 2) | (currentMp < teleportCosts[row])` (62-63); the row with cost 15 is the current circle and sets `nearestSavePoint` (64-67; without one it is uninitialised and written into `cursorMemories[22/23]`); `if (D_800F4EA0 & 0xB7)` greys every row (77); display `i = teleportCosts[vs_mainMenu_getConfirmedRow()]; _setMPCostDirect(i, currentMp < i)` (111-112); `selectedRow == -1` clears to item 0 and returns -1 (102-104); the packing from `i = _savePointData[a2] + 1` with the cost at bit 19 (119-136).
  - `vs_menu0_exec` `handleShortcut` (195-222): `i = vs_battle_shortcutInvoked; if (i != 0) { vs_battle_setMenuItem(i + 9, ..., _magicStrings[_magicStrings[(i * 3) - 3]]) ...` indexes the magic strings by the shortcut **before** its switch, so a new shortcut id must branch before that line.
  - `initSubmenu` (224-276): the Teleport row exists only `if (vs_main_stateFlags.unkB5 != 0)` (246); its description and `rowTypes[i++] = ((j == 48) || (unkB5 == 2) || (func_8008A4FC() == 0))` (255-270).
  - `teleportMenu` state (452-466): `i > 0` → `D_800F4E98.unk2 = 0xA; executeAbility.s32 = i - 1; *state = exit`; `i == -2` → exit; else back to `initSubmenu`.
  - MENU0 states: none 0, handleShortcut 3, initSubmenu 4, handleSubmenu 5, ... teleportMenu 14, exitToMainMenu 15, executeMagic 16, exit 17. `vs_battle_submenuStates[1]` holds it (currentMenu 1).
- MENU0 `84.c`: `_savePointData[48]` (102, 35 used), `_getTeleportCost` (117-150): 15 + 4 per index between this room's point (`(_savePointData[i] & 0xFFFF) == vs_battle_getCurrentRoomId()`, 48 when none) and the target.
- BATTLE `146C.c`:
  - `func_800735F8` case 10 (4953-4966): effect, `vs_battle_setStateFlag(0xB7, 1)`, `func_8007C694(room, x, z, 0, 8)`, MP minus `unk0_19` if MP >= it, `func_8009E070(0, NULL, 7)`; then `func_8006F5CC(); func_800735CC(arg0);`.
  - Field case 1 (7602-7650): `status & 0x2000` and the event trigger `func_800BEC58(0x20, ...)` first, then the existing ROOD_PC `pc_room_warp_poll()` (7610-7619; returning 1 breaks out as after a door), then doors, then Triangle (`func_800CACD0(0, 0)` + `func_8007357C(0, 0)` when `unk954 == 0 && !(status & 0x4000)`).
  - The L2 shortcuts (case 6, 7790-7816): `vs_battle_validateShortcutSelection(n)` = `func_800CACD0(menuState, 0); vs_battle_shortcutInvoked = n; D_800F4FDB = 1;`, then `func_8007357C(0, 0)`.
  - Select (`vs_main_buttonsPressed.all & 0x100`) toggles the camera's near / far zoom in `func_8006E158` (2872-2894), called from the camera update **after** the field switch.
  - `func_8008A4FC` (15673-15706): the Circle's id (`_mpdRoomSection9` type 16, `unkA`) under Ashley, 0 otherwise. `func_80089DC0` calls `pc_room_warp_room_loaded()` (15507-15512) once a room's sections are loaded.
- BATTLE `5BF94.c`: `func_800C930C` reads `u_int enabled = vs_main_settings.menuFlags` into a local (2706) and has a ROOD_PC block at 2710; `items[0].unselectable = (mask & 0xB7) != 0` (2756). `func_800CACD0` (3497) recomputes `menuFlags` (bit 0 when a spell is learned or `unkB5 != 0`) and **stores** it (3543): it reaches the card through MENU7 (260.c:746). `func_800CABE0` (3456) is pure: 2 carrying, 4 hanging, 8 sheathed, 0x20 MP 0, 0x40 HP 0, 0x80 statuses, 0x100 ward.
- MENU7 `260.c`: Save is enabled when `func_8008A4FC() != 0 && unkA7 == 0` (4038-4039); `stats.saveLocation = D_800F4E6B` (737); `stateFlags` (0x440 bytes) at file offset 0x200, then `actionsLearned` (32), `mapStatus` (0x48), `settings` at 0x6A8 (`menuFlags` at +2), ... `unk1778` = the room's door record (742-750).
- `vs_main_flags_t` offsets: `gameOver` 0xA4, `unkA7` 0xA7, `introState` 0xAB, `unkB5` 0xB5, `magicUsed` 0xB7, `puzzleMode` 0x11C (the game option, on by default: not a "puzzle running" flag), `puzzleModeDisabled` 0x11D (a puzzle is under way), `mapPaling` 0x135, `savePointStates` 0x156.
- Port: `pc/src/room_warp.c` (field poll, room hooks; snapshots only `vs_main_mapStatus` per hop), `pc/src/room_catalog.c` (the warp's state, `warped` per chain origin, the disc catalog; MPD section lengths in order, door section index 3, collision 1, section 9 index 8), `pc/src/ui/magick_bridge.c` (copies MENU0's tables, reads `unkB5` at 335/342/530), `pc/src/ui/menu_bridge.c` (`commands()` 312-320 reads `menuFlags`; `deliver()` greys Magic with `mask & 0xB7`), `pc/src/boot_controller.c` (16 bindings, `bind_key_<button>` / `bind_pad_<button>`).

## Decisions taken where the design leaves room

1. **Whose unlock.** `pc_ft_whose`: `unkB5 != 0` → the game's (its value is what MENU0 reads, 2 included); else `qol_fast_travel && introState >= 3 && !puzzleModeDisabled` → the port's (MENU0 reads 1); else off (MENU0 reads 0). Time attacks need no rule of their own: a boss fight sets `unkA7` ("Cannot save during battle"), which the port refuses (rule R3).
2. **The port's refusals grey the Teleportation row** (one more `||` term in MENU0's `rowTypes`, only while the port's unlock is in effect): Room Browser warped or a warp under way (R7), `unkA7` (R3), and this room's Circle not recorded (`savePointStates[here] == 0`, which would leave MENU0's `nearestSavePoint` uninitialised: R1). The row keeps the game's own description.
3. **Cost.** Free / flat (`qol_fast_travel_flat`, default 15) / scaled (`qol_fast_travel_scale` x the game's cost, default 0.5, rounded) / game, clamped to the packed field's 12 bits; only while the port's unlock is in effect. Applied at MENU0's compare, display and packing; `_getTeleportCost` and the `== 15` sentinel untouched. Free mode drops the MP-0 bit (0x20) from the mask on a Circle, in MENU0's list and on the top level's Magic command.
4. **Magic command.** `pc_fast_travel_menu_bits()` is 1 only while the port's unlock is in effect **and Ashley stands on a Circle** (elsewhere every Magic row would be greyed anyway, and the top level keeps the game's layout off the Circles).
5. **magicUsed.** `pc_fast_travel_begin` returns `unkB5 != 0`: set when the game unlocked Teleportation, not otherwise. Decided inside case 10, synchronously.
6. **The Travel button opens MENU0's own list in both menu modes**: the modern shell takes only the top level (`pc_ui_menu_open`: `vs_battle_shortcutInvoked == 0`), so the shortcut path is MENU0's look with `ui_menus` on too. The shell's own Magick > Teleport page is the modern route.
7. **The Travel button's binding is a 17th binding slot** (`PC_BIND_TRAVEL`): `bind_key_travel` (default none) and `bind_pad_travel` (default `back`, the element PS1 Select has by default). It never swaps with a PS1 button on the Bindings page: sharing with Select is the point. When it fires, the PS1 buttons bound to the same key or element are taken out of that frame's `vs_main_buttonsPressed` (so Select does not also toggle the camera zoom). When the button would not open the list (off a Circle, travel off, refused), nothing happens: no sound, Select does what it always does.
8. **The Travel button's guard** (`pc_ft_shortcut`, the prompt shows exactly when it passes): the unlock (port's or game's) is 1, Ashley is on a Circle, no event / script room change / transition / game over, Ashley free (`unk954 == 0`, not falling, L2 and R2 not held: they are the weapon slots' and the shortcuts' chords), the port's refusals (port's unlock only), this room's Circle recorded, and at least one other destination usable.
9. **The original list still offers the current Circle** (as the stock game does once Teleportation is learned); Task 8 proves choosing it returns to the field without a hang.
10. **R5 / RPC "go" dropped.** No RPC hand-off of its own: tests drive the real paths (the original list by pad, the shell's Magick > Teleport, the Travel button through `fast_travel {press}`, which is the bound button's press).
11. **The script scan (R9).** `pc/tools/fast_travel_scan.py` walks every room and event script op by op with the interpreter's op lengths. A read-only trial of that walk on the data (901 scripts, all walked to their end) found: every Circle's own room script sets its `savePointStates` entry to 1 (all 35), the only `unkB5` write is 0057.EVT "set 1" (the Lich), no script tests `unkB5`, no script writes a `savePointStates` value other than 1, and `unkA7` is set in 0049.EVT and cleared in 0065.EVT (a stretch where saving, and so travel (R3), is refused). So no one-way stretch needs mirroring and `pc/assets/fast_travel/blocks.json` is not created; Task 3 re-runs the scan as a test and Task 3b (conditional) adds the blocks file only if a finding is actionable.
12. **Prompt.** A small plate (the Travel binding's icon for the device used last, and "Travel") over the game picture at tunable `qol_fast_travel_prompt_x` / `_y` / `_scale`, toggle `qol_fast_travel_prompt`; hidden while the editor shows and when the binding has nothing on the device used last.
13. **Settings file values.** `qol_fast_travel_cost` is an int (0 free, 1 flat, 2 scaled, 3 game) like `qol_skip_charging`; RPC also takes the names.

## File structure

Created:

| File | Responsibility |
|---|---|
| `pc/src/fast_travel.h` | The rules' types and functions, MENU0's tables, the settings' names, `PC_SHORTCUT_TRAVEL`, the game-side hooks' prototypes (included by MENU0 15AC.c under ROOD_PC). |
| `pc/src/fast_travel_rules.c` | Pure C: tables, whose unlock, cost, mask, menu bits, refusals, the Travel button's guard, packing, the report's JSON, `[travel]` log lines. |
| `pc/src/fast_travel.c` | Game side (battle builds): fills the state, the hooks for MENU0 / 5BF94 / 146C, the field poll (Travel button, arrival check, prompt state), the RPC report and the disc audit. |
| `pc/src/fast_travel_prompt.cpp` | The Travel prompt drawn with Dear ImGui over the game picture. |
| `pc/tests/fast_travel_probe.c` | CTest probe of `fast_travel_rules.c` (table parity with 84.c, packing parity with 15AC.c, cost, unlock / guard truth tables, JSON). |
| `pc/tools/fast_travel_scan.py` | The script scan: room and event scripts walked op by op; flag ops on `unkB5`, `unkA7`, `savePointStates`; JSON. |
| `pc/tests/fast_travel_scan_test.py` | Runs the scan: no desync, every Circle sets its own state, no actionable finding outside `blocks.json`. |
| `pc/tests/menu_nav.py` | `top_command`: the original top level's cursor to a command by the game's own cursor (not by counting rows). |
| `pc/tests/fast_travel_smoke.py` | Headless game test, `--menus off` and `--menus on`: status, audit, warp hygiene, menu travel, save / decode / continue, refusals, game unlock, current Circle, Travel button, prompt. |
| `pc/docs/Fast_Travel.md` | The feature, its rules, settings, RPC, tests, the scan's findings. |

Modified:

| File | Change |
|---|---|
| `src/MENU/MENU0.PRG/15AC.c` | ROOD_PC macro block; 4 unlock reads, 3 cost uses, the mask; the port's refusals on the row; the Travel button's entry and its Cross. |
| `src/BATTLE/BATTLE.PRG/5BF94.c` | `func_800C930C`: menu bits ORed into the local `enabled`; the Magic command's mask. |
| `src/BATTLE/BATTLE.PRG/146C.c` | `func_800735F8` case 10: `magicUsed` through `pc_fast_travel_begin`. |
| `pc/src/room_warp.c` | Calls the fast travel poll and room hook; snapshots / restores `savePointStates` around a warp chain. |
| `pc/src/room_warp.h`, `pc/src/room_catalog.c` | `pc_room_info.circle_id` / `circle_cell` from MPD section 9 (the audit). |
| `pc/src/project_settings.cpp` | `qol_fast_travel`, `_cost`, `_flat`, `_scale`, `_prompt`, `_prompt_x`, `_prompt_y`, `_prompt_scale`. |
| `pc/src/boot_controller.h`, `.c` | `PC_BIND_TRAVEL` / `PC_BIND_SLOTS`; `bind_key_travel`, `bind_pad_travel`; `pc_controller_element`, `pc_keyboard_travel`, `pc_bind_shared_bits`. |
| `pc/src/boot_gamepad.c`, `pc/src/boot_window.c` | The Travel slot's held state (`pc_window_travel_held`, `pc_window_travel_shared`). |
| `pc/src/button_prompts.h`, `.c`, `pc/src/prompt_glyphs.cpp` | `PC_PROMPT_TRAVEL` (slot 16) glyphs. |
| `pc/src/dev_rpc.c` | RPC `fast_travel`. |
| `pc/src/dev_overlay.cpp` | Gameplay panel rows; the prompt drawn each frame. |
| `pc/src/ui/ui_settings.cpp`, `pc/tests/ui_settings_probe.cpp`, `pc/tests/ui_settings_stubs.cpp` | Gameplay rows, Bindings' Travel row, row counts. |
| `pc/src/ui/magick_bridge.c`, `pc/src/ui/menu_bridge.c` | The shared predicate, cost, mask and menu bits; the shared tables. |
| `pc/tests/controller_probe.c` | The Travel slot's mapping and settings. |
| `pc/tests/ui_data_smoke.py`, `save_menu_smoke.py`, `interp_menu_2d_smoke.py` (and any other suite Task 14 finds) | Top-level navigation by the game's cursor; `menu7_driver` for fast_travel_smoke. |
| `pc/tests/run.py`, `pc/CMakeLists.txt`, `pc/tools/build_boot.py` | New suites, probe, units. |
| `pc/README.md` | Settings, bindings and RPC rows; a Fast travel line. |

---

### Task 1: Worktree and baseline

**Files:** none changed.

- [ ] **Step 1: Branch in a worktree**

Use superpowers:using-git-worktrees. From the main checkout:

```bash
git worktree add .claude/worktrees/fast-travel -b claude/fast-travel main
cd .claude/worktrees/fast-travel
cp -a $MAIN/build build; cp -a $MAIN/data data; cp -a $MAIN/disks disks
mkdir -p pc/build; cp $MAIN/pc/build/test-card1.mcd pc/build/
cp -a $MAIN/pc/build/nrd pc/build/
```

Expected: a new worktree on `claude/fast-travel` with `build/`, `data/`, `disks/`, `pc/build/test-card1.mcd`, `pc/build/nrd/`.

- [ ] **Step 2: Configure CTest**

```bash
test -f pc/build/build.ninja || cmake -S pc -B pc/build -G Ninja
```

Expected: `pc/build/build.ninja` exists.

- [ ] **Step 3: Build the test binaries**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
```

Expected: both exit 0, no `error:` line.

- [ ] **Step 4: Record the baseline of the suites this plan touches**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest ui_magick menu0_levelled ui_data save_menu interp_menu_2d ui_menus ui_system system_settings button_prompts room_browser weapon_slots input
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan ui_magick ui_data room_browser
```

Expected: tables ending in `N/N passed` (or a known failure list). Write down any suite that already fails; later tasks must not add failures.

---

### Task 2: The rules, and their probe

**Files:**
- Create: `pc/src/fast_travel.h`, `pc/src/fast_travel_rules.c`, `pc/tests/fast_travel_probe.c`
- Modify: `pc/CMakeLists.txt` (after the `data_plan_probe` block), `pc/tools/build_boot.py` (the pure host units' list, line 922)

- [ ] **Step 1: Write the failing probe**

Create `pc/tests/fast_travel_probe.c`:

```c
/* Fast travel's rules (pc/src/fast_travel_rules.c) without the game: MENU0's
 * tables against the decompiled 84.c, the packing against 15AC.c's own
 * arithmetic, the cost modes, whose unlock is in effect, the port's refusals,
 * the Travel button's guard and the RPC report. */
#include "fast_travel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...)                            \
    do {                                            \
        if (!(cond)) {                              \
            fprintf(stderr, "FAIL %d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);           \
            fputc('\n', stderr);                    \
            ++failures;                             \
        }                                           \
    } while (0)

/* MENU0 84.c's `name[] = { ... }` initialiser, read from the source tree. */
static int read_table(const char* name, int* out, int max)
{
    char path[1024], key[96];
    static char text[1 << 16];
    snprintf(path, sizeof path, "%s/src/MENU/MENU0.PRG/84.c", ROOD_ROOT);
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    snprintf(key, sizeof key, "%s[] = {", name);
    char* at = strstr(text, key);
    if (!at)
        return -1;
    at += strlen(key);
    int count = 0;
    while (count < max) {
        char* end;
        long v = strtol(at, &end, 0);
        if (end == at)
            break;
        out[count++] = (int)v;
        at = end;
        while (*at == ',' || *at == ' ' || *at == '\n' || *at == '\r')
            ++at;
        if (*at == '}')
            break;
    }
    return count;
}

/* 15AC.c returnIfReady, line for line (rowCount's stale high bits are masked
 * away), then vs_menu0_exec teleportMenu's `executeAbility.s32 = i - 1`. */
static int menu0_packed(int point, int cost, int rowCount)
{
    int i = pc_ft_save_point_data[point] + 1;
    int v1 = i & 0x1FF;
    int init = ~0x3E00;
    int v0 = (rowCount & ~0x1FF) | v1;
    v1 = (i >> 7) & 0x3E00;
    v0 &= init;
    v0 |= v1;
    int a0 = (i >> 0x18) & 0x1F;
    a0 <<= 0xE;
    v1 = cost << 0x13;
    v0 &= 0x3FFF;
    return (v0 | a0 | v1) - 1;
}

static pc_ft_settings on(int cost)
{
    pc_ft_settings c = { 1, cost, 15, 0.5f };
    return c;
}

/* On Circle 2 (MENU0 index 2, location 0x199), MP 40, prologue over,
 * Circles 0-2 found, nothing in the way. */
static pc_ft_state circle2(void)
{
    pc_ft_state s;
    memset(&s, 0, sizeof s);
    s.intro = 3;
    s.circle = 3;
    s.here = 2;
    s.mask = 8; /* sheathed only */
    s.mp = 40;
    s.states[0] = s.states[1] = s.states[2] = 1;
    return s;
}

static void tables(void)
{
    int data[64], costs[64];
    CHECK(read_table("_savePointData", data, 64) == PC_FT_POINTS, "84.c _savePointData has 48 entries");
    CHECK(read_table("savePointCosts", costs, 64) == PC_FT_POINTS, "84.c savePointCosts has 48 entries");
    for (int i = 0; i < PC_FT_POINTS; ++i) {
        CHECK(pc_ft_save_point_data[i] == data[i], "_savePointData[%d] %#x (84.c %#x)", i, pc_ft_save_point_data[i], data[i]);
        CHECK(pc_ft_save_point_costs[i] == costs[i], "savePointCosts[%d] %d (84.c %d)", i, pc_ft_save_point_costs[i], costs[i]);
    }
    for (int i = 0; i < PC_FT_USED; ++i)
        CHECK(pc_ft_save_point_data[i] != 0, "entry %d is used", i);
    CHECK(pc_ft_save_point_data[PC_FT_USED] == 0, "35 entries used");
}

static void packing(void)
{
    static const int costs[] = { 0, 1, 15, 19, 87, 4095 };
    for (int p = 0; p < PC_FT_USED; ++p)
        for (unsigned k = 0; k < sizeof costs / sizeof costs[0]; ++k)
            CHECK(pc_ft_packed(p, costs[k]) == menu0_packed(p, costs[k], 0x5A5A00 + p),
                "point %d cost %d: %#x (MENU0 %#x)", p, costs[k], pc_ft_packed(p, costs[k]),
                menu0_packed(p, costs[k], 0x5A5A00 + p));
    CHECK(pc_ft_here(0x1B) == 0 && pc_ft_here(0x199) == 2 && pc_ft_here(0x3FF) == PC_FT_POINTS
            && pc_ft_here(0) == PC_FT_USED,
        "this room's point: MENU0's search (48 none; location 0 matches the first unused entry, as in 84.c)");
}

static void costs(void)
{
    pc_ft_state s = circle2();
    CHECK(pc_ft_raw_cost(2, 2) == 15 && pc_ft_raw_cost(2, 0) == 23 && pc_ft_raw_cost(2, 18) == 79,
        "_getTeleportCost: 15 here, + 4 per index between");
    CHECK(pc_ft_raw_cost(PC_FT_POINTS, 3) == 15 + 4 * 44, "no point in this room: 48");
    pc_ft_settings c = on(PC_FT_COST_FREE);
    CHECK(pc_ft_cost(&c, &s, 23) == 0, "free");
    c = on(PC_FT_COST_FLAT);
    CHECK(pc_ft_cost(&c, &s, 23) == 15, "flat 15");
    c.flat = 9000;
    CHECK(pc_ft_cost(&c, &s, 23) == PC_FT_COST_MAX, "flat: the packed field's 12 bits");
    c = on(PC_FT_COST_SCALED);
    CHECK(pc_ft_cost(&c, &s, 23) == 12, "scaled x0.5, rounded");
    c = on(PC_FT_COST_GAME);
    CHECK(pc_ft_cost(&c, &s, 23) == 23, "game");
    c = on(PC_FT_COST_FREE);
    s.game = 1;
    CHECK(pc_ft_cost(&c, &s, 23) == 23, "the game's unlock: its own cost whatever the mode");
}

static void unlock(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(pc_ft_whose(&c, &s) == PC_FT_PORT && pc_ft_unlock(&c, &s) == 1, "the port's unlock");
    s.intro = 2;
    CHECK(pc_ft_whose(&c, &s) == PC_FT_OFF && pc_ft_unlock(&c, &s) == 0, "not during the prologue");
    s = circle2();
    s.special = 1;
    CHECK(pc_ft_unlock(&c, &s) == 0, "not in the middle of a puzzle");
    s = circle2();
    c.enabled = 0;
    CHECK(pc_ft_unlock(&c, &s) == 0, "qol_fast_travel 0");
    s.game = 1;
    CHECK(pc_ft_whose(&c, &s) == PC_FT_GAME && pc_ft_unlock(&c, &s) == 1, "the game's unlock wins (setting off)");
    s.game = 2;
    CHECK(pc_ft_unlock(&c, &s) == 2, "the spirit paling: 2, as MENU0 reads it");
    c.enabled = 1;
    CHECK(pc_ft_unlock(&c, &s) == 2, "the paling wins over the port's unlock");
    CHECK(pc_ft_counts_as_magic(&s) == 1, "the game's unlock counts as magic");
    s = circle2();
    CHECK(pc_ft_counts_as_magic(&s) == 0, "the port's does not");
}

static void masks(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == (PC_FT_MAGIC_MASK & ~PC_FT_NO_MP), "free on a Circle: MP 0 greys nothing");
    s.circle = 0;
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == PC_FT_MAGIC_MASK, "off a Circle: the game's mask");
    s = circle2();
    c = on(PC_FT_COST_FLAT);
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == PC_FT_MAGIC_MASK, "a cost: MP 0 greys");
    c = on(PC_FT_COST_FREE);
    s.game = 1;
    CHECK(pc_ft_mask(&c, &s, PC_FT_MAGIC_MASK) == PC_FT_MAGIC_MASK, "the game's unlock: the game's mask");
    s = circle2();
    CHECK(pc_ft_menu_bits(&c, &s) == 1, "the Magic command on a Circle");
    s.circle = 0;
    CHECK(pc_ft_menu_bits(&c, &s) == 0, "not off a Circle");
    s = circle2();
    s.game = 1;
    CHECK(pc_ft_menu_bits(&c, &s) == 0, "the game's unlock sets menuFlags itself");
}

static void refusals(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(!pc_ft_blocked(&c, &s), "nothing in the way");
    s.warped = 1;
    CHECK(pc_ft_blocked(&c, &s) && strstr(pc_ft_blocked(&c, &s), "Room Browser"), "R7: warped");
    s = circle2();
    s.no_save = 1;
    CHECK(pc_ft_blocked(&c, &s) != NULL, "R3: unkA7");
    s = circle2();
    s.states[2] = 0;
    CHECK(pc_ft_blocked(&c, &s) != NULL, "R1: this room's Circle not recorded (MENU0's nearestSavePoint)");
    s = circle2();
    s.here = PC_FT_POINTS;
    CHECK(pc_ft_blocked(&c, &s) != NULL, "R1: no Circle in MENU0's table for this room");
    s = circle2();
    s.no_save = 1, s.game = 1;
    CHECK(!pc_ft_blocked(&c, &s), "the game's unlock: the game's rules only");
    s = circle2();
    CHECK(!pc_ft_refused(&c, &s, 0) && !pc_ft_refused(&c, &s, 1), "found Circles");
    CHECK(pc_ft_refused(&c, &s, 3) != NULL, "R4: not found");
    s.states[1] = 2;
    CHECK(pc_ft_refused(&c, &s, 1) && strstr(pc_ft_refused(&c, &s, 1), "paling"), "R4: the spirit paling");
    s = circle2();
    c = on(PC_FT_COST_GAME);
    s.mp = 18;
    CHECK(pc_ft_refused(&c, &s, 1) != NULL && !pc_ft_refused(&c, &s, 2), "MP short (19) for Circle 1; here costs 15");
    s = circle2();
    s.mask |= 2;
    CHECK(pc_ft_refused(&c, &s, 1) && strstr(pc_ft_refused(&c, &s, 1), "carrying"), "the mask: carrying");
    s = circle2();
    s.mask |= PC_FT_NO_MP, s.mp = 0;
    c = on(PC_FT_COST_FREE);
    CHECK(!pc_ft_refused(&c, &s, 1), "free: MP 0 refuses nothing");
}

static void shortcut(void)
{
    pc_ft_settings c = on(PC_FT_COST_FREE);
    pc_ft_state s = circle2();
    CHECK(!pc_ft_shortcut(&c, &s), "the Travel button opens the list");
    s.circle = 0;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "off a Circle");
    s = circle2();
    s.event = 1;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "an event");
    s = circle2();
    s.busy = 1;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "Ashley busy (or L2 / R2 held)");
    s = circle2();
    s.states[0] = s.states[1] = 0;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "no other Circle found");
    s = circle2();
    s.game = 2;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "the spirit paling");
    s = circle2();
    c.enabled = 0;
    CHECK(pc_ft_shortcut(&c, &s) != NULL, "travel off, the game's spell not learned");
    s.game = 1, s.mp = 99;
    CHECK(!pc_ft_shortcut(&c, &s), "the game's Teleportation: the button opens its list too");
    s.no_save = 1;
    CHECK(!pc_ft_shortcut(&c, &s), "... with the game's rules only");
}

static void report(void)
{
    static pc_ft_report r;
    static char out[1 << 16];
    memset(&r, 0, sizeof r);
    r.settings = on(PC_FT_COST_FREE);
    r.state = circle2();
    int n = pc_ft_json(&r, out, sizeof out);
    CHECK(n > 0 && n < (int)sizeof out, "the JSON fits");
    CHECK(strstr(out, "\"whose\":\"port\"") && strstr(out, "\"cost\":\"free\"") && strstr(out, "\"why\":\"\""),
        "whose, cost, the Travel button's reason");
    CHECK(strstr(out, "{\"i\":2,\"state\":1,\"raw\":15,\"cost\":0,\"current\":true"), "the current Circle's row: %.200s", out);
    CHECK(pc_ft_json(&r, out, 64) < 0, "too small: refused, not cut");
}

int main(void)
{
    tables();
    packing();
    costs();
    unlock();
    masks();
    refusals();
    shortcut();
    report();
    if (failures)
        return 1;
    puts("PASS: fast travel rules (tables, packing, cost modes, unlock, mask, refusals, Travel button, JSON)");
    return 0;
}
```

In `pc/CMakeLists.txt`, after the `data_plan_probe` block (line 152) add:

```cmake
# Fast travel's rules (fast_travel_rules.c): MENU0's tables and packing, cost modes, unlock and guards.
add_executable(fast_travel_probe tests/fast_travel_probe.c src/fast_travel_rules.c)
target_include_directories(fast_travel_probe PRIVATE src)
target_compile_definitions(fast_travel_probe PRIVATE ROOD_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(fast_travel_probe PRIVATE -Wall -Wextra -Werror)
target_link_libraries(fast_travel_probe PRIVATE m)
add_test(NAME fast_travel_probe COMMAND fast_travel_probe)
```

- [ ] **Step 2: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null && flock "$ROOD_HEAVY_LOCK" ninja -C pc/build fast_travel_probe
```

Expected: `fatal error: fast_travel.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `pc/src/fast_travel.h`:

```c
/* Fast travel between Circles of Magic (pc/docs/Fast_Travel.md): the game's
 * own Teleportation (MENU0 _teleportMenu, BATTLE func_800735F8 case 10,
 * func_8007C694, the door's room load), offered from the first Circle once the
 * prologue is over (setting qol_fast_travel) instead of only once the game has
 * set vs_main_stateFlags.unkB5. When the game itself has set it, everything is
 * the game's.
 *
 * fast_travel_rules.c: the rules, plain C with no game headers
 * (pc/tests/fast_travel_probe.c). fast_travel.c (battle builds) fills
 * pc_ft_state from the game and is what the src hooks call (MENU0 15AC.c
 * includes this header under ROOD_PC). Nothing writes unkB5, menuFlags,
 * savePointStates or magicUsed. */
#ifndef ROOD_FAST_TRAVEL_H
#define ROOD_FAST_TRAVEL_H
#ifdef __cplusplus
extern "C" {
#endif

#define PC_FT_POINTS 48       /* savePointStates[48], MENU0's tables */
#define PC_FT_USED 35         /* the entries with a location */
#define PC_SHORTCUT_TRAVEL 9  /* vs_battle_shortcutInvoked of the Travel button (the game's run 1..8) */
#define PC_FT_MAGIC_MASK 0xB7 /* D_800F4EA0 bits that grey Magic and every destination (MENU0) */
#define PC_FT_NO_MP 0x20      /* ... MP 0 (func_800CABE0) */
#define PC_FT_HERE 15         /* _getTeleportCost of the Circle Ashley stands on */
#define PC_FT_COST_MAX 4095   /* the packed cost's 12 bits (executeAbility.fields.unk0_19) */

enum { PC_FT_COST_FREE, PC_FT_COST_FLAT, PC_FT_COST_SCALED, PC_FT_COST_GAME, PC_FT_COST_MODES };
enum { PC_FT_OFF, PC_FT_PORT, PC_FT_GAME }; /* whose unlock is in effect */

extern const int pc_ft_save_point_data[PC_FT_POINTS];   /* MENU0 84.c _savePointData */
extern const char pc_ft_save_point_costs[PC_FT_POINTS]; /* _getTeleportCost's savePointCosts */
extern const char* const pc_ft_cost_names[PC_FT_COST_MODES]; /* "free", "flat", "scaled", "game" */

/* Settings (project_settings.cpp): qol_fast_travel 0/1 (1), qol_fast_travel_cost
 * PC_FT_COST_* (free), _flat MP (15), _scale (0.5); the prompt: on (1), where
 * (fractions of the window, 0.5 / 0.84) and how big (1). */
extern int pc_qol_fast_travel, pc_qol_fast_travel_cost, pc_qol_fast_travel_flat, pc_qol_fast_travel_prompt;
extern float pc_qol_fast_travel_scale, pc_qol_fast_travel_prompt_x, pc_qol_fast_travel_prompt_y,
    pc_qol_fast_travel_prompt_scale;

typedef struct {
    int enabled, cost, flat;
    float scale;
} pc_ft_settings;

typedef struct {
    int game;     /* vs_main_stateFlags.unkB5: 0 not learned, 1 learned, 2 the spirit paling */
    int intro;    /* introState (3 and up: the prologue is over) */
    int special;  /* puzzleModeDisabled: a timed puzzle is under way */
    int warped;   /* the Room Browser: warped, or a warp under way */
    int no_save;  /* unkA7: the game refuses to save here now */
    int circle;   /* func_8008A4FC(): the Circle under Ashley (point + 1), 0 none */
    int here;     /* this room's point in MENU0's table (_getTeleportCost's), PC_FT_POINTS none */
    int event;    /* an event script, a script room change, a screen transition or game over */
    int busy;     /* Ashley acts (unk954) or falls (status 0x4000), or L2 / R2 held */
    unsigned mask; /* func_800CABE0(0) now */
    int mp;
    unsigned char states[PC_FT_POINTS]; /* savePointStates: 0 not found, 1 found, 2 the spirit paling */
} pc_ft_state;

int pc_ft_here(int location);             /* this room's point (MENU0's search: PC_FT_POINTS when none) */
int pc_ft_raw_cost(int here, int target); /* _getTeleportCost */
int pc_ft_whose(const pc_ft_settings* c, const pc_ft_state* s);
int pc_ft_unlock(const pc_ft_settings* c, const pc_ft_state* s); /* what MENU0 reads for unkB5 */
int pc_ft_cost(const pc_ft_settings* c, const pc_ft_state* s, int raw);
unsigned pc_ft_mask(const pc_ft_settings* c, const pc_ft_state* s, unsigned bits);
unsigned pc_ft_menu_bits(const pc_ft_settings* c, const pc_ft_state* s); /* menuFlags bits to show (bit 0 Magic) */
/* NULL, or why the port's unlock refuses to leave from here (R1, R3, R7). */
const char* pc_ft_blocked(const pc_ft_settings* c, const pc_ft_state* s);
/* NULL, or why destination `point` is refused (R4, the mask, MP). */
const char* pc_ft_refused(const pc_ft_settings* c, const pc_ft_state* s, int point);
/* NULL when the Travel button opens the destinations now, or why not. */
const char* pc_ft_shortcut(const pc_ft_settings* c, const pc_ft_state* s);
int pc_ft_counts_as_magic(const pc_ft_state* s); /* case 10 sets magicUsed */
int pc_ft_packed(int point, int cost);           /* MENU0's hand-off (D_800F4E98.executeAbility) */

/* The RPC report (fast_travel.c fills it, pc_ft_json writes it). */
typedef struct {
    int zone, room;   /* the destination's room (the executable's location table) */
    int circle_id;    /* its Circle object's id (section 9, type 16), 0 none */
    int cell[3];      /* its x, z, level */
    int ok;           /* id == point + 1 and the cell is the arrival cell, level 0 */
} pc_ft_audit_point;
typedef struct {
    int valid, from, to, location, whose, counted;
    int arrived, circle, room_ok, mp_before, mp_after;
} pc_ft_trip;
typedef struct {
    pc_ft_settings settings;
    pc_ft_state state;
    int prompt, prompt_on;          /* the prompt would show now; qol_fast_travel_prompt */
    unsigned presses, opened, ignored, prompt_drawn;
    pc_ft_trip last;
    int audited;
    pc_ft_audit_point audit[PC_FT_USED];
} pc_ft_report;
/* The report as JSON into out (its length, or -1 when it does not fit). */
int pc_ft_json(const pc_ft_report* r, char* out, int size);
/* The [travel] log lines (stderr). */
void pc_ft_log_trip(const pc_ft_trip* t);
void pc_ft_log_arrival(const pc_ft_trip* t);

/* fast_travel.c (battle builds): the src hooks and the field poll. */
int pc_fast_travel_unlock(void);             /* MENU0, for vs_main_stateFlags.unkB5 */
int pc_fast_travel_cost(int raw);            /* MENU0: what destination of raw cost `raw` costs */
unsigned pc_fast_travel_mask(unsigned bits); /* MENU0 / BATTLE: the menu mask bits that grey Magic */
unsigned pc_fast_travel_menu_bits(void);     /* BATTLE func_800C930C: commands shown, never stored */
int pc_fast_travel_blocked(void);            /* MENU0: the Teleportation row greyed by the port's rules */
const char* pc_fast_travel_why(void);        /* ... why (the modern Magick page) */
int pc_fast_travel_begin(int packed);        /* BATTLE func_800735F8 case 10: 1 = set magicUsed */
int pc_fast_travel_poll(void);               /* room_warp.c's field poll: 1 = the Travel button opened the list */
void pc_fast_travel_room_loaded(void);       /* room_warp.c: a room's sections are loaded */
void pc_fast_travel_press(void);             /* RPC fast_travel {press}: the Travel button, once */
int pc_fast_travel_prompt(void);             /* the prompt shows now (fast_travel_prompt.cpp) */
void pc_fast_travel_report(pc_ft_report* r, int audit); /* RPC fast_travel */

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 4: Write the rules**

Create `pc/src/fast_travel_rules.c`:

```c
/* Fast travel's rules (fast_travel.h): plain C, no game headers. */
#include "fast_travel.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>

/* MENU0 84.c, copied (fast_travel_probe compares them with the source). */
const int pc_ft_save_point_data[PC_FT_POINTS] = { 0x0305001B, 0x0801000E, 0x04030199, 0x0C0C001C, 0x070C0028,
    0x040200CD, 0x0F070035, 0x08060093, 0x060000CE, 0x09080106, 0x09080111, 0x0C030098, 0x090800E0, 0x060900C8,
    0x050800CA, 0x050F00CC, 0x0109009B, 0x0A07007C, 0x000400CF, 0x080800EC, 0x020400F1, 0x0705011D, 0x06080120,
    0x04080139, 0x04080147, 0x0904014F, 0x010300A2, 0x150C00A3, 0x030200D0, 0x060600D1, 0x1007008B, 0x010400AB,
    0x05020055, 0x07060063, 0x0A0400D2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
const char pc_ft_save_point_costs[PC_FT_POINTS] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0 };
const char* const pc_ft_cost_names[PC_FT_COST_MODES] = { "free", "flat", "scaled", "game" };

static const char PALING[] = "The spirit paling prevents teleportation.";

int pc_ft_here(int location)
{
    int i;
    for (i = 0; i < PC_FT_POINTS; ++i)
        if ((pc_ft_save_point_data[i] & 0xFFFF) == location)
            break;
    return i;
}

int pc_ft_raw_cost(int here, int target)
{
    int i = here, j, cost = PC_FT_HERE;
    if (target < i) {
        i = target;
        target = here;
    }
    for (j = i; j < target && j < PC_FT_POINTS; ++j)
        cost += pc_ft_save_point_costs[j];
    return cost;
}

int pc_ft_whose(const pc_ft_settings* c, const pc_ft_state* s)
{
    if (s->game)
        return PC_FT_GAME;
    return c->enabled && s->intro >= 3 && !s->special ? PC_FT_PORT : PC_FT_OFF;
}

int pc_ft_unlock(const pc_ft_settings* c, const pc_ft_state* s)
{
    return s->game ? s->game : pc_ft_whose(c, s) == PC_FT_PORT;
}

int pc_ft_cost(const pc_ft_settings* c, const pc_ft_state* s, int raw)
{
    long v;
    if (pc_ft_whose(c, s) != PC_FT_PORT)
        return raw;
    switch (c->cost) {
    case PC_FT_COST_FREE:
        return 0;
    case PC_FT_COST_FLAT:
        v = c->flat;
        break;
    case PC_FT_COST_SCALED:
        v = c->scale == c->scale ? lroundf((float)raw * c->scale) : raw; /* (NaN: the game's) */
        break;
    default:
        return raw;
    }
    return v < 0 ? 0 : v > PC_FT_COST_MAX ? PC_FT_COST_MAX : (int)v;
}

unsigned pc_ft_mask(const pc_ft_settings* c, const pc_ft_state* s, unsigned bits)
{
    if (pc_ft_whose(c, s) == PC_FT_PORT && c->cost == PC_FT_COST_FREE && s->circle)
        return bits & ~(unsigned)PC_FT_NO_MP;
    return bits;
}

unsigned pc_ft_menu_bits(const pc_ft_settings* c, const pc_ft_state* s)
{
    return pc_ft_whose(c, s) == PC_FT_PORT && s->circle ? 1u : 0u;
}

const char* pc_ft_blocked(const pc_ft_settings* c, const pc_ft_state* s)
{
    if (pc_ft_whose(c, s) != PC_FT_PORT)
        return NULL;
    if (s->warped)
        return "Leave the Room Browser warp first.";
    if (s->no_save)
        return "Cannot travel from here now.";
    if (s->here >= PC_FT_POINTS || !s->states[s->here])
        return "This Circle of Magic is not recorded yet.";
    return NULL;
}

/* The lowest bit of the menu mask MENU0 greys for, in the modern Magick
 * page's words (magick_bridge.c mask_reason). */
static const char* mask_why(unsigned m)
{
    if (m & 2)
        return "Not while carrying a cube.";
    if (m & 4)
        return "Not while hanging from a ledge.";
    if (m & PC_FT_NO_MP)
        return "Not enough MP.";
    if (m & 0x80)
        return "Silenced: spells cannot be cast.";
    return m ? "Not usable right now." : NULL;
}

const char* pc_ft_refused(const pc_ft_settings* c, const pc_ft_state* s, int point)
{
    const char* why;
    if (point < 0 || point >= PC_FT_USED || !s->states[point])
        return "Not found yet.";
    if (s->states[point] == 2)
        return PALING;
    if ((why = mask_why(s->mask & pc_ft_mask(c, s, PC_FT_MAGIC_MASK))))
        return why;
    if (s->mp < pc_ft_cost(c, s, pc_ft_raw_cost(s->here, point)))
        return "Not enough MP.";
    return NULL;
}

const char* pc_ft_shortcut(const pc_ft_settings* c, const pc_ft_state* s)
{
    int unlock = pc_ft_unlock(c, s), i;
    const char* why;
    if (!unlock)
        return c->enabled ? "Not yet: after the prologue." : "Fast travel is off.";
    if (unlock == 2)
        return PALING;
    if (!s->circle)
        return "Stand on a Circle of Magic.";
    if (s->event)
        return "Not during an event.";
    if (s->busy)
        return "Not now.";
    if ((why = pc_ft_blocked(c, s)))
        return why;
    if (s->here >= PC_FT_POINTS || !s->states[s->here])
        return "This Circle of Magic is not recorded yet.";
    for (i = 0; i < PC_FT_USED; ++i)
        if (i != s->here && !pc_ft_refused(c, s, i))
            return NULL;
    return "No other Circle of Magic to travel to.";
}

int pc_ft_counts_as_magic(const pc_ft_state* s) { return s->game != 0; }

int pc_ft_packed(int point, int cost)
{
    unsigned d = (unsigned)pc_ft_save_point_data[point];
    return (int)((d & 0x1FF) | ((d >> 16) & 0x1F) << 9 | ((d >> 24) & 0x1F) << 14
        | ((unsigned)cost & PC_FT_COST_MAX) << 19);
}

/* ---- the report ---- */

static const char* const whose_names[3] = { "off", "port", "game" };

#define PUT(...)                                                        \
    do {                                                                \
        int k = snprintf(out + n, (size_t)(size - n), __VA_ARGS__);     \
        if (k < 0 || k >= size - n)                                     \
            return -1;                                                  \
        n += k;                                                         \
    } while (0)

int pc_ft_json(const pc_ft_report* r, char* out, int size)
{
    const pc_ft_settings* c = &r->settings;
    const pc_ft_state* s = &r->state;
    const char* blocked = pc_ft_blocked(c, s);
    const char* why = pc_ft_shortcut(c, s);
    int n = 0, i;
    if (size <= 0)
        return -1;
    PUT("{\"settings\":{\"enabled\":%d,\"cost\":\"%s\",\"flat\":%d,\"scale\":%g,\"prompt\":%d},", c->enabled,
        c->cost >= 0 && c->cost < PC_FT_COST_MODES ? pc_ft_cost_names[c->cost] : "game", c->flat, (double)c->scale,
        r->prompt_on);
    PUT("\"whose\":\"%s\",\"unlock\":%d,\"menu_bits\":%u,\"prompt\":%s,\"prompt_drawn\":%u,",
        whose_names[pc_ft_whose(c, s)], pc_ft_unlock(c, s), pc_ft_menu_bits(c, s), r->prompt ? "true" : "false",
        r->prompt_drawn);
    PUT("\"state\":{\"game\":%d,\"intro\":%d,\"special\":%d,\"warped\":%d,\"no_save\":%d,\"circle\":%d,\"here\":%d,"
        "\"event\":%d,\"busy\":%d,\"mask\":%u,\"mp\":%d},",
        s->game, s->intro, s->special, s->warped, s->no_save, s->circle, s->here, s->event, s->busy, s->mask, s->mp);
    PUT("\"blocked\":\"%s\",\"why\":\"%s\",\"points\":[", blocked ? blocked : "", why ? why : "");
    for (i = 0; i < PC_FT_USED; ++i) {
        int raw = pc_ft_raw_cost(s->here, i);
        const char* refused = pc_ft_refused(c, s, i);
        PUT("%s{\"i\":%d,\"state\":%d,\"raw\":%d,\"cost\":%d,\"current\":%s,\"refused\":\"%s\"}", i ? "," : "", i,
            s->states[i], raw, pc_ft_cost(c, s, raw), raw == PC_FT_HERE ? "true" : "false", refused ? refused : "");
    }
    PUT("],\"presses\":%u,\"opened\":%u,\"ignored\":%u,", r->presses, r->opened, r->ignored);
    PUT("\"last\":{\"valid\":%s,\"from\":%d,\"to\":%d,\"location\":%d,\"whose\":\"%s\",\"counted\":%d,"
        "\"arrived\":%s,\"circle\":%d,\"room_ok\":%s,\"mp_before\":%d,\"mp_after\":%d}",
        r->last.valid ? "true" : "false", r->last.from, r->last.to, r->last.location,
        whose_names[r->last.whose >= 0 && r->last.whose < 3 ? r->last.whose : 0], r->last.counted,
        r->last.arrived ? "true" : "false", r->last.circle, r->last.room_ok ? "true" : "false", r->last.mp_before,
        r->last.mp_after);
    if (r->audited) {
        PUT(",\"audit\":[");
        for (i = 0; i < PC_FT_USED; ++i) {
            const pc_ft_audit_point* a = &r->audit[i];
            PUT("%s{\"i\":%d,\"zone\":%d,\"room\":%d,\"circle\":%d,\"cell\":[%d,%d,%d],\"ok\":%s}", i ? "," : "", i,
                a->zone, a->room, a->circle_id, a->cell[0], a->cell[1], a->cell[2], a->ok ? "true" : "false");
        }
        PUT("]");
    }
    PUT("}");
    return n;
}

void pc_ft_log_trip(const pc_ft_trip* t)
{
    fprintf(stderr, "[travel] Circle %d -> %d (location %#x), %s unlock%s\n", t->from, t->to, t->location,
        whose_names[t->whose >= 0 && t->whose < 3 ? t->whose : 0], t->counted ? ", counts as magic" : "");
}

void pc_ft_log_arrival(const pc_ft_trip* t)
{
    if (t->arrived)
        fprintf(stderr, "[travel] arrived on Circle %d (MP %d -> %d)\n", t->to, t->mp_before, t->mp_after);
    else
        fprintf(stderr, "[travel] arrival mismatch: Circle %d wanted, room %s, standing on %d\n", t->to,
            t->room_ok ? "right" : "wrong", t->circle - 1);
}
```

- [ ] **Step 5: Run the probe**

```bash
flock "$ROOD_HEAVY_LOCK" ninja -C pc/build fast_travel_probe && pc/build/fast_travel_probe
```

Expected: `PASS: fast travel rules (...)`.

- [ ] **Step 6: Link the rules into the game**

In `pc/tools/build_boot.py`, in the list at line 922 (`for name in ["audio_libspu", ..., "snapshot"] + ...`), add `"fast_travel_rules"` after `"room_catalog"`. (A pure unit: compiled with the host flags into every build.)

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
```

Expected: exit 0.

- [ ] **Step 7: Commit**

```bash
git add pc/src/fast_travel.h pc/src/fast_travel_rules.c pc/tests/fast_travel_probe.c pc/CMakeLists.txt pc/tools/build_boot.py
git commit -m "Fast travel: the rules (unlock, cost, mask, refusals, Travel button guard, MENU0 packing) and their probe

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: The script scan (rule R9), as a test

**Files:**
- Create: `pc/tools/fast_travel_scan.py`, `pc/tests/fast_travel_scan_test.py`
- Modify: `pc/tests/run.py` (a suite after `lp64_alias_scan`)

- [ ] **Step 1: Write the failing test**

Create `pc/tests/fast_travel_scan_test.py`:

```python
#!/usr/bin/env python3
"""Fast travel's script scan (pc/tools/fast_travel_scan.py, rule R9 in pc/docs/Fast_Travel.md).

    python3 pc/tests/fast_travel_scan_test.py

Every room script (data/MAP/MAPnnn.MPD) and event script (data/EVENT/nnnn.EVT)
walks op by op to its end with the interpreter's op lengths; every Circle's
own room script records it (savePointStates[i] = 1, all 35); and no finding is
actionable unless pc/assets/fast_travel/blocks.json mirrors it (a destination
blocked for it, reviewed by hand). Skipped (77) without data/."""
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BLOCKS = ROOT / "pc/assets/fast_travel/blocks.json"


def main():
    if not (ROOT / "data/MAP").is_dir() or not (ROOT / "data/BATTLE/BATTLE.PRG").exists():
        print("SKIP: no data/ (the disc's files)")
        return 77
    out = subprocess.run([sys.executable, str(ROOT / "pc/tools/fast_travel_scan.py")], capture_output=True,
                         text=True, check=True).stdout
    scan = json.loads(out)
    failures = []

    def check(ok, message):
        print(("ok   " if ok else "FAIL ") + message)
        if not ok:
            failures.append(message)

    check(scan["scripts"] > 800 and not scan["desync"],
          f"{scan['scripts']} scripts walked to their end (desync: {scan['desync'][:4]})")
    recorded = {f["point"] for f in scan["findings"] if f["kind"] == "records"}
    check(recorded == set(range(35)), f"every Circle's room script records it ({sorted(set(range(35)) - recorded)} missing)")
    blocks = json.loads(BLOCKS.read_text()) if BLOCKS.exists() else {"blocks": []}
    covered = {(b["file"], b["offset"]) for b in blocks["blocks"]}
    actionable = [f for f in scan["findings"] if f["actionable"]]
    loose = [f for f in actionable if (f["file"], f["offset"]) not in covered]
    for f in actionable:
        print(f"info: actionable: {f['file']}@{f['offset']:#x} {f['what']}")
    check(not loose, f"every actionable finding is mirrored in {BLOCKS.relative_to(ROOT)} ({loose[:4]})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `lp64_alias_scan` suite (line 108) add:

```python
        Suite("fast_travel_scan", [PY, str(TESTS / "fast_travel_scan_test.py")], 120, binary=None,
              note="fast travel R9: every room and event script walked op by op; every Circle records itself; "
                   "no script tests unkB5 or writes a Circle state other than 1 unless blocks.json mirrors it"),
```

- [ ] **Step 2: Run it and see it fail**

```bash
python3 pc/tests/fast_travel_scan_test.py
```

Expected: `FileNotFoundError` / non-zero exit from `subprocess.run` (no `fast_travel_scan.py`).

- [ ] **Step 3: Write the scan**

Create `pc/tools/fast_travel_scan.py`:

```python
#!/usr/bin/env python3
"""Fast travel's script scan (pc/docs/Fast_Travel.md, rule R9): which scripts
touch the flags fast travel depends on.

    python3 pc/tools/fast_travel_scan.py [--data data]

Every room script (MAP/MAPnnn.MPD: the MPD header's script section, offset at
0x10, length at 0x14) and event script (EVENT/nnnn.EVT) is walked op by op
from byte 16 to its text (the u16 at +2; BATTLE 4A0A8.c func_800BF5F0) with
the interpreter's own op lengths (BATTLE.PRG D_800E9C30, 256 bytes; 4A0A8.c
advances by D_800E9C30[op]). Flag ops (INITBTL 12AC.c's table, 4A0A8.c):
  0x40 jumpFwdIfFlag   [op, mode | flag >> 6, (flag & 63) << 2, value, jump16]
  0x41 jumpFwdIfFlags  [op, mode | f0 >> 6, (f0 & 63) << 2 | f1 >> 8, f1 & 255, jump16]
  0x46 setFlagImmContext [op, flag16]: the flag into the switch context
  0x50 setFlagImm      [op, mode | flag >> 6, (flag & 63) << 2, value] (mode 0x70: set)
  0x51 setFlag         [op, mode | f0 >> 6, (f0 & 63) << 2 | f1 >> 8, f1 & 255] (f0 from f1)
The flags: unkB5 (0xB5, Teleportation learned / the spirit paling), unkA7
(0xA7, saving refused), savePointStates (0x156 + i, Circle i found / paled).

Actionable (fast travel would have to mirror it: blocks.json): a test or copy of
unkB5 (a script that behaves differently once Teleportation is learned), a write of
unkB5 other than "set 1", a write of a Circle's state other than "set 1".
Reported, not actionable: each Circle recording itself ("records"), unkA7
writes (travel is refused while it is set, rule R3), other tests of the
Circles' states. Prints JSON; writes nothing (the output names files and
offsets only: keep it out of git)."""
import argparse
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
VRAM = 0x80068800       # BATTLE.PRG's load address (config/BATTLE/BATTLE.PRG/splat.yaml)
OP_LENGTHS = 0x800E9C30  # D_800E9C30 (573B8.c, 4A0A8.c)
UNK_B5, UNK_A7, POINTS = 0xB5, 0xA7, 0x156


def flag_name(flag):
    if flag == UNK_B5:
        return "unkB5"
    if flag == UNK_A7:
        return "unkA7"
    if POINTS <= flag < POINTS + 48:
        return f"savePointStates[{flag - POINTS}]"
    return None


def walk(name, code, lengths, findings, desync):
    if len(code) < 16:
        return 0
    end = struct.unpack_from("<H", code, 2)[0]
    if end < 16 or end > len(code):
        end = len(code)
    p = 16
    while p < end:
        op = code[p]
        n = lengths[op]
        if n == 0 or p + n > len(code):
            desync.append(f"{name}@{p:#x} op {op:#x}")
            return 1
        b = code[p:p + n]
        hits = []
        if op in (0x40, 0x50) and n >= 4:
            hits.append(((b[2] >> 2) | ((b[1] & 0xF) << 6), b[1] & 0xF0, b[3], "test" if op == 0x40 else "write"))
        elif op in (0x41, 0x51) and n >= 4:
            f0, f1 = (b[2] >> 2) | ((b[1] & 0xF) << 6), b[3] | ((b[2] & 3) << 8)
            hits.append((f0, b[1] & 0xF0, None, "test" if op == 0x41 else "write"))
            hits.append((f1, None, None, "test" if op == 0x41 else "read"))
        elif op == 0x46 and n >= 3:
            hits.append((b[1] | b[2] << 8, None, None, "test"))
        for flag, mode, value, how in hits:
            what = flag_name(flag)
            if not what:
                continue
            point = flag - POINTS if what.startswith("savePointStates") else None
            sets_one = op == 0x50 and mode == 0x70 and value == 1
            if point is not None and sets_one:
                kind = "records"
            else:
                kind = how
            actionable = (flag == UNK_B5 and how in ("test", "read")) or (flag == UNK_B5 and how == "write" and not sets_one) \
                or (point is not None and how == "write" and not sets_one)
            findings.append({"file": name, "offset": p, "op": op, "flag": flag, "mode": mode, "value": value,
                             "point": point, "kind": kind, "actionable": actionable,
                             "what": f"op {op:#x} {how} {what}" + (f" mode {mode:#x} value {value}"
                                                                   if value is not None else "")})
        p += n
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=str(ROOT / "data"))
    args = parser.parse_args()
    data = pathlib.Path(args.data)
    prg = (data / "BATTLE/BATTLE.PRG").read_bytes()
    lengths = prg[OP_LENGTHS - VRAM:OP_LENGTHS - VRAM + 256]
    # the table as 4A0A8.c's handlers read their operands
    if (lengths[0x40], lengths[0x41], lengths[0x42], lengths[0x46], lengths[0x50], lengths[0x51]) != (6, 6, 3, 3, 4, 4):
        sys.exit(f"BATTLE.PRG: no op length table at {OP_LENGTHS:#x}")
    findings, desync, scripts = [], [], 0
    for f in sorted((data / "MAP").glob("MAP*.MPD")):
        b = f.read_bytes()
        if len(b) < 0x18:
            continue
        off, size = struct.unpack_from("<I", b, 0x10)[0], struct.unpack_from("<H", b, 0x14)[0]
        if size and off + size <= len(b):
            scripts += walk(f.name, b[off:off + size], lengths, findings, desync)
    for f in sorted((data / "EVENT").glob("*.EVT")):
        scripts += walk(f.name, f.read_bytes(), lengths, findings, desync)
    json.dump({"scripts": scripts, "desync": desync, "findings": findings}, sys.stdout, indent=1)
    print()


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the test**

```bash
python3 pc/tests/fast_travel_scan_test.py
```

Expected (the read-only trial of this walk): `ok   901 scripts walked to their end (desync: [])`, `ok   every Circle's room script records it ([] missing)`, no `info: actionable:` line, `ok   every actionable finding is mirrored ...`; exit 0. Also look at the full output once, by hand:

```bash
python3 pc/tools/fast_travel_scan.py | python3 -c "import json,sys; d=json.load(sys.stdin); [print(f['file'], hex(f['offset']), f['what']) for f in d['findings'] if f['kind'] != 'records']"
```

Expected: `0057.EVT 0x3de op 0x50 write unkB5 mode 0x70 value 1` (the Lich teaches Teleportation), `0049.EVT ... write unkA7 ... value 1` and `0065.EVT ... write unkA7 ... value 0` (a stretch with saving refused: travel is refused there by R3). Write down every line you see for the report and for `pc/docs/Fast_Travel.md` (Task 15). If any line is `actionable` (the test prints `info: actionable:` and fails), do Task 3b before going on; otherwise skip Task 3b.

- [ ] **Step 5: Commit**

```bash
git add pc/tools/fast_travel_scan.py pc/tests/fast_travel_scan_test.py pc/tests/run.py
git commit -m "Fast travel: the script scan (R9) as a test: scripts walked op by op, every Circle records itself, nothing to mirror

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3b (only if Task 3 or Task 4's audit finds something): blocked destinations as data

Skip this task when Task 3's test passes with no `info: actionable:` line and Task 4's audit reports 35 of 35. Otherwise:

**Files:**
- Create: `pc/assets/fast_travel/blocks.json`
- Modify: `pc/src/fast_travel.h`, `pc/src/fast_travel_rules.c`, `pc/src/fast_travel.c`, `src/MENU/MENU0.PRG/15AC.c` (once Task 6's block exists), `pc/tests/fast_travel_probe.c`

- [ ] **Step 1: Review and write the data.** For each actionable finding, read the script around it (its jump targets and the flags it tests) and decide which destinations a first playthrough must not reach while the flag holds. Write `pc/assets/fast_travel/blocks.json` in your own words (no game text):

```json
{
  "blocks": [
    { "file": "MAPnnn.MPD", "offset": 1234, "points": [12, 13], "flag": 181, "while": [0, 0],
      "why": "Not yet: the way back is closed." }
  ]
}
```

(`points` blocked while flag `flag` is in `[while[0], while[1]]`; `file` / `offset` name the finding it mirrors, for the scan test.)

- [ ] **Step 2: Probe first.** Add to `fast_travel_probe.c` a `blocks()` section: `pc_ft_blocks_parse("{...}")` with one rule; `pc_ft_refused` returns its `why` for a listed point while `s.flags[flag]` is in range, and NULL outside it or under the game's unlock. Run it: fails to compile.

- [ ] **Step 3: Implement.** `pc_ft_state` gains `unsigned char flags[0x440]` (fill copies `vs_main_stateFlags`); `fast_travel_rules.c` gains a tiny parser for this one shape (`pc_ft_blocks_parse(const char* json)`, up to 16 rules) and `pc_ft_refused` checks them first while `pc_ft_whose == PC_FT_PORT`; `fast_travel.c` loads the file once (`pc_paths.h`'s asset directory) on first use. In MENU0 15AC.c `_teleportMenu`, after `availableSavePoints[rowCount] = i;`, add under the existing `#ifdef ROOD_PC` style:

```c
#ifdef ROOD_PC
            rowTypes[rowCount] |= pc_fast_travel_refused(i); /* blocks.json (the port's unlock only) */
#endif
```

with `int pc_fast_travel_refused(int point)` (1 when a block applies) in fast_travel.h / fast_travel.c. Run the probe, commit-check, the smoke's `menu` case with a blocked point made current by RAM pokes.

- [ ] **Step 4: Commit** (`Fast travel: blocked destinations mirrored from the scan (blocks.json)`).

---

### Task 4: The game side, the settings, the RPC report and the audit

**Files:**
- Create: `pc/src/fast_travel.c`, `pc/tests/fast_travel_smoke.py`
- Modify: `pc/src/project_settings.cpp`, `pc/src/room_warp.c`, `pc/src/room_warp.h`, `pc/src/room_catalog.c`, `pc/src/dev_rpc.c`, `pc/tools/build_boot.py` (the battle units' list, line 735), `pc/tests/run.py`

- [ ] **Step 1: Write the failing smoke (status and audit)**

Create `pc/tests/fast_travel_smoke.py`:

```python
#!/usr/bin/env python3
"""Fast travel (pc/docs/Fast_Travel.md; pc/src/fast_travel.c, fast_travel_rules.c;
src hooks in MENU0 15AC.c, BATTLE 5BF94.c and 146C.c): the game's own
Teleportation from the first Circle of Magic once the prologue is over.

    python3 pc/tests/fast_travel_smoke.py [--card pc/build/test-card1.mcd] [--menus off|on] [--cases a,b] [--out DIR]

Card safety: everything runs inside card_fixtures.card_guard (the player's
cards hashed before and after; ROOD_CARD1 / ROOD_CARD2 temp copies); the save
case writes only its own copy. Skipped (77) without a card, or when the card's
save room is not a Circle of Magic after the prologue.

Cases (each from the save_room checkpoint in a process of its own; status and
audit share the first):
  status  defaults (on, free), the port's unlock, the points = savePointStates,
          each cost mode, the Magic command shown and not stored, off
  audit   R6: each of the 35 destinations' rooms has its Circle object (id
          point + 1) on the arrival cell, read from the disc
  warp    R7 and warp hygiene: a Room Browser warp (scripts on) into an unfound
          Circle's room: travel refused while warped; back where the warps
          started: savePointStates as before (the Circle recorded meanwhile
          is put back)
  menu    the original list (off: Triangle > Magic > TELEPORTATION; on:
          Magick > Teleport): rows, free, the cursor on this Circle; travel to
          the nearest other Circle: MENU0's hand-off, arrival on the Circle,
          MP, magicUsed, unkB5, menuFlags, savePointStates unchanged
  save    travel, then Data > Save as file 2: decoded, checksums, saveLocation
          = the destination + 1, stateFlags = RAM, menuFlags bit 0 not stored,
          the room record = the destination; Continue from file 2: on that
          Circle, saving allowed, travel offered again
  refuse  unkA7, this Circle unrecorded, an event, MP short (game cost), off
          the Circle: refused (the row greyed, the button ignored, the room kept)
  game    unkB5 1: the game's own (raw cost charged, magicUsed set) whatever the
          settings; unkB5 2: everything refused
  here    (off) choosing the Circle Ashley stands on: back to the field
  button  the Travel button (a key bound to it and shared with Select; RPC
          press): the list directly, Select's zoom untouched, Cross closes,
          travel; refused presses open nothing
  prompt  the prompt shows on the Circle when travel is offered, not when off,
          not when the device used last has nothing bound
--out keeps screenshots (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import re
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import card_fixtures as cf  # noqa: E402

PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (characterState, menuItems)
CLOSED = 0x3F           # vs_battle_menuState.currentMenu with no menu
FLAGS_SIZE = 0x440      # sizeof(vs_main_flags_t)
UNK_A7, INTRO, UNK_B5, MAGIC_USED, SAVE_POINTS = 0xA7, 0xAB, 0xB5, 0xB7, 0x156
MENU_FLAGS = 2          # vs_main_settings.menuFlags (bit 0: the Magic command)
MP, MP_MAX = 0x1C, 0x1E  # vs_battle_actor2 currentMP / maxMP
ITEM = 64               # sizeof(vs_battle_menuItem_t): state +0, selected +6, unselectable +7
MAGIC_ITEM, TELEPORT_ITEM, LIST_ITEM = 0, 14, 20  # the top level's Magic, MENU0's Teleport row, the destinations
TELEPORT_MENU = 14      # vs_menu0_exec's state while _teleportMenu runs (vs_battle_submenuStates[1])
SHORTCUT_TRAVEL = 9     # vs_battle_shortcutInvoked of the Travel button (fast_travel.h)
INITIAL_DISTANCE = 32   # vs_battle_cameraCurrentSpherical.initialDistance (Select: 0x600 near / 0x900 far)
USED = 35
SAVE_FLAGS, SAVE_SETTINGS, SAVE_ROOM = 0x200, 0x6A8, 0x1778  # savedata_t (MENU7 260.c)


def save_point_data():
    """MENU0 84.c _savePointData (fast_travel_probe checks the port's copy against it)."""
    text = (ROOT / "src/MENU/MENU0.PRG/84.c").read_text()
    body = re.search(r"_savePointData\[\]\s*=\s*\{([^}]*)\}", text).group(1)
    return [int(x, 0) for x in body.replace("\n", " ").split(",") if x.strip()]


DATA = save_point_data()


def packed(point, cost):
    """MENU0's hand-off (D_800F4E98.executeAbility, unsigned)."""
    d = DATA[point]
    return (d & 0x1FF) | ((d >> 16) & 0x1F) << 9 | ((d >> 24) & 0x1F) << 14 | (cost & 0xFFF) << 19


class Trip:
    """One game process at the card's save room."""

    def __init__(self, name, out, card, menus, env, shots=False):
        self.name, self.out, self.card, self.menus, self.env, self.shots = name, out, card, menus, env, shots
        self.g = launch_at("save_room", card=card, fast=True, log=out / f"{name}.log", extra_env=env)
        self.sym = symbols()
        self.g.call("graphics", interpolate=False)
        self.g.call("ui_menu", modern=menus == "on")
        self.g.run(2)

    def close(self):
        self.g.close()

    def shot(self, what):
        if self.shots:
            self.g.present_screenshot(self.out / f"{self.name}-{what}.png")

    # ---- reads and writes ----
    def ft(self, **params):
        return self.g.call("fast_travel", **params)

    def u8(self, name, offset=0):
        return self.g.read(self.sym[name] + offset, 1)[0]

    def u32(self, address):
        return struct.unpack("<I", self.g.read(address, 4))[0]

    def flags(self):
        return bytes(self.g.read(self.sym["vs_main_stateFlags"], FLAGS_SIZE))

    def set_flag(self, offset, value):
        self.g.write(self.sym["vs_main_stateFlags"] + offset, bytes([value]))

    def settings_bytes(self):
        return bytes(self.g.read(self.sym["vs_main_settings"], 4))

    def menu(self):
        return self.u8("vs_battle_menuState")

    def menu0_state(self):
        return self.u8("vs_battle_submenuStates", 1)

    def actor2(self):
        actor = int.from_bytes(self.g.read(self.sym["vs_battle_characterState"], PTR), "little")
        return int.from_bytes(self.g.read(actor + 0x3C, 4), "little")

    def mp(self):
        return struct.unpack("<h", self.g.read(self.actor2() + MP, 2))[0]

    def set_mp(self, mp, top=None):
        a = self.actor2()
        mp_max = struct.unpack("<h", self.g.read(a + MP_MAX, 2))[0]
        top = max(mp_max, top or 0, mp)
        self.g.write(a + MP, struct.pack("<hh", mp, top))
        self.g.run(2)

    def location(self):
        st = self.g.state()
        table = self.g.read(self.sym["vs_main_locationZoneMapIds"], 1024)
        return next((i for i in range(1, 512) if table[2 * i] == st["zone"] and table[2 * i + 1] == st["room"]), 0)

    def room_of(self, location):
        table = self.g.read(self.sym["vs_main_locationZoneMapIds"], 1024)
        return table[2 * location], table[2 * location + 1]

    def handoff(self):
        e = self.g.read(self.sym["D_800F4E98"], 8)
        unk2 = struct.unpack("<h", e[2:4])[0]
        return unk2, struct.unpack("<I", e[4:8])[0]

    # ---- the original menus ----
    def items(self):
        p = int.from_bytes(self.g.read(self.sym["vs_battle_menuItems"], PTR), "little")
        if not p:
            return {}
        raw = self.g.read(p, ITEM * 40)
        return {i: (raw[i * ITEM], raw[i * ITEM + 6], raw[i * ITEM + 7]) for i in range(40) if raw[i * ITEM]}

    def wait(self, pred, limit, what, step=2, quiet=False):
        for n in range(0, limit + 1, step):
            if pred():
                return n
            self.g.run(step)
        if quiet:
            return None
        raise TimeoutError(f"{self.name}: {what}: not after {limit} vsyncs (currentMenu {self.menu():#x})")

    def wait_ready(self, want, limit=400):
        calm = [0]

        def ready():
            items = self.items()
            ok = want in items and all(s <= 1 for s, _, _ in items.values())
            calm[0] = calm[0] + 1 if ok else 0
            return calm[0] >= 3
        self.wait(ready, limit, f"menu item {want} ready", step=4)
        self.g.run(4)

    def selected(self, ids):
        items = self.items()
        return next((i for i in ids if i in items and items[i][1]), None)

    def nav(self, ids, target):
        """Up / Down until item `target` of the list `ids` is the selected one."""
        ids = list(ids)
        for _ in range(2 * len(ids) + 4):
            cur = self.selected(ids)
            if cur == target:
                return
            self.g.press("down" if cur is None or cur < target else "up", vsyncs=3, settle=6)
        raise TimeoutError(f"{self.name}: item {target} not reached (selected {self.selected(ids)})")

    def close_menu(self):
        for _ in range(8):
            if self.menu() == CLOSED:
                break
            self.g.press("triangle", vsyncs=3, settle=20)
        self.wait(lambda: self.menu() == CLOSED, 300, "the menu closed", step=4)
        self.g.run(10)


def nearest_other(s):
    here = s["state"]["here"]
    found = [p["i"] for p in s["points"] if p["state"] == 1 and p["i"] != here]
    return min(found, key=lambda i: abs(i - here)) if found else None


# ---- cases ----
def case_status(t, check, args):
    s = t.ft()
    st = s["state"]
    check(s["settings"] == {"enabled": 1, "cost": "free", "flat": 15, "scale": 0.5, "prompt": 1},
          f"status: defaults {s['settings']}")
    check(s["whose"] == "port" and s["unlock"] == 1 and st["game"] == 0,
          f"status: the port's unlock ({s['whose']}, unkB5 {st['game']})")
    here = st["here"]
    check(st["circle"] == here + 1, f"status: on Circle {st['circle'] - 1}, this room's point {here}")
    pts = s["points"]
    ram = t.flags()[SAVE_POINTS:SAVE_POINTS + USED]
    check([p["state"] for p in pts] == list(ram), "status: the points' states are savePointStates")
    check(pts[here]["current"] and all(p["cost"] == 0 for p in pts), "status: free, this room's Circle current")
    other = nearest_other(s)
    check((other is not None) == (s["why"] == ""), f"status: the Travel button: '{s['why']}' (another: {other})")
    check(s["menu_bits"] == 1 and t.settings_bytes()[MENU_FLAGS] & 1 == 0,
          "status: the Magic command shown (menu_bits 1), menuFlags bit 0 not written")
    if other is not None:
        raw = pts[other]["raw"]
        for mode, want in (("flat", 15), ("scaled", int(raw * 0.5 + 0.5)), ("game", raw), ("free", 0)):
            got = t.ft(set={"cost": mode})["points"][other]["cost"]
            check(got == want, f"status: cost {mode}: Circle {other} {got} MP ({want})")
    s = t.ft(set={"enabled": False})
    check(s["whose"] == "off" and s["unlock"] == 0 and s["why"] and s["menu_bits"] == 0,
          f"status: qol_fast_travel 0: off ('{s['why']}')")
    t.ft(set={"enabled": True})


def case_audit(t, check, args):
    s = t.ft(audit=True)
    bad = [a for a in s.get("audit", []) if not a["ok"]]
    check(len(s.get("audit", [])) == USED and not bad,
          f"audit: every destination's room has its Circle on the arrival cell (failing: {bad[:4]})")


SHARED = ("status", "audit")
CASES = {"status": case_status, "audit": case_audit}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(cf.default_save()))
    parser.add_argument("--menus", choices=("off", "on"), default="off")
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--out", help="keep screenshots here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / f"fast_travel_{args.menus}").resolve()
    out.mkdir(parents=True, exist_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)  # the defaults
    wanted = [c for c in args.cases.split(",") if c in CASES]
    failures = []

    def check(ok, message):
        print(("ok   " if ok else "FAIL ") + message)
        if not ok:
            failures.append(message)

    with cf.card_guard(out / "cards", source=args.card) as cards:
        lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
        env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0"}
        first = Trip("first", out, cards.card1, args.menus, env, bool(args.out))
        try:
            st = first.ft()["state"]
            if not st["circle"] or st["intro"] < 3:
                print(f"SKIP: the card's save room is not a Circle of Magic after the prologue "
                      f"(circle {st['circle']}, introState {st['intro']})")
                return 77
            for name in wanted:
                if name in SHARED:
                    CASES[name](first, check, args)
        finally:
            first.close()
        for name in wanted:
            if name in SHARED:
                continue
            card = out / f"{name}-card1.mcd"
            shutil.copyfile(cards.card1, card)
            t = Trip(name, out, card, args.menus, env, bool(args.out))
            try:
                CASES[name](t, check, args)
            except TimeoutError as e:
                check(False, f"{name}: {e}")
            finally:
                t.close()
    print(f"{len(failures)} failure(s)" if failures else "PASS: fast travel")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `menu0_levelled_smoke` suite (line 272) add:

```python
        *([Suite("fast_travel_smoke", [PY, str(TESTS / "fast_travel_smoke.py"), "--card", str(args.save)], 900,
                 note="fast travel (original menus): unlock from the first Circle, free cost, R6 audit, Room Browser "
                      "hygiene, travel then save then decode then Continue, refusals, the game's own unlock, the "
                      "Travel button, the prompt")]
          if args.save.exists() else []),
```

and add `"fast_travel_smoke"` to `CHECKPOINT_SUITES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 fast_travel_smoke
```

Expected: FAIL (`RpcError`: unknown method `fast_travel`).

- [ ] **Step 3: The settings**

In `pc/src/project_settings.cpp`, after `int pc_qol_confirm_cross, pc_qol_field_cross_attack;` (line 65) add:

```cpp
/* Fast travel (fast_travel.h, pc/docs/Fast_Travel.md): qol_fast_travel offers
 * the game's Teleportation from the first Circle of Magic once the prologue is
 * over (on by default; the game's own unlock always applies); before the
 * game's own unlock it costs qol_fast_travel_cost 0 free (default), 1 flat
 * (qol_fast_travel_flat MP), 2 scaled (qol_fast_travel_scale x the game's), 3
 * the game's. The Travel prompt on a Circle: qol_fast_travel_prompt, its
 * centre (fractions of the window) and size. */
int pc_qol_fast_travel = 1, pc_qol_fast_travel_cost = 0, pc_qol_fast_travel_flat = 15, pc_qol_fast_travel_prompt = 1;
float pc_qol_fast_travel_scale = 0.5f, pc_qol_fast_travel_prompt_x = 0.5f, pc_qol_fast_travel_prompt_y = 0.84f,
    pc_qol_fast_travel_prompt_scale = 1.0f;
```

and in `entries[]`, after `I("qol_skip_charging", pc_qol_skip_charging), /* 0 off, 1 fast, 2 skip */` add:

```cpp
    I("qol_fast_travel", pc_qol_fast_travel),
    I("qol_fast_travel_cost", pc_qol_fast_travel_cost), /* 0 free, 1 flat, 2 scaled, 3 game */
    I("qol_fast_travel_flat", pc_qol_fast_travel_flat),
    F("qol_fast_travel_scale", pc_qol_fast_travel_scale),
    I("qol_fast_travel_prompt", pc_qol_fast_travel_prompt),
    F("qol_fast_travel_prompt_x", pc_qol_fast_travel_prompt_x),
    F("qol_fast_travel_prompt_y", pc_qol_fast_travel_prompt_y),
    F("qol_fast_travel_prompt_scale", pc_qol_fast_travel_prompt_scale),
```

- [ ] **Step 4: The Circle objects in the room catalog (the audit's data)**

In `pc/src/room_warp.h`, `pc_room_info`, after `int floor_w, floor_h; ...` add:

```c
    /* its Circle of Magic (MPD section 9, _mpdRoomSection9 of type 16): the
     * save point id it reports (func_8008A4FC: point + 1), 0 none; its cell
     * x, z and level */
    int circle_id;
    int circle_cell[3];
```

In `pc/src/room_catalog.c`, `build_catalog`, change the section loop and add the read after the floor grid's:

```c
            unsigned at = room_offset + 0x60, door_at = 0, door_len = 0, scene_at = 0, floor_at = 0, objects_at = 0,
                     objects_len = 0;
            for (int s = 0; s < 24; s++) {
                unsigned len = le32(lengths + 4 * s);
                if (s == 1 && len >= 4)
                    floor_at = at; /* the collision map: width, height (cells) */
                if (s == 3)
                    door_at = at, door_len = len;
                if (s == 8)
                    objects_at = at, objects_len = len; /* section 9: 12-byte objects */
                if (s == 10 && len >= 0x52)
                    scene_at = at + 0x50;
                at += len;
            }
```

and after the `if (self && floor_at && ...) { ... }` block:

```c
            if (self && objects_len >= 12 && objects_len <= 12 * 256 && objects_at + objects_len <= mpd_size) {
                /* _mpdRoomSection9: x, z, level, type (16: a Circle of Magic), lock, id (unkA) */
                unsigned char* objects = read_bytes(mpd, objects_at, objects_len);
                for (unsigned o = 0; objects && o < objects_len / 12 && !self->circle_id; o++) {
                    const unsigned char* e = objects + 12 * o;
                    if (le16(e + 6) == 16) {
                        self->circle_id = e[10];
                        self->circle_cell[0] = (short)le16(e);
                        self->circle_cell[1] = (short)le16(e + 2);
                        self->circle_cell[2] = (short)le16(e + 4);
                    }
                }
                free(objects);
            }
```

- [ ] **Step 5: The game side**

Create `pc/src/fast_travel.c`:

```c
/* Fast travel, game side (fast_travel.h; the rules: fast_travel_rules.c;
 * pc/docs/Fast_Travel.md). Compiled with the game headers (battle builds).
 * The src hooks (ROOD_PC) call:
 *   MENU0 15AC.c     pc_fast_travel_unlock (for unkB5), _cost, _mask,
 *                    _blocked; vs_battle_shortcutInvoked PC_SHORTCUT_TRAVEL
 *   BATTLE 5BF94.c   func_800C930C: pc_fast_travel_menu_bits, _mask
 *   BATTLE 146C.c    func_800735F8 case 10: pc_fast_travel_begin
 * and room_warp.c calls pc_fast_travel_poll from its field poll (vs_battle_exec
 * case 1, after the event trigger, before doors and Triangle) and
 * pc_fast_travel_room_loaded from its room hook. Nothing here writes unkB5,
 * menuFlags, savePointStates or magicUsed. */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../src/SLUS_010.40/main.h"
#include "fast_travel.h"
#include "room_warp.h"

extern char D_800F4C34; /* an event script is running */
extern short D_800F4BE2; /* a script room change is pending */
extern char vs_main_locationZoneMapIds[]; /* 512 x (zone, room) */
void func_8007357C(int arg0, int arg1);
u_int func_800A0BE0(int actorId); /* (as room_warp.c declares it) */
int VSync(int mode);
int pc_window_travel_held(void);        /* boot_window.c: the Travel binding is held (focused) */
unsigned pc_window_travel_shared(void); /* boot_window.c: PS1 buttons on the same key or pad element */

unsigned pc_fast_travel_prompt_drawn; /* fast_travel_prompt.cpp counts the frames it drew */

static int held_before;
static int rpc_press; /* RPC fast_travel {press}: VSync(-1) + 1 when asked */
static unsigned presses, opened, ignored;
static int prompt_ok, prompt_vsync;
static struct {
    int pending, loaded;
    pc_ft_trip t;
} trip;
static pc_ft_trip last;

static pc_ft_settings settings(void)
{
    pc_ft_settings c;
    c.enabled = pc_qol_fast_travel;
    c.cost = pc_qol_fast_travel_cost;
    c.flat = pc_qol_fast_travel_flat;
    c.scale = pc_qol_fast_travel_scale;
    return c;
}

static vs_battle_actor2* ashley(void)
{
    return vs_battle_characterState ? psx_get(vs_battle_characterState->unk3C) : NULL;
}

static void fill(pc_ft_state* s)
{
    vs_battle_actor2* a = ashley();
    int i, phase = pc_room_warp_phase ? pc_room_warp_phase() : 0;
    u_int status = a ? func_800A0BE0(0) : 0;
    s->game = vs_main_stateFlags.unkB5;
    s->intro = vs_main_stateFlags.introState;
    s->special = vs_main_stateFlags.puzzleModeDisabled != 0;
    s->warped = (pc_room_warp_warped && pc_room_warp_warped(NULL, NULL)) || (phase >= 1 && phase <= 3);
    s->no_save = vs_main_stateFlags.unkA7 != 0;
    s->circle = a ? func_8008A4FC() : 0;
    s->here = a ? pc_ft_here(vs_battle_getCurrentRoomId()) : PC_FT_POINTS;
    s->event = D_800F4C34 != 0 || D_800F4BE2 != 0 || vs_battle_screenTransitionStep != 0
        || vs_main_stateFlags.gameOver != 0;
    s->busy = !a || a->unk954 != 0 || (status & 0x4000) || (vs_main_buttonsState & 3); /* L2, R2 */
    s->mask = a ? (unsigned)func_800CABE0(0) : ~0u;
    s->mp = a ? a->currentMP : 0;
    for (i = 0; i < PC_FT_POINTS; ++i)
        s->states[i] = (unsigned char)vs_main_stateFlags.savePointStates[i];
}

/* ---- the src hooks ---- */

int pc_fast_travel_unlock(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_unlock(&c, &s);
}

int pc_fast_travel_cost(int raw)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_cost(&c, &s, raw);
}

unsigned pc_fast_travel_mask(unsigned bits)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_mask(&c, &s, bits);
}

unsigned pc_fast_travel_menu_bits(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_menu_bits(&c, &s);
}

const char* pc_fast_travel_why(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    return pc_ft_blocked(&c, &s);
}

int pc_fast_travel_blocked(void) { return pc_fast_travel_why() != NULL; }

/* func_800735F8 case 10, before func_8007C694 and the MP charge: the trip to
 * check on arrival; whether it counts as magic (the game's unlock only). */
int pc_fast_travel_begin(int packed)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    fill(&s);
    trip.t.valid = 1;
    trip.t.location = packed & 0x1FF;
    trip.t.to = pc_ft_here(trip.t.location);
    trip.t.from = s.here;
    trip.t.whose = pc_ft_whose(&c, &s);
    trip.t.counted = pc_ft_counts_as_magic(&s);
    trip.t.arrived = trip.t.circle = trip.t.room_ok = 0;
    trip.t.mp_before = s.mp;
    trip.t.mp_after = 0;
    trip.pending = 1;
    trip.loaded = trip.t.location == vs_battle_getCurrentRoomId(); /* the same room: nothing loads */
    pc_ft_log_trip(&trip.t);
    return trip.t.counted;
}

void pc_fast_travel_room_loaded(void)
{
    if (trip.pending)
        trip.loaded = 1;
}

/* The first field frame after the trip's room load with no event running:
 * where Ashley stands (logged, reported; nothing retried or written). */
static void arrival(void)
{
    vs_battle_actor2* a = ashley();
    if (!trip.pending || !trip.loaded || D_800F4C34 != 0 || vs_battle_screenTransitionStep != 0)
        return;
    trip.pending = 0;
    trip.t.circle = a ? func_8008A4FC() : 0;
    trip.t.room_ok = vs_battle_getCurrentRoomId() == trip.t.location;
    trip.t.arrived = trip.t.room_ok && trip.t.circle == trip.t.to + 1;
    trip.t.mp_after = a ? a->currentMP : 0;
    last = trip.t;
    pc_ft_log_arrival(&last);
}

void pc_fast_travel_press(void) { rpc_press = VSync(-1) + 1; }

/* room_warp.c's field poll (vs_battle_exec case 1): the arrival check, the
 * prompt's state, the Travel button. 1: the field loop breaks out (as after a
 * door). The Travel button's entry into MENU0 comes with its src hook (the
 * shortcut id would reach handleShortcut's string index otherwise). */
int pc_fast_travel_poll(void)
{
    pc_ft_settings c = settings();
    pc_ft_state s;
    const char* why;
    int held = pc_window_travel_held(), now = VSync(-1);
    int pressed = (held && !held_before) || (rpc_press && now - (rpc_press - 1) <= 2);
    held_before = held;
    rpc_press = 0;
    arrival();
    fill(&s);
    why = pc_ft_shortcut(&c, &s);
    prompt_ok = why == NULL;
    prompt_vsync = now;
    if (!pressed)
        return 0;
    ++presses;
    ++ignored; /* (Task 10 opens the list here) */
    return 0;
}

int pc_fast_travel_prompt(void)
{
    return pc_qol_fast_travel_prompt && prompt_ok && VSync(-1) - prompt_vsync <= 2;
}

/* ---- RPC fast_travel ---- */

static void audit_points(pc_ft_report* r)
{
    int i, k, n = pc_rooms_count ? pc_rooms_count() : 0;
    for (i = 0; i < PC_FT_USED; ++i) {
        pc_ft_audit_point* a = &r->audit[i];
        int d = pc_ft_save_point_data[i], loc = d & 0x1FF;
        a->zone = (u_char)vs_main_locationZoneMapIds[loc * 2];
        a->room = (u_char)vs_main_locationZoneMapIds[loc * 2 + 1];
        a->circle_id = a->cell[0] = a->cell[1] = a->cell[2] = 0;
        for (k = 0; k < n; ++k) {
            const pc_room_info* room = pc_rooms_get(k);
            if (room && room->zone == a->zone && room->room == a->room) {
                a->circle_id = room->circle_id;
                a->cell[0] = room->circle_cell[0];
                a->cell[1] = room->circle_cell[1];
                a->cell[2] = room->circle_cell[2];
            }
        }
        a->ok = a->circle_id == i + 1 && a->cell[0] == ((d >> 16) & 0x1F) && a->cell[1] == ((d >> 24) & 0x1F)
            && a->cell[2] == 0;
    }
    r->audited = 1;
}

void pc_fast_travel_report(pc_ft_report* r, int audit)
{
    r->settings = settings();
    fill(&r->state);
    r->prompt = pc_fast_travel_prompt();
    r->prompt_on = pc_qol_fast_travel_prompt;
    r->presses = presses;
    r->opened = opened;
    r->ignored = ignored;
    r->prompt_drawn = pc_fast_travel_prompt_drawn;
    r->last = last;
    r->audited = 0;
    if (audit)
        audit_points(r);
}
```

(`opened` stays 0 until Task 10; `-Wunused` does not apply to a file-scope variable that is read.)

In `pc/tools/build_boot.py`, in the battle list after `("pc/src/room_warp.c", "room_warp"),` add `("pc/src/fast_travel.c", "fast_travel"),`.

`pc_window_travel_held` / `pc_window_travel_shared` come with Task 9; until then add temporary definitions at the end of `pc/src/boot_window.c` (Task 9 replaces them):

```c
/* The Travel binding (fast travel; Task 9 of the plan gives it its keys and pad element). */
int pc_window_travel_held(void) { return 0; }
unsigned pc_window_travel_shared(void) { return 0; }
```

- [ ] **Step 6: The field poll and the room hook call it**

In `pc/src/room_warp.c`, after the prototypes block (`extern int pc_room_warp_enemies, pc_room_warp_scripts;`) add:

```c
int pc_fast_travel_poll(void);       /* fast_travel.c */
void pc_fast_travel_room_loaded(void);
```

In `pc_room_warp_poll`, change

```c
    if (pc_room_warp_finish(D_800F1AB0.zndId, D_800F1AB0.mpdId)) {
        /* The first field frame there: the map screen keeps no trace of it. */
        vs_main_mapStatus = map_before;
        return 0;
    }
    if (!pc_room_warp_pending())
        return 0;
```

to

```c
    if (pc_room_warp_finish(D_800F1AB0.zndId, D_800F1AB0.mpdId)) {
        /* The first field frame there: the map screen keeps no trace of it. */
        vs_main_mapStatus = map_before;
        return 0;
    }
    if (pc_fast_travel_poll()) /* the Travel button on a Circle of Magic (fast_travel.c) */
        return 1;
    if (!pc_room_warp_pending())
        return 0;
```

and in `pc_room_warp_room_loaded`, as its first line:

```c
    pc_fast_travel_room_loaded(); /* a fast travel trip arrives (fast_travel.c) */
```

- [ ] **Step 7: RPC fast_travel**

In `pc/src/dev_rpc.c`, add `#include "fast_travel.h"` after `#include "camera_stick.h"`, and after the `#include` block:

```c
/* fast_travel.c (battle builds) */
extern void pc_fast_travel_report(pc_ft_report* r, int audit) __attribute__((weak));
extern void pc_fast_travel_press(void) __attribute__((weak));
```

In the method list comment at the top, after the `skip_charging` entry, add:

```c
 *   fast_travel {set, press, audit}  fast travel (fast_travel.c): set {enabled,
 *                                cost (0..3 or free / flat / scaled / game),
 *                                flat, scale, prompt} (settings qol_fast_travel*);
 *                                press: the Travel button once, as its key or
 *                                pad button; audit: each destination's Circle
 *                                object read from the disc; replies settings,
 *                                whose unlock (off / port / game), unlock, the
 *                                state, blocked and why (the Travel button's
 *                                reason, "" when it opens the list), each
 *                                point's state / raw / cost / current /
 *                                refused, counters, the last trip (battle builds)
```

In the dispatch, after the `skip_charging` branch, add:

```c
    } else if (!strcmp(method, "fast_travel")) {
        /* Fast travel (fast_travel.c, fast_travel_rules.c pc_ft_json). */
        static pc_ft_report report;
        const char* set = params ? member(params, "set") : NULL;
        int flag, audit = 0, press = 0;
        double real;
        if (!pc_fast_travel_report) {
            fail(fd, id, -32000, "fast_travel needs the battle build");
            return;
        }
        if (set) {
            char name[16] = "";
            if (bool_value(member(set, "enabled"), &flag))
                pc_qol_fast_travel = flag;
            if (number_value(member(set, "cost"), &n) && n >= 0 && n < PC_FT_COST_MODES)
                pc_qol_fast_travel_cost = (int)n;
            else if (string_value(member(set, "cost"), name, sizeof name)) {
                int k;
                for (k = 0; k < PC_FT_COST_MODES && strcmp(name, pc_ft_cost_names[k]); k++)
                    ;
                if (k == PC_FT_COST_MODES) {
                    fail(fd, id, -32602, "fast_travel cost: 0..3 or free, flat, scaled, game");
                    return;
                }
                pc_qol_fast_travel_cost = k;
            }
            if (number_value(member(set, "flat"), &n) && n >= 0 && n <= PC_FT_COST_MAX)
                pc_qol_fast_travel_flat = (int)n;
            if (real_value(member(set, "scale"), &real) && real >= 0 && real <= 10)
                pc_qol_fast_travel_scale = (float)real;
            if (bool_value(member(set, "prompt"), &flag))
                pc_qol_fast_travel_prompt = flag;
        }
        if (params && bool_value(member(params, "press"), &press) && press && pc_fast_travel_press)
            pc_fast_travel_press();
        if (params)
            bool_value(member(params, "audit"), &audit);
        pc_fast_travel_report(&report, audit);
        if (pc_ft_json(&report, result, (int)sizeof result) < 0) {
            fail(fd, id, -32000, "fast_travel: the report does not fit");
            return;
        }
        reply(fd, id, result);
```

(`n` is the dispatch's `long long n`, as the `camera_options` branch uses it; `member` / `bool_value` / `number_value` / `string_value` / `real_value` are the file's helpers; the RPC is polled on the main thread at vsync, so reading the game's state here is safe.)

- [ ] **Step 8: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 fast_travel_smoke
```

Run with `--cases status,audit` by hand if the suite runs the other (not yet written) cases: `flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases status,audit`.

Expected: every `ok` line, `PASS: fast travel`. If the audit reports failing points, list them; each failing point is either an audit bug (the cell or id read differently: check `func_8008D438` / `func_8008A4FC` again) or a destination for Task 3b.

- [ ] **Step 9: Commit**

```bash
git add pc/src/fast_travel.c pc/src/project_settings.cpp pc/src/room_warp.c pc/src/room_warp.h pc/src/room_catalog.c \
    pc/src/dev_rpc.c pc/src/boot_window.c pc/tools/build_boot.py pc/tests/fast_travel_smoke.py pc/tests/run.py
git commit -m "Fast travel: game side (state, hooks, arrival check), settings, RPC fast_travel, the R6 audit from the disc

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Warp hygiene: a Room Browser warp never records a Circle (R7)

**Files:**
- Modify: `pc/src/room_warp.c`, `pc/tests/fast_travel_smoke.py`

- [ ] **Step 1: Write the failing case**

In `pc/tests/fast_travel_smoke.py`, before `SHARED = ...` add:

```python
def case_warp(t, check, args):
    g = t.g
    s = t.ft()
    before = t.flags()[SAVE_POINTS:SAVE_POINTS + 48]
    origin = (g.state()["zone"], g.state()["room"])
    unfound = [i for i in range(USED) if s["points"][i]["state"] == 0]
    target = None
    for k in unfound:
        zone, room = t.room_of(DATA[k] & 0x1FF)
        r = g.call("room_goto", zone=zone, room=room, enemies=False, scripts=True)
        if r["ok"]:
            target = k
            break
    if target is None:
        check(True, "warp: no unfound Circle's room to warp to (skipped)")
        return
    t.wait(lambda: (g.state()["zone"], g.state()["room"]) == (zone, room) and g.call("rooms")["phase"] in (0, 4),
           1800, "the warp arrives", step=4)
    g.run(120)  # its entry script runs (a Circle's records it)
    if t.flags()[SAVE_POINTS + target] == 0:
        t.set_flag(SAVE_POINTS + target, 1)  # as its script would (with the Circle's own event already played)
        print(f"info: warp: Circle {target}'s script did not record it here; recorded by the test")
    s = t.ft()
    check(s["state"]["warped"] == 1 and s["blocked"].startswith("Leave the Room Browser"),
          f"warp: R7 while warped: blocked '{s['blocked']}'")
    check(s["why"] != "", f"warp: the Travel button refused while warped ('{s['why']}')")
    g.call("room_goto", action="return")
    t.wait(lambda: not g.call("room_goto", action="status")["warped"] and g.call("rooms")["phase"] in (0, 4)
           and (g.state()["zone"], g.state()["room"]) == origin, 1800, "back where the warps started", step=4)
    g.run(30)
    after = t.flags()[SAVE_POINTS:SAVE_POINTS + 48]
    check(after == before, f"warp: savePointStates as before the warps (Circle {target}: {before[target]} -> "
                           f"{after[target]})")
    check(t.ft()["state"]["warped"] == 0 and t.ft()["blocked"] == "", "warp: travel offered again")
```

and add `"warp": case_warp` to `CASES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases warp
```

Expected: `FAIL warp: savePointStates as before the warps (Circle k: 0 -> 1)`.

- [ ] **Step 3: Snapshot and restore**

In `pc/src/room_warp.c`, add after the fast travel prototypes:

```c
int pc_room_warp_warped(int* zone, int* room); /* room_catalog.c */
int pc_room_warp_phase(void);

/* savePointStates when a chain of warps started. A warp with scripts on runs
 * the room's entry script, and a Circle's room script records the Circle
 * (op 0x50: savePointStates[i] = 1), which would make a Room Browser visit a
 * fast travel destination. Put back once the warps end: back where they
 * started, or the warped state cleared ("keep this room" keeps the room, not
 * the Circles found while warped). */
static char points_before[48];
static int points_held;

static void points_restore(void)
{
    int i, phase = pc_room_warp_phase();
    if (!points_held || pc_room_warp_warped(NULL, NULL) || (phase >= 1 && phase <= 3))
        return;
    for (i = 0; i < 48; ++i)
        vs_main_stateFlags.savePointStates[i] = points_before[i];
    points_held = 0;
}
```

In `pc_room_warp_poll`, after the `bench_enemies` line add `points_restore();`, and change

```c
    if (!pc_room_warp_take(D_800F1AB0.zndId, D_800F1AB0.mpdId, (unsigned char*)&door))
        return 0;
    map_before = vs_main_mapStatus;
```

to

```c
    {
        int chain = pc_room_warp_warped(NULL, NULL); /* (before the take: a new chain or the next hop) */
        int i;
        if (!pc_room_warp_take(D_800F1AB0.zndId, D_800F1AB0.mpdId, (unsigned char*)&door))
            return 0;
        if (!chain && !points_held) {
            for (i = 0; i < 48; ++i)
                points_before[i] = vs_main_stateFlags.savePointStates[i];
            points_held = 1;
        }
    }
    map_before = vs_main_mapStatus;
```

- [ ] **Step 4: Run it, and the Room Browser's own smoke**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases warp
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 room_browser_smoke
```

Expected: `ok   warp: ...` lines and `PASS: fast travel`; room_browser_smoke passes.

- [ ] **Step 5: Commit**

```bash
git add pc/src/room_warp.c pc/tests/fast_travel_smoke.py
git commit -m "Room Browser: savePointStates put back when a chain of warps ends (a warp never records a Circle)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 6: The src hooks: the unlock, the cost, the mask, the Magic command, magicUsed

**Files:**
- Modify: `src/MENU/MENU0.PRG/15AC.c`, `src/BATTLE/BATTLE.PRG/5BF94.c`, `src/BATTLE/BATTLE.PRG/146C.c`, `pc/tests/fast_travel_smoke.py`

- [ ] **Step 1: Write the failing case (the original list, free, travel)**

In `pc/tests/fast_travel_smoke.py`, before `SHARED = ...` add:

```python
def open_list(t):
    """Triangle > Magic > TELEPORTATION (the original menus): MENU0's destinations list up."""
    g = t.g
    for _ in range(4):  # Triangle is ignored while an action still runs
        g.press("triangle", vsyncs=3, settle=10)
        if t.wait(lambda: MAGIC_ITEM in t.items(), 60, "", step=4, quiet=True) is not None:
            break
    t.wait_ready(MAGIC_ITEM)
    t.nav(range(0, 10), MAGIC_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    t.wait_ready(TELEPORT_ITEM)
    t.nav(range(10, 15), TELEPORT_ITEM)
    g.press("circle", vsyncs=3, settle=10)
    wait_list(t)


def wait_list(t):
    t.wait(lambda: t.menu0_state() == TELEPORT_MENU and LIST_ITEM in t.items(), 180, "the destinations list")
    t.wait_ready(LIST_ITEM)


def pick(t, rows, point):
    """MENU0's list: the cursor to `point`'s row, Circle; the hand-off (unk2, executeAbility)."""
    if len(rows) > 8:
        raise TimeoutError(f"{len(rows)} Circles found: the list pages (not driven here)")
    t.nav(range(LIST_ITEM, LIST_ITEM + len(rows)), LIST_ITEM + rows.index(point))
    t.g.press("circle", vsyncs=3, settle=1)
    t.wait(lambda: t.handoff()[0] == 0xA, 120, "the hand-off (unk2 0xA)", step=1)
    return t.handoff()


def mp_drop(t, limit=150):
    """MP regenerates meanwhile: the step where it goes down (case 10's charge), 0 when none."""
    last, drop = [t.mp()], []

    def seen():
        v = t.mp()
        if v < last[0] and not drop:
            drop.append(last[0] - v)
        last[0] = v
        return bool(drop)
    t.wait(seen, limit, "", step=1, quiet=True)
    return drop[0] if drop else 0


def arrive(t, target, limit=1800):
    loc = DATA[target] & 0x1FF

    def there():
        if t.menu() != CLOSED or t.location() != loc:
            return False
        last = t.ft()["last"]
        return last["valid"] and last["to"] == target
    t.wait(there, limit, f"arrival at Circle {target}", step=4)
    t.g.run(60)
    return t.ft()


def travel_to(t, check, args, target, what):
    """Through the menus to `target`: (the report after arrival, the MP charged)."""
    s = t.ft()
    rows = [p["i"] for p in s["points"] if p["state"]]
    if args.menus == "off":
        open_list(t)
        unk2, ability = pick(t, rows, target)
    else:
        unk2, ability = shell_travel(t, target)
    want = packed(target, s["points"][target]["cost"])
    check(unk2 == 0xA and ability == want,
          f"{what}: MENU0's hand-off: unk2 {unk2:#x}, executeAbility {ability:#x} ({want:#x})")
    drop = mp_drop(t)
    return arrive(t, target), drop


def shell_travel(t, target):
    raise TimeoutError("the modern menus' Magick > Teleport comes with Task 13")


def case_menu(t, check, args):
    g = t.g
    s = t.ft()
    here, target = s["state"]["here"], nearest_other(s)
    if target is None:
        check(True, "menu: no other Circle found (skipped)")
        return
    rows = [p["i"] for p in s["points"] if p["state"]]
    t.set_mp(t.mp() // 2 + 1)  # MP short of the game's cost would grey nothing: free
    flags0, settings0 = t.flags(), t.settings_bytes()
    if args.menus == "off":
        open_list(t)
        items = t.items()
        greyed = [items.get(LIST_ITEM + r, (0, 0, 9))[2] for r in range(len(rows))]
        check(not any(greyed), f"menu: no row greyed (free, MP {t.mp()}): {greyed}")
        check(t.selected(range(LIST_ITEM, LIST_ITEM + len(rows))) == LIST_ITEM + rows.index(here),
              "menu: the cursor starts on this Circle (MENU0's cost-15 row)")
        t.shot("list")
        unk2, ability = pick(t, rows, target)
        check(unk2 == 0xA and ability == packed(target, 0),
              f"menu: MENU0's hand-off: unk2 {unk2:#x}, executeAbility {ability:#x} ({packed(target, 0):#x})")
        drop = mp_drop(t)
        s = arrive(t, target)
    else:
        s, drop = travel_to(t, check, args, target, "menu")
    last = s["last"]
    check(last["arrived"] and last["circle"] == target + 1 and last["room_ok"],
          f"menu: arrived on Circle {target} ({last})")
    check(last["whose"] == "port" and last["counted"] == 0 and drop == 0,
          f"menu: the port's unlock, free (MP charged {drop}), not counted as magic")
    flags = t.flags()
    check(flags[MAGIC_USED] == flags0[MAGIC_USED], f"menu: magicUsed {flags0[MAGIC_USED]} -> {flags[MAGIC_USED]}")
    check(flags[UNK_B5] == 0 and flags[SAVE_POINTS:SAVE_POINTS + 48] == flags0[SAVE_POINTS:SAVE_POINTS + 48],
          "menu: unkB5 and savePointStates unchanged")
    check(t.settings_bytes()[MENU_FLAGS:MENU_FLAGS + 2] == settings0[MENU_FLAGS:MENU_FLAGS + 2],
          "menu: menuFlags unchanged")
    check(not g.call("room_goto", action="status")["warped"], "menu: not a Room Browser warp")
    t.shot("arrived")
```

and add `"menu": case_menu` to `CASES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases menu
```

Expected: `FAIL menu: menu item 0 ready: not after 400 vsyncs` (the test card's Ashley has no spell: no Magic command), or the Teleportation row missing (`menu item 14`).

- [ ] **Step 3: MENU0 15AC.c**

After `#include "build/assets/MENU/MENU0.PRG/teleportation.h"` add:

```c

#ifdef ROOD_PC
/* Fast travel (pc/src/fast_travel.c, pc/docs/Fast_Travel.md): Teleportation
 * offered from the first Circle of Magic (the port's unlock beside the game's
 * unkB5), its cost mode, the port's refusals greying the row, the Travel
 * button's entry straight into the destinations list. */
#include "pc/src/fast_travel.h"
#define TELEPORT_UNLOCK pc_fast_travel_unlock()
#define TELEPORT_COST(cost) pc_fast_travel_cost(cost)
#define TELEPORT_MASK pc_fast_travel_mask(0xB7)
#else
#define TELEPORT_UNLOCK vs_main_stateFlags.unkB5
#define TELEPORT_COST(cost) (cost)
#define TELEPORT_MASK 0xB7
#endif
```

In `_teleportMenu`:
- `(savePointState == 2) | (currentMp < teleportCosts[rowCount]);` becomes `(savePointState == 2) | (currentMp < TELEPORT_COST(teleportCosts[rowCount]));`
- `        if (D_800F4EA0 & 0xB7) {` (the line before `for (i = 0; i < 48; ++i) {` / `rowTypes[i] |= 1;`) becomes `        if (D_800F4EA0 & TELEPORT_MASK) {`
- `i = teleportCosts[vs_mainMenu_getConfirmedRow()];` becomes `i = TELEPORT_COST(teleportCosts[vs_mainMenu_getConfirmedRow()]);`
- `v1 = teleportCosts[selectedRow - 1] << 0x13;` becomes `v1 = TELEPORT_COST(teleportCosts[selectedRow - 1]) << 0x13;`

(`teleportCosts[]` itself and `if (teleportCosts[rowCount] == 15)` stay: the current circle's sentinel.)

In `vs_menu0_exec`, `initSubmenu`:
- `if (vs_main_stateFlags.unkB5 != 0) {` becomes `if (TELEPORT_UNLOCK != 0) {`
- both `(func_8008A4FC() != 0) && (vs_main_stateFlags.unkB5 == 2)` become `(func_8008A4FC() != 0) && (TELEPORT_UNLOCK == 2)` (Edit with `replace_all` on `(vs_main_stateFlags.unkB5 == 2)`; it also changes the third, in `rowTypes`, which the next edit rewrites)
- the row's type becomes:

```c
            rowTypes[i++] = ((j == 48) || (TELEPORT_UNLOCK == 2) || (func_8008A4FC() == 0)
#ifdef ROOD_PC
                             || pc_fast_travel_blocked() /* the port's rules (fast_travel.c) */
#endif
            );
```

On the PS1 build every macro expands to the original tokens.

- [ ] **Step 4: BATTLE 5BF94.c**

Change the ROOD_PC block before `func_800C930C`

```c
#ifdef ROOD_PC
/* Modern menus (pc/src/ui/menu_bridge.c): the shell replaces the top level
 * when the ui_menus setting is 1. */
extern int pc_ui_menu_open(int mode);
extern int pc_ui_menu_top(void);
extern int pc_ui_menu_active(void);
extern int pc_ui_menu_hide_rows(void);
#endif
```

to

```c
#ifdef ROOD_PC
/* Modern menus (pc/src/ui/menu_bridge.c): the shell replaces the top level
 * when the ui_menus setting is 1. */
extern int pc_ui_menu_open(int mode);
extern int pc_ui_menu_top(void);
extern int pc_ui_menu_active(void);
extern int pc_ui_menu_hide_rows(void);
/* Fast travel (pc/src/fast_travel.c): the Magic command on a Circle of Magic
 * once the port offers Teleportation (shown, never stored in menuFlags), and
 * MP 0 not greying it while travel is free. */
extern unsigned pc_fast_travel_menu_bits(void);
extern unsigned pc_fast_travel_mask(unsigned bits);
#define MAGIC_COMMAND_MASK pc_fast_travel_mask(0xB7)
#else
#define MAGIC_COMMAND_MASK 0xB7
#endif
```

In `func_800C930C`, change

```c
#ifdef ROOD_PC
    if (!mode && pc_ui_menu_active()) {
```

to

```c
#ifdef ROOD_PC
    enabled |= pc_fast_travel_menu_bits();
    if (!mode && pc_ui_menu_active()) {
```

and `items[0].unselectable = (mask & 0xB7) != 0;` to `items[0].unselectable = (mask & MAGIC_COMMAND_MASK) != 0;`.

- [ ] **Step 5: BATTLE 146C.c case 10**

Change

```c
        func_8006F4B0(&sp18.unk0);
        vs_battle_setStateFlag(0xB7, 1);
        func_8007C694(arg0->executeAbility.fields.unk0_0,
```

to

```c
        func_8006F4B0(&sp18.unk0);
#ifdef ROOD_PC
        {
            /* Fast travel (pc/src/fast_travel.c): the trip, and magicUsed only
             * when the game itself has taught Teleportation (unkB5). */
            extern int pc_fast_travel_begin(int packed);
            if (pc_fast_travel_begin(arg0->executeAbility.s32)) {
                vs_battle_setStateFlag(0xB7, 1);
            }
        }
#else
        vs_battle_setStateFlag(0xB7, 1);
#endif
        func_8007C694(arg0->executeAbility.fields.unk0_0,
```

- [ ] **Step 6: Build, run the case and the probe**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases status,audit,warp,menu
```

Expected: all `ok`, `PASS: fast travel`.

- [ ] **Step 7: commit-check**

Run the commit-check from the ground rules. Expected: `All files match`.

- [ ] **Step 8: The existing magic suites still pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ui_magick menu0_levelled
```

Expected: both pass. (ui_magick_smoke pokes `unkB5 = 1` before its teleport checks: the game's unlock, stock behaviour. If its original-menus session fails on the school list now having a fifth row on the Circle, the failure belongs to Task 14's list: note it and go on.)

- [ ] **Step 9: Commit**

```bash
git add src/MENU/MENU0.PRG/15AC.c src/BATTLE/BATTLE.PRG/5BF94.c src/BATTLE/BATTLE.PRG/146C.c pc/tests/fast_travel_smoke.py
git commit -m "Fast travel: MENU0 reads the shared unlock, cost and mask; the Magic command shown on a Circle; magicUsed only for the game's unlock

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Travel, save, decode, Continue (the round trip)

**Files:**
- Create: `pc/tests/menu_nav.py`
- Modify: `pc/tests/ui_data_smoke.py` (`Session.open_data`'s original branch; a `menu7_driver` helper), `pc/tests/fast_travel_smoke.py`

- [ ] **Step 1: Write the failing case**

In `pc/tests/fast_travel_smoke.py`, before `SHARED = ...` add:

```python
def case_save(t, check, args):
    from ui_data_smoke import menu7_driver
    s = t.ft()
    target = nearest_other(s)
    if target is None:
        check(True, "save: no other Circle found (skipped)")
        return
    if args.menus == "off":
        rows = [p["i"] for p in s["points"] if p["state"]]
        open_list(t)
        pick(t, rows, target)
        s = arrive(t, target)
    else:
        s, _ = travel_to(t, check, args, target, "save")
    check(s["last"]["arrived"], f"save: on Circle {target} ({s['last']})")
    d = menu7_driver(t.g, "save", t.out, modern=args.menus == "on", card1=t.card)
    w0 = d.writes()
    d.open_data()
    d.chooser(0)       # Save
    d.card_rows(1)     # memory card 1
    d.file_rows(1)     # file 2 (new)
    d.confirm_pad()
    d.wait(lambda: d.writes() > w0, "the save written")
    t.g.run(30)
    d.press("circle", settle=10)  # "saved"
    d.back_out()
    d.close_menu()
    ram, settings = t.flags(), t.settings_bytes()
    zone, room = t.g.state()["zone"], t.g.state()["room"]
    image = bytearray(t.card.read_bytes())
    files = cf.saves(image)
    check(2 in files, f"save: file 2 written ({sorted(files)})")
    if 2 not in files:
        return
    plain = cf.decode(cf.read_file(image, files[2]))
    check(cf.checksums_ok(plain), "save: the checksums are valid")
    loc = cf.stats(plain)["location"]
    check(loc == target + 1, f"save: saveLocation {loc} (Circle {target} + 1)")
    check(bytes(plain[SAVE_FLAGS:SAVE_FLAGS + FLAGS_SIZE]) == ram,
          "save: stateFlags as RAM (unkB5, savePointStates, magicUsed as the game had them)")
    check(plain[SAVE_SETTINGS + MENU_FLAGS] & 1 == 0 and settings[MENU_FLAGS] & 1 == 0,
          "save: menuFlags bit 0 (Magic) not stored")
    check((plain[SAVE_ROOM], plain[SAVE_ROOM + 1]) == (zone, room),
          f"save: the room record (unk1778) {plain[SAVE_ROOM]}/{plain[SAVE_ROOM + 1]} is the destination {zone}/{room}")
    t.close()
    # Continue from file 2
    g = t.g = Game.launch(fast=True, card=str(t.card), log=t.out / "save-continue.log", extra_env=t.env)
    g.wait_title()
    g.press("start", vsyncs=4, settle=90)
    g.press("circle", vsyncs=3, settle=90)  # Continue
    g.press("circle", vsyncs=3, settle=90)  # memory card 1
    g.press("down", vsyncs=3, settle=30)    # file 2
    g.press("circle", vsyncs=3, settle=90)
    g.run(400)
    s = t.ft()
    check(t.location() == DATA[target] & 0x1FF and s["state"]["circle"] == target + 1,
          f"continue: on Circle {target} (location {t.location():#x}, standing on {s['state']['circle'] - 1})")
    check(t.flags()[UNK_A7] == 0, "continue: saving allowed (on a Circle, unkA7 0: MENU7's two conditions)")
    check(s["why"] == "" or nearest_other(s) is None, f"continue: travel offered again ('{s['why']}')")
```

and add `"save": case_save` to `CASES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases save
```

Expected: `ImportError: cannot import name 'menu7_driver'`.

- [ ] **Step 3: Top-level navigation by the game's cursor**

Create `pc/tests/menu_nav.py`:

```python
"""The original top level by the game's own cursor (vs_battle_submenuStates[0]),
not by counting rows: which commands it shows varies (a spell learned, fast
travel's Magic command on a Circle of Magic, pc/docs/Fast_Travel.md)."""
MAGIC_COMMAND, DATA_COMMAND = 0, 6  # the commands' rows (currentMenu = row + 1: Data is 7)


def top_command(game, sym, command, limit=16):
    """Down until the top level's cursor is on `command` (the menu open)."""
    for _ in range(limit):
        if game.read(sym["vs_battle_submenuStates"], 1)[0] == command:
            return
        game.press("down", vsyncs=3, settle=10)
    raise TimeoutError(f"the top level's cursor never reached command {command} "
                       f"(at {game.read(sym['vs_battle_submenuStates'], 1)[0]})")
```

In `pc/tests/ui_data_smoke.py`, add `from menu_nav import DATA_COMMAND, top_command  # noqa: E402` after `import card_fixtures as cf`, and in `Session.open_data` change

```python
        else:
            for _ in range(4):
                self.press("down", settle=10)
            self.press("circle", settle=2)
```

to

```python
        else:
            top_command(g, self.sym, DATA_COMMAND)
            self.press("circle", settle=2)
```

After `class Session` (before `def changed_ranges`) add:

```python
def menu7_driver(g, name, out, modern=False, shots=False, card1=None):
    """Session's MENU7 by pad (open_data, chooser, card_rows, file_rows,
    confirm_pad, back_out, close_menu, writes) on a game launched elsewhere
    (fast_travel_smoke.py)."""
    s = Session.__new__(Session)
    s.name, s.out, s.shots, s.modern, s.card1 = name, out, shots, modern, card1
    s.g, s.sym, s.m7 = g, symbols(), menu7_symbols()
    return s
```

- [ ] **Step 4: Run it, and ui_data_smoke**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases save
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ui_data_smoke
```

Expected: every `save:` / `continue:` line `ok`; ui_data_smoke passes (its original session now reaches Data through the Magic command's extra row).

If `continue:` lands in the first file's room, the load list's cursor did not start on file 1: replace the single `down` with a loop on MENU7's file cursor (`d.file_cursor()` from a `menu7_driver` on the new process) until it is 1.

- [ ] **Step 5: Commit**

```bash
git add pc/tests/menu_nav.py pc/tests/ui_data_smoke.py pc/tests/fast_travel_smoke.py
git commit -m "Fast travel: travel, save, decode, Continue: the destination's Circle, the game's own bytes, no Magic bit stored

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Refusals, the game's own unlock, the current Circle

**Files:**
- Modify: `pc/tests/fast_travel_smoke.py`

These cases test the rules already in place (Tasks 2, 4, 6): they should pass at once. A failure here is a bug in those tasks, fixed there test-first.

- [ ] **Step 1: Write the cases**

In `pc/tests/fast_travel_smoke.py`, before `SHARED = ...` add:

```python
def magic_rows(t):
    """Triangle: the top level's Magic row (None absent, "greyed", "on") and, through
    it, MENU0's Teleportation row (the same); the menu closed again."""
    g = t.g
    for _ in range(4):
        g.press("triangle", vsyncs=3, settle=10)
        if t.wait(lambda: t.menu() != CLOSED and t.items(), 60, "", step=4, quiet=True) is not None:
            break
    g.run(40)
    items = t.items()
    magic = None if MAGIC_ITEM not in items else ("greyed" if items[MAGIC_ITEM][2] else "on")
    teleport = None
    if magic == "on":
        t.nav(range(0, 10), MAGIC_ITEM)
        g.press("circle", vsyncs=3, settle=10)
        t.wait_ready(10)
        items = t.items()
        teleport = None if TELEPORT_ITEM not in items else ("greyed" if items[TELEPORT_ITEM][2] else "on")
    t.close_menu()
    return magic, teleport


def button_refused(t):
    """The Travel button pressed: nothing opened, the room kept, the press counted as ignored."""
    n, loc = t.ft()["ignored"], t.location()
    t.ft(press=True)
    t.g.run(8)
    return t.menu() == CLOSED and t.location() == loc and t.ft()["ignored"] == n + 1


def case_refuse(t, check, args):
    g, off = t.g, args.menus == "off"
    here = t.ft()["state"]["here"]
    t.set_mp(99, 99)
    # R3: the game refuses to save here now
    t.set_flag(UNK_A7, 1)
    g.run(2)
    s = t.ft()
    check(s["blocked"] == "Cannot travel from here now." and s["why"], f"refuse: R3 unkA7: '{s['blocked']}'")
    check(button_refused(t), "refuse: R3: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows == ("on", "greyed"), f"refuse: R3: MENU0's Teleportation row greyed {rows}")
    t.set_flag(UNK_A7, 0)
    # R1: this room's Circle not recorded (MENU0's cursor would have no current row)
    t.set_flag(SAVE_POINTS + here, 0)
    g.run(2)
    s = t.ft()
    check("not recorded" in s["blocked"], f"refuse: R1 this Circle unrecorded: '{s['blocked']}'")
    check(button_refused(t), "refuse: R1: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[1] == "greyed", f"refuse: R1: MENU0's Teleportation row greyed {rows}")
    t.set_flag(SAVE_POINTS + here, 1)
    # an event script running
    n = t.ft()["ignored"]
    g.write(t.sym["D_800F4C34"], b"\x01")
    t.ft(press=True)
    g.run(2)
    s = t.ft()
    g.write(t.sym["D_800F4C34"], b"\x00")
    g.run(6)
    check(s["state"]["event"] == 1 and s["why"] == "Not during an event." and t.menu() == CLOSED
          and t.ft()["ignored"] == n + 1, f"refuse: an event script: '{s['why']}'")
    # MP 0 with the game's cost: every other Circle refused, the Magic command greyed as the game's
    t.ft(set={"cost": "game"})
    t.set_mp(0)
    s = t.ft()
    check(s["why"] != "" and all(p["refused"] for p in s["points"] if p["i"] != here and p["state"] == 1),
          f"refuse: MP 0, the game's cost: refused ('{s['why']}')")
    check(button_refused(t), "refuse: MP 0, the game's cost: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[0] == "greyed", f"refuse: MP 0, the game's cost: the Magic command greyed {rows}")
    # ... and free: MP 0 greys nothing
    t.ft(set={"cost": "free"})
    s = t.ft()
    check(s["why"] == "" or nearest_other(s) is None, f"refuse: MP 0, free: offered ('{s['why']}')")
    if off:
        rows = magic_rows(t)
        check(rows == ("on", "on"), f"refuse: MP 0, free: Magic and Teleportation selectable {rows}")
    t.set_mp(99)
    # off the Circle
    for d in ("down", "up", "left", "right"):
        g.hold([d], 24)
        g.run(30)
        if t.ft()["state"]["circle"] == 0:
            break
    s = t.ft()
    if s["state"]["circle"]:
        check(True, "refuse: could not walk off the Circle (skipped)")
        return
    check(s["why"] == "Stand on a Circle of Magic." and s["menu_bits"] == 0,
          f"refuse: off the Circle: '{s['why']}', menu_bits {s['menu_bits']}")
    check(button_refused(t), "refuse: off the Circle: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[0] is None or rows[1] == "greyed", f"refuse: off the Circle: no travel in the menu {rows}")


def case_game(t, check, args):
    g, off = t.g, args.menus == "off"
    t.set_flag(UNK_B5, 1)
    t.set_flag(MAGIC_USED, 0)
    t.set_mp(99, 99)
    s = t.ft()
    target = nearest_other(s)
    check(s["whose"] == "game" and s["unlock"] == 1, f"game: unkB5 1: the game's unlock ({s['whose']})")
    if target is None:
        check(True, "game: no other Circle found (skipped)")
        return
    raw = s["points"][target]["raw"]
    check(s["points"][target]["cost"] == raw, f"game: the game's cost {raw} whatever qol_fast_travel_cost (free)")
    s, drop = travel_to(t, check, args, target, "game")
    check(s["last"]["arrived"] and s["last"]["whose"] == "game" and s["last"]["counted"] == 1,
          f"game: arrived, the game's trip ({s['last']})")
    check(drop == raw, f"game: MP charged {drop} ({raw}, _getTeleportCost)")
    check(t.flags()[MAGIC_USED] == 1, "game: magicUsed set, as the game's Teleportation does")
    t.set_flag(UNK_B5, 2)
    g.run(2)
    s = t.ft()
    check(s["unlock"] == 2 and "paling" in s["why"], f"game: unkB5 2: the spirit paling ('{s['why']}')")
    check(button_refused(t), "game: unkB5 2: the Travel button opens nothing")
    if off:
        rows = magic_rows(t)
        check(rows[1] == "greyed", f"game: unkB5 2: MENU0's Teleportation row greyed {rows}")


def case_here(t, check, args):
    if args.menus == "on":
        check(True, "here: the original list only (skipped)")
        return
    s = t.ft()
    here = s["state"]["here"]
    rows = [p["i"] for p in s["points"] if p["state"]]
    open_list(t)
    unk2, ability = pick(t, rows, here)
    check(unk2 == 0xA and ability == packed(here, 0), f"here: MENU0's hand-off for this Circle ({ability:#x})")
    n = t.wait(lambda: t.menu() == CLOSED and t.u8("D_800F196C") == 1 and t.ft()["last"]["valid"], 900, "",
               step=4, quiet=True)
    check(n is not None, "here: back to the field (no hang)")
    s = t.ft()
    check(s["last"]["room_ok"] and s["state"]["circle"] == here + 1, f"here: the same room, on its Circle ({s['last']})")
```

and add `"refuse": case_refuse, "game": case_game, "here": case_here` to `CASES`.

- [ ] **Step 2: Run them**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases refuse,game,here
```

Expected: every line `ok`, `PASS: fast travel`. (`button_refused` counts the press as ignored; until Task 10 every press is ignored, so those lines prove the refusal only from Task 10 on, when a press that passes the guard opens the list.)

If `here` hangs: choosing the current Circle is the stock game's behaviour once Teleportation is learned, but if it hangs here, grey that row while the port's unlock is in effect (a `pc_ft_refused`-style term on `rowTypes |= 4` rows in `_teleportMenu`, behind ROOD_PC, its probe check first), note it in `pc/docs/Fast_Travel.md`, and change the case to expect the row greyed.

- [ ] **Step 3: Commit**

```bash
git add pc/tests/fast_travel_smoke.py
git commit -m "Fast travel tests: refusals (R1, R3, events, MP, off the Circle), the game's own unlock and paling, this Circle

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 9: The Travel binding (a slot beside the PS1 buttons)

**Files:**
- Modify: `pc/src/boot_controller.h`, `pc/src/boot_controller.c`, `pc/src/boot_gamepad.c`, `pc/src/boot_window.c`, `pc/tests/controller_probe.c`

- [ ] **Step 1: Write the failing probe checks**

In `pc/tests/controller_probe.c`, before `puts("PASS: ...`, add:

```c
    /* The Travel slot (fast travel): its own keys and pad element beside the
     * PS1 buttons; by default the pad's back (Select's too), no key. */
    pc_bindings_reset();
    memset(&s, 0, sizeof(s));
    s.buttons[SDL_GAMEPAD_BUTTON_BACK] = 1;
    CHECK(pc_controller_element(&s, pc_bind_pad[PC_BIND_TRAVEL]) && pc_controller_mask(&s) == (1u << 8));
    CHECK(pc_bind_shared_bits(PC_BIND_TRAVEL) == (1u << 8));
    CHECK(!pc_keyboard_travel(SDLK_TAB) && pc_keyboard_bits(SDLK_TAB) == (1u << 8));
    CHECK(pc_bind_slot_index("travel") == PC_BIND_TRAVEL && pc_pad_button_index("travel") == -1);
    CHECK(pc_binding_setting("bind_key_travel", "tab") && pc_keyboard_travel(SDLK_TAB)
          && pc_keyboard_bits(SDLK_TAB) == (1u << 8)); /* shared: Tab is still Select */
    CHECK(pc_binding_setting("bind_pad_travel", "rightstick")
          && pc_bind_shared_bits(PC_BIND_TRAVEL) == ((1u << 8) | (1u << 10))); /* Tab: Select; right stick: R3 */
    {
        FILE* f = tmpfile();
        char text[4096] = "";
        CHECK(f != NULL);
        pc_binding_settings_write(f);
        rewind(f);
        size_t n = fread(text, 1, sizeof text - 1, f);
        text[n] = 0;
        fclose(f);
        CHECK(strstr(text, "bind_key_travel tab\n") && strstr(text, "bind_pad_travel rightstick\n"));
    }
    pc_bindings_reset();
    CHECK(!pc_keyboard_travel(SDLK_TAB) && pc_bind_pad[PC_BIND_TRAVEL] == SDL_GAMEPAD_BUTTON_BACK);
```

and extend the PASS line with `, the Travel slot`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" ninja -C pc/build controller_probe
```

Expected: `error: 'PC_BIND_TRAVEL' undeclared`.

- [ ] **Step 3: The slot**

In `pc/src/boot_controller.h`, after `#define PC_PAD_BUTTONS 16` add:

```c
/* Binding slots: the 16 PS1 buttons, then the port's own actions, which never
 * reach the game's pad word: PC_BIND_TRAVEL, fast travel's Travel button
 * (fast_travel.c; settings bind_key_travel, bind_pad_travel). */
#define PC_BIND_TRAVEL 16
#define PC_BIND_SLOTS 17
```

and change the binding arrays and add the helpers:

```c
extern SDL_Keycode pc_bind_keys[PC_BIND_SLOTS][PC_BIND_KEYS]; /* 0: none */
extern int pc_bind_pad[PC_BIND_SLOTS]; /* SDL_GamepadButton, PC_PAD_AXIS(trigger) or PC_PAD_NONE */
```

```c
/* A slot's settings name: pc_pad_button_names, then "travel". */
const char* pc_bind_slot_name(int slot);
/* A slot for a settings name (a PS1 button or an action), -1 if none. */
int pc_bind_slot_index(const char* name);
/* Whether gamepad element `element` is pressed in `s` (a trigger past PC_TRIGGER_PRESSED). */
int pc_controller_element(const PcControllerState* s, int element);
/* Whether `key` is bound to the Travel action. */
int pc_keyboard_travel(SDL_Keycode key);
/* The PS1 buttons bound to one of slot's keys or to its pad element (pressed with it). */
unsigned pc_bind_shared_bits(int slot);
```

and in the boot_gamepad.c section: `int pc_controller_travel(void); /* the Travel slot's pad element is held */`.

In `pc/src/boot_controller.c`: append `, { 0 }` (no key) to `DEFAULT_KEYS` and `, SDL_GAMEPAD_BUTTON_BACK /* Travel: shares Select's back */` to `DEFAULT_PAD`; size `default_keys`, `default_pad`, `pc_bind_keys`, `pc_bind_pad` with `PC_BIND_SLOTS`; then:

```c
const char* pc_bind_slot_name(int slot)
{
    return slot >= 0 && slot < PC_PAD_BUTTONS ? pc_pad_button_names[slot] : slot == PC_BIND_TRAVEL ? "travel" : "";
}

int pc_bind_slot_index(const char* name)
{
    int i = pc_pad_button_index(name);
    return i >= 0 ? i : !strcasecmp(name, "travel") ? PC_BIND_TRAVEL : -1;
}

int pc_controller_element(const PcControllerState* s, int e)
{
    return e >= 0 && e < SDL_GAMEPAD_BUTTON_COUNT ? s->buttons[e] != 0
        : e >= PC_PAD_AXIS(0) && e < PC_PAD_AXIS(SDL_GAMEPAD_AXIS_COUNT)
        ? s->axes[e - PC_PAD_AXIS(0)] > PC_TRIGGER_PRESSED /* the triggers (L2 / R2 by default) */
        : 0;
}

int pc_keyboard_travel(SDL_Keycode key)
{
    return key && (pc_bind_keys[PC_BIND_TRAVEL][0] == key || pc_bind_keys[PC_BIND_TRAVEL][1] == key);
}

unsigned pc_bind_shared_bits(int slot)
{
    unsigned bits = 0;
    if (slot < 0 || slot >= PC_BIND_SLOTS)
        return 0;
    for (int i = 0; i < PC_PAD_BUTTONS; i++) {
        int shared = i != slot && pc_bind_pad[slot] != PC_PAD_NONE && pc_bind_pad[i] == pc_bind_pad[slot];
        for (int k = 0; k < PC_BIND_KEYS; k++)
            for (int m = 0; m < PC_BIND_KEYS; m++)
                if (i != slot && pc_bind_keys[slot][k] && pc_bind_keys[slot][k] == pc_bind_keys[i][m])
                    shared = 1;
        if (shared)
            bits |= 1u << i;
    }
    return bits;
}
```

In `pc_controller_mask` replace the loop's condition with `if (pc_controller_element(s, pc_bind_pad[i]))`. In `pc_binding_setting`: `int button = pc_bind_slot_index(key + 9);` (the error message unchanged). In `pc_binding_settings_write`: loop `i < PC_BIND_SLOTS` and print `pc_bind_slot_name(i)` in place of `pc_pad_button_names[i]`.

- [ ] **Step 4: The held state**

In `pc/src/boot_gamepad.c`: a `static int travel;`; in `pc_controller_poll`, `travel = 0;` in the no-pad branch and, after `mask = pc_controller_mask(&state);`, `travel = pc_controller_element(&state, pc_bind_pad[PC_BIND_TRAVEL]);`; and after `pc_controller_buttons`:

```c
/* The Travel slot's pad element held (fast travel). */
int pc_controller_travel(void) { return travel; }
```

In `pc/src/boot_window.c`: a `static int travel_key;` beside `buttons`; in `SDL_EVENT_KEY_DOWN` add `if (pc_keyboard_travel(event.key.key)) travel_key = 1;` and add `|| pc_keyboard_travel(event.key.key)` to the "keyboard is the last device" condition; in `SDL_EVENT_KEY_UP` add `if (pc_keyboard_travel(event.key.key)) travel_key = 0;`; set `travel_key = 0` where `buttons = camera_keys = 0` (focus lost, `pc_window_release_keys`). Replace Task 4's temporary definitions with:

```c
/* The Travel binding (fast travel, fast_travel.c): held now (focused; not
 * while a binding capture takes the pad), and the PS1 buttons sharing its key
 * or pad element (taken out of that frame when it fires). */
int pc_window_travel_held(void)
{
    return focused && (travel_key || (!capture_filter(NULL) && pc_controller_travel()));
}
unsigned pc_window_travel_shared(void) { return pc_bind_shared_bits(PC_BIND_TRAVEL); }
```

- [ ] **Step 5: Run the probes and the input suites**

```bash
flock "$ROOD_HEAVY_LOCK" ninja -C pc/build controller_probe ui_settings_probe ui_settings_probe_hooks && pc/build/controller_probe
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest button_prompts input system_settings
```

Expected: `PASS: ... the Travel slot`; the suites pass (the Bindings page still shows 16 PS1 rows until Task 12).

- [ ] **Step 6: Commit**

```bash
git add pc/src/boot_controller.h pc/src/boot_controller.c pc/src/boot_gamepad.c pc/src/boot_window.c pc/tests/controller_probe.c
git commit -m "Bindings: a Travel slot beside the PS1 buttons (bind_key_travel none, bind_pad_travel back, shared with Select)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: The Travel button opens the list directly (MENU0 handleShortcut)

**Files:**
- Modify: `src/MENU/MENU0.PRG/15AC.c`, `pc/src/fast_travel.c`, `pc/tests/fast_travel_smoke.py`

- [ ] **Step 1: Write the failing case**

In `pc/tests/fast_travel_smoke.py`, before `SHARED = ...` add:

```python
def key(t, name, hold=3):
    """A real key press (SDL events through the system RPC)."""
    t.g.call("system", key=name, down=True)
    t.g.run(hold)
    t.g.call("system", key=name, down=False)
    t.g.run(10)


def case_button(t, check, args):
    g = t.g
    cam = t.sym["vs_battle_cameraCurrentSpherical"] + INITIAL_DISTANCE
    s = t.ft()
    here, target = s["state"]["here"], nearest_other(s)
    if target is None:
        check(True, "button: no other Circle found (skipped)")
        return
    rows = [p["i"] for p in s["points"] if p["state"]]
    loc = t.location()
    # a key for Travel, shared with Select (Tab), as the default pad element (back) is
    g.call("button_prompts", bind={"bind_key_travel": "tab"})
    t.ft(set={"enabled": False})  # the control: Tab is only Select, the camera's zoom toggles
    d0 = t.u32(cam)
    key(t, "tab")
    if t.u32(cam) != d0:
        key(t, "tab")  # back
    else:
        print("info: button: Select does not toggle the zoom here (no control)")
    t.ft(set={"enabled": True})
    d0 = t.u32(cam)
    key(t, "tab")
    t.wait(lambda: t.menu() == 1 and t.u8("vs_battle_shortcutInvoked") == SHORTCUT_TRAVEL, 30,
           "the Travel button opens MENU0 (currentMenu 1, shortcut 9)")
    wait_list(t)
    items = t.items()
    check(TELEPORT_ITEM in items and not any(i in items for i in range(10, 14)),
          "button: straight to the destinations (the TELEPORTATION title, no school rows)")
    check(t.selected(range(LIST_ITEM, LIST_ITEM + len(rows))) == LIST_ITEM + rows.index(here),
          "button: the cursor on this Circle")
    check(t.u32(cam) == d0, f"button: Select's zoom not toggled by the shared key ({d0:#x} -> {t.u32(cam):#x})")
    t.shot("button-list")
    g.press("cross", vsyncs=3, settle=10)
    t.wait(lambda: t.menu() == CLOSED, 180, "Cross closes the menu", step=2)
    check(t.handoff()[0] != 0xA and t.location() == loc, "button: Cross: closed, nothing handed to the game")
    g.run(20)
    t.ft(press=True)  # the pad button's path (RPC press)
    g.run(4)
    wait_list(t)
    unk2, ability = pick(t, rows, target)
    check(unk2 == 0xA and ability == packed(target, 0), f"button: MENU0's hand-off ({ability:#x})")
    s = arrive(t, target)
    check(s["last"]["arrived"] and s["opened"] == 2, f"button: arrived on Circle {target} (opened {s['opened']})")
    t.set_flag(UNK_A7, 1)
    check(button_refused(t), "button: a refused press (unkA7) opens nothing")
    t.set_flag(UNK_A7, 0)
```

and add `"button": case_button` to `CASES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases button
```

Expected: `FAIL button: the Travel button opens MENU0 ...: not after 30 vsyncs`.

- [ ] **Step 3: MENU0's entry for the shortcut**

In `src/MENU/MENU0.PRG/15AC.c`, `vs_menu0_exec`, `handleShortcut`, change

```c
        i = vs_battle_shortcutInvoked;

        if (i != 0) {
```

to

```c
        i = vs_battle_shortcutInvoked;

#ifdef ROOD_PC
        if (i == PC_SHORTCUT_TRAVEL) {
            /* The Travel button (pc/src/fast_travel.c): straight into the
             * destinations list, its title in the Teleport row's item (14) as
             * a school shortcut's is in its own (before the magic strings are
             * indexed by the shortcut). */
            vs_battle_setMenuItem(14, 0x140, 0x22, 0x8C, 8,
                (char*)(_teleportationStrings + VS_teleportation_OFFSET_teleportation))
                ->selected = 1;
            *state = teleportMenu;
            _teleportMenu(1);
            break;
        }
#endif
        if (i != 0) {
```

and in `_teleportMenu`, `handleInput`, change

```c
            vs_mainMenu_displayActionCost = 0;
            if (selectedRow == (-1)) {
```

to

```c
            vs_mainMenu_displayActionCost = 0;
#ifdef ROOD_PC
            if (vs_battle_shortcutInvoked == PC_SHORTCUT_TRAVEL && selectedRow == -1) {
                selectedRow = -2; /* the Travel button: Cross closes the menu (as a school shortcut's list) */
            }
#endif
            if (selectedRow == (-1)) {
```

(`-2` takes the `else` branch: cleared, the box dismissed, `returnIfReady` returns -2, `teleportMenu` exits.)

- [ ] **Step 4: The poll opens it**

In `pc/src/fast_travel.c`, `pc_fast_travel_poll`, replace

```c
    ++presses;
    ++ignored; /* (Task 10 opens the list here) */
    return 0;
```

with

```c
    ++presses;
    if (why || !func_800CACD0(1, 0)) {
        ++ignored; /* nothing happens: no sound, Select (if shared) does what it always does */
        return 0;
    }
    /* as an L2 shortcut opens a school's list (vs_battle_validateShortcutSelection,
     * then func_8007357C): MENU0 straight into _teleportMenu (15AC.c handleShortcut) */
    vs_battle_shortcutInvoked = PC_SHORTCUT_TRAVEL;
    D_800F4FDB = 1;
    func_8007357C(0, 0);
    vs_main_buttonsPressed.all &= ~pc_window_travel_shared(); /* Select's zoom does not toggle too */
    ++opened;
    return 1;
```

and change the poll's comment to drop the Task 10 sentence.

- [ ] **Step 5: Build, run, commit-check**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases button,refuse,game
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ui_magick menu0_levelled weapon_slots
```

Expected: all `ok` (the `refuse` / `game` `button_refused` lines now prove the refusals: a passing press would open the list); the three suites pass. Then the commit-check: `All files match`.

- [ ] **Step 6: Commit**

```bash
git add src/MENU/MENU0.PRG/15AC.c pc/src/fast_travel.c pc/tests/fast_travel_smoke.py
git commit -m "Fast travel: the Travel button opens MENU0's destinations list directly; Cross closes it; Select's zoom untouched

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: The prompt on a Circle

**Files:**
- Create: `pc/src/fast_travel_prompt.cpp`
- Modify: `pc/src/button_prompts.h`, `pc/src/button_prompts.c`, `pc/src/prompt_glyphs.cpp`, `pc/src/dev_overlay.cpp`, `pc/tools/build_boot.py` (the ImGui units' list), `pc/tests/fast_travel_smoke.py`

- [ ] **Step 1: Write the failing case**

In `pc/tests/fast_travel_smoke.py`, before `SHARED = ...` add:

```python
def drawn(t, vsyncs=20):
    n = t.ft()["prompt_drawn"]
    t.g.run(vsyncs)
    return t.ft()["prompt_drawn"] - n


def case_prompt(t, check, args):
    g = t.g
    s = t.ft()
    if s["why"]:
        check(True, f"prompt: travel not offered here ('{s['why']}', skipped)")
        return
    g.call("button_prompts", device="playstation", variant=4)
    check(t.ft()["prompt"] and drawn(t) > 0, "prompt: shown on the Circle (the pad's back)")
    t.shot("prompt")
    t.ft(set={"enabled": False})
    check(not t.ft()["prompt"] and drawn(t) == 0, "prompt: not with fast travel off")
    t.ft(set={"enabled": True, "prompt": False})
    check(drawn(t) == 0, "prompt: not with qol_fast_travel_prompt 0")
    t.ft(set={"prompt": True})
    g.call("button_prompts", device="keyboard")
    check(drawn(t) == 0, "prompt: not on the keyboard while Travel has no key")
    g.call("button_prompts", bind={"bind_key_travel": "t"})
    check(drawn(t) > 0, "prompt: on the keyboard once Travel has a key")
    t.set_flag(UNK_A7, 1)
    check(drawn(t) == 0, "prompt: not while travel is refused (unkA7)")
    t.set_flag(UNK_A7, 0)
```

and add `"prompt": case_prompt` to `CASES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases prompt
```

Expected: `FAIL prompt: shown on the Circle` (nothing draws it: `prompt_drawn` stays 0).

- [ ] **Step 3: A glyph for the Travel slot**

In `pc/src/button_prompts.h`, change the `PC_PROMPT_*` enum's last line to `PC_PROMPT_DOWN, PC_PROMPT_LEFT, PC_PROMPT_TRAVEL /* binding slot 16 (boot_controller.h PC_BIND_TRAVEL) */` and in the `pc_prompt_glyph` comment add "(PC_PROMPT_TRAVEL: the Travel action's key or pad element)". In `pc/src/button_prompts.c`, `pc_button_prompts_icon_rgba`, the range check becomes `button >= PC_BIND_SLOTS`. In `pc/src/prompt_glyphs.cpp`, `pc_prompt_glyph`, `button > PC_PROMPT_LEFT` becomes `button > PC_PROMPT_TRAVEL`.

- [ ] **Step 4: Draw it**

Create `pc/src/fast_travel_prompt.cpp`:

```cpp
// The Travel prompt (fast travel, pc/docs/Fast_Travel.md): while Ashley stands
// on a Circle of Magic and the Travel button would open the destinations
// (fast_travel.c pc_fast_travel_prompt), the Travel binding's icon for the
// device used last (pc_prompt_glyph) and "Travel" on a small plate over the
// game picture, centred at qol_fast_travel_prompt_x / _y (fractions of the
// window), qol_fast_travel_prompt_scale its size. Nothing when that device has
// nothing bound to Travel. Called from dev_overlay.cpp's frame.
#include "button_prompts.h"
#include "fast_travel.h"
#include "imgui.h"
#include <cfloat>

extern "C" int pc_fast_travel_prompt(void) __attribute__((weak)); // fast_travel.c (battle builds)
extern "C" unsigned pc_fast_travel_prompt_drawn __attribute__((weak));

extern "C" void pc_fast_travel_prompt_draw(void)
{
    if (!pc_fast_travel_prompt || !pc_fast_travel_prompt())
        return;
    ImGuiIO& io = ImGui::GetIO();
    float size = io.DisplaySize.y * 0.045f * pc_qol_fast_travel_prompt_scale;
    pc_prompt_image g = pc_prompt_glyph(PC_PROMPT_TRAVEL, size);
    if (!g.texture)
        return;
    const char* label = "Travel";
    ImFont* font = ImGui::GetFont();
    float fs = size * 0.7f, pad = size * 0.25f;
    ImVec2 text = font->CalcTextSizeA(fs, FLT_MAX, 0, label);
    float w = pad + g.w + pad + text.x + pad * 2, h = (g.h > text.y ? g.h : text.y) + pad * 2;
    float x = io.DisplaySize.x * pc_qol_fast_travel_prompt_x - w * 0.5f;
    float y = io.DisplaySize.y * pc_qol_fast_travel_prompt_y - h * 0.5f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(12, 10, 9, 200), h * 0.5f);
    // ImTextureRef has no ImTextureData* constructor: GetTexRef (prompt_glyphs.cpp)
    dl->AddImage(((ImTextureData*)g.texture)->GetTexRef(), ImVec2(x + pad, y + (h - g.h) * 0.5f),
        ImVec2(x + pad + g.w, y + (h + g.h) * 0.5f), ImVec2(g.uv0[0], g.uv0[1]), ImVec2(g.uv1[0], g.uv1[1]));
    dl->AddText(font, fs, ImVec2(x + pad * 2 + g.w, y + (h - text.y) * 0.5f), IM_COL32(232, 224, 208, 255), label);
    if (&pc_fast_travel_prompt_drawn)
        ++pc_fast_travel_prompt_drawn;
}
```

In `pc/src/dev_overlay.cpp`, declare `extern "C" void pc_fast_travel_prompt_draw(void); /* fast_travel_prompt.cpp */` with the other externs and change

```cpp
    pc_ui_draw_frame((float)dt); /* modern menus (pc/src/ui) over the game picture */
```

to

```cpp
    pc_ui_draw_frame((float)dt); /* modern menus (pc/src/ui) over the game picture */
    if (!visible)
        pc_fast_travel_prompt_draw(); /* the Travel prompt on a Circle of Magic (not over the editor) */
```

In `pc/tools/build_boot.py`, in the ImGui units' list, after `root / "pc/src/prompt_glyphs.cpp",` add `root / "pc/src/fast_travel_prompt.cpp",`.

- [ ] **Step 5: Run it**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases prompt --out pc/build/tests/fast_travel_look
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 button_prompts
```

Expected: every `prompt:` line `ok`; look at `pc/build/tests/fast_travel_look/prompt-prompt.png` (the plate with the PlayStation Select icon and "Travel", centred low on the picture; game imagery: not committed); button_prompts passes.

- [ ] **Step 6: Commit**

```bash
git add pc/src/fast_travel_prompt.cpp pc/src/button_prompts.h pc/src/button_prompts.c pc/src/prompt_glyphs.cpp \
    pc/src/dev_overlay.cpp pc/tools/build_boot.py pc/tests/fast_travel_smoke.py
git commit -m "Fast travel: the Travel prompt on a Circle (the binding's icon for the device used last), tunable place and size

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Settings everywhere: the Gameplay page, the Bindings row, the dev panel, the file

**Files:**
- Modify: `pc/src/ui/ui_settings.cpp`, `pc/tests/ui_settings_probe.cpp`, `pc/tests/ui_settings_stubs.cpp`, `pc/src/dev_overlay.cpp`, `pc/tests/fast_travel_smoke.py`

- [ ] **Step 1: Write the failing probe checks**

In `pc/tests/ui_settings_probe.cpp`, `pages()`: change `CHECK(pc_ui_settings_rows(B) == 17);` to

```cpp
    CHECK(pc_ui_settings_rows(B) == 18); // the 16 PS1 buttons, Travel, Reset
    CHECK(find(B, "bind_travel") == find(B, "bind_reset") - 1 && row(B, "bind_travel").button == PC_BIND_TRAVEL);
```

and `CHECK(pc_ui_settings_rows(P) == (game ? 12 : 5));` to

```cpp
    CHECK(pc_ui_settings_rows(P) == (game ? 15 : 8)); // fast travel: on, its cost, its prompt
    // fast travel after battle charging: On, Free; Flat shows its MP, Scaled its factor
    CHECK(find(P, "qol_fast_travel") == find(P, "qol_skip_charging") + 1
        && find(P, "qol_fast_travel_cost") == find(P, "qol_fast_travel") + 1);
    CHECK(value(P, "qol_fast_travel") == "On" && value(P, "qol_fast_travel_cost") == "Free");
    CHECK(change(P, "qol_fast_travel_cost", 1) == 1 && pc_qol_fast_travel_cost == 1
        && pc_ui_settings_rows(P) == (game ? 16 : 9) && find(P, "qol_fast_travel_flat") == find(P, "qol_fast_travel_cost") + 1);
    CHECK(change(P, "qol_fast_travel_cost", 1) == 1 && pc_qol_fast_travel_cost == 2
        && find(P, "qol_fast_travel_scale") == find(P, "qol_fast_travel_cost") + 1 && find(P, "qol_fast_travel_flat") < 0);
    pc_qol_fast_travel_cost = 0;
    pc_qol_fast_travel = 0;
    CHECK(!row(P, "qol_fast_travel_cost").enabled); // greyed: needs fast travel
    pc_qol_fast_travel = 1;
```

(`find`, `row`, `value`, `change` are the probe's helpers; `row(...).button` is `pc_ui_setting_row.button`.)

In `pc/tests/ui_settings_stubs.cpp`, after `int pc_qol_confirm_cross, pc_qol_field_cross_attack;` add:

```cpp
int pc_qol_fast_travel = 1, pc_qol_fast_travel_cost, pc_qol_fast_travel_flat = 15, pc_qol_fast_travel_prompt = 1;
float pc_qol_fast_travel_scale = 0.5f, pc_qol_fast_travel_prompt_x = 0.5f, pc_qol_fast_travel_prompt_y = 0.84f,
    pc_qol_fast_travel_prompt_scale = 1.0f;
```

- [ ] **Step 2: Run them and see them fail**

```bash
flock "$ROOD_HEAVY_LOCK" ninja -C pc/build ui_settings_probe ui_settings_probe_hooks && pc/build/ui_settings_probe
```

Expected: FAIL lines for the Bindings and Gameplay row counts.

- [ ] **Step 3: The rows**

In `pc/src/ui/ui_settings.cpp`, add `#include "fast_travel.h"` with the other includes. After the Bindings loop (before `b.push_back(action("bind_reset", ...`) add:

```cpp
    Row travel = base("bind_travel", "Travel",
        "Fast travel: on a Circle of Magic, opens the Circles you can teleport to. It may share a button with "
        "Select: where you can travel it travels, elsewhere Select does what it always does. Circle to change.",
        PC_UI_SET_BINDING);
    travel.arg = PC_BIND_TRAVEL;
    travel.why = [](const Row&) { return pc_ui_capture_begin ? nullptr : COMING; };
    b.push_back(travel);
```

After the `qol_skip_charging` row add:

```cpp
    static const char* const travel_costs[] = {"Free", "Flat", "Scaled", "Game"};
    p.push_back(toggle("qol_fast_travel", "Fast travel",
        "Teleport between the Circles of Magic you have found, from the first one after the prologue: stand on a "
        "Circle and press Travel, or Magic > Teleportation. The game's own spell, once learned, works as always.",
        &pc_qol_fast_travel));
    p.push_back(choice("qol_fast_travel_cost", "Travel cost",
        "Until the game teaches Teleportation: Free, Flat (a set MP), Scaled (part of the game's cost) or Game (15 MP "
        "plus 4 per Circle between). Once it has, the game's own cost.",
        &pc_qol_fast_travel_cost, travel_costs, 4)
                    .grey([](const Row&) -> const char* { return pc_qol_fast_travel ? nullptr : "Needs Fast travel"; }));
    p.push_back(islider("qol_fast_travel_flat", "Flat cost", "The MP each trip costs.", &pc_qol_fast_travel_flat, 0, 99,
        "%d MP")
                    .when([](const Row&) { return pc_qol_fast_travel_cost == PC_FT_COST_FLAT; })
                    .grey([](const Row&) -> const char* { return pc_qol_fast_travel ? nullptr : "Needs Fast travel"; }));
    p.push_back(fslider("qol_fast_travel_scale", "Cost scale", "The game's cost times this.", &pc_qol_fast_travel_scale,
        0.1f, 1.0f, 0.05f, "x%.2f")
                    .when([](const Row&) { return pc_qol_fast_travel_cost == PC_FT_COST_SCALED; })
                    .grey([](const Row&) -> const char* { return pc_qol_fast_travel ? nullptr : "Needs Fast travel"; }));
    p.push_back(toggle("qol_fast_travel_prompt", "Travel prompt",
        "Show the Travel button on a Circle of Magic when you can travel.", &pc_qol_fast_travel_prompt)
                    .grey([](const Row&) -> const char* { return pc_qol_fast_travel ? nullptr : "Needs Fast travel"; }));
```

Change `write_keys`' `pc_pad_button_names[button]` to `pc_bind_slot_name(button)`, and at the top of `pc_ui_settings_bind`:

```cpp
    if (button < 0 || button >= PC_BIND_SLOTS)
        return -2;
    if (button >= PC_PAD_BUTTONS) { // the port's own action: shares keys and pad buttons, never swaps
        if (kind == PC_UI_CAPTURE_KEY) {
            SDL_Keycode key = (SDL_Keycode)code;
            if (!key || !pc_key_name(key) || pc_ui_settings_key_reserved(code))
                return -2;
            if (pc_bind_keys[button][0] == key)
                return -1;
            write_keys(button, key, pc_bind_keys[button][0]);
        } else if (kind == PC_UI_CAPTURE_PAD) {
            const char* name = pc_pad_element_name(code);
            if (code == PC_PAD_NONE || pc_pad_element_from_name(name) != code)
                return -2;
            if (pc_bind_pad[button] == code)
                return -1;
            std::string key = std::string("bind_pad_") + pc_bind_slot_name(button);
            pc_binding_setting(key.c_str(), name);
        } else
            return -2;
        mark_dirty();
        return -1;
    }
```

(replacing the old `if (button < 0 || button >= PC_PAD_BUTTONS) return -2;`).

- [ ] **Step 4: The dev panel**

In `pc/src/dev_overlay.cpp`, add `#include "fast_travel.h"` and at the end of `gameplay_panel()`:

```cpp
    bool travel = pc_qol_fast_travel != 0;
    if (ImGui::Checkbox("Fast travel (from the first Circle of Magic)", &travel))
        pc_qol_fast_travel = travel;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Settings qol_fast_travel. The game's own Teleportation between the Circles found, offered\n"
                          "once the prologue is over; the game's own unlock (the Lich) always applies as the game has it.");
    ImGui::Combo("Travel cost", &pc_qol_fast_travel_cost, "Free\0Flat\0Scaled\0Game\0");
    if (pc_qol_fast_travel_cost == PC_FT_COST_FLAT)
        ImGui::SliderInt("Flat cost (MP)", &pc_qol_fast_travel_flat, 0, 99);
    if (pc_qol_fast_travel_cost == PC_FT_COST_SCALED)
        ImGui::SliderFloat("Cost scale", &pc_qol_fast_travel_scale, 0.1f, 1.0f, "x%.2f");
    bool prompt = pc_qol_fast_travel_prompt != 0;
    if (ImGui::Checkbox("Travel prompt", &prompt))
        pc_qol_fast_travel_prompt = prompt;
    ImGui::SliderFloat("Prompt x", &pc_qol_fast_travel_prompt_x, 0, 1);
    ImGui::SliderFloat("Prompt y", &pc_qol_fast_travel_prompt_y, 0, 1);
    ImGui::SliderFloat("Prompt size", &pc_qol_fast_travel_prompt_scale, 0.5f, 2.0f);
```

- [ ] **Step 5: The settings file (a smoke case)**

In `pc/tests/fast_travel_smoke.py`, before `SHARED = ...` add:

```python
def case_settings(t, check, args):
    s = t.ft()
    check(s["settings"]["cost"] == "flat" and s["settings"]["flat"] == 7,
          f"settings: read from ROOD_SETTINGS ({s['settings']})")
    other = nearest_other(s)
    if other is not None:
        check(s["points"][other]["cost"] == 7, "settings: flat 7 MP per trip")
    t.ft(set={"cost": "scaled", "scale": 0.25})
    r = t.g.call("settings", action="save")
    text = pathlib.Path(r["path"]).read_text()
    check("qol_fast_travel_cost 2\n" in text and "qol_fast_travel_scale 0.25\n" in text,
          "settings: saved back (qol_fast_travel_cost 2, qol_fast_travel_scale 0.25)")


def settings_env(out):
    path = out / "settings-fast-travel.cfg"
    path.write_text("qol_fast_travel_cost 1\nqol_fast_travel_flat 7\n")
    return {"ROOD_SETTINGS": str(path)}


CASE_ENV = {"settings": settings_env}
```

add `"settings": case_settings` to `CASES`, and in `main()` change the per-case launch to `t = Trip(name, out, card, args.menus, dict(env, **CASE_ENV.get(name, lambda o: {})(out)), bool(args.out))`.

- [ ] **Step 6: Run everything this task touched**

```bash
flock "$ROOD_HEAVY_LOCK" ninja -C pc/build ui_settings_probe ui_settings_probe_hooks && pc/build/ui_settings_probe && pc/build/ui_settings_probe_hooks
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --cases settings
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ui_system system_settings
```

Expected: both probes pass; `settings:` lines `ok`; ui_system_smoke and system_settings_smoke pass (if one counts the Bindings or Gameplay rows, update its count by the rows added, with a comment naming them).

- [ ] **Step 7: Commit**

```bash
git add pc/src/ui/ui_settings.cpp pc/tests/ui_settings_probe.cpp pc/tests/ui_settings_stubs.cpp pc/src/dev_overlay.cpp pc/tests/fast_travel_smoke.py
git commit -m "Fast travel settings: Gameplay rows (on, cost, flat, scale, prompt), the Bindings page's Travel row, the dev panel

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: The modern menus: Magick > Teleport on the same rules

**Files:**
- Modify: `pc/src/ui/magick_bridge.c`, `pc/src/ui/menu_bridge.c`, `pc/tests/fast_travel_smoke.py`, `pc/tests/run.py`

- [ ] **Step 1: Write the failing suite**

In `pc/tests/fast_travel_smoke.py`, add to `Trip`:

```python
    def ui(self, **params):
        return self.g.call("ui_menu", **params)

    def mg(self):
        return self.ui().get("magick") or {}
```

and replace the placeholder `shell_travel` with:

```python
def shell_travel(t, target):
    """The modern menus: Magick > Teleport, the cursor to `target`, Circle; the hand-off."""
    g = t.g
    for _ in range(6):  # Triangle is ignored while an action still runs
        if t.ui().get("visible") and t.menu() == 0:
            break
        g.press("triangle", vsyncs=3, settle=4)
        t.wait(lambda: t.ui().get("visible") and t.menu() == 0, 120, "", step=2, quiet=True)
    t.wait(lambda: t.ui().get("visible") and t.menu() == 0, 2, "the shell opens")
    g.run(6)  # the shell ignores input for its first ticks
    for _ in range(8):
        if t.ui().get("tab") == "magick":
            break
        g.press("r1", vsyncs=3, settle=4)
    t.wait(lambda: t.ui().get("tab") == "magick", 20, "the Magick tab")
    for _ in range(6):
        if t.mg().get("sub") == "teleport":
            break
        g.press("r2", vsyncs=3, settle=4)
    t.wait(lambda: t.mg().get("sub") == "teleport", 20, "the Teleport sub-tab")
    tp = t.mg()["teleport"]
    d = next((x for x in tp["destinations"] if x["point"] == target), None)
    if not (tp["exists"] and tp["enabled"] and d and d["usable"]):
        raise TimeoutError(f"Teleport not usable: '{tp['reason']}' / {d}")
    for _ in range(50):
        if t.mg()["focus"] == target:
            break
        g.press("down", vsyncs=3, settle=4)
    g.press("circle", vsyncs=3, settle=1)
    t.wait(lambda: not t.ui()["visible"], 60, "the shell closes for the teleport")
    t.wait(lambda: t.handoff()[0] == 0xA, 120, "the hand-off", step=1)
    return t.handoff()
```

In `pc/tests/run.py`, after the `fast_travel_smoke` suite add:

```python
        *([Suite("fast_travel_shell_smoke", [PY, str(TESTS / "fast_travel_smoke.py"), "--card", str(args.save),
                                             "--menus", "on"], 900,
                 note="fast travel with the modern menus: Magick > Teleport on the same unlock, cost, mask and "
                      "refusals; save (System > Data) and Continue; the Travel button")]
          if args.save.exists() else []),
```

and add `"fast_travel_shell_smoke"` to `CHECKPOINT_SUITES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/fast_travel_smoke.py --menus on --cases menu
```

Expected: `FAIL menu: the Magick tab: not after 20 vsyncs` (the shell shows no Magick tab without a spell: it reads `menuFlags` alone).

- [ ] **Step 3: The shell's commands**

In `pc/src/ui/menu_bridge.c`, add `#include "../fast_travel.h"` after the game headers; in `commands()`:

```c
    c.enabled = (vs_main_settings.menuFlags | pc_fast_travel_menu_bits()) & 0x3FF; /* fast travel's Magic on a Circle */
    c.mask = mask;
    c.unselectable = ((mask & pc_fast_travel_mask(0xB7)) ? 1u : 0) | ((mask & 0x15F) ? 2u : 0) | ((mask & 7) ? 8u : 0)
        | (vs_battle_getCurrentSceneId() == 0 ? 0x20u : 0);
```

and in `deliver()`: `= (cmd == 0 && (mask & pc_fast_travel_mask(0xB7))) || (cmd == 1 && (mask & 0x15F));`.

- [ ] **Step 4: The Magick page**

In `pc/src/ui/magick_bridge.c`:
- add `#include "../fast_travel.h"` after `#include "ui_shell.h"`;
- delete `save_point_data[]` and `save_point_costs[]` (now `pc_ft_save_point_data` / `_costs`, checked against 84.c by fast_travel_probe) and `teleport_packed` (now `pc_ft_packed`, checked against 15AC.c);
- `teleport_cost` becomes:

```c
/* MENU0 _getTeleportCost (fast_travel_rules.c, MENU0's tables). */
static int teleport_cost(int target) { return pc_ft_raw_cost(pc_ft_here(vs_battle_getCurrentRoomId()), target); }
```

- `mask_reason(char* out, int size)` becomes `mask_reason_in(u_int bits, char* out, int size)` reading `(u_int)D_800F4EA0 & bits`, plus `static int mask_reason(char* out, int size) { return mask_reason_in(MAGIC_MASK, out, size); }`;
- `fill_teleport` becomes:

```c
static void fill_teleport(void)
{
    int any = 0, circle, i, mp = snap.mp, unlock = pc_fast_travel_unlock();
    const char* blocked = pc_fast_travel_why();
    char row_reason[64];
    snap.teleport = unlock != 0; /* the game's unkB5, or fast travel's (fast_travel.c) */
    if (!snap.teleport)
        return;
    for (i = 0; i < PC_UI_SAVE_POINTS; ++i)
        if (vs_main_stateFlags.savePointStates[i])
            any = 1;
    circle = func_8008A4FC() != 0;
    snap.teleport_enabled = any && unlock != 2 && circle && !blocked;
    copy_text(snap.teleport_help, sizeof snap.teleport_help, circle && unlock == 2 ? paling : teleport_desc);
    row_reason[0] = 0;
    if (!any)
        copy_text(row_reason, sizeof row_reason, "No Circle of Magic found yet.");
    else if (unlock == 2)
        copy_text(row_reason, sizeof row_reason, "The spirit paling prevents teleportation.");
    else if (!circle)
        copy_text(row_reason, sizeof row_reason, "Stand on a Circle of Magic to teleport.");
    else if (blocked)
        copy_text(row_reason, sizeof row_reason, blocked);
    copy_text(snap.teleport_reason, sizeof snap.teleport_reason, row_reason);
    for (i = 0; i < PC_UI_SAVE_POINTS; ++i) {
        int state = vs_main_stateFlags.savePointStates[i], raw;
        pc_ui_destination* d;
        if (!state)
            continue;
        d = &snap.destinations[snap.destination_count++];
        d->point = i;
        copy_text(d->name, sizeof d->name, point_name[i]);
        copy_text(d->help, sizeof d->help, state == PALING ? paling : point_help[i]);
        raw = teleport_cost(i);
        d->cost = pc_fast_travel_cost(raw); /* fast travel's cost mode before the game's own unlock */
        d->current = raw == CURRENT_COST;
        if (row_reason[0])
            copy_text(d->reason, sizeof d->reason, row_reason);
        else if (state == PALING)
            copy_text(d->reason, sizeof d->reason, "The spirit paling prevents teleportation.");
        else if (mp < d->cost)
            copy_text(d->reason, sizeof d->reason, "Not enough MP.");
        else if (!mask_reason_in(pc_fast_travel_mask(MAGIC_MASK), d->reason, sizeof d->reason) && !resident)
            copy_text(d->reason, sizeof d->reason, "Loading\xE2\x80\xA6");
        d->usable = d->reason[0] == 0;
    }
}
```

- in `pc_ui_magick_request`, `cast.id = teleport_packed(d->point, d->cost);` becomes `cast.id = pc_ft_packed(d->point, d->cost);`;
- in `pc_ui_magick_cast`, `TELEPORT_ROW + (vs_main_stateFlags.unkB5 != 0)` becomes `TELEPORT_ROW + (pc_fast_travel_unlock() != 0)` (MENU0's school list has the row whenever MENU0 reads the unlock);
- in the header comment's `teleport` paragraph, "the row exists with vs_main_stateFlags.unkB5 != 0" becomes "the row exists while MENU0 offers Teleportation (pc_fast_travel_unlock: the game's unkB5, or fast travel's), greyed by fast travel's refusals too", and "cost _getTeleportCost" becomes "cost _getTeleportCost through fast travel's cost mode".

- [ ] **Step 5: Run both variants and the shell's suites**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 fast_travel_smoke fast_travel_shell_smoke ui_magick ui_menus ui_data
```

Expected: all pass. In `fast_travel_shell_smoke`, `refuse`'s and `here`'s original-menu checks are skipped by design; `button` opens MENU0's list there too (decision 6).

- [ ] **Step 6: Commit**

```bash
git add pc/src/ui/magick_bridge.c pc/src/ui/menu_bridge.c pc/tests/fast_travel_smoke.py pc/tests/run.py
git commit -m "Modern menus: Magick > Teleport and the shell's commands on fast travel's unlock, cost, mask and refusals

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: The suites that count the original top level's rows

**Files:**
- Modify: `pc/tests/save_menu_smoke.py`, `pc/tests/interp_menu_2d_smoke.py`, and any suite the run below finds

With fast travel on by default, the original top level shows the Magic command on a Circle (the save room is one): a suite that reaches a command by counting Down presses lands one row off.

- [ ] **Step 1: Run the menu-driving suites**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 save_menu interp_menu_2d ui_menus ui_magick ui_abilities ui_items ui_equip ui_ashley ui_loot ui_map ui_container ui_score ui_manual ui_workshop hud_label_solid transition_fade route_continuation weapon_slots menu0_levelled ui_data
```

Expected before the fixes: `save_menu_smoke` and `interp_menu_2d_smoke` fail (they count rows); note any other failure and its cause.

- [ ] **Step 2: Navigate by the game's cursor**

In `pc/tests/save_menu_smoke.py`, add `sys.path.insert(0, str(ROOT / "pc/tests"))` and `from menu_nav import DATA_COMMAND, top_command  # noqa: E402` after the existing imports, add `from rood_rpc import symbols` to the rood_rpc import, and change

```python
        game.press("triangle", vsyncs=3, settle=60)
        for _ in range(4):
            game.press("down", vsyncs=3, settle=10)
        game.press("circle", vsyncs=3, settle=60)
```

to

```python
        game.press("triangle", vsyncs=3, settle=60)
        top_command(game, symbols(), DATA_COMMAND)  # by the game's cursor (fast travel's Magic on a Circle)
        game.press("circle", vsyncs=3, settle=60)
```

and its docstring's "DATA (fifth)" to "DATA".

In `pc/tests/interp_menu_2d_smoke.py` (fixed press sequences are its subject's input, not the menu's layout), right after its `launch_at(...)` add:

```python
    game.call("fast_travel", set={"enabled": False})  # the original top level's rows as its sequences count them
```

Any other failing suite: the same two remedies (cursor navigation where the suite picks a command; fast travel off where a fixed sequence is the point), each with a one-line comment.

- [ ] **Step 3: Run them again**

Same command as Step 1. Expected: all pass (or only the baseline's known failures from Task 1).

- [ ] **Step 4: Commit**

```bash
git add pc/tests/
git commit -m "Tests: the original top level by the game's cursor (fast travel shows Magic on a Circle)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: Docs, README, and the full verification

**Files:**
- Create: `pc/docs/Fast_Travel.md`
- Modify: `pc/README.md`

- [ ] **Step 1: Write `pc/docs/Fast_Travel.md`**

Sections (prose, in the style of the other `pc/docs` pages; no game text, no paths outside the repository):
1. *What it is*: the game's own Teleportation from the first Circle once the prologue is over; the flat original list; how to travel (Travel on a Circle, or Magic > Teleportation; Magick > Teleport with the modern menus); what happens after the choice (the game's effect, the door's room load, arrival on the Circle, Save at once).
2. *Whose unlock*: the port's (`qol_fast_travel`, `introState >= 3`, not mid-puzzle) vs the game's (`unkB5`, the Lich in 0057.EVT: everything as the game, the paling included).
3. *Cost and magic*: the four modes, applied at MENU0's compare / display / packing only while the port's unlock is in effect; MP 0 in free mode; `magicUsed` only for the game's unlock (the end-game "no magic" title).
4. *Rules*: R1 (on a Circle, this Circle recorded), R2 (no event, transition, game over), R3 (Ashley free; `unkA7`), R4 (found, not paled), R6 (the audit: 35 of 35), R7 (Room Browser; the `savePointStates` snapshot and restore), R8 (one hand-off per menu session, MENU0's own), R9 (the scan's findings, verbatim from Task 3 Step 4: each Circle records itself; the Lich's 0057.EVT sets `unkB5` 1; 0049.EVT / 0065.EVT set and clear `unkA7`; nothing to mirror, so no `blocks.json`; or the blocks Task 3b added).
5. *The Travel button*: the binding slot, sharing with Select, nothing on a refused press, the prompt and its settings.
6. *What the card holds*: only the game's bytes (no Magic bit in `menuFlags`, no `unkB5`, no new state); no sidecar.
7. *src hooks*: the three files and what each `#ifdef ROOD_PC` does.
8. *Settings and RPC*: the keys, `fast_travel {set, press, audit}` and its reply.
9. *Tests*: fast_travel_probe, fast_travel_scan, fast_travel_smoke / fast_travel_shell_smoke and their cases.
10. *Not yet*: the modern map picker (area grouping, the ARM map, a confirm card, an arrival toast) waits for the modern-menu work.

- [ ] **Step 2: README**

In `pc/README.md`: a "Fast travel" paragraph in the gameplay QoL section linking `docs/Fast_Travel.md`; settings rows (`qol_fast_travel`, `qol_fast_travel_cost`, `qol_fast_travel_flat`, `qol_fast_travel_scale`, `qol_fast_travel_prompt`, `_prompt_x`, `_prompt_y`, `_prompt_scale`) beside `qol_skip_charging`; in the bindings rows (line 1253-1254) add `travel` to the button names with "(the Travel action: no key, `back` by default)"; an RPC row for `fast_travel` beside `skip_charging`.

- [ ] **Step 3: The leak scan**

```bash
git diff main --name-only | xargs grep -nE "/ho[m]e/|/tm[p]/clau[d]e|Si[l]ent Hi[l]l|S[H]1" || echo clean
```

Expected: `clean`.

- [ ] **Step 4: Every build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows
```

Expected: all exit 0 (Windows: compiles and links; no identifier clashes with `windows.h`).

- [ ] **Step 5: The suites, both architectures**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest fast_travel fast_travel_scan ui_magick menu0_levelled ui_data save_menu interp_menu_2d ui_menus ui_system system_settings button_prompts room_browser weapon_slots input
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan fast_travel ui_magick ui_data room_browser button_prompts
```

Expected: every suite passes except the baseline's known failures (Task 1).

- [ ] **Step 6: commit-check, once more on the final tree**

Expected: `All files match`.

- [ ] **Step 7: Commit**

```bash
git add pc/docs/Fast_Travel.md pc/README.md
git commit -m "Fast travel: docs (rules, settings, RPC, tests, the scan's findings) and README

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Report**

Report: the branch and its commits; the src lines added (`git diff main --stat -- src/`); the scan's findings (Task 3 Step 4, verbatim) and whether Task 3b ran; the audit (35 of 35, or the failing points); every suite's result on both architectures; anything decided during the work (for example Task 8's current-Circle fallback). Don't merge or push.

---

## Self-review

### Spec coverage (the user's decisions and the approved design)

| Requirement | Task |
|---|---|
| 1. Unlock on by default from the first Circle once `introState >= 3`; `qol_fast_travel` turns it off | 2 (`pc_ft_whose`), 4 (settings, status case), 6 (MENU0 reads it), 12 (UI) |
| 1. The game's unlock (`unkB5 != 0`) wins, state 2 refusals included | 2 (truth table), 6 (MENU0's `== 2` reads), 8 (`game` case: 1 and 2) |
| 2. Free before the game's unlock; free / flat / scaled / game tunable; the game's costs after | 2 (`pc_ft_cost`), 4 (status: each mode), 6 (compare / display / packing), 8 (`game`: raw charged), 12 |
| 2. Free: MP 0 mask bit does not block | 2 (`pc_ft_mask`), 6 (MENU0 `TELEPORT_MASK`, 5BF94 `MAGIC_COMMAND_MASK`), 8 (`refuse`: MP 0 free), 13 (shell) |
| 3. Not `magicUsed` under the port's unlock; the game's decides after | 2, 6 (case 10 hook), 6 / 8 (`menu`: unchanged; `game`: set) |
| 4. Travel button opens the list directly (MENU0 handleShortcut, design (d)) | 10 |
| 4. Default pad Select on a Circle, keyboard unbound, `bind_key_travel` / `bind_pad_travel` rebindable | 9 (slot, defaults, probe), 12 (Bindings row) |
| 4. Prompt on a Circle when available | 11 |
| 5. Block departure when `unkA7` | 2 (R3), 6 (row greyed), 8, 10 |
| 5. No sheathed-weapon rule | not added (only `unk954`, falling, L2 / R2 held: decision 8) |
| 5. Script scan; blocks.json if found; findings listed | 3 (scan as a test; trial results in decision 11), 3b (conditional), 15 (docs, report) |
| 5. The original flat list now; the map picker later | 6 / 10 (MENU0's list); 15 (docs "Not yet") |
| Works with `ui_menus` off and on; magick_bridge uses the same predicate, cost and bits | 13 (`fast_travel_shell_smoke`), decision 6 |
| Code map verified (84.c, 15AC.c, 146C.c case 10, 5BF94.c menu bits, magick_bridge, room_warp) | "Verified code map"; 2 (probe against 84.c / 15AC.c) |
| Mechanism: predicate, cost, begin, guard, poll | 2, 4, 6, 10 |
| R1-R9 | R1 2/6/8; R2 2/8; R3 2/6/8/10; R4 2/8; R5 dropped (decision 10; current Circle as stock: 8 `here`); R6 4 (audit); R7 2/5; R8 MENU0's own (one `unk2` per session); R9 3/3b |
| Snapshot / restore `savePointStates` around Room Browser warp chains | 5 |
| Failure handling: refused before the hand-off writes nothing; arrival mismatch logged and reported, no retry | 4 (`arrival`, `pc_ft_log_arrival`), 8 / 10 (refusals) |
| Tests: fast_travel_probe; save_point_audit (audit + scan); fast_travel_smoke off and on; travel then save then decode then continue | 2; 3 + 4 (`audit`); 4-13; 7 |
| Safety guards and the round trip early | 2 (rules), 5 (warp hygiene), 6, 7 (round trip) before the button (10) and the UI (11-13) |
| Card safety, thermal lock, `-j 2`, commit-check after src changes, no game data, commit trailer | Ground rules; 6 / 10 / 15 commit-check |

Not planned: test (f) "A to B to A equals a door entry" (the destination's load is the door's own code path, `func_8007C694` writing the door record the door loads use; the `menu`, `save` and `button` cases cover arrival, saving and travelling again), the music check (no RPC readout; a zone change switches music in `_finishLoadZnd` exactly as for a door), the circle-tour soak on a late-game fixture (needs a fixture card the pinned set does not have; ask the user before adding one).

### Placeholder scan

No step says "TBD" or "similar to Task N"; every code step shows its code or the exact old and new text. Task 3b is conditional by design (it runs only if the scan or the audit finds something) and gives its data shape and hook; Task 14's "any other failing suite" names the two remedies to apply. `shell_travel` is a placeholder raising `TimeoutError` from Task 6 to Task 13, by design (the shell suite is registered in Task 13).

### Type and name consistency

- `pc_ft_settings {enabled, cost, flat, scale}`, `pc_ft_state {game, intro, special, warped, no_save, circle, here, event, busy, mask, mp, states[48]}`, `pc_ft_trip`, `pc_ft_audit_point`, `pc_ft_report` (Task 2): filled by `fast_travel.c` `settings()` / `fill()` / `pc_fast_travel_report` (Task 4), written by `pc_ft_json`, read by the smoke as `settings`, `whose`, `unlock`, `menu_bits`, `prompt`, `prompt_drawn`, `state.{game, intro, warped, circle, here, event}`, `blocked`, `why`, `points[{i, state, raw, cost, current, refused}]`, `presses`, `opened`, `ignored`, `last.{valid, to, arrived, circle, room_ok, whose, counted, mp_before, mp_after}`, `audit[{ok}]`.
- Rules: `pc_ft_here`, `pc_ft_raw_cost`, `pc_ft_whose`, `pc_ft_unlock`, `pc_ft_cost`, `pc_ft_mask`, `pc_ft_menu_bits`, `pc_ft_blocked`, `pc_ft_refused`, `pc_ft_shortcut`, `pc_ft_counts_as_magic`, `pc_ft_packed`, `pc_ft_json`, `pc_ft_log_trip`, `pc_ft_log_arrival`; tables `pc_ft_save_point_data`, `pc_ft_save_point_costs`, `pc_ft_cost_names`; constants `PC_FT_POINTS`, `PC_FT_USED`, `PC_SHORTCUT_TRAVEL` (9), `PC_FT_MAGIC_MASK`, `PC_FT_NO_MP`, `PC_FT_HERE`, `PC_FT_COST_MAX`, `PC_FT_COST_*`, `PC_FT_OFF/PORT/GAME`.
- Hooks: `pc_fast_travel_unlock` / `_cost` / `_mask` / `_blocked` (MENU0 macros `TELEPORT_UNLOCK`, `TELEPORT_COST`, `TELEPORT_MASK`), `pc_fast_travel_menu_bits` / `_mask` (5BF94 `MAGIC_COMMAND_MASK`), `pc_fast_travel_begin` (146C), `pc_fast_travel_poll` / `_room_loaded` (room_warp.c), `pc_fast_travel_why` (magick_bridge), `pc_fast_travel_press` / `_report` (dev_rpc, weak), `pc_fast_travel_prompt` / `pc_fast_travel_prompt_drawn` (fast_travel_prompt.cpp, weak), `pc_fast_travel_prompt_draw` (dev_overlay).
- Settings: `pc_qol_fast_travel`, `_cost`, `_flat`, `_scale`, `_prompt`, `_prompt_x`, `_prompt_y`, `_prompt_scale` = keys `qol_fast_travel*` (project_settings.cpp, ui_settings.cpp rows of the same ids, stubs, dev panel, RPC `set`).
- Bindings: `PC_BIND_TRAVEL` (16), `PC_BIND_SLOTS` (17), `pc_bind_slot_name`, `pc_bind_slot_index`, `pc_controller_element`, `pc_keyboard_travel`, `pc_bind_shared_bits`, `pc_controller_travel`, `pc_window_travel_held`, `pc_window_travel_shared`; `PC_PROMPT_TRAVEL` (16) in button_prompts.h; row id `bind_travel` (ui_settings.cpp, the `system {bind: "travel"}` capture).
- Smoke constants: `TELEPORT_MENU` 14, `SHORTCUT_TRAVEL` 9, items `MAGIC_ITEM` 0 / `TELEPORT_ITEM` 14 / `LIST_ITEM` 20, flags offsets 0xA7 / 0xAB / 0xB5 / 0xB7 / 0x156, savedata 0x200 / 0x6A8 / 0x1778, `INITIAL_DISTANCE` 32; helpers `menu_nav.top_command` / `DATA_COMMAND`, `ui_data_smoke.menu7_driver`.
