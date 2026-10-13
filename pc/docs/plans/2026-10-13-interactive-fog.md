# Interactive ground fog and hands-on fog volumes (design + prototype)

Status 2026-10-13: design for review; a minimal prototype is in the tree
behind an off-by-default per-volume flag (`fog <id> ... sim=1`, section
"Prototype"). Nothing here is on in any room by default.

## Goals

- **Low ground fog that responds to the characters** in chosen areas
  (Snowfly Forest clearings, the Undercity's flooded halls, the town's
  lower streets at night): Ashley and the enemies part it around their feet
  and leave a short, soft wake when they move; it settles back within a few
  seconds. Subtle: it must read as atmosphere, never as an effect that
  draws the eye, and never hide the floor, the enemies or the pickups.
- **Readable and PS1-faithful.** The game's look is low-resolution textures
  drawn with nearest filtering and a fixed camera; the fog is a smooth,
  slow, low-frequency layer *between* the camera and those texels (they are
  never blurred), it sits low (ankle to knee height by default), and every
  limit of the medium (2026-10-13: `haze_max_opacity`, `haze_max_light`,
  `haze_max_forward`, `haze_shafts`) applies to it as to everything else.
- **Much more hands-on tools**: placing, shaping and painting fog in the
  editor the way one paints lights now, with presets, paths, paint brushes
  for density and flow, per-volume interaction, and a live preview — the
  user tunes the game's look interactively, so the tooling is the feature.
- **One medium for every renderer**: whatever is authored feeds the same
  `medium.glsl` record the Vulkan froxel haze, the OpenGL march (ptgi.c) and
  the Low (unshadowed) haze already read, so it works on 32-bit GL, 64-bit
  GL and Vulkan RT, in the editor view, and in the ray-traced haze (shafts
  through parted fog) with no extra path.
- **Deterministic**: stepped by vsyncs (the RPC's stepping, the cinematic
  tool's seeking), so tests and recordings reproduce.

## References (concepts only)

- *FluidNinja* (an Unreal plugin): 2D fluid simulations, baked to flipbooks
  or run live, drive volumetric fog volumes; characters push the smoke;
  lit by the scene's lights; the look does not depend on the camera angle
  because the fog lives in the world, not on screen.
- *Ultra Volumetrics* (an Unreal plugin; the user's manual, read for its
  feature list only): stackable box fog volumes with saved **presets**;
  **spline** actors that populate volumes along a path (spacing, scale per
  point, follow the curve, closed loops) and animate them (fill a corridor
  over time, retreat, travel along it); a **third-person component** that
  keeps a radius around the player (and a second character) clear, per
  volume opt-in, radius animatable; **interaction**: 3D carving (sphere,
  cone for a flashlight, projectile paths) and 2D trails, bursts and rings
  for low fog, with a performance-saver mode that only ticks while the
  player is inside a volume; **paint mode**: paint density in or out and a
  velocity (flow) brush, saved to a texture per volume; **quality
  triggers** per area and near/far fade distances; **sequencer** rendering
  (parameters animated in cinematics, an update hook so renders match the
  viewport); **path tracing** support through the engine's heterogeneous
  volumes.

What maps to rood-boi: volumes we have (box/sphere, gizmo, 16 per room);
presets, paths, paint, interaction and quality are what is missing.
Distance-field carving and 3D interaction are out of scope (no distance
fields; the PS1 rooms are small and the camera fixed).

How those plugins are organised, as general inspiration (from the names of
their parts only; nothing of theirs is used here):

- *FluidNinja*: a classic 2D stable-fluids loop (add density, add
  velocity, advect velocity, advect density, dissipate), whose live variant
  feeds tracked actors' positions and motion in as sources; the 2D density
  drives volumes by ray marching it extruded into a height volume; the
  low-resolution sim density is *detail-mapped* with a high-frequency
  noise texture (a baked volume-noise utility makes that texture), so a
  coarse field still reads as fine smoke; baked sims play back as
  flipbooks or flow maps where nothing needs to react; field and flow
  presets are data (a "painter" preset lets actors paint trails).
- *Ultra Volumetrics*: one box fog actor with many named presets (small,
  large, layered, edgeless, organic, distorted, rotating; a ground variant
  for low fog; a "stamp" for a local density decal; variants hugging
  nearby geometry), spline population, a quality-trigger volume, paint
  mode, a separate fog-scattering pass and a path-traced variant;
  interaction through a controller plus a trail component per character or
  object writing into a 2D interaction field, with effect stamps (ring,
  burst, vortex, swing, jump, projectile), blockers, and an overlap
  interface that tells each volume who is inside.

Takeaways folded into this plan:

1. A 2D top-down density + velocity field per volume at low resolution
   (stable-fluids-lite: add, advect, dissipate; the prototype), shaped by a
   height profile for ground fog, and **detail-mapped** at render time by
   a higher-frequency 3D noise. The 2026-10-13 quality pass already
   replaced the blotchy value noise with domain-warped gradient noise
   computed in the shader; a baked tileable 3D noise texture (64^3, 2-3
   octaves of Perlin-Worley, built once at startup by a compute pass, so
   nothing is shipped) is the next step: cheaper per sample than ALU
   noise and richer (Worley billows), sampled with trilinear filtering,
   its contrast faded out near the volume's soft edges.
2. Actors as sources: radius from their bounds (the prototype uses one
   radius for all), velocity from their motion; later **action stamps**
   from the game's own events (a weapon swing = a vortex, a spell or a
   landing = a burst/ring) through the same splat.
