# Modern menus: the native Container screen (phase 7e)

Design: `pc/docs/Modern_Menus.md` ("Container"). Code: `pc/src/ui/container_ops.c/.h`
(MENUD's data operations), `container_bridge.c` (the MENUD session and the
MENU7 frame), `container_frame.h` (the model), `ui_container.cpp` (model,
mailbox, JSON), `ui_container_draw.cpp`; hooks in
`src/MENU/MENU7.PRG/260.c` (ROOD_PC). Tests: `pc/tests/container_ops_probe.c`,
`pc/tests/ui_container_smoke.py`, `data_plan_probe` (PROMPT_YESNO).

## 0. Shape

The save-room Container is two overlays in sequence, hosted two ways:

- **MENU7** `vs_menu7_saveContainerMenu` (260.c) stays the only state
  machine for the "Check the container?" prompt, the card read, the
  containerWarn and cancelWarning prompts, `_copyContainer` (with the
  CONTAINER_GEM_BYTES fix), `_showSaveMenu(2)`, the teardown and the free.
  It runs **framed** like Data: its drawing is gated, its statics are
  published, and the player's native answers are injected as single pad
  presses (never answered for the player).
- **MENUD** `vs_menuD_exec` (direct entry 14) is **ported** natively, like
  Loot, on MENU7's own `vs_menu_inventoryStorage`; no card I/O. The card
  bytes are MENU7's by construction.

The original MENUD mode model is kept 1:1 (TOP: Remove / Store / End; LIST:
8 tabs, staging, the second pick, actions, discard, sort, detail; CONFIRM:
the commit), so every pad script has exactly one native equivalent and RAM
writes are the original's expressions in the original's order. The
presentation is free; nothing original shows. One latch, at MENU7 entry
state 0: `framed = pc_ui_menus && pc_overlay_active()`; MENUD is native iff
the run is framed (a flow never changes hands).

## 1. src/ hooks (260.c only, all ROOD_PC)

- (a) the top gate block: `vs_battle_drawCursor` gated (`_promptYesNo`'s
  cursor; its animation static is presentation only).
- (b) before `vs_menu7_saveContainerMenu`: the original renamed
  `_pcContainerMenu` by a macro.
- (c) after the Data wrapper: the wrapper, calling
  `pc_ui_container_call_begin` / `_end` (data_menu7.h) around it with
  `_pcStatics`.

No change to MENUD 234.c, MAINMENU C48.c / 58EC.c, BATTLE 5BF94.c / 146C.c.
MENU7's own menu items (0 "CONTAINER", 0x1E / 0x1F YES / NO) are hidden by
MAINMENU's resident `vs_mainMenu_hideMenu = 1` while the run is latched
(`_renderMenuItems` still advances the slides); the value before is saved at
the latch and restored at the end, an abandon and a reset.

## 2. pc/src

- `container_ops.c/.h` (pure C): 234.c's index helpers, `_initContainerObject`,
  `_copyContainerItem` (3 gem bytes), defragment, the bag / container copies,
  parent lookups, staging (`func_801031A0` / `func_80103270`, `func_801066DC`,
  `func_80106784`, `func_801068BC`, `func_80106948`, `func_801069B0`,
  `func_80106A04`, `func_80106A50`), the commit, state 6's merge or swap, the
  action menu's rules (2693), the sorts (pick-max, stable, negated keys) and
  the discards; the storage passed in and read fresh on every call.
- `container_bridge.c`: the MENUD session (`pc_ui_container_begin` / `_frame` /
  `_abandon`), the per-tick `vs_mainMenu_*` pointers (234.c:3149), mode init
  (2429), rebuilds (state 1), cursor memory at the original's points with the
  original's 8-visible-row arithmetic (MAINMENU `vs_mainmenu_setMenuRows`,
  `vs_mainMenu_renderItemsList`, `vs_mainMenu_addMenuActions`,
  `vs_mainMenu_renderItemActionMenu`, `vs_mainMenu_storeCursor`), the sounds,
  the hand-back (state 6); and the MENU7 frame (latch, inject, publish, phases).
- `data_bridge.c` / `data_plan.c`: the save list of `_showSaveMenu(2)` is the
  Data screen's model and planner in a container mode (no tabs, the
  Container header); `pc_ui_data_menu7_publish` shared; a PROMPT_YESNO goal.
- `menu_bridge.c`: direct entry 14, the hand-back, abandon / reset, the
  frame's heartbeat.

## 3. Layout, controls, lifecycle, parity, tests

