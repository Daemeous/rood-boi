# Fixture catalog: every fixture type lit once

The game's rooms hold about 2300 fixture-like polygon clusters (candles,
torches, lanterns, braziers, the save point, windows, but also cobwebs,
scrolling floor arrows, floor/ceiling glow decals and fake light shafts).
They are made from about 240 models. Instead of guessing a light for each
instance, each **model (type)** is classified once in a catalog, and every
instance of it gets the same treatment. Fixing a type fixes all its
instances at once.

| Piece | What |
| --- | --- |
| `pc/assets/fixtures/catalog.json` | the catalog (tracked): classes, types, match rules, how each is lit. Hashes and numbers only |
| `pc/tools/fixture_lib.py` | reads rooms from your disc (`ROOD_DISC`, default `disks/SLUS-01040.bin`), finds fixtures, matches them to types, computes `glows.c`'s glow keys |
| `pc/tools/fixture_scan.py` | builds (`--init`), checks (`--check`) and extends (`--update`) the catalog |
| `pc/tools/generate_lighting.py` | writes `lighting/<pack>/generated/zoneZZ_roomRR.txt` from the catalog |
| `pc/tools/lighting_gallery.py` | a local before/after gallery of a zone (game imagery: never commit it) |
| `pc/src/fixture_catalog.cpp` | the game's reader: type parameters, offsets, radii and glow treatment applied live |
| `pc/src/fixture_editor.cpp` | Details > *Fixture type* / *Edit type*, RPC `fixtures` |

## Finding fixtures (`fixture_lib.py`)

Per room, polygons are loaded in world space as the game draws them (groups
with flag 0x100 are disabled and left out). Each polygon carries a texture
**content key**: a hash of the texel indices under its UV box, read from the
zone's textures as loaded. The same model gives the same keys in every
zone. Seeds are additive polygons, polygons on a VRAM animation, warm
"flame" texels and room texture effects. Seeds within 48 units merge into an
emitter, and the small meshes touching it (24 units, at most 320 units and
80 polygons) are its holder. That makes one cluster per fixture.
`describe()` gives each cluster:

- **frame**: origin at the bottom centre of its bounds, +y down. The yaw
  makes forward point away from the wall the fixture hangs on (the nearest
  large vertical polygon within 96 units); without a wall, right follows
  the principal horizontal axis. Prefab convention: R = Ry(yaw), forward =
  (sin, 0, cos).
- **flames**: the flame sprites, which are additive polygons with one of the
  two animated flame textures (`8127aac776`, the candle flame, 7x15 texels;
  `eb4eda4936`, the fire flame, 15x31), grouped 16 units apart, with their
  centres. Flame-less light types use the emitters' area-weighted centre.
- **signatures**: `emitters` is the multiset of the additive emitter
  polygons' keys (`opaque:` and all seeds when none is additive). `holder`
  is a hash of every other polygon's key. Also the polygon count and the
  sorted extents.

## The catalog (`catalog.json`)

```json
{
  "format": 1,
  "tolerance": { "abs": 4, "rel": 0.1 },
  "classes": {
    "candle": { "family": "fire", "description": "candle (holder and flame)", "prefab": "candle",
                "params": { "flame_height": 0 }, "fixture_radius": 14, "enabled": true, "glow": "drive" },
    "shaft": { "family": "shaft", "description": "light shaft / glow volume (additive)", "prefab": "none",
               "glow": "suppress", "haze": true },
    ...
  },
  "types": [
    {
      "id": "T002",
      "class": "candle",
      "confidence": "high",
      "review": "scan",
      "instances": 125, "rooms": 62, "zones": "9,10,...",
      "match": {
        "emitters": ["8127aac776"],
        "polygons": [12, 14],
        "extent": { "min": [16, 32.9, 104], "max": [18.1, 33.9, 104] },
        "holders": ["1a2b3c4d5e", "..."]
      },
      "flames": [[-8, -48, 0.6]],
      "params": { "intensity": 0.8 },
      "offset": [0, 2, 0],
      "fixture_radius": 16,
      "glow": "drive",
      "enabled": true
    }
  ]
}
```

- **classes** (25) give the defaults:
  - `prefab` (a built-in prefab id, or `none`: not a light);
  - `params` (prefab parameters; height parameters are 0 because generated
    actors sit on the flame's centre);
  - `fixture_radius` (self-shadow exclusion: shadow rays stop this short of
    the light, so the fixture's own cage, candle or niche lets its light
    through);
  - `enabled` (inferred fill lights such as the save point and windows are
    generated **off**);
  - `glow`: what happens to the game glow the fixture's polygons make.
    `drive` links it, so its emission flickers with the light. `suppress`
    means it is not a light. `leave` leaves it alone.
  - `glow_shape` (optional): the glow's emitting shape. `auto` (the
    default) is a column when its polygons enclose room for a character
    and stand on the floor, else a small sphere; `sphere` always the
    sphere; `column` the column of its polygons whenever one fits (a flat
    group: a solid disc). The save point class says `column`: a character
    standing in it hides only part of its light ([Glows.md](Glows.md),
    "Enclosing glows").
  - `haze: true` flags the shafts as a future source for the volumetric
    haze; nothing reads it yet.

  The non-lights are cobwebs, floor arrows, decals and shafts (all
  `prefab: none` and `glow: suppress`), plus exit markers, animated
  surfaces and warm stone texels (no glow at all) and `other` (review).
