# Review: 75 pipeline-written Zig ports of `4A0A8.c` (2026-11-03)

Scope: the 75 functions in the review request, all from
`src/BATTLE/BATTLE.PRG/4A0A8.c`, ported by the haiku pipeline into
`game/battle/<module>.zig`. Method: each port read beside its C body against
the PORTING.md section 19 checklist, with the callee prototypes, the
`views_4A0A8` manifest (`game/ownership.toml`, `battle_4A0A8_port.views`) and
the zig_diff spec classes. Nothing was built or run (the cloud cannot build
this project); the preprocessed `.i` and the objects were not available, so
every claim about "what GCC emits" below is unverified and says so.

Nothing in the repo contained instructions aimed at the reviewer.

## Summary

- 75 ports reviewed: **65 clean, 10 suspect**. No high-confidence behavioural
  defect. Nothing found that random testing would miss and that changes game
  state with the inputs the game actually produces.
- The suspects are: two calls through a mistyped function-pointer cast
  (arguments not converted as C converts them), one forced value for an
  uninitialised local, two unverified claims about the object's behaviour,
  uninitialised stack replaced by zero in two functions before a callee
  copies the whole struct into game state (unread fields), and three section 16 / section 4.4
  rule breaks (hand offset where a view field exists, private static ports
  without `/// orig:`, an uncommented plain `+`).

| Function | Verdict | Confidence |
|---|---|---|
| func_800B6D48 | suspect (F1, F2) | medium |
| func_800B91F0 | suspect (F1) | medium |
| func_800B9C58 | suspect (F4, spec gap) | low |
| func_800BD444 | suspect (F5) | low |
| func_800BD6C4 | suspect (F6) | low |
| func_800BDAB4 | suspect (F6) | low |
| func_800B7358 | suspect (F7) | low |
| func_800BE53C | suspect (F8) | low |
| _vectorFixedPointMult | suspect (F8) | low |
| func_800BE5A4 | suspect (F9, nit) | low |
| all others (65) | clean | see notes |

Clean, one line each (checked against the C; any note is not a defect):