3. Presets as data (lighting-pack items referencing project presets) and
   per-volume quality; a **stamp** item (a local density decal: a
   doorway's draft, a drain's steam) next to box volumes.
4. Editor: the box gizmo (have), paint density and flow brushes, stamps,
   path fill, a presets dropdown, the interaction toggle (have, prototype),
   preview with a ghost actor.
5. A non-interactive **flow** mode for cheap areas: the field advected by a
   painted or preset flow map with no actors (or a baked loop played
   back), so ambient motion costs one texture fetch.
6. Who is inside: the sim only ticks while an actor stands in or near the
   volume's footprint (the plugins' overlap idea); idle volumes keep their
   rested field.

## Options for the interaction

| Option | What | Look | Cost | Verdict |
| --- | --- | --- | --- | --- |
| A. Analytic carve | each actor subtracts a soft capsule from the density in the shader | a clean hole that moves with the actor, no wake, no memory | ~0 (a few ALU per sample per actor) | fallback for Low quality and for 3D effects (a spell's sphere) |
| B. 2D height-field sim | per volume, a top-down grid (32-64 cells a side) of a density multiplier and a velocity, advected + diffused + relaxed each vsync; actors inject velocity and remove density | holes that open as you walk in, a wake that trails and curls with the wind, fog piling softly at the edge, refilling over seconds | CPU 32x32: ~0.02 ms; GPU 64x64: < 0.01 ms; +1 bilinear fetch per medium sample inside the volume | **chosen** |
| C. 3D fluid in a froxel/volume grid | Navier-Stokes in 3D | rolling, billowing fog | 1-3 ms, memory, tuning | overkill for low fog under a fixed PS1 camera |
| D. Baked flipbooks | FluidNinja's baked mode: a looping 2D sim baked offline | ambient motion (steam over a vent, smoke from a brazier) | a texture fetch | later, for authored ambient motion; complements B |

B is the FluidNinja idea at PS1 scale: the fog is low, so a 2D field (x, z)
times a height profile is enough; the march already samples a density per
point, it only multiplies one more factor in.

## The simulation (B)

Per simulated volume, a grid of N x N cells over the volume's footprint
(its own turned frame, so a long thin volume down a corridor gets cells
along it):

- `d` density multiplier (1 at rest, 0 cleared, up to 1.5 piled), `u, v`
  velocity in cells per second.
- Each vsync (dt = 1/59.94 s, at most 8 per frame; a room change or a jump
  of over 10 s restarts it at rest):
  1. **Actors** (Ashley and every enemy whose feet are inside the footprint
     and within the volume's height + a margin): velocity from their last
     move (the game moves them on game frames, 1-4 vsyncs apart; standing
     still after 6 vsyncs); a Gaussian footprint of radius R (44 units)
     pulls `d` toward a hole floor (1 - strength x 0.8) faster when moving,
     sets the velocity toward the actor's (x 0.25: a little drag; the cleared trail stays behind it) plus a little
     outward flow, and puts 60 % of what it removed into a ring at 1-2.4 R
     (fog pushed aside, capped at 1.5).
  2. **Advection**, semi-Lagrangian (bilinear back-trace) of `d`, `u`, `v`
     by the velocity plus the room's fog wind, so a wake drifts downwind
     with the wisps; outside the grid is rest.
  3. **Diffusion** (a 4-neighbour blur, 0.6 / s), **damping** of the
     velocity (e^-1.8 per second), **relaxation** of `d` toward 1 with the
     volume's `sim_recover` (3 s).
- No pressure projection: the field is not an incompressible fluid, only a
  convincing one at this scale; projection (a few Jacobi iterations) is a
  later option if swirls are wanted.
- Determinism: everything is a function of the vsync count and the actors'
  positions; no wall clock. The cinematic tool can seek by re-running from
  the cut's start, or snapshot the field (16 KB per 64x64 volume).
- Wind: the room's fog wind (`env fog wind=`) carries it, so parted fog
  fills back from upwind first.

### Rendering

`medium_volume()` multiplies the volume's density (shape, soft edge,
wisps) by the field at the point's footprint coordinates (bilinear). The
height profile stays the volume's (a low box with a soft top edge reads as
ground fog; an `exp` profile like the ground fog's is a small addition:
`profile=exp height=..`). Because the field is in the world, the result is
the same from every camera angle, lit by the lights, the moon and its
shafts, and traced like the rest of the medium.

