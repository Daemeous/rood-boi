# Emissive texels: textures that glow

Some textures show light: lit window panes, a magic circle, a forge's coals,
glowing glyphs. On the PS1 they were drawn like any other texel, darkened by
the room's baked vertex light, and our lighting darkens them further. An
**emissive** texel is drawn at least as bright as its own colour (times a
tint and a strength), whatever the scene's light; it feeds bloom; and with
ray tracing it lights the room (it becomes a game glow, [Glows.md](Glows.md)).
This is the base for window light shafts and fog emitters later.

Which texels glow is data: a curated catalog (`pc/assets/emissive/catalog.json`,
committed) and the user's own entries (`emissive.json` in the user lighting
directory, `pc/user/lighting/` or `$ROOD_LIGHTS`; `ROOD_EMISSIVE_USER` and
`ROOD_EMISSIVE` override the two paths). Everything else stays as the PS1
drew it.

| Piece | What |
| --- | --- |
| `pc/assets/emissive/catalog.json` | curated entries (hashes and numbers only) |
| `pc/src/emissive.cpp` | catalog, texture uses and their content keys, the slot map, CPU evaluation, room and object lights |
| `pc/src/emissive_editor.cpp` | picking a texture in the view, the editor panel, RPC `emissive` |
| `pc/src/materials.cpp` | painted channel maps per texture ([Materials.md](Materials.md)) |
| `pc/shaders/vulkan/emissive.glsl` | the per-texel test, shared by every draw shader (OpenGL splices it in as `PC_GLSL_EMISSIVE`) |
| `pc/tests/emissive_smoke.py` | the test (run.py suite `emissive_smoke`) |

## Entries

```json
{
  "id": "E004",
  "name": "Forge coals (workshops)",
  "where": "zones 43-47 room 0",
  "match": [
    { "key": "381844b9c2", "size": [33, 31] },
    { "key": "767480c2c3", "size": [31, 31], "offset": [32, 0] }
  ],
  "mask": { "indices": [5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15] },
  "color": [1, 0.9, 0.75],
  "strength": 1.6,
  "light": 1,
  "review": "checked"
}
```

- **match**: the textures it applies to. A texture is what a polygon
  samples: its UV box in its page. `key` is the fixture catalog's content
  key ([Fixture_Catalog.md](Fixture_Catalog.md), `fixture_lib.py` `ikey`):
  md5 of the texel indices under the box (rows, uint16 little-endian) and
  the box's extents, 10 hex digits. It is the same in every zone the texture
  is loaded into, wherever it lands in VRAM. `size` is the box in texels
  (only boxes of a catalogued size are hashed). `offset` places this box in
  the entry's texture space (another polygon that samples part of the same
  image). `clut` (optional) also requires the palette's colours (md5 of the
  16 or 256 CLUT entries): needed for keys too common on their own (a 1 x 1
  box). A texture animated in VRAM (a VRAM copy cycling frames: the floating
  blocks' circle, the forge's bed) lists one key per frame.
