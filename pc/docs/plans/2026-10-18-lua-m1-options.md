# Lua M1: vanilla++ options

Status: done. Phases A and B (2026-10-19, branch `m1-options`), C-E
(2026-10-19, branch `m1-options2`). See "Progress" at the end.

User decisions:
1. **Placement:** pages live inside the game's own Option screen. Its rows
   stay; ours are extra entries below them.
2. **Look:** the game's own style (window frames, cursor hand, font; sliders
   as gauge bars with a number). With `ui_text font`, only the typography is
   upgraded.
3. **Applying:** instant, with a "Keep these settings? 10 s" auto-revert for
   risky display changes.
4. **Modern UI:** its System pages are generated from the same page data.

M1 is the mechanism: any future option is a settings key plus a line of Lua,
and plugins can add pages.

## What exists

- **The Option screen** is MENU8, `vs_menu8_exec`
  (`src/MENU/MENU8.PRG/88.c:489`), entered from MAINMENU
  `submenuEntrypoints[7]` (`src/MENU/MAINMENU.PRG/C48.c:349`).
  - `init` (88.c:523) builds 9 rows through `vs_mainmenu_setMenuRows(9, 0x145,
    ...)`.
  - `handleInput` (88.c:543) runs `vs_mainMenu_flyoutMenuRightAndHoistSelection`.
    Each row opens a value flyout: another `setMenuRows` list with flag 4 on
    the current choice.
  - Returns -2 for Triangle (close) and -1 for Cross (back).
- **The list and input** (`vs_mainMenu_renderItemsList`, 58EC.c:916):
  - Up/Down through `vs_main_buttonRepeat`; wrap only on a fresh press once
    the list scrolls; paging.
  - Circle (`PADRright`) confirms, Cross (`PADRdown`) goes back, Triangle
    (`PADRup`) closes.
  - Sounds through `playMenu*Sfx`; cursor memories in
    `D_800F4EE8.cursorMemories`.
- **Drawing blocks:**
  - the cursor hand, `vs_mainMenu_renderCursor` (58EC.c:499);
  - item windows, `vs_battle_setMenuItem` (5BF94.c:2513);
  - text, `vs_battle_renderTextRaw` and `vs_mainMenu_renderRowLabel` (which
    already goes through the `pc_text_glyph` hook, so typography is
    automatic);
  - gauges, `vs_battle_renderStatBar` (5BF94.c:2925);
  - numbers, `vs_mainMenu_renderIntColor`;
  - help, `vs_mainmenu_setInformationMessage`.
- **Patterns to copy:**
  - the `ROOD_PC` hooks in C48.c (`pc_ui_direct_tick`, `pc_ui_data_armed`);
  - the overlay statics table `score_menu9.h`;
  - `game_options.c`, which mirrors MENU8's writes.
- **Settings catalog:** `pc/src/ui/ui_settings.cpp` (Row structs, `build()`,
  `PAGE_IDS`/`pages[]` :208-212). `rood.settings.list/pages` read it
  (lua_api_settings.cpp:612/648). The registry (lua_registry.cpp) already
  handles kinds, namespaced ids, after/before/group and drops entries on
  reload.
- **Text:** only a decoder exists for the game's string encoding
  (`pc_ui_text_decode`, ui_text.h:176).

## Architecture

```
settings catalog (ui_settings.cpp rows by id; schema kinds; plugin:<id>/<name>)
      | metadata (rood.settings.list/pages stay catalog-based)
      v
rood.options (Lua, shipped)  -+     third-party plugins
  rood.menu.page{...}         +-->  registry kind "menu.page" (after/before/group)
C++ fallback pages (same ids)-+           | generation counter
                                          v
                     page model  pc/src/ui/option_pages.{h,cpp}
                     (pages -> rows: setting/action/link/header/help,
                      each with live value/enabled/reason)
               +--------------------------+---------------------------+
   classic renderer (MENU8 hook, game primitives)    modern shell (ui_settings API
   pc/src/ui/classic_options.c + options_nav.c        re-backed by the page model)
```

