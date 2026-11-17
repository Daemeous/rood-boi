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
| S4 | Heap to its own arena (done as the first form, measured): the game heap's arena left the PS1's 2 MB block for 0x80200000, the same allocator, size and offsets from the arena start (heap report equal). It stays in the 16 MB window (0x80000000..) and below 4 GB: the ordering tables and primitive buffers are allocated from this heap (INITBTL, vs_field_finishRoomLoad) and the game's 24-bit links cannot name another address, so the arena leaves the window, and the `psx_addr` 4 GB limit and the low stack go, only after T4 (frames as data) or when those buffers get a separate allocator. Layout table: memory_layouts/s4-heap-arena.tsv | determinism_smoke, snapshot, heap report, fight, menus | 0.5 d measured |
| S5 | `address_as_integer` 195: packet words stay (T4), file offsets move to S6, the rest to typed pointers | lint count | 3 d |
| S6 | File decoders, one format per commit: MPD sections, SHP, SEQ, WEP, ARM, ZND, effect P-files, menu data, saves (T22). Native struct filled from bytes; payload kept raw where the renderer wants it. 15 formats or so | gate per format; rooms lane, spells lane; for formats a room test (a room load compared by `rood_rpc` before and after) | 25-35 d |
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
