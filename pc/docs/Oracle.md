# State oracle (ownership tasks 14–18)

Linux/x86 only, in one running process. No Zig toolchain is required. The
oracle observes the original game; routed shadow calls are opt-in as described below.

## Protocol

Pause the game and wait for its step budget to reach zero before `snapshot`
`mark` or `rewind`. One in-memory mark is retained; rewinding does not consume
it. A new mark replaces it. Restore copies game memory, stack/registers, SPU
and interactive fog, then resynchronizes CD/render caches. RPC clients and
input/digest recordings are host state, not rewound. Rewind clears injected
RPC holds and seeks replay input again. It stops an active input recording
rather than writing non-monotonic timestamps.

- `snapshot {action: "mark"}` → `{marked: true, vsync}`.
- `snapshot {action: "rewind"}` → `{rewound: true, vsync}` (paused).
- `record {path: "..."}` records pad buttons and right stick by absolute vsync.
- `record {path: ""}` closes the recording; `null` also stops.
- `replay {path: "..."}` loads a recording transactionally and replays it by
  absolute vsync. Rewind to its start before playing. Stop with an empty path.
  Recording and replay are mutually exclusive, including completed replay.
- `digest {action: "record", ticks: 600, masks: [[address, size], ...]}`
  records per-vsync FNV-1a hashes for every 4 KiB page plus the SPU image.
- `digest {action: "stop"}` stops early without dropping the baseline.
- `digest {action: "compare"}` compares subsequent ticks with the baseline.
- `digest {action: "result"}` reports `recorded`, `ticks`, `pages`, `first_tick`,
  `first_pages` and `error`. Success requires `error == 0`, `first_tick == -1`
  **and** `ticks == recorded` (an incomplete comparison is not proof).

Record and compare stop automatically at their capacity/baseline length.
Stop an active run before starting another. Masks are installed only with a
new recording; omit them to retain the previous set, or supply `[]` to clear.
Changing masks invalidates the old baseline. Range names and offsets identify
pages, not individual differing bytes or C fields. A mismatch at tick 10 is
the eleventh sampled frame. The digest runs before RPC polling each vsync.

