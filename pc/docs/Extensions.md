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
mode, opened by its own button (F12), doing nothing until then.

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
| `plugin_budget_ms` | `budget_ms` | 4 | one handler, timer or call between plugins |
| `plugin_budget_instructions` | `budget_instructions` | 2000000 | |
| `plugin_load_budget_ms` | `load_budget_ms` | 250 | a main chunk, an eval, a test step |
| `plugin_load_budget_instructions` | `load_budget_instructions` | 50000000 | |
| `plugin_memory_mb` | `memory_mb` | 32 | each plugin's allocator refuses past it |
| `plugin_fault_limit`, `plugin_fault_window` | `fault_limit`, `fault_window_s` | 5, 10 | that many faults in that many seconds disable a plugin |
| `plugin_watch_vsyncs` | `watch_vsyncs` | 30 | hot reload checks |

Once a call is over budget, every following instruction fails until the
call returns, so `pcall` cannot keep a runaway alive. The C loops a
plugin can make long check the clock too (string patterns, plain `find`,
`table.insert/remove/move/sort`: `pc/third_party/lua/ROOD-NOTES.md`), and
every `rood` function checks it on entry, so a loop of costly calls stops
at the first one past the budget. Errors are caught with a traceback (the
log, the console, stderr `[lua] ...`). Disabled plugins come back when
their files change or with a reload. Known limits: one C call without Lua
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
| `rood.on(event, fn) -> handle`, `rood.off(handle)`, `rood.emit(event, payload) -> count` | `fn(payload, sender)`: sender is the emitting plugin's id (nil for the core's events), since a plugin may emit in another's namespace (a request); plugins emit namespaced events (`my.plugin/changed`); core events: `tick {vsync, game_time}`, `frame {dt, real_time}`, `plugin.loaded/unloaded {id}`, `registry.changed {kind, id, action}`, `unload` (to the plugin itself), `input.action {name, device}` (a port action's button pressed, whatever captures the input: `photo`, bind_key_photo F12 / bind_pad_photo misc1; device `keyboard` or `pad`), `photo.released {what, reason}` (to its owner: below) |
| `rood.timer.after/every(seconds, fn[, {clock = "real"}]) -> id`, `rood.timer.cancel(id)` | game time by default (stops while paused) |
| `rood.clock.game()`, `rood.clock.real()` | seconds |
| `rood.registry.register(kind, id, data[, {after, before, group}])`, `get`, `list(kind[, {group}])`, `remove`, `kinds()` | ids in the plugin's namespace (`<plugin id>/<name>`); `after`/`before`: ids or `group:<name>` |
| `rood.storage.get/set/remove/keys/flush`, `read(path)`, `read_data/write_data/list_data` | plain data, 1 MB; `read` reads the plugin's folder |
| `rood.settings.get/set/reset/run/list([{page}])/pages()/define(name, spec)/on_change(id, fn)/off_change` | System rows (`"skip_intro"`), schema kinds (`"<kind>/<field>"`), the plugin's own (`"plugin:<id>/<name>"`; another plugin's are read and watched, never changed), each validated by its registry; settings.cfg keys without a row are read and watched, never set: the plain numeric ones (`"qol_fast_travel_prompt_size"`; several numbers, a colour: a table) and ui_text's (`"ui_text_status_size"`, a font's path as text) |
| `rood.test.case(name, fn)`, `eq`, `ok`, `near`, `raises`, `skip`, `wait(ticks)` | in `tests/*.lua` |
| `rood.menu.page{...}`, `action`, `remove`, `pages`, `rows`, `actions`, `generation` | the Option screens' pages (below) |
| `rood.game.pause/resume/paused`, `rood.camera`, `rood.post`, `rood.ui`, `rood.capture`, `rood.input` | the photo mode foundation (below), each with its grant |

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
| `d.sprite(x, y, {page_x, page_y, bits, u, v, w, h, clut_x, clut_y}[, color[, alpha]])` | classic only: a VRAM rectangle as the game draws its sprites (page_x a multiple of 64, page_y 0 or 256, bits 4, 8 or 15; color modulates it, white: as it is) |
| `d.window(x, y, w, h[, brightness])` | classic only: the game's text box window (its stone body, frame and shadow); brightness 1 a selected box, 0.5 the others |
| `d.cursor(x, y[, step])` | classic only: the menus' pointing hand, its brightness animation at `step` (0..15; default: the game's) |
| `d.bar(x, y, value, max[, pool])` | classic only: the status display's bar (`hp`, `mp`, `risk`) filled value / max |
| `d.text_raw(s, x, y, word[, typeset])` | classic only: the game's raw printer (`vs_battle_renderTextRawColor`: the status words, RISK ZERO) called as the game calls it: `s` as given (`$` centres it on `x`, `#` right-aligns, the game's narrow blanks), `word` the PS1 colour word (`0xBBGGRR`, `0x80` full); `typeset` how ui_text sets it: `{style = "status", blanks = "one" \| "each", keep_on_screen, screen_w, margin, word_space}` (fields left out: ui_text's defaults, its settings) |
| `d.layer(offset \| nil)` | classic only: the ordering-table slot the commands that follow link into, as the game names its slots: bytes from `vs_scratch.unk4` (-64..64, a multiple of 4: the status words' -16, the gauges' bars and labels -12, their digits -16); nil the kind's own. A slot draws its packets in the order of the commands (the game's: the reverse of its calls) |
| `d.value(font, x, y, value) -> x, y`, `d.digit(font, x, y, digit)` | classic only: the HUD's digits (`vs_battle_renderValue`: the rightmost at `x, y`; font 0 small, 1 large; returns the pen the game's call leaves) and one digit (`_renderDigit`; font 2 the separator) |
| `d.xy(x, y, delta) -> x, y` | the game's packed position arithmetic: `x, y` packed as the game packs them plus `delta` written as the game writes its steps (`56 \| (-8 << 16)`), so x's carry or borrow reaches y as in the game |
| `d.anchor(where)` | where the draw's commands sit in 16:9: `"left"`, `"right"`, `"centre"`, `"element"` (its own, the default), `"screen"` (a panel's: the picture's square pixels, 240 high; not in the classic flavour); in the classic flavour the range is placed exactly (`PC_ANCHOR_EXACT`: no edge extension, cover or side-paint guesses) |
| `rood.draw.panel(fn \| nil)` | the plugin's own layer over the picture (one a plugin; nil removes it): `fn(d, view)` every presented frame, in the modern pass whatever the look, after the HUD and the menus, whatever `rood.ui.hide` hides; `view`: `width` (240 times the picture's aspect), `height` 240, `aspect`, `look` (`classic`, `modern`), `dt`. Coordinates are the picture's square pixels from its top-left (`d.anchor("screen")`, its default); the classic-only primitives raise. Never in a photo (captured before the overlay). A draw that raises stops it until the plugin sets it again or reloads. While one is set its commands replace the HUD's in RPC `hud {stats}`'s last frame |
| `rood.dev.show(kind, {...})` | dev mode only (`ROOD_RPC` set or `ROOD_DEV=1`): in the field the game's own spawner (status: `timing`, `variant`; chain: `count`; buff: `slot`, `stat`, `value`, `sign`; damage on Ashley: `value`, `heal` or `sign`, `pool`, `stat`; travel: the prompt without a Circle of Magic; name_window: `index` (184), `message` (11), the game's own `vs_battle_displaySceneMessage`), held at `age` (10) until `rood.dev.clear([kind])` (`hold = false`: it plays out; a name window closes after 120 vsyncs); elsewhere a synthetic element (the name window's `text`) only modern claims draw. `rood.dev.list()` (with `drawn`, `suppressed`) |
| `rood.dev.bench.save(name)`, `list()`, `load(name)`, `current()` | the gallery (below) |

Honoured: a claim that has not failed, and the modern look (`ui_menus 1`);
in the classic look a claim made for the classic flavour when the kind's
`hud_classic_<kind>` is on (status and chain by default: the kinds whose
`rood.classic` draw passes the parity gate; buff and damage off), any
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
`d.window`, `d.cursor`, `d.bar` exist (in the modern flavour they raise).
So the classic look gains only what a plugin draws with the game's own
pieces; the Travel prompt has no classic flavour (its classic look is the
game's), and synthetic elements are drawn only in the modern look. A draw that raises counts a fault and the game draws the
element again until the plugin reloads. The command buffer is fixed (1024
commands, 16 KB of text: past it `d.text` returns false). The host's own
work inside a call (a font loaded and its glyphs baked on first use) does not
count against the draw's budget. A reload drops the plugin's claims, styles
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
| `rood.game.paused()` | the photo mode's pause holds the game (or was asked for); `pause()` / `resume()` with the `game.pause` grant (below) |

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
- `look.typeset`: how ui_text sets the words (`blanks = "one"`: a run of
  the game's narrow blanks is one space, `keep_on_screen`, `screen_w`,
  `margin`, `word_space`; the playtest fix `645a7477` as Lua values, the
  settings `ui_text_edge_margin` and `ui_text_word_space` read into it);
- `look.typography`: per ui_text style, its fields and tunables for this
  run (`rood.hud.typography`): `digits = {descender_lift}` is playtest fix
  `1d2f20c5` (the old-style figures risen into the room above them before
  any is squeezed), read from the setting `ui_text_descender_lift`;
- `look.overrides`: per kind, each deliberate difference from the game's
  packets (`typography` for all three; a non-zero `dx` would be `place`). The parity
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
`hud_classic_chain`, `hud_classic_gauges` (on), `hud_classic_buff`,
`hud_classic_damage` (off: not through the gate yet); the Lua console's
Bench toggles them.

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
ui_text restyles at raster time, after the gate.

| RPC | |
|---|---|
| `packets {kind}` | a site's last capture (`status`, `chain`, `buff`, `damage`, `status_panel`), normalised: each packet's words, kind, vertices, texels, CLUT, page, colour, the glyph it shows; in the parity mode the Lua draw's beside it. Sites capture from the first call |
| `packets {start, end}` | the packets of a guest range in allocation order |
| `classic {parity, reset}` | per kind: frames compared, matched, mismatched (by field), skipped (why), the first difference with both packets |

`pc/tests/classic_parity.py` (run.py `classic_parity`, the null renderer;
also in verify.sh's CPU lane) plays every element of each kind with
`rood.dev.show`'s spawners: each timing and success word, chains 1, 2, 3,
9, 10, 99, first held, then its whole life (slide in, held, slide out:
every age compared, the slides required); the gauges through HP / MP of
one to four digits, empty and full, RISK ZERO and RISK's bar, the display
sliding out and back (Ashley's record and `D_800EB9AF` written). It
requires no unwaived mismatch and waivers = declared overrides; checks the
default (drawn by rood.classic), the fallback (setting off, a raising
draw, the plugin off) and state parity (from one mark, rood.classic
drawing then the game: every vsync's state the same but the packets and
the host clock: Oracle page digests). Reports `<out>/<kind>.json`.
Result 2026-10-20: status 576 frames, chain 558, gauges 1192, 0
mismatches; state parity 120 of 120 vsyncs.

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
| `game.pause` | `rood.game.pause() -> true \| nil, why`, `resume()`; `rood.game.paused()` (any plugin) needs no grant |
| `camera` | `rood.camera.begin()`, `finish()`, `active()`, `get()`, `set{...}`, `orbit(yaw, pitch[, zoom])`, `move(right, up, forward)` |
| `post` | `rood.post.list()`, `get(key)`, `set(key, value)`, `reset([key])`, `overrides()` |
| `ui.hide` | `rood.ui.hide{hud, game2d, overlay}` (no table: all three), `show()`, `hidden()` |
| `capture` | `rood.capture.photo{scale, path, done} -> path, id[, note]`, `status(id)`, `folder` |
| `input.capture` | `rood.input.capture(fn)`, `release()`, `captured()`, `state()` |

### The pause and the frozen moment

`rood.game.pause()` holds the game at the next vsync boundary (at once when
asked between frames, as from the REPL or the RPC): no vsync advances (RPC
`step`s asked meanwhile are dropped, so a test's `run` returns at the same
vsync), no game-time Lua (ticks, game-clock timers), no audio fed (the
queued tail plays out, then silence). The window, the overlay, Lua's frames
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
Resuming continues from M exactly (the hardware target is rebuilt from M's
VRAM, as after a rewind).

### rood.camera

The editor's live free camera (`editor_view.cpp`), borrowed: its eye, yaw
and pitch for the plugin's time, the editor's own camera given back after,
the editor not turned on. World units (y down), degrees.

| | |
|---|---|
| `begin() -> true \| nil, why` | needs the game paused (or asked to be) and a room; starts at the game's camera (the same picture), the orbit's target on Ashley's middle (else 1500 units ahead) |
| `finish()` | the game's camera again: its own state was never touched (the view is built from the free camera inside `func_8007ACB0`'s hook only during renders, which are rewound), so the next render is the held picture exactly |
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
...) and two of the photo mode's own: `preset` (a named post profile from
`pc/assets/post` or `pc/user/post` under the plugin's values: a filter;
`""` none) and `format` (the picture cropped to an aspect ratio, width over
height, 0 the whole picture: black bars over the live view, the photo
cropped to it).

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
timing words, chains, buffs, damage numbers), every `PC_CLASSIC` / `PC_HUD`
site's packets made no-ops after the game ran them (claims' classic packets
too), and the modern pass (Lua claims, the Travel prompt, the save state
notice). `game2d`: the rest of the game's 2D in the field: the packets
linked at the ordering table's first `game2d_slots` entries (35: the text,
window and HUD layer, `vs_scratch.unk4` +- 16 entries; RPC `photo
{game2d_slots}`): text boxes, the name window, the minimap, targeting
marks; native lists' markers and mode packets are kept. `overlay`: the
developer overlay (its windows and the free camera's grid) as if closed.
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
`render_frames`, `game2d_slots` tune it. An engine reload
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

### Testing

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
above, `rood.draw.panel`, `rood.settings` and `rood.menu`. It does nothing
until its button is pressed: the binding slot `photo` (`bind_key_photo`
F12, `bind_pad_photo` `misc1`: the Xbox Share, Switch Capture or PS5
microphone button; System > Bindings: Photo mode), which the host sends as
the event `input.action {name = "photo"}` whether or not the input is
captured, so the same button enters and leaves.

Entering is allowed in the field and in battle (`rood.game.mode()` `field`,
`battle`); elsewhere a short notice says why ("Photo mode: not in a menu",
"... not during a scene", "... not while loading", the title, the demo), and
the foundation's own refusals (a room transition, memory card I/O) are shown
the same way. Then: the game held (`rood.game.pause`), the free camera begun
at the game's (its pivot on Ashley), the HUD, the game's 2D and the
developer overlay hidden, the input captured, and the card drawn as a panel.
Leaving gives back the input, the layers, the post overrides (the format
too), the camera and the pause, in that order; the plugin unloading or
reloading does the same by itself.

| Pad (keyboard) | |
|---|---|
| D-pad up / down (arrows) | the row |
| D-pad left / right (arrows) | the row's value (held: repeats) |
| Left stick (I J K L) | move the camera (the pivot along), within `radius` of where it began |
| Right stick (F H T G) | turn about the pivot |
| L1 / R1 (Q W; U O) | lower / raise |
| L2 / R2 (1 2; C V) | zoom (the distance to the pivot) |
| Circle (Z) | take a photo (`rood.capture.photo`, the "Photo size" setting's scale): "Saved <name>" |
| Cross (X), Start (Enter), Escape, the photo button | leave |
| Triangle (S) | hide / show the card |
| Square (A) | reset the row (its override dropped) |
| Select (Tab) | the camera back where it began |

The rows (`M.rows`; each a post override of the plugin's, live, never saved):

| Row | Values | Post keys |
|---|---|---|
| Format | Fullscreen, 4:3, 3:2, 16:9, Cinema 2.39:1, Square, Portrait 4:5, Vertical 9:16 | `format` |
| Filter | Off, Noir, Sepia, Faded, Vivid, Warm, Cold (grades: saturation, contrast, white balance, gains, offsets), then every post preset (Film, Cinematic, Clean, and the user's) | the grade's keys, `preset` |
| Vignette, Grain | 0..1 by 0.05 | `vignette`, `grain` |
| Aberration | 0..2 by 0.1 | `chromatic_aberration` |
| Exposure | -3..+3 EV by 0.25 | `exposure` |
| Aperture | Off, f/1.4 .. f/22 | `dof` off, or on with `dof_fstop` |
| Focus | Subject (the DoF camera subject: Ashley in the field, the acting actor in battle), Centre, Cursor (the mouse), Ashley | `dof_focus` (greyed with Aperture off) |

Sliders start at the live value (so entering changes nothing), untouched
rows follow a filter's preset. No Pose row: see above.

Look and feel are tables in `data.lua` (hot-reloaded; the REPL's `M`):
`M.look` (the card's place and size, the modern and classic colours and
typography, the notices, the thirds grid, the hints), `M.camera` (speeds,
dead zone, radius, collision, the keys, the slider repeat), `M.formats`,
`M.filters`, `M.preset_labels`, `M.apertures`, `M.focus`, `M.rows`. The card
is drawn over the picture (never in the photo): with the modern look in the
labels font with the button prompt icons; with the classic look in the
game's window colours and its menu font (ui_text's typography) and the
buttons named in words, since the game's own button sprites cannot be drawn
over the picture.

The player's settings (the Option screen's Photo mode page, saved in the
plugin's own `settings.json`, never `settings.cfg`): Photo size (1x; 2x-4x
are not supported yet: saved at the window's size, the notice says so),
Camera speed, Invert camera Y, Thirds grid; the page also shows the
binding.

Tests: its Lua tests (`tests/photo_test.lua`, in `lua_smoke`: where it
opens, the rows' keys and ranges, the filters' keys, the page) and
`photo_mode_smoke` (`--renderer gl` and `vulkan`): in the save room, the
button refused in the field menu; F12 enters; every row by the keyboard
read back through `rood.post`; the camera keys; a photo; Cross leaves with
the state digest, the vsync, the post values and the picture as without the
session; the pad's button enters, F12 leaves, Start leaves; settings.cfg
untouched.

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
| `rood.menu.page(t) -> id` | defines (or, again, redefines in place) a page `"<plugin id>/<id>"`. `id`: a slug (1-32 of a-z 0-9 _ -); `title` 1-24 characters the game's font has (`vs_encode`); `help`; `after` / `before` (ids, local names, `group:<name>`) or `parent` must name pages that exist; `group` defaults to `plugins` (after every core page; `system` is the shipped pages'); `rows` at most 64, 16 pages a plugin. A row is a key or a table with exactly one of `setting`, `define`, `header`, `page`, `action` (or `help` alone: a help line), plus `label`, `help`, `where` (`classic`, `modern`, `all`). A setting must be in the catalog (the plugin's own defined first; another plugin's is allowed); `define` takes `rood.settings.define`'s spec, with `kind` `toggle` / `choice` / `slider` for `type`. Errors raise in the plugin; text the font lacks in labels and help is a warning (shown as `?` in the classic look) |
| `rood.menu.action(name, fn[, {label, help}]) -> id` | `"<plugin id>/<name>"`, run in the plugin's sandbox under its budget when the player picks its row (an error: the plugin's fault) |
| `rood.menu.style{ classic = {...}, confirm = {...} } -> values` | the Option screens' look and feel (`pc/src/ui/option_style.h`; fields `<group>_<name>`): the classic page layout (`row_x`, `row_w`, `value_x`, `uppercase`, `toggle_gap`, `bar_colour`, `bar_width`, `lit`, `other`, `help_width`, `help_seconds`, `help_lines`) and `confirm.seconds`. Only `rood.options` sets it (its `M.style` table; the C defaults are the same values); back to the defaults when it unloads; RPC `options {style}` tries values live. No argument: the values |
| `rood.menu.remove(id) -> bool`, `rood.menu.pages()`, `rood.menu.rows(id[, where])`, `rood.menu.actions()`, `rood.menu.generation()` | own pages only; every listed page (`id, title, help, source, group, parent, depth`); a page's rows a screen shows now (`id, form, kind, label, help, text, enabled, reason, page, where`); the core actions (`plugin_ok`) and plugins' (`owner`); the page model's change counter |

In the classic look a label wider than its column is cut (the game logs it;
RPC `options` lists them as `cut`): keep labels short (about 20 capitals).
A catalog row flagged `confirm` (only the exclusive fullscreen mode: a mode
the display cannot show leaves a black screen; the window mode and the display
apply and save at once) asks "Keep these settings?" in both screens: it
applies at once, settings.cfg keeps the old value until it is kept, and it
reverts by itself after `confirm.seconds` (`pc/src/display_confirm.h`).

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
- `pc/tests/run.py repl_smoke`: one game in the save room, driven as an agent
  does with `rood_repl.py -e` processes: a session's bindings across them,
  JSON replies, an error's exit status and traceback, multi-line input from a
  pipe; rood.hud's sandbox and module; claims, exports, queries, plugins,
  describe; SLOW! held, `M.draw.status` redefined (drawn one vsync later),
  a claim from the REPL, then rood.hud's file saved: its drawing again and
  the session started over; no faults counted; `rood_rpc.py repl`.