- **types**:
  - `id` is stable: `--update` never renumbers or reclassifies a type.
  - `class`, plus `review`: `scan` (classified by the scan's heuristics),
    `new` (found by `--update`) or `checked` (by a person).
  - Instance counts.
  - **match**: the rule.
  - **flames**: the flames' local positions (frame above), describing the
    model.
  - Optional overrides of the class: `prefab`, `params`, `offset` (the
    light's offset from the flame centre, in the fixture's frame: x right,
    y down, z forward), `fixture_radius`, `glow`, `glow_shape`, `enabled`.
- **Matching** (`fixture_lib.match`):
  - A fixture matches a type when its emitter signature is one of the
    type's, its polygon count is in range and its sorted extents are within
    the range plus the tolerance (max(abs, rel x max)).
  - It is **exact** when its holder is one of the type's and its size is
    in the range. Otherwise it is matched **by rule**, and the generator
    and the scan report it for review.
  - With several candidates, exact wins, then the nearest size range, the
    tightest range, and the lowest id.
  - `fixture_scan.py --init` settles the scan's fuzzy grouping until every
    instance on the disc matches its own type exactly (2298 fixtures, 239
    types, 61 of them light sources).

The file layout is `prefab::json_write`'s (two spaces, short arrays and small
objects on one line). `fixture_lib.json_write` writes the same bytes, so the
editor's Save and the tools give identical files and diffs stay small.

## Generating (`generate_lighting.py`)

```sh
python3 pc/tools/generate_lighting.py --zone 9            # (re)write zone 9
python3 pc/tools/generate_lighting.py --zone 9 --room 5   # one room
python3 pc/tools/generate_lighting.py --zone 9 --dry-run  # what would change
python3 pc/tools/generate_lighting.py --zone 9 --check    # exit 1 if not up to date
python3 pc/tools/generate_lighting.py --zone 9 --gallery pc/build/gallery/zone09
```

For every matched fixture whose class has a prefab, the generator writes
one actor per flame:

```
actor gen.t002.6ae6b54163 name="candle T002" prefab=candle pos=1288,-240,2952 rot=90,0,0 seed=7047 fixture=1288,-240,2952 fixture_type=T002
```

- The id is `actor:gen.<type>.<sha1(type, zone, room, the flame polygon's
  offset)>`. It stays the same across regenerations, so overrides in
  `rooms/` or your user layer keep applying. An instance that moves to
  another type gets a new id, and old overrides then show up as dangling.
- `pos` is the flame sprite's centre (exact, from the flame texture). `rot`
  is the fixture's yaw. `seed` comes from the id. `fixture=` is the flame,
  which turns the shadow exclusion on. `flame=N` names the Nth flame of a
  multi-flame type.
- `enabled=0` marks the off-by-default classes.
- `glow=` and `glowkey=` are written when the flame or emitter belongs to a
  game glow. The key is computed exactly as `glows.c` builds its room
  groups (same triangles, 24-unit bound unions, 384/160 splits, FNV tag of
  polygon offsets, area-weighted home). The candle and torch **flame
  sprites are camera-facing planes** (group flag 0x400), which the scene
  capture leaves out, so they have **no game glow**: their actors are the
  only light. Glows exist for the save point, windows, decals, shafts and
  other additive room polygons.
- `glow <key> pos=... fixture_type=T061` is written for every game glow that
  belongs to one catalogued fixture. The game applies the type's glow
  treatment, so a cobweb's, shaft's or decal's glow stops counting as a
  light, and switching a type to suppress applies to all its glows at once.
  A glow spanning fixtures of different types is left alone and reported.

The type's params, offset, fixture radius and glow treatment are **not**
written. The game reads them from the catalog for each actor that names
the type. `prefab`, `enabled` and which fixtures exist are written: after
changing those in the catalog, regenerate. Output is sorted and has fixed
precision. Two runs give identical files (`fixture_catalog_test.py`
checks it). Never edit generated files.

Precedence for an actor's parameter: its own override (authored or user
layer) > its fixture type's (the type's own, then its class's) > the prefab's
default. The editor shows inherited type values in Details. The tooltip
says "Fixture type T002", and the reset arrow goes back to the type.

## In the editor

Select a generated actor (the Outliner row reads e.g. `Candle 3 T002`):

- **Details > Fixture type** shows the type, its class, flame index,
  instances in the room and on the disc, and review status.
- **Edit type T002** shows the type's prefab parameters (the type's own are
  highlighted, with a reset arrow back to the class), the light offset, the
  fixture radius, the game glow (Leave / Drive / Suppress), lit by default,
  and review. Every instance follows **live**.
  - **Save catalog** writes `pc/assets/fixtures/catalog.json` (tracked:
    commit it).
  - **Revert** drops unsaved type edits.
  - **Regenerate room** / **Regenerate zone** runs the generator in the
    background (it saves the catalog first). The packs reload live, and
    unsaved room edits stay on top.
