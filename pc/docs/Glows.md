# Glows: the game's glowing sprites as light sources

The game draws its lamps, flames, the save point and many effects as
additive translucent polygons. The lighting paths (GL PTGI, Vulkan ray
tracing, probe GI, haze) turn them into small spherical light sources:
*glows* (`emitter_pos` / `emitter_col`, at most 32 per update). This page
covers how glows are found, why they used to jump, and how they are kept
stable, smoothed and editable (`pc/src/glows.c`, `glow_overrides.h`).

## The problem (2026-10-11)

User report from the save room (zone 9 room 17): "How do I control the save
spot glow? It's a little unhinged." and "There are between 2 and 4 glow
spots and they appear and move rapidly." The log showed the emitter count
changing every update (3, 4, 6, 9, 10...) and, in another room, alternating
32 and 3 from the same 108 glowing triangles.

Root cause: `ptgi.c` re-clustered every glowing triangle from scratch every
update. Triangles were sorted by this frame's weight (area x luminance of
four texels sampled under their corners and centre), and each joined the
first emitter whose *running* centroid was within 96 units. So:

- **Save point**: its column is 34 additive room triangles whose texture
  scrolls (room texture effect type 2). The scroll changes which texels pass
  under the four sample points, so the weights and their order change every
  frame, and the greedy clustering cut the 160-unit column into 2, 3 or 4
  emitters at different heights. Each emitter carried a full glow's light,
  so the light energy jumped by a third with every change (coefficient of
  variation 0.47), the emitters moved, and selecting one in the editor was
  hopeless (its position, the only identity, changed under the cursor).
- **32 <-> 3** (the title's idle cutscene, zone 20 room 0, the hall with the
  tall windows): its 108 glowing triangles are the window panes, whose
  texels the cutscene brightens and darkens with the lightning (all panes
  together, colour 0 -> 0.94 -> 0 over about half a second). Dark, every
  pane weighed under the 1e-3 cut-off and only the 3 other emitters were
  left; lit, the panes spread over the whole hall made more than 32 clusters
  and the list was cut at 32 (which 32 depending on the weight order). The
  count flipped every time a pane crossed the threshold.
- **Effects** (first room: the fire's embers drifting past the windows,
  sprites living a fraction of a second) made a new set of emitters every
  frame (21-27, 36 count changes in 120 vsyncs).

Overrides, mute/solo, selection and light-actor links were all matched to
"the emitter nearest this position", so they followed whichever cluster was
nearest that frame.

## Design

`ptgi.c update_emitters()` still collects the glowing triangles (room ones
from the captured scene, effects and objects from `gpu_hw.c`'s emissive
list) and their colour from native VRAM, but each now carries its **source**
(`pc_glow_fragment.source`: origin << 28 | tag), and `pc_glows_update()`
(glows.c) turns them into glows:

