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
and clamps (the pad wins). Map status, mapPaling, cursor memories, settings,
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
Plans: `pc/docs/plans/`. Next: phase 7+ (Container, the Data frame, Score,
Title), then the HUD.

| Phase | Screens |
| --- | --- |
| 1 | Shell, tabs, original screens from rows, HUD hidden |
| 2 | Equipment: Gear + Quick swap; mouse |
| 3 | Ashley summary + live model viewport (right stick turns) |
| 4 | System: settings pages + the game's options |
| 5 | Abilities (chain/defense mapping, break arts) |
| 6 | Items and Magick |
| 7+ | Workshop and Loot (7a), Map frame (7b), Quick manual (7c), Container, Data, Score, Title; then the HUD |

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
| System | Game: Save, Load, Score, Quick manual (native reader: topics rail, page card). Settings: Graphics, Display, Interface, Controls, Gameplay | Settings change with left/right and apply at once; the manual scrolls by line or page, Square opens the original |

Screens not yet replaced open as the game's own screens from the shell:
Workshop (assemble, gems, disassemble, rename, repair, combine), Container,
Loot, Score, Map internals, Results, Title, Game over. They
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
- Gameplay: Skip intro (`skip_intro`, from the next start), quick weapon slots (`qol_weapon_slots`) and the game's own
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
   (7c, done), Container,
   Data, Score, Title; then the HUD.

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