- _atan2FixedPoint: clean (`>> 6` arithmetic on both args, argument order kept).
- func_800B66F4: clean (null test and `short` compare against 4 as in the C).
- func_800B71C4: clean.
- func_800B7490: clean (u16 truncations, `flag + 1/+2` converted to short, `actor == 1` after the u16 store).
- func_800B7574: clean (call order getShort(+3), getShort(+1), func_800BFE50 is the right-to-left argument order; verified only by the diff, not by an object).
- func_800B76A8: clean (one-parameter signature matches the C).
- func_800B792C: clean (`char v = 0x10` is unsigned under `-funsigned-char`, pad store and the `0xFF` / `-1` ternaries right).
- func_800B7A98: clean (bytes 4..15 of `sp10` stay uninitialised in the C; port leaves them `undefined`).
- func_800B7EF0: clean (zeroed `sp10` where the C is uninitialised: masked, not a state difference).
- func_800B7F84: clean (same note; `& 0xFFFF` and index range right).
- func_800B804C: clean (setVector comma-operator order kept).
- func_800B822C: clean (one-parameter signature).
- func_800B86E8: clean.
- func_800B9BF8: clean (no parameters, as the C).
- func_800B9F4C: clean.
- func_800BA218: clean (right-to-left getShort order, verified only by the diff).
- func_800BA35C: clean.
- func_800BA8BC: clean (right-to-left order as above).
- func_800BA94C: clean (all three arms and the latch).
- func_800BAB14: clean (`D_800F4BC0`, `D_800F4C38` are unsized so many-item, no bounds trap on a0 >= 8; `void*` + 8 byte arithmetic kept).
- func_800BAD78: clean.
- func_800BADE0: clean (argument order from the right).
- func_800BAE74: clean (needlessly convoluted pointer add, same value).
- func_800BB33C, func_800BB4F0, func_800BB5A4, func_800BB5AC: clean (`_evtFile` / `D_800F4C24` re-read after the store and the alloc, as the C).
- func_800BB41C: clean (the C may load `arg0[1]` twice; nothing can change it between).
- func_800BB554: clean (store before the getShort, `psx.addr` 4 GB check, negative offset wraps).
- func_800BB604: clean.
- func_800BB788: clean (index is the pre-increment `unk1`, may pass the 22-byte array, `psx.at`).
- func_800BB874: clean (LP64 branch ported through u32 scratch slots; `setSemiTrans` is `code |= 2`, `AddPrim` before the cursor advance, `D_8005DFDA` re-read each iteration).
- func_800BB9B0: clean (`var_s1` taken from `D_800EB9BC` at entry and stepped by the struct size, null re-tested at each store, as the C).
- func_800BBAFC: clean (`++unk2` wraps as u8 before the shift; divisor non-zero by the guard).
- func_800BBBE8: clean (every store and call of the C present, in order; the `a = b = 0` chains differ in store order only).
- func_800BBDDC, func_800BBE04, func_800BBE10, func_800BBE94: clean.
- func_800BD2B8: clean (sign-extend of the low 15 bits via `<< 17 >> 17`, `short lowest` compares).
- func_800BD57C: clean.
- func_800BD610: clean (`char temp_a2` is unsigned on the PC, as ported).
- func_800BDBB4, func_800BDF6C: clean (scratchpad `0x1F800088` alias kept, reloaded after each store).
- func_800BE01C: clean (default arm leaves the scratch word alone; divisions guarded by the `unk9 == 0` return).
- func_800BE180: clean (`func_800BDF6C_t` stride 12 from `&unk1A4.unk2`, `c` not cleared, `unkA >> 6` on a u8).
- func_800BE36C, func_800BE3A0: clean.
- func_800BEB00, func_800BEB10, func_800BEB34, func_800BEB9C, func_800BEBEC, func_800BEC30: clean (the redundant branch of `func_800BEC30` is kept).
- _setVectorImm, _vectorAdd, _vectorSubtract, _vecToRotMatrix: clean (`-32768` squared twice wraps with `*%`).
- vs_battle_getStateFlag, vs_battle_setStateFlag: clean (negative short index sign-extends).
- vs_battle_script_buttonPressed: clean (`(pressed & mask) != 0` parses right in Zig: `&` binds tighter than `!=`).
- vs_battle_script_loadSoundFile, vs_battle_script_playMenuSelectSfx (`VS_SFX_MENUSELECT` is 5), vs_battle_script_setFlagIfButton, vs_battle_script_setFlagImmContext: clean.

`/// orig:` lines are present in all 75 ports. `ownership.toml` `zig =` paths were checked by script for the 13
renamed ports and by hand for func_800B66F4 and func_800B6D48/func_800B7358; the remaining `func_*` entries were not
individually checked. The "uninitialised sp10" zero-fills elsewhere are the section 11.4 sanctioned treatment.

## Findings, most severe first

### F1. Call to `func_800BFE50` through a mistyped cast (function-pointer cast hides the u_short conversion)

- Functions: func_800B6D48, func_800B91F0. Confidence: medium that it is a deviation, low that it changes behaviour today.
- Port: `game/battle/func_800B6D48.zig:14` (`getFn`, passes `@as(c_int, actorIndex)`) and `game/battle/func_800B91F0.zig:8` (casts to `fn (c_short) c_int`, and also casts `func_8009DBDC`).
- C: `4A0A8.c:807` (`func_800BB7C4(func_800BFE50(vs_battle_getShort(script + 2)), &origin)`), `4A0A8.c:1792`.
- Checklist: 1 (same arguments), 2 (implicit conversion), section 10.2 / 18 (casts that change a call).
- Differs: `func_800BFE50` is declared `int func_800BFE50(u_short)` in `573B8.h`, which `4A0A8.c` includes; the other ports call `c.func_800BFE50(psx.conv(u16, ...))`. These two cast the pointer to a `c_int` / `c_short` parameter, so a negative script short is passed sign-extended (`0xFFFF8001`) where C passes the converted `u_short` (`0x8001`). The comment in func_800B91F0 ("its prototype is not in the unit's declarations") is false, and `func_8009DBDC(int, int)` needs no cast either.
- Input: a script whose short at offset 2 (func_800B6D48) or offset 1 (func_800B91F0) is `0x8001`.
- Why it is probably invisible today: the real callee reads `arg0 >> 12` of a `u_short` (GCC re-extends the low 16 bits), and the harness stubs read the declared 16-bit type, so zig_diff cannot see it. It would show for a callee that trusts the upper bits, or if the port is routed ahead of a C callee built differently.
- Fix: call `c.func_800BFE50(psx.conv(u16, ...))` and `c.func_8009DBDC(...)` directly.

