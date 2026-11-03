# D2 rows with no PC body: reachability review (2026-11-03)

Scope: the 21 rows in `pc/docs/porting/battle/FUNCTIONS.tsv` whose kind is
"D2: asm on the PS1; PC runs none: calls trap (pc_unimplemented)". Read-only
review; nothing was built or run. The PS1 assembly **is** in the repo
(`src/BATTLE/BATTLE.PRG/*.s`), so the "what it does" column comes from the
assembly itself plus the PC code that replaces its callers.

## Headline

**None of the 21 is a missing feature.** No PC code calls any of them, and all
21 are covered another way:

- 18 are helpers, inner steps, patched-code targets or alternate entry labels
  of a larger asm function. The PC replaces the larger function and
  reimplements the step as a static helper, inline code or a PC renderer
  variable. The helper often has a different name, so the table generator
  found no `func_XXXXXXXX` definition and marked the row "calls trap".
- 2 (`func_800988BC`, `func_800B16F4`) are jump-table bodies entered by `jr`
  from `func_8009874C` / `func_800B17F0`; the PC computes the matrix directly.
- 1 (`func_80098B1C`) has no caller in the repo at all.

`pc_unimplemented` (`pc/src/boot_runtime.c:122`) prints
`[UNIMPLEMENTED] <name> (battle frame N)`, dumps the stack and calls `exit(2)`.
No row's name is passed to it: the "calls trap" text is the table generator's
default when no C definition with the asm symbol's name exists
(`pc/tools/porting_tables.py:695`). Nothing links these symbols to a trap
stub either: grep finds no definition, declaration or reference to any of the
21 names in `pc/src`, `pc/include`, `src/**/*.c` or the build files, outside
comments. So reaching one cannot even trap; there is simply no PC call site.

Classes used: **unreachable (covered)** = behaviour exists on the PC under
another name, nothing calls the symbol. **unreachable (dead/patch)** = no PC
caller and no PC need. No row is reachable in normal play, reachable in rare
content, or unknown.

## Table

All files are under `src/BATTLE/BATTLE.PRG/` (asm) or `pc/src/` (PC). "PC
cover" is the code that actually does the work on the PC.

