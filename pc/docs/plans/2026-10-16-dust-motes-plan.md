# Dust motes and a small particle system Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Dust motes that glint where light falls through the air and vanish in shadow: tiny square pixel motes (1-2 game pixels, nearest, like the PS1's texels) drifting slowly with a little turbulence, built as the first use of a small, reusable, data-driven particle system. Emitters are lighting-layer items (`emitter <id>`, saved per room like fog volumes), edited through the schema in Details (value ladder, reset to the asset's value), selectable and movable in the editor; their look comes from emitter assets (`pc/assets/emitters/<name>.json`); a graphics setting **Dust** (Off, Low, High) scales them, and Off is the old picture, pixel for pixel.

**Architecture:** A mote is a closed-form function of its emitter's packed record, its index and the time (`pc/src/particle_math.h`, mirrored in `pc/shaders/vulkan/particles_sim.glsl`): a seeded hash picks its lifetime, phase and spawn point (box, room, point, disc, the floor under a box), then drift, gravity with drag, curl-noise turbulence, fade in/out and its pixel size follow analytically, so the simulation needs no state, is the same at any frame rate, stops with the game and is deterministic for tests. Each present the motes are evaluated, lit (in light only: their strongest lights and the sun, each with a shadow ray; or lit: plus an ambient term) and splatted into a **key image** of one texel per game pixel (`depth << 16 | mote`, the nearest wins) and a **colour image** (one texel per mote). Vulkan does all of it in one compute pass (`vk_particles.inc`, `particles.comp` / `particles_rt.comp` with ray queries against this frame's acceleration structure); OpenGL does it on the CPU (`particles.cpp`, shadow rays through `ptgi.c`'s BVH, a ray budget per frame) and uploads the two textures. The present shaders (`scaled.frag`, `gl_present.c`) add the colour of the mote that owns the pixel's game pixel when it is in front of the scene (`particles.glsl`, shared through `gl_shaders.py`), so motes are exactly square, never filtered, and depth-tested. Emitters live in the room state next to the fog volumes (`rood_env` reset/apply/add_items), so saving, layers, undo, copy/paste and Promote work as for fog volumes.

**Tech Stack:** C++17 (game side), C99 header (`particle_math.h`), GLSL 450/460 (Vulkan, glslang via `pc/tools/vk_shaders.py`) and GLSL 3.30 (OpenGL present, `pc/tools/gl_shaders.py`), Vulkan compute + `VK_KHR_ray_query`, Dear ImGui + ImGuizmo (vendored), the project's JSON (`prefab::Json`), dev RPC (`pc/src/dev_rpc.c`), Python 3 headless tests (`pc/tests/run.py`, `rood_rpc.py`, numpy + PIL), CTest probes (`pc/CMakeLists.txt`).

---

## Ground rules for every task

- The main checkout (`$MAIN`: Task 1 finds it with `git rev-parse --git-common-dir`) is read-only: never build, test or write there (nor in the verify checkout beside it). All work happens in the worktree of Task 1.
- No changes under `src/` (the decompilation). Everything here is `pc/`, `lighting/` and docs.
- Look and feel are data, never constants: every new parameter is an emitter field (asset + lighting item + schema), a settings key (project settings, Details > Atmosphere, RPC) or both. New effects are off: the settings key `dust` defaults to 0, and with 0 nothing is evaluated, bound or drawn.
- The PS1 look stays: motes are whole game pixels, composited by texel fetch (nearest), never filtered.
- Heavy work always runs under `flock "$ROOD_HEAVY_LOCK"` (`$ROOD_HEAVY_LOCK` is the lock file shared by every agent on this machine; set it in the shell, never write its path into a committed file), tests at `-j 2`. No step creates artificial CPU load (no stress tools, busy loops or extra parallel builds), not even to reproduce a flake; tests wait with `game.run(n)` (vsyncs). Every wait is bounded; kill by PID only; leave no background processes.
- Tests use the pinned card `pc/build/test-card1.mcd` (or temp copies); they never read or write `pc/save`, `pc/user` or `pc/lights` (scratch `ROOD_LIGHTS`, `ROOD_SETTINGS` under the test's output directory).
- Never commit game data or game-derived assets (frames stay in `pc/build/tests/`). Committed files never mention other game ports by name and never contain absolute home-directory or temp-directory paths.
- Commit after every task; messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Do not merge into main, do not push.
- Windows: `windows.h` is force-included on MinGW (`pc/src/win_compat.h`), so no new identifier may be a Windows macro or typedef (`near`, `far`, `small`, `interface`, `DELETE`, `FLOAT`, `IN`, `OUT`). New enums are prefixed (`PC_SPAWN_*`); probes use `close_to`, not `near`.
- The modern menus work is on hold; this plan may touch `editor_schema.cpp`'s `kind_of` (one `case`) and registers its kind with `rood_schema::add_kind` (the menu light's mechanism), nothing else in the registry.

## Decisions taken where the brief leaves room

1. **Stateless closed-form simulation.** A mote's position is a pure function of (emitter record, index, time): its cycle `k = floor((t + phase) / life)` reseeds its spawn, `age` runs 0..life. Drift is the terminal velocity, gravity (y down: positive falls, negative rises: buoyancy) accumulates under drag to `g / drag`, turbulence is a curl-noise displacement `amplitude x curl(psi)(p / scale, t x speed)` (divergence-free: swirls, no clumping). It runs the same on the CPU and the GPU, at any frame rate, stops when the game pauses (game seconds: vsyncs / 59.94, as the fog), and RPC `particles {time}` freezes it for tests. Later fog emitters can feed the fog sim with the same positions and their analytic velocities.
2. **Time:** seconds since entering the room (as `atmosphere.cpp medium_time`), not wrapped.
3. **Lighting "in light only":** per mote the `particles_lights` (default 2, 1..4) strongest sources by unshadowed light at the mote (lights from `pc_lights_list`, glows from `pc_fog_glows` x `particles_glow`), each with one shadow ray to its centre (noise-free: no temporal filter needed, no shimmer), plus the sun with one ray along its direction. Brightness = that light x the emitter's colour x brightness x fade x per-mote variation. **"Lit"** (the item field `lit` = 1) adds the emitter's `ambient` on top, so the motes show everywhere. Rays honour each light's `shadows` flag; glows are traced while `particles_glow_shadows` is 1 (default), whatever their own category says (the air behind a wall is dark).
4. **Which tracer:** Vulkan with ray queries and this frame's acceleration structure (the path tracer ran): ray queries (`particles_rt.comp`, `rt_common.glsl trace`). OpenGL with the path tracer's scene built: `pc_gi_trace_cpu` on the CPU, at most `particles_cpu_rays` rays a frame (default 1536), each mote refreshed round-robin (2048 motes, 3 rays each: every 4 frames). Without a tracer (lighting quality without ray-traced GI or shadows, `ROOD_GI=0`): the motes are lit unshadowed (documented; RPC `particles` reports `path: "unshadowed"`). The haze's froxel light is not used: it is scaled by the medium's density (zero without haze) and only exists with haze High.
5. **Compositing:** additive (a mote reflects light: it brightens, never darkens; a mote in shadow adds nothing and shows nowhere), in linear light, before the fog volumes and the haze (they attenuate it as the surface behind it), only in the lit view mode. Dark motes (no light) never take a pixel, so they cannot hide a lit mote behind them.
6. **Key image:** one R32_UINT texel per game pixel of the display area, `min(SZ, 65535) << 16 | mote index`, cleared to `0xFFFFFFFF`; `imageAtomicMin` (Vulkan) or a CPU min (OpenGL) keeps the nearest mote. A pixel shows its mote when the mote's SZ is less than the scene's depth there (the far plane always loses). At most `PC_PARTICLES_MAX` = 16384 motes per room (the 16-bit index; 64 rows of 256 colours); at most 16 emitters.
7. **Pixel size:** each mote is `pixel_min..pixel_max` whole game pixels (fields, default 1..2, at most 4), anchored at `floor(s - (size - 1) / 2)` of its projected display position, the same size near and far (pixel motes).
8. **Spawn shapes:** `box` (in its turned box), `room` (in the room's bounds, packed as a box on the CPU), `point` (in a sphere of `radius`; 0 a point), `disc` (a horizontal disc of `radius`), `surface` (on the floor under each point of its box's footprint: the room's floor grid, `pc_scene_floor_grid`, as the ground fog; the box's bottom face without one). Walls as surfaces come later.
9. **Behaviour modules, no node graph:** an asset's `modules` object lists `drift` {velocity}, `curl` {amplitude, scale, speed}, `gravity` {acceleration}, `drag` {rate}, `fade` {in, out}; an absent module is neutral (0). In the lighting file they are flat fields (`drift`, `curl`, `curl_scale`, `curl_speed`, `gravity`, `drag`, `fade_in`, `fade_out`), so any instance overrides any of them and Details shows each with its reset arrow.
10. **Assets are presets:** `asset=<id>` names `pc/assets/emitters/<id>.json` (`ROOD_EMITTERS` overrides the directory); every field an item does not write comes from it, the editor writes only what differs from it (`lighting::preset_field("emitter", key)`, the fog presets' mechanism), and the reset arrow restores the asset's value. Asset files carry `kind: "emitter"`, `name`, `category`, `tags`, `icon`, `description` so the library (another branch) lists them by kind; `rood_particles::assets()` and `rood_particles::add_emitter(asset, pos)` are its entry points. No library is built here.
11. **Renderer field:** `render` has one value now, `pixel`; `soft` and `streak` are named in the docs as later renderers (the lint rejects them until they exist).
12. **Lighting mode as a bool** `lit` (0: in light only, the default; 1: lit), because the schema's enums match lighting text by name and a bool reads plainly in Details ("Lit everywhere").
13. **Dust setting:** settings key `dust` 0 (Off, default), 1 (Low: each emitter's count x `dust_low`, default 0.4), 2 (High: the full count). An asset names the setting that governs it (`quality: "dust"`; the only one so far). Graphics page row "Dust", Details > Graphics > Display and rendering combo, Details > Atmosphere's Dust section, RPC `graphics {dust}` and `particles {level}`. Default Off as the project's rule for new effects; the user turns it on (the summary says so).
14. **The editor:** a new selection kind `KIND_EMITTER` (`emitter:<id>`). Emitters are picked by their icon (a small pickable dot at the centre, as lights), not by a ray against their volume (a dust box fills a room: a ray would make the room unclickable). They get outlines (Show > Emitters), an Outliner section, Details through the schema (Basics, Spawn, Motion, Look, Transform), the gizmo (W move, E yaw, R size through `rood_transform::scale_volume`), hide, copy/paste/duplicate, delete, F, and "Add emitter" (with an asset combo) in the Atmosphere tab.
15. **Not in the separate editor view (F2):** motes draw in the game's presented picture on both renderers; the F2 view (`editor_view.cpp`) does not draw them (documented).
16. **Example:** dust motes in the town house (zone 34 room 0) as an authored item (`lighting/default/rooms/zone34_room00.txt`), placed in front of its lit windows by a measured procedure (Task 15), kept only if subtle: at Dust High at most 2 % of the game view's pixels change and the change's mean is under 1.5 / 255; otherwise the item goes into `lighting/README.md` as documented example data instead.
17. **Cost:** Vulkan times the pass as perf section `particles` (RPC `perf`, `ROOD_PERF=1`); OpenGL reports the CPU path's milliseconds (RPC `particles` `cpu_ms`). Budgets checked by the test: Vulkan under 0.3 ms for 4096 motes, OpenGL CPU under 3 ms.

## File structure

Created:

| File | Responsibility |
|---|---|
| `pc/src/particle_math.h` | Plain C, header-only: the emitter record (`pc_emitter_gpu`), the frame record (`pc_particles_gpu`), hash, curl noise, a mote's closed form (`pc_particle_mote`), the floor grid lookup, projection, key packing and splat anchors. The CPU path and `pc/tests/particles_probe.cpp` use it; `particles_sim.glsl` mirrors it. |
| `pc/src/particle_assets.h`, `.cpp` | Emitter assets: `pc/assets/emitters/*.json` (`ROOD_EMITTERS`) read into lighting fields (modules flattened), the list for the library, the asset fields (`ASSET_FIELDS`). No game dependencies (probe-tested). |
| `pc/src/particles.h` | C API: the settings (`pc_dust`, ...), the CPU frame for OpenGL, the GPU frame for Vulkan, the RPC. |
| `pc/src/particles_internal.h` | C++ editor-facing API (`rood_particles`): the room's emitters as lighting items, by index, for the scene, schema, clipboard, outliner and viewport. |
| `pc/src/particles.cpp` | The room's emitters (items in and out, assets resolved), the frame gather (lights, glows, sun, camera, floor grid, emitter records), the CPU path (evaluate, light, splat; ray budget), stats, RPC `particles`. No Dear ImGui. |
| `pc/src/particles_editor.cpp` | The emitter's schema kind (registered with `add_kind`), Details tools, the Outliner rows, the Atmosphere tab's Dust section, the viewport (outlines, icons, gizmo). |
| `pc/src/vk_particles.inc` | The Vulkan pass (included at the end of `vk_rt.inc`): images, pipelines, the dispatch, binding for the present, readback for the RPC. |
| `pc/shaders/vulkan/particles_sim.glsl` | The mote's closed form (GLSL copy of `particle_math.h`). |
| `pc/shaders/vulkan/particles_main.glsl` | The pass: per mote evaluate, light (`PARTICLES_RT`: ray queries), colour, splat. |
| `pc/shaders/vulkan/particles.comp`, `particles_rt.comp` | The two pipelines (without / with ray queries). |
| `pc/shaders/vulkan/particles.glsl` | The composite (`particles_at`), shared by `scaled.frag` and OpenGL (`gl_shaders.py` `PC_GLSL_PARTICLES`). |
| `pc/assets/emitters/dust_motes.json`, `README.md` | The dust motes asset; the asset format. |
| `pc/tests/particles_probe.cpp` | CTest probe of `particle_math.h`, `particle_assets.cpp` and the C light shape. |
| `pc/tests/particles_smoke.py` | Headless test: items (round trip through the lighting file), motes (deterministic, count, time), picture (Dust Off identical; lit motes appear, shadowed ones do not; square, nearest, 1-2 game pixels), editor (select, move, reset, delete, duplicate), cost (perf), example (zone 34 room 0). |
| `pc/docs/Particles.md` | The particle system: emitters, assets, modules, renderers, lighting, settings, RPC, cost, limits, later uses. |

Modified:

| File | Change |
|---|---|
| `pc/src/lighting_layers.cpp`, `pc/tools/lighting_format.py`, `pc/tools/lighting_lint.py` | The `emitter` kind (fields, asset fields as preset fields, override diffs); the lint's checks. |
| `pc/src/light_shape.h` | `pc_light_shape`, the C copy of `light_shape()` (the CPU path). |
| `pc/src/environment.cpp`, `pc/src/lighting_editor.cpp` | Emitters in the room state (reset, apply, add_items; the item kinds lists). |
| `pc/src/ptgi.c` | `pc_gi_scene_ready` (the OpenGL tracer's scene exists). |
| `pc/src/gl_present.c` | The CPU frame, its two textures, the composite in the present shader. |
| `pc/src/vk_rt.inc`, `pc/src/vk_present.c`, `pc/shaders/vulkan/scaled.frag` | The Vulkan pass included; bindings 21, 22 and `particles_` in the present UBO; the composite. |
| `pc/tools/gl_shaders.py` | `particles.glsl` as a fragment (`PC_GLSL_PARTICLES`). |
| `pc/src/project_settings.cpp` | Keys `dust`, `dust_low`, `particles_glow`, `particles_lights`, `particles_glow_shadows`, `particles_cpu_rays`. |
| `pc/src/dev_rpc.c` | Method `particles` (an undoable edit); `graphics {dust}`. |
| `pc/src/editor_selection.h`, `.cpp`, `pc/tests/selection_probe.cpp` | `KIND_EMITTER` (`emitter:`). |
| `pc/src/editor_scene.h`, `.cpp`, `pc/src/editor_clipboard.cpp`, `pc/src/editor_schema.cpp` | Emitters as scene objects (refs, labels, positions, hiding, delete), copy/paste, `kind_of`. |
| `pc/src/lights.cpp` | The particles viewport called after the fog volumes'; R on an emitter (`scale_object`); `editor_transform` accepts emitters. |
| `pc/src/dev_overlay.cpp` | Outliner section, Show > Emitters (both menus), Graphics combo "Dust". |
| `pc/src/ui/ui_settings.cpp`, `pc/tests/ui_settings_probe.cpp`, `pc/tests/ui_settings_stubs.cpp` | The Graphics page's "Dust" row (16 -> 17 rows). |
| `pc/tests/editor_schema_smoke.py` | `emitter` among the expected kinds. |
| `pc/tools/build_boot.py`, `pc/CMakeLists.txt`, `pc/tests/run.py` | New sources, the probe, the smoke (a checkpoint suite). |
| `lighting/README.md`, `pc/README.md`, `pc/docs/Vulkan_Renderer.md` | The `emitter` item kind and its fields; settings, RPC, the renderer's pass. |
| `lighting/default/rooms/zone34_room00.txt` | The example (only if subtle, Task 15). |

---

### Task 1: Worktree and baseline

**Files:** none changed.

- [ ] **Step 1: Branch in a worktree, copy the data (never symlink)**

Use superpowers:using-git-worktrees. From MAIN:

```bash
git worktree add .claude/worktrees/dust-motes -b claude/dust-motes main
cd .claude/worktrees/dust-motes
MAIN=$(git rev-parse --path-format=absolute --git-common-dir)/..
cp -a "$MAIN/build" build && cp -a "$MAIN/data" data && cp -a "$MAIN/disks" disks
mkdir -p pc/build && cp "$MAIN/pc/build/test-card1.mcd" pc/build/
cp -a "$MAIN/pc/build/nrd" pc/build/   # the 64-bit build links NVIDIA NRD
```

Expected: a worktree on `claude/dust-motes` with `build/`, `data/`, `disks/`, `pc/build/test-card1.mcd`, `pc/build/nrd/`.

- [ ] **Step 2: Configure CTest**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null
```

Expected: `pc/build/build.ninja` exists.

- [ ] **Step 3: Build the three binaries the tests use**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
```

Expected: each exits 0 with a `Built ...` line and no `error:` line.

- [ ] **Step 4: Record the baseline**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest lighting_lint editor atmosphere fog glow emissive ui_system
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan editor atmosphere fog glow emissive
```

Expected: tables ending in `N/N passed` (or a known failure list). Write the failing suites down in the task notes; later tasks must not add failures.

---

### Task 2: The mote's closed form (`particle_math.h`)

**Files:**
- Create: `pc/src/particle_math.h`, `pc/tests/particles_probe.cpp`
- Modify: `pc/CMakeLists.txt` (after the `editor_tools_probe` block), `pc/src/light_shape.h` (after `pc_barn_doors`)

- [ ] **Step 1: Write the failing probe**

Create `pc/tests/particles_probe.cpp`:

```cpp
// The particle system without the game (pc/docs/Particles.md): the mote's
// closed form (pc/src/particle_math.h: spawn shapes, drift, gravity and
// drag, curl-noise turbulence, fade, pixel size, cycles), projection and the
// key image's packing, the CPU light shape (light_shape.h pc_light_shape),
// and later the emitter assets (particle_assets.cpp).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "light_shape.h"
#include "particle_math.h"

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool close_to(float a, float b, float e = 1e-3f) { return fabsf(a - b) <= e; }

/* A box emitter at (100, -50, 200), 200 x 100 x 300, no motion, 10 s lives. */
static pc_emitter_gpu still_box()
{
    pc_emitter_gpu e;
    memset(&e, 0, sizeof e);
    e.v[0][0] = 100, e.v[0][1] = -50, e.v[0][2] = 200, e.v[0][3] = 0;
    e.v[1][0] = 200, e.v[1][1] = 100, e.v[1][2] = 300, e.v[1][3] = PC_SPAWN_BOX;
    e.v[4][0] = 10, e.v[4][1] = 0.5f;
    e.v[5][0] = e.v[5][1] = e.v[5][2] = 1, e.v[5][3] = 1;
    e.v[6][0] = 1, e.v[6][1] = 2, e.v[6][2] = 0.5f;
    e.v[7][0] = 7, e.v[7][3] = 64;
    return e;
}

static void spawn_and_cycles()
{
    pc_emitter_gpu e = still_box();
    pc_mote a, b;
    bool inside = true, sizes = true, same = true;
    int ones = 0;
    for (int i = 0; i < 64; i++) {
        pc_particle_mote(&e, i, 3.25f, nullptr, nullptr, 0, &a);
        pc_particle_mote(&e, i, 3.25f, nullptr, nullptr, 0, &b);
        same &= !memcmp(&a, &b, sizeof a);
        inside &= fabsf(a.pos[0] - 100) <= 100.01f && fabsf(a.pos[1] + 50) <= 50.01f && fabsf(a.pos[2] - 200) <= 150.01f;
        sizes &= a.size >= 1 && a.size <= 2 && a.age >= 0 && a.age < a.life && a.life <= 10 && a.life >= 5;
        ones += a.size == 1;
    }
    check(same, "a mote is a pure function of (emitter, index, time)");
    check(inside, "a still box emitter's motes are in its box");
    check(sizes && ones > 10 && ones < 54, "sizes 1..2 game pixels (both), ages within lives of 5..10 s");
    pc_particle_mote(&e, 5, 3.25f, nullptr, nullptr, 0, &a);
    pc_particle_mote(&e, 5, 3.25f + a.life, nullptr, nullptr, 0, &b);
    check(b.cycle == a.cycle + 1 && close_to(b.age, a.age, 1e-2f) && fabsf(b.pos[0] - a.pos[0]) + fabsf(b.pos[2] - a.pos[2]) > 1e-3f,
        "one life later: the next cycle, the same age, a new spawn point");
    pc_emitter_gpu turned = e;
    turned.v[0][3] = 3.14159265f * 0.5f; /* yaw 90: the box's x runs along -z */
    turned.v[1][0] = 200, turned.v[1][2] = 2; /* a thin slab along its x */
    bool along_z = true;
    for (int i = 0; i < 32; i++) {
        pc_particle_mote(&turned, i, 1, nullptr, nullptr, 0, &a);
        along_z &= fabsf(a.pos[0] - 100) <= 1.01f && fabsf(a.pos[2] - 200) <= 100.01f;
    }
    check(along_z, "yaw turns the box (atmosphere.cpp's corner(): x' = c x + s z, z' = -s x + c z)");
    pc_emitter_gpu disc = e;
    disc.v[1][3] = PC_SPAWN_DISC, disc.v[1][0] = 50;
    pc_emitter_gpu point = e;
    point.v[1][3] = PC_SPAWN_POINT, point.v[1][0] = 30;
    bool on_disc = true, in_ball = true;
    for (int i = 0; i < 64; i++) {
        pc_particle_mote(&disc, i, 1, nullptr, nullptr, 0, &a);
        on_disc &= close_to(a.pos[1], -50) && hypotf(a.pos[0] - 100, a.pos[2] - 200) <= 50.01f;
        pc_particle_mote(&point, i, 1, nullptr, nullptr, 0, &b);
        in_ball &= sqrtf((b.pos[0] - 100) * (b.pos[0] - 100) + (b.pos[1] + 50) * (b.pos[1] + 50)
                       + (b.pos[2] - 200) * (b.pos[2] - 200)) <= 30.01f;
    }
    check(on_disc, "a disc: a horizontal circle of its radius");
    check(in_ball, "a point: a ball of its radius");
    /* The floor under a surface emitter: a 2 x 2 grid sloping in x (y down: the floor at -10 .. -30). */
    float heights[4] = { -10, -30, -10, -30 }, info[4] = { 0, 100, 1.0f / 200, 1.0f / 200 };
    pc_emitter_gpu surface = e;
    surface.v[1][3] = PC_SPAWN_SURFACE;
    bool on_floor = true;
    for (int i = 0; i < 32; i++) {
        pc_particle_mote(&surface, i, 1, heights, info, 2, &a);
        float expect = pc_particle_floor(heights, info, 2, a.pos[0], a.pos[2]) - 1;
        on_floor &= close_to(a.pos[1], expect, 1e-3f) && a.pos[1] <= -11 + 1e-3f && a.pos[1] >= -31 - 1e-3f;
    }
    check(on_floor, "a surface emitter spawns just above the floor grid under it");
    pc_particle_mote(&surface, 3, 1, nullptr, nullptr, 0, &a);
    check(close_to(a.pos[1], -50 + 50 - 1), "without a floor grid: its box's bottom face (y down)");
}

/* The first mote of e at least 2 s old at t0 whose cycle is the same at t1 (no respawn between). */
static int steady(const pc_emitter_gpu& e, float t0, float t1)
{
    for (int i = 0; i < 64; i++) {
        pc_mote a, b;
        pc_particle_mote(&e, i, t0, nullptr, nullptr, 0, &a);
        pc_particle_mote(&e, i, t1, nullptr, nullptr, 0, &b);
        if (a.cycle == b.cycle && a.age >= 2)
            return i;
    }
    return 0;
}

static void motion()
{
    pc_emitter_gpu e = still_box();
    e.v[4][0] = 20, e.v[4][1] = 0; /* 20 s lives (small ages: float precision) */
    e.v[2][0] = 3, e.v[2][1] = -1, e.v[2][2] = 2; /* drift */
    pc_mote a, b;
    int i = steady(e, 10, 12);
    pc_particle_mote(&e, i, 10, nullptr, nullptr, 0, &a);
    pc_particle_mote(&e, i, 12, nullptr, nullptr, 0, &b);
    check(b.cycle == a.cycle && close_to(b.pos[0] - a.pos[0], 6) && close_to(b.pos[1] - a.pos[1], -2)
            && close_to(b.pos[2] - a.pos[2], 4),
        "drift: its velocity times the time");
    pc_emitter_gpu fall = still_box();
    fall.v[4][0] = 20, fall.v[4][1] = 0, fall.v[2][3] = 2; /* gravity 2 u/s^2, no drag */
    i = steady(fall, 1, 4);
    pc_particle_mote(&fall, i, 1, nullptr, nullptr, 0, &a);
    pc_particle_mote(&fall, i, 4, nullptr, nullptr, 0, &b);
    check(close_to(b.pos[1] - a.pos[1], b.age * b.age - a.age * a.age, 1e-2f),
        "gravity without drag: 1/2 g age^2 (y down: it falls)");
    fall.v[4][0] = 1000, fall.v[3][3] = 4; /* drag: the fall's speed tends to g / drag */
    i = steady(fall, 600, 601);
    pc_mote c, d;
    pc_particle_mote(&fall, i, 600, nullptr, nullptr, 0, &c);
    pc_particle_mote(&fall, i, 601, nullptr, nullptr, 0, &d);
    check(c.age >= 2 && close_to(d.pos[1] - c.pos[1], 0.5f, 1e-2f), "with drag the fall settles at g / drag units a second");
    pc_emitter_gpu fade = still_box();
    fade.v[4][1] = 0, fade.v[4][2] = 2, fade.v[4][3] = 3;
    bool shaped = true;
    for (int k = 0; k < 64; k++) {
        pc_particle_mote(&fade, k, 7.5f, nullptr, nullptr, 0, &a);
        float in = fminf(a.age / 2, 1), out = fminf((a.life - a.age) / 3, 1);
        float expect = in * in * (3 - 2 * in) * out * out * (3 - 2 * out);
        shaped &= close_to(a.fade, expect, 1e-4f);
    }
    check(shaped, "fade in over fade_in, out over fade_out (smoothstep), 1 between");
}

static void curl()
{
    float q[3] = { 0.3f, -1.2f, 2.5f }, c[3];
    double sum = 0, div_max = 0;
    for (int i = 0; i < 200; i++) {
        q[0] = 0.37f * i, q[1] = -0.21f * i + 1, q[2] = 0.11f * i - 3;
        pc_particle_curl(q, 0.5f, c);
        sum += sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        const float h = 1e-2f;
        float div = 0;
        for (int k = 0; k < 3; k++) {
            float a[3] = { q[0], q[1], q[2] }, b[3] = { q[0], q[1], q[2] }, ca[3], cb[3];
            a[k] += h, b[k] -= h;
            pc_particle_curl(a, 0.5f, ca);
            pc_particle_curl(b, 0.5f, cb);
            div += (ca[k] - cb[k]) / (2 * h);
        }
        div_max = fmax(div_max, fabs(div));
    }
    check(div_max < 2e-2, "curl noise is divergence-free (no clumping)");
    check(sum / 200 > 0.3 && sum / 200 < 1.5, "curl noise has about unit strength (the amplitude is in world units)");
    float c0[3], c1[3];
    pc_particle_curl(q, 0, c0);
    pc_particle_curl(q, 0.01f, c1);
    check(fabsf(c0[0] - c1[0]) + fabsf(c0[1] - c1[1]) + fabsf(c0[2] - c1[2]) < 0.05f, "it evolves smoothly in time");
}

static void projection_and_keys()
{
    /* Camera at the origin looking along +z (the GTE's: view = R p + T), H 256, offset (160, 120). */
    float rot[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } }, proj[4] = { 256, 256, 160, 120 };
    float p[3] = { 50, -25, 512 }, s[3];
    check(pc_particle_project(rot, proj, p, s) && close_to(s[0], 185) && close_to(s[1], 107.5f) && close_to(s[2], 512),
        "projection: offset + H x view / z, SZ = view z");
    float behind[3] = { 0, 0, 8 };
    check(!pc_particle_project(rot, proj, behind, s), "nearer than PC_PARTICLE_NEAR: not drawn");
    check(pc_particle_key(512.7f, 9) == ((512u << 16) | 9u) && pc_particle_key(1e6f, 3) == ((65535u << 16) | 3u)
            && pc_particle_key(1000, 1) < pc_particle_key(1001, 0),
        "keys: the nearer mote is the smaller key");
    check(pc_particle_anchor(10.9f, 1) == 10 && pc_particle_anchor(10.9f, 2) == 10 && pc_particle_anchor(10.2f, 2) == 9,
        "a 2-pixel mote covers the two game pixels nearest its centre");
}

static void light_shape()
{
    /* A spot at the origin aiming +y (down), cone 60 (cos 30 = 0.866), inner cos 0.95. */
    float centre[3] = { 0, 0, 0 }, dir[4] = { 0, 1, 0, 0.866f }, right[4] = { 0.95f, 0, 0, 0 }, up[4] = { 0, 0, 0, 0 };
    float below[3] = { 0, 100, 0 }, l[3] = { 0, -1, 0 };
    check(close_to(pc_light_shape(1, centre, dir, right, up, below, l), 1), "inside a spot's inner cone: 1");
    float side[3] = { 100, 10, 0 }, ls[3] = { -0.995f, -0.0995f, 0 };
    check(close_to(pc_light_shape(1, centre, dir, right, up, side, ls), 0), "outside its cone: 0");
    check(close_to(pc_light_shape(0, centre, dir, right, up, side, ls), 1), "a point light: 1");
}

int main(int argc, char** argv)
{
    (void)argc, (void)argv;
    spawn_and_cycles();
    motion();
    curl();
    projection_and_keys();
    light_shape();
    printf("%s\n", failures ? "FAIL" : "PASS: particles");
    return failures ? 1 : 0;
}
```

In `pc/CMakeLists.txt`, after `add_test(NAME editor_tools_probe COMMAND editor_tools_probe)`, add:

```cmake
# The particle system (pc/src/particle_math.h, particle_assets.cpp) without the game.
add_executable(particles_probe tests/particles_probe.cpp)
target_include_directories(particles_probe PRIVATE src)
target_compile_features(particles_probe PRIVATE cxx_std_17)
target_compile_options(particles_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME particles_probe COMMAND particles_probe ${CMAKE_CURRENT_SOURCE_DIR}/assets/emitters)
```

- [ ] **Step 2: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null && ninja -C pc/build particles_probe
```

Expected: `fatal error: particle_math.h: No such file or directory`.

- [ ] **Step 3: The C light shape**

In `pc/src/light_shape.h`, after `pc_barn_doors` (before the `PC_LIGHT_SHAPE_GLSL` comment), add:

```c
/* light_shape() (below) in C, for the particles' CPU path (particles.cpp):
 * the factor on a light's falloff at p, l the unit direction toward it. */
static inline float pc_light_shape(float type, const float centre[3], const float dir[4], const float right[4],
    const float up[4], const float p[3], const float l[3])
{
    float c = -(l[0] * dir[0] + l[1] * dir[1] + l[2] * dir[2]);
    if (type > 1.5f)
        return fmaxf(c, 0.0f) * pc_barn_doors(centre, right, up, dir, dir[3], right[3], p);
    if (type > 0.5f) {
        float x = fminf(fmaxf((c - dir[3]) / fmaxf(right[0] - dir[3], 1e-6f), 0.0f), 1.0f);
        return x * x * (3 - 2 * x);
    }
    return 1;
}
```

- [ ] **Step 4: Write the header**

Create `pc/src/particle_math.h`:

```c
/* The port's particle system (pc/docs/Particles.md; not the game's effect
 * particles): a mote as a closed-form function of its emitter's record, its
 * index and the time, so the simulation keeps no state, runs the same at any
 * frame rate, on the CPU (OpenGL: particles.cpp) and the GPU (Vulkan:
 * pc/shaders/vulkan/particles_sim.glsl, its GLSL copy: keep the two the
 * same), stops with the game and is deterministic for tests.
 *
 * Each mote i of an emitter lives `life` seconds (lifetime, less up to
 * `jitter` of it, per mote), offset by a random phase; its cycle k =
 * floor((t + phase) / life) reseeds its spawn point; its age runs 0..life.
 * From the spawn point it drifts (the drift velocity is its terminal
 * velocity), falls under gravity (y down; negative rises) slowed by drag to
 * g / drag, and the curl of a smooth vector field (divergence-free: swirls
 * without clumping) displaces it by `curl` world units, the field evolving
 * with the time. It fades in and out (smoothstep over fade_in, fade_out),
 * shines `gain` (1 - variation x a random) and is `size` whole game pixels.
 *
 * World space, y down. Plain C (pc/tests/particles_probe.cpp). */
#ifndef ROOD_PARTICLE_MATH_H
#define ROOD_PARTICLE_MATH_H
#include <math.h>
#include <stdint.h>

#define PC_PARTICLES_MAX 16384 /* motes per room: the key's low 16 bits; 64 rows of 256 colours */
#define PC_PARTICLE_EMITTERS 16 /* emitters per room the renderers take */
#define PC_PARTICLE_COLOUR_W 256 /* the colour image's width (its height PC_PARTICLES_MAX / 256) */
#define PC_PARTICLE_GLOWS 32 /* glows a mote may be lit by */
#define PC_PARTICLE_FLOOR_N 24 /* scene_capture.h PC_SCENE_FLOOR_GRID */
#define PC_PARTICLE_NEAR 16.0f /* nearer than this (view z): not drawn */
#define PC_PARTICLE_NONE 0xffffffffu /* a key image texel without a mote */

/* Spawn shapes (the item field `shape`; `room` is packed as a box). */
enum { PC_SPAWN_BOX = 0, PC_SPAWN_POINT = 1, PC_SPAWN_DISC = 2, PC_SPAWN_SURFACE = 3 };

/* One emitter as the renderers read it (particles_sim.glsl PARTICLE_EMITTER(e, k)):
 *  0 pos xyz, yaw (radians)
 *  1 size xyz (full extents; point and disc: x the radius), shape (PC_SPAWN_*)
 *  2 drift xyz (u/s), gravity (u/s^2, y down)
 *  3 curl amplitude (u), curl scale (u), curl speed (1/s), drag (1/s)
 *  4 lifetime (s), lifetime jitter (0..0.95), fade in (s), fade out (s)
 *  5 colour rgb, brightness
 *  6 pixel size min, max (1..4), variation (0..1), lit (0 in light only, 1 lit)
 *  7 seed (an integer below 2^24), ambient, first mote (index into the frame's), motes */
typedef struct {
    float v[8][4];
} pc_emitter_gpu;

/* The frame's motes for the Vulkan pass (particles_main.glsl `Particles`, std140). */
typedef struct {
    int info[4]; /* motes, emitters, display w, display h */
    int info2[4]; /* lights, glows, lights per mote (1..4), shadow rays (1: traced) */
    float time[4]; /* t (s), glow scale, debug (1: every mote white, unlit), glow shadows (1) */
    float projection[4]; /* hx (widescreen-narrowed), hy, ofx, ofy */
    float rotation[3][4]; /* the GTE camera: rows of R, w the translation (view = R p + T) */
    float env[5][4]; /* the room's sun and sky (environment.h pc_env_gpu) */
    float floor_info[4]; /* the floor grid: corner x, z, 1 / cell x, 1 / cell z */
    float floor_n[4]; /* cells per side (0: none) */
    float floor[PC_PARTICLE_FLOOR_N * PC_PARTICLE_FLOOR_N / 4][4]; /* heights, cell i + j n */
    float glow_pos[PC_PARTICLE_GLOWS][4]; /* xyz, radius of influence */
    float glow_col[PC_PARTICLE_GLOWS][4]; /* rgb, emitting radius */
    pc_emitter_gpu emitters[PC_PARTICLE_EMITTERS];
} pc_particles_gpu;

/* A mote now. */
typedef struct {
    float pos[3];
    float age, life, fade, gain;
    int size; /* game pixels */
    int cycle;
} pc_mote;

static inline uint32_t pc_particle_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
/* The next uniform number in 0..1 of a hash stream. */
static inline float pc_particle_unit(uint32_t* s)
{
    *s = pc_particle_hash(*s + 0x9e3779b9u);
    return (float)(*s >> 8) * (1.0f / 16777216.0f);
}
static inline float pc_particle_smooth(float x)
{
    x = fminf(fmaxf(x, 0.0f), 1.0f);
    return x * x * (3 - 2 * x);
}

/* The floor grid's height (world y) under (x, z): bilinear between cell
 * centres, clamped at its edges (medium.glsl medium_floor). */
static inline float pc_particle_floor(const float* heights, const float info[4], int n, float x, float z)
{
    float cx = (x - info[0]) * info[2] - 0.5f, cz = (z - info[1]) * info[3] - 0.5f;
    int i = (int)floorf(cx), j = (int)floorf(cz);
    float fx = cx - (float)i, fz = cz - (float)j;
    int i0 = i < 0 ? 0 : i >= n ? n - 1 : i, i1 = i + 1 < 0 ? 0 : i + 1 >= n ? n - 1 : i + 1;
    int j0 = j < 0 ? 0 : j >= n ? n - 1 : j, j1 = j + 1 < 0 ? 0 : j + 1 >= n ? n - 1 : j + 1;
    float a = heights[j0 * n + i0] + (heights[j0 * n + i1] - heights[j0 * n + i0]) * fx;
    float b = heights[j1 * n + i0] + (heights[j1 * n + i1] - heights[j1 * n + i0]) * fx;
    return a + (b - a) * fz;
}

/* The curl of psi, a sum of six plane waves (two per component; wave vector,
 * angular speed, phase): smooth, divergence-free, about unit strength. */
static const float PC_CURL_WAVES[6][5] = {
    { 0.83f, 1.31f, -0.47f, 0.9f, 0.0f }, { -1.62f, 0.38f, 1.17f, 1.3f, 1.3f },
    { 1.12f, -0.71f, 0.95f, 0.7f, 2.1f }, { 0.41f, 1.77f, -1.08f, 1.1f, 0.7f },
    { -0.92f, 0.56f, 1.43f, 0.8f, 4.2f }, { 1.36f, -1.21f, -0.33f, 1.5f, 3.3f },
};
static inline void pc_particle_curl(const float q[3], float tau, float out[3])
{
    float g[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }; /* the gradient of each component of psi */
    for (int j = 0; j < 6; j++) {
        const float* k = PC_CURL_WAVES[j];
        float w = ((j & 1) ? 0.5f : 1.0f) * cosf(k[0] * q[0] + k[1] * q[1] + k[2] * q[2] + tau * k[3] + k[4]);
        g[j >> 1][0] += w * k[0], g[j >> 1][1] += w * k[1], g[j >> 1][2] += w * k[2];
    }
    out[0] = 0.5f * (g[2][1] - g[1][2]);
    out[1] = 0.5f * (g[0][2] - g[2][0]);
    out[2] = 0.5f * (g[1][0] - g[0][1]);
}

/* Mote i of emitter e at time t (seconds, >= 0). `heights` (n x n, info:
 * pc_particles_gpu floor_info) is the room's floor grid for surface
 * emitters (NULL: none). Returns 1. */
static inline int pc_particle_mote(const pc_emitter_gpu* e, int i, float t, const float* heights, const float info[4], int n,
    pc_mote* m)
{
    const float(*v)[4] = e->v;
    uint32_t s = pc_particle_hash((uint32_t)v[7][0] * 0x9e3779b1u ^ (uint32_t)i * 0x85ebca77u);
    float jitter = fminf(fmaxf(v[4][1], 0.0f), 0.95f);
    float life = fmaxf(v[4][0], 0.05f) * (1.0f - jitter * pc_particle_unit(&s));
    float tt = fmaxf(t, 0.0f) + pc_particle_unit(&s) * life;
    float k = floorf(tt / life);
    float age = fminf(fmaxf(tt - k * life, 0.0f), life);
    uint32_t c = pc_particle_hash(s ^ ((uint32_t)(int32_t)k * 0xc2b2ae35u));
    float u0 = pc_particle_unit(&c), u1 = pc_particle_unit(&c), u2 = pc_particle_unit(&c);
    int shape = (int)v[1][3];
    float p[3];
    if (shape == PC_SPAWN_POINT) {
        float z = 1 - 2 * u0, r = sqrtf(fmaxf(1 - z * z, 0.0f)), a = 6.2831853f * u1, rr = v[1][0] * cbrtf(u2);
        p[0] = v[0][0] + rr * r * cosf(a), p[1] = v[0][1] + rr * z, p[2] = v[0][2] + rr * r * sinf(a);
    } else if (shape == PC_SPAWN_DISC) {
        float rr = v[1][0] * sqrtf(u0), a = 6.2831853f * u1;
        p[0] = v[0][0] + rr * cosf(a), p[1] = v[0][1], p[2] = v[0][2] + rr * sinf(a);
    } else { /* a box (the room's bounds come packed as one), or its footprint on the floor */
        float lx = (u0 - 0.5f) * v[1][0], ly = (u1 - 0.5f) * v[1][1], lz = (u2 - 0.5f) * v[1][2];
        float cs = cosf(v[0][3]), sn = sinf(v[0][3]);
        p[0] = v[0][0] + cs * lx + sn * lz, p[1] = v[0][1] + ly, p[2] = v[0][2] - sn * lx + cs * lz;
        if (shape == PC_SPAWN_SURFACE)
            p[1] = (heights && n > 0 ? pc_particle_floor(heights, info, n, p[0], p[2]) : v[0][1] + v[1][1] * 0.5f) - 1.0f;
    }
    float g = v[2][3], drag = v[3][3];
    float fall = drag > 1e-4f ? g * (age / drag - (1.0f - expf(-drag * age)) / (drag * drag)) : 0.5f * g * age * age;
    for (int a = 0; a < 3; a++)
        p[a] += v[2][a] * age;
    p[1] += fall;
    if (v[3][0] > 0) {
        float sc = fmaxf(v[3][1], 1.0f), q[3] = { p[0] / sc, p[1] / sc, p[2] / sc }, d[3];
        pc_particle_curl(q, t * v[3][2], d);
        for (int a = 0; a < 3; a++)
            p[a] += v[3][0] * d[a];
    }
    for (int a = 0; a < 3; a++)
        m->pos[a] = p[a];
    m->age = age, m->life = life, m->cycle = (int)k;
    m->fade = (v[4][2] > 0 ? pc_particle_smooth(age / v[4][2]) : 1.0f) * (v[4][3] > 0 ? pc_particle_smooth((life - age) / v[4][3]) : 1.0f);
    m->gain = 1.0f - fminf(fmaxf(v[6][2], 0.0f), 1.0f) * pc_particle_unit(&c);
    int lo = (int)v[6][0] < 1 ? 1 : (int)v[6][0] > 4 ? 4 : (int)v[6][0];
    int hi = (int)v[6][1] < lo ? lo : (int)v[6][1] > 4 ? 4 : (int)v[6][1];
    int size = lo + (int)(pc_particle_unit(&c) * (float)(hi - lo + 1));
    m->size = size > hi ? hi : size;
    return 1;
}

/* A world point in the display (the GTE camera: view = R p + T; display =
 * offset + H view.xy / view.z): out x, y (game pixels), z (SZ). 0 when
 * nearer than PC_PARTICLE_NEAR. */
static inline int pc_particle_project(const float rot[3][4], const float proj[4], const float p[3], float out[3])
{
    float vz = rot[2][0] * p[0] + rot[2][1] * p[1] + rot[2][2] * p[2] + rot[2][3];
    if (!(vz >= PC_PARTICLE_NEAR))
        return 0;
    float vx = rot[0][0] * p[0] + rot[0][1] * p[1] + rot[0][2] * p[2] + rot[0][3];
    float vy = rot[1][0] * p[0] + rot[1][1] * p[1] + rot[1][2] * p[2] + rot[1][3];
    out[0] = proj[2] + proj[0] * vx / vz, out[1] = proj[3] + proj[1] * vy / vz, out[2] = vz;
    return 1;
}
/* A key image texel: the nearer mote is the smaller key. */
static inline uint32_t pc_particle_key(float sz, int index)
{
    uint32_t z = sz >= 65535.0f ? 65535u : sz <= 1.0f ? 1u : (uint32_t)sz;
    return z << 16 | ((uint32_t)index & 0xffffu);
}
/* The first game pixel a mote of `size` pixels covers along one axis. */
static inline int pc_particle_anchor(float s, int size) { return (int)floorf(s - 0.5f * (float)(size - 1)); }
#endif
```

- [ ] **Step 5: Run it and see it pass**

```bash
ninja -C pc/build particles_probe && pc/build/particles_probe
```

Expected: every line `ok:`, last line `PASS: particles`. (The motion checks take a mote that does not respawn between their two times and keep ages small, so float rounding stays far under the tolerances; a failure there is the formula, not the tolerance.)

- [ ] **Step 6: Commit**

```bash
git add pc/src/particle_math.h pc/src/light_shape.h pc/tests/particles_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Particles: a mote's closed form (spawn shapes, drift, gravity and drag, curl noise, fade, pixel size), projection and keys; C light shape (particles_probe)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: The `emitter` item kind in the lighting files

**Files:**
- Modify: `pc/src/lighting_layers.cpp` (`kinds()` after the `"actor"` entry; `preset_field`; `canonicalize`; `diff_one`'s kind list), `pc/tools/lighting_format.py` (`SCHEMA`, `REQUIRED`, `COUNTS`, constants, `canonicalize`, `diff_one`), `pc/tools/lighting_lint.py` (`check_values`)
- Test: `pc/tests/lighting_probe.cpp` (`selftest`), `pc/tests/lighting_layers_test.py` (the lint's bad file)

- [ ] **Step 1: Write the failing checks**

In `pc/tests/lighting_probe.cpp` `selftest()`, after the check `"a file round-trips (quotes too)"`, add:

```cpp
    /* An emitter (pc/src/particles.cpp; lighting/README.md "Emitters"): with an
     * asset it keeps the asset fields it writes even at the schema's
     * defaults (they differ from its asset's), without one they go. */
    File ef;
    check(parse_text("rood-lighting 2\nemitter 68ea4c1200a3f2 asset=dust_motes pos=10,-20,30 count=400 drift=1,0,0.5\n", ef)
            && ef.entries.size() == 1 && ef.entries[0].item.kind == "emitter" && ef.entries[0].item.get("count")
            && *ef.entries[0].item.get("drift") == "1,0,0.5",
        "an emitter parses; with its asset, a field at the default stays (count=400)");
    Entry ee;
    ee.item = ef.entries[0].item;
    ee.id = ee.item.id();
    check(format_entry(ee) == "emitter 68ea4c1200a3f2 asset=dust_motes pos=10,-20,30 count=400 drift=1,0,0.5",
        "and formats back the same (the schema's field order)");
    Item bare;
    bare.kind = "emitter", bare.name = "68ea4c1200a3f3";
    bare.set("pos", "1,2,3"), bare.set("count", "400"), bare.set("yaw", "0"), bare.set("lit", "1");
    canonicalize(bare);
    check(!bare.get("count") && !bare.get("yaw") && bare.get("pos") && bare.get("lit") && preset_field("emitter", "curl")
            && !preset_field("emitter", "pos") && known_kind("emitter"),
        "without an asset its defaults are left out; asset fields are preset fields, its own are not");
```

In `pc/tests/lighting_layers_test.py`, in the lint's `bad` file (the `write(bad / "default/rooms/zone09_room17.txt", ...)` text), add after `light  68ea4c1200a3f4   pos=1.00,2,3`:

```
emitter 68ea4c1200a3f5 asset=dust_motes pos=1,2,3 shape=cube render=streak
```

and to `expect` add `"shape must be one of box, room, point, disc, surface"` and `"render must be pixel"`.

- [ ] **Step 2: Run them and see them fail**

```bash
ninja -C pc/build lighting_probe && ctest --test-dir pc/build -R lighting_layers --output-on-failure
```

Expected: `FAIL: an emitter parses; ...` (an unknown kind) and the lint's two new expectations missing; `0% tests passed`.

- [ ] **Step 3: The C++ schema**

In `pc/src/lighting_layers.cpp` `kinds()`, after the `"actor"` entry (the list is sorted by kind), add:

```cpp
        /* Particle emitters ("emitter <id>"): particles.cpp (pc/docs/Particles.md). Its own
         * fields, then the ones its asset (pc/assets/emitters/<asset>.json) gives unless written. */
        { "emitter",
            { { "name", -1, nullptr, false }, { "asset", -1, nullptr, false }, { "pos", 1, nullptr, false },
                { "yaw", 2, "0", false }, { "enabled", 0, "1", false }, { "seed", 0, "1", false },
                /* spawn */
                { "shape", -1, "box", false }, { "size", 1, "256,192,256", false }, { "radius", 1, "64", false },
                { "count", 0, "400", false }, { "lifetime", 2, "10", false }, { "lifetime_jitter", 2, "0.5", false },
                /* behaviour modules (0: off) */
                { "drift", 2, "0,0,0", false }, { "curl", 2, "0", false }, { "curl_scale", 1, "64", false },
                { "curl_speed", 3, "0.1", false }, { "gravity", 3, "0", false }, { "drag", 3, "0", false },
                { "fade_in", 2, "0", false }, { "fade_out", 2, "0", false },
                /* the renderer, its lighting, the graphics setting that scales it */
                { "render", -1, "pixel", false }, { "pixel_min", 0, "1", false }, { "pixel_max", 0, "2", false },
                { "color", 3, "1,1,1", false }, { "brightness", 3, "1", false }, { "variation", 2, "0.5", false },
                { "lit", 0, "0", false }, { "ambient", 4, "0", false }, { "quality", -1, "dust", false } },
            false, 4 },
```

Replace `preset_field`'s body's first line `if (kind != "fog") return false;` with:

```cpp
    if (kind == "emitter") /* everything after its own fields comes from its asset */
        return field_rank("emitter", key) >= field_rank("emitter", "shape") && field_spec("emitter", key);
    if (kind != "fog")
        return false;
```

In `canonicalize`, change `bool preset = item.kind == "fog" && item.get("preset");` to:

```cpp
    bool preset = (item.kind == "fog" && item.get("preset")) || (item.kind == "emitter" && item.get("asset"));
```

and the comment above it to `/* A fog volume with a preset (an emitter with an asset) keeps the values it writes even when they are the schema's defaults: they differ from its preset's. */`.

In `diff_one`, change `if (x.kind != "light" && x.kind != "actor" && x.kind != "env" && x.kind != "fog" && x.kind != "post") {` to:

```cpp
    if (x.kind != "light" && x.kind != "actor" && x.kind != "env" && x.kind != "fog" && x.kind != "emitter"
        && x.kind != "post") {
```

- [ ] **Step 4: The Python schema and the lint**

In `pc/tools/lighting_format.py` `SCHEMA`, after the `"actor"` entry, add the same kind:

```python
    # Particle emitters ("emitter <id>"): pc/src/particles.cpp. Its own fields, then those its
    # asset (pc/assets/emitters/<asset>.json) gives unless written.
    "emitter": (False, 4, [
        ("name", -1, None, False), ("asset", -1, None, False), ("pos", 1, None, False),
        ("yaw", 2, "0", False), ("enabled", 0, "1", False), ("seed", 0, "1", False),
        ("shape", -1, "box", False), ("size", 1, "256,192,256", False), ("radius", 1, "64", False),
        ("count", 0, "400", False), ("lifetime", 2, "10", False), ("lifetime_jitter", 2, "0.5", False),
        ("drift", 2, "0,0,0", False), ("curl", 2, "0", False), ("curl_scale", 1, "64", False),
        ("curl_speed", 3, "0.1", False), ("gravity", 3, "0", False), ("drag", 3, "0", False),
        ("fade_in", 2, "0", False), ("fade_out", 2, "0", False),
        ("render", -1, "pixel", False), ("pixel_min", 0, "1", False), ("pixel_max", 0, "2", False),
        ("color", 3, "1,1,1", False), ("brightness", 3, "1", False), ("variation", 2, "0.5", False),
        ("lit", 0, "0", False), ("ambient", 4, "0", False), ("quality", -1, "dust", False)]),
```

`REQUIRED`: add `"emitter": ["pos"]`. `COUNTS`: add `"drift": 3`. After `FOG_SHAPES = ("box", "sphere")` add:

```python
EMITTER_SHAPES = ("box", "room", "point", "disc", "surface")
EMITTER_RENDERS = ("pixel",)  # soft sprites and streaks come later (pc/docs/Particles.md)
EMITTER_QUALITIES = ("dust",)
# The fields an emitter's asset gives (everything after its own: pc/src/lighting_layers.cpp preset_field).
EMITTER_ASSET_FIELDS = tuple(f[0] for f in SCHEMA["emitter"][2][6:])
```

In `canonicalize`, change `preset = kind == "fog" and any(key == "preset" for key, _ in fields)` to:

```python
    preset = (kind == "fog" and any(key == "preset" for key, _ in fields)) or \
        (kind == "emitter" and any(key == "asset" for key, _ in fields))
```

and the `if` below it to `and not (preset and key in (PRESET_FIELDS if kind == "fog" else EMITTER_ASSET_FIELDS)):`. In `diff_one`, add `"emitter"` to the tuple `("light", "actor", "env", "fog", "post")`.

In `pc/tools/lighting_lint.py`: after `fog_presets()` add

```python
_EMITTER_ASSETS = None


def emitter_assets():
    """The emitter assets' ids (pc/assets/emitters/*.json)."""
    global _EMITTER_ASSETS
    if _EMITTER_ASSETS is None:
        _EMITTER_ASSETS = {p.stem for p in (ROOT / "pc/assets/emitters").glob("*.json")}
    return _EMITTER_ASSETS
```

and in `check_values`, after the `if kind == "fog" and key == "preset":` block, add:

```python
        if kind == "emitter" and key == "shape":
            if value not in lf.EMITTER_SHAPES:
                report("error", path, e.line, f"{e.id}: shape must be one of {', '.join(lf.EMITTER_SHAPES)}")
            continue
        if kind == "emitter" and key == "render":
            if value not in lf.EMITTER_RENDERS:
                report("error", path, e.line, f"{e.id}: render must be {' or '.join(lf.EMITTER_RENDERS)}")
            continue
        if kind == "emitter" and key == "quality":
            if value not in lf.EMITTER_QUALITIES:
                report("error", path, e.line, f"{e.id}: quality must be one of {', '.join(lf.EMITTER_QUALITIES)}")
            continue
        if kind == "emitter" and key == "asset":
            if value not in emitter_assets():
                report("warning", path, e.line, f"{e.id}: asset '{value}' is not in pc/assets/emitters (its fields as written)")
            continue
```

and in the `count` cases change `elif key == "size" and kind == "fog":` to `elif key == "size" and kind in ("fog", "emitter"):`.

- [ ] **Step 5: Run them and see them pass**

```bash
ninja -C pc/build lighting_probe && ctest --test-dir pc/build -R 'lighting_layers' --output-on-failure
python3 pc/tools/lighting_lint.py
```

Expected: `100% tests passed` (the C++ and Python schemas match); the lint over the repository's `lighting/` exits 0.

- [ ] **Step 6: Commit**

```bash
git add pc/src/lighting_layers.cpp pc/tools/lighting_format.py pc/tools/lighting_lint.py pc/tests/lighting_probe.cpp pc/tests/lighting_layers_test.py
git commit -m "$(cat <<'EOF'
Lighting files: the emitter item kind (its own fields, asset fields as preset fields, override diffs); lint checks shape, render, quality, asset

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Emitter assets (`pc/assets/emitters`, `particle_assets.cpp`)

**Files:**
- Create: `pc/src/particle_assets.h`, `pc/src/particle_assets.cpp`, `pc/assets/emitters/dust_motes.json`, `pc/assets/emitters/README.md`
- Modify: `pc/tests/particles_probe.cpp` (a section; its call in `main`), `pc/CMakeLists.txt` (the probe's sources)

- [ ] **Step 1: Write the failing checks**

In `pc/tests/particles_probe.cpp`, add `#include <string>`, `#include "light_prefabs.h"` and `#include "particle_assets.h"` to the includes, and before `int main` add:

```cpp
/* Emitter assets (particle_assets.h): modules flattened into lighting fields. */
static void assets(const char* dir)
{
    if (dir)
        setenv("ROOD_EMITTERS", dir, 1);
    rood_particles::reload_assets();
    const rood_particles::Asset* dust = rood_particles::find_asset("dust_motes");
    check(dust && dust->name == "Dust motes" && dust->category == "Atmosphere" && !dust->tags.empty(),
        "dust_motes.json: its name, category and tags (the library's)");
    auto has = [&](const char* key, const char* value) {
        const std::string* v = dust ? dust->get(key) : nullptr;
        return v && *v == value;
    };
    check(dust && has("shape", "box") && has("count", "900") && has("drift", "1.2,0.4,0.8") && has("curl", "9")
            && has("curl_scale", "56") && has("fade_in", "2.5") && has("lit", "0") && has("quality", "dust")
            && has("pixel_max", "2") && !dust->get("gravity"),
        "its spawn, modules (drift, curl, fade) and renderer as lighting fields; an absent module writes nothing");
    prefab::Json root;
    std::string error;
    rood_particles::Asset a;
    prefab::json_parse("{\"kind\":\"emitter\",\"modules\":{\"wobble\":{}}}", root);
    check(!rood_particles::asset_from_json("x", root, a, &error) && error.find("wobble") != std::string::npos,
        "an unknown module is refused, by name");
    prefab::json_parse("{\"kind\":\"emitter\",\"lighting\":{\"mode\":\"lit\"},\"render\":{\"type\":\"pixel\"}}", root);
    check(rood_particles::asset_from_json("y", root, a, &error) && a.get("lit") && *a.get("lit") == "1",
        "lighting mode lit is the field lit=1");
    prefab::json_parse("{\"kind\":\"prefab\"}", root);
    check(!rood_particles::asset_from_json("z", root, a, &error), "only kind \"emitter\" files are emitter assets");
}
```

and in `main` change `(void)argc, (void)argv;` to nothing and add `assets(argc > 1 ? argv[1] : nullptr);` after `light_shape();`.

In `pc/CMakeLists.txt` change the probe's `add_executable` to:

```cmake
add_executable(particles_probe tests/particles_probe.cpp src/particle_assets.cpp src/lighting_layers.cpp src/light_prefabs.cpp)
```

- [ ] **Step 2: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null && ninja -C pc/build particles_probe
```

Expected: `fatal error: particle_assets.h: No such file or directory`.

- [ ] **Step 3: The asset**

Create `pc/assets/emitters/dust_motes.json`:

```json
{
  "kind": "emitter",
  "name": "Dust motes",
  "category": "Atmosphere",
  "icon": "dust",
  "tags": ["dust", "motes", "interior", "light shafts"],
  "description": "Tiny motes of dust hanging in a room's air: they drift slowly and swirl a little, and show only where light falls through them (a window's beam, a lamp's pool); in shadow they vanish. Place the box where the light crosses the air.",
  "quality": "dust",
  "spawn": { "shape": "box", "size": [384, 256, 384], "count": 900, "lifetime": 14, "lifetime_jitter": 0.5 },
  "modules": {
    "drift": { "velocity": [1.2, 0.4, 0.8] },
    "curl": { "amplitude": 9, "scale": 56, "speed": 0.06 },
    "fade": { "in": 2.5, "out": 3 }
  },
  "render": { "type": "pixel", "pixel_min": 1, "pixel_max": 2, "color": [1, 0.95, 0.85], "brightness": 0.8, "variation": 0.6 },
  "lighting": { "mode": "in_light", "ambient": 0 }
}
```

Create `pc/assets/emitters/README.md` (the format, for authors and the library):

````markdown
# Emitter assets

An emitter asset is the look of a particle emitter (pc/docs/Particles.md):
how its particles spawn, how they move, how they are drawn and lit. An
emitter placed in a room (`emitter <id> asset=<name> ...` in the lighting
files, lighting/README.md "Emitters") takes every field it does not write
from its asset, so editing an asset here changes every emitter using it.
`ROOD_EMITTERS` reads another directory instead (tests).

```json
{
  "kind": "emitter",
  "name": "Dust motes", "category": "Atmosphere", "icon": "dust", "tags": ["dust"],
  "description": "What it is for and where it goes (the library shows it).",
  "quality": "dust",
  "spawn": { "shape": "box", "size": [384, 256, 384], "radius": 64, "count": 900, "lifetime": 14, "lifetime_jitter": 0.5 },
  "modules": {
    "drift": { "velocity": [1.2, 0.4, 0.8] },
    "curl": { "amplitude": 9, "scale": 56, "speed": 0.06 },
    "gravity": { "acceleration": 0 },
    "drag": { "rate": 0 },
    "fade": { "in": 2.5, "out": 3 }
  },
  "render": { "type": "pixel", "pixel_min": 1, "pixel_max": 2, "color": [1, 0.95, 0.85], "brightness": 0.8, "variation": 0.6 },
  "lighting": { "mode": "in_light", "ambient": 0 }
}
```

| JSON | Lighting field | Meaning |
| --- | --- | --- |
| `quality` | `quality` | the graphics setting that scales it: `dust` (Graphics > Dust: Off none, Low `dust_low` of its count, High all) |
| `spawn.shape` | `shape` | `box` (in its box), `room` (in the room's bounds), `point` (a ball of `radius`), `disc` (a horizontal disc of `radius`), `surface` (on the floor under its box) |
| `spawn.size`, `spawn.radius` | `size`, `radius` | the box's full extents; the point's or disc's radius (world units) |
| `spawn.count`, `spawn.lifetime`, `spawn.lifetime_jitter` | `count`, `lifetime`, `lifetime_jitter` | particles alive at once (High), seconds each lives, the share of that a particle may live less |
| `modules.drift.velocity` | `drift` | its steady drift, world units a second (y down) |
| `modules.curl.amplitude`, `.scale`, `.speed` | `curl`, `curl_scale`, `curl_speed` | turbulence: how far (units) a smooth swirling field moves it, the swirls' size, how fast they change |
| `modules.gravity.acceleration` | `gravity` | units a second squared, y down (negative: it rises) |
| `modules.drag.rate` | `drag` | per second: it falls at most `gravity / drag` |
| `modules.fade.in`, `.out` | `fade_in`, `fade_out` | seconds to appear and to go |
| `render.type` | `render` | `pixel` (square game pixels, nearest; soft sprites and streaks later) |
| `render.pixel_min`, `.pixel_max` | `pixel_min`, `pixel_max` | its size in game pixels (1..4), the same near and far |
| `render.color`, `.brightness`, `.variation` | `color`, `brightness`, `variation` | its colour (linear), scale, how much dimmer each one may be |
| `lighting.mode`, `.ambient` | `lit`, `ambient` | `in_light` (0: it shows only where light reaches it) or `lit` (1: also `ambient` everywhere) |

A module left out does nothing. `kind`, `name`, `category`, `icon`, `tags`
and `description` are what the editor's library lists.
````

- [ ] **Step 4: Write the header**

Create `pc/src/particle_assets.h`:

```cpp
/* Emitter assets (pc/assets/emitters/README.md; pc/docs/Particles.md):
 * pc/assets/emitters/<id>.json (ROOD_EMITTERS: another directory), each an
 * emitter's look as lighting field values (lighting/README.md "Emitters"):
 * its spawn, its behaviour modules (drift, curl, gravity, drag, fade; one
 * left out is neutral), its renderer and lighting, and what the library
 * lists (kind "emitter", name, category, tags, icon, description). An
 * emitter item names one with asset=<id>; every field it does not write
 * comes from it. No game dependencies (pc/tests/particles_probe.cpp). */
#ifndef ROOD_PARTICLE_ASSETS_H
#define ROOD_PARTICLE_ASSETS_H
#include <string>
#include <vector>

#include "lighting_layers.h"

namespace prefab {
struct Json;
}
namespace rood_particles {
struct Asset {
    std::string id, name, category, icon, description; /* id: the file's stem */
    std::vector<std::string> tags;
    lighting::Fields fields; /* lighting key, canonical value (kind "emitter") */
    const std::string* get(const std::string& key) const;
};
std::string assets_dir();
/* An asset from its JSON; false (and why) when it is not one. */
bool asset_from_json(const std::string& id, const prefab::Json& root, Asset& out, std::string* error);
/* Every asset of the directory, sorted by id (read once; reload_assets reads them again). */
const std::vector<Asset>& assets();
void reload_assets();
const Asset* find_asset(const std::string& id);
const std::string& assets_status(); /* "1 asset (pc/assets/emitters)", or the problems */
} // namespace rood_particles
#endif
```

- [ ] **Step 5: Write the implementation**

Create `pc/src/particle_assets.cpp`:

```cpp
// Emitter assets (particle_assets.h).
#include "particle_assets.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "light_prefabs.h"

namespace rood_particles {
namespace {
std::vector<Asset> loaded;
bool loaded_once;
std::string status;

/* Where each JSON value goes: group (a top-level object, or a module under "modules"), key, lighting field. */
struct Path {
    const char* group;
    const char* key;
    const char* field;
};
const Path PATHS[] = {
    { "spawn", "shape", "shape" }, { "spawn", "size", "size" }, { "spawn", "radius", "radius" },
    { "spawn", "count", "count" }, { "spawn", "lifetime", "lifetime" }, { "spawn", "lifetime_jitter", "lifetime_jitter" },
    { "drift", "velocity", "drift" }, { "curl", "amplitude", "curl" }, { "curl", "scale", "curl_scale" },
    { "curl", "speed", "curl_speed" }, { "gravity", "acceleration", "gravity" }, { "drag", "rate", "drag" },
    { "fade", "in", "fade_in" }, { "fade", "out", "fade_out" }, { "render", "type", "render" },
    { "render", "pixel_min", "pixel_min" }, { "render", "pixel_max", "pixel_max" }, { "render", "color", "color" },
    { "render", "brightness", "brightness" }, { "render", "variation", "variation" }, { "lighting", "mode", "lit" },
    { "lighting", "ambient", "ambient" },
};
const char* const MODULES[] = { "drift", "curl", "gravity", "drag", "fade" };
bool is_module(const char* group)
{
    for (const char* m : MODULES)
        if (!strcmp(m, group))
            return true;
    return false;
}
std::string text_of(const prefab::Json& j)
{
    if (j.type == prefab::Json::String)
        return j.string;
    if (j.type == prefab::Json::Bool)
        return j.boolean ? "1" : "0";
    if (j.type == prefab::Json::Number)
        return lighting::format_number(j.number, 6);
    std::string s;
    if (j.type == prefab::Json::Array)
        for (size_t i = 0; i < j.items.size(); i++)
            s += (i ? "," : "") + lighting::format_number(j.items[i].number, 6);
    return s;
}
bool read_text(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buffer[4096];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof buffer, f)) > 0)
        out.append(buffer, n);
    fclose(f);
    return true;
}
} // namespace

const std::string* Asset::get(const std::string& key) const
{
    for (const auto& f : fields)
        if (f.first == key)
            return &f.second;
    return nullptr;
}

std::string assets_dir()
{
    const char* e = getenv("ROOD_EMITTERS");
    return e && *e ? e : "pc/assets/emitters";
}

bool asset_from_json(const std::string& id, const prefab::Json& root, Asset& out, std::string* error)
{
    out = Asset();
    out.id = id;
    auto fail = [&](const std::string& why) {
        if (error)
            *error = id + ": " + why;
        return false;
    };
    const prefab::Json* kind = root.get("kind");
    if (root.type != prefab::Json::Object || !kind || kind->string != "emitter")
        return fail("not an emitter asset (kind \"emitter\")");
    for (const char* key : { "name", "category", "icon", "description" })
        if (const prefab::Json* j = root.get(key)) {
            std::string& to = !strcmp(key, "name") ? out.name : !strcmp(key, "category") ? out.category
                : !strcmp(key, "icon")                                                ? out.icon
                                                                                      : out.description;
            to = j->string;
        }
    if (out.name.empty())
        out.name = id;
    if (const prefab::Json* tags = root.get("tags"))
        for (const prefab::Json& t : tags->items)
            out.tags.push_back(t.string);
    const prefab::Json* modules = root.get("modules");
    if (modules)
        for (const auto& m : modules->members)
            if (!is_module(m.first.c_str()))
                return fail("modules: unknown module '" + m.first + "' (drift, curl, gravity, drag, fade)");
    if (const prefab::Json* q = root.get("quality"))
        out.fields.emplace_back("quality", q->string);
    for (const Path& p : PATHS) {
        const prefab::Json* group = is_module(p.group) ? (modules ? modules->get(p.group) : nullptr) : root.get(p.group);
        const prefab::Json* v = group ? group->get(p.key) : nullptr;
        if (!v)
            continue;
        std::string text = text_of(*v);
        if (!strcmp(p.field, "lit")) {
            if (text != "in_light" && text != "lit")
                return fail("lighting.mode is in_light or lit, not '" + text + "'");
            text = text == "lit" ? "1" : "0";
        }
        if (!strcmp(p.field, "shape") && text != "box" && text != "room" && text != "point" && text != "disc"
            && text != "surface")
            return fail("spawn.shape is box, room, point, disc or surface, not '" + text + "'");
        if (!strcmp(p.field, "render") && text != "pixel")
            return fail("render.type is pixel (soft sprites and streaks come later), not '" + text + "'");
        out.fields.emplace_back(p.field, lighting::canonical_value("emitter", p.field, text));
    }
    return true;
}

void reload_assets()
{
    loaded_once = true;
    loaded.clear();
    status.clear();
    std::string dir = assets_dir(), problems;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        status = "no emitter assets (" + dir + ")";
        return;
    }
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json"))
            continue;
        std::string text, error;
        prefab::Json root;
        Asset a;
        if (!read_text(dir + "/" + name, text) || !prefab::json_parse(text.c_str(), root, &error)
            || !asset_from_json(name.substr(0, name.size() - 5), root, a, &error)) {
            problems += (problems.empty() ? "" : "; ") + name + ": " + error;
            continue;
        }
        loaded.push_back(a);
    }
    closedir(d);
    std::sort(loaded.begin(), loaded.end(), [](const Asset& a, const Asset& b) { return a.id < b.id; });
    status = std::to_string(loaded.size()) + (loaded.size() == 1 ? " asset (" : " assets (") + dir + ")"
        + (problems.empty() ? "" : "; " + problems);
    if (!problems.empty())
        fprintf(stderr, "[particles] %s\n", problems.c_str());
}
const std::vector<Asset>& assets()
{
    if (!loaded_once)
        reload_assets();
    return loaded;
}
const Asset* find_asset(const std::string& id)
{
    for (const Asset& a : assets())
        if (a.id == id)
            return &a;
    return nullptr;
}
const std::string& assets_status()
{
    assets();
    return status;
}
} // namespace rood_particles
```

- [ ] **Step 6: Run it and see it pass**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null && ninja -C pc/build particles_probe && ctest --test-dir pc/build -R particles_probe --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 7: Commit**

```bash
git add pc/src/particle_assets.h pc/src/particle_assets.cpp pc/assets/emitters pc/tests/particles_probe.cpp pc/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Emitter assets: pc/assets/emitters/*.json (modules flattened into lighting fields), the dust motes asset and its format

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: The emitter as a selection kind

**Files:**
- Modify: `pc/src/editor_selection.h` (`enum Kind`; `kind_name`'s comment), `pc/src/editor_selection.cpp` (`ref_of`'s prefixes, `kind_name`)
- Test: `pc/tests/selection_probe.cpp`

- [ ] **Step 1: Write the failing check**

In `pc/tests/selection_probe.cpp`, after the check `"kind and source names"`, add:

```cpp
    check(ref_of("emitter:d4").kind == KIND_EMITTER && ref_of("emitter:d4").id == "emitter:d4" && !ref_of("emitter:")
            && !strcmp(kind_name(KIND_EMITTER), "emitter"),
        "emitter ids are particle emitters (particles.cpp)");
```

- [ ] **Step 2: Run it and see it fail**

```bash
ninja -C pc/build selection_probe
```

Expected: `error: 'KIND_EMITTER' was not declared in this scope`.

- [ ] **Step 3: The kind**

In `pc/src/editor_selection.h`, change the enum to (appended: the others keep their values):

```cpp
enum Kind { KIND_NONE, KIND_LIGHT, KIND_ACTOR, KIND_GLOW, KIND_VOLUME, KIND_SUN, KIND_EMITTER };
```

the header comment's id list to `("light:<id>", "actor:<id>", "fog:<id>", "emitter:<id>", "glow:<key>"; lighting_layers.h)`, and `kind_name`'s comment to `/* "light", "actor", "glow", "fog", "sun", "emitter", "none" */`.

In `pc/src/editor_selection.cpp` `ref_of`, change the prefixes to

```cpp
    } prefixes[] = { { "light:", KIND_LIGHT }, { "actor:", KIND_ACTOR }, { "glow:", KIND_GLOW }, { "fog:", KIND_VOLUME },
        { "emitter:", KIND_EMITTER } };
