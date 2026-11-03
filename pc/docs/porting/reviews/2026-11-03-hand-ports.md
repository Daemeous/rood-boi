# Review of 61 hand-written battle ports (2026-11-03)

Method: each port read beside its C (`146C.c`, `3A1A0.c`, `40564.c`, `44F14.c`, `4A0A8.c`, `5BF94.c`) against PORTING.md §19, and its zig_diff spec glanced at. Code was read only: nothing built or run. Four reviewers took disjoint groups (A, B, C, D below); their notes follow verbatim after the summary.

## Summary

- 61 ports reviewed. **No high- or medium-confidence behavioural defect found.**
- 59 clean; 2 suspect at **low** confidence (both about a value the C leaves undefined).
- No repo text tried to instruct the reviewers.

| function | verdict | confidence |
|---|---|---|
| `_hitPrerequisiteUnused0` | suspect (return claim inconsistent with Unused1/2) | low |
| `func_800BEC58` | clean for defined behaviour; suspect on the fall-off return value | low |
| `func_800BDC9C` | clean (fall-off return from object) | medium |
| `_hitPrerequisiteUnused1`, `_hitPrerequisiteUnused2`, `_invalidHitFunction` | clean | medium |
| `func_800BCA8C`, `func_800D2ADC`, `_renderTextImmediate`, `func_80078AB4`, `func_8007138C`, `func_800AC690`, `func_800AEF94` | clean | medium-high |
| all other 41 functions | clean | high |

## Top findings (all low)

1. **`_hitPrerequisiteUnused0`** (`hit_prerequisite_unused0.zig:5-8`, C `146C.c:10754`; items 1, 11): its body is identical to Unused1/2 (`146C.c:10756,10758`), but its comment says the object returns the caller's `%eax` while Unused1/2 say 0; the port returns 0 in all three. The Unused0 spec mutation also omits `return 0`. One claim is wrong; check `objdump` on all three. Exposed by a call via `_attackPrerequisiteFunctions[37]` from `_canPerformAttack` (`146C.c:10767`) with a nonzero index.
2. **`func_800BEC58`** fall-off return (`func_800BEC58.zig:30-36` and final `return`, C `4A0A8.c:4365-4645`; items 1, 8): the port rebuilds `%eax` by hand (`last`). On the unknown-opcode path it returns 0, but the object may leave `sprintf`'s return (about 27). Exposed by an unknown opcode in the 16th trigger so the loop falls off the end; the spec's sprintf stub may mask it. No real game data has this.
3. **`_renderTextImmediate`** (`render_text_immediate.zig:23`, C `5BF94.c` `_pcFadeBegin`; §5.4): `.?` on `D_800F51B8` where the C only computes `NULL + k`. Traps if the pointer is null while `pc_hw_opacity_on()` is 1. Low.
4. **`func_800BCFB4`** (`func_800BCFB4.zig:58-60`, C `4A0A8.c:3571-3575`): `delta.vx` stored before `func_800BD2B8` runs, where C evaluates all arguments first. Matters only if the callee touches scratchpad 0x1F800098; it appears not to. Very low.
5. **`func_800B6B98`**: calls `c.func_800CCE10`, which returns a 4-byte struct. The i386 struct-return ABI is unprobed (§13/§20) and the spec stub returns `u32`, so an ABI mismatch would be hidden. Process note.
6. **Spec gaps** (port correct): `func_800AC690` `prepare` never sets `unk5B7` (8-bit wrap in hover branch not driven); `func_800AEF94` never sets `vs_gametime_tickspeed` to 0 or negative; `func_800BCA8C` evaluation-order comments checked only for callee purity.
7. **Style / §4.4 item 2 and §11.4** (no behaviour change): plain `+`/`*` without a one-line reason in `func_800AC690` (hover `y * 2`, `>> 1`), `func_800AEF94`, `func_800B6B98`, `func_800B75F4`, `func_800B7D10`, `func_800B7DC4`, `func_800B7B58`, `_renderTextImmediate`; `func_8007138C` zero-initialises `object` without the "uninitialised in the original" comment. Uninitialised bytes zeroed (allowed by §11.4): `func_800B81A0`, `func_800B8290`, `func_800B8360`, `func_800B8590`, `func_800B8788`, and out-parameters in `func_800BAC5C`, `func_800BAEA0`, `func_800BB7C4`, `func_800BC1CC`.

## Original-C bugs the ports keep (correct)

