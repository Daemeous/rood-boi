# Extensions: plugins and Lua

rood-boi runs plugins written in Lua 5.4 (vendored in `pc/third_party/lua`).
Milestone 0 is the runtime, its safety nets and the core API; the HUD layer
adds drawing the game's HUD elements, their events and read-only game
queries; M1 adds the Option screens' pages (`rood.menu`). Input comes in a
later milestone. Vanilla stays vanilla: the shipped example is off; the
shipped `rood.hud` (on) draws the HUD's words with the modern look, and
off, the game draws them; the shipped `rood.classic` (on) draws the classic
look's timing words, chain counts and HP / MP / RISK gauges with the game's own pieces, packet for
packet as the game does (the parity gate), its typography and 16:9
placement a hot-reloaded table, and off, the game draws them; the shipped
`rood.options` (on) defines the core Option pages, which the C++ fallback
builds identically without it; the shipped `rood.photo` (on) is the photo
mode, opened by its own button (F12), doing nothing until then; the shipped
`rood.dev_example` (off) is a developer tool window in Lua ("Developer tool
windows"), and the shipped `rood.material_review` (off) the Material review
window in Lua, replacing the C++ one while on; the shipped `rood.materials`
(off) is Window > Materials in Lua, replacing the C++ one while on.

## Where Lua runs

Single-threaded, on the game thread (`rl::run` refuses any other):

| Entry | From | When |
|---|---|---|
| `pc_lua_init` | `main()` (`boot_runtime.c`), after `settings.cfg` loads | once |
| `pc_lua_tick` | `pc_tick()`, after the RPC | every game vsync while the game runs (not while the RPC has it paused) |
| `pc_lua_frame` | `pc_overlay_frame()`, the ImGui frame open | every presented image (interpolated in-betweens, paused redraws) |
| `pc_lua_rpc` | RPC method `lua` (`dev_rpc.c`) | between frames |
| `pc_hud_classic_draw` | the game's HUD hooks (`PC_CLASSIC`, `hud_bridge.c`), inside its frame | a claim in the classic flavour, per element drawn (in the parity mode `pc_hud_classic_parity_draw`, into a shadow slot) |
| `pc_lua_shutdown` | `atexit` | writes stores and plugin settings; runs no Lua |

Never on the audio callback thread or helper threads. Save states
(`snapshot.c`) do not include Lua: plugins keep their state across a resume
(a restart, RPC `restart` or `rood.dev.bench.load`, loads them again).

## Plugins

A plugin is a folder named after its id, with a manifest and a main chunk:

```
pc/plugins/rood.example/      shipped (committed; rood.hud, rood.classic, rood.options too); ROOD_PLUGINS_SHIPPED overrides, "" for none
pc/user/plugins/<id>/         yours (not committed); ROOD_PLUGINS overrides
pc/user/plugin_data/<id>/     its storage, its settings.json; ROOD_PLUGIN_DATA overrides
```

`plugin.lua` (`return { ... }`, evaluated with no libraries) or `plugin.json`:

| Field | | |
|---|---|---|
| `id` | required | lowercase words joined by dots, at least two (`author.plugin`); `rood.*` is for shipped plugins |
| `api_version` | required | the API it was written for; this build has 1 |
| `name`, `version` (`x.y.z`), `description`, `author` | | |
| `entry` | `main.lua` | its main chunk |
| `tests` | `tests` | the folder of its Lua tests |
| `default_enabled` | `true` | on until the player turns it off |
| `depends` | | ids, or `{ id = "a.b", version = ">=1.2.0" }`; they load first |
| `capabilities` | | what it asks for beyond the sandbox (below) |

A main chunk may `return` a table: the plugin's module. The host keeps it
for the REPL (its `M`, and the exports `lua repl` lists); nothing else reads
it. `rood.hud` returns `{draw, look, style, refresh}` and its claims call
`M.draw[kind]`, so a draw function replaced there draws from the next frame.

Load order: dependencies first, then `plugin_order`, shipped before yours,
then by id. A plugin whose dependency is missing, off or failed waits and
loads when it can.

Hot reload: every `plugin_watch_vsyncs` ticks (and every 0.5 s of frames,
so also while paused) the code files' times and sizes are compared; a
changed plugin and what depends on it lose everything they registered
(handlers, timers, registry entries, settings) and run again in a fresh
state. Editing a file in `tests/` does not reload (the next run reads it).

## The sandbox

Each plugin has its own Lua state: the base, coroutine, table, string,
math and utf8 libraries; no `io`, `os`, `debug`, `package` (not even
linked), `dofile`, `loadfile`, `warn` or `string.dump`. `load` takes text
only. `require("a.b")` loads `a/b.lua` from the plugin's own folder (no
hidden files, at most 8 folders deep). `print` writes to the log.
`collectgarbage` only counts, collects and steps. `setmetatable` refuses a
`__gc` field: Lua runs finalizers with the debug hooks off, outside any
budget (use the `unload` event or a `<close>` variable). No link (symlink)
leads a read, a `require`, the entry or a write out of the plugin's folder
or its data folder (checked on Linux and macOS; Windows links need
privileges and are not checked).
`getmetatable("")` and `getmetatable(rood)` give `"locked"`; `rood` and its
tables are read-only. Values that go to another plugin (events, registry
entries) are copies (at most 32 MB, 100000 parts, 32 levels); functions in
them run in their own plugin. What the host keeps for a plugin is bounded
too: log lines and error messages are cut at 16 KB, its registry entries
hold at most `plugin_memory_mb` (4096 entries, 32 ids per `after` /
`before`), a setting's spec 64 KB (256 per plugin). Paths never name a
Windows device (`CON`, `COM1`, ...) and only regular files are read.

## Budgets and faults

| Setting (`settings.cfg`) | RPC `budget` | Default | |
|---|---|---|---|
| `plugin_budget_ms` | `budget_ms` | 4 | one handler, timer or call between plugins: the plugin's own Lua, on the game thread's CPU clock (below) |
| `plugin_host_budget_ms` | `host_budget_ms` | 100 | the host's work inside `rood` calls a handler may make on top, uncounted |
| `plugin_budget_instructions` | `budget_instructions` | 2000000 | |
| `plugin_load_budget_ms` | `load_budget_ms` | 250 | a main chunk, an eval, a test step |
| `plugin_load_budget_instructions` | `load_budget_instructions` | 50000000 | |
| `plugin_memory_mb` | `memory_mb` | 32 | each plugin's allocator refuses past it |
| `plugin_fault_limit`, `plugin_fault_window` | `fault_limit`, `fault_window_s` | 5, 10 | that many faults in that many seconds disable a plugin |
| `plugin_fault_cooldown`, `plugin_fault_revivals` | `fault_cooldown_s`, `fault_revivals` | 20, 3 | a shipped plugin disabled by faults loads again after that many seconds; at most that many revivals (cool-down or button) in 10 minutes |
| `plugin_watch_vsyncs` | `watch_vsyncs` | 30 | hot reload checks |

What counts is the plugin's own time: the budget clock is the game
thread's CPU time (Linux; Windows' thread times are too coarse, there it is
the monotonic clock), so time the thread did not run (the OS gave the core
to another process, a throttled CPU, a wait on the GPU) is never a plugin's,
and the host's work inside every `rood` function (drawing, queries, a font
baked, the renderer asked) is excluded, up to `plugin_host_budget_ms` a call
(past that it counts, so a loop of costly calls still stops). RPC `budget
{slow_call = {name, ms, plugin}}` makes a `rood` function that slow on the
host's side (tests); `ROOD_LUA_SLOW_MS=N` logs every `rood` call slower than
N ms (wall clock), with its name. The over-budget message gives the plugin's
time and the host work excluded.

Once a call is over budget, every following instruction fails until the
call returns, so `pcall` cannot keep a runaway alive. The C loops a
plugin can make long check the clock too (string patterns, plain `find`,
`table.insert/remove/move/sort`: `pc/third_party/lua/ROOD-NOTES.md`), and
every `rood` function checks it on entry, so a loop of costly calls stops
at the first one past the budget. Errors are caught with a traceback (the
log, the console, stderr `[lua] ...`). Disabled plugins come back when
their files change or with a reload; one disabled by faults comes back by
itself too, so a run of slow frames never leaves a dead button: a plugin
that listened to `input.action` loads again when that action's button is
pressed (before the event reaches it: the photo mode opens on the same
press), a shipped plugin after `plugin_fault_cooldown` seconds, at most
`plugin_fault_revivals` times in 10 minutes (then it stays off until its
files change). After such a load it gets `plugin.recovered {reason, by}`
(`by`: `action`, `cooldown`). A panel draw over its budget is drawn again
next frame (one that raises an error stays off until the plugin sets it
again or reloads). Known limits: one C call without Lua
instructions (a large `table.concat`, a `..` or `<` of two 16 MB strings)
overruns by its own length, and up to 1000 of them between two checks of
the hook; memory is capped, which bounds them (about a second at worst).

## Capabilities

Declared in the manifest, granted by the player per plugin and per
capability, only while "Allow plugins to request elevated permissions"
(`plugins_elevated`) is on, and only for the code they were granted for
(`plugin_grant <id> <sha256 of its code> <capability>`): a changed plugin
is asked again. The hash covers every file code can be loaded from:
`*.lua`, `*.fnl` and `plugin.json` in any case, at most 8 folders deep,
hidden ones never (they cannot be loaded); a plugin with more than 2000
code files is refused. It does not cover data: a plugin that `load`s text
it reads (its storage, `rood.storage.read`) can change what it does
without a new hash, which a reviewer of its code can see. Revoking takes the functions away at once (the plugin
reloads). Every elevated call is logged (`<plugin_data>/audit.log`).

| Capability | M0 | Functions |
|---|---|---|
| `fs.read`, `fs.read:<folder>` | yes | `rood.fs.read`, `exists`, `list`: the plugin's folder and the granted folders |
| `fs.write`, `fs.write:<folder>` | yes | `rood.fs.write`, `remove` |
| `net:<host>`, `exec`, `memory.read`, `memory.write`, `flags.write`, `native` | declared only | exist when granted and refuse: "not available in this version" |
| `game.pause`, `camera`, `post`, `ui.hide`, `capture`, `input.capture` | the photo mode foundation | below ("Photo mode foundation"); not elevated |
| `dev.tools` | yes | `rood.imgui`, `rood.dev.window`, `rood.dev.rpc` (below, "Developer tool windows"); not elevated; in dev mode declared is granted |

The photo mode's capabilities are not elevated: they need their grant (tied
to the code's hash, as every grant) but not `plugins_elevated`, and a shipped
plugin (`pc/plugins`) has the ones it declares. `rood.plugin.elevated` is
true only for an elevated grant.

The grant dialog comes with the menus; in M0 grants are made with RPC
`lua {action "grant"}` (dev mode) or by editing `settings.cfg`.
Write grants refuse folders containing the active settings file (or the default
`pc/user/settings.cfg`), shipped/user plugin roots, or the plugin-data root,
including their ancestors. Existing symlinks and existing prefixes of new
folders are resolved before checking; unresolvable paths fail closed. Settings
`plugin_grant` entries follow the same policy, and write scopes are rechecked
when used. Grants within an individual plugin directory remain possible.
Grants outside the recipient's own plugin and data directories emit a warning
and appear in `lua plugins` as `external_filesystem_grants` (an array of grants).
This warns about cross-plugin scopes too; it does not make them safe. Path
checks are not a defense against another process racing filesystem changes.

## API (api_version 1)

| | |
|---|---|
| `rood.log/warn/error(...)` | to the log and the console |
| `rood.version`, `rood.api_version` | |
| `rood.plugin` | `id, name, version, dir, data_dir, shipped, capabilities, granted, missing, elevated` |
| `rood.on(event, fn) -> handle`, `rood.off(handle)`, `rood.emit(event, payload) -> count` | `fn(payload, sender)`: sender is the emitting plugin's id (nil for the core's events), since a plugin may emit in another's namespace (a request); plugins emit namespaced events (`my.plugin/changed`); core events: `tick {vsync, game_time}`, `frame {dt, real_time}`, `plugin.loaded/unloaded {id}`, `registry.changed {kind, id, action}`, `unload` (to the plugin itself), `input.action {name, device}` (a port action's button pressed, whatever captures the input: `photo`, bind_key_photo F12 / bind_pad_photo back+dpup; device `keyboard` or `pad`), `plugin.recovered {reason, by}` (to a plugin loaded again after a fault disable), `photo.released {what, reason}` (to its owner: below) |
| `rood.timer.after/every(seconds, fn[, {clock = "real"}]) -> id`, `rood.timer.cancel(id)` | game time by default (stops while paused) |
| `rood.clock.game()`, `rood.clock.real()` | seconds |
| `rood.registry.register(kind, id, data[, {after, before, group}])`, `get`, `list(kind[, {group}])`, `remove`, `kinds()` | ids in the plugin's namespace (`<plugin id>/<name>`); `after`/`before`: ids or `group:<name>` |
| `rood.storage.get/set/remove/keys/flush`, `read(path)`, `read_data/write_data/list_data` | plain data, 1 MB; `read` reads the plugin's folder |
| `rood.settings.get/set/reset/run/list([{page}])/pages()/define(name, spec)/on_change(id, fn)/off_change` | System rows (`"skip_intro"`), schema kinds (`"<kind>/<field>"`), the plugin's own (`"plugin:<id>/<name>"`; another plugin's are read and watched, never changed), each validated by its registry; settings.cfg keys without a row are read and watched, never set: the plain numeric ones (`"qol_fast_travel_prompt_size"`; several numbers, a colour: a table) and ui_text's (`"ui_text_status_size"`, a font's path as text) |
| `rood.test.case(name, fn)`, `eq`, `ok`, `near`, `raises`, `skip`, `wait(ticks)` | in `tests/*.lua` |
| `rood.menu.page{...}`, `action`, `remove`, `pages`, `rows`, `actions`, `generation` | the Option screens' pages (below) |
| `rood.game.pause/resume/paused`, `rood.camera`, `rood.post`, `rood.ui`, `rood.capture`, `rood.input` | the photo mode foundation (below), each with its grant |
| `rood.imgui`, `rood.dev.window/window_open/remove_window/rpc` | developer tool windows (below), the dev.tools grant |

## HUD: claims and drawing

The visible layer (plan `pc/docs/plans/2026-10-18-lua-visible-layer.md`;
`pc/src/hud`, `pc/src/lua/lua_api_hud.cpp`). The game decides when and what
(its HUD elements, copied each frame into a fixed pool); a plugin's claim
decides how it looks. The kinds:

