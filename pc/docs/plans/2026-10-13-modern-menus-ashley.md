# Modern Menus — Phase 3: Ashley (status summary + live model) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Ashley tab becomes a native status summary (HP/MP/RISK, weapon and shield, STR/INT/AGL, condition per limb, status effects with remaining time, location and play time) with Ashley's live 3D model in the centre column, turned by the right stick or a mouse drag; the original Status screen stays one button away.

**Architecture:** A new battle-side `pc/src/ui/ashley_bridge.c` fills a plain-C `pc_ui_ashley` snapshot each tick and runs the model: while the Ashley tab shows (and MAINMENU is resident and the load queue is empty) it freezes the world (`func_8008A4DC(0)`), builds Ashley's menu clone in actor slot 1 (`func_800F9A24(0)`, MAINMENU 224.c, no MAINMENU statics), and each tick draws it with its own camera (MENU4's orbit formula with our yaw/pitch, centred on the layout's model column with `SetGeomOffset`), restoring camera, view matrix, projection distance and geometry offset right after. It tears everything down before any original screen starts, on tab change, close, abandon and soft reset. The shell gets an Ashley screen (rows: weapon, shield, condition, effects) and the draw layer leaves the centre column open so the model shows through.

**Tech Stack:** C (bridge), C++17 (shell, draw, Dear ImGui 1.93), Python RPC tests, ctest.

**Ground rules:**
- Branch `claude/musing-haslett-8e55eb` (its own worktree), already at main 3193d1e0. Merge latest `main` before the merge request.
- `src/` only under `#ifdef ROOD_PC` + commit-check ("All files match"); builds `python3 pc/tools/build_boot.py --battle`, `--battle --arch 64`, `--battle --target windows --nrd off` (report line 2: 0 fail; rebuild `--battle` after); `decl_mismatch.py --review` HAZARD 0. Tests `python3 pc/tests/run.py -j 2 ...`, one run at a time (thermal). Never build in the main checkout.
- Never commit personal paths, emails, other-project names, game data, names or screenshots; scan the branch diff for them before the merge request.
- Other agents today: fog fix (atmosphere/environment code) and button-prompt styles (button_prompts.c / prompt_glyphs.cpp; `pc_prompt_glyph` API stays). Don't touch those files.

**Facts (research brief; file:line in the worktree):**
- Status word: `vs_battle_actor2.statuses` (0x948) + `statusTimers[8]` (0x94C) (`146C.h:552-553`). Display bits: `int vs_battle_getStatusFlags(vs_battle_actor2*)` (`5BF94.c:3000`); display bit i ↔ string `VS_status_INDEX_strDown + i` in MENU4's `status.vsString` (`build/assets/MENU/MENU4.PRG/status.h:67-126`; entries "NAME" 0xE8 description; also limb texts `critical..excellent`). Buff vs ailment = MENU4's palette mask `0x0F0F906A` (set bit = buff palette). Timers (units ≈ seconds, tick once per 60 frames): [0] STR/INT/AGL up/down (raw 0x7E0, shared), [1] quicken (0x810), [2] equip + elemental (0x1FF80000, shared), [3] paralysis (0x2000), [4] numbness (0x8000), [5] poison/regen tick interval (NOT remaining time); silent, curse, magic ward: no timer. The original shows no times.
- Limbs: `vs_battle_getLimbStatus` (`5BF94.c:3016`), enum `5BF94.h:491` (critical hp<2, damaged hp*4<max, wounded hp*4<max*3, good hp<max, excellent); Ashley's `limbs[]` order R.ARM, L.ARM, HEAD, BODY, LEGS; count = first `nameIndex == 0`; names `vs_battle_limbNames[nameIndex]` (`5BF94.h:689`).
- Location: live room `D_800F1AB0.zndId/.mpdId`; names via `pc_rooms_count()`/`pc_rooms_get(i)` (`pc/src/room_warp.h:21-22`, weak; first call reads the disc: build once at shell open). Play time `vs_main_gametime` (`main.h:494`, `.t.h/.m/.s/.f`).
- Rank lives in MENU9 statics only: out of scope (Score phase).
- Clone: `func_800F9A24(int actor)` builds slot 1 (≈45 KB heap; its `func_800F9A78` SPINS on `vs_battle_processObjectDataQueue()` — never call it with queue entries pending), `func_800F9EB8(MATRIX*)` animates + draws (−1 if no clone), `func_800F9E0C()` frees. MENU4 lifecycle: init `120.c:2336-2370` (`D_800EB9AE = 1`, `func_8008A4DC(0)`, H 512, backups), orbit `120.c:2195-2218`, teardown `2793-2806`.
- `func_8008A4DC(int)` only sets `D_800F190C`, which gates in `vs_battle_exec` (`146C.c:7978-7988`) the sky, actors' animation+draw (`func_800AEF94`), **the load queue**, `SetGeomOffset(160,112)`, labels. The menu tick (and so `pc_ui_menu_top`) runs before the gated world draw and before `DrawOTag`: drawing the clone from the bridge is valid (OT/prim pointers set), as MENU4 does. Keep the world frozen while the clone shows (else it is drawn twice).
- Screen mapping: layout x = W/2 + 3·(sx − 160), layout y = 3·sy (W = 960 / 1280; PsyX narrows about 160 in 16:9). Centre the model at layout X with `xOff = 0` and `SetGeomOffset(160 + (X − W/2)/3, 112 + dy)` around the draw; scale with H > 512.
- While frozen the load queue doesn't drain: pump `if (vs_battle_objectDataSlots[0].dataType) vs_battle_processObjectDataQueue();` each tick (only the Ashley tab freezes; Equipment equips happen with the world live, so no clone rebuild is needed in this phase).
- Right stick: `pc_window_stick(float out[2])` (`boot_window.c:180`) shaped with `pc_camera_stick_shape` + `pc_camera_stick_deadzone/_curve/_speed/_invert_x` (`camera_stick.h`); RPC stick via `pc_rpc_stick` like `camera_stick.c read_stick`. Mouse: `read_pointer` in `ui_draw.cpp` (layout units) → a drag API like `pc_ui_shell_mouse`.
- Existing bridge state: `menu_bridge.c` (`pc_ui_menu_open/top`, `deliver`, `abandon`, heartbeat, `pc_ui_menu_reset`, `mainmenu_resident()`), `equip_bridge.c`, `weapon_slots.c` (`pc_weapon_slots_menu_can_load`).

