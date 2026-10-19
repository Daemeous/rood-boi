# Freedom from the PS1, and a fast inner loop

Status: plan, 2026-10-18. It supersedes the order of
`2026-10-16-ownership-m0-plan.md` (whose tasks 1-8 are done and are kept) and
folds in the dev-speed work and Lua M1. Bun's Zig-to-Rust port
([bun.sh/blog/bun-in-rust](https://bun.sh/blog/bun-in-rust.md)) is the model:
a mechanical, faithful bulk port per subsystem, checked by an oracle, then
idiomatic cleanup.

## Goal

1. **The game is not held back by the PS1.** Its logic is owned code (Zig)
   working on native data: no 2 MB RAM image, no 32-bit guest pointers, no
   overlays, no ordering tables, no frame-locked hacks. The decompiled C in
   `src/` stays as the byte-matching reference and the oracle, not the thing
   we patch.
2. **Iteration takes seconds.** Look, feel and QoL change in Lua with hot
   reload. Game logic changes in Zig hot-swap into the running game. Any
   moment of the game can be summoned in one step. Engine (C++) changes
   rebuild only their module.

## Who owns what (end state)

| Layer | Language | Role | How a change lands |
|---|---|---|---|
| `src/` | decompiled C | The original, byte-identical to the PS1 build: the reference and the oracle | Not edited (no new hooks) |
| `game/` | Zig | The game's rules and flow, ported faithfully and then freed from PS1 structures | Hot swap in development (seconds), with a differential test before merging |
| `pc/` | C/C++ | The engine: platform, renderer, audio, input, editor, RPC, the Lua host and primitives | Rebuilds only its own module |
| plugins | Lua | Look, feel, QoL, HUD, menus, options, dev tools, the scenario bench, mods | Hot reload (seconds); `rood.test` |
| content | Lua tables, settings, packs, assets | Values artists tune | Live in the editor |

Rule: C++ and Zig provide capabilities; Lua and data decide behaviour and
look. No new look value lives in C++ (enforced by a lint, phase F).

## Phases (sequential: one task at a time, merged before the next)

### A. Fast builds and tests (engine loop)

1. **Shared build cache** (branch `shared-build-cache`, after its security
   review): third-party builds, the Wine prefix and objects are shared across
   checkouts.
2. **Cut the fixed per-frame cost.** Tests already run without path tracing, and
   the measured cost per game frame (about 0.45 ms of GPU) barely changes with
   resolution, AA or lighting. It is fixed overhead. A CPU profile of the
   classic UI (2026-10-18) showed what it is made of:
   - the emissive system re-hashing textures (MD5) on every VRAM write;
   - the CPU software mirror of 2D drawing (`triangle`, `display_pixel`);
   - many small VRAM tile uploads, plus GPU waits.

   Profile a headless test game, then:
   - cache emissive fingerprints by region and generation;
   - skip the CPU mirror when nothing reads it back;
   - batch uploads;
   - remove per-frame waits.

   The measure is suite wall time before and after (the classic menus get
   snappier as a side effect).
3. **Compile and link are not the bottleneck** (measured 2026-10-18): a
   one-line code change in a C or C++ file rebuilds the 64-bit game in about
   5.7 s; build_boot's object cache already does what ccache would, and
   linking is quick. mold/ccache are dropped. The time goes to test suites,
   replaying back to the moment, and agent overhead.
4. **Snapshot-resume across rebuilds.** Save states are tied to the whole
   binary today, so any rebuild makes them stale. Key their validity on the
   game's memory layout and data instead (the guest image, the overlay table,
   the import addresses), so an engine-only change keeps them valid. Then:
   - `run-game.sh` saves state on exit and resumes it on launch (`--resume`);
   - RPC `restart` re-execs into the saved state.

   Any change (C++, Zig, `src/`) becomes: rebuild, then back at the same
   moment within seconds.

   **Done for engine changes (2026-10-18, 64-bit):** the link pins the
   game's code, read-only data and state after the engine
   (`pc/tools/snapshot_layout.py`); a snapshot saves the game's stack only
   below the vsync boundary (`pc_vsync_boundary`) and resumes by returning
   into the game from there; its keys are the game's code before linking and
   the state layout. `pc/tools/dev_loop.sh` (rebuild + RPC `restart`)
   measured 6.2 s from a renderer edit to the game running again at the same
   vsync (build 5.7 s, restart 0.5 s; Vulkan). A game-code (`src/`) change is still
   refused: resuming across it needs the game's own frames to stay valid,
   which hot swap (C) addresses. 32 bits: same-binary only (its native code
   must stay below 16 MB).
5. **The engine as a hot-reloadable library** (Live Coding, or Handmade
   Hero's game-code reload, but for the engine):
   - **Host:** a thin executable owns the game (`src/`, the PS1 memory image
     and its state, the data imports).
   - **Library:** all of `pc/` (renderer, UI, editor, Lua host, platform
     glue) is one shared library that the host reloads when it changes.
   - **Reload rules:** GPU resources are rebuilt on reload; engine state that
     must survive is kept in host-owned blocks, and the rest re-initialises.
     Function pointers into the library are re-resolved.

   A C++ UI or renderer edit shows in the running game within seconds. This
   replaces the earlier "split pc/ into modules" step: reloading one library
   is the bigger win.

   **Done (2026-10-19, Linux 64 bits):** `pc/run-game.sh --reload` and
   `pc/tools/dev_loop.sh --watch`; RPC `reload` swaps the library in 40-50 ms
   (GL) or about 300 ms (Vulkan); a renderer edit is on screen 2.5 s after
   the save (the A4 restart loop: 8.1 s on the same machine). Details,
   boundary and limits: `2026-10-19-engine-reload.md`.

### B. The oracle (before any bulk port)

The original is deterministic: the same inputs give the same state. That is a
better oracle than Bun's test suite. These are tasks 14-20 of the M0 plan,
pulled forward:
- state ranges, captures and page digests (`state_diff.c`);
- in-memory mark and rewind;
- input recording and replay over RPC;
- digests and noise masks;
- **shadow mode:** every routed call runs both the original and the owned
  version and compares;
- addresses to fields (`field_map.py`);
- the battle corpus: recorded play covering casts, items, arts, chains,
  defence, enemies, status effects.

**Headless CPU oracle** (with part 2, before the bulk port): game state does
not depend on rendering, and the oracle compares memory, not pixels. So:
- add a null-renderer mode for oracle, shadow and corpus runs (no GPU, no
  window, no presentation; the hardware VRAM target is not created), so they
  run on the CPU in parallel across all cores instead of queueing on the one
  GPU lock;
- tests that check pictures keep the GPU.

**More machines:** the user has other machines in the house (laptops). A
small runner (`pc/tools/farm.py`) shards oracle/corpus/test jobs to them:
- ssh, with a build artifact plus the job list;
- each machine reads its own copy of the user's disc;
- the results come back as digests and logs.

Game data stays on the user's own machines; nothing is uploaded elsewhere.

Done when: a recorded corpus replays bit-identically, and a deliberately
wrong owned function is caught with the field and frame named.

### C. The Zig dev loop (hot swap)

1. `game/` is split into many small Zig modules (per overlay or subsystem).
2. A development build of `game/` is a shared library built by
   `zig build --watch` in Debug (incremental compilation; the self-hosted
   x86-64 backend where it works on 0.17 for our target, else LLVM). Release
   stays ReleaseSafe and statically linked.
3. On rebuild, the host reloads the library and the owner switch (task 8)
   repoints routed functions. Battle or field state carries over, because
   owned code keeps its state in the game's memory or in explicitly migrated
   state.
4. RPC and console commands: `owner`, `reload`, and shadow mode on or off
   for one subsystem.

Done when: editing an owned function shows in the running game in a few
seconds, with no restart.

### D. Bulk port, battle first (faithful)

1. **Prep, reviewed adversarially before any porting:**
   - `PORTING.md`: decomp-C patterns mapped to Zig (`psx.Ptr`, globals
     through `psx.global`, bitfields at DWARF positions, the GTE, fixed
     point, overlays, `ROOD_PC` hooks, `extern` data, varargs, inline asm);
   - `STATE.tsv`: every global and static the subsystem touches, with its
     owner, meaning and lifetime.
2. **Trial on 3 functions:** one implementer, two adversarial reviewers, one
   fixer.
3. **The whole battle subsystem in one pass:** `translate-c` output as the
   seed, rewritten faithfully per `PORTING.md`. The work queues are, in
   order: compile errors per module, then shadow-mode mismatches per
   function, then corpus failures.
4. **Flip the subsystem** to owned once the corpus is green in shadow mode on
   both architectures. Delete its `ROOD_PC` hooks from `src/`. The charging
   skip becomes native here.

Rules (learned the hard way):
- No `git stash`/`reset`.
- At most 2-4 worktrees, on the shared cache.
- Reviewers reject stubs, and code that needs a paragraph to justify a
  workaround.
- The implementer never reviews its own work.
- No test is skipped to make a queue empty.

Then field, menus, then the rest, one subsystem at a time, the same way.

### E. Liberation (per owned subsystem)

Once a subsystem is owned and green:
1. **Native data:** move its state out of the PS1 RAM image into Zig
   structs. Guest pointers become real pointers or handles. `STATE.tsv`
   becomes the migration map.
2. **Drop PS1 mechanics it no longer needs:**
   - overlay loading;
   - ordering-table drawing (the native renderer draws from owned data);
   - 30 Hz-locked updates where interpolation or a variable tick is better.
3. **Its own tests:** the shadow oracle no longer applies once behaviour is
   meant to differ. Golden recordings become property and scenario tests.
4. **Lua events and actions:** the subsystem exposes events and actions to
   `rood.*` (binding layers 3/4), so gameplay QoL moves to Lua.

From here on, changes to that subsystem are ordinary feature work.

### F. Lua: the visible layer

1. **Queries, events and draw primitives** (binding layers 3 and 5):
   - text with font, size, stroke and tracking;
   - the game's sprites and window frames, rects;
   - HUD moments (status words, buff popups, prompts, damage numbers) and
     battle/field state as read-only tables.
2. **`rood.hud`:** a shipped plugin owning the HUD and prompt look, hot
   reloaded. The C++ look stays only as the fallback. The HUD typography
   branch's fixes become its first content.
3. **The scenario bench:** `rood.dev.show(...)` summons any HUD moment; a
   gallery of named save states; both from the console and a dev panel.
4. **M1 options** (`rood.menu`, `rood.options`, the classic renderer, the
   display back-ends) on the same layers: plan
   `2026-10-18-lua-m1-options.md`.
5. **Lint:** no new look literal in `pc/src` UI code.
6. **REPL bridge:** an nREPL-style socket on top of the RPC `lua` eval, used
   mostly by agents (the user rarely by hand). It evaluates forms in a
   plugin's or the console's sandbox, inspects the queries, and redefines HUD
   draw functions live. Fennel can come later. Done 2026-10-19: RPC `lua
   {action "repl"}` and `pc/tools/rood_repl.py` (Extensions.md "The REPL").

## Order

**A4 (snapshot resume) → A2 (per-frame cost) → A1 (shared cache) → F1-F3
and F6 → A5 (engine as a reloadable library, lower urgency: a 6 s rebuild plus
resume is already close) → B (oracle) → C (Zig hot swap) → D (battle port)
→ E (liberation) → F4 (M1 options) → next subsystems.**
- A and F come first because they speed up all later work.
- B and C come before D because the oracle and hot swap make the bulk port
  fast and safe.

## Measures of success

| Loop | Today | Target |
|---|---|---|
| HUD/typography tweak | hours (agent build and test) | under 1 min (Lua edit, hot reload, bench) |
| Reach a UI moment | play until it happens | 1 command |
| Game-logic edit visible | rebuild + restart + replay (minutes) | seconds (Zig hot swap) |
| Engine one-file edit to runnable | minutes, whole relink | seconds (reload in place) |
| Any rebuild back to the same moment | replay by hand | seconds (snapshot resume) |
| Verify a gameplay port | play-testing | shadow corpus, field and frame named |
| Fresh worktree | 3-8 GB | under 1 GB |
| Full 64-bit Vulkan suite | ~10 min | ~5 min |

## Budget

Bulk porting uses Kameosa (the user's ChatGPT plan) for mechanical
translation, and Claude for the porting guide, adversarial review and
verification. Work is sequential, at most a few workers on one subsystem, and
the oracle is the gate. Workflows (multi-agent fan-out) only with the user's
opt-in.
