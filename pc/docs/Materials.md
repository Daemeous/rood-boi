# Material channel maps

Hand-made maps over a game texture, one 8-bit value per texel: how much a
texel glows, how rough its surface is, its height (the relief: per-texel
normals) and whether it is metal. The emissive
texels ([Emissive.md](Emissive.md)) are the first consumer; the layout and
the loader are meant for every channel.

## Layout

A map belongs to a **texture**, named by its content key: the fixture
catalog's key of the texture's UV box (md5 of the texel indices under the
box and its extents, 10 hex digits; [Fixture_Catalog.md](Fixture_Catalog.md),
`fixture_lib.py ikey`). The key is the same in every room the texture is
loaded into and wherever it lands in VRAM, so a map painted once applies
everywhere the texture is drawn.

```
<materials>/<key>/emissive.png     how much each texel glows (0 none, 255 fully)
<materials>/<key>/height.png       height: 0 lowest, 255 highest (the relief)
<materials>/<key>/metal.png        metal: 0 dielectric, 255 bare metal
<materials>/<key>/roughness.png    roughness: 0 mirror, 255 fully rough
```

- **Size**: exactly the texture's box (the exported reference's size: a
  texture drawn by a 33 x 31 UV box has 33 x 31 maps), texel for texel,
  nearest (no filtering, no offsets): pixel (x, y) is texel (u0 + x, v0 + y).
- **Format**: 8-bit grayscale PNG. On import a colour image counts by its
  luminance and an image whose alpha is used (an alpha-only layer) by its
  alpha; the editor and the importer always write grayscale.
- **Layers**, the user's winning per channel file:

| Layer | Where | |
| --- | --- | --- |
| shipped | `pc/assets/materials/<key>/<channel>.png` (`$ROOD_MATERIALS_SHIPPED`) | committed; single-channel grayscale only |
| user | `materials/<key>/<channel>.png` in the user lighting directory (`pc/user/lighting`, `$ROOD_LIGHTS`; `$ROOD_MATERIALS` overrides) | the artist's |
| exports | `pc/exports/materials/<key>/reference.png` (`$ROOD_MATERIAL_EXPORTS`) | gitignored: the texture itself, game art, never committed |

