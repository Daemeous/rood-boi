# Modern menus: the native Data screen (save / load)

Design: `pc/docs/Modern_Menus.md` (phase 7d). Code: `pc/src/ui/data_frame.h`,
`data_plan.c`, `data_bridge.c`, `ui_data.cpp`, `ui_data_draw.cpp`; hooks in
`src/MENU/MENU7.PRG/260.c` and `src/MENU/MAINMENU.PRG/C48.c` (ROOD_PC).

## 0. Principle

MENU7 (`vs_menu7_dataMenu`) runs unchanged and owns the pad, every decision,
sound and cursor memory, all card I/O and every write to `vs_main_settings`
and `stateFlags`. While a Data run is framed:

- (a) its GPU and text output is gated; all logic still runs (including
  `_drawPlayTime`'s keystream draw, `_drawFileMenu`'s animation steps,
  `_fileProgressCounter++` and the Triangle invalid sound);
- (b) native clicks are goals; a pure planner turns a goal into at most one
  synthetic pad press a frame, injected at the top of `vs_menu7_dataMenu`
  only while the real pad is idle; the real values are restored after the
  call;
- (c) after every call MENU7's statics are published by value into a C
  model; ImGui draws from the model.

The only MENU7 UI replaced is the two-row Save / Load chooser (states 5 and
6, a MAINMENU row list). Nothing old is drawn.

## 1. Scope

In: System > Data from the shell (row `data`, command 6, delivers 7), and
saving at a save point (Save enabled when `func_8008A4FC() != 0 &&
!stateFlags.unkA7`; the frame also greys it after a Room Browser warp).

Out (follow-ups; only Data's state 3 latches the frame, so these stay
original): the container chest (`vs_menu7_saveContainerMenu`, with the
native Container / MENUD screen), Game Over's continue
(`vs_menu7_gameOver`), TITLE's Continue and the post-ending save
(`TITLE 22C.c`), location thumbnails, save dates or a host sidecar, a
read-only card peek, a Damaged badge (MENU7 counts unreadable files as free
blocks), separate Save and Load rows in the shell.

## 2. Hooks (all ROOD_PC; `make commit-check` matches)

- H1 260.c file scope: `_pcNoDraw`, `_pcFramed`; macros gating `DrawPrim`,
  `vs_battle_renderTextRaw`, `vs_mainmenu_setInformationMessage` (each a
  statement in 260.c, no prototype after the macros, never taken as a
  pointer); `pc_text_char` returns at once while gated.
- H2 the wrapper `vs_menu7_dataMenu` (the original renamed `_pcDataMenu` by a
  macro) calls `pc_ui_data_call_begin` / `_end` (data_bridge.c) with
  `_pcStatics`, a const table of pointers to MENU7's statics
  (`pc/src/ui/data_menu7.h`; 260.c checks the mirrored struct sizes): state
  3 latches (`pc_ui_data_frame_begin(++run)`, `_pcRun` in 260.c so save
  states keep it), a save state without the host's frame falls back to the
  original look, stamp, inject, call, restore the pad, publish
  (`menu7_publish`: message lookup, location names, the files), finish on
  return (`PC_UI_DATA_CLOSED` when the call entered in state 14, else
  `_BACK`).
- H3 state 5 framed: `D_800F4E6B`, the row types and `func_8008A4DC(0)` kept;
  no `vs_mainmenu_setMenuRows`, no cursorMemory swap;
  `pc_ui_data_frame_ops(save_ok, at_point, in_battle)`. State 6 framed:
  `row = pc_ui_data_frame_op()` (0 stay, 1 Save, 2 Load, -1 back, -2
  close); the textbox-7 branch skipped.
- H4 the load's success, before `setRECT` / `ClearImage`:
  `pc_ui_data_frame_finish(PC_UI_DATA_LOADED)`, the real pad back, the gate
  off (the jump shows its loading screen before `pc_ui_menu_reset` runs).
- H5 C48.c case 0, selected menu 7 (`submenuEntrypoints[6]`) while armed:
  `targetPosition0 = 320` (the DATA title off screen) and
  `menuActionNone`; `toggleBackgroundFade(1)` kept (states 13 / 14 wait on
  it).
