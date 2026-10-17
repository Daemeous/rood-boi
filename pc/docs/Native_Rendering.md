# Native rendering

Phase 2 of [Modernization_Plan.md](Modernization_Plan.md): game code submits
meshes and transforms, the renderer draws them, instead of the renderer
reconstructing 3D from the PS1 GPU packets the game builds. This note
describes the API (`pc/src/render.h`, `render.c`), the first user (the room,
`room_native.c`) and how the rest follows.

## API

- **Meshes** (`render_mesh_create(polygons, count)`): immutable vertex
  buffers of PS1 polygons. A `render_polygon` has 3 or 4 model-space
  positions, a face normal (computed when zero), UVs, baked vertex colours
  (the PS1's lighting, 128 = texel as is), texture page and CLUT, the GPU
  polygon code (textured, raw, semi-transparent) and flags (quad,
  double-sided). Textures stay in native VRAM and are sampled exactly as the
  packet path samples them (CLUT, texture window, nearest texels; the
  optional bilinear mode too).
- **Lists** (`render_list_begin(camera, rules)`, `render_list_add(list,
  instance)`): per frame. The camera is the game's: the GTE view matrix
  (rows keep the GTE's scales, X carries the pixel aspect), H, OFX/OFY and
  the widescreen flag. An instance is a mesh with a model-to-world
  transform plus the PS1 per-polygon rule inputs (ordering-table bias,
  subdivision level). `render_list_tint` applies `func_80098D6C`'s colour
  fade/offset to a whole list.
- **Placement** (`render_list_link(list, ot_entry, packet)`): a list is
  drawn where a three-word marker packet sits in the frame's ordering table
  (a GPU no-op to everything else, recognised by `pc_draw_packet`). The list
  therefore keeps its place relative to the packets the game still builds
  (backgrounds before it, characters, effects and UI after it) and draws
  with the drawing area, offset, texture window and mask state in effect
  there.
- **Statistics** (`render_stats_get`): lists, instances and triangles drawn
  natively in the last frame, room triangles still drawn from packets by the
  hardware path, and those left to native lists. RPC `render_stats`, editor
  Diagnostics tab.

- **Bone palettes** (phase 2 step 2): a polygon carries a palette index per
  vertex (`bone[4]`); an instance carries `bones` model-to-world transforms
  (`palette`) and optionally the same model's palette from the frame before
  (`previous`). The vertex stage moves each vertex through its bone's
  model-view transform (camera x world, composed per bone on the CPU; the
  room is the one-entry case). Instances can override the vertex colour
  (flat-lit models: the room light at the model) and the texture page/CLUT
  (the model's texture slot; the polygon keeps its own blend bits).
- **Object rules** (`render_rules.objects`, BATTLE/4B16C.s): a polygon is
  culled when its first triangle's NCLIP is <= 0 (no quad fallback, except
  double-sided records) and it does not face the camera at the positions it
  is drawn at either (a triangle whose NCLIP on the unrounded screen
  positions is > 0 is kept: a sliver with a whole-pixel NCLIP of 0 covers no
  pixel on the PS1 but leaves a hole at higher internal resolutions, e.g.
  Ashley's nose, eyelids and jaw line, and the sword's edges), sorted by its average depth (quads (SZ0..3) >> 4,
  triangles AVSZ3 with the game's ZSF3) plus its own bias, kept below
  bucket 0x7c0; non-planar "split" quads pick their diagonal from the GTE
  OP of the projected corners as `split_quad` does and cull each half. No
  UV halving, no subdivision. Object lists never reset depth (below) and
  write 0.75 into the G-buffer normal alpha (moving geometry, as the packet
  path marks characters).
- **PS1 order** (`render_list_order(list, entries, count)`): the polygons of
  a list's instances in the order the PS1 draws them this frame (entries
  `instance << 16 | polygon`). With an order the list paints its polygons in
  that order (only the surfaces drawn before the list hide them) instead of
  depth-testing them against each other (`pc_native_actor_order`).
- **Translucent polygons** (phase 2 step 3; `render_list_translucent(list,
  entries, flags, count)`, `render_list_link_translucent(list, first, count,
  ot_entry, packet)`): semi-transparent polygons (code bit 1, or every
  polygon of a blended instance) never draw with their list. The list keeps
  a translucent sequence of polygon entries (`instance << 16 | polygon`),
  and five-word translucent markers in the ordering table draw ranges of it
  where the PS1 would draw those polygons, so they keep their place against
  everything else in the table (other translucent polygons, effects, 2D).
  A translucent marker only draws when its list's own marker drew earlier in
  the same table (else the packets draw). Entries can carry
  `RENDER_TRANSLUCENT_DECIDED` (the game already culled and sorted the
  polygon: no back-face or range test here) and `RENDER_TRANSLUCENT_HALVE`
  (the game halved its UVs).
- **Blended instances** (`render_instance.blend = RENDER_BLEND(mode)`): the
  whole instance semi-transparent with PS1 mode 0-3, its polygons textured
  and modulated, the mode's bits replacing the polygon's own (the blended
  builders). With `RENDER_OVERRIDE_TEXTURE` the CLUT is the instance's plus
  the polygon's (material groups that switch to an alternate CLUT keep a
  0x480 offset per polygon).
- **Layered lists** (`render_rules.layered`): room rules, but drawn over the
  native surfaces so far (no depth reset, depth-tested and added to them, as
  object lists are) for room blocks other than the room itself.

### Drawing

`render.c` draws each instance with one `GL_LINES_ADJACENCY` call (four
vertices per polygon), so a geometry shader sees whole polygons. It applies
the PS1's per-polygon rules on real view-space positions: back faces
(NCLIP < 0, with the quad fallback to the 3,2,1 triangle and the
double-sided flag), the ordering-table range (largest vertex SZ >> 2 plus the
bias, within the room's near/far buckets), and the distance UV halving
(beyond the LOD bucket: UVs >> 1, texture page - 1, unless the PS1 would
subdivide the polygon). It then projects exactly as the GTE projects
(`sx = OFX + H x / z`, X * 3/4 about the screen centre in widescreen) into the scaled VRAM
target, with real near-plane clipping instead of the PS1's near-polygon
subdivision, and writes per-pixel depth as GTE SZ / 65536, the encoding the
packet path and the present pass use. The G-buffer outputs (colour,
view-space normal with a = 1 for room surfaces, albedo, shade) are those of
`gpu_hw.c`'s draw shader, so lighting, PTGI, haze, depth of field, picking
and FXAA consume native surfaces unchanged.

Positions: model-to-view is evaluated in float and snapped to whole units
(`floor`), as the GTE produces them (the packet path's sub-pixel positions
also divide the GTE's integer IR1/IR2 by SZ). Unsnapped, sub-unit gaps
between groups opened into visible seams at 4x. Back faces are tested on
whole-pixel screen positions for the same reason (the PS1 keeps edge-on
slivers with NCLIP 0, and they fill seams). Nearest texels are clamped to
the polygon's UV range (edge pixels otherwise picked up neighbouring atlas
texels).

Depth: the list first resets depth over its drawing area (whatever came
before it in the ordering table is behind it), draws depth-tested, and its
depth is copied to `room_depth`. Packets drawn after it are hidden where they
are more than a small tolerance behind it (`z > room * 1.02 + 8`,
`pc_native_occlusion`, editor checkbox), so the PS1's per-polygon painter's
order becomes per-pixel for characters against the room while decals and
drop shadows on the floor stay. Packets keep their own (PS1-like) depth.

Coplanar room surfaces (`pc_native_room_order`, default 1; RPC `graphics
room_order`, settings key `native_room_order`, editor Diagnostics checkbox):
the PS1 had no depth buffer, so a room polygon lying on another one (the
documents on the council table in the attract cutscene, notices on the
walls of Rue Lejour, rugs) won by the ordering table: the larger table
polygon has the larger largest-vertex SZ, sits in a farther bucket and is
painted first. Depth-tested, the two z-fight (whole-unit vertex snapping
puts the paper a fraction behind the wood or in front of it, per pixel). A
room list therefore draws in three passes (`draw_room_ordered`,
`vk_draw_room_ordered`):

1. depth only into the native surfaces' depth (`object_fbo`): the nearest
   room surface per pixel, D;
2. colour and G-buffer into the frame, only fragments within the slack of D
   (`v_z <= D * (1 + 0.004) + 2`, `pc_native_room_slack`, RPC `room_slack`,
   `room_slack_units`; `surface_test` 4), depth-tested on the PS1 draw
   order key instead of depth: `bucket << 10 | sequence` (bucket = largest
   SZ >> 2 plus the instance bias, as the range test computes it; sequence =
   the polygon's index in the list's walk, scaled into 10 bits; smaller paints
   later, so it wins: nearer bucket, and within a bucket the polygon linked
   first, as linking at the bucket's head does);
3. depth only into the frame with the fragments at D (the objects' pass 2),
   so the frame's depth, the copy to `room_depth`, lighting, PTGI, haze and
   DoF see the real nearest depth, never the key.

Real occlusion stays per pixel beyond the slack (about 4 units at a typical
depth of 500), so walls, furniture and floors that merely meet keep exact
edges; at contact lines a pixel-wide strip can follow the ordering table as
on the PS1. MSAA runs the same passes on the twins (OpenGL syncs pass 1's
depth to the single-sampled surfaces as between an object list's passes;
Vulkan reads the multisampled twin). Regression test `coplanar_order_smoke`
(attract cutscene papers against the packet path's ordering-table picture,
stable over game frames, MSAA; with a memory card the forge's coals on the
floor in Workshop "Metal Works", zone 45, and the notices of Rue Lejour,
zone 32 room 15, at eight camera yaws).

Objects and other room blocks (layered lists: floor markers) are painted
after the room surfaces they lie on, so with `pc_native_room_order` they
test against the native surfaces as if the same slack nearer
(`layer_slack`: pass 1 writes that depth into the surfaces, pass 2 and
their translucent polygons compare it, the frame keeps the real depth): a
model resting on a table or a marker on the floor wins there as on the
PS1. Effects lists keep their own decal slack.

Survey (all 364 rooms with doors, arrival view, `room_order` off/on on the
same redrawn frame): 96 rooms change anywhere, by up to ~1850 pixels at
960 x 720 (Rue Mal Fallde and Rue Lejour: notices and the black shells
that cover the floor beyond the play area; workshops: coals in the forges,
cloth on shelves); elsewhere single-pixel contact lines. A wider slack
(2% + 8) changes at most 264 more pixels per room (foliage, contact lines),
so the default stays narrow: interpenetrations deeper than the slack (a
hanging pelt pushed into a wall) still clip per pixel.

Frame interpolation: a list draw is recorded as an operation of the frame's
draw stream (list id, drawing area, offset). The replay redraws it from the
list ring (`render.c` keeps the last 8) with the interpolated camera; world
transforms make that exact, and the packets replayed after it are occlusion
tested against the replayed room. A recording's camera is its room list's
camera (not the GTE camera captured at present time, which already belongs
to the frame the game is building); lighting and path tracing reconstruct
through the camera of the recording whose picture is presented
(`pc_hw_view_camera`).

The game double-buffers: it draws frame B into the buffer that is not on
screen (x 0 or 320) while showing the frame before it, A, and flips to B
when it has drawn the frame after B. So when B arrives, A is shown as is,
and the vsyncs until the flip show B replayed at t = vsyncs since B arrived
/ B's interval (1/2 at 30 fps), presented from B's own area of the replay
target (`pc_hw_present_origin`; the GL and Vulkan presenters, lighting and
path tracing all take that area, so its G-buffer, camera and object lists
are the replay's). This adds no latency, since B is what the game shows
next. A frame drawn into the displayed buffer itself is shown from its
arrival, blended as before. Before this, the replay drew B into its
off-screen area while the displayed area showed a copy of A, so every
other presented image repeated the one before it. Opening pan, presented
frame-to-frame mean differences (0-255): 0.00/2.5 alternating before, an
even 0.9-2.3 after (GL and Vulkan); gameplay walk 0.00/9.7 -> 6.4-7.1,
R1 turn 0.00/8.6 -> 2.4-6.5 rising with the turn's speed. Regression test
`interpolation_smoke`. Discrete changes (a cutaway wall hidden, 2D
sprites) still step: the interpolated image shows B's state.

2D batches that sample what their own frame drew (texels inside an
earlier draw's area: the original Status and Equip screens redraw the rows
under their information line from the frame buffer every frame) sampled
the target as drawn so far when drawn live (`pc_hw_sync`), but the replay
redrew them from native VRAM: the CPU's 4:3 picture, whose 2D is not
compressed at 16:9. Every in-between image then showed a smeared copy of
"INFORMATION" to its left and dashes right of the stat lines (issue #8).
The replay now gives such a batch (`op_feedback`) the replay target as
replayed so far over its texels (`replay_sample_own`: read back at native
resolution and uploaded into the sampled copy of VRAM, then native VRAM
again after the draw). Regression test `interp_menu_2d_smoke` (16:9,
`present_rate` 1 and 3).

2D batches whose texels or CLUT the game rewrote after they drew
(`op_rewritten`: the memory card screens swap the CLUT row their pictures
share in for each save's thumbnail and back) were replayed by copying the
game's finished picture over where they drew; every later batch then drew
there again, and translucent ones blended twice: on the field's DATA > LOAD
list the "MENU" title shone brighter on every in-between image (issue
#14). The CPU's VRAM writes now keep what they replace (up to 4096
halfwords; `pc_hw_before_write` from LoadImage and VRAM copies), and the
replay gives such a batch the sampled copy of VRAM with the writes since
undone, newest first (`replay_unwrite`), then native VRAM again. A write
that was not kept still falls back to the copy.

Widescreen sides beside extended 2D: untextured 2D reaching the screen's
edge (menu bars) stretches to the view's edge, but a screen whose
background is 2D (the title's memory card screen) erases its bars on the
PS1 by drawing the background again over the 4:3 screen, which compressed
never reaches the view's sides. What the bars painted there stayed in each
frame buffer: after the card screen closed, the right ends of its three
save bars; above the first save, the end of the bar it slid away from,
different in each buffer and so flickering (issue #14, with or without
interpolation). The sides an extension paints are now remembered
(`side_paint`), and an opaque compressed 2D rectangle over the screen's
edge column clears the remembered side beside its rows to black (recorded
as a fill, so in-between images clear it too): the extension goes with the
bar it continued. Anything else drawn over a remembered side (3D, full-view
or anchored 2D, fills, uploads, copies) makes it theirs, never cleared, so
field screens keep their Hor+ sides. Regression test `interp_menu_2d_smoke`
(the field's card list; the title's card screen opened and closed with
interpolation off, then on at `present_rate` 1 and 3).

Object lists (characters): drawn into the colour target and G-buffer with
`room_depth` (the native surfaces so far: the room, then earlier object
lists) as their depth buffer, so they are hidden exactly by the room and by
each other and add themselves to what later packets are occlusion-tested
against (a drop shadow or effect behind a character is hidden there). With
a PS1 order, the list's own polygons paint in that order instead (a copy of
`room_depth` from before the list is the only test: `surface_before`). A
second pass writes the visible fragments' depth into the frame's depth (what
post effects read). An object list reached before any room list of its
ordering table (a world-frozen menu's model over 2D: MENU3/MENU4/MENU9/
MENUB's Ashley through MAINMENU's clone, the modern menu's Ashley tab) finds
the surfaces empty: `room_depth` is cleared to far over its drawing area
first, as a room list leaves it before its first polygon, so the model is
native (internal resolution, MSAA, interpolated) instead of 2D packets; a
room list after it in the same table paints over it, as on the PS1. Such a
picture's camera (`pc_hw_view_camera`, what the lighting, haze and path
tracing reconstruct world positions through) is then its object lists'
camera, on live and interpolated images alike (before, live images used the
scene's GTE camera and interpolated ones the menu's: the lighting moved on
every other image, a flicker). The modern Ashley tab stands its clone at
Ashley's place in the room and orbits it there and asks for a detail view
while it shows (`pc_hw_detail_view`: the internal resolution at least the
window's, up to 8x; the setting back when the model goes). Its frames
(`pc_hw_present_menu_model`) are lit by a studio rig of their own
(`pc_hw_present_menu_studio`, `pc/shaders/vulkan/menu_studio.glsl` in both
presenters: the texture's colour under a key, a fill, an ambient and a rim
in view space; its values are data, the theme's `menu_model_light` block,
`ui/menu_light.h`, handed to the shader as nine vec4s: GL uniform
`menu_studio_rig`, Vulkan `studio_` at the end of the present UBO; RPC
`graphics menu_light`, Modern_Menus.md), not by the room's lights, GI and haze nor by the game's
colours for the model; the bridge leaves out the offset copy MAINMENU draws
behind the clone as a rim (`no_rim_copy`). RPC `graphics menu_studio` 0
gives back the game's colours, the rim copy (at the next build) and the
room's light.
With `pc_native_room` 0, characters stay packets.

Translucent draws (`render_draw_translucent`): a range of the sequence draws
into the frame target with depth ALWAYS, in sequence order, each fragment
hidden where it is behind the native surfaces drawn before it (room depth
read as a texture: exact for objects, the packet path's room tolerance,
0.2% + 1, for room polygons that lie against room surfaces). As the packet
path does for translucent packets, each run of one instance and one blend
mode draws twice: the opaque texels (no STP bit) write colour, G-buffer and
the frame's depth like opaque surfaces, then the STP texels (all pixels
when untextured) blend with the page's mode (`B/2+F/2`, `B+F`, `B-F`,
`B+F/4`) into the colour only, leaving the depth and the G-buffer of the
surface behind them. Translucent pixels never write the native surfaces'
depth, so they do not hide packets or each other. In the hardware path,
consecutive translucent markers that continue one range of one list (a
model's buckets, far to near, with nothing else drawn between) draw as one
range. Replays redraw them with the interpolated camera and palettes.

Frame interpolation of object lists: the replay redraws the list with the
interpolated camera and each instance's palette blended between `previous`
and the current one: rotations slerped through the skeleton, poses the
game holds between changes spread over the hold (a jump over 512 units in
any bone is not blended). See "Animation in-betweens" below.

### Ownership and lifetime

Everything runs on the game thread, which owns the GL context. Meshes belong
to their creator and live until `render_mesh_destroy`; handles carry a
generation, so a list (or a replay of an old list) that names a destroyed
mesh skips it instead of drawing a reused slot. `render_mesh_update` gives a
live mesh new polygons (same count, same handle; on Vulkan the vertices move
to a new range and the old one is freed when the frames in flight finish):
room texture animations rewrite polygons in place. Lists live in a ring of 8:
the newest few frames stay replayable. Without the hardware rasterizer
(`ROOD_SCALE=0`, software presentation, a failed shader) `render_list_begin`
returns 0 and callers keep their packet paths.

## The room (`room_native.c`)

`func_8009723C` (`room_geometry.c`) still walks the room's groups each frame
and still decides visibility exactly as before: the hide flags
(`flags & 0x1100`), the alternate set (`flags >> 15 & mode`) and the cutaway
facing masks (for the editor's free camera: "Seeing into rooms" below). For the
current room (the block `pc_scene_room` captured) it calls into
`room_native.c`:

- once per room block, one mesh per distinct geometry block, decoded as
  `room_geometry.c` and `scene_capture.c` decode it (group-local positions,
  the 0x800 delta shift, colours, UVs, page, CLUT; the double-sided flag in
  bit 0 of the code byte), with a map from the block's polygon index to the
  mesh's;
- per visible group, an instance whose world transform is the inverse
  camera times the group's camera-composed matrix (group + 0x20), so the
  live frame matches the GTE exactly and a replay can re-project it;
- the list's marker goes into the room's ordering table at the farthest
  room bucket, and `func_80098D6C`'s tint, when the game applies one to the
  room packets, applies to the list too (its packet walk steps over
  translucent markers);
- each semi-transparent polygon the game keeps (its own back-face, screen
  and ordering-table tests in `pc_room_group_packets`) gets an entry
  (`DECIDED`, `HALVE` when it halved the UVs) and a translucent marker
  linked at its bucket right after its packet, i.e. drawn exactly where the
  packet would be. A subdivided polygon gets one marker at its top-level
  bucket and draws whole (its leaves' packets are skipped). Out of packet
  space the room's translucent polygons all stay packets for that frame.

`pc_native_room` (`ROOD_NATIVE_ROOM`, RPC `graphics native_room`, editor
Graphics checkbox):

| Value | Room packets built | Hardware draws |
| --- | --- | --- |
| 0 | all | all from packets (the reconstruction path) |
| 1 | all, for the CPU's reference VRAM | opaque from meshes, semi-transparent at markers (`pc_native_blend`) |
| 2 (default) | semi-transparent only | opaque from meshes, semi-transparent at markers (`pc_native_blend`); the CPU rasterizes none of the room |

Mode 1 keeps the native VRAM complete by drawing everything twice. Mode 2
(the default since phase 2 step 4) saves the room's GTE work and software
rasterization; native VRAM then lacks the room until something reads it:
see "Without reference packets" below (StoreImage readers, primitives that
sample the framebuffer, VRAM copies, RPC `screenshot` and the checkpoints
copy it back from the hardware target).

Semi-transparent room polygons (stained glass, window light, the save
point's light column) are native with `pc_native_blend` (`ROOD_NATIVE_BLEND`,
RPC `graphics native_blend`, editor "Native translucent polygons", settings
key `native_blend`; default 1): the list's packet range is then skipped by
the hardware path for its semi-transparent packets too, once the list drew.
With 0 they draw from packets as before.

Other blocks drawn through `func_8009723C`: `func_8008DF14` draws one quad
per room section 9 entry (a fading floor marker: `func_8008E19C` writes the
quad's position and brightness into a one-group block, `D_800E8548`, and
draws it with the camera matrix as the group's). With `pc_native_objects`
(`ROOD_NATIVE_OBJECTS`, RPC `graphics native_objects`, editor "Native room
objects", settings key `native_objects`; default 1) such a block becomes a
layered list: its marker one bucket nearer than the room's (after it),
depth-tested over the room, its semi-transparent polygons at translucent
markers like the room's. The block is rewritten between calls, so its meshes
are keyed by a hash of all of its geometry and kept for 8 frames (replays).

### Room texture animations: animated materials (2026-10-11)

A room's texture effects (MPD room section 0xC, set up by `func_80092548`,
run every game frame through the table `D_800E85CC`, BATTLE `2842C.c`):

| Type | Size | Function | What it changes | On the disc |
| --- | --- | --- | --- | --- |
| 0 | 24 | `func_80091C6C` | one polygon's UVs: atlas frames | none |
| 1 | 20 | `func_80091FE8` | VRAM: `MoveImage` of an atlas frame or a vertical scroll, full and half-size copies | 544 in 325 rooms (two in the save room) |
| 2 | 32 | `func_80091E10` | one polygon's UVs: a scrolling window | 1590 in 372 rooms (the save point's sigil and light column) |
| 6 | 36 | `func_800923F8` | a group's rotation (doors) | 720 in 330 rooms |

VRAM copies reach native draws by themselves (the copy marks native VRAM
tiles dirty; they are uploaded before the next native list draws) and group
rotations come with the group's matrix each frame.

Types 0 and 2 rewrite one polygon's UV bytes: `uv = (base uv + offset) &
255` per vertex, the offset an atlas frame's corner or the scroll window's
position (`func_80091E10` steps it by `(stepU, stepV)` every `delay + 1`
ticks and wraps it inside the region `base .. base + size` so the visible
window stays inside). They are **animated materials** (`room_effects.c`):

- At room load the effect section is parsed (the same walk as
  `func_80092548`, so effect `slot` = the game's slot) and each type 0/2
  polygon is located in the room's geometry blocks. Its native mesh keeps
  the effect's base UVs and a per-polygon effect index
  (`render_polygon.uv_effect`, a vertex attribute).
- Every game frame (`pc_uv_effects_frame`, from `pc_room_native_begin`) the
  offset is read back from the polygon's own bytes, checked against every
  vertex, and the room list gets one entry per effect
  (`render_list_uv_effects`: offset, motion since the previous frame, the
  scroll's window and wrap period). The shaders add the offset per vertex
  with the bytes' wrap (`native.geom` / GL geometry stage), so game frames
  are exactly the packet path's; the game's own state drives everything
  (timing, pauses, starts and stops, save/load). Nothing is re-uploaded:
  `render_stats.mesh_updates` stays 0 in the save room, `uv_effects`
  counts the table.
- Frame interpolation (`interp_uv`, settings key and RPC `graphics`; the
  overlay's "Texture scrolls"): 0 steps with the game; 1 (default) draws in
  between game frames at `offset - (1 - t) * motion`, the motion unwrapped
  across the scroll's wrap, every game frame exact; 2 smooth: the save
  point steps once every two game frames, so 1 moves in every other frame
  interval; 2 adds how far the effect's tick counter is towards its next
  step (`lead`), a constant speed (game frames are then exact only where
  the effect steps). In between, the fragment stage wraps texels that run
  past the region back by the period, as the game's scroll wraps.
- Ray tracing: `scene_capture.c` keeps the animated triangles' UVs as
  drawn (`pc_scene_uv_refresh`, `pc_scene_uv_serial`); the path tracer
  rewrites those triangles' UV words in its triangle records in command
  order (Vulkan `vk_rt.inc` `vrt_room_uvs`: `vkCmdUpdateBuffer`; GL
  `ptgi.c` `update_room_uvs`: `glBufferSubData`), so hits, glows and cut-out
  tests read the current texels (34 records per step in the save room).
- Editor: Details > Inspector shows the probed polygon's effect (kind,
  game slot, window, step and period, running, the game's and the drawn
  offset) with per-room overrides, speed multiplier and freeze, saved with
  the room's lights file (`uv_effect <index> <speed> <freeze>`). A speed
  override runs the effect on its own phase advanced by the game's motion
  times the speed. RPC `uv_effects` lists the effects (and sets
  overrides); its `drawn` trace is the first effect's offset per draw.
- Fallback: an effect whose polygon's bytes are not base + one offset (game
  code rewriting them otherwise, a polygon outside the room's groups, a
  code byte the game reads as the other polygon kind) is not modelled; and
  anything else rewriting a geometry block is still followed: each mesh
  compares its block with a copy once per frame (the modelled effects' UV
  bytes left out) and re-decodes it on a change (`mesh_follow`,
  `render_mesh_update`). `ROOD_UV_EFFECTS=0` (RPC `graphics uv_effects`)
  models nothing, the behaviour before.

Cost in the save room (64-bit, per game frame, CPU): the previous FNV hash
plus re-decoding of two meshes every step 7.3 us (and a vertex buffer
reallocation per step); now 1.7 us for the block comparisons (`memcmp`
against the copy instead of hashing) plus 1.2 us for the effects (18) and
the scene's UVs. Tables are 48 bytes per effect per draw.

Test: `pc/tests/texture_animation_smoke.py` (registered in `run.py` when
there is a save): three runs of the same session from the save room
(animated materials; `ROOD_UV_EFFECTS=0`; the packet path), frames matched
by the effects' offsets and the camera. The ring of the save point is
pixel-identical in all three (16 matched frames on 64-bit GL and Vulkan;
the 32-bit build does not replay a session exactly, so it skips the
comparison with a note), no mesh is rewritten, freeze and speed work, the
interpolation modes behave as above across a wrap, and the path tracer's
records follow. Every type 2 effect on the disc is a save point's (step
0, 1 every 2 ticks); no room uses type 0, so atlas frames are modelled but
untested.

## Characters, weapons and props (`actor_native.c`)

Every skinned model the battle code draws goes through `func_800B3A68`
(`actor_vertices.c`: vertex groups through the per-bone GTE matrices) and
then one of four packet builders, all of which call into `actor_native.c`
after writing their packets:

- `func_800B3CF0` (`weapon_packets.c`): flat-lit textured records, one
  colour per model (the room light at the actor). Characters, their
  weapons and shields, and the second copy some actors get (below).
- `func_800B4594` (`actor_packets.c`): per-vertex coloured records
  (object flag 8 actors, the props of `BATTLE/49268.c`: chests, crates and
  other placed objects).
- `func_800B516C` (`weapon_packets.c`) and `func_800B5C30`
  (`actor_packets.c`): the same records walked in material groups (up to
  eight counts at the model per section, then the remainder; a group whose
  bit is set in object byte 0x64 takes the alternate CLUT, +0x480), and,
  when object byte 9 selects a mode, the whole model semi-transparent
  (1-6 `B-F`, 7 and up `B+F`; the coloured variant then tints every vertex
  with the object colour). The game uses them for actors in special states
  (fading out, the battle camera transition `D_800E8FC0`, alternate CLUTs)
  and for props in states 4-8 (except 6) and 12 and up. The mesh records a
  CLUT offset per polygon (0 or 0x480; the group walk is reproduced,
  including `func_800B516C` skipping an empty section's eight counts and
  `func_800B5C30` not), and is keyed by the mask and the group counts too;
  a selected mode becomes the instance's blend.

Semi-transparent polygons (record flag 16 with its blend bits, a coloured
record's own code bit, every polygon of a blended model) are native with
`pc_native_blend`: each draw computes their buckets from the depths
`func_800B3A68` produced (the builders' rules: AVSZ3 or the quad average,
the bias, the 0x7c0 limit, the builders' NCLIP culling of the projected
positions), and links one translucent marker per bucket at that bucket
after the model's packets (within a bucket the later record first, as the
builder's head links leave them). The list's own marker goes at the
farthest bucket any of the draw's polygons uses (at least the largest
vertex depth / 4), so it is reached first. Additive polygons of a drawn
list become PTGI emitters (as the packet path's additive triangles do).
With `pc_native_blend` 0, translucent polygons stay packets and a wholly
blended model is not submitted at all.

- **Meshes**, once per model: the records decoded as the builders read them
  (vertex offsets, UVs, flags, bias, double-sided and split records), a
  palette index per vertex from the vertex groups. Cached by the model's
  address and a hash of its groups, vertices and records (a reloaded model
  buffer gets a new mesh, never a stale one); meshes not drawn for 120
  frames are destroyed. Models whose mixed records disagree with their own
  sizes stay packets (none seen).
- **Palettes**, per draw: each bone matrix the GTE used becomes
  camera^-1 x matrix (world space, the room's convention); the same draw's
  palette from the previous frame (keyed by object, model, matrices and the
  draw's occurrence in the frame) goes along for interpolation.
- **Order**: the PS1 paints a model's polygons in ordering-table order, and
  the models are built for that: shells that intersect (hair over a scalp,
  a coat over a body) and, for some actors, a second copy of the whole
  model drawn right after the first (same object and matrices, its own
  colour, a couple of units away, the ordering-table pointer shifted by
  `unk6F4`). The two copies interleave polygon by polygon in the table; on
  Ashley the dark copy wins most of the head and coat. Per-pixel depth
  between the copies showed the light copy instead (mean difference 0.4,
  a lighter wedge on the head and panels on the coat). So each draw
  computes every polygon's ordering-table index from the depths
  `func_800B3A68` just produced (with the bucket rules and the table
  pointer shift), a repeated draw of the same object joins the previous
  draw's list, and the list paints in the joint order (`render_list_order`;
  ties: the later packet first, as linking at a bucket's head does). Depth
  against the room and other models stays exact. `pc_native_actor_order` 0
  depth-tests a model against itself instead (for comparison).
- **Placement**: one marker per draw at the model's farthest bucket (largest
  vertex SZ / 4, or a biased polygon's bucket beyond it), before all of its
  packets; a list two draws joined has two markers and the first one reached
  draws it (`gpu_hw.c` skips repeats).
- **Packets**: still built (the fallback when the list cannot draw); their
  range is reported with the list (`pc_hw_object_packets`), and once the
  list has drawn, the hardware path skips the range's opaque triangles, and
  with `pc_native_actors` 2 the CPU rasterizer skips them too. If the list
  could not draw (`pc_native_room` 0), the packets draw as before, on both.
- **Shadow casters**: a drawn object list's polygons (both faces, every
  polygon, world space, with their surfaces) replace the casters the packet
  path reconstructed from projected packets (`render_list_triangles`), so
  PTGI shadows and the editor view see whole models, including faces turned
  away from the camera.

`pc_native_actors` (`ROOD_NATIVE_ACTORS`, RPC `graphics native_actors`,
editor Graphics checkbox "Native characters" with "No reference", settings
key `native_actors`; `actor_order` / "PS1 order within a model" /
`native_actor_order` for the order): 0 legacy packets, 1 native with the
packets still rasterized by the CPU, 2 (default) native without that
rasterization (also for drop shadows and particles; see "Without reference
packets").
`render_stats` counts object lists, instances and triangles, object packet
triangles drawn and left to native lists, meshes built in the frame and
blended instances in replays.

Known differences and leftovers:

- Two copies of a blended model (dark and light) draw their translucent
  ranges per copy and per bucket; they alternate in the table, so their
  markers do not merge (one draw per bucket and copy: about +1 ms per frame
  with every actor of the first room forced blended).

- The culled-record quirk of `func_800B4594` (after a culled mixed record
  the next record's flags come from the wrong word) is not reproduced: the
  native mesh uses each record's own flags.
- Two-copy models cast two nearly coincident shadows (both copies are
  casters).
- `pc_native_room` 0 keeps characters on packets. A frame without a room
  list (menus that freeze the world and show a model) draws its object lists
  over empty surfaces (above); its frame interpolation takes the object
  lists' camera (`recording_camera`; lists with different cameras are not
  interpolated).

## Drop shadows and particles (phase 2 step 4, `effect_native.c`)

Measured first: with the room, characters and translucent polygons native,
what still reached the hardware path as reconstructed 3D packets
(`legacy_other_triangles`, attributed to the code that issued the GTE
projections with `ROOD_OTHER_DEBUG=1`, which logs per 300 frames the caller
of each such triangle's first vertex, its GPU code and blend mode) was, in
the first room 72 per frame, and over a 20,000-vsync soak from the save
card (four rooms) 99% drop shadows (`pc_actor_shadow`: 327,423 triangles),
then particles (`func_800D6E44`: 3,166), then 19 triangles of other effects
(an untextured quad through `RotTransPers`, a Gouraud quad). Particles are
mostly not counted there at all: their corners are placed in screen space,
so the projection log rarely makes them 3D and they were drawn as 2D. So
drop shadows and `func_800D6E44` particles are native; the other effect
renderers stay packets (below).

Both use an **effects list** (`render_rules.effects`, `RENDER_EFFECTS_*`):
a list of translucent entries only. Every polygon of it (semi-transparent or
not) is drawn at translucent markers linked into the ordering table where
the game links the packets, so they keep the PS1's order against
everything else; the list itself has no marker. A marker draws once the
room list of that ordering table drew (else the packets draw), hidden behind
the native surfaces before it with the slack the packet path gives decals
(`z > surface * 1.02 + 8`); opaque texels draw unlit (G-buffer normal alpha
0) with their depth, semi-transparent texels blend with the PS1 mode. The
first marker drawn covers the draw's packets (`pc_hw_effect_packets`), which
the hardware path then skips. Additive polygons of a drawn effects list
become PTGI emitters (`render_list_translucent_triangles`, sprites with
their world-space corners). Frame interpolation replays the markers with
the interpolated camera (and the shadows' blended transforms).

- **Drop shadows** (`func_800B4B14`, `actor_shadow.c` calls
  `pc_shadow_native` after writing its packets): the disc (`D_800E9A68`,
  18 vertices in the XZ plane) and its 24 faces (`D_800E9AD4`) are one mesh
  per light value (the rings' colours: light, light / 2, black; POLY_G3,
  B-F), cached; the instance's world transform is camera^-1 x the matrix
  the game built (camera x terrain slope x actor yaw x size), with the
  actor's transform from the frame before for interpolation. The triangles
  the game kept (its NCLIP and bucket tests) are decided entries in the
  PS1's order (far buckets first, later triangle first in a bucket), one
  marker per bucket. The drop-shadow modes are those of the packets
  (`pc_drop_shadow_mode`: Auto turns them off while traced shadows cover
  characters, contact-only makes them small and faint), since the native
  draw is built from what the game computed.
- **Moving platforms' shadows** (`func_8008CEB0`, `room_shadows.inc` calls
  `pc_platform_shadow_native` after writing each square's packets): a room
  with moving platforms (MPD section 12: lifts, the floating blocks with a
  magic circle in Rue Vermillion) draws a flat square on the floor under
  each, POLY_F4 after a B-F draw mode, its size and grey from the
  platform's height above the floor (`shade`). Native: one quad mesh per
  shade, an instance at the square's centre (the frame before's centre for
  interpolation), one decided entry and marker per square at its bucket, in
  the same effects list kind as drop shadows (`pc_native_shadows`,
  `shadow_markers`). Before (2026-10-15), with the reconstruction retired,
  the packet reached the hardware path as 2D: subtractive screen furniture
  that replaced the surface under it, a hard black square on water. It is
  still the PS1's hard-edged square, now subtracting from the lit picture
  like the packet path with the reconstruction (`platform_shadow_smoke`).
- **Particles** (`func_800D6E44`, `effect_particles.c` reports each node):
  a node's centre goes through the view (or a parent's) matrix and its
  textured quads (POLY_FT4) are placed around the projected centre in
  screen space, sized by projection / depth. Native: `RENDER_POLY_SPRITE`
  polygons in a transient mesh per call (`render_mesh_transient`: destroyed
  12 frames later), each with the centre's world position, the corners'
  screen offsets and the depth they were measured at; the geometry shader
  projects the centre as the GTE does, scales the offsets by
  measured depth / depth (X narrowed in widescreen like the rest of the
  projected picture) and tests the sprite at its nearest point (centre
  depth minus its screen radius in view units), so a sprite is hidden only
  by surfaces in front of all of it. One marker per node at the node's
  bucket, the node's last quad first. Nodes on fixed layers (depth modes
  5-8: the far end of the table, or in front of all of it) stay packets.
  Visible difference: smoke drifting outside the first room's window used
  to paint over the window sill in front of it (2D packets ignore depth);
  natively it is hidden there.

Switches: `pc_native_shadows` (`ROOD_NATIVE_SHADOWS`, RPC `graphics
native_shadows`, editor "Native drop shadows", settings `native_shadows`)
and `pc_native_effects` (`ROOD_NATIVE_EFFECTS`, `native_effects`, "Native
particles", `native_effects`), both on by default. `render_stats`:
`shadow_markers`/`shadow_polygons`, `effect_markers`/`effect_polygons`,
`skipped_effect_triangles` (packet triangles left to them) and
`legacy_effect_triangles` (drawn from packets).

What stays packets, and why: the effect plugins in `src/EFFECT` (ribbons,
trails, rings, grids, surfaces; 314 renderers, shared through a dozen
headers) build their geometry in screen space from projected control points
(offsets perpendicular to the projected direction, widths in pixels, random
jitter per frame), so there is no world-space mesh to submit without
rewriting each renderer's maths; none of them appeared in the tested rooms
or the soak beyond those 19 triangles. 2D UI, menus, the HUD and movies
keep their packet path by design. With the reconstruction retired (below)
such packets draw as 2D in their ordering-table place (as the PS1 paints
them), not depth-tested against the native room.

## Without reference packets (`pc_native_room` / `pc_native_actors` 2)

Mode 2 of both switches (the defaults) stops the CPU from drawing what
native draws cover: the room's opaque packets are not built at all, and
the CPU rasterizer skips covered room, character, drop-shadow and particle
triangles (`pc_hw_triangle` returns 1 for them). Native VRAM then lacks
them, so `gpu_hw.c` remembers each drawing area a native draw covered as
**stale**, and before the CPU reads VRAM there it copies the scaled target
back (`pc_hw_sync`: a nearest blit to 1x, `glReadPixels`, 5 bits per
channel as the CPU rasterizer quantizes, the mask bit from alpha):

- StoreImage (`pc_vram_transfer` reads: text boxes, menu backups, glyphs),
- primitives that sample texels or a CLUT in a stale area (exact UV bounds,
  `pc_hw_sync_texture`: screen effects that read the frame back),
- RPC `screenshot` and the checkpoints that write the display.

VRAM-to-VRAM copies from a stale area mark their destination stale
instead of reading back (the game copies two pixels from the screen's
corner every frame; a readback there cost a GPU stall per frame).
`render_stats` reports `vram_syncs` (copies back since startup) and
`stale_rects`; `ROOD_SYNC_DEBUG=1` logs each copy and StoreImage caller.
In the first room nothing reads back; in the soak a screen effect samples
rows of the frame now and then (about one copy per 12 vsyncs), and menus
copy back a few times when they open. The software presenter is only used
without the hardware rasterizer, where nothing is native and the packets
draw as before. The per-frame `loading.ppm` debug image is written from
native VRAM without a copy (it lacks native content in mode 2).

## Retiring the reconstruction (`pc_reconstruct`)

With every native switch on, the reconstruction of 3D from packets serves
nothing: the GTE projection log (recording every projection and looking up
each packet vertex), the room depth records (`pc_hw_room_depths`), caster
and emitter capture from projected packets, and nearest-triangle matching
for frame interpolation (which only applies to packets with world
positions). `pc_reconstruct` (`ROOD_RECONSTRUCT`, RPC `graphics
reconstruct`, editor "Reconstruct 3D from packets", settings
`reconstruct`; default 0) turns them off whenever every native switch is on
(`pc_gte_log` 0 from the next ordering table: nothing recorded, no lookups,
no room depths; packets that still reach the hardware path have no depth,
so no casters, emitters or matching come from them). With 1, or any native
switch off, it all runs as before (kept for one release). Room packets are
now covered by their native list whether or not they were classified 3D
(without the log every packet is 2D to the hardware path).

## The editor's free camera

The live free camera drives the game's own camera, so the game builds the
frame from it. Where the renderer owns the geometry it no longer applies
the game camera's limits: room lists drop the ordering-table range (the
near bucket the game animates, the table's far end) and object lists the
0x7c0 limit, so flying close to walls or far away (13,000 units above the
first room) keeps the room and characters; the renderer clips at its own
near plane. Still limited by the game: which groups a room script hides,
which actors and effects the game draws at all, the semi-transparent room
polygons and model polygons the game decides (its back-face and bucket
tests, from the free camera), drop shadows (the game's culling of their
triangles) and everything that stays packets.

### Seeing into rooms (`room_editor.h`)

Vagrant Story builds its rooms for a camera inside them looking at Ashley,
and hides what would stand between. From the free camera that used to cover
the room: in the save room (zone 9 room 17) from the user's high pose, a
black slab over the left of the view and black wall tops. Dumping the
room's groups (RPC `room_groups`, `scene_dump`) shows what they are:

- **Black shells**: one group per wall side (save room groups 25-28,
  32-54 polygons each) whose every vertex colour is 0, with flag 0x8000
  (the alternate set) and an ordering-table bias of -12: each wall's
  thickness, top and outside, drawn black over whatever lies beyond the
  room. Their facing masks equal their walls'. The game camera only ever
  sees them as the void around the room; from above or outside they are
  front-facing, opaque and nearest, the black occluders of the report.
- **Facing-masked walls**: wall groups whose flags' low byte has one bit per
  45 degrees of camera yaw; `scene_submit.inc` sets the game camera's
  left/front/right bits and `func_8009723C` draws only the groups that have
  all three, so the walls between the camera and the room are left out
  (the game's "cutaway walls"). The free camera kept every group.

  The masks are per wall orientation, not per distance: in rooms that are
  not a simple box a wall can be cut while it stands beside Ashley, and she
  then walks into a wall that is not drawn (zone 9 room 13 at yaw 0xE00:
  groups 6, 7, 9, 12-21, 50, 51, 56-59 cut, the wall right of the arched
  door among them). The original does the same: the mask in
  `scene_submit.inc` agrees with `func_8008AC78`'s assembly (the yaw word at
  0x1F800058), the group test in `room_geometry.c` with `func_8009723C` in
  `2EA3C.s`, and both `pc_native_room` paths draw the groups that one test
  keeps. RPC `room_groups` reports the mask the latest
  frame tested (`facing`) and which groups it drew (`drawn`);
  `pc/tests/facing_cutaway_smoke.py` checks the native room against the
  packet path at all eight yaws and through L1/R1 turns. As an option,
  `gameplay_walls smart` draws such walls while they stand clear of the
  view ("Smart walls" below).

While the free camera flies (gameplay never), three switches, each on by
default, in the editor toolbar ("See into rooms"), the settings file and
RPC `graphics`:

| Switch | Effect |
| --- | --- |
| `editor_game_cutaway` ("Cutaway walls") | `func_8009723C` tests the facing masks with the bits the game would set for the free camera's own yaw (`pc_room_editor_facing`, from the view matrix; straight down: every group) |
| `editor_hide_masks` ("Hide black shells") | groups whose polygons all have vertex colour 0 (and no raw texture) are left out (`pc_room_editor_hidden`) |
| `editor_cutaway` ("Cut to focus", key C; `editor_cutaway_radius`, default 320) | while room surfaces hide a focus point (the selected light or glow, else the middle of Ashley; RPC `editor_view {focus}` fixes one) from the eye, room polygons whose plane separates the two are drawn as **ghosts** inside the cone from the eye to the focus (its radius at the focus: a circle of constant size on screen): `editor_cutaway_opacity` (default 0.3) of their pixels and their polygon edges stay, out to 3/4 of the radius, blending to solid at the radius (4x4 ordered dither); opacity 0 cuts a hole as before |
| `editor_cutaway_auto` ("Only when the focus is hidden", default on) | off: always cut what stands between the eye and the focus, hidden or not |

**Ghosts, not holes (2026-10-11).** The cut used to discard every pixel in
the cone. Where nothing stood behind the cut surfaces (a light beyond a
wall, a fixture in a niche, a focus outside the room) that opened a window
onto the black void around the room, and editing lights there felt like
polygons "going black". A cut surface is now drawn as a screen-door ghost:
an ordered dither keeps `editor_cutaway_opacity` of its pixels and every
pixel within a pixel of the polygon's real edges (the geometry stage passes
barycentrics and which of a triangle's edges are real, a quad's diagonal
not), so the room keeps its shape and the focus shows through. A dither
rather than blending keeps the G-buffer exact: depth, normals and lighting
of the pixels drawn are the wall's, the pixels left out show what is
behind, lit as it is, and nothing about the deferred lighting, the path
tracer or the probes changes. Ghost pixels carry G-buffer normal alpha
0.55 (a surface, lit as moving geometry); picking (`pc_hw_pick`) looks
through them to the nearest pixel the dither left to what is behind.

**Only when hidden.** `editor_view.cpp` casts five rays (to the focus and
to four points 12 units around it) against the room triangles the view
draws (shown groups, front faces, opaque), ignoring the last 24 units (or
5%) before the focus, its own fixture. The focus counts as hidden when
three rays are blocked; it becomes hidden after 2 game frames in a row and
visible again after 10, and the cut fades in and out over 5 frames
(`render_cutaway`'s `amount` scales the ghost's opacity towards 1). RPC
`editor_view` replies `hidden` and `amount`. The viewport draws the focus
as a ring with a cross: solid and labelled while it ghosts what hides it,
dashed while it is in plain view.

The editor's viewport overlays (`editor_overlays.cpp`: room edges and points,
snap targets) follow the same rules on the CPU: with the free camera live a
triangle shows when its group is not hidden, not cut away by the facing mask
and not a hidden black shell (`pc_room_editor_groups`); with the game camera,
when the game's own facing mask keeps it. Inside the cut to focus, points and
edge pieces on polygons that separate the eye from the focus are left out and
those polygons hide nothing there (the same plane and cone tests as below).

The cutaway is per pixel in the native list shaders (render.c's GL strings,
`native.geom`/`native.frag` on Vulkan): the geometry stage tests the
polygon's plane once (floors under the focus and walls behind it never
separate the two, so they stay), the fragment stage keeps the ghost's share
of the pixels inside the cone (`cut_scale.w`, 1 when nothing is cut) and
the polygon's edges. Positions are compared in view space with the rows' scales divided
out (`cut_scale`), so the cone is round in world units. Room lists only
(room and layered blocks; not characters, effects or packets: with
`pc_native_room` 0 only the two group switches apply). Replays of
interpolated frames use the focus of the newest frame.

The Outliner's "Room groups" lists every group (polygon count, flags,
black shell, cut by the facing mask, hidden by the room) with a visibility
checkbox; RPC `room_groups {hide: [...], show: [...], show_all}` does the
same. Hidden groups reset when the room changes.

The editor reads the groups (the Outliner, `room_groups`, and its undo
snapshot every overlay frame) only from the game's loaded room block
(`pc_scene_room_block`, scene_capture.c): `vs_battle_roomData` reports a
room loaded and its geometry section is the block last captured. A room
change frees the block (func_8008ABF0) and the disc reads the next room,
possibly into the same memory; reading the freed block then found a group
count that passed and garbage geometry offsets, and crashed outside guest
RAM. While a room loads the editor has no groups (`room_groups` reports
`"room": "loading"`, else `"ready"`), and every group's geometry offset
and polygon counts are checked against the section's size
(`header.geometrySectionLen`) before they are read
(`pc_room_group_geometry`; the capture leaves out a group outside the
block and logs it). Test: `pc/tests/editor_roomload_smoke.py` steps every
vsync of warps across zones and a walked door with the editor open.

None of this touches the ray-traced scene: `scene_capture.c` keeps every
group, so the hidden walls and shells still cast shadows and block light
(and haze stays bounded by the room). The G-buffer is the picture's, so a
cut wall shows the surfaces behind it, lit and shadowed as they are. The haze and ground fog also leave the void alone: present pixels that
show a black shell or nothing keep none past where their ray leaves the
room (Vulkan_Renderer.md "The room's interior and the void"), and with
`haze_interior` the shells count for nothing in the fog's floor grid.

Test: `pc/tests/editor_cutaway_smoke.py` (registered in `run.py` when a
save card exists): Continue into the save room; the game camera draws the
same groups, triangles and picture with every switch on and off; the user's
pose drops from 42 to 30 groups and the black slab's area goes from
brightness 1.8 to 53; from a low pose behind the props the cutaway alone
changes 38% of the view's centre (a ghost keeps 13% of it as it was, a
hole 0%) and leaves the wall away from the line of sight unchanged; from
inside the room with the focus beyond the far wall the hole showed the void
(centre brightness 0.0) and the ghost the wall (10.7); with Ashley in plain
view nothing is cut (the picture as with the cut off), and the cut holds for
a moment when she comes into view, then fades; hiding a group in
`room_groups` drops one instance and `show_all` restores it. Golden frames and the comparison tools set the
three switches off (their references show the free camera with every
group).

### Smart walls (`gameplay_walls`, 2026-10-11)

The original's cut is per orientation, so in L- and cross-shaped rooms it
removes walls that block nothing (black void behind a crate stack, a wall
Ashley runs into). `gameplay_walls smart` (settings key, RPC `graphics`,
Graphics > Cutaway walls; default `original`) keeps the game's mask for the
groups it keeps and decides again for the groups it cuts
(`gameplay_walls.c`, called from `func_8009723C` for the current room under
the game camera):

- Each frame the room's triangles (the captured scene, world space) are
  rasterised on the CPU into an 80 x 60 depth grid over the 320 x 240
  screen, as the game camera sees them: opaque, front faces as the PS1
  culls them (and two-sided polygons). The groups the mask keeps make the
  *base* picture; each cut group (not a black shell, not a camera-facing
  plane, not hidden by the room's flags) is then tested against it.
- A cut group is **in the way** when a front face of it lies, nearer than
  Ashley, in her screen region: a capsule from her feet to her head widened
  by `gameplay_walls_radius` (64) plus her half width; in battle with a
  target chosen (the point the battle camera follows) a disc around it too.
  It is also in the way when 6 or more of its cells are seen (over the base
  picture or over void) more than 96 units nearer than Ashley: in front of
  the play area (a near wall's face, trims, columns). Seen over void at or
  beyond the play area is what drawing it is for; behind the base picture it
  hides nothing; back faces never show (the PS1 culls them), so a revealed
  wall never shows its outside.
- Hysteresis: in the way from the first frame, clear again after 10 frames
  outside a region 1.3 times as wide. The drawn share fades out over 4 game
  frames and in over 7 as an ordered dither (`render_instance.cut`, with
  `cut_previous` blended by frame interpolation between game frames, so the
  fade is as smooth as the presented frame rate). Groups the mask keeps
  stay solid (they fade in only after a smart hide, e.g. while the camera
  turns).
- In the way: left out, or with `gameplay_walls_ghost` drawn as ghosts
  (`gameplay_walls_opacity` of their pixels and their polygon edges, the
  editor's ghost; G-buffer alpha 0.55).
- Black shells always stay hidden. While an event script runs (`D_800F4C34`,
  cutscenes) and while the editor's free camera flies the mask decides
  alone, and the next smart frame starts without fades.
- The packet path (`native_room 0`) draws the same groups, without the
  dithered fades. The ray-traced scene, probes and shadows are unchanged
  (they always held every group).

RPC `room_groups` adds per group `shown` (drawn share), `occluding` and
`cells` (this frame's grid cells: in Ashley's region nearer than her, seen in
front of the play area, over void, all front-facing). In zone 9 room 13 at
yaw 0xE00 (Ashley where the Room Browser's warp puts her) the mask cuts 19
groups; smart draws 14 of them (most face away from the camera and show
nothing; group 19, a wall piece right of the arched door, shows) and keeps
13, 14 and 57, which stand between the camera and Ashley, hidden.
`facing_cutaway_smoke.py` checks it, and at each save-room yaw that no
black shell is drawn, that every kept group still is and that both paths
draw the same groups.

## Verification (2026-10-10)

- Golden frames (`pc/tests/golden_frames.py`, references from the packet
  path): modes 1 and 2 match within 0.00-0.05 mean difference per frame;
  the depth view differs by 0.50 (exact per-pixel depth instead of affine,
  stale depth from earlier frames now reads as far).
- `pc/tests/native_room_compare.py` (two deterministic New Game runs, legacy
  vs native, game camera and free-camera poses, depth views, interpolated
  frames; `--widescreen`, `--graphics`) and `pc/tests/native_room_tour.py`
  (rooms of a save, or the opening with `--new-game`) write side-by-side
  frames for looking.
- Full suite (`pc/tests/run.py`, which passes `ROOD_NATIVE_ROOM` through):
  17/17 in mode 1 and in mode 0.
- Known small differences: the present pass (lighting/haze filters that
  look at depth) can draw a faint line along a few depth edges where the
  packet path's affine depth differed; a handful of edge pixels differ in
  the raw target (about 500 of 8 million at 4x).

### Characters (2026-10-10)

- `pc/tests/native_actors_compare.py` (two deterministic New Game runs,
  native_actors 0 and 1 with the native room in both; game camera, free
  camera poses on the characters, depth views, interpolated frames) writes
  side-by-side frames. First room at 1500 vsyncs: game camera mean
  difference 0.04 (0.00% strong pixels), free camera on the characters
  0.07-0.10 (0.08-0.12%), depth views 0.00-0.01, interpolated frames (the
  replays blend bone palettes: 119 blended instances over the run) 0.06-0.08.
  Captures at later times can drift by a frame between the two runs (they
  wait for a settled picture while characters animate; interpolation is
  time-based), and then show different poses, not rendering differences.
- Golden frames (references from the packet path): pass with characters
  native (room-game-camera 0.04, the rest unchanged).
- Full suite (`pc/tests/run.py -j 2`, which passes `ROOD_NATIVE_ACTORS`
  through): 17/17 with 1 (default) and with 0.
- PTGI with a test light: the shadow visibility of the first room matches
  the packet path's casters (native casters add the faces turned away from
  the camera).

### Translucent polygons and room blocks (2026-10-10)

- `pc/tests/native_room_compare.py --blend` (native room in both runs,
  native_blend 0 vs 1): the first room's stained glass and window light (40
  of its 1089 polygons are semi-transparent) match the packet path to
  0.00 mean difference from the game camera and most poses; the high pose
  looking at the window 0.16-0.33 (0.2-0.4% strong pixels, all along the
  pane borders: the packets' sub-pixel positions put the pane edges a
  fraction of a pixel off the native frame's), depth views 0.00-0.06,
  interpolated frames 0.08-0.09. `render_stats`: every semi-transparent room
  and character triangle left to native draws (`legacy_translucent_triangles`
  0).
- `pc/tests/native_actors_compare.py --blend --fade` (blended characters:
  the forced battle-camera-transition state sends every actor through
  `func_800B516C`, object byte 9 set to `B-F` then `B+F` and animated by the
  game as a fade): 0.00-0.09 mean difference, up to 0.15% strong pixels,
  interpolated frames 0.00-0.10; 381 translucent polygons in 76 markers per
  frame in that scene.
- Room blocks: in the save room (zone 9 room 17), forcing its section 9
  entry on draws `func_8008E19C`'s quad as a layered list; native_objects 0
  and 1 give identical frames. The save room's 21 translucent polygons (the
  save point's column) are native, 0.00 difference in the high view
  (`native_room_tour.py`; the other views differ by the column's animation
  between the two captures).
- Golden frames pass with the new defaults (room-high 0.28 mean, 0.32%
  strong: the window panes above), the full suite passes 17/17 with
  `ROOD_NATIVE_BLEND`/`ROOD_NATIVE_OBJECTS` 1 (default) and 0.
- Frame time (fast mode, first room, loaded machine): 7.1 ms per game frame
  with native_blend 0 and 1 alike; with every actor forced blended 8.8-9.2
  ms (packets) vs 9.8-10.1 ms (native).
- 64-bit pass: no new diagnostics (`render.c`, `room_native.c`, `gpu_hw.c`
  compile cleanly; `actor_native.c`'s are the existing ones of
  `psx_load`/`psx_store` in a unit compiled without `ROOD_PC_LP64`).

### Drop shadows, particles, no reference packets, reconstruction retired (2026-10-10)

- Drop shadows: from the save card with Ashley on the cellar floor and the
  original shadows forced (`graphics drop_shadows 1`), the native disc
  matches the packets to within 1 of 255 in 150 pixels of the raw target
  (gpu_dump) and 37 of the presented frame; the first room's characters
  (72 shadow triangles per frame) give identical frames from the game
  camera and every free-camera pose, interpolated frames 0.01 mean
  (`native_actors_compare.py --shadows`, which forces the original shadows;
  `native_room_tour.py --switch native_shadows --graphics '{"drop_shadows":1}'`
  adds a view of Ashley's feet).
- Particles: the first room's burning city outside the window (93 sprites
  in about 90 markers per frame by 2400 vsyncs after New Game):
  `native_actors_compare.py --effects --times 2400` gives 0.13 mean (0.16%
  strong pixels) from the game camera, 0.08 from the high pose, 0.00
  elsewhere, interpolated frames 0.13-0.15; the difference is the smoke the
  2D packets painted over the window sill, now hidden behind it.
- No spell or break art could be triggered over RPC within the time given
  (the battle menu needs enemies and menu navigation by blind input
  drifts); `ROOD_OTHER_DEBUG=1` attributes whatever a played spell still
  sends as 3D packets.
- `legacy_other_triangles`: 0 in the first room and over a 20,000-vsync
  soak from the save card (zone 9 rooms 17 and 12) with the defaults; the
  soak's StoreImage readers (menus) and frame-sampling primitives copy
  back as described, no findings.
- Golden frames (references regenerated this morning with native rooms,
  characters and translucency): 0.00 mean for every frame with the new
  defaults (room and characters mode 2, shadows and particles native,
  reconstruction retired), and with mode 1 and the reconstruction on.
- Full suite (`run.py -j 2`): 17/17 with the defaults (32-bit, and
  `--arch 64` after merging main's LP64 link), 17/17 with
  `ROOD_NATIVE_ROOM=1 ROOD_NATIVE_ACTORS=1 ROOD_NATIVE_SHADOWS=0
  ROOD_NATIVE_EFFECTS=0 ROOD_RECONSTRUCT=1` (step 3's renderer), 17/17 with
  every native switch 0.
- Frame time (fast mode, first room, 1200 vsyncs three times, loaded
  machine, wall time per game frame including the presentation of every
  vsync): step 3's defaults 8.4-10.2 ms; mode 1 with shadows/particles
  native 8.0-11.4 ms; the new defaults 3.8-5.4 ms (32-bit), 2.7-5.2 ms
  (64-bit). The reference rasterization of room and characters was about
  half the frame.
- 64-bit: the new and changed units compile without diagnostics
  (`build_boot.py --arch 64`: 213/213 units, 0 errors).

## Animation in-betweens (2026-10-10)

How the game animates, from the decompilation (BATTLE.PRG): every actor
with the animating flag is advanced once per game frame by `func_800AEF94`
→ `func_800AF6E8`/`func_800AFA28`, which runs `unk5B2` animation ticks
(1, or 2 with the SEQ "double speed" flag, doubled again when the game runs
at 4 vsyncs per frame, `vs_gametime_tickspeed` 4) and then decodes the SEQ
tracks for those ticks (`func_800AFDE8`). A SEQ track is a stream of
segments, each a constant per-tick velocity for 1-287 ticks, of 13-bit
Euler angles per bone (Rz·Ry·Rx), plus root offset and per-bone scale
tracks; the pose is piecewise linear in Euler space and changes once per
tick. There is no fractional time anywhere: at 30 fps one tick per frame,
at 15 fps two ticks per frame with the middle one never drawn. Changing
animation crossfades `unk704` toward the new pose by k/n of the remaining
distance per frame (`func_800B147C`). `func_800B28A8` then composes the bone
matrices: bone i at parent × (-length of the parent, 0, 0), rotation
parent (unscaled) × Euler, column scales accumulated separately, plus
special bones (head look, spinning, camera-facing billboards). Weapons and
shields are drawn from the holder's bone matrix (`func_800B217C`); props
(`D_800F45E0`) have one matrix tweened per frame; event scenes use the same
actors and player. So every character animates at the game's frame rate
and frame interpolation between consecutive game frames is the right model,
with the exceptions found by tracing (below).

**Tracing** (`render.c` `render_anim_trace`, RPC `anim_trace {path, count}`,
`pc/tools/anim_trace.py`): for each presented image, every object instance
of the lists shown (`pc_hw_present_lists`: characters, weapons, props) with
its game frame, blend t, the vsync fraction, whether it had a previous
palette, whether the jump test refused it, the carrier it was attached to,
and three palettes (as drawn, the game frame's, the previous one's). The
tool keys instances by mesh and occurrence and reports per instance: pose
change per game frame (bones relative to the root) and root motion,
distinct drawn poses per second ("fps"), short holds (pose unchanged 1-3
game frames between changes), cadence (game frame intervals), stalls (an
image repeating the one before although the renderer had a new frame or t;
a frame the game itself repeats is a hitch), jump-test false positives and
negatives, bone shrink mid-blend, unmatched instances (no previous although
drawn the frame before; appearing instances are counted apart) and
attachment drift (a carried root leaving its place on the carrier's bone
mid-blend).

**Findings** (fast mode, 26 segments, ~6,500 images: New Game opening
scenes at vsync 2490-13000 and the prologue fight; from the save card the
save room's idle, walk, turns, battle mode with attacks and longer walks;
before = linear blending):

| What | Where | Before | After |
| --- | --- | --- | --- |
| Fast rotations shrink bones halfway (linear matrix blending) | Ashley's attack swing (bone 18 turns 95.7° in one game frame), drawing the weapon (93.6°), cutscene gestures | up to 31% (Ashley, weapon), 51% (cutscene character, opening 11500), walking 4-9%, turning 9% | 0.0% |
| Poses held 1 game frame between changes (idle breathing in event scenes: the whole pose changes every other frame) | opening at 4000 (two characters, both copies), 11500, 13000; a 2-bone prop in the save room | 29-34 distinct poses/s | 56-59 |
| Weapon in the hand mid-swing | Ashley's weapon on bone 18 | 1.0-1.7 units (consistent because everything was linear) | 1.1-2.1 units (carried by the slerped hand) |
| Unmatched instances | all segments | 0 | 0 |
| Jump test | all segments | no false positives; root jumps of ~315 units only at camera cuts, which the camera test already shows unblended | same |
| Game cadence | gameplay and event scenes | 2 vsyncs per frame, a rare 1- or 3-vsync frame (game hitches, shown as is); 4 vsyncs only in scenes without characters traced | same |

Animating characters already showed a new pose every image (60 per
second) with interpolation on, so the "lower frame rate" look came from
the in-betweens themselves: halfway images with limbs up to a third
shorter (a 30 Hz pulse on every fast motion), and idle motion moving every
other frame. Particles stepped at 30 Hz (below).

**Fixes:**

- *Skeleton blending* (`pc_interp_skeleton`, RPC/settings `interp_skeleton`,
  overlay "Rotation-aware character in-betweens", default on): each bone's
  rotation (its matrix with column scales divided out) is slerped relative
  to its parent's, the rest of the matrix (Q^T M: scales and the GTE's
  fixed-point skew) lerped, and each joint carried by its blended parent
  (its offset in the parent's frame lerped), so limbs swing on arcs at full
  length. The hierarchy is found in the data (the earlier bone in whose
  frame a bone's offset stays the same in both palettes, within 3 units +
  1%), so both palettes come back exactly at t = 0 and 1 whatever is found,
  and an unchanged bone stays exactly as it was (the lerped rest; an early
  version that re-orthogonalised the matrices jittered idle characters by
  ~0.15 units). A model's root carried by another model's bone (a weapon
  or shield in a hand: offset within 3 units + 2%, at most 256 units away,
  while the carrier moved at least 8× that tolerance) is blended through
  that bone's blended transform, so the weapon stays in the hand. Lists
  other than object lists (particles) blend each entry on its own.
- *Held poses* (`actor_native.c` `hold_segment`, `pc_interp_holds`,
  `interp_holds`, "Smooth held poses", default on): a pose identical to the
  frame before (up to float noise; a moving camera's rounding counts as a
  change) is a hold. After two changes in a row that came after holds of
  1-3 frames, a change is blended from the pose shown at its arrival to
  the new one over (the shorter of the last two holds + 1) game frames
  (`render_instance.blend_offset/scale`; the live frame is drawn partway
  too). A change that comes sooner starts from the pose shown at that
  moment, so nothing snaps; anything that changes every frame is unaffected
  (no lag); held slow motions lag by up to one game frame.
- *Particles* (`effect_native.c`): each `func_800D6E44` node's centre is a
  palette entry (chunks of 64 nodes, one transient mesh and instance each),
  with where the same node was the frame before (keyed by the node's
  address; not when it is younger, i.e. reused, or moved 256 units or more).
  First room's burning city: ~91 nodes per frame, 93% followed, 6.7 units
  per game frame on average, now interpolated (`ROOD_PARTICLE_DEBUG=1` logs
  this per 60 frames). Drop shadows already blended their transforms.
- *Images per vsync* (`pc_present_rate`, `ROOD_PRESENT_RATE`, RPC/settings
  `present_rate` 1-8, overlay slider "Images per vsync", default 1):
  `pc_tick` presents again at k/n of the vsync (`pc_window_wait_fraction`
  sleeps to that fraction of the paced field; no wait in fast mode) with
  `pc_present_phase` added to the replay's t. With 4: 240 distinct poses
  per second for every animating character (opening 2490: 8 even steps per
  30 fps game frame), the arrival vsync's later fractions already blending
  toward the new frame.

What still steps: 2D packets (HUD, menus, text, the screen-space effect
plugins in `src/EFFECT`), the room's discrete changes (cutaway walls),
texture animation, vertex morphs (SEQ opcodes 36-39 swap vertex data, not
palettes) and particles on fixed layers (depth modes 5-8, still packets).

**For unlocked frame rates**, what remains: present on the display's own
clock instead of integer fractions of the 59.94 Hz field (t from real time
since the frame's arrival; the replay already takes any t), with a
vsync-aware swap (FIFO/mailbox) instead of the frame loop's sleep; make
each image cheaper (each one redraws the replay, the lighting and, with GI,
a path-tracing update: the per-image cost decides the reachable rate); and,
for an exact in-between pose instead of the slerp, evaluate the animation
at fractional ticks natively: keep each actor's previous and current
`unk704` (post-blend Euler angles, 0x550 bytes), lerp the angles with the
±4096 wrap the game's own blend uses, and compose the bones with a
side-effect-free copy of `func_800B28A8`'s maths (the game's copy mutates
spin bones, sway counters and the bone tables, so it cannot be called
twice), snapping across `func_800AF844`'s Euler flip at a crossfade's
start, loop resets and animation changes without a blend. The skeleton
slerp differs from that exact evaluation only by Euler-lerp vs slerp paths
within one tick.

Test: `pc/tests/animation_smoke.py` (registered in `run.py`; the battle
half with a save card): New Game, the opening's idle scene, held poses
stepped vs blended (about 30 → over 50 distinct poses per second);
Continue, battle mode and an attack with linear (31% shrink, the control)
and skeleton blending (0%, every animating instance new every image,
weapon drift under 4 units, no unmatched instances).
