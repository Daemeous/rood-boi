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
  cone size(w,h) shadows enabled`. Actor fields: `name prefab pos rot(yaw,pitch,roll)
  seed enabled glow glowkey fixture fixture_radius fixture_shadows
  fixture_type flame`, and any parameter of its prefab
  (`pc/assets/prefabs/*.json`) it overrides. Glow: `pos muted intensity
  tint size reach fixture_type`. Texture effect: `speed freeze`. Setting:
  `value`.
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
  bounds), `haze_anisotropy` (forward scattering, also the fog's) and
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

## Fog volumes

Local mist (a cellar's damp corner, a forest hollow, a sewer outlet):
`fog <id>` items, placed in the editor (Details > Atmosphere > Fog volumes:
Add volume, then the Move / Size / Turn gizmo) like lights.

```
fog 68ea4c1200a3f1 name="Cellar mist" shape=sphere pos=320,-60,640 size=400,120,300 yaw=30 density=0.006 fill=0.01,0.012,0.012
```

- Fields: `name`, `shape` (`box` or `sphere`; box), `pos` (its centre),
  `size` (full extents x, y, z; 256,128,256), `yaw` (degrees about the
  vertical; 0), `density` (0.004), `edge` (the soft part, a fraction of the
  half size; 0.5), `color` (albedo; 1,1,1), `fill` (0,0,0), `noise` (0.5; the
  wisps drift with the room's fog wind), `enabled` (1).
- A room renders up to 16 enabled volumes; they add to the haze and the
  ground fog and may reach past the room's bounds.

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

## Contributing lighting

See "Contributing lighting" in [pc/README.md](../pc/README.md#contributing-lighting).
In short: light rooms in the editor, Promote to `default`, run
`python3 pc/tools/lighting_lint.py`, commit only `lighting/`.
