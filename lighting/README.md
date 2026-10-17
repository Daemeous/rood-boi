# Lighting packs

Per-room lighting for the PC port: lights, light actors (prefab instances),
overrides of the game's own glows and texture effects, and per-room settings.
It is data, authored in the editor (`pc/run-game.sh`, Escape) and kept here
as small text files that diff and merge well. It contains no game data: only
positions, colours and parameters authored for this port.

## Layers

A room's lighting is the merge of these layers, lowest first:

| Layer | Path | Tracked | Who writes it |
| --- | --- | --- | --- |
| generated | `lighting/<pack>/generated/zoneZZ_roomRR.txt` | yes | a generator (e.g. fixtures found in the room), regenerated wholesale; never edit by hand |
| authored | `lighting/<pack>/rooms/zoneZZ_roomRR.txt` | yes | people: hand edits and the editor's **Promote to pack**; pull requests |
| user | `pc/user/lighting/zoneZZ_roomRR.txt` | no | the editor's **Save area** (your own tweaks) |

Several packs stack in the order of the settings key `lighting_packs`
(`pc/user/settings.cfg`, default `default`; e.g. `lighting_packs default,moody`):
each pack's generated then authored layer, then the next pack's, then the
user layer. **Promote** writes the authored layer of the highest pack, or of
`lighting_active_pack` when set. Project defaults for the per-room settings
may also come from `lighting/<pack>/defaults.txt` (`set <key> <value>` lines),
under the user's `pc/user/lighting/defaults.txt`.

Every item has a stable id, and a higher layer changes a lower layer's item
by naming its id:

| Kind | Id | What |
| --- | --- | --- |
| `light` | `light:<14 hex digits>` | a point, spot or rect light |
| `actor` | `actor:<14 hex digits>` | a light actor (prefab instance) |
| `glow` | `glow:<glow key>` | an override of one of the game's glows (the key the editor shows; `@x,y,z` for old position-only ones) |
| `uv` | `uv:<1-64>` | an override of a room texture effect (speed, freeze) |
| `set` | `set:<key>` | a per-room setting (`baked_lighting`, `haze_density`, ...: `pc/src/room_settings.cpp`) |
| `env` | `env:sun`, `env:sky`, `env:fog` | the room's sun (or moon), sky and ground fog (below, "Environment") |
| `fog` | `fog:<14 hex digits>` | a fog volume: a box or sphere of mist (below, "Fog volumes") |
| `emitter` | `emitter:<14 hex digits>` | a particle emitter: dust motes in a room's light (below, "Emitters") |
| `post` | `post:room`, `post:cutscene`, `post:event.<id>` | the room's post-processing profile in play, in its cutscenes, in one event (fields: [pc/docs/Post_Processing.md](../pc/docs/Post_Processing.md); `preset=<name>` puts a named profile under the item's own fields; nothing is generated: every effect is off unless a layer sets it) |

The editor makes ids from the time and a random part, so ids made by
different people do not collide and sort in the order they were made.
Generated items take deterministic ids from what they were generated from
(`pc/tools/lighting_format.py` `generated_id("actor", "candle", fixture_key)`
gives `actor:gen.candle.<hash>`), so regenerating keeps every id, and the
overrides that name them, valid.

## File format (version 2)

```
rood-lighting 2
# zone 9 room 17: a comment (anywhere; also after an entry)
actor 68ea4c1200a3f1 prefab=candle pos=448,-40,576 rot=90,0,0 seed=7 intensity=1.4
override actor:gen.candle.5be1c0d2a9 intensity=0.6 !flicker
glow room:0000123@448,-40,576 pos=448,-40,576 intensity=0.5
light 68ea4c1d0071c9 name=Moonlight type=spot pos=120,-300,96 dir=0.2,1,0 color=0.6,0.7,1 intensity=2 shadows=1
delete light:gen.window.77c1a0e4f2
set haze_density value=0.0006
uv 3 speed=0.5
```

