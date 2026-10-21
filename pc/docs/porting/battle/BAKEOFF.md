# The model bake-off: eleven battle functions, scored automatically

Several (cheap) models each port the same eleven battle functions from the
same prompt; a CPU-only scorer compares every port with the original C on
random and edge inputs. This file is the setup: the functions and their
fixed names, what each spec covers, the exact context a model gets, how to
run and score, and the reference result (the strong-model baseline: the
ports in `game/battle/`, written per `../PORTING.md`).

Read `TRIAL.md` first for the porting method and `../PORTING.md` §1 and
§19 item 11 for the differential harness (`pc/tools/zig_diff.py`).

## The functions

All 64-bit Linux, from the routed `.i` of `pc/build/boot64`. One Zig module
per function in `game/battle/`, each with its `/// orig:` line and its
`zig` path in `game/ownership.toml` (default `original`). Subsystems, one
per unit: `battle_146C_bakeoff` and `battle_5BF94_bakeoff` (beside the
trial's `battle_146C`/`battle_5BF94`, so the trial's corpus suites and
their root-call gate keep checking only the trial's functions), and
`battle_30DB0`, `battle_40564`. The list, signatures and traps are data in
`pc/tests/zig_diff/bakeoff.toml`.

| # | Original (unit) | Module.function | Signature (Zig) | Difficulty (FUNCTIONS.tsv) |
|---|---|---|---|---|
| 1 | `func_8006CE50` (146C.c) | `room_timer.startSection12Timer` | `() void` | S, 1 line |
| 2 | `func_8007C088` (146C.c) | `actor_weapon.isWeaponDrawn` | `(actorId: c_int) c_int` | S, 1 line |
| 3 | `vs_battle_lerpSvector` (5BF94.c) | `vector_lerp.lerpSvector` | `(src: [*c]i16, t: c_int, vec: ?*v.SVECTOR) void` | S, 13 lines |
| 4 | `func_8007357C` (146C.c) | `field_menu.enterMenuMode` | `(c_int, c_int) void` | S, 7 lines |
| 5 | `_factorDamageReceived` (146C.c) | `hit_factors.factorDamageReceived` | `(action: ?*v.vs_action_t, ?*v._hitEntity_t, ?*v._hitEntity_t, hitNumber: c_int, c_int) i16` | S, 12 lines |
| 6 | `func_8008D5FC` (146C.c) | `room_lock.operateLock` | `(lock: ?*v._mpdRoomSectionA) void` | S, 12 lines |
| 7 | `func_80074A20` (146C.c) | `actor_defeat.checkDefeated` | `(actorId: c_int) c_int` | S, 23 lines |
| 8 | `vs_battle_applyWeaponStats` (146C.c) | `weapon_stats.applyWeaponStats` | `(target: ?*v.vs_battle_uiWeapon, source: ?*v.vs_battle_weaponIntermediate) void` | M, 59 lines |
| 9 | `func_800AD494` (40564.c) | `animation_lookup.findAnimation` | `(actor: ?*anyopaque, animation: c_int, frames: [*c][*c]u16) c_int` | M, 69 lines |
| 10 | `func_80078828` (146C.c) | `ability_window.canUseBattleAbility` | `(button: c_int) c_int` | M, 103 lines |
| 11 | `func_800A0204` (30DB0.c) | `actor_animation.startAnimation` | `(actorId: c_int, animation: c_int, c_int, blendFrames: c_int) c_int` | L, 123 lines |

Every signature ends `callconv(.c)`; `v` is the unit's views module.
Parameter names are free (an unused one must be `_`); the types are fixed,
because the spec builds the arguments from the port's own type.

**Replaced:** `func_80084770` (146C.c, S, 10 lines) was on the list. Its
`int` return is undefined: no `return` at all, and the object returns
whatever `%eax` holds, the low half of `D_800F19CC` on one path and the
void callee `func_800801E0`'s leftover on the other. Neither the harness
nor shadow mode can compare that return (it is not a value the C computes),
and a port cannot reproduce it. Its whole family of hit-effect functions in
146C.c has the same defect. **`_factorDamageReceived`** replaces it: the
same table of hit functions, the same five-parameter signature and size
(S, 12 lines), with two bitfields, a signed division and a short result.

Names: from the decompilation's own name where it has one
(`lerpSvector`, `applyWeaponStats`, `factorDamageReceived`), else from what
the function does, read from its callers and callees (all inferred):
`func_8006CE50` (an event-script opcode's action) starts the section-12
timer `D_800F1D68` at 180 through `func_8008D5A0`; `func_8007357C` puts
`vs_battle_exec` in mode 5 (the field menu: Triangle in the field);
`func_8008D5FC` operates a map section A lock (`keyOrSigil`; the toggle
`func_8008D658` plays the door sounds); `func_80074A20` settles an actor at
0 HP; `func_80078828` decides whether a chain or defense ability may be
input; `func_800AD494` looks an animation up in the actor's tables;
`func_800A0204` starts an actor's animation.

## What each spec covers

`pc/tests/zig_diff/<Function>.zig` builds the inputs and names the input
classes; `specs.toml` holds the mutations (wrong variants that
`zig_diff.py --self-check` must catch). Every class is reached and every
mutation caught (seed 1); the counts are of 40 000 (or 20 000) iterations.
External calls are recording stubs (arguments and a hash of all state at
each call), so a dropped, added, reordered or re-argued call, and a store
moved across a call, all show.

| Function | Inputs and classes (iterations reaching each, seed 1) | Mutations (all caught) |
|---|---|---|
| `func_8006CE50` | none; one call (2 000 iterations) | S1 179 ticks; S2 the call dropped |
| `func_8007C088` | id 0-15, every slot an arena actor, weaponDrawn 0/1/2/3/-1/-2/INT_MIN/INT_MAX: drawn 10009, sheathed 9991 | W1 `& 3`; W2 the next slot (a crash at slot 16) |
| `vs_battle_lerpSvector` | pairs at the i16 edges; t in 0-128, outside it, and the int edges: t-0-128 21524, t-outside 4259, product-wraps 14217 | V1 `/ 128` for `>> 7`; V2 vy from the z pair; V3 a trapping `*` (panic) |
| `func_8007357C` | the character's weaponDrawn 0/1/2/3/-1: drawn 11064, sheathed 8936 | M1 mode 4; M2 the weapon bit unmasked; M3 the camera mode set after the calls |
| `_factorDamageReceived` | hpEffect 0-3, hp/mp at the i16 edges, statFactor 0-31 on either hit, hit 0/1: no-effect 10054, effect-1 4710, effect-2 10046, effect-3 4667, negative 7007, i16-overflow 3516 | F1 `== 2` for `== 3`; F2 `@divFloor`; F3 hp + mp in i16; F4 always hit 0 |
| `func_8008D5FC` | unkB 0 or 1/0x7F/0x80/0xFF (a signed char test), state 0 or not: busy 4971, locked-queued 4982, open-toggled 5021, busy-negative 5026 | L1 `<= 0`; L2 21 ticks; L3 no return after queueing; L4 busy set after the calls |
| `func_80074A20` | id 0-15 (0 a quarter of the time), slots empty or actors with HP 0 or not, unk26 0 or not, unk27 0x80 or not: empty 4940, alive 17592, falls-unk26 8689, defeated 4336, defeated-0x80 3121, ashley-0x80 1322 | D1 `and` for `or`; D2 0x10 for 0x20; D3 the wrong block field's address; D4 the first call after the null test |
| `vs_battle_applyWeaponStats` | random weapon and parts (negative sums, u16 wrap), damage type 0-3 or 4-7 (past `types`, as the C reads): damage-type-0-3 7468, damage-type-4-7 4950, negative-strength 7582 | P1 the blade's types; P2 the third gem left out; P3 7 classes; P4 a wrong memcpy size; P5 unk168 not cleared |
| `func_800AD494` | six table fields and `D_800F4768` arena tables or NULL (index tables with room before them), counts and globals at their edges, animations in every range and -16..-1: anim-0 2907, ff-index 860, ff-slot 117, fe 559, dc 2946, ca 2903, 100 1947, low 3988, negative 5997, no-slot 375, past-count 9821, no-table 2991, empty-entry 4589 | U1 `>` for `>=`; U2 `D_800F49DC` not set; U3 `+ 8` for `+ 10`; U4 the 0xFE branch's slot; U5 the empty entry missed |
| `func_80078828` | button 0-5; the current action's entry tests passing or not, chain or defense, first hit on nobody/the same/another actor, actionIndex at 0/39/40/41/53/54/0x8000/0xFFFF, mapped abilities 0 or not, unk27 0x80 or not, the previous input, the attempted flag; `_getActionCost` returns a top byte half the time: entry-fails 9280, any-chain 3493, any-defense 3535, chain-button 5402, defense-button 5296, previous-input 4413, attempted 4479, action-40-53 4102 | A1 the `- 40u` compare signed; A2 `button` for `button - 1`; A3 the sign bit only; A4 the defense loop reads the chain slots; A5 `func_800A0BE0` dropped; A6 the attempted flag ignored |
| `func_800A0204` | id 0-16, slot NULL or an arena actor with random flag words (stop bit rare), unk34 zero or not, animationId 84 or not, unk63C at 0x7F/0x80; animations 0/1/6/13/25/26/36/39/41/59/84/-100 and -200..300; blendFrames 0/1/2/3/-1/255/256/INT_MIN/INT_MAX/2^30; per-iteration stub plans for `func_800AD494` (found or not, frames NULL or not, `D_800F49DC` and `found` equal to the actor's or not, first call and retry) and `func_800BEB00` (4 or not): animation-0 581, empty-slot 3961, stopped 1686, same-84 90, restart 17660, no-restart 4121, retry-ok 7465, retry-fails 3745, turned 691 | R1 the failed retry returns 0 (the object returns `found`); R2 0x7F; R3 only 25 keeps flag 15; R4 the goto mapped wrong (the still branch turns); R5 the second frame word; R6 no retry |

Not covered by any spec (by construction of the harness): what a callee
would have done with its arguments (stubs only record them); host state
outside the regions (none of these functions touches any); the 32-bit and
Windows objects (the harness is 64-bit Linux only).

### The corpus (shadow call counts; informational)

Call mode of `pc/tests/diff/differ.py SUBSYSTEM --scenario RECIPE --mode
call` on the null lane, classic profile, for each of the three implemented
recipes, 64-bit, `ROOD_RENDERER=null`. The corpus is not part of the score.

| Function | enemy_cast_wait | action_name_window | enemy_kill | Mismatches |
|---|---|---|---|---|
| `func_8006CE50`, `func_8007C088`, `func_8007357C`, `_factorDamageReceived`, `func_8008D5FC`, `vs_battle_applyWeaponStats` | 0 | 0 | 0 | - |
| `func_80074A20` | 2 | 4 | 10 | 0 |
| `func_80078828` | 1 | 2 | 5 | 0 |
| `vs_battle_lerpSvector` | 804 | 1632 | 312 | 0 |
| `func_800AD494` | 528 | 1033 | 1643 | 0 |
| `func_800A0204` | 23 | 46 | 93 | 0 |

(Root calls; no nested calls in these runs, each subsystem shadowed on its
own. Every recipe passed its witness; the root-call gate fails
`battle_146C_bakeoff` because six of its eight functions are never called:
expected, the gate is not used for the bake-off.) So five of the eleven
reference ports also have shadow-mode evidence (0 mismatches over every
call), and six have only the harness.

## The per-function context (the prompt)

`python3 pc/tools/bakeoff_context.py FUNCTION` prints it (`--all OUTDIR`
writes all eleven and their sizes; `--no-guide` leaves PORTING.md out, for
a model that is given it once). In order:

1. **The task**: write the complete `game/battle/<module>.zig`; the exact
   signature (from `bakeoff.toml`); the `/// orig:` line and a meaning line;
   imports limited to `std`, `common` (psx), `views_<unit>`, `c_<unit>`;
   reply with the file only.
2. **`pc/docs/porting/PORTING.md`**, whole.
3. **Two example ports with their C**: `_updateEnemyKills` →
   `game/battle/score.zig` (bitfields, `psx.at` past an array, wrapping
   score arithmetic) and `vs_battle_displaySceneMessage` →
   `game/battle/scene_message.zig` (globals, `psx.put`, string tables, a
   weak hook), each C body from the routed `.i`.
4. **The function's C**: the body from the routed `.i` (what GCC compiled,
   `psx_get` expanded, `__orig` renamed back), and the `.c` body when it
   differs (for names and comments). For `func_800A0204` (`asm = true` in
   `bakeoff.toml`) also the game object's disassembly of it: its bare
   `return;` in an `int` function is undefined in C, and PORTING.md §1/§11.4
   say to port what the object returns.
5. **`views_<unit>` subset**: the `g` accessors of the globals the body
   names, and the views types it names (parameters, locals, casts, the
   globals' types) closed over the fields it accesses by name (a field
   renamed in MEANINGS.tsv matches its `/// orig:` name: `unk3C` finds
   `stats`); layouts keep fields and bitfield tables, not the offset
   asserts. Types of fields it does not touch are left out.
6. **`c_<unit>` subset**: the prototypes of the functions it calls, the
   constants and statics it names (a static shows as its `@compileError`),
   and the c types of those prototypes (a struct as a one-line stub: the
   views type of that name has the layout; an opaque one says so).
7. **`common.psx`'s public API**: signatures and doc comments.

Sizes (lines / characters; with PORTING.md, about 25 k tokens at most;
without it, 300-800 lines):

| Function | With PORTING.md | Without |
|---|---|---|
| `func_8006CE50` | 1643 / 81575 | 376 / 15284 |
| `func_8007C088` | 1634 / 81473 | 367 / 15182 |
| `vs_battle_lerpSvector` | 1577 / 79708 | 310 / 13417 |
| `func_8007357C` | 1612 / 80814 | 345 / 14523 |
| `_factorDamageReceived` | 1722 / 85338 | 455 / 19047 |
| `func_8008D5FC` | 1594 / 80096 | 327 / 13805 |
| `func_80074A20` | 1771 / 85227 | 504 / 18936 |
| `vs_battle_applyWeaponStats` | 1700 / 83877 | 433 / 17586 |
| `func_800AD494` | 1938 / 91752 | 671 / 25461 |
| `func_80078828` | 1994 / 92389 | 727 / 26098 |
| `func_800A0204` | 2067 / 95337 | 800 / 29046 |

## Running the bake-off

```sh
python3 pc/tools/build_boot.py --arch 64                      # once, under $ROOD_BUILD_LOCK
python3 pc/tools/bakeoff_context.py --all prompts/            # prompts/<FUNCTION>.md
# give each model each prompt; save its reply as ports-<model>/<module>.zig
python3 pc/tools/bakeoff_score.py ports-<model>/ --json <model>.json
```

Rules that keep it fair: the same prompt for every model; one attempt per
function for the headline (pass@1); a second round may feed back only the
scorer's own first line for that function (the compiler error, or the first
mismatch/panic line), reported separately; nobody sees the specs. A model's
reply is the file as given (strip a surrounding code fence, nothing else).

### The scorer

`pc/tools/bakeoff_score.py PORT_DIR [--json OUT] [--seeds 1,2,3]
[--iterations N] [--only FUNCTION] [-j 4] [--timeout 120]`, or
`--reference` for the checkout's own ports. For each function in
`bakeoff.toml` it takes `PORT_DIR/<module>.zig`, compiles it in place of the
reference in a scratch directory (zig_diff copies `game/common` and the
candidate there; the checkout is never written, so there is nothing to
restore), links it with the function's original C body and the stubs, and
runs the harness once per seed with the spec's iterations (the normal run:
no mutations). JSON per function: `present`, `compiled`, `compile_error`,
`orig_line`, `runs` (per seed: `exit`, `iterations`, `mismatches`, `panic`,
`crash`, `timeout`, `first` report line, `classes`), `passed`, `verdict`
(pass, mismatch, panic, crash, timeout, compile error, missing), `seconds`;
and a `summary` (passed, compiled, present, verdicts). A table goes to
stderr. Exit 0 when all passed, 1 otherwise, 77 without a 64-bit build.
CPU only, no game, no lock; about 26 s for all eleven with three seeds.
CTest `bakeoff_tools` (`pc/tests/bakeoff_test.py`) checks the prompts and
the scorer (the references pass; a wrong, a non-compiling and a missing
candidate get their verdicts).