```

and in `kind_name` add `: k == KIND_EMITTER ? "emitter"` after the `KIND_SUN` line.

- [ ] **Step 4: Run it and see it pass**

```bash
ninja -C pc/build selection_probe && pc/build/selection_probe | tail -1
```

Expected: the probe's PASS line; no `FAIL:` line.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_selection.h pc/src/editor_selection.cpp pc/tests/selection_probe.cpp
git commit -m "$(cat <<'EOF'
Editor selection: KIND_EMITTER (emitter:<id>)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Emitters in the room, the settings, RPC `particles` (items)

**Files:**
- Create: `pc/src/particles.h`, `pc/src/particles_internal.h`, `pc/src/particles.cpp`, `pc/tests/particles_smoke.py`
- Modify: `pc/src/editor_scene.h`, `pc/src/editor_scene.cpp` (`emitter_ref`, `emitter_hidden_id`), `pc/src/environment.cpp` (`rood_env::reset`, `apply_item`, `add_items`), `pc/src/lighting_editor.cpp` (`apply_state`'s kinds, line 151; `layers_outliner_header`, line 491), `pc/src/project_settings.cpp` (externs; `entries[]` after `fog_volume_shadows`), `pc/src/dev_rpc.c` (method `particles` after `environment`; `edits()`), `pc/tools/build_boot.py` (after `root / "pc/src/fog_sim.cpp",`), `pc/tests/run.py` (a Suite after `fog_volume_smoke`; `CHECKPOINT_SUITES`)

- [ ] **Step 1: Write the failing test (its first section)**

Create `pc/tests/particles_smoke.py`:

```python
#!/usr/bin/env python3
"""The port's particle system and its first use, dust motes (pc/src/particles.cpp,
particle_math.h; Vulkan vk_particles.inc, OpenGL gl_present.c; pc/docs/Particles.md).

    python3 pc/tests/particles_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

From a copy of the pinned card into its save room, with a scratch lights
directory (ROOD_LIGHTS) and settings file (ROOD_SETTINGS); the room's packs
(ROOD_LIGHTING) as in the game. Sections:

- items: an emitter added over RPC with a few fields saves as one
  `emitter <id> asset=dust_motes ...` line (only what differs from its
  asset), comes back from the file with the same fields, and goes;
- motes: with the time frozen the motes are the same from frame to frame,
  inside their box; Dust Off draws none, Low its share, High all; time moves
  them by their drift;
- picture: Dust Off is the picture without emitters, pixel for pixel; a
  wall between a light and the motes leaves them dark (none shows), the
  motes a light in the room reaches show as bright game pixels, each a
  uniform square of 1 or 2 game pixels with nothing around it;
- editor: an emitter is an object of the editor (icon, click, Outliner),
  Details edits it (one undo step, reset to its asset's value), the W gizmo
  moves it with the mouse, Delete and undo, duplicate;
- cost: the motes' GPU time (Vulkan, RPC perf section "particles") or CPU
  time (OpenGL, RPC particles cpu_ms) for 4096 motes;
- example: the town house (zone 34 room 0) has its authored dust, subtle.
"""
import argparse
import math
import os
import pathlib
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_selection_smoke import pinned_card  # noqa: E402