- **mask**: which texels glow, in texture space (texels, from the first
  box's origin; x right, y down). All parametric, no images:
  - `rects` `[x0, y0, x1, y1]` (x1, y1 excluded) and `polygons` (lists of
    `[x, y]`; a texel's centre inside). No shapes: the whole texture.
  - `indices`: a CLUT index set (4- and 8-bit textures).
  - `color_min`, `color_max` (5-bit RGB) and `luma` `[lo, hi]` (0..1): the
    drawn texel's colour must lie in them.
  - `map`: `"emissive"`: a painted map per texture, each texel's amount
    (0..255) from `materials/<key>/emissive.png` of the matched texture
    ([Materials.md](Materials.md)); with shapes and filters too, all apply.
  The shapes are where it can glow; the filters are tested per drawn texel,
  so palette animations and palette swaps keep exact masks.
- **color** (tint), **strength**: the emission is texel colour x tint x
  strength x the `emissive` setting, in display light (1 = the texel's own
  colour). Above 1 only matters for bloom (and HDR output).
- **light**: 0, not a light (it glows on screen and blooms); above 0, its
  triangles are game glows with this weight (below).
- **scope** (optional): where the entry applies (next section). Without
  one it applies everywhere its textures are drawn.
- `enabled`, `review` (`checked`, `new`), `note`, `where`: as in the fixture
  catalog.

## Scope

A mask lives in texture space, so every surface that samples those texels
glows: the same stone texture around a lit window is also a patch of floor
far below. A scope limits an entry to where it is meant:

```json
"scope": {
  "rooms": [[45, 0]],
  "surfaces": [{ "room": [45, 0], "polygons": [43444, 43484] }],
  "models": ["4b8870d7"]
}
```

- **rooms** `[zone, room]` (as the `state` RPC and the lights files name
  rooms): the entry is marked only while the player is in one of them
  (everything it would mark there: room, other room blocks, characters).
- **surfaces**: room polygons, by their byte offset in the room's geometry
  data (`scene_capture.h` `scene_triangle.polygon`; a quad is one polygon,
  both its triangles). The offset is the game data's own, the same on every
  visit and every load, independent of drawing order or VRAM; groups that
  share one geometry block share its polygons.
- **models**: characters, weapons and props, by the content hash of the
  model's data (8 hex digits; `actor_native.c`: its kind, groups, vertices
  and records, the hash its mesh cache keys on), the same for every copy of
  the model in every room.
- No scope, or an empty one: everywhere (the curated v1 entries). `rooms`
  and the surface lists combine: with rooms, surfaces and models apply only
  in those rooms; with surfaces or models, only those glow (a room polygon
  in `surfaces` or a mesh of a model in `models`).

How it is drawn: an entry limited to surfaces or models gets one of 16
**scope bits** while it is marked (more such entries at once are left
unmarked, and the editor says so); its slot carries the bit (parameter byte
13). `room_native.c` and `actor_native.c` tell `emissive.cpp` which mesh
draws which room geometry block or model (`pc_emissive_room_mesh`,
`pc_emissive_model_mesh`); on a change of the catalog or the room each mesh
gets its polygons' scope bits (`render_mesh_emissive_scope`: the vertices'
`emissive_scope` field, otherwise padding, re-uploaded only then), and the
native shaders pass them from the geometry stage to `emissive_texel`, which
skips a scoped slot unless the polygon has its bit. The packet path (native
room off) has no bits: scoped entries do not show there. The lights (room
triangles, object triangles) apply the same scope on the CPU.

Editor (Details > Emissive, the selected entry's **Scope**): *Limit to this
room*; *Limit to picked surfaces*: while armed, each click in the view adds
the room polygon (or the character's model) under it, a second click on it
removes it; *Add / remove the picked surface*, *Clear surfaces*; **Show
other uses** outlines in the view every other surface the entry lights
(orange, pulsing; "N other surface(s) glow too (M in view)"), its scope's own
surfaces (green) and those its scope leaves out (grey), so the side effects
of a texture mask are visible before saving.

A user entry with a curated id replaces it ("curated, with your changes");
other ids (`U001`...) are added. Delete or *Revert to curated* in the
editor drops the user's copy.

## How it works

**Texture uses.** `render.c` notes every texture a native list draws: each
instance added to a room, object or room-block list (not effects lists:
their meshes live one frame) gives its polygons' (page, CLUT, UV box), with
the instance's texture override applied (characters' texture slots). A use
lives as long as its mesh (`render_mesh_destroy`). Each use of a catalogued
size is hashed once (2 M texels per frame at most) and matched against the
catalog; a match is kept while the use lives. One that did not match is
tried again every frame (VRAM animations) and when VRAM under it changes
(`gpu_hw.c pc_hw_mark`: a texture loaded later). Uses are noted and matched
whatever the setting, so turning it on shows the marks at once.

**The slot map.** A VRAM-sized R8UI texture, 4096 x 1028: rows 0-511 have
one cell per texel position (x = page x * 4 + u shifted by the page's colour
depth, so 4-, 8- and 16-bit texels share one addressing), 0 or the slot of
the entry whose shapes cover that texel where a matched use sits; rows
512-515 hold each slot's parameters (tint, strength with the setting folded
in, colour and luminance ranges, the 256-bit index set, the scope bit);
rows 516-1027 each cell's amount (a painted map's value, else 255: the
emission's factor / 255; a painted 0 leaves the cell unmarked). The CPU rebuilds it
when matches, the catalog or the setting change and uploads the rows that
changed (`gpu_hw.c emissive_sync`, Vulkan `pc_vk_hw_upload_emissive`), before
a game frame's first draw or at once on a change (a paused game's redraws
show it). With the setting at 0 nothing is written: the draw shaders' switch
(`emissive_on`, push constant / UBO field `emissive`) is off and no texel
changes.

