# `pc_unimplemented` paths

`pc_unimplemented(name)` stops the game with `[UNIMPLEMENTED] name` and a
stack. Native code uses it where the original would do something the port
does not reproduce. This page lists every site, sorts it into one of three
classes, and says what was done with it.

- **(a) guard**: the input cannot happen with the game's data, or would be
  corrupt (truncated packets, out-of-range indices, buffer overflows). On a
  PS1 these mostly mean a crash, a hang or a write into unrelated memory.
  The diagnostic stays.
- **(b) untranslated**: game logic that normal play can reach. These are
  translated from the assembly and the stop is removed.
- **(c) unclear**: these have not been shown to be unreachable, and the
  original's behaviour cannot be reproduced exactly. The evidence collected
  so far is noted next to each one.

The audit started from 87 sites:
- 81 literal `pc_unimplemented("...")` calls in `pc/src` and `src/`
- the context-named `pc_require_native_callback`
- the trap stubs that `build_boot.py` generates for unlinked functions.

## Translated in this pass, class (b)

### Route continuation through a room link: L800DF8A4

- **Sites:** `battle_movement.c` `func_800E4B2C`, `battle_movement.c` `func_800E4B70` and `actor_update.c` `route_target`. All three printed "actor route continuation L800DF8A4".
- **Action:** `pc_route_continuation` (766FC.s L800DF8A4), reached from 7BDD4.s func_800E4B2C / func_800E4B70 and from 7C7E0.s L800E550C.
- **Byte 3 of the new step tile:** this comes from the caller's `$s2`. The original never initialises that register.
  - Along the route-record path, `$s2` is the record itself, so byte 3 is the top byte of its address.
  - On the other two paths the compiled C callers' `$s2` is not known. 0 is passed there.
  - Byte 3 cannot be observed. `func_800D954C` shifts it out, and the command consumers ignore it: `func_80075554`, `func_800A208C` and `func_800B6580` read only bytes 0-2, and nothing reads 0x5EF.

### Divisions by zero

All of these are hand-written `div $zero`, which has no zero check. The R3000 then sets LO to -1 for a non-negative dividend and to 1 for a negative one, and sets HI to the dividend.

| Site | Message | Action |
|---|---|---|
| `battle_gte.c` func_800C0B50 | target shape intensity divide by zero | R3000 quotient |
| `effect_particles.c` | effect particle projection depth zero | R3000 quotient |
| `effect_particles.c` `pc_curve_value` | invalid particle color curve (size 0 part) | R3000 remainder (HI = clock). The index ≥ 32 part stays a guard |
| `actor_blend.c` func_800B147C | invalid animation blend extent (duration 0 part) | R3000 quotient. The count ≤ 0 / > 42 part stays a guard |
| `boot_sdk.c` func_80098648 | zero background far clip | R3000 quotient (-1) |

### Reads of PS1 low RAM and other out-of-list reads

| Site | Message | Action |
|---|---|---|
| `battle_movement.c` func_800D954C | tile entry test against an actor without state | An actor without an AI state (attributes lack bit 0/2, func_800E6178) has a null `0x1F80037C` entry. The original then reads 0x94/0x174/0x178 from address 0, which is PS1 low RAM. Those bytes are now read through guest RAM |
| `actor_update.c` func_800E5308 | actor navigation state missing | Same null entry: the original loads the route record from address 0x18C. That is now read through guest RAM. On a PS1 it must be 0, or the game would write into its kernel |
| `actor_update.c` `pc_actor_scratch` | actor navigation grid missing | func_800E5308 does not test `D_800F58BC`. Word 4 of address 0 is now read through guest RAM |
| `battle_movement.c` func_800DBCEC | actor engagement with out-of-list hostile id | The walk starts at `&vs_battle_actors`. The first "node" id is a pointer. Its relation, model (0x800F4538 + id*4) and tile (+0x5C) are now read through guest RAM, including the KUSEG/KSEG1 mirrors |

### The ending: `vs_ending_exec`