| Kind | What | Fields beyond the common ones |
|---|---|---|
| `status` | the battle's timing word (TOO FAST!, FAST!, SLOW!, TOO SLOW!, COOL!, ...) | `timing` (`too_fast`, `fast`, `success`, `slow`, `too_slow`), `variant` (the success word) |
| `chain` | 3 CHAINS | `value` (the count) |
| `buff` | an equipment's class or affinity change: WEAPON HUMAN +1 | `slot` (`WEAPON`, `SHIELD`, `R.ARM`, `L.ARM`, `HEAD`, `BODY`, `LEGS`), `stat` (`HUMAN` .. `EVIL`, `PHYSICS` .. `DARKNESS`), `sign` (+1, -1), `value`, `parts`: the two words', the sign's and the digits' boxes |
| `damage` | a number over an actor: damage, healing, a stat's change | `value`, `sign` (-1 damage, +1 healing), `pool` (`hp`, `mp`, `stat`), `bounce` (the digits' rise this frame, the rightmost's; each to its left half the one before), `stat` (0..5 with `pool` `stat`), `parts`: the digits' box (a stat's: its icon's word STR, INT, ..., the sign, the digits) |
| `travel` | the Travel prompt on a Circle of Magic (the modern look) | `glyph` (the button: `travel`), `corner` (0..8: which of its corners goes on `x, y`, top left .. bottom right), `unit` (game pixels per layout unit of the menus, 720 high) |
| `notice` | a line of the port's own news: the save state slots' ("Saved to slot 3", `pc/src/savestate_notice.cpp`; never the classic flavour) | `slot` (the save state slot, 0 none), `corner` (which of its corners goes on `x, y`: 1, its top middle), `unit` (game pixels per layout unit of the menus, 720 high) |
| `name_window` | the action's name in text box 7 (vs_battle_displaySceneMessage: a band across the bottom); an event, never claimed: the game draws it | `index` (the action, `vs_main_actions`), `message` (the scene message: 11 a technique's name, 12 a spell mastered, ...) |
| `gauges` | the classic look's only (claimed with `{flavour = "classic"}`; the modern look hides the status display): the status display's HP / MP / RISK gauges, their digits and labels, or RISK ZERO (5BF94.c func_800CA2DC's `_renderStatBar` calls); up whenever the field is (no show) | `hp`, `max_hp`, `mp`, `max_mp`, `risk` (Ashley's HUD record), `slide` (D_800EB9B0: the display sliding up, 0x80000 a step of 8 pixels in its high half) |
| `actor_word` | the classic look's only (the modern look: the game's own): a word over an actor in the label loop (2842C.c): MISS (func_8009506C) or a combat label, the ailments' and buffs' words (func_80095258, with a lowered stat's arrow); the game decides which and when (no show) | `word` (`miss`, `label`), `label` (the combat label: D_800690B4's index), `lowered` (a lowered stat's word: no slide, the arrow), `age`, `call_x`, `call_y` (where the loop placed it) |
| `timer` | the classic look's only: the time attack's timer (5BF94.c `_renderTimer`, while `vs_battle_lowerScreenUiState` is 1: the game decides; no show) | `text` (the game's string, mm:ss:hh: 59:59:99 at most), `minutes`, `seconds`, `hundredths` (as it shows them), `call_x`, `call_y` (128, 200) |
| `emote` | the classic look's only: an emote over an actor (2842C.c func_80095C18: the balloon with one to three dots, the shout's '!', the heart, the surprise strokes, '?' alone; an off-screen actor's arrow, func_8009651C); the game decides which, where and when (no show; a far actor's sprite stays the game's) | `draw` (`balloon`, `arrow`), `emote` (the kind, 1..7), `side` (0..5: the balloon and the way it faces), `z` (its depth: the slot `unk4 + z` words, the kind's own layer), `direction` (the arrow: 1 left, 2 right, 4 up, 8 down and pairs), `actor`, `call_x`, `call_y` (the actor's place on the screen; the arrow's clamped) |
| `text_frame` | the classic look's only: a text box's frame, the talk bubbles' and the information box's shapes (5BF94.c func_800CD3E4, in func_800CB83C), the box's entries of the text boxes' own ordering table; the game opens, sizes and closes it (no show) | `box` (0..7), `box_x`, `box_y`, `box_w`, `box_h` (its place and size this frame, the zoom included), `shape` (the frames' table), `flip`, `tail` (the speaker's part) |
| `text_band` | the classic look's only: a text box's band across the screen, the name window's (5BF94.c func_800CD158: the picture behind darkened row by row, black past it, the action's icon once settled); the game opens and closes it (`rood.dev.show("name_window")` shows one) | `box`, `top` (grown from the top), `edge` (the rows grown), `lines`, `settled`, `icon` (the box's icon field) |
| `box_text` | the classic look's only: a text box's text as the game's printer sets it this frame (5BF94.c `_printVariableWidthFont`: its glyphs, recorded as it sets them; the typewriter, pages, the continuation arrow, sounds and button reads stay the game's) | `box`, `glyphs` (6 integers a glyph, in the order set: code, font table, colour style, x, y, brightness; one table, reused), `value` (the glyphs' count) |
| `image` | the classic look's only: a portrait or still (5BF94.c func_800CCA90: New Game's opening cards; its tiled drawer, D_800EC25C[0]: the other drawers, the credits' wipes and such, stay the game's until a scene the gate plays shows them); the game decides (no show) | `image` (0..2), `drawer`, `clut` (its CLUT word) |
| `target_row` | the classic look's only: a row of the targeting list (58578.c func_800C1DC4: a part's name by the raw printer on its plate, a gradient with a black shadow), in the slot the game linked it into (`vs_scratch.unk8 + 8`, the selected `+ 4`); the targeting decides which rows, their slide and the plate's fade (no show) | `text` (the name as the game's string: `#` right-aligns), `call_x`, `call_y` (the row's place, the list's offset in y), `selected`, `fade` (the plate's step 0..8, before the game steps it), `bank` (its colours: 0, 1) |
| `target_line` | the classic look's only: the selected row's leader lines (58578.c func_800C1DC4's func_800C16FC, on the left half func_800C1A40: from the part's point to the row, swept in by their age; from age 18 a second behind), in `vs_scratch.unk8`; the targeting decides (no show) | `call_x`, `call_y` (the line's start: the part's point projected), `row_x`, `row_y` (the row's place), `age` (the sweep: the row's unk3) |
| `target_marker` | the classic look's only: the area target's marker (58578.c func_800C28AC: three sprites of the field UI page, one pulsing; with the pitch free, the brackets' two quads), slot -4; the targeting decides (no show) | `call_x`, `call_y` (the point projected), `lock` (the call's arg1: the pitch locked, no brackets, the other sprites), `step` (the pulse's brightness step, 0..15) |
| `menu_row` | the classic look's only, and only in the screens the gate tours (Items, MENU3, and the Option screen, MENU8; every other menu screen keeps the game's drawing, no element, until its tour is green; the same for `menu_cursor`, `menu_command`, `menu_tabs`, `menu_background`): a row of MAINMENU's shared frame (58EC.c `_renderMenuItems`' emit, every menu screen's: its window, label, counts, icons, scroll arrow; its slide stepped by the game first), in `vs_scratch.unk8 - 4 .. + 12` (the row icon at `+ 4`); one element per row (0..39) | `row` (a copy of the record before the emit: `id, state, x, y, w, background, gradient, scrollable, selected, unselectable, button, icon, color, invert, material, outset, page, count, max`), `call_x`, `call_y` (its place) |
| `menu_cursor` | the classic look's only: the menus' pointing hand (`vs_mainMenu_renderCursor`), in `vs_scratch.unk8`; also BATTLE's hand (5BF94.c `vs_battle_drawCursor`: the same packets, colour 0, at x 180) on Setup's and Loot's Yes / No and row menus (MENUC, MENUB; hud_bridge.c `BATTLE_CURSOR_SCREENS`: not Battle Abilities, Data, the rename grid or the battle command list yet) | `call_x`, `call_y`, `step` (its pulse, 0..15), `color` (0, 1: its CLUT) |
| `menu_command` | the classic look's only: the command title's sprite (`_renderMenuCommand`: OPTIONS and the others), in `vs_scratch.unk8` | `action` (1..6), `call_y` (its slide) |
| `option_value` | the classic look's only: a row's value in our Option pages (`pc/src/ui/classic_options.c` draw_values, the game's menu font and gauge: ON / OFF, a choice between arrows, a slider, a binding's or an info's text, a link's arrow, a note), in `vs_scratch.unk8 - 4` and `+ 8`; one element per visible row; `classic_options.c` stays its fallback | `option` (`row, kind, enabled, focused, index, right, lit, other, gap, bar_colour, bar_width, fill, link, upper, window_x, value, on, off, label`: what the page draws it from, its numbers rood.options' `M.style`), `call_x` (the value column), `call_y` |
| `menu_tabs`, `menu_background` | the classic look's only (toured in Items): the tab arrows and L1 / R1 (`vs_mainMenu_renderTabNavigation`); the menu background (`_renderBackground`), its own slot | tabs: `mode`, `step`, `lit`; background: `fade` (0..16), `brightness`, `ot_slot`, `buffer` |
| `items_category`, `items_status_button` | the classic look's only, on the Items screen (MENU3, currentMenu 4) and Loot's Organize inventory (MENUB, 12: items0.h, and its own copy of the strip, 260.c, hooked the same); the icons on Storage too (MENUD, 14: its own copy, 234.c `func_80103E24`, the dim icons in `unk4 - 12`): its eight category icons (items0.h `_renderItemCategory`'s loop: the others in `vs_scratch.unk4`, the selected one raised in `unk8` while highlighted, or handed to row 31 to draw; the tab arrows it calls are `menu_tabs`) and the STATUS strip (16C.c: the button's window, `vs_mainmenu_renderButtonBackground`, in `unk4 - 12`; the Square button and the word in `unk8`) | `items`: icons `step` (the loop's y: `animationStep * 8 - 16`), `slot` (the dim icons' layer: 0, MENUD's -12), `highlight`, `selected` (0..7, 7 the Manage page), `on_row` (the icon row 31 draws instead, -1 none); strip `row` (its slide), `state` (1 lit) |
| `equip_stats`, `dp_pp_bars`, `gem_slots` | the classic look's only, on the screens whose tours cover them (hud_bridge.c `PANEL_SCREENS`): the stats panel and the bars on Items (its status views and Equip menus), Status (MENU4: a piece's detail, a part's stats), Setup (MENUC), Storage (MENUD's status views) and Loot (MENUB's Organize inventory); the gem slots on Items, Loot and Storage (MENUD's own copy, 234.c `_gripNavigation`, hooked the same): MAINMENU's stats panel (C48.c `vs_mainMenu_renderEquipStats` after its slide is stepped: ORG/EQP, RANGE and RISK, STR / INT / AGI or their ATTACK / DEFENSE names, each value and its equipped total, coloured by its threshold; `vs_scratch.unk4 - 24`) and DP / PP bars (`vs_mainMenu_renderDpPpBars` after its step: the words, maximum, slash and current value, the gauges; `unk4 - 12`); a grip's gem slots (items1.h `_gripStatusView`: the gem icon in `unk8 + 4`, `X     n` in `unk8`) | `equip` (`x, y` the panel's place, `known`, `range_risk` (shown), `range, risk, range_delta, subtype`, and per stat (1..3) `stat, threshold, base, total, accessory`: the values as the panel reads them), `bars` (`step, target` (1 DP, 2 PP), `dp, max_dp` (hundreds), `pp, max_pp`), `gems` (`count`) |
| `stat_page` | the classic look's only, where the stats panel is (above): the class / affinity / type table of a status view, an Equip menu, Status's details, Setup's lists (C48.c `_renderStatPage` as `vs_mainMenu_drawClassAffinityType` calls it, sliding in: each row's name, value (comparing: the difference and `+` / `*`), its gauge of four shaded lines, frame and shading, the draw modes and shaded lines between, in `vs_scratch.unk4 - 12`; the Square button and the header CLASS / AFFINITY / TYPE in the text boxes' font, `unk4 - 16`) | `page` (`x_offset` (its slide, 0 or less), `page` (0 class, 1 affinity, 2 type), `enabled` (the pages' bits), `subtype`, `stats` (a copy of `vs_mainMenu_equipmentStats`, 0..63, as the game indexes it)) |
| `ok_mark`, `storage_buttons`, `storage_counts`, `loot_capacities`, `discard_count` | the classic look's only, each on its screen (hud_bridge.c `PANEL_SCREENS`): the pulsing OK beside a menu's last choice (MENUC 168.c `_drawOk`: Setup's Assemble, Gems, Combine; MENUD 234.c `func_801089BC`: Storage's top menu; `vs_scratch.unk4 - 12`); Storage's button strip (MENUD `vs_menuD_exec`: two button windows in `unk4 - 12`, Square / Triangle / Cross and STATUS / CANCEL / END in `unk8`) and its counts (`func_80106464`'s `capacity / count` per category, in `unk8`: the strip's, below the bag's or the container's name, the last row the transfer list's; or the top menu's, `func_80106504`: GADGET BAG and CONTAINER side by side); Loot's capacities strip (MENUB 260.c `_displayInventoryCapacities`: GADGET BAG, each category's name, `/`, capacity and count, coloured by the row looked at; the GET / ALL / END windows, buttons and words); a misc item's discard amount (items1.h `_discardMenu`'s picker on Items, MENUD's copy on Storage: three digits in the text boxes' font, style 5, `unk8`) | `ok` (`x, y`, `level`: the pulse's grey, `(rsin(step * 8) >> 5) + 64`); `storage` (buttons: `x` the slide, `state` (bits 0..1 light STATUS, bit 2 dims END); counts: `layout` 0 the strip, 1 the top menu, `x` the strip's slide, `container` (0 the bag, 1 the container), `capacity[col][i]`, `count[col][i]` (col 1, 2: the bag's, the container's; i 1..7)); `loot` (`x` the slide, `controls`, `looking` (0 none, 1 the leave row, 2 an item), `all` (ALL lit), per category (1..7) `capacity`, `count`, `incoming` (what the item looked at adds)); `discard` (`count`, `x, y` the units' place) |
| `file_menu`, `file_element` | the classic look's only, on the file menu: MENU7's (260.c `_drawFileMenu`, currentMenu 7: Data's SAVE / LOAD, the save list after Storage, the game over's LOAD) and the title's copy (TITLE 22C.c: the title's LOAD, the clear-data save after the ending), which draws **at once** (`DrawPrim` and `LoadImage`, no ordering table: `PC_CLASSIC_IMMEDIATE`, below): its frame in parts (`part` 0: the bottom band's two sprites, under the panels; 1: the band's three shaded rows, the caution sprite, the message's tile, the hand, the message, the screen's fade; 2: the background's two sprites, before the frame, on the title's screens and the game over's: `_drawFileMenuBg`) and each panel (`_drawFileMenuElement`: a card's or a file's: its shaded gradient and shadow tile, its words, and for a file the location picture with its CLUT swapped in and back, No. and the slot, the slot's words or the location's name, MAP %, SAVE, CLEAR, the clear badge, TIME, the HP / MP gauges and digits, the progress animations of a save or load). `d.anchor` and `d.layer` do nothing here: the packets are drawn where the game's would have been, in the game's order | `file_menu` (`part`, `x` (the frame buffer's, 0 or 320: the band's rows read it), `cursor_xy` (packed, 0 none), `cursor_color` (its saturation step, before the game steps it), `fade`, `arrow` (the printer's blink step before), `message` (its codes to 0xE7, nil none)); `file` (`id` 0..9, `x, y, w, h`, `selected`, `unavailable`, `outer`, `inner` (the blend steps as the draw uses them), `slot` (0..4 a file, 255 a card), `location`, `saving`, `arrow`; a file's `slot_state` (0 in use, 1 empty, 2 temporary, 3 saved), `map`, `saves`, `clears`, `hours, minutes, seconds`, `hp, hp_max, mp, mp_max`; `progress` (0 none, 1 processing at `progress_x`, 2 complete in `progress_colour`); the game's words as codes: `text` (the panel's, its 0xFA pairs, to 0xFF), `place`, `in_use`, `empty`, `fresh`; `cluts[1..2]` the two location CLUTs as `d.image_load` takes them) |
| `menuf`, `menuf_load` | the classic look's only, on MENUF's screens (3B8.c, currentMenu 15: the congratulations, the time attack's start and end, the cube puzzle's start, end and quit; reached after a fight, or by RPC `menuf`) and, `menuf` only, the ending's MISSION COMPLETE screen (ENDING D4.c: the same leaves; its words, score, map completion and rank; not the picture under them): `menuf` a frame's sprites as the screen's drawing leaves make them (`_renderTextureFadeIn` and its CLUT, tint, pop-in and wipe kin from `src/renderTextures.h`, the stat wheel's lines and its two draw areas, the PRESS BUTTON prompt: its words by the raw printer in `unk8`, the rest in `vs_scratch.unk4 - 28`); `menuf_load` the screen's VRAM uploads, done **at once** (`PC_CLASSIC_IMMEDIATE`, below: the TIMs and CLUTs `_initMenu` loads, once, and the CLUT row the time attack's end and the cube puzzle's end and quit pulse every frame) | `menuf` (`screen` (the submenu: 0 congratulations, 1 / 2 the time attack's start / end, 4 / 5 / 6 the cube puzzle's start / end / quit; 16 the ending's MISSION COMPLETE), `prompt[1..2]` (the prompt's words: the game's codes), `calls[i]`: the frame's calls in order, each `op` (1 fade, 2 fade with its CLUT, 3 tint, 4 pop-in, 5 wipe, 6 a stat wheel's lines, 7 a draw area, 8 the prompt), `tex` (the picture's index, 255 none) and its `u, v, w, h`, `tpage`, `clut` as the game reads them, `x, y`, `a, b` (fade: the step, the CLUT; wipe: the wipe's edge; a draw area: its `w, h`; the prompt: its slide step, the blink timer), `colour1`, `colour2` (tint, pop-in, wipe: the two colour words, `r \| g << 8 \| b << 16 \| step << 24`)); `load` (`screen`, `phase` (0 `_initMenu`, 1 a frame), `uploads[i]`: `x, y, w, h` and `pixels`, the game's own pixels' handle for `d.image_load`) |
| `rename_grid`, `rename_box` | the classic look's only, on the rename grid (MENU8 21A0.c, currentMenu 8: Setup's Rename after Yes; a new weapon's name after Assemble): the grid (`_renderRenameMenu`, sliding in from the right: the Cross / Square / Triangle buttons and the pad's sprite in `unk8`; each command's bar of 12 shaded lines fading by its counter in `unk4 - 4`; the commands' words in the text boxes' font in `unk4 - 8`; the name typed and the character table in `unk8 + 4`; at rest the L1 / R1 arrows (their digits and letters by the raw printer, their sprites) and the name's window in `unk8`; the four shaded polygons of the background in `unk4 - 4`) and a highlight box on it (`_highlightSelection`: the character under the hand, the name's pen; a semi-transparent shaded polygon in `unk8`) | `rename` (`x_offset` the slide, 0 at rest; `fade[1..4]` the bars' counters, Insert first, 8 the command just chosen; `lit`, `step` the arrows' light and step; `name` its 20 codes, `commands[1..4]` (Insert, Delete, Back, OK) and `table[1..9]` (14 codes a row): the menu charset's codes, for `d.font_text`); `highlight` (`x, y, w, h`) |

Every element: `kind, id, text, x, y, w, h` (the game's drawing's box in its
screen, 320 x 224 in the field, `x, y` its centre; a damage number: where the
game placed it, before its bounce; travel: the picture's point the prompt is
anchored to, the picture as 320 x 240), `rgb`, `age`, `life`, `anchor`
(`centre`, `left`, `right`: where widescreen anchors it; `picture`), `held`,
`synthetic`, `flavour` (`modern`, or `classic`: below); `parts` are `{x, y, w, h, text}`. One
table, reused.

Events: `rood.on("hud.<kind>", fn)` (`hud.status`, ..., `hud.name_window`) fires once per
appearance, at the tick after the game drew it, with the element's fields (a copy) — for logic;
drawing is the claims'.

