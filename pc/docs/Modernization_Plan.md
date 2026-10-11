# Modernization plan

rood-boi started as a vertical slice that runs the decompiled C on a recreated
PS1: the original data image mapped at `0x80000000`, ~940 globals linked to
their PS1 addresses (`pc/build/boot/imports.ld`), native code pinned below
16 MB so 24-bit ordering-table links can address it, a 32-bit ABI, and a
renderer that receives only the PS1's 2D GPU packets and reconstructs 3D from
them (GTE projection log, room depth records, caster capture).

That got the game playable quickly, and it is now the main source of
friction:

- Lighting bugs came from the 2D-to-3D reconstruction: black tiles (log
  eviction), stale depths (packet address reuse), the depth checkerboard.
- The free camera fights the game's culling and never-cleared screen.
- 32-bit rules out the Vulkan ray-tracing drivers (NVIDIA and Intel ship no
  ray query in their 32-bit Vulkan drivers) and modern libraries.

The goal is to pay the modernization cost early, as other PS1 decompilation
ports have found worthwhile, so later work builds on a native foundation.

Invariant throughout: the PS1 build stays byte-identical. Every `src/` change
is behind `#ifdef ROOD_PC` (or a PC-only header) and
`make commit-check` reports "All files match". Game data is never committed.

Upstream sync: `src/` tracks the decomp (ser-pounce/rood-reverse, PR #186
under review), which keeps renaming and reformatting. We sync at milestones,
not continuously (when #186 merges, or when upstream lands something the port
needs), and keep the port's footprint in `src/` small so a sync stays
mechanical: retype fields in PC-only headers rather than editing matched
source, keep `ROOD_PC` blocks small, put real logic in `pc/src`.

Estimates are agent time. The bottlenecks are verification throughput,
playtesting and merge order, not writing code.

## Order

| Phase | What | Agent time | Depends on |
| --- | --- | --- | --- |
| 0 | Fast, parallel-safe verification | a few hours | - |
| 1 | 64-bit with a native data model | ~1 day | 0 |
| 2 | Native 3D submission to the renderer | ~1-2 days | 1 |
| 3 | Vulkan renderer with hardware ray tracing | ~2-3 days | 2 |
| 4 | Data-first tooling (ongoing) | small pieces | - |

Phase 2 runs in parallel with the rest of phase 1 (decided 2026-10-09): it
gives most of the visible improvement and works on the 32-bit build, while
phase 1 is the prerequisite for phase 3 only (no ray tracing in 32-bit
Vulkan drivers). They mostly touch different code (phase 2: the port's
renderer and draw code in `pc/src`; phase 1: game structs in `src/`); the
port's own LP64 fixes are coordinated with phase 2. Work inside a phase is
parallel. Phase 4 items slot in
anywhere. Feature work on the reconstruction-based lighting should stop
except for blocking bugs: it is replaced in phase 2.

## Phase 0: verification throughput

Today a headless run renders with llvmpipe and parallel agents collide on
Xvfb displays ("Cannot open X11 display") and CPU (test timeouts at load 20+).

- Headless GPU rendering: SDL3 offscreen video with EGL on the NVIDIA device
  (`EGL_EXT_device_enumeration`/platform device), falling back to llvmpipe.
  Path tracing and 4x targets become fast enough to test as played.
- One test runner (`pc/tests/run.py`) that allocates a private display or
  EGL device per test, runs suites in parallel safely, and prints one table.
- Golden-frame checks: fixed RPC camera poses (`editor_camera`) in a few rooms,
  compared with tolerance, so renderer refactors are caught without a human.
- The soak test (`pc/tests/soak.py`) as a gate: N thousand vsyncs from New
  Game and from a copied save, no findings.

Gate: the full suite runs in a few minutes, several in parallel, without
flakes.

## Phase 1: 64-bit with a native data model

What breaks under LP64, measured on today's tree:

- 302 pointer members in `src`/`include` headers. Where the struct overlays
  PS1 data (the original image, disc files, scratchpad), every field after a
  pointer shifts.