EMITTER = "dusttest000001"  # the test's emitters' ids (items emitter:<id>)
FAR = "dusttest000002"


class Session:
    def __init__(self, game, out, user, vulkan):
        self.game, self.out, self.user, self.vulkan = game, out, user, vulkan
        self.failures = []
        self.player = self.zone = self.room = None

    def check(self, ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            self.failures.append(message)
        return ok

    def particles(self, **params):
        return self.game.call("particles", **params)

    def emitter(self, ident=EMITTER):
        return next((e for e in self.particles()["emitters"] if e["id"] == ident), None)

    def room_file(self):
        return self.user / f"zone{self.zone:02d}_room{self.room:02d}.txt"

    def shot(self, name, frames=8):
        if frames:
            self.game.run(frames)
        path = self.game.present_screenshot(self.out / f"{name}.png")
        return np.asarray(Image.open(path).convert("RGB"), np.float32) / 255.0


def start(args):
    card = pathlib.Path(args.card)
    if not card.exists():
        print("SKIP: no memory card", card)
        return None
    out = pathlib.Path(args.out) if args.out else OUT / "particles_smoke"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shutil.copyfile(card, out / "card1.mcd")
    user = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    # Lights hold still, no anti-aliasing filter (motes are exact game pixels), Dust Off as by default.
    (out / "settings.cfg").write_text("light_animation 0\naa off\n")
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_GI": "1", "ROOD_SCALE": "2", "ROOD_WINDOW": "960x720", "ROOD_LIGHTING_PRESET": "ultra",
           "ROOD_PERF": "1", "ROOD_EMITTERS": str(ROOT / "pc/assets/emitters")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-dust-{os.getpid()}.sock"
    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)
    s = Session(game, out, user, vulkan)
    st = game.state()
    s.player, s.zone, s.room = st.get("player"), st.get("zone"), st.get("room")
    if not s.player:
        s.check(False, f"no room with Ashley after Continue: {st}")
    return s


def finish(s):
    s.game.close()
    log = pathlib.Path(s.game.log).read_text(errors="replace")
    bad = [line for line in log.splitlines()
           if "[crash]" in line or "[gl] shader" in line or "validation:" in line or "[particles] the" in line]
    s.check(not bad, "no crash, shader error or validation message" + (f": {bad[:3]}" if bad else ""))
    shutil.rmtree(s.user, ignore_errors=True)
    print("PASS: particles" if not s.failures else f"FAIL: {len(s.failures)} check(s)")
    return 1 if s.failures else 0


def check_items(s):
    """An emitter round-trips through the lighting file: only what differs from its asset is written."""
    p = s.player
    pos = [round(p["x"]), round(p["y"] - 120), round(p["z"])]
    r = s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": pos, "count": 300, "drift": [0, -2, 0],
                               "seed": 11}])
    e = s.emitter()
    s.check(e is not None and e["asset"] == "dust_motes" and e["count"] == 300 and e["fields"]["curl"] == "9"
            and e["fields"]["drift"] == "0,-2,0",
            f"added over RPC: its fields, the rest from its asset ({e and e['fields']})")
    s.check(any(a["id"] == "dust_motes" for a in r["assets"]), "the assets are listed (the library's source)")
    s.game.call("lighting", action="save")
    text = s.room_file().read_text() if s.room_file().exists() else ""
    line = next((l for l in text.splitlines() if l.startswith(f"emitter {EMITTER} ")), "")
    s.check(line == f"emitter {EMITTER} asset=dust_motes pos={pos[0]},{pos[1]},{pos[2]} seed=11 count=300 drift=0,-2,0",
            f"saved as one line, only what differs from its asset ({line})")
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    s.check(s.emitter() is None, "removed over RPC")
    s.game.call("lighting", action="reload")
    s.game.run(4)
    e = s.emitter()
    s.check(e is not None and e["pos"] == pos and e["count"] == 300 and e["fields"]["drift"] == "0,-2,0"
            and e["fields"]["curl"] == "9", f"read back from the file the same ({e and e['fields']})")
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    s.game.call("lighting", action="save")
    text = s.room_file().read_text() if s.room_file().exists() else ""
    s.check(f"emitter {EMITTER}" not in text, "removed and saved: the line goes")


# In order: later sections start from where earlier ones leave the room.
SECTIONS = [check_items]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (items, motes, ...)")
    args = parser.parse_args()
    s = start(args)
    if s is None:
        return 77
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                section(s)
    finally:
        status = finish(s)
    return status


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `fog_volume_smoke` Suite, add:

```python
            Suite("particles_smoke", [PY, str(TESTS / "particles_smoke.py"), "--card", str(args.save)], 600,
                  note="particles: emitters round-trip through the lighting file; motes deterministic, counted by "
                       "Dust; Dust Off pixel-identical, shadowed motes never show, lit ones are square game pixels; "
                       "editor select/move/reset/delete; GPU/CPU cost; the town house's dust (34/0)"),
```

and add `"particles_smoke"` to `CHECKPOINT_SUITES`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 particles_smoke
```

Expected: FAIL (`unknown method particles`).

- [ ] **Step 3: The C API**

Create `pc/src/particles.h`:

```c
/* The port's particle system (pc/docs/Particles.md; not the game's own
 * effect particles): per room up to PC_PARTICLE_EMITTERS emitters
 * (`emitter <id>` items of the lighting layers, lighting/README.md
 * "Emitters"), each a look from an emitter asset (pc/assets/emitters) and
 * its own overrides, and their motes evaluated in closed form each present
 * (particle_math.h), lit, and splatted into a key image (a game pixel per
 * texel: the nearest mote) and a colour per mote that the present shaders
 * composite (pc/shaders/vulkan/particles.glsl). Vulkan: one compute pass
 * (vk_particles.inc); OpenGL: this file's CPU path. */
#ifndef ROOD_PARTICLES_H
#define ROOD_PARTICLES_H
#include <stdint.h>

#include "light_list.h"
#include "particle_math.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Settings (project_settings.cpp; Details > Atmosphere > Dust; RPC particles):
 * dust 0 Off (nothing evaluated or drawn: the picture as before), 1 Low
 * (each "dust" emitter's count x dust_low), 2 High (all); particles_glow
 * scales the glows' light on motes; particles_lights the strongest sources
 * each mote is lit by (1..4, each with a shadow ray), the sun on top;
 * particles_glow_shadows: glows' light is shadowed too; particles_cpu_rays
 * the OpenGL path's shadow rays a frame (its motes refreshed round-robin).
 * RPC only: particles_debug (1: every mote white, unlit: where they are),
 * particles_time (fixed seconds; -1 live). */
extern int pc_dust, pc_particles_lights, pc_particles_glow_shadows, pc_particles_cpu_rays, pc_particles_debug;
extern float pc_dust_low, pc_particles_glow, pc_particles_time;

/* OpenGL: this present's motes for the display area w x h (game pixels):
 * the key image (w x h, PC_PARTICLE_NONE where no mote) and the colours
 * (rgba per mote: rgb its colour, a its light's luminance). Returns 0 when
 * there are none (Dust Off, no emitter, no camera). */
int pc_particles_cpu_frame(int w, int h, const uint32_t** keys, const float** colours, int* count);
/* Vulkan: this present's records for the pass (pc_particles_gpu) and the
 * lights (`lights`, at most `max`); returns the motes (0: no pass). */
int pc_particles_gpu_frame(int w, int h, pc_particles_gpu* out, pc_light_gpu* lights, int max, int* light_count);
/* Vulkan: whether this frame's pass traced its shadow rays (the RPC's path). */
void pc_particles_gpu_traced(int traced);
/* RPC particles (dev_rpc.c hands over the params object's text): 0, or -1 and a message. */
int pc_particles_rpc(const char* params, char* out, int size);
#ifdef __cplusplus
}
#endif
#endif
```

Create `pc/src/particles_internal.h`:

```cpp
/* The room's particle emitters for the editor (particles.cpp): as lighting
 * items ("emitter:<id>"), by index, for the scene (editor_scene.cpp), the
 * schema (particles_editor.cpp), the clipboard and the room state
 * (environment.cpp). Editor internals. */
#ifndef ROOD_PARTICLES_INTERNAL_H
#define ROOD_PARTICLES_INTERNAL_H
#include <string>

#include "lighting_layers.h"

namespace rood_particles {
/* The room state (environment.cpp rood_env::reset / apply_item / add_items call these). */
void reset();
bool apply_item(const lighting::Item& it); /* an `emitter` item; false for others */
void add_items(lighting::State& s);

int emitter_count();
std::string emitter_id(int index); /* "emitter:<id>" ("" out of range; an id assigned on the spot) */
int emitter_index(const std::string& item_id); /* -1: none */
std::string emitter_label(int index); /* "Emitter 2: Dust motes" */
bool emitter_centre(int index, float out[3], float* radius);
bool emitter_size(int index, float out[3]); /* its box; false out of range */
void set_emitter_size(int index, const float size[3]); /* the room unsaved */
lighting::Item emitter_item(int index);
/* A copy of `item` with a new id, offset: its index, or -1. */
int add_emitter_item(const lighting::Item& item, const float offset[3]);
void remove_emitter(int index);
/* A new emitter of `asset` at `pos`, selected; its index (the Atmosphere tab, the library). */
int add_emitter(const std::string& asset, const float pos[3]);
/* Its value of a lighting field (as written, else its asset's, else the default); setting one
 * writes it (equal to its asset's: it follows the asset again). */
std::string field(int index, const char* key);
void set_field(int index, const char* key, const std::string& value);
/* Its asset's value of a field (the reset arrow's), and whether it writes its own. */
std::string asset_value(int index, const char* key);
bool overrides(int index, const char* key);
/* The emitter's own fields as members, for the editor and the RPC. */
struct Own {
    std::string name, asset;
    bool enabled;
    float pos[3], yaw;
    int seed;
};
Own own(int index);
void set_own(int index, const Own& o);
bool emitters_shown(); /* Show > Emitters */
bool* emitters_shown_flag();
int drawn(int index); /* its motes last frame */
} // namespace rood_particles
#endif
```

- [ ] **Step 4: The room's emitters and the RPC**

Create `pc/src/particles.cpp` with the room's emitters, the settings and the RPC (the frame and the CPU path come in Task 6):

```cpp
// The port's particle system (particles.h, particles_internal.h; pc/docs/Particles.md):
// the room's emitters (lighting items, their assets resolved), the frame the
// renderers draw, OpenGL's CPU path, RPC particles.
#include "particles.h"
#include "particles_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <functional>
#include <string>
#include <vector>

#include "atmosphere.h"
#include "editor_scene.h"
#include "editor_selection.h"
#include "editor_undo.h"
#include "environment.h"
#include "light_prefabs.h"
#include "light_shape.h"
#include "lighting_layers.h"
#include "lights_internal.h"
#include "particle_assets.h"
#include "scene_capture.h"

extern "C" {
unsigned pc_vsync_count(void) __attribute__((weak)); /* game time */
int pc_gi_trace_cpu(const float o[3], const float d[3], float tmax, float* t_out, float centre[3]) __attribute__((weak));
int pc_gi_scene_ready(void) __attribute__((weak)); /* ptgi.c: OpenGL's traced scene is built */
extern int pc_gte_widescreen;
extern int pc_vk_active __attribute__((weak));
extern int pc_gl_view[4] __attribute__((weak));
void pc_lights_mark_dirty(void);
int pc_vk_particles_read(uint32_t* keys, int w, int h, float* colours, int count) __attribute__((weak)); /* vk_particles.inc */

int pc_dust = 0, pc_particles_lights = 2, pc_particles_glow_shadows = 1, pc_particles_cpu_rays = 1536, pc_particles_debug = 0;
float pc_dust_low = 0.4f, pc_particles_glow = 1.0f, pc_particles_time = -1.0f;
}

