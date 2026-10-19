# Particles: the port's particle system (dust motes first)

The port has a small, data-driven particle system of its own (not the
game's effect particles, which the game still draws). Its first use is
**dust motes**: tiny square motes of one or two game pixels, nearest like
the PS1's texels, that drift slowly with a little turbulence and show only
where light crosses the air (a window's beam, a lamp's pool); in shadow
they vanish. The graphics setting **Dust** turns them on (Off by default:
the picture as before, pixel for pixel).

It is built to be reused. Later uses already have a place in it:

- **fog emitters**: brazier smoke rising, a vent pouring mist. A mote's
  position and its velocity are closed-form (below), so an emitter can feed
  the interactive fog (`fog_sim.cpp`) with the same positions and their
  analytic velocities, without a particle buffer;
- **window light shafts**: a `streak` renderer (and a `soft` sprite) beside
  the `pixel` one, the same emitters and modules.

## Emitters and assets

An emitter is a lighting item, `emitter <id>`, saved per room like a fog
volume (lighting/README.md "Emitters"): its own fields (`name`, `asset`,
`pos`, `yaw`, `enabled`, `seed`) and every other field from its **asset**,
`pc/assets/emitters/<asset>.json` (`ROOD_EMITTERS` reads another
directory), unless the item writes its own value. The editor writes only
what differs from the asset, so editing an asset changes every emitter
using it, and the reset arrow in Details goes back to the asset's value.
The asset format is in `pc/assets/emitters/README.md`: a spawn shape and
its numbers, the behaviour modules, the renderer and the lighting mode,
plus what a library lists (`kind: "emitter"`, `name`, `category`, `icon`,
`tags`, `description`).

**Reload assets** (Details > Atmosphere > Particle renderer, RPC `particles
{reload_assets}`) reads the assets again and resolves every emitter of the
room again: what its item writes stays, the rest follows the (edited)
asset, and the room is not left with unsaved edits. The same happens on
its own when the asset files change on disk (an edit in a text editor or a
script): the directory is polled about twice a second. An emitter whose asset
is missing (or fails to parse) keeps exactly the fields its item writes
(the rest are the defaults until the asset loads), so saving it loses
nothing and a later reload makes it follow the asset.

**Behaviour modules** are combined per emitter, without a node graph:
`drift` (a steady velocity), `curl` (curl-noise turbulence: amplitude, swirl
size, swirl speed), `gravity` (y down; negative rises: buoyancy), `drag`
(the fall settles at `gravity / drag`), `fade` (in, out). A module left out
is neutral. In the lighting file they are flat fields (`drift`, `curl`,
`curl_scale`, `curl_speed`, `gravity`, `drag`, `fade_in`, `fade_out`), so
an instance overrides any one of them.

**Spawn shapes**: `box` (in its turned box), `room` (in the room's bounds),
`point` (a ball of `radius`; 0 a point), `disc` (a horizontal disc of
`radius`), `surface` (on the floor under the box's footprint: the room's
floor grid, `pc_scene_floor_grid`, as the ground fog uses; the box's bottom
face without one). It is a floor only: walls, ceilings and props as
surfaces (a vent pouring mist from a wall, a brazier's bowl) are not there
yet, so the fog emitters must add mesh sampling to it (or a new shape)
rather than assume it.

**In the editor**: Details > Atmosphere > Dust and particles has the Dust
setting, an asset combo and **Add emitter** (on the surface under the
view's centre, selected), the renderer's settings and a debug view (every
mote white, unlit). An emitter shows as its outline (box, or circle for
points and discs) with an icon at its centre: clicking the icon selects it
(not a ray against the volume: a room-sized dust box would make the room
unclickable). The Outliner lists it (Emitters), Details edits it through
the property schema (Basics, Spawn, Motion, Look, Transform; the value
ladder and the reset arrows), the gizmo moves it (W), turns it (E) and
sizes its box, or the radius of a point or disc (R; RPC `editor_transform
{scale}` the same). Copy, paste, duplicate, hide, delete, F and undo work as
for fog volumes (the emitters live in the room state through `rood_env`
reset / apply / add_items). Show > Emitters hides the outlines and icons
(nothing to click then). With Dust Off (the default) a room's authored
emitters still show in the editor, dimmed and labelled "(Dust Off)": they
are objects of the room whatever the setting, and nothing of them is drawn
in the picture.

Details' Basics hold **Lighting** (In light only, Lit: the item's `lit`),
**Ambient** (Lit's light everywhere; 0.05 by default) and **Graphics
setting** (`quality`: Dust, or Always: all of its count whatever Dust is,
for emitters that are not dust). The renderer (`render`) has one value,
`pixel`, so Details has nothing to choose for it yet.

**Library entry points** (a library window lives on another branch):
`rood_particles::assets()` lists the assets (`particle_assets.h`), and
`rood_particles::add_emitter(asset, pos)` places one (`particles_internal.h`).

## Simulation: a closed form, no state

A mote is a pure function of its emitter's packed record, its index and the
time (`pc/src/particle_math.h`, and its GLSL copy
`pc/shaders/vulkan/particles_sim.glsl`: keep the two the same):

- a seeded hash (the emitter's `seed`, the mote's index) picks its lifetime
  (`lifetime`, less up to `lifetime_jitter` of it) and a phase; its cycle
  `k = floor((t + phase) / life)` reseeds its spawn point, its age runs
  0..life;
- from the spawn point it drifts (`drift` is its terminal velocity), falls
  under `gravity` slowed by `drag` (analytically: `g (age / drag - (1 -
  exp(-drag age)) / drag²)`, or `g age² / 2` without drag), and the curl of a
  smooth vector field displaces it by `curl` world units. The field is six
  plane waves: smooth, divergence-free (swirls without clumping), about
  unit strength, evolving with `curl_speed`;
- it fades in and out (smoothstep over `fade_in`, `fade_out`), shines
  `1 - variation x a random`, and is `pixel_min..pixel_max` whole game
  pixels.

So the simulation needs no buffers, runs the same on the CPU and the GPU at
any frame rate, and is deterministic. The time is game seconds since the
room was entered (vsyncs / 59.94, as the fog uses), so the motes stop when
the game pauses. RPC `particles {time}` freezes it (tests).

## Lighting

**In light only** (the default, `lit` 0): a mote reflects the light that
reaches it, and nothing else. Per mote its `particles_lights` (2; 1..4)
strongest sources by unshadowed light there (the room's lights and the
game's glows, the glows scaled by `particles_glow`) each get one shadow ray
toward their centre, and the sun one along its direction. The result is
noise-free (no random rays): no temporal filter, no shimmer. A mote in
shadow is black and takes no pixel at all.

**Lit** (`lit` 1) adds the emitter's `ambient` light (0.05 by default)
everywhere, so its motes show in the dark too. It is a flat value, not the
room's own ambient (its probes or the haze's grid).

The shadow rays are traced whatever the lighting quality, GI on or off:

| Renderer | Tracer | RPC `particles` `scene` |
| --- | --- | --- |
| Vulkan | ray queries (`particles_rt.comp`, `rt_common.glsl trace`) against this frame's acceleration structure: the path tracer's when it ran, else one the pass builds (`vrt_scene`: the room's BLAS once a room, the movers and the TLAS each frame) | `gi`, `own` |
| OpenGL | `pc_gi_trace_cpu`, `ptgi.c`'s BVH, on the CPU: the path tracer's scene, or built on the CPU for the motes when it does not run (`pc_gi_scene_cpu`: the room once, the casters when they move); at most `particles_cpu_rays` (1536) rays a frame, the motes' light refreshed round-robin | `cpu` |
| either | none (a Vulkan device without ray queries; RPC `particles {untraced}` for tests): a source that casts shadows gives no light, so motes in light only show only where a light without shadows, or the sun without them, reaches | `none`, `path: "untraced"` |

So a mote in light only never glints where it could be in shadow: with no
tracer it is dark rather than lit through walls.

**OpenGL's latency.** A mote's light is refreshed once every `n x
(particles_lights + 1) / particles_cpu_rays` frames (4096 motes, the
defaults: about every 8 frames) and kept in between; a mote drifting out of
a beam keeps glinting for up to that many frames, and one that respawns is
dark until its turn. Vulkan lights every mote every frame. Raise
`particles_cpu_rays` (more CPU time) to shorten it.

Lights honour their own `shadows` flag. Glows are shadowed while
`particles_glow_shadows` is 1 (the default), whatever their own category
says: the air behind a wall is dark. Glows are treated as spheres at their
centre for the ray.

The haze's froxel light grid is not used: it is scaled by the medium's
density (zero without haze) and only exists at haze High. Nor are the
shadow maps (the sun's, the lights'): they cover only the view's casters
and are GPU images, out of reach of OpenGL's CPU path.

## Drawing

Each present the motes are evaluated, lit and splatted into two images:

- a **key image**, one R32_UINT texel per game pixel of the display area:
  `min(SZ, 65535) << 16 | mote index`, cleared to `0xFFFFFFFF`; the nearest
  mote wins (`imageAtomicMin` on Vulkan, a CPU min on OpenGL). A mote covers
  its `size` x `size` game pixels anchored at `floor(s - (size - 1) / 2)` of
  its projected position: the same size near and far;
- a **colour image**, one RGBA32F texel per mote (256 wide).

The present shaders (`scaled.frag`, `gl_present.c`) composite them with
`particles_at` (`pc/shaders/vulkan/particles.glsl`, shared with OpenGL
through `gl_shaders.py` as `PC_GLSL_PARTICLES`): the mote owning the
pixel's game pixel adds its colour when it is in front of the scene there.
Texels are fetched, never filtered: motes are exact squares. The motes add
in linear light before the fog volumes and the haze (which attenuate them
like the surface behind), in the lit view mode only; not over the menu's
model view, and not in the F2 editor view (`editor_view.cpp`).

Vulkan runs one compute pass (`vk_particles.inc`: `particles.comp`, or
`particles_rt.comp` with ray queries) after the fog volumes' passes; the
present binds its images at 21 and 22. OpenGL does the same on the CPU
(`particles.cpp`) and uploads the two textures (units 19 and 20). The two
small textures stay bound with Dust Off too (so the integer sampler never
shares a unit with a float one). A GL driver with fewer than 21 texture
units gets a present compiled without particles (`PC_NO_PARTICLES`; a line
in the log) and draws none.

With Dust Off no mote is evaluated and no pass runs (`particles_smoke`
checks the picture is pixel-identical to the room with no emitter at all on
OpenGL, the Original preset; on Vulkan within its measured redraw noise,
which is 0 when two redraws are equal).

## Settings and RPC

| Key | Default | Meaning |
| --- | --- | --- |
| `dust` | 0 | 0 Off, 1 Low, 2 High (System > Graphics > Dust, Details > Graphics, Details > Atmosphere) |
| `dust_low` | 0.4 | the share of each `dust` emitter's count Low draws |
| `particles_glow` | 1 | the glows' light on particles |
| `particles_lights` | 2 | sources per mote (1..4), each with a shadow ray, the sun on top |
| `particles_glow_shadows` | 1 | glows' light is shadowed too |
| `particles_cpu_rays` | 1536 | OpenGL: shadow rays a frame |

An asset names the setting that governs it (`quality`: `dust`, or
`always`).

Details > Atmosphere > Particle renderer and the dev tools' Graphics tab
("Particle settings") draw `dust_low` and the `particles_*` settings
through the property schema (the standalone kind `particles`,
`particles_editor.cpp`): ranges, the reset arrows, the value ladder, undo;
RPC `editor_schema {kind: "particles"}` reads and edits them. Dust itself
is the Graphics setting (System > Graphics, the dev tools' Dust row).

RPC `particles` (`particles.cpp` documents its params): `level`, `low`,
`time`, `debug`, `untraced`, `show` (Show > Emitters), `settings` {`glow`,
`lights`, `glow_shadows`, `cpu_rays`}, `reload_assets`, `emitters` [{`id`,
`remove`, any field}], `add` {`asset`, `pos`} or {`asset`, `here`: true}
(as Add emitter), `select`, `motes` {`emitter`, `offset`, `max`}. Every
entry is checked before anything changes: an error leaves the room as it
was. The reply has the settings, `renderer`, `path` (`traced`, `untraced`,
`off`), `scene` (`gi`, `own`, `cpu`, `none`), `motes`,
`cpu_ms`, `display`, `view`, `assets`, `emitters` (with resolved `fields`
and `drawn`) and, asked for, `mote_list` (each mote's `pos`, `cell`, `size`,
`sz`, `colour`, the light the renderer gave it and the CPU's unshadowed
`open` light, `won`). RPC `graphics {dust}` sets the setting.

## Cost

Measured by `particles_smoke` (section cost: 4096 motes in a 600 x 300 x 600
box in the save room, Dust High, live time; 960 x 720 window, internal scale
2, the Ultra preset; the machine's RTX PRO 6000):

| Renderer | Path | Cost a frame | Budget (the test) |
| --- | --- | --- | --- |
| Vulkan | `particles` perf section (`ROOD_PERF=1`, RPC `perf`): the compute pass with ray queries | 0.02 ms of GPU time | under 0.3 ms |
| OpenGL | RPC `particles` `cpu_ms`, RPC `perf` `cpu_sections` `particles`: evaluate, light (1536 shadow rays round-robin), splat, on the CPU | 2.1 ms of CPU time | under 3 ms |

RPC `perf` reports `cpu_sections` `particles` on Vulkan too (gathering the
pass's records: about 0.003 ms). With Dust Off neither renderer reports a
`particles` section at all. Without the path tracer the motes' own scene
costs too: on Vulkan the movers' BLASes and the TLAS each frame, on OpenGL
the casters' BVH when they move (the room's once). On OpenGL
the cost grows with the motes (each evaluated and splatted every frame) and
with `particles_cpu_rays` (the shadow rays); lower it if a slow CPU needs to.

## Limits

- 16 emitters and 16384 motes a room (the key's 16-bit index);
- glows are spheres at their centre for shadow rays;
- without any tracer (a Vulkan device without ray queries) sources that
  cast shadows give motes no light;
- OpenGL refreshes a mote's light round-robin (above: a few frames late);
- surfaces are floors only;
- one renderer, `pixel` (the lint rejects `soft` and `streak` until they
  exist).

## Tests

- `particles_probe` (CTest): the closed form (shapes, cycles, drift,
  gravity with drag, curl noise divergence-free, fade, sizes), projection
  and keys, the C light shape, the assets.
- `particles_smoke` (`pc/tests/run.py particles`, both renderers): items
  (an emitter round-trips through the lighting file; a bad RPC call changes
  nothing), assets (reload: a missing asset followed once there, an edited
  one followed, nothing pinned), motes (deterministic, counted by Dust,
  moved by drift), picture (Dust Off identical, with or without an emitter;
  a wall between a light and the motes leaves them dark with the path
  tracer, without it (GI off) and with no tracer at all; a low sun behind
  the wall too; lit motes are uniform squares of 1 or 2 game pixels),
  editor (object, Details, reset, R on a box and a disc, Add emitter,
  Show > Emitters, delete, duplicate), viewport (a click on the icon, the W
  gizmo with the mouse), setting, cost (and RPC perf), example (the town
  house, zone 34 room 0: glints in the windows' light, none in shadow).
