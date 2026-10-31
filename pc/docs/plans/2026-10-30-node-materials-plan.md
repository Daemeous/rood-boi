# The node material editor: implementation plan (materials and inspection, part 4)

Design: `2026-10-27-materials-and-inspection-design.md`, section "4. The node material editor" and what
"Decided by the user in review", "Calls made in review", "Left to the builder" say of graphs. Base: part 3
(`2026-10-30-master-materials-plan.md`, branch master-materials a329336e8, on click-to-inspect). Worktree
`node-materials`. Brief: `pc/docs/Worker_Brief.md`.

## What exists (found before adding anything)

- A master is a data file (`masters/<name>.json`: `shader`, `parameters`, `slots`); `materials.cpp
  read_master` reads any file in `masters_dir()`; the standard one is `builtin:standard_surface`. An
  instance names its master; `entry_of` resolves the parameters by name into a `Surface` (the five
  standard names; others are carried but unused), and `emissive.cpp write_materials` stamps that
  Surface into six per-texel planes (`PC_MATERIAL_PLANES`) of the slot-map image, read by
  `material.glsl material_texel` in `native_frag.glsl` and packed into the G-buffer's material target.
- Shader hot reload (`shader_reload.cpp`): polls the stage files of `$ROOD_SHADERS` (else
  `pc/shaders/vulkan`) and what they include, compiles with the in-process glslang
  (`shader_compiler.cpp`: a `DirStackFileIncluder` over the stage's folder, then SMAA), keeps the
  last good pipeline on an error, RPC `shaders`.
- `rood.imgui` (`lua_api_imgui.cpp`): widgets, tables, popups, keys, `GetIO().MousePos`; nothing for a
  draw list, the cursor's screen position, invisible buttons or mouse buttons: a node canvas needs
  those (added here, task 4).
- GL draws no material response (part 3): nothing here reaches GL.

## Builder's decisions (mine, flagged for the user)

1. **How graph materials reach the draws: a seventh material plane and one generated include.** Plane 6
   (`PC_MATERIAL_PLANES` 7) holds a *graph slot* per texel cell: 0 none, else 1..255, one slot per
   assigned instance whose master is a graph (sorted by instance id). `native_frag.glsl` includes
   `material_graph.glsl`: the shipped file is a stub (an empty switch, so the embedded SPIR-V and every
   frame without a graph instance stay exactly as they are); the generator writes a shadowing file of the
   same name into the generated folder (`$ROOD_SHADERS_GENERATED`, else `pc/build/shaders/generated`),
   which the compiler's includer searches first and the reload service watches like a source. The
   generated file holds one GLSL function per graph master and a `switch` over the slots that calls the
   instance's master with **the instance's parameter values baked in as literals**: an edit of a value
   regenerates and hot reloads (the design's "about a second"). A uniform table for slider-speed
   updates is a later optimisation, not this slice.
2. **The graph's job is surface inputs only.** The generated function receives the texel colour, the
   vertex colour, the UV, the normal and the planes' values (roughness, specular, metal, relief) and may
   set: base colour (replaces the texel's colour before the game's own modulation), roughness, specular,
   metal, the relief (pass-through by default) and a glow (emissive output: `c = max(c, glow)`, the shade
   marked as emissive_apply marks it: glow only, never the emissive light system). Lighting is untouched
   (one GGX model, `scaled.frag`). Light intensities untouched.
3. **Node definitions are GLSL functions in JSON** (`pc/assets/materials/nodes/<name>.json`, and the
   user's under `<user lighting dir>/materials/nodes`): `inputs` (name, type `float|float2|float3|float4`,
   default, min, max, `pin`), `outputs`, and `glsl`, the function's body over those names. The generator
   emits `void node_<name>(in ..., out ...)` once per definition and one call per graph node. A developer
   adds a node by adding a file; the user sees a node with knobs. Source nodes (`texel`, `planes`, `uv`,
   `normal`, `vertex_color`) and the `output` node are built in (they read the context). A `float`
   feeding a `float3` input broadcasts; any other mismatch is an error on the link's target node.
4. **Per pixel, texel-snapped by default.** Every built-in source but UV is per texel already (the
   colour and planes are read at the texel the pixel lands in). The UV node has a `snap` toggle (default
   on: the texel's centre, `floor(uv) + 0.5`); off gives the raw per-pixel coordinate. Other nodes are
   pure functions of their inputs, so snapping is a property of the sources.
5. **Graphs are master files**: `masters/<name>.json` with `"shader": "graph:<name>"`, the `parameters`
   list (part 3's reader; here derived from the promoted knobs and written by Save so instances see
   them) and a `graph` object: `nodes` (id, type, position, knob values, pinned knobs, snap), `links`
   (from node.output to node.input), `promoted` (node.input → parameter name). The user's graphs are
   saved under `<user lighting dir>/materials/masters` (`$ROOD_MATERIAL_GRAPHS`), read beside the
   shipped masters (the user's wins by name). The standard surface as a graph ships as
   `standard_surface_graph.json`: planes → output, nothing else.
6. **Invalid graph**: generation reports the first error with its node; the generated file is not
   rewritten (the last good shader stays), the editor outlines the node red and shows the message, RPC
   `material_graph status` carries it. A compile error of the generated GLSL (a bad custom node body)
   comes back through RPC `shaders` as today and is shown the same way.
7. **The editor** is `pc/plugins/rood.material_graph` (Lua, `dev.tools`, on by default like the review):
   Window > Material graph. Left a graph list and the node palette, centre the canvas (nodes with pinned
   knobs on their bodies, pins, bezier links; drag to move, drag pin to pin to link, right-click to add,
   Delete removes), right the parameter panel of the selected node (every knob with a pin toggle and
   Promote), the master's promoted parameters and the error. The live preview is part 3's Material
   preview window (its sphere shades the resolved standard values; the graph's full response shows in
   the game view). All edits go through RPC `material_graph` (`set` of the whole graph after each
   edit: the editor is stateless across reloads).

## Formats

Node (`nodes/<name>.json`):
```json
{ "format": 1, "name": "lerp", "label": "Lerp", "category": "Math",
  "inputs": [ { "name": "a", "type": "float3", "default": [0, 0, 0] }, { "name": "b", "type": "float3", "default": [1, 1, 1] },
              { "name": "t", "type": "float", "default": 0.5, "min": 0, "max": 1, "pin": true } ],
  "outputs": [ { "name": "out", "type": "float3" } ],
  "glsl": "out = mix(a, b, t);" }
```
Graph master (`masters/<name>.json`):
```json
{ "format": 1, "name": "tinted", "shader": "graph:tinted",
  "parameters": [ { "name": "tint", "type": "number", "default": 1, "min": 0, "max": 1, "label": "Tint" } ],
  "graph": { "nodes": [ { "id": "n1", "type": "texel", "pos": [40, 120] },
                        { "id": "n2", "type": "lerp", "pos": [260, 120], "knobs": { "b": [1, 0.5, 0.2], "t": 1 }, "pinned": ["t"] },
                        { "id": "n3", "type": "output", "pos": [480, 120] } ],
             "links": [ { "from": "n1.color", "to": "n2.a" }, { "from": "n2.out", "to": "n3.base" } ],
             "promoted": { "n2.t": "tint" } } }
```
Generated GLSL (`material_graph.glsl`): the `GraphIn` struct, `node_<type>` functions, `graph_<name>`
functions whose arguments are the promoted parameters, and `material_graph(uint slot, ...)` switching
on the slot. The built-in sources read `GraphIn`; the output node assigns the `inout` results and sets
bits of `set` (1 base, 2 glow).

## Tasks

1. **Node definitions and graphs in C++** (`material_graph.{h,cpp}`): the node library (shipped + user
   files, built-ins), graph read/write, validation and GLSL generation; the slot registry
   (`pc_material_graph_slot(entry id)`), the generated file written on change (atomic rename), the
   per-frame poll. Test: `material_graph_smoke` section `generate` (RPC `material_graph generate` on
   the shipped standard graph and a scratch graph: the GLSL names the nodes, a cycle, a type mismatch and
   an unknown input are refused naming the node, the last good text is kept).
2. **The seventh plane and the include**: `emissive.h/.cpp` (plane 6 = slot; a slot alone makes a cell
   listed), `vk_hw.c` (loop unchanged, height from the constant), `native_frag.glsl` + the stub
   `material_graph.glsl`, `shader_compiler.cpp` (the generated folder searched first, files recorded),
   `shader_reload.cpp` (the generated folder polled; a generated file affects the stages that include a
   file of its name). Test: section `identity` (the Keep: a user layer whose entries use
   `standard_surface_graph`: frames byte-identical to the same entries on `standard_surface`, Vulkan);
   `live` (a tint graph assigned: the frame changes within 3 s; a knob edit changes it again; a broken
   graph keeps it; the stub alone — no generated file — is byte-identical to before the branch).
3. **RPC `material_graph`**: `list`, `nodes`, `get`, `new`, `set`, `save`, `remove`, `generate`,
   `status`, and the node-level edits the editor uses (`node_add`, `node_remove`, `node_move`, `link`,
   `unlink`, `knob`, `pin`, `promote`). Test: section `rpc`.
4. **`rood.imgui` additions for a canvas**: `GetCursorScreenPos`, `SetCursorScreenPos`,
   `InvisibleButton`, `IsMouseDown/Clicked/Released/Dragging`, `GetMouseDragDelta`, `ResetMouseDragDelta`,
   `GetMousePos`, `IsMouseHoveringRect`, `GetItemRectMin/Max`, `PushClipRect/PopClipRect`, and
   `DrawList_*` (`AddLine`, `AddRect`, `AddRectFilled`, `AddCircle`, `AddCircleFilled`, `AddBezierCubic`,
   `AddText`, `AddTriangleFilled`) on the window's draw list (colours as `{r, g, b, a}` tables, Lua's way).
   Test: `lua_imgui_smoke` grows a `canvas` check (the draw-list calls run, an invisible button reports
   hover/active, the cursor round-trips).
5. **The editor plugin** `rood.material_graph`. Test: section `editor` (the window opens by RPC `lua
   window`; the plugin's own state (`selected`, `graph`) read through RPC `lua eval`; a knob edit from
   the panel reaches `material_graph get`).
6. **Docs and gates**: Extensions.md (RPC, Lua API), Materials.md (graphs), run.py, affected.toml; the
   named gates on GL and Vulkan.

## Progress

- 2026-10-30 22:15: plan written.
- 2026-10-30 22:40: tasks 1-6 built (b38c4cb48, the canvas check, the docs f56c53447): `material_graph.{h,cpp}`
  (the node library: five built-in sources and the output, 14 shipped node files under
  `pc/assets/materials/nodes`; graph masters read beside the plain ones, the user's under `graphs_dir()`;
  validation and generation; the slot registry; the generated include written on change and removed when
  no instance uses a graph; RPC `material_graph`), the seventh plane (`emissive.{h,cpp}`: the slot in the
  stamp key and the resolution, so a master change on an entry rewrites its cells), the stub
  `material_graph.glsl` and the call in `native_frag.glsl`, the compiler's includer reading the generated
  folder first, the reload service watching it (a generated file affects the stages that include a file of
  its name), `rood.imgui` canvas calls (positions as two numbers, like `GetContentRegionAvail`),
  `pc/plugins/rood.material_graph` (palette, canvas, panel; the test drives it through its globals),
  `material_graph_smoke` (Vulkan: generate 32 checks, identity, live, editor; GL: generate and editor,
  identity and live skipped as GL draws no material response). Calls made on the way: a graph master's
  parameters are the standard five (they feed the planes) then the promoted knobs; only float knobs promote
  (colour knobs: later); every instance's values are literals in the regenerated include (a uniform table for
  slider-speed updates is a later optimisation); `promote` saves the file (it changes the master's
  interface), other edits are live and unsaved until Save; with no graph instance the generated file is
  removed; the time source, packet and character draws, and reflections of per-pixel results are not in this
  slice. Next: the gates on GL and Vulkan.
- 2026-10-30 22:50: the Materials panel got a Master combo (RPC `material_instance set/new {master}`), so a
  graph master is reachable from the UI (95ca7644b). GL gates green, one suite at a time, nice 10:
  material_graph_smoke (generate and editor; identity and live skipped on GL), material_instances,
  material_catalog, material_rollout, material_bake, material_smoke (its picker checks now read the seventh
  plane), material_relief, material_baked, material_survey, material_review, editor_inspect, editor_cutaway,
  editor_handles, editor_library, editor_roomload, editor_schema, editor_selection, editor_shell, editor_snap,
  editor_transform, editor_undo, photo_engine_reload, photo_foundation, photo_full_room, photo_mode,
  shader_compiler, widescreen, lua_imgui (with the canvas check), fixture_lighting, light_actors
  (shader_reload_smoke skips on GL); editor_tools_probe (CMake) passes. Vulkan gates running.
- 2026-10-30 22:45: Vulkan gates green (the same 31 suites, shader_reload_smoke included; material_graph_smoke
  70 checks: the Keep byte-identical through the standard surface graph, the tint graph live within a few
  seconds, a knob edit, a broken graph kept out with its node named, a promoted knob set per instance, the
  master switched from the Materials panel's path, the generated file removed when no instance uses a graph,
  the editor). The folder scans behind the generation are throttled (twice a second; a graph save re-reads
  the masters at once). Part 4's slice is done; left for later (listed in Materials.md "Node graphs"): colour
  knobs as parameters, a uniform table for slider-speed updates, a time source, packet and character draws,
  per-pixel results in reflections, GL (none, by the design).
- 2026-10-31 11:40: follow-ups (worktree node-materials-2, from 9bf28db17), each green before the next:
  1. **Slider-speed edits** (ae0f4279d): values leave the generated GLSL for a values table, `graph_values[]`
     (std430, the native draws' binding 7, uploaded per draw from `pc_material_graph_values`): entry 0 the game
     time, then every used graph's unlinked, unpromoted knobs and every slot's promoted parameters. The include
     reads them at literal offsets; an offset is handed out once per key (`k:<graph>:<node>.<input>`,
     `p:<instance>:<param>`) and never reused while the table lives (past 4096 entries it starts over), so the
     pipeline still on the previous include during a reload reads its own keys' current values. A value edit
     refills the table (the accessor sees the catalog's generation, so an instance edit shows on the very next
     frame); only structure rewrites the include. The stub declares the buffer too: the hot reload's contract is
     the embedded stage's interface. RPC `material_graph values`; `status` has `values` and `time`. Found on the
     way: a graph file's array knobs (colours) read back empty (`floats_of(.., 0)` resized to 0): a saved graph
     lost its colour knobs; fixed.
  2. **Colour parameters** (d9c62a1cb): a `float3` knob marked `color` promotes to a master parameter of type
     `color` (default the knob's colour); materials hold it as `<name>.r/.g/.b` (resolve, own, from) and the
     files write `[r, g, b]`; RPC `material_instance set {name: [r, g, b] | null}`; Window > Materials shows a
     swatch with Reset (both the three parts). Other float2/3/4 knobs stay knobs.
  3. **Time**: the built-in *Time* source (Seconds within its Loop knob, Phase 0..1) and a node-file input flag
     `time` (unlinked, it reads the game time). The game time is vsyncs / 60 (`pc_vsync_count`), looped each
     hour: the game held (pause, photo mode) holds it, and a photo render (run from the held moment M, rewound)
     draws the same time each render, so the held picture is identical (checked: two renders and a later
     present, 0 pixels). Shipped example: `nodes/pulse.json` (Animation), assigned to nothing.
  Tests: `material_graph_smoke` grew the drag (20 knob values, one a frame: every frame changes, no reload, the
  include untouched), an instance's parameter on the next frame, a colour parameter (set, bad values refused,
  Reset, saved as [r, g, b] and read back), and section `time` (Time -> Pulse generates, the game time moves
  0.25 s in 15 vsyncs, the pulse moves the picture, held: time and picture stay, photo renders identical).
- 2026-10-31 11:50: gates green, one suite at a time, nice 10, GL and Vulkan: material_graph_smoke (Vulkan 85+
  checks incl. the drag, colour and time sections; GL the generate, colour-promote, time-generation and editor
  parts), material_instances, material_catalog, material_rollout, material_bake, material_smoke, material_relief,
  material_baked, material_survey, material_review, editor_cutaway, editor_undo, editor_selection, editor_schema,
  editor_transform, editor_handles, editor_library, editor_snap, editor_roomload, editor_shell, editor_inspect,
  photo_engine_reload, photo_foundation, photo_mode, photo_full_room, shader_compiler, shader_reload (skips on GL),
  widescreen, lua_imgui, fixture_lighting, light_actors (61 pass + 1 GL skip); editor_tools_probe (CMake) passes.
  Left: packet and character draws, reflections of per-pixel/time results, float2/float4 parameters, GL (none).
