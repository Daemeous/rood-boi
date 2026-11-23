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
| `baked_texture_relief` | how much of a texture's own relief estimate (mortar, grain: `auto_height` entries) goes on top of a *baked* height map, which holds only what the geometry shows; 0: the bake alone | 1 |
| `painted_texture_relief` | the same for a *painted* height map (the user's layer, a live `map_set`: the Aseprite Height layer, which starts as the baked one); 0: the painting alone is the relief; shipped maps are never added to | 1 |
| `key_light` | the per-room key light (below, "The key light"); 0: the picture without it (the default: the owner prefers the bake alone) | 0 |
| `key_light_strength` | a multiplier on every room's key light, 0..2 | 1 |

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

## The key light (Gitea #20)

The baked maps only shade light that has a direction, and most of a room's light does not: the PS1's vertex colours ignore
normals, and some rooms (City Walls South) have no dynamic light at all. The key light is a soft, directional light per room
whose main job is to **show the relief**: by default it only reshapes the room's own light by the normal map and adds nothing
to its brightness, so the original art's lighting and mood stay. Vulkan only (OpenGL draws no relief or material).

- **Relief share** (`key_relief`, 0..1, default 1). With share `k` the light reshapes the baked, indirect and sky light by the
  relief: `1 + k G I dot(n_s - n, l)` per unit of flat area (`G` the gain, `I` the intensity, `l` the light's direction,
  `n_s` the bent normal, `n` the geometry's), through a smooth odd limit (0.1..1.9) so it never goes black or blows out. A flat
  surface keeps exactly its light, and over slopes that cancel the mean is unchanged (the faces turned toward the light gain what
  the faces turned away lose). The other `1 - k` is a plain light (`I` times the cosine of `n_s`, the baked shadow
  (`baked_shadow_*`) and the ambient occlusion dimming it): `k` 1 is the relief alone, 0 a real light that also brightens.
  Roughness and metal count as for every light: its highlight is the same GGX lobe
  (`key_specular` times the lobe of the bent normal minus `k` times that of the geometry's: a relief-only key adds a
  highlight only where the relief makes one), so rough stone shows a faint one and glossy or metal surfaces a sharp one.
- **Where it applies.** Pixels with a material (so, in a room with baked or estimated maps, the relief's pixels): a room, model
  or prop without maps gets nothing from the relief share. The frame is exactly the old one when the key light is off, when no
  material is in view, when the specular switch is off (`specular 0`), and in the Original lighting preset (dynamic lights off).
  The frame is deterministic (a function of the room settings, the view and the frame; nothing time-based).
- **Direction.** `key_azimuth` and `key_elevation` (degrees; the sun's convention, azimuth 0 from +z, 90 from +x). With
  `key_view` 1 (the default) the azimuth is added to the direction away from where the camera looks, so that a room whose
  camera is turned the other way shows the same relief (the game's cameras are fixed per room, but they differ between rooms);
  0: the world's direction.
- **Where the data lives.** The light's values are room settings (`pc/src/room_settings.cpp`, the Lighting tab > Key light,
  RPC `room_settings`, `set key_<name> value=` in a room's lighting file like `haze_density`): `key_intensity` (0..4, 1),
  `key_relief` (1), `key_gain` (0..16, 3: how many times the maps' slopes the relief share exaggerates: the baked maps are
  shallow, 0.1 and under in slope), `key_azimuth` (35), `key_elevation` (45), `key_view` (1), `key_red`, `key_green`,
  `key_blue` (colour, 1), `key_specular` (1). Their project defaults are `lighting/default/defaults.txt`, one set for
  rooms under a roof (`set key_...`) and one for rooms open to the sky (`set exterior.key_...`: those with an `env sun` or
  `env sky`, which the generator writes for the open rooms; the game's data has no other exterior flag, so there is no
  per-zone table). Exterior default: a cool light a little higher (50 degrees); interior: neutral, 45 degrees. A pack, the
  user's `pc/user/lighting/defaults.txt` ("Use as the project default" in the panel) and a room's own lines override them.
  `pc/tools/lighting_lint.py` checks the file. The global switch `key_light` and multiplier `key_light_strength` (0..2) are
  lighting-quality settings (settings file, Graphics > Lighting quality > Materials, RPC `graphics`); `key_light 0` is the
  picture without it, byte for byte.
- **Not touched.** Lanterns, candles, glows and every emissive's intensity; the dynamic lights' own relief; shadows,
  GI and the traced reflections' rays.

## Materials by object

An entry may be keyed by an object as well as by texture or palette (Gitea #20 step 2; plan in
`plans/2026-10-27-materials-and-inspection-design.md`): `"object": {"groups": [..], "polygons": [..], "models": ["4b8870d7"]}`
beside `match` and `scope`. A group is the room geometry block a native room mesh draws (its byte offset in the room
data), a polygon a room polygon (byte offset), a model the content hash of a character, weapon or prop (every copy).
Groups and polygons need a room scope. With `match` the entry is for that texture of the object; without it, for every
texture of the object. An object entry is never found by the texture lookup (`find_match`): only
`materials::find_object_match` returns it, for a polygon of its object, and it wins over the texture and palette
entries there. The file is `format: 2` when it holds one. The picker (`material pick`) reports the pick's `object`
(`groups`, `polygon_offset`, `text`) and `object_entry` (the object entry that applies to it); RPC `material_instance`
`assign {object}` and `assign_selection {object: group|polygon|model|true, object_wide}` make one; `material` `new`
and `set` take the same. The Materials panel assigns "...this object's" texture or "...all of its textures". The
draw: a polygon of the object carries a **variant** (a 16-bit vertex attribute, `render_mesh_variants`, set by
`emissive.cpp` `rebuild_variants` where the catalog, the room, the meshes or the uses' keys change) per (entry, texture
use); the variant's seven planes (made as a use's, with the entry's values) sit box-relative in a second image,
`vk_hw.variants` (R8_UINT, 4096 wide, grown on demand up to the device's limit): rows 0-127 hold an 8-byte record per
variant (box origin and size, cells' place), the planes below are shelf-packed. `variant_texel` (`native_frag.glsl`)
reads it for polygons with a variant, else `material_texel` as before (a texel outside the variant's box too). Past the
limit the rest use the texture's own entry (log line; RPC `material list` `variants {count, rows, overflow}`). A model
drawn with another page and CLUT over its own takes its first instance's. The packet path and OpenGL have no variants
(as for scopes). An object entry with no texture hashes every size of texture while it exists (as a palette entry
does).

## Blender round trip (one room)

`pc/tools/room_blender.py` takes a room out to Blender and brings maps back into the baked layer (Gitea #20; Blender 5.2
LTS, `/usr/bin/blender`, driven headless with `blender -b -P pc/tools/blender/bake_room.py`, Cycles on the CPU at a modest
sample count, 4 threads, nice 19):

1. `export`: the room the game is in (or `--zone Z --room R`) to glTF under `pc/build/room_blender/<room>/gltf`: RPC
   `scene_dump` (positions, vertex colours, each polygon's texels, page, CLUT, object group, flags), RPC `emissive uses`
   (every texture in view: key, page, CLUT, UV box) and RPC `material texture` (the texels as drawn). One primitive and one
   material per texture (the key), UVs over the texture's own box (texel `u0 + k` at `k / w`), 1 unit = 512 game units, y up,
   each triangle turned to face the room's middle (the capture's winding is mixed). `export.json` keeps what comes back:
   key, size, page, CLUT, box, each triangle's group and flags. Polygons the game did not draw in the view are not in `uses`
   and so not exported.
2. `bake`: Blender bakes each texture texel for texel from the room's geometry: `cover` (which texels a triangle covers),
   `wear` (edge wear: a bevel node against the face's own normal) and `cavity` (1 - ambient occlusion). The tunables
   (bevel radius, gain, occlusion distance, samples) are data: `pc/tools/room_blender.json`.
3. `import`: `height.png`, `roughness.png` (the channels the game reads) and `normal.png` (for other tools) per texture,
   exactly the texture's box in size, 8-bit, into `<out>/materials` (use it as `$ROOD_MATERIALS_BAKED`): wear carves the
   height and polishes, cavity lowers and roughens; a texel no triangle covers keeps the blank template. The maps' formulas
   are `room_blender.json`'s `maps`.
4. `shot`: the room three times (no entry; the preset on every baked texture with the game's own derived relief; the same
   with the baked maps) into `<out>/shots`: `plain`, `before`, `after`, `before_after.png`.

A map applies where a material entry does (`shot` writes one `"rough stone"` entry per texture into a scratch user catalog).
Everything under `pc/build/room_blender` is game art: never committed. Textures shared by several room groups (PS1
textures are, by different models and walls) are baked once per group: `export` finds each triangle's group (the scene dump
carries its polygon's byte offset, RPC `material groups` the groups' blocks), splits a texture used by more than one group
into `<key>@g<offset>` primitives and materials, and `import` writes their maps under
`<out>/materials/<key>/@g<offset>/` (`materials::channel(key, name, object)`: the object's folder over the texture's own,
in each layer), so the texels they share stop taking whichever baked last. `shot` writes an object entry for each such
(texture, group) (`object {groups}`, scoped to the room), which is what gives the group its variant (Materials by object);
a texture on a single group keeps one entry and its maps in `<key>/` as before.

### A model through the same bake (Gitea #20)

`pc/tools/model_blender.py` does the room's steps for a character, weapon or prop (`all --model 4b8870d7 [--zone Z --room R]
[--yaw DEG]`, out under `pc/build/model_blender/<model>`): `export` is `paint_roundtrip.py export model` (with `--zone` and `--room`
the room is warped to with its enemies; the hashes in a view come from `material pick`), `bake` the same Blender script on
`paint.gltf` (one atlas per page and palette, UVs over the 256 x 256 page), `import` cuts each of the model's textures (the
boxes of the atlas: the game's own, `slices` of `seed.json`) from the bakes and writes height, roughness and normal into
`<out>/materials/<texture>/@m<model>/`, and `shot` puts the model in the game twice in the same paused frame (an entry per texture,
`object {models}`, preset from `room_blender.json` `model.preset`; the maps then copied into the user layer the game watches):
`before_after.png` and one pair per view of `model.shot.views` (the shading normals with the relief, the material target). The
`model` section of `room_blender.json` lays its `bake` (a model is a fraction of a room: a smaller bevel and occlusion distance) and
`maps` over the room's values, and frames the shot.

What a model export is: the model as the newest frame draws it (the game's pose: an idle stance, a mid-attack frame; there is no bind
pose to ask the renderer for, and the skeleton is not in the dump), moved to the origin with its feet on the ground, the faces turned outward (a model is drawn without culling and its winding is
mixed: `room_blender.face_outward` makes the winding alike across every edge two triangles share, then turns each connected piece
so it faces away from its middle). `pc/tools/blender/check_gltf.py` opens a package in Blender and reports meshes, materials,
UVs, welded joints, winding and size (`blender -b -P pc/tools/blender/check_gltf.py -- paint.gltf [preview.png]`; with a preview,
renders from each side with back faces culled). Two limits: a texel two triangles share (a left and a right arm drawn from the
same texels, a mirrored face) holds the one baked last; and the maps are keyed by the model's hash, which also names the VRAM
page it was loaded into: the same character loaded at another page is another hash with the same texture keys, so a model needs
its own export and import per hash (two copies of one girl seen in the Undercity, `dfcf8792` at page 18 and `266d0792` at page 10, with the same 194 texture keys).

### Metal and roughness from the colours (Gitea #20 step 3): an optional seed, off by default

`room_blender.json` `metal.enabled` is false: maps are painted in a painter (next section). Set it true to seed `import` with this guess.

`import` also writes `metal.png` (and the metal's own roughness into `roughness.png`) for a texture whose colours look like metal:
`room_blender.json` `metal` lists the materials as colour windows (hue, saturation, luminance with a soft edge) and, for
`neutral` ones, the greys of a texture that also has colour (iron bands over brown planks, studs on a door: a texture grey all
over is stone). A texture takes the map only when metal texels are `min_share` to `max_share` of it, pieces under `min_area`
texels or `despeckle` neighbours go, and the geometry shapes it: worn edges polish (`wear_boost`, `rough_wear`), crevices dirty
(`cavity_drop`, `rough_cavity`). Colour is a weak signal (the first room: 8 of 484 baked textures, the chest's bands and a few
barrels): the windows are data to tune, and `shot --light-at X,Y` puts a light beside a spot on each side (`highlights.png`: before
above, after below; `*-spec-*.png`: the specular lobe alone) to look at the result.

In the renderer a metal texel's specular fraction is full (`emissive.cpp` make_stamp: the cell's specular is the larger of the entry's
and its metal): F0 is albedo x specular, so an entry's dielectric 0.3 had made baked iron a dull third of itself. Roughness and metal
planes feed the raster lighting (GGX lobe, baked light reflected as the environment) and the traced reflections alike; `material
pick` now also says where the pick landed (`world`).

## Painting the maps in a painter

Metal, roughness, normals and occlusion are painted in a texture-painting program on the whole mesh, not in the dev tools. The
round trip is glTF 2.0 with the PBR metallic-roughness material and PNG textures, which both open-source painters take:

- **TextureCraft** (github.com/storytold/texturecraft, Rust): reads glTF/GLB, FBX, COLLADA, USD, Alembic, OBJ, PLY, STL; writes PNG,
  JPEG, TGA, TIFF, BMP, WEBP, EXR and HDR at 8, 16 or 32 bits through an output template with channel packing, and textured
  OBJ/GLB/USD. Its README does not say how the PBR maps are named or packed: set the template to the names below.
- **SubCraft** (github.com/Aalzard/subcraft, Rust): imports OBJ, glTF/GLB, FBX, USD, Alembic, PLY, STL and "a whole texture set matched
  by file name"; exports texture sets through templates (PBR metal/rough, Unreal ORM, Unity, Godot 4, glTF) and the scene as OBJ,
  glTF, GLB or USDA.
- Blender's texture paint also works.

glTF covers both: they read the mesh and material from it, and what they write back is PNG files per texture set, which is what
`import` reads. The maps are 8-bit.

**The short way**

```
python3 pc/tools/paint_roundtrip.py paint model --model 4b8870d7 --out pc/user/paint/ashley     # export, then open it in SubCraft
python3 pc/tools/paint_roundtrip.py import pc/user/paint/ashley                                  # after you export the maps there
```

`paint` is `export` plus opening the package in SubCraft (as `paint.subcraft`, see below) when it is on the PATH (Blender otherwise; `--painter subcraft|blender`
forces one). It takes everything `export` takes (`room`, `--zone/--room`, `--model`, `--model-at`, `--bake`, `--out`).

**Steps in SubCraft** (0.4.5; menu names are from its own docs, "check" marks what to look at on your screen)

1. Run the `paint` line above. SubCraft opens `paint.subcraft`, a project the export writes next to `paint.gltf` (`subcraft_project.py`):
   no New Project window, the model with one texture set per material (named as the material, 256 x 256 for a character page, the
   smallest power of two that holds a room texture), the **Albedo filter** of each set on *Nearest* (so the game's colour is
   pixelated, not blurred; check: Texture Set Settings), and a Fill layer **Game texture (seed)** at the bottom of each set with the
   game's base colour, roughness, metal, normal (the geometry bake's relief) and occlusion. Paint on layers above it. What you
   export is then the game's maps with your paint on top, so unpainted areas import unchanged and `--keep-seed` is not needed.
   The project is written for SubCraft 0.4.5 (project schema 18); with any other version `paint` says so and opens the plain
   `paint.gltf` instead, and then you set the New Project window: **Template** *PBR Metallic Roughness*, **Resolution** 256 x 256
   (2048 also imports, averaged down), **Texture sets** *By material*, **Normal maps** *OpenGL (Y+)*, no high-poly; the game's
   colour is then not in it: drag `tex/<material>_BaseColor.png` onto the layer stack (Assets/Shelf imports, UV projection,
   Albedo filter Nearest in Texture Set Settings; check) so you do not paint blind.
2. Paint on the *Metallic* and *Roughness* channels (the channel list in the Layers panel; check), on a Paint layer above the seed
   layer (the Fill layer's own *Filtering* box reads Bilinear | HQ; it has no effect at the texture's own size).
3. **Export** (Ctrl+Shift+E; the *Export Textures* window). Template *PBR Metallic Roughness* (or glTF 2.0 / Godot 4 ORM). Set the
   export folder to the package folder (`pc/user/paint/ashley`; check that the window's folder is that one) and tick only the maps
   you painted (Roughness, Metallic, and Normal / AO if you painted those): unticked maps are not exported and so cannot disturb the
   game's own. SubCraft writes `paint_<material>_<Map>.png` (`<model name>_<texture set>_<Map>`); the import reads that, a prefix of
   any other name, or `<material>_<Map>.png`. Of two files for one map the newer wins, so the export beats the package's own images.
4. `python3 pc/tools/paint_roundtrip.py import pc/user/paint/ashley`. Add `--keep-seed` only with the plain-glTF fallback (no seed layer), when you painted only part of a map: SubCraft
   fills the rest with one flat value, and `--keep-seed` turns that flat fill back into the game's own map there (a painted flat
   area that is most of the image would be mistaken for it: paint something non-flat or leave the flag off).
5. With the game running the maps show without a reload (it watches those files). Paint, export, import, look.

**Steps in Blender** (texture paint)

1. `python3 pc/tools/paint_roundtrip.py paint model --model 4b8870d7 --out pc/user/paint/ashley --painter blender` (the glTF is
   imported). Or File > Import > glTF 2.0.
2. Switch to the *Texture Paint* workspace and, in the tool settings, set the paint *Mode* to *Image* and pick the image
   `<material>_MetallicRoughness` (check: Blender lists the material's images there). Blender paints colours, not single channels:
   the brush colour is the map. Keep red at 1 (the glTF's occlusion is a separate image, so red is not read), green is the roughness
   and blue the metal, for example (1, 0.3, 1) is smooth metal and (1, 0.9, 0) rough non-metal. Set the brush *Blend* to *Mix*, strength 1,
   and turn colour management to *Standard / Non-Color* for that image (check: Image Editor > Image > Color Space = Non-Color).
3. Image > Save the painted images (or Image > Save All Images) over the files in `tex/`. Blender writes them in place, which is what
   `import` reads (they are newer than the package's).
4. `import`, as above.

**Settings so `import` just works**: resolution 256 (or any size), by material, OpenGL normals, PBR Metallic Roughness template,
export into the package folder. The game reads a map only at its texture's size (texel for texel), so a higher-resolution paint is
always averaged down to it (normals renormalised) and a smaller one repeated up; the game itself does not take bigger maps.

Everything below is the package's workings.

**Steps (any painter)**

1. Start the game and go to the room (or have the model on screen). From the repository root:
   `python3 pc/tools/paint_roundtrip.py export room --out pc/user/paint/my-room` (a model: `export model --model 4b8870d7`, the hash
   in `material pick`'s object text without the m, or `--model-at X,Y` for a pixel of the view). To start the normal maps from the
   geometry bake the game shows, add `--bake <room_blender out>/materials` (or set `ROOD_MATERIALS_BAKED` as you run the game).
2. Open `paint.gltf` in the painter. A material per object and texture: `<texture>_g<group>` for a room (a texture used by several
   room groups has an image set for each, so each object owns its texels), one `m<model>_p<page>_c<palette>` atlas per page of a model.
   Its base colour is the game's texture texel for texel (set the painter's sampling to nearest); `tex/` holds BaseColor,
   MetallicRoughness (G roughness, B metal), Normal and Occlusion images, seeded from the maps the game has (or neutral).
3. Paint metallicRoughness, normal and occlusion. Do not edit the base colour (it is not imported).
4. Save the glTF where it was (its images next to it), or export the texture set as PNGs into the folder named `<material>_<Map>.png`
   (BaseColor, Roughness, Metallic, Normal, AO, or ORM / OcclusionRoughnessMetallic / MetallicRoughness packed; an ORM's R is the occlusion).
5. `python3 pc/tools/paint_roundtrip.py import pc/user/paint/my-room`. Each map that differs from its seed becomes the user layer's map
   of that object (`<user lighting>/materials/<texture>/@<object>/metal.png`, `roughness.png`, `normal.png` with `normal_x.png` and
   `normal_y.png`, `occlusion.png`) and a user catalog entry (`PT-<texture>-<object>`: the values the game drew that texture with until then, its matched entry resolved, or the
   no-entry surface (roughness 1, no specular, no metal) when it had none, so a first import changes only what was painted; change
   it in Details > Material) makes the object use them; the user layer wins over the baked one.
6. With the game running, the maps and the entry show without a reload (the game watches those files). Paint, import, look.

An export imported without painting changes nothing: only maps that differ from the seed are written, and the seed copies in `seed/`
are never edited. The geometry-based normals (`normal.png` of the bake) are the seed of the normal map, so the relief the owner likes
stays where nothing is painted over it (the game reads that relief from the bake's `height.png`, which an import never writes); a
painted normal replaces the relief of its object (the game reads `normal_x.png` and `normal_y.png` from the user layer only, and
uses them in place of the height, with the strength recorded in `normal.json` beside them: the bake's `maps.normal_strength`, so the two cannot drift). The occlusion map is kept for other tools: the game does not read it yet. Everything exported is
game art: it stays in `pc/user` or `pc/build`, never in git.

### Painting height, metal and roughness per pixel in Aseprite (live)

For a true one-pixel brush with the game's colour under it, paint in Aseprite (1.3 or later; tested with 1.3.18.6-dev). The game shows
each stroke as you paint it.

1. **Install Aseprite** (`yay -S aseprite` builds it from source), then the extension: `python3 pc/tools/aseprite/build_extension.py`
   writes `pc/build/aseprite/rood-live.aseprite-extension`; double-click it, or Aseprite > Edit > Preferences > Extensions > Add
   Extension. (Once; restart Aseprite.)
2. **Export** the model or room with the game running: `python3 pc/tools/paint_roundtrip.py paint model --model 4b8870d7 --out
   pc/user/paint/ashley --painter aseprite` (or `export room ... --painter aseprite`). It writes `pc/user/paint/ashley/aseprite/<material>.aseprite`
   at each texture's own size (no scaling), and opens Aseprite on them when it is on the PATH and there are up to six.
3. **Start the bridge**: `python3 pc/tools/paint_roundtrip.py live pc/user/paint/ashley`. It listens on `ws://127.0.0.1:47480` (`--port`) and
   also watches the .aseprite files, so a save (Ctrl+S) imports the maps into the user layer (what survives a restart of the game).
   `watch` alone does only the saves. Neither needs the game to run: without it the files are still written and read.
4. **Paint.** Open a `.aseprite` file. The first time, Aseprite asks to let the script open a network connection (and read files): tick
   *Give full trust to this script*. A small "Rood live" window shows the state (connected, game running or not). Layers, bottom to top:
   - *Game colour (reference)*: the texture, locked; look at it, do not paint it (an edit is not imported).
   - *Height*: grayscale relief, hidden; 128 is flat, brighter is raised, darker is sunk (the layer order is the channel table's,
     below). Seeded from the game's `height.png` for that texture where there is one (the baked or a painted one); where there is none it
     is flat 128 and the game's own relief estimate (mortar, grain of `auto_height` entries) stands until you paint: **a layer left as
     exported writes and pushes nothing**. Once you paint, the map is the game's whole height for that object, with the texture's own
     estimate still added on top by `painted_texture_relief` (below), exactly as a baked map gets `baked_texture_relief`. The relief
     needs the entry's `bump` above 0: the import's catalog entry turns a 0 into the standard 1 when height is painted. Erasing the
     layer where the game had no height map takes the map out of the game again (`material map_clear`), so its estimate shows.
   - *Metal*: grayscale, white = metal, shown at 60% so the colour shows through (the opacity is display only: the game reads the pixels).
   - *Roughness*: grayscale (white = rough), hidden. Click its eye to show it and hide Metal when you paint roughness (one channel at a
     time reads best; Height is hidden the same way). The pencil, 1 px, nearest everywhere (View > Pixel grid on). Select the layer, pick white or a gray, paint.
   - *Material* (top, 70%): paint a **kind of material** in one stroke, see below. Empty until you paint.
5. **Ctrl+S** to keep it. **Erase** with the eraser (or delete the cel): a transparent pixel means *keep the game's value*, so erasing
   returns that texel to what the game had when you exported; an opaque gray sets it. Do not resize the canvas or rename the layers
   (extra layers of your own are ignored).

**Painting a kind of material (the Material layer).** Instead of one layer and then the other, paint with a *preset swatch*: each
swatch colour of the file's palette is one material (Polished steel, Worn steel, Dark iron, Rusted iron, Gold, Bronze / brass, Silver,
Leather, Cloth, Stone, Wood, Skin), and one stroke on the *Material* layer sets that texel's metal **and** roughness together. The
"Rood live" window lists the presets as buttons: click "Worn steel" and the foreground colour becomes its swatch and the Material layer
is selected (the window's *Swatch* line shows the active preset's metal and roughness; pick from the palette and it says so as well).
Then paint with the pencil (a brush's blended edges still count when within a few levels of a swatch; a colour that is none of the
swatches is ignored). Erase to take the preset away. Transparent = no preset.

Precedence per texel: a **Metal or Roughness layer pixel painted over the Material** (opaque and different from the game's value,
which is what those layers are exported with) wins for its own channel (fine-tuning on top of a preset), else the **Material
layer's preset**, else the game's value. One caveat: a Metal or Roughness pixel still at the game's own value counts as untouched, so
to force the game's value over a preset paint it one level off. Metal and Roughness layers you never touch give way to the presets
completely; files without Metal or Roughness layers (only Material) import too.

The presets are data. Shipped: `pc/assets/materials/paint-presets.json` (CC0, no game data): per preset `id`, `name`, `colour` (the
swatch: distinct, never a gray), `metal`, `roughness` and `height` (0..1, whichever of the channel table's rows has `"presets": true`; a
preset with no `height` leaves Height to its layer and the game), optional `notes`; channels the table does not have yet (emissive,
alpha...) are carried as given. Starter values (metal / roughness): Polished steel 1.0 / 0.12 (#82b1ff), Worn
steel 1.0 / 0.42 (#2962ff), Dark iron 0.9 / 0.62 (#651fff), Rusted iron 0.4 / 0.85 (#ff1744), Gold 1.0 / 0.2 (#ffea00), Bronze / brass
1.0 / 0.32 (#ff9100), Silver 1.0 / 0.15 (#64ffda), Leather 0 / 0.6 (#d500f9), Cloth 0 / 0.92 (#ff80ab), Stone 0 / 0.85 (#76ff03),
Wood 0 / 0.72 (#00c853), Skin 0 / 0.55 (#ffab91). Yours go in `pc/user/lighting/paint-presets.json` (`$ROOD_PAINT_PRESETS_USER`, or
`<$ROOD_LIGHTS>/paint-presets.json`): `{"presets": [{"id": "worn_steel", "colour": "#2962ff", "metal": 0.8, "roughness": 0.5},
{"id": "mud", "name": "Mud", "colour": "#aabb00", "roughness": 1.0}]}`: an entry of a shipped id replaces it in place, a new id adds a
swatch after the shipped ones, `"disabled": true` drops one; a preset with only `roughness` leaves metal to the lower layers. A change
of a preset's *values* reaches the game on the next stroke of `live` (and the next `import`); a change of a preset's *colour* or a new
or dropped preset needs the palette refreshed in existing files: `python3 pc/tools/paint_roundtrip.py palette pc/user/paint/ashley`
(close the files in Aseprite first; pixels and layers are untouched; re-exporting does the same and also resets your work). Strokes
already painted keep their old colour, so change a colour only for presets you have not painted with. The swatches are written into the
palette with their names (the format's colour names); Aseprite 1.3.18 does not show them and drops the names when it saves (the
colours stay), which is why the extension's window lists the presets by name. A contributor's Material layer is part of their
.aseprite file like the others, so a shared pack carries it and imports on the owner's side with the owner's presets.

How it works: the extension sends the changed layer's cel (about 10 times a second and once more when the stroke ends) as a WebSocket
frame (every layer when it connects: the bridge keeps the last cel of each); the bridge turns them into the maps exactly as `import` does and puts it into the running game's memory (`material map_set`),
no file per stroke, the game's next frame draws it (about 80 ms with the screenshot's own cost in `aseprite_smoke`). The
in-memory map holds until a file of that map is written; the save's import then replaces it with the same pixels.
A Material stroke pushes every channel its preset sets. `import` reads the .aseprite files too (the newest file of a channel wins over PNG exports).

**The channel table.** The channels are one table, `pc/tools/aseprite/channels.json` (read by `paint_roundtrip.py`, `paint_live.py` and
`material_presets.py` through `pc/tools/paint_channels.py`): per row the game's map name (`channel`), its Aseprite `layer`, the `blank`
value of a map nobody painted (height 128, metal 0, roughness 255: the same as `materials.cpp` `blank_value`), where the export gets the
game's current map (`seed`: a plane of the glTF's metallicRoughness image, or an image of its own: `tex/<material>_Height.png`, with the
untouched copy in `seed/`), whether presets can set it, and how the layer is exported (visible, opacity). Layers are written bottom to
top in table order under the Material layer. The bridge's greeting names the layers and each preset's values to the extension, so the
extension has no list of its own: a new channel (alpha, emissive) is a row. A package exported before the Height row has no height seed:
its Height is flat 128 (re-export to seed it). Height is not read from PNG exports of other painters (SubCraft writes a Height image of
its own that is not the game's relief); the Aseprite layer is its painter.

**How a painted height shades.** The same as a baked one: `use_heights` (emissive.cpp) gives a map's texels as heights times the entry's
`bump`, adds the texture's own estimate on top, and the Sobel slopes, the sunk (cavity) term and everything downstream (relief normal,
bent lights, GGX, baked light, indirect, sky) are the same code. The share of the estimate is a tunable per origin: `baked_texture_relief`
for the baked layer, `painted_texture_relief` (default 1, the same) for the user's layer and maps set live; a shipped map is the whole
relief. Because the Height layer starts as the game's own map and the estimate keeps its share, the first stroke changes only the texels
it touched; set `painted_texture_relief` to 0 for the painting alone to be the relief. RPC `material map_set` (key, object?, channel
`height`, w, h, base64 gray) sets a map in memory, `material map_clear` (key, object?, channel) takes it away. A map of an *object* (`@g...`,
`@m...`) shows only where a catalog entry names that object (the import and the bridge write it); a map of the texture key alone shows on
every use of the texture. Smoke: `pc/tests/material_height_paint_smoke.py`.

Reinstall the extension after updating it (`build_extension.py`, version 1.2.0: the layers come from the bridge, the Height layer).

Version notes: needs `sprite.events`, `WebSocket`, `Image.bytes` and `Timer` (all present in 1.3.18.6-dev; checked in a headless run: the
extension connects, the bridge receives the strokes, the status window opens); the Lua is `pc/tools/aseprite/rood-live/rood-live.lua`
(a package.json beside it). The sprite must be in RGB colour mode. Height is not exported (the import never writes the relief).

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

The emissive panel's texture picker: **Pick texture**, click a surface; the picked object (a room's group, or a model) is listed with its entry if it has
one, else *New entry for this texture on this object* and *...for all of this object's textures* (`material new
{object}` over RPC) make an entry for that object only; the Materials panel does the same from the selection.
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

### Model keys (materials of a model follow the model)

A model's paint used to be keyed by `m` + 8 hex digits, the hash `actor_native.c mesh_for` makes of the model and the texture page it was loaded into, so
one character had two ids (Ashley at two pages) and a pack made on one machine might not match another. Models now also have a **content key**:
`m` + 10 hex digits, `pc_model_content_key` (`pc/src/model_key.c`, tested by `model_key_test`), a hash of the object header, vertex groups, vertices and
polygon records with each texture-coordinate v taken relative to the first polygon's page bits (the loader adds the texture slot's row to every v: the page
enters a model only there). The same model has the same key at any slot, in any room, on any machine. (Spike S2: no collisions over 657 files.)

- `object.models` in an entry takes either: a 10 digit content key, or an 8 digit legacy id. A legacy id matches the model drawn with that id (as before) and
  any model its alias names: `<lighting>/model_aliases.json` (`$ROOD_MODEL_ALIASES`), `{"4b8870d7": "1a2b3c4d5e"}`, read with the catalogs (RPC `material
  action=reload` re-reads it), plus the pairs the game has seen drawn this run. Map folders are the entry's own text (`<texture>/@m<id>/`), so an unmigrated tree
  keeps working untouched.
- `material pick` names a model by its content key (`object.text` = `m<key>`, `object.legacy` the legacy id). `material model_ids` lists every model mesh known:
  `{legacy, content}`. `model_dump` takes either id. New exports (`paint_roundtrip.py export model`) key the package, its entries and its map folders by the content
  key and keep the legacy id in `seed.json` (`model_legacy`).
- **Migration** (`paint_roundtrip.py rekey [--visit Z,R ...] [--apply]`): asks the game for the ids of the models in the rooms visited, merges them into
  `model_aliases.json`, and with `--apply` moves the user catalog's model entries to the content key and COPIES their map folders to `@m<content>` (the legacy
  folders stay, a conflicting file: the newer wins and it is listed). Run it again after visiting more rooms: a model loaded at another page has another legacy
  id, which only the alias lets an old entry reach. Nothing is deleted; `model_key_smoke.py` proves the owner's Ashley paint is the same picture before and after.