- **Loader** (`pc/src/materials.cpp`, `materials.h`): generic. It finds
  whatever channel files a texture has (`channels(key)`: any `<name>.png`
  but `reference.png`), loads one on demand (`channel(key, name)`: the
  user's, else the shipped one) and reloads a file that changed on disk
  (`poll()`, about once a second: mtime and size), so a map painted in an
  outside editor shows live. Consumers ask for the channels they know
  (`known_channel`: `emissive`, `roughness`, `height`, `metal`). Blank
  templates: roughness 255, height 128 (flat, room to carve down or raise),
  the rest 0.

## Emissive maps

An emissive entry can take its mask from the maps instead of shapes:
`"mask": { "map": "emissive" }`. Each texture the entry matches then glows
per texel by its own `emissive.png` (amount / 255), times the entry's
shapes and filters if it has any, within its scope, with its tint,
strength and light weight. A matched texture without a map (or with one of
another size) does not glow; the editor and RPC `emissive materials` list
it. The amount reaches the shaders through the slot map's amount rows
(`emissive.glsl`) and the lights through the same integration as filtered
masks: a painted area's emitting share and brightness weight its light.

Workflow (Details > Emissive, an entry with *Painted map* on):

1. **Export reference**: `reference.png` (the texture as RGBA, transparent
   texels transparent) into the export folder, and an empty `emissive.png`
   into your materials folder (if it has none yet). The exporter refuses
   any folder inside `pc/assets` or the shipped layer.
2. Paint `emissive.png` in any editor (Photoshop, Aseprite: the reference
   as a layer underneath); the game reloads it when saved.
3. Or **Import** a PNG painted elsewhere (its size must be the texture's),
   or use the [emissive texture toolbar](Emissive_editor_tools.md): Pencil (B),
   Brush (radius, hardness, opacity), Eraser (E), Rectangle (R; Alt subtracts),
   connected same-index Fill (G), and Select colour (W). X swaps add/remove;
   Clear and Invert operate on the painted map, independently of index filters.
   Painting and imports use ordinary editor Ctrl+Z/Ctrl+Y undo/redo.

RPC `emissive`: `materials {key}` (the texture's channels, map problems,
the folders), `export_reference {key, dir, channel}`, `import_map {key,
path, channel}`, `paint {key, channel, fill, rects: [[x0, y0, x1, y1,
value]...]}`; `key` defaults to the last pick's texture (which must be in
view for its size and texels).

## Committing maps

Painted emissive maps may be committed under `pc/assets/materials` (copy
the user file there; review it like any curated entry; an entry using it
goes into the curated catalog). The publish leak scan (`pc/tools/publish.sh`
with `pc/tools/check_material_maps.py`) lets only
`pc/assets/materials/<10-hex-digit key>/<allowed channel>.png` through, and
only as single-channel grayscale PNGs of at most 256 x 256; every other
image still fails the publish. References never leave the export folder.

## Roughness and the catalog

`roughness.png` is exactly one 8-bit value per source texel: 0 is mirror
smooth, 255 is fully rough. No map defaults to 255. An exported blank
roughness template is filled with 255, not emissive's 0. A mismatched-size
map must not be stretched; sample the value at the same integer texel as
the PS1 colour (no bilinear filtering).

`pc/assets/materials/catalog.json` is curated; `materials.json` in the user
lighting directory wins per entry id (`$ROOD_MATERIAL_CATALOG_USER` overrides).
`$ROOD_MATERIAL_CATALOG` overrides the shipped catalog path. Both contain
`format: 1`, named `presets` and `entries`. A preset defines `roughness`,
`specular` and `metal` as fractions in [0,1], and optionally `auto_height`
(a boolean: estimate the relief from the texture's colours, below) and
`bump` (a multiple of the global relief depth, 0..4, default 1). An entry's
`preset` selects one; explicit fields override it. Its `match` has a 10-digit content
`key`, `size: [width,height]`, and optional palette `clut` hash. Optional
`scope: {"room": [zone,room]}` restricts it to that room. `id` identifies
user overrides; `name`, `where`, and `review: "new"` support curation. The
runtime parser rejects invalid keys, size, fractions, palette and scope.
A **palette entry** has no `key` (and usually no `size`): `"match": {"clut":
"<palette key>"}` matches every texture drawn with that palette (of that
size, when it names one), wherever no entry names the texture itself (a
texture's own entry always wins over a palette entry, whatever their
order). A room's pieces of one painted surface share a palette (the Keep's
walls are 218 boxes of one palette, its floor 49 of another), so one
palette entry does what dozens of texture entries would; scope it to the
room (`scope`), since a palette can be reused elsewhere for other art.
While any palette entry exists every drawn box is hashed (cheap: once per
VRAM change). Unmatched textures have roughness 1, specular 0, metal 0, no relief. A
roughness map overrides the uniform roughness, a metal map the uniform metal
(per texel); specular stays the entry's. Presets are suggestions, not an
inference of material from a disc image.

Preset values are physical: `specular` 1 is a dielectric's 4% reflectance
at normal incidence (stone, wood, glass), 0.5 water's 2% (a wet film, gel),
and metal 1 reflects with the albedo's colour; what makes a wet or polished
surface read is its low roughness, not a higher reflectance. Shipped:
`stone` (0.7 rough, auto_height), `rough stone` (0.82, auto_height: walls,
mortar relief), `flagstone` (0.62, auto_height, bump 0.7: a worn floor,
smoother and flatter), `plaster` (0.9, specular 0.8, auto_height, bump 0.4),
`wet stone` (0.32, specular 0.5, auto_height), `worn wood` (0.8,
auto_height), `cloth` (0.95, specular 0.5), `worn iron` (0.55, metal,
auto_height), `polished metal` (0.3, metal), `glass` (0.1), `gel` (0.35,
specular 0.5).

The seeded entries (M001-M011) are provisional (`review: "new"`): wet stone
in the rain (the manor courtyard 2/0, Snowfly Forest 40/13), a damp cellar
floor (13/3), the Duke's polished hall floor (1/0), the Keep's glyph tile
and studded doors (29/3; the doors' `roughness.png` makes the iron studs
smooth and the planks rough, their `metal.png` the studs, bars and
fittings metal: hand-made masks, the same texels as the roughness maps;
worn wood with the relief estimated), the glass lamps of 68/0.

The Keep (29/3) is seeded room-wide (M012-M016, `review: "new"`), from
its survey (below): the walls' palette `rough stone` (M012, 54% of the
room's textured view over the survey's four views), the floor's palette
`flagstone` (M013, 40%), the studded door's other two boxes (M014, M015:
the boxes overlap M010's and M011's texels, so their maps are M010's and
M011's hand-made maps re-cut to the box: the same texels, studs metal), and
the glyph tile (M016, `flagstone`). With the door's M010/M011 that is 96%
of the textured view; the rest is the black void beyond the walls (a 1 x 1
box), Ashley (actor materials: Next) and the lantern. The look, on Vulkan
from the game camera: without a dynamic light about 3% of the pixels
change by more than 4 levels (mean 1 level): the floor and walls take a
faint sheen of the baked lantern light and their mortar seams a little
ambient occlusion; with a point light near a wall about 15% change (some
by 30-80 levels): the stone blocks' faces lift towards the light and their
seams fall into shade, and the flagstones get a soft, rough highlight.
Nothing reads as plastic: rough stone stays matte.

