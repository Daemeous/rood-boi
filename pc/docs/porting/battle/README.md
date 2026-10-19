# Battle: the inventory for the bulk port

`FUNCTIONS.tsv` and `STATE.tsv` describe what phase D ports first: the
battle subsystem, with the rules in `../PORTING.md`.

## Scope

**In scope (D's first pass):**
- `src/BATTLE/BATTLE.PRG/*.c`: 23 units, 1693 C functions, 49 096 lines,
  including the `ROOD_PC` C that replaces PS1 assembly (`INCLUDE_ASM` and
  `__asm__` blocks).
- The host C compiled into those units: `pc/src/charge_skip.inc`,
  `scene_submit.inc`, `actor_commands.inc`, `room_shadows.inc`,
  `room_callbacks.inc` (41 functions) and the generated
  `event_callbacks.inc`/`effect_callbacks.inc` (43 small wrappers).

**D2 (after battle flips; rows marked "D2"):**
- The 184 functions that are MIPS assembly on the PS1
  (`src/BATTLE/BATTLE.PRG/*.s`, and global `__asm__` in `4A0A8.c`, `573B8.c`,
  `5BF94.c`). On the PC 161 of them are port-written C in `pc/src`
  (`battle_movement.c`, `battle_helpers.c`, `battle_motion.c`, `battle_gte.c`,
  `actor_*.c`, `room_*.c`, `navigation.c`, `weapon_packets.c`, `map_view.c`,
  `effect_particles.c`, and SDK-level ones in `boot_sdk.c`): that C is their
  oracle, and the first pass calls them through `c.*`. 23 have no C anywhere
  and nothing on the PC calls them (rows say "calls trap"). State only these
  functions touch is marked "D2 only" in `STATE.tsv`.
- `src/BATTLE/INITBTL.PRG` (the battle initialisation overlay, 3 units; it
  shares state with BATTLE, e.g. it allocates `D_800F19CC`).

**Boundary (stays C during D; called through `c.*`):**
- The 82 functions of `src/SLUS_010.40/main.c` that battle calls (rows with
  kind "SLUS boundary"). 35 of them are called by battle alone: candidates
  to move with battle later (the sound and CLUT helpers, `vs_main_getRandSmoothed`,
  `pc_cd_queue_load`).
- The SDK layer (`pc/src/boot_sdk.c`, libgpu, PsyCross libgte and the
  software GTE), the sound driver (`25AC.c`), the other overlays battle calls
  (INITBTL, MAINMENU, MENU0-F, EFFPURGE, SCREFF2, the PLG effects), and the
  host features in `pc/src` (`callees_outside` names them per function).


## Files

| File | What |
|---|---|
| `FUNCTIONS.tsv` | one row per function in scope (2043 rows: 1961 battle, 82 SLUS boundary) |
| `STATE.tsv` | one row per global or static any in-scope function (or a battle-only SLUS function) reads, writes or takes the address of (701 rows) |
| `MEANINGS.tsv` | the hand-written meanings and value lifetimes `STATE.tsv` takes (symbol, meaning, value_lifetime): edit here, then regenerate |

## How they were made

`pc/tools/porting_tables.py` writes both tables; it builds and runs nothing:

```
python3 pc/tools/porting_tables.py --tree pc/build/boot64 [--tree32 pc/build/boot] [--work DIR] [-j 4]
```

`--tree` is a 64-bit build tree made by `build_boot.py --arch 64` with
BATTLE (it reads the `.i`/`.o` files, the reference ELFs, `imports.ld`,
`lp64_imports.json`, `native_imports.c`, `function_tables.c`); `--tree32`
adds PS1 sizes from the 32-bit objects' DWARF. Needs clang-18, gdb and
binutils; intermediate results are cached in `--work` (default
`<tree>/porting`). The committed tables come from the 64-bit build of
2026-10-18 (sources as of `1100a9c0`). The analysis:
- clang 18's JSON AST of each unit's preprocessed `.i` (the text GCC
  compiled: `ROOD_PC` selected, macros and `.inc` files expanded): functions,
  their source ranges, calls, global accesses (read, written,
  read-modify-written, address taken, read or written through the pointer),
  bitfield accesses, `goto`, `switch`, divisions, guest-address literals;
- `nm` of every object of the build (callers outside battle, who defines a
  callee, which asm functions `pc/src` implements);
- the objects' DWARF through gdb (types and sizes on 64 bits; PS1 sizes from
  the reference ELFs or the 32-bit objects), the build's
  `lp64_imports.json` (which imports move on 64-bit, their declared types per
  unit) and `imports.ld` (which data is linked to a guest address).

Limits: indirect calls are counted, not resolved; "through pointer"
attributes an access to the global at the root of the expression
(`D_800F19CC->unk854[i].x = ...` is a write through `D_800F19CC`); line
counts are the function's source range (for asm rows, the PC C's range, or
instructions where there is none).

## Conventions

- Lists are `N: a, b, c (+k)`: the count, then the first names.
- `STATE.tsv` `meaning`: written by hand for the 103 globals that matter most
  (the most-used, the action and charging-skip state, the dispatch tables,
  the host settings); the rest say `unreviewed:` and give the hints the
  analysis has (a descriptive name, named fields, descriptive users).
  "(inferred)" marks a hand-written meaning read from use only.
- `lifetime`: the storage's lifetime (certain: `static`, `per-overlay-load`,
  `per-frame (scratchpad)`, `host`), then, where reviewed, the value's
  (`per-room`, `per-battle`, `per-action`, `per-frame`, `per-event`...).
- `zig_plan`: what D does (`D: ...`) and what phase E does with it (`E: ...`).
- `FUNCTIONS.tsv` `difficulty`: S/M/L/XL from size and PS1 features (GTE,
  inline asm, function pointers, `goto`, guest-address literals, `ROOD_PC`
  blocks, local statics). A guide for ordering the work, not an estimate.
- `trial`: the three functions proposed for the trial (plan step D.2).

## The trial (plan step D.2)

| Function | Unit | Lines | Why |
|---|---|---|---|
| `_updateEnemyKills` | `146C.c` | 34 | struct-heavy: a `psx_ptr` field, `u_int : 3` bitfields used as indices, `char`/`u_short`/`u_int` mixing with an unsigned accumulator and a clamp, signed divisions, `1 << (n % 32)` into an `int` array; writes `vs_main_scoredata` (save data shared with the menus) |
| `func_800B07DC` | `44F14.c` | 22 | GTE and fixed point: a scratchpad block at a fixed address, `gte_SetRotMatrix`/`SetTransMatrix`/`ldv0`/`rtv0tr2`/`stlvnl`, a libgte call (`RotMatrixYXZ_gte`), 32-bit translation results narrowed into a `short` vector and `+=` on `short`s |
| `vs_battle_displaySceneMessage` | `5BF94.c` | 35 | a `ROOD_PC` hook (the charging skip's `pc_charge_skip_name(1)`, in `charge_skip.inc`), a heap allocation, `psx_set` of a string pointer, game text through the string tables, a bitfield store into a text box, signed index arithmetic |

All three are called on the PC (1, 3 and 7 callers), are not `static` (so
they can be routed alone), and come from three units (so the trial also
proves per-unit `c` and `views`).
