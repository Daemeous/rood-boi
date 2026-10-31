# The classic HUD and menus drawn in Lua

Status: in progress. Phase 0 done; phase 1's battle-status-panel drawing
migration is done (2026-10-23, branch `classic-lua-step4`), and so are buff
and damage (step 5), step 7's actor words, timer and emotes (2026-10-28,
branch `classic-lua-step5`), step 6's frames, band, text and images
and step 8's targeting (same day, branch `classic-lua-step8`; see "Progress"
below), with the remaining phase 1 work listed separately. Phase 2 is under
way (branch `classic-lua-phase2`): the menu model, the shared frame's hooks,
and its rows, hand and command title drawn in Lua through the Option screen. Phase 3 has begun with
Items (branch `classic-lua-items`): the shared frame's kinds, the tab arrows and the background with them; the Quick
Manual and Status followed (branch `classic-lua-manual`); Items' own panels are piece 2 (branch
`classic-lua-items2`). It
follows `2026-10-18-lua-visible-layer.md` (F1-F3, done),
`2026-10-18-lua-m1-options.md` (done) and F5 of
`2026-10-18-freedom-and-fast-iteration.md` (the lint).

## Progress

2026-10-31 (branch `classic-menuf`, from main `f6d70e45`; no merge or publication):

- **MENUF drawn by rood.classic: the last MAINMENU screen (currentMenu 15).** Design: §3.4b (the worker's calls). The
  previous survey's "BATTLE's TIM sprite list" is not a drawing path: 146C.c `func_8007DFF0` / `func_8007E0A8`
  reserve and free VRAM slots, `func_8007DD50` .. `func_8007DE5C` set BATTLE's countdown effect; MENUF draws through
  the ordering table from a few leaves and uploads at once. Two kinds, claimed on 15 only:
  **`menuf`** (a `PC_CLASSIC` range around `_execMenu`; the record: the frame's leaf calls in order, the `_disMap`
  entries carried, the prompt's words as codes) and **`menuf_load`** (`PC_CLASSIC_IMMEDIATE` ranges around
  `_initMenu` and `_execMenu`: the TIMs and CLUT at the opening, the end screens' pulsed CLUT row each frame; the
  recorder keeps the pixels, `d.image_load` takes the record's handle). 3B8.c and `src/renderTextures.h` change
  under `ROOD_PC` only (ENDING's copy of the header: the hook's empty default). No Zig port touches either.
- **1. Reaching the screens:** RPC `menuf {submenu, boss}` (loot_bridge.c, beside RPC `container`): from the save
  room's field, the submenu set, BATTLE's own opener (`func_800CB23C`, as after a fight). Every screen reached:
  the congratulations (0), the time attack's start (1; courses 1..7) and end (2), the cube puzzle's start, end and
  quit (4..6); 3 (the heal bonus) draws nothing. The congratulations' exit raises a stat and the screens write the
  score data: the tour rewinds after each screen, the proofs put the score data back (the bonus stays taken in that
  run's memory; no card is written).
- **Primitives** (Extensions.md): `d.image_load`'s pixels may be a record's upload handle (`e.load.uploads[i].pixels`:
  the game's own pixels, any size). The parity normaliser leaves out a shaded polygon's colour pads (the top byte
  of its second to fourth colours: setRGB1..3 leave it). `PC_HUD_CLASSIC_COMMANDS` 256 -> 512 and
  `PC_CLASSIC_CAPTURE` 384 -> 512 (the congratulations' stat wheel: about 270 packets). `look.menuf` (the leaves'
  steps, ramps, wipe, wheel bands and CLUTs, the prompt's slide; `place`).
- **Tours** (`tour_menuf`, after `tour_loot`): each screen played through and left with Circle (the time attack's
  start on courses 1 and 7: the boss's name in one picture or two), then again skipped with an early Circle.
  **Proofs** (default and fallback of both kinds, state parity 120/120): the congratulations' bonus chosen, the time
  attack's records, the cube puzzle's end and its quit, at rest. Outside the gate: the game's frame buffer with the
  claims and without them identical on every screen (0 pixels; the presented picture differs by a few hundred
  pixels in the field's background between two runs of the game alone, so it is not the measure); a one-pixel
  change caught at once (`look.menuf.place.dx = 1`: 190 of 190 frames on `xy`; the uploads one pixel right: 53 of
  53 on `word`).
- **Counts** (the tour alone, GL): `menuf` 1,709 frames, `menuf_load` 636, 0 mismatches, 0 skips, no waivers.
- **Verification:** `--arch 64` and `--battle` builds. Docker `make commit-check` (a copy of the tree; 3B8.c and
  `src/renderTextures.h` touched): **All files match**, the formatter changing nothing. `run.py -j 1 --cpu-jobs 1`
  (nice 10) `classic_parity hud classic cutscene_player_smoke widescreen_smoke` plus every suite matching `manual ui_
  menu prompts button options items equip status workshop container loot memory card save data` (44 suites):
  **GL 43/44, then classic_parity alone: pass** (its first run's four MENUF proofs found no kind drawn: menuf and
  menuf_load were missing from its default `--kind` list; added). **Vulkan 44/44**, first run. classic_parity: no
  budget faults on either GPU; every report green (menuf 1,709 frames, menuf_load 636; 139 menus states).
  classic_hud_smoke's Lua cost: GL mean 0.043 ms, Vulkan mean 0.054 ms (p99 0.097), under 0.1.
- **Left:** none of MAINMENU's screens is the game's own drawing now. Untoured (the same leaves, claimed): the
  congratulations' wheel left to its own stop (2,072 frames), the time attack's end without a new record, courses 2
  to 6 (the boss's name in one picture, as course 1); the ENDING's copy of the leaves (not claimed).

2026-10-30, late night (branch `classic-immediate`, from main `45c74602`; no merge or publication):

- **An immediate-draw capture (`PC_CLASSIC_IMMEDIATE`; design: §3.4a, the worker's calls) and MENU7's file menu drawn
  by rood.classic.** Two kinds, both claimed on currentMenu 7 only (Data's SAVE / LOAD, the save list after Storage;
  not the game over's): **`file_menu`** (`_drawFileMenu`'s frame, two ranges: the bottom band's sprites before the
  panels; the band's shaded rows, caution, message tile, hand, message and fade after) and **`file_element`**
  (`_drawFileMenuElement`, wrapped: a card's or a file's panel, its words, the location picture with its CLUT swapped
  in and back, No., MAP / SAVE / CLEAR / TIME, the clear badge, the HP / MP gauges, the progress animations). 260.c
  changes under `ROOD_PC` only (the wrapper, the two ranges, the printer's blink step's address); no Zig port touches
  MENU7. The records carry the game's words as codes and the two location CLUTs as data (`d.image_load`'s pixels).
- **Primitives** (Extensions.md): `d.packet` takes 1..15 words (the panel's draw mode + tile + shaded polygon in one
  tag: 12); `d.image_store` / `d.image_load` / `d.image_restore` (VRAM transfers in their place among the packets);
  `d.text_glyph` (a packet reported to ui_text as a printer's glyph, as MENU7's printer reports its own). The Lua
  draw (`look.file_menu`: the printer's widths, the info sprites, the panel and gauge colours, the frame) is made in
  the game's order. While a claim draws, the game's own glyph reports are left out (`pc_classic_immediate_held`).
- **Counts** (classic_parity's menus game, GL): `file_menu` 1,844 frames, `file_element` 5,432 (tour_data's SAVE and
  LOAD: the cards, card 1's files walked past the last page and back; tour_storage's save list), 0 mismatches,
  0 skips, no waivers, first run. Proofs (default and fallback of both, state parity 120/120): Data's SAVE file list,
  Data's LOAD file list (new, in the save room: SAVE is not taken in the workshop the Setup proofs warp to), the
  save list after Storage (now with both kinds). Outside the gate: the presented picture with the claim and without
  it identical (a card with a save: picture, gauges, words), and a deliberate change (every packet's third word + 1)
  caught at once (66 of 88 frames mismatched on `xy`; the 22 others were the panels the draw skips).
- **Bug found on the way (own):** the first state proofs failed at `state_bss+0x5000`: the site's `lua[]` and the
  draws' VRAM buffer were statics of hud_bridge.c, whose bss a save state holds, written only when Lua draws. The
  claim's draw now keeps nothing there (the buffer is classic_parity.c's, outside the state sections).
- **Untoured** (claimed, the same draw): the progress animations of an actual save or load (no test saves), a slot
  in use by another game and a temporary slot, 100 hours or 100% map; the title's LOAD and the game over's file menu
  (not claimed: currentMenu 7 and no game over only).
- **MENUF (not started, surveyed):** `vs_menuF_exec`'s screens (congratulations, time attack start / end, the cube
  puzzle's start / end / quit; `vs_main_stateFlags.menuFSubmenu`) draw through BATTLE's sprite list (146C.c
  `func_8007DDB8` .. `func_8007E0A8`: parsed TIMs, `LoadImage` of their pixels) and `vs_battle_renderTextRawColor`;
  none of the existing kinds (rows, hand, panels) is drawn there, so there is nothing to claim with them. Each screen
  is a direct entry (a boss room in time attack, a puzzle room), with no tour yet.
- **Verification:** `--arch 64` and `--battle` builds. Docker `make commit-check` (a copy of the tree; MENU7 touched):
  **All files match** (the formatter's wraps of the new lines kept). `run.py -j 1 --cpu-jobs 1` (nice 10)
  `classic_parity hud classic cutscene_player_smoke widescreen_smoke` plus every suite matching `manual ui_ menu prompts
  button options items equip status workshop container loot memory card save data` (44 suites): **GL 42/43 plus
  classic_parity alone (pass); classic_hud_smoke's Lua cost check failed once (0.185 ms against 0.1, the Docker build
  running beside it), passed alone (0.058 ms). Vulkan 43/44; ui_workshop_smoke failed once (an item's value fb vs fc
  in its modern / classic parity, not MENU7's), passed alone.** classic_parity: no budget faults on either GPU.

2026-10-30, night (branch `classic-lua-tours`, from main `5a4d200d`; no merge or publication):

- **The menu states left as the game's own drawing: toured and claimed, in the order asked.**
  1. **MENU8's rename grid from Setup (done).** Two new kinds, each a `ROOD_PC` `PC_CLASSIC` emit-only range in
     21A0.c (MENU8 has no Zig port; not a vsstring region): **`rename_grid`** (`_renderRenameMenu`: the Cross / Square /
     Triangle buttons and the pad's sprite, the four commands' bars of 12 shaded lines fading by their counters, the
     commands' words, the name typed, at rest the L1 / R1 arrows and the name's window, the character table, the
     background's four polygons; about 250 packets in four slots) and **`rename_box`** (`_highlightSelection`: the
     character under the hand, the name's pen). Their record carries the game's words as charset codes (no game text
     in the plugin). BATTLE's hand is claimed on 8 too (the rename's Yes / No, `_confirmationScreen`; the Option
     screen's own menus draw none). Primitives widened (Extensions.md): `d.packet` takes 1..9 words (the grid's
     packets are one tag each: a draw mode, a shaded line, a draw mode; a draw mode and a shaded polygon);
     `d.font_text` takes a table of codes and a fixed step. `PC_CLASSIC_CAPTURE` 160 -> 384 (the grid's packets).
     Tour (`tour_rename`, in `tour_setup`): Rename, the first weapon, Yes; the grid walked past every edge, two
     characters typed, one taken back, the pen moved (L1, R1); the commands walked round, Insert, Delete and Back taken
     (the bars' fades), Right back; Start: its Yes / No walked, Cross (the grid slides in again); Start, the third row
     (no change): out to Setup's commands. The gate was shown to catch a change (the table one pixel right, the box's
     colour one step: every frame mismatched). Proofs: the grid at rest, its Yes / No at rest.
  2. **MENU7's save list after Storage (toured; its own drawing stays the game's).** `tour_storage` now walks the
     first prompt's Yes / No, then at MENUD's top Cross: MENU7's warning (its Yes / No walked), Yes: the save list (the
     cards, card 1's files walked, out to the warning again), No: left, nothing saved. BATTLE's hand is claimed on 7
     (`BATTLE_CURSOR_SCREENS`: MENU7's only `vs_battle_drawCursor` is the container's `_promptYesNo`). The save list
     itself (`_drawFileMenu`, `_drawFileMenuElement`: panels, files, location pictures, play time, HP / MP, the hand,
     the progress animations) **cannot be claimed by this method**: it draws with `DrawPrim` at once (and swaps CLUTs
     with `LoadImage`), never through the ordering table, so there is no packet range to capture or suppress. It needs
     an immediate-draw capture of its own (later work). Proofs: the warning's Yes / No and the save list at rest
     (shared rows and command title; the hand).
  3. **Setup's OK in Assemble and Combine, Loot's discard picker (done).** `tour_setup_picks` (in `tour_setup`'s
     workshop, rewound after): two weapons not equipped taken apart (ui_workshop_smoke's `o_disassemble`), then
     Assemble with a blade and a grip picked, Combine (Blades) with two blades picked: the OK beside them
     (`_drawOk`), walked, left. `tour_loot`: Organize inventory's misc row, Discard, the amount picker walked;
     `discard_count` now claimed on Loot (12). Proofs at both OKs and at Loot's picker.
  4. **BATTLE's hand on Battle Abilities and Data (done).** Battle Abilities' learned list (MENU2's
     `vs_battle_drawCursor`) was already walked by `tour_abilities`: claimed on 3, proof at its first type's list at
     rest. On Data (7) the hand is the container's prompts' (item 2); the Data screen's own file menu draws its hand
     by `DrawPrim` (as item 2).
- **Counts** (classic_parity's menus game, GL; 126 scenarios): `rename_grid` 283 frames, `rename_box` 380,
  `menu_cursor` 14,915 (BATTLE's hand now on 3, 7, 8 too), `ok_mark` 300 (Assemble's and Combine's OK +126),
  `discard_count` 176 (Loot's picker +54), `menu_row` 204,387; 0 mismatches, 0 skips, no waivers. 41 menus
  state-parity marks (8 new), all 120/120.
- **Bugs found and fixed on the way:** `rename_box`'s Lua field was first named `box`: `fill_menu` nils its keys
  list for every element, so text_frame's own `box` went nil and its draw failed at the opening (now `highlight`; a
  kind's field must not reuse another kind's top-level name). The grid's record is zeroed first (its words' tails
  were stack bytes, which state parity saw in the kept element: `pc_hud_fields` statics are in the snapshot's bss).
  A proof whose screen Triangle does not leave (the rename grid, Setup's two picks, MENU2's list) needs `after=`.
- **Verification:** `--arch 64` and `--battle` builds. Docker `make commit-check` (a copy of the tree; MENU8 touched):
  **All files match** (the formatter's rewrites of other files discarded). `run.py -j 1 --cpu-jobs 1` (nice 10)
  `classic_parity hud classic cutscene_player_smoke widescreen_smoke` plus every suite matching `manual ui_ menu
  prompts button options items equip status workshop container loot` (40 suites): **GL 39/40, then classic_parity
  again: pass; Vulkan 39/40, classic_parity again twice: the first stopped (rood.classic disabled), the second
  passed.** Each classic_parity failure was a draw over its CPU budget (4 ms) in the menus game, not a mismatch:
  2, 2 and 6 "budget exceeded" faults at the first call of an ordinary draw (menu_row, target_row, the stat pages:
  0 instructions run, so the time went before the draw's first instruction), after Setup's warp to the workshop;
  2 skips each time (or 5 faults in 10 s: the plugin disabled, the tour's REPL failing). Of seven classic_parity runs of this
  branch three had faults, four none (the last GL one too); main's code passed its one Vulkan run here with none.
  The cause is not found (the machine was shared; main's runs elsewhere were 40/40).
- **Left:** MENU7's file menu (Data's SAVE / LOAD and the save list after Storage): an immediate-draw capture, not the
  emit-only range; the rename grid reached from Assemble (a new weapon's name) is the same draw, claimed, untoured on
  that route; MENUF (not asked for).

2026-10-30, evening (branch `classic-lua-panels2`, from main `d91b54af`; no merge or publication):

- **Phase 3, the screens' own panels after Items piece 2's kinds: Storage's, Setup's and Loot's, done.** Each a
  `ROOD_PC` `PC_CLASSIC` emit-only range (no MENUx function has a Zig port: no port touched), drawn by rood.classic,
  claimed on its screens only (`PANEL_SCREENS`, hud_bridge.c):

  | Kind | Game routine | Screens | Frames (GL) |
  |---|---|---|---:|
  | `items_category` (widened) | MENUD's own copy of the icon loop (234.c `func_80103E24`: the one that calls `vs_mainMenu_renderTabNavigation`; the record's new `slot`, its dim icons in `unk4 - 12`) | Items, Loot, now Storage | 11,029 (whole game) |
  | `ok_mark` (new) | the pulsing OK: MENUC 168.c `_drawOk` (Assemble, Gems, Combine), MENUD's top menu (`func_801089BC`) | Setup, Storage | 174 |
  | `storage_buttons` (new) | `vs_menuD_exec`'s strip: two windows, Square / Triangle / Cross, STATUS / CANCEL / END | Storage | 1,801 |
  | `storage_counts` (new) | `func_80106464`'s `capacity / count` rows: the strip's (the bag's or the container's name, the categories, the transfer list) and the top menu's (`func_80106504`, both columns) | Storage | 1,979 |
  | `loot_capacities` (new) | MENUB 260.c `_displayInventoryCapacities`: GADGET BAG, the counts coloured by the row looked at, GET / ALL / END | Loot | 289 |
  | `discard_count` (new) | a misc item's discard amount (items1.h `_discardMenu`, MENUD's copy) | Items, Storage | 122 |

  0 mismatches, 0 skips, no waivers. Lua: look tables for each (places, words, colours, the buttons' texels:
  `look.buttons`), the calls recorded in the game's order and made last to first, the packed places by `d.xy` (the
  slide's borrow into y kept, CANCEL's carry too); no new primitive. The gate was shown to catch a change (Loot's
  count column one pixel left: 289 of 289 mismatched on `xy`). Tours: none new (tour_storage, tour_setup's Gems and
  tour_loot already pass every state drawn: the top menu, Remove and Store, the slides in and out; the loot list's
  leave row, an item, none). Proofs (default and fallback at rest, state parity 120/120): Setup's Gems, a weapon's
  gem slots (new, the OK), Storage's top menu (OK, the two columns), Storage's Store list (icons, strip, counts), the
  Loot list and its leave Yes / No (capacities); Items' and Storage's discard picker (the digits).
- **MENUD's item action menu** is drawn by the shared pieces (`vs_mainMenu_renderItemActionMenu`: rows 32..
  through `_renderActionRow`, the hand by `vs_mainMenu_renderCursor`: Lua already, menu_row / menu_cursor) and,
  for a misc item's Discard, the amount picker's digits: now **`discard_count`** (new kind), items1.h
  `_discardMenu` on Items and MENUD's copy on Storage (three digits in the text boxes' font, style 5, by
  `d.font_text`, split as the game splits them). items1.h's is in a `#pragma vsstring` region: the transformer
  rewrites the strings of the *preprocessed* text, so a `#ifdef` block or a macro over several lines there leaves a
  line marker whose file name it rewrites (`invalid #line` in the PS1 build, `'{' is not a valid filename` in the
  PC's); the hook is two one-line macros defined above the function (`PC_DISCARD_COUNT_BEGIN` / `_END`: the site
  called by hand; empty in the PS1 build). Tours: tour_items' misc Discard walks the picker (up by ones, tens,
  hundreds, down); tour_storage adds Store's misc tab, a row's Discard, the picker walked (Circle twice: picked,
  then its actions). Proofs at Items' and Storage's picker at rest. Docker commit-check: All files match.
- **Verification:** `--arch 64` and `--battle` builds. Docker `make commit-check` (a copy of the tree, MENUB / MENUC /
  MENUD touched): **All files match**; the formatter's re-indents of the new ranges discarded, its wraps of the new
  record lines kept. `run.py -j 1 --cpu-jobs 1` (nice 10) `classic_parity hud classic cutscene_player_smoke
  widescreen_smoke` plus every suite matching `manual ui_ menu prompts button options items equip status workshop
  container loot` (40 suites): **40/40 on GL and 40/40 on Vulkan**, first time; the same classic_parity counts on
  both (33 menus state-parity marks, all 120/120), run again after discard_count: the same.
- **Left:** Setup's Assemble / Combine OK (same kind; their two-picks states untoured), Loot's Organize inventory's
  discard picker (items1.h's, claimed on Items and Storage only: untoured on Loot), MENU8's rename grid from Setup, MENU7's save list after Storage. Every other draw of MENUB's and MENUD's own is now
  in a claimed range or a shared piece.

2026-10-30, afternoon (branch `classic-lua-panels`, from main `8257f69d`; no merge or publication):

- **Phase 3: Items piece 2's kinds widened to every screen that draws them: Status, Setup, Storage, Loot, done.**
  The claim is now per kind and screen (`PANEL_SCREENS` in hud_bridge.c, beside `MENU_TOURED_SCREENS`), each bit
  set once that screen's tour covered the panel. Two of the screens draw a kind through **their own copy** of the
  routine, not the shared one: MENUB's STATUS strip (260.c `_topLevelMenuTransition`, a copy of 16C.c's) and MENUD's
  gem slots (234.c `_gripNavigation`, a copy of items1.h's). Each copy got the same `ROOD_PC` `PC_CLASSIC` emit-only
  range and record as the original; the packets match exactly (0 mismatches), so both are claimed too.

  | Kind | Items (4) | Status (5) | Setup (13) | Storage (14) | Loot (12) |
  |---|---|---|---|---|---|
  | `items_category` | on | not drawn | not drawn | not drawn | on (items0.h, Organize inventory) |
  | `items_status_button` | on | not drawn | not drawn | not drawn | on (MENUB's copy) |
  | `equip_stats` | on | on | on | on | on (items1.h's status views) |
  | `dp_pp_bars` | on | on | on | on | on |
  | `gem_slots` | on | not drawn | not drawn | on (MENUD's copy) | on (items1.h) |
  | `stat_page` | on | on | on | on | on |

  Frames compared per screen (classic_parity's menus game; Setup's tour also counts the Items screen it passes
  through):

  | Screen | `items_category` | `items_status_button` | `equip_stats` | `dp_pp_bars` | `gem_slots` | `stat_page` |
  |---|---:|---:|---:|---:|---:|---:|
  | Status | | | 2,177 | 1,392 | | 1,838 |
  | Setup | | | 600 | 491 | | 699 |
  | Storage | | | 1,122 | 728 | 169 | 1,140 |
  | Loot | 3,085 | 3,071 | 2,364 | 1,537 | 376 | 2,382 |
  | The whole menus game | 7,798 | 5,327 | 8,806 | 5,821 | 931 | 8,638 |

  0 mismatches, 0 skips, no waivers. **Tours** (`classic_parity.py`): `tour_status`' equipment walk runs with
  Ashley's weapon broken (its and its blade's DP 0) and her shield's PP spent (`status_equipment_fixture`, put back
  after), and pages each detail's table with Square; `tour_setup` pages the table with Square after every list
  it walks (MENUC takes no Square); `tour_storage` opens the status view of every Store tab's first row (R1 / L1
  through the items, Square through the table, the view's hand walked, Cross out) with Items' fixture
  (`status_view_fixture`: broken / full blades, gem slots 0 / 3, loose gems); `tour_loot`'s Organize inventory
  opens every category's status view the same way (the fixture again). **Proofs** (default and fallback of every
  kind drawn at rest, state parity 120/120): Status's weapon detail, and again broken (the regression mark below);
  Setup's Assemble, the blades' list; Storage's Store status view and a grip's (the gem slots); Loot's Organize
  inventory, a weapon's and a grip's status view (`screen_proofs(after=...)`: the bag put back and the loot menu
  left, the mark taken again). 28 menus marks green.
- **Bug found and fixed (all claims, also on main's Items):** a claim suppressed the game's packets by walking its
  allocation (`pc_classic_suppress`: each tag's length to the next), but `vs_battle_renderStatBar`'s empty bar (fill
  0) is a 3-word tile in a 14-word allocation; the walk read the stale tpage word as a tag and stopped, so every
  packet allocated after it stayed live. With an empty DP bar (a broken weapon, any blade in Setup's lists) the
  game's own PP bar was drawn a second time, over Lua's PP word and digits (a pixel row, on the picture only: the
  packets the gate compares are equal; state parity caught it in VRAM at Setup's Assemble list). Fix
  (`pc_classic_suppress_linked`, classic_parity.c): after the allocation walk, `classic_finish` also suppresses the
  range's packets reached through the kind's slots' links. Regression mark: Status's weapon detail, broken (fails
  without the fix: first difference at vsync 2 in VRAM). Any claim whose range holds an empty stat bar before other packets shares the exposure (the HUD gauges at 0 MP or 0 HP: not checked; the fix covers them, `classic_finish` is theirs too).
- **Verification:** `--arch 64` and `--battle` builds. Docker `make commit-check` (a copy of the tree, MENUB /
  MENUD touched): **All files match**; the formatter's re-indents of the new ranges discarded (as before), its
  comment wrap in 234.c kept. `run.py -j 1 --cpu-jobs 1` (nice 10) `classic_parity hud classic
  cutscene_player_smoke widescreen_smoke` plus every suite matching `manual ui_ menu prompts button options items
  equip status workshop container loot` (40 suites): **40/40 on GL and 40/40 on Vulkan**, first time; the same
  classic_parity counts on both (115 scenarios; 28 menus state-parity marks, all 120/120).
- **Next panel: Setup's and Loot's own hand, done.** `vs_battle_drawCursor` (5BF94.c, BATTLE's: MENUC's Yes / No,
  MENUB's Yes / No and row menus, also MENU2, MENU7, MENU8 and the battle command list) draws exactly
  `vs_mainMenu_renderCursor`'s packets with colour 0 at x 180, so it is hooked as `menu_cursor` (a `ROOD_PC`
  `PC_CLASSIC` emit-only range; the record's new `battle` flag) and claimed on Setup and Loot only
  (`BATTLE_CURSOR_SCREENS`, hud_bridge.c); no new kind, no Lua change. menu_cursor frames: Setup's Disassemble /
  Rename / Repair +16 / +16 / +22, Loot's leave / Organize / Consolidate +3 / +24 / +17 (whole game 13,605); 0
  mismatches. Proofs: Setup's Repair Yes / No and the Loot list's leave Yes / No (the default and the fallback,
  state parity 120/120; the loot menu left after). Docker commit-check: All files match (the new range's re-indent
  discarded). The same gate again: **40/40 on GL and 40/40 on Vulkan**, the same classic_parity counts on both
  (30 menus state-parity marks, all 120/120); `--arch 64` and `--battle` builds.
- **Left:** `items_category`, `items_status_button` and `gem_slots` are not drawn on Status or Setup (nothing to
  claim); MENU8's rename grid from Setup and MENU7's save list after Storage are still untoured; BATTLE's hand on
  Battle Abilities, Data and the rename grid stays the game's until their tours walk it. The screen-specific drawing
  listed below stays the game's; next by the plan's order: Storage's `renderTabNavigation` counts and MENUD's item
  action menu, then Setup's OK mark and Loot's button strip and counts.
2026-10-30 (branch `classic-lua-screens`, from main `3fc0edbd`; no merge or publication):

- **Phase 3, the remaining MAINMENU screens: the shared frame's kinds on Score, the Map, Data, Setup, Storage
  and Loot, done** (`bd4d4efb`, `0ffac3da`). `MENU_TOURED_SCREENS` (hud_bridge.c) is now `0x77FE`: 1..10 and
  12 Loot (MENUB), 13 Setup (MENUC), 14 Storage (MENUD): every MAINMENU screen but 15 (MENUF: the time attack
  and congratulations screens, direct entries; not asked for, not toured, not claimed; 11 is unused). No capture
  gap and no crash: every screen's rows, hand, title, tabs and background are the shared ones, 0 mismatches from
  the first run. Tours (`classic_parity.py`, `tour_remaining` in the menus game after Status; the original
  menus, the cursor memory off; rows found by id and text through `rood.game.menu()`, MENU states read from the
  overlays' statics by name, `ovl()`):
  - `tour_score` (MENU9; card_fixtures' "maxed" score, so every list pages): opened (SCORE shown: menuFlags
    bit 8); the top menu walked round; each section (Title, Gazette, Record Time, Encyclopaedia) by its row, its
    list walked down past its end and up past its top; a foe's page (its model, Right / Left, Select twice), out;
    closed from a section; reopened, closed from the top.
  - `tour_map` (MENU5; ui_map_smoke's fixture: areas 1..13 visited, every countable room seen): opened; the area
    list walked past both ends (it pages), left; four rows down, another area's map loaded; R1 / L1 rooms, L2 +
    Right (the turn), Select twice (the help); Cross to the top level, closed; reopened, the list, Triangle.
  - `tour_data` (MENU7, nothing saved or loaded): its rows walked; SAVE and LOAD each: the two cards, card 1's
    files walked down past the last and back up (the pages), out with Cross; Cross to the top level, closed;
    reopened, Triangle. (Circle on a file is not pressed: Save writes at once on an empty slot, Load loads.)
  - `tour_setup` (MENUC, Items > Setup; warped to zone 47 room 0, a workshop, with ui_workshop_smoke's
    `prepare` bag: gems, gem slots, damage): its six commands walked; Assemble: the blade's and the grip's pick
    lists; Gems: Weapons, a weapon's gem slots, a slot's gem list, Shields' list; Disassemble and Rename: a
    weapon's Yes / No; Repair's Yes / No; Combine: its categories, the blades' first pick list; each left with
    Cross (the tour checks MENUC stays in the command after every step).
  - `tour_storage` (MENUD, through RPC container as the field opens it; the card's own container, empty):
    MENU7's Yes / No (rows 30 / 31, shared); MENUD's top menu walked; Remove and Store: every tab round with R1
    (each list walked) and L1; a Store row's actions walked and left; Cross at the top (to MENU7's save).
  - `tour_loot` (MENUB, through RPC loot as after a battle; ui_loot_smoke's lists from the bag's own records): a
    weapon, an armor and three misc items: the list past both ends, leave's Yes / No left on No, a row taken,
    Square (the rest); the misc bag full (ui_loot_smoke's fill_misc) and three misc items: a row that does not
    fit, its menu, Organize inventory (MENUB's Items: every category round with R1 and L1, a list walked),
    back, Consolidate's Yes / No left on No, leave: Yes.
  - Setup, Storage and Loot mark the save room first and rewind to it after (the classic counters survive a
    rewind): nothing is saved, the bag and the room are the save room's again for the proofs after.

  | Screen (scenarios) | `menu_row` | `menu_cursor` | `menu_command` | `menu_tabs` | `menu_background` |
  |---|---:|---:|---:|---:|---:|
  | Score (7) | 3,765 | 933 | 1,295 | 0 | 1,309 |
  | Map (4) | 2,934 | 195 | 446 | 0 | 446 |
  | Data (5) | 984 | 44 | 704 | 0 | 706 |
  | Setup (8) | 11,464 | 849 | 1,826 | 15 | 1,827 |
  | Storage (5) | 7,824 | 390 | 1,429 | 1,196 | 1,436 |
  | Loot (5) | 6,236 | 144 | 378 | 364 | 386 |
  | The whole menus game (107) | 114,233 | 7,259 | 14,882 | 4,452 | 12,770 |

  0 mismatches, 0 skips, no packet waivers. The default and the fallback (setting off, a raising draw, the plugin
  off) of every kind drawn at rest, and state parity 120/120 vsyncs, at eight new marks (`remaining_proofs`):
  Score's top menu, the Map, the Map's area list, Data's rows, Setup's commands (in the workshop), Storage's top
  menu, Storage's Store list (the tab arrows) and the Loot list (rows and hand only: Loot draws no title or
  background); all 18 menus marks green.
- **Verification:** 64-bit and 32-bit (`--battle`) builds; no `src/` change (no commit-check needed).
  `run.py -j 1 --cpu-jobs 1` (nice 10) `classic_parity hud classic cutscene_player_smoke widescreen_smoke` plus
  every suite matching `manual ui_ menu prompts button options items status magic arts abilit map data score
  loot setup storage container workshop` (42 suites, scipy present again: ui_text_smoke, button_prompts_smoke,
  button_roles_symbols_smoke and interp_menu_2d_smoke ran): **42/42 on GL and 42/42 on Vulkan**, first time
  (ui_system_smoke too). The same classic_parity counts on both.
- **Left as the game's own drawing (later work):** Score: MENU9's pages (the titles, gazette, record times and
  the encyclopaedia's lists and descriptions, `vs_battle_renderTextRaw`), the rank (text box 0), the foe viewer,
  the button strip (`vs_mainmenu_renderButton`). Map: MENU5's 3D map, compass, icons, door and connecting area
  names (`_renderScreen` and friends, raw text), its controls help (`_drawUIControls`, the button strip), the room
  name (text box 0). Data: ~~MENU7's whole file menu (`_drawFileMenu`: the card and file panels, its own Yes / No
  elements, `vs_battle_drawCursor`, the play time, HP / MP, the progress animations)~~ (Lua since the 2026-10-30 late-night entry: `file_menu`,
  `file_element`). Setup: ~~the stat panels~~
  (Lua since the afternoon entry above), ~~MENUC's own cursor
  (`vs_battle_drawCursor`)~~ (Lua, the afternoon entry), the OK mark; the information box is text box 7 (Lua already). Storage: MENUD's item
  action menu, ~~status view and stat panels~~ (Lua since the afternoon entry), `renderTabNavigation`'s counts, the button strips, raw text; MENU7's
  save list after it. Loot: ~~MENUB's own cursor (`vs_battle_drawCursor`)~~ (Lua, the afternoon entry), the button strip and its background,
  the counts (`renderIntColor`), raw text. Not toured: MENU7's game over screen (currentMenu 7 at a game over),
  MENU8's rename grid from Setup (currentMenu 8, the Option screen's overlay), MENUF.
2026-10-30 (branch `classic-lua-items2`, from main `3fc0edbd`; no merge or publication):

- **Phase 3, screen 1 (Items, MENU3), piece 2: the Items screen's own panels, done (Ashley's 3D stays the game's).**
  Six kinds, each a `ROOD_PC`-only `PC_CLASSIC` emit-only range (every state step stays the game's; the PS1 code
  unchanged), a copy of what its draw reads, a rood.classic look drawn with the game's own leaf routines, a
  `hud_classic_*` setting. All six are claimed on Items only (`currentMenu & 0x3F == 4`, in `pc_hud_menu`): items0.h
  is MENUB's too and the MAINMENU panels draw on Status as well, both untoured, so the game draws them there.

  | Element / HUD kind | Hook | Slots | Frames | Scenarios |
  |---|---|---|---:|---:|
  | Category icons / `items_category` | items0.h `_renderItemCategory`'s loop (`on_row`: the icon handed to row 31, by the loop's own test) | unk4, unk8 | 4,672 | 81 |
  | STATUS strip / `items_status_button` | 16C.c (window, Square button, word) | unk4 - 12, unk8 | 2,233 | 81 |
  | Stats panel / `equip_stats` | C48.c `vs_mainMenu_renderEquipStats`, after its slide | unk4 - 24 | 2,543 | 81 |
  | DP / PP bars / `dp_pp_bars` | C48.c `vs_mainMenu_renderDpPpBars`, after its step | unk4 - 12 | 1,673 | 81 |
  | Gem slots / `gem_slots` | items1.h `_gripStatusView` (gem icon, `X     n`) | unk8 + 4, unk8 | 386 | 81 |
  | Class / affinity / type table / `stat_page` | C48.c `_renderStatPage` as `drawClassAffinityType` calls it | unk4 - 12, - 16 | 2,579 | 81 |

  0 mismatches, 0 skips, no waivers (each green on its first run). The equip menus' stat panels are these same
  kinds (MENU3's Equip on the Manage page calls the same C48.c routines), toured by `tour_items`' Equip walk.
  **Defaults on:** `hud_classic_items_category`, `hud_classic_items_status_button`, `hud_classic_equip_stats`,
  `hud_classic_dp_pp_bars`, `hud_classic_gem_slots`, `hud_classic_stat_page`.
- **Draw primitives added** (Extensions.md): `d.menu_int` (`vs_mainMenu_renderIntColor`, returns its pen),
  `d.stat_bar` (`vs_battle_renderStatBar` in any slot), `d.button_background` (the button window's shaded lines,
  a copy: the game's links at unk4 - 12 always), `d.packet` (one primitive of the game's words: the table's lines,
  tiles and draw modes, as `vs_battle_addTile` / `insertTpage` link them), `d.font_text` / `d.font_width` (the
  text boxes' font after `vs_battle_setFontStyle`: the table's header); `d.layer("unk8", n)`; `d.value`'s font 2
  (the DP / PP slash). Gotcha kept in the look: the game's `temp + vs_getXY(-7, -2)` is 7 left and **1** up (the
  packed borrow), done with `d.xy`.
- **Tours** (`classic_parity.py`): `tour_status_views` after `tour_items` (cursor memory off): a fixture of what
  play reaches (`status_view_fixture`: two blades broken (DP 0, PP full) and full (PP 0), a grip with no gem slot
  and one with three, two loose gems in an empty gem bag; the bag and its indices restored after); each category's
  status view opened (Square on its first row), every item paged with R1 and back with L1, Square on each (the
  table's next page: class, affinity, type), the view's own hand walked down, across and up, left with Cross
  (the panels sliding out); misc has no status view. Proofs: the default and the fallback (setting off, a
  raising draw, the plugin off) of every kind drawn at rest, and state parity 120/120 vsyncs, at the Items list
  (icons, strip), a weapon's status view (icons, stats, bars, table) and a grip's (icons, stats, gem slots, table).
- **Verification:** `--arch 64` and `--battle` builds. Docker `make commit-check`: **All files match** (run in a
  copy of the tree; the formatter's re-indents of the new ranges discarded, as for 58EC.c). The gate (`run.py -j
  1`, nice 10: classic_parity, hud, classic, cutscene_player_smoke, widescreen_smoke and every manual, ui_, menu,
  prompts, button, options, items, equip suite): **40/40 on GL, 40/40 on Vulkan**; classic_parity rerun on GL
  with the final tour (the fixture): PASS.
- **Not a bug, a trap:** editing `pc/plugins/rood.classic/main.lua` while a test runs hot-reloads it into the
  running game; with a kind the binary does not know yet the plugin fails to load and every later kind fails
  (emote "0 frames compared" in one run here). Edit the plugin only between runs.
- **Left on Items:** Ashley (`_renderAshley.h`, the 3D: the game's by decision). The same MAINMENU panels on
  Status (MENU4: its equipment detail's stats, the table) stay the game's until Status's tour covers them (widen
  the `currentMenu == 4` test in `pc_hud_menu` then); MENUB's (`items0.h`'s icons in its own screen) likewise.

2026-10-28, night (branch `classic-lua-skills`, from main `95adc53e`; no merge or publication):

- **Phase 3, screens 2-4 (Magic, Break Arts, Battle Abilities): the shared frame's kinds on, done.**
  `MENU_TOURED_SCREENS` now 1 Magic (MENU0), 2 Break Arts (MENU1), 3 Battle Abilities (MENU2), 4 Items, 8 the
  Option screen. No capture change was needed: their rows, hand and title are the shared ones (58EC.c), and the
  screens' own drawing (below) lands outside the rows' packet ranges (MENU2's button icons go to
  `vs_scratch.unk8 + 8`, MENU0's LV picker to `unk4 - 4`): 0 mismatches from the first run.
- **Tours** (`classic_parity.py tour_magic`, `tour_arts`, `tour_abilities`, in the menus game after Items; the
  original menus; nothing cast; rows found by text through `rood.game.menu()` and the shared list's cursor, never
  fixed ids). The save room's card has no spells or arts, so each tour first sets up what play learns (no state
  play cannot reach): every spell (every grimoire read) with the Magic command flag, and Teleport's Circles of Magic
  (the save room's and four others); arts of three categories (the equipped one's first three, all four of
  another, one of a third; `artsLearned` to match); every chain and defense ability. Magic: opened; each school
  (Warlock, Shaman, Sorcerer, Enchanter, Teleportation) in, its list walked past both ends (18 rows scroll), Warlock's
  Explosion into the LV picker (levels right past the end and back), Cross out; closed. Break Arts: the equipped
  category's arts walked, View Arts, the ten categories walked (they scroll), three categories' arts in and View
  Arts out again, Cross on the categories, closed. Battle Abilities: Chain then Defense (found by text), the list
  walked (14 learned, 8 shown: it scrolls), a row put on Circle, Triangle, Square (moves) and taken off again,
  Cross; closed; the mapping restored. Counts (the menus game: Option, Items and these together, GL):

  | Element / HUD kind | Frames | Scenarios |
  |---|---:|---:|
  | Menu rows / `menu_row` | 51,842 | 49 |
  | The hand / `menu_cursor` | 2,604 | 49 |
  | The command title / `menu_command` | 5,475 | 49 |
  | The tab arrows / `menu_tabs` | 1,807 (Items only) | 49 |
  | The background / `menu_background` | 1,977 (Items only) | 49 |
  | Our pages' values / `option_value` | 2,791 (Option only) | 49 |

  0 mismatches, 0 skips, no packet waivers. The default and the fallback (setting off, a raising draw, the plugin
  off) of rows, hand and title on each of the three screens; state parity 120/120 vsyncs at each one's first list
  at rest (Magic's schools, Break Arts' list, Battle Abilities' Chain / Defense). The tab arrows and the background
  are not drawn on these screens.
- **A bug found by the Break Arts tour, fixed (`c6b91f1a`):** with a category's four arts learned, Circle on View
  Arts closed the menu (0x3F) to "cast" an art. MENU1 `_weaponArtsMenu`'s init writes View Arts' `0xFFFF` to
  `actionIds[rowCount]`, rowCount 4: one past `actionIds[4]`. On the PS1 that slot keeps the value until the
  selection; on the PC it is another static, so the selection read a stale action id. Game code, with the kinds on
  or off. Under `ROOD_PC` the array has a fifth slot. commit-check: All files match.
- **Verification:** 64-bit and 32-bit (`--battle`) builds; Docker `make commit-check`: **All files match** (after
  the 30.c change; formatter rewrites of 5BF94.c and 58EC.c discarded). `run.py classic_parity hud classic
  cutscene_player_smoke widescreen_smoke` plus every `manual`, `ui_`, `menu`, `prompts`, `button`, `options`,
  `items`, `magic`, `arts`, `abilit` suite: **41/41 on GL**. Vulkan: stopped at time with 27/41 passed, 0 failed
  (classic_parity among them); the remaining 14 not run.
- **Left (the game's own drawing still):** Magic: the LV picker's level boxes and arrows (84.c `levelledSpell`, into
  `unk4 - 4`), the MP cost box (`vs_mainMenu_displayActionCost`); Break Arts: the HP cost and the kills to the next
  art (`_setHPCost`, `_setNextArtUnlockPointsRemaining`); Battle Abilities: its list's hand (MENU2 `_abilityMenu` draws
  it with BATTLE's `vs_battle_drawCursor`, not the shared hand: `menu_cursor` there is the Chain / Defense rows'), the button icons on mapped rows
  (`vs_mainmenu_renderButton`), the RISK cost, the kills to the next ability; MENU2's "learned" screen
  (143C.c `vs_menu2_actionUnlock`: reached by kills in battle, not from the menu, so not toured). The information
  box is text box 7 (already Lua).
- **2026-10-29, main merged in (`4a98c503`: the Quick Manual and Status tours, wave 6's ports, photo mode, the
  GCC 16 build fixes); re-gated on the new OS (GCC 16.2), no fixes needed.** `MENU_TOURED_SCREENS` is the union
  1, 2, 3, 4, 5, 8, 10. The menus game now tours Option, Items, Magic, Break Arts, Battle Abilities, the Quick
  Manual and Status in one run with no interaction between the tours: `menu_row` 81,026 frames, `menu_cursor`
  4,704, `menu_command` 8,804, `menu_tabs` 2,877, `menu_background` 6,660, `option_value` 2,791; 0 mismatches,
  0 skips, no waivers on the menu kinds; state parity 120/120 vsyncs at all ten marks (Option list, Display,
  Items, Magic's schools, Break Arts, Battle Abilities, the Quick Manual, Status, its equipment list and a part's
  stats). The same counts on GL and on Vulkan. Builds: `--arch 64` and `--battle`. Docker `make commit-check`:
  **All files match** (formatter rewrites of 5BF94.c and 58EC.c discarded). The gate (`run.py -j 1`, nice 10):
  classic_parity plus the 36 other matching suites, **37/37 on GL and 37/37 on Vulkan**. Not run (scipy is not
  installed on the new system; each imports it at the top): `ui_text_smoke`, `button_prompts_smoke`,
  `button_roles_symbols_smoke`, `interp_menu_2d_smoke`.
2026-10-28, night (branch `classic-lua-manual`, from main `95adc53e`; no merge or publication):

- **Phase 3, the Quick Manual (MENUE, currentMenu 10) and Status (MENU4, currentMenu 5): the shared frame's kinds,
  done.** `MENU_TOURED_SCREENS` is now 4, 5, 8, 10. No capture gap showed: neither screen needed a change to the
  capture or to rood.classic, only the claim. Both tours run in `run_menus` after Items (the original menus, the
  cursor memory off):
  - `tour_manual`: QUICK MANUAL opened (menuFlags bit 9 set); the topic list walked 15 down and 16 up (the window
    scrolls, both wraps); every one of the 14 topics opened by the list's cursor (`rood.game.menu().cursor`), its
    lines stepped down and up, Up at the top (to the end) and Down at the end (back), every page turned with
    Square + Down, Start, back to the list (Circle; Cross for the last); Cross to the top level; reopened, a page,
    Triangle. MENUE's `_showMenuState` is read as ui_manual_smoke does.
  - `tour_status`: Status opened; the body's parts and stat descriptions walked every way past their ends; R1 / L1
    through the room's actors (Ashley, actor 3) and back to Ashley (MENU4's `_selectedActor`); two parts' equipment
    lists (Circle) walked past both ends, each of the 8 pieces' detail opened (Circle; its rows from 32) and walked;
    a part's stats (Square): rows past both ends, the parts round with R1 / L1; closed. The hand is moved by MENU4's
    own neighbour table (D_801080FC, `status_to`); a press while the screen glides is retried only if the state did
    not move (`status_into`). (A first try with R1 / L1 left on actor 3 refused every detail and Square: the tour
    now checks it is back on Ashley.)

  | Element / HUD kind | Quick Manual frames | Status frames |
  |---|---:|---:|
  | Menu rows / `menu_row` | 8,216 | 20,954 |
  | The hand / `menu_cursor` | 255 | 1,845 |
  | The command title / `menu_command` | 1,861 | 1,466 |
  | The tab arrows / `menu_tabs` | 0 (none on the screen) | 1,070 |
  | The background / `menu_background` | 1,855 | 2,826 |

  (17 Quick Manual and 7 Status scenarios.) 0 mismatches, 0 skips, no packet waivers. The default and the fallback
  (setting off, a raising draw, the plugin off) for every kind each screen draws at rest, and state parity 120/120
  vsyncs (`screen_proofs`): the Quick Manual list at rest (rows, hand, title, background); Status at rest (rows,
  title, background), its equipment list (rows, hand, title, background) and a part's stats (the same). The hand
  on Status's main screen is drawn while it moves (167 of 173 frames of the walk) but not counted at rest.
- **Verification:** 64-bit build (no `src/` change: no commit-check needed). The whole menus run (Option screen,
  Items, Quick Manual, Status): menu_row 62,294, menu_cursor 3,588, menu_command 6,588, menu_tabs 2,877,
  menu_background 6,658, option_value 2,791 frames, 54 scenarios, all green; classic_parity PASS (0 failures).
  `run.py -j 1 classic_parity hud classic cutscene_player_smoke widescreen_smoke manual ui_ menu prompts button
  options items status`: 40/40 on GL, 40/40 on Vulkan (ui_system_smoke passed first time on both).
- **Left as the game's own drawing:** the Quick Manual's page: its text (text box 0, already the dialogue style),
  the TURN PAGE / MENU strip (`vs_mainmenu_renderButton`, `_drawControlsBg`), PAGE n / m, the page background and
  its fades (`_setPageBg`, `_fadeMenuUpper` / `Lower`), the page arrows, the illustrations (`_drawContent`,
  `_drawSprite`). Status: `_drawScreen` (the body figure and its parts' condition, `_renderLimbUi`, the basic stats
  and bars, the status icons, `_drawMenuBackground`), the STATUS button strip, `vs_mainMenu_renderEquipStats`, the
  class / affinity table, the equipment detail's panels; Ashley in 3D stays the game's.

2026-10-28, late (branch `quick-manual-crash`, from main `1a9b4875`; no merge or publication):

- **The Quick Manual crash, fixed (root cause).** Reproduced with currentMenu 10 added to `MENU_TOURED_SCREENS`
  (ui_text_smoke: signal 11 at battle frame 286) and traced with a log of every `pc_text_box_line` query: the
  line asked for was never a Lua row (their glyphs are table 0, the menu style, which never takes the bubble path)
  but the manual page's own text (x 13, y 55, table-1 glyphs: the dialogue style, so `bubble_segments`), which
  text box 0 prints. MENUE's `_copyNextPageText` hands `vs_battle_setTextBox(0, buffer)` a **local** 1024-byte
  buffer: box 0's `string` points into a dead stack frame (0x63fff920 in the log). The game reprints the page from
  it in `func_800CB83C` before anything deeper reuses that stack (as on the PS1); `text_lines.c` walks it later,
  inside `DrawOTag`'s text flush, where the frames below have overwritten it: garbage, an inline `0xFF` string
  through a wild pointer, the crash. Lua's drawing only changed what the dead frame held, so it crashed with the
  kinds on and happened not to without. Also walked every query: box 7, closed (state 0) with a stale `string`.
- **Fix:** (1) `src/MENU/MENUE.PRG/494.c`: under `ROOD_PC` the page buffer is `static` (the box's pointer stays
  valid until the next page); (2) `text_lines.c`: `pc_text_box_line` walks only boxes the game prints this frame
  (`printed()`: `func_800CB83C`'s own test: state open and fully grown (`state == unk22 + 1`), not held (bit 31),
  not kind 3 or 6), so a closed box's stale string is never read. `MENU_TOURED_SCREENS` stays 4 and 8 (no Quick
  Manual tour yet); with 10 added (not committed) ui_text_smoke and button_roles_symbols_smoke pass on GL and
  Vulkan.
- **Verification:** 64-bit build; Docker `make commit-check`: **All files match** (formatter rewrites of 5BF94.c
  and 58EC.c discarded). Gate results in the commit message / hand-back.
- **Left:** a `classic_parity` tour of the Quick Manual, then 10 in `MENU_TOURED_SCREENS`.

2026-10-28, evening (branch `classic-lua-items`, from main `e1c639bc`; no merge or publication):

- **Phase 3, screen 1 (Items, MENU3), piece 1: the shared frame's kinds on Items, done.** `pc_hud_menu`
  (hud_bridge.c) now claims on a set of toured screens, `MENU_TOURED_SCREENS` (`currentMenu & 0x3F`, menu_model.c's
  names): 4 Items and 8 the Option screen. (Items is currentMenu 4, not 3: the overlay is MENU3, the screen index the
  top level's command + 1.) A row's capture now also walks `vs_scratch.unk8 + 4`, where
  `vs_mainMenu_renderMenuRowIcon` links the row icon: the Option screen has no row icons, so its tour never needed
  it; Items' first tour showed 15,946 mismatched frames (the game's capture lacked the icon the Lua copy, walking its
  whole shadow, had), 0 after. The Lua draw was right all along: suppression is by packet range, not by slot.
- **Tour** (`classic_parity.py tour_items`, in the menus game after the Option tour; the original menus, the cursor
  memory off, `introState` lifted to 3 for the tour so Manage's Equip is selectable, then restored): Items opened;
  Equip, its parts walked, the weapon's list in and out; each category in turn with R1 (the tab arrows): its list
  walked down past its end and back, the row's status view (Square) in and out where the category has one, a row
  picked up, its actions walked, Discard's Yes / No opened (found by the action's text through `rood.game.menu()`) and
  left on No; an empty category skipped (Cross would leave); back round with L1; closed. Counts (the menus game,
  Option screen and Items together):

  | Element / HUD kind | Frames | Scenarios |
  |---|---:|---:|
  | Menu rows / `menu_row` | 33,124 | 30 |
  | The hand / `menu_cursor` | 1,488 | 30 |
  | The command title / `menu_command` | 3,261 | 30 |
  | The tab arrows / `menu_tabs` | 1,807 | 30 |
  | The background / `menu_background` | 1,977 | 30 |
  | Our pages' values / `option_value` | 2,791 | 30 |

  0 mismatches, 0 skips, no packet waivers. The default and the fallback (setting off, a raising draw, the plugin off)
  tested for every frame kind in Items; state parity 120/120 vsyncs at the Items list at rest (a category's list: the
  arrows bobbing, the background up), and the Option screen's two proofs as before. **Defaults on:**
  `hud_classic_menu_tabs`, `hud_classic_menu_background` (the rows, hand and title already were); the kinds draw by
  default in Items and the Option screen, every other screen keeps the game's drawing.
- **A crash found by the tour, fixed (`7b75d81c`):** Square on a grip in Items segfaulted on the PC with or without
  Lua: `_gripStatusView` (items1.h) writes the gem-slot count into a string literal (the decomp's "BUG: rodata
  .write"), read-only on the host. Under `ROOD_PC` the buffer is a writable array (as MENUD's `_gripNavigation`
  already does); commit-check All files match. It is not the Quick Manual crash: that one needed Lua's kinds on
  (phase 2's fix), and this one is game code with the kinds off too.
- **The Quick Manual crash, reproduced and located (not fixed):** with the Quick Manual (currentMenu 10) added to
  the toured set for an experiment (not committed), ui_text_smoke's screens part crashed again on Vulkan at battle
  frame 282 and button_roles_symbols_smoke failed its Quick Manual page, as on main after phase 2. The stack:
  `DrawOTag` → `gpu_hw.c` text_run_flush → `ui_text.c` `pc_ui_text_layout_run` → `bubble_segments` (ui_text.c:1768) →
  `pc_text_box_line` → `text_lines.c` `walk` (line 36, `c = *str++`). So a run of menu glyphs went down the talk
  bubbles' whole-line path (`ui_text_bubble_align`: a run's first segment asks the text boxes for the line it
  starts), and `pc_text_box_line` walked a text box whose `string` (or an inline `0xFF` string) is stale in the
  Quick Manual: it walks every box with a non-null string, live or not. Two things to settle in that screen's tour:
  why the Lua-drawn rows' glyphs reach the bubble path there (the game's own rows do not), and which box's string
  is dangling (walk only boxes the game draws this frame). Items' rows do not take that path (the Items tour and
  every gate suite are clean with the kinds on). What else was checked to differ on Items: its rows are the shared
  rows (text inline in the record, `subText` a guest pointer kept valid by the live record, the scrollable rows'
  statics keyed on the frame buffer, so a second emit in the same frame steps nothing), row icons at `unk8 + 4`
  (now walked), materials and outset icons at `unk8`, no `vs_scratch` redirection in MENU3 or MENUE.
- **Verification:** 64-bit and 32-bit (`--battle`) builds; Docker `make commit-check`: **All files match** (after
  the items1.h change; formatter-only rewrites of 5BF94.c and 58EC.c discarded, build/ restored from main's copy).
  `run.py classic_parity hud classic cutscene_player_smoke widescreen_smoke` plus every `manual`, `ui_`, `menu`,
  `prompts`, `button`, `options`, `items`, `equip` suite: 40/40 on GL; 39/40 on Vulkan, the one failure
  ui_system_smoke's settings-save debounce timing (0.99 s against 1 s, unrelated to menus), 1/1 on its rerun.
  classic_parity in the gate now checks `menu_tabs` and `menu_background` too (its default kinds).
- **Left for Items:** the screen's own drawing (still the game's): the status views' panels
  (`_itemStatusDispatcher`'s weapon, blade, grip, shield, armor, gem, misc views: `vs_mainMenu_renderEquipStats`,
  `vs_mainMenu_renderDpPpBars`, the class / affinity table, the gem-slot count), the category icons
  (`_renderItemCategory`), the STATUS button strip, Ashley (`_renderAshley.h`; the 3D stays the game's), the equip
  menus' stat panels; the information box is text box 7 (already Lua). Each needs `PC_CLASSIC` emit-only ranges in
  MAINMENU / MENU3 sources, a model, look tables and the same gates. The list itself (rows, hand, scroll arrows,
  counts, icons, materials) and Discard's Yes / No are the shared rows: done with piece 1.

2026-10-28, afternoon (branch `classic-lua-phase2`, from main `dbbeb2aa`; no merge or publication):

- **Phase 2 step 1, done (`106e331e`):** `rood.game.menu()` (`pc/src/classic/menu_model.c`, the
  Lua side in `lua_api_game.cpp`): one locked table per vsync, copies only: the live rows of
  `vs_battle_menuItems` (every field, `text` decoded, `glyphs` the codes the game set, `sub`),
  `cursor` (58EC.c's list statics through a `ROOD_PC` accessor, `pc_classic_mainmenu_read`, and the
  sub-menu's cursor memory), `command`, `fade`, `info` (text box 7), `loaded`. `menu_model_smoke`
  (null renderer) reads it against RPC `options` in the Option screen: the list's cursor, Display's
  rows and help, Scaling's flyout; `vs_main_settings` and the card unchanged.
- **Step 2, done (`01a87351`):** `ROOD_PC`-only `PC_CLASSIC` ranges in 58EC.c, emit-only (every state
  step stays the game's): `_renderMenuItems`' per-row emit (split out under `ROOD_PC` as
  `_renderMenuItem`, after the row's slide is stepped; the row copied before the emit steps its
  gradient), `vs_mainMenu_renderCursor`, `_renderMenuCommand`'s sprite, the tab arrows, the
  background. The split needed no restructure of the logic: the slide steps already precede the emit
  in each iteration, so the emit is wrapped and nothing moves (the oracle state proof below is the
  check). `vs_mainMenu_renderItemsList` draws only through the hand (its rows are the shared rows);
  the information box is text box 7 (step 6's `text_frame` / `box_text`). HUD kinds `menu_background`,
  `menu_command`, `menu_row`, `menu_cursor`, `menu_tabs` (classic-only, never hidden with the HUD);
  `d.menu_row` (the game's emit on a copy of the row, `pc_mainmenu_render_row`; in the parity mode
  with `vs_scratch.unk8` pointed at a shadow); `d.sprite` gains `abr`. The HUD pool is 96.
- **Steps 3 and 4, in part (`71f3df2c`):** rood.classic draws the frame: `look.menu_row`
  (`d.menu_row`), `look.menu_cursor`, `look.menu_command` (`d.sprite` from the game's tables), and
  `menu_tabs`, `menu_background` (written, not toured: their settings stay off). All placed with
  anchor `game` (16:9 as today); `place.dx`, `dy` move a piece. The tour (`classic_parity.py
  run_menus`, its own game from the save room, the original menus): the Option list walked through
  every entry and back, the nine rows' flyouts, each of our pages to its end (scroll arrows),
  Display's Scaling flyout, Fullscreen mode's "Keep these settings?" (KEEP / REVERT / Cross; the test
  modes), the menu closed. Counts:

  | Element / HUD kind | Commit | Frames | Scenarios |
  |---|---|---:|---:|
  | Menu rows (the game's and our pages' windows and labels, the flyouts) / `menu_row` | `71f3df2c` | 10,700 | 18 |
  | The pointing hand / `menu_cursor` | `71f3df2c` | 492 | 18 |
  | The command title (OPTIONS) / `menu_command` | `71f3df2c` | 1,507 | 18 |
  | Our pages' value column (ON / OFF, choices, sliders, bindings, links, notes) / `option_value` | `51e3b9ac` | 2,791 | 18 |

  0 mismatches, 0 skips, no packet waivers; the fallback (setting off, a raising draw, the plugin
  off) tested for each; state parity 120/120 vsyncs at the list at rest and as Display's rows come
  in (masked too: `classic_options.c`'s `help.since`, the host's clock when a help box was set).
  Defaults on: `hud_classic_menu_row`, `hud_classic_menu_cursor`, `hud_classic_menu_command`,
  `hud_classic_option_value`.
- **Our pages' value column (`51e3b9ac`):** `classic_options.c`'s draw_values records each visible
  row's value as it draws it (kind, enabled, focused, index, the column and the window's right end,
  colours, gap, the gauge's colour, width and fill, the page model's texts) around a site of its own
  (emit-only), kind `option_value`; rood.classic's `look.option_value` draws it with the page's own
  calls (`d.option_text`: encoded and cut as the page does, returning the pen; `d.option_width`;
  `d.option_bar`: its gauge). `classic_options.c` keeps the input and navigation and stays the
  fallback renderer. The default and the fallback tested on Display; state parity there.
- **Verification:** 64-bit and 32-bit (`--battle`) builds; Docker `make commit-check`: **All files
  match** after each src change (formatter-only changes to unrelated code discarded; the game code
  inside the new ranges keeps its indentation). `run.py classic_parity hud classic
  cutscene_player_smoke widescreen_smoke options menu` 16/16 on GL and 16/16 on Vulkan on the
  final tree (19/19 on GL with the ctest suites before the value column) (with
  `menu_model_smoke`, `options_classic_smoke`, `options_confirm_smoke`, `save_menu_smoke`,
  `ui_menus_smoke`, `camera_stick_menus_smoke`, `menu0_levelled_smoke`, `interp_menu_2d_smoke`; every
  earlier kind green with its counts); probes `classic_parity_probe`, `lua_probe`,
  `options_nav_probe`, `option_pages_probe`, `lua_menu_probe` 5/5.
- **Fix after the merge (main's quick verify on Vulkan: ui_text_smoke crashed in the Quick Manual,
  button_roles_symbols_smoke's Quick Manual page with the L2 shortcut's buttons failed):** the frame's
  kinds were claimed on every MAINMENU screen, the Option screen being the only one toured. They are
  now restricted to it (`vs_battle_menuState.currentMenu & 0x3F == 8`, hud_bridge.c `pc_hud_menu`):
  every other screen keeps the game's own drawing, no element, until its own tour is green (phase 3).
  The cause on the Quick Manual was not isolated (no stack in the log); it goes with that screen's
  tour. The gate now also runs every `manual`, `ui_`, `menu`, `prompts`, `button` suite on GL and
  Vulkan: 40/40 on each (with ui_text_smoke, button_roles_symbols_smoke,
  ui_manual_smoke, button_prompts_smoke and every ui_* suite). menu_row now compares 10,188 frames and menu_command 1,450 (the
  menu's close, once it has left MENU8, is the game's own); menu_cursor 492 and option_value 2,791 as before.
- **Done-when status:** the Option screen (the game's rows, the flyouts, the hand, the title, the
  information box, our pages' rows and values) is drawn by Lua by default, parity green. `option_style.c`'s values were already only
  fallback defaults (rood.options' `M.style` sets all thirteen, M1); the help's width, lines and
  seconds are those Lua values, its breaking rule (a sentence a line when they fit, else greedy) is
  still `classic_options.c`'s, unchanged; no other line-break bug was found to fix (question below).
  `vs_main_settings` is unchanged by the tours (`menu_model_smoke`, `options_classic_smoke`).
  `primitive_extend` and `side_paint` stay on for these ranges: the look is today's (anchor `game`),
  and the rows reaching the 16:9 edges (widescreen_smoke) is their work.
- **Left in phase 2:** the tab arrows and the background toured
  (another screen shows them: Items, Status) and turned on; the help's breaking rule as Lua; the
  explicit 16:9 placement (heuristics off) as a look decision; the per-frame budget for a menu
  frame measured.
- **Questions:** which help line-break bug the task means (the fix `d474115f` is in; breaking is
  sentence-per-line when it fits, else greedy at 270 px / two lines, as `options_classic_smoke`
  checks). Whether the menus' 16:9 placement should leave today's heuristics (a look decision).

2026-10-28, later (branch `classic-lua-step8`, from main `23855d99`; no merge or publication):

- **Step 8 (targeting), done:** the rows, their leader lines and the area
  target's marker; the range shapes stay the game's (3D); the part details are
  text box 7, already step 6's kinds. Same gates as steps 4-7: each kind passed
  packet parity (zero mismatches, zero skips, no packet waivers) and state
  parity before its default went on and before the next was started; the
  fallback (setting off, a raising draw, the plugin off, recovery) is tested
  for each. Counts from the final null-renderer run:

  | Element / HUD kind | Commit | Frames | Scenarios |
  |---|---|---:|---:|
  | Target rows (a part's name on its plate) / `target_row` | `a94fe3b8` | 474 | 37 |
  | Their leader lines / `target_line` | `4448c263` | 502 | 36 |
  | Area target's marker / `target_marker` | `2299862b` | 150 | 15 |

  - **Hooks and model:** `ROOD_PC`-only ranges in 58578.c (it now includes
    `classic_hook.h`): func_800C1DC4's name and plate emit (the row's name,
    place, selected, the plate's fade before the game steps it, its colour
    bank, the slot it linked into: `unk8 + 8`, the selected `+ 4`), its two
    line calls (the projected start, the row's place, the lines' age, unk3) and
    func_800C28AC's emit (the projected place, lock = arg1, the brightness step
    just taken). The fade, the step and the sweep stay the game's; every kind
    is classic-only and the HUD's (hidden by the photo's hide{hud}).
  - **Primitives:** `d.plate` (the row's one packet: a black tile, a dithered
    POLY_G4; no game routine builds it alone, so a hand copy as `d.quad` is)
    and `d.leader` (the game's own func_800C16FC / func_800C1A40 on Lua's
    arguments, vs_scratch.unk8 pointed at the kind's slot for the call). The
    marker is `d.sprite` and `d.quad` (its corners packed as the game adds
    them, `d.xy`). Look tables `look.target_row` (the name's places and
    colours, the plate's size and the banks' colours lerped as
    vs_battle_uiGradientStop does), `look.target_line` (the ring point, lag,
    trail), `look.target_marker` (D_800EA670/684/698, the brackets, the pulse
    table). All placed with anchor `game` (16:9 as today).
  - **Coverage:** in the save room with targeting up (no enemy about: the
    gate writes rows as func_800C5798 does): every part's name (16) on a
    row's whole life from either side (slide in, the plate's fade 8..0, slide
    out), four rows at once with one selected on each side and bank, a
    deselected row's fade 7..0; the selected row's lines from 18 points about
    Ashley to a row on either side at their sweep's ages (every age 0..33
    once). The marker in its own game from the save room (Explosion's target
    through the original menus, spells written as ui_magick_smoke's prepare
    does): moved with the d-pad, every brightness step locked and with its
    brackets (Circle: the second stage), Square held. State parity 120/120 for
    rows and lines together (targeting up) and for the marker. Text box 7 in
    targeting (the band and its text) compares under `text_band` and
    `box_text`, 0 mismatches.
  - **Defaults on:** `hud_classic_target_row`, `hud_classic_target_line`,
    `hud_classic_target_marker`.
  - **Verification:** 64-bit and 32-bit (`--battle`) builds; probes
    `classic_parity_probe` and `lua_probe`; `run.py classic_parity hud classic
    cutscene_player_smoke widescreen_smoke` 7/7 on GL and 7/7 on Vulkan (every
    earlier kind green with the same counts); Docker `make commit-check`: **All
    files match** after each src change (formatter-only changes to unrelated
    code discarded). The gate leaves targeting after its tours (`3d99bc69`: the
    other kinds' fallback checks had run with it up).
  - **Not toured:** rows and lines over a real fight (the targeting's own
    state 19: the sweep's 0x22 wrap, part HP and status lines in box 7 with
    `_renderTextImmediate`'s icons, if any); the game's fixed tables are the
    gate's own rows. The 16:9 place of the rows (today centre-compressed: they
    slide in from the 4:3 edges) is a look decision left open.
- **Still outside this drawing migration:** step 6's remainders (the closing
  text, `_renderTextImmediate`, image drawers 1..3, a far actor's emote
  sprite); the live-fight packet-parity tour (HP changes, targeting over real
  enemies); step 1's remaining hand-written primitives (`d.window`, `d.rect`,
  `d.gradient`, `d.glyph`, `d.cursor`, `d.text`); the per-frame budget
  measured; per-range typography and the look lint.

2026-10-28 (branch `classic-lua-step5`, from main; no merge or publication):

- **Step 5 and step 7's actor words, timer and emote balloons, done** (step 7
  complete). Same gates as step 4:
  each element passed packet and state parity (zero mismatches, zero skips,
  no packet waivers) before its default went on and before the next was
  started; the fallback (setting off, a raising draw, the plugin off) is
  tested for each. Counts from the final null-renderer run:

  | Element / HUD kind | Commit | Frames | Scenarios |
  |---|---|---:|---:|
  | Buffs (WEAPON HUMAN +1) / `buff` | `137b6e09` | 4,925 | 35 |
  | Numbers over the actors / `damage` | `137b6e09` | 1,224 | 36 |
  | MISS and the combat labels / `actor_word` | `e62996eb` | 1,920 | 61 |
  | Time attack timer / `timer` | `8cd61271` | 60 | 14 |
  | Emote balloons / `emote` | `ade26096` | 134 | 44 |

  Every earlier kind stays green with the same counts as 2026-10-23.
- **Coverage:** buffs: every equipment and class / affinity word, both
  signs, one to three figures, the stacking rise (10, 20, 30). Numbers: HP
  and MP damage and healing (four colours, both bounces), one to three
  figures, each stat's change both ways, every age 0..30. Actor words:
  every combat label's whole life plain and lowered (the arrow), MISS from
  both the number's and the word's MISS call, two words at once, every age
  0..30 (records written as the game's spawners write them: no show).
  Timer: every digit in each place, the carries' boundaries, the 59:59:99
  overflow, hidden, shown, unpinned. Emotes, in their own launch at the
  opening (`first_bubble`, no card: actors on screen): every kind on every
  actor with both side bits (all six balloon sides), then one emoting actor
  moved (its position written each frame) through a grid beyond the view:
  the off-screen arrow in all eight directions; state parity there for
  three kinds. A far actor's sprite (the kind's bit 8) stays the game's:
  no scene the gate plays shows one. State parity 120/120 vsyncs in all
  three same-mark runs, with a buff and a number held, a lowered stat's
  word playing out and the timer up.
- **Hooks and model:** buff and damage reuse 2842C.c's existing `PC_HUD`
  ranges; the buff gains a read-only `rise` (the record's unk12). New
  `ROOD_PC`-only `PC_CLASSIC` ranges: the label loop's four
  func_8009506C / func_80095258 calls (`PC_ACTOR_WORD`: which draw the game
  chose, age, label index, lowered, copied at the call) and func_800CB83C's
  `_renderTimer` call (its BCD string still composed by the game; Lua gets
  the string and minutes / seconds / hundredths). func_80095C18 records what
  it decides as it decides it (balloon kind, side, place, depth; the arrow's
  direction and clamped place) for a range around func_8009627C's call; the
  emote's default layer resolves to its own depth slot (`unk4 + z` words),
  as `"unk8"` resolves (decision 2026-10-28). Kinds from `gauges` on are
  `PC_HUD_CLASSIC_ONLY`. MISS's third call site (label kind 3) is hooked but
  not toured: nothing in BATTLE.PRG's C creates a kind 3 label.
- **Presentation and primitives:** one new classic primitive, `d.quad` (a
  POLY_FT4 as the label loop builds its own: screen rectangle, texel
  rectangle, colour word, raw, page blending, alpha; explicit corners and
  per-corner texels for the mirrored balloon and the turned arrow, one
  primitive by decision). The gate's normaliser
  now ignores what the GPU never reads (a raw-textured primitive's colour, a
  textured polygon's pads after its last two texel pairs): the game's
  hand-built quads leave them as the buffer held them; the probe tests it.
  The state proof also masks the text boxes' private ordering tables
  (D_800F4CD0) and the room drawing's copies of the packet pointer
  (D_800F1CC0, D_800F1BB8): a classic draw allocates after the game's
  no-ops, so later packets and the links to them sit further on (seen
  with a text box open at the opening).
  Own look tables: `look.buff`, `look.damage`, `look.actor_word` (the game's
  label, piece and colour tables, slide, fade, squash, arrow), `look.timer`,
  `look.emote`.
  The buffs' and numbers' digits are the digits style's, so
  `look.typography.digits` sets them (declared `typography`, waived with no
  fields, as the gauges').
- **Defaults on after green:** `hud_classic_buff`, `hud_classic_damage`,
  `hud_classic_actor_word`, `hud_classic_timer`, `hud_classic_emote`.
- **Verification, passed:** 64-bit and 32-bit (`--battle`) builds; probes
  `classic_parity_probe` and `lua_probe`; `run.py classic_parity hud
  classic` 5/5 on GL and 5/5 on Vulkan; Docker PS1 `make commit-check`: **All
  files match** (after each src change and on the final tree; formatter-only
  changes to unrelated code discarded). No game data, outputs or player
  files committed.
- **Step 6 (text boxes, the name window): frames, band, text and images done**
  (decisions 2026-10-28: the vector bubbles stay an opt-in over rood.classic's
  copies; the settings stay the source of the dialogue layout; frames first,
  then the text with the typewriter left in C). Same gates; counts from the
  final null-renderer run:

  | Element / HUD kind | Commit | Frames | Scenarios |
  |---|---|---:|---:|
  | Bubble and info box frames / `text_frame` | `9b4f8d8e` | 917 | 146 |
  | Name window band / `text_band` | `35f897f7` | 216 | 43 |
  | Text boxes' text / `box_text` | `9a38d4e6` | 878 (+124 in the save room) | 6 |
  | Portraits and stills, tiled drawer / `image` | `84892647` | 717 | 1 (3 images' whole lives) |

  - **Approach:** each element is drawn by the game's own routine on a copy
    of its inputs (§2.4's `window` row): `d.frame` (func_800CD3E4), `d.band`
    (func_800CD158 with its icon), `d.char` (vs_battle_printVariableWidthFontChar,
    with the font table, colour style and brightness the printer had),
    `d.image` (D_800EC25C's drawer). The typewriter (speed, pages, the
    arrow, sounds, buttons) stays C: ROOD_PC snapshots record each glyph
    as it is set. The text boxes' private ordering table is the hook's slot
    (a shadow table in the parity mode; the band's icon slot in unk8 too).
  - **Placement:** new `d.anchor("game")`: no exact anchor, widescreen
    treats the range as the game's own 2D. With the exact anchor the band
    lost its 16:9 edge treatment (the band's right end differed); with
    "game" the band rows are pixel-identical. Frames, band, text and images
    use it, so 16:9 looks as today. The explicit Lua placement of §2.6
    (heuristics off) is left for a later look decision.
  - **Interplay:** the vector bubbles (opt-in) verify rood.classic's frames
    (a copy replaces the game's pending bubble; nothing recorded for the
    shadow); ui_text registers `d.char`'s glyphs as the game's (layout and
    bubble alignment from the settings); a drawer's own widescreen range in
    the shadow is noted, not recorded (`pc_hw_anchor_muted`).
  - **Coverage:** frames: the opening bubble's zoom, all nine shapes at each
    flip and tail, two lines' bubbles closing and opening; band: two whole
    name-window lives, top and bottom, one to four lines, each icon; text:
    the duke's line, the box as other kinds and selections, the next line
    typed out glyph by glyph, the name window's text in the save room;
    images: New Game's opening cards from fade-in to fade-out. State parity
    120/120 for each.
  - **Still the game's:** the closing text (`_printFixedWidthFont`) and
    `_renderTextImmediate` (types 0/6); the image drawers 1..3 (the credits'
    wipes and others: no scene the gate plays shows them); a far actor's
    emote sprite.
  - **Defaults on:** `hud_classic_text_frame`, `hud_classic_text_band`,
    `hud_classic_box_text`, `hud_classic_image`.
  - **Verification:** 64-bit and 32-bit builds; probes; `run.py
    classic_parity hud classic ui_text_smoke ui_lines_smoke opening_smoke
    widescreen_smoke` 9/9 on GL and 7/7 on Vulkan; commit-check All files
    match after each src change.
  - **Fix after the merge (verify's cutscene_player_smoke, 16:9 credits
    card and tear missing):** not the Lua draw: every `PC_CLASSIC` site's
    packets were suppressed while the photo hide (`hide{hud}`, the cutscene
    player's capture) was on, and the images, text boxes, emotes, actor
    words and timer had never been the HUD's to hide (the dialogue has its
    own, `game2d`). Those sites now keep their packets under the HUD hide,
    as before step 5. The classic gate now runs `run.py classic_parity hud
    classic cutscene_player_smoke widescreen_smoke` on GL and Vulkan (packet
    parity cannot see a host-side hide or widening).
- **Still outside this drawing migration:** step 6's remainders (above);
  targeting (step 8); the live-fight
  packet-parity tour for HP changes; step 1's unused remaining primitives;
  per-range typography and the look lint.

2026-10-23 (branch `classic-lua-step4`, from main; no merge or publication):

- **Step 4's remaining drawing, done.** Each element passed the packet and
  state gates before its default was enabled and before the next element
  was started. All have zero mismatches, zero skips and no packet waivers.
  Counts below are from the final canonical-card null-renderer run:

  | Element / HUD kind | Commit | Frames | Scenarios |
  |---|---|---:|---:|
  | Ashley's body/damage silhouette / `figure` | `969378ec` | 1,232 | 22 |
  | Status carousel and indicators / `status_icons` | `9e839185` | 640 | 237 |
  | BATTLE MODE / `battle_mode` | `2c87e13f` | 139 | 42 |
  | Ability icons / `ability_icons` | `4e5b322b` | 2,086 | 31 |

  Ability icons have nine ranges per display frame: 18,774 hook comparisons,
  not 18,774 display frames; the JSON reports distinguish these units.
  The existing status, chain and gauges remain green (576, 558 and 1,192
  frames respectively).
- **Coverage:** both figure stances and every limb-condition boundary;
  all 32 status icons on/off, clipping, count boundaries and carousel
  phases; BATTLE MODE entry, exit, pulse and panel slide; ability label
  availability, every entry index, exit/hidden state and occupied/empty
  weapon slots. State parity is 120/120 vsyncs per run, including active
  carousel/pulse at tick speeds 2 and 4 and the ability exit animation.
  Setting-off, raising-draw and plugin-off fallback/recovery are tested.
- **Hooks and model:** `5BF94.c` has `ROOD_PC`-only `PC_CLASSIC` ranges in
  `func_800CA2DC` (figure, carousel, BATTLE MODE) and `func_800C9FE8`
  (ability groups). Logic, animation and availability decisions stay C;
  Lua gets copied fields. `pc_weapon_slots_label` remains outside the
  ranges, called once per ID in the original order, retaining its PC-only
  weapon names/markers and button-prompt side effects. `_renderTextImmediate`
  is untouched. The enclosing `status_panel` remains capture-only.
- **Presentation and primitives:** all four copies use their own Lua look
  tables. `d.sprite` now allocates/links through `vs_battle_setSprite`
  (the earlier progress description overstated this); exact BGR, zero-size
  clipping and inherited texture-page/NOP support preserve the packets.
  `d.layer("unk8")` resolves the actual slot, including shadow ordering.
  No additional hand-written primitive was needed. Follow-up look-only
  commits expose supplemental clipping/indicator geometry (`a17215cc`) and
  ability availability colours/alpha (`c2847ddb`), each parity-checked.
- **Defaults on after green:** `hud_classic_figure`,
  `hud_classic_status_icons`, `hud_classic_battle_mode`,
  `hud_classic_ability_icons`.
- **Verification, passed:** 32-bit boot/battle and 64-bit builds passed;
  CMake probes 67/67; Docker PS1 `make commit-check`: **All files match**.
  Formatter-only changes were discarded. The requested
  `run.py classic_parity hud classic` passed 5/5 on GL and 5/5 on Vulkan.
  The screenshot smoke now compares the same marked frame (`afe72911`),
  and the HUD bench checks the loaded weapon stance rather than assuming
  it is sheathed (`284b68db`, corrected model-bit oracle `03459fa0`), without
  weakening the assertions.
  Final `verify.sh --changed main -- -j 6` exited 0: **all passed** in
  4,571 seconds, using the pinned test card. Null: 13/13 passed; Vulkan:
  149/151 passed, 2 skipped; GL: 148/151 passed, 3 skipped. Skips are the
  renderer-specific probes and golden frames (no local image references).
  Logs: `pc/build/step4-final/verify-final.log`, detailed results under
  `pc/build/verify/`; packet reports under
  `pc/build/verify/t64null/classic_parity/classic_parity/`.
  An earlier run using a different play-state fixture was stopped; its
  failures are superseded by the completed pinned-fixture run. No game data,
  outputs or player files are committed. Work parked before the deadline.
- **Still outside this drawing migration:** the dedicated live-fight
  packet-parity tour for HP changes; step 1's unused remaining primitives;
  buff/damage, text boxes/name window, actor words/timer/emotes and targeting;
  per-range typography and the look lint. BATTLE MODE and ability icons
  from step 7 are now done. The PC-only weapon-slot names/markers remain C.

2026-10-20 (branch `classic-lua`; Extensions.md "The classic look in Lua"):

- **Phase 0, done.** `pc/src/classic/classic_parity.c` (the walker in
  ordering-table order, the normaliser, the comparison by field, the JSON
  dump), ctest `classic_parity_probe` (synthetic packets, the round trip),
  RPC `packets {kind | start, end}` (the status panel's range dumped
  normalised: `packets {kind: "status_panel"}`) and `classic {parity,
  reset}`. `PC_CLASSIC` (`classic_hook.h`; `PC_HUD` is now this hook) with
  the emit-only suppression generalised (`pc_classic_suppress`: every
  drawing packet of the range, packets that only set draw modes kept). The
  unknowns are in the inventory below (MISS, BATTLE MODE, area titles,
  Ending). `PC_ANCHOR_EXACT` (widescreen.h) for ranges placed by data.
- **Phase 1 step 1, in part.** On game routines: `d.text_raw`
  (vs_battle_renderTextRawColor), `d.value`, `d.digit`
  (vs_battle_renderValue, _renderDigit), `d.bar` (as before), `d.sprite`
  (the game's sprite packet), `d.layer`, `d.anchor`, `d.xy` (the game's
  packed arithmetic). Still hand-written: `d.window`, `d.rect`,
  `d.gradient`, `d.glyph`, `d.cursor`, `d.text` (no kind of rood.classic
  uses them yet).
- **Step 2, done** for the kinds rood.classic draws: the parity mode at the
  hooks (the Lua draw into shadow slots), `pc/tests/classic_parity.py`
  (run.py `classic_parity`, null renderer, verify.sh's CPU lane): whole
  lives of every element, waivers = declared overrides, the default and
  the fallback, state parity from one mark (§3.4). Buff and damage are
  compared only once rood.classic claims them (step 5).
- **Step 3, done.** `pc/plugins/rood.classic` (shipped, on): status and
  chain as exact copies, green (576 and 558 frames, 0 mismatches), on by
  default (`hud_classic_status`, `hud_classic_chain`). Their typography is
  its `look.typeset` (blanks, keep on screen, margin, word space: `645a7477`
  as Lua values, handed to ui_text with the range,
  `pc_text_status_typeset`) and `look.typography`. Claims per flavour
  (`rood.hud.claim(kind, fn, {flavour})`); rood.hud keeps the modern look.
  Hot reload measured at 60 Hz: 0.19 s mean, 0.20 s max from the save to the
  first frame set with it (classic_hud_smoke times it on every run).
- **Step 4, in part: the gauges.** A HUD kind `gauges` (the classic look's
  only): 5BF94.c's `PC_CLASSIC` around func_800CA2DC's `_renderStatBar`
  calls and RISK ZERO (commit-checked); fields hp, max_hp, mp, max_mp,
  risk, slide. rood.classic's copy is green (1192 frames: digits of one to
  four figures, empty and full bars, RISK ZERO and RISK's bar, the slide
  out and back) and on by default (`hud_classic_gauges`); their 16:9 place
  is `look.gauges.place`; the digits' typography is
  `look.typography.digits.descender_lift` (`1d2f20c5`), through
  `rood.hud.typography`, which now also takes ui_text's tunables. Still
  the game's: Ashley's figure, the status icons, BATTLE MODE, the ability
  icons (the rest of func_800CA2DC). The digits' typography is style-wide
  (every digit the digits style draws), not keyed by range: per-range
  overrides for ui_text's glyph path are still to do (§6 "ui_text
  coupling").
- **Left in phase 1:** step 1's remaining primitives; step 4's other parts
  (and a corpus fight for HP changes in play); steps 5-8 (buff and damage,
  text boxes and the name window, the remaining HUD kinds, targeting);
  the per-range typography; the look lint (§5: not started, not even its
  report-only baseline).

## Goal

Every aspect of the classic look is a hot-reloadable Lua edit, not a C change:
- typography;
- spacing and small layout tweaks;
- 16:9 placement.

Two rules hold throughout:
1. **Classic respected.** With the modern UI off (`ui_menus 0`), the classic
   look keeps the game's own sprites, windows, gauges, cursor and layout. The
   only allowed upgrade is typography (memory rule "Classic UI respected").
   - By default, a Lua classic draw reproduces the game's packets exactly
     (the parity gate, §3).
   - Every deviation (a typography style, a 16:9 offset) is explicit, named
     and listed.
2. **Presentation is Lua.** Lua tables and draw functions over C primitives.
   - No new JSON or look formats.
   - No look values as C constants (the lint, §5).

Why now: the user plays on the classic look, and today's typography
experience is what drives this plan. Four playtest bugs (2026-10-20, branch
`playtest-fixes`, not yet on main) each cost a C change, a rebuild and a
replay:

| Commit | Bug | Fixed in |
|---|---|---|
| `1d2f20c5` | HUD digits squashed ("57 * 57": old-style 5 and 7 squeezed by `outline_room`) | `ui_text.c`, `ui_text_descender_lift` |
| `645a7477` | Timing-word gaps: RIGHT ON! / WELL TIMED! / TOO SLOW! with a huge gap; WELL cut at the left edge in 16:9 | `ui_text.c` status layout; rood.hud `look.words.margin` |
| `d474115f` | Option help line breaks | `classic_options.c`, `option_style`, rood.options `help_lines` |
| `178969a4` | The save confirm's YES/NO rows split in 16:9 (cleared by `side_paint` beside the cursor sprite) | `gpu_hw.c` |

Phase 1 is ordered so this class of problem becomes a Lua edit first, on the
elements the user sees most.

## What exists (the facts this plan builds on)

- **ui_text works below the packets.** `pc/src/ui_text.c`:
  - `gpu_hw.c` hands every textured 2D region to `pc_ui_text_classify` /
    `pc_ui_text_render`. A glyph is recognised by its page, CLUT and texel
    rectangle and re-rasterised from an SDF of the style's font.
  - The printers' `ROOD_PC` hooks (`pc_text_glyph`, `pc_text_status`) add
    pen positions and advances for line layout.
  - So **typography is downstream of the packets**: the game (or Lua)
    emits the same SPRTs, and ui_text restyles them.
  - The packet stream is therefore a clean parity target: identical packets
    plus ui_text equals identical classic look plus typography.
- **The classic flavour of the HUD layer exists, but opt-in and partial.**
  - Mechanism: `hud_classic_override 1` makes a claim run at the element's
    hook (`PC_HUD` in 2842C.c → `pc_hud_element` →
    `pc_hud_classic_draw` → `classic_packets`, `pc/src/hud/hud_bridge.c`).
    It emits PS1 packets into the HUD labels' OT slot.
  - It covers four kinds only (status, chain, buff, damage). The name
    window is an event only; Travel is C++.
  - `d.text` uses the game's printers and `d.bar` calls
    `vs_battle_renderStatBar`. **But `d.window`, `d.rect`, `d.gradient`,
    `d.sprite`, `d.glyph` and `d.cursor` hand-write packets**
    (`window_packets`, `quad_packets`, `sprite`), so their parity with the
    game is not guaranteed.
- **No `PC_HUD` hook exists** for:
  - the HP/MP/RISK panel (`func_800CA2DC`; only anchored);
  - the text boxes (`func_800CB83C`);
  - the over-actor words (label kinds 2/3);
  - any menu screen.
- **Widescreen 2D** (`pc/src/widescreen.h`, `gpu_hw.c`):
  - By default, 2D is compressed 3/4 about the centre.
  - Ranges tagged with `PC_ANCHOR_PACKETS` / `pc_hw_anchor_packets` are
    scaled about an edge. Tagged today: the whole status panel (146C.c:8095,
    left) with the ability icons (centre) and its top-right sprite (right)
    inside it; status/chain/buff in 2842C.c (left); `_drawUIControls` in
    MENU5 4D8.c (left); scene/cover/fill ranges. Text boxes, the name
    window, targeting and all menus are centre-compressed.
  - Everything else relies on heuristics in `pc_hw_set_bounds`:
    - `primitive_extend` stretches untextured 2D that reaches an edge;
    - `side_paint` clears what an extension painted.
  - These heuristics are where the 16:9 menu bugs come from.
- **The menus already have a retained row model.** `vs_battle_menuItems`:
  - 40 × `vs_battle_menuItem_t`, `BATTLE/5BF94.h:43`;
  - fields: state, x, y, w, selected, unselectable, text[32], subText,
    icon, button, font colour, material, count/max, …
  - Rows 0-9 are the ring or the submenu list; rows 32+ are the action menu.
  - It is drawn every frame by `_renderMenuItems` (58EC.c:1480) through
    `vs_battle_renderMenuItem` (5BF94.c:2590).
  - **But `_renderMenuItems` also steps the rows' slide animations: logic
    and drawing are interleaved.** This is the main design constraint for
    menus.
- **Tooling to reuse:**
  - The oracle (`pc/docs/Oracle.md`): mark/rewind, record/replay, page
    digests, `alternate`.
  - `pc/tests/diff/differ.py` and `waivers.toml`.
  - The bench (`rood.dev.show`, held moments) and the gallery
    (`rood.dev.bench`, RPC `restart {snapshot}`).
  - RPC `pause` / `step`; the null renderer lane.
  - `golden_frames.py`.
  - There is **no packet/OT dump** yet (only `hud {stats}`' last commands).

## 1. Inventory

Columns: **Draws** = the src function that emits it. **PC today** = what
pc/ code touches it. **Lua?** = can Lua draw it today in the classic look.

### Field and battle (BATTLE.PRG, every frame)

Frame order (146C.c ~7590-8110): pad, then OT/scratch setup, then the state
switch (menu mode: `func_800C97BC`), then `func_800CA2DC` (HUD), then
`func_800CB83C` (text boxes), then the OT draw. Labels (status words, damage
numbers…) come from the 2842C.c label loop (~1830-1930), over a pool of
`D_800F1BAC_t` records.

| Element | Draws | PC today | Lua? | Known bugs / notes |
|---|---|---|---|---|
| HP / MP / RISK gauges and digits, RISK ZERO, the panel's slide (`D_800EB9B0`) | `func_800CA2DC` → `_renderStatBar` (5BF94.c:2959) → `vs_battle_renderStatBar`, `vs_battle_renderValue` / `_renderDigit`, `vs_battle_renderTextRaw("RISK  ZERO")`; labels HP/MP/RISK as sprites (`func_800C9CB4`) | whole panel anchored left (146C.c:8090-8095, raw `pc_hw_anchor_packets(…, 1)`); the label sprite made opaque under PC in `_renderStatBar`; ui_text `digits` and fixed labels (HP/MP/RISK, `ui_labels.h`); modern menus hide it | no | squashed digits (`1d2f20c5`) |
| Limb status figure (5 parts, colour by limb status) | `func_800CA2DC` loop over `D_800EBCEC`/`D_800EBD14` | none | no | |
| Status-effect icons and words (poison, …) | `func_800CA2DC` after `vs_battle_getStatusFlags` | ui_text | no | |
| BATTLE MODE, the top-right sliding sprite (`D_800EB9CD` its slide-in step while `D_800EB9CC` is 1: battle mode on) | `func_800CA2DC` (`vs_battle_setSprite` 73 x 10, page (448, 256), texels (64, 0), CLUT (976, 220), bobbing by `rsin(D_800EBD64)`) | anchored right inside the panel's left range | no | confirmed (phase 0: RPC `packets {kind: status_panel}` after Circle, and its picture) |
| Chain / defense ability icons (L2 cross, face-button diamond, D-pad) | `func_800C9FE8`, `func_800C9F98` (5BF94.c) | centre anchor; `weapon_slots.c` replaces `func_800C9EB8`; `button_prompts.c` redraws texels | no | |
| Emote balloons (…, !, ?, heart) | `func_80095C18` (2842C.c) | `PC_GTE_UNNARROWED`; `ui_emotes.c` vector redraw | no | |
| Timing word (TOO FAST!, FAST!, SLOW!, success words) | 2842C.c label kind 4 → `func_800958A4` | `PC_HUD(PC_HUD_STATUS)`, anchored left, ui_text `status` (`pc_text_status`) | yes (opt-in flavour) | gaps and left-edge cut (`645a7477`) |
| Chains (3 CHAINS) | kind 5 → `func_80095A4C` | `PC_HUD(PC_HUD_CHAIN)`, anchored left | yes (opt-in) | |
| Buff popup (WEAPON HUMAN +1) | kind 6/7 → `func_8009695C` | `PC_HUD(PC_HUD_BUFF)`, anchored left | yes (opt-in) | |
| Damage / heal / MP / stat numbers over actors | kind 1 → `func_80094844` / `func_80094B0C` | `PC_HUD(PC_HUD_DAMAGE)`; unnarrowed projection; ui_text `popup_scale` | yes (opt-in) | digits share the squash bug |
| Over-actor words: `func_8009506C` (`unkE == 2`, label kinds 1-3): MISS, four textured quads of the info capitals (`D_800E8640`: M at (100, 86) w 11, I, S, S; page (768, 0), CLUT (864, 223)) squashed open and shut (`D_800E8634`); `func_80095258` (kind 2 otherwise): a `CombatLabel` (`D_800690B4[unkC]`: one or two pieces of page (448, 256) from `D_80068FAC`, a colour from `D_80069134`; the status ailments' and buffs' words), sliding in, a fade ramp, `func_800957D0`'s arrow for a lowered stat | 2842C.c label loop | ui_text; centre-compressed | no | confirmed (phase 0: the tables read from RAM) |
| Name window (action name, text box 7 band) | `vs_battle_displaySceneMessage` → text box 7 → `func_800CB83C` | `pc_charge_skip_name`, `pc_hud_name_window` event; drawn as a band by `func_800CD158`; Zig trial port of `displaySceneMessage` (logic only) | event only | |
| Text boxes: dialogue bubbles, information box (box 7), item/notice boxes | `func_800CB83C` (all 8 boxes) → frames `func_800CD158` / `func_800CD3E4`; text `_printVariableWidthFont` / `_printFixedWidthFont` / `_renderTextImmediate` into the private OT `D_800F51B8`, spliced with `AddPrims` | ui_text `dialogue`/`menu` (line layout, `bubble_segments`); `ui_bubbles.c` vector bubbles (`pc_bubble_rect`/`pc_bubble_frame`); `text_lines.c`, `fixed_font.c`; centre-compressed; portraits/stills `func_800CCA90` (full-screen ones `PC_ANCHOR_FILL`) | no | |
| Timer (time attacks) | `_renderTimer` (5BF94.c:3788) | ui_text digits | no | |
| Button prompts in the field (classic sprites) | `vs_mainmenu_renderButton` / text boxes | button roles redraw (`pc_button_roles_glyph`) | `d.glyph` (opt-in) | |
| Travel prompt (PC feature) | `pc/src/fast_travel.c`, C++ | modern claim; the classic look is drawn with the game's sprites by C++ | modern only | |
| Targeting: range shapes, target rows and leader lines, lock-on marker, part details | `58578.c:func_800C0D78` (shapes), `func_800C20B4` / `func_800C1DC4` (rows: gradient + `renderTextRawColor`), `func_800C16FC` / `func_800C1A40` (leader lines), `5BF94.c:func_800C28AC` (marker); state machine `func_800C58F8` prints part HP/status/class/affinity into text box 7 | `PC_GTE_UNNARROWED` on the line start and marker; rest centre-compressed; ui_text | no | |
| Minimap / compass | none in the field HUD; the compass is the Map screen's (`_renderCompass`, MENU5) | — | — | |
| Area titles, loading | no HUD drawer: the room names (`vs_battle_initSceneAndGetRoomNames`, the scene's section F) are shown by the map (MENU5 text box 0) and by event text boxes (`func_800CB83C`), so an area's title card is a `text_box` (phase 1 step 6); no loading screen, only `146C.c:_drawScreenTransition` (fades) | — | — | phase 0: no separate drawer in BATTLE.PRG; the card's box id is recorded with step 6 |
| Command ring (battle mode menu, rows 0-9) | `func_800C930C` (logic) and `func_800C97BC` → `vs_battle_renderMenuItem`, `vs_battle_drawCursor` | modern menus replace it (`pc_ui_menu_*`) | no | |

### Menus (MAINMENU.PRG + MENUx overlays; `vs_mainMenu_exec`, C48.c:347)

Shared frame: `vs_mainMenu_renderScreen` (58EC.c:1735) draws, in order:
- the items list `vs_mainMenu_renderItemsList`;
- the background fade `_renderBackground`;
- the `_renderMenuCommand` title;
- `_renderMenuItems` (the 40 rows);
- `_renderAbilityCost`;
- `vs_mainMenu_drawClassAffinityType`;
- `vs_mainMenu_renderEquipStats`;
- `vs_mainMenu_renderDpPpBars`.

Shared primitives:
- `vs_mainMenu_renderCursor`, `vs_mainmenu_renderButton`,
  `vs_mainMenu_renderIntColor`, `vs_mainMenu_renderRowLabel`,
  `vs_mainMenu_renderMenuRowIcon`, `vs_mainmenu_renderButtonBackground`,
  `vs_mainMenu_renderTabNavigation`;
- the information line `vs_mainmenu_setInformationMessage` (text box 7).

Modern equivalents: `pc/src/ui/*_bridge.c` + `ui_*_draw.cpp` (paused, a
separate plugin later).

| Screen | Overlay | Draws | PC today | Lua? | Known bugs |
|---|---|---|---|---|---|
| Ring / shared frame | MAINMENU | above; ring rows `func_800C97BC` | `pc_ui_direct_tick`, `pc_ui_menu_*` hooks (C48.c, 58EC.c) | no | |
| Magick (+ Teleport) | MENU0 | `vs_menu0_exec` (15AC.c), `84.c` menus; rows via the shared frame | magick_bridge (modern); 4 `ROOD_PC` hooks | no | |
| Break Arts | MENU1 | `vs_menu1_exec` (30.c) | abilities_bridge (modern) | no | |
| Battle Abilities; unlock popup | MENU2 | `vs_menu2_exec` (64.c), `vs_menu2_actionUnlock` (143C.c) | abilities_bridge | no | |
| Items (lists, status views, equip, sort, discard) | MENU3 | `vs_menu3_exec` (16C.c) + `items0/1/2.h`, `_renderAshley.h` | items_bridge, equip_bridge | the shared frame (rows, hand, title, tabs, background; 2026-10-28) | a grip's status view crashed (`7b75d81c`) |
| Status / equipment detail | MENU4 | `vs_menu4_exec` → `_drawScreen` (120.c:2195), `_renderLimbUi`, `_renderBasicStats`, `_drawWeaponInfo`…; 3D actor MAINMENU 224.c | ashley_bridge; 1 hook | no | |
| Map | MENU5 | `_renderScreen` (4D8.c:1007), `_renderMap`, `_renderCompass`, `_drawUIControls`, map labels | map_bridge frame; `_drawUIControls` anchored left; 16 hooks | no | the 3D map stays the game's |
| Data: save/load card screens, save-container prompt, Game Over | MENU7 | `_drawFileMenu`, `_drawFileMenuElement`, `_printString` / `_printCharacter` (own printer), `_drawSprt`, `_drawHPMP`, `_displayGameOverScreen` | data_bridge (modern frame); 13 hooks; ui_text through `_printCharacter` | no | YES/NO split in 16:9 (`178969a4`) |
| Options (+ our pages); Rename | MENU8 | `vs_menu8_exec` (88.c); our pages `pc/src/ui/classic_options.c` (game primitives); Rename `_renderRenameMenu` (21A0.c) | M1 hooks; `option_style` values from rood.options' `M.style` | the values only | help line breaks (`d474115f`) |
| Score | MENU9 | `_renderTopMenu`, `_renderTitleMenu`, `_renderEnemyMenu`, `_renderEnemyDetailScreen`, `_renderBossRush*` (238.c) | score_bridge (native modern) | no | |
| Quick Manual | MENUE | `_drawContent`, `_drawContentLines`, `_drawSprite` (494.c) | manual_bridge (native modern) | no | |
| Loot | MENUB | `vs_menuB_exec` (260.c), `_displayCurrentLoot`, `items*.h` | loot_bridge | no | |
| Workshop | MENUC | `vs_menuC_exec` (168.c), `_drawOk` | workshop_bridge | no | |
| Container | MENUD | `vs_menuD_exec` (234.c) | container_bridge | no | |
| Results (congratulations, time attack, cube puzzle) | MENUF | `_renderCongratulationsScreen`, `_renderTime*`, `_renderCubePuzzle*` (3B8.c) | 1 hook | no | |
| Title, its card screens | TITLE.PRG (22C.c) | own `_drawSprt`, `_drawSaveInfoUI`, `_drawHPMP`, `_printCharacter` (a copy of MENU7's) | ui_text | no | runs without BATTLE.PRG: its primitives differ |
| Ending | ENDING.PRG (D4.c) | credits elements (`_renderText`, `_renderPhantomPain`, `_renderVagrantStory`, `_renderFin`, illustrations), then the results screen: `_renderCongratulationsScreen`, `_renderScore` / `_renderIncrementalScore`, `_renderMapCompletion`, `_renderRiskbreakerRank`; its own `_drawScreen` and OT | `ending_smoke`; ui_text | no | surveyed in phase 0: the results screen is UI (phase 3 #10); the credits stay the game's |

## 2. Architecture

```
game frame (src/, unchanged logic and input)
  draw site, wrapped:  PC_CLASSIC(kind, model_fill, game_draw)      pc/src/classic/classic_hook.h
     model: copy the inputs of this draw into a fixed record      (read-only to Lua)
     owner? (a Lua claim for kind, honoured, not failed, overlay resident, budget left)
        yes: Lua draw(e, d) -> classic commands -> game primitives -> packets (same OT slot)
             game_draw's *logic* side still runs (see "split"), its packets are not emitted
        no:  game_draw as today
     parity mode: both run; the Lua packets go to a capture arena and are compared (§3)
```

### 2.1 Element kinds (HUD) and claims

- **Extend the existing HUD layer, don't fork it.** The kind table in
  `pc/src/hud/hud.h` gains classic kinds.
  - Each kind has: a hook site in src, a fields record and a parts list.
  - Its parts are named sub-boxes, so Lua can restyle one piece and leave
    the rest the game's.
- **New kinds:**

  | Kind | Hook site | Fields |
  |---|---|---|
  | `status_panel` | `func_800CA2DC` | hp, max_hp, mp, max_mp, risk, slide, limb statuses, status flags, notice sprite state |
  | `text_box` | `func_800CB83C`, per box id | id, kind, state, rect, lines (decoded text and the glyph runs), cursor/arrow |
  | `actor_word` | 2842C.c kind 2 | |
  | `timer` | `_renderTimer` | |
  | `target_rows` | `func_800C1DC4` per row, `func_800C28AC` | |
  | `ability_icons` | `func_800C9FE8`, `func_800C9F98` | |

  `name_window` becomes claimable as a `text_box` with id 7.
- **Classic is a flavour, not a kind.** The same claim gets
  `e.flavour == "classic"` and a `d` restricted to the classic primitives.
- `hud_classic_override` becomes **per kind and defaults on once the kind
  passes parity** (open question 4). The global setting remains as a master
  switch.

### 2.2 The split: logic stays the game's

Several draw functions mutate state as they draw:
- `_renderMenuItems` steps the slide animations;
- `func_800CA2DC` advances `D_800EBD64` and the slide `D_800EB9B0`;
- the label loop ages records.

The game's logic and input must stay untouched. So a hook never skips a
function that mutates state. Per site, one of:
- **(a) Emit-only suppression.** The game's function runs, and its packets
  between `first` and `end` are turned into no-ops (as `pc_hud_element`
  does today). Lua emits its own. Cheapest. The OT link order is kept
  because Lua's packets go into the same slot.
- **(b) A logic/draw split.** Where a state change is buried in a loop that
  also emits many packets (the 40-row `_renderMenuItems`), a `ROOD_PC`-only
  restructure keeps the state steps and wraps each row's emit in
  `PC_CLASSIC`.

  Proof that the restructure changed nothing: `make commit-check` (the PS1
  build is byte-identical; ROOD_PC code is not in it), plus an oracle
  digest run with Lua off that compares the PC build's RAM before and after
  the restructure.

The state-parity check (§3.4) proves for every kind that a Lua draw leaves
game RAM identical to the game's draw.

### 2.3 The menu model (read-only Lua tables)

- `rood.game.menu()`: one cached table per vsync, a copy (never pointers):

  ```lua
  { screen = "items", overlay = "MENU3", loaded = true, menu_state = 0x44,
    state = 7,                                        -- the overlay's submenu state byte
    rows = { {id=0, state="shown", x=, y=, w=, text="Weapons", sub="",
              selected=true, unselectable=false, icon=, button=, color=,
              material=, count=, max=, scrollable=, anim=}, ... },   -- vs_battle_menuItems, live rows only
    cursor = { row=, page=, rows=, memory=, color= },  -- 58EC.c statics via a tiny accessor
    info = "…",                                       -- text box 7 (setInformationMessage)
    list = { … },                                     -- vs_mainMenu_renderItemsList's page when open
    panels = { equip_stats=…, dp_pp=…, class_affinity=…, ability_cost=… },  -- each when drawn
    screen_data = { … } }                             -- per-overlay extras (§1), filled by that screen's bridge
  ```

- **Where it is filled:**
  - `pc/src/classic/menu_model.c` (BATTLE builds), from
    `vs_battle_menuItems`, `vs_battle_menuState`, `vs_battle_submenuStates`,
    `D_800F4EE8.cursorMemories` and `vs_battle_textBoxes[7]`;
  - plus `ROOD_PC` accessors for 58EC.c statics (`_selectedRow`,
    `_subMenuPage`, `_itemsText`), the same pattern as `score_menu9.h`.
  - Text is decoded by `pc_ui_text_decode`. A row's `glyphs` are kept too,
    so a Lua draw can re-emit the game's own glyph codes exactly.
- **Rule: draw functions read the model and emit.** No writes, no input, no
  sounds. Actions (Circle, Cross, rows' effects) stay the game's. The Lua
  side of a menu is a skin.
- **The Zig tie-in (phase 4).** The model's shape is designed to outlive
  the hooks. When `game/` owns MAINMENU and the overlays, it fills the same
  table from native data, and the `PC_CLASSIC` hooks in src/ are deleted
  with the rest of the subsystem's hooks at its flip.

### 2.4 Classic primitives (`d` in the classic flavour)

All classic primitives become **calls into the game's own routines**, and
packets are emitted in the game's frame. Today's hand-written packet
builders in `hud_bridge.c` are removed or kept only behind a parity waiver.

| `d.` | Game routine | Note |
|---|---|---|
| `sprite` | `vs_battle_setSprite` / `setSpriteDefaultTexPage` | |
| `tile` (new), `rect` | `vs_battle_addTile` | `rect` alpha via the game's DR_MODE |
| `gradient` | as `vs_battle_renderMenuItem`'s POLY_G4 | factor that emit into a ROOD_PC helper if no standalone routine exists |
| `text_raw` (new; `d.text` font `info`) | `vs_battle_renderTextRaw(Color)` | |
| `text` (fonts `menu` / `dialogue`) | `vs_battle_printVariableWidthFontChar` per glyph with `vs_battle_characterWidths` | the glyph codes from `vs_encode` or from the model |
| `value` (new) | `vs_battle_renderValue` / `_renderDigit` | the HUD digits |
| `int` (new) | `vs_mainMenu_renderIntColor` / `…WithThreshold` | MAINMENU only |
| `bar` | `vs_battle_renderStatBar` | as today |
| `menu_item` (new) | `vs_battle_renderMenuItem(&copy)` | a model row, Lua may change x, y, w, text |
| `row_label`, `row_icon`, `button`, `button_bg`, `tabs` (new) | `vs_mainMenu_renderRowLabel`, `vs_mainMenu_renderMenuRowIcon`, `vs_mainmenu_renderButton`, `vs_mainmenu_renderButtonBackground`, `vs_mainMenu_renderTabNavigation` | MAINMENU only |
| `cursor` | `vs_mainMenu_renderCursor` (menus) / `vs_battle_drawCursor` (ring) | the animation step from the model |
| `window` | the text box frame `func_800CD158` / `func_800CD3E4` | today's `window_packets` is a hand copy |
| `glyph` | `vs_mainmenu_renderButton` | as the button roles redraw |
| `layer(n)` (new) | | the OT slot: the game's `unk4`/`unk8` offsets, default the hook's own |
| `anchor(kind)` (new) | `pc_hw_anchor_packets` over the commands that follow | §2.6 |

- Routines in MAINMENU or a MENUx overlay exist only while it is resident.
  The host refuses (a fault, then the game's fallback) unless the model says
  `loaded`.
- TITLE.PRG has its own copies (`_drawSprt`, `_printCharacter`). Its
  primitives are a separate table, available only on the title.
- **Overrides are commands too.** A `d.text` in a non-game style (a size or
  layout ui_text alone can't do) emits the game's glyph SPRTs for the same
  codes, and marks them with a PC-only tag. The renderer and ui_text then
  set them string by string at the positions Lua chose (§2.5). In parity
  mode that tag is what the waiver names.

### 2.5 Typography moves into Lua

- **Today:** ui_text's styles (settings `ui_text_<style>_<field>`) plus its
  heuristics in C:
  - `digit_scale`, `outline_room`, `DIGITS_ASPECT`, the descender lift;
  - the status words' `status_layout`;
  - bubble centring.

  `rood.hud.typography` overrides fields for a run.
- **Target:** ui_text keeps the mechanism, Lua owns the decisions.
  - **Mechanism (C):** glyph recognition, SDF fonts, the CLUT ramp mapping,
    `ui_text_layout line`.
  - **Decisions (Lua):** per element, the style plus layout parameters as
    a Lua table, handed to ui_text with the packets as a per-range override
    (`pc_ui_text_override` keyed by packet range, not only by style). For
    example: digits' aspect, lift, margin and width policy; words' gap
    collapse and edge clamp.
  - The playtest fixes `1d2f20c5` and `645a7477` become Lua defaults in
    `rood.classic`'s look table (e.g. `look.digits = { aspect = 0.78, lift
    = …, keep_figures = true }`). The C constants become fallback defaults
    only.
- The settings keys stay. They are what the Option pages and the dev tools
  edit. Lua reads them as rood.hud does today (`rood.settings.get`), and the
  table is rebuilt when they change.

### 2.6 Widescreen placement as Lua data

- **Each kind's look table has a `place` entry:**
  `{ anchor = "left" | "right" | "centre" | "fill", dx = 0, dy = 0 }`, in
  game pixels of the 4:3 screen.
  - The default reproduces today's placement exactly, so parity holds.
  - A 16:9 tweak is a non-zero `dx` or a different anchor, listed as a
    placement override.
- **A range Lua emitted is tagged `PC_ANCHOR_EXACT`** (a new anchor kind,
  `0x16`). It gets no `primitive_extend`, no `side_paint` and no cover
  heuristics: the range is scaled about its anchor and that's all.
- The heuristic stretch is removed per screen as each screen moves to Lua
  (open question 3). It stays the fallback for screens still drawn by the
  game, until the last one moves. Then `primitive_extend` and `side_paint`
  are deleted.
- **Menu bars that should reach the 16:9 edge** (the reason for
  `primitive_extend`) are drawn by Lua explicitly. Example:
  `d.anchor("left")` and a wider `w` on the bar's gradient, as a listed
  placement override of that screen.

### 2.7 Fallback

- No claim, Lua off, the plugin disabled or broken, a draw that raises, the
  budget exceeded, or an overlay not resident: **the game's own drawing**,
  as now.
- The hook decides before emitting. If a Lua draw fails part-way, its
  emitted packets are rewound (the scratch pointer is restored to `first`)
  and the game's are left live.
- Our own pages (MENU8's PC entries) have no game drawing. Their fallback
  stays `classic_options.c` (C), the way rood.options has a C++ page model
  fallback.

### 2.8 Where the code goes

- `pc/src/classic/`:
  - `classic_hook.h` (the `PC_CLASSIC` macro);
  - `classic_prims.c` (the `d` primitives over game routines, out of
    `hud_bridge.c`);
  - `menu_model.c`;
  - `classic_parity.c` (§3).
- `pc/src/lua/lua_api_classic.cpp`: claims for classic kinds,
  `rood.game.menu()`, `rood.classic.*`. It reuses `lua_api_hud.cpp`'s style
  handles and command buffer.
- **The classic command buffer.** Today it is 256 commands / 4 KB, enough
  for HUD words. A menu frame needs more (40 rows × label glyphs), so it
  becomes per-frame arenas sized from measured worst cases (§6).
- `pc/plugins/rood.classic` (shipped, on): a look table and draw functions
  per kind and screen.
  - `rood.hud` keeps the modern look.
  - Both may share helpers through `rood.registry` or an exported module.
  - Alternative: one plugin with two skins (open question 6).

## 3. The parity gate

By default a Lua classic draw must emit exactly what the game emits for the
same frame. The gate is automatic, per element and per screen, and runs in
CI like the Zig oracle.

### 3.1 Where it hooks

- `PC_CLASSIC` in parity mode (RPC `classic {parity on}` or
  `ROOD_CLASSIC_PARITY=1`, dev mode only) runs both draws on the same
  model:
  1. The model is captured **before** the game's draw, so both sides see
     the same inputs.
  2. The game's draw runs for real, and its packets stay live.
  3. Lua's draw runs with the scratch pointer redirected to a capture arena
     outside the frame's primitive buffer. Its OT links go to a shadow OT,
     so nothing it emits is drawn.
  4. The two ranges are normalised and compared.
- Nothing Lua does in parity mode reaches the screen or the game's state.

### 3.2 What is compared

- **Packets, walked in OT order within the hook's slot(s), normalised:**
  - code and length;
  - vertices (x, y);
  - uv, clut and tpage;
  - colours and the semi-transparency bit;
  - DR_TPAGE/DR_MODE settings, in order.
- Link pointers and absolute addresses are dropped. Only order is kept.
- **Text normalisation.** Glyph SPRTs compare as (font page, glyph code
  from uv/clut, x, y, colour). This is before ui_text, which runs at
  raster time and never changes packets. The typography upgrade is
  therefore outside the gate by construction.
- Lua's PC-tagged override commands (§2.4) compare as their underlying
  glyph codes and positions. A difference is then allowed only if that
  element's waiver names it.
- **Waivers:** `pc/tests/classic/waivers.toml`, the pattern of
  `pc/tests/diff/waivers.toml`. One per kind or screen, for each field and
  each declared override. Example:

  ```toml
  [status.typography]
  reason = "status words set by rood.classic look.words"
  ```

  The plugin's look table declares its overrides (`look.overrides = {...}`),
  and the harness checks that the declared set equals the waived set.

### 3.3 How frames are driven

- **HUD:**
  - The bench: `rood.dev.show` holds each kind's moment (status timings,
    chains 1-N, buffs per slot and stat, damage per pool).
  - The battle corpus recipes (`pc/tests/diff/scenarios.py`) for the
    status panel, actor words and text boxes over real fights.
  - Every presented frame of the run compares.
- **Menus:** recorded **menu tours**, one per screen. A pad recording
  (Oracle `record`/`replay`) from a pinned save state visits every row,
  page, flyout and confirm, and returns.
  - Kept under `pc/tests/classic/tours/` as Lua tables of pad events per
    vsync, not new formats.
  - Played back with RPC `pause` / `step`.
- **The snapshot gallery** for moments that are hard to reach: the save
  confirm, Game Over, the title card screen, results screens.
  - Entries for tests live under `ROOD_BENCH=pc/build/…`, never
    `pc/user/bench`.
- **The null renderer lane.** Parity compares packets, not pixels, so it
  runs without a GPU, in parallel, and on farm machines.

### 3.4 State parity

The same tour runs twice from one mark: Lua classic on, then off. Oracle
page digests (`digest record/compare`) are taken at each vsync. Excluded:
the primitive buffers, the OT, and the scratch pointer.

This proves the Lua path changes no game state: animation steps, cursors,
sounds and card bytes. It runs once per kind or screen, at its flip.

### 3.4a Immediate draws (2026-10-30, the worker's calls)

Some screens draw at once (`DrawPrim`, `LoadImage`, `StoreImage`) instead of through the ordering table: MENU7's
file menu (`_drawFileMenu`, `_drawFileMenuElement`). An emit-only range cannot claim them: there are no packets
left in the frame's buffer to walk or suppress, they are already drawn. The design chosen:

- **A range around the call, as `PC_CLASSIC` is** (`PC_CLASSIC_IMMEDIATE`, `classic_hook.h`): `begin(kind)`,
  the statement, `site(kind, record)`. The statement always runs (emit-only: whatever state it steps is stepped).
- **The recorder sits in the PC's libgpu** (`boot_sdk.c`'s `DrawPrim`, `LoadImage`, `StoreImage` call weak hooks
  into `classic_parity.c`): every call inside an open range is recorded in order, a packet as its words (the same
  normalised form as an ordering-table packet's), an upload as GP0's own copy command `{0xA0000000, xy, wh, a hash
  of its pixels}`, a read as `{0xC0000000, xy, wh}`. That list is the kind's capture and is compared by the same
  `pc_classic_compare`; so the CLUT swaps the location pictures depend on are part of parity, not around it.
- **Held back, not undone.** When the range opens the bridge asks whether a claim on the kind is honoured (the same
  test `pc_hud_record` makes); if so the recorder keeps the draws and uploads (copies of their words and pixels)
  instead of doing them. At `site` the claim draws (into the arena past `vs_scratch.unk0`, linked to no table,
  each packet `pc_draw_packet`ed as it is made, in the order made, the arena given back) and the held ones are
  dropped; with no claim, or one that fails, they are done after all, in their order. Reads are always done (the
  game reads the pixels). More than the recorder can hold (384 entries, 16 KB of pixels) gives up holding for the
  rest of the range and the game draws that frame. In the parity mode nothing is held.
- **The OT path is untouched**: `classic_packets` and its walks are as they were; the command switch is shared
  (`emit_command`), the immediate executor (`immediate_packets`) runs the commands first to last (nothing to
  reverse) and adds three commands: `d.image_store` / `d.image_load` / `d.image_restore` (VRAM transfers done in
  their place among the packets) and `d.text_glyph` (the next packet reported to ui_text as a printer's glyph:
  MENU7's printer reports its own with `pc_text_glyph`, so the upgraded font sets Lua's the same).
- **Records filled by the game side**, before and after the statement as the draw reads them (the blend factors,
  the hand's saturation step, the printer's blink step before; places after the slide), the words as codes, the
  two location CLUTs as data (`d.image_load`'s pixels): no game text or art in the plugin.
- **Not held for**: the modern Data screen (MENU7's own gate, `_pcNoDraw`, runs first), the game over's file menu
  (not toured), any screen but currentMenu 7.
- Known limit: a fallback that replays held glyphs (a failing draw, a frame the recorder gave up on) replays them
  from copies, so ui_text's line layout misses them for that frame (their glyphs placed cell by cell).

### 3.4b MENUF (2026-10-31, the worker's calls)

MENUF (`vs_menuF_exec`, currentMenu 15) is not drawn through BATTLE's sprite list: 146C.c `func_8007DFF0` /
`func_8007E0A8` only reserve and free VRAM slots 29..31, and `func_8007DD50` .. `func_8007DE5C` set BATTLE's
countdown effect (the time attack's start), which BATTLE draws, not MENUF. What MENUF draws, it draws through the
ordering table from a few leaves (`_renderTextureFadeIn`, `_renderTextureFadeInClut`, `src/renderTextures.h`'s tint,
pop-in and wipe, `_renderAnimatedStatWheel`, the stat wheel's two `DR_AREA`s, `_renderPressButtonPrompt` with the raw
printer); and it uploads at once (`LoadImage`: `_initMenu`'s TIMs and CLUT, the end screens' pulsed CLUT row). No
146C.c function is hooked (none of their ports is touched); no MENUF function has a Zig port.

- **`menuf`: one ordering-table kind, a frame's calls as its record.** A `PC_CLASSIC` emit-only range around
  `_execMenu` (`_pcExecMenu`); each leaf records its call on entry, before its own tests (`PC_MENUF_CALL`: an op, the
  `_disMap` entry's texels, page and CLUT as C reads them, the place, the step / CLUT / wipe edge and the two colour
  words; the prompt's words as the game's codes). The screens' logic (timers, states, counting, layout, which
  picture) stays the game's; the leaves' drawing (what a step means, the colour ramps, the wipe's strips, the
  wheel's faded lines, the prompt's slide and bob) is the look's, in Lua. One element a frame (at most 128 calls;
  more: the game draws that frame).
- **`menuf_load`: an immediate kind for the uploads.** `PC_CLASSIC_IMMEDIATE` ranges around `_initMenu` and
  `_execMenu`; the recorder keeps the uploads' pixels for this kind (`pc_classic_immediate_keep`, a 160 KB pool:
  three TIMs of 32,640 bytes) and `d.image_load` takes the record's handle for them. So the claim makes the uploads
  (where, in which order: its call) with the game's own pixels (no art in the plugin), and the gate compares each by
  rectangle and hash as for the file menu's CLUTs; a held TIM is held, not done twice.
- **Not done**: the ENDING's copy of `src/renderTextures.h` (its hook is the empty default).

### 3.5 Reporting

- `pc/tests/run.py classic_parity` (per kind and screen; `--kind`,
  `--screen`) writes JSON `pc/build/classic_parity/<kind>.json` with:
  - frames compared and mismatches;
  - the first differing packet (index, field, game vs Lua words, the
    model row or part it came from);
  - waived fields;
  - coverage (which rows, states and parts the tour reached).
- RPC `classic {parity}` gives the live counters, so an agent in the REPL
  sees "status_panel: frame 1204, packet 7, x 44 vs 45".
- **The gate:** a kind becomes the default (§2.1) only with zero
  unwaived mismatches, state parity green, and coverage of every part.
- **Secondary check:** rendered-pixel goldens (`golden_frames.py`) of the
  gallery entries, with ui_text original and font. They catch renderer and
  typography regressions the packet gate can't see. They are not the gate.

## 4. Phases

Sizes are agent-hours of implementation and verification, sequential.

### Phase 0: groundwork (≈ 6-8 h)

1. Merge `playtest-fixes` first, so its four fixes are the baseline (and
   later move into Lua).
2. Packet capture:
   - an OT-range walker and normaliser (`classic_parity.c`);
   - RPC `packets {kind | range}`;
   - a ctest probe on synthetic packets.
3. Confirm the unknowns:
   - label kind 2's words;
   - the `D_800EB9CD` sprite (BATTLE MODE?);
   - area title cards;
   - Ending.

   Write them into this plan's inventory.
4. The `PC_CLASSIC` macro and the emit-only suppression generalised from
   `pc_hud_element`. Commit-check.

**Done when:** RPC `packets` dumps the status panel's range normalised, and
a probe round-trips.

### Phase 1: the classic HUD (≈ 24-32 h)

Ordered for the fastest visible improvement on the user's classic look.

1. **Classic primitives on game routines** (§2.4): `d.window`, `rect`,
   `sprite`, `glyph`, `cursor` stop hand-writing packets; new `d.value`,
   `d.text_raw`, `d.tile`, `layer`, `anchor`. (4 h)
2. **Parity harness for HUD kinds** on the bench: status, chain, buff,
   damage. `classic_parity` suite, waivers, JSON report. (5 h)
3. **The `rood.classic` plugin: status words and chains** as exact copies
   of the game's draw.
   - Pass parity; flip the per-kind default on.
   - Then the timing-word typography (`645a7477`'s gap and edge clamp) as
     Lua look values with a declared override.
   - **First visible result:** the timing word is tuned live in the REPL on
     a held SLOW!. (3 h)
4. **`status_panel`:**
   - hook `func_800CA2DC` (emit-only suppression; logic untouched);
   - parts: hp/mp/risk bars, digits, labels, RISK ZERO, limbs, status
     icons;
   - parity on corpus fights (HP changes, risk 0 and >0, the slide when a
     menu opens);
   - then the digits' typography as Lua (`1d2f20c5`'s policy as look
     values) and its 16:9 `place` (anchor left, an option).
   - **Second visible result.** (6 h)
5. **Buff and damage** in rood.classic. Parity; damage digits share the
   digits look. (2 h)
6. **Text boxes and the name window:**
   - `func_800CB83C` per box id;
   - the private OT `D_800F51B8` handled as the hook's slot;
   - dialogue line layout parameters as Lua (bubble align, line mode). (6 h)
7. **The remaining HUD kinds:** actor words, timer, ability icons, emote
   balloons (their `ui_emotes.c` redraw is modern-ish: classic keeps the
   game's sprites), the BATTLE MODE sprite. (3 h)
8. **Targeting:** shapes stay the game's (3D); rows, leader lines, marker
   and part details as kinds. (4 h)

**Done when:**
- every HUD kind is green in `classic_parity` (bench + corpus) and in state
  parity;
- each is drawn by Lua by default in the classic look;
- the game's fallback is tested (plugin off, a raising draw, Lua off);
- the per-frame budget holds (§6);
- the two playtest typography fixes live in Lua (their C constants are
  defaults only);
- Extensions.md "Classic look in Lua" is written.

### Phase 2: one full menu end to end (≈ 18-24 h)

**Recommended: the shared MAINMENU frame plus the Options screen (MENU8),
with our pages.**
- The frame is shared by every later screen, so it is built once.
- Options is small (9 rows plus flyouts).
- It has an existing PC renderer whose look values (`option_style`) move
  to Lua.
- It carries today's help line-break bug.
- Alternative: Items (MENU3), most used but four times larger (open
  question 5).

Steps:
1. The menu model `rood.game.menu()`, 58EC.c accessors, model tests against
   RPC `options`. (4 h)
2. Split `_renderMenuItems` (§2.2b) and hook:
   - `vs_mainMenu_renderScreen`'s pieces: background, the command title,
     rows, cursor, the information box (text box 7), tab navigation;
   - `vs_mainMenu_renderItemsList`.

   Commit-check; state digest. (6 h)
3. rood.classic menu frame and MENU8: rows via `d.menu_item`, the flyout,
   our pages' rows drawn from the page model (`rood.menu.rows`) with the
   same primitives. `classic_options.c` keeps its input and navigation and
   stays the fallback renderer. (5 h)
4. Tours: Options rows, every flyout, our pages, Keep/Revert, rename
   excluded. Parity plus state parity; the 16:9 help and placement as Lua
   data; `primitive_extend` and `side_paint` off for these ranges. (5 h)

**Done when:**
- the Options screen is drawn by Lua by default, parity green;
- the help's line breaks and width are Lua values;
- `option_style.c`'s values exist only as fallback defaults;
- the save `vs_main_settings` bytes test is unchanged.

### Phase 3: the rest, screen by screen (≈ 60-80 h)

Each screen follows the same steps: hooks (emit-only where possible), model
extras, a tour, parity, state parity, flip, its heuristics off. In order of
use:

| # | Screen | Size |
|---|---|---|
| 1 | Items MENU3 (lists, status views, equip, discard) | 10 h |
| 2 | Status / equipment MENU4 (the 3D actor stays the game's) | 8 h |
| 3 | Data / save / load / Game Over MENU7, then TITLE's card screens. The YES/NO bug's area; its own printer `_printCharacter` | 10 h |
| 4 | Ring / command menu (`func_800C97BC`, `vs_battle_drawCursor`) | 4 h |
| 5 | Magick MENU0, Break Arts MENU1, Battle Abilities MENU2 (shared list patterns) | 8 h |
| 6 | Loot MENUB, Container MENUD (reuse Items' list) | 6 h |
| 7 | Workshop MENUC | 6 h |
| 8 | Map MENU5 (frame, labels, controls, compass; the map stays the game's) | 5 h |
| 9 | Score MENU9, Quick Manual MENUE | 6 h |
| 10 | Rename (MENU8 21A0.c), results MENUF, title menu, Ending | 6 h |

**Done when:**
- every row of the inventory reads "Lua, parity green";
- `primitive_extend` and `side_paint` are deleted from `gpu_hw.c`;
- the look lint has no baseline entries left for classic UI files.

### Phase 4 (later): the Zig tie-in

- When a menu subsystem is ported (plan D/E), its logic is owned, so
  drawing and logic separate naturally. The owned code fills
  `rood.game.menu()` from native data, and the `PC_CLASSIC` hooks in src/
  go at the flip.
- **Modern and classic become two Lua skins over one model.**
  - The modern menus (paused, branch `claude/musing-haslett-8e55eb`) are
    rebuilt as a plugin on the same model.
  - The bridges' snapshots (`*_bridge.c`) become model fillers.
- Once liberation (E) drops the OT, the classic primitives are
  re-implemented natively over the same `d` API. The parity gate still
  compares the native emit with recorded packet streams of the original
  until the classic skin is frozen.

## 5. The look lint (F5)

"No new look literal in pc/src UI code."

- **Tool:** `pc/tools/look_lint.py`, run by `verify.sh` and as a ctest.
- **Scope:**
  - `pc/src/ui/`, `pc/src/hud/`, `pc/src/classic/`;
  - `ui_text.c`, `fast_travel.c`, `dev_overlay.cpp`'s HUD parts.
- **Flags:**
  - numeric literals in calls to draw APIs: ImGui draw lists, `d`-command
    builders, `vs_*render*` calls from pc code, `pc_ui_draw_*`;
  - colour hex (`0x[0-9A-Fa-f]{6,8}` outside packet code words);
  - font paths;
  - floats in `*_draw.cpp` / `*_style.c` initialisers.
- **Exempt, by marker:**
  - packet encodings in primitive builders (`/* gpu */` on the line, or
    files listed as mechanism: `classic_prims.c`, `hud_bridge.c`'s packet
    helpers);
  - fallback defaults that mirror a setting or a Lua value
    (`/* look-default: <key> */`, checked: the key must exist in the
    settings catalog or a shipped plugin's look table).
- **Rollout:**
  1. Phase 0: report only. Generate `pc/tools/look_lint_baseline.txt`
     (counts per file and function) from today's tree.
  2. From phase 1: fail on any **new** literal. The diff against the merge
     base must not raise a file's count. The baseline may only shrink.
  3. Each phase deletes the baseline lines of the files it moved to Lua.
     The modern-menu files' entries are cleared when that plugin is
     rebuilt.
  4. When the baseline for classic files is empty, the lint becomes
     strict for them.

## 6. Risks

- **Performance per frame.**
  - Today's classic claims cost 0.009 ms per game frame for HUD words. A
    menu frame is ~40 rows × up to 24 glyphs, plus panels: perhaps 1-2k
    primitive calls from Lua.
  - Budget: 0.5 ms of Lua per game frame for a menu, 0.1 ms for the HUD.
    Measure in phase 2 step 3 before building the rest.
  - Mitigations:
    - batch commands: one Lua call emits a row (`d.menu_item`), not
      glyphs;
    - reuse model tables (already the pattern);
    - no strings per frame.
  - `plugin_budget_ms` stays the hard ceiling; past it, the fallback.
- **Packet budget and the OT.**
  - The frame's primitive buffer (`D_8005E0C0[frameBuf]`) is fixed. By
    default Lua emits exactly as much as the game.
  - An override that adds packets must fit. The host checks the remaining
    bytes before emitting and falls back otherwise.
  - The OT slots must match the game's (`unk4`/`unk8` offsets,
    `setBackgroundRenderPriority`, the text boxes' private OT spliced with
    `AddPrims`), or draw order changes. Parity compares order within the
    slot; a `d.layer` mistake shows as a mismatch.
- **Overlay loading.**
  - MAINMENU and MENUx routines exist only while loaded
    (`vs_battle_loadMenuPrg` is asynchronous).
  - The host checks residency before any call. The model carries `loaded`.
  - TITLE has its own primitive copies.
  - A draw during a load transition falls back.
- **Input timing.** Lua never reads the pad for classic menus and never
  delays the game's logic. Any slowdown is a frame-time risk only, covered
  by the budget. State parity proves cursor and animation timing are
  unchanged.
- **Save safety.**
  - Draws are read-only copies; there is no write API in the classic
    flavour.
  - The memory card screens (MENU7, TITLE) keep the game's logic.
  - Tests compare `vs_main_settings` and card bytes before and after tours
    with Lua on and off.
- **Interleaved logic in draw functions.** Some (`_renderMenuItems`, label
  ageing, the HUD slide) need ROOD_PC restructures that commit-check cannot
  fully verify, because ROOD_PC code isn't in the PS1 build. The oracle
  state digest (§3.4) is the check.
- **ui_text coupling.** Overrides at packet-range granularity need ui_text
  to accept range-keyed parameters. Today it has style-wide overrides only.
  Unknown cost (estimate 3-4 h inside phase 1 step 3).
- **32-bit and Windows builds.** The hooks are C in src/ and must build on
  all four targets. The parity suite runs on 64-bit Linux first.
- **Unknowns:**
  - what label kind 2 shows;
  - the top-right sprite;
  - whether Ending has UI worth moving;
  - the real cost of menu tours (recording them is manual the first time:
    an agent drives RPC input).

## 7. Decisions (the user, 2026-10-20)

1. Parity: the packet gate is a given, not an option (pixels stay a secondary check on the gallery).
2. Small `ROOD_PC` hooks in src/MENU now: yes.
3. The heuristic widescreen stretch is removed per screen as each moves to Lua: yes.
4. A kind or screen's Lua draw becomes the classic default once it passes the parity gate: yes.
5. Phase 2's screen: left to us. The main-menu frame plus Options (the default below).
6. One plugin or two: left to us. A new shipped `rood.classic` (the default below).

The questions as they were put:

### Open questions (answered above)

The plan is written with the recommended default for each.

1. **Parity target.**
   - *Default:* packets are the gate: fast, exact, per element, on the null
     renderer. Text is normalised to glyph codes and positions before the
     ui_text restyle. Rendered-pixel goldens are only a secondary check on
     the gallery.
   - *Alternative:* pixels as the gate. This catches renderer differences
     too, but needs the GPU (serial, slower), makes typography a pixel
     waiver everywhere, and gives worse diagnostics (where, not which
     packet).
2. **Hooks in src/MENU now.**
   - *Default:* small `ROOD_PC` hooks are acceptable now (precedent: MENU8's
     Option pages hooks). Each is minimal and commit-checked. The menu model
     is designed so the Zig port replaces them later.
   - *Alternative:* wait for the Zig port to own the menus. There are no
     src changes then, but nothing happens for menus until plan D/E reaches
     them (months), and the playtest bugs stay C work.
3. **The heuristic widescreen stretch.**
   - *Default:* remove `primitive_extend`/`side_paint` per screen as each
     moves to Lua (placement becomes Lua data there). Keep them as the
     fallback elsewhere until the last screen moves.
   - *Alternative:* remove them now. That is simpler, but the screens not
     yet moved lose their bars' 16:9 extension in the meantime.
   - *Or:* keep them forever as the fallback. That is safer, but it keeps
     the class of bug that split the YES/NO rows.
4. **Default on.**
   - *Default:* once a kind or screen passes the parity gate, its Lua draw
     is the default for the classic look (`hud_classic_override` on per
     kind). The game's own drawing remains the automatic fallback.
   - *Alternative:* keep the override opt-in. That is the safest, but
     typography tweaks in Lua then don't reach the user's normal play.
5. **Phase 2's screen.**
   - *Default:* the shared MAINMENU frame plus Options (small, has the help
     bug, an existing PC renderer to retire).
   - *Alternative:* Items (MENU3). Most used and the biggest payoff, but
     about four times the work for the first end-to-end proof.
6. **One plugin or two.**
   - *Default:* a new shipped `rood.classic` for the classic skin, beside
     `rood.hud` (modern).
   - *Alternative:* fold both into `rood.hud` as two skins now. One place to
     edit, but a bigger file, and it couples the classic skin to the paused
     modern work.
