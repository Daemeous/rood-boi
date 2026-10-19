# The Lua visible layer (F1-F3)

Status: plan, 2026-10-18. Phase F1-F3 of
`2026-10-18-freedom-and-fast-iteration.md`.

Goal: a HUD or UI tweak in under a minute. Edit a Lua file, it hot-reloads in
the running game, and the moment is summoned with one command. The game
decides when and what; Lua decides how it looks.

Prerequisite: branch `hud-typography` merged. It adds `PC_TEXT_STATUS`,
`pc_text_status`, the `status`/`labels` styles and the Travel prompt keys.

## Data flow

```
game frame (BATTLE overlay)
  2842C.c label switch -> PC_HUD(kind, label, call)       pc/src/hud/hud_hook.h
     -> pc_hud_element(kind, label*, first, end)        pc/src/hud/hud_elements.cpp
          copy the fields into a fixed pool (kind, id, text[32], x, y, rgb, age, life, extra)
          claimed? classic: run the Lua draw now (packets into the same OT slot)
                   modern:  queue for the overlay pass
                   ok:      NOP the game's packets [first, end)
          no claim, or a Lua error: leave the game's packets (fallback)
presented frame: dev_overlay.cpp (after pc_lua_frame) -> rl::hud_frame()
     modern claims: fn(elem, d) -> fixed command buffer -> foreground draw list, picture space
pc_lua_tick (boot_runtime.c): query cache, rood.dev.show injection and hold, expiry
```

- Elements are copies, never pointers.
- With Lua off, nothing is claimed and the picture is today's.
- One owner per kind: the last in load order wins, with a warning.
- **Classic mode (`ui_menus` off):** a claim is honoured only with
  `hud_classic_override` (default 0). Otherwise only typography changes:
  `rood.hud.typography(style, {...})` is a runtime override layer over
  `pc_ui_text_styles`, never saved, and dropped on unload.

## Elements

| Kind | Source | Fields |
|---|---|---|
| status | 2842C.c `func_800958A4` (FAST!/SLOW!/TOO FAST!/success) | text, x, y, rgb, timing, age, life |
| chain | 2842C.c `func_80095A4C` | count, text, x, y, rgb, age, life |
| buff | 2842C.c `func_8009695C` (kinds 6/7) | stat, sign, value, text, x, y, age, life |
| damage | 2842C.c `func_80094B0C` | value, type, x, y, bounce, age |
| name_window | 5BF94.c `vs_battle_displaySceneMessage` | text, open/held (event only at first) |
| travel | fast_travel.c `pc_fast_travel_prompt()` | shown, glyph, anchor |

- **Events:** `rood.on("hud.status", fn)` and the like fire once per
  appearance, for logic. Drawing happens only through claims.
- **Queries** (`lua_api_game.cpp`), one cached read-only table per tick:
  - `rood.game.ashley()` gives hp/mp/risk/position/statuses;
  - `rood.game.mode()`, `rood.game.room()`, `rood.game.first_person()`.

## Draw API (`d` in draw functions, also `rood.draw`)

**Both flavours:**
- `d.style{font, size, color, stroke, stroke_color, tracking, stretch,
  align, anchor}` returns an immutable handle, parsed once.
- `d.text`, `d.measure`, `d.rect`, `d.gradient`, `d.glyph(button)`.

**Classic only** (hook pass), as PS1 packets anchored like
`PC_ANCHOR_PACKETS`:
- `d.sprite`;
- game-font `d.text`, through vs_encode and restyled by ui_text;
- `d.window`, `d.cursor`, `d.bar`.

**Coordinates** are the game's 320x240 space.

## Hooks

- **src** (tiny `ROOD_PC` wraps; commit-check must match): 2842C.c around
  the status, chain, buff and damage label calls.
- **pc:**
  - dev_overlay.cpp: the modern pass, and skipping the Travel prompt when
    claimed;
  - boot_runtime.c: the tick and end of frame;
  - Lua openers in the lua_api_settings.cpp pattern;
  - RPC `hud` and `restart` in dev_rpc.c.