## Surveying a room

What to give materials first is what covers the screen. Doing a room:

1. `python3 pc/tools/material_survey.py ZONE ROOM` starts its own headless
   game (its own RPC socket, scratch lighting and material layers: never a
   running game), goes to the room and surveys it from four standard views
   (`game`: the game camera; `left`, `right`: the free camera at its eye
   turned 30 degrees; `back`: from the room's far side looking back, the
   walls the game camera never faces). It prints the palettes by share
   (what a palette entry would cover) and the textures (key, size, palette)
   by mean share, each with its views' shares, its entry and its atlas
   group (textures whose boxes overlap on one page and palette: pieces of
   one painted region), and writes `survey.json`, `contact.png` (every
   texture as drawn, from VRAM, labelled: what each one is) and the views'
   frames and attribution maps (`map-<view>.png`, the contact sheet's
   colours) into `pc/build/material_survey/ZONE-ROOM`. These are game art:
   they stay local, never committed.
2. In the game, Details > Material > **Textures in this room** lists the
   same for the current view (the game camera, or the free camera: move it
   around the room), refreshed about once a second: a thumbnail from VRAM
   (hover: the texture large), its share of the view, its palette's share,
   and its entry, or a preset dropdown. Picking a preset creates a user
   entry with it at once (live, unsaved: Save keeps it), for **its whole
   palette** (default: a palette entry) or **the texture**, scoped to this
   room (the texture's entry also to its palette), review "new", its `where`
   noting the share it had. Then tune it in the entry editor as any other.
3. Copy the entries worth shipping into the catalog (Copy JSON) and paint
   maps where a texture is only partly one material (iron fittings on wood:
   a `metal.png`).

How it measures: the native lists' textured triangles (room, characters,
props; effect sprites and shadows left out) are rasterised on the CPU
into a grid over the tools' view (160 cells wide by default), with a depth
test and the game's back-face rule (the PS1's NCLIP: double-sided polygons
both ways), and each cell goes to the texture its nearest triangle
samples, by the polygon's UV box, as the material matching sees it.
Boxes of the same texture (same key and palette, anywhere in VRAM) merge.
`material_survey_smoke` checks it against the picker (RPC material pick)
at sampled pixels.