`alternate` optionally selects a checked ownership subsystem index for compare;
its original owner is restored on stop. Unavailable optional ownership is an
error rather than an unchecked write. The subsystem runs as owner 3
(`alternate`) for the whole compare: every routed call flips its route between
owned and original (odd calls owned), so each function's calls split whatever
vsync they land on (a per-vsync flip put a once-a-frame battle function on one
side only, decided by the launch's parity). The result adds
`"alternate_calls":[{"function","owned","original"}]` for the subsystem;
`differ.py` fails the mode when a called function ran on one side only.
Routed shadow mode is described below.

## Coverage and intentional exclusions

`snapshot.c` exports guest RAM, scratchpad, I/O, native game state sections,
overlay statics and VRAM. The software GTE is already in the saved native
state (`gte`, `libgte`, `inline` are state host units). SPU serialization is
included separately. Game stack and fog are rewound but are not page-digested;
caller-stack out-parameters need Task 18's own comparison boundary. Host
renderer, RPC, editor, input capture and digest allocations stay outside the
ranges, as they do for save states. Save/card file writes are not undone.

The integration proof uses the title checkpoint with deterministic scanlines
and interpolation disabled. It masks only `last_vsync`, the host monotonic-clock
timestamp used for non-deterministic scanline estimation. This is the existing
`snapshot_diff.py` volatile-symbol exclusion, not a game-state noise mask.
Addresses and sizes are resolved from the tested binary, never hard-coded.
The proof records/replays 600 vsyncs and then flips one unused guest-RAM byte:
the latter must report tick 10 and `ram+0x1000`. C probes also test unmasked
synthetic state, masks, failures, capacity bounds and repeated rewind.

The Python convenience methods are `Game.mark()`, `rewind()`, `record(path)`,
`replay(path)` and `digest(action, **params)`. The registered integration suite
is `ownership_smoke`; CTest probes are `state_diff`, `state_rewind` and
`input_record`. This title checkpoint is not the full battle corpus required
for the bulk port. Field mapping, complete feature profiles and whole-overlay
corpus coverage extend this machinery in tasks 19–20.

## Task 18 routed shadow transactions

`shadow.c` provides `pc_shadow_compare`: capture the input state, execute the
original once, save its results, restore the input, execute the owned callback,
compare, and restore the original results. Explicit caller-output ranges are
captured and restored separately. These must exclude the transaction's own
frames and its callback context. Overlapping output ranges are rejected.

`pc_shadow_result` retains the function, frame, panic message and first differing
byte (range, address, offset, expected and actual). SPU addresses are offsets in
the SPU image, distinguished by the `spu` range. A comparison error is separate
from a mismatch: allocation failure after the original has executed must not
cause the caller to execute it again. Restore failure is fatal rather than
continuing on partly restored state.

The owned callback runs behind a live C `setjmp` guard. `pc_zig_panic` delegates
to it only during that callback; normal Zig panics still follow the crash path.
CTest `shadow_zig` compiles a real ReleaseSafe Zig integer-overflow probe and
verifies recovery, the function/frame/message, and original output restoration.
`shadow` checks state and caller-output mismatches, recovery and subsequent
transactions. `shadow_failures` injects failure at each allocation stage.

`route.zig` now connects shadow exports to this backend. Its noinline wrapper
keeps callback arguments and return values below the comparison boundary. The
C backend validates that frame against the game stack and compares/restores all
older stack bytes, including caller locals and stack-passed arguments. Return
values are compared by Zig value equality, not struct padding. Nested calls run
on their enclosing original/owned side; the enclosing transaction compares their
combined effects (individual nested mismatches are not yet localized).

CTest `shadow_route` calls the exported eight-argument wrapper from C with a
pointer to a local. It checks clean equality, wrong caller output, wrong state,
a real routed panic, a wrong return, and nested original/owned execution. The
`owner {action: "report", reset: bool}` RPC serializes up to 32 mismatch/error
records, with function, frame, address/range, expected/actual bytes and panic
message. A failed serialization does not reset the report. Coverage lists
root transactions and nested invocations per function (nested invocations
include both original runs and the owned run).

Routed calls execute the original twice from the same input, with two dead-stack
fill patterns, before the owned execution. Only bytes stable between those two
original executions are compared. This applies to state, SPU and caller outputs;
unstable return values are likewise excluded. These exclusions are local to the
transaction, never installed as global masks. The first original's state,
caller outputs and return value remain authoritative. The second original
suppresses host effects and keeps nested routes on the original side.
The lower-level `pc_shadow_compare` test API deliberately retains single-reference
semantics. `shadow_route` also tests noisy outputs/returns and that adjacent
stable wrong state still produces a mismatch.

Game shadow is opt-in on Linux x86-64 with `ROOD_ORACLE=1`. This profile bypasses
modern menu-shell entrypoints on both sides. Speculative text capture and RPC
sound counters are suppressed; packet changes before text capture still run.
The `shadow_battle_smoke` runs enemies in zone 48, room 10 and requires nonzero
routed calls with no mismatches/errors. This is not the full corpus or a claim
that every PC feature is disabled. The smoke then injects a wrong owned write to
`vs_main_stateFlags[0]`, requires its function/frame/address in the report and
checks rollback. Test injection is explicit: `owner {action: "fault", function:
"name", address: N}` requires oracle mode and an address in a captured range;
`function: "", address: 0` clears it. This simulates an owned-code write; the
separate routed Zig fixture contains genuinely incorrect owned implementations.
Nested-call localization and full battle-corpus coverage remain later work.
`pc_shadow_speculative()` exposes the isolation boundary.
In particular, the battle-action direct-call closure reaches text capture,
menu-shell hooks and the RPC sound counters, whose mutable host state is not
part of the game images. Memory rollback alone cannot undo those side effects.
Do not broaden the enabled subsystem set without auditing its host hooks.

See [CPU-Oracle.md](CPU-Oracle.md) for the experimental null backend, measured
CPU-lane throughput and the still-unproven cross-backend parity gate. The local
corpus and its explicit coverage gaps are in `pc/tests/diff/CORPUS.md`.

## Known hazards: partly written locals copied whole

The original C often fills only the used fields of a stack local and then
copies the whole struct into game memory (or passes it to a callee that
does). On the PS1 the unwritten bytes are PS1 stack residue; here they are
dead host stack, which differs between owned and original routes (a Zig
callee leaves different frames) and between builds. Tick and alternate
modes then report a divergence that no port caused, and shadow mode
reports one when the residue is left by calls inside the transaction (both
original runs leave the same residue, so the two-run noise detection
cannot mask it).

Fixed: `func_8007138C` (146C.c) zeroes its effect request under ROOD_PC.
`func_800CF0E8` copies the request whole into `D_800F5230` (0xE8 bytes) and
`func_800CE714` copies `targets[0]` whole into each effect node; only the
reserved bytes, the last four bytes of an actor target's union, the targets
past `count`, the others past `otherCount` and `position.pad` were dead
stack (offsets 0x18..0x60 diverged with `battle_38C1C_port` owned). No
reader found interprets them. Regression: run.py `shadow_battle_smoke_38C1C`.

Fixed: `func_800E1388` (771A8.c) zeroes its `dest` tile under ROOD_PC. It
writes only `dest.p.x`/`dest.p.z` and copies the whole word into
`state->destination`: `p.y` and `p.status` were dead host stack, and with
`battle_71618_port` owned (`func_800D9E18`, which reaches it through
`func_800DB370` -> `func_800E24EC`) call mode diverged in `destination.y`
(0 owned, 64 original) on enemy_cast_wait and action_name_window. Found
with a `gdb` write watchpoint on the guest byte (the method below).

Fixed: `func_8008631C` and `func_800863A4` (146C.c) zero their hit request
`sp10` (a `D_800F19CC_t2`, 0x84C bytes) under ROOD_PC. They fill a few
fields and `func_80085B10` copies the rest (`unk844`, the hit entries'
SVECTOR padding) into its result, the evaluation block `D_800F5900`; with
`battle_74088_port` owned (`func_800DCAA0` calls both) call mode diverged
in that block on enemy_cast_wait and action_name_window. Found with the
same watchpoint: the byte's last writer was `func_80085B10`'s copy.

Found by watching the bytes: `ROOD_BINARY` can be a wrapper that runs the
game under `gdb -batch` with a Python script setting access watchpoints on
the guest addresses (they are host addresses in the 64-bit build) whose
`stop()` logs a backtrace and returns False; drive the recipe with the
normal RPC.

Not fixed (no recipe diverges on them yet; zero the local under ROOD_PC
the same way when one does):

- the other effect requests, `func_800CF0E8_t` locals with a few fields
  written, passed to `func_800CF3F8`/`func_800CF0E8` (copied whole into
  `D_800F53C0`/`D_800F5230`): 146C.c `func_8006C4C4`, `func_8006F450`,
  `func_8006F4B0`, `func_8006F848`, `func_800733F8`, `func_80073D30`,
  `func_8007BE20`; 5BF94.c `func_800CF7A8`, `func_800CF8BC`;
- SVECTOR/VECTOR locals whose `pad` is never written, copied whole into
  game memory: 146C.c `func_800719FC` (`origin` into
  `D_800F19CC->unk29B8`), `func_80085B10` (`impact` into a hit entry),
  `func_80089DC0` (`lookAt` into `vs_scratch.camera.lookAt`); 3A1A0.c
  `func_800A76BC` (`probe` into `D_800F4B08`).

The SVECTOR/VECTOR list comes from a scan of the BATTLE units for locals
copied with `= local;` and no `.pad` or whole assignment; locals passed by
address to a callee that copies them are not covered by it.