Extending the same field to the room's ground fog (`env fog sim=1`) is
direct: its footprint is the room's floor grid (already a top-down grid
over the room's bounds, `pc_scene_floor_grid`).

### Data path

- Prototype: the field of **one** simulated volume rides in the medium
  record (`atmosphere.h` `PC_MEDIUM_SIM`: 32 x 32 cells, 8-bit, three per
  float, 86 vec4s; the record grows from 163 to 250 vec4s, uniforms on
  OpenGL and UBOs on Vulkan, no new bindings).
- Production: an R8/R16F 2D texture array (one layer per simulated volume,
  64 x 64) bound to the four passes that read the medium (rt_haze,
  rt_haze_integrate, scaled.frag; ptgi.c and gl_present.c on GL); the sim
  as a compute shader on Vulkan (two ping-pong images, 3 dispatches per
  vsync) and the CPU path above on GL (or a GL compute port).

## Editor tooling

The Atmosphere tab already lists volumes with a move/size/turn gizmo. Add:

1. **Presets** (`pc/assets/fog_presets/*.json`: name, every volume field,
   optional profile): "Ankle mist", "Knee fog", "Sewer steam", "Forest
   floor", "Doorway draft"; *Apply* to the selection, *Save as preset* from
   it. A volume keeps `preset=<name>` plus only the fields that differ, so a
   preset edit updates every volume using it (the fixture-type idea the
   user liked for lights).
2. **Paint mode** (top-down brush in the view, like the surface snapping
   cursor): *density* (paint fog in or out: a per-volume mask, 64 x 64,
   multiplied in like the sim's field), *flow* (paint a velocity field the
   sim and the wisps follow: a draft through a doorway), *erase*; brush
   radius, strength, falloff on the wheel; undo per stroke
   (`editor_undo`). Stored per volume as a small text-encoded grid in the
   lighting pack (see "Data format"), so it diffs and merges.