- The first line that is not blank or a comment is the header `rood-lighting 2`
  (files without it are the old `pc/lights` format, read as a user layer and
  rewritten in this format when the editor saves).
- One entry per line, one entry per id per file, sorted by id:
  - `<kind> <name> key=value ...` defines an item (id `kind:name`), or
    replaces one from a lower layer;
  - `override <id> key=value ... !key` changes fields of a lower layer's item
    (`!key` puts a field back to its default);
  - `delete <id>` removes a lower layer's item (to switch one off but keep it,
    `override <id> enabled=0`).
- Values are numbers, comma vectors (`x,y,z`) or words; `"quoted"` when they
  hold spaces (`\"` and `\\` inside). Numbers have fixed precision (positions
  0.1, colours 0.001, angles 0.01, ...), no trailing zeros. Fields at their
  defaults are left out.
- Light fields: `name type(point|spot|rect) pos dir color intensity radius
  cone inner size(w,h) barn_angle barn_length shadows enabled`. `inner` (a
  spot): full brightness inside this angle, fading out to `cone` (0: the
  outer quarter of the cone fades). `barn_angle`, `barn_length` (a rect):
  its four barn doors, at that angle from its facing (0 straight ahead, 90
  folded flat) and that long (0: no doors). Actor fields: `name prefab pos
  rot(yaw,pitch,roll) scale seed enabled glow glowkey fixture fixture_radius
  fixture_shadows fixture_type flame` (`scale`: its size; its lights'
  offsets, radii and sizes follow, never their brightness), and any
  parameter of its prefab (`pc/assets/prefabs/*.json`) it overrides.
  Glow: `pos muted intensity tint size reach fixture_type shape`;
  `shape=sphere|column` sets its emitting shape (default `auto`: as its fixture type says, else a column
  when it encloses room for a character, the save point's;
  [pc/docs/Glows.md](../pc/docs/Glows.md) "Enclosing glows"), and `size` is
  that sphere's or column's radius. Texture effect: `speed freeze`.
  Setting: `value`.
- `fixture_type=T002` (generated actors and glows) names a type of the
  fixture catalog (`pc/assets/fixtures/catalog.json`,
  [pc/docs/Fixture_Catalog.md](../pc/docs/Fixture_Catalog.md)). Such an
  actor takes the type's prefab parameters (below its own overrides), its
  light offset and its fixture radius (unless it sets `fixture_radius`),
  and a glow takes the type's treatment (suppressed: not a light). A
  catalog edit changes every instance at once.

An override or delete whose id no lower layer defines is **dangling** (the
item was deleted or regenerated under it). It is never dropped: the editor
lists it under Outliner > Layers (and keeps it in your user layer until you
discard it), RPC `lighting` reports it and the lint fails on it.

## Environment

A room open to the sky has `env sun` and `env sky` (`pc/src/environment.cpp`;
rendering: pc/docs/Vulkan_Renderer.md, "Sun and sky"); a room without them
has neither. The generator writes both for the rooms `pc/tools/outdoor.py`
finds open to the sky; override them by id like any item
(`override env:sun elevation=50`, `override env:sky enabled=0`,
`delete env:sun`).

```
env sun azimuth=205 elevation=35 color=0.55,0.65,0.95 intensity=0.7 angle=2
env sky zenith=0.07,0.09,0.16 horizon=0.17,0.18,0.23 ground=0.03,0.03,0.035 sun_tint=0.25 ambient=0.6
```

- Sun fields: `enabled` (1), `azimuth` (degrees, the compass direction it
  shines from: 0 from +z, 90 from +x; 180), `elevation` (degrees above the
  horizon; 45), `color` (1,1,1), `intensity` (1), `angle` (its angular
  diameter in degrees: the shadows' softness; 0.53, the real sun's),
  `shadows` (1).