| Function | Asm | Callers (asm) | PC cover (file:line) | Reachability | What it does | Size to port | 
|---|---|---|---|---|---|---|
| func_80097388 | 2EA3C.s:98 | func_8009723C (room draw) | `room_geometry.c:187` `subdivide_polygon`, called from `pc_room_group_packets` (`room_geometry.c:~255`), itself called by `func_8009723C` (`room_geometry.c:363`) | unreachable (covered) | Per-polygon room packet submit incl. adaptive subdivision of near large polygons | none needed (XL if standalone) |
| func_8009797C | 2EA3C.s:498 | func_80097388 | `room_geometry.c:15` `decode_vertices` (triangle form) | unreachable (covered) | Unpack a room polygon's 3 delta-encoded vertices (signed bytes << gp shift) and UVs into registers | none (M) |
| func_80097A10 | 2EA3C.s:538 | func_80097388, func_8009723C | `room_geometry.c:15` `decode_vertices`, with RTPT/NCLIP inline in `pc_room_group_packets` | unreachable (covered) | Same unpack, interleaved with GTE RTPT + NCLIP for the first three vertices; leaves max depth and NCLIP area | none (M) |
| func_80097AEC | 2EA3C.s:598 | func_80097388 and itself | `room_geometry.c:~135` `subdivide_triangle` (comment names it) | unreachable (covered) | Recursive triangle subdivision, leaf = packet emit | none (L) |
| func_80097CF0 | 2EA3C.s:734 | func_80097388 and itself | `room_geometry.c:160` `subdivide_quad` (comment names it) | unreachable (covered) | Recursive quad subdivision | none (L) |
| func_80097F70 | 2EA3C.s:902 | AEC, CF0 | `room_geometry.c:64` `midpoint` (comment names it) | unreachable (covered) | Edge midpoint: byte colour/UV averages, arithmetic-shift XYZ | none (M) |
| func_80098014 | 2EA3C.s:946 | func_80097AEC/CF0/388, 9723C | `room_render.c:36` `pc_room_quad_visible` | unreachable (covered) | Screen-rect visibility test of 4 packed XY values; its immediates (0xE0, 0x140) are patched at run time by `func_800981B8` (self-modifying code); PC replaced by `clip_left/top/right/bottom` | none (M) |
| func_800980F8 | 2EA3C.s:1011 | same callers | `room_render.c:22` `pc_room_depth_bucket` | unreachable (covered) | Depth to ordering-table bucket with near/far bounds and bias, then links the packet into the OT; bounds patched by `func_80098160`/`func_8009817C` | none (M) |
| func_8009874C | 2EA3C.s:1455 | func_800985AC, func_80098648 | `boot_sdk.c:585` `RotMatrixYXZ_gte` (called from `pc_room_transform`, `boot_sdk.c:~752`) | unreachable (covered) | Build a Y-X-Z rotation matrix from packed angles with an indexed jump through `func_800988BC` stubs | none (L) |
| func_800988BC | 2EA3C.s:1551 | none: address taken by 8009874C (`jr` through a table of 0x20-byte stubs, `$t7`) | same as 8009874C | unreachable (dead/patch) | Jump-table body of func_8009874C: sign/swap variants for the sine/cosine lookup by octant | none (M) |
| func_800989B8 | 2EA3C.s:1617 | func_800985AC, func_80098648 | `boot_sdk.c:566` `compose_rotation` | unreachable (covered) | 3x3 matrix multiply through the GTE | none (L) |
| func_80098AB4 | 2EA3C.s:1683 | func_800983F8 | `rcos` (`boot_sdk.c`/libs; used at `boot_sdk.c:~877`) | unreachable (covered) | One-instruction entry: `a0 += 0x400` then falls into func_80098AB8, so cos(a) = sin(a+0x400) | none (M, trivial) |
| func_80098AB8 | 2EA3C.s:1687 | func_800983F8 | `rsin` (`boot_sdk.c:~876`) | unreachable (covered) | Sine from `_trig_table` (4096 per turn, octant folding) | none (M, trivial) |
| func_80098B1C | 2EA3C.s:1720 | **none** (no `jal`/`jr`/pointer reference anywhere) | none | unreachable (dead) | 5 instructions: load `vs_battle_roomData`, read word at +0x98, `jr` to that + 0x14. A leftover room-data hook stub. | S if ever needed |
| func_80098E50 | 2EA3C.s:1950 | func_80098D6C (seven calls) | `room_geometry.c:446` `tint_color` (comment names it) | unreachable (covered) | Fade a packet colour toward luminance with GTE DPCS, add signed offsets, clamp | none (M) |
| func_800B16F4 | 48C7C.s:~100 | none: address taken by func_800B17F0 (`$t7`, jump table) | `boot_sdk.c:654` `func_800B17F0` | unreachable (dead/patch) | Jump-table body for the actor rotation matrix (func_800B17F0): per-octant sign variants | none (L) |
| func_800E4604 | 7BDD4.s:~25 | func_800D96C8 (70D4C.s:241); falls from func_800E45F4 | `battle_movement.c:156` `func_800E45F4` (both tiles in one call); `func_800D96C8` at `battle_movement.c:904` | unreachable (covered) | Second-tile half of the squared tile-distance helper: unpacks tile B, subtracts, tail-jumps to func_800E4660 | none (M) |
| func_800E5158 | 7C7E0.s:~118 | func_800E5154 (falls into it with a3=1), func_800E5240, func_800E527C, others with a3=0 | `actor_update.c:61` `func_800E5154`; `actor_update.c:44` `func_800E5240`; checks inline in `func_800E527C` (`actor_update.c:~96`) | unreachable (covered) | Look up actor id in `D_800F5878`; return nav-state validity (a3=0) or 0/1/2 target-tile status (a3=1) | none (M) |
| func_800E51C8 | 7C7E0.s:~150 | func_800E527C (7C7E0.s:233) | `actor_update.c:74` `route_init` | unreachable (covered) | Copy actor nav state (0x484 bytes) into a route record and reset its fields | none (M) |
| func_800E6974 | 7E05C.s:~100 | func_800E68EC (7E05C.s:95 region), func_800DC574 via `L800E6958`, itself | `battle_helpers.c:336` `sort_candidates` (comment names it), used at `battle_helpers.c:360,383` | unreachable (covered) | Quicksort of AI candidate pointers by score, descending | none (L) |
| func_800E6BA0 | 7E05C.s:~250 | func_800E6C34 (7E05C.s:439) | `battle_movement.c:1597` `clear_of_obstacles`, used at `battle_movement.c:1676` | unreachable (covered) | Probe-point clearance against the room's obstacle points (0x40/0x20/0x40 box) | none (M) |

