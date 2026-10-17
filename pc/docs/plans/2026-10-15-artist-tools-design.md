# Artist tools: editor ergonomics for environment and lighting artists

Status: design approved 2026-10-15. The implementation plan comes next.
Phases A-C landed 2026-10-15; D and E, fit to window and the follow-ups:
pc/docs/plans/2026-10-16-artist-tools-de-plan.md (Place Actors became the
Library).

Open questions for the user (the review of D and E, 2026-10-16), where the
build differs from this design until decided:

- **Thumbnails** are drawn on the CPU (`pc/src/library_thumbs.cpp`) with the
  renderers' light model and the fog volume shader's terms (shape, soft
  edge, taper, wisps, phase, glow at its base, opacity, brightness, ambient,
  a self-shadow estimate), not by the OpenGL or Vulkan renderer itself: the
  same bytes on both renderers and every run, but not the exact in-game
  picture (no flames, glow sprites or the room's light). Rendering them
  offscreen with the real renderers is the alternative.
- **Docking:** the Library is a tab beside the Outliner or one floating
  window (Pop out / Dock), not Dear ImGui docking. The bundled Dear ImGui
  is its docking branch, so real docking is possible, but turning it on
  changes how every editor window moves and is laid out.
- **Save as prefab** is in the Library (Save...), the Outliner's right-click
  menu, Edit and a fog volume's Details (lights and light actors already had
  theirs), not in a viewport right-click menu: the right mouse button flies
  the camera there.

## Goal

Move the in-game editor toward the conveniences and conventions that
professional environment and lighting artists expect. The conventions are
Unreal Engine's, plus a few borrowed from Houdini. The camera already follows
Unreal (RMB + WASD, Alt orbit, MMB pan, F frame), and move/rotate are on W/E.
This design extends that to selection, panels, transforms, viewport handles,
volumes and a placement library.

Non-goals: Blender-style modal keys (G/R/S + typed axis), a separate editor
application, and dockable panels in this round (a later step).

## Current state (2026-10-15)

- `pc/src/dev_overlay.cpp` lays out a fixed side column: Outliner over
  Details. Details has the tabs Lighting / Inspector / Atmosphere /
  Post-processing / Graphics / Diagnostics. Dear ImGui and ImGuizmo.
- Picking (`lights.cpp` `pc_lights_viewport`) covers lights, light actors
  (prefab instances) and game glows, by distance to their screen icons.
  Fog volumes are selectable only from the Atmosphere tab list or RPC
  `select_volume`.
- Selecting in the viewport neither scrolls the Outliner nor opens Details.
  The details show only if the Lighting tab is already open.
- Gizmos: ImGuizmo translate (world) and rotate (local). The fog volume has
  its own Move/Size/Turn modes. No scale for lights or prefabs.
- Lights (point, spot, rect) edit intensity, colour, radius, cone and size
  only in the panel.
- Fog volumes have about 37 controls in one list. `fog_presets.json` holds
  8 presets, which can be applied but not dropped in as objects.
- Light prefabs live in `pc/assets/prefabs/*.json` plus `pc/user/prefabs`,
  placed from the Place Actors tab.
- Undo (`editor_undo.cpp`) diffs snapshots per frame and names its steps.
  Snapping lives in `editor_overlays.cpp`.

## Decisions

1. **Conventions:** Unreal. Houdini where it adds something (the value
   ladder).
2. **Selection follows you:** clicking an object in the viewport selects it,
   scrolls the Outliner to it and highlights it, and Details shows it.
3. **Details:** one panel shows the selection, in collapsible categories
   with a search box. A lock pins it to one object. Room-wide settings
   (Atmosphere, Post-processing, Graphics) move to their own tabs.
4. **Value ladder:** middle-drag on any number field scrubs it in steps of
   0.01 / 0.1 / 1 / 10 (Houdini).
5. **Scale (R):** changes shape only, never brightness. See the table below.
6. **Light handles:** on the selected light only: an intensity ring, a
   radius/range dot, and a colour swatch that opens the colour popover.
   Plus per-type handles, below.
7. **Colour popover:** a Temperature tab (Kelvin strip with named stops,
   plus a magenta-green tint) and a Colour tab (hue wheel and value). Shared
   by both: a room palette (colours used by lights in this room), recent
   colours, an eyedropper that picks from the scene, and Hex/RGB fields.
   It remembers the last tab per light type.
8. **Fog volumes:** preset first. Unreal-style collapsible groups with
   Basics open. Search covers all groups. A per-value ↺ resets that value
   to the preset. Volumes are clickable in the viewport.
9. **Library:** one dockable library for light prefabs, fixtures, fog volumes
   and (later) emitters. It has a list/grid toggle, rendered thumbnails with a
   type icon on each tile, category chips, favourites, "Mine" and search.

## Scale semantics