A pass means "no difference found on these inputs". The corpus (shadow mode)
is the second opinion for the functions it calls; a port that passes here
and is then routed still goes through the corpus as any port does.

### The reference result

`python3 pc/tools/bakeoff_score.py --reference` (seeds 1, 2, 3):

| Function | Verdict | Iterations (3 seeds) | Mismatches | Seconds (compile + runs) |
|---|---|---|---|---|
| `func_8006CE50` | pass | 6000 | 0 | 1.13 |
| `func_8007C088` | pass | 60000 | 0 | 4.0 |
| `vs_battle_lerpSvector` | pass | 120000 | 0 | 7.33 |
| `func_8007357C` | pass | 60000 | 0 | 5.06 |
| `_factorDamageReceived` | pass | 120000 | 0 | 6.98 |
| `func_8008D5FC` | pass | 60000 | 0 | 3.99 |
| `func_80074A20` | pass | 120000 | 0 | 9.03 |
| `vs_battle_applyWeaponStats` | pass | 60000 | 0 | 6.45 |
| `func_800AD494` | pass | 120000 | 0 | 8.11 |
| `func_80078828` | pass | 120000 | 0 | 8.04 |
| `func_800A0204` | pass | 120000 | 0 | 12.43 |

11/11 passed; 26 s wall clock with `-j 4` (the per-function seconds overlap).

