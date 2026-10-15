# Modern Menus — Phase 7b: the native Map frame around MENU5

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Leá Monde > Map keeps running the game's own map screen (MENU5: its 3D wireframe map, markers, labels, sounds, VRAM, world freeze, HUD flags, CD reads and exit codes), and the modern shell draws native chrome around a cut-out of its picture: the tab strip, a header, a rail (compass, completion, visited areas with the shown area's rooms, a legend) and clickable prompts; the pointer drives the map. With `ui_menus` off (or no overlay) nothing changes.

**Architecture:** MENU5 stays the only state machine. Frame mode changes three things only: (a) Circle's area list is native (MAINMENU 58EC.c's rows list ported) but runs inside MENU5's own `displayMapMenu` state, which still does the switch; (b) `_drawUIControls` is not drawn; (c) a mailbox of pointer requests that MENU5 applies at its own input point (`processInput`) with its own statements and clamps. `_pcFramed` is latched per run at `vs_menu5_exec` state 3 and is 0 on the original path, where every hook falls through to the original statements.

- `src/MENU/MENU5.PRG/4D8.c` (ROOD_PC only): stamp, latch, publish, requests, list hand-off, controls help skipped.
- `pc/src/ui/map_frame.h`: the C interface (4D8.c, the bridge, the C++ side).
- `pc/src/ui/map_bridge.c` (battle builds): lifecycle (arm / begin / stamp / finish / end / forget), the area model, completion, the ported list, the mailbox's game end.
- `pc/src/ui/ui_map.cpp`: pure state, the mailbox, pointer requests, JSON (`ui_menu` `"map"`), the cut-out rect, the drawn pointer targets.
- `pc/src/ui/ui_map_draw.cpp`: `draw_map_frame` (4:3 and 16:9).
- `pc/src/ui/menu_bridge.c`: `deliver(6)` arms, the heartbeat ends the frame once `currentMenu & 0x3F != 6`, `pc_ui_menu_open` ends it and reopens on a pending tab, reset forgets it; `pc_ui_map_buttons` exposes the shell's pad mapping.
- `pc/src/ui/ui_draw.cpp`: `draw_tabs` factored out, the map branch, pointer routing (wheel, right, middle, drags), `pc_ui_shell_wants_mouse`; `pc/src/dev_overlay.cpp`: the mouse gate takes the map frame too.

**Ground rules:** as earlier phases: branch `claude/musing-haslett-8e55eb`, merge main first; src/ changes only inside `#ifdef ROOD_PC` in 4D8.c with the C48.c prefix pattern so the PSX token stream is unchanged, clang-formatted, `make commit-check` "All files match"; three builds (32, 64, Windows) + decl check + ctest; tests through RPC only (`launch_at('save_room')` fixtures, `--out` for artefacts); no game data, screenshots, personal paths, emails or other-project names in commits; never touch button_prompts*.c, prompt_glyphs.cpp, ui_text.c, ui_labels.h, atmosphere / fog / haze files or the present chains (pc_prompt_glyph and pc_ui_text_decode are only called).

## Facts (research brief)

**Dispatch and exits.** The shell row `{"map", ..., ROW_COMMAND, 5}` (ui_shell.cpp:49) delivers r = 6; `vs_mainMenu_exec` stores `currentMenu = 6 | 0x40` (MAINMENU C48.c:483-491) and calls `submenuEntrypoints[5]` = `vs_menu5_exec` every frame. Cross: `_quitState` 1, `func_800FAA5C` (C48.c:316-329) sets `currentMenu = 0` and calls `func_800C930C(1)` → `pc_ui_menu_open(1)` in the same frame. Triangle: `_quitState` 2, state 10 sets `currentMenu = 6`, then 0x7F. In the list, Triangle (-3) closes too (4D8.c displayMapMenu).

**`vs_menu5_exec`** (4D8.c:260-390): 3 allocates the 0xB600 VRAM backup and saves (768,227 256x1) and (672,256 96x240); 4 loads MAPBG at (640,256); 5 the scene's ARM, the world freeze (`func_8008A4DC(0)`), HUD out (`func_800CB654(1)`, `D_800EB9B0 = 0x200000`); 7 `_menuManager`; 8-9 fade, clear and restore VRAM; 10 the exit code.

**`_loadArmFile`** (4D8.c:481-549) writes `mapPaling[0] = 1` and the roomFlags[1] 0x800000 → 0x400000 fix-up, sets the camera (`vx = 568`, `vy = vz = 2048 - field yaw`, distance ONE*3/16), `_currentScene` from `vs_battle_getCurrentSceneId()` (≥ 32 → 0), `_currentSceneRow = _currentScene`.

