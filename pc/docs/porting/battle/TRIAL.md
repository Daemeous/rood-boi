# The Zig trial port: three battle functions

Plan step D.2 of `pc/docs/plans/2026-10-18-freedom-and-fast-iteration.md`:
one implementer ports three functions faithfully per `../PORTING.md`; two
adversarial reviewers then try to break the work. This file is the
implementer's evidence: what was ported, how it was checked, what it cost,
what the guide lacked, and what the bulk port will cost.

Two adversarial reviews followed (faithfulness; oracle evidence), and a
fixer acted on them (branch `zig-trial-fix`): read **"After the adversarial
reviews"** below first. It corrects this file's evidence (the `alternate`
rows, the `=owned` smoke rows), lists the gaps that slipped, and revises the
cost estimate.

Branch `zig-trial` (from `main` e17d8626). 64-bit Linux unless said
otherwise. All game runs used the pinned test card (`pc/build/test-card1.mcd`,
copied per run), the classic oracle profile (`pc/tests/diff/corpus.py`) and
`ROOD_ORACLE=1`.

## What was ported

| Original (unit) | Zig function | Subsystem | Difficulty (FUNCTIONS.tsv) |
|---|---|---|---|
| `_updateEnemyKills` (`146C.c`) | `game/battle/score.zig` `updateEnemyKills` | `battle_146C` | S, 34 lines |
| `func_800B07DC` (`44F14.c`) | `game/battle/actor_transform.zig` `offsetToWorld` | `battle_44F14` | L, 22 lines |
| `vs_battle_displaySceneMessage` (`5BF94.c`) | `game/battle/scene_message.zig` `displaySceneMessage` | `battle_5BF94` | M, 35 lines |