## What the setup found

- **`psx.globalMany` shrank `vs_battle_actors` on 64-bit.** It declared an
  unsized array as one element; the 64-bit build sizes each moved pointer
  import from every object's declarations, so the first views accessor of
  `vs_battle_actors` made `rood_lp64_800f1928` 2 slots instead of 16
  (`vs_battle_actors[2]` would have aliased `D_800F19CC`). Fixed: it
  declares `T name[0]`, and `build_boot.py` no longer lets a Zig object size
  a region and fails when one disagrees with the C (checked: the old
  `globalMany` fails the build naming `vs_battle_actors`; `native_imports.c`
  is identical to main's).
- zig_diff: native-moved arrays sized from `native_imports.c`; unprototyped
  callees (variadic in translate-c, `func_800BEB00`) stubbed as `R f(void)`;
  libc's `__stack_chk_fail` for functions with a local array; `expect =
  "crash"` mutations.
- `c_30DB0`'s `D_800F4538_t` and `c_146C`'s `vs_battle_actor2` and
  `D_800F19CC_t2` are opaque (translate-c cannot lay out their bitfields):
  `psx.cptr` cannot reach them; view pointers go through `@ptrCast`.
- `func_800A0204`'s missing return returns `found` in the 64-bit object;
  the harness's own compile of the body agrees (mutation R1).

## Risks

- **The specs are the score.** A blind spot in a spec is a blind spot for
  every model; the mutations bound it from below only. Most classes are
  well populated; `func_800A0204`'s same-84 (90) and `func_800AD494`'s
  ff-slot (117) are thin.
- **The prescribed signatures** remove one way to fail (a different but
  ABI-compatible parameter type would make the spec, not the port, fail to
  compile); a model that changes them fails to compile, which the verdict
  shows.
- **Unchecked indices past arrays** (`damageType` 4-7, `button` 4-5,
  negative animations) reward ports that follow PORTING.md §5.4 (`psx.at`)
  over plain indexing, which traps; that is the guide's rule, but the game
  may never produce those values.
- **The undefined return of `func_800A0204`** is ported from the object;
  the prompt carries the disassembly, but a model that returns 0 there is
  "wrong" only by the object's behaviour.
- **Stubs do not model callees**: a port that calls the right function with
  the right arguments passes even if the real callee would make a later
  read differ (none of these functions reads state a callee writes, except
  `func_800A0204`'s `D_800F49DC` and out-parameter, which its stub plans
  model).
- The scorer trusts the candidate to compile only against the listed
  modules; a candidate that imports something else fails to compile.

## Spec bake-off: can a cheap model write the specs?

Porting the eleven cost Claude Haiku 5.5 $0.08; the expensive part of a
bulk port is writing the zig_diff specs. `pc/tools/spec_bakeoff.py --out
DIR [--model M] [--budget 2.0] [--only F] [--rescore]` asks a model
(OpenRouter, `OPENROUTER_API_KEY` from the environment, temperature 0) for
each function's spec file and `specs.toml` entry with 3-6 mutations, and
scores the reply in DIR (the checkout's specs are never written). The
prompt: the task and how the harness uses a spec, PORTING.md §19, `zdiff.zig`,
two worked examples with their ports and `specs.toml` entries (the trial's
`_updateEnemyKills` and `func_800B07DC`, never one of the eleven), the
function's C, its accepted (reference) port, and the declarations it touches
(bakeoff_context.py's extraction; about 9.6k-20k prompt tokens). One
attempt; when the spec does not build or fails its own self-check, one retry
fed only the checker's error lines. Scores: (a) compiles; (b) self-check:
the reference port passes on seed 1, every declared class occurs and every
one of the model's mutations is caught as it declares; (c) strength: how
many of the reference spec's mutations (above, written with the reference
ports) the model's spec catches (any mismatch, panic or crash), counted
only when (d) holds; (d) the reference port passes the model's spec with 0
mismatches on seeds 1-3.

Haiku 5.5 (`anthropic/claude-haiku-5.5`), 2026-10-20:

| Function | Tries | Compiles | Self-check (own mutations) | Strength (reference mutations) | Reference port passes |
|---|---|---|---|---|---|
| `func_8006CE50` | 1 | yes | pass (5/5) | 2/2 | yes |
| `func_8007C088` | 2 | yes | pass (5/5) | 2/2 | yes |
| `vs_battle_lerpSvector` | 1 | yes | pass (5/5) | 3/3 | yes |
| `func_8007357C` | 1 | yes | pass (6/6) | 3/3 | yes |
| `_factorDamageReceived` | 2 | yes | pass (6/6) | 4/4 | yes |
| `func_8008D5FC` | 1 | yes | pass (5/5) | 4/4 | yes |
| `func_80074A20` | 1 | yes | pass (5/5) | 4/4 | yes |
| `vs_battle_applyWeaponStats` | 2 | yes | pass (6/6) | 5/5 | yes |
| `func_800AD494` | 2 | yes | FAIL (5/6) | 4/5 | yes |
| `func_80078828` | 1 | yes | pass (6/6) | 5/6 | yes |
| `func_800A0204` | 2 | yes | pass (6/6) | 6/6 | yes |

11/11 compile and accept the reference port on the first try; 10/11 pass
their self-check (6 first try); strength 42/44 (43/44 for the first
attempts). 16 calls, 233k prompt + 181k completion tokens, $0.11 and 13
minutes of model time (plus $0.06 for six calls of a first run whose 16k
`max_tokens` the three largest functions spent on reasoning, with empty
replies: the default is now 64k).

- **The first-try failures are mostly mechanical**: a mutation declared
  `mismatch` that crashes or panics (2), and mutations that do not compile
  because Zig rejects a now-unused local (3). The checker's error lines
  fixed them in the retry, except in `func_800AD494`.
- **Missed:** `func_800AD494`'s U1 (the count test as `>`: no input with
  `idx == unk5B6` exactly) and `func_80078828`'s A3 (the cost's sign bit
  only: the model's `_getActionCost` stub returns the top byte all clear
  or all set, never nonzero with bit 31 clear, where the default random
  value would have caught it). Both are value edges a reviewer names in one
  line. `func_800AD494` still fails its self-check after the
  retry (a declared class never occurs: classify's bound test comes first);
  the retry also lost the U1 catch its first attempt had.
- **The model's own mutations are weaker than the reference's**: many are
  constant changes and dropped calls that any input catches; fewer test a
  boundary or a signedness edge. Strength against the reference mutations
  is the better measure; the self-check mostly measures that the spec runs.

Read: Haiku-written specs are good enough as the first draft of the bulk
port's specs, gated by `zig_diff.py --self-check` with a retry. A stronger
model's review (or the porter's) should look at the classes for exact
boundaries (`==` against a bound, a value with only the sign bit or only
the high byte) and at callee results the C tests bit by bit (a `stubs`
entry), which is where both misses were.

## The port pipeline (after both bake-offs)

`pc/tools/port_pipeline.py` turns the two bake-offs into the bulk port's
loop: per function, the model drafts the spec (with mutations of the **C**,
`target = "c"`), then the port; each draft is gated by the checker, with one
retry fed only its sanitised errors; what passes is written into the tree;
a stronger reviewer works from `pipeline/REVIEW.md`.

```sh
python3 pc/tools/port_pipeline.py --unit 146C --difficulty S --limit 10 --dry-run   # plan + prompts, no calls
bash -ic 'python3 pc/tools/port_pipeline.py --unit 146C --difficulty S --budget 2'   # key from your profile
bash -ic 'python3 pc/tools/port_pipeline.py func_80069D14 _insertActor --budget 0.10'
python3 pc/tools/port_pipeline.py --unit 7CD68 --base-url http://127.0.0.1:8990/v1 --model M --thinking off \
    --temperature 0.6 --attempts 4 --jobs 3      # a local OpenAI-compatible server, best-of-4 ("Local Qwen vs Haiku")
```

- **Plan (deterministic, recorded):** Zig name from `MEANINGS.tsv`'s
  `zig_name`, else the decompilation's name without `vs_battle_`/`vs_main_`/`_`
  (`_insertActor` -> `insertActor`), else the original symbol kept
  (`func_80069D14`); module `game/battle/<snake_case>.zig`; spec
  `pc/tests/zig_diff/<zigName>.zig`; subsystem `battle_<unit>_port` (one per
  unit, default original); signature from the unit's translate-c prototype
  with struct pointers as views pointers and the c module's typedefs as Zig
  types (`u_short*` -> `[*c]c_ushort`; another unit name goes through `c.`);
  the views types and globals the body names (translate-c's `pub extern var`
  and `@extern` globals both), closed over the fields it reaches; a global
  without a generated views accessor stops the plan (the prompts show only
  what exists). Static functions, functions routed elsewhere and already
  ported ones are skipped.
- **Spec gate:** `zig_diff` against a stand-in port that is the C itself
  (`--standin`): 0 mismatches (the spec is deterministic and the C never
  faults on it), every class occurs, 3-6 C mutations all caught, and the
  coverage lane passes (every branch, no boundary/sign flag, or a waiver
  with a reason). The spec does not depend on the port, so a wrong port
  cannot bend it. A spec stub may name struct pointers by their views
  types (the harness casts to the c prototype's). The retry's feedback is
  meant to be acted on: a C crash comes with plain advice, its fault address
  and the iteration's arguments, named (`arena`, a global, `low_ram`,
  `NULL+0x68`), and nothing else (the mutations and coverage wait for a
  crash-free run); each coverage gap with what to add (a sign flag: values
  above the constant that are positive).
- **Port gate:** 0 mismatches on seeds 1-3 and the `/// orig:` line.
- **Writing:** the spec and its `specs.toml` entry, the port, the
  `ownership.toml` route and views, `root.zig`'s import and export, after the
  unit's existing specs pass with the widened views (else `views-conflict`,
  nothing written). Records: `pipeline/<unit>/<function>.json` (plan, tokens,
  cost, seconds, attempts, verdicts, sanitised errors), `pipeline/<unit>/summary.json`.