- An instance's own tweak is an ordinary actor edit. Save area writes
  `override actor:gen.t002.… intensity=0.4` to your user layer, and Promote
  moves it to `rooms/`.
- A suppressed glow appears greyed in Outliner > Glows ("not a light
  (suppressed by its type)"). Its Details show the type, and **Ignore its
  type here** frees that one glow (saved by its key).
- Type edits are not in the room's undo history. Revert drops them before
  they are saved.

The catalog file also reloads live when edited by hand, unless there are
unsaved type edits in the editor.

RPC `fixtures`:

- `list` (default) returns the room's types (`all: true` for every type,
  `type` for one), its typed actors and its typed glows (with `muted` 4
  when suppressed).
- `set {type, params {key: value | null}, offset | null, fixture_radius,
  glow, enabled, review}` edits a type.
- `save`, `revert`, and `regenerate {scope: room | zone}` (waits) complete
  the set.

## Rolling out, one zone at a time

1. `python3 pc/tools/fixture_scan.py --check --zone N`: every fixture of the
   zone must match one type exactly. If not, run `--update` and review the
   new types.
2. `python3 pc/tools/generate_lighting.py --zone N --gallery DIR` (or
   `pc/tools/lighting_gallery.py --all --out DIR --resume` for the whole
   game), then look at `DIR/pages/zoneZZ_N.jpg` (each room before | after,
   game camera and a second view), `DIR/pages/types/Txxx.jpg` (a type's
   close-ups across the game) and `DIR/issues.txt` (rooms much brighter or
   darker than the game's lighting, clipped, over a light budget, not
   reached). Rooms no door leads into are reached through a made-up door
   (RPC `room_goto {cell}`).
3. Fix types, not instances: tune in the editor (*Edit type*) or in the
   catalog, regenerate, and look again. Mark reviewed types `checked`.
4. Commit `pc/assets/fixtures/catalog.json` and the zone's
   `lighting/default/generated/` files. Never commit the gallery.

Committed: every zone, 338 room files (2298 fixtures matched exactly; 1152
actors, 1105 on and 47 inferred fills off; 529 typed glows, 278 of them
suppressed: cobwebs, floor arrows, decals, light shafts). Zone 9 was
reviewed in game; the rest from the whole-game gallery (2026-10-12):

- **Review**: every light type was looked at in its contact sheet across
  all zones; 58 are `checked`. T031, T044 and T060 stay `scan` (their
  close-ups were blocked by a pillar, dark or a cutscene face).
- **Reclassified**: T008 (a hanging cage lantern, was a torch flame; zone
  9's T008 actors switch prefab torch -> lantern), T043, T048, T049
  (lanterns, were wall torches), T038 (the wall bowl T037 is: wall torch;
  one model split in two by its size), T050 (wall lantern), T059 (a lamp
  halo, not a window), T067 (the magic circle of zones 27 and 65: its glow
  lights those rooms, so `other`, glow left on, instead of a suppressed
  decal). T045 and T046 are tall pillar candles, not hearths.
- **Class defaults** (the light's look; the game's baked light stays the
  base). Candles keep the prefab's values zone 9 was approved with (0.9,
  reach 360, 1900 K). Torches 0.8 / 520 / 1800 K: the prefab's 1.6 / 760
  doubled a torch room's brightness and washed it orange (gallery ratio of
  mean brightness after / before: 2.2 in the catacombs, now 1.3; zone 9's
  approved rooms are 1.1). Braziers 0.9 / 680, fireplaces and fire sprites
  0.9 / 700, lanterns and glass lamps 0.8 / 460. Street lanterns (the
  town): cool cyan-white, 6500 K tinted 0.72 1 0.95, 1.0 / 560, almost no
  flicker: their panes are cyan, and a warm flame light turned the blue
  streets brown. The candle hall's T007 (67 candles in one room) 0.35 /
  300. Snowfly Forest and the other outdoor zones have no light fixtures
  (exit markers and floor arrows only).
- **Off by default**: save points and windows (inferred fills), as before.
- **Look at by hand**: zone 12 rooms 0-1 (window light shafts suppressed:
  0.84x the game's brightness under them), zone 9 rooms with fires (T008
  and the fire sprites are gentler than when zone 9 was approved), zones
  11 and 64 (a big hearth, 1.4x in the overview). A made-up door's cell
  outside the room's map crashes the game (zone 7 room 0); the gallery
  retries through another cell.

## Limits

- Flame sprites have no game glow (see above), so Drive does nothing for
  candles and torches. Their light is the actor's.
- Matching is by texture content and geometry. A fixture that the scan's
  clustering merges with a neighbour (two candles touching one holder) is
  one cluster. Types whose instances differ in flame count are reported
  by the generator.
- Classes came from the scan's heuristics (`review: scan`) and have not
  been checked by a person. The `other` class (8 types) is left as found.
- Shafts are only flagged (`haze`); nothing feeds the haze from them yet.
