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
- **Layers**, the user's winning per channel file, then the shipped one,
  then the baked one:

| Layer | Where | |
| --- | --- | --- |
| baked | `pc/build/materials/<key>/<channel>.png` (`$ROOD_MATERIALS_BAKED`) | gitignored: derived from the game's art by `material_bake.py` ([Baked maps](#baked-maps)), never committed; used while `materials_baked` is on |
| shipped | `pc/assets/materials/<key>/<channel>.png` (`$ROOD_MATERIALS_SHIPPED`) | committed; single-channel grayscale only |
| user | `materials/<key>/<channel>.png` in the user lighting directory (`pc/user/lighting`, `$ROOD_LIGHTS`; `$ROOD_MATERIALS` overrides) | the artist's |
| exports | `pc/exports/materials/<key>/reference.png` (`$ROOD_MATERIAL_EXPORTS`) | gitignored: the texture itself, game art, never committed |

- **Loader** (`pc/src/materials.cpp`, `materials.h`): generic. It finds
  whatever channel files a texture has (`channels(key)`: any `<name>.png`
  but `reference.png`), loads one on demand (`channel(key, name)`: the
  user's, else the shipped one, else the baked one) and reloads a file that changed on disk
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
`scope: {"room": [zone,room]}` restricts it to that room, `scope: {"rooms":
[[zone,room], ...]}` to those rooms (one or more, no repeats; one of the
two, not both). `id` identifies
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
VRAM change). A palette the game animates is still one palette (below,
Animated palettes). Unmatched textures have roughness 1, specular 0, metal 0, no relief. A
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
specular 0.5); organic: `foliage` (0.7, specular 0.7: leaves, a faint waxy
sheen, no relief: they are cut-outs), `grass` (0.9, specular 0.6,
auto_height, bump 0.5), `earth` (0.95, specular 0.7, auto_height, bump
0.6), `bark` (0.88, specular 0.8, auto_height, bump 1.2: deep furrows),
`moss` (0.97, specular 0.5, auto_height, bump 0.4).

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

### Animated palettes

The game animates some palettes: it uploads new colours into a CLUT while the
texels stay (enemies' flashes and colour cycles; the Catacombs' first room,
9/0, with its enemies). A palette key is the md5 of the colours, so an entry
keyed by one frame's colours matched only on the frames that showed them, and
the material switched on and off (the bulk-accepted review entries were keyed
by whatever frame the survey saw).

