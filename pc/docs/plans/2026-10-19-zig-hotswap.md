# Zig hot swap (plan C)

Status: runtime integration implemented; acceptance results below. Builds on
the A5 engine host, not a second game runtime. Development only, Linux x86-64.

## Build and boundary

`build_boot.py --arch 64 --engine-library` with Zig available builds
`librood_game.so` alongside `rood_host` and `librood_engine.so`. The owned
modules under game/ use Debug, PIC and unwind tables. Keep the pinned 0.17
compiler and its persistent local/global caches. Try the self-hosted backend
and incremental compilation only where that compiler supports the shared
library target; otherwise use LLVM and the cache. Do not claim incremental
compilation merely because unchanged builds hit the cache.

Generate the library exports and host imports from ownership.toml. Every
`name__owned` is a stable host trampoline through a replaceable slot; both
owner routing and shadow's candidate call must go through it. The library
exports the implementations under separate names to avoid ELF interposition
binding a candidate back to its own trampoline. Original functions, shadow
transactions, ownership settings and routing remain in the host. Changes to
the ownership manifest or ABI require rebuilding the host and restarting.
Resolve and validate every candidate slot before publishing any of them.
Preserve the selected owner and shadow settings when repointing slots.

The core Zig ABI probe and non-routed code need not be reloadable. No-Zig
builds retain their existing original-only behavior. Static Linux and Windows
builds retain the existing ReleaseSafe objects, routing and static linkage;
shared-library flags must not leak into those build paths.

## State and lifetime

Owned functions are currently pass-through implementations. Their durable
state is the game's memory: RAM, pinned game globals and overlay statics.
The library must not own persistent mutable globals, heap state, threads,
callbacks or pointers retained by the host. A future native-state port must
supply a versioned, explicitly migrated host-owned state block before it can
participate in hot swap. Reload itself neither snapshots nor restores the
game: battle/field state stays where it is. String and function addresses
inside the library must not escape an owned call.

Reuse A5's host-side vsync service after RPC polling, immutable per-generation
library files and unwinder-based live-frame check. A request made while an
owned function is suspended in a call to the original (including a nested
vsync) is refused, not deferred by advancing a paused game. Check the entire
old library address range, not just routed entry addresses. Unwind failure
must fail closed. Never unload a library while an owned frame is live.

Load a fresh candidate alongside the old library with local symbol scope;
validate its ABI/route inventory and resolve all slots first. On failure,
close the candidate without changing the live slots or generation. At a
safe boundary publish the complete slot set, then release the previous
handle. An engine reload must leave the game-library handle, owner selection
and slots intact; all are host state.

## Controls and edit loop

Keep bare RPC/console `reload` meaning engine reload for compatibility; add
an explicit Zig target. Reuse existing subsystem `owner` and shadow controls,
and expose the same target in rood_rpc.py. Return generation, frame and reload
time so callers can distinguish refusal from success. The dev-loop watcher
watches game/ as well as the existing engine sources, builds the relevant
library under `$ROOD_BUILD_LOCK`, and requests the corresponding reload only
after successful atomic publication. Failed compilation leaves play running
on the previous library. Structural changes refuse reload and require the
full host rebuild/restart path, never a partial reload. Watch mode must not
overwrite source edits.

## Acceptance and measurement

Register zig_hotswap_smoke in run.py and affected.toml. Use a temporary source
copy/build artifact, a pinned test card and the existing battle checkpoint.
Prove a pass-through owned edit actually executes after reload in the same
PID; compare game-state hashes immediately around a paused reload and compare
subsequent deterministic frames with an unmodified run. A deliberate wrong
field write must be reported by shadow with the field name and frame. Also
exercise a missing export and a live owned frame: both refuse without changing
slots, generation or state. Restore/rebuild the clean library afterward.

Measure wall time from source publication through build to the first frame
executing the edited function (not just the load acknowledgment); report build,
reload and total separately, compiler/backend and cache conditions included.
Run the required no-Zig boot, static 64-bit and Windows builds, engine reload,
snapshot resume/equivalence, ownership and shadow regression suites, and
changed verification under the build/heavy locks. No performance claim or
completion claim until these checks have actually run.

## First implementation checkpoint

