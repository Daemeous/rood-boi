# Materials and inspection: design (2026-10-27)

## Intent

The user wants to work on the game's look the way an environment artist
works in Unreal: click something in the game, see what it is and what it is
made of, and change its material, live. The current set-up (a fixed list of
numbers per texture or palette, fed into one fixed lighting shader; shaders
compiled into the binary; no way to click a surface) was enough to prove
physical materials, not to author them. The guiding question, in the user's
words: "what features would an environment artist expect?"

The user is most at home in UE and likes Houdini's nodes and parameter
panels best. They do not write GLSL or HLSL, so the node graph is how they
build materials; code is the layer underneath that the developers write.

This is four projects, built in this order (agreed with the user; hot reload
first because everything after it depends on fast iteration):

1. Shader hot reload
2. Click-to-inspect (selection, Details, where used, view modes)
3. Master materials and material instances
4. The node material editor

Each gets its own implementation plan; this document is the shared design.

## Goals

- Click any surface or object in the game view and see a Details panel of
  what it is and what it is made of.
- Edit a material and see it in the game within about a second, with no
  rebuild and no restart.
- Build materials as node graphs, UE style, with Houdini's hybrid of knobs
  on the node and a full parameter panel.
- Every artist-facing thing is data (readable, diffable files) and every
  tool is scriptable (Lua panels, RPC), in line with the project's plugin
  direction: contributors and plugins are never bottlenecked on a rebuild.

## Non-goals (for now)

- Moving, rotating or scaling anything. The user said explicitly they are
  not after transforms at this time; selection is for inspecting and
  material work only.
- Decals, vertex painting, layer blending, replacing meshes or textures.
  Possible later; the PS1 texture look (nearest sampling) may rule some out.
- The GL renderer. Graph materials are Vulkan only (builder's call, see
  below); GL keeps today's fixed material path.

## Anti-goals (would make it a failure even if it works)

- A broken edit that blacks out the game. A shader or graph that fails to
  compile marks the error and the game keeps the last working version.
- Losing the user's work: approvals made in today's Material review, and
  entries in their material layer, must carry over exactly.
- Changing the look of anything nobody edited. With no materials (or only
  converted ones), frames match today's renderer.
- Changing lantern, candle or emissive light intensities (a standing user
  rule).

## Constraints

- Vulkan renderer, GLSL compiled to SPIR-V by the vendored glslang
  (`pc/tools/vk_shaders.py`, today at build time into a generated header).
- Materials today (`pc/docs/Materials.md`): entries match a texture (content
  key) or a palette, per room scope; maps are 8-bit PNGs per texture key
  (emissive, roughness, height, metal), user layer over shipped layer,
  already hot reloaded from disk; the values are written into material planes
  per texture cell and read by the draws into a G-buffer target, lit in one
  pass (`scaled.frag`) and the path-traced reflections. Debug view modes 6-10
  exist already (RPC `graphics view_mode`).
- Game art never leaves the machine and is never committed; thumbnails and
  previews of game textures stay under gitignored build/export folders.
- The project's rules: look/feel parameters are tunable data (settings, dev
  panel, RPC), never constants; workers run targeted tests; publish only via
  `pc/tools/publish.sh`.
- The Lua UI work approved the same day (`rood.imgui` bindings for dev-tool
  plugins, and Lua access to the dev RPC actions) comes before the panels in
  parts 2-4, which are built on it.

## 1. Shader hot reload

- The game can load SPIR-V from disk at run time (the embedded copies stay
  as the fallback and the shipped default).
- Saving a `.glsl`/stage file under `pc/shaders/vulkan` (or a material's
  generated shader, part 4) recompiles it in the background with the
  vendored glslang and swaps the affected pipelines at a safe point in the
  frame.
- A compile error is shown in the dev overlay (file, line, message) and over
  RPC; the old pipeline stays.
- Exposed over RPC (reload, status, errors) and to Lua.

## 2. Click-to-inspect

What an environment artist expects, adapted to this game. "Asset" here
means (my reading, the user agreed): room geometry, picked per surface;
characters and enemies; props and objects (doors, chests, crates); effects
if cheap.

- **Select**: click in the game view selects the thing under the cursor,
  outlined or tinted; Escape clears. Selection works paused.
- **Details panel**: what it is (room/zone, object or actor name/id), its
  mesh (triangle count, which list), the texture(s) and palette under the
  cursor (keys, sizes, a nearest-sampled preview), the material on it and
  where that came from (shipped catalog, user layer, a review proposal, an
  instance), and the shader it draws with. A button opens the material in
  the editor (part 3/4).
- **Outliner**: the current room's things as a list; selecting in either
  selects in both.
- **Where used**: every room and object that uses this texture, palette or
  material (from the rollout survey data where present), with Go buttons.
- **View modes** in a menu (existing modes 6-10 plus): unlit, lighting
  only, base colour, roughness, metal, normals, emissive, AO, wireframe.
- The Material review becomes one panel within this (its proposals are
  suggested material assignments).

## 3. Master materials and instances