Decision (mine, the palette-flicker worker, 2026-10-31): **a CLUT slot (colour depth and
CLUT position) is one palette while the player stays in a room, whatever its
colours do.** Every frame the drawn palettes' colours are read (cheap: a
comparison per slot; hashed only when they change); each key a slot shows is
recorded as one of its **variants** (at most 64; a room change forgets them).
A palette entry, or a texture entry with a palette, matches a slot when it
names any of its variants (the colours now first, then the others in the
order seen; a texture's own entry still wins over a palette entry). When a
slot shows a new variant its uses are resolved again at once, so an entry
applies from the first frame its colours appear and then stays. Its
**identity** is the variant drawn in the most frames this room (a palette
that flashes or cycles now and then: its resting colours); the survey, the
Material review's proposals, the picker (Accept, Details > Material, the
Inspector), the review's highlight and Focus name and find a palette by it.
Entries written before (by any variant) keep working without being accepted
again: they apply as soon as that variant is drawn. A palette that never
animates has one variant, its key: matching is exactly as before (the frames
and uses in the Keep 29/3 and the Catacombs 13/3 are byte-identical with
the user's 399 entries, GL and Vulkan). Not covered: a palette whose slot the
game refills with unrelated art in the same room (its variants then include
both).
`material_palette_anim_smoke` checks it.

Emissive entries' `clut` follow the same rule (the emissive-palettes
worker, 2026-10-31): a match's palette is met when it names the colours now
or any variant of the use's slot, a slot's new variant has its textures
matched again at once (the last matching entry winning as ever), and the
emissive picker (Details > Emissive, RPC `emissive pick`'s `clut_key`, what
*Only with the picked texture's palette* writes) names the slot by its
identity; the checkbox shows ticked for any variant. A window light pattern
qualified by a palette (`key@palette` in `pattern_faces`) finds the slot by
any variant too. A glow on an animated palette therefore stays on through
the animation: in 9/0 with its enemies an entry on a slot's identity, read
while another variant showed, was late 4 times of 4 and missing on 42 of 240
frames before, 0 after (`emissive_palette_anim_smoke`). A slot's growth is a
flag on the slot (taken by the frame's palette pass), so a look at a slot
earlier in the frame (matching, an RPC between frames) no longer hides a new
variant from the material planes. Rooms without animated palettes are
unchanged: the Keep 29/3, the Catacombs 13/3, the town houses 34/0 and 35/0,
the forge 43/0 and the magic crates 53/21 (with enemies) give the same frame
buffer, presented image, matched emissive uses and material uses as the
previous binary, GL and Vulkan. (Every room shows a few 4-bit slots with two
variants: the colours left in the slot from before the room's own upload;
an entry naming such a leftover could match in the new room's first frames.
Not seen with the shipped entries.)

## Masters and instances

(`pc/docs/plans/2026-10-30-master-materials-plan.md`.) A **master material**
is a shader exposing named parameters; this version has one, the **standard
surface** (`pc/assets/materials/masters/standard_surface.json`: the five
fields above, `roughness`, `specular`, `metal`, `auto_height`, `bump`, with
their defaults, ranges, labels and tips, and the texture slots: the game
texel and the channel maps), the planes path described below. A master
generated from a node graph (part 4) is one more file in that folder
(`$ROOD_MATERIAL_MASTERS`, else `masters/` beside the shipped catalog) with
its own `shader` and parameters; an instance of another master writes its
values under `params`.

A **material instance** is a master, an optional **parent** instance, the
values it sets itself and an optional assignment. The catalog files are
read as instances, losslessly and without rewriting: every preset is a root
instance (id = its name) and every entry an instance whose parent is its
preset and whose own values are its explicit fields; the entries that shade
(`Entry` in `materials.h`) are derived from the assigned instances, so a
converted layer renders exactly as before (`material_instances_smoke`; a
real layer: `pc/tests/material_migration_check.py --user FILE`, on a copy).
Values resolve live: an instance's value is its own, else its parent's, up
to the root, else the master's default, so editing a preset changes every
instance under it (the one difference from the old reader, which copied a
preset's values at parse time). An entry naming an unknown preset or parent
is listed unresolved, makes no entry (as before) and is reported.

Edits are in memory: the two files are read on load, on Revert, on
`material reload` and when one changes on disk, and written on Save only.
A value edit (the panel's sliders, RPC `material_instance set`) changes the
instance and derives again only the entries of it and of the instances
below it, so a dragged slider shows on the very next frame with no file
read; a graph instance's value only refills the values table (the include
is regenerated for structure only). RPC `material_instance status` counts
the reads.

Writing (Save) keeps the files' layout and format: a root goes into
`presets`, the rest into `entries` with `preset` naming the root preset at
the top of its chain (what older readers see) and `parent` only when the
parent is not that preset; an unassigned instance (a user-made preset with
a parent) has no `match`; another master is `master`. A file with none of
those stays format 1; one with any gets `format: 2`. `material_bake.py` and
`material_rollout_apply.py` read either.

**Window > Materials** shows the instances as a tree and the selected one's
parameters as the master lists them: an own value in normal text with
*Reset* (to the parent's), an inherited one greyed with the ancestor it comes
from; the assignment (a texture key, a palette, *This room only*, *Assign
to selection's texture* / *its palette*: the surface or model selected in
the view, scoped to this room; *Unassign*); *New child*, *Duplicate*,
*Remove*, *Save*, *Revert*, *Copy JSON for the catalog*, *Open in Details >
Material*. Details > Material's own entry editor still works (its preset
combo lists every root). The panel also exists in Lua: the shipped plugin
`rood.materials` (`pc/plugins/rood.materials/main.lua`, **off** by default:
the C++ panel stays Window > Materials; turn it on in the Lua console's
Plugins list). On, it replaces the C++ window (`rood.dev.window{replaces =
"materials"}`) and draws the same tree, rows and tools with `rood.imgui` on
RPC `material_instance` alone (a value edit is the same `set` the C++
sliders make); the Name and Where fields apply when left (Enter or a
click elsewhere). **Window > Material preview** is a sphere shaded
on the CPU with the same GGX lobe, textured nearest with the instance's
texture when it is drawn in the room (a palette instance: the first texture
it draws here; else a grey swatch). Where the instance draws the texture
here, each texel takes the material planes' values (painted roughness and
metal, specular) and their relief (the height map's or the estimate's
slopes bend the sphere's normal as scaled.frag bends its shading normal,
times `bump_strength` and `material_preview_relief`; the cavity darkens the
ambient by `bump_occlusion`); a texture drawn by another entry uses the
instance's own painted maps. **Lit like the room** (`material_preview_room`,
on by default): the key light is the room's light that delivers most at
Ashley (`material_preview_room_height` units above her feet; its colour
times the renderers' window (1 - x^2)^2; the environment's sun competes by
its colour), turned into the game camera's view, times
`material_preview_room_key`; the ambient is the game's own room light
colour at Ashley (BATTLE's actor lighting, 128 = 1) times
`material_preview_room_ambient`. No light reaching Ashley keeps the
preview's own key light (with the room's ambient); off, or with no room,
the preview's own key light and ambient (`material_preview_light`, `_yaw`,
`_pitch`, `_ambient`). The swatch's grey and the size are
`material_preview_swatch` and `_size` (Graphics, RPC `graphics`). Lantern,
candle and emissive intensities are read, never changed. It is the same
picture on GL and Vulkan (`material_instances_smoke` compares the two runs'
digests). RPC `material_instance` and `material_preview`: Extensions.md.

