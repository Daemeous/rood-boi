# Modern menus

A native replacement for the game's menus, drawn by the PC runtime, with
modern typography, layout and ergonomics. The game still decides every rule
and runs every action; the new layer only presents state and forwards
choices. Option `ui_menus original|modern` (default `original` until the
first screens are verified); the original menus stay complete and reachable
at all times. A modern HUD follows the menus (same layer, separate option).

Inspiration: Andrew Kuhar's [Vagrant Story UI Remaster](https://andrewkuhar.com/blog/the-phantom-pain/)
([Figma prototype](https://www.figma.com/proto/roPMrnFqsqd8lths3rJG6n/The-Phantom-Pain?node-id=429-804&starting-point-node-id=429%3A804)).
The layouts here are our own implementation; no assets from the prototype are used.

Status: phase 1 (shell, bridge, theme, drawing, RPC `ui_menu`, HUD hidden),
phase 2 (Equipment: Gear with compare, Quick swap from the bag, mouse;
equips byte-for-byte equal to the original menu's, `ui_equip_smoke`) and
phase 3 (Ashley: summary, live model turned by the right stick or a drag,
Square = the original Status; `ui_ashley_smoke`), phase 4 (System: settings
pages and the game's options; `ui_system_smoke`) and phase 5 (Abilities:
Chain / Defense mapping equal to the original's, Break Arts with locked arts
and kills to go, native casting, a cancelled target back in the shell;
`ui_abilities_smoke`) landed. Phase 6a landed: a target cancelled after a
cast from the shell (Break Arts, Magick) returns to the shell on the same
row, not to the original screen (one from the original screen still returns
there), and the native Magick tab (schools, spells with MP and description,
the LV picker sharing the original's level memory, greyed spells with their
reason, casting through the game's targeting) with Teleport (destinations
with their MP cost, the spirit paling; `ui_magick_smoke`). Phase 6b landed:
the native Items tab (categories, list with marks and quantities, details;
the original's actions per category: Use through the game's targeting with a
cancel back to the shell, Search, Discard with the quantity picker, Discard
all, Sort by the original's stats; Move: swap, misc stacks merge), its
changes and cursor memories equal to the original Items menu's
(`ui_items_smoke`). Phase 6 is complete.
Phase 7a: the native Loot screen (after a battle with drops and at a chest:
the game's direct entry 12, MENUB, handed to the shell by the
`pc_ui_direct_tick` hook in MAINMENU). Take, Take all, Leave, keys, the
difficulty filter, Organize (Disassemble for weapons and shields,
Consolidate, Organize inventory with MENUB's Items menu actions: Discard with
the quantity picker, Discard all, Sort, Move) leave the bag, its lists, the
key and chest flags, the loot result, the heap, the HUD / world flags and
the Items menu memories as MENUB leaves them (`ui_loot_smoke`, including the
save room's real chest through the field's own chest flow). Deliberate
differences: the shell slides the field HUD out while it shows (the flag is
given back at the end only while it is still the shell's); after Organize
inventory closes, D_800EB9B0 is left at 0x200000, also after the session
(the panel's own HUD restore writes 0, the shell's slide-out sets it again;
harmless: it is the HUD's slide offset, which slides back in from there); the Items menu opens
on the drop's category, not on the Manage page MENUB shows without cursor
memory; a Move of a misc stack onto another of the same item that fits in
one never loses items: the merge is the Items menu's (MENU3: the sum kept),
where MENUB's copy of the Items menu initialises the stack moved, the one
holding the sum, so the original keeps only the target's count
(`ui_loot_smoke` checks both sides of this one difference). A direct session that is not ticked for a
while stays as it is (only the game leaving its menu or a reset ends it).
Phase 7b: the Map frame. Leá Monde > Map still runs the game's own map screen
(MENU5): its pad, 3D map, markers and labels, sounds, VRAM, world freeze,
HUD flags and exit codes are the original's. With the shell the screen gets
native chrome around a cut-out of the picture: the tabs, a header (area,
"Ashley is here", the room with ‹ › and the rooms seen, a JAMMED badge and
the paling's line), a rail (compass, map completion, the visited areas with
the shown area's rooms nested, a legend in the game's marker colours) and
clickable prompts; MENU5's own controls help is not drawn. Circle's area
list is native (MAINMENU's list ported: the same rows, window, wrap and
cursor memories) inside MENU5's own state; the pointer (drag rotate, wheel
zoom, middle drag pan, area / room rows, prompts, tabs, right click back)
sends requests MENU5 applies at its own input point with its own statements
and clamps (the pad wins). Map controls (System > Controls): Modern (the
right stick turns and tilts, L2 / R2 alone zoom, the left stick pans) or
Original (the PlayStation's stick code on the right stick), framed or not
(`pc/src/ui/map_pad.c`; the plan's controls table). Map status, mapPaling, cursor memories, settings,
menu state, HUD flags, the scene buffer, the field camera and MENU5's VRAM
rects end equal to the original path's, and MENU5's own scene, room and
camera after a switch, rooms and a turn, by pad and by pointer
(`ui_map_smoke`). Plan: `pc/docs/plans/2026-10-14-modern-menus-map.md`.
Phase 7c: the Quick manual. System > Quick manual is native (MENUE never
runs for it): a rail of the 14 topics (the original list's window and its
cursor memories, wrapping on a press) and a card with the focused topic's
page: its text read from the disc's SMALL/HELPnn.HF0 at run time and set in
the menus' label font, each segment where the original placed it (the
original's column advances and wide gaps anchor; runs joined by a space
flow), the face-button pictographs as prompt glyphs, the illustrations as
the game's own pixels (HF1 blocks and CLUTs composited to textures, nearest
scaled; their PlayStation button art drawn as the device's prompts on
another device, recognised by pixel hash; a pad's D-pad stays the
original's) and its rules, blinking where the original blinks. Up / Down a line,
Square + Up / Down or Left / Right a page, Start the top, as MENUE; the
pointer clicks topics, page chevrons and the scroll track and wheels a line
a notch. Square opens the original MENUE on the same topic. The display
state (last topic, a line per topic), cursorMemories[4] / [5], the submenu
cursor and the world freeze equal the original path's, by pad and by
pointer; the sounds the game plays (counted at `vs_main_playSfxDefault`)
equal the original's by pad and the manual's requests by pad and pointer
(`ui_manual_smoke`). Plan:
`pc/docs/plans/2026-10-14-modern-menus-manual.md`.
Phase 7d: the Data screen. System > Data runs the game's own save / load
screen (MENU7, `vs_menu7_dataMenu`) unchanged; everything shown is native
(nothing of MENU7's own screen: no sprites, DATA title, play time, message
box or thumbnails). MENU7 keeps the pad, every decision and sound, its
cursor memories, all card I/O and every write to the settings and flags.
While a run is framed (latched at its state 3 when the shell armed it) three
things differ (260.c, ROOD_PC): its GPU and text output is gated (DrawPrim,
renderTextRaw, the information box, the text overlay's glyphs; its logic,
the elements' animation steps, the progress counter and the sounds run);
its Save / Load chooser (states 5 and 6, a MAINMENU row list) is the
bridge's list, in getSelectedRow() + 1's protocol; and a click is a goal
(OP, CARD, FILE, MOVE (the wheel), ANSWER, DISMISS, BACK, CLOSE, TAB) the planner
(`data_plan.c`, pure) turns into at most one pad press a frame, injected at
the top of the call only while the real pad is idle (the real values back
after it). After every call MENU7's statics are published by value
(`pc_ui_data_game`: state, cursor, message, progress, fade, the scanned
files) and the frame's phase is derived from that publish alone: OPS (the
chooser), CARD / FILES / PROMPT (MENU7's cursor at x 180 / 24 / 0), WAIT (no
cursor, a message that waits for a press), else BUSY. The planner's rules:
Circle only when the phase and MENU7's cursor are the goal's target in the
same publish, a frame without a press after each press, a publish from a
call that carried a press waited out (it can show the list a page behind),
a real press clears the goal, a goal waits through BUSY (600 frames at most) and expires after
60 frames where it is taken, CLOSE / TAB back out of the card rows and the
list with Cross and stop at a prompt or a message (prompts are never
answered for the player); a click on Save / Load advances to MENU7's own
card row by itself. Posts made while BUSY are dropped (a double click, a
click while saving: one write). A load ends the frame before the jump's
loading screen; Back reopens the shell on the System list (a tab click or L1
/ R1 on the chooser reopens it on that tab); Close ends it through
0x7F; the HUD stays out while it shows; the panel is as see-through as the
other tabs (ui_menu_opacity). Saves byte-for-byte equal to the original
screen's (pad and mouse), a load's blocks and the file it read (pad and
mouse), the error paths and the exits (`ui_data_smoke`, card-safe through `card_fixtures.card_guard`).
Follow-ups (still original; only Data's state 3 and the Container's entry
latch a frame): Game Over's continue (`vs_menu7_gameOver`),
TITLE's Continue and the post-ending save (`TITLE 22C.c`); location
thumbnails, save dates, a read-only card peek, a Damaged badge (MENU7 counts
unreadable files as free blocks), separate Save and Load rows. Plan:
`pc/docs/plans/2026-10-15-modern-menus-data.md`.
Phase 7e: the Container (the save room's container: the field opens it,
BATTLE `func_800CB1C0`, MENU7 then MENUD). See "Container" below. Plan:
`pc/docs/plans/2026-10-15-modern-menus-container.md`.
Phase 7f: Score (pass 1). System > Score is native (MENU9.PRG never loads
for it). See "Score" below. Plan: `pc/docs/plans/2026-10-15-modern-menus-score.md`.
Plans: `pc/docs/plans/`. Next: phase 7+ (Title; the Score's enemy viewer), then the HUD.

| Phase | Screens |
| --- | --- |
| 1 | Shell, tabs, original screens from rows, HUD hidden |
| 2 | Equipment: Gear + Quick swap; mouse |
| 3 | Ashley summary + live model viewport (right stick turns) |
| 4 | System: settings pages + the game's options |
| 5 | Abilities (chain/defense mapping, break arts) |
| 6 | Items and Magick |
| 7+ | Workshop and Loot (7a), Map frame (7b), Quick manual (7c), Data (7d), Container (7e), Score (7f), Title; then the HUD |

## Score (phase 7f)

System > Score (`pc/src/ui/ui_score.h` has the design) is a native sub-screen
that takes the whole view: the score and the Risk Breaker rank on top, a
strip of the four sections with their counts (Titles k/32, Records 23, Time
Attack k/8, Bestiary k/78), then Home (a summary card and the four sections)
or a section's list and its detail card. MENU9's game rules stay MENU9's: at
each open `score_bridge.c` runs MENU9's own `_calculateProgress` once,
through one const table of pointers in 238.c (`score_menu9.h`, ROOD_PC, 10
lines: the function, `_score`, `_characterRank`, the progress statics and
the text tables, with a layout check), so roomFlags[1], the cursor memory's
bzero, the scoredata high-water marks, the titles, the score and the rank are
the original's by construction. Everything else is native in `pc/src`: the
numbers (`score_format.c`), the lists' cursor rules (`score_nav.c`, MENU9's
page / row stepping, wrap on a press only), MON.BIN's parse (`mon_parse.c`,
`score_mon.c`), the screen (`ui_score.cpp`) and its drawing
(`ui_score_draw.cpp`, every rect and size in theme.json's "score" group,
per layout width; RPC `ui_menu score_tune` overrides it until reset or the
next edit of the file, the active ones listed in the JSON's
`tune_overrides`). The cursor memories are written as MENU9 writes them:
menu9CursorMemory's pairs on every move in a section; cursorMemories[2] =
Home's row and [3] = 0 at the open and at every exit from Home (a section,
L1 / R1, Cross, Triangle, Square), not on Home's own moves; gazetteRow at
every exit from Home. So Square (anywhere) opens MENU9 where the native
screen was.

Intended differences from the original:

- The close is the shell's (-1), as for the Quick manual, not MENU9's
  currentMenu 9 path; the end state is checked equal (`ui_score_smoke` 2).
- MON.BIN is read into host memory once per process (`pc_ui_disc_read`,
  kept until exit like the Quick manual's pages), never the game's heap or
  CD queue; each open parses it with the current kill flags.
- The world after the close is as it was at the open: OPEN remembers
  D_800F190C and CLOSE calls `func_8008A4DC(1)` only if it was running.
  MENU9's state 6 (and the Quick manual's close) restart it
  unconditionally. The two differ only when something else already holds
  the world frozen at the open, which no known path does (MAINMENU runs
  with it running; the Ashley tab's freeze ends before the System tab
  shows); there the native close leaves it frozen for its owner instead of
  restarting it under it. `ui_score_smoke` 2 checks D_800F190C after every
  close against MENU9's.
- A click focuses its row through the canonical pair Down presses from
  (0, 0) would reach; the pad keeps MENU9's exact pairs.
- No 3D in pass 1: Home has no Ashley pose; the Bestiary's model viewer is
  the original's (Circle on an unlocked foe shows the hint "Original screen
  (Encyclopaedia)"; Square opens MENU9 on its top menu with the
  Encyclopaedia row selected, the list position kept with cursor memory
  on). The camera backup MENU9 makes is never needed (nothing moves it).
- L1 / R1 switch sections (MENU9 has no such shortcut; Home's row follows).

Follow-ups: the native enemy viewer (after an original-path ZUD smoke on
32-bit, 64-bit GL and Vulkan; shared menu-model code out of
`ashley_bridge.c`, a 592C.c table for the heap pre-check, `vs_menu9_loadZud`
/ `vs_menu9_freeZud`); Ashley's pose 33 on Home; preview on focus; the
"score" group in the editor's schema (`rood_schema::add_kind`, on another
branch).

## Container (phase 7e)

The save room's container is two overlays in sequence, hosted two ways
(`pc/src/ui/container_frame.h`):

- **MENU7** (`vs_menu7_saveContainerMenu`, 260.c) stays the only state
  machine for its prompts ("Check the container?", containerWarn, the
  cancel warning), the card read, `_copyContainer` and the save list
  (`_showSaveMenu(2)`), its teardown and the free. It runs **framed** as the
  Data screen's run does: its drawing gated (the Data macros plus
  `vs_battle_drawCursor`, `_promptYesNo`'s cursor), its statics published
  after every call, the player's native answers injected as single pad
  presses (`data_plan.c` PROMPT_YESNO: Up until Yes is lit, then Circle; No
  is Cross, never Triangle; never answered for the player; nothing while
  the prompt animates). Its save list is the Data screen's own model and
  planner in a **container mode** (no tabs, no chooser, the Container's
  header; published as the save flow). Phases from MENU7's state: 1
  PROMPT_CHECK, 2 / 3 / 6 / 8 / 9 BUSY, 4 WAIT (emptyCard: any button), 5
  PROMPT_WARN, 7 and MENUD TRANSFER, 10 / 11 SAVE, 12 PROMPT_CANCEL, 13..16
  CLOSING. The hooks: 260.c only (`ROOD_PC`, 15 lines: the cursor gate, the
  original renamed `_pcContainerMenu`, the wrapper calling
  `pc_ui_container_call_begin` / `_end` with `_pcStatics`).
- **MENUD** (`vs_menuD_exec`, direct entry 14) is **ported**
  (`container_ops.c`, `container_bridge.c`) on MENU7's own
  `vs_menu_inventoryStorage`, with no card I/O, as Loot ports MENUB. Its
  modes are the original's 1:1 (TOP: Remove / Store / End; LIST: 8 tabs,
  the second pick (merge two misc stacks of one item to 100 and the rest,
  else swap), the actions (Remove / Store with the original's reasons,
  Search, Discard, Discard all, Sort), the amount picker, the details (L1 /
  R1); CONFIRM: the commit), read from the pad as the original reads it
  (repeat, the held L1 + Left rules), so every pad script has one native
  equivalent and every write is the original's expression in the
  original's order: the storage, the bag and its lists, containerEmpty, the
  menu state, `vs_menuD_containerData` and the six `vs_mainMenu_*` pointers
  (each tick, 234.c:3149), `vs_battle_rowTypeBuf`, and the cursor memories
  at the original's points with MAINMENU's own row and action list
  arithmetic (8 visible rows; `unk9E`, `unkA0`, the per-tab rows 0x51..0x58,
  the action / sort / discard / commit menus' own).

**The latch.** At MENU7's entry 0: `framed = ui_menus && the overlay`. MENUD
is native iff the run is framed, so a flow never changes hands. MENU7's
overlay statics restart when it reloads after MENUD (as on the PlayStation),
so its latch is re-established from the host's frame at its state 8; a save
state that brings back another run's latch runs the rest original.

**The hand-back.** MENUD's state 6, always: `_copyContainerToInventory`, the
container's lists defragmented, containerEmpty, `currentMenu = returnState`
(7), `returnState = code + 2` (End: 5, the save; Cross at the top: 1, the
cancel warning; Triangle anywhere: 0, the revert), `*state` 0; the direct
session clears itself on that tick (menu 7 never routes through it).
Abandoned (the game left 14 without a hand-back): dropped, nothing written,
nothing freed (the heap is MENU7's). The frame ends when MENU7 returns, or
when the game leaves menus 7 / 14 or clears `D_800F4EA0 & 0x400`
(heartbeat); a soft reset forgets both layers.

**What is hidden.** MAINMENU's `vs_mainMenu_hideMenu` is 1 from the latch to
the end (MENU7's CONTAINER / YES / NO rows: `_renderMenuItems` still runs
their slides), the value before is restored at the end, an abandon and a
reset; the "MENU" command title is cleared each call; the information box's
CAUTION header (text box 7, whose init and dismiss stay MENU7's) is none
while framed; the HUD is out from the latch to the end.

**Deviations.** MENUD's item texts (`vs_mainMenu_loadItemText` /
`ensureItemTextUnloaded`) are not loaded (as Loot leaves MENUB's out; the
names and descriptions come from the disc's ITEMNAME / ITEMHELP; the heap's
free total at the close is compared between the original and native runs). The armor detail reads the container's
record (the original reads `vs_main_inventory.armor` for its DP bar: display
only, not reproduced). `unk4BB0` (MENUD's scratch for the misc stacking) is
refreshed where the original refreshes it on its own logic, but the panel's
slide-in timing is not reproduced: every read of it follows a fresh
`func_801031A0` (checked against 234.c by the smoke test), so it is never
read stale. `func_80106C64`'s row types: 0xCA00 (equipped) and 0xCC00
(combined) rows can be picked; only 0xCC01 (a staged part on tab 7) cannot.

**Screen.** The header (Container, the mode or the save room's name); TOP:
the three rows and the summary table (the bag and the container per
category, count / capacity, gold under half); LIST: the tab strip (16:9
chips with counts; 4:3 icons and the current tab's label), the list (13
rows, the original's marks: EQUIPPED, SET, the first pick), the right panel
(the destination: what is there + staged / capacity, misc after stacking;
the focused row's card: DP / PP, STR / INT / AGL, gem slots, the part's
weapon or shield), menus as popovers by the row, the amount and the Yes /
No's as centred cards; the prompts and the card wait as cards. A click is
the pad sequence that does the same (a row: the cursor there, then Circle;
a tab: one step at a time with each step's writes; a prompt's button: the
planner's goal); a right click is Cross, the wheel Up / Down; hover writes
nothing; clicks while MENU7 is busy are dropped and counted.

RPC `ui_menu` "container" (while latched, else null): run, visible, phase,
m7_state, session, handed_back, mode, dir, top, tab, rows_n, cursor, sel,
page, list_active, picked, staged, prompt_yes, prompt_ready, location,
location_name, prompt_text, wait_text, info, empty_text, confirm_text,
gated, presses, busy_drops, rejected, requests, hide_saved, misc_after,
misc_cap, discard {type, count, max, step}, detail {category, index, count,
slot}, tab_count, tab_cap, bag_count, box_count, dest_count, dest_staged,
dest_cap, menu {count, focus, entries[] {kind, enabled, label, desc}}, card,
rows[] {c, slot, flags, id, count, name}, hover, the drawn `hits`. `data`
carries `container` 1 for the save list. RPC `container {location}` opens
the container as the field does (the room's save point by default); RPC
`chest open=` also opens a container object (flag >= 64). Tests:
`container_ops_probe` (no game), `data_plan_probe` (PROMPT_YESNO),
`ui_shell_probe`, `ui_container_smoke` (card-safe: A original, B native by
pad, C by mouse; S1 at MENUD's exit, S2 the saved cards, S3 the close).

## Goals

- Readable, spacious menus: tabbed shell, one job per screen, a contextual
  card that explains the focused row, no clutter.
- Fewer steps for common actions (equip with a stat compare, assign
  abilities to buttons, set quick-swap weapon slots straight from the bag).
- Crisp at any resolution, 4:3 and 16:9 layouts, controller first with
  mouse and keyboard support.
- Settings for the PC port reachable in-game (System tab), including the
  switch back to the original menus.
- Ship only our own work: our layouts, open fonts, CC0 glyphs and the
  game's own live-rendered content (Ashley's model). Reference designs from
  around the web are inspiration only; no concept art, screenshots or other
  third-party artwork is copied or shipped.

## Design language

Prototype (private, placeholder data): the "Riskbreaker Menus" artifact.

- **Shell**: tabs across the top, switched with L1/R1:
  ASHLEY · EQUIPMENT · ITEMS · ABILITIES · MAGICK · LEÁ MONDE · SYSTEM.
  Sub-tabs (L2/R2) inside a tab. A hint bar bottom right (prompt + verb),
  a breadcrumb bottom left (area · room, or tab › sub-view).
- **Columns**: a left rail the player moves through, the live model in the
  centre, a card on the right for the focused row. In 4:3 the model
  column narrows; screens without a model use the width for the list.
- **Type** (all OFL, already vendored in `pc/third_party/fonts`):
  Alegreya for item, ability and spell names; Asap Condensed for tabs and
  letter-spaced labels; Chakra Petch for numbers and data (tabular
  figures).
- **Colour** (theme tokens): near-black warm ground, warm off-white text,
  crimson accent for focus and the active tab, gold for condition/rank,
  green/blue/red for HP/MP/RISK, green/red for stat deltas.
- **Opacity**: the ground over the game is see-through on every tab
  (System > Interface > Menu opacity, settings `ui_menu_opacity` 0..1,
  default 0.94 = the theme's ground alpha); panels scale with it
  (`ui_theme.cpp` theme()). The Ashley tab's model view shows the world
  behind the model through the presenters (the picture kept before the world
  froze, under the same ground; `pc/docs/Post_Processing.md`), so it is as
  see-through as the others; a long weapon is no longer cut at the column.
  The model itself is lit by a studio rig of its own (key, fill, ambient,
  rim; `menu_studio.glsl`), the same in every room, with the texture's
  colours as they are (no room light, no game rim copy; Native_Rendering.md).
  The rig is data: the `menu_model_light` block of `pc/assets/ui/theme.json`
  (`ui/menu_light.h`, live-reloaded with the theme), tuned live in the dev
  tools (Graphics > Menu model light: the editor's Details rows for the
  schema kind `menu_model_light`, with ↺, the value ladder, undo, "Save to
  theme.json" and "Reset to defaults") or by RPC `graphics menu_light`.

  | Key (`menu_model_light.`) | Default | Range | What |
  | --- | --- | --- | --- |
  | `key.direction` | [-0.45, -0.5, -0.74] | each -1..1, not 0 | view space toward the light (x right, y down, z away); or `azimuth` / `elevation` in degrees (0, 0: from the camera; azimuth positive to the right, 180 behind) |
  | `key.color` | [1, 0.97, 0.92] | 0..4 | linear colour, times intensity |
  | `key.intensity` | 0.5 | 0..4 | |
  | `key.wrap` | 0.4 | 0..1 | n.l wrapped: the far side keeps this share (0 Lambert) |
  | `fill.direction` / `color` / `intensity` / `wrap` | [0.85, -0.05, -0.52] / [0.9, 0.95, 1] / 0.15 / 0 | as the key's | |
  | `rim.direction` / `color` / `intensity` / `wrap` | [0.5, -0.55, 0.67] / [0.85, 0.9, 1] / 0.45 / 0.2 | as the key's | times (1 - n.v) ^ power |
  | `rim.power` | 2 | 0.25..16 | higher: a thinner rim |
  | `rim.lift` | 0.1 | 0..1 | added to the texture's colour under the rim |
  | `ambient.sky` / `ground` | [0.56, 0.56, 0.58] / [0.5, 0.5, 0.52] | 0..4 | on upward / downward faces |
  | `ambient.intensity` | 1 | 0..4 | |
  | `exposure` | 1 | 0..8 | scale on the lit colour |
  | `saturation` | 1 | 0..2 | 0 grey, 1 as lit |

  A missing key keeps its default; a value out of range is clamped; a value
  of the wrong type (or a zero direction) keeps the whole theme as it was
  and logs why. Saving rewrites the block alone (the file's other keys and
  formatting kept; atomically); the defaults save byte for byte as shipped.
- **Condition card**: the Condition panel as a whole titles the card
  "Condition · <the worst part's state>" (the game's terms: Critical,
  Damaged, Wounded, Good, Excellent) with a summary of the parts; a body
  part under the pointer, or stepped to with Left / Right on the row,
  titles it with that part and its HP (`pc_ui_ashley_condition_text`).
- **Tint**: System > Interface > Menu tint recolours the backdrop and panels,
  as older RPGs let you set the window colour. Presets: Leá Monde (the
  theme file untouched: the default), Classic (deep navy), Slate, Crimson,
  Verdant, and Custom; Tint red / green / blue sliders 0..31 (the PlayStation's
  5-bit colour) show the current colour, and moving one copies it into
  Custom and switches to Custom (picking a preset only sets the sliders;
  Custom remembers its colour). Settings `ui_menu_tint` (id or 0..5) and
  `ui_menu_tint_custom r g b`. The derivation (`ui_theme.cpp` tint_theme,
  pure, probed): ground = v * 64 / 31 per channel, darkened in linear light
  to a relative luminance of at most 0.02 (a white tint gives #272727);
  panel, rule and rule2 keep their offsets above the theme's ground, each
  channel's offset weighted by 1 + 0.35 * (v / mean - 1) (0.6..1.4) so they
  lean a little toward the hue and ground < panel < rule < rule2 always
  holds; dim and ash mix toward ash / bone in tenths only if their contrast
  on the panel would fall below 2.4 / 4.5 (or the theme's own, if lower);
  likewise the accent below 2.5, and the selection wash (accent_soft over
  the panel) when it stands nearer the panel (sRGB distance) than on the
  theme's own (a red wash on Crimson or a red Custom); text, gold, bars and
  prompts keep their colours. Leá Monde's sliders and chip read the theme
  file's live ground (v * 31 / 64), so a nudge after a theme.json edit
  starts from that colour. It is applied
  before the opacity, so `stage`, the Ashley tab's presenter backdrop
  (`pc_ui_menu_backdrop`) and model tile (`pc_ui_theme_ground_rgb`) follow
  it at any opacity, 4:3 or 16:9. The row shows the vivid colour as a chip;
  RPC `ui_menu` reports the shown colours under `theme`.
- **Motion**: short (150-250 ms) eased transitions: tab underline slides,
  focus bar grows, a screen fades up 6 px on entry; none under reduced
  motion. The model never turns by itself; the right stick turns it.
- **Rules shown, not hidden**: unavailable rows stay visible, greyed, with
  the game's own reason (not enough MP, only at a save point, silenced...).

### Screens (first wave)

| Tab | Content | Actions |
| --- | --- | --- |
| Ashley | Rank; HP/MP/RISK; weapon and shield (DP/PP, material, type); STR/INT/AGL; condition per limb; status effects with timers | Focus explains (class/affinity, limb effects, ailment cures); confirm jumps to Equipment or to the curing item |
| Equipment › Gear | 8 slots (weapon, shield, head, body, arms, legs, accessory) with DP; quick-swap badge on weapons | Confirm opens the bag for that slot; the card compares candidate vs equipped (STR/INT/AGL/DP deltas, class and affinity bars with the current value marked); confirm equips |
| Equipment › Quick swap | 8 slots laid out like the pad (face-button diamond, D-pad diamond), weapon + shield each, equipped marker | Confirm: pick any weapon from the bag (need not be equipped), then a shield or none for one-handed weapons; Square clears; a weapon lives in one slot (picking it elsewhere moves it) |
| Items | Category chips (L2/R2), capacity, list with quantities and equipped / part-of-a-set marks; Square: details | Circle: the original's actions per category (Use, Search, Discard with a quantity picker, Discard all, Sort by the original's stats), plus Move (swap, misc stacks merge up to 100) and "Original screen" (the original Items menu, also its Setup); every change is the original menu's own, byte for byte |
| Abilities | Chain / Defense: 3 button slots + learned list with RISK cost; Break arts by weapon type with kills to the next art | Assign an ability to a button (swaps if already mapped) |
| Magick | Warlock / Shaman / Sorcerer / Enchanter / Teleport; MP; cost per spell | Cast (target picked in the field as today) |
| Leá Monde | Map frame (native rail, legend, prompts; the game's 3D map) | Open the map; areas and rooms by pad or click; drag / wheel / middle drag turn, zoom and pan |
| System | Game: Data (native Save / Load over the game's MENU7), Score (native: Titles, Records, Time Attack, Bestiary), Quick manual (native reader: topics rail, page card). Settings: Graphics, Display, Interface, Controls, Gameplay | Settings change with left/right and apply at once; Data's clicks walk the game's own cursor; the manual scrolls by line or page, Square opens the original |

Screens not yet replaced open as the game's own screens from the shell:
Workshop (assemble, gems, disassemble, rename, repair, combine), Container,
Score's enemy model viewer, Map internals, Results, Title, Game over. They
are replaced one by one later.

### System settings

The System tab edits the same variables as the editor and `settings.cfg`
(`project_settings.cpp`), so all three stay in sync:

- Graphics: lighting preset, anti-aliasing, frame interpolation, texture
  filtering, depth of field, haze, renderer (restart).
- Display: aspect (`widescreen`), window mode, frame pacing, brightness.
- Interface: **Menus (Modern / Original)**, Text (`ui_text`), UI upscaling,
  Button prompts (the main session's `button_prompts`, not a second
  setting), HUD (later).
- Controls: right-stick camera and its options, vibration.
- Gameplay: Skip intro (`skip_intro`, from the next start), quick weapon slots (`qol_weapon_slots`), Battle
  charging (`qol_skip_charging`: Off / Fast / Skip, pc/README.md "Battle charging") and the game's own
  options (simple map, ability timing, weapon/armor status, cursor memory,
  information, puzzle mode, sound), written to `vs_main_settings`.

Implemented (phase 4) with these differences from the plan:

- A binding capture started from the gamepad cancels after 5 s without
  gamepad input (a pad has no Escape; Back/Select is a bindable button, so
  it does not cancel). A capture from the keyboard waits until Esc or a
  click.
- Square for "default value" on a row is not implemented (Bindings has a
  Reset to defaults row).
- Reserved keys are refused at capture with the reason in the prompt: Esc,
  F1, F2 (the editor), Alt and Super (the desktop), J L I K O while the
  camera keys are on. A swap that moves a key or gamepad element to another
  button says so on the status line for 3 s ("M moved from Circle to
  Triangle; Circle gets S").
- The key that ended a capture stays the capture's until released (its
  auto-repeat and text never reach the game), as does a captured gamepad
  element.
- Internal resolution applies when a pointer drag ends (the number follows
  the drag); Left/Right apply at once.
- Brightness is a curve with a finite slope at black, run on the post
  chain's 16-bit float image before the window's 8-bit rounding (no extra
  pass at 50).
- A window mode switch that fails keeps the window's mode (the one saved);
  the `system` RPC reports the failure.

Fallback: switching Menus to Original takes effect the next time the menu
opens. It is also reachable from the editor (Graphics panel) and
`settings.cfg`, so a broken modern screen can never lock the player out.

## Architecture

All new code in `pc/src/ui/`. Game-side changes only under `#ifdef ROOD_PC`;
`make commit-check` keeps matching.

1. **Bridge** (`ui/bridge`). The game opens its menu exactly as today
   (Triangle, `func_800CACD0`), so pausing and the "menu not allowed now"
   rules stay the game's (`D_800F4EA0` disable mask, `menuFlags`). With
   `ui_menus modern` the bridge keeps the original top-level list from
   drawing and from reading the pad while the shell runs, and closes
   through the game's own close path (`vs_battle_menuState.currentMenu`).
   For screens not yet replaced, the shell asks the game to open that
   submenu (as if its row were chosen) and hides until it returns; greyed
   rows open too, as in the original (the game's screen greys what is off),
   and the card gives the reason from the disable mask. The shell takes a
   session only at the top level (`currentMenu` 0, no direct entry
   `D_800F4EA0 & 0x200`, no shortcut); shortcut entries (L2
   magic/arts/items, loot, containers, ability learned, script menus) stay
   original until their screen exists. The Menus choice is latched when the
   menu opens, so switching applies from the next open. A session that ends
   without the game's close path is cleaned up: every battle frame
   (`pc_ui_menu_heartbeat`, from the pad hook in `weapon_slots.c`) closes a
   shell whose top level was not ticked for 4 frames or whose menu left the
   top level, and the jumps to the title or a new battle (`boot_sdk.c`, the
   soft reset) end the session and drop a pending model reload. The model
   reload's finish runs from the same per-frame hook, not from the menu.
   Weapon and shield equips wait until `MAINMENU.PRG` is resident (the
   snapshot says busy for the first few ticks of each open).
2. **Model** (`ui/model`). A read-only snapshot per frame: Ashley
   (`vs_battle_actor2`: HP/MP/RISK, STR/INT/AGL, limbs, statuses), computed
   equipment (`vs_battle_uiEquipment`/`uiWeapon`/`uiShield`/`uiArmor`/
   `uiAccessory`, class/affinity arrays), inventory (`vs_main_inventory_t`
   and the item records in `func_8006B57C_t.h`), abilities and spells
   (`vs_main_actions`, `vs_main_settings.mappedChainAbilities`/
   `mappedDefenseAbilities`, arts status), settings. Game strings decode to
   UTF-8 through ui_text's charset table. Plain functions; testable without
   drawing.
3. **Actions** (`ui/actions`). Each action calls the same game functions
   the original menu calls (equip, map ability, use item, cast, discard,
   sort). Quick-swap slots reuse `weapon_slots.c` storage (slot byte in
   `weapons[i].gems[3]`, shield masks in `shields[i].gems[3]`), so memory
   cards stay compatible with the unmodded game and the mod; `weapon_slots`
   gains exported helpers (assign weapon N to slot S with shield M).
4. **Draw** (`ui/draw`). Dear ImGui draw lists only (no ImGui widgets): the
   overlay already renders on GL and Vulkan, and ImGui 1.93's dynamic font
   sizes give sharp text at any scale from the vendored TTFs. Our own
   widgets: rows, bars, rings, cards, diamonds, sub-tabs. Layout in virtual
   units (1280x720 or 960x720), scaled to the window, drawn after the game
   frame. Controller glyphs from the main session's prompts API (Kenney
   Input Prompts, CC0); stubbed until it lands.
5. **Theme** (`pc/assets/ui/theme.json`). Colours, fonts, sizes, spacing,
   motion timings; live reload like `settings.cfg`.
6. **Live model**. First version: the game renders Ashley itself, as the
   original Equip/Status screens do (`_renderAshley.h`), with the camera
   placed for the centre column and yaw from the right stick (J/L on the
   keyboard). An offscreen viewport (`render.h` target) only if needed.
7. **Input**. Pad to the shell while it is open (L1/R1 tabs, L2/R2
   sub-tabs, Circle confirm, Cross back, Triangle exit, Square secondary).
   New: mouse routed to the shell (hover focuses, click confirms, wheel
   scrolls). Prompts follow the last device used.
8. **Tests**. RPC `ui_dump` returns the open screen as JSON (rows, values,
   focus, enabled/reason). Per screen: an RPC test that navigates and
   checks the dump. Per action: a parity test that performs it through the
   original menu and through the shell and compares the resulting game
   RAM. Screenshot goldens stay local (`pc/build/golden`, never committed).
   Suites register in `pc/tests/run.py`.

## Build order and estimate (agent time)

1. Foundation: bridge open/close, draw layer, theme, input, `ui_dump`,
   shell with empty tabs (~1 day).
2. Equipment: Gear + Quick swap, with parity tests (~1 day).
3. Ashley summary (~1/2 day).
4. System settings (~1/2 day).
5. Abilities, Items, Magick (~1/2 day each).
6. Later: Workshop and Loot (7a, done), Map frame (7b, done), Quick manual
   (7c, done), Data (7d, done), Container (7e, done), Score (7f, pass 1 done), Title; then the HUD.

The bottleneck is verification (action parity, playtesting feel), not code.

### HUD backlog (user ideas, not started)

**Contextual action prompt bar** (bottom right; prompt icon + spaced caps verb in the label font, no panel or a very subtle one; follows button_prompts style and bindings). The game has no "can interact" indicator: field buttons are resolved at press time in `vs_battle_exec` (BATTLE 146C.c, field case `D_800F196C == 1`). The bar runs the same side-effect-free probes each frame:

| Context | Game state to read | Prompts |
| --- | --- | --- |
| Crate in front / carrying | `func_800A38E0(0) >= 0` (3A1A0.c); status `func_800A0BE0(0)` bit 0x80000 carrying | Cross PUSH / LIFT / DROP; no Square JUMP while carrying |
| NPC | actor-cone probe `func_800A3760(0, 0xA0, 0x80)` with `actor->unk24 == 4` | Cross TALK |
| Examine | type-4 event in tile mode (`func_80074860`, `func_800BEC58` 4A0A8.c) — needs a side-effect-free copy of the condition evaluator | Cross EXAMINE |
| Door | `func_800748B8(0)` section-A door; `lock.state` 0 open, 1/3 needs `keyOrSigil` (`vs_battle_itemIdIsInInventory`, sigil state flags) | Cross OPEN / UNLOCK / LOCKED |
| Chest | `func_80074950(0)` (facing, not yet opened `unk10+0x3C0`, height match); `keyOrSigil` lock | Cross OPEN |
| Save point / magic circle | `func_8008A4FC()` save-point id; Save allowed when `stateFlags.unkA7 == 0`; Teleport when `unkB5 != 0` and a `savePointStates` entry | Triangle MENU (Save / Teleport) |
| Workshop | `vs_battle_inWorkshop()` (146C.c) | Triangle MENU (Assemble / Repair / Combine) |
| Weapon drawn | `vs_battle_characterState->weaponDrawn & 1` | Circle ATTACK, Cross SHEATHE; sheathed: Circle DRAW |
| Hanging | status 0x100000 | (no draw / first-person) |
| L2 shortcuts | `vs_battle_isSpellClassUnlocked`, `_breakArtsUnlocked`, `vs_battle_chainAbilitiesUnlocked` (5BF94.c) | slots greyed when empty |

Gaps: `func_80075554` (undecompiled, per-frame field events), `D_800F196C` values unnamed.

**Vector limb-condition figure:** replace the blocky bottom-left limb sprite with an authored vector figure whose regions take the game's live limb-condition colours (`vs_battle_getLimbStatus`, `D_800EBC54`).

## Ownership (agreed with the main session, 2026-10-12)

- This work: `pc/src/ui/*`, HUD widgets, game-side mouse input, the
  quick-swap assignment UI.
- Main session: controller glyphs and `button_prompts` (exports a small C
  API for drawing them), ui_text string layout in the game's own text,
  talk bubbles (`ui_upscale.c`), renderer depth fixes.
- Settings additions (`ui_menus`, theme path) live in their own block of
  `project_settings.cpp`.