- **The catalog becomes catalog plus API.** Rows keep a `home` page for
  metadata only. `pc_ui_settings_*` become views over the page model.
  - Replace the fixed `PC_UI_SETTINGS_BINDINGS` uses (ui_shell.cpp:808,
    ui_system_draw.cpp:443, dev_rpc.c:2336) with
    `pc_ui_settings_page_index("rood.options/bindings")`.
  - New row kinds: `PC_UI_SET_HEADER`, `PC_UI_SET_NOTE`, `PC_UI_SET_LINK`.
- **Fallback.** With Lua off, or `rood.options` disabled or broken, C++
  produces identical pages. A probe checks they are equal.
- **Placement in the list.**
  - Pages without `parent` are Option-screen entries: core pages in
    `group:system` (Gameplay, Display, Graphics, Audio, Interface, Controls),
    plugins in `group:plugins` after them.
  - Bindings is a sub-page of Controls.
  - Game-option rows carry `where = "modern"`: the classic page hides them,
    since the vanilla rows above already show them.

## Hooking the Option screen (`ROOD_PC` only; `make commit-check` must match)

- **88.c:491:** add the state `pcPage` to the enum.
- **88.c:509:** `menuStrings[18 + 2*PC_OPT_MAX]`, `rowTypes[9 + PC_OPT_MAX]`.
- **88.c:539:**
  `n = pc_options_entries(menuStrings + 18, rowTypes + 9, PC_OPT_MAX)`, then
  `setMenuRows(9 + n, ...)`.
  - The strings are game-encoded and owned by classic_options.c.
  - Their descriptions are the page help.
  - Past 9 rows the vanilla list pages.
- **88.c:~587 (the default case):** `if (selectedRow >= 9) { *state = pcPage;
  pc_options_open(selectedRow - 9); return 0; }`.
- **`case pcPage`:**
  - `i = pc_options_exec()`;
  - 0 keeps going;
  - -2 means `exitToBattle`;
  - anything else goes back to `init`, which restores the cursor.
- **Cursor memory:** cursorMemories[0x45] may hold a row of 9 or more.
  `setMenuRows` clamps it (58EC.c:819-824); a test covers it.
- **Overlay statics** reset per load; `pc_options_open` resets its own state.

## Classic renderer (`classic_options.c`; navigation in a pure `options_nav.c`)

**Layout.** Up to 9 `vs_battle_setMenuItem` windows (ids 10-18, slide-in like
`_initItemRow`), the label on the left, the value at x about 200:

| Row kind | Value column |
|---|---|
| toggle | ON / OFF, the current one lit |
| choice | the current name with arrows |
| slider | `renderStatBar` plus `renderIntColor` |
| action | the label only |
| link | an arrow |
| header | inverted gradient, not selectable |
| help | a dim line, not selectable |

- Focus help goes to `setInformationMessage`.
- Greyed rows use the unselectable colour, with the reason in the info box.

**Input:**
- **Up/Down** follow `renderItemsList` exactly: 58EC.c:996-1110, ported into
  `options_nav.c` and probe-tested.
- **Left/Right** step the value, with repeat and the change sound.
- **Circle** depends on the row:

  | Row | Circle |
  |---|---|
  | toggle | flips it |
  | choice | the vanilla flyout (flag 4 on the current choice, flag 1 on unavailable ones) |
  | action | runs it |
  | link | pushes the sub-page |
  | binding | `pc_ui_capture_begin` |

- **Cross** goes back; **Triangle** closes (returns -2).
- Invalid input plays `playInvalidSfx`.
- The confirm/Cross swap works for free at the pad layer.

**Text and hot reload:**
- New `pc/src/ui/vs_encode.c` converts UTF-8 to the game's encoding; an
  unencodable character becomes `?` with a warning.
- When the page model's generation changes, the renderer rebuilds and keeps
  focus by row id. If the page is gone, it returns to the list.

## Save safety

- Rows write only through the catalog setters. Saving uses the debounced
  `pc_settings_save` (settings.cfg), flushed when a page closes.
- `vs_main_settings` (the memory card) is touched only by the 9 vanilla rows;
  a test checks its bytes and card saves.
- Risky rows are not saved until kept.

## Lua API (`pc/src/lua/lua_api_menu.cpp`)

```lua
rood.menu.page{ id="tweaks", title="Tweaks", help="...", after="rood.options/gameplay",
  parent=nil, rows={
    "widescreen",
    { setting="plugin:me.tweaks/speed", label="Speed" },
    { define="fast", kind="toggle", default=false, label="Fast" },
    { header="Advanced" }, { help="Restart to apply." },
    { page="me.tweaks/more" },
    { action="settings.reset_page" },
    { action="me.tweaks/clear" },
}}
rood.menu.action("clear", fn)
rood.menu.remove(id); rood.menu.pages()
```