namespace {
const float DEG = 3.14159265f / 180.0f;
using rood_particles::Asset;

/* Defaults: the lighting schema's (lighting_layers.cpp kind "emitter"). */
struct Emitter {
    std::string id, name, asset; /* id: the item's name */
    bool enabled = true;
    float pos[3] = { 0, 0, 0 }, yaw = 0;
    int seed = 1;
    /* from its asset unless written */
    int shape = 0; /* SHAPES */
    float size[3] = { 256, 192, 256 }, radius = 64;
    int count = 400;
    float lifetime = 10, jitter = 0.5f;
    float drift[3] = { 0, 0, 0 }, curl = 0, curl_scale = 64, curl_speed = 0.1f, gravity = 0, drag = 0;
    float fade_in = 0, fade_out = 0;
    int pixel_min = 1, pixel_max = 2;
    float color[3] = { 1, 1, 1 }, brightness = 1, variation = 0.5f;
    bool lit = false;
    float ambient = 0;
    std::string quality = "dust";
    int drawn = 0; /* motes last frame */
};
const char* const SHAPES[5] = { "box", "room", "point", "disc", "surface" };
std::vector<Emitter> emitters;
bool show_emitters = true;
int time_zone = -1, time_room = -1;
double time_origin;

std::string num(double v, int d) { return lighting::format_number(v, d); }
std::string vec3(const float* v, int d) { return num(v[0], d) + "," + num(v[1], d) + "," + num(v[2], d); }
const Asset* asset_of(const Emitter& e) { return e.asset.empty() ? nullptr : rood_particles::find_asset(e.asset); }

/* Every field an asset may give, as lighting text. */
lighting::Fields asset_fields(const Emitter& e)
{
    return { { "shape", SHAPES[e.shape] }, { "size", vec3(e.size, 1) }, { "radius", num(e.radius, 1) },
        { "count", std::to_string(e.count) }, { "lifetime", num(e.lifetime, 2) }, { "lifetime_jitter", num(e.jitter, 2) },
        { "drift", vec3(e.drift, 2) }, { "curl", num(e.curl, 2) }, { "curl_scale", num(e.curl_scale, 1) },
        { "curl_speed", num(e.curl_speed, 3) }, { "gravity", num(e.gravity, 3) }, { "drag", num(e.drag, 3) },
        { "fade_in", num(e.fade_in, 2) }, { "fade_out", num(e.fade_out, 2) }, { "render", "pixel" },
        { "pixel_min", std::to_string(e.pixel_min) }, { "pixel_max", std::to_string(e.pixel_max) },
        { "color", vec3(e.color, 3) }, { "brightness", num(e.brightness, 3) }, { "variation", num(e.variation, 2) },
        { "lit", e.lit ? "1" : "0" }, { "ambient", num(e.ambient, 4) }, { "quality", e.quality } };
}
void from_fields(Emitter& e, const std::function<std::string(const char*)>& get)
{
    auto f = [&](const char* k) { return (float)atof(get(k).c_str()); };
    auto v3 = [&](const char* k, float* out) {
        float x[3];
        if (sscanf(get(k).c_str(), "%f,%f,%f", &x[0], &x[1], &x[2]) == 3)
            memcpy(out, x, sizeof x);
    };
    std::string shape = get("shape");
    e.shape = 0;
    for (int i = 0; i < 5; i++)
        if (shape == SHAPES[i])
            e.shape = i;
    v3("size", e.size);
    e.radius = fmaxf(f("radius"), 0);
    e.count = std::max(0, std::min(PC_PARTICLES_MAX, (int)lroundf(f("count"))));
    e.lifetime = fmaxf(f("lifetime"), 0.05f), e.jitter = fminf(fmaxf(f("lifetime_jitter"), 0), 0.95f);
    v3("drift", e.drift);
    e.curl = fmaxf(f("curl"), 0), e.curl_scale = fmaxf(f("curl_scale"), 1), e.curl_speed = f("curl_speed");
    e.gravity = f("gravity"), e.drag = fmaxf(f("drag"), 0);
    e.fade_in = fmaxf(f("fade_in"), 0), e.fade_out = fmaxf(f("fade_out"), 0);
    e.pixel_min = std::max(1, std::min(4, (int)lroundf(f("pixel_min"))));
    e.pixel_max = std::max(e.pixel_min, std::min(4, (int)lroundf(f("pixel_max"))));
    v3("color", e.color);
    e.brightness = fmaxf(f("brightness"), 0), e.variation = fminf(fmaxf(f("variation"), 0), 1);
    e.lit = f("lit") > 0.5f, e.ambient = fmaxf(f("ambient"), 0);
    e.quality = get("quality");
}
/* A field of an item: as written, else its asset's, else the schema's default. */
std::string item_field(const lighting::Item& it, const Asset* a, const char* key)
{
    if (const std::string* v = it.get(key))
        return *v;
    if (a)
        if (const std::string* v = a->get(key))
            return *v;
    return lighting::field_default("emitter", key);
}
std::string canonical(const char* key, const std::string& v) { return lighting::canonical_value("emitter", key, v); }
/* An emitter as its item: with an asset, only what differs from it (an asset edit updates every emitter). */
lighting::Item item_of(Emitter& e)
{
    if (e.id.empty())
        e.id = lighting::new_name();
    lighting::Item it;
    it.kind = "emitter", it.name = e.id;
    if (!e.name.empty())
        it.set("name", e.name);
    if (!e.asset.empty())
        it.set("asset", e.asset);
    it.set("pos", vec3(e.pos, 1));
    it.set("yaw", num(e.yaw, 2));
    it.set("enabled", e.enabled ? "1" : "0");
    it.set("seed", std::to_string(e.seed));
    const Asset* a = asset_of(e); /* an unknown asset keeps every value */
    for (const auto& f : asset_fields(e)) {
        if (a) {
            const std::string* p = a->get(f.first);
            std::string base = p ? *p : lighting::field_default("emitter", f.first);
            if (canonical(f.first.c_str(), f.second) == canonical(f.first.c_str(), base))
                continue;
        }
        it.set(f.first.c_str(), f.second);
    }
    lighting::canonicalize(it);
    return it;
}
int index_of_id(const std::string& id)
{
    for (int i = 0; i < (int)emitters.size(); i++)
        if (emitters[i].id == id)
            return i;
    return -1;
}
double game_seconds()
{
    if (pc_vsync_count)
        return pc_vsync_count() / 59.94;
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
/* Seconds since entering the room (the fog's medium_time, unwrapped), or the fixed time. */
float particles_time()
{
    if (pc_particles_time >= 0)
        return pc_particles_time;
    if (time_zone != rood_lights::zone || time_room != rood_lights::room) {
        time_zone = rood_lights::zone, time_room = rood_lights::room;
        time_origin = game_seconds();
    }
    return (float)fmax(game_seconds() - time_origin, 0.0);
}
} // namespace

/* ---- the room's emitters (particles_internal.h) ---- */

void rood_particles::reset() { emitters.clear(); }
bool rood_particles::apply_item(const lighting::Item& it)
{
    if (it.kind != "emitter")
        return false;
    Emitter e;
    e.id = it.name;
    if (const std::string* n = it.get("name"))
        e.name = *n;
    if (const std::string* a = it.get("asset"))
        e.asset = *a;
    const Asset* a = asset_of(e);
    if (!e.asset.empty() && !a)
        fprintf(stderr, "[particles] emitter %s: unknown asset '%s' (its fields as written)\n", e.id.c_str(), e.asset.c_str());
    auto get = [&](const char* key) { return item_field(it, a, key); };
    e.enabled = atoi(get("enabled").c_str()) != 0;
    sscanf(get("pos").c_str(), "%f,%f,%f", &e.pos[0], &e.pos[1], &e.pos[2]);
    e.yaw = (float)atof(get("yaw").c_str());
    e.seed = std::max(1, atoi(get("seed").c_str()));
    from_fields(e, get);
    int i = index_of_id(e.id);
    if (i >= 0)
        emitters[i] = e;
    else
        emitters.push_back(e);
    return true;
}
void rood_particles::add_items(lighting::State& s)
{
    for (Emitter& e : emitters) {
        lighting::Item it = item_of(e);
        s[it.id()] = it;
    }
}
int rood_particles::emitter_count() { return (int)emitters.size(); }
std::string rood_particles::emitter_id(int index)
{
    if (index < 0 || index >= (int)emitters.size())
        return "";
    if (emitters[index].id.empty())
        emitters[index].id = lighting::new_name();
    return "emitter:" + emitters[index].id;
}
int rood_particles::emitter_index(const std::string& item_id)
{
    if (item_id.compare(0, 8, "emitter:") || item_id.size() <= 8)
        return -1;
    return index_of_id(item_id.substr(8));
}
std::string rood_particles::emitter_label(int index)
{
    if (index < 0 || index >= (int)emitters.size())
        return "";
    const Emitter& e = emitters[index];
    const Asset* a = asset_of(e);
    std::string what = !e.name.empty() ? e.name : a ? a->name : "Emitter";
    return "Emitter " + std::to_string(index + 1) + ": " + what;
}
bool rood_particles::emitter_centre(int index, float out[3], float* radius)
{
    if (index < 0 || index >= (int)emitters.size())
        return false;
    const Emitter& e = emitters[index];
    memcpy(out, e.pos, sizeof e.pos);
    *radius = e.shape == 2 || e.shape == 3 ? fmaxf(e.radius, 32)
                                           : 0.5f * sqrtf(e.size[0] * e.size[0] + e.size[1] * e.size[1] + e.size[2] * e.size[2]);
    return true;
}
bool rood_particles::emitter_size(int index, float out[3])
{
    if (index < 0 || index >= (int)emitters.size())
        return false;
    memcpy(out, emitters[index].size, sizeof emitters[index].size);
    return true;
}
void rood_particles::set_emitter_size(int index, const float size[3])
{
    if (index < 0 || index >= (int)emitters.size())
        return;
    for (int k = 0; k < 3; k++)
        emitters[index].size[k] = fmaxf(size[k], 2);
    pc_lights_mark_dirty();
}
lighting::Item rood_particles::emitter_item(int index) { return item_of(emitters[index]); }
int rood_particles::add_emitter_item(const lighting::Item& item, const float offset[3])
{
    if (item.kind != "emitter")
        return -1;
    lighting::Item copy = item;
    copy.name = lighting::new_name();
    if (!apply_item(copy))
        return -1;
    Emitter& e = emitters.back();
    for (int k = 0; k < 3; k++)
        e.pos[k] += offset[k];
    return (int)emitters.size() - 1;
}
void rood_particles::remove_emitter(int index)
{
    if (index >= 0 && index < (int)emitters.size())
        emitters.erase(emitters.begin() + index);
}
int rood_particles::add_emitter(const std::string& asset, const float pos[3])
{
    lighting::Item it;
    it.kind = "emitter", it.name = lighting::new_name();
    if (!asset.empty())
        it.set("asset", asset);
    it.set("pos", vec3(pos, 1));
    it.set("seed", std::to_string(1 + (int)(prefab::hash32((uint32_t)emitters.size() * 977u + 13u) % 999999u)));
    apply_item(it);
    int i = (int)emitters.size() - 1;
    rood_selection::set(rood_scene::emitter_ref(i), rood_selection::FROM_CODE);
    rood_scene::publish();
    pc_lights_mark_dirty();
    return i;
}
std::string rood_particles::field(int index, const char* key)
{
    lighting::Item it = item_of(emitters[index]);
    return item_field(it, asset_of(emitters[index]), key);
}
void rood_particles::set_field(int index, const char* key, const std::string& value)
{
    lighting::Item it = item_of(emitters[index]);
    it.set(key, canonical(key, value));
    apply_item(it);
    pc_lights_mark_dirty();
}
std::string rood_particles::asset_value(int index, const char* key)
{
    const Asset* a = asset_of(emitters[index]);
    const std::string* v = a ? a->get(key) : nullptr;
    return v ? *v : std::string(lighting::field_default("emitter", key));
}
/* An item writes an asset field only where it differs from its asset (item_of; without an asset, from the default). */
bool rood_particles::overrides(int index, const char* key)
{
    return lighting::preset_field("emitter", key) && item_of(emitters[index]).get(key) != nullptr;
}
rood_particles::Own rood_particles::own(int index)
{
    const Emitter& e = emitters[index];
    Own o;
    o.name = e.name, o.asset = e.asset, o.enabled = e.enabled, o.yaw = e.yaw, o.seed = e.seed;
    memcpy(o.pos, e.pos, sizeof o.pos);
    return o;
}
void rood_particles::set_own(int index, const Own& o)
{
    lighting::Item it = item_of(emitters[index]);
    if (o.asset != emitters[index].asset) { /* a new asset: the fields it writes stay, the rest follow the new one */
        it.erase("asset");
        if (!o.asset.empty())
            it.set("asset", o.asset);
    }
    it.erase("name");
    if (!o.name.empty())
        it.set("name", o.name);
    it.set("pos", vec3(o.pos, 1));
    it.set("yaw", num(o.yaw, 2));
    it.set("enabled", o.enabled ? "1" : "0");
    it.set("seed", std::to_string(std::max(1, o.seed)));
    lighting::canonicalize(it);
    apply_item(it);
    pc_lights_mark_dirty();
}
bool rood_particles::emitters_shown() { return show_emitters; }
bool* rood_particles::emitters_shown_flag() { return &show_emitters; }
int rood_particles::drawn(int index) { return index >= 0 && index < (int)emitters.size() ? emitters[index].drawn : 0; }

/* ---- RPC particles ---- */

namespace {
using prefab::Json;
std::string jstr(const std::string& s) { return prefab::json_quote(s); }
std::string jvec(const float* v, int n)
{
    std::string s = "[";
    for (int i = 0; i < n; i++)
        s += (i ? "," : "") + num(v[i], 3);
    return s + "]";
}
/* A JSON value as lighting text (numbers, vectors, words, booleans). */
std::string text_of(const Json& j)
{
    if (j.type == Json::String)
        return j.string;
    if (j.type == Json::Bool)
        return j.boolean ? "1" : "0";
    if (j.type == Json::Number)
        return num(j.number, 6);
    std::string s;
    for (size_t i = 0; i < j.items.size(); i++)
        s += (i ? "," : "") + num(j.items[i].number, 6);
    return s;
}
std::string bare(const std::string& id) { return id.compare(0, 8, "emitter:") ? id : id.substr(8); }
struct Stats {
    const char* path = "off"; /* "traced", "unshadowed", "off" */
    int motes = 0;
    double cpu_ms = 0; /* smoothed */
    unsigned frames = 0;
} stats;
std::string emitter_json(int i)
{
    const Emitter& e = emitters[i];
    lighting::Item it = item_of(emitters[i]);
    const Asset* a = asset_of(e);
    std::string fields = "{";
    bool first = true;
    for (const auto& f : asset_fields(e)) {
        fields += (first ? "" : ",") + jstr(f.first) + ":" + jstr(item_field(it, a, f.first.c_str()));
        first = false;
    }
    return "{\"id\":" + jstr(e.id) + ",\"ref\":" + jstr("emitter:" + e.id) + ",\"name\":" + jstr(e.name)
        + ",\"asset\":" + jstr(e.asset) + ",\"enabled\":" + (e.enabled ? "true" : "false") + ",\"pos\":[" + num(e.pos[0], 1)
        + "," + num(e.pos[1], 1) + "," + num(e.pos[2], 1) + "],\"yaw\":" + num(e.yaw, 2) + ",\"seed\":" + std::to_string(e.seed)
        + ",\"shape\":" + jstr(SHAPES[e.shape]) + ",\"size\":" + jvec(e.size, 3) + ",\"count\":" + std::to_string(e.count)
        + ",\"drawn\":" + std::to_string(e.drawn) + ",\"fields\":" + fields + "}}";
}
} // namespace

/* params: level (dust 0..2), low (dust_low), time (seconds; -1 live), debug,
 * settings {glow, lights, glow_shadows, cpu_rays}, reload_assets, emitters [{id
 * (existing: changed; else added), remove, name, asset, pos, yaw, enabled, seed, or any
 * asset field}], add {asset, pos} (a new emitter, selected: "added"), select (an id: the
 * editor's selection), motes {emitter, max} (Task 6). The reply: the settings, the
 * path ("traced", "unshadowed", "off"), the motes drawn, cpu_ms, the view, the assets,
 * the emitters (each with its resolved fields). */
extern "C" int pc_particles_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_lights::track_room();
    bool changed = false;
    if (const Json* j = params.get("level"))
        pc_dust = std::max(0, std::min(2, (int)j->number));
    if (const Json* j = params.get("low"))
        pc_dust_low = fminf(fmaxf((float)j->number, 0), 1);
    if (const Json* j = params.get("time"))
        pc_particles_time = (float)j->number;
    if (const Json* j = params.get("debug"))
        pc_particles_debug = j->type == Json::Bool ? j->boolean : j->number != 0;
    if (const Json* st = params.get("settings")) {
        if (const Json* j = st->get("glow"))
            pc_particles_glow = fmaxf((float)j->number, 0);
        if (const Json* j = st->get("lights"))
            pc_particles_lights = std::max(1, std::min(4, (int)j->number));
        if (const Json* j = st->get("glow_shadows"))
            pc_particles_glow_shadows = j->type == Json::Bool ? j->boolean : j->number != 0;
        if (const Json* j = st->get("cpu_rays"))
            pc_particles_cpu_rays = std::max(16, (int)j->number);
    }
    if (const Json* j = params.get("reload_assets"))
        if (j->type == Json::Bool ? j->boolean : j->number != 0)
            rood_particles::reload_assets();
    if (const Json* list = params.get("emitters"))
        for (const Json& o : list->items) {
            const Json* id = o.get("id");
            std::string name = id ? bare(id->string) : lighting::new_name();
            int i = index_of_id(name);
            const Json* rm = o.get("remove");
            if (rm && (rm->type == Json::Bool ? rm->boolean : rm->number != 0)) {
                if (i >= 0) {
                    emitters.erase(emitters.begin() + i);
                    changed = true;
                }
                continue;
            }
            lighting::Item it;
            if (i >= 0)
                it = item_of(emitters[i]);
            else
                it.kind = "emitter", it.name = name;
            for (const auto& m : o.members) {
                if (m.first == "id" || m.first == "remove")
                    continue;
                if (!lighting::known_field("emitter", m.first)) {
                    snprintf(out, size, "emitters: no field %s (lighting/README.md \"Emitters\")", m.first.c_str());
                    return -1;
                }
                it.set(m.first.c_str(), canonical(m.first.c_str(), text_of(m.second)));
            }
            if (!it.get("pos"))
                it.set("pos", "0,0,0");
            rood_particles::apply_item(it);
            changed = true;
        }
    std::string extra;
    if (const Json* add = params.get("add")) {
        const Json* a = add->get("asset");
        const Json* p = add->get("pos");
        float pos[3] = { 0, 0, 0 };
        if (!p || p->items.size() != 3) {
            snprintf(out, size, "add needs pos [x, y, z]");
            return -1;
        }
        for (int k = 0; k < 3; k++)
            pos[k] = (float)p->items[k].number;
        int i = rood_particles::add_emitter(a ? a->string : "dust_motes", pos);
        pc_undo_hint("Add emitter");
        extra += ",\"added\":" + jstr(emitters[i].id);
        changed = true;
    }
    if (const Json* sel = params.get("select")) {
        int i = index_of_id(bare(sel->string));
        rood_selection::set(i >= 0 ? rood_scene::emitter_ref(i) : rood_selection::Ref(), rood_selection::FROM_RPC);
        rood_scene::publish();
    }
    if (changed) {
        pc_lights_mark_dirty();
        rood_scene::refresh();
    }
    std::string s = "{\"level\":" + std::to_string(pc_dust) + ",\"low\":" + num(pc_dust_low, 3) + ",\"time\":"
        + num(pc_particles_time, 3) + ",\"debug\":" + std::to_string(pc_particles_debug) + ",\"settings\":{\"glow\":"
        + num(pc_particles_glow, 3) + ",\"lights\":" + std::to_string(pc_particles_lights) + ",\"glow_shadows\":"
        + std::to_string(pc_particles_glow_shadows) + ",\"cpu_rays\":" + std::to_string(pc_particles_cpu_rays)
        + "},\"renderer\":\"" + (&pc_vk_active && pc_vk_active ? "vulkan" : "gl") + "\",\"path\":\"" + stats.path
        + "\",\"motes\":" + std::to_string(stats.motes) + ",\"cpu_ms\":" + num(stats.cpu_ms, 4) + ",\"assets_status\":"
        + jstr(rood_particles::assets_status()) + ",\"assets\":[";
    const auto& list = rood_particles::assets();
    for (size_t i = 0; i < list.size(); i++) {
        std::string tags = "[";
        for (size_t t = 0; t < list[i].tags.size(); t++)
            tags += (t ? "," : "") + jstr(list[i].tags[t]);
        s += std::string(i ? "," : "") + "{\"id\":" + jstr(list[i].id) + ",\"name\":" + jstr(list[i].name)
            + ",\"category\":" + jstr(list[i].category) + ",\"icon\":" + jstr(list[i].icon) + ",\"tags\":" + tags + "]}";
    }
    s += "],\"emitters\":[";
    for (int i = 0; i < (int)emitters.size(); i++)
        s += (i ? "," : "") + emitter_json(i);
    s += "]" + extra + "}";
    if ((int)s.size() >= size) {
        snprintf(out, size, "reply too large");
        return -1;
    }
    snprintf(out, size, "%s", s.c_str());
    return 0;
}
```

In `pc/src/editor_scene.h` add, after `Ref volume_ref(int index);`, `Ref emitter_ref(int index);`, and after `bool volume_hidden_id(const std::string& id); /* by a volume's bare id */`, `bool emitter_hidden_id(const std::string& id); /* by an emitter's bare id */`. In `pc/src/editor_scene.cpp` add `#include "particles_internal.h"`, after `rood_scene::volume_ref`:

```cpp
Ref rood_scene::emitter_ref(int index)
{
    std::string id = rood_particles::emitter_id(index);
    return id.empty() ? Ref() : Ref { rood_selection::KIND_EMITTER, id };
}
```

and after `rood_scene::volume_hidden_id`:

```cpp
bool rood_scene::emitter_hidden_id(const std::string& id)
{
    return hiding() && rood_selection::hidden(Ref { rood_selection::KIND_EMITTER, "emitter:" + id });
}
```