| | |
|---|---|
| `rood.hud.claim(kind, fn[, {flavour}]) -> owner?` | the kinds of `rood.hud.kinds()` (not `name_window`); `fn(e, d)` draws each element of the kind on every presented frame; while the claim is honoured the game's packets for it are no-ops (the Travel prompt: its C++ drawing skipped). `flavour`: `"both"` (default), `"modern"` or `"classic"`, the looks it draws in. One owner per kind and look: a claim made for that look's flavour alone first, then the last in load order (a console sandbox after every plugin), with a warning |
| `rood.hud.release(kind)`, `rood.hud.kinds([flavour])`, `rood.hud.honoured(kind)`, `rood.hud.look()` | `kinds()`: the modern look's kinds (`kinds("classic")`: the classic look's, with `gauges`, without `travel` and `notice`, the port's own); `look()`: `"classic"` (ui_menus 0) or `"modern"` |
| `rood.hud.typography(style, {...} \| nil)` | a ui_text style's fields (`font`, `size`, `stroke`, `tracking`, `stretch`, `slant`, `embolden`, ...: the `ui_text_<style>_<field>` settings) and ui_text's tunables (`word_space`, `edge_margin`, `popup_scale`, `descender_lift`: the `ui_text_<tunable>` settings, held through the style named) for this run, over the settings, never saved; nil gives the settings' back; dropped when the plugin unloads. What the classic look may change (with ui_text font) |
| `rood.draw.style{...} -> style` (also `d.style`) | an immutable handle, parsed once (make styles at load): `font` (a ui_text style's font: `dialogue`, `menu`, `info`, `digits`, `status`, `labels`), `size` (em, game pixels), `cap` (instead of size: a capital H this tall), `color` (`0xRRGGBB` or `"#rrggbb"`), `alpha`, `stroke` (game pixels), `stroke_color`, `stroke_alpha`, `tracking` (em), `stretch`, `align` (`left`, `center`, `right`), `anchor` (`top`, `middle`: the capitals' middle, `baseline`, `bottom`); unknown fields and bad values raise; at most 256 at once (a collected handle lets its slot go) |
| `d.text(style, s, x, y[, color[, alpha]]) -> queued` | inside a draw function only; game coordinates (widescreen narrows them about the element's anchored edge, as the game's 2D) |
| `d.fit(style, s, x, y, w, h[, color[, alpha]]) -> queued` | inside the box (its top-left at `x, y`): no larger than the style, smaller to fit its height and condensed to fit its width (`w` 0: any) with the outline kept in; across by the style's align, the capitals' middle on the box's |
| `d.glyph(button, x, y, h[, style[, color[, alpha]]]) -> w`, `d.glyph_width(button, h)` | the button's prompt icon for the device used last (`l2` .. `left`, `travel`), `h` high, its left at `x`, its middle at `y`; the style's stroke outlines it; 0 when that device has none |
| `d.rect(x, y, w, h, color[, alpha])`, `d.measure(style, s) -> w, h` | |
| `d.gradient(x, y, w, h, color, color2[, vertical[, alpha]])` | `color` at the left (the top) to `color2` at the right (the bottom) |
| `d.sprite(x, y, {page_x, page_y, bits, u, v, w, h, clut_x, clut_y, bgr?, set_page?, abr?}[, color[, alpha]])` | classic only: a VRAM rectangle allocated/linked by `vs_battle_setSprite` (page_x a multiple of 64, page_y 0 or 256, bits 4, 8 or 15; dimensions 0..256). `color` modulates it, white: as it is. Optional descriptor `bgr` overrides color with an exact GPU modulation word (0xBBGGRR, 0x80 full), preserving the game's colours without RGB conversion. `set_page=false` leaves the game's NOP before the sprite instead of setting a texture page, inheriting the preceding draw mode (default true). `abr` (0..3) the page's blending (the game's setTPage), for a semi-transparent sprite (`alpha` below 1). |
| `d.option_text(text, x, y, colour[, {room, upper}])`, `d.option_width(text[, {room, upper}])`, `d.option_bar(x, y, fill, colour)` | classic only (option_value's): our Option pages' value text by their own draw (encoded as the page encodes it, capitals unless `upper` is false, cut to `room` pixels with `..`; colour 0 white, 1 lit, 3 grey; returns the pen), its width (nothing drawn), and a slider's gauge (`vs_battle_renderStatBar`, `fill` 0..64) |
| `d.menu_int(value, x, y, word) -> x, y` | classic only: a number by MAINMENU's printer (`vs_mainMenu_renderIntColor`: its digits right to left from `x`, 8 pixels each, a `-` before a negative; `word` the PS1 colour word `0xBBGGRR`, 0x80 full); returns the pen the game's call returns (left of its last digit) |
| `d.stat_bar(x, y, fill, colour)` | classic only: the game's gauge (`vs_battle_renderStatBar`, the HP / MP / RISK and DP / PP bars: `colour` 0..3 its table's, `fill` its width in pixels, 0..64 within its frame) at `x, y`, in the layer's slot |
| `d.button_background(x, y, w, h)` | classic only: a button's window as MAINMENU draws it behind a button and its word (`vs_mainmenu_renderButtonBackground`: `h` shaded lines, the i-th from `x` to `x + w - i`, dark gold to near black, semi-transparent; the STATUS strip's), in the layer's slot (the game's: `vs_scratch.unk4 - 12`) |
| `d.packet(w1[, w2 .. w15])` | classic only: one primitive of the game's own words (1..15 after its tag, which links it into the layer's slot as `vs_battle_addTile` and `vs_battle_insertTpage` link theirs): a line or tile (code and colour, place, place or size), a draw mode word (`0xE1...`), a shaded line (colour, place, colour, place), several in one tag as the game writes them (the rename grid's: a draw mode, a shaded line, a draw mode; a draw mode and a shaded four-point polygon; the file menu's panel: a draw mode, a tile and a shaded polygon, 12). In an immediate draw (`file_menu`, `file_element`) each is drawn at once, in the order made, as `DrawPrim` draws one |
| `d.image_store(x, y, w, h)`, `d.image_load(x, y, w, h, pixels)`, `d.image_restore()` | classic only: VRAM transfers (a rectangle of 1..1024 pixels): read into the draw's own buffer (`StoreImage`), uploaded from `pixels` (a string of `w * h` 16-bit pixels, little-endian: a record's CLUT, `e.file.cluts[i]`; or a record's upload handle, `e.load.uploads[i].pixels`: the game's own pixels as its range recorded them, any size, `w, h` the upload's; `LoadImage`), the last read put back. In an immediate draw each is done in its place among the packets (the file menu's CLUT swap for a location picture: read the CLUT row, upload the picture's, the sprite, put it back) and compared as the game's are (an upload by its rectangle and a hash of its pixels, a read by its rectangle); elsewhere done at once, as the draw is made |
| `d.text_glyph(code, x, y, advance)` | classic only, immediate draws: the next packet is a printer's glyph `code` (the menu charset's) set at `x, y` with the printer's `advance`, reported to ui_text as the game's printer reports its own (`pc_text_glyph`: its line layout with the upgraded font, the button roles' glyph); `code` -1 a space (the pen moved, nothing drawn) |
| `d.font_text(s, x, y, style[, step]) -> x`, `d.font_width(s, x) -> x` | classic only: `s` (the menu charset, `vs_encode`; or a table of the charset's codes as a record gives them, 0..0xE6, at most 64) in the text boxes' font by the game's printer (`vs_battle_printVariableWidthFontChar` after `vs_battle_setFontStyle(style)`: the header of the class / affinity / type table), from `x` along `y`; `step` (1..64) moves the pen that far a glyph whatever its width (the rename grid's table and words); returns the pen after it. `d.font_width`: that pen, nothing drawn |
| `d.menu_row([{x, y, w}])` | classic only (menu_row's): the row the element recorded drawn by the game's own emit (58EC.c, through `pc_mainmenu_render_row`) on a copy of its record; `x, y` (its place) and `w` (its window's width, 0..255) replace the record's when given |
| `d.quad(x, y, w, h, {page_x, page_y, bits, u, v, w, h, clut_x, clut_y, bgr?, raw?, abr?, corners?, uvs?}[, alpha])` | classic only: a textured four-point polygon (POLY_FT4) as the label loop builds its numbers and buffs (2842C.c func_80094B0C, func_8009695C: setPolyFT4, setXY4, setUV4, setTPage, setClut, AddPrim): `x, y` its top-left, `w x h` on the screen; the descriptor's `u, v, w, h` its texels (their own size: a quad may squash them); `bgr` the colour word (`0xBBGGRR`, `0x80` full; default `0x808080`), `raw = true` the texture unmodulated (setShadeTex 1), `abr` the page's blending (0..3), `alpha` below 1 semi-transparent. `corners = {x0, y0, .. x3, y3}` (from `x, y`, in the packet's vertex order) with `uvs = {u0, v0, .. u3, v3}`: each corner and its texel given (a mirrored balloon, a turned arrow); `w, h` and the texel rectangle are then unused |
| `d.frame(x, y, w, h, {box, shape, flip?, tail?})` | classic only: a text box's frame by the game's own routine (func_800CD3E4) on a copy of box `box` (0..7: it links into that box's entries of the text boxes' ordering table): its place, size, shape, flip and tail as given, the rest the box's. The vector talk bubbles (`ui_bubbles vector`, an option) redraw it as they redraw the game's |
| `d.band(box, {top, edge, lines, settled?, icon?})` | classic only: a text box's band across the screen by the game's own routine (func_800CD158, the name window's) on a copy of box `box`: from the top or the bottom, `edge` rows grown, its lines, its icon when settled |
| `d.char(code, x, y, font, style, brightness)` | classic only (box_text's): one glyph of the text boxes' font by the game's printer (vs_battle_printVariableWidthFontChar) in the element's box, its font table, colour style and brightness set for it; ui_text sets it as the game's (the dialogue and menu styles, `ui_text_layout`, `ui_text_bubble_align`: the settings stay the source) |
| `d.plate(x, y, w, h, left, right, shadow_x, shadow_y, shadow_w, shadow_h)` | classic only (target_row's): a target row's plate as func_800C1DC4 builds it, one packet: a black tile (the shadow), then a dithered gradient across `x, y, w x h` from the colour word `left` to `right` (`0xBBGGRR`, `0x80` full) |
| `d.leader(x0, y0, x1, y1, count, left)` | classic only (target_line's): a leader line by the game's own routine (func_800C16FC; `left` true: func_800C1A40) as func_800C1DC4 calls it: from `x0, y0` to `x1, y1`, `count` its sweep; in the kind's slot |
| `d.image(image, drawer, clut)` | classic only (image's): a portrait or still by the game's own drawer (D_800EC25C[drawer], as func_800CCA90 calls it), in the kind's slot; the drawer's own widescreen placement (a full-screen still fills 16:9) applies as to the game's |
| `d.window(x, y, w, h[, brightness])` | classic only: the game's text box window (its stone body, frame and shadow); brightness 1 a selected box, 0.5 the others |
| `d.cursor(x, y[, step])` | classic only: the menus' pointing hand, its brightness animation at `step` (0..15; default: the game's) |
| `d.bar(x, y, value, max[, pool])` | classic only: the status display's bar (`hp`, `mp`, `risk`) filled value / max |
| `d.text_raw(s, x, y, word[, typeset])` | classic only: the game's raw printer (`vs_battle_renderTextRawColor`: the status words, RISK ZERO) called as the game calls it: `s` as given (`$` centres it on `x`, `#` right-aligns, the game's narrow blanks), `word` the PS1 colour word (`0xBBGGRR`, `0x80` full); `typeset` how ui_text sets it: `{style = "status", blanks = "one" \| "each", keep_on_screen, screen_w, margin, word_space}` (fields left out: ui_text's defaults, its settings) |
| `d.layer(slot \| nil)`, `d.layer("unk8", bytes)` | classic only: the ordering-table slot the commands that follow link into, as the game names its slots: bytes from `vs_scratch.unk4` (-64..64, a multiple of 4: the status words' -16, the gauges' bars and labels -12, their digits -16); `"unk8", bytes` `vs_scratch.unk8` plus bytes (-64..64, a multiple of 4: a menu row icon's + 4); nil the kind's own (an emote's: its actor's depth slot, `unk4 + z` words, resolved per element). `"unk8"` resolves `vs_scratch.unk8` at the draw, also for shadow-slot identity and OT sorting (not a guessed offset). A slot draws its packets in the order of the commands (the game's: the reverse of its calls) |
| `d.value(font, x, y, value) -> x, y`, `d.digit(font, x, y, digit)` | classic only: the HUD's digits (`vs_battle_renderValue`: the rightmost at `x, y`; font 0 small, 1 large, 2 the separator (value 0: the DP / PP slash); returns the pen the game's call leaves) and one digit (`_renderDigit`; font 2 the separator) |
| `d.xy(x, y, delta) -> x, y` | the game's packed position arithmetic: `x, y` packed as the game packs them plus `delta` written as the game writes its steps (`56 \| (-8 << 16)`), so x's carry or borrow reaches y as in the game |
| `d.anchor(where)` | where the draw's commands sit in 16:9: `"left"`, `"right"`, `"centre"`, `"element"` (its own, the default), `"game"` (classic only: placed as the game's own 2D is, no anchor of its own: widescreen's edge extension and cover guesses as for the game's packets; the text boxes' frames and band use it so 16:9 looks as today), `"screen"` (a panel's: the picture's square pixels, 240 high; not in the classic flavour); in the classic flavour the range is placed exactly (`PC_ANCHOR_EXACT`: no edge extension, cover or side-paint guesses) |
| `rood.draw.panel(fn \| nil[, {classic = true}])` | the plugin's own layer over the picture (one a plugin; nil removes it): `fn(d, view)` every presented frame, in the modern pass whatever the look, after the HUD and the menus, whatever `rood.ui.hide` hides; `view`: `width` (240 times the picture's aspect), `height` 240, `aspect`, `look` (`classic`, `modern`), `flavour` (`modern`, or `classic`: below), `dt`. Coordinates are the picture's square pixels from its top-left (`d.anchor("screen")`, its default); the classic-only primitives raise. Never in a photo (captured before the overlay). A draw over its budget is drawn again next frame; one that raises an error stops it until the plugin sets it again or reloads (a shipped plugin's: for `plugin_fault_cooldown` seconds). While one is set its commands replace the HUD's in RPC `hud {stats}`'s last frame. **`{classic = true}`: the panel in the classic look is drawn by the game's own frame, with the classic primitives** (`view.flavour == "classic"`: `d.window`, `d.sprite`, `d.cursor`, `d.text_raw`, `d.glyph`'s button sprites, the game's fonts; below), and not in the modern pass; with the modern look on it is the modern pass's as before. Not a claim (nothing of the game's is suppressed; no packet parity); the game's 320 x 240 pixels (`view.width` 240 times the aspect: more in 16:9), placed from the left edge exactly (`d.anchor("left" \| "right" \| "centre")`, `"screen"` raises; the commands link where the menus' windows do, over the HUD, under nothing). It is drawn with the game's frame (30 Hz, `dt` the last presented frame's) and only where the game draws the field's ordering table (the field, battle, the original menus; not the title or a movie), never in a photo (its packets lie in the hidden game 2D layer). RPC `hud {stats}`: `panel_last` (its first 16 commands), `panel_packets` (`start`, `end`: the guest range of its packets in the last frame, for RPC `packets {start, end}`) |
| `rood.dev.show(kind, {...})` | dev mode only (`ROOD_RPC` set or `ROOD_DEV=1`): in the field the game's own spawner (status: `timing`, `variant`; chain: `count`; buff: `slot`, `stat`, `value`, `sign`; damage on Ashley: `value`, `heal` or `sign`, `pool`, `stat`; travel: the prompt without a Circle of Magic; name_window: `index` (184), `message` (11), the game's own `vs_battle_displaySceneMessage`), held at `age` (10) until `rood.dev.clear([kind])` (`hold = false`: it plays out; a name window closes after 120 vsyncs); elsewhere a synthetic element (the name window's `text`) only modern claims draw. `rood.dev.list()` (with `drawn`, `suppressed`) |
| `rood.dev.bench.save(name)`, `list()`, `load(name)`, `current()` | the gallery (below) |

Honoured: a claim that has not failed, and the modern look (`ui_menus 1`);
in the classic look a claim made for the classic flavour when the kind's
`hud_classic_<kind>` is on (every kind `rood.classic` draws, by default: each
passed the parity gate before its default went on), any
claim with `hud_classic_override 1` (default 0). With the parity mode on
(below) nothing is honoured in the classic look: the game draws and the
Lua draw is only compared.

Two flavours (`e.flavour`). Modern: the presented frame's pass, drawn over
the picture (ImGui, the fonts of ui_text's styles, outlines, any size).
Classic (the classic look, as honoured above): the claim runs
inside the game's frame at the element's own hook and its commands become
PS1 packets of the game's kinds in the HUD labels' slot, anchored as the
element in widescreen (`pc/src/hud/hud_bridge.c`): `d.text` and `d.measure`
use the game's printers (fonts `dialogue` and `menu`: the menus'
variable-width font, through `vs_encode`; the others the status words'
capitals, `align` by the printer's own centring; the `status` style's word
restyled by ui_text as SLOW! is; size, stroke and tracking are ui_text's,
not the style's), `d.fit` places without scaling, `d.glyph` is the game's
16 x 16 button sprite (triangle, square, circle, cross, L1, R1, L2, R2;
the button prompts setting and the Circle/Cross roles redraw it as they do
the game's), `d.rect` and `d.gradient` are tiles and Gouraud quads (alpha
below 1: the PS1's half blending), and the classic-only `d.sprite`,
`d.window`, `d.cursor`, `d.bar` exist (in the modern flavour they raise). A plugin panel can draw in this flavour too (`rood.draw.panel(fn, {classic = true})`: "A panel in the classic look" below).
So the classic look gains only what a plugin draws with the game's own
pieces; the Travel prompt has no classic flavour (its classic look is the
game's), and synthetic elements are drawn only in the modern look. A draw that raises counts a fault; over its budget (a slow
frame) the element is the game's for that frame and the claim is drawn again
next frame (the fault policy disables a plugin that keeps overrunning); an
error hands the kind back to the game until the plugin reloads, or for a
shipped plugin for `plugin_fault_cooldown` seconds, so one slow moment never
costs a shipped kind for the session. The command buffer is fixed (1024
commands, 16 KB of text: past it `d.text` returns false). The host's own
work inside every call (a font loaded and its glyphs baked on first use, any
`d.*`) does not count against the draw's budget (Budgets and faults). A reload drops the plugin's claims, styles
and typography; held moments belong to the host and stay, so an edit shows
on the held word about half a second after it is saved (the hot reload
check).

Classic elements also carry `call_x, call_y`: the place the game's call
was given (the timing word's 44, 48 before its slide), which an exact copy
of the game's draw starts from.

RPC `hud`: `list`, `show {kind, ...}` (as `rood.dev.show`; the name
window's `index`, `message`, `text`), `clear {kind}`, `stats {reset}` (per
kind: seen, suppressed, fallback, drawn, faults, owner; the claims' Lua per
presented frame and, classic, per game frame: mean / p99 / max ms; Lua
allocations per frame; events emitted; the last frame's commands, and the
last classic draw's), `bench {op save / list / load, name}`, `set
{classic_override}`; `pc/tools/rood_rpc.py hud-show KIND` (`--index`,
`--message`, `--text` for the name window).

### Performance

The plan's budget: at most 0.25 ms of Lua per presented frame for the whole
HUD and 0.1 ms per game frame for classic claims; `plugin_budget_ms` stays
the hard ceiling of each draw. No allocation per frame on the host's side
(a fixed pool of 64 elements, 256 styles, 1024 commands and 16 KB of text;
256 classic commands and 4 KB), and the element tables, their parts and the
query tables are reused, so a draw function that makes no strings makes no
garbage. RPC `hud {stats}` reports it; hud_bench_smoke asserts it with
rood.hud's five kinds held (measured 2026-10-19: 0.010 ms mean, 0.017 ms
p99 a presented frame; classic 0.009 ms a game frame; 0 allocations a
frame; the hot reload loop 0.3 s mean, under 0.5 s).

### Queries

`rood.game` (`pc/src/lua/lua_api_game.cpp`, `pc/src/hud/query_bridge.c`):
read-only game state for logic and draw functions. The game's records are
copied at most once per game vsync, on the first query; each plugin gets one
read-only table per query per vsync (the same table until the next one, so
`==` tells whether it changed; nothing to collect in a draw function).

| | |
|---|---|
| `rood.game.ashley()` | nil, or `{hp, max_hp, mp, max_mp, risk, str, int, agl, position = {x, y, z}, facing, battle, statuses = {{name, buff, seconds}, ...}}` (her HUD record and actor; `battle`: the weapon drawn; statuses as the Status screen lists them, in the game's words) |
| `rood.game.mode()` | `title`, `attract`, `cutscene` (an event script), `menu` (the field menu or the modern menus), `battle`, `field`, or `none` (no room: loading) |
| `rood.game.room()` | nil, or `{zone, room, area, name}` (the game's names) |
| `rood.game.first_person()` | the first-person view |
| `rood.game.menu()` | nil (no menu frame), or the classic menus' model (`pc/src/classic/menu_model.h`; battle builds): `{menu, screen, loaded, submenu, hidden, rows, cursor, command, command_next, fade, fade_in, info}`. `rows` are `vs_battle_menuItems`' live rows (`{id, state, x, y, w, background, target_x, target_y, selected, unselectable, invert, scrollable, gradient, anim, button, icon, outset, material, color, count, max, page, text, glyphs, sub}`: `text` decoded, `glyphs` the codes the game set); `cursor` MAINMENU's list (`{row, page, sub_menu, row_info, rows, list_open, list_row, row_offset, memory_row, memory_page}`); `info` text box 7 while it is up. A skin for the classic menus reads it; actions stay the game's |
| `rood.game.paused()` | the photo mode's pause holds the game (or was asked for); `pause()` / `resume()` with the `game.pause` grant (below) |
| `rood.game.inventory()`, `weapons()`, `shields()`, `armour()`, `accessories()`, `equipment()`, `workshop()`, `labels()`, `combine(a, b)`, `craftable()` | the player state: below |

### Player state

`rood.game` (`pc/src/lua/lua_api_player.cpp`, `pc/src/hud/player_bridge.c`,
the records `pc/src/hud/player_query.h`): the bag, the weapons and armour,
what is equipped, the workshop, and the game's own combine run on copies.
Read only, with the same once-per-vsync tables as the queries above (`==`
tells whether the next vsync's answer is a new table; the values are the
same when nothing changed). RPC `game_state {parts}` and `combine_preview`
give the same structures as JSON (`pc/README.md`).

Where the values come from. The bag is `vs_main_inventory` in the menus'
order (`vs_main_inventoryIndices` as `vs_mainMenu_rebuildInventory` leaves
it: records the list lacks appended, worked out on a copy), each record
described as the Items screen's card describes it (`pc_ui_items_describe`:
DP in whole points rounded up, the game's names, materials and words), as
the menus see it: Ashley's battle record holds the equipped items' current
values and MAINMENU's `vs_mainMenu_initInventory` copies them into the bag
as every menu opens, so the query runs that copy and puts the bag and the
record back afterwards. A weapon or shield not equipped is computed with
`vs_battle_applyWeapon` / `Shield` as the Items screen computes it; their
scratch block in the game's heap is put back too. A query leaves the game's
memory and save state byte for byte as they were (`player_state_smoke`: the
RPC `snapshot {action "hash"}` before and after). The container's records
are not in it: they live in MENUD's storage, filled only while the Container
screen runs.

| | |
|---|---|
| `inventory()` | nil (no player), or `{counts, items, keys, sigils, blades, grips, gems}`: `counts` per bag category (`weapons`, `blades`, `grips`, `shields`, `armor`, `gems`, `misc`) `{count, capacity}`; `items` the misc records that are not keys or sigils (Cure Root, grimoires, ...), `keys`, `sigils` (by the game's item ids), each `{kind, record, id, name, count}`; `blades`, `grips`, `gems` every such record (a part, below), in a weapon or shield or not |
| `weapons()` | nil, or the weapons in the menus' order: a part of kind `weapon` (its totals, as the Items screen shows the assembled weapon) with `blade`, `grip` (parts) and `gems` (a list of parts) |
| `shields()` | nil, or the shields: a part of kind `shield` (with its gems' effect, as the Items screen shows it) and `gems` |
| `armour()`, `accessories()` | nil, or the armor records that are body armour (kind `armor`), or accessories (kind `accessory`) |
| `equipment()` | nil, or what each slot holds: `weapon` (a weapon as `weapons()`), `shield`, `right_arm`, `left_arm`, `head`, `body`, `legs` (armor), `accessory`; an empty slot is absent |
| `workshop()` | nil (not in a workshop), or `{number, name, materials}`: the game's workshop number (`vs_battle_inWorkshop`, 1..6), the room's name, the materials it works (MENUC `_workshopMaterials`), as names |
| `labels()` | the game's words for the keys below, in its order: `{classes, affinities, types}` (its sort menu's names: HUMAN .. EVIL, PHYSICAL .. DARK, BLUNT .. PIERCING) |
| `combine(a, b)` | the result the game would produce, or nil and why: `a`, `b` two blades, two shields or two armor records (tables from these queries, or `{kind = "blade", record = 3}`; a weapon stands for its blade). Gives `{kind, first, second, allowed, why, result}`: `first` / `second` `{record, name}` (the result goes into the first's record, so the order matters), `allowed` whether the current workshop would do it (not in a workshop, or a material it does not work: `why`, the game's own line), `result` a part as the game would store it |
| `craftable()` | every combination the Combine screen allows here (an empty list outside a workshop): every ordered pair of two blades, two shields, two armor records (not accessories) whose materials the workshop works, each as `combine` gives it |

A part (a record) has the fields its kind has:

| Field | Kinds | |
|---|---|---|
| `kind` | all | `weapon`, `blade`, `grip`, `shield`, `armor`, `accessory`, `gem`; misc records: `item`, `key`, `sigil` |
| `record` | all | its record in the game's array of that kind, from 1 (accessories are armor records); the game's own references use the same numbers (a weapon's blade, a gem's weapon) |
| `id` | all | the game's item id (a weapon: its blade's record, as MAINMENU's `_getItemId`) |
| `name` | all | the game's name (a weapon: its own name) |
| `count` | item, key, sigil | the stack's count |
| `material` | weapon, blade, shield, armor | the game's material name (Wood, Leather, Bronze, Iron, Hagane, Silver, Damascus) |
| `category` | weapon, blade, grip, shield, armor | the menus' word: a blade's weapon class (Sword, Axe & Mace, ...), a grip's kind (its line in the Items list), armor's body part |
| `dp`, `dp_max` | weapon, blade, shield, armor | as the menus show them (whole points, rounded up) |
| `pp`, `pp_max` | weapon, blade, shield | |
| `str`, `int`, `agl` | all but misc | |
| `classes` | all but grip and misc | `{human, beast, undead, phantom, dragon, evil}` |
| `affinities` | all but grip and misc | `{physical, air, fire, earth, water, light, dark}` |
| `types` | weapon, grip, shield, armor, accessory | `{blunt, edged, piercing}` (a weapon: its grip's) |
| `damage_type` | weapon, blade | `blunt`, `edged` or `piercing` |
| `range`, `risk`, `two_handed` | weapon, blade | the RISK cost; a two-handed weapon class |
| `gem_slots` | weapon, grip, shield | (a weapon: its grip's) |
| `weapon` | blade, grip, gem | the weapon record it is part of (absent: none) |
| `shield` | gem | the shield record it is set in |
| `equipped`, `where` | all but misc | worn, equipped, or part of the equipped weapon or shield; `where` `equipped` or `bag` |
| `slot` | weapon, shield, armor, accessory | the slot it is equipped on (`equipment()`'s names) |
| `quick` | weapon | its quick-swap slot (the port's weapon slots), absent: none |

The combine preview runs the game's code, not a copy of its rules: MENUC's
own `_combineBlades`, `_combineShields`, `_combineArmor` (`src/MENU/MENUC.PRG/
168.c`, through its `ROOD_PC` entry `pc_menuC_combine`) on copies of the two
records as the menus see them, into a buffer prepared as the Combine
screen's (`_setCombineBladeUi` and its shield and armor twins), with the
SYD tables read from the disc into host memory. Those functions write only
the result and the overlay's own table pointers, which the entry puts back;
the records' table entries are checked to lie in the files before the call
(a corrupt save cannot make it read past them). `player_state_smoke` does a
preview, checks the state hash is unchanged, then the same combine on the
original Combine screen: the record it stores is the preview's `raw`, byte
for byte (blades and armor).

`pc/plugins/rood.craftbook` (off by default) is the example: at a workshop
it lists `craftable()` in a panel (`rood.draw.panel`, both looks), as a
template with a setting, a timer, a module and tests.

### The gallery

`rood.dev.bench` (`pc/src/lua/lua_bench.cpp`; dev mode) keeps moments to
come back to. `save(name)` (a-z, 0-9, `-`, `_`) writes the game's save state
at the next vsync boundary as `<bench>/<name>.snap` and, beside it,
`<name>.lua`: a Lua table of what the save state does not hold (the held
`rood.dev.show` moments are the host's), the room and when (`<bench>` is
`ROOD_BENCH`, else `pc/user/bench`). `list()` gives the entries (`name`,
`saved`, `zone`, `room`, `area`, `room_name`, `held` (kinds), `valid`,
`why`): an entry goes stale when the game's code changes (a rebuild of
`src/` or a unit built like it; Lua edits and engine rebuilds keep it).
`load(name)` restarts the process resuming it (RPC `restart {snapshot}`'s
way: the same command line and environment, so the RPC socket and the
memory cards stay; paused if the RPC had it paused), and the new process
holds the entry's moments again (`current()` names it). About 0.4 s from
`load` to the word held again.

### rood.hud (shipped, on)

`pc/plugins/rood.hud` claims every kind. Its look is the table in its
`main.lua`, built from the settings it reads (`rood.settings.get`; read
again when one changes): the status words and chains in the status style's
typography (`ui_text_status_*`) outlined, where the game centres them; a
buff's words and digits each fitted inside the game's box for it, the
outline kept in; a damage number's digits where the game bounces them; the
Travel prompt as the C++ prompt lays it out (`qol_fast_travel_prompt_*`,
`ui_prompt_*`): the icon and TRAVEL outlined, no plate. The classic look is
`rood.classic`'s (below); there rood.hud's claims draw only with
`hud_classic_override`, for the kinds rood.classic does not draw yet.
Turned off, everything is the game's and the C++ drawing. Its Lua tests (`tests/hud_test.lua`) summon each kind and wait
for its fields and drawn frames.

Try it (the Lua console, F1, in the field with the modern look):

```lua
rood.dev.show("status", { timing = "slow" })
rood.dev.show("chain", { count = 3 })
rood.dev.show("buff", { slot = "weapon", stat = "human", value = 2 })
rood.dev.show("damage", { value = 123 })           -- { heal = true }, { pool = "mp" }, { pool = "stat", stat = 2 }
rood.dev.show("travel")
w = rood.draw.style{ font = "status", size = 14, stroke = 1.5, align = "center", anchor = "middle" }
rood.hud.claim("status", function(e, d) d.text(w, e.text, e.x, e.y, e.rgb) end)   -- the console's claim wins
rood.hud.release("status")   -- rood.hud's again
rood.dev.clear()
```

### A panel in the classic look: `rood.draw.panel(fn, {classic = true})`

With the modern UI off, new prompts and HUD keep the game's own button sprites
and layout; only typography may be upgraded. A plugin's panel can follow that
rule with the primitives rood.classic draws with, not an imitation of them:
declare it `classic` and, with the classic look, the game's frame calls it with
`view.flavour == "classic"` and the classic-only `d` primitives work (`d.window`
the game's text box: stone body, frame and shadow; `d.glyph` its 16 x 16 button
sprites, redrawn per the button prompts setting and pad family as the game's;
`d.text`, `d.fit` and `d.measure` the menus' font through ui_text, so the
typography settings and `rood.hud.typography` apply; `d.sprite`, `d.cursor`,
`d.text_raw`, `d.bar`, `d.value`, `d.layer`). With the modern look the same
function is drawn in the modern pass (`view.flavour == "modern"`), so one
function draws both, branching where the looks differ (the window against a
rect, the metrics: the menus' font is 12 pixels high, the game's 320 wide).

```lua
rood.draw.panel(function(d, view)
  if view.flavour == "classic" then
    d.window(8, 42, 200, 40, 0.5)                      -- the game's own window
    d.fit(menu, "Craftbook", 16, 50, 184, 12)           -- the game's font
    local w = d.glyph("triangle", 16, 70, 14, menu)     -- the game's button sprite
    d.fit(menu, "Combine", 16 + w + 2, 64, 160, 12)
  else
    d.rect(8, 42, 168, 40, 0x0E1014, 0.8)               -- the modern panel
    d.fit(label, "Craftbook", 12, 46, 160, 9)
  end
end, { classic = true })
```

Panels are new drawing, not claims: no kind is suppressed, nothing is compared
by the parity gate, `rood.classic`'s claims are unaffected. See
`pc/plugins/rood.craftbook`. The game side is one hook under `ROOD_PC` in
`BATTLE.PRG/146C.c` func_8007629C (before `DrawOTag`), `hud_bridge.c`
`pc_hud_panels_classic`; the test is player_state_smoke's craftbook checks (the
window's body is the game's textured quad with the window's CLUT, the text the
menu font's glyph sprites, the prompt the button sprite, nothing of it in the
modern pass or the modern look).

### The classic look in Lua: rood.classic (shipped, on)

Plan `pc/docs/plans/2026-10-20-classic-ui-in-lua.md` (phase 0 and the first
slice of phase 1). With the modern UI off the classic look keeps the
game's own sprites, windows and layout; only typography may change. So
`pc/plugins/rood.classic` draws each kind it owns as an exact copy of the
game's own drawing, made of the game's own pieces (`d.text_raw` is the
game's raw printer), and what it may change is a table:

- `look.status`, `look.chain`: the game's strings, colour words and slides
  (func_800958A4, func_80095A4C) as data, and `place = {anchor, dx, dy}`
  (16:9: the left edge, as the game's hook anchors them);
- `look.gauges`: the HP / MP / RISK gauges' places, labels (the field UI
  page's HP, MP, RISK), steps (as the game writes them: packed
  `x | y << 16`), slots and RISK ZERO, and their `place`;
- `look.figure`: Ashley's five-part body/damage silhouette, both stances' rectangles,
  texels, palette, condition colours, layer and `place`. Its read-only fields are
  `stance` (0/1) and `limb_0_status` through `limb_4_status` in the game's limb order
  (condition 0 critical, 1 damaged, 2 wounded, 3 good, 4 excellent). C still decides
  conditions and visibility: there is no `rood.dev.show("figure")`; it is a classic-only kind;
- `look.status_icons`: the status carousel's clipped texture cells, supplemental stat glyphs,
  palettes, count/selection indicators, layers and left-edge `place`. Read-only fields are
  `previous`, `current`, `next` (icon IDs 0..31, 255 absent), `count`, `selected`
  (the pre-rotation ordinal, 255 absent), `offset` (the draw's pre-increment slide offset),
  and `slide` (the panel's packed Y displacement). C retains status decisions, carousel
  mutation, tickspeed and visibility; there is no `rood.dev.show("status_icons")`;
- `look.battle_mode`: the BATTLE MODE sprite, right-edge `place`, layer, entry spacing,
  packed base colour and semitransparency. Read-only `step` (1..4), `slide` (packed panel
  displacement before its update), and `pulse` (`rsin(phase & 0x7ff) >> 5` before phase
  advance) are copied at the emit boundary. The pulse adds to the packed BGR word,
  not to every channel. C retains slide/reset/phase logic; no `rood.dev.show("battle_mode")`;
- `look.ability_icons`: marker/label rectangles, cross and face-button texels, palettes,
  spacing and centre-edge `place`; `layer = "unk8"`. Read-only `animation`, `offset`
  (the game's negative row-animation displacement), `group` (0..3 right labels and
  face buttons, 4..7 left labels, 8 cross), and `availability` (0 absent, 0x140 dim,
  0x180 bright) are copied after the original C availability call. Nine emit ranges
  share this kind but have separate stable keys. `pc_weapon_slots_label` is invoked
  exactly once in its original order; its weapon-mode names/markers and button-prompt
  side effects remain C, outside these ranges. No `rood.dev.show("ability_icons")`;
- `look.buff`: WEAPON HUMAN +1 (2842C.c func_8009695C) as `d.quad`s of the field UI page: the
  equipment's and the class or affinity's words (`slots`, `stats`: by word, the game's
  `D_80069144` / `D_8006917C` texels), the colour words of a gain and a loss, the gaps, the sign's
  and the small digits' cells, its layer (+8) and left-edge `place`. Its fields are `slot`, `stat`,
  `sign`, `value` and the read-only `rise` (the record's unk12: 10 for each buff shown after it);
- `look.damage`: a number over an actor (func_80094B0C): the large digits' cells, the colour
  words by pool (`hp`, `mp`: a loss, a gain), the bounce by frame (the game's table; each digit to
  the left half the one before), and a stat's change (func_80094E18): its icon (the combat labels'
  texels by stat), its sign, the drift by half the age. Fields `value`, `sign`, `pool`, `stat`,
  `age`; the place is where the label loop put it (`call_x`, `call_y`, its spread applied). Its
  digits are the digits style's, so `look.typography.digits` sets them as it sets the gauges';
- `look.actor_word`: MISS (its four capitals' texels, the squash by step, half blended) and the
  combat labels (`labels`: each its colour and pieces, the game's D_800690B4; `pieces`, D_80068FAC's
  texels; `colours`, D_80069134), their slide in and out, fade, held brightness, and a lowered
  stat's arrow (its cell stepping from age 15); layer -20, centred like the numbers;
- `look.emote`: the emote balloons: the balloon's corners and texels by side (D_80068F04,
  D_80068F64), the marks' offsets by side (D_80068F94, packed as the game adds them), the dots,
  '!', heart and '?' sprites, the off-screen arrow's eight points and its direction table; drawn in
  the actor's depth slot (the kind's own layer, `d.layer(nil)`), the arrow in -16;
- `look.text_frame`: a text box's frame drawn with `d.frame` from the box's own fields (its
  place, size, shape, flip and tail are the game's), centred; `place` offsets it;
- `look.text_band`: the name window's band by `d.band` from the box's fields, placed as the game's
  (`anchor = "game"`);
- `look.box_text`: a text box's text, each glyph the printer set again by `d.char` (last first: each
  links at the head of the box's entry), placed as the game's; its typography is ui_text's, from
  the settings;
- `look.image`: the portraits and stills by `d.image`, placed as the game's;
- `look.target_row`: a target row: its name (`d.text_raw` at `left` or `right` from the row by the
  screen's half, grey or the selected white), its plate (`d.plate`: `w x h`, the banks' `near` and
  `far` colours lerped by the fade step as vs_battle_uiGradientStop does, mirrored on the left half)
  and the plate's shadow; placed as the game's (anchor `game`);
- `look.target_line`: the selected row's leader lines by `d.leader`, to the row's `ring` point by the
  screen's half, their count the age less `lag`, and from `trail` a second (the age plus `step`,
  wrapped at `cycle`);
- `look.target_marker`: the area target's marker: its sprites (`d.sprite`: the game's packed steps,
  sizes and texels; the locked set from `locked`; `pulse_at` by the brightness step's `pulse`) and,
  unless locked, the brackets (`d.quad`s with corners packed as the game adds them);
- `look.option_value`: our Option pages' value column, from the row's copied inputs, in the page's
  order of calls reversed (each glyph links at its slot's head): `d.option_text`, `d.option_width`,
  `d.option_bar`; `arrow_gap`, `note_x`, `bar_room` as the page spaces them;
- `look.items_category`, `look.items_status_button`: the Items screen's category icons (`left`, `gap`,
  `raised_y`, their texels from `u0`, `v`, the `cluts` lit and dim) and its STATUS strip (`window`, `button`,
  `word`, `colour` shifted by the state), by `d.sprite`, `d.text_raw`, `d.button_background`;
- `look.equip_stats`, `look.dp_pp_bars`, `look.gem_slots`: MAINMENU's stats panel (`line`, `header_x`,
  `columns`, the colours `colour`, `up`, `down`, the words: `header`, `unknown`, `range_risk`, `names` by
  index as the game's table), the DP / PP bars (`bars`: each word's `x`, its digits' `digits`, its `gauge`
  colour) and a grip's gem slots (`icon`, `text`, `prefix`), by `d.text_raw`, `d.menu_int`, `d.value`,
  `d.stat_bar`, `d.sprite`, each in the game's slot (`layer`);
- `look.stat_page`: the class / affinity / type table: `rows` (per page the stats' offsets of its values, the
  compared and the equipped ones, its rows' extent), the columns (`name_x`, `value_x`, `diff_x`, `gauge_x`),
  the colours (`white`, `grey`, `up`, `down`), the gauge's `positive` / `negative` lines, `frame`,
  `frame_light`, `shade`, `shade_dark`, the shaded `line` and the draw `modes` (`d.packet`), the `window`,
  `button`, the `header` (its parts and which page each names, in `styles` + `font_style`: `d.font_text`). Its
  calls are recorded in the game's order and made last to first;
- `look.ok_mark`: the OK mark (`word`, `grey` times the game's level, `layer`); `look.buttons`: the buttons'
  sprite (`sprite`) and each button's texels (`u`: triangle, square, circle, cross);
- `look.storage_buttons`, `look.storage_counts`: Storage's button strip (`windows` and `window_layer`, `buttons`,
  `words`: each its text, x offset from the slide (STATUS / CANCEL / END packed as the game packs them), y, colour
  and how the state lights it) and counts (`colour`, `low` under half full, `slash`, `slash_dx`; the strip's
  `names`, `categories`, `name_x`, `count_x`, `name_y`, `top`, `line`; the top menu's `heads`, `head_x`, `head_y`,
  `columns`, `name_x`, `categories`), by `d.text_raw`, `d.menu_int`, `d.sprite`, `d.button_background` at the
  game's packed places (`d.xy`: the slide's borrow into y kept);
- `look.loot_capacities`: Loot's capacities strip (`head`, `top`, `line`, `name_x`, `slash_x`, `capacity_x`,
  `count_x`, `categories`; the colours `colour`, `dim` (lit `<< 1`), `low`, `over`; `windows`, `window_layer`,
  `words`: each its text, place, button and the button's place, ALL lit by its state too). The strips' calls are
  recorded in the game's order and made last to first;
- `look.discard_count`: a misc item's discard amount (`style`, the digits `step` apart, right to left from the game's
  place, split as the game splits them), by `d.font_text`;
- `look.rename_grid`: the rename grid (`buttons`: which, x from the left edge, y; the pad's `sprite`; `bar`: `rows`
  shaded lines `length` long from `top`, `line` a command, their colours the game's gradient stop between `near`
  and `far` at the bar's fade, the line `code` and the draw `modes` around each; `words`, `name`, `table`: places and
  steps in the text boxes' font, `style`, the name's `blank` skipped; `arrows`: the digits' and letters' places,
  `lit` / `dim`, the sprites' places, texels and CLUTs `clut_lit` / `clut_dim`; `window`: the name window's words;
  `background`: its draw mode, colour words and `corners`), by `d.sprite`, `d.packet`, `d.font_text` (the record's
  codes, a fixed step), `d.text_raw`; recorded in the game's order and made last to first. `look.rename_box`: the
  highlight box (`mode`, `code`, `colour`), by `d.packet`;
- `look.file_menu`: MENU7's file menu (`font`: its printer's page, CLUT row, cell, colour and draw mode, `widths` its
  advances as runs, the blank, the blinking arrow, the line step; `info`, `digits`, `hp_digits`: the save info's
  sprites and figures; `panel`: the gradient's colours by its blend steps; `gauge`; `picture`: the location
  pictures and the CLUT row they swap; `clear_badge`; `frame`: the band, its rows, the caution, the message tile, the
  hand and its saturation steps, the fade; `progress`), by `d.packet`, `d.image_store` / `d.image_load` /
  `d.image_restore` and `d.text_glyph`; made in the game's order (an immediate draw: no table to reverse);
- `look.menuf`: MENUF's screens' leaves (`layer` the sprites' slot; `semi`, `subtract` the pages' blending bits;
  `fade`: its steps, their grey unit, the grey after; `pop`: the flash greys by step, the settling steps toward
  `toward`, the dim CLUT below `dim`; `wipe`: the strips' width, the lit `edge`; `wheel`: the lines faded outside
  `top` .. `bottom` by `step` a line, drawn twice there (added, subtracted) by `faded_clut`, else by `clut`;
  `prompt`: its slide (`dx`, `steps` of `step`), its words' place and `colour`, the bobbing button (`look.buttons`'
  sprite); `place`: the pictures and the prompt moved from the game's places), by `d.packet` (four-point textured
  polygons, flat and shaded, the draw areas' two words) and `d.text_raw`, made last to first; `menuf_load` by
  `d.image_load` with the record's handles, in the game's order;
- `look.menu_row`, `look.menu_cursor`, `look.menu_command` (and `menu_tabs`, `menu_background`):
  MAINMENU's shared frame. A row by `d.menu_row` (the game's emit on a copy); the hand and the
  command title by `d.sprite` from the game's tables (the hand's `pulse`, `cluts`; the title's `sizes`,
  `uvs`, right-aligned at `right`); placed as the game's (anchor `game`); `dx`, `dy` move a piece;
- `look.timer`: the time attack's timer, the game's string (`e.text`, composed by `_renderTimer`)
  through its raw printer at 128, 200, grey, slot -4;
- `look.typeset`: how ui_text sets the words (`blanks = "one"`: a run of
  the game's narrow blanks is one space, `keep_on_screen`, `screen_w`,
  `margin`, `word_space`; the playtest fix `645a7477` as Lua values, the
  settings `ui_text_edge_margin` and `ui_text_word_space` read into it);
- `look.typography`: per ui_text style, its fields and tunables for this
  run (`rood.hud.typography`): `digits = {descender_lift}` is playtest fix
  `1d2f20c5` (the old-style figures risen into the room above them before
  any is squeezed), read from the setting `ui_text_descender_lift`;
- `look.overrides`: per kind, each deliberate difference from the game's
  packets (`typography` for the status words, the chains, the gauges, the buffs and the
  numbers; a non-zero `dx` would be `place`). The parity
  gate's waivers (`pc/tests/classic/waivers.toml`) must list the same.

Edit and save: the running game reloads it (measured: 0.19 s mean from the
save to the first frame set with it at 60 Hz, 0.20 s max). From the REPL
(`pc/tools/rood_repl.py --plugin rood.classic`) `M.look.typeset.margin = 4`
shows on a held word at the next frame; `M.draw.status` replaced draws from
the next frame; saving the file brings its own back.

```lua
rood.dev.show("status", { timing = "success", variant = 5 })  -- WELL TIMED!, held
rood.dev.show("chain", { count = 3 })
```

The fallback: rood.classic off or failing (a draw that raises: counted, the
game draws until the plugin reloads), a kind's `hud_classic_<kind>` off,
or Lua off: the game's own drawing (its typography ui_text's defaults,
which keep the same fixes). Settings: `hud_classic_status`,
`hud_classic_chain`, `hud_classic_gauges`, `hud_classic_figure`, `hud_classic_status_icons`, `hud_classic_battle_mode`, `hud_classic_ability_icons`, `hud_classic_buff`,
`hud_classic_damage`, `hud_classic_actor_word`, `hud_classic_timer`, `hud_classic_emote`, `hud_classic_text_frame`, `hud_classic_text_band`, `hud_classic_box_text`, `hud_classic_image`, `hud_classic_target_row`, `hud_classic_target_line`, `hud_classic_target_marker`, `hud_classic_menu_row`, `hud_classic_menu_cursor`, `hud_classic_menu_command`, `hud_classic_option_value`, `hud_classic_menu_tabs`, `hud_classic_menu_background`, `hud_classic_items_category`, `hud_classic_items_status_button`, `hud_classic_equip_stats`, `hud_classic_dp_pp_bars`, `hud_classic_gem_slots`, `hud_classic_stat_page`, `hud_classic_ok_mark`, `hud_classic_storage_buttons`, `hud_classic_storage_counts`, `hud_classic_loot_capacities`, `hud_classic_discard_count`, `hud_classic_rename_grid`, `hud_classic_rename_box`, `hud_classic_file_menu`, `hud_classic_file_element` (all on); the Lua console's Bench toggles those of status, chain, buff, damage and gauges.

**The parity gate** (`pc/src/classic/classic_parity.c`; plan §3). The
hooks (`PC_CLASSIC`, `pc/src/classic/classic_hook.h`; `PC_HUD` is one)
capture the packets of their range in ordering-table order from their
slots, each kept as its words without the link and its widescreen anchor.
With the parity mode on (`ROOD_CLASSIC_PARITY=1` or RPC `classic
{parity: true}`; dev mode only) the game draws and the classic claim of
the same element draws into a shadow slot (packets after the game's,
never linked, given back); both are compared packet for packet: code,
colours, vertices, texels, CLUT and page, sizes, draw modes, anchor. Text
needs nothing more: a glyph sprite's texels and CLUT are its code, and
ui_text restyles at raster time, after the gate. What the GPU never reads is
not compared: a raw-textured primitive's colour and a textured polygon's
pads after its last two texel pairs (the game's hand-built quads leave
them as the buffer held them).

**Immediate draws** (`PC_CLASSIC_IMMEDIATE`, classic_hook.h; the recorder in classic_parity.c, the site in
hud_bridge.c). Some screens draw at once (`DrawPrim`, `LoadImage`) instead of through the ordering table: MENU7's
file menu, MENUF's uploads. Their range records, in order, every packet the statement draws (its words, as an ordering-table
packet's) and every VRAM transfer (an upload as GP0's copy command with its rectangle and a hash of its pixels, a
read with its rectangle); that list is the kind's capture, compared as packets are. While a classic claim on the
kind is honoured the recorder holds the game's draws and uploads back (reads are done: the game reads them); at the
range's end the claim draws at once in their place, or, with no claim (or a draw that fails), the held ones are
done after all, in their order. In the parity mode nothing is held: the game draws, the claim's draw is made into
the list only. A kind may keep its uploads' pixels (`menuf_load`: up to 160 KB a range, MENUF's three TIMs): its
record then gives each upload a handle that `d.image_load` takes in place of a string, in the parity mode too.

| RPC | |
|---|---|
| `packets {kind}` | a site's last capture (`status`, `chain`, `buff`, `damage`, `gauges`, `figure`, `status_icons`, `battle_mode`, `ability_icons`, `actor_word`, `timer`, `emote`, `text_frame`, `text_band`, `box_text`, `image`, `target_row`, `target_line`, `target_marker`, `menu_background`, `menu_command`, `menu_row`, `menu_cursor`, `menu_tabs`, `option_value`, `items_category`, `items_status_button`, `equip_stats`, `dp_pp_bars`, `gem_slots`, `stat_page`, `ok_mark`, `storage_buttons`, `storage_counts`, `loot_capacities`, `discard_count`, `rename_grid`, `rename_box`, `file_menu`, `file_element`, `menuf`, `menuf_load`, `status_panel`), normalised: each packet's words, kind, vertices, texels, CLUT, page, colour, the glyph it shows; in the parity mode the Lua draw's beside it. Sites capture from the first call |
| `packets {start, end}` | the packets of a guest range in allocation order |
| `classic {parity, reset}` | per kind: frames compared, matched, mismatched (by field), skipped (why), the first difference with both packets |
| `menuf {submenu, boss}` | tests: MAINMENU's screen 15 (MENUF) opened as BATTLE opens it after a fight, `vs_main_stateFlags.menuFSubmenu` set to `submenu` (0 the congratulations, 1 / 2 the time attack's start / end, 3 the heal bonus: nothing drawn, 4 / 5 / 6 the cube puzzle's start / end / quit) and, given, `timeAttackBoss` to `boss` (the course, 0..6); only in the field with Ashley free to act; `{opened}` |

`pc/tests/classic_parity.py` (run.py `classic_parity`, the null renderer;
also in verify.sh's CPU lane) plays every element of each kind with
`rood.dev.show`'s spawners: each timing and success word, chains 1, 2, 3,
9, 10, 99, first held, then its whole life (slide in, held, slide out:
every age compared, the slides required); the gauges through HP / MP of
one to four digits, empty and full, RISK ZERO and RISK's bar, the display
sliding out and back (Ashley's record and `D_800EB9AF` written); the figure in both
stances at all five limb conditions and their threshold boundaries, mixed conditions,
and hidden/restored by the menu slide and lower-screen UI gate. Status icons cover all
32 IDs on/off and clipped (including zero-width cells), counts 1/2/3/8/9/16/32,
all carousel phases at tickspeed 2 and 4, invalidated slots, visibility and panel slide.
BATTLE MODE covers entry/exit, hidden, pulse extrema and phase wrap at both tickspeeds,
all panel slide positions and the right anchor. The same-mark state proof includes an
active carousel and advancing BATTLE MODE phase, in Lua and fallback at both tickspeeds.
Ability icons cover all nine groups, locked/enabled/disabled availability, every entry
index, hide/hidden, and real weapon-slot mode off/on with occupied and empty slots.
Their counter counts group comparisons (nine per display frame); `packets` shows the
last group. State parity checks normal and occupied weapon-slot modes held open, then
weapon-slot exit, preserving C names/markers and side effects. Buffs cover every
equipment and class / affinity word, both signs, digits of one to three figures and
the rise (10, 20, 30); the numbers HP and MP damage and healing (the four colours,
both bounces) of one to three figures and each stat's change both ways, every age of
their lives; the actor words every combat label's whole life plain and lowered, MISS from
both of the loop's MISS calls (a number's, a word's) and two words at once (records
written as the game's spawners write them); state parity holds a buff and a number beside
SLOW! and plays a lowered stat's word out from the mark; the timer every digit in each
place, the carries' boundaries and the overflow (59:59:99), hidden and shown, and runs in
both state parity runs. Emotes play in their own game at the opening (the `first_bubble`
checkpoint: actors on screen): every kind on every actor with both side bits (all six balloon
sides), then one emoting actor moved through a grid of places beyond the view (its position
written each frame) for the off-screen arrow, all eight directions; state parity there for three
kinds. The proof masks what a classic draw's later allocation moves: the primitive buffers, the
ordering tables (the text boxes' private one, D_800F4CD0, too) and the packet pointer and its
copies (D_800F1CC0, D_800F1BB8). The talk bubble's frame plays there too: the duke's
bubble zooming in, every shape of the frames' table at each flip and tail (written into the
open box), the next two lines' bubbles closing and opening; with `ui_bubbles vector` on, the
vector bubbles verify rood.classic's frames as the game's. The name window's band: two whole
lives, then held from the top and the bottom with one to four lines and each icon (its icon
compared too: it links into `vs_scratch.unk8`); state parity holds it. The bubble's text at the
opening: the duke's line, its box as other kinds (font table, colour style) and selections
(brightness), the next line typed out glyph by glyph; state parity there; the name window's
text, in the save room, is compared with its band. The target rows, in the save room with targeting up (no
enemy about: the gate writes the rows as func_800C5798 does): every part's name on a row's whole life
from either side (its slide, the plate's fade 8..0, its slide out), four rows at once on each side and
bank with one selected, a deselected row's fade; the selected row's leader lines from points about
Ashley in every direction to a row on either side, at their sweep's ages (every age 0..33 once);
default, fallback and state parity with targeting up. The area target's marker in a fourth game from the
save room (Explosion's target through the original menus): moved about with the d-pad, every
brightness step locked and with its brackets, Square held; its default, fallback and state parity
there. The part details are text box 7 (the band, its text): compared as those kinds. The images play in a third game, New Game
from the title: from the first card's first frame (a mark) the default and fallback and state
parity, then back to the mark and the opening cards' whole lives compared (a routine's own
widescreen range in the shadow is noted, not recorded, and compared as the anchor). It
requires no unwaived mismatch and waivers = declared overrides; checks the
default (drawn by rood.classic), the fallback (setting off, a raising
draw, the plugin off) and state parity (from one mark, rood.classic
drawing then the game: every vsync's state the same but the packets and
the host clock: Oracle page digests). Reports `<out>/<kind>.json`.
Result 2026-10-20: status 576 frames, chain 558, gauges 1192, 0
mismatches; state parity 120 of 120 vsyncs. 2026-10-28: buff 2,103 frames
(35 states), damage 1,224 frames (36 states), actor_word 1,920 frames (61
states), timer 60 frames (14 states), emote 134 frames (44 states), text_frame 917 frames (146 states), text_band 216 frames (43 states), box_text 878 frames (6 states; and the
name window's text in the save room, 124), image 717 frames, target_row 474 frames (37
states), target_line 502 frames (36 states), target_marker 150 frames (15 states), 0 mismatches. The
menus (`run_menus`, the game's Option screen: the list, the nine flyouts, our pages, Keep these
settings?): menu_row 10,700 frames, menu_cursor 492, menu_command 1,507, option_value 2,791 (18 states), 0 mismatches;
state parity 120 of 120 vsyncs at the list and as a page's rows come in. 2026-10-30, Items' own panels
(`tour_items`, `tour_status_views`): items_category 4,672 frames, items_status_button 2,233, equip_stats 2,543,
dp_pp_bars 1,673, gem_slots 386, stat_page 2,579 (81 states), 0 mismatches; state parity 120 of 120 at the Items
list, a weapon's and a grip's status view.

## Photo mode foundation

What a photo mode plugin is built on (`pc/src/photo.h`, `photo.cpp`; Lua:
`pc/src/lua/lua_api_photo.cpp`; the post overrides in `post_profile.cpp`).
Each part is behind its own capability, declared in `plugin.lua` and granted
as any other (RPC `lua {action "grant"}` in dev mode, `plugin_grant` lines);
a table or function exists only when granted. Everything a plugin holds is
its own and stacked with other plugins' (the pause holds while any plugin
holds it, hidden layers add up, post overrides apply in the order the
plugins began), and is given back when it lets go, when it unloads or
reloads (an edit, a revoked grant, turned off), when it is disabled for its
faults, and at an engine reload. An input handler that raises gives the
input back at once. Nothing of it is saved: settings.cfg, the post presets
and room files never see it, and the game's state is untouched (below).

| Capability | Functions |
|---|---|
| `game.pause` | `rood.game.pause([{audio, duck}]) -> true \| nil, why`, `resume()`; `rood.game.paused()` (any plugin) needs no grant |
| `camera` | `rood.camera.begin()`, `finish()`, `active()`, `get()`, `set{...}`, `orbit(yaw, pitch[, zoom])`, `move(right, up, forward)` |
| `post` | `rood.post.list()`, `get(key)`, `set(key, value)`, `reset([key])`, `overrides()` |
| `ui.hide` | `rood.ui.hide{hud, game2d, overlay, actors}` (no table: the three layers), `show()`, `hidden()` |
| `capture` | `rood.capture.photo{scale, path, done} -> path, id[, note]`, `status(id)`, `folder` |
| `input.capture` | `rood.input.capture(fn)`, `release()`, `captured()`, `state()` |

### The pause and the frozen moment

`rood.game.pause()` holds the game at the next vsync boundary (at once when
asked between frames, as from the REPL or the RPC): no vsync advances (RPC
`step`s asked meanwhile are dropped, so a test's `run` returns at the same
vsync), no game-time Lua (ticks, game-clock timers). The sound:
`audio = "stop"` (the default: nothing fed, the queued tail plays out, then
silence), `"keep"` (the sound driver's ticks and the SPU go on in real time,
nothing else of the game: the music and the sound loops play on, sounds
already playing finish, no new one starts since the game's logic is held)
or `"duck"` (kept, at `duck` 0..1 of the master volume, default 0.35). With
several holders the quietest wins. Kept, the sound's RAM moves on while the
rest of the game stays M's: each render first marks the moment again (so
its rewind keeps the sound where it got to), the vsync and every other part
of the digest stay, and on resume the game goes on with the music where it
is (RPC `photo` `audio`, `audio_vsyncs`). The window, the overlay, Lua's frames
(`frame` events, `{clock = "real"}` timers), the post stack (the focus and
auto exposure follow real time while held) and the RPC go on. Refused, with
the reason, while it is not safe: loading (no room: a door, a movie), a room
transition, memory card I/O (a card file open, the save screen). The title
and the attract demo may be held.

What changes the picture while held (the camera, the hidden HUD and 2D) is
drawn by a **render**: the game runs from the held moment M for
`render_frames` game frames (4; RPC `photo {render_frames}`) with nothing to
see or hear (no presents, no audio, no input, no Lua ticks, deterministic
scanline timing so every render of M is the same, memory card writes
refused, RPC requests waiting), then its state is rewound to M
(`state_rewind.c`'s photo slot; the RPC's `snapshot mark` slot is its own)
while the picture stays. So the game's state, its digest (RPC `snapshot
hash`), its vsync and every `rood.game` query stay M's the whole time; the
picture shows the moment four frames on, the same each render. A render
costs the CPU time of those frames, unpaced: about 5 ms (8 vsyncs). Renders
need the hardware renderer and save states (x86); without them the camera
and the hidden layers do not show while held (RPC `photo` says why).
Resuming continues from M exactly. Each camera session also checkpoints its
**original renderer presentation**: full-resolution live/replay colour,
G-buffer and depth targets, the displayed rectangle and interpolation state,
and deep copies of the native lists, palettes and referenced meshes. Camera
finish (even if another owner keeps the game paused) and release of that
session restore these, not a new render of M. The baseline is the picture
at camera begin, including any earlier non-camera photo renders; it can be
M-1 or an interpolated frame. The host poll allocates the checkpoint
before the first borrowed render, outside budgeted Lua callbacks. Allocation
failure releases the camera, emits `photo.released`, and reports `photo.error`
without rendering a borrowed view. A renderer reconfiguration that
makes restoration incompatible is reported in `photo.error`, never silently
claimed as an exact restoration. Loading or seeking to another game state
discards the old presentation checkpoint and releases/notifies the camera
owner (other pause owners remain held). The RPC `renders` counter counts
completed presentation updates, including restoration; a restored image has
`last_render_vsyncs: 0` because no game frames were simulated.

### rood.camera

The editor's live free camera (`editor_view.cpp`), borrowed: its eye, yaw
and pitch for the plugin's time, the editor's own camera given back after,
the editor not turned on. World units (y down), degrees. While begun,
occlusion (see-through walls, `occlusion.c`) is suspended: its fades follow
the game's camera, so the held moment's renders draw every wall solid (the
setting itself is kept; RPC `occlusion` lists no subjects meanwhile). The
room is drawn whole: every group it has, without the cutaway walls or the
hide list the game uses for its own camera (the black shells, which mask the
void outside the room, left out), and the sky about the free camera's eye
(`photo_full_room`, RPC `photo {full_room}`; pc/docs/Native_Rendering.md "A
plugin's free camera sees the whole room").

| | |
|---|---|
| `begin() -> true \| nil, why` | needs the game paused (or asked to be) and a room; starts at the game's camera (the same picture), the orbit's target on Ashley's middle (else 1500 units ahead) |
| `finish()` | releases the borrowed camera and redraws the held moment with the game's camera; when the last pause owner resumes, the original renderer presentation is restored exactly rather than regenerated from the later pause boundary |
| `get() -> {position, target, yaw, pitch, roll, fov, collision}` | nil unless begun by this plugin |
| `set{position, target, yaw, pitch, roll, fov, collision}` | any of them: `position` / `target` as `{x, y, z}` or `{x =, y =, z =}`; a target without yaw / pitch turns the camera to it; a position alone keeps the direction (the target moves along); `pitch` -89..89; `roll` (-180..180) is the game's own view roll (`vs_scratch.camera.angles.vz`); `fov` (5..120, vertical) scales the game's projection distance; `collision` keeps the eye in front of the room's solid surfaces between the target and it |
| `orbit(yaw, pitch[, zoom])` | about the target, the distance times `zoom` |
| `move(right, up, forward)` | in view space; the target moves along |
| `active()` | this plugin has the camera |

### rood.post

The post profile's live values by key: every field of `post_profile.cpp`'s
table (depth of field: `dof`, `dof_focus`, `dof_focus_depth`, `dof_fstop`,
`dof_aperture`, `dof_max_blur`, bokeh; `tilt_shift` and its band; exposure:
`exposure`, `exposure_mode`, auto exposure; bloom; `chromatic_aberration`;
`vignette`; grading: white balance, the four ranges' saturation / contrast /
gamma / gain / offset, `lut`, `lut_intensity`; `tonemapper`; film `grain`;
...) and three of the photo mode's own: `preset` (a named post profile from
`pc/assets/post` or `pc/user/post` under the plugin's values: a filter;
`""` none), `format` (the picture cropped to an aspect ratio, width over
height, 0 the whole picture: bars over the live view, the photo cropped to
it) and `format_color` (the bars' colour, `0xRRGGBB`, 0 black).

| | |
|---|---|
| `list()` | `{key, kind, group, label, tip, components, min, max, log, integer, default, choices, labels}` per key (`kind`: `bool`, `float`, `enum`, `color`, `vec4`, `text`) |
| `get(key)` | what the stack resolves now (every layer and every plugin's overrides): a boolean, a number, a name, `{r, g, b}` / `{r, g, b, a}`, a string |
| `set(key, value) -> true \| nil, why` | this plugin's override: numbers clamped to the field's range, names checked; applies at once (no blend) |
| `reset([key])`, `overrides()` | this plugin's overrides gone (one, or all); the keys it set and their text |

The overrides are a layer over every other (before the lens blur's gate:
depth of field and tilt-shift stay off on the title, in menus, without
Ashley), never written: the user's layers (RPC `post` `layers`) are as they
were.

### rood.ui

`hide{hud = true, game2d = true, overlay = true}` (any of them; no table:
all three), `show()` (this plugin's), `hidden()` (every plugin's together).
`hud`: the HUD's elements (the status display: figure, gauges, status icons;
timing words, chains, buffs, damage numbers; the words over the actors and
the time attack's timer too, RPC `photo {hud_words, hud_timer}`, 1 by
default), every `PC_CLASSIC` / `PC_HUD`
site's packets made no-ops after the game ran them (claims' classic packets
too), and the modern pass (Lua claims, the Travel prompt, the save state
notice). `game2d`: the rest of the game's 2D in the field: the packets
linked at the ordering table's first `game2d_slots` entries (35: the text,
window and HUD layer, `vs_scratch.unk4` +- 16 entries; RPC `photo
{game2d_slots}`): text boxes, the name window, the minimap, targeting
marks; and anywhere in the ordering table, the 2D anchored in the world:
packets drawing from the battle page (VRAM (448, 256): the emote bubbles
over actors, the status words and numbers over heads); native lists'
markers and mode packets are kept. `overlay`: the
developer overlay (its windows and the free camera's grid) as if closed.
`actors = "ashley" | "others" | "all" | "none"`: characters left out of the
renders (actor_native.c: a model drawn from an actor record's memory, slot
0 Ashley, the others enemies and NPCs; its drop shadow too, and the weapon
and shield it holds (their own records, `vs_battle_wepModels`, by their
holder's slot; RPC `photo {actors_held}`, 1 by default); props stay);
`hidden().actors` says which (nil: none); `hidden().hud_words`,
`hud_timer`, `actors_held` the tunables.
While held, a change is drawn by a render.

### rood.capture

`photo{scale = 1, path = nil, done = fn} -> path, id[, note]`: the next
picture presented once the frozen moment is drawn as it is to be (no render
waiting), post applied, read back before the overlay (so no ImGui: the dev
overlay, modern HUD claims and a plugin's own drawing are never in it;
the HUD and 2D the game draws are, unless hidden), cropped to the format,
written as a PNG with a timestamped name (`rood-boi-YYYYMMDD-HHMMSS.png`,
`-2` ... when taken) in `pc/user/photos` (`ROOD_PHOTOS`; `rood.capture.folder`).
`path`: only inside a folder the plugin's `fs.write` grant names (audited).
`scale` 2..4: not supported yet; the photo is the window's resolution, nothing
upscaled, and a third result says so. `done(result)` and the plugin's
`photo.capture` event get `{id, ok, path, width, height, scale, error}`;
`status(id)` the same meanwhile (`done` false).

### rood.input

`capture(fn) -> true \| nil, why` (one plugin at a time): the keyboard (its
keys while the overlay does not take them), the gamepad and the RPC's input
go to `fn(e)` instead of the game, which sees no buttons and no stick
(what it held is let go). Events: `{type = "button", name, down, device}`
(the PS1 buttons by name: `cross`, `circle`, `start`, `up`, ...; `device`
`pad`, `keyboard` (through the key bindings), `rpc`), `{type = "key", name,
down, device = "keyboard"}` (any key by its settings name: `w`, `left`,
`f1`), `{type = "analog", dt}` each frame a stick or trigger is off rest, and
`{type = "released", reason}`; each also carries `held` (the buttons held:
`{cross = true, ...}`), `left` and `right` (`{x, y}`, -1..1, down +y), `l2`,
`r2` (0..1; -1 bound to buttons). `state()` gives the same now. `release()`.

The safety: Escape (taken before the developer overlay, which it would
open) or Start gives the input back (`released` with the reason; the Start
press does not reach the game until let go). With nothing captured while a
plugin holds the game, Escape or Start gives every plugin's photo holds back
(each told by a `photo.released` event `{what, reason}`).

### RPC and the engine reload

RPC `photo` (`pc/tools/rood_rpc.py`: `Game.call("photo")`): `phase`
(`running`, `asked`, `held`, `rendering`), `owners`, `held_vsync`,
`renders`, `last_render_ms`, `last_render_vsyncs`, `mark`, `error`,
`camera` (owner and pose), `hidden`, `format`, `input`, `captures`; params
`render_frames`, `game2d_slots`, `hud_words`, `hud_timer`, `actors_held`
tune it. An engine reload
(`build_boot.py --engine-library`) is refused while a render runs
(`retry`); otherwise every hold is given back first (the state is M's) and
the plugins, started again by the next library, ask again.

### Example

`pc/tests/lua/plugins/test.photo` (a test fixture, not shipped):

```lua
-- plugin.lua: capabilities = { "game.pause", "camera", "post", "ui.hide", "capture", "input.capture" }
function M.start()
  assert(rood.game.pause())
  assert(rood.camera.begin())
  assert(rood.camera.orbit(35, 12))            -- degrees about Ashley
  assert(rood.post.set("dof", true))
  assert(rood.post.set("dof_fstop", 1.4))
  assert(rood.post.set("exposure", 0.7))
  assert(rood.ui.hide { hud = true, game2d = true, overlay = true })
  assert(rood.input.capture(function(e)        -- the game sees nothing meanwhile
    if e.type == "button" and e.name == "cross" and e.down then rood.camera.orbit(-10, 0) end
  end))
  return rood.capture.photo { done = function(result)
    M.photo = result
    rood.input.release() rood.ui.show() rood.post.reset() rood.camera.finish() rood.game.resume()
  end }
end
```

### What the foundation lacks

For the reference card (`rood.photo` below): no pose (Ashley's poses are the
game's animations and a held moment is rewound after every render, so a pose
is not something to set read-only; a pose picker needs an animation query
and a way to hold a chosen frame), no shaped masks or borders for the format
(crop and bars only), no LUT files shipped (`lut` takes a `.cube` path, and
a plugin cannot list `pc/user/post/luts` without an `fs.read` grant). Missing
as effects: photos above the window's resolution (`scale` > 1: needs an
offscreen present at a larger size), lens distortion, halation, motion blur,
a light leak or frame overlay, and the modern HUD in photos (ImGui is not
captured). The 2D the game draws as screen sprites (some particles, sky
tiles) follows the camera only as the game draws it from the free camera.

### Developer tool windows

The overlay's tool windows written in Lua (`pc/src/lua/lua_api_imgui.cpp`,
`lua_dev_tools.cpp`; plan `pc/docs/plans/2026-10-27-rood-imgui.md`): a window
in Window, drawn with Dear ImGui from a plugin, edited and hot-reloaded while
the game runs. They need the `dev.tools` capability: a shipped plugin has it
when it declares it, yours when you grant it (RPC `lua {action "grant"}`;
tied to the code's hash like every grant), and in dev mode (`ROOD_RPC` set or
`ROOD_DEV=1`) declaring it is enough, so a tool you are writing keeps it
through every save.

```lua
-- plugin.lua: capabilities = { "dev.tools" }
local imgui = rood.imgui
local strength, name = 0.5, "lamp"
rood.dev.window {
  id = "lamps", title = "Lamps", size = { 320, 200 },
  draw = function()                         -- between the host's Begin and End
    local changed
    changed, strength = imgui.SliderFloat("strength", strength, 0, 1)
    changed, name = imgui.InputText("name", name)
    if imgui.Button("Pause") then rood.dev.rpc("pause", { paused = true }) end
    local g = rood.dev.rpc("graphics")      -- a reply is a table
    imgui.Text(string.format("scale %s", tostring(g and g.scale)))
  end,
}
```

| | |
|---|---|
| `rood.dev.window{id, title, draw[, open][, keys][, size = {w, h}][, size_em][, flags][, replaces][, menu = "Window"]}` | a checkable entry of Window (sorted by title); `draw()` runs every presented frame while it is open and the overlay shows. Returns `"<plugin>/<id>"`; registering the same id again replaces it. Its open state is yours: kept across reloads (`open` is only the first state) and runs (settings `dev_window <id> 0\|1`). `keys`: while it is focused the editor's shortcuts (Ctrl+Z, H, ...) stand aside. `size_em`: the first size in font heights. `flags`: `imgui.WindowFlags_*`. `replaces`: a C++ window's name (`"material_review"`): while this window is registered the C++ one is neither drawn nor listed, and its open flag is this window's open state (the plugin off: the C++ one is back) |
| `rood.dev.window_open(id[, open]) -> open`, `rood.dev.remove_window(id)` | |
| `rood.dev.command{id, title, run[, category][, shortcut]}` | a command in the editor's registry (`pc/src/editor_commands.h`): the command palette (Ctrl+Shift+P) lists and runs it, so does RPC `editor_command`. `run()` runs under the plugin's call budget; `category` defaults to the plugin's id, `shortcut` is a label only. Returns `"<plugin>/<id>"`; registering the same id again replaces it. Every `rood.dev.window` also registers `window.<plugin>/<id>` (opens and focuses it). `rood.dev.remove_command(id)` |
| `rood.dev.rpc(method[, params]) -> result \| nil, error` | one developer RPC method (`pc/tools/rood_rpc.py`'s, dev_rpc.c) in-process: the same handler, its undo; refused: `lua`, `quit`, `restart`, `reload`, `auth`, memory `read`/`write`, `state_flag`, `record`, `replay`, `input_record`, and a call from inside another |
| `rood.imgui.*` | Dear ImGui by its own names (below) |

`rood.imgui` keeps Dear ImGui's names, so its documentation applies:
`ImGui::SliderFloat` is `imgui.SliderFloat`, `ImGuiWindowFlags_MenuBar`
`imgui.WindowFlags_MenuBar`, `ImGuiCol_Text` `imgui.Col_Text`,
`ImGuiKey_Enter` `imgui.Key_Enter`. Values go in and come back Lua's way:
`changed, v = imgui.SliderFloat(label, v, min, max[, format, flags])`;
`changed, text = imgui.InputText(label, text[, flags])`;
`changed, rgb = imgui.ColorEdit3(label, {r, g, b})` (a new table);
`changed, i = imgui.Combo(label, i, items)` (1-based); `visible, open =
imgui.Begin(name, open, flags)`, `selected, open = imgui.BeginTabItem(...)`;
`clicked, selected = imgui.MenuItem(label, shortcut, selected, enabled)`.
A canvas (the node editor's needs, `pc/plugins/rood.material_graph`):
`x, y = imgui.GetCursorScreenPos()`, `imgui.SetCursorScreenPos(x, y)`,
`GetCursorPos` / `SetCursorPos`, `GetWindowPos`, `GetWindowSize`,
`pressed = imgui.InvisibleButton(id, w, h[, flags])` (an item: `IsItemHovered`,
`IsItemActive`, `IsItemClicked` apply), `GetItemRectMin` / `GetItemRectMax`,
`SetNextItemAllowOverlap`, `x, y = imgui.GetMousePos()`,
`IsMouseDown/Clicked/DoubleClicked/Released([button])`,
`IsMouseDragging([button[, threshold]])`, `dx, dy =
imgui.GetMouseDragDelta([button[, threshold]])`, `ResetMouseDragDelta`,
`IsMouseHoveringRect(x0, y0, x1, y1[, clip])`, `GetMouseWheel()`,
`PushClipRect(x0, y0, x1, y1[, intersect])` / `PopClipRect()` (a scope, like
every Push), and the window's draw list in screen coordinates with colours
as `{r, g, b[, a]}`: `DrawList_AddLine(x0, y0, x1, y1, color[, thickness])`,
`DrawList_AddRect(x0, y0, x1, y1, color[, rounding[, thickness]])`,
`DrawList_AddRectFilled(x0, y0, x1, y1, color[, rounding])`,
`DrawList_AddCircle(x, y, r, color[, segments[, thickness]])`,
`DrawList_AddCircleFilled(x, y, r, color[, segments])`,
`DrawList_AddTriangleFilled(x0, y0, x1, y1, x2, y2, color)`,
`DrawList_AddBezierCubic(x0, y0, x1, y1, x2, y2, x3, y3, color[, thickness])`,
`DrawList_AddText(x, y, color, text)`. Positions come back as two numbers,
like `GetContentRegionAvail` and `CalcTextSize`.
Text is never a format string (`imgui.Text(string.format(...))`). Dear
ImGui's own indexes stay (`TableSetColumnIndex` is 0-based). There are:
Begin/End, BeginChild/EndChild, SetNextWindowSize/Pos, IsWindowFocused/Hovered;
Text (TextUnformatted), TextWrapped, TextDisabled, TextColored(`{r,g,b,a}`, text),
LabelText, BulletText, SeparatorText; Button, SmallButton, Checkbox,
RadioButton, Selectable, ProgressBar, Bullet; SliderFloat, SliderInt,
DragFloat, DragInt, DragFloat2/3/4 (tables), InputFloat, InputInt,
InputText, InputTextMultiline; Combo, BeginCombo/EndCombo,
BeginListBox/EndListBox; ColorEdit3/4; SameLine, Separator, Spacing,
NewLine, Dummy, Indent, Unindent, BeginGroup/EndGroup,
PushItemWidth/PopItemWidth, SetNextItemWidth, AlignTextToFramePadding,
GetContentRegionAvail, CalcTextSize, GetFontSize, GetFrameHeight,
GetTextLineHeightWithSpacing, GetStyleColorVec4 (`{r, g, b, a}`);
SetScrollHereY, SetScrollY, GetScrollY; ListClipper_Begin(count[, row height]) and
`more, first, last = ListClipper_Step()` (cimgui's names for Dear ImGui's
ImGuiListClipper: only the rows in view drawn; 1-based, inclusive; Step
until it returns false);
TreeNode (TreeNodeEx: flags), TreePop, CollapsingHeader, SetNextItemOpen;
BeginTable/EndTable, TableSetupColumn, TableSetupScrollFreeze,
TableHeadersRow, TableNextRow, TableNextColumn, TableSetColumnIndex;
BeginTabBar/EndTabBar, BeginTabItem/EndTabItem; BeginTooltip,
BeginItemTooltip, EndTooltip, SetTooltip, SetItemTooltip, SetClipboardText;
OpenPopup,
BeginPopup, BeginPopupContextItem, EndPopup, CloseCurrentPopup;
BeginMenuBar/EndMenuBar, BeginMenu/EndMenu, MenuItem; IsItemHovered,
IsItemClicked, IsItemToggledOpen (a tree node's arrow), IsItemActive,
IsItemEdited, IsItemDeactivatedAfterEdit,
IsAnyItemActive, SetItemDefaultFocus, SetKeyboardFocusHere; PushID/PopID,
BeginDisabled/EndDisabled, PushStyleColor/PopStyleColor,
PushStyleVar/PopStyleVar; PlotLines, PlotHistogram; IsKeyPressed,
IsKeyDown, IsKeyReleased, HasKeys, GetIO (a copy: KeyCtrl, KeyShift,
KeyAlt, WantTextInput, DeltaTime, ...), GetTime.

Keys answer only while the window has the keyboard: focused, nothing being
typed, no right mouse held, the camera not flying (`imgui.HasKeys()`), so a
tool's shortcuts never fire while you type into one of its fields or fly.

Errors never unbalance the overlay. Every Begin/Push is checked against
its End/Pop before Dear ImGui sees it (a stray `imgui.End()` is a Lua
error, not Dear ImGui's assertion). A draw that raises, or returns with a
Begin or Push left open, is rewound with Dear ImGui's error recovery (every
window, table, tree, ID and style push it opened closed) and its window
shows the error and its traceback with Retry until Retry, reopening it or
the plugin reloading; one fault is counted (the fault policy as for every
handler). A draw over its budget is drawn again next frame. `rood.imgui`
answers only inside the plugin's own draws.

The shipped `rood.dev_example` (off: turn it on in the Lua console's
Plugins list) is Window > Lua example: the game's status by `rood.dev.rpc`,
Pause / Resume, an RPC explorer, a few widgets. Edit
`pc/plugins/rood.dev_example/main.lua` while it is open: the window redraws
from the saved file and stays open. The shipped `rood.material_review`
(off) is Window > Material review in Lua (pc/docs/Materials.md, "Reviewing
in the game"): the C++ window's picture pixel for pixel and its keys, on RPC
`material_review` alone. The shipped `rood.materials` (off by default) is
Window > Materials in Lua (pc/docs/Materials.md, "Masters and instances"):
the instance tree, the master's parameter rows, the assignment and the
instance tools, on RPC `material_instance` alone (`status` each frame, the
tree read again only when the catalog's structure or its unsaved layer
changed, `get` for the selected instance); off, the C++ window is Window >
Materials. RPC `lua {action "windows"}` lists the
windows (`id, title, plugin, menu, open, keys, focused, replaces, failed, error,
frames, failures`); `lua {action "window", id, open, focus, retry}` opens,
focuses or retries one.

The editor is one docked workspace (`pc/src/editor_shell.cpp`, plan
`pc/docs/plans/2026-10-30-editor-shell-plan.md`): the Outliner (with the
Library tab) left, the Game panel (the game picture) centre, Details right,
and the tool windows, yours included, tabbed below it the first time they
open; drag any tab to dock it elsewhere or float it. Window > Reset layout
puts the default back (its proportions: Details > Graphics > Editor shell,
settings keys `editor_shell_left`, `_right`, `_bottom`; RPC `graphics` sets
them). The layout lives in `pc/build/boot/imgui.ini` (per user; a test's own
through `ROOD_IMGUI_INI`). RPC `editor_shell {action "state" | "reset" |
"palette" | "window", open, query, name}` reads the layout (its nodes, the
Game panel's rectangle, the picture's inset and view, the palette), resets
it, opens the palette with a query, or reports one window's docking by name;
`editor_command {action "list" | "query" | "run", query, id}` lists the
registry (`query`: ranked as the palette ranks) and runs a command.

Click-to-inspect (`pc/src/editor_inspect.cpp`, plan
`pc/docs/plans/2026-10-30-click-to-inspect-plan.md`): a plain click on a room
surface or a drawn model (a character, weapon or prop) in the Game panel
selects it (selection ids `surface:<zone>-<room>:<polygon offset>` and
`model:<hash>`, outlined in the view; Escape clears it, then closes the
editor as before). Details shows what it is, its mesh, the texture and palette
under the click (a nearest-sampled preview), the material entry and the layer
it came from (shipped catalog, user layer, unsaved edit, review preview), the
shader, and where the texture is used (the rollout survey's rooms,
`$ROOD_MATERIAL_ROLLOUT` or `pc/build/material_rollout`, with Go); "Open in
Material editor" is Details > Material on that texture. The Outliner's
"Surfaces and models" section lists the models drawn now and the surfaces
inspected. View > View mode and the commands `view.mode.<name>` (lit, unlit,
lighting, base_colour, vertex_colours, normals, roughness, metal, emissive,
ao, wireframe; the four material ones are Vulkan only, lit on GL). RPC
`editor_inspect {action "pick" | "state" | "items" | "outliner" |
"where_used" | "go" | "open_material" | "view_mode" | "view_modes", x, y |
u, v | world, select, id, key, clut, zone, room, name}`: `pick` selects (unless
`select` false) and replies the item as Details shows it (kind, room, polygon
or model, actor_slot, triangles, polygons, list, key, size, clut_key, tpage,
clut, box, entry, entry_source, entry_name, entry_preset, maps, shader);
`state` the primary's (or `id`'s); `where_used` the rooms (zone, room, name,
share, live) of `key` or a palette `clut` (default: the primary's texture)
and a `status` when the survey data is missing; `view_mode` sets a mode by
name and replies `previous` and `current` (a test restores with `previous`).

Master materials and instances (`pc/src/materials.cpp`,
`material_instances.cpp`, `material_preview.cpp`; plan
`pc/docs/plans/2026-10-30-master-materials-plan.md`; `pc/docs/Materials.md`
"Masters and instances"): the catalog's presets are root instances of the
standard surface master and its entries instances of their preset; an
instance may parent any instance. **Window > Materials** (command
`window.materials`) is the tree and the parameter panel (a row per parameter
of the master, inherited values greyed with the ancestor they come from,
Reset to the parent's value), the assignment (a texture, a palette, the
part-2 selection's texture or palette scoped to this room), New child,
Duplicate, Remove, Save / Revert (the unsaved layer Details > Material
shares). **Window > Material preview** (`window.material_preview`) is a
sphere shaded on the CPU with the shared GGX model, textured with the
instance's texture where it is drawn now, with the planes' per-texel values
and relief; lit like the room by default (`material_preview_room`: the
strongest light at Ashley as the key, the game's room light as the ambient,
times `material_preview_room_key` and `_room_ambient`, taken
`_room_height` above her feet), else by its own light; the settings keys
`material_preview_size`, `_light`, `_yaw`, `_pitch`, `_ambient`, `_swatch`,
`_room`, `_room_key`, `_room_ambient`, `_room_height`, `_relief` (RPC
`graphics`). RPC `material_instance {action "list" | "masters" | "get" |
"set" | "parent" | "new" | "duplicate" | "assign" | "assign_selection" |
"unassign" | "remove" | "select" | "save" | "revert" | "validate" |
"migrate", id, params, name, where, review, parent, of, root, key, size,
clut, scope, palette, out, offset, limit, masters}`: `list` pages the
instances (`offset`, `limit`; `masters` false leaves the masters out), each
with its own values (`own`), every parameter resolved (`values`), the ids
walked (`chain`), which ancestor sets each value (`from`: "" the master's
default), its match, scope and children, with the catalog's `problems`;
`set {params: {name: value | null}, reset: [names], master}` sets or resets to the parent
(`master`: another master, e.g. a node graph; the Materials panel's Master
combo; a colour parameter, `type` "color" with a `[r, g, b]` default, takes
`[r, g, b]` each 0..1 or null for all three, and shows in `own`, `values`
and `from` as its parts `<name>.r`, `.g`, `.b`; the files write it as one
`[r, g, b]`); `new {id, parent, params, root, master}` makes an unassigned child
(`root` true: a new preset); `assign {key, size, clut, scope}` and `assign_selection {palette}`
(the primary selection's texture, or its palette, in this room); `validate`
counts what resolves; `list {brief: true}` lists each instance as a tree row
only (id, name, parent, master, root, user, pending, preview, assigned: no
resolution); `get {slots: true}` adds the master's texture slots with the
layer each channel map comes from; `json {id}` replies the entry as the
catalog writes it (Copy JSON for the catalog); `migrate {out}` writes the user layer's instances into
a file (never the layer itself unless named); `status` replies the catalog's
`generation` (any change), `structure` (more than values: an instance added,
removed, re-parented, re-assigned or given another master), `file_reads`
(catalog files read so far), `dirty`, `pending` and `selected`. A value edit (`set`
with the same structure, as a dragged slider sends) is laid over the
catalog in memory: no file is read, `structure` stays, and a graph
instance's edit only refills the values table; the files are read on load,
Revert and `material reload` (and when one changes on disk) and written on
Save. RPC `material {action "palettes"}` lists the palettes drawn now by
CLUT slot (`depth`, `clut`, `key` of the colours now, `identity`, `variants`
seen this room, `uses`, `entry`): an animated palette is one palette, matched
by any of its variants and named by its identity (Materials.md, "Animated
palettes"); emissive entries' `match.clut` and RPC `emissive pick`'s
`clut_key` the same. RPC `material_preview {action
"state" | "select" | "open" | "save", id, open, path}` replies the sphere's
`digest` (and `digest_hex`, every bit), `mean`, `centre` and `edge` pixels,
the texture used, `relief` (0 none, 1 a height map, 2 estimated), `planes`
(the values from the material planes), `lighting` (`room`, `key_from`:
"light <n>", "sun" or "preview", `key`, `dir` in the preview's frame,
`ambient`) and the settings; `save {path}` writes the sphere as a PNG. RPC
`material list` and `reload` take `entries: false` (the uses and tunables
without the entries: a layer of hundreds of entries outgrows one reply).

Node material graphs (`pc/src/material_graph.cpp`, the Lua editor
`pc/plugins/rood.material_graph`; plan
`pc/docs/plans/2026-10-30-node-materials-plan.md`; `pc/docs/Materials.md`
"Node graphs"): a graph master is a master file with a `graph` (nodes,
links, promoted knobs); a node definition is a small GLSL function with
declared inputs and outputs in `pc/assets/materials/nodes/<name>.json` (the
user's under `<user maps layer>/nodes`). **Window > Material graph** (the
Lua plugin, on; `rood.material_graph/graph`) is the node palette, the canvas
(drag a node by its header, drag a link from an output pin to an input pin,
click a linked input to cut its link, right-click for a node, Delete removes
the selected node; pinned knobs on the node bodies) and the parameter panel
(every knob of the selected node with *Pin* and, for a number or a colour
knob, *Promote* to a master parameter instances then set in Window >
Materials: a slider, or a colour swatch). A value edit (a knob, an
instance's parameter) is live on the next frame: the generated include reads
every unlinked knob and every instance's parameters from a values table
(`graph_values[]`, the native draws' binding 7), so dragging a slider
rewrites the table, not the shader. A structural edit (a node, a link, a
promotion, an assignment) regenerates `material_graph.glsl` into
`pc/build/shaders/generated` (`$ROOD_SHADERS_GENERATED`) and the shader hot
reload swaps it in within about a second; an invalid graph outlines its node
and keeps the last good shader. RPC `material_graph {action "list" |
"nodes" | "status" | "values" | "get" | "new" | "set" | "save" | "remove" |
"generate" | "node_add" | "node_remove" | "node_move" | "link" | "unlink" |
"knob" | "pin" | "promote" | "reload", name, graph, id, type, x, y, from,
to, input, value, on, param}`: `list` the graphs (`user`, `dirty`, `ok`,
`error`) and the `status` (the generated file's `path`, `written`,
`generation` (its writes: a value edit leaves it), the `slots` in use, the
values table's size `values` and the game `time` it carries, the first
`error`, `node_problems`); `values` the table as the next frame uploads it
(`{key, value [x, y, z, w]}`: `time`, `k:<graph>:<node>.<input>` a knob,
`p:<instance>:<param>` a parameter); `nodes` the node library (each with
`inputs`: name, label, type `float|float2|float3|float4`, `default`, `min`,
`max`, `pin`, `color`, `knob`, `optional`, `time` (unlinked, it reads the
game time); `outputs`; `builtin`, `user`); `get` / every edit reply the graph (`graph.nodes` with
`pos`, `knobs`, `pinned`; `graph.links` `{from "node.port", to
"node.input"}`; `graph.promoted`), whether it generates (`ok`, `error`,
`error_node`), its promoted `params` and the `instances` using it with
their slots; `generate` replies the graph's own GLSL (`glsl`), the node
functions it calls (`functions`) and the whole include (`include`); `set
{graph}` replaces the whole graph; `save` writes the master file into the
user's graph folder (`$ROOD_MATERIAL_GRAPHS`, else `<user maps
layer>/masters`; a shipped graph saved becomes the user's copy of the name);
`promote {param}` saves as well (`param` null unpromotes; a number knob, or a
`float3` knob marked `color`); `knob {value:
number | [..] | null}` (null: back to the definition's default); `reload`
re-reads the node and graph files.

## Testing

- `ctest lua_probe`: the Lua side over a fake host: the grants (none: the
  functions absent; not elevated), pause refused with its reason, the camera's
  pose both ways, post values both ways (names, the format, presets), the
  hidden layers, a capture's path rules and its event, input events with the
  held state and sticks, a raising handler, the host's notices, an unload
  giving everything back, a revoked grant; a panel (`rood.draw.panel`).
- `pc/tests/run.py photo_foundation_smoke` (`--renderer gl` and `vulkan`):
  the example plugin granted, then step by step from the REPL and its own
  session to the end (above).
- `photo_engine_reload_smoke`: the same on the dev64 split build (built
  first, as `engine_reload_smoke` does), then an engine reload while the
  plugin holds the pause, the camera, hidden layers, a post override and the
  input: everything given back, the moment's state kept, the plugin started
  again with nothing held.

## Photo mode (rood.photo, shipped, on)

`pc/plugins/rood.photo` is the photo mode, made only of the foundation
above, `rood.draw.panel`, `rood.settings`, `rood.storage` and `rood.menu`.
It does nothing until its button is pressed: the binding slot `photo`
(`bind_key_photo` F12; `bind_pad_photo` `back+dpup`: Back (Select) held,
then D-pad up; System > Bindings: Photo mode), which the host sends as the
event `input.action {name = "photo"}` whether or not the input is captured,
so the same button enters and leaves.

**The pad's default.** Vagrant Story reads every button of its digital pad:
the D-pad, the face buttons, L1 / R1 (the camera's 45-degree turns), L2 /
R2, Start (the first-person view) and Select (the field camera's mode, on a
press: `146C.c`); the port adds the right stick (the camera), R3 (its reset;
R3 is `PADj`, which battle code reads too), L3 + R3 (fast-forward) and Back
for Travel at save points, and many pads' Share / Capture button is the
system's (the old default, `misc1`). So the photo mode takes a **chord** the
game never sees (`pc_bind_pad_mod`, `boot_gamepad.c`): while Back is held,
its PS1 buttons (Select) are kept from the game for up to 15 polls (~250 ms)
waiting for D-pad up; D-pad up then fires the photo mode, and neither
reaches the game (nor Travel); Back let go before that without D-pad up is a
Select tap the game gets at the release; Back held longer is Select as
always. Any port action may be a chord in `settings.cfg` (`bind_pad_photo
back+dpup`, `bind_pad_travel leftshoulder+start`, ...); a binding captured
in System > Bindings is a single element. D-pad up alone, or pressed before
Back, is the game's.

Entering is allowed in the field and in battle (`rood.game.mode()` `field`,
`battle`); elsewhere a short notice says why ("Photo mode: not in a menu",
"... not during a scene", "... not while loading", the title, the demo), and
the foundation's own refusals (a room transition, memory card I/O) are shown
the same way. Then: the game held (`rood.game.pause` with the Music
setting: keep by default, the music and sound loops playing on), the free
camera begun at the game's (its pivot on Ashley), the HUD, the game's 2D and
the developer overlay hidden, the input captured, and the card drawn as a
panel. Leaving gives back the input, the layers, the post overrides (the
format and its bars too), the camera and the pause, in that order; the
plugin unloading or reloading does the same by itself. Disabled for its
faults, the photo button loads it again and opens it with a notice ("Photo
mode was restarted after an error"; Budgets and faults).

| Pad (keyboard) | |
|---|---|
| L1 / R1 (Q / W, E) | the previous / next tab |
| D-pad up / down (arrows) | the row (the card scrolls) |
| D-pad left / right (arrows) | the row's value (held: repeats); on a preset: left saves, right loads |
| Left stick (I J K L) | Orbit: move sideways (J L) and the distance to the pivot (up / down); Free: fly (forward, sideways). The pivot stays within `radius` of where it began. The camera's only: while a plugin captures the input, the stick's D-pad (the game's menus') gives `rood.input` no button events |
| Right stick (F H T G) | Orbit: turn about the pivot; Free: look around |
| L2 / R2 (1 2; U O) | lower / raise |
| (C V) | the distance to the pivot |
| Circle (Z) | take a photo (`rood.capture.photo`, the "Photo size" setting's scale): "Saved <name>" |
| Cross (X), Start (Enter), Escape, the photo button | leave |
| Triangle (S) | hide / show the card; hidden, only the camera, Triangle, Circle and leaving act (`M.hidden_buttons`): the D-pad, L1 / R1, Square, R3 and Select do nothing |
| Square (A) | reset the row (its override dropped); on a preset: clear it |
| R3 (R) | reset the tab (the Camera tab: the camera too) |
| Select (Tab) | reset everything: every row, the filter, the camera |

The tabs (`M.tabs` in `data.lua`; each row live, a post override of the
plugin's never saved, or the camera, or the plugin's own; values shown with
units; a row that depends on another is dimmed and does nothing until it
applies):

| Tab | Rows (post keys) |
|---|---|
| Camera | Camera Orbit / Free; Field of view 10-110 degrees shown with the focal length giving it on a 24 mm sensor (`rood.camera` fov); Roll -45..45 degrees; Move speed (the Camera speed setting); Collision (`rood.camera` collision); Show Ashley, Show others (`rood.ui.hide{actors}`); Reset camera |
| Lens | Depth of field (`dof`); Focus: Camera subject, On Ashley, Screen centre, Cursor, Manual (`dof_focus`); Focus distance in metres (Manual; `dof_focus_depth`); Model Physical / Artistic (`dof_model`); Aperture f/1.2-f/22 in stops (Physical; `dof_fstop`); Blur amount (Artistic; `dof_aperture`); Lens mm or From FOV (`dof_focal_length`); Max blur, Near blur (`dof_max_blur`, `dof_near_blur`); Bokeh blades (Round, 3-9), Blade curve, Bokeh rotation, Anamorphic squeeze, Bokeh edge Soft / Even / Rim, Highlights, Bokeh fringe, Cat's eye (`bokeh_*`) |
| Effects | Vignette, Vignette size, Vignette shape (`vignette`, `vignette_gain`, `vignette_roundness`); Lens distortion (`lens_distortion`); Grain, Grain size, Grain colour; Aberration px; Bloom, Bloom threshold, Bloom size; Sharpen; Tilt-shift, its band, width and blur |
| Colour | Filter: Off, Noir, Sepia, Faded, Vivid, Warm, Cold, Bleach, Teal & orange (grades), then every post preset (Film, Cinematic, Clean, the user's); Filter strength (a grade blended over the picture's own values); Exposure EV; Contrast, Saturation (the master of `contrast`, `saturation`); Temperature K, Tint; LUT strength (with a LUT set) |
| Frame | Format: Fullscreen, 4:3, 3:2, 16:9, 1.85:1, 2.39:1, Square, 4:5, 9:16 (`format`); Bars: Black, White, Grey, Ink (`format_color`); Guides: Off, Thirds, Grid 4x4, Golden ratio, Centre (over the view, never in the photo) |
| Presets | six slots in the plugin's storage (`presets`): every row of the Lens, Effects, Colour and Frame tabs (not the camera's place), named after what they hold ("Noir, f/2.0, Cinema 2.39:1") |

Rows start at the live value (so entering changes nothing); a filter's grade
writes the rows on its keys (a starting point the rows then change).

Not offered, and why: a Pose row (the game has no pose system, and Ashley's
animation is game state: a paused moment is rewound after every render, so
changing it is not read-only); borders inside the photo
(the capture crops to the format; the bars' colour is for the live view);
naming presets (no text entry on a pad: they are named after their content).

Look and feel are tables in `data.lua` (hot-reloaded; the REPL's `M`):
`M.look` (the card's place, size and row count, the modern and classic
colours and typography, the tabs, the notices, the guides, the hints),
`M.audio` (the duck gain), `M.camera` (speeds, dead zone, radius,
collision, sensor, the keys, the repeat), `M.formats`, `M.filters`,
`M.preset_labels`, `M.apertures`, `M.focus`, `M.dof_models`,
`M.bokeh_edges`, `M.camera_modes`, `M.guides`, `M.bars`, `M.tabs`. The card
is drawn over the picture (never in the photo): with the modern look in the
labels font with the button prompt icons; with the classic look in the
game's window colours and its menu font (ui_text's typography) and the
buttons named in words, since the game's own button sprites cannot be drawn
over the picture.

The player's settings (the Option screen's Photo mode page, saved in the
plugin's own `settings.json`, never `settings.cfg`): Photo size (1x; 2x-4x
are not supported yet: saved at the window's size, the notice says so),
Camera speed, Invert camera Y, Music (keep, duck, stop), Thirds grid (the
Guides row's first value); the page also shows the binding.

Tests: its Lua tests (`tests/photo_test.lua`, in `lua_smoke`: where it
opens, the tabs' rows, keys and ranges, the choices, the settings and page)
and `photo_mode_smoke` (`--renderer gl` and `vulkan`): in the save room,
the button refused in the field menu; F12 enters; the tabs by L1 / R1 and
E and rows of each read back live (a dimmed row, the f-stops, a filter's
strength, units, Square, R, Select, a preset saved and loaded); the camera
keys; a photo; Cross leaves with the state digest, the vsync, the post
values and the picture as without the session (Music stop); D-pad up alone
does nothing, the pad's chord enters, F12 leaves, Start leaves; Music keep
(the sound driver runs, the vsync held); a host call made 6 ms slow every
frame never faults it; disabled by faults, F12 brings it back open with the
notice, and the cool-down brings it back by itself; settings.cfg untouched.

## Option pages

`rood.menu` (`pc/src/lua/lua_api_menu.cpp`; plan
`pc/docs/plans/2026-10-18-lua-m1-options.md`): pages of the Option screens. The
page model (`pc/src/ui/option_pages.h`) holds them; the modern System tab
(`pc_ui_settings_*`, `ui_shell.cpp`) lists every page in tree order, and the
game's own Option screen shows the top-level pages as entries below its nine
rows (`pc/src/ui/classic_options.c`, MENU8's `ROOD_PC` hooks): a page there is
drawn with the game's own windows, cursor, font, flyouts and information box,
and a link opens its sub-page. A page's rows name settings of the settings catalog
(`pc/src/ui/ui_settings.cpp`; `rood.settings.list()` lists the keys), so a new
option is a catalog row plus a line of Lua.

```lua
rood.menu.page{ id = "tweaks", title = "Tweaks", help = "Small things.",
  after = "rood.options/gameplay",          -- or before; ids or "group:<name>"; or parent = a page id
  rows = {
    "widescreen",                           -- a catalog key
    { setting = "aspect", label = "Fill", help = "How it fills.", where = "classic" },
    { define = "fast", kind = "toggle", default = false, label = "Fast" }, -- rood.settings.define in place
    { setting = "plugin:other.plugin/speed" }, -- another plugin's: greyed while it is not loaded
    { header = "Advanced" },
    { help = "Restart to apply." },          -- a line of help
    { page = "more" },                       -- a link (a page id; local names are this plugin's)
    { action = "settings.save" },            -- a core action
    { action = "clear", label = "Clear" },   -- this plugin's (rood.menu.action)
  } }                                        --> "me.tweaks/tweaks"
rood.menu.page{ id = "more", title = "More", parent = "tweaks", rows = { "filter" } }
rood.menu.action("clear", function() rood.storage.set("seen", {}) end, { help = "Forget what was seen." })
```

| | |
|---|---|
| `rood.menu.page(t) -> id` | defines (or, again, redefines in place) a page `"<plugin id>/<id>"`. `id`: a slug (1-32 of a-z 0-9 _ -); `title` 1-24 characters the game's font has (`vs_encode`); `help`; `after` / `before` (ids, local names, `group:<name>`) or `parent` must name pages that exist; `group` defaults to `plugins` (after every core page; `system` is the shipped pages'); `rows` at most 64, 16 pages a plugin. A row is a key or a table with exactly one of `setting`, `define`, `header`, `page`, `action` (or `help` alone: a help line), plus `label`, `help`, `where` (`classic`, `modern`, `all`), `preview` (true / false: the row offers a preview; default the setting's own, every Graphics setting and Brightness). A setting must be in the catalog (the plugin's own defined first; another plugin's is allowed); `define` takes `rood.settings.define`'s spec, with `kind` `toggle` / `choice` / `slider` for `type`. Errors raise in the plugin; text the font lacks in labels and help is a warning (shown as `?` in the classic look) |
| `rood.menu.action(name, fn[, {label, help}]) -> id` | `"<plugin id>/<name>"`, run in the plugin's sandbox under its budget when the player picks its row (an error: the plugin's fault) |
| `rood.menu.style{ classic = {...}, confirm = {...} } -> values` | the Option screens' look and feel (`pc/src/ui/option_style.h`; fields `<group>_<name>`): the classic page layout (`row_x`, `row_w`, `value_x`, `uppercase`, `toggle_gap`, `bar_colour`, `bar_width`, `lit`, `other`, `help_width`, `help_seconds`, `help_lines`) and `confirm.seconds`. Only `rood.options` sets it (its `M.style` table; the C defaults are the same values); back to the defaults when it unloads; RPC `options {style}` tries values live. No argument: the values |
| `rood.menu.remove(id) -> bool`, `rood.menu.pages()`, `rood.menu.rows(id[, where])`, `rood.menu.actions()`, `rood.menu.generation()` | own pages only; every listed page (`id, title, help, source, group, parent, depth`); a page's rows a screen shows now (`id, form, kind, label, help, text, enabled, reason, page, where, preview`); the core actions (`plugin_ok`) and plugins' (`owner`); the page model's change counter |

In the classic look a label wider than its column is cut (the game logs it;
RPC `options` lists them as `cut`): keep labels short (about 20 capitals).
A catalog row flagged `confirm` (only the exclusive fullscreen mode: a mode
the display cannot show leaves a black screen; the window mode and the display
apply and save at once) asks "Keep these settings?" in both screens: it
applies at once, settings.cfg keeps the old value until it is kept, and it
reverts by itself after `confirm.seconds` (`pc/src/display_confirm.h`).

**Preview.** A row that offers one (`preview`) says "Hold SQUARE: preview."
after its help (both screens). While it is focused and Square is held the
menu steps aside (`pc/src/ui/option_peek.c`): the classic screen's 2D layer
(its windows, text, information box) and the HUD, or the modern shell, are
hidden, so the live 3D view shows the value; Left / Right still change it
and a small readout at the bottom names the row and its value; releasing
Square brings the menu back as it was. The game stays paused under the menu
(the field does not move) while the 3D view is redrawn every frame (lighting,
occlusion, post; lens blur is allowed during the preview). The classic menu
draws no dimming layer: the field looks darker under it because the game
holds its effects while a menu is open (torch flames and their glow lights),
and that stays so in the preview. See-through walls only fade when Ashley is
hidden, so its help says to stand behind something first. RPC `options`
(classic) and `ui_menu` (modern) report `peek` and each row's `preview`.

Rows show live values and greyed reasons from the catalog; a plugin's own
settings (`plugin:<id>/<name>`, and standalone schema kinds' `<kind>/<field>`)
come through the catalog's provider (`lua_api_settings.cpp`): booleans are
toggles, enums choices, numbers sliders, colours read-only. Other plugins'
settings stay read-only to code: only the player changes them in the menus.

Actions: core ones (`pc/src/ui/option_actions.c`: `settings.save` "Save now",
`plugins.reload` "Reload plugins"); a plugin's page offers only those flagged
`plugin_ok` and its own; the shipped `rood.*` plugins any. The pages are also
registry entries (`menu.page`, owned by the plugin: `registry.changed`);
everything a plugin defined goes when it unloads or reloads, and a screen
showing it keeps its focus by id (or goes back to the list).

### rood.options (shipped, on)

`pc/plugins/rood.options/main.lua`: the core pages as a table (`M.pages`):
Gameplay, Display, Graphics, Audio, Interface, Controls (group `system`) and
Bindings under Controls (the classic screen reaches it by a link; the modern
list shows it as an entry). The game's own options carry `where = "modern"`
in the catalog: the classic Option screen has its own rows for them. With it
off, broken or without Lua, `option_pages.cpp` builds the same pages from each
catalog row's home page; `lua_menu_probe` checks they are equal row for row
and its Lua tests check every catalog row is on exactly one page.

## The REPL

An nREPL-style bridge over RPC `lua {action "repl"}` (`pc/src/lua/lua_repl.cpp`;
plan `2026-10-18-freedom-and-fast-iteration.md` F6), used mostly by agents.
Dev mode only (`ROOD_RPC` set or `ROOD_DEV=1`). It rides the existing RPC
socket rather than a socket of its own: the RPC already runs on the game
thread between frames (where Lua must run), has the auth (a Unix socket, or a
token over TCP), the pause and stepping an agent drives tests with, and one
client library; a second listener would duplicate all of that for nothing a
request/reply eval needs.

A session evaluates in one sandbox, chosen when it starts: a plugin's (its
globals; its module as `M`) or a console's (`@console`, the overlay console's
own, the default; `@<name>` makes one of that name). Sessions are named by
the client (letters, digits, `_ - .`, up to 32; made on first use) or by the
host (`s1`, ... in the reply); 64 at most, the least recently used dropped.
Its bindings live in its own environment over the sandbox's globals:

- a new name (`x = 1`) stays in the session (another session or the plugin
  does not see it); a name the sandbox already has is the sandbox's, so
  `function bump() ... end` redefines a plugin's global function live
  (`_G.name = v` always writes the sandbox's); a top-level `local` lasts one
  form;
- `_` is the last form's first value, `_e` its last error;
- a reload of the plugin (its files saved, `repl.reload()`, RPC `lua reload`)
  gives it a new state: the session's bindings and anything it redefined are
  gone, the file's code runs again, and the next reply says so (`reset`).

A form is evaluated as the console does (as an expression if it parses as
one, else as statements) under the eval budget (`plugin_load_budget_*`;
errors are logged, never counted towards disabling a plugin). A draw
function or handler a session installs runs as the plugin's own: its budget
and fault rules are the plugin's (a draw that raises stops the claim until
the plugin reloads).

| `op` | Params | Reply (besides `session`, `plugin`, `generation`, `reset`) |
|---|---|---|
| `eval` | `code`, `session`, `plugin` | `ok`, `values` (each result printed as Lua: strings quoted, tables to 6 levels and 200 entries, `function<source:line>`), `results` (JSON; `null` when not plain data), `output` (what the form and anything it called logged: `print`, warnings; others' lines prefixed with their id), `ms`; failed: `error`, `traceback`; `incomplete` when the code ends early (an open `function`, `{`: send more lines with it) |
| `describe` | `code` (an expression) | `result`: `type`, `repr`, a table's `keys` (key, type, value, `where`), `count`, `length`, `metatable`; a function's `where` and `function` (`source`, `line`, `last_line`, `nparams`, `vararg`, `upvalues`: names) |
| `inspect` | `what`, `arg` | `result` of `repl.<what>(arg)`: `plugins`, `claims`, `exports`, `queries`, `globals`, `upvalues` (`arg`: an expression), `complete` (`arg`: the text) |
| `sessions` | | `sessions`: `session`, `plugin`, `generation`, `current`, `evals`, `idle_s` |
| `close` | `session` | the sessions left |

In a session, `repl` (kept in its environment's metatable: `repl = 1`
shadows it, the RPC still finds it) holds what plugins never get:

| | |
|---|---|
| `repl.describe(v)`, `repl.repr(v)`, `repl.where(f)` | as `describe`; printed; `"source:line"` |
| `repl.plugins()` | every plugin and console: `id, state, status, generation, shipped, console, enabled, claims` (kinds), `exports` (`name, type, where` of its module) |
| `repl.claims()`, `repl.claim(kind)` | every live claim: `kind, owner, active, failed, honoured, where`; this plugin's draw function for kind |
| `repl.exports()`, `repl.queries()`, `repl.globals()` | the module's fields; each `rood.game` query and its value now; the sandbox's and the session's names |
| `repl.upvalues(f)`, `repl.getupvalue(f, name)`, `repl.setupvalue(f, name, v)` | a function's upvalues: a main chunk's locals are reachable through the functions that use them (`setupvalue` changes the local every closure shares) |
| `repl.reload([id])` | the plugin (default this one) reloads after the form |
| `repl.complete(text)` | completions of the identifier chain ending text |

Clients: `pc/tools/rood_repl.py` (interactive, with history, Tab completion
and `>>` continuation lines; `-e CODE` one-shot, repeatable, `-` for stdin;
`-s NAME` a session kept between calls; `-p ID` the sandbox; `--json` one
reply per line; `--describe`, `--inspect`, `--sessions`, `--close`; exit
status 1 when a form failed), `rood_rpc.py repl eval|describe|inspect|sessions|close`,
and `Game.repl(op, session, plugin, ...)` in tests. An agent's loop:

```sh
export ROOD_RPC=...   # the game's socket
rood_repl.py --json -p rood.hud -s hud --inspect claims
rood_repl.py --json -s hud -e 'rood.dev.show("status", { timing = "slow" })'
rood_repl.py --json -s hud -e 'M.draw.status = function(e, d)
  d.text(M.style.word, e.text, e.x, e.y - 4, 0xFF8040) end'     # drawn from the next frame
rood_repl.py --json -s hud -e 'M.look.words.nudge = 1.5'          # a field of the look, live
rood_repl.py --json -s hud -e 'repl.reload()'                     # or save main.lua: the file wins
```

## Developer tools

- The console: F1, Window > Lua console. Evaluate in the console's own
  sandbox or a plugin's (Up/Down history); the plugins with on/off and
  reload; the budgets and the master switch; in dev mode the Bench: summon
  each HUD kind, clear, the classic override, and the gallery (save this
  moment, the entries with their room and held kinds, Load).
- RPC `lua` (`pc/src/lua/lua_rpc.cpp`): `eval`, `console`, `repl` (above), `plugins`,
  `reload`, `rescan`, `enable`, `grant`, `budget`, `test`, `test_status`,
  `log`, `audit`, `emit`, `registry`.
- `pc/tools/rood_rpc.py lua 'return rood.version'` and
  `rood_rpc.py lua-test --plugin my.plugin` against a running game;
  `Game.lua`, `Game.lua_test`, `Game.lua_plugins` in tests.

## Testing

- `ctest lua_probe` (seconds, no game): allocator cap, budgets, sandbox,
  values between plugins, events, timers, registry, manifests, storage,
  capabilities, hot reload, the test harness, the RPC, the HUD layer, the
  REPL (dev mode only; sessions, printed values, output, errors, incomplete
  input, the budget; a plugin's sandbox, its module and upvalues;
  inspection; a draw redefined live, then reloaded from its file).
- Lua tests: `tests/*.lua` in a plugin, run inside a running game with no
  relaunch: `rood_rpc.py lua-test`, or `python3 pc/tests/lua_smoke.py
  --tests-only --plugin my.plugin` (one launch).
- `ctest lua_menu_probe` (Linux; the real settings catalog and page model):
  rood.options' pages equal the C++ fallback's row for row and the fallback
  comes back identical when it is off or broken; every validation error; a
  plugin's page placed after the core ones, its rows per screen, links,
  parents; actions (refused core ones, its own greyed until registered, run
  in its sandbox, a raising one its own fault); hot reload and turning off.
- `pc/tests/run.py lua_smoke`: one game, every Lua test (rood.options': its
  pages against the catalog; the fixture test.menu: a page with settings
  defined in place shown as toggle / slider / choice rows, its actions), then runaway and
  failing scripts, memory, capabilities, hot reload and settings.
- `pc/tests/run.py player_state_smoke`: one game from the test card: the
  player state queries against the bag's records and the Items screen's
  lists, Lua's tables once per vsync and read-only, the state hash unchanged
  by queries and previews, `workshop()` and `craftable()` at a workshop, the
  combine preview against the original Combine screen's stored record
  (blades, armor), and `rood.craftbook`'s panel and Lua tests.
- `pc/tests/run.py hud_bench_smoke`: one game in the save room:
  `rood.game` against RPC state; the name window (the game's own call,
  held, its event, closed; a damage number's event); SLOW! summoned and
  held, claimed (suppressed, drawn), hot-reloaded while held (the loop's
  seconds printed), and the game's own drawing with no claim, a draw error,
  the plugin off, the classic look; the classic flavour (packets at the
  hook, every classic primitive, the game's font measured, the primitives
  refused in the modern flavour, a classic draw error); then rood.hud on
  every kind (claimed, suppressed, drawn; all five held at once: under 0.25
  ms of Lua a presented frame, p99 under 1 ms, no allocations, memory flat;
  the classic flavour under 0.1 ms a game frame; its Lua tests; an edit of
  its look hot-reloaded; off: the game's drawing; typography never saved);
  last the gallery (save, list, the moment changed, load: the restarted
  process holds the word again; a stale entry refused).
- `ctest classic_parity_probe`: the parity gate's walker and comparison on
  synthetic packets (the round trip, each field named, the scan, the
  emit-only suppression, the dump).
- `pc/tests/run.py classic_parity` (null renderer): the gate (above).
- `pc/tests/run.py classic_hud_smoke`: rood.classic on a renderer: the
  word and the gauges drawn by default, its typeset handed to ui_text, its
  Lua tests in the field, the two-word phrases one space apart and on
  screen at 16:9 (sizes 1 and 1.6), the same HUD picture as the game's, the
  digits' descender lift edited in its look (the setting untouched), hot
  reload timed (paused and running), a live REPL edit, an undeclared
  `place.dx` reported by the gate.
- `pc/tests/run.py lua_imgui_smoke`: one game at the title, the overlay
  shown, the fixture test.imgui: no rood.imgui without dev.tools; its two
  windows in Window (RPC `lua windows`), the open one drawn every frame, one
  opened by RPC and saved; widget values round-tripping through Lua;
  rood.imgui outside a draw refused; `rood.dev.rpc` (a result, an error, a
  refusal); keys with the window focused; a draw raising and one leaving
  scopes open: that window failed with its message, the other and the
  overlay going on, no Dear ImGui assertion; Retry; hot reload with the
  window open; the shipped rood.dev_example on.
- `pc/tests/run.py repl_smoke`: one game in the save room, driven as an agent
  does with `rood_repl.py -e` processes: a session's bindings across them,
  JSON replies, an error's exit status and traceback, multi-line input from a
  pipe; rood.hud's sandbox and module; claims, exports, queries, plugins,
  describe; SLOW! held, `M.draw.status` redefined (drawn one vsync later),
  a claim from the REPL, then rood.hud's file saved: its drawing again and
  the session started over; no faults counted; `rood_rpc.py repl`.