- `func_8007138C`: `targetLimb` tested via `unk4C[count]` instead of `[i]`; the `others` loop indexes `unk4C[i]` by actor.
- `func_80078AB4`: enemy-defense loop tests `unk4C[0]` every pass.
- `func_80074860`: arg2 is the cell buffer address, not an actor ID (C's own "BUG" comment).
- `func_800A4A24`: returns a pointer to its own table (stack on PS1, static on PC).
- `func_80070278`/`func_80070F28`/`func_8007138C`: unchecked `vs_main_actions[...]` and `mappedChainAbilities[unk2C03 - 1]`.
- `func_800AEF94`: unguarded `entries[unk17FD]`; `unk5B2` stored then overwritten by an overlapping word read; `unk13` unchecked against `bones`.
- `func_800AE47C`: `sp10` read uninitialised if `func_800AD494` leaves it.
- `func_800B784C`/`func_800B7B58`: unchecked `D_800F4B70[actor]`.
- `func_800B6B98`: clamp writes a width-derived value into `x`.
- `func_800B7DC4`: wrap fixup runs once, so `turn` can stay outside ±0x800.
- `func_800B849C`: unchecked `D_800E9B58[script[3]]`.
- `func_800BAC5C`: divides by `script[5]` with no zero check.
- `func_800BC1CC`: `temp_v0` truncated to u8 into `D_800E9C1C`.
- `func_800BEC58`: no `return` after the trigger loop; 0xC0 pivot uses `-unk144.vy` for x; actor id 1 leaves `result` 0.
- `func_800BDC9C`: no case 238, no default.
- `_invalidHitFunction`, `_hitPrerequisiteUnused0/1/2`: empty int-returning bodies.
- `_hpHealed`: uninitialised `weaponAffinity`; affinity tables read out of range at affinity 0.
- `func_800D2ADC`: ring/sphere cases leave `unk2C` stale when `flags & 0x1800` is 0x1800.

---

# Reviewer notes


### Group A review (12 ports)

All twelve read beside their C (`146C.c`, `3A1A0.c`, `40564.c`) against the §19 checklist. No behavioural defect found.

| function | verdict | confidence |
|---|---|---|
| func_80070278 | clean | high |
| func_800704B0 | clean | high |
| func_80070F28 (commitAction) | clean | high |
| func_8007138C (startCast) | clean | medium-high |
| func_80074860 | clean | high |
| func_80078748 | clean | high |
| func_800787F0 | clean | high |
| func_80078AB4 (timingWindow) | clean | medium-high |
| func_80079030 | clean | high |
| func_800A4A24 | clean | high |
| func_800A9530 | clean | high |
| func_800AC690 | clean | medium-high |

### Findings

None that change behaviour. Low-severity notes only:

1. **func_800AC690 `airborne`, hover branch** (`game/battle/func_800AC690.zig`, `actor.unk5B7 -% y * 2`): the plain `y * 2` and `y = actor.unk5B7 >> 1` are non-wrapping operators with no one-line reason (§4.4 rule 2). Cannot overflow (y <= 127). Checklist item 4. Confidence: high, style-level only.
2. **func_800AC690 spec gap** (`pc/tests/zig_diff/func_800AC690.zig`): `prepare` never sets `unk5B7` (the C's `timer` bitfield at +0x5B4), so `unk5B7 + y` wrapping past 255 in the 0x400000 hover branch is not driven. The port wraps correctly (`psx.conv(u8, ...)`), so this is only a coverage gap.
3. **func_8007138C**: `object` is zero-initialised where the C leaves it uninitialised (§11.4 asks for the comment "uninitialised in the original"). No behavioural difference expected, because the double oracle masks it. Low.

### Per-function detail (what was checked)

- **func_80070278**: the `actionIndex - 0x16u < 0x20` test is unsigned (`conv(c_uint)`). `unk0` is `u_int` and passed as `int`. `mappedChainAbilities` is `char[4]`, indexed by `unk2C03 - 1` (can be -1, via `psx.at`). `-0x16` is done in int. `pc_charge_skip_hold` guard kept. `battleAbilityTargetWindow = (u_short)(f*2)` wraps. `vs_gametime_tickspeed = 4` only in the non-plain branch. Call order matches.
- **func_80070F28**: the `pending` read sits between `func_8006EF10` and `func_800C16DC`, as in the C. The `D_800F18EC` NULL to 0x80000000 guard (`lowRam`) matches. `(index-0xB8) < 0x28U` is unsigned. `weaponAttacks[category]` goes through `psx.at`. `unk2C03 = previousChainInput = 0`. Switch labels match. Loop calls `func_8009E5C4(i)` for `i != targetActor`.
- **func_8007138C**: the ROOD_PC `memset(&request, 0, sizeof)` is kept first. Request byte offsets verified against `actionEffectRequest` (source +4 (type) / +8 (actor, +9 reserved, +10 part); targets +0x10, 12 bytes each; others +0xD0; position +0xE0; count +2; otherCount +3). Duplicate merge and `targetLimb == -1` (signed char) bug at `unk4C[count]` are kept (original bug). The second `func_800A152C` call is made after the first, as in the C. The `others` loop keeps the original bug (`unk4C[i]` indexed by actor). `armor[a][b]` is flattened as `a*4+b` (armor is `[6][4]`). `(actionIndex-0xB8) % 4` uses `@rem`. `object.index` is a bitcast of the u8 target.
- **func_80074860**: the LP64 static `sp20` vs 32-bit stack buffer matches the C's `#ifdef`. `psx.addr(sp20)` is bitcast to int. Original "BUG: arg2 should be an actor ID" is kept.
- **func_80078748 / func_800787F0 / func_800704B0 / func_80079030**: short-circuit order and call order match; `func_800CEF74(0)` result is ignored.
- **func_80078AB4**: `margin -= unk0/3` is computed unsigned (u_int `unk0`), then the signed `< 2` clamp, as in the C. `PADRup/right/left` are bits 4/5/7 (`include/psx/libetc.h`). Button priority 3 (bit 7), 2 (bit 4), 1 (bit 5) is the same. `~unk2C02 & mask` is widened to int before `~`. The `unk4C[0]` loop (index unused) and the `break` inside the loop are kept. `chainStreak <= 0xFFFE` guard kept. `D_8005FFB0` is a `short`. The `return` in the `unk40 != 0` branch skips the tail, as in the C.
- **func_800A4A24** (3A1A0): static table, `0x80000000` fallback and `arg0 - 2` match. `id` is `short`, widened. The 3A1A0 function is not a copy; the 40564 inlined copy (`platformById` in `func_800A9530.zig`) is also correct.
- **func_800A9530**: `direction = D_800F49F9 >> 1` (u_char), `unk1800 = direction << 10` truncated to i16, `-position` wraps, `edge - (position & 127)` truncated to i16, the fold into -32..32 reads the other axis' stale value as the C does, `*(u_int*)+0x5AC & 0xF0000000` and `0x1000` tests the same, `rsin(0x200) * unk63C / 4096` signed. `unkC_0` zeroed then restored around `func_800A70DC`.
- **func_800AC690**: restructured with `goto` mapped to `Next {proceed, land, fall}`. Paths verified: state 2 (windup/take-off), state 3 (hover timer in `unk5B7`, floor sign-extension `<<17>>17`, bounce, slide, platform jump case 2 with `unk1816` decremented first, `land`/`fall` targets and landing sequence reading flags after `func_800AA698`), state 4 (uses the entry-time `flags`; `break` becomes `return`). `profile` is computed first. `D_800F49F0` is dereferenced only when `y == 3000` (short-circuit kept).

### Kept original-C bugs (correct to keep)

- func_8007138C: `request.targets[count].value.actor.part` is tested via `unk4C[count].unk0.targetLimb` instead of `[i]`; the `others` duplicate test uses `unk4C[i]` with the actor index `i`.
- func_80078AB4: the enemy-defense loop tests `unk4C[0]` on every pass regardless of `i`.
- func_80074860: `func_800BEC58` arg2 is the cell buffer's address, not an actor ID ("BUG" in the C).
- func_800A4A24 / platformById: returns a pointer to its own table; on the PS1 this is a stack pointer (the PC branch makes it static).
- func_80070278 / func_80070F28 / func_8007138C: `vs_main_actions[actionIndex]` and `mappedChainAbilities[unk2C03 - 1]` are unchecked (the port uses `psx.at`).

### Spec check

Mutation lists and `classes` look adequate: boundary mutations exist for every threshold (0x8D, 0xB8, 0xE0, 0x16/0x20, 54, margin 2, ...). `func_800AC690` classes and `prepare` reach windup, take-off, flight, recovery, slide, platform-jump and landing (apart from the `unk5B7` wrap noted above).

### Repo text aimed at me

None found in the files read.


### Group B review (15 ports)

Read each port beside its C (44F14.c, 4A0A8.c) and its specs; no build possible, so every claim is from reading.

### Verdicts

| function | verdict | confidence |
|---|---|---|
| func_800AE47C | clean | high |
| func_800AEF94 | clean (one spec gap, below) | medium-high |
| func_800B6B98 | clean | high |
| func_800B75F4 | clean | high |
| func_800B770C | clean | high |
| func_800B77DC | clean | high |
| func_800B784C | clean | high |
| func_800B7B58 | clean | high |
| func_800B7C90 | clean | high |
| func_800B7D10 | clean | high |
| func_800B7DC4 | clean | high |
| func_800B810C | clean | high |
| func_800B81A0 | clean (uninitialised byte zeroed, allowed by 11.4) | high |
| func_800B8290 | clean (uninitialised pad zeroed, allowed by 11.4) | high |
| func_800B8360 | clean (uninitialised pad zeroed, allowed by 11.4) | high |

No high- or medium-confidence defects found.

### Findings (all low severity)

1. **Plain `+`/`*` without a reason comment (checklist 4, section 4.4 rule 2)** - low confidence it matters, it is a rule breach, not a behaviour change.
   - func_800AEF94 (`depth().* +%= @as(c_int, actor.unk6F4.unk3) * 4`, game/battle/func_800AEF94.zig, the linked-actor block; `shift`'s `sign * @as(c_int, o.unk0)`; `((word >> 3) & 1) + 1`), func_800B6B98 (`* 13 + 4`, `* 12 + 8`, `320 - ...`), func_800B75F4/func_800B7D10 (`<< 7) + 0x40`), func_800B7DC4 (`turn += 0x1000`), func_800B7B58. All provably cannot overflow (u8/i8 times a small constant), but section 4.4 asks for a one-line "why" on each non-wrapping operator. Not a defect.

2. **func_800AEF94 spec gap** (checklist 11), spec `pc/tests/zig_diff/func_800AEF94.zig`: `vs_gametime_tickspeed` only takes {2,4,1,8}, never 0, negative or odd. The C's `tick / 2` (step) and `(tick + (tick>>>31)) >> 1` (func_800B0908's argument) are the sign-sensitive spots. The port is right for negatives (`@divTrunc`, arithmetic `>>` after the unsigned sign add), so this is only about coverage. Example: tick = -3 gives step -1 in C (trunc) and -2 if someone wrote `>> 1`. Also `unk6F4` offsets and `unk3` are whatever `ctx.alloc` fills, not edge-biased (-128, 127, 255). Confidence the gap is real: medium; port itself is correct.

3. **Uninitialised-value zeroing differs from the C's stack garbage (11.4, allowed)**. Each was checked:
   - func_800B81A0 (`setting.unk3 = 0`, .zig:16): the C copies all four bytes of `sp10` into `actor->unk0.unk58` (D_800F4538_unk58_2 is `*arg1` copied, 30DB0.c:4061), so garbage reaches game state; the port stores 0. Masked by the double oracle. Low.
   - func_800B8360 (`vec.pad = 0`): func_8009FC60 copies `*arg2` into unk17F4 but then overwrites `.pad` with arg3 (always 0x1000 or script[7], never -1), so the garbage is dead. Truly harmless.
   - func_800B8290: func_800A9F64 / func_800A9FD0 only read vx, vy, vz (func_800AAA88 reads vx/vy/vz; func_800A9FD0 builds its own SVECTOR). Harmless.
   - func_800B75F4 / func_800B7D10: `pos.pad = 0` in the func_8007CFF8 branch; the callee reads vx and vz only (146C.c:9369). Harmless. (The C leaves `pad` unset there; the extra store is invisible except through a stub that hashes the struct.)

4. **i386 struct return** (func_800B6B98 calls `c.func_800CCE10`, returning a 4-byte bitfield struct `textOffset`, 5BF94.c:4705). PORTING 13/20 flags struct-by-value returns on i386 as unprobed. The port declares the return as `c.textOffset`, as the C does, so it follows whatever Zig's i386 ABI does; the spec stub returns a `u32`, which would hide an ABI mismatch on the 32-bit target. Low confidence, process note rather than a port bug.

### Original-C bugs kept (correct)

- func_800AEF94: attached actor `entries[actor->unk17FD]` is dereferenced without a null test (port `.?`), and `entries[unk12]->bones[unk13]` only has the PC null guard; `unk13` is not bounds-checked against `bones` (port uses `psx.at`).
- func_800AEF94: `actor->unk5B2` is stored 1 then overwritten by a word read at 0x5B0 that overlaps it; the port keeps the store order.
- func_800AE47C: `sp10` (header) is read uninitialised if func_800AD494 does not write it; the port's `undefined` equals that.
- func_800B784C / func_800B7B58 / others: `D_800F4B70[actor]` indexes a `char[]` by an unchecked u16; port indexes the unsized view the same way.
- func_800B6B98 clamp: `width = 320 - width; result->x = width;` writes a width-derived value into x (original quirk), kept exactly.
- func_800B81A0: writes only 3 of 4 bytes of `D_800F4538_unk58_2`, kept (zeroed fourth, see 3).
- func_800B7DC4: the wrap fixup only runs once, so `turn` can stay outside +-0x800 when pad is far from the target, and is then truncated to `short` for func_800A9E38 (the C prototype is `short`; the port's `psx.conv(c_short, turn)` matches).

### Hook / guard check (checklist 5, 9)

- func_800AE47C and func_800AEF94: both PC guards ported at exactly the C's places (`v0 == NULL || sp10 == NULL`; empty parent slot; texture-search skip of empty slots, in the main and linked actor blocks). The log-once statics go through the globalised accessors; `reported++` is `+%1` on the latch; the log text arguments match (unkF/animationId; i, unk13, unk12).
- No PC feature hooks (`pc_*` weak calls, `PC_GTE_UNNARROWED`) in any of the 15 C bodies.

### Repo text that tried to instruct me

None found.


### Group C review (19 functions, all in 4A0A8.c)

No defects found. All 19 are clean against the section 19 checklist. Confidence is high unless noted.

| function | verdict | confidence |
|---|---|---|
| func_800B83D4 | clean | high |
| func_800B8438 | clean | high |
| func_800B849C | clean | high |
| func_800B8510 | clean | high |
| func_800B8590 | clean (uninit pad zeroed, see N1) | high |
| func_800B8788 | clean (uninit unk3 zeroed, see N1) | high |
| func_800B92A0 | clean | high |
| func_800B92F4 | clean | high |
| func_800B9608 | clean | high |
| func_800B9670 | clean | high |
| func_800B9D74 | clean | high |
| func_800B9DB4 | clean | high |
| func_800BAC5C | clean | high |
| func_800BAEA0 | clean | high |
| func_800BAF6C | clean | high |
| func_800BB7C4 | clean | high |
| func_800BC1CC | clean | high |
| func_800BC9E0 | clean | high |
| func_800BCA8C | clean | medium-high (large; the evaluation-order comments "read before the call, as the object does" were checked only for purity of func_800BFEBC, not against the object) |

### What was checked
- Call order and arguments. B8510 calls getShort(+3) before getShort(+1), as C does. BAC5C reads the flag via getShort(+6) before the two func_800A1108 calls. BC1CC measures the eye distance before the look-at distance. The BCA8C cases 2/3/4/5/8/9/11, the follow blend and the end-of-time tail follow the C order.
- Signedness and widening. D_800E9B58, D_800E9BF0 and D_800E9C1C are u8, D_800E9B4C is int. State-flag values go through conv(u8), so -1 becomes 0xFF in BAC5C. dx/dz are narrowed to i16 and their squares are summed with +% (can reach 2^31). tickspeed is int, and the u8 time field is converted back with conv(u8).
- Shifts: BCA8C `(angleDelta*ease)>>12` and the >>12 in vectorMagnitude and toNormalIntegerVector keep arithmetic shifts. BC1CC `(duration<<16)!=0` is equivalent to duration != 0.
- Operator precedence: Zig `&` and `|` bind tighter than `==`/`!=`, so B8788's `a|b|c != 0` and B9DB4's `x & 0x80 == 0` read as the C does.
- Scratchpad addresses. cameraScratch offsets match 0x1F800088 (value, distance), v1=0x98, v2=0xA8, rotation=0xD8, matrix=0xF8, and all are 4-aligned.
- Statics. `_copyVector`, `_vectorMagnitude`, `_sVectorToFixedPointVector`, `_fixedPointMult` and `_toNormalIntegerVector` are static in 4A0A8.c, so they are re-ported privately with the PC branch of `_fixedPointMult` (i64 product, >>12, truncate). Each carries an `orig:` line.
- Hooks: none of these functions has a ROOD_PC or PC_* hook, except that B9DB4 itself is a ROOD_PC replacement of INCLUDE_ASM and is ported faithfully.
- Null guards: B92F4's `.?` on func_800CB66C is safe (it returns &D_800F4E98, never null). `arg.?` in BCA8C and `D_800F4BA4 .?` in BC1CC/BC9E0 match the C's unconditional deref (no guard in the C).
- Specs: all 19 have `[function.*]` specs with 4-10 mutations each. They cover each branch seen: B92F4 cases 10-15 and default, B9DB4's three paths, B849C 0xFF, B8788 tail zero/nonzero, and the BCA8C modes with time/duration and follow edges. Mutations for the port-only helpers (fixedPointMult, toNormalIntegerVector, finishTransition, case 14 ordering, the camera re-read) are present. I did not run them.

### Notes (not defects)
- N1 (low): func_800B8590 (vec.pad) and func_800B8788 (entries[0].unk3) leave a byte uninitialised in the C, and func_8009FC60 / func_8009F940 copy the whole struct into the actor. The ports zero it (PORTING 11.4). The original leaves stack garbage there; the zig_diff specs exclude it. Correct as written.
- N2 (low): BAC5C, BAEA0, BB7C4 and BC1CC zero-initialise stack out-parameters (zeroes of func_8006EBF8_t, MATRIX, sp30, VECTORs). In C they are uninitialised, but they are written by the callee before any read, assuming func_800A1108 fills every field read (vx, vz of unk0.unk4). If it did not write them, C would read stack garbage and the port zeros; no sign of that.

### Original-C bugs kept (correct)
- func_800B849C indexes D_800E9B58[script[3]] unchecked (an unsized array); the port keeps it via psx.at.
- func_800B96B0-style missing returns are not in this group. B8510/B8590 have the unused `arg1`.
- func_800BAC5C divides by script[5] with no zero check (SIGFPE in C, panic in the port).
- func_800BC1CC `D_800E9C1C[2] = D_800E9C1C[6] = temp_v0` truncates to u8 and reaches the commands via the unchecked short-to-byte path; kept.

### Repo text aimed at me
None found in the files read.


### Group D review (15 functions)

Read each port beside its C (4A0A8.c, 146C.c, 5BF94.c) against PORTING.md section 19. No source changed.

| function | verdict | confidence |
|---|---|---|
| func_800BCFB4 | clean (one very-low note) | high |
| func_800BDC9C | clean; fall-off return taken from the object | medium |
| func_800BE3D0 | clean | high |
| func_800BEC58 | clean for defined behaviour; suspect only on the fall-off return value | low |
| func_800BF5F0 | clean | high |
| func_800BF850 | clean | high |
| func_800D2ADC | clean | medium-high |
| _handleCombatCameraZoom | clean | high |
| _hitPrerequisiteUnused0 | suspect (inconsistent with Unused1/2) | low |
| _hitPrerequisiteUnused1 | clean | medium |
| _hitPrerequisiteUnused2 | clean | medium |
| _hpHealed | clean | high |
| _invalidHitFunction | clean (uninitialised return, 0 here) | medium |
| _renderTextImmediate | clean (one low note) | medium-high |
| vs_battle_applyCameraState | clean | high |

### Findings (most severe first; all low)

### 1. _hitPrerequisiteUnused0 vs Unused1/2: the same empty C body gets two different return claims
- Port: hit_prerequisite_unused0.zig:5-8, unused1.zig:5-8, unused2.zig:5-8. C: 146C.c:10754, 10756, 10758. Checklist items 1 and 11 (return value, oracle fidelity).
- The three C bodies are identical (`{ }`). Unused0 claims its object leaves the caller's %eax (the 64-bit table index, the 32-bit function address) and returns 0. Unused1/2 claim the object returns 0 on both targets. One of the two statements is wrong.
- The specs differ too: the Unused0 mutation is `{ }` -> `{ if (arg1) arg1[0]=1; }` with no return, while the Unused1/2 mutations add `return 0;`. This suggests Unused0's return is not compared.
- Exposing input: any call through `_attackPrerequisiteFunctions[37]` from `_canPerformAttack` (146C.c:10767) with a non-zero table index. The caller gets garbage in the C and 0 in the port. `_canPerformAttack` returns that value directly, so a caller testing `!= 0` could differ.
- Fix: check `objdump -d` for all three and make the comments and returns agree. Confidence low; cannot verify without the object.

### 2. func_800BEC58: the fall-off return value
- Port: func_800BEC58.zig:30-36 (comment), the `last` bookkeeping, and the final `return`. C: 4A0A8.c:4365-4645 (no return after the `for`). Checklist items 1 and 8.
- The port reproduces the object's `%eax` at the end as `last`: the last trigger's type byte when its mask missed, else the last result, else 1 when an actor reference resolved to "none" (id 1). I could not check this against the object.
- The unknown-opcode path (`default:`, 4A0A8.c:4630) is the suspicious part. The port returns `result` (0). The real `%eax` after `sprintf` may still be sprintf's return, 27 for "UNKNOWN TRIGGER DETECTED:%02x". The spec emits opcode 0x20 as an unknown trigger. If its sprintf stub returns 0 or a constant, this is masked.
- Exposing input: the 16th trigger (offset index 15) holds an unknown opcode and the 16-trigger loop falls off the end. The C returns whatever is in `%eax` there (possibly 27 or an actor id); the port returns 0.
- Real game data never has an unknown trigger, so practical impact is nil. Confidence low.

### 3. _renderTextImmediate / fadeBegin: `.?` where the C only does pointer arithmetic
- Port: render_text_immediate.zig:23 (`psx.at(v.g.D_800F51B8().*.?, ...)`). C: 5BF94.c `_pcFadeBegin`, `_pcFadeOt = D_800F51B8 + D_800F4E80 * 4;`. PORTING.md 5.4 (null base) and 5.3.
- The C computes `NULL + k` without dereferencing. The port traps if `D_800F51B8` is NULL, and `psx.at` on a possibly-null base is the case 5.4 warns about.
- Exposing input: `pc_hw_opacity_on()` returns 1 while the ordering-table pointer `D_800F51B8` is null (a text box closing during a scene load).
- During normal rendering the pointer is non-null. Confidence low.

### 4. _renderTextImmediate: plain `+`/`*` without the one-line reason (section 4.4 item 2, section 17)
- Port: render_text_immediate.zig:66-67, 69 (`x + width`, `y + height`, `width * height * 2`). The operands are small (box fields), so no overflow is possible. Style rule only: add a comment or switch to wrapping operators. Confidence high that it is only style.

### 5. func_800BCFB4: delta.vx written before func_800BD2B8 runs
- Port: func_800BCFB4.zig:58-60. C: 4A0A8.c:3571-3575, a `setVector(&delta, x, func_800BD2B8(...) - y, z)` call whose arguments are all evaluated before any store.
- The port stores `delta.vx` before calling func_800BD2B8. The only observable difference is if func_800BD2B8 or func_8008DC7C read or write scratchpad 0x1F800098. Neither appears to. Confidence very low; noted for completeness.

### Checked and found equivalent (spot list)
- func_800BDC9C: in the opcode 234/236 non-zero branch `unk4 = unk2` happens only when script[1] != 0 (matches the C ternary). 235 always sets unk4. 239 clears `entry[1]` then `entry[0]`, as the C does.
- func_800BE3D0: the two `initialiseCameraFromSpherical` argument orders and all four `_copyVector` targets match. `char` is u8 (`-funsigned-char`).
- func_800BEC58: signedness of `type`/`mode` (short), setStateFlag's `char` argument (`conv(u8)`), the evaluation order of the two `func_800BFE50` calls in case 6, and cases 3 and 15 (the `goto compare`) all match. The `-(vy/128)` and `-func_800A92B8()/128` forms use `@divTrunc` and `-%`.
- func_800BF5F0: store order of the chained assignments matches (`[3]`,`[2]`,`[1]`; `.high` then `.low`; unk3 `[2]`,`[1]`,`[0]`). `D_800F4BB0` is stored by bits as `usize`.
- func_800BF850: all four state branches, the `short` switch on D_800F4BE0 (case 2 indexes `short*` doors by `_evtFile`), `(u_char)func_800BD610() == 0xFF`, and loop `i` in 1..3 all match.
- func_800D2ADC: every shape case (0, 8, 16, 24, 32, 40/48) and each mode. The `goto scaleN` merges are equivalent to the C's `switch` with the shared tail. The 0x4000/0x6000 fall-through into the `case 0` loop is reproduced by the pre-block plus merged switch. The 0x8000 case has no 0x40000 test, as in the C. The 0xC0 pivot bug (x spread is [vx, -vy]) is kept. The `count > 0` PC guard is kept. RAND_UNIFORM_WIDE's `(rand() + (rand() << 16)) % n + lo` is ported with the first draw as the left operand.
- _handleCombatCameraZoom: matches branch by branch. Zoom kinds 0, 1, 2 and other.
- _hpHealed: sourceAffinity is a `u_char`, so affinity 0 becomes 0xFF and the `if (sourceAffinity)` tests are true (matches `psx.at` with u8 index). `func_8007FE5C`'s `short` parameter gets `conv(c_short, attack -% defense)`.
- _renderTextImmediate: `' '` is 143 and `'\f'` is 229 (re-encoded in the `.i`); PADRright is 1<<5; the `(width-12) >= printX` unsigned compares are converted.
- vs_battle_applyCameraState: scratch offsets (0xD8 = `((SVECTOR*)0x1F800088)[10]`), order of the vz, vx, vy stores, and call order match.

### Original-C bugs kept (correct to keep)
- func_800BEC58: no `return` after the 16-trigger loop (value taken from the object). Opcode 0xC0 pivot: x spread uses `unk144.vy` instead of `.vx`. Opcode 7/13/14/16 with actor id 1 resets `negate` and leaves `result` 0.
- func_800BDC9C: no case for opcode 238 and no default; falls off the end.
- _invalidHitFunction, _hitPrerequisiteUnused0/1/2: empty int-returning bodies.
- _hpHealed: with the hit's affinity 1, `sourceAffinity` is 0 and `weaponAffinity` is read uninitialised. With affinity 0, `sourceAffinity` is 0xFF and the affinity tables are indexed out of range. Both are kept (zero and `psx.at`).
- func_800D2ADC: ring and sphere cases leave `unk2C` stale when `flags & 0x1800` is 0x1800.

### Repo text that tried to instruct me
None seen in the files read.

---

## Follow-up (2026-11-03)

Checked against the code and the objects (`objdump -d` of `battle_146C.o`, `battle_4A0A8.o` in `pc/build/boot64` and
`pc/build/boot`). Commits: B = ad85b6bce, C = 331bb6d64, D = dfd6e2c0e.

| Finding | Verdict | Evidence | Fix |
|---|---|---|---|
| 1 `_hitPrerequisiteUnused0` vs Unused1/2 return claims | rejected: both comments are right | the objects differ: Unused0 is a bare `ret` on both targets (the caller's `eax`, like `_invalidHitFunction`), Unused1 and Unused2 are `xor eax,eax; ret` | none; 0 stays the sanctioned stand-in for Unused0 (not reproducible in Zig) |
| 2 func_800BEC58 fall-off return after an unknown opcode | rejected | 64-bit: `call sprintf; xor eax,eax; jmp <result==negate test>`; 32-bit: `call sprintf; add esp,0x10; xor eax,eax; jmp`: `eax` is `result` (0) after the call, as the port has it | none |
| 3 `_renderTextImmediate` `.?` on `D_800F51B8` | confirmed (PORTING 5.4: a null base is plain address arithmetic) | C: `_pcFadeOt = D_800F51B8 + D_800F4E80 * 4` (u_long elements), no dereference | C: null base gives the integer result (`slot * 4`, null when 0); the same fix is open in `print_variable_width_font.zig:14` (not in the review) |
| 4 func_800BCFB4 `delta.vx` stored before the call | rejected | `setVector` is the comma macro `(v)->vx = _x, (v)->vy = _y, ..` (`include/psx/libgpu.h:82`): vx is stored before `_y`'s call is evaluated, as in the port | none |
| 5 `c.func_800CCE10` struct return on i386 | confirmed, already documented as a prerequisite (PORTING 13 / 20) | 32-bit object: `push` of a hidden result pointer, `mov eax,[esp+0x28]` after the call (sret); 64-bit: result in `eax`. Zig derives the ABI from the `c` prototype; the stub returns `u32`, valid on the 64-bit gate only | none; open until the i386 probe exists |
| 6 spec gaps | func_800AC690 `unk5B7`: confirmed; func_800AEF94 tick speed: partly rejected (`ctx.pick` already gives any value half the time); func_800BCA8C order comments: purity only | | D: AC690 `prepare` sets `unk5B7` to edge values (0, 1, 0x7E, 0x7F, 0x80, 0xFE, 0xFF); AEF94 tick speed edges 0, -1, -2, -3, 3 added |
| 7 plain `+`/`*` without a reason | confirmed for func_800AC690, func_800AEF94, func_800B6B98, func_800B75F4, func_800B7DC4, `_renderTextImmediate`; rejected for func_800B7D10, func_800B7B58 (no plain operators in them: only `script + k` pointer arithmetic and `>> 1`) | | C: wrapping operators (`+%`, `-%`, `*%`) throughout; the specs' Zig-text mutations updated |
| 7 func_8007138C zeroed `object` without the comment | confirmed (style) | | C: "uninitialised in the original" comment |
| 7/N1/N2 zeroed uninitialised bytes (func_800B81A0, B8290, B8360, B8590, B8788, BAC5C, BAEA0, BB7C4, BC1CC) | original behaviour (PORTING 11.4); no change | callees copy or overwrite, or the fields are never read (as the reviewers traced) | none |

Group notes that were not findings (func_800BDC9C fall-off, func_800D2ADC, `_hpHealed`, ...): unchanged.