**Validation** raises in the plugin on:
- an id that isn't a slug;
- a title outside 1-24 characters, or one that can't be encoded;
- more than 64 rows or 16 pages per plugin;
- a row without exactly one form;
- an unknown catalog key. A missing plugin's setting shows greyed instead.
- an after/before/parent that doesn't name a page.

Labels wider than the row are truncated, with a warning.

**Permissions:**
- Pages need no capability.
- Actions are either core actions (`pc/src/ui/option_actions.c`, each
  flagged `plugin_ok`) or the plugin's own, run in its sandbox.
- Risky rows always keep the 10 s revert.
- Other plugins' settings stay read-only to code; only the player changes
  them through the menu.

**Hot reload:** the registry drops a plugin's entries, plus a generation
counter (`rl::registry_generation()`, bumped in lua_registry.cpp:81).

## Display back-ends (settings.cfg keys)

- **`fullscreen_mode WxH@Hz|desktop`:**
  - built from `SDL_GetFullscreenDisplayModes`, deduplicated, with refresh
    rate filtered by resolution;
  - `window_mode_apply` (boot_window.c:405-414) uses it;
  - a missing mode falls back to the closest.
- **`display <index> <name>`:**
  - matched by name, then index, then the primary display;
  - windowed: centred on that display;
  - fullscreen: moves first, then sets the mode.
- **`remember_window`, `window_rect x y w h`:**
  - saved on move or resize while windowed;
  - applied at the three create sites (boot_window.c:145-151,
    gl_present.c:1019, vk_present.c:326) through one helper;
  - only if a display contains it.
- **`vsync`:**
  - OpenGL: `SDL_GL_SetSwapInterval` (gl_present.c:1162);
  - Vulkan: FIFO vs MAILBOX/IMMEDIATE (vk_present.c:192-193), with a
    swapchain recreate.
- **Fps cap:** `present_rate` (ui_settings.cpp:350) becomes a choice: 60, 120,
  180, 240, match display. The game logic stays at 59.94.
- **Render quality:** `internal_resolution` (ui_settings.cpp:279) is
  relabelled, 1x-8x plus "Match window". Saved as `internal_resolution auto`,
  resolved in `pc_hw_supersample` (gpu_hw.c:2994-3008).
- **10 s confirm:** a pure `pc/src/display_confirm.c` with an injected clock.
  - Rows flagged `confirm` snapshot their old values, apply, and hold back
    saving. Only fullscreen_mode asks (user, after the 2026-10-20 playtest:
    window_mode and display apply at once).
  - Classic shows a Keep/Revert flyout; modern shows a modal.
  - A timeout or Cross reverts.
  - RPC `options {keep:true}` is there for tests.

## Testing

- **ctest probes:**
  - `options_nav_probe.c`;
  - `option_pages_probe.cpp` (ordering, fallback = rood.options, validation,
    cycles);
  - `display_confirm_probe.c`;
  - `vs_encode_probe.c` (round trip with the decoder).
- **lua_probe:** rood.menu validation, namespacing, ordering, refused actions,
  hot-reload removal.
- **Lua tests:** `pc/plugins/rood.options/tests`: every catalog row appears
  exactly once, ids are stable, values round-trip. Plus a fixture plugin,
  `pc/tests/lua/menu.fixture`.
- **`options_classic_smoke.py`:**
  - Option → Display; change rows and check settings.cfg;
  - the choice flyout; Cross restores the cursor; Triangle closes;
  - the vanilla rows are unchanged, and `vs_main_settings` and card saves are
    byte-equal;
  - hot reload while a page is open.
  - New RPC `options` reports {screen, page, focus, rows}.
- **Golden screenshots** of the Display page, with `ui_text` original and
  font.
- **Revert smoke:** fullscreen with no answer goes back to windowed after
  10 s.
- **Modern parity:** the modern page rows match the `options` RPC.

## Tasks