| Object | R changes | Viewport shape |
|---|---|---|
| Point light | radius (uniform only) | radius sphere |
| Spot light | range along the axis; the cone angle has its own handle | cone |
| Rect light | width × height (non-uniform), and range on the forward axis | rectangle + range line |
| Fog volume | size on X/Y/Z (today's "Size" mode becomes R) | box / ellipsoid |
| Prefab | uniform size; light positions and radii scale together, intensities don't | bounds box |

Scaling never changes intensity, for any light or prefab.

## Viewport handles per type

All handle types share these behaviours:
- Ctrl gives fine steps and Shift snaps to round values.
- The value shows while dragging, and Esc cancels.
- Each drag is one undo step (`pc_undo_hint` names it).
- Handles hide while the camera flies.
- Only the selection shows handles.

- **Point:** intensity ring (log scale), radius dot, swatch, and a
  shadows toggle on the ring.
- **Spot:** ring, swatch, a range dot on the axis, an outer-cone handle on
  the cone edge, and an inner (soft) cone handle. "Aim at surface": click a
  point to point the spot at it.
- **Rect:** ring, swatch, edge handles for width and height (Alt keeps it
  symmetric) and a range dot along its facing.
  - **Barn doors:** angle and length, dragged as flaps on the rect's edges.
    This needs renderer support (a separate task; the handles drive its
    parameters).
  - Later: "fit to window", which sizes and places the rect on a clicked
    window pane (ties into the emissive and window-shaft work).
- **Sun/moon:** ring, swatch, and the disc dragged across a sky dome
  (azimuth and elevation), replacing the rotate gizmo.
- **Fog volume:** density ring, an inner outline for the soft edge, swatch,
  and R for size. "Drop to floor" and "Fit to floor area" stay.

## Architecture

Extend the existing editor (Dear ImGui + ImGuizmo). Add two shared pieces so
each object kind doesn't need its own UI code.

### Property schema (`pc/src/editor_schema.*`)

Each object kind registers its fields once. A field has:
- a name and label;
- a category;
- a type (float, int, bool, colour, enum, vec3, angle);
- range, unit, log scale and step;
- a default, and for kinds with presets, the preset value;
- get/set accessors.

Kinds: point, spot and rect light, sun, moon, fog volume, prefab instance
(its exposed params), glow override, and emissive mask once that lands.

Uses:
- Details draws from the schema: categories, search, ↺ reset to
  default/preset, the value ladder and tooltips (unit, range).
- Undo names come from the field label.
- RPC can list and set fields by name, for tests and tools.
- A schema lint (every field has a range and a default) runs in the tests.

### Selection model (`pc/src/editor_selection.*`)

One selection set of typed references (kind + id), shared by the viewport,
Outliner, Details and RPC. It provides:
- viewport picking for all kinds, including fog volumes (ray against the
  box/ellipsoid) and the sun;
- box select;
- Shift to add, Ctrl to toggle;
- hide/unhide (H, Ctrl+H), stored as editor state, not saved into lighting
  data;
- copy/paste/duplicate (Ctrl+C/V/D) through the layer item format;
- "frame selection" (F, exists).

Selection changes request an Outliner scroll (`SetScrollHereY` on the row)
and switch Details to the selection.

### Handle layer (`pc/src/editor_handles.*`)

Handles are drawn in screen space from world positions with ImGui draw lists,
like today's icons. Each handle has:
- a hit area;
- a drag mapping (screen delta to value, linear or log);
- the shared modifiers.

Each kind describes its handles in terms of schema fields, so a handle drag
changes exactly one field. ImGuizmo stays for move/rotate/scale. R becomes
ImGuizmo SCALE mapped through the scale table above.

### Library (`pc/src/editor_library.*`)

- **Sources:** light prefabs (`pc/assets/prefabs`, `pc/user/prefabs`), fog
  presets (`fog_presets.json` plus user presets) and fixtures. Each entry has
  a kind, a name, tags and a thumbnail.
- **Thumbnails:** rendered by our renderer in a neutral test scene, cached
  under `pc/build/` and never committed. The type icon is drawn over the
  tile.
- **Placing:** drag into the viewport and the item lands on the surface under
  the cursor, using the existing `v.pick` world position and its surface
  normal. Fog sits on the floor; wall lights face out from the wall. The
  drop is one undo step and the new item is selected.
- **Saving:** "Save as prefab" from any selection writes to the user
  library.

### Data

There's no change to lighting layer formats beyond new fields: barn doors
(rect) and the sun/moon's azimuth and elevation, if they aren't already
stored that way. Editor-only state (hidden objects, favourites, library view
mode, last colour tab) is saved in user settings, not in lighting data.

## Phases

1. **A: Selection + Details.** The selection model, the schema for lights,
   prefabs and fog volumes, Details from the schema, Outliner sync, fog
   volume picking, hide/isolate, copy/paste, and the value ladder.
2. **B: Transform.** W/E/R with scale semantics, and a world/local toggle on
   the toolbar.
3. **C: Light handles** (handle layer, per-type handles, colour popover) and
   **D: volumes** (grouped Details, viewport handles) can run in parallel
   once A lands.
4. **E: Library.**
5. **Separate tasks:** barn doors in the renderer, "fit to window" (after
   emissive), and dockable panels.

## Testing

Headless RPC tests, extending the existing editor RPCs (`editor_input`,
`editor_camera`, `editor_ui`, `editor_undo`):

- **Selection:** clicking each kind selects it; the Outliner's scroll target
  and Details' subject follow; fog volumes are pickable; hide/unhide; copy,
  paste and duplicate produce the expected items.
- **Schema:** every field has a range and a default; RPC get/set round-trips;
  ↺ restores the default or preset value.
- **Handles** (simulated mouse drags): each drag changes exactly one field
  by the expected amount, Esc restores it, and the drag is one undo step.
- **Scale:** R on each light type changes radius, range or size and never
  intensity. Prefab scale moves the lights and radii together.
- **Library:** dropping onto a floor and a wall lands on the surface with the
  expected orientation; "Save as prefab" round-trips.
- **Value ladder:** a simulated middle-drag changes values by the chosen
  step.
- The existing editor, lighting and atmosphere suites stay green.