### F2. `func_800B6D48` stores 0 for the uninitialised `texture`

- Port: `game/battle/func_800B6D48.zig:67`. C: `4A0A8.c:836-838` (`int texture;` never set; `box->unk0.unk0_24 = texture`). Confidence: low-medium.
- Checklist: PORTING 1 and 11.4 ("read the object and port what it does").
- Differs: the port always clears the 6-bit `unk0_24` field. GCC treats the read of an uninitialised local as an undefined value and may emit no store at all (field keeps its old bits), or store a register or stack residue. If the object does not store, the port clears bits the original leaves alone. The comment says the value "is not reproducible" but does not say the object was read.
- Input: any call where the text box's `unk0_24` is nonzero before the call (e.g. a reused box slot).
- Action: `objdump -d` the `func_800B6D48` object (32-bit and 64-bit) and either drop the store or port the register the object uses. This is the original C's bug; keeping it is correct, the open question is only what it compiles to.

### F4. `func_800B9C58`: shift count of 32 or more is not exercised and the assumption is unverified

- Port: `game/battle/func_800B9C58.zig:20` and the two sibling arms (`(1 << n) & 0x1F` as `1 << (n & 31)`). C: `4A0A8.c:2139, 2147, 2153`. Confidence: low.
- Checklist: 3 (shifts), 11 (spec coverage).
- Differs: `arg0[4]` is a full byte, so `1 << arg0[4]` is undefined for 32..255. The port assumes the object shifts by `n & 31` (x86 `shl %cl`). GCC could instead compile `(1 << n) & 0x1F` as a compare-and-select, giving 0 for every `n >= 5`. The spec pins `p[4]` to `0..31` (`ctx.range(u8, 0, 31)`), so the case is never tried.
- Input: `arg0[4] = 32` (port: `1`, a compare-and-select object: `0`). The game's scripts presumably never use it.
- Action: widen the spec to the full byte once an object is at hand.

### F5. `func_800BD444`: claim about GCC's "no default" arm is unchecked

- Port: `game/battle/func_800BD444.zig:7`. C: `4A0A8.c:3662-3669` (`switch (arg0[0])` with only `0xE2` and `0xE3`, `var_s0` otherwise uninitialised). Confidence: low.
- Differs: the port says GCC's object takes the `0xE3` arm for every other opcode; this is not backed by an object read here, and the spec only ever feeds `0xE2` / `0xE3`. Unreachable through the opcode table, so no game effect.

### F6. `func_800BD6C4`, `func_800BDAB4`: zero in place of uninitialised stack, copied wholesale into game state

- Port: `game/battle/func_800BD6C4.zig:10-11`, `game/battle/func_800BDAB4.zig:7-8`. C: `4A0A8.c:3750-3752`, `3921-3923`. Confidence: low (no behavioural effect found).
- Differs: `func_8007DDF8(&values)` does `D_800F1A68 = *arg0` and `func_8007DDD4(&color)` does `D_800F1A78 = *arg0` (`146C.c:9678, 9680`): the whole 16-byte / 4-byte struct is stored. The C writes only `unk0`/`unk4` (and `r0`/`g0`/`b0`), so `D_800F1A68.unk8/unkC` and `D_800F1A78.code` receive stack residue in the original and zero in the port. Only `unk0`, `unk4`, `r0`, `g0`, `b0` are read anywhere else (`146C.c:9505-9615`; `unk8` is initialised to 0 at `146C.c:9488`), so the difference is not read. The port's comment ("only partly written before a callee fills them") is wrong for these paths: nothing fills them, the callee copies them.
- Also: `func_800BD6C4` returns 0 for an opcode outside the eight cases where the C falls off the end of a non-void function (unspecified `%eax`); unreachable via the table.

