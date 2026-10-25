# Material channel maps

Hand-made maps over a game texture, one 8-bit value per texel: how much a
texel glows or how rough its surface is, and later its height (normals)
and other material properties (wet stone in the rain, polished metal). The emissive
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
<materials>/<key>/height.png       (planned) height, for normals
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
  (`known_channel`: `emissive`, `roughness`).

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
`specular` and `metal` as fractions in [0,1]. An entry's `preset` selects one;
explicit fractions override its fields. Its `match` has a 10-digit content
`key`, `size: [width,height]`, and optional palette `clut` hash. Optional
`scope: {"room": [zone,room]}` restricts it to that room. `id` identifies
user overrides; `name`, `where`, and `review: "new"` support curation. The
runtime parser rejects invalid keys, size, fractions, palette and scope.
Unmatched textures have roughness 1, specular 0, metal 0. A map overrides
only the uniform roughness, not specular or metal. Presets are suggestions,
not an inference of material from a disc image.

The seeded entries (M001-M011) are provisional (`review: "new"`): wet stone
in the rain (the manor courtyard 2/0, Snowfly Forest 40/13), a damp cellar
floor (13/3), the Duke's polished hall floor (1/0), the Keep's glyph tile
and studded doors (29/3; the doors' `roughness.png` makes the iron studs
smooth and the planks rough), the glass lamps of 68/0. Values stay subtle:
specular 0.4-0.6 of a dielectric's, roughness 0.4-0.75.

## The data path

1. **Matching** (`emissive.cpp` `write_materials`, the emissive texture
   uses): every texture the native room and character lists draw is a use
   (page, CLUT, UV box). A use of a size some entry names is hashed (its
   content key, and its palette key when an entry needs it) and matched
   against the catalog in the current room. The last matching entry wins.
2. **Material planes**: three planes of 512 rows in the emissive slot map
   image (Vulkan; `emissive.h` `PC_MATERIAL_ROW`, rows 1028 on), cell for
   cell like the slots: roughness, specular, metal x 255. A matched use's
   box gets its entry's fractions, roughness from its `roughness.png` when
   the map has the box's size. Cells without a material: 255, 0, 0.
   Rebuilt when the catalog, a map, the room, the uses or the switch change.
3. **G-buffer**: the native and packet draws (`material.glsl`) read the
   cell of the texel the colour came from (nearest, the texture window
   applied) into a fifth target, `material` (RGBA8: roughness, specular,
   metal, 1; else 0). MSAA keeps the representative sample's.
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
- Energy: the dynamic diffuse light is scaled by (1 - F_env)(1 - metal);
  the game's baked vertex light (its own colour) is left as it is.

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
material target, 7 the specular term alone, 8 the traced reflections.

**OpenGL** draws no material response: the catalog matches (RPC, the
editor) but the GL G-buffer has no material target and the GL present
pass is unchanged. Rooms without dynamic lights show no highlights either
(the game's baked light has no direction).

## Editing (Details > Material)

The emissive panel's texture picker: **Pick texture**, click a surface.
The panel lists the catalog (`*` your entries, `[n here]` textures in view)
and edits the selected entry: preset, roughness, specular, metal (greyed
while they come from the preset), this room only, this palette only, needs
review. Edits are live (an unsaved layer over both files); **Save** writes
them into `materials.json` in the user lighting directory, **Revert** drops
them, **Copy JSON for the catalog** puts the entry on the clipboard for
`pc/assets/materials/catalog.json`. Below, the texture with its roughness
map: Pencil, Brush (radius, hardness, opacity), Eraser (255), Rectangle and
Fill (connected texels of the same palette index) paint `roughness.png` in
the user layer with the value set (0 mirror, 255 rough); Undo stroke (and
Ctrl+Z over the canvas) steps back. The panel keeps its own stroke history
(not the editor's undo).

RPC `material`: `list` (default: switch, tunables, texels, uses, entries,
presets), `pick {x, y}` or `{world}` (key, palette, size, texel, room, the
entry and the texel's planes), `new` (for the last pick), `set {id,
fields}` or `{entry}`, `select`, `remove`, `save`, `revert`, `copy {id}`,
`paint {key, value, rects, tool, at}`, `map {key}`, `undo`, `reload`.

## Planned: height and normals

Height can be estimated at load time from the player's own disc (texel
colours, edges, palette ramps), never shipped as extracted art. A handmade
`height.png` could then correct an erroneous inferred ridge or puddle.