**`_menuManager`** (4D8.c:556-960): Select toggles `_triggerHideUi`; processInput: Circle (sfx 0x7E/5, `displayMapMenu`, `_mapSelectionMenu(1)`), Triangle / Cross (sfx 0x7E/6, quit), a paled area takes nothing else, the left stick / L2 rotate (±64, pitch clamped 0..1024; the D-pad path adds while below 1024, so the pitch can end at 1080), R2 zoom (±16, ONE/8..ONE/4), Start resets (712-721), otherwise pan (±4, ±96 / ±80) and L1 / R1 rooms (`vs_main_buttonRepeat` 4 / 8, `_updateRoomIndex` walks the next / prev ring, loops forever without a visited room). The map runs a frame every second vsync. displayMapMenu: -2 back (`_setMenuItemMapName`), -3 close, a row switches (816-826: `_isPaling`, `_currentScene = _sceneRowIdMap[row]`, `initNewMap`, CD read; the shown area reloads too).

**The area list** (`_mapSelectionMenu` 4D8.c:1008-1070, `_configureMapMenu` 147-187): rows = areaFlags bits 1..31; the current row (0x80 when `_currentSceneRow == 0`); `_currentMapRow` latched once a run; cursorMemories[0..1] by the three-way formula; `vs_mainmenu_setMenuRows(n, 256, ...)` with the cursor memory forced on (58EC.c:798-850: rowInfo 1, nine rows shown, the window rules), `vs_mainMenu_renderItemsList` case 17 (58EC.c:1002-1100: Triangle -3, Circle the row, Cross -2, each writing cursorMemories[0..1] = row in the window, scroll; Up / Down with repeat, wrap on a press only).

**Rendering** (`_renderScreen` 4D8.c:973-1020): `_drawUIControls` (the controls help, strips, compass, JAMMING, text box 0's slide) anchored to the left edge; paled: the warp and text box 0 = `_paling`; otherwise `_setCamera`, `_darkenBackground`, `_renderMap` (geometry offset `(_geomOffsetX + 144, _geomOffsetY + 136)`). Markers (`_renderRoom` 4D8.c:1190, its points at 1540-1640): grey cross a door, magenta (blinking) the door Ashley came by, cyan an exit to another area (with icon 0 and a label), yellow a marked spot (Ashley's icon, the SAVE tag and the others cycling every 32 frames), a padlock for a locked door (`func_80107630`). The area name (menu item 10) sits at game y 34 with its shadow to y 51; title row 5 at y 18; text box 0 at y 224; the menu command (`_setNextAction`) at the top right, under the tab strip.

**Data.** `vs_main_mapStatus` (main.h:379: roomFlags[16], areaFlags[2]); names `_mapNames[_mapNames[i]]` (MENU5 global); `vs_main_stateFlags.mapPaling[32]` at offset 0x135; completion = popcount(roomFlags & vs_battle_mapCompletionFlags) * 100 / 361 (146C.c:17291, with a write we do not make); rooms from `vs_battle_sceneBuffer->rooms[].visited` and `_roomNamesTable`.

## Spec (as built)

### Hooks in 4D8.c (all `#ifdef ROOD_PC`)
- Include block: `pc/src/ui/map_frame.h`. Statics `_pcFramed`, `_pcRun`, `_pcPalingText`; prototypes of the helpers.
- H1 `vs_menu5_exec`: `pc_ui_map_frame_stamp(*state)` first; state 3: `_pcFramed = pc_ui_map_frame_begin(++_pcRun)` (1 only when the bridge is armed); state 10: `pc_ui_map_frame_finish(_quitState)`, `_pcFramed = 0`.
- H2 after the state switch, beside `_smoothMapToCenterpoint`: `_pcPublish()` (reads only).
- H3 processInput: (a) after the hide-UI sync, with no button down: one discrete request (`_pcApply(0, 0)`: AREA / LIST = the Circle path, QUIT / TAB = the Cross / Triangle path, HIDE, then — not while paled — ROOM, STEP (guarded: a visited room exists), RESET (712-721)); (b) at the end of the case: `var_s2 |= _pcApply(1, var_s2 | buttonsPressed)`: one merged ORBIT / ZOOM / PAN with MENU5's clamps, dropped while the pad moves the view or a button went down (the pad wins), 3 / 2 / 0 for the controls' dimming.
- H4 `_renderScreen`: `_drawUIControls` only when not framed; `_pcPalingText = _paling`.
- H9 Circle: `_pcListOpen()` instead of `_mapSelectionMenu(1)` when framed (the same rows, current row, 0x80 rule, `_currentMapRow` latch and cursor memory formula; no menu rows).
- H10 displayMapMenu: `pc_ui_map_frame_list()` instead of `_mapSelectionMenu(0)` when framed; -2 / -3 / the switch run as the original.
- H11 helpers at the end of the file: `_pcPublish`, `_pcSetRoom`, `_pcResetView`, `_pcListOpen`, `_pcApply` (cross-referenced to the original lines).

