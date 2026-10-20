# Testing faster

How to verify a change without waiting for everything, and what the test
runner does to stay quick and out of a player's way. The suites themselves
are listed in `pc/README.md` (Running the tests) and by `run.py --list`.

## The standard verify

```sh
pc/tools/verify.sh                    # 4 builds + the 32-bit GL and 64-bit Vulkan suites
pc/tools/verify.sh --changed          # the same builds, only the suites the change can affect
pc/tools/verify.sh --changed my-base  # ... since another base than main
pc/tools/verify.sh -- -j 8            # more run.py options for both suites
```

It builds `build_boot.py`, `--battle`, `--arch 64` and `--target windows`
(`--no-windows` skips that one), then runs both suites, and prints one line
per step (result, seconds, the suite's pass count, the log under
`pc/build/verify/`). With `ROOD_HEAVY_LOCK` set, every step holds that lock
(`flock`) for its own duration only, so parallel sessions interleave.

## Only the affected suites: `--changed`

```sh
python3 pc/tests/run.py --changed            # what changed since main
python3 pc/tests/run.py --changed=HEAD~3 ui  # since HEAD~3, and only suites matching "ui"
python3 pc/tests/affected.py --files pc/src/emissive.cpp   # what would this run?
python3 pc/tests/run.py --changed-files pc/src/emissive.cpp # ... and run it
python3 pc/tests/affected.py --all-files     # tracked files no rule covers
```

The changed files (commits since the merge base with BASE, uncommitted and
untracked files) are mapped to suites by the table `pc/tests/affected.toml`:

- `[core]`: the runtime, RPC, snapshots, the shared renderer, the build and
  the harness itself (`run.py`, `checkpoints.py`, `rood_rpc.py`...); one of
  them runs everything.
- `[[rule]]`: paths to suites (`pc/src/lua/*` to `lua_smoke` and `ctest`,
  `pc/src/ui/*` to the menu suites, `src/BATTLE/*` to the battle, charging and
  soak suites, docs to nothing...).
- `[baseline]`: `rpc_smoke` and `golden_frames` for any change to what the
  game is built from or loads.
- Derived from the tree: a test script selects the suites that run it or
  import it (transitively); a file `pc/CMakeLists.txt` names, or a probe,
  selects `ctest`; suites resuming a checkpoint bring `snapshot_equivalence`.
- A file nothing covers runs everything. Add a rule rather than guessing.

The runner prints why each suite was chosen (or which file forced the full
suite).

## What limits the suites, and the lanes

The GPU. Measured on the RTX PRO 6000 (32-bit GL, the save room, 1200
vsyncs stepped): one game steps ~560 vsyncs/s using a quarter of the GPU;
two ~1120/s; four or eight together ~1430/s in all, the GPU 85-90% busy
and each game waiting on its frame fence most of the time. A stepped vsync
costs ~0.45 ms of GPU whatever the internal scale, window size, AA or
lighting preset (fixed per-frame work), so throughput is frames rendered,
and past three or four games stepping at once more games only queue. In a
full `-j 8` run the GPU was busy the whole time while the CPU was ~40% busy.
Booting, Xvfb/EGL start-up and save-state restore are small next to it
(a resumed checkpoint is up in ~1 s; a boot to the save room ~4 s).

So the runner:

- runs the suites that run the game (`Suite.gpu`) and the rest (CTest,
  lints, scans, build checks) in separate lanes: `--gpu-jobs N` (default
  `-j`) and `--cpu-jobs N` (default 2), so a lint never takes a game slot;
- runs at most `--heavy-jobs` (default 2) of `HEAVY_SUITES` at once (the
  path tracer, probe GI, NRD): with more, the shared GPU made them fail
  (light not settled after N vsyncs). They form a queue of their own, so the
  list stays short (their summed time / `--heavy-jobs` is a floor for the
  wall time: with soaks and paced suites in it a full run took 897 s instead
  of 790), they start first, and a free slot takes the next suite it may
  run, so a capped one never holds a slot;
- leaves the path tracer's automatic budget (`ptgi.c`) on: it times the
  shared GPU, so under load it traces less often. Pinning it for every suite
  (`ROOD_GI_BUDGET=0`) made the whole run GPU-bound for longer (852 s
  instead of 790); `save_point_glow_smoke`, whose light must settle in a
  fixed number of vsyncs, pins it itself;
- lowers the redraw rate of a paused game nobody is talking to
  (`ROOD_PAUSED_REDRAW_HZ=20`; it still redraws at once after every
  request): 4 paused games took ~10% of the GPU at 60 Hz, ~1% at 20;
- waits up to `ROOD_LAUNCH_TIMEOUT` (180 s) for a starting game's RPC
  socket instead of 30 s (an exited game fails at once).

## Temperature

