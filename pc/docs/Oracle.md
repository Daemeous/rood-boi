# State oracle (ownership tasks 14–17)

Linux/x86 only, in one running process. No Zig toolchain is required. The
oracle observes the original game; it does not yet implement shadow calls.

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
error rather than an unchecked write. Shadow mode itself remains Task 18.

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
for the bulk port. Recoverable Zig panics, field mapping, routed stack outputs,
feature-hook isolation and whole-overlay coverage remain later phase-B work.
