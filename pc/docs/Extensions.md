# Extensions: plugins and Lua

rood-boi runs plugins written in Lua 5.4 (vendored in `pc/third_party/lua`).
This is Milestone 0: the runtime, its safety nets and the core API. Menus,
drawing, input and game queries come in later milestones. Vanilla stays
vanilla: no plugin runs unless it is enabled, and the shipped example is off.

## Where Lua runs

Single-threaded, on the game thread (`rl::run` refuses any other):

| Entry | From | When |
|---|---|---|
| `pc_lua_init` | `main()` (`boot_runtime.c`), after `settings.cfg` loads | once |
| `pc_lua_tick` | `pc_tick()`, after the RPC | every game vsync while the game runs (not while the RPC has it paused) |
| `pc_lua_frame` | `pc_overlay_frame()`, the ImGui frame open | every presented image (interpolated in-betweens, paused redraws) |
| `pc_lua_rpc` | RPC method `lua` (`dev_rpc.c`) | between frames |
| `pc_lua_shutdown` | `atexit` | writes stores and plugin settings; runs no Lua |

Never on the audio callback thread or helper threads. Save states
(`snapshot.c`) do not include Lua: plugins keep their state across a resume.

## Plugins

A plugin is a folder named after its id, with a manifest and a main chunk:

```
pc/plugins/rood.example/      shipped (committed); ROOD_PLUGINS_SHIPPED overrides, "" for none
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
| `rood.on(event, fn) -> handle`, `rood.off(handle)`, `rood.emit(event, payload) -> count` | `fn(payload, sender)`: sender is the emitting plugin's id (nil for the core's events), since a plugin may emit in another's namespace (a request); plugins emit namespaced events (`my.plugin/changed`); core events: `tick {vsync, game_time}`, `frame {dt, real_time}`, `plugin.loaded/unloaded {id}`, `registry.changed {kind, id, action}`, `unload` (to the plugin itself) |
| `rood.timer.after/every(seconds, fn[, {clock = "real"}]) -> id`, `rood.timer.cancel(id)` | game time by default (stops while paused) |
| `rood.clock.game()`, `rood.clock.real()` | seconds |
| `rood.registry.register(kind, id, data[, {after, before, group}])`, `get`, `list(kind[, {group}])`, `remove`, `kinds()` | ids in the plugin's namespace (`<plugin id>/<name>`); `after`/`before`: ids or `group:<name>` |
| `rood.storage.get/set/remove/keys/flush`, `read(path)`, `read_data/write_data/list_data` | plain data, 1 MB; `read` reads the plugin's folder |
| `rood.settings.get/set/reset/run/list([{page}])/pages()/define(name, spec)/on_change(id, fn)/off_change` | System rows (`"skip_intro"`), schema kinds (`"<kind>/<field>"`), the plugin's own (`"plugin:<id>/<name>"`; another plugin's are read and watched, never changed), each validated by its registry |
| `rood.test.case(name, fn)`, `eq`, `ok`, `near`, `raises`, `skip`, `wait(ticks)` | in `tests/*.lua` |

## HUD: claims and drawing

The visible layer (plan `pc/docs/plans/2026-10-18-lua-visible-layer.md`;
`pc/src/hud`, `pc/src/lua/lua_api_hud.cpp`). The game decides when and what
(its HUD elements, copied each frame into a fixed pool); a plugin's claim
decides how it looks. So far one kind: `status`, the battle's timing word
(TOO FAST!, FAST!, SLOW!, TOO SLOW!, COOL!, ...).

| | |
|---|---|
| `rood.hud.claim(kind, fn) -> owner?` | `fn(e, d)` draws each element of the kind on every presented frame; while the claim is honoured the game's packets for it are no-ops. One owner per kind: the last in load order (a console sandbox after every plugin), with a warning. `e`: `kind, id, text, x, y, w, h` (the game's drawing's box in its 320 x 240 screen, x, y its centre), `rgb`, `timing` (`too_fast`, `fast`, `success`, `slow`, `too_slow`), `variant`, `age`, `life`, `held`, `synthetic`; one table, reused |
| `rood.hud.release(kind)`, `rood.hud.kinds()` | |
| `rood.draw.style{...} -> style` (also `d.style`) | an immutable handle, parsed once (make styles at load): `font` (a ui_text style's font: `dialogue`, `menu`, `info`, `digits`, `status`, `labels`), `size` (em, game pixels), `color` (`0xRRGGBB` or `"#rrggbb"`), `alpha`, `stroke` (game pixels), `stroke_color`, `stroke_alpha`, `tracking` (em), `stretch`, `align` (`left`, `center`, `right`), `anchor` (`top`, `middle`: the capitals' middle, `baseline`, `bottom`); unknown fields and bad values raise; at most 256 per load |
| `d.text(style, s, x, y[, color[, alpha]]) -> queued` | inside a draw function only; game coordinates (widescreen narrows them about the element's anchored edge, as the game's 2D) |
| `d.rect(x, y, w, h, color[, alpha])`, `d.measure(style, s) -> w, h` | |
| `rood.dev.show(kind, {timing, variant, age, hold})` | dev mode only (`ROOD_RPC` set or `ROOD_DEV=1`): in the field the game's own spawner, held at `age` (10) until `rood.dev.clear([kind])`; elsewhere a synthetic element only claims draw. `rood.dev.list()` |

Honoured: a claim that has not failed, and the modern look (`ui_menus 1`)
or `hud_classic_override 1` (default 0: the classic look stays the game's,
typography only). A draw that raises counts a fault and the game draws the
element again until the plugin reloads. The command buffer is fixed (1024
commands, 16 KB of text: past it `d.text` returns false). A reload drops the
plugin's claims and styles; held moments belong to the host and stay, so an
edit shows on the held word about half a second after it is saved (the
hot reload check).

RPC `hud`: `list`, `show {kind, timing, variant, age, hold}`, `clear {kind}`,
`stats {reset}` (per kind: seen, suppressed, fallback, drawn, faults, owner;
the claims' Lua per frame, mean / p99 / max ms; the last frame's commands),
`set {classic_override}`.

Try it (the Lua console, F1, in the field with the modern look):

```lua
rood.dev.show("status", { timing = "slow" })
w = rood.draw.style{ font = "status", size = 14, stroke = 1.5, align = "center", anchor = "middle" }
rood.hud.claim("status", function(e, d) d.text(w, e.text, e.x, e.y, e.rgb) end)
rood.hud.release("status")   -- the game's own word again
rood.dev.clear()
```

## Developer tools

- The console: F1, Window > Lua console. Evaluate in the console's own
  sandbox or a plugin's (Up/Down history); the plugins with on/off and
  reload; the budgets and the master switch.
- RPC `lua` (`pc/src/lua/lua_rpc.cpp`): `eval`, `console`, `plugins`,
  `reload`, `rescan`, `enable`, `grant`, `budget`, `test`, `test_status`,
  `log`, `audit`, `emit`, `registry`.
- `pc/tools/rood_rpc.py lua 'return rood.version'` and
  `rood_rpc.py lua-test --plugin my.plugin` against a running game;
  `Game.lua`, `Game.lua_test`, `Game.lua_plugins` in tests.

## Testing

- `ctest lua_probe` (seconds, no game): allocator cap, budgets, sandbox,
  values between plugins, events, timers, registry, manifests, storage,
  capabilities, hot reload, the test harness, the RPC, the HUD layer.
- Lua tests: `tests/*.lua` in a plugin, run inside a running game with no
  relaunch: `rood_rpc.py lua-test`, or `python3 pc/tests/lua_smoke.py
  --tests-only --plugin my.plugin` (one launch).
- `pc/tests/run.py lua_smoke`: one game, every Lua test, then runaway and
  failing scripts, memory, capabilities, hot reload and settings.
- `pc/tests/run.py hud_bench_smoke`: one game in the save room: SLOW!
  summoned and held, claimed (suppressed, drawn), hot-reloaded while held
  (the loop's seconds printed), and the game's own drawing with no claim,
  a draw error, the plugin off, the classic look.
