# Master materials and instances: implementation plan (materials and inspection, part 3)

Design: `2026-10-27-materials-and-inspection-design.md`, section "3. Master materials and instances".
Base: part 2 (`2026-10-30-click-to-inspect-plan.md`, branch click-to-inspect 7c015cfd0). Worktree
`master-materials`. Brief: `pc/docs/Worker_Brief.md`.

## What exists (found before adding anything)

- Two catalog files, `format: 1` (`pc/assets/materials/catalog.json`, the user's `materials.json`):
  `presets` (name → roughness, specular, metal, optional auto_height, bump) and `entries` (id, name,
  where, review, `preset`, explicit fields over it, `match` {key, size, clut} or {clut} (a palette entry),
  `scope` {room} | {rooms}). `materials.cpp` parses both into `Entry` (resolved `Surface` + the
  `explicit_fields` mask + the scope) and the later file wins per id; the editor's unsaved layer
  (`set_pending`) and the review's temporary previews (`set_preview`) are entries over both. The user
  layer is read, modified and written as JSON (`put_user_entry`, `remove_user_entry`; the file keeps its
  layout). The review's accepted proposals are user entries (`review: "accepted"`, ids `R<palette>`). The
  baked layer is maps only (`pc/build/materials/<key>/*.png`): it names no entries.
- Everything downstream reads `Entry` and `Surface`: the material planes (`emissive.cpp
  write_materials`), the picker, Details > Material (`material_editor.cpp`), the review, part 2's
  Details (entry, preset, layer), the RPCs. GL matches the catalog but draws no response.
- Thumbnails are drawn as ImDrawList rectangles (no renderer upload path; the same on GL and Vulkan).
- The shell docks any tool window (`rood_shell::dock_tool_window`); commands in `editor_commands`.

## Builder's decisions (mine, flagged for the user)

1. **The instance model is the catalog's own format, extended; `Entry` becomes a derived view.** An
   `Instance` (materials.h) has an id, a master (`standard_surface`, the only one in this part), an
   optional parent (an instance id), its own parameter values by name (only the ones it sets), and an
   optional assignment (match + scope) with the curation fields (name, where, review). Resolution walks
   the parents, then the master's defaults. The render-facing `Entry` list is derived from the assigned
   instances (value = resolved, `explicit_fields` = own values, scope kept), so the planes, the picker,
   the review, Details and the old RPCs are unchanged, and the migration renders identically by
   construction (and is checked, task 6).
2. **Lossless, automatic migration in memory; the files are format 1 + optional fields.** Reading a
   format-1 file: every preset becomes a root instance (id = the preset's name), every entry an instance
   whose parent is its preset and whose own values are its explicit fields. Nothing is rewritten on
   read. Writing (Save) keeps `presets` and `entries` and adds only what is new: `parent` (when the parent
   is not a bare preset; `preset` then still names the root preset, so old readers get the preset plus
   the instance's own values), `master` (when not the standard one), and entries without `match`
   (unassigned instances: user-made presets with a parent). A file with none of those stays byte-for-byte
   the format it was; one with any of them gets `format: 2`. Old files stay readable forever; old tools
   (`material_bake.py`, `material_rollout_apply.py`) read format 2 as format 1 (they skip what they do
   not know). Ids: presets and entries share one id space; a clash (a preset named like an entry id) is
   reported by RPC `validate` and the entry wins, as the format-1 reader has it (the preset is looked up
   by name in `presets`, never in `entries`).
3. **The master is a data file**: `pc/assets/materials/masters/standard_surface.json` lists the
   parameters (name, type number/bool, default, min, max, label, tip, group) and the texture slots
   (`texel`, `roughness`, `height`, `metal`, `emissive`: the game texel and the channel maps, which stay
   per texture key). The UI and the RPC are driven by it (nothing in the panel names roughness), so a
   part-4 master generated from a graph is one more file with a `shader` field and its own parameter
   list. The standard master's `shader` is `builtin:standard_surface` (the existing planes → material.glsl
   → scaled.frag path). The C++ keeps a built-in copy of the standard description for when the file is
   missing (the game never changes because a data file went).
4. **"Assign to selection"**: the primary selection's surface/model (part 2) gives its texture key, size
   and palette; the instance is assigned to that texture, scoped to the current room (the design's
   reading); a second button assigns the palette instead. Both go through the unsaved layer (Save keeps).
5. **The Materials panel** (`material_instances.cpp`, Window > Materials, command `window.materials`):
   left the instances as a tree (roots, then children; a filter), right the parameter panel from the
   master (Houdini-style rows: label, control, a reset-to-parent button on rows with an own value; inherited
   values greyed and labelled with the ancestor they come from), the assignment, New child / Duplicate /
   Remove, Save / Revert. Part 2's "Open in Material editor" keeps opening Details > Material (the C++
   entry editor stays, as the review said), and that editor's preset combo lists every root.
6. **The preview** (`material_preview.cpp`, Window > Material preview): a sphere shaded on the CPU with the
   same GGX model (`Materials.md` "Shading": GGX D, height-correlated Smith, Schlick, F0 = 0.04 x specular
   mixed to the albedo by metal), nearest-sampled with the assigned texture's texels where the texture is
   in view, else a swatch; lit by a key light and an ambient whose direction, colour and strength are
   settings keys (`material_preview_*`, Details > Graphics), with "lit like the room" taken as the room's
   first dynamic light's colour when one exists. Drawn as rectangles, so identical on GL and Vulkan. The
   relief shows as the estimate's or the height map's slopes bending the normal (bump kept). Pixels
   are cached and reshaded only on change.
7. **RPC `material_instance`** carries everything the panel shows and edits (task 2); Lua reaches it
   through `rood.dev.rpc`.

## Formats

Master (`pc/assets/materials/masters/<name>.json`):
```json
{ "format": 1, "name": "standard_surface", "shader": "builtin:standard_surface",
  "parameters": [ { "name": "roughness", "type": "number", "default": 1, "min": 0, "max": 1,
                    "label": "Roughness", "group": "Surface", "tip": "..." }, ... ],
  "slots": [ { "name": "texel", "label": "Game texel" }, { "name": "roughness", "channel": "roughness" }, ... ] }
```
Catalog (format 1, plus): an entry may carry `"parent": "<instance id>"`, `"master": "<name>"`, and
may omit `match` (unassigned). `presets` unchanged.

## Tasks

1. **Instances in materials.{h,cpp}**: `Instance`, `Master` (+ the built-in standard description and the
   file reader), `instances()`, `masters()`, `resolve(id)`, `set_pending_instance`, derivation of the
   `Entry` list, the writer. Test: `material_instances_smoke` sections `migrate` (a format-1 scratch user
   file with overrides, palette entries, scopes, a user preset: the instances and their resolved values
   equal a Python resolver of the old format; `material list` entries identical before and after the
   file is rewritten through Save) and `parents` (child of child, reset to parent, cycle refused).
2. **RPC `material_instance`** (`material_instances.cpp`): `list`, `get`, `set {id, params}` (null resets),
   `parent`, `new`, `duplicate`, `assign`, `assign_selection`, `remove`, `save`, `revert`, `validate`,
   `migrate {out}` (writes the user layer as format 2 into a path, counts), `masters`. Test: `rpc`.
3. **Materials panel**. Test: `panel` (the window opens by command, docked; its rows list the master's
   parameters: RPC `editor_shell window`).
4. **Preview panel**. Test: `preview` (pixels returned, change with roughness, the same digest on both
   renderers — it is CPU-shaded).
5. **Rendering identity**: `identity` (the Keep on the migrated file vs the format-1 file: frames byte-
   identical on GL and Vulkan; the planes equal); the main checkout's user layer copied into pc/build:
   counts reported in the hand-back.
6. **Docs and gates**: Extensions.md, Materials.md, run.py, affected.toml; the gates on GL and Vulkan.

## Progress

- 2026-10-30 21:00: plan written.
- 2026-10-30 21:30: tasks 1-5 built (f37868de2, af15799f8): instances and masters in `materials.{h,cpp}`
  (the Entry list derived), `pc/assets/materials/masters/standard_surface.json`, `material_instances.cpp`
  (the Materials panel, RPC `material_instance`), `material_preview.cpp` (the sphere, RPC
  `material_preview`, settings keys `material_preview_*`), `material_instances_smoke` (104 checks, green on
  GL and Vulkan), `material_migration_check.py` on a copy of the main checkout's user layer: 399 entries, 399
  converted, 399 identical to the old reader, 0 differing, 431 instances (16 shipped presets, 16 shipped
  entries, the user's 399), rewritten through `migrate` and read back with identical values; the Keep
  (29/3) and the Catacombs (13/3) byte-identical on the migrated layer (307 / 279 textures with a
  material), Vulkan. Calls made on the way: an unknown parent keeps the instance listed (unresolved) and
  makes no entry (the old reader dropped the entry), and `migrate` writes it back as read; resolution is
  live (a preset edited in the user layer reaches shipped entries too: the old reader copied preset values
  at parse time, so a user layer with `presets` redefining a shipped preset's name shades shipped entries
  differently now; the main checkout's layer has no presets); the preview's light is its own (settings
  keys), not the room's lights; `material list` grew `entries: false` because the real layer's entries
  outgrow one RPC reply. Next: Extensions.md and Materials.md, the gates.
- 2026-10-30 21:35: part 3 done (6cc9de8a6, 15bbdbbfd). Gates green on GL and Vulkan, one suite at a time,
  nice 10: material_instances (104 checks each), material_catalog, material_rollout, material_bake,
  material_smoke, material_relief, material_baked, material_survey, material_review, editor_inspect,
  editor_cutaway, editor_handles, editor_library, editor_roomload, editor_schema, editor_selection,
  editor_shell, editor_snap, editor_transform, editor_undo, photo_engine_reload, photo_foundation,
  photo_full_room, photo_mode, widescreen, lua_imgui, fixture_lighting, light_actors; editor_tools_probe
  (CMake). material_migration_check.py on the main checkout's layer (a copy): byte-identical frames in
  29/3 and 13/3 on GL and Vulkan. Not done (for part 4 or later): the preview is lit by its own key light
  and ambient (settings), not the room's lights; the relief does not show on the sphere; the Materials
  panel is C++ (no Lua port); masters beyond the standard one are read but nothing generates them yet.

