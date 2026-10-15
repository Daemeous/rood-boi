# Modern Menus — Phase 7c: the native Quick manual

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** System > Game > Quick manual opens a native reader of the game's own manual (MENUE): the same 14 topics, their pages, text colours, button pictographs, illustrations and rules, read from the disc's page files at run time and drawn in the modern menus' language (a topic rail, a card with the page canvas, a page row, a scroll bar), pad first with the mouse (topics, wheel, page chevrons, the track), 4:3 and 16:9. Square on the row (or on the topics) still opens the original MENUE. With `ui_menus` off nothing changes: the original top level's QUICK MANUAL row leads to MENUE as before.

**Architecture:** no src/ edit (494.c and C48.c byte-identical). The manual is a sub-screen of the System tab, hosted like the Workshop in the Items tab: its own pure module, its own event outbox to its own bridge.

- `pc/src/ui/manual_parse.h/.c`: pure C. HF0 / HF1 parsing (bounds-checked), the line tokenizer (runs placed at the original's x), the sprite compositor (8bpp / 4bpp + CLUT lines to RGBA), and the original's rules as functions: `_getRoundedLineCount`, `_setMenuRows` (14 rows), MAINMENU 58EC.c's list step, 494.c's page step, `_getAnimationState`.
- `pc/src/ui/ui_manual.h/.cpp`: pure state (mode, topic, the list window = cursorMemories[4] / [5], a line per topic), tick, pointer, JSON (`ui_menu` `system.manual`), the event outbox (OPEN, LIST_STORE, SCROLL, SOUND, CLOSE, ORIGINAL).
- `pc/src/ui/manual_bridge.c` (battle builds): the compiled-in menuText (names, descriptions), the disc cache (HELPnn.HF0 / .HF1 parsed once, kept), decoded word text, the animations of the shown topic, the events applied to the game (display state, cursor memories, world freeze, sounds).
- `pc/src/ui/ui_manual_draw.cpp`: the rail, the card, the canvas (word-anchored text, prompt glyphs, illustrations as nearest-scaled RGBA textures, rules), fades, the page row, the scroll bar, hints.
- `pc/src/ui/ui_shell.cpp`: the System row enters the manual; Square on it returns 10; Cross / Triangle / tabs / close through `sys_leave`; pointer kinds 220-223; `system_json` gains `"manual"`.
- `pc/src/ui/menu_bridge.c`: fill before the shell tick, refresh after it, abandon and reset.

**Ground rules:** branch `claude/musing-haslett-8e55eb`, merge main first; no src/ edits (`make commit-check` "All files match" at every commit); three builds (32, 64, Windows) + decl check + ctest; tests through RPC only (`launch_at('save_room')`, `--out` for artefacts); no game data, decoded text, sprites, screenshots, personal paths, emails or other-project names in commits (the menuText table is compiled in from build/assets by `#include`, as the other bridges do); never touch button_prompts*.c, prompt_glyphs.cpp, ui_text.c, ui_labels.h, atmosphere / fog / haze files or the present chains (`pc_prompt_glyph`, `pc_ui_text_decode` and `pc_ui_disc_read` are only called).

## Facts (research brief)

**One entry.** Top-level command 9 ("QUICK MANUAL", `func_800C930C`, BATTLE 5BF94.c) gives selectedMenu 10, `submenuEntrypoints[9]` = `vs_menuE_exec` (MAINMENU C48.c), gated by `menuFlags` bit 9. Not a direct entry; no other caller. The shell row is `{"manual", "Quick manual", ..., ROW_COMMAND, 9}` (System > Game), delivered as r = 10 until this phase.

**Exits (494.c).** Cross on the list: result 1, `func_800FAA5C` back to the top level (the shell reopens on System / manual). Triangle on the list or a page: result 2, `currentMenu = 10`, the menu closes. Circle or Cross on a page: back to the list (`_topMenu(1)` re-seeds the window).

**Sounds.** All `vs_main_playSfxDefault(0x7E, n)`: change 4, select 5, leave 6 (= `vs_battle_playMenu{Change,Select,Leave}Sfx`). The list: change when row + page changes, select on Circle, leave on Cross or Triangle. The page: change on every line or page step and wrap; Start none; leaving plays select if Circle was pressed that frame, else leave.

**The page step (494.c showSubMenu).** `exitBack = pressed & (Circle | Cross)`, `exitClose = pressed & Triangle`. Start: scroll 0. Else Square held (`vs_main_buttonsPreviousState & PADRleft`, the held state): Up pressed on page 0 with lastPage > 0 → lastPage; Down pressed on lastPage with lastPage > 0 → 0; Up / Down repeat ±1 page (scroll = page * 10). Else lines: Up pressed at 0 → lineCount; Down pressed at lineCount → 0; repeat ±1. `scrollPositions[currentManual] = scroll` every frame, the exit frame included. `lineCount = max(0, roundup10(n) - 10)`, `lastPage = lineCount / 10`, the label "#page+1/#lastPage+1".

**The list (58EC.c, 14 rows, rowInfo 1).** Nine rows shown, scroll 0..5. Seed (`_setMenuRows`): row < 8 → (row, 0), else (row - 5, 5); `vs_mainmenu_setMenuRows`' normalisation leaves both. Up: page 0: row 0 wraps on a press only to (8, 5), else row - 1; page > 0: row 1 → page - 1, else row - 1. Down mirrors it (page 5, row 8: a press wraps to (0, 0); row 7 → page + 1). Up then Down in one frame. Circle / Cross / Triangle store `cursorMemories[4] = row`, `[5] = page`; MENUE then always writes `currentManual = cm4 + cm5` (cancel too).

**State.** `_initMenuState` zeroes `vs_battle_manualDisplayState` (0x24 bytes: currentManual, scrollPositions[14]) when `vs_main_settings.cursorMemory == 0`. World freeze `func_8008A4DC(0)` after the opening delay, `(1)` on exit (`D_800F190C`). HUD `func_800CB654(1)`, `D_800EB9B0 = 0x200000` (the shell's `hide_hud` covers the slide-out). VRAM tpages 29-31 (`func_8007DFF0/E0A8(0x1D, 3, 5)`), text box 0, information box 7, the background fade and the HF heap buffers: never touched natively.

**Files.** `SMALL/HELPnn.HF0` / `.HF1` (nn = topic + 1). Format: tools/data/SMALL/HELP.py, tools/kaitai/ksy/data/SMALL/help_hf0.ksy / help_hf1.ksy, 494.c `_copySprites`, `_copyCluts`, `_drawSprite`, `_drawContentLines`, `_copyNextPageText`.

**The dump (pc/tools/manual_dump.py, stdout only).** Every topic's lines are a multiple of 10 (1650 lines in all). The codes in the lines are FA (x advance) and FB only; FB operands 0, 1, 2 (colours; no font table switch, no justify, no y offset). Pictographs: the face buttons △ ○ × □ (3 lines in all) and the star (58); the arrows ← → ↑ ↓ appear in tables and examples as relation arrows (between two words, or a sequence of moves), not as D-pad buttons, so they are drawn as arrows, not prompts. Every sprite is 8bpp with clut_x 0 (4bpp is supported, untested by data). Animated sprites only in HELP12 (2) and HELP13 (1); no animated rules.

## Spec (as built)

### Hooks (one src/ edit: the sound count in main.c, for the smoke)
- `ui_shell.cpp`: `sys_list_confirm` on the "manual" row calls `pc_ui_manual_enter()` and returns 0 (the shell stays); `system_tick` hands the tick to `pc_ui_manual_tick` while the manual is on (a 10 from Square: `sys_leave`, hidden, result 10) and, on the System list's manual row, Square returns 10 after `pc_ui_manual_original()`; `pc_ui_shell_tick`: Cross (not with Triangle) calls `pc_ui_manual_back(in)`, Triangle first runs `pc_ui_manual_exit_step(in)`; `sys_leave` calls `pc_ui_manual_leave()` first (tab switch, close, Triangle, an original screen, `pc_ui_shell_close`), `pc_ui_shell_open` too (a manual left open by a session that ended without its close); pointer kinds 220-223 to `pc_ui_manual_mouse_focus` / `_click` (a manual click is the tick's whole action); `system_json` appends `"manual"`; `pc_ui_shell_manual_shown()`.
- `ui_system_draw.cpp`: `draw_system` hands the screen to `draw_manual` while the manual shows; the manual row's card says "Read here, or on the game's own screen" with Circle Read / Square Original screen.
- `menu_bridge.c`: `pc_ui_manual_bridge_fill(settle > 0)` before the shell tick, `pc_ui_manual_bridge_refresh()` right after it (before any delivery), `pc_ui_manual_bridge_abandon()` in `abandon()` after `pc_ui_shell_close()`, `pc_ui_manual_bridge_reset()` first in `pc_ui_menu_reset()`.
- Build: `manual_bridge.c` with the game's units; `manual_parse.c` and `manual_cache.c` as host units (their malloc'd cache must not sit in the game units' snapshot sections: a save state restored in another process would hold stale pointers); `ui_manual.cpp`, `ui_manual_draw.cpp` with the ImGui units; the probe links `ui_manual.cpp` and `manual_parse.c`, and an ASan / UBSan twin `ui_shell_probe_asan` where the toolchain has it.

### Lifecycle
- off → topics: `pc_ui_manual_enter` (OPEN {zero = no cursor memory}: the bridge zeroes the display state, freezes the world, sets `vs_battle_submenuStates[0] = 9`, plays select; then LIST_STORE of the seeded window).
- topics → reading: Circle / Right / a click (LIST_STORE, select). reading → topics: Circle (select) or Cross (leave), the frame's step first, SCROLL, the window re-seeded (LIST_STORE, as `_topMenu(1)` writes it).
- topics → off: Cross (LIST_STORE, leave, CLOSE: `func_8008A4DC(1)`, as MENUE's exit). Any other way out: `pc_ui_manual_leave` (LIST_STORE or SCROLL, CLOSE; then ORIGINAL when Square asked for MENUE).
- abandon: the shell's close leaves the manual; the bridge drains (the freeze back). Reset: forget (no event, no game write), the drawing's textures rebuilt (generation).
- The page files load on the first showing of a topic (fill and refresh), synchronously, and stay; `loaded` (-1 / 0 / 1) is published only after the parse; models never change once published.

### Layout (layout units)
- Rail: x 56, width 236 (4:3) / 300 (16:9); "Topics" at top + 4; rows of 36 from top + 24 (Alegreya 18); the focus band while the topics have the focus, the accent bar alone while reading (other rows in ash). At 4:3 the rail fades and the card slides over it while reading (`approach(focus_grow)`), so the canvas keeps k 2.6; no rail hits then.
- Card: from the rail's right + 28 to W - 56; title (Alegreya 25), the topic's line (`paragraph`, ash), a rule. Canvas: k = min(2.6, inner width / 300), 300k x 130k (one original page), centred; 16:9: 780 x 338 at x 414; 4:3 topics: k 1.8; 4:3 reading: k 2.6 at x 90.
- Text: Asap Condensed. A line's runs form segments: a run joins the one before it when the original's gap is at most `PC_UI_MANUAL_JOIN_PX` (9: a space, or a small FA advance used as one; touching runs touch), else it starts a segment at its original x (columns, room for an illustration). Size per topic in units of k: from 0.8 x 13 down to a floor of 0.64 x 13 so every segment ends 3 original pixels before the next (every topic fits at 0.8: `overflow` 0). Colours: f0 bone, f1 gold, f2 accent. Face buttons: `glyph()` 11k high (the player's device); the star a drawn five-point star in accent; arrows (relation arrows in tables) the font's ← → ↑ ↓.
- Illustrations: the shown topic's images in one 2048² RGBA atlas, pre-scaled by the integer ceil(k x window scale) with the reading layout's k (fixed per width, so the 4:3 slide only resamples on the GPU; nearest; smaller if they would not fit, packed aside so a topic that does not fit leaves the cached atlas whole), rebuilt when the topic, the scale or the bridge's generation changes; the PlayStation button art (HELP01's legend and its repeats in HELP07/08/10/13: face buttons, L1/L2/R1/R2, Start, Select, the D-pad; recognised by `pc_ui_manual_button_art`, a table of pixel hashes from `manual_dump.py --art`) is drawn as the device's prompts off a PlayStation pad (a key cap no wider than 1.5 x the sprite; the keyboard's D-pad as the four arrow keys in a cross, a pad's D-pad stays the game's pixels; an unbound key: the pixels); drawn at (x, y - 13 line) x k, skipped while their animation hides them. Rules: their own colour at 0.6 alpha, max(1, 0.5k) wide. 16-unit fades to the card at the edges with more content. The drawn scroll approaches the line (snaps on a new topic, the first frame, jumps over a page).
- Page row: a progress track (half the canvas) and "‹ n / m ›" (Chakra Petch 16, bone while Square is held), the chevrons PAGE -1 / +1; the scroll bar 2 units at x1 + 10 (thumb 10 / (line_count + 10)), its track a SCROLL hit mapping y to a line.
- Footer: "System > Quick manual > the focused topic" (topics and reading); topics: Circle Read, Square Original screen, Cross Back, Triangle Exit menu; reading: Up/Down Scroll, Left/Right Page, Cross Back, Triangle Exit menu.

### Controls
As the final spec's table: the topics step with `pc_ui_manual_list_step` (Up then Down, wrap on a press), Circle / Right read, Square the original (result 10), Cross back to the System list on the manual row, Triangle closes. Reading: `pc_ui_manual_step` (Start first, Square held: pages, else lines; Left / Right as Square + Up / Down), written every tick (SCROLL), Circle back (select), Cross back (leave), Triangle closes; Square pages only. Pointer: hover walks the list's cursor (the window as Up / Down repeats leave it, a change sound), a topic click reads it (from reading: back, then that topic), the canvas reads the focused topic, chevrons ±1 page (no wrap), the track a line, the wheel a line a notch (repeats: no wrap), a right click is Cross; content clicks are ignored until the topic's files are read.

### RAM (parity)
`ui_manual_smoke`: after every step of the story the display state, D_800F4EE8, `vs_battle_submenuStates[0]`, the settings and `D_800F190C` equal the original path's (pad and pointer); after the closes also the menu state, the HUD flags, text box 0's state; the heap's largest free block is as large as before. No mask was needed. `--diff-all` at the story's end lists what else differs, none of it read back by the native path: guest heap contents and MAINMENU / MENUE statics (the original's list and page ran in A), VRAM 768-1023 x 224-287 and the CLUT rows 496-511 (MENUE's uploads and its fades), text boxes 0 / 7 transient fields, and what differs with any two runs (frame counters, RNG, music, the SPU).

## Deviations from the final spec
- The arrows (← → ↑ ↓) are relation arrows in the manual's tables and examples (the dump shows them between words), not D-pad buttons: drawn as the font's arrows, published as the arrow characters in `page_lines`.
- Words are not each anchored at their original x (the reader's font is far narrower than the game's proportional one, which left wide holes inside sentences); runs join into segments by the original's pixel gap and only segment starts are anchored. The fit is over segments; the base size is 0.8 x 13 (floor 0.64) instead of 0.82 / 0.70.
- CLOSE sets `func_8008A4DC(1)` as MENUE's exit does instead of restoring a saved value (the same value on every path the smoke checks; the original's own behaviour).
- `manual_parse.c` and the page cache (`manual_cache.c`, an added file) are host units; the bridge keeps only `owned` (see Hooks). `pc_ui_manual_parse` takes the width table's length too.
- The rules are drawn at 0.6 alpha (quieter than the original's full-strength lines), in their own colours.
- The pointer smoke uses RPC `ui_menu pointer` (the shell's hit tests), as the other menu smokes; there is no system-level mouse injection.
- The footer crumb is not checked from frames (not in the JSON); `--out` keeps the screenshots.

## Tasks

- [x] 1. Dump tool and this plan.
- [x] 2. manual_parse.c/.h; probe tests (synthetic files, the list and step ports against C++ copies of the original).
- [x] 3. ui_manual.cpp/.h, the shell hooks, the probe's shell tests.
- [x] 4. manual_bridge.c, manual_cache.c, menu_bridge.c hooks, build lists.
- [x] 5. ui_manual_draw.cpp, the draw_system dispatch.
- [x] 6. pc/tests/ui_manual_smoke.py, run.py (and CHECKPOINT_SUITES).
- [x] 7. Docs (Modern_Menus.md, README), review, merge request.

## Review notes
- Every topic fits at the base size at k 2.6 and 1.8 (`overflow` 0, `unknown` 0 for all 14).
- The drawn scroll and the 4:3 card slide are animated in real time: tests wait for them (k 2.6) rather than for vsyncs.
- The forced currentMenu write leaves the game's menu mid-run (as in the map smoke): it is the robustness section's last case.

## Review fixes
- Circle + Cross in one frame: Circle wins (58EC.c case 17 checks Circle first; 494.c plays select when Circle was pressed): `pc_ui_manual_back` reads the topic, or leaves the page with select.
- The world freeze is reconciled after every drain (`owned` is game state, saved with a snapshot; the manual's mode is host state): a snapshot resumed in another process, or loaded mid-manual, puts the freeze back in step. A non-shell open (`pc_ui_menu_open`) applies the closed manual's events at once.
- Runs' `off` / `len` are 32-bit (HF0 lines reach ~128 KiB); an allocation failure after the parse frees the topic; the fitted text size is redone when the prompts' generation or the bridge's changes.
- Sounds: `vs_main_playSfxDefault` counts its calls under `ROOD_PC` (RPC `sfx`); the smoke compares the sounds played with A's (pad, to "top") and with the manual's requests (pad and pointer). The pad story takes a "top" dump (native Cross out of the topics against MENUE's Cross to the top level) before Square; the steps' names must match; the walks are bounded; the canvas pixel check always runs.

## Follow-ups
- A pad's D-pad art (an outline D-pad icon per family would need a whole-pad glyph from the prompt atlas).
- PgUp / PgDn / Home keys; a scroll bar drag.