| Phase | Task | After | Files |
|---|---|---|---|
| A | A1 `vs_encode.c` + probe *(delegate)* | — | pc/src/ui/vs_encode.{c,h}, CMakeLists |
| A | A2 `options_nav.c` + probe, port of 58EC.c:996-1110 *(delegate)* | — | pc/src/ui/options_nav.{c,h} |
| A | A3 `display_confirm.c` + probe | — | pc/src/display_confirm.{c,h} |
| A | A4 page model + C++ fallback pages, row kinds, catalog `home` | — | option_pages.{h,cpp}, ui_settings |
| B | B1 re-back `pc_ui_settings_*` on the page model | A4 | ui_settings, ui_shell, ui_system_draw, dev_rpc |
| B | B2 `rood.menu` API, validation, generation counter, core actions, lua_probe | A4 | lua_api_menu.cpp, lua_registry, option_actions.c |
| B | B3 shipped `pc/plugins/rood.options`, Extensions.md | B2 | pc/plugins/rood.options |
| C | C1 MENU8 hooks, classic renderer and input, flyout, RPC `options`; commit-check | A1, A2, A4 | 88.c, classic_options.c, dev_rpc |
| C | C2 confirm UI, classic and modern | C1, A3 | classic_options.c, ui_shell.cpp |
| D | D1 fullscreen_mode, display, vsync | A3, A4 | boot_window, gl/vk_present, settings |
| D | D2 window_rect / remember_window *(delegate)* | — | boot_window + create sites |
| D | D3 render quality "Match window", fps cap | A4 | gpu_hw.c, settings |
| E | E1 classic smoke + fixture; E2 golden; E3 revert/parity + docs | C, D | tests, docs |

## Progress

**Done (A, B):**
- **A1** `vs_encode.{c,h}` + `vs_encode_probe` (on main since 021b2fe9; the
  HUD layer uses it). Unencodable characters come back as `?` with a count:
  rood.menu raises for titles and warns for labels and help.
- **A2** `options_nav.{c,h}` + probe (on main); reviewed against
  58EC.c:1053-1109. Added: a focus restored on visible row 0 / 8 of a
  scrolled page pages instead of leaving the window.
- **A3** `display_confirm.{c,h}` + probe (from the parked `m1-page-model`).
  Which keys ask is the catalog's data (`confirm` on a row: fullscreen_mode),
  not a list in C; a held key set back to its snapshot is let go.
- **A4** `option_pages.{h,cpp}` + probe, row kinds HEADER / NOTE / LINK,
  catalog `home` and `where` (parked branch). Fixed: a catalog row not shown
  now fills its metadata only (the fallback crashed without the game-options
  bridge); action rows take label, help and availability from
  `option_actions`.
- **B1** `pc_ui_settings_*` are the page model's pages (where modern), in
  tree order: Gameplay, Display, Graphics, Audio, Interface, Controls,
  Bindings (the modern order changed to this one), then plugins' pages. The
  fixed page enum is gone; the catalog is reachable by index
  (`catalog_choice/change/set/activate`, `pc_ui_settings_binding`), which
  `rood.settings` and the RPC use; the shell keeps focus per page id across
  generations, opens links, skips headers and notes. Core page ids stay short
  (`display`) for the RPC, tests and `rood.settings`. The catalog has a
  provider for rows by id: `lua_api_settings.cpp` provides schema fields
  (plugins' own settings).
- **B2** `lua_api_menu.cpp` (`rood.menu.page/action/remove/pages/rows/actions/
  generation`), validation as above, pages also `menu.page` registry entries
  (`registry_put` / `registry_drop`), `option_actions.{h,c}` (core
  `settings.save`, `plugins.reload` with `plugin_ok`; plugins' through a
  provider). Tested by `lua_menu_probe` (a probe of its own: lua_probe has
  no catalog) rather than lua_probe.
- **B3** `pc/plugins/rood.options` (pages as Lua tables, tests),
  `pc/tests/lua/plugins/test.menu` (a plugin's page with settings defined in
  place), Extensions.md "Option pages".

**Deviations:** no `rl::registry_generation()`: the page model's
generation (`pc_option_pages_generation`, `rood.menu.generation`) is the one
renderers watch, bumped by every define and removal including a plugin's
unload. `settings.reset_page` is not a core action: catalog rows record no
defaults (a seam: a `def` per row, then the action). Label width (truncate
with a warning) needs the classic renderer's measure: C1.

**Seams for C-E:**
- C1: `pc_option_pages_children(-1)` gives the Option-screen entries;
  `pc_option_page_rows/row(page, PC_UI_WHERE_CLASSIC, ...)` the rows (kinds,
  values, greyed reasons, link targets); `pc_ui_settings_catalog_change/set/
  activate` change a setting row; `pc_option_action_run` runs an action;
  `options_nav_step` moves; `vs_encode` encodes; watch
  `pc_option_pages_generation` and keep focus with `pc_option_page_find_row`.
- C2 / D1: a row's `confirm` flag says when to go through
  `pc_display_confirm_begin`; the settings writer must save
  `pc_display_confirm_held` values (not wired yet, nothing calls begin).
- D1 / D3: new rows go in the catalog (`ui_settings.cpp`, with their home) and
  in `rood.options/main.lua`; `lua_menu_probe` and the plugin's tests fail
  until both have them.

**Done (C-E, branch `m1-options2`):**
- **C1** MENU8 hooks (88.c, `ROOD_PC`: a `pcPage` state, 24 entries' room,
  the entries after the nine rows, the default case opening a page, `init`
  restoring the cursor past nine); `pc/src/ui/classic_options.c` (battle
  builds): rows as the game's menu windows at rowInfo 2 (ids 20-27, eight
  visible, the entry hoisted into the title stack above them), the value
  column, the game's flyout for choices (its cursor memory slot 0x46 saved
  and put back, the cursor on the current choice), links push sub-pages,
  bindings start the capture; RPC `options`. `options_nav` takes the window's
  row count. Labels are measured with the game's widths (cut with a warning;
  RPC `cut`): five catalog labels were shortened to fit.