## Performance

- **Budget:** at most 0.25 ms of Lua per presented frame for the whole HUD,
  and 0.1 ms per game frame for classic claims; `plugin_budget_ms` is the hard
  ceiling.
- **No allocation churn:**
  - element tables reused, text pushed only when it changes;
  - style handles;
  - a fixed 1024-command buffer and a 16 KB text arena;
  - a pool of 64 elements.
- **Measure:** `hud {stats}` gives mean and p99 ms and GC steps; the smoke
  asserts p99 under 1 ms and flat memory.

## Sandbox and hot reload

- Drawing, claims, events, typography and queries need no capability.
- `rood.dev` exists only in dev mode (`ROOD_RPC` set or `ROOD_DEV=1`).
- A reloaded `rood.hud` drops its claims, styles and overrides, then claims
  again. Held `show()` moments belong to the host and survive the reload. The
  loop: edit, about 0.5 s, the held moment is redrawn.
- A draw error keeps the game's packets and counts a fault; past the limit
  the plugin is disabled until its files change.

## Scenario bench (F3)

**`rood.dev.show(kind, args)`:**
- **In the field:** calls the game's own spawner, so both the claim and the
  fallback see a real element:

  | Kind | Spawner |
  |---|---|
  | status | `vs_battle_renderBattleAbilityTimingResult` |
  | chain | `func_80093F24` |
  | damage | `func_80093B68` |
  | buff | `func_80096768` |
  | name window | `vs_battle_displaySceneMessage` |
  | travel | a forced prompt flag |

  Hold pins the element until `rood.dev.clear`.
- **Elsewhere:** a synthetic element for modern claims.

**Gallery:**
- `rood.dev.bench.save(name)` saves a snapshot under `pc/user/bench/`, with
  the index in the plugin's storage.
- `load(name)` uses RPC `restart {snapshot}`, which re-execs with
  `ROOD_SNAPSHOT`.
- Entries go stale after a rebuild (the snapshot is tied to the code hash);
  Lua edits keep them valid.

**Entry points:**
- the console;
- a Bench tab in the Lua console;
- RPC `hud {show, clear, list, stats}`;
- `rood_rpc.py hud-show`.

## rood.hud (shipped, on)

- **Status:** typography (classic), or a stroked word (modern).
- **Buff text:** fitted inside its box, the outline kept in.
- **Travel prompt:** stroked text plus the icon, no backdrop. Its look is a
  Lua table; values come from the `qol_fast_travel_prompt_*` settings.

## Tests (no screenshots)

- **lua_probe:** validation, claims dropped on reload, `rood.dev` absent
  outside dev mode, a bounded buffer.
- **rood.hud's Lua tests:** show, wait, then drawn counts and fields.
- **`hud_bench_smoke.py`:** one launch. For each kind:
  - claimed, drawn and suppressed;
  - with the plugin off, the game's own drawing;
  - stats.

## Tasks (sequential)

0. Merge `hud-typography`.
1. Element core, status only: the PC_HUD macro, pool, NOP suppression, RPC
   `hud {list}`. Commit-check.
2. Modern pass and claims: draw and HUD APIs, style handles, stroked text.
   lua_probe.
3. Dev mode plus `show("status")` with hold. **The first end-to-end result:**
   edit SLOW!'s draw and it hot-reloads onto the held word.
4. The `rood.hud` plugin (status) and the smoke.
5. Chain, buff and damage.
6. Travel.
7. Queries.
8. Classic flavour primitives and `hud_classic_override`.
9. Name window (an event, and show).
10. Gallery: RPC `restart`, save/list/load, the Bench tab.
11. Performance assertions, docs.

## Risks to check early

- NOP suppression versus `pc_hw_anchor_packets` and ui_text's packet tracking
  (task 1 probes it).
- Classic primitives that allocate overlay state: use packet equivalents if
  they do.
- Re-exec on restart must keep the RPC socket and the card environment.
