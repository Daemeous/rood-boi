# Probe GI (DDGI)

The indirect light of the Medium and High lighting presets (`gi_method
probes`): a grid of irradiance probes per room, after Majercik et al. 2019,
"Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields".
Cheap, stable and smooth, and it runs without ray tracing hardware. The
path tracer (`gi_method path`, Ultra) stays the reference.

    graphics {lighting_preset: "high"}        # or gi_method: "probes"
    probe_gi {dump: "/tmp/field.bin"}          # state, and the field to a file
    python3 pc/tests/run.py --arch 64 --renderer vulkan probe_gi

## Design

- **Grid** (`probe_gi.inc probe_fit`): fitted to the captured room's bounds
  at cell centres (no probe on the outer shell), `probe_spacing` (world
  units; Medium/High 128, 96 when Ultra falls back to probes) grown by 5 %
  steps until the count fits `probe_budget` (4096). Refitted, atlases and
  probe state cleared, when the room capture changes (`pc_scene_serial`) or
  the spacing/budget setting does. First room: 1536 x 1472 x 1672 units ->
  12 x 12 x 13 probes at 128, 16 x 15 x 17 at 96.
- **Atlases**: irradiance 8 x 8 octahedral texels per probe plus a one-texel
  border (RGBA16F), visibility 16 x 16 + border (mean and mean square of the
  hit distance in units of the largest spacing, clamped at 2), probe data
  (offset, light scale; RGBA32F). Borders are written by the blend pass from
  shared memory across the octahedron's folded edges.
- **Update, every presented frame** (`probe_trace*.comp`, `probe_state.comp`,
  `probe_blend.comp`):
  1. *Trace*: `probe_rays` rays per active probe (Medium/High 32/64;
     spherical Fibonacci, a new random rotation per update). A hit is shaded
     like a path vertex of the path tracer (`rt_trace.comp`'s `direct` and
     `emitted`: every dynamic light with a shadow ray, one glow picked by its
     unshadowed light, the baked bounce), plus the field itself at the hit
     with `bounces >= 2` (multi-bounce through last update's atlases), and
     the room's sun (its own shadow ray, Vulkan_Renderer.md "Sun and
     sky"). Misses bring the room's sky (nothing in a roofed room). Back faces (the room is NCLIP-culled, so its
     winding is reliable: front faces' `cross(b - a, c - a)` points along the
     ray) store -0.2 x their distance. Characters and props are not in the
     probes' scene (they would make the field flicker); their own contact and
     glow shadows stay with the per-pixel trace.
  2. *State*: during the first 8 updates in a room, relocation (a probe with
     more than 25 % back faces moves through the nearest one; one too close
     to a front face moves away; offsets within 0.45 of the spacing); at
     update 8 classification (inactive: still inside geometry, or no surface
     within its cell's diagonal — they light nothing). Inactive probes are
     neither traced nor sampled (first room: 1961 of 4080 active, 985 of
     1872). Every update the probe's *light scale* (below).
  3. *Blend*: one 8 x 8 workgroup per probe; this update's rays in shared
     memory; irradiance texels cosine-weighted (back faces left out),
     visibility texels with cosine^50; hysteresis `probe_hysteresis` (0.97),
     but a running mean (1 - 1 / (n + 1)) while the history is shorter, so a
     reset converges in a few updates without a noisy first frame. RTXGI's
     faster adaptation above a change threshold is off: with a few dozen rays
     the noise of dim texels crossed it constantly (5 % frame-to-frame change
     on room surfaces with it, 0.8 % without).
- **Lookup** (`probe_common.glsl probe_irradiance_at`): the eight probes of
  the cell, trilinear x wrapped cosine toward the probe x Chebyshev
  visibility (cubed, floor 0.05, small weights crushed), the point biased
  0.2 x spacing along the normal and 0.3 x spacing toward the eye. It is the
  same quantity as the path tracer's indirect light (the cosine-weighted mean
  of the radiance arriving), so the present multiplies albedo as before:
  - Vulkan with a traced result this frame (traced shadows or AO): at GI
    resolution into the result's rgb (`probe_apply.comp`; the traced
    indirect light is off with probes), upsampled by the present's joint
    bilateral lookup exactly where the path tracer's light would go;
  - Vulkan without one (shadow maps, no ray tracing): `probe_surface.comp`
    reconstructs the GI pixels from the G-buffer and writes the field into
    its own images, which the present binds in place of the traced result
    (`gi_on` 256);
  - OpenGL (and `ROOD_PROBE_PER_PIXEL=1` on Vulkan): per display pixel in the
    present shader (`gi_on` 64; the GLSL is shared through
    `pc/tools/gl_shaders.py`).
  Glow light and shadow visibility still come from the trace when it runs.