- Design committed first: `abdb1fd5`.
- Routing foundation: `03b6826b`. `routing_source(..., reloadable=True)`
  generates stable host `__owned` trampolines and replaceable slots for
  sysv64 only. No build enables this option yet; the default generated
  routing is unchanged. The future loader must bind every slot before use.
- Ownership unit checks: 43/43 passed. The compiled C harness checks owner
  selection, shadow calls, retained function addresses, slot replacement,
  integer/FP/stack arguments and return values. This is not a game reload
  smoke test and does not prove unload safety or state preservation.
- Changed verification passed with both lock environment variables:
  `pc/tools/verify.sh --changed main -- -j 6`, 70 s total. Three builds
  passed; Vulkan and GL selected suites each passed 1/1; null selected no
  suites. Static 64-bit was also rebuilt after the no-Zig check.
- Explicit GL/EGL regression run: 5/5 passed in 41 s: engine_reload_smoke,
  snapshot_resume_smoke, snapshot_equivalence, ownership_smoke and
  shadow_battle_smoke.
- No-Zig 64-bit build passed; a null-renderer boot reached the title,
  reported no owned subsystems and advanced 60 frames, using temporary
  absent cards. Windows build passed under the build lock at nice 19.
- No files under src/ were changed; commit-check was not required.

At this checkpoint the goal was not delivered; the following implementation
supersedes that limitation.

## Implemented development path

The split builds two Zig variants. The host retains the ReleaseSafe shadow
transaction wrappers, whose owned callbacks call stable `__owned` trampolines;
only the implementations (`__candidate`) move into the Debug library. The
static build still exports `__owned` implementations directly, as before.
`game/core` remains static. The host exports public game symbols (including
absolute guest globals) and the panic/pointer-check services. A new dependency
on an unexported host service fails loading rather than inventing a trap.

`game_lib.py` compiles Debug PIC objects and links a library exporting only the
route implementations, compiler-emitted function signatures and a route-inventory
ABI key. It publishes with rename
on success. Stable generated module paths and local/global caches avoid
unnecessary compilation. The pinned Zig 0.17 self-hosted PIC/incremental probe
linked with an invalid `.eh_frame` warning; this is unacceptable for live-frame
refusal. LLVM with `-fincremental` reported `TODO implement saving linker state
for lld`. Therefore this path explicitly uses LLVM and unwind tables, with
cached non-incremental compilation, not `zig build --watch`.

`game_host.inc` is part of A5's engine_host.c, not an engine unit. Startup loads
the game library before gameplay. Reload requests are serviced by the existing
A5 vsync hook. The real-frame unwinder must reach `vs_main_exec` and find no
address in the old library; a missing unwind chain refuses too. Candidate ABI,
all symbols, function signatures and mapped range are checked before touching
live state. Publication is one atomic exchange of the complete table pointer;
the x86-64 trampolines load it before their indirect tail jump. No game code or
callbacks run during publication on the sole game thread. The previous library is released
only afterward. This is not a multi-threaded dispatch API: owned background
threads and escaping library pointers remain prohibited.

An unchanged on-disk inode can cause dlopen to return the same handle; the
loader obtains the actual map name with dlinfo rather than assuming the new
hard-link filename appears in the loaded module list. A failed candidate does
not advance the generation or unload the current library.

### Commands

Start the split with `pc/run-game.sh --reload`. Then:

```sh
python3 pc/tools/rood_rpc.py owner battle_action owned
python3 pc/tools/rood_rpc.py reload zig
python3 pc/tools/rood_rpc.py shadow battle_action on
pc/tools/dev_loop.sh --zig
pc/tools/dev_loop.sh --watch
```

Set `ROOD_RPC` to the running game's socket and `ROOD_BUILD_LOCK` to the shared
build lock. Python clients have `Game.reload("zig")` and
`Game.shadow("battle_action", True/False)`. Console commands are
`owner battle_action owned`, `shadow battle_action on` / `off`, and `reload zig`;
bare `reload` remains the engine reload. Shadow off selects original execution.
The watcher polls both game/ and engine sources, notices edits made during a
build, and does not modify source files. Library ABI changes are refused and
require a full host rebuild/restart; native-state migration is not yet needed
or implemented, because the current owned code uses existing game memory.

### Shadow and saved-stack details