3. **Path fill** (`fogpath <id>`): click points along a corridor or a
   street; it lays volumes along the polyline (spacing, width and height
   per point, follow the curve, closed loop), all sharing one preset; the
   path is the authored item, the volumes are derived (regenerated on
   edit). Later: an animated fill (fog creeping down a corridor in a
   cutscene) as a 0..1 `fill` parameter the cinematic tool can key.
4. **Interaction per volume**: a checkbox "Characters part it" (`sim=1`),
   strength, recovery time; a "Reveal radius" option (Ultra Volumetrics'
   third-person component: keep a radius around Ashley clear in dense
   volumes so she never disappears, independent of the sim).
5. **Live preview**: the sim runs in the editor's free camera too; a
   "drag a ghost actor" mode (hold a key, the mouse's surface point is an
   actor) to preview the wake without walking Ashley; a top-down debug
   overlay of the field (heat map on the volume's floor); "Show the haze
   alone" already exists.
6. **Quality per area**: a room setting `haze_quality_min` / `max` (a dense
   sewer room may ask for traced haze; an iGPU preset may drop sims to the
   analytic carve), and per-volume near/far fade distances.
7. **Cinematics**: volume parameters and the path fill keyable in the
   cinematic tool; the sim seeks deterministically (above).

## Data format (lighting packs)

Items keep stable ids and live in the usual layers (generated, authored
`rooms/`, the user's `pc/user/lighting`); authored overrides by id work
unchanged.

```
fog 7c1e0a9b3d55f2 name="Clearing mist" preset=ankle_mist pos=320,-24,640 size=900,64,700 yaw=15 sim=1 sim_strength=1 sim_recover=3
fogmask 7c1e0a9b3d55f2 n=64 density=<base64 of 64x64 u8> flow=<base64 of 64x64x2 s8>
fogpath 2b9d41c07a3e18 name="Sewer channel" preset=sewer_steam points=0,0,0;400,0,40;800,-20,160 width=180 height=60 spacing=200
```

- `sim`, `sim_strength`, `sim_recover` are fields of `fog` (the prototype
  has them; `lighting_layers.cpp` and `lighting_format.py` schemas).
- Masks are separate items keyed by the volume's id so a big blob never
  sits in the middle of a volume's line, and a layer can override the mask
  without the volume (or the reverse).
- Presets are project assets (JSON), referenced by name; unknown preset =
  the fields as written.

## Cost budget

| Part | Budget (RTX PRO 6000, 1440 x 1080) | iGPU / Low |
| --- | --- | --- |
| Sim, 4 volumes 64 x 64, GPU | < 0.02 ms | CPU 32 x 32 < 0.1 ms |
| Medium sampling (+1 fetch per sample in a sim volume) | < 0.01 ms in the froxel pass | +5 % of `present` with Low haze |
| Paint masks | one more fetch | same |
| Total atmosphere (haze + fog + volumes + sims) | < 0.15 ms (today 0.04-0.08 ms) | < 0.5 ms |

The prototype's CPU sim (one 32 x 32 volume: three advected fields, a
splat per actor) is about 10^4 float operations per vsync, far under the
frame's noise; not separately timed. The medium's own cost after the
2026-10-13 quality pass (froxels carrying the medium's average, gradient
noise): Vulkan `haze` 0.12 ms in Rue Vermillion (0.15 before), 0.058 in
Snowfly Forest (0.044), 0.055 in Undercity West (0.039).

## Readable, subtle, PS1-faithful

- The sim only *removes* fog near characters (and piles at most 1.5 x in a
  thin ring): it cannot make a scene foggier than the authored volume, and
  the medium's limits still cap opacity and light.
- Slow by design: no motion faster than the characters', relaxation over
  seconds, diffusion blurs edges; no high-frequency detail (32-64 cells
  over a room-sized volume).
- Low by default (presets at 48-96 units tall: ankle to knee); it never
  covers faces, enemies' bodies or pickups' glows.
- Lives between the camera and the texels: the PS1 texels stay nearest-
  filtered and sharp; the fog volume is smooth at its own low resolution
  (the froxel volume) and sampled at each pixel's depth.
- Off in rooms nobody authored; generated moods do not get sims.

## Prototype (in the tree, off by default)

- `fog <id> ... sim=1 [sim_strength=1] [sim_recover=3]`: the first enabled
  simulated volume of the room gets a 32 x 32 field (`pc/src/fog_sim.cpp`),
  stepped per vsync from `pc_medium_gpu_get`, actor positions from the
  game's actor table (`dev_state.c` `pc_dev_actor_positions`, battle
  builds), packed into the medium record (`PC_MEDIUM_SIM`), sampled by
  `medium.glsl` `medium_sim` in `medium_volume`.