### Lifecycle (map_bridge.c)
off → armed (`deliver(6)`, "Loading map" for up to 30 vsyncs) → framed (state 3 latched) → loading / ready / list / switching / closing (from `exec_state` and `menu_state`) → off (Cross: `pc_ui_menu_open` → end, the shell reopens on Leá Monde / Map or the pending tab; Triangle: the heartbeat once `currentMenu & 0x3F != 6`; reset: forget). The chrome draws while the stamp is at most 8 vsyncs old.

### Layout (layout units; game → layout: x = W/2 + 3 (sx - 160), y = 3 sy)
- Opaque ground everywhere but the cut-out (four rects), a 1 px rule and corner ticks; "Loading map…" / "Closing" fill it opaque.
- Cut-out: game 96..312 x 52..216 → 4:3 x 288..936, 16:9 x 448..1096, y 156..648.
- Header y 102..140: area name, JAMMED / SWITCHING badge, "Ashley is here"; right: ‹ room › (clickable steps) or the paling's line, "Rooms seen k / n" (the shown area only).
- Rail (4:3 x 24..272, 16:9 x 40..424): compass (ring with yaw, needle yaw - yaw0, 4096 a turn), "Areas" with "Map NN%" and a bar, nine area rows of 30 (current pip, shown highlight, JAMMED tag, the list's cursor band), the shown area's visited rooms nested in a five-row window around the selected one (next-ring order), carets for more; wheel: cosmetic scroll.
- Legend (4:3 under the rail y 538..648, 16:9 a right column x 1112..1256): room shown / other room swatches, door, way in, other area, marked spot crosses, locked door padlock.
- Footer y 656..720: prompts with pc_prompt_glyph, all clickable, dimmed by `cmd_state` as `_drawUIControls` dims them; list focus has its own prompts. Hidden (Select): everything fades (120 ms) to a "Select Show" pill.

### Controls
Pad: MENU5's (above), plus the right stick and the triggers by System > Controls "Map controls" (settings `qol_map_controls`; `pc/src/ui/map_pad.c`, one ROOD_PC call in processInput after MENU5's own input, framed and on the original screen alike). On PC the game's stick buffer stays centred (the pad is digital), so MENU5's stick code never ran; the PC left stick reaches it as the D-pad.