RPC `material`: `survey {grid, map, cull}` (the view's textures: key, size,
palette key `clut`, `tpage`, `palette`, `boxes`, `cells`, `share`, `entry`;
`textured` and `untextured` shares, the view's `eye` and `forward`, `map`:
the cells' texture index, -1 nothing, -2 untextured; `cull` 0 draws both
faces), `texture {key, palette}` (a texture in view as base64 RGBA), and
`new {key, palette, preset, share, whole_palette}` (an entry for a texture
in view by key, as the panel's one click).

## The data path

1. **Matching** (`emissive.cpp` `write_materials`, the emissive texture
   uses): every texture the native room and character lists draw is a use
   (page, CLUT, UV box). A use of a size some entry names is hashed (its
   content key, and its palette key when an entry needs it) and matched
   against the catalog in the current room. The last matching entry wins.
2. **Material planes**: six planes of 512 rows in the emissive slot map
   image (Vulkan; `emissive.h` `PC_MATERIAL_ROW`, rows 1028 on), cell for
   cell like the slots: roughness, specular, metal x 255; the relief's
   slopes along u and v (0 none, else 128 + slope x 63.5) and its cavity
   (x 255). A matched use's box gets its entry's fractions, roughness and
   metal from its `roughness.png` and `metal.png` when they have the box's
   size, and its relief (below). Cells without a material: 255, then 0s.
   Rebuilt when the catalog, a map, the room, the uses, the switch or the
   estimate's settings change.
3. **G-buffer**: the native and packet draws (`material.glsl`) read the
   cell of the texel the colour came from (nearest, the texture window
   applied) into a fifth target, `material` (RGBA16 UNORM, packed by
   `material_pack.glsl`: roughness and specular, metal and cavity and a
   "has material" bit, and the relief normal for a unit depth, octahedral;
   all 0: no material). The native draws build the relief normal from the
   slopes (below); packets carry no position for it (their cavity only).
   MSAA keeps the representative sample's.
4. **Lighting** (`scaled.frag`, the present pass): where the target says
   material, an energy-conserving GGX lobe (below) is added; elsewhere the
   expression is the old one, untouched.
5. **Reflections** (`rt_reflect.comp`, `rt_reflect_filter.comp`, path
   tracing on): the surface pass copies the material at each GI pixel; one
   ray per pixel drawn from the GGX lobe (visible normals) brings back the
   hit surface as the game shows it plus the dynamic lights and sun on it
   (shadow rays), or the sky; a roughness-wide edge-aware blur and a
   reprojected temporal blend (history clamped to the neighbourhood,
   shorter on smoother surfaces) denoise it. NRD's specular path is not
   used (NRD runs RELAX diffuse only).

## Shading

GGX (Trowbridge-Reitz) normal distribution, height-correlated Smith
visibility, Schlick Fresnel, in the lights' units (the diffuse term has no
1/pi, so the lobe carries pi). F0 = 0.04 x specular for a dielectric,
albedo x specular for metal (mixed by metal); F90 = specular (dielectric) to
1 (metal). Roughness is perceptual (alpha = roughness squared), plus the
bias, clamped to 0.03..1.

- Direct: every dynamic light with its own attenuation, shape and shadow
  (the diffuse term's: traced visibility, shadow maps); a point or spot
  light's size widens the lobe (renormalised, Karis 2013); a rect light's
  highlight comes from its point nearest the reflection ray; the sun with
  its disc's angular size.
- Indirect: the traced reflections times the split-sum Fresnel (Karis'
  analytic fit of the environment BRDF).
- The environment: the traced reflections show the room's geometry, but
  the game's baked vertex light comes from lights the trace cannot see
  (they have no geometry), so the light it brings to the pixel is also
  reflected as a uniform surround (the split-sum's environment at its
  simplest), times `env_specular`; where no reflection is traced, the
  indirect light too.
- Energy: the dynamic diffuse light is scaled by (1 - F_env)(1 - metal),
  and so is the game's baked light by `baked_energy` (1: metal shows its
  reflections in place of the baked diffuse; 0: the baked light untouched,
  as step 1 had it).