Each Zig function starts with `/// orig: <symbol> (<unit>)`;
`game/ownership.toml` maps the symbol to it (`zig = "score.updateEnemyKills"`),
one routing subsystem per unit, each with its `views` manifest. Nothing in
`src/` changed. Both `ROOD_PC` hooks of `vs_battle_displaySceneMessage` are
kept in place and order: `pc_charge_skip_name(1)` first (through `c_5BF94`,
which now declares the block-scope extern), `pc_hud_name_window(message,
action)` last, behind `psx.weak` (the C's null test of a weak symbol).

## Prerequisites the trial had to build

`PORTING.md` §20 listed items 1-5 as "must exist before the trial"; on `main`
none of 1-4 existed (no `views`, no `c`, no `gte.zig`, `psx.at` broken). The
trial built what its three functions needed (commit `bba2e08c`), generically:

- `pc/tools/zig_views.py`: per-unit views from the unit object's DWARF (gdb),
  for the types and globals in the subsystem's `views` manifest; extern
  structs with explicit padding, `align(1)` where C is misaligned, `?*align(4)`
  for pointers to types aligned above 4, `psx.Ptr` for `psx_ptr` fields,
  bitfield tables, offset/size asserts that run on import, `g.<global>()`
  accessors, readable names from `MEANINGS.tsv`'s new `zig_name` column with
  `orig:` comments. Test: `pc/tests/zig_views_test.py` (CTest `zig_views`,
  32 and 64 bits, a C-filled instance read back field by field).
- `pc/tools/zig_c.py`: translate-c of the routed `.i`, bodies stripped
  (`ownership.strip_bodies` now keeps block-scope `extern`s), data
  definitions turned into `extern` declarations.
- `build_boot.py`: generates `views_<unit>`/`c_<unit>` for ELF and the COFF
  twin; `game_lib.py` (hot swap) uses the same generated modules.
- `game/common/gte.zig`: the five macros `func_800B07DC` uses.
- `psx.zig`: `weak`, `put`, `rawOf`, `lowRam`, `byteAt`, `Long`, `GuestPtr`,
  and `at` fixed (any pointer shape, any integer index converted as C does,
  wrapping arithmetic, alignment kept), with unit tests; globals through the
  GOT in the hot-swap library.
- `field_map.py`: `TYPE@ADDRESS` roots for blocks the code addresses by
  number (the scratchpad), with a test.

Not built (not needed by these three): the statics globalisation, the
post-routing inlining check, the i386 struct-return probe.

## How each function was checked

For every function:
1. **Corpus** (`pc/tests/diff/differ.py <subsystem> --scenario <recipe>`):
   original/original control, call shadow (`shadow`), owned every tick
   (`tick`) and alternating (`alternate`), all from one mark in one process,
   on Vulkan and on the null lane. Registered as run.py suites
   `trial_corpus_kill`, `trial_corpus_transform`, `trial_corpus_name_window`
   (null-lane safe; `verify.sh` runs them there).
2. **shadow_battle_smoke** (zone 48 room 10, 600 vsyncs) with the function's
   subsystem in `shadow` and in `owned` beside `battle_action` in shadow
   (`--owner SUBSYSTEM=MODE`, added for this).
3. **A deliberately wrong variant**, a temporary edit never committed,
   rebuilt and run through the same corpus.
4. **Hot swap** with `pc/tests/trial_hotswap.py` (zig_hotswap_smoke's
   method): one `rood_host` PID, the scenario marked once, a clean attempt,
   then the wrong edit in a private copy of `game/`, `game_lib.py` rebuild,
   `reload zig` while paused, the shadow mismatch witnessed, the source
   restored, rebuilt, reloaded and a clean attempt again.

How it was checked that the corpus calls the function: the shadow report's
per-function `coverage` (root calls and nested calls), and for the two new
recipes a semantic witness in every mode.

### `_updateEnemyKills` → `score.updateEnemyKills`

No existing recipe kills an enemy, so the corpus gained **`enemy_kill`**
(`scenarios.py` `KillScenario`): zone 9 room 18 with enemies; before the mark
the weapon is drawn, 600 vsyncs let the nearer Zombie come, 50 vsyncs of
Right+Down walk at it and every hostile enemy's HP is set to 1; the recorded
play is Circle, Circle, Circle (sphere, body part, attack) and 831 vsyncs
more. Witness: the sum of `enemyKills` rises (20 → 21) in every run.

| Check | Result |
|---|---|
| Corpus, Vulkan | passed: control; call 1 root call, 0 mismatches; tick 900/900, no page differs; alternate 900/900 (*correction: a launch-timing artefact, see "After the adversarial reviews"; the recipe called the function once, on one branch*) |
| Corpus, null | passed, same counts |
| Smoke, `battle_146C=shadow` / `=owned` | passed (162 routed calls of battle_action, 0 mismatches); `_updateEnemyKills` is not called in that room (no kill). *Correction: `=owned` only shows nothing crashed: an owned subsystem bypasses shadow, so nothing of it is compared* |
| Wrong variant: `kills.* +%= 2` | call: mismatch in `_updateEnemyKills`, **frame 2975**, `vs_main_scoredata.enemyKills[2]` (0x80060EA8), expected 1, actual 2; tick and alternate: first tick 407, same field (*alternate caught it only because the call fell on an odd vsync in that launch*) |
| Hot swap | same PID; library build 0.135 s, reload 0.23 ms, edit to witnessed mismatch 1.01 s (408 single-stepped vsyncs included); mismatch frame 2975, 0x80060EA8, 1 → 2; generation 2; restored generation 3 clean |

### `func_800B07DC` → `actor_transform.offsetToWorld`

The existing `enemy_cast_wait` recipe calls it **496 times** in its 600
vsyncs (shadow coverage), so no new recipe was needed (the `actor_transform`
corpus entry stays "incomplete": it would need its own semantic witness).

| Check | Result |
|---|---|
| Corpus, Vulkan | passed: call 496 root calls, 0 mismatches; tick and alternate 600/600 (*corrections: alternate was a launch-timing artefact; in the corpus `roll` is always 0 and `offset + position` never overflows an i16, so a port that ignored roll (T2) or saturated (T3) passed every mode: a value-variety gap*) |
| Corpus, null | passed, same counts |
| Smoke, `battle_44F14=shadow` | passed: 658 routed calls (496 of them this function), 0 mismatches |
| Smoke, `battle_44F14=owned` | passed (*no crash only: nothing compared*) |
| Wrong variant: `offset.vy += position.vz` | call: 496 mismatches, first in `func_800B07DC`, **frame 1883**, `actorTransformScratch@0x1F80035C.offset.vy` (0x1F8003B2), expected 116, actual 180; tick: first tick 0 (first snapshot difference in the GTE's `m_mac0`; the enemy's cast never happened: witness failed); **alternate: not caught** (*an artefact of the launch's vsync parity: the reviewer's next launch caught it*) |
| Hot swap | same PID; build 0.145 s, reload 0.20 ms, edit to witness 0.21 s; mismatch frame 1883, 0x1F8003B2, 116 → 180; restored clean |

### `vs_battle_displaySceneMessage` → `scene_message.displaySceneMessage`

The corpus gained **`action_name_window`** (`NameWindowScenario`): the
`enemy_cast_wait` room and play, witnessed by the name windows the charging
tracker saw open (one: action 54, its text starting with the
`0x04FB00F8` header the function writes). One root call per run. Only the
`message == 11` branch runs there (the header survives, and a fault in the
`else` branch went unseen, below).

| Check | Result |
|---|---|
| Corpus, Vulkan | passed: call 1 root call, 0 mismatches; tick and alternate 600/600 (*correction: alternate was a launch-timing artefact; one call, one branch*) |
| Corpus, null | passed, same counts |
| Smoke, `battle_5BF94=shadow` | passed: 0 root calls, 3 nested (inside `func_8007138C`'s battle_action shadow transaction), 0 mismatches |
| Smoke, `battle_5BF94=owned` | passed (*no crash only: nothing compared*) |
| Wrong variant in the `else` branch (`D_800EBF58` row `action - 0x1C9`) | **not caught**: the branch is not exercised (a coverage gap, not an oracle miss) |
| Wrong variant in the exercised branch (`menuStrings[type + 21]`) | call: mismatch in `vs_battle_displaySceneMessage`, **frame 2031**, 0x801F7354 = `(*D_800EB9D8)+0x4` (the first text byte of the name window buffer), expected 0x1C, actual 0xE7; tick: first tick 148, same byte; alternate: not caught (*launch parity again: the reviewer's launch caught it*) |
| Hot swap | same PID; build 0.153 s, reload 0.25 ms, edit to witness 0.63 s; mismatch frame 2031, `(*D_800EB9D8)+0x4`, 28 → 231; restored clean |

## Time spent (wall clock, agent time)

| Piece | Time | Of which waiting on game runs |
|---|---|---|
| Reading the guide, tables, oracle, hot-swap docs; worktree; first build | ~25 min | build ~3 min |
| Step 0: the naming rule, table mapping, `orig:` check | ~10 min | — |
| Prerequisites (views, c, gte, psx, build integration, tests) | ~7 min after step 0 | — |
| `_updateEnemyKills` | 11.5 min | ~6 min (attack-recipe exploration 5 runs, corpus ×3, smokes ×2, hot swap) |
| `func_800B07DC` | 6.4 min | ~5 min (corpus ×3, smokes ×2, hot swap) |
| `vs_battle_displaySceneMessage` | 6.7 min | ~5 min (corpus ×4, smokes ×2, hot swap) |

Writing each port took 2-4 minutes; checking it took the rest.

## Problems hit

1. §20's prerequisites 1-4 did not exist (above): built them.
2. translate-c exported a second definition of `5BF94.c`'s tables
   (`pub export var D_800EC25C = ...`), which the build's "no mutable statics"
   check stopped. `zig_c.py` now turns data definitions into `extern`s.
3. Block-scope `extern` declarations (the `ROOD_PC` hooks' host functions)
   were dropped by `strip_bodies`, so `c` lacked `pc_charge_skip_name`; now
   hoisted (PORTING.md §11.4 said "the c module has them": it did not).
4. The hot-swap library failed to load: `undefined symbol:
   __zig_probe_stack` (a Debug safety panic's message buffer); `game_lib.py`
   now passes `-fno-stack-check` like the static objects. Then the host link
   failed on `memcpy@GLIBC_2.14` in its dynamic list (the library's libc
   imports); versioned imports are skipped.
5. `psx.Ptr(T).ptr()` returned `?*T`; for a 64-bit type aligned above 4 the
   alignment check would trap on a guest address: now `GuestPtr(T)`.
6. A 3-bit bitfield indexes a 6-entry array (`enemyKills[enemyClass]`): a
   plain Zig index would trap where the C writes into the next field;
   `psx.at`.
7. The field mapper could not name a scratchpad address (no symbol): added
   `TYPE@ADDRESS` roots. It still cannot name bytes in a heap buffer behind
   an `int*` (`D_800EB9D8`), and the corpus annotates after the run, when the
   buffer is freed; `trial_hotswap.py` labels them while paused, as an offset
   from each root pointer's target.
8. A scratchpad view as `*align(1) T` (as §5.7 said) cannot pass its fields
   to `psx.cptr`/`gte`; an aligned slot is `*T`.
9. Windows twin: translate-c for `x86_64-windows-gnu` lays bitfields out
   MSVC's way and fails the units' `_Static_assert`s; the twin's `.win.i` is
   now translated with GCC's layout (a Linux target).
10. Windows twin: `psx.weak` produced a `.refptr` stub (LLVM's only way to a
   COFF weak external), which the build rejects; on COFF it is now a strong
   PC-relative reference (PORTING.md §12): every Windows build links the
   hooks. Compiled and linked, not run.

## PORTING.md gaps found (fixed in this branch)

- §16 Names: rewritten (step 0): readable names, `orig:` lines, the table's
  `zig` path, `MEANINGS.tsv` `zig_name` for globals and fields; §2 module
  naming, §17, §11.4 names row and §19 item 10 follow it.
- §2: `c` and `views` as built (per unit, manifest, readable names).
- §5.4: an index type wider than the array (bitfield index) → `psx.at`.
- §5.7: natural alignment for aligned scratchpad slots; `TYPE@ADDRESS` roots.
- §11.4: block-scope externs are hoisted into `c`.
- §14: a hook whose host state is in a game-compiled `pc/src` unit
  (`hud_bridge.c`) is inside the compared state, so shadow mode is safe for
  it; check where a hook's state lives before applying §20 item 7.
- §20: what the trial built of items 1-5.

## Findings for the reviewers and the oracle

- **The `alternate` mode is blind to half of the game** (*fixed after the
  reviews: alternate now flips per routed call*). It flips the owner
  every vsync (`state_diff.c`: owner = `ticks & 1`), but battle logic runs
  once per game frame, every second vsync, so a function called once a frame
  always runs on the same side: either every call is owned (then `alternate`
  equals `tick`) or none is. The wrong `func_800B07DC` and
  `vs_battle_displaySceneMessage` passed `alternate` (their calls fall on
  even ticks after the mark); the wrong `_updateEnemyKills` was caught (tick
  407, odd). Not changed here; a schedule by game frame (or period 3) would
  fix it.
- Branch coverage is not measured: the corpus calls `displaySceneMessage`
  but only its `message == 11` branch; `== 12` and the `else` branch (with
  the negative `D_800EBF58` row) have never run against the oracle.
- `_updateEnemyKills` runs once per kill: one call per corpus run.
- 32-bit: the owned objects compile for i686 (views from the 32-bit DWARF)
  but shadow mode is 64-bit only, so the 32-bit ports are compile-checked,
  not oracle-checked.

## Cost estimate for the bulk port (battle, D's first pass)

Measured: writing a faithful S/M/L port of 20-35 lines with this tooling
took 2-4 minutes; no port needed a fix after its first shadow run. Per
function, the rest is names (reading users to justify a readable name: 1-3
minutes), views manifest entries, and getting the corpus to call it.

| Difficulty | Functions (C, FUNCTIONS.tsv) | Avg lines | Port + names (agent time) | Total |
|---|---|---|---|---|
| S | 1283 | 13 | ~3 min | ~64 h |
| M | 256 | 50 | ~8 min | ~34 h |
| L | 113 | 99 | ~18 min | ~34 h |
| XL | 30 | 266 | ~45 min | ~23 h |
| `.inc`/generated wrappers, asm replacements | 95 | small | ~3 min | ~5 h |

About **160 agent-hours** to write battle's first pass, plus:
- fixes from shadow mismatches: unknown rate (0 of 3 here); budget 10-15 %
  of functions at ~10 min: ~30 h;
- **corpus recipes**: the main risk. 20 recipes are still "incomplete", and
  each will leave functions uncalled (the per-function coverage lists them).
  A new recipe with a witness took ~8 minutes here (`enemy_kill`, mostly
  exploring inputs); budget 60-100 recipes: ~10-15 h;
- machine time: a corpus run is ~50 s per (subsystem, recipe, lane); 23
  units × ~25 recipes × 2 lanes ≈ 16 h of runs per full pass (the null lane
  runs in parallel; GPU runs serialise on the heavy lock).

So roughly **200-230 agent-hours** for battle to reach "corpus green in
shadow mode", if the mismatch rate stays low and the views generator copes
with the remaining types (it took every type these three needed, including
the 1.9 KB `D_800F4538_t`, on the first try).

## Builds and suites at the end

- No-Zig 64-bit build (`ROOD_NO_ZIG=1`): built; a null-renderer boot ran
  1800 vsyncs to the title (zone 20 room 0), reported no owned subsystems,
  advanced 60 more frames, no crash.
- Static 64-bit, `--engine-library` (dev64 host + `librood_game.so`),
  32-bit (`build_boot.py` and `--battle`, the Zig objects for i686) and
  Windows (`--target windows`, nice 19, ELF + COFF twins): built.
- `pc/build` (cmake --build) and ctest: 51/51 passed (with the new
  `zig_views`).
- `pc/tools/verify.sh --changed main -- -j 6` (both locks): builds 32,
  battle-32 and 64 passed; GL 130/133 (3 skipped) passed; Vulkan 129/133
  (2 skipped) and null 7/8 failed on two tests that pinned the old table:
  `ownership_smoke` (expected battle_action to be the only subsystem) and
  `verify_args_test` (the null-lane suite list). Both fixed (the trial
  subsystems must default to original) and rerun: 2/2 passed. Every
  `trial_corpus_*`, `shadow_battle_smoke`, `battle_corpus_candidate` and
  `zig_hotswap_smoke` passed on all three lanes.

## Risks

- (Fixed after the reviews.) The alternate-mode blindness above: a function that only `alternate`
  checked is not checked.
- Corpus breadth is the bottleneck, and branch coverage is invisible.
- On Windows `psx.weak` is never null (problem 10); the Windows twin was
  built, not run. The i386 struct-return ABI remains unchecked.
- The `views` names: the trial renamed 9 globals and 4 fields (13 names:
  `actions`, `textBoxes`, `nameWindowText`, `stringContext`, `scoreData`,
  `nameWindowTimer`, `nameWindowHeld`, `sceneMessages`, `menuStrings`;
  `stats`, `allegiance`, `pitch`, `roll`); every rename
  needs evidence, and wrong meanings would mislead the bulk port (each is
  marked "(inferred)" where read from use).
- Debug-only hot-swap library: `-fno-stack-check` means a deep Debug frame
  is not probed (as in the static build).

## After the adversarial reviews

### What the reviews found, and what was done

| Finding | Fix | Evidence |
|---|---|---|
| **B1** (bug): `psx.at` gave the element pointer its base's alignment; `&score.enemyKills` is `*align(4) [6]u16`, so an odd `enemyClass` (Beast, Phantom, Evil) trapped "incorrect alignment" in ReleaseSafe and in the Debug hot-swap library, with `battle_146C` owned or shadowed. PORTING.md §5.4 used this exact call as its model | element alignment = min(base alignment, the largest power of two dividing `@sizeOf(E)`); §5.4 states the rule and the trap | psx unit tests for over-aligned bases with u8, u16, 3-byte, 6-byte and u32 elements (fail on the old code: "expected 2, found 4"); zig_diff mutation B1 (the old rule): PANIC at the first odd class; corpus, odd classes now killed: the old rule exits the game in tick mode with `[zig] panic: incorrect alignment` |
| Differential check (the reviewer's method) | `pc/tools/zig_diff.py` + `pc/tests/zig_diff/`, CTest `zig_diff`: the build's own `FUNCTION__orig` next to the port, random and edge inputs, all touched memory, a trace of external calls (arguments and the state hash at each call), return values; mutation self-checks; PORTING.md §1, §19 item 11 | 40,000 iterations per function, 0 mismatches; every declared input class reached (kills: no stats, not hostile, new class, same class < 100, at 100, clamped; transform: roll 0 and not; name window: 11, 12, other with action 0 and not); all 15 mutations caught (below); ~40 s for the three |
| R1: translate-c copies of the unit's statics in `c_<unit>` | every file-scope static (variables, tables, static functions, header `static inline`s) is a `@compileError` naming it; a leftover `pub var` fails generation; the overlay `.data`/`.bss` rejection reads the COFF twin too | CTest `zig_c` (each static used fails the build naming it; externs, prototypes, enums, types usable); `c_146C`: 16 statics poisoned (the five `charge_skip` variables, `D_80068BEC`, ...) |
| R2: the COFF twin's views came from the ELF DWARF | a Linux-hosted `--target windows` build also emits the views from the twin's own DWARF (gdb reads COFF) and fails on any difference | Windows build (below) |
| R3: a union's bitfield member emitted full-width | refused with its name | `zig_views` test |
| R4, R5: `psx.put`'s `@alignCast` on native fields; `psx.at` on a null `[*c]` base | PORTING.md §5.2, §5.4 | — |
| Nits | `zig_c.py` docstring and the dead `windows` parameter; §5.7's example and rule (a multiple of the type's alignment on every target); the rename count (9 globals + 4 fields, Risks); MEANINGS.tsv `allegiance`: a one-hot mask (0x10, `& 7`, `& 0x8000`, the target masks) | — |
| Oracle 1: `alternate` flipped per vsync, battle calls land on even gaps only, so a function was always or never owned by launch parity | owner 3 (`alternate`) in the trampolines: each routed call flips its route, owned/original counted per function; `differ.py` fails the mode when a called function ran on one side only, unless waived with a reason (a function a recipe calls once: `battle_action`'s four pass-throughs in `enemy_cast_wait`) | `routing_probe` on i386, sysv64 and win64 (wine): owned, original, owned and the counts; trial runs: transform 248/248, name window 2/1, kill 1/1 |
| Oracle 2: a subsystem passed with any call | every routed function needs root calls in call mode in some selected recipe, or a reasoned waiver (`pc/tests/diff/waivers.toml`: battle_action's four pass-throughs) | `root_calls.json`; corpus_test fixtures |
| Oracle 3: feature-gated `ROOD_PC` hooks outside the oracle | `--profile features` (corpus.py: the charging skip on, for every caster; input-changing features stay off); the base witness reset no longer forces the skip off; run.py `trial_corpus_*_features` | all three recipes pass in both profiles; N3 still slips both (below) |
| Oracle 4: witnesses not exact, one call per run | `enemy_kill` now kills twice (the idle Zombie moved within reach; classes 1 and 3; the streak at class 1, count 99): same class 99 -> 100, then a new class; exact witness (kill counts, streak, streak score 8197, kill flags). `action_name_window` runs 1200 vsyncs: three windows, exact texts | corpus_test fixtures reject +2 kills, a wrong streak, score or flag, and corrupted, missing or truncated texts |
| Oracle 5: TRIAL.md | the corrections above, the gaps and the estimate below | — |

### The wrong variants, re-run

Corpus: null lane, classic profile unless said, 64-bit, after the fixes;
call / tick / alternate (tick and alternate also fail their exact witness
where they diverge). zig_diff: `--self-check`, seed 1.

| Variant | Corpus | zig_diff |
|---|---|---|
| K1 `kills +%= 2` | caught in all three: call frame 2985 `vs_main_scoredata.enemyKills[1]` 17 -> 18; tick 408; alternate 408 (1 owned, 1 original) | caught: `vs_main_scoredata+0xe` |
| K2 `chainStreak` read as `weaponKillStreak` | caught in all three: frame 2985 / tick 408, `streakScore` | caught |
| K3 `killStreakEnemyCount < 99` | **now caught** in all three (the first kill takes the count 99 -> 100): frame 2985 / tick 408, `streakScore` | caught |
| K4 no clamp at 999999999 | not run (the corpus does not clamp: below) | caught |
| Odd class (B1, the old `psx.at`) | caught: the game exits in tick mode, `[zig] panic: incorrect alignment` | caught: PANIC, iteration 0 |
| T1 `vy += position.vz` | caught in all three: call frame 1895 `actorTransformScratch@0x1F80035C.offset.vy`, 496 mismatches; tick 1; **alternate now caught** (300 owned, 300 original) | caught: scratchpad 0x1F8003B2 |
| T2 roll ignored | **slips all three** (roll is always 0 in the corpus) | caught: scratchpad 0x1F8003E4 |
| T3 saturate, not wrap | **slips all three** (no i16 overflow in the corpus) | caught: scratchpad 0x1F8003B0 |
| N1 `menuStrings[type + 21]` | caught in all three: call frame 2031 0x801F7354 (the window text), tick 137, alternate (2 owned, 1 original) | caught: `vs_battle_printf` argument 1 |
| N2 `pc_hud_name_window` dropped | caught in all three: `name.serial` (hud_bridge state), frame 2031 / tick 137 | caught: one external call fewer |
| N3 `pc_charge_skip_name(1)` dropped | **slips all three, in both profiles** (below) | caught: the first call differs |
| N4 held timer 0x77; N5 the `else` branch's row; N6 message 12; N7 `unk0_8` cleared after the printf | not run in the corpus (N5, N6: branches no recipe takes) | all caught (N7 by the state hash at the printf) |

### Known gaps (variants that slipped, and why)

- **K3** slipped the trial's corpus because its one kill never took the
  same-class branch. The two-kill recipe takes it at the 99 boundary: caught.
  The streak at 100 (no increment) and the 999999999 clamp are still not in
  the corpus (the witness needs an exact final score, which a clamp hides):
  zig_diff covers both (K4).
- **T2, T3**: a value-variety gap, not a path gap. `func_800B07DC` is one
  basic block and every corpus call runs it, but `roll` is 0 for every actor
  the corpus reaches and no offset + position overflows an i16. A recipe with
  a rolled actor and far offsets would be needed; zig_diff catches both with
  random values.
- **N3**: `pc_charge_skip_name(1)` only acts when a name window held past a
  skipped wind-up is still up when the next one opens. Even with the skip on
  (features profile), the room's casts are ~200 vsyncs apart and a held window
  closes within 2 s, so the hook never acts and dropping it changes nothing
  the oracle sees. A recipe with two wind-ups in quick succession (Ashley's
  spell right after an enemy's) would be needed; zig_diff catches it (the
  call is missing from the trace).
- The `message == 12` and `else` branches of `displaySceneMessage` (N5, N6)
  and the non-hostile / no-stats paths of `_updateEnemyKills` are still never
  run by a recipe; zig_diff's classes cover them.
- The B1 variant's corpus run lost its per-mode results: the panic in tick
  mode exited the game, so `differ.py` reports only the run's error. Call
  mode survives a panic (shadow recovers it); tick and alternate cannot.

### Cost estimate, revised

The reviewers' points, and what the fix work measured:

- **Corpus work: 30-60 h, not 10-15.** A recipe that merely calls a function
  took ~8 minutes; one that reaches a chosen branch with exact expectations
  did not: the two-kill recipe took ~25 minutes of agent time and 8 game runs
  (finding a reachable second enemy, the kill order, the classes, the
  expected score), the three-window recipe ~10. Branches like
  `message == 12`, the scene-message `else` rows or a held name window need
  targeted state and inputs; value variety (T2, T3) needs more.
- **A coverage lane per unit** (design note below): build it once (~4-6 h),
  then per unit a coverage run of all recipes and the work to close or waive
  its gaps: ~1-2 h per unit, 23 units: **25-45 h**.
- **zig_diff specs**: a generic spec generator (pointer parameters as arena
  objects of their view types, integer parameters at their C edges) first,
  ~4-6 h; then each function's biases, classes and its review mutations:
  ~2 min (S) to ~25 min (XL), **~60-80 h** for battle. It is the cheapest
  strong check: seconds per function, no game, no lock.
- **"0 of 3 needed fixes" is weak evidence**: 3 of 6 plausible wrong
  variants produced no corpus mismatch. Budget fixes for 10-20 % of
  functions at ~10 min: **~30-60 h**.
- **Machine time and lock contention**: ~45-75 s per game run matches, but
  with three agents on the machine the reviewer waited 8-25 min per hold of
  the heavy lock. So: one call-mode run per recipe with **all subsystems
  shadowed at once** (each call is compared on its own), not one per
  (subsystem, recipe): ~25 recipes x 2 lanes x ~1.5 min = ~1.5 h per full
  pass instead of ~16 h; tick and alternate only per subsystem being
  flipped; the null lane outside the heavy lock.

Revised total for battle to "shadow-green with coverage": writing ~160 h +
fixes 30-60 h + corpus 30-60 h + coverage lane 25-45 h + zig_diff specs
60-80 h, about **305-405 agent-hours** (was 200-230), plus machine time
that is small once runs are batched as above.

### Design note: the coverage lane and per-call value ranges (next task)

Not built here (a separate task follows; its spec half is now built: see
"Built: the coverage lane on the zig_diff inputs" below); what it should be:

- **Coverage build**: compile each routed unit's `__orig` bodies (the same
  `.i`, the routed functions only) a second time at `-O0 --coverage
  -fcondition-coverage` (GCC 14+, MC/DC) into a coverage variant of the
  64-bit binary; `-O0` because the reviewer's gdb basic-block prototype
  showed `-O2` line tables misattribute lines and branchless code (the
  999999999 clamp) hides decisions. Run every recipe original-only (no
  shadow, no owned code) with `GCOV_PREFIX` per recipe, merge with
  `gcov --json-format`, and report per routed function: lines, branches and
  conditions covered, and which recipe covered each.
- **Gate**: per function, every branch and condition covered by some recipe,
  or a waiver with its reason (the `waivers.toml` of the root-call gate,
  with a `coverage` table); the uncovered list is the corpus to-do list.
- **Per-call value ranges**: in call mode the shadow wrapper already holds
  each call's arguments; record per routed function the min/max and a small
  set of distinct values of each argument, and at entry the same for the
  globals in the unit's `views` manifest (read through the field map). A
  report like "roll: always 0" or "offset + position: never overflows" (the
  latter needs a derived-value probe the spec names) flags T2/T3-style gaps
  that coverage cannot see.
- **Feedback**: uncovered branches and narrow ranges become zig_diff spec
  biases (cheap) or new recipes (expensive); a function counts as checked
  when zig_diff's self-check passes and the corpus covers its branches.
- **Cost**: gcov runs are cheaper than the gdb prototype (40-85 s per
  recipe); one coverage pass over ~25 recipes is ~30-40 min of machine time.

### Built: the coverage lane on the zig_diff inputs (`zig_diff.py --coverage`)

The first lane built measures what the **specs** reach, not the corpus
recipes (the corpus lane above remains a design): CPU only, seconds per
function, and the gate every port's spec passes before any game run.

- **Build**: the routed `.i`'s `FUNCTION__orig` (or an unrouted function's
  body, renamed) again at `-O0` with `--coverage` (gcov branches; GCC 14+
  adds `-fcondition-coverage`, MC/DC: this machine's GCC 13 gives branches
  only, and the report says so), `-fsanitize-coverage=trace-cmp` (every
  comparison's operands) and `-fsanitize=thread` instrumentation with
  `pc/tests/zig_diff/zdiff_cov.c` as its runtime (every load's address and
  value; libtsan is never linked). gcov's counters stay out of the
  per-iteration restored section. Only the C runs (`gen.coverage`).
- **Report** per function: branches hit/total and each uncovered one with
  its line (`+N bK`, relative to the function's first line, so waivers
  survive edits elsewhere in the unit); per argument min/max, zero,
  negative, the values when few; per comparison the `==`/`<`/`>` counts
  (signed and unsigned); per load its range and where it read (a global +
  offset, the arena, the scratchpad).
- **Value flags** (branch coverage cannot see them): `+N eq`, an ordered
  comparison whose two sides were seen both ways but equal never, or fewer
  than 10 times and under 1% of its runs (one equal input can be masked by
  a later test: Haiku's U1 spec had exactly one); `+N sign`, a test whose
  true side (values above its constant, unsigned) only ever held values with
  the sign bit set, so a port testing `< 0` would pass (Haiku's A3; a lone
  -1 or a lone sign bit, which is what a 1-bit or top-bit field gives, is
  not flagged).
- **Gate**: `--coverage-threshold` (default 100% of branches, waivers counted
  as covered), no unwaived flag (`--coverage-values report` only reports
  them); waivers in the function's `specs.toml` entry,
  `[[function.F.coverage_waiver]] id = "+38 b1"`, `reason = "..."`, a waiver
  without a reason fails, a stale one is reported. CTest `zig_diff` runs
  `--self-check --coverage`; CTest `zig_diff_coverage` weakens checked-in
  specs the way real misses looked and requires the gate to name them.

**The 14 functions with specs on `main`** (seed 1, the specs' own iterations),
before and after this branch's spec fixes:

| Function | Branches (main's spec) | Flags (main's spec) | After |
|---|---|---|---|
| `_updateEnemyKills` | 14/14 | `+25 eq`: `streakScore > 999999999` never exactly at the clamp | streak score exactly 999999999 a quarter of the clamp cases: passes |
| `func_800B07DC` | 0/0 (one block), 21/21 lines | - | passes |
| `vs_battle_displaySceneMessage` | 9/10: `+38 b1` `if (pc_hud_name_window)` null side | - | waived (the weak hook is always linked); `message == 11`, `== 12` and the `else` rows are all covered (the trial reviewer's gap is closed by the spec) |
| `func_8006CE50` | 0/0, 1/1 lines | - | passes |
| `func_8007C088` | 0/0, 1/1 lines | - | passes |
| `vs_battle_lerpSvector` | 0/0, 11/11 lines | - | passes |
| `func_8007357C` | 0/0, 6/6 lines | - | passes |
| `_factorDamageReceived` | 4/4 | - | passes |
| `func_8008D5FC` | 4/4 | - | passes |
| `func_80074A20` | 10/10 | - | passes |
| `vs_battle_applyWeaponStats` | 8/8 | - | passes |
| `func_800AD494` | 32/32 | - | passes |
| `func_80078828` | 49/50: `+10 b3`, `unk4A == 0` never generated (the spec left the random fill, never 0) | `+76 eq`, `+82 eq`: the cost never exactly 0x00FFFFFF (GCC tests `& 0xFF000000` as `> 0xFFFFFF`) | unk4A 0 one time in 8, the stub returns 0x00FFFFFF and 0x01000000 too: 50/50, passes |
| `func_800A0204` | 82/84: `+34 b2` the second flag-15 test never reached with keepFlag15 clear (only a callee could set the bit again); `+85 b2` unk34 never with only vz nonzero | - | a `func_800AC500` stub that sets flag 15 again half the time, unk34 with vx = vy = 0 and vz nonzero: 84/84, passes |

**Haiku's two spec misses (the spec bake-off) are now caught**, with the
model's own saved specs: `func_800AD494` (its final spec): `+32 eq`,
`idx >= actor->unk5B6` in the 0xFE branch equal 1 time in 763 (the one
equal input returned 0 for another reason, so U1 slipped), plus an uncovered
`+13 b2` (`unk6E6 == 2` never); `func_80078828`: `+76 sign` and `+82 sign`,
every cost above 0xFFFFFF had the sign bit set (the stub returned the top
byte all clear or all set), so A3 (the sign bit only) slipped. Both specs fail
the gate. Boundary equality is not visible to branch coverage (the branch
was covered both ways); the value flags are what see it.

Not built: the corpus half (each recipe through a coverage build of the
game). The specs' lane covers what the bulk port gates on; the corpus lane
remains the design above.

## Reproducing

```sh
python3 pc/tools/build_boot.py --arch 64            # under $ROOD_BUILD_LOCK
python3 pc/tools/zig_diff.py --self-check           # CPU only, ~40 s
python3 pc/tools/zig_diff.py --self-check --coverage # + the coverage lane, ~2.5 min (CTest zig_diff)
python3 pc/tools/zig_diff.py func_800AD494 --coverage --spec SPEC.zig   # another spec through the lane
ROOD_ARCH=64 ROOD_RENDERER=null python3 pc/tests/diff/differ.py battle_146C --scenario enemy_kill --profile features
ROOD_ARCH=64 python3 pc/tests/run.py --arch 64 trial_corpus shadow_battle_smoke
ROOD_ARCH=64 ROOD_RENDERER=null python3 pc/tests/run.py --arch 64 --renderer null trial_corpus
python3 pc/tests/shadow_battle_smoke.py --card CARD --owner battle_5BF94=shadow
python3 pc/tools/build_boot.py --arch 64 --engine-library
ROOD_ARCH=64 python3 pc/tests/trial_hotswap.py battle_146C _updateEnemyKills battle/score.zig \
    "kills.* +%= 1;" "kills.* +%= 2;" enemy_kill --card CARD --work pc/build/trial-hotswap
```