---

## File structure

| File | Responsibility |
| --- | --- |
| `pc/src/ui/ui_model.h` | + `pc_ui_ashley` snapshot (no game headers) |
| `pc/src/ui/ui_shell.cpp/.h` | + Ashley screen (rows, inspector focus, original-Status action), model view input (yaw/pitch deltas from stick + drag), JSON |
| `pc/src/ui/ashley_bridge.c` | (new, battle) snapshot fill + model lifecycle/camera/draw/pump |
| `pc/src/ui/menu_bridge.c` | call ashley_bridge each tick; tear the model down before deliver/abandon/reset |
| `pc/src/ui/ui_ashley_draw.cpp` | (new) Ashley screen drawing + centre cutout |
| `pc/src/ui/ui_draw.cpp` | backdrop with a cutout when the screen asks for one |
| `pc/tests/ui_shell_probe.cpp` | + Ashley screen cases |
| `pc/tests/ui_ashley_smoke.py` | (new) game test |

---

### Task 1: Ashley snapshot + shell screen (pure)

**Files:** `ui_model.h`, `ui_shell.h/.cpp`, `pc/tests/ui_shell_probe.cpp`.

- [ ] **Step 1:** Add to `ui_model.h`:

```c
enum { PC_UI_LIMBS = 6, PC_UI_EFFECTS = 32 };
typedef struct {
    char name[32];          /* UTF-8 */
    int hp, hp_max;
    int state;              /* 0 critical, 1 damaged, 2 wounded, 3 good, 4 excellent */
} pc_ui_limb;
typedef struct {
    char name[32];          /* UTF-8 */
    char help[160];         /* UTF-8 description */
    int buff;               /* 1 buff, 0 ailment */
    int seconds;            /* remaining, -1 = no timer */
} pc_ui_effect;
typedef struct {
    int valid;
    int hp, hp_max, mp, mp_max, risk;   /* risk 0..100 */
    int str, intel, agl;                /* as the original Status shows them */
    int limb_count;
    pc_ui_limb limbs[PC_UI_LIMBS];
    int effect_count;
    pc_ui_effect effects[PC_UI_EFFECTS];
    char area[64], room[96];            /* UTF-8 location */
    int play_h, play_m, play_s;
    int model;                          /* 0 none, 1 loading, 2 shown */
    float model_rect[4];                /* layout units: where the model is drawn (x, y, w, h) */
} pc_ui_ashley;
```