As `pc/docs/Modern_Menus.md` "Container" states them (layout units 1280 x 720
and 960 x 720; pad = the original's exactly; a click is the pad sequence that
does the same; hover writes nothing). Parity: S1 at MENUD's exit (the bag,
`unkC430`, `unk105B0`, `unk87B0`, `itemsToTransfer`, `containerEmpty`,
`returnState` / `currentMenu`, the source pointers; cursor memory A = B), S2
the saved card images (KEY and the play time pinned), S3 at the close.

## Task log

| Task | Work | Commit |
|---|---|---|
| T1 | container_ops.c/.h, container_ops_probe | Container: MENUD data ops port + probe |
| T2 | MENUD session (direct 14), menu_bridge, RPC container / chest >= 64 | Container: native MENUD session (direct 14) |
| T3 | ui_container.cpp, ui_container_draw.cpp (16:9, 4:3) | Container: native screen |
| T4 | 260.c hooks, the MENU7 frame, data_plan PROMPT_YESNO, hideMenu | Container: MENU7 container states framed |
| T5 | fixtures, ui_container_smoke, regressions, docs | Tests + docs: native Container |

Notes as they came:

- T1: the probe runs container_ops.c alone (`-DPC_CONTAINER_OPS_PROBE`,
  `-funsigned-char` as the game: a misc count is a char, 95 + 70 = 165).
  `func_80106C64`'s row flags: 0xCA00 (equipped icon) and 0xCC00 (combined
  icon) rows are selectable (bit 0 clear); only 0xCC01 (a staged weapon's or
  shield's part on tab 7) is not.
- T2-T4 landed as one commit (the MENUD session, the screen and the MENU7
  frame depend on each other: the session is native only when MENU7's run
  is latched; the save list is the Data screen's container mode).
- MENU7's overlay statics restart when it reloads after MENUD (as on the
  PlayStation): its `_pcRun` / `_pcFramed` come back 0 at state 8, so the
  bridge re-establishes the latch from the host's frame (a run that handed
  back); any other mismatch runs the rest original.
- The screen stays up across the overlay loads between MENU7 and MENUD
  (neither is ticked then): visible while latched with a stamp up to 120
  vsyncs old; the heartbeat ends a frame whose menus the game left.
- MENU7's information box (text box 7) is inited with the CAUTION header;
  the gate keeps its text empty but the header showed through the
  backdrop: none while framed (pc/src, presentation only).
- The panel's misc count is computed live on a host copy; `unk4BB0` /
  `D_80109A82` are refreshed only where the original refreshes them (init,
  unstaging, the misc action rules). The waiver is checked against 234.c's
  source by the smoke test.
- `with_container` builds from the save's own bag as RAM has it after the
  load; the test card's bag has no gems and no gem slots, so the weapon's
  grip gets two slots and the shield one, and the gems are synthetic
  records (ids 263..267).
- The original (A) is driven at its own input points: the smoke test reads
  MENUD's and MAINMENU's input statics (`D_80109A70`, `D_80109A68`,
  `_itemsText`, `_sortItemsTextBuf`, the rows' input row 17 ...) through the
  build's symbol table before every press; the native screen takes a press
  at once.
- The scripts run with the game's cursor memory option off (every process
  alike), so menus start where the scripts expect; the memories are still
  written and compared (A = B).
- The menus run at 15 to 30 frames a second: the scripts hold a press for 6
  vsyncs so it spans a pad read; the original's top menu keeps its cursor
  after the commit (the scripts walk `unk9E`, the same RAM in every process).
- dev_overlay.cpp hands mouse buttons to the modern menus only while one of
  their screens shows (`pc_ui_data_visible` among them): that function now
  answers for the Container's screen too (both are MENU7's framed runs),
  without touching dev_overlay.cpp. The footer's hints are not in the drawn
  targets (`hits`): the mouse scripts use a right click for Cross.
- MENU7 turns the soft reset off while it holds the card
  (`vs_main_enableReset(0)`), as on the PlayStation: set 7 resets in the
  transfer screen and checks the combination does nothing in the save list.
- 16:9 shots by pad only (the 1280-unit layout is scaled into the 960 x 720
  test window, so layout units are not pixels there).

Results (both archs, run.py): ui_container_smoke (A / B all sets, C sets 4, 1
and 7: Remove, the prompts, the cancel warning, the save list),
ui_loot_smoke, ui_items_smoke, ui_data_smoke, ui_menus_smoke; ctest 15/15
(container_ops_probe, data_plan_probe PROMPT_YESNO, ui_shell_probe and its
ASan build); builds 32-bit, 64-bit, Windows (0 fail); decl HAZARD 0;
commit-check "All files match" (260.c: 15 lines).

Gaps: a save state restored mid-run (the stale-latch rule) and the
heartbeat's abandon (the game leaving 7 / 14 without a hand-back) are
implemented but not exercised by the smoke test; the item details are a
compact card (Items-style pages are out of scope).
