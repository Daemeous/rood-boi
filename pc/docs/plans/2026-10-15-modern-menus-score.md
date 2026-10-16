# Modern menus, phase 7f: the native Score screen (MENU9), pass 1

Data pages and the Bestiary list; the 3D enemy viewer is a follow-up (below).

## Principles

- Score is a native sub-screen of the System tab and follows the Quick
  manual's pattern (`ui_manual.cpp`, `manual_bridge.c`): Circle on the row
  enters it, Cross steps back, Triangle closes, Square (on the row or
  anywhere inside) delivers command 9, the original MENU9, unchanged. With
  ui_menus off only the original exists.
- MENU9.PRG is never loaded on the native path.
- Game semantics come from the original code: MENU9's `_calculateProgress`
  runs once per open (roomFlags[1] |= 0x400000, the cursor memory's bzero
  without cursor memory, the scoredata high-water marks, `titles |=`,
  `_score` and `_characterRank`). Score tables, rank thresholds, title rules
  and reference times stay in 238.c; nothing of them is ported or made data.
- Formatting, navigation, layout and the MON.BIN parse are native in pc/src.
- No 3D in this pass.

## src/ hook (ROOD_PC, one block in `src/MENU/MENU9.PRG/238.c`)

After `_timeAttackReferenceTimes`: `#include "pc/src/ui/score_menu9.h"`, the
layout check (`PC_UI_SCORE_MENU9_LAYOUT`: the statics' widths, the tables'
counts through the build's generated index enums) and one const table
`pc_ui_score_menu9_table` of pointers (`_calculateProgress`, `_score`,
`_characterRank`, `_clearCount`, `_openedChestCount`, `_mapCompletion`, the
nine text tables, the reference times). ENDING's same-named statics are
separate objects (`nm`: one `_calculateProgress`, one table).

## pc/src

| File | Role |
| --- | --- |
| `ui/score_menu9.h` | the table type, the layout macro |
| `ui/score_bridge.c` | the game side: OPEN (freeze, calculate, snapshot, select sound), CURSOR / HOME / GAZETTE (the cursor memories), SFX, CLOSE, ORIGINAL; abandon, reset, a snapshot reconcile |
| `ui/score_format.c` | Records values, fastest clear (99:59 cap), percentages, trial times (0x800000 sentinel), grouping |
| `ui/score_nav.c` | MENU9's page / row rules per list, the canonical pair |
| `ui/mon_parse.c`, `ui/score_mon.c` | MON.BIN (host memory): 78 records, descriptions, unlocked and the prev / next ring, bounds checked |
| `ui/ui_score.cpp` | the screen's state, input, pointer, JSON (`system.score`) |
| `ui/ui_score_draw.cpp` | the drawing; every rect from theme.json's "score" group |

Shell wiring next to the Quick manual's (enter, back, leave, Square, JSON,
mouse kinds `PC_UI_MOUSE_SCORE_*`); `menu_bridge.c` refresh / abandon /
reset; `ui_draw.cpp` gives the screen the whole view (its sections where the
tabs go); `ui_theme.cpp` the "score" group (clamped; RPC `ui_menu
score_tune {key, index, value}` / `{reset: true}` overrides it: one
override per key and index, cleared by a reset or the next edit of the
file, listed in the JSON's `tune_overrides`).

## Layout (layout units; 16:9 1280 x 720, 4:3 960 x 720)

Header y 32-112 (score, rank), strip y 120-168 (the sections and counts),
content y 184-640, the footer. Per page (x0-x1, wide / narrow; the outer
edges at the header's margin_x): Home card 56-700 / 56-500 and list
732-1224 / 524-904; Titles list 56-700 / 56-500, detail 732-1224 / 524-904;
Records table 56-820 / 56-600, detail 852-1224 / 624-904; Time Attack list
56-520 / 56-420, panel 552-1224 / 444-904; Bestiary list 56-560 / 56-440,
detail 592-1224 / 464-904. score_size 40, rank_size 44, row_h 44,
visible_rows 10 (clamped to the content band), page_rows 8, locked_alpha
0.45; every per-element type size (title_size, name_size, number_size, ...)
and spacing (pad, inset, name_x, line_gap, head_rule, hint_y, ...) is in the
same group.

## Controls

Home: Up / Down, Circle or Right enters, Cross back to the System list,
Triangle closes, Square the original. Sections: L1 / R1 switch (wrapping,
sound 4), Up / Down with repeat by MENU9's rules (sound 4), the Bestiary's
L2 / R2 a page, Cross Home (6), Triangle closes (6), Square the original. The
Bestiary's Circle (footer "Viewer" on an unlocked foe): the hint "Original
screen (Encyclopaedia)" on an unlocked foe, sound 7 on a locked one. Square
opens MENU9 on its top menu with the Encyclopaedia row selected (not on the
foe's viewer: that is the native viewer's follow-up). Pointer: hover highlights only, a click focuses or
enters, the wheel steps, a right click backs out.

## Cursor-memory mirror

menu9CursorMemory's pairs on every move (titlePage / titleRow: Titles;
recordTimePage / Row: Records; gazettePage: the boss; encyclopaediaPage /
Row: the Bestiary); cursorMemories[2] = Home's row, [3] = 0 (as `_setRows`
with 4 rows) at the open and at every exit from Home (a section, L1 / R1,
Cross, Triangle, Square), not on Home's own moves, as the original's main
menu list writes it; gazetteRow = [2] + [3] at every exit from Home. Pad paths keep exact
pairs; pointer paths reach the same entry through its canonical pair.