### F7. `func_800B7358`: hand-written field offset where the view has the field

- Port: `game/battle/func_800B7358.zig:9-11`. C: `4A0A8.c:945-949`. Confidence: low (value correct).
- Differs: the comment says `vs_battle_textBox` is not in `views_4A0A8`; it is (listed in `battle_4A0A8_port.views.types`, and `func_800B6D48` and `dismiss_text_box.zig` use `box.unk22`). The port computes `box + 0x22` by hand. The offset is right (`unk22` is a `char` at 0x22, checked by hand from `5BF94.h`), and the write is one byte as in the C. Checklist 5 / section 2: "if a type is missing from the views, add it", not "write the offset".

### F8. Private ports of statics lack the `/// orig:` line (section 16)

- `vectorMagnitude` in `game/battle/func_800BE53C.zig` (C `_vectorMagnitude`, `4A0A8.c:4190`) and `fixedPointMult` in `game/battle/vector_fixed_point_mult.zig` (C `_fixedPointMult`, `4A0A8.c:4224`). Body checked against the C, including the PC branch `(int)(((long long)a * b) >> 12)`: faithful. Only the mandatory `/// orig: <symbol> (<unit>)` line is missing (the `copyVector` in `func_800BBBE8.zig` has it). Confidence: certain, severity: naming-rule only.

### F9. `func_800BE5A4`: plain `+` without the section 4.4 comment

- Port: `game/battle/func_800BE5A4.zig` (`@as(c_int, a.duration) + @as(c_int, b.duration)`). Cannot overflow (two widened u8s); only the one-line reason is missing. Nit.

## Original-C bugs the port keeps (correct behaviour, noted only)

- `func_800B6D48`: `texture` read uninitialised (see F2); `func_800B6B98` leaves `result->distance` unset when it returns 1 (the caller only reads it on the fallback path).
- `func_800BD444`: `var_s0` uninitialised for opcodes other than `0xE2`/`0xE3`. `func_800BD6C4`: falls off the end for unknown opcodes.
- `func_800B7574`: `sp10[4..11]` never initialised; `func_800B7A98`: `sp10[4..15]`; `func_800B7EF0` / `func_800B7F84`: the unwritten parts of `sp10`; `func_800B792C`, `func_800B804C`, `func_800BBBE8`, `func_800BD6C4`: unwritten fields of `sp10` / `values` / `color` handed to callees.
- `func_800B9C58`: `1 << arg0[4]` with a byte shift count (UB for >= 32).
- `func_800BB788`: `unk1` indexes `unk2[22]` with a byte that can pass the array (`psx.at` used, correct).
- `func_800BEC30`: both arms return 1.
- `func_800BD610` / `func_800BB41C`: `char temp_a2` is unsigned on the PC (`-funsigned-char`), signed on the PS1; the port follows the PC oracle.
- `func_800BBE10`: unused `int _[4]`; `func_800BBBE8`: unused `VECTOR _`.

## Spec notes (not defects)

- Specs for the clean ports reach every branch their classes name. The gaps worth closing: `func_800B9C58` (shift counts 32..255), `func_800BD444` (opcode other than `0xE2`/`0xE3`), `func_800BAB14` (selector >= 8 without bit 3 set, valid since the array is unsized) and `func_800B6D48` (a pre-existing nonzero `unk0_24`, F2).
- The right-to-left argument-order comments (func_800B7574, func_800BA218, func_800BA8BC, func_800BADE0) match GCC's usual order and the specs record the getShort calls, so the diff would catch a wrong order; they were not checked against an object.

## Follow-up (2026-11-03)