| Input | Modern (default) | Original |
|---|---|---|
| Right stick X / Y | turn (yaw) / tilt (pitch), 64 a map frame at full deflection, proportional past the camera stick's deadzone, pitch 0..1024 | MENU5's stick code: pans 4 a frame (±96 / ±80, `qol_map_invert`); with L2 turns ±64; with R2 Y zooms ±16 |
| L2 / R2 alone (no D-pad, stick or Start, not both) | zoom out / in, 16 a frame at full (a trigger axis: by its travel past the press point), 512..1024 | nothing (L2 lights Rotate, R2 Zoom, as MENU5) |
| L2 / R2 + D-pad | MENU5's: turn / zoom | the same |
| Left stick, D-pad | pan (MENU5's D-pad, `qol_map_invert`) | the same |
| L1 / R1, Start | rooms, reset | the same |

Original applies the stick only while no D-pad bit is held (on the PS1 the stick won over the D-pad; here MENU5's D-pad step has already run). The frame's prompts follow the setting: Modern "R-stick Turn, L2 R2 Zoom, L-stick Pan" (stick glyphs drawn by ui_map_draw.cpp), Original "L2 Rotate, R2 Zoom". List: Up / Down (repeat, 58EC wrap), Left / Right a page, Circle the row, Cross back, Triangle close, L1 / R1 back then a TAB request (quit 1, pending tab). Pointer: left drag rotates (yaw +8 per unit right, pitch +8 per unit down), wheel zooms (64 a notch, up closer), middle drag pans (a game pixel per 3 units), right click = Back (the list's Cross while it has the focus), area row = AREA (Circle path + confirm), room row / ‹ › = ROOM / STEP (visited only), prompts their action, tab click = TAB.

### RAM (parity)
The shell writes no game RAM; every write is MENU5's or mirrors a MENU5 / 58EC statement. After close the map status, mapPaling, cursor memories, settings, menu state, `D_800EB9AF` / `D_800EB9B0`, `vs_battle_sceneBuffer`, the field camera (0x1F800014, 0x54 bytes), `vs_main_projectionDistance` and MENU5's VRAM rects equal the original path's (ui_map_smoke: no masks needed).

## Tasks

- [x] 4D8.c hooks (H1-H4, H9-H11), map_frame.h, a stub bridge; commit-check "All files match".
- [x] Bridge lifecycle, area model, completion on a copy, mailbox, the 58EC list port, menu_bridge changes, shell set_tab and JSON, ui_map.cpp.
- [x] Drawing (4:3 and 16:9), `draw_tabs`, pointer routing, the dev_overlay gate; screenshots reviewed (hidden items under the chrome, the menu command at the top right under the tabs, the SAVE tag on a yellow marked spot, 16:9 alignment).
- [x] Pointer requests (drag, wheel, middle drag, rows, prompts, tabs, right click); paling.
- [x] `ui_map_smoke.py` (parity A / B / B2, frame checks, robustness, 16:9) and run.py registration; ui_shell_probe cases.
- [x] Docs (Modern_Menus.md, README), this plan.

## Review notes

- Deviations from the spec as written: the cut-out starts at game y 52, not 48 (the area name's shadow reaches y 51 and showed as a strip); the rail nests a five-row window of the shown area's rooms (all of them pushed the other areas out of reach; 4:3 list 258..528, legend 538..648); request kinds LIST and PAD added (the Areas prompt and the list's prompts / right click); the pointer's camera requests are drained whenever the pad acts (the spec's skip would have applied them after the pad released); `_pcPublish` takes no argument (it only runs in state 7); the legend adds "Marked spot" (the yellow cross where SAVE and the other tags cycle; only the SAVE tag was confirmed by screenshot, so tags are not named); `pc_ui_draw_pointer` gained `middle`, `glyph` a tint (the prompts fade with the chrome); the JSON adds `hits` (drawn pointer targets), `map_heap_free`, `ring`, `list`.
- MENU5's pitch can end at 1080 through the D-pad (the check is before the add); the frame keeps it (parity).
- The original path publishes nothing (`_pcFramed` 0), so the off-path test waits by vsync counts.
- Loot's absence from the dev_overlay mouse gate is a separate bug, not fixed here.

## Review fixes

- The list waits for its rows as MAINMENU's does (58EC.c case 16): the pad is
  not read for the rows' frames plus the last row's slide (computed from
  `vs_battle_rowAnimationSteps`) and the ready frame; pointer requests are
  not held back. A double-tapped Circle no longer reloads the shown area.
- A click on the shown area's rail row does nothing (the list's own confirm
  of the current row still reloads it, as the original's).
- Pointer pans reach MENU5 in whole steps of 4 (the rest kept in the
  mailbox; `_pcApply` rounds too), so the D-pad's check-then-add still stops
  at ±96 / ±80.
- Framed means MENU5's latch and the bridge's frame of that run
  (`pc_ui_map_framed`): a save state restores the latch without the host's
  model; MENU5 then falls back to `_drawUIControls` and its own list
  (initialised if it was open). `pc_ui_map_note_controls` counts
  `_drawUIControls` (JSON `map_controls`).
- A tab from the list quits at once (list -4: Cross with the tab kept); no
  deferred TAB request survives into a later processInput.
- Chrome: the strip's L1 / R1 icons are lit only while the list has the
  focus, with a glyph hint under the strip; the header measures its right
  group first and fits the area name, badge and "Ashley is here" (dropped
  first) and the paling's line; the 16:9 list shows every row the rail fits;
  unlit prompts' glyphs dim (still clickable: MENU5 refuses what it would
  refuse from the pad); the footer is the other screens' (hints
  right-aligned at the margin, the crumb left of them when it fits) and the
  rail / legend start at the theme margin; Shift + left drag pans, the Zoom
  prompt turns at each clamp, a mouse hint shows under the picture while the
  pointer is over it; the Choose prompt's halves are Up and Down.

## Follow-ups

- Click a room in the 3D picture (per-room screen bounds from `_renderRoom`).
- Projection scale or re-centre so the map uses the whole cut-out.
- Native door / connecting-area labels; naming the marker tags.
- Auto-host the map on the Leá Monde tab (dwell), using the same frame.
- A scene-0 room fixture for the unselectable Map row; an unvisited-room click fixture.
- The Loot screen's dev_overlay mouse gate.
