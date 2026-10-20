# Local battle corpus (Task 20, partial)

This is harness infrastructure, **not the completed battle coverage gate**.
`scenarios.py` has one implemented candidate: `enemy_cast_wait`, the zone 48,
room 10, 600-neutral-frame recipe used by `shadow_battle_smoke.py`. Its shadow
run must compare at least one routed call, with zero errors or mismatches.
Each run also requires an ended enemy action with nonzero actor and a completed
wind-up, independently witnessed by the charge tracker. This does not cover
all spell IDs, outcomes or phases. Every other listed
scenario is explicitly incomplete (including the trial's kill, transform and
name-window paths). The default full-corpus invocation exits nonzero for these
gaps; selecting the candidate is only a harness smoke, not acceptance of Task 20.

## Commands

Run from the repository root. Use the project's heavy lock with bounded waits
and an outer timeout for game tests; do not run while the user is playing.

```sh
python3 pc/tests/diff/differ.py --list
flock -w 60 "$ROOD_HEAVY_LOCK" timeout 60s python3 pc/tests/diff/corpus_test.py
ROOD_ARCH=64 flock -w 60 "$ROOD_HEAVY_LOCK" timeout 600s \
  python3 pc/tests/diff/differ.py battle_action --scenario enemy_cast_wait
```

`--mode call tick alternate` is the default. `--card` defaults to the pinned
`pc/build/test-card1.mcd`, never the user's card. Each invocation gets a unique
ignored `pc/build/diff-corpus/run-*` directory. `--out` must remain under that
cache (resolved symlink/path escapes are rejected). Do not commit, upload, or
publish its contents: the states and recordings are game-derived data.

## Contract and artifacts

* `checkpoints.launch_at('save_room')` boots fresh with a private card copy.
  Shared checkpoint caches are disabled during corpus generation: they are not
  keyed by this startup feature profile. All owners start original. Setup runs
  once, then `snapshot mark` establishes the in-process baseline.
* `start.snap`, `inputs.rec`, `settings.cfg` and their SHA-256 manifest pin the
  generated state, neutral-input stream and profile. `target.json` pins the
  executable and source card hashes and records roots/routed function names.
  These are provenance pins of this generated corpus, not a portable-state
  promise or an approved golden-state hash.
* Original recording, original/original determinism control, shadow, owned and
  alternating runs all use the **same process and mark/rewind**. A failed
  original/original control stops owned comparisons. The only enabled mask is
  boot_runtime.c's host pacing `struct timespec`, selected by file-qualified DWARF
  with verified type/fields/size and recorded in the manifest. No game-field,
  effect, or dead-stack mask is silently enabled. Replay/digest modes are stopped before the next rewind.
* The classic profile disables all currently identified gameplay feature gates,
  modern menus, camera controls, interpolation, native rendering and lighting
  gates. `ROOD_ORACLE=1` also suppresses speculative host feature hooks. Safety,
  assembly replacements and LP64 adaptations stay intact. This is a pinned
  profile, not an exhaustive automatic audit of future settings/host state.
* `report.json` records incomplete/failed/passed status per scenario, full shadow
  RPC results, routed function coverage and uncalled names (relative to the
  selected subsystem's ownership table), frame/tick, and field-map diagnostics.
  Nested-only calls are listed separately: they were not independently compared.
* Call mismatches map their exact `first` address through `FieldMap`. Tick
  mismatches save both states at `first_tick + 1`; alternating diagnostics replay
  the alternating schedule, not a pure-owned substitute. Snapshot memory is
  compared and mapped for diagnosis; SPU-only/unreproduced differences are
  explicitly unresolved. Raw snapshots may contain host/stack noise outside
  digest ranges, so a snapshot's first differing field is a diagnostic candidate,
  not proof it caused the first digest mismatch.

## Verification and remaining work

`corpus_test.py` uses CPU-only protocol fixtures for rewind/owner sequencing,
control failure, partial digests, zero calls/errors/truncation, field annotation,
missing scenarios, private artifact manifests, bounded room setup and path guards.
These fixtures do **not** execute game code or establish corpus coverage.
Sixteen CPU fixture tests pass. The 64-bit `enemy_cast_wait` candidate passes
original/original control, call shadow, owned-per-tick and alternating owners on
both Vulkan and null, with an enemy-cast witness in every run. Initial live runs
found and fixed the RPC's record-only mask contract and the five-field ended
record format; the original/original control correctly exposed the host clock
before the narrow verified mask was applied.

`battle_corpus_candidate` is registered in run.py; `diff_corpus` in CTest checks
the protocol fixtures. This is still **not full task20 acceptance**: 22 recipes,
32-bit acceptance, and a corpus-driven deliberate-fault gate remain unfinished.
The separate real-battle fault smoke is evidence for the backend, not a substitute
for that coverage. Cross-backend runs currently generate their baselines
independently; their room transition endpoints can differ in vsync. Matching
witnesses or independent original/owned passes are **not cross-backend gameplay
state parity**. The report retains end-state component hashes so this limitation
cannot be concealed.
Changed-suite verification exposed an additional blocker: with run.py's saved
save-room checkpoint enabled, the Vulkan original/original control failed at
tick 154 in `state_bss` pages starting at `0x18490000`. Replaying both diagnostic
sides from the mark did not reproduce a memory difference. Fresh-boot candidate
runs passed. The shared checkpoint is not keyed by this corpus's startup feature profile.
Corpus generation now explicitly refuses that cache; targeted Vulkan and OpenGL
candidate reruns passed (36.9 s and 40.0 s) with the normal runner cache environment
present. A fixture verifies the cache override and environment restoration even
if launch fails. No VRAM/whole-state mask was added. Generic cached restore
normalization remains unproven; this fix enforces profile-compatible generation.

Do not use this partial corpus as a subsystem-flip gate.