- A **master material** is a shader (from a node graph, part 4, or
  hand-written) that exposes named parameters: numbers, colours, toggles
  and texture slots (the existing channel maps, and the game texel itself).
- A **material instance** is a master plus parameter values. Instances can
  have an instance as parent (UE style), with "reset to parent" per value.
- Assignment: an instance is assigned to a texture key or palette, scoped
  as today (everywhere, rooms, a room), plus to a selection from part 2.
  The layering stays: shipped, then the user's, then narrower scopes.
- **Migration**: today's presets (flagstone, rough stone, foliage, ...)
  become instances of one standard surface master; every existing entry,
  including accepted review proposals, converts automatically and
  losslessly into an instance (preset becomes the parent, fine-tuning
  becomes overrides, scope kept). Old files stay readable.
- A **preview** (a sphere or swatch, lit like the room) next to the live
  game view.
- Lighting stays one shared, physically based model (GGX as today);
  materials decide the surface inputs (base colour, roughness, metal,
  specular, normal/relief, emissive, opacity later), not the lighting.

## 4. The node material editor

- UE's layout: the graph in the middle, a live preview, a parameter panel
  at the side.
- **Houdini's hybrid** (the user's own idea): each node shows a few pinned
  knobs on its body (slider, colour swatch, toggle) for quick tweaks; the
  full list is in the parameter panel when the node is selected. Pinning is
  per node and per knob, from the panel; defaults pin the one or two
  most-used values.
- **Promote to parameter**: any knob can be promoted to a material
  parameter, which then shows on every instance (part 3) as a slider.
- **Custom nodes**: a developer writes a small GLSL function with declared
  inputs and outputs in a node file; it appears in the graph as a node with
  its inputs exposed, so the user never sees code.
- **Live**: every edit regenerates GLSL from the graph and goes through
  part 1's hot reload; it shows in the game and the preview in about a
  second. An invalid graph marks the bad node and keeps the last good
  shader.
- Graphs, node definitions and instances are data files; the editor is a
  Lua panel on `rood.imgui`.

## Decided by the user in review

- **Per pixel, pixel art by default**: graphs are evaluated per screen
  pixel in the draw shaders, but by default every node reads at the centre
  of the texture texel the pixel lands in, so results stay blocky like the
  art; a per-node toggle turns the snapping off for things meant to be
  smooth (e.g. a time-based flicker).
- **Assignment target**: texture or palette (everywhere, or scoped to
  rooms), as today. Per object (this crate) or per kind (all crates) is a
  later step and needs a per-draw material id; not in this design's first
  version. "Assign to selection" in part 3 means: to the texture or palette
  under the selection, scoped to its room.

## Calls made in review (Claude's, not the user's; flagged for the user to check)

- **Shader compilation ships**: glslang (already vendored, permissive
  licences) is linked into the game on every platform, so graphs and custom
  nodes from plugins work without a rebuild; compiled SPIR-V is cached
  under the build folder, never committed. The embedded SPIR-V stays the
  default and the fallback.
- **Graph outputs**: base colour (tint, blend, or replace the game texel's
  colour; this is not replacing texture files), roughness, specular, metal,
  normal/relief, emissive. A graph's emissive is glow only: it does not
  cast light through the emissive light system, so no light intensity
  changes.
- **Reflections and probes**: the traced reflections use the G-buffer
  material where it exists, as today; surfaces hit by reflection rays use
  each instance's constant values (time-varying or per-pixel effects do not
  show in reflections in the first version).
- **Draws**: graph materials apply to the native room draws first; packet
  draws and characters (actor materials, already listed under "Next" in
  Materials.md) follow, falling back to the standard surface values until
  then.
- **"Frames match"**: with no materials, bit-exact as today; converted
  entries within a small tolerance (no visible change, checked by the
  smoke tests with a per-channel threshold), so the fixed path can be
  replaced by the generated standard surface master.
- **Lua and the existing windows**: `rood.imgui` is its own prerequisite
  project (approved 2026-10-27, not yet designed): the binding, then the
  Material review ported to Lua as its first user. The C++ Details >
  Material editor stays until part 3 replaces it; the review's decisions
  file and the user layer keep their formats until migration (part 3) reads
  them.

## Left to the builder

- How graph materials reach the draws (e.g. a material id plane beside the
  existing material planes and one generated uber-shader that switches on
  it, versus a pipeline per material), the file formats, and the node set
  beyond the obvious (texture/game texel sample, math, lerp, constants,
  parameters, UV, world position/normal, time, vertex colour, the channel
  maps).
- How picking resolves what is under the cursor (an id buffer pass, or CPU
  ray casts against the native meshes).
- GL support for graph materials (decided here: none; GL falls back to the
  standard surface values).
- Test design, as long as each part has headless smoke coverage like the
  existing `material_smoke` / `material_review_smoke`.

## Added 2026-10-27 (after the review): better maps, and relief that catches the light

The user accepted every rollout proposal (to tune while playing) and found the
relief weak: most surfaces "do not really catch the light ... or feel like
there's some subtle displacement". Two causes, two pieces of work:

- **Relief only bends direct light.** The height estimate's normals reach the
  dynamic lights and the sun; the game's baked light has no direction and the
  traced GI shades with the triangle's normal, so in most rooms the bumps barely
  show. Feed the relief normal into the GI/indirect shading and give the baked
  light a direction (e.g. the probes' dominant direction). Do this first (or in
  parallel): it is quick to try with shader hot reload and shows whether better
  maps pay off.
  Done (branch relief-indirect): the probes' own directional response for the
  indirect light; the baked light's direction from its own gradient along the
  surface plus light from above (the probes do not see the baked light's
  sources); Materials.md "The relief under the baked and indirect light".
- **Maps from analysis, not a runtime guess.** An offline bake tool per texture
  key writes a full map set into the user layer: height and normal (new
  `normal.png` channel), and also roughness, metal, specular/cavity/AO as the
  user asked ("good roughness/metalness/etc maps all around"). Methods compared
  side by side in game: an improved classical estimate (frequency separation,
  seam/edge detection, per-material profiles, the painted shading's light
  direction integrated to height) and local ML models (texture-to-material and
  depth/normal estimators, after upscaling the tiny textures; weights download
  only with the user's OK; game art never leaves the machine). Instances (part
  3) take these maps as their texture inputs.

## Plan 2026-11-21: materials by object (Gitea #20 step 2)

The design's "later step": an entry may be keyed by an object as well as by texture or palette, so two objects
that share a texture (PS1 art is full of them) take different materials. Decisions (the builder's):

1. **Identity: what the game already names stably.** The emissive scopes already use it
   (`pc/docs/Emissive.md` Scope; `emissive.cpp` `scope_bits`): a room **group** (the room geometry block a native
   room mesh draws: its byte offset in the room data, the same on every visit; `pc_emissive_room_mesh`), a room
   **polygon** (byte offset, for a finer cut), and a **model** (the content hash of a character, weapon or prop:
   every copy, "per kind"). A single instance of a model (this crate among crates) has no identity yet: not in this
   step. Entry field `object`: `{"room": [z, r], "groups": [offsets], "polygons": [offsets], "models": [hashes]}`
   (room needed for groups and polygons). An entry without `object` is what it is today.
2. **Resolution.** For a drawn polygon: the object entries naming its group, polygon or model, in the
   room, that match its texture (`match` key/size/clut as today, or no `match`: every texture of the object) win
   over the texture and palette entries; the usual layering (shipped, user, narrower scope, last wins) orders them
   among themselves. Nothing else changes: with no object entry the frames are bit-identical.
3. **Draw: a per-polygon variant id, not another copy of the planes.** The material planes are per texel of VRAM,
   so a shared texture's cells are shared. A polygon gets a **variant** (1..65535, 0 none: a 16-bit vertex
   attribute beside the 16-bit scope bits, set from the CPU the same way (`render_mesh_emissive_scope`'s twin; per
   mesh and polygon, re-uploaded only on change)). A variant is an (object entry, texture use) pair; its cells are
   the same seven planes (`make_stamp`, with the object entry's values and the object's own maps) laid box-relative
   in an atlas below the planes, shelf-packed by each variant's real box size and grown on demand (the slot map
   image is re-created taller; its limit is the device's 2D size, 16384 rows; one record per variant, in a table
   the shader indexes: the box's origin and the atlas cell). `material_texel`/`material_graph_slot` read the atlas
   when the polygon's variant is not 0 and the planes otherwise. Past the atlas's limit the rest fall back to the
   texture entry, with a log line and the editor's warning. The packet path (native room off) has no variants,
   like scopes; GL draws no response.
4. **Maps by object.** The loader gets one more level: `<materials>/<key>/@<object id>/<channel>.png` over the
   texture's own, per layer as today (`materials::channel(key, channel, object)`); an object id is the entry's
   object text, short form. A map applies to the object's variant only.
5. **API.** Catalog: `object` on an entry (format stays 1 for a reader that ignores it; instances carry it, so
   `format: 2` as for any new field). RPC `material`: `pick` reports the picked object (kind, room, group or
   polygon, model), `new {object: true}` assigns the pick's object instead of its texture, `set`/`list` carry
   `object`. Details > Material: the assignment row gets **Assign to: this texture | this object** (the object
   named: "group 0x1a40 of 29/3", "model 4b8870d7"), and the list marks object entries.
6. **Blender import** (`room_blender.py`): every export triangle already has its group. Where a texture is used by
   more than one group, `import` writes the maps per (texture, group) under `@<group>/` and a user entry per
   object with the baked preset; a texture of one group stays as today. The scratch catalog of `shot` does the same.
7. **Tests**: `material_object_smoke` (an object entry beats the texture entry for its object only, in the pick and
   in the G-buffer's material target; another object of the same texture is unchanged; no entry: unchanged),
   the catalog/format round trip, `room_blender_test` for the per-object output; `material_smoke`,
   `material_instances_smoke`, `material_review_smoke` unchanged.

Order: catalog and resolution (data, RPC, tests) first, then the variant draw, then the editor row, then the
Blender import; each its own commit.