`run.py` defaults to `-j 3` (`$ROOD_TEST_JOBS`): past three or four games
the GPU is saturated anyway (above), and eight at once kept the CPU package
near its limit. Before each suite starts it reads the package temperature
(hwmon: coretemp `Package id 0`, k10temp `Tctl`); at `--thermal-pause` (95
C) or more no new suite starts until it is down to `--thermal-resume` (85
C), at most `--thermal-wait` (900 s) at a time; the running suites go on.
The summary line says how long it waited. `run.py --wait-cool` only waits
(`verify.sh` runs it before every step); `--thermal-pause 0` turns it off.

## Waiting in a suite

A suite waits for something the game reports, never for a guessed number of
seconds or vsyncs: the modern menus glide and files are polled on real
time, a loaded machine is slower, and a thermal guard may stop (SIGSTOP) a
test game for 5 s or more while the CPU is hot.

- File edits: `game.wait_polled("files" | "plugins" | "theme")` runs the game
  until that watcher has looked at its files since the edit (RPC `status`
  `polls`: the twice-a-second look at settings.cfg, the room's lighting
  files, emitter assets and fog presets; the plugins' hot reload; the UI
  theme), then the suite checks the exact value. Where an RPC reloads at once
  (`lights reload`, `particles reload_assets`), use that.
- State the game reaches: `game.until(condition)` (or the suite's own
  condition loop), e.g. RPC `ui_menu` `at_rest` for a finished glide, the
  settings page's `dirty` flag for a debounced save; its timing, where it
  matters, from the game's own clock (`system.debounce`: the save came at the
  first tick after 1 s of quiet).
- What remains of a timeout is a hang guard, generous, counted by
  `rood_rpc.Deadline` / `awake()`: wall time less the time the suite's games
  spent stopped (sampled from /proc). A latency a suite measures (a hot
  reload's) uses `awake()` too.

`pc/tests/stall_inject.py` reproduces such a race: it runs a suite with its
game stopped for `--seconds` at a chosen moment (before an RPC request, from
a source line, after a file write or an X11 key), and `run.py --stall
"OPTIONS"` does it for the suites of a run, e.g.

    python3 pc/tests/run.py --stall "--site 'sysv()[\"dirty\"]' --nth 2" ui_system_smoke

## Lower impact on a player

- Every suite runs at `nice 10` (`--nice N`, `0` = as is) in the idle I/O
  class.
- NVIDIA's OpenGL spins a core while it waits for the GPU; the suites set
  `__GL_YIELD=USLEEP`: same throughput, each of 8 games at ~0.4 of a core
  instead of 1.0 (a full run: ~40% CPU busy before, ~20% after).
- The Vulkan renderer asks for a low-priority queue
  (`ROOD_GPU_PRIORITY=low`, `VK_KHR_global_priority` where the driver has
  it), so a game being played keeps the GPU first. OpenGL through SDL has no
  context priority.
- Stepped suites run unpaced (`--fast`) as before; only what checks wall
  time runs paced (`skip_charging_smoke`'s wind-ups, compared with the
  option off in the same run; its casters' run-on is unpaced now).

## Checkpoint save states

`snapshot_equivalence` validates the four checkpoints in parallel
(`save_room`, which most suites need, first), and each suite waits only for
its own checkpoint instead of all of them. A checkpoint validated for this
build (the cache directory is keyed by the game binary and the disc data)
by the same harness (`checkpoints.harness_key()`: `checkpoints.py`,
`snapshot_equivalence.py`, `snapshot_diff.py`) is not validated again;
`snapshot_equivalence.py --again` does.

## Measured

All on the RTX PRO 6000 / Core Ultra 7 265K (8 P + 12 E cores), wall time
of `run.py`, main's harness against this one on the same build:

| Run | Before | After |
| --- | --- | --- |
| 32-bit GL, full, `-j 8` | 790 s, 108/111 (`save_point_glow_smoke` failed; 765 s and 3 failures on an earlier main) | 790 s, 109/111 (2 skipped, none failed) |
| 32-bit GL, full, `-j 5` | | 905 s, 109/111 |
| 64-bit Vulkan, full, `-j 8` | 372 s, 109/111 (`editor_library_smoke` failed) | 300 s, 109/111 |
| CPU busy during the 32-bit run | ~43% | ~20% |
| `snapshot_equivalence` (first job; the checkpoint suites wait) | 34 s, all four in a row | 15 s new build (save_room ready after ~9 s), 0.2 s same build |
| 4 paused games, GPU busy | ~10% | ~1% |
| `--changed`, a Lua runtime file (4 suites, `-j 3`) | full run | 22 s |
| `--changed`, `emissive.cpp` (8 suites) | full run | 20 s |
| `--changed`, a modern-menu file (32 suites) | full run | 375 s |

A full run is GPU-bound, so its wall time hardly moved; what changed is
that it passes under load, takes half the CPU, and that most changes need
only part of it. The next lever is the GPU's fixed per-frame cost (a stepped
vsync renders and presents a full frame even when no suite looks at it).