(The rest of the scene's emitter support is Task 10.)

- [ ] **Step 5: Emitters in the room state**

In `pc/src/environment.cpp`: add `#include "particles_internal.h"`; in `rood_env::reset()` after `rood_atmo::reset();` add `rood_particles::reset();`; in `apply_item` after the `rood_atmo::apply_item` line add

```cpp
    if (rood_particles::apply_item(it)) /* particle emitters (particles.cpp) */
        return true;
```

and in `add_items` after `rood_atmo::add_items(s);` add `rood_particles::add_items(s);`. (Undo snapshots, `rood_env::text`, then include emitters.)

In `pc/src/lighting_editor.cpp` `apply_state`, change `} else if (it.kind == "env" || it.kind == "fog" || it.kind == "post") {` to `} else if (it.kind == "env" || it.kind == "fog" || it.kind == "emitter" || it.kind == "post") {`, and in `layers_outliner_header` add `&& kv.second.kind != "emitter"` after `&& kv.second.kind != "fog"`.

- [ ] **Step 6: Settings, RPC routing, the build**

In `pc/src/project_settings.cpp`, after the `pc_fog_volume_*` externs add

```cpp
extern int pc_dust, pc_particles_lights, pc_particles_glow_shadows, pc_particles_cpu_rays; /* particles.cpp */
extern float pc_dust_low, pc_particles_glow;
```

and in `entries[]` after `I("fog_volume_shadows", pc_fog_volume_shadows),`:

```cpp
    /* Particles (particles.h, pc/docs/Particles.md): Dust (0 off, 1 low, 2 high), Low's share, the glows'
     * light on them, sources per mote, glow shadows, OpenGL's shadow rays a frame. */
    I("dust", pc_dust),
    F("dust_low", pc_dust_low),
    F("particles_glow", pc_particles_glow),
    I("particles_lights", pc_particles_lights),
    I("particles_glow_shadows", pc_particles_glow_shadows),
    I("particles_cpu_rays", pc_particles_cpu_rays),
```

In `pc/src/dev_rpc.c`, after the `environment` method's block add:

```c
    } else if (!strcmp(method, "particles")) {
        /* The port's particle system (particles.cpp): settings, emitters, motes. */
        extern int pc_particles_rpc(const char* params, char* out, int size) __attribute__((weak));
        if (!pc_particles_rpc) {
            fail(fd, id, -32000, "particles needs the editor (hardware renderer)");
            return;
        }
        static char args[LINE_MAX_BYTES];
        const char* end = params ? value_end(params) : NULL;
        if (params && end && (size_t)(end - params) < sizeof args) {
            memcpy(args, params, (size_t)(end - params));
            args[end - params] = 0;
        } else {
            strcpy(args, "{}");
        }
        if (pc_particles_rpc(args, result, sizeof result) < 0) {
            fail(fd, id, -32000, result);
            return;
        }
        reply(fd, id, result);
```

and in `edits()` add `|| !strcmp(method, "particles")` after `!strcmp(method, "environment")`.

In `pc/tools/build_boot.py`, after `root / "pc/src/fog_sim.cpp",` add:

```python
    root / "pc/src/particles.cpp",
    root / "pc/src/particle_assets.cpp",
```

- [ ] **Step 7: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 particles_smoke atmosphere_smoke editor_undo_smoke
```

Expected: build exit 0; `particles_smoke` passes its `items` section (the saved line exactly `emitter dusttest000001 asset=dust_motes pos=...,...,... seed=11 count=300 drift=0,-2,0`); the atmosphere and undo suites still pass (`3/3 passed`).

- [ ] **Step 8: Commit**

```bash
git add pc/src/particles.h pc/src/particles_internal.h pc/src/particles.cpp pc/src/environment.cpp pc/src/lighting_editor.cpp pc/src/project_settings.cpp pc/src/dev_rpc.c pc/src/editor_scene.h pc/src/editor_scene.cpp pc/tools/build_boot.py pc/tests/particles_smoke.py pc/tests/run.py
git commit -m "$(cat <<'EOF'
Particles: emitters in the room state (assets resolved, only overrides written), settings keys, RPC particles; particles_smoke (items round-trip)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: The frame, OpenGL's CPU path, RPC `particles {motes}`

**Files:**
- Modify: `pc/src/particles.cpp` (the frame, lighting, the CPU path, the GPU frame, the motes listing), `pc/src/ptgi.c` (`pc_gi_scene_ready` before `pc_gi_trace_cpu`), `pc/src/gl_present.c` (the CPU frame called each present; its pictures come in Task 8)
- Test: `pc/tests/particles_smoke.py` (section `motes`)

- [ ] **Step 1: Write the failing section**

In `pc/tests/particles_smoke.py`, before `# In order:` add:

```python
def motes(s, ident=EMITTER, limit=4096):
    """The last frame's motes of an emitter, in pages (the RPC's reply holds 256 KB)."""
    out = []
    while len(out) < limit:
        page = s.particles(motes={"emitter": ident, "offset": len(out), "max": min(512, limit - len(out))})["mote_list"]
        out += page
        if len(page) < 512:
            break
    return out


def check_motes(s):
    """Frozen time: the same motes each frame, inside their box; Dust sets how many; time moves them by their drift."""
    p = s.player
    centre = [round(p["x"]), round(p["y"] - 140), round(p["z"])]
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": centre, "size": [300, 200, 300], "count": 500,
                           "seed": 5, "lifetime": 30, "drift": [0, 0, 0], "curl": 0}], level=2, time=4.0)
    s.game.run(6)
    a = motes(s)
    s.game.run(6)
    b = motes(s)
    s.check(len(a) == 500 and [m["pos"] for m in a] == [m["pos"] for m in b],
            f"time frozen: the same {len(a)} motes frame after frame")
    inside = all(abs(m["pos"][k] - centre[k]) <= half + 0.05 for m in a for k, half in enumerate((150, 100, 150)))
    s.check(inside, "without motion every mote is in its box")
    s.check(all(m["size"] in (1, 2) for m in a) and {m["size"] for m in a} == {1, 2}, "sizes: 1 and 2 game pixels")
    for level, expect in ((0, 0), (1, 200), (2, 500)):
        s.particles(level=level)
        s.game.run(3)
        r = s.particles()
        drawn = next(e["drawn"] for e in r["emitters"] if e["id"] == EMITTER)
        s.check(drawn == expect and r["motes"] == expect, f"Dust {level}: {drawn} motes drawn ({expect})")
    s.particles(emitters=[{"id": EMITTER, "drift": [5, 0, 0]}], time=4.0)
    s.game.run(3)
    a = motes(s)
    s.particles(time=5.0)
    s.game.run(3)
    b = motes(s)
    moved = [bb["pos"][0] - aa["pos"][0] for aa, bb in zip(a, b) if aa["cycle"] == bb["cycle"]]
    s.check(len(moved) > 400 and all(abs(d - 5) < 0.02 for d in moved),
            f"a second later every mote that did not respawn drifted 5 units ({len(moved)} of {len(a)})")
    s.particles(emitters=[{"id": EMITTER, "remove": True}], level=0)
```

and add `check_motes` to `SECTIONS` after `check_items`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only motes
```

Expected: FAIL with `KeyError: 'mote_list'` (no listing yet).

- [ ] **Step 3: OpenGL's traced scene is up**

In `pc/src/ptgi.c`, before `int pc_gi_trace_cpu(`, add:

```c
/* Whether pc_gi_trace_cpu has a scene to trace (OpenGL: the path tracer
 * built it this present; Vulkan: never used for this). The particles' CPU
 * path traces its shadow rays only then (particles.cpp). */
int pc_gi_scene_ready(void)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return 0;
#endif
    return triangle_count > 0 && node_count > 0;
}
```

- [ ] **Step 4: The frame, lighting, the CPU path**

In `pc/src/particles.cpp`, after the anonymous namespace with `particles_time()` (before `/* ---- the room's emitters`), add:

```cpp
/* ---- the frame ---- */

namespace {
struct Frame {
    pc_particles_gpu gpu; /* the records (Vulkan: the pass's UBO) */
    std::vector<pc_light_gpu> lights;
    int light_count = 0, motes = 0, w = 0, h = 0, floor_n = 0;
    float t = 0;
    float heights[PC_PARTICLE_FLOOR_N * PC_PARTICLE_FLOOR_N];
    float floor_info[4] = { 0, 0, 1, 1 };
    std::vector<std::string> ids; /* the packed emitters' ids */
};
Frame frame;
struct Stats {
    const char* path = "off"; /* "traced", "unshadowed", "off" */
    int motes = 0;
    double cpu_ms = 0; /* smoothed */
} stats;

/* The share of an emitter's count the graphics settings draw. */
float share(const Emitter& e)
{
    if (e.quality == "dust")
        return pc_dust >= 2 ? 1.0f : pc_dust == 1 ? fminf(fmaxf(pc_dust_low, 0), 1) : 0.0f;
    return 1.0f;
}
void pack(const Emitter& e, int first, int count, const float lo[3], const float hi[3], pc_emitter_gpu& out)
{
    memset(&out, 0, sizeof out);
    float* v0 = out.v[0];
    float* v1 = out.v[1];
    memcpy(v0, e.pos, sizeof e.pos);
    v0[3] = e.yaw * DEG;
    memcpy(v1, e.size, sizeof e.size);
    v1[3] = PC_SPAWN_BOX;
    if (e.shape == 1) { /* room: its bounds as a box */
        for (int k = 0; k < 3; k++)
            v0[k] = (lo[k] + hi[k]) * 0.5f, v1[k] = fmaxf(hi[k] - lo[k], 1);
        v0[3] = 0;
    } else if (e.shape == 2 || e.shape == 3) {
        v1[0] = e.radius, v1[3] = e.shape == 2 ? PC_SPAWN_POINT : PC_SPAWN_DISC;
    } else if (e.shape == 4) {
        v1[3] = PC_SPAWN_SURFACE;
    }
    memcpy(out.v[2], e.drift, sizeof e.drift);
    out.v[2][3] = e.gravity;
    out.v[3][0] = e.curl, out.v[3][1] = e.curl_scale, out.v[3][2] = e.curl_speed, out.v[3][3] = e.drag;
    out.v[4][0] = e.lifetime, out.v[4][1] = e.jitter, out.v[4][2] = e.fade_in, out.v[4][3] = e.fade_out;
    memcpy(out.v[5], e.color, sizeof e.color);
    out.v[5][3] = e.brightness;
    out.v[6][0] = (float)e.pixel_min, out.v[6][1] = (float)e.pixel_max, out.v[6][2] = e.variation, out.v[6][3] = e.lit ? 1.0f : 0.0f;
    out.v[7][0] = (float)(e.seed & 0xffffff), out.v[7][1] = e.ambient, out.v[7][2] = (float)first, out.v[7][3] = (float)count;
}
/* This present's motes for a display w x h: false when there are none (Dust Off, nothing enabled, no camera). */
bool gather(Frame& f, int w, int h)
{
    rood_lights::track_room();
    f.motes = 0;
    f.ids.clear();
    float r[3][3], tr[3], hh, ox, oy, lo[3], hi[3];
    bool camera = w > 0 && h > 0 && pc_scene_camera_get(r, tr, &hh, &ox, &oy);
    pc_scene_bounds(lo, hi);
    memset(&f.gpu, 0, sizeof f.gpu);
    int n = 0, total = 0;
    for (Emitter& e : emitters) {
        e.drawn = 0;
        if (!camera || n >= PC_PARTICLE_EMITTERS || !e.enabled || rood_scene::emitter_hidden_id(e.id))
            continue;
        int count = std::min((int)lroundf((float)e.count * share(e)), PC_PARTICLES_MAX - total);
        if (count <= 0)
            continue;
        pack(e, total, count, lo, hi, f.gpu.emitters[n++]);
        f.ids.push_back(e.id);
        e.drawn = count;
        total += count;
    }
    if (!total)
        return false;
    f.motes = total, f.w = w, f.h = h, f.t = particles_time();
    pc_particles_gpu& g = f.gpu;
    g.info[0] = total, g.info[1] = n, g.info[2] = w, g.info[3] = h;
    for (int row = 0; row < 3; row++) {
        for (int k = 0; k < 3; k++)
            g.rotation[row][k] = r[row][k];
        g.rotation[row][3] = tr[row];
    }
    g.projection[0] = hh * (pc_gte_widescreen ? 0.75f : 1.0f), g.projection[1] = hh, g.projection[2] = ox, g.projection[3] = oy;
    pc_env_gpu env;
    static_assert(sizeof env == sizeof g.env, "pc_particles_gpu env: pc_env_gpu");
    pc_env_gpu_get(&env);
    memcpy(g.env, &env, sizeof env);
    float corner[2] = { 0, 0 }, cell[2] = { 1, 1 };
    f.floor_n = pc_scene_floor_grid(f.heights, corner, cell) ? PC_PARTICLE_FLOOR_N : 0;
    f.floor_info[0] = corner[0], f.floor_info[1] = corner[1];
    f.floor_info[2] = 1.0f / fmaxf(cell[0], 1e-3f), f.floor_info[3] = 1.0f / fmaxf(cell[1], 1e-3f);
    memcpy(g.floor_info, f.floor_info, sizeof f.floor_info);
    g.floor_n[0] = (float)f.floor_n;
    static_assert(sizeof g.floor == sizeof f.heights, "the floor grid's size");
    memcpy(g.floor, f.heights, sizeof f.heights);
    f.lights.resize(PC_LIGHTS_MAX);
    f.light_count = pc_lights_list(f.lights.data(), PC_LIGHTS_MAX);
    float shapes[PC_PARTICLE_GLOWS][4];
    int glows = pc_fog_glows(g.glow_pos, g.glow_col, shapes, PC_PARTICLE_GLOWS);
    g.info2[0] = f.light_count, g.info2[1] = glows, g.info2[2] = std::max(1, std::min(4, pc_particles_lights)), g.info2[3] = 0;
    g.time[0] = f.t, g.time[1] = pc_particles_glow, g.time[2] = pc_particles_debug ? 1.0f : 0.0f;
    g.time[3] = pc_particles_glow_shadows ? 1.0f : 0.0f;
    return true;
}

float lum(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
bool blocked(const float o[3], const float d[3], float t)
{
    float hit, centre[3];
    return t > 0 && pc_gi_trace_cpu && pc_gi_trace_cpu(o, d, t, &hit, centre) >= 0;
}
/* One source of a mote's light (particles_main.glsl mote_light the same). */
struct Source {
    float rgb[3], dir[3], reach, weight;
    bool casts;
};
/* The light reaching p: its strongest sources (lights, glows) with a shadow
 * ray each when `trace`, and the sun; `open` the same without shadows. */
void light_at(const Frame& f, const float p[3], bool trace, float out[3], float open[3])
{
    const pc_particles_gpu& g = f.gpu;
    int keep = g.info2[2], kept = 0;
    Source best[4];
    auto offer = [&](const Source& s) {
        int at = kept < keep ? kept++ : keep;
        if (at == keep && !(s.weight > best[keep - 1].weight))
            return;
        if (at == keep)
            at = keep - 1;
        while (at > 0 && best[at - 1].weight < s.weight)
            best[at] = best[at - 1], --at;
        best[at] = s;
    };
    for (int i = 0; i < f.light_count; i++) {
        const pc_light_gpu& L = f.lights[i];
        float c[3] = { L.pos[0], L.pos[1], L.pos[2] };
        if (L.col[3] > 1.5f) { /* a rect: its nearest point */
            float rel[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
            for (const float* ax : { L.right, L.up }) {
                float a2 = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2];
                float t = a2 > 1e-6f ? fminf(fmaxf((rel[0] * ax[0] + rel[1] * ax[1] + rel[2] * ax[2]) / a2, -1.0f), 1.0f) : 0;
                for (int k = 0; k < 3; k++)
                    c[k] += ax[k] * t;
            }
        }
        float to[3] = { c[0] - p[0], c[1] - p[1], c[2] - p[2] };
        float d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        float l[3] = { to[0] / fmaxf(d, 1e-3f), to[1] / fmaxf(d, 1e-3f), to[2] / fmaxf(d, 1e-3f) };
        float x = fminf(d / fmaxf(L.pos[3], 1e-3f), 1.0f), a = (1 - x * x) * (1 - x * x);
        a *= pc_light_shape(L.col[3], L.pos, L.dir, L.right, L.up, p, l);
        if (!(a > 0))
            continue;
        Source s = { { L.col[0] * a, L.col[1] * a, L.col[2] * a }, { l[0], l[1], l[2] }, d - 2.0f, 0, L.info[0] != 0 };
        s.weight = lum(s.rgb);
        offer(s);
    }
    for (int j = 0; j < g.info2[1]; j++) {
        const float* gp = g.glow_pos[j];
        float to[3] = { gp[0] - p[0], gp[1] - p[1], gp[2] - p[2] };
        float d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
        float x = fminf(d / fmaxf(gp[3], 1e-3f), 1.0f), a = (1 - x * x) * (1 - x * x) * g.time[1];
        if (!(a > 0))
            continue;
        Source s = { { g.glow_col[j][0] * a, g.glow_col[j][1] * a, g.glow_col[j][2] * a },
            { to[0] / fmaxf(d, 1e-3f), to[1] / fmaxf(d, 1e-3f), to[2] / fmaxf(d, 1e-3f) },
            d - fmaxf(fabsf(g.glow_col[j][3]), 2.0f), 0, g.time[3] > 0.5f };
        s.weight = lum(s.rgb);
        offer(s);
    }
    for (int k = 0; k < 3; k++)
        out[k] = open[k] = 0;
    for (int i = 0; i < kept; i++) {
        bool lit = !(trace && best[i].casts && blocked(p, best[i].dir, best[i].reach));
        for (int k = 0; k < 3; k++)
            open[k] += best[i].rgb[k], out[k] += lit ? best[i].rgb[k] : 0;
    }
    const float* sun = g.env[1];
    if (sun[0] + sun[1] + sun[2] > 0) { /* environment.glsl env_sun_on */
        bool lit = !(trace && sun[3] > 0.5f && blocked(p, g.env[0], 30000.0f)); /* ENV_SUN_DISTANCE */
        for (int k = 0; k < 3; k++)
            open[k] += sun[k], out[k] += lit ? sun[k] : 0;
    }
}
/* A mote's colour from the light reaching it (rgb), its light's luminance (a). */
void colour_of(const pc_emitter_gpu& e, const pc_mote& m, const float light[3], bool debug, float out[4])
{
    float l[3] = { light[0], light[1], light[2] };
    if (debug)
        l[0] = l[1] = l[2] = 1;
    if (e.v[6][3] > 0.5f) /* lit: its ambient everywhere */
        for (int k = 0; k < 3; k++)
            l[k] += e.v[7][1];
    for (int k = 0; k < 3; k++)
        out[k] = l[k] * e.v[5][k] * e.v[5][3] * m.fade * m.gain;
    out[3] = lum(l);
}
/* A coloured mote into the key image. */
void splat(std::vector<uint32_t>& keys, int w, int h, const pc_particles_gpu& g, const pc_mote& m, int index)
{
    float s[3];
    if (!pc_particle_project(g.rotation, g.projection, m.pos, s))
        return;
    uint32_t key = pc_particle_key(s[2], index);
    int ax = pc_particle_anchor(s[0], m.size), ay = pc_particle_anchor(s[1], m.size);
    for (int dy = 0; dy < m.size; dy++)
        for (int dx = 0; dx < m.size; dx++) {
            int x = ax + dx, y = ay + dy;
            if (x >= 0 && y >= 0 && x < w && y < h) {
                uint32_t& k = keys[(size_t)y * w + x];
                k = key < k ? key : k;
            }
        }
}
/* OpenGL's buffers and its per-mote light cache (refreshed round-robin within the ray budget). */
std::vector<uint32_t> cpu_keys;
std::vector<float> cpu_colours, cached;
std::vector<int> cached_cycle;
int cursor;
} // namespace

extern "C" int pc_particles_cpu_frame(int w, int h, const uint32_t** keys, const float** colours, int* count)
{
    auto start = std::chrono::steady_clock::now();
    if (!gather(frame, w, h)) {
        stats.motes = 0, stats.path = "off";
        return 0;
    }
    const pc_particles_gpu& g = frame.gpu;
    bool trace = pc_gi_scene_ready && pc_gi_scene_ready() && pc_gi_trace_cpu;
    int n = frame.motes;
    cpu_keys.assign((size_t)w * h, PC_PARTICLE_NONE);
    cpu_colours.assign((size_t)n * 4, 0.0f);
    if ((int)cached_cycle.size() != n)
        cached.assign((size_t)n * 3, 0.0f), cached_cycle.assign(n, INT_MIN), cursor = 0;
    /* each mote's light costs one ray per source and one for the sun */
    int per = trace ? std::max(1, std::min(n, pc_particles_cpu_rays / (g.info2[2] + 1))) : n;
    int from = cursor % n;
    bool debug = g.time[2] > 0.5f;
    for (int e = 0; e < g.info[1]; e++) {
        const pc_emitter_gpu& em = g.emitters[e];
        int first = (int)em.v[7][2], motes = (int)em.v[7][3];
        for (int i = 0; i < motes; i++) {
            int gi = first + i;
            pc_mote m;
            pc_particle_mote(&em, i, frame.t, frame.floor_n ? frame.heights : nullptr, frame.floor_info, frame.floor_n, &m);
            float* light = &cached[(size_t)gi * 3];
            if ((gi - from + n) % n < per) { /* due */
                float open[3];
                light_at(frame, m.pos, trace, light, open);
                cached_cycle[gi] = m.cycle;
            } else if (cached_cycle[gi] != m.cycle) { /* respawned: dark until it is lit at its new place */
                light[0] = light[1] = light[2] = 0;
                cached_cycle[gi] = m.cycle;
            }
            float* c = &cpu_colours[(size_t)gi * 4];
            colour_of(em, m, light, debug, c);
            if (c[0] + c[1] + c[2] > 1e-4f)
                splat(cpu_keys, w, h, g, m, gi);
        }
    }
    cursor = (from + per) % n;
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stats.cpu_ms = stats.cpu_ms > 0 ? stats.cpu_ms * 0.9 + ms * 0.1 : ms;
    stats.motes = n, stats.path = trace ? "traced" : "unshadowed";
    *keys = cpu_keys.data(), *colours = cpu_colours.data(), *count = n;
    return 1;
}
extern "C" int pc_particles_gpu_frame(int w, int h, pc_particles_gpu* out, pc_light_gpu* lights, int max, int* light_count)
{
    if (!gather(frame, w, h)) {
        stats.motes = 0, stats.path = "off";
        return 0;
    }
    *out = frame.gpu;
    int n = std::min(frame.light_count, max);
    memcpy(lights, frame.lights.data(), (size_t)n * sizeof(pc_light_gpu));
    *light_count = n;
    out->info2[0] = n;
    stats.motes = frame.motes;
    return frame.motes;
}
extern "C" void pc_particles_gpu_traced(int traced) { stats.path = traced ? "traced" : "unshadowed"; }
```

Delete the RPC section's own `struct Stats { ... } stats;` (the frame's above replaces it).

- [ ] **Step 5: The motes listing in the RPC**

In `pc_particles_rpc`, before `if (changed) {`, add:

```cpp
    if (const Json* mq = params.get("motes")) {
        /* The last frame's motes (the time frozen: exactly what it drew): where each is, its
         * game pixels, its light as drawn (OpenGL: the CPU path's; Vulkan: read back) and
         * without shadows (the CPU's), and whether it owns its anchor pixel. */
        const Json* who = mq->get("emitter");
        const Json* lim = mq->get("max");
        const Json* off = mq->get("offset");
        int limit = std::min(lim ? (int)lim->number : 256, 600), skip = off ? (int)off->number : 0; /* 600: the reply's 256 KB */
        std::vector<uint32_t> keys;
        std::vector<float> colours;
        bool vulkan = &pc_vk_active && pc_vk_active;
        if (frame.motes > 0) {
            if (vulkan && pc_vk_particles_read) {
                keys.resize((size_t)frame.w * frame.h), colours.resize((size_t)frame.motes * 4);
                if (!pc_vk_particles_read(keys.data(), frame.w, frame.h, colours.data(), frame.motes))
                    keys.clear(), colours.clear();
            } else if (!vulkan && cpu_keys.size() == (size_t)frame.w * frame.h) {
                keys = cpu_keys, colours = cpu_colours;
            }
        }
        std::string list = ",\"mote_list\":[";
        int listed = 0;
        const pc_particles_gpu& g = frame.gpu;
        for (int e = 0; e < g.info[1] && frame.motes > 0; e++) {
            if (who && bare(who->string) != frame.ids[e])
                continue;
            const pc_emitter_gpu& em = g.emitters[e];
            int first = (int)em.v[7][2], count = (int)em.v[7][3];
            for (int i = 0; i < count && listed < limit; i++) {
                if (skip > 0) {
                    --skip;
                    continue;
                }
                int gi = first + i;
                pc_mote m;
                pc_particle_mote(&em, i, frame.t, frame.floor_n ? frame.heights : nullptr, frame.floor_info, frame.floor_n, &m);
                float lit[3], open[3], s[3] = { -1, -1, 0 };
                light_at(frame, m.pos, false, lit, open);
                bool seen = pc_particle_project(g.rotation, g.projection, m.pos, s);
                int ax = seen ? pc_particle_anchor(s[0], m.size) : -1, ay = seen ? pc_particle_anchor(s[1], m.size) : -1;
                bool won = seen && ax >= 0 && ay >= 0 && ax < frame.w && ay < frame.h && !keys.empty()
                    && keys[(size_t)ay * frame.w + ax] == pc_particle_key(s[2], gi);
                const float* c = colours.empty() ? nullptr : &colours[(size_t)gi * 4];
                char text[512];
                snprintf(text, sizeof text,
                    "%s{\"g\":%d,\"emitter\":\"%s\",\"i\":%d,\"cycle\":%d,\"pos\":[%.3f,%.3f,%.3f],\"screen\":[%.3f,%.3f],"
                    "\"cell\":[%d,%d],\"size\":%d,\"sz\":%.2f,\"fade\":%.4f,\"colour\":[%.5f,%.5f,%.5f],\"light\":%.5f,"
                    "\"open\":%.5f,\"won\":%s}",
                    listed ? "," : "", gi, frame.ids[e].c_str(), i, m.cycle, m.pos[0], m.pos[1], m.pos[2], s[0], s[1], ax, ay,
                    m.size, s[2], m.fade, c ? c[0] : 0, c ? c[1] : 0, c ? c[2] : 0, c ? c[3] : 0, lum(open),
                    won ? "true" : "false");
                list += text;
                ++listed;
            }
        }
        extra += list + "]";
    }
    extra += ",\"display\":[" + std::to_string(frame.w) + "," + std::to_string(frame.h) + "]";
    if (&pc_gl_view)
        extra += ",\"view\":[" + std::to_string(pc_gl_view[0]) + "," + std::to_string(pc_gl_view[1]) + ","
            + std::to_string(pc_gl_view[2]) + "," + std::to_string(pc_gl_view[3]) + "]";
```

and move `std::string extra;` up before the `motes` block (from the `add` block). The reply's buffer (`result` in `dev_rpc.c`) is `LINE_MAX_BYTES`, 256 KB, so the listing comes in pages of at most 600 motes (about 330 bytes each): `offset`, `max` (the test's `motes()` pages through them).

- [ ] **Step 6: OpenGL runs the CPU frame each present**

In `pc/src/gl_present.c` add `#include "particles.h"` after `#include "emissive.h"`, and in `pc_gl_present`, right after `GLuint gi = pc_gl_show_depth ? 0 : pc_gi_update(x, y, w, h);` (the traced scene is current then), add:

```c
            /* The particles (particles.cpp): evaluated, lit and splatted on the CPU after the
             * path tracer built its scene (their shadow rays use it); composited below. */
            const uint32_t* particle_keys = NULL;
            const float* particle_colours = NULL;
            int particle_count = 0;
            int particles_on = !pc_gl_show_depth
                && pc_particles_cpu_frame(w, h, &particle_keys, &particle_colours, &particle_count);
            (void)particles_on; /* drawn from Task 8 on */
```

- [ ] **Step 7: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only items,motes
```

Expected: `PASS: particles` (500 motes frame after frame, inside the box, Dust 0 / 1 / 2: 0 / 200 / 500 drawn, a second's drift of 5 units).

- [ ] **Step 8: Commit**

```bash
git add pc/src/particles.cpp pc/src/ptgi.c pc/src/gl_present.c pc/tests/particles_smoke.py
git commit -m "$(cat <<'EOF'
Particles: the frame (emitters packed, camera, lights, glows, sun, floor grid), the mote's light (strongest sources and the sun, shadow rays), OpenGL's CPU path with a ray budget; RPC motes listing

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: The composite and the picture on OpenGL

**Files:**
- Create: `pc/shaders/vulkan/particles.glsl`
- Modify: `pc/tools/gl_shaders.py` (`FRAGMENTS`), `pc/src/gl_present.c` (uniforms; the shader's declarations after the fog volumes' and its two calls; the textures and uniforms each present)
- Test: `pc/tests/particles_smoke.py` (section `picture`)

- [ ] **Step 1: Write the failing section**

In `pc/tests/particles_smoke.py`, before `# In order:` add:

```python
def lum(image):
    return image @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def view_cells(r):
    """The view's window rectangle and, per window column and row, its game pixel (the composite's
    floor(uv x display))."""
    vx, vy, vw, vh = r["view"]
    dw, dh = r["display"]
    xs = np.floor((np.arange(vw) + 0.5) / vw * dw).astype(int)
    ys = np.floor((np.arange(vh) + 0.5) / vh * dh).astype(int)
    return (vx, vy, vw, vh), xs, ys


def walls(s):
    """Wall points in the game's view (editor_select pick): world point and the normal toward Ashley."""
    g, p = s.game, s.player
    feet = np.array([p["x"], p["y"], p["z"]], float)
    g.call("editor_ui", visible=True)
    g.run(6)
    found = []
    for v in (0.4, 0.3, 0.5, 0.2):
        for u in (0.5, 0.35, 0.65, 0.2, 0.8):
            hit = g.call("editor_select", action="pick", u=u, v=v)
            if not hit.get("world"):
                continue
            w, n = np.array(hit["world"], float), np.array(hit["normal"], float)
            if abs(n[1]) > 0.25 or not 150 < np.linalg.norm((w - feet)[[0, 2]]) < 900:
                continue
            n[1] = 0
            n /= max(np.linalg.norm(n), 1e-6)
            found.append((w, n if np.dot(feet - w, n) > 0 else -n))
    g.call("editor_ui", visible=False)
    g.run(6)
    return found


def add_light(s, pos, radius, enabled=True):
    g = s.game
    ident = g.call("lights", action="add", position=[float(x) for x in pos])["lights"][-1]["id"]
    g.call("editor_schema", action="set", id=ident, values={"type": "point"})
    g.call("editor_schema", action="set", id=ident, values={"radius": radius, "intensity": 4, "color": [1, 1, 1],
                                                            "shadows": True, "enabled": enabled})
    return ident


def check_picture(s):
    """Dust Off is the picture without motes; a wall leaves motes dark (none shows); lit motes are square
    game pixels of 1 or 2, uniform, with nothing around them."""
    g = s.game
    g.call("post", layer="room", clear="all", snap=True)
    g.call("post", layer="gameplay", clear="all", snap=True)
    g.call("graphics", haze="off")
    g.call("pause", paused=True)
    s.particles(level=0, time=10.0)

    def frame(name):
        return s.shot(name, 40)  # 40 presents: OpenGL's ray budget relights every mote several times

    def noise_of(a, b):
        d = np.abs(a - b).max()
        return 0.0 if d == 0 else d * 2 + 2 / 255

    candidates = walls(s)
    # A closed wall: in the room every mote it shadows is dark, behind it every one lit. A broken
    # shadow ray fails one side or the other, so trying up to four walls cannot pass a wrong renderer.
    if not s.check(bool(candidates), f"a wall in the game's view ({len(candidates)} found)"):
        return
    for wall, n in candidates[:4]:
        yaw = math.degrees(math.atan2(n[0], n[2]))
        common = {"asset": "dust_motes", "size": [240, 180, 100], "yaw": round(yaw, 2), "count": 1200, "lifetime": 1000,
                  "lifetime_jitter": 0, "drift": [0, 0, 0], "curl": 0, "fade_in": 0, "fade_out": 0, "variation": 0,
                  "brightness": 1.5, "pixel_min": 1, "pixel_max": 2}
        s.particles(emitters=[dict(common, id=EMITTER, pos=[round(float(x), 1) for x in wall + n * 70], seed=3),
                              dict(common, id=FAR, pos=[round(float(x), 1) for x in wall - n * 70], seed=4)])
        behind = add_light(s, wall - n * 120, 900)
        inside = add_light(s, wall + n * 260, 230, enabled=False)
        g.run(120)  # the new light's indirect light settles (path tracing accumulates)
        # Dust Off: the picture as without emitters (they exist, nothing is drawn)
        off = frame("picture-off")
        off2 = frame("picture-off2")
        noise = noise_of(off, off2)
        s.particles(level=2)
        on = frame("picture-wall")
        room, far = motes(s, EMITTER), motes(s, FAR)
        shadowed = [m for m in room if m["open"] > 0.02]
        dark = [m for m in shadowed if m["light"] < 0.05 * m["open"]]
        reached = [m for m in far if m["open"] > 0.02]
        bright = [m for m in reached if m["light"] > 0.8 * m["open"]]
        ok_geometry = len(shadowed) > 100 and len(dark) >= 0.9 * len(shadowed) and len(bright) >= 0.9 * max(len(reached), 1)
        if not ok_geometry:  # not a closed wall here (a doorway, a thin edge): the next
            print(f"info: wall at {wall.round()} leaves {len(dark)}/{len(shadowed)} dark, {len(bright)}/{len(reached)} "
                  "lit behind it; trying the next")
            s.particles(level=0, emitters=[{"id": EMITTER, "remove": True}, {"id": FAR, "remove": True}])
            for ident in (behind, inside):
                g.call("editor_select", action="set", ids=[ident])
                g.call("editor_select", action="delete")
            continue
        s.check(True, f"the wall shadows the motes in the room ({len(dark)} of {len(shadowed)} the light behind it "
                      f"reaches stay dark); behind it they are lit ({len(bright)} of {len(reached)})")
        break
    else:
        s.check(False, "a closed wall to test shadowed motes")
        return
    s.check(np.abs(on - off).max() <= noise,
            f"no mote shows: those in the wall's shadow are dark, those behind it are hidden by it "
            f"(largest change {np.abs(on - off).max() * 255:.1f}/255, redraw noise {noise * 255:.1f})")
    s.particles(level=0)
    s.check(np.abs(frame("picture-off-again") - off).max() <= noise, "Dust Off again: the same picture")
    s.particles(emitters=[{"id": EMITTER, "enabled": False}, {"id": FAR, "enabled": False}], level=2)
    s.check(np.abs(frame("picture-disabled") - off).max() <= noise,
            "Dust High with the emitters disabled: the same picture (nothing evaluated or drawn)")
    s.particles(emitters=[{"id": EMITTER, "enabled": True}, {"id": FAR, "enabled": True}], level=0)
    # A light in the room: the motes it reaches glint.
    g.call("editor_schema", action="set", id=inside, values={"enabled": True})
    g.run(120)
    lit_off = frame("picture-lit-off")
    lit_noise = max(noise, noise_of(lit_off, frame("picture-lit-off2")))
    s.particles(level=2)
    lit_on = frame("picture-lit")
    r = s.particles()
    (vx, vy, vw, vh), xs, ys = view_cells(r)
    diff = lum(np.abs(lit_on - lit_off))[vy:vy + vh, vx:vx + vw]
    room, far = motes(s, EMITTER), motes(s, FAR)
    # Every game pixel a drawn (coloured) mote covers: where the picture may change.
    covered = np.zeros((r["display"][1], r["display"][0]), bool)
    for m in room + far:
        if sum(m["colour"]) > 1e-4 and m["cell"][0] > -m["size"]:
            cx, cy, k = m["cell"][0], m["cell"][1], m["size"]
            covered[max(cy, 0):max(cy + k, 0), max(cx, 0):max(cx + k, 0)] = True
    cells = covered[np.ix_(ys, xs)]
    s.check(diff[~cells].max() <= lit_noise + 1e-6,
            f"nothing changes outside the motes' game pixels (largest {diff[~cells].max() * 255:.1f}/255)")
    glints = [m for m in room if m["won"] and m["light"] > 0.1 and m["light"] > 0.5 * m["open"]]
    inner_tol = 1 / 255 if not s.vulkan else 3 / 255
    shown = alone = square = 0
    for m in glints:
        cx, cy, k = m["cell"][0], m["cell"][1], m["size"]
        mine = np.zeros_like(cells)
        mine[np.ix_((ys >= cy) & (ys < cy + k), (xs >= cx) & (xs < cx + k))] = True
        block = diff[mine]
        if not block.size or block.mean() <= 10 / 255:
            continue
        shown += 1
        if covered[max(cy - 1, 0):cy + k + 1, max(cx - 1, 0):cx + k + 1].sum() != k * k:
            continue  # another mote beside it
        alone += 1
        ring = np.zeros_like(cells)
        ring[np.ix_((ys >= cy - 1) & (ys < cy + k + 1), (xs >= cx - 1) & (xs < cx + k + 1))] = True
        ring &= ~mine
        if k in (1, 2) and block.max() - block.min() <= inner_tol and diff[ring].max() <= lit_noise + 1e-6:
            square += 1
    s.check(len(glints) >= 20 and shown >= 0.8 * len(glints),
            f"the motes the room's light reaches glint ({shown} of {len(glints)} lit motes raise their game pixels "
            "by over 10/255)")
    s.check(alone >= max(1, shown // 2) and square == alone,
            f"each a uniform square of its 1 or 2 game pixels with nothing around it, nearest ({square} of {alone} "
            "isolated motes)")
    for ident in (behind, inside):
        g.call("editor_select", action="set", ids=[ident])
        g.call("editor_select", action="delete")
    s.particles(level=0, emitters=[{"id": EMITTER, "remove": True}, {"id": FAR, "remove": True}])
    g.call("pause", paused=False)
```

and add `check_picture` to `SECTIONS` after `check_motes`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only picture
```

Expected: the wall checks pass (the CPU path lights the motes) but `FAIL: the motes the room's light reaches glint (0 of N ...)`: nothing is composited yet.

- [ ] **Step 3: The composite**

Create `pc/shaders/vulkan/particles.glsl`:

```glsl
// The particles' composite (particles.h; pc/docs/Particles.md), shared by
// scaled.frag and OpenGL's present shader (gl_shaders.py PC_GLSL_PARTICLES):
// the mote that owns this pixel's game pixel (the key image: one texel per
// game pixel of the display, depth << 16 | mote, 0xFFFFFFFF none), its
// colour (one texel per mote, 256 wide) when it is in front of the scene
// there (d: the scene's depth, SZ / 65536; UI pixels have none). Whole game
// pixels, fetched, never filtered.
#ifndef PARTICLES_GLSL
#define PARTICLES_GLSL
vec3 particles_at(usampler2D keys, sampler2D colours, ivec2 display, vec2 uv, float d) {
    ivec2 cell = clamp(ivec2(floor(uv * vec2(display))), ivec2(0), display - 1);
    uint k = texelFetch(keys, cell, 0).r;
    float z = d * 65536.0;
    if (k == 0xffffffffu || z < 0.5 || (d < 1.0 && float(k >> 16) >= z)) return vec3(0.0);
    int i = int(k & 0xffffu);
    return max(texelFetch(colours, ivec2(i & 255, i >> 8), 0).rgb, vec3(0.0));
}
#endif
```

In `pc/tools/gl_shaders.py` `FRAGMENTS`, after `"fog_volume.glsl",` add:

```python
             # The port's particles' composite (gl_present.c's present).
             "particles.glsl",
```

- [ ] **Step 4: OpenGL draws it**

In `pc/src/gl_present.c`:

1. Declarations: change `static GLint u_medium, u_medium_tex, u_fogvol, u_fogvol_on;` to `static GLint u_medium, u_medium_tex, u_fogvol, u_fogvol_on, u_particle_keys, u_particle_colours, u_particles_on;`.
2. The shader: after `"#define FOGVOL(i) fogvol_[i]\n" PC_GLSL_FOG_VOLUME` add

```c
    /* The port's particles (particles.h): the key image (a game pixel per texel) and their colours. */
    "uniform usampler2D particle_keys;\n"
    "uniform sampler2D particle_colours;\n"
    "uniform ivec4 particles_on;\n" /* x on, y z the display's size in game pixels */
    PC_GLSL_PARTICLES
```

3. In `main`: change `"    bool haze_done = false, fogvol_done = false, lit_done = false;\n"` to `"    bool haze_done = false, fogvol_done = false, lit_done = false, particles_done = false;\n"`; before `"            if (fogvol_on.x != 0) { c = fogvol_apply(c, q, d); fogvol_done = true; }\n"` add

```c
    "            if (particles_on.x != 0 && view_mode == 0) {\n" /* the motes in front of it, before the fog and haze */
    "                c += particles_at(particle_keys, particle_colours, particles_on.yz, uv, d);\n"
    "                particles_done = true;\n"
    "            }\n"
```

and before `"    if (fogvol_on.x != 0 && !fogvol_done && !ui && view_mode != 1) c = fogvol_apply(c, texel(uv), d);\n"` add

```c
    "    if (particles_on.x != 0 && !particles_done && view_mode == 0)\n" /* unlit pixels, and where nothing is drawn */
    "        c += particles_at(particle_keys, particle_colours, particles_on.yz, uv, d);\n"
```

4. Uniform locations, after `u_fogvol_on = gl_GetUniformLocation(scaled_program, "fogvol_on");`:

```c
    u_particle_keys = gl_GetUniformLocation(scaled_program, "particle_keys");
    u_particle_colours = gl_GetUniformLocation(scaled_program, "particle_colours");
    u_particles_on = gl_GetUniformLocation(scaled_program, "particles_on");
```

5. Before `static GLint u_light_right, u_light_up, ...` add the textures:

```c
/* The particles' key image (R32UI, a game pixel per texel) and colours (RGBA32F, 256 x 64)
 * for the present (particles.h pc_particles_cpu_frame), on units 18 and 19. Always bound,
 * so the usampler never shares a unit with a float sampler. */
static void particles_bind(int on, const uint32_t* keys, int w, int h, const float* colours, int count)
{
    static GLuint key_tex, colour_tex;
    static int key_w, key_h;
    if (!key_tex) {
        gl_GenTextures(1, &key_tex);
        gl_GenTextures(1, &colour_tex);
        static const uint32_t none = PC_PARTICLE_NONE;
        gl_BindTexture(GL_TEXTURE_2D, key_tex);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 1, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &none);
        key_w = key_h = 1;
        gl_BindTexture(GL_TEXTURE_2D, colour_tex);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, PC_PARTICLE_COLOUR_W, PC_PARTICLES_MAX / PC_PARTICLE_COLOUR_W, 0,
            GL_RGBA, GL_FLOAT, NULL);
    }
    gl_ActiveTexture(GL_TEXTURE18);
    gl_BindTexture(GL_TEXTURE_2D, key_tex);
    if (on) {
        gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (key_w != w || key_h != h) {
            gl_TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, keys);
            key_w = w, key_h = h;
        } else {
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, keys);
        }
    }
    gl_ActiveTexture(GL_TEXTURE19);
    gl_BindTexture(GL_TEXTURE_2D, colour_tex);
    if (on && count > 0) {
        int rows = (count + PC_PARTICLE_COLOUR_W - 1) / PC_PARTICLE_COLOUR_W, full = count / PC_PARTICLE_COLOUR_W;
        if (full)
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, PC_PARTICLE_COLOUR_W, full, GL_RGBA, GL_FLOAT, colours);
        if (rows > full) /* the last row's motes (the rest of it is never fetched) */
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, full, count - full * PC_PARTICLE_COLOUR_W, 1, GL_RGBA, GL_FLOAT,
                colours + (size_t)full * PC_PARTICLE_COLOUR_W * 4);
    }
    gl_ActiveTexture(GL_TEXTURE0);
}
```

6. In `pc_gl_present`, replace Task 7's `(void)particles_on; /* drawn from Task 8 on */` with nothing, and after the block that binds `u_scaled_depth` (`gl_Uniform1i(u_scaled_depth, 1);`) add:

```c
            particles_bind(particles_on, particle_keys, w, h, particle_colours, particle_count);
            gl_Uniform1i(u_particle_keys, 18);
            gl_Uniform1i(u_particle_colours, 19);
            gl_Uniform4i(u_particles_on, particles_on, w, h, 0);
```

(`particles_on` and friends were declared inside the same `if (target)` block in Task 7, so they are in scope here.)

- [ ] **Step 5: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only items,motes,picture
```

Expected: `PASS: particles`. Look at `picture-lit.png` in the test's output directory (run directly: `pc/build/boot/particles_smoke/`; under run.py: below `pc/build/tests/particles_smoke/`) with the Read tool: glints by the room's light, none in the wall's shadow. If a check of the picture fails, debug with superpowers:systematic-debugging; never loosen a threshold without a measured reason written into the test's comment.

- [ ] **Step 6: Commit**

```bash
git add pc/shaders/vulkan/particles.glsl pc/tools/gl_shaders.py pc/src/gl_present.c pc/tests/particles_smoke.py
git commit -m "$(cat <<'EOF'
Particles on OpenGL: the composite (the mote owning each game pixel, in front of the scene, nearest; before the fog and haze); picture tests (Dust Off identical, shadowed motes never show, lit ones uniform 1-2 pixel squares)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: The Vulkan pass

**Files:**
- Create: `pc/shaders/vulkan/particles_sim.glsl`, `pc/shaders/vulkan/particles_main.glsl`, `pc/shaders/vulkan/particles.comp`, `pc/shaders/vulkan/particles_rt.comp`, `pc/src/vk_particles.inc`
- Modify: `pc/src/vk_rt.inc` (include it after `vk_fogvol.inc`), `pc/src/vk_present.c` (`present_ubo`; `scaled_types`; the pass after the fog volumes'; bindings 21, 22), `pc/shaders/vulkan/scaled.frag` (the UBO's `particles_`, bindings, the composite), `pc/src/gl_present.c` (no motes in the menu's model view), `pc/tests/particles_smoke.py` (Vulkan tolerances)
- Test: `pc/tests/particles_smoke.py` on Vulkan

- [ ] **Step 1: Run the test on Vulkan and see it fail**

In `pc/tests/particles_smoke.py` `check_picture`, after `cells = covered[np.ix_(ys, xs)]` insert the Vulkan tolerance for a mote's pixel landing one game pixel over (the GPU's float maths may round a projected position on the other side of a pixel's edge than the CPU's listing):

```python
    if s.vulkan:  # the GPU's own rounding: a game pixel's margin around each mote
        grown = covered.copy()
        grown[1:, :] |= covered[:-1, :]
        grown[:-1, :] |= covered[1:, :]
        grown[:, 1:] |= grown[:, :-1].copy()
        grown[:, :-1] |= grown[:, 1:].copy()
        cells = grown[np.ix_(ys, xs)]
```

and in `pc_particles_rpc`'s listing compare the owner by index only (the GPU's SZ may differ by one unit): change `&& keys[(size_t)ay * frame.w + ax] == pc_particle_key(s[2], gi);` to `&& (keys[(size_t)ay * frame.w + ax] & 0xffffu) == (uint32_t)gi;`.

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan particles_smoke
```

Expected: `items` passes; `motes` fails (`Dust 2: 0 motes drawn`: nothing gathers on Vulkan yet).

- [ ] **Step 2: The mote in GLSL**

Create `pc/shaders/vulkan/particles_sim.glsl` (the GLSL copy of `particle_math.h`; keep the two the same):

```glsl
// A mote's closed form (pc/src/particle_math.h, its C original: keep the two
// the same). The includer defines PARTICLE_EMITTER(e, k) (vec4 k of emitter
// e's record, particle_math.h pc_emitter_gpu), PARTICLE_FLOOR(i) (vec4 i of
// the floor grid's heights), PARTICLE_FLOOR_INFO and PARTICLE_FLOOR_N.
#ifndef PARTICLES_SIM_GLSL
#define PARTICLES_SIM_GLSL
const int SPAWN_BOX = 0, SPAWN_POINT = 1, SPAWN_DISC = 2, SPAWN_SURFACE = 3;
uint particle_hash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float particle_unit(inout uint s) { s = particle_hash(s + 0x9e3779b9u); return float(s >> 8) * (1.0 / 16777216.0); }
float particle_smooth(float x) { x = clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }
float particle_floor_cell(int i, int j, int n) {
    int k = clamp(j, 0, n - 1) * n + clamp(i, 0, n - 1);
    return PARTICLE_FLOOR(k >> 2)[k & 3];
}
float particle_floor(vec2 xz) {
    int n = PARTICLE_FLOOR_N;
    vec2 c = (xz - PARTICLE_FLOOR_INFO.xy) * PARTICLE_FLOOR_INFO.zw - 0.5;
    ivec2 i = ivec2(floor(c));
    vec2 f = c - vec2(i);
    float a = mix(particle_floor_cell(i.x, i.y, n), particle_floor_cell(i.x + 1, i.y, n), f.x);
    float b = mix(particle_floor_cell(i.x, i.y + 1, n), particle_floor_cell(i.x + 1, i.y + 1, n), f.x);
    return mix(a, b, f.y);
}
const vec4 CURL_K[6] = vec4[6](vec4(0.83, 1.31, -0.47, 0.9), vec4(-1.62, 0.38, 1.17, 1.3), vec4(1.12, -0.71, 0.95, 0.7),
    vec4(0.41, 1.77, -1.08, 1.1), vec4(-0.92, 0.56, 1.43, 0.8), vec4(1.36, -1.21, -0.33, 1.5));
const float CURL_P[6] = float[6](0.0, 1.3, 2.1, 0.7, 4.2, 3.3);
vec3 particle_curl(vec3 q, float tau) {
    vec3 g[3] = vec3[3](vec3(0.0), vec3(0.0), vec3(0.0));
    for (int j = 0; j < 6; j++) {
        vec4 k = CURL_K[j];
        g[j >> 1] += ((j & 1) != 0 ? 0.5 : 1.0) * cos(dot(k.xyz, q) + tau * k.w + CURL_P[j]) * k.xyz;
    }
    return 0.5 * vec3(g[2].y - g[1].z, g[0].z - g[2].x, g[1].x - g[0].y);
}
struct Mote { vec3 pos; float age, life, fade, gain; int size, cycle; };
Mote particle_mote(int e, int i, float t) {
    vec4 v0 = PARTICLE_EMITTER(e, 0), v1 = PARTICLE_EMITTER(e, 1), v2 = PARTICLE_EMITTER(e, 2), v3 = PARTICLE_EMITTER(e, 3);
    vec4 v4 = PARTICLE_EMITTER(e, 4), v6 = PARTICLE_EMITTER(e, 6), v7 = PARTICLE_EMITTER(e, 7);
    uint s = particle_hash(uint(v7.x) * 0x9e3779b1u ^ uint(i) * 0x85ebca77u);
    float life = max(v4.x, 0.05) * (1.0 - clamp(v4.y, 0.0, 0.95) * particle_unit(s));
    float tt = max(t, 0.0) + particle_unit(s) * life;
    float k = floor(tt / life);
    float age = clamp(tt - k * life, 0.0, life);
    uint c = particle_hash(s ^ (uint(int(k)) * 0xc2b2ae35u));
    float u0 = particle_unit(c), u1 = particle_unit(c), u2 = particle_unit(c);
    int shape = int(v1.w);
    vec3 p;
    if (shape == SPAWN_POINT) {
        float z = 1.0 - 2.0 * u0, r = sqrt(max(1.0 - z * z, 0.0)), a = 6.2831853 * u1, rr = v1.x * pow(u2, 1.0 / 3.0);
        p = v0.xyz + rr * vec3(r * cos(a), z, r * sin(a));
    } else if (shape == SPAWN_DISC) {
        float rr = v1.x * sqrt(u0), a = 6.2831853 * u1;
        p = v0.xyz + vec3(rr * cos(a), 0.0, rr * sin(a));
    } else {
        vec3 l = (vec3(u0, u1, u2) - 0.5) * v1.xyz;
        float cs = cos(v0.w), sn = sin(v0.w);
        p = v0.xyz + vec3(cs * l.x + sn * l.z, l.y, -sn * l.x + cs * l.z);
        if (shape == SPAWN_SURFACE)
            p.y = (PARTICLE_FLOOR_N > 0 ? particle_floor(p.xz) : v0.y + v1.y * 0.5) - 1.0;
    }
    float g = v2.w, drag = v3.w;
    float fall = drag > 1e-4 ? g * (age / drag - (1.0 - exp(-drag * age)) / (drag * drag)) : 0.5 * g * age * age;
    p += v2.xyz * age;
    p.y += fall;
    if (v3.x > 0.0) p += v3.x * particle_curl(p / max(v3.y, 1.0), t * v3.z);
    Mote m;
    m.pos = p, m.age = age, m.life = life, m.cycle = int(k);
    m.fade = (v4.z > 0.0 ? particle_smooth(age / v4.z) : 1.0) * (v4.w > 0.0 ? particle_smooth((life - age) / v4.w) : 1.0);
    m.gain = 1.0 - clamp(v6.z, 0.0, 1.0) * particle_unit(c);
    int lo = clamp(int(v6.x), 1, 4), hi = clamp(int(v6.y), lo, 4);
    m.size = min(lo + int(particle_unit(c) * float(hi - lo + 1)), hi);
    return m;
}
#endif
```

- [ ] **Step 3: The pass**

Create `pc/shaders/vulkan/particles_main.glsl`:

```glsl
// The particles' pass on Vulkan (pc/src/vk_particles.inc; particles.h): per
// mote its closed form (particles_sim.glsl), the light reaching it (its
// strongest lights and glows and the sun: with PARTICLES_RT and this frame's
// acceleration structure a shadow ray each, else unshadowed), its colour
// into the colour image and, coloured, its game pixels into the key image
// (depth << 16 | mote: the nearest wins). particles.cpp's CPU path is the
// same for OpenGL; keep the two the same.
layout(local_size_x = 64) in;
layout(std140, binding = 0) uniform Particles { // particle_math.h pc_particles_gpu
    ivec4 info; // motes, emitters, display w, h
    ivec4 info2; // lights, glows, sources per mote, shadow rays
    vec4 time_; // t, glow scale, debug, glow shadows
    vec4 projection; // hx, hy, ofx, ofy
    vec4 rotation[3]; // rows of R, w the translation
    vec4 env_[5];
    vec4 floor_info; // corner x, z, 1 / cell x, 1 / cell z
    vec4 floor_n; // x cells per side (0 none)
    vec4 floor_[144];
    vec4 glow_pos[32];
    vec4 glow_col[32];
    vec4 emitters[128]; // 16 x 8
} p;
struct Light { vec4 pos, col, right, up, dir, world; ivec4 info; }; // light_list.h pc_light_gpu; info.x casts
layout(std430, binding = 4) readonly buffer LightList { Light lights[]; };
layout(binding = 5, r32ui) uniform uimage2D keys;
layout(binding = 6, rgba32f) uniform writeonly image2D colours;
#define PARTICLE_EMITTER(e, k) p.emitters[(e) * 8 + (k)]
#define PARTICLE_FLOOR(i) p.floor_[i]
#define PARTICLE_FLOOR_INFO p.floor_info
#define PARTICLE_FLOOR_N int(p.floor_n.x)
#include "particles_sim.glsl"
#ifdef PARTICLES_RT
#include "rt_common.glsl"
bool blocked(vec3 o, vec3 d, float t) { float th; int hit; vec2 bc; return t > 0.0 && trace(o, d, t, true, d, 0.0, th, hit, bc); }
#else
uint rng = 0u;
float rand() { rng = rng * 747796405u + 2891336453u; return float(rng >> 8) / 16777216.0; }
bool blocked(vec3 o, vec3 d, float t) { return false; }
#endif
#define env_sun_dir p.env_[0]
#define env_sun_col p.env_[1]
#define env_zenith p.env_[2]
#define env_horizon p.env_[3]
#define env_ground p.env_[4]
#include "environment.glsl"
#include "light_shape.glsl"
float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// The light reaching x (particles.cpp light_at): the strongest sources, a shadow ray each, and the sun.
vec3 mote_light(vec3 x) {
    int keep = clamp(p.info2.z, 1, 4), kept = 0;
    vec3 rgb[4], dir[4];
    float reach[4], weight[4];
    bool casts[4];
    for (int i = 0; i < p.info2.x + p.info2.y; i++) {
        vec3 col, l;
        float d, a, skip;
        bool cast;
        if (i < p.info2.x) {
            vec3 c = lights[i].pos.xyz, rel = x - c, r = lights[i].right.xyz, u = lights[i].up.xyz;
            if (lights[i].col.w > 1.5) { // a rect: its nearest point
                float rr = dot(r, r), uu = dot(u, u);
                c += r * (rr > 1e-6 ? clamp(dot(rel, r) / rr, -1.0, 1.0) : 0.0) + u * (uu > 1e-6 ? clamp(dot(rel, u) / uu, -1.0, 1.0) : 0.0);
            }
            vec3 to = c - x;
            d = length(to), l = to / max(d, 1e-3);
            float f = min(d / max(lights[i].pos.w, 1e-3), 1.0);
            a = (1.0 - f * f) * (1.0 - f * f)
                * light_shape(lights[i].col.w, lights[i].pos.xyz, lights[i].dir, lights[i].right, lights[i].up, x, l);
            col = lights[i].col.rgb * a, skip = 2.0, cast = lights[i].info.x != 0;
        } else {
            int j = i - p.info2.x;
            vec3 to = p.glow_pos[j].xyz - x;
            d = length(to), l = to / max(d, 1e-3);
            float f = min(d / max(p.glow_pos[j].w, 1e-3), 1.0);
            a = (1.0 - f * f) * (1.0 - f * f) * p.time_.y;
            col = p.glow_col[j].rgb * a, skip = max(abs(p.glow_col[j].w), 2.0), cast = p.time_.w > 0.5;
        }
        if (!(a > 0.0)) continue;
        float w = lum(col);
        int at = kept < keep ? kept++ : keep;
        if (at == keep) {
            if (!(w > weight[keep - 1])) continue;
            at = keep - 1;
        }
        for (; at > 0 && weight[at - 1] < w; at--) {
            rgb[at] = rgb[at - 1], dir[at] = dir[at - 1], reach[at] = reach[at - 1];
            weight[at] = weight[at - 1], casts[at] = casts[at - 1];
        }
        rgb[at] = col, dir[at] = l, reach[at] = d - skip, weight[at] = w, casts[at] = cast;
    }
    bool traced = p.info2.w != 0;
    vec3 sum = vec3(0.0);
    for (int k = 0; k < kept; k++)
        if (!(traced && casts[k] && blocked(x, dir[k], reach[k]))) sum += rgb[k];
    if (env_sun_on() && !(traced && env_sun_col.w > 0.5 && blocked(x, env_sun_dir.xyz, ENV_SUN_DISTANCE)))
        sum += env_sun_col.rgb;
    return sum;
}
void main() {
    int g = int(gl_GlobalInvocationID.x);
    if (g >= p.info.x) return;
    int e = 0;
    for (int k = 1; k < p.info.y; k++)
        if (g >= int(PARTICLE_EMITTER(k, 7).z)) e = k;
    Mote m = particle_mote(e, g - int(PARTICLE_EMITTER(e, 7).z), p.time_.x);
    vec4 v5 = PARTICLE_EMITTER(e, 5), v6 = PARTICLE_EMITTER(e, 6), v7 = PARTICLE_EMITTER(e, 7);
    vec3 l = (p.time_.z > 0.5 ? vec3(1.0) : mote_light(m.pos)) + (v6.w > 0.5 ? vec3(v7.y) : vec3(0.0));
    vec3 colour = l * v5.rgb * v5.w * m.fade * m.gain;
    imageStore(colours, ivec2(g & 255, g >> 8), vec4(colour, lum(l)));
    if (dot(colour, vec3(1.0)) <= 1e-4) return; // a dark mote takes no pixel
    vec3 v = vec3(dot(p.rotation[0].xyz, m.pos) + p.rotation[0].w, dot(p.rotation[1].xyz, m.pos) + p.rotation[1].w,
        dot(p.rotation[2].xyz, m.pos) + p.rotation[2].w);
    if (!(v.z >= 16.0)) return; // PC_PARTICLE_NEAR
    vec2 s = p.projection.zw + p.projection.xy * v.xy / v.z;
    uint key = (uint(clamp(v.z, 1.0, 65535.0)) << 16) | uint(g & 0xffff);
    ivec2 a = ivec2(floor(s - 0.5 * float(m.size - 1)));
    for (int dy = 0; dy < m.size; dy++)
        for (int dx = 0; dx < m.size; dx++) {
            ivec2 c = a + ivec2(dx, dy);
            if (all(greaterThanEqual(c, ivec2(0))) && all(lessThan(c, p.info.zw))) imageAtomicMin(keys, c, key);
        }
}
```

Create `pc/shaders/vulkan/particles.comp`:

```glsl
#version 460
#extension GL_GOOGLE_include_directive : require
// The particles' pass without ray queries: every mote lit unshadowed (particles_main.glsl).
#include "particles_main.glsl"
```

Create `pc/shaders/vulkan/particles_rt.comp`:

```glsl
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
// The particles' pass with this frame's acceleration structure: a shadow ray per source (particles_main.glsl).
#define PARTICLES_RT
#include "particles_main.glsl"
```

Create `pc/src/vk_particles.inc`:

```c
/* The port's particles on Vulkan (included at the end of vk_rt.inc;
 * particles.h, pc/docs/Particles.md): one compute pass a present
 * (particles.comp, or particles_rt.comp with this frame's acceleration
 * structure: shadow rays) evaluates every mote, lights it, writes its colour
 * (RGBA32F, 256 x 64) and splats it into the key image (R32_UINT, a texel
 * per game pixel of the display, cleared to none first); scaled.frag
 * particles_at composites them (bindings 21, 22). Perf section "particles". */
#include "particles.h"

static struct {
    int state; /* 0 untried, 1 ready, -1 unavailable */
    VkDescriptorSetLayout set, rt_set;
    VkPipelineLayout layout, rt_layout;
    VkPipeline plain, rt;
    vk_image keys, colours;
    int size[2], on, count;
} vpt;

static int vpt_init(void)
{
    if (vpt.state)
        return vpt.state > 0;
    vpt.state = -1;
    const VkDescriptorType U = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, S = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           T = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, B = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           A = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    VkDescriptorType plain[7] = { U, U, U, U, B, S, S }; /* 1-3 unused without ray queries */
    VkDescriptorType rt[10] = { U, A, B, T, B, S, S, U, U, B }; /* rt_common.glsl: 1 tlas, 2 tris, 3 vram, 9 movers */
    vpt.set = vk_push_layout(plain, 7);
    vpt.layout = vrt_layout(vpt.set, 0);
    vpt.plain = vrt_compute(vk_spv_particles_comp, vk_spv_particles_comp_size, vpt.layout);
    if (vk.ray_query) {
        vpt.rt_set = vk_push_layout(rt, 10);
        vpt.rt_layout = vrt_layout(vpt.rt_set, 0);
        vpt.rt = vrt_compute(vk_spv_particles_rt_comp, vk_spv_particles_rt_comp_size, vpt.rt_layout);
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!vpt.plain
        || !vk_image_create(&vpt.colours, PC_PARTICLE_COLOUR_W, PC_PARTICLES_MAX / PC_PARTICLE_COLOUR_W,
            VK_FORMAT_R32G32B32A32_SFLOAT, usage)) {
        fprintf(stderr, "[particles] the Vulkan particle pass could not be created\n");
        return 0;
    }
    vpt.state = 1;
    return 1;
}

/* This present's motes for the display area w x h (game pixels); `traced`:
 * the path tracer built this frame's acceleration structure. Returns 1 when
 * there is something to composite (pc_vk_particles_bind). */
int pc_vk_particles_update(int x, int y, int w, int h, int traced)
{
    (void)x, (void)y;
    vpt.on = 0;
    if (w <= 0 || h <= 0)
        return 0;
    VkDeviceSize alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    vk_slice us = vk_arena(sizeof(pc_particles_gpu), alignment);
    if (!us.data)
        return 0;
    static pc_light_gpu lights[PC_LIGHTS_MAX];
    int light_count = 0;
    pc_particles_gpu* pu = us.data;
    int motes = pc_particles_gpu_frame(w, h, pu, lights, PC_LIGHTS_MAX, &light_count);
    if (motes <= 0 || !vpt_init())
        return 0;
    if (vpt.size[0] != w || vpt.size[1] != h || !vpt.keys.image) {
        vk_image_destroy_later(&vpt.keys);
        if (!vk_image_create(&vpt.keys, w, h, VK_FORMAT_R32_UINT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            vpt.size[0] = vpt.size[1] = 0;
            return 0;
        }
        vpt.size[0] = w, vpt.size[1] = h;
    }
    VkDeviceSize light_bytes = (VkDeviceSize)(light_count ? light_count : 1) * sizeof(pc_light_gpu);
    vk_slice ls = vk_arena(light_bytes, vk.properties.limits.minStorageBufferOffsetAlignment);
    if (!ls.data)
        return 0;
    memcpy(ls.data, lights, (size_t)light_count * sizeof(pc_light_gpu));
    int rt = traced && vpt.rt && vrt.tlas;
    pu->info2[3] = rt;
    pc_particles_gpu_traced(rt);
    vk_end_rendering();
    vk_section("particles");
    VkClearColorValue none = { .uint32 = { PC_PARTICLE_NONE, 0, 0, 0 } };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(vk_cmd(), vpt.keys.image, VK_IMAGE_LAYOUT_GENERAL, &none, 1, &range);
    vk_barrier();
    vk_descriptors d = { 0 };
    vk_descriptors_buffer(&d, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, us.buffer, us.offset, sizeof(pc_particles_gpu));
    vk_descriptors_buffer(&d, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ls.buffer, ls.offset, light_bytes);
    vk_descriptors_storage_image(&d, 5, vpt.keys.view);
    vk_descriptors_storage_image(&d, 6, vpt.colours.view);
    VkCommandBuffer cmd = vk_cmd();
    if (rt) {
        vk_descriptors_structure(&d, 1, vrt.tlas);
        vk_descriptors_buffer(&d, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vrt.room_tris.buffer, 0,
            (VkDeviceSize)(room_triangles ? room_triangles : 1) * sizeof *triangles);
        vk_descriptors_image(&d, 3, vk_hw.vram.view, vk.nearest);
        for (int b = 7; b <= 8; b++) /* declared, unused */
            vk_descriptors_buffer(&d, b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, us.buffer, us.offset, sizeof(pc_particles_gpu));
        vrt_bind_movers(&d, 9, -1);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.rt);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.rt_layout);
    } else {
        for (int b = 1; b <= 3; b++) /* declared, unused */
            vk_descriptors_buffer(&d, b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, us.buffer, us.offset, sizeof(pc_particles_gpu));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.plain);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_COMPUTE, vpt.layout);
    }
    vkCmdDispatch(cmd, (uint32_t)(motes + 63) / 64, 1, 1);
    vk_barrier();
    vpt.on = 1, vpt.count = motes;
    return 1;
}
/* The present's images (scaled.frag bindings 21, 22). */
int pc_vk_particles_bind(VkImageView* keys, VkImageView* colours)
{
    if (!vpt.on)
        return 0;
    *keys = vpt.keys.view, *colours = vpt.colours.view;
    return 1;
}
/* RPC particles {motes}: the last pass's key image (w x h) and its motes' colours. */
int pc_vk_particles_read(uint32_t* keys, int w, int h, float* colours, int count)
{
    if (!vpt.on || vpt.size[0] != w || vpt.size[1] != h || count > vpt.count || count <= 0)
        return 0;
    int rows = (count + PC_PARTICLE_COLOUR_W - 1) / PC_PARTICLE_COLOUR_W;
    float* all = malloc((size_t)rows * PC_PARTICLE_COLOUR_W * 4 * sizeof(float));
    int ok = all && vk_read_image(&vpt.keys, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, w, h, keys, sizeof(uint32_t))
        && vk_read_image(&vpt.colours, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, PC_PARTICLE_COLOUR_W, rows, all, 4 * sizeof(float));
    if (ok)
        memcpy(colours, all, (size_t)count * 4 * sizeof(float));
    free(all);
    return ok;
}
```

In `pc/src/vk_rt.inc`, after `#include "vk_fogvol.inc" /* the fog volumes' own renderer */` add:

```c
#include "vk_particles.inc" /* the port's particles (dust motes) */
```

- [ ] **Step 4: The present binds and composites it**

In `pc/src/vk_present.c`:

1. After the `pc_vk_fogvol_bind` extern add

```c
/* The port's particles (vk_particles.inc): their pass this frame, then their key and colour images. */
extern int pc_vk_particles_update(int x, int y, int w, int h, int traced) __attribute__((weak));
extern int pc_vk_particles_bind(VkImageView* keys, VkImageView* colours) __attribute__((weak));
```

2. In `present_ubo`, after `float studio[PC_MENU_LIGHT_VEC4S][4];` add `int particles[4]; /* the particles (vk_particles.inc): on, the display's size in game pixels */`.
3. In the layout's setup change `VkDescriptorType scaled_types[21];`, its loop `for (int i = 0; i < 21; i++)` and `scaled_set = vk_push_layout(scaled_types, 21);` to 23 (21, 22: combined image samplers like the others), and its comment to `... 20: the menu's backdrop (backdrop_keep); 21, 22: the particles (vk_particles.inc) */`.
4. After `int fogvol = !pc_gl_show_depth && pc_vk_fogvol_update ? ... : 0;` add

```c
            int particles = !pc_gl_show_depth && pc_vk_particles_update ? pc_vk_particles_update(x, y, w, h, gi) : 0;
```

5. After `vk_descriptors_image(&d, 20, backdrop_fill(pu), vk.linear);` add

```c
            /* The particles (vk_particles.inc): off, a uint and a float image nothing fetches. */
            VkImageView particle_keys = vk_hw.vram.view, particle_colours = vk_hw.dummy_color.view;
            if (particles && pc_vk_particles_bind && pc_vk_particles_bind(&particle_keys, &particle_colours))
                pu->particles[0] = 1, pu->particles[1] = w, pu->particles[2] = h;
            vk_descriptors_image(&d, 21, particle_keys, vk.nearest);
            vk_descriptors_image(&d, 22, particle_colours, vk.nearest);
```

In `pc/shaders/vulkan/scaled.frag`: after `vec4 studio_[9]; ...` in the UBO add `ivec4 particles_; // the particles (vk_particles.inc): on, the display's size in game pixels`; after `layout(binding = 20) uniform sampler2D backdrop_tex;` add

```glsl
layout(binding = 21) uniform usampler2D particle_keys; // the particles' key image (particles.glsl)
layout(binding = 22) uniform sampler2D particle_colours;
#include "particles.glsl"
```

change `bool haze_done = false, fogvol_done = false;` to `bool haze_done = false, fogvol_done = false, particles_done = false;`; in the menu model view branch change `lit_done = haze_done = fogvol_done = true;` to `lit_done = haze_done = fogvol_done = particles_done = true;`; before `if (u.fogvol.z != 0) { c = fogvol_apply(c, q, d); fogvol_done = true; }` add

```glsl
            if (u.particles_.x != 0 && view_mode == 0) { // the motes in front of it, before the fog and haze
                c += particles_at(particle_keys, particle_colours, u.particles_.yz, uv, d);
                particles_done = true;
            }
```

and before `if (u.fogvol.z != 0 && !fogvol_done && !ui && view_mode != 1) c = fogvol_apply(c, texel(uv), d);` add