- menu_bridge.c: `deliver(7)` arms (and keeps the HUD out); `pc_ui_menu_open`
  ends the frame and takes its pending tab; the heartbeat ends it when
  `currentMenu & 0x3F != 7` (the HUD then back as an abandoned session's);
  `pc_ui_menu_reset` forgets it.

## 3. Model, planner, phases

`pc_ui_data_game` (by value each call): state, `_dataNotSaved`, the
container bit, `_selectCursorXy`, `_fileMenuElementsActive()`, the list's
element 5 y (the page), the message (by its MCMAN offset) and its text, the
progress counter, the sweep's x as MENU7 draws it, the target, the fade, the
five files (`_saveFileInfo`: state, generation, stats, the location's name)
while the list is up, `_findCurrentSaveOnActiveMemcard()`,
`_getNewestSaveFile()`, `D_800F4E6B` and its name, the card row MENU7 starts
on. Heap-backed values only in states 5..14, the flows' statics only in
7..10. The bridge decodes strings at once (`pc_ui_text_decode`) and keeps no
pointer.

Cursor decode (probes check the constants): card = (y - 26) / 16 at x 180;
file = (y - 62) / 40 + page at x 24 with page = (72 - element 5's y) / 40;
cancelled = (y - 10) / 16 at x 0.

Phase from one publish: OPS (state 6); BUSY outside states 8 / 10, or
elements animating, progress running or the fade; CARD / FILES / PROMPT by
the cursor's x; WAIT with no cursor and a waiting message (insertError,
emptyCard, saveFailed, loadfailed, fileCorruptDescription, saved, loaded,
noData, cardFull, formatFailed, formatCancelled, removed); else BUSY.

| Goal | Taken in | Presses |
|---|---|---|
| OP(1 save / 2 load) | OPS | the chooser returns it; then CARD(MENU7's own start row) as an automatic goal |
| CARD(n) | CARD | Up until the row is n, then Circle |
| FILE(i) | FILES | Up / Down until the file is i, then Circle (MENU7 refuses a file it does not take) |
| MOVE(i) | FILES | Up / Down until the file is i, nothing confirmed (the wheel) |
| ANSWER(yes / no) | PROMPT | yes: Up until Yes, then Circle; no: Cross |
| DISMISS | WAIT | Circle |
| BACK | CARD, FILES, PROMPT, WAIT, OPS | Cross (OPS: the chooser returns -1) |
| CLOSE / TAB(t) | OPS | the chooser returns -2, or -1 with the tab pending |
| CLOSE / TAB(t) | CARD, FILES | Cross until OPS; stops at a prompt or a message |

Rules: one press then a frame without one; nothing while the real pad is
busy; a real press clears the goal (L1 / R1 in the card rows or the list
post TAB); a publish from a call that carried a press (real or injected) is
waited out as BUSY (`settling`: MENU7's handleInput lays the list out from
the old page before it reads Up / Down, so a press that pages the list
publishes the new cursor over the old page, the file one behind); Left /
Right in the card rows swap them as Up / Down (4:3 draws the two cards side
by side); a goal waits through BUSY for at most 600 frames, expires after 60
frames in a phase that takes it and is dropped in one that does not; the
automatic goal stops at any WAIT or PROMPT; posts while BUSY are dropped and
counted; the same goal posted again (a double click) goes on where it is.

## 4. Layout

Layout units (1280 x 720 for 16:9, 960 x 720 for 4:3); the shell's backdrop
and tabs; a content panel as see-through as the other tabs (no fill of its
own: the ground and the panels at `ui_menu_opacity`, as the theme scales
them). 16:9: header (title, Save | Load with the
reason line, play time), a rail (two card pills: "n saves" / "No card" /
"Not checked", a ring on MENU7's card cursor, a check on the card in use;
"This journey"), five save cards of 840 x 76 from y 184 (the spec's 96 did
not fit above the status line), the status line at y 624 with Continue in
WAIT, the Yes / No dialog (overwrite: this journey against the save it
replaces), the footer's crumb and hints. 4:3: the rail folds into the header
(compact card pills), cards 880 x 76 at x 40, this journey only in the
overwrite dialog. Progress: MENU7's sweep (pink saving, yellow-green
loading) on the file in use, the white completion flash, the load's fade as
black over the picture and the footer. The save cards' clear count has a
drawn star (the bundled fonts have no U+2605); save counts read "SAVES
nnnn" everywhere (the cards, this journey, the overwrite dialog). The card
rows and the list keep L1 R1 Tabs in the footer. The wheel walks MENU7's
file cursor (MOVE), so the focused save is the one Confirm acts on.

## 5. Tests

- ctest `data_plan_probe`: phase x cursor x goal x real x edge, exhaustive
  (Circle only on the target, Cross only for BACK, No and backing out, no
  prompt answered for another goal, a real press clears, the edge frame),
  walks against MENU7's cursor rules, expiry, BUSY waits, CLOSE stopping at
  a prompt, the cursor decode against 260.c's formulas, the waiting
  messages.
- ctest `ui_shell_probe` (and `_asan`): the mailbox by phase, busy drops,
  the pointer's goals and hover, the decode, the dialog's focus, the JSON.
- `pc/tests/ui_data_smoke.py` (run.py, `--card`, checkpoint suite), all
  inside `card_fixtures.card_guard`: open; save parity A (original) / B
  (pad) / C (mouse, real SDL events) with the keystream and play time
  pinned at MENU7's confirm (byte-identical images required, one write,
  also after Continue and the close, no .tmp, only the target file
  changed); hover changes nothing; a double click and a
  click while saving write once; overwrite No / Yes; load parity of the
  blocks `_applyLoadedSaveFile` writes, by pad (B) and by the mouse (C),
  the fixture's files with their own play time hours so the RAM names the
  file loaded (the snapshot hashes are reported: the menu paths' own state
  differs); errors (card 2 absent, corrupt, full,
  blank, `card {fail_writes}`, `card {unformatted}`); exits.
- `pc/tests/card_fixtures.py`: `card_guard` (also in `save_menu_smoke`) and
  fixture cards (multi, full, corrupt, orphan temp, temp only, blank, wrong
  size) built as `memory_card.c` writes them from saves the game made.

## 6. Tasks

- [x] 1. `data_frame.h`, `data_plan.c`, `data_plan_probe`
- [x] 2. 260.c H1-H4, C48 H5, `data_bridge.c`, menu_bridge, JSON;
      commit-check matches
- [x] 3. `ui_data.cpp`, `ui_data_draw.cpp` (16:9, 4:3), the `ui_draw`
      dispatch, the pointer kinds, `ui_shell_probe` cases
- [x] 4. `memory_card.c` / `dev_rpc.c` card hooks, the `rood_rpc.py`
      docstring, `card_fixtures.py`, the guard in `save_menu_smoke`
- [x] 5. `ui_data_smoke` and its run.py suite
- [x] 6. Docs and this plan

## 7. Deviations from the first spec

- Card row decode: (y - 26) / 16 (not + 1): MENU7's rows sit at
  (selectedFile + 1) * 16 + 10.
- The run's finish code is taken from the state the call entered with (MENU7
  sets `*state` to 0 before returning 1).
- `menu7_gated` (JSON) counts the gated drawing calls; the smoke checks it
  grows while framed (nothing of MENU7's reaches the GPU by construction).
- A goal waits through BUSY (600 frames) and ages only where it is taken
  (60 frames), so the automatic card goal survives MENU7's own animations.
- `fail_writes` refuses writes through the warp's refusal path;
  `unformatted` holds until a format (one EvSpNEW would not reach the format
  prompt: MENU7 asks the card again after the file is picked).
- `card {}` answers data writes (`pc_psx_write` calls), one per save.
- Save cards are 76 high (96 did not fit above the status line).
- Load parity compares every block the load writes; the whole-state hashes
  differ by the menu paths' own state (MAINMENU's rows and information box,
  the chooser's sounds and the sound engine, MENU7's drawing in VRAM) and
  are reported.
- `pc_ui_data_busy()` is exported; no PC reset or quit path defers on it yet.
