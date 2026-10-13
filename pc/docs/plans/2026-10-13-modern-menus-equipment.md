# Modern Menus — Phase 2: Equipment (Gear + Quick swap) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Equipment tab becomes a native screen: eight slots with the equipped item, a candidate list per slot with a stat compare against what is equipped, equipping/unequipping with exactly the original menu's effect on game state, and a Quick swap sub-tab that assigns any weapon in the bag (plus a shield) to the eight field swap slots; mouse hover/click/wheel work in the shell.

**Architecture:** The bridge (`pc/src/ui/menu_bridge.c`, battle builds) fills a plain-C snapshot of equipment and candidates each tick (`pc/src/ui/ui_model.h`, no game headers) and executes requests the shell queues (`equip`, `quick_set`, `quick_clear`). Equip commits with the resident BATTLE functions (`vs_battle_equipWeapon/Shield/Armor/Accessory`, `146C.c:1440-1482`) in MENU3's exact flag order; weapon/shield models reload through a helper factored out of `pc/src/weapon_slots.c begin_swap` that never touches `D_800F196C`. Candidate stats come from `vs_battle_applyWeapon/Shield/Armor/Accessory` into locals (side-effect free). Item names come from `MENU/ITEMNAME.BIN` read natively from the disc image and decoded with an exported ui_text decoder. The shell gains per-tab "screens" with their own state; ui_draw draws them. Parity tests compare game RAM after an equip through the original MENU3 path and through the shell.

**Tech Stack:** C (bridge, game side), C++17 (shell, draw; Dear ImGui 1.93), Python RPC tests, ctest.

**Phases of the modern menus** (each its own plan, merged when green, all behind `ui_menus`):

| Phase | Screens | Notes |
| --- | --- | --- |
| 1 (done) | Shell, tabs, original screens from rows, HUD hidden | merged b9316d24 + 3fd42c93 |
| **2 (this)** | **Equipment: Gear + Quick swap; mouse** | parity tests vs MENU3 |
| 3 | Ashley summary + live model viewport (right stick turns) | native `_renderAshley` clone, yaw |
| 4 | System: settings pages (graphics, display, interface, controls, gameplay + game options) | binds project_settings + vs_main_settings |
| 5 | Abilities (chain/defense mapping, break arts) | `vs_main_settings.mapped*Abilities` |
| 6 | Items (use/discard/sort) and Magick (cast hands off to field targeting) | use-item parity tests |
| 7+ | Workshop, Container, Loot, Data/Save/Load frame, Map frame, Score, Title; then the HUD | one plan each |

**Ground rules:**
- Work on branch `claude/musing-haslett-8e55eb` (its own worktree). Before starting: merge latest `main` into the branch (main has the foundation merged as b9316d24; more is landing: Windows build, haze).
- `src/` only under `#ifdef ROOD_PC`; `make commit-check` = "All files match":
  `touch -d 2026-10-01 requirements.txt && docker run --rm -w /work -u $(id -u):$(id -g) -v $PWD:/work -v $MAIN:$MAIN -v $MAIN/tools/maspsx:/work/tools/maspsx rood-reverse-dev:local make commit-check` (MAIN = main checkout), then rebuild.
- Build: `python3 pc/tools/build_boot.py --battle` and `--battle --arch 64`; `python3 pc/tools/decl_mismatch.py --review` = HAZARD 0. Tests: `python3 pc/tests/run.py -j 2 ...` one run at a time (thermal limit).
- Files owned by the main session: `ui_text.c`, `ui_upscale*.c`, `button_prompts*`, `weapon_slots.c` is shared (small exported helpers OK; tell the main session). Keep settings additions in our own block.
- Never commit game data or anything derived from it (names, screenshots, dumps). Test output under `pc/build/`.