### Why step 1 was faint

Measured in the Keep (29/3) at the game camera: the doors' entries set
`specular` 0.45 (F0 1.8%, under half a real dielectric's), the iron was a
dielectric too, the room has no dynamic light (its lantern is the game's
baked light only, no light in the lighting pack), so no lobe ever lit, and
the traced reflections (the walls' baked look, about 0.1-0.15) times a
1.8% Fresnel gave about one level of 255 on the door (specular strength x4:
a mean change of 1.4 levels). Now the studs, bars and fittings are metal (F0 their own colour, about
50%), the presets are physical, and the baked light is reflected as the
surround: strength scales the metal's whole response, and the iron reads
by its tint and its Fresnel. Visible highlights still need light with a
direction: a dynamic light at the lantern (the fixture catalog) gives them.

## Height and normals (the relief)

A texture's relief comes from its `height.png` (0..255 is 0..1 height
unit), else, where its entry (or preset) has `auto_height` and the global
`auto_height` switch is on, from an estimate made at run time from its own
texels on the player's disc: the luminance, stretched between the
texture's 5th and 95th percentiles (dark seams and gaps low, bright faces
high), times `auto_height_strength`. Nothing is shipped from the estimate.
A painted `height.png` replaces the estimate entirely. Both are times the
entry's `bump`. Off by default per entry: only entries and presets that
say `auto_height` get bumps, so a catalog without the flag and without
maps draws exactly what it did.

- **Slopes**: a Sobel filter over the heights (edges clamped to the box),
  one slope pair per texel: nearest, so the bumps stay pixel art.
- **Normals**: the native draw carries the slopes into view space with the
  texture coordinates' screen derivatives (the cotangent frame), with a
  texel's size so a height unit is `bump_strength` texels of depth, and
  stores the relief normal for a unit depth. The present pass scales its
  tangential part by `bump_strength` onto the smoothed normal.