- **REVIEW.md** lists every function's classes, C mutations, argument ranges
  and comparison outcomes (the value edges), flagged first: a retry, a
  `ROOD_PC` hook, unit statics, a coverage waiver.
- **Privacy:** every message goes through `pc/tools/prompt_privacy.py` (the
  checkout, home, Zig cache and scratch paths and the user name replaced)
  and is refused if anything local is left (CTest `prompt_tools`).
- **Budget/resume:** `--budget` (default $5) checked before every call;
  replies are saved in `--work` (default `pc/build/port-pipeline`) with a key
  of the conversation they answered (model, prompt, feedback) and reused only
  for the same conversation (else set aside as `.staleN`; a reply placed by
  hand, without its `.json`, is always used); a function whose record says
  `ported` is skipped.

Smoke run (Haiku 5.5, 2026-10-20): `func_80069D14` (S, a 16-slot scan) and
`_insertActor` (S, a list insert with a dead inner `else`) both **ported**,
$0.031 in all (6 calls, 118k prompt + 37k completion tokens, 162 s of model
time, 214 s of checking). Both specs needed their retry: `func_80069D14`
declared a class its inputs never made; `_insertActor`'s first spec left the
dead `else` uncovered, and the retry waived it with the right reason (the
outer test already requires `id != 0`). Both ports passed first try. CTest
`port_pipeline` runs the whole pipeline offline on canned replies.