```glsl
    if (u.particles_.x != 0 && !particles_done && view_mode == 0) // unlit pixels, and where nothing is drawn
        c += particles_at(particle_keys, particle_colours, u.particles_.yz, uv, d);
```

In `pc/src/gl_present.c`'s shader, the same for the menu's model view: change `"            lit_done = haze_done = fogvol_done = true;\n"` to `"            lit_done = haze_done = fogvol_done = particles_done = true;\n"` (no motes over the menu's model).

- [ ] **Step 5: Run it and see it pass on both renderers**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan particles_smoke fog_volume_smoke emissive_smoke
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 particles_smoke fog_volume_smoke emissive_smoke
```

Expected: `3/3 passed` both times; the Vulkan log has no `validation:` line (run once with `ROOD_VK_VALIDATE=1` set as the docs say if the layer is installed). Read both runs' `picture-lit.png` (below `pc/build/tests/particles_smoke/`, one per renderer run; copy the first aside before the second run overwrites it) and compare: the same glints in the same places.

- [ ] **Step 6: Commit**

```bash
git add pc/shaders/vulkan/particles_sim.glsl pc/shaders/vulkan/particles_main.glsl pc/shaders/vulkan/particles.comp pc/shaders/vulkan/particles_rt.comp pc/src/vk_particles.inc pc/src/vk_rt.inc pc/src/vk_present.c pc/shaders/vulkan/scaled.frag pc/src/gl_present.c pc/src/particles.cpp pc/tests/particles_smoke.py
git commit -m "$(cat <<'EOF'
Particles on Vulkan: one compute pass (closed form, strongest sources and the sun with ray-query shadows, key image by atomic min), composited in scaled.frag; RPC reads the pass back

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Emitters as editor objects: scene, schema kind, clipboard, R

**Files:**
- Create: `pc/src/particles_editor.cpp` (the schema kind now; the rest in Task 11)
- Modify: `pc/src/particles_internal.h`, `pc/src/particles.cpp` (`clear_field`), `pc/src/editor_scene.cpp` (`all_objects`, `index_of`, `label`, `position`, `delete_selection`), `pc/src/editor_clipboard.cpp` (copy, paste, messages), `pc/src/editor_schema.cpp` (`kind_of`), `pc/src/lights.cpp` (`scale_object`; the `editor_transform` scale message), `pc/tools/build_boot.py`, `pc/tests/editor_schema_smoke.py` (`EXPECTED_KINDS`)
- Test: `pc/tests/particles_smoke.py` (section `editor`)

- [ ] **Step 1: Write the failing section**

In `pc/tests/particles_smoke.py`, before `# In order:` add:

```python
def check_editor(s):
    """An emitter is an editor object: listed with its icon, clicked, edited in Details (one undo step,
    reset to its asset's value), R sizes its box, deleted and back, duplicated."""
    g, p = s.game, s.player
    ref = "emitter:" + EMITTER
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": [round(p["x"] + 150), round(p["y"] - 100),
                                                                        round(p["z"])], "size": [160, 120, 160]}])
    g.call("graphics", editor_cutaway=False)
    g.call("editor_ui", visible=True)
    g.run(8)

    def objects():
        return {o["id"]: o for o in g.call("editor_select")["objects"]}

    def history():
        return g.call("editor_undo", action="list")

    o = objects().get(ref)
    s.check(o is not None and o["kind"] == "emitter" and o["label"] == "Emitter 1: Dust motes",
            f"an emitter is an object of the editor ({o})")
    if o and o["screen"]:
        r = g.call("editor_select", action="click", x=o["screen"][0], y=o["screen"][1])
        s.check(r["under"] == ref and r["selection"] == [ref], f"a click on its icon selects it ({r['selection']})")
    else:
        s.check(False, "its icon is on screen")
    got = g.call("editor_schema", action="get", id=ref)
    f = got["fields"]
    s.check(got["kind"] == "emitter" and f["count"]["value"] == 900 and f["count"]["source"] == "asset"
            and not f["count"]["overridden"] and f["curl"]["value"] == 9,
            f"Details: its fields, from its asset ({got['kind']}, count {f['count']})")
    done = history()["done"]
    g.call("editor_schema", action="set", id=ref, values={"count": 250, "drift": [0, -3, 0]})
    h = history()
    e = s.emitter()
    s.check(e["count"] == 250 and e["fields"]["drift"] == "0,-3,0" and h["done"] == done + 1
            and h["undo"].startswith("Edit"), f"set through the schema: the emitter changes, one undo step ({h['undo']})")
    g.call("editor_schema", action="reset", id=ref, fields=["count"])
    f = g.call("editor_schema", action="get", id=ref)["fields"]
    s.check(f["count"]["value"] == 900 and not f["count"]["overridden"], "the reset arrow: its asset's value again")
    g.call("editor_undo", action="undo")
    g.call("editor_undo", action="undo")
    s.check(s.emitter()["count"] == 900 and s.emitter()["fields"]["drift"] == "1.2,0.4,0.8", "undo puts both back")
    before = s.emitter()["size"]
    g.call("editor_transform", action="scale", id=ref, ratio=[2, 1, 0.5])
    after = s.emitter()["size"]
    s.check(after == [before[0] * 2, before[1], before[2] * 0.5] and history()["undo"] == "Scale emitter",
            f"R on an emitter: its box {before} -> {after}")
    g.call("editor_undo", action="undo")
    g.call("editor_select", action="set", ids=[ref])
    g.call("editor_select", action="delete")
    s.check(s.emitter() is None and g.call("editor_select")["selection"] == [], "Delete: it goes, with the selection")
    g.call("editor_undo", action="undo")
    s.check(s.emitter() is not None, "undo: it is back")
    g.call("editor_select", action="set", ids=[ref])
    r = g.call("editor_select", action="duplicate")
    copies = [e for e in s.particles()["emitters"] if e["id"] != EMITTER and e["asset"] == "dust_motes"]
    s.check(r["count"] == 1 and len(copies) == 1 and copies[0]["pos"][0] == s.emitter()["pos"][0] + 64,
            "Ctrl+D: a copy 64 units along x, a new id")
    for c in copies:
        s.particles(emitters=[{"id": c["id"], "remove": True}])
    g.call("editor_ui", visible=False)
```

add `check_editor` to `SECTIONS` after `check_picture`; in `pc/tests/editor_schema_smoke.py` change `EXPECTED_KINDS` to include `"emitter"`.

- [ ] **Step 2: Run them and see them fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only editor
```

Expected: `FAIL: an emitter is an object of the editor (None)`.

- [ ] **Step 3: The scene**

In `pc/src/editor_scene.cpp`:

- `all_objects()`: after the fog volumes' loop add

```cpp
    for (int i = 0; i < rood_particles::emitter_count(); i++)
        out.push_back(rood_scene::emitter_ref(i));
```

- `index_of`: before `case rood_selection::KIND_GLOW: {` add

```cpp
    case rood_selection::KIND_EMITTER:
        return rood_particles::emitter_index(r.id);
```

- `label`: before `case rood_selection::KIND_GLOW: {` add

```cpp
    case rood_selection::KIND_EMITTER:
        if (i >= 0)
            return rood_particles::emitter_label(i);
        break;
```

- `position`: before `case rood_selection::KIND_GLOW: {` add

```cpp
    case rood_selection::KIND_EMITTER:
        return rood_particles::emitter_centre(i, out, radius);
```

- `delete_selection`: declare `std::vector<int> gone_lights, gone_actors, gone_volumes, gone_emitters;`, replace the push line with

```cpp
        (r.kind == rood_selection::KIND_LIGHT  ? gone_lights
                : r.kind == rood_selection::KIND_ACTOR ? gone_actors
                : r.kind == rood_selection::KIND_EMITTER ? gone_emitters
                                                       : gone_volumes)
            .push_back(i);
```

add `+ gone_emitters.size()` to `count`, `descending(gone_emitters);`, and after the volumes' removal loop

```cpp
    for (int i : gone_emitters)
        rood_particles::remove_emitter(i);
```

and the header comment's "lights, actors and volumes" to "lights, actors, volumes and emitters" (also in `editor_scene.h`'s `delete_selection` and `in_box` comments).

In `pc/src/editor_clipboard.cpp`: add `#include "particles_internal.h"`; in `copy_text` replace the item expression with

```cpp
        e.item = r.kind == rood_selection::KIND_LIGHT ? lighting::item_from_light(lights[i])
            : r.kind == rood_selection::KIND_ACTOR    ? lighting::item_from_actor(actors[i])
            : r.kind == rood_selection::KIND_EMITTER  ? rood_particles::emitter_item(i)
                                                       : rood_atmo::volume_item(i);
```

in `paste_text` after the `"fog"` branch add

```cpp
        } else if (e.item.kind == "emitter") {
            int i = rood_particles::add_emitter_item(e.item, offset);
            if (i >= 0)
                added.push_back(emitter_ref(i));
```

and change the two messages' "lights, light actors or fog volumes" to "lights, light actors, fog volumes or emitters".

- [ ] **Step 4: The schema kind**

In `pc/src/particles_internal.h` add after `set_field`:

```cpp
void clear_field(int index, const char* key); /* it follows its asset again (the reset arrow) */
void outliner(const char* filter); /* the Outliner's Emitters rows (particles_editor.cpp) */
void details_extra(int index); /* Details: an emitter's tools under its fields (particles_editor.cpp) */
```

In `pc/src/particles.cpp` after `set_field`:

```cpp
void rood_particles::clear_field(int index, const char* key)
{
    lighting::Item it = item_of(emitters[index]);
    it.erase(key);
    apply_item(it);
    pc_lights_mark_dirty();
}
```

Create `pc/src/particles_editor.cpp`:

```cpp
// The particle emitters in the editor (particles_internal.h; pc/docs/Particles.md):
// their schema kind (Details, the value ladder, the reset arrow to the asset's
// value, RPC editor_schema), Details' tools, the Outliner rows, the
// Atmosphere tab's Dust section, the viewport's outlines, icons and gizmo.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImGuizmo.h"
#include "editor_overlays.h"
#include "editor_scene.h"
#include "editor_schema.h"
#include "editor_selection.h"
#include "editor_undo.h"
#include "lighting_layers.h"
#include "lights_internal.h"
#include "particle_assets.h"
#include "particles.h"
#include "particles_internal.h"

extern "C" void pc_lights_mark_dirty(void);
extern "C" int pc_lights_gizmo_space; /* lights.cpp: the move tool's axes (0 world, 1 local) */

namespace {
namespace rs = rood_schema;
using rs::Field;
using rs::Value;
const char* const SHAPE_OPTIONS[] = { "Box", "Room", "Point", "Disc", "Surface" };

/* A schema value as lighting text. */
std::string text_of(rs::Type type, const std::vector<std::string>& options, const Value& v)
{
    char text[96];
    switch (type) {
    case rs::TYPE_ENUM: {
        int i = (int)lroundf(v.x());
        std::string s = i >= 0 && i < (int)options.size() ? options[i] : options.empty() ? "" : options[0];
        for (char& c : s)
            c = (char)tolower((unsigned char)c);
        return s;
    }
    case rs::TYPE_BOOL:
        return v.x() > 0.5f ? "1" : "0";
    case rs::TYPE_INT:
        return std::to_string((int)lroundf(v.x()));
    case rs::TYPE_VEC3:
    case rs::TYPE_COLOUR:
        snprintf(text, sizeof text, "%.4f,%.4f,%.4f", v.at(0), v.at(1), v.at(2));
        return text;
    default:
        snprintf(text, sizeof text, "%.6g", v.x());
        return text;
    }
}

rs::Kind emitter_kind()
{
    rs::Kind k;
    k.name = "emitter";
    k.label = "Particle emitter";
    k.object = rood_selection::KIND_EMITTER;
    k.closed = { "Motion", "Look" };
    k.extras = [](int i) { rood_particles::details_extra(i); };
    /* an asset field: through the item's text, its asset's value for the reset arrow */
    auto add = [&k](Field f) {
        const char* text = lighting::field_default("emitter", f.name);
        if (*text)
            f.def = rs::from_text(f.type, f.options, text);
        std::string key = f.name;
        rs::Type type = f.type;
        std::vector<std::string> options = f.options;
        f.get = [key, type, options](int i) { return rs::from_text(type, options, rood_particles::field(i, key.c_str())); };
        f.set = [key, type, options](int i, const Value& v) {
            rood_particles::set_field(i, key.c_str(), text_of(type, options, v));
        };
        f.base = [key, type, options](int i) {
            return rs::from_text(type, options, rood_particles::asset_value(i, key.c_str()));
        };
        f.base_source = [](int i) -> const char* { return rood_particles::own(i).asset.empty() ? "default" : "asset"; };
        f.overridden = [key](int i) { return rood_particles::overrides(i, key.c_str()); };
        f.reset = [key](int i) { rood_particles::clear_field(i, key.c_str()); };
        k.fields.push_back(f);
    };
    auto integer = [](Field f) {
        f.type = rs::TYPE_INT;
        return f;
    };
    /* Basics: its own switch, then how many and how they light */
    {
        Field f = rs::bool_field("enabled", "Enabled", "Basics", true, "Off: none of its particles are drawn");
        f.get = [](int i) { return Value::scalar(rood_particles::own(i).enabled ? 1.0f : 0.0f); };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            o.enabled = v.x() > 0.5f;
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    add(integer(rs::number_field("count", "Count", "Basics", 0, PC_PARTICLES_MAX, 1, 400, "", true,
        "Particles alive at once (Graphics > Dust High; Low draws dust_low of them)")));
    add(rs::bool_field("lit", "Lit everywhere", "Basics", false,
        "Off: they show only where light reaches them (in light only); on: also their ambient light everywhere"));
    add(rs::number_field("ambient", "Ambient", "Basics", 0, 1, 0.001f, 0, "", false, "Their light everywhere when lit"));
    /* Spawn */
    add(rs::enum_field("shape", "Shape", "Spawn", std::vector<std::string>(SHAPE_OPTIONS, SHAPE_OPTIONS + 5), 0,
        "Box: in its box; Room: in the room's bounds; Point: a ball of its radius; Disc: a horizontal disc; Surface: on the "
        "floor under its box"));
    add(rs::vec3_field("size", "Size", "Spawn", 2, 8192, 1, Value::vec(256, 192, 256), "u", "Its box's full extents"));
    add(rs::number_field("radius", "Radius", "Spawn", 0, 4096, 1, 64, "u", false, "A point's or a disc's radius"));
    add(rs::number_field("lifetime", "Lifetime", "Spawn", 0.1f, 120, 0.1f, 10, "s", true, "Seconds each particle lives"));
    add(rs::number_field("lifetime_jitter", "Lifetime jitter", "Spawn", 0, 0.95f, 0.01f, 0.5f, "", false,
        "The share of its lifetime a particle may live less"));
    {
        Field f = integer(rs::number_field("seed", "Seed", "Spawn", 1, 999999, 1, 1, "", false,
            "Its random choices (two emitters with one seed move alike)"));
        f.shared = false;
        f.get = [](int i) { return Value::scalar((float)rood_particles::own(i).seed); };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            o.seed = (int)lroundf(v.x());
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    /* Motion: the behaviour modules (0: off) */
    add(rs::vec3_field("drift", "Drift", "Motion", -200, 200, 0.1f, Value::vec(0, 0, 0), "u/s", "Its steady drift (y down)"));
    add(rs::number_field("curl", "Turbulence", "Motion", 0, 200, 0.1f, 0, "u", false,
        "How far a smooth swirling field moves each particle"));
    add(rs::number_field("curl_scale", "Swirl size", "Motion", 4, 2048, 1, 64, "u", true));
    add(rs::number_field("curl_speed", "Swirl speed", "Motion", 0, 4, 0.001f, 0.1f, "/s", false, "How fast the swirls change"));
    add(rs::number_field("gravity", "Gravity", "Motion", -200, 200, 0.01f, 0, "u/s2", false, "y down: negative rises (buoyancy)"));
    add(rs::number_field("drag", "Drag", "Motion", 0, 10, 0.001f, 0, "/s", false, "It falls at most gravity / drag"));
    add(rs::number_field("fade_in", "Fade in", "Motion", 0, 30, 0.01f, 0, "s"));
    add(rs::number_field("fade_out", "Fade out", "Motion", 0, 30, 0.01f, 0, "s"));
    /* Look: the renderer */
    add(rs::enum_field("render", "Renderer", "Look", { "Pixel" }, 0, "Square game pixels, nearest (soft sprites and streaks later)"));
    add(integer(rs::number_field("pixel_min", "Size (min)", "Look", 1, 4, 1, 1, "px", false, "Game pixels")));
    add(integer(rs::number_field("pixel_max", "Size (max)", "Look", 1, 4, 1, 2, "px", false, "Game pixels")));
    add(rs::colour_field("color", "Colour", "Look", 4, Value::vec(1, 1, 1), "Linear colour"));
    add(rs::number_field("brightness", "Brightness", "Look", 0, 8, 0.01f, 1, "", false, "Scale on the light it reflects"));
    add(rs::number_field("variation", "Variation", "Look", 0, 1, 0.01f, 0.5f, "", false, "How much dimmer each one may be"));
    /* Transform: its own */
    {
        Field f = rs::vec3_field("pos", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "Its centre");
        f.resettable = false, f.shared = false;
        f.get = [](int i) {
            rood_particles::Own o = rood_particles::own(i);
            return Value::vec(o.pos[0], o.pos[1], o.pos[2]);
        };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            for (int c = 0; c < 3; c++)
                o.pos[c] = v.at(c);
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    {
        Field f = rs::angle_field("yaw", "Yaw", "Transform", -180, 180, 0.5f, 0);
        f.get = [](int i) { return Value::scalar(rood_particles::own(i).yaw); };
        f.set = [](int i, const Value& v) {
            rood_particles::Own o = rood_particles::own(i);
            o.yaw = v.x();
            rood_particles::set_own(i, o);
        };
        k.fields.push_back(f);
    }
    return k;
}
struct Register {
    Register() { rs::add_kind(emitter_kind()); }
} registered;
} // namespace

/* Details > an emitter's tools under its fields (Task 11 fills them). */
void rood_particles::details_extra(int index) { (void)index; }
void rood_particles::outliner(const char* filter) { (void)filter; }
```

In `pc/src/editor_schema.cpp` `kind_of`, before `default:` add

```cpp
    case rood_selection::KIND_EMITTER:
        return find_kind("emitter"); /* particles_editor.cpp registers it */
```

In `pc/src/lights.cpp`: add `#include "particles_internal.h"`; in `scale_object`, before `} else {` add

```cpp
    } else if (r.kind == rood_selection::KIND_EMITTER) {
        float size[3], out[3];
        if (!rood_particles::emitter_size(i, size))
            return;
        rood_transform::scale_volume(size, ratio, out);
        rood_particles::set_emitter_size(i, out);
        pc_undo_hint("Scale emitter");
```

and change the `editor_transform` scale message to `"scale needs ratio [x, y, z] and a light, light actor, fog volume or emitter (id or the selection)"`.

In `pc/tools/build_boot.py` after `root / "pc/src/particle_assets.cpp",` add `root / "pc/src/particles_editor.cpp",`.

- [ ] **Step 5: Run them and see them pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only editor
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema_smoke editor_selection_smoke editor_transform_smoke editor_undo_smoke
```

Expected: `PASS: particles` except the icon click (`FAIL: its icon is on screen`: the viewport draws icons in Task 11; fine for now); the four editor suites `4/4 passed` (the schema lint takes the new kind: every field named, labelled, in range).

- [ ] **Step 6: Commit**

```bash
git add pc/src/particles_editor.cpp pc/src/particles_internal.h pc/src/particles.cpp pc/src/editor_scene.cpp pc/src/editor_scene.h pc/src/editor_clipboard.cpp pc/src/editor_schema.cpp pc/src/lights.cpp pc/tools/build_boot.py pc/tests/particles_smoke.py pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Editor: emitters are scene objects (labels, F, delete, copy/paste/duplicate, R sizes the box) with a schema kind (Basics, Spawn, Motion, Look, Transform; reset to the asset's value)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Emitters in the viewport, the Outliner and the Atmosphere tab

**Files:**
- Modify: `pc/src/particles_editor.cpp` (the viewport, Outliner rows, Details tools, the Atmosphere tab's section), `pc/src/lights.cpp` (call the viewport after `pc_atmosphere_viewport`), `pc/src/environment.cpp` (call the page after `pc_atmosphere_page`), `pc/src/dev_overlay.cpp` (the Outliner section; Show > Emitters in both menus)
- Test: `pc/tests/particles_smoke.py` (section `viewport`)

- [ ] **Step 1: Write the failing section**

In `pc/tests/particles_smoke.py`, before `# In order:` add:

```python
def check_viewport(s):
    """Real mouse events: a click on an emitter's icon selects it, the W gizmo's centre drags it (one undo step)."""
    g, p = s.game, s.player
    ref = "emitter:" + EMITTER
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": [round(p["x"] + 150), round(p["y"] - 100),
                                                                        round(p["z"])], "size": [160, 120, 160]}])
    g.call("editor_ui", visible=True)
    g.run(8)

    def mouse(xy, down):
        g.call("system", mouse=[float(xy[0]), float(xy[1])], button=1, down=down)
        g.run(3)

    xy = {o["id"]: o for o in g.call("editor_select")["objects"]}[ref]["screen"]
    if not s.check(xy is not None, "its icon is on screen"):
        return
    g.call("editor_select", action="clear")
    mouse(xy, True)
    mouse(xy, False)
    r = g.call("editor_select")
    s.check(r["selection"] == [ref] and r["source"] == "viewport", f"a mouse click on its icon selects it ({r['selection']})")
    g.call("editor_input", keys=["w"])
    g.run(4)
    before, done = s.emitter()["pos"], g.call("editor_undo", action="list")["done"]
    mouse(xy, True)
    mouse([xy[0] + 30, xy[1]], True)
    mouse([xy[0] + 60, xy[1] + 10], True)
    mouse([xy[0] + 60, xy[1] + 10], False)
    after, h = s.emitter()["pos"], g.call("editor_undo", action="list")
    moved = math.dist(before, after)
    s.check(moved > 10 and h["done"] == done + 1 and h["undo"] == "Move emitter",
            f"the W gizmo's centre dragged 60 px: it moves {moved:.0f} units in the view's plane, one undo step ({h['undo']})")
    g.call("editor_undo", action="undo")
    s.check(s.emitter()["pos"] == before, "undo puts it back")
    s.particles(emitters=[{"id": EMITTER, "remove": True}])
    g.call("editor_ui", visible=False)
```

and add `check_viewport` to `SECTIONS` after `check_editor`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only editor,viewport
```

Expected: `FAIL: its icon is on screen`.

- [ ] **Step 3: The viewport, Outliner, Details tools and the Atmosphere section**

In `pc/src/particles_editor.cpp`, replace the two placeholder functions at the end with:

```cpp
namespace {
const float DEG = 3.14159265f / 180.0f;
void tooltip(const char* text)
{
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}
int asset_choice = 0; /* the Atmosphere tab's asset for Add emitter */
const char* const LEVELS = "Off\0Low\0High\0";
/* A new emitter of the chosen asset on the surface under the view's centre, its box's bottom on it. */
bool add_here()
{
    const auto& list = rood_particles::assets();
    float rect[4];
    rood_lights::Placement where;
    if (list.empty() || !rood_lights::view_rect(rect)
        || !rood_lights::place_compute("light:point", rect[0] + rect[2] * 0.5f, rect[1] + rect[3] * 0.5f, PC_SNAP_MODE_SURFACE,
            where))
        return false;
    const rood_particles::Asset& a = list[std::min(asset_choice, (int)list.size() - 1)];
    float size[3] = { 256, 192, 256 };
    if (const std::string* s = a.get("size"))
        sscanf(s->c_str(), "%f,%f,%f", &size[0], &size[1], &size[2]);
    float pos[3] = { where.surface[0], where.surface[1] - size[1] * 0.5f - 8, where.surface[2] }; /* y down */
    rood_particles::add_emitter(a.id, pos);
    pc_undo_hint("Add emitter");
    return true;
}
} // namespace

/* Details > an emitter's tools under its fields: its name, its asset (its own
 * values stay, the rest follow the new one), the gizmo's mode, Reset to its
 * asset, Delete. */
void rood_particles::details_extra(int index)
{
    if (index < 0 || index >= emitter_count())
        return;
    Own o = own(index);
    ImGui::PushID(index);
    ImGui::SeparatorText("Tools");
    char name[96];
    snprintf(name, sizeof name, "%s", o.name.c_str());
    if (ImGui::InputText("Name", name, sizeof name)) {
        o.name = name;
        set_own(index, o);
        pc_undo_hint("Rename emitter");
    }
    const auto& list = assets();
    if (ImGui::BeginCombo("Asset", o.asset.empty() ? "(none)" : o.asset.c_str())) {
        for (const Asset& a : list)
            if (ImGui::Selectable(a.name.c_str(), a.id == o.asset)) {
                o.asset = a.id;
                set_own(index, o);
                pc_undo_hint("Change emitter asset");
            }
        ImGui::EndCombo();
    }
    tooltip("pc/assets/emitters: every field it does not set comes from its asset (the reset arrows go back to it).");
    ImGui::RadioButton("Move (W)", &rood_lights::gizmo_operation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Turn (E)", &rood_lights::gizmo_operation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Size (R)", &rood_lights::gizmo_operation, 2);
    if (ImGui::Button("Reset all to its asset")) {
        pc_undo_hint("Reset emitter to its asset");
        for (const auto& f : lighting::Fields(emitter_item(index).fields))
            if (lighting::preset_field("emitter", f.first))
                clear_field(index, f.first.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete emitter")) {
        pc_undo_hint("Delete emitter");
        remove_emitter(index);
        rood_scene::refresh();
        pc_lights_mark_dirty();
    }
    ImGui::Text("%d particles drawn", drawn(index));
    ImGui::PopID();
}

/* Outliner rows for the room's emitters: the enabled box, click to select (Shift adds, Ctrl toggles), the layer badge. */
void rood_particles::outliner(const char* filter)
{
    if (!emitter_count()) {
        ImGui::TextDisabled("None in this room (Details > Atmosphere: Add emitter)");
        return;
    }
    for (int i = 0; i < emitter_count(); i++) {
        std::string label = emitter_label(i);
        if (filter && filter[0] && !strcasestr(label.c_str(), filter))
            continue;
        ImGui::PushID(i);
        Own o = own(i);
        bool on = o.enabled;
        if (ImGui::Checkbox("##on", &on)) {
            o.enabled = on;
            set_own(i, o);
            pc_undo_hint(on ? "Enable emitter" : "Disable emitter");
        }
        ImGui::SameLine();
        rood_selection::Ref ref = rood_scene::emitter_ref(i);
        bool hidden = rood_selection::hidden(ref);
        if (!on || hidden)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (hidden)
            label += "  [hidden]###" + emitter_label(i);
        if (ImGui::Selectable(label.c_str(), rood_selection::contains(ref)))
            rood_scene::row_clicked(ref);
        rood_scene::row_drawn(ref);
        if (!on || hidden)
            ImGui::PopStyleColor();
        rood_lights::layer_row(ref.id);
        ImGui::PopID();
    }
}

/* Details > Atmosphere > Dust and particles: the setting, adding an emitter,
 * the renderer's settings and what it drew. Returns true when the room changed. */
bool pc_particles_page(void)
{
    bool changed = false;
    ImGui::PushID("particles");
    ImGui::SeparatorText("Dust and particles");
    ImGui::Combo("Dust (all rooms)", &pc_dust, LEVELS);
    tooltip("Graphics > Dust (settings key dust): Off draws none (the picture as before), Low dust_low of each emitter's\n"
            "count, High all of them.");
    ImGui::SliderFloat("Low's share", &pc_dust_low, 0, 1, "%.2f");
    const auto& list = rood_particles::assets();
    if (!list.empty()) {
        asset_choice = std::min(asset_choice, (int)list.size() - 1);
        if (ImGui::BeginCombo("##asset", list[asset_choice].name.c_str())) {
            for (int i = 0; i < (int)list.size(); i++)
                if (ImGui::Selectable(list[i].name.c_str(), i == asset_choice))
                    asset_choice = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add emitter"))
            changed |= add_here();
        tooltip("An emitter of this asset on the surface under the view's centre, selected: its fields are in Details.");
    } else {
        ImGui::TextDisabled("%s", rood_particles::assets_status().c_str());
    }
    ImGui::Checkbox("Show emitters in the view", rood_particles::emitters_shown_flag());
    bool debug = pc_particles_debug != 0;
    if (ImGui::Checkbox("Show every particle (unlit)", &debug))
        pc_particles_debug = debug;
    tooltip("Where they are: every particle white, whatever reaches it (RPC particles {debug}).");
    if (ImGui::TreeNode("Particle renderer")) {
        ImGui::SliderFloat("Glows' light", &pc_particles_glow, 0, 4, "%.2f");
        tooltip("How much the glows (fires, windows, the save point) light the particles (settings particles_glow).");
        ImGui::SliderInt("Sources per particle", &pc_particles_lights, 1, 4);
        tooltip("Its strongest lights and glows, each with a shadow ray, and the sun (settings particles_lights).");
        bool shadows = pc_particles_glow_shadows != 0;
        if (ImGui::Checkbox("Glows cast shadows on them", &shadows))
            pc_particles_glow_shadows = shadows;
        ImGui::SliderInt("CPU shadow rays a frame (OpenGL)", &pc_particles_cpu_rays, 64, 8192);
        if (ImGui::SmallButton("Reload assets"))
            rood_particles::reload_assets();
        ImGui::TextDisabled("%s", rood_particles::assets_status().c_str());
        ImGui::TreePop();
    }
    ImGui::PopID();
    return changed;
}

/* The room's emitters in the editor view (Show > Emitters): their outlines,
 * an icon at each centre (a click picks it, box select and the RPC see it),
 * the primary's gizmo (W move, E turn, R its box's size or its radius).
 * Returns true while the gizmo has the mouse. */
bool pc_particles_viewport(const float view[16], const float projection[16], const float rect[4], void* draw_list)
{
    ImDrawList* draw = (ImDrawList*)draw_list;
    auto project = [&](const float w[3], ImVec2& out) {
        float v[4], c[4];
        for (int r = 0; r < 4; r++)
            v[r] = view[r] * w[0] + view[4 + r] * w[1] + view[8 + r] * w[2] + view[12 + r];
        for (int r = 0; r < 4; r++)
            c[r] = projection[r] * v[0] + projection[4 + r] * v[1] + projection[8 + r] * v[2] + projection[12 + r] * v[3];
        if (c[3] <= 1e-3f)
            return false;
        out = ImVec2(rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * rect[2], rect[1] + (0.5f - c[1] / c[3] * 0.5f) * rect[3]);
        return true;
    };
    using namespace rood_particles;
    int primary = rood_selection::primary().kind == rood_selection::KIND_EMITTER
        ? emitter_index(rood_selection::primary().id) : -1;
    for (int i = 0; emitters_shown() && i < emitter_count(); i++) {
        rood_selection::Ref ref = rood_scene::emitter_ref(i);
        if (rood_selection::hidden(ref))
            continue;
        Own o = own(i);
        bool chosen = rood_selection::contains(ref);
        ImU32 col = i == primary ? IM_COL32(255, 230, 120, 230) : chosen ? IM_COL32(255, 230, 120, 140)
                                                                         : IM_COL32(200, 255, 190, o.enabled ? 110 : 45);
        std::string shape = field(i, "shape");
        float size[3];
        emitter_size(i, size);
        float c = cosf(o.yaw * DEG), s = sinf(o.yaw * DEG);
        if (shape == "box" || shape == "surface") {
            static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            ImVec2 q[8];
            bool ok[8];
            for (int k = 0; k < 8; k++) {
                float lx = (k & 1 ? 0.5f : -0.5f) * size[0], ly = (k & 2 ? 0.5f : -0.5f) * size[1],
                      lz = (k & 4 ? 0.5f : -0.5f) * size[2];
                float p[3] = { o.pos[0] + c * lx + s * lz, o.pos[1] + ly, o.pos[2] - s * lx + c * lz };
                ok[k] = project(p, q[k]);
            }
            for (const auto& e : edges)
                if (ok[e[0]] && ok[e[1]])
                    draw->AddLine(q[e[0]], q[e[1]], col, chosen ? 2.0f : 1.0f);
        } else if (shape == "point" || shape == "disc") {
            float r = (float)atof(field(i, "radius").c_str());
            ImVec2 pts[33];
            int n = 0;
            for (int k = 0; k <= 32; k++) {
                float a = k * 6.2831853f / 32, p[3] = { o.pos[0] + r * cosf(a), o.pos[1], o.pos[2] + r * sinf(a) };
                if (project(p, pts[n]))
                    n++;
            }
            if (n > 1)
                draw->AddPolyline(pts, n, col, 0, chosen ? 2.0f : 1.0f);
        }
        ImVec2 at;
        if (project(o.pos, at)) {
            draw->AddCircleFilled(at, 4.5f, col);
            char label[32];
            snprintf(label, sizeof label, "emitter %d", i + 1);
            draw->AddText(ImVec2(at.x + 7, at.y - 7), col, label);
            rood_scene::icon(ref, at.x, at.y, 9, true); /* a click picks it (not a ray: a room-sized box would hide the room) */
        }
    }
    if (primary < 0 || rood_lights::selected >= 0 || rood_lights::selected_actor >= 0)
        return false;
    Own o = own(primary);
    std::string shape = field(primary, "shape");
    bool ball = shape == "point" || shape == "disc"; /* R sizes its radius */
    float size[3];
    emitter_size(primary, size);
    if (ball)
        size[0] = size[1] = size[2] = fmaxf(2 * (float)atof(field(primary, "radius").c_str()), 2);
    float c = cosf(o.yaw * DEG), s = sinf(o.yaw * DEG);
    float model[16] = { c * size[0], 0, -s * size[0], 0, 0, size[1], 0, 0, s * size[2], 0, c * size[2], 0, o.pos[0], o.pos[1],
        o.pos[2], 1 };
    int operation = rood_lights::gizmo_operation;
    ImGuizmo::OPERATION op = operation == 2 ? ImGuizmo::SCALE : operation == 1 ? ImGuizmo::ROTATE_Y : ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mode = operation == 0 && !pc_lights_gizmo_space ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    if (ImGuizmo::Manipulate(view, projection, op, mode, model)) {
        float sx = sqrtf(model[0] * model[0] + model[1] * model[1] + model[2] * model[2]);
        float sy = sqrtf(model[4] * model[4] + model[5] * model[5] + model[6] * model[6]);
        float sz = sqrtf(model[8] * model[8] + model[9] * model[9] + model[10] * model[10]);
        o.pos[0] = model[12], o.pos[1] = model[13], o.pos[2] = model[14];
        if (operation == 1 && sx > 1e-6f)
            o.yaw = atan2f(-model[2], model[0]) / DEG;
        set_own(primary, o);
        if (operation == 2) {
            if (ball) {
                char text[32];
                snprintf(text, sizeof text, "%.1f", fmaxf(fmaxf(sx, sy), sz) * 0.5f);
                set_field(primary, "radius", text);
            } else {
                float out[3] = { fmaxf(sx, 2), fmaxf(sy, 2), fmaxf(sz, 2) };
                set_emitter_size(primary, out);
            }
        }
        pc_undo_hint(operation == 0 ? "Move emitter" : operation == 2 ? "Size emitter" : "Turn emitter");
    }
    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}
```

Add `#include <algorithm>` to the file's includes.

In `pc/src/lights.cpp`, next to `bool pc_atmosphere_viewport(...)`'s declaration add `bool pc_particles_viewport(const float view[16], const float projection[16], const float rect[4], void* draw_list);` and after

```cpp
    if (pc_atmosphere_viewport(v.view, v.projection, v.rect, draw))
        gizmo_busy = true;
```

add

```cpp
    /* Particle emitters (particles_editor.cpp): outlines, icons, the selected one's gizmo. */
    if (pc_particles_viewport(v.view, v.projection, v.rect, draw))
        gizmo_busy = true;
```

In `pc/src/environment.cpp`, next to `bool pc_atmosphere_page(void);` declare `bool pc_particles_page(void); /* particles_editor.cpp */` and after `changed |= pc_atmosphere_page();` add `changed |= pc_particles_page(); /* dust and particles */`.

In `pc/src/dev_overlay.cpp`: add `#include "particles_internal.h"`; in the Outliner after the `"Fog volumes (this room)"` tree node add

```cpp
            if (rood_scene::scroll_due(rood_selection::KIND_EMITTER))
                ImGui::SetNextItemOpen(true);
            if (ImGui::TreeNodeEx("Emitters (this room)", ImGuiTreeNodeFlags_DefaultOpen)) {
                rood_particles::outliner(filter);
                ImGui::TreePop();
            }
```

and after both `ImGui::MenuItem("Fog volumes", nullptr, pc_atmosphere_show_volumes());` lines add `ImGui::MenuItem("Emitters", nullptr, rood_particles::emitters_shown_flag());`.

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only editor,viewport
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection_smoke editor_handles_smoke editor_transform_smoke
```

Expected: `PASS: particles`; the three editor suites `3/3 passed` (an emitter only exists where added: the save room has none).

- [ ] **Step 5: Commit**

```bash
git add pc/src/particles_editor.cpp pc/src/lights.cpp pc/src/environment.cpp pc/src/dev_overlay.cpp pc/tests/particles_smoke.py
git commit -m "$(cat <<'EOF'
Editor: emitters in the view (outlines, a pickable icon, the W/E/R gizmo), the Outliner, Details tools, Show > Emitters; Atmosphere > Dust and particles (Add emitter, the renderer's settings)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: The Dust setting: the Graphics page, Details > Graphics, RPC `graphics`

**Files:**
- Modify: `pc/src/ui/ui_settings.cpp` (an extern; a row after "Glowing surfaces"), `pc/tests/ui_settings_stubs.cpp` (`pc_dust`), `pc/tests/ui_settings_probe.cpp` (16 -> 17 rows; the row), `pc/src/dev_overlay.cpp` (`graphics_panel`), `pc/src/dev_rpc.c` (`graphics {dust}` and its reply)
- Test: `pc/tests/ui_settings_probe.cpp`, `pc/tests/particles_smoke.py` (section `setting`)

- [ ] **Step 1: Write the failing checks**

In `pc/tests/ui_settings_probe.cpp`: add `extern int pc_dust;` to the `extern "C"` block, change `CHECK(pc_ui_settings_rows(G) == 16);` to `CHECK(pc_ui_settings_rows(G) == 17);`, and after the emissive checks (`pc_emissive = 1.0f;`) add:

```cpp
    // Dust (particles.h): after Glowing surfaces, Off / Low / High, Off by default
    CHECK(find(G, "dust") == find(G, "emissive") + 1);
    CHECK(value(G, "dust") == "Off");
    pc_dust = 2;
    CHECK(value(G, "dust") == "High");
    pc_dust = 0;
```

In `pc/tests/particles_smoke.py`, before `# In order:` add:

```python
def check_setting(s):
    """Dust is a setting: RPC graphics sets it, particles reports it, settings save writes it."""
    g = s.game
    r = g.call("graphics", dust=1)
    s.check(r.get("dust") == 1 and s.particles()["level"] == 1, f"graphics {{dust: 1}} ({r.get('dust')})")
    g.call("settings", action="save")
    text = (s.out / "settings.cfg").read_text()
    s.check("dust 1" in text.splitlines(), "settings save writes `dust 1`")
    g.call("graphics", dust=0)
    g.call("settings", action="save")
    s.check(s.particles()["level"] == 0, "and back to Off")
```

and add `check_setting` to `SECTIONS` after `check_viewport`.

- [ ] **Step 2: Run them and see them fail**

```bash
ninja -C pc/build ui_settings_probe ui_settings_probe_hooks && ctest --test-dir pc/build -R ui_settings --output-on-failure
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only setting
```

Expected: the probe's link fails or its row checks fail (`pc_dust` undefined / 16 rows); the smoke's `graphics {dust: 1} (None)` fails.

- [ ] **Step 3: The row, the stub, the panel, the RPC**

In `pc/src/ui/ui_settings.cpp`, add to the `extern "C"` block `extern int pc_dust;              /* particles.cpp: dust motes, 0 off, 1 low, 2 high */`, and after the "emissive" row's block (`g.push_back(r.grey(need_hw));` that closes it) add:

```cpp
    {
        static const char* const dust[] = {"Off", "Low", "High"};
        g.push_back(choice("dust", "Dust",
            "Motes of dust that glint where light crosses the air and vanish in shadow (rooms that have them). Off: the picture as before.",
            &pc_dust, dust, 3)
                        .grey(need_hw));
    }
```

In `pc/tests/ui_settings_stubs.cpp`, after `float pc_emissive = 1.0f;` add `int pc_dust; // particles.cpp: dust motes (0 off)`.

In `pc/src/dev_overlay.cpp`: add `extern "C" int pc_dust;` near the other externs at file scope (if `particles.h` is not included; or include `particles.h`), and in `graphics_panel()` after the "Glowing surfaces" slider's tooltip add:

```cpp
    ImGui::Combo("Dust", &pc_dust, "Off\0Low\0High\0");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Dust motes (particles: pc/docs/Particles.md) in rooms that have them: Off draws none (the\n"
                          "picture as before), Low dust_low of them, High all. Details > Atmosphere places and tunes them.");
```

In `pc/src/dev_rpc.c` `graphics`: after the `emissive` block that sets `pc_emissive` add

```c
        /* Dust motes (particles.cpp: 0 off, 1 low, 2 high). */
        extern int pc_dust __attribute__((weak));
        if (params && &pc_dust && number_value(member(params, "dust"), &n) && n >= 0 && n <= 2)
            pc_dust = (int)n;
```

and after the reply's `emissive` append add

```c
        if (&pc_dust && used < (int)sizeof result - 64)
            used += snprintf(result + used - 1, sizeof result - used + 1, ",\"dust\":%d}", pc_dust) - 1;
```

(`n` is the handler's `long long` for number values, as the `present_rate` and `scale` keys use it.)

- [ ] **Step 4: Run them and see them pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
ninja -C pc/build ui_settings_probe ui_settings_probe_hooks && ctest --test-dir pc/build -R ui_settings --output-on-failure
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 particles_smoke ui_system_smoke system_settings_smoke
```

Expected: `100% tests passed` (both probe variants); `3/3 passed` (the System > Graphics page's rows by id are unaffected by the new one).

- [ ] **Step 5: Commit**

```bash
git add pc/src/ui/ui_settings.cpp pc/tests/ui_settings_stubs.cpp pc/tests/ui_settings_probe.cpp pc/src/dev_overlay.cpp pc/src/dev_rpc.c pc/tests/particles_smoke.py
git commit -m "$(cat <<'EOF'
Dust setting: Graphics page row (Off, Low, High), Details > Graphics, RPC graphics {dust}; ui_settings_probe 17 rows

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 13: The cost, reported and bounded

**Files:**
- Test: `pc/tests/particles_smoke.py` (section `cost`)

- [ ] **Step 1: Write the section (it should pass at once: the pass and the CPU timer exist; this pins the budgets)**

In `pc/tests/particles_smoke.py`, before `# In order:` add:

```python
def check_cost(s):
    """4096 motes: Vulkan's pass under 0.3 ms of GPU time (RPC perf section "particles", ROOD_PERF=1),
    OpenGL's CPU path under 3 ms a frame (RPC particles cpu_ms)."""
    g, p = s.game, s.player
    s.particles(emitters=[{"id": EMITTER, "asset": "dust_motes", "pos": [round(p["x"]), round(p["y"] - 120), round(p["z"])],
                           "count": 4096, "size": [600, 300, 600]}], level=2, time=-1)
    g.run(30)
    if s.vulkan:
        g.call("perf", reset=True)
        g.run(240)
        sections = g.call("perf").get("sections") or {}
        cost = sections.get("particles")
        s.check(cost is not None and cost < 0.3, f"Vulkan: the particles' pass takes {cost} ms a frame for 4096 motes")
    else:
        g.run(120)
        r = s.particles()
        s.check(r["motes"] == 4096 and 0 < r["cpu_ms"] < 3.0,
                f"OpenGL: the CPU path takes {r['cpu_ms']:.3f} ms a frame for {r['motes']} motes ({r['path']})")
    s.particles(emitters=[{"id": EMITTER, "remove": True}], level=0)
    g.run(10)
    if s.vulkan:
        g.call("perf", reset=True)
        g.run(60)
        sections = g.call("perf").get("sections") or {}
        s.check("particles" not in sections, "Dust Off: no pass at all")
```

and add `check_cost` to `SECTIONS` after `check_setting`.

- [ ] **Step 2: Run it on both renderers**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only cost
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan particles_smoke
```

Expected: both pass; the printed numbers go into `pc/docs/Particles.md`'s cost table (Task 14). If the OpenGL path is over budget, lower the default `particles_cpu_rays` (the round-robin's refresh is the cost) and record why in the commit; never add threads or busy work to measure it.

- [ ] **Step 3: Commit**

```bash
git add pc/tests/particles_smoke.py
git commit -m "$(cat <<'EOF'
particles_smoke: the cost for 4096 motes (Vulkan perf section, OpenGL CPU time), none with Dust Off

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: Documentation

**Files:**
- Create: `pc/docs/Particles.md`
- Modify: `lighting/README.md` (the item kinds table; an "Emitters" section after "Fog volumes"), `pc/README.md` (settings table after `fog_volume_shadows`; RPC table after `environment` (fog); a "Dust and particles" paragraph after the fog volumes' own renderer's), `pc/docs/Vulkan_Renderer.md` (a "Particles" subsection at the end of "Haze, ground fog and sun shafts")
- Test: `pc/tests/lighting_layers_test.py` (the README's first example still parses: unchanged), the lint

- [ ] **Step 1: `lighting/README.md`**

In the kinds table, after the `fog` row add:

```markdown
| `emitter` | `emitter:<14 hex digits>` | a particle emitter: dust motes in a room's light (below, "Emitters") |
```

After the "Fog volumes" section (before "## The generated layer") add:

````markdown
## Emitters

Particles (pc/docs/Particles.md): `emitter <id>` items, placed in the editor
(Details > Atmosphere > Dust and particles: Add emitter) and edited like fog
volumes: click one's icon in the view or its row in the Outliner (Emitters)
and its fields are in Details (Basics, Spawn, Motion, Look, Transform; the
reset arrow goes back to its asset's value), with the Move / Turn / Size
gizmo. The graphics setting Dust (Off, Low, High) decides how many of an
emitter's particles are drawn; Off draws none.

```
emitter 68f0c0de000001 name="Window dust" asset=dust_motes pos=512,-260,384 size=640,360,480 seed=7 count=1200
```

- Its own fields: `name`, `asset` (one of `pc/assets/emitters/*.json`: every
  field below it does not write comes from it), `pos` (its centre), `yaw`
  (degrees about the vertical; 0), `enabled` (1), `seed` (its random
  choices; 1).
- From its asset unless written (defaults in brackets, all neutral):
  - spawn: `shape` (`box`, `room`: in the room's bounds, `point`: a ball of
    `radius`, `disc`: a horizontal disc of `radius`, `surface`: on the floor
    under its box; box), `size` (its box's full extents; 256,192,256),
    `radius` (64), `count` (particles alive at once at Dust High; 400),
    `lifetime` (seconds; 10), `lifetime_jitter` (the share of that a
    particle may live less; 0.5);
  - behaviour: `drift` (u/s, y down; 0,0,0), `curl`, `curl_scale`,
    `curl_speed` (turbulence: how far a smooth swirling field moves each
    one, the swirls' size, how fast they change; 0, 64, 0.1), `gravity`
    (u/s², y down: negative rises; 0), `drag` (per second; 0), `fade_in`,
    `fade_out` (seconds; 0, 0);
  - look: `render` (`pixel`: square game pixels, nearest), `pixel_min`,
    `pixel_max` (game pixels, 1..4; 1, 2), `color` (1,1,1), `brightness`
    (1), `variation` (how much dimmer each one may be; 0.5);
  - lighting: `lit` (0: they show only where light reaches them; 1: also
    `ambient` everywhere), `ambient` (0), `quality` (the setting that scales
    it: `dust`).
- A room draws up to 16 enabled emitters and 16384 particles.
````

- [ ] **Step 2: `pc/docs/Particles.md`**

Create it with these sections (prose in the style of `pc/docs/Glows.md`; measured numbers from Task 13):

- **What it is**: the port's particle system (not the game's effect particles); first use dust motes; later uses (fog emitters feeding the fog sim with the motes' positions and analytic velocities: brazier smoke, vents; window light shafts with a streak renderer).
- **Emitters and assets**: the item (lighting/README.md "Emitters"), assets (`pc/assets/emitters/README.md`), the editor (Atmosphere tab, Outliner, Details, gizmo, Show > Emitters), the library's entry points (`rood_particles::assets()`, `rood_particles::add_emitter(asset, pos)`, assets listed by `kind: "emitter"`).
- **Simulation**: the closed form (`particle_math.h` and its GLSL copy): cycles and seeds, spawn shapes, drift, gravity with drag, curl noise (six plane waves, divergence-free), fade, pixel size; game seconds since the room was entered; deterministic (RPC `particles {time}` freezes it).
- **Lighting**: in light only vs lit; the strongest `particles_lights` sources and the sun with a shadow ray each; which tracer (Vulkan ray queries with this frame's acceleration structure; OpenGL `pc_gi_trace_cpu` within `particles_cpu_rays`, round-robin; neither: unshadowed, RPC `path`); glows' light (`particles_glow`, `particles_glow_shadows`).
- **Drawing**: the key image and colour image, `particles_at` in the present shaders, additive before the fog volumes and the haze, lit view only, not in the menu's model view nor the F2 editor view; Dust Off: no pass, nothing bound (the picture pixel for pixel as before: `particles_smoke`).
- **Settings and RPC**: the six settings keys, Details > Atmosphere's section, Graphics > Dust, RPC `particles` (params and reply as in `particles.cpp`'s comment), `graphics {dust}`.
- **Cost**: Vulkan's perf section `particles` and OpenGL's `cpu_ms` for 4096 motes (the numbers Task 13 printed, with the GPU and the window size).
- **Limits**: 16 emitters, 16384 motes a room; glows treated as spheres for shadow rays; no shadows without a tracer; surfaces are floors only; one renderer (`pixel`).
- **Tests**: `particles_probe` (CTest), `particles_smoke` (its sections).

- [ ] **Step 3: `pc/README.md` and `pc/docs/Vulkan_Renderer.md`**

In `pc/README.md`'s settings table, after the `fog_volume_shadows` row add:

```markdown
| `dust` | 0 | dust motes in rooms that have them (pc/docs/Particles.md): 0 Off (the picture as before), 1 Low, 2 High; Graphics > Dust |
| `dust_low` | 0.4 | the share of each dust emitter's particles Low draws |
| `particles_glow`, `particles_lights`, `particles_glow_shadows` | 1, 2, 1 | the glows' light on particles; the strongest sources each is lit by (with a shadow ray each, and the sun); glows' light shadowed too |
| `particles_cpu_rays` | 1536 | OpenGL: the particles' shadow rays a frame (their light refreshed round-robin) |
```

In the RPC table, after the `environment` (fog) row add:

```markdown
| `particles` | `level` (Dust 0-2), `low`, `time` (seconds, -1 live), `debug`, `settings` {`glow`, `lights`, `glow_shadows`, `cpu_rays`}, `reload_assets`, `emitters` [{`id`, `remove`, `name`, `asset`, `pos`, `yaw`, `enabled`, `seed`, any asset field}], `add` {`asset`, `pos`}, `select`, `motes` {`emitter`, `offset`, `max`} | the settings, `renderer`, `path` (`traced`, `unshadowed`, `off`), `motes`, `cpu_ms`, `display`, `view`, `assets`, `emitters` (with their resolved `fields` and `drawn`), `mote_list` (each mote's `pos`, `cell`, `size`, `sz`, `colour`, `light`, `open`, `won`) |
```

After the fog volumes' own renderer's paragraphs add a short "#### Dust and particles" paragraph pointing to `pc/docs/Particles.md` (what Dust does, where emitters are placed, the settings).

In `pc/docs/Vulkan_Renderer.md`, at the end of the "Haze, ground fog and sun shafts" section (before "## Status (2026-10-11): NRD"), add a paragraph "Particles (`vk_particles.inc`, `particles_main.glsl`)": one compute pass after the fog volumes' passes, the two pipelines, bindings (0 UBO `pc_particles_gpu`, 1-3 and 9 `rt_common.glsl`, 4 the light list, 5 the R32_UINT key image, 6 the RGBA32F colours), the clear, `imageAtomicMin`, the present's bindings 21-22, perf section `particles` and its measured time.

- [ ] **Step 4: Check the docs**

```bash
ctest --test-dir pc/build -R lighting_layers --output-on-failure
python3 pc/tools/lighting_lint.py
git diff main -- pc/docs lighting pc/README.md pc/assets/emitters | grep "^+" | grep -n "$HOME\|/tmp/" || echo "no local paths"
```

Expected: `100% tests passed`; lint exit 0; `no local paths`.

- [ ] **Step 5: Commit**

```bash
git add pc/docs/Particles.md lighting/README.md pc/README.md pc/docs/Vulkan_Renderer.md
git commit -m "$(cat <<'EOF'
Docs: particles (emitters, assets, closed-form simulation, lighting, drawing, settings, RPC, cost, limits); the emitter item kind; Dust settings

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 15: The example: dust in the town house (zone 34 room 0)

**Files:**
- Create (only if subtle): `lighting/default/rooms/zone34_room00.txt`
- Modify: `pc/tests/particles_smoke.py` (section `example`); otherwise `lighting/README.md` (the example's line under "Emitters", marked as an example to place)

- [ ] **Step 1: Look at the room**

Write a throwaway script in the scratchpad (never committed) that launches the save room checkpoint like `particles_smoke.py`'s `start()` (same scratch `ROOD_LIGHTS` and settings), warps with `room_goto` to zone 34 room 0 (`emissive_smoke.py`'s `goto`), and prints: `environment` `bounds`, `glows` (each glow's `position`, `category`, `key`, `name`), Ashley's position, and saves the game view with Dust Off. Run it under the lock. Read the screenshot with the Read tool: where the windows are, which part of the air their light crosses in the game's view.

- [ ] **Step 2: Place the emitter and measure it**

With the same script (still in the scratchpad), add an emitter over RPC: `asset=dust_motes`, `pos` between the lit windows' glows and the middle of the room (the windows' glows' mean position moved about 40 % of the way toward the bounds' centre, at the windows' height), `size` covering the windows' light across the room (start from 640,360,480; yaw so its long side follows the window wall: `atan2` of the wall's horizontal normal), `seed` 7, `count` 1200. At Dust High with the time frozen, capture the game view with Dust Off and High (paused, 40 presents each), and compute on the game view: the share of pixels that change and the mean change (0..255), and how many motes glint (`particles {motes}`: `won` and `light` > 0.05). Read both screenshots.

Iterate (position, size, count, `brightness`) until: the motes glint in the windows' light (at least 10 lit, owning motes), the change is subtle (at most 2 % of the view's pixels change, mean change under 1.5 / 255), and nothing glints in the room's dark parts. If no placement meets all three (or the windows do not light the air on a renderer: no emissive glows in `glows`), the example becomes documentation (Step 4, second branch).

- [ ] **Step 3: The failing section**

In `pc/tests/particles_smoke.py`, before `# In order:` add (with the authored id you will commit, `68f0c0de000001` below):

```python
EXAMPLE = "68f0c0de000001"  # lighting/default/rooms/zone34_room00.txt


def check_example(s):
    """The town house (zone 34 room 0): its authored dust glints in the windows' light, subtly."""
    g = s.game
    g.call("pause", paused=False)
    g.call("room_goto", zone=34, room=0, enemies=False, scripts=False)
    for _ in range(120):
        g.run(20)
        if g.call("room_goto", action="status")["phase"] in (0, 4):
            break
    g.run(60)
    s.check(g.state()["zone"] == 34, "warped to the town house (zone 34 room 0)")
    e = s.emitter(EXAMPLE)
    if not s.check(e is not None and e["asset"] == "dust_motes", f"its authored dust is there ({e and e['fields']})"):
        return
    g.call("post", layer="room", clear="all", snap=True)
    g.call("pause", paused=True)
    s.particles(level=0, time=12.0)
    off = s.shot("example-off", 40)
    s.particles(level=2)
    on = s.shot("example-high", 40)
    r = s.particles()
    (vx, vy, vw, vh), _, _ = view_cells(r)
    change = np.abs(on - off)[vy:vy + vh, vx:vx + vw].max(axis=2) * 255
    share, mean = float((change > 2).mean()), float(change.mean())
    glints = [m for m in motes(s, EXAMPLE) if m["won"] and m["light"] > 0.05]
    windows = [x for x in g.call("glows")["glows"] if x.get("origin") == "emissive" or "E002" in str(x.get("key", ""))]
    if windows:  # where the windows light the room on this renderer
        s.check(len(glints) >= 10, f"its motes glint in the windows' light ({len(glints)} lit)")
    else:
        print("info: no window glows on this renderer: the motes stay dark (in light only)")
    s.check(share <= 0.02 and mean < 1.5, f"subtle: {share * 100:.2f} % of the view changes, by {mean:.2f}/255 on average")
    s.particles(level=0)
    g.call("pause", paused=False)
```

and add `check_example` last in `SECTIONS`. Check the `glows` RPC's reply for how an emissive glow is recognised (its `origin` or `category` field: `glow_overrides.h` `PC_GLOW_ORIGIN_*`) and use that field in `windows` instead of the guesses above.

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only example
```

Expected: `FAIL: its authored dust is there (None)`.

- [ ] **Step 4: Ship it (first branch) or document it (second branch)**

First branch, the measurements of Step 2 met the criteria: create `lighting/default/rooms/zone34_room00.txt`:

```
rood-lighting 2
# zone 34 room 0 (the town house): dust motes in the light of its big windows (lighting/README.md "Emitters")
emitter 68f0c0de000001 name="Window dust" asset=dust_motes pos=<x>,<y>,<z> yaw=<deg> size=<w>,<h>,<d> seed=7 count=<n>
```

with the measured values, canonical (`python3 pc/tools/lighting_lint.py --fix lighting/default/rooms/zone34_room00.txt`, then check the file reads as written). The user's own `pc/user/lighting/zone34_room00.txt` in MAIN is not touched (it layers on top; it has no emitter).

Second branch, they did not: add the line under `lighting/README.md` "Emitters" as "An example to try (Details > Atmosphere > Add emitter, then set these fields): ..." with the closest placement found and what kept it from shipping, and change `check_example` to add that emitter over RPC first (`s.particles(emitters=[{...}])`) instead of expecting the authored one.

```bash
python3 pc/tools/lighting_lint.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/particles_smoke.py --only example
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan particles_smoke emissive_smoke
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 particles_smoke emissive_smoke
```

Expected: lint exit 0; `PASS: particles` both ways; `emissive_smoke` still passes (Dust is Off by default, so its frames of 34/0 do not change).

- [ ] **Step 5: Commit**

```bash
git add lighting/default/rooms/zone34_room00.txt pc/tests/particles_smoke.py   # or lighting/README.md in the second branch
git commit -m "$(cat <<'EOF'
Example: dust motes in the town house's window light (zone 34 room 0), subtle at Dust High; particles_smoke checks it

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 16: Full verification

**Files:** none new (fixes only, each its own commit).

- [ ] **Step 1: Every build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows
```

Expected: all four exit 0 with a `Built ...` line and no `error:` or `warning:` line from the new files (MinGW: no identifier clashes with `windows.h`; `strcasestr` as `atmosphere.cpp` uses it).

- [ ] **Step 2: Every suite the feature touches, both renderers**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest lighting_lint particles editor atmosphere fog glow emissive ui_system system_settings
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan particles editor atmosphere fog glow emissive
```

Expected: the same pass list as Task 1's baseline plus `particles_smoke` and the `ctest` additions; no new failure. A flaky suite is re-run once alone (never with extra load); a second failure is a bug to fix (superpowers:systematic-debugging).

- [ ] **Step 3: Pixel identity with Dust Off, once more by eye**

Read `picture-off.png` and `picture-disabled.png` from both renderers' `particles_smoke` output: identical; and `picture-lit.png`: square glints of one or two game pixels.

- [ ] **Step 4: Review**

Use superpowers:requesting-code-review on the branch (`git diff main...claude/dust-motes`), with these points to check: Dust Off runs no pass and binds nothing new (`pc_particles_cpu_frame` / `pc_vk_particles_update` return before any work); `particle_math.h` and `particles_sim.glsl`, `particles.cpp light_at` and `particles_main.glsl mote_light` are the same algorithm; no constant controls the look (every number is an emitter field or a settings key); no committed file names a local path or another game's port. Fix what it finds, each fix its own commit.

- [ ] **Step 5: Hand over**

Do not merge or push. Leave the worktree; report the branch, the commits, the measured costs, the example's measurements and screenshots' paths, and that the user turns the effect on with Graphics > Dust (default Off).
