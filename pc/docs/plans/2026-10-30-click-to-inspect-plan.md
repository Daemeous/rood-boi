# Click-to-inspect: implementation plan (materials and inspection, part 2)

Design: `2026-10-27-materials-and-inspection-design.md`, section "2. Click-to-inspect". Built inside the
editor shell (`2026-10-30-editor-shell-design.md`). Worktree `click-to-inspect`, from main 0d2336ee1.
Brief: `pc/docs/Worker_Brief.md`.

## What exists (found before adding anything)

- Picking a textured native triangle under a window pixel exists: `emissive_editor.cpp`'s `Picked`
  (`pick_pixel` / `pick_world`: the renderer's depth readback gives the world point, then the native lists
  of the presented frame are walked for the nearest triangle containing it). It knows the texture (content
  key, palette key, page, CLUT, UV box, texel), the mesh handle, the surface (`SURFACE_ROOM` with the
  polygon's byte offset in the room data, `SURFACE_MODEL` with the model hash) and the room. It is shared
  as `emissive::TexturePick` (`emissive_internal.h`) with the material editor (`material pick`) but the
  surface fields are dropped there. The material survey rasterises the same lists.
- The selection model (`editor_selection.h`) has kinds light, light actor (prefab instance), glow, fog
  volume, sun, emitter; none for a surface or a drawn model. Viewport clicks on nothing clear the
  selection (`lights.cpp` marquee release → `empty_click`; RPC `editor_select click` → `click_at`).
- Details (`editor_details.cpp`) draws the primary through the schema; glows have a hand-drawn page.
- The Outliner (`dev_overlay.cpp outliner`) has sections per kind plus "Room groups" (mesh groups with
  polygon counts). Rows sync through `rood_scene::row_clicked / row_drawn`.
- View modes: `pc_gl_view_mode` (0-5, 11 on both renderers; 6-10, 12 Vulkan only), `pc_gi_debug`,
  `pc_gl_show_depth`; commands `view.mode.0..10` by a UI index; wireframe is an overlay switch
  (`editor_wireframe`, `editor_character_wireframe`).
- Where-used data: the rollout survey (`pc/build/material_rollout/rooms/<zone>-<room>.json`, per texture
  key, size, clut, share; `rooms.json` names the rooms); no run-time reader. `pc_room_browser_go_anywhere`
  warps.
- Game characters: `pc_dev_actor_of(object)` maps the drawn object to an actor slot at draw time
  (`actor_native.c pc_object_native_draw`); meshes are registered by hash only.

## Builder's decisions (mine, flagged for the user)

1. **Two new selection kinds**, not a separate "inspected" state: `KIND_SURFACE` (id
   `surface:<zone>-<room>:<polygon offset, hex>`: one room polygon) and `KIND_MODEL` (id
   `model:<hash>`: one drawn model, a character, weapon or prop). They go through the one selection, so
   Details, the Outliner, Escape, the lock and the RPC treat them as any object. They cannot be moved,
   hidden, copied or deleted (the design's non-goal: no transforms); those operations ignore them.
2. **Picking stays CPU**: the existing picker (depth readback + the native lists) is reused and
   extended (`TexturePick` gains the surface fields and the picked triangle); no id buffer. A click on
   empty space picks a surface/model **before** clearing (Shift/Ctrl clicks keep their placing meaning);
   RPC `editor_select click` does the same, so tests pick through the existing RPC.
3. **The inspected item is kept per reference** (`editor_inspect.cpp`: ref → what was picked) while the
   room stays; room change drops them (as glows). Position for F: the picked triangle's centroid.
4. **Details page** for both kinds is hand-drawn (`rood_inspect::details`), like the glow page: the
   schema has no read-only text fields. Sections: What (kind, room, polygon or model, actor slot when
   known), Mesh (triangle count, list: room / layered block / object), Texture (key, size, palette key,
   page, CLUT, box, nearest-sampled preview drawn as texel rectangles — no renderer upload path needed,
   same on GL and Vulkan), Material (entry id, name, preset, the layer it came from: shipped catalog, user
   layer, pending edit, review preview; "none"), Shader (the present pass's shader name per renderer),
   buttons "Open in Material editor" (Details > Material with the entry selected, or a new entry for the
   texture) and "Where used".
5. **Where used** reads the rollout survey's `rooms/*.json` once (lazily; `$ROOD_MATERIAL_ROLLOUT` or the
   directory of the review's proposals file) into an index key → rooms (share), and palettes likewise;
   "drawn now" comes from the live use list. Go = `pc_room_browser_go_anywhere` (the review's
   enemies/scripts settings). Missing survey data: the section says so, the live rooms still list.
6. **Outliner**: a section "Surfaces and models (this room)" listing the models drawn now (from the
   object lists of the presented frame: label from the actor slot — "Ashley", "Actor N" — else "Model
   <hash>") and every surface selected or inspected so far ("Surface <offset> (group G)"). Not every
   room polygon: thousands of rows would bury the rest. Rows sync both ways.
7. **View modes**: a View > View mode submenu and commands for the design's list: Lit, Unlit, Lighting
   only, Base colour, Vertex colours, World normals, Roughness, Metal, Emissive, AO, Wireframe, plus the
   existing debug ones. Roughness / Metal / Emissive / AO are new shader modes 13-16 in `scaled.frag`
   (Vulkan; they read the material planes and the emissive/AO terms the pass already has). GL has no
   material planes in its present pass: those four show the lit picture there and the menu says
   "(Vulkan)". Wireframe toggles the room and character wireframe overlays together. RPC
   `editor_inspect {action "view_mode", name}` sets by name and replies the previous, so a test restores.
8. **RPC `editor_inspect`**: `pick {x, y | world}` (selects and replies the item), `state` (the primary's
   item as Details shows it), `where_used {key | clut}`, `go {zone, room}`, `view_mode {name}` / `view_modes`,
   `outliner` (the section's rows). Lua reaches it through `rood.dev.rpc` as any RPC.

## Tasks

1. **Picker exposure** (`emissive_internal.h/.cpp`): `TexturePick` gains `kind` (room/model/other),
   `polygon`, `model`, `mesh`, `object_list`, `triangle[9]`; `emissive::mesh_detail(mesh, ...)` reports a
   mesh's triangle count and kind. actor_native.c notes the actor slot per model mesh
   (`pc_inspect_model_actor`, weak). Test: `editor_inspect_smoke check_pick` (RPC pick on a floor pixel
   replies kind room, a key, a size).
2. **Selection kinds + module** (`editor_selection.*`, `editor_inspect.{h,cpp}`, `editor_scene.cpp`,
   `lights.cpp`): the kinds, ids, labels, exists, position; the click fallback (viewport and RPC);
   highlight of the picked polygon / model outline in the view; room change drops items. Tests: `check_pick`
   (click selects `surface:`; Escape clears; a prop/actor click selects `model:`; works paused).
3. **Details + RPC state** (`editor_details.cpp`, `editor_inspect.cpp`): the page and `state`. Test:
   `check_details` (fields present, entry source, open-in-editor switches the Details tab and selects).
4. **Outliner section** (`dev_overlay.cpp`). Test: `check_outliner` (the selected surface has a row, the
   RPC `outliner` lists it; selecting the row through RPC selects it in the view state).
5. **Where used** (`editor_inspect.cpp`). Test: `check_where_used` (the floor texture's rooms include the
   save room; Go to another listed room changes the room; back).
6. **View modes** (`dev_overlay.cpp`, `scaled.frag`, `editor_inspect.cpp`). Test: `check_view_modes`
   (each toggles through RPC and restores; the picture differs from lit for the ones the renderer has).
7. **Docs and gates**: `Extensions.md` (RPC), `run.py` suite, `affected.toml`; the gates on GL and Vulkan.

## Progress

- 2026-10-30 19:50: plan written; builds (64, battle) green in the worktree.
- 2026-10-30 20:00: tasks 1-6 built (393af5cb0): `editor_inspect.{h,cpp}`, the picker's surface
  fields, the kinds, the click fallback (viewport and `editor_select click`), the outline, Details'
  page, the Outliner section, where-used from the survey, view modes (scaled.frag 13-16), RPC
  `editor_inspect`. `editor_inspect_smoke` green: GL 88 checks, Vulkan 92. Decisions made on the way:
  Escape clears an inspected selection first and closes the editor on the next press (the editor's
  Escape = close stays for everything else); a model never picked but drawn now (an Outliner row, an
  RPC id) gets its item from its mesh; the Outliner lists surfaces inspected so far, not every polygon.
  Next: the gates on GL and Vulkan, Extensions.md.
- 2026-10-30 20:10: part 2 done. Gates green on GL and Vulkan, one suite at a time, nice 10:
  editor_inspect (GL 88 checks, Vulkan 92), editor_cutaway, editor_handles, editor_library,
  editor_roomload, editor_schema, editor_selection, editor_shell (its "click on nothing" check now
  accepts the surface there being selected: the one behaviour change of this part), editor_snap,
  editor_transform, editor_undo, lua_imgui, material_bake (+ material_baked), material_catalog,
  material_relief, material_review, material_rollout, material_smoke, material_survey,
  photo_engine_reload, photo_foundation, photo_full_room, photo_mode, widescreen_smoke;
  editor_tools_probe and selection_probe (CMake). H / Shift+H leave inspected items alone.
  For the user: Escape's first press clears an inspected selection (the design's "Escape clears");
  roughness / metal / emissive / AO modes are Vulkan only (GL's present pass has no material planes);
  the Outliner lists inspected surfaces, not every polygon; the design's "effects if cheap" is not
  picked (effects lists are not in the picker's lists).

