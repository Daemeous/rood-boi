# Modern Menus — Phase 4: System (settings pages) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The System tab becomes a native settings hub: the game's screens (Data, Options, Score, Quick manual) plus pages Graphics, Display, Audio, Interface, Controls, Bindings and Gameplay that change the port's settings live and save them to `settings.cfg`, and the game's own options (saved with the game). New features: window mode (windowed / borderless / fullscreen), master volume, brightness, and key/pad rebinding with capture.

**Architecture:** A settings provider in C++ (`pc/src/ui/ui_settings.cpp`, in every build) describes pages and rows (kind: choice, toggle, slider, action, binding; label; help; value text; enabled + reason) and applies changes through the same functions the F1 editor uses (`pc_lighting_set`/`pc_lighting_apply_preset`, `pc_aa_*`, `pc_hw_set_scale`, plain variables for live keys, `pc_binding_setting`/`pc_bindings_reset`), then calls `pc_settings_save()` (debounced: on leaving a page or after 1 s of no change). The game's options (`vs_main_settings`) come through a small battle-side bridge with getters/setters that mirror MENU8's side effects. The shell's System screen shows a page list on the left and the page's rows on the right; Left/Right changes values, Circle runs actions or starts a binding capture.

**Tech Stack:** C/C++ (SDL3, Dear ImGui draw lists), Python RPC tests, ctest.

**Ground rules:** as in the earlier phases (branch `claude/musing-haslett-8e55eb`, merge main first, ROOD_PC + commit-check for `src/`, three builds + decl check, `run.py -j 2`, one heavy job at a time, never build in the main checkout, no personal paths/emails/other-project names/game data in commits, leak scan before the merge request). Coordinate with the main session before touching `gl_present.c`, `vk_present.c`, shaders, `boot_window.c`, `audio_output.c`, `boot_controller.c`, `button_prompts.c` — keep changes in small new blocks.

**Facts (research brief):**
- Settings: `pc/src/project_settings.cpp` `entries[]` (I/F/FN/B), hand-parsed keys (`internal_resolution`, `aa`, `aa_downsample`, `ui_upscale`, `ui_text`, `ui_lines`, `ui_bubbles`, `button_prompts`, `button_prompts_style`, `button_prompts_scale`, `bind_key_*`, `bind_pad_*`, `gameplay_walls`, lighting keys). Save = `pc_settings_save()` (whole file, `ROOD_SETTINGS`; status via `pc_settings_status()`; no dirty flag). `pc_settings_apply(text)` applies "key value" lines. Live reload every 0.5 s (`pc_settings_poll`).
- Apply steps: `internal_resolution` → `pc_hw_set_scale(n)` (only when `pc_hw_active`); AA live (MSAA ≤ `pc_aa_msaa_max()`); lighting via `pc_lighting_set(key, value)` / `pc_lighting_apply_preset(p)` / `pc_lighting_set("lighting_preset","auto")`, availability via `pc_lighting_unavailable(key, value)`, caps via `pc_lighting_caps`; everything else live by variable write; bindings via `pc_binding_setting` (bumps `pc_bind_generation`).
- Startup bug: `gl_present.c:982-989` / `vk_present.c:343-347` overwrite `pc_gte_widescreen` and `pc_hw_perspective` from env after settings load — fix: apply env only when set.
- Not existing: window mode, brightness, master volume, vibration (PadSetAct no-op; show Vibration as "No effect on PC yet").
- Renderer, ray tracing, window size: restart only (show read-only info).
- Game options (`vs_main_settings_t`, main.h:132-151; MENU8 88.c): simpleMap (3 Frame, 1 Simple, 0 Off), bit5 ability timing, bit6 weapon status, bit7 armor status, cursorMemory, information, puzzle mode = bit3 inverted (+ `vs_main_stateFlags.puzzleMode`; greyed when `puzzleModeDisabled`), monoSound (+ `vs_main_setMonoSound(v)`), vibrationOn. Saved in the game save.
- Bindings: `pc_bind_keys[16][2]`, `pc_bind_pad[16]`, names `pc_pad_button_names`, `pc_key_name`/`pc_key_from_name`, `pc_pad_element_name`/`_from_name`, `pc_binding_setting(key, value)`, `pc_bindings_reset()`; icons via `pc_prompt_glyph`.

## File structure