- Editor: Atmosphere > a selected volume > "Characters part it (prototype)"
  with strength and recovery.
- RPC `environment` reports `sim` {`id`, `least`, `mean`, `at_player`,
  `actors`} and takes `sim_reset`.
- Starts from the 2026-10-13 quality pass: domain-warped gradient-noise
  wisps, froxels that carry the medium's average over their cell (no
  slicing), drift in world units per game second, the medium's limits.
- Measured (pc/tests/fog_sim_smoke.py, Snowfly Forest, a 900 x 90 x 900
  volume around Ashley): standing, the field under her settles to 0.28
  (strength 1) with the rest at rest (mean 0.996); a 530-unit walk leaves a
  trail (least 0.50, mean 0.988); 5 s after stopping the trail is gone and
  only her footprint remains.
- Evidence (not committed): a hand-placed volume in a scratch user layer
  in Snowfly Forest (`fog simdemo0000001 name="Clearing mist (demo)" ...
  sim=1`), top-down maps of the field (standing, after a walk, 1 s and 5 s
  after stopping) and the same paused frame with the field and without it.
- Known limits: one simulated volume per room; the field is packed into
  the medium record (a texture is the production path); actor footprints
  are one size for everyone; volumes added live over RPC (not saved) were
  seen to vanish once when Ashley walked to one side of Snowfly Forest
  room 6 (the room's lighting re-applied; saved volumes come back with it)
  - to look at before building on it.

## Decisions for the user

1. **Go ahead with B (2D sim per volume)** as the interaction model, with A
   (analytic carve) for Low quality? Or start with A only?
2. **Where first?** Snowfly Forest clearings, the Undercity, or the town's
   lower streets; and should generated moods ever add sims (default: no,
   authored only)?
3. **How strong?** Default hole depth (prototype: a footprint at 20 % of the
   fog when standing) and recovery (3 s); should enemies part it too
   (prototype: yes) and should spells/attacks burst it (later)?
4. **Paint storage**: text-encoded masks in the lighting packs (diffable,
   proposed) or small PNGs next to them.
5. **Presets as project assets** with volumes referencing them (proposed),
   or copied values only.
6. **Production data path**: texture array + GPU compute on Vulkan (and CPU
   on GL), replacing the prototype's record packing (proposed).

## Phases

1. Prototype review (this) -> decisions.
2. Texture-array data path, several simulated volumes, GPU sim on Vulkan;
   `env fog sim=1` over the floor grid; tests (deterministic field after N
   vsyncs of scripted walking; never darker/denser than authored; limits).
3. Editor: presets, interaction controls, live preview + ghost actor, debug
   overlay.
4. Paint mode (density, flow, erase) with masks in the packs.
5. Path fill (`fogpath`), animated fill for cinematics; quality per area.
