# Artist tools phases D and E (and the follow-ups) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Finish the artist tools of `pc/docs/plans/2026-10-15-artist-tools-design.md`: phase D (fog volumes preset-first, in Unreal-style groups, with viewport handles: a density ring, the soft edge's inner outline, a colour swatch, R for size; the four per-source disturb weights as "Use global" plus a slider), phase E (one library for basic lights, light prefabs, fixtures, fog volume presets and later emitters: list or grid with thumbnails our light model renders in a neutral scene and caches under `pc/build/`, a type icon per tile, kind and category chips, favourites, Mine, search, drag into the view onto the surface under the cursor, "Save as prefab" from the selection), and the follow-ups (emissive Delete / Duplicate with a confirm and undo, the sun as a real schema kind, the phase A review leftovers, the glow tooltip's format warning, a lint warning for room post profiles that set the tonemapper, and "fit rect light to window").

**Architecture:** Pure parts stay ImGui-free and are tested by CTest probes: `editor_math` gains the fog footprint fit and the window pane fit (`editor_tools_probe`); the new `library_model.*` (entries, filters, favourites) and `library_thumbs.*` (the thumbnail renderer: our renderers' light falloff, spot edge and barn doors from `light_shape.h`, a fog volume's soft box, on a neutral floor and wall; the cache file) are tested by a new `library_probe`. The schema (`editor_schema.*`) gains a kind header row and "use global" fields, Details draws both; the handle layer (`editor_handles.cpp`) learns fog volumes and `editor_volume_handles.cpp` builds their handles from the fog schema. `editor_library.*` owns the sources (one per kind, registered with `rood_library::add_source`, so the emitters branch only adds a source), placing (through the existing `rood_lights::place_*` drag and drop: `place_commit` hands non-basic items to the library), the thumbnail atlas (one `ImTextureData`, as `prompt_glyphs.cpp`), the panel (the side column's second tab, which it replaces Place Actors with, or a floating window), Save as prefab and RPC `editor_library`. Fog presets gain a user layer (`<user prefab dir>/fog/<id>.json`) and `rood_atmo::place_volume`. The emissive user layer joins undo snapshots.

**Tech Stack:** C++17, Dear ImGui 1.93 WIP (docking branch, `ImTextureData` user textures) + ImGuizmo (vendored), SDL3, the project's JSON (`prefab::Json`), dev RPC (`pc/src/dev_rpc.c`), Python 3 headless RPC tests (`pc/tests/run.py`, `rood_rpc.py`, numpy where pixels are read), CTest probes (`pc/CMakeLists.txt`), `pc/tools/lighting_lint.py`.

---

## Ground rules for every task

- No changes under `src/` (the decompilation). Everything here is `pc/` (plus `lighting/README.md` and `pc/docs/`).
- Dear ImGui and ImGuizmo stay; nothing new is vendored; no game data or game-derived asset is committed. Thumbnails are cached under `pc/build/library_thumbs/` (ignored by git: `/pc/build*/`), test output under the test's own directory.
- Editor-only state goes in the user settings with `editor_` keys (`editor_library_view`, `editor_library_docked`, `editor_library_window`, `editor_library_favourites`, `editor_fit_window_offset`, `editor_fit_window_kelvin`): `editor_undo.cpp` treats `editor_*` keys as preferences, never undo steps.
- Look/feel parameters are data and tunable (settings key, a dev-tools control, RPC), never constants in code: the fit tool's offset and colour are settings keys shown in its popup and set by RPC `graphics`; the thumbnail scene's look is the renderer's (a versioned constant set in one table, `rood_thumbs::LOOK`, exposed through RPC `editor_library {action "thumbs", look}` for tests only) and changing it renames the cache files. New effects are off or neutral by default.
- Every editor action that changes room data names its undo step (`pc_undo_hint`, or `pc_undo_begin` for a drag).
- Committed files never mention other game ports by name and never contain absolute home-directory or temp-directory paths. The heavy-work lock is written `$ROOD_HEAVY_LOCK` (the session's rules give its path; export it before Task 1).
- Heavy work (builds, game tests) always runs under `flock "$ROOD_HEAVY_LOCK"`, tests at `-j 2`. No step creates artificial CPU load (no busy loops, stress tools or extra parallel builds), not even to reproduce a flake; a test waits only with `game.run(n)` (vsyncs) or bounded RPC polls. Kill only by PID; leave no background process.
- Tests use the pinned card `pc/build/test-card1.mcd` (or temp copies); they never read or write `pc/save`, `pc/user` or `pc/lights` (scratch `ROOD_LIGHTS`, `ROOD_SETTINGS`, `ROOD_PREFABS` and `ROOD_LIBRARY_THUMBS` under the test's output directory; user fog presets live under `ROOD_PREFABS`, so they follow it).
- The main checkout is read-only: build and test only in the worktree.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Do not merge into main and do not push.
- Windows: `windows.h` is force-included on MinGW (`pc/src/win_compat.h`), so no new identifier may be a Windows macro or typedef (`near`, `far`, `small`, `interface`, `DELETE`, `FLOAT`, `IGNORE`, `ERROR`). New enums are prefixed. `--target windows` must still compile (Task 23).

## Decisions taken where the brief leaves room

1. **Fog volume groups**: Basics (open: Enabled, Shape, Density, Colour, Soft edge, Fill light), Look (closed: Opacity, Brightness, Forward scattering, Ambient, Glow, Glow at its base, Taper, Wisps), Motion & interaction (closed: Characters part it, Strength, Recovery time, Footprint, Trail, Wind, Swirl, the four disturb weights), Advanced (closed: Detail, Billow size, Self-shadow), Transform (open: Position, Size, Yaw; Unreal keeps Transform open). Categories show in that order (Details draws them in the order their first field appears).
2. **Preset first**: a kind may have a `header` row that Details draws above its search box; the fog volume's is its preset row (pick, Reset to preset, Save as preset, Reload), moved out of the tools under its fields. The per-value ↺ already restores the preset's value (`base` / `base_source` from the preset): Task 11's test checks it per group.
3. **Disturb weights**: a schema field may follow a global setting (`Field::uses_global` / `set_uses_global`). Details draws a "Use global" box before such a value and disables the slider while it is ticked (the slider then shows the global weight). The fields' range becomes 0..2 (default 1, the global weights' default for Ashley); the data format keeps -1 = global. RPC `editor_schema set` takes `"global"` (or `null`) for such a field; `get` reports `"global": true|false`. Unticking starts from the global weight now, so the picture does not change until the slider moves. Reset (↺) restores the preset's weight, which may be "global".
4. **Volume handles**: the density ring is the light's intensity ring on the volume's centre (100 pixels right doubles it, Ctrl a tenth, Shift 1-2-5), working in units of 1e-3 so `log_drag`'s floor of 0.01 does not clamp a density of 0.004. The soft edge's dot sits on the inner outline (the full-density core: half size x (1 - edge)) along whichever of the volume's x or z axes is more across the view; dragging it out lowers `edge`. The swatch opens the colour popover on `color` (its albedo), which remembers its own tab (a fifth slot in `editor_colour_tabs`). R is the existing size gizmo (Phase B); the gizmo is now disabled while a volume handle has the mouse, as for lights.
5. **The library replaces Place Actors**: the side column's second tab becomes "Library" (Shift+1, Window > Library, RPC `editor_ui {tab: "library"}`; `"place"` stays an alias). "Dockable" here means docked in that tab or floating as its own movable, resizable window (a pop-out button; `editor_library_docked`, `editor_library_window x y w h`), not Dear ImGui's docking system, which the fixed side column does not use. The toolbar's "+ Place" menu stays (lights and light prefabs at the view's centre).
6. **Sources, one per kind**: `light` (point, spot, rect), `prefab` (`pc/assets/prefabs`, the user's `pc/user/prefabs`), `fixture` (the fixture catalog's classes that are lights: dropping one on a room fixture lights it as the Light fixture tool does, with the class's prefab and parameters; elsewhere it places an actor of that prefab with the class's parameters as overrides), `fog` (the built-in presets and the user's), `emitter` (none here: the emitters branch registers a source with `rood_library::add_source({kind = "emitter", ...})`; the library lists every registered kind in `KINDS` order, unknown kinds after them, and hides a kind chip without entries). The plan's Task 19 includes the snippet that branch adds.
7. **Thumbnails** are rendered on the CPU by `library_thumbs.cpp` with our renderers' light model (the falloff `(1 - (d/r)^2)^2`, N.L, the spot edge and the rect's front face and barn doors from `light_shape.h`, which gains a C `pc_light_falloff` the GLSL copies match), and a fog volume's soft box or ellipsoid marched in 40 steps with its density, edge, albedo, fill and glow. One code path for OpenGL and Vulkan, no GPU readback, deterministic (identical bytes every run), headless. The neutral scene: a grey floor with a 64-unit grid and a back wall, the item at the centre, a camera from the front and above framing the item's reach. 96 x 96 pixels, cached as `<kind>-<id>-<hash>.rgba` (a 16-byte header, then RGBA) under `pc/build/library_thumbs/` (`ROOD_LIBRARY_THUMBS`), the hash covering what the picture depends on (the item's lights or preset fields, the scene's look, the size, a version); at most two are rendered per frame while the grid shows. The type icon is drawn over the tile by ImGui, never baked in.
8. **Drops**: the library uses the existing drag and drop (`place_drag_*`, `editor_place`), so snapping, the lit ghost, Escape and the RPC all work for every kind. A fog volume dropped on a floor rests its bottom on that floor and keeps its footprint inside the floor's level area around the drop point (each size at most the area's, the centre moved in); dropped on a wall or a ceiling it goes to the floor below the drop point. A spot or rect light dropped on a wall aims out of it (exists); a prefab with `align: "normal"` faces out (exists); an upright prefab (`align: "up"`) dropped on a wall now turns to face out of it (yaw only). One undo step "Place <name>", the new object selected.
9. **Save as prefab** (Library toolbar, RPC): selected lights become a light prefab (the existing Create prefab: they are replaced by an actor of it); one light actor becomes a prefab with its values (the existing Save as prefab: the actor uses it); one fog volume becomes a user fog preset (`<user prefab dir>/fog/<id>.json`; the volume then uses it). Any other selection is refused with a message saying what can be saved. "Mine" lists the user's prefabs and fog presets. The Atmosphere tab's "Save as preset..." keeps writing the shipped `fog_presets.json` (an authoring tool) and says so in its tooltip.
10. **Emissive Delete / Duplicate**: Duplicate copies any entry (curated or yours) into your layer under a new id with " copy" after its name and selects it; Delete removes your entry after a confirm popup (Yes / Cancel); a curated entry has no Delete (it ships; Disable it, or edit it), an override keeps "Revert to curated", also behind the confirm. Undo covers the emissive user layer from now on (a new snapshot part, counted like the settings: changes the panels or an editing RPC made), so Duplicate, Delete, Revert and every other emissive edit are undoable. RPC `emissive` gains `duplicate {id}`, `select {id}` and `ui` (the panel's button rectangles, for tests).
11. **The sun's Details**: a schema kind `sun` (object `KIND_SUN`, id `env:sun`, index 0) with Enabled, Azimuth, Elevation, Colour, Intensity, Angular size and Casts shadows, ranges as the Atmosphere tab's sliders; its tools under the fields: a note on the dome handles and a button to the Atmosphere tab (the sky). The handles keep their own undo names ("Edit Sun direction", "Edit Sun intensity").
12. **Ctrl+click on a glow places**: Ctrl+click on a glow that is not selected adds the Library's current item at the surface there (as Ctrl+click on empty space; glows mostly sit on the very surfaces one lights); Ctrl+click on a selected glow still takes it out of the selection (as fog volumes already do). Documented in the help rows and the README.
13. **A glow selection does not survive a room change**: `rood_scene::frame` notices the room changing and drops glow references (and a Details lock on a glow). Lights, actors and fog volumes already drop out (their ids belong to the room).
14. **Value ladder / handle drag when the editor closes**: closing the editor (Escape, F1, RPC `editor_ui visible false`) ends a value ladder or handle drag in progress, keeping its value (Unreal commits on focus loss), so its undo transaction closes.
15. **Schema kinds across a prefab library reload**: the per-prefab kinds of an old library serial are retired, not freed, until the next overlay frame (`rood_schema::collect`, called from `rood_scene::frame`), so a `Kind*` / `Field&` taken earlier in the frame stays valid if the library reloads mid-frame ("Apply to prefab" in an actor's tools, a file change picked up by `poll`).
16. **Tonemapper lint**: `lighting_lint.py` warns (not an error: the game still applies it) when a `post` item of a room file sets `tonemapper` or `tonemap_white`, or names a `preset` whose `pc/assets/post/<name>.json` values set them: the tonemapper is the player's choice. `pc/docs/Post_Processing.md`'s example drops its tonemapper.
17. **Fit rect light to window**: a click on a room surface takes the "pane": the room triangles connected to the clicked one in its plane (normals within 8 degrees, within 2 units of it) with its texture page and CLUT, up to 256 triangles; the rect light is centred on the pane's extent in its plane, `editor_fit_window_offset` units (default 2) in front of it facing out toward the camera's side, width and height the extent along the light's own frame (`rood_math::light_frame`), so the rect lines up with its edge handles. With a rect light selected the click fits it (its colour and intensity kept); otherwise a new rect light is made in a daylight colour (`editor_fit_window_kelvin`, default 5600 K). Armed from a rect light's Details tools ("Fit to window"), the toolbar's "+ Place" menu ("Rect light on a window...") or RPC `editor_handles {action "fit_window"}`; Escape disarms; one undo step "Fit rect light to window". It works on any flat textured surface, so it also suits doors and pictures.
18. **The glow tooltip's format warning**: the format string in `lights.cpp`'s glow Outliner tooltip has seven conversions for eight arguments; the missing `%s` (the muted-by-type note) is added.

## File structure

Created:

| File | Responsibility |
|---|---|
| `pc/src/library_model.h`, `.cpp` | The library's entries (item, kind, name, category, description, tags, mine, thumbnail description), filters (kind, category, search words, favourites, mine), kind labels and order, category lists, the favourites settings line. No ImGui. |
| `pc/src/library_thumbs.h`, `.cpp` | The thumbnail renderer (`rood_thumbs::Scene`: lights as `prefab::EvalLight`, an optional fog volume; `render`), its look table, the cache's name (hash) and file (read, write). No ImGui. |
| `pc/src/editor_library.h`, `.cpp` | Sources (`add_source`; light, prefab, fixture, fog built in), placing library items, the drop ghost of a fog volume, the thumbnail atlas, the panel (grid / list, chips, favourites, Mine, search, docked or floating), Save as prefab, settings lines, RPC `editor_library`. |
| `pc/src/editor_volume_handles.cpp` | A fog volume's handles: the density ring, the soft edge dot and its inner outline, the colour swatch. |
| `pc/src/editor_fit_window.cpp`, `.h` | "Fit rect light to window": arming, the click, the pane from the room's triangles, the light placed and sized, its RPC. |
| `pc/tests/library_probe.cpp` | CTest probe of `library_model` and `library_thumbs` (and `pc_light_falloff`). |
| `pc/tests/editor_library_smoke.py` | Headless test: sources, filters, favourites, thumbnails, drops on a floor and a wall per kind, a real mouse drag from a tile, Save as prefab round trips, the panel's views. |
| `pc/tests/emissive_entries_smoke.py` | Headless test: emissive Duplicate and Delete (RPC and the panel's buttons with the confirm), one undo step each. |

Modified:

| File | Change |
|---|---|
| `pc/src/lights.cpp` | The glow tooltip's format; Ctrl+click on a glow; library items in `parse_item` / `place_compute` / `place_commit` / the drag's ghost; upright prefabs on walls face out; ImGuizmo stays disabled under a handle through the fog gizmo; the fit tool's click; a rect light's "Fit to window" button. |
| `pc/src/editor_scene.h`, `.cpp` | Glows dropped on a room change; `ctrl_places`; `click_at` places on Ctrl+click on a glow; `rood_schema::collect` each frame. |
| `pc/src/editor_schema.h`, `.cpp` | `Kind::header`; `Field::uses_global`, `set_uses_global`, `global_tooltip`; `apply_global`; the sun kind; objects by kind index (the sun); retired actor kinds and `collect`; RPC `set` "global", `get` "global", `reload_probe`. |
| `pc/src/editor_details.h`, `.cpp` | The header row, the "Use global" box, categories and their open state in RPC `ui`, the sun's note gone, `ladder_finish`. |
| `pc/src/editor_handles.h`, `.cpp` | Fog volumes (build, restore from the press's fields, extras); `Drag::fields` / `field`; `colour_swatch` shared; `finish`; RPC `fit_window`, the fit state. |
| `pc/src/editor_light_handles.cpp` | `swatch` becomes `rood_handles::colour_swatch`. |
| `pc/src/editor_colour.cpp`, `.h` | Fog volumes (a fifth tab slot); the RPC opens on a volume. |
| `pc/src/atmosphere.cpp` | The fog schema's groups, header, disturb fields; user fog presets; `fog_presets`, `volume_drop`, `place_volume`, `save_user_preset`; `floor_area` shared with Fit to floor area. |
| `pc/src/environment_internal.h`, `environment.cpp` | The new `rood_atmo` functions; `rood_env::sun_schema_kind`, `sun_details_extra`. |
| `pc/src/emissive_internal.h`, `emissive.cpp` | `user_text`, `set_user_text`. |
| `pc/src/emissive_editor.cpp` | Duplicate and Delete with the confirm; RPC `duplicate`, `select`, `ui`. |
| `pc/src/editor_undo.cpp` | The emissive user layer as a snapshot part (`EMISSIVE`). |
| `pc/src/editor_math.h`, `.cpp` | `fit_footprint`, `PaneTriangle`, `Pane`, `fit_pane`. |
| `pc/src/light_shape.h` | `pc_light_falloff`. |
| `pc/src/light_actors.cpp` | Place Actors' panel body becomes the library's; `draw_icon` knows `fog`, `fixture`, `emitter` items; `item_colour` for library items. |
| `pc/src/dev_overlay.cpp` | The Library tab (and floating window), Window menu, Shift+1, the toolbar's fit item, Escape for the fit tool, closing the editor ends drags, help rows. |
| `pc/src/dev_rpc.c` | Routes `editor_library`; `editor_ui {tab "library"}`; `emissive` editing actions and `editor_library` are undoable edits. |
| `pc/src/project_settings.cpp` | The library's and the fit tool's settings lines. |
| `pc/tools/build_boot.py` | Compiles the six new sources. |
| `pc/CMakeLists.txt` | `library_probe`; `editor_tools_probe` unchanged sources. |
| `pc/tools/lighting_lint.py` | The tonemapper warning. |
| `pc/tests/run.py` | Registers `editor_library_smoke`, `emissive_entries_smoke`. |
| `pc/tests/editor_tools_probe.cpp`, `editor_selection_smoke.py`, `editor_schema_smoke.py`, `editor_handles_smoke.py`, `lighting_layers_test.py` | New checks. |
| `lighting/README.md`, `pc/assets/prefabs/README.md`, `pc/docs/Post_Processing.md`, `pc/docs/Emissive.md` | The library, user fog presets, the fit tool, Ctrl+click on glows, the tonemapper rule, emissive Duplicate / Delete. |

---

### Task 1: Worktree and baseline

**Files:** none changed.

- [ ] **Step 1: Branch in a worktree and copy the data in**

Use superpowers:using-git-worktrees. From the main checkout (read-only: nothing is built or written there):

```bash
MAIN=$(git rev-parse --show-toplevel)
git worktree add .claude/worktrees/artist-tools-de -b claude/artist-tools-de main
cd .claude/worktrees/artist-tools-de
cp -a "$MAIN/build" build
cp -a "$MAIN/data" data
cp -a "$MAIN/disks" disks
mkdir -p pc/build
cp "$MAIN/pc/build/test-card1.mcd" pc/build/
test -d "$MAIN/pc/build/nrd" && cp -a "$MAIN/pc/build/nrd" pc/build/
```

Expected: a new worktree on branch `claude/artist-tools-de`; `build/`, `data/`, `disks/` and `pc/build/test-card1.mcd` are copies (never symlinks).

- [ ] **Step 2: Configure CTest**

```bash
test -f pc/build/build.ninja || flock "$ROOD_HEAVY_LOCK" cmake -S pc -B pc/build -G Ninja
```

Expected: `pc/build/build.ninja` exists.

- [ ] **Step 3: Build the test binaries**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tee pc/build/baseline-build.log | tail -3
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64 | tail -3
```

Expected: both exit 0 with a `Built …` line and no `error:` line. Keep `pc/build/baseline-build.log` (Task 2 reads it).

- [ ] **Step 4: Record the baseline**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest lighting_lint editor light_actors atmosphere fog emissive fixture lighting
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan editor light_actors atmosphere fog emissive
```

Expected: tables ending in `N/N passed` (or a known failure list). Write down any suite that already fails; later tasks must not add failures.

---

## Follow-ups

### Task 2: The glow Outliner tooltip's format warning

**Files:**
- Modify: `pc/src/lights.cpp` (the glow rows' tooltip, about line 2234)

- [ ] **Step 1: See the warning**

```bash
grep -n "too many arguments for format\|Wformat-extra-args" pc/build/baseline-build.log
```

Expected: one warning pointing at `lights.cpp` and the `"Glow %u, %s\n%d glowing triangles this frame%s%s%s%s"` format (seven conversions, eight arguments: the last, the muted-by-type note, has no `%s`).

- [ ] **Step 2: Add the missing conversion**

In `pc/src/lights.cpp`, replace:

```cpp
            ImGui::SetTooltip("Glow %u, %s\n%d glowing triangles this frame%s%s%s%s", g.id, g.key, g.triangles,
```

with:

```cpp
            ImGui::SetTooltip("Glow %u, %s\n%d glowing triangles this frame%s%s%s%s%s", g.id, g.key, g.triangles,
```

- [ ] **Step 3: Build and see it gone**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle 2>&1 | tee pc/build/task2-build.log | tail -2
grep -c "too many arguments for format" pc/build/task2-build.log
```

Expected: the build exits 0; the count is `0`. (If the build skipped `lights.cpp` as cached, touch it first: `touch pc/src/lights.cpp`.)

- [ ] **Step 4: Commit**

```bash
git add pc/src/lights.cpp
git commit -m "$(cat <<'EOF'
Glow Outliner tooltip: the muted-by-type note has its %s (format warning)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Lint: a room post profile must not set the tonemapper

**Files:**
- Modify: `pc/tools/lighting_lint.py` (`check_values`, the `post` branch, line 138), `pc/tests/lighting_layers_test.py` (the bad pack, line 200), `pc/docs/Post_Processing.md` (the example at line 182)

- [ ] **Step 1: Write the failing check**

In `pc/tests/lighting_layers_test.py`, in the `bad / "default/rooms/zone09_room17.txt"` text, after the line `env sun zenith=1,1,1` add two lines:

```
post room exposure=0.5 tonemapper=aces
post cutscene preset=cinematic
```

and in the `expect` list, after `"'zenith' is not a field of the sun"` add:

```python
              "post:room: sets tonemapper", "post:cutscene: its preset cinematic sets tonemapper",
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/lighting_layers_test.py --probe pc/build/lighting_probe
```

Expected: `FAIL: lint reports: post:room: sets tonemapper` and `FAIL: lint reports: post:cutscene: its preset cinematic sets tonemapper` (if the probe is not built yet: `flock "$ROOD_HEAVY_LOCK" ninja -C pc/build lighting_probe` first).

- [ ] **Step 3: The warning**

In `pc/tools/lighting_lint.py`, after `def fog_presets():` and its body add:

```python
TONEMAP_KEYS = ("tonemapper", "tonemap_white")


def post_preset_tonemap(name):
    """The tonemapper keys a post preset (pc/assets/post/<name>.json "values") sets."""
    try:
        values = json.loads((ROOT / "pc/assets/post" / f"{name}.json").read_text()).get("values", {})
    except (OSError, ValueError):
        return []
    return [k for k in TONEMAP_KEYS if k in values]
```

and replace the `post` branch of `check_values`:

```python
    if kind == "post":
        name = e.id.split(":", 1)[1]
        if not (name in ("room", "cutscene") or (name.startswith("event.") and name[6:].isdigit())):
            report("error", path, e.line, f"{e.id}: post profiles are post:room, post:cutscene and post:event.<id>")
        return  # the fields: pc/src/post_profile.cpp checks them (and logs unknown ones)
```

with:

```python
    if kind == "post":
        name = e.id.split(":", 1)[1]
        if not (name in ("room", "cutscene") or (name.startswith("event.") and name[6:].isdigit())):
            report("error", path, e.line, f"{e.id}: post profiles are post:room, post:cutscene and post:event.<id>")
        # The tonemapper is the player's choice (Graphics > Post-processing, settings post_preset): a room's
        # profile that sets it overrides theirs. A warning: the game still applies it.
        for key, _ in fields:
            if key in TONEMAP_KEYS:
                report("warning", path, e.line, f"{e.id}: sets {key}: the tonemapper is the player's choice; "
                                                f"leave {key} out of room profiles")
        preset = dict(fields).get("preset")
        if preset:
            for key in post_preset_tonemap(preset):
                report("warning", path, e.line, f"{e.id}: its preset {preset} sets {key}: the tonemapper is the "
                                                f"player's choice; name a preset without it, or set the fields here")
        return  # the fields: pc/src/post_profile.cpp checks them (and logs unknown ones)
```

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/lighting_layers_test.py --probe pc/build/lighting_probe
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/lighting_lint.py | tail -1
```

Expected: every line `ok:` and the final PASS line; the repository's own lighting packs still pass the lint (no new warning: none of their `post` items set the tonemapper).

- [ ] **Step 5: The docs**

In `pc/docs/Post_Processing.md`, change the example line `post cutscene tonemapper=aces exposure=0.5` to `post cutscene exposure=0.5 vignette=0.3`, and after the paragraph that introduces the example add:

```markdown
The tonemapper (`tonemapper`, `tonemap_white`) is the player's choice: room
profiles leave it out. `pc/tools/lighting_lint.py` warns when a room's
`post` item sets either, or names a preset whose values set them.
```

- [ ] **Step 6: Commit**

```bash
git add pc/tools/lighting_lint.py pc/tests/lighting_layers_test.py pc/docs/Post_Processing.md
git commit -m "$(cat <<'EOF'
Lighting lint: warn when a room post profile (or its preset) sets tonemapper or tonemap_white (the player's choice)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 4: A glow selection does not survive a room change

**Files:**
- Modify: `pc/src/editor_scene.cpp` (`rood_scene::frame`, line 251), `pc/tests/editor_selection_smoke.py` (a new last section)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_selection_smoke.py`, add to the module docstring's list:

```
- room_change: a glow (and a Details lock on it) selected when the room
  changes is dropped; nothing of the old room stays selected (the last
  section: it leaves the save room for Workshop "Metal Works", 45/0);
```

and before `SECTIONS = [...]` add:

```python
def goto(game, zone, room):
    """RPC room_goto, bounded: True once the game is in (zone, room)."""
    game.call("editor_camera", off=1)
    game.call("room_goto", zone=zone, room=room, enemies=False, scripts=False)
    for _ in range(60):
        game.run(20)
        if game.call("room_goto", action="status")["phase"] in (0, 4):
            break
    game.run(40)
    state = game.state()
    return (state["zone"], state["room"]) == (zone, room)


def check_room_change(s):
    """A glow selected (and locked in Details) when the room changes is dropped."""
    g = s.game
    glows = [x for x in g.call("glows")["glows"] if x["category"] != "effect"]
    if not s.check(glows, "the save room has a glow"):
        return
    g.call("glows", action="select", id=glows[0]["id"])
    g.run(2)
    s.select(action="lock", on=True)
    r = s.select()
    s.check(len(r["selection"]) == 1 and r["primary"].startswith("glow:"), f"a glow is selected ({r['selection']})")
    if not s.check(goto(g, 45, 0), "the game warps to Workshop Metal Works (45/0)"):
        return
    g.call("editor_ui", visible=True)
    g.run(6)
    r = s.select()
    s.check(r["selection"] == [] and r["locked"] is None,
            f"after the room change nothing is selected and Details is unlocked ({r['selection']}, {r['locked']})")
```

and append `check_room_change` to `SECTIONS` (last).

(`editor_select`'s reply carries `locked`: `state_json` in `editor_scene.cpp`.)

- [ ] **Step 2: Build and run it; see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_selection_smoke.py --only room_change
```

Expected: `FAIL: after the room change nothing is selected and Details is unlocked (['glow:…'], 'glow:…')`.

- [ ] **Step 3: Drop glows when the room changes**

In `pc/src/editor_scene.cpp`, replace the start of `rood_scene::frame`:

```cpp
void rood_scene::frame()
{
    track_room();
    rood_selection::set_room(zone, room);
    refresh();
```

with:

```cpp
void rood_scene::frame()
{
    track_room();
    rood_selection::set_room(zone, room);
    /* Another room: its glows are other glows (a key or position of the
     * old room may name one here), so glow references go; lights, actors
     * and fog volumes go by their ids in refresh. */
    static int seen_zone = -1, seen_room = -1;
    if (zone != seen_zone || room != seen_room) {
        if (seen_zone >= 0) {
            rood_selection::retain([](const Ref& r) { return r.kind != rood_selection::KIND_GLOW; },
                rood_selection::FROM_CODE);
            if (rood_selection::locked().kind == rood_selection::KIND_GLOW)
                rood_selection::lock(false);
        }
        seen_zone = zone, seen_room = room;
    }
    refresh();
```

- [ ] **Step 4: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_selection_smoke.py --only room_change
```

Expected: every line `ok:`, `PASS: …`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_scene.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Selection: a room change drops glow references and a Details lock on a glow

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Ctrl+click on a glow places instead of toggling

**Files:**
- Modify: `pc/src/editor_scene.h` (after `click_at`), `pc/src/editor_scene.cpp` (`click_at`, line 349; a new `ctrl_places`), `pc/src/lights.cpp` (the viewport's click, line 1540), `pc/src/dev_overlay.cpp` (help rows), `pc/tests/editor_selection_smoke.py` (`check_modifiers`)

- [ ] **Step 1: Write the failing checks**

In `pc/tests/editor_selection_smoke.py`, at the end of `check_modifiers` add:

```python
    # Ctrl+click on a glow that is not selected places the current item there (the Library's; a point
    # light by default); on a selected glow it still takes it out.
    glows = [i for i, x in o.items() if x["kind"] == "glow" and x["screen"]]
    if s.check(glows, "a glow on screen"):
        xy = o[glows[0]]["screen"]
        s.select(action="clear")
        lights, done = len(s.game.call("lights")["lights"]), s.history()["done"]
        r = s.select(action="click", x=xy[0], y=xy[1], ctrl=True)
        after = s.game.call("lights")["lights"]
        s.check(len(after) == lights + 1 and r["selection"] == [after[-1]["id"]]
                and s.history()["done"] == done + 1 and s.history()["undo"].startswith("Place"),
                f"Ctrl+click on a glow places a light there, selected, one undo step ({r['selection']}, "
                f"{s.history()['undo']!r})")
        s.game.call("editor_undo", action="undo")
        s.select(action="set", ids=[glows[0]])
        r = s.select(action="click", x=xy[0], y=xy[1], ctrl=True)
        s.check(r["selection"] == [] and len(s.game.call("lights")["lights"]) == lights,
                f"Ctrl+click on the selected glow takes it out, places nothing ({r['selection']})")
        # The same with real mouse events (lights.cpp's viewport click).
        s.select(action="clear")
        s.game.call("editor_input", hold=["ctrl"])
        s.game.run(2)
        s.click(xy)
        s.game.call("editor_input", release=True)
        s.game.run(2)
        s.check(len(s.game.call("lights")["lights"]) == lights + 1, "a real Ctrl+click on a glow places too")
        s.game.call("editor_undo", action="undo")
```

(`lights` and `actors` RPC entries carry their full item id, `light:<id>` / `actor:<id>`, as `place()` uses them.)

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_selection_smoke.py --only modifiers
```

Expected: `FAIL: Ctrl+click on a glow places a light there, …` (the glow is toggled into the selection instead).

- [ ] **Step 3: The rule, shared by the RPC and the viewport**

In `pc/src/editor_scene.h`, after the `click_at` declaration add:

```cpp
/* Ctrl+click on `under`: a glow that is not selected is a place to put the
 * Library's current item (Ctrl+click on empty space does the same), not a
 * toggle; true when this click places. */
bool ctrl_places(const Ref& under, bool ctrl);
```

In `pc/src/editor_scene.cpp`, before `Ref rood_scene::click_at` add:

```cpp
bool rood_scene::ctrl_places(const Ref& under, bool ctrl)
{
    return ctrl && under.kind == rood_selection::KIND_GLOW && !rood_selection::contains(under);
}
```

and replace the body of `click_at`:

```cpp
    Ref under = object_at(x, y);
    pc_overlay_view v;
    pc_overlay_pick pick = nullptr;
    if (!under && pc_lights_tools_view(&v, &pick))
        under = volume_at(v, pick, x, y);
    if (under)
        rood_selection::click(under, shift, ctrl, s);
    else if (!shift && !ctrl)
        rood_selection::clear(s);
    publish();
    return under;
```

with:

```cpp
    Ref under = object_at(x, y);
    pc_overlay_view v;
    pc_overlay_pick pick = nullptr;
    if (!under && pc_lights_tools_view(&v, &pick))
        under = volume_at(v, pick, x, y);
    if (ctrl_places(under, ctrl)) { /* the current item at the surface there (it is selected) */
        rood_lights::Placement where;
        if (rood_lights::place_compute(rood_lights::place_item, x, y, 0, where))
            rood_lights::place_commit(rood_lights::place_item, where);
    } else if (under) {
        rood_selection::click(under, shift, ctrl, s);
    } else if (!shift && !ctrl) {
        rood_selection::clear(s);
    }
    publish();
    return under;
```

In `pc/src/lights.cpp`, replace in the viewport's click:

```cpp
    } else if (under) {
        /* Unreal's: a click selects, Shift adds, Ctrl toggles. */
        rood_selection::click(under, io.KeyShift, io.KeyCtrl, rood_selection::FROM_VIEWPORT);
        rood_scene::publish();
    } else {
```

with:

```cpp
    } else if (rood_scene::ctrl_places(under, io.KeyCtrl)) {
        /* Ctrl+click on a glow places the current item there (glows sit on the surfaces one lights). */
        empty_click(io.MousePos.x, io.MousePos.y, false, true);
    } else if (under) {
        /* Unreal's: a click selects, Shift adds, Ctrl toggles. */
        rood_selection::click(under, io.KeyShift, io.KeyCtrl, rood_selection::FROM_VIEWPORT);
        rood_scene::publish();
    } else {
```

In `pc/src/dev_overlay.cpp`, help rows, after the `{ "Lights", "Ctrl+click (empty)", … }` row add:

```cpp
        { "Lights", "Ctrl+click (a glow)", "Place the Library item at the surface there (a selected glow is taken out of the selection instead)" },
```

- [ ] **Step 4: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_selection_smoke.py --only modifiers,picking
```

Expected: every line `ok:`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_scene.h pc/src/editor_scene.cpp pc/src/lights.cpp pc/src/dev_overlay.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
Ctrl+click on an unselected glow places the current item at the surface (a selected glow is still taken out)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Closing the editor ends a value ladder or handle drag

**Files:**
- Modify: `pc/src/editor_details.h`, `pc/src/editor_details.cpp` (`ladder_finish`), `pc/src/editor_handles.h`, `pc/src/editor_handles.cpp` (`finish`), `pc/src/dev_overlay.cpp` (`set_visible`, line 187), `pc/tests/editor_schema_smoke.py` (`check_ladder`)

- [ ] **Step 1: Write the failing check**

In `pc/tests/editor_schema_smoke.py`, at the end of `check_ladder` add:

```python
    # The editor closing mid-drag (Escape, F1, RPC) ends the ladder: its value stays, its undo step closes.
    s.select(action="set", ids=[s.light])
    g.call("editor_ui", details="details")
    g.run(4)
    rect = schema(s, action="ui")["rects"]["intensity"]
    start = centre(rect)
    before = value(s, s.light, "intensity")
    done = s.history()["done"]
    s.mouse(start, button=2, down=True)
    s.mouse([start[0] + 60, start[1]], button=2, down=True)
    mid = value(s, s.light, "intensity")
    g.call("editor_ui", visible=False)
    g.run(3)
    undo = g.call("editor_undo", action="list")
    s.mouse([start[0] + 60, start[1]], button=2, down=False)
    g.call("editor_ui", visible=True)
    g.run(4)
    s.check(not close(mid, before) and close(value(s, s.light, "intensity"), mid) and undo["hold"] == 0
            and not undo["open"] and undo["done"] == done + 1 and undo["undo"] == "Edit Intensity"
            and not schema(s, action="ui")["ladder"]["active"],
            f"closing the editor mid-ladder keeps {mid} and closes its undo step (hold {undo['hold']}, "
            f"open {undo['open']}, {undo['undo']!r})")
    g.call("editor_undo", action="undo")
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py --only ladder
```

Expected: `FAIL: closing the editor mid-ladder keeps … (hold 1, open True, …)`.

- [ ] **Step 3: ladder_finish and finish**

In `pc/src/editor_details.h`, after `bool ladder_cancel();` add:

```cpp
void ladder_finish(); /* the editor closes: a ladder being dragged keeps its value, its undo step closes */
```

In `pc/src/editor_details.cpp`, after `rood_details::ladder_cancel` add:

```cpp
void rood_details::ladder_finish()
{
    if (ladder.active)
        ladder_end(true);
}
```

In `pc/src/editor_handles.h`, after `bool cancel();` add:

```cpp
void finish(); /* the editor closes: a drag keeps its value, its undo step closes; aiming is disarmed */
```

In `pc/src/editor_handles.cpp`, after `rood_handles::cancel` add:

```cpp
void rood_handles::finish()
{
    end(true);
    aim_target = Ref();
}
```

In `pc/src/dev_overlay.cpp`, in `set_visible`, before `/* Keys and buttons held when the editor closes never see their release. */` add:

```cpp
    /* A drag the editor's frames would have ended on release ends now: its
     * value stays and its undo step closes (editor_details.cpp, editor_handles.cpp). */
    rood_details::ladder_finish();
    rood_handles::finish();
```

- [ ] **Step 4: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py --only ladder
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py --only point
```

Expected: every line `ok:`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_details.h pc/src/editor_details.cpp pc/src/editor_handles.h pc/src/editor_handles.cpp pc/src/dev_overlay.cpp pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Closing the editor ends a value ladder or handle drag (its value kept, its undo transaction closed)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Schema kinds stay valid when the prefab library reloads mid-frame

**Files:**
- Modify: `pc/src/editor_schema.h` (`collect`), `pc/src/editor_schema.cpp` (`actor_kinds`, the RPC's `reload_probe`), `pc/src/editor_scene.cpp` (`rood_scene::frame`), `pc/tests/editor_schema_smoke.py` (a `reload` section)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_schema_smoke.py`, add to the docstring's list:

```
- reload: a prefab kind taken before the prefab library reloads in the same
  frame still reads (its old fields, retired until the next frame); after a
  frame the kind is the reloaded one;
```

and before `SECTIONS` (or the section list the file uses) add:

```python
def check_reload(s):
    """A Kind* held across a prefab library reload stays valid until the next frame (editor_schema.cpp collect)."""
    g = s.game
    r = schema(s, action="reload_probe", prefab="candle")
    s.check(r["kept"] and r["name"] == "actor:candle" and r["fields_before"] == r["fields_after"] and r["retired"] >= 1,
            f"the old kind still reads after the reload, retired ({r})")
    g.run(2)
    r2 = schema(s, action="reload_probe", prefab="candle", reload=False)
    s.check(r2["retired"] == 0 and r2["kept"], f"a frame later the retired kinds are gone ({r2})")
```

and add `check_reload` to the file's sections list.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py --only reload
```

Expected: an `RpcError` (`action: kinds, lint, get, set, reset or ui`): the probe does not exist yet.

- [ ] **Step 3: Retire instead of clearing**

In `pc/src/editor_schema.h`, after `std::vector<std::string> lint();` add:

```cpp
/* The prefab kinds a library reload replaced stay readable until this is
 * called (rood_scene::frame, each overlay frame and RPC): a Kind* or Field&
 * taken earlier in the frame never dangles. Returns how many it freed. */
int collect();
```

In `pc/src/editor_schema.cpp`, replace `actor_kinds()`:

```cpp
/* One kind per prefab, rebuilt when the library reloads (its parameters may change). */
std::map<std::string, Kind>& actor_kinds()
{
    static std::map<std::string, Kind> kinds;
    static int serial = -1;
    if (serial != prefab::library().serial) {
        kinds.clear();
        serial = prefab::library().serial;
    }
    return kinds;
}
```

with:

```cpp
/* The kinds of older library serials, kept until collect() (pointers into them stay valid this frame). */
std::deque<std::map<std::string, Kind>>& retired_kinds()
{
    static std::deque<std::map<std::string, Kind>> retired;
    return retired;
}
/* One kind per prefab, rebuilt when the library reloads (its parameters may change). */
std::map<std::string, Kind>& actor_kinds()
{
    static std::map<std::string, Kind> kinds;
    static int serial = -1;
    if (serial != prefab::library().serial) {
        if (!kinds.empty())
            retired_kinds().push_back(std::move(kinds));
        kinds = std::map<std::string, Kind>();
        serial = prefab::library().serial;
    }
    return kinds;
}
```

After `add_kind` add:

```cpp
int collect()
{
    int n = (int)retired_kinds().size();
    retired_kinds().clear();
    return n;
}
```

In the RPC, before `} else if (action == "ui") {` add:

```cpp
    } else if (action == "reload_probe") {
        /* Tests: a prefab's kind taken, the library reloaded (reload: false skips it), the kind read again. */
        const Json* p = params.get("prefab");
        const Json* again = params.get("reload");
        if (!p || p->type != Json::String || !prefab::library().find(p->string))
            return failure("reload_probe needs prefab (a prefab id)", out, size);
        const Kind* k = rood_schema::find_kind("actor:" + p->string);
        size_t before = k ? k->fields.size() : 0;
        if (!again || again->type != Json::Bool || again->boolean)
            prefab::library().load();
        const Kind* renewed = rood_schema::find_kind("actor:" + p->string); /* the new serial's kind: the old map retires */
        std::string name = k ? k->name : std::string(); /* through the pointer taken before the reload */
        size_t after = k ? k->fields.size() : 0;
        (void)renewed;
        text = "{\"kept\":" + std::string(k && name == "actor:" + p->string ? "true" : "false") + ",\"name\":" + quote(name)
            + ",\"fields_before\":" + std::to_string(before) + ",\"fields_after\":" + std::to_string(after)
            + ",\"retired\":" + std::to_string(rood_schema::retired_count()) + "}";
```

and in the header after `int collect();` add `int retired_count(); /* tests */`, defined after `collect` as:

```cpp
int retired_count() { return (int)retired_kinds().size(); }
```

(`pc_editor_schema_rpc` calls `rood_scene::frame()` first, which now collects: so the probe's `retired` counts only the reload it made.)

In `pc/src/editor_scene.cpp`, at the start of `rood_scene::frame` (before `track_room();`) add:

```cpp
    rood_schema::collect(); /* kinds a prefab reload replaced last frame (editor_schema.h) */
```

and add `#include "editor_schema.h"` to its includes.

- [ ] **Step 4: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py --only reload,actors,lint
```

Expected: every line `ok:`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_schema.h pc/src/editor_schema.cpp pc/src/editor_scene.cpp pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Schema: prefab kinds replaced by a library reload are retired until the next frame (no dangling Kind*/Field&)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: The sun as a schema kind (env:sun)

**Files:**
- Modify: `pc/src/environment_internal.h`, `pc/src/environment.cpp` (`sun_schema_kind`, `sun_details_extra`), `pc/src/editor_schema.cpp` (`fixed_kinds`, `kind_of`, `apply`, `apply_reset`, a local `object_index`), `pc/src/editor_details.cpp` (the sun's note), `pc/tests/editor_schema_smoke.py` (a `sun` section; `EXPECTED_KINDS`)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_schema_smoke.py`, add `"sun"` to `EXPECTED_KINDS`, to the docstring's list:

```
- sun: the room's sun is a schema kind (env:sun): get / set / reset its
  fields (clamped), one undo step each; the environment RPC follows;
  Details draws its fields;
```

and add the section (and to the sections list):

```python
def check_sun(s):
    g = s.game
    g.call("environment", sun={"enabled": True, "azimuth": 180, "elevation": 30, "intensity": 1, "color": [1, 1, 1]})
    g.run(2)
    got = schema(s, action="get", id="env:sun")
    f = got["fields"]
    s.check(got["kind"] == "sun" and {"enabled", "azimuth", "elevation", "color", "intensity", "angle", "shadows"} <= set(f)
            and close(f["elevation"]["value"], 30), f"the sun's fields ({sorted(f)})")
    done = s.history()["done"]
    schema(s, action="set", id="env:sun", values={"elevation": 60, "intensity": 99})
    sun = g.call("environment")["sun"]
    s.check(close(sun["elevation"], 60) and sun["intensity"] <= 8 and s.history()["done"] == done + 1,
            f"set reaches the sun, clamped, one undo step ({sun}, {s.history()['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(close(g.call("environment")["sun"]["elevation"], 30), "undo puts it back")
    schema(s, action="set", id="env:sun", values={"angle": 2})
    schema(s, action="reset", id="env:sun", fields=["angle"])
    s.check(close(value(s, "env:sun", "angle"), 0.53) and s.history()["undo"] == "Reset Angular size",
            f"reset: the default ({s.history()['undo']!r})")
    s.select(action="set", ids=["env:sun"])
    g.call("editor_ui", details="details")
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == "env:sun" and ui["kind"] == "sun" and "azimuth" in ui["fields"],
            f"Details draws the sun's fields ({ui['fields']})")
    g.call("environment", remove="sun")
    s.select(action="clear")
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py --only sun,lint
```

Expected: `FAIL: the kinds …` (no `sun`) and an `RpcError` `no object with fields: env:sun`.

- [ ] **Step 3: The kind**

In `pc/src/environment_internal.h`, in `namespace rood_env` after `bool sun_icon_shown();` add:

```cpp
/* The sun as a schema kind (editor_schema.h: "sun", object KIND_SUN, index 0;
 * Details, RPC editor_schema {id "env:sun"}) and its tools under its fields. */
void sun_schema_kind(rood_schema::Kind& out);
void sun_details_extra(int index);
```

and move the `namespace rood_schema { struct Kind; }` forward declaration (now below) above `namespace rood_env`.

In `pc/src/environment.cpp`, add `#include "editor_schema.h"` and `#include "imgui.h"` (if not there), and at the end of the file:

```cpp
/* ---- the sun's schema kind (editor_schema.h) ---- */

extern "C" int pc_overlay_set_details_tab(const char* name) __attribute__((weak)); /* dev_overlay.cpp */

void rood_env::sun_schema_kind(rood_schema::Kind& k)
{
    namespace rs = rood_schema;
    using rs::Field;
    using rs::Value;
    k = rs::Kind();
    k.name = "sun";
    k.label = "Sun";
    k.object = rood_selection::KIND_SUN;
    k.extras = [](int i) { rood_env::sun_details_extra(i); };
    const Sun d = Sun();
    Field f = rs::bool_field("enabled", "Enabled", "Sun", d.enabled, "Off: no sunlight (the sky stays)");
    f.get = [](int) { return Value::scalar(sun.enabled ? 1.0f : 0.0f); };
    f.set = [](int, const Value& v) { sun.enabled = v.x() > 0.5f, pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::number_field("intensity", "Intensity", "Sun", 0, 8, 0.01f, d.intensity, "", false, "Scale on its colour");
    f.get = [](int) { return Value::scalar(sun.intensity); };
    f.set = [](int, const Value& v) { sun.intensity = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::colour_field("color", "Colour", "Sun", 1, Value::vec(d.color[0], d.color[1], d.color[2]), "Linear colour");
    f.get = [](int) { return Value::vec(sun.color[0], sun.color[1], sun.color[2]); };
    f.set = [](int, const Value& v) {
        for (int c = 0; c < 3; c++)
            sun.color[c] = v.at(c);
        pc_lights_mark_dirty();
    };
    k.fields.push_back(f);
    f = rs::bool_field("shadows", "Casts shadows", "Sun", d.shadows);
    f.get = [](int) { return Value::scalar(sun.shadows ? 1.0f : 0.0f); };
    f.set = [](int, const Value& v) { sun.shadows = v.x() > 0.5f, pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::angle_field("azimuth", "Azimuth", "Direction", 0, 360, 0.5f, d.azimuth,
        "Compass direction it shines from: 0 = from +z, 90 = from +x (drag its disc in the view)");
    f.get = [](int) { return Value::scalar(sun.azimuth); };
    f.set = [](int, const Value& v) { sun.azimuth = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::angle_field("elevation", "Elevation", "Direction", 0, 90, 0.5f, d.elevation, "Above the horizon");
    f.get = [](int) { return Value::scalar(sun.elevation); };
    f.set = [](int, const Value& v) { sun.elevation = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
    f = rs::angle_field("angle", "Angular size", "Direction", 0, 10, 0.01f, d.angle,
        "Its disc's diameter: the softness of its shadows (the sun's own: 0.53)");
    f.get = [](int) { return Value::scalar(sun.angle); };
    f.set = [](int, const Value& v) { sun.angle = v.x(), pc_lights_mark_dirty(); };
    k.fields.push_back(f);
}
void rood_env::sun_details_extra(int)
{
    ImGui::Spacing();
    ImGui::TextWrapped("In the view: drag its disc across the sky dome (azimuth and elevation), its ring "
                       "(intensity), its swatch (colour).");
    if (ImGui::SmallButton("The sky: Atmosphere tab") && pc_overlay_set_details_tab)
        pc_overlay_set_details_tab("atmosphere");
}
```

(`sun` is the file's own `Sun sun;` in its anonymous namespace: the lambdas are in the same file.)

In `pc/src/editor_schema.cpp`, `fixed_kinds()`, after `kinds.push_back(fog);` add:

```cpp
        Kind sun;
        rood_env::sun_schema_kind(sun);
        kinds.push_back(sun);
```

In `kind_of`, replace its first lines:

```cpp
    int i = r.kind == rood_selection::KIND_GLOW ? -1 : rood_scene::index_of(r);
```

with:

```cpp
    if (r.kind == rood_selection::KIND_SUN) { /* the room's sun: one object, index 0 */
        bool on = rood_env::sun_state().defined;
        if (index)
            *index = on ? 0 : -1;
        return on ? find_kind("sun") : nullptr;
    }
    int i = r.kind == rood_selection::KIND_GLOW ? -1 : rood_scene::index_of(r);
```

Add in the anonymous namespace before `fixed_kinds`:

```cpp
/* An object's index for its kind's accessors (the sun's is 0; -1: gone). */
int object_index(const Ref& r)
{
    int i = -1;
    return kind_of(r, &i) ? i : -1;
}
```

(it calls `kind_of`, declared in the header, so it compiles before its definition) and in `apply` and `apply_reset` replace `int i = rood_scene::index_of(r);` with `int i = object_index(r);`.

In `pc/src/editor_details.cpp` `rood_details::draw`, delete the block:

```cpp
    if (subject.kind == rood_selection::KIND_SUN) { /* no schema kind yet: its settings are the Atmosphere tab's */
        ImGui::TextWrapped("The room's sun (or moon). In the view: drag its disc across the sky dome (azimuth and "
                           "elevation), its ring (intensity), its swatch (colour). Every setting: the Atmosphere tab.");
        ImGui::PopID();
        return;
    }
```

- [ ] **Step 4: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py --only sun,lint,lights,fog
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py --only sun
```

Expected: every line `ok:` (the sun's handles keep their undo names).

- [ ] **Step 5: Commit**

```bash
git add pc/src/environment_internal.h pc/src/environment.cpp pc/src/editor_schema.cpp pc/src/editor_details.cpp pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
The sun is a schema kind (env:sun): Details draws its fields, RPC editor_schema gets/sets/resets them

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 9: Emissive entries: Duplicate and Delete (with a confirm), undoable

**Files:**
- Create: `pc/tests/emissive_entries_smoke.py`
- Modify: `pc/src/emissive_internal.h`, `pc/src/emissive.cpp` (`user_text`, `set_user_text`), `pc/src/editor_undo.cpp` (part `EMISSIVE`), `pc/src/emissive_editor.cpp` (`entry_editor`, the RPC), `pc/src/dev_rpc.c` (`edits`), `pc/tests/run.py`

- [ ] **Step 1: Write the failing test**

Create `pc/tests/emissive_entries_smoke.py`:

```python
#!/usr/bin/env python3
"""Emissive entries in the editor (pc/src/emissive_editor.cpp): Duplicate and
Delete, by RPC and with the panel's buttons, each one undo step.

    python3 pc/tests/emissive_entries_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The save room with the editor open (editor_selection_smoke.py's start; the
emissive user layer is the scratch lights directory's emissive.json).

- rpc: an entry of yours duplicates under a new id with " copy" after its
  name (one undo step "Duplicate emissive entry"; undo takes the copy away,
  redo brings it back); remove deletes it (one undo step "Delete emissive
  entry"; undo brings it back);
- panel: on the Emissive tab, Delete... asks first (the entry stays while the
  confirm shows); Yes deletes it, one undo step; Cancel keeps it; a curated
  entry has Duplicate but no Delete, and its copy is yours.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import centre, finish, pinned_card, start  # noqa: E402

TEST = "U950"


def entries(s):
    return {e["id"]: e for e in s.game.call("emissive")["entries"]}


def ui(s):
    s.game.run(3)
    return s.game.call("emissive", action="ui")


def check_rpc(s, curated):
    g = s.game
    g.call("emissive", action="set", entry={"id": TEST, "name": "entries test", "match": curated["match"], "mask": {},
                                            "color": [1, 0.5, 0.25], "strength": 2, "light": 0})
    copy = g.call("emissive", action="duplicate", id=TEST)
    e, h = entries(s), s.history()
    s.check(copy["id"] != TEST and copy["id"] in e and e[copy["id"]]["name"] == "entries test copy"
            and e[copy["id"]]["strength"] == 2 and e[copy["id"]]["origin"] == "user"
            and h["undo"] == "Duplicate emissive entry",
            f"duplicate: a copy of yours under {copy['id']}, one undo step ({h['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(copy["id"] not in entries(s), "undo takes the copy away")
    g.call("editor_undo", action="redo")
    s.check(copy["id"] in entries(s), "redo brings it back")
    g.call("emissive", action="remove", id=copy["id"])
    s.check(copy["id"] not in entries(s) and s.history()["undo"] == "Delete emissive entry",
            f"remove: gone, one undo step ({s.history()['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(copy["id"] in entries(s), "undo brings the deleted entry back")
    g.call("emissive", action="remove", id=copy["id"])


def check_panel(s, curated):
    g = s.game
    g.call("editor_ui", details="emissive")
    g.call("emissive", action="select", id=TEST)
    state = ui(s)
    if not s.check("delete" in state["rects"] and "duplicate" in state["rects"] and state["selected"] == TEST,
                   f"the panel shows the entry's Duplicate and Delete... ({state})"):
        return
    s.click(centre(state["rects"]["delete"]))
    state = ui(s)
    s.check(state["confirm"] and TEST in entries(s), "Delete... asks first; the entry stays")
    s.click(centre(state["rects"]["confirm_cancel"]))
    state = ui(s)
    s.check(not state["confirm"] and TEST in entries(s), "Cancel keeps it")
    s.click(centre(state["rects"]["delete"]))
    state = ui(s)
    done = s.history()["done"]
    s.click(centre(state["rects"]["confirm_yes"]))
    ui(s)
    h = s.history()
    s.check(TEST not in entries(s) and h["done"] == done + 1 and h["undo"] == "Delete emissive entry",
            f"Yes deletes it, one undo step ({h['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(TEST in entries(s), "undo brings it back")
    g.call("emissive", action="select", id=curated["id"])
    state = ui(s)
    s.check("duplicate" in state["rects"] and "delete" not in state["rects"],
            f"a curated entry: Duplicate, no Delete ({sorted(state['rects'])})")
    before = set(entries(s))
    s.click(centre(state["rects"]["duplicate"]))
    ui(s)
    new = sorted(set(entries(s)) - before)
    s.check(len(new) == 1 and entries(s)[new[0]]["origin"] == "user"
            and entries(s)[new[0]]["name"] == (curated["name"] or curated["id"]) + " copy",
            f"its copy is yours ({new})")
    for ident in new + [TEST]:
        g.call("emissive", action="remove", id=ident)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    args = parser.parse_args()
    s = start(args, "emissive_entries")
    if s is None:
        return 77
    try:
        curated = next((e for e in s.game.call("emissive")["entries"] if e["origin"] == "curated"), None)
        if s.check(curated is not None, "a curated entry to copy the match from"):
            print("-- rpc", flush=True)
            check_rpc(s, curated)
            print("-- panel", flush=True)
            check_panel(s, curated)
    finally:
        status = finish(s, "emissive entries duplicate and delete (with a confirm), each one undo step")
    return status


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `editor_handles_smoke` suite add:

```python
            Suite("emissive_entries_smoke", [PY, str(TESTS / "emissive_entries_smoke.py"), "--card", str(args.save)],
                  300, note="emissive entries: Duplicate and Delete (a confirm first) by RPC and the panel, one undo step "
                            "each, undo and redo"),
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/emissive_entries_smoke.py
```

Expected: an `RpcError` for `emissive duplicate` (`emissive: action list, uses, …`).

- [ ] **Step 3: The user layer as text**

In `pc/src/emissive_internal.h`, after `std::string catalog_text(const Catalog& c, const char* comment);` add:

```cpp
/* The user layer as text (the editor's undo snapshots: editor_undo.cpp),
 * and back (the layer replaced and unsaved; nothing when it is the same). */
std::string user_text();
void set_user_text(const std::string& text);
```

In `pc/src/emissive.cpp`, after `catalog_text`'s definition add:

```cpp
std::string user_text()
{
    catalog();
    static uint32_t made_for = ~0u;
    static std::string text;
    if (made_for != catalog_generation) { /* merge() bumps it on every change */
        text = catalog_text(user, nullptr);
        made_for = catalog_generation;
    }
    return text;
}
void set_user_text(const std::string& text)
{
    if (text == user_text())
        return;
    Catalog c;
    Json j;
    if (prefab::json_parse(text.c_str(), j) && j.type == Json::Object)
        if (const Json* es = j.get("entries"); es && es->type == Json::Array)
            for (const Json& ej : es->items) {
                Entry e;
                if (entry_from_json(ej, e, nullptr))
                    c.entries.push_back(e);
            }
    user = c;
    dirty_user = true;
    merge();
}
```

(`catalog_text` is defined after the anonymous namespace holding `dirty_user` (line 566), so both are in scope there.)

- [ ] **Step 4: Undo snapshots keep it**

In `pc/src/editor_undo.cpp`, add `#include "emissive_internal.h"`; in the header comment's list of what a snapshot holds add "the emissive user layer"; and:

```cpp
enum Part { LIGHTS = 1, ACTORS = 2, ROOM = 4, GLOWS = 8, UV = 16, GROUPS = 32, SETTINGS = 64, ROOM_PARTS = 63, KINDS = 128,
    EMISSIVE = 256 };
```

In `struct Snapshot` after `std::string kinds;` add:

```cpp
    std::string emissive; /* the emissive user layer (emissive::user_text) */
```

In `capture`, after `s.kinds = rood_schema::standalone_state();` add `s.emissive = emissive::user_text();`. In `difference`, before `return d;` add:

```cpp
    if ((parts & EMISSIVE) && a.emissive != b.emissive)
        d |= EMISSIVE;
```

In `describe`, before `return "Edit";` add:

```cpp
    if (parts & EMISSIVE)
        return "Edit glowing surfaces";
```

In `apply`, after the `KINDS` line add:

```cpp
    if (parts & EMISSIVE)
        emissive::set_user_text(to.emissive);
```

In `pc_undo_frame_end`, change `difference(pre_ui, now, SETTINGS | GROUPS | KINDS)` to `difference(pre_ui, now, SETTINGS | GROUPS | KINDS | EMISSIVE)` and after `open_before.kinds = pre_ui.kinds;` add `open_before.emissive = pre_ui.emissive;`. In `pc_undo_flush`, change `ROOM_PARTS | KINDS` to `ROOM_PARTS | KINDS | EMISSIVE`.

In `pc/src/dev_rpc.c` `edits`, before `if (!strcmp(method, "lighting")) {` add:

```c
    if (!strcmp(method, "emissive")) { /* edits of the user layer; the rest read, pick, save, reload or paint maps */
        const char* p = member(request, "params");
        char action[32] = "";
        if (p)
            string_value(member(p, "action"), action, sizeof action);
        return !strcmp(action, "set") || !strcmp(action, "new") || !strcmp(action, "remove")
            || !strcmp(action, "duplicate") || !strcmp(action, "scope_pick");
    }
```

- [ ] **Step 5: Duplicate and Delete in the panel and the RPC**

In `pc/src/emissive_editor.cpp`, add `#include "editor_undo.h"`. After `std::string selected;` (the panel's state) add:

```cpp
bool scroll_to_editor; /* RPC select: the entry editor's title scrolled into view next frame */
bool confirm_shown;
int ui_frame = -1;
std::map<std::string, ImVec4> ui_rects; /* the entry's buttons as last drawn (RPC ui) */
void keep_button(const char* name)
{
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    ui_rects[name] = ImVec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
}
/* A copy of an entry (curated or yours) in your layer: a new id, " copy"
 * after its name; selected. "" when there is no such entry. */
std::string duplicate_entry(const std::string& id)
{
    const Entry* e = find(id);
    if (!e)
        return std::string();
    Entry copy = *e;
    copy.id = new_id();
    copy.name = (e->name.empty() ? e->id : e->name) + " copy";
    copy.origin = ORIGIN_USER;
    put(copy);
    selected = copy.id;
    pc_undo_hint("Duplicate emissive entry");
    return copy.id;
}
/* Your entry deleted, or your changes to a curated one reverted. */
bool remove_entry(const std::string& id)
{
    const Entry* e = find(id);
    bool revert = e && e->origin == ORIGIN_OVERRIDE;
    if (!remove(id))
        return false;
    pc_undo_hint(revert ? "Revert emissive entry" : "Delete emissive entry");
    if (!revert)
        selected.clear();
    return true;
}
```

(add `#include <map>` if missing). In `entry_editor`, replace the first line that prints the id:

```cpp
    ImGui::Text("%s  (%s)", e.id.c_str(), origins[e.origin]);
```

with:

```cpp
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  (%s)", e.id.c_str(), origins[e.origin]);
    if (scroll_to_editor) {
        ImGui::SetScrollHereY(0.0f);
        scroll_to_editor = false;
    }
    ui_frame = ImGui::GetFrameCount();
    ui_rects.clear();
    confirm_shown = false;
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        duplicate_entry(e.id);
        return; /* the copy is selected: drawn next frame */
    }
    keep_button("duplicate");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A copy in your layer under a new id (Ctrl+Z undoes)");
    if (e.origin != ORIGIN_CURATED) {
        ImGui::SameLine();
        if (ImGui::SmallButton(e.origin == ORIGIN_OVERRIDE ? "Revert to curated..." : "Delete..."))
            ImGui::OpenPopup("confirm remove");
        keep_button("delete");
    }
    if (ImGui::BeginPopupModal("confirm remove", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        confirm_shown = true;
        if (e.origin == ORIGIN_OVERRIDE)
            ImGui::Text("Revert %s to the curated entry? Your changes to it go.", e.id.c_str());
        else
            ImGui::Text("Delete %s (%s)?", e.id.c_str(), e.name.c_str());
        ImGui::TextDisabled("Ctrl+Z undoes it.");
        bool yes = ImGui::Button("Yes");
        keep_button("confirm_yes");
        ImGui::SameLine();
        bool no = ImGui::Button("Cancel");
        keep_button("confirm_cancel");
        if (yes || no)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (yes) {
            remove_entry(e.id);
            return;
        }
    }
```

and delete the old block at the end of `entry_editor`:

```cpp
    if (e.origin != ORIGIN_CURATED) {
        ImGui::SameLine();
        if (ImGui::Button(e.origin == ORIGIN_OVERRIDE ? "Revert to curated" : "Delete")) {
            remove(e.id);
            if (e.origin == ORIGIN_USER)
                selected.clear();
        }
    }
```

In `pc_emissive_rpc`, in the `remove` action replace `!remove(id->string)` with `!remove_entry(id->string)`, and before `if (action == "save") {` add:

```cpp
    if (action == "duplicate") {
        const Json* id = params.get("id");
        std::string copy = id && id->type == Json::String ? duplicate_entry(id->string) : std::string();
        if (copy.empty())
            return fail("emissive duplicate: needs the id of an entry", out, size);
        return reply(entry_to_json(*find(copy)), out, size);
    }
    if (action == "select") { /* the panel's entry (its editor scrolled into view next frame) */
        const Json* id = params.get("id");
        if (!id || id->type != Json::String || !find(id->string))
            return fail("emissive select: unknown id", out, size);
        selected = id->string;
        scroll_to_editor = true;
        Json j = list_json();
        j.set("selected", Json::str(selected));
        return reply(j, out, size);
    }
    if (action == "ui") { /* the entry editor's buttons as last drawn (window pixels x, y, w, h) */
        Json j = Json::object(), rects = Json::object();
        bool current = ImGui::GetCurrentContext() && ui_frame >= ImGui::GetFrameCount() - 1;
        for (const auto& kv : ui_rects) {
            if (!current)
                break;
            Json r = Json::array();
            for (float v : { kv.second.x, kv.second.y, kv.second.z, kv.second.w })
                r.items.push_back(Json::num(v));
            rects.set(kv.first.c_str(), r);
        }
        j.set("rects", rects);
        j.set("confirm", Json::boolean_(current && confirm_shown));
        j.set("selected", Json::str(selected));
        return reply(j, out, size);
    }
```

and extend the final failure text's action list with `duplicate, select, ui`. Update the RPC's doc comment above `pc_emissive_rpc` with `duplicate {id} | select {id} | ui`.

- [ ] **Step 6: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/emissive_entries_smoke.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_undo emissive
```

Expected: `PASS: emissive entries …`; the undo and emissive suites still pass.

- [ ] **Step 7: Commit**

```bash
git add pc/tests/emissive_entries_smoke.py pc/tests/run.py pc/src/emissive_internal.h pc/src/emissive.cpp pc/src/editor_undo.cpp pc/src/emissive_editor.cpp pc/src/dev_rpc.c
git commit -m "$(cat <<'EOF'
Emissive entries: Duplicate and Delete (a confirm first) in the panel and RPC; the emissive user layer is undoable

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

## Phase D: volumes

### Task 10: Fog volume Details: preset first, the five groups, "Use global" disturb weights

**Files:**
- Modify: `pc/src/editor_schema.h` (`Kind::header`, `Field::uses_global` / `set_uses_global` / `global_tooltip`, `apply_global`), `pc/src/editor_schema.cpp` (`apply_global`; RPC `set` "global", `get` "global"), `pc/src/editor_details.cpp` (the header row, the box, categories in RPC `ui`), `pc/src/atmosphere.cpp` (`schema_kind`, `details_extra`, a new `details_header`; the Atmosphere tab's "Save as preset..." tooltip), `pc/src/environment_internal.h`, `pc/tests/editor_schema_smoke.py` (`check_fog`, `check_details`)

- [ ] **Step 1: Write the failing checks**

In `pc/tests/editor_schema_smoke.py`, at the end of `check_fog` (before its last `g.call("environment", …)` reset line) add:

```python
    # The disturb weights: "Use global" (the volume follows the global weight) or its own.
    f = schema(s, action="get", id=s.volume)["fields"]
    glob = g.call("environment")["disturb"]["player"]
    s.check(f["disturb_player"]["global"] is True and close(f["disturb_player"]["value"], glob),
            f"a weight left global: the box ticked, the global weight shown ({f['disturb_player']})")
    schema(s, action="set", id=s.volume, values={"disturb_player": 0.25})
    v = next(x for x in g.call("environment")["volumes"] if x["id"] == VOLUME)
    s.check(close(v["disturb_player"], 0.25) and schema(s, action="get", id=s.volume)["fields"]["disturb_player"]["global"]
            is False, "a value of its own unticks it")
    schema(s, action="set", id=s.volume, values={"disturb_player": "global"})
    v = next(x for x in g.call("environment")["volumes"] if x["id"] == VOLUME)
    s.check(close(v["disturb_player"], -1) and s.history()["undo"] == "Use global Ashley",
            f"\"global\" ticks it again (-1 in the data; {s.history()['undo']!r})")
    kinds = {k["name"]: k for k in schema(s, action="kinds")["kinds"]}
    weight = next(x for x in kinds["fog"]["fields"] if x["name"] == "disturb_player")
    s.check(weight["min"] == 0 and weight["max"] == 2, f"the weights' range is 0..2 ({weight['min']}..{weight['max']})")
    # Every group's ↺ goes to the preset: one field per group, changed then reset.
    g.call("environment", volumes=[{"id": VOLUME, "preset": "ankle_mist"}])
    for field, changed_to in (("density", 0.03), ("brightness", 2.5), ("sim_trail", 2.5), ("shadow", 1.5)):
        base = value(s, s.volume, field)
        schema(s, action="set", id=s.volume, values={field: changed_to})
        schema(s, action="reset", id=s.volume, fields=[field])
        s.check(close(value(s, s.volume, field), base), f"↺ on {field} restores the preset's {base}")
    g.call("environment", volumes=[{"id": VOLUME, "preset": ""}])
```

In `check_details`, after the fog volume check `"a fog volume: Basics open, Look closed at first"`, add:

```python
    cats = [(c["name"], c["open"]) for c in ui["categories"]]
    s.check(cats == [("Basics", True), ("Look", False), ("Motion & interaction", False), ("Advanced", False),
                     ("Transform", True)] and ui["header"],
            f"a fog volume's groups in order, Basics and Transform open, its preset row first ({cats}, header {ui['header']})")
    schema(s, action="ui", search="ashley")
    g.run(3)
    ui = schema(s, action="ui")
    box = ui["rects"].get("disturb_player.global")
    if s.check(box is not None, f"the weight's Use global box ({sorted(ui['rects'])})"):
        done = s.history()["done"]
        s.click(centre(box))
        f = schema(s, action="get", id=s.volume)["fields"]["disturb_player"]
        s.check(f["global"] is False and s.history()["done"] == done + 1 and s.history()["undo"] == "Edit Ashley",
                f"unticking it: its own weight (from the global one), one undo step ({f}, {s.history()['undo']!r})")
        s.click(centre(box))
        s.check(schema(s, action="get", id=s.volume)["fields"]["disturb_player"]["global"] is True, "ticking it: global again")
    schema(s, action="ui", search="")
```

- [ ] **Step 2: Run them and see them fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py --only fog,details
```

Expected: `KeyError: 'global'` (or `FAIL` lines for the groups and the box).

- [ ] **Step 3: The schema's header and "use global"**

In `pc/src/editor_schema.h`, in `struct Field` after `std::function<void(int)> reset; /* null: set(base) */` add:

```cpp
    /* A value that may follow a setting every room shares (a fog volume's
     * disturb weights: the global fog_disturb_*): Details draws a "Use
     * global" box before it and disables the value while it is ticked (get
     * then gives the global value); RPC set takes "global". Null: none. */
    std::function<bool(int)> uses_global;
    std::function<void(int, bool)> set_uses_global;
    std::string global_tooltip; /* what the global value is ("the weight every room uses ...") */
```

In `struct Kind` after `std::function<void(int)> extras;` add:

```cpp
    std::function<void(int)> header; /* Details: the kind's own row above its search (a fog volume's preset) */
```

After `void apply_reset(...)` add:

```cpp
/* A "use global" field ticked (on) or given its own value (off: the global value now), to every target. */
void apply_global(const Kind& k, const Field& f, const rood_selection::Ref& target, bool on);
```

In `pc/src/editor_schema.cpp`, after `apply_reset` add:

```cpp
void apply_global(const Kind& k, const Field& f, const Ref& target, bool on)
{
    if (!f.set_uses_global)
        return;
    for (const Ref& r : targets(k, f, target)) {
        int i = object_index(r);
        if (i >= 0)
            f.set_uses_global(i, on);
    }
    rood_lights::dirty = true;
    pc_undo_hint(((on ? "Use global " : "Edit ") + f.label).c_str());
}
```

In the RPC's `set`, replace the loop that collects edits:

```cpp
            for (const auto& m : values->members) {
                const Field* f = k->field(m.first);
                if (!f)
                    return failure("no field " + m.first + " on " + k->name, out, size);
                rood_schema::Value v;
                if (!rood_schema::from_json(*f, m.second, v, &error))
                    return failure(error, out, size);
                edits.push_back({ f, v });
            }
            for (const auto& e : edits)
                edit(*e.first, e.second);
            if (edits.size() > 1)
```

with:

```cpp
            std::vector<const Field*> globals; /* "global" (or null) for a field that may follow one */
            for (const auto& m : values->members) {
                const Field* f = k->field(m.first);
                if (!f)
                    return failure("no field " + m.first + " on " + k->name, out, size);
                if (f->set_uses_global && !standalone
                    && (m.second.type == Json::Null || (m.second.type == Json::String && m.second.string == "global"))) {
                    globals.push_back(f);
                    continue;
                }
                rood_schema::Value v;
                if (!rood_schema::from_json(*f, m.second, v, &error))
                    return failure(error, out, size);
                edits.push_back({ f, v });
            }
            for (const auto& e : edits)
                edit(*e.first, e.second);
            for (const Field* f : globals)
                rood_schema::apply_global(*k, *f, target, true);
            if (edits.size() + globals.size() > 1)
```

In `object_json`, after the `"overridden"` member add a `"global"` member for such fields:

```cpp
            + (f.resettable && rood_schema::is_overridden(f, index) ? "true" : "false")
            + (f.uses_global ? std::string(",\"global\":") + (f.uses_global(index) ? "true" : "false") : std::string()) + "}";
```

(replacing that expression's tail `+ (f.resettable && …) ? "true" : "false") + "}";`).

- [ ] **Step 4: Details draws them**

In `pc/src/editor_details.cpp`:

In `struct Drawn` add:

```cpp
    std::vector<std::pair<std::string, bool>> categories; /* as drawn: each category and whether it is open */
    bool header = false; /* the kind's header row drawn */
```

After `edit_reset` add:

```cpp
void edit_global(const Kind& k, const Field& f, const Subject& s, bool on)
{
    if (s.standalone()) {
        f.set_uses_global(s.index, on);
        pc_undo_hint(((on ? "Use global " : "Edit ") + f.label).c_str());
    } else {
        rood_schema::apply_global(k, f, s.ref, on);
    }
}
```

In `row`, replace:

```cpp
    ImGui::TableNextColumn();
    Value v = f.get(index), before = v;
    if (widget(k, f, target, index, v)) {
```

with:

```cpp
    ImGui::TableNextColumn();
    bool global = f.uses_global && f.uses_global(index);
    if (f.uses_global) { /* "Use global": the value follows the setting every room shares */
        bool box = global;
        if (ImGui::Checkbox("##global", &box))
            edit_global(k, f, target, box);
        keep_rect(false, f.name + ".global");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Use global: %s.\nUntick to give this one its own value.",
                f.global_tooltip.empty() ? "the value every room uses" : f.global_tooltip.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(global);
    }
    Value v = f.get(index), before = v;
    bool changed = widget(k, f, target, index, v);
    if (f.uses_global)
        ImGui::EndDisabled();
    if (changed) {
```

(the block that follows `if (widget(...)) {` keeps its body; it is now under `if (changed) {`).

In `fields`, before `ImGui::SetNextItemWidth(-FLT_MIN);` add:

```cpp
    if (k.header) { /* preset first (a fog volume's): above the search, always shown */
        k.header(index);
        drawing->header = true;
        ImGui::Spacing();
    }
```

and record each category: replace

```cpp
        if (!ImGui::CollapsingHeader((c + "##category").c_str(), closed ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
            continue;
```

with:

```cpp
        bool open = ImGui::CollapsingHeader((c + "##category").c_str(), closed ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
        drawing->categories.push_back({ c, open });
        if (!open)
            continue;
```

In `state_json`, after the Details `"reset"` rects add the categories and header:

```cpp
    s += ",\"categories\":[";
    for (size_t i = 0; current && i < details_drawn.categories.size(); i++)
        s += std::string(i ? "," : "") + "{\"name\":" + q(details_drawn.categories[i].first) + ",\"open\":"
            + (details_drawn.categories[i].second ? "true" : "false") + "}";
    s += std::string("],\"header\":") + (current && details_drawn.header ? "true" : "false");
```

- [ ] **Step 5: The fog volume's fields in groups, its preset row first, the weights**

In `pc/src/environment_internal.h`, after `void details_extra(int index);` add:

```cpp
void details_header(int index); /* Details: a fog volume's preset row, above its fields */
```

In `pc/src/atmosphere.cpp` `rood_atmo::schema_kind`, set:

```cpp
    k.closed = { "Look", "Motion & interaction", "Advanced" };
    k.extras = [](int i) { rood_atmo::details_extra(i); };
    k.header = [](int i) { rood_atmo::details_header(i); };
```

and reorder the field registrations so the groups come in order (Details draws categories in the order their first field appears). Replace everything from `flag(rs::bool_field("enabled", …` to the end of the disturb loop with:

```cpp
    /* Basics (open) */
    flag(rs::bool_field("enabled", "Enabled", "Basics", true), &Volume::enabled);
    {
        Field f = rs::enum_field("shape", "Shape", "Basics", { "Box", "Sphere" }, 0, "A box, or an ellipsoid of the same size");
        f.get = [](int i) { return Value::scalar(volumes[i].sphere ? 1.0f : 0.0f); };
        f.set = [](int i, const Value& v) { volumes[i].sphere = v.x() > 0.5f; };
        add(f);
    }
    scalar(rs::number_field("density", "Density", "Basics", 0, 0.05f, 0.0001f, 0.004f, "/u", true,
               "Extinction per world unit in its middle (its ring in the view)"),
        &Volume::density);
    triple(rs::colour_field("color", "Colour", "Basics", 1, Value::vec(1, 1, 1),
               "Albedo: the share of each colour it scatters (its swatch in the view)"),
        &Volume::color);
    scalar(rs::number_field("edge", "Soft edge", "Basics", 0.01f, 1, 0.01f, 0.5f, "", false,
               "The part of its half size over which it fades out (its inner outline in the view)"),
        &Volume::edge);
    triple(rs::colour_field("fill", "Fill light", "Basics", 4, Value::vec(0, 0, 0),
               "Light it scatters from everywhere (radiance): keeps it visible where no light reaches"),
        &Volume::fill);
    /* Look */
    scalar(rs::number_field("opacity", "Opacity", "Look", 0, 1, 0.01f, 1, "", false,
               "The most of the scene it may hide along a ray (1: solid)"),
        &Volume::opacity);
    scalar(rs::number_field("brightness", "Brightness", "Look", 0, 4, 0.01f, 1, "", false, "Scale on the light it scatters and emits"),
        &Volume::brightness);
    scalar(rs::number_field("phase", "Forward scattering", "Look", -0.9f, 0.9f, 0.01f, 0.3f, "", false,
               "How much brighter it is looking toward a light through it"),
        &Volume::phase);
    scalar(rs::number_field("ambient", "Ambient", "Look", 0, 4, 0.01f, 1, "", false,
               "Scale on its ambient light (the room's baked light around it, the sky)"),
        &Volume::ambient);
    triple(rs::colour_field("glow", "Glow", "Look", 16, Value::vec(0, 0, 0), "Light it emits per unit of density"), &Volume::glow);
    scalar(rs::number_field("glow_core", "Glow at its base", "Look", 0, 1, 0.01f, 0.5f), &Volume::glow_core);
    scalar(rs::number_field("taper", "Taper", "Look", 0, 1, 0.01f, 0, "", false, "0 an even density, 1 thinning toward its top"),
        &Volume::taper);
    scalar(rs::number_field("noise", "Wisps", "Look", 0, 1, 0.01f, 0.5f, "", false, "How much the drifting noise thins and thickens it"),
        &Volume::noise);
    /* Motion & interaction */
    flag(rs::bool_field("sim", "Characters part it", "Motion & interaction", false,
             "Interactive fog: characters, attacks and spells clear it and leave a wake"),
        &Volume::sim);
    scalar(rs::number_field("sim_strength", "Strength", "Motion & interaction", 0, 2, 0.01f, 1), &Volume::sim_strength);
    scalar(rs::number_field("sim_recover", "Recovery time", "Motion & interaction", 0.3f, 12, 0.1f, 3, "s"), &Volume::sim_recover);
    scalar(rs::number_field("sim_footprint", "Footprint", "Motion & interaction", 0.25f, 3, 0.01f, 1), &Volume::sim_footprint);
    scalar(rs::number_field("sim_trail", "Trail", "Motion & interaction", 0, 3, 0.01f, 1), &Volume::sim_trail);
    scalar(rs::number_field("sim_wind", "Wind", "Motion & interaction", 0, 4, 0.01f, 1), &Volume::sim_wind);
    scalar(rs::number_field("sim_swirl", "Swirl", "Motion & interaction", 0, 4, 0.01f, 0), &Volume::sim_swirl);
    static const char* const global_keys[4] = { "player", "actors", "attacks", "spells" };
    for (int s = 0; s < 4; s++) {
        /* Not through add(): the lighting schema's default is -1 (global); here the value shown is the weight in effect. */
        Field f = rs::number_field(DISTURB_KEYS[s], SOURCE_LABELS[s], "Motion & interaction", 0, 2, 0.01f, 1, "", false,
            SOURCE_TIPS[s]);
        f.global_tooltip = std::string("the weight every room uses (Atmosphere > Interactive fog: all rooms; settings key fog_disturb_")
            + global_keys[s] + ")";
        f.get = [s](int i) { return Value::scalar(volume_weight(volumes[i], s)); };
        f.set = [s](int i, const Value& v) { volumes[i].disturb[s] = fmaxf(v.x(), 0.0f); };
        f.uses_global = [s](int i) { return volumes[i].disturb[s] < 0; };
        f.set_uses_global = [s](int i, bool on) { volumes[i].disturb[s] = on ? -1 : fmaxf(pc_fog_disturb[s], 0.0f); };
        std::string key = DISTURB_KEYS[s];
        auto raw = [key](int i) { /* the preset's weight (-1: global), else the schema's -1 */
            const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
            const std::string* x = p ? p->get(key) : nullptr;
            float w = x ? (float)atof(x->c_str()) : -1.0f;
            return w < 0 ? -1.0f : w;
        };
        f.base = [raw, s](int i) {
            float w = raw(i);
            return Value::scalar(w < 0 ? fmaxf(pc_fog_disturb[s], 0.0f) : w);
        };
        f.base_source = [key](int i) -> const char* {
            const Preset* p = volumes[i].preset.empty() ? nullptr : find_preset(volumes[i].preset);
            return p && p->get(key) ? "preset" : "default";
        };
        f.overridden = [raw, s](int i) { return fabsf(volumes[i].disturb[s] - raw(i)) > 1e-4f; };
        f.reset = [raw, s](int i) { volumes[i].disturb[s] = raw(i); };
        k.fields.push_back(f);
    }
    /* Advanced */
    scalar(rs::number_field("detail", "Detail", "Advanced", 0, 1, 0.01f, 0.5f, "", false,
               "The detail noise at its soft edges and in the interactive field"),
        &Volume::detail);
    scalar(rs::number_field("detail_size", "Billow size", "Advanced", 0, 1024, 1, 0, "u", false,
               "Its billows' size (0: from the room's wisp size)"),
        &Volume::detail_size);
    scalar(rs::number_field("shadow", "Self-shadow", "Advanced", 0, 2, 0.01f, 0, "", false,
               "Its dense parts shade themselves from the main light"),
        &Volume::shadow);
    /* Transform (open) */
    {
        Field f = rs::vec3_field("pos", "Position", "Transform", -32768, 32768, 1, Value::vec(0, 0, 0), "u", "Its centre");
        f.resettable = false, f.shared = false;
        triple(f, &Volume::pos);
    }
    triple(rs::vec3_field("size", "Size", "Transform", 2, 8192, 1, Value::vec(256, 128, 256), "u",
               "Full extents (the scale tool, R)"),
        &Volume::size);
    scalar(rs::angle_field("yaw", "Yaw", "Transform", -180, 180, 0.5f, 0), &Volume::yaw);
```

(`volume_weight`, `SOURCE_LABELS` and `SOURCE_TIPS` (line 1261) are earlier in this file, in its anonymous namespaces.)

Before `rood_atmo::details_extra` add:

```cpp
/* Details > a fog volume's preset row, above its fields (preset first): pick
 * one, Reset to preset, Save as preset..., Reload presets. */
void rood_atmo::details_header(int index)
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    ImGui::PushID(volumes[index].id.c_str());
    if (preset_ui(volumes[index]))
        pc_lights_mark_dirty();
    ImGui::PopID();
}
```

and in `details_extra` delete the line `changed |= preset_ui(v);`.

In `preset_ui`, change the "Save as preset..." tooltip (after its button) to say it writes the shipped file:

```cpp
    tooltip("Writes this volume's look into pc/assets/environment/fog_presets.json (the shipped presets: an\n"
            "authoring tool). The Library's Save as prefab keeps one of your own instead.");
```

- [ ] **Step 6: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_schema_smoke.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 fog atmosphere
```

Expected: every line `ok:`; the fog and atmosphere suites still pass (the environment RPC's `disturb_*` keep -1 for global).

- [ ] **Step 7: Commit**

```bash
git add pc/src/editor_schema.h pc/src/editor_schema.cpp pc/src/editor_details.cpp pc/src/atmosphere.cpp pc/src/environment_internal.h pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Fog volume Details: preset row first, Basics / Look / Motion & interaction / Advanced / Transform, disturb weights as Use global + slider

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 11: The handle layer and the colour popover learn fog volumes

**Files:**
- Create: `pc/src/editor_volume_handles.cpp` (the swatch and the inner outline here; the ring and dot in Task 12)
- Modify: `pc/src/editor_handles.h`, `pc/src/editor_handles.cpp` (`build`, `restore`, the press, `frame`, `Drag::field`), `pc/src/editor_light_handles.cpp` (`swatch` becomes `rood_handles::colour_swatch`), `pc/src/editor_colour.cpp` (volumes, five tab slots), `pc/src/lights.cpp` (ImGuizmo stays disabled through the fog gizmo, line 1475), `pc/tools/build_boot.py`, `pc/tests/editor_handles_smoke.py` (a `volume` section, `check_layer`)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_handles_smoke.py`, add to the docstring's list:

```
- volume: a fog volume selected shows its handles (the colour swatch, the
  density ring, the soft edge dot); the swatch opens the colour popover on
  its colour (one undo step, only its colour changes); each drag changes
  exactly one field by the expected amount in one undo step; Escape puts
  it back; R still sizes it while its handles show;
```

and add (to `SECTIONS` after `check_sun`):

```python
def check_volume(s):
    g = s.game
    s.select(action="set", ids=[s.volume])
    schema(s, action="set", id=s.volume, values={"density": 0.004, "edge": 0.5, "color": [1, 1, 1]})
    g.run(2)
    st = handles(s)
    names = {h["name"] for h in st["handles"]}
    if not s.check(st["subject"] == s.volume and st["hidden"] is None and "colour" in names,
                   f"a fog volume's handles ({st['hidden']}, {sorted(names)})"):
        return
    before, done = fields(s, s.volume), s.history()["done"]
    s.click(handle(s, "colour")["screen"])
    st = handles(s)["colour"]
    s.check(st["open"] and st["subject"] == s.volume, f"its swatch opens the colour popover on it ({st['subject']})")
    colour(s, kelvin=3200, tint=0)
    after = fields(s, s.volume)
    s.check(changed(before, after) == ["color"] and s.history()["done"] == done + 1
            and s.history()["undo"] == "Edit Colour", f"only its colour changes, one undo step ({changed(before, after)})")
    g.call("editor_undo", action="undo")
    s.keys(["escape"])
```

In `check_layer`, the actor check stays `unsupported`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py --only volume
```

Expected: `FAIL: a fog volume's handles (unsupported, [])`.

- [ ] **Step 3: The layer**

In `pc/src/editor_handles.h`, in `struct Drag` after `rood_env::SunState sun;` add:

```cpp
    /* A fog volume's schema fields as the drag began (Escape puts them back; its handles read their start here). */
    std::vector<std::pair<std::string, prefab::Value>> fields;
    prefab::Value field(const char* name) const; /* one of them (a zero scalar when absent) */
```

After the `sun_icon` declaration add:

```cpp
/* editor_volume_handles.cpp: a fog volume's handles (the density ring, the
 * soft edge dot, the colour swatch) and its inner outline (the full-density core). */
void volume_handles(const pc_overlay_view& v, const rood_selection::Ref& r, int index, std::vector<Handle>& out);
void volume_extras(const pc_overlay_view& v, int index, void* draw_list);
/* The colour swatch below right of a ring centred at (cx, cy): a click opens (or closes) the colour popover. */
Handle colour_swatch(const rood_selection::Ref& r, float cx, float cy, float ring_radius, const float colour[3]);
```

In `pc/src/editor_handles.cpp`:

after `int frame_now() …` add:

```cpp
const rood_schema::Field* field_of(const Ref& r, const std::string& name, int* index)
{
    const rood_schema::Kind* k = rood_schema::kind_of(r, index);
    return k ? k->field(name) : nullptr;
}
```

in `build`, before `if (p.kind == rood_selection::KIND_SUN && …` add:

```cpp
    if (p.kind == rood_selection::KIND_VOLUME) {
        int i = rood_scene::index_of(p);
        if (i < 0)
            return "nothing";
        rood_handles::volume_handles(v, p, i, out);
        return nullptr;
    }
```

replace `restore`:

```cpp
void restore(const Active& a)
{
    int i = rood_scene::index_of(a.subject);
    if (a.subject.kind == rood_selection::KIND_LIGHT && i >= 0)
        rood_lights::lights[i] = a.drag.light;
    else if (a.subject.kind == rood_selection::KIND_SUN)
        rood_env::set_sun_state(a.drag.sun);
    else if (a.subject.kind == rood_selection::KIND_VOLUME && i >= 0) {
        for (const auto& f : a.drag.fields) {
            int index;
            if (const rood_schema::Field* field = field_of(a.subject, f.first, &index))
                field->set(index, f.second);
        }
        rood_lights::dirty = true;
    }
}
```

in `frame`, after `if (subject.kind == rood_selection::KIND_LIGHT) light_extras(…);` add:

```cpp
    if (subject.kind == rood_selection::KIND_VOLUME)
        volume_extras(v, rood_scene::index_of(subject), draw);
```

at the press, after `d.sun = rood_env::sun_state();` add:

```cpp
        d.fields.clear();
        int vi;
        const rood_schema::Kind* vk = subject.kind == rood_selection::KIND_VOLUME ? rood_schema::kind_of(subject, &vi) : nullptr;
        for (size_t f = 0; vk && f < vk->fields.size(); f++)
            d.fields.push_back({ vk->fields[f].name, vk->fields[f].get(vi) });
```

and after `rood_handles::hot()` add:

```cpp
prefab::Value Drag::field(const char* name) const
{
    for (const auto& f : fields)
        if (f.first == name)
            return f.second;
    return prefab::Value();
}
```

(as `prefab::Value rood_handles::Drag::field(…) const`, outside the anonymous namespace). In the RPC's `where` failure message, say "a light, the sun or a fog volume selected".

In `pc/src/editor_light_handles.cpp`, move `swatch` out of the anonymous namespace and rename it:

```cpp
/* The colour swatch, below right of the ring: a click opens (or closes) the colour popover. */
Handle rood_handles::colour_swatch(const Ref& r, float cx, float cy, float ring_radius, const float colour[3])
```

(the body unchanged; place it after the anonymous namespace closes, before `rood_handles::light_handles`) and change its two calls `swatch(` to `rood_handles::colour_swatch(` (in `light_handles` and `sun_handles`).

- [ ] **Step 4: The swatch and the inner outline**

Create `pc/src/editor_volume_handles.cpp`:

```cpp
// A fog volume's handles (editor_handles.h): the colour swatch (the colour
// popover on its albedo), the density ring around its centre (logarithmic,
// as a light's intensity) and the soft edge's dot on its inner outline (the
// full-density core, half size x (1 - edge)). R sizes it (atmosphere.cpp's
// gizmo). Built from the fog schema's fields (atmosphere.cpp schema_kind):
// a drag changes exactly one of them.
#include "editor_handles.h"

#include <math.h>
#include <array>
#include <string>

#include "imgui.h"
#include "editor_math.h"
#include "editor_scene.h"

using prefab::Value;
using rood_handles::Drag;
using rood_handles::Handle;
using rood_selection::Ref;

namespace {
using V3 = std::array<float, 3>;
const float DEG = 3.14159265f / 180.0f;
float em() { return ImGui::GetStyle().FontScaleMain; }
bool pixel(const pc_overlay_view& v, const V3& w, float out[2]) { return rood_math::project(v.view, v.projection, v.rect, w.data(), out); }
V3 along(const V3& p, const V3& u, float s) { return { p[0] + u[0] * s, p[1] + u[1] * s, p[2] + u[2] * s }; }
uint32_t colour_of(const Value& c)
{
    return IM_COL32((int)(fminf(c.at(0), 1) * 255), (int)(fminf(c.at(1), 1) * 255), (int)(fminf(c.at(2), 1) * 255), 255);
}
/* A volume's frame from its fields (pos, size, yaw, shape): atmosphere.cpp's
 * corner() turn (x' = (cos, 0, -sin), z' = (sin, 0, cos)). */
struct Frame {
    V3 centre, axis[3];
    float half[3];
    bool sphere;
};
Frame frame_of(const Value& pos, const Value& size, float yaw, bool sphere)
{
    Frame f;
    float c = cosf(yaw * DEG), s = sinf(yaw * DEG);
    f.centre = { pos.at(0), pos.at(1), pos.at(2) };
    f.axis[0] = { c, 0, -s }, f.axis[1] = { 0, 1, 0 }, f.axis[2] = { s, 0, c };
    for (int k = 0; k < 3; k++)
        f.half[k] = size.at(k) * 0.5f;
    f.sphere = sphere;
    return f;
}
Frame frame_now(const Ref& r)
{
    return frame_of(rood_handles::get_value(r, "pos"), rood_handles::get_value(r, "size"), rood_handles::get(r, "yaw"),
        rood_handles::get(r, "shape") > 0.5f);
}
Frame frame_at_press(const Drag& d)
{
    return frame_of(d.field("pos"), d.field("size"), d.field("yaw").x(), d.field("shape").x() > 0.5f);
}
} // namespace

void rood_handles::volume_handles(const pc_overlay_view& v, const Ref& r, int, std::vector<Handle>& out)
{
    Frame f = frame_now(r);
    float c[2];
    if (!pixel(v, f.centre, c))
        return;
    Value colour = rood_handles::get_value(r, "color");
    float rgb[3] = { colour.at(0), colour.at(1), colour.at(2) };
    out.push_back(rood_handles::colour_swatch(r, c[0], c[1], 22 * em(), rgb));
}

/* Its core (the part at full density) as a thin outline inside its own. */
void rood_handles::volume_extras(const pc_overlay_view& v, int index, void* draw_list)
{
    Ref r = rood_scene::volume_ref(index);
    if (!r)
        return;
    Frame f = frame_now(r);
    float inner = 1 - fminf(fmaxf(rood_handles::get(r, "edge"), 0.0f), 1.0f);
    ImDrawList* draw = (ImDrawList*)draw_list;
    ImU32 col = IM_COL32(255, 170, 90, 170);
    auto at = [&](float x, float y, float z) {
        V3 p = along(along(along(f.centre, f.axis[0], x * f.half[0] * inner), f.axis[1], y * f.half[1] * inner), f.axis[2],
            z * f.half[2] * inner);
        return p;
    };
    if (f.sphere) {
        for (int axis = 0; axis < 3; axis++) {
            ImVec2 pts[33];
            int n = 0;
            for (int k = 0; k <= 32; k++) {
                float a = k * 6.2831853f / 32, u = cosf(a), w = sinf(a), px[2];
                V3 p = axis == 0 ? at(u, w, 0) : axis == 1 ? at(u, 0, w) : at(0, u, w);
                if (pixel(v, p, px))
                    pts[n++] = ImVec2(px[0], px[1]);
            }
            if (n > 1)
                draw->AddPolyline(pts, n, col, 0, 1.0f);
        }
        return;
    }
    static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 },
        { 1, 5 }, { 2, 6 }, { 3, 7 } };
    float q[8][2];
    bool ok[8];
    for (int c = 0; c < 8; c++)
        ok[c] = pixel(v, at(c & 1 ? 1 : -1, c & 2 ? 1 : -1, c & 4 ? 1 : -1), q[c]);
    for (const auto& e : edges)
        if (ok[e[0]] && ok[e[1]])
            draw->AddLine(ImVec2(q[e[0]][0], q[e[0]][1]), ImVec2(q[e[1]][0], q[e[1]][1]), col, 1.0f);
}
```

(`frame_at_press`, `colour_of` and `DEG` are used in Task 12; until then mark `frame_at_press` and `colour_of` `[[maybe_unused]]` so `-Wunused-function` stays quiet.)

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_light_handles.cpp",` add:

```python
    root / "pc/src/editor_volume_handles.cpp",
```

- [ ] **Step 5: The colour popover on a volume**

In `pc/src/editor_colour.cpp`:

```cpp
int tabs[5]; /* the last tab per type: point, spot, rect, sun, fog volume */
```

In `type_index`, after the `KIND_SUN` line add `if (r.kind == rood_selection::KIND_VOLUME) return 4;`. In `colour_of`, replace `if (r.kind != rood_selection::KIND_LIGHT || rood_scene::index_of(r) < 0)` with `if ((r.kind != rood_selection::KIND_LIGHT && r.kind != rood_selection::KIND_VOLUME) || rood_scene::index_of(r) < 0)`. In `rood_colour::rpc`'s open, replace `if (r.kind != rood_selection::KIND_LIGHT && r.kind != rood_selection::KIND_SUN) {` with `if (r.kind != rood_selection::KIND_LIGHT && r.kind != rood_selection::KIND_SUN && r.kind != rood_selection::KIND_VOLUME) {` and its message with `"the colour popover needs a light, the sun or a fog volume selected"`. In `state_json`, print five tabs: `\"tabs\":[%d,%d,%d,%d,%d]` with `tabs[4]` added. In `parse_setting`:

```cpp
        int t[5] = { 0, 0, 0, 0, 0 };
        sscanf(rest, "%d %d %d %d %d", &t[0], &t[1], &t[2], &t[3], &t[4]); /* four from before fog volumes: the fifth 0 */
        for (int k = 0; k < 5; k++)
```

and in `write_settings`: `fprintf(f, "editor_colour_tabs %d %d %d %d %d\n", tabs[0], tabs[1], tabs[2], tabs[3], tabs[4]);`. Update the header comment of `editor_colour.h` ("opened from a light's, the sun's or a fog volume's swatch").

- [ ] **Step 6: The gizmo stays off under a handle through the fog gizmo**

In `pc/src/lights.cpp`, replace:

```cpp
    ImGuizmo::Enable(true);
    gizmo_busy |= handles_busy;
    /* Fog volumes (atmosphere.cpp): outlines, the selected one's gizmo. */
    if (pc_atmosphere_viewport(v.view, v.projection, v.rect, draw))
        gizmo_busy = true;
```

with:

```cpp
    gizmo_busy |= handles_busy;
    /* Fog volumes (atmosphere.cpp): outlines, the selected one's gizmo (off while a handle has the mouse). */
    if (pc_atmosphere_viewport(v.view, v.projection, v.rect, draw))
        gizmo_busy = true;
    ImGuizmo::Enable(true);
```

- [ ] **Step 7: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py --only layer,volume,colour,sun
```

Expected: every line `ok:` (the colour test's `"editor_colour_tabs 1 0 0 0"` still matches the five-value line).

- [ ] **Step 8: Commit**

```bash
git add pc/src/editor_volume_handles.cpp pc/src/editor_handles.h pc/src/editor_handles.cpp pc/src/editor_light_handles.cpp pc/src/editor_colour.cpp pc/src/editor_colour.h pc/src/lights.cpp pc/tools/build_boot.py pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
Handles on fog volumes: the layer restores a volume's fields on Escape; its swatch opens the colour popover; its core's inner outline

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: A fog volume's density ring and soft edge dot

**Files:**
- Modify: `pc/src/editor_volume_handles.cpp`, `pc/tests/editor_handles_smoke.py` (`check_volume`), `pc/src/dev_overlay.cpp` (help rows)

- [ ] **Step 1: Extend the failing section**

In `pc/tests/editor_handles_smoke.py`, at the end of `check_volume` add:

```python
    st = handles(s)
    names = {h["name"] for h in st["handles"]}
    s.check({"density", "edge", "colour"} <= names, f"the density ring and the soft edge dot too ({sorted(names)})")
    check_drag(s, s.volume, "density", "density", "Density", 0.008, 0.008)  # 100 px right doubles it
    ring = handle(s, "density")
    drag(s, ring["screen"], [ring["screen"][0] + 60, ring["screen"][1]], hold=["shift"])  # 2^0.6 = 1.52: snaps to 0.005
    s.check(close(value(s, s.volume, "density"), 0.005, 0.01), f"Shift: 1, 2, 5 steps ({value(s, s.volume, 'density')})")
    g.call("editor_undo", action="undo")
    check_drag(s, s.volume, "edge", "edge", "Soft edge", 0.25, 0.25)
    check_escape(s, s.volume, "edge", "edge", 0.8)
    # R: the scale tool still sizes it (only its size) while its handles show.
    s.keys(["r"])
    before = fields(s, s.volume)
    g.call("editor_transform", action="scale", ratio=[2, 1, 1])
    after = fields(s, s.volume)
    s.check(changed(before, after) == ["size"] and close(after["size"][0], before["size"][0] * 2)
            and g.call("editor_transform")["operation"] == 2 and handles(s)["subject"] == s.volume,
            f"R with its handles shown: only its size ({changed(before, after)})")
    g.call("editor_undo", action="undo")
    s.keys(["w"])
```

(`editor_transform_smoke.py`'s fog section, run in Step 4, checks the gizmo's own R drag on the volume.)

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py --only volume
```

Expected: `FAIL: the density ring and the soft edge dot too (['colour'])`.

- [ ] **Step 3: The ring and the dot**

In `pc/src/editor_volume_handles.cpp`, remove the `[[maybe_unused]]` marks and add in the anonymous namespace:

```cpp
/* Density: the light's intensity ring (100 px right doubles it, Ctrl a
 * tenth, Shift 1-2-5), in units of 1e-3 so log_drag's floor (0.01) is
 * 1e-5 here, below any fog worth seeing. */
const float DENSITY_UNIT = 1e-3f;
Handle density_ring(const Ref& r, float cx, float cy, const Value& colour)
{
    Handle h;
    h.name = "density", h.label = "Density";
    h.shape = rood_handles::SHAPE_RING;
    h.cx = cx, h.cy = cy, h.radius = 22 * em();
    h.x = cx + h.radius, h.y = cy;
    h.colour = colour_of(colour);
    h.drag = [r](const Drag& d) {
        float v = rood_math::log_drag(d.field("density").x() / DENSITY_UNIT, d.now[0] - d.press[0], d.fine);
        rood_handles::set(r, "density", Value::scalar((d.snap ? rood_math::snap_125(v) : v) * DENSITY_UNIT));
    };
    h.readout = [r]() { return rood_handles::readout(r, "density"); };
    float gx = h.x, gy = h.y;
    h.where = [r, gx, gy](const Value& v, float out[2]) {
        float now = rood_handles::get(r, "density");
        if (v.x() <= 0 || now <= 0)
            return false;
        out[0] = gx + 100 * log2f(v.x() / now), out[1] = gy;
        return true;
    };
    return h;
}
/* The axis of x and z more across the view (the dot slides on screen, not toward the eye). */
int across_axis(const pc_overlay_view& v, const Frame& f)
{
    float r[3], u[3], fw[3];
    rood_math::view_axes(v.view, v.projection, v.rect, r, u, fw);
    float dx = fabsf(f.axis[0][0] * fw[0] + f.axis[0][1] * fw[1] + f.axis[0][2] * fw[2]);
    float dz = fabsf(f.axis[2][0] * fw[0] + f.axis[2][1] * fw[1] + f.axis[2][2] * fw[2]);
    return dz < dx ? 2 : 0;
}
/* Soft edge: a dot on the core's outline along that axis (at half x (1 - edge));
 * dragged out the core grows (edge falls). Ctrl a tenth; Shift steps of 0.05. */
Handle edge_dot(const pc_overlay_view& v, const Ref& r, const Frame& f)
{
    int a = across_axis(v, f);
    float edge = rood_handles::get(r, "edge");
    Handle h;
    h.name = "edge", h.label = "Soft edge";
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(f.centre, f.axis[a], f.half[a] * (1 - edge)), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = IM_COL32(255, 170, 90, 255);
    h.drag = [r, a](const Drag& d) {
        Frame f0 = frame_at_press(d);
        float half = fmaxf(f0.half[a], 1e-3f);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, f0.centre.data(), f0.axis[a].data());
        float t1 = rood_math::line_param(d.origin, d.dir, f0.centre.data(), f0.axis[a].data());
        float s = rood_math::linear_drag(half * (1 - d.field("edge").x()), t0, t1, d.fine);
        float e = fminf(fmaxf(1 - s / half, 0.01f), 1.0f);
        rood_handles::set(r, "edge", Value::scalar(d.snap ? rood_math::snap_step(e, 0.05f) : e));
    };
    h.readout = [r]() { return rood_handles::readout(r, "edge"); };
    pc_overlay_view view = v;
    Frame at = f;
    h.where = [view, at, a](const Value& value, float out[2]) {
        return pixel(view, along(at.centre, at.axis[a], at.half[a] * (1 - value.x())), out);
    };
    return h;
}
```

and replace the body of `rood_handles::volume_handles` with:

```cpp
    Frame f = frame_now(r);
    float c[2];
    if (!pixel(v, f.centre, c))
        return;
    Value colour = rood_handles::get_value(r, "color");
    Handle ring = density_ring(r, c[0], c[1], colour);
    out.push_back(ring);
    out.push_back(edge_dot(v, r, f));
    float rgb[3] = { colour.at(0), colour.at(1), colour.at(2) };
    out.push_back(rood_handles::colour_swatch(r, c[0], c[1], ring.radius, rgb));
```

In `pc/src/dev_overlay.cpp`, help rows, after the `{ "Handles", "Drag a dot", … }` row add:

```cpp
        { "Handles", "A fog volume's ring / dot / swatch", "Density (doubles every 100 pixels to the right), the soft edge (the inner outline: drag out for a harder edge), its colour" },
```

- [ ] **Step 4: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_transform editor_selection
```

Expected: every handles section passes (point, spot, rect, sun, colour, eyedropper unchanged); the transform smoke's fog volume scale still passes.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_volume_handles.cpp pc/tests/editor_handles_smoke.py pc/src/dev_overlay.cpp
git commit -m "$(cat <<'EOF'
Fog volume handles: the density ring (log, Ctrl fine, Shift 1-2-5) and the soft edge dot on its core's outline; one field and one undo step per drag

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
## Phase E: the library

### Task 13: Placement maths: a fog footprint on a floor, a window pane

**Files:**
- Modify: `pc/src/editor_math.h`, `pc/src/editor_math.cpp`, `pc/tests/editor_tools_probe.cpp`

- [ ] **Step 1: Write the failing checks**

In `pc/tests/editor_tools_probe.cpp`, before `int main()` add:

```cpp
/* A fog volume dropped on a floor keeps its footprint inside the floor's
 * area; "fit to window" takes the pane around the clicked triangle. */
static void placement()
{
    const float lo[2] = { 0, 0 }, hi[2] = { 400, 1000 };
    float c[2], s[2];
    const float at[2] = { 380, 500 }, want[2] = { 600, 300 };
    rood_math::fit_footprint(at, want, lo, hi, c, s);
    check(near(s[0], 400) && near(s[1], 300) && near(c[0], 200) && near(c[1], 500),
        "a footprint wider than the floor: its width the floor's, centred on it; its depth kept");
    const float corner[2] = { 390, 990 }, small_want[2] = { 100, 100 };
    rood_math::fit_footprint(corner, small_want, lo, hi, c, s);
    check(near(s[0], 100) && near(c[0], 350) && near(c[1], 950), "near a corner: moved in so it fits");
    const float no_lo[2] = { 0, 0 }, no_hi[2] = { 0, 0 };
    rood_math::fit_footprint(at, want, no_lo, no_hi, c, s);
    check(near(s[0], 600) && near(c[0], 380), "no floor area known: as wanted, where dropped");

    /* A 100 x 60 quad in the plane z = 0 (two triangles, texture 7), seen from -z. */
    rood_math::PaneTriangle t[5] = {
        { { { 0, 0, 0 }, { 100, 0, 0 }, { 100, -60, 0 } }, 7 },
        { { { 0, 0, 0 }, { 100, -60, 0 }, { 0, -60, 0 } }, 7 },
        { { { 500, 0, 0 }, { 600, 0, 0 }, { 600, -60, 0 } }, 7 }, /* the same texture and plane, apart */
        { { { 100, 0, 0 }, { 160, 0, 0 }, { 100, -60, 0 } }, 8 }, /* beside it, another texture */
        { { { 0, 0, 0 }, { 0, -60, 0 }, { 0, 0, 80 } }, 7 }, /* beside it, another plane */
    };
    const float eye[3] = { 50, -30, -200 };
    rood_math::Pane p;
    bool ok = rood_math::fit_pane(t, 5, 0, eye, p);
    check(ok && p.triangles == 2 && near(p.width, 100) && near(p.height, 60) && near3(p.centre, 50, -30, 0)
            && near3(p.normal, 0, 0, -1),
        "the pane: its two triangles, 100 x 60, centred, facing the eye");
    check(near3(p.right, 1, 0, 0) && near3(p.up, 0, -1, 0), "its axes: a rect light's own frame (right +x, up -y)");
    const float behind[3] = { 50, -30, 200 };
    rood_math::fit_pane(t, 5, 1, behind, p);
    check(near3(p.normal, 0, 0, 1) && p.triangles == 2, "seen from the other side: facing that way");
    check(!rood_math::fit_pane(t, 5, 9, eye, p), "no such triangle: no pane");
}
```

and in `main`, after `colours();` add `placement();`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" ninja -C pc/build editor_tools_probe
```

Expected: `error: 'fit_footprint' is not a member of 'rood_math'`.

- [ ] **Step 3: The declarations**

In `pc/src/editor_math.h`, before the closing `} // namespace rood_math` add:

```cpp
/* ---- placing (the library's drops, "fit rect light to window") ---- */
/* A fog volume's footprint (x, z) dropped at `at` wanting full sizes `want`,
 * kept inside a floor's area lo..hi: each size at most the area's, the
 * centre moved in so it fits. An empty area (hi <= lo) keeps both as given. */
void fit_footprint(const float at[2], const float want[2], const float lo[2], const float hi[2], float centre[2],
    float size[2]);
/* A room triangle for fit_pane: its corners and its texture (page << 16 | CLUT). */
struct PaneTriangle {
    float v[3][3];
    unsigned texture;
};
/* A flat pane: its centre, the normal facing the eye, the axes of a rect
 * light facing out of it (light_frame of the normal: right, up), its extent
 * along them and how many triangles make it. */
struct Pane {
    float centre[3] = { 0, 0, 0 }, normal[3] = { 0, 0, 1 }, right[3] = { 1, 0, 0 }, up[3] = { 0, -1, 0 };
    float width = 0, height = 0;
    int triangles = 0;
};
/* The pane around triangle `seed`: the triangles reached from it through
 * shared edges (two corners within 0.5 units) that share its texture and lie
 * in its plane (normals within 8 degrees, corners within 2 units), at most
 * 256. False when `seed` is no triangle (out of range, degenerate). */
bool fit_pane(const PaneTriangle* triangles, int count, int seed, const float eye[3], Pane& out);
```

- [ ] **Step 4: The implementation**

In `pc/src/editor_math.cpp`, add `#include <vector>` and before the closing `} // namespace rood_math`:

```cpp
void fit_footprint(const float at[2], const float want[2], const float lo[2], const float hi[2], float centre[2], float size[2])
{
    for (int k = 0; k < 2; k++) {
        float room = hi[k] - lo[k];
        if (room <= 0) {
            size[k] = want[k], centre[k] = at[k];
            continue;
        }
        size[k] = fminf(want[k], room);
        float half = size[k] * 0.5f;
        centre[k] = fminf(fmaxf(at[k], lo[k] + half), hi[k] - half);
    }
}

namespace {
bool triangle_normal(const PaneTriangle& t, float n[3])
{
    float a[3] = { t.v[1][0] - t.v[0][0], t.v[1][1] - t.v[0][1], t.v[1][2] - t.v[0][2] };
    float b[3] = { t.v[2][0] - t.v[0][0], t.v[2][1] - t.v[0][1], t.v[2][2] - t.v[0][2] };
    n[0] = a[1] * b[2] - a[2] * b[1], n[1] = a[2] * b[0] - a[0] * b[2], n[2] = a[0] * b[1] - a[1] * b[0];
    float len = sqrtf(dot(n, n));
    if (len < 1e-6f)
        return false;
    for (int k = 0; k < 3; k++)
        n[k] /= len;
    return true;
}
bool shares_edge(const PaneTriangle& a, const PaneTriangle& b)
{
    int same = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float d[3] = { a.v[i][0] - b.v[j][0], a.v[i][1] - b.v[j][1], a.v[i][2] - b.v[j][2] };
            if (dot(d, d) <= 0.25f) {
                same++;
                break;
            }
        }
    return same >= 2;
}
} // namespace

bool fit_pane(const PaneTriangle* t, int count, int seed, const float eye[3], Pane& out)
{
    out = Pane();
    float n[3];
    if (seed < 0 || seed >= count || !triangle_normal(t[seed], n))
        return false;
    float to_eye[3] = { eye[0] - t[seed].v[0][0], eye[1] - t[seed].v[0][1], eye[2] - t[seed].v[0][2] };
    if (dot(n, to_eye) < 0)
        n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
    float plane = dot(n, t[seed].v[0]);
    const float cos_limit = cosf(8 * 3.14159265f / 180);
    std::vector<int> candidates;
    for (int i = 0; i < count; i++) {
        float m[3];
        if (t[i].texture != t[seed].texture || !triangle_normal(t[i], m) || fabsf(dot(m, n)) < cos_limit)
            continue;
        bool flat = true;
        for (int c = 0; c < 3 && flat; c++)
            flat = fabsf(dot(n, t[i].v[c]) - plane) <= 2;
        if (flat)
            candidates.push_back(i);
    }
    std::vector<int> pane = { seed };
    std::vector<char> taken(candidates.size(), 0);
    for (size_t k = 0; k < candidates.size(); k++)
        taken[k] = candidates[k] == seed;
    for (size_t at = 0; at < pane.size() && pane.size() < 256; at++)
        for (size_t k = 0; k < candidates.size() && pane.size() < 256; k++)
            if (!taken[k] && shares_edge(t[pane[at]], t[candidates[k]])) {
                taken[k] = 1;
                pane.push_back(candidates[k]);
            }
    float f[3];
    light_frame(n, f, out.right, out.up);
    float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
    for (int i : pane)
        for (int c = 0; c < 3; c++) {
            float r = dot(out.right, t[i].v[c]), u = dot(out.up, t[i].v[c]);
            lo[0] = fminf(lo[0], r), hi[0] = fmaxf(hi[0], r), lo[1] = fminf(lo[1], u), hi[1] = fmaxf(hi[1], u);
        }
    float mr = (lo[0] + hi[0]) * 0.5f, mu = (lo[1] + hi[1]) * 0.5f;
    for (int k = 0; k < 3; k++) {
        out.normal[k] = f[k];
        out.centre[k] = out.right[k] * mr + out.up[k] * mu + f[k] * plane;
    }
    out.width = hi[0] - lo[0], out.height = hi[1] - lo[1];
    out.triangles = (int)pane.size();
    return true;
}
```

(`dot` and `light_frame` are this file's: `dot` in its anonymous namespace above, `light_frame` in `rood_math`. If `dot` takes `const float*` both ways it works on `t[i].v[c]` directly.)

- [ ] **Step 5: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" sh -c 'ninja -C pc/build editor_tools_probe && pc/build/editor_tools_probe | tail -8'
```

Expected: every line `ok:`, last line `PASS: editor tools maths`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/editor_math.h pc/src/editor_math.cpp pc/tests/editor_tools_probe.cpp
git commit -m "$(cat <<'EOF'
Editor maths: a fog footprint fitted to a floor's area; the flat pane around a room triangle (fit to window)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: The library's model and the thumbnail renderer

**Files:**
- Create: `pc/src/library_model.h`, `pc/src/library_model.cpp`, `pc/src/library_thumbs.h`, `pc/src/library_thumbs.cpp`, `pc/tests/library_probe.cpp`
- Modify: `pc/src/light_shape.h` (`pc_light_falloff`, `pc_smoothstep`), `pc/CMakeLists.txt`, `pc/tools/build_boot.py`

- [ ] **Step 1: Write the failing probe**

Create `pc/tests/library_probe.cpp`:

```cpp
// The library without the game: its entries and filters (pc/src/library_model.cpp)
// and its thumbnails (library_thumbs.cpp: our renderers' light model on a
// neutral floor and wall, the cache file).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "library_model.h"
#include "library_thumbs.h"
#include "light_shape.h"

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool near(float a, float b, float e = 1e-3f) { return fabsf(a - b) <= e; }

static rood_library::Entry entry(const char* item, const char* kind, const char* name, const char* category, bool mine = false)
{
    rood_library::Entry e;
    e.item = item, e.kind = kind, e.name = name, e.category = category, e.mine = mine;
    e.id = strchr(item, ':') ? strchr(item, ':') + 1 : item;
    return e;
}

static void model()
{
    using namespace rood_library;
    std::vector<Entry> all = { entry("fog:ankle_mist", "fog", "Ankle mist", "Fog presets"),
        entry("prefab:candle", "prefab", "Candle", "Fire"), entry("light:point", "light", "Point light", "Lights"),
        entry("emitter:sparks", "emitter", "Sparks", "Fire"), entry("prefab:my_lamp", "prefab", "My lamp", "My prefabs", true),
        entry("fixture:wall_torch", "fixture", "Wall torch", "Fire"), entry("thing:x", "thing", "X", "Other") };
    all[0].description = "A thin sheet of mist at the feet";
    sort_entries(all);
    std::string order;
    for (const Entry& e : all)
        order += e.kind + " ";
    check(order == "light prefab prefab fixture fog emitter thing ", "sorted by kind: lights, prefabs, fixtures, fog, emitters, then others");
    std::set<std::string> fav = { "prefab:candle" };
    Filter f;
    check(shown(all, f, fav).size() == all.size(), "no filter: everything");
    f.kind = "fog";
    check(shown(all, f, fav).size() == 1 && shown(all, f, fav)[0]->item == "fog:ankle_mist", "a kind chip");
    f = Filter();
    f.search = "MIST feet";
    check(shown(all, f, fav).size() == 1, "search: every word, any case, in the name or description");
    f.search = "fire";
    check(shown(all, f, fav).size() == 3, "search finds a category too");
    f = Filter();
    f.favourites = true;
    check(shown(all, f, fav).size() == 1 && shown(all, f, fav)[0]->item == "prefab:candle", "favourites only");
    f = Filter();
    f.mine = true;
    check(shown(all, f, fav).size() == 1 && shown(all, f, fav)[0]->item == "prefab:my_lamp", "Mine: the user's own");
    f = Filter();
    f.kind = "prefab", f.category = "Fire";
    check(shown(all, f, fav).size() == 1, "a kind and a category");
    std::vector<std::string> cats = categories(all, "prefab");
    check(cats.size() == 2 && cats[0] == "Fire" && cats[1] == "My prefabs", "a kind's categories in order");
    std::vector<std::string> kinds = kinds_present(all);
    check(kinds.size() == 6 && kinds.front() == "light" && kinds.back() == "thing", "the kinds present, in order");
    check(kind_label("fog") == "Fog volumes" && kind_label("emitter") == "Emitters" && kind_label("thing") == "thing",
        "kind labels");
    std::set<std::string> back = parse_favourites(favourites_line(fav).c_str() + strlen("editor_library_favourites "));
    check(back == fav && favourites_line({}).empty(), "favourites: a settings line and back");
}

static rood_thumbs::Scene point_scene(float intensity)
{
    rood_thumbs::Scene s;
    prefab::EvalLight l;
    l.type = prefab::LIGHT_POINT;
    l.position[0] = 0, l.position[1] = -48, l.position[2] = 0;
    l.intensity = intensity, l.radius = 320;
    s.lights.push_back(l);
    return s;
}
static float luma(const std::vector<uint8_t>& p, int size, float u, float v)
{
    int x = (int)(u * (size - 1)), y = (int)(v * (size - 1));
    const uint8_t* q = &p[((size_t)y * size + x) * 4];
    return 0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2];
}

static void thumbs(const char* dir)
{
    check(near(pc_light_falloff(0, 100), 1) && near(pc_light_falloff(50, 100), 0.5625f) && near(pc_light_falloff(120, 100), 0),
        "the renderers' falloff: (1 - (d/r)^2)^2");
    const int n = rood_thumbs::SIZE;
    std::vector<uint8_t> a, b, dark, fogged;
    rood_thumbs::render(point_scene(1.5f), n, a);
    rood_thumbs::render(point_scene(1.5f), n, b);
    check(a.size() == (size_t)n * n * 4 && a == b, "96 x 96 RGBA, the same bytes every time");
    check(luma(a, n, 0.5f, 0.75f) > luma(a, n, 0.05f, 0.95f) + 20, "lit under the light, darker toward the corner");
    rood_thumbs::render(rood_thumbs::Scene(), n, dark);
    check(luma(dark, n, 0.5f, 0.75f) < luma(a, n, 0.5f, 0.75f), "no light: only the ambient");
    check(rood_thumbs::hash(point_scene(1.5f), n) == rood_thumbs::hash(point_scene(1.5f), n)
            && rood_thumbs::hash(point_scene(1.5f), n) != rood_thumbs::hash(point_scene(2), n)
            && rood_thumbs::hash(point_scene(1.5f), n).size() == 16,
        "the hash follows what the picture shows");
    std::string before = rood_thumbs::hash(point_scene(1.5f), n);
    float saved = rood_thumbs::look().floor;
    rood_thumbs::look().floor = 0.3f;
    check(rood_thumbs::hash(point_scene(1.5f), n) != before, "and the scene's look");
    rood_thumbs::look().floor = saved;
    rood_thumbs::Scene spot;
    prefab::EvalLight l;
    l.type = prefab::LIGHT_SPOT;
    l.position[1] = -200, l.direction[0] = 0, l.direction[1] = 1, l.direction[2] = 0;
    l.cone = 30, l.radius = 600, l.intensity = 2;
    spot.lights.push_back(l);
    std::vector<uint8_t> sp;
    rood_thumbs::render(spot, n, sp);
    /* v = 0.64: the floor under the spot (z about 0) from this camera; u = 0.15 there is 46 degrees off its axis */
    check(luma(sp, n, 0.5f, 0.64f) > luma(sp, n, 0.15f, 0.64f) + 15, "a spot: its pool on the floor, dark outside its cone");
    rood_thumbs::Scene fog;
    fog.fog.on = true, fog.fog.density = 0.01f, fog.fog.fill[0] = fog.fog.fill[1] = fog.fog.fill[2] = 0.2f;
    rood_thumbs::render(fog, n, fogged);
    check(fogged != dark && luma(fogged, n, 0.5f, 0.7f) > luma(dark, n, 0.5f, 0.7f), "a fog volume shows over the floor");
    std::string path = std::string(dir) + "/" + rood_thumbs::file_name("fog", "ankle mist/x", "0123456789abcdef");
    check(path.find("fog-ankle_mist_x-0123456789abcdef.rgba") != std::string::npos, "a file name safe for any id");
    std::string error;
    std::vector<uint8_t> back;
    check(rood_thumbs::save(path, n, fogged, &error) && rood_thumbs::load(path, n, back) && back == fogged,
        "the cache file round-trips");
    check(!rood_thumbs::load(path, n * 2, back), "a file of another size is not taken");
}

int main(int argc, char** argv)
{
    model();
    thumbs(argc > 1 ? argv[1] : "library_probe_out");
    printf("%s\n", failures ? "FAIL" : "PASS: library model and thumbnails");
    return failures ? 1 : 0;
}
```

In `pc/CMakeLists.txt`, after the `editor_tools_probe` block add:

```cmake
# The library's entries, filters and thumbnails (pc/src/library_model.cpp,
# library_thumbs.cpp) without the game.
add_executable(library_probe tests/library_probe.cpp src/library_model.cpp src/library_thumbs.cpp src/editor_math.cpp
    src/light_prefabs.cpp)
target_include_directories(library_probe PRIVATE src)
target_compile_features(library_probe PRIVATE cxx_std_17)
target_compile_options(library_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME library_probe COMMAND library_probe ${CMAKE_CURRENT_BINARY_DIR}/library_probe_out)
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" sh -c 'cmake -S pc -B pc/build -G Ninja >/dev/null && ninja -C pc/build library_probe'
```

Expected: `fatal error: library_model.h: No such file or directory`.

- [ ] **Step 3: The falloff in light_shape.h**

In `pc/src/light_shape.h`, before `/* light_shape(type, …` add:

```c
/* Every renderer's falloff of a light at distance d with reach r:
 * (1 - (d / r)^2)^2, 0 beyond it (editor_view.cpp, gl_present.c, the
 * Vulkan shaders); the library's thumbnails use it on the CPU. */
static inline float pc_light_falloff(float d, float r)
{
    float x = r > 0 ? fminf(fmaxf(d / r, 0.0f), 1.0f) : 1.0f, y = 1 - x * x;
    return y * y;
}
/* GLSL's smoothstep. */
static inline float pc_smoothstep(float e0, float e1, float x)
{
    float t = e1 != e0 ? fminf(fmaxf((x - e0) / (e1 - e0), 0.0f), 1.0f) : (x >= e1 ? 1.0f : 0.0f);
    return t * t * (3 - 2 * t);
}
```

- [ ] **Step 4: The model**

Create `pc/src/library_model.h`:

```cpp
/* The library's entries and filters (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Library"; editor_library.h fills and draws them): an entry per thing to
 * place (a basic light, a light prefab, a fixture, a fog preset, an emitter),
 * its kind (its source, and its tile's type icon), name, category,
 * description and tags; filters by kind, category, search words, favourites
 * and the user's own ("Mine"); the favourites' settings line. No Dear ImGui
 * (pc/tests/library_probe.cpp). */
#ifndef ROOD_LIBRARY_MODEL_H
#define ROOD_LIBRARY_MODEL_H
#include <set>
#include <string>
#include <vector>

namespace rood_library {
struct Entry {
    std::string item; /* what placing it names: "light:point", "prefab:candle", "fixture:wall_torch", "fog:ankle_mist", "emitter:<id>" */
    std::string kind; /* "light", "prefab", "fixture", "fog", "emitter", or a registered source's */
    std::string id; /* the item after its kind ("candle"): its thumbnail file's name */
    std::string name, category, description;
    std::vector<std::string> tags;
    bool mine = false; /* the user's own (pc/user/prefabs and its fog/) */
    std::string error; /* a load problem (shown on its tile) */
};
struct Filter {
    std::string kind; /* "": every kind */
    std::string category; /* "": every category */
    std::string search; /* words, all of which must appear (any case) */
    bool favourites = false, mine = false;
};
/* The kinds in the library's order; any other kind ranks after them, by name. */
const std::vector<std::string>& kind_order();
int kind_rank(const std::string& kind);
std::string kind_label(const std::string& kind); /* "Lights", "Light actors", "Fixtures", "Fog volumes", "Emitters"; else the kind */
void sort_entries(std::vector<Entry>& all); /* by kind, then as listed (stable) */
bool matches(const Entry& e, const Filter& f, const std::set<std::string>& favourites);
std::vector<const Entry*> shown(const std::vector<Entry>& all, const Filter& f, const std::set<std::string>& favourites);
std::vector<std::string> kinds_present(const std::vector<Entry>& all); /* in order */
std::vector<std::string> categories(const std::vector<Entry>& all, const std::string& kind); /* first seen first; "" kind: all */
/* "editor_library_favourites <item> <item> ..." ("" when none), and its rest back. */
std::string favourites_line(const std::set<std::string>& favourites);
std::set<std::string> parse_favourites(const char* rest);
} // namespace rood_library
#endif
```

Create `pc/src/library_model.cpp`:

```cpp
// The library's entries and filters (library_model.h).
#include "library_model.h"

#include <ctype.h>
#include <string.h>
#include <algorithm>

namespace rood_library {
namespace {
std::string lower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = (char)tolower((unsigned char)c);
    return out;
}
std::vector<std::string> words(const std::string& s)
{
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!w.empty())
                out.push_back(lower(w)), w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty())
        out.push_back(lower(w));
    return out;
}
} // namespace

const std::vector<std::string>& kind_order()
{
    static const std::vector<std::string> order = { "light", "prefab", "fixture", "fog", "emitter" };
    return order;
}
int kind_rank(const std::string& kind)
{
    const std::vector<std::string>& o = kind_order();
    auto it = std::find(o.begin(), o.end(), kind);
    return it == o.end() ? (int)o.size() : (int)(it - o.begin());
}
std::string kind_label(const std::string& kind)
{
    if (kind == "light")
        return "Lights";
    if (kind == "prefab")
        return "Light actors";
    if (kind == "fixture")
        return "Fixtures";
    if (kind == "fog")
        return "Fog volumes";
    if (kind == "emitter")
        return "Emitters";
    return kind;
}
void sort_entries(std::vector<Entry>& all)
{
    std::stable_sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        int ra = kind_rank(a.kind), rb = kind_rank(b.kind);
        return ra != rb ? ra < rb : (ra == (int)kind_order().size() && a.kind < b.kind);
    });
}
bool matches(const Entry& e, const Filter& f, const std::set<std::string>& favourites)
{
    if (!f.kind.empty() && e.kind != f.kind)
        return false;
    if (!f.category.empty() && e.category != f.category)
        return false;
    if (f.favourites && !favourites.count(e.item))
        return false;
    if (f.mine && !e.mine)
        return false;
    std::string text = lower(e.name + " " + e.category + " " + e.description + " " + kind_label(e.kind) + " " + e.item);
    for (const std::string& t : e.tags)
        text += " " + lower(t);
    for (const std::string& w : words(f.search))
        if (text.find(w) == std::string::npos)
            return false;
    return true;
}
std::vector<const Entry*> shown(const std::vector<Entry>& all, const Filter& f, const std::set<std::string>& favourites)
{
    std::vector<const Entry*> out;
    for (const Entry& e : all)
        if (matches(e, f, favourites))
            out.push_back(&e);
    return out;
}
std::vector<std::string> kinds_present(const std::vector<Entry>& all)
{
    std::vector<std::string> out;
    for (const Entry& e : all)
        if (std::find(out.begin(), out.end(), e.kind) == out.end())
            out.push_back(e.kind);
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        int ra = kind_rank(a), rb = kind_rank(b);
        return ra != rb ? ra < rb : a < b;
    });
    return out;
}
std::vector<std::string> categories(const std::vector<Entry>& all, const std::string& kind)
{
    std::vector<std::string> out;
    for (const Entry& e : all)
        if ((kind.empty() || e.kind == kind) && std::find(out.begin(), out.end(), e.category) == out.end())
            out.push_back(e.category);
    return out;
}
std::string favourites_line(const std::set<std::string>& favourites)
{
    if (favourites.empty())
        return std::string();
    std::string s = "editor_library_favourites";
    for (const std::string& f : favourites)
        s += " " + f;
    return s;
}
std::set<std::string> parse_favourites(const char* rest)
{
    std::set<std::string> out;
    for (const std::string& w : words(rest ? rest : "")) /* items have no spaces; lowercase is how they are written */
        out.insert(w);
    return out;
}
} // namespace rood_library
```

(Items are lowercase ids (`prefab::make_id`, preset ids, `light:point`), so `parse_favourites` lowercasing them is harmless; if a source ever uses capitals, split without lowering instead.)

- [ ] **Step 5: The thumbnail renderer**

Create `pc/src/library_thumbs.h`:

```cpp
/* The library's thumbnails (pc/docs/plans/2026-10-16-artist-tools-de-plan.md,
 * decision 7): an item lit in a neutral scene (a grey floor at y = 0 with a
 * 64-unit grid, a back wall), rendered on the CPU with our renderers' light
 * model (light_shape.h: the falloff, N.L, a spot's edge, a rect's front face
 * and barn doors) and a fog volume's soft box marched through, so OpenGL and
 * Vulkan show the same picture and the bytes are the same every run. Cached
 * as <kind>-<id>-<hash>.rgba under pc/build/library_thumbs (ROOD_LIBRARY_THUMBS);
 * never committed. No Dear ImGui (pc/tests/library_probe.cpp). */
#ifndef ROOD_LIBRARY_THUMBS_H
#define ROOD_LIBRARY_THUMBS_H
#include <stdint.h>
#include <string>
#include <vector>

#include "light_prefabs.h"

namespace rood_thumbs {
constexpr int SIZE = 96;
constexpr int VERSION = 1; /* bump whenever render() draws differently: every cached name changes */
/* The neutral scene's look (RPC editor_library {action "thumbs", look}); part of every hash. */
struct Look {
    float floor = 0.5f, wall = 0.42f, grid = 0.8f; /* albedos; the grid lines' factor on the floor */
    float ambient = 0.04f; /* light everywhere on the surfaces */
    float fog_ambient = 0.25f; /* light everywhere in a fog volume (the sky, the room) */
    float exposure = 1.8f; /* display = 1 - exp(-exposure x light), then sRGB */
    float background[3] = { 0.03f, 0.03f, 0.035f };
    float fov = 40; /* degrees, vertical */
};
Look& look();
struct Fog {
    bool on = false, sphere = false;
    float size[3] = { 256, 128, 256 }; /* full extents; it rests on the floor, centred on x = z = 0 */
    float density = 0.004f, edge = 0.5f;
    float color[3] = { 1, 1, 1 }, fill[3] = { 0, 0, 0 }, glow[3] = { 0, 0, 0 };
};
struct Scene {
    std::vector<prefab::EvalLight> lights; /* world: y down, the floor at y = 0, the item about x = z = 0 */
    Fog fog;
    float wall_z = 200; /* the back wall's plane (facing -z) */
};
/* size x size RGBA (sRGB, opaque). */
void render(const Scene& s, int size, std::vector<uint8_t>& rgba);
/* What the picture depends on (the scene, the look, the size, VERSION): 16 hex digits. */
std::string hash(const Scene& s, int size);
std::string cache_dir(); /* ROOD_LIBRARY_THUMBS, else pc/build/library_thumbs */
std::string file_name(const std::string& kind, const std::string& id, const std::string& hash);
/* The cache file: "RTHB", width, height, VERSION (uint32 each, little-endian), then the pixels. */
bool load(const std::string& path, int size, std::vector<uint8_t>& rgba);
bool save(const std::string& path, int size, const std::vector<uint8_t>& rgba, std::string* error);
} // namespace rood_thumbs
#endif
```

Create `pc/src/library_thumbs.cpp`:

```cpp
// The library's thumbnails (library_thumbs.h).
#include "library_thumbs.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor_math.h"
#include "light_shape.h"
#include "platform.h"

namespace rood_thumbs {
namespace {
float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void normalize3(float* v)
{
    float l = sqrtf(dot3(v, v));
    if (l > 1e-9f)
        v[0] /= l, v[1] /= l, v[2] /= l;
}
struct Camera {
    float eye[3], forward[3], right[3], up[3], tan_half;
};
/* From the front and above (y down), looking at the item, far enough for its reach. */
Camera camera_for(const Scene& s)
{
    float extent = 96;
    for (const prefab::EvalLight& l : s.lights)
        extent = fmaxf(extent, fminf(l.radius * 0.6f, 900.0f));
    if (s.fog.on)
        extent = fmaxf(extent, 0.6f * fmaxf(s.fog.size[0], fmaxf(s.fog.size[1], s.fog.size[2])));
    extent = fminf(extent, 1200.0f);
    Camera c;
    c.forward[0] = 0, c.forward[1] = 0.45f, c.forward[2] = 1;
    normalize3(c.forward);
    float focus[3] = { 0, -extent * 0.25f, 0 }, distance = extent * 2.2f;
    for (int k = 0; k < 3; k++)
        c.eye[k] = focus[k] - c.forward[k] * distance;
    c.right[0] = 1, c.right[1] = 0, c.right[2] = 0;
    c.up[0] = 0, c.up[1] = -c.forward[2], c.up[2] = c.forward[1]; /* across the forward, toward -y */
    c.tan_half = tanf(look().fov * 0.5f * 3.14159265f / 180);
    return c;
}
/* One light's contribution at p (normal n; null: a fog sample, no N.L), as the renderers evaluate it. */
void add_light(const prefab::EvalLight& l, const float p[3], const float* n, float out[3])
{
    if (!l.enabled || l.intensity <= 0)
        return;
    float f[3], r[3], u[3], centre[3] = { l.position[0], l.position[1], l.position[2] };
    rood_math::light_frame(l.direction, f, r, u);
    float hr[3], hu[3];
    for (int k = 0; k < 3; k++)
        hr[k] = r[k] * l.size[0] * 0.5f, hu[k] = u[k] * l.size[1] * 0.5f;
    if (l.type == prefab::LIGHT_RECT) { /* the nearest point of its rectangle (editor_view.cpp) */
        float rel[3] = { p[0] - centre[0], p[1] - centre[1], p[2] - centre[2] };
        float a = fminf(fmaxf(dot3(rel, hr) / fmaxf(dot3(hr, hr), 1e-6f), -1.0f), 1.0f);
        float b = fminf(fmaxf(dot3(rel, hu) / fmaxf(dot3(hu, hu), 1e-6f), -1.0f), 1.0f);
        for (int k = 0; k < 3; k++)
            centre[k] += hr[k] * a + hu[k] * b;
    }
    float to[3] = { centre[0] - p[0], centre[1] - p[1], centre[2] - p[2] };
    float d = sqrtf(dot3(to, to));
    float ld[3] = { to[0] / fmaxf(d, 1e-3f), to[1] / fmaxf(d, 1e-3f), to[2] / fmaxf(d, 1e-3f) };
    float a = pc_light_falloff(d, l.radius) * (n ? fmaxf(dot3(n, ld), 0.0f) : 1.0f);
    float c = -dot3(ld, f);
    if (l.type == prefab::LIGHT_SPOT)
        a *= pc_smoothstep(cosf(l.cone * 0.5f * 3.14159265f / 180), pc_spot_cos_inner(l.cone, l.inner), c);
    if (l.type == prefab::LIGHT_RECT) {
        float ba, bb;
        pc_barn_pack(l.barn_angle, l.barn_length, &ba, &bb);
        a *= fmaxf(c, 0.0f) * pc_barn_doors(l.position, hr, hu, f, ba, bb, p);
    }
    for (int k = 0; k < 3; k++)
        out[k] += l.color[k] * l.intensity * a;
}
float encode(float linear)
{
    float c = 1 - expf(-look().exposure * fmaxf(linear, 0.0f));
    c = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f;
    return fminf(fmaxf(c, 0.0f), 1.0f);
}
/* Where the ray enters and leaves the fog volume (it rests on the floor, centred on x = z = 0). */
bool fog_span(const Fog& fog, const float o[3], const float d[3], float* t0, float* t1)
{
    float half[3] = { fog.size[0] * 0.5f, fog.size[1] * 0.5f, fog.size[2] * 0.5f };
    float centre[3] = { 0, -half[1], 0 };
    float lo = 0, hi = 1e30f;
    if (fog.sphere) { /* the ellipsoid as a unit sphere */
        float q[3], v[3];
        for (int k = 0; k < 3; k++)
            q[k] = (o[k] - centre[k]) / half[k], v[k] = d[k] / half[k];
        float a = dot3(v, v), b = dot3(q, v), c = dot3(q, q) - 1, disc = b * b - a * c;
        if (disc <= 0)
            return false;
        float s = sqrtf(disc);
        lo = fmaxf((-b - s) / a, 0.0f), hi = (-b + s) / a;
    } else {
        for (int k = 0; k < 3; k++) {
            if (fabsf(d[k]) < 1e-9f) {
                if (fabsf(o[k] - centre[k]) > half[k])
                    return false;
                continue;
            }
            float a = (centre[k] - half[k] - o[k]) / d[k], b = (centre[k] + half[k] - o[k]) / d[k];
            lo = fmaxf(lo, fminf(a, b)), hi = fminf(hi, fmaxf(a, b));
        }
    }
    *t0 = lo, *t1 = hi;
    return hi > lo;
}
} // namespace

Look& look()
{
    static Look l;
    return l;
}

void render(const Scene& s, int size, std::vector<uint8_t>& rgba)
{
    rgba.assign((size_t)size * size * 4, 255);
    const Look& lk = look();
    Camera cam = camera_for(s);
    const int steps = 40;
    for (int j = 0; j < size; j++)
        for (int i = 0; i < size; i++) {
            float u = ((i + 0.5f) / size * 2 - 1) * cam.tan_half, v = (1 - (j + 0.5f) / size * 2) * cam.tan_half;
            float d[3];
            for (int k = 0; k < 3; k++)
                d[k] = cam.forward[k] + cam.right[k] * u + cam.up[k] * v;
            normalize3(d);
            float t = 1e30f, n[3] = { 0, 0, 0 }, albedo = 0;
            if (d[1] > 1e-6f) { /* the floor, y = 0 (the camera is above it: y < 0) */
                float tf = -cam.eye[1] / d[1], z = cam.eye[2] + d[2] * tf;
                if (tf > 0 && z <= s.wall_z) {
                    t = tf, n[1] = -1;
                    float x = cam.eye[0] + d[0] * tf;
                    bool line = fabsf(fmodf(fabsf(x) + 2, 64.0f)) < 4 || fabsf(fmodf(fabsf(z) + 2, 64.0f)) < 4;
                    albedo = lk.floor * (line ? lk.grid : 1.0f);
                }
            }
            if (d[2] > 1e-6f) { /* the back wall, z = wall_z, facing -z */
                float tw = (s.wall_z - cam.eye[2]) / d[2], y = cam.eye[1] + d[1] * tw;
                if (tw > 0 && tw < t && y <= 0)
                    t = tw, n[0] = n[1] = 0, n[2] = -1, albedo = lk.wall;
            }
            float colour[3] = { lk.background[0], lk.background[1], lk.background[2] };
            if (t < 1e29f) {
                float p[3] = { cam.eye[0] + d[0] * t, cam.eye[1] + d[1] * t, cam.eye[2] + d[2] * t };
                float light[3] = { lk.ambient, lk.ambient, lk.ambient };
                for (const prefab::EvalLight& l : s.lights)
                    add_light(l, p, n, light);
                for (int k = 0; k < 3; k++)
                    colour[k] = albedo * light[k];
            }
            float t0, t1;
            if (s.fog.on && fog_span(s.fog, cam.eye, d, &t0, &t1) && t0 < t) {
                t1 = fminf(t1, t);
                float dt = (t1 - t0) / steps, transmit = 1, gathered[3] = { 0, 0, 0 };
                float half[3] = { s.fog.size[0] * 0.5f, s.fog.size[1] * 0.5f, s.fog.size[2] * 0.5f };
                for (int k = 0; k < steps; k++) {
                    float tt = t0 + (k + 0.5f) * dt, p[3] = { cam.eye[0] + d[0] * tt, cam.eye[1] + d[1] * tt, cam.eye[2] + d[2] * tt };
                    float q[3] = { p[0] / half[0], (p[1] + half[1]) / half[1], p[2] / half[2] };
                    float r = s.fog.sphere ? sqrtf(dot3(q, q)) : fmaxf(fabsf(q[0]), fmaxf(fabsf(q[1]), fabsf(q[2])));
                    float sigma = s.fog.density * (1 - pc_smoothstep(1 - fmaxf(s.fog.edge, 0.01f), 1, r));
                    if (sigma <= 0)
                        continue;
                    float in[3] = { lk.fog_ambient + s.fog.fill[0], lk.fog_ambient + s.fog.fill[1], lk.fog_ambient + s.fog.fill[2] };
                    for (const prefab::EvalLight& l : s.lights)
                        add_light(l, p, nullptr, in);
                    for (int c = 0; c < 3; c++)
                        gathered[c] += transmit * (s.fog.color[c] * in[c] + s.fog.glow[c]) * sigma * dt;
                    transmit *= expf(-sigma * dt);
                }
                for (int c = 0; c < 3; c++)
                    colour[c] = colour[c] * transmit + gathered[c];
            }
            uint8_t* px = &rgba[((size_t)j * size + i) * 4];
            for (int c = 0; c < 3; c++)
                px[c] = (uint8_t)(encode(colour[c]) * 255 + 0.5f);
            px[3] = 255;
        }
}

std::string hash(const Scene& s, int size)
{
    const Look& lk = look();
    char text[512];
    std::string desc;
    snprintf(text, sizeof text, "v%d %d look %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g wall %.6g\n", VERSION, size,
        lk.floor, lk.wall, lk.grid, lk.ambient, lk.fog_ambient, lk.exposure, lk.background[0], lk.background[1],
        lk.background[2], lk.fov, s.wall_z);
    desc += text;
    for (const prefab::EvalLight& l : s.lights) {
        snprintf(text, sizeof text,
            "l %d %d %.6g %.6g %.6g c %.6g %.6g %.6g d %.6g %.6g %.6g i %.6g r %.6g %.6g %.6g %.6g %.6g %.6g %.6g\n", l.type,
            l.enabled ? 1 : 0, l.position[0], l.position[1], l.position[2], l.color[0], l.color[1], l.color[2],
            l.direction[0], l.direction[1], l.direction[2], l.intensity, l.radius, l.cone, l.inner, l.size[0], l.size[1],
            l.barn_angle, l.barn_length);
        desc += text;
    }
    if (s.fog.on) {
        const Fog& f = s.fog;
        snprintf(text, sizeof text, "f %d %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g\n",
            f.sphere ? 1 : 0, f.size[0], f.size[1], f.size[2], f.density, f.edge, f.color[0], f.color[1], f.color[2], f.fill[0],
            f.fill[1], f.fill[2], f.glow[0], f.glow[1], f.glow[2]);
        desc += text;
    }
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : desc)
        h = (h ^ c) * 1099511628211ULL;
    snprintf(text, sizeof text, "%016llx", (unsigned long long)h);
    return text;
}

std::string cache_dir()
{
    const char* e = getenv("ROOD_LIBRARY_THUMBS");
    return e && *e ? e : "pc/build/library_thumbs";
}
std::string file_name(const std::string& kind, const std::string& id, const std::string& h)
{
    std::string safe;
    for (char c : id)
        safe += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return kind + "-" + (safe.empty() ? "default" : safe) + "-" + h + ".rgba";
}
namespace {
void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
uint32_t get32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void make_dirs(const std::string& dir)
{
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/')
            pc_mkdir(dir.substr(0, i).c_str());
}
} // namespace
bool load(const std::string& path, int size, std::vector<uint8_t>& rgba)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    uint8_t head[16];
    size_t want = (size_t)size * size * 4;
    bool ok = fread(head, 1, 16, f) == 16 && !memcmp(head, "RTHB", 4) && get32(head + 4) == (uint32_t)size
        && get32(head + 8) == (uint32_t)size && get32(head + 12) == (uint32_t)VERSION;
    if (ok) {
        rgba.resize(want);
        ok = fread(rgba.data(), 1, want, f) == want;
    }
    fclose(f);
    return ok;
}
bool save(const std::string& path, int size, const std::vector<uint8_t>& rgba, std::string* error)
{
    size_t slash = path.rfind('/');
    if (slash != std::string::npos)
        make_dirs(path.substr(0, slash));
    std::string temp = path + ".tmp";
    FILE* f = fopen(temp.c_str(), "wb");
    if (!f) {
        if (error)
            *error = "cannot write " + path + ": " + strerror(errno);
        return false;
    }
    uint8_t head[16];
    memcpy(head, "RTHB", 4);
    put32(head + 4, (uint32_t)size), put32(head + 8, (uint32_t)size), put32(head + 12, (uint32_t)VERSION);
    bool ok = fwrite(head, 1, 16, f) == 16 && fwrite(rgba.data(), 1, rgba.size(), f) == rgba.size();
    fclose(f);
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    return true;
}
} // namespace rood_thumbs
```

(add `#include <ctype.h>` for `isalnum`.)

- [ ] **Step 6: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" sh -c 'ninja -C pc/build library_probe && pc/build/library_probe pc/build/library_probe_out | tail -6'
```

Expected: every line `ok:`, `PASS: library model and thumbnails`. If a lighting check fails by a margin (`lit under the light…`), look at the probe's picture: `python3 -c "import numpy,PIL.Image as I;d=open('pc/build/library_probe_out/fog-ankle_mist_x-0123456789abcdef.rgba','rb').read()[16:];I.fromarray(numpy.frombuffer(d,numpy.uint8).reshape(96,96,4)).save('pc/build/library_probe_out/fog.png')"` and fix the camera or the sample points, not the falloff.

- [ ] **Step 7: Build them into the game**

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_colour.cpp",` add:

```python
    root / "pc/src/library_model.cpp",
    root / "pc/src/library_thumbs.cpp",
```

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
```

Expected: exit 0.

- [ ] **Step 8: Commit**

```bash
git add pc/src/library_model.h pc/src/library_model.cpp pc/src/library_thumbs.h pc/src/library_thumbs.cpp pc/tests/library_probe.cpp pc/src/light_shape.h pc/CMakeLists.txt pc/tools/build_boot.py
git commit -m "$(cat <<'EOF'
Library model (entries, kind/category/search/favourites/Mine filters) and thumbnails (our light model on a neutral floor and wall, cached by hash)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 15: The library's sources, filters, favourites and RPC

**Files:**
- Create: `pc/src/editor_library.h`, `pc/src/editor_library.cpp`, `pc/tests/editor_library_smoke.py`
- Modify: `pc/src/environment_internal.h`, `pc/src/atmosphere.cpp` (`FogPreset`, `fog_presets`, `fog_presets_serial`, `preset_look`), `pc/src/dev_rpc.c` (route `editor_library`, `edits`), `pc/src/project_settings.cpp` (settings lines), `pc/tests/editor_selection_smoke.py` (`start(…, extra_env)`), `pc/tests/run.py`, `pc/tools/build_boot.py`

- [ ] **Step 1: Write the failing test**

In `pc/tests/editor_selection_smoke.py`, give `start` an `extra_env` parameter:

```python
def start(args, name, extra_env=None):
```

and change `env = {...}` there to `env = {..., **(extra_env or {})}` (the existing keys first, the extra ones winning).

Create `pc/tests/editor_library_smoke.py`:

```python
#!/usr/bin/env python3
"""The editor's library (pc/src/editor_library.h): one panel for basic lights,
light prefabs, fixtures, fog volume presets and (when a source registers
them) emitters.

    python3 pc/tests/editor_library_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The save room with the editor open (editor_selection_smoke.py's start: a
point light, a candle and a fog volume around Ashley), scratch prefab and
thumbnail directories (ROOD_PREFABS, ROOD_LIBRARY_THUMBS). Sections:

- sources: every kind's entries (point, spot and rect lights; the built-in
  prefabs; the fixture catalog's light classes; a plain fog volume and the
  fog presets), each with a name and a category; no emitters without their
  source;
- filters: a kind chip, a category, search words, favourites (a user
  setting) and Mine;
"""
import argparse
import math
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from editor_selection_smoke import centre, distance, finish, pinned_card, start  # noqa: E402
from editor_schema_smoke import close, schema, value  # noqa: E402


def lib(s, **params):
    return s.game.call("editor_library", **params)


def items(r):
    return [e["item"] for e in r["entries"]]


def check_sources(s):
    r = lib(s, action="filter", kind="", category="", search="", favourites=False, mine=False)
    got = set(items(r))
    want = {"light:point", "light:spot", "light:rect", "prefab:candle", "prefab:torch", "prefab:lantern",
            "fixture:wall_torch", "fixture:candle", "fog:", "fog:ankle_mist"}
    s.check(want <= got, f"every kind's entries ({sorted(want - got)} missing)")
    s.check(r["kinds"][:4] == ["light", "prefab", "fixture", "fog"] and "emitter" not in r["kinds"],
            f"the kinds in order, no emitters without their source ({r['kinds']})")
    s.check(all(e["name"] and e["category"] and e["kind"] for e in r["entries"]), "each has a name, a category, a kind")
    s.check(not any(e["item"] == "fixture:cobweb" for e in r["entries"]), "fixture classes that are no light are left out")


def check_filters(s):
    g = s.game
    r = lib(s, action="filter", kind="fog")
    s.check(r["entries"] and all(e["kind"] == "fog" for e in r["entries"]) and r["filter"]["kind"] == "fog",
            f"a kind chip: fog volumes only ({len(r['entries'])})")
    r = lib(s, action="filter", kind="", search="ankle mist")
    s.check(items(r) == ["fog:ankle_mist"], f"search: every word ({items(r)})")
    r = lib(s, action="filter", search="", kind="prefab", category="Fire")
    s.check(r["entries"] and all(e["category"] == "Fire" and e["kind"] == "prefab" for e in r["entries"]),
            f"a kind and a category ({items(r)})")
    lib(s, action="filter", kind="", category="")
    lib(s, action="favourite", item="prefab:candle", on=True)
    r = lib(s, action="filter", favourites=True)
    s.check(items(r) == ["prefab:candle"] and r["favourites"] == ["prefab:candle"], f"favourites only ({items(r)})")
    g.call("settings", action="save")
    s.check("editor_library_favourites prefab:candle" in pathlib.Path(s.settings).read_text(),
            "the favourites are a user setting")
    lib(s, action="favourite", item="prefab:candle", on=False)
    r = lib(s, action="filter", favourites=False, mine=True)
    s.check(r["entries"] == [], f"Mine: nothing of the user's yet ({items(r)})")
    lib(s, action="filter", mine=False)


SECTIONS = [check_sources, check_filters]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (sources, filters, ...)")
    args = parser.parse_args()
    work = pathlib.Path(args.out) if args.out else OUT / "editor_library"  # start() writes its own files here too
    for sub in ("prefabs", "thumbs"):  # a rerun starts with no prefabs of the user's and no cached thumbnails
        shutil.rmtree(work / sub, ignore_errors=True)
    s = start(args, "editor_library", extra_env={"ROOD_PREFABS": str(work / "prefabs"),
                                                  "ROOD_LIBRARY_THUMBS": str(work / "thumbs")})
    if s is not None:
        s.work = work
    if s is None:
        return 77
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                section(s)
    finally:
        status = finish(s, "the library: sources, filters, favourites, thumbnails, drops on floors and walls, "
                           "the panel, Save as prefab")
    return status


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `emissive_entries_smoke` suite add:

```python
            Suite("editor_library_smoke", [PY, str(TESTS / "editor_library_smoke.py"), "--card", str(args.save)], 600,
                  note="the library: sources, filters, favourites, thumbnails (deterministic, cached), drops on a floor "
                       "and a wall per kind (one undo step, selected), a real drag from a tile, the panel's views, "
                       "Save as prefab round trips (lights, an actor, a fog volume)"),
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py --only sources
```

Expected: an `RpcError` `unknown method` for `editor_library`.

- [ ] **Step 3: Fog presets for the library**

In `pc/src/environment_internal.h`, in `namespace rood_atmo` (with a forward declaration `namespace rood_thumbs { struct Fog; }` above it) add:

```cpp
/* Fog presets for the library (editor_library.cpp): the built-in ones
 * (fog_presets.json) and the user's (Task 18), in file order. */
struct FogPreset {
    std::string id, name, why;
    bool user = false;
};
std::vector<FogPreset> fog_presets();
unsigned fog_presets_serial(); /* bumps on every (re)load */
/* A preset's look for its thumbnail ("": a new volume's defaults); false when there is no such preset. */
bool preset_look(const std::string& id, rood_thumbs::Fog& out);
```

(and `#include <vector>`). In `pc/src/atmosphere.cpp`, add `#include "library_thumbs.h"`, a `unsigned presets_serial;` beside `presets_loaded`, `++presets_serial;` at the end of `load_presets` (every exit path: put it first in the function), and at the end of the file:

```cpp
/* ---- fog presets for the library (environment_internal.h) ---- */

std::vector<rood_atmo::FogPreset> rood_atmo::fog_presets()
{
    if (!presets_loaded)
        load_presets();
    std::vector<FogPreset> out;
    for (const Preset& p : presets)
        out.push_back({ p.id, p.name, p.why, p.user });
    return out;
}
unsigned rood_atmo::fog_presets_serial()
{
    if (!presets_loaded)
        load_presets();
    return presets_serial;
}
bool rood_atmo::preset_look(const std::string& id, rood_thumbs::Fog& out)
{
    Volume v;
    if (!id.empty()) {
        const Preset* p = find_preset(id);
        if (!p)
            return false;
        apply_preset(v, *p);
    }
    out = rood_thumbs::Fog();
    out.on = true, out.sphere = v.sphere;
    for (int k = 0; k < 3; k++) {
        out.size[k] = v.size[k];
        out.color[k] = v.color[k], out.fill[k] = v.fill[k], out.glow[k] = v.glow[k];
    }
    out.density = v.density, out.edge = v.edge;
    return true;
}
```

`Preset` gains `bool user = false; std::string path;` (Task 18 fills them; built-in presets keep `false`).

- [ ] **Step 4: The library header**

Create `pc/src/editor_library.h`:

```cpp
/* The editor's library (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Library"; pc/docs/plans/2026-10-16-artist-tools-de-plan.md decisions 5-9):
 * one panel for everything one places: basic lights, light prefabs,
 * fixtures, fog volume presets and whatever else registers a source (the
 * emitters). Each source is a kind: it lists entries (library_model.h),
 * places one where a drop lands, and describes its thumbnail's scene
 * (library_thumbs.h). Drops use the lights' drag and drop (lights_internal.h
 * place_*): rood_lights::place_commit hands every item that is not a basic
 * light or a light prefab to place() here. Editor internals; RPC editor_library. */
#ifndef ROOD_EDITOR_LIBRARY_H
#define ROOD_EDITOR_LIBRARY_H
#include <stdio.h>
#include <functional>
#include <string>
#include <vector>

#include "editor_overlays.h"
#include "library_model.h"
#include "library_thumbs.h"
#include "lights_internal.h"

namespace rood_library {
/* A source of entries: one per kind. An emitters source, say:
 *
 *     rood_library::Source s;
 *     s.kind = "emitter";
 *     s.serial = [] { return emitters::serial(); };
 *     s.list = [](std::vector<rood_library::Entry>& out) { ... one per emitters::list() ... };
 *     s.place = [](const rood_library::Entry& e, const rood_lights::Placement& w) { return emitters::place(e.id, w.surface, w.normal); };
 *     rood_library::add_source(s);
 */
struct Source {
    std::string kind;
    std::function<unsigned()> serial; /* changes whenever its entries may have */
    std::function<void(std::vector<Entry>&)> list; /* its entries (kind, item "<kind>:<id>", id, name, category, ...) */
    /* Put `e` where a drop landed (rood_lights::place_compute's Placement):
     * the new object's index, selected; -1 when it cannot. The caller names
     * the undo step ("Place <name>"). */
    std::function<int(const Entry& e, const rood_lights::Placement& where)> place;
    /* Its thumbnail's scene; false (or null): the tile shows its type icon only. */
    std::function<bool(const Entry& e, rood_thumbs::Scene& out)> scene;
    /* Optional: what it looks like while dragged over the view (a fog volume's box). */
    std::function<void(const Entry& e, const pc_overlay_view& v, const rood_lights::Placement& where, void* draw_list)> ghost;
    /* Optional: the prefab whose placement (align, surface offset) and lit preview a drop uses (a fixture's). */
    std::function<const prefab::Prefab*(const Entry& e)> prefab;
};
void add_source(Source s); /* any time; replaces the source of the same kind */
std::vector<std::string> source_kinds(); /* registered, in kind order */
const std::vector<Entry>& entries(); /* every source's, sorted by kind (rebuilt when a source's serial changes) */
const Entry* find(const std::string& item);
/* An item placing goes through here: one of the library's, not a basic light ("light:") or a light prefab ("prefab:"). */
bool is_item(const std::string& item);
const prefab::Prefab* item_prefab(const std::string& item); /* null: none */
std::string label(const std::string& item); /* its name, else the item */
int place(const std::string& item, const rood_lights::Placement& where);
void draw_ghost(const std::string& item, const pc_overlay_view& v, const rood_lights::Placement& where, void* draw_list);

/* The panel (Task 17), Save as prefab (Task 18). */
void panel(); /* the side column's Library tab, or the floating window's body */
bool docked(); /* in the side column (else a floating window) */
void window(); /* each overlay frame: the floating window when undocked */
std::string save_selection(const std::string& name, std::string* message); /* the new item, or "" and why */

/* Settings lines (project_settings.cpp): editor_library_view, editor_library_docked,
 * editor_library_window, editor_library_favourites. */
bool parse_setting(const char* key, const char* rest);
void write_settings(FILE* f);
} // namespace rood_library

extern "C" int pc_editor_library_rpc(const char* params, char* out, int size);
#endif
```

- [ ] **Step 5: The sources and the RPC**

Create `pc/src/editor_library.cpp`:

```cpp
// The editor's library (editor_library.h): its sources (basic lights, light
// prefabs, fixtures, fog presets; more by add_source), placing their items,
// settings and RPC editor_library. The panel and thumbnails: Task 17; Save
// as prefab: Task 18.
#include "editor_library.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>

#include "imgui.h"
#include "editor_math.h"
#include "editor_scene.h"
#include "editor_selection.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "fixture_catalog.h"

using prefab::Json;
using prefab::Value;
using rood_library::Entry;
using rood_library::Source;
using rood_lights::Placement;

namespace {
std::vector<Source>& sources()
{
    static std::vector<Source> s;
    return s;
}
unsigned sources_serial = 1; /* bumps when a source is added or replaced */
void ensure_builtins();

std::vector<Entry> all_entries;
std::vector<unsigned> seen_serials;
unsigned seen_sources;
rood_library::Filter filter;
std::set<std::string> favourites;
bool grid_view = true, in_column = true;
float window_rect[4] = { -1, -1, -1, -1 }; /* the floating window: x, y, w, h (-1: not placed yet) */

const Source* source_of(const std::string& kind)
{
    ensure_builtins();
    for (const Source& s : sources())
        if (s.kind == kind)
            return &s;
    return nullptr;
}
std::string kind_of_item(const std::string& item)
{
    size_t colon = item.find(':');
    return colon == std::string::npos ? std::string() : item.substr(0, colon);
}
std::string upper_first(std::string s)
{
    for (char& c : s)
        if (c == '_')
            c = ' ';
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
        s[0] = (char)(s[0] - 'a' + 'A');
    return s;
}

/* ---- thumbnails' scenes ---- */
void wall_facing(prefab::Actor& a, const rood_thumbs::Scene& s, float height)
{
    const float out[3] = { 0, 0, -1 };
    prefab::rotation_facing(out, a.rotation);
    a.position[0] = 0, a.position[1] = -height, a.position[2] = s.wall_z;
}
bool prefab_scene(const prefab::Prefab& p, const std::vector<std::pair<std::string, Value>>& overrides, rood_thumbs::Scene& s)
{
    prefab::Actor a;
    a.prefab = p.id;
    a.overrides = overrides;
    if (p.align == "normal")
        wall_facing(a, s, 96);
    prefab::evaluate(p, a, 0, false, s.lights);
    return !s.lights.empty();
}

/* ---- the built-in sources ---- */
Source light_source()
{
    Source s;
    s.kind = "light";
    s.serial = [] { return 1u; };
    s.list = [](std::vector<Entry>& out) {
        static const char* const types[3][3] = { { "point", "Point light", "Light in every direction from a point." },
            { "spot", "Spot light", "A cone of light (it aims away from the surface it is dropped on)." },
            { "rect", "Rect light", "An area light from a rectangle's front face (it faces away from the surface)." } };
        for (const auto& t : types) {
            Entry e;
            e.kind = "light", e.id = t[0], e.item = std::string("light:") + t[0];
            e.name = t[1], e.category = "Lights", e.description = t[2];
            e.tags = { "light", t[0] };
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) { return rood_lights::place_commit(e.item, w); };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) {
        prefab::EvalLight l;
        const rood_lights::Light d = rood_lights::Light();
        for (int k = 0; k < 3; k++)
            l.color[k] = d.color[k];
        l.intensity = 1.5f;
        if (e.id == "spot") {
            l.type = prefab::LIGHT_SPOT, l.position[1] = -220, l.direction[0] = 0, l.direction[1] = 1, l.direction[2] = 0;
            l.cone = 50, l.radius = 420;
        } else if (e.id == "rect") {
            l.type = prefab::LIGHT_RECT, l.position[1] = -110, l.position[2] = sc.wall_z - 1;
            l.direction[0] = 0, l.direction[1] = 0, l.direction[2] = -1;
            l.size[0] = 120, l.size[1] = 80, l.radius = 420;
        } else {
            l.position[1] = -64, l.radius = 320;
        }
        sc.lights.push_back(l);
        return true;
    };
    return s;
}
Source prefab_source()
{
    Source s;
    s.kind = "prefab";
    s.serial = [] { return (unsigned)prefab::library().serial; };
    s.list = [](std::vector<Entry>& out) {
        for (const prefab::Prefab& p : prefab::library().prefabs) {
            Entry e;
            e.kind = "prefab", e.id = p.id, e.item = "prefab:" + p.id;
            e.name = p.name, e.category = p.category, e.description = p.description, e.error = p.error;
            e.tags = { p.icon, p.align == "normal" ? "wall" : "upright" };
            e.mine = p.user;
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) { return rood_lights::place_commit(e.item, w); };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) {
        const prefab::Prefab* p = prefab::library().find(e.id);
        return p && prefab_scene(*p, {}, sc);
    };
    s.prefab = [](const Entry& e) { return prefab::library().find(e.id); };
    return s;
}
/* A fixture class (pc/assets/fixtures/catalog.json "classes") that is a light: its prefab and parameters. */
const Json* class_json(const std::string& cls)
{
    const Json* classes = fixtures::catalog().json.get("classes");
    return classes ? classes->get(cls.c_str()) : nullptr;
}
std::string class_prefab(const Json* c)
{
    const Json* p = c ? c->get("prefab") : nullptr;
    return p && p->type == Json::String ? p->string : std::string();
}
std::vector<std::pair<std::string, Value>> class_params(const Json* c, const prefab::Prefab& p)
{
    std::vector<std::pair<std::string, Value>> out;
    const Json* params = c ? c->get("params") : nullptr;
    for (size_t i = 0; params && i < params->members.size(); i++) {
        const auto& m = params->members[i];
        if (!p.param(m.first))
            continue;
        if (m.second.type == Json::Number)
            out.push_back({ m.first, Value::scalar((float)m.second.number) });
        else if (m.second.type == Json::Array && m.second.items.size() == 3)
            out.push_back({ m.first, Value::vec((float)m.second.items[0].number, (float)m.second.items[1].number,
                                         (float)m.second.items[2].number) });
        else if (m.second.type == Json::Bool)
            out.push_back({ m.first, Value::scalar(m.second.boolean ? 1.0f : 0.0f) });
    }
    return out;
}
Source fixture_source()
{
    Source s;
    s.kind = "fixture";
    s.serial = [] { return (unsigned)fixtures::catalog().serial * 7919u + (unsigned)prefab::library().serial; };
    s.list = [](std::vector<Entry>& out) {
        const Json* classes = fixtures::catalog().json.get("classes");
        for (size_t i = 0; classes && i < classes->members.size(); i++) {
            const auto& m = classes->members[i];
            std::string id = class_prefab(&m.second);
            if (id.empty() || id == "none" || !prefab::library().find(id))
                continue; /* not a light (a cobweb, a decal) */
            const Json* d = m.second.get("description");
            const Json* fam = m.second.get("family");
            Entry e;
            e.kind = "fixture", e.id = m.first, e.item = "fixture:" + m.first;
            e.name = upper_first(m.first);
            e.category = fam && fam->type == Json::String ? upper_first(fam->string) : "Fixtures";
            e.description = (d && d->type == Json::String ? d->string : m.first)
                + ". Dropped on one of the room's fixtures it lights it (as the Light fixture tool); elsewhere it "
                  "places its prefab, " + id + ".";
            e.tags = { id, "fixture" };
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) {
        const Json* c = class_json(e.id);
        const prefab::Prefab* p = prefab::library().find(class_prefab(c));
        if (!p)
            return -1;
        int index = rood_lights::light_fixture_at(w.surface, w.normal, p->id); /* a fixture there: lit */
        if (index < 0)
            index = rood_lights::place_commit("prefab:" + p->id, w); /* none: the prefab, placed */
        if (index < 0 || index >= (int)rood_lights::actors.size())
            return -1;
        for (const auto& kv : class_params(c, *p))
            rood_lights::actors[index].set_override(kv.first, kv.second);
        rood_lights::select_actor(index);
        return index;
    };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) {
        const Json* c = class_json(e.id);
        const prefab::Prefab* p = prefab::library().find(class_prefab(c));
        return p && prefab_scene(*p, class_params(c, *p), sc);
    };
    s.prefab = [](const Entry& e) { return prefab::library().find(class_prefab(class_json(e.id))); };
    return s;
}
Source fog_source()
{
    Source s;
    s.kind = "fog";
    s.serial = [] { return rood_atmo::fog_presets_serial(); };
    s.list = [](std::vector<Entry>& out) {
        Entry plain;
        plain.kind = "fog", plain.id = "", plain.item = "fog:";
        plain.name = "Fog volume", plain.category = "Fog volumes";
        plain.description = "A new fog volume with the default look (pick a preset in Details after).";
        plain.tags = { "volume", "mist" };
        out.push_back(plain);
        for (const rood_atmo::FogPreset& p : rood_atmo::fog_presets()) {
            Entry e;
            e.kind = "fog", e.id = p.id, e.item = "fog:" + p.id;
            e.name = p.name, e.category = p.user ? "My presets" : "Fog presets", e.description = p.why;
            e.tags = { "volume", "preset" };
            e.mine = p.user;
            out.push_back(e);
        }
    };
    s.place = [](const Entry& e, const Placement& w) { return rood_atmo::place_volume(e.id, w.surface, w.normal); };
    s.scene = [](const Entry& e, rood_thumbs::Scene& sc) { return rood_atmo::preset_look(e.id, sc.fog); };
    s.ghost = [](const Entry& e, const pc_overlay_view& v, const Placement& w, void* draw_list) {
        float pos[3], size[3];
        bool sphere;
        if (!rood_atmo::volume_drop(e.id, w.surface, w.normal, pos, size, &sphere))
            return;
        ImDrawList* draw = (ImDrawList*)draw_list;
        static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        float q[8][2];
        bool ok[8];
        for (int c = 0; c < 8; c++) {
            float p[3] = { pos[0] + (c & 1 ? 0.5f : -0.5f) * size[0], pos[1] + (c & 2 ? 0.5f : -0.5f) * size[1],
                pos[2] + (c & 4 ? 0.5f : -0.5f) * size[2] };
            ok[c] = rood_math::project(v.view, v.projection, v.rect, p, q[c]);
        }
        for (const auto& ed : edges)
            if (ok[ed[0]] && ok[ed[1]])
                draw->AddLine(ImVec2(q[ed[0]][0], q[ed[0]][1]), ImVec2(q[ed[1]][0], q[ed[1]][1]), IM_COL32(170, 200, 255, 220),
                    1.5f);
    };
    return s;
}
void ensure_builtins()
{
    static bool added;
    if (added)
        return;
    added = true;
    std::vector<Source> builtin = { light_source(), prefab_source(), fixture_source(), fog_source() };
    for (Source& s : builtin) /* before any registered earlier: a registered kind of the same name wins */
        if (std::none_of(sources().begin(), sources().end(), [&](const Source& x) { return x.kind == s.kind; }))
            sources().push_back(s);
}
} // namespace

void rood_library::add_source(Source s)
{
    for (Source& old : sources())
        if (old.kind == s.kind) {
            old = std::move(s);
            ++sources_serial;
            return;
        }
    sources().push_back(std::move(s));
    ++sources_serial;
}
std::vector<std::string> rood_library::source_kinds()
{
    ensure_builtins();
    std::vector<std::string> out;
    for (const Source& s : sources())
        out.push_back(s.kind);
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return kind_rank(a) < kind_rank(b); });
    return out;
}
const std::vector<Entry>& rood_library::entries()
{
    ensure_builtins();
    std::vector<unsigned> now;
    for (const Source& s : sources())
        now.push_back(s.serial ? s.serial() : 0);
    if (now != seen_serials || seen_sources != sources_serial) {
        all_entries.clear();
        for (const Source& s : sources())
            if (s.list) {
                size_t first = all_entries.size();
                s.list(all_entries);
                for (size_t i = first; i < all_entries.size(); i++)
                    all_entries[i].kind = s.kind; /* the source's kind, whatever it wrote */
            }
        sort_entries(all_entries);
        seen_serials = now, seen_sources = sources_serial;
    }
    return all_entries;
}
const Entry* rood_library::find(const std::string& item)
{
    for (const Entry& e : entries())
        if (e.item == item)
            return &e;
    return nullptr;
}
bool rood_library::is_item(const std::string& item)
{
    return item.rfind("light:", 0) != 0 && item.rfind("prefab:", 0) != 0 && find(item) != nullptr;
}
const prefab::Prefab* rood_library::item_prefab(const std::string& item)
{
    const Entry* e = find(item);
    const Source* s = e ? source_of(e->kind) : nullptr;
    return s && s->prefab ? s->prefab(*e) : nullptr;
}
std::string rood_library::label(const std::string& item)
{
    const Entry* e = find(item);
    return e ? e->name : item;
}
int rood_library::place(const std::string& item, const Placement& where)
{
    const Entry* found = find(item);
    if (!found)
        return -1;
    Entry e = *found; /* placing may reload a source (its entries rebuilt) */
    const Source* s = source_of(e.kind);
    return s && s->place ? s->place(e, where) : -1;
}
void rood_library::draw_ghost(const std::string& item, const pc_overlay_view& v, const Placement& where, void* draw_list)
{
    const Entry* e = find(item);
    const Source* s = e ? source_of(e->kind) : nullptr;
    if (s && s->ghost)
        s->ghost(*e, v, where, draw_list);
}

/* ---- settings ---- */

bool rood_library::parse_setting(const char* key, const char* rest)
{
    if (!strcmp(key, "editor_library_view")) {
        grid_view = strncmp(rest, "list", 4) != 0;
        return true;
    }
    if (!strcmp(key, "editor_library_docked")) {
        in_column = atoi(rest) != 0;
        return true;
    }
    if (!strcmp(key, "editor_library_window")) {
        sscanf(rest, "%f %f %f %f", &window_rect[0], &window_rect[1], &window_rect[2], &window_rect[3]);
        return true;
    }
    if (!strcmp(key, "editor_library_favourites")) {
        favourites = parse_favourites(rest);
        return true;
    }
    return false;
}
void rood_library::write_settings(FILE* f)
{
    fprintf(f, "editor_library_view %s\n", grid_view ? "grid" : "list");
    fprintf(f, "editor_library_docked %d\n", in_column ? 1 : 0);
    if (window_rect[2] > 0)
        fprintf(f, "editor_library_window %.0f %.0f %.0f %.0f\n", window_rect[0], window_rect[1], window_rect[2], window_rect[3]);
    std::string line = favourites_line(favourites);
    if (!line.empty())
        fprintf(f, "%s\n", line.c_str());
}

/* ---- RPC editor_library ---- */

namespace {
std::string q(const std::string& s) { return prefab::json_quote(s); }
std::string entry_json(const Entry& e)
{
    return "{\"item\":" + q(e.item) + ",\"kind\":" + q(e.kind) + ",\"id\":" + q(e.id) + ",\"name\":" + q(e.name)
        + ",\"category\":" + q(e.category) + ",\"mine\":" + (e.mine ? "true" : "false") + ",\"favourite\":"
        + (favourites.count(e.item) ? "true" : "false") + ",\"error\":" + q(e.error) + "}";
}
std::string list_json()
{
    const std::vector<Entry>& all = rood_library::entries();
    std::string s = "{\"sources\":[";
    std::vector<std::string> kinds = rood_library::source_kinds();
    for (size_t i = 0; i < kinds.size(); i++)
        s += (i ? "," : "") + q(kinds[i]);
    s += "],\"kinds\":[";
    kinds = rood_library::kinds_present(all);
    for (size_t i = 0; i < kinds.size(); i++)
        s += (i ? "," : "") + q(kinds[i]);
    s += "],\"categories\":[";
    std::vector<std::string> cats = rood_library::categories(all, filter.kind);
    for (size_t i = 0; i < cats.size(); i++)
        s += (i ? "," : "") + q(cats[i]);
    s += "],\"filter\":{\"kind\":" + q(filter.kind) + ",\"category\":" + q(filter.category) + ",\"search\":" + q(filter.search)
        + ",\"favourites\":" + (filter.favourites ? "true" : "false") + ",\"mine\":" + (filter.mine ? "true" : "false")
        + "},\"view\":" + q(grid_view ? "grid" : "list") + ",\"docked\":" + (in_column ? "true" : "false") + ",\"favourites\":[";
    bool first = true;
    for (const std::string& f : favourites)
        s += (first ? "" : ",") + q(f), first = false;
    s += "],\"count\":" + std::to_string(all.size()) + ",\"entries\":[";
    first = true;
    for (const Entry* e : rood_library::shown(all, filter, favourites))
        s += (first ? "" : ",") + entry_json(*e), first = false;
    return s + "]}";
}
bool json_bool(const Json* j, bool fallback) { return j ? (j->type == Json::Bool ? j->boolean : j->number != 0) : fallback; }
} // namespace

/* RPC editor_library (dev_rpc.c hands over the params object's text): action
 * list (default): the sources, the kinds present, the filter's categories,
 * the filter, view, docked, favourites and the entries it shows; filter
 * {kind, category, search, favourites, mine} (the keys given change); favourite
 * {item, on}; view {mode "grid" | "list", docked}. Each replies as list. */
extern "C" int pc_editor_library_rpc(const char* params_text, char* out, int size)
{
    Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error) || params.type != Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_scene::frame();
    const Json* a = params.get("action");
    std::string action = a && a->type == Json::String ? a->string : "list";
    if (action == "filter") {
        if (const Json* j = params.get("kind"))
            filter.kind = j->string;
        if (const Json* j = params.get("category"))
            filter.category = j->string;
        if (const Json* j = params.get("search"))
            filter.search = j->string;
        filter.favourites = json_bool(params.get("favourites"), filter.favourites);
        filter.mine = json_bool(params.get("mine"), filter.mine);
    } else if (action == "favourite") {
        const Json* item = params.get("item");
        if (!item || item->type != Json::String || !rood_library::find(item->string)) {
            snprintf(out, size, "favourite needs item (a library item)");
            return -1;
        }
        if (json_bool(params.get("on"), true))
            favourites.insert(item->string);
        else
            favourites.erase(item->string);
    } else if (action == "view") {
        if (const Json* m = params.get("mode"))
            grid_view = m->string != "list";
        in_column = json_bool(params.get("docked"), in_column);
    } else if (action != "list") {
        snprintf(out, size, "action: list, filter, favourite or view");
        return -1;
    }
    std::string text = list_json();
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
```

(`rood_atmo::place_volume` and `volume_drop` come in Task 16; until then declare them in `environment_internal.h` now and stub them in `atmosphere.cpp` returning `-1` / `false`, so this task builds:

```cpp
/* A fog volume of preset `preset` ("": the defaults) dropped on a surface: it
 * rests on the floor there (a wall's or ceiling's: the floor below), its
 * footprint inside the floor's area. volume_drop: where (pos, full size,
 * its shape); place_volume: added and selected, its index or -1. */
bool volume_drop(const std::string& preset, const float surface[3], const float normal[3], float pos[3], float size[3],
    bool* sphere);
int place_volume(const std::string& preset, const float surface[3], const float normal[3]);
```

with stub bodies `{ return false; }` and `{ return -1; }`.) `panel`, `docked`, `window` and `save_selection` come in Tasks 17 and 18: give them empty bodies now (`void rood_library::panel() {}`, `bool rood_library::docked() { return in_column; }`, `void rood_library::window() {}`, `std::string rood_library::save_selection(const std::string&, std::string* m) { *m = "not yet"; return ""; }`) so the header holds.

In `pc/tools/build_boot.py`, after `root / "pc/src/library_thumbs.cpp",` add `root / "pc/src/editor_library.cpp",`.

In `pc/src/dev_rpc.c`: add `|| !strcmp(method, "editor_library")` to the `editor_select … editor_handles` branch's condition, declare `extern int pc_editor_library_rpc(const char*, char*, int) __attribute__((weak));` with the others and pick it in the handler chain:

```c
        int (*handler)(const char*, char*, int) = !strcmp(method, "editor_select") ? pc_editor_select_rpc
            : !strcmp(method, "editor_schema")                                     ? pc_editor_schema_rpc
            : !strcmp(method, "editor_transform")                                  ? pc_editor_transform_rpc
            : !strcmp(method, "editor_library")                                    ? pc_editor_library_rpc
                                                                                   : pc_editor_handles_rpc;
```

and in `edits`, add `|| !strcmp(method, "editor_library")` (Save as prefab edits the room; the rest change nothing undo keeps). Document the method in the RPC list comment near line 295: `editor_library {action list | filter | favourite | view | thumbs | save}`.

In `pc/src/project_settings.cpp`, add `#include "editor_library.h" /* editor_library_view, _docked, _window, _favourites */`, after `if (rood_colour::parse_setting(key, rest))` add:

```cpp
    if (rood_library::parse_setting(key, rest)) /* editor state: the library's view, place and favourites */
        return 1;
```

and after `rood_colour::write_settings(f);` add `rood_library::write_settings(f);`.

- [ ] **Step 6: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py --only sources,filters
```

Expected: every line `ok:`.

- [ ] **Step 7: Commit**

```bash
git add pc/src/editor_library.h pc/src/editor_library.cpp pc/tests/editor_library_smoke.py pc/tests/editor_selection_smoke.py pc/tests/run.py pc/src/environment_internal.h pc/src/atmosphere.cpp pc/src/dev_rpc.c pc/src/project_settings.cpp pc/tools/build_boot.py
git commit -m "$(cat <<'EOF'
Library: sources per kind (lights, prefabs, fixture classes, fog presets; add_source for emitters), filters, favourites, RPC editor_library

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 16: Drops: library items land on the surface under the cursor

**Files:**
- Modify: `pc/src/lights.cpp` (`parse_item`, `item_label`, `place_compute`, `place_commit`, `update_drag`, `draw_place_drag`), `pc/src/atmosphere.cpp` (`floor_area`, `fit_to_floor`, `volume_drop`, `place_volume`), `pc/src/editor_library.cpp` (the fog source's drop point), `pc/src/light_actors.cpp` (`draw_icon`, `item_colour`), `pc/tests/editor_library_smoke.py` (a `drops` section)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_library_smoke.py`, add to the docstring's list:

```
- drops: through the drag and drop (editor_place, as a drag from a tile):
  a fog preset on the floor by Ashley rests its bottom on that floor with
  its footprint inside the room, one undo step "Place <name>", selected;
  a plain fog volume dropped on a wall goes to the floor below; a spot
  dropped on the wall aims out of it, a torch (align normal) faces out, a
  lantern (upright) turns to face out with no pitch or roll; a fixture
  class dropped where there is no fixture places its prefab with the
  class's parameters;
```

and add (to `SECTIONS`):

```python
def floor_and_wall(s):
    """A floor pixel by Ashley and a wall pixel (its normal across), both in the view; either may be None."""
    p = s.player
    px = s.select(action="project", point=[p["x"] + 60, p["y"], p["z"] + 140])["pixel"]
    floor = (px, s.select(action="pick", x=px[0], y=px[1])) if px else None
    view = s.select()["view"]
    for fy in (0.15, 0.25, 0.35, 0.45):
        for fx in (0.08, 0.2, 0.32, 0.68, 0.8, 0.92):
            xy = [view[0] + view[2] * fx, view[1] + view[3] * fy]
            hit = s.select(action="pick", x=xy[0], y=xy[1])
            if hit["normal"] and abs(hit["normal"][1]) < 0.3:
                return floor, (xy, hit)
    return floor, None


def drop(s, item, xy):
    """A drag of `item` from the library dropped at window pixel xy: (editor_place's reply, undo steps it made)."""
    g = s.game
    done = s.history()["done"]
    g.call("editor_place", item=item, phase="begin", x=xy[0], y=xy[1])
    r = g.call("editor_place", phase="drop")
    return r, s.history()["done"] - done


def new_volume(s):
    sel = s.select()["selection"]
    ident = sel[0][4:] if sel and sel[0].startswith("fog:") else None
    return next((v for v in s.game.call("environment")["volumes"] if v["id"] == ident), None)


def forward(rotation):
    """An actor's forward axis from yaw, pitch, roll (light_prefabs.cpp rotation_basis)."""
    yaw, pitch = math.radians(rotation[0]), math.radians(rotation[1])
    return [math.sin(yaw) * math.cos(pitch), -math.sin(pitch), math.cos(yaw) * math.cos(pitch)]


def flat(v):
    n = math.hypot(v[0], v[2]) or 1
    return [v[0] / n, 0, v[2] / n]


def check_drops(s):
    g = s.game
    floor, wall = floor_and_wall(s)
    if not s.check(floor and floor[1]["normal"] and floor[1]["normal"][1] < -0.7, "a floor pixel by Ashley"):
        return
    fxy, fhit = floor
    r, steps = drop(s, "fog:ankle_mist", fxy)
    v = new_volume(s)
    lo, hi = g.call("scene")["bounds"]
    s.check(v is not None and steps == 1 and s.history()["undo"] == "Place Ankle mist" and v["preset"] == "ankle_mist"
            and abs(v["pos"][1] + v["size"][1] / 2 - fhit["world"][1]) < 2
            and v["size"][0] <= 600.5 and v["size"][2] <= 600.5,
            f"a fog preset on the floor: its bottom on it, at most the preset's size, one undo step, selected ({v}, "
            f"{s.history()['undo']!r})")
    if v:
        s.check(v["pos"][0] - v["size"][0] / 2 >= lo[0] - 1 and v["pos"][0] + v["size"][0] / 2 <= hi[0] + 1
                and v["pos"][2] - v["size"][2] / 2 >= lo[2] - 1 and v["pos"][2] + v["size"][2] / 2 <= hi[2] + 1,
                f"its footprint stays inside the room ({v['pos']}, {v['size']}; room {lo}..{hi})")
    g.call("editor_undo", action="undo")
    s.check(new_volume(s) is None, "undo takes it away")
    if not s.check(wall is not None, "a wall pixel in the view"):
        return
    wxy, whit = wall
    n = whit["normal"]
    r, steps = drop(s, "fog:", wxy)
    v = new_volume(s)
    s.check(v is not None and v["pos"][1] + v["size"][1] / 2 >= whit["world"][1] - 1
            and abs(v["pos"][1] + v["size"][1] / 2 - s.player["y"]) < 24,
            f"a fog volume dropped on a wall goes to the floor below ({v and v['pos']}, wall hit {whit['world']})")
    g.call("editor_undo", action="undo")
    r, steps = drop(s, "light:spot", wxy)
    spot = g.call("lights")["lights"][-1]["id"]  # the lights RPC has no direction: the schema's (editor_schema get)
    aim = value(s, spot, "direction")
    s.check(steps == 1 and close(aim, n, 0.02), f"a spot on the wall aims out ({aim} vs {n})")
    g.call("editor_undo", action="undo")
    for prefab, upright in (("torch", False), ("lantern", True)):
        r, steps = drop(s, "prefab:" + prefab, wxy)
        a = g.call("actors")["actors"][-1]
        f = forward(a["rotation"])
        want = flat(n) if upright else n
        s.check(a["prefab"] == prefab and steps == 1 and close(f, want, 0.03)
                and (not upright or (abs(a["rotation"][1]) < 0.5 and abs(a["rotation"][2]) < 0.5)),
                f"a {prefab} on the wall faces out ({[round(x, 3) for x in f]} vs {want}; rotation {a['rotation']})")
        g.call("editor_undo", action="undo")
    r, steps = drop(s, "fixture:wall_torch", fxy)
    a = g.call("actors")["actors"][-1]
    s.check(a["prefab"] == "torch" and close(a["overrides"].get("radius", 0), 520) and steps == 1
            and s.history()["undo"] == "Place Wall torch",
            f"a fixture class with no fixture there: its prefab with the class's parameters ({a['overrides']})")
    g.call("editor_undo", action="undo")
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py --only drops
```

Expected: `FAIL: a fog preset on the floor: …` (`editor_place` with a `fog:` item does not begin: `parse_item` refuses it).

- [ ] **Step 3: Library items in placing**

In `pc/src/lights.cpp`, add `#include "editor_library.h"`. Replace `parse_item`:

```cpp
/* "light:point|spot|rect" (light_type 0-2) or "prefab:<id>" (*p). */
static bool parse_item(const std::string& item, int* light_type, const prefab::Prefab** p)
{
    *p = nullptr;
    *light_type = -1;
    if (item.rfind("light:", 0) == 0) {
        std::string t = item.substr(6);
        *light_type = t == "spot" ? 1 : t == "rect" ? 2 : 0;
        return t == "point" || t == "spot" || t == "rect";
    }
    if (item.rfind("prefab:", 0) == 0) {
        *p = prefab::library().find(item.substr(7));
        return *p != nullptr;
    }
    return false;
}
```

with:

```cpp
/* "light:point|spot|rect" (light_type 0-2), "prefab:<id>" (*p), or another
 * library item (editor_library.h; light_type -1): a fixture class gives its
 * prefab in *p (its placement and lit preview), a fog preset none. */
static bool parse_item(const std::string& item, int* light_type, const prefab::Prefab** p)
{
    *p = nullptr;
    *light_type = -1;
    if (item.rfind("light:", 0) == 0) {
        std::string t = item.substr(6);
        *light_type = t == "spot" ? 1 : t == "rect" ? 2 : 0;
        return t == "point" || t == "spot" || t == "rect";
    }
    if (item.rfind("prefab:", 0) == 0) {
        *p = prefab::library().find(item.substr(7));
        return *p != nullptr;
    }
    if (rood_library::is_item(item)) {
        *p = rood_library::item_prefab(item);
        return true;
    }
    return false;
}
```

In `item_label`, replace `return p ? p->name : light_type_name(type);` with:

```cpp
    if (type < 0)
        return rood_library::label(item);
    return p ? p->name : light_type_name(type);
```

In `place_compute`, replace the position loop and the rotation line:

```cpp
    for (int k = 0; k < 3; k++) {
        /* A light: off the surface by the snap offset (r.position); an
         * actor's pivot: on it, by the prefab's own offset. */
        float at = !p || snap != PC_SNAP_SURFACE ? r.position[k] : r.surface[k] + r.normal[k] * p->surface_offset;
        out.position[k] = grid ? roundf(at / pc_lights_snap_move) * pc_lights_snap_move : at;
    }
    if (p && p->align == "normal" && snap == PC_SNAP_SURFACE)
        prefab::rotation_facing(r.normal, out.rotation);
```

with:

```cpp
    for (int k = 0; k < 3; k++) {
        /* A light: off the surface by the snap offset (r.position); an
         * actor's pivot: on it, by the prefab's own offset; another library
         * item (a fog volume): the surface point itself. */
        float at = snap != PC_SNAP_SURFACE ? r.position[k]
            : p                            ? r.surface[k] + r.normal[k] * p->surface_offset
            : type < 0                     ? r.surface[k]
                                           : r.position[k];
        out.position[k] = grid ? roundf(at / pc_lights_snap_move) * pc_lights_snap_move : at;
    }
    if (p && p->align == "normal" && snap == PC_SNAP_SURFACE) {
        prefab::rotation_facing(r.normal, out.rotation);
    } else if (p && snap == PC_SNAP_SURFACE && fabsf(r.normal[1]) < 0.5f) {
        /* An upright prefab on a wall turns to face out of it (yaw only). */
        float out_of_wall[3] = { r.normal[0], 0, r.normal[2] };
        prefab::rotation_facing(out_of_wall, out.rotation);
    }
```

In `place_commit`, replace:

```cpp
    track_room();
    int index;
    if (!p) {
```

with:

```cpp
    track_room();
    int index;
    if (type < 0) { /* a library item: its source places and selects it (editor_library.cpp) */
        index = rood_library::place(item, where);
        if (index < 0) {
            snprintf(status, sizeof status, "Could not place %s there", item_label(item).c_str());
            return -1;
        }
    } else if (!p) {
```

In `update_drag`, after `parse_item(drag.item, &type, &p);` add:

```cpp
    if (!p && type < 0)
        return; /* a library item without a lit preview (a fog volume): draw_place_drag draws its outline */
```

In `draw_place_drag`, replace `if (drag.over && drag.where.ok && !drag_ghost.empty()) {` and the line `const Ghost& g = drag_ghost[0];` with:

```cpp
    bool outline = drag_ghost.empty() && rood_library::is_item(drag.item); /* drawn by its source (a fog volume's box) */
    if (drag.over && drag.where.ok && (!drag_ghost.empty() || outline)) {
        const Ghost& g = outline ? Ghost() : drag_ghost[0];
```

(`Ghost()` binds to the const reference for the block's lifetime) and replace `if (g.actor)` there with:

```cpp
        if (outline)
            rood_library::draw_ghost(drag.item, ov, drag.where, draw);
        else if (g.actor)
```

- [ ] **Step 4: A fog volume placed on the floor**

In `pc/src/atmosphere.cpp`, add `#include "editor_math.h"`. Replace `fit_to_floor` with a shared `floor_area` and a shorter `fit_to_floor`:

```cpp
/* The level floor around (x, z): the room's floor grid cells connected to
 * the one under it within 12 units of its height; their world x, z bounds
 * and that height. False without a grid or outside it. */
bool floor_area(float x, float z, float lo[2], float hi[2], float* height)
{
    const int n = PC_SCENE_FLOOR_GRID;
    static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    float corner[2], cell[2];
    if (!pc_scene_floor_grid(heights, corner, cell))
        return false;
    int i0 = (int)floorf((x - corner[0]) / cell[0]), j0 = (int)floorf((z - corner[1]) / cell[1]);
    if (i0 < 0 || j0 < 0 || i0 >= n || j0 >= n)
        return false;
    float h0 = heights[j0 * n + i0];
    std::vector<char> seen(n * n, 0);
    std::vector<int> stack = { j0 * n + i0 };
    seen[j0 * n + i0] = 1;
    int a[2] = { i0, j0 }, b[2] = { i0, j0 };
    while (!stack.empty()) {
        int k = stack.back();
        stack.pop_back();
        int i = k % n, j = k / n;
        a[0] = std::min(a[0], i), a[1] = std::min(a[1], j), b[0] = std::max(b[0], i), b[1] = std::max(b[1], j);
        const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
        for (int e = 0; e < 4; e++) {
            int u = i + di[e], w = j + dj[e];
            if (u < 0 || w < 0 || u >= n || w >= n || seen[w * n + u] || fabsf(heights[w * n + u] - h0) > 12)
                continue;
            seen[w * n + u] = 1;
            stack.push_back(w * n + u);
        }
    }
    lo[0] = corner[0] + a[0] * cell[0], hi[0] = corner[0] + (b[0] + 1) * cell[0];
    lo[1] = corner[1] + a[1] * cell[1], hi[1] = corner[1] + (b[1] + 1) * cell[1];
    *height = h0;
    return true;
}
/* Fit to floor area: the volume's footprint becomes the level floor around
 * its centre (floor_area), turned square to the room, dropped onto it. */
bool fit_to_floor(Volume& v)
{
    float lo[2], hi[2], h0;
    if (!floor_area(v.pos[0], v.pos[2], lo, hi, &h0))
        return false;
    v.yaw = 0;
    v.pos[0] = (lo[0] + hi[0]) * 0.5f, v.pos[2] = (lo[1] + hi[1]) * 0.5f;
    v.size[0] = hi[0] - lo[0], v.size[2] = hi[1] - lo[1];
    v.pos[1] = h0 - v.size[1] * 0.5f; /* on that floor (Drop to floor would rest it on the highest point under it) */
    return true;
}
```

Replace the Task 15 stubs of `volume_drop` and `place_volume` with:

```cpp
bool rood_atmo::volume_drop(const std::string& preset, const float surface[3], const float normal[3], float pos[3],
    float size[3], bool* sphere)
{
    Volume v;
    if (!preset.empty()) {
        const Preset* p = find_preset(preset);
        if (!p)
            return false;
        apply_preset(v, *p);
    }
    float at[3] = { surface[0], surface[1], surface[2] };
    if (!(normal[1] < -0.5f)) { /* a wall or a ceiling (y down: a floor's normal points to -y) */
        float h = sqrtf(normal[0] * normal[0] + normal[2] * normal[2]);
        if (h > 1e-3f) { /* out of a wall by half its footprint */
            float reach = 0.5f * fmaxf(v.size[0], v.size[2]);
            at[0] += normal[0] / h * reach, at[2] += normal[2] / h * reach;
        }
        float y;
        if (floor_under(at[0], at[2], at[1] - 8, &y) == 1) /* the floor below */
            at[1] = y;
    }
    float centre[2] = { at[0], at[2] }, foot[2] = { v.size[0], v.size[2] };
    float lo[2], hi[2], h0;
    if (floor_area(at[0], at[2], lo, hi, &h0) && fabsf(h0 - at[1]) <= 12) { /* that floor's level area */
        const float here[2] = { at[0], at[2] }, want[2] = { v.size[0], v.size[2] };
        rood_math::fit_footprint(here, want, lo, hi, centre, foot);
    }
    pos[0] = centre[0], pos[1] = at[1] - v.size[1] * 0.5f, pos[2] = centre[1];
    size[0] = foot[0], size[1] = v.size[1], size[2] = foot[1];
    *sphere = v.sphere;
    return true;
}
int rood_atmo::place_volume(const std::string& preset, const float surface[3], const float normal[3])
{
    float pos[3], size[3];
    bool sphere;
    if (!volume_drop(preset, surface, normal, pos, size, &sphere))
        return -1;
    Volume v;
    if (const Preset* p = preset.empty() ? nullptr : find_preset(preset)) {
        apply_preset(v, *p);
        v.name = p->name;
    }
    v.id = lighting::new_name();
    memcpy(v.pos, pos, sizeof v.pos);
    memcpy(v.size, size, sizeof v.size);
    volumes.push_back(v);
    int index = (int)volumes.size() - 1;
    rood_selection::set(rood_scene::volume_ref(index), rood_selection::FROM_CODE);
    rood_scene::publish();
    pc_lights_mark_dirty();
    return index;
}
```

In `pc/src/editor_library.cpp`, the fog source: drop points from a snap other than the surface have no surface (`Placement::surface` is only the surface snap's). Add before `fog_source`:

```cpp
/* Where a drop lands for a fog volume: the surface and its normal, or a snapped point (as a floor). */
void drop_point(const Placement& w, float at[3], float n[3])
{
    bool surface = w.snap == PC_SNAP_SURFACE;
    for (int k = 0; k < 3; k++)
        at[k] = surface ? w.surface[k] : w.position[k], n[k] = surface ? w.normal[k] : (k == 1 ? -1.0f : 0.0f);
}
```

and use it in the fog source's `place` and `ghost`:

```cpp
    s.place = [](const Entry& e, const Placement& w) {
        float at[3], n[3];
        drop_point(w, at, n);
        return rood_atmo::place_volume(e.id, at, n);
    };
```

(and in `ghost`: `float at[3], n[3]; drop_point(w, at, n);` then `volume_drop(e.id, at, n, …)`).

- [ ] **Step 5: Their icons**

In `pc/src/light_actors.cpp` `draw_icon`, after the `prefab:` lookup add:

```cpp
    if (item.rfind("fixture:", 0) == 0) { /* a fixture class: its prefab's icon */
        const Prefab* p = rood_library::item_prefab(item);
        icon = p && !p->icon.empty() ? p->icon : "light:point";
    } else if (item.rfind("fog:", 0) == 0) {
        icon = "fog";
    } else if (item.rfind("emitter:", 0) == 0) {
        icon = "emitter";
    }
```

and before the final `} else { /* point light …` add two icons:

```cpp
    } else if (icon == "fog") { /* a low cloud over a ground line */
        ImU32 mist = IM_COL32(190, 205, 230, (int)(alpha * 230));
        d->AddCircleFilled(ImVec2(x - h * 0.35f, y + h * 0.15f), h * 0.32f, mist, 16);
        d->AddCircleFilled(ImVec2(x + h * 0.05f, y - h * 0.05f), h * 0.42f, mist, 16);
        d->AddCircleFilled(ImVec2(x + h * 0.45f, y + h * 0.2f), h * 0.28f, mist, 16);
        d->AddRectFilled(ImVec2(x - h * 0.65f, y + h * 0.2f), ImVec2(x + h * 0.72f, y + h * 0.48f), mist);
        d->AddLine(ImVec2(x - h * 0.9f, y + h * 0.75f), ImVec2(x + h * 0.9f, y + h * 0.75f), line, 1.5f);
    } else if (icon == "emitter") { /* sparks from a point */
        for (int k = 0; k < 6; k++) {
            float a = -1.5707963f + (k - 2.5f) * 0.42f;
            d->AddLine(ImVec2(x, y + h * 0.7f), ImVec2(x + cosf(a) * h * 0.85f, y + h * 0.7f + sinf(a) * h * 1.3f), col, 1.5f);
        }
        d->AddCircleFilled(ImVec2(x, y + h * 0.7f), h * 0.16f, col);
```

and add `#include "editor_library.h"`. In `item_colour`, before the `prefab:` check, map a fixture to its prefab:

```cpp
    std::string key = item;
    if (item.rfind("fixture:", 0) == 0)
        if (const Prefab* p = rood_library::item_prefab(item))
            key = "prefab:" + p->id;
    if (item.rfind("fog:", 0) == 0) {
        out[0] = 0.75f, out[1] = 0.8f, out[2] = 0.9f;
        return;
    }
```

and use `key` instead of `item` for the rest of the function.

- [ ] **Step 6: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py --only drops
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 light_actors fog_volume atmosphere editor_selection
```

Expected: every line `ok:`; the Place Actors drag and drop (`light_actors_smoke`) and Fit to floor area (`fog_volume_smoke`) unchanged.

- [ ] **Step 7: Commit**

```bash
git add pc/src/lights.cpp pc/src/atmosphere.cpp pc/src/editor_library.cpp pc/src/light_actors.cpp pc/tests/editor_library_smoke.py
git commit -m "$(cat <<'EOF'
Library drops: fog presets rest on the floor (inside its level area; from a wall, the floor below), fixture classes light a fixture or place their prefab, upright prefabs face out of walls

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 17: The Library panel: grid and list, thumbnails, type icons, chips, docked or floating

**Files:**
- Modify: `pc/src/editor_library.cpp` (the thumbnail atlas, the panel, the window, RPC `thumbs` and the drawn rectangles), `pc/src/dev_overlay.cpp` (the Library tab replaces Place Actors, the floating window, Window menu, toolbar, help rows), `pc/src/dev_rpc.c` (`editor_ui {tab "library"}`), `pc/src/light_actors.cpp` (the old Place Actors body goes), `pc/tests/editor_library_smoke.py` (`thumbs`, `panel` sections)

- [ ] **Step 1: Write the failing sections**

In `pc/tests/editor_library_smoke.py`, add to the docstring's list:

```
- thumbs: each kind's thumbnail is rendered once into the cache directory
  (96 x 96 RGBA behind a 16-byte header), the same bytes when rendered
  again, then read from the cache; a fog preset's differs from a light's;
  changing the neutral scene's look gives another file;
- panel: the side column's Library tab draws a tile per shown entry with its
  thumbnail; a kind chip filters; List shows rows; a real mouse drag from a
  fog preset's tile onto the floor places it (one undo step, selected); Pop
  out floats the panel in its own window, Dock puts it back; both are user
  settings;
```

and add (to `SECTIONS`):

```python
HEADER = 16


def check_thumbs(s):
    wanted = ["light:point", "light:spot", "light:rect", "prefab:candle", "fog:ankle_mist", "fixture:wall_torch"]
    r = lib(s, action="thumbs", items=wanted)
    got = {t["item"]: t for t in r["thumbs"]}
    files = {i: pathlib.Path(got[i]["file"]) for i in wanted if i in got and got[i]["file"]}
    s.check(set(files) == set(wanted) and all(f.exists() and f.stat().st_size == HEADER + 96 * 96 * 4 for f in files.values())
            and all(not got[i]["cached"] for i in wanted) and all(str(f).startswith(str(s.work / "thumbs")) for f in files.values()),
            f"a thumbnail per kind, rendered into the cache directory ({sorted(files)})")
    if len(files) != len(wanted):
        return
    first = {i: f.read_bytes() for i, f in files.items()}
    again = {t["item"]: t for t in lib(s, action="thumbs", items=wanted)["thumbs"]}
    s.check(all(again[i]["cached"] and again[i]["hash"] == got[i]["hash"] for i in wanted), "a second time: from the cache")
    files["fog:ankle_mist"].unlink()
    lib(s, action="thumbs", items=["fog:ankle_mist"])
    s.check(files["fog:ankle_mist"].read_bytes() == first["fog:ankle_mist"], "rendered again: the same bytes")
    s.check(first["fog:ankle_mist"] != first["light:point"] and first["light:spot"] != first["light:point"],
            "a fog preset's thumbnail differs from a light's, a spot's from a point's")
    r = lib(s, action="thumbs", items=["light:point"], look={"floor": 0.3})
    s.check(r["thumbs"][0]["hash"] != got["light:point"]["hash"] and pathlib.Path(r["thumbs"][0]["file"]).exists(),
            "another look of the neutral scene: another file")
    lib(s, action="thumbs", items=[], look={"floor": 0.5})


def check_panel(s):
    g = s.game
    lib(s, action="filter", kind="", category="", search="", favourites=False, mine=False)
    lib(s, action="view", mode="grid", docked=True)
    g.call("editor_ui", tab="library")
    g.run(12)  # thumbnails: at most two rendered per frame
    r = lib(s)
    tiles = {e["item"]: e for e in r["entries"] if e["tile"]}
    s.check(r["drawn"] and "light:point" in tiles and tiles["light:point"]["thumb"],
            f"the Library tab: tiles with their thumbnails ({len(tiles)} drawn)")
    chip = r["rects"].get("chip:fog")
    if s.check(chip is not None, f"a kind chip for fog volumes ({sorted(r['rects'])})"):
        s.click(centre(chip))
        r = lib(s)
        s.check(r["filter"]["kind"] == "fog" and all(e["kind"] == "fog" for e in r["entries"]), "clicked: fog volumes only")
    tile = next((e["tile"] for e in lib(s)["entries"] if e["item"] == "fog:ankle_mist" and e["tile"]), None)
    floor, _ = floor_and_wall(s)
    if s.check(tile is not None and floor is not None, "the Ankle mist tile and a floor pixel"):
        done = s.history()["done"]
        start_xy, end_xy = centre(tile), floor[0]
        s.mouse(start_xy, down=True)
        for k in range(1, 7):  # past the drag threshold, then across into the view
            s.mouse([start_xy[0] + (end_xy[0] - start_xy[0]) * k / 6, start_xy[1] + (end_xy[1] - start_xy[1]) * k / 6], down=True)
        s.mouse(end_xy, down=False)
        v = new_volume(s)
        s.check(v is not None and v["preset"] == "ankle_mist" and s.history()["done"] == done + 1,
                f"a real drag from the tile onto the floor places it, selected, one undo step ({v and v['id']})")
        g.call("editor_undo", action="undo")
    lib(s, action="view", mode="list")
    g.run(3)
    rows = [e["tile"] for e in lib(s)["entries"] if e["tile"]]
    s.check(rows and all(t[3] < 60 for t in rows), f"List: rows instead of tiles ({rows[:2]})")
    lib(s, action="view", mode="grid", docked=False)
    g.run(3)
    r = lib(s)
    s.check(not r["docked"] and r["window"] is not None and g.call("editor_ui")["tab"] != "library",
            f"Pop out: its own window ({r['window']}), not a tab")
    g.call("settings", action="save")
    text = pathlib.Path(s.settings).read_text()
    s.check("editor_library_docked 0" in text and "editor_library_view grid" in text, "docked and the view are user settings")
    lib(s, action="view", docked=True)
    lib(s, action="filter", kind="")
```

- [ ] **Step 2: Run them and see them fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py --only thumbs,panel
```

Expected: an `RpcError` `action: list, filter, favourite or view` (no `thumbs`).

- [ ] **Step 3: The thumbnail atlas**

In `pc/src/editor_library.cpp`, add `#include "imgui_internal.h" // RegisterUserTexture, ImTextureDataQueueUpload` and, after the built-in sources' anonymous namespace, a new one:

```cpp
namespace {
/* Thumbnails: rendered (or read from the cache) on demand, at most two
 * renders a frame, packed into one ImTextureData that the GL and Vulkan
 * backends upload themselves (as prompt_glyphs.cpp). 10 x 10 slots of 100
 * pixels (96 and a 2-pixel margin); the least recently drawn slot goes when full. */
const int ATLAS = 1024, SLOT = 100, PER_ROW = ATLAS / SLOT, MAX_RENDERS = 2;
ImTextureData* atlas;
ImGuiContext* atlas_owner;
struct Slot {
    std::string item, hash;
    int frame = -1;
};
Slot slots[PER_ROW * PER_ROW];
std::map<std::string, int> slot_of;
int render_frame = -1, renders;
unsigned look_serial = 1; /* bumps when RPC changes the look (every hash changes) */
struct Hashed {
    unsigned serial = 0, look = 0;
    std::string hash;
    bool scene = false;
};
std::map<std::string, Hashed> hashes;

unsigned source_serial(const Source* s) { return s && s->serial ? s->serial() : 0; }
/* The item's scene and its hash (cached while its source and the look stay the same). */
bool scene_of(const Entry& e, rood_thumbs::Scene* scene, std::string* hash)
{
    const Source* src = source_of(e.kind);
    if (!src || !src->scene)
        return false;
    Hashed& h = hashes[e.item];
    unsigned serial = source_serial(src);
    if (!scene && h.serial == serial && h.look == look_serial && !h.hash.empty()) {
        *hash = h.hash;
        return h.scene;
    }
    rood_thumbs::Scene s;
    h.scene = src->scene(e, s);
    h.serial = serial, h.look = look_serial;
    h.hash = h.scene ? rood_thumbs::hash(s, rood_thumbs::SIZE) : std::string();
    *hash = h.hash;
    if (scene)
        *scene = s;
    return h.scene;
}
std::string thumb_path(const Entry& e, const std::string& hash)
{
    return rood_thumbs::cache_dir() + "/" + rood_thumbs::file_name(e.kind, e.id, hash);
}
/* The thumbnail's pixels: from the cache, else rendered (and cached) when `may_render`. */
bool pixels_of(const Entry& e, bool may_render, std::vector<uint8_t>& px, std::string* hash, bool* cached)
{
    rood_thumbs::Scene s;
    if (!scene_of(e, nullptr, hash))
        return false;
    std::string path = thumb_path(e, *hash);
    if (rood_thumbs::load(path, rood_thumbs::SIZE, px)) {
        *cached = true;
        return true;
    }
    if (!may_render)
        return false;
    scene_of(e, &s, hash);
    rood_thumbs::render(s, rood_thumbs::SIZE, px);
    std::string error;
    if (!rood_thumbs::save(path, rood_thumbs::SIZE, px, &error))
        fprintf(stderr, "[library] %s\n", error.c_str());
    *cached = false;
    return true;
}
bool ensure_atlas()
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context)
        return false;
    if (!atlas || atlas_owner != context) {
        atlas = IM_NEW(ImTextureData)();
        atlas->Create(ImTextureFormat_RGBA32, ATLAS, ATLAS);
        memset(atlas->Pixels, 0, (size_t)atlas->GetSizeInBytes());
        ImGui::RegisterUserTexture(atlas);
        atlas_owner = context;
        slot_of.clear();
        for (Slot& s : slots)
            s = Slot();
    }
    return true;
}
/* Where the item's thumbnail is in the atlas this frame (uv0, uv1); false while it is not there yet. */
bool thumb_uv(const Entry& e, ImVec2* uv0, ImVec2* uv1)
{
    if (!ensure_atlas())
        return false;
    int now = ImGui::GetFrameCount();
    if (render_frame != now)
        render_frame = now, renders = 0;
    std::string hash;
    if (!scene_of(e, nullptr, &hash))
        return false;
    auto it = slot_of.find(e.item);
    int slot = it == slot_of.end() ? -1 : it->second;
    if (slot < 0 || slots[slot].hash != hash) {
        std::vector<uint8_t> px;
        bool cached = false;
        if (!pixels_of(e, renders < MAX_RENDERS, px, &hash, &cached))
            return false;
        if (!cached)
            renders++;
        if (slot < 0) { /* a free slot, else the least recently drawn */
            int best = 0;
            for (int k = 0; k < PER_ROW * PER_ROW; k++) {
                if (slots[k].item.empty()) {
                    best = k;
                    break;
                }
                if (slots[k].frame < slots[best].frame)
                    best = k;
            }
            slot = best;
            if (!slots[slot].item.empty())
                slot_of.erase(slots[slot].item);
        }
        int x = (slot % PER_ROW) * SLOT + 2, y = (slot / PER_ROW) * SLOT + 2, n = rood_thumbs::SIZE;
        for (int row = 0; row < n; row++)
            memcpy(atlas->GetPixelsAt(x, y + row), &px[(size_t)row * n * 4], (size_t)n * 4);
        if (atlas->Status != ImTextureStatus_WantCreate)
            ImTextureDataQueueUpload(atlas, x, y, n, n);
        slots[slot].item = e.item, slots[slot].hash = hash;
        slot_of[e.item] = slot;
    }
    slots[slot].frame = now;
    int x = (slot % PER_ROW) * SLOT + 2, y = (slot / PER_ROW) * SLOT + 2;
    *uv0 = ImVec2((float)x / ATLAS, (float)y / ATLAS);
    *uv1 = ImVec2((float)(x + rood_thumbs::SIZE) / ATLAS, (float)(y + rood_thumbs::SIZE) / ATLAS);
    return true;
}
} // namespace
```

- [ ] **Step 4: The panel and the floating window**

In the same file, replace the Task 15 empty `panel`, `docked` and `window` with:

```cpp
namespace {
/* As last drawn (RPC): each shown entry's tile or row, the chips and buttons, the floating window. */
int drawn_frame = -100;
std::map<std::string, ImVec4> tile_rects, ui_rects;
std::set<std::string> thumbs_shown;
float window_drawn[4];
int window_frame = -100;

void keep(std::map<std::string, ImVec4>& into, const std::string& name)
{
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    into[name] = ImVec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
}
/* A chip (wrapping to the next line when the row is full); true when clicked. */
bool chip(const std::string& text, bool on, const std::string& name, bool* first)
{
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float w = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    if (!*first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < right)
        ImGui::SameLine();
    *first = false;
    if (on)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    bool clicked = ImGui::SmallButton((text + "##" + name).c_str());
    if (on)
        ImGui::PopStyleColor();
    keep(ui_rects, name);
    return clicked;
}
/* The item one kind's type icon draws (the tile's badge). */
const char* badge_item(const std::string& kind)
{
    return kind == "light" ? "light:point" : kind == "prefab" ? "candle" : kind == "fixture" ? "torch" : kind == "fog" ? "fog"
        : kind == "emitter" ? "emitter" : "light:point";
}
void star(ImDrawList* d, ImVec2 c, float r, bool on)
{
    ImVec2 p[10];
    for (int k = 0; k < 10; k++) {
        float a = -1.5707963f + k * 3.14159265f / 5, rr = k & 1 ? r * 0.45f : r;
        p[k] = ImVec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    if (on)
        for (int k = 0; k < 10; k += 2)
            d->AddTriangleFilled(c, p[k], p[(k + 1) % 10], IM_COL32(255, 210, 60, 255)),
                d->AddTriangleFilled(c, p[(k + 9) % 10], p[k], IM_COL32(255, 210, 60, 255));
    d->AddPolyline(p, 10, on ? IM_COL32(255, 210, 60, 255) : IM_COL32(200, 200, 200, 200), ImDrawFlags_Closed, 1.2f);
}
/* The tile's or row's clicks: current item (Ctrl+click in the view adds it), double-click at the view's
 * centre, a drag into the view, the favourite star; its tooltip. */
void item_actions(const Entry& e, bool pressed, const ImVec4& star_rect)
{
    ImGuiIO& io = ImGui::GetIO();
    float em = ImGui::GetFontSize();
    bool on_star = io.MousePos.x >= star_rect.x && io.MousePos.y >= star_rect.y && io.MousePos.x < star_rect.x + star_rect.z
        && io.MousePos.y < star_rect.y + star_rect.w;
    if (pressed && on_star) {
        if (!favourites.erase(e.item))
            favourites.insert(e.item);
        return;
    }
    if (pressed) {
        rood_lights::place_item = e.item;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            rood_lights::place_at_centre(e.item);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, em * 0.4f) && !rood_lights::place_drag_active())
        rood_lights::place_drag_begin(e.item, false, io.MousePos.x, io.MousePos.y);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !rood_lights::place_drag_active())
        ImGui::SetTooltip("%s  (%s, %s)\n%s%s%s\n\nDrag into the view to place it on the surface there; double-click: at the "
                          "view's centre;\nCtrl+click in the view adds it. The star: a favourite.",
            e.name.c_str(), rood_library::kind_label(e.kind).c_str(), e.category.c_str(), e.description.c_str(),
            e.error.empty() ? "" : "\nProblem: ", e.error.c_str());
}
void tile(const Entry& e, float side)
{
    ImGui::PushID(e.item.c_str());
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 lo = ImGui::GetCursorScreenPos(), hi(lo.x + side, lo.y + side);
    float line = ImGui::GetTextLineHeight(), em = ImGui::GetFontSize();
    bool pressed = ImGui::Selectable("##tile", rood_lights::place_item == e.item, ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(side, side + line + 4));
    keep(tile_rects, e.item);
    ImVec4 star_rect(hi.x - em * 1.1f, lo.y, em * 1.1f, em * 1.1f);
    item_actions(e, pressed, star_rect);
    ImVec2 uv0, uv1;
    if (thumb_uv(e, &uv0, &uv1)) {
        d->AddImage(atlas->GetTexRef(), lo, hi, uv0, uv1);
        thumbs_shown.insert(e.item);
    } else {
        float c[3] = { 1, 0.85f, 0.6f };
        d->AddRectFilled(lo, hi, IM_COL32(28, 28, 34, 255), 4);
        rood_lights::draw_icon(d, (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, side * 0.5f, e.item, c, 1);
    }
    if (!e.error.empty())
        d->AddRect(lo, hi, IM_COL32(255, 90, 70, 255), 2, 0, 2);
    /* The type icon, bottom left: what kind of thing it is at a glance. */
    float b = side * 0.24f;
    ImVec2 bc(lo.x + b * 0.65f, hi.y - b * 0.65f);
    const float white[3] = { 1, 1, 1 };
    d->AddCircleFilled(bc, b * 0.58f, IM_COL32(12, 12, 16, 225), 16);
    rood_lights::draw_icon(d, bc.x, bc.y, b * 0.8f, badge_item(e.kind), white, 1);
    bool fav = favourites.count(e.item) != 0;
    if (fav || ImGui::IsItemHovered())
        star(d, ImVec2(star_rect.x + star_rect.z * 0.5f, star_rect.y + star_rect.w * 0.5f), em * 0.42f, fav);
    std::string name = e.name;
    while (name.size() > 1 && ImGui::CalcTextSize(name.c_str()).x > side)
        name.pop_back();
    d->AddText(ImVec2(lo.x + 2, hi.y + 2), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    ImGui::PopID();
}
void row(const Entry& e)
{
    ImGui::PushID(e.item.c_str());
    float em = ImGui::GetFontSize(), h = em * 1.8f;
    ImVec2 at = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::Selectable("##row", rood_lights::place_item == e.item, ImGuiSelectableFlags_AllowDoubleClick,
        ImVec2(0, h));
    keep(tile_rects, e.item);
    float right = ImGui::GetItemRectMax().x;
    ImVec4 star_rect(right - em * 1.2f, at.y + (h - em) * 0.5f, em * 1.1f, em * 1.1f);
    item_actions(e, pressed, star_rect);
    ImDrawList* d = ImGui::GetWindowDrawList();
    float c[3] = { 1, 0.85f, 0.6f };
    rood_lights::draw_icon(d, at.x + h * 0.5f, at.y + h * 0.5f, h * 0.7f, e.item, c, 1);
    d->AddText(ImVec2(at.x + h + em * 0.3f, at.y + h * 0.5f - em * 0.55f), ImGui::GetColorU32(ImGuiCol_Text), e.name.c_str());
    std::string kind = rood_library::kind_label(e.kind);
    float kw = ImGui::CalcTextSize(kind.c_str()).x;
    d->AddText(ImVec2(right - em * 1.6f - kw, at.y + h * 0.5f - em * 0.55f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
        kind.c_str());
    star(d, ImVec2(star_rect.x + star_rect.z * 0.5f, star_rect.y + star_rect.w * 0.5f), em * 0.42f, favourites.count(e.item) != 0);
    ImGui::PopID();
}
} // namespace

void rood_library::panel()
{
    float em = ImGui::GetFontSize();
    drawn_frame = ImGui::GetFrameCount();
    tile_rects.clear(), ui_rects.clear(), thumbs_shown.clear();
    const std::vector<Entry>& all = entries();
    static char search[64];
    static std::string synced;
    if (synced != filter.search) /* the RPC changed it */
        snprintf(search, sizeof search, "%s", filter.search.c_str()), synced = filter.search;
    ImGui::SetNextItemWidth(-em * 11);
    if (ImGui::InputTextWithHint("##libsearch", "Search the library", search, sizeof search))
        filter.search = synced = search;
    ImGui::SameLine();
    if (ImGui::SmallButton(grid_view ? "List" : "Grid"))
        grid_view = !grid_view;
    keep(ui_rects, "view");
    ImGui::SameLine();
    if (ImGui::SmallButton(in_column ? "Pop out" : "Dock"))
        in_column = !in_column;
    keep(ui_rects, "dock");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(in_column ? "Float the library in its own window" : "Back into the side column (beside the Outliner)");
    /* Kinds, then the kind's categories, Favourites and Mine. */
    bool first = true;
    if (chip("All", filter.kind.empty(), "chip:all", &first))
        filter.kind.clear(), filter.category.clear();
    for (const std::string& k : kinds_present(all))
        if (chip(kind_label(k), filter.kind == k, "chip:" + k, &first))
            filter.kind = filter.kind == k ? std::string() : k, filter.category.clear();
    first = true;
    for (const std::string& c : categories(all, filter.kind))
        if (chip(c, filter.category == c, "category:" + c, &first))
            filter.category = filter.category == c ? std::string() : c;
    if (chip("Favourites", filter.favourites, "favourites", &first))
        filter.favourites = !filter.favourites;
    if (chip("Mine", filter.mine, "mine", &first))
        filter.mine = !filter.mine;
    ImGui::Separator();
    std::vector<const Entry*> list = shown(all, filter, favourites);
    ImGui::BeginChild("##library items", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.2f));
    if (grid_view) {
        float side = em * 5, gap = ImGui::GetStyle().ItemSpacing.x;
        int columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (side + gap)));
        for (size_t i = 0; i < list.size(); i++) {
            if (i % columns)
                ImGui::SameLine();
            tile(*list[i], side);
        }
    } else {
        for (const Entry* e : list)
            row(*e);
    }
    if (list.empty())
        ImGui::TextDisabled(all.empty() ? "Nothing in the library" : "Nothing matches");
    ImGui::EndChild();
    ImGui::TextDisabled("%zu of %zu. Ctrl+click in the view adds: %s", list.size(), all.size(),
        rood_lights::item_label(rood_lights::place_item).c_str());
}
bool rood_library::docked() { return in_column; }
void rood_library::window()
{
    if (in_column)
        return;
    ImGuiIO& io = ImGui::GetIO();
    float em = ImGui::GetFontSize();
    if (window_rect[2] <= 0)
        window_rect[0] = io.DisplaySize.x * 0.3f, window_rect[1] = io.DisplaySize.y * 0.15f, window_rect[2] = em * 28,
        window_rect[3] = em * 30;
    ImGui::SetNextWindowPos(ImVec2(window_rect[0], window_rect[1]), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(window_rect[2], window_rect[3]), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin("Library##floating", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        panel();
        ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        window_rect[0] = window_drawn[0] = p.x, window_rect[1] = window_drawn[1] = p.y;
        window_rect[2] = window_drawn[2] = s.x, window_rect[3] = window_drawn[3] = s.y;
        window_frame = ImGui::GetFrameCount();
    }
    ImGui::End();
    if (!open) /* its close box docks it back */
        in_column = true;
}
```

- [ ] **Step 5: RPC thumbs and the drawn rectangles**

In `entry_json`, append the tile and whether its thumbnail is shown (both only when drawn this frame or the last):

```cpp
std::string entry_json(const Entry& e)
{
    bool now = ImGui::GetCurrentContext() && drawn_frame >= ImGui::GetFrameCount() - 1;
    auto it = tile_rects.find(e.item);
    char rect[96] = "null";
    if (now && it != tile_rects.end())
        snprintf(rect, sizeof rect, "[%.1f,%.1f,%.1f,%.1f]", it->second.x, it->second.y, it->second.z, it->second.w);
    return "{\"item\":" + q(e.item) + ",\"kind\":" + q(e.kind) + ",\"id\":" + q(e.id) + ",\"name\":" + q(e.name)
        + ",\"category\":" + q(e.category) + ",\"mine\":" + (e.mine ? "true" : "false") + ",\"favourite\":"
        + (favourites.count(e.item) ? "true" : "false") + ",\"error\":" + q(e.error) + ",\"tile\":" + rect + ",\"thumb\":"
        + (now && thumbs_shown.count(e.item) ? "true" : "false") + "}";
}
```

(move `entry_json` / `list_json` below the panel's anonymous namespace so they see `drawn_frame`, `tile_rects`, `thumbs_shown`). At the end of `list_json`, before the closing brace, add the drawn state:

```cpp
    bool now = ImGui::GetCurrentContext() && drawn_frame >= ImGui::GetFrameCount() - 1;
    s += "],\"drawn\":" + std::string(now ? "true" : "false") + ",\"rects\":{";
    bool first_rect = true;
    for (const auto& kv : ui_rects) {
        if (!now)
            break;
        char r[96];
        snprintf(r, sizeof r, "[%.1f,%.1f,%.1f,%.1f]", kv.second.x, kv.second.y, kv.second.z, kv.second.w);
        s += (first_rect ? "" : ",") + q(kv.first) + ":" + r;
        first_rect = false;
    }
    char w[96] = "null";
    if (ImGui::GetCurrentContext() && window_frame >= ImGui::GetFrameCount() - 1 && !in_column)
        snprintf(w, sizeof w, "[%.1f,%.1f,%.1f,%.1f]", window_drawn[0], window_drawn[1], window_drawn[2], window_drawn[3]);
    return s + "},\"window\":" + w + "}";
```

(replacing its last line `return s + "]}";` by closing the entries array inside this block).

In `pc_editor_library_rpc`, before `} else if (action != "list") {` add:

```cpp
    } else if (action == "thumbs") { /* render (or read) thumbnails now; look {field: value} changes the neutral scene */
        if (const Json* look = params.get("look")) {
            rood_thumbs::Look& lk = rood_thumbs::look();
            const std::pair<const char*, float*> fields[] = { { "floor", &lk.floor }, { "wall", &lk.wall }, { "grid", &lk.grid },
                { "ambient", &lk.ambient }, { "fog_ambient", &lk.fog_ambient }, { "exposure", &lk.exposure }, { "fov", &lk.fov } };
            for (const auto& f : fields)
                if (const Json* v = look->get(f.first))
                    *f.second = (float)v->number;
            ++look_serial;
        }
        std::vector<std::string> wanted;
        if (const Json* list = params.get("items"))
            for (const Json& i : list->items)
                wanted.push_back(i.string);
        else
            for (const Entry* e : rood_library::shown(rood_library::entries(), filter, favourites))
                wanted.push_back(e->item);
        std::string text = "{\"dir\":" + q(rood_thumbs::cache_dir()) + ",\"thumbs\":[";
        for (size_t i = 0; i < wanted.size(); i++) {
            const Entry* e = rood_library::find(wanted[i]);
            std::vector<uint8_t> px;
            std::string hash;
            bool cached = false, ok = e && pixels_of(*e, true, px, &hash, &cached);
            text += std::string(i ? "," : "") + "{\"item\":" + q(wanted[i]) + ",\"file\":"
                + (ok ? q(thumb_path(*e, hash)) : std::string("null")) + ",\"hash\":" + q(hash) + ",\"cached\":"
                + (cached ? "true" : "false") + "}";
        }
        text += "]}";
        snprintf(out, size, "%s", text.c_str());
        return (int)text.size() < size ? 0 : -1;
```

and extend the failure text to `action: list, filter, favourite, view, thumbs or save`, and the RPC comment with `thumbs {items, look}`.

- [ ] **Step 6: The Library in the side column**

In `pc/src/dev_overlay.cpp`, add `#include "editor_library.h"`; in `outliner`, replace the Place Actors tab:

```cpp
        if (ImGui::BeginTabItem("Place Actors", nullptr, place_flags)) {
            place_tab_shown = true;
            pc_lights_place_panel();
            ImGui::EndTabItem();
        }
```

with:

```cpp
        /* The Library (editor_library.cpp): this tab, or its own window when popped out. */
        if (rood_library::docked() && ImGui::BeginTabItem("Library", nullptr, place_flags)) {
            place_tab_shown = true;
            rood_library::panel();
            ImGui::EndTabItem();
        }
```

and the comment above the tab bar `/* Two tabs as in UE's default layout: the Outliner and Place Actors. */` with `/* The Outliner and the Library (UE's Place Actors, with every kind of thing to place). */`. In `pc_overlay_frame`, after `details(…);` add `rood_library::window(); /* the Library when popped out */`. In the Window menu replace `ImGui::MenuItem("Place Actors", "Shift+1", place_tab_shown)` with `ImGui::MenuItem("Library", "Shift+1", place_tab_shown || !rood_library::docked())`. In the toolbar's "+ Place" tooltip and popup replace "Place Actors" with "the Library" (`"Open the Library (drag into the view)"`). In the help rows, replace the three `"Place"` rows naming Place Actors with:

```cpp
        { "Place", "Drag from the Library", "Place a light, light actor, fixture or fog volume on the surface where it is dropped (snapping as set; a preview follows)" },
        { "Place", "Esc (dragging)", "Cancel the drag (dropping off the view cancels too)" },
        { "Place", "Double-click in the Library", "Place at the surface under the view's centre" },
        { "Place", "Shift+1", "Show the Library (List / Grid, Pop out / Dock on its toolbar; the star: a favourite)" },
```

Delete the now unused `extern "C" void pc_lights_place_panel(void);` declaration. In `pc/src/light_actors.cpp` delete `rood_lights::place_panel` and `pc_lights_place_panel` (the library draws its entries now; `pc_lights_place_menu` and `entries()` stay for the toolbar's quick add), and the declaration `void place_panel();` in `lights_internal.h`.

In `pc/src/dev_rpc.c` `editor_ui`, accept `"library"`: `tab = !strcmp(tab_name, "place") || !strcmp(tab_name, "library") ? 1 : 0;` and reply `shown ? "library" : "outliner"`; update the comment `editor_ui {visible, help, tab ("outliner" | "library"; "place" too), details}`.

- [ ] **Step 7: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 light_actors editor_selection editor_undo
```

Expected: every section of the library smoke passes; the Place Actors RPC tests (`light_actors_smoke`) are unchanged.

- [ ] **Step 8: Commit**

```bash
git add pc/src/editor_library.cpp pc/src/dev_overlay.cpp pc/src/dev_rpc.c pc/src/light_actors.cpp pc/src/lights_internal.h pc/tests/editor_library_smoke.py
git commit -m "$(cat <<'EOF'
Library panel: grid (thumbnails with a type icon, favourite star) or list, kind and category chips, Favourites, Mine, search; docked or floating; replaces Place Actors

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 18: Save as prefab from the selection; the user's fog presets; Mine

**Files:**
- Modify: `pc/src/light_prefabs.h`, `pc/src/light_prefabs.cpp` (`json_files`, `directory_stamp` public), `pc/src/environment_internal.h`, `pc/src/atmosphere.cpp` (user presets: load, poll, `save_user_preset`; `preset_json` shared with `save_preset`), `pc/src/editor_library.cpp` (`save_selection`, the toolbar's Save as prefab, RPC `save`), `pc/tests/editor_library_smoke.py` (a `save` section), `pc/assets/prefabs/README.md`, `lighting/README.md`

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_library_smoke.py`, add to the docstring's list:

```
- save: Save as prefab from a light makes a light prefab of yours (its file
  in the user prefab directory; the light becomes an actor of it), listed
  under Mine; dropped again it lights the same; from a light actor, a
  prefab with its values; from a fog volume, a fog preset of yours
  (<user prefabs>/fog/<id>.json) that the volume then uses, dropped again
  with the same look; a mixed selection is refused;
```

and add (to `SECTIONS`, last):

```python
def check_save(s):
    g, p = s.game, s.player
    floor, _ = floor_and_wall(s)
    if not s.check(floor is not None, "a floor pixel"):
        return
    # A light: a light prefab of the user's.
    light = g.call("lights", action="add", position=[p["x"] - 100, p["y"] - 120, p["z"] + 60])["lights"][-1]["id"]
    schema(s, action="set", id=light, values={"intensity": 2.5, "radius": 480})
    s.select(action="set", ids=[light])
    done = s.history()["done"]
    r = lib(s, action="save", name="Test lamp")
    path = s.work / "prefabs" / "test_lamp.json"
    s.check(r["item"] == "prefab:test_lamp" and path.exists() and s.history()["done"] == done + 1,
            f"a light saved as prefab: {r['item']}, its file, one undo step ({r.get('message')})")
    mine = lib(s, action="filter", mine=True)
    e = next((x for x in mine["entries"] if x["item"] == "prefab:test_lamp"), None)
    s.check(e is not None and e["mine"] and e["category"] == "My prefabs", f"listed under Mine ({items(mine)})")
    lib(s, action="filter", mine=False)
    r, steps = drop(s, "prefab:test_lamp", floor[0])
    a = g.call("actors")["actors"][-1]
    s.check(a["prefab"] == "test_lamp" and close(a["lights"][0]["intensity"], 2.5, 0.05)
            and close(a["lights"][0]["radius"], 480, 0.01),
            f"dropped again it lights the same ({a['lights'][0]['intensity']}, {a['lights'][0]['radius']})")
    g.call("editor_undo", action="undo")
    # A light actor: a prefab with its values.
    schema(s, action="set", id=s.actor, values={"intensity": 3})
    before = next(x for x in g.call("actors")["actors"] if x["id"] == s.actor)["lights"][0]["intensity"]
    s.select(action="set", ids=[s.actor])
    r = lib(s, action="save", name="Bright candle")
    after = next(x for x in g.call("actors")["actors"] if x["id"] == s.actor)
    s.check(r["item"] == "prefab:bright_candle" and after["prefab"] == "bright_candle" and not after["overrides"]
            and close(after["lights"][0]["intensity"], before, 0.05),
            f"a light actor saved as prefab: it uses it, its values baked in ({after['prefab']}, {after['overrides']})")
    # A fog volume: a fog preset of the user's.
    schema(s, action="set", id=s.volume, values={"density": 0.02, "color": [0.5, 0.6, 1]})
    s.select(action="set", ids=[s.volume])
    r = lib(s, action="save", name="Test mist")
    fog_file = s.work / "prefabs" / "fog" / "test_mist.json"
    v = next(x for x in g.call("environment")["volumes"] if "fog:" + x["id"] == s.volume)
    s.check(r["item"] == "fog:test_mist" and fog_file.exists() and v["preset"] == "test_mist",
            f"a fog volume saved: a preset of yours that it uses ({r['item']}, {v['preset']})")
    e = next((x for x in lib(s, action="filter", mine=True)["entries"] if x["item"] == "fog:test_mist"), None)
    s.check(e is not None and e["kind"] == "fog" and e["category"] == "My presets", "listed under Mine with the fog presets")
    lib(s, action="filter", mine=False)
    r, steps = drop(s, "fog:test_mist", floor[0])
    nv = new_volume(s)
    got = schema(s, action="get", id="fog:" + nv["id"])["fields"] if nv else {}
    s.check(nv is not None and nv["preset"] == "test_mist" and close(got["density"]["value"], 0.02)
            and close(got["color"]["value"], [0.5, 0.6, 1]), "dropped again: the same look, from its preset")
    g.call("editor_undo", action="undo")
    # Anything else is refused.
    s.select(action="set", ids=[s.actor, s.volume])
    try:
        lib(s, action="save", name="Mixed")
        s.check(False, "a mixed selection is refused")
    except RpcError as err:
        s.check("select" in str(err), f"a mixed selection is refused ({err})")
```

(and `from rood_rpc import OUT, RpcError`).

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py --only save
```

Expected: an `RpcError` `action: list, filter, favourite, view, thumbs or save` … or `not yet` from the Task 15 stub.

- [ ] **Step 3: Directory listing from light_prefabs**

In `pc/src/light_prefabs.cpp`, move `json_files` and `directory_stamp` out of the anonymous namespace (close it before them and reopen it after them, or move the two functions above `namespace {` at line 914). In `pc/src/light_prefabs.h`, after `std::string make_id(const std::string& name);` add:

```cpp
/* A directory's *.json file names, sorted (none when it is missing), and a
 * stamp of their names, times and sizes (changes when any does). */
std::vector<std::string> json_files(const std::string& dir);
long long directory_stamp(const std::string& dir);
```

- [ ] **Step 4: The user's fog presets**

In `pc/src/atmosphere.cpp`:

Factor the per-preset parse out of `load_presets` and read the user's after the built-in ones:

```cpp
/* A preset's object (the built-in file's member, or a user file): name, why, its fields. */
Preset preset_from_json(const std::string& id, const prefab::Json& j)
{
    Preset p;
    p.id = id;
    for (const auto& f : j.members) {
        if (f.first == "name")
            p.name = f.second.string;
        else if (f.first == "why")
            p.why = f.second.string;
        else if (lighting::preset_field("fog", f.first))
            p.fields.emplace_back(f.first, lighting::canonical_value("fog", f.first, json_text(f.second)));
    }
    if (p.name.empty())
        p.name = p.id;
    return p;
}
/* The user's own presets (Save as prefab of a fog volume): one file each beside their light prefabs. */
std::string user_presets_dir() { return prefab::library().user_dir + "/fog"; }
long long user_presets_stamp = -1;
void load_presets()
{
    presets_loaded = true;
    ++presets_serial;
    presets.clear();
    std::string text, error;
    prefab::Json root;
    if (!read_text(presets_path(), text)) {
        presets_status = "no presets file (" + presets_path() + ")";
    } else if (!prefab::json_parse(text.c_str(), root, &error)) {
        presets_status = presets_path() + ": " + error;
        fprintf(stderr, "[atmosphere] %s\n", presets_status.c_str());
    } else {
        if (const prefab::Json* list = root.get("presets"))
            for (const auto& m : list->members)
                presets.push_back(preset_from_json(m.first, m.second));
        presets_status = std::to_string(presets.size()) + " presets (" + presets_path() + ")";
    }
    std::string dir = user_presets_dir();
    int mine = 0;
    for (const std::string& file : prefab::json_files(dir)) {
        std::string t, e, path = dir + "/" + file;
        prefab::Json j;
        if (!read_text(path, t) || !prefab::json_parse(t.c_str(), j, &e) || j.type != prefab::Json::Object) {
            fprintf(stderr, "[atmosphere] %s: %s\n", path.c_str(), e.empty() ? "not a preset object" : e.c_str());
            continue;
        }
        Preset p = preset_from_json(file.substr(0, file.size() - 5), j);
        p.user = true, p.path = path;
        auto same = std::find_if(presets.begin(), presets.end(), [&](const Preset& x) { return x.id == p.id; });
        if (same != presets.end())
            *same = p; /* yours wins over a built-in of the same id */
        else
            presets.push_back(p);
        mine++;
    }
    user_presets_stamp = prefab::directory_stamp(dir);
    if (mine)
        presets_status += ", " + std::to_string(mine) + " of yours (" + dir + ")";
}
```

Factor the JSON of a volume's look out of `save_preset`:

```cpp
/* A volume's look as a preset object: its fields that are not the schema's defaults (sim always says). */
prefab::Json preset_json(const Volume& v, const std::string& name, const std::string& why)
{
    prefab::Json p = prefab::Json::object();
    p.set("name", prefab::Json::str(name));
    if (!why.empty())
        p.set("why", prefab::Json::str(why));
    for (const auto& f : volume_fields(v)) {
        if (f.second == lighting::canonical_value("fog", f.first, lighting::field_default("fog", f.first)) && f.first != "sim")
            continue;
        char* end;
        double x = strtod(f.second.c_str(), &end);
        p.set(f.first.c_str(), *end == 0 && !f.second.empty() ? prefab::Json::num(x) : prefab::Json::str(f.second));
    }
    return p;
}
```

and use it in `save_preset` (`prefab::Json p = preset_json(v, name.empty() ? id : name, old ? old->why : "");` replacing its own loop and `name` / `why` lines).

Make `fog_presets_serial` notice files changing on disk (twice a second at most):

```cpp
unsigned rood_atmo::fog_presets_serial()
{
    if (!presets_loaded)
        load_presets();
    static double checked;
    double t = now_seconds();
    if (t - checked > 0.5) {
        checked = t;
        if (prefab::directory_stamp(user_presets_dir()) != user_presets_stamp)
            load_presets();
    }
    return presets_serial;
}
```

In `pc/src/environment_internal.h` add:

```cpp
/* The volume's look as a preset of the user's (<user prefab dir>/fog/<id>.json,
 * the id from the name, unique); the volume then uses it. */
bool save_user_preset(int index, const std::string& name, std::string* id, std::string* message);
```

and in `atmosphere.cpp`:

```cpp
bool rood_atmo::save_user_preset(int index, const std::string& name, std::string* id_out, std::string* message)
{
    if (index < 0 || index >= (int)volumes.size()) {
        *message = "Save as prefab: no such fog volume";
        return false;
    }
    if (!presets_loaded)
        load_presets();
    std::string base = prefab::make_id(name), id = base;
    for (int n = 2; find_preset(id); n++)
        id = base + "_" + std::to_string(n);
    Volume& v = volumes[index];
    std::string dir = user_presets_dir(), path = dir + "/" + id + ".json", temp = path + ".tmp";
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/')
            pc_mkdir(dir.substr(0, i).c_str());
    std::string text = prefab::json_write(preset_json(v, name, "")) + "\n";
    FILE* f = fopen(temp.c_str(), "wb");
    bool ok = f && fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f)
        fclose(f);
    if (!ok || pc_replace_file(temp.c_str(), path.c_str())) {
        *message = "cannot write " + path;
        return false;
    }
    load_presets();
    v.preset = id; /* it saves only what differs from its preset now */
    pc_lights_mark_dirty();
    *id_out = id;
    *message = "Saved fog preset " + name + " (" + path + ")";
    return true;
}
```

(add `#include "platform.h"` for `pc_mkdir` / `pc_replace_file` if `atmosphere.cpp` lacks it.)

- [ ] **Step 5: Save as prefab**

In `pc/src/editor_library.cpp`, replace the stub `save_selection` with:

```cpp
std::string rood_library::save_selection(const std::string& name, std::string* message)
{
    if (name.empty()) {
        *message = "Save as prefab: give it a name";
        return std::string();
    }
    const std::vector<rood_selection::Ref>& sel = rood_selection::items();
    int lights_n = 0, actors_n = 0, volumes_n = 0;
    for (const rood_selection::Ref& r : sel)
        lights_n += r.kind == rood_selection::KIND_LIGHT, actors_n += r.kind == rood_selection::KIND_ACTOR,
            volumes_n += r.kind == rood_selection::KIND_VOLUME;
    std::string item;
    if (lights_n && lights_n == (int)sel.size()) { /* lights: a light prefab; they become an actor of it */
        rood_scene::publish(); /* the lights' selected and multi follow the selection (Create prefab reads them) */
        if (rood_lights::create_prefab_from_selection(name) && rood_lights::selected_actor >= 0)
            item = "prefab:" + rood_lights::actors[rood_lights::selected_actor].prefab;
        *message = rood_lights::status;
    } else if (actors_n == 1 && sel.size() == 1) { /* an actor: a prefab with its values; it uses it */
        int i = rood_scene::index_of(sel[0]);
        if (i >= 0 && rood_lights::save_actor_as_prefab(i, name))
            item = "prefab:" + rood_lights::actors[i].prefab;
        *message = rood_lights::status;
    } else if (volumes_n == 1 && sel.size() == 1) { /* a fog volume: a fog preset of the user's; it uses it */
        std::string id;
        if (rood_atmo::save_user_preset(rood_scene::index_of(sel[0]), name, &id, message))
            item = "fog:" + id;
    } else {
        *message = "Save as prefab: select one or more lights, one light actor or one fog volume";
    }
    if (!item.empty())
        pc_undo_hint(("Save as prefab: " + name).c_str());
    return item;
}
```

On the panel's toolbar, after the Pop out / Dock button, add:

```cpp
    ImGui::SameLine();
    static char save_name[64];
    ImGui::BeginDisabled(rood_selection::empty());
    if (ImGui::SmallButton("Save..."))
        ImGui::OpenPopup("library save");
    ImGui::EndDisabled();
    keep(ui_rects, "save");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Save as prefab: the selected lights (a light prefab), light actor (a prefab with its values)\n"
                          "or fog volume (a fog preset), into your library (Mine)");
    if (ImGui::BeginPopup("library save")) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputTextWithHint("##name", "Name", save_name, sizeof save_name, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Save") || enter) && save_name[0]) {
            std::string message;
            save_selection(save_name, &message);
            snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
            save_name[0] = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
```

(`rood_lights::status` is a `char[256]`.) In the RPC, before `} else if (action != "list") {` add:

```cpp
    } else if (action == "save") { /* Save as prefab from the selection */
        const Json* name = params.get("name");
        std::string message;
        std::string item = name && name->type == Json::String ? rood_library::save_selection(name->string, &message) : "";
        if (item.empty()) {
            snprintf(out, size, "%s", message.empty() ? "save needs name" : message.c_str());
            return -1;
        }
        std::string text = "{\"item\":" + q(item) + ",\"message\":" + q(message) + "}";
        snprintf(out, size, "%s", text.c_str());
        return (int)text.size() < size ? 0 : -1;
```

- [ ] **Step 6: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_library_smoke.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest light_actors fog_volume atmosphere editor_schema
```

Expected: every section passes; the prefab probe and the fog suites are unchanged.

- [ ] **Step 7: Docs**

In `pc/assets/prefabs/README.md`, add a section:

```markdown
## Your library

The editor's Library (beside the Outliner; Shift+1) lists the built-in
prefabs, yours (`pc/user/prefabs`, `ROOD_PREFABS`: "Mine"), the fixture
catalog's light classes, the fog presets and yours. Library > Save... makes
one of yours from the selection: lights become a light prefab (replaced by
an actor of it), a light actor a prefab with its values, a fog volume a fog
preset in `pc/user/prefabs/fog/<id>.json` (same fields as an entry of
`pc/assets/environment/fog_presets.json`). Yours override a built-in of the
same id. Thumbnails are rendered by the game into `pc/build/library_thumbs`
(never committed).
```

In `lighting/README.md`, under the fog volumes' `preset` field, add: "A preset may be one of yours (`pc/user/prefabs/fog/<id>.json`): a room file naming it then needs your file; promote only volumes with built-in presets (or none) into a pack."

- [ ] **Step 8: Commit**

```bash
git add pc/src/light_prefabs.h pc/src/light_prefabs.cpp pc/src/environment_internal.h pc/src/atmosphere.cpp pc/src/editor_library.cpp pc/tests/editor_library_smoke.py pc/assets/prefabs/README.md lighting/README.md
git commit -m "$(cat <<'EOF'
Library: Save as prefab from the selection (lights, a light actor, a fog volume); the user's fog presets beside their prefabs; Mine

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 19: Fit a rect light to a window pane

**Files:**
- Create: `pc/src/editor_fit_window.h`, `pc/src/editor_fit_window.cpp`
- Modify: `pc/src/lights.cpp` (the viewport's click and hint; a rect light's "Fit to window" button), `pc/src/editor_handles.cpp` (RPC `fit_window`, `fit_arm`; `state_json` "fit"), `pc/src/dev_overlay.cpp` (Escape, the "+ Place" menu item, help rows), `pc/src/project_settings.cpp`, `pc/tools/build_boot.py`, `pc/tests/editor_handles_smoke.py` (a `fit` section)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_handles_smoke.py`, add `from editor_library_smoke import floor_and_wall  # noqa: E402` to the imports, to the docstring's list:

```
- fit: "fit rect light to window" on a wall: a new rect light on the
  clicked pane's plane (editor_fit_window_offset in front, facing out), its
  size the pane's, selected, one undo step; with that light selected a
  second fit moves it (no new light); the offset is tunable; armed from
  RPC (as the Details button and the toolbar do) a real click fits;
  Escape disarms;
```

and add (to `SECTIONS`):

```python
def check_fit(s):
    g = s.game
    _, wall = floor_and_wall(s)
    if not s.check(wall is not None, "a wall pixel in the view"):
        return
    xy, hit = wall
    s.select(action="clear")
    count, done = len(g.call("lights")["lights"]), s.history()["done"]
    r = handles(s, action="fit_window", x=xy[0], y=xy[1])
    lights = g.call("lights")["lights"]
    new = lights[-1]["id"] if len(lights) == count + 1 else None
    if not s.check(new is not None and lights[-1]["type"] == "rect" and r["light"] == new, f"a new rect light ({r})"):
        return
    f = fields(s, new)
    n = f["direction"]
    off = sum((f["position"][k] - hit["world"][k]) * n[k] for k in range(3))
    s.check(abs(sum(n[k] * hit["normal"][k] for k in range(3))) > 0.95 and abs(off - 2) < 0.5
            and f["width"] >= 8 and f["height"] >= 8 and s.select()["selection"] == [new]
            and s.history()["done"] == done + 1 and s.history()["undo"] == "Fit rect light to window",
            f"on the pane's plane, 2 units out, facing out, sized ({f['width']} x {f['height']}), selected, one undo step "
            f"({off:.2f}, {s.history()['undo']!r})")
    view = s.select()["view"]
    other = [xy[0] + (40 if xy[0] < view[0] + view[2] / 2 else -40), xy[1] + 20]
    r = handles(s, action="fit_window", x=other[0], y=other[1])
    s.check(r["light"] == new and len(g.call("lights")["lights"]) == count + 1,
            "with it selected a second fit moves that light (no new one)")
    g.call("editor_undo", action="undo")
    handles(s, action="fit_window", offset=10)
    handles(s, action="fit_window", x=xy[0], y=xy[1])
    f = fields(s, new)
    off = sum((f["position"][k] - hit["world"][k]) * f["direction"][k] for k in range(3))
    s.check(abs(off - 10) < 0.5, f"the offset is tunable (10: {off:.2f})")
    handles(s, action="fit_window", offset=2)
    handles(s, action="fit_arm")
    s.check(handles(s)["fit"]["armed"] and handles(s)["fit"]["target"] == new, "armed on the selected rect light")
    s.select(action="clear")  # its handles and gizmo out of the way of the click (the tool keeps its target)
    s.click(other)
    s.check(not handles(s)["fit"]["armed"] and s.history()["undo"] == "Fit rect light to window", "a real click fits it")
    handles(s, action="fit_arm", new=True)
    s.keys(["escape"])
    s.check(not handles(s)["fit"]["armed"] and s.keys([])["visible"], "Escape disarms (the editor stays open)")
    s.select(action="set", ids=[new])
    s.keys(["delete"])
    s.select(action="set", ids=[s.light])
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py --only fit
```

Expected: an `RpcError` `action: state, where or colour`.

- [ ] **Step 3: The tool**

Create `pc/src/editor_fit_window.h`:

```cpp
/* "Fit rect light to window" (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * the rect light's "Later: fit to window"; the de plan's decision 17): armed,
 * a click on a flat textured room surface (a window pane: the connected
 * triangles in its plane with its texture, rood_math::fit_pane) sizes and
 * places a rect light on it, offset in front of it and facing out; the
 * selected rect light, or a new one in a daylight colour. One undo step.
 * Settings keys editor_fit_window_offset (default 2 units) and
 * editor_fit_window_kelvin (default 5600 K). Editor internals; RPC
 * editor_handles {action "fit_window" | "fit_arm"}. */
#ifndef ROOD_EDITOR_FIT_WINDOW_H
#define ROOD_EDITOR_FIT_WINDOW_H
#include <stdio.h>
#include <string>

#include "editor_selection.h"

namespace rood_fit {
extern float offset, kelvin; /* tunable: the settings keys, the tool's popup, RPC */
void arm(const rood_selection::Ref& light); /* the next click fits that rect light; an empty ref: a new one */
bool armed();
rood_selection::Ref target();
bool cancel(); /* Escape: true when it was armed */
bool viewport_click(float x, float y); /* armed: this click fits (true: it took the click) */
void hint(void* draw_list); /* armed: the pane under the pointer outlined, a label */
void options(); /* the offset and the colour (a popup's body) */
/* Fit at window pixel (x, y): `light` (a rect light) or a new one; its index, or -1 and *message. */
int fit_at(float x, float y, const rood_selection::Ref& light, std::string* message);
bool parse_setting(const char* key, const char* rest);
void write_settings(FILE* f);
std::string state_json();
} // namespace rood_fit
#endif
```

Create `pc/src/editor_fit_window.cpp`:

```cpp
// Fit rect light to window (editor_fit_window.h).
#include "editor_fit_window.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "imgui.h"
#include "editor_math.h"
#include "editor_overlays.h"
#include "editor_scene.h"
#include "editor_undo.h"
#include "lights_internal.h"
#include "scene_capture.h"

extern "C" int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick); /* lights.cpp */

using rood_selection::Ref;

namespace rood_fit {
float offset = 2, kelvin = 5600;
}

namespace {
bool is_armed;
Ref aimed; /* the rect light to fit (empty: a new one) */

/* The pane under window pixel (x, y): false when there is no room surface there. */
bool pane_at(float x, float y, rood_math::Pane& pane, std::string* message)
{
    pc_overlay_view v;
    pc_overlay_pick pick = nullptr;
    float w[3], n[3];
    if (!pc_lights_tools_view(&v, &pick) || !pick || !pick(x, y, w, n)) {
        *message = "Fit to window: no surface there";
        return false;
    }
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data(&count);
    int seed = t ? pc_scene_triangle_at(w, 2.0f) : -1;
    if (seed < 0) {
        *message = "Fit to window: not a room surface (a character or a prop)";
        return false;
    }
    std::vector<rood_math::PaneTriangle> tris((size_t)count);
    for (int i = 0; i < count; i++) {
        for (int c = 0; c < 3; c++)
            memcpy(tris[i].v[c], t[i].vertex[c].position, sizeof tris[i].v[c]);
        tris[i].texture = (unsigned)t[i].tpage << 16 | t[i].clut;
    }
    float eye[3];
    rood_math::view_eye(v.view, eye);
    if (!rood_math::fit_pane(tris.data(), count, seed, eye, pane) || pane.width < 1 || pane.height < 1) {
        *message = "Fit to window: no flat pane there";
        return false;
    }
    return true;
}
} // namespace

void rood_fit::arm(const Ref& light)
{
    is_armed = true;
    aimed = light;
}
bool rood_fit::armed() { return is_armed; }
Ref rood_fit::target() { return aimed; }
bool rood_fit::cancel()
{
    bool was = is_armed;
    is_armed = false;
    return was;
}
bool rood_fit::viewport_click(float x, float y)
{
    if (!is_armed)
        return false;
    is_armed = false;
    std::string message;
    if (fit_at(x, y, aimed, &message) < 0)
        snprintf(rood_lights::status, sizeof rood_lights::status, "%s", message.c_str());
    return true;
}
int rood_fit::fit_at(float x, float y, const Ref& light, std::string* message)
{
    using rood_lights::lights;
    rood_math::Pane pane;
    if (!pane_at(x, y, pane, message))
        return -1;
    rood_lights::track_room();
    int index = light ? rood_scene::index_of(light) : -1;
    if (index < 0 || lights[index].type != 2) { /* a new rect light in daylight */
        rood_lights::Light l;
        l.type = 2;
        rood_math::kelvin_tint(kelvin, 0, l.color);
        lights.push_back(l);
        index = (int)lights.size() - 1;
    }
    rood_lights::Light& l = lights[index];
    for (int k = 0; k < 3; k++) {
        l.position[k] = pane.centre[k] + pane.normal[k] * offset;
        l.direction[k] = pane.normal[k];
    }
    l.size[0] = fminf(fmaxf(pane.width, 8.0f), 4096.0f);
    l.size[1] = fminf(fmaxf(pane.height, 8.0f), 4096.0f);
    rood_lights::dirty = true;
    rood_lights::select_light(index);
    pc_undo_hint("Fit rect light to window");
    snprintf(rood_lights::status, sizeof rood_lights::status, "Fitted a rect light to a %.0f x %.0f pane (%d triangles)",
        pane.width, pane.height, pane.triangles);
    *message = rood_lights::status;
    return index;
}
void rood_fit::hint(void* draw_list)
{
    if (!is_armed)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* front = ImGui::GetForegroundDrawList();
    const char* text = aimed ? "Click a window pane: the rect light fits it (Esc cancels)"
                             : "Click a window pane: a rect light on it (Esc cancels)";
    front->AddText(ImVec2(io.MousePos.x + 16, io.MousePos.y + 14), IM_COL32(255, 230, 120, 255), text);
    /* The pane under the pointer, outlined (recomputed when the pointer moves). */
    static float last[2] = { -1, -1 };
    static rood_math::Pane pane;
    static bool have;
    if (io.MousePos.x != last[0] || io.MousePos.y != last[1]) {
        std::string ignored;
        have = pane_at(io.MousePos.x, io.MousePos.y, pane, &ignored);
        last[0] = io.MousePos.x, last[1] = io.MousePos.y;
    }
    pc_overlay_view v;
    if (!have || !pc_lights_tools_view(&v, nullptr))
        return;
    ImDrawList* d = (ImDrawList*)draw_list;
    ImVec2 q[4];
    for (int c = 0; c < 4; c++) {
        float sr = (c == 1 || c == 2 ? 0.5f : -0.5f) * pane.width, su = (c >= 2 ? 0.5f : -0.5f) * pane.height, p[3], px[2];
        for (int k = 0; k < 3; k++)
            p[k] = pane.centre[k] + pane.right[k] * sr + pane.up[k] * su;
        if (!rood_math::project(v.view, v.projection, v.rect, p, px))
            return;
        q[c] = ImVec2(px[0], px[1]);
    }
    d->AddPolyline(q, 4, IM_COL32(120, 220, 255, 230), ImDrawFlags_Closed, 2.0f);
}
void rood_fit::options()
{
    ImGui::SliderFloat("Offset", &offset, 0, 32, "%.1f u");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("How far in front of the pane the rect light goes (settings key editor_fit_window_offset)");
    ImGui::SliderFloat("New light's colour", &kelvin, 1500, 12000, "%.0f K");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("A new rect light's colour temperature (settings key editor_fit_window_kelvin); a fitted\n"
                          "light keeps its own colour");
}
bool rood_fit::parse_setting(const char* key, const char* rest)
{
    if (!strcmp(key, "editor_fit_window_offset")) {
        offset = fminf(fmaxf((float)atof(rest), 0.0f), 32.0f);
        return true;
    }
    if (!strcmp(key, "editor_fit_window_kelvin")) {
        kelvin = fminf(fmaxf((float)atof(rest), 1500.0f), 12000.0f);
        return true;
    }
    return false;
}
void rood_fit::write_settings(FILE* f)
{
    fprintf(f, "editor_fit_window_offset %g\neditor_fit_window_kelvin %g\n", offset, kelvin);
}
std::string rood_fit::state_json()
{
    char text[256];
    snprintf(text, sizeof text, "{\"armed\":%s,\"target\":%s,\"offset\":%g,\"kelvin\":%g}", is_armed ? "true" : "false",
        is_armed && aimed ? prefab::json_quote(aimed.id).c_str() : "null", offset, kelvin);
    return text;
}
```

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_library.cpp",` add `root / "pc/src/editor_fit_window.cpp",`.

- [ ] **Step 4: Wire it in**

In `pc/src/lights.cpp`, add `#include "editor_fit_window.h"`. In the viewport, before `/* A spot waiting to be aimed (its handle) or the colour popover's eyedropper takes the click. */` add:

```cpp
    /* Fit rect light to window (editor_fit_window.cpp), when armed, takes the click. */
    if (rood_fit::viewport_click(io.MousePos.x, io.MousePos.y))
        return;
```

and near `draw_place_drag(v, ov, draw);` (each frame of the viewport) add `rood_fit::hint(draw);`. In `light_details_extra`, after the Create prefab popup add:

```cpp
    if (l.type == 2) {
        if (ImGui::Button("Fit to window"))
            rood_fit::arm(rood_scene::light_ref(index));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Click a window pane in the view: this rect light takes its size and sits in front of it,\n"
                              "facing out (Esc cancels)");
        ImGui::SameLine();
        if (ImGui::SmallButton("...##fit"))
            ImGui::OpenPopup("fit options");
        if (ImGui::BeginPopup("fit options")) {
            rood_fit::options();
            ImGui::EndPopup();
        }
    }
```

In `pc/src/editor_handles.cpp`, add `#include "editor_fit_window.h"`; in `state_json`, before `+ ",\"colour\":"` add `+ ",\"fit\":" + rood_fit::state_json()`; in the RPC, before `} else if (action == "state") {` add:

```cpp
    } else if (action == "fit_window" || action == "fit_arm") {
        /* offset, kelvin: the tool's settings; fit_window {x, y | u, v}: fit there now; fit_arm: the next click.
         * The light: id, else (new: true) a new one, else the selection's primary when it is a rect light. */
        if (const prefab::Json* o = params.get("offset"))
            rood_fit::offset = fminf(fmaxf((float)o->number, 0.0f), 32.0f);
        if (const prefab::Json* k = params.get("kelvin"))
            rood_fit::kelvin = fminf(fmaxf((float)k->number, 1500.0f), 12000.0f);
        Ref light;
        const prefab::Json* fresh = params.get("new");
        if (const prefab::Json* id = params.get("id"))
            light = rood_selection::ref_of(id->string);
        else if (!(fresh && fresh->boolean)) {
            Ref p = rood_selection::primary();
            int i = rood_scene::index_of(p);
            if (p.kind == rood_selection::KIND_LIGHT && i >= 0 && rood_lights::lights[i].type == 2)
                light = p;
        }
        int index = -1;
        std::string message;
        const prefab::Json *x = params.get("x"), *y = params.get("y"), *u = params.get("u"), *w = params.get("v");
        if (action == "fit_arm") {
            rood_fit::arm(light);
        } else if ((x && y) || (u && w)) {
            float px = x ? (float)x->number : 0, py = y ? (float)y->number : 0;
            pc_overlay_view v;
            if (!x && pc_lights_tools_view(&v, nullptr))
                px = v.rect[0] + (float)u->number * v.rect[2], py = v.rect[1] + (float)w->number * v.rect[3];
            index = rood_fit::fit_at(px, py, light, &message);
            if (index < 0) {
                snprintf(out, size, "%s", message.c_str());
                return -1;
            }
        }
        text = "{\"index\":" + std::to_string(index) + ",\"light\":"
            + (index >= 0 ? prefab::json_quote(rood_scene::light_ref(index).id) : std::string("null")) + ",\"message\":"
            + prefab::json_quote(message) + ",\"fit\":" + rood_fit::state_json() + "}";
```

and extend the failure text to `action: state, where, colour, fit_window or fit_arm`.

In `pc/src/dev_overlay.cpp`, add `#include "editor_fit_window.h"`; in `pc_overlay_event` beside the other Escape handlers add:

```cpp
        /* Escape disarms "fit rect light to window" (editor_fit_window.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_fit::cancel())
            return 1;
```

in the "+ Place" popup after `pc_lights_place_menu();` add:

```cpp
        ImGui::Separator();
        if (ImGui::MenuItem("Rect light on a window..."))
            rood_fit::arm(rood_selection::Ref());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Click a window pane in the view: a rect light its size, in front of it, facing out");
        if (ImGui::BeginMenu("Fit to window options")) {
            rood_fit::options();
            ImGui::EndMenu();
        }
```

and a help row after the `"Handles"` rows:

```cpp
        { "Tools", "Fit to window (a rect light's Details, + Place)", "Click a window pane: the rect light takes its size and sits in front of it, facing out; Esc cancels" },
```

In `pc/src/project_settings.cpp`, add `#include "editor_fit_window.h"`, after the library's `parse_setting` line add `if (rood_fit::parse_setting(key, rest)) return 1;` and after `rood_library::write_settings(f);` add `rood_fit::write_settings(f);`.

- [ ] **Step 5: Build and run; see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/editor_handles_smoke.py --only fit,rect
```

Expected: every line `ok:`.

- [ ] **Step 6: Commit**

```bash
git add pc/src/editor_fit_window.h pc/src/editor_fit_window.cpp pc/src/lights.cpp pc/src/editor_handles.cpp pc/src/dev_overlay.cpp pc/src/project_settings.cpp pc/tools/build_boot.py pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
Fit rect light to window: click a pane (its connected flat triangles of one texture) to size and place a rect light in front of it, facing out; offset and colour tunable

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 20: Docs, help and full verification

**Files:**
- Modify: `pc/docs/plans/2026-10-15-artist-tools-design.md` (status), `lighting/README.md`, `pc/docs/Emissive.md`, `pc/src/dev_overlay.cpp` (help rows check)

- [ ] **Step 1: Docs**

In `pc/docs/plans/2026-10-15-artist-tools-design.md`, after `Status: design approved 2026-10-15.` add `Phases A-C landed 2026-10-15; D and E, fit to window and the follow-ups: pc/docs/plans/2026-10-16-artist-tools-de-plan.md.` and in "Current state" note that Place Actors became the Library.

In `lighting/README.md`, in the editor section: the Library (Shift+1: kinds, categories, Favourites, Mine, search; drag onto a surface; Save...), a fog volume's handles (ring: density; dot: soft edge; swatch: colour), the disturb weights' "Use global", Ctrl+click on a glow places, Fit to window.

In `pc/docs/Emissive.md`, in the editor section: "An entry's Duplicate (a copy of yours under a new id) and Delete... (yours; after a confirm) are on its title line; both, like every emissive edit, are undoable (Ctrl+Z)."

- [ ] **Step 2: No forbidden text in the branch's files**

```bash
git diff main --name-only | xargs grep -nE "$FORBIDDEN_TEXT" || echo clean
```

(`FORBIDDEN_TEXT`: the session rules' pattern of absolute home and temp
paths and other ports' names; it is not written into committed files.)

Expected: `clean`.

- [ ] **Step 3: The probes**

```bash
flock "$ROOD_HEAVY_LOCK" sh -c 'ninja -C pc/build && ctest --test-dir pc/build --output-on-failure'
```

Expected: `100% tests passed` (library_probe, editor_tools_probe among them).

- [ ] **Step 4: Every build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64 | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py | tail -1
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows | tail -1
```

Expected: each exits 0 with a `Built …` line, no `error:` and no new warning (`grep -c "warning:"` no higher than the baseline log's, minus the glow tooltip's).

- [ ] **Step 5: The suites (OpenGL, then Vulkan)**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest lighting_lint editor light_actors atmosphere fog emissive fixture lighting
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan editor light_actors atmosphere fog emissive
```

Expected: no failure beyond Task 1's baseline list. A failure here is investigated with superpowers:systematic-debugging (never by loading the CPU to reproduce a flake).

- [ ] **Step 6: Look at it**

Launch the 64-bit build in the save room with the editor open (`pc/tests/checkpoints.py launch_at save_room` with scratch `ROOD_LIGHTS`, `ROOD_SETTINGS`, `ROOD_PREFABS`), open the Library (`editor_ui {tab "library"}`), and save screenshots (RPC `screenshot` into the test output directory) of: the grid, the list, the floating window, a fog volume's handles, Details of a fog volume (its groups) and of the sun. Check by eye: thumbnails read (a lit floor, a fog volume's soft box), every tile has its type icon, nothing overlaps, the swatch opens the popover beside the volume. Close the game by its PID.

- [ ] **Step 7: Commit**

```bash
git add pc/docs/plans/2026-10-15-artist-tools-design.md lighting/README.md pc/docs/Emissive.md pc/src/dev_overlay.cpp
git commit -m "$(cat <<'EOF'
Docs and help: the Library, fog volume handles and groups, Use global weights, Ctrl+click on glows, Fit to window, emissive Duplicate/Delete

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-review

### Spec coverage

| Brief item | Task |
|---|---|
| D: preset-first | 10 (the header row) |
| D: Basics (open) / Look / Motion & interaction / Advanced / Transform | 10 |
| D: per-value ↺ to the preset (check) | 10 (one field per group reset to the preset's value) |
| D: density ring, soft-edge inner outline, colour swatch, R = size | 11 (layer, swatch, outline), 12 (ring, edge dot, R alongside) |
| D: disturb weights as "use global" + slider | 10 |
| E: one dockable library (lights, prefabs, fixtures, fog presets; emitters by kind later) | 15 (sources, `add_source`), 17 (docked or floating) |
| E: volume prefabs (drop a preset fog volume) | 15, 16 |
| E: list/grid, thumbnails by our renderer in a neutral scene, cached under pc/build, type icon per tile | 14, 17 |
| E: category chips, favourites, Mine, search | 14, 15, 17, 18 |
| E: drag into the view onto the surface (fog on the floor, fits; wall lights face out), one undo step, selected | 16, 17 (a real drag) |
| E: Save as prefab from any selection (lights, actors, fog volumes) | 18 |
| Emissive Delete / Duplicate, confirm, undo | 9 |
| Sun's Details as a schema kind | 8 |
| Glow selection across a room change | 4 |
| Ctrl+click on a glow places | 5 |
| Ladder transaction when the editor closes | 6 (and handle drags) |
| Kind*/Field& across a prefab reload | 7 |
| Glow tooltip format warning | 2 |
| Tonemapper lint warning | 3 |
| Fit rect light to window | 13 (pane maths), 19 |
| Tests: library drop floor / wall; save-as-prefab round trip; volume handles one field one undo; emissive delete/duplicate; sun kind; lint warning; existing suites | 16, 18, 12, 9, 8, 3, 20 |

### Placeholder scan

No step says "TBD" or "similar to"; every code step shows the code. Where an existing line must be found by its text the step quotes it; line numbers are main's at 5ae51beb and only guide.

### Type and name consistency

- `rood_schema::Kind::header`, `Field::uses_global`, `Field::set_uses_global`, `Field::global_tooltip`, `apply_global`, `collect`, `retired_count`: Tasks 7, 8, 10.
- `rood_handles::Drag::fields` / `field`, `volume_handles`, `volume_extras`, `colour_swatch`, `finish`: Tasks 6, 11, 12.
- `rood_atmo::FogPreset`, `fog_presets`, `fog_presets_serial`, `preset_look`, `volume_drop`, `place_volume`, `save_user_preset`, `details_header`: Tasks 10, 15, 16, 18.
- `rood_library::Entry`, `Filter`, `Source`, `add_source`, `entries`, `find`, `is_item`, `item_prefab`, `label`, `place`, `draw_ghost`, `panel`, `docked`, `window`, `save_selection`, `parse_setting`, `write_settings`; `pc_editor_library_rpc`: Tasks 14, 15, 16, 17, 18.
- `rood_thumbs::SIZE`, `VERSION`, `Look`, `look`, `Fog`, `Scene`, `render`, `hash`, `cache_dir`, `file_name`, `load`, `save`: Task 14, used in 15 and 17.
- `rood_math::fit_footprint`, `PaneTriangle`, `Pane`, `fit_pane`: Task 13, used in 16 and 19.
- `rood_fit::offset`, `kelvin`, `arm`, `armed`, `target`, `cancel`, `viewport_click`, `hint`, `options`, `fit_at`, `parse_setting`, `write_settings`, `state_json`: Task 19.
- `emissive::user_text`, `set_user_text`; editor_undo part `EMISSIVE`: Task 9.
- `pc_light_falloff`, `pc_smoothstep` (`light_shape.h`): Task 14.
- RPC: `editor_library {list, filter, favourite, view, thumbs, save}`, `editor_handles {fit_window, fit_arm}`, `emissive {duplicate, select, ui}`, `editor_schema {reload_probe}` and `set "global"`, `editor_ui {tab "library"}`.
- Settings keys: `editor_library_view`, `editor_library_docked`, `editor_library_window`, `editor_library_favourites`, `editor_fit_window_offset`, `editor_fit_window_kelvin`, `editor_colour_tabs` (five values now).
