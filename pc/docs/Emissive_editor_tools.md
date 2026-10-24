# Emissive texture tools

The mask chooses which texture texels glow; tint and strength set their emission.
Spatial tools write a user-layer `materials/<key>/emissive.png`, while Select colour
edits the entry's CLUT-index filter (which intersects its shapes and painted map).

In the texture view toolbar:

- **Pencil (B)** writes exactly one texel at full value.
- **Brush** uses radius in texels, hardness and opacity.
- **Eraser (E)** clears one texel.
- **Rectangle (R)** drags an additive rectangle, including both endpoint texels;
  **Alt** subtracts instead.
- **Fill (G)** fills the four-connected region with the clicked CLUT index,
  not disconnected regions with the same index.
- **Select colour (W)** toggles the clicked index in the indices filter, selecting
  that index throughout the texture. Direct-colour textures have no CLUT and
  cannot use this tool.
- **Remove (X)** swaps add/remove for spatial tools; right mouse also removes.
  **Clear mask** and **Invert mask** operate on the entire painted map, not the
  CLUT/colour filters: an inverted texel excluded by a filter stays excluded.

Shortcuts are local to the hovered texture canvas and do not fire while typing.
Wheel zoom, middle-drag pan and Fit are unchanged. **Ctrl+Z / Ctrl+Y** use the
ordinary editor transaction history for painted values as well as entry fields;
map restoration rewrites the user PNG. Mouse-held edits are grouped by the
existing editor transaction mechanism. A missing map restored by undo is an
all-zero user PNG, rather than deletion of the file.

Starting spatial painting rasterizes shared rectangles/polygons into a map for
every matched texture, preserving other matches before removing shape constraints.
Painting can then add disjoint areas outside the old shapes; CLUT/colour filters
remain independent. All matches need valid declared sizes, otherwise conversion
is refused rather than silently making other textures dark.

Window entries expose `rect_light` intensity, radius, offset, pattern, aperture
mode, elevation, spread and grid resolution. User-layer pattern controls update
Vulkan lights live; intensity, radius and offset require saving and regenerating
with `generate_lighting.py --emissive-user <saved-emissive-layer.json>`.
See [Window lights](Emissive.md#window-lights) for the angular model and GL fallback.
Unknown entry, match, mask, scope and surface-object fields are
retained through edit, duplicate, undo, save and reload; unknown nested contents
of `rect_light` are copied verbatim. Known optional fields are serialized from
current state so clearing them cannot revive stale values.

## RPC

Select an entry first, then use the same tool operations as the canvas:

```json
{"action":"ui","tool":"pencil","paint":{"key":"0123456789","x":3,"y":4}}
{"action":"ui","tool":"brush","brush":{"radius":3,"hardness":0.5,"opacity":0.4},"paint":{"key":"0123456789","x":8,"y":9}}
{"action":"ui","tool":"eraser","paint":{"key":"0123456789","x":3,"y":4}}
{"action":"ui","tool":"rectangle","paint":{"key":"0123456789","x":2,"y":2,"x1":8,"y1":9,"subtract":true}}
{"action":"ui","tool":"fill","paint":{"key":"0123456789","x":3,"y":4}}
{"action":"ui","tool":"colour","paint":{"key":"0123456789","x":3,"y":4}}
```

Coordinates are local texels, not page coordinates. The texture must be live and
match the selected entry. `key` may be omitted for the last pick or shown texture;
`subtract` can be set on `ui` as a persistent mode or per `paint` operation.
`ui` replies include the tool and subtract mode, even without an ImGui panel.
For the toolbar's whole-map operations use `ui {paint: {key, operation: "clear"}}`
or `operation: "invert"`; these share the canvas implementation and undo history.
The older `action: paint` bulk-map API remains available and is undoable too.

`pc/tests/emissive_tools_smoke.py` exercises all six tools through RPC, ordinary
undo/redo, PNG contents, and unknown-field/rect-light save/reload. It uses scratch
lighting/settings and a copy of the canonical test memory card, never real user
lighting. `emissive_entries_smoke.py` checks real SDL rectangle and brush gestures
at different zooms in addition to entry operations.