## Deviations

- The close is the shell's (-1), not MENU9's currentMenu 9 path; the end
  state is checked equal (`ui_score_smoke` 2).
- The world after the close is as it was at the open (`score_bridge.c`
  remembers D_800F190C at OPEN; CLOSE calls `func_8008A4DC(1)` only if it
  was running). MENU9's state 6 restarts it unconditionally; the two differ
  only when another owner holds the world frozen at the open (no known
  path), and then the native close leaves it frozen for that owner.
  `ui_score_smoke` 2 compares D_800F190C after each close with MENU9's.
- MON.BIN is read into host memory once per process, parsed per open.
- A click focuses its row through the canonical pair; L1 / R1 switch
  sections (MENU9 has no shortcut).

## Tests

ctest: `score_nav_probe`, `score_format_probe`, `mon_parse_probe`,
`ui_shell_probe` (score_tests). Smoke: `pc/tests/ui_score_smoke.py` (cases
1-8 in its docstring; card-safe through `card_guard`), run on 32-bit, 64-bit
GL and Vulkan, with `ui_menus_smoke`, `ui_system_smoke`, `ui_ashley_smoke`,
`ui_ashley_detail_smoke`.

## Follow-ups

- **Native enemy viewer.** First an original-path ZUD smoke (Encyclopaedia,
  Circle on an unlocked foe, R1 / L1, back) on 32-bit, 64-bit GL and Vulkan:
  that path has never run on PC. Then: `menu_model.c` out of
  `ashley_bridge.c` (FILL, ASHLEY_AT_512, LOOK_UP, PITCH_MAX into theme.json
  "model"); a 592C.c table hook (about 5 lines: `_zudSizes`, the variants)
  for the heap pre-check; `vs_menu9_loadZud` / `vs_menu9_freeZud` with the
  states WAIT_SLOT, HEAP_CHECK, LOADING, DRAINING (a load is never
  abandoned: drained, then freed); never `func_800F9E0C` on an enemy.
- Ashley's pose 33 on Home; preview on focus.
- The "score" group in the editor's schema (`rood_schema::add_kind` /
  `rood_details::draw_kind`, not on this branch yet); sharing
  `menu_model_light`.