**Drawing.** The packet shaders (`gpu_hw.c`, `hw_frag.glsl`; lit 3D only)
and the native list shaders (`render.c`, `native_frag.glsl`) look up the
nearest texel's cell (after the texture window, at the polygon's clamped or
animated-material UV), test the slot's filters and compute the emission E.
The colour becomes `max(c, min(E, 1))` (nearest texels: the edge is
texel-crisp), and the G-buffer's shade target carries the emission:
alpha 0.25 (else 1 textured, 0.5 untextured, 0 unlit), rgb E / 2. The MSAA
resolve keeps one representative sample's shade, as before.

**Present** (`scaled.frag`, `gl_present.c`; post bit 16 when anything is
marked): for a shade alpha of 0.25 the lit result becomes `max(lit, E)`,
before the haze and fog volumes (they still cover it), and the soft
shoulder above 0.8 is skipped for those pixels. Unlit pixels keep the
raster's colour (already `max(c, E)` up to 1). When the grade pass runs
(post bit 32: any post effect, bloom among them) emissive pixels may leave
the present above 1, so bloom (computed from the lit image before tone
mapping; threshold 0.7 by default) takes its light from them first; the
grade's `none` tone mapping clamps the picture to 1 afterwards, so the rest
of the picture is unchanged. With post off the window target clamps: bright
and unlit.

**Lights** (`light` > 0). `ptgi.c update_emitters` adds glow fragments:

- room triangles (the captured scene) with light texels: every texel whose
  centre lies inside the triangle's UV triangle is looked at (slot, scope,
  filters); the fragment's colour is the glowing texels' mean emission x the
  light weight x their share of the triangle's texels, the share counted as
  at least 0.25 (`LIGHT_MIN_SHARE`). `glows.c build_groups` takes them into
  the room polygon groups (24-unit bound unions, stable keys from polygon
  offsets) as additive polygons are, so an emissive window is one glow per
  window, named, overridable and keyed like any room glow. A change of the
  set of light triangles (`pc_emissive_light_serial`) regroups. The
  integration runs when the room, the catalog or the setting change, and
  again for a triangle when VRAM under its texels or CLUT changes (palette
  and VRAM animations) or room texture effects move its UVs.