Shell API: `void pc_ui_shell_set_ashley(const pc_ui_ashley*)`; `int pc_ui_shell_wants_model(void)` (Ashley tab is current and the shell is visible); `void pc_ui_shell_model_drag(float dx, float dy)` (layout units, from ui_draw); `void pc_ui_shell_model_turn(float* yaw_delta, float* pitch_delta)` (consumes accumulated drag; the bridge adds the stick itself).

- [ ] **Step 2: Probe cases** (write first, see them fail): Ashley is tab 0; rows `weapon`, `shield`, `condition`, then one row per effect (`effect0..`); Up/Down move; Circle on weapon → Equipment tab Gear with the weapon slot focused (no request); Circle on shield → Equipment shield slot; Square (ALT) anywhere on Ashley → result 5 (original Status, command 4) like the old Status row; JSON `"screen":"ashley"`, `"ashley":{valid,hp,hp_max,mp,mp_max,risk,str,int,agl,limbs:[{name,hp,hp_max,state}],effects:[{name,buff,seconds}],area,room,play:"h:mm:ss",model,row}`; `pc_ui_shell_wants_model()` true only on the Ashley tab while visible; drag accumulates and `model_turn` consumes it.
- [ ] **Step 3:** Implement (Ashley tab = native screen, replacing its Status command row; the old row stays reachable through Square). Run the probe → ok.
- [ ] **Step 4:** Commit `ui shell: Ashley screen (summary rows, jumps to Equipment, Square = original Status), model view input`.

### Task 2: Ashley data bridge (no model yet)

**Files:** create `pc/src/ui/ashley_bridge.c` (battle list in `build_boot.py`); modify `menu_bridge.c` (call `pc_ui_ashley_fill()` each tick before the shell tick).

- [ ] Fill from `psx_get(vs_battle_characterState->unk3C)`: HP/MP/RISK; STR/INT/AGL exactly as the original Status (`_renderBasicStats`, MENU4 `120.c:745`: check what it adds, e.g. the accessory — match it); limbs (count, names decoded with `pc_ui_text_decode` or plain ASCII as `vs_battle_limbNames` stores them, hp, `vs_battle_getLimbStatus`); effects from `vs_battle_getStatusFlags` bits in display order, names/help from MENU4's `status.vsString` included as the game's own generated asset (`#include "build/assets/MENU/MENU4.PRG/status.h"` and the vsString array as MENU4 does; split at 0xE8; decode), buff from the palette mask, seconds from the timer table above (−1 when none); location from `pc_rooms_*` (cache the zone/room → name lookup at shell open); play time from `vs_main_gametime`.
- [ ] Runtime sanity (RPC from a card copy; print the `ui_menu` JSON on the Ashley tab; compare HP/MP/RISK with the `state` RPC; apply a status in RAM if the save has none, e.g. set a STR-up bit + timer, and check the effect row) — no outputs committed.
- [ ] Builds (32/64/Windows), decl check; commit `ui: Ashley summary data (stats, limbs, status effects with time left, location, play time)`.

### Task 3: The live model

**Files:** `ashley_bridge.c`, `menu_bridge.c`.