- **Changes**:
  - room change: refit, clear, relocate again (test: 16 x 15 x 17 ->
    15 x 10 x 23 when the opening moves on to the next room);
  - light change (`probe_begin`: a hash of the lights at their *base*
    intensity, the baked bounce, emitter light, bounces, glow shadows, light
    size): convergence restarts from a 3-update history (a dragged light
    stays smooth);
  - animated lights (light actors' flicker and pulse): the probes are traced
    with animation off (`probe_overlay.cpp pc_probe_lights_base`), so flicker
    neither resets nor smears the field; `probe_state.comp` gives each probe
    a per-frame scale, the current / base intensity of each light weighted
    by its unshadowed reach at the probe, applied in the lookup — the bounce
    flickers with the direct light, locally;
  - the game's room tint fades change only the baked vertex colours the
    present already draws; the probes' bounce (dynamic lights) follows the
    dynamic direct light, which the tint does not touch either. The baked
    bounce (`gi_baked_bounce`, 0 by default) uses the captured colours.

## Backends

| Renderer | Trace | Scene |
| --- | --- | --- |
| Vulkan with ray queries (backend 1) | `probe_trace_rq.comp` | a TLAS over vk_rt.inc's room BLASes (opaque, forced opaque; cut-outs with the per-texel test) |
| Vulkan without (backend 2; `ROOD_VK_RT=0`, or `ROOD_PROBE_TRACE=bvh`) | `probe_trace_bvh.comp` | ptgi.c's binned-SAH BVH of the room, built on the CPU once per room capture, uploaded |
| OpenGL 4.3 compute (backend 3; 32 and 64 bits) | the same `probe_trace_bvh.comp` text | ptgi.c's GL BVH (its room root) |

One shader source for both APIs: the probe passes are written in GLSL that
compiles as Vulkan GLSL 460 (vk_shaders.py) and as GL 4.30
(`pc/tools/gl_shaders.py` inlines the includes into `gl_shaders.h`; unique
binding numbers, image units < 8). A BVH rather than a voxel/SDF scene:
ptgi.c already builds it, it is exact (cut-outs, thin walls), and the no-RT
trace costs 0.9 ms on the Intel iGPU at Medium. `pc_probe_gi_available()`
reports probes on Vulkan and on OpenGL with compute; the presets
(lighting_quality.cpp) fall back to `off` without them.

Files: `probe_gi.h`, `probe_gi.inc` (end of ptgi.c: settings, grid, resets,
OpenGL backend, readback), `vk_probes.inc` (end of vk_rt.inc), `probe_overlay.cpp`
(settings panel under the lighting panel's GI method, the probes drawn as
spheres in the editor view with `probe_show`, base lights),
`pc/shaders/vulkan/probe_*`. Hooks: ptgi.c (include, GL update), vk_rt.inc
(include), vk_present.c / gl_present.c / scaled.frag (bindings 12-15, `gi_on`
bits 64/128/256), lighting_quality.cpp (keys `probe_spacing`, `probe_rays`,
the panel), dev_rpc.c (`probe_gi`, `graphics {probe_hysteresis,
probe_budget, probe_show}`), lights.cpp (the editor drawing).

Settings: `gi_method` (lighting quality key), `probe_spacing`, `probe_rays`
(lighting quality keys, set by the presets), `probe_hysteresis`,
`probe_budget`, `probe_show` (0 off, 1 active probes, 2 also inactive ones,
red outlines; editor view).

## Measurements (2026-10-11)

First room after the opening pan, a test light 700 units ahead of the
camera; GPU time from `ROOD_PERF` (median of 300-frame windows; the iGPU
shares power and memory with the CPU, numbers vary by ~20 %).

| GPU, preset | probe trace | state | blend | lookup | probes in all |
| --- | --- | --- | --- | --- | --- |
| Intel Arc iGPU (ARL), Medium, ray queries | 0.33 | 0.03 | 0.85 | 0.26 (apply, GI res 1.5) | **1.5 ms** |
| Intel Arc iGPU, Medium, `ROOD_VK_RT=0` (compute BVH) | 0.93 | 0.02 | 0.85 | 0.36 (surface pass) | 2.2 ms |
| Intel Arc iGPU, 96 units / 64 rays (4080 probes) | 0.99 | 0.03 | 2.72 | 0.28 | 4.0 ms |
| NVIDIA RTX PRO 6000, Medium | 0.016 | 0.004 | 0.010 | 0.010 | 0.04 ms |
| NVIDIA RTX PRO 6000, 96 units / 64 rays | 0.024 | 0.004 | 0.020 | 0.010 | 0.06 ms |

The path tracer at Ultra costs 3.4 ms trace + 3.2 ms NRD on the iGPU and
0.18 + 0.20 ms on NVIDIA. Per display pixel (the first version) the lookup
cost 1.0 ms on the iGPU at 1280 x 960; at GI resolution 0.26-0.36 ms. The
blend is bound by per-probe fixed costs on Intel (0.53 ms with its loops
removed), not by the ray loops.

Against the path tracer (Ultra's raw samples averaged over 24-32 updates,
per GI pixel on room surfaces):

- First room: probes 1.15-1.20 x the path tracer's indirect light (probe
  interpolation spreads the light a little further; characters neither
  occlude nor bounce probe light, so they are brighter). Displayed images
  (Vulkan): indirect-light view mean 27.7 (Medium) / 27.5 (High) vs 21.7
  (path traced, NRD). OpenGL: 27.8 vs 18.7 (the GL path tracer is darker).
