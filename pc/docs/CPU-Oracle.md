# CPU-only oracle backend (experimental)

`ROOD_RENDERER=null` retains game execution, RPC input/stepping, CPU PS1
rasterization and emulated VRAM. It creates no window, video context, hardware
render target, Vulkan device or presentation path. CPU VRAM is game-observable
memory (StoreImage, copies, textures); it is deliberately **not removed**.
Graphics libraries remain linked, but are not initialized.

`pc_window_init` preserves logical controller presence, loads the private
settings profile, and returns before SDL video initialization. Existing inactive
hardware paths keep CPU rasterization authoritative. `pc_hw_init` also rejects
null explicitly. Python launch avoids EGL probing/Xvfb and removes display
variables. A null + desktop-window request is rejected.

## Running

```sh
python3 pc/tests/run.py --arch 64 --renderer null --cpu-jobs 2 \
  null_backend_smoke shadow_battle_smoke ownership_smoke battle_corpus_candidate
```

These allowlisted game suites use the CPU lane while retaining binary/staleness
dependencies. Other game/visual suites cannot accidentally enter the null lane.
Snapshot caches are disabled there; the corpus owns its private pinned artifacts.
The thermal gate and nice policy still apply. `verify.sh --changed` runs this
CPU-only step **outside** `ROOD_HEAVY_LOCK`; Vulkan/OpenGL suites retain that lock.
Builds retain `ROOD_BUILD_LOCK`.

`null_backend_smoke` checks an invalid SDL video driver, forbidden Python EGL/Xvfb
helpers, one child process, inactive hardware, RPC stepping, and absence of
GPU-device descriptors or child display servers on Linux. This is resource
absence evidence, not a claim that the executable has no graphics dependencies.

## Measured in this worktree

* Null resource, real shadow battle/fault and ownership suites: 3/3 passed.
  One CPU worker: 29 s; three workers: 21 s (about **1.38x** throughput for this
  mixed sample, no thermal wait reported). This is not a general scaling claim.
* The witnessed 600-tick enemy-cast corpus passes original control, call shadow,
  owned-per-tick and alternating owners separately on Vulkan and null. One
  observed Vulkan run took 38.3 s; null 36.5 s (about **1.05x**, small enough that
  system load matters). The larger benefit is removing GPU-lock contention.
* A separate updated null corpus run also passed in 38.79 s, illustrating the
  variance. Do not infer a large single-process speedup.

## Not yet a parity gate

The two renderer runs currently generate their initial room states separately.
One measured pair ended at vsync 2483 versus 2494. Their enemy-action witnesses
matched, but raw component hashes differed. That is **not a valid matched-state
cross-backend parity comparison**, and it is not a passing gameplay-digest proof.
A subsequent null run **did** resume the Vulkan candidate's exact `start.snap`
and replay its `inputs.rec`: both ended at vsync 2483 with the same enemy-action
witness. End-state **VRAM, scratch, IO and overlay hashes matched**; raw RAM and
combined state hashes still differed. Those remaining differences are not yet
localized and must not be assumed harmless. This is encouraging endpoint
evidence, not per-tick gameplay parity. Canonical per-tick comparison and wider
StoreImage/readback coverage remain required.

Default native Vulkan rendering can omit CPU reference packets and later read
rendered pixels back into game-visible VRAM. Null cannot simply erase those
semantics. First establish parity under a matched classic/reference profile;
do not claim default-native full-VRAM parity or use this experimental backend
to approve a subsystem ownership flip. The task20 corpus is also incomplete;
see `pc/tests/diff/CORPUS.md`.
