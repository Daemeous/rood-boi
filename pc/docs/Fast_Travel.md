# Fast travel

Teleportation between the Circles of Magic, offered from the first Circle
once the prologue is over. It is the game's own spell: the port adds no new
way of moving, only an earlier unlock, a cost mode, a few refusals of its
own, a Travel button and a prompt. Code: `pc/src/fast_travel_rules.c` (the
rules, plain C), `pc/src/fast_travel.c` (the game side), four small
`#ifdef ROOD_PC` hooks in `src/`, the modern menus' bridges. Plan:
`pc/docs/plans/2026-10-16-fast-travel-plan.md`.

## What it is

The game already has save-point teleportation: MENU0's Magic >
TELEPORTATION lists the Circles found (`savePointStates`), costs MP and
moves Ashley with BATTLE `func_800735F8` case 10, which calls
`func_8007C694`, the same screen transition and room load a door uses. The
game only offers it once the Lich teaches the spell, late in the story.

With fast travel on (the default), it is offered from the first Circle once
the prologue is over:

- **Travel button**: stand on a Circle and press Travel (Select on a
  gamepad by default; no key until you bind one). The game's own list of
  destinations opens directly, the cursor on the Circle you stand on.
  Circle travels, Cross closes the menu.
- **Original menus**: Triangle > Magic > TELEPORTATION. On a Circle the
  top level shows the Magic command even before the first spell.
- **Modern menus** (`ui_menus 1`): Magick > Teleport. The Travel button
  opens the game's own list there too: deliberately the original flat list
  in both menu modes for now (the user's choice; the modern map picker that
  would replace it waits for the modern-menu work, see "Not yet").

After the choice only the game's code runs: the effect, the fade, the room
load, the camera, the music (it changes only with the zone, as through a
door), the room's entry script, its enemies, the map's seen flags. Ashley
arrives on the destination's Circle, so Data > Save works at once. The list
is the game's flat one (each line names its area); a map-based picker waits
for the modern menu work.

## Whose unlock

- **The game's**: `vs_main_stateFlags.unkB5 != 0` (the Lich's event sets it
  to 1). Everything is the game's then, settings or not: its costs, its
  `magicUsed`, and `unkB5 == 2` (the spirit paling) refusing travel.
- **The port's**: otherwise, while `qol_fast_travel` is on and `introState >= 3`
  (the prologue is over). MENU0 reads 1 for `unkB5` then
  (`pc_fast_travel_unlock`).
- Neither: MENU0 reads 0, as the stock game.