## Node graphs

(`pc/docs/plans/2026-10-30-node-materials-plan.md`; `pc/src/material_graph.cpp`.)
A **graph master** is a master file with a `graph`: nodes (an instance of a
node definition each, with the values of its unlinked knobs, which knobs are
pinned on its body, and its position), links (an output to an input) and
the promoted knobs (knob -> parameter name). The shipped
`standard_surface_graph` is the surface planes straight into the output,
and renders exactly as `standard_surface` (`material_graph_smoke` proves
the Keep byte for byte); a new graph starts as that. The user's graphs are
saved under `<user maps layer>/masters` (`$ROOD_MATERIAL_GRAPHS`), read
beside the shipped masters; every graph master has the five standard
parameters (they feed the planes the *Surface planes* node reads) and then
its promoted knobs, so Window > Materials shows them on its instances like
any other.

A **node definition** is a small GLSL function in a JSON file
(`pc/assets/materials/nodes/<name>.json`, the user's under `<user maps
layer>/nodes`; `$ROOD_MATERIAL_NODES`): `inputs` (name, `type` float,
float2, float3 or float4, `default`, `min`, `max`, `pin` to show it on the
node body by default, `color` for a swatch, `time` for an input that reads
the game time when nothing is linked to it), `outputs`, and `glsl`, the
body over those names (`result = mix(a, b, t);`). A developer adds a node by
adding a file; the user sees a node with knobs and never the code. Built in:
the sources *Game texel*, *Surface planes* (roughness, specular, metal, the
packed surface and the relief as the planes hold them), *UV* (texels; its
*Snap to texel* knob, on by default, reads the texel's centre so results
stay pixel art; off is the raw per-pixel coordinate), *Normal*, *Vertex
colour*, *Time* (the game time: *Seconds* within its *Loop* knob, 60 by
default, and *Phase*, 0..1 through the loop), and *Surface output* (base colour, roughness, specular, metal, glow,
surface, relief; an unlinked input leaves the game's value). A float output
feeding a wider input broadcasts; any other mismatch, a cycle, two links
into one input or an unknown node is refused naming the node.

**The game time** is vsyncs at 60 a second: it moves only as the game does,
so it stops while the game is held (pause, the photo mode) and a photo
render, which runs the game from the held moment and rewinds it, draws the
same time every time (the held picture never flickers). It loops each hour
to keep float precision: a *Time* loop or a *Pulse* rate that divides an
hour stays seamless across it. The shipped example is the *Pulse* node
(`nodes/pulse.json`, Animation): a smooth pulse between *Low* and *High*,
*Rate* times a second, its time input the game's unless something is linked
to it (link a *Time* node to set a loop); it is not assigned to any shipped
material. Time-varying results do not show in the traced reflections (they
read the planes).

**Parameters**: a promoted number knob is a number parameter (a slider), a
promoted colour knob (a `float3` input marked `color`) a colour parameter
(type `color`, default the knob's colour; a swatch with Reset in Window >
Materials; written `[r, g, b]` in the catalog's `params`, held as
`<name>.r`, `.g`, `.b` in the resolved values). Other `float2`, `float3` and
`float4` knobs stay knobs.

**How it reaches the draws**: the seventh material plane holds a **graph
slot** per texel cell (0 none; else one per assigned instance of a graph
master, by id, up to 255). The game writes `material_graph.glsl` into
`pc/build/shaders/generated` (`$ROOD_SHADERS_GENERATED`): one GLSL function
per node definition in use, one per graph master (its promoted parameters
as arguments) and `material_graph(slot, ...)`, a switch calling each
instance's master. Values are not in the shader: every unlinked knob and
every instance's parameter is an entry of a **values table** the native
draws upload with each frame (`graph_values[]`, binding 7; entry 0 the game
time), read at offsets the include names. A value edit (dragging a knob,
an instance's slider or swatch) is a table write and shows on the next
frame; only a structural edit (a node, a link, a promotion, an assignment)
rewrites the include. An offset belongs to its knob or parameter for as long
as the table lives, so the shader still running the previous include during
a reload reads correct values. The
shipped `pc/shaders/vulkan/material_graph.glsl` is the stub the generated
file shadows (the shader compiler's includer reads the generated folder
first; the hot reload watches it): with no graph instance the file is
removed and nothing changes, the embedded shaders included.
`native_frag.glsl` calls it after reading the planes where the slot is not
0: the graph decides the surface inputs (the base colour replaces the texel's
before the game's own modulation; the glow is `max`ed into the colour and
marked in the shade as an emissive texel is: glow only, never a light);
the lighting is the one GGX model as ever. A structural graph edit: the
include is rewritten and swapped in within about a second; a graph that does not generate keeps the last good file
(and the editor outlines the node); a node file whose GLSL does not compile
is reported by RPC `shaders` like any shader error and the last good
pipeline stays. OpenGL draws no material response, so no graph either.

**Window > Material graph** (`pc/plugins/rood.material_graph`, Lua on
`rood.imgui`): the node palette left, the canvas in the middle, the
parameter panel right (Extensions.md "Developer tool windows"). The live
preview is the game view and Window > Material preview (the sphere shows
the resolved standard values). Not in this version: the packet and
character draws (the native room draws only), reflections of a graph's
per-pixel or time-varying result (the traced reflections use the planes),
`float2`/`float4` parameters.

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
palette key `clut`, `tpage`, `palette`, `boxes`, `cells`, `share`, `entry`,
`facing` [floor, wall, ceiling] cells, `object` cells;
`textured` and `untextured` shares, the view's `eye` and `forward`, `map`:
the cells' texture index, -1 nothing, -2 untextured; `cull` 0 draws both
faces), `texture {key, palette}` (a texture in view as base64 RGBA), and
`new {key, palette, preset, share, whole_palette}` (an entry for a texture
in view by key, as the panel's one click).

## Rolling out

The game-wide pass: survey every room, propose a preset per palette, review
the proposals, then apply the accepted ones to the catalog.

1. `python3 pc/tools/material_rollout.py survey [--until 21:30]` runs ONE
   headless game (as the room survey above) through the game's own room
   table (RPC `rooms`: every room read from the disc, with its area and
   name), warping into each (no enemies, no scripts; a room no door leads
   into is entered at the middle of its floor grid) and surveying it from
   the same four views. It is resumable: each room's survey lands in
   `pc/build/material_rollout/rooms/<zone>-<room>.json` and `state.json`
   notes it done or failed (a failed room is tried once more on a later
   run). `--until HH:MM` parks it (no room is started after then);
   `--zone`, `--rooms Z/R,...` and `--max` narrow it. A game that dies or
   stalls is killed by its PID and relaunched. Each texture's texels are
   kept once (`tex/`), each room's game view small (`views/`): game art, local.
2. `python3 pc/tools/material_rollout.py report` aggregates the rooms: every
   texture and every palette (what a palette entry covers), the rooms each
   is in, its coverage (the sum over rooms of its mean share of the views:
   rooms' worth of screen) and whether the catalog already has it. One
   proposal per palette (per texture for 15-bit textures, which have none)
   names a preset (or `none`: leave it as it is) with a reason and a
   confidence, from the texels' look (luminance, contrast, saturation, hue,
   dark seams, bright specks, grain direction), the survey's hints (how much
   of it faces up, sideways or down, how much object lists draw: props and
   characters) and the rooms' names (rain, water; cellars, catacombs;
   forest, garden). Organic looks come first: a green cut-out is `foliage`,
   green floors `grass`, green walls `moss` (jointed green blocks only low:
   mossy stone?), and in rooms named for a forest a streaked wall `bark`
   and an earthy floor `earth`; near black, cut-outs that are not green and
   textures mostly drawn by object lists are `none`; stone guesses on weak
   cues (low contrast, no seams; mixed facing; a forest) are low
   confidence. The thresholds and the room-name words are tunables in
   `pc/tools/material_rollout_rules.json` (`--rules`). It writes
   `proposals.json` and `review.html`, sorted by coverage, and prints the
   preset distribution.
3. Review in the game (below: Window > Material review), or open
   `pc/build/material_rollout/review.html` in a browser (a local file:
   no server, nothing uploaded): each proposal with its thumbnails, the
   rooms and their views, the hints; Accept, Reject or change the preset
   (and for a 15-bit texture the scope), then **Export review JSON**. The
   decisions are kept in the browser between visits.
4. `python3 pc/tools/material_rollout_apply.py material_review.json
   [--dry-run]` adds the accepted decisions to
   `pc/assets/materials/catalog.json` (`--catalog`, `--out`): a palette
   becomes ONE palette entry scoped to the rooms it was surveyed in (`scope:
   {"rooms": [...]}`, or `{"room"}` for one; palette entries stay scoped: a
   palette can be reused for other art elsewhere); a 15-bit texture its key
   and size, scoped to its rooms or everywhere.
   Fresh ids after the catalog's highest, `review: "new"`, a `where` naming
   the room and the proposal; entries the catalog already has are skipped,
   and the file keeps its layout.

### Reviewing in the game

The proposals are judged where they are drawn: **Window > Material review**
(or **Material review...** in Details > Material). It loads
`pc/build/material_rollout/proposals.json` (the path field and **Load** take
another; `$ROOD_MATERIAL_REVIEW` sets the default) and lists the proposals
by coverage, filtered by decision (undecided, accepted, rejected), preset,
confidence and zone. The window's first line is the routine: 1. pick a
proposal, 2. **Focus** to see its surfaces, 3. compare before / after,
4. tune the preset, 5. **Accept** or **Reject**. Under the proposal's
description three lines say where the review stands: the room (one of the
proposal's rooms, with its share, or not, and where Focus would go), what
the surfaces show (PROPOSED preset or BEFORE, without a material) and the
decision (undecided, accepted with its entry, rejected). For the selected
proposal:

- **Focus** (F) points the free camera at its surfaces: the room's
  triangles drawn with the palette (or the 15-bit texture) are joined into
  patches (vertices closer than *Join within*), and the largest patch by
  area is framed (its radius x *Margin* fills the view's height, between
  *Nearest* and *Farthest*), seen from between the surfaces' normal and the
  player's side (*View from*: 0 straight on, 1 from the player). When the
  player is in none of its rooms, Focus goes to its top room first and
  frames it on arrival. The editor's **Cut to focus** aims at the framed
  surfaces while the review is open (*Cut to focus aims at the surfaces*).
  A paused game is stepped two vsyncs so the picture follows the camera.
- **Highlight** (H) pulses the surfaces in the highlight colour (flat by
  default; *Flat colour* off: the texel's own colour times it), from
  *Pulse low point* to *Strength* at *Pulses a second*. It is not a glow:
  it shows whatever Glowing surfaces is set to.
- **Isolate** (O) dims and greys everything else (*Dim the rest*, *Grey the
  rest*): only the proposal's surfaces keep their colour, with the
  previewed material on them. It works with or without the highlight.
- its rooms by share, each with **Go** (the room is loaded as the survey
  saw it: without enemies or scripts unless ticked; a room no door leads
  into is entered at the middle of its floor grid) and **Next room** (G);
- the preview: its preset (or the one chosen in the dropdown, fine-tuned
  with Details > Material's own controls under **Fine-tune**) is live at
  once on every texture of the palette (or the 15-bit texture) in its
  rooms, as a temporary entry that is never saved; **Proposed material**
  off (B) shows the surfaces without a material (before / after);
- **Accept** (Ctrl+Enter) writes ONE entry into the user material layer
  (`materials.json`, where Details > Material saves): id `R<palette>`
  (`R<key>-<w>x<h>` for a texture), the palette (or the texture's key and
  size), scoped to all the proposal's rooms (`scope: {"rooms": [...]}`),
  the preset and the fine-tuning, `review: "accepted"`; accepting `none`
  writes nothing. **Reject** (Ctrl+Backspace) is recorded (and removes an
  entry an earlier accept wrote); **Clear decision** forgets it. **Undo**
  (Ctrl+Z, the last 64) puts the last decision back: the proposal's
  decision as it was, its user-layer entry restored or removed, and the
  selection back on it. **Open in Details > Material** selects an accepted
  entry there (its maps can be painted as any entry's). After a decision
  the next undecided proposal is selected (a setting).
- Keys, while the window has the keyboard (the line at the bottom says
  whether it has): N / P next / previous, U next undecided, F focus, B
  before / after, H highlight, O isolate, G next room, Ctrl+Enter accept,
  Ctrl+Backspace reject, Ctrl+Z undo; pad L1 / R1 previous / next, R2
  before / after, L2 highlight. None fires while the camera flies (the
  right mouse held: WASD, Q and E move it while the window keeps the
  keyboard), while a control is held or a field typed in, and decisions are
  never a bare letter. While the window has the keyboard its F, H and
  Ctrl+Z win over the editor's (frame the selected light, hide the
  selection, the editor's undo).

The look and behaviour (**Look and behaviour**: the highlight's colour,
flat, strength, rate and low point; Isolate's dimming and greying; Focus's
margin, join distance, view, nearest and farthest distance and cut; the
next undecided after a decision; Go on selection) are tunables, saved in
the state file and set by RPC `settings`. A paused game draws no frame of
its own, so while the review's highlight, Isolate or preview change, the
paused loop draws the newest frame again (`pc_material_review_redraw`).

The decisions, the selection and the settings are kept in
`review_state.json` beside `proposals.json` (game art's folder: local), so
a session resumes where it stopped. It is a review in
`material_rollout_apply.py`'s format, so the same decisions can also be
applied to a catalog copy.

RPC `material_review`: `status` (default: the path, counts, preview,
highlight, isolate and the marked texels, `showing` proposed or before,
`in_room`, `keys` (the window has the keyboard), `undo` (newest first),
`focus` (pending, ok, triangles, cluster, clusters, centre, eye, radius,
distance), the settings, the catalog's `presets` (name and values), the
`filters`, the room, the selected proposal (its alternatives and catalog
ids too) and its entry),
`open {open, focus}` (focus: the window takes the keyboard), `load
{path}`, `list {status, preset, confidence, zone, offset, limit}` (also
`counts`: undecided, accepted, rejected of all), `filters {status, preset,
confidence, zone}` (the window's own, which next / previous / undecided
follow), `select
{id | stable | index | next | previous | undecided}`, `preview {on,
highlight, isolate, pulse}` (pulse: the pulse held at 0..1 of its wave, -1
running), `preset {preset}`, `tune {roughness, specular, metal,
auto_height, bump}`, `go {room: [zone, room]}` (or the next room),
`focus`, `decide {decision: accept | reject | clear}`, `undo`, `settings
{highlight_rgb, highlight_strength, highlight_rate, highlight_floor,
highlight_flat, isolate_dim, isolate_grey, focus_margin, focus_join,
focus_view, focus_min, focus_max, focus_cut, enemies, scripts, advance,
auto_go, save}` (saved in the state file; `save: false` applies without
writing, while a slider is dragged). `material_review_smoke` checks it
headless (the highlight's pulse on screen, a paused game's picture, Isolate,
Focus here and from another room, Undo, the keys, the Lua window).

The window also exists in Lua: the shipped plugin `rood.material_review`
(`pc/plugins/rood.material_review/main.lua`, on by default; turn it off in
the Lua console's Plugins list for the C++ window). On, Window > Material review is the Lua
window (`rood.dev.window{replaces = "material_review"}`: the C++ window is
neither drawn nor listed and its open flag is the Lua window's), drawn with
`rood.imgui` on RPC `material_review` alone, with the same keys; off, the
C++ window is back. It draws the C++ window's picture pixel for pixel
(checked by `material_review_smoke`). Edit its `main.lua` while the game
runs: the window redraws from the saved file (pc/docs/Extensions.md,
"Developer tool windows").

The survey's hints come from RPC `material survey`: per texture, `facing`
(its cells whose triangles face up, sideways and down as seen from the eye)
and `object` (its cells drawn by object lists).

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
  Fresnel of the environment; and, below, the game's baked light, the
  indirect light (probes or path traced) and the sky's ambient. The
  geometry's normal keeps the shadows, the GI's lookup and denoising, AO
  and the traced reflections' rays.
- **The cavities** (a texel below its eight neighbours' mean) lose a share
  of the ambient light (baked, indirect, sky), `bump_occlusion`: physical
  (a pit sees less of its surround) and subtle.

### The relief under the baked and indirect light

In most rooms most of the light is the game's baked (vertex) light and the
indirect light, and neither has a direction, so bending only the dynamic
lights left the relief faint. `scaled.frag` (`relief_*`) now shades both
with it, Vulkan only:

- **The model.** Over a relief whose tilt is `t` (the bent normal's part
  along the surface, unnormalised: the height's slope times
  `bump_strength`), light from direction `l` delivers `dot(n + t, l)` per
  unit of flat area (each facet's cosine times its area). Against the flat
  value that is `1 + dot(t, tau)`, `tau` the light's tilt from the normal
  along the surface (the tangent of its angle). So a flat surface keeps
  exactly its light, and over slopes that cancel (any height map: the
  slopes of a closed bump sum to zero) the mean is unchanged: the light
  only moves from the faces turned away to the faces turned toward it. With
  a share `k` of the light from that direction, `1 + k dot(t, tau)`
  (clamped to 0..2); `k` is the term's strength.
- **The baked light's direction.** The vertex light is the light arriving
  at the surface; it carries no direction, and nothing else in the frame
  knows where it came from (its lights have no geometry, the probes do not
  see them, and the rooms where it matters most have no dynamic light and
  so no probe field at all). But an irradiance field rises toward its light
  along a surface: a point light `h` above it, `x` along, gives
  `grad E / E = 3x / (h^2 + x^2)` where the light's tilt is `x / h`. So
  `tau = relief_reach x grad E / E`, `relief_reach` about the light's
  height over the surface. The gradient is taken in world space on the
  surface from the vertex light at four screen neighbours (central
  differences over a footprint of `relief_reach / 8`, 8..64 units,
  one-sided at an edge, none across a crease or a depth step), so it is
  smooth across the game's triangles. Where the light hardly changes
  (broad, evenly lit walls), `relief_overhead` adds light from above (the
  world's up along the surface; nothing on floors and ceilings): the
  prior we read bumps by, and what the room's lanterns and torches above
  head height mostly give. `tau` is clamped to 2 (63 degrees).
- **The indirect light.** With the probe field readable this frame
  (`gi_method probes`, `gi_on` 512) it has its own direction: DDGI stores
  irradiance by normal, so the term is the field's own response,
  `(E(n + t) - E(n - t)) / 2E(n)` at the unnormalised bent normals (for
  irradiance linear in the normal exactly `dot(t, grad)`, the same flux
  change; the probes' weights are the geometry normal's). The path
  tracer's result is irradiance per GI pixel without a direction: its
  gradient along the surface, as for the baked light (the GI result looked
  up at the same four neighbours), plus the light from above. The sky's
  ambient (`env_ambient`, rooms where nothing traces the sky) by the same
  difference at the bent normals.
- **Strength 0** (either term) and pixels without a relief take exactly
  the old expressions: the frame is byte-identical to the renderer before
  (checked against the old binary in the Keep and Snowfly Forest, preset
  Low, and by `material_relief_smoke` against the terms on with nothing to
  give a direction).
- **Measured** (the Keep 29/3 at the game camera, no dynamic light, the
  defaults): the walls' high-pass luminance deviation rises 12-15 %, their
  mean luminance changes by under 1 %; over every material pixel of the
  view -0.1..+0.1 %. Snowfly Forest 40/13 the same within 0.15 %. Brick
  tops catch the lantern's light, their undersides darken; the stone
  floor shows its slabs toward the lantern. The Catacombs (13/3) change
  little: few of its textures have a relief entry yet.

Debug view 12 (RPC `graphics view_mode`): the baked factor (r) and the
indirect factor (g), 0.5 = 1, and the baked light's tilt (b, 1 = 2).

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
| `relief_baked` | the relief under the game's baked light: the directional share of that light, 0..4; 0: as before (byte-identical) | 2 |
| `relief_indirect` | the relief under the indirect light (probes, path traced) and the sky's ambient, 0..4; 0: as before | 2 |
| `relief_reach` | world units that turn the light's relative change along the surface into its tilt (about the light's height over the surface); also sets the gradient's footprint (reach / 8, 8..64); 0: no tilt from it | 96 |
| `relief_overhead` | the tilt toward the world's up added on walls (light from above), 0..1 | 0.6 |

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

## Baked maps

The textures carry no height, roughness or metal of their own, so
`pc/tools/material_bake.py` derives a full map set for every texture a
material entry applies to (the shipped catalog and the user's: by key or
by palette; the accepted rollout proposals are palette entries, about
15,000 textures) from its texels, offline, by classical image analysis
(no learned model, nothing downloaded):

```
python3 pc/tools/material_bake.py --all [--dry-run] [--preview pc/build/bake-preview]
python3 pc/tools/material_bake.py --keys 9a87cf9633,d83ebe3791 [--method classical]
```

- **Where**: into the baked layer, `pc/build/materials/<key>/<channel>.png`
  (`--out`, `$ROOD_MATERIALS_BAKED`), at exactly the texture's box size,
  8-bit grayscale. The maps are derived from the game's art: they stay on
  this machine (the tool refuses `pc/assets`; `--preview` contact sheets
  only go under `pc/build`). Only files whose content changed are
  rewritten, and the game reloads those live.
- **The textures**: the exported references
  (`pc/exports/materials/<key>/reference.png`) and the rollout's texture
  thumbnails (`--textures`, default `pc/build/material_rollout/tex`, as RPC
  `material texture` draws them: `<key>-<w>x<h>-<clut>.png`). A key drawn
  with several palettes takes the first an entry matches. `--user-catalog`
  (default the user lighting directory's `materials.json`) is only read.
- **height**: the luminance, smoothed edge-preserving (a self-guided
  filter, `smooth_radius`, `smooth_eps`: the palette's quantization and
  dithering go, edges stay), split into bands: what is broader than
  `low_sigma` texels is the shading painted into the texture and is
  dropped (big gradients do not become slopes); the brick-sized band
  (`w_detail`) and the finer one (`w_mid`, above `mid_sigma`) are kept.
  Dark narrow features, a black top-hat over a disc of `groove_radius`
  (mortar, cracks, gaps; above `groove_threshold`, softened by
  `groove_soften`) carve down by `w_groove`. Centred on the mean and
  scaled by its `height_percentile` so the result spans 128 ±
  `height_amplitude` x 127: a mean of about 128.
- **roughness**: the entry's roughness (its own, else its preset's),
  rougher where the texture is busy (local contrast, `roughness_detail`)
  and in the grooves (`roughness_cavity`), smoother on raised faces
  (`roughness_height`), clamped to `roughness_range`.
- **metal**: only presets with `"metal": true` (worn iron, polished
  metal) get a `metal.png`: the entry's metal where the texels are grey
  (saturation under `metal_saturation`) and bright enough
  (`metal_brightness`), none where they are rust-coloured
  (`metal_rust_hue`) and less in the grooves (`metal_cavity`). Every other
  preset has no metal map (its entry's metal applies, 0): plain stone is
  never metal.
- **Tunables**: `pc/tools/material_bake.json`, `default` and per-preset
  overrides (stone, wood, bark, metal, cloth... each its own weights and
  ranges). A baked map is a map like any other: at run time a baked
  `height.png` replaces the colour estimate (`auto_height`), a baked
  `roughness.png` the entry's roughness, both times `bump_strength` and the
  entry's `bump` as painted maps are.
- **The switch**: `materials_baked` (Graphics > Lighting quality,
  Materials: *Baked maps*; RPC `graphics materials_baked=0`, listed by
  `material list` with `baked_dir`). Off: the baked layer is not looked at,
  the picture exactly the one without it (the estimate where an entry asks
  for it). A painted or shipped map always wins over a baked one; RPC
  `emissive materials {key}` lists each channel's layer (`user`, `baked`).
- **Methods**: `--method classical` is the only one. Another (a learned
  height estimator, once its weights are approved) is one more function in
  `METHODS`: it gets the texture (RGBA) and the preset's parameters and
  returns the channel arrays.
- **Tests**: `material_bake` (disc-free: a made-up block wall has its
  mortar below the blocks, no slope from its painted shading, no speckle;
  metal only on metal presets; targets, dry run, tunables, refusals) and
  `material_baked_smoke` (the Keep: a baked map picked up live, the user's
  winning, off byte-identical to no baked layer).

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
or metal), `map {key, channel}`, `undo`, `preview {auto_height}`, `reload`,
`palettes` (the palettes drawn now, by CLUT slot: `depth`, `clut`, `key` of
the colours now, `identity`, `variants` this room, `uses`, `entry`: Animated
palettes).
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
- The relief under the glows' light (`gi_extra`'s gba, traced) and the
  baked light's direction from the room's fixtures (the lighting pack's
  fixture catalog knows where the lanterns are) where they are known.