- **Where it fades**: the slopes are one per texel, nearest. Where a
  pixel's footprint in the texture is stretched along one direction (a face
  edge-on to the view, or one whose texture coordinates collapse to a sliver,
  like the sides of the Keep's arches and pillars) the cotangent frame is
  degenerate and the normals speckle in bands. The relief (and its cavities)
  fades smoothly to flat by the ratio of the footprint's long side to its
  short one (`relief_aniso`: starts there, gone at twice) and by its long
  side in texels per pixel (`relief_footprint`, the same). Broad faces are
  unchanged (the Keep's walls and floor differ by 0 in the normals view).
- **What it bends**: the dynamic lights' diffuse cosine (the attenuation,
  shapes and shadows stay the geometry's), the sun's, the GGX lobe and the
  Fresnel of the environment. The geometry's normal keeps the shadows, GI,
  AO and the traced reflections' rays.
- **The baked light** has no direction, so the relief cannot shade it;
  only the cavities (a texel below its eight neighbours' mean) lose a share
  of the ambient light (baked, indirect, sky), `bump_occlusion`: physical
  (a pit sees less of its surround) and subtle.

Tunables (lighting quality keys: settings, Graphics > Lighting quality >
Materials, RPC `graphics`):

| Key | | Default |
| --- | --- | --- |
| `bump_strength` | relief depth in texels per height unit; 0: no relief (identical to none) | 1 |
| `auto_height` | the estimate's switch (entries with `auto_height`) | 1 |
| `auto_height_strength` | the estimate's height range, 0..2 | 0.5 |
| `bump_occlusion` | the cavities' share of ambient light lost, 0..1 | 0.5 |
| `relief_aniso` | the footprint's stretch (long side / short side) where the relief starts to fade, gone at twice; 0: no limit (Material panel too) | 4 |
| `relief_footprint` | texels per pixel (long side) where the relief starts to fade, gone at twice; 0: no limit | 3 |
| `env_specular` | the baked (and indirect) light reflected as the surround, 0..1 | 1 |
| `baked_energy` | the baked light under the material's energy split, 0..1 | 1 |

No maps and no flags: the picture is byte-identical (material_smoke).

Tunables (lighting quality keys, so the settings file, Graphics > Lighting
quality > Materials, and RPC `graphics` set them):

| Key | | Default |
| --- | --- | --- |
| `specular` | the quality switch: 0 turns the lobe off entirely (no material planes, nothing read: the picture is the one without materials) | 1 |
| `specular_strength` | global strength of the lobe and the reflections, 0..4 | 1 |
| `roughness_bias` | added to every roughness, -1..1 | 0 |
| `reflection_bounces` | the path tracer's reflection rays: 0 none (the lights' highlights only), 1 the hit lit by the lights and sun, 2 plus one diffuse bounce at the hit | 1 |

With no entry matching anything in view (or `specular 0`) the frame is
byte-identical to the renderer before materials (checked with captures of
the old and new binaries; `material_smoke` checks the switch and empty
catalogs). View modes for debugging (RPC `graphics view_mode`): 6 the
material target, 7 the specular term alone, 8 the traced reflections, 9
the shading normals with the relief (world xyz as rgb), 10 the cavities.

**OpenGL** draws no material response, relief or metal: the catalog and
the maps match (RPC, the editor, the planes on the CPU) but the GL
G-buffer has no material target and the GL present pass is unchanged
(material_smoke checks the frame is identical). Rooms without dynamic lights show no highlights either
(the game's baked light has no direction).

## Editing (Details > Material)

The emissive panel's texture picker: **Pick texture**, click a surface.
The panel lists the catalog (`*` your entries, `[n here]` textures in view)
and edits the selected entry: preset, roughness, specular, metal (greyed
while they come from the preset), this room only, this palette only, needs
review, the height estimate (auto_height) and the relief multiple (bump). Edits are live (an unsaved layer over both files); **Save** writes
them into `materials.json` in the user lighting directory, **Revert** drops
them, **Copy JSON for the catalog** puts the entry on the clipboard for
`pc/assets/materials/catalog.json`. Below, the texture with one of its
maps (Roughness, Height or Metal): Pencil, Brush (radius, hardness,
opacity), Eraser (the channel's neutral value: 255, 128, 0), Rectangle and
Fill (connected texels of the same palette index) paint the channel's PNG
in the user layer with the value set; Undo stroke (and Ctrl+Z over the
canvas) steps back. The panel keeps its own stroke history (not the
editor's undo). **Preview the height estimate** gives every material in
view without a `height.png` the estimate (not saved); **Debug view**
switches the present pass to the material, specular, reflection, relief
normal or cavity view.

RPC `material`: `list` (default: switch, tunables, texels, uses, entries,
presets), `pick {x, y}` or `{world}` (key, palette, size, texel, room, the
entry and the texel's planes), `new` (for the last pick, or `{key,
palette, preset}`: Surveying a room), `survey`, `texture`, `set {id,
fields}` or `{entry}`, `select`, `remove`, `save`, `revert`, `copy {id}`,
`paint {key, channel, value, rects, tool, at}` (channel roughness, height
or metal), `map {key, channel}`, `undo`, `preview {auto_height}`, `reload`.
`list` gives the relief tunables and, per use, `painted_metal` and `relief`
(`painted`, `estimated` or empty); `pick`'s `material` has the six planes.

## Next

- Actor materials: characters and enemies (slimes: gel, wet; armour:
  metal) draw through the same native lists, so their textures can take
  entries; their relief needs the same frame (it is built per pixel, so it
  follows the animation).
- Transparency: translucent surfaces (glass, water, slime bodies) write no
  material today (the translucent passes keep the opaque G-buffer); a
  material there needs its own pass.
- Reflections along the relief normal (the traced rays use the geometry's).
