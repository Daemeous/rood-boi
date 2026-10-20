# The classic HUD and menus drawn in Lua

Status: plan, 2026-10-20. Not started. It follows
`2026-10-18-lua-visible-layer.md` (F1-F3, done), `2026-10-18-lua-m1-options.md`
(done) and F5 of `2026-10-18-freedom-and-fast-iteration.md` (the lint).

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
| Top-right sliding sprite `D_800EB9CD` (73 x 10; probably BATTLE MODE, unconfirmed) | `func_800CA2DC` | anchored right inside the panel's left range | no | confirm in phase 0 |
| Chain / defense ability icons (L2 cross, face-button diamond, D-pad) | `func_800C9FE8`, `func_800C9F98` (5BF94.c) | centre anchor; `weapon_slots.c` replaces `func_800C9EB8`; `button_prompts.c` redraws texels | no | |
| Emote balloons (…, !, ?, heart) | `func_80095C18` (2842C.c) | `PC_GTE_UNNARROWED`; `ui_emotes.c` vector redraw | no | |
| Timing word (TOO FAST!, FAST!, SLOW!, success words) | 2842C.c label kind 4 → `func_800958A4` | `PC_HUD(PC_HUD_STATUS)`, anchored left, ui_text `status` (`pc_text_status`) | yes (opt-in flavour) | gaps and left-edge cut (`645a7477`) |
| Chains (3 CHAINS) | kind 5 → `func_80095A4C` | `PC_HUD(PC_HUD_CHAIN)`, anchored left | yes (opt-in) | |
| Buff popup (WEAPON HUMAN +1) | kind 6/7 → `func_8009695C` | `PC_HUD(PC_HUD_BUFF)`, anchored left | yes (opt-in) | |
| Damage / heal / MP / stat numbers over actors | kind 1 → `func_80094844` / `func_80094B0C` | `PC_HUD(PC_HUD_DAMAGE)`; unnarrowed projection; ui_text `popup_scale` | yes (opt-in) | digits share the squash bug |
| Over-actor words: `func_8009506C` (4-piece word when `unkE == 2`, probably MISS/GUARD) and `func_80095258` (status-ailment names) | 2842C.c label loop, kind 2 | ui_text; centre-compressed | no | confirm the words in phase 0 |
| Name window (action name, text box 7 band) | `vs_battle_displaySceneMessage` → text box 7 → `func_800CB83C` | `pc_charge_skip_name`, `pc_hud_name_window` event; drawn as a band by `func_800CD158`; Zig trial port of `displaySceneMessage` (logic only) | event only | |
| Text boxes: dialogue bubbles, information box (box 7), item/notice boxes | `func_800CB83C` (all 8 boxes) → frames `func_800CD158` / `func_800CD3E4`; text `_printVariableWidthFont` / `_printFixedWidthFont` / `_renderTextImmediate` into the private OT `D_800F51B8`, spliced with `AddPrims` | ui_text `dialogue`/`menu` (line layout, `bubble_segments`); `ui_bubbles.c` vector bubbles (`pc_bubble_rect`/`pc_bubble_frame`); `text_lines.c`, `fixed_font.c`; centre-compressed; portraits/stills `func_800CCA90` (full-screen ones `PC_ANCHOR_FILL`) | no | |
| Timer (time attacks) | `_renderTimer` (5BF94.c:3788) | ui_text digits | no | |
| Button prompts in the field (classic sprites) | `vs_mainmenu_renderButton` / text boxes | button roles redraw (`pc_button_roles_glyph`) | `d.glyph` (opt-in) | |
| Travel prompt (PC feature) | `pc/src/fast_travel.c`, C++ | modern claim; the classic look is drawn with the game's sprites by C++ | modern only | |
| Targeting: range shapes, target rows and leader lines, lock-on marker, part details | `58578.c:func_800C0D78` (shapes), `func_800C20B4` / `func_800C1DC4` (rows: gradient + `renderTextRawColor`), `func_800C16FC` / `func_800C1A40` (leader lines), `5BF94.c:func_800C28AC` (marker); state machine `func_800C58F8` prints part HP/status/class/affinity into text box 7 | `PC_GTE_UNNARROWED` on the line start and marker; rest centre-compressed; ui_text | no | |
| Minimap / compass | none in the field HUD; the compass is the Map screen's (`_renderCompass`, MENU5) | — | — | |
| Area titles, loading | no dedicated draw found (room names appear in MENU5's text box 0; title cards are probably event text boxes); no loading screen, only `146C.c:_drawScreenTransition` | — | — | unconfirmed |
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
| Items (lists, status views, equip, sort, discard) | MENU3 | `vs_menu3_exec` (16C.c) + `items0/1/2.h`, `_renderAshley.h` | items_bridge, equip_bridge | no | |
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
| Ending | ENDING.PRG | not surveyed | — | no | |

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
