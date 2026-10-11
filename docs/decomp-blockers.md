# Unmatched-function blockers

Snapshot: `../rood-reverse-hasm/progress.json` (fuzzy_match_percent != 100): **88 functions, 68,132 bytes** (code 1,868,460 / 1,936,592 = 96.48%). Best scores are the lowest strict score per function in `.work/family-strategy/trials.sqlite`; lower is better, 0 is exact. Each score was taken under the recipe in force when the trial ran. Mechanisms come from `.work/ultracode/*/NOTES.md` (directory names are given per entry), `hasm-catalogue/`, `maspsx-div/NOTES.txt` and the `survey-*` lanes.

## Summary by category

| # | Category | Functions | Bytes | What unblocks it |
|---|---|---:|---:|---|
| 1 | Original hand-written asm or inline asm | 4 | 9,780 | Policy decision (hasm conversion or an inline-asm allowance) |
| 2 | Exact only with source the integrity rules reject | 3 | 1,660 | Policy decision on the specific construct |
| 3 | Library compiler version, proven with a diagnostic compiler or flag set | 17 | 8,780 | Toolchain or recipe decision (per-unit cc1 pin, patched cc1, or flag patch) |
| 4 | Library compiler version, systemic traits no available compiler reproduces | 44 | 23,900 | Toolchain fix: find or build the PsyQ library cc1 (older cse/loop/reorg behaviour); a few also need reconstruction |
| 5 | Register allocation priority or preference (correct compiler) | 7 | 5,040 | More search for the original source shape |
| 6 | Instruction scheduling order (sched1/sched2 tie-breaks) feeding allocation | 10 | 11,372 | More search; a sched1 simulator over block RTL would turn it into a search |
| 7 | cse / jump canonicalisation | 2 | 4,664 | More search for an original construct (label or copy) that disappears by final |
| 8 | reorg / delay-slot fill in game code | 1 | 2,936 | More search for a label placement before dbr |
| | **Total** | **88** | **68,132** | |

Readout:
- **Policy decisions (11,440 B, categories 1 and 2).** These are the cheapest bytes left. Category 1 is hand-written or inline asm (func_800C2E24 alone is 6,188 B). Category 2 already has exact candidates: func_8006CB04 needs a dead conditional store, SpuClearReverbWorkArea needs a volatile local and _card_format needs a pointer-copy chain.
- **Library compiler (32,680 B, categories 3 and 4).** The PsyQ library objects were not built by any cc1 we ship. 2.7.2-cdk -mmips-as (with reload_cse_regs off) reproduces libcd BIOS/C_011, libgpu/SYS case trees and PADIF. The GFE flags reproduce the libds DSSYS_2 near misses. Neither is guard-safe for every unit. The remaining traits are struct-base CSE, mem-extend double loads, register-for-constant, queued post-increment, older loop.c and newer reorg. They point to an older (2.6.x-era) ccpsx with a later reorg.
- **Source search under the correct compiler (categories 5-8).** Mechanisms are pinned with RTL dumps. Further progress needs either a natural form nobody has found yet or tooling: the proposed sched1 simulator for the 25AC Sound_* family.

Key: *best* = lowest ledger strict score and its candidate (repo-relative). *alt* = a candidate outside the ledger or under a diagnostic compiler.

## 1. Original hand-written asm or inline asm — 4 functions, 9,780 bytes

Unblock route: Policy decision (hasm conversion or an inline-asm allowance).

### BATTLE.PRG/58578 func_800C2E24 (6,188 B)
- best: none in ledger; ledger trials: 0
- blocker: Original asm text: two `ori $at,$zero,0x8000; addu $t2,$t2,$at` sequences (0x5AAF8, 0x5AE34) are the assembler expansion of `addu $t2,$t2,32768`. cc1 (2.8.1-psx, 2.7.2-cdk) never emits $at and folds (x<<16)+0x8000 to `ori`; GTE moves are also interleaved with no nops (psyq `_b` style).
- tried: ~12 C spellings of the +0x8000 in probe.c/probe2.c (asm barrier, loop-carried add, negated/unsigned constants, pointer arithmetic, long long); every one gives `ori` or `li reg,32768; addu`. No full C draft (cannot reach 0 under the rules).
- unblock: Policy: convert to a hand-written .s (hasm) unit like 2EA3C.s, or allow an inline-asm GTE+add block. hasm-catalogue CATALOGUE.md line 70 still lists it as compiled C; revise that.
- notes: r13-C2E24, r10-lane1, r12-f

### BATTLE.PRG/6E644 func_800D6E44 (2,512 B)
- best: 593 (`.work/ultracode/gte-macros/s5.c`); ledger trials: 64
- blocker: Original inline asm: split `div $zero,..` ... 16 insns ... `mflo` (only compiled-C split div in the binary, maspsx-div/scan2.tsv), and reorg leaves the following `jal rcos` slot empty, which only happens when the mflo was an asm statement. Body is otherwise GCC + GTE macros (0x48 frame, jump table, single epilogue).
- tried: maspsx/cc1 audit: cc1 2.7.2/2.8.1 only have the `=d` divmodsi4/divsi3 patterns, no LO-output div, so no compiler or maspsx fix exists. Probe s5/s6 (asm `div $zero,%0,%1` early + asm `mflo %0` late) reproduces the region exactly (score 593 for the whole function).
- unblock: Policy (already decided by the user: left INCLUDE_ASM). Would need either acceptance of two inline-asm statements (div / mflo) in C, or a hasm conversion.
- notes: maspsx-div/NOTES.txt, r12-f, r8-6E644-tail, hasm-catalogue CATALOGUE.md:291

### BATTLE.PRG/573B8 func_800C0B50 (552 B)
- best: none in ledger; ledger trials: 0
- blocker: Needs operand-constrained GTE inline asm that no existing inline_c/vs_inline_c macro provides (ctc2 of C values into L-matrix $8-$12, mfc2 $9-$11 into C vars, mtc2 $0/$1, no-nop `_b` variants of mvmva/rtps), plus a bare unchecked `div $zero; mflo` for a lone `/` (same bucket as func_800D6E44). Prologue/delay-slot fills are compiler output.
- tried: Not attempted in the ledger; target audited in r6, r10-lane4, r12-f, misc-battle-lib.
- unblock: Policy: allow new operand GTE macros (and the unchecked-div inline asm), or hasm. Then a normal C reconstruction.
- notes: r12-f, r10-lane4, r6-3A1A0-misc, misc-battle-lib, CATALOGUE.md:293

### SCREFF2.PRG/0 func_800F9BD8 (528 B)
- best: none in ledger; ledger trials: 0
- blocker: Hand-written (r10-lane0 proven_handwritten): splat 'Handwritten function', leaf with manual `sw $s0` in an 8-byte frame, raw mtc2/mfc2 GTE sequencing marked 'handwritten instruction'. CATALOGUE.md lists it 'unsure' (GCC-shaped body, maybe C + gte_ldLZCS/sqr).
- tried: Skipped in ending-screff2 and r10-lane0; no C draft.
- unblock: Policy: hasm conversion of SCREFF2.PRG/0, or one C attempt with GTE macros if the catalogue's 'unsure' reading is preferred.
- notes: ending-screff2, r10-lane0, r12-f, CATALOGUE.md:294

## 2. Exact only with source the integrity rules reject — 3 functions, 1,660 bytes

Unblock route: Policy decision on the specific construct.