The development host compiles original game C with
`-ftrivial-auto-var-init=zero`. Its code copies partly initialised aggregates
into game memory; the larger Debug-owned stack exposed incidental bytes that
had coincidentally agreed between the two original shadow runs. Zeroing these
locals/padding in the development host stabilises the reference without masking
any captured game address or weakening shadow's comparison. Static Linux and
Windows compilation are unchanged. The original shadow poison/repeat algorithm
is unchanged. `route.original` is inline so resolving a compile-time symbol does
not introduce a Debug error-trace frame.

Save and mark also use the host's unwind guard: a stack with a live owned frame
cannot be saved for a later generation. Safe marks remain usable across reloads,
as the smoke proves by replaying the same battle recipe. Static-build snapshots
are not interchangeable with the split host's differently compiled, pinned game
code; engine_reload_smoke boots its checkpoints rather than attempting to resume
the test runner's static snapshots. Normal snapshot invalidation remains intact.

### Runtime evidence

The registered `zig_hotswap_smoke` uses a private source tree and private card,
never edits the checkout's game sources, and launches one host PID. Its battle
recipe selects an exercised function, edits a real Zig field write, rebuilds,
reloads while paused, and single-steps until shadow reports the edited function,
`vs_main_stateFlags[0]` and frame 2033. Full snapshot hashes, vsync and pause are
unchanged by reload. The wrong write is rolled back by shadow; clean restoration
removes the mismatch and repeats the unmodified deterministic frame sequence,
compared byte-for-byte on the null backend with the existing snapshot-equivalence
comparator (which excludes `last_vsync` wall-clock time and saved host stack
frames). GPU runs validate full paused reload preservation, shadow and ongoing
presentation; GPU rewind history is not byte-deterministic in this recipe and
is a separate follow-up, not masked away by this test. The builder
also refuses mutable module/TLS statics without replacing the published library.
The test covers broken ELF input, a missing route export, an incompatible
function signature, live-frame refusal, unsafe save/mark refusal, and an
unpaused owned game continuing after reload. Controls exercised include RPC,
console, Python CLI and the dev-loop's one-shot Zig reload path.

Two GL/EGL fast-mode samples, with warm LLVM caches and a prebuilt host, measured:

| Sample | Build | Host reload | Edit to first edited execution |
| --- | ---: | ---: | ---: |
| 1 | 0.118 s | 0.240 ms | 0.862 s |
| 2 | 0.106 s | 0.268 ms | 0.676 s |

These include 151 single-stepped vsyncs waiting for the selected battle function
to execute; they are not just reload acknowledgments. Null-renderer samples
were 0.686 s and 0.698 s. This is an observed field write in the game-level
oracle, not an image-difference test or a cold-build benchmark. Real-time play's
visibility also depends on when the edited function next runs. The watcher
adds its 200 ms polling interval and reports acknowledgment latency, not a
claim that an edited function has executed.

### Final verification

Final focused changed verification, with `ROOD_BUILD_LOCK` and
`ROOD_HEAVY_LOCK` set:

```sh
pc/tools/verify.sh --changed main --windows -- -j 4 \
  zig_hotswap_smoke engine_reload_smoke snapshot_resume_smoke \
  snapshot_equivalence ownership_smoke shadow_battle_smoke
```

Passed Linux 32-bit startup/battle and Linux 64-bit builds; null **5/5**, Vulkan
**6/6**, GL **6/6**. Windows ran last at nice 19 and was up to date; its preceding
cross-build succeeded. The null lane now explicitly includes hot swap. Separate
ownership routing tests passed (including a saved trampoline pointer, shadow,
integer/FP/stack arguments and atomic table exchange). A no-Zig Linux64 build
booted the title, exposed zero owned subsystems and ran 60 further frames.
`src/` was unchanged, so no commit-check was required. No protected save/user/
lights files were changed.

The earlier broad changed run was **not fully green**: Vulkan 121/125 with 2
skips, GL 120/125 with 3 skips, before the final hot-swap test corrections.
Hot-swap/loader failures were corrected and the focused run above passed.
A separate GL `glow_stability_smoke` failure (0.32 peak-relative light change
versus its 0.25 limit) remains a follow-up. The GPU rewind byte differences
above are also recorded separately. Neither broader issue was hidden by claiming
an all-green broad suite.