- object lists and other room blocks (characters, props, the floating
  blocks), and the room's own groups the scene capture left out (a group
  disabled when the room loaded and shown later, moving with its own
  transform: Rue Vermillion's floating blocks, 32/3; a triangle of the
  room's list is the capture's when a captured triangle of its polygon lies
  where it is drawn): the same integration per triangle, cached by texture
  area and scope (a room polygon's own); object glows by mesh and by the
  side the surface faces (`PC_GLOW_ORIGIN_OBJECT`, effect category until
  promoted, as glows.c treats additive object polygons), 10 units in front
  of the surface (`glow_emissive_lift`), each a disc facing its side ([Glows.md](Glows.md),
  "Emissive texels as glows"), never shadowing the baked light.

Why the crates lit nothing (fixed 2026-10-15): an object glow was its
mesh's light triangles' mean position. The magic crates (53/21) glow on
every side, so their glows' centres were inside the crates; a crate is a
shadow caster, so every shadow ray to them hit it: no light around them,
however high the weight, and (magic glows shadow the baked light) a darker
floor (-6 levels by the crates with light 1). The floating blocks' circle
(32/3) made no light at all: its polygons are room geometry drawn with the
room's list, which the object path skipped as scene triangles, but the
capture had left its group out (hidden at load).

Why filters lost the light (fixed 2026-10-15): the glow of a light
triangle used to be the mean emission of 15 fixed barycentric points (4 for
objects): the corners, the edges' quarter points and a few inside. With
the whole texture glowing every point hits a glowing texel. A filter (CLUT
indices, colour or luminance range) or a small rect that leaves only a few
texels glowing (the bright top of a window jamb, the hottest embers) is
missed by all 15 points on most triangles: their emission came out 0, the
glow's weight (area x luminance) fell under glows.c's 1e-3 observation
threshold and the light was gone, while the screen (per pixel) still
showed the glow. Now every texel counts, so the light follows the glowing
area exactly (the coals narrowed to their hottest index: 2-7% of a
triangle's texels, still three light triangles), and the share's floor keeps
a narrow strip a usable light whose strength the light weight (up to 4)
scales.

The glows' existing budget (32 traced, per-piece strength, smoothing,
categories, shadows) applies unchanged; nothing new is traced per pixel, so
NRD's input is as stable as for any glow. Room triangles that are already
additive glows are left alone.

## Settings, RPC, editor

- Settings key `emissive` (0..2, default 1; 0 off: the picture without
  emissive texels, bit for bit). Graphics page of the modern menus (*Glowing
  surfaces*, Off at 0), the dev overlay (Graphics > Display and rendering,
  and the Emissive tab), RPC `graphics emissive` (0..4).
- RPC `emissive {action}`:
  - `list` (default): the entries (with `origin` curated / override / user
    and `marked`, the uses marked now), `active`, `marked_uses`,
    `marked_texels`, `light_triangles`, the two paths, `unsaved`;
  - `surfaces {id}` (*Show other uses*): the surfaces the entry's marked
    texels are on now (the room's polygons, models and other blocks drawn):
    `kind`, `polygon` / `model`, `texels`, `covered` (under its shapes),
    `glowing` (filters passed), `allowed` (its scope), `listed` (named by
    its scope), `picked`, `centre`, `screen` (window pixels of its
    triangles' centres in the view); `affected` (surfaces it lights),
    `other` (besides the picked one and its scope's own), `excluded`
    (left out by its scope);
  - `scope_pick {id}`: the last pick's room polygon or model into the
    entry's scope, or out of it when it is there (`added`);
  - `lights`: the room triangles that are lights now: `rgb` (the glow
    colour they give), `texels`, `glowing`; `luminance` summed;
  - `uses {keys, matched, stats, limit}`: the textures drawn (page, CLUT,
    box, key, entry; `stats`: mean colour and share of bright texels);
  - `pick {x, y}` (window pixel) or `{world: [x, y, z]}`: the textured
    native triangle there: key, size, page, CLUT, box, texel, CLUT index,
    colour, clut key, entry, emission, `glows` (the texel, as a texture),
    `surface` (`kind` room / model / other, `room`, `polygon` or `model`)
    and `glows_here` (the entry's scope lets it glow on that surface);
  - `new` (an entry for the last pick), `set {entry}` (a whole entry; no id:
    a new `U###`) or `set {id, field: value...}` (merged into a copy in the
    user layer), `remove {id}`, `save` (the user file), `reload` (both files
    from disk), `export {id}` (the entry's JSON for the curated catalog);
  - `texel {u, v, tpage, clut}`: the CPU's evaluation (slot, emission,
    light); `texture {tpage, clut, box}`: a texture's indices, palette and
    key from VRAM (authoring; game data, never for the repository).
- Editor: Details > **Emissive**. *Pick texture* arms the picker; the next
  click in the view (game camera or free camera) picks the textured
  surface under it (room, characters, props) and selects its entry, if any.
  *New entry for the picked texture*, or *Add the picked texture* to the
  selected entry (pick the other frames of an animated texture the same
  way). The entry's name, enabled, tint, strength, *Is a light* and its
  weight, mask rects (typed, or drawn: drag on the texture view; Shift adds
  a rect), the CLUT index filter (typed, or *+ picked* adds the picked
  texel's index), the colour range filter, and *Only with the picked
  texture's palette*. The texture view draws the texels from VRAM with the
  mask's texels outlined (yellow: they glow; blue: the filters leave them
  out) and pulsing. Every edit applies live. *Save (yours)* writes the user
  file; *Copy JSON for the curated catalog* puts the entry (review
  `checked`) on the clipboard to paste into `catalog.json`; *Revert to
  curated...* / *Delete...* (after a confirm). An entry's *Duplicate* (a
  copy of yours under a new id) and *Delete...* are on its title line;
  both, like every emissive edit, are undoable (Ctrl+Z). Files edited by
  hand reload live (unless the editor has unsaved changes).

## Curated v1 (2026-10-15)

| Id | What | Where | Mask | Light |
| --- | --- | --- | --- | --- |
| E001 | the floating blocks' magic circle (five animation frames) | 32/3 and the other rooms with floating blocks | CLUT indices 1-15 (0 is the block's ground) | 0.6 |
| E002 | lattice window panes of the town houses | 34/0, 35/0 (36/0 shares a texture) | index 0 (the white pane) | 1 |
| E003 | the town houses' octagonal window (one white texel; only with its palette) | 34/0 | index 0 | 0.5 |
| E004 | forge coals (four animation frames) | workshops 43-47 | indices 5-15 (embers; 0-4 ash) | 1 |
| E005 | the movable magic crates' glow lines and glyphs | 53/21 and the other crate puzzles | indices 10-15 | 0.4 |

Rue Sant D'alsa (32/7, the user's "Rue Vermillion interiors" by its zone
and room) has no bright window texture: its windows have dark panes and a
lit sill. The lit lattice windows are in the house interiors of Dinas Walk
and Zebel's Walk (34/0, 35/0). No lava or glowing rune textures were found
in room geometry (fires and the T067 magic circles are additive sprites,
already glows). Candidates not yet in the catalog are listed in the
session's report for review.

## Cost

Vulkan, RTX PRO 6000, Workshop "Metal Works" (45/0, the coals marked and a
light), Ultra (path traced GI, NRD), internal resolution 3x, 240 frames:
0.794 ms per frame with emissive 0, 0.814 with 1 (+0.02 ms: draw +0.003,
present +0.002, the GI trace +0.015 for one more traced glow). The CPU work
is one walk of the uses per frame, plus hashing when a room loads.

## Tests

`pc/tests/emissive_smoke.py` (run.py suite `emissive_smoke`, needs the
card): 45/0 paused, RPC redraw per setting. Emissive 0, emissive 1 with
every entry disabled and emissive 0 again are pixel-identical (Vulkan: on
the Original preset; lit, within a redraw's own noise); the coals get at
least 15% brighter, nothing outside them changes, with bloom (threshold 1)
a halo appears around them; Vulkan path traced: the hood above the forge
gets brighter (their light); every curated entry is found in its room
(32/3 with the floating blocks shown, 34/0 in the free camera, 45/0,
53/21); a mask made over RPC on a picked texture is saved, reloaded and
marks the same texels with the same emission. Scopes: an entry on the
forge bed's texture lights its 12 polygons; *show other uses* counts them
and each one visible changes on screen exactly when counted; limited to one
picked polygon only it changes (another polygon with the same texels stays
as without the entry), then two; a room scope of another room marks
nothing, of this room everything; the scope survives save and reload; a
light limited to a polygon lights from it only; a mask on Ashley's texture
limited to another model shows nowhere, to hers on her. Filtered light: the
coals narrowed to CLUT index 15 (7% of a triangle's texels at most) are
still light triangles with light, and with path tracing (Vulkan) still
light the hood (light 1 against light 0). Lights on objects (path traced:
Vulkan, and OpenGL with `--trace`; light 1 against 0 after the glows settle
as magic glows): the magic crates (53/21) are traced object glows and the
floor and Ashley beside them turn cyan (blue rises 0.6 levels or more, more
than red; before the fix about -6 levels); each is a solid disc facing its
side, and `glows {emissive_disc, emissive_lift}` halve its radius and move
it out; a floating block's circle (32/3, Ashley placed on it, light 3) is
an object glow and its green light reaches her in the lighting-only view
(green rises a level or more, more than red) and, on Vulkan, the front of
her legs (RPC `gi_probe`; before: no glow). Painted maps: an entry on the
floor's texture with a painted map glows nowhere without its map; the
reference export writes the texture and an empty map and refuses
`pc/assets`; a rect painted over RPC glows exactly there (picks across the
floor), the file holds those pixels and so does a reload; imported PNGs
(grayscale, alpha-only) replace it, another size is refused; a map saved
from outside is reloaded while the game runs; with a light weight the
painted floor adds light triangles. `ui_settings_probe` checks the menu
row.

## Limits and next

- Uses come from native lists: with `native_room 0` (legacy packets) nothing
  is noted, so nothing is marked (the packet shader would show marks made
  while native drawing was on).
- A texture animated in VRAM needs each frame's key; one never seen is
  matched only on the frames listed. The editor picks one key at a time.
- The mask is per VRAM location: another texture sampling the same texels
  (it is the same image) glows too, unless the entry has a scope.
- Where two entries' masks cover the same texel the later entry's slot
  wins, also on surfaces its scope leaves out (they then show neither).
- At most 16 entries limited to surfaces or models are marked at once.
- The editor's free view (`editor_view.cpp`'s own shading) does not apply
  the emission; the game view does.
- A painted map is per texture key: each animation frame of a texture
  animated in VRAM needs its own map.
- Next: window light shafts and fog emitters from light entries (their
  glows' positions and colours are there).