| Origin | Source tag | One glow per |
| --- | --- | --- |
| room | the room triangle (grouped once per capture) | room polygon group: polygons whose bounds come within 24 units (a fixture: the save point's column, caps and sigil); groups wider than 384 units split into 160-unit pieces in polygon order |
| particles | `effect_particles.c`: the node's sprite script, its table and the matrix it is attached to (`render_polygon.source`, passed through `render_emit_tag`) | compact system: the glow its triangles are nearest to (192 units), else a new one; *spread* system (its triangles span > 384 units in a frame: embers, sparkles): a fixed 512-unit (x, z) grid, one glow per cell |
| object | the instance's mesh handle (`render_emit_tag`) | as compact particles, 96 units |
| packet | texture page and CLUT (effects still drawn from packets) | as compact particles, 96 units |
| baked | the estimate's index | estimate (`baked_estimate`, off by default) |

- **Identity**: each glow has an **id** (stable for the room visit; a new
  room capture starts over) and a persistent **key**:
  `origin:tag@x,y,z` (home position: a room group's fixed centre, a
  particle glow's first position, a cell's centre). Keys match exactly, else
  the same origin and tag nearest the key's home (96 units), else the same
  origin there (mesh handles and particle tags may differ between runs).
- **Observation** per update: weighted centroid (a room group sits at its
  area-weighted centre, independent of its texels), mean colour, emitting
  size (0.15 x sqrt(area), 2-8), strength. Room triangles sample their
  texels on a 15-point barycentric grid (was 4), so a scrolling texture
  gives its mean rather than whatever passes under the corners.
- **Strength**: one glow now stands for what used to be several emitters,
  each with the full colour; to keep rooms as bright as before, a glow's
  light is multiplied by the number of 96-unit pieces it spans (the save
  point's column: 4; a cell of embers: the embers' clusters this frame,
  smoothed), and the traced glows together are capped at 32 pieces (the old
  emitter limit: the window hall's lit panes). Overrides scale on top.
- **Smoothing** (per category, see below: fixtures and magic 0.15 s,
  effects 0.1 s; RPC `glows {smoothing}` sets all): exponential towards the
  observation in game time (vsyncs / 59.94, so stepping and pauses are
  deterministic): position, colour, size, strength. A jump of more than 192
  units snaps (a respawned effect). Presence fades in with the smoothing and
  out three times slower (at least 0.3 s), so blinking sprites give a
  steady light; a glow with nothing drawn for 1 s (5 s for grid cells) and
  faded out is dropped; room groups stay for the visit.
- **Tracers**: up to 32 glows, the strongest (power x presence; glows traced
  last update count 1.5x, so two of similar strength do not trade places),
  passed in id order (stable for the GL path's lighting-signature bits).
  Consumers are unchanged: GL PTGI, Vulkan `vk_rt.inc` (glow light and glow
  shadows), probe GI (`probe_gi.inc`, `vk_probes.inc`), haze. NRD's
  history reset does not look at glows; the filter's glow history now sees
  a steady signal.
- **Overrides** (`pc_glow_override`): bound to a glow id once per visit
  (by key; an old line without a key by position, taking the glow's key),
  then applied by id: enabled, intensity, tint, size, reach. Mute, solo and
  light-actor links (`pc_lights_glow_drive(key, ...)`) are by id/key too.

### Categories (fixtures, magic, transient effects)

Glows also come from attacks, spells, hit sparks and other short-lived
battle effects, which cannot be tweaked one at a time, and whose shadows
jumped all over the place. Every glow has a category
(`pc_glow_info.category`), from its source:

| Category | What | From |
| --- | --- | --- |
| fixture | candles, torches, lanterns, braziers, flames in room polygons | room polygon groups (not magic), baked estimates; a non-room glow that stayed (promoted) and is warm |
| magic | persistent magic glows: the save point | room groups with the save point's texture effect (type 2), or cool coloured; a promoted cool glow |
| effect | attacks, spells and ability effects, hit sparks, particles | particles, object meshes, packet effects; *promoted* to fixture/magic when seen 80% of at least 4 s (a fire's embers, a brazier's sprites), never when the particles are attached to a character's or effect's matrix (`PC_GLOW_ATTACHED`, `effect_particles.c` flags 0x20000: attacks and spells) |

- **Effects never cast shadows**: `pc_glow_casts` (bit i: traced emitter i
  may be shadowed; fixtures and magic glows when their category's *cast
  shadows* is on, baked estimates always) reaches every tracer: Vulkan
  `rt_lights.glsl` `i3.x` / `glow_casts_shadow(i)` in `rt_trace.comp` (glow
  light shadows, glow shadows on the baked light, lighting-signature bits),
  `rt_haze.comp` (in-scattering shadow rays), `probe_trace.glsl` (probe GI),
  and the GL PTGI shader's `glow_casts` uniform (the same four places). An
  effect's light still adds, unshadowed, with an attack/decay envelope (in
  with the effects' smoothing, out three times slower, at least 0.3 s).
- **Category controls** (`pc_glow_category[3][5]`): enabled, intensity
  multiplier, reach multiplier, cast shadows (effects: always off, shown
  disabled with the reason), smoothing seconds. Room settings
  `glow_fixtures_*`, `glow_magic_*`, `glow_effects_*` (`room_settings.cpp`:
  project default in `pc/lights/defaults.txt`, per-room override in the room
  file, Details > Lighting > *Game glows by kind*); RPC `graphics` takes and
  reports the same keys. Defaults: everything on at 1, except effects at
  intensity 0.5 (their light now lasts its decay: a hit is about as bright
  overall as before, without the strobe).
- **Per-glow overrides, selection, solo and actor links are for fixtures
  and magic glows only**; effects are one Outliner row, "Effect glows (N
  active)", with the kind's switch, and are not drawn or picked in the view.
  RPC `glows` refuses `select`/`set` on an effect.

### Room file format

    glow x y z muted intensity r g b size reach [key=KEY]

The key is new; old lines (no key) still load and bind to the glow nearest
their position; saving writes the key. Old builds read the new lines (the
key is past the ten numbers they scan). Light actors save their link as
`glow=x,y,z glowkey=KEY` (`glowkey` is new; without it the actor links the
glow nearest `glow=`).

### Editor

Outliner > Glows (from the game): one row per glow, by name ("Save point
glow", "Flame (candle) #2", "Flame (torch)", "Fire", "Embers #3",
"Sparkles", "Magic glow", "Room glow", "Baked estimate"), greyed while its
source draws nothing; checkbox mutes, S solos, the tooltip shows the id, key
and triangle count. The view draws a diamond at each glow's smoothed centre
(hollow when out); a click selects by id and the selection stays on that
glow. Details: name, origin, key, whether it is traced, the overrides and
its kind (and where the kind's settings are). *Light this fixture*, *Light all fixtures* and *Link
nearest glow* store the glow's key on the actor.

RPC `glows` (README): list, select, set, reset, solo, smoothing,
fragments (this update's triangles with their glow ids).

## Measurements (2026-10-11, Vulkan, RTX PRO 6000, default Custom settings)

`measure.py` (scratch tool): per vsync for 120 vsyncs, the traced glows and
their light energy (sum of luminance of what the tracers get), and the
presented image in the glow-light view (`gi_debug 1`, `gi_bounces 0`):
mean absolute frame-to-frame difference (0-255).

| | count (min-max) | count changes | energy mean | energy CV | energy step / frame | glow light frame-to-frame |
| --- | --- | --- | --- | --- | --- | --- |
| Save room, before | 2-4 | 13 | 1.19 | 0.47 | 0.050 | 0.54 (median 0.22, max 5.7) |
| Save room, after | 1 | 0 | 1.37 | 0.031 | 0.004 | 0.058 (max 0.12) |
| First room, before | 21-27 | 36 | 3.18 | 0.057 | 0.037 | see below |
| First room, after | 17-19 | 1 | 3.68 | 0.018 | 0.014 | see below |

Save room: one glow, id 1 throughout; the glow light's frame-to-frame
change is 9x smaller. First room (the fire outside the windows): 19 glows
(19 ids) over the 120 vsyncs, the traced set changed once. Its glow light barely reaches the visible surfaces
at the default *Glow light* (0.25): with it at 2 and the unlit window
sprites masked out, the frame-to-frame difference went 1.02 -> 0.74 (mean;
the rest is the window fire's animation and Ashley). Title idle hall: 23
room-group glows with fixed ids; the traced count changed twice in 2500
vsyncs (on and off with the lightning) instead of every few frames.

Test: `pc/tests/glow_stability_smoke.py` (run.py suite
`glow_stability_smoke`, with a save): the save point is one glow with one
id for 90 vsyncs, holds still, energy variation < 0.1, stays selected by
id, an intensity override halves its energy, is saved with its key and
applies after a relaunch, and an old-format line still binds (muted).

### Transient effects (the prologue fight)

New Game, the knight in the courtyard (zone 3 room 0; New Game and a
Circle press every 100 vsyncs reach it at vsync ~12000, hits from ~12100),
glow shadows on, 320 vsyncs with attacks:

| | effect emitters | light energy per vsync: largest change | glow-light frame-to-frame (max) | shadows from effects |
| --- | --- | --- | --- | --- |
| before | 0-10, one new set per game frame | 0.43 of the peak, alternating every frame | 16.8 (alternating 14.7 / 3.8 / 16.8 / 1.7) | yes (every emitter with glow shadows on) |
| after | 0-4 glows (4 ids) | 0.15 of the peak; smooth rise, exponential decay | 7.6 | never (`casts` bits clear) |

Mean effect light 0.076 before, 0.102 after (intensity 0.5); peak 1.58 vs
1.34.

### Fixture types (2026-10-12)

The generated lighting layer lists each game glow that belongs to a
catalogued fixture as `glow <key> fixture_type=T…`
([Fixture_Catalog.md](Fixture_Catalog.md)). The override carries the type
(`pc_glow_override.fixture_type`). A type whose glow treatment is
`suppress` (cobwebs, scrolling floor arrows, floor/ceiling glow decals,
fake light shafts) mutes it with `muted` 4, so it is not traced and adds
no light. The check runs every update (`pc_fixture_glow_mode`), so a
catalog edit applies at once. RPC `glows` and `fixtures` report the type.
The candle and torch flame sprites are camera-facing room planes (flag
0x400), which the scene capture skips, so they make no glow at all. Their
light comes from the generated actors.

## Enclosing glows: standing in a save point (2026-10-13)

GitHub issue 2 (Daemeous): "Save points can presently have their light
blocked by just standing in them". With Ashley in the circle, the room went
dark around him. The save point's glow was a sphere of radius 8 at the area-weighted centre of
its polygons (446, -94, 574 in the save room), 94 units above the floor:
inside Ashley's chest. Every shadow ray to it from the room hit him, so its
light and its share of the baked light (the characters' glow shadows on the
baked light, `baked_visibility`) were gone everywhere it reached. The
reporter's workaround, an emitting size of 32, kept the light but blurred
every shadow.

The save point's glowing polygons are an octagonal column 128 units wide
(corners 64-68 units from its axis) and 224 high, around a 128-unit sigil
quad on the floor (34 triangles). Its light comes from that column's side.
A character in it hides only the part of the side behind him, as seen from
a point in the room, and still shadows the floor beyond him.

- **Shape** (`glows.c fit_column`, once per room capture): a room group's
  upright polygons (normal within 45 degrees of horizontal) give a vertical
  column. The axis runs through the middle of their bounds, from their
  lowest to their highest corner, and the radius is their corners' mean
  distance from the axis. For the save point: centre (448, -112, 576), half
  height 112, radius 63. A group of mostly flat polygons fits a solid disc
  of its area. A column **encloses** when a character fits in it and it
  stands on the floor: radius at least 24, at least 96 high, upright
  polygons in at least 6 of 8 sectors around the axis, and a room polygon
  under the middle of its foot (`pc_scene_triangle_at`). Such a glow is a
  column without being asked.
- **Which glows**: an offline pass over every room (the same rules on
  `fixture_lib.glow_groups`) finds the 39 save points (types T053-T055,
  all r 63, h 224), and one light shaft that stands on a street (zone 32 room 3,
  T235, not a light: its type suppresses it). Nothing else qualifies.
  Hanging glass street lanterns (T013/T014, octagons r 26, h 57) are too
  small and hang in the air, the other shafts float, and floor decals and
  arrows are flat. Candles, torches, braziers and fireplaces cannot hold a
  character. Their flame sprites make no glow (their light comes from light
  actors), and their glows are small spheres at the flame, where the fixture
  itself stops a character.
- **Choice**: `shape` of a glow override (`auto`, `sphere`, `column`; room
  file `shape=`, editor Details > Shape, RPC `glows set {shape}`), else its
  fixture type's `glow_shape` (the catalog's `save_point` class says
  `column`), else auto. An override's emitting size is the column's radius.
  RPC `glows {shapes: 0}` (or `ROOD_GLOW_SHAPES=0`) makes every glow a
  sphere again, as before (comparisons), and 2 makes the columns solid.
- **Tracers**: a third array beside `emitter_pos`/`emitter_col`,
  `emitter_shape` (half axis, radius: > 0 the column's side, < 0 a solid
  disc or cylinder, 0 the old sphere). It is at the end of
  `rt_lights.glsl`'s Trace block (`vrt_trace_ubo`, `probe_trace_ubo`), plus
  a GL uniform. `emitter_sample` picks a point on the shape for the glow
  light, the glow shadows on the baked light, the haze and the probes, on
  GL and Vulkan alike. The emitter position becomes the column's centre.
  The light's sample sets its cosine and falloff; its shadow ray is
  `emitter_shadow`'s (below), and so is the lighting signature's
  noise-free ray (`emitter_shadow_nearest`).

### Options compared (save room, Ashley at the centre, the edge (40 off the axis) and outside (100 off))

How much of the room's light stays with glow shadows on against off
(mean of the frame without the HUD, Ashley's position written into his
record; Vulkan Ultra, 64-bit, RTX PRO 6000):

| Glow | centre | edge | outside | Ashley's shadow outside |
| --- | --- | --- | --- | --- |
| sphere r 8 (before) | 0.56 | 0.99 | 1.00 | crisp, long |
| sphere r 32 (the workaround) | 0.90 | 0.98 | 1.00 | faint, soft |
| sphere r 44 | 0.95 | 0.98 | 1.00 | faint |
| column side r 63 (**default**, before the core below) | 0.91 | 0.92 | 0.98 | soft: its feet, a wedge behind him (too faint in play: "Visible shadows") |
| column side r 32 | 0.87 | 0.91 | 1.00 | soft |
| solid column r 63 | 0.89 | 0.94 | 0.99 | soft |

The baked light's visibility on the floor around the circle (RPC
`gi_probe` at world points, rings 96 and 160 units from the axis) with
Ashley at the centre: about 0.45 before (every direction), 0.86-0.93 with
the column. With him just outside, the floor straight behind him reads
0.71-0.74 against 0.99-1.0 to his sides. GL (32- and 64-bit, the GL path
tracer and probe GI) gives the same within 0.03. Low and Original are
unchanged (their glows cast no shadows). Medium's glows cast no shadows by
default either; with glow shadows switched on, its probe GI's light goes
0.85 -> 0.97.

The column's side is the default. It is the glow's real shape, it keeps
the most light of the shapes the size of the save point, and its shadows
are as soft as a 128-unit-wide light makes them. The soft shadows are the
price of standing in a light this big: any emitter Ashley cannot cover
must be wider than him. The sphere options are not the glow's shape. A
sphere large enough to stick out of Ashley floats at chest height.
`shape=sphere` plus a size, or a smaller column radius, are there for
anyone who prefers crisper shadows over the light.

### Visible shadows (playtest, 2026-10-14)

The user played save room 29/0 on 64-bit Vulkan (Custom: path traced GI,
NRD, traced shadows, glow shadows, haze High) and saw no shadow of Ashley
beside the save point. Measured in the presented frame there (glow shadows
on against off, the floor 1 and 2 body lengths (112 units) behind him, the
GI probe's screen position of those points): 12-15% and 6-7% darker. The
old sphere gave only 5-17% in that room too. Two things kept it faint:

- **The shadow rays went to the whole 128-unit-wide side**, so beyond
  Ashley the floor still saw most of it (a penumbra, no umbra).
- **The glow's share of the baked light was small.** The baked light's
  shadow term weighs each glow by its colour (`near = w / (w + 0.1)`, at
  most `1 - baked_ambient` = 0.65). The save point in 29/0 is found by
  dimmer scrolling texels than the one in 9/17 (luminance 0.07 against
  0.34), so even a full block darkened the floor by about 20%. The blue
  patch on the floor around it is all its own baked light, though.

Now (`rt_lights.glsl emitter_shadow`, the GL shader the same, the haze
and the probes too):

- **Core**: a shadow ray aims at the same point of a core a quarter of
  the column's radius (16 units) as the light's sample. The light (its
  cosine and falloff) still comes from the whole side. Beside the column,
  Ashley now hides most of what a point behind him sees: a soft shadow
  with a real middle.
- **Clear middle**: the ray stops where it enters a cylinder 0.4 of the
  radius (25 units) round the axis. A character standing in the middle of
  the circle blocks none of the light: the room stays lit (29/0 keeps
  0.94, 9/17 0.91-0.93). One anywhere else in the circle shadows the floor
  beyond him (the edge, 40 off the axis: 44% darker).
- **Baked share**: a column's weight in the baked light's shadow term is
  `3 x` its presence and category intensity instead of its colour (the
  `emitter_shadow_.y` channel), so near it the term reaches its full
  `1 - baked_ambient`.

Measured in the presented frame (same method): 29/0 beside the save
point 36-38% darker 1 body length behind him and 36-40% at 2; 9/17 40-44%
and 38-40% (Vulkan; GL 33-36% in 29/0, 36-43% in 9/17). Each change alone gave at most 22-30% in 29/0. Knobs: RPC
`glows {shadow_core, shadow_clear, column_baked}` (fractions of the
radius, and the weight; 1 / 0 / 0 restores the first column's shadows,
except that a character in the middle now never darkens the room). A
fixture type can set its own `glow_shadow_core` (the catalog).

Test: `pc/tests/save_point_glow_smoke.py` (run.py suite
`save_point_glow_smoke`, with the pinned card). It checks the column, a
shape override saved by key, and, at High and Ultra, the floor's
visibility and the frame's light with Ashley at the centre (and that the
old sphere fails). It checks his shadow behind him at the edge and
outside, also in the presented frame at 1 and 2 body lengths (at least
30% darker). In 29/0 (`room_goto`) it checks the same visible shadow on
both sides and the light kept with him in the circle.

## Emissive texels as glows (2026-10-15)

Texels marked emissive in the emissive catalog ([Emissive.md](Emissive.md))
with a `light` weight join the glows: their room triangles become room
polygon groups as additive polygons do (one glow per window, stable keys),
and light texels on characters, props and other room blocks become object
glows by mesh. Their colour is the texels' emission times the weight; the
budget, smoothing, categories and shadows are the glows' own.

## Limits and next steps

- Categories come from the source: a spell drawn with unattached world
  particles that lasts more than 4 s would be promoted to a persistent
  glow; long-lived effects owned by an actor never are. There is no battle
  state check yet (the game's battle flag could keep everything in an
  active fight an effect).

- Particle sources are identified by their sprite script; two systems with
  the same script close together are one glow (as before, within 192
  units). Effects drawn from packets only have their texture as source.
- A spread source's grid cells do not follow a moving fire; a carried torch
  is a compact source and is followed.
- Long room groups are a single point light at their centre (strength 4),
  except enclosing columns (the save points: "Enclosing glows" above), whose
  light comes from their whole side.
- Names are a guess from colour and size (warm: candle/torch/fire by size;
  blue: magic glow).
