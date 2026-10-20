# Split-engine rewind: native draw-list IDs escape the snapshot

Investigation on 2026-10-19. No reload is necessary to reproduce the GPU
replay mismatch reported by `zig_hotswap_smoke.py`. No comparator exclusions
or production restoration code were changed in this investigation.

## Control and results

Used private copies of the existing monolithic boot64 binary and the
zig-hotswap worktree's dev64 host, engine and game libraries. The latter
were the artifacts built at 21:40; the source branch was at d3f27a57.
The investigation checkout was based on main edc735ed. This is an artifact
comparison, not a claim that these two binaries differ only in link mode.

All game launches used the heavy-test lock, private settings and a copy of
the pinned test card. Replay runs used existing artifacts and performed no
reloads. The recipe:

1. Disable checkpoint caching (`ROOD_TEST_SNAPSHOTS=0`), enable
   `ROOD_DETERMINISTIC=1`, `ROOD_ORACLE=1`, and disable GI.
2. Private settings: `ui_menus 0`, `widescreen 0`, `interpolate 0`,
   `qol_skip_charging 0`, `camera_stick 0`.
3. `launch_at("save_room", ..., fast=True)`; request
   `room_goto(zone=48, room=10, enemies=True, scripts=False)`.
   Run at most 80 batches of 20 frames, stopping when the transition phase
   is 0 or 4; assert the requested zone and room.
4. Mark once. Rewind, run 600 frames, save A. Rewind **the same mark**,
   run 600 frames, save B. Neither leg reloads any code.
5. Compare using `snapshot_diff.read`, `snapshot_diff.Symbols(binary)` and
   the unchanged `snapshot_diff.diff`; also compare snapshot vsyncs.

| Artifact / renderer | Result |
| --- | --- |
| Monolithic boot64 / GL | Byte exact |
| Split dev64 / null | Byte exact |
| Split dev64 / GL | 2,624 RAM bytes differ; reproduced twice |
| Split dev64 / Vulkan | 2,624 RAM bytes differ |
| Split dev64 / GL, restore counter only | Byte exact |
| Split dev64 / Vulkan, restore counter only | Byte exact |

Seven runs total. GPU endpoints were vsync 2483 on both legs; null endpoints
were 2494 on both legs. Other compared snapshot ranges matched. GL used
llvmpipe (Mesa); this was not a physical-GPU performance test. The counter
intervention is explained below, not a shipped fix.

## What the differing bytes mean

The final unmodified GL control contained 1,312 differing 32-bit RAM words,
with two differing bytes per word. **Every word's delta was 1,564 (0x61c).**
Of those words, 1,303 immediately followed `0x00544e44` (TND), and eight
immediately followed `0x00524e44` (RND). The remaining word at `0x8013fec4`
was in the same command-buffer region but did not immediately follow a
marker; do not describe it as a validated live packet.

Examples:

| Guest address | A | B |
| --- | --- | --- |
| `0x801383b8` | `0x849` | `0xe65` |
| `0x80139c84` | `0x84a` | `0xe66` |
| `0x80139f90` | `0x84b` | `0xe67` |

`pc/src/render.c` explains the 20-byte strides:

- `render_list_begin` allocates a 24-bit list ID from `next_id`.
- `render_list_link` writes a three-word RND packet, including its tag.
- `render_list_link_translucent` writes a five-word TND packet: tag,
  marker, list ID, first polygon, count.
- Both place the ID in `packet[2]` in guest RAM.

The comparator's nearest-symbol labels for these heap addresses are not
reliable identification of the allocation. Packet words and the writer
code establish the interpretation instead.

## Restoration and link-boundary cause

`render.c` already declares:

```c
static uint32_t next_id PC_SAVED_STATE = 1;
```

Its accompanying comment explicitly says IDs are saved because they are
written into game packets. That works in the monolithic link.

In the inspected split artifact, `readelf -SW librood_engine.so` reports a
four-byte `rood_state_data` section. `nm -S` places the render counter there
at `0x64d837a0`. This is an artifact-specific address, **not an API or a
proposed hard-coded restoration address**.

`pc/src/snapshot.c:ranges` enumerates the executable's
`__start_rood_state_data` / `__stop_rood_state_data` and BSS, guest RAM,
scratch/I/O and overlay statics. The engine library's four bytes are not
in the resulting snapshot memory chunks. `pc_state_ranges` uses that same
list (and VRAM).

`pc/src/state_rewind.c` copies exactly those ranges at mark and restores
them at rewind, plus stack/context, SPU and fog. It then calls
`pc_cd_restored`, `pc_hw_resync`, `pc_input_rewound` and re-enters the marked
vsync through `pc_tick_resumed`. It cannot restore a variable absent from
its range list.

`pc_hw_resync` flushes, ends multisampling, forgets recordings, dirties the
VRAM tiles and uploads restored native VRAM. That presentation-history
reset does not restore the draw-list ID allocator. Clearing more render
history or resetting the allocator to a fixed constant is not equivalent
to restoring its marked value.

`pc/tools/engine_boundary.py` transfers globally visible engine variables
referenced by host objects into host-owned storage. A private static such
as `next_id` is not captured by that mechanism. Merely adding the saved
state annotation therefore does not ensure host snapshot coverage in the
split build.

## Causal intervention

In two additional, private split-host processes, read the counter's four
bytes through `/proc/<child-pid>/mem` immediately after marking. After each
rewind, write those same four bytes back, **before** running the 600 frames.
No other memory was changed, and the comparator was unchanged.

Both GL and Vulkan reported:

- Value at mark: `0x233`.
- Value after first rewind: `0x233`.
- Value after second rewind, before intervention: `0x84f`.
- Restoring the marked `0x233` before both legs: byte-exact A/B comparison.

`0x84f - 0x233 == 0x61c`, exactly the delta in every differing RAM word.
This isolates the missing counter restoration as sufficient to explain
this reproducer; it does not prove every engine setting/history field is
correctly snapshotted, or prove engine reload itself is fully equivalent.

## Production follow-up

Make saved game-visible engine state participate in the host snapshot
contract. Prefer host ownership for this allocator, with a build-time
check preventing saved-state sections from silently remaining in engine
objects. Alternatively, explicit engine state serialization must cover
snapshot save/resume, in-process rewind, state layout compatibility and
engine reload lifetime together. Do not store raw addresses into an
unloaded engine, mask command-buffer addresses, or relax `snapshot_diff`.

Add a registered no-reload replay regression for the split host on GPU
lanes, failing before the fix and passing afterward; then rerun the Zig
reload replay oracle. The diagnostic counter write above must not become
the regression's normal path. A production fix and those regression runs
remain undone; this report records the investigation only.

## Verification limits

`pc/tools/verify.sh --changed` was attempted afterward with the required
build/heavy lock variables. Three build jobs failed because this fresh
checkout lacked the generated `build/config/SLUS_010.40/link.ld`. The null
suite failed before running tests because it treated `null_backend_smoke`
as a Git comparison ref. GL/Vulkan suite selection reported nothing to run
for this documentation-only change. This is not a successful full verifier
run. No `src/` changes were made, so commit-check was not applicable.