**Facts this plan relies on (from the research brief, file:line in the worktree):**
- Overlay code is linked natively; MENU3 functions are `static` (not callable); MAINMENU `vs_mainMenu_*` are global but valid only after MAINMENU.PRG loaded (the bridge's first ticks come from BATTLE `func_800C97BC` before that).
- Equip sequences (MENU3 `16C.c`): weapon 632-654 (`vs_mainMenu_unequipWeapon()` = `vs_battle_equipWeapon(NULL)` + clear all `weapons[i].isEquipped`; set flag; if two-handed `vs_mainMenu_unequipShield()`; `vs_battle_equipWeapon(w)`; then `_loadObjectData`); shield 807-828 (unequip shield; set flag; if the actor's weapon is two-handed unequip weapon; `vs_battle_equipShield(s)`; load model); armor 992-1003 (`old->limb = 0`; `a->limb = bodyPart; vs_battle_equipArmor(bodyPart-1, a)` or NULL; no model); accessory 466-478 (`limb = 7`; `vs_battle_equipAccessory`). Two-handed: `(0x1D4 >> (category-1)) & 1` (weapon_slots.c:123 = MAINMENU `vs_mainMenu_weaponHands`).
- Armor `limb`: 1 right arm, 2 left arm, 3 head, 4 body, 5 legs, 7 accessory; candidate category per slot `{0,5,5,2,3,4,6,7}`[bodyPart] (16C.c:860) with `limb == 0`.
- At the top level the main loop processes the object-data load queue every frame (146C.c:7978 `if (D_800F190C)`); only `func_8008A4DC(0)` stops it (MAINMENU does not call it).
- `vs_battle_apply*` (146C.c:1311-1392) compute a `vs_battle_ui*` record from an inventory record without equipping (temporary heap alloc only).
- Names: weapons `vs_main_inventoryWeapon.name[24]`; blades/shields/armor/materials via `vs_mainMenu_itemNames` which is NOT loaded at the top level (MENU3 loads it). We read `MENU/ITEMNAME.BIN` from the disc ourselves (offsets: blade `[blade->id]`, shield `[shield->base.id]`, armor `[armor->id]`, material `[material-1 + VS_ITEMNAME_BIN_INDEX_wood]`, see `build/assets/MENU/ITEMNAME.BIN.h`).
- Mouse: `dev_overlay.cpp pc_overlay_event` (~278-351) feeds motion to ImGui with the editor closed but not button/wheel events.

---

## File structure

| File | Responsibility |
| --- | --- |
| `pc/src/ui/ui_model.h` | Plain-C snapshot structs (equipment slots, candidates, quick slots) and request structs; no game headers |
| `pc/src/ui/ui_shell.cpp/.h` | + per-tab screen state (Equipment: mode gear/pick/quick/quick-pick-weapon/quick-pick-shield, cursors), request queue, JSON for the screen |
| `pc/src/ui/ui_draw.cpp` | + Equipment drawing (slot rail, candidate list, compare card, quick-swap diamonds) |
| `pc/src/ui/ui_equip_draw.cpp` | (new, if ui_draw.cpp passes ~600 lines) Equipment drawing split out |
| `pc/src/ui/equip_bridge.c` | (new, battle builds) snapshot fill from game state, request execution (equip, quick slots), names |
| `pc/src/ui/item_names.c/.h` | (new, battle builds) ITEMNAME.BIN from the disc into host memory, lookup + UTF-8 decode |
| `pc/src/ui_text.c/.h` | + exported `pc_ui_text_decode()` (main session's file: one small function) |
| `pc/src/weapon_slots.c` | + exported `pc_weapon_slots_equip()` / `pc_weapon_slots_busy()` / slot get/set helpers (factored, behaviour of the field path unchanged) |
| `pc/src/dev_overlay.cpp` | mouse buttons/wheel to ImGui while the shell is visible |
| `pc/tests/ui_shell_probe.cpp` | + Equipment state machine tests on a fake snapshot |
| `pc/tests/ui_equip_smoke.py` | (new) game test: snapshot, equip via shell, parity vs MENU3 path, quick swap, mouse |

---

### Task 1: Snapshot and request model (pure C header) + shell Equipment state machine

**Files:** Create `pc/src/ui/ui_model.h`; modify `pc/src/ui/ui_shell.h`, `pc/src/ui/ui_shell.cpp`; test `pc/tests/ui_shell_probe.cpp`.

- [ ] **Step 1: Write `pc/src/ui/ui_model.h`**

```c
/* Modern menus: what the screens show and ask for, as plain C (filled and
 * executed by the battle-side bridges; no game headers here). */
#ifndef PC_UI_MODEL_H
#define PC_UI_MODEL_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_UI_CLASSES = 6, PC_UI_AFFINITIES = 7, PC_UI_EQUIP_SLOTS = 8, PC_UI_CANDIDATES = 32, PC_UI_QUICK = 8 };
enum { PC_UI_SLOT_WEAPON, PC_UI_SLOT_SHIELD, PC_UI_SLOT_RARM, PC_UI_SLOT_LARM, PC_UI_SLOT_HEAD, PC_UI_SLOT_BODY,
       PC_UI_SLOT_LEGS, PC_UI_SLOT_ACCESSORY };

typedef struct {
    char name[48];      /* UTF-8; "" = nothing */
    char material[24];  /* UTF-8; "" = none */
    int inventory;      /* index into the game's inventory array of that kind, -1 = none */
    int dp, dp_max, pp, pp_max;
    int str, intel, agl;
    int classes[PC_UI_CLASSES];
    int affinities[PC_UI_AFFINITIES];
    int damage_type;    /* weapons: 0..2, else -1 */
    int two_handed;     /* weapons */
    int quick;          /* weapons: quick-swap slot 1..8, 0 none */
} pc_ui_item;

typedef struct {
    int valid;                                  /* the bridge filled it this session */
    int str, intel, agl;                        /* Ashley's current totals */
    pc_ui_item equipped[PC_UI_EQUIP_SLOTS];
    /* candidates for one slot (the one the shell asked for: slot field) */
    int slot;
    int count;
    pc_ui_item candidates[PC_UI_CANDIDATES];
    /* quick swap: weapon and shield inventory indices per slot (-1 none) */
    int quick_weapon[PC_UI_QUICK], quick_shield[PC_UI_QUICK];
    int busy;                                   /* a model load runs: no new equip yet */
} pc_ui_equipment;

enum { PC_UI_REQ_NONE, PC_UI_REQ_EQUIP, PC_UI_REQ_QUICK_SET, PC_UI_REQ_QUICK_CLEAR, PC_UI_REQ_CANDIDATES };
typedef struct {
    int kind;
    int slot;      /* EQUIP/CANDIDATES: PC_UI_SLOT_*; QUICK_*: 0..7 */
    int item;      /* EQUIP: inventory index, -1 = take off; QUICK_SET: weapon inventory index */
    int shield;    /* QUICK_SET: shield inventory index, -1 none */
} pc_ui_request;

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Shell API additions in `ui_shell.h`** (inside `extern "C"`):

```c
#include "ui_model.h"
/* Equipment screen data (the bridge sets it each tick before pc_ui_shell_tick). */
void pc_ui_shell_set_equipment(const pc_ui_equipment* e);
/* The next queued request (the bridge executes them after the tick); 0 when none. */
int pc_ui_shell_pop_request(pc_ui_request* out);
```

- [ ] **Step 3: Write failing probe cases** in `pc/tests/ui_shell_probe.cpp` (new function `equipment_tests()` called from `main()`): build a fake `pc_ui_equipment` (weapon slot with "Sword A" inventory 0 equipped, candidates "Sword A"(0) / "Axe B"(1, two-handed); shield "Shield A"(0)); open the shell, R1 to Equipment; check:
  - JSON has `"screen":"equipment"`, `"mode":"gear"`, eight slots, the focused slot `weapon`;
  - Circle → a `PC_UI_REQ_CANDIDATES` request for slot 0, mode `pick`; after `set_equipment` with candidates, Down + Circle → `PC_UI_REQ_EQUIP {slot 0, item 1}` and mode back to `gear`;
  - in `pick`, Cross → back to `gear` without a request;
  - in gear, Square on a slot → `PC_UI_REQ_EQUIP {slot, item -1}` (take off) (any slot, the weapon too: the game allows no weapon);
  - `busy = 1` → Circle in pick queues nothing;
  - R2 → sub-tab `quick`: JSON `"mode":"quick"`, 8 quick slots; Circle → `quick_weapon` pick mode listing all weapons (candidates for slot 0 requested), Circle on a one-handed weapon → `quick_shield` mode (shield candidates requested: slot 1), Circle on a shield → `PC_UI_REQ_QUICK_SET {slot, weapon, shield}`; a two-handed weapon skips the shield step (`shield -1`); Square on a quick slot → `PC_UI_REQ_QUICK_CLEAR`;
  - L2/R2 wrap between Gear and Quick swap; Left/Right in quick mode jumps between the face-button and D-pad groups (index ±4).
  Write each as `CHECK(...)` lines in the probe's existing style.

- [ ] **Step 4: Run, expect FAIL** (`cmake --build pc/build --target ui_shell_probe` → undefined `pc_ui_shell_set_equipment`).

- [ ] **Step 5: Implement in `ui_shell.cpp`**: a `Screen` notion per tab (`ROW_COMMAND` tabs stay as they are; the Equipment tab gets `screen = EQUIPMENT`, its own state struct `{mode, slot, pick, quick, quick_pick_w, quick_pick_s, pending_weapon}`), a fixed-size request ring (16), `pc_ui_shell_set_equipment` copying the snapshot, equipment JSON (`screen`, `mode`, `slots:[{id,name,material,dp,dp_max}]`, `candidates` in pick modes with `{name,inventory,str,int,agl,dp}` and deltas vs equipped `{dstr,dint,dagl,ddp}`, `quick:[{weapon,shield}]`). Slot ids: `weapon shield right_arm left_arm head body legs accessory`. The quick slot order matches weapon_slots.c: 1-4 Circle, Triangle, Square, Cross; 5-8 Up, Down, Right, Left. The Equipment tab no longer has the original `equip` command row (the original equip page stays reachable through Items).

- [ ] **Step 6: Run the probe, expect `ui_shell_probe: ok`.**

- [ ] **Step 7: Commit** `ui shell: Equipment screen state (gear, pick, quick swap) and request queue`.

### Task 2: Item names from the disc (`item_names.c`) and an exported decoder

**Files:** Create `pc/src/ui/item_names.h/.c`; modify `pc/src/ui_text.c`/`.h` (export), `pc/tools/build_boot.py` (battle list); test: extend `pc/tests/ui_equip_smoke.py` (Task 6) and a decode unit check in a C probe if a probe exists for ui_text (else in the smoke test via RPC).

- [ ] **Step 1:** In `ui_text.c` add (next to `charset[]`):

```c
/* A game string (0xE7-terminated, the vsString encoding: charset[] codes,
 * 0x8F space, 0xFA n spacing = one space, 0xE8 newline = space, other
 * control bytes skipped) as UTF-8 into out (always terminated). Returns the
 * UTF-8 length. For the modern menus (pc/src/ui). */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);
```

Implement with the existing `charset[]` (codepoints) and a small UTF-8 encoder; read `tools/etc/vsString.py` (lines ~404-447) for the control bytes, and the charset comment at the top of ui_text.c. Declare it in `ui_text.h`. Tell the main session (SendMessage to "rood-boi MAIN — renderer, editor & integration") that this export was added.

- [ ] **Step 2:** `item_names.c`: on first use, read `MENU/ITEMNAME.BIN` from the disc image the game uses (look at `pc/src/overlay_statics.c find_file` / `pc/src/cd_drive.c` for how a file is found by name on the image and read whole), keep it in host memory; parse it as the game does (it is a `u_short` offset table: see `tools/splat_ext/vsStringTable.py` and how `vs_mainMenu_itemNames` is indexed as `char (*)[24]` — confirm whether ITEMNAME.BIN is fixed 24-byte records (2D10.h:236 says `char (*)[24]`), and index accordingly). API:

```c
/* Names of items and materials from the disc (MENU/ITEMNAME.BIN), UTF-8. */
const char* pc_ui_item_name(int id);          /* the table's entry id (blade/shield/armor id) */
const char* pc_ui_material_name(int material); /* material 1.. ; "" for 0 */
```

Cache decoded strings (static array). Never write them to disk.

- [ ] **Step 3:** No new RPC: Task 6's smoke test checks names through the Equipment JSON (equipped weapon name equals the weapon record's own name decoded; shield name non-empty).

- [ ] **Step 4:** Build both archs; commit `ui: item and material names from the disc (ITEMNAME.BIN), pc_ui_text_decode`.

### Task 3: Weapon-slot helpers for menu equips and quick slots (`weapon_slots.c`)

**Files:** Modify `pc/src/weapon_slots.c` (+ a header if one exists; else declare in `pc/src/ui/equip_bridge.c`).

- [ ] **Step 1:** Factor `begin_swap` (weapon_slots.c ~230) into `static void queue_models(vs_main_inventoryWeapon* w, vs_main_inventoryShield* shield_or_null, int category, int wep, int material)` (the SEQ reload + `func_8009AA84` + queue entries, using `vs_battle_populateDataSlot` instead of writing `vs_battle_objectDataSlots[0..]` directly — check its signature in `src/BATTLE/BATTLE.PRG/30DB0.c:154`) and keep the field path calling it with its hold/sheathe behaviour unchanged.

- [ ] **Step 2:** Export for the menu (no `D_800F196C` writes, no sheathe/redraw, no name box):

```c
/* Modern menus (pc/src/ui/equip_bridge.c): reload actor 0's weapon (and
 * shield) models after an equip done with vs_battle_equipWeapon/Shield;
 * the main loop drains the queue at the top level. Returns 0 when queued,
 * -1 when the queue is busy. */
int pc_weapon_slots_menu_models(vs_main_inventoryWeapon* w, vs_main_inventoryShield* s);
/* A menu model reload is still running (the queue is not empty or the
 * finish has not run). Drives the finish (swap_step's 479-484 subset)
 * when the queue drains; call once per tick. */
int pc_weapon_slots_menu_busy(void);
/* Quick swap slots (the mod's bytes): slot 1..8 of weapon i, shield masks. */
int pc_weapon_slots_get(int weapon_index);               /* 0 none */
int pc_weapon_slots_shield_for(int slot);                /* shield index or -1 */
void pc_weapon_slots_set(int slot, int weapon_index, int shield_index); /* -1 shield: none; moves the weapon */
void pc_weapon_slots_clear(int slot);
```

Implement set/clear with the existing `assign`/`shield_bind` rules (a weapon lives in one slot; a two-handed weapon gets no shield; slot shield bits in `shields[i].gems[3]`).

- [ ] **Step 3:** `weapon_slots_smoke` must still pass unchanged (the field path): `python3 pc/tests/run.py -j 2 weapon_slots_smoke`.

- [ ] **Step 4:** Commit `weapon_slots: menu model reload and quick-slot helpers (field path unchanged)`; message the main session about the new exports.

### Task 4: Equipment bridge (`equip_bridge.c`): snapshot + requests

**Files:** Create `pc/src/ui/equip_bridge.c` (battle list in build_boot.py); modify `pc/src/ui/menu_bridge.c` (call it each tick: fill → `pc_ui_shell_set_equipment` → tick → pop requests → execute).

- [ ] **Step 1:** Snapshot (`pc_ui_equip_fill(pc_ui_equipment*)`): Ashley `psx_get(vs_battle_characterState->unk3C)` (`strength/intelligence/agility` current), equipped items from the actor (`->weapon`, `->shield`, `->limbs[limb-1].armor`, `->accessory`; names: weapon `name[24]` decoded; others via `pc_ui_item_name`), inventory indices for each equipped item (scan `vs_main_inventory.weapons[i].isEquipped`, `shields[i].isEquipped`, `armor[i].limb`), quick slots from Task 3 helpers, `busy` from `pc_weapon_slots_menu_busy()`.
- [ ] **Step 2:** Candidates for a slot (on `PC_UI_REQ_CANDIDATES`): weapons → every non-empty weapon record (in `vs_main_inventoryIndices.weapons[]` order, 1-based), shields likewise, armor slots → armor with matching category and `limb == 0` plus the one worn there, accessory → category 7. Stats via `vs_battle_applyWeapon/Shield/Armor/Accessory` into a local `vs_battle_uiWeapon` etc. (read their exact signatures in `146C.c:1311-1392` and copy fields: DP/PP current/max, STR/INT/AGL (current), classes 0-5, affinities 0-6, damage type; check `classAffinityCurrent` layout for weapons).
- [ ] **Step 3:** `PC_UI_REQ_EQUIP` execution, in MENU3's order (see "Facts" above), refusing while `pc_weapon_slots_menu_busy()`; weapons/shields then `pc_weapon_slots_menu_models(...)`. Play `vs_battle_playMenuSelectSfx()` on success, `vs_battle_playInvalidSfx()` on refusal.
- [ ] **Step 4:** Quick set/clear via Task 3 helpers.
- [ ] **Step 5:** Closing the menu or opening an original screen while busy: the bridge returns 0 (stays) from `pc_ui_menu_top` until not busy (so no screen starts mid-load).
- [ ] **Step 6:** Build both archs, `decl_mismatch --review` HAZARD 0, commit `ui: Equipment bridge (snapshot, candidates, equip in MENU3 order, quick slots)`.

### Task 5: Drawing the Equipment screen + mouse

**Files:** Create `pc/src/ui/ui_equip_draw.cpp` (called from ui_draw.cpp when the tab's screen is Equipment); modify `pc/src/dev_overlay.cpp` (mouse), `ui_shell.cpp` (mouse focus/click API).

- [ ] **Step 1:** Layout per the prototype (https://claude.ai/artifact/KsfQfQ9sGZF31qs775Uvbd, Equipment tab): sub-tabs Gear / Quick swap with L2/R2 prompts; Gear = 8 rows (ring icon drawn as a circle + our own simple line icon per slot kind, kind label, item name in the name font, DP bar); pick mode = candidate rows with STR/INT/AGL deltas; right card = compare (stat rows Now/After/delta, class and affinity bars with a tick at the equipped value). Quick swap = two diamonds (face buttons, D-pad) of eight cards (prompt icon via `pc_prompt_glyph` for PC_PROMPT_CIRCLE/TRIANGLE/SQUARE/CROSS/UP/DOWN/RIGHT/LEFT, weapon name, shield or "Two-handed"/"No shield"), gold dot on the equipped one; right card = slot detail + "In the field: hold L2 + R2 and press <button>". Centre column left empty (phase 3 puts the model there). Colours/sizes from the theme (add tokens `up`, `down` for deltas to ui_theme + theme.json).
- [ ] **Step 2:** Mouse: in `dev_overlay.cpp pc_overlay_event`, when the editor is not visible and `pc_ui_shell_visible()`, pass mouse button and wheel events to `ImGui_ImplSDL3_ProcessEvent` and return 1. In ui_draw, hit-test rows/cards/tabs against `io.MousePos` and latch `{hover row, clicked, wheel}` into the shell (`pc_ui_shell_mouse(int target_kind, int index, int clicked, float wheel)`); the shell applies them at its next tick (hover focuses, click confirms, wheel scrolls lists, click on a tab switches). Add probe cases for the shell side.
- [ ] **Step 3:** Build, look at 4:3 and 16:9 screenshots from Task 6's test (`--out`), fix overlap/clipping. Commit `ui: draw the Equipment screen (gear, compare, quick swap) and mouse input`.

### Task 6: Game test `ui_equip_smoke.py` with parity

**Files:** Create `pc/tests/ui_equip_smoke.py`; register in `pc/tests/run.py` (card suites).

- [ ] **Step 1:** From a copy of the card in the save room, `ui_menus` on, open the shell, R1 to Equipment; check the JSON: eight slots; the weapon's name equals the decoded `vs_main_inventory.weapons[i].name` of the equipped weapon (read RAM with `game.read`, decode with a small Python version of the charset — or compare against the shell's own decode via the dump only, if a Python decoder is too much: then check non-empty and ASCII-printable); DP values equal the actor record's (symbols: `vs_battle_characterState`, follow `unk3C` with the 32-bit layout; or compare against `weapon_slots` RPC which already reports what Ashley's battle record has equipped).
- [ ] **Step 2: Parity.** Pick a second weapon in the bag (the save must have one; if not, the test writes a copy of the equipped weapon record into a free slot in RAM before opening the menu — document it).
  (a) Original path: `ui_menus` off; open the menu, Items → the equip page (find the input sequence by stepping with screenshots; Square/L1/R1 navigate MENU3's tabs; see `pc/tests/save_menu_smoke.py` for driving MAINMENU), equip the second weapon, close; dump `vs_main_inventory` (0x540 bytes weapons/shields/blades + armor), Ashley's actor record (`unk3C`, its size from 146C.h `vs_battle_actor2`), `vs_battle_wepModels` ids.
  (b) Reload the game from the same card (new process); shell path: `ui_menus` on, Equipment, pick the same weapon; close; dump the same.
  Compare (a) and (b) byte-for-byte except fields documented as volatile (animation/timers; list them explicitly after looking at the first diff). Do the same for a shield (unequip/equip), one armor piece, the accessory.
- [ ] **Step 3: Quick swap.** In the shell: set slot 2 = a bag weapon (not equipped) + no shield; check the mod's bytes (`weapons[i].gems[3] == 2`, shield masks) as `weapon_slots_smoke` reads them; close the menu; L2 + R2 + Triangle swaps to it in the field (reuse weapon_slots_smoke's `overlay()` helper pattern); Square clears.
- [ ] **Step 4: Mouse.** With `send_mouse` (see `pc/tests/send_mouse.c` / how window_smoke uses it; needs xvfb headless mode) or skip with a note if the EGL headless mode can't take mouse events: hover the third slot → focus moves; click → pick mode.
- [ ] **Step 5:** Run 32-bit and 64-bit Vulkan: `python3 pc/tests/run.py -j 2 ui_equip_smoke ui_menus_smoke weapon_slots_smoke` and `--arch 64 --renderer vulkan`. Commit `ui_equip_smoke: equipment snapshot, parity with MENU3 equips, quick swap`.

### Task 7: Docs, full suites, merge request

- [ ] Update `pc/docs/Modern_Menus.md` (status: phase 2 landed; phases table), `pc/README.md` Modern menus section (Equipment screen, Quick swap, mouse).
- [ ] Full suites `python3 pc/tests/run.py -j 2` then `--arch 64 --renderer vulkan -j 2`; commit-check; merge latest main into the branch; send the main session a merge request with the results; ask the user to playtest.