- Sky fields: `enabled` (1), `zenith`, `horizon`, `ground` (radiance
  straight up, at the horizon and below it; 0.1,0.12,0.2 / 0.25,0.25,0.3 /
  0.05,0.05,0.05), `intensity` (1, scales all three), `sun_tint` (0: how much
  the sky around the sun takes its colour), `ambient` (0.5: the strength of
  the hemisphere term that stands in for the sky where nothing traces it).
- A sky radiance of 0.15 adds about 0.15 x the surface's albedo to an open
  floor (the indirect light is the cosine-weighted mean radiance arriving).

A room may also have a **ground fog**, `env fog` (`pc/src/atmosphere.cpp`;
rendering: pc/docs/Vulkan_Renderer.md, "Haze, ground fog and sun shafts"),
roofed or not; the generator writes one for the areas of
`pc/assets/environment/atmosphere.json` (with the haze settings
`set haze_density` / `set haze_anisotropy` of the area's mood):

```
env fog color=0.85,0.9,1 density=0.01 height=40 noise=0.7 scale=200 wind=9,0,3 fill=0.012,0.014,0.02
```

- Fog fields: `enabled` (1), `density` (extinction per world unit at its
  base; 0.003), `height` (scale height: the density falls by e every this many
  units up; 48), `offset` (its base above the floor under each point: the
  room's lowest up-facing level face there, on a 24 x 24 top-down grid, so
  it lies on the street and not on the roofs above it; 0), `noise` (0..1, how much the
  drifting wisps thin and thicken it; 0.6), `scale` (the wisps' size; 160),
  `wind` (their drift, world units per game second, y down; 4,0,1.5: mist creeps), `color` (its albedo;
  1,1,1), `fill` (light it scatters from everywhere, a radiance: keeps it
  visible where no light reaches; 0,0,0), `sky` (how much of the sky's light
  the haze and the fog scatter; 1).
- The haze stays the room settings `haze_density` (uniform in the room's
  interior, below), `haze_anisotropy` (forward scattering, also the fog's) and
  `haze_glow`.
- Limits, also room settings (Details > Atmosphere; an area's mood may set
  them): `haze_max_opacity` (0.55: the most of the scene the haze, the fog
  and the volumes together may hide along any ray), `haze_max_light` (0.15:
  the most light they may add toward the eye, a luminance, with a soft
  knee), `haze_shafts` (0.5: the sun shafts' contrast; shadowed air keeps
  1 - this of the sun's in-scattering), `haze_max_forward` (3: the peak of
  forward scattering, so looking toward the moon cannot blow out the
  picture; 0 none). Whatever the densities, the atmosphere never hides the
  scene: `pc/tools/atmosphere_sweep.py` checks every room from several
  angles.
- Where the room's air is, also room settings (Details > Atmosphere):
  `haze_interior` (1: the ground fog keeps to the room's floor, not its
  bounds box, and the haze leaves the void under the floor; 0 as before),
  `haze_interior_fill` (1: holes up to two floor-grid cells across count
  as floor), `haze_interior_slope` (60: the steepest up-facing face, in
  degrees, that counts as floor), `haze_interior_edge` (0.2: the
  interior's soft edge, 0 hard .. 0.5), `haze_outside` (1: the haze's share
  above the cells without a floor, the air between an outside camera and
  the room; 0 keeps the haze to the interior too), `haze_under_floor` (32:
  how far under the floor the haze reaches), `haze_void` (1: the void
  around the room, what the PS1 draws black, stays clean darkness past
  where its ray leaves the room; fog volumes are exempt),
  `haze_void_level` (0: the brightest vertex colour, of 255, that counts
  as that black). RPC `environment {haze: {interior, interior_fill,
  interior_slope, interior_edge, outside, under_floor, void,
  void_level}}`.

## Fog volumes

Local mist (a cellar's damp corner, a forest hollow, a sewer outlet):
`fog <id>` items, placed in the editor (Details > Atmosphere: Add volume) and
then edited like lights: click one in the view or the Outliner (Fog volumes)
and its fields are in Details (Basics, Transform, Look, Interaction; the
reset arrow goes back to its preset's value), with the Move / Size / Turn
gizmo.

```
fog 68ea4c1200a3f1 name="Cellar mist" shape=sphere pos=320,-60,640 size=400,120,300 yaw=30 density=0.006 fill=0.01,0.012,0.012
```

- Fields: `name`, `shape` (`box` or `sphere`; box), `pos` (its centre),
  `size` (full extents x, y, z; 256,128,256), `yaw` (degrees about the
  vertical; 0), `density` (0.004), `edge` (the soft part, a fraction of the
  half size; 0.5), `color` (albedo; 1,1,1), `fill` (0,0,0), `noise` (0.5; the
  wisps drift with the room's fog wind), `enabled` (1).
- A room renders up to 16 enabled volumes; they add to the haze and the
  ground fog and may reach past the room's bounds. The first 8 are drawn by
  their own renderer (pc/README.md, "Fog volumes' own renderer"; settings
  `fog_volume_render` 0: all in the haze), exempt from the room's haze
  limits, with these fields for their look: `opacity` (1: the most of the
  scene it may hide along a ray), `brightness` (1), `phase` (0.3: forward
  scattering, brighter looking toward a light through it), `ambient` (1:
  scale on its ambient light, the room's baked light around it and the
  sky), `glow` (0,0,0: light it emits per unit density, smoke lit from
  within), `glow_core` (0.5: 0 the glow even through it, 1 gathered at its
  base), `taper` (0: height profile, 1 thinning toward its top), `detail_size`
  (0: its billows' size in world units; 0 from the room's wisp size).
- **Interactive** (pc/README.md, "Interactive fog"): `sim` (0; 1: the
  characters, attacks and spells part it), `sim_strength` (1: how much a
  source clears), `sim_recover` (3 s to settle back), `sim_footprint` (1:
  scale on every source's size), `sim_trail` (1: how much of a source's
  motion it takes and keeps), `sim_wind` (1: scale on the room's fog wind),
  `detail` (0.5: billows and fine wisps, carried along by its flow),
  `sim_swirl` (0: eddies, up to 4), `shadow` (0: self-shadowing toward the
  sun or moon, up to 2; in the volumes' own renderer it is always
  self-shadowed toward every light, by 0.5 + `shadow` times its depth), `disturb_player`, `disturb_actors`, `disturb_attacks`,
  `disturb_spells` (-1: the global settings `fog_disturb_*`; 0 off). The
  first four interactive volumes of a room (`fog_sim_budget`) simulate.
- **Presets**: `preset=<name>` names one of
  `pc/assets/environment/fog_presets.json`; every field the volume does not
  write comes from it (the editor writes only what differs from the preset,
  even a value equal to the schema default), so a preset edit updates every
  volume using it. An unknown preset leaves the fields as written.
  A preset may be one of yours (`pc/user/prefabs/fog/<id>.json`, made
  with the Library's Save...): a room file naming it then needs your file;
  promote only volumes with built-in presets (or none) into a pack.

```
fog 7c1e0a9b3d55f2 name="Clearing mist" pos=320,-24,640 size=1200,72,900 yaw=15 sim_trail=1.5 preset=forest_floor
```

## Emitters

Particles (pc/docs/Particles.md): `emitter <id>` items, placed in the editor
(Details > Atmosphere > Dust and particles: Add emitter) and edited like fog
volumes: click one's icon in the view or its row in the Outliner (Emitters)
and its fields are in Details (Basics, Spawn, Motion, Look, Transform; the
reset arrow goes back to its asset's value), with the Move / Turn / Size
gizmo. The graphics setting Dust (Off, Low, High) decides how many of an
emitter's particles are drawn; Off draws none.

```
emitter 68f0c0de000001 name="Window dust" asset=dust_motes pos=400,-1000,1300 seed=7 size=900,700,700 count=1200
```

(The town house, zone 34 room 0, ships this one in
`default/rooms/zone34_room00.txt`: about a hundred motes glint in its high
windows' light around Ashley at Dust High, changing about 1.3 % of the view by 0.4/255 on average.)

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
    `ambient` everywhere; Details: Lighting, In light only or Lit),
    `ambient` (0.05), `quality` (the setting that scales it: `dust`, the
    Graphics > Dust setting; `always`: all of them, whatever Dust is).
- A room draws up to 16 enabled emitters and 16384 particles.

## The generated layer

`pc/tools/generate_lighting.py --zone N` writes the generated files from
the fixture catalog and your disc (one actor per flame of every catalogued
fixture, and the typed game glows; for rooms open to the sky, their sun and
sky). They are deterministic and sorted, and
regenerating keeps every id. Zone 9 is generated so far. To change how
every candle of a type is lit, edit the type (editor: Details > *Edit
type*, or the catalog), not the generated files.

## Editor

- **Save area** (Ctrl+S) writes your user layer: only what differs from the
  packs (`override` lines for changed lights and actors, definitions for new
  items, `delete` for removed ones).
- The Outliner marks each light and actor with its layers: `[G]` generated,
  `[P]` pack (authored), `[U]` yours only, `[G>P>U]` defined in a generated
  layer and changed in the authored one and by you. Right-click a row:
  **Revert to the layers below** (undoable), **Promote to pack**, Copy id.
  Outliner > Layers lists the files, what you deleted (Restore) and problems.
- **File > Promote room to pack** / **Promote selection to pack** saves, then
  moves your user-layer entries for the room (or the selection) into the
  pack's authored file, keeping its other lines and comments.
- Every layer file reloads live; when a pack changes on disk while you have
  unsaved edits (a `git pull`), your edits are kept on top of it.
- **The Library** (beside the Outliner; Shift+1, Window > Library) lists
  everything to place: lights, light prefabs, the fixture catalog's light
  classes, a fog volume, the fog presets and any emitters
  (`pc/assets/emitters/*.json`; yours under *Mine*). Kind and category
  chips, Favourites (the star), Mine and search filter it; Grid shows
  thumbnails (rendered by the game into `pc/build/library_thumbs`; their
  neutral scene: *Thumbnails...*, `pc/assets/editor/library_thumbs.json`)
  with a type badge per kind, List shows rows; *Pop out* floats it. Drag an
  entry onto a surface in the view (a fog volume rests on the floor, inside
  its level area; lights and wall prefabs face out of a wall),
  double-click to place at the view's centre, Ctrl+click in the view (or on
  a glow) adds the current one. Right-click an entry: *Edit prefab* (also
  under the list for the current item), *Favourite*, and for yours
  *Rename...*, *Delete...* (its file is moved aside to `.bak`) and *Copy
  file path*. *Save...* (also the Outliner's right-click menu, Edit, and a
  fog volume's Details) makes a prefab of yours from the selection: lights
  and light actors become one light prefab (an actor's lights baked as they
  are at rest), one light actor a prefab with its values, one fog volume a
  fog preset. A room's volume using a fog preset of yours still writes all
  of its values (your preset file stays on your machine).
- A selected **fog volume** has handles: its ring sets the density, the dot
  on its inner outline the soft edge, the swatch its colour; Details groups
  its fields (Basics, Look, Motion & interaction, Advanced, Transform) under
  its preset row, and each disturb weight has *Use global* beside its slider.
- **Fit to window** (a rect light's Details, or + Place > Rect light on a
  window...): click a window pane and the rect light takes its size and sits
  just in front of it, facing out.

## Contributing lighting

See "Contributing lighting" in [pc/README.md](../pc/README.md#contributing-lighting).
In short: light rooms in the editor, Promote to `default`, run
`python3 pc/tools/lighting_lint.py`, commit only `lighting/`.
