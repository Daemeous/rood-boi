# The memory lane: guest pointers, fixed addresses and the 2 MB RAM

Status: REVIEWED 2026-11-16, with the user's decisions folded in (two arenas, no structural serialisation, layout tables, measured estimates; sections 2.1, 2.2, 3, 6). Design only until each step is started. It covers ties T13
(4-byte guest pointers, PS1-layout structs), T15 (globals at PS1 addresses, raw memory in Zig) and T12 (the 2 MB
fixed RAM, the game heap, the low stack) of [PS1_Ties.md](../PS1_Ties.md), inside
[the off-the-PS1 plan](2026-11-08-off-the-ps1.md). Numbers are measured on main 9fcb0d12f (2026-11-16) unless
stated; sizes are rough agent days (one worker, targeted tests, one full verify per merge).

## 1. What is actually left (the survey changes the plan)

The ties map (2026-11-08) counted 282 guest-pointer fields and 2,921 accesses. Today the lint says 8 and 38. T13 as
"pointer width" is nearly done: structs the game builds itself already have native layout and 8-byte pointers;
only structs whose bytes overlay a file, the scratchpad, a save or an SDK structure keep `psx_ptr`. What keeps the
game on a fake PS1 is not pointer width but four couplings that the lint barely sees:

| # | Coupling | Measure | Why it holds |
|---|---|---|---|
| A | Everything allocated lives in a 970 KB arena inside the 2 MB RAM at 0x8010C000 (`vs_main_allocHeap` 65 call sites, `allocHeapR` 153, `freeHeap` 176) | 0 of it can grow; a pointer above 4 GB is fatal (`psx_addr`, `pc_psx_bad_pointer`) | `psx_ptr` fields hold 32-bit addresses, so every heap object must sit below 4 GB; the game stack is pinned at 0x60000000 for the same reason |
| B | Loaded files are used in place: MPD, SHP, SEQ, WEP, ARM, ZND, effect P-files, menu data are read into the heap or a slot and cast to structs whose "pointers" are offsets relocated in place | the structs are the PS1 layout by necessity | the byte image of the file is the object; 116 pinned globals are pointed at by words inside such files |
| C | 956 data imports; 655 already native, 301 pinned (139 in the effect plug-in slot placed by the link, 162 at their PS1 address) | `lp64_imports.json` lists each with the reason | arrays of unknown length, pointed-at-by-data, GPU packet storage, overlay slots (T10) |
| D | Code reaches memory by PS1 byte offset | `zig_raw_memory` 944 (`psx.fieldAt`, `psx.byteAt`, `@ptrFromInt`), `address_as_integer` 195, `PSX_OFFSET` in C, 2,024 `@ptrCast` | the decomp indexes past a field into its neighbours; layouts cannot change while this holds |

And the host depends on all of it: the snapshot saves RAM, the scratchpad, the native statics sections and the game
stack at pinned addresses (`snapshot.c`); the RPC and the tests find things with `nm` plus a raw `read(address)`
(`rood_rpc.py symbols()`, 42 test files, 174 such uses in `classic_parity.py` alone); the Oracle digests 4 KiB pages
by address; `ownership.toml` keys every Zig function by its PS1 address.

So the lane is: make code use names not offsets (D), put host-owned storage where addresses do not matter (A, C), turn
files into decoded native objects (B), and only then delete the RAM block and its fixed addresses. T14 (the
scratchpad, 52 sites) rides along as its own small step.

## 2. Design

### 2.1 The one rule that keeps snapshots working: pinned arenas

Save states, rewind and the Oracle work today because all game state is "a memory image": RAM plus statics sections
plus the call stack, at addresses that are the same in every process. Real 64-bit pointers into an arbitrary `malloc`
heap would break that. The design keeps the property and drops the PS1 geometry:

- The game's allocations come from two host-reserved arenas (decision 2026-11-16): the *game heap arena*, which keeps the
  original's allocator, size and behaviour (provably identical, 2.2), and a separate *port arena* for the decoded
  file objects of S6, so decoding never changes the game's allocation pattern. Both are mapped at fixed addresses chosen by us (above 4 GB,
  far from the engine image and the stack; `snapshot_layout.py` already pins code/state sections, `map_guest` and
  `reserve_game_stack` show the pattern, including the Windows `VirtualAlloc` twin). Pointers into arenas, statics
  and the stack are ordinary 64-bit pointers; they stay valid in a new process because the arena is at the same
  address. A snapshot saves the arena's used extent, not 2 MB.
- Nothing needs to be below 4 GB afterwards, so `psx_addr`, `pc_psx_bad_pointer`, the low-stack constraint and
  `GuestPtr` all go.
- No structural serialisation (decision 2026-11-16). Save states only have to work within a build; snapshot and rewind
  inside one build is the requirement. Instead every step that moves state records a layout table (2.7), so an agent
  could later write an old-state converter if it is ever wanted.

### 2.2 Heap (T12)

Keep the allocator's behaviour (first-fit from either end, 16-byte units, block header, `vs_main_initHeap` callers in
main, INITBTL and ENDING): game behaviour can depend on which block is returned (fragmentation, allocation failure)
and the build_compare gates would show a difference. Change only where the arena is and how big it is:

1. The arena moves from RAM 0x8010C000 to a pinned host arena (S4). Its size stays the original's 0xF2000 by default;
   raising it later is a deliberate setting (the "nothing can grow" fix), never a side effect. Same algorithm, same
   sizes: the allocation sequence (each call's size, the returned offset from the arena start, the free totals) must
   be identical to the build before, recorded and compared in the gate. Decoded file objects (S6) are not in this
   arena: they come from the port arena (2.1), which has its own allocator and may grow.
2. The free list moves out of the arena (side tables, a guard word in the header unit), so an overrun cannot corrupt
   the allocator and tools can walk blocks without reading guest memory. VS-NativePort's
   `Native/Engine/include/Core/Memory/Heap.hpp` and `src/Core/Memory/Heap.cpp` (CC0, derived from the same
   `main.c` functions) are a ready reference for the table layout, the unit/guard rules, the error list
   (`kOutsideArena`, `kNotAllocated`) and `PayloadCapacity`; we port the design, not the C++.
3. `rood_rpc.py heap_free()` (walks the circular free list with 4-byte pointers) becomes an RPC `heap` report.

### 2.3 Guest pointers and PS1-layout structs (T13)

- The 8 `psx_ptr` fields (`actor_prepare.c` 2+15 accesses, `BATTLE.PRG/49268.c` 2, `44F14.c` 1, `3A1A0.c` 1,
  `battle_movement.c` 1) and `psx_to_int/psx_to_ptr` (195, concentrated in `25AC.c` 28, effect child-track Zig,
  `boot_sdk.c` 8) are cleared by S5. Most of the 195 are packet/ordering-table words (T4 owns those: the walker's
  24-bit link words stay until frames are data); the rest are file offsets (S6).
- Structs that overlay file bytes stop being used in place (S6). Each format gets a decode step that reads the loaded
  bytes with explicit offsets into a native struct allocated from the arena; the file buffer is then freed or kept only
  as raw payload (textures, vertex streams). This is VS-NativePort's own stated rule
  (`NATIVE_SOURCE_PORT_PLAN.md` 2.1.4 and EC-04: "separate disk layout from native object layout"), which they have
  not yet carried out for pointer-bearing types.
- Structs the host or Zig views read through DWARF (`zig_views.py`) keep working: views come from our own `.i`, so a
  native layout is picked up automatically; the Zig side must not hard-code offsets (hence S1).

### 2.4 Globals at PS1 addresses (T15)

Groups, by the reason `lp64_imports.json` gives (counts from the same file; they overlap partly):

| Group | n | Resolution |
|---|---|---|
| in the effect plug-in slot, placed in `pc_plugin_slot` by the link | 139 | become the plug-in's own statics with explicit reset (T10 modules); pointed-at ones (83) need their file decoded (S6) |
| arrays of unknown length and what follows them | 55 + 23 | declare the real extent. VS-NativePort's `DATA_LEDGER.csv` gives sizes for most data symbols (also `UNSIZED-ARRAY` hazards listed in their ledger): cross-check rather than guess; then they move like the 538 plain-data imports already moved |
| a word in a game file points into it | 17 (+ plug-ins) | the file's pointer table is decoded away (S6); until then stays |
| another name at its address / overlaps / declared with different sizes | 18 + 8 + 11 | merge to one symbol per object after the decoders; per-case |
| hold pointers or LP64-sized type | 11 | follow S5 |
| GPU packet or ordering-table storage | 13 | T4's: packets become typed items; not this lane |
| shared by BATTLE, INITBTL and the program | 2 | split into one owner each |

`imports.ld` and `native_imports.c` shrink group by group; the mover (`lp64_native_imports` in `build_boot.py`)
already moves a group as one region with the guest layout, so each group is a mechanical flip plus a proof.
`ownership.toml` stops keying Zig functions by PS1 address (S8; names are in place from the name import).

### 2.5 The RAM block and fixed addresses (T12)

After S4-S7 the 2 MB map holds only: overlay slot buffers (T10: the code is native, the bytes are initial statics
and scratch destinations), the packet/ordering-table area (T4), and what is left pinned. Then:
`map_guest(0x80000000, 0x200000)`, `ROOD_GUEST_RAM_BASE/SIZE`, `vs_overlay_slots` addresses, `rood_null_page`
(reads of the PS1's address 0: replaced by explicit guards, the hazard class `NULL-TABLE` of VS-NativePort's
`PLATFORM_HAZARDS.md`) and the pinned stack's low address go. The stack stays at a fixed address for snapshots, but
chosen freely.

### 2.6 What VS-NativePort already solved, and what it did not

Reusable (CC0, their own work; nothing from their `THIRD_PARTY.md`), as reference or ported code:

| Need | Their file | Use |
|---|---|---|
| Names for types and fields | `Docs/ledgers/TYPE_LEDGER.csv` (842), `FIELD_LEDGER.csv` (6,926) | Part 1 import (`pc/tools/import_fields.py`); type renames follow |
| Types whose layout changed upstream | `Docs/ledgers/UPSTREAM_LAYOUTS.csv` (25: 15 `layout`, 10 `type-only`) | skip list for the import; also the list of structs where the decomp's offsets are not stable |
| Data sizes | `DATA_LEDGER.csv`, `Docs/PORTING_WATCHLIST.md` | sizes of unsized arrays (2.4) |
| Heap with side tables and guards | `Native/Engine/include/Core/Memory/Heap.hpp`, `src/Core/Memory/Heap.cpp`, `HeapLive.cpp` | 2.2 |
| Module data and pointer relocation | `Native/Content/src/ModuleLoader.cpp` (`SymbolMap::ResolveIn`, `RelocatePointers`), `Native/Game/src/Boot/ModuleData.cpp`, `ModulePointers.cpp` (generated), `Content/ModuleData.h` | model for S6's decoders and for the relocation findings: interior pointers are valid only where layout identity is proven (their EC-04); their M1 package list of synthetic fixtures (null entries, truncated files, interior pointers) is a good test list |
| Overlays | `Native/Game/src/Boot/OverlayReset.cpp`/`.h`, `Native/Engine/src/Core/Overlays.cpp`, `Core/Arena.h` (`Core_overlayArena`) | per-overlay reset functions are what our `overlay_statics.c` table does; EC-05 "do not blindly clear every static on module entry" |
| Scratchpad | `Native/Engine/src/Core/Scratch.cpp`, `Core/Scratch.h` (`Core_ScratchAddr`) | they keep a 1 KB block with an accessor and replace uses by locals when a function is modernised: the same shape as our S3 |
| Typed views over a flag record | `Native/Game/include/State/ScriptFlags.hpp` (`std::span` view, bounds-checked, write observer) | model for named views of `vs_main_stateFlags` and other flat records |
| Hazard taxonomy | `Docs/PLATFORM_HAZARDS.md`, `ledgers/HAZARD_LEDGER.csv` | `POINTER-INT` (67), `BYTE-OFFSET` (19), `NULL-TABLE`: the sites we will meet; checklist for S5 |

Not solved there, so not to be assumed: their copied game code still includes `Legacy/Psx.h` and keeps the decomp's
struct layouts (`Native/Game/src/Actors/ActorRecord.h` is a copy with names); module pointers unresolved today become
null with a warning (their 1c note: "12 module globals are structs holding pointers and are not relocated yet"); their
scratch and overlay slots keep PS1 offsets (EC-05 is open); no save-state mechanism comparable to ours. So we borrow
names, sizes, heap design and test lists, not a finished object model.

### 2.7 Layout tables

Every step that moves or reshapes state (S4, S4a, S5, S6, S7, S9) commits a layout table under
`pc/docs/memory_layouts/<step>.tsv`: one row per state item (global, struct field, heap block class or file-struct
member) with its old and new location (symbol or arena + offset), its size and its type, plus a one-line note on any
reinterpretation (a pointer widened, an offset turned into a field, a padding change). The rows are generated by a
tool from the build's DWARF before and after (the one S2 writes for the RPC), not by hand. With the old build's
table and the new one an agent can write a converter from an old save state; nobody has to.

## 3. Order of work

Each step merges on its own, the game stays playable, and each has a proof. **All day counts below are unmeasured
guesses.** After S1's first 100 or so offset sites the actual time spent is reported and every remaining estimate
is redone from the measured throughput; until then treat the table as an order of magnitude, not a plan.
The proof is "identical to the original" (build_compare, determinism_smoke, snapshots) while we restructure. When
the CPU reference renderer goes (T5), the reference is no longer the PS1 path: it becomes our own previous build,
and each step's proof is "identical to the build before it". "Gate" below means: build_compare fight
(frames, states, audio, drops) and the 14-route menus lane 0 differ; `determinism_smoke`; snapshot smokes
(`snapshot_resume`, `snapshot_names`) and the rewind check; Windows build links; `classic_parity` when HUD memory is
touched; object_identity (identical or `--reordered`) wherever the claim is "same code". The 15-minute lane applies;
one game at a time.

| Step | Work | Proof beyond the gate | Size |
|---|---|---|---|
| S0 | Meters: lint rules `pinned_import` (from `lp64_imports.json`), `byte_offset_access` (fieldAt, byteAt, PSX_OFFSET), `ram_reference`; baseline | lint counts | 2 d |
| S1 | Names not offsets (T13a). Field names from the ledger (Part 1, in progress), then replace `psx.fieldAt/byteAt` and `PSX_OFFSET` sites by named fields, a file or subsystem per commit (37 sites in `vs_actors_updateActorAppearanceTransition.zig`, 23 in `parse_shp.zig`, ... 944 in all). Where the structs genuinely overlay, give the overlaid struct a union member | object_identity (often identical code); gate | 6-8 d |
| S2 | Host reads by name (done 2026-11-17, branch memory-2): `pc/tools/layout_table.py` reads the symbols, types and field offsets from the linked binary's DWARF into a sidecar `<binary>.layout`; the game answers RPC `field {path}` (value, bytes, write), `symbol_info` and `symbols` from it (`pc/src/dev_layout.c`); the digest's masks take `{path, size, deref}`; `Game.field/value/data/store/info/table`; about 100 tests and tools and `classic_parity` read and write by name, `snapshot_diff` names addresses by symbol and member; the `pc_*_offset_*` exports and `Game.layout/actor_offset/room_pointer/...` are gone. Lint meter `tool_address_read` 1231 -> 0 (the client `rood_rpc` and the tests of the raw RPC read/write and of the table itself are not counted) | `layout_smoke` (game and generator agree on 4,000+ paths); the moved tests pass unchanged in meaning; classic_parity; build_compare fight and menus 0 differ | about 5 h of work + proofs (3.2 below) |
| S3 | Scratchpad overlays. Measured: the old lint's 52 `scratchpad` hits were 46 I/O-page hardware registers (T16/T18/T21, now the `io_register` meter) and 6 scratchpad mentions; the real scratchpad code is `pad.addr(0xNNN)` (Zig, 137) and `PC_PAD(0xNNN)` (C, 21): about 15 typed overlays (actor and weapon transform 0x35C, collision 0x3BC, animation frame 0x3EC, AI slots 0x37C, ...) that alias one another in a native 1 KB block (`vs_pad`, not at 0x1F800000). S3a (done): `pad.at(T, off)` and `pad.many(T, off)`, compile-time checked, replace the `@ptrFromInt(pad.addr(..))` casts (86 lines; object code identical, 527 of 527). S3b: the rest by hand (offset arithmetic, tables), then name each overlay's owner; relocation of the block with S9 | new meter `pad_offset` (176 -> 90) | S3a 0.5 d measured |
| S4a | The 8 `psx_ptr` fields and 38 accesses to native pointers or offsets (S6 prerequisites first where the field is in a file struct) | lint `guest_pointer_*` 0; gate | 3-4 d |
| S4 | Heap to its own arena (done as the first form, measured): the game heap's arena left the PS1's 2 MB block for 0x80200000, the same allocator, size and offsets from the arena start (heap report equal). Second step (2026-11-17, after T8 removed the 24-bit links): the arenas (heap and the S6 port arena) are their own mapping at `ROOD_ARENA_BASE` 0x90000000, out of the window (which is the PS1's 2 MB again); `rood_guest_memory` is the one range check (`pc_game_pointer_ok`, the RPC's read/write/packets). Third step (same day): they are above 4 GB, at `ROOD_ARENA_BASE` 0x1_0000_0000. The 157 `address_as_integer` sites (heap pointers in 32-bit words) were not all removed: 110 were (plan 3.4), and what is left is made to work by naming: a guest word for an arena object is `ROOD_ARENA_ALIAS` (0x80200000, the old heap place) plus its offset (`psx_addr`, `psx_word`, read back by `psx_to_ptr`), so the sound driver's parameter words and the bank wrappers need no change. Layout tables: memory_layouts/s4-heap-arena.tsv, s4-arena-out-of-window.tsv, s4-arena-above-4gb.tsv | determinism_smoke, snapshot, heap report, fight, menus | 0.5 d measured |
| S5 | `address_as_integer` 195: packet words stay (T4), file offsets move to S6, the rest to typed pointers | lint count | 3 d |
| S6 | File decoders, one format per commit: WEP (done 2026-11-17, with the port arena), ZUD with the object queue's file address widened (done 2026-11-17), then SHP and SEQ loaders, ZND/MPD tables, the effect P-file's spawn fields, ARM, menu data, saves (T22). Native struct filled from bytes; payload kept raw where the renderer wants it. Design and survey: section 3.3 | gate per format; rooms lane, spells lane, 13 menu routes with the story scenes, GI replay, classic_parity, determinism, snapshot, rewind, Windows | measured: 12 and 20 agent minutes of work for the first two (easy ones), 2.5 h of proof each (one lane at a time); re-estimate in 3.3 |
| S7 | Globals by group (2.4), the 301 pinned: first the unsized-array and overlap groups with real extents, then the plug-in slot with T10, then the pointed-at ones after their decoder | lint `pinned_import` per group; gate | 10-14 d |
| S8 | `ownership.toml` keyed by name; `imports.ld`, reference ELF address checks (T11) | build links; hotswap smokes | 3 d |
| S9 | Delete the RAM block: `map_guest`, `ROOD_GUEST_RAM_*`, slot addresses, `rood_null_page` (guards at the NULL-TABLE sites), the low stack address | lint `ps1_address` 0; gate; full lane once | 3-4 d |
| S10 | Snapshot keys from the layout table; Oracle and `field_map.py` by symbol; the in-process rewind ranges follow the arenas | snapshot and rewind smokes, determinism_smoke, zig_hotswap | 5-6 d |

S0-S3 can run in parallel with each other and with the T4 and T10 lanes; S4a-S5 need S1; S6 needs S2 and S4; S7 follows
S6 (pointed-at) and T10 (plug-in slot); S9 needs S4-S8. Total S0-S10: an unmeasured 70-90 agent days guess (3 workers: 4-6 calendar
weeks), dominated by S6 and S7. The first four steps cost about 15 days and already stop new code from depending on
offsets.

**Order as decided after the first S1 measurements (2026-11-16, coordinator):** automatic S1 tops out at about 10%
and the rest is file sections (S6's) or hand work, so S1's tail is not ground down site by site. The order is now:
(1) bitfields: the names import for the few placeholder bitfields, and `psx.bitsAt`, a compile-time bounds-checked window
over a `_bits_*` group instead of `psx.fieldAt` on its bytes; mapping the masks to named bits is left to hand work where
a subsystem is touched; (2) S3, the scratchpad overlays (see its row: S3a done, the lint's 52 were mostly I/O registers);
(3) S4a, the 8 `psx_ptr` fields and 38 accesses, then S4, the game heap in its own pinned arena (identical allocator,
size and behaviour; the port arena for decoded objects comes with S6; the low stack and the 4 GB limit go; proven by
determinism_smoke, snapshot and rewind across the new range, and a heap report equal to before); (4) the remaining S1
sites are absorbed by S6 as each file format is decoded, or done by hand when a subsystem is touched anyway.
Medium-confidence field names are deferred.

### 3.1 S1 measured so far (2026-11-16, first 3 hours)

Not yet the 100 sites the plan asked for; what the first hours showed, for the re-estimate:

- The 944 `zig_raw_memory` hits split: 300 `psx.fieldAt` (182 at offset 0, 118 elsewhere, 92 of the offset-0 ones are
  the words of bitfield groups, `_bits_*`) and 218 `psx.byteAt`, the rest `@ptrFromInt` and friends. Most `byteAt`
  bases are file sections (`definition`, `section`, `header`, `gim`): they belong to S6 (decoders), not to S1.
- Object code identity is not a usable proof here: typed access removes the null checks of the pointer arithmetic, so
  24 of 1,907 functions differ in instruction count after only 5 sites. The proof for S1 is the behaviour gates (fight,
  spells, menus, determinism, snapshots), and they did their job: a first batch of 61 sites made 103 of 120 fight
  frames differ (the optimistic "offset 0 means the field" rewrite ignored that T was wider than the field, so a
  4-byte copy of two adjacent tile words became a 2-byte copy), found by bisecting by file in about 40 minutes.
- Rules that survived: replace only when the field resolved from the DWARF dump is exactly T's size and every struct
  that has the chain's first field name resolves the same way. That yields 23 sites in 4 files (bone view matrix,
  actor root offset, move with collision, the room-object pose) from the whole tree, so automatic coverage of
  `fieldAt` is about 8%.
- Next, a type oracle (the compiler says what the base expression's type is, through `@compileLog`) to resolve the
  rest without guessing. Bitfield words need the views generator to emit a named word. Honest throughput after the
  tooling: 23 verified sites in roughly 1.5 hours of which most was building the tool and finding the bad rule; a
  steady rate is still unmeasured.

### 3.2 Measured rates (2026-11-16, one worker, about 11 hours from 07:17)

| Step | What | Agent time | Notes |
|---|---|---|---|
| Part 1 | 273 field renames, 55 type renames, the tools (import_fields.py with the compiler-driven --fix) | about 5 h | the first tool version had three bugs the build found; each chunk's proof (14-route menus lane 30 min, fight 40 s, Windows build 4 min, smokes 3 min) is about 1 h of wall time on top of the work |
| S0 | meters: byte_offset_access, ram_reference, unnamed_field, pad_offset, io_register (and the scratchpad split) | 0.5 h | |
| S1 | 138 raw accesses named or bounds-checked (25 + 13 by DWARF and type oracle, 100 bitsAt) | about 4 h | most of it building the oracle and finding a wrong rule through the fight gate; the automatic ceiling is about 10% of the 944; there is no steady hand rate yet |
| S3a | 98 scratchpad casts to pad.at/pad.many | 1 h | object code identical: the cheapest kind of proof |
| S3b 1 | owner table of the scratchpad | 0.5 h | documentation |
| S4a | 6 pointer fields in scratch overlays | about 3 h | layout arithmetic and the Zig/C translate-c disagreement over packed pointers; 14 menu routes + fight + spells + determinism |
| S4 | the heap arena out of the 2 MB block | about 2 h | the surprise was that packets live in the heap |
| S4b | the navigation pointers (8 globals declared four ways) and the AI's pad slots as a native struct | about 3 h of work and 2 h of proof | the coordinator asked for proof that the clobber was dead: an instrumented build over a fight, 9 rooms, the spells lane and 43 enemy rooms, 1.5 million slot reads, 0 clobbered; the `--casts enemies` lane as one run dies on an unimplemented effect opcode on main too |

| S2 | the layout table, the RPC and the migration of the tools and tests | about 5 h of work in one day, one worker | the generator is a small DWARF reader (2-3 s on the 84 MB binary; gdb was not needed). What took the time was not the reader but the names: the same struct is seen by the C header and by each Zig view under different member names (merged by offset, both names resolve), pinned imports have a generic definition (the header's declaration is used), overlay statics need their overlay (`<overlay>:<name>`), and a pointer-heavy path needs the game to follow it (hence `field` in the game). About 110 test and tool files moved, 70% by a one-off codemod (`g.read(sym["X"] + K, N)` -> `g.data("X.member", N)`, with the member found at K in the table and the address checked), the rest by hand (about 15 files with attribute-held addresses, offset constants of card layouts: those stay as `offset=` from a named base). Each moved test ran once (about 25 min of games for the menus family, 20 min classic_parity). Two bugs found by the proofs: a baseline fallback decoding length-0 reads as 0 (build_compare's loot differed), a word of bit fields read as a struct |

What this changes in the estimates above (S5 onward are still guesses, now with a floor):

- A proven step costs at least 1 to 1.5 hours of wall time for the gates whatever its size, so the cost of S6 (15 formats)
  and S7 (the pinned groups) is at least one proven commit per format or group: 15 + about 12 commits, 1.5 to 3 hours each
  when the code change is small and a day when it is not. The earlier 25-35 and 10-14 day guesses stand as ranges, not
  as measurements.
- S5 looked at (end of day 1): `address_as_integer` is 188 now. Beyond the sound driver (28) and the effect spawn fields, the
  event script VM is a trap: its 250 opcode handlers all return `int`, and the ones that jump return a script address in that
  int (the interpreter loop treats it as an address), so widening the address means widening the return type of the whole
  table and every handler, C and Zig (about 14 handlers return addresses; the rest would need a defined upper half). It is a
  day of mechanical work with a high-risk surface (the scripts are the whole game's story), so it waits for S6's script
  format decoding to turn the cursor into an index.
- S5 (`address_as_integer` 195) is not one sweep: 28 sites are the sound driver's command parameters carrying guest
  addresses (T18: they need a handle table), about 40 are effect and room code in Zig storing heap pointers in 32-bit
  fields, the rest are packet words. About 6 days, with the sound part possibly folded into T18.
- S4 b (the arena above 4 GB and the low stack gone) cannot precede S5 and a separate allocator for the packet and
  ordering-table buffers: 2 to 3 days after S5.
- The navigation slots in the scratchpad (60 sites, guest pointers mirroring 32-bit globals) wait for the T15 pointer
  globals (D_800F58xx): 2 days after those are widened.

### 3.3 S6 design (2026-11-17): what a decoder is here, and the first formats

Survey (read before choosing; all from the tree at memory-2's head):

- The 14 pinned imports in the group "a pointer word in a game file points into it" are false positives for S6: the
  detector (`pointed_into` in `build_boot.py`) scans words of the EXE, the PRGs and EFFECT/*.BIN for anything that looks like a RAM
  address, and what it finds are static pointer tables inside overlay data (BATTLE.PRG 0x835ac: the four school spell-id
  pointers; `_uiTable`, `_saveMenuBg_clut`, `vs_main_inventoryIndices`, ...). They are T15 data, **S7's**, not file formats; freeing
  them needs the pointer tables made native, not a decoder.
- The container level of ZND, MPD, SHP and the P-file is already decoded by copy into native structs with native pointers
  (`load_znd_*`, `loadMpd*Section`, `parseShp`, `bindEffectPackageBlocks`): there is little to decode there. What is left in S6 is
  (a) the cursors that carry a file address in a 32-bit field (`vs_battle_objectData.dataAddr`, the effect spawn fields), and
  (b) in-place use of file bytes: SEQ is a delta-stream the animation code walks with an offset kept in 16-bit actor fields,
  MPD geometry and room texture frames poke polygons by offset. (b) are views (S1) or a redesign of playback, not a decode, and
  are last.
- So the order is by self-contained size: WEP (one reader, two passes, a cursor in a pinned global), then ZUD (the enemy
  package: two readers of the same header, in battle and in MENU9, feeding `dataAddr`), then the rest of the `dataAddr`
  loaders (SHP, SEQ), then the P-file spawn fields.

What a decoder is:

1. The file's bytes are read once, by one function per format (`game/battle/<format>_file.zig`, `decode(at)`), which holds the format's layout as
   its header comment and returns a native record. Nothing else reads the layout.
2. The record holds the fields as native types and, for the payload the renderer or `LoadImage` wants raw (polygons, palettes,
   textures, bone info), 64-bit pointers into the raw buffer. The raw buffer stays in the game heap exactly as the original allocated
   and freed it: **a decoder never takes or releases a block the original allocates**, so the heap's allocation sequence is the
   original's. Where the original made a copy of part of the file in the heap (the WEP offsets block, `allocHeap(size)`), the copy is still made:
   the game's allocator pattern is the claim, and the copy is what the renderer reads.
3. The record lives in the port arena (`game/common/port_arena.zig`, `ROOD_PORT_BASE` 0x80300000, 0x40000 bytes in the 16 MB window
   beside the heap's arena). A header (bump offset, high-water mark, 8 root slots, 64 size-class free lists, offsets only), then 16-byte units.
   Each decoded format owns a root slot: `put` frees what the root held and allocates the new record, `clear` frees it, so an aborted load
   cannot leak and the arena does not grow with play time. Objects are at most 1 KB (a decoder of a larger table splits it); allocation failure
   returns null and the loader fails as the game's own allocation failures do.
4. Snapshot and rewind: the arena is a snapshot range ("port", after "io"; saved whole, so the capacity is a deliberate constant), so
   the Oracle digest, `snapshot_resume` and the in-process rewind carry the records; pointers into the heap stay valid because both arenas
   are at fixed addresses. Sizing: the records are tens to hundreds of bytes (WEP 64); 256 KB holds every decoded format of the plan with room
   (a zone's MPD table is a few KB); raising it is a change of `ROOD_PORT_SIZE` in `psx_types.h` and `psx.zig` and takes old snapshots with it.
   The arena may later leave the window (pointers in it are 64-bit); it is inside now because the window is already mapped.
5. Users switch over format by format: the decode function is called where the loader first touches the bytes, and every
   later reader takes the record. The old cursor globals go (`_wepFileEpilog` is the first). Layout tables: `memory_layouts/s6-*.tsv`.

Measured (2026-11-17, one worker): the port arena and WEP, from a cut-off draft: 12 minutes to review, redo the allocator and the
snapshot range order, build and commit; ZUD plus widening `dataAddr`: 20 minutes (one build error class: a static inline in a header is not
shareable with Zig, so the decoder is a plain function in 146C.c; a stray `sed` on `unk2` in a shared header, caught by the build). The proof is the
cost: the lanes in order are fight x2 (35 s each), 8 rooms (3 min), casts (2.5 min), 13 menu routes (9 min), GI replay (5 min), the smokes
(4 min), classic_parity (25 min), Windows (4 min, in parallel): about 55 minutes of wall time with one game at a time, about 80 with parity.
So a format is about 1.5 hours of wall time when its change is a decoder of a header or a cursor, whatever its size, and the proof
dominates. The remaining formats by kind: loaders of the same shape (SHP's pass 0/1 cursor, the effect spawn fields, ZND/MPD tables):
about half a day each with proof; SEQ (a delta stream walked in place, offsets kept in 16-bit actor fields) and MPD geometry/room texture
frames (polygon pokes by offset): a day each, and they are S1 views work as much as decoding. About 8 formats of the first kind and 3 of
the second: roughly 7-9 days, not 25-35.
Not covered by any lane or suite: the ENEMY menu's ZUD load (MENU9, `vs_menu9_parseZudFile`), which runs only with the classic menus
(ui_menus off), the Bestiary and Circle on an unlocked foe (ui_score_smoke checks that the native screen does not load one). It was
compared by hand for ZUD: all 78 foes set unlocked, the first 12 opened in turn on main and on the new build, and the preview actor's bone count,
model id and the weapon and shield slots' bones and models printed after each load: 12 of 12 equal (including foes with a weapon, a shield, both, and
two-part models). The probe is pc/tools/bestiary_zud_probe.py (it drives ui_score_smoke's Session; run it on both builds with ROOD_BINARY and ROOD_SYMBOLS and diff). The battle spawn path, which uses the
same decoder, is covered by every fight and room lane.

**The alias was temporary (decision 2026-11-17); it was deleted by T18 on 2026-11-18 (section 3.7, PS1_Ties.md T18).** A guest word naming an arena object is `ROOD_ARENA_ALIAS` plus its offset (psx_types.h). That is a
32-bit word naming memory, a PS1-shaped tie kept only so the sound driver's parameter words work unchanged. T18 (the sound driver; PS1_Ties.md)
removes it: the words become real pointers or handles, then `ROOD_ARENA_ALIAS`, `psx_word` and the arena arm of `psx_addr`/`psx_to_ptr` are deleted.
The lint meter `arena_alias` (psx_word, psx_word_ptr, psx_to_int, psx_to_ptr sites; baseline 53 at the first step) may only fall; zero means the alias can go.

### 3.7 T18: the sound driver's words become pointers, and the alias goes (2026-11-18)

Where the alias is used (lint meter `arena_alias`, 54 at the start): the command parameters `FSoundCommandParams.Param1/Param2` (u_int), the pending
sequence word `D_80037788.PendingSequence`, the playing channel's identity `FSoundChannel.unk3C` (compared with `Param1` by the stop, fade and evict
commands), the commands' argument word `unk4` (dead: it only serves the pair command 0x16, which nothing issues), the ids-or-addresses that travel
through main.c's wrappers (`vs_audio_resolveSfxProgram`'s int return, the bank wrappers, `vs_audio_playbackSoundEffectIdForTag`'s int return,
`vs_audio_controlSoundEffectChannelIds`' u_int array), and a few tokens (`unk38`, the old callbacks of `DrawSyncCallback` and `VSyncCallback`).

Decision: widen, not a handle table. The words become `__UINTPTR_TYPE__` and hold either an id (a small number, zero-extended: `u_long` ids as before;
the sentinels -1 and -2 are compared as ints, so a sign-extended or a zero-extended one both match) or a real pointer; a pointer is told from an id
by being above 0xFFFF (`func_800123C8`, the only place that asks). A handle table would add a table and a lifetime to a driver that is going to be
replaced by a host mixer anyway (T18b); the widening is mechanical and each step has a compile-time check (the LP64 pass refuses a pointer
cast to a smaller integer). The `FSoundCommandParams` and `FSoundChannel` structs grow (two statics and an array of 24 channels); nothing reads them by offset.

Steps, one commit: remove the dead pair command and `vs_audio_playbackStartMusicPair`; widen the fields and the wrappers; replace each `psx_to_int`/`psx_to_ptr`/`psx_word` by the
pointer or a cast; delete `ROOD_ARENA_ALIAS`, `psx_word`, `psx_word_ptr` and the arena arm of `psx_addr`/`psx_to_ptr`/`psx_get` (they return to the PS1 form: a word is the
pointer's 32 bits and fatal above 4 GB), and the `arena_alias` meter. Proof: the audio lane of build_compare (the recorded mix of the fight room, byte for byte), the
13 menu routes and the story scenes (scripted sound effects), casts spells and enemies (effect sounds), determinism, snapshot and rewind (the driver's state is in the
saved range), and the heap above 4 GB as it is now.
### 3.5 The effect package loader (S6, 2026-11-17)

What it is: an effect P-file ("package") is read into one heap buffer (`vs_effects_loadEffectPackage`: `vs_battle_pfileBuf`, sized by
the queued context, read in one disc request), then `vs_effects_bindEffectPackageBlocks` walks its block chain and a small route per block
type (`_parsePfileBlock1, 2, 3, 5, 8, 9, 10, 11, 12, 13` and the curve block) stores a pointer (and sometimes a count or a derived
table) into the global record `vs_effects_package` (`D_800F569C_t`, 0xD4 bytes of pointers: native since S4). That is already a decode by
copy of the offsets, spread over twelve one-line owned functions.

Plan, one commit:
1. `game/battle/effect_package.zig` holds the format (the block chain: u32 meta, kind in the high half, size in the low half, 0 ends; each
   kind's role and layout, in its header comment) and one `bind(first)` that walks the chain and fills the package record, including the three
   blocks with a derived part (block 1: the table pointers and count; the curve block: its 32 curve pointers; blocks 12 and 13: the room sound
   bank and cutscene stream they hand to the audio side).
2. `vs_effects_bindEffectPackageBlocks` becomes a call of it. The twelve routes have no other caller (the C twin's calls are replaced by the
   owned function), so their files, `ownership.toml` entries, `root.zig` imports and C prototypes are removed (always remove what is no longer needed).
3. The raw payload stays in the file buffer (particle definitions, curves, key tables, timeline); the record keeps pointers, as before; the heap
   allocation of the buffer and its release are the callers' and unchanged.

Not in this step: the effect scripts' interpreter reads the blocks in place by offset (that is the S1 views work, per opcode) and the effect
plug-ins (T10). Saves are T22's and wait for the open questions.

Proof (targeted until the lanes are released): fights and rooms (every fight loads its packages), the spells lane (the packages with
particles, curves and room sound), `effect_opcode_smoke`; the full set when land-17 is in.

### 3.6 ARM, the map scene file (S6, 2026-11-17)

What it is: `SMALL/SCENnnn.ARM` (31 files, one per map scene) is the map screen's data and also the shape of the MPD's section F (the room's
own scene). The map screen (MENU5.PRG 4D8.c) reads it into a heap buffer (`vs_battle_sceneBuffer`, `vs_main_allocHeapR` of the file's size) and
uses it in place. Layout: `i32 count`, `count` room records of 12 bytes (`visited`, `size` of the room's blob, `zoneId`, `mapId + 1`), the room
blobs one after another (vertex counts and vertices, lines; `_scaleRoomVertices` scales them in place), then `count` name records of 36 bytes (32
bytes of name, `prev` and `next` as shorts). Three things touch it: `vs_battle_initSceneAndGetRoomNames` (turns each record's `size` into the
blob's offset from the buffer's start, in the same word, and returns the names), `vs_battle_setRoomsUnk0` (writes `visited` from the room flags)
and the map code (blob = buffer + offset). All of it is offsets from the buffer and 32-bit words that never hold an address, so the file is not a
64-bit problem; what is left is that the layout is spread over a C macro, a Zig function with integer arithmetic and the comment in
`room_catalog.c`.

Plan, one commit: `game/battle/arm_file.zig` names the layout once (header comment, `Scene`/`Room`/`Name` views, `names(scene)` and `blob(scene, room)`
as pointer arithmetic); `initSceneAndGetRoomNames` uses it (the in-place size-to-offset conversion stays: the map code reads `dataOffset`);
`vs_battle_roomDataOf` in MENU5 stays a C macro (the map code is C and unchanged). Not here: decoding the blobs into separate arrays (that
is the map screen's rewrite, T-menus), and `_scaleRoomVertices`'s in-place scaling. Proof: the `map` and `areamap` menu routes (both read a scene),
rooms (the MPD's section F goes through the same function), `room_catalog` users.

### 3.4 Above 4 GB: what still stops it (2026-11-17, measured)

The arenas are out of the window (S4, second step) but below 4 GB. To find what needs that, a build put the arena at 0x1_9000_0000 with the
same pages mapped again at 0x9000_0000 (so a truncated address still works) and made `psx_addr` log each distinct caller instead of
stopping; the fight, the 8 rooms, the spells lane and the 13 menu routes were played on it. The firing list was 47 functions at first;
it is the work list. After the file-format batch (MPD container, ZND image pack, room section words, P-file block 1, shared sequence tables,
the effect spawn records' pointer fields, the ordering-table cursors, the animation decoder's stream address, `D_800F5920` declared one way
everywhere, `func_800A141C`, `func_800DCAA0` and the event trigger evaluator taking pointers) `address_as_integer` fell from 157 to 62 and the
list to what is below. The LP64 compile pass already refuses a pointer cast to a smaller integer (`-Werror=pointer-to-int-cast`), so the C
side cannot truncate silently; the Zig side's explicit `@truncate` calls are the identity tokens (they only compare) listed in the layout table.

| What | Sites | Why it needs an address below 4 GB | Resolution |
|---|---|---|---|
| the sound driver's identities and parameters: `FSoundCommandParams.Param1/Param2` and `D_80037788.PendingSequence` (u_int, hold the address of a sequence or sound-effect blob in the heap), `FSoundChannel.unk3C` (the playing blob's address or id; the stop, fade and evict commands compare it with `Param1`), `vs_audio_playbackSoundEffectIdForTag`'s and `vs_audio_playbackPlaySoundEffectBlob`'s int returns (an id or an address), the `unk4` of the command arguments, the bank wrappers' `psx_to_int` in main.c | 28 in 25AC.c, 5 in main.c | the words are 32 bits and an id and an address share them (ids are small or -2, so a plain widening changes `-2`) | T18, as a handle table (a blob gets a small handle; every comparison is between handles) rather than a widening: the audio lane of build_compare (the recorded mix, byte for byte) is the proof. `vs_audio_playbackStartMusicPair` and its command 0x16 have no caller and can go first. Estimated 1 to 1.5 days |
| the event script VM's jump handlers | 0 (done, `event_jump.zig`: a jump stores its target in one game global and returns the code 5) | - | story scenes, 5 script routes and the opening: 0 differ against main |
| `vs_event_subroutineReturnCursor` and `trace_attack`'s record address | 2 | an int holding an address that is followed | widen the two holders |
| `no_hostile_near_target`'s out-of-list read | 1 | reads PS1 RAM by a 32-bit alias of a heap address | the PS1's own out-of-list behaviour, unobservable; a guard |
| `hud_bridge` packet cursor span, `boot_sdk` callbacks (function addresses) | 4 | packet addresses (T4) and code addresses | with the packet allocator; code addresses stay below 4 GB (the image is pinned) |

With these done the arena moves by changing `ROOD_ARENA_BASE` (one constant, tested by the aliased build above before the real move), and the
`psx_addr` limit and the low stack (the game stack at 0x6000_0000 holds locals' addresses in the same 32-bit words) go after it.

## 4. What breaks when layouts change

| Thing | Breaks how | Handling |
|---|---|---|
| Save states | (within a build only) Keys include the state layout and code hash (`snapshot_key.c`); any step that moves state refuses states from older builds (accepted by the user). Within a build, snapshot, rewind and Oracle must work | 2.1 pinned arenas; a snapshot test at every step (S4 adds a range) |
| Snapshot keys | They hash the decomp's symbol names via `symbols.tsv`, not field names; field renames do not touch them (checked in Part 1: object_identity shows the objects equal). Moving data does change the layout key | key from the generated layout table (S10) |
| RPC memory by name | `pc_object_offset_*`, `pc_room_offset_*`, `battle_state`'s JSON keys (`F(&j, s, field)` stringises the member name) and `read(address)` assume PS1 offsets and field names | Part 1 keeps the export symbol suffix and tools map new to old through `pc/names/fields.tsv` for build comparisons; S2 replaces the raw reads by `field {path}` |
| Lua bindings | `pc/src/lua/lua_api_game.cpp` etc. call C functions; they read no raw memory today (no `psx_*` or 0x80 address in `lua_api_*.cpp`) | no change; add a rule that bindings use the named API |
| Classic parity gate | Page digests masked by address (state parity 3.4) and spawners writing game records by symbol and offset | S2 turns masks into symbol and field ranges; packet parity is unaffected until T4 |
| Tools reading guest addresses | `rood_rpc.py`, `field_map.py`, `snapshot_diff`, `memcap`, `rood_repl.py`, `42 tests | S2; each tool takes a build's layout table |
| `ownership.toml` | Keyed by PS1 address; checked against reference ELFs | S8 |
| Zig views | Generated from our `.i` DWARF: layouts follow; hand-written offsets do not | S1 first |
| build_compare | Compares RPC `battle_state` by key: renamed keys differ between builds | map through `fields.tsv` (done in Part 1 if a rename reaches a key) |
| Object identity proof | Holds for renames and byte-offset to field changes; not for layout changes | behaviour gates instead |

## 5. How the lint meters fall

The current lint (baseline at this commit): guest_pointer_field 8, guest_pointer_access 38, address_as_integer 195,
zig_raw_memory 944, unnamed_symbol 7,548, scratchpad 52, ps1_address 7, gpu_packet_writes 22. Added by Part 1:
`unnamed_field` (members used as `.unkNN` or `->unkNN`, 14,211 after the first import chunk; it falls with each
field-import chunk).

| After | guest_ptr_field/access | address_as_integer | zig_raw_memory | scratchpad | ps1_address | new meters |
|---|---|---|---|---|---|---|
| now | 8 / 38 | 195 | 944 | 52 | 7 | pinned_import 301 |
| S1 | 8 / 38 | 195 | about 100 (the real overlays) | 52 | 7 | byte_offset_access falls with zig_raw |
| S3 | 8 / 38 | 195 | about 100 | 0 | 7 | |
| S4a, S4 | 0 / 0 | 195 | about 100 | 0 | 6 (heap address) | |
| S5 | 0 / 0 | about 60 (packets) | about 100 | 0 | 6 | |
| S6 | 0 / 0 | about 20 | about 20 | 0 | 6 | pinned_import about 190 |
| S7 | 0 / 0 | about 20 | about 20 | 0 | 5 | pinned_import about 20 (packets) |
| S9 | 0 / 0 | 0 once T4 lands | 0 | 0 | 0 | pinned_import 0 |

`unnamed_symbol` is a naming meter (names import) and is independent of this lane. The tie lint's counts of
`guest_pointer_*` look finished early; that is why S0 adds meters for the real couplings (pinned imports, byte-offset
access, RAM references).

## 6. Risks and open questions

- S6 is the long pole and touches rendering data paths; it parallelises per format but each needs its own proof and
  some formats are shared with T9 (disc reads by asset) and T4. Order them by how many pinned globals they free.
- Decoding file structs may change the heap's fragmentation and so behaviour; the heap algorithm stays, and the
  gates would show it. A decoded object plus the raw file in the heap doubles use for a while: the arena size setting
  exists for this.
- Where the decomp reads past a field into the next ones (`fieldAt(..., 0x24)`) the "named field" is sometimes a
  union or an array view: S1 must decide per site and the ledger's field names are a hint, not proof.
- 64-bit `long` and `size_t` leaks are in `LP64_Audit.md`; the same sweep applies when pointers widen.
- Windows: `VirtualAlloc` at a fixed address can fail (ASLR); the stack code already has a fallback ("save states are
  off") that arenas must copy.
- Decided 2026-11-16: two arenas (game heap identical to the original; port arena for decoded objects), no structural
  serialisation, layout tables per step, measured estimates. Open: the arenas' fixed addresses (to pick in S4 beside
  `snapshot_layout.py`'s).