- **Site:** the trap `build_boot.py` generated for `vs_ending_exec`, which main.c's `vs_main_showEndingAndReturnToTitle` calls after loading ENDING.PRG. The final event script reaches it (`vs_battle_script_specialOp` op 1).
- **Action:** the ENDING overlay (`src/ENDING/ENDING.PRG/D4.c`, `illustLbas.c`) is compiled into the battle builds, 32- and 64-bit:
  - Its globals are renamed `ending_<name>` after compiling, except `vs_ending_exec`. MENUF defines some of the same names (`_renderRiskbreakerRank`, the `renderTextures.h` functions).
  - Its data stays in guest RAM, where the loader puts ENDING.PRG (0x80068800, over BATTLE). It resolves against `build/data/ENDING/ENDING.PRG.elf`. Names MENU9 uses for other data (`_score`, `_mapCompletion`, ...) become `name__ending`.
  - Its statics restart on each load of ENDING.PRG (`pc_overlay_statics`, entry `ENDING/ENDING.PRG`).
  - 64-bit: its pointer imports are defined natively in ENDING's own group, separate from BATTLE's imports at the same addresses. The scratchpad slots use `psx_slot_get/set`. `_creditsElements` holds native pointers and is defined natively.
- **Fixes in D4.c (ROOD_PC):**
  - `sprintf("%03d")` into `char[2]`: the buffer is 4 bytes natively. The PS1 overran into unused frame space.
  - The PHANTOM PAIN logo mesh reads column 16 and row 4 of its 4x16 table. It now reads flat, as the PS1 does, so GCC cannot assume the loop stops early.
- **Texture depth 3:** the MISSION COMPLETE screen draws its ENDSCRnn background with `getTPage(3, ...)`. The GPU reads the "reserved" depth 3 as 15-bit. The software rasteriser used to stop with "reserved texture depth" there. It now reads 15-bit texels, like the hardware renderers.
- **Verified:**
  - From the field with the RPC `ending` (a developer jump, `room_warp.c`).
  - All 27,431 frames of the credits, 32- and 64-bit: the PHANTOM PAIN and VAGRANT STORY logos, the illustrations, the credit text, the copyright and "Fin.".
  - The skip (Start), the score/map/rank screen, and the title's clear-data save screen (declined).
  - `pc/tests/ending_smoke.py` (run.py suite `ending_smoke`) runs the ending twice from the save. The second pass must give the same score and rank, and the card copy must stay unchanged. It passes on 32-bit, 64-bit GL and 64-bit Vulkan.

A caveat for the low-RAM reads: the port does not emulate the PS1 BIOS/kernel area, so the words read there are 0 natively. On a PS1 they are kernel data. For the route record the two must agree, otherwise the PS1 would corrupt its kernel. For func_800D954C the kernel bytes would only change how close an actor without state may be approached.

## Kept guards: class (a)

Unless the notes say otherwise, a hit on any of these means corrupt data, or a state in which the PS1 itself would crash, hang or overrun a buffer.

