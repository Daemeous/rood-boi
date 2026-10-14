# Interactive ground fog and hands-on fog volumes (design + prototype)

Status 2026-10-13 (evening): **phases 2 and 3 built** (section
"Production"): several simulated volumes per room, a weight per
disturbance source (global settings and per volume), the game's attacks
and spells as stamps, footprints from each actor's size, the fields and a
baked detail noise in one small 3D texture on every renderer, presets as
data, the editor's interaction controls with a live preview, save states.
Still per-volume opt-in (`sim=1`): no room has an interactive volume by
default; the user places and tunes them (Snowfly Forest, the Undercity).
Paint mode, path fill and the ground fog's own field (phases 4-5, `env fog
sim=1`) are not built.

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

## Prototype (superseded by "Production")

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
- Known limits (all lifted by "Production" below): one simulated volume per room; the field is packed into
  the medium record (a texture is the production path); actor footprints
  are one size for everyone; volumes added live over RPC (not saved) were
  seen to vanish once when Ashley walked to one side of Snowfly Forest
  room 6 (the room's lighting re-applied; saved volumes come back with it)
  - to look at before building on it.

## Production (2026-10-13)

User decisions taken: B (the 2D sim) per volume; every source with its
own weight (Ashley, the other actors on; attacks and spells hooked but at
0 until tried), global and per volume; enemies part it by default; hands-on
tuning in the editor with presets as data; Snowfly Forest and the
Undercity volumes placed by the user (no example volume ships: the
generated layer has no interactive volume, so atmosphere_sweep's rooms are
unchanged).

- **Engine** (`pc/src/fog_sim.cpp`): any number of volumes by id, the
  first `fog_sim_budget` (4) of a room simulate. Stepped once per game
  vsync from `pc_tick` (`atmosphere.cpp pc_fog_vsync`), not from the
  renderers, so it never depends on the frame rate or on presents. A volume
  ticks only while a source can touch it or its field is more than 1 % off
  rest (then it snaps to rest and sleeps: no work, no upload). Sources per
  vsync: every actor slot (`dev_state.c pc_dev_actors`: position, its drop
  shadow's radius `shadowRadius` x 0.9 as the footprint, velocity from its
  moves), the editor's ghost, and stamps (events lasting a few vsyncs).
- **Weights**: `pc_fog_disturb[4]` (settings `fog_disturb_player`,
  `_actors`, `_attacks`, `_spells`: 1, 1, 0, 0); a volume's `disturb_*`
  (-1: global) overrides both ways; the ghost always 1. Weight scales the
  clearing strength; 0 removes the source (it does not even wake the
  volume).
- **Game events** (no `src/` change): `scene_submit.inc
  pc_battle_action` (compiled inside BATTLE's 146C.c) reads the one battle
  action running (`D_800F196C == 2`, `D_800F19CC->unk854[unk0 & 3]`:
  performer, action, aim point `unk8.unk844`) and `_cameraMode`; each vsync
  the sim watches the edges: phase 7 (`func_80070278` started a swing, a
  chain step or a release; a new chain step re-enters 7) and phase 5 (an
  effect action started casting). Plain attacks, abilities and break arts
  are *attacks*: a plain swing stamps a SWING (an arc of +-80 degrees in
  front, toward the aim point, swirled), an art's release a BURST at the
  aim; spells, items and traps (`vs_main_actions` type 1, 5, 7) are
  *spells*: casting stamps a RING around the caster, the release a BURST
  where it lands. Enemies' actions go through the same machine, so they
  count too.
- **Data path** (R8, 32 x 32 at first; second pass below: RGBA8, 64 x 64): one 3D texture (`atmosphere.h pc_medium_texture`,
  65 x 65 x 69): slices 0-64 a tileable 64^3 Perlin-Worley detail noise
  (three octaves of each, period-wrapped lattices, normalised to mean 0.5;
  its first row/column/slice repeated so trilinear filtering with clamping
  tiles), generated by our own code the first time it is needed (about
  0.16 s, once, at the first frame; nothing shipped); slices 65-68 the four fields (32 x 32 in a
  corner, density / 2). Uploaded whole once, then the four field slices
  when they change (once per vsync at most; Vulkan: one image per frame in
  flight, a copy -> shader-read barrier only). Bound to Vulkan `rt_haze`
  (binding 11) and `scaled.frag` (17), OpenGL ptgi's trace and
  gl_present's Low haze (`MEDIUM_TEX` in `medium.glsl`; the integrate pass
  does not sample volumes). The medium record shrank from 250 to 165 vec4s
  (the packed prototype field is gone): volume `fill.w` = its layer + 1,
  `w` of its fifth vec4 = `detail`; 163 the texture's dimensions, 164 the
  detail period (3.2 x the wisp size).
- **Detail**: `medium_erode` adds the noise where a value is between 0 and
  1 (a soft edge, a parted footprint's rim): `v + (n - 0.5) x detail x 3 x
  v(1 - v)`, so rest and fully clear regions keep their value and only the
  transitions get fine wisps.
- **Save states**: `snapshot.c` chunk `CHUNK_FOG` (`pc_fog_state_*`): the
  sims (fields, velocities, clocks), actor tracks, pending events, the
  preview clock offset. A volume added live and not saved is not in a
  resumed process (host state), but its field is, and comes back with it.
- **Live preview**: while the game is paused (the editor's free camera,
  Pause) and the Atmosphere tab shows, the fog keeps stepping on the wall
  clock (extra ticks of the fog's own clock, `rood_fog_sim::clock`), so the
  ghost, the stamp buttons and every slider show at once.
- **The prototype's vanishing volumes**: not a fog bug. Walking to the
  side of Snowfly Forest room 6 goes through the door to room 7 and back;
  each room entry re-read the room's files and dropped unsaved edits. Now
  leaving a room with unsaved edits keeps them for the session
  (`lighting_editor.cpp layers_stash`) and entering it again puts them back
  on top of its layers, still unsaved (RPC `lighting reload` drops them).
- **Cost** (Vulkan RT, RTX PRO 6000, 1440 x 1080, Ultra, Snowfly Forest
  room 6, `ROOD_PERF=1`, median of 5 x 600 frames on a shared GPU, noise
  about +-0.1 ms): `haze` 0.72 ms without volumes, 0.79 with two plain
  volumes (Forest floor, Ankle mist), 0.76 with both interactive and both
  active (Ashley in one, the ghost in the other): the interaction itself is
  within the noise; two volumes with detail about +0.05 ms. CPU: one 32 x
  32 step 0.02-0.03 ms (RPC `sims[].ms`, the editor shows it), only while awake.
- Tests: `pc/tests/fog_sim_smoke.py` (off by default; footprint, wake,
  settling; every weight 0 disables its source, per-volume override both
  ways; the enemies part it; attack and spell stamps only with their
  weights; the game's attacks seen; two volumes with a layer each, the
  budget; presets saved as preset + differences; unsaved volumes back after
  leaving the room; a save state resumes the same field; the ghost; the
  editor's tab).

### Second pass (2026-10-13): the FluidNinja-like look, snapping

After the user's playtest notes (fog that reads as thick, billowy, swirling
and lit, not only parted mist; volumes that snap to the ground). This
supersedes the numbers above where they differ.

- **Resolution**: 64 x 64 cells per volume (the texture slice's 64 + 1).
  CPU per awake volume per vsync (bench, 64-bit): 0.05 ms, 0.1 ms with
  swirls; 32-bit 0.1 / 0.17 ms (the sim uses SSE maths on i386, no libm
  floor in its inner loop and flushes tiny velocities: x87 denormals had
  made it 20 times slower).
- **Swirls** (`sim_swirl`, 0-4): vorticity confinement (force along N x w,
  only where the fog is disturbed, so settled fog stops curling and sleeps)
  and a pressure projection (16 Jacobi iterations on a padded grid): pushes
  turn into curls; a walker sheds a pair of counter-rotating eddies at its
  sides.
- **Flow-mapped detail**: the texture is RGBA8 now; a field slice carries
  its velocity (world x, z) next to the density; `medium_detail` samples
  the baked Perlin-Worley twice, each advected by that velocity over a
  staggered 2.5 s cycle, cross-faded (the usual flow-map trick), so billows
  follow the wake; `detail` also modulates the density itself (billows,
  `smoothstep` contrast), not only the transitions.
- **Self-shadowing** (`shadow`, 0-2): per sample in a shadowing volume,
  four steps toward the main light (the sun or moon from the room's
  environment, else straight up; record 164.yzw) through that volume's
  coarse density (shape x field) give a transmittance that scales its
  scattering (all lights: an occlusion toward the main light) and 60 % of
  its fill: dark cores, lit rims.
- **Presets**: Rolling fog, Thick smoke (rich); Forest floor and Sewer steam
  got mild swirl, detail and shadow; Ankle mist, Knee fog, Still pool,
  Doorway draft stay restrained. Generated rooms have no volumes, so
  atmosphere_sweep is unaffected.
- **Limits**: thick smoke still obeys `haze_max_opacity` (0.55) and
  `haze_max_light` (0.15) per ray; an author who wants it solid raises them
  for the room (the evidence frames used 0.85 / 0.35). Dark rooms make dark
  fog: it is lit by the room's lights (and its `fill`).
- **Emitters** (vents, cracks, a pour or rise direction, rate) are the
  follow-up: not built.
- **Snapping**: *Drop to floor* (button, **End**, RPC `drop_volume`):
  vertical rays over the captured room triangles (up-facing within 60
  degrees) under the centre and the footprint's corners; the highest floor
  below wins, corners with nothing below (over a pit or a ledge) are left
  out, the nearest floor above only when nothing is below. *Fit to floor
  area* (button, RPC `fit_volume`): flood fill of the room's floor grid
  from the cell under the centre (within 12 units), the footprint becomes
  its bounds. Gizmo moves snap with the lights' modes (`pc_lights_snap_at`):
  the bottom face's centre on the point, edge, midpoint or surface.
- **Evidence** (scratchpad): Snowfly Forest room 6 with Rolling fog and
  the Undercity (zone 48 room 3) with Sewer steam and Rolling fog, Ashley
  walking through (12 frames, 4 vsyncs apart), standing after (6 frames),
  a swing; field maps; haze-alone frames. Vulkan `haze` (GPU, same run):
  Undercity 0.058 ms without, 0.064-0.067 ms with an active volume; Snowfly
  Forest within the shared GPU's noise.

## Decisions for the user (answered 2026-10-13: see "Production")

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

1. Prototype review -> decisions (done, 2026-10-13).
2. Texture data path, several simulated volumes, weights, attack/spell
   stamps, save states, tests (done, "Production"; the sim stays on the CPU:
   0.02-0.03 ms per awake volume, so a GPU port was not worth its plumbing).
   Still open: `env fog sim=1` over the floor grid.
3. Editor: presets, interaction controls, live preview + ghost actor, field
   overlay (done).
4. Paint mode (density, flow, erase) with masks in the packs.
5. Path fill (`fogpath`), animated fill for cinematics; quality per area.