### SLUS_010.40/libcard/FORMAT _card_format (652 B)
- best: 2628 (`.work/ultracode/r12-lib2/fmt/t_c1.c`); ledger trials: 4; alt: exact non-natural: .work/ultracode/r12-lib2/fmt/q0.c
- blocker: Post-loop 'M','C' header block: target stores through a separate copy (s0 = s1, then via a2); a one-buffer natural form always folds buf[1] to D_80040128+1. Unit (libcard/FORMAT) has no other C and needs 2.7.2-psx -mhard-float.
- tried: fmt/q0.c is EXACT (survey sv MATCH) but needs chained pointer copies p1->p2->p3 (p3 assigned twice) = permuter artefact. n4.c (loop2 assigns buf in body) leaves only the M/C block; best ledgered t_c1.c 2628; ~40 flag variants in survey-libpad-etc.
- unblock: Policy (accept q0.c's pointer chain) or a natural source with inline helpers taking buffer params (would explain the copies); plus the per-unit -mhard-float setting (guard-free: no C in unit).
- notes: r12-lib2, gas-lib-B, lib-pad-card-api, survey-libpad-etc

### BATTLE.PRG/146C func_8006CB04 (604 B)
- best: 0 (`.work/ultracode/r12-e/cb9.c`); ledger trials: 40; alt: best rule-clean: battle-146C/cb04/a5.c (1056)
- blocker: cse canonicalisation (make_regs_eqv): the loop pre-test operand i (=0) is canonicalised to the older never-read `applied` flag (s5) only if applied is mentioned after the loop; that mention must vanish after global alloc. Only a conditional dead store does that (flow1 deletes the body, jump2 removes the jump to .+1).
- tried: 40 ledger trials (r7..r12): applied at top / in then-branch / for(i=applied), applied=1 before/after the call, later stores/reads; RTL dumps cb6/cb7. cb9.c (`if (applied) { effect = 0; }` after the effect switch) is SCORE 0 and in situ 151/151 words.
- unblock: Policy decision: accept the conditional dead store as the probable original (then integrate cb9.c), or leave at 1056 (cb04/a5.c).
- notes: r12-e, r9-146C, r8-146C, r7-sweep, battle-146C

### SLUS_010.40/libspu/S_CRWA SpuClearReverbWorkArea (404 B)
- best: 0 (`.work/ultracode/gas-lib-A/t/S_CRWA.c`); ledger trials: 4
- blocker: Target keeps the saved _spu_transferCallback in a stack slot (0x10(sp)) and re-reads it (lw;beqz;lw): volatile-local semantics.
- tried: Score 0 only with `void (*volatile callback)(void)` local (gas-lib-A/t/S_CRWA.c); non-volatile local and 1-element local array both 1491; was dropped at integration for the artificial volatile.
- unblock: Policy decision on the volatile local (PsyQ declares the global volatile; the local is the open question). If accepted, integrate S_CRWA.c (gas recipe).
- notes: r10-lane1, r9-lib-rest, lib-spu-snd, gas-lib-A

## 3. Library compiler version, proven with a diagnostic compiler or flag set — 17 functions, 8,780 bytes

Unblock route: Toolchain or recipe decision (per-unit cc1 pin, patched cc1, or flag patch).

### SLUS_010.40/libcd/C_011 StCdInterrupt (2,332 B)
- best: 1902 (`.work/ultracode/r13-cd/st_b.c`); ledger trials: 3
- blocker: Compiler version: unit needs the CDK cc1 (2.7.2-cdk -mmips-as -> 40, st_b.c), and the remaining `li a3,1` -> `move a3,v1` comes from CDK's reload_cse_regs (Cygnus backport). With reload_cse_simplify_set disabled (diag cc1 2.7.2-nocse) st_b.c is EXACT.
- tried: u_char/short/K&R prototypes for the 4th param; permuter only finds a fake label. Under the psx recipe the ledger best is 1902.
- unblock: Toolchain: adopt a cc1 = cdk minus reload_cse (earlier Cygnus 2.7.2 snapshot, or the patched binary as a toolchain entry) for libcd BIOS/C_011 (matched code is IDENTICAL under cdk, unitcmp.sh).
- notes: r13-cd, r9-libcd-libds

### SLUS_010.40/libcd/BIOS CD_cw (1,036 B)
- best: 3355 (`.work/ultracode/r13-cd/cw1_s.c`); ledger trials: 3; alt: diag best: .work/ultracode/r13-cd/cw1_s.c
- blocker: Compiler version: as CD_ready (cdk diag 173); `com`/`param` swap from cdk live lengths, plus `move a1,zero` for CD_sync(0,0) from reload_cse. 2.7.2-psx: no strength reduction of param[i] loop missing, no store-flag.
- tried: Same loop/callback/alarm forms as CD_ready; cw1/cw2/cw1_s..u.
- unblock: Same as CD_ready (real library cc1; cw1_s.c + cdk-nocse).
- notes: r13-cd, r8-lib-a

### SLUS_010.40/libpad/PADIF func_8002D924 (916 B)
- best: 505 (`.work/ultracode/r6-lib-pad-spu-etc/PIF/d924_c.c`); ledger trials: 4
- blocker: PADIF is a 2.7.2-cdk -mmips-as unit (r12-lib2 integ/PADIF.c). Under cdk the only diff is register priority: p gets s2 instead of s1 (p 19 refs/278 insns ranks below the &D_800335F4[port] pointer and the giv).
- tried: Decl order, port spelling, n vs D_80033620, loop forms; cdk permuter 20 min reaches 0 only with a `new_var = p;` copy (rejected). psx best 505 (return -3 cross-jump choice).
- unblock: First land the PADIF cdk unit switch (unlocks D6DC/D860); then more search for a natural shape that raises p's priority.
- notes: r12-lib2, r6-lib-pad-spu-etc

### SLUS_010.40/libcd/BIOS CD_ready (712 B)
- best: 1097 (`.work/ultracode/r13-cd/rd_b_s.c`); ledger trials: 2; alt: diag best: .work/ultracode/r13-cd/rd_b_s.c
- blocker: Compiler version: under cdk -mmips-as -mhard-float 98 (diag). Residual = global-alloc priority: cdk gives the mode parameter a live length of 162 insns where 2.7.2-psx gives 80, so params rank below the hoisted D_80032220 base; psx has the target's s-reg order but not cdk's reorg/loop. Floor ~50 is scorer-only (Alarm struct +4/+8 addends).
- tried: Alarm struct cast (fixed printf arg scheduling), while/break, do-while(mode==0), for(;;), ret variable, sotn/xg loop and callback forms, memcpy variants; `do {} while (0)` wrapper reaches the floor (not acceptable C). 5 permuter runs.
- unblock: Toolchain: the actual library cc1 (cdk-like reorg, no strength reduction, psx-like flow live lengths, no reload_cse). Then rd_b_s.c should match.
- notes: r13-cd, r8-lib-a, r10-lane6

### SLUS_010.40/libcd/BIOS CD_sync (640 B)
- best: 2439 (`.work/ultracode/r8-lib-a/cd/sync1.c`); ledger trials: 1
- blocker: Compiler version: as CD_ready (cdk diag 121). Under psx: loop.c does not hoist D_80032220/constant 2 into s-regs (threshold), queued post-increment.
- tried: sync1.c and later forms; permuter run (stopped).
- unblock: Same as CD_ready (sy_a_s.c).
- notes: r13-cd, r8-lib-a, r10-lane6

### SLUS_010.40/libcd/BIOS CD_init (480 B)
- best: 1124 (`.work/ultracode/r8-lib-a/cd/init4.c`); ledger trials: 6; alt: diag exact: .work/ultracode/r13-cd/var/i1.c
- blocker: Compiler version: CD_flush is inlined; volatile struct base folding (cse1 find_best_addr) and missing store-flag of the -1 tails under 2.7.2-psx. var/i1.c (three separate `if (...) return -1;`) is EXACT with the no-reload_cse cdk cc1, 45 under stock cdk (`move a2,a1`).
- tried: explicit/inline flush, ternary/||/&& tails (1124-1807 under psx).
- unblock: Same toolchain fix as StCdInterrupt (cdk without reload_cse) for libcd BIOS.
- notes: r13-cd, r8-lib-a, r10-lane6

### SLUS_010.40/libetc/INTR func_8001FB40 (464 B)
- best: 1122 (`.work/ultracode/r10-lane5/intr/fb40_c.c`); ledger trials: 4
- blocker: Under 2.7.2-cdk -mmips-as DIFF27: equivalent string/symbol relocs plus the 2nd printf `la a0` scheduled after the lhu loads. INTR is not cdk-safe (func_8001FD10 s-reg permutation). Under psx: env base reg / s4 = s1+4 struct-base CSE (S1).
- tried: fb40_c (`while ((mask = ...) != 0)` + u_short mask) 1122, fb40_e local env pointer 1613.
- unblock: Toolchain: INTR under cdk with func_8001FD10 re-spelled for cdk, then a sched fix for the printf argument; or the real library cc1.
- notes: r12-lib2, r10-lane5, r9-lib-rest, libetc-INTR

### SLUS_010.40/libgpu/SYS ResetGraph (372 B)
- best: 1379 (`.work/ultracode/lib-batch/GPUSYS/reset.c`); ledger trials: 3; alt: cdk-exact: .work/ultracode/r10-lane0/rg0.c
- blocker: Compiler version / reorg: target reorg steals `li v0,3`/`li v0,5` from the branch target into the case-tree `beqz` slot. 2.7.2-cdk and 2.8.1-psx do this on the same C; 2.7.2-psx leaves a nop. r10-lane0/rg0.c is exact under 2.7.2-cdk -mmips-as.
- tried: Switch tree shape fixed by the case values; flag sweeps (survey-libgpu). Recommended libgpu/SYS flags (-fno-rerun-cse-after-loop -fno-force-mem) do not fix it.
- unblock: Toolchain: libgpu/SYS under cdk -mmips-as regresses DrawOTag (reload_cse `li a3,0`->`move a3,a2`, no C fix) and func_80029B08; needs a cc1 between 2.7.2-psx and cygnus-970404 (newer cse/reorg, no reload_cse, psx alloc order).
- notes: survey-libgpu, r10-lane0, lib-batch

### SLUS_010.40/libcd/BIOS CD_datasync (360 B)
- best: 1770 (`.work/ultracode/r6-lib-gpu-cd-ds/cd/dsync1.c`); ledger trials: 7
- blocker: Compiler version: as CD_ready (cdk diag 121). Under psx: 0x3C0000 timeout constant hoisted into s4 in target only; queued post-increment copy of the poll counter.
- tried: struct alarm, volatile counter, operand orders, pre-increment, no-inline (same RTL); -mhard-float 804.
- unblock: Same as CD_ready (ds_s.c).
- notes: r13-cd, r10-lane6, r6-lib-gpu-cd-ds, lib-batch

### SLUS_010.40/libgpu/SYS func_8002A8F8 (336 B)
- best: 165 (`.work/ultracode/r6-lib-gpu-cd-ds/gpu/a8f8.c`); ledger trials: 3; alt: cdk-exact: r6 a8f8b.c (.work/ultracode/r6-lib-gpu-cd-ds/gpu/)
- blocker: Same reorg slot steal as ResetGraph (`li v0,3` stolen into the switch-tree beqz slot); otherwise identical (165).
- tried: Baseline re-runs; exact under 2.7.2-cdk -mmips-as (r6 a8f8b.c).
- unblock: Same libgpu/SYS compiler fix as ResetGraph.
- notes: survey-libgpu, r10-lane0, r6-lib-gpu-cd-ds

### SLUS_010.40/libgpu/SYS func_8002ABB8 (324 B)
- best: 112 (`.work/ultracode/libgpu-SYS/ABB8b.c`); ledger trials: 21; alt: cdk-exact: .work/ultracode/libgpu-SYS/ABB8v1.c
- blocker: Queued post-increment: target `lw v0; move a0,v0; addiu v0,v0,1` (protect_from_queue copy before the increment); 2.7.2-psx emits `lw a0; addiu v0,a0,1`. Exact under 2.7.2-cdk -mmips-as (libgpu-SYS/ABB8v1.c).
- tried: 21 ledger trials: array/scalar/struct counters, `n = cnt++`, operand orders, Alarm struct, volatile; diag -fno-rerun-cse/-fno-sched/-fno-expensive-opts, 2.8.1.
- unblock: Same libgpu/SYS compiler fix as ResetGraph.
- notes: survey-libgpu, r10-lane0, libgpu-SYS, lib-batch

### SLUS_010.40/libgpu/SYS func_800286B8 (284 B)
- best: 570 (`.work/ultracode/r6-lib-gpu-cd-ds/gpu/chk.c`); ledger trials: 1; alt: cdk-exact: .work/ultracode/survey-libgpu/cand/chk2.c
- blocker: Compiler version: exact under cdk -mmips-as (cand/chk2.c); under the recommended psx flags DIFF5 = only the h/fptr register pair in the shared tail printf.
- tried: chk..chk6 variants; -fno-rerun-cse-after-loop.
- unblock: Same libgpu/SYS compiler fix as ResetGraph.
- notes: survey-libgpu, r6-lib-gpu-cd-ds

### SLUS_010.40/libgpu/SYS func_80029924 (152 B)
- best: 1183 (`.work/ultracode/r6-lib-gpu-cd-ds/sweep/libgpu-SYS/9924v1.c`); ledger trials: 11; alt: cdk-exact: .work/ultracode/survey-libgpu/cand/cs1.c
- blocker: Mem-extend CSE (S2/S3): target loads screen w/h twice (lh+lhu) with no frame; 2.7.2-psx merges them and leaves a spurious 16-byte frame. cand/cs1.c (fully natural) is exact under 2.7.2-cdk -mmips-as.
- tried: 11 ledger trials (short/int params, casts, GpuEnv); best psx 1183.
- unblock: Same libgpu/SYS compiler fix as ResetGraph.
- notes: survey-libgpu, libgpu-SYS, r8-lib-a

### SLUS_010.40/libds/DSSYS_2 DsReady (148 B)
- best: 682 (`.work/ultracode/r6-lib-gpu-cd-ds/sweep/libds-DSSYS_1/DsReady.c`); ledger trials: 7; alt: GFE-exact: .work/ultracode/r8-lib-b/ds2/rdy_a.c
- blocker: cse folds `lbu 4(s0)` to a macro under the recipe; target keeps the `la s0` base across the call. Exact under GFE (rdy_a.c, in r9's DSSYS_2.gfe.c).
- tried: 7 ledger trials.
- unblock: Same DSSYS_2 GFE recipe patch.
- notes: r9-libcd-libds, r8-lib-b, r10-lane8

### SLUS_010.40/libds/DSSYS_2 DS_CQ_flush (96 B)
- best: 232 (`.work/ultracode/libds-DSSYS_1/DS_CQ_flush_b.c`); ledger trials: 16; alt: GFE-exact: .work/ultracode/r8-lib-b/ds2/cqf_e.c
- blocker: cse struct-base folding: exact under -fno-rerun-cse-after-loop -fno-expensive-optimizations (GFE); under the unit recipe the head load after the branch folds to a sym+K macro.
- tried: 16 ledger trials; integ/DSSYS_2.gfe.c in situ MATCH (25 functions) in r9.
- unblock: Recipe decision: apply r9 patches/libds_DSSYS_1_2_gfe.patch with the DSSYS_2 flag change (r8 found func_800244EC breaks under plain GF; r9's cleaned unit matches). Needs maintainer approval of a per-unit flag.
- notes: r10-lane8, r9-libcd-libds, r8-lib-b, libds-DSSYS_1

### SLUS_010.40/libds/DSSYS_2 func_80023360 (84 B)
- best: 122 (`.work/ultracode/libds-DSSYS_1/func_80023360_b.c`); ledger trials: 18; alt: GFE-exact: .work/ultracode/r8-lib-b/ds2/p360_f.c
- blocker: Same as DS_CQ_flush (base `la D_80039CF0+200` forms, head load folded to macro under the recipe).
- tried: 18 ledger trials (q1 local struct pointer, q2, q3).
- unblock: Same DSSYS_2 GFE recipe patch.
- notes: r10-lane8, r9-libcd-libds, r8-lib-b

### SLUS_010.40/libpad/PADMAIN _padSetVsyncParam (44 B)
- best: 485 (`.work/ultracode/r6-lib-pad-spu-etc/PM/vs_a.c`); ledger trials: 5
- blocker: Struct base CSE (`la D_8003FC04; sw -4/0/4/8`): exact under -fno-rerun-cse-after-loop (score 10 = reloc naming), but PADMAIN is not flag-safe (_padSioRW2's inlined chkRC2wait loop needs the post-loop CSE copy).
- tried: 5 ledger trials (pointer-to-func2 worse, 7305).
- unblock: Toolchain/recipe: a compiler that does both (library cc1), or a natural respelling of _padSioRW2 that is exact under the flag.
- notes: r12-lib2, r9-lib-rest, lib-near

## 4. Library compiler version, systemic traits no available compiler reproduces — 44 functions, 23,900 bytes

Unblock route: Toolchain fix: find or build the PsyQ library cc1 (older cse/loop/reorg behaviour); a few also need reconstruction.

### SLUS_010.40/libc/SPRINTF sprintf (2,180 B)
- best: 11527 (`.work/ultracode/r6-lib-pad-spu-etc/SP/sp_b.c`); ledger trials: 3
- blocker: Large residual: fmt and ap memory-resident (va_start &fmt; S4), '0' hoisted into s3, spec struct reconstruction; candidates 112-208 bytes short under every compiler.
- tried: sp_a/sp_b (full reconstruction: spec struct copied from D_80032854, signed char fmt/sign, srl+andi bit tests).
- unblock: Deeper reconstruction (needs decomp work, not a setting) + stdarg handling.
- notes: survey-libgpu, r6-lib-pad-spu-etc, r12-lib2

### SLUS_010.40/libgpu/SYS PutDispEnv (1,272 B)
- best: 3611 (`.work/ultracode/r9-libgpu-libc/gpu/pde2v.c`); ledger trials: 2
- blocker: S1 base reg s2=&level, reorg fill choices in the width class, and `lhu + sll/sra` on saved D.disp RECT compares reproduced only by a volatile GpuEnv (not adopted).
- tried: pde2 8126, pde2v (volatile) 3611; diag if/else, ternaries, inline cmprect, local RECT copy.
- unblock: Library cc1.
- notes: r9-libgpu-libc, survey-libgpu

### SLUS_010.40/libpad/PADCMD func_8002CF58 (1,016 B)
- best: 1504 (`.work/ultracode/lib-near/pad/func_8002CF58_c.c`); ledger trials: 5
- blocker: `buf[5] >> 7` is `lbu; sra 7` with a separate reload in target (every compiler here folds to srl); copy loop la-hoists &D_8003FC20 only for the copy.
- tried: func_8002CF58_c.c 1504; >>, /128, bitfields, int temp (gets sra but load CSE'd); cdk 1304.
- unblock: Library cc1.
- notes: lib-near, r6-lib-pad-spu-etc, r9-lib-rest

### SLUS_010.40/libgpu/FONT FntPrint (960 B)
- best: 3593 (`.work/ultracode/r9-libgpu-libc/font/p2a.c`); ledger trials: 2
- blocker: S4 va_list memory-resident (ap in a stack slot, loaded before the updated ap is stored) and S5 queued post-increment of fs->count. Reproducing S4 needs a private `*(char**)&(AP) += n` va_arg (not adopted).
- tried: p2 4245 (natural), p2a 3593 (address-taken va_arg), diag p3 worse; candidates do not build in splice under some settings.
- unblock: Library cc1 + stdarg decision.
- notes: r9-libgpu-libc, survey-libgpu

### SLUS_010.40/libds/DSSYS_2 _DsPacket2 (880 B)
- best: 2457 (`.work/ultracode/r9-libcd-libds/ds2/pk_j.c`); ledger trials: 3
- blocker: Loop pointer giv `&p->func`: target derives it from the pointer reg (s2 = s4+12), our cse2 folds the frame-address equivalence (sp+28); right under GF but 17 hunks remain.
- tried: pk_g/pk_i/pk_j (guarded do-while; local DS_PKT cmd[5]).
- unblock: Library cc1 + more reconstruction.
- notes: r9-libcd-libds

### SLUS_010.40/libpad/PADMAIN _padInitSioMode (820 B)
- best: 469 (`.work/ultracode/r6-lib-pad-spu-etc/PM/ism_d.c`); ledger trials: 2
- blocker: Body matches except a dead 8-byte frame slot (vars=8) from the multitap release loop's loop.c hoist (gone ONLY with -fno-rerun-cse-after-loop, which PADMAIN cannot take) and `return p->cmd == 0` folded to sltiu where target branches.
- tried: ism_d.c.
- unblock: Library cc1 (or NR-safe PADMAIN).
- notes: r6-lib-pad-spu-etc

### SLUS_010.40/libgpu/FONT FntFlush (796 B)
- best: 4078 (`.work/ultracode/r6-lib-gpu-cd-ds/font/flush2.c`); ledger trials: 2
- blocker: HImode temps: vars=72 frame vs target 24 (S3), struct bases (S1).
- tried: flush2.c; sweep.
- unblock: Library cc1.
- notes: survey-libgpu, r6-lib-gpu-cd-ds

### SLUS_010.40/libds/DSREADY func_80026360 (732 B)
- best: 2924 (`.work/ultracode/lib-batch/DSREADY/6360b.c`); ledger trials: 5
- blocker: Reg-for-constant: target keeps constant 1 in s3 and stores `request = 1` through it at a point shared with the n != pos path; our cse picks the intr register (also 1) so no cross-jump.
- tried: 6360b/c/d/e (goto-free, switch forms); 557 under both GF flags (diag).
- unblock: Library cc1 (cse tie preference for registers over constants).
- notes: r10-lane6, lib-batch

### SLUS_010.40/libds/DSSYS_1 func_80024BDC (720 B)
- best: none in ledger; ledger trials: 0
- blocker: Vsync callback with DS_cw_system inlined x4: target keeps constant 1 in s0 across calls and 11 in a1 and stores them (reg-for-constant); also macro store tail in jal/jr slot.
- tried: Analysis only, no C draft.
- unblock: Missing reconstruction + library cc1.
- notes: r9-libcd-libds, libds-DSSYS_1

### SLUS_010.40/libgpu/FONT FntOpen (696 B)
- best: 4429 (`.work/ultracode/lib-batch/FONT/open.c`); ledger trials: 3
- blocker: Struct base registers (s0 = s1-0x10, S1) and frame layout; ~170 words off under every setting.
- tried: open.c; flag/compiler sweep (sweep_FONT.txt).
- unblock: Library cc1.
- notes: survey-libgpu, lib-batch, r9-libgpu-libc

### SLUS_010.40/libgpu/SYS func_8002A3E8 (688 B)
- best: 584 (`.work/ultracode/r9-libgpu-libc/gpu/aq3.c`); ledger trials: 1
- blocker: _addque2: ONLY diff is S1, target `la a0,D_80033444` + 1/8/0xC(a0); ours sym+K. -fno-rerun-cse-after-loop keeps a base but picks the first-referenced address (D+8/-7/+4).
- tried: aq3.c (volatile queue fixes the j slot); aq4/aq5 pointer locals constant-propagated.
- unblock: Library cc1 (cse base cost).
- notes: r9-libgpu-libc

### SLUS_010.40/libds/DSSYS_1 func_80025228 (648 B)
- best: 2542 (`.work/ultracode/r9-libcd-libds/ds1/228_a.c`); ledger trials: 1
- blocker: Under GF 648/648 bytes, 17 hunks from one cse choice: after `if (intr == DslComplete)` our cse makes the zero-extended intr temp canonical for value 2 (stores sys[0]=2 from a0); target uses the `li 2` compare register.
- tried: K&R params, switch(intr), switch(sys[1]).
- unblock: Library cc1 (cse register-for-constant preference).
- notes: r9-libcd-libds, r8-lib-b

### SLUS_010.40/libgpu/SYS func_8002A054 (640 B)
- best: 2170 (`.work/ultracode/r9-libgpu-libc/gpu/drs1.c`); ledger trials: 1
- blocker: _drs: clamp lh+lhu (S2) + spurious vars=32 frame (S3, combine leaves a (use) of a dead shift temp); rest identical.
- tried: drs1.c.
- unblock: Library cc1.
- notes: r9-libgpu-libc

### SLUS_010.40/libspu/SPU _spu_t (640 B)
- best: 476 (`.work/ultracode/gas-lib-A/SPU/t_4a.c`); ledger trials: 21
- blocker: reorg: missing `li v0,2` steal into the switch-tree beqz slot (reproduced by 2.7.2-cdk and 2.8.1; not by 2.7.2-psx) + case-2 register allocation. cdk/2.8.1 are not drop-in for libspu (global address forms).
- tried: 21 ledger trials; fsweep over ~25 flags.
- unblock: Library cc1: 2.7.2-psx-like with a post-970404 reorg.c.
- notes: r10-lane8, gas-lib-A

### SLUS_010.40/libpad/PADPORTD func_8002E04C (636 B)
- best: 1301 (`.work/ultracode/lib-batch/PADPORTD/e04cb.c`); ledger trials: 2
- blocker: Struct-base / constant differences (survey: 1605-1645 under other compilers).
- tried: e04cb.c.
- unblock: Library cc1.
- notes: lib-batch, survey-libpad-etc

### SLUS_010.40/libgpu/SYS func_80029694 (624 B)
- best: 4159 (`.work/ultracode/r6-lib-gpu-cd-ds/gpu/9694.c`); ledger trials: 1
- blocker: HImode/QImode temporaries: our cc1 allocates spurious stack slots and merges loads; target has double loads, no slot.
- tried: 9694.c (one draft).
- unblock: Library cc1.
- notes: r6-lib-gpu-cd-ds

### SLUS_010.40/libgpu/SYS func_8002A698 (608 B)
- best: 2691 (`.work/ultracode/r6-lib-gpu-cd-ds/gpu/a698.c`); ledger trials: 2
- blocker: S1 (`la v1,D+8` then `addiu v0,v1,-8`) plus loop-invariant hoisting of 0x04000000/0x01000000 into s-regs.
- tried: a698.c.
- unblock: Library cc1.
- notes: r9-libgpu-libc, r6-lib-gpu-cd-ds

### SLUS_010.40/libds/DSSYS_2 DsCommand (580 B)
- best: 2338 (`.work/ultracode/r6-lib-gpu-cd-ds/ds/cmd3.c`); ledger trials: 8
- blocker: Our cse/jump folds the second `if (param)` (known non-zero) away, target keeps it; DslSetloc inline arg constant kept in an s-reg by ours, target rematerialises `li v0,2`.
- tried: cmd_a.c, cmd3/cmd4.c (2338-2428).
- unblock: Library cc1 (no known-non-zero folding).
- notes: r8-lib-b, r6-lib-gpu-cd-ds

### SLUS_010.40/libgpu/SYS func_80029E18 (572 B)
- best: 2838 (`.work/ultracode/r6-lib-gpu-cd-ds/gpu/9e18.c`); ledger trials: 2
- blocker: _dws: HImode/QImode temp issue (S2/S3).
- tried: 9e18.c.
- unblock: Library cc1.
- notes: r6-lib-gpu-cd-ds

### SLUS_010.40/libgpu/SYS func_80029BE8 (560 B)
- best: 3111 (`.work/ultracode/r9-libgpu-libc/gpu/clr1.c`); ledger trials: 1
- blocker: _clr: S2/S3 plus packet base registers folded (S1).
- tried: clr1.c 3111; diag clr2 (direct indexing), clr3 (pointer locals) constant-folded.
- unblock: Library cc1.
- notes: r9-libgpu-libc

### SLUS_010.40/libds/DSSYS_2 func_80023614 (548 B)
- best: 1226 (`.work/ultracode/r9-libcd-libds/ds2/614_e.c`); ledger trials: 2
- blocker: Sync callback: `q = &D_80039CF0.q[next]` keeps the q base; cse1 folds `sb intr,4(v0)` (flag is only a partial emulation); store-flag of `c ? x : 0` absent in target.
- tried: 614_e.c 1226, 614_h.c 1327; inline func_800244EC body.
- unblock: Library cc1.
- notes: r9-libcd-libds

### SLUS_010.40/libpad/PADMAIN _padSioRW (528 B)
- best: 361 (`.work/ultracode/r6-lib-pad-spu-etc/PM/rw_f.c`); ledger trials: 4
- blocker: After the int-temp sra fix: s1/s2 swap (baud/recv) and the final `move v0,recv` placement; fixed only with `do {} while (0)` barriers (loop-depth ref weighting).
- tried: rw_f.c 361; rw_t/rw_u diag barriers; permuter 45 with empty do/while (rejected).
- unblock: Library cc1, or policy on SIO_SET_BAUD-style do/while(0) macros (plausible PsyQ macro).
- notes: r6-lib-pad-spu-etc, lib-near

### SLUS_010.40/libgpu/SYS SetDrawEnv (504 B)
- best: 1243 (`.work/ultracode/r6-lib-gpu-cd-ds/gpu/sde2.c`); ledger trials: 2
- blocker: S2 mem-extend CSE (lh+lhu double loads) + S3 spurious HImode-temp frame; sde2.c is DIFF6 under cdk.
- tried: sde2.c; flag sweeps.
- unblock: Library cc1.
- notes: survey-libgpu, r8-lib-a, r6-lib-gpu-cd-ds

### SLUS_010.40/libds/DSSYS_2 func_800233B4 (496 B)
- best: 740 (`.work/ultracode/r12-lib1/DS2/b4m.c`); ledger trials: 2
- blocker: Structure right; head/len base CSE and loop details differ.
- tried: b4_b.c, b4m.c.
- unblock: Library cc1.
- notes: r8-lib-b, r12-lib1

### SLUS_010.40/libds/DSSYS_2 DsSync (496 B)
- best: 3853 (`.work/ultracode/r9-libcd-libds/ds2/sy_d.c`); ledger trials: 3
- blocker: Store-flag for the not-found ternary absent in target; our loop.c treats `n = 0` (in `if (++n >= 8) n = 0`) as a biv set and strength-reduces n*16, target recomputes sll each pass (GFE 14 hunks).
- tried: sy_a, sy_d (inline DS_sync_status / DS_get_result).
- unblock: Library cc1 (older loop.c).
- notes: r9-libcd-libds, r8-lib-b

### SLUS_010.40/libds/DSSYS_1 func_80025044 (484 B)
- best: 5515 (`.work/ultracode/r8-lib-b/ds1/5044_b.c`); ledger trials: 1
- blocker: Target re-reads D.com for the switch, ours CSEs it (+ a0/a2 swap at entry). GF near-exact; unit recipe has no base CSE (5515).
- tried: 5044_b.c.
- unblock: Library cc1 (mem re-read kept).
- notes: r8-lib-b

### SLUS_010.40/libds/DSSYS_1 func_80024A04 (472 B)
- best: none in ledger; ledger trials: 0; alt: diag draft: .work/ultracode/r8-lib-b/ds1/a04_f.c
- blocker: Reg-for-constant (case DslStandby stores the loaded stat[3] register) and target re-loads D.com for D_800326D0[com] after `if (com == DslNop)` where our cse folds it.
- tried: a04_f.c (13 hunks, diag only; no ledger trial).
- unblock: Library cc1.
- notes: r8-lib-b

### SLUS_010.40/libcd/C_011 func_80022EAC (424 B)
- best: 3588 (`.work/ultracode/r6-lib-gpu-cd-ds/cd/2eacb.c`); ledger trials: 4
- blocker: Once-used 5th (stack) parameter chcr: target reloads it from the incoming arg slot (2.8-like) and loads intr with lbu into an extended pseudo; 2.7.2-psx gives chcr s5 (frame 56 vs 48). 2.8.1 reloads but differs in address forms (mixed traits).
- tried: K&R vs prototyped (identical), 2eacb.c.
- unblock: Library cc1.
- notes: r10-lane4, r9-libcd-libds, r6-lib-gpu-cd-ds

### SLUS_010.40/libpad/PADPORTD PadInitDirect (352 B)
- best: 323 (`.work/ultracode/lib-batch/PADPORTD/init_b.c`); ledger trials: 2
- blocker: Target keeps p in s0 and stores sendBuf via 0x30(s0) (S1); NR diag 218 leaves s0/s1/s2 permutation and loop giv init.
- tried: init_b/init_h; permuter (NR) 180 only via a garbage `if (p)`.
- unblock: Library cc1.
- notes: r9-lib-rest, lib-batch

### SLUS_010.40/libds/DSSYS_2 DsInit (324 B)
- best: none in ledger; ledger trials: 0
- blocker: Same body as DsReset (+ DS_init + callbacks): DS_RESQ clear-loop biv elimination plus macro store tail in jal/jr delay slot.
- tried: No ledger trial (analysis with DsReset).
- unblock: Library cc1 (no biv elimination), then a C draft.
- notes: r9-libcd-libds, libds-DSSYS_1

### SLUS_010.40/libds/DSREAD DsRead (304 B)
- best: 2663 (`.work/ultracode/gas-lib-B/DSR/DsRead.c`); ledger trials: 5
- blocker: S1 struct-base CSE: target la-CSEs the ctx struct base across a branch with +8/-16 offsets and fills the s-reg epilogue; 2.7.2-psx folds sym+offset back into macro loads unless the pointer lives across calls (then wrong s-reg).
- tried: 5 ledger trials; local pointer forms (func_800260A8_b.c pattern).
- unblock: Library cc1 (older ccpsx cse cost model).
- notes: gas-lib-B, libds-READ, r10-lane5

### SLUS_010.40/libcard/FORMAT func_8002F154 (304 B)
- best: 692 (`.work/ultracode/gas-lib-B/FORMAT/f154_n.c`); ledger trials: 29
- blocker: Checksum loop `xor v0,a1,v0; move a1,v0` + hoisted &verify only appear with 2.8.1/cdk (DIFF20), but those run reload_cse_regs while the target has s1=0 and a1=0 as separate `li`s; constant hoisting into s5-s7 never reproduced.
- tried: 29 ledger trials (f154_i/n/p/q); psx -mhard-float DIFF61.
- unblock: Library cc1 (cdk-like without reload_cse, same as libcd).
- notes: r12-lib2, lib-pad-card-api, gas-lib-B

### SLUS_010.40/libds/DSSYS_1 func_80024F34 (272 B)
- best: 1878 (`.work/ultracode/r8-lib-b/ds1/f34_c.c`); ledger trials: 1
- blocker: Reg-for-constant: target stores u50 = 1 from the register a1 holding 1 for the `com != DslNop` compare (across a label); our cc1 rematerialises li. 1 hunk under GF.
- tried: f34_c.c.
- unblock: Library cc1.
- notes: r8-lib-b, r9-libcd-libds

### SLUS_010.40/libds/DSSYS_2 DsReset (268 B)
- best: 1758 (`.work/ultracode/r9-libcd-libds/ds2/rst_b.c`); ledger trials: 1
- blocker: Under GF 264/268, 3 hunks: our loop.c eliminates the reversed DS_RESQ biv (bgez on offset giv); target keeps the 7..0 counter.
- tried: do-while, count-down for, u_int, shared i (rst_b..g).
- unblock: Library cc1 (older loop.c).
- notes: r9-libcd-libds, r10-lane8

### SLUS_010.40/libpad/PADPORTD func_8002DE8C (244 B)
- best: 1187 (`.work/ultracode/lib-batch/PADPORTD/de8cb.c`); ledger trials: 2
- blocker: Target hoists -9 into s3 (constant hoisting) and keeps the arg in a1.
- tried: de8cb.c.
- unblock: Library cc1 (loop/constant cost).
- notes: lib-batch

### SLUS_010.40/libpad/PADMAIN func_8002C438 (240 B)
- best: 691 (`.work/ultracode/lib-batch/PADMAIN/c438b.c`); ledger trials: 5
- blocker: Target `la a1,D_800335E0; lw 0(a1) ... sw 0(a1)` (S1); ours macros under R and GF; PADMAIN not GF-safe.
- tried: c438_a.c, c438b.c.
- unblock: Library cc1.
- notes: r8-lib-b, lib-batch

### SLUS_010.40/libds/DSSYS_2 func_80023214 (236 B)
- best: 403 (`.work/ultracode/r8-lib-b/ds2/3214_b.c`); ledger trials: 4
- blocker: a2/a3/t0 permutation; head reload at loop top is a macro in target; final tail = head is base-relative in target (S1).
- tried: 3214_b (guarded do/while with id back edge) 403 (prior 1786).
- unblock: Library cc1 (base CSE), then reg-alloc search.
- notes: r8-lib-b

### SLUS_010.40/libpad/PADMAIN _padStartCom (204 B)
- best: 1060 (`.work/ultracode/lib-batch/PADMAIN/start.c`); ledger trials: 2
- blocker: Pointer not reloaded / D_8003FC10 base (S1).
- tried: start.c.
- unblock: Library cc1.
- notes: lib-batch

### SLUS_010.40/libspu/SPU _spu_FiDMA (188 B)
- best: 103 (`.work/ultracode/lib-near/spu/_spu_FiDMA_c.c`); ledger trials: 14
- blocker: reorg compensation `addiu v1,v1,-1` after the timeout loop: 2.7.2-psx mark_target_live_regs thinks v1 is live on the fall-through (scan stops at the first conditional jump).
- tried: 14 ledger trials (volatile callback needed); fsweep ~25 flags all 10 difflines.
- unblock: Same newer-reorg library cc1 as _spu_t.
- notes: r10-lane8, gas-lib-A, lib-near

### SLUS_010.40/libcd/C_009 StGetNext (184 B)
- best: 1 (`.work/ultracode/gas-lib-B/CD/StGetNext_b.c`); ledger trials: 15
- blocker: reorg: target branch goes to the join `lhu v1,0(a2)`; our reorg (relax_delay_slots/redundant_insn) skips the redundant load and branches one insn later. -fno-delayed-branch puts the label right (diag).
- tried: 15 ledger trials incl. volatile header (243), post-join volatile (121), volatile int temp (413).
- unblock: Library cc1 reorg (same family as _spu_t / ResetGraph steals).
- notes: r10-lane3, gas-lib-B, lib-cd-stream-ds

### SLUS_010.40/libspu/SPU _spu_FsetRXXa (164 B)
- best: 270 (`.work/ultracode/gas-lib-A/SPU/fsa_s2.c`); ledger trials: 9
- blocker: Assembler version: target remu expansion is `divu; bnez; nop; break` (old aspsx / maspsx --expand-div order), the gas recipe emits `bnez; divu; break`; plus one `move v1,a3` copy.
- tried: 9 ledger trials (switch form fsa_s2 270).
- unblock: Toolchain: an expand-div order matching aspsx under the gas pipeline (maspsx --expand-div is not in the unit recipe).
- notes: gas-lib-A, lib-near

### SLUS_010.40/libgpu/SYS func_800299BC (152 B)
- best: 1203 (`.work/ultracode/r9-libgpu-libc/gpu/ce1.c`); ledger trials: 1
- blocker: get_ce: S2/S3 (lh+lhu double load, spurious frame), same as func_80029924 (which is cdk-exact).
- tried: ce1.c; cs1/cs2 diag (cse still merges).
- unblock: Library cc1; try cs1-style source under cdk.
- notes: r9-libgpu-libc, libgpu-SYS

### SLUS_010.40/libds/DSSYS_1 DS_cw (124 B)
- best: 1363 (`.work/ultracode/libds-DSSYS_1/DS_cw.c`); ledger trials: 5
- blocker: Target keeps a second base a3 = &D_8003267C (a2-68) for state/sys[1]; GFE leaves 4 hunks.
- tried: 6 forms (cw_e.c and later).
- unblock: Library cc1 (base CSE).
- notes: r8-lib-b, r9-libcd-libds, libds-DSSYS_1

### SLUS_010.40/libds/DSSYS_1 DS_stop (120 B)
- best: 3206 (`.work/ultracode/r6-lib-gpu-cd-ds/sweep/libds-DSSYS_1/DS_stop_b.c`); ledger trials: 11
- blocker: Target tests 11, 17, 16 sequentially and the body gets a fresh la base; 2.7.2 fold_range_test reassociates (11||17)||16 and range-merges 17/16 (although func_80025228's target does fold a range: unexplained).
- tried: 11 ledger trials; duplicated-if form gets the order but cse reuses li 11; GFE 3 hunks.
- unblock: Library cc1; unexplained fold difference.
- notes: r8-lib-b, r9-libcd-libds, libds-DSSYS_1

## 5. Register allocation priority or preference (correct compiler) — 7 functions, 5,040 bytes

Unblock route: More search for the original source shape.

### BATTLE.PRG/146C func_80074BAC (1,548 B)
- best: 40 (`.work/ultracode/r7-near-b/74bac/p40.full.c`); ledger trials: 35
- blocker: Global-alloc priority: cell/actor s5/s6 swap (+ dependent v0/v1 in the vs_battle_actors address). Ref counts match the target (cell 22, actor 13), so live lengths must differ (ours cell 428/actor 150; need actor >= 190 or cell <= 338). Live lengths are fixed by code shape.
- tried: 35 ledger trials; position-view param (1025), no position var (1782), permuter runs (75/85/215/40 via unnatural actor copies); r12 bc1 dead store 40.
- unblock: More search for the original shape around the door block (actor live there), or policy on a dead conditional store (same family as func_8006CB04; still 40 then).
- notes: r12-e, r11-battle-b, r10-lane2, r9-146C, r8-146C, r7-near-b

### BATTLE.PRG/771A8 func_800E3600 (1,480 B)
- best: 20 (`.work/ultracode/r8-6E644-tail/e3600/vt10.c`); ledger trials: 87
- blocker: Global-alloc preference: the radius-clamp pseudo gets a3 (copy preference from the func_800E49D8 argument) where the target has v1; no conflict with a3 is possible in the target's live ranges.
- tried: 87 ledger trials; separate clamp local (180), inline wrapper (20/180); permuter 900 s.
- unblock: Unclear; hypothesis is 2.8.1 prune_preferences behaviour. Needs a form without the a3 copy preference.
- notes: r12-b, r11-battle-a, r10-lane5, r8-6E644-tail

### BATTLE.PRG/146C func_8008CEB0 (1,040 B)
- best: 10 (`.work/ultracode/r11-battle-b/ce15.c`); ledger trials: 95
- blocker: Local vs global allocation: the quotient pseudo is block-local (sra+neg in one block) and local-alloc gives it v0 before global runs; target needs it global so set_preference ties it to the neg (height a1, unkAC load a0, quotient v1).
- tried: 95 ledger trials (r10-r12): reused variable x, nested if, dead init, altitude forms (all 10); permuter 3498 iterations.
- unblock: More search for a form that makes the quotient a global pseudo without dead code.
- notes: r12-a, r11-battle-b, r10-lane2

### BATTLE.PRG/40564 func_800AB098 (532 B) — EXACT CANDIDATE (r15), pending integration
- best: 0 (`.work/ultracode/r15-E0A68-AB098/ab/r3.c` ledgered; `ab/final.c` = r3 minus the unused `page` local; in situ text/data/rodata identical, 0 reloc diffs; negative control differs). Patch: `r15-E0A68-AB098/patches/40564_func_800AB098.patch` (also moves D_800E8F30/90 externs and vs_battlePacketBegin/End above the function).
- what it took (r15): (1) fallback via `offset = index * 4; fallback.x = actor->unk614[offset]; ...[offset + 1]; ...[offset + 2];` fixes the `addu v0,t6,v0` operand order and leaves actor (8 refs/140) and srcY (6/70) at an exact 1714 global-alloc priority tie, which the lower allocno (the actor param) wins; (2) permuter (h1, -j1) found the tie-breaker: the srcY variable is reused for the destination y, `srcY = (y << 8) + (row << 6) + py; SetDrawMove(packet, &rect, dstX + (x << 6), srcY);` (srcY gains refs -> actor t6, srcY t5). Variable reuse, same class as the e2 `frame` reuse: flag for review.
- notes: r15-E0A68-AB098, r13-40564, r12-f

### SLUS_010.40/libetc/INTR func_8001FA68 (216 B)
- best: 10 (`.work/ultracode/r10-lane5/intr/fa68_ll.c`); ledger trials: 5
- blocker: 2nd D_800320D4 load in v1 vs a0; identical under psx/NR/cdk (not a compiler-setting issue). Diag: declaring startIntrDMA as returning long long (v0:v1 occupied) gives 10 (reloc-only), i.e. v1 must be busy there.
- tried: 5 ledger trials; permuter 13k iterations.
- unblock: Find the real return type/usage of startIntrDMA that occupies v1 naturally (or accept the reloc-equivalent diag only if the type is justified).
- notes: r12-lib2, r10-lane5, r9-lib-rest, r6-lib-pad-spu-etc

### SLUS_010.40/libpad/PADCMD _padSetMainMode (152 B)
- best: 326 (`.work/ultracode/gas-lib-B/PADC/sm_e.c`); ledger trials: 19
- blocker: Target keeps two live copies of offs (s1 for the store, s3 un-extended for the compare, `andi` at the compare); ours merges or extends at the copy.
- tried: 19 ledger trials: K&R variants (393-613), u_short/u_long copies (326), int + (u_char) compare (408).
- unblock: More search / library cc1 (mem-extend behaviour).
- notes: r10-lane3, gas-lib-B

### SLUS_010.40/libspu/S_SNC SpuSetNoiseClock (72 B)
- best: 112 (`.work/ultracode/lib-spu-snd/SNC/a.c`); ledger trials: 18
- blocker: Register preference: target copies n_clock to v0 first and tests v0. Form a.c gives the exact layout but the param pseudo gets a0 (prefs only 4, conflicts with clock via the early `clock = 0`).
- tried: 18 ledger trials, 26 natural forms.
- unblock: More search (a form keeping both halves).
- notes: r10-lane8, lib-spu-snd, gas-lib-A

## 6. Instruction scheduling order (sched1/sched2 tie-breaks) feeding allocation — 10 functions, 11,372 bytes

Unblock route: More search; a sched1 simulator over block RTL would turn it into a search.

### BATTLE.PRG/44F14 func_800AD714 (3,416 B)
- best: 0 EXACT (`.work/ultracode/r15-AD714/final.c`), in situ ALL MATCH; patch `.work/ultracode/r15-AD714/44F14.patch`; ledger trials: 63
- fix (r15): block-local `D_800F4538_t* target;` in cases 5/7/8 with plain `target = D_800F4538[id];`. Single-set target makes the lw a sched1 birthing insn, so the script[2] load lands between addu and lw and local-alloc gives base v0 / address v1 with la first.
- integration: variadic prototype removed; func_800AE47C calls `((void (*)())func_800AD714)(arg0, &arg0->unk704, 2, var_a3);` (ignored 4th arg, a3 is set in the target).
- notes: r15-AD714, r13-AD714, r10-lane5, r8-58578-44F14

### BATTLE.PRG/771A8 func_800E0A68 (2,000 B) — EXACT CANDIDATE (r15), pending integration
- best: 0 (`.work/ultracode/r15-E0A68-AB098/e0/u/x4.c`, ledgered; clean form `r15-E0A68-AB098/e0/final_body.c`, in situ text/data/rodata identical, 0 reloc diffs; negative control differs). Patch: `r15-E0A68-AB098/patches/771A8_func_800E0A68.patch`.
- what it took (r15): (1) height pick: a dedicated `int actorY` (only set from the short actor y) and `else selectedHeight = (short)actorY;` -- the SIGN_EXTEND source blocks jump.c's `x = b; if (c) x = a` hoist, combine then drops the extension (actorY has 17+ sign bits); (2) `angle = ratan2(..); angle &= 0xFFF;` (+2 refs so the angle pseudo outranks `state` for s0); (3) tile-equal moves as `tileX = dest.x*128; deltaX = pos.x-64; moveX = tileX-deltaX;` same for Z, moveY last (X,Z,Y order); (4) that branch's height in its own block-scoped `floorY` (shared `height` gave case 7's height-py an a0 preference via the branch's `subu a0`, swapping step/square a0/a1; block scope makes floorY lose the 1887 priority tie to the 0xFF00FF mask pseudo).
- the r11-r13 sched1/birthing analysis was not needed: blocking the jump.c hoist (SIGN_EXTEND source) gives the target RTL directly.
- notes: r15-E0A68-AB098, r13-tail-big, r12-b, r11-battle-c

### BATTLE.PRG/146C func_8008AC78 (1,412 B)
- best: 308 (`.work/ultracode/r8-146C/8ac78/v16.c`); ledger trials: 46
- blocker: cse + sched1 across a call: target's `addiu v0,s3,40 ... move s4,v0` copy means frame+40 was computed BEFORE AddPrim(frame) and `line = T` after it; combine cannot merge across the call. One missing register copy remains.
- tried: 46 ledger trials (e1-e5, q2, q3, a1/a2), permuter 20 min strict.
- unblock: More search for the original cursor/line assignment order.
- notes: r12-a, r11-battle-b, r9-146C, r8-146C

### ENDING.PRG/D4 _renderText (988 B) — EXACT in r15 (pending integration)
- best: 0 (`.work/ultracode/r15-25AC-ENDING/rt_final.c`, ledgered); in situ text/rodata/data identical, 0 reloc diffs; negative control (rt56) differs. Patch: `.work/ultracode/r15-25AC-ENDING/patches/D4__renderText.patch`.
- fix: in the POLY_FT4 block, `poly->y0 = poly->y1 = y;` moved to directly after `x += element->width;` (before `y2/y3 = y + 3`). Mechanism: the stores after the width load (lhu) anti-depend on it, so the lhu becomes ready (backward) only after the earliest of them is scheduled; when that last store's value is a long-lived reg (y, not the birthing `y + 3` temp), the lhu is blocked one cycle and the only other ready insn is the AddPrim `a1 = poly` copy, which is then placed after the x0/x2 stores -> optimize_reg_copy_1 leaves x0/x2 on s8. (`u1/u3 = 16` in the same slot is also exact; y0/y1 reads more naturally.)
- previous blocker (r8-r13): sched1 tie-break; prior best 70 (`ending-screff2/text/rt56.c`).
- notes: r15-25AC-ENDING, r13-tail-big, r12-d, r10-lane1, r8-25AC-ENDING

### SLUS_010.40/25AC Sound_SetMusicSequence (936 B) — candidate in r15 (link-identical, pending gate)
- best: 30 (`.work/ultracode/r15-25AC-ENDING/smA.c`, ledgered); the 30 is only symbol+addend relocs (D_80036774 = D_80036770+4, D_80036728 = D_800366F0+0x38, D_800358FE = g_PushedMusicConfig+0x6E). In situ (insitu/snd, both Sound functions): sections same size, data/rodata identical, the 4 differing text words are LO16 immediates that resolve to the same addresses (relchk.py). Patch: `.work/ultracode/r15-25AC-ENDING/patches/25AC_Sound_LoadAkaoSequence_SetMusicSequence.patch`.
- fix: (1) `g_PushedMusicConfig.MusicId = 0;` instead of `D_800358FE = 0;` (same address; as a struct member the store keeps its order after `pConfig->PendingKeyOffMask = 0`). (2) `g_Sound_VoiceSchedulerState.KeyOffFlags |= ~mask & 0xFFFFFF & ~(A | D);` — fold() turns `(~mask & C) & X` into `~mask & (X & C)`, exactly the target tree, and the single statement gives the &g_Sound_VoiceSchedulerState base a low LUID (sched1 places it before the Active load, so local-alloc does not tie it to the hi reg). Every other spelling (`~mask & (X & C)`, separate freeVoices, casts, inline helper) is reassociated or scheduled differently.
- notes: r15-25AC-ENDING, r13-tail-big, r12-c, r9-25AC-40564-ENDING

### SLUS_010.40/25AC Sound_LoadAkaoSequence (876 B) — candidate in r15 (link-identical, pending gate)
- best: 10 (`.work/ultracode/r15-25AC-ENDING/akB.c`, ledgered); the 10 is only D_80036774 vs D_80036770+4 (same address, verified in situ with relchk.py). Patch shared with Sound_SetMusicSequence (see there).
- fix: same free-voice statement `KeyOffFlags |= ~mask & 0xFFFFFF & ~(A | D);` plus the Chrono Cross loop-init order `mask = 1; i = 0; pChannel = g_ActiveMusicChannels; pChannelOffsets += 0x20; while (i < 0x20) { ...; pChannel++; i++; mask <<= 1; }` (fixes `li s1,1`).
- notes: r15-25AC-ENDING, r13-tail-big, r12-c, r9-25AC-40564-ENDING

### SLUS_010.40/25AC func_8001369C (668 B)
- best: 85 (`.work/ultracode/r15-25AC-ENDING/i69_85.c`, ledgered); ledger trials: 10
- r15 findings: Chrono Cross Sound_Setup statement order (FmChannelFlags + Timer* right after FmVoiceFlags, CD_VOL_R before the SPU control read, VolFade before Volume, ControlLatches before LfoPhase) fixes the loop-1 giv placement: 1090 -> 295 -> 85. CC also reads the SPU control register into the loop counter, so the q.c reuse is the original shape. SPU register macros come from CC include/hw.h (SPU_MAIN_VOL_L/R 0x1F801D80/82, SPU_CTRL_REG_CPUCNT 0x1F801DAA) and need adding to src/include/hw.h.
- blocker: local-alloc swaps a2/a3: the &g_ActiveMusicChannels qty (hi tied to lo_sum, 5 refs) outranks the 0x1F801DAA qty (3 refs, born at block top because lui/ori is a double set), so it takes a2. Target needs the SPU address allocated first.
- tried (r15): 2665 + 542 + 2129 single-statement moves (mv/, mv2/, mv3/), pChannel at every init position, global pChannel / separate SFX pointer (h2 910, h7/h9 205), CC while loop (85).
- unblock: a source shape where the sym qty is not tied to its hi or has fewer refs, or the SPU address qty more refs/shorter life.
- notes: r15-25AC-ENDING, r13-tail-big, r12-c, r10-lane8, r9-25AC-40564-ENDING

### SLUS_010.40/libpad/PADCMD _padRecvAtLoadInfo (624 B)
- best: 407 (`.work/ultracode/r6-lib-pad-spu-etc/PC/rali_f0.c`); ledger trials: 3
- blocker: Case-4 return-0 tails: target sets v0=0 after the stores and cross-jumps into case 3's `j end; move v0,zero`; our sched hoists v0=0 above the stores. Needs PadPort `u_short curExId` at 0xE6.
- tried: 3 ledger trials (rali_f0 407); diag -fno-schedule-insns 2171.
- unblock: More search / library cc1 scheduling.
- notes: r10-lane3, r6-lib-pad-spu-etc

### SLUS_010.40/25AC func_80015BAC (240 B)
- best: 90 (`.work/ultracode/r11-battle-c/5bac/b1.c`); ledger trials: 22
- blocker: Saved-config pointer must conflict with the v1 holder to land in a3: needs sched1 load order (sA, aA, aK, sK) plus `voice = keyOffMask` copy placement. r15: the or-result `~(a8 & a12) | (s8 & s12)` is in a3 = the voice variable's register, so it is probably computed into `voice` (v70 form gets the target load order and saved->a3, but voices/voice registers swap and nor/and order differs: 525+).
- tried: 22 ledger trials; ~1500 screened expression shapes, 2 permuter runs (235 via dead code, rejected); r15: 144 CC-local forms (g1/), 42 voice-temp forms (g2/), permuter 20 min on qc0 (185, junk).
- unblock: global-alloc simulation of voice/voices priorities (r13 s25/sim.py could be adapted), then a matching source shape.
- notes: r15-25AC-ENDING, r13-tail-big, r12-c, r11-battle-c, r8-25AC-ENDING

### SLUS_010.40/libpad/PADCMD _padLoadActInfo (212 B)
- best: 240 (`.work/ultracode/gas-lib-B/PADC/_padLoadActInfo_b.c`); ledger trials: 12
- blocker: sched2: target puts the return `li v0,1` first in the block (stolen into the beqz slot) and `sll a1,2` late in the lbu delay; same under cdk/hard-float/NR.
- tried: 12 ledger trials (ret variable forms, statement orders l1-l4).
- unblock: More search / library cc1 scheduling.
- notes: r12-lib2, r10-lane3, gas-lib-B

## 7. cse / jump canonicalisation — 2 functions, 4,664 bytes

Unblock route: More search for an original construct (label or copy) that disappears by final.

### BATTLE.PRG/49268 func_800B28A8 (4,292 B)
- best: 230 (`.work/ultracode/r12-d/b28/v12.c`); ledger trials: 63
- blocker: cse1 skip-block path: the block-1 test jumps around block 1 (LABEL_NUSES(L1)==1, no used label inside), so block 2's `(plus scratch 100)` is replaced by the pre-call temp (move a0,s0 vs target addiu a0,s1,100). The original had an extra use of L1 at cse1 time that vanished by final. Other diffs are link-identical reloc forms.
- tried: 63 ledger trials (r6-r13): 2.7.2 TU move (87119716), unused int _[2] pad, static inline block-1 helper, do/while(0), duplicated block 2; g1 diag (`&& scratch != 0`) proves the mechanism; 3 x 20-min permuter runs.
- unblock: A natural construct leaving a used label between the block-1 test and block 2 at cse1 (e.g. the original 'scale matrix by actor scale' helper/macro), not a flag.
- r15: path '210 to 334' needs TWO AROUND skips (jump 247 around block 1 AND jump 289 around the second `||` clause to body label 299); breaking either suffices. The obstacle must also survive cse2 (rerun-cse-after-loop, same skip_blocks; LOOP_END notes only stop cse1) and vanish only in thread_jumps-after-cse2/flow/combine/jump2 - no natural form found. Invalidation route needs scratch or reg 155 re-set in block 1/clause 2 (steering).
- notes: r15-B28A8-B9DB4, r13-B28A8, r12-d, r11-battle-c, r10-lane7, r8-B28A8, r7-4A0A8-big

### BATTLE.PRG/4A0A8 func_800B9DB4 (372 B)
- best: 15 (`.work/ultracode/r10-lane3/b9_p15.c`, steering); natural best 87; ledger trials: 98
- blocker: cse canonical register (make_regs_eqv) in the applyState arms: target keeps `state` and a `flags = state` copy in separate registers, our cse(follow-jumps) folds the copy; plus sched1 hoists the return `v0 = 1` above the QI constant for `D_800F4C4E = 1`.
- tried: 95 ledger trials: flags reuse for the animation id (292 -> 87), char return types, per-arm calls (307/360), chained copies, else-first.
- unblock: More search for a natural applyState shape; region-1 needs state canonical in the OR arm and the AND arm not path-followed.
- r15: proved from 2.8.1 cse.c (make_regs_eqv: promote iff (last>bb_end || first<bb_start) && last(new)>last(old); T path processed first with larger bb_end) that no lifetime choice works while the AND arm is path-followed; its label needs >1 use through cse1 AND cse2. New: inline int setter for region 3 (87, constant arg substituted), ?: arms (87), state reused for the 0x80 test (496: A reads state but B substituted, copy dies).
- notes: r15-B28A8-B9DB4, r12-d, r11-battle-a, r10-lane3, r8-4A0A8-30DB0-40564

## 8. reorg / delay-slot fill in game code — 1 functions, 2,936 bytes

Unblock route: More search for a label placement before dbr.

### BATTLE.PRG/146C func_80075554 (2,936 B)
- best: 187 (`.work/ultracode/battle-146C/75554/a6.full.c`); ledger trials: 38
- blocker: reorg/jump2: target case-23 tail is `L610: li s0,64; L614: j b18` with the inner switch default branching to L614; ours has jump2's cross-jump label before the li, so fill_simple_delay_slots takes the li backwards (4 bytes short). Needs a pre-reorg label between `li s0,64` and the jump.
- tried: 38 ledger trials (a6 rewrite 7762 -> 187, goto moving, state-before-call 322); jump2 RTL dumps.
- unblock: A natural source form that places a label between li and j before dbr; none found (cross-jump labels always precede the matched li).
- notes: r12-e, r11-battle-b, r9-146C, r8-146C, r7-near-b