| Site | Message | Class | Notes |
|---|---|---|---|
| actor_packets.c:11 | actor polygon vertex offset | a | packet data out of range |
| actor_packets.c:38, :261 | actor polygon stride | a | malformed packet |
| actor_packets.c:162 | negative actor material group count | a | malformed model |
| actor_shadow.c:154 | shadow vertex offset | a | packet data out of range |
| actor_vertices.c:17 | invalid actor vertex group extent | a | empty or >768 group: malformed model |
| actor_blend.c:49 | invalid animation blend extent | a | the do-while over count ≤ 0 would run away; records hold 42 bones |
| weapon_packets.c:11 | weapon polygon vertex offset | a | packet data |
| weapon_packets.c:115 | weapon polygon stride | a | packet data |
| weapon_packets.c:128 | split flag on non-quad weapon face | a | packet data |
| screff2_warp.c:15 | screen warp OT pointer | a | OT corruption |
| screff2_warp.c:62 | truncated screen warp polygon | a | OT corruption |
| screff2_warp.c:80 | screen warp OT cycle | a | OT corruption |
| boot_polygons.inc:125 | truncated polygon packet | a | GPU packet corruption |
| boot_lines.inc:41/46/52 | truncated line / unterminated polyline / truncated Gouraud vertex | a | GPU packet corruption |
| boot_runtime.c:90 | VRAM rectangle out of bounds | a | |
| boot_runtime.c:359/371/374 | negative disc LBA / disc read failed / unsupported sector format | a | the disc image is missing or broken |
| boot_runtime.c:388 | display dimensions | a | |
| boot_runtime.c:488 | truncated VRAM copy | a | |
| boot_runtime.c:564 | truncated sprite packet | a | (the sprite dimensions stop is gone: the size is masked to 10 / 9 bits as on the GPU, which MENU0's LV picker relies on) |
| boot_runtime.c:645/647 | OT pointer outside mapped memory / OT cycle | a | |
| boot_runtime.c:761 | (context) `pc_require_native_callback` | a | a callback field holds a non-native address. Used for the actor movement callback and 573B8.c |
| boot_runtime.c:775 | effect-specific PLG callback relocation | a | every PLG effect entry resolves natively (`pc_plg_effect_unresolved` is all 0), so this is now a build-time guard |
| boot_sdk.c:314 | event callback/table full | a | the only callback event the game opens is RCntCNT2 (25AC.c) |
| boot_sdk.c:323 | sound clock without the audio objects | a | build configuration |
| boot_sdk.c:398/410 | empty (reverse) ordering table | a | n ≤ 0 |
| boot_sdk.c:726 | background vertex index | a | |
| boot_sdk.c:765, room_geometry.c:369 | room group count | a | > 4096 groups |
| room_geometry.c:117/296 | room packet capacity | a | the packet buffer would overflow |
| room_geometry.c:230 | room polygon count | a | |
| room_geometry.c:504 | room-name table with no entries | a | the do-while would run away |
| room_lighting.c:54 | room light triangle count | a | |
| map_view.c:51/483 | map row buffer beyond scratchpad / circle clip | a | the PS1 would write past the 1 KB scratchpad |
| overlay_statics.c:93/102 | overlay file lookup / statics snapshot | a | build or overlay table mismatch |
| effect_particles.c:22 | invalid particle color curve | a | only curve index ≥ 32 is left (corrupt effect data). Size 0 is now translated |
| effect_particles.c:129 | unknown particle sprite command | a | effect script data |
| battle_helpers.c:13 | battle zero-fill invalid span | a | |
| battle_helpers.c:185/229 | actor evaluation / stat snapshot with empty actor list | a | the AI driver only runs with actors present (D_800F590C); Ashley is always listed |
| battle_helpers.c:358/373 | candidate sort > 256 / shuffle > 16 | a | candidate list bounds |
| battle_helpers.c:448 | AI driver with no active actors | a | the original spins forever (hang) |
| battle_movement.c:190 | actor engagement read outside guest RAM | a | unaligned or non-RAM address: a PS1 address or bus error |
| battle_movement.c:366 | actor movement dispatch index | a | 0x125 is 0-2 (func_800E6828), kind 0/1 |
| battle_movement.c:1506 | actor command beyond jtbl_800F16F8 | a | 18-entry table |
| battle_movement.c:1599 | obstacle point buffer overflow | a | the scan below shows at most 2 active section-12 objects per room (buffer 8) |
| battle_movement.c:1786 | func_800E4764 endless reaction scan | a | the original loops forever when byte 1 is set and no gait bit is (hang) |
| navigation.c:73 | navigation allocation failed | a | heap exhausted |
| navigation.c:124 | room exit count exceeds original buffer | a | scan: at most 2 active section-12 objects in any room (buffer 13) |
| navigation.c:156 | navigation relies on original stack address in v1 | a | scan: no room's route table entry 0 has bit 26 without bit 10; links exist in only 6 of 418 rooms (MAP014/055/077/084/150/429) |
| actor_update.c:47/64/96/143 | actor navigation index | a | ids ≥ 17. Ids come from the actor list (0-15) or event scripts naming actors |
| actor_update.c:138 | actor navigation list cycle | a | |
| actor_update.c:172 | actor navigation model missing | a | every listed actor has a model |
| actor_update.c:180 | actor navigation tile outside grid | a | |
| src 573B8.c:285 | vs_battle_memcpy size not a positive multiple of 8 | a | callers pass constant sizes |
| src 5BF94.c:7688 | effect command opcode out of range | a | 49-entry table, effect script data |
| traps.c (64-bit and battle builds) | `_Unwind_Resume` | a | C++ runtime import, never called |
| traps.c (battle builds) | `SystemError` | a | BIOS error call, from ENDING's `_addRenderer` when all 31 credit element slots are taken. The full credits never reach it. The PS1 would stop in the BIOS error handler |

## Class (c)

| Site | Message | Class | Evidence / reason |
|---|---|---|---|
| boot_runtime.c:607 | GPU command | c | Unhandled GP0 commands in OT packets: 0x02 (fill), 0x1F, 0xA0-0xBF, 0xC0-0xDF. The game's sources show no OT use. The enemy soak hit none (below) |

The non-battle `rood_boot` build also traps the battle functions (`vs_battle_exec`, `func_80069FC4`, ...). That is by design: that build has no battle overlay.

## Data scan (MPD files)

These "max/none" results come from scanning every MAPnnn.MPD on the disc:
- 512 files, 421 with a room section, 418 with a navigation table (section 6).
- **Links:** route-table entries with link bits (0x04000400) exist in only six rooms: MAP014 (zone 9 room 5, Wine Guild Hall), MAP055 (15/8), MAP077 (23/4), MAP084 (24/5), MAP150 (32/3) and MAP429 (no door into it).
- **Section-12 objects:** never more than 2 active per room.
- **Entry 0:** no room has the bit 26 without bit 10 pattern in entry 0.

## Enemy soak

`pc/tests/enemy_soak.py` warps into every room that has a door, using the Room Browser warp `room_goto {enemies: 1, scripts: 0}`. Link rooms go first. In each room it plays a seeded script for 1500 vsyncs, or 60 when the room has no actors:
- waits for the enemies,
- enters battle mode and attacks,
- walks,
- leaves battle mode.

When a warp gets stuck (Game Over, menus) it relaunches. `pc/tests/enemy_soak_report.py` summarises the run directories, and `--skip-done` lets a follow-up pass skip rooms already covered.

### Coverage (32-bit build, about 1 h 40 min of soak in total)

| Result | Count |
|---|---|
| Rooms attempted | 364 |
| Warped into | 363 |
| With actors | 254 |
| Vsyncs of enemy play | 401,000 |

- **Not reached:** zone 24 room 2. Arrival hands over to another room.
- **Zones covered:** 9, 11-17, 22-25, 28-32, 34-56. The full per-room list is in `enemy_soak_report.py` output.
- **L800DF8A4:** the soak never hit it naturally, so `pc_route_continuations` stayed 0. Gates are used only when an enemy walks across a link object.

### Findings

No `[UNIMPLEMENTED]` was hit. Four native crashes were found and fixed. Each one is a NULL access that a PS1 survives, because guest address 0 is RAM. Under `ROOD_PC` the access now goes to guest RAM 0x80000000, as `func_800A30A0` already did; commit-check reports "All files match".

| Room | Crash | Fix |
|---|---|---|
| zone 24 room 7 (Cracked Pleasures) | `func_800A4A88`: no matching platform (`func_800A4A24` NULL) | both copies of `func_800A4A24` (3A1A0.c, 40564.c) return address 0 |
| zone 24 room 7 | `func_800A70DC`: off-floor centre tile (`func_800A8D64` NULL) | read address 0 |
| zone 49 room 4 (A Knight Sells his Sword) | `func_800C0700`: target shape already freed | write to address 0 |
| zones 53/55 (Bonfire, Burning, Bootikens) | `func_8009DDF4`: a fire effect hits an empty actor slot | the actor is read at address 0 |

After the fixes, the crash rooms were played again twice: 3000 and 5000 vsyncs, two seeds. There were no findings.