Time attacks need no rule of their own: a boss fight sets `unkA7` ("Cannot
save during battle"), which refuses travel (R3).

## Cost and magic

While the port's unlock is in effect, `qol_fast_travel_cost` decides the
cost: 0 free (the default), 1 flat (`qol_fast_travel_flat` MP, 15), 2 scaled
(`qol_fast_travel_scale` times the game's cost, 0.5, rounded), 3 the game's
(15 MP plus 4 per Circle between). The value is clamped to the 12 bits the
hand-off packs it in (in float before rounding, so a huge scale from a
hand-edited settings file caps at 4095 rather than wrapping to free). It is applied where MENU0 compares, shows and packs
the cost; `_getTeleportCost` and its "cost 15 is the Circle you stand on"
sentinel are untouched (MENU0's cursor depends on it). In free mode the
menu mask's MP-0 bit (0x20) does not grey Magic, the Magic title MAINMENU
draws as the school list opens (C48.c) or the destinations while Ashley
stands on a Circle.

The game's Teleportation sets `magicUsed`, which forfeits the ending's "no
magic" title. Case 10 asks `pc_fast_travel_begin` first: it is set only when
the game's unlock is in effect.

## Rules

- **R1** Leave only from a Circle (`func_8008A4FC() != 0`) whose own state is
  recorded (`savePointStates[here] != 0`; without it MENU0's cursor row
  would be uninitialised). The port's refusals grey MENU0's Teleportation
  row (one extra term in `initSubmenu`).
- **R2** No event script, script room change, screen transition or game
  over (the Travel button's guard).
- **R3** Ashley free: not acting (`unk954`), not falling, L2 / R2 not held
  (the weapon slots' and the shortcuts' chords); the menu mask as the game
  has it (carrying, hanging, silenced; MP 0 only when a cost applies); not
  while `unkA7` is set (the game's "cannot save here").
- **R4** A destination is found (state 1); state 2 (the paling) is refused,
  as the game does.
- **R6** Each of the 35 destinations' rooms has its Circle object (section 9,
  type 16) with id point + 1 on the arrival cell: the RPC `fast_travel
  {audit}` reads them from the disc. All 35 pass.
- **R7** Not while the Room Browser has warped (or a warp is under way). A
  warp with scripts on runs a Circle room's entry script, which records the
  Circle: `room_warp.c` snapshots `savePointStates` when a chain of warps
  starts and puts it back when the chain ends (back where it started, or the
  warped state cleared: the Room Browser's Stay drops the Circles recorded
  while warped too), so a design visit never becomes a destination. The
  snapshot is host state kept beside the warped flag (`room_catalog.c`), and
  both are dropped when the game's state is replaced (`boot_sdk.c`'s jumps
  to the title and into BATTLE: soft reset, game over, the ending, New Game,
  Continue), so one game's snapshot is never put back into another. One
  exception: with the Room Browser's Refuse saves off, a save made while
  warped writes the Circles recorded while warped to the card (the snapshot
  is put back in RAM only once the chain ends).
- **R8** One hand-off per menu session: MENU0's own (`unk2 = 0xA`).
- **R9** The script scan (`pc/tools/fast_travel_scan.py`, the
  `fast_travel_scan` suite) walks every room and event script (901) op by op
  with the interpreter's op lengths, and reads every op that tests or writes
  a state flag (0x40, 0x41, 0x46, 0x50, 0x51 and its swap, 0x1F, 0x52, 0x56,
  0xB2, 0xB6). Findings: every Circle's own room script records it
  (`savePointStates[i] = 1`, all 35); the Lich's event (0057.EVT) sets
  `unkB5` to 1, the only write of it; no script tests `unkB5` or writes a
  Circle state other than 1; 0049.EVT sets `unkA7` and 0065.EVT clears it
  (a stretch where saving, and so travel, is refused). Nothing needs
  mirroring, so there is no `pc/assets/fast_travel/blocks.json`.
  What the scan cannot show: one-way stretches before the Lich. The original
  never had to stop teleportation there, so a stretch the story means to be
  one-way (a door locked behind Ashley, a scripted fall, a chase) need not
  touch any flag the scan reads; "none found" covers only the flags travel
  itself reads. Travel cannot strand Ashley (it lands on a recorded Circle
  and is free by default, so she can always travel back); the unchecked risk
  is returning into an earlier region's room scripts in a story state they
  never saw. Closing it needs a hand review of the stretches between the
  first Circle and the Lich, or a tour on a card from that part of the game
  (no such fixture card yet).

The stock list still offers the Circle Ashley stands on; choosing it plays
the effect and returns to the field in the same room, as the stock game does.

## The Travel button

A binding slot beside the 16 PS1 buttons (`PC_BIND_TRAVEL`; settings
`bind_key_travel`, no key by default, and `bind_pad_travel`, `back` by
default, the element Select has). It may share a key or element with a PS1
button and never swaps with one on the Bindings page; the page's Unbind
Travel row takes it off its keys and gamepad button (Select then only zooms
the camera, on a Circle too). When it opens the
list, the PS1 buttons on the same key or element are taken out of that
frame, so Select does not also toggle the camera's zoom. When it would not
open the list (off a Circle, travel off, refused) nothing happens: no sound,
and Select does what it always does. It opens MENU0 as an L2 shortcut opens
a school's list (`func_800CACD0(1, 0)`, `vs_battle_shortcutInvoked` 9,
MENU0's `handleShortcut` going straight to `_teleportMenu`).

The prompt (`fast_travel_prompt.cpp`, modern UI only: with `ui_menus` off
the classic look keeps the game's own sprites and layout): the Travel
binding's icon for the device used last and "TRAVEL", while the button
would open the list. It is drawn like the HUD's own words: the HUD labels'
typography (settings `ui_text_labels_font`, `_stretch`), the icon and the
verb outlined, no plate. Its look is settings (README "Fast travel":
`qol_fast_travel_prompt_size`, `_stroke`, `_color`, `_stroke_color`,
`_stretch`, `_backdrop` for the menus' hint plate again, and
`ui_prompt_icon`, `_gap`, `_tracking`, shared with the menus' hint bar), and it is placed in the game picture, not the
window: in 4:3 the window's side bars are left alone. By default it sits in
the picture's upper right, clear of the HUD's HP / MP in the upper left. With `button_prompts original` (the PS1's own glyphs) a pad shows
the PlayStation glyph of Travel's element, and the keyboard shows Travel's
key (Travel is no PS1 button: Select's glyph would name a key that only
zooms). The Travel prompt row is never greyed: after the game's own unlock
the button and its prompt work whatever `qol_fast_travel` says. Settings
`qol_fast_travel_prompt` (on); `qol_fast_travel_prompt_anchor` (2): where in
the picture it goes, 0 top left, 1 top, 2 top right, 3 left, 4 centre, 5
right, 6 bottom left, 7 bottom, 8 bottom right (the plate's same corner or
edge sits on that point); `qol_fast_travel_prompt_offset_x` / `_y` (-0.025,
0.035): moved from there by fractions of the picture's width and height, +
right and down; `qol_fast_travel_prompt_scale` (1: x
`qol_fast_travel_prompt_size`); `qol_fast_travel_prompt_hold` (8): vsyncs it
stays after the field's last poll offered travel. The dev overlay's
Graphics > Gameplay has them (Prompt anchor, offset x / y, size, hold,
backdrop, text size, outline, stretch, colours, icon size, gap, tracking),
and RPC `fast_travel {set: {prompt_anchor, prompt_offset_x,
prompt_offset_y, prompt_scale, prompt_hold, prompt_backdrop, prompt_size,
prompt_stroke}}`; the report has the prompt as last drawn (`prompt_box`,
window pixels), the `picture` it was placed in, `prompt_font` (`labels`: the
HUD labels', `default`: the menus' label font when that file is missing),
`prompt_backdrop` and `prompt_stroke`. Settings files from before the
anchor have `qol_fast_travel_prompt_x` / `_y` (the centre in fractions of
the window, 0.5 / 0.84 by default then): they are read and dropped, so the
prompt takes the anchor's default, and the next save writes only the new
keys. Hidden over the editor, when the device
used last has nothing bound to Travel, and whenever the field loop is not
taking input (a menu open, an action playing: an enemy's attack holds the
field loop, and the button waits for it as Triangle does).
The prompt is also the HUD layer's `travel` element (`pc/docs/Extensions.md`,
"HUD"): the shipped `rood.hud` plugin claims it and draws the same layout
from these settings in Lua (`pc/plugins/rood.hud/main.lua`); the C++ drawing
above is the fallback (the plugin off, or its draw failing).
`rood.dev.show("travel")` (RPC `hud {show, kind: "travel"}`) forces the
prompt in the field without a Circle of Magic until `rood.dev.clear`
(`pc_fast_travel_force_prompt`; the button still does only what the field
allows).

The rules read the game only from a loaded room: the RPC report can come at
any vsync, also inside a room load's wait loop, where `func_8008A4FC` would
read a collision map that is not there yet (`fast_travel.c` `ashley()`).

## What the card holds

Only the game's bytes. The port never writes `unkB5`, `menuFlags` (the Magic
command bit is shown, never stored), `savePointStates` (except restoring its
own snapshot around Room Browser warps) or `magicUsed`. A save after a trip
is the save the original writes on that Circle: `saveLocation` the
destination + 1, the room record the destination. No sidecar file.

## src hooks

- `src/MENU/MENU0.PRG/15AC.c`: `TELEPORT_UNLOCK` (`pc_fast_travel_unlock()`
  for the four `unkB5` reads), `TELEPORT_COST` (the compare, the display and
  the packing), `TELEPORT_MASK` (free mode's MP-0 bit), the port's refusals on
  the Teleportation row, and the Travel button's entry
  (`PC_SHORTCUT_TRAVEL` straight to the list, Cross closes). On the PS1 build
  every macro is the original's tokens.
- `src/BATTLE/BATTLE.PRG/5BF94.c` `func_800C930C`: `pc_fast_travel_menu_bits()`
  ORed into its local copy of `menuFlags`; the Magic command's mask through
  `pc_fast_travel_mask`.
- `src/BATTLE/BATTLE.PRG/146C.c` `func_800735F8` case 10: `magicUsed` only
  when `pc_fast_travel_begin` says so.
- `src/MENU/MAINMENU.PRG/C48.c` `vs_mainMenu_exec`: the Magic title's mask
  through `pc_fast_travel_mask` (`MAGIC_TITLE_MASK`, `0xB7` on the PS1).

## Settings and RPC

| Setting | Default | Meaning |
| --- | --- | --- |
| `qol_fast_travel` | 1 | offer Teleportation from the first Circle once the prologue is over |
| `qol_fast_travel_cost` | 0 | before the game's own unlock: 0 free, 1 flat, 2 scaled, 3 the game's |
| `qol_fast_travel_flat` | 15 | flat mode's MP |
| `qol_fast_travel_scale` | 0.5 | scaled mode's factor on the game's cost |
| `qol_fast_travel_prompt` | 1 | the Travel prompt on a Circle |
| `qol_fast_travel_prompt_anchor` | 2 | where in the game picture the prompt goes: 0-8, top left to bottom right (2 top right) |
| `qol_fast_travel_prompt_offset_x`, `_y` | -0.025, 0.035 | moved from the anchor, fractions of the picture (+ right, down) |
| `qol_fast_travel_prompt_scale` | 1 | the prompt's size (x `qol_fast_travel_prompt_size`) |
| `qol_fast_travel_prompt_hold` | 8 | vsyncs the prompt stays after the field last offered travel |
| `bind_key_travel`, `bind_pad_travel` | none, `back` | the Travel button |

The modern menus' System > Gameplay (Fast travel, Travel cost, Flat cost /
Cost scale, Travel prompt) and System > Bindings (Travel), the editor's
Graphics > Gameplay panel (every setting, the prompt's place and size).

RPC `fast_travel {set, press, audit}`: `set` takes `enabled`, `cost` (0..3 or
`free` / `flat` / `scaled` / `game`), `flat`, `scale`, `prompt`; `press`
presses the Travel button once (taken by the field loop's next frame, within
4 s); `audit` adds each destination's Circle read from the disc. The reply:
the settings, `whose` (`off` / `port` / `game`),
`unlock` (what MENU0 reads), `menu_bits`, `prompt`, the state the rules
read, `blocked` (the port's refusal of the row) and `why` (the Travel
button's reason, "" when it opens the list), each point's state, raw and
applied cost, `current` and `refused`, the press counters and the last trip
(checked on arrival: room, Circle, MP). `[travel]` log lines record each trip
and its arrival.

## Tests

- `fast_travel_probe` (CTest): MENU0's tables against `84.c`, the packing
  against `15AC.c`'s own arithmetic, the cost modes, whose unlock, the mask,
  the refusals, the Travel button's guard, the JSON.
- `fast_travel_scan`: the script scan (R9).
- `fast_travel_smoke` (original menus) and `fast_travel_shell_smoke` (modern
  menus), from the pinned card's save room (Circle 2): `status` (defaults,
  the points, each cost mode, the Magic command not stored), `audit` (R6),
  `warp` (R7 and the snapshot), `menu` (travel to the nearest Circle: the
  hand-off, the arrival, MP, `magicUsed`, `unkB5`, `menuFlags`,
  `savePointStates`), `save` (travel, save as file 2, decode, Continue),
  `refuse` (the prologue, R3, R1, an event, MP with the game's cost, free at
  MP 0, off the Circle; with the modern menus each through Magick >
  Teleport's own refusal), `game` (`unkB5` 1 and 2), `here` (the Circle you
  stand on), `button` (the default binding on a virtual gamepad, RPC system
  `virtual_pad`; a key shared with Select; the RPC press; Cross; a refused
  press), `prompt` (original mode too), `settings` (the settings file),
  `reload` (a warp chain open across a soft reset and Continue: its snapshot
  dropped). On the pinned test card a case that cannot run its checks fails.
- `controller_probe` (the Travel slot), `ui_settings_probe` (the rows,
  Unbind Travel).

Not tested: that A to B to A gives the same room state as a door entry (the
arrival is the door's own load path, `func_8007C694` writing the door record
the door loads use); the music (no RPC readout; the zone change switches it
in `_finishLoadZnd` as for a door); a tour of every Circle on a late-game
card (it needs a fixture card the pinned set does not have).

## Not yet

The modern map picker: destinations grouped by area, the area map, a
confirm card and an arrival toast. It waits for the modern-menu work.