Checked against the code and the objects (`build_boot.py --arch 64` and `--battle`; `objdump -d` of
`battle_4A0A8.o` in `pc/build/boot64` and `pc/build/boot`, sections `.text.<fn>__orig`). Commits: A = 46197f6d7,
B = ad85b6bce, C = 331bb6d64, D = dfd6e2c0e.

| Finding | Verdict | Evidence | Fix |
|---|---|---|---|
| F1 func_800B6D48, func_800B91F0 mistyped cast | confirmed | `573B8.h`: `int func_800BFE50(u_short)`; the other ports call `c.func_800BFE50(psx.conv(u16, ..))`. The diff cannot see it (the stub reads 16 bits). | A: both call `c.func_800BFE50(psx.conv(u16, ..))` and `c.func_8009DBDC(..)` directly; false comment dropped |
| F2 func_800B6D48 `texture` | rejected (port already right); comment fixed | both objects: `and BYTE PTR [eax+0x3],0xc0` with no or-in: GCC stores 0 into `unk0_24` (64-bit `and BYTE PTR [rax+0x3],0xc0`, 32-bit the same) | A: comment now says the objects store 0; C: spec mutations M7 (stores 1) and M8 (store dropped), both caught |
| F4 func_800B9C58 shift count >= 32 | rejected (port matches both objects) | `shl esi,cl; and esi,0x1f` (64-bit) and `shl eax,cl; and eax,0x1f` (32-bit) at all three arms: the count is taken mod 32, not compare-and-select | D: spec now feeds `p[4]` over the whole byte (edges 32, 33, 63, 64, 255 included) |
| F5 func_800BD444 default arm | confirmed (claim false); behaviour kept | object: `cmp al,0xe2 / je / cmp al,0xe3 / je`, any other opcode falls through with `rbx` (callee-saved, caller's value) unset; it does not take the 0xE3 arm. Unreachable through the opcode table, value unspecified | B: comment rewritten ("uninitialised, stale register"); the 0xE3 arm stays as the sanctioned stand-in. No spec can feed it (the oracle's value is a stale register) |
| F6 func_800BD6C4, func_800BDAB4 zeroed struct | confirmed (comment wrong); behaviour original | `func_8007DDF8`/`func_8007DDD4` copy the whole struct (`146C.c:9669-9671`), but only `unk0`/`unk4`, `r0`/`g0`/`b0` are read anywhere | B: comments say what happens. Fall-off return of func_800BD6C4: object `eax` is the opcode + 0x47 outside the jump table range 185..237 (`add eax,0x47` then `ja`), a code address inside it (`jmp rax` leaves the target in `rax`; the harness showed 4289576). B ports the first (`+% 0x47`), 0 for the second; the spec feeds out-of-range unknown opcodes (new class "no case") |
| F7 func_800B7358 hand offset | confirmed | `vs_battle_textBox` is in `views_4A0A8`; `unk22` exists | C: `box.unk22 = ..` through the view |
| F8 private statics without `orig:` | confirmed | `vectorMagnitude` (func_800BE53C.zig), `fixedPointMult` (vector_fixed_point_mult.zig) | C: `/// orig: _vectorMagnitude (..)` and `/// orig: _fixedPointMult (..)` |
| F9 func_800BE5A4 plain `+` | confirmed (style) | two widened u8 | C: one-line reason comment |

Extra: argument-order notes for func_800B7574, func_800BA218, func_800BA8BC, func_800BADE0 stand: zig_diff
compares the call trace with the arguments, and the oracle is the same compiler, so a wrong order would fail
(the objects show the two `vs_battle_getShort` calls ahead of the callee in all four). Spec gap `func_800BAB14`
(selector >= 8 with bit 3 clear): closed in D (selectors 16..87, table entries 0..95 set; passes). Spec gap
`func_800BD444` (other opcode): cannot be closed, the oracle's value is a stale register (see F5).
Outside the review's scope, same pattern: `print_variable_width_font.zig:14` also does `D_800F51B8.*.?` where the C
only adds to the pointer (the fix C made for `_pcFadeOt` is the template); not changed here.
