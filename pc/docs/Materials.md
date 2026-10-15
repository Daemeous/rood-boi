# Material channel maps

Hand-made maps over a game texture, one 8-bit value per texel: how much a
texel glows today, and later its height (normals), roughness and other
material properties (wet stone in the rain, polished metal). The emissive
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
<materials>/<key>/roughness.png    (planned) roughness, for reflections
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
  (`known_channel`: `emissive` now).

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
   or paint in the editor: *Paint* (brush radius, hardness, opacity, value;
   left paints, right erases; one stroke per press, *Undo stroke*), *Fill*,
   *Invert*, *Clear*, magic wands (*the filters' texels*: what the entry's
   shapes and filters pass; *CLUT index (picked)*: the picked texel's index)
   to seed a selection, and *Texture underneath* (its opacity).

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

## Planned: height and roughness

Height and roughness come mostly from the textures themselves: generated at
load time from the player's own disc (the texel colours, edges and the
palette's ramps give a plausible height; luminance and hue a roughness),
never shipped as images. Hand-painted maps in the same layout
(`height.png`, `roughness.png`) then correct the generated ones where they
are wrong (a mortar line read as a ridge, a puddle). The generated maps
need no files: they are a function of the texture, cached by its key.
