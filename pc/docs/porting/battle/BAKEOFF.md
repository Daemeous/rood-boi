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