- 53 allocations/clears with literal sizes (`vs_main_bzero(&x, 0xC4)`) and
  216 places doing byte-offset arithmetic on pointers (`(u8*)p + 0x34`):
  correct for PS1 layouts only.
- The ~940 data imports read the original image directly; tables in it hold
  4-byte PS1 addresses where C now expects 8-byte pointers.
- `unsigned long` is 8 bytes on LP64 (e.g. the SDK's `GsCOORDINATE2.flg`).
- Port conventions: packed OT links (`ptr & 0xffffff`), native code below
  16 MB, function tables of 32-bit entries (`function_tables.c`).

Approach:

1. **Audit tool** (`pc/tools/lp64_audit.py`, pycparser or libclang over the
   preprocessed sources): list every struct with pointer or `long` members,
   where each is instantiated (static, heap, scratchpad, file data, image
   import), literal sizes and offsets that disagree with `sizeof`/`offsetof`,
   and each data import's type. This is the work list; agents split it by
   overlay.
2. **Classify each pointer-bearing struct:**
   - *Runtime state* (built by game code): let it grow to native layout. Fix
     literal sizes and offsets with `sizeof`/`offsetof` under `ROOD_PC`.
   - *Overlaid on PS1 data* (image, disc formats, scratchpad): keep the PS1
     layout. A PC-only header gives the field a 32-bit guest-address type
     (`psx_ptr(T)`, 4 bytes) with accessors; guest memory stays mapped low,
     so a zero-extended guest address is a valid host pointer. Matched source
     stays byte-identical where the header retype is enough.
   - `_Static_assert` every load-bearing size and offset, so a layout
     regression fails the build instead of corrupting memory.
3. **Data imports:** generate natively-typed globals at build time from the
   user's local image (never committed), widening pointer fields through
   the audit's type information; or, for PS1-layout data, keep reading the
   image through `psx_ptr`. Function tables become native pointer arrays.
4. **Drawing lists:** OT links become indices (or 64-bit pointers in a
   PC-side list) under `ROOD_PC`; the 16 MB code placement goes away.
5. **Build:** `build_boot.py` gains `--arch 64` and builds both while the
   64-bit build catches up; tests run against both; 32-bit is dropped once
   64-bit passes everything.

Gate: 64-bit passes the phase 0 suite, two 40,000-vsync soaks (New Game,
copied save) and a playtest pass through the rooms played so far; compiler
pointer-truncation warnings are errors.

Parallel split: one agent on the audit tool, then one per overlay group
(main, BATTLE, MENU, TITLE/ending) on the classification fixes, one on
imports and the build.

Progress (2026-10-09): groundwork done. `build_boot.py --arch 64` measures
the conversion (`pc/build/boot64/report.txt`), `pc/tools/lp64_audit.py`
writes the work list (`pc/docs/LP64_Audit.md`, `pc/build/lp64_audit/audit.json`)
and `pc/src/psx_types.h` holds `psx_ptr(T)` and the layout asserts. Surprise:
most layout changes come from the SDK's `u_long` (packets, OT, VECTOR), so a
32-bit `u_long` under `ROOD_PC` goes first; the BATTLE code's many alias
views over actor/state blocks favour keeping PS1 layouts there.

Optional check after `build_boot.py --arch 64` (seconds, <1 GB):
`pc/tools/memcap python3 pc/tools/decl_mismatch.py [--review]` compares every
declaration each 64-bit object was compiled with (its DWARF) against the
definition the linker binds it to, and exits 1 on a wrong-size pointer/integer
parameter, return, out-parameter pointee or data import (the class of the
`func_800AD494` stack smash). See `LP64_Audit.md`, "Cross-unit declarations".

## Phase 2: native 3D submission

Replace "game builds PS1 packets, renderer reconstructs 3D" with "game code
submits meshes, transforms and materials; the renderer draws them". 2D UI,
the HUD, menus and movies keep the packet path.

1. **Render API** (`pc/src/render.h`): register immutable meshes once
   (positions, normals, UVs, vertex colours, texture page/CLUT, blend mode)
   and submit instances per frame with a world transform, depth-sort hints
   and material state. Vertex colours keep the PS1's baked lighting as an
   input; lighting is computed from real positions and normals.
2. **Migration order**, each behind a legacy/native switch with submission
   counters (coverage instrumentation):
   - room geometry (`room_geometry.c`, port code already) and room objects;
   - characters (`actor_vertices.c`, `actor_packets.c`) with bone palettes;
   - weapons and shields (`weapon_packets.c`), crates and props;
   - shadows (`actor_shadow.c`), effects and particles (`effect_particles.c`,
     `pc/src/effects`), translucent and additive surfaces in PS1 order.
3. **Retire the reconstruction**: the GTE projection log, room depth
   records, caster capture from packets and G-buffer guessing go away.
   Frame interpolation interpolates transforms instead of replaying packets.
   The free camera becomes a renderer camera; game culling no longer limits it.

Gate: coverage counters show no legacy 3D submissions in the tested rooms;
golden frames match the legacy path within tolerance; soak and playtest pass.

Parallel split: API plus room first (one agent); then characters, props and
effects in parallel.

## Phase 3: Vulkan renderer with hardware ray tracing

- Vulkan for the whole frame (no GL interop): rasterize the native meshes,
  trace with `VK_KHR_ray_query` against a BLAS per mesh and a TLAS per frame
  (room static, characters refit), shadows and indirect light from real
  geometry with texture alpha tested in any-hit.
- Denoising: a temporal/spatial denoiser (lighting
  signature, strict history); NVIDIA NRD RELAX for the indirect light,
  fetched at build time (its license rules out vendoring).
- Keep a GL raster fallback for machines without ray tracing until it is
  clearly unneeded.
- Haze, AA, post-processing and the editor views move over.

Gate: frame time and image quality at least match the current GL path in
the golden-frame rooms; the editor works on the Vulkan path.

## Phase 4: data-first tooling (ongoing)

Done: light actors (data-first JSON prefabs in pc/assets/prefabs and
pc/user/prefabs, instances with overrides in the room files, deterministic
flicker, glow links), one-click lighting of room fixtures with shadow
exclusion of the fixture, and a UE-style Place Actors panel (README);
per-room lights, glows and settings files; the user settings file;
live reload of both; RPC control (`editor_camera`, `graphics`,
`present_dump` bursts); the UE-style editor; seeing into rooms from the
free camera (the game's cutaway walls for its yaw, black wall shells left
out, a cutaway to the focus, per-group visibility in the Outliner;
Native_Rendering.md).

Next:

- Shader hot reload: load shaders from `pc/shaders/` when present (embedded
  copies as fallback), watch and recompile on save.
- Asset overrides directory (textures, models) layered over the disc image,
  never committed.
- Editor parity over RPC for scripting.
- Material and lighting authoring persisted per room, with the editor as
  the primary way to change data.

## Tracking

Each phase gets an issue list from its audit or coverage counters, worked
by parallel agents in worktrees, merged into `main` in phase order after the
gate passes. Notes on progress and surprises go in this file.

## Progress

**Phase 0 (2026-10-09): done; gate met.** `pc/tests/run.py` runs 17 suites
(CTest, RPC/checkpoint smokes, X11 window smoke, the six
`start_button_smoke.sh` input cases, golden frames, soaks from New Game and
from a copied save) in about 2.5-3 minutes at `-j 4` on EGL (NVIDIA). Before:
the same smokes run serially under Xvfb/llvmpipe took about 17 minutes
(`start_button_smoke.sh` alone 856 s) and could not run in parallel. Two
runners at once (`--out`) both pass in about 4 minutes; six consecutive full
runs had no failures. Xvfb mode (`--headless xvfb`) passes with longer
timeouts (about 10 minutes, CPU-heavy).

Surprises:

- SDL3's offscreen driver already enumerates EGL devices
  (`SDL_HINT_EGL_DEVICE`); the 32-bit NVIDIA driver gives a 4.6 core
  context there with no X server. Device 1 (NVIDIA through Mesa) fails,
  2 is the Intel GPU, 3 llvmpipe.
- With rendering on the GPU the tests are CPU bound: the software VRAM
  rasteriser (`triangle`/`polygon_command`) still runs every frame, about
  200 vsyncs/s. Phase 2 removes most of it; until then the long input cases
  (60-125 s) and soak (~150 s, mostly the 13k-vsync opening) set the
  wall time.
- The game wrote `pc/build/boot/loading.ppm` every frame and read lights from
  `pc/lights`, so parallel runs collided and user edits changed results:
  `ROOD_OUT` and `ROOD_LIGHTS` redirect them.
- Golden frames are bit-exact run to run only when New Game is pressed on a
  fixed phase of the vsync count (the game draws every 2-4 vsyncs; the other
  phase shifts every later frame by a sub-pixel camera step).
- Each renderer (NVIDIA, Intel, llvmpipe) gets its own reference set in
  `pc/build/golden/<gpu>/`.

**Phase 1 groundwork (2026-10-09): done.** `pc/tools/lp64_audit.py`,
`build_boot.py --arch 64` (measurement only) and `pc/src/psx_types.h`;
findings and the task list in `LP64_Audit.md`.

**Phase 1 execution order.**

1. Foundation, sequential (started 2026-10-09 evening): `u_long` 32-bit
   under the port, `psx_types.h` available to all game code, SDK layout
   asserts.
2. `psx_ptr_codemod.py` (started 2026-10-09 evening, in parallel with 1):
   rewrites access sites of PS1-layout pointer fields from the audit, keeping
   the PS1 and 32-bit builds identical.
3. In parallel, one agent each, after 1 and 2 merge: main (sound driver,
   heap, CD queue, scratchpad struct); BATTLE actors and AI; BATTLE world
   and UI; effects; MENU and TITLE/ending. Each re-runs the audit and
   `run.py`, merges after its group's `--arch 64` errors reach zero.
4. Last: link the 64-bit build, convert the data imports, ordering-table
   links to indices, drop the 16 MB code placement; then run the phase 1
   gate on 64-bit and retire the 32-bit build.

**Phase 1, 2026-10-09 evening.** Merged: the foundation (32-bit SDK `long`
on LP64 via `pc/include` wrappers, `psx_types.h` force-included, 71 SDK
layout asserts) and `pc/tools/psx_ptr_codemod.py` (sample conversion in
main). Audit after: 141 structs change layout (was 252), 161 moving byte
offsets (was 660). Lesson: a JSON-AST tool bug grew to 38 GB and the OOM
kill took the desktop app down; heavy tools run under `pc/tools/memcap`.
A pilot converts the main group end to end and writes the recipe for the
parallel group tasks.

**Phase 2, step 1 (2026-10-10): render API and native room.** `pc/src/render.h`
(meshes, per-frame lists with the GTE camera, a marker packet that places a
list in the ordering table) and `room_native.c` (one mesh per room geometry
block, built once per room; an instance per visible group). On by default
(`ROOD_NATIVE_ROOM`, RPC `graphics native_room`, editor checkbox; mode 1
keeps the room packets for the CPU's reference VRAM, mode 2 drops the opaque
ones). Golden frames match the packet path to a mean difference of 0.00-0.05
(depth view 0.50: exact per-pixel depth, and areas the never-cleared screen
left stale now read as far); the full suite passes in modes 1 (default) and 0, golden frames in 2; see
`Native_Rendering.md`. Surprises:

- Sub-unit seams between groups showed at 4x until view-space positions were
  snapped to whole units as the GTE produces them (the packet path's
  "precise" positions use the GTE's integer IR1/IR2 too).
- Back faces must be tested on whole-pixel screen positions: edge-on slivers
  with NCLIP 0 are kept by the PS1 and fill seams.
- The room's near bucket is animated by the game (1..64); the far bucket is
  0x7ff and UV halving starts at 512-1024 buckets in the rooms tested.
- Frame time (first room, ROOD_PERF, loaded machine): legacy ~5.4 ms of
  work per tick, mode 1 the same, mode 2 ~3.8 ms (no room GTE work or CPU
  rasterization). Mode 2 waits on StoreImage readers (immediate text boxes)
  reading the hardware target.

**Phase 2, step 2 (2026-10-10): native characters.** `pc/src/actor_native.c`
turns every model drawn by `func_800B3CF0`/`func_800B4594` (characters,
their weapons and shields, the props of `BATTLE/49268.c`) into a render.h
mesh with a palette index per vertex, drawn per frame from the bone matrices
the game computed (world-space palettes plus the previous frame's, blended
by frame interpolation), with exact depth against the room and other models,
the PS1's polygon order within a model, and real caster geometry for PTGI.
On by default (`ROOD_NATIVE_ACTORS`, RPC `graphics native_actors`, editor
checkbox; packets kept for the CPU's reference VRAM). Golden frames and the
full suite pass in both modes; the first room matches the packet path to a
mean difference of 0.04 (game camera) and 0.07-0.10 (free camera on the
characters); frame time is unchanged (~6-7 ms of work per tick in the first
room either way, loaded machine). See `Native_Rendering.md`. Surprises:

- Per-pixel depth inside a model changed how characters look: the PS1
  paints a model in ordering-table order, and the models rely on it (hair
  and coat shells intersect; some actors are drawn twice per frame, a dark
  and a light copy a couple of units apart whose polygons interleave in the
  table). The native path computes each polygon's bucket from the game's
  own projected depths and paints in that order, joining both copies into
  one list.
- Characters and weapons use the same packet builder (`func_800B3CF0`
  lives in `weapon_packets.c`), so weapons came along.

**Phase 2, step 3 (2026-10-10): translucent draws and room blocks.**
Semi-transparent polygons are native: the room's (stained glass, window
light, the save point's column), characters' translucent records, and the
blended builders `func_800B516C`/`func_800B5C30` (material groups with
alternate CLUTs, whole models faded with `B-F`/`B+F`). A list keeps a
translucent sequence; five-word translucent markers in the ordering table
draw ranges of it exactly where the PS1 draws those polygons (the room: one
per polygon right after its packet; models: one per bucket), with the PS1
blend modes, opaque texels as surfaces and STP texels blended into colour
only, hidden behind the native surfaces before them. Other blocks drawn
through `func_8009723C` (`func_8008E19C`'s section 9 floor quads) are
layered room-rules lists. Switches `ROOD_NATIVE_BLEND` / `ROOD_NATIVE_OBJECTS`
(RPC `graphics native_blend` / `native_objects`, editor checkboxes,
settings keys), both on by default; `render_stats` counts translucent
markers and polygons, translucent packet triangles drawn or skipped,
layered lists, and the 3D packet triangles that are still reconstructed
(`legacy_other_triangles`: drop shadows and effects, 72 in the first room).
Golden frames and the full suite pass with both switches on and off; the
comparisons are in `Native_Rendering.md`. Next: drop shadows and particles
on the same translucent path, then retiring the reconstruction (plan in
`Native_Rendering.md`).
**Phase 1, task 7 (2026-10-10, WIP).** OT links kept as 24-bit guest addresses
(all packets live in guest RAM); every `--arch 64` unit compiles; the 64-bit
link and the pointer-typed data imports (native regions from DWARF types) are
in progress. Details in `LP64_Audit.md`, Progress, task 7.

**Phase 1, task 7 (2026-10-10): 64-bit links and passes.** `run.py --arch 64`
17/17 (golden frames, short soaks); soak:save at 20,000 vsyncs passes; the New
Game long soak finds the same soft-freeze as 32 bits plus a game-over the
harness does not handle. OT links stay 24-bit guest addresses; pointer imports
are native regions generated from DWARF types; the game runs on a stack below
2 GB. The 32-bit build remains the default. Details: `LP64_Audit.md`.

**Phase 2, step 4 (2026-10-10): drop shadows, particles, retiring the
reconstruction. Phase 2 done; gate met in the tested rooms.** Measured what
was still reconstructed first (`ROOD_OTHER_DEBUG=1` attributes each 3D packet
triangle to the code that issued its GTE projections): over a 20,000-vsync
soak 99% drop shadows, then `func_800D6E44` particles, then 19 triangles of
other effects. Both are now native through effects lists (translucent
entries only, drawn at markers where the packets are linked): drop shadows
as world-space triangles of a cached disc mesh with the actor's transform,
particles as screen-aligned sprites at their world centres
(`RENDER_POLY_SPRITE`, transient meshes); additive ones are PTGI emitters.
Room and characters default to mode 2 (no reference packets: the CPU
rasterizes nothing native draws cover; native VRAM is copied back from the
hardware target where the game reads it), and the reconstruction (GTE
projection log and lookups, room depth records, packet casters, nearest-
triangle matching) is off whenever every native switch is on
(`pc_reconstruct`, kept for one release). The live free camera no longer
takes the game camera's ordering-table limits. `legacy_other_triangles` is 0
in the first room and the save-card soak; golden frames match at 0.00; the
full suite passes 17/17 with the defaults (32- and 64-bit), with step 3's
renderer and with every native switch off. Frame time in the first room
roughly halves (about 10 ms to 5 ms per game frame, loaded machine). Left
on packets: the `src/EFFECT` renderers (screen-space geometry from projected
control points, untested: no spell could be cast over RPC), particles on
fixed layers, 2D UI, menus and movies. See `Native_Rendering.md`.

Surprises:

- With the projection log off, room packets became "2D" to the hardware
  path, which only checked 3D room packets against the native list: they
  drew over it. Coverage now ignores the 2D/3D classification.
- The game copies two pixels from the screen's corner every frame
  (DR_MOVE); syncing VRAM back for it stalled the GPU every frame. Copies
  from stale areas now make their destination stale instead.
- Drop shadows were invisible in every golden pose: the Auto drop-shadow
  mode turns them off or makes them faint when traced shadows exist; the
  comparisons force the original ones.

**Phase 3, stages 1-3 first pass (2026-10-10): Vulkan renderer.**
`ROOD_RENDERER=vulkan` on 64 bits (`run.py --renderer vulkan`): the whole
frame on Vulkan (packet path, native lists, translucent/effect markers,
replay, present passes, ImGui overlay, headless offscreen for tests) and the
path tracer on ray queries (room BLAS, per-frame casters BLAS, TLAS; texel
cut-outs tested per candidate). Golden frames match the GL references
(0.00-0.05 mean), the suite passes 17/17, GI matches GL (0.05) at less than
half GL's frame work (no CPU BVH). GL stays the default. Shaders compile with
the vendored glslang. See `Vulkan_Renderer.md` (architecture, status, next).

**Phase 3, ray-traced lighting (2026-10-10).** Acceleration structures per
mesh (room static, split into a forced-opaque BLAS and texel cut-outs;
characters and props skinned on the GPU from their bone palettes, one BLAS
per instance, refit), motion from movers' previous poses, SVGF-style
variance-guided denoising per signal (indirect, visibility, baked
visibility, glow light) with a short history where moving blockers touch
the visibility, a froxel haze volume applied to every 3D pixel (no holes
around effects), shadow maps on Vulkan devices without ray tracing, the GI
probe on Vulkan (legacy separate editor renderer retired there). Moving
shadows lose the smeared outlines; GL vs Vulkan lighting 0.9-1.8 mean.
GPU time: NVIDIA ~0.7 ms per frame; Intel iGPU 70-75 -> 16.5 ms (GI 1.5 by
default on integrated GPUs). NRD was not vendored: its license is not
compatible with this CC0 repository. Details and numbers in
`Vulkan_Renderer.md`.

**Ultra lighting: NVIDIA NRD (2026-10-11).** RELAX diffuse denoises the
path-traced indirect light (following NVIDIA's RELAX sample usage: world normals, view
depth, world motion, unfiltered irradiance; textures multiplied back
afterwards), default on Vulkan when built with it; our filter keeps the
visibility signals and stays selectable (`gi_denoiser` nrd/filter/raw).
NRD is fetched pinned at build time (`pc/tools/nrd.py`, never committed;
`--nrd off` builds without it). Steadier indirect light when still, no
blotches or stale silhouettes while moving, +0.24 ms on NVIDIA. See
`Vulkan_Renderer.md`, "NRD".