Pilot, unit 38C1C (17 functions, Haiku 5.5, 2026-10-20): first run 5
ported ($0.32). The failures were the pipeline's: typedef'd parameters
(`[*c]u_short`) the stub, harness and port could not name (3 specs); the
globals the C reads (`D_800F4538`, a table of actor pointers, translate-c's
`pub extern var`) missing from the views, so the prompts showed no
declaration and the model guessed a 32-bit `psx.Ptr` table (crashes, ports
that did not compile); spec stubs typed with views structs where the c
prototype has c ones (harness did not build); the PC C's NULL guards read
PS1 address 0, which the harness did not map; and the written tree did not
build (`route.exportAll` ran out of comptime branches at five routes). After
the fixes the 12 failed functions all ported on the rerun ($0.26, 3 spec
retries, no port retry): 17/17. CTest `port_pipeline` keeps canned
`func_800A1AF8` and `func_800A17BC` for these regressions.

### The pilot's review (Opus 5.5, 2026-10-21)

All 17 reviewed adversarially against the `.c`, the routed `.i` and (where
the C is undefined) the object: per-function verdicts in the records and
`pipeline/REVIEW.md`.

| | Functions | What |
|---|---|---|
| Port bugs | 1 | `func_800A1C10`: the matrix from `func_800A1DE8` cast to a non-optional pointer before the weapon test, so ReleaseSafe panicked ("cast causes pointer to be null") where the C returns -1: an actor whose weapon bone has no weapon model. A crash in owned mode, invisible to the corpus (no recipe reaches it) and to the spec (its stub never returned NULL) |
| Port faithfulness fixes | 1 | `func_800A1648`: the switch has no default; the port took case 2 for any other part and read the armature, with a comment saying GCC does so. The object returns its uninitialised result register without a read; the port now leaves `v` undefined as the C does (every caller passes 0) |
| Spec fixes (behaviour) | 4 (+ `func_800A1C10`) | the stubs modelled no callee effects, so "read before the call" ports passed: `func_800A1DE8` (`func_800B07DC` rotates the scratch offset, `func_80041C68` writes the product's t: both now written), `func_800A208C` (the flags word at +8 and `unkF` re-read after `func_800AE4FC` / `func_800A0204`: now changed by the stubs), `func_800A2574` (the slot fetched before two calls: a stub now swaps it; indices 0-16, not 0-3), and `func_800A1C10`'s NULL matrix above; one missing value edge: `func_800A25EC`'s -3000 sentinel is tested on the stored short (-3000 + 65536 now an input) |
| Harness fix | 1 | `zdiff.Ctx.value` gave one value per stub call, so a stub filling three fields filled them alike (`func_800A1C10`'s t[0..2]: a t-index swap slipped); each draw now differs |
| Mutations added only | 4 | `func_800A141C` (PC_GTE_UNNARROWED dropped), `func_800A152C` (nBones signed), `func_800A1B28`, `func_800A1B9C` (the second call given arg1); `func_800A1648` got one too (a signed nibble) |
| Sound as drafted | 7 | `func_800A1720`, `func_800A17BC`, `func_800A190C`, `func_800A1AF8`, `func_800A249C` (its `+39 eq` waiver holds), `func_800A2790`, `func_800A291C` |

13 mutations added (R1/R2 in `specs.toml`); the 7 that need the spec fixes
were each checked to slip the drafted spec and be caught by the fixed one
(`func_800A1C10`'s t-index swap slipped the old harness). Names: six
renamed (`projectBonePoint`, `findBone`, `boneViewPoint`, `boneViewMatrix`,
`startJump`, `turnToward`); left as symbols, with proposals not yet
confident: `func_800A1648` (a bone's limb code), `func_800A1720`/`func_800A17BC`
(bone view matrix and length), `func_800A190C` (an actor's anchor point by
selector), `func_800A1AF8`/`func_800A1B28`/`func_800A1B9C` (boneViewPoint by
index / high / low code), `func_800A2574`, `func_800A25EC`, `func_800A2790`,
`func_800A291C` (movement steps whose callees are unnamed).

The pattern: Haiku's specs reach every branch and the value edges the
coverage lane flags (it passed the gate everywhere), but treat callees as
pure recorders. What the lane cannot flag is a callee's effect the C reads
back (state re-read after a call, a slot fetched before it, a NULL a real
callee returns). The reviewer's first question per call: what does the
real callee change that the C reads afterwards, and what can it return?

Corpus (64-bit, classic profile; `differ.py battle_38C1C_port` on the three
implemented recipes): call mode (shadow) passes everywhere, 0 mismatches.
Reached as roots: `func_800A152C`, `func_800A1720`, `func_800A190C`,
`func_800A1AF8`, `func_800A1B9C`, `func_800A249C` (all three recipes),
`func_800A141C`, `func_800A1648` (enemy_kill); nested only:
`func_800A17BC`, `func_800A1C10`, `func_800A1DE8`; never: `func_800A1B28`,
`func_800A208C`, `func_800A2574`, `func_800A25EC`, `func_800A2790`,
`func_800A291C` (movement and jumps: no recipe moves an actor). Tick and
alternate modes, and `shadow_battle_smoke --owner battle_38C1C_port=shadow`
(battle_action's `_handleCombatCameraZoom` shadowed with 38C1C nested),
diverged in guest RAM 0x800F5248..0x800F5290 (it looked like unnamed
BATTLE bss after `D_800F5238`; it is inside `D_800F5230`). Not a port bug: owning only `func_800A1AF8` (a 3-line
forwarder) diverges there too, and the original side's bytes change from
build to build with the C unchanged (only the routing trampolines differ):
dead host stack. Closed (branch oracle-tick-gap): the bytes are
`D_800F5230` (the effect request, 0xE8 bytes) offsets 0x18..0x60, written
by `func_800CF0E8`'s whole-struct copy of `func_8007138C`'s local request,
of which only the used fields are written (reserved bytes, the union tail
of actor targets, the targets past `count`); `func_800CE714` then copies
`targets[0]` whole into each effect node. `func_8007138C` runs inside
`_handleCombatCameraZoom` after the routed 38C1C helpers, whose frames left
the residue. Under ROOD_PC the request is now zeroed first; tick, alternate
and call modes pass on all three recipes and `shadow_battle_smoke_38C1C`
(run.py) is the regression. See Oracle.md, known hazards.

Review cost (agent time, 17 functions): ~32 min reading and fixing the
specs and ports (zig_diff `--self-check --coverage` for all 17 in 3.5 min,
re-runs of single functions under a minute), ~5 min renames, ~20 min corpus
including the bisection of the tick divergence. Per function: ~1 min for a
forwarder (`func_800A1AF8`), 2-3 min for a sound mid-size one, 5-8 min for
one needing a spec or port fix; mean ~2 min of review plus ~1 min of
corpus per function once the oracle gap is closed. For the bulk port's
cost model: one fix in three functions (6/17 needed a spec behaviour or
port change), and the one port bug was in a path neither the spec nor the
corpus reached: budget the review at ~3 min per function, and expect most
of its value from asking what each callee changes.

`c_body` (bakeoff_context.py) used to run a one-line function
(`func_8006CE50`, `func_8007C088`) on into the next function's body; it now
ends at the matching brace, so those two bake-off prompts are shorter than the
sizes table above says.

## Local Qwen vs Haiku (unit 7CD68, 2026-10-21)

A fresh unit neither model had seen: `src/BATTLE/BATTLE.PRG/7CD68.c`, 12
functions (9 S, 3 M; movement-grid flood fills, range shapes, a callback
table). Same pipeline, same prompts (including the new callee question
below), each run with `--no-write` into its own scratch `--work`/`--records`:

- **Haiku 5.5** on OpenRouter: temperature 0, thinking default, one sample,
  one feedback retry, `--jobs 4`, budget $1.
- **Qwen 3.8 27B FP8** on the local vLLM server (48K context, prefix
  caching): thinking **off**, temperature 0.6, `--attempts 4` (best-of-4
  before the feedback retry, the four samples asked for at once), `--jobs 3
  --max-calls 9`. Then one rerun of its failures with thinking **on**
  (`--max-tokens 24000`, same sampling).

The endpoint switches (pc/tools/model_client.py, shared with spec_bakeoff):
`--base-url` (a loopback host is local: no key, no Authorization header,
cost 0, max_tokens clamped to what the server's `max_model_len` leaves
after the prompt, counted with its `/tokenize`), `--api-key-env`,
`--thinking on|off|default` (local: `chat_template_kwargs.enable_thinking`;
OpenRouter: `reasoning.enabled`), `--temperature`; `--attempts N`
(best-of-N), `--jobs J` (functions in parallel; checker builds/runs bounded
by `--check-jobs`, the process at nice 10; tree writes one at a time, each
re-checked against the views earlier writes widened). Every call is
recorded: model, endpoint kind, tokens, seconds, cost, round and sample;
records and REVIEW.md say which model drafted a function and how.

```sh
bash -ic 'python3 pc/tools/port_pipeline.py --unit 7CD68 --budget 1 --jobs 4'
python3 pc/tools/port_pipeline.py --unit 7CD68 --base-url http://127.0.0.1:8990/v1 --model qwen3.8-27b-port \
    --thinking off --temperature 0.6 --attempts 4 --jobs 3 --max-calls 9
```

### Result

Gate outcome per step: `first` (the first sample passed), `best-of-N` (a
later sample), `retry` (after the feedback retry), `failed`. Haiku's columns
are after the tooling fixes below (its first run, before them: 6/12).
Tokens are prompt + completion over every call of the function's final
conversation; model time is the sum of call times.

| Function | | Haiku spec / port | Haiku tokens, model s, $ | Qwen off spec / port | Qwen off tokens, model s | Qwen think spec |
|---|---|---|---|---|---|---|
| `func_800E5568` | M | first / first | 46k+8k, 39, 0.009 | failed | 44k+6k, 117 | failed |
| `func_800E5600` | S | first / first | 47k+14k, 65, 0.012 | failed | 48k+10k, 178 | failed |
| `func_800E5698` | S | retry / **failed** | 99k+15k, 71, 0.018 | failed | 49k+8k, 124 | failed |
| `func_800E5710` | S | retry / **failed** | 107k+42k, 180, 0.032 | failed | 55k+11k, 158 | failed |
| `func_800E5998` | S | first / first | 49k+23k, 105, 0.017 | failed | 50k+8k, 118 | failed |
| `func_800E5A74` | S | retry / first | 57k+7k, 35, 0.010 | retry / first | 142k+6k, 137 | (passed off) |
| `func_800E5A9C` | M | **failed** | 34k+26k, 116, 0.017 | failed | 65k+12k, 219 | failed |
| `func_800E5EC0` | S | first / first | 46k+19k, 88, 0.014 | failed | 45k+13k, 174 | failed |
| `func_800E5FDC` | S | first / first | 46k+20k, 89, 0.015 | failed | 44k+8k, 120 | failed |
| `func_800E6098` | S | first / first | 45k+12k, 53, 0.011 | failed | 44k+14k, 171 | failed |
| `func_800E6158` | S | first / first | 45k+2k, 13, 0.006 | failed | 40k+5k, 102 | first / first |
| `func_800E6178` | M | **failed** | 39k+60k, 258, 0.034 | failed | 72k+16k, 202 | failed |

| | Haiku 5.5 | Qwen 27B, thinking off, best-of-4 | Qwen 27B, thinking on (11 failures) |
|---|---|---|---|
| Ported (both gates) | **8/12** (6 first try for both steps) | 1/12 | 1/11 |
| Spec gate passed | 10/12 | 1/12 | 1/11 |
| Port gate passed (of specs passed) | 8/10 (all first try) | 1/1 | 1/1 |
| Calls (all runs) | 40 | 89 | 58 |
| Tokens (all runs, in + out) | 884k + 390k | 972k + 196k | 632k + 1287k |
| Model time (sum of calls) | 1694 s | 2719 s | 16763 s |
| Wall | 1041 s first run (`--jobs 4`; 13 min of it `func_800E6098`'s checks at 30000 iterations), +820 s rerun | 318 s first run, 549 s rerun | 2424 s |
| Cost | $0.28 (first run $0.19) | $0 | $0 |

Why the drafts failed (every judged draft, final runs):

| | Haiku | Qwen off | Qwen think |
|---|---|---|---|
| Spec does not compile | 4 of 16 | **54 of 61** | 4 of 50 |
| Spec reply truncated (24K tokens, all of it reasoning) | 0 | 0 | **44 of 50** |
| No/bad fenced block | 0 | 0 | 2 |
| Spec runs but wrong (C crashes, prepare panics, coverage, classes, mutations) | 2 | 5 | 0 |
| Port does not compile / differs | 3 / 1 | 0 / 0 | 0 / 0 |

**Model failures.** Qwen with thinking off writes Zig that does not
compile: Zig 0.17 idioms it does not know (`@bitCast`/`@ptrCast` need a
result type, `for (a..b) |i|` captures, no `?:`/`let`/`for (i := 0; ...)`,
`packed` as a parameter name, unused locals/parameters are errors, `1 <<
x` with a runtime `x`), stub types that are not the prototype's (`u32` for
`c_int`), `ctx.chance` inside stubs. Best-of-4 rarely helps: the four
samples make the same kinds of mistakes, and only `func_800E5A74` got a
passing spec (after the retry). With thinking on it reasons past 24K tokens
on 44 of 50 drafts (the 48K context leaves no room for more after a ~10K
prompt), so almost nothing comes out; the one function it passed is the
one-line `func_800E6158`. Haiku's four failures: `func_800E5698` and
`func_800E5710` passed the spec gate and failed the port with type slips
(`c_int` vs `c_uint` at a call, then a null unwrap); `func_800E5A9C` (a
callback-table dispatcher) and `func_800E6178` (111 lines, four callees, a
`while (func_800DEC88(0))` loop) failed the spec: misaligned objects, a C
crash on its inputs, a `c.` struct translate-c left opaque, one
uncovered loop branch.

**Tooling failures, fixed (and covered in CTest `port_pipeline`):**

- `func_800E5A9C`'s harness did not build for either model: `D_800F1790`, a
  function-pointer table declared `extern T x[];` here and defined in
  `battle_movement.o`, had no size (no DWARF, not in imports.ld); zig_diff now
  takes the size from the object that defines it (`nm -S`).
- `u_short (*D_800F58B8)[32]` (the movement grids, rows of 32) was
  `?*anyopaque` in the views, so the models guessed a table of row pointers
  (and wrote past the global): a pointer to a sized integer array is now
  `?[*][32]u16` (zig_views), and the prompt's subset keeps its
  `psx.global` accessor (it used to turn any `[*]` into `globalMany`).
  Haiku's `func_800E5EC0`/`func_800E5FDC` specs passed on the rerun.
- A crash in the spec's own `prepare` was reported as "the C itself
  crashed"; it is now the spec's-own-code message.
- A spec stub returning `u32` where the prototype has `c_int` failed
  inside the harness's `main.zig` with a bare type error; it now says to
  declare the stub with the prototype's types.
- `func_800E5EC0` calls itself: in the harness (as in the game) that call
  goes through the routed symbol, a recording stub, so a port recursing
  into its own Zig function differs. The port prompt now says so for a
  self-recursive function (the port calls `c.FUNCTION`); Haiku's port
  then passed first try.
- Writing the ports, the 64-bit build failed: a routed function the unit
  has no prototype of exists in the c module only as `FUNCTION__orig`, so
  `c.func_800E5568` (a sibling's call) did not resolve. zig_c now declares
  the routed name too.
- `func_800E6098` scans the whole 32x32 grid per iteration: its drafted
  30000 iterations took 13 minutes of gate time; its entry now runs 2000
  (every class still ~500 times).

The new spec-prompt question (from the 38C1C review: callees were treated
as pure recorders) asks, per callee, what it can change that the C reads
back and what it can return, with a short stub example. On 7CD68 Haiku's
stubs write back where the C re-reads (e.g. `func_800E6098`'s
`func_800E5FDC` stub returns negative results and the scan reads cells
the stub of `func_800E5EC0` changes); whether it closes the gap the 38C1C
review found is for the review of these 8.

**Kept:** Haiku's 8 ported functions (routed as `battle_7CD68_port`,
default original; queued in `pipeline/REVIEW.md`, not reviewed). The one
function both models passed, `func_800E5A74`: each spec catches all of the
other's mutations (Haiku's 3/3 of Qwen's, Qwen's 5/5 of Haiku's), so
Haiku's (five mutations, three classes) is kept.

**Recommendation.** Haiku 5.5 for the bulk port, as run here: temperature
0, thinking default, one sample plus the feedback retry, `--jobs 4`
(~$0.02 per function, 8/12 on a fresh unit after the tooling fixes, the
four failures all model mistakes a second retry or the reviewer can take).
Qwen 3.8 27B is not usable for drafting specs in this pipeline: with
thinking off its Zig does not compile, with thinking on it does not finish
in the context the server has. If the local model is to earn a place, the
cheapest thing to try is a compile-fix loop (feed back only zig's errors,
several cheap rounds, no gate run) rather than more samples; server-side,
see the suggestions in the agent's report (not applied).
