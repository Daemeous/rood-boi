# Review notes: PORTING.md and the battle tables

Two independent adversarial reviews of the first draft (commit `8dc27602`):
**A** on C-to-Zig semantics and **B** on architecture and the state table.
Both were asked to hand in early, so both are partial. A's own Zig probes
were refused by the sandbox; the claims below marked "checked" were then
probed on Zig 0.17 by the author, the rest were checked by reading the code.
Decisions taken by the coordinator are under "Decisions".

## Decisions

1. **Overflow wraps by default** (`+%`, `-%`, `*%`): the PS1 code is
   `addu`/`subu` and the PC C is `-fwrapv`. A trapping operator needs a
   one-line reason. Other ReleaseSafe checks stay. (§4.4)
2. **INITBTL** is out of D's first pass: the D2 pass.
3. **The `pc/src` asm ports** (`battle_movement.c`, `battle_helpers.c`,
   `actor_*.c`, `room_*.c`, ...) are out of the first pass: the D2 pass, after
   battle flips. They stay the oracle for the asm functions until then.
4. **"Delete the hooks at the flip"** means the feature hooks only; safety
   guards, asm replacements and LP64 adaptations stay in `src/`; every feature
   gets a setting that turns it off, and the differential runner pins them all
   off. (§12)
5. **One routing subsystem per unit**, for bisecting. (§20)
6. The analysis is a committed tool, `pc/tools/porting_tables.py`, with the
   hand-written meanings in `battle/MEANINGS.tsv`. (`battle/README.md`)

## Found and fixed

| # | From | Severity | Finding | Fix |
|---|---|---|---|---|
| 1 | A | wrong behaviour | §11.3 said `break` in a `switch` in a loop refers to the loop; in C it leaves the switch, and a Zig `break` in a prong leaves the loop | §11.1/§11.3 rewritten |
| 2 | A | wrong behaviour | `do {} while` mapped to `while (true) { ...; if (!c) break; }` skips the test on `continue` | labeled body block, `continue` → `break :body` |
| 3 | A | wrong behaviour (checked) | unary `~`/`-` on a `u8` stay 8-bit in Zig; C promotes (`f &= ~m` clears `f`'s high byte in Zig) | §4.1: widen first |
| 4 | A | wrong behaviour (checked) | `u == -1` with `u: u32` compiles and is always false in Zig | §4.1, §11.1 case labels |
| 5 | A | compile error (checked) | `n % 32` with `n: c_int` is rejected; the guide's snippet only works for unsigned `n` | §4.5 |
| 6 | A | compile error (checked) | `psx.at` rejects a `*T` base (`std.meta.Elem`) | §20 item 4: fix `at` |
| 7 | A | compile error / trap | `psx.at`'s `isize` index (u32 fails on 32-bit), trapping `+`/`*` on negative guest addresses, lost `align(1)` | §20 item 4 |
| 8 | A | compile error | §5.4 `p + k` needs `usize`; `@ptrCast` of an `align(1)` pointer to `[*]T` | §5.4 table |
| 9 | A | compile error | §4.2 example `@as(i16, h)` fails for unsigned `h`; `conv` takes no literals | §4.2 |
| 10 | A | gap | enums not covered (GCC: `unsigned int` unless negative enumerators) | §3 row |
| 11 | A | gap | bitfield sign (`i5`), compound assignment, which pointer to pass | §7.3 |
| 12 | A | gap | `sizeof` products: 32- and 64-bit oracles differ if shifted/compared before truncation | §4.6 |
| 13 | A | gap | x87 excess precision; `@intFromFloat` out of range | §4.9 |
| 14 | A | gap | `u_int += int` not shown | §4.3 |
| 15 | A | gap | i386 struct returns through a hidden pointer (`ret $4`) | §13, §20 item 5 probe |
| 16 | A | gap | `int` base to pointer must zero-extend (via `u32`) | §5.5 |
| 17 | A | nit | scratch example used a non-views type; `@divExact` trap; widen-then-operate | §5.7, §5.4, §4.4 |
| 18 | A | gap | guest pointers to structs aligned above 4 trap in `@ptrFromInt` | §20 item 1: views emit `align(4)` |
| 19 | B | table error | function-local statics named without their object symbol; 3 have no symbol (folded) | tool: symbol (`sp20.1`, ...) or "folded"; §6.3/§6.5 counts |
| 20 | B | wrong behaviour | `const` statics are in `.rodata`: not restarted, not compared | tool marks them; §6.3/§6.4 |
| 21 | B | system | log-once latches and charging-skip statics are compared state | §14 |
| 22 | B | ambiguity | bss inside `BATTLE.PRG`? | checked: the file is 0x8D124 bytes; §6.4 |
| 23 | B | table error | the 94 LP64-moved rows said "stays at its guest address" and "guest RAM" | tool: `defined_in` names the `rood_lp64_*` region; zig_plan fixed |
| 24 | B | wrong behaviour risk | integer aliases inside moved regions read the low half of an 8-byte slot | tool lists names per region; §6.2 rule |
| 25 | B | ambiguity | `.inc` and generated wrappers' routability | §13, FUNCTIONS kind |
| 26 | B | ambiguity | inlining/local resolution of routed functions | §13 + §20 build check |
| 27 | B | design gap | feature hooks run three times in shadow mode | §14, §20 oracle item 7 |
| 28 | B | ambiguity | removing a hook that touches game state (charging-skip reseed) | §12 |
| 29 | B | criteria | trial functions need corpus paths | §20 oracle item 8 |
| 30 | author | wrong behaviour (checked) | `@extern(?F, .linkage = .weak)` null test folded by Zig 0.17: absent hook crashes | `psx.weak` (laundered), §12 |
| 31 | author | gap | `route.zig` stops at 4 parameters | §20 oracle item 0 |

Not changed:
- B: `D_80069998` ("00:00:00", `5BF94.c:3778`) is missing from `STATE.tsv`:
  it is defined and referenced by nothing, so no function reads or writes it.
- B item 12 (undefined data of the asm-port objects not fully enumerated):
  those objects are D2; rerun the tool's state pass over them before D2.
- A item 21 (the `sprintf` example text): checked, it is not inside a
  `#pragma vsstring` block (those are only in `5BF94.c`).

## Not reviewed (open)

- A did not check the 25 STATE and 15 FUNCTIONS rows it was asked to, nor
  port `func_800B07DC` and `vs_battle_displaySceneMessage` mentally; B did not
  do the 20-row reader/writer spot check. The trial's reviewers should.
- About 600 `STATE.tsv` meanings say `unreviewed:`.
- COFF behaviour of `psx.weak`; i386 struct returns; the views generator's
  handling of nested bitfields: to check in the trial.