- Frame to frame (still camera): 0.8 % of the light.
- Leak test (light in the corridor beside the hall, free camera in the hall
  facing the doorway, probes 256 apart so cells straddle the wall): where the
  path tracer finds no indirect light (50 % of the view) the probes give
  8.5 % of their mean light, mostly on the floor and jambs near the doorway.
- Save room (zone 9 room 17 from the user's save, a light by Ashley; 7 x 10 x 12
  probes at 128, 534 active): High 1.17 x, Medium 1.16 x the path tracer's
  indirect light on room surfaces.
- A flickering brazier actor (flicker 0.8): no light resets over 40 frames
  (base intensities hashed), and the probes' indirect light follows the
  flicker frame by frame (0.056-0.161 around a mean of 0.101).

Test: `pc/tests/probe_gi_smoke.py` (run.py suite `probe_gi_smoke`, every
renderer): backend, grid, classification, convergence (field mean within
3 % over 40 vsyncs), energy vs path traced (Vulkan per GI pixel 0.7-1.5;
OpenGL indirect view 0.6-1.7), stability (< 3 %), the wall leak (< 20 %),
light reset, room change refit, no crash / validation / shader errors.

## Next

- Blend cost on integrated GPUs: update a rotating subset of probes per
  frame, or a compacted active list (the readback after classification).
- Characters as probe blockers (optional mover BLASes) if their light leak
  through them shows in play.
- Sky/outdoor rooms: done (2026-10-12): misses bring the room's sky, the sun
  lights every hit (Vulkan_Renderer.md "Sun and sky"). Open-air probes leak
  the sky a little more than the path tracer (1.4-1.5 x in Snowfly Forest).