| File | Responsibility |
| --- | --- |
| `pc/src/ui/ui_settings.cpp/.h` | Pages/rows model + apply + save debounce (C++; every build) |
| `pc/src/ui/game_options.c` | (battle) get/set the game's options with MENU8's side effects |
| `pc/src/ui/ui_shell.cpp/.h` | System screen state (page list, rows, capture mode), JSON |
| `pc/src/ui/ui_system_draw.cpp` | drawing |
| `pc/src/boot_window.c` | + `window_mode` (live), raw input capture hook for bindings |
| `pc/src/audio_output.c` | + `master_volume` gain |
| present code + shaders | + `brightness` (per the main session's answer) |
| `pc/tests/ui_shell_probe.cpp`, `pc/tests/ui_system_smoke.py` | tests |

### Task 1: Settings model (`ui_settings.cpp`) + probe
- [ ] API (C): `int pc_ui_settings_pages(void)`, `const char* pc_ui_settings_page_title(int)`, `int pc_ui_settings_rows(int page)`, `int pc_ui_settings_row(int page, int row, pc_ui_setting_row* out)` (kind, label, help, value text, index/count or min/max/step/value, enabled, reason), `int pc_ui_settings_change(int page, int row, int delta)` (Left/Right; returns 1 when changed), `int pc_ui_settings_activate(int page, int row)` (actions; bindings start capture), `void pc_ui_settings_tick(double now)` (debounced save). Pages and rows per the brief's proposal (Graphics, Display, Audio, Interface, Controls, Bindings, Gameplay; game-option rows delegated to `game_options.c` via weak functions).
- [ ] Probe: build with stubs for the setters (the probe links `ui_settings.cpp` with a stub file that records calls), check page/row counts, value text, wrap/clamp, greyed rows with reasons, debounce (no save until 1 s quiet or page leave), `ui_menus` row saves.

### Task 2: New features
- [ ] **Window mode** (`boot_window.c`, new block): setting `window_mode windowed|borderless|fullscreen` (project_settings hand-parsed, own block), applied live with `SDL_SetWindowFullscreen` (+ `SDL_SetWindowFullscreenMode(window, NULL)` for borderless desktop); resize already handled. Remembered windowed size restored when leaving fullscreen.
- [ ] **Master volume** (`audio_output.c`, new block): `master_volume 0..100` (default 100) as a gain in the output callback (`feed()`), clamped, with a short ramp to avoid clicks.
- [ ] **Brightness** (present stage, GL + Vulkan, per the main session): `brightness 0..100` (default 50 = unchanged), applied as a gamma-style curve in the final present pass (not the UI overlay), both renderers.
- [ ] **Startup fix**: env overrides for widescreen/perspective only when the env var is set (per the main session).
- [ ] **Bindings capture** (`boot_window.c` raw event hook): `pc_ui_capture_begin(kind)` (key or pad), the next key-down / gamepad-button-down / trigger past 0.5 is reported (Escape cancels a key capture; Back/Select cancels a pad capture after 5 s of nothing), never reaches the game while capturing; the settings provider writes it with `pc_binding_setting("bind_key_<btn>", "...")` / `bind_pad_<btn>`; conflicts: the same key on another action is swapped (shown in a toast); Reset defaults row (`pc_bindings_reset`).

### Task 3: Game options bridge (`game_options.c`, battle)
- [ ] Getters/setters for each MENU8 option with the exact encodings and side effects above; enabled/reason for Puzzle mode; Vibration greyed ("No effect on PC yet").

### Task 4: System screen (shell) + drawing
- [ ] Shell: System tab = page list (Game: Data, Options (original), Score, Quick manual; Settings: the pages) on the left; Circle on a settings page enters it (rows on the right with focus), Left/Right change, Circle on actions/bindings, Cross back to the list, Square = default for the row (where defined). Capture mode: shell shows "Press a key…/button…", ignores navigation until the capture ends. Leaving the page flushes the save.
- [ ] Drawing per the prototype's System tab (page list, option rows "‹ value ›", sliders as bars, binding rows with key cap + pad glyph, help text at the bottom of the card, greyed reasons, save status line from `pc_settings_status()`); 4:3 and 16:9; mouse (click rows, click arrows to change, wheel on sliders).

### Task 5: Tests (`ui_system_smoke.py`)
- [ ] With `ROOD_SETTINGS` pointing at a temp file: change one row per page through the shell (RPC input) and check the variable/live effect and the saved file (e.g. widescreen → `pc_gl_view` aspect 16:9 and `widescreen 1` saved; AA; lighting preset; prompt style; camera stick speed; quick slots; master volume gain via an RPC readout; brightness value; window mode state (headless: the SDL window flags via RPC)); game options: simple map / sound mono (check `vs_main_settings` bytes); bindings: capture via injected SDL events (RPC or send_key on xvfb), check `bind_key_*` saved and the binding works in the field; restart the game process with the same settings file and check the values persisted (incl. widescreen after the startup fix).
- [ ] Run with the other menu suites, both archs.

### Task 6: Docs, review, merge request
- [ ] README (System tab, new keys `window_mode`, `master_volume`, `brightness`), Modern_Menus.md status; code review; fixes; merge-request checks; merge request; tell the user.