- **C2** `display_confirm` wired through the catalog (a `confirm` row's choice
  goes through `pc_display_confirm_begin`; ticked with the settings' debounce on
  the real clock); the settings writer saves held keys' snapshots; classic
  KEEP / REVERT flyout with the seconds in the information box, modern modal
  card (pad, pointer, `ui_menu system.confirm`).
- **D1** `display`, `fullscreen_mode` (the confirm row), `vsync` (GL swap
  interval, Vulkan FIFO with a swapchain recreate).
- **D2** `remember_window` + `window_rect`: one helper for the three create
  sites (`pc_window_place`), reading the file (the settings load after the
  window exists).
- **D3** Render quality: `internal_resolution` 1x-8x then Match window
  (`auto`); Frame rate: `present_rate` a choice 60 / 120 / 180 / 240 fps /
  Match display (0), capped at the display's rate with vsync.
- **E1** `options_classic_smoke.py` (with the fixture plugin `test.menu`),
  **E2** its goldens of the Display page (original glyphs and ui_text font;
  `--update`, `pc/build/golden/<renderer>/`), **E3** `options_confirm_smoke.py`
  (keep / Cross / timeout / RPC keep in both screens; both screens' rows
  against `rood.menu.rows` for every core page); docs (README "PC options in
  the game's Option screen", Extensions.md).
- Also: the modern System list scrolls when plugins' pages overflow it.

**Deviations (C-E):**
- Look and feel are data: `option_style.h` values set from rood.options'
  `M.style` (`rood.menu.style`, core plugin only) and RPC `options {style}`;
  C keeps the same values as defaults for the fallback.
- The information box first held one line (a long help's lines took turns,
  `classic_help_seconds`). After the 2026-10-20 playtest it is set as the
  game's own descriptions are (MENU8's menu.vsString): up to two lines
  (`classic_help_lines`) of at most 270 pixels (`classic_help_width`, the
  originals' longest) with the game's newline (0xE8), broken at a sentence's
  end when each sentence fits a line; only help longer than two lines takes
  turns, a box at a time.
- Page rows sit at rowInfo 2 (eight rows) under the hoisted entry, as the
  game's own flyouts do, rather than nine at rowInfo 1.
- The game's TUs have no host stdio: `pc/src/host_format.c` gives them
  `pc_format`, `pc_log_line`, `pc_host_seconds`.
- Display and fullscreen-mode lists are dynamic catalog choices
  (`dyn_count`, `dyn_name`, `cfg`); headless (SDL offscreen) has one display
  and no exclusive modes, so the tests cover them through the RPC and the
  settings round trip; a real multi-monitor check stays manual.