- [ ] **Lifecycle** (`pc_ui_ashley_model_tick()`, called from `pc_ui_menu_top` each tick after the shell tick):
  - Enter when `pc_ui_shell_wants_model()` && `mainmenu_resident()` && `pc_weapon_slots_menu_can_load()` && no held result: back up `vs_scratch.viewMatrix`, `vs_scratch.camera`, `vs_main_projectionDistance`; `func_8008A4DC(0)`; `D_800EB9AE = 1`; `func_800F9A24(0)`; skip drawing this tick; `model = 1`.
  - Each later tick while wanted and `D_800F4538[1]` non-NULL: pump the queue (above); `vs_battle_setProjectionDistance(H)` (H from the theme? start at 512, tune to the column height); camera = MENU4 orbit with our `yaw` (start 0xB00) and `pitch` (0..896, start 0), `xOff = 0`; `SetGeomOffset(160 + (X − W/2)/3, 112 + dy)` for the model column centre X; `func_8007ACB0(); func_800F9EB8(&vs_scratch.viewMatrix);` then restore camera/view/H and `SetGeomOffset(160, 112)`; `model = 2`; publish `model_rect`.
  - Leave (tab change, any `deliver`, `abandon`, close): `func_800F9E0C()`; restore the backups; `func_8008A4DC(1)`; `D_800EB9AE = 0`. Must run BEFORE an original screen starts (it builds its own slot-1 clone and calls `func_8008A4DC(1)` itself on exit). On `pc_ui_menu_reset` only forget the state (don't free into a reset heap).
  - Heap: check `func_800F9A24` results (slot 1 non-NULL) and give up quietly (`model = 0`) on failure.
- [ ] **Turning:** yaw += stick x · `pc_camera_stick_speed` (deg/s) · dt (vsync-based) · 4096/360 (honour `pc_camera_stick_invert_x`), pitch from stick y, clamped 0..896; plus the shell's drag (`pc_ui_shell_model_turn`: 1 layout px = 8 units yaw). Never auto-rotate.
- [ ] Interactions to verify by hand with RPC + screenshots: Ashley tab → Equipment → Ashley (model rebuilt, world unfrozen in between: the Equipment screen's backdrop shows the live scene again); Square → original Status (its own model works; back → our model again); equip on Equipment then Ashley (new weapon in hand); close from Ashley (world runs, HUD back); soft reset on Ashley (title fine, next game fine).
- [ ] Builds + decl; commit `ui: Ashley's live model in the Ashley tab (menu clone, own camera, right stick / drag turns it)`.

### Task 4: Drawing the Ashley screen

**Files:** create `pc/src/ui/ui_ashley_draw.cpp` (C++ list), modify `ui_draw.cpp` (backdrop with a cutout rect: draw `t.ground` as four rectangles around `model_rect`; inside, a soft vignette at low alpha so the model reads), `dev_overlay.cpp` only if the drag needs it (it shouldn't: `read_pointer` + left button).

- [ ] Layout per the prototype's Ashley tab (summary): left rail — HP/MP/RISK meters (three columns), weapon and shield rows (icon, name, DP/PP bars, material, type), STR/INT/AGL diamonds, Condition block (two-column limb bars with state labels and colours), Effects chips (name + time); centre — the model (cutout) with a small "Turn" prompt (right stick glyph from `pc_prompt_glyph(PC_PROMPT_R3?)` if available, else our own small icon); right — the inspector card for the focused row (weapon/shield: class + affinity bars; condition: worst limb explained; effect: description + time). Hints: Turn, Change (Circle on weapon/shield), Square "Status (original)", Triangle Exit menu. Crumb: area · room.
- [ ] Mouse: drag inside `model_rect` → `pc_ui_shell_model_drag`; rows hover/click as elsewhere.
- [ ] Screenshots (4:3 and 16:9) reviewed for overlap/clipping and the model centred in its column; fix. Commit `ui: draw the Ashley screen (summary, condition, effects, model column)`.

### Task 5: Game test `ui_ashley_smoke.py`

- [ ] From a card copy in the save room, `ui_menus` on: open (Ashley tab): JSON values = RAM (HP/MP/RISK, limb hp, effects after setting one in RAM with its timer); `model` reaches 2 within N frames; the model column's pixels differ from the backdrop and change after a stick turn (RPC stick) and after a drag (pointer RPC); R1 → Equipment: `D_800F190C` back to 1 (world live), slot 1 freed; R1 back → model 2 again; equip a weapon on Equipment then Ashley → JSON weapon name changed and model 2; Square → original Status (currentMenu 0x45), Cross back → Ashley model 2; Triangle closes: `D_800F190C` 1, `D_800EB9AE` 0, HUD flag back, camera/H equal to their values before the menu opened (read `vs_main_projectionDistance` and the camera in RAM before/after).
- [ ] Re-run `ui_equip_smoke` (parity must still pass), `ui_menus_smoke`, `weapon_slots_smoke`, editor snap/undo smokes; 32 GL and 64 Vulkan. Register in `run.py` (one line block).
- [ ] Commit `ui_ashley_smoke: data, live model, tab/screen transitions, state restored`.

### Task 6: Docs, review, merge request

- [ ] `pc/docs/Modern_Menus.md` status + README Modern menus section (Ashley tab, turning the model).
- [ ] Code review (subagent), fix findings.
- [ ] Merge main; commit-check; three builds; decl; suites as above both archs; leak grep; merge request to the main session; tell the user.
