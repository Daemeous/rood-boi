# Extension system Milestone 0: Lua runtime and bindings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Embed Lua 5.4 in rood-boi as a secure-by-default, capability-based plugin runtime (sandbox per plugin, instruction/time/memory budgets, error isolation, hot reload) with the core `rood` API (log, events and frame/tick hooks, timers, storage, registry, settings through the existing settings registries, a test harness), dev tooling (RPC `lua`, an overlay console, `rood_rpc.py` helpers) and fast tests (a CTest probe without the game, Lua tests run inside one running game), on all four build targets, before any mod or menu work.

**Architecture:** Lua 5.4.8 is vendored in `pc/third_party/lua` and compiled as C++ (its errors are C++ exceptions, so the bindings can hold `std::string` across Lua calls); every plugin gets its own `lua_State` with a capped allocator and a count-hook budget, and everything that crosses between plugins is copied (functions become proxies that run in their own plugin under its budget). A small C++ runtime in `pc/src/lua/` (runtime, sandbox, values, events/timers, registry, storage, capabilities, plugins, test harness, RPC) is game-independent and fully exercised by `ctest lua_probe`; three game-side pieces (`rood.settings`, the console, the hooks in `boot_runtime.c` / `dev_overlay.cpp` / `dev_rpc.c` / `project_settings.cpp`) connect it to the game, single-threaded on the game thread.

**Tech Stack:** Lua 5.4.8 (MIT, vendored, plain C API with a thin C++ helper layer; no sol2, no LuaRocks), C++17, the project's JSON (`prefab::Json`), Dear ImGui 1.93 (console), dev RPC (`pc/src/dev_rpc.c`), Python 3 (`pc/tools/rood_rpc.py`, `pc/tests/run.py`), CTest (`pc/CMakeLists.txt`), `build_boot.py` (32-bit, `--battle`, `--arch 64`, `--target windows`).

---

## Decisions this plan makes (beyond the brief)

| Decision | Why |
|---|---|
| **One `lua_State` per plugin** (not one state with an `_ENV` per plugin) | Isolation by construction: no shared string metatable, library tables or registry; the memory cap is simply the state's allocator; a reload is `lua_close` + a fresh state, so nothing a plugin did can survive it. Cost: values are copied between plugins (cheap at our sizes) and functions cross as proxies. |
| **Lua compiled as C++** (`g++ -x c++`, `LANGUAGE CXX` in CMake) | `lua_error` becomes a C++ exception, so binding code may keep `std::string`/`std::vector` alive across Lua calls without leaks or UB. Consequence: include `lua.h`/`lauxlib.h`/`lualib.h`, never `lua.hpp`; all binding files are `.cpp` (the brief's example names used `.c`). |
| **Budgets: count hook every 1000 instructions + wall clock; once exceeded, every following instruction fails** (hook count 1) | A plain error from a hook can be swallowed by `pcall` in a loop; making it sticky until control returns to C defeats that. |
| **One local change to the vendored `lstrlib.c`**: a `luai_patternstep` macro on each pattern-matcher step | Lua patterns run in C (no instructions, so no hook): `string.find(("a"):rep(4000), ".-.-.-.-.-.-.-b")` would hang a frame. Documented in `pc/third_party/lua/ROOD-NOTES.md`. |
| **No `io`, `os`, `debug`, `package`, `dofile`, `loadfile`, `warn`, `string.dump`** — the core libraries `liolib.c`, `loslib.c`, `loadlib.c`, `ldblib.c`, `linit.c`, `lua.c`, `luac.c` are vendored but not compiled | What is not linked cannot be reached. `require` is our own: the plugin's own `.lua` files only. |
| **`rood.settings` is generated from metadata** over three sources: the System pages' rows (`ui_settings.h`), standalone kinds of the editor schema (`editor_schema.h`), and plugin settings defined as schema kinds `plugin:<id>` | Every setting a plugin reads or changes is one the menus/dev tools show, validated by the same metadata (kinds, ranges, choices, why greyed). Adds `pc_ui_settings_find` and `rood_schema::remove_kind`. System rows have no recorded defaults, so `reset` works for schema/plugin settings only. Raw `settings.cfg` keys without metadata are not exposed. |
| **Grants, enable/disable, load order and the budgets live in `settings.cfg`** (`plugins_elevated`, `plugin_grant <id> <sha256> <capability>`, `plugin_enabled`, `plugin_order`, `plugin_budget_*`) | One user settings file; a grant names the code's hash, so a changed plugin is asked again. The budget keys come from one `tunables()` table that also drives the RPC and the console panel (tunable, not hard-coded). |
| **Content hash = SHA-256 of code files only** (`*.lua`, `*.fnl`, `plugin.json`) | Files a plugin writes into its folder with `fs.write` (data) do not revoke its own grants; code changes do. Our own SHA-256 (no new dependency). |
| **Storage**: a JSON key/value store and data files in `pc/user/plugin_data/<id>/` (`ROOD_PLUGIN_DATA`); `rood.storage.read` reads the plugin's own folder read-only | Never writes into committed `pc/plugins/`; writing into a plugin's folder is what the `fs.write` capability is for. |
| **Core events are un-namespaced** (`tick`, `frame`, `plugin.loaded`, `plugin.unloaded`, `registry.changed`, `unload`); **plugins may only emit namespaced events** (`my.plugin/changed`) and register ids in their own namespace (`<plugin id>/<name>`) | A plugin cannot fake the core's events or another plugin's entries. `rood.` ids are reserved for shipped plugins. |
| **Registry order**: `after`/`before` (an id or `group:<name>`), stable topological sort by registration order; cycles fall back to registration order with a warning | Order by relation, not index (the re-frame-like model); M1 places "PC Settings" after a vanilla entry. |
| **Faults**: 5 faults within 10 s disable a plugin at the next safe point (`settle`), with a log line; evals and tests never count | A broken plugin cannot spam every frame forever; a developer's typo in the console cannot disable the plugin they are debugging. |
| **`rood.test`**: test files `tests/*.lua` per plugin; cases are coroutines, `rood.test.wait(n)` resumes after n game ticks; RPC `lua {action "test"}` starts a run, `test_status` polls it | Lua tests run inside a running game (no relaunch), synchronous cases finish inside the RPC call, async ones advance with `Game.run`. |
| **Example plugin `rood.example` ships off** (`default_enabled = false`) | Vanilla stays vanilla; it documents the layout and its test runs in the suite. |

## Where Lua runs (single-threaded, the game thread)

| Entry point | Called from | When | Notes |
|---|---|---|---|
| `pc_lua_init` | `main()` in `pc/src/boot_runtime.c`, right after `pc_window_init` (which loads `settings.cfg`) and the `skip_intro` block | once | Records the game thread; scans `pc/plugins` and the user folder; loads enabled plugins in order. On 64-bit the game later switches to its low stack (`run_game_on_low_stack`) — same thread, Lua does not care which stack. |
| `pc_lua_tick` | `pc_tick()` in `boot_runtime.c`, after `pc_rpc_poll()` and `pc_fog_vsync()`, before `pc_window_tick()` | once per game vsync **while the game runs**; never while paused by the RPC (`pc_rpc_poll` blocks then) | `tick` event, game-time timers, settings watchers, waiting test cases, the file watcher every `watch_vsyncs`, then `settle` (disables, reloads). Without an overlay (software presenter) it also emits `frame`. |
| `pc_lua_frame(dt)` | `pc_overlay_frame()` in `pc/src/dev_overlay.cpp`, after `ImGui::NewFrame()` / `ImGuizmo::BeginFrame()` | every presented image: interpolated in-betweens (`present_rate`), and paused redraws | `frame` event (real time), real-time timers, the file watcher every 0.5 s, `settle`. The ImGui frame is open (M1 drawing). |
| `pc_lua_rpc` | `handle_method()` in `pc/src/dev_rpc.c` (`pc_rpc_poll`, start of `pc_tick` or the paused loop) | between frames | Dev-only: the RPC exists only with `ROOD_RPC`. |
| `pc_lua_console_window` | `pc_overlay_frame()` while the editor shows | each presented image | Draws only; evaluation goes through `rl::console_submit`. |
| `pc_lua_shutdown` | `atexit` (window close and RPC `quit` call `exit` on the game thread) | once | Writes stores and plugin settings; never runs Lua. |

Never on the SDL audio callback thread (`audio_output.c`), the detached helper threads (`lights.cpp` suggest, `fixture_editor.cpp`, `atmosphere.cpp` noise), or driver threads. `rl::run` checks the thread and refuses any other, with a log line.

## Ground rules for every task

- Work in a git worktree (Task 1), never build in the main checkout.
- Every build and test runs under `flock "$ROOD_HEAVY_LOCK" ...` (a lock shared by all agents; never a literal temp path in committed files), tests at `-j 2`. Never generate artificial CPU load.
- No changes under `src/` (the decompilation) in M0.
- Never commit game data (`data/`, `disks/`, `build/`, frames, saves, `pc/user/`).
- Committed files never mention other game ports by name and never contain absolute home-directory paths.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Windows/MinGW: `pc/src/win_compat.h` is force-included into host objects (not into the Lua core, Task 19). No new identifier may be a Windows macro (`DELETE`, `ERROR`, `IN`, `OUT`, `near`, `far`, `small`, `interface`): enums here are prefixed (`PLUGIN_*`, `BUDGET_*`, `LOG_*`). Use `PC_MTIME_NS`, `pc_mkdir`, `pc_replace_file` (`pc/src/platform.h`); `realpath`/`gmtime_r` only under `#ifndef _WIN32`.
- `pc/src/dev_overlay.cpp` and `pc/src/editor_schema.*` are shared with the main session: keep edits to the lines shown, merge `main` before the merge request.
- Lua: include `lua.h`, `lauxlib.h`, `lualib.h` (via `rl_internal.h`), never `lua.hpp`. Touch a plugin's state only inside `rl::run` (a protected call): any Lua API call outside one can raise and abort.

## File structure

Created:

| File | Responsibility |
|---|---|
| `pc/third_party/lua/src/*` | Lua 5.4.8 sources as released (only the core and safe libraries are compiled), one local change in `lstrlib.c`. |
| `pc/third_party/lua/LICENSE`, `UPSTREAM.txt`, `ROOD-NOTES.md` | The MIT licence (from `lua.h`), the version and checksum, the local change and what is not compiled. |
| `pc/src/lua/rood_luauser.h` | Force-included into the Lua core: `luai_patternstep` → `rood_lua_pattern_step`. |
| `pc/src/lua/lua_runtime.h` | The C API the game calls (`pc_lua_*`). |
| `pc/src/lua/rl_internal.h` | Internal C++ declarations shared by the `rl` files (structs, functions). |
| `pc/src/lua/lua_limits.cpp` | Tunables (`rl::config`), the capped allocator, the instruction/wall-clock budget hook, the pattern-matcher check. |
| `pc/src/lua/lua_runtime.cpp` | Plugin states, `rl::run` (protected call + budget + traceback), faults, the log, console sandboxes, eval, shared Lua helpers (guarded C functions, read-only tables), extension hook lists. |
| `pc/src/lua/lua_value.cpp` | `rl::Value`: copies between states, function proxies, `call_function`, JSON. |
| `pc/src/lua/lua_sandbox.cpp` | The safe libraries, stripped globals, text-only `load`, own-folder `require`, `print` to the log, limited `collectgarbage`. |
| `pc/src/lua/lua_api_core.cpp` | The `rood` table: log/warn/error, version, api_version, plugin info; opens the other modules. |
| `pc/src/lua/lua_events.cpp` | `rood.on/off/emit`, core events (`tick`, `frame`, ...), timers, game and real clocks. |
| `pc/src/lua/lua_registry.cpp` | `rood.registry`: namespaced entries by kind, after/before/group order, ownership. |
| `pc/src/lua/lua_files.cpp` | File helpers: read/write (atomic), mkdir -p, stat, list, safe relative paths. |
| `pc/src/lua/sha256.h`, `sha256.cpp` | SHA-256 for content hashes. |
| `pc/src/lua/lua_plugins.cpp` | Discovery, manifests, enable/order, dependency order, load/unload/reload, `settle`, hot reload watcher, settings lines, tunables, the game's entry points (`pc_lua_init/tick/frame/...`). |
| `pc/src/lua/lua_caps.cpp` | Capabilities: declarations, grant store, gate, `rood.fs`, refusing stubs (net/exec/memory/flags/native), audit log. |
| `pc/src/lua/lua_storage.cpp` | `rood.storage`: key/value store and data files. |
| `pc/src/lua/lua_test.cpp` | `rood.test`: assertions, case collection, coroutine runner, results as JSON. |
| `pc/src/lua/lua_rpc.cpp` | RPC method `lua`, `console_submit`, `pc_lua_emit_json`. |
| `pc/src/lua/lua_api_settings.cpp` | `rood.settings` (game side: `ui_settings.h`, `editor_schema.h`). |
| `pc/src/lua/lua_console.cpp` | The overlay's Lua console window (game side: ImGui). |
| `pc/tests/lua_probe.cpp` | CTest probe of the runtime without the game. |
| `pc/tests/lua_smoke.py` | One game: every Lua test, then the hard cases over RPC. |
| `pc/tests/lua/plugins/test.*/` | Fixture plugins and their Lua tests (core, sandbox, settings, isolation a/b, caps, reload). |
| `pc/plugins/rood.example/` | The shipped example plugin (off by default) and its test. |
| `pc/docs/Extensions.md` | Plugins, manifest, API, budgets, capabilities, where Lua runs, tools, tests. |

Modified:

| File | Change |
|---|---|
| `pc/CMakeLists.txt` | `rood_lua` library (Lua as C++), `lua_probe` test. |
| `pc/tools/build_boot.py` | Compiles the Lua core and `pc/src/lua/*.cpp` on every target; keeps `win_compat.h` out of the Lua core. |
| `pc/tools/windows_dist.py` | Ships `pc/plugins` and Lua's licence. |
| `pc/src/boot_runtime.c` | `pc_lua_init` in `main`, `pc_lua_tick` in `pc_tick` (weak). |
| `pc/src/dev_overlay.cpp` | `pc_lua_frame` in `pc_overlay_frame`; the console window and its Window menu item. |
| `pc/src/dev_rpc.c` | Routes method `lua`; documents it. |
| `pc/src/project_settings.cpp` | Reads/writes the `plugin*` lines. |
| `pc/src/ui/ui_settings.h`, `.cpp` | `pc_ui_settings_find(id, &page, &row)`. |
| `pc/tests/ui_settings_probe.cpp` | Checks `pc_ui_settings_find`. |
| `pc/src/editor_schema.h`, `.cpp` | `rood_schema::remove_kind`. |
| `pc/tools/rood_rpc.py` | `Game.lua`, `Game.lua_test`, `Game.lua_plugins`; CLI `lua`, `lua-test`. |
| `pc/tests/run.py` | Suite `lua_smoke`; every suite gets scratch `ROOD_PLUGINS` / `ROOD_PLUGIN_DATA`. |
| `THIRD_PARTY.md`, `pc/README.md` | Lua's row; a short section and the test table rows. |

---

### Task 1: Worktree and baseline

**Files:** none changed.

- [ ] **Step 1: Branch in a worktree**

Use superpowers:using-git-worktrees. From the main checkout:

```bash
git worktree add .claude/worktrees/extension-m0 -b claude/extension-m0 main
cd .claude/worktrees/extension-m0
ln -s ../../../data data && ln -s ../../../disks disks
cp -a ../../../build build
```

Expected: a worktree on `claude/extension-m0`; `ls -d data disks build` lists all three. (`build/` is copied, not linked: the reference link reads it through Docker.)

- [ ] **Step 2: Configure CTest in this tree**

```bash
cmake -S pc -B pc/build -G Ninja
```

Expected: `-- Build files have been written to: .../pc/build`.

- [ ] **Step 3: Build the battle binary and record the trap count**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tail -3
```

Expected: a line `Built .../pc/build/boot/rood_battle_probe: N data imports, M fail-fast function imports`. Write down `M`: Task 19 must leave it unchanged (the Lua objects must not add a trap).

- [ ] **Step 4: Baseline of the suites this plan touches**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest rpc_smoke system_settings ui_system editor_schema
```

Expected: a table ending in `N/N passed` (write down any suite that already fails; later tasks must not add failures).

---

### Task 2: Vendor Lua 5.4.8

**Files:**
- Create: `pc/third_party/lua/src/*.c`, `*.h`, `lua.hpp` (upstream), `pc/third_party/lua/LICENSE`, `UPSTREAM.txt`, `ROOD-NOTES.md`
- Modify: `pc/third_party/lua/src/lstrlib.c` (one local change), `THIRD_PARTY.md`

- [ ] **Step 1: Download and verify the release**

```bash
mkdir -p pc/build/lua-download
curl -fL -o pc/build/lua-download/lua-5.4.8.tar.gz https://www.lua.org/ftp/lua-5.4.8.tar.gz
sha256sum pc/build/lua-download/lua-5.4.8.tar.gz
```

Expected: the printed SHA-256 equals the one https://www.lua.org/ftp/ lists for `lua-5.4.8.tar.gz`. If they differ, stop. (If the harness asks before downloads, ask the user: `lua-5.4.8.tar.gz`, from lua.org, about 370 KB.)

- [ ] **Step 2: Copy the sources**

```bash
tar -xzf pc/build/lua-download/lua-5.4.8.tar.gz -C pc/build/lua-download
mkdir -p pc/third_party/lua/src
cp pc/build/lua-download/lua-5.4.8/src/*.c pc/build/lua-download/lua-5.4.8/src/*.h \
   pc/build/lua-download/lua-5.4.8/src/lua.hpp pc/third_party/lua/src/
ls pc/third_party/lua/src | wc -l
```

Expected: `63` files (35 `.c`, 27 `.h`, `lua.hpp`; the count may differ by one or two between 5.4 releases — what matters is that `lapi.c`, `lstrlib.c`, `lauxlib.c`, `lbaselib.c` and `luaconf.h` are there).

- [ ] **Step 3: Extract the licence from `lua.h`**

```bash
python3 - <<'EOF'
import pathlib, re
text = pathlib.Path("pc/third_party/lua/src/lua.h").read_text()
lines = text[text.index("* Copyright (C) 1994"):].splitlines()
out = []
for line in lines:
    if re.fullmatch(r"\*+/", line.strip()):
        break
    out.append(re.sub(r"^\* ?", "", line))
pathlib.Path("pc/third_party/lua/LICENSE").write_text("\n".join(out).strip() + "\n")
EOF
head -3 pc/third_party/lua/LICENSE
```

Expected: `Copyright (C) 1994-2025 Lua.org, PUC-Rio.`, an empty line, `Permission is hereby granted, free of charge, to any person obtaining`.

- [ ] **Step 4: The pattern matcher's budget check (the one local change)**

```bash
python3 - <<'EOF'
import pathlib
path = pathlib.Path("pc/third_party/lua/src/lstrlib.c")
text = path.read_text()
anchor = "  init: /* using goto to optimize tail recursion */\n"
assert text.count(anchor) == 1, "lstrlib.c differs from 5.4.8: find match()'s init label by hand"
text = text.replace(anchor, anchor + "  luai_patternstep(ms->L);  /* rood-boi: budget check (ROOD-NOTES.md) */\n")
include = '#include "lualib.h"\n'
assert text.count(include) == 1
text = text.replace(include, include + "\n\n/* rood-boi: called on every step of the pattern matcher (ROOD-NOTES.md) */\n"
                    "#if !defined(luai_patternstep)\n#define luai_patternstep(L)\t((void)0)\n#endif\n")
path.write_text(text)
EOF
grep -c luai_patternstep pc/third_party/lua/src/lstrlib.c
```

Expected: `3`.

- [ ] **Step 5: Write `pc/third_party/lua/UPSTREAM.txt`**

```text
https://www.lua.org/ftp/lua-5.4.8.tar.gz
Version: 5.4.8
SHA-256: <the value printed in Step 1, verified against https://www.lua.org/ftp/>
Contents: src/ of the release (all .c and .h files and lua.hpp), unchanged except
lstrlib.c (ROOD-NOTES.md). doc/, Makefile and test files are not vendored.
```

Replace the angle-bracket text with the 64 hex digits from Step 1 before committing.

- [ ] **Step 6: Write `pc/third_party/lua/ROOD-NOTES.md`**

```markdown
# Lua in rood-boi

Lua 5.4.8 runs plugins (pc/docs/Extensions.md). It is compiled as C++
(pc/tools/build_boot.py, pc/CMakeLists.txt), so a Lua error is a C++
exception; include lua.h, lauxlib.h and lualib.h, never lua.hpp.

## Compiled

The core (lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject
lopcodes lparser lstate lstring ltable ltm lundump lvm lzio), lauxlib and
the safe libraries (lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib).

Not compiled (vendored only so that an upgrade is a plain copy): liolib.c,
loslib.c, loadlib.c, ldblib.c, linit.c, lua.c, luac.c. A plugin cannot
reach what is not linked.

## Local change

lstrlib.c, match(): `luai_patternstep(ms->L)` at the `init:` label, so every
step of the pattern matcher (which runs in C, where the instruction hook
never fires) can check the plugin's time budget. The macro is empty unless
pc/src/lua/rood_luauser.h is force-included (-include), which defines it as
rood_lua_pattern_step (pc/src/lua/lua_limits.cpp). Reapply it on upgrade
(the Python snippet in the M0 plan, Task 2 Step 4, asserts its anchors).
```

- [ ] **Step 7: Add Lua to `THIRD_PARTY.md`**

In the table under "Vendored in `pc/third_party/`", after the PsyCross software SPU row, add:

```markdown
| [Lua](https://www.lua.org) (`lua/`) | 5.4.8 | MIT, Copyright (C) 1994-2025 Lua.org, PUC-Rio (`LICENSE`) | Runs plugins (`pc/docs/Extensions.md`): the core and its safe libraries, compiled as C++ into the game; the io, os, package and debug libraries and the standalone interpreters are not built. One local change, a budget check in the pattern matcher (`ROOD-NOTES.md`); `UPSTREAM.txt`. |
```

- [ ] **Step 8: Commit**

```bash
git add pc/third_party/lua THIRD_PARTY.md
git commit -m "$(cat <<'EOF'
Vendor Lua 5.4.8 for plugins

The release's src/ as is, its MIT licence, and one local change: a
budget check on every step of the pattern matcher (ROOD-NOTES.md).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Lua compiled as C++, the probe, the pattern-step hook

**Files:**
- Create: `pc/src/lua/rood_luauser.h`, `pc/src/lua/rl_internal.h`, `pc/src/lua/lua_limits.cpp`, `pc/tests/lua_probe.cpp`
- Modify: `pc/CMakeLists.txt` (append at the end)

- [ ] **Step 1: Write the failing probe `pc/tests/lua_probe.cpp`**

```cpp
// The extension system without the game (pc/src/lua; pc/docs/Extensions.md):
// Lua compiled as C++, the allocator's cap, the budgets, the sandbox, values
// between plugins, events, timers, the registry, plugins on disk, storage,
// capabilities, hot reload, the test harness and the RPC. ctest lua_probe
// (argv[1]: a scratch directory the probe owns).
#include "lua/rl_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string scratch;
static bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

/* Lua is compiled as C++: an error unwinds to lua_pcall as an exception; the
 * pattern matcher calls rood_lua_pattern_step (rood_luauser.h). */
static void lua_core()
{
    rl::Plugin p;
    p.manifest.id = "@probe-core";
    lua_State* L = rl::new_state(&p);
    CHECK(L != nullptr);
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_settop(L, 0);
    CHECK(luaL_dostring(L, "error('boom')") == LUA_ERRRUN && contains(lua_tostring(L, -1), "boom"));
    lua_settop(L, 0);
    unsigned long long before = rl::pattern_steps;
    CHECK(luaL_dostring(L, "return string.find(string.rep('a', 50), 'a-b')") == LUA_OK);
    CHECK(rl::pattern_steps > before);
    lua_close(L);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: lua_probe <scratch directory>\n");
        return 2;
    }
    scratch = argv[1];
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch + "/plugins");
    setenv("ROOD_PLUGINS", (scratch + "/plugins").c_str(), 1);
    setenv("ROOD_PLUGIN_DATA", (scratch + "/data").c_str(), 1);
    setenv("ROOD_PLUGINS_SHIPPED", "", 1);
    lua_core();
    if (failures) {
        std::printf("lua_probe: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("lua_probe: ok\n");
    return 0;
}
```

- [ ] **Step 2: Add the library and the probe to `pc/CMakeLists.txt`** (append at the end)

```cmake
# The extension system (pc/src/lua, pc/docs/Extensions.md): Lua 5.4 (pc/third_party/lua) compiled as
# C++ so its errors are exceptions, with the pattern matcher's budget hook (rood_luauser.h), and the
# runtime without the game (lua_probe).
set(ROOD_LUA_CORE_NAMES lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate
    lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib lmathlib lstrlib ltablib lutf8lib)
set(ROOD_LUA_CORE)
foreach(name IN LISTS ROOD_LUA_CORE_NAMES)
    list(APPEND ROOD_LUA_CORE third_party/lua/src/${name}.c)
endforeach()
set_source_files_properties(${ROOD_LUA_CORE} PROPERTIES LANGUAGE CXX)
add_library(rood_lua STATIC ${ROOD_LUA_CORE})
target_include_directories(rood_lua PUBLIC third_party/lua/src)
target_compile_features(rood_lua PUBLIC cxx_std_17)
target_compile_definitions(rood_lua PRIVATE ROOD_LUA_CORE)
target_compile_options(rood_lua PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/src/lua/rood_luauser.h)
set(ROOD_LUA_RUNTIME src/lua/lua_limits.cpp)
find_package(Threads REQUIRED)
add_executable(lua_probe tests/lua_probe.cpp ${ROOD_LUA_RUNTIME})
target_include_directories(lua_probe PRIVATE src src/lua)
target_link_libraries(lua_probe PRIVATE rood_lua Threads::Threads)
target_compile_options(lua_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME lua_probe COMMAND lua_probe ${CMAKE_CURRENT_BINARY_DIR}/lua_probe_scratch)
```

- [ ] **Step 3: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | tail -5
```

Expected: the build stops: `Cannot find source file: src/lua/lua_limits.cpp` (or `lua/rl_internal.h: No such file or directory`).

- [ ] **Step 4: Write `pc/src/lua/rood_luauser.h`**

```c
/* Force-included into the vendored Lua core (-include; build_boot.py,
 * CMakeLists.txt; pc/third_party/lua/ROOD-NOTES.md): the pattern matcher's
 * budget check. Lua is compiled as C++, so this is a C++ declaration. */
#ifndef ROOD_LUAUSER_H
#define ROOD_LUAUSER_H
struct lua_State;
void rood_lua_pattern_step(struct lua_State* L);
#define luai_patternstep(L) rood_lua_pattern_step(L)
#endif
```

- [ ] **Step 5: Write `pc/src/lua/rl_internal.h`** (the core declarations; later tasks add sections before the closing `} // namespace rl`)

```cpp
/* The extension system's internals (pc/src/lua; pc/docs/Extensions.md).
 * Lua 5.4 (pc/third_party/lua) is compiled as C++: its errors are C++
 * exceptions, so these files may keep std::string and friends across Lua
 * calls. Include lua.h / lauxlib.h / lualib.h (here), never lua.hpp: its
 * extern "C" would not match the C++ definitions. Everything runs on the
 * game thread (rl::run checks), and a plugin's state is only touched inside
 * rl::run. */
#ifndef ROOD_RL_INTERNAL_H
#define ROOD_RL_INTERNAL_H
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#define RL_VERSION "0.1.0"     /* rood.version */
#define RL_API_VERSION 1       /* rood.api_version; manifests name the one they need */
#define RL_HOOK_STEP 1000      /* instructions between two budget checks */
#define RL_MAX_FILE (4u << 20) /* a script, manifest or data file */

namespace prefab {
struct Json;
}

namespace rl {

/* ---- Tunables: settings plugin_* (lua_plugins.cpp tunables()), RPC lua {action "budget"}, the console ---- */
struct Config {
    double budget_ms = 4;                   /* one handler, timer or call between plugins */
    long long budget_instructions = 2000000;
    double load_budget_ms = 250;            /* a main chunk, a test step, an eval */
    long long load_budget_instructions = 50000000;
    double memory_mb = 32;                  /* each plugin's cap */
    int fault_limit = 5;                    /* this many faults ... */
    double fault_window_s = 10;             /* ... within this many seconds disable a plugin */
    int watch_vsyncs = 30;                  /* hot reload: files checked every N vsyncs (and every 0.5 s of frames) */
    bool elevated = false;                  /* plugins_elevated: elevated capabilities may be granted */
};
extern Config config;

struct Memory {
    size_t used = 0, peak = 0;
    size_t cap = 0;   /* 0: none */
    size_t grace = 0; /* extra room while an error is being reported */
    unsigned long long refusals = 0;
};
struct Budget {
    bool active = false, killed = false;
    double start = 0, deadline = 0, limit_ms = 0;
    long long instructions = 0, max_instructions = 0;
};
/* CALL: handlers, timers, calls between plugins. LOAD: main chunks. TEST: evals and test
 * steps (load-sized; their faults are logged but never count towards disabling). */
enum BudgetKind { BUDGET_CALL, BUDGET_LOAD, BUDGET_TEST };

struct Stats {
    unsigned long long calls = 0, faults = 0, kills = 0;
    double total_ms = 0, max_ms = 0;
};
enum PluginState { PLUGIN_OFF, PLUGIN_LOADED, PLUGIN_FAILED, PLUGIN_DISABLED };

struct Dependency {
    std::string id, min_version; /* min_version "" : any */
};
struct Manifest {
    std::string id, name, version = "0.0.0", entry = "main.lua", description, author, tests = "tests";
    int api_version = RL_API_VERSION;
    bool default_enabled = true;
    std::vector<Dependency> depends;
    std::vector<std::string> capabilities; /* "fs.read", "fs.write:pc/user/x", "net:example.com", ... */
};

struct Plugin {
    Manifest manifest;
    std::string dir;          /* its folder ("pc/plugins/rood.example"); "" for a console sandbox */
    std::string data_dir;     /* its storage ("pc/user/plugin_data/rood.example"); "" for a console */
    bool shipped = false;     /* from pc/plugins */
    bool console = false;     /* a sandbox without a folder (eval, the console) */
    bool manifest_ok = false; /* its manifest parsed and checked */
    bool waiting = false;     /* failed for a dependency: retried when it loads */
    std::string hash;         /* SHA-256 of its code files (grants follow it) */
    std::vector<std::string> granted; /* capabilities active in this load */
    lua_State* L = nullptr;
    unsigned generation = 0; /* every load and unload: references into an older state go stale */
    int state = PLUGIN_OFF;
    std::string status; /* "loaded", or why it is not */
    Memory mem;
    Budget budget;
    Stats stats;
    int depth = 0; /* calls into it in progress */
    std::vector<double> fault_times;
    std::string disable_reason; /* set by fault(): settle() disables it */
    bool reload_requested = false;
    std::vector<int> pending_unrefs; /* registry references freed at its next call */
    std::map<std::string, std::pair<long long, long long>> files; /* hot reload: path -> (mtime ns, size) */
    const std::string& id() const { return manifest.id; }
};

/* ---- lua_limits.cpp ---- */
double now_seconds(); /* monotonic */
void* alloc(void* ud, void* ptr, size_t osize, size_t nsize); /* lua_Alloc; ud: the plugin's Memory */
lua_State* new_state(Plugin* p); /* with p's allocator and p in the extra space; no libraries */
Plugin* plugin_of(lua_State* L);
extern unsigned long long pattern_steps; /* every pattern-matcher step so far (the probe) */

} // namespace rl
#endif
```

- [ ] **Step 6: Write `pc/src/lua/lua_limits.cpp`** (allocator without a cap yet, the pattern-step counter)

```cpp
// The safety nets every plugin state has (pc/docs/Extensions.md, "Budgets"):
// the allocator that counts a plugin's memory, and the pattern matcher's
// step hook (lstrlib.c luai_patternstep, pc/third_party/lua/ROOD-NOTES.md).
#include "rl_internal.h"

#include <cstdlib>
#include <ctime>

namespace rl {

Config config;
unsigned long long pattern_steps;

double now_seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

void* alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    Memory* m = (Memory*)ud;
    size_t old = ptr ? osize : 0; /* without ptr, osize is a type tag */
    if (nsize == 0) {
        free(ptr);
        m->used -= old;
        return nullptr;
    }
    void* p = realloc(ptr, nsize);
    if (!p)
        return nsize <= old ? ptr : nullptr; /* Lua assumes shrinking never fails */
    m->used = m->used - old + nsize;
    if (m->used > m->peak)
        m->peak = m->used;
    return p;
}

lua_State* new_state(Plugin* p)
{
    lua_State* L = lua_newstate(alloc, &p->mem);
    if (L)
        *(Plugin**)lua_getextraspace(L) = p; /* threads created later copy it */
    return L;
}

Plugin* plugin_of(lua_State* L) { return *(Plugin**)lua_getextraspace(L); }

} // namespace rl

/* lstrlib.c (luai_patternstep): every step of the pattern matcher. */
void rood_lua_pattern_step(lua_State*) { ++rl::pattern_steps; }
```

- [ ] **Step 7: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | tail -2
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R '^lua_probe$' --output-on-failure | tail -3
```

Expected: `100% tests passed, 0 tests failed out of 1`; with `--output-on-failure -V` the probe prints `lua_probe: ok`.

- [ ] **Step 8: Commit**

```bash
git add pc/src/lua/rood_luauser.h pc/src/lua/rl_internal.h pc/src/lua/lua_limits.cpp pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua compiled as C++ with the pattern-step hook; lua_probe

rood_lua (CMake) builds the vendored core and safe libraries as C++, so
Lua errors are exceptions; lstrlib's luai_patternstep calls into
lua_limits.cpp. lua_probe exercises the runtime without the game.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: The allocator's cap

**Files:**
- Modify: `pc/tests/lua_probe.cpp`, `pc/src/lua/lua_limits.cpp`

- [ ] **Step 1: Write the failing test** — add before `main` in `pc/tests/lua_probe.cpp`:

```cpp
/* Each plugin's allocator counts its memory and refuses past the cap: Lua
 * raises "not enough memory", the state stays usable, and closing it gives
 * every byte back. */
static void allocator_cap()
{
    rl::Plugin p;
    p.manifest.id = "@probe-memory";
    p.mem.cap = 2u << 20;
    lua_State* L = rl::new_state(&p);
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_settop(L, 0);
    CHECK(p.mem.used > 0 && p.mem.used < p.mem.cap);
    CHECK(luaL_dostring(L, "local t = {} for i = 1, 1e7 do t[i] = ('x'):rep(64) .. i end") == LUA_ERRMEM);
    lua_settop(L, 0);
    CHECK(p.mem.refusals > 0);
    CHECK(p.mem.peak <= p.mem.cap);
    lua_gc(L, LUA_GCCOLLECT);
    CHECK(p.mem.used < p.mem.cap / 2);
    CHECK(luaL_dostring(L, "return 1 + 1") == LUA_OK && lua_tointeger(L, -1) == 2);
    lua_close(L);
    CHECK(p.mem.used == 0);
}
```

and call it in `main` after `lua_core();`:

```cpp
    allocator_cap();
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch
```

Expected: the loop never runs out of memory (or takes several seconds), then `FAIL ... luaL_dostring(...) == LUA_ERRMEM`, `FAIL ... p.mem.refusals > 0`, `lua_probe: 2 failure(s)` or more.

- [ ] **Step 3: Implement the cap** — in `rl::alloc` (`pc/src/lua/lua_limits.cpp`), between the `nsize == 0` block and `realloc`:

```cpp
    if (nsize > old && m->cap && m->used - old + nsize > m->cap + m->grace) {
        ++m->refusals;
        return nullptr; /* Lua raises "not enough memory" */
    }
```

and change the file's header comment's first sentence to: `// The safety nets every plugin state has (pc/docs/Extensions.md, "Budgets"): an allocator that counts a plugin's memory and refuses past its cap, and the pattern matcher's step hook (lstrlib.c luai_patternstep, pc/third_party/lua/ROOD-NOTES.md).`

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch
```

Expected: `lua_probe: ok`.

- [ ] **Step 5: Commit**

```bash
git add pc/tests/lua_probe.cpp pc/src/lua/lua_limits.cpp
git commit -m "$(cat <<'EOF'
Lua: each plugin's allocator refuses past its memory cap

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Instruction and wall-clock budgets

**Files:**
- Modify: `pc/tests/lua_probe.cpp`, `pc/src/lua/rl_internal.h`, `pc/src/lua/lua_limits.cpp`

- [ ] **Step 1: Write the failing test** — add before `main`:

```cpp
static int budget_run(rl::Plugin& p, const char* code, double* ms)
{
    rl::begin_budget(&p, rl::BUDGET_CALL);
    int status = luaL_dostring(p.L, code);
    *ms = rl::end_budget(&p);
    return status;
}

/* A runaway script is stopped by its instruction budget or its wall clock,
 * pcall and coroutines cannot keep it alive, a string pattern (C code) is
 * stopped too, and the next call runs normally. */
static void budgets()
{
    rl::Plugin p;
    p.manifest.id = "@probe-budget";
    p.L = rl::new_state(&p);
    luaL_requiref(p.L, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(p.L, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(p.L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_settop(p.L, 0);
    double ms = 0;
    rl::config.budget_instructions = 200000;
    rl::config.budget_ms = 2000;
    CHECK(budget_run(p, "while true do end", &ms) == LUA_ERRRUN);
    CHECK(contains(lua_tostring(p.L, -1), "budget exceeded"));
    CHECK(p.budget.instructions >= 200000 && p.budget.instructions < 200000 + 2 * RL_HOOK_STEP);
    lua_settop(p.L, 0);
    CHECK(budget_run(p, "while true do pcall(function() while true do end end) end", &ms) == LUA_ERRRUN);
    lua_settop(p.L, 0);
    CHECK(budget_run(p, "local co = coroutine.wrap(function() while true do end end) co()", &ms) == LUA_ERRRUN);
    lua_settop(p.L, 0);
    rl::config.budget_instructions = 1LL << 50;
    rl::config.budget_ms = 30;
    CHECK(budget_run(p, "while true do end", &ms) == LUA_ERRRUN && ms >= 29 && ms < 500);
    lua_settop(p.L, 0);
    CHECK(budget_run(p, "return string.find(string.rep('a', 4000), '.-.-.-.-.-.-.-b')", &ms) == LUA_ERRRUN);
    CHECK(contains(lua_tostring(p.L, -1), "string pattern") && ms < 500);
    lua_settop(p.L, 0);
    rl::config = rl::Config();
    CHECK(budget_run(p, "local n = 0 for i = 1, 1000 do n = n + i end return n", &ms) == LUA_OK);
    CHECK(lua_tointeger(p.L, -1) == 500500);
    lua_close(p.L);
}
```

and in `main`: `budgets();` after `allocator_cap();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `error: 'begin_budget' is not a member of 'rl'`.

- [ ] **Step 3: Declare the budget functions** — add to `pc/src/lua/rl_internal.h`, in the `lua_limits.cpp` section:

```cpp
void begin_budget(Plugin* p, BudgetKind kind); /* a call starts: deadline, instruction limit, the hook */
double end_budget(Plugin* p);                  /* it ended: elapsed ms */
/* Raises "budget exceeded<where>: ..." (never returns). */
void budget_error(lua_State* L, const Budget& b, const char* where);
```

- [ ] **Step 4: Implement them** — in `pc/src/lua/lua_limits.cpp`, add `#include <cstdio>` to the includes, replace the trailing `rood_lua_pattern_step` and add inside the namespace, after `plugin_of`:

```cpp
void budget_error(lua_State* L, const Budget& b, const char* where)
{
    char text[200];
    std::snprintf(text, sizeof text, "budget exceeded%s: %.1f ms and %lld instructions (limits %.1f ms, %lld)",
        where, (now_seconds() - b.start) * 1000.0, b.instructions, b.limit_ms, b.max_instructions);
    luaL_error(L, "%s", text);
}

/* Every RL_HOOK_STEP instructions (every instruction once a call is over
 * budget, so that pcall cannot swallow the error and go on). */
static void hook(lua_State* L, lua_Debug*)
{
    Plugin* p = plugin_of(L);
    if (!p)
        return;
    Budget& b = p->budget;
    int count = lua_gethookcount(L);
    b.instructions += count;
    if (!b.active)
        return;
    if (!b.killed && (b.instructions > b.max_instructions || now_seconds() > b.deadline))
        b.killed = true;
    if (b.killed) {
        if (count != 1)
            lua_sethook(L, hook, LUA_MASKCOUNT, 1);
        budget_error(L, b, "");
    }
    if (count != RL_HOOK_STEP) /* a coroutine left at 1 by an earlier call's kill */
        lua_sethook(L, hook, LUA_MASKCOUNT, RL_HOOK_STEP);
}

void begin_budget(Plugin* p, BudgetKind kind)
{
    Budget& b = p->budget;
    bool load = kind != BUDGET_CALL;
    b.active = true;
    b.killed = false;
    b.instructions = 0;
    b.limit_ms = load ? config.load_budget_ms : config.budget_ms;
    b.max_instructions = load ? config.load_budget_instructions : config.budget_instructions;
    b.start = now_seconds();
    b.deadline = b.start + b.limit_ms / 1000.0;
    lua_sethook(p->L, hook, LUA_MASKCOUNT, RL_HOOK_STEP);
}

double end_budget(Plugin* p)
{
    p->budget.active = false;
    p->mem.grace = 0;
    return (now_seconds() - p->budget.start) * 1000.0;
}
```

and at the end of the file:

```cpp
/* lstrlib.c (luai_patternstep): every step of the pattern matcher, which
 * runs in C where the instruction hook never fires. */
void rood_lua_pattern_step(lua_State* L)
{
    if (++rl::pattern_steps & 1023)
        return;
    rl::Plugin* p = rl::plugin_of(L);
    if (!p || !p->budget.active)
        return;
    rl::Budget& b = p->budget;
    if (b.killed || rl::now_seconds() > b.deadline) {
        b.killed = true;
        rl::budget_error(L, b, " in a string pattern");
    }
}
```

Update the header comment to: `// The safety nets every plugin state has (pc/docs/Extensions.md, "Budgets"): an allocator that counts a plugin's memory and refuses past its cap; an instruction and wall-clock budget per call (a count hook every RL_HOOK_STEP instructions; once exceeded, every following instruction fails, so pcall cannot swallow it); the same clock inside the string pattern matcher, which runs in C (lstrlib.c luai_patternstep, pc/third_party/lua/ROOD-NOTES.md).`

- [ ] **Step 5: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch
```

Expected: `lua_probe: ok` within about a second.

- [ ] **Step 6: Commit**

```bash
git add pc/tests/lua_probe.cpp pc/src/lua/rl_internal.h pc/src/lua/lua_limits.cpp
git commit -m "$(cat <<'EOF'
Lua: instruction and wall-clock budgets per call

A count hook checks both every 1000 instructions; once over, every
instruction fails until the call returns, so pcall and coroutines cannot
keep a runaway alive. The pattern matcher checks the clock too.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 6: Protected calls, values between plugins, the log

**Files:**
- Create: `pc/src/lua/lua_runtime.cpp`, `pc/src/lua/lua_value.cpp`, `pc/src/lua/lua_sandbox.cpp` (first version), `pc/src/lua/lua_api_core.cpp` (first version)
- Modify: `pc/src/lua/rl_internal.h`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — in `pc/tests/lua_probe.cpp` add `#include <thread>` and `#include <vector>` to the includes, then before `main`:

```cpp
static std::string eval_json(rl::Plugin* p, const char* code)
{
    if (!p)
        return "no plugin";
    std::vector<rl::Value> results;
    std::string error;
    if (!rl::eval(p, code, &results, &error))
        return "error: " + error;
    std::string s;
    for (size_t k = 0; k < results.size(); k++)
        s += (k ? "," : "") + rl::to_json(results[k]);
    return s;
}
static std::string eval_error(rl::Plugin* p, const char* code)
{
    std::string error;
    if (!p || rl::eval(p, code, nullptr, &error))
        return "";
    return error;
}
/* Runs code inside rl::run, errors propagating (unlike luaL_dostring). */
static void lua_do(lua_State* L, const char* code)
{
    if (luaL_loadstring(L, code) != LUA_OK)
        lua_error(L);
    lua_call(L, 0, LUA_MULTRET);
}

/* Protected calls with tracebacks; plain data is copied between plugin
 * states; a function stays in its plugin and is called there through a
 * proxy; only the game thread may call in. */
static void values_and_calls()
{
    rl::Plugin* a = rl::console("@probe-a");
    rl::Plugin* b = rl::console("@probe-b");
    CHECK(a->state == rl::PLUGIN_LOADED && b->state == rl::PLUGIN_LOADED && a->console);
    CHECK(eval_json(a, "1 + 2, 'x', nil, true") == "3,\"x\",null,true");
    CHECK(eval_json(a, "{10, 20, 30}") == "[10,20,30]");
    CHECK(eval_json(a, "local t = {1, 2, n = {k = 'v'}} return t") == "{\"1\":1,\"2\":2,\"n\":{\"k\":\"v\"}}");
    CHECK(eval_json(a, "x = 5") == "");
    std::string e = eval_error(a, "local function f() error('deep') end f()");
    CHECK(contains(e, "deep") && contains(e, "stack traceback"));
    CHECK(contains(rl::log_lines().back().text, "deep") && rl::log_lines().back().level == rl::LOG_ERROR);
    for (int k = 0; k < 10; k++)
        eval_error(a, "error('again')");
    CHECK(a->state == rl::PLUGIN_LOADED); /* a console sandbox is never disabled */
    std::string error;
    rl::Value v;
    bool ok = rl::run(a, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        lua_do(L, "local t = {} t.self = t return t");
        v = rl::Value::from_lua(L, -1);
    }, &error);
    CHECK(!ok && contains(error, "contains itself"));
    ok = rl::run(a, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        lua_do(L, "return coroutine.create(print)");
        v = rl::Value::from_lua(L, -1);
    }, &error);
    CHECK(!ok && contains(error, "cannot pass a thread"));
    rl::Value fn;
    CHECK(rl::run(a, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        lua_do(L, "secret = 'only a' return function(x) return x * 2, secret end");
        fn = rl::Value::from_lua(L, -1);
    }));
    CHECK(fn.kind == rl::Value::FUNCTION && fn.fn->owner == "@probe-a");
    CHECK(rl::run(b, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        rl::push(L, fn);
        lua_setglobal(L, "from_a");
    }));
    CHECK(eval_json(b, "type(from_a), from_a(21)") == "\"function\",42,\"only a\"");
    CHECK(eval_json(b, "secret") == "null");
    std::vector<rl::Value> results;
    CHECK(rl::call_function(fn.fn, { rl::Value::integer(5) }, &results, &error));
    CHECK(results.size() == 2 && results[0].kind == rl::Value::INTEGER && results[0].i == 10);
    /* a table passed on is the receiver's own */
    rl::Value t = rl::Value::new_table();
    t.set("n", rl::Value::integer(1));
    CHECK(rl::run(b, "probe", rl::BUDGET_TEST, [&](lua_State* L) {
        rl::push(L, t);
        lua_setglobal(L, "copy");
    }));
    CHECK(eval_json(b, "copy.n = 2 return copy.n") == "2" && t.get("n")->i == 1);
    /* only the game thread may call in */
    std::thread other([&] { ok = rl::eval(a, "1", nullptr, &error); });
    other.join();
    CHECK(!ok && contains(error, "game thread"));
    CHECK(a->stats.calls > 0 && a->stats.faults >= 11);
}
```

and in `main`: `values_and_calls();` after `budgets();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `error: 'Value' is not a member of 'rl'`.

- [ ] **Step 3: Declare values and the runtime** — add to `pc/src/lua/rl_internal.h` before `} // namespace rl`:

```cpp
/* ---- lua_value.cpp: values crossing between plugin states ---- */
struct FunctionRef {
    std::string owner;       /* the plugin whose state holds the function */
    unsigned generation = 0; /* that plugin's load it belongs to */
    int ref = LUA_NOREF;     /* in that state's registry */
    ~FunctionRef();          /* frees the reference at the owner's next call */
};
struct Value {
    enum Kind { NIL, BOOLEAN, INTEGER, NUMBER, STRING, TABLE, FUNCTION };
    Kind kind = NIL;
    bool b = false;
    long long i = 0;
    double n = 0;
    std::string s;
    std::vector<std::pair<Value, Value>> table; /* the array part first, in order */
    std::shared_ptr<FunctionRef> fn;

    static Value str(const std::string& text);
    static Value number(double v);
    static Value integer(long long v);
    static Value boolean(bool v);
    static Value new_table();
    /* A copy of the value at idx: plain data; a function becomes a reference to it
     * in this state. Raises a Lua error for a table that contains itself, userdata,
     * threads, more than 32 levels or more than 100000 parts. */
    static Value from_lua(lua_State* L, int idx);
    const Value* get(const char* key) const; /* a table's string key */
    Value& set(const char* key, Value v);    /* adds or replaces a string key */
    void push_back(Value v);                 /* key #+1 */
    size_t length() const;                   /* keys 1..n */
};
/* Pushes a copy. A function of another state (or of an older load of this one)
 * becomes a proxy that runs it in its own plugin, under that plugin's budget. */
void push(lua_State* L, const Value& v);
/* Calls a function where it lives (a plugin's handler from the game, M1's renderers). */
bool call_function(const std::shared_ptr<FunctionRef>& fn, const std::vector<Value>& args, std::vector<Value>* results,
    std::string* error);
std::string to_json(const Value& v); /* tables with keys 1..n only: arrays; functions: "<function of id>" */
Value from_json(const prefab::Json& j);

/* ---- lua_runtime.cpp: states, protected calls, faults, the log ---- */
enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR };
struct LogLine {
    unsigned long long seq;
    int level;
    std::string plugin, text;
};
void log(int level, const std::string& plugin, const std::string& text); /* stderr "[lua] ..." and the ring */
const std::deque<LogLine>& log_lines(); /* the last 2000 */
unsigned long long log_last_seq();
const char* state_name(int state); /* "off", "loaded", "failed", "disabled" */
std::vector<std::unique_ptr<Plugin>>& all(); /* every plugin and console sandbox; never shrinks */
Plugin* find(const std::string& id);
Plugin* console(const std::string& name = "@console"); /* a sandbox without a folder, made on first use */
bool open_state(Plugin* p, std::string* error); /* a fresh state: sandbox and rood */
void close_state(Plugin* p);                    /* lua_close under the load budget (finalizers) */
/* Runs body inside a protected call into p's state, under a budget (the outermost
 * call into p sets it), with a traceback on error. On error: logs it, counts a fault
 * (a CALL or LOAD), puts the message (with traceback) in *error, returns false. */
bool run(Plugin* p, const char* what, BudgetKind kind, const std::function<void(lua_State*)>& body,
    std::string* error = nullptr);
/* Code as an expression ("return " + code) if it parses as one, else as statements;
 * its results copied (BUDGET_TEST: an eval never counts as a fault). */
bool eval(Plugin* p, const std::string& code, std::vector<Value>* results, std::string* error);
void fault(Plugin* p, const char* what, const std::string& message, bool killed, bool counted);
bool on_game_thread(); /* the first caller's thread is the game's */
int calls_in_progress();
/* Lua helpers: C functions wrapped so a C++ exception becomes a Lua error; a read-only view. */
void push_cfunction(lua_State* L, lua_CFunction f);
void set_cfunction(lua_State* L, int table, const char* name, lua_CFunction f);
void lock_table(lua_State* L); /* replaces the table on top with a read-only view of it */
/* Game-side modules (rood.settings) register themselves here (static constructors). */
using ApiOpener = void (*)(lua_State* L, Plugin* p);
using PluginHook = void (*)(Plugin* p);
using Hook = void (*)();
std::vector<ApiOpener>& api_openers(); /* add fields to rood */
std::vector<PluginHook>& unload_hooks(); /* drop what a plugin registered */
std::vector<Hook>& poll_hooks();         /* every tick and frame */
std::vector<Hook>& flush_hooks();        /* at exit: write what is unsaved */

/* ---- lua_sandbox.cpp, lua_api_core.cpp ---- */
void open_sandbox(lua_State* L, Plugin* p);
void open_rood(lua_State* L, Plugin* p);
```

- [ ] **Step 4: Write `pc/src/lua/lua_value.cpp`**

```cpp
// Values crossing between plugin states (pc/docs/Extensions.md,
// "Isolation"): plain data is copied (nil, booleans, numbers, strings and
// tables, without their metatables); a function stays in its plugin, and
// another plugin (or the game) calls it through a proxy that runs it there,
// under that plugin's budget. JSON for the RPC, storage and manifests.
#include "rl_internal.h"
#include "light_prefabs.h" /* prefab::Json */

#include <cmath>
#include <cstdio>
#include <exception>
#include <new>

namespace rl {
namespace {
const int MAX_DEPTH = 32;
const size_t MAX_PARTS = 100000;

int proxy_call(lua_State* L);

struct Copy {
    size_t parts = 0;
    std::vector<const void*> open; /* the tables being copied, outermost first */
};

void copy(lua_State* L, int idx, Value& out, int depth, Copy& c)
{
    idx = lua_absindex(L, idx);
    if (++c.parts > MAX_PARTS)
        luaL_error(L, "value too large to pass between plugins (more than %d parts)", (int)MAX_PARTS);
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        out = Value();
        return;
    case LUA_TBOOLEAN:
        out = Value::boolean(lua_toboolean(L, idx) != 0);
        return;
    case LUA_TNUMBER:
        out = lua_isinteger(L, idx) ? Value::integer((long long)lua_tointeger(L, idx))
                                    : Value::number((double)lua_tonumber(L, idx));
        return;
    case LUA_TSTRING: {
        size_t len;
        const char* s = lua_tolstring(L, idx, &len);
        out = Value::str(std::string(s, len));
        return;
    }
    case LUA_TFUNCTION: {
        out = Value();
        out.kind = Value::FUNCTION;
        if (lua_tocfunction(L, idx) == proxy_call) { /* a proxy: the function it stands for */
            lua_getupvalue(L, idx, 1);
            out.fn = *(std::shared_ptr<FunctionRef>*)lua_touserdata(L, -1);
            lua_pop(L, 1);
            return;
        }
        Plugin* p = plugin_of(L);
        auto fn = std::make_shared<FunctionRef>();
        fn->owner = p ? p->id() : std::string();
        fn->generation = p ? p->generation : 0;
        lua_pushvalue(L, idx);
        fn->ref = luaL_ref(L, LUA_REGISTRYINDEX);
        out.fn = fn;
        return;
    }
    case LUA_TTABLE:
        break;
    default:
        luaL_error(L, "cannot pass a %s value between plugins", luaL_typename(L, idx));
    }
    if (depth >= MAX_DEPTH)
        luaL_error(L, "table nested too deeply to pass between plugins (more than %d levels)", MAX_DEPTH);
    const void* self = lua_topointer(L, idx);
    for (const void* open : c.open)
        if (open == self)
            luaL_error(L, "cannot pass a table that contains itself");
    luaL_checkstack(L, 4, "copying a table");
    c.open.push_back(self);
    out = Value::new_table();
    lua_Integer n = (lua_Integer)lua_rawlen(L, idx);
    for (lua_Integer k = 1; k <= n; k++) {
        lua_rawgeti(L, idx, k);
        Value item;
        copy(L, -1, item, depth + 1, c);
        lua_pop(L, 1);
        out.table.emplace_back(Value::integer((long long)k), std::move(item));
    }
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        int kt = lua_type(L, -2);
        if (kt == LUA_TNUMBER && lua_isinteger(L, -2)) {
            lua_Integer k = lua_tointeger(L, -2);
            if (k >= 1 && k <= n) {
                lua_pop(L, 1);
                continue;
            }
        }
        if (kt != LUA_TSTRING && kt != LUA_TNUMBER && kt != LUA_TBOOLEAN)
            luaL_error(L, "cannot pass a table with %s keys between plugins", lua_typename(L, kt));
        Value key, item;
        copy(L, -2, key, depth + 1, c);
        copy(L, -1, item, depth + 1, c);
        lua_pop(L, 1);
        out.table.emplace_back(std::move(key), std::move(item));
    }
    c.open.pop_back();
}

int release_box(lua_State* L)
{
    ((std::shared_ptr<FunctionRef>*)lua_touserdata(L, 1))->~shared_ptr();
    return 0;
}

void push_ref(lua_State* L, const std::shared_ptr<FunctionRef>& fn)
{
    Plugin* p = plugin_of(L);
    if (p && fn && fn->owner == p->id() && fn->generation == p->generation && fn->ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, fn->ref);
        return;
    }
    void* box = lua_newuserdatauv(L, sizeof(std::shared_ptr<FunctionRef>), 0);
    new (box) std::shared_ptr<FunctionRef>(fn);
    if (luaL_newmetatable(L, "rood.function")) {
        lua_pushcfunction(L, release_box);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, "locked");
        lua_setfield(L, -2, "__metatable");
    }
    lua_setmetatable(L, -2);
    lua_pushcclosure(L, proxy_call, 1);
}

void push_value(lua_State* L, const Value& v, int depth)
{
    luaL_checkstack(L, 3, "passing a value between plugins");
    switch (v.kind) {
    case Value::NIL:
        lua_pushnil(L);
        return;
    case Value::BOOLEAN:
        lua_pushboolean(L, v.b);
        return;
    case Value::INTEGER:
        lua_pushinteger(L, (lua_Integer)v.i);
        return;
    case Value::NUMBER:
        lua_pushnumber(L, (lua_Number)v.n);
        return;
    case Value::STRING:
        lua_pushlstring(L, v.s.data(), v.s.size());
        return;
    case Value::FUNCTION:
        push_ref(L, v.fn);
        return;
    case Value::TABLE:
        break;
    }
    if (depth >= MAX_DEPTH)
        luaL_error(L, "table nested too deeply (more than %d levels)", MAX_DEPTH);
    size_t array = v.length();
    lua_createtable(L, (int)array, (int)(v.table.size() - array));
    for (const auto& kv : v.table) {
        if (kv.first.kind == Value::NIL || kv.second.kind == Value::NIL)
            continue;
        push_value(L, kv.first, depth + 1);
        push_value(L, kv.second, depth + 1);
        lua_rawset(L, -3);
    }
}

/* A function of another plugin, called from this one: arguments and results copied. */
int proxy_call(lua_State* L)
{
    static int depth;
    std::shared_ptr<FunctionRef> fn = *(std::shared_ptr<FunctionRef>*)lua_touserdata(L, lua_upvalueindex(1));
    if (depth >= 16)
        return luaL_error(L, "calls between plugins nested more than 16 deep");
    std::vector<Value> args, results;
    std::string error;
    bool ok;
    try {
        for (int k = 1, n = lua_gettop(L); k <= n; k++)
            args.push_back(Value::from_lua(L, k));
        ++depth;
        ok = call_function(fn, args, &results, &error);
        --depth;
    } catch (const std::exception& e) {
        return luaL_error(L, "internal error: %s", e.what());
    }
    if (!ok)
        return luaL_error(L, "%s", error.c_str());
    luaL_checkstack(L, (int)results.size() + 1, "results of a call between plugins");
    for (const Value& r : results)
        push_value(L, r, 0);
    return (int)results.size();
}

std::string key_text(const Value& k)
{
    char buffer[40];
    switch (k.kind) {
    case Value::STRING:
        return k.s;
    case Value::INTEGER:
        return std::to_string(k.i);
    case Value::BOOLEAN:
        return k.b ? "true" : "false";
    case Value::NUMBER:
        std::snprintf(buffer, sizeof buffer, "%.17g", k.n);
        return buffer;
    default:
        return "?";
    }
}
} // namespace

FunctionRef::~FunctionRef()
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL)
        return;
    Plugin* p = find(owner);
    if (p && p->L && p->generation == generation)
        p->pending_unrefs.push_back(ref);
}

Value Value::str(const std::string& text)
{
    Value v;
    v.kind = STRING;
    v.s = text;
    return v;
}
Value Value::number(double x)
{
    Value v;
    v.kind = NUMBER;
    v.n = x;
    return v;
}
Value Value::integer(long long x)
{
    Value v;
    v.kind = INTEGER;
    v.i = x;
    return v;
}
Value Value::boolean(bool x)
{
    Value v;
    v.kind = BOOLEAN;
    v.b = x;
    return v;
}
Value Value::new_table()
{
    Value v;
    v.kind = TABLE;
    return v;
}
Value Value::from_lua(lua_State* L, int idx)
{
    Value out;
    Copy c;
    copy(L, idx, out, 0, c);
    return out;
}
const Value* Value::get(const char* key) const
{
    for (const auto& kv : table)
        if (kv.first.kind == STRING && kv.first.s == key)
            return &kv.second;
    return nullptr;
}
Value& Value::set(const char* key, Value v)
{
    kind = TABLE;
    for (auto& kv : table)
        if (kv.first.kind == STRING && kv.first.s == key) {
            kv.second = std::move(v);
            return kv.second;
        }
    table.emplace_back(str(key), std::move(v));
    return table.back().second;
}
size_t Value::length() const
{
    size_t n = 0;
    for (const auto& kv : table)
        if (kv.first.kind == INTEGER && kv.first.i == (long long)n + 1)
            ++n;
    return n;
}
void Value::push_back(Value v)
{
    kind = TABLE;
    table.emplace_back(integer((long long)length() + 1), std::move(v));
}

void push(lua_State* L, const Value& v) { push_value(L, v, 0); }

bool call_function(const std::shared_ptr<FunctionRef>& fn, const std::vector<Value>& args, std::vector<Value>* results,
    std::string* error)
{
    Plugin* owner = fn ? find(fn->owner) : nullptr;
    if (!owner || !owner->L || owner->state != PLUGIN_LOADED || owner->generation != fn->generation) {
        if (error)
            *error = "the function's plugin '" + (fn ? fn->owner : std::string("?")) + "' was unloaded or reloaded";
        return false;
    }
    return run(owner, "call from another plugin", BUDGET_CALL, [&](lua_State* L) {
        int base = lua_gettop(L);
        luaL_checkstack(L, (int)args.size() + 2, "arguments of a call between plugins");
        lua_rawgeti(L, LUA_REGISTRYINDEX, fn->ref);
        for (const Value& a : args)
            push_value(L, a, 0);
        lua_call(L, (int)args.size(), LUA_MULTRET);
        if (results)
            for (int k = base + 1; k <= lua_gettop(L); k++)
                results->push_back(Value::from_lua(L, k));
    }, error);
}

std::string to_json(const Value& v)
{
    char buffer[40];
    switch (v.kind) {
    case Value::NIL:
        return "null";
    case Value::BOOLEAN:
        return v.b ? "true" : "false";
    case Value::INTEGER:
        return std::to_string(v.i);
    case Value::NUMBER:
        if (!std::isfinite(v.n))
            return "null";
        std::snprintf(buffer, sizeof buffer, "%.17g", v.n);
        return buffer;
    case Value::STRING:
        return prefab::json_quote(v.s);
    case Value::FUNCTION:
        return prefab::json_quote("<function of " + (v.fn ? v.fn->owner : std::string("?")) + ">");
    case Value::TABLE:
        break;
    }
    size_t n = v.length();
    bool array = n > 0 && n == v.table.size();
    std::string s(1, array ? '[' : '{');
    bool first = true;
    for (const auto& kv : v.table) {
        if (!array && kv.second.kind == Value::NIL)
            continue;
        if (!first)
            s += ',';
        first = false;
        if (!array)
            s += prefab::json_quote(key_text(kv.first)) + ":";
        s += to_json(kv.second);
    }
    s += array ? ']' : '}';
    return s;
}

Value from_json(const prefab::Json& j)
{
    switch (j.type) {
    case prefab::Json::Bool:
        return Value::boolean(j.boolean);
    case prefab::Json::Number:
        if (std::floor(j.number) == j.number && std::fabs(j.number) < 9007199254740992.0)
            return Value::integer((long long)j.number);
        return Value::number(j.number);
    case prefab::Json::String:
        return Value::str(j.string);
    case prefab::Json::Array: {
        Value t = Value::new_table();
        for (const prefab::Json& item : j.items)
            t.push_back(from_json(item));
        return t;
    }
    case prefab::Json::Object: {
        Value t = Value::new_table();
        for (const auto& m : j.members)
            t.set(m.first.c_str(), from_json(m.second));
        return t;
    }
    default:
        return Value();
    }
}

} // namespace rl
```

- [ ] **Step 5: Write `pc/src/lua/lua_runtime.cpp`**

```cpp
// The extension system's runtime (pc/docs/Extensions.md): each plugin's own
// Lua state, protected calls with budgets and tracebacks, faults (a plugin
// that keeps failing is disabled at the next safe point, lua_plugins.cpp
// settle), the log the console and the RPC read, console sandboxes and
// eval, and the Lua helpers the API files share. Everything runs on the
// game thread (pc/docs/Extensions.md "Where Lua runs"); run() refuses any
// other thread.
#include "rl_internal.h"

#include <cstdio>
#include <exception>
#ifndef _WIN32
#include <pthread.h>
#endif

namespace rl {
namespace {
int in_progress; /* calls into any plugin */
unsigned long long next_seq = 1;
unsigned long long owner_thread;
bool owner_known;

unsigned long long thread_id()
{
#ifdef _WIN32
    return (unsigned long long)GetCurrentThreadId();
#else
    return (unsigned long long)pthread_self();
#endif
}

std::deque<LogLine>& log_buffer()
{
    static auto* lines = new std::deque<LogLine>();
    return *lines;
}

struct Job {
    const std::function<void(lua_State*)>* body;
};

int trampoline(lua_State* L)
{
    Job* job = (Job*)lua_touserdata(L, 1);
    lua_settop(L, 0);
    Plugin* p = plugin_of(L);
    for (int ref : p->pending_unrefs)
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    p->pending_unrefs.clear();
    try {
        (*job->body)(L);
    } catch (const std::exception& e) {
        return luaL_error(L, "internal error: %s", e.what());
    }
    return 0;
}

int traceback(lua_State* L)
{
    plugin_of(L)->mem.grace = 256 * 1024; /* room for the text when the cap is what failed */
    const char* message = lua_tostring(L, 1);
    if (!message)
        message = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    luaL_traceback(L, L, message, 1);
    return 1;
}

int guarded_call(lua_State* L)
{
    lua_CFunction f = lua_tocfunction(L, lua_upvalueindex(1));
    try {
        return f(L);
    } catch (const std::exception& e) {
        return luaL_error(L, "internal error: %s", e.what());
    }
}

int locked_newindex(lua_State* L)
{
    return luaL_error(L, "rood is read-only (cannot set '%s')", luaL_tolstring(L, 2, nullptr));
}
int raw_next(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 2);
    if (lua_next(L, 1))
        return 2;
    lua_pushnil(L);
    return 1;
}
int locked_pairs(lua_State* L)
{
    lua_pushcfunction(L, raw_next);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushnil(L);
    return 3;
}

void warn_to_log(void* ud, const char* message, int tocont)
{
    static std::string pending;
    Plugin* p = (Plugin*)ud;
    if (pending.empty() && !tocont && message[0] == '@')
        return; /* a control message ("@on") */
    pending += message;
    if (!tocont) {
        log(LOG_WARN, p->id(), pending);
        pending.clear();
    }
}
} // namespace

void log(int level, const std::string& plugin, const std::string& text)
{
    static const char* const levels[] = { "info", "warn", "error" };
    std::fprintf(stderr, "[lua] %s%s%s: %s\n", plugin.c_str(), plugin.empty() ? "" : " ", levels[level], text.c_str());
    log_buffer().push_back({ next_seq++, level, plugin, text });
    while (log_buffer().size() > 2000)
        log_buffer().pop_front();
}
const std::deque<LogLine>& log_lines() { return log_buffer(); }
unsigned long long log_last_seq() { return next_seq - 1; }

const char* state_name(int state)
{
    switch (state) {
    case PLUGIN_LOADED:
        return "loaded";
    case PLUGIN_FAILED:
        return "failed";
    case PLUGIN_DISABLED:
        return "disabled";
    default:
        return "off";
    }
}

std::vector<std::unique_ptr<Plugin>>& all()
{
    static auto* list = new std::vector<std::unique_ptr<Plugin>>();
    return *list;
}
Plugin* find(const std::string& id)
{
    for (auto& p : all())
        if (p->id() == id)
            return p.get();
    return nullptr;
}

std::vector<ApiOpener>& api_openers()
{
    static auto* list = new std::vector<ApiOpener>();
    return *list;
}
std::vector<PluginHook>& unload_hooks()
{
    static auto* list = new std::vector<PluginHook>();
    return *list;
}
std::vector<Hook>& poll_hooks()
{
    static auto* list = new std::vector<Hook>();
    return *list;
}
std::vector<Hook>& flush_hooks()
{
    static auto* list = new std::vector<Hook>();
    return *list;
}

bool on_game_thread()
{
    if (!owner_known) {
        owner_thread = thread_id();
        owner_known = true;
    }
    return thread_id() == owner_thread;
}
int calls_in_progress() { return in_progress; }

void push_cfunction(lua_State* L, lua_CFunction f)
{
    lua_pushcfunction(L, f);
    lua_pushcclosure(L, guarded_call, 1);
}
void set_cfunction(lua_State* L, int table, const char* name, lua_CFunction f)
{
    table = lua_absindex(L, table);
    push_cfunction(L, f);
    lua_setfield(L, table, name);
}
void lock_table(lua_State* L)
{
    lua_newtable(L);          /* the view */
    lua_createtable(L, 0, 4); /* its metatable */
    lua_pushvalue(L, -3);
    lua_setfield(L, -2, "__index");
    lua_pushvalue(L, -3);
    lua_pushcclosure(L, locked_pairs, 1);
    lua_setfield(L, -2, "__pairs");
    lua_pushcfunction(L, locked_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_remove(L, -2);
}

bool open_state(Plugin* p, std::string* error)
{
    p->mem = Memory();
    p->mem.cap = (size_t)(config.memory_mb * 1048576.0);
    p->budget = Budget();
    p->pending_unrefs.clear();
    p->L = new_state(p);
    if (!p->L) {
        if (error)
            *error = "cannot create a Lua state (is the memory cap too small?)";
        return false;
    }
    lua_setwarnf(p->L, warn_to_log, p);
    bool ok = run(p, "open", BUDGET_LOAD, [p](lua_State* L) {
        open_sandbox(L, p);
        open_rood(L, p);
    }, error);
    if (!ok)
        close_state(p);
    return ok;
}

void close_state(Plugin* p)
{
    if (!p->L)
        return;
    begin_budget(p, BUDGET_LOAD); /* finalizers run under a budget too */
    ++in_progress;
    lua_close(p->L);
    --in_progress;
    end_budget(p);
    p->L = nullptr;
    p->pending_unrefs.clear();
}

bool run(Plugin* p, const char* what, BudgetKind kind, const std::function<void(lua_State*)>& body, std::string* error)
{
    if (!p || !p->L) {
        if (error)
            *error = "not loaded";
        return false;
    }
    if (!on_game_thread()) {
        log(LOG_ERROR, p->id(), std::string(what) + ": called off the game thread; refused");
        if (error)
            *error = "called off the game thread";
        return false;
    }
    lua_State* L = p->L;
    if (!lua_checkstack(L, 4)) {
        if (error)
            *error = "Lua stack exhausted";
        return false;
    }
    bool top = p->depth == 0;
    if (top)
        begin_budget(p, kind);
    ++p->depth;
    ++in_progress;
    Job job{ &body };
    int base = lua_gettop(L);
    lua_pushcfunction(L, traceback);
    lua_pushcfunction(L, trampoline);
    lua_pushlightuserdata(L, &job);
    int status = lua_pcall(L, 1, 0, base + 1);
    std::string message;
    if (status != LUA_OK) {
        const char* text = lua_tostring(L, -1);
        message = text ? text : "(error without a message)";
    }
    lua_settop(L, base);
    --p->depth;
    --in_progress;
    bool killed = p->budget.killed;
    if (top) {
        double ms = end_budget(p);
        p->stats.calls++;
        p->stats.total_ms += ms;
        if (ms > p->stats.max_ms)
            p->stats.max_ms = ms;
    }
    if (status == LUA_OK)
        return true;
    fault(p, what, message, killed, kind != BUDGET_TEST);
    if (error)
        *error = message;
    return false;
}

void fault(Plugin* p, const char* what, const std::string& message, bool killed, bool counted)
{
    p->stats.faults++;
    if (killed)
        p->stats.kills++;
    log(LOG_ERROR, p->id(), std::string(what) + ": " + message);
    if (!counted || p->console || p->state != PLUGIN_LOADED || !p->disable_reason.empty())
        return;
    double now = now_seconds();
    p->fault_times.push_back(now);
    while (!p->fault_times.empty() && now - p->fault_times.front() > config.fault_window_s)
        p->fault_times.erase(p->fault_times.begin());
    if ((int)p->fault_times.size() >= config.fault_limit) {
        char window[32];
        std::snprintf(window, sizeof window, "%g", config.fault_window_s);
        p->disable_reason = std::to_string(p->fault_times.size()) + " faults within " + window + " s (last: "
            + message.substr(0, message.find('\n')) + ")";
    }
}

bool eval(Plugin* p, const std::string& code, std::vector<Value>* results, std::string* error)
{
    return run(p, "eval", BUDGET_TEST, [&](lua_State* L) {
        std::string expression = "return " + code;
        if (luaL_loadbufferx(L, expression.data(), expression.size(), "=eval", "t") != LUA_OK) {
            lua_pop(L, 1);
            if (luaL_loadbufferx(L, code.data(), code.size(), "=eval", "t") != LUA_OK)
                lua_error(L);
        }
        lua_call(L, 0, LUA_MULTRET);
        if (results)
            for (int k = 1; k <= lua_gettop(L); k++)
                results->push_back(Value::from_lua(L, k));
    }, error);
}

Plugin* console(const std::string& name)
{
    if (Plugin* p = find(name))
        return p;
    auto made = std::make_unique<Plugin>();
    Plugin* p = made.get();
    p->manifest.id = p->manifest.name = name;
    p->console = true;
    all().push_back(std::move(made));
    ++p->generation;
    std::string error;
    if (open_state(p, &error)) {
        p->state = PLUGIN_LOADED;
        p->status = "loaded";
    } else {
        p->state = PLUGIN_FAILED;
        p->status = error;
    }
    return p;
}

} // namespace rl
```

- [ ] **Step 6: Write the first `pc/src/lua/lua_sandbox.cpp`** (Task 7 replaces it)

```cpp
// A plugin's sandbox (pc/docs/Extensions.md, "Sandbox"). First version: the
// safe standard libraries only; Task 7 of the M0 plan strips what reaches
// outside.
#include "rl_internal.h"

namespace rl {
void open_sandbox(lua_State* L, Plugin*)
{
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base },
        { LUA_COLIBNAME, luaopen_coroutine },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    for (const luaL_Reg& lib : libs) {
        luaL_requiref(L, lib.name, lib.func, 1);
        lua_pop(L, 1);
    }
}
} // namespace rl
```

- [ ] **Step 7: Write the first `pc/src/lua/lua_api_core.cpp`** (Task 8 replaces it)

```cpp
// The rood table (pc/docs/Extensions.md, "API"). First version: rood.version.
#include "rl_internal.h"

namespace rl {
void open_rood(lua_State* L, Plugin*)
{
    lua_newtable(L);
    lua_pushliteral(L, RL_VERSION);
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "rood");
}
} // namespace rl
```

- [ ] **Step 8: Add the files to the probe** — in `pc/CMakeLists.txt` change the `ROOD_LUA_RUNTIME` line to:

```cmake
set(ROOD_LUA_RUNTIME src/lua/lua_limits.cpp src/lua/lua_value.cpp src/lua/lua_runtime.cpp src/lua/lua_sandbox.cpp
    src/lua/lua_api_core.cpp src/light_prefabs.cpp)
```

- [ ] **Step 9: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -E "error|warning" | head
pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: no compiler output from the grep; `lua_probe: ok` (the `[lua] ...` lines on stderr are the expected error logs).

- [ ] **Step 10: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua runtime: protected calls, values between plugin states, the log

rl::run calls into a plugin's own state under its budget, with a
traceback, and counts faults; values are copied between states and a
function crosses as a proxy that runs in its own plugin. Only the game
thread may call in.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: The sandbox

**Files:**
- Modify: `pc/src/lua/lua_sandbox.cpp` (replace), `pc/src/lua/rl_internal.h`, `pc/tests/lua_probe.cpp`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* What reaches outside the sandbox is gone; load takes text only; require
 * loads the plugin's own files; print goes to the log. */
static void sandbox()
{
    rl::Plugin* s = rl::console("@probe-sandbox");
    CHECK(eval_json(s, "io, os, debug, package, dofile, loadfile, warn, string.dump")
        == "null,null,null,null,null,null,null,null");
    CHECK(contains(eval_error(s, "load(function() return nil end)"), "text chunks"));
    CHECK(contains(eval_json(s, "select(2, load('\\27LuaT\\0', '=bin'))"), "binary chunk"));
    CHECK(contains(eval_json(s, "select(2, load('return 1', '=c', 'b'))"), "text chunks"));
    CHECK(eval_json(s, "load('return x', '=c', 't', {x = 5})()") == "5");
    CHECK(eval_json(s, "getmetatable('')") == "\"locked\"");
    CHECK(contains(eval_error(s, "collectgarbage('stop')"), "not allowed"));
    CHECK(contains(eval_json(s, "collectgarbage('count') > 0"), "true"));
    CHECK(contains(eval_error(s, "require('os')"), "no folder"));
    CHECK(contains(eval_error(s, "require('../x')"), "bad module name"));
    CHECK(eval_json(s, "print('hello from print')") == "");
    CHECK(rl::log_lines().back().text == "hello from print" && rl::log_lines().back().plugin == "@probe-sandbox");
}
```

and in `main`: `sandbox();` after `values_and_calls();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `FAIL ... "null,null,null,null,null,null,null,null"`-check and others; `lua_probe: N failure(s)` with N ≥ 6.

- [ ] **Step 3: Declare the file helper the sandbox needs** — the sandbox's `require` reads files: add to `pc/src/lua/rl_internal.h` before `} // namespace rl` (the definition comes in Task 12; until then a minimal one lives in `lua_sandbox.cpp`, see Step 4):

```cpp
/* ---- lua_files.cpp ---- */
/* The whole file; false (and why: "not found", "larger than N KB") past `limit` bytes. */
bool read_file(const std::string& path, std::string* out, size_t limit, std::string* why);
```

- [ ] **Step 4: Replace `pc/src/lua/lua_sandbox.cpp`**

```cpp
// A plugin's sandbox (pc/docs/Extensions.md, "Sandbox"): its own Lua state
// with the safe standard libraries only (base, coroutine, table, string,
// math, utf8; io, os, package and debug are not even linked); no dofile,
// loadfile or warn; no string.dump; load takes text only; require loads the
// plugin's own .lua files; print goes to the log; collectgarbage only
// measures and collects; getmetatable("") gives "locked". What a plugin may
// do beyond that comes through rood (lua_api_core.cpp) and, when granted,
// its capabilities (lua_caps.cpp).
#include "rl_internal.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace rl {
namespace {
const size_t MAX_CHUNK = 1u << 20; /* load(): text up to 1 MB */
char loading_marker;

int sandbox_print(lua_State* L)
{
    std::string text;
    for (int k = 1, n = lua_gettop(L); k <= n; k++) {
        size_t len;
        const char* s = luaL_tolstring(L, k, &len);
        if (k > 1)
            text += '\t';
        text.append(s, len);
        lua_pop(L, 1);
    }
    log(LOG_INFO, plugin_of(L)->id(), text);
    return 0;
}

int sandbox_load(lua_State* L)
{
    if (lua_type(L, 1) != LUA_TSTRING)
        return luaL_error(L, "load: only text chunks (a string) can be loaded here");
    size_t len;
    const char* text = lua_tolstring(L, 1, &len);
    const char* name = luaL_optstring(L, 2, "=(load)");
    const char* mode = luaL_optstring(L, 3, "t");
    bool env = !lua_isnoneornil(L, 4);
    if (env)
        luaL_checktype(L, 4, LUA_TTABLE);
    if (std::strcmp(mode, "t") != 0) {
        luaL_pushfail(L);
        lua_pushliteral(L, "load: only text chunks (mode \"t\") can be loaded here");
        return 2;
    }
    if (len > MAX_CHUNK) {
        luaL_pushfail(L);
        lua_pushliteral(L, "load: chunk larger than 1 MB");
        return 2;
    }
    if (luaL_loadbufferx(L, text, len, name, "t") != LUA_OK) {
        luaL_pushfail(L);
        lua_insert(L, -2);
        return 2;
    }
    if (env) {
        lua_pushvalue(L, 4);
        if (!lua_setupvalue(L, -2, 1))
            lua_pop(L, 1);
    }
    return 1;
}

bool module_name_ok(const char* s)
{
    bool start = true;
    if (!*s)
        return false;
    for (; *s; s++) {
        if (*s == '.') {
            if (start)
                return false;
            start = true;
            continue;
        }
        if (!(std::isalnum((unsigned char)*s) || *s == '_'))
            return false;
        start = false;
    }
    return !start;
}

int sandbox_require(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    Plugin* p = plugin_of(L);
    if (!module_name_ok(name))
        return luaL_error(L, "require: bad module name '%s' (letters, digits, _ and dots)", name);
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "rood.loaded"); /* 2 */
    lua_getfield(L, 2, name);                          /* 3 */
    if (lua_toboolean(L, 3)) {
        if (lua_islightuserdata(L, 3))
            return luaL_error(L, "require: module '%s' is loading (a cycle) or failed to load", name);
        return 1;
    }
    lua_pop(L, 1);
    if (p->dir.empty())
        return luaL_error(L, "require: module '%s' not found (this sandbox has no folder)", name);
    std::string relative = name;
    for (char& c : relative)
        if (c == '.')
            c = '/';
    relative += ".lua";
    std::string code, why;
    if (!read_file(p->dir + "/" + relative, &code, RL_MAX_FILE, &why))
        return luaL_error(L, "require: module '%s' not found in plugin %s (%s: %s)", name, p->id().c_str(),
            relative.c_str(), why.c_str());
    lua_pushlightuserdata(L, &loading_marker);
    lua_setfield(L, 2, name);
    std::string chunk = "@" + p->id() + "/" + relative;
    if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK) /* 3 */
        return lua_error(L);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1); /* 3: the module */
    if (lua_isnil(L, 3)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, 3);
    lua_setfield(L, 2, name);
    return 1;
}

int sandbox_collectgarbage(lua_State* L)
{
    const char* option = luaL_optstring(L, 1, "collect");
    if (!std::strcmp(option, "count")) {
        int kb = lua_gc(L, LUA_GCCOUNT), bytes = lua_gc(L, LUA_GCCOUNTB);
        lua_pushnumber(L, (lua_Number)kb + (lua_Number)bytes / 1024);
        return 1;
    }
    if (!std::strcmp(option, "collect")) {
        lua_gc(L, LUA_GCCOLLECT);
        lua_pushinteger(L, 0);
        return 1;
    }
    if (!std::strcmp(option, "step")) {
        lua_pushboolean(L, lua_gc(L, LUA_GCSTEP, (int)luaL_optinteger(L, 2, 0)));
        return 1;
    }
    if (!std::strcmp(option, "isrunning")) {
        lua_pushboolean(L, lua_gc(L, LUA_GCISRUNNING));
        return 1;
    }
    return luaL_error(L, "collectgarbage: '%s' is not allowed in a plugin (count, collect, step, isrunning)", option);
}
} // namespace

void open_sandbox(lua_State* L, Plugin*)
{
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base },
        { LUA_COLIBNAME, luaopen_coroutine },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    for (const luaL_Reg& lib : libs) {
        luaL_requiref(L, lib.name, lib.func, 1);
        lua_pop(L, 1);
    }
    for (const char* name : { "dofile", "loadfile", "warn" }) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
    lua_pushliteral(L, ""); /* getmetatable("") gives "locked", not the string library's metatable */
    lua_getmetatable(L, -1);
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 2);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "rood.loaded");
    static const luaL_Reg replaced[] = {
        { "print", sandbox_print },
        { "load", sandbox_load },
        { "require", sandbox_require },
        { "collectgarbage", sandbox_collectgarbage },
    };
    for (const luaL_Reg& f : replaced) {
        push_cfunction(L, f.func);
        lua_setglobal(L, f.name);
    }
}

} // namespace rl
```

and, until Task 12 adds `lua_files.cpp`, append to `pc/src/lua/lua_sandbox.cpp` a minimal `read_file` that Task 12 moves (Task 12 Step 4 deletes this block):

```cpp
/* Moved to lua_files.cpp by Task 12. */
namespace rl {
bool read_file(const std::string& path, std::string* out, size_t limit, std::string* why)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (why)
            *why = "not found";
        return false;
    }
    std::string text;
    char buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
        if (text.size() + n > limit) {
            std::fclose(f);
            if (why)
                *why = "larger than " + std::to_string(limit / 1024) + " KB";
            return false;
        }
        text.append(buffer, n);
    }
    std::fclose(f);
    *out = std::move(text);
    return true;
}
} // namespace rl
```

- [ ] **Step 5: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `lua_probe: ok`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/lua/lua_sandbox.cpp pc/src/lua/rl_internal.h pc/tests/lua_probe.cpp
git commit -m "$(cat <<'EOF'
Lua sandbox: safe libraries only, text-only load, own-folder require

No io, os, debug, package, dofile, loadfile, warn or string.dump; print
goes to the log; collectgarbage measures and collects; the string
metatable cannot be fetched.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: The `rood` core table

**Files:**
- Modify: `pc/src/lua/lua_api_core.cpp` (replace), `pc/tests/lua_probe.cpp`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* rood: log levels, version, api_version, the plugin's own manifest; read-only. */
static void core_api()
{
    rl::Plugin* c = rl::console("@probe-api");
    CHECK(eval_json(c, "rood.version, rood.api_version, rood.plugin.id, rood.plugin.elevated")
        == "\"0.1.0\",1,\"@probe-api\",false");
    CHECK(eval_json(c, "rood.warn('careful', 1)") == "");
    CHECK(rl::log_lines().back().level == rl::LOG_WARN && rl::log_lines().back().text == "careful 1");
    CHECK(eval_json(c, "rood.error('bad')") == "" && rl::log_lines().back().level == rl::LOG_ERROR);
    CHECK(contains(eval_error(c, "rood.log = nil"), "read-only"));
    CHECK(contains(eval_error(c, "rood.plugin.id = 'x'"), "read-only"));
    CHECK(eval_json(c, "getmetatable(rood)") == "\"locked\"");
    CHECK(eval_json(c, "local n = 0 for k in pairs(rood) do n = n + 1 end return n >= 6") == "true");
}
```

and in `main`: `core_api();` after `sandbox();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `FAIL` lines for `core_api` (`rood.api_version` is nil); `lua_probe: N failure(s)`.

- [ ] **Step 3: Replace `pc/src/lua/lua_api_core.cpp`**

```cpp
// The rood table every plugin gets (pc/docs/Extensions.md, "API"): log,
// warn, error (to the log and the console), version, api_version, plugin
// (its own manifest, folders and capabilities), then the modules: events
// and timers, registry, storage, tests, capabilities when granted, and what
// game-side modules add (rood.settings). Read-only views throughout.
#include "rl_internal.h"

#include <algorithm>

namespace rl {
namespace {
std::string joined(lua_State* L)
{
    std::string text;
    for (int k = 1, n = lua_gettop(L); k <= n; k++) {
        size_t len;
        const char* s = luaL_tolstring(L, k, &len);
        if (k > 1)
            text += ' ';
        text.append(s, len);
        lua_pop(L, 1);
    }
    return text;
}
int api_log(lua_State* L)
{
    log(LOG_INFO, plugin_of(L)->id(), joined(L));
    return 0;
}
int api_warn(lua_State* L)
{
    log(LOG_WARN, plugin_of(L)->id(), joined(L));
    return 0;
}
int api_error(lua_State* L)
{
    log(LOG_ERROR, plugin_of(L)->id(), joined(L));
    return 0;
}

void set_string(lua_State* L, const char* key, const std::string& value)
{
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}
void set_strings(lua_State* L, const char* key, const std::vector<std::string>& list)
{
    lua_createtable(L, (int)list.size(), 0);
    for (size_t k = 0; k < list.size(); k++) {
        lua_pushstring(L, list[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    lua_setfield(L, -2, key);
}

void push_plugin_info(lua_State* L, Plugin* p)
{
    const Manifest& m = p->manifest;
    lua_createtable(L, 0, 16);
    set_string(L, "id", m.id);
    set_string(L, "name", m.name);
    set_string(L, "version", m.version);
    set_string(L, "description", m.description);
    set_string(L, "author", m.author);
    set_string(L, "dir", p->dir);
    set_string(L, "data_dir", p->data_dir);
    lua_pushinteger(L, m.api_version);
    lua_setfield(L, -2, "api_version");
    lua_pushboolean(L, p->shipped);
    lua_setfield(L, -2, "shipped");
    lua_pushboolean(L, !p->granted.empty());
    lua_setfield(L, -2, "elevated");
    set_strings(L, "capabilities", m.capabilities);
    set_strings(L, "granted", p->granted);
    std::vector<std::string> missing;
    for (const std::string& cap : m.capabilities)
        if (std::find(p->granted.begin(), p->granted.end(), cap) == p->granted.end())
            missing.push_back(cap);
    set_strings(L, "missing", missing);
    lock_table(L);
}
} // namespace

void open_rood(lua_State* L, Plugin* p)
{
    lua_newtable(L);
    set_cfunction(L, -1, "log", api_log);
    set_cfunction(L, -1, "warn", api_warn);
    set_cfunction(L, -1, "error", api_error);
    lua_pushliteral(L, RL_VERSION);
    lua_setfield(L, -2, "version");
    lua_pushinteger(L, RL_API_VERSION);
    lua_setfield(L, -2, "api_version");
    push_plugin_info(L, p);
    lua_setfield(L, -2, "plugin");
    for (ApiOpener open : api_openers())
        open(L, p);
    lock_table(L);
    lua_setglobal(L, "rood");
}

} // namespace rl
```

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `lua_probe: ok`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/lua/lua_api_core.cpp pc/tests/lua_probe.cpp
git commit -m "$(cat <<'EOF'
Lua: the rood table (log, warn, error, version, api_version, plugin)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 9: Events and the tick/frame hooks

**Files:**
- Create: `pc/src/lua/lua_events.cpp`
- Modify: `pc/src/lua/rl_internal.h`, `pc/src/lua/lua_api_core.cpp`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* rood.on/off/emit: each plugin gets its own copy of a payload; a handler's
 * error is its own plugin's fault, with a traceback; plugins emit only
 * namespaced events; the core's tick reaches every plugin. */
static void events()
{
    rl::Plugin* a = rl::console("@probe-ev-a");
    rl::Plugin* b = rl::console("@probe-ev-b");
    CHECK(eval_json(b, "got = {} h = rood.on('probe/ping', function(v) got[#got + 1] = v end) return h > 0") == "true");
    CHECK(eval_json(a, "payload = { n = 1, list = { 1, 2 } } return rood.emit('probe/ping', payload)") == "1");
    CHECK(eval_json(b, "got[1].n, got[1].list[2]") == "1,2");
    CHECK(eval_json(b, "got[1].list[1] = 99 return got[1].list[1]") == "99");
    CHECK(eval_json(a, "payload.list[1]") == "1");
    CHECK(contains(eval_error(a, "rood.emit('tick', {})"), "namespaced"));
    CHECK(contains(eval_error(a, "rood.on('bad name!', print)"), "bad event name"));
    CHECK(eval_json(b, "rood.on('probe/boom', function() error('handler failed') end) return true") == "true");
    unsigned long long faults = b->stats.faults;
    CHECK(eval_json(a, "rood.emit('probe/boom')") == "1");
    CHECK(b->stats.faults == faults + 1);
    CHECK(contains(rl::log_lines().back().text, "handler failed") && contains(rl::log_lines().back().text, "stack traceback"));
    CHECK(eval_json(b, "rood.off(h), rood.off(h)") == "true,false");
    CHECK(eval_json(a, "rood.emit('probe/ping', 2)") == "0");
    CHECK(eval_json(a, "ticks = 0 rood.on('tick', function(t) ticks = ticks + 1 last = t.vsync end) return true") == "true");
    rl::tick_events(41);
    rl::tick_events(42);
    CHECK(eval_json(a, "ticks, last") == "2,42");
    CHECK(rl::handler_count("@probe-ev-a") == 1 && rl::handler_count("@probe-ev-b") == 1);
}
```

and in `main`: `events();` after `core_api();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `error: 'tick_events' is not a member of 'rl'`.

- [ ] **Step 3: Declare the events** — add to `pc/src/lua/rl_internal.h` before `} // namespace rl`:

```cpp
/* ---- lua_events.cpp: events, the core's hooks, timers ---- */
void open_events(lua_State* L, Plugin* p); /* rood.on/off/emit, rood.timer, rood.clock */
/* Calls every handler of `event`, each in its own plugin with its own copy of payload;
 * returns how many were called. */
int emit(const std::string& event, const Value& payload);
int emit_to(Plugin* p, const std::string& event, const Value& payload); /* p's handlers only ("unload") */
void tick_events(unsigned vsync);  /* game time: "tick", game-time timers */
void frame_events(double dt);      /* real time: "frame", real-time timers */
void drop_events(const std::string& plugin); /* its handlers and timers */
size_t handler_count(const std::string& plugin);
size_t timer_count(const std::string& plugin);
double game_clock(); /* seconds of game time (ticks / 59.94) */
double real_clock(); /* seconds since the first call */
```

- [ ] **Step 4: Write `pc/src/lua/lua_events.cpp`**

```cpp
// The event bus and the core's hooks (pc/docs/Extensions.md, "Events and
// timers"): rood.on / off / emit; the core's events, un-namespaced: tick
// (every game vsync while the game runs: {vsync, game_time}), frame (every
// presented image, real time: {dt, real_time}), plugin.loaded and
// plugin.unloaded ({id}), registry.changed ({kind, id, action}), and unload
// (to the plugin itself, before it goes). Plugins emit namespaced events
// only ("my.plugin/changed"). Handlers run one by one, each under its own
// plugin's budget, with its own copy of the payload.
#include "rl_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace rl {
namespace {
struct Handler {
    unsigned long long id;
    std::string plugin;
    unsigned generation;
    int ref;
    std::string event;
};
std::vector<Handler>& handlers()
{
    static auto* list = new std::vector<Handler>();
    return *list;
}
unsigned long long next_id = 1;
int emit_depth;
double game_time;

bool event_name_ok(const char* s)
{
    size_t n = std::strlen(s);
    if (n == 0 || n > 64)
        return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!(std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '/' || c == ':'))
            return false;
    }
    return true;
}

bool callable(const std::string& plugin, unsigned generation, Plugin** out)
{
    Plugin* p = find(plugin);
    if (!p || !p->L || p->state != PLUGIN_LOADED || p->generation != generation || !p->disable_reason.empty())
        return false;
    *out = p;
    return true;
}

bool registered(unsigned long long id)
{
    for (const Handler& h : handlers())
        if (h.id == id)
            return true;
    return false;
}

int call_handlers(const std::vector<Handler>& matching, const std::string& event, const Value& payload)
{
    int called = 0;
    ++emit_depth;
    for (const Handler& h : matching) {
        Plugin* p;
        if (!registered(h.id) || !callable(h.plugin, h.generation, &p))
            continue;
        ++called;
        run(p, ("on " + event).c_str(), BUDGET_CALL, [&](lua_State* L) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, h.ref);
            push(L, payload);
            lua_call(L, 1, 0);
        });
    }
    --emit_depth;
    return called;
}

int api_on(lua_State* L)
{
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (!event_name_ok(event))
        return luaL_error(L, "rood.on: bad event name '%s' (letters, digits and _ . - / :)", event);
    Plugin* p = plugin_of(L);
    lua_settop(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    handlers().push_back({ next_id, p->id(), p->generation, ref, event });
    lua_pushinteger(L, (lua_Integer)next_id++);
    return 1;
}

int api_off(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    Plugin* p = plugin_of(L);
    std::vector<Handler>& list = handlers();
    for (auto it = list.begin(); it != list.end(); ++it)
        if ((lua_Integer)it->id == id && it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            list.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}

int api_emit(lua_State* L)
{
    const char* event = luaL_checkstring(L, 1);
    if (!event_name_ok(event) || !std::strchr(event, '/'))
        return luaL_error(L, "rood.emit: plugins emit namespaced events ('%s' needs a '/', as in 'my.plugin/changed')",
            event);
    if (emit_depth >= 8)
        return luaL_error(L, "rood.emit: events nested more than 8 deep");
    Value payload = lua_isnoneornil(L, 2) ? Value() : Value::from_lua(L, 2);
    lua_pushinteger(L, emit(event, payload));
    return 1;
}
} // namespace

int emit(const std::string& event, const Value& payload)
{
    std::vector<Handler> matching;
    for (const Handler& h : handlers())
        if (h.event == event)
            matching.push_back(h);
    return call_handlers(matching, event, payload);
}

int emit_to(Plugin* p, const std::string& event, const Value& payload)
{
    std::vector<Handler> matching;
    for (const Handler& h : handlers())
        if (h.event == event && h.plugin == p->id())
            matching.push_back(h);
    return call_handlers(matching, event, payload);
}

void tick_events(unsigned vsync)
{
    game_time += 1.0 / 59.94;
    Value payload = Value::new_table();
    payload.set("vsync", Value::integer(vsync));
    payload.set("game_time", Value::number(game_time));
    emit("tick", payload);
}

void frame_events(double dt)
{
    Value payload = Value::new_table();
    payload.set("dt", Value::number(dt));
    payload.set("real_time", Value::number(real_clock()));
    emit("frame", payload);
}

void drop_events(const std::string& plugin)
{
    std::vector<Handler>& list = handlers();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Handler& h) { return h.plugin == plugin; }),
        list.end());
}

size_t handler_count(const std::string& plugin)
{
    return (size_t)std::count_if(handlers().begin(), handlers().end(),
        [&](const Handler& h) { return h.plugin == plugin; });
}

double game_clock() { return game_time; }
double real_clock()
{
    static double origin = -1;
    double now = now_seconds();
    if (origin < 0)
        origin = now;
    return now - origin;
}

void open_events(lua_State* L, Plugin*)
{
    set_cfunction(L, -1, "on", api_on);
    set_cfunction(L, -1, "off", api_off);
    set_cfunction(L, -1, "emit", api_emit);
}

} // namespace rl
```

(`timer_count` is defined in Task 10; nothing calls it before then.)

- [ ] **Step 5: Open it in `rood`** — in `pc/src/lua/lua_api_core.cpp` `open_rood`, after `lua_setfield(L, -2, "plugin");` add:

```cpp
    open_events(L, p);
```

- [ ] **Step 6: Add the file to the probe** — in `pc/CMakeLists.txt` append `src/lua/lua_events.cpp` to `ROOD_LUA_RUNTIME`.

- [ ] **Step 7: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `lua_probe: ok`.

- [ ] **Step 8: Commit**

```bash
git add pc/src/lua/lua_events.cpp pc/src/lua/rl_internal.h pc/src/lua/lua_api_core.cpp pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua: event bus and the tick/frame hooks

rood.on/off/emit; each handler runs in its own plugin with its own copy
of the payload and its errors are its own plugin's faults; plugins emit
namespaced events only.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Timers and clocks

**Files:**
- Modify: `pc/src/lua/lua_events.cpp`, `pc/tests/lua_probe.cpp`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* rood.timer: game time advances with ticks (and stops when the game is
 * paused), real time with frames; every / cancel; bad arguments refused. */
static void timers()
{
    rl::Plugin* t = rl::console("@probe-timers");
    CHECK(eval_json(t, "fired = 0 rood.timer.after(0.05, function() fired = fired + 1 end) return fired") == "0");
    rl::tick_events(1);
    rl::tick_events(2);
    CHECK(eval_json(t, "fired") == "0");
    rl::tick_events(3);
    CHECK(eval_json(t, "fired") == "1");
    rl::tick_events(4);
    CHECK(eval_json(t, "fired") == "1");
    CHECK(eval_json(t, "n = 0 id = rood.timer.every(1 / 59.94, function() n = n + 1 end) return true") == "true");
    for (int k = 0; k < 3; k++)
        rl::tick_events(10 + k);
    CHECK(eval_json(t, "n >= 2, rood.timer.cancel(id), rood.timer.cancel(id)") == "true,true,false");
    CHECK(eval_json(t, "before = n return true") == "true");
    for (int k = 0; k < 3; k++)
        rl::tick_events(20 + k);
    CHECK(eval_json(t, "n == before") == "true");
    CHECK(eval_json(t, "real = 0 rood.timer.after(0, function() real = real + 1 end, { clock = 'real' }) return real")
        == "0");
    rl::frame_events(1.0 / 60);
    CHECK(eval_json(t, "real") == "1");
    CHECK(contains(eval_error(t, "rood.timer.after(-1, print)"), "seconds"));
    CHECK(contains(eval_error(t, "rood.timer.after(1, print, { clock = 'wall' })"), "clock"));
    CHECK(eval_json(t, "rood.clock.game() > 0, rood.clock.real() >= 0") == "true,true");
    CHECK(rl::timer_count("@probe-timers") == 0);
}
```

and in `main`: `timers();` after `events();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `undefined reference to `rl::timer_count(...)'`.

- [ ] **Step 3: Implement timers** — in `pc/src/lua/lua_events.cpp`, inside the anonymous namespace after `api_emit`, add:

```cpp
struct Timer {
    unsigned long long id;
    std::string plugin;
    unsigned generation;
    int ref;
    bool real;
    double due, interval; /* interval 0: once */
};
std::vector<Timer>& timers()
{
    static auto* list = new std::vector<Timer>();
    return *list;
}

int make_timer(lua_State* L, bool repeat)
{
    lua_Number seconds = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    bool real = false;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "clock");
        const char* clock = lua_tostring(L, -1);
        if (clock && !std::strcmp(clock, "real"))
            real = true;
        else if (clock && std::strcmp(clock, "game"))
            return luaL_error(L, "rood.timer: clock is 'game' or 'real', not '%s'", clock);
        lua_pop(L, 1);
    }
    if (!(seconds >= 0) || (repeat && seconds < 1.0 / 240))
        return luaL_error(L, "rood.timer: %s seconds must be %s", repeat ? "every's" : "after's",
            repeat ? "at least 1/240" : "0 or more");
    Plugin* p = plugin_of(L);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    double now = real ? real_clock() : game_time;
    timers().push_back({ next_id, p->id(), p->generation, ref, real, now + seconds, repeat ? (double)seconds : 0 });
    lua_pushinteger(L, (lua_Integer)next_id++);
    return 1;
}
int api_after(lua_State* L) { return make_timer(L, false); }
int api_every(lua_State* L) { return make_timer(L, true); }
int api_cancel(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    Plugin* p = plugin_of(L);
    std::vector<Timer>& list = timers();
    for (auto it = list.begin(); it != list.end(); ++it)
        if ((lua_Integer)it->id == id && it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            list.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}
int api_clock_game(lua_State* L)
{
    lua_pushnumber(L, game_time);
    return 1;
}
int api_clock_real(lua_State* L)
{
    lua_pushnumber(L, real_clock());
    return 1;
}

/* The due timers of one clock, earliest first; a timer cancelled by an earlier one is skipped. */
void fire_timers(bool real, double now)
{
    std::vector<Timer> due;
    for (const Timer& t : timers())
        if (t.real == real && t.due <= now + 1e-9)
            due.push_back(t);
    std::sort(due.begin(), due.end(), [](const Timer& a, const Timer& b) {
        return a.due != b.due ? a.due < b.due : a.id < b.id;
    });
    for (const Timer& snapshot : due) {
        std::vector<Timer>& list = timers();
        auto it = std::find_if(list.begin(), list.end(), [&](const Timer& t) { return t.id == snapshot.id; });
        if (it == list.end())
            continue;
        Plugin* p;
        if (!callable(it->plugin, it->generation, &p))
            continue;
        int ref = it->ref;
        bool once = it->interval <= 0;
        if (once)
            list.erase(it);
        else {
            it->due += it->interval;
            if (it->due <= now)
                it->due = now + it->interval; /* no catching up after a long frame */
        }
        run(p, "timer", BUDGET_CALL, [&](lua_State* L) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            if (once)
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            lua_call(L, 0, 0);
        });
    }
}
```

Then, outside the anonymous namespace: in `tick_events` add `fire_timers(false, game_time);` as its last line; in `frame_events` add `fire_timers(true, real_clock());` as its last line; in `drop_events` add before its closing brace:

```cpp
    std::vector<Timer>& list2 = timers();
    list2.erase(std::remove_if(list2.begin(), list2.end(), [&](const Timer& t) { return t.plugin == plugin; }),
        list2.end());
```

add after `handler_count`:

```cpp
size_t timer_count(const std::string& plugin)
{
    return (size_t)std::count_if(timers().begin(), timers().end(), [&](const Timer& t) { return t.plugin == plugin; });
}
```

and in `open_events` add after the `emit` line:

```cpp
    lua_newtable(L);
    set_cfunction(L, -1, "after", api_after);
    set_cfunction(L, -1, "every", api_every);
    set_cfunction(L, -1, "cancel", api_cancel);
    lock_table(L);
    lua_setfield(L, -2, "timer");
    lua_newtable(L);
    set_cfunction(L, -1, "game", api_clock_game);
    set_cfunction(L, -1, "real", api_clock_real);
    lock_table(L);
    lua_setfield(L, -2, "clock");
```

Update the file's header comment, adding: `Timers: rood.timer.after / every / cancel on the game's clock (ticks; it stops while the game is paused) or the real one ({ clock = "real" }; frames); rood.clock.game() / real().`

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `lua_probe: ok`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/lua/lua_events.cpp pc/tests/lua_probe.cpp
git commit -m "$(cat <<'EOF'
Lua: timers on the game's clock or the real one

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: The registry

**Files:**
- Create: `pc/src/lua/lua_registry.cpp`
- Modify: `pc/src/lua/rl_internal.h`, `pc/src/lua/lua_api_core.cpp`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* rood.registry: open kinds, ids in the owner's namespace, order by
 * after / before / group:<name>, entries as copies with callable functions,
 * only the owner removes; a cycle falls back to registration order. */
static void registry()
{
    rl::Plugin* r = rl::console("@probe-reg");
    rl::Plugin* o = rl::console("@probe-other");
    CHECK(eval_json(r,
        "local R = rood.registry "
        "R.register('probe.thing', '@probe-reg/b', { v = 'b' }, { after = '@probe-reg/a' }) "
        "R.register('probe.thing', '@probe-reg/a', { v = 'a', fn = function(x) return x + 1 end }) "
        "R.register('probe.thing', '@probe-reg/c', { v = 'c' }, { before = '@probe-reg/a', group = 'early' }) "
        "R.register('probe.thing', '@probe-reg/d', { v = 'd' }, { after = 'group:early', before = '@probe-reg/a' }) "
        "return true") == "true");
    CHECK(eval_json(o, "local ids = {} for _, e in ipairs(rood.registry.list('probe.thing')) do ids[#ids + 1] = e.id end "
                       "return table.concat(ids, ' ')")
        == "\"@probe-reg/c @probe-reg/d @probe-reg/a @probe-reg/b\"");
    CHECK(eval_json(o, "#rood.registry.list('probe.thing', { group = 'early' })") == "1");
    CHECK(eval_json(o, "local e = rood.registry.get('probe.thing', '@probe-reg/a') return e.owner, e.data.v, e.data.fn(41)")
        == "\"@probe-reg\",\"a\",42");
    CHECK(eval_json(o, "rood.registry.get('probe.thing', '@probe-reg/zz')") == "null");
    CHECK(contains(eval_error(o, "rood.registry.register('probe.thing', '@probe-reg/x', {})"), "namespace"));
    CHECK(contains(eval_error(o, "rood.registry.remove('probe.thing', '@probe-reg/a')"), "belongs"));
    CHECK(contains(eval_error(r, "rood.registry.register('probe.thing', '@probe-reg/a', {})"), "already"));
    CHECK(contains(eval_error(r, "rood.registry.register('Bad Kind', '@probe-reg/z', {})"), "bad kind"));
    CHECK(eval_json(r, "rood.registry.register('probe.loop', '@probe-reg/x', {}, { after = '@probe-reg/y' }) "
                       "rood.registry.register('probe.loop', '@probe-reg/y', {}, { after = '@probe-reg/x' }) "
                       "return #rood.registry.list('probe.loop')") == "2");
    CHECK(rl::registry_count("@probe-reg") == 6);
    CHECK(eval_json(r, "rood.registry.remove('probe.loop', '@probe-reg/y'), rood.registry.kinds()['probe.thing']")
        == "true,4");
    rl::drop_registry("@probe-reg");
    CHECK(eval_json(o, "#rood.registry.list('probe.thing')") == "0" && rl::registry_count("@probe-reg") == 0);
}
```

and in `main`: `registry();` after `timers();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `error: 'registry_count' is not a member of 'rl'`.

- [ ] **Step 3: Declare the registry** — add to `pc/src/lua/rl_internal.h` before `} // namespace rl`:

```cpp
/* ---- lua_registry.cpp ---- */
struct RegistryEntry {
    std::string kind, id, owner, group;
    std::vector<std::string> after, before; /* ids, or "group:<name>" */
    Value data;
    unsigned long long seq; /* registration order */
};
void open_registry(lua_State* L, Plugin* p); /* rood.registry */
/* A kind's entries in order (after / before, then registration order); group "": all. */
std::vector<const RegistryEntry*> registry_list(const std::string& kind, const std::string& group = "");
const RegistryEntry* registry_get(const std::string& kind, const std::string& id);
std::vector<std::string> registry_kinds();
size_t registry_count(const std::string& owner);
void drop_registry(const std::string& owner); /* its entries (registry.changed for each) */
```

- [ ] **Step 4: Write `pc/src/lua/lua_registry.cpp`**

```cpp
// rood.registry (pc/docs/Extensions.md, "Registry"): open registries keyed
// by kind ("menu.entry", "my.plugin.widget": lowercase words joined by
// dots) and namespaced ids ("<plugin id>/<name>": a plugin registers only
// in its own namespace and only the owner removes). Entries are data
// (copies; functions in them run in their owner, lua_value.cpp), ordered by
// relation, not index: after / before name an id or "group:<name>"; ties
// and cycles keep registration order. Everything a plugin registered goes
// when it unloads (drop_registry); registry.changed tells the others.
#include "rl_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

namespace rl {
namespace {
std::vector<RegistryEntry>& entries()
{
    static auto* list = new std::vector<RegistryEntry>();
    return *list;
}
unsigned long long next_seq = 1;
std::set<std::string> warned; /* kinds whose cycle was reported since the last change */

bool kind_ok(const char* s)
{
    size_t n = std::strlen(s);
    if (n == 0 || n > 64 || !std::islower((unsigned char)s[0]))
        return false;
    for (size_t k = 0; k < n; k++) {
        unsigned char c = (unsigned char)s[k];
        if (c == '.') {
            if (k + 1 == n || !std::islower((unsigned char)s[k + 1]))
                return false;
            continue;
        }
        if (!(std::islower(c) || std::isdigit(c) || c == '_'))
            return false;
    }
    return true;
}
bool local_ok(const char* s)
{
    size_t n = std::strlen(s);
    if (n == 0 || n > 64)
        return false;
    for (; *s; s++)
        if (!(std::isalnum((unsigned char)*s) || *s == '_' || *s == '.' || *s == '-'))
            return false;
    return true;
}

std::vector<std::string> string_list(lua_State* L, int table, const char* field)
{
    std::vector<std::string> out;
    lua_getfield(L, table, field);
    if (lua_type(L, -1) == LUA_TSTRING)
        out.push_back(lua_tostring(L, -1));
    else if (lua_istable(L, -1))
        for (lua_Integer k = 1; k <= (lua_Integer)lua_rawlen(L, -1); k++) {
            lua_rawgeti(L, -1, k);
            if (lua_type(L, -1) == LUA_TSTRING)
                out.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    else if (!lua_isnil(L, -1))
        luaL_error(L, "rood.registry.register: %s is an id or a list of ids", field);
    lua_pop(L, 1);
    return out;
}

void changed(const RegistryEntry& e, const char* action)
{
    warned.clear();
    Value payload = Value::new_table();
    payload.set("kind", Value::str(e.kind));
    payload.set("id", Value::str(e.id));
    payload.set("action", Value::str(action));
    emit("registry.changed", payload);
}

void push_strings(lua_State* L, const std::vector<std::string>& list)
{
    lua_createtable(L, (int)list.size(), 0);
    for (size_t k = 0; k < list.size(); k++) {
        lua_pushstring(L, list[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
}
void push_entry(lua_State* L, const RegistryEntry& e)
{
    lua_createtable(L, 0, 7);
    lua_pushstring(L, e.kind.c_str());
    lua_setfield(L, -2, "kind");
    lua_pushstring(L, e.id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, e.owner.c_str());
    lua_setfield(L, -2, "owner");
    if (!e.group.empty()) {
        lua_pushstring(L, e.group.c_str());
        lua_setfield(L, -2, "group");
    }
    push_strings(L, e.after);
    lua_setfield(L, -2, "after");
    push_strings(L, e.before);
    lua_setfield(L, -2, "before");
    push(L, e.data);
    lua_setfield(L, -2, "data");
}

int api_register(lua_State* L)
{
    const char* kind = luaL_checkstring(L, 1);
    const char* id = luaL_checkstring(L, 2);
    Plugin* p = plugin_of(L);
    if (!kind_ok(kind))
        return luaL_error(L, "rood.registry.register: bad kind '%s' (lowercase words joined by dots)", kind);
    std::string prefix = p->id() + "/";
    if (std::strncmp(id, prefix.c_str(), prefix.size()) != 0 || !local_ok(id + prefix.size()))
        return luaL_error(L, "rood.registry.register: id '%s' must be in this plugin's namespace ('%s<name>')", id,
            prefix.c_str());
    for (const RegistryEntry& e : entries())
        if (e.kind == kind && e.id == id)
            return luaL_error(L, "rood.registry.register: '%s' is already registered as a %s (remove it first)", id, kind);
    RegistryEntry e;
    e.kind = kind;
    e.id = id;
    e.owner = p->id();
    e.data = lua_isnoneornil(L, 3) ? Value() : Value::from_lua(L, 3);
    if (lua_istable(L, 4)) {
        e.after = string_list(L, 4, "after");
        e.before = string_list(L, 4, "before");
        lua_getfield(L, 4, "group");
        if (lua_type(L, -1) == LUA_TSTRING)
            e.group = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    e.seq = next_seq++;
    entries().push_back(e);
    changed(e, "register");
    lua_pushstring(L, id);
    return 1;
}

int api_get(lua_State* L)
{
    const RegistryEntry* e = registry_get(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
    if (e)
        push_entry(L, *e);
    else
        lua_pushnil(L);
    return 1;
}

int api_list(lua_State* L)
{
    const char* kind = luaL_checkstring(L, 1);
    std::string group;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "group");
        if (lua_type(L, -1) == LUA_TSTRING)
            group = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    std::vector<const RegistryEntry*> list = registry_list(kind, group);
    lua_createtable(L, (int)list.size(), 0);
    for (size_t k = 0; k < list.size(); k++) {
        push_entry(L, *list[k]);
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}

int api_remove(lua_State* L)
{
    const char* kind = luaL_checkstring(L, 1);
    const char* id = luaL_checkstring(L, 2);
    Plugin* p = plugin_of(L);
    std::vector<RegistryEntry>& list = entries();
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->kind != kind || it->id != id)
            continue;
        if (it->owner != p->id())
            return luaL_error(L, "rood.registry.remove: '%s' belongs to plugin %s", id, it->owner.c_str());
        RegistryEntry gone = *it;
        list.erase(it);
        changed(gone, "remove");
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int api_kinds(lua_State* L)
{
    lua_newtable(L);
    for (const std::string& kind : registry_kinds()) {
        lua_pushinteger(L, (lua_Integer)registry_list(kind).size());
        lua_setfield(L, -2, kind.c_str());
    }
    return 1;
}
} // namespace

std::vector<const RegistryEntry*> registry_list(const std::string& kind, const std::string& group)
{
    std::vector<const RegistryEntry*> nodes;
    for (const RegistryEntry& e : entries()) /* registration order */
        if (e.kind == kind)
            nodes.push_back(&e);
    size_t n = nodes.size();
    std::vector<std::vector<size_t>> next(n);
    std::vector<int> incoming(n, 0);
    auto targets = [&](const std::string& ref) {
        std::vector<size_t> out;
        bool is_group = ref.compare(0, 6, "group:") == 0;
        for (size_t k = 0; k < n; k++)
            if (is_group ? nodes[k]->group == ref.substr(6) : nodes[k]->id == ref)
                out.push_back(k);
        return out;
    };
    auto edge = [&](size_t from, size_t to) {
        if (from == to)
            return;
        next[from].push_back(to);
        incoming[to]++;
    };
    for (size_t k = 0; k < n; k++) {
        for (const std::string& a : nodes[k]->after)
            for (size_t t : targets(a))
                edge(t, k);
        for (const std::string& b : nodes[k]->before)
            for (size_t t : targets(b))
                edge(k, t);
    }
    std::vector<const RegistryEntry*> ordered;
    std::vector<bool> done(n, false);
    bool cycle = false;
    for (size_t round = 0; round < n; round++) {
        size_t pick = n;
        for (size_t k = 0; k < n && pick == n; k++)
            if (!done[k] && incoming[k] == 0)
                pick = k;
        if (pick == n) { /* a cycle: the earliest registered of what is left */
            cycle = true;
            for (size_t k = 0; k < n && pick == n; k++)
                if (!done[k])
                    pick = k;
        }
        done[pick] = true;
        ordered.push_back(nodes[pick]);
        for (size_t t : next[pick])
            incoming[t]--;
    }
    if (cycle && warned.insert(kind).second)
        log(LOG_WARN, "", "registry kind '" + kind + "': after/before form a cycle; registration order breaks it");
    if (!group.empty())
        ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                          [&](const RegistryEntry* e) { return e->group != group; }),
            ordered.end());
    return ordered;
}

const RegistryEntry* registry_get(const std::string& kind, const std::string& id)
{
    for (const RegistryEntry& e : entries())
        if (e.kind == kind && e.id == id)
            return &e;
    return nullptr;
}

std::vector<std::string> registry_kinds()
{
    std::set<std::string> kinds;
    for (const RegistryEntry& e : entries())
        kinds.insert(e.kind);
    return std::vector<std::string>(kinds.begin(), kinds.end());
}

size_t registry_count(const std::string& owner)
{
    return (size_t)std::count_if(entries().begin(), entries().end(),
        [&](const RegistryEntry& e) { return e.owner == owner; });
}

void drop_registry(const std::string& owner)
{
    std::vector<RegistryEntry> gone;
    std::vector<RegistryEntry>& list = entries();
    for (auto it = list.begin(); it != list.end();)
        if (it->owner == owner) {
            gone.push_back(*it);
            it = list.erase(it);
        } else
            ++it;
    for (const RegistryEntry& e : gone)
        changed(e, "remove");
}

void open_registry(lua_State* L, Plugin*)
{
    lua_newtable(L);
    set_cfunction(L, -1, "register", api_register);
    set_cfunction(L, -1, "get", api_get);
    set_cfunction(L, -1, "list", api_list);
    set_cfunction(L, -1, "remove", api_remove);
    set_cfunction(L, -1, "kinds", api_kinds);
    lock_table(L);
    lua_setfield(L, -2, "registry");
}

} // namespace rl
```

- [ ] **Step 5: Open it in `rood`** — in `open_rood` (`lua_api_core.cpp`), after `open_events(L, p);` add `open_registry(L, p);`.

- [ ] **Step 6: Add the file to the probe** — append `src/lua/lua_registry.cpp` to `ROOD_LUA_RUNTIME` in `pc/CMakeLists.txt`.

- [ ] **Step 7: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `lua_probe: ok`.

- [ ] **Step 8: Commit**

```bash
git add pc/src/lua/lua_registry.cpp pc/src/lua/rl_internal.h pc/src/lua/lua_api_core.cpp pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua: registry of namespaced entries ordered by after/before/group

Open kinds; a plugin registers in its own namespace and removes only its
own entries; functions in entries run in their owner; cycles fall back
to registration order with a warning.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: File helpers and SHA-256

**Files:**
- Create: `pc/src/lua/lua_files.cpp`, `pc/src/lua/sha256.h`, `pc/src/lua/sha256.cpp`
- Modify: `pc/src/lua/lua_sandbox.cpp` (remove the temporary `read_file`), `pc/src/lua/rl_internal.h`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — add `#include "lua/sha256.h"` to the probe's includes, then before `main`:

```cpp
static void files_and_hash()
{
    CHECK(rl::sha256_hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(rl::sha256_hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::string million(1000000, 'a');
    CHECK(rl::sha256_hex(million.data(), million.size())
        == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std::string why;
    CHECK(rl::safe_relative("data/a-b_c.txt", &why));
    CHECK(!rl::safe_relative("../x", &why) && contains(why, ".."));
    CHECK(!rl::safe_relative("/etc/passwd", &why));
    CHECK(!rl::safe_relative("a//b", &why));
    CHECK(!rl::safe_relative("C:/x", &why));
    CHECK(!rl::safe_relative("a\\b", &why));
    std::string dir = scratch + "/files";
    CHECK(rl::write_file(dir + "/deep/er/note.txt", "hello", &why));
    std::string text;
    CHECK(rl::read_file(dir + "/deep/er/note.txt", &text, 100, &why) && text == "hello");
    CHECK(!rl::read_file(dir + "/deep/er/note.txt", &text, 3, &why) && contains(why, "larger"));
    CHECK((rl::list_dir(dir + "/deep") == std::vector<std::string>{ "er" }));
    long long mtime = 0, size = 0;
    bool is_dir = true;
    CHECK(rl::file_stat(dir + "/deep/er/note.txt", &mtime, &size, &is_dir) && size == 5 && !is_dir && mtime > 0);
}
```

and in `main`: `files_and_hash();` after `registry();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `fatal error: lua/sha256.h: No such file or directory`.

- [ ] **Step 3: Write `pc/src/lua/sha256.h` and `pc/src/lua/sha256.cpp`**

```cpp
/* SHA-256 (FIPS 180-4): a plugin's content hash (lua_plugins.cpp), which
 * its grants follow (lua_caps.cpp). */
#ifndef ROOD_SHA256_H
#define ROOD_SHA256_H
#include <cstddef>
#include <cstdint>
#include <string>

namespace rl {
class Sha256 {
public:
    void update(const void* data, size_t size);
    std::string hex(); /* finishes: the object is spent */

private:
    void block(const unsigned char* p);
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char buffer[64] = {};
    size_t used = 0;
    uint64_t total = 0;
};
std::string sha256_hex(const void* data, size_t size);
} // namespace rl
#endif
```

```cpp
// SHA-256 (FIPS 180-4), written for this project (CC0 like the rest of
// pc/): plugins' content hashes.
#include "sha256.h"

#include <cstdio>
#include <cstring>

namespace rl {
namespace {
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
} // namespace

void Sha256::block(const unsigned char* p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

void Sha256::update(const void* data, size_t size)
{
    const unsigned char* p = (const unsigned char*)data;
    total += size;
    while (size) {
        size_t take = 64 - used < size ? 64 - used : size;
        std::memcpy(buffer + used, p, take);
        used += take, p += take, size -= take;
        if (used == 64) {
            block(buffer);
            used = 0;
        }
    }
}

std::string Sha256::hex()
{
    uint64_t bits = total * 8;
    unsigned char pad = 0x80, zero = 0, length[8];
    update(&pad, 1);
    while (used != 56)
        update(&zero, 1);
    for (int i = 0; i < 8; i++)
        length[i] = (unsigned char)(bits >> (56 - 8 * i));
    update(length, 8);
    char out[65];
    for (int i = 0; i < 8; i++)
        std::snprintf(out + 8 * i, 9, "%08x", (unsigned)h[i]);
    return std::string(out, 64);
}

std::string sha256_hex(const void* data, size_t size)
{
    Sha256 s;
    s.update(data, size);
    return s.hex();
}
} // namespace rl
```

- [ ] **Step 4: Write `pc/src/lua/lua_files.cpp`** and delete the temporary `read_file` block (the one marked `Moved to lua_files.cpp by Task 12`) at the end of `pc/src/lua/lua_sandbox.cpp`

```cpp
// Files for the extension system (pc/docs/Extensions.md): whole-file reads
// with a size limit, atomic writes (a .tmp then pc_replace_file), mkdir -p,
// stat, sorted directory lists, and the one path check every plugin path
// goes through (safe_relative: no "..", no absolute or drive paths, plain
// characters only).
#include "rl_internal.h"
#include "platform.h"

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>

namespace rl {

bool read_file(const std::string& path, std::string* out, size_t limit, std::string* why)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (why)
            *why = "not found";
        return false;
    }
    std::string text;
    char buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
        if (text.size() + n > limit) {
            std::fclose(f);
            if (why)
                *why = "larger than " + std::to_string(limit / 1024) + " KB";
            return false;
        }
        text.append(buffer, n);
    }
    bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) {
        if (why)
            *why = "read error";
        return false;
    }
    *out = std::move(text);
    return true;
}

bool make_dirs(const std::string& path)
{
    std::string part;
    for (size_t k = 0; k <= path.size(); k++) {
        if (k == path.size() || path[k] == '/') {
            if (!part.empty() && part != ".") {
                struct stat st;
                if (stat(part.c_str(), &st) != 0 && pc_mkdir(part.c_str()) != 0)
                    return false;
            }
        }
        if (k < path.size())
            part += path[k];
    }
    return true;
}

bool write_file(const std::string& path, const std::string& text, std::string* why)
{
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && !make_dirs(path.substr(0, slash))) {
        if (why)
            *why = "cannot create " + path.substr(0, slash);
        return false;
    }
    std::string temp = path + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) {
        if (why)
            *why = "cannot write " + temp;
        return false;
    }
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        std::remove(temp.c_str());
        if (why)
            *why = "cannot write " + path;
        return false;
    }
    return true;
}

bool file_stat(const std::string& path, long long* mtime_ns, long long* size, bool* is_dir)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    if (mtime_ns)
        *mtime_ns = PC_MTIME_NS(st);
    if (size)
        *size = (long long)st.st_size;
    if (is_dir)
        *is_dir = S_ISDIR(st.st_mode);
    return true;
}

std::vector<std::string> list_dir(const std::string& path)
{
    std::vector<std::string> names;
    DIR* d = opendir(path.c_str());
    if (!d)
        return names;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name != "." && name != "..")
            names.push_back(name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

bool safe_relative(const std::string& path, std::string* why)
{
    auto refuse = [&](const char* reason) {
        if (why)
            *why = reason;
        return false;
    };
    if (path.empty() || path.size() > 200)
        return refuse("empty or longer than 200 characters");
    if (path[0] == '/')
        return refuse("an absolute path");
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        std::string part = path.substr(start, end - start);
        if (part.empty())
            return refuse("an empty path component");
        if (part == "." || part == "..")
            return refuse("'.' and '..' are not allowed");
        for (char c : part)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'))
                return refuse("only letters, digits, _ - . and / are allowed");
        start = end + 1;
    }
    return true;
}

} // namespace rl
```

Add `#include <cctype>` to its includes (for `std::isalnum`).

- [ ] **Step 5: Declare the helpers** — in `pc/src/lua/rl_internal.h` replace the `lua_files.cpp` section (from Task 7) with:

```cpp
/* ---- lua_files.cpp ---- */
/* The whole file; false (and why: "not found", "larger than N KB") past `limit` bytes. */
bool read_file(const std::string& path, std::string* out, size_t limit, std::string* why);
bool write_file(const std::string& path, const std::string& text, std::string* why); /* atomic; makes folders */
bool make_dirs(const std::string& path);
bool file_stat(const std::string& path, long long* mtime_ns, long long* size, bool* is_dir);
std::vector<std::string> list_dir(const std::string& path); /* sorted names, without . and .. */
bool safe_relative(const std::string& path, std::string* why); /* a/b.c: no "..", no absolute path */
```

- [ ] **Step 6: Add the files to the probe** — append `src/lua/lua_files.cpp src/lua/sha256.cpp` to `ROOD_LUA_RUNTIME` in `pc/CMakeLists.txt`.

- [ ] **Step 7: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `lua_probe: ok`.

- [ ] **Step 8: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua: file helpers and SHA-256 for plugins

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 13: Plugins on disk — manifests, order, load/unload, faults, settings lines

**Files:**
- Create: `pc/src/lua/lua_runtime.h`, `pc/src/lua/lua_plugins.cpp`, `pc/src/lua/lua_caps.cpp` (first part)
- Modify: `pc/src/lua/rl_internal.h`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — add `#include "lua/lua_runtime.h"`, `#include <algorithm>` and `#include <fstream>` to the probe's includes, then before `main`:

```cpp
static void write_text(const std::string& path, const std::string& text)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
static std::string state_of(const char* id)
{
    rl::Plugin* p = rl::find(id);
    return p ? std::string(rl::state_name(p->state)) : std::string("missing");
}
static std::string settings_text()
{
    FILE* f = std::tmpfile();
    pc_lua_settings_write(f);
    std::rewind(f);
    char buffer[8192] = {};
    size_t n = std::fread(buffer, 1, sizeof buffer - 1, f);
    std::fclose(f);
    return std::string(buffer, n);
}

/* Plugins from folders: manifests (Lua or JSON) checked, dependencies
 * first, a plugin that faults on every tick disabled at the next safe point
 * while the others go on, enable/disable and order through settings lines. */
static void plugins_on_disk()
{
    std::string root = scratch + "/plugins";
    write_text(root + "/probe.alpha/plugin.lua",
        "return { id = 'probe.alpha', name = 'Alpha', version = '1.2.0', api_version = 1 }\n");
    write_text(root + "/probe.alpha/main.lua",
        "ticks = 0\n"
        "rood.on('tick', function() ticks = ticks + 1 end)\n"
        "rood.registry.register('probe.entry', 'probe.alpha/one', { version = 1 })\n");
    write_text(root + "/probe.beta/plugin.json",
        "{ \"id\": \"probe.beta\", \"version\": \"0.1.0\", \"api_version\": 1,\n"
        "  \"depends\": [ { \"id\": \"probe.alpha\", \"version\": \">=1.0.0\" } ] }\n");
    write_text(root + "/probe.beta/main.lua",
        "rood.registry.register('probe.entry', 'probe.beta/two', {}, { after = 'probe.alpha/one' })\n");
    write_text(root + "/probe.misnamed/plugin.lua", "return { id = 'probe.other', api_version = 1 }\n");
    write_text(root + "/probe.misnamed/main.lua", "\n");
    write_text(root + "/probe.future/plugin.lua", "return { id = 'probe.future', api_version = 99 }\n");
    write_text(root + "/probe.future/main.lua", "\n");
    write_text(root + "/probe.faulty/plugin.lua", "return { id = 'probe.faulty', api_version = 1 }\n");
    write_text(root + "/probe.faulty/main.lua", "rood.on('tick', function() error('every tick') end)\n");
    write_text(root + "/probe.broken/plugin.lua", "return { id = 'probe.broken', api_version = 1 }\n");
    write_text(root + "/probe.broken/main.lua", "this is not lua\n");
    write_text(root + "/probe.off/plugin.lua", "return { id = 'probe.off', api_version = 1, default_enabled = false }\n");
    write_text(root + "/probe.off/main.lua", "\n");
    write_text(root + "/rood.reserved/plugin.lua", "return { id = 'rood.reserved', api_version = 1 }\n");
    write_text(root + "/rood.reserved/main.lua", "\n");
    pc_lua_settings_parse("plugin_order", "probe.beta,probe.alpha\n");
    pc_lua_init();
    CHECK(state_of("probe.alpha") == "loaded" && state_of("probe.beta") == "loaded");
    CHECK(state_of("probe.off") == "off");
    CHECK(state_of("probe.misnamed") == "failed" && contains(rl::find("probe.misnamed")->status, "named after its id"));
    CHECK(state_of("probe.future") == "failed" && contains(rl::find("probe.future")->status, "api_version"));
    CHECK(state_of("probe.broken") == "failed" && contains(rl::find("probe.broken")->status, "main chunk"));
    CHECK(state_of("rood.reserved") == "failed" && contains(rl::find("rood.reserved")->status, "reserved"));
    std::vector<std::string> ids;
    for (rl::Plugin* p : rl::load_order())
        ids.push_back(p->id());
    CHECK(std::find(ids.begin(), ids.end(), "probe.alpha") < std::find(ids.begin(), ids.end(), "probe.beta"));
    CHECK(rl::find("probe.alpha")->data_dir == scratch + "/data/probe.alpha");
    CHECK(eval_json(rl::find("probe.alpha"), "rood.plugin.name, rood.plugin.version, rood.plugin.shipped")
        == "\"Alpha\",\"1.2.0\",false");
    for (int k = 0; k < 6; k++)
        pc_lua_tick();
    CHECK(state_of("probe.faulty") == "disabled" && contains(rl::find("probe.faulty")->status, "faults within"));
    CHECK(eval_json(rl::find("probe.alpha"), "ticks") == "6");
    pc_lua_settings_parse("plugin_enabled", "probe.alpha 0\n");
    pc_lua_settings_end();
    pc_lua_tick();
    CHECK(state_of("probe.alpha") == "off" && state_of("probe.beta") == "failed");
    CHECK(rl::registry_list("probe.entry").empty());
    pc_lua_settings_parse("plugin_enabled", "probe.alpha 1\n");
    pc_lua_settings_end();
    pc_lua_tick();
    CHECK(state_of("probe.alpha") == "loaded" && state_of("probe.beta") == "loaded");
    std::string written = settings_text();
    CHECK(contains(written, "plugin_budget_ms 4\n") && contains(written, "plugin_enabled probe.alpha 1\n"));
    CHECK(contains(written, "plugin_order probe.beta,probe.alpha\n") && contains(written, "plugins_elevated 0\n"));
}
```

and in `main`: `plugins_on_disk();` after `files_and_hash();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `fatal error: lua/lua_runtime.h: No such file or directory`.

- [ ] **Step 3: Write `pc/src/lua/lua_runtime.h`**

```c
/* The extension system's entry points for the game (pc/src/lua;
 * pc/docs/Extensions.md "Where Lua runs"). Every call is on the game thread. */
#ifndef ROOD_LUA_RUNTIME_H
#define ROOD_LUA_RUNTIME_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

void pc_lua_init(void);       /* main(), after the settings load: finds and loads the plugins */
void pc_lua_shutdown(void);   /* atexit: writes what plugins stored; runs no Lua */
void pc_lua_tick(void);       /* pc_tick: once per game vsync while the game runs */
void pc_lua_frame(double dt); /* pc_overlay_frame: every presented image, the ImGui frame open */
/* RPC method lua (dev_rpc.c): the params object's text; the reply's JSON in out
 * (its length), or < 0 with the error's text in out. */
int pc_lua_rpc(const char* params, char* out, int size);
/* A core event to every plugin (payload: JSON, or NULL); returns how many handlers ran. */
int pc_lua_emit_json(const char* event, const char* json);
/* settings.cfg (project_settings.cpp): plugins_elevated, plugin_enabled, plugin_order,
 * plugin_grant, plugin_budget_ms, ... begin/end bracket a whole file's load. */
void pc_lua_settings_begin(void);
int pc_lua_settings_parse(const char* key, const char* rest); /* 1: a plugin line */
void pc_lua_settings_end(void);
void pc_lua_settings_write(FILE* f);
/* The dev overlay's Lua console (lua_console.cpp). */
void pc_lua_console_menu_item(void);
void pc_lua_console_window(void);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 4: Declare capabilities and plugins** — add to `pc/src/lua/rl_internal.h` before `} // namespace rl`:

```cpp
/* ---- lua_caps.cpp ---- */
struct CapabilityInfo {
    const char* name;  /* "fs.read" */
    bool scoped;       /* takes ":<scope>" (a folder, a host) */
    bool implemented;  /* M0: fs.read and fs.write; the others refuse with a message */
    const char* plain; /* what the grant dialog says */
};
const std::vector<CapabilityInfo>& capability_table();
const CapabilityInfo* capability_info(const std::string& name); /* without the scope */
bool capability_ok(const std::string& capability, std::string* why);

/* ---- lua_plugins.cpp ---- */
std::string shipped_root(); /* ROOD_PLUGINS_SHIPPED, else pc/plugins ("" : none) */
std::string user_root();    /* ROOD_PLUGINS, else pc/user/plugins */
std::string data_root();    /* ROOD_PLUGIN_DATA, else pc/user/plugin_data */
bool parse_manifest(const Value& v, Manifest& m, std::string* error);
bool read_manifest(const std::string& dir, Manifest& m, std::string* error); /* plugin.json, else plugin.lua */
std::string content_hash(const std::string& dir); /* SHA-256 over its code files, paths sorted */
void scan();                       /* new folders become plugins (not loaded yet) */
std::vector<Plugin*> load_order(); /* dependencies first, then plugin_order, shipped, id */
bool load(Plugin* p);
void unload(Plugin* p, int new_state, const std::string& status);
void unload_with_dependents(Plugin* p, int new_state, const std::string& status);
void reload(Plugin* p); /* and what depends on it */
void request_reload_all();
bool plugin_enabled(const Plugin* p);
void set_enabled(const std::string& id, bool enabled); /* settings plugin_enabled; saves settings.cfg */
void config_changed(); /* settle() reconciles: enabled set, grants, memory caps */
void save_settings_now(); /* pc_settings_save when the game has it (enable, grants) */
void settle();         /* the safe point: disables, reloads, reconcile; nothing when a call is in progress */
void init();
bool initialized();
void tick();
void frame(double dt);
struct Tunable {
    const char* key;   /* settings.cfg */
    const char* rpc;   /* RPC lua {action "budget"} */
    const char* label; /* the console */
    double* d;
    long long* i;
    int* k;
    double lo, hi;
};
const std::vector<Tunable>& tunables();
double get_tunable(const Tunable& t);
void set_tunable(const Tunable& t, double value); /* clamped; config_changed() */
```

- [ ] **Step 5: Write `pc/src/lua/lua_caps.cpp` (first part)**

```cpp
// Capabilities (pc/docs/Extensions.md, "Capabilities"): what a plugin may
// ask for beyond the sandbox, declared in its manifest ("fs.read",
// "fs.write:pc/user/screens", "net:example.com", "exec", "memory.read",
// "memory.write", "flags.write", "native"), granted by the user per plugin
// and per capability, tied to the plugin's content hash. This part knows
// the capabilities and checks a declaration.
#include "rl_internal.h"

#include <cctype>

namespace rl {

const std::vector<CapabilityInfo>& capability_table()
{
    static const std::vector<CapabilityInfo> table = {
        { "fs.read", true, true, "Read files in its own folder and in the folders you approve" },
        { "fs.write", true, true, "Create and change files in its own folder and in the folders you approve" },
        { "net", true, false, "Connect to the internet (the hosts it lists)" },
        { "exec", false, false, "Run programs on your computer" },
        { "memory.read", false, false, "Read the game's memory" },
        { "memory.write", false, false, "Change the game's memory (this can corrupt saves)" },
        { "flags.write", false, false, "Change the game's story flags (this can corrupt saves)" },
        { "native", false, false, "Load native code (full access to your computer)" },
    };
    return table;
}

const CapabilityInfo* capability_info(const std::string& name)
{
    for (const CapabilityInfo& c : capability_table())
        if (name == c.name)
            return &c;
    return nullptr;
}

bool capability_ok(const std::string& capability, std::string* why)
{
    size_t colon = capability.find(':');
    std::string name = capability.substr(0, colon);
    std::string scope = colon == std::string::npos ? std::string() : capability.substr(colon + 1);
    const CapabilityInfo* info = capability_info(name);
    if (!info) {
        *why = "unknown capability '" + name + "' (fs.read, fs.write, net, exec, memory.read, memory.write, "
               "flags.write, native)";
        return false;
    }
    if (colon != std::string::npos && !info->scoped) {
        *why = "'" + name + "' takes no scope";
        return false;
    }
    if (colon != std::string::npos && scope.empty()) {
        *why = "an empty scope";
        return false;
    }
    for (char c : scope)
        if (std::isspace((unsigned char)c)) {
            *why = "a scope may not contain spaces";
            return false;
        }
    if (name == "fs.read" || name == "fs.write") {
        std::string path = scope;
        for (char& c : path)
            if (c == '\\')
                c = '/';
        size_t start = 0;
        while (start <= path.size()) {
            size_t end = path.find('/', start);
            if (end == std::string::npos)
                end = path.size();
            if (path.compare(start, end - start, "..") == 0 && end - start == 2) {
                *why = "a scope may not contain '..'";
                return false;
            }
            start = end + 1;
        }
    }
    return true;
}

} // namespace rl
```

- [ ] **Step 6: Write `pc/src/lua/lua_plugins.cpp`**

```cpp
// Plugins (pc/docs/Extensions.md, "Plugins"): folders with a manifest
// (plugin.lua returning a table, or plugin.json) in pc/plugins (shipped,
// committed; ROOD_PLUGINS_SHIPPED) and in the user's folder
// (pc/user/plugins; ROOD_PLUGINS), their data in
// pc/user/plugin_data/<id> (ROOD_PLUGIN_DATA). Discovery and the
// manifest's checks; enable / disable and load order (settings.cfg
// plugin_enabled, plugin_order; dependencies always first); loading a
// plugin's main chunk in a fresh sandbox, unloading everything it
// registered, reloading it with what depends on it; the safe point
// (settle) where faults disable plugins and reloads happen; the tunables
// (settings plugin_*); and the game's entry points (lua_runtime.h).
#include "rl_internal.h"
#include "light_prefabs.h"
#include "lua_runtime.h"
#include "sha256.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>

extern "C" int pc_overlay_active(void) __attribute__((weak));
extern "C" unsigned pc_vsync_count(void) __attribute__((weak));
extern "C" void pc_settings_save(void) __attribute__((weak));

namespace rl {
std::map<std::string, bool> enabled_override; /* settings plugin_enabled (pc_lua_settings_* below) */
std::vector<std::string> order;               /* settings plugin_order */
namespace {
bool started;
bool dirty_config;
unsigned ticks;

std::string env_or(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

bool id_ok(const std::string& id)
{
    if (id.empty() || id.size() > 64 || id.front() == '.' || id.back() == '.')
        return false;
    int dots = 0;
    for (size_t k = 0; k < id.size(); k++) {
        char c = id[k];
        if (c == '.') {
            if (id[k + 1] == '.')
                return false;
            ++dots;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return dots >= 1;
}

bool version_ok(const std::string& v)
{
    int parts = 0;
    size_t start = 0;
    while (true) {
        size_t end = v.find('.', start);
        std::string part = v.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part.size() > 9 || part.find_first_not_of("0123456789") != std::string::npos)
            return false;
        ++parts;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return parts == 3;
}

int compare_versions(const std::string& a, const std::string& b)
{
    long x[3] = {}, y[3] = {};
    std::sscanf(a.c_str(), "%ld.%ld.%ld", &x[0], &x[1], &x[2]);
    std::sscanf(b.c_str(), "%ld.%ld.%ld", &y[0], &y[1], &y[2]);
    for (int k = 0; k < 3; k++)
        if (x[k] != y[k])
            return x[k] < y[k] ? -1 : 1;
    return 0;
}

std::string first_line(const std::string& s) { return s.substr(0, s.find('\n')); }

std::string folder_name(const std::string& dir)
{
    size_t slash = dir.find_last_of('/');
    return slash == std::string::npos ? dir : dir.substr(slash + 1);
}

/* plugin.lua in a throwaway state with no libraries: a table constructor is all it can be. */
bool manifest_from_lua(const std::string& dir, const std::string& code, Value* out, std::string* error)
{
    Plugin temp;
    temp.manifest.id = "@manifest";
    temp.console = true;
    temp.mem.cap = 1u << 20;
    temp.L = new_state(&temp);
    if (!temp.L) {
        *error = "plugin.lua: cannot create a state";
        return false;
    }
    std::string chunk = "@" + dir + "/plugin.lua", why;
    bool ok = run(&temp, "plugin.lua", BUDGET_TEST, [&](lua_State* L) {
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 1);
        *out = Value::from_lua(L, -1);
    }, &why);
    close_state(&temp);
    if (!ok)
        *error = "plugin.lua: " + first_line(why);
    return ok;
}

/* The checks every load repeats: the manifest, the folder's name, the reserved namespace. */
bool check_manifest(const std::string& dir, bool shipped, Manifest& m, std::string* error)
{
    if (!read_manifest(dir, m, error))
        return false;
    if (m.id != folder_name(dir)) {
        *error = "the folder must be named after its id ('" + m.id + "')";
        return false;
    }
    if (!shipped && m.id.compare(0, 5, "rood.") == 0) {
        *error = "ids starting with 'rood.' are reserved for plugins shipped with the game";
        return false;
    }
    return true;
}

bool ends_with(const std::string& s, const char* suffix)
{
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

/* Code files under dir (relative, sorted): *.lua, *.fnl, plugin.json; `skip`: a subfolder left out. */
std::vector<std::string> code_files(const std::string& dir, const std::string& skip)
{
    std::vector<std::string> out;
    std::vector<std::pair<std::string, int>> todo{ { std::string(), 0 } };
    while (!todo.empty() && out.size() < 2000) {
        std::pair<std::string, int> next = todo.back();
        todo.pop_back();
        std::string here = next.first.empty() ? dir : dir + "/" + next.first;
        for (const std::string& name : list_dir(here)) {
            if (name[0] == '.')
                continue;
            std::string path = next.first.empty() ? name : next.first + "/" + name;
            bool is_dir = false;
            if (!file_stat(here + "/" + name, nullptr, nullptr, &is_dir))
                continue;
            if (is_dir) {
                if (next.second < 8 && path != skip)
                    todo.push_back({ path, next.second + 1 });
                continue;
            }
            if (ends_with(name, ".lua") || ends_with(name, ".fnl") || name == "plugin.json")
                out.push_back(path);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/* Loaded plugins that depend on p, directly or not, in load order. */
std::vector<Plugin*> dependents_of(Plugin* p)
{
    std::vector<Plugin*> out;
    std::vector<std::string> ids{ p->id() };
    for (Plugin* q : load_order()) {
        if (q == p || q->state != PLUGIN_LOADED)
            continue;
        for (const Dependency& d : q->manifest.depends)
            if (std::find(ids.begin(), ids.end(), d.id) != ids.end()) {
                out.push_back(q);
                ids.push_back(q->id());
                break;
            }
    }
    return out;
}

void reconcile()
{
    size_t cap = (size_t)(config.memory_mb * 1048576.0);
    for (auto& up : all())
        if (up->L)
            up->mem.cap = cap;
    for (Plugin* p : load_order()) {
        bool want = plugin_enabled(p);
        if (want && (p->state == PLUGIN_OFF || (p->state == PLUGIN_FAILED && p->waiting)))
            load(p);
        else if (!want && p->state == PLUGIN_LOADED)
            unload_with_dependents(p, PLUGIN_OFF, "off (disabled in the plugin list)");
        else if (!want && p->state != PLUGIN_OFF) {
            p->state = PLUGIN_OFF;
            p->status = "off (disabled in the plugin list)";
            p->waiting = false;
        }
    }
}
} // namespace

std::string shipped_root() { return env_or("ROOD_PLUGINS_SHIPPED", "pc/plugins"); }
std::string user_root() { return env_or("ROOD_PLUGINS", "pc/user/plugins"); }
std::string data_root() { return env_or("ROOD_PLUGIN_DATA", "pc/user/plugin_data"); }

void save_settings_now()
{
    if (pc_settings_save)
        pc_settings_save();
}

bool parse_manifest(const Value& v, Manifest& m, std::string* error)
{
    m = Manifest();
    if (v.kind != Value::TABLE) {
        *error = "the manifest must be a table (plugin.lua: return { ... }) or an object (plugin.json)";
        return false;
    }
    auto text = [&](const char* key, std::string& out, bool required) {
        const Value* f = v.get(key);
        if (!f || f->kind == Value::NIL) {
            if (required)
                *error = std::string("the manifest needs ") + key;
            return !required;
        }
        if (f->kind != Value::STRING) {
            *error = std::string("the manifest's ") + key + " must be a string";
            return false;
        }
        out = f->s;
        return true;
    };
    if (!text("id", m.id, true) || !text("name", m.name, false) || !text("version", m.version, false)
        || !text("entry", m.entry, false) || !text("description", m.description, false)
        || !text("author", m.author, false) || !text("tests", m.tests, false))
        return false;
    if (!id_ok(m.id)) {
        *error = "id '" + m.id + "': lowercase words joined by dots, at least two (author.plugin)";
        return false;
    }
    if (m.name.empty())
        m.name = m.id;
    if (!version_ok(m.version)) {
        *error = "version '" + m.version + "': three numbers, as in 1.0.0";
        return false;
    }
    std::string why;
    if (!safe_relative(m.entry, &why) || !ends_with(m.entry, ".lua")) {
        *error = "entry '" + m.entry + "': a .lua file in the plugin's folder";
        return false;
    }
    if (!safe_relative(m.tests, &why)) {
        *error = "tests '" + m.tests + "': " + why;
        return false;
    }
    const Value* api = v.get("api_version");
    if (!api || api->kind != Value::INTEGER) {
        *error = "the manifest needs api_version (an integer; this build has " + std::to_string(RL_API_VERSION) + ")";
        return false;
    }
    if (api->i < 1 || api->i > RL_API_VERSION) {
        *error = "needs api_version " + std::to_string(api->i) + "; this build has " + std::to_string(RL_API_VERSION);
        return false;
    }
    m.api_version = (int)api->i;
    if (const Value* on = v.get("default_enabled")) {
        if (on->kind != Value::BOOLEAN) {
            *error = "default_enabled must be true or false";
            return false;
        }
        m.default_enabled = on->b;
    }
    if (const Value* deps = v.get("depends")) {
        if (deps->kind != Value::TABLE) {
            *error = "depends must be a list";
            return false;
        }
        for (const auto& kv : deps->table) {
            const Value& d = kv.second;
            Dependency dep;
            if (d.kind == Value::STRING)
                dep.id = d.s;
            else if (d.kind == Value::TABLE && d.get("id") && d.get("id")->kind == Value::STRING) {
                dep.id = d.get("id")->s;
                if (const Value* ver = d.get("version")) {
                    if (ver->kind != Value::STRING || ver->s.compare(0, 2, ">=") != 0 || !version_ok(ver->s.substr(2))) {
                        *error = "depends: a version is \">=x.y.z\"";
                        return false;
                    }
                    dep.min_version = ver->s.substr(2);
                }
            } else {
                *error = "depends: each is an id or { id = ..., version = \">=x.y.z\" }";
                return false;
            }
            if (!id_ok(dep.id)) {
                *error = "depends: bad id '" + dep.id + "'";
                return false;
            }
            m.depends.push_back(dep);
        }
    }
    if (const Value* caps = v.get("capabilities")) {
        if (caps->kind != Value::TABLE) {
            *error = "capabilities must be a list";
            return false;
        }
        for (const auto& kv : caps->table) {
            if (kv.second.kind != Value::STRING) {
                *error = "capabilities: each is a string";
                return false;
            }
            if (!capability_ok(kv.second.s, &why)) {
                *error = "capability '" + kv.second.s + "': " + why;
                return false;
            }
            m.capabilities.push_back(kv.second.s);
        }
    }
    return true;
}

bool read_manifest(const std::string& dir, Manifest& m, std::string* error)
{
    std::string text, why;
    Value v;
    if (read_file(dir + "/plugin.json", &text, RL_MAX_FILE, &why)) {
        prefab::Json j;
        if (!prefab::json_parse(text.c_str(), j, &why)) {
            *error = "plugin.json: " + why;
            return false;
        }
        v = from_json(j);
    } else if (read_file(dir + "/plugin.lua", &text, RL_MAX_FILE, &why)) {
        if (!manifest_from_lua(dir, text, &v, error))
            return false;
    } else {
        *error = "no plugin.lua or plugin.json";
        return false;
    }
    return parse_manifest(v, m, error);
}

std::string content_hash(const std::string& dir)
{
    Sha256 h;
    for (const std::string& file : code_files(dir, "")) {
        std::string text, why;
        read_file(dir + "/" + file, &text, RL_MAX_FILE * 4, &why);
        unsigned char size[8];
        uint64_t n = text.size();
        for (int k = 0; k < 8; k++)
            size[k] = (unsigned char)(n >> (8 * k));
        h.update(file.data(), file.size() + 1); /* with its terminating zero */
        h.update(size, sizeof size);
        h.update(text.data(), text.size());
    }
    return h.hex();
}

void scan()
{
    for (int pass = 0; pass < 2; pass++) {
        bool shipped = pass == 0;
        std::string root = shipped ? shipped_root() : user_root();
        if (root.empty())
            continue;
        for (const std::string& name : list_dir(root)) {
            std::string dir = root + "/" + name;
            bool is_dir = false;
            if (name[0] == '.' || !file_stat(dir, nullptr, nullptr, &is_dir) || !is_dir)
                continue;
            bool known = false;
            for (auto& p : all())
                known |= p->dir == dir;
            if (known)
                continue;
            Manifest m;
            std::string error;
            bool ok = check_manifest(dir, shipped, m, &error);
            if (find(ok ? m.id : name)) {
                log(LOG_ERROR, "", dir + ": another plugin already has the id " + (ok ? m.id : name) + "; skipped");
                continue;
            }
            auto made = std::make_unique<Plugin>();
            Plugin* p = made.get();
            p->dir = dir;
            p->shipped = shipped;
            if (ok) {
                p->manifest = m;
                p->manifest_ok = true;
                p->status = "off";
            } else {
                p->manifest.id = p->manifest.name = name;
                p->state = PLUGIN_FAILED;
                p->status = error;
                log(LOG_ERROR, name, error);
            }
            p->data_dir = data_root() + "/" + p->id();
            all().push_back(std::move(made));
        }
    }
}

std::vector<Plugin*> load_order()
{
    std::vector<Plugin*> nodes;
    for (auto& up : all())
        if (!up->console && up->manifest_ok)
            nodes.push_back(up.get());
    auto rank = [&](Plugin* p) {
        auto it = std::find(order.begin(), order.end(), p->id());
        return std::make_tuple(it == order.end() ? 1 : 0, (long)(it - order.begin()), p->shipped ? 0 : 1, p->id());
    };
    std::sort(nodes.begin(), nodes.end(), [&](Plugin* a, Plugin* b) { return rank(a) < rank(b); });
    std::vector<Plugin*> out;
    std::vector<bool> done(nodes.size(), false);
    for (size_t round = 0; round < nodes.size(); round++) {
        size_t pick = nodes.size();
        for (size_t k = 0; k < nodes.size() && pick == nodes.size(); k++) {
            if (done[k])
                continue;
            bool ready = true;
            for (const Dependency& d : nodes[k]->manifest.depends)
                for (size_t j = 0; j < nodes.size(); j++)
                    if (!done[j] && j != k && nodes[j]->id() == d.id)
                        ready = false;
            if (ready)
                pick = k;
        }
        for (size_t k = 0; k < nodes.size() && pick == nodes.size(); k++) /* a cycle: it fails at load */
            if (!done[k])
                pick = k;
        done[pick] = true;
        out.push_back(nodes[pick]);
    }
    return out;
}

bool plugin_enabled(const Plugin* p)
{
    auto it = enabled_override.find(p->id());
    return it != enabled_override.end() ? it->second : p->manifest.default_enabled;
}

void set_enabled(const std::string& id, bool enabled)
{
    enabled_override[id] = enabled;
    config_changed();
    save_settings_now();
}

void config_changed() { dirty_config = true; }

bool load(Plugin* p)
{
    if (p->console || !p->manifest_ok)
        return false;
    if (p->state == PLUGIN_LOADED)
        return true;
    p->waiting = false;
    for (const Dependency& d : p->manifest.depends) {
        Plugin* dep = find(d.id);
        std::string why;
        if (!dep || !dep->manifest_ok)
            why = "needs plugin " + d.id + " (not installed)";
        else if (dep->state != PLUGIN_LOADED)
            why = "needs plugin " + d.id + " (" + state_name(dep->state) + ")";
        else if (!d.min_version.empty() && compare_versions(dep->manifest.version, d.min_version) < 0)
            why = "needs " + d.id + " " + d.min_version + " or newer (" + dep->manifest.version + " is installed)";
        if (!why.empty()) {
            p->state = PLUGIN_FAILED;
            p->status = why;
            p->waiting = true;
            log(LOG_ERROR, p->id(), why);
            return false;
        }
    }
    std::string code, error;
    if (!read_file(p->dir + "/" + p->manifest.entry, &code, RL_MAX_FILE, &error)) {
        p->state = PLUGIN_FAILED;
        p->status = "cannot read " + p->manifest.entry + ": " + error;
        log(LOG_ERROR, p->id(), p->status);
        return false;
    }
    p->hash = content_hash(p->dir);
    p->granted.clear();
    ++p->generation;
    p->fault_times.clear();
    p->disable_reason.clear();
    if (!open_state(p, &error)) {
        p->state = PLUGIN_FAILED;
        p->status = first_line(error);
        return false;
    }
    p->state = PLUGIN_LOADED; /* its main chunk may already register, emit and call */
    std::string chunk = "@" + p->id() + "/" + p->manifest.entry;
    bool ok = run(p, "load", BUDGET_LOAD, [&](lua_State* L) {
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 0);
    }, &error);
    if (!ok) {
        unload(p, PLUGIN_FAILED, "its main chunk failed: " + first_line(error));
        return false;
    }
    p->status = "loaded";
    log(LOG_INFO, p->id(), "loaded (version " + p->manifest.version + ")");
    Value payload = Value::new_table();
    payload.set("id", Value::str(p->id()));
    emit("plugin.loaded", payload);
    return true;
}

void unload(Plugin* p, int new_state, const std::string& status)
{
    bool was_loaded = p->state == PLUGIN_LOADED && p->L;
    if (was_loaded)
        emit_to(p, "unload", Value());
    for (PluginHook hook : unload_hooks())
        hook(p);
    drop_events(p->id());
    drop_registry(p->id());
    close_state(p);
    ++p->generation;
    p->state = new_state;
    p->status = status;
    p->granted.clear();
    p->disable_reason.clear();
    p->fault_times.clear();
    if (was_loaded) {
        Value payload = Value::new_table();
        payload.set("id", Value::str(p->id()));
        emit("plugin.unloaded", payload);
    }
}

void unload_with_dependents(Plugin* p, int new_state, const std::string& status)
{
    std::vector<Plugin*> dependents = dependents_of(p);
    for (auto it = dependents.rbegin(); it != dependents.rend(); ++it) {
        unload(*it, PLUGIN_FAILED, "needs plugin " + p->id() + " (" + status + ")");
        (*it)->waiting = true;
    }
    unload(p, new_state, status);
}

void reload(Plugin* p)
{
    if (p->console)
        return;
    std::vector<Plugin*> dependents = dependents_of(p);
    for (auto it = dependents.rbegin(); it != dependents.rend(); ++it)
        unload(*it, PLUGIN_OFF, "reloading " + p->id());
    if (p->state == PLUGIN_LOADED)
        unload(p, PLUGIN_OFF, "reloading");
    Manifest m;
    std::string error;
    if (!check_manifest(p->dir, p->shipped, m, &error)) {
        p->manifest_ok = false;
        p->state = PLUGIN_FAILED;
        p->status = error;
        log(LOG_ERROR, p->id(), error);
    } else {
        p->manifest = m;
        p->manifest_ok = true;
        if (plugin_enabled(p))
            load(p);
        else {
            p->state = PLUGIN_OFF;
            p->status = "off (disabled in the plugin list)";
        }
    }
    for (Plugin* d : dependents)
        if (plugin_enabled(d))
            load(d);
}

void request_reload_all()
{
    scan();
    for (auto& up : all())
        if (!up->console && (up->state != PLUGIN_OFF || !up->manifest_ok))
            up->reload_requested = true;
    config_changed();
}

void settle()
{
    if (calls_in_progress() > 0)
        return;
    for (auto& up : all()) {
        Plugin* p = up.get();
        if (p->disable_reason.empty())
            continue;
        std::string reason = p->disable_reason;
        p->disable_reason.clear();
        if (p->state == PLUGIN_LOADED) {
            log(LOG_ERROR, p->id(), "disabled: " + reason + "; it reloads when its files change");
            unload_with_dependents(p, PLUGIN_DISABLED, "disabled: " + reason);
        }
    }
    for (auto& up : all())
        if (up->reload_requested) {
            up->reload_requested = false;
            reload(up.get());
        }
    if (dirty_config) {
        dirty_config = false;
        reconcile();
    }
}

void init()
{
    if (started)
        return;
    on_game_thread(); /* this thread is the game's */
    started = true;
    scan();
    reconcile();
    int found = 0, loaded = 0;
    for (auto& up : all())
        if (!up->console) {
            ++found;
            loaded += up->state == PLUGIN_LOADED;
        }
    log(LOG_INFO, "", std::to_string(found) + " plugin(s) in " + (shipped_root().empty() ? "-" : shipped_root()) + " and "
        + user_root() + ", " + std::to_string(loaded) + " loaded");
}
bool initialized() { return started; }

void tick()
{
    ++ticks;
    tick_events(pc_vsync_count ? pc_vsync_count() : ticks);
    for (Hook hook : poll_hooks())
        hook();
    if (!pc_overlay_active || !pc_overlay_active())
        frame_events(1.0 / 59.94); /* no overlay (software presenter): frames come from here */
    settle();
}

void frame(double dt)
{
    frame_events(dt);
    for (Hook hook : poll_hooks())
        hook();
    settle();
}

const std::vector<Tunable>& tunables()
{
    static const std::vector<Tunable> list = {
        { "plugin_budget_ms", "budget_ms", "Call budget (ms)", &config.budget_ms, nullptr, nullptr, 0.1, 1000 },
        { "plugin_budget_instructions", "budget_instructions", "Call budget (instructions)", nullptr,
            &config.budget_instructions, nullptr, 1000, 1e11 },
        { "plugin_load_budget_ms", "load_budget_ms", "Load budget (ms)", &config.load_budget_ms, nullptr, nullptr, 1,
            60000 },
        { "plugin_load_budget_instructions", "load_budget_instructions", "Load budget (instructions)", nullptr,
            &config.load_budget_instructions, nullptr, 1000, 1e12 },
        { "plugin_memory_mb", "memory_mb", "Memory per plugin (MB)", &config.memory_mb, nullptr, nullptr, 1, 4096 },
        { "plugin_fault_limit", "fault_limit", "Faults before disabling", nullptr, nullptr, &config.fault_limit, 1,
            1000 },
        { "plugin_fault_window", "fault_window_s", "Fault window (s)", &config.fault_window_s, nullptr, nullptr, 0.1,
            3600 },
        { "plugin_watch_vsyncs", "watch_vsyncs", "Hot reload check (vsyncs)", nullptr, nullptr, &config.watch_vsyncs, 1,
            3600 },
    };
    return list;
}
double get_tunable(const Tunable& t) { return t.d ? *t.d : t.i ? (double)*t.i : (double)*t.k; }
void set_tunable(const Tunable& t, double value)
{
    value = value < t.lo ? t.lo : value > t.hi ? t.hi : value;
    if (t.d)
        *t.d = value;
    else if (t.i)
        *t.i = (long long)value;
    else
        *t.k = (int)value;
    config_changed();
}

} // namespace rl

extern "C" void pc_lua_init(void)
{
    if (rl::initialized())
        return;
    rl::init();
    std::atexit(pc_lua_shutdown);
}

extern "C" void pc_lua_shutdown(void)
{
    if (!rl::initialized() || !rl::on_game_thread())
        return;
    for (rl::Hook hook : rl::flush_hooks())
        hook();
}

extern "C" void pc_lua_tick(void)
{
    if (rl::initialized())
        rl::tick();
}

extern "C" void pc_lua_frame(double dt)
{
    if (rl::initialized())
        rl::frame(dt);
}

extern "C" void pc_lua_settings_begin(void)
{
    rl::enabled_override.clear();
    rl::order.clear();
}

extern "C" int pc_lua_settings_parse(const char* key, const char* rest)
{
    char a[256], b[1024];
    long long n;
    if (!std::strcmp(key, "plugins_elevated")) {
        if (std::sscanf(rest, "%lld", &n) == 1)
            rl::config.elevated = n != 0;
        rl::config_changed();
        return 1;
    }
    if (!std::strcmp(key, "plugin_enabled")) {
        if (std::sscanf(rest, "%255s %lld", a, &n) == 2)
            rl::enabled_override[a] = n != 0;
        rl::config_changed();
        return 1;
    }
    if (!std::strcmp(key, "plugin_order")) {
        rl::order.clear();
        if (std::sscanf(rest, "%1023s", b) == 1)
            for (char* id = std::strtok(b, ","); id; id = std::strtok(nullptr, ","))
                rl::order.push_back(id);
        rl::config_changed();
        return 1;
    }
    for (const rl::Tunable& t : rl::tunables()) {
        if (std::strcmp(key, t.key))
            continue;
        double value;
        if (std::sscanf(rest, "%lf", &value) == 1)
            rl::set_tunable(t, value);
        return 1;
    }
    return 0;
}

extern "C" void pc_lua_settings_end(void) { rl::config_changed(); }

extern "C" void pc_lua_settings_write(FILE* f)
{
    std::fprintf(f, "plugins_elevated %d\n", rl::config.elevated ? 1 : 0);
    for (const rl::Tunable& t : rl::tunables())
        std::fprintf(f, "%s %.10g\n", t.key, rl::get_tunable(t));
    for (const auto& e : rl::enabled_override)
        std::fprintf(f, "plugin_enabled %s %d\n", e.first.c_str(), e.second ? 1 : 0);
    if (!rl::order.empty()) {
        std::string joined;
        for (const std::string& id : rl::order)
            joined += (joined.empty() ? "" : ",") + id;
        std::fprintf(f, "plugin_order %s\n", joined.c_str());
    }
}
```

- [ ] **Step 7: Add the files to the probe** — append `src/lua/lua_plugins.cpp src/lua/lua_caps.cpp` to `ROOD_LUA_RUNTIME` in `pc/CMakeLists.txt`.

- [ ] **Step 8: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -E "error|warning" | head
pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: nothing from the grep; `lua_probe: ok`.

- [ ] **Step 9: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua plugins: manifests, load order, load/unload, fault policy

Plugins are folders with plugin.lua or plugin.json; ids are namespaced
and match their folder ('rood.' is for shipped plugins); dependencies
load first; a plugin that faults 5 times in 10 s is disabled at the next
safe point. Enable/disable, order and the budgets are settings lines.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 14: Storage

**Files:**
- Create: `pc/src/lua/lua_storage.cpp`
- Modify: `pc/src/lua/rl_internal.h`, `pc/src/lua/lua_api_core.cpp`, `pc/src/lua/lua_plugins.cpp`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* rood.storage: plain data kept as JSON in the plugin's data folder (kept
 * across a reload); its own folder read-only; data files in the data folder. */
static void storage()
{
    rl::Plugin* a = rl::find("probe.alpha");
    CHECK(eval_json(a, "rood.storage.set('answer', { n = 42, list = { 1, 2 } }) return rood.storage.get('answer').n") == "42");
    CHECK(eval_json(a, "rood.storage.get('missing', 'fallback')") == "\"fallback\"");
    CHECK(contains(eval_error(a, "rood.storage.set('f', print)"), "function"));
    CHECK(eval_json(a, "rood.storage.flush()") == "true");
    std::string text, why;
    CHECK(rl::read_file(scratch + "/data/probe.alpha/store.json", &text, 4096, &why));
    CHECK(contains(text, "\"n\":42") && contains(text, "\"list\":[1,2]"));
    CHECK(contains(eval_json(a, "rood.storage.read('main.lua')"), "rood.on('tick'"));
    CHECK(contains(eval_error(a, "rood.storage.read('../probe.beta/main.lua')"), "bad path"));
    CHECK(eval_json(a, "rood.storage.write_data('notes/today.txt', 'x') return rood.storage.read_data('notes/today.txt')")
        == "\"x\"");
    CHECK(contains(eval_error(a, "rood.storage.write_data('store.json', '{}')"), "reserved"));
    CHECK(eval_json(a, "rood.storage.keys()[1], #rood.storage.list_data()") == "\"answer\",1");
    CHECK(eval_json(rl::console("@probe-nostore"), "pcall(rood.storage.get, 'x')") != "true");
    a->reload_requested = true;
    pc_lua_tick();
    CHECK(eval_json(rl::find("probe.alpha"), "rood.storage.get('answer').list[2]") == "2");
}
```

and in `main`: `storage();` after `plugins_on_disk();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `FAIL` lines from `storage` (`rood.storage` is nil); `lua_probe: N failure(s)`.

- [ ] **Step 3: Declare storage** — add to `pc/src/lua/rl_internal.h` before `} // namespace rl`:

```cpp
/* ---- lua_storage.cpp ---- */
void open_storage(lua_State* L, Plugin* p); /* rood.storage */
bool flush_storage(Plugin* p);              /* its store.json now, if anything changed */
void flush_storage_due();                   /* stores changed more than a second ago */
void drop_storage(Plugin* p);               /* writes it and forgets it (an unload) */
```

- [ ] **Step 4: Write `pc/src/lua/lua_storage.cpp`**

```cpp
// rood.storage (pc/docs/Extensions.md, "Storage"): a plugin's own key/value
// store (plain data only, at most 1 MB, saved as JSON in its data folder,
// <ROOD_PLUGIN_DATA>/<id>/store.json, a second after a change, on unload and
// at exit) and its files: read() reads the plugin's own folder (its code
// and data as installed; no capability needed), read_data / write_data /
// list_data its data folder. Nothing outside those two folders; the fs.*
// capabilities (lua_caps.cpp) go further.
#include "rl_internal.h"
#include "light_prefabs.h"

#include <algorithm>
#include <cstring>

namespace rl {
namespace {
const size_t STORE_LIMIT = 1u << 20;

struct Store {
    Value data = Value::new_table();
    bool loaded = false, dirty = false;
    double since = 0;
};
std::map<std::string, Store>& stores()
{
    static auto* map = new std::map<std::string, Store>();
    return *map;
}

Plugin* need_storage(lua_State* L, const char* fn)
{
    Plugin* p = plugin_of(L);
    if (p->data_dir.empty())
        luaL_error(L, "rood.storage.%s: this sandbox has no storage (plugins from a folder have)", fn);
    return p;
}

Store& store_of(Plugin* p)
{
    Store& s = stores()[p->id()];
    if (!s.loaded) {
        s.loaded = true;
        std::string text, why;
        prefab::Json j;
        if (read_file(p->data_dir + "/store.json", &text, STORE_LIMIT * 2, &why)) {
            if (prefab::json_parse(text.c_str(), j, &why) && j.type == prefab::Json::Object)
                s.data = from_json(j);
            else
                log(LOG_WARN, p->id(), "store.json is unreadable (" + why + "); starting empty");
        }
    }
    return s;
}

bool holds_function(const Value& v)
{
    if (v.kind == Value::FUNCTION)
        return true;
    for (const auto& kv : v.table)
        if (holds_function(kv.first) || holds_function(kv.second))
            return true;
    return false;
}

std::string checked_name(lua_State* L, const char* fn, int arg)
{
    const char* name = luaL_checkstring(L, arg);
    std::string why;
    if (!safe_relative(name, &why))
        luaL_error(L, "rood.storage.%s: bad path '%s': %s", fn, name, why.c_str());
    return name;
}

int storage_get(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    lua_settop(L, 2);
    Store& s = store_of(need_storage(L, "get"));
    const Value* v = s.data.get(key);
    if (v && v->kind != Value::NIL)
        push(L, *v);
    else
        lua_pushvalue(L, 2);
    return 1;
}

int storage_set(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    Plugin* p = need_storage(L, "set");
    size_t length = std::strlen(key);
    if (length == 0 || length > 128)
        return luaL_error(L, "rood.storage.set: a key is 1 to 128 characters");
    Value v = lua_isnoneornil(L, 2) ? Value() : Value::from_lua(L, 2);
    if (holds_function(v))
        return luaL_error(L, "rood.storage.set: '%s' holds a function; storage keeps plain data", key);
    Store& s = store_of(p);
    Value updated = s.data;
    auto& t = updated.table;
    t.erase(std::remove_if(t.begin(), t.end(),
                [&](const std::pair<Value, Value>& kv) { return kv.first.kind == Value::STRING && kv.first.s == key; }),
        t.end());
    if (v.kind != Value::NIL)
        t.emplace_back(Value::str(key), v);
    if (to_json(updated).size() > STORE_LIMIT)
        return luaL_error(L, "rood.storage.set: storage is limited to 1 MB per plugin");
    s.data = std::move(updated);
    if (!s.dirty) {
        s.dirty = true;
        s.since = now_seconds();
    }
    return 0;
}

int storage_remove(lua_State* L)
{
    lua_settop(L, 1);
    lua_pushnil(L);
    return storage_set(L);
}

int storage_keys(lua_State* L)
{
    Store& s = store_of(need_storage(L, "keys"));
    std::vector<std::string> keys;
    for (const auto& kv : s.data.table)
        if (kv.first.kind == Value::STRING && kv.second.kind != Value::NIL)
            keys.push_back(kv.first.s);
    std::sort(keys.begin(), keys.end());
    lua_createtable(L, (int)keys.size(), 0);
    for (size_t k = 0; k < keys.size(); k++) {
        lua_pushstring(L, keys[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}

int storage_flush(lua_State* L)
{
    lua_pushboolean(L, flush_storage(need_storage(L, "flush")));
    return 1;
}

int read_in(lua_State* L, const std::string& folder, const std::string& name)
{
    std::string text, why;
    if (!read_file(folder + "/" + name, &text, RL_MAX_FILE, &why)) {
        luaL_pushfail(L);
        lua_pushstring(L, why.c_str());
        return 2;
    }
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

int storage_read(lua_State* L)
{
    std::string name = checked_name(L, "read", 1);
    Plugin* p = plugin_of(L);
    if (p->dir.empty())
        return luaL_error(L, "rood.storage.read: this sandbox has no folder");
    return read_in(L, p->dir, name);
}

int storage_read_data(lua_State* L)
{
    std::string name = checked_name(L, "read_data", 1);
    return read_in(L, need_storage(L, "read_data")->data_dir, name);
}

int storage_write_data(lua_State* L)
{
    std::string name = checked_name(L, "write_data", 1);
    size_t length;
    const char* text = luaL_checklstring(L, 2, &length);
    Plugin* p = need_storage(L, "write_data");
    if (name == "store.json" || name == "settings.json")
        return luaL_error(L, "rood.storage.write_data: '%s' is reserved (rood.storage.set, rood.settings)", name.c_str());
    if (length > RL_MAX_FILE)
        return luaL_error(L, "rood.storage.write_data: larger than %d KB", (int)(RL_MAX_FILE / 1024));
    std::string why;
    if (!write_file(p->data_dir + "/" + name, std::string(text, length), &why))
        return luaL_error(L, "rood.storage.write_data: %s", why.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int storage_list_data(lua_State* L)
{
    Plugin* p = need_storage(L, "list_data");
    std::vector<std::string> names;
    for (const std::string& name : list_dir(p->data_dir))
        if (name != "store.json" && name != "settings.json")
            names.push_back(name);
    lua_createtable(L, (int)names.size(), 0);
    for (size_t k = 0; k < names.size(); k++) {
        lua_pushstring(L, names[k].c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}
} // namespace

bool flush_storage(Plugin* p)
{
    auto it = stores().find(p->id());
    if (it == stores().end() || !it->second.dirty)
        return true;
    std::string why;
    if (!write_file(p->data_dir + "/store.json", to_json(it->second.data), &why)) {
        log(LOG_ERROR, p->id(), "storage: " + why);
        return false;
    }
    it->second.dirty = false;
    return true;
}

void flush_storage_due()
{
    double now = now_seconds();
    for (auto& kv : stores())
        if (kv.second.dirty && now - kv.second.since >= 1.0)
            if (Plugin* p = find(kv.first))
                flush_storage(p);
}

void drop_storage(Plugin* p)
{
    flush_storage(p);
    stores().erase(p->id());
}

void open_storage(lua_State* L, Plugin*)
{
    lua_newtable(L);
    set_cfunction(L, -1, "get", storage_get);
    set_cfunction(L, -1, "set", storage_set);
    set_cfunction(L, -1, "remove", storage_remove);
    set_cfunction(L, -1, "keys", storage_keys);
    set_cfunction(L, -1, "flush", storage_flush);
    set_cfunction(L, -1, "read", storage_read);
    set_cfunction(L, -1, "read_data", storage_read_data);
    set_cfunction(L, -1, "write_data", storage_write_data);
    set_cfunction(L, -1, "list_data", storage_list_data);
    lock_table(L);
    lua_setfield(L, -2, "storage");
}

} // namespace rl
```

- [ ] **Step 5: Wire it in**
  - `lua_api_core.cpp` `open_rood`: after `open_registry(L, p);` add `open_storage(L, p);`.
  - `lua_plugins.cpp` `unload`: after `drop_registry(p->id());` add `drop_storage(p);`.
  - `lua_plugins.cpp` `settle`: as its last line add `flush_storage_due();`.
  - `lua_plugins.cpp` `pc_lua_shutdown`: after the `flush_hooks` loop add:

```cpp
    for (auto& up : rl::all())
        if (up->L)
            rl::flush_storage(up.get());
```

  - `pc/CMakeLists.txt`: append `src/lua/lua_storage.cpp` to `ROOD_LUA_RUNTIME`.

- [ ] **Step 6: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `lua_probe: ok`.

- [ ] **Step 7: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua: rood.storage, a plugin's key/value store and data files

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 15: Capabilities — grants, the gate, `rood.fs`, refusing stubs, the audit log

**Files:**
- Modify: `pc/src/lua/lua_caps.cpp`, `pc/src/lua/rl_internal.h`, `pc/src/lua/lua_api_core.cpp`, `pc/src/lua/lua_plugins.cpp`, `pc/tests/lua_probe.cpp`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* Ungranted capabilities are absent; a grant needs the master switch and the
 * plugin's current code; fs is scoped; unimplemented ones refuse plainly;
 * elevated calls are audited; revoking or changing the code takes them away. */
static void capabilities()
{
    std::string root = scratch + "/plugins";
    write_text(root + "/probe.caps/plugin.lua",
        "return { id = 'probe.caps', api_version = 1, capabilities = { 'fs.read', 'fs.write', 'net:example.com', 'exec' } }\n");
    write_text(root + "/probe.caps/main.lua", "\n");
    write_text(root + "/probe.badcap/plugin.lua", "return { id = 'probe.badcap', api_version = 1, capabilities = { 'fs.read:../up' } }\n");
    write_text(root + "/probe.badcap/main.lua", "\n");
    rl::scan();
    rl::config_changed();
    pc_lua_tick();
    rl::Plugin* c = rl::find("probe.caps");
    CHECK(c && c->state == rl::PLUGIN_LOADED);
    CHECK(state_of("probe.badcap") == "failed" && contains(rl::find("probe.badcap")->status, ".."));
    CHECK(eval_json(c, "rood.fs, rood.net, rood.exec, #rood.plugin.missing") == "null,null,null,4");
    std::string error;
    CHECK(!rl::grant("probe.caps", "native", false, &error) && contains(error, "does not declare"));
    CHECK(rl::grant("probe.caps", "fs.read", false, &error) && rl::grant("probe.caps", "fs.write", false, &error)
        && rl::grant("probe.caps", "net:example.com", false, &error));
    pc_lua_tick();
    CHECK(eval_json(rl::find("probe.caps"), "rood.fs") == "null"); /* the master switch is off */
    rl::config.elevated = true;
    rl::config_changed();
    pc_lua_tick();
    c = rl::find("probe.caps");
    CHECK((c->granted == std::vector<std::string>{ "fs.read", "fs.write", "net:example.com" }));
    CHECK(eval_json(c, "rood.fs.write('out/a.txt', 'hi'), rood.fs.read('out/a.txt'), rood.fs.exists('out/a.txt')")
        == "true,\"hi\",true");
    CHECK(contains(eval_error(c, "rood.fs.write('../escape.txt', 'x')"), "bad path"));
    CHECK(contains(eval_error(c, "rood.fs.read('/etc/hostname')"), "bad path"));
    CHECK(contains(eval_error(c, "rood.net.request('https://example.com')"), "not available in this version"));
    CHECK(eval_json(c, "rood.exec, rood.plugin.elevated, rood.plugin.missing[1]") == "null,true,\"exec\"");
    CHECK(!rl::audit_lines().empty() && contains(rl::audit_lines().back(), "probe.caps net"));
    std::string written = settings_text();
    CHECK(contains(written, "plugin_grant probe.caps " + c->hash + " fs.write\n") && contains(written, "plugins_elevated 1\n"));
    CHECK(rl::grant("probe.caps", "fs.write", true, &error));
    pc_lua_tick();
    CHECK(eval_json(rl::find("probe.caps"), "rood.fs.write, type(rood.fs.read)") == "null,\"function\"");
    write_text(root + "/probe.caps/main.lua", "-- changed\n");
    rl::find("probe.caps")->reload_requested = true;
    pc_lua_tick();
    c = rl::find("probe.caps");
    CHECK(c->granted.empty() && rl::pending_grants(c).size() == 2 && eval_json(c, "rood.fs") == "null");
    rl::config.elevated = false;
    rl::config_changed();
    pc_lua_tick();
}
```

and in `main`: `capabilities();` after `storage();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `error: 'grant' is not a member of 'rl'`.

- [ ] **Step 3: Declare grants and the gate** — add to the `lua_caps.cpp` section of `pc/src/lua/rl_internal.h`:

```cpp
struct Grant {
    std::string plugin, hash, capability; /* settings plugin_grant <plugin> <hash> <capability> */
};
std::vector<Grant>& grants();
std::vector<std::string> active_grants(const Plugin* p);  /* declared, granted for its hash, master switch on */
std::vector<std::string> pending_grants(const Plugin* p); /* granted for another hash: asked again */
/* The user's grant (later the dialog; now RPC lua {action "grant"} and the probe): the
 * capability must be declared; tied to the plugin's current hash; saves settings.cfg. */
bool grant(const std::string& plugin, const std::string& capability, bool revoke, std::string* error);
void open_capabilities(lua_State* L, Plugin* p); /* rood.fs, rood.net, ... for p->granted */
void audit(Plugin* p, const std::string& capability, const std::string& detail);
const std::deque<std::string>& audit_lines();
```

- [ ] **Step 4: Implement them** — in `pc/src/lua/lua_caps.cpp` add to the includes:

```cpp
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
```

and append inside `namespace rl` (after `capability_ok`):

```cpp
namespace {
std::deque<std::string>& audit_buffer()
{
    static auto* lines = new std::deque<std::string>();
    return *lines;
}

std::string name_of(const std::string& capability) { return capability.substr(0, capability.find(':')); }

/* The plugin's own folder and the folders its grants of `which` (fs.read / fs.write) name. */
bool roots_for(Plugin* p, const char* which, std::vector<std::string>& roots)
{
    roots.clear();
    bool any = false;
    for (const std::string& cap : p->granted) {
        if (name_of(cap) != which)
            continue;
        any = true;
        size_t colon = cap.find(':');
        if (colon != std::string::npos) {
            std::string scope = cap.substr(colon + 1);
            for (char& ch : scope)
                if (ch == '\\')
                    ch = '/';
            while (scope.size() > 1 && scope.back() == '/')
                scope.pop_back();
            roots.push_back(scope);
        }
    }
    if (any)
        roots.insert(roots.begin(), p->dir);
    return any;
}

/* POSIX: a link inside the folder must not lead out of it. */
void check_inside(lua_State* L, const std::string& root, const std::string& full, const char* path)
{
#ifndef _WIN32
    char real_root[PATH_MAX], real_parent[PATH_MAX];
    std::string parent = full == root ? full : full.substr(0, full.find_last_of('/'));
    if (!realpath(root.c_str(), real_root) || !realpath(parent.c_str(), real_parent))
        return; /* not there yet: a write creates it below the root (checked lexically) */
    size_t n = std::strlen(real_root);
    if (std::strncmp(real_parent, real_root, n) != 0 || (real_parent[n] != '/' && real_parent[n] != 0))
        luaL_error(L, "rood.fs: '%s' leads outside its folder (a link)", path);
#else
    (void)L, (void)root, (void)full, (void)path;
#endif
}

std::string resolve(lua_State* L, const char* which, const char* path)
{
    Plugin* p = plugin_of(L);
    std::vector<std::string> roots;
    if (!roots_for(p, which, roots))
        luaL_error(L, "rood.fs: '%s' is not granted", which);
    std::string s = path;
    for (char& ch : s)
        if (ch == '\\')
            ch = '/';
    std::string why;
    for (size_t k = 1; k < roots.size(); k++) { /* a granted folder, as the manifest names it */
        const std::string& root = roots[k];
        if (s != root && (s.size() <= root.size() || s.compare(0, root.size(), root) != 0 || s[root.size()] != '/'))
            continue;
        std::string rest = s.size() > root.size() ? s.substr(root.size() + 1) : std::string();
        if (!rest.empty() && !safe_relative(rest, &why))
            luaL_error(L, "rood.fs: bad path '%s': %s", path, why.c_str());
        check_inside(L, root, s, path);
        return s;
    }
    if (!safe_relative(s, &why))
        luaL_error(L, "rood.fs: bad path '%s': %s (paths are relative to the plugin's folder or inside a granted folder)",
            path, why.c_str());
    std::string full = p->dir + "/" + s;
    check_inside(L, p->dir, full, path);
    return full;
}

int fs_read(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    std::string full = resolve(L, "fs.read", path);
    audit(plugin_of(L), "fs.read", path);
    std::string text, why;
    if (!read_file(full, &text, RL_MAX_FILE * 4, &why)) {
        luaL_pushfail(L);
        lua_pushstring(L, why.c_str());
        return 2;
    }
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}
int fs_exists(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    std::string full = resolve(L, "fs.read", path);
    lua_pushboolean(L, file_stat(full, nullptr, nullptr, nullptr));
    return 1;
}
int fs_list(lua_State* L)
{
    const char* path = luaL_optstring(L, 1, ".");
    std::string full = std::strcmp(path, ".") ? resolve(L, "fs.read", path) : plugin_of(L)->dir;
    audit(plugin_of(L), "fs.read", std::string(path) + " (list)");
    std::vector<std::string> names = list_dir(full);
    lua_createtable(L, (int)names.size(), 0);
    for (size_t k = 0; k < names.size(); k++) {
        bool is_dir = false;
        file_stat(full + "/" + names[k], nullptr, nullptr, &is_dir);
        lua_pushstring(L, (names[k] + (is_dir ? "/" : "")).c_str());
        lua_rawseti(L, -2, (lua_Integer)k + 1);
    }
    return 1;
}
int fs_write(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    size_t length;
    const char* text = luaL_checklstring(L, 2, &length);
    std::string full = resolve(L, "fs.write", path);
    if (length > RL_MAX_FILE * 4)
        return luaL_error(L, "rood.fs.write: larger than %d MB", (int)(RL_MAX_FILE * 4 >> 20));
    audit(plugin_of(L), "fs.write", std::string(path) + " (" + std::to_string(length) + " bytes)");
    std::string why;
    if (!write_file(full, std::string(text, length), &why))
        return luaL_error(L, "rood.fs.write: %s", why.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int fs_remove(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    std::string full = resolve(L, "fs.write", path);
    if (full == plugin_of(L)->dir)
        return luaL_error(L, "rood.fs.remove: not the plugin's own folder");
    audit(plugin_of(L), "fs.write", std::string(path) + " (remove)");
    lua_pushboolean(L, std::remove(full.c_str()) == 0);
    return 1;
}

/* A granted capability this version does not provide yet: it says so, plainly. */
int stub_call(lua_State* L)
{
    const char* cap = lua_tostring(L, lua_upvalueindex(1));
    const char* fn = lua_tostring(L, lua_upvalueindex(2));
    audit(plugin_of(L), cap, std::string(fn) + " (not available)");
    return luaL_error(L, "%s: not available in this version of rood-boi (the '%s' capability is granted but not "
                         "implemented yet)", fn, cap);
}
} // namespace

std::vector<Grant>& grants()
{
    static auto* list = new std::vector<Grant>();
    return *list;
}

std::vector<std::string> active_grants(const Plugin* p)
{
    std::vector<std::string> out;
    if (!config.elevated || p->console)
        return out;
    for (const std::string& cap : p->manifest.capabilities)
        for (const Grant& g : grants())
            if (g.plugin == p->id() && g.hash == p->hash && g.capability == cap) {
                out.push_back(cap);
                break;
            }
    return out;
}

std::vector<std::string> pending_grants(const Plugin* p)
{
    std::vector<std::string> out;
    for (const Grant& g : grants())
        if (g.plugin == p->id() && g.hash != p->hash)
            out.push_back(g.capability);
    return out;
}

bool grant(const std::string& plugin, const std::string& capability, bool revoke, std::string* error)
{
    Plugin* p = find(plugin);
    if (!p || p->console || !p->manifest_ok) {
        *error = "no plugin '" + plugin + "'";
        return false;
    }
    const std::vector<std::string>& declared = p->manifest.capabilities;
    if (std::find(declared.begin(), declared.end(), capability) == declared.end()) {
        *error = "plugin '" + plugin + "' does not declare '" + capability + "'";
        return false;
    }
    if (p->hash.empty())
        p->hash = content_hash(p->dir);
    std::vector<Grant>& list = grants();
    list.erase(std::remove_if(list.begin(), list.end(),
                   [&](const Grant& g) { return g.plugin == plugin && g.capability == capability; }),
        list.end());
    if (!revoke)
        list.push_back({ plugin, p->hash, capability });
    audit(p, revoke ? "revoke" : "grant", capability);
    config_changed();
    save_settings_now();
    return true;
}

void audit(Plugin* p, const std::string& capability, const std::string& detail)
{
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &tm);
    std::string line = std::string(stamp) + " " + p->id() + " " + capability + " " + detail;
    audit_buffer().push_back(line);
    while (audit_buffer().size() > 500)
        audit_buffer().pop_front();
    make_dirs(data_root());
    if (FILE* f = std::fopen((data_root() + "/audit.log").c_str(), "a")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fclose(f);
    }
    log(LOG_INFO, p->id(), "[audit] " + capability + " " + detail);
}
const std::deque<std::string>& audit_lines() { return audit_buffer(); }

void open_capabilities(lua_State* L, Plugin* p)
{
    bool read = false, write = false;
    for (const std::string& cap : p->granted) {
        read |= name_of(cap) == "fs.read";
        write |= name_of(cap) == "fs.write";
    }
    if (read || write) {
        lua_newtable(L);
        if (read) {
            set_cfunction(L, -1, "read", fs_read);
            set_cfunction(L, -1, "exists", fs_exists);
            set_cfunction(L, -1, "list", fs_list);
        }
        if (write) {
            set_cfunction(L, -1, "write", fs_write);
            set_cfunction(L, -1, "remove", fs_remove);
        }
        lock_table(L);
        lua_setfield(L, -2, "fs");
    }
    struct Stub {
        const char* capability;
        const char* table;
        const char* field;
    };
    static const Stub stubs[] = {
        { "net", "net", "request" }, { "exec", "exec", "run" }, { "memory.read", "memory", "read" },
        { "memory.write", "memory", "write" }, { "flags.write", "flags", "write" }, { "native", "native", "load" },
    };
    for (const char* table : { "net", "exec", "memory", "flags", "native" }) {
        bool made = false;
        for (const Stub& s : stubs) {
            if (std::strcmp(s.table, table))
                continue;
            bool granted = false;
            for (const std::string& cap : p->granted)
                granted |= name_of(cap) == s.capability;
            if (!granted)
                continue;
            if (!made) {
                lua_newtable(L);
                made = true;
            }
            lua_pushstring(L, s.capability);
            lua_pushfstring(L, "rood.%s.%s", s.table, s.field);
            lua_pushcclosure(L, stub_call, 2);
            lua_setfield(L, -2, s.field);
        }
        if (made) {
            lock_table(L);
            lua_setfield(L, -2, table);
        }
    }
}
```

`grant` saves through `save_settings_now()` (Task 13).

Update the header comment of `lua_caps.cpp` (replace its last sentence) with: `Implemented in M0: fs.read and fs.write (the plugin's own folder and the folders the grant names); the others exist when granted and refuse with a plain message. Every elevated call is audited (the log, <ROOD_PLUGIN_DATA>/audit.log, RPC lua {action "audit"}).`

- [ ] **Step 5: Gate the grants at load and reconcile them, and keep them in settings.cfg** — in `pc/src/lua/lua_plugins.cpp`:
  - `load`: replace `p->granted.clear();` with `p->granted = active_grants(p);`.
  - `reconcile`: in the loop, before the `else if (!want && p->state == PLUGIN_LOADED)` branch, add:

```cpp
        else if (want && p->state == PLUGIN_LOADED && active_grants(p) != p->granted)
            reload(p);
```

  - `pc_lua_settings_begin`: add `rl::grants().clear();`.
  - `pc_lua_settings_parse`: before the tunables loop add:

```cpp
    if (!std::strcmp(key, "plugin_grant")) {
        char hash[80], cap[512];
        if (std::sscanf(rest, "%255s %79s %511s", a, hash, cap) == 3) {
            bool known = false;
            for (const rl::Grant& g : rl::grants())
                known |= g.plugin == a && g.hash == hash && g.capability == cap;
            if (!known)
                rl::grants().push_back({ a, hash, cap });
        }
        rl::config_changed();
        return 1;
    }
```

  - `pc_lua_settings_write`: at the end add:

```cpp
    for (const rl::Grant& g : rl::grants())
        std::fprintf(f, "plugin_grant %s %s %s\n", g.plugin.c_str(), g.hash.c_str(), g.capability.c_str());
```

- [ ] **Step 6: Open them in `rood`** — `lua_api_core.cpp` `open_rood`: after `open_storage(L, p);` add `open_capabilities(L, p);`.

- [ ] **Step 7: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -E "error|warning" | head
pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: nothing from the grep; `lua_probe: ok`.

- [ ] **Step 8: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp
git commit -m "$(cat <<'EOF'
Lua capabilities: grants tied to the code's hash, rood.fs, audit log

A declared capability's functions exist only when the master switch is
on and the user granted it for the plugin's current code. fs.read and
fs.write reach the plugin's folder and the folders granted; net, exec,
memory, flags and native refuse with a plain message. Elevated calls are
audited; grants live in settings.cfg.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 16: Hot reload

**Files:**
- Modify: `pc/src/lua/lua_plugins.cpp`, `pc/src/lua/rl_internal.h`, `pc/tests/lua_probe.cpp`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* Hot reload: an edit reloads the plugin and what depends on it, with
 * everything it registered gone first; a new folder loads; a broken edit
 * fails and fixing it reloads. */
static void hot_reload()
{
    std::string root = scratch + "/plugins";
    rl::Plugin* a = rl::find("probe.alpha");
    unsigned generation = a->generation;
    size_t handlers = rl::handler_count("probe.alpha");
    write_text(root + "/probe.alpha/main.lua",
        "ticks = 0\n"
        "rood.on('tick', function() ticks = ticks + 1 end)\n"
        "rood.registry.register('probe.entry', 'probe.alpha/one-v2', { version = 2 })\n");
    rl::config.watch_vsyncs = 2;
    pc_lua_tick();
    pc_lua_tick();
    a = rl::find("probe.alpha");
    CHECK(a->generation > generation && a->state == rl::PLUGIN_LOADED);
    CHECK(rl::handler_count("probe.alpha") == handlers);
    std::vector<std::string> ids;
    for (const rl::RegistryEntry* e : rl::registry_list("probe.entry"))
        ids.push_back(e->id);
    CHECK((ids == std::vector<std::string>{ "probe.alpha/one-v2", "probe.beta/two" }));
    write_text(root + "/probe.late/plugin.lua", "return { id = 'probe.late', api_version = 1 }\n");
    write_text(root + "/probe.late/main.lua", "late = true\n");
    pc_lua_tick();
    pc_lua_tick();
    CHECK(state_of("probe.late") == "loaded");
    write_text(root + "/probe.late/main.lua", "late = \n");
    pc_lua_tick();
    pc_lua_tick();
    CHECK(state_of("probe.late") == "failed");
    write_text(root + "/probe.late/main.lua", "late = 2\n");
    pc_lua_tick();
    pc_lua_tick();
    CHECK(state_of("probe.late") == "loaded" && eval_json(rl::find("probe.late"), "late") == "2");
    rl::config.watch_vsyncs = 30;
}
```

and in `main`: `hot_reload();` after `capabilities();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 >/dev/null && pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: `FAIL ... a->generation > generation ...` and the `probe.late` checks; `lua_probe: N failure(s)`.

- [ ] **Step 3: Declare the watcher** — add to the `lua_plugins.cpp` section of `rl_internal.h`:

```cpp
void remember_files(Plugin* p); /* its code files' (mtime, size) now */
void poll_files();              /* changed plugins reload at settle(); new folders load; removed ones unload */
```

- [ ] **Step 4: Implement it** — in `pc/src/lua/lua_plugins.cpp`, inside the anonymous namespace after `code_files`, add:

```cpp
/* What the watcher compares: code files, without the tests folder (a test edit
 * does not reload the plugin: the next test run reads it). */
std::map<std::string, std::pair<long long, long long>> watched_files(const Plugin* p)
{
    std::map<std::string, std::pair<long long, long long>> out;
    for (const std::string& file : code_files(p->dir, p->manifest.tests)) {
        long long mtime = 0, size = 0;
        if (file_stat(p->dir + "/" + file, &mtime, &size, nullptr))
            out[file] = { mtime, size };
    }
    return out;
}
```

after `request_reload_all` add:

```cpp
void remember_files(Plugin* p) { p->files = watched_files(p); }

void poll_files()
{
    size_t before = all().size();
    scan();
    if (all().size() != before)
        config_changed(); /* new folders: reconcile loads the enabled ones */
    for (auto& up : all()) {
        Plugin* p = up.get();
        if (p->console || p->dir.empty())
            continue;
        bool is_dir = false;
        if (!file_stat(p->dir, nullptr, nullptr, &is_dir) || !is_dir) {
            if (p->state == PLUGIN_LOADED)
                unload_with_dependents(p, PLUGIN_OFF, "its folder was removed");
            p->manifest_ok = false;
            p->state = PLUGIN_OFF;
            p->status = "its folder was removed";
            p->files.clear();
            continue;
        }
        if (p->state == PLUGIN_OFF && p->manifest_ok)
            continue; /* not enabled: picked up when it is */
        auto now = watched_files(p);
        if (now != p->files) {
            p->files = now;
            p->reload_requested = true;
            log(LOG_INFO, p->id(), "files changed: reloading");
        }
    }
}
```

then:
  - in `scan`, right before `all().push_back(std::move(made));` add `p->files = watched_files(p);`;
  - in `load`, right after `p->hash = content_hash(p->dir);` add `remember_files(p);`;
  - in `reload`, in the failing-manifest branch (after `p->status = error;`) add `remember_files(p);`;
  - in `tick`, before `settle();` add:

```cpp
    if (config.watch_vsyncs > 0 && ticks % (unsigned)config.watch_vsyncs == 0)
        poll_files();
```

  - in `frame`, before `settle();` add:

```cpp
    static double last_poll;
    double now = now_seconds();
    if (now - last_poll >= 0.5) { /* paused redraws: edits still reload */
        last_poll = now;
        poll_files();
    }
```


Update the header comment's list with: `hot reload (code files' mtime and size every watch_vsyncs ticks and every 0.5 s of frames: the plugin and what depends on it reload, everything they registered gone first);`.

- [ ] **Step 5: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -E "error|warning" | head
pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: nothing from the grep; `lua_probe: ok`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp
git commit -m "$(cat <<'EOF'
Lua: hot reload of a plugin's files without restarting the game

Every watch_vsyncs ticks (and every 0.5 s of frames while paused) the
code files are compared; a changed plugin and what depends on it unload
everything they registered and run again. New folders load, removed
ones unload, a broken edit fails until it is fixed.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 17: `rood.test` — Lua tests inside a running game

**Files:**
- Create: `pc/src/lua/lua_test.cpp`
- Modify: `pc/src/lua/rl_internal.h`, `pc/src/lua/lua_api_core.cpp`, `pc/src/lua/lua_plugins.cpp`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
/* rood.test: cases collected from a plugin's tests/*.lua, run as coroutines
 * (wait(n) resumes after n ticks); pass / fail / skip with messages; a
 * runaway case is stopped by the budget and the plugin stays loaded. */
static void test_harness()
{
    std::string root = scratch + "/plugins";
    write_text(root + "/probe.alpha/tests/alpha_test.lua",
        "local t = rood.test\n"
        "t.case('passes', function() t.eq({ 1, { 2 } }, { 1, { 2 } }) end)\n"
        "t.case('fails', function() t.eq(1, 2, 'one') end)\n"
        "t.case('skips', function() t.skip('not here') end)\n"
        "t.case('waits', function() local before = ticks t.wait(3) t.eq(ticks, before + 3) end)\n"
        "t.case('raises', function() t.raises(function() error('x marks') end, 'marks') end)\n");
    int run = rl::start_tests("probe.alpha", "");
    std::string json = rl::test_run_json(run);
    CHECK(contains(json, "\"done\":false") && contains(json, "\"waiting\":1"));
    for (int k = 0; k < 4; k++)
        pc_lua_tick();
    json = rl::test_run_json(run);
    CHECK(contains(json, "\"done\":true") && contains(json, "\"passed\":3") && contains(json, "\"failed\":1")
        && contains(json, "\"skipped\":1"));
    CHECK(contains(json, "expected 2, got 1") && contains(json, "not here"));
    write_text(root + "/probe.alpha/tests/zz_runaway_test.lua", "rood.test.case('runaway', function() while true do end end)\n");
    run = rl::start_tests("probe.alpha", "runaway");
    json = rl::test_run_json(run);
    CHECK(contains(json, "\"done\":true") && contains(json, "budget exceeded") && contains(json, "\"failed\":1"));
    CHECK(state_of("probe.alpha") == "loaded");
    std::filesystem::remove(root + "/probe.alpha/tests/zz_runaway_test.lua");
    CHECK(contains(eval_error(rl::find("probe.alpha"), "rood.test.case('x', print)"), "only in a test file"));
}
```

and in `main`: `test_harness();` after `hot_reload();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 error
```

Expected: `error: 'start_tests' is not a member of 'rl'`.

- [ ] **Step 3: Declare the harness** — add to `pc/src/lua/rl_internal.h` before `} // namespace rl`:

```cpp
/* ---- lua_test.cpp ---- */
void open_test(lua_State* L, Plugin* p); /* rood.test */
/* Collects the cases of every loaded plugin's tests folder (or one plugin's; filter: a
 * substring of the case's name) and runs what is ready; returns the run's id. */
int start_tests(const std::string& plugin, const std::string& filter);
void step_tests(bool tick); /* a tick: waiting cases count down; ready ones run */
std::string test_run_json(int run); /* {run, done, passed, failed, skipped, waiting, results}; "" if unknown */
void drop_tests(const std::string& plugin); /* its waiting cases fail: the plugin unloaded */
```

- [ ] **Step 4: Write `pc/src/lua/lua_test.cpp`**

```cpp
// rood.test (pc/docs/Extensions.md, "Testing"): Lua tests that run inside a
// running game, without a relaunch. A plugin's tests are the .lua files in
// its tests folder (manifest "tests", default "tests"); each calls
// rood.test.case(name, fn). RPC lua {action "test"} collects them and runs
// every case as a coroutine under the test budget: rood.test.wait(n) resumes
// it after n game ticks (rood_rpc.Game.lua_test steps the game meanwhile).
// Assertions: eq (deep), ok, near, raises, skip. Results as JSON.
#include "rl_internal.h"
#include "light_prefabs.h"

#include <cstdio>
#include <cstring>

namespace rl {
namespace {
const char* const TEST_LIB = R"lua(
local test, collect_case, wait = ...
local function show(v)
  if type(v) == "string" then return string.format("%q", v) end
  if type(v) ~= "table" then return tostring(v) end
  local parts, n = {}, 0
  for k, x in pairs(v) do
    n = n + 1
    if n > 8 then parts[#parts + 1] = "..." break end
    parts[#parts + 1] = tostring(k) .. "=" .. show(x)
  end
  return "{" .. table.concat(parts, ", ") .. "}"
end
local function deep_eq(a, b, seen)
  if a == b then return true end
  if type(a) ~= "table" or type(b) ~= "table" then return false end
  seen = seen or {}
  if seen[a] == b then return true end
  seen[a] = b
  for k, v in pairs(a) do if not deep_eq(v, b[k], seen) then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end
local function prefix(msg) return msg and (tostring(msg) .. ": ") or "" end
function test.eq(actual, expected, msg)
  if not deep_eq(actual, expected) then
    error(prefix(msg) .. "expected " .. show(expected) .. ", got " .. show(actual), 2)
  end
end
function test.ok(value, msg)
  if not value then error(prefix(msg) .. "expected a true value, got " .. show(value), 2) end
  return value
end
function test.near(actual, expected, eps, msg)
  eps = eps or 1e-6
  if type(actual) ~= "number" or math.abs(actual - expected) > eps then
    error(prefix(msg) .. "expected " .. show(expected) .. " within " .. eps .. ", got " .. show(actual), 2)
  end
end
function test.raises(fn, pattern, msg)
  local ok, err = pcall(fn)
  if ok then error(prefix(msg) .. "expected an error", 2) end
  err = tostring(err)
  if pattern and not string.find(err, pattern) then
    error(prefix(msg) .. "error " .. show(err) .. " does not match " .. show(pattern), 2)
  end
  return err
end
function test.skip(reason) error("\1skip:" .. tostring(reason or ""), 0) end
test.case = collect_case
test.wait = wait
return test
)lua";

struct Case {
    std::string plugin, file, name;
    unsigned generation;
    int thread_ref;
    int wait = 0;
    double ms = 0;
};
struct Result {
    std::string plugin, file, name, status, message;
    double ms;
};
struct TestRun {
    int id = 0;
    std::vector<Case> pending;
    std::vector<Result> results;
    bool done = false;
};
std::deque<TestRun>& runs()
{
    static auto* list = new std::deque<TestRun>();
    return *list;
}
int next_run = 1;

int test_case(lua_State* L)
{
    luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (lua_getfield(L, LUA_REGISTRYINDEX, "rood.test.collect") != LUA_TTABLE)
        return luaL_error(L, "rood.test.case: only in a test file (run them with RPC lua {action \"test\"})");
    lua_createtable(L, 2, 0);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, 1);
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, 2);
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    return 0;
}

int test_wait(lua_State* L)
{
    lua_Integer ticks = luaL_optinteger(L, 1, 1);
    if (ticks < 1 || ticks > 100000)
        return luaL_error(L, "rood.test.wait: 1 to 100000 ticks");
    if (!lua_isyieldable(L))
        return luaL_error(L, "rood.test.wait: only inside a test case");
    lua_settop(L, 0);
    lua_pushinteger(L, ticks);
    return lua_yield(L, 1);
}

std::string number(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.3f", v);
    return buffer;
}

/* Collects one test file's cases: each a coroutine anchored in the registry. */
void collect(Plugin* p, const std::string& file, const std::string& filter, TestRun& r)
{
    std::string folder = p->dir + "/" + p->manifest.tests, code, why;
    if (!read_file(folder + "/" + file, &code, RL_MAX_FILE, &why)) {
        r.results.push_back({ p->id(), file, "(file)", "fail", why, 0 });
        return;
    }
    std::string chunk = "@" + p->id() + "/" + p->manifest.tests + "/" + file, error;
    std::vector<std::pair<std::string, int>> cases;
    bool ok = run(p, "collect tests", BUDGET_TEST, [&](lua_State* L) {
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "rood.test.collect");
        if (luaL_loadbufferx(L, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK)
            lua_error(L);
        lua_call(L, 0, 0);
        lua_getfield(L, LUA_REGISTRYINDEX, "rood.test.collect"); /* 1 */
        lua_pushnil(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "rood.test.collect");
        for (lua_Integer k = 1; k <= (lua_Integer)lua_rawlen(L, 1); k++) {
            lua_rawgeti(L, 1, k); /* 2: {name, fn} */
            lua_rawgeti(L, 2, 1);
            std::string name = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (!filter.empty() && name.find(filter) == std::string::npos) {
                lua_pop(L, 1);
                continue;
            }
            lua_State* co = lua_newthread(L); /* 3 */
            lua_rawgeti(L, 2, 2);
            lua_xmove(L, co, 1);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the thread */
            lua_pop(L, 1);
            cases.emplace_back(name, ref);
        }
    }, &error);
    if (!ok) {
        r.results.push_back({ p->id(), file, "(file)", "fail", error, 0 });
        return;
    }
    for (auto& c : cases)
        r.pending.push_back({ p->id(), file, c.first, p->generation, c.second, 0, 0 });
}

void step_run(TestRun& r, bool tick)
{
    for (size_t k = 0; k < r.pending.size();) {
        Case& c = r.pending[k];
        if (tick && c.wait > 0)
            c.wait--;
        if (c.wait > 0) {
            k++;
            continue;
        }
        Plugin* p = find(c.plugin);
        std::string status, message, error;
        int wait = 0;
        if (!p || p->state != PLUGIN_LOADED || p->generation != c.generation) {
            status = "fail";
            message = "the plugin was unloaded or reloaded during the test";
        } else {
            double start = now_seconds();
            bool ok = run(p, "test", BUDGET_TEST, [&](lua_State* L) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, c.thread_ref);
                lua_State* co = lua_tothread(L, -1);
                int results = 0;
                int st = lua_resume(co, L, 0, &results);
                if (st == LUA_YIELD) {
                    wait = results > 0 && lua_isinteger(co, -1) ? (int)lua_tointeger(co, -1) : 1;
                    lua_pop(co, results);
                    status = "wait";
                } else if (st == LUA_OK) {
                    status = "pass";
                } else {
                    const char* m = lua_tostring(co, -1);
                    std::string text = m ? m : "(error object is not a string)";
                    if (text.compare(0, 6, "\1skip:") == 0) {
                        status = "skip";
                        message = text.substr(6);
                    } else {
                        luaL_traceback(L, co, text.c_str(), 0);
                        message = lua_tostring(L, -1);
                        lua_pop(L, 1);
                        status = "fail";
                    }
                }
                lua_pop(L, 1);
            }, &error);
            c.ms += (now_seconds() - start) * 1000.0;
            if (!ok) {
                status = "fail";
                message = error;
            }
            if (status == "wait") {
                c.wait = wait;
                k++;
                continue;
            }
            p->pending_unrefs.push_back(c.thread_ref);
        }
        r.results.push_back({ c.plugin, c.file, c.name, status, message, c.ms });
        r.pending.erase(r.pending.begin() + (long)k);
    }
    if (r.pending.empty())
        r.done = true;
}
} // namespace

void open_test(lua_State* L, Plugin*)
{
    if (luaL_loadbufferx(L, TEST_LIB, std::strlen(TEST_LIB), "=rood.test", "t") != LUA_OK)
        lua_error(L);
    lua_newtable(L);
    push_cfunction(L, test_case);
    push_cfunction(L, test_wait);
    lua_call(L, 3, 1);
    lock_table(L);
    lua_setfield(L, -2, "test");
}

int start_tests(const std::string& plugin, const std::string& filter)
{
    TestRun r;
    r.id = next_run++;
    for (auto& up : all()) {
        Plugin* p = up.get();
        if (p->console || p->state != PLUGIN_LOADED || (!plugin.empty() && p->id() != plugin))
            continue;
        for (const std::string& file : list_dir(p->dir + "/" + p->manifest.tests))
            if (file.size() > 4 && file.compare(file.size() - 4, 4, ".lua") == 0)
                collect(p, file, filter, r);
    }
    runs().push_back(std::move(r));
    while (runs().size() > 20)
        runs().pop_front();
    step_run(runs().back(), false);
    return runs().back().id;
}

void step_tests(bool tick)
{
    for (TestRun& r : runs())
        if (!r.done)
            step_run(r, tick);
}

std::string test_run_json(int id)
{
    for (const TestRun& r : runs()) {
        if (r.id != id)
            continue;
        int passed = 0, failed = 0, skipped = 0;
        std::string results;
        for (const Result& x : r.results) {
            passed += x.status == "pass";
            failed += x.status == "fail";
            skipped += x.status == "skip";
            results += (results.empty() ? "" : ",") + std::string("{\"plugin\":") + prefab::json_quote(x.plugin)
                + ",\"file\":" + prefab::json_quote(x.file) + ",\"name\":" + prefab::json_quote(x.name)
                + ",\"status\":" + prefab::json_quote(x.status) + ",\"message\":" + prefab::json_quote(x.message)
                + ",\"ms\":" + number(x.ms) + "}";
        }
        return "{\"run\":" + std::to_string(r.id) + ",\"done\":" + (r.done ? "true" : "false")
            + ",\"passed\":" + std::to_string(passed) + ",\"failed\":" + std::to_string(failed)
            + ",\"skipped\":" + std::to_string(skipped) + ",\"waiting\":" + std::to_string(r.pending.size())
            + ",\"results\":[" + results + "]}";
    }
    return "";
}

void drop_tests(const std::string& plugin)
{
    for (TestRun& r : runs())
        for (size_t k = 0; k < r.pending.size();)
            if (r.pending[k].plugin == plugin) {
                const Case& c = r.pending[k];
                r.results.push_back({ c.plugin, c.file, c.name, "fail", "the plugin was unloaded during the test", c.ms });
                r.pending.erase(r.pending.begin() + (long)k);
            } else
                k++;
}

} // namespace rl
```

- [ ] **Step 5: Wire it in**
  - `lua_api_core.cpp` `open_rood`: after `open_capabilities(L, p);` add `open_test(L, p);`.
  - `lua_plugins.cpp` `unload`: after `drop_storage(p);` add `drop_tests(p->id());`.
  - `lua_plugins.cpp` `tick`: right after the `poll_hooks` loop add `step_tests(true);`.
  - `pc/CMakeLists.txt`: append `src/lua/lua_test.cpp` to `ROOD_LUA_RUNTIME`.

- [ ] **Step 6: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -E "error|warning" | head
pc/build/lua_probe pc/build/lua_probe_scratch 2>/dev/null
```

Expected: nothing from the grep; `lua_probe: ok`.

- [ ] **Step 7: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua: rood.test, tests that run inside a running game

A plugin's tests/*.lua register cases; a run collects them and resumes
each as a coroutine (rood.test.wait(n) waits n game ticks) under the
test budget; results as JSON. Test failures never count as faults.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 18: RPC method `lua` and the console's submit path

**Files:**
- Create: `pc/src/lua/lua_rpc.cpp`
- Modify: `pc/src/lua/rl_internal.h`, `pc/tests/lua_probe.cpp`, `pc/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — before `main`:

```cpp
static std::string rpc(const char* params)
{
    static char out[262144];
    int n = pc_lua_rpc(params, out, (int)sizeof out);
    return n < 0 ? std::string("error: ") + out : std::string(out);
}

/* RPC lua: eval with output, the plugin list, budgets, the console's path,
 * the registry, enable, emit, tests, the log; unknown actions refused. */
static void rpc_actions()
{
    CHECK(contains(rpc("{\"action\":\"eval\",\"code\":\"return 6 * 7\"}"), "\"results\":[42]"));
    std::string r = rpc("{\"action\":\"eval\",\"code\":\"print('from eval') error('nope')\"}");
    CHECK(contains(r, "\"ok\":false") && contains(r, "nope") && contains(r, "from eval"));
    r = rpc("{\"action\":\"plugins\"}");
    CHECK(contains(r, "\"id\":\"probe.alpha\"") && contains(r, "\"state\":\"loaded\"") && contains(r, "\"api_version\":1"));
    r = rpc("{\"action\":\"budget\",\"budget_ms\":8}");
    CHECK(contains(r, "\"budget_ms\":8") && rl::config.budget_ms == 8);
    rpc("{\"action\":\"budget\",\"budget_ms\":4}");
    r = rpc("{\"action\":\"console\",\"input\":\"return 1 + 1\"}");
    CHECK(contains(r, "\"results\":[2]") && rl::console_history().back() == "return 1 + 1");
    CHECK(contains(rpc("{\"action\":\"registry\",\"kind\":\"probe.entry\"}"), "probe.alpha/one-v2"));
    rpc("{\"action\":\"enable\",\"plugin\":\"probe.off\",\"enabled\":true}");
    CHECK(state_of("probe.off") == "loaded");
    CHECK(contains(rpc("{\"action\":\"emit\",\"event\":\"probe/rpc\",\"payload\":{\"x\":1}}"), "\"handlers\":0"));
    CHECK(rpc("{\"action\":\"nope\"}").rfind("error: ", 0) == 0);
    r = rpc("{\"action\":\"test\",\"plugin\":\"probe.alpha\",\"filter\":\"passes\"}");
    CHECK(contains(r, "\"done\":true") && contains(r, "\"passed\":1"));
    CHECK(contains(rpc("{\"action\":\"test_status\",\"run\":1}"), "\"run\":1"));
    CHECK(contains(rpc("{\"action\":\"log\",\"since\":0}"), "\"lines\":["));
    CHECK(contains(rpc("{\"action\":\"audit\"}"), "probe.caps"));
}
```

and in `main`: `rpc_actions();` after `test_harness();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target lua_probe -j 2 2>&1 | grep -m1 -E "error|undefined"
```

Expected: `undefined reference to `pc_lua_rpc'` (or `'console_history' is not a member of 'rl'`).

- [ ] **Step 3: Declare the console path** — add to `pc/src/lua/rl_internal.h` before `} // namespace rl`:

```cpp
/* ---- lua_rpc.cpp ---- */
std::vector<std::string>& console_history(); /* the console's inputs, oldest first (200) */
/* As typed into the console: history, "> input" and "= results" in the log, eval in
 * target ("@console": the console's own sandbox). */
bool console_submit(const std::string& target, const std::string& text, std::vector<Value>* results,
    std::string* error);
```

- [ ] **Step 4: Write `pc/src/lua/lua_rpc.cpp`**

```cpp
// RPC method lua (dev_rpc.c; dev-only: the RPC exists only with ROOD_RPC;
// pc/docs/Extensions.md "Developer tools"). Actions:
//   eval {code, plugin}       run code in a plugin (default: the console's own
//                             sandbox) under the test budget: {ok, results [JSON],
//                             output [what it logged], error}
//   console {input, plugin}   as typed into the overlay's console (history, echo)
//   plugins                   every plugin: state, status, manifest, grants, memory,
//                             faults, budget kills, time, handlers, timers, entries
//   reload {plugin}           one (and what depends on it); without plugin, all
//   rescan                    new folders load, removed ones unload
//   enable {plugin, enabled}  settings plugin_enabled
//   grant {plugin, capability, revoke, elevated}
//                             a grant as the user's dialog will make it (dev mode;
//                             elevated: the master switch), saved in settings.cfg
//   budget {budget_ms, budget_instructions, load_budget_ms, load_budget_instructions,
//           memory_mb, fault_limit, fault_window_s, watch_vsyncs, reset_stats}
//                             the tunables (tunables()), then them and every plugin
//   test {plugin, filter}     start a test run (rood.test); test_status {run} polls it
//   log {since}               log lines after a sequence number
//   audit                     the elevated calls (recent)
//   emit {event, payload}     a core event to every plugin (pc_lua_emit_json)
//   registry {kind}           the registry's entries (every kind without kind)
// Also pc_lua_emit_json, the game's way to emit an event (M1's menu hooks).
#include "rl_internal.h"
#include "light_prefabs.h"
#include "lua_runtime.h"

#include <cstdio>
#include <cstring>

namespace rl {
std::vector<std::string>& console_history()
{
    static auto* history = new std::vector<std::string>();
    return *history;
}

bool console_submit(const std::string& target, const std::string& text, std::vector<Value>* results, std::string* error)
{
    std::vector<std::string>& history = console_history();
    if (history.empty() || history.back() != text)
        history.push_back(text);
    if (history.size() > 200)
        history.erase(history.begin());
    log(LOG_INFO, "console", "> " + text);
    Plugin* p = target.empty() || target == "@console" ? console() : find(target);
    if (!p || p->state != PLUGIN_LOADED) {
        std::string why = "no loaded plugin '" + target + "'";
        log(LOG_ERROR, "console", why);
        if (error)
            *error = why;
        return false;
    }
    std::vector<Value> local;
    std::vector<Value>* out = results ? results : &local;
    if (!eval(p, text, out, error))
        return false;
    if (!out->empty()) {
        std::string shown;
        for (size_t k = 0; k < out->size(); k++)
            shown += (k ? ", " : "") + to_json((*out)[k]);
        if (shown.size() > 2000)
            shown = shown.substr(0, 2000) + "...";
        log(LOG_INFO, "console", "= " + shown);
    }
    return true;
}
} // namespace rl

namespace {
using prefab::Json;

std::string q(const std::string& s) { return prefab::json_quote(s); }
std::string num(double v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.10g", v);
    return buffer;
}
std::string list(const std::vector<std::string>& items)
{
    std::string s = "[";
    for (size_t k = 0; k < items.size(); k++)
        s += (k ? "," : "") + q(items[k]);
    return s + "]";
}
const std::string* text_param(const Json& params, const char* key)
{
    const Json* j = params.get(key);
    return j && j->type == Json::String ? &j->string : nullptr;
}
bool flag_param(const Json& params, const char* key, bool fallback)
{
    const Json* j = params.get(key);
    return j && j->type == Json::Bool ? j->boolean : fallback;
}

std::string plugin_json(const rl::Plugin& p)
{
    const rl::Manifest& m = p.manifest;
    return "{\"id\":" + q(m.id) + ",\"name\":" + q(m.name) + ",\"version\":" + q(m.version)
        + ",\"state\":" + q(rl::state_name(p.state)) + ",\"status\":" + q(p.status) + ",\"dir\":" + q(p.dir)
        + ",\"shipped\":" + (p.shipped ? "true" : "false") + ",\"console\":" + (p.console ? "true" : "false")
        + ",\"enabled\":" + (p.console || rl::plugin_enabled(&p) ? "true" : "false")
        + ",\"generation\":" + std::to_string(p.generation) + ",\"hash\":" + q(p.hash)
        + ",\"declared\":" + list(m.capabilities) + ",\"granted\":" + list(p.granted)
        + ",\"pending\":" + list(rl::pending_grants(&p)) + ",\"elevated\":" + (p.granted.empty() ? "false" : "true")
        + ",\"memory\":{\"used_kb\":" + num(p.mem.used / 1024.0) + ",\"peak_kb\":" + num(p.mem.peak / 1024.0)
        + ",\"cap_kb\":" + num(p.mem.cap / 1024.0) + ",\"refusals\":" + std::to_string(p.mem.refusals) + "}"
        + ",\"calls\":" + std::to_string(p.stats.calls) + ",\"faults\":" + std::to_string(p.stats.faults)
        + ",\"kills\":" + std::to_string(p.stats.kills) + ",\"max_ms\":" + num(p.stats.max_ms)
        + ",\"total_ms\":" + num(p.stats.total_ms) + ",\"handlers\":" + std::to_string(rl::handler_count(m.id))
        + ",\"timers\":" + std::to_string(rl::timer_count(m.id)) + ",\"entries\":" + std::to_string(rl::registry_count(m.id))
        + "}";
}

std::string plugins_reply()
{
    std::string s = "{\"version\":" + q(RL_VERSION) + ",\"api_version\":" + std::to_string(RL_API_VERSION)
        + ",\"elevated\":" + (rl::config.elevated ? "true" : "false") + ",\"plugins\":[";
    bool first = true;
    for (auto& p : rl::all()) {
        s += (first ? "" : ",") + plugin_json(*p);
        first = false;
    }
    return s + "]}";
}

std::string eval_reply(bool ok, const std::vector<rl::Value>& results, const std::string& error,
    unsigned long long since)
{
    std::string s = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"results\":[";
    for (size_t k = 0; k < results.size(); k++)
        s += (k ? "," : "") + rl::to_json(results[k]);
    s += "],\"output\":[";
    bool first = true;
    for (const rl::LogLine& line : rl::log_lines())
        if (line.seq > since) {
            s += (first ? "" : ",") + q(line.text);
            first = false;
        }
    s += "]";
    if (!ok)
        s += ",\"error\":" + q(error);
    return s + "}";
}

const char* level_name(int level) { return level == rl::LOG_ERROR ? "error" : level == rl::LOG_WARN ? "warn" : "info"; }
} // namespace

extern "C" int pc_lua_rpc(const char* params_text, char* out, int size)
{
    auto failure = [&](const std::string& message) {
        std::snprintf(out, (size_t)size, "%s", message.c_str());
        return -1;
    };
    Json params;
    std::string error, reply;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object)
        return failure("lua: params: " + (error.empty() ? std::string("expected an object") : error));
    if (!rl::on_game_thread())
        return failure("lua: off the game thread");
    const std::string* a = text_param(params, "action");
    std::string action = a ? *a : "plugins";
    const std::string* plugin = text_param(params, "plugin");
    if (action == "eval" || action == "console") {
        const std::string* code = text_param(params, action == "eval" ? "code" : "input");
        if (!code)
            return failure("lua: " + action + " needs " + (action == "eval" ? "code" : "input"));
        unsigned long long since = rl::log_last_seq();
        std::vector<rl::Value> results;
        bool ok;
        if (action == "console")
            ok = rl::console_submit(plugin ? *plugin : "@console", *code, &results, &error);
        else {
            rl::Plugin* p = plugin ? rl::find(*plugin) : rl::console();
            if (!p || p->state != rl::PLUGIN_LOADED)
                return failure("lua: no loaded plugin '" + (plugin ? *plugin : std::string("@console")) + "'");
            ok = rl::eval(p, *code, &results, &error);
        }
        rl::settle();
        reply = eval_reply(ok, results, error, since);
    } else if (action == "plugins") {
        reply = plugins_reply();
    } else if (action == "reload" || action == "rescan") {
        if (action == "rescan") {
            rl::scan();
            rl::config_changed();
        } else if (plugin) {
            rl::Plugin* p = rl::find(*plugin);
            if (!p || p->console)
                return failure("lua: no plugin '" + *plugin + "'");
            p->reload_requested = true;
        } else
            rl::request_reload_all();
        rl::settle();
        reply = plugins_reply();
    } else if (action == "enable") {
        if (!plugin || !rl::find(*plugin))
            return failure("lua: enable needs a known plugin");
        rl::set_enabled(*plugin, flag_param(params, "enabled", true));
        rl::settle();
        reply = plugins_reply();
    } else if (action == "grant") {
        const std::string* capability = text_param(params, "capability");
        if (!plugin || !capability)
            return failure("lua: grant needs plugin and capability");
        if (params.get("elevated")) {
            rl::config.elevated = flag_param(params, "elevated", rl::config.elevated);
            rl::config_changed();
        }
        if (!rl::grant(*plugin, *capability, flag_param(params, "revoke", false), &error))
            return failure("lua: " + error);
        rl::settle();
        reply = plugins_reply();
    } else if (action == "budget") {
        for (const rl::Tunable& t : rl::tunables()) {
            const Json* v = params.get(t.rpc);
            if (v && v->type == Json::Number)
                rl::set_tunable(t, v->number);
        }
        if (flag_param(params, "reset_stats", false))
            for (auto& p : rl::all())
                p->stats = rl::Stats();
        rl::settle();
        reply = "{\"config\":{";
        for (const rl::Tunable& t : rl::tunables())
            reply += q(t.rpc) + ":" + num(rl::get_tunable(t)) + ",";
        reply += "\"elevated\":" + std::string(rl::config.elevated ? "true" : "false") + "},\"plugins\":"
            + plugins_reply() + "}";
    } else if (action == "test") {
        const std::string* filter = text_param(params, "filter");
        int run = rl::start_tests(plugin ? *plugin : "", filter ? *filter : "");
        rl::settle();
        reply = rl::test_run_json(run);
    } else if (action == "test_status") {
        const Json* run = params.get("run");
        if (!run || run->type != Json::Number)
            return failure("lua: test_status needs run");
        reply = rl::test_run_json((int)run->number);
        if (reply.empty())
            return failure("lua: no test run " + num(run->number));
    } else if (action == "log") {
        const Json* since = params.get("since");
        unsigned long long after = since && since->type == Json::Number ? (unsigned long long)since->number : 0;
        reply = "{\"lines\":[";
        bool first = true;
        size_t skip = rl::log_lines().size() > 500 ? rl::log_lines().size() - 500 : 0;
        for (const rl::LogLine& line : rl::log_lines()) {
            if (skip) {
                --skip;
                continue;
            }
            if (line.seq <= after)
                continue;
            reply += std::string(first ? "" : ",") + "{\"seq\":" + std::to_string(line.seq) + ",\"level\":\""
                + level_name(line.level) + "\",\"plugin\":" + q(line.plugin) + ",\"text\":" + q(line.text) + "}";
            first = false;
        }
        reply += "],\"next\":" + std::to_string(rl::log_last_seq()) + "}";
    } else if (action == "audit") {
        std::vector<std::string> lines(rl::audit_lines().begin(), rl::audit_lines().end());
        reply = "{\"lines\":" + list(lines) + "}";
    } else if (action == "emit") {
        const std::string* event = text_param(params, "event");
        if (!event)
            return failure("lua: emit needs event");
        const Json* payload = params.get("payload");
        int handlers = rl::emit(*event, payload ? rl::from_json(*payload) : rl::Value());
        rl::settle();
        reply = "{\"handlers\":" + std::to_string(handlers) + "}";
    } else if (action == "registry") {
        const std::string* kind = text_param(params, "kind");
        std::vector<std::string> kinds = kind ? std::vector<std::string>{ *kind } : rl::registry_kinds();
        reply = "{\"kinds\":" + list(rl::registry_kinds()) + ",\"entries\":[";
        bool first = true;
        for (const std::string& k : kinds)
            for (const rl::RegistryEntry* e : rl::registry_list(k)) {
                reply += std::string(first ? "" : ",") + "{\"kind\":" + q(e->kind) + ",\"id\":" + q(e->id)
                    + ",\"owner\":" + q(e->owner) + ",\"group\":" + q(e->group) + ",\"data\":" + rl::to_json(e->data) + "}";
                first = false;
            }
        reply += "]}";
    } else
        return failure("lua: unknown action '" + action
            + "' (eval, console, plugins, reload, rescan, enable, grant, budget, test, test_status, log, audit, emit, "
              "registry)");
    if ((int)reply.size() >= size)
        return failure("lua: the reply is larger than " + std::to_string(size) + " bytes");
    std::memcpy(out, reply.c_str(), reply.size() + 1);
    return (int)reply.size();
}

extern "C" int pc_lua_emit_json(const char* event, const char* json)
{
    if (!rl::initialized() || !event)
        return 0;
    rl::Value payload;
    if (json && *json) {
        prefab::Json j;
        if (!prefab::json_parse(json, j, nullptr))
            return -1;
        payload = rl::from_json(j);
    }
    int handlers = rl::emit(event, payload);
    rl::settle();
    return handlers;
}
```

- [ ] **Step 5: Add the file to the probe** — append `src/lua/lua_rpc.cpp` to `ROOD_LUA_RUNTIME` in `pc/CMakeLists.txt`.

- [ ] **Step 6: Run it and see it pass; run the whole CTest set**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build -j 2 2>&1 | grep -E "error|warning" | head
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -j 2 --output-on-failure | tail -3
```

Expected: nothing from the grep; `100% tests passed` (every probe, `lua_probe` among them).

- [ ] **Step 7: Commit**

```bash
git add pc/src/lua pc/tests/lua_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Lua: RPC method lua (eval, plugins, reload, grant, budget, tests, log)

Also the console's submit path and pc_lua_emit_json, the game's way to
emit an event.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 19: Build Lua and the runtime on all four targets

**Files:**
- Modify: `pc/tools/build_boot.py`, `pc/tools/windows_dist.py`

Two facts this task relies on (from `build_boot.py`): every host object is scanned for undefined symbols, and a referenced function that no scanned library defines becomes a fail-fast trap in `traps.c`. Today `_Unwind_Resume` (C++ unwinding, in `libgcc_s`) is such a trap on Linux, because `libgcc_s` is not scanned — harmless while nothing throws, fatal once Lua's errors are C++ exceptions. Step 2 scans and links `libgcc_s`.

- [ ] **Step 1: See the problem**

```bash
grep -c "_Unwind_Resume" pc/build/boot/traps.c
```

Expected: `1` (the baseline build of Task 1 traps C++ unwinding).

- [ ] **Step 2: Scan and link `libgcc_s` (Linux)** — in `pc/tools/build_boot.py`:
  - in the non-`coff` `host_libraries` list, add `"/lib/x86_64-linux-gnu/libgcc_s.so.1",` after the 64-bit `gcc_lib + "/libgcc.a",` line and `"/lib/i386-linux-gnu/libgcc_s.so.1",` after the 32-bit `gcc_lib + "/32/libgcc.a",` line;
  - in the loop below it, read every shared library's dynamic symbols (today only names ending in `.6` are): replace

```python
        for line in run(["nm", "-D" if library.endswith(".6") else "", "--defined-only", library] if library.endswith(".6") else [*NM, "--defined-only", library], capture_output=True, text=True).stdout.splitlines():
```

with

```python
        shared = ".so." in os.path.basename(library)  # libc.so.6, libgcc_s.so.1: their dynamic symbols
        for line in run(["nm", "-D", "--defined-only", library] if shared else [*NM, "--defined-only", library],
                        capture_output=True, text=True).stdout.splitlines():
```

  - in `stamp_files`, after `"/lib/i386-linux-gnu/libc.so.6", "/lib/i386-linux-gnu/libm.so.6",` add `"/lib/x86_64-linux-gnu/libgcc_s.so.1", "/lib/i386-linux-gnu/libgcc_s.so.1",`;
  - in the final Linux link's argument list, after `"-l:libstdc++.so.6",` add `"-l:libgcc_s.so.1",  # C++ unwinding (Lua errors are exceptions: pc/docs/Extensions.md)`.

- [ ] **Step 3: Keep `windows.h` out of the Lua core** — in `win_command`, replace

```python
    host = "-nostdinc" not in args and not any(a.endswith(".i") for a in args)
```

with

```python
    # The vendored Lua core (-DROOD_LUA_CORE) needs none of it either: Windows'
    # macros stay away from its identifiers.
    host = "-nostdinc" not in args and not any(a.endswith(".i") for a in args) and "-DROOD_LUA_CORE" not in args
```

- [ ] **Step 4: Compile Lua and `pc/src/lua`** — insert right before the line `vendor = root / "pc/third_party/psycross"`:

```python
# Lua 5.4 (vendored, MIT; pc/third_party/lua) for plugins (pc/docs/Extensions.md):
# the core and its safe libraries only (no io, os, package or debug, no
# standalone interpreters), compiled as C++ so a Lua error is a C++
# exception and the bindings may keep std::string across Lua calls; with
# the pattern matcher's budget hook (rood_luauser.h, ROOD-NOTES.md).
lua_source = root / "pc/third_party/lua/src"
LUA_CORE = ["lapi", "lcode", "lctype", "ldebug", "ldo", "ldump", "lfunc", "lgc", "llex", "lmem", "lobject",
            "lopcodes", "lparser", "lstate", "lstring", "ltable", "ltm", "lundump", "lvm", "lzio", "lauxlib",
            "lbaselib", "lcorolib", "lmathlib", "lstrlib", "ltablib", "lutf8lib"]
for name in LUA_CORE:
    obj = out / ("lua_" + name + ".o")
    compile_object(["g++", *cxx_flags, "-DROOD_LUA_CORE", "-include", "pc/src/lua/rood_luauser.h",
                    "-I" + str(lua_source), "-x", "c++", "-c", str(lua_source / (name + ".c")), "-o", str(obj)],
                   f"pc/third_party/lua/src/{name}.c", "c++")
    objects.append(obj)
# The extension system (pc/src/lua): runtime, API, RPC, console.
for source in sorted((root / "pc/src/lua").glob("*.cpp")):
    obj = out / (source.stem + ".o")
    compile_object(["g++", *cxx_flags, "-I" + str(lua_source), "-I" + str(imgui), "-Ipc/src", "-Ipc/src/lua",
                    *sdl_flags, "-c", str(source), "-o", str(obj)], source.relative_to(root), "c++")
    objects.append(obj)
```

(Object names: `lua_lapi.o` ... for the core, `lua_runtime.o`, `sha256.o` ... for ours; none starts with an overlay's key such as `main_`, `title_`, `init_`, `menu`, `plg`, `ending`.)

- [ ] **Step 5: Ship plugins and Lua's licence in the Windows folder** — in `pc/tools/windows_dist.py`: change `for tree in ["data", "lighting", "pc/assets", "pc/third_party/fonts"]:` to `for tree in ["data", "lighting", "pc/assets", "pc/third_party/fonts", "pc/plugins"]:` and add `("lua.txt", "pc/third_party/lua/LICENSE"),` after the `("ImGuizmo.txt", ...)` entry of the licence list. Until Task 25 adds `pc/plugins`, create it so the copy does not fail: `mkdir -p pc/plugins && touch pc/plugins/.gitkeep`.

- [ ] **Step 6: Build the 32-bit boot binary**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py 2>&1 | tail -2
grep -c "_Unwind\|void lua\|void _Z" pc/build/boot/traps.c
```

Expected: `Built .../pc/build/boot/rood_boot: ...`; the grep prints `0`.

- [ ] **Step 7: Build the battle binary**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tail -2
grep -c "_Unwind\|void lua\|void _Z" pc/build/boot/traps.c
```

Expected: `Built .../rood_battle_probe: N data imports, M' fail-fast function imports` with `M' = M - 1` (M from Task 1: `_Unwind_Resume` is no longer a trap, and Lua added none); the grep prints `0`.

- [ ] **Step 8: Build the 64-bit binary**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64 2>&1 | tail -3
python3 -c "import json; f = json.load(open('pc/build/boot64/report.json'))['files']; print(sum(not x['compiled'] for x in f), sum(x['source'].startswith(('pc/third_party/lua', 'pc/src/lua')) for x in f))"
grep -c "_Unwind\|void lua\|void _Z" pc/build/boot64/traps.c
```

Expected: `Built .../pc/build/boot64/rood_battle_probe: ...`; the Python line prints `0 41` (no unit failed; 27 Lua core units and the 14 `.cpp` files of `pc/src/lua` so far — Tasks 23 and 24 add two); the grep prints `0`.

- [ ] **Step 9: Build the Windows binary**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows 2>&1 | tail -2
```

Expected: `Built .../pc/build/win64/rood.exe: ...`. If a Lua core unit fails under MinGW, the report (`pc/build/win64/report.txt`) names it; a Windows macro colliding with a Lua identifier means Step 3 did not apply (the command line of that unit must not contain `win_compat.h`).

- [ ] **Step 10: The build's own tests still pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 build_noop rpc_smoke
```

Expected: `2/2 passed`.

- [ ] **Step 11: Commit**

```bash
git add pc/tools/build_boot.py pc/tools/windows_dist.py pc/plugins/.gitkeep
git commit -m "$(cat <<'EOF'
build_boot: compile Lua (as C++) and pc/src/lua on every target

The Linux builds now scan and link libgcc_s, so C++ unwinding
(_Unwind_Resume) is the real one instead of a fail-fast trap: Lua's
errors are exceptions. The Lua core is built without win_compat.h.
The Windows folder ships pc/plugins and Lua's licence.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 20: The game calls in — init, tick, frame, RPC, settings lines

**Files:**
- Modify: `pc/src/boot_runtime.c`, `pc/src/dev_overlay.cpp`, `pc/src/dev_rpc.c`, `pc/src/project_settings.cpp`

- [ ] **Step 1: See RPC `lua` missing in the running game**

```bash
flock "$ROOD_HEAVY_LOCK" bash -c '
  export ROOD_PLUGINS=$PWD/pc/build/m0-plugins ROOD_PLUGIN_DATA=$PWD/pc/build/m0-plugin-data
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock launch >/dev/null &&
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock call lua "{\"action\":\"eval\",\"code\":\"return rood.version\"}"
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock call quit >/dev/null 2>&1; true'
```

Expected: a Python traceback ending in `rood_rpc.RpcError: ...` naming the unknown method `lua`.

- [ ] **Step 2: Init and tick** — in `pc/src/boot_runtime.c`:
  - after `extern void pc_overlay_statics_loaded(int);` add:

```c
/* Plugins (pc/src/lua, pc/docs/Extensions.md "Where Lua runs"); weak: probes link this file alone. */
extern void pc_lua_init(void) __attribute__((weak));
extern void pc_lua_tick(void) __attribute__((weak));
```

  - in `main`, right after the `if (&pc_skip_intro && pc_skip_intro) { ... }` block (before `pc_audio_output_start();`) add:

```c
    if (pc_lua_init)
        pc_lua_init(); /* after the settings (their plugin order and grants), on this thread */
```

  - in `pc_tick`, right after the `if (pc_fog_vsync) pc_fog_vsync(); ...` statement add:

```c
    if (pc_lua_tick)
        pc_lua_tick(); /* plugins' tick handlers and game-time timers, after the RPC's changes */
```

- [ ] **Step 3: Frame and console** — in `pc/src/dev_overlay.cpp`:
  - after the `extern "C" void pc_settings_load(void);` line add:

```cpp
extern "C" void pc_lua_frame(double dt);        /* pc/src/lua: plugins' frame handlers (ImGui frame open) */
extern "C" void pc_lua_console_menu_item(void); /* lua_console.cpp */
extern "C" void pc_lua_console_window(void);
```

  - in `pc_overlay_frame`, right after `ImGuizmo::BeginFrame();` add `pc_lua_frame(dt); /* every presented image, real time */`;
  - in `menu_bar`, in the "Window" menu, after `ImGui::MenuItem("Room Browser", "Ctrl+R", &pc_room_browser_open);` add `pc_lua_console_menu_item();`;
  - in `pc_overlay_frame`, right after `memory_cards_window();` add `pc_lua_console_window();`.

  `lua_console.cpp` arrives in Task 24; until then add these two stubs to the end of `pc/src/lua/lua_rpc.cpp` (Task 24 Step 4 deletes them):

```cpp
extern "C" void pc_lua_console_menu_item(void) {}
extern "C" void pc_lua_console_window(void) {}
```

- [ ] **Step 4: Route RPC `lua`** — in `pc/src/dev_rpc.c`:
  - in the method list comment, after the `editor_schema {...}` entry's lines, add:

```c
 *   lua {action, ...}            the extension system (pc/src/lua/lua_rpc.cpp,
 *                                pc/docs/Extensions.md): eval {code, plugin},
 *                                console {input}, plugins, reload {plugin},
 *                                rescan, enable {plugin, enabled}, grant
 *                                {plugin, capability, revoke, elevated} (dev
 *                                mode), budget {...}, test {plugin, filter},
 *                                test_status {run}, log {since}, audit, emit
 *                                {event, payload}, registry {kind}
```

  - right before `} else if (!strcmp(method, "editor_overlay")) {` add:

```c
    } else if (!strcmp(method, "lua")) {
        /* The extension system; lua_rpc.cpp parses the params object itself. */
        extern int pc_lua_rpc(const char*, char*, int) __attribute__((weak));
        if (!pc_lua_rpc) {
            fail(fd, id, -32000, "no Lua runtime in this build");
            return;
        }
        static char args[LINE_MAX_BYTES];
        const char* end = params ? value_end(params) : NULL;
        if (params && end && (size_t)(end - params) < sizeof args) {
            memcpy(args, params, (size_t)(end - params));
            args[end - params] = 0;
        } else {
            strcpy(args, "{}");
        }
        if (pc_lua_rpc(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
```

- [ ] **Step 5: Settings lines** — in `pc/src/project_settings.cpp`:
  - add `#include "lua/lua_runtime.h" /* plugins_elevated, plugin_enabled, plugin_order, plugin_grant, plugin_budget_* */` after `#include "editor_colour.h"`;
  - in `apply_line`, right after `if (pc_button_prompts_setting(key, rest))\n        return 1;` add:

```cpp
    if (pc_lua_settings_parse(key, rest))
        return 1;
```

  - in `pc_settings_load`, right after `rood_selection::clear_hidden(); /* the file's sets replace them */` add `pc_lua_settings_begin(); /* the file's plugin lines replace them */`, and right after the `fclose(f);` that follows the read loop add `pc_lua_settings_end();`;
  - in `pc_settings_write`, right before `rood_selection::write_hidden(f);` add `pc_lua_settings_write(f);`.

- [ ] **Step 6: Build and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tail -1
flock "$ROOD_HEAVY_LOCK" bash -c '
  export ROOD_PLUGINS=$PWD/pc/build/m0-plugins ROOD_PLUGIN_DATA=$PWD/pc/build/m0-plugin-data
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock launch >/dev/null &&
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock call lua "{\"action\":\"eval\",\"code\":\"return rood.version, rood.clock.game() >= 0\"}"
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock call quit >/dev/null 2>&1; true'
grep "\[lua\]" pc/build/boot/rpc-game.log | head -3
```

Expected: `{"ok": true, "results": ["0.1.0", true], "output": []}`; the log has `[lua] info: 0 plugin(s) in pc/plugins and .../pc/build/m0-plugins, 0 loaded`.

- [ ] **Step 7: Existing suites unaffected**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest rpc_smoke system_settings ui_system editor_schema
```

Expected: the same result as the Task 1 baseline.

- [ ] **Step 8: Commit**

```bash
git add pc/src/boot_runtime.c pc/src/dev_overlay.cpp pc/src/dev_rpc.c pc/src/project_settings.cpp pc/src/lua/lua_rpc.cpp
git commit -m "$(cat <<'EOF'
Plugins run in the game: init after the settings, tick per vsync, frame per image

pc_lua_init in main (after settings.cfg), pc_lua_tick in pc_tick after
the RPC, pc_lua_frame in the overlay's frame; RPC method lua; the
plugin lines of settings.cfg. All on the game thread.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 21: Fast Lua testing — `rood_rpc.py` helpers, `lua_smoke.py`, the core fixture

**Files:**
- Create: `pc/tests/lua_smoke.py`, `pc/tests/lua/plugins/test.core/plugin.lua`, `main.lua`, `data/hello.txt`, `tests/core_test.lua`
- Modify: `pc/tools/rood_rpc.py`, `pc/tests/run.py`

- [ ] **Step 1: Write the core fixture plugin** — `pc/tests/lua/plugins/test.core/plugin.lua`:

```lua
return {
  id = "test.core",
  name = "Core API tests",
  version = "1.0.0",
  api_version = 1,
}
```

`pc/tests/lua/plugins/test.core/main.lua`:

```lua
-- Tests only (tests/core_test.lua): nothing to do at load.
```

`pc/tests/lua/plugins/test.core/data/hello.txt` (one line, ending in a newline):

```text
hello
```

`pc/tests/lua/plugins/test.core/tests/core_test.lua`:

```lua
local t = rood.test

t.case("version and plugin info", function()
  t.eq(type(rood.version), "string")
  t.eq(rood.api_version, 1)
  t.eq(rood.plugin.id, "test.core")
  t.ok(rood.plugin.dir:find("test.core", 1, true), rood.plugin.dir)
  t.eq(rood.plugin.elevated, false)
end)

t.case("events reach handlers as copies", function()
  local got
  local h = rood.on("test.core/ping", function(payload) got = payload end)
  local payload = { n = 1, list = { 1, 2 } }
  t.ok(rood.emit("test.core/ping", payload) >= 1)
  t.eq(got, payload)
  t.ok(got ~= payload, "a copy, not the same table")
  t.ok(rood.off(h))
  got = nil
  rood.emit("test.core/ping", payload)
  t.eq(got, nil)
end)

t.case("the core's events cannot be emitted by plugins", function()
  t.raises(function() rood.emit("tick", {}) end, "namespaced")
end)

t.case("game-time timers fire after ticks", function()
  local fired = 0
  rood.timer.after(0.05, function() fired = fired + 1 end)
  t.eq(fired, 0)
  t.wait(5)
  t.eq(fired, 1)
end)

t.case("every and cancel", function()
  local n = 0
  local id = rood.timer.every(1 / 59.94, function() n = n + 1 end)
  t.wait(3)
  t.ok(n >= 2, "fired every tick")
  t.ok(rood.timer.cancel(id))
  local before = n
  t.wait(3)
  t.eq(n, before)
end)

t.case("registry order: after, before, group", function()
  local R = rood.registry
  R.register("test.thing", "test.core/b", { v = "b" }, { after = "test.core/a" })
  R.register("test.thing", "test.core/a", { v = "a" })
  R.register("test.thing", "test.core/c", { v = "c" }, { before = "test.core/a", group = "early" })
  local ids = {}
  for _, e in ipairs(R.list("test.thing")) do ids[#ids + 1] = e.id end
  t.eq(ids, { "test.core/c", "test.core/a", "test.core/b" })
  t.eq(#R.list("test.thing", { group = "early" }), 1)
  t.eq(R.get("test.thing", "test.core/a").data.v, "a")
  t.raises(function() R.register("test.thing", "other.plugin/x", {}) end, "namespace")
  for _, id in ipairs(ids) do t.ok(R.remove("test.thing", id)) end
  t.eq(#R.list("test.thing"), 0)
end)

t.case("storage keeps plain data; files stay in their folders", function()
  rood.storage.set("answer", { n = 42 })
  t.eq(rood.storage.get("answer"), { n = 42 })
  t.raises(function() rood.storage.set("f", print) end, "function")
  t.eq(rood.storage.read("data/hello.txt"), "hello\n")
  t.raises(function() rood.storage.read("../test.sandbox/main.lua") end, "bad path")
  rood.storage.write_data("note.txt", "x")
  t.eq(rood.storage.read_data("note.txt"), "x")
end)
```

- [ ] **Step 2: Add the Lua helpers to `pc/tools/rood_rpc.py`** — in class `Game`, after `wait_title`, add:

```python
    # ---- Lua (pc/src/lua, pc/docs/Extensions.md) ----
    def lua(self, code, plugin=None):
        """Evaluate Lua in a plugin (default: the console's own sandbox) under the
        test budget: {"ok", "results", "output", "error"} (a Lua error is a result,
        not an RpcError)."""
        params = {"action": "eval", "code": code}
        if plugin:
            params["plugin"] = plugin
        return self.call("lua", **params)

    def lua_plugins(self):
        """Every plugin by id (RPC lua {action "plugins"})."""
        return {p["id"]: p for p in self.call("lua", action="plugins")["plugins"]}

    def lua_test(self, plugin=None, filter=None, step=2, timeout=120):
        """Run the plugins' Lua tests (rood.test) in this game, without a relaunch:
        start a run, then step the game `step` vsyncs at a time while cases wait for
        ticks. Returns the finished run ({passed, failed, skipped, results})."""
        params = {"action": "test"}
        if plugin:
            params["plugin"] = plugin
        if filter:
            params["filter"] = filter
        run = self.call("lua", **params)
        deadline = time.monotonic() + timeout
        while not run["done"]:
            if time.monotonic() > deadline:
                raise RpcError(f"Lua tests still waiting after {timeout} s: {run['waiting']} case(s)")
            self.run(step)
            run = self.call("lua", action="test_status", run=run["run"])
        return run
```

  and in `main()`, after the `sym` parser lines add:

```python
    lua = sub.add_parser("lua", help="evaluate Lua in the running game (RPC lua eval)")
    lua.add_argument("code")
    lua.add_argument("--plugin")
    lua_test = sub.add_parser("lua-test", help="run the plugins' Lua tests in the running game")
    lua_test.add_argument("--plugin")
    lua_test.add_argument("--filter")
```

  and before the final `else: parser.print_help()`:

```python
    elif args.command == "lua":
        print(json.dumps(Game(args.socket).lua(args.code, plugin=args.plugin), indent=1))
    elif args.command == "lua-test":
        run = Game(args.socket).lua_test(plugin=args.plugin, filter=args.filter)
        for r in run["results"]:
            print(f"{r['status'].upper():<5} {r['plugin']} {r['file']}: {r['name']}")
            if r["status"] == "fail":
                print("      " + r["message"].replace("\n", "\n      "))
        print(f"{run['passed']} passed, {run['failed']} failed, {run['skipped']} skipped")
        sys.exit(1 if run["failed"] else 0)
```

  Add to the module docstring's usage list (at its end):

```text
    rood_rpc.py lua 'return rood.version' [--plugin ID]   evaluate Lua in the running game
    rood_rpc.py lua-test [--plugin ID] [--filter TEXT]     run the plugins' Lua tests in it
```

- [ ] **Step 3: Write `pc/tests/lua_smoke.py`** (this version runs the Lua tests; Task 26 adds the hard cases)

```python
#!/usr/bin/env python3
"""The extension system in one running game (pc/src/lua, pc/docs/Extensions.md):
every fixture plugin's Lua tests (pc/tests/lua/plugins, rood.test) and the
shipped plugins', in a single launch.

    python3 pc/tests/lua_smoke.py
    python3 pc/tests/lua_smoke.py --tests-only --plugin test.settings   one plugin's Lua tests (the fast loop)
"""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "pc/tests/lua/plugins"

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--plugin", action="append", default=[], help="only this plugin's Lua tests (repeatable)")
parser.add_argument("--tests-only", action="store_true", help="only the Lua tests")
args = parser.parse_args()

plugins = OUT / "lua-plugins"
data = OUT / "lua-plugin-data"
settings = OUT / "lua-settings.cfg"
shutil.rmtree(plugins, ignore_errors=True)
shutil.rmtree(data, ignore_errors=True)
settings.unlink(missing_ok=True)
shutil.copytree(FIXTURES, plugins)


def lua_tests(game):
    listed = game.lua_plugins()
    for folder in sorted(p.name for p in FIXTURES.iterdir() if p.is_dir()):
        assert listed[folder]["state"] == "loaded", listed[folder]
    if "rood.example" in listed:
        game.call("lua", action="enable", plugin="rood.example", enabled=True)
    results = []
    for plugin in args.plugin or [None]:
        results += game.lua_test(plugin=plugin)["results"]
    for r in results:
        print(f"{r['status'].upper():<5} {r['plugin']} {r['file']}: {r['name']} ({r['ms']} ms)")
    failed = [r for r in results if r["status"] == "fail"]
    assert not failed, "Lua tests failed:\n" + "\n\n".join(f"{r['plugin']} {r['name']}:\n{r['message']}" for r in failed)
    return results


game = Game.launch(fast=True, extra_env={"ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(data),
                                         "ROOD_SETTINGS": str(settings)})
try:
    game.pause()
    results = lua_tests(game)
    print(f"PASS: {len(results)} Lua tests in one game")
finally:
    game.close()
```

- [ ] **Step 4: Register the suite and isolate every suite's plugins** — in `pc/tests/run.py`:
  - in `suites()`, after the `rpc_tcp_token_smoke` entry add:

```python
        Suite("lua_smoke", [PY, str(TESTS / "lua_smoke.py")], 300,
              note="plugins in one game: every Lua test (rood.test), runaway and failing scripts stopped "
                   "within their budgets, sandbox, capabilities, hot reload, settings from Lua"),
```

  - in `run_suite`'s `env.update(...)`, after `ROOD_PREFABS=str(out / "prefabs"),` add:

```python
        ROOD_PLUGINS=str(out / "plugins"),  # none of the user's plugins (pc/user/plugins)
        ROOD_PLUGIN_DATA=str(out / "plugin_data"),
```

- [ ] **Step 5: Run it and see it pass** (the core API exists, so this is integration coverage)

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 lua_smoke
```

Expected: `1/1 passed`; `pc/build/tests/lua_smoke/output.log` lists 7 `PASS test.core ...` lines and `PASS: 7 Lua tests in one game`. (The storage case's `bad path` error unwinds through C++ frames with locals: this is the in-game proof that `_Unwind_Resume` is real, Task 19.)

- [ ] **Step 6: The fast loop works on a running game**

```bash
flock "$ROOD_HEAVY_LOCK" bash -c '
  export ROOD_PLUGINS=$PWD/pc/tests/lua/plugins ROOD_PLUGIN_DATA=$PWD/pc/build/m0-plugin-data
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock launch >/dev/null &&
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock lua-test --plugin test.core
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock call quit >/dev/null 2>&1; true'
```

Expected: seven `PASS  test.core core_test.lua: ...` lines, then `7 passed, 0 failed, 0 skipped`.

- [ ] **Step 7: Commit**

```bash
git add pc/tests/lua pc/tests/lua_smoke.py pc/tools/rood_rpc.py pc/tests/run.py
git commit -m "$(cat <<'EOF'
Lua tests in one running game: lua_smoke, rood_rpc lua / lua-test

Game.lua, Game.lua_test and Game.lua_plugins drive the RPC; lua_smoke
launches one game and runs every fixture plugin's Lua tests; every
run.py suite gets empty plugin folders of its own.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 22: Settings registry hooks — `pc_ui_settings_find`, `rood_schema::remove_kind`

**Files:**
- Modify: `pc/src/ui/ui_settings.h`, `pc/src/ui/ui_settings.cpp`, `pc/tests/ui_settings_probe.cpp`, `pc/src/editor_schema.h`, `pc/src/editor_schema.cpp`

- [ ] **Step 1: Write the failing probe check** — in `pc/tests/ui_settings_probe.cpp`, in `pages()`, right after `CHECK(find(D, "present_rate") == 4);` add:

```cpp
    int fp = -1, fr = -1;
    CHECK(pc_ui_settings_find("present_rate", &fp, &fr) == 1 && fp == D && fr == 4);
    pc_hw_interpolate = 0;
    CHECK(pc_ui_settings_find("present_rate", &fp, &fr) == 2); /* exists, not shown now */
    pc_hw_interpolate = 1;
    CHECK(pc_ui_settings_find("no_such_row", &fp, &fr) == 0);
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target ui_settings_probe -j 2 2>&1 | grep -m1 error
```

Expected: `error: 'pc_ui_settings_find' was not declared in this scope`.

- [ ] **Step 3: Declare and implement it** — in `pc/src/ui/ui_settings.h`, after the `pc_ui_settings_row` declaration add:

```c
/* A row by its id (plugins: rood.settings, pc/src/lua/lua_api_settings.cpp): 1 and
 * its page and its index among the rows shown now; 2 when it exists but is not
 * shown now (it depends on another setting); 0 when there is none. */
int pc_ui_settings_find(const char* id, int* page, int* row);
```

and in `pc/src/ui/ui_settings.cpp`, after `pc_ui_settings_row`'s definition:

```cpp
extern "C" int pc_ui_settings_find(const char* id, int* page, int* row)
{
    build();
    if (!id)
        return 0;
    for (int p = 0; p < PC_UI_SETTINGS_PAGES; p++) {
        int shown = 0;
        for (const Row& r : pages[p]) {
            bool visible = !r.shown || r.shown(r);
            if (!std::strcmp(r.id, id)) {
                if (!visible)
                    return 2;
                if (page)
                    *page = p;
                if (row)
                    *row = shown;
                return 1;
            }
            shown += visible;
        }
    }
    return 0;
}
```

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target ui_settings_probe ui_settings_probe_hooks -j 2 >/dev/null
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R ui_settings_probe --output-on-failure | tail -2
```

Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 5: `remove_kind`** — in `pc/src/editor_schema.h`, after `void add_kind(Kind k);` add:

```cpp
/* Unregister an add_kind kind (a plugin's settings when it unloads; pc/src/lua);
 * true when there was one. Pointers to it are gone: Details and the RPC find
 * kinds by name every frame. */
bool remove_kind(const std::string& name);
```

and in `pc/src/editor_schema.cpp`, after `add_kind`'s definition:

```cpp
bool remove_kind(const std::string& name)
{
    std::deque<Kind>& kinds = added_kinds();
    for (auto it = kinds.begin(); it != kinds.end(); ++it)
        if (it->name == name) {
            kinds.erase(it);
            return true;
        }
    return false;
}
```

(Its test is in Task 26: after a plugin with settings is disabled, its kind is gone and the schema lint is clean.)

- [ ] **Step 6: Build the game**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tail -1
```

Expected: `Built .../rood_battle_probe: ...`.

- [ ] **Step 7: Commit**

```bash
git add pc/src/ui/ui_settings.h pc/src/ui/ui_settings.cpp pc/tests/ui_settings_probe.cpp pc/src/editor_schema.h pc/src/editor_schema.cpp
git commit -m "$(cat <<'EOF'
Settings registries: find a System row by id; remove a schema kind

For plugins: rood.settings reaches rows by id, and a plugin's settings
kind goes when the plugin unloads.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 23: `rood.settings`, generated from the settings registries

**Files:**
- Create: `pc/src/lua/lua_api_settings.cpp`, `pc/tests/lua/plugins/test.settings/plugin.lua`, `main.lua`, `tests/settings_test.lua`

- [ ] **Step 1: Write the failing Lua tests** — `pc/tests/lua/plugins/test.settings/plugin.lua`:

```lua
return { id = "test.settings", name = "Settings tests", version = "1.0.0", api_version = 1 }
```

`pc/tests/lua/plugins/test.settings/main.lua`:

```lua
-- Tests only (tests/settings_test.lua).
```

`pc/tests/lua/plugins/test.settings/tests/settings_test.lua`:

```lua
local t = rood.test
local S = rood.settings

t.case("System rows read with their types", function()
  t.eq(type(S.get("skip_intro")), "boolean")
  t.eq(S.get("qol_skip_charging"), "off")
  t.eq(type(S.get("ui_menu_opacity")), "number")
end)

t.case("validation is enforced", function()
  t.raises(function() S.set("skip_intro", "yes") end, "boolean")
  t.raises(function() S.set("qol_skip_charging", "warp") end, "one of")
  t.raises(function() S.set("ui_menu_opacity", 5) end, "between")
  t.raises(function() S.set("no_such_setting", 1) end, "unknown setting")
  t.raises(function() S.set("reset_camera", true) end, "action")
  t.raises(function() S.reset("skip_intro") end, "no recorded default")
end)

t.case("valid changes apply and read back", function()
  t.eq(S.set("qol_skip_charging", "fast"), "fast")
  t.eq(S.get("qol_skip_charging"), "fast")
  t.eq(S.set("qol_skip_charging", "Off"), "off") -- the label works too
  t.near(S.set("ui_menu_opacity", 0.5), 0.5, 1e-6)
  t.eq(S.set("skip_intro", true), true) -- lua_smoke checks settings.cfg for it
end)

t.case("list has every row's metadata", function()
  local seen
  for _, s in ipairs(S.list()) do
    if s.id == "qol_skip_charging" then seen = s end
  end
  t.ok(seen, "qol_skip_charging listed")
  t.eq(seen.kind, "choice")
  t.eq(seen.page, "gameplay")
  t.eq(seen.source, "system")
  t.eq(#seen.choices, 3)
  t.eq(seen.choices[2].id, "fast")
  t.ok(#S.pages() >= 7)
end)

t.case("plugin settings: define, validate, reset", function()
  local id = S.define("volume", { type = "number", label = "Volume", min = 0, max = 1, step = 0.05, default = 0.5 })
  t.eq(id, "plugin:test.settings/volume")
  S.reset(id)
  t.near(S.get(id), 0.5)
  t.raises(function() S.set(id, 2) end, "between")
  t.raises(function() S.set(id, "loud") end, "number")
  S.set(id, 0.25)
  t.near(S.get(id), 0.25)
  S.reset(id)
  t.near(S.get(id), 0.5)
  t.raises(function() S.define("volume", { type = "number", min = 0, max = 2 }) end, "already defined")
  local mode = S.define("mode", { type = "enum", options = { "calm", "loud" }, default = "calm" })
  t.raises(function() S.set(mode, "quiet") end, "one of")
  t.eq(S.set(mode, "loud"), "loud")
  local listed = S.list({ page = "plugin:test.settings" })
  t.ok(#listed >= 2, "volume and mode listed")
  t.eq(listed[1].source, "plugin")
  t.eq(listed[1].id, id)
end)

t.case("on_change fires once per change", function()
  local id = S.define("flag", { type = "boolean", default = false })
  S.set(id, false)
  local seen = {}
  S.on_change(id, function(v) seen[#seen + 1] = v end)
  S.set(id, true)
  t.wait(2)
  t.eq(seen, { true })
end)
```

- [ ] **Step 2: Run them and see them fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/lua_smoke.py --tests-only --plugin test.settings 2>&1 | tail -4
```

Expected: `AssertionError: Lua tests failed:` with `attempt to index a nil value (upvalue 'S')` (there is no `rood.settings` yet).

- [ ] **Step 3: Write `pc/src/lua/lua_api_settings.cpp`**

```cpp
// rood.settings (pc/docs/Extensions.md, "Settings"): one namespace over the
// game's settings registries, validated by their own metadata, so a setting
// a plugin reads or changes is one the menus and the dev tools show:
//   - the System pages' rows (pc/src/ui/ui_settings.h): ids such as
//     "widescreen" or "qol_skip_charging"; a toggle is a boolean, a choice
//     its choice's id (the label in lower case: "fast"), a slider a number;
//     actions run with rood.settings.run; greyed rows refuse, saying why;
//   - standalone kinds of the editor's property schema (editor_schema.h
//     add_kind) with one object: "<kind>/<field>";
//   - a plugin's own settings (rood.settings.define): the schema kind
//     "plugin:<plugin id>" (so the dev tools' Details can draw them), the
//     values in the plugin's data folder (settings.json).
// Generated from that metadata (ids, kinds, ranges, choices, availability):
// nothing here names a single setting. System changes reach settings.cfg
// through ui_settings' own save. Game side: not in lua_probe.
#include "rl_internal.h"
#include "editor_schema.h"
#include "light_prefabs.h"
#include "ui/ui_settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace rl {
namespace {
using rood_schema::Field;
using rood_schema::Kind;

const char* const ROW_KINDS[] = { "choice", "toggle", "slider", "action", "binding", "info" }; /* PC_UI_SET_* */

std::string slug(const char* label)
{
    std::string out;
    for (const char* s = label; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (std::isalnum(c))
            out += (char)std::tolower(c);
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

void set_text(lua_State* L, const char* key, const std::string& value)
{
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}
void set_number(lua_State* L, const char* key, double value)
{
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
}
void set_flag(lua_State* L, const char* key, bool value)
{
    lua_pushboolean(L, value);
    lua_setfield(L, -2, key);
}

/* ---- System rows ---- */
struct RowRef {
    int page = -1, row = -1;
    pc_ui_setting_row r;
};
/* 1: shown (ref filled), 2: exists but not shown now, 0: no such row. */
int find_row(const std::string& id, RowRef& ref)
{
    int found = pc_ui_settings_find(id.c_str(), &ref.page, &ref.row);
    if (found == 1 && !pc_ui_settings_row(ref.page, ref.row, &ref.r))
        return 0;
    return found;
}

std::string choices_text(const RowRef& ref)
{
    std::string s;
    for (int c = 0; c < ref.r.count; c++) {
        const char* name = nullptr;
        if (pc_ui_settings_choice(ref.page, ref.row, c, &name, nullptr) && name)
            s += (s.empty() ? "" : ", ") + slug(name);
    }
    return s;
}

void push_row_value(lua_State* L, const RowRef& ref)
{
    const pc_ui_setting_row& r = ref.r;
    switch (r.kind) {
    case PC_UI_SET_TOGGLE:
        lua_pushboolean(L, r.index == 1);
        return;
    case PC_UI_SET_CHOICE: {
        const char* name = nullptr;
        if (r.index >= 0 && pc_ui_settings_choice(ref.page, ref.row, r.index, &name, nullptr) && name)
            lua_pushstring(L, slug(name).c_str());
        else
            lua_pushnil(L);
        return;
    }
    case PC_UI_SET_SLIDER:
        if (r.step >= 1 && std::floor(r.step) == r.step && std::floor(r.min) == r.min)
            lua_pushinteger(L, (lua_Integer)std::lround(r.number));
        else
            lua_pushnumber(L, r.number);
        return;
    case PC_UI_SET_ACTION:
        lua_pushnil(L);
        return;
    default:
        lua_pushstring(L, r.value);
        return;
    }
}

/* The Lua value at idx for a row, validated (raises with what is allowed). */
double row_value(lua_State* L, int idx, const RowRef& ref, const char* id)
{
    const pc_ui_setting_row& r = ref.r;
    if (r.kind == PC_UI_SET_ACTION)
        luaL_error(L, "'%s' is an action: run it with rood.settings.run", id);
    if (r.kind == PC_UI_SET_BINDING || r.kind == PC_UI_SET_INFO)
        luaL_error(L, "'%s' is read-only here", id);
    if (!r.enabled)
        luaL_error(L, "'%s' cannot change now: %s", id, r.reason);
    if (r.kind == PC_UI_SET_TOGGLE) {
        if (!lua_isboolean(L, idx))
            luaL_error(L, "'%s' is a boolean (true or false), got a %s", id, luaL_typename(L, idx));
        return lua_toboolean(L, idx) ? 1 : 0;
    }
    if (r.kind == PC_UI_SET_SLIDER) {
        if (lua_type(L, idx) != LUA_TNUMBER)
            luaL_error(L, "'%s' is a number, got a %s", id, luaL_typename(L, idx));
        double v = lua_tonumber(L, idx);
        if (!(v >= r.min - 1e-6 && v <= r.max + 1e-6))
            luaL_error(L, "'%s' must be between %g and %g (got %g)", id, (double)r.min, (double)r.max, v);
        return v;
    }
    if (lua_type(L, idx) != LUA_TSTRING)
        luaL_error(L, "'%s' is one of: %s (got a %s)", id, choices_text(ref).c_str(), luaL_typename(L, idx));
    const char* want = lua_tostring(L, idx);
    for (int c = 0; c < r.count; c++) {
        const char* name = nullptr;
        const char* reason = nullptr;
        if (!pc_ui_settings_choice(ref.page, ref.row, c, &name, &reason) || !name)
            continue;
        if (slug(name) == want || !std::strcmp(name, want)) {
            if (reason)
                luaL_error(L, "'%s' cannot be %s here: %s", id, want, reason);
            return c;
        }
    }
    luaL_error(L, "'%s' must be one of: %s (got '%s')", id, choices_text(ref).c_str(), want);
    return 0;
}

void push_row_info(lua_State* L, const RowRef& ref, const char* page)
{
    const pc_ui_setting_row& r = ref.r;
    lua_createtable(L, 0, 16);
    set_text(L, "id", r.id);
    set_text(L, "label", r.label);
    set_text(L, "help", r.help ? r.help : "");
    set_text(L, "kind", ROW_KINDS[r.kind]);
    set_text(L, "page", page);
    set_text(L, "source", "system");
    push_row_value(L, ref);
    lua_setfield(L, -2, "value");
    set_text(L, "text", r.value);
    set_flag(L, "enabled", r.enabled != 0);
    if (r.reason[0])
        set_text(L, "reason", r.reason);
    set_flag(L, "game_option", r.game_option != 0);
    if (r.kind == PC_UI_SET_SLIDER) {
        set_number(L, "min", r.min);
        set_number(L, "max", r.max);
        set_number(L, "step", r.step);
    }
    if (r.kind == PC_UI_SET_CHOICE || r.kind == PC_UI_SET_TOGGLE) {
        lua_createtable(L, r.count, 0);
        for (int c = 0; c < r.count; c++) {
            const char* name = nullptr;
            const char* why = nullptr;
            pc_ui_settings_choice(ref.page, ref.row, c, &name, &why);
            lua_createtable(L, 0, 4);
            set_text(L, "id", slug(name ? name : "?"));
            set_text(L, "label", name ? name : "?");
            set_flag(L, "available", why == nullptr);
            if (why)
                set_text(L, "reason", why);
            lua_rawseti(L, -2, c + 1);
        }
        lua_setfield(L, -2, "choices");
    }
}

/* ---- Schema fields ---- */
/* "<kind>/<field>" of a standalone kind with one object. */
const Field* schema_field(const std::string& id, const Kind** kind)
{
    size_t slash = id.rfind('/');
    if (slash == std::string::npos)
        return nullptr;
    const Kind* k = rood_schema::find_kind(id.substr(0, slash));
    if (!k || k->object != rood_selection::KIND_NONE || k->count != 1)
        return nullptr;
    const Field* f = k->field(id.substr(slash + 1));
    if (f && kind)
        *kind = k;
    return f;
}

std::string options_text(const Field& f)
{
    std::string s;
    for (const std::string& o : f.options)
        s += (s.empty() ? "" : ", ") + o;
    return s;
}

void push_field_value(lua_State* L, const Field& f, const prefab::Value& v)
{
    switch (f.type) {
    case rood_schema::TYPE_BOOL:
        lua_pushboolean(L, v.x() != 0);
        return;
    case rood_schema::TYPE_INT:
        lua_pushinteger(L, (lua_Integer)std::lround(v.x()));
        return;
    case rood_schema::TYPE_ENUM: {
        long i = std::lround(v.x());
        if (i >= 0 && i < (long)f.options.size())
            lua_pushstring(L, f.options[(size_t)i].c_str());
        else
            lua_pushnil(L);
        return;
    }
    case rood_schema::TYPE_COLOUR:
    case rood_schema::TYPE_VEC3:
        lua_createtable(L, 3, 0);
        for (int c = 0; c < 3; c++) {
            lua_pushnumber(L, v.at(c));
            lua_rawseti(L, -2, c + 1);
        }
        return;
    default:
        lua_pushnumber(L, v.x());
        return;
    }
}

/* The Lua value at idx (absolute) for a field, validated (raises with what is allowed). */
prefab::Value field_value(lua_State* L, int idx, const Field& f, const char* id)
{
    auto in_range = [&](double x) {
        if (!(x >= f.min - 1e-6 && x <= f.max + 1e-6))
            luaL_error(L, "'%s' must be between %g and %g (got %g)", id, (double)f.min, (double)f.max, x);
    };
    switch (f.type) {
    case rood_schema::TYPE_BOOL:
        if (!lua_isboolean(L, idx))
            luaL_error(L, "'%s' is a boolean (true or false), got a %s", id, luaL_typename(L, idx));
        return prefab::Value::scalar(lua_toboolean(L, idx) ? 1.0f : 0.0f);
    case rood_schema::TYPE_ENUM: {
        if (lua_type(L, idx) != LUA_TSTRING)
            luaL_error(L, "'%s' is one of: %s (got a %s)", id, options_text(f).c_str(), luaL_typename(L, idx));
        const char* want = lua_tostring(L, idx);
        for (size_t k = 0; k < f.options.size(); k++)
            if (f.options[k] == want || slug(f.options[k].c_str()) == want)
                return prefab::Value::scalar((float)k);
        luaL_error(L, "'%s' must be one of: %s (got '%s')", id, options_text(f).c_str(), want);
        return prefab::Value();
    }
    case rood_schema::TYPE_COLOUR:
    case rood_schema::TYPE_VEC3: {
        if (!lua_istable(L, idx) || lua_rawlen(L, idx) != 3)
            luaL_error(L, "'%s' is a table of 3 numbers", id);
        float c[3];
        for (int k = 0; k < 3; k++) {
            lua_rawgeti(L, idx, k + 1);
            if (lua_type(L, -1) != LUA_TNUMBER)
                luaL_error(L, "'%s' is a table of 3 numbers", id);
            c[k] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
            in_range(c[k]);
        }
        return prefab::Value::vec(c[0], c[1], c[2]);
    }
    default: {
        if (lua_type(L, idx) != LUA_TNUMBER)
            luaL_error(L, "'%s' is a number, got a %s", id, luaL_typename(L, idx));
        double x = lua_tonumber(L, idx);
        if (f.type == rood_schema::TYPE_INT && std::floor(x) != x)
            luaL_error(L, "'%s' is an integer (got %g)", id, x);
        in_range(x);
        return prefab::Value::scalar((float)x);
    }
    }
}

void push_field_info(lua_State* L, const Kind& k, const Field& f)
{
    lua_createtable(L, 0, 16);
    set_text(L, "id", k.name + "/" + f.name);
    set_text(L, "label", f.label);
    set_text(L, "help", f.tooltip);
    set_text(L, "kind", rood_schema::type_name(f.type));
    set_text(L, "page", k.name);
    set_text(L, "category", f.category);
    set_text(L, "source", k.name.compare(0, 7, "plugin:") == 0 ? "plugin" : "schema");
    push_field_value(L, f, f.get(0));
    lua_setfield(L, -2, "value");
    set_text(L, "text", rood_schema::format(f, f.get(0)));
    set_flag(L, "enabled", true);
    set_number(L, "min", f.min);
    set_number(L, "max", f.max);
    set_number(L, "step", f.step);
    push_field_value(L, f, f.def);
    lua_setfield(L, -2, "default");
    if (f.type == rood_schema::TYPE_ENUM) {
        lua_createtable(L, (int)f.options.size(), 0);
        for (size_t c = 0; c < f.options.size(); c++) {
            lua_createtable(L, 0, 3);
            set_text(L, "id", slug(f.options[c].c_str()));
            set_text(L, "label", f.options[c]);
            set_flag(L, "available", true);
            lua_rawseti(L, -2, (lua_Integer)c + 1);
        }
        lua_setfield(L, -2, "choices");
    }
}

/* Pushes a setting's value; false when there is none shown. */
bool push_setting(lua_State* L, const std::string& id)
{
    RowRef ref;
    if (find_row(id, ref) == 1) {
        push_row_value(L, ref);
        return true;
    }
    const Kind* k = nullptr;
    if (const Field* f = schema_field(id, &k)) {
        push_field_value(L, *f, f->get(0));
        return true;
    }
    return false;
}

/* What a watch compares; "" when unknown or not shown. */
std::string current_text(const std::string& id)
{
    RowRef ref;
    if (find_row(id, ref) == 1)
        return std::to_string(ref.r.index) + "|" + ref.r.value + "|" + std::to_string(ref.r.number);
    const Kind* k = nullptr;
    if (const Field* f = schema_field(id, &k))
        return rood_schema::to_json(*f, f->get(0));
    return "";
}

/* ---- A plugin's own settings ---- */
struct PluginSettings {
    std::map<std::string, prefab::Value> values;
    std::map<std::string, std::string> specs; /* name -> its spec as JSON (a redefinition must match) */
    prefab::Json saved;                       /* settings.json as last read or written */
    bool loaded = false, dirty = false;
};
std::map<std::string, PluginSettings>& plugin_settings()
{
    static auto* map = new std::map<std::string, PluginSettings>();
    return *map;
}
std::map<std::string, Kind>& plugin_kinds()
{
    static auto* map = new std::map<std::string, Kind>();
    return *map;
}

PluginSettings& settings_of(Plugin* p)
{
    PluginSettings& s = plugin_settings()[p->id()];
    if (!s.loaded) {
        s.loaded = true;
        s.saved = prefab::Json::object();
        std::string text, why;
        if (!p->data_dir.empty() && read_file(p->data_dir + "/settings.json", &text, RL_MAX_FILE, &why)) {
            prefab::Json j;
            if (prefab::json_parse(text.c_str(), j, &why) && j.type == prefab::Json::Object)
                s.saved = j;
            else
                log(LOG_WARN, p->id(), "settings.json is unreadable (" + why + "); using the defaults");
        }
    }
    return s;
}

void save_settings_of(const std::string& id)
{
    auto s = plugin_settings().find(id);
    auto k = plugin_kinds().find(id);
    Plugin* p = find(id);
    if (s == plugin_settings().end() || !s->second.dirty || k == plugin_kinds().end() || !p || p->data_dir.empty())
        return;
    prefab::Json out = s->second.saved; /* settings not defined in this load keep their values */
    for (const Field& f : k->second.fields) {
        auto v = s->second.values.find(f.name);
        prefab::Json j;
        if (v != s->second.values.end() && prefab::json_parse(rood_schema::to_json(f, v->second).c_str(), j, nullptr))
            out.set(f.name.c_str(), j);
    }
    std::string why;
    if (write_file(p->data_dir + "/settings.json", prefab::json_write(out) + "\n", &why)) {
        s->second.saved = out;
        s->second.dirty = false;
    } else
        log(LOG_ERROR, id, "settings: " + why);
}

Field make_field(lua_State* L, const std::string& name, const Value& spec)
{
    auto text = [&](const char* key, const std::string& fallback) {
        const Value* v = spec.get(key);
        if (!v || v->kind == Value::NIL)
            return fallback;
        if (v->kind != Value::STRING)
            luaL_error(L, "rood.settings.define: %s must be a string", key);
        return v->s;
    };
    auto number = [&](const char* key, bool required, double fallback) {
        const Value* v = spec.get(key);
        if (!v || v->kind == Value::NIL) {
            if (required)
                luaL_error(L, "rood.settings.define: '%s' needs %s", name.c_str(), key);
            return fallback;
        }
        if (v->kind != Value::INTEGER && v->kind != Value::NUMBER)
            luaL_error(L, "rood.settings.define: %s must be a number", key);
        return v->kind == Value::INTEGER ? (double)v->i : v->n;
    };
    std::string type = text("type", ""), label = text("label", name), category = text("category", "Settings"),
                help = text("help", ""), unit = text("unit", "");
    const Value* def = spec.get("default");
    if (type == "number" || type == "integer") {
        double min = number("min", true, 0), max = number("max", true, 1);
        if (!(min < max))
            luaL_error(L, "rood.settings.define: '%s' needs min < max", name.c_str());
        double step = number("step", false, type == "integer" ? 1 : (max - min) / 100);
        double d = number("default", false, min);
        if (d < min || d > max)
            luaL_error(L, "rood.settings.define: '%s' default %g is outside %g..%g", name.c_str(), d, min, max);
        Field f = rood_schema::number_field(name.c_str(), label.c_str(), category.c_str(), (float)min, (float)max,
            (float)step, (float)d, unit.c_str(), false, help.c_str());
        if (type == "integer")
            f.type = rood_schema::TYPE_INT;
        return f;
    }
    if (type == "boolean") {
        if (def && def->kind != Value::NIL && def->kind != Value::BOOLEAN)
            luaL_error(L, "rood.settings.define: '%s' default must be true or false", name.c_str());
        return rood_schema::bool_field(name.c_str(), label.c_str(), category.c_str(), def && def->kind == Value::BOOLEAN && def->b,
            help.c_str());
    }
    if (type == "enum") {
        std::vector<std::string> options;
        if (const Value* list = spec.get("options"))
            for (const auto& kv : list->table)
                if (kv.second.kind == Value::STRING)
                    options.push_back(kv.second.s);
        if (options.empty())
            luaL_error(L, "rood.settings.define: '%s' needs options (a list of strings)", name.c_str());
        int index = 0;
        if (def && def->kind == Value::STRING) {
            auto it = std::find(options.begin(), options.end(), def->s);
            if (it == options.end())
                luaL_error(L, "rood.settings.define: '%s' default '%s' is not one of its options", name.c_str(), def->s.c_str());
            index = (int)(it - options.begin());
        }
        return rood_schema::enum_field(name.c_str(), label.c_str(), category.c_str(), options, index, help.c_str());
    }
    if (type == "colour" || type == "color") {
        prefab::Value d = prefab::Value::vec(1, 1, 1);
        if (def && def->kind == Value::TABLE && def->length() == 3) {
            float c[3] = { 1, 1, 1 };
            for (const auto& kv : def->table)
                if (kv.first.kind == Value::INTEGER && kv.first.i >= 1 && kv.first.i <= 3)
                    c[kv.first.i - 1] = (float)(kv.second.kind == Value::INTEGER ? (double)kv.second.i : kv.second.n);
            d = prefab::Value::vec(c[0], c[1], c[2]);
        }
        return rood_schema::colour_field(name.c_str(), label.c_str(), category.c_str(), 1.0f, d, help.c_str());
    }
    luaL_error(L, "rood.settings.define: '%s' type must be number, integer, boolean, enum or colour", name.c_str());
    return Field();
}

/* ---- Watches ---- */
struct Watch {
    unsigned long long id;
    std::string plugin;
    unsigned generation;
    int ref;
    std::string setting, last;
};
std::vector<Watch>& watches()
{
    static auto* list = new std::vector<Watch>();
    return *list;
}
unsigned long long next_watch = 1;
bool flush_pending;
int flush_polls;
double flush_since;

/* ---- rood.settings ---- */
int api_get(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    RowRef ref;
    if (find_row(id, ref) == 2)
        return luaL_error(L, "rood.settings.get: '%s' is not shown now (it depends on another setting)", id);
    if (!push_setting(L, id))
        return luaL_error(L, "rood.settings.get: unknown setting '%s'", id);
    return 1;
}

int api_set(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    luaL_checkany(L, 2);
    RowRef ref;
    int found = find_row(id, ref);
    if (found == 2)
        return luaL_error(L, "rood.settings.set: '%s' is not shown now (it depends on another setting)", id);
    if (found == 1) {
        double v = row_value(L, 2, ref, id);
        if (pc_ui_settings_set(ref.page, ref.row, v) && !flush_pending) {
            flush_pending = true;
            flush_polls = 0;
            flush_since = now_seconds();
        }
        push_setting(L, id);
        return 1;
    }
    const Kind* k = nullptr;
    if (const Field* f = schema_field(id, &k)) {
        prefab::Value v = field_value(L, 2, *f, id);
        rood_schema::apply_at(*k, *f, 0, v);
        push_setting(L, id);
        return 1;
    }
    return luaL_error(L, "rood.settings.set: unknown setting '%s'", id);
}

int api_reset(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    RowRef ref;
    if (find_row(id, ref))
        return luaL_error(L, "rood.settings.reset: '%s' (a System row) has no recorded default; set it instead", id);
    const Kind* k = nullptr;
    const Field* f = schema_field(id, &k);
    if (!f)
        return luaL_error(L, "rood.settings.reset: unknown setting '%s'", id);
    rood_schema::apply_reset_at(*k, *f, 0);
    push_setting(L, id);
    return 1;
}

int api_run(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    RowRef ref;
    if (find_row(id, ref) != 1 || ref.r.kind != PC_UI_SET_ACTION)
        return luaL_error(L, "rood.settings.run: '%s' is not an action shown now", id);
    if (!ref.r.enabled)
        return luaL_error(L, "rood.settings.run: '%s' cannot run now: %s", id, ref.r.reason);
    lua_pushboolean(L, pc_ui_settings_activate(ref.page, ref.row));
    return 1;
}

int api_list(lua_State* L)
{
    std::string filter;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "page");
        if (lua_type(L, -1) == LUA_TSTRING)
            filter = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_newtable(L);
    lua_Integer n = 0;
    for (int page = 0; page < pc_ui_settings_pages(); page++) {
        const char* pid = pc_ui_settings_page_id(page);
        if (!filter.empty() && filter != pid)
            continue;
        for (int row = 0; row < pc_ui_settings_rows(page); row++) {
            RowRef ref;
            ref.page = page;
            ref.row = row;
            if (!pc_ui_settings_row(page, row, &ref.r))
                continue;
            push_row_info(L, ref, pid);
            lua_rawseti(L, -2, ++n);
        }
    }
    for (const Kind* k : rood_schema::all_kinds()) {
        if (k->object != rood_selection::KIND_NONE || k->count != 1 || (!filter.empty() && filter != k->name))
            continue;
        for (const Field& f : k->fields) {
            push_field_info(L, *k, f);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}

int api_pages(lua_State* L)
{
    lua_createtable(L, pc_ui_settings_pages(), 0);
    for (int page = 0; page < pc_ui_settings_pages(); page++) {
        lua_createtable(L, 0, 2);
        set_text(L, "id", pc_ui_settings_page_id(page));
        set_text(L, "title", pc_ui_settings_page_title(page));
        lua_rawseti(L, -2, page + 1);
    }
    return 1;
}

int api_define(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    size_t length = std::strlen(name);
    bool ok = length > 0 && length <= 48;
    for (const char* s = name; *s; s++)
        ok &= std::isalnum((unsigned char)*s) || *s == '_';
    if (!ok)
        return luaL_error(L, "rood.settings.define: bad name '%s' (1 to 48 letters, digits and _)", name);
    Plugin* p = plugin_of(L);
    std::string kind_name = "plugin:" + p->id(), id = kind_name + "/" + name;
    Value spec = Value::from_lua(L, 2);
    std::string spec_text = to_json(spec);
    PluginSettings& s = settings_of(p);
    auto old = s.specs.find(name);
    if (old != s.specs.end()) {
        if (old->second != spec_text)
            return luaL_error(L, "rood.settings.define: '%s' is already defined differently", name);
        lua_pushstring(L, id.c_str());
        return 1;
    }
    Field f = make_field(L, name, spec);
    std::string pid = p->id(), key = name;
    f.get = [pid, key](int) { return plugin_settings()[pid].values[key]; };
    f.set = [pid, key](int, const prefab::Value& v) {
        PluginSettings& ps = plugin_settings()[pid];
        ps.values[key] = v;
        ps.dirty = true;
    };
    prefab::Value initial = f.def;
    if (const prefab::Json* saved = s.saved.get(name)) {
        prefab::Value v;
        std::string why;
        if (rood_schema::from_json(f, *saved, v, &why))
            initial = rood_schema::clamp(f, v);
    }
    s.values[key] = initial;
    s.specs[key] = spec_text;
    Kind& k = plugin_kinds()[pid];
    if (k.name.empty()) {
        k.name = kind_name;
        k.label = p->manifest.name + " (plugin)";
        k.object = rood_selection::KIND_NONE;
        k.count = 1;
    }
    k.fields.push_back(f);
    rood_schema::add_kind(k);
    lua_pushstring(L, id.c_str());
    return 1;
}

int api_on_change(lua_State* L)
{
    const char* id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    RowRef ref;
    const Kind* k = nullptr;
    if (!find_row(id, ref) && !schema_field(id, &k))
        return luaL_error(L, "rood.settings.on_change: unknown setting '%s'", id);
    Plugin* p = plugin_of(L);
    lua_settop(L, 2);
    int fn = luaL_ref(L, LUA_REGISTRYINDEX);
    watches().push_back({ next_watch, p->id(), p->generation, fn, id, current_text(id) });
    lua_pushinteger(L, (lua_Integer)next_watch++);
    return 1;
}

int api_off_change(lua_State* L)
{
    lua_Integer handle = luaL_checkinteger(L, 1);
    Plugin* p = plugin_of(L);
    std::vector<Watch>& list = watches();
    for (auto it = list.begin(); it != list.end(); ++it)
        if ((lua_Integer)it->id == handle && it->plugin == p->id()) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            list.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}

/* Every tick and frame: changed settings call their watchers; System changes reach
 * settings.cfg (ui_settings' save) after 30 polls or half a second; plugin settings
 * are written when they changed. */
void poll_settings()
{
    std::vector<Watch> snapshot = watches();
    for (const Watch& w : snapshot) {
        std::string now = current_text(w.setting);
        auto it = std::find_if(watches().begin(), watches().end(), [&](const Watch& x) { return x.id == w.id; });
        if (it == watches().end() || now == it->last)
            continue;
        it->last = now;
        Plugin* p = find(w.plugin);
        if (!p || p->state != PLUGIN_LOADED || p->generation != w.generation || !p->disable_reason.empty())
            continue;
        run(p, ("settings change " + w.setting).c_str(), BUDGET_CALL, [&](lua_State* L) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, w.ref);
            if (!push_setting(L, w.setting))
                lua_pushnil(L);
            lua_call(L, 1, 0);
        });
    }
    if (flush_pending && (++flush_polls >= 30 || now_seconds() - flush_since >= 0.5)) {
        flush_pending = false;
        pc_ui_settings_flush();
    }
    for (auto& kv : plugin_settings())
        if (kv.second.dirty)
            save_settings_of(kv.first);
}

void drop_settings(Plugin* p)
{
    save_settings_of(p->id());
    std::vector<Watch>& list = watches();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Watch& w) { return w.plugin == p->id(); }), list.end());
    if (plugin_kinds().erase(p->id()))
        rood_schema::remove_kind("plugin:" + p->id());
    plugin_settings().erase(p->id());
}

void flush_settings()
{
    for (auto& kv : plugin_settings())
        save_settings_of(kv.first);
    if (flush_pending)
        pc_ui_settings_flush();
}

void open_settings(lua_State* L, Plugin*)
{
    lua_newtable(L);
    set_cfunction(L, -1, "get", api_get);
    set_cfunction(L, -1, "set", api_set);
    set_cfunction(L, -1, "reset", api_reset);
    set_cfunction(L, -1, "run", api_run);
    set_cfunction(L, -1, "list", api_list);
    set_cfunction(L, -1, "pages", api_pages);
    set_cfunction(L, -1, "define", api_define);
    set_cfunction(L, -1, "on_change", api_on_change);
    set_cfunction(L, -1, "off_change", api_off_change);
    lock_table(L);
    lua_setfield(L, -2, "settings");
}

struct Register {
    Register()
    {
        api_openers().push_back(open_settings);
        unload_hooks().push_back(drop_settings);
        poll_hooks().push_back(poll_settings);
        flush_hooks().push_back(flush_settings);
    }
} register_settings;
} // namespace
} // namespace rl
```

- [ ] **Step 4: Build and see the tests pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/lua_smoke.py --tests-only --plugin test.settings 2>&1 | tail -8
```

Expected: six `PASS  test.settings settings_test.lua: ...` lines and `PASS: 6 Lua tests in one game`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/lua/lua_api_settings.cpp pc/tests/lua/plugins/test.settings
git commit -m "$(cat <<'EOF'
rood.settings: the game's settings registries, validated, for plugins

Generated from the System pages' rows and the editor schema's
standalone kinds: get/set/reset/run/list/pages with each registry's
own validation (kinds, ranges, choices, why a row is greyed); a
plugin's own settings are a schema kind of their own, saved in its data
folder; on_change watches any of them.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 24: The Lua console in the dev overlay

**Files:**
- Create: `pc/src/lua/lua_console.cpp`
- Modify: `pc/src/lua/lua_rpc.cpp` (remove the Task 20 stubs)

- [ ] **Step 1: Write `pc/src/lua/lua_console.cpp`**

```cpp
// The Lua console (the dev overlay: Window > Lua console; pc/docs/Extensions.md
// "Developer tools"): every plugin's log, errors with their tracebacks in
// red; an input line evaluated in a sandbox (the console's own, or a loaded
// plugin's) with Up / Down history, through rl::console_submit like RPC lua
// {action "console"}; the plugins (state, memory, faults, slowest call,
// elevated) with on/off and reload; the budgets (tunables(), settings
// plugin_*) and the master switch for elevated permissions. Game side.
#include "imgui.h"
#include "lua_runtime.h"
#include "rl_internal.h"

#include <algorithm>

extern "C" void pc_settings_save(void) __attribute__((weak));

namespace {
bool open;
char input[2048];
std::string target = "@console";
unsigned long long cleared;
int history_pos = -1;
bool scroll_down;

int history(ImGuiInputTextCallbackData* data)
{
    const std::vector<std::string>& h = rl::console_history();
    if (h.empty())
        return 0;
    if (data->EventKey == ImGuiKey_UpArrow)
        history_pos = history_pos < 0 ? (int)h.size() - 1 : std::max(0, history_pos - 1);
    else if (data->EventKey == ImGuiKey_DownArrow)
        history_pos = history_pos < 0 || history_pos + 1 >= (int)h.size() ? -1 : history_pos + 1;
    data->DeleteChars(0, data->BufTextLen);
    if (history_pos >= 0)
        data->InsertChars(0, h[(size_t)history_pos].c_str());
    return 0;
}

void plugins_table()
{
    if (!ImGui::BeginTable("##lua plugins", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Plugin");
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("Memory");
    ImGui::TableSetupColumn("Faults");
    ImGui::TableSetupColumn("Slowest");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (auto& up : rl::all()) {
        rl::Plugin& p = *up;
        if (p.console)
            continue;
        ImGui::PushID(p.id().c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(p.id().c_str());
        if (!p.granted.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "[elevated]");
            if (ImGui::IsItemHovered()) {
                std::string granted;
                for (const std::string& g : p.granted)
                    granted += (granted.empty() ? "" : ", ") + g;
                ImGui::SetTooltip("Granted: %s", granted.c_str());
            }
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(rl::state_name(p.state));
        if (ImGui::IsItemHovered() && !p.status.empty())
            ImGui::SetTooltip("%s", p.status.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.0f / %.0f KB", p.mem.used / 1024.0, p.mem.cap / 1024.0);
        ImGui::TableNextColumn();
        ImGui::Text("%u (%u budget)", (unsigned)p.stats.faults, (unsigned)p.stats.kills);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f ms", p.stats.max_ms);
        ImGui::TableNextColumn();
        bool enabled = rl::plugin_enabled(&p);
        if (ImGui::Checkbox("##on", &enabled))
            rl::set_enabled(p.id(), enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload"))
            p.reload_requested = true;
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void budgets()
{
    float width = ImGui::GetFontSize() * 9;
    for (const rl::Tunable& t : rl::tunables()) {
        double value = rl::get_tunable(t);
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputDouble(t.label, &value, 0, 0, "%.10g", ImGuiInputTextFlags_EnterReturnsTrue))
            rl::set_tunable(t, value);
    }
    bool elevated = rl::config.elevated;
    if (ImGui::Checkbox("Allow plugins to request elevated permissions", &elevated)) {
        rl::config.elevated = elevated;
        rl::config_changed();
    }
    if (ImGui::Button("Save settings") && pc_settings_save)
        pc_settings_save();
}
} // namespace

extern "C" void pc_lua_console_menu_item(void) { ImGui::MenuItem("Lua console", nullptr, &open); }

extern "C" void pc_lua_console_window(void)
{
    if (!open)
        return;
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 46, em * 28), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lua console", &open)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(em * 14);
    if (ImGui::BeginCombo("##lua target", target.c_str())) {
        if (ImGui::Selectable("@console", target == "@console"))
            target = "@console";
        for (auto& p : rl::all())
            if (!p->console && p->state == rl::PLUGIN_LOADED && ImGui::Selectable(p->id().c_str(), target == p->id()))
                target = p->id();
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        cleared = rl::log_last_seq();
    ImGui::SameLine();
    if (ImGui::Button("Reload all"))
        rl::request_reload_all();
    if (ImGui::CollapsingHeader("Plugins"))
        plugins_table();
    if (ImGui::CollapsingHeader("Budgets"))
        budgets();
    float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##lua log", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    for (const rl::LogLine& line : rl::log_lines()) {
        if (line.seq <= cleared)
            continue;
        ImVec4 colour = line.level == rl::LOG_ERROR ? ImVec4(1, 0.45f, 0.4f, 1)
            : line.level == rl::LOG_WARN            ? ImVec4(1, 0.8f, 0.3f, 1)
                                                    : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextWrapped("%s%s%s", line.plugin.c_str(), line.plugin.empty() ? "" : ": ", line.text.c_str());
        ImGui::PopStyleColor();
    }
    if (scroll_down || ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    scroll_down = false;
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##lua input", input, sizeof input,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, history)) {
        if (input[0])
            rl::console_submit(target, input, nullptr, nullptr);
        input[0] = 0;
        history_pos = -1;
        scroll_down = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}
```

- [ ] **Step 2: Remove the stubs** — delete the two `pc_lua_console_*` stub lines Task 20 added at the end of `pc/src/lua/lua_rpc.cpp`.

- [ ] **Step 3: Keep `lua_console.cpp` and `lua_api_settings.cpp` out of the probe** — check `pc/CMakeLists.txt`'s `ROOD_LUA_RUNTIME` lists neither (it lists files one by one; nothing to change if Tasks 3–18 were followed).

- [ ] **Step 4: Build; the console path still works over RPC**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tail -1
flock "$ROOD_HEAVY_LOCK" bash -c '
  export ROOD_PLUGINS=$PWD/pc/build/m0-plugins ROOD_PLUGIN_DATA=$PWD/pc/build/m0-plugin-data
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock launch >/dev/null &&
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock call lua "{\"action\":\"console\",\"input\":\"return 6 * 7\"}"
  python3 pc/tools/rood_rpc.py --socket /tmp/rood-m0.sock call quit >/dev/null 2>&1; true'
```

Expected: `Built .../rood_battle_probe: ...`; then `{"ok": true, "results": [42], "output": ["> return 6 * 7", "= 42"]}`.

- [ ] **Step 5: Look at it once (needs a desktop; skip on a headless machine)** — `pc/run-game.sh`, F1 for the editor, Window > Lua console: type `rood.version` and Enter (the log shows `> rood.version` and `= "0.1.0"`), Up recalls it, Plugins and Budgets open, Clear empties the log.

- [ ] **Step 6: Commit**

```bash
git add pc/src/lua/lua_console.cpp pc/src/lua/lua_rpc.cpp
git commit -m "$(cat <<'EOF'
Dev overlay: Lua console with history, plugin list and budgets

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 25: Sandbox and isolation fixtures, the shipped example plugin

**Files:**
- Create: `pc/tests/lua/plugins/test.sandbox/{plugin.lua,main.lua,lib/util.lua,tests/sandbox_test.lua}`, `pc/tests/lua/plugins/test.isolation_a/{plugin.lua,main.lua,tests/isolation_test.lua}`, `pc/tests/lua/plugins/test.isolation_b/{plugin.lua,main.lua,tests/isolation_test.lua}`, `pc/plugins/rood.example/{plugin.lua,main.lua,tests/example_test.lua}`
- Delete: `pc/plugins/.gitkeep`

- [ ] **Step 1: The sandbox fixture** — `pc/tests/lua/plugins/test.sandbox/plugin.lua`:

```lua
return { id = "test.sandbox", name = "Sandbox tests", version = "1.0.0", api_version = 1 }
```

`main.lua`:

```lua
-- Tests only (tests/sandbox_test.lua); lib/util.lua is what require may load.
```

`lib/util.lua`:

```lua
return { twice = function(x) return x * 2 end }
```

`tests/sandbox_test.lua`:

```lua
local t = rood.test

t.case("no io, os, debug, package or file loaders", function()
  for _, name in ipairs({ "io", "os", "debug", "package", "dofile", "loadfile", "warn" }) do
    t.eq(rawget(_G, name), nil, name)
  end
  t.eq(string.dump, nil)
end)

t.case("load takes text only", function()
  t.raises(function() load(function() return nil end) end, "text chunks")
  local f, err = load("\27Lua", "=bin", "b")
  t.eq(f, nil)
  t.ok(tostring(err):find("text chunks", 1, true), err)
  f, err = load("\27LuaT\0", "=bin")
  t.eq(f, nil)
  t.ok(tostring(err):find("binary chunk", 1, true), err)
  t.eq(load("return 1 + 1")(), 2)
end)

t.case("require loads only the plugin's own files", function()
  t.eq(require("lib.util").twice(2), 4)
  t.eq(require("lib.util"), require("lib.util"))
  t.raises(function() require("../test.core/main") end, "bad module name")
  t.raises(function() require("os") end, "not found")
end)

t.case("shared metatables and rood are locked", function()
  t.eq(getmetatable(""), "locked")
  t.eq(getmetatable(rood), "locked")
  t.raises(function() rood.log = nil end, "read%-only")
  t.raises(function() rood.registry.register = nil end, "read%-only")
  t.raises(function() setmetatable(rood, {}) end, "protected")
end)

t.case("collectgarbage only measures and collects", function()
  t.ok(collectgarbage("count") > 0)
  t.raises(function() collectgarbage("stop") end, "not allowed")
end)

t.case("ungranted capabilities are absent", function()
  t.eq(rood.fs, nil)
  t.eq(rood.net, nil)
  t.eq(rood.exec, nil)
  t.eq(rood.memory, nil)
  t.eq(rood.native, nil)
  t.eq(rood.plugin.elevated, false)
end)
```

- [ ] **Step 2: The isolation fixtures** — `pc/tests/lua/plugins/test.isolation_a/plugin.lua`:

```lua
return { id = "test.isolation_a", name = "Isolation tests A", version = "1.0.0", api_version = 1 }
```

`test.isolation_a/main.lua`:

```lua
secret = "only in a"
string.rep = nil -- changes this plugin's own string library only
rood.registry.register("test.shared", "test.isolation_a/greeter", {
  greet = function(name) return "hello " .. name .. " from " .. rood.plugin.id end,
  list = { 1, 2, 3 },
})
```

`test.isolation_a/tests/isolation_test.lua`:

```lua
local t = rood.test

t.case("another plugin's globals are invisible", function()
  t.eq(b_secret, nil)
  t.eq(secret, "only in a")
end)
```

`pc/tests/lua/plugins/test.isolation_b/plugin.lua`:

```lua
return { id = "test.isolation_b", name = "Isolation tests B", version = "1.0.0", api_version = 1 }
```

`test.isolation_b/main.lua`:

```lua
b_secret = "only in b"
```

`test.isolation_b/tests/isolation_test.lua`:

```lua
local t = rood.test

t.case("another plugin's globals are invisible", function()
  t.eq(secret, nil)
  t.eq(rawget(_G, "secret"), nil)
  t.eq(b_secret, "only in b")
end)

t.case("another plugin's changes to its libraries stay there", function()
  t.ok(string.rep ~= nil)
  t.eq(("x"):rep(3), "xxx")
end)

t.case("registry data is a copy; its functions run in their own plugin", function()
  local e = rood.registry.get("test.shared", "test.isolation_a/greeter")
  t.ok(e, "a's entry")
  t.eq(e.owner, "test.isolation_a")
  t.eq(e.data.greet("b"), "hello b from test.isolation_a")
  e.data.list[1] = 99
  t.eq(rood.registry.get("test.shared", "test.isolation_a/greeter").data.list[1], 1)
end)

t.case("a chunk's _ENV stays its own", function()
  local env = {}
  load("leaked = 1", "=chunk", "t", env)()
  t.eq(env.leaked, 1)
  t.eq(leaked, nil)
end)
```

- [ ] **Step 3: The shipped example plugin** — `git rm pc/plugins/.gitkeep`, then `pc/plugins/rood.example/plugin.lua`:

```lua
return {
  id = "rood.example",
  name = "Example plugin",
  version = "1.0.0",
  api_version = 1,
  description = "A plugin's layout: a setting, a timer, a registry entry and a test. Off by default.",
  author = "rood-boi",
  default_enabled = false,
}
```

`pc/plugins/rood.example/main.lua`:

```lua
-- An example plugin (pc/docs/Extensions.md). Off by default: turn it on in
-- the Lua console's Plugins list (F1, Window > Lua console) or with
-- RPC lua {action "enable", plugin "rood.example"}.
local greet = rood.settings.define("greet", {
  type = "boolean",
  label = "Greet in the log",
  help = "Writes a line to the log every minute of play.",
  default = true,
})

rood.timer.every(60, function()
  if rood.settings.get(greet) then
    rood.log(string.format("still here after %d s of play", math.floor(rood.clock.game())))
  end
end)

rood.registry.register("rood.example.note", "rood.example/hello", { text = "Hello from rood.example" })
```

`pc/plugins/rood.example/tests/example_test.lua`:

```lua
local t = rood.test

t.case("registers its note and its setting", function()
  t.eq(rood.registry.get("rood.example.note", "rood.example/hello").data.text, "Hello from rood.example")
  t.eq(type(rood.settings.get("plugin:rood.example/greet")), "boolean")
end)
```

- [ ] **Step 4: Run every Lua test in one game**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/lua_smoke.py --tests-only 2>&1 | tail -4
```

Expected: `PASS: 25 Lua tests in one game` (core 7, settings 6, sandbox 6, isolation 1 + 4, example 1). These pass on the first run: they pin behaviour Tasks 6–17 built (the probe proved it without the game; this proves it in the game). If one fails, fix the runtime, not the test.

- [ ] **Step 5: Commit**

```bash
git add pc/tests/lua/plugins pc/plugins
git commit -m "$(cat <<'EOF'
Lua fixtures: sandbox and isolation tests; the example plugin (off)

Escapes stay blocked (io, os, debug, package, bytecode, require outside
the plugin, locked metatables); two plugins cannot see each other's
globals or library changes; registry data arrives as a copy and its
functions run in their own plugin. pc/plugins/rood.example shows a
plugin's layout and ships off.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 26: The hard cases in `lua_smoke.py`

**Files:**
- Create: `pc/tests/lua/plugins/test.caps/{plugin.lua,main.lua}`, `pc/tests/lua/plugins/test.reload/{plugin.lua,main.lua}`
- Modify: `pc/tests/lua_smoke.py` (replace)

- [ ] **Step 1: The fixtures the hard cases drive** — `pc/tests/lua/plugins/test.caps/plugin.lua`:

```lua
return {
  id = "test.caps",
  name = "Capability tests",
  version = "1.0.0",
  api_version = 1,
  capabilities = { "fs.read", "fs.write", "net:example.com", "exec" },
}
```

`test.caps/main.lua`:

```lua
-- Driven by pc/tests/lua_smoke.py: grants arrive over RPC, checks are evals.
```

`pc/tests/lua/plugins/test.reload/plugin.lua`:

```lua
return { id = "test.reload", name = "Hot reload fixture", version = "1.0.0", api_version = 1 }
```

`test.reload/main.lua` (lua_smoke edits it while the game runs):

```lua
local VERSION = 1
rood.registry.register("test.reload", "test.reload/entry", { version = VERSION })
rood.on("tick", function() end)
rood.timer.every(1, function() end)
```

- [ ] **Step 2: Replace `pc/tests/lua_smoke.py`**

```python
#!/usr/bin/env python3
"""The extension system in one running game (pc/src/lua, pc/docs/Extensions.md).

1. Every fixture plugin's Lua tests (pc/tests/lua/plugins, rood.test) and the
   shipped plugins', in this one game.
2. What Lua cannot test from inside: a runaway eval (also a pcall loop and a
   pattern) is stopped within its budget and the game runs on; memory
   exhaustion is refused; a runaway tick hook and a failing frame hook are
   caught, logged with a traceback and their plugins disabled; capabilities
   exist only when granted (and go with a revoke or a code change), fs stays
   in its folders, the others refuse, elevated calls are audited; hot reload
   swaps a plugin's registrations; settings changed from Lua reach
   settings.cfg; a disabled plugin's settings kind is gone.

    python3 pc/tests/lua_smoke.py
    python3 pc/tests/lua_smoke.py --tests-only --plugin test.settings   one plugin's Lua tests (the fast loop)
"""
import argparse
import pathlib
import shutil
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "pc/tests/lua/plugins"

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--plugin", action="append", default=[], help="only this plugin's Lua tests (repeatable)")
parser.add_argument("--tests-only", action="store_true", help="only the Lua tests (part 1)")
args = parser.parse_args()

plugins = OUT / "lua-plugins"
data = OUT / "lua-plugin-data"
settings = OUT / "lua-settings.cfg"
shutil.rmtree(plugins, ignore_errors=True)
shutil.rmtree(data, ignore_errors=True)
settings.unlink(missing_ok=True)
shutil.copytree(FIXTURES, plugins)


def write_plugin(name, main):
    folder = plugins / name
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "plugin.lua").write_text(f'return {{ id = "{name}", version = "1.0.0", api_version = 1 }}\n')
    (folder / "main.lua").write_text(main)


def lua_tests(game):
    listed = game.lua_plugins()
    for folder in sorted(p.name for p in FIXTURES.iterdir() if p.is_dir()):
        assert listed[folder]["state"] == "loaded", listed[folder]
    if "rood.example" in listed:
        game.call("lua", action="enable", plugin="rood.example", enabled=True)
    results = []
    for plugin in args.plugin or [None]:
        results += game.lua_test(plugin=plugin)["results"]
    for r in results:
        print(f"{r['status'].upper():<5} {r['plugin']} {r['file']}: {r['name']} ({r['ms']} ms)")
    failed = [r for r in results if r["status"] == "fail"]
    assert not failed, "Lua tests failed:\n" + "\n\n".join(f"{r['plugin']} {r['name']}:\n{r['message']}" for r in failed)
    return results


def runaway(game):
    vsync = game.status()["vsync"]
    for code, why in [("while true do end", "budget exceeded"),
                      ("while true do pcall(function() while true do end end) end", "budget exceeded"),
                      ("return string.find(string.rep('a', 4000), '.-.-.-.-.-.-.-b')", "string pattern")]:
        start = time.monotonic()
        result = game.lua(code)
        assert not result["ok"] and why in result["error"], (code, result)
        assert time.monotonic() - start < 2.0, (code, "the eval budget is 250 ms")
    assert game.run(5)["vsync"] == vsync + 5
    game.call("lua", action="budget", memory_mb=8, load_budget_ms=5000, load_budget_instructions=10**10)
    result = game.lua("local t = {} for i = 1, 1e8 do t[i] = ('x'):rep(100) .. i end")
    assert not result["ok"] and "not enough memory" in result["error"], result
    kb = game.lua("collectgarbage() return collectgarbage('count')")["results"][0]
    assert kb < 8 * 1024, kb
    game.call("lua", action="budget", memory_mb=32, load_budget_ms=250, load_budget_instructions=50000000)
    assert game.run(5)["vsync"] == vsync + 10


def failing_hooks(game):
    write_plugin("hard.loop", 'rood.on("tick", function() while true do end end)\n')
    write_plugin("hard.frame_error", 'rood.on("frame", function() error("boom from a frame hook") end)\n')
    game.call("lua", action="rescan")
    listed = game.lua_plugins()
    assert listed["hard.loop"]["state"] == "loaded" and listed["hard.frame_error"]["state"] == "loaded", listed
    vsync = game.status()["vsync"]
    start = time.monotonic()
    assert game.run(30)["vsync"] == vsync + 30
    assert time.monotonic() - start < 15, "five call budgets, then plain vsyncs"
    listed = game.lua_plugins()
    loop, frame = listed["hard.loop"], listed["hard.frame_error"]
    assert loop["state"] == "disabled" and loop["kills"] >= 5, loop
    assert frame["state"] == "disabled" and frame["faults"] >= 5, frame
    lines = game.call("lua", action="log", since=0)["lines"]
    assert any("boom from a frame hook" in line["text"] and "main.lua:1" in line["text"] for line in lines), lines[-5:]


def capabilities(game):
    caps = "test.caps"
    assert game.lua("return rood.fs == nil and rood.net == nil and rood.exec == nil", plugin=caps)["results"] == [True]
    assert game.lua_plugins()[caps]["declared"] == ["fs.read", "fs.write", "net:example.com", "exec"]
    for capability in ("fs.read", "fs.write", "net:example.com"):
        game.call("lua", action="grant", plugin=caps, capability=capability, elevated=True)
    p = game.lua_plugins()[caps]
    assert p["granted"] == ["fs.read", "fs.write", "net:example.com"] and p["elevated"], p
    result = game.lua('rood.fs.write("out/note.txt", "hi") return rood.fs.read("out/note.txt")', plugin=caps)
    assert result["ok"] and result["results"] == ["hi"], result
    assert (plugins / caps / "out/note.txt").read_text() == "hi"
    result = game.lua('return pcall(rood.fs.write, "../escape.txt", "x")', plugin=caps)
    assert result["results"][0] is False and "bad path" in result["results"][1], result
    result = game.lua('return pcall(rood.net.request, "https://example.com")', plugin=caps)
    assert result["results"][0] is False and "not available in this version" in result["results"][1], result
    assert game.lua("return rood.exec == nil", plugin=caps)["results"] == [True]
    audit = game.call("lua", action="audit")["lines"]
    assert any(" test.caps fs.write " in line for line in audit), audit
    assert "test.caps fs.write" in (data / "audit.log").read_text()
    game.call("lua", action="grant", plugin=caps, capability="fs.write", revoke=True)
    assert game.lua("return rood.fs.write == nil and rood.fs.read ~= nil", plugin=caps)["results"] == [True]
    main = plugins / caps / "main.lua"
    main.write_text(main.read_text() + "-- changed: grants are asked again\n")
    game.run(31)
    p = game.lua_plugins()[caps]
    assert p["granted"] == [] and "fs.read" in p["pending"], p
    assert game.lua("return rood.fs == nil", plugin=caps)["results"] == [True]
    assert f"plugin_grant {caps} " in settings.read_text()


def hot_reload(game):
    def entries():
        return [(e["id"], e["data"]["version"]) for e in game.call("lua", action="registry", kind="test.reload")["entries"]]
    assert entries() == [("test.reload/entry", 1)]
    before = game.lua_plugins()["test.reload"]
    main = plugins / "test.reload/main.lua"
    main.write_text(main.read_text().replace("VERSION = 1", "VERSION = 2")
                    .replace('"test.reload/entry"', '"test.reload/entry2"'))
    game.run(31)
    after = game.lua_plugins()["test.reload"]
    assert after["generation"] > before["generation"] and after["state"] == "loaded", (before, after)
    assert entries() == [("test.reload/entry2", 2)], entries()
    assert (after["handlers"], after["timers"]) == (before["handlers"], before["timers"]) == (1, 1), (before, after)


def settings_file(game):
    text = settings.read_text()
    assert "skip_intro 1" in text, text
    assert (data / "test.settings/settings.json").exists()
    game.call("lua", action="enable", plugin="test.settings", enabled=False)
    assert game.lua('return #rood.settings.list({ page = "plugin:test.settings" })')["results"] == [0]
    lint = game.call("editor_schema", action="lint")
    assert not lint["problems"], lint
    assert "plugin_enabled test.settings 0" in settings.read_text()


game = Game.launch(fast=True, extra_env={"ROOD_PLUGINS": str(plugins), "ROOD_PLUGIN_DATA": str(data),
                                         "ROOD_SETTINGS": str(settings)})
try:
    game.pause()
    results = lua_tests(game)
    if not args.tests_only:
        runaway(game)
        failing_hooks(game)
        capabilities(game)
        hot_reload(game)
        game.run(40)  # settings.cfg: the Lua tests' changes are saved within 30 polls
        settings_file(game)
        assert game.call("lua", action="console", input="return 6 * 7")["results"] == [42]
        vsync = game.status()["vsync"]
        assert game.run(5)["vsync"] == vsync + 5
    print(f"PASS: {len(results)} Lua tests in one game"
          + ("" if args.tests_only else "; runaway scripts, memory, failing hooks, capabilities, hot reload and "
                                        "settings checked in the same game"))
finally:
    game.close()
```

- [ ] **Step 3: Run it**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 lua_smoke
tail -2 pc/build/tests/lua_smoke/output.log
```

Expected: `1/1 passed`; the log ends with `PASS: 25 Lua tests in one game; runaway scripts, memory, failing hooks, capabilities, hot reload and settings checked in the same game`.

- [ ] **Step 4: The same on the 64-bit build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64 2>&1 | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 lua_smoke
```

Expected: `Built .../boot64/rood_battle_probe: ...`; `1/1 passed`.

- [ ] **Step 5: Commit**

```bash
git add pc/tests/lua/plugins/test.caps pc/tests/lua/plugins/test.reload pc/tests/lua_smoke.py
git commit -m "$(cat <<'EOF'
lua_smoke: runaway, memory, failing hooks, capabilities, hot reload

In the same game as the Lua tests: a runaway eval, pcall loop or
pattern stops within its budget and the game runs on; memory exhaustion
is refused; a runaway tick hook and a failing frame hook are disabled
with a traceback in the log; capabilities appear only when granted and
go with a revoke or a code change; hot reload swaps registrations;
settings changed from Lua reach settings.cfg.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 27: Documentation

**Files:**
- Create: `pc/docs/Extensions.md`
- Modify: `pc/README.md`

- [ ] **Step 1: Write `pc/docs/Extensions.md`**

````markdown
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
only. `require("a.b")` loads `a/b.lua` from the plugin's own folder. `print`
writes to the log. `collectgarbage` only counts, collects and steps.
`getmetatable("")` and `getmetatable(rood)` give `"locked"`; `rood` and its
tables are read-only. Values that go to another plugin (events, registry
entries) are copies; functions in them run in their own plugin.

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
call returns, so `pcall` cannot keep a runaway alive; string patterns
(which run in C) check the clock too. Errors are caught with a traceback
(the log, the console, stderr `[lua] ...`). Disabled plugins come back when
their files change or with a reload. Known limits: one long C call without
Lua instructions (sorting a huge table, a large `table.concat`) can overrun
a budget by its own length; memory is capped, which bounds most of them.

## Capabilities

Declared in the manifest, granted by the player per plugin and per
capability, only while "Allow plugins to request elevated permissions"
(`plugins_elevated`) is on, and only for the code they were granted for
(`plugin_grant <id> <sha256 of its code> <capability>`): a changed plugin
is asked again. Revoking takes the functions away at once (the plugin
reloads). Every elevated call is logged (`<plugin_data>/audit.log`).

| Capability | M0 | Functions |
|---|---|---|
| `fs.read`, `fs.read:<folder>` | yes | `rood.fs.read`, `exists`, `list`: the plugin's folder and the granted folders |
| `fs.write`, `fs.write:<folder>` | yes | `rood.fs.write`, `remove` |
| `net:<host>`, `exec`, `memory.read`, `memory.write`, `flags.write`, `native` | declared only | exist when granted and refuse: "not available in this version" |

The grant dialog comes with the menus; in M0 grants are made with RPC
`lua {action "grant"}` (dev mode) or by editing `settings.cfg`.

## API (api_version 1)

| | |
|---|---|
| `rood.log/warn/error(...)` | to the log and the console |
| `rood.version`, `rood.api_version` | |
| `rood.plugin` | `id, name, version, dir, data_dir, shipped, capabilities, granted, missing, elevated` |
| `rood.on(event, fn) -> handle`, `rood.off(handle)`, `rood.emit(event, payload) -> count` | plugins emit namespaced events (`my.plugin/changed`); core events: `tick {vsync, game_time}`, `frame {dt, real_time}`, `plugin.loaded/unloaded {id}`, `registry.changed {kind, id, action}`, `unload` (to the plugin itself) |
| `rood.timer.after/every(seconds, fn[, {clock = "real"}]) -> id`, `rood.timer.cancel(id)` | game time by default (stops while paused) |
| `rood.clock.game()`, `rood.clock.real()` | seconds |
| `rood.registry.register(kind, id, data[, {after, before, group}])`, `get`, `list(kind[, {group}])`, `remove`, `kinds()` | ids in the plugin's namespace (`<plugin id>/<name>`); `after`/`before`: ids or `group:<name>` |
| `rood.storage.get/set/remove/keys/flush`, `read(path)`, `read_data/write_data/list_data` | plain data, 1 MB; `read` reads the plugin's folder |
| `rood.settings.get/set/reset/run/list([{page}])/pages()/define(name, spec)/on_change(id, fn)/off_change` | System rows (`"skip_intro"`), schema kinds (`"<kind>/<field>"`), the plugin's own (`"plugin:<id>/<name>"`), each validated by its registry |
| `rood.test.case(name, fn)`, `eq`, `ok`, `near`, `raises`, `skip`, `wait(ticks)` | in `tests/*.lua` |

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
  capabilities, hot reload, the test harness, the RPC.
- Lua tests: `tests/*.lua` in a plugin, run inside a running game with no
  relaunch: `rood_rpc.py lua-test`, or `python3 pc/tests/lua_smoke.py
  --tests-only --plugin my.plugin` (one launch).
- `pc/tests/run.py lua_smoke`: one game, every Lua test, then runaway and
  failing scripts, memory, capabilities, hot reload and settings.
````

- [ ] **Step 2: Point to it from `pc/README.md`** — right before `## Developer control interface (JSON-RPC)` add:

```markdown
## Plugins and Lua (Milestone 0)

Plugins are folders with a manifest and Lua 5.4 code: shipped ones in
`pc/plugins` (the example `rood.example` is off), yours in
`pc/user/plugins`. Each runs in its own sandbox with time and memory
budgets; a plugin that keeps failing is disabled, never the game. Elevated
capabilities (files beyond its folder, later network or memory access)
need the "Allow plugins to request elevated permissions" switch and a
grant per plugin. F1 > Window > Lua console evaluates Lua and lists the
plugins; RPC `lua` drives it in tests. See `docs/Extensions.md`.
```

and in the tests table, after the `rpc_smoke`, `gi_smoke`, `movie_smoke` row add:

```markdown
| `lua_smoke` | plugins in one game: every Lua test (`rood.test`), runaway and failing scripts stopped within their budgets, memory cap, sandbox, capabilities, hot reload, settings from Lua (`docs/Extensions.md`) |
```

- [ ] **Step 3: Commit**

```bash
git add pc/docs/Extensions.md pc/README.md
git commit -m "$(cat <<'EOF'
Docs: plugins and Lua (Milestone 0)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 28: Verify every target and suite, then hand off

**Files:** none changed (fixes go back to the task that owns the file).

- [ ] **Step 1: CTest**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build -j 2 2>&1 | grep -E "error|warning" | head
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -j 2 --output-on-failure | tail -3
```

Expected: nothing from the grep; `100% tests passed`.

- [ ] **Step 2: All four builds**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py 2>&1 | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64 2>&1 | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows 2>&1 | tail -1
for t in boot boot64; do grep -c "_Unwind\|void lua\|void _Z" pc/build/$t/traps.c; done
python3 -c "import json; f = json.load(open('pc/build/boot64/report.json'))['files']; print(sum(not x['compiled'] for x in f), sum(x['source'].startswith(('pc/third_party/lua', 'pc/src/lua')) for x in f))"
```

Expected: four `Built ...` lines (`rood_boot`, `rood_battle_probe`, `boot64/rood_battle_probe`, `win64/rood.exe`); `0` and `0`; `0 43` (27 Lua core units, 16 files of `pc/src/lua`).

- [ ] **Step 3: The suites this work touches, 32-bit and 64-bit**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest lua_smoke rpc_smoke build_noop system_settings ui_system editor_schema
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 lua_smoke rpc_smoke
```

Expected: every suite passes, or fails exactly as in the Task 1 baseline.

- [ ] **Step 4: Project rules**

```bash
git diff main --stat -- src/ | tail -1
git diff main --name-only | grep -E "^(data|disks|build)/|\.(mcd|ppm|bin|iso)$" ; echo "game data: ${?}"
pc/tools/publish.sh --dry-run "leak check"   # the publish leak scan (its deny list)
echo "commits $(git rev-list --count main..HEAD), attributed $(git log main..HEAD --format=%B | grep -c 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>')"
```

Expected: no `src/` changes (empty first line); `game data: 1` (grep found nothing); no output from the name scan; the two counts are equal.

- [ ] **Step 5: Finish the branch**

Use superpowers:finishing-a-development-branch: merge `main` into `claude/extension-m0` first (the main session may have touched `dev_overlay.cpp`, `editor_schema.*`, `dev_rpc.c`, `run.py`), rerun Step 3, then ask for the merge. Do not push to GitHub.

---

## Self-review

### Spec coverage

| Requirement (brief) | Where |
|---|---|
| Lua 5.4 vendored as source in `pc/third_party/lua`, MIT, no LuaRocks | Task 2 (5.4.8, licence, UPSTREAM, ROOD-NOTES), THIRD_PARTY.md |
| Built by our build on all four targets + CMake probes | Task 3 (CMake `rood_lua`), Task 19 (32-bit, `--battle`, `--arch 64`, `--target windows`), Task 28 Step 2 |
| Sandboxed env per plugin; no io/os/debug/package/loadfile/dofile/require from disk; `load` text-only in the plugin's env | Decision "one state per plugin"; Task 7; fixtures Task 25 |
| Capability mechanism: manifest field, grant store in user settings, gate, audit log, dev grant over RPC | Task 13 (declaration check), Task 15 (store in `settings.cfg`, gate, audit), Task 18 (`grant` action), Task 20 (settings lines) |
| fs.read/fs.write scoped to the plugin folder and granted paths | Task 15 (`resolve`, `check_inside`), Task 26 |
| net/exec/memory/native declared-but-unimplemented, refusing clearly | Task 15 (`stub_call`), Task 26 |
| Master switch off by default, hash-tied grants, re-ask on update, revocable, elevated marked, elevated calls logged | Task 15, console mark in Task 24, Task 26 (revoke, code change) |
| Instruction/time budget per call (count hook + wall clock) | Task 5 (+ pattern matcher), Task 13 (tunables) |
| Memory cap per plugin (custom `lua_Alloc` with accounting) | Task 4, Task 26 |
| Errors caught with tracebacks to log and console, never crashing; repeated faults disable | Task 6 (`run`, `fault`), Task 13 (`settle`), Task 19 (`_Unwind_Resume` real), Task 26 |
| Plugins: folder + manifest (Lua or JSON; id, name, version, api_version, entry, depends, capabilities) | Task 13 |
| Search paths `pc/plugins` + user folder | Task 13 (`shipped_root`, `user_root`), Task 25 (`rood.example`) |
| Enable/disable and load order in settings | Task 13 (`plugin_enabled`, `plugin_order`), Task 20 |
| Hot reload by mtime, unregister everything then re-run | Task 16, Task 26 |
| `rood.log/warn/error`, `version`, `api_version`, `plugin` | Task 8 |
| `rood.on/off/emit`, frame/tick hooks with budgets | Task 9, Task 20 (where they are called) |
| `rood.timer.after/every/cancel`, game and real time | Task 10 |
| `rood.storage` (key/value + data files) | Task 14 |
| `rood.registry.register/get/list/remove`, namespaced ids, after/before/group, open kinds, tracked for undo | Task 11, Task 13 (`unload` drops it) |
| `rood.settings.get/set/reset/on_change/define` through the settings registry with validation, generated from metadata (project settings, editor schema `add_kind`/`draw_kind`) | Task 22 (`pc_ui_settings_find`, `remove_kind`), Task 23 |
| Plain Lua C API with a thin helper layer (no sol2) | `push_cfunction`/`lock_table`/`Value` (Task 6) |
| Dev RPC `lua` {eval, plugins, reload, grant, budget stats} in `dev_rpc.c` | Task 18, Task 20 Step 4 |
| Console in the dev overlay with history and output | Task 24 |
| `rood_rpc.py` helpers | Task 21 |
| `rood.test` harness, tests in Lua in a running game over RPC, discovery per plugin, JSON results | Task 17, Task 21 |
| A run.py suite that launches one game and runs all Lua tests | Task 21, Task 26 (`lua_smoke`) |
| Fast C-level probes (CTest) for the sandbox, allocator cap, budgets | Tasks 3–18 (`lua_probe`) |
| Hard test: infinite loop stopped within budget, game keeps running | Task 5 (probe), Task 26 (`runaway`, `failing_hooks`) |
| Hard test: memory exhaustion refused, game keeps running | Task 4, Task 26 |
| Hard test: error in a frame hook doesn't crash | Task 26 (`hard.frame_error`) |
| Hard test: sandbox escapes blocked (io, os, debug, package, string.dump/bytecode, metatable tricks, `_ENV` leaks) | Task 7, Task 25 (sandbox, isolation) |
| Hard test: ungranted capabilities absent, granting adds them | Task 15, Task 25, Task 26 |
| Hard test: hot reload unregisters and re-registers cleanly | Task 16, Task 26 |
| Hard test: settings validation enforced from Lua | Task 23 |
| Hard test: two plugins can't see each other's globals | Task 6 (probe), Task 25 |
| Where Lua runs in the frame, which thread; none on render/audio threads | "Where Lua runs" above, Task 20, `pc/docs/Extensions.md` |
| Worktree; `flock "$ROOD_HEAVY_LOCK"`; tests at `-j 2`; no CPU load; no `src/`; no game data; nothing the publish leak scan denies; attribution line; MinGW portability | Ground rules; Task 1; every command; Task 28 Step 4; Task 19 Step 3 |

### Placeholder scan

One deliberate fill-in: `UPSTREAM.txt`'s SHA-256 is the value printed by Task 2 Step 1 after it is checked against lua.org (it cannot be known before the download is verified). No "TBD", "similar to Task N" or unwritten code elsewhere; every step names its files, code and command with the expected output.

### Type and name consistency

- `rl::Plugin` fields used across tasks: `manifest`, `dir`, `data_dir`, `shipped`, `console`, `manifest_ok`, `waiting`, `hash`, `granted`, `L`, `generation`, `state`, `status`, `mem`, `budget`, `stats`, `depth`, `fault_times`, `disable_reason`, `reload_requested`, `pending_unrefs`, `files`, `id()` — all declared in Task 3.
- `BudgetKind` is `BUDGET_CALL` / `BUDGET_LOAD` / `BUDGET_TEST` everywhere; evals, test steps and manifest evaluation use `BUDGET_TEST` (never counted as faults).
- `rl::run(Plugin*, const char*, BudgetKind, const std::function<void(lua_State*)>&, std::string* = nullptr)`; `rl::eval(Plugin*, const std::string&, std::vector<Value>*, std::string*)`; `rl::fault(..., bool killed, bool counted)` — one signature each.
- `rl::Value` constructors are `str`, `number`, `integer`, `boolean`, `new_table`; `from_lua` is static; `push`, `call_function`, `to_json`, `from_json` are free functions.
- Settings lines: `plugins_elevated`, `plugin_enabled`, `plugin_order`, `plugin_grant`, and the `tunables()` keys; parsed only in `pc_lua_settings_parse`, written only in `pc_lua_settings_write`.
- Setting ids: System rows bare (`skip_intro`), schema fields `<kind>/<field>`, plugin settings `plugin:<plugin id>/<name>` (Task 23 test asserts the form).
- Registry ids `<plugin id>/<name>`; plugin events must contain `/`; core events never do.
- C API names in `lua_runtime.h` match their definitions: `pc_lua_init/shutdown/tick/frame/rpc/emit_json`, `pc_lua_settings_begin/parse/end/write` (Task 13), `pc_lua_console_menu_item/window` (Task 24; stubs in `lua_rpc.cpp` between Tasks 20 and 24).

## Milestone 1 (outline): "PC Settings" in the vanilla menu

**Goal:** the classic (vanilla) menu gains a "PC Settings" entry whose page lists every setting from the shared registry (the same rows the modern System tab shows, plus plugins' own), drawn in the classic style: the game's window frames, cursor and button sprites, its text printers (the `ui_text` font is the only upgrade allowed in classic mode). The entry and the page are data plus Lua in a shipped plugin (`rood.pc_settings`, on by default); the core only renders and dispatches.

**Shape:**
- `rood.vanilla` (shipped plugin) registers the vanilla menu's entries as data (`menu.entry` kind: `rood.vanilla/status`, `.../options`, ..., each "run the game's screen X"); `rood.pc_settings` registers `rood.pc_settings/entry` with `after = "rood.vanilla/options"` and a screen `rood.pc_settings/page` (`menu.screen` kind) whose rows are built from `rood.settings.list()` grouped by `rood.settings.pages()`.
- Minimal `src/` hooks under `#ifdef ROOD_PC` (commit-check must still match): where the MAINMENU builds its item list and dispatches a selection, call the host (`pc_classic_menu_items` / `pc_classic_menu_select`), which asks the registry for extra entries and opens the plugin's screen.
- A classic renderer in C++ (component type × flavour "classic": list, row kinds toggle/choice/slider/action/info, help line, greyed-with-reason) using the game's sprites and text printers; input through the game's pad word (Cross/Circle roles respected, `button_roles.c`).
- Lua decides content and reacts (`on_change`, actions through `rood.settings.run`); the core draws and applies through `rood.settings`, so validation and saving are the registry's own; both UIs show each setting once.
- Tests: Lua tests in `rood.pc_settings` (rows match `rood.settings.list`, a change round-trips, greyed rows refuse) plus a smoke that opens the vanilla menu over RPC, moves to the entry, toggles a row and checks `settings.cfg`, golden frames for the classic page at 4:3 and 16:9, with `ui_text` original and font.

## Milestone 1 dependencies on Milestone 0

M1 needs these M0 pieces; each is in this plan (the task in brackets), so M0 must not drop them:

- Registry ordering by `after`/`before`/`group:` and namespaced ids, open kinds, `registry.changed` events [11]; the C++ side's `registry_list` / `registry_get` for the renderer [11].
- `rl::call_function` and function values in registry entries, callable from C++ under the owner's budget [6].
- `pc_lua_emit_json`, the game's way to raise an event from a `src/` hook [18].
- `rood.settings.list` with full metadata — kind, page, choices with ids/labels/availability/reason, min/max/step, `enabled`/`reason`, `game_option`, `text`, `source` — plus `pages`, `get`/`set`/`run`, `on_change`; plugin-defined settings appear in the same list [23]; `pc_ui_settings_find` [22].
- Shipped plugins in `pc/plugins`, `default_enabled`, the reserved `rood.*` namespace, load order by dependency [13, 25]; `windows_dist.py` ships `pc/plugins` [19].
- The frame hook inside an open ImGui frame and the tick hook after the RPC (M1's renderer and its input handling run there) [20]; budgets small enough for per-frame UI callbacks, tunable [5, 13].
- `rood.test.wait` and `Game.lua_test` / `lua_smoke --plugin` for UI tests in one running game [17, 21]; hot reload while the menu is open (rebuild on `plugin.loaded`) [16].
- C++ exceptions working on Linux (`libgcc_s` scanned and linked) — any binding that raises through C++ frames depends on it [19].