Function-pointer, event-script, effect and dispatch tables: none of the 21 is
in `function_tables.c` or any other table in `pc/src`, and there is no
`ROOD_PC` branch to bypass; the PC replacement is made by replacing the larger
asm function (`func_8009723C`, `func_800983F8`, `func_800985AC`,
`func_80098648`, `func_800B17F0`, `func_800D96C8`, `func_800E5154`/`5240`/`527C`,
`func_800E6C34`, `func_800DC3CC`, `func_800DC574`) wholesale.

## Notes

1. **Table classifier.** The generator marks a row "calls trap" whenever no PC
   function with the asm symbol's exact name exists
   (`pc/tools/porting_tables.py:686-699`). It cannot see static helpers (the
   room subdivision family, `tint_color`, `sort_candidates`, `route_init`,
   `clear_of_obstacles`) or fall-through/tail entry points. So the 21 count is
   a naming artefact, not a coverage gap. Suggested follow-up (not done here;
   read-only task): record the PC cover next to these rows so D2 does not budget
   work for them. `pc/docs/porting/battle/README.md:25` says "23"; the table
   has 21 (the other two, `_printFixedWidthFontChar` and `func_800E45B4`, are
   "PC runs fixed_font/runtime").
2. **Evidence limits.** I checked callers by reading the asm files and
   grepping every `.c/.h/.s/.inc/.py/.txt/.sh/.cmake` file in the repo, plus
   docs. The generator's `callers_*` columns for these rows are all 0 or
   "see asm" and were not used. I did not run anything, so "covered" means the
   PC code's comments and structure match the asm's role, not a byte-for-byte
   diff; the comments in `room_geometry.c`, `actor_update.c`,
   `battle_helpers.c` and `battle_movement.c` name several of these functions
   explicitly (97AEC, 97CF0, 97F70, 98E50, 800E5154, 800E51C8, 800E6974,
   800E6BA0, 80097388).
3. **Weakest cases.** `func_800E4604` is covered inside `func_800D96C8`, whose
   PC body approximates the approach test via a single SQR; its header comment
   documents the quirks, but I did not verify the second-tile arithmetic
   line by line. `func_80098AB4/AB8` are not mapped one-to-one to PC
   functions; `rsin`/`rcos` come from the SDK layer and I did not check them
   against `_trig_table`.
4. **func_80098B1C** is the only row with no PC cover and no caller. If a
   save/overlay ever sets `vs_battle_roomData+0x98` and something jumps to
   this address by number, it would matter; nothing in the repo does.
5. The PS1 asm was said not to be in the repo; it is, under
   `src/BATTLE/BATTLE.PRG/*.s`. No repo file contained instructions aimed at
   the reviewer.

## Counts

- Reachable in normal play: 0
- Reachable only in rare content: 0
- Unknown: 0
- Unreachable on the PC: 21 (18 covered by PC code under another name, 2 jump-table bodies, 1 dead stub).
