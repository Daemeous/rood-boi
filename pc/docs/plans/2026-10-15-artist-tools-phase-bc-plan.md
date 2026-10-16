# Artist tools phases B and C Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Give the editor Unreal's W/E/R transform tools (R maps through the design's scale table: shape only, never brightness; a world/local switch on the toolbar), and Unreal-style viewport handles on the selected light and the sun (intensity ring, radius/range dots, cone and inner-cone handles, rect edges, barn doors, the sun's sky dome, a colour popover with Kelvin/tint, wheel, palette, recent colours, eyedropper and Hex/RGB), with barn doors and a spot inner cone evaluated by both renderers.

**Architecture:** Pure maths lives in `editor_math.*` (projection, handle rays, drag mappings, the sun's angles, colour) and `editor_transform.*` (the scale table), both tested by one CTest probe; `light_shape.h` / `light_shape.glsl` hold the spot edge and barn doors the renderers evaluate from slots the light list already has free. The handle layer (`editor_handles.*`) draws and drives handles generically (hover, drag, Ctrl/Shift/Alt, readout, Esc, one undo step, hidden while flying, primary only) and each kind's handles (`editor_light_handles.cpp`) change schema fields through `rood_schema::apply`; the colour popover (`editor_colour.*`) is an ImGui window opened from a handle. New RPCs (`editor_transform`, `editor_handles`, `editor_select` project/pick, `editor_input` hold) let headless tests drive real mouse drags.

**Tech Stack:** C++17, Dear ImGui 1.9x + ImGuizmo (vendored), SDL3, GLSL (OpenGL 3.3/4.3 strings, Vulkan GLSL with glslang), the project's JSON (`prefab::Json`), dev RPC (`pc/src/dev_rpc.c`), Python 3 headless RPC tests (`pc/tests/run.py`, `rood_rpc.py`, numpy + PIL for pixels), CTest probes (`pc/CMakeLists.txt`).

---

## Ground rules for every task

- No changes under `src/` (the decompilation). Everything here is `pc/` (plus `lighting/README.md`).
- Dear ImGui and ImGuizmo stay; nothing new is vendored; no game-derived assets are committed (test frames stay in `pc/build/tests/`).
- Editor-only state goes in the user settings with `editor_` keys (`editor_gizmo_space`, `editor_colour_tabs`, `editor_recent_colours`): `editor_undo.cpp` treats `editor_*` keys as preferences, so they never become undo steps. Lighting data gets only the new fields named below.
- Every editor action that changes room data names its undo step (`pc_undo_hint`, or `pc_undo_begin` for a drag).
- Committed files never mention other game ports by name and never contain absolute home-directory or temp-directory paths.
- Heavy work (builds, game tests) always runs under `flock "$ROOD_HEAVY_LOCK"` (a lock file shared by all concurrent agents), tests at `-j 2`. No step creates artificial CPU load (no busy loops, stress tools or extra parallel builds); a test waits only with `game.run(n)` (vsyncs).
- Tests use the pinned card `pc/build/test-card1.mcd`; they never read or write `pc/save`, `pc/user` or `pc/lights` (scratch `ROOD_LIGHTS`, `ROOD_SETTINGS` under the test's output directory).
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Windows: `windows.h` is force-included on MinGW (`pc/src/win_compat.h`), so no new identifier may be a Windows macro or typedef (`near`, `far`, `small`, `interface`, `DELETE`, `FLOAT`). New enums are prefixed (`SHAPE_*`, `COLOUR_TAB_*`); new parameters avoid `near`/`far` (`near_dir`, `far_dir`).
- The menus session is adding `rood_schema::add_kind` and `rood_details::draw_kind` (generic singleton kinds). This plan does not touch the schema's kind registry (`fixed_kinds`, `actor_kinds`, `find_kind`, `kind_of`, `all_kinds`) or `rood_details::draw`'s structure. Its only hunks in those files are new `Field`s inside `light_kind` / `actor_kind` (Tasks 4, 8) and one early-return branch for the sun in `rood_details::draw` (Task 17). Rebase onto whatever lands first; neither conflicts with an `add_kind` call.

## Decisions taken where the design leaves room

1. **Prefab scale is stored**: a new actor field `scale` (default 1, three decimals). `prefab::evaluate` multiplies its lights' offsets, jitter, radii, sizes, light sizes and barn door lengths by it, never intensities (the design's "light positions and radii scale together").
2. **Spot inner cone**: a new light field `inner` (full angle, degrees, 0..170). 0 means "automatic": the outer quarter of the cone's cosine range fades, exactly today's `smoothstep(cos_o, mix(cos_o, 1, 0.25), c)`, so every existing room renders as before.
3. **Barn doors** (rect lights): `barn_angle` (degrees from the rect's facing, 0..90: 0 straight ahead, the tightest beam; 90 folded flat, no effect; default 45) and `barn_length` (world units, default 0: no doors). One angle and length for all four doors (as Unreal). The renderers apply an analytic visibility factor (per axis, the share of the rect's width a point sees past the two doors), so shadow maps and traced shadows need no change.
4. **Packing without growing the light records**: a spot's `extent_right.x` (unused by spots) carries cos(inner half angle); a rect's `direction.w` (unused by rects) carries `length * cos(angle)` and `extent_right.w` carries `length * sin(angle)` (0 without doors, as today). The two shadow-map softness reads of `right.w` gain a "not a rect" test.
5. **Rect edge handles**: a plain drag moves that edge only (Unreal's box handles: the opposite edge stays, so the width and the position change, still one undo step "Edit Width"); Alt keeps it symmetric (only the width changes).
6. **The sun's disc** changes azimuth and elevation together (one undo step "Edit Sun direction"). The sun becomes selectable as `env:sun` (a new `rood_selection::KIND_SUN`); its handles show only while it is selected. Until a singleton schema kind exists, Details shows a note for it.
7. **World/local** applies to move and rotate; scale is always local (Unreal). Default world (the rotate tool used to be local only).
8. **Shadows toggle** only on the point light's ring (the design lists it there).
9. **Handles show for the selection's primary**; with several lights of one kind selected, a shared field goes to each (as Details), position-like values to the primary only.
10. **Eyedropper colour**: the presented pixel (3 x 3 mean) converted from display (sRGB) to linear and scaled so its brightest channel is 1 (Intensity keeps the brightness).
11. **Limitation kept**: the fog volumes' own renderer (`fog_volume.glsl`) has no rect extents in its light slots; it keeps the automatic spot edge and ignores barn doors. Documented in `Vulkan_Renderer.md`.

## File structure

Created:

| File | Responsibility |
|---|---|
| `pc/src/editor_math.h`, `.cpp` | Pure maths of the viewport tools: projection, the view's axes and eye, a light's frame, ray vs line / plane / sphere, drag mappings (log, linear, Ctrl, Shift), the sun's angles and dome, the automatic inner cone, colour (Kelvin + tint, hex, sRGB, normalise). No ImGui. |
| `pc/src/editor_transform.h`, `.cpp` | The design's scale table: gizmo ratios per local axis to a light's radius / size, an actor's scale, a fog volume's size; never intensity. No ImGui. |
| `pc/src/light_shape.h` | The spot edge and barn doors: CPU packing helpers, a C reference of the doors' factor, and `PC_LIGHT_SHAPE_GLSL` (the OpenGL shaders' copy). |
| `pc/shaders/vulkan/light_shape.glsl` | The Vulkan shaders' `barn_doors` and `light_shape`. |
| `pc/src/editor_handles.h`, `.cpp` | The handle layer: `Handle`/`Drag`, drawing, hover and press, drags with Ctrl/Shift/Alt, the readout, Esc, one undo step per drag, hidden while flying, primary only, spot aiming, RPC `editor_handles`. |
| `pc/src/editor_light_handles.cpp` | Each kind's handles: point (ring, radius dot, shadows toggle), spot (range, cone, inner cone, aim), rect (edges, range, barn doors and their flaps), the sun (icon, dome, disc, ring), the colour swatch. |
| `pc/src/editor_colour.h`, `.cpp` | The colour popover: Temperature (Kelvin strip, named stops, tint) and Colour (wheel, value) tabs, room palette, recent colours, eyedropper, Hex/RGB, last tab per light type; its settings lines and RPC. |
| `pc/tests/editor_tools_probe.cpp` | CTest probe of `editor_math`, `editor_transform` and `light_shape.h`. |
| `pc/tests/editor_transform_smoke.py` | Headless test: W/E/R, world/local, R per light type, the R gizmo with the mouse, prefab and fog volume scale. |
| `pc/tests/barn_doors_smoke.py` | Headless pixel test: barn doors cut the light, the inner cone sets the edge (OpenGL and Vulkan). |
| `pc/tests/editor_handles_smoke.py` | Headless test: the handle layer and every handle with simulated mouse drags, the sun, the colour popover, the eyedropper. |

Modified:

| File | Change |
|---|---|
| `pc/src/lights.cpp` | `light_frame` through `rood_math`; W/E/R (`gizmo_operation` 2 = scale), `pc_lights_gizmo_space`, scale through `rood_transform`; RPC `editor_transform`; packing of the inner cone and barn doors; `from_eval` copies the new fields; the handle layer in the viewport (ImGuizmo disabled under a handle, aim / eyedropper clicks); the old sun gizmo call removed. |
| `pc/src/lights_internal.h` | `Light::inner`, `barn_angle`, `barn_length`; `gizmo_operation` comment. |
| `pc/src/light_prefabs.h`, `.cpp` | `Actor::scale`; `EvalLight` / `LightTemplate` `inner`, `barn_angle`, `barn_length`; `evaluate` scales by the actor. |
| `pc/src/lighting_layers.cpp` | Light fields `inner`, `barn_angle`, `barn_length`; actor field `scale`; their conversions. |
| `pc/tools/lighting_format.py` | The same fields (the schemas must match). |
| `pc/src/editor_schema.cpp` | Fields: spot `inner`, rect `barn_angle` / `barn_length`, actor `scale`. |
| `pc/src/editor_undo.cpp` | `same_light` / `same_actor` compare the new fields. |
| `pc/src/light_actors.cpp` | Actor JSON `scale`; Break into lights and Create prefab carry the new light fields. |
| `pc/src/atmosphere.cpp` | The fog volume's gizmo follows W/E/R and the space switch; `volume_size` / `set_volume_size`. |
| `pc/src/environment_internal.h`, `environment.cpp` | `rood_env::SunState`, `sun_state`, `set_sun_state`, `sun_icon_shown`; `pc_env_draw` removed. |
| `pc/src/editor_selection.h`, `.cpp` | `KIND_SUN` (`env:sun`). |
| `pc/src/editor_scene.cpp` | The sun as an object (exists, label, listed, not box-selected); RPC `editor_select` project / pick. |
| `pc/src/editor_details.cpp` | A note when the sun is the subject. |
| `pc/src/dev_overlay.cpp` | R key, Scale and World/Local on the toolbar; Esc cancels a handle drag / aiming / the popover; `editor_input` hold / release of modifiers; the colour popover drawn; help rows. |
| `pc/src/dev_rpc.c` | Routes `editor_transform`, `editor_handles`; `editor_input` hold / release; graphics key `editor_gizmo_space`; both new methods are undoable edits. |
| `pc/src/editor_view.cpp` | No Alt-orbit while a handle drags; the GL editor view's shader evaluates light shapes. |
| `pc/src/project_settings.cpp` | `editor_gizmo_space`; the colour popover's lines. |
| `pc/src/gl_present.c`, `vk_present.c` | Light shapes in the GL present shader; the eyedropper's pixel sample at present time. |
| `pc/src/ptgi.c` | Light shapes in the GL path tracer's four light evaluations. |
| `pc/src/light_list.h` | Documents the packed slots. |
| `pc/shaders/vulkan/rt_lights.glsl`, `rt_haze.comp`, `probe_trace.glsl`, `scaled.frag` | Light shapes. |
| `pc/tools/build_boot.py` | Compiles the five new sources. |
| `pc/CMakeLists.txt` | `editor_tools_probe`. |
| `pc/tests/run.py` | Registers the three smokes (checkpoint suites). |
| `pc/tests/prefab_probe.cpp`, `lighting_probe.cpp`, `selection_probe.cpp`, `editor_selection_smoke.py` | Checks for the new fields, `KIND_SUN`, project / pick. |
| `lighting/README.md`, `pc/assets/prefabs/README.md`, `pc/docs/Vulkan_Renderer.md` | The new fields, light shapes. |

---

### Task 1: Worktree and baseline

**Files:** none changed.

- [ ] **Step 1: Branch in a worktree**

Use superpowers:using-git-worktrees. From the main checkout:

```bash
git worktree add .claude/worktrees/artist-tools-bc -b claude/artist-tools-bc main
cd .claude/worktrees/artist-tools-bc
ln -s ../../../data data && ln -s ../../../disks disks
```

Expected: a new worktree on branch `claude/artist-tools-bc`.

- [ ] **Step 2: Configure CTest if this tree has no build yet**

```bash
test -f pc/build/build.ninja || cmake -S pc -B pc/build -G Ninja
```

Expected: `pc/build/build.ninja` exists.

- [ ] **Step 3: Build the test binaries**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
```

Expected: both exit 0 with a `Built …` line and no `error:` line.

- [ ] **Step 4: Record the baseline**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest editor light_actors atmosphere fog glow fixture lighting room_browser gi
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan editor light_actors atmosphere fog glow fixture lighting gi
```

Expected: tables ending in `N/N passed` (or a known failure list). Write down any suite that already fails; later tasks must not add failures.

---

## Phase B: transform

### Task 2: Editor maths: the view, handle rays, drags, the sun

**Files:**
- Create: `pc/src/editor_math.h`, `pc/src/editor_math.cpp`, `pc/tests/editor_tools_probe.cpp`
- Modify: `pc/CMakeLists.txt` (after the `selection_probe` block, line 51), `pc/tools/build_boot.py` (after `root / "pc/src/editor_details.cpp",`), `pc/src/lights.cpp` (`light_frame`, line 370)

- [ ] **Step 1: Write the failing probe**

Create `pc/tests/editor_tools_probe.cpp`:

```cpp
// The editor's viewport tools without the game: projection and the view's
// axes (pc/src/editor_math.cpp), where a mouse ray meets a handle, drags
// (logarithmic, linear, Ctrl, Shift), the sun's angles and dome; later the
// scale table (editor_transform.cpp), the renderers' light shapes
// (light_shape.h) and colour.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "editor_math.h"

static int failures;
static void check(bool ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static bool near(float a, float b, float e = 1e-3f) { return fabsf(a - b) <= e; }
static bool near3(const float* a, float x, float y, float z, float e = 1e-3f)
{
    return near(a[0], x, e) && near(a[1], y, e) && near(a[2], z, e);
}

/* A camera at the origin looking along +z, y down (lights.cpp gizmo_matrices'
 * convention: clip w = eye z), 100 x 100 pixels. */
static void test_view(float view[16], float projection[16], float rect[4])
{
    memset(view, 0, 16 * sizeof(float));
    view[0] = view[5] = view[10] = view[15] = 1;
    memset(projection, 0, 16 * sizeof(float));
    float n = 16, f = 65536;
    projection[0] = 1, projection[5] = -1, projection[10] = (f + n) / (f - n), projection[14] = -2 * f * n / (f - n);
    projection[11] = 1;
    rect[0] = rect[1] = 0, rect[2] = rect[3] = 100;
}

static void view_maths()
{
    float view[16], projection[16], rect[4], px[2];
    test_view(view, projection, rect);
    const float ahead[3] = { 0, 0, 100 }, right_of[3] = { 10, 0, 100 }, below[3] = { 0, 10, 100 }, behind[3] = { 0, 0, -100 };
    check(rood_math::project(view, projection, rect, ahead, px) && near(px[0], 50) && near(px[1], 50),
        "a point ahead is the view's centre");
    check(rood_math::project(view, projection, rect, right_of, px) && near(px[0], 55) && near(px[1], 50), "+x is to the right");
    check(rood_math::project(view, projection, rect, below, px) && near(px[1], 55), "+y (down) is down");
    check(!rood_math::project(view, projection, rect, behind, px), "behind the camera: no pixel");
    float r[3], u[3], f[3], eye[3];
    check(rood_math::view_axes(view, projection, rect, r, u, f) && near3(r, 1, 0, 0) && near3(u, 0, -1, 0)
            && near3(f, 0, 0, 1),
        "the view's axes: right +x, up -y, forward +z");
    check(rood_math::view_eye(view, eye) && near3(eye, 0, 0, 0), "the eye");
    view[12] = -5, view[13] = 2, view[14] = 7; /* R e + t = 0: the eye moves to (5, -2, -7) */
    check(rood_math::view_eye(view, eye) && near3(eye, 5, -2, -7), "the eye of a moved camera");
    float c[3];
    check(rood_math::dome_centre(view, projection, rect, c) && near3(c, 5, -2, 893, 0.5f), "the sky dome 900 units ahead");
    const float down[3] = { 0, 1, 0 };
    float lf[3], lr[3], lu[3];
    rood_math::light_frame(down, lf, lr, lu);
    check(near3(lf, 0, 1, 0) && fabsf(lr[0] * lf[0] + lr[1] * lf[1] + lr[2] * lf[2]) < 1e-5f
            && near(sqrtf(lu[0] * lu[0] + lu[1] * lu[1] + lu[2] * lu[2]), 1),
        "a light's frame: forward its direction, right and up across it");
}

static void rays()
{
    const float o[3] = { 3, 0, -10 }, d[3] = { 0, 0, 1 }, p[3] = { 0, 0, 0 }, x[3] = { 1, 0, 0 };
    check(near(rood_math::line_param(o, d, p, x), 3), "the line point nearest a ray (3 along x)");
    const float py[3] = { 0, 5, 0 };
    check(near(rood_math::line_param(o, d, py, x), 3), "a line passing beside the ray");
    const float n[3] = { 0, 0, 1 }, at[3] = { 0, 0, 20 };
    float hit[3];
    check(rood_math::plane_hit(o, d, at, n, hit) && near3(hit, 3, 0, 20), "a ray meets a plane");
    const float back[3] = { 0, 0, -1 };
    check(!rood_math::plane_hit(o, back, at, n, hit), "not behind it");
    const float c[3] = { 0, 0, 0 }, o2[3] = { 0, 0, -10 };
    float nd[3], fd[3];
    check(rood_math::sphere_hits(o2, d, c, 2, nd, fd) == 2 && near3(nd, 0, 0, -1) && near3(fd, 0, 0, 1),
        "a sphere: its near and far points");
    const float miss[3] = { 5, 0, -10 };
    check(rood_math::sphere_hits(miss, d, c, 2, nd, fd) == 0 && near3(nd, 1, 0, 0) && near3(fd, 1, 0, 0),
        "a ray missing it: toward its nearest point");
}

static void drags()
{
    check(near(rood_math::log_drag(1, 100, false), 2) && near(rood_math::log_drag(1, -100, false), 0.5f),
        "the ring doubles every 100 pixels");
    check(near(rood_math::log_drag(1, 100, true), powf(2, 0.1f)), "Ctrl: a tenth as fast");
    check(near(rood_math::log_drag(0, 100, false), 0.02f), "from 0 it starts at 0.01");
    check(near(rood_math::linear_drag(600, 10, 310, false), 900) && near(rood_math::linear_drag(600, 10, 310, true), 630),
        "linear drags (Ctrl: a tenth)");
    check(near(rood_math::snap_step(613, 16), 608) && near(rood_math::snap_step(-7, 5), -5), "Shift: multiples of the step");
    check(near(rood_math::snap_125(1.4f), 1) && near(rood_math::snap_125(1.5f), 2) && near(rood_math::snap_125(3.3f), 5)
            && near(rood_math::snap_125(0.27f), 0.2f) && near(rood_math::snap_125(7.2f), 10) && rood_math::snap_125(0) == 0,
        "Shift on a logarithmic value: 1, 2, 5 x 10^k");
}

static void sun()
{
    float d[3], az, el;
    rood_math::sun_direction(90, 0, d);
    check(near3(d, 1, 0, 0), "azimuth 90: from +x");
    rood_math::sun_direction(180, 45, d);
    check(near3(d, 0, -0.70711f, -0.70711f), "elevation is up (y down)");
    rood_math::sun_angles(d, &az, &el);
    check(near(az, 180, 1e-2f) && near(el, 45, 1e-2f), "back to the angles");
    rood_math::sun_direction(350, 10, d);
    rood_math::sun_angles(d, &az, &el);
    check(near(az, 350, 1e-2f) && near(el, 10, 1e-2f), "azimuth wraps to 0..360");
    float expect = 2 * acosf(cosf(30 * 3.14159265f / 180) * 0.75f + 0.25f) * 180 / 3.14159265f;
    check(near(rood_math::auto_inner(60), expect) && rood_math::auto_inner(60) < 60,
        "a spot's automatic inner cone: the outer quarter of its cosine fades");
}

int main()
{
    view_maths();
    rays();
    drags();
    sun();
    printf("%s\n", failures ? "FAIL" : "PASS: editor tools maths");
    return failures ? 1 : 0;
}
```

In `pc/CMakeLists.txt`, after `add_test(NAME selection_probe COMMAND selection_probe)` (line 51), add:

```cmake
# The editor's viewport maths, scale table and light shapes (pc/src/editor_math.cpp,
# editor_transform.cpp, light_shape.h) without the game.
add_executable(editor_tools_probe tests/editor_tools_probe.cpp src/editor_math.cpp src/editor_picking.cpp
    src/light_prefabs.cpp)
target_include_directories(editor_tools_probe PRIVATE src)
target_compile_features(editor_tools_probe PRIVATE cxx_std_17)
target_compile_options(editor_tools_probe PRIVATE -Wall -Wextra -Werror)
add_test(NAME editor_tools_probe COMMAND editor_tools_probe)
```

- [ ] **Step 2: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null && ninja -C pc/build editor_tools_probe
```

Expected: a compile error, `fatal error: editor_math.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `pc/src/editor_math.h`:

```cpp
/* Plain maths of the editor's viewport tools
 * (pc/docs/plans/2026-10-15-artist-tools-design.md, "Handle layer"):
 * projecting world points, the view's axes and eye, a light's frame, where a
 * mouse ray meets a handle's line, plane or sphere, how a drag turns into a
 * value (linear or logarithmic, Ctrl fine, Shift snapped), the sun's angles
 * and its sky dome, a spot's automatic inner cone, and colour (Kelvin and
 * tint, hex, display to linear). No Dear ImGui (pc/tests/editor_tools_probe.cpp). */
#ifndef ROOD_EDITOR_MATH_H
#define ROOD_EDITOR_MATH_H
#include <string>

namespace rood_math {
/* ---- the view: column-major view and projection (clip w > 0 in front) and
 * its window rectangle x, y, w, h (pc_overlay_view) ---- */
bool project(const float view[16], const float projection[16], const float rect[4], const float world[3], float out[2]);
/* The view's right, up (on screen) and forward axes, unit length, from rays
 * through its centre (whatever the matrices' conventions). */
bool view_axes(const float view[16], const float projection[16], const float rect[4], float right[3], float up[3],
    float forward[3]);
/* The eye (R e + t = 0; R may carry a scale). False when R does not invert. */
bool view_eye(const float view[16], float eye[3]);
/* An aimed light's frame (lights.cpp): forward = its direction, right and up
 * span a rect (world up is -y). */
void light_frame(const float direction[3], float forward[3], float right[3], float up[3]);

/* ---- where a ray (origin o, unit direction d) meets a handle ---- */
/* The parameter along the line p + s u (u unit) of its point nearest the ray. */
float line_param(const float o[3], const float d[3], const float p[3], const float u[3]);
/* Where the ray meets the plane through p with normal n; false when parallel or behind. */
bool plane_hit(const float o[3], const float d[3], const float p[3], const float n[3], float out[3]);
/* The unit directions from c to where the ray meets the sphere (c, r): its
 * nearer and farther points (2), or both toward the sphere's point nearest
 * the ray when it misses (0). */
int sphere_hits(const float o[3], const float d[3], const float c[3], float r, float near_dir[3], float far_dir[3]);

/* ---- drags ---- */
/* A logarithmic drag (an intensity ring): the value (at least 0.01) doubles
 * every 100 pixels to the right (Ctrl: every 1000). */
float log_drag(float start, float pixels, bool fine);
/* A linear drag: start plus the parameter's change (Ctrl: a tenth of it). */
float linear_drag(float start, float t_press, float t_now, bool fine);
float snap_step(float v, float step); /* the nearest multiple (Shift) */
float snap_125(float v); /* the nearest of 1, 2, 5 x 10^k (Shift on a logarithmic value; 0 stays 0) */

/* ---- the sun (environment.cpp: azimuth from +z toward +x, elevation above
 * the horizon, y down) ---- */
void sun_direction(float azimuth, float elevation, float out[3]);
void sun_angles(const float dir[3], float* azimuth, float* elevation); /* azimuth 0..360 */
/* The sky dome the sun's disc is dragged on: 900 units ahead of the eye,
 * radius 300 (where the old sun gizmo drew it). */
constexpr float DOME_AHEAD = 900, DOME_RADIUS = 300;
bool dome_centre(const float view[16], const float projection[16], const float rect[4], float out[3]);

/* ---- spots ---- */
/* The inner cone (full angle, degrees) a spot with inner 0 shows: the outer
 * quarter of its cosine range fades (light_shape.h pc_spot_cos_inner). */
float auto_inner(float cone);
} // namespace rood_math
#endif
```

- [ ] **Step 4: Write the implementation**

Create `pc/src/editor_math.cpp`:

```cpp
// Plain maths of the editor's viewport tools (editor_math.h).
#include "editor_math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "editor_picking.h"

namespace rood_math {
namespace {
const float DEG = 3.14159265f / 180.0f;
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void normalize(float v[3])
{
    float l = sqrtf(dot(v, v));
    for (int k = 0; k < 3; k++)
        v[k] = l > 1e-9f ? v[k] / l : 0;
}
} // namespace

bool project(const float view[16], const float projection[16], const float rect[4], const float w[3], float out[2])
{
    float e[4], c[4];
    for (int r = 0; r < 4; r++)
        e[r] = view[r] * w[0] + view[4 + r] * w[1] + view[8 + r] * w[2] + view[12 + r];
    for (int r = 0; r < 4; r++)
        c[r] = projection[r] * e[0] + projection[4 + r] * e[1] + projection[8 + r] * e[2] + projection[12 + r] * e[3];
    if (c[3] < 1e-3f)
        return false;
    out[0] = rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * rect[2];
    out[1] = rect[1] + (0.5f - c[1] / c[3] * 0.5f) * rect[3];
    return true;
}
bool view_axes(const float view[16], const float projection[16], const float rect[4], float right[3], float up[3],
    float forward[3])
{
    float cx = rect[0] + rect[2] * 0.5f, cy = rect[1] + rect[3] * 0.5f;
    float o[3], d[3], ox[3], dx[3], oy[3], dy[3];
    if (!rood_pick::ray(view, projection, rect, cx, cy, o, d) || !rood_pick::ray(view, projection, rect, cx + 1, cy, ox, dx)
        || !rood_pick::ray(view, projection, rect, cx, cy - 1, oy, dy))
        return false;
    for (int k = 0; k < 3; k++) {
        float p = o[k] + d[k] * 1000;
        right[k] = ox[k] + dx[k] * 1000 - p;
        up[k] = oy[k] + dy[k] * 1000 - p;
        forward[k] = d[k];
    }
    normalize(forward);
    /* across the central ray only (the neighbouring rays lean outward) */
    float r_on = dot(right, forward), u_on = dot(up, forward);
    for (int k = 0; k < 3; k++)
        right[k] -= forward[k] * r_on, up[k] -= forward[k] * u_on;
    normalize(right);
    normalize(up);
    return true;
}
bool view_eye(const float view[16], float eye[3])
{
    float m[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[r][c] = view[c * 4 + r];
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabsf(det) < 1e-12f)
        return false;
    for (int k = 0; k < 3; k++) { /* Cramer's rule on R e = -t */
        float a[3][3];
        memcpy(a, m, sizeof a);
        for (int r = 0; r < 3; r++)
            a[r][k] = -view[12 + r];
        eye[k] = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                     + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
            / det;
    }
    return true;
}
void light_frame(const float direction[3], float forward[3], float right[3], float up[3])
{
    float len = sqrtf(dot(direction, direction));
    for (int k = 0; k < 3; k++)
        forward[k] = len > 1e-6f ? direction[k] / len : (k == 1);
    float world_up[3] = { 0, -1, 0 };
    if (fabsf(forward[1]) > 0.95f)
        world_up[1] = 0, world_up[2] = 1;
    right[0] = world_up[1] * forward[2] - world_up[2] * forward[1];
    right[1] = world_up[2] * forward[0] - world_up[0] * forward[2];
    right[2] = world_up[0] * forward[1] - world_up[1] * forward[0];
    normalize(right);
    up[0] = forward[1] * right[2] - forward[2] * right[1];
    up[1] = forward[2] * right[0] - forward[0] * right[2];
    up[2] = forward[0] * right[1] - forward[1] * right[0];
}

float line_param(const float o[3], const float d[3], const float p[3], const float u[3])
{
    float w[3] = { o[0] - p[0], o[1] - p[1], o[2] - p[2] };
    float b = dot(d, u), denom = 1 - b * b;
    if (denom < 1e-6f) /* parallel: where the ray's origin is along the line */
        return dot(w, u);
    return (dot(u, w) - b * dot(d, w)) / denom;
}
bool plane_hit(const float o[3], const float d[3], const float p[3], const float n[3], float out[3])
{
    float denom = dot(d, n);
    if (fabsf(denom) < 1e-6f)
        return false;
    float w[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
    float t = dot(w, n) / denom;
    if (t < 0)
        return false;
    for (int k = 0; k < 3; k++)
        out[k] = o[k] + d[k] * t;
    return true;
}
int sphere_hits(const float o[3], const float d[3], const float c[3], float r, float near_dir[3], float far_dir[3])
{
    float oc[3] = { o[0] - c[0], o[1] - c[1], o[2] - c[2] };
    float b = dot(oc, d), disc = b * b - (dot(oc, oc) - r * r);
    if (disc < 0) {
        float t = fmaxf(-b, 0.0f);
        for (int k = 0; k < 3; k++)
            near_dir[k] = far_dir[k] = oc[k] + d[k] * t;
        normalize(near_dir);
        normalize(far_dir);
        return 0;
    }
    float s = sqrtf(disc), t0 = -b - s, t1 = -b + s;
    for (int k = 0; k < 3; k++)
        near_dir[k] = oc[k] + d[k] * t0, far_dir[k] = oc[k] + d[k] * t1;
    normalize(near_dir);
    normalize(far_dir);
    return 2;
}

float log_drag(float start, float pixels, bool fine) { return fmaxf(start, 0.01f) * exp2f(pixels / (fine ? 1000.0f : 100.0f)); }
float linear_drag(float start, float t_press, float t_now, bool fine) { return start + (t_now - t_press) * (fine ? 0.1f : 1.0f); }
float snap_step(float v, float step) { return step > 0 ? roundf(v / step) * step : v; }
float snap_125(float v)
{
    if (v <= 0)
        return 0;
    float e = floorf(log10f(v)), scale = powf(10, e), m = v / scale, best = 1, best_d = 1e30f;
    for (float c : { 1.0f, 2.0f, 5.0f, 10.0f }) {
        float dist = fabsf(logf(m / c));
        if (dist < best_d)
            best_d = dist, best = c;
    }
    return best * scale;
}

void sun_direction(float azimuth, float elevation, float out[3])
{
    float a = azimuth * DEG, e = elevation * DEG;
    out[0] = cosf(e) * sinf(a);
    out[1] = -sinf(e); /* y is down */
    out[2] = cosf(e) * cosf(a);
}
void sun_angles(const float dir[3], float* azimuth, float* elevation)
{
    *elevation = asinf(fminf(1.0f, fmaxf(-1.0f, -dir[1]))) / DEG;
    float a = atan2f(dir[0], dir[2]) / DEG;
    *azimuth = a < 0 ? a + 360 : a;
}
bool dome_centre(const float view[16], const float projection[16], const float rect[4], float out[3])
{
    float eye[3], r[3], u[3], f[3];
    if (!view_eye(view, eye) || !view_axes(view, projection, rect, r, u, f))
        return false;
    for (int k = 0; k < 3; k++)
        out[k] = eye[k] + f[k] * DOME_AHEAD;
    return true;
}

float auto_inner(float cone)
{
    float co = cosf(cone * 0.5f * DEG), ci = co + (1 - co) * 0.25f;
    return 2 * acosf(ci) / DEG;
}
} // namespace rood_math
```

- [ ] **Step 5: Run it and see it pass**

```bash
ninja -C pc/build editor_tools_probe && pc/build/editor_tools_probe
```

Expected: every line `ok:`, last line `PASS: editor tools maths`.

- [ ] **Step 6: lights.cpp uses the shared light frame; the game build compiles it**

In `pc/src/lights.cpp`, add `#include "editor_math.h"` after `#include "editor_selection.h"` (line 42), and replace the body of `static void light_frame(const Light& l, float forward[3], float right[3], float up[3])` (lines 370-388) with:

```cpp
static void light_frame(const Light& l, float forward[3], float right[3], float up[3])
{
    rood_math::light_frame(l.direction, forward, right, up);
}
```

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_details.cpp",` add:

```python
    root / "pc/src/editor_math.cpp",
```

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection light_actors
```

Expected: the build exits 0 with a `Built …` line; `2/2 passed` (the light frame is the same maths).

- [ ] **Step 7: Commit**

```bash
git add pc/src/editor_math.h pc/src/editor_math.cpp pc/tests/editor_tools_probe.cpp pc/CMakeLists.txt pc/tools/build_boot.py pc/src/lights.cpp
git commit -m "$(cat <<'EOF'
Editor maths: projection, view axes and eye, handle rays, drags (log/linear, Ctrl, Shift), the sun's dome (editor_tools_probe)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: The scale table

**Files:**
- Create: `pc/src/editor_transform.h`, `pc/src/editor_transform.cpp`
- Modify: `pc/tests/editor_tools_probe.cpp` (a new section before `int main()`, its call in `main`), `pc/CMakeLists.txt` (the `editor_tools_probe` sources), `pc/src/light_prefabs.h` (`Actor`, line 160), `pc/tools/build_boot.py`

- [ ] **Step 1: Add the failing checks**

In `pc/tests/editor_tools_probe.cpp`, add `#include "editor_transform.h"` after `#include "editor_math.h"`, insert before `int main()`:

```cpp
/* R (pc/docs/plans/2026-10-15-artist-tools-design.md, "Scale semantics"):
 * shape only, never brightness. */
static void scale_table()
{
    const float twice_z[3] = { 1, 1, 2 }, half_x[3] = { 0.5f, 1, 1.2f }, even[3] = { 1.5f, 1.5f, 1.5f };
    check(near(rood_transform::uniform_ratio(twice_z), 2) && near(rood_transform::uniform_ratio(half_x), 0.5f)
            && near(rood_transform::uniform_ratio(even), 1.5f),
        "a uniform shape takes the axis that moved most");
    rood_lights::Light point, out;
    point.intensity = 1.5f;
    rood_transform::scale_light(point, twice_z, out);
    check(near(out.radius, 1200) && out.intensity == 1.5f && !memcmp(out.color, point.color, sizeof out.color),
        "a point light: radius x2; intensity and colour kept");
    rood_lights::Light spot = point;
    spot.type = 1, spot.cone = 50;
    const float range[3] = { 1, 1, 1.5f };
    rood_transform::scale_light(spot, range, out);
    check(near(out.radius, 900) && out.cone == 50, "a spot: its range x1.5; the cone has its own handle");
    rood_lights::Light rect = point;
    rect.type = 2, rect.size[0] = 128, rect.size[1] = 64;
    const float per_axis[3] = { 2, 0.5f, 1.25f };
    rood_transform::scale_light(rect, per_axis, out);
    check(near(out.size[0], 256) && near(out.size[1], 32) && near(out.radius, 750) && out.intensity == 1.5f,
        "a rect: width x2, height x0.5, range x1.25");
    point.radius = 4000;
    rood_transform::scale_light(point, twice_z, out);
    check(near(out.radius, rood_transform::RADIUS_MAX), "clamped to the schema's range");
    prefab::Actor a, b;
    rood_transform::scale_actor(a, even, b);
    check(near(b.scale, 1.5f), "an actor: its scale x1.5");
    const float huge[3] = { 100, 100, 100 };
    rood_transform::scale_actor(a, huge, b);
    check(near(b.scale, rood_transform::SCALE_MAX), "an actor's scale clamped");
    const float size[3] = { 160, 128, 160 }, by[3] = { 2, 1, 0.5f };
    float sized[3];
    rood_transform::scale_volume(size, by, sized);
    check(near3(sized, 320, 128, 80), "a fog volume: its size per axis");
}
```

and in `main`, after `sun();` add `scale_table();`.

In `pc/CMakeLists.txt`, change the `editor_tools_probe` line to:

```cmake
add_executable(editor_tools_probe tests/editor_tools_probe.cpp src/editor_math.cpp src/editor_transform.cpp
    src/editor_picking.cpp src/light_prefabs.cpp)
```

- [ ] **Step 2: Run it and see it fail**

```bash
cmake -S pc -B pc/build -G Ninja >/dev/null && ninja -C pc/build editor_tools_probe
```

Expected: `fatal error: editor_transform.h: No such file or directory`.

- [ ] **Step 3: The actor's scale field**

In `pc/src/light_prefabs.h`, struct `Actor`, after `float rotation[3] = { 0, 0, 0 }; // yaw, pitch, roll (degrees): R = Ry(yaw) Rx(pitch) Rz(roll)` add:

```cpp
    float scale = 1; // its size (R): its lights' offsets, jitter, radii and sizes scale with it, never their intensity
```

- [ ] **Step 4: Write the header**

Create `pc/src/editor_transform.h`:

```cpp
/* R (scale) on the editor's objects (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Scale semantics"): a gizmo scale's ratios per local axis (right, up,
 * forward: a light's frame, an actor's rotation basis, a fog volume's yawed
 * x, y, z) mapped onto each kind's shape. Never brightness: no function here
 * touches an intensity or a colour. Plain maths (pc/tests/editor_tools_probe.cpp). */
#ifndef ROOD_EDITOR_TRANSFORM_H
#define ROOD_EDITOR_TRANSFORM_H
#include "lights_internal.h"

namespace rood_transform {
/* The schema's ranges (editor_schema.cpp light_kind and actor_kind; fog volumes at least 2). */
constexpr float RADIUS_MIN = 32, RADIUS_MAX = 4096, SIZE_MIN = 8, SIZE_MAX = 4096, SCALE_MIN = 0.05f, SCALE_MAX = 20,
                VOLUME_MIN = 2;
/* The ratio a uniform shape takes: the axis whose ratio is furthest from 1
 * (ImGuizmo's centre handle moves all three alike, an axis handle one). */
float uniform_ratio(const float ratio[3]);
/* A light from `start` (as the drag began): point: radius x the uniform
 * ratio; spot: radius (its range) x the uniform ratio, the cone kept; rect:
 * width x right, height x up, radius x forward. */
void scale_light(const rood_lights::Light& start, const float ratio[3], rood_lights::Light& out);
/* A light actor: its scale x the uniform ratio (its lights follow in prefab::evaluate). */
void scale_actor(const prefab::Actor& start, const float ratio[3], prefab::Actor& out);
/* A fog volume's size per axis. */
void scale_volume(const float start[3], const float ratio[3], float out[3]);
} // namespace rood_transform
#endif
```

- [ ] **Step 5: Write the implementation**

Create `pc/src/editor_transform.cpp`:

```cpp
// R (scale) on the editor's objects (editor_transform.h).
#include "editor_transform.h"

#include <math.h>

namespace rood_transform {
namespace {
float within(float v, float lo, float hi) { return fminf(fmaxf(v, lo), hi); }
} // namespace

float uniform_ratio(const float ratio[3])
{
    float best = 1, far_from_one = -1;
    for (int k = 0; k < 3; k++) {
        float r = fmaxf(ratio[k], 0.001f), d = fabsf(logf(r));
        if (d > far_from_one)
            far_from_one = d, best = r;
    }
    return best;
}
void scale_light(const rood_lights::Light& start, const float ratio[3], rood_lights::Light& out)
{
    out = start;
    if (start.type == 2) {
        out.size[0] = within(start.size[0] * ratio[0], SIZE_MIN, SIZE_MAX);
        out.size[1] = within(start.size[1] * ratio[1], SIZE_MIN, SIZE_MAX);
        out.radius = within(start.radius * ratio[2], RADIUS_MIN, RADIUS_MAX);
        return;
    }
    out.radius = within(start.radius * uniform_ratio(ratio), RADIUS_MIN, RADIUS_MAX);
}
void scale_actor(const prefab::Actor& start, const float ratio[3], prefab::Actor& out)
{
    out = start;
    out.scale = within(start.scale * uniform_ratio(ratio), SCALE_MIN, SCALE_MAX);
}
void scale_volume(const float start[3], const float ratio[3], float out[3])
{
    for (int k = 0; k < 3; k++)
        out[k] = fmaxf(start[k] * ratio[k], VOLUME_MIN);
}
} // namespace rood_transform
```

- [ ] **Step 6: Run it and see it pass**

```bash
ninja -C pc/build editor_tools_probe && pc/build/editor_tools_probe
```

Expected: every line `ok:`, last line `PASS: editor tools maths`.

- [ ] **Step 7: Build it into the game**

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_math.cpp",` add:

```python
    root / "pc/src/editor_transform.cpp",
```

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
```

Expected: exit 0, a `Built …` line.

- [ ] **Step 8: Commit**

```bash
git add pc/src/editor_transform.h pc/src/editor_transform.cpp pc/tests/editor_tools_probe.cpp pc/CMakeLists.txt pc/src/light_prefabs.h pc/tools/build_boot.py
git commit -m "$(cat <<'EOF'
Scale table (editor_transform): R changes a light's radius/range/size, an actor's scale, a fog volume's size; never intensity

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: A light actor's scale: its lights' offsets and radii follow

**Files:**
- Modify: `pc/src/light_prefabs.cpp` (`evaluate`, line 1338), `pc/src/lighting_layers.cpp` (actor `KindSpec`, line 37; `item_from_actor`; `actor_from_item`), `pc/tools/lighting_format.py` (actor fields, line 27), `pc/src/editor_undo.cpp` (`same_actor`, line 165), `pc/src/light_actors.cpp` (actor JSON, line 1541), `pc/src/editor_schema.cpp` (`actor_kind`, after the rotation field)
- Test: `pc/tests/prefab_probe.cpp`, `pc/tests/lighting_probe.cpp`

- [ ] **Step 1: Write the failing checks**

In `pc/tests/prefab_probe.cpp`, after the check `"a fixture's light skips its fixture's shadow (shadow_skip = fixture radius)"`, add:

```cpp
    /* Scale (R): the light's offset from the pivot and its radius grow with
     * the actor; its intensity does not. */
    Actor big = b;
    big.scale = 2;
    std::vector<EvalLight> scaled;
    evaluate(*candle, big, 0, false, scaled);
    check(scaled.size() == 1
            && near(scaled[0].position[1] - big.position[1], 2 * (still[0].position[1] - b.position[1]), 1e-3f)
            && near(scaled[0].radius, 2 * still[0].radius, 1e-3f) && near(scaled[0].intensity, still[0].intensity),
        "a scaled actor: its light twice as far from the pivot, twice the radius, the same intensity");
```

In `pc/tests/lighting_probe.cpp` `selftest()`, after the check `"an actor round-trips (parameters, glow link)"`, add:

```cpp
    prefab::Actor sized = a;
    sized.scale = 1.75f;
    Item si = item_from_actor(sized);
    prefab::Actor sb;
    check(actor_from_item(si, sb) && fabsf(sb.scale - 1.75f) < 1e-4f && si.get("scale") && !ai.get("scale"),
        "an actor's scale round-trips; at 1 it is left out");
```

- [ ] **Step 2: Run them and see them fail**

```bash
ninja -C pc/build prefab_probe lighting_probe && ctest --test-dir pc/build -R 'prefab_probe|lighting_layers' --output-on-failure
```

Expected: `FAIL: a scaled actor: ...` and `FAIL: an actor's scale round-trips; ...`; `0% tests passed, 2 tests failed`.

- [ ] **Step 3: Evaluate with the scale**

In `pc/src/light_prefabs.cpp` `evaluate`, after `float skip = a.fixture && !a.fixture_shadows ? fmaxf(actor_fixture_radius(a), 0) : 0;` add:

```cpp
    float s = a.scale > 0 ? a.scale : 1; /* its size (R): lengths scale, intensities do not */
```

and change these lines in the loop:

```cpp
        float local[3] = { offset.at(0) * s, offset.at(1) * s, offset.at(2) * s }, world[3];
```

```cpp
        l.radius = fmaxf(expr_eval(t.radius, lookup, Value::scalar(600)).x() * s, 1);
```

```cpp
        l.size[0] = fmaxf(size.at(0) * s, 1), l.size[1] = fmaxf((size.n == 3 ? size.v[1] : size.v[0]) * s, 1);
```

```cpp
        l.light_size = fmaxf(expr_eval(t.light_size, lookup, Value::scalar(0)).x() * s, 0);
```

```cpp
            float jitter = expr_eval(t.jitter, lookup, Value::scalar(0)).x() * s;
```

- [ ] **Step 4: Store it**

In `pc/src/lighting_layers.cpp`, the `"actor"` KindSpec: change `{ "rot", 2, "0,0,0", false },` to `{ "rot", 2, "0,0,0", false }, { "scale", 3, "1", false },`. In `item_from_actor`, after `it.set("rot", vec(a.rotation, 3, 2));` add:

```cpp
    it.set("scale", format_number(a.scale, 3));
```

In `actor_from_item`, after `get_vec(it, "rot", a.rotation, 3);` add:

```cpp
    a.scale = (float)get_num(it, "scale", 1);
```

In `pc/tools/lighting_format.py`, the `"actor"` entry: change `("rot", 2, "0,0,0", False), ("seed", 0, "1", False),` to `("rot", 2, "0,0,0", False), ("scale", 3, "1", False), ("seed", 0, "1", False),`.

- [ ] **Step 5: Run them and see them pass**

```bash
ninja -C pc/build prefab_probe lighting_probe && ctest --test-dir pc/build -R 'prefab_probe|lighting_layers' --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 2` (the C++ and Python schemas match).

- [ ] **Step 6: Undo, the actors RPC and the schema know it**

In `pc/src/editor_undo.cpp` `same_actor`, change `&& !memcmp(a.rotation, b.rotation, sizeof a.rotation) && a.enabled == b.enabled && a.seed == b.seed` to:

```cpp
        && !memcmp(a.rotation, b.rotation, sizeof a.rotation) && a.scale == b.scale && a.enabled == b.enabled && a.seed == b.seed
```

In `pc/src/light_actors.cpp`, the actor JSON: change `+ ",\"position\":" + vec_json(a.position) + ",\"rotation\":" + vec_json(a.rotation)` to:

```cpp
        + ",\"position\":" + vec_json(a.position) + ",\"rotation\":" + vec_json(a.rotation) + ",\"scale\":" + num(a.scale)
```

In `pc/src/editor_schema.cpp` `actor_kind`, after the rotation field's `k.fields.push_back(f);` add:

```cpp
    f = number_field("scale", "Scale", "Transform", 0.05f, 20, 0.01f, 1, "", true,
        "Its size (the scale tool, R): its lights' offsets, radii and sizes scale with it, never their brightness");
    f.get = [](int i) { return Value::scalar(actors[i].scale); };
    f.set = [](int i, const Value& v) { actors[i].scale = v.x(); };
    k.fields.push_back(f);
```

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema light_actors
```

Expected: build exit 0; `2/2 passed` (the lint takes the new field: range 0.05..20, default 1).

- [ ] **Step 7: Commit**

```bash
git add pc/src/light_prefabs.cpp pc/src/lighting_layers.cpp pc/tools/lighting_format.py pc/src/editor_undo.cpp pc/src/light_actors.cpp pc/src/editor_schema.cpp pc/tests/prefab_probe.cpp pc/tests/lighting_probe.cpp
git commit -m "$(cat <<'EOF'
Light actors: a scale field (lighting item, schema, undo); its lights' offsets, jitter, radii and sizes follow, intensities do not

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 5: The transform smoke (fails until the tools exist)

**Files:**
- Create: `pc/tests/editor_transform_smoke.py`
- Modify: `pc/tests/run.py` (after the `editor_schema_smoke` Suite, line 162; `CHECKPOINT_SUITES`, line 428)

- [ ] **Step 1: Write the test**

Create `pc/tests/editor_transform_smoke.py`:

```python
#!/usr/bin/env python3
"""The editor's transform tools (lights.cpp's gizmo, editor_transform.h): W,
E and R, and the toolbar's World / Local switch. R (scale) changes a light's
reach or size, a light actor's size (its lights' offsets and radii with it)
and a fog volume's size, and never brightness.

    python3 pc/tests/editor_transform_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The same save room and objects as editor_selection_smoke.py (a point light,
a candle, a fog volume). Sections:

- keys: W, E and R pick move, rotate and scale;
- space: World / Local (settings key editor_gizmo_space) sets the move and
  rotate manipulators' mode; scale is always local;
- lights: R on a point light scales its radius, on a spot its range (the cone
  kept), on a rect its width, height and range per axis; the intensity and
  colour never change; each is one undo step named Scale ...;
- mouse: a real drag of the scale gizmo's centre 50 pixels right makes a
  point light's radius 1.5 times larger (ImGuizmo: 1% per pixel), one undo step;
- actor: R on the candle puts its light 1.5 times as far from the pivot with
  1.5 times the radius and the same intensity;
- fog: R on the fog volume scales its size per axis.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import finish, pinned_card, start  # noqa: E402
from editor_schema_smoke import close, schema, value  # noqa: E402


def transform(s, **params):
    return s.game.call("editor_transform", **params)


def fields(s, ident):
    return {k: v["value"] for k, v in schema(s, action="get", id=ident)["fields"].items()}


def changed(a, b):
    return sorted(k for k in b if k in a and not close(a[k], b[k]))


def check_keys(s):
    s.select(action="set", ids=[s.light])
    for key, op in (("r", 2), ("e", 1), ("w", 0), ("r", 2)):
        s.check(s.keys([key])["gizmo"] == op, f"{key.upper()} picks tool {op}")
    s.check(transform(s)["operation"] == 2, "editor_transform reports the scale tool")
    s.keys(["w"])


def check_space(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "spot"})
    schema(s, action="set", id=s.light, values={"direction": [1, 0, 0]})
    for space in ("world", "local"):
        g.call("graphics", editor_gizmo_space=1 if space == "local" else 0)
        s.check(transform(s)["space"] == space, f"the space is {space}")
        for key, op, mode in (("w", "move", space), ("e", "rotate", space), ("r", "scale", "local")):
            s.keys([key])
            gz = transform(s)["gizmo"]
            s.check(gz["shown"] and gz["subject"] == s.light and gz["operation"] == op and gz["mode"] == mode,
                    f"{space}: {key.upper()} is {op} in {mode} space ({gz})")
    g.call("graphics", editor_gizmo_space=0)
    schema(s, action="set", id=s.light, values={"type": "point"})
    s.keys(["w"])


def check_lights(s):
    s.select(action="set", ids=[s.light])
    cases = [
        ("point", {}, [1, 1, 2], {"radius": 1200}),
        ("spot", {"cone": 50}, [1, 1, 1.5], {"radius": 900}),
        ("rect", {"width": 128, "height": 64}, [2, 0.5, 1.25], {"width": 256, "height": 32, "radius": 750}),
    ]
    for kind, extra, ratio, expect in cases:
        schema(s, action="set", id=s.light, values={"type": kind})
        schema(s, action="set", id=s.light, values={"radius": 600, "intensity": 1.5, **extra})
        before, done = fields(s, s.light), s.history()["done"]
        transform(s, action="scale", id=s.light, ratio=ratio)
        after, h = fields(s, s.light), s.history()
        s.check(changed(before, after) == sorted(expect) and all(close(after[k], v) for k, v in expect.items()),
                f"R {ratio} on a {kind} light: {changed(before, after)} -> {[after[k] for k in sorted(expect)]}")
        s.check(close(after["intensity"], 1.5) and close(after["color"], before["color"])
                and h["done"] == done + 1 and h["undo"].startswith("Scale"),
                f"{kind}: intensity and colour kept; one undo step ({h['undo']})")
    schema(s, action="set", id=s.light, values={"type": "point"})
    schema(s, action="set", id=s.light, values={"radius": 600, "intensity": 1})


def check_mouse(s):
    """A real drag of the scale gizmo's centre: ImGuizmo's uniform scale, 1% per pixel."""
    g = s.game
    s.select(action="set", ids=[s.light])
    s.keys(["r"])
    xy = s.objects()[s.light]["screen"]
    done = s.history()["done"]
    s.mouse(xy, down=True)
    s.mouse([xy[0] + 25, xy[1]], down=True)
    s.mouse([xy[0] + 50, xy[1]], down=True)
    s.mouse([xy[0] + 50, xy[1]], down=False)
    r, h = value(s, s.light, "radius"), s.history()
    s.check(close(r, 900, 0.01) and h["done"] == done + 1 and h["undo"].startswith("Scale"),
            f"the gizmo's centre dragged 50 px right: radius 600 -> {r} (900), one undo step ({h['undo']})")
    g.call("editor_undo", action="undo")
    s.check(close(value(s, s.light, "radius"), 600), "undo puts the radius back")
    s.keys(["w"])


def check_actor(s):
    g = s.game
    g.call("actors", time=0)  # the animation frozen: the light's position is exact
    g.run(2)

    def light_of():
        a = next(x for x in g.call("actors")["actors"] if x["id"] == s.actor)
        return a["position"], a["lights"][0]

    pivot, before = light_of()
    s.select(action="set", ids=[s.actor])
    transform(s, action="scale", id=s.actor, ratio=[1.5, 1.5, 1.5])
    pivot2, after = light_of()
    off0 = [b - p for b, p in zip(before["position"], pivot)]
    off1 = [b - p for b, p in zip(after["position"], pivot2)]
    s.check(close(off1, [1.5 * x for x in off0], 0.01) and close(after["radius"], 1.5 * before["radius"], 0.01),
            f"R on the candle: its light 1.5x as far from the pivot ({off0} -> {off1}) with 1.5x the radius "
            f"({before['radius']} -> {after['radius']})")
    s.check(close(after["intensity"], before["intensity"]) and close(value(s, s.actor, "scale"), 1.5),
            f"its intensity kept ({before['intensity']} -> {after['intensity']}); Scale reads 1.5")
    g.call("editor_undo", action="undo")
    s.check(close(value(s, s.actor, "scale"), 1), "undo: scale 1 again")


def check_fog(s):
    s.select(action="set", ids=[s.volume])
    before = value(s, s.volume, "size")
    transform(s, action="scale", ratio=[2, 1, 0.5])
    after = value(s, s.volume, "size")
    s.check(close(after, [before[0] * 2, before[1], before[2] * 0.5]) and s.history()["undo"] == "Scale fog volume",
            f"R on the fog volume: its size {before} -> {after} ({s.history()['undo']})")
    s.game.call("editor_undo", action="undo")


SECTIONS = [check_keys, check_space, check_lights, check_mouse, check_actor, check_fog]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (keys, space, ...)")
    args = parser.parse_args()
    s = start(args, "editor_transform")
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
        status = finish(s, "W/E/R and World/Local; R changes shape (lights, actors, fog volumes), never brightness")
    return status


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `editor_schema_smoke` Suite entry (it ends with `"from it (categories, search, lock, reset arrow), the value ladder (middle-drag, Esc)"),`) add:

```python
            Suite("editor_transform_smoke", [PY, str(TESTS / "editor_transform_smoke.py"), "--card", str(args.save)],
                  300, note="W/E/R and World/Local; R scales a light's reach or size, an actor's lights and radii, a fog "
                            "volume's size, never brightness; the scale gizmo's centre with the mouse"),
```

and in `CHECKPOINT_SUITES` change `"editor_schema_smoke", "facing_cutaway_smoke",` to `"editor_schema_smoke", "editor_transform_smoke", "facing_cutaway_smoke",`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_transform
```

Expected: `0/1 passed`; the log shows `FAIL: R picks tool 2` (R does nothing yet), then `RpcError: unknown method editor_transform`.

- [ ] **Step 3: Commit**

```bash
git add pc/tests/editor_transform_smoke.py pc/tests/run.py
git commit -m "$(cat <<'EOF'
editor_transform_smoke: W/E/R, World/Local, R per light type (never intensity), the gizmo with the mouse, prefab and fog scale (fails until the tools exist)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: W/E/R, the world/local switch, R through the scale table, RPC editor_transform

**Files:**
- Modify: `pc/src/lights.cpp` (includes; `gizmo_operation`, line 62; the `extern "C"` snap block, line 669; a new `scale_object` before `pc_lights_viewport`; the gizmo block in `pc_lights_viewport`, lines 1345-1420; a new RPC after `pc_lights_gizmo_operation`, line 1763), `pc/src/lights_internal.h` (line 45), `pc/src/atmosphere.cpp` (line 148; `pc_atmosphere_viewport` lines 1628-1645; `details_extra` lines 1904-1908; new accessors after `rood_atmo::volume_count`, line 1660), `pc/src/environment_internal.h` (`rood_atmo`), `pc/src/dev_overlay.cpp` (externs line 675; toolbar line 1279; `viewport_keys` line 1697; help line 1400), `pc/src/project_settings.cpp` (line 30, line 192), `pc/src/dev_rpc.c` (weak externs line 346; graphics `editor_keys` line 1550; the `editor_select` route line 2395; `edits`, line 3215)

Its test is the smoke written in Task 5.

- [ ] **Step 1: The tool and the space**

In `pc/src/lights.cpp`, change line 62 to:

```cpp
int gizmo_operation; // 0 move (W), 1 rotate (E: aims spot and rect lights, turns actors), 2 scale (R: shape only)
```

In `pc/src/lights_internal.h`, change `extern int gizmo_operation;` to:

```cpp
extern int gizmo_operation; /* 0 move (W), 1 rotate (E), 2 scale (R; editor_transform.h) */
```

In `pc/src/lights.cpp`, the block

```cpp
extern "C" {
float pc_lights_snap_move = 8, pc_lights_snap_angle = 15; /* grid step: Unreal's default size */
int pc_lights_snap; /* grid snapping of moves and rotations (settings editor_snap_grid) */
}
```

becomes

```cpp
extern "C" {
float pc_lights_snap_move = 8, pc_lights_snap_angle = 15; /* grid step: Unreal's default size */
int pc_lights_snap; /* grid snapping of moves and rotations (settings editor_snap_grid) */
/* The move and rotate tools' axes: 0 the world's, 1 the object's own (toolbar
 * World / Local; settings editor_gizmo_space). Scale is always the object's. */
int pc_lights_gizmo_space;
}
```

Add `#include "editor_transform.h"` and `#include "environment_internal.h"` after `#include "editor_math.h"`.

- [ ] **Step 2: The fog volume's size for R**

In `pc/src/environment_internal.h`, namespace `rood_atmo`, after `void remove_volume(int index);` add:

```cpp
bool volume_size(int index, float out[3]); /* its full extents; false out of range */
void set_volume_size(int index, const float size[3]); /* the room unsaved */
```

In `pc/src/atmosphere.cpp`, after `int rood_atmo::volume_count() { return (int)volumes.size(); }` add:

```cpp
bool rood_atmo::volume_size(int index, float out[3])
{
    if (index < 0 || index >= (int)volumes.size())
        return false;
    memcpy(out, volumes[index].size, sizeof volumes[index].size);
    return true;
}
void rood_atmo::set_volume_size(int index, const float size[3])
{
    if (index < 0 || index >= (int)volumes.size())
        return;
    memcpy(volumes[index].size, size, sizeof volumes[index].size);
    pc_lights_mark_dirty();
}
```

- [ ] **Step 3: R through the scale table**

In `pc/src/lights.cpp`, before the comment `/* Draw and edit lights and actors in the scene (called with the overlay`, add:

```cpp
/* R through the scale table (editor_transform.h): from the shape as the drag
 * began (`from_light` / `from_actor`; a fog volume from its size now), one
 * undo step named after the object. */
static void scale_object(const rood_selection::Ref& r, const float ratio[3], const Light& from_light,
    const prefab::Actor& from_actor)
{
    int i = rood_scene::index_of(r);
    if (i < 0)
        return;
    if (r.kind == rood_selection::KIND_LIGHT) {
        rood_transform::scale_light(from_light, ratio, lights[i]);
        pc_undo_hint((std::string("Scale ") + light_type_name(lights[i].type)).c_str());
    } else if (r.kind == rood_selection::KIND_ACTOR) {
        rood_transform::scale_actor(from_actor, ratio, actors[i]);
        pc_undo_hint(("Scale " + rood_scene::label(r)).c_str());
    } else if (r.kind == rood_selection::KIND_VOLUME) {
        float size[3], out[3];
        if (!rood_atmo::volume_size(i, size))
            return;
        rood_transform::scale_volume(size, ratio, out);
        rood_atmo::set_volume_size(i, out);
        pc_undo_hint("Scale fog volume");
    } else {
        return;
    }
    dirty = true;
}
namespace {
Light scale_from_light; /* the shape as a scale drag began */
prefab::Actor scale_from_actor;
/* The light / actor manipulator as last drawn (RPC editor_transform). */
struct GizmoShown {
    int frame = -100;
    std::string subject;
    const char* operation = "";
    const char* mode = "";
} gizmo_shown;
} // namespace
```

- [ ] **Step 4: The gizmo block**

In `pc_lights_viewport`, replace the whole block from `if ((have_light || have_actor) && !drag.active) {` through its closing `} else {\n        was_using = false;\n    }` with:

```cpp
    if ((have_light || have_actor) && !drag.active) {
        float model[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float right[3], second[3], forward[3], *position;
        bool rotate, scale = gizmo_operation == 2;
        if (have_light) {
            Light& l = lights[selected];
            light_frame(l, forward, right, second); /* columns: right, up, forward */
            position = l.position;
            rotate = gizmo_operation == 1 && l.type != 0;
        } else {
            prefab::Actor& a = actors[selected_actor];
            prefab::rotation_basis(a.rotation, right, second, forward); /* right, down, forward */
            position = a.position;
            rotate = gizmo_operation == 1;
        }
        /* Unit columns every frame: a scale drag's columns come back as its ratios. */
        for (int k = 0; k < 3; k++)
            model[k] = right[k], model[4 + k] = second[k], model[8 + k] = forward[k];
        model[12] = position[0], model[13] = position[1], model[14] = position[2];
        float snap[3] = { pc_lights_snap_move, pc_lights_snap_move, pc_lights_snap_move };
        if (rotate)
            snap[0] = snap[1] = snap[2] = pc_lights_snap_angle;
        if (scale)
            snap[0] = snap[1] = snap[2] = 0.1f; /* ratio steps */
        Light before_light = have_light ? lights[selected] : Light();
        prefab::Actor before_actor = have_actor ? actors[selected_actor] : prefab::Actor();
        if (!was_using) /* the shape as a scale drag begins (R maps the ratios onto it) */
            scale_from_light = before_light, scale_from_actor = before_actor;
        bool snapping = (pc_lights_snap != 0) != io.KeyCtrl; /* Ctrl inverts grid snapping while dragging */
        ImGuizmo::OPERATION op = scale ? ImGuizmo::SCALE : rotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;
        /* The toolbar's World / Local for move and rotate; scale always on the object's own axes. */
        ImGuizmo::MODE mode = scale || pc_lights_gizmo_space ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        rood_selection::Ref subject = have_light ? rood_scene::light_ref(selected) : rood_scene::actor_ref(selected_actor);
        gizmo_shown.frame = ImGui::GetFrameCount();
        gizmo_shown.subject = subject.id;
        gizmo_shown.operation = scale ? "scale" : rotate ? "rotate" : "move";
        gizmo_shown.mode = mode == ImGuizmo::LOCAL ? "local" : "world";
        if (ImGuizmo::Manipulate(v.view, v.projection, op, mode, model, nullptr, snapping ? snap : nullptr)) {
            if (scale) {
                float ratio[3];
                for (int col = 0; col < 3; col++)
                    ratio[col] = sqrtf(model[col * 4] * model[col * 4] + model[col * 4 + 1] * model[col * 4 + 1]
                        + model[col * 4 + 2] * model[col * 4 + 2]);
                scale_object(subject, ratio, scale_from_light, scale_from_actor);
            } else {
                position[0] = model[12], position[1] = model[13], position[2] = model[14];
                if (rotate) {
                    float c[3][3];
                    for (int col = 0; col < 3; col++) {
                        float len = sqrtf(model[col * 4] * model[col * 4] + model[col * 4 + 1] * model[col * 4 + 1]
                            + model[col * 4 + 2] * model[col * 4 + 2]);
                        for (int k = 0; k < 3; k++)
                            c[col][k] = model[col * 4 + k] / (len > 1e-6f ? len : 1);
                    }
                    if (have_light)
                        memcpy(lights[selected].direction, c[2], sizeof c[2]);
                    else
                        prefab::rotation_from_basis(c[0], c[1], c[2], actors[selected_actor].rotation);
                }
            }
            dirty = true;
        }
        /* Point, edge and surface snapping take the selection to the target
         * under the cursor, wherever the handle would have gone. */
        pc_snap_result target;
        if (ImGuizmo::IsUsing() && !rotate && !scale && modes
            && pc_overlay_snap(&ov, io.MousePos.x, io.MousePos.y, modes, pick_for_snap, &target)) {
            if (memcmp(position, target.position, sizeof target.position))
                dirty = true;
            memcpy(position, target.position, sizeof target.position);
            pc_overlay_draw_snap(&ov, draw, &target);
        }
        gizmo_busy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        /* Alt+drag (as in UE): a copy stays where the light or actor was
         * (moving or turning it; a scale has no copy). */
        if (ImGuizmo::IsUsing() && !was_using && io.KeyAlt && !scale) {
            pc_undo_hint(have_light ? "Duplicate light (Alt+drag)" : "Duplicate actor (Alt+drag)");
            /* The copy gets its own id (the selection, by id, stays on the
             * one being dragged; editor_scene.cpp assigns the new id). */
            if (have_light) {
                before_light.id.clear();
                lights.push_back(before_light);
            } else {
                before_actor.id.clear();
                before_actor.seed = new_seed();
                actors.push_back(before_actor);
            }
            dirty = true;
        }
        was_using = ImGuizmo::IsUsing();
    } else {
        was_using = false;
    }
```

- [ ] **Step 5: RPC editor_transform**

In `pc/src/lights.cpp`, after `extern "C" int* pc_lights_gizmo_operation(void) { return &gizmo_operation; }` add:

```cpp
/* RPC editor_transform (dev_rpc.c hands over the params object's text):
 * action state (default): the tool (0 move, 1 rotate, 2 scale), the space,
 * and the light / actor manipulator as last drawn (its subject, operation,
 * mode); scale {ratio [x, y, z], id (default the selection's primary)}: R as
 * the gizmo applies it (editor_transform.h), one undo step. */
extern "C" int pc_editor_transform_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_scene::frame();
    const prefab::Json* a = params.get("action");
    std::string action = a && a->type == prefab::Json::String ? a->string : "state";
    if (action == "scale") {
        rood_selection::Ref r = rood_selection::primary();
        if (const prefab::Json* id = params.get("id"))
            r = rood_selection::ref_of(id->string);
        const prefab::Json* ratio = params.get("ratio");
        int i = rood_scene::index_of(r);
        if (i < 0 || r.kind == rood_selection::KIND_GLOW || !ratio || ratio->type != prefab::Json::Array
            || ratio->items.size() != 3) {
            snprintf(out, size, "scale needs ratio [x, y, z] and a light, light actor or fog volume (id or the selection)");
            return -1;
        }
        float k[3] = { (float)ratio->items[0].number, (float)ratio->items[1].number, (float)ratio->items[2].number };
        scale_object(r, k, r.kind == rood_selection::KIND_LIGHT ? lights[i] : Light(),
            r.kind == rood_selection::KIND_ACTOR ? actors[i] : prefab::Actor());
    } else if (action != "state") {
        snprintf(out, size, "action: state or scale");
        return -1;
    }
    bool shown = ImGui::GetCurrentContext() && gizmo_shown.frame >= ImGui::GetFrameCount() - 1;
    std::string s = "{\"operation\":" + std::to_string(gizmo_operation) + ",\"space\":\""
        + (pc_lights_gizmo_space ? "local" : "world") + "\",\"gizmo\":{\"shown\":" + (shown ? "true" : "false")
        + ",\"subject\":" + (shown ? prefab::json_quote(gizmo_shown.subject) : std::string("null")) + ",\"operation\":\""
        + (shown ? gizmo_shown.operation : "") + "\",\"mode\":\"" + (shown ? gizmo_shown.mode : "") + "\"}}";
    snprintf(out, size, "%s", s.c_str());
    return (int)s.size() < size ? 0 : -1;
}
```

- [ ] **Step 6: The fog volume follows the tools**

In `pc/src/atmosphere.cpp`, delete line 148 (`int operation = 0; /* gizmo: 0 move, 1 size, 2 turn */`). In the `extern "C" {` block at line 41, after `int pc_lights_snap_at(...); /* lights.cpp */` add:

```cpp
extern int pc_lights_gizmo_space; /* lights.cpp: the move tool's axes (0 world, 1 local) */
```

In `pc_atmosphere_viewport`, replace

```cpp
    ImGuizmo::OPERATION op = operation == 1 ? ImGuizmo::SCALE : operation == 2 ? ImGuizmo::ROTATE_Y : ImGuizmo::TRANSLATE;
    if (ImGuizmo::Manipulate(view, projection, op, operation == 0 ? ImGuizmo::WORLD : ImGuizmo::LOCAL, model)) {
```

with

```cpp
    /* The editor's tools: W moves, E turns, R sizes (scale semantics: its size per axis). */
    int operation = rood_lights::gizmo_operation;
    ImGuizmo::OPERATION op = operation == 2 ? ImGuizmo::SCALE : operation == 1 ? ImGuizmo::ROTATE_Y : ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mode = operation == 0 && !pc_lights_gizmo_space ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    if (ImGuizmo::Manipulate(view, projection, op, mode, model)) {
```

and in the same block change `if (operation == 1)` (the size line) to `if (operation == 2)`, `if (operation == 2 && sx > 1e-6f)` (the yaw line) to `if (operation == 1 && sx > 1e-6f)`, and the hint to:

```cpp
        pc_undo_hint(operation == 0 ? "Move fog volume" : operation == 2 ? "Size fog volume" : "Turn fog volume");
```

In `rood_atmo::details_extra`, replace the three radio buttons with:

```cpp
    ImGui::RadioButton("Move (W)", &rood_lights::gizmo_operation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Turn (E)", &rood_lights::gizmo_operation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Size (R)", &rood_lights::gizmo_operation, 2);
```

- [ ] **Step 7: Keys, toolbar, help, settings**

In `pc/src/dev_overlay.cpp`: after `extern int pc_lights_snap;` (line 675) add `extern int pc_lights_gizmo_space;`. In `viewport_keys`, after the line `*pc_lights_gizmo_operation() = 1;` add:

```cpp
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) /* Ctrl+R is the Room Browser */
        *pc_lights_gizmo_operation() = 2;
```

In `toolbar`, after

```cpp
    if (toggle_button("Rotate", *op == 1, "Rotate tool (E): aims spot and rect lights"))
        *op = 1;
```

add

```cpp
    flow(button_width("Scale"));
    if (toggle_button("Scale", *op == 2,
            "Scale tool (R): a light's reach (point, spot) or size and reach (rect), a light actor's size (its lights\n"
            "and their radii with it), a fog volume's size. Never brightness."))
        *op = 2;
    flow(button_width("Local"));
    if (toggle_button(pc_lights_gizmo_space ? "Local" : "World", pc_lights_gizmo_space != 0,
            "Move and rotate along the world's axes or the object's own (scale is always the object's)"))
        pc_lights_gizmo_space = !pc_lights_gizmo_space;
```

In `help_window`, after `{ "Tools", "E", "Rotate tool (aim spot and rect lights, turn actors)" },` add:

```cpp
        { "Tools", "R", "Scale tool: a light's reach or size, a light actor's size (its lights and radii with it), a fog volume's size; never brightness" },
        { "Tools", "World / Local (toolbar)", "Move and rotate along the world's axes or the object's own; scale is always the object's" },
```

In `pc/src/project_settings.cpp`, after `extern int pc_lights_snap;` (line 30) add `extern int pc_lights_gizmo_space;`, and after `I("editor_snap_grid", pc_lights_snap),` add:

```cpp
    I("editor_gizmo_space", pc_lights_gizmo_space), /* 0 world, 1 local (move and rotate) */
```

- [ ] **Step 8: dev_rpc.c**

Change line 346's declaration end `pc_snap_surface __attribute__((weak)), pc_lights_snap __attribute__((weak));` to:

```c
    pc_snap_surface __attribute__((weak)), pc_lights_snap __attribute__((weak)), pc_lights_gizmo_space __attribute__((weak));
```

In the graphics `editor_keys` table, after `{ "editor_snap_grid", &pc_lights_snap, NULL },` add:

```c
            { "editor_gizmo_space", &pc_lights_gizmo_space, NULL },
```

Replace the `editor_select` / `editor_schema` route's first four lines

```c
    } else if (!strcmp(method, "editor_select") || !strcmp(method, "editor_schema")) {
        /* The editor's selection (editor_scene.cpp) and property schema
         * (editor_schema.cpp); they parse the params object themselves. */
        extern int pc_editor_select_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_schema_rpc(const char*, char*, int) __attribute__((weak));
        int (*handler)(const char*, char*, int) = !strcmp(method, "editor_select") ? pc_editor_select_rpc : pc_editor_schema_rpc;
```

with

```c
    } else if (!strcmp(method, "editor_select") || !strcmp(method, "editor_schema") || !strcmp(method, "editor_transform")) {
        /* The editor's selection (editor_scene.cpp), property schema
         * (editor_schema.cpp) and transform tools (lights.cpp); they parse the
         * params object themselves. */
        extern int pc_editor_select_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_schema_rpc(const char*, char*, int) __attribute__((weak));
        extern int pc_editor_transform_rpc(const char*, char*, int) __attribute__((weak));
        int (*handler)(const char*, char*, int) = !strcmp(method, "editor_select") ? pc_editor_select_rpc
            : !strcmp(method, "editor_schema")                                     ? pc_editor_schema_rpc
                                                                                   : pc_editor_transform_rpc;
```

In `edits`, change `|| !strcmp(method, "editor_schema"))` to `|| !strcmp(method, "editor_schema") || !strcmp(method, "editor_transform"))`.

- [ ] **Step 9: Build and run the smoke and the editor suites**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor atmosphere
```

Expected: build exit 0; `editor_transform_smoke` passes, and every other editor suite and `atmosphere_smoke` pass (W and E keep their tools; the fog volume's Move/Turn/Size buttons now set the editor's tool). If a transform check fails, use superpowers:systematic-debugging: read `pc/build/tests/editor_transform_smoke/output.log` (each message prints the values), fix `scale_object` or the gizmo block in `pc/src/lights.cpp`, rebuild and rerun.

- [ ] **Step 10: Commit**

```bash
git add pc/src/lights.cpp pc/src/lights_internal.h pc/src/atmosphere.cpp pc/src/environment_internal.h pc/src/dev_overlay.cpp pc/src/project_settings.cpp pc/src/dev_rpc.c
git commit -m "$(cat <<'EOF'
Transform tools: R scales through the scale table (lights, actors, fog volumes), World/Local on the toolbar, RPC editor_transform

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

## Phase C: light handles

### Task 7: View helpers for tests: editor_select project and pick

**Files:**
- Modify: `pc/src/editor_scene.cpp` (includes; `pc_editor_select_rpc`, the action chain before `} else {\n        return rpc_fail("unknown action "`), `pc/tests/editor_selection_smoke.py` (a new section, `SECTIONS`, docstring)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_selection_smoke.py`, add before `# In order: later sections start from where earlier ones leave the room.`:

```python
def check_view(s):
    """project / pick (the tests' view helpers): a light's position projects onto its icon; a pick under Ashley finds a surface by her."""
    o = s.objects()
    light = next(x for x in s.game.call("lights")["lights"] if x["id"] == s.light)
    px = s.select(action="project", point=light["position"])["pixel"]
    s.check(px is not None and distance(px, o[s.light]["screen"]) < 1.0,
            f"project: the light's position is its icon ({px} vs {o[s.light]['screen']})")
    p = s.player
    feet = s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]
    hit = s.select(action="pick", x=feet[0], y=feet[1])["world"] if feet else None
    s.check(hit is not None and math.dist(hit, [p["x"], p["y"], p["z"]]) < 160,
            f"pick under Ashley's feet: a surface by her ({hit})")
```

add `check_view` to the end of `SECTIONS`, and to the docstring's list:

```
- view: project gives a world point's window pixel, pick the surface under a
  pixel (the handles' tests use both).
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `0/1 passed`; the log shows `RpcError: unknown action project`.

- [ ] **Step 3: Implement**

In `pc/src/editor_scene.cpp`, add `#include "editor_math.h"` after `#include "editor_picking.h"`. In `pc_editor_select_rpc`, before `} else {\n        return rpc_fail("unknown action " + action, out, size);` add:

```cpp
    } else if (action == "project") { /* a world point's window pixel (null behind the camera) */
        const Json* p = params.get("point");
        pc_overlay_view v;
        if (!p || p->type != Json::Array || p->items.size() != 3 || !pc_lights_tools_view(&v, nullptr))
            return rpc_fail("project needs point [x, y, z] and an editor view", out, size);
        float w[3] = { (float)p->items[0].number, (float)p->items[1].number, (float)p->items[2].number }, px[2];
        char text[96];
        if (rood_math::project(v.view, v.projection, v.rect, w, px))
            snprintf(text, sizeof text, ",\"pixel\":[%.2f,%.2f]", px[0], px[1]);
        else
            snprintf(text, sizeof text, ",\"pixel\":null");
        extra += text;
    } else if (action == "pick") { /* the surface under a window pixel {x, y | u, v} */
        float x, y, w[3], n[3];
        pc_overlay_view v;
        pc_overlay_pick pick = nullptr;
        if (!cursor_of(params, &x, &y) || !pc_lights_tools_view(&v, &pick) || !pick)
            return rpc_fail("pick needs an editor view", out, size);
        char text[160];
        if (pick(x, y, w, n))
            snprintf(text, sizeof text, ",\"world\":[%.2f,%.2f,%.2f],\"normal\":[%.4f,%.4f,%.4f]", w[0], w[1], w[2], n[0],
                n[1], n[2]);
        else
            snprintf(text, sizeof text, ",\"world\":null,\"normal\":null");
        extra += text;
```

Update the RPC's comment block: after `copy (replies the clipboard), paste {text}, duplicate, delete;` add ` project {point} (replies pixel), pick {x, y | u, v} (replies world, normal);`.

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_selection
```

Expected: `1/1 passed`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_scene.cpp pc/tests/editor_selection_smoke.py
git commit -m "$(cat <<'EOF'
editor_select project / pick: a world point's pixel and the surface under a pixel (tests' view helpers)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 8: Light fields: the spot's inner cone and the rect's barn doors

**Files:**
- Modify: `pc/src/lights_internal.h` (`Light`, lines 23-24), `pc/src/lighting_layers.cpp` (light `KindSpec`, line 83; `item_from_light`, `light_from_item`), `pc/tools/lighting_format.py` (light fields, line 69), `pc/src/editor_undo.cpp` (`same_light`, line 140), `pc/src/editor_schema.cpp` (`light_kind`: after the cone and height fields), `pc/src/light_prefabs.h` (`EvalLight` line 221, `LightTemplate`), `pc/src/light_prefabs.cpp` (template parse line 865; `evaluate`), `pc/src/lights.cpp` (`from_eval`), `pc/src/light_actors.cpp` (Create prefab line 333; Break into lights line 1040), `pc/assets/prefabs/README.md` (light fields table)
- Test: `pc/tests/lighting_probe.cpp`, `pc/tests/prefab_probe.cpp`, `pc/tests/editor_schema_smoke.py`

- [ ] **Step 1: Write the failing checks**

In `pc/tests/lighting_probe.cpp` `selftest()`, after the check `"defaults are left out of items"` add:

```cpp
    rood_lights::Light shaped;
    shaped.id = "fedcba98765432";
    shaped.type = 2;
    shaped.inner = 20, shaped.barn_angle = 30, shaped.barn_length = 48;
    Item si = item_from_light(shaped);
    rood_lights::Light sb;
    check(light_from_item(si, sb) && sb.inner == 20 && sb.barn_angle == 30 && sb.barn_length == 48 && si.get("barn_length")
            && !item_from_light(rood_lights::Light()).get("barn_angle"),
        "the inner cone and barn doors round-trip; at their defaults they are left out");
```

In `pc/tests/prefab_probe.cpp`, after the scale check added in Task 4, add:

```cpp
    /* A prefab's lights may have an inner cone and barn doors (the doors'
     * length scales with the actor, the angles do not). */
    Json shaped_json;
    check(json_parse("{\"name\": \"Shaped\", \"lights\": [{\"type\": \"rect\", \"barn_angle\": 30, \"barn_length\": 40},"
                     " {\"type\": \"spot\", \"inner\": 25}]}",
              shaped_json),
        "a prefab with barn doors and an inner cone parses");
    Prefab shaped = prefab_from_json("shaped", shaped_json);
    Actor sa;
    sa.prefab = "shaped";
    sa.scale = 2;
    std::vector<EvalLight> se;
    evaluate(shaped, sa, 0, false, se);
    check(shaped.error.empty() && se.size() == 2 && near(se[0].barn_angle, 30) && near(se[0].barn_length, 80)
            && near(se[1].inner, 25),
        "its lights carry them");
```

In `pc/tests/editor_schema_smoke.py` `check_lights`, after the check ending `"spot fields round-trip")` add:

```python
    schema(s, action="set", id=s.light, values={"inner": 20})
    s.check(close(value(s, s.light, "inner"), 20), "a spot's inner cone round-trips")
```

and after the check ending `"rect fields round-trip")` add:

```python
    schema(s, action="set", id=s.light, values={"barn_angle": 30, "barn_length": 48})
    s.check(close(value(s, s.light, "barn_angle"), 30) and close(value(s, s.light, "barn_length"), 48),
            "a rect's barn doors round-trip")
    schema(s, action="reset", id=s.light, fields=["barn_angle", "barn_length"])
```

- [ ] **Step 2: Run the probes and see them fail**

```bash
ninja -C pc/build prefab_probe lighting_probe
```

Expected: compile errors, `'struct rood_lights::Light' has no member named 'inner'` and `'struct prefab::EvalLight' has no member named 'barn_angle'`.

- [ ] **Step 3: The fields**

In `pc/src/lights_internal.h`, `struct Light`: after `float cone = 45;` add

```cpp
    float inner = 0; // spot: full brightness inside this full angle, fading to the cone (0: the outer quarter fades)
```

and after `float size[2] = { 128, 128 }; // rect: width, height (world units)` add

```cpp
    float barn_angle = 45; // rect: its barn doors' angle from its facing (0 straight ahead .. 90 folded flat)
    float barn_length = 0; // rect: how far its doors reach out from each edge (world units; 0: none)
```

In `pc/src/light_prefabs.h`, `struct EvalLight`: after `float intensity = 1, radius = 600, cone = 45, size[2] = { 128, 128 };` add

```cpp
    float inner = 0, barn_angle = 45, barn_length = 0; // spot inner cone; rect barn doors (lights_internal.h Light)
```

and in `struct LightTemplate` change `ExprPtr offset, direction, color, intensity, radius, cone, size, shadows, light_size, enabled;` to

```cpp
    ExprPtr offset, direction, color, intensity, radius, cone, inner, size, barn_angle, barn_length, shadows, light_size, enabled;
```

In `pc/src/light_prefabs.cpp`, the template parse: after `l.cone = field(j, "cone", where);` add `l.inner = field(j, "inner", where);`, and after `l.size = field(j, "size", where);` add

```cpp
            l.barn_angle = field(j, "barn_angle", where);
            l.barn_length = field(j, "barn_length", where);
```

In `evaluate`, after `l.cone = expr_eval(t.cone, lookup, Value::scalar(45)).x();` add

```cpp
        l.inner = expr_eval(t.inner, lookup, Value::scalar(0)).x();
```

and after the `l.size[0] = ..., l.size[1] = ...;` line add

```cpp
        l.barn_angle = expr_eval(t.barn_angle, lookup, Value::scalar(45)).x();
        l.barn_length = fmaxf(expr_eval(t.barn_length, lookup, Value::scalar(0)).x() * s, 0);
```

- [ ] **Step 4: Store them**

In `pc/src/lighting_layers.cpp`, the `"light"` KindSpec, replace

```cpp
                { "radius", 1, "600", false }, { "cone", 2, "45", false }, { "size", 1, "128,128", false },
                { "shadows", 0, "0", false }, { "enabled", 0, "1", false } },
```

with

```cpp
                { "radius", 1, "600", false }, { "cone", 2, "45", false }, { "inner", 2, "0", false },
                { "size", 1, "128,128", false }, { "barn_angle", 2, "45", false }, { "barn_length", 1, "0", false },
                { "shadows", 0, "0", false }, { "enabled", 0, "1", false } },
```

In `item_from_light`, after `it.set("cone", format_number(l.cone, 2));` add `it.set("inner", format_number(l.inner, 2));`, and after `it.set("size", vec(l.size, 2, 1));` add

```cpp
    it.set("barn_angle", format_number(l.barn_angle, 2));
    it.set("barn_length", format_number(l.barn_length, 1));
```

In `light_from_item`, after `l.cone = (float)get_num(it, "cone", 45);` add `l.inner = (float)get_num(it, "inner", 0);`, and after `get_vec(it, "size", l.size, 2);` add

```cpp
    l.barn_angle = (float)get_num(it, "barn_angle", 45);
    l.barn_length = (float)get_num(it, "barn_length", 0);
```

In `pc/tools/lighting_format.py`, the `"light"` entry, replace

```python
        ("radius", 1, "600", False), ("cone", 2, "45", False), ("size", 1, "128,128", False),
        ("shadows", 0, "0", False), ("enabled", 0, "1", False)]),
```

with

```python
        ("radius", 1, "600", False), ("cone", 2, "45", False), ("inner", 2, "0", False),
        ("size", 1, "128,128", False), ("barn_angle", 2, "45", False), ("barn_length", 1, "0", False),
        ("shadows", 0, "0", False), ("enabled", 0, "1", False)]),
```

- [ ] **Step 5: Run the probes and see them pass**

```bash
ninja -C pc/build prefab_probe lighting_probe && ctest --test-dir pc/build -R 'prefab_probe|lighting_layers' --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 6: Undo, schema, the lights the renderers get, prefabs**

In `pc/src/editor_undo.cpp` `same_light`, change `&& a.cone == b.cone && !memcmp(a.size, b.size, sizeof a.size)` to

```cpp
        && a.cone == b.cone && a.inner == b.inner && !memcmp(a.size, b.size, sizeof a.size) && a.barn_angle == b.barn_angle
        && a.barn_length == b.barn_length
```

In `pc/src/editor_schema.cpp` `light_kind`, inside `if (type == 1) {` after the cone field's `k.fields.push_back(f);` add

```cpp
        f = angle_field("inner", "Inner cone", "Shape", 0, 170, 0.5f, d.inner,
            "Full brightness inside this angle, fading out to the cone's edge; 0: the outer quarter fades");
        f.get = [](int i) { return Value::scalar(lights[i].inner); };
        f.set = [](int i, const Value& v) { lights[i].inner = v.x(); };
        k.fields.push_back(f);
```

and inside `if (type == 2) {` after the height field's `k.fields.push_back(f);` add

```cpp
        f = angle_field("barn_angle", "Barn door angle", "Barn doors", 0, 90, 0.5f, d.barn_angle,
            "Its four doors' angle from its facing: 0 straight ahead (the tightest beam), 90 folded flat (no effect)");
        f.get = [](int i) { return Value::scalar(lights[i].barn_angle); };
        f.set = [](int i, const Value& v) { lights[i].barn_angle = v.x(); };
        k.fields.push_back(f);
        f = number_field("barn_length", "Barn door length", "Barn doors", 0, 1024, 1, d.barn_length, "u", false,
            "How far the doors reach out from each edge; 0: no doors");
        f.get = [](int i) { return Value::scalar(lights[i].barn_length); };
        f.set = [](int i, const Value& v) { lights[i].barn_length = v.x(); };
        k.fields.push_back(f);
```

In `pc/src/lights.cpp` `from_eval`, after `l.cone = e.cone;` add `l.inner = e.inner;`, and after `l.size[0] = e.size[0], l.size[1] = e.size[1];` add `l.barn_angle = e.barn_angle, l.barn_length = e.barn_length;`.

In `pc/src/light_actors.cpp`, Break into lights: after `l.cone = e.cone;` add `l.inner = e.inner;` and after `l.size[0] = e.size[0], l.size[1] = e.size[1];` add `l.barn_angle = e.barn_angle, l.barn_length = e.barn_length;`. In Create prefab (the template JSON), replace

```cpp
        if (l.type == 1)
            j.set("cone", Json::num(l.cone));
```

with

```cpp
        if (l.type == 1)
            j.set("cone", Json::num(l.cone));
        if (l.type == 1 && l.inner > 0)
            j.set("inner", Json::num(l.inner));
        if (l.type == 2 && l.barn_length > 0) {
            j.set("barn_angle", Json::num(l.barn_angle));
            j.set("barn_length", Json::num(l.barn_length));
        }
```

In `pc/assets/prefabs/README.md`, the lights table: after the `cone` row add

```
| `inner` | 0 | spot inner cone, degrees: full brightness inside it (0: the outer quarter of the cone fades) |
```

and after the `size` row add

```
| `barn_angle`, `barn_length` | 45, 0 | rect barn doors: angle from its facing (0 straight ahead, 90 folded flat) and length (0: none; scales with the actor) |
```

- [ ] **Step 7: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_schema editor_undo light_actors lighting
```

Expected: build exit 0; every suite passes (the lint takes the three fields; the spot and rect round-trips pass).

- [ ] **Step 8: Commit**

```bash
git add pc/src/lights_internal.h pc/src/lighting_layers.cpp pc/tools/lighting_format.py pc/src/editor_undo.cpp pc/src/editor_schema.cpp pc/src/light_prefabs.h pc/src/light_prefabs.cpp pc/src/lights.cpp pc/src/light_actors.cpp pc/assets/prefabs/README.md pc/tests/lighting_probe.cpp pc/tests/prefab_probe.cpp pc/tests/editor_schema_smoke.py
git commit -m "$(cat <<'EOF'
Light fields: spot inner cone, rect barn doors (angle, length) in the lighting items, schema, undo and prefabs

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Light shapes: the packing and the shared formula

**Files:**
- Create: `pc/src/light_shape.h`
- Modify: `pc/src/lights.cpp` (`pc_lights_world`, the packing at line 1685), `pc/src/light_list.h` (the `pc_light_gpu` comments), `pc/tests/editor_tools_probe.cpp`

- [ ] **Step 1: Write the failing checks**

In `pc/tests/editor_tools_probe.cpp`, add `#include "light_shape.h"` after `#include "editor_transform.h"`, insert before `int main()`:

```cpp
/* The renderers' spot edge and barn doors (light_shape.h). */
static void light_shapes()
{
    float co = cosf(30 * 3.14159265f / 180);
    check(pc_spot_cos_inner(60, 0) == fmaxf(co + (1.0f - co) * 0.25f, co + 1e-4f), "inner 0: the old soft edge exactly");
    check(near(pc_spot_cos_inner(60, 20), cosf(10 * 3.14159265f / 180), 1e-5f), "inner 20: cos 10 degrees");
    check(pc_spot_cos_inner(60, 80) > co, "an inner cone wider than the cone stays inside it");
    float a, b;
    pc_barn_pack(90, 50, &a, &b);
    check(near(a, 0, 1e-4f) && near(b, 50), "doors folded flat (90): no reach forward");
    pc_barn_pack(0, 50, &a, &b);
    check(near(a, 50) && near(b, 0, 1e-4f), "doors straight ahead (0)");
    const float c[3] = { 0, 0, 0 }, r[3] = { 32, 0, 0 }, u[3] = { 0, 0, 32 }, f[3] = { 0, 1, 0 };
    const float under[3] = { 0, 96, 0 }, side[3] = { 128, 96, 0 }, part[3] = { 16, 96, 0 };
    check(near(pc_barn_doors(c, r, u, f, 96, 0, under), 1), "straight under the light: all of it");
    check(near(pc_barn_doors(c, r, u, f, 96, 0, side), 0), "far to the side: the doors hide it all");
    check(near(pc_barn_doors(c, r, u, f, 96, 0, part), 0.75f), "a little to the side: a quarter hidden");
    check(near(pc_barn_doors(c, r, u, f, 0, 50, side), 1), "doors folded flat hide nothing");
}
```

and in `main`, after `scale_table();` add `light_shapes();`.

- [ ] **Step 2: Run it and see it fail**

```bash
ninja -C pc/build editor_tools_probe
```

Expected: `fatal error: light_shape.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `pc/src/light_shape.h`:

```c
/* A spot's edge and a rect's barn doors as every renderer evaluates them
 * (pc/docs/Vulkan_Renderer.md "Many lights"; lighting/README.md light
 * fields), and how lights.cpp packs them into slots the lights already have
 * free (light_list.h):
 *   spot: extent_right.x = cos(inner half angle) (direction.w: cos(cone / 2));
 *   rect: direction.w = barn length x cos(barn angle),
 *         extent_right.w = barn length x sin(barn angle) (0 0: no doors).
 * The doors: per axis of the rect, a point p in front sees the part of its
 * width past the two doors on that axis; the factor is the product of both
 * axes' shares (Unreal's barn doors: four flaps of one angle and length).
 * PC_LIGHT_SHAPE_GLSL is the OpenGL shaders' copy of
 * pc/shaders/vulkan/light_shape.glsl (keep the two the same). Plain C
 * (pc/tests/editor_tools_probe.cpp). */
#ifndef ROOD_LIGHT_SHAPE_H
#define ROOD_LIGHT_SHAPE_H
#include <math.h>

/* cos of a spot's inner half angle: its `inner` (full angle, degrees), or
 * with 0 the old soft edge (the outer quarter of the cosine range fades);
 * always above cos(cone / 2) (smoothstep needs its edges in order). */
static inline float pc_spot_cos_inner(float cone, float inner)
{
    float co = cosf(cone * 0.5f * 3.14159265f / 180.0f);
    float ci = inner > 0 ? cosf(fminf(inner, cone) * 0.5f * 3.14159265f / 180.0f) : co + (1.0f - co) * 0.25f;
    return fmaxf(ci, co + 1e-4f);
}
/* A rect's barn doors as two numbers: a = length x cos(angle), b = length x
 * sin(angle) (angle from its facing, 0..90 degrees). */
static inline void pc_barn_pack(float angle, float length, float* a, float* b)
{
    float t = fminf(fmaxf(angle, 0.0f), 90.0f) * 3.14159265f / 180.0f, l = fmaxf(length, 0.0f);
    *a = l * cosf(t);
    *b = l * sinf(t);
}
/* The share of a rect light (centre, half extents right / up, unit facing
 * dir, doors a / b) a point p in front of it sees: the shaders' barn_doors. */
static inline float pc_barn_doors(const float centre[3], const float right[3], const float up[3], const float dir[3],
    float a, float b, const float p[3])
{
    if (a <= 0)
        return 1;
    float v[3] = { p[0] - centre[0], p[1] - centre[1], p[2] - centre[2] };
    float z = fmaxf(v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2], 1e-3f);
    float w = fmaxf(sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]), 1e-3f);
    float h = fmaxf(sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]), 1e-3f);
    float tx = (v[0] * right[0] + v[1] * right[1] + v[2] * right[2]) / (w * z);
    float ty = (v[0] * up[0] + v[1] * up[1] + v[2] * up[2]) / (h * z);
    float fx = 1 - (fmaxf(a * tx - b, 0) + fmaxf(-a * tx - b, 0)) / (2 * w);
    float fy = 1 - (fmaxf(a * ty - b, 0) + fmaxf(-a * ty - b, 0)) / (2 * h);
    return fminf(fmaxf(fx, 0), 1) * fminf(fmaxf(fy, 0), 1);
}

/* light_shape(type, centre, dir, right, up, p, l): the factor on a light's
 * falloff at p (l: the unit direction toward the light): a spot's edge, a
 * rect's front face times its barn doors; 1 for a point light. */
#define PC_LIGHT_SHAPE_GLSL                                                                                            \
    "float barn_doors(vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p) {\n"                                        \
    "    if (dir.w <= 0.0) return 1.0;\n"                                                                              \
    "    vec3 v = p - centre;\n"                                                                                       \
    "    float z = max(dot(v, dir.xyz), 1e-3), w = max(length(right.xyz), 1e-3), h = max(length(up.xyz), 1e-3);\n"   \
    "    float tx = dot(v, right.xyz) / (w * z), ty = dot(v, up.xyz) / (h * z);\n"                                    \
    "    float fx = clamp(1.0 - (max(dir.w * tx - right.w, 0.0) + max(-dir.w * tx - right.w, 0.0)) / (2.0 * w), 0.0, 1.0);\n" \
    "    float fy = clamp(1.0 - (max(dir.w * ty - right.w, 0.0) + max(-dir.w * ty - right.w, 0.0)) / (2.0 * h), 0.0, 1.0);\n" \
    "    return fx * fy;\n"                                                                                            \
    "}\n"                                                                                                               \
    "float light_shape(float type, vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p, vec3 l) {\n"                   \
    "    float c = dot(-l, dir.xyz);\n"                                                                                \
    "    if (type > 1.5) return max(c, 0.0) * barn_doors(centre, dir, right, up, p);\n"                               \
    "    if (type > 0.5) return smoothstep(dir.w, right.x, c);\n"                                                      \
    "    return 1.0;\n"                                                                                                \
    "}\n"
#endif
```

- [ ] **Step 4: Run it and see it pass**

```bash
ninja -C pc/build editor_tools_probe && pc/build/editor_tools_probe
```

Expected: every line `ok:`, last line `PASS: editor tools maths`.

- [ ] **Step 5: Pack the lights**

In `pc/src/lights.cpp`, add `#include "light_shape.h"` after `#include "light_list.h"`. In `pc_lights_world`, after

```cpp
        extent_right[n][3] = l.type != 2 ? c.light_size : 0;
        extent_up[n][3] = c.shadow_skip;
```

add

```cpp
        if (l.type == 1) /* the spot's inner cone (light_shape.h; its right axis is unused) */
            extent_right[n][0] = pc_spot_cos_inner(l.cone, l.inner);
        if (l.type == 2) /* the rect's barn doors (light_shape.h; its cone slot and own size are unused) */
            pc_barn_pack(l.barn_angle, l.barn_length, &direction[n][3], &extent_right[n][3]);
```

In `pc/src/light_list.h`, change the `right`, `up` and `dir` comments in `pc_light_gpu` to:

```c
    float right[4], up[4]; /* rect half extents; right.w: own emitting radius (rect: barn length x sin angle);
                            * up.w: shadow skip; spot: right.x cos(inner half angle) (light_shape.h) */
    float dir[4]; /* direction, cos(half cone) (rect: barn length x cos angle) */
```

- [ ] **Step 6: Build and check nothing changed on screen yet**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 light_actors lighting fixture glow
```

Expected: build exit 0; every suite passes (the shaders do not read the new slots yet; a rect's `right.w` is still 0 without doors).

- [ ] **Step 7: Commit**

```bash
git add pc/src/light_shape.h pc/src/lights.cpp pc/src/light_list.h pc/tests/editor_tools_probe.cpp
git commit -m "$(cat <<'EOF'
Light shapes: spot inner cone and barn doors packed into the lights' free slots (light_shape.h, probe)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: The barn doors smoke (pixels; fails until the shaders read the shapes)

**Files:**
- Create: `pc/tests/barn_doors_smoke.py`
- Modify: `pc/tests/run.py` (after the `editor_transform_smoke` Suite; `CHECKPOINT_SUITES`)

- [ ] **Step 1: Write the test**

Create `pc/tests/barn_doors_smoke.py`:

```python
#!/usr/bin/env python3
"""Barn doors and a spot's inner cone in the renderers (light_shape.h,
pc/shaders/vulkan/light_shape.glsl), measured in the presented picture.
Run it under OpenGL, and under Vulkan with run.py --arch 64 --renderer vulkan.

    python3 pc/tests/barn_doors_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

In the card's save room (scratch ROOD_LIGHTS, the editor closed, the
Lighting only view, the animation frozen), a rect light hangs 96 units above
the floor beside Ashley, facing down. Frames: the light off, on without
doors, on with 96-unit doors pointing straight down. Under the light the
doors change little; 128 units to the side they leave at most a quarter of
its light. Then the light becomes a spot (a 60 degree cone, 128 above the
floor): 20 degrees off its axis an inner cone of 20 degrees leaves less light
than the automatic edge, and on its axis the same.
"""
import argparse
import math
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import golden_frames as gf  # noqa: E402
from editor_selection_smoke import pinned_card  # noqa: E402

RECT_HEIGHT = 96  # the rect light above the floor
SIDE = 128  # the floor point beside it that its doors hide
SPOT_HEIGHT = 128
OFF_AXIS = SPOT_HEIGHT * math.tan(math.radians(20))  # 20 degrees off the spot's axis


def lum(image, xy, r=2):
    """Mean luminance (0-255) of the (2r+1)^2 pixels around xy."""
    x, y = int(round(xy[0])), int(round(xy[1]))
    box = image[y - r:y + r + 1, x - r:x + r + 1].astype(float)
    return float((box[..., 0] * 0.3 + box[..., 1] * 0.59 + box[..., 2] * 0.11).mean())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "barn_doors"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    settings.write_text("# barn_doors: scratch settings\naa fxaa\n")
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_SETTINGS": str(settings), "ROOD_SCALE": "2"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-barn-{os.getpid()}.sock"  # sun_path is short
    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def schema(**params):
        return g.call("editor_schema", **params)

    def floor_pixel(point):
        """The window pixel of a floor point, when the floor (nothing in front of it) is there."""
        xy = g.call("editor_select", action="project", point=point)["pixel"]
        if xy is None:
            return None
        hit = g.call("editor_select", action="pick", x=xy[0], y=xy[1])["world"]
        return xy if hit and math.dist(hit, point) < 10 else None

    try:
        g.call("graphics", **{**gf.DEFAULTS, "scale": 2, "view_mode": 2, "gi": False})
        g.call("actors", time=0)
        g.run(gf.SETTLE)
        p = g.state()["player"]
        found = None
        for dx, dz in ((240, 0), (-240, 0), (0, 240), (0, -240), (320, 160), (-320, -160)):
            c = [p["x"] + dx, p["y"], p["z"] + dz]
            points = [c, [c[0] + SIDE, c[1], c[2]], [c[0] + OFF_AXIS, c[1], c[2]]]
            pixels = [floor_pixel(q) for q in points]
            if all(pixels):
                found = (c, pixels)
                break
        if not check(found is not None, "a floor spot beside Ashley with the points under and beside it in view"):
            return 1
        c, (at_c, at_side, at_q) = found
        light = g.call("lights", action="add", position=[c[0], c[1] - RECT_HEIGHT, c[2]])["lights"][-1]["id"]
        schema(action="set", id=light, values={"type": "rect"})
        schema(action="set", id=light, values={"direction": [0, 1, 0], "width": 64, "height": 64, "radius": 800,
                                               "intensity": 1, "color": [1, 1, 1], "enabled": False})
        off = gf.settled(g, out / "off")
        schema(action="set", id=light, values={"enabled": True})
        lit = gf.settled(g, out / "rect-open")
        schema(action="set", id=light, values={"barn_length": 96, "barn_angle": 0})
        doors = gf.settled(g, out / "rect-doors")
        lit_c, lit_s = lum(lit, at_c) - lum(off, at_c), lum(lit, at_side) - lum(off, at_side)
        door_c, door_s = lum(doors, at_c) - lum(off, at_c), lum(doors, at_side) - lum(off, at_side)
        check(lit_c > 12 and lit_s > 6, f"the rect light lights the floor under it and beside it ({lit_c:.1f}, {lit_s:.1f})")
        check(door_c >= 0.85 * lit_c, f"under it the doors change little ({door_c:.1f} of {lit_c:.1f})")
        check(door_s <= 0.25 * lit_s, f"beside it the doors cut the light ({door_s:.1f} of {lit_s:.1f})")
        schema(action="set", id=light, values={"type": "spot"})
        schema(action="set", id=light, values={"position": [c[0], c[1] - SPOT_HEIGHT, c[2]], "direction": [0, 1, 0],
                                               "cone": 60, "inner": 0, "radius": 900, "intensity": 0.6})
        auto = gf.settled(g, out / "spot-auto")
        schema(action="set", id=light, values={"inner": 20})
        narrow = gf.settled(g, out / "spot-inner20")
        a_q, n_q = lum(auto, at_q) - lum(off, at_q), lum(narrow, at_q) - lum(off, at_q)
        a_c, n_c = lum(auto, at_c) - lum(off, at_c), lum(narrow, at_c) - lum(off, at_c)
        check(a_q > 6 and a_q > 1.25 * n_q,
              f"20 degrees off the axis an inner cone of 20 leaves less light than the automatic edge ({n_q:.1f} vs {a_q:.1f})")
        check(abs(a_c - n_c) <= 0.1 * max(a_c, 1), f"on the axis both are the same ({n_c:.1f} vs {a_c:.1f})")
    finally:
        g.close()
        shutil.rmtree(lights, ignore_errors=True)
    log = pathlib.Path(g.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", g.log)
        return 1
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: barn doors cut a rect light's spill; a spot's inner cone sets its edge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `editor_transform_smoke` Suite add:

```python
            Suite("barn_doors_smoke", [PY, str(TESTS / "barn_doors_smoke.py"), "--card", str(args.save)], 300,
                  note="light shapes in the picture: a rect's barn doors cut its spill beside it, a spot's inner cone "
                       "sets its edge (OpenGL; Vulkan with --arch 64 --renderer vulkan)"),
```

and in `CHECKPOINT_SUITES` change `"editor_transform_smoke", "facing_cutaway_smoke",` to `"editor_transform_smoke", "barn_doors_smoke", "facing_cutaway_smoke",`.

- [ ] **Step 2: Run it and see it fail on both renderers**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 barn_doors
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan barn_doors
```

Expected: `0/1 passed` each; the logs show `FAIL: beside it the doors cut the light (...)` and `FAIL: 20 degrees off the axis ...` (the renderers do not read the shapes yet), while the first check passes.

- [ ] **Step 3: Commit**

```bash
git add pc/tests/barn_doors_smoke.py pc/tests/run.py
git commit -m "$(cat <<'EOF'
barn_doors_smoke: pixel measures of a rect's barn doors and a spot's inner cone (fails until the shaders read them)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Light shapes in the Vulkan shaders

**Files:**
- Create: `pc/shaders/vulkan/light_shape.glsl`
- Modify: `pc/shaders/vulkan/rt_lights.glsl` (after `#include "environment.glsl"`, line 30; `light_falloff`, line 177), `pc/shaders/vulkan/rt_haze.comp` (`source_light`, line 81), `pc/shaders/vulkan/probe_trace.glsl` (`probe_direct`, line 61), `pc/shaders/vulkan/scaled.frag` (after `#include "medium.glsl"`, line 59; `low_haze`, line 280; the lights loop, lines 459-468)

- [ ] **Step 1: The shared functions**

Create `pc/shaders/vulkan/light_shape.glsl`:

```glsl
// A spot's edge and a rect's barn doors (lights.cpp packs them; light_shape.h
// documents the slots and holds the OpenGL shaders' copy of these two
// functions; keep them the same).
// type (col.w): 1 spot: dir.w cos(cone / 2), right.x cos(inner / 2);
//               2 rect: dir.w and right.w its barn doors (length x cos / sin of their angle).
// The doors: per axis of the rect, the share of its width a point p in front
// sees past the two doors on that axis; the factor is both axes' product.
float barn_doors(vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p) {
    if (dir.w <= 0.0) return 1.0;
    vec3 v = p - centre;
    float z = max(dot(v, dir.xyz), 1e-3), w = max(length(right.xyz), 1e-3), h = max(length(up.xyz), 1e-3);
    float tx = dot(v, right.xyz) / (w * z), ty = dot(v, up.xyz) / (h * z);
    float fx = clamp(1.0 - (max(dir.w * tx - right.w, 0.0) + max(-dir.w * tx - right.w, 0.0)) / (2.0 * w), 0.0, 1.0);
    float fy = clamp(1.0 - (max(dir.w * ty - right.w, 0.0) + max(-dir.w * ty - right.w, 0.0)) / (2.0 * h), 0.0, 1.0);
    return fx * fy;
}
// The factor on a light's falloff at p (l: unit direction toward the light,
// centre: the light's centre): a spot's edge, a rect's front face times its
// doors, 1 for a point light.
float light_shape(float type, vec3 centre, vec4 dir, vec4 right, vec4 up, vec3 p, vec3 l) {
    float c = dot(-l, dir.xyz);
    if (type > 1.5) return max(c, 0.0) * barn_doors(centre, dir, right, up, p);
    if (type > 0.5) return smoothstep(dir.w, right.x, c);
    return 1.0;
}
```

- [ ] **Step 2: rt_lights.glsl (the trace, GI bounces, probes)**

After `#include "environment.glsl"` add `#include "light_shape.glsl"`. In `light_falloff`, replace

```glsl
    if (lcol(i).w > 1.5) a *= max(dot(-l, ldir(i).xyz), 0.0);
    else if (lcol(i).w > 0.5) a *= smoothstep(ldir(i).w, mix(ldir(i).w, 1.0, 0.25), dot(-l, ldir(i).xyz));
    return a;
```

with

```glsl
    a *= light_shape(lcol(i).w, lpos(i).xyz, ldir(i), lright(i), lup(i), p, l); // spot edge, rect face and barn doors
    return a;
```

- [ ] **Step 3: rt_haze.comp and probe_trace.glsl**

In `rt_haze.comp` `source_light`, replace

```glsl
    if (i < light_count && lcol(i).w > 1.5) a *= max(dot(-l, ldir(i).xyz), 0.0);
    else if (i < light_count && lcol(i).w > 0.5) a *= smoothstep(ldir(i).w, mix(ldir(i).w, 1.0, 0.25), dot(-l, ldir(i).xyz));
```

with

```glsl
    if (i < light_count) a *= light_shape(lcol(i).w, lpos(i).xyz, ldir(i), lright(i), lup(i), x, l);
```

In `probe_trace.glsl` `probe_direct`, replace

```glsl
        if (light_col[i].w > 1.5) a *= max(dot(-l, light_dir[i].xyz), 0.0);
        else if (light_col[i].w > 0.5) a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));
        if (a <= 0.0) continue;
```

with

```glsl
        a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], p, l);
        if (a <= 0.0) continue;
```

- [ ] **Step 4: scaled.frag (the present: direct light, low haze, shadow maps)**

After `#include "medium.glsl"` add `#include "light_shape.glsl"`. In `low_haze`, replace

```glsl
            if (lights[i].col.w > 1.5) a *= max(dot(-l, lights[i].dir.xyz), 0.0);
            else if (lights[i].col.w > 0.5) a *= smoothstep(lights[i].dir.w, mix(lights[i].dir.w, 1.0, 0.25), dot(-l, lights[i].dir.xyz));
```

with

```glsl
            a *= light_shape(lights[i].col.w, lights[i].pos.xyz, lights[i].dir, lights[i].right, lights[i].up, x, l);
```

In the lights loop, replace

```glsl
                if (lights[i].col.w > 1.5) // rect: emits from its front face
                    a *= max(dot(-l, lights[i].dir.xyz), 0.0);
                else if (lights[i].col.w > 0.5) // spot: smooth cone edge
                    a *= smoothstep(lights[i].dir.w, mix(lights[i].dir.w, 1.0, 0.25), dot(-l, lights[i].dir.xyz));
```

with

```glsl
                // a spot's edge, a rect's front face and barn doors (light_shape.glsl)
                a *= light_shape(lights[i].col.w, lights[i].pos.xyz, lights[i].dir, lights[i].right, lights[i].up, world, l);
```

and in the shadow-map branch below it change `lights[i].right.w > 0.0 ? lights[i].right.w : u.quality2.x,` to

```glsl
                        lights[i].col.w < 1.5 && lights[i].right.w > 0.0 ? lights[i].right.w : u.quality2.x,
```

(a rect's `right.w` is its doors now, not an emitting radius; shadow maps themselves are unchanged: the doors are a factor on the falloff, before any shadow test.)

- [ ] **Step 5: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan barn_doors lighting light_actors fog gi glow
```

Expected: the build compiles every shader (no `glslang` error) and exits 0; `barn_doors_smoke` passes on Vulkan, and the lighting, actor, fog, GI and glow suites stay green (with `inner` 0 and no doors the picture is as before).

- [ ] **Step 6: Commit**

```bash
git add pc/shaders/vulkan/light_shape.glsl pc/shaders/vulkan/rt_lights.glsl pc/shaders/vulkan/rt_haze.comp pc/shaders/vulkan/probe_trace.glsl pc/shaders/vulkan/scaled.frag
git commit -m "$(cat <<'EOF'
Vulkan: light shapes (spot inner cone, rect barn doors) in the present, trace, haze and probes (light_shape.glsl)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 12: Light shapes in the OpenGL shaders

**Files:**
- Modify: `pc/src/ptgi.c` (includes; `trace_source` before `vec3 direct(`, line 1086; the light evaluations at lines 1100, 1121, 1280, 1387), `pc/src/gl_present.c` (includes; `scaled_fragment_source` before `vec3 low_haze(`, line 351; lines 370, 585-594), `pc/src/editor_view.cpp` (includes; `fragment_source` before its `void main() {`; line 246)

- [ ] **Step 1: ptgi.c (the GL path tracer)**

Add `#include "light_shape.h"` after `#include "glow_overrides.h"`. In `trace_source`, before the line `    /* Direct light from the dynamic lights at a world point, with shadow rays. */` add:

```c
    PC_LIGHT_SHAPE_GLSL /* a spot's edge, a rect's face and barn doors (light_shape.h) */
```

The two lines

```c
    "        if (light_col[i].w > 1.5) a *= max(dot(-l, light_dir[i].xyz), 0.0);\n"
    "        else if (light_col[i].w > 0.5) a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));\n"
```

appear three times (in `direct`, `visibility` and the sun's weights); replace every occurrence (Edit with `replace_all`) with

```c
    "        a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], p, l);\n"
```

In the haze's `source_light`, replace

```c
    "    if (i < light_count && light_col[i].w > 1.5) a *= max(dot(-l, light_dir[i].xyz), 0.0);\n"
    "    else if (i < light_count && light_col[i].w > 0.5) a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));\n"
```

with

```c
    "    if (i < light_count) a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], x, l);\n"
```

- [ ] **Step 2: gl_present.c (the GL present)**

Add `#include "light_shape.h"` after `#include "gl_shaders.h"`. In `scaled_fragment_source`, before the comment `    /* Unshadowed haze (haze Low; scaled.frag the same): the room's medium at` add:

```c
    PC_LIGHT_SHAPE_GLSL /* a spot's edge, a rect's face and barn doors (light_shape.h) */
```

In `low_haze`, replace

```c
    "            if (light_col[i].w > 1.5) a *= max(dot(-l, light_dir[i].xyz), 0.0);\n"
    "            else if (light_col[i].w > 0.5) a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));\n"
```

with

```c
    "            a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], x, l);\n"
```

In the lights loop, replace

```c
    "                if (light_col[i].w > 1.5)\n" /* rect: emits from its front face */
    "                    a *= max(dot(-l, light_dir[i].xyz), 0.0);\n"
    "                else if (light_col[i].w > 0.5)\n" /* spot: smooth cone edge */
    "                    a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));\n"
```

with

```c
    "                a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], world, l);\n"
```

and change `"                        light_right[i].w > 0.0 ? light_right[i].w : quality2.x, min(sqrt(1.0 - cosine * cosine) / cosine, 4.0));\n"` to

```c
    "                        light_col[i].w < 1.5 && light_right[i].w > 0.0 ? light_right[i].w : quality2.x, min(sqrt(1.0 - cosine * cosine) / cosine, 4.0));\n"
```

- [ ] **Step 3: editor_view.cpp (the editor's own GL view)**

Add `#include "light_shape.h"` after `#include "gl_api.h"`. In `fragment_source` (a raw string), replace

```cpp
    return weight > 1e-5 ? sum / weight : vec4(0.0, 0.0, 0.0, 1.0);
}
void main() {
    if (facing != 0u
```

with

```cpp
    return weight > 1e-5 ? sum / weight : vec4(0.0, 0.0, 0.0, 1.0);
}
)" PC_LIGHT_SHAPE_GLSL R"(
void main() {
    if (facing != 0u
```

and replace

```cpp
        if (light_col[i].w > 1.5)
            a *= max(dot(-l, light_dir[i].xyz), 0.0);
        else if (light_col[i].w > 0.5)
            a *= smoothstep(light_dir[i].w, mix(light_dir[i].w, 1.0, 0.25), dot(-l, light_dir[i].xyz));
```

with

```cpp
        a *= light_shape(light_col[i].w, light_pos[i].xyz, light_dir[i], light_right[i], light_up[i], v_world, l);
```

- [ ] **Step 4: Build and run**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 barn_doors lighting light_actors fog gi glow
```

Expected: build exit 0; `barn_doors_smoke` passes on OpenGL, the other suites stay green (no `[gl] shader` line in their logs).

- [ ] **Step 5: Commit**

```bash
git add pc/src/ptgi.c pc/src/gl_present.c pc/src/editor_view.cpp
git commit -m "$(cat <<'EOF'
OpenGL: light shapes (spot inner cone, rect barn doors) in the present, path tracer, haze and editor view (PC_LIGHT_SHAPE_GLSL)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 13: The handle layer

**Files:**
- Create: `pc/src/editor_handles.h`, `pc/src/editor_handles.cpp`, `pc/src/editor_light_handles.cpp`, `pc/tests/editor_handles_smoke.py`
- Modify: `pc/src/lights.cpp` (include; `pc_lights_viewport`: before `ImGuizmo::SetOrthographic(false);`, before the fog volumes' call, before the emissive click hook), `pc/src/dev_overlay.cpp` (include line 25; Escape chain line 377; `pc_overlay_input` line 230-297), `pc/src/dev_rpc.c` (the `editor_select` route; `editor_input`, line 2941; `edits`), `pc/src/editor_view.cpp` (Alt orbit, line 911), `pc/tools/build_boot.py`, `pc/tests/run.py`

- [ ] **Step 1: Write the failing test**

Create `pc/tests/editor_handles_smoke.py`:

```python
#!/usr/bin/env python3
"""The editor's viewport handles (pc/src/editor_handles.h, editor_light_handles.cpp)
and the colour popover (editor_colour.cpp), driven with real mouse events.

    python3 pc/tests/editor_handles_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The same save room and objects as editor_selection_smoke.py (a point light, a
candle, a fog volume). Each handle drag is checked the same way: dragged to
where the handle shows the wanted value (RPC editor_handles where), exactly
its field changes, by the expected amount; the drag is one undo step named
after it, and undo puts it back. Sections:

- layer: handles show for the selection's primary only (a light; none for
  nothing, a light actor or a hidden light), never while the camera flies;
  editor_input holds and releases modifiers.
"""
import argparse
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import centre, finish, pinned_card, start  # noqa: E402
from editor_schema_smoke import close, schema, value  # noqa: E402


def handles(s, **params):
    return s.game.call("editor_handles", **params)


def handle(s, name):
    return next((h for h in handles(s)["handles"] if h["name"] == name), None)


def fields(s, ident):
    return {k: v["value"] for k, v in schema(s, action="get", id=ident)["fields"].items()}


def changed(a, b):
    return sorted(k for k in b if k in a and not close(a[k], b[k]))


def drag(s, start, end, hold=None, steps=2, release=True):
    """A real mouse drag (RPC system mouse) from start to end, modifiers held (editor_input hold)."""
    g = s.game
    if hold:
        g.call("editor_input", hold=hold)
        g.run(2)
    s.mouse(start, down=True)
    for k in range(1, steps + 1):
        s.mouse([start[0] + (end[0] - start[0]) * k / steps, start[1] + (end[1] - start[1]) * k / steps], down=True)
    if release:
        s.mouse(end, down=False)
    if hold:
        g.call("editor_input", release=True)
        g.run(2)


def check_drag(s, ident, name, field, label, target_value, expect, tolerance=0.02, hold=None, also=()):
    """Drag handle `name` to where it shows `target_value`: only `field` (and `also`) change, `field` to
    `expect`; one undo step "Edit <label>"; undo puts it back. Returns the fields after the drag."""
    h = handle(s, name)
    if not s.check(h is not None, f"{ident}: a {name} handle ({[x['name'] for x in handles(s)['handles']]})"):
        return None
    target = handles(s, action="where", handle=name, value=target_value)["screen"]
    before, done = fields(s, ident), s.history()["done"]
    drag(s, h["screen"], target, hold=hold)
    after, hist = fields(s, ident), s.history()
    s.check(changed(before, after) == sorted([field, *also]) and close(after[field], expect, tolerance),
            f"{name} dragged{' with ' + '+'.join(hold) if hold else ''}: {changed(before, after)}; "
            f"{field} {before[field]} -> {after[field]} ({expect} expected)")
    s.check(hist["done"] == done + 1 and hist["undo"] == "Edit " + label,
            f"{name}: one undo step ({hist['undo']!r}, {hist['done'] - done} steps)")
    s.game.call("editor_undo", action="undo")
    s.check(close(fields(s, ident)[field], before[field]), f"{name}: undo puts {field} back")
    return after


def check_escape(s, ident, name, field, target_value):
    """Escape during a drag: the value back, no undo step, the editor still open."""
    h = handle(s, name)
    target = handles(s, action="where", handle=name, value=target_value)["screen"]
    before, done = fields(s, ident), s.history()["done"]
    drag(s, h["screen"], target, release=False)
    mid = fields(s, ident)[field]
    state = s.keys(["escape"])
    s.mouse(target, down=False)
    after = fields(s, ident)
    s.check(not close(mid, before[field]) and changed(before, after) == [] and state["visible"]
            and s.history()["done"] == done,
            f"{name}: Escape mid-drag ({field} {mid}) puts it back ({after[field]}), no undo step, the editor open")


def check_layer(s):
    g = s.game
    s.select(action="clear")
    st = handles(s)
    s.check(st["hidden"] == "nothing" and st["handles"] == [] and st["subject"] is None,
            f"nothing selected: no handles ({st['hidden']})")
    s.select(action="set", ids=[s.actor])
    g.run(2)
    st = handles(s)
    s.check(st["hidden"] == "unsupported" and st["handles"] == [], f"a light actor: none ({st['hidden']})")
    s.select(action="set", ids=[s.light])
    g.run(2)
    st = handles(s)
    s.check(st["subject"] == s.light and st["hidden"] is None, f"a light selected: its handles ({st['subject']})")
    s.keys(["h"])  # H hides the selection (and takes it out of it)
    s.select(action="set", ids=[s.light])
    g.run(2)
    st = handles(s)
    s.check(st["hidden"] == "hidden" and st["handles"] == [], f"a hidden light: none ({st['hidden']})")
    s.keys(["h"], ctrl=True)
    s.select(action="set", ids=[s.light])
    view = centre(s.select()["view"])
    s.mouse(view, 3, True)
    g.run(2)
    st = handles(s)
    s.mouse(view, 3, False)
    s.check(st["hidden"] == "flying" and st["handles"] == [], f"while the camera flies: none ({st['hidden']})")
    g.call("editor_camera", off=1)
    g.run(4)
    s.check(g.call("editor_input", hold=["shift"])["held"] == ["shift"], "editor_input holds Shift")
    s.check(g.call("editor_input", release=True)["held"] == [], "and lets it go")


SECTIONS = [check_layer]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (layer, point, ...)")
    args = parser.parse_args()
    s = start(args, "editor_handles")
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
        status = finish(s, "handles on the selected light and the sun; the colour popover")
    return status


if __name__ == "__main__":
    sys.exit(main())
```

In `pc/tests/run.py`, after the `barn_doors_smoke` Suite add:

```python
            Suite("editor_handles_smoke", [PY, str(TESTS / "editor_handles_smoke.py"), "--card", str(args.save)], 600,
                  note="viewport handles (mouse drags): each changes one field, Ctrl fine, Shift snap, Esc, one undo "
                       "step, hidden while flying; point/spot/rect/sun handles; the colour popover and eyedropper"),
```

and in `CHECKPOINT_SUITES` change `"barn_doors_smoke", "facing_cutaway_smoke",` to `"barn_doors_smoke", "editor_handles_smoke", "facing_cutaway_smoke",`.

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `0/1 passed`; the log shows `RpcError: unknown method editor_handles`.

- [ ] **Step 3: The header**

Create `pc/src/editor_handles.h`:

```cpp
/* The viewport's handles (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * "Handle layer" and "Viewport handles per type"): small controls drawn in
 * screen space at world positions on the selection's primary (a light, the
 * sun), each changing one property as it is dragged or clicked. Shared by
 * every handle: Ctrl drags finely, Shift snaps to round values, the value
 * shows by the pointer while dragging, Escape puts it back, a drag is one
 * undo step (pc_undo_begin with its name), and no handle shows while the
 * camera flies or for anything but the primary. ImGuizmo keeps move, rotate
 * and scale. editor_light_handles.cpp builds each kind's handles from its
 * schema fields (editor_schema.h). Editor internals; RPC editor_handles. */
#ifndef ROOD_EDITOR_HANDLES_H
#define ROOD_EDITOR_HANDLES_H
#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

#include "editor_overlays.h"
#include "editor_selection.h"
#include "environment_internal.h"
#include "lights_internal.h"

namespace rood_handles {
enum Shape { SHAPE_DOT, SHAPE_RING, SHAPE_SQUARE, SHAPE_TOGGLE, SHAPE_CROSS, SHAPE_DISC };
/* A drag in progress, as a handle's drag function sees it. */
struct Drag {
    float press[2] = { 0, 0 }, now[2] = { 0, 0 }; /* window pixels */
    float press_origin[3] = { 0, 0, 0 }, press_dir[3] = { 0, 0, 1 }; /* the mouse ray at the press */
    float origin[3] = { 0, 0, 0 }, dir[3] = { 0, 0, 1 }; /* and now */
    bool fine = false, snap = false, symmetric = false; /* Ctrl, Shift, Alt */
    pc_overlay_view view; /* as the drag began */
    rood_lights::Light light; /* the light as the drag began (Escape puts it back) */
};
struct Handle {
    std::string name; /* "intensity", "radius", "range", "cone", "inner", "width+", ...: the RPC's and the tests' */
    std::string label; /* by the pointer and in the undo step ("Edit Intensity") */
    Shape shape = SHAPE_DOT;
    float x = 0, y = 0; /* where it is grabbed (window pixels) */
    float cx = 0, cy = 0; /* a ring's centre */
    float radius = 6; /* its hit radius; a ring's radius (grabbed within 5 pixels of it) */
    uint32_t colour = 0xffffffff; /* IM_COL32 */
    std::function<void(const Drag&)> drag; /* null: a click handle */
    std::function<void()> click; /* a press and release without a drag */
    std::function<std::string()> readout; /* "Intensity 2.00" */
    /* The pixel to drag it to for a value (RPC where; tests): a number, or [azimuth, elevation] for the sun. */
    std::function<bool(const prefab::Value& value, float out[2])> where;
};

/* editor_light_handles.cpp: the primary's handles in this view by its kind,
 * and what a selected light draws besides (a spot's inner cone, a rect's
 * barn door flaps). */
void light_handles(const pc_overlay_view& v, const rood_selection::Ref& r, int index, std::vector<Handle>& out);
void light_extras(const pc_overlay_view& v, int index, void* draw_list);

/* Each viewport frame (lights.cpp, with the editor's view and surface
 * picker): draws the primary's handles and runs their mouse; true when a
 * handle has the mouse (hovered or dragged): the gizmo and the view's clicks
 * leave it alone. */
bool frame(const pc_overlay_view& v, pc_overlay_pick pick, void* draw_list);
bool dragging();
const char* hot(); /* the handle dragged or under the pointer (its name), or null */
bool handle_pixel(const char* name, float out[2]); /* where a handle was last drawn */
/* Escape: a drag's values back (no undo step), else aiming disarmed; true when it did. */
bool cancel();
/* A spot's "aim at surface" (its handle): the next click on a surface points it there. */
void arm_aim(const rood_selection::Ref& r);
bool viewport_click(float x, float y); /* aiming: this click aims; true when it took it */
/* A schema field of an object (editor_schema.h), read and set as Details does. */
prefab::Value get_value(const rood_selection::Ref& r, const char* field);
float get(const rood_selection::Ref& r, const char* field);
void set(const rood_selection::Ref& r, const char* field, const prefab::Value& v);
std::string readout(const rood_selection::Ref& r, const char* field); /* "Radius 640 u" */
std::string state_json(); /* RPC editor_handles {action "state"} */
} // namespace rood_handles

extern "C" int pc_editor_handles_rpc(const char* params, char* out, int size);
extern "C" int pc_editor_handle_drag(void); /* editor_view.cpp: no Alt orbit while a handle drags */
#endif
```

- [ ] **Step 4: The layer**

Create `pc/src/editor_handles.cpp`:

```cpp
// The viewport's handles (editor_handles.h): drawing, hover, presses, drags
// with the shared modifiers, the readout, Escape, one undo step per drag,
// spot aiming, and the RPC editor_handles.
#include "editor_handles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "ImGuizmo.h"
#include "editor_math.h"
#include "editor_picking.h"
#include "editor_scene.h"
#include "editor_schema.h"
#include "editor_undo.h"

extern "C" int pc_editor_flying(void); /* editor_view.cpp */
extern "C" int pc_lights_tools_view(pc_overlay_view* out, pc_overlay_pick* pick); /* lights.cpp */

using prefab::Value;
using rood_handles::Drag;
using rood_handles::Handle;
using rood_selection::Ref;

namespace {
std::vector<Handle> shown; /* the primary's, as last drawn */
Ref subject;
const char* hidden_reason = "nothing"; /* why none show: nothing, unsupported, hidden, flying */
int shown_frame = -100;
int hovered = -1;
struct Active {
    bool on = false, moved = false;
    std::string name;
    Ref subject;
    Drag drag;
} active;
Ref aim_target; /* a spot waiting for the click that aims it */
pc_overlay_pick aim_pick;

int frame_now() { return ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1; }
const char* shape_name(rood_handles::Shape s)
{
    static const char* const names[] = { "dot", "ring", "square", "toggle", "cross", "disc" };
    return names[s];
}
const Handle* find(const std::string& name)
{
    for (const Handle& h : shown)
        if (h.name == name)
            return &h;
    return nullptr;
}
bool over(const Handle& h, float x, float y)
{
    bool ring = h.shape == rood_handles::SHAPE_RING;
    float dx = x - (ring ? h.cx : h.x), dy = y - (ring ? h.cy : h.y), d = sqrtf(dx * dx + dy * dy);
    return ring ? fabsf(d - h.radius) < 5 : d < h.radius + 3;
}
/* The primary's handles in this view, or why it has none. */
const char* build(const pc_overlay_view& v, std::vector<Handle>& out, Ref* who)
{
    out.clear();
    Ref p = rood_selection::primary();
    *who = p;
    if (!p)
        return "nothing";
    if (rood_selection::hidden(p))
        return "hidden";
    if (p.kind == rood_selection::KIND_LIGHT) {
        int i = rood_scene::index_of(p);
        if (i < 0)
            return "nothing";
        rood_handles::light_handles(v, p, i, out);
        return nullptr;
    }
    return "unsupported";
}
void restore(const Active& a)
{
    int i = rood_scene::index_of(a.subject);
    if (a.subject.kind == rood_selection::KIND_LIGHT && i >= 0)
        rood_lights::lights[i] = a.drag.light;
}
void end(bool keep)
{
    if (!active.on)
        return;
    if (!keep)
        restore(active);
    active.on = false;
    pc_undo_end();
}
void draw_handle(ImDrawList* d, const Handle& h, bool hot)
{
    ImU32 lit = IM_COL32(255, 255, 0, 255), edge = hot ? lit : IM_COL32(0, 0, 0, 220);
    ImVec2 p(h.x, h.y);
    switch (h.shape) {
    case rood_handles::SHAPE_RING:
        d->AddCircle(ImVec2(h.cx, h.cy), h.radius, IM_COL32(0, 0, 0, 160), 48, 4.0f);
        d->AddCircle(ImVec2(h.cx, h.cy), h.radius, hot ? lit : h.colour, 48, 2.0f);
        d->AddCircleFilled(p, 3.5f, hot ? lit : h.colour);
        break;
    case rood_handles::SHAPE_SQUARE:
        d->AddRectFilled(ImVec2(p.x - h.radius, p.y - h.radius), ImVec2(p.x + h.radius, p.y + h.radius), h.colour, 2);
        d->AddRect(ImVec2(p.x - h.radius, p.y - h.radius), ImVec2(p.x + h.radius, p.y + h.radius), edge, 2, 0, 1.5f);
        break;
    case rood_handles::SHAPE_TOGGLE:
        d->AddCircleFilled(p, h.radius, h.colour, 16);
        d->AddCircle(p, h.radius, edge, 16, 1.5f);
        break;
    case rood_handles::SHAPE_CROSS:
        d->AddCircle(p, h.radius, hot ? lit : h.colour, 16, 1.5f);
        d->AddLine(ImVec2(p.x - h.radius - 3, p.y), ImVec2(p.x + h.radius + 3, p.y), hot ? lit : h.colour, 1.5f);
        d->AddLine(ImVec2(p.x, p.y - h.radius - 3), ImVec2(p.x, p.y + h.radius + 3), hot ? lit : h.colour, 1.5f);
        break;
    case rood_handles::SHAPE_DISC:
        d->AddCircleFilled(p, h.radius, h.colour, 24);
        d->AddCircle(p, h.radius, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(30, 30, 30, 200), 24, 2.0f);
        break;
    default:
        d->AddCircleFilled(p, h.radius, h.colour, 16);
        d->AddCircle(p, h.radius, edge, 16, 1.5f);
    }
}
} // namespace

bool rood_handles::frame(const pc_overlay_view& v, pc_overlay_pick pick, void* draw_list)
{
    ImDrawList* draw = (ImDrawList*)draw_list;
    ImGuiIO& io = ImGui::GetIO();
    shown_frame = frame_now();
    aim_pick = pick;
    hovered = -1;
    if (pc_editor_flying()) { /* the camera flies: no handles; a drag in progress is put back */
        end(false);
        shown.clear();
        hidden_reason = "flying";
        return false;
    }
    hidden_reason = build(v, shown, &subject);
    if (hidden_reason) {
        end(false);
        return false;
    }
    if (subject.kind == rood_selection::KIND_LIGHT)
        light_extras(v, rood_scene::index_of(subject), draw);
    float mx = io.MousePos.x, my = io.MousePos.y;
    if (active.on) {
        const Handle* h = find(active.name);
        if (!h || active.subject != subject) { /* the selection changed under the drag */
            end(false);
            return false;
        }
        Drag& d = active.drag;
        d.now[0] = mx, d.now[1] = my;
        rood_pick::ray(d.view.view, d.view.projection, d.view.rect, mx, my, d.origin, d.dir);
        d.fine = io.KeyCtrl, d.snap = io.KeyShift, d.symmetric = io.KeyAlt;
        if (fabsf(mx - d.press[0]) + fabsf(my - d.press[1]) >= 2)
            active.moved = true;
        if (active.moved && h->drag)
            h->drag(d);
        for (const Handle& k : shown)
            draw_handle(draw, k, k.name == active.name);
        if (active.moved && h->readout) { /* the value by the pointer */
            std::string text = h->readout();
            ImDrawList* front = ImGui::GetForegroundDrawList();
            ImVec2 size = ImGui::CalcTextSize(text.c_str()), at(mx + 16, my - size.y - 6);
            front->AddRectFilled(ImVec2(at.x - 4, at.y - 2), ImVec2(at.x + size.x + 4, at.y + size.y + 2),
                IM_COL32(20, 20, 24, 220), 3);
            front->AddText(at, IM_COL32(255, 230, 120, 255), text.c_str());
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            std::function<void()> click = active.moved ? std::function<void()>() : h->click;
            end(true);
            if (click)
                click();
        }
        return true;
    }
    /* Hover: not while the gizmo drags or a panel is under the pointer. */
    if (!ImGuizmo::IsUsing() && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        for (int i = (int)shown.size() - 1; i >= 0 && hovered < 0; i--)
            if (over(shown[i], mx, my))
                hovered = i;
    for (int i = 0; i < (int)shown.size(); i++)
        draw_handle(draw, shown[i], i == hovered);
    if (hovered < 0)
        return false;
    const Handle& h = shown[hovered];
    ImGui::SetTooltip("%s", h.label.c_str());
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        active = Active();
        active.on = true;
        active.name = h.name;
        active.subject = subject;
        Drag& d = active.drag;
        d.press[0] = d.now[0] = mx, d.press[1] = d.now[1] = my;
        d.view = v;
        rood_pick::ray(v.view, v.projection, v.rect, mx, my, d.press_origin, d.press_dir);
        memcpy(d.origin, d.press_origin, sizeof d.origin);
        memcpy(d.dir, d.press_dir, sizeof d.dir);
        int i = rood_scene::index_of(subject);
        if (subject.kind == rood_selection::KIND_LIGHT && i >= 0)
            d.light = rood_lights::lights[i];
        pc_undo_begin(("Edit " + h.label).c_str()); /* the whole drag is one undo step */
    }
    return true;
}
bool rood_handles::dragging() { return active.on; }
const char* rood_handles::hot()
{
    if (active.on)
        return active.name.c_str();
    return hovered >= 0 && hovered < (int)shown.size() && shown_frame >= frame_now() - 1 ? shown[hovered].name.c_str()
                                                                                           : nullptr;
}
bool rood_handles::handle_pixel(const char* name, float out[2])
{
    const Handle* h = find(name);
    if (!h)
        return false;
    out[0] = h->x, out[1] = h->y;
    return true;
}
bool rood_handles::cancel()
{
    if (active.on) {
        end(false);
        return true;
    }
    if (aim_target) {
        aim_target = Ref();
        return true;
    }
    return false;
}
void rood_handles::arm_aim(const Ref& r) { aim_target = r; }
bool rood_handles::viewport_click(float x, float y)
{
    if (!aim_target)
        return false;
    Ref r = aim_target;
    aim_target = Ref();
    int i = rood_scene::index_of(r);
    float w[3], n[3];
    if (i < 0 || !aim_pick || !aim_pick(x, y, w, n))
        return true; /* the click was the aim's; on nothing it does nothing */
    const float* p = rood_lights::lights[i].position;
    float d[3] = { w[0] - p[0], w[1] - p[1], w[2] - p[2] }, len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-3f)
        return true;
    set(r, "direction", Value::vec(d[0] / len, d[1] / len, d[2] / len));
    pc_undo_hint("Aim at surface");
    return true;
}

Value rood_handles::get_value(const Ref& r, const char* field)
{
    int i;
    const rood_schema::Kind* k = rood_schema::kind_of(r, &i);
    const rood_schema::Field* f = k ? k->field(field) : nullptr;
    return f ? f->get(i) : Value();
}
float rood_handles::get(const Ref& r, const char* field) { return get_value(r, field).x(); }
void rood_handles::set(const Ref& r, const char* field, const Value& v)
{
    int i;
    const rood_schema::Kind* k = rood_schema::kind_of(r, &i);
    const rood_schema::Field* f = k ? k->field(field) : nullptr;
    if (f)
        rood_schema::apply(*k, *f, r, v);
}
std::string rood_handles::readout(const Ref& r, const char* field)
{
    int i;
    const rood_schema::Kind* k = rood_schema::kind_of(r, &i);
    const rood_schema::Field* f = k ? k->field(field) : nullptr;
    return f ? f->label + " " + rood_schema::format(*f, f->get(i)) : std::string();
}

std::string rood_handles::state_json()
{
    auto q = [](const std::string& s) { return prefab::json_quote(s); };
    bool current = shown_frame >= 0 && shown_frame >= frame_now() - 1;
    const char* why = current ? hidden_reason : "editor";
    std::string s = "{\"subject\":" + (why ? std::string("null") : q(subject.id)) + ",\"hidden\":"
        + (why ? q(why) : std::string("null")) + ",\"handles\":[";
    for (size_t i = 0; !why && i < shown.size(); i++) {
        const Handle& h = shown[i];
        bool ring = h.shape == SHAPE_RING;
        char geo[200];
        snprintf(geo, sizeof geo, ",\"screen\":[%.2f,%.2f],\"centre\":[%.2f,%.2f],\"radius\":%.2f", h.x, h.y,
            ring ? h.cx : h.x, ring ? h.cy : h.y, h.radius);
        s += std::string(i ? "," : "") + "{\"name\":" + q(h.name) + ",\"label\":" + q(h.label) + ",\"shape\":\""
            + shape_name(h.shape) + "\"" + geo + "}";
    }
    const Handle* h = active.on ? find(active.name) : nullptr;
    s += "],\"drag\":{\"active\":" + std::string(active.on ? "true" : "false") + ",\"handle\":"
        + (active.on ? q(active.name) : std::string("null")) + ",\"moved\":" + (active.moved ? "true" : "false")
        + ",\"readout\":" + (h && h->readout ? q(h->readout()) : std::string("null")) + "},\"aim\":"
        + (aim_target ? q(aim_target.id) : std::string("null")) + "}";
    return s;
}

/* RPC editor_handles (dev_rpc.c hands over the params object's text): action
 * state (default): the primary's handles as last drawn (name, label, shape,
 * screen: where to press, a ring's centre and radius), why none show
 * (nothing, unsupported, hidden, flying, editor), the drag in progress (its
 * readout) and the spot waiting to be aimed; where {handle, value}: the
 * window pixel to drag the handle to for that value (the sun's: [azimuth,
 * elevation]). */
extern "C" int pc_editor_handles_rpc(const char* params_text, char* out, int size)
{
    prefab::Json params;
    std::string error;
    if (!prefab::json_parse(params_text && *params_text ? params_text : "{}", params, &error)
        || params.type != prefab::Json::Object) {
        snprintf(out, size, "params: %s", error.empty() ? "expected an object" : error.c_str());
        return -1;
    }
    rood_scene::frame();
    const prefab::Json* a = params.get("action");
    std::string action = a && a->type == prefab::Json::String ? a->string : "state";
    std::string text;
    if (action == "where") {
        const prefab::Json *name = params.get("handle"), *value = params.get("value");
        pc_overlay_view v;
        std::vector<Handle> hs;
        Ref who;
        if (!name || !value || !pc_lights_tools_view(&v, nullptr) || build(v, hs, &who)) {
            snprintf(out, size, "where needs handle and value, an editor view and a light or the sun selected");
            return -1;
        }
        const Handle* h = nullptr;
        for (const Handle& k : hs)
            if (k.name == name->string)
                h = &k;
        if (!h || !h->where) {
            snprintf(out, size, "no handle %s with a position for a value", name->string.c_str());
            return -1;
        }
        Value val = value->type == prefab::Json::Array && value->items.size() >= 2
            ? Value::vec((float)value->items[0].number, (float)value->items[1].number,
                  value->items.size() > 2 ? (float)value->items[2].number : 0.0f)
            : Value::scalar((float)value->number);
        float px[2];
        if (!h->where(val, px)) {
            snprintf(out, size, "that value is off the view");
            return -1;
        }
        char buf[64];
        snprintf(buf, sizeof buf, "{\"screen\":[%.2f,%.2f]}", px[0], px[1]);
        text = buf;
    } else if (action == "state") {
        text = rood_handles::state_json();
    } else {
        snprintf(out, size, "action: state or where");
        return -1;
    }
    snprintf(out, size, "%s", text.c_str());
    return (int)text.size() < size ? 0 : -1;
}
extern "C" int pc_editor_handle_drag(void) { return active.on ? 1 : 0; }
```

Create `pc/src/editor_light_handles.cpp` (each kind's handles arrive in Tasks 14-17):

```cpp
// Each kind's handles (editor_handles.h): none yet (the layer alone).
#include "editor_handles.h"

void rood_handles::light_handles(const pc_overlay_view&, const rood_selection::Ref&, int, std::vector<Handle>&) {}
void rood_handles::light_extras(const pc_overlay_view&, int, void*) {}
```

- [ ] **Step 5: Wire it into the view, Escape, input and the RPC**

In `pc/src/lights.cpp`, add `#include "editor_handles.h"` after `#include "editor_transform.h"`. In `pc_lights_viewport`, replace

```cpp
    if (probe_valid)
        draw_probe(v, draw);
    ImGuizmo::SetOrthographic(false);
```

with

```cpp
    if (probe_valid)
        draw_probe(v, draw);
    /* The selection's handles (editor_handles.cpp) have the mouse before the
     * gizmo and the view's clicks; the gizmo is off under a handle. */
    bool handles_busy = !drag.active && rood_handles::frame(ov, pick_for_snap, draw);
    ImGuizmo::Enable(!handles_busy);
    ImGuizmo::SetOrthographic(false);
```

replace

```cpp
    /* Fog volumes (atmosphere.cpp): outlines, the selected one's gizmo. */
    if (pc_atmosphere_viewport(v.view, v.projection, v.rect, draw))
```

with

```cpp
    ImGuizmo::Enable(true);
    gizmo_busy |= handles_busy;
    /* Fog volumes (atmosphere.cpp): outlines, the selected one's gizmo. */
    if (pc_atmosphere_viewport(v.view, v.projection, v.rect, draw))
```

and before `/* The emissive panel's texture picker (emissive_editor.cpp) takes the click when armed. */` add

```cpp
    /* A spot waiting to be aimed (its handle) takes the click. */
    if (rood_handles::viewport_click(io.MousePos.x, io.MousePos.y))
        return;
```

In `pc/src/dev_overlay.cpp`, add `#include "editor_handles.h"` after `#include "editor_details.h"`. Before `/* Escape during a value ladder drag cancels it (editor_details.cpp). */` add

```cpp
        /* Escape during a handle drag puts its value back, or stops aiming (editor_handles.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_handles::cancel())
            return 1;
```

Change `pc_overlay_input`'s signature to

```cpp
extern "C" int pc_overlay_input(const char* keys, const char* text, int ctrl, int shift, const char* focus, const char* hold,
    int release, char* out, int size)
```

(its comment gains: `hold: modifiers ("ctrl", "shift", "alt") pressed and kept down across requests until one with release;`), replace `SDL_Keymod mod = (ctrl ? SDL_KMOD_LCTRL : 0) | (shift ? SDL_KMOD_LSHIFT : 0);` with

```cpp
    /* hold: modifiers kept down across requests (a handle dragged with Ctrl,
     * Shift or Alt) until a request with release. */
    static SDL_Keymod held;
    static const struct {
        const char* name;
        SDL_Keycode key;
        SDL_Keymod mod;
    } modifiers[] = { { "ctrl", SDLK_LCTRL, SDL_KMOD_LCTRL }, { "shift", SDLK_LSHIFT, SDL_KMOD_LSHIFT },
        { "alt", SDLK_LALT, SDL_KMOD_LALT } };
    auto push_modifier = [](SDL_Keycode key, bool down, SDL_Keymod mods) {
        SDL_Event e;
        memset(&e, 0, sizeof e);
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = SDL_GetWindowID(overlay_window);
        e.key.key = key;
        e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
        e.key.mod = mods;
        e.key.down = down;
        SDL_PushEvent(&e);
    };
    if (release)
        for (const auto& m : modifiers)
            if (held & m.mod) {
                held = (SDL_Keymod)(held & ~m.mod);
                push_modifier(m.key, false, held);
            }
    for (const char* p = hold ? strchr(hold, '"') : nullptr; p; p = strchr(p, '"')) {
        const char* end = strchr(p + 1, '"');
        if (!end)
            break;
        char name[16];
        snprintf(name, sizeof name, "%.*s", (int)(end - p - 1), p + 1);
        p = end + 1;
        bool known = false;
        for (const auto& m : modifiers)
            if (!strcmp(name, m.name)) {
                known = true;
                held = (SDL_Keymod)(held | m.mod);
                push_modifier(m.key, true, held);
            }
        if (!known) {
            snprintf(out, size, "unknown modifier %s (ctrl, shift, alt)", name);
            return -1;
        }
    }
    SDL_Keymod mod = (SDL_Keymod)((ctrl ? SDL_KMOD_LCTRL : 0) | (shift ? SDL_KMOD_LSHIFT : 0) | held);
```

and replace the reply

```cpp
    snprintf(out, size, "{\"visible\":%s,\"want_text_input\":%s,\"item_active\":%s,\"gizmo\":%d}", visible ? "true" : "false",
        io.WantTextInput ? "true" : "false", ImGui::IsAnyItemActive() ? "true" : "false", *pc_lights_gizmo_operation());
```

with

```cpp
    char held_names[48] = "";
    for (const auto& m : modifiers)
        if (held & m.mod)
            snprintf(held_names + strlen(held_names), sizeof held_names - strlen(held_names), "%s\"%s\"",
                held_names[0] ? "," : "", m.name);
    snprintf(out, size, "{\"visible\":%s,\"want_text_input\":%s,\"item_active\":%s,\"gizmo\":%d,\"held\":[%s]}",
        visible ? "true" : "false", io.WantTextInput ? "true" : "false", ImGui::IsAnyItemActive() ? "true" : "false",
        *pc_lights_gizmo_operation(), held_names);
```

In `pc/src/dev_rpc.c`, the `editor_input` branch: change the extern to

```c
        extern int pc_overlay_input(const char* keys, const char* text, int ctrl, int shift, const char* focus,
            const char* hold, int release, char* out, int size) __attribute__((weak));
```

change `char keys[256] = "", typed[128] = "", focus[16] = "";` and `int ctrl = 0, shift = 0;` to

```c
        char keys[256] = "", typed[128] = "", focus[16] = "", hold[64] = "";
        int ctrl = 0, shift = 0, release = 0;
```

after `bool_value(member(params, "shift"), &shift);` add

```c
            const char* held = member(params, "hold");
            const char* held_end = held ? value_end(held) : NULL;
            if (held && held_end && (size_t)(held_end - held) < sizeof hold) {
                memcpy(hold, held, (size_t)(held_end - held));
                hold[held_end - held] = 0;
            }
            bool_value(member(params, "release"), &release);
```

and change the call to `pc_overlay_input(keys, typed, ctrl, shift, focus, hold, release, result, (int)sizeof result)`. In the route changed in Task 6, add the method: the condition becomes `... || !strcmp(method, "editor_transform") || !strcmp(method, "editor_handles")) {`, add `extern int pc_editor_handles_rpc(const char*, char*, int) __attribute__((weak));`, and the handler choice

```c
        int (*handler)(const char*, char*, int) = !strcmp(method, "editor_select") ? pc_editor_select_rpc
            : !strcmp(method, "editor_schema")                                     ? pc_editor_schema_rpc
            : !strcmp(method, "editor_transform")                                  ? pc_editor_transform_rpc
                                                                                   : pc_editor_handles_rpc;
```

In `edits`, change `|| !strcmp(method, "editor_transform"))` to `|| !strcmp(method, "editor_transform") || !strcmp(method, "editor_handles"))`.

In `pc/src/editor_view.cpp`, before `/* Camera input (called each overlay frame while the editor is active). */` add

```cpp
extern "C" int pc_editor_handle_drag(void); /* editor_handles.cpp: a handle is dragged (Alt keeps a rect symmetric) */
```

and change `if (!over_ui && io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {` to

```cpp
    if (!over_ui && io.KeyAlt && !pc_editor_handle_drag() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
```

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_transform.cpp",` add

```python
    root / "pc/src/editor_handles.cpp",
    root / "pc/src/editor_light_handles.cpp",
```

- [ ] **Step 6: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles editor_selection editor_undo
```

Expected: build exit 0; `3/3 passed` (the layer section; the other editor suites unchanged).

- [ ] **Step 7: Commit**

```bash
git add pc/src/editor_handles.h pc/src/editor_handles.cpp pc/src/editor_light_handles.cpp pc/src/lights.cpp pc/src/dev_overlay.cpp pc/src/dev_rpc.c pc/src/editor_view.cpp pc/tools/build_boot.py pc/tests/editor_handles_smoke.py pc/tests/run.py
git commit -m "$(cat <<'EOF'
Handle layer: primary-only viewport handles with Ctrl/Shift/Alt, readout, Esc, one undo step, hidden while flying; RPC editor_handles, editor_input hold

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 14: The point light's handles: intensity ring, radius dot, shadows toggle

**Files:**
- Modify: `pc/src/editor_light_handles.cpp` (replaced whole), `pc/tests/editor_handles_smoke.py` (a section, `SECTIONS`, docstring)

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_handles_smoke.py`, before `SECTIONS = [check_layer]` add:

```python
def check_point(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "point"})
    # A small radius keeps its dot (and a drag to 1.5x and 2x) inside the view.
    schema(s, action="set", id=s.light, values={"intensity": 1, "radius": 300, "shadows": False})
    g.run(2)
    names = {h["name"] for h in handles(s)["handles"]}
    s.check({"intensity", "radius", "shadows"} <= names, f"a point light's handles ({sorted(names)})")
    check_drag(s, s.light, "intensity", "intensity", "Intensity", 2, 2)  # 100 px right: twice as bright
    ring = handle(s, "intensity")
    for hold, expect, what in ((["ctrl"], 2 ** 0.1, "Ctrl: a tenth as fast"), (["shift"], 2, "Shift: 1, 2, 5 steps")):
        right = 100 if hold == ["ctrl"] else 60  # Shift: 2^0.6 = 1.52 snaps to 2
        drag(s, ring["screen"], [ring["screen"][0] + right, ring["screen"][1]], hold=hold)
        v = value(s, s.light, "intensity")
        s.check(close(v, expect, 0.01), f"intensity ring {right} px with {hold[0]}: {v} ({what}, {expect:.3f})")
        g.call("editor_undo", action="undo")
    check_drag(s, s.light, "radius", "radius", "Radius", 450, 450)
    check_escape(s, s.light, "radius", "radius", 600)
    h = handle(s, "radius")  # the readout while dragging
    s.mouse(h["screen"], down=True)
    s.mouse([h["screen"][0] + 20, h["screen"][1]], down=True)
    st = handles(s)
    s.keys(["escape"])
    s.mouse([h["screen"][0] + 20, h["screen"][1]], down=False)
    s.check(st["drag"]["active"] and st["drag"]["handle"] == "radius" and (st["drag"]["readout"] or "").startswith("Radius"),
            f"the value shows by the pointer while dragging ({st['drag']})")
    done = s.history()["done"]
    s.click(handle(s, "shadows")["screen"])
    h = s.history()
    s.check(value(s, s.light, "shadows") is True and h["done"] == done + 1 and h["undo"] == "Edit Casts shadows",
            f"a click on the ring's toggle: casts shadows, one undo step ({h['undo']})")
    g.call("editor_undo", action="undo")
    # Only the primary: a second light selected after it has the handles.
    other = g.call("lights", action="add", position=[s.player["x"], s.player["y"] - 200, s.player["z"] + 120])["lights"][-1]["id"]
    s.select(action="set", ids=[s.light, other])
    g.run(2)
    s.check(handles(s)["subject"] == other, "two lights selected: the primary's handles only")
    s.select(action="set", ids=[other])
    s.keys(["delete"])
    s.select(action="set", ids=[s.light])
```

change `SECTIONS = [check_layer]` to `SECTIONS = [check_layer, check_point]`, and add to the docstring's list:

```
- point: the intensity ring (100 px right doubles it; Ctrl a tenth as fast;
  Shift 1-2-5 steps), the radius dot, Escape mid-drag, the readout, the
  shadows toggle on the ring; with two lights selected only the primary's.
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `0/1 passed`; the log shows `FAIL: a point light's handles ([])`.

- [ ] **Step 3: Implement the point light's handles**

Replace `pc/src/editor_light_handles.cpp` with:

```cpp
// Each kind's handles (editor_handles.h), built every frame from the object
// as it is; their drag functions work from the object as the drag began
// (Drag::light), so a handle that moves the object it is on stays steady.
// Point: the intensity ring (logarithmic), the radius dot, the shadows toggle.
#include "editor_handles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <string>

#include "imgui.h"
#include "editor_math.h"
#include "editor_scene.h"
#include "editor_undo.h"

using prefab::Value;
using rood_handles::Drag;
using rood_handles::Handle;
using rood_lights::Light;
using rood_lights::lights;
using rood_selection::Ref;

namespace {
using V3 = std::array<float, 3>;
const float DEG = 3.14159265f / 180.0f;
float em() { return ImGui::GetStyle().FontScaleMain; }
V3 vec3(const float* p) { return { p[0], p[1], p[2] }; }
V3 along(const V3& p, const V3& u, float s) { return { p[0] + u[0] * s, p[1] + u[1] * s, p[2] + u[2] * s }; }
uint32_t colour_of(const float c[3])
{
    return IM_COL32((int)(fminf(c[0], 1) * 255), (int)(fminf(c[1], 1) * 255), (int)(fminf(c[2], 1) * 255), 255);
}
bool pixel(const pc_overlay_view& v, const V3& w, float out[2]) { return rood_math::project(v.view, v.projection, v.rect, w.data(), out); }
void frame_of(const Light& l, V3& forward, V3& right, V3& up)
{
    rood_math::light_frame(l.direction, forward.data(), right.data(), up.data());
}
V3 view_right(const pc_overlay_view& v)
{
    float r[3], u[3], f[3];
    rood_math::view_axes(v.view, v.projection, v.rect, r, u, f);
    return vec3(r);
}

/* The intensity ring around an icon (cx, cy): dragged right it doubles every
 * 100 pixels, left halves (Ctrl: every 1000; Shift: 1, 2, 5 steps). */
Handle ring(float cx, float cy, uint32_t colour, const char* label, std::function<float(const Drag&)> start,
    std::function<void(float)> apply, std::function<float()> now, std::function<std::string()> readout)
{
    Handle h;
    h.name = "intensity", h.label = label;
    h.shape = rood_handles::SHAPE_RING;
    h.cx = cx, h.cy = cy, h.radius = 22 * em();
    h.x = cx + h.radius, h.y = cy;
    h.colour = colour;
    h.drag = [start, apply](const Drag& d) {
        float v = rood_math::log_drag(start(d), d.now[0] - d.press[0], d.fine);
        apply(d.snap ? rood_math::snap_125(v) : v);
    };
    h.readout = readout;
    float gx = h.x, gy = h.y;
    h.where = [now, gx, gy](const Value& v, float out[2]) {
        if (v.x() <= 0)
            return false;
        out[0] = gx + 100 * log2f(v.x() / fmaxf(now(), 0.01f)), out[1] = gy;
        return true;
    };
    return h;
}
/* A dot on the world line p + s u (u unit) at s, dragged along it: `line`
 * gives the line from the object as the drag began; the field's value is its
 * start plus the change of s (Ctrl a tenth; Shift multiples of `step`). */
Handle line_dot(const pc_overlay_view& v, const Ref& r, const char* name, const char* field, const char* label, const V3& p,
    const V3& u, float s, std::function<void(const Drag&, V3&, V3&)> line, std::function<float(const Drag&)> start,
    float step)
{
    Handle h;
    h.name = name, h.label = label;
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(p, u, s), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = IM_COL32(255, 230, 120, 255);
    std::string f = field;
    h.drag = [r, f, line, start, step](const Drag& d) {
        V3 lp, lu;
        line(d, lp, lu);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, lp.data(), lu.data());
        float t1 = rood_math::line_param(d.origin, d.dir, lp.data(), lu.data());
        float value = rood_math::linear_drag(start(d), t0, t1, d.fine);
        rood_handles::set(r, f.c_str(), Value::scalar(d.snap ? rood_math::snap_step(value, step) : value));
    };
    h.readout = [r, f]() { return rood_handles::readout(r, f.c_str()); };
    pc_overlay_view view = v;
    h.where = [view, p, u](const Value& value, float out[2]) { return pixel(view, along(p, u, value.x()), out); };
    return h;
}
/* The point light's radius: a dot on its sphere's outline, to the right on screen. */
Handle radius_dot(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    return line_dot(v, r, "radius", "radius", "Radius", vec3(l.position), view_right(v), l.radius,
        [](const Drag& d, V3& p, V3& u) { p = vec3(d.light.position), u = view_right(d.view); },
        [](const Drag& d) { return d.light.radius; }, 16);
}
/* Casts shadows: a toggle on top of the ring (dark when on). */
Handle shadows_toggle(const Ref& r, float cx, float cy, float ring_radius)
{
    Handle h;
    h.name = "shadows", h.label = "Casts shadows";
    h.shape = rood_handles::SHAPE_TOGGLE;
    h.x = cx, h.y = cy - ring_radius, h.radius = 5 * em();
    h.colour = rood_handles::get(r, "shadows") > 0.5f ? IM_COL32(40, 40, 48, 255) : IM_COL32(235, 235, 235, 255);
    h.click = [r]() { rood_handles::set(r, "shadows", Value::scalar(rood_handles::get(r, "shadows") > 0.5f ? 0.0f : 1.0f)); };
    h.readout = [r]() { return rood_handles::readout(r, "shadows"); };
    return h;
}
} // namespace

void rood_handles::light_handles(const pc_overlay_view& v, const Ref& r, int index, std::vector<Handle>& out)
{
    const Light& l = lights[index];
    float c[2];
    if (!pixel(v, vec3(l.position), c))
        return;
    Handle intensity = ring(c[0], c[1], colour_of(l.color), "Intensity", [](const Drag& d) { return d.light.intensity; },
        [r](float x) { rood_handles::set(r, "intensity", Value::scalar(x)); }, [r]() { return rood_handles::get(r, "intensity"); },
        [r]() { return rood_handles::readout(r, "intensity"); });
    out.push_back(intensity);
    if (l.type == 0) {
        out.push_back(radius_dot(v, r, l));
        out.push_back(shadows_toggle(r, c[0], c[1], intensity.radius));
    }
}
void rood_handles::light_extras(const pc_overlay_view&, int, void*) {}
```

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles editor_transform
```

Expected: `2/2 passed` (the R gizmo's centre is still the gizmo's: the handles sit around it). If a drag lands short, read `pc/build/tests/editor_handles_smoke/output.log` (each check prints before and after), then check `line_dot`'s parameters with superpowers:systematic-debugging.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_light_handles.cpp pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
Point light handles: intensity ring (log; Ctrl fine, Shift 1-2-5), radius dot, shadows toggle on the ring

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 15: The spot's handles: range, outer and inner cone, aim at surface

**Files:**
- Modify: `pc/src/editor_light_handles.cpp` (new functions in the anonymous namespace; `light_handles`; `light_extras`), `pc/tests/editor_handles_smoke.py`

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_handles_smoke.py`, before `SECTIONS = ` add:

```python
def check_spot(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "spot"})
    schema(s, action="set", id=s.light, values={"radius": 300, "cone": 60, "inner": 0, "direction": [0, 1, 0]})
    g.run(2)
    names = {h["name"] for h in handles(s)["handles"]}
    s.check({"intensity", "range", "cone", "inner", "aim"} <= names, f"a spot's handles ({sorted(names)})")
    check_drag(s, s.light, "range", "radius", "Range", 450, 450)
    check_drag(s, s.light, "cone", "cone", "Cone", 80, 80)
    check_drag(s, s.light, "inner", "inner", "Inner cone", 40, 40)  # from the automatic inner cone (about 52)
    check_escape(s, s.light, "cone", "cone", 90)
    # Aim at surface: the crosshair, then a click on the floor by Ashley.
    p = s.player
    floor = s.select(action="project", point=[p["x"] + 120, p["y"], p["z"] + 60])["pixel"]
    hit = s.select(action="pick", x=floor[0], y=floor[1])["world"]
    light = next(x for x in g.call("lights")["lights"] if x["id"] == s.light)
    done = s.history()["done"]
    s.click(handle(s, "aim")["screen"])
    s.check(handles(s)["aim"] == s.light, "the crosshair arms aiming")
    s.click(floor)
    want = [a - b for a, b in zip(hit, light["position"])]
    n = math.sqrt(sum(x * x for x in want))
    got, hist = value(s, s.light, "direction"), s.history()
    s.check(close(got, [x / n for x in want], 0.02) and hist["done"] == done + 1 and hist["undo"] == "Aim at surface"
            and handles(s)["aim"] is None,
            f"a click on the floor aims the spot there ({got} vs {[round(x / n, 3) for x in want]}; {hist['undo']})")
    g.call("editor_undo", action="undo")
    s.click(handle(s, "aim")["screen"])
    s.keys(["escape"])
    s.check(handles(s)["aim"] is None and s.keys([])["visible"], "Escape stops aiming (the editor stays open)")
```

change `SECTIONS` to `[check_layer, check_point, check_spot]`, and add to the docstring:

```
- spot: the range dot on its axis, the outer and inner cone handles on the
  cone's side, Escape; the crosshair then a click on the floor aims it there
  (one undo step "Aim at surface"); Escape disarms it.
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `0/1 passed`; `FAIL: a spot's handles (['intensity'])`.

- [ ] **Step 3: Implement**

In `pc/src/editor_light_handles.cpp`, in the anonymous namespace after `shadows_toggle`, add:

```cpp
/* A spot's or rect's range: a dot on its axis at its radius. */
Handle range_dot(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    return line_dot(v, r, "range", "radius", "Range", vec3(l.position), f, l.radius,
        [](const Drag& d, V3& p, V3& u) {
            V3 rt, up;
            frame_of(d.light, u, rt, up);
            p = vec3(d.light.position);
        },
        [](const Drag& d) { return d.light.radius; }, 16);
}
/* The side of a spot's cone facing the camera: across its axis and the view. */
V3 cone_side(const pc_overlay_view& v, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    float vr[3], vu[3], vf[3];
    rood_math::view_axes(v.view, v.projection, v.rect, vr, vu, vf);
    V3 s = { f[1] * vf[2] - f[2] * vf[1], f[2] * vf[0] - f[0] * vf[2], f[0] * vf[1] - f[1] * vf[0] };
    float n = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (n < 1e-3f) /* looking down its axis */
        return rt;
    return { s[0] / n, s[1] / n, s[2] / n };
}
float cone_angle(const Light& l, bool inner) { return inner ? (l.inner > 0 ? l.inner : rood_math::auto_inner(l.cone)) : l.cone; }
/* A cone handle: `share` of the radius along the axis, out to the cone's edge
 * (outer) or the inner cone's (inner) on the side facing the camera; dragged
 * across, its angle = 2 atan(offset / height) (Ctrl a tenth; Shift 5 degrees). */
Handle cone_handle(const pc_overlay_view& v, const Ref& r, const Light& l, bool inner)
{
    const float share = inner ? 0.3f : 0.5f;
    const char* field = inner ? "inner" : "cone";
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    float height = l.radius * share;
    V3 base = along(vec3(l.position), f, height), side = cone_side(v, l);
    Handle h;
    h.name = field, h.label = inner ? "Inner cone" : "Cone";
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(base, side, height * tanf(cone_angle(l, inner) * 0.5f * DEG)), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = inner ? IM_COL32(255, 170, 90, 255) : IM_COL32(120, 220, 255, 255);
    h.drag = [r, inner, share, field](const Drag& d) {
        V3 f, rt, up;
        frame_of(d.light, f, rt, up);
        float height = d.light.radius * share;
        V3 base = along(vec3(d.light.position), f, height), side = cone_side(d.view, d.light);
        float s0 = height * tanf(cone_angle(d.light, inner) * 0.5f * DEG);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, base.data(), side.data());
        float t1 = rood_math::line_param(d.origin, d.dir, base.data(), side.data());
        float s = rood_math::linear_drag(s0, t0, t1, d.fine);
        float angle = 2 * atanf(fmaxf(s, 0.0f) / height) / DEG;
        rood_handles::set(r, field, Value::scalar(d.snap ? rood_math::snap_step(angle, 5) : angle));
    };
    h.readout = [r, field]() { return rood_handles::readout(r, field); };
    pc_overlay_view view = v;
    h.where = [view, base, side, height](const Value& a, float out[2]) {
        return pixel(view, along(base, side, height * tanf(a.x() * 0.5f * DEG)), out);
    };
    return h;
}
/* "Aim at surface": a crosshair left of the ring; a click arms it, the next
 * click on a surface points the spot there (editor_handles.cpp viewport_click). */
Handle aim_handle(const Ref& r, float cx, float cy, float ring_radius)
{
    Handle h;
    h.name = "aim", h.label = "Aim at surface";
    h.shape = rood_handles::SHAPE_CROSS;
    h.x = cx - ring_radius - 14 * em(), h.y = cy, h.radius = 6 * em();
    h.colour = IM_COL32(255, 255, 255, 230);
    h.click = [r]() { rood_handles::arm_aim(r); };
    return h;
}
```

In `rood_handles::light_handles`, after the `if (l.type == 0) { ... }` block add:

```cpp
    if (l.type == 1) {
        out.push_back(range_dot(v, r, l));
        out.push_back(cone_handle(v, r, l, false));
        out.push_back(cone_handle(v, r, l, true));
        out.push_back(aim_handle(r, c[0], c[1], intensity.radius));
    }
```

Replace `void rood_handles::light_extras(const pc_overlay_view&, int, void*) {}` with:

```cpp
/* A selected light's shapes besides its handles: a spot's inner cone (its edges inside the cone's). */
void rood_handles::light_extras(const pc_overlay_view& v, int index, void* draw_list)
{
    if (index < 0 || index >= (int)lights.size())
        return;
    const Light& l = lights[index];
    ImDrawList* draw = (ImDrawList*)draw_list;
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    float apex[2];
    if (l.type != 1 || !pixel(v, vec3(l.position), apex))
        return;
    float spread = tanf(fminf(cone_angle(l, true), l.cone) * 0.5f * DEG) * l.radius;
    V3 tip = along(vec3(l.position), f, l.radius);
    for (int k = 0; k < 4; k++) {
        float a = k * 1.5707963f, px[2];
        if (pixel(v, along(along(tip, rt, cosf(a) * spread), up, sinf(a) * spread), px))
            draw->AddLine(ImVec2(apex[0], apex[1]), ImVec2(px[0], px[1]), IM_COL32(255, 170, 90, 150), 1.0f);
    }
}
```

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `1/1 passed`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_light_handles.cpp pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
Spot handles: range dot, outer and inner cone on the cone's side, aim at surface (crosshair, then a click)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 16: The rect's handles: edges (Alt symmetric), range, barn doors

**Files:**
- Modify: `pc/src/editor_light_handles.cpp` (include; new functions; `light_handles`; `light_extras` replaced), `pc/tests/editor_handles_smoke.py`

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_handles_smoke.py`, before `SECTIONS = ` add:

```python
def check_rect(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "rect"})
    schema(s, action="set", id=s.light, values={"radius": 300, "width": 128, "height": 96, "direction": [0, 1, 0],
                                                 "barn_angle": 45, "barn_length": 0})
    g.run(2)
    names = {h["name"] for h in handles(s)["handles"]}
    want = {"intensity", "width+", "width-", "height+", "height-", "range", "barn_angle", "barn_length"}
    s.check(want <= names, f"a rect's handles ({sorted(names)})")
    # Alt: both edges move, only the width changes.
    check_drag(s, s.light, "width+", "width", "Width", 192, 192, hold=["alt"])
    # A plain drag moves that edge only: the width and the centre change (the opposite edge stays).
    pos0 = value(s, s.light, "position")
    after = check_drag(s, s.light, "width+", "width", "Width", 2 * 192 - 128, 192, also=("position",))
    if after:
        moved = math.dist(after["position"], pos0)
        s.check(abs(moved - 32) < 2, f"the centre moved half the change (32 units: {moved:.1f})")
    check_drag(s, s.light, "height-", "height", "Height", 64, 64, hold=["alt"])
    check_drag(s, s.light, "range", "radius", "Range", 450, 450)
    check_drag(s, s.light, "barn_length", "barn_length", "Barn door length", 48, 48, tolerance=0.03)
    schema(s, action="set", id=s.light, values={"barn_length": 48})
    check_drag(s, s.light, "barn_angle", "barn_angle", "Barn door angle", 20, 20, tolerance=0.03)
    check_escape(s, s.light, "barn_angle", "barn_angle", 70)
    schema(s, action="reset", id=s.light, fields=["barn_length", "barn_angle"])
    schema(s, action="set", id=s.light, values={"type": "point"})
```

change `SECTIONS` to `[check_layer, check_point, check_spot, check_rect]`, and add to the docstring:

```
- rect: an edge with Alt changes only the width (both edges move); a plain
  edge drag moves that edge (the width and the centre, one step); the height,
  the range dot, the barn doors' length and angle (flaps on its edges), Escape.
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `0/1 passed`; `FAIL: a rect's handles (['intensity'])`.

- [ ] **Step 3: Implement**

In `pc/src/editor_light_handles.cpp`, add `#include "editor_transform.h"` after `#include "editor_scene.h"`, and in the anonymous namespace after `aim_handle` add:

```cpp
/* A rect's edge: its width (axis 0, along its right) or height (axis 1, up)
 * on the + or - side. A plain drag moves that edge only (the opposite edge
 * stays: the width and the centre change); Alt keeps the centre (both edges
 * move). Shift: multiples of 8 units. */
Handle edge_handle(const pc_overlay_view& v, const Ref& r, const Light& l, int axis, int sign)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    V3 u = axis == 0 ? rt : up, p = vec3(l.position);
    Handle h;
    h.name = std::string(axis == 0 ? "width" : "height") + (sign > 0 ? "+" : "-");
    h.label = axis == 0 ? "Width" : "Height";
    h.shape = rood_handles::SHAPE_SQUARE;
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(p, u, sign * l.size[axis] * 0.5f), px);
    h.x = px[0], h.y = px[1], h.radius = 4 * em();
    h.colour = IM_COL32(120, 220, 255, 255);
    const char* field = axis == 0 ? "width" : "height";
    h.drag = [r, axis, sign, field](const Drag& d) {
        V3 f, rt, up;
        frame_of(d.light, f, rt, up);
        V3 u = axis == 0 ? rt : up, p = vec3(d.light.position);
        float t0 = rood_math::line_param(d.press_origin, d.press_dir, p.data(), u.data());
        float t1 = rood_math::line_param(d.origin, d.dir, p.data(), u.data());
        float moved = (t1 - t0) * (d.fine ? 0.1f : 1.0f) * sign; /* the edge's move, outward */
        float w0 = d.light.size[axis], w = d.symmetric ? w0 + 2 * moved : w0 + moved;
        if (d.snap)
            w = rood_math::snap_step(w, 8);
        w = fminf(fmaxf(w, rood_transform::SIZE_MIN), rood_transform::SIZE_MAX);
        rood_handles::set(r, field, Value::scalar(w));
        if (!d.symmetric) { /* the opposite edge stays: the centre moves half the change toward this edge */
            V3 c = along(p, u, sign * (w - w0) * 0.5f);
            rood_handles::set(r, "position", Value::vec(c[0], c[1], c[2]));
        }
    };
    h.readout = [r, field]() { return rood_handles::readout(r, field); };
    pc_overlay_view view = v;
    /* where: the symmetric drag's pixel for a width (a plain drag to where(2w - w0) gives w) */
    h.where = [view, p, u, sign](const Value& value, float out[2]) { return pixel(view, along(p, u, sign * value.x() * 0.5f), out); };
    return h;
}
/* Barn doors: the flap on an edge (outward `out`) leans from the rect's
 * facing by the doors' angle. The handles sit at the right and top flaps'
 * tips, a quarter of the rect's smaller side past the doors' length (clear of
 * the edge handles when the doors are short). */
V3 flap_dir(const V3& out, const V3& f, float angle)
{
    float t = fminf(fmaxf(angle, 0.0f), 90.0f) * DEG;
    return { out[0] * sinf(t) + f[0] * cosf(t), out[1] * sinf(t) + f[1] * cosf(t), out[2] * sinf(t) + f[2] * cosf(t) };
}
float barn_clear(const Light& l) { return 0.25f * fminf(l.size[0], l.size[1]); }
/* The angle: the right flap's tip turns about its hinge, in the plane of the
 * rect's right and facing (Ctrl a tenth; Shift 5 degrees). */
Handle barn_angle_handle(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    V3 hinge = along(vec3(l.position), rt, l.size[0] * 0.5f);
    float reach = l.barn_length + barn_clear(l);
    Handle h;
    h.name = "barn_angle", h.label = "Barn door angle";
    float px[2] = { -1e4f, -1e4f };
    pixel(v, along(hinge, flap_dir(rt, f, l.barn_angle), reach), px);
    h.x = px[0], h.y = px[1], h.radius = 5 * em();
    h.colour = IM_COL32(200, 200, 215, 255);
    h.drag = [r](const Drag& d) {
        V3 f, rt, up;
        frame_of(d.light, f, rt, up);
        V3 hinge = along(vec3(d.light.position), rt, d.light.size[0] * 0.5f);
        float a[3], b[3];
        if (!rood_math::plane_hit(d.press_origin, d.press_dir, hinge.data(), up.data(), a)
            || !rood_math::plane_hit(d.origin, d.dir, hinge.data(), up.data(), b))
            return;
        auto angle_of = [&](const float* q) {
            float x = (q[0] - hinge[0]) * rt[0] + (q[1] - hinge[1]) * rt[1] + (q[2] - hinge[2]) * rt[2];
            float z = (q[0] - hinge[0]) * f[0] + (q[1] - hinge[1]) * f[1] + (q[2] - hinge[2]) * f[2];
            return atan2f(x, z) / DEG;
        };
        float t = d.light.barn_angle + (angle_of(b) - angle_of(a)) * (d.fine ? 0.1f : 1.0f);
        rood_handles::set(r, "barn_angle", Value::scalar(d.snap ? rood_math::snap_step(t, 5) : t));
    };
    h.readout = [r]() { return rood_handles::readout(r, "barn_angle"); };
    pc_overlay_view view = v;
    h.where = [view, hinge, rt, f, reach](const Value& value, float out[2]) {
        return pixel(view, along(hinge, flap_dir(rt, f, value.x()), reach), out);
    };
    return h;
}
/* The length: the top flap's tip, along the flap (Shift 4 units). */
Handle barn_length_handle(const pc_overlay_view& v, const Ref& r, const Light& l)
{
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    V3 dir = flap_dir(up, f, l.barn_angle);
    V3 base = along(along(vec3(l.position), up, l.size[1] * 0.5f), dir, barn_clear(l));
    Handle h = line_dot(v, r, "barn_length", "barn_length", "Barn door length", base, dir, l.barn_length,
        [](const Drag& d, V3& p, V3& u) {
            V3 f, rt, up;
            frame_of(d.light, f, rt, up);
            u = flap_dir(up, f, d.light.barn_angle);
            p = along(along(vec3(d.light.position), up, d.light.size[1] * 0.5f), u, barn_clear(d.light));
        },
        [](const Drag& d) { return d.light.barn_length; }, 4);
    h.colour = IM_COL32(200, 200, 215, 255);
    return h;
}
```

In `rood_handles::light_handles`, after the `if (l.type == 1) { ... }` block add:

```cpp
    if (l.type == 2) {
        for (int axis = 0; axis < 2; axis++)
            for (int sign = 1; sign >= -1; sign -= 2)
                out.push_back(edge_handle(v, r, l, axis, sign));
        out.push_back(range_dot(v, r, l));
        out.push_back(barn_length_handle(v, r, l));
        out.push_back(barn_angle_handle(v, r, l));
    }
```

Replace the whole `rood_handles::light_extras` with:

```cpp
/* A selected light's shapes besides its handles: a spot's inner cone (its
 * edges inside the cone's), a rect's four barn door flaps. */
void rood_handles::light_extras(const pc_overlay_view& v, int index, void* draw_list)
{
    if (index < 0 || index >= (int)lights.size())
        return;
    const Light& l = lights[index];
    ImDrawList* draw = (ImDrawList*)draw_list;
    V3 f, rt, up;
    frame_of(l, f, rt, up);
    float apex[2];
    if (l.type == 1 && pixel(v, vec3(l.position), apex)) {
        float spread = tanf(fminf(cone_angle(l, true), l.cone) * 0.5f * DEG) * l.radius;
        V3 tip = along(vec3(l.position), f, l.radius);
        for (int k = 0; k < 4; k++) {
            float a = k * 1.5707963f, px[2];
            if (pixel(v, along(along(tip, rt, cosf(a) * spread), up, sinf(a) * spread), px))
                draw->AddLine(ImVec2(apex[0], apex[1]), ImVec2(px[0], px[1]), IM_COL32(255, 170, 90, 150), 1.0f);
        }
    }
    if (l.type != 2 || l.barn_length <= 0)
        return;
    for (int e = 0; e < 4; e++) { /* +right, -right, +up, -up */
        const V3& axis = e < 2 ? rt : up;
        const V3& across = e < 2 ? up : rt;
        float sign = e % 2 ? -1.0f : 1.0f, half = l.size[e < 2 ? 0 : 1] * 0.5f, span = l.size[e < 2 ? 1 : 0] * 0.5f;
        V3 outward = { axis[0] * sign, axis[1] * sign, axis[2] * sign };
        V3 hinge = along(vec3(l.position), outward, half), flap = flap_dir(outward, f, l.barn_angle);
        V3 corner[4] = { along(hinge, across, -span), along(hinge, across, span), {}, {} };
        corner[2] = along(corner[1], flap, l.barn_length);
        corner[3] = along(corner[0], flap, l.barn_length);
        ImVec2 q[4];
        bool seen = true;
        for (int k = 0; k < 4; k++) {
            float px[2];
            seen &= pixel(v, corner[k], px);
            q[k] = ImVec2(px[0], px[1]);
        }
        if (seen) {
            draw->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(60, 60, 70, 90));
            draw->AddPolyline(q, 4, IM_COL32(200, 200, 210, 220), ImDrawFlags_Closed, 1.5f);
        }
    }
}
```

- [ ] **Step 4: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `1/1 passed`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_light_handles.cpp pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
Rect handles: edges (one edge, Alt symmetric), range dot, barn door flaps with angle and length handles

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 17: The sun: selectable, its disc dragged across a sky dome

**Files:**
- Modify: `pc/src/editor_selection.h`, `pc/src/editor_selection.cpp` (`ref_of`, `kind_name`), `pc/src/environment_internal.h` (`rood_env`), `pc/src/environment.cpp` (line 48; line 245; line 283; `pc_env_draw` lines 329-412 removed; new accessors after `} // namespace rood_env`, line 180), `pc/src/editor_scene.cpp` (`all_objects`, `exists`, `label`, `in_box`), `pc/src/editor_details.cpp` (`rood_details::draw`, after the glow branch), `pc/src/editor_handles.h`, `pc/src/editor_handles.cpp` (`build`, `restore`, `frame`), `pc/src/editor_light_handles.cpp` (new `sun_icon`, `sun_handles`), `pc/src/lights.cpp` (line 35 and the `pc_env_draw` call removed)
- Test: `pc/tests/selection_probe.cpp`, `pc/tests/editor_handles_smoke.py`

- [ ] **Step 1: Write the failing checks**

In `pc/tests/selection_probe.cpp` `references()`, after the check `"an empty name or another kind is no reference"` add:

```cpp
    check(ref_of("env:sun").kind == KIND_SUN && !ref_of("env:sky") && !strcmp(kind_name(KIND_SUN), "sun"),
        "the sun is env:sun (the sky is no reference)");
```

In `pc/tests/editor_handles_smoke.py`, before `SECTIONS = ` add:

```python
def check_sun(s):
    g = s.game

    def sun():
        return g.call("environment")["sun"]

    g.call("environment", sun={"enabled": True, "azimuth": 180, "elevation": 30, "intensity": 1, "color": [1, 1, 1]})
    g.run(4)
    o = s.objects()
    if not s.check("env:sun" in o and o["env:sun"]["screen"] is not None, "the sun's icon is on its dome in the view"):
        return
    s.click(o["env:sun"]["screen"])
    g.run(2)
    s.check(s.select()["selection"] == ["env:sun"], "a click on it selects the sun")
    names = {h["name"] for h in handles(s)["handles"]}
    s.check({"sun", "intensity"} <= names, f"the sun's handles ({sorted(names)})")
    h = handle(s, "sun")
    target = handles(s, action="where", handle="sun", value=[200, 40])["screen"]
    done = s.history()["done"]
    drag(s, h["screen"], target)
    after, hist = sun(), s.history()
    s.check(abs(after["azimuth"] - 200) < 1.5 and abs(after["elevation"] - 40) < 1.5 and hist["done"] == done + 1
            and hist["undo"] == "Edit Sun direction",
            f"its disc dragged across the dome: azimuth {after['azimuth']}, elevation {after['elevation']} (200, 40); "
            f"{hist['undo']!r}")
    g.call("editor_undo", action="undo")
    s.check(abs(sun()["azimuth"] - 180) < 0.5 and abs(sun()["elevation"] - 30) < 0.5, "undo turns it back")
    ring = handle(s, "intensity")
    drag(s, ring["screen"], [ring["screen"][0] + 100, ring["screen"][1]])
    s.check(close(sun()["intensity"], 2, 0.01) and s.history()["undo"] == "Edit Sun intensity",
            f"its ring: intensity 2 ({sun()['intensity']}, {s.history()['undo']!r})")
    g.call("editor_undo", action="undo")
    h = handle(s, "sun")
    target = handles(s, action="where", handle="sun", value=[160, 20])["screen"]
    drag(s, h["screen"], target, release=False)
    s.keys(["escape"])
    s.mouse(target, down=False)
    s.check(abs(sun()["azimuth"] - 180) < 0.5 and abs(sun()["elevation"] - 30) < 0.5, "Escape mid-drag puts the sun back")
    g.call("environment", remove="sun")
    s.select(action="set", ids=[s.light])
```

change `SECTIONS` to `[check_layer, check_point, check_spot, check_rect, check_sun]`, and add to the docstring:

```
- sun: its icon on the sky dome selects it (env:sun); its disc dragged
  across the dome sets azimuth and elevation in one undo step; its ring its
  intensity; Escape puts it back.
```

- [ ] **Step 2: Run them and see them fail**

```bash
ninja -C pc/build selection_probe
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `error: 'KIND_SUN' was not declared in this scope`; the smoke `0/1 passed` with `FAIL: the sun's icon is on its dome in the view`.

- [ ] **Step 3: The sun as a reference**

In `pc/src/editor_selection.h`, change the enum to

```cpp
enum Kind { KIND_NONE, KIND_LIGHT, KIND_ACTOR, KIND_GLOW, KIND_VOLUME, KIND_SUN };
```

and the `ref_of` comment to `/* A reference from an item id, by its prefix ("env:sun": the room's sun or moon; KIND_NONE when unknown). */`, and `kind_name`'s to `/* "light", "actor", "glow", "fog", "sun", "none" */`. In `pc/src/editor_selection.cpp` `ref_of`, before the `for` loop add

```cpp
    if (id == "env:sun") /* the room's sun or moon (its sky and fog are no objects) */
        return Ref { KIND_SUN, id };
```

and change `kind_name` to

```cpp
const char* kind_name(Kind k)
{
    return k == KIND_LIGHT ? "light" : k == KIND_ACTOR ? "actor" : k == KIND_GLOW ? "glow" : k == KIND_VOLUME ? "fog"
        : k == KIND_SUN                                                                              ? "sun"
                                                                                                     : "none";
}
```

```bash
ninja -C pc/build selection_probe && pc/build/selection_probe | tail -1
```

Expected: `PASS: ...` (the probe's last line).

- [ ] **Step 4: The environment's sun for the editor**

In `pc/src/environment_internal.h`, namespace `rood_env`, after `void set_text(const std::string& text);` add:

```cpp
/* The room's sun (or moon) for the editor's handles (editor_light_handles.cpp). */
struct SunState {
    bool defined = false, enabled = true;
    float azimuth = 180, elevation = 45; /* degrees (rood_math::sun_direction) */
    float color[3] = { 1, 1, 1 };
    float intensity = 1, angle = 0.53f;
    bool shadows = true;
};
SunState sun_state();
void set_sun_state(const SunState& s); /* the room unsaved */
bool sun_icon_shown(); /* Atmosphere > "Sun in the view" */
```

In `pc/src/environment.cpp`, after `} // namespace rood_env` (line 180) add:

```cpp
rood_env::SunState rood_env::sun_state()
{
    SunState s;
    s.defined = sun.defined, s.enabled = sun.enabled, s.azimuth = sun.azimuth, s.elevation = sun.elevation;
    memcpy(s.color, sun.color, sizeof s.color);
    s.intensity = sun.intensity, s.angle = sun.angle, s.shadows = sun.shadows;
    return s;
}
void rood_env::set_sun_state(const SunState& s)
{
    sun.defined = s.defined, sun.enabled = s.enabled, sun.azimuth = s.azimuth, sun.elevation = s.elevation;
    memcpy(sun.color, s.color, sizeof sun.color);
    sun.intensity = s.intensity, sun.angle = s.angle, sun.shadows = s.shadows;
    pc_lights_mark_dirty();
}
bool rood_env::sun_icon_shown() { return gizmo; }
```

Delete `int page_frame = -100; /* the Atmosphere tab was drawn this frame (the gizmo shows) */` (line 48), change `bool gizmo = true;` to `bool gizmo = true; /* the sun's icon in the editor's view (its handles: editor_light_handles.cpp) */`, delete `page_frame = ImGui::GetFrameCount();` in `pc_environment_page`, replace `ImGui::Checkbox("Direction gizmo in the view", &gizmo);` with

```cpp
            ImGui::Checkbox("Sun in the view", &gizmo);
            tooltip("Its disc on a sky dome in the view: click it, then drag it (azimuth and elevation), its ring\n"
                    "(intensity) or its swatch (colour).");
```

and the tooltip of Azimuth `"... Drag the sun in the view too."` stays. Delete the whole section from `/* ---- editor: the sun direction gizmo ----` through the end of `pc_env_draw` (the line before `/* ---- RPC environment ---- */`). In `pc/src/lights.cpp`, delete line 35 (`extern "C" void pc_env_draw(...); /* environment.cpp */`) and the call `pc_env_draw(ov.view, ov.projection, ov.rect); /* the sun direction gizmo (environment.cpp) */` in `pc_lights_viewport`.

- [ ] **Step 5: The sun in the scene and Details**

In `pc/src/editor_scene.cpp`: in `all_objects()`, before `static pc_glow_info glows[160];` add

```cpp
    if (rood_env::sun_state().defined)
        out.push_back(Ref { rood_selection::KIND_SUN, "env:sun" });
```

change `exists` to

```cpp
bool rood_scene::exists(const Ref& r)
{
    if (r.kind == rood_selection::KIND_SUN)
        return rood_env::sun_state().defined;
    return r.kind == rood_selection::KIND_GLOW || index_of(r) >= 0;
}
```

in `label`'s switch, before `default:` add

```cpp
    case rood_selection::KIND_SUN:
        return "Sun";
```

and in `in_box` change `c.ref.kind != rood_selection::KIND_GLOW` to `c.ref.kind != rood_selection::KIND_GLOW && c.ref.kind != rood_selection::KIND_SUN`.

In `pc/src/editor_details.cpp` `rood_details::draw`, after the glow branch (`pc_lights_glow_details(); ImGui::PopID(); return; }`) add:

```cpp
    if (subject.kind == rood_selection::KIND_SUN) { /* no schema kind yet: its settings are the Atmosphere tab's */
        ImGui::TextWrapped("The room's sun (or moon). In the view: drag its disc across the sky dome (azimuth and "
                           "elevation), its ring (intensity), its swatch (colour). Every setting: the Atmosphere tab.");
        ImGui::PopID();
        return;
    }
```

- [ ] **Step 6: The sun's handles**

In `pc/src/editor_handles.h`, struct `Drag`, after `rood_lights::Light light;` add

```cpp
    rood_env::SunState sun; /* the sun as the drag began */
```

and after the `light_extras` declaration add

```cpp
void sun_handles(const pc_overlay_view& v, std::vector<Handle>& out);
/* Every frame the sun shows: its disc on the sky dome, pickable (env:sun). */
void sun_icon(const pc_overlay_view& v, void* draw_list);
```

In `pc/src/editor_handles.cpp`: in `build`, before `return "unsupported";` add

```cpp
    if (p.kind == rood_selection::KIND_SUN && rood_env::sun_state().defined) {
        rood_handles::sun_handles(v, out);
        return nullptr;
    }
```

in `restore`, after the light line add

```cpp
    else if (a.subject.kind == rood_selection::KIND_SUN)
        rood_env::set_sun_state(a.drag.sun);
```

in `frame`, after `hovered = -1;` add

```cpp
    sun_icon(v, draw); /* the sun on its dome, selected or not */
```

and at the press, before `pc_undo_begin(("Edit " + h.label).c_str()); /* the whole drag is one undo step */` add

```cpp
        d.sun = rood_env::sun_state();
```

In `pc/src/editor_light_handles.cpp`, at the end of the file add:

```cpp
/* The sun on its sky dome (rood_math::dome_centre: ahead of the camera): its
 * disc where it shines from, a dome outline (horizon and its meridian) while
 * selected. Registered as an icon: a click selects it (env:sun). */
void rood_handles::sun_icon(const pc_overlay_view& v, void* draw_list)
{
    rood_env::SunState s = rood_env::sun_state();
    Ref r { rood_selection::KIND_SUN, "env:sun" };
    float c[3];
    if (!s.defined || !s.enabled || !rood_env::sun_icon_shown() || rood_selection::hidden(r)
        || !rood_math::dome_centre(v.view, v.projection, v.rect, c))
        return;
    ImDrawList* draw = (ImDrawList*)draw_list;
    bool chosen = rood_selection::contains(r);
    if (chosen) {
        ImVec2 horizon[49], meridian[25];
        int n = 0, m = 0;
        for (int k = 0; k <= 48; k++) {
            float a = k * 6.2831853f / 48, px[2];
            V3 d = { cosf(a), 0, sinf(a) };
            if (pixel(v, along(vec3(c), d, rood_math::DOME_RADIUS), px))
                horizon[n++] = ImVec2(px[0], px[1]);
        }
        for (int k = 0; k <= 24; k++) {
            float d[3], px[2];
            rood_math::sun_direction(s.azimuth, k * 90.0f / 24, d);
            if (pixel(v, along(vec3(c), vec3(d), rood_math::DOME_RADIUS), px))
                meridian[m++] = ImVec2(px[0], px[1]);
        }
        if (n > 1)
            draw->AddPolyline(horizon, n, IM_COL32(255, 220, 120, 120), 0, 1.5f);
        if (m > 1)
            draw->AddPolyline(meridian, m, IM_COL32(255, 220, 120, 90), 0, 1.0f);
    }
    float d[3], px[2];
    rood_math::sun_direction(s.azimuth, s.elevation, d);
    if (!pixel(v, along(vec3(c), vec3(d), rood_math::DOME_RADIUS), px))
        return;
    ImVec2 b(px[0], px[1]);
    ImU32 col = colour_of(s.color);
    float rr = 9 * em();
    draw->AddCircleFilled(b, rr, col, 24);
    draw->AddCircle(b, rr, chosen ? IM_COL32(255, 255, 0, 255) : IM_COL32(30, 30, 30, 200), 24, 2.0f);
    for (int i = 0; i < 8; i++) { /* rays */
        float t = i * 0.785398f;
        draw->AddLine(ImVec2(b.x + cosf(t) * rr * 1.3f, b.y + sinf(t) * rr * 1.3f),
            ImVec2(b.x + cosf(t) * rr * 1.8f, b.y + sinf(t) * rr * 1.8f), col, 1.5f);
    }
    char label[48];
    snprintf(label, sizeof label, "sun %.0f / %.0f deg", s.azimuth, s.elevation);
    draw->AddText(ImVec2(b.x + rr * 2, b.y - rr), IM_COL32(255, 240, 200, 230), label);
    rood_scene::icon(r, b.x, b.y, rr + 4, true);
}
/* The selected sun: its disc (dragged across the dome: azimuth and elevation
 * together; Ctrl a tenth; Shift 15 / 5 degrees) and its intensity ring (0..4). */
void rood_handles::sun_handles(const pc_overlay_view& v, std::vector<Handle>& out)
{
    rood_env::SunState s = rood_env::sun_state();
    float c[3], d[3], px[2];
    if (!rood_math::dome_centre(v.view, v.projection, v.rect, c))
        return;
    rood_math::sun_direction(s.azimuth, s.elevation, d);
    if (!pixel(v, along(vec3(c), vec3(d), rood_math::DOME_RADIUS), px))
        return;
    out.push_back(ring(px[0], px[1], colour_of(s.color), "Sun intensity", [](const Drag& g) { return g.sun.intensity; },
        [](float x) {
            rood_env::SunState t = rood_env::sun_state();
            t.intensity = fminf(x, 4.0f);
            rood_env::set_sun_state(t);
        },
        []() { return rood_env::sun_state().intensity; },
        []() {
            char text[32];
            snprintf(text, sizeof text, "Intensity %.2f", rood_env::sun_state().intensity);
            return std::string(text);
        }));
    Handle h;
    h.name = "sun", h.label = "Sun direction";
    h.shape = rood_handles::SHAPE_DISC;
    h.x = px[0], h.y = px[1], h.radius = 9 * em();
    h.colour = colour_of(s.color);
    h.drag = [](const Drag& g) {
        float centre[3], n0[3], f0[3], n1[3], f1[3], was[3], prev[3], a0, e0, a1, e1;
        if (!rood_math::dome_centre(g.view.view, g.view.projection, g.view.rect, centre))
            return;
        rood_env::SunState now = rood_env::sun_state();
        rood_math::sun_direction(g.sun.azimuth, g.sun.elevation, was);
        rood_math::sun_direction(now.azimuth, now.elevation, prev);
        rood_math::sphere_hits(g.press_origin, g.press_dir, centre, rood_math::DOME_RADIUS, n0, f0);
        rood_math::sphere_hits(g.origin, g.dir, centre, rood_math::DOME_RADIUS, n1, f1);
        /* A ray meets the dome twice: the point nearer the sun as it was (the
         * press) and as it is (now), so the disc follows the pointer smoothly. */
        auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        rood_math::sun_angles(dot(n0, was) >= dot(f0, was) ? n0 : f0, &a0, &e0);
        rood_math::sun_angles(dot(n1, prev) >= dot(f1, prev) ? n1 : f1, &a1, &e1);
        float k = g.fine ? 0.1f : 1.0f, da = a1 - a0;
        da = da > 180 ? da - 360 : da < -180 ? da + 360 : da;
        now.azimuth = g.sun.azimuth + da * k;
        now.elevation = g.sun.elevation + (e1 - e0) * k;
        if (g.snap)
            now.azimuth = rood_math::snap_step(now.azimuth, 15), now.elevation = rood_math::snap_step(now.elevation, 5);
        now.azimuth = fmodf(fmodf(now.azimuth, 360.0f) + 360.0f, 360.0f);
        now.elevation = fminf(90.0f, fmaxf(0.0f, now.elevation));
        rood_env::set_sun_state(now);
    };
    h.readout = []() {
        rood_env::SunState t = rood_env::sun_state();
        char text[48];
        snprintf(text, sizeof text, "Sun %.0f / %.0f deg", t.azimuth, t.elevation);
        return std::string(text);
    };
    pc_overlay_view view = v;
    h.where = [view](const Value& value, float px_out[2]) {
        float centre[3], dir[3];
        if (!rood_math::dome_centre(view.view, view.projection, view.rect, centre))
            return false;
        rood_math::sun_direction(value.at(0), value.at(1), dir);
        return pixel(view, along(vec3(centre), vec3(dir), rood_math::DOME_RADIUS), px_out);
    };
    out.push_back(h);
}
```

- [ ] **Step 7: Run them and see them pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest editor_handles editor_selection editor_schema sky atmosphere
```

Expected: build exit 0; every suite passes (`sky_smoke` and `atmosphere_smoke` still see the room's sun the same way; the old gizmo is gone).

- [ ] **Step 8: Commit**

```bash
git add pc/src/editor_selection.h pc/src/editor_selection.cpp pc/src/environment_internal.h pc/src/environment.cpp pc/src/editor_scene.cpp pc/src/editor_details.cpp pc/src/editor_handles.h pc/src/editor_handles.cpp pc/src/editor_light_handles.cpp pc/src/lights.cpp pc/tests/selection_probe.cpp pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
The sun: selectable (env:sun), its disc dragged across a sky dome (azimuth and elevation), its intensity ring; the old sun gizmo goes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---
### Task 18: Colour maths: Kelvin and tint, hex, display to linear

**Files:**
- Modify: `pc/src/editor_math.h` (a colour section before `} // namespace rood_math`), `pc/src/editor_math.cpp` (includes; the functions at the end of the namespace), `pc/tests/editor_tools_probe.cpp`

- [ ] **Step 1: Write the failing checks**

In `pc/tests/editor_tools_probe.cpp`, before `int main()` add:

```cpp
/* The colour popover's maths (editor_math.h). */
static void colours()
{
    float c[3], k, t;
    rood_math::kelvin_tint(6500, 0, c);
    check(fmaxf(c[0], fmaxf(c[1], c[2])) == 1 && c[2] > 0.9f, "6500 K is near white, brightest channel 1");
    rood_math::kelvin_tint(1900, 0, c);
    check(c[0] == 1 && c[1] < 0.7f && c[2] < 0.3f, "1900 K is orange");
    rood_math::kelvin_tint(2700, 0.4f, c);
    rood_math::estimate_kelvin_tint(c, &k, &t);
    check(fabsf(k - 2700) < 80 && fabsf(t - 0.4f) < 0.05f, "Kelvin and tint back from the colour");
    float g[3], m[3];
    rood_math::kelvin_tint(5600, 1, g);
    rood_math::kelvin_tint(5600, -1, m);
    check(g[1] > m[1] && m[0] >= g[0] - 1e-6f, "tint: +1 green, -1 magenta");
    check(near(rood_math::kelvin_strip_u(1000), 0) && near(rood_math::kelvin_strip_u(12000), 1)
            && near(rood_math::kelvin_strip_k(rood_math::kelvin_strip_u(3200)), 3200, 0.5f),
        "the Kelvin strip: 1000 K to 12000 K");
    float h[3];
    check(rood_math::parse_hex("#4080FF", h) && near3(h, 64 / 255.0f, 128 / 255.0f, 1) && rood_math::parse_hex("4080ff", h)
            && !rood_math::parse_hex("#40", h),
        "hex parses with or without #");
    check(rood_math::format_hex(h) == "#4080FF", "and formats");
    check(near(rood_math::srgb_to_linear(0.5f), 0.21404f, 1e-4f) && rood_math::srgb_to_linear(0) == 0
            && near(rood_math::srgb_to_linear(1), 1),
        "display to linear (sRGB)");
    float n[3] = { 0.2f, 0.4f, 0.1f };
    rood_math::normalize_colour(n);
    check(near3(n, 0.5f, 1, 0.25f), "brightest channel 1");
}
```

and in `main`, after `light_shapes();` add `colours();`.

- [ ] **Step 2: Run it and see it fail**

```bash
ninja -C pc/build editor_tools_probe
```

Expected: `error: 'kelvin_tint' is not a member of 'rood_math'`.

- [ ] **Step 3: Implement**

In `pc/src/editor_math.h`, before `} // namespace rood_math` add:

```cpp
/* ---- colour (the colour popover, editor_colour.cpp) ---- */
/* A blackbody colour (prefab::kelvin, 1000..12000 K) with a tint (-1 magenta
 * .. +1 green: green x (1 + 0.25 tint)), brightest channel 1. */
void kelvin_tint(float kelvin, float tint, float out[3]);
/* The nearest Kelvin (on the strip's grid) and tint for a colour. */
void estimate_kelvin_tint(const float rgb[3], float* kelvin, float* tint);
/* The Kelvin strip: 0 at 1000 K, 1 at 12000 K, even in mired (1e6 / K). */
float kelvin_strip_u(float kelvin);
float kelvin_strip_k(float u);
bool parse_hex(const std::string& text, float out[3]); /* "#4080ff", "4080FF" */
std::string format_hex(const float rgb[3]); /* "#4080FF" (0..1 clamped) */
float srgb_to_linear(float c); /* a display value (sRGB) to linear light */
void normalize_colour(float rgb[3]); /* brightest channel 1 (black stays black) */
```

In `pc/src/editor_math.cpp`, add `#include <ctype.h>` after `#include <math.h>` and `#include "light_prefabs.h" /* prefab::kelvin */` after `#include "editor_picking.h"`, and before the closing `} // namespace rood_math` add:

```cpp
void normalize_colour(float rgb[3])
{
    float m = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
    if (m > 1e-6f)
        for (int k = 0; k < 3; k++)
            rgb[k] /= m;
}
void kelvin_tint(float kelvin, float tint, float out[3])
{
    prefab::kelvin(fminf(fmaxf(kelvin, 1000.0f), 12000.0f), out);
    out[1] *= 1 + 0.25f * fminf(fmaxf(tint, -1.0f), 1.0f);
    normalize_colour(out);
}
float kelvin_strip_u(float kelvin) { return (1000.0f - 1e6f / kelvin) / (1000.0f - 1e6f / 12000.0f); }
float kelvin_strip_k(float u) { return 1e6f / (1000.0f - u * (1000.0f - 1e6f / 12000.0f)); }
void estimate_kelvin_tint(const float rgb[3], float* kelvin, float* tint)
{
    float c[3] = { rgb[0], rgb[1], rgb[2] };
    normalize_colour(c);
    float best = 1e30f;
    *kelvin = 6500, *tint = 0;
    for (int i = 0; i <= 400; i++) {
        float k = kelvin_strip_k(i / 400.0f), b[3];
        prefab::kelvin(k, b);
        float rb = b[0] * b[0] + b[2] * b[2];
        if (rb < 1e-9f || b[1] < 1e-6f)
            continue;
        float s = (c[0] * b[0] + c[2] * b[2]) / rb; /* red and blue set the scale, green the tint */
        if (s <= 0)
            continue;
        float t = fminf(fmaxf((c[1] / (s * b[1]) - 1) / 0.25f, -1.0f), 1.0f), g = s * b[1] * (1 + 0.25f * t);
        float e = (c[0] - s * b[0]) * (c[0] - s * b[0]) + (c[2] - s * b[2]) * (c[2] - s * b[2]) + (c[1] - g) * (c[1] - g);
        if (e < best)
            best = e, *kelvin = k, *tint = t;
    }
}
bool parse_hex(const std::string& text, float out[3])
{
    const char* p = text.c_str();
    while (*p == ' ' || *p == '#')
        p++;
    int digits = 0;
    while (isxdigit((unsigned char)p[digits]))
        digits++;
    unsigned v[3];
    char rest[2];
    if (digits != 6 || sscanf(p, "%2x%2x%2x%1s", &v[0], &v[1], &v[2], rest) != 3)
        return false;
    for (int k = 0; k < 3; k++)
        out[k] = v[k] / 255.0f;
    return true;
}
std::string format_hex(const float rgb[3])
{
    int c[3];
    for (int k = 0; k < 3; k++)
        c[k] = (int)lroundf(fminf(fmaxf(rgb[k], 0.0f), 1.0f) * 255);
    char text[16];
    snprintf(text, sizeof text, "#%02X%02X%02X", c[0], c[1], c[2]);
    return text;
}
float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
```

- [ ] **Step 4: Run it and see it pass**

```bash
ninja -C pc/build editor_tools_probe && pc/build/editor_tools_probe
```

Expected: every line `ok:`, last line `PASS: editor tools maths`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/editor_math.h pc/src/editor_math.cpp pc/tests/editor_tools_probe.cpp
git commit -m "$(cat <<'EOF'
Editor maths: Kelvin and tint (and back), the Kelvin strip, hex, sRGB to linear, brightest-channel normalising

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 19: The colour popover and the swatches

**Files:**
- Create: `pc/src/editor_colour.h`, `pc/src/editor_colour.cpp`
- Modify: `pc/src/editor_light_handles.cpp` (include; `swatch`; `light_handles`, `sun_handles`), `pc/src/editor_handles.cpp` (include; `state_json`; RPC action `colour`), `pc/src/dev_overlay.cpp` (include; Escape chain; `rood_colour::draw()` after `viewport_keys();`), `pc/src/project_settings.cpp` (include line 89; parse line 387; write after `rood_selection::write_hidden(f);`), `pc/tools/build_boot.py`
- Test: `pc/tests/editor_handles_smoke.py`

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_handles_smoke.py`, before `SECTIONS = ` add:

```python
def colour(s, **params):
    return handles(s, action="colour", **params)["colour"]


def check_colour(s):
    g = s.game
    g.call("actors", time=0)
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "point"})
    schema(s, action="set", id=s.light, values={"color": [0.2, 0.4, 0.6]})
    g.run(2)
    s.click(handle(s, "colour")["screen"])
    st = handles(s)["colour"]
    s.check(st["open"] and st["subject"] == s.light and st["tab"] == "temperature",
            f"the swatch opens the popover on Temperature ({st['open']}, {st['tab']})")
    done = s.history()["done"]
    st = colour(s, kelvin=2700, tint=0)
    c = value(s, s.light, "color")
    s.check(close(c, st["colour"], 0.002) and close(c[0], 1) and c[0] > c[1] > c[2] and abs(st["kelvin"] - 2700) < 80,
            f"2700 K: a warm colour, brightest channel 1 ({c}; it reads back {st['kelvin']:.0f} K)")
    s.check(s.history()["done"] == done + 1 and s.history()["undo"] == "Edit Colour", "one undo step, Edit Colour")
    g.run(2)
    st = handles(s)["colour"]
    s.click(centre(st["rects"]["stop_1900"]))
    st = handles(s)["colour"]
    s.check(abs(st["kelvin"] - 1900) < 60 and value(s, s.light, "color")[2] < 0.3,
            f"the Candle stop: 1900 K ({st['kelvin']:.0f})")
    s.click(centre(st["rects"]["hex"]))  # the Hex field: typed, then Enter
    g.call("editor_input", text="#4080FF")
    s.keys(["enter"])
    c, st = value(s, s.light, "color"), handles(s)["colour"]
    blue = [64 / 255, 128 / 255, 1.0]
    s.check(close(c, blue, 0.005) and close(st["recent"][0], blue, 0.005),
            f"Hex #4080FF typed: the colour ({c}), first of the recent colours")
    candle = next(a for a in g.call("actors")["actors"] if a["id"] == s.actor)["lights"][0]["color"]
    k = next((i for i, p in enumerate(st["palette"]) if close(p, candle, 0.02)), None)
    if s.check(k is not None, f"the room's palette has the candle's colour ({st['palette']})"):
        done = s.history()["done"]
        s.click(centre(st["rects"][f"palette{k}"]))
        s.check(close(value(s, s.light, "color"), st["palette"][k], 0.002) and s.history()["done"] == done + 1,
                "a palette swatch clicked: the light takes it, one undo step")
    st = handles(s)["colour"]
    j = next((i for i, p in enumerate(st["recent"]) if close(p, blue, 0.005)), None)
    if s.check(j is not None, f"the hex colour is among the recent ones ({st['recent']})"):
        s.click(centre(st["rects"][f"recent{j}"]))
        s.check(close(value(s, s.light, "color"), blue, 0.005), "a recent colour clicked")
    s.click(centre(handles(s)["colour"]["rects"]["tab_colour"]))
    g.run(2)
    st = handles(s)["colour"]
    s.check(st["tab"] == "colour" and st["tabs"][0] == 1, f"the Colour tab, remembered for point lights ({st['tabs']})")
    s.keys(["escape"])
    s.check(not handles(s)["colour"]["open"] and s.keys([])["visible"], "Escape closes the popover (the editor stays open)")
    schema(s, action="set", id=s.light, values={"type": "spot"})
    colour(s, open=True)
    g.run(3)
    s.check(handles(s)["colour"]["tab"] == "temperature", "a spot opens on its own last tab (Temperature)")
    colour(s, open=False)
    schema(s, action="set", id=s.light, values={"type": "point"})
    colour(s, open=True)
    g.run(3)
    s.check(handles(s)["colour"]["tab"] == "colour", "a point light again: Colour")
    colour(s, open=False)
    g.call("settings", action="save")
    text = pathlib.Path(s.settings).read_text()
    s.check("editor_colour_tabs 1 0 0 0" in text and "editor_recent_colours" in text,
            "the tabs and the recent colours are user settings")
```

change `SECTIONS` to `[check_layer, check_point, check_spot, check_rect, check_sun, check_colour]`, and add to the docstring:

```
- colour: the swatch opens the popover (Temperature first); Kelvin and tint
  give a warm colour in one undo step; the Candle stop; Hex typed; the room's
  palette and the recent colours; the tab remembered per light type, saved
  in the user settings; Escape closes it.
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `0/1 passed`; the log shows a `TypeError` (`'NoneType' object is not subscriptable`: no `colour` handle yet).

- [ ] **Step 3: The popover's header**

Create `pc/src/editor_colour.h`:

```cpp
/* The colour popover (pc/docs/plans/2026-10-15-artist-tools-design.md,
 * decision 7), opened from a light's or the sun's swatch in the view: a
 * Temperature tab (a Kelvin strip with named stops and a magenta-green tint)
 * and a Colour tab (a hue wheel and value); under both the room's palette
 * (the colours its lights use), recent colours and Hex / RGB. It remembers the
 * last tab per light type and the recent colours in the user settings
 * (editor_colour_tabs, editor_recent_colours: preferences, not undo steps).
 * Each change is one undo step ("Edit Colour"). Editor internals. */
#ifndef ROOD_EDITOR_COLOUR_H
#define ROOD_EDITOR_COLOUR_H
#include <stdio.h>
#include <string>

#include "editor_selection.h"
#include "light_prefabs.h"

namespace rood_colour {
enum Tab { COLOUR_TAB_TEMPERATURE, COLOUR_TAB_WHEEL };
void open(const rood_selection::Ref& target, float x, float y); /* a light or the sun, by the swatch at (x, y) */
bool is_open();
void close();
void draw(); /* each overlay frame (dev_overlay.cpp), after the view */
bool cancel(); /* Escape: closes it; true when it was open */
std::string state_json(); /* RPC editor_handles: "colour" */
/* RPC editor_handles {action "colour"}: open (bool: on the selection's
 * primary), tab ("temperature" | "colour"), kelvin and tint, hex, rgb,
 * palette (index), recent (index). False and *error when refused. */
bool rpc(const prefab::Json& params, std::string* error);
/* Settings lines (project_settings.cpp). */
bool parse_setting(const char* key, const char* rest);
void write_settings(FILE* f);
} // namespace rood_colour
#endif
```

- [ ] **Step 4: The popover**

Create `pc/src/editor_colour.cpp`:

```cpp
// The colour popover (editor_colour.h).
#include "editor_colour.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "editor_handles.h"
#include "editor_math.h"
#include "editor_scene.h"
#include "editor_undo.h"
#include "environment_internal.h"
#include "lights_internal.h"

using prefab::Value;
using rood_selection::Ref;

namespace {
using RGB = std::array<float, 3>;
int tabs[4]; /* the last tab per type: point, spot, rect, sun */
std::vector<RGB> recent; /* newest first, at most 8 */
struct Stop {
    float kelvin;
    const char* name;
};
const Stop STOPS[] = { { 1900, "Candle" }, { 2700, "Tungsten" }, { 3200, "Halogen" }, { 4100, "Moonlight" },
    { 5600, "Daylight" }, { 6500, "Overcast" }, { 10000, "Blue sky" } };

bool shown;
Ref target;
float anchor[2];
int tab = rood_colour::COLOUR_TAB_TEMPERATURE;
int select_frames; /* the remembered tab is selected over the frames after opening */
int opened_frame = -100, shown_frame = -100;
float kelvin = 6500, tint = 0;
RGB estimated_for = { -1, -1, -1 }; /* the colour kelvin and tint were read from */
char hex[16];
bool hex_active;
bool armed, waiting; /* the eyedropper: armed, then a pixel asked for */
std::map<std::string, ImVec4> rects; /* window pixels: x, y, w, h (RPC) */

int type_index(const Ref& r)
{
    if (r.kind == rood_selection::KIND_SUN)
        return 3;
    int i = rood_scene::index_of(r);
    if (r.kind != rood_selection::KIND_LIGHT || i < 0)
        return 0;
    int type = rood_lights::lights[i].type;
    return type == 1 ? 1 : type == 2 ? 2 : 0;
}
bool colour_of(const Ref& r, float out[3])
{
    if (r.kind == rood_selection::KIND_SUN) {
        rood_env::SunState s = rood_env::sun_state();
        memcpy(out, s.color, sizeof s.color);
        return s.defined;
    }
    if (r.kind != rood_selection::KIND_LIGHT || rood_scene::index_of(r) < 0)
        return false;
    Value v = rood_handles::get_value(r, "color");
    for (int k = 0; k < 3; k++)
        out[k] = v.at(k);
    return true;
}
void remember(const float rgb[3])
{
    RGB c = { rgb[0], rgb[1], rgb[2] };
    for (size_t i = 0; i < recent.size(); i++)
        if (fabsf(recent[i][0] - c[0]) + fabsf(recent[i][1] - c[1]) + fabsf(recent[i][2] - c[2]) < 0.006f) {
            recent.erase(recent.begin() + i);
            break;
        }
    recent.insert(recent.begin(), c);
    if (recent.size() > 8)
        recent.resize(8);
}
/* The colour to the target (0..1), one undo step per change; `keep`: into the recent colours. */
void apply(const float in[3], bool keep, const char* step = "Edit Colour")
{
    float rgb[3];
    for (int k = 0; k < 3; k++)
        rgb[k] = fminf(fmaxf(in[k], 0.0f), 1.0f);
    if (target.kind == rood_selection::KIND_SUN) {
        rood_env::SunState s = rood_env::sun_state();
        memcpy(s.color, rgb, sizeof rgb);
        rood_env::set_sun_state(s);
    } else {
        rood_handles::set(target, "color", Value::vec(rgb[0], rgb[1], rgb[2]));
    }
    pc_undo_hint(step);
    if (keep)
        remember(rgb);
}
/* The colours this room's lights use (lights, its actors' lights, the sun): each once, at most 12. */
std::vector<RGB> palette()
{
    std::vector<RGB> out;
    auto add = [&](const float c[3]) {
        for (const RGB& p : out)
            if (fabsf(p[0] - c[0]) + fabsf(p[1] - c[1]) + fabsf(p[2] - c[2]) < 0.02f)
                return;
        if (out.size() < 12)
            out.push_back({ c[0], c[1], c[2] });
    };
    for (const rood_lights::Light& l : rood_lights::lights)
        add(l.color);
    std::vector<prefab::EvalLight> ls;
    for (const prefab::Actor& a : rood_lights::actors) {
        rood_lights::actor_lights(a, ls, false);
        for (const prefab::EvalLight& e : ls)
            add(e.color);
    }
    rood_env::SunState s = rood_env::sun_state();
    if (s.defined)
        add(s.color);
    return out;
}
void keep_rect(const std::string& name)
{
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    rects[name] = ImVec4(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
}
void set_tab(int t)
{
    tab = t;
    tabs[type_index(target)] = t;
}
/* The Kelvin strip: blackbody colours from 1000 to 12000 K (even in mired),
 * ticks at the named stops, the current one marked; click or drag to pick. */
bool kelvin_strip()
{
    float w = ImGui::GetFontSize() * 16, h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##kelvin", ImVec2(w, h));
    keep_rect("kelvin_strip");
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int N = 48;
    for (int i = 0; i < N; i++) {
        float c[3];
        rood_math::kelvin_tint(rood_math::kelvin_strip_k((i + 0.5f) / N), 0, c);
        d->AddRectFilled(ImVec2(p.x + w * i / N, p.y), ImVec2(p.x + w * (i + 1) / N + 1, p.y + h),
            IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), 255));
    }
    for (const Stop& s : STOPS) {
        float x = p.x + w * rood_math::kelvin_strip_u(s.kelvin);
        d->AddLine(ImVec2(x, p.y + h * 0.65f), ImVec2(x, p.y + h), IM_COL32(0, 0, 0, 200), 1.5f);
    }
    float x = p.x + w * rood_math::kelvin_strip_u(kelvin);
    d->AddRect(ImVec2(x - 3, p.y - 2), ImVec2(x + 3, p.y + h + 2), IM_COL32(255, 255, 255, 255), 1, 0, 2.0f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
        ImGui::SetTooltip("%.0f K", rood_math::kelvin_strip_k(fminf(fmaxf((ImGui::GetIO().MousePos.x - p.x) / w, 0.0f), 1.0f)));
    if (!ImGui::IsItemActive())
        return false;
    kelvin = rood_math::kelvin_strip_k(fminf(fmaxf((ImGui::GetIO().MousePos.x - p.x) / w, 0.0f), 1.0f));
    return true;
}
void temperature_tab(float rgb[3])
{
    bool changed = kelvin_strip();
    bool done = ImGui::IsItemDeactivated();
    for (int i = 0; i < (int)(sizeof STOPS / sizeof STOPS[0]); i++) { /* the named stops */
        if (i % 4)
            ImGui::SameLine();
        char label[48];
        snprintf(label, sizeof label, "%s##stop", STOPS[i].name);
        if (ImGui::SmallButton(label)) {
            kelvin = STOPS[i].kelvin;
            changed = done = true;
        }
        keep_rect("stop_" + std::to_string((int)STOPS[i].kelvin));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%.0f K", STOPS[i].kelvin);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    changed |= ImGui::DragFloat("Kelvin", &kelvin, 10, 1000, 12000, "%.0f K");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("kelvin");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    changed |= ImGui::SliderFloat("Tint", &tint, -1, 1, "%.2f");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("tint");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Magenta (-1) to green (+1)");
    if (changed || done) {
        float c[3];
        rood_math::kelvin_tint(kelvin, tint, c);
        apply(c, done);
        memcpy(rgb, c, sizeof c);
        estimated_for = { c[0], c[1], c[2] }; /* keep the Kelvin and tint as chosen */
    }
}
void colour_tab(float rgb[3])
{
    float c[3] = { rgb[0], rgb[1], rgb[2] };
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    bool changed = ImGui::ColorPicker3("##wheel", c,
        ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs
            | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float);
    bool done = ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("wheel");
    float value = fmaxf(c[0], fmaxf(c[1], c[2]));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::SliderFloat("Value", &value, 0, 1, "%.3f")) {
        float m = fmaxf(c[0], fmaxf(c[1], c[2]));
        for (int k = 0; k < 3; k++)
            c[k] = m > 1e-6f ? c[k] / m * value : value;
        changed = true;
    }
    done |= ImGui::IsItemDeactivatedAfterEdit();
    keep_rect("value");
    if (changed || done) {
        apply(c, done);
        memcpy(rgb, c, sizeof c);
    }
}
/* Under both tabs: the room's palette, recent colours, Hex and RGB. */
void shared(float rgb[3])
{
    auto swatches = [&](const char* title, const char* name, const std::vector<RGB>& colours, const char* none) {
        ImGui::SeparatorText(title);
        for (size_t i = 0; i < colours.size(); i++) {
            if (i % 8)
                ImGui::SameLine();
            ImGui::PushID((std::string(name) + std::to_string(i)).c_str());
            if (ImGui::ColorButton("##swatch", ImVec4(colours[i][0], colours[i][1], colours[i][2], 1),
                    ImGuiColorEditFlags_NoAlpha)) {
                apply(colours[i].data(), true);
                memcpy(rgb, colours[i].data(), 3 * sizeof(float));
            }
            keep_rect(name + std::to_string(i));
            ImGui::PopID();
        }
        if (colours.empty())
            ImGui::TextDisabled("%s", none);
    };
    swatches("Room palette", "palette", palette(), "No lights yet");
    swatches("Recent", "recent", std::vector<RGB>(recent), "None yet");
    ImGui::Separator();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    if (!hex_active)
        snprintf(hex, sizeof hex, "%s", rood_math::format_hex(rgb).c_str());
    if (ImGui::InputText("Hex", hex, sizeof hex, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        float c[3];
        if (rood_math::parse_hex(hex, c)) {
            apply(c, true);
            memcpy(rgb, c, sizeof c);
        }
    }
    hex_active = ImGui::IsItemActive();
    keep_rect("hex");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    float c[3] = { rgb[0], rgb[1], rgb[2] };
    if (ImGui::DragFloat3("RGB", c, 0.005f, 0, 1, "%.3f")) {
        apply(c, false);
        memcpy(rgb, c, sizeof c);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        remember(rgb);
    keep_rect("rgb");
}
std::string colours_json(const std::vector<RGB>& colours)
{
    std::string s = "[";
    for (size_t i = 0; i < colours.size(); i++) {
        char c[64];
        snprintf(c, sizeof c, "%s[%.4f,%.4f,%.4f]", i ? "," : "", colours[i][0], colours[i][1], colours[i][2]);
        s += c;
    }
    return s + "]";
}
} // namespace

void rood_colour::open(const Ref& r, float x, float y)
{
    target = r;
    anchor[0] = x, anchor[1] = y;
    shown = true;
    armed = waiting = false;
    tab = tabs[type_index(r)];
    select_frames = 2;
    opened_frame = ImGui::GetFrameCount();
    estimated_for = { -1, -1, -1 };
    hex_active = false;
}
bool rood_colour::is_open() { return shown; }
void rood_colour::close()
{
    shown = false;
    armed = false;
}
bool rood_colour::cancel()
{
    if (!shown)
        return false;
    close();
    return true;
}
void rood_colour::draw()
{
    shown_frame = ImGui::GetFrameCount();
    rects.clear();
    if (!shown)
        return;
    float rgb[3];
    if (target != rood_selection::primary() || !colour_of(target, rgb)) { /* the selection moved on */
        close();
        return;
    }
    if (fabsf(rgb[0] - estimated_for[0]) + fabsf(rgb[1] - estimated_for[1]) + fabsf(rgb[2] - estimated_for[2]) > 0.004f) {
        rood_math::estimate_kelvin_tint(rgb, &kelvin, &tint);
        estimated_for = { rgb[0], rgb[1], rgb[2] };
    }
    ImGui::SetNextWindowPos(ImVec2(anchor[0] + 12, anchor[1] + 12), ImGuiCond_Appearing);
    bool open_now = true;
    if (ImGui::Begin("Colour##popover", &open_now,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("%s", rood_scene::label(target).c_str());
        if (ImGui::BeginTabBar("##colour tabs")) {
            bool first = select_frames > 0;
            bool on = ImGui::BeginTabItem("Temperature", nullptr,
                first && tab == COLOUR_TAB_TEMPERATURE ? ImGuiTabItemFlags_SetSelected : 0);
            keep_rect("tab_temperature");
            if (on) {
                if (!first && tab != COLOUR_TAB_TEMPERATURE)
                    set_tab(COLOUR_TAB_TEMPERATURE);
                temperature_tab(rgb);
                ImGui::EndTabItem();
            }
            on = ImGui::BeginTabItem("Colour", nullptr, first && tab == COLOUR_TAB_WHEEL ? ImGuiTabItemFlags_SetSelected : 0);
            keep_rect("tab_colour");
            if (on) {
                if (!first && tab != COLOUR_TAB_WHEEL)
                    set_tab(COLOUR_TAB_WHEEL);
                colour_tab(rgb);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        shared(rgb);
    }
    bool inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::End();
    if (select_frames > 0)
        --select_frames;
    /* Closed by its button, or by a click elsewhere (not on a handle: the swatch toggles it). */
    if (!open_now
        || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside && !armed && !waiting && !rood_handles::hot()
            && ImGui::GetFrameCount() > opened_frame + 1))
        close();
}
std::string rood_colour::state_json()
{
    auto q = [](const std::string& s) { return prefab::json_quote(s); };
    bool current = shown && shown_frame >= ImGui::GetFrameCount() - 1;
    float rgb[3] = { 0, 0, 0 };
    if (shown)
        colour_of(target, rgb);
    char head[400];
    snprintf(head, sizeof head,
        "{\"open\":%s,\"subject\":%s,\"tab\":\"%s\",\"tabs\":[%d,%d,%d,%d],\"kelvin\":%.1f,\"tint\":%.3f,"
        "\"colour\":[%.4f,%.4f,%.4f],\"armed\":%s,\"waiting\":%s",
        current ? "true" : "false", shown ? q(target.id).c_str() : "null", tab == COLOUR_TAB_WHEEL ? "colour" : "temperature",
        tabs[0], tabs[1], tabs[2], tabs[3], kelvin, tint, rgb[0], rgb[1], rgb[2], armed ? "true" : "false",
        waiting ? "true" : "false");
    std::string s = std::string(head) + ",\"palette\":" + colours_json(palette()) + ",\"recent\":" + colours_json(recent)
        + ",\"rects\":{";
    bool first = true;
    for (const auto& kv : rects) {
        if (!current)
            break;
        char r[96];
        snprintf(r, sizeof r, "[%.1f,%.1f,%.1f,%.1f]", kv.second.x, kv.second.y, kv.second.z, kv.second.w);
        s += (first ? "" : ",") + q(kv.first) + ":" + r;
        first = false;
    }
    return s + "}}";
}
bool rood_colour::rpc(const prefab::Json& p, std::string* error)
{
    if (const prefab::Json* o = p.get("open")) {
        bool on = o->type == prefab::Json::Bool ? o->boolean : o->number != 0;
        if (!on) {
            close();
        } else {
            Ref r = rood_selection::primary();
            if (r.kind != rood_selection::KIND_LIGHT && r.kind != rood_selection::KIND_SUN) {
                *error = "the colour popover needs a light or the sun selected";
                return false;
            }
            float at[2] = { 0, 0 };
            rood_handles::handle_pixel("colour", at);
            open(r, at[0], at[1]);
        }
    }
    bool edits = p.get("tab") || p.get("kelvin") || p.get("tint") || p.get("hex") || p.get("rgb") || p.get("palette")
        || p.get("recent");
    if (!edits)
        return true;
    if (!shown) {
        *error = "the colour popover is not open (open: true)";
        return false;
    }
    if (const prefab::Json* t = p.get("tab")) {
        set_tab(t->string == "colour" ? COLOUR_TAB_WHEEL : COLOUR_TAB_TEMPERATURE);
        select_frames = 2;
    }
    if (p.get("kelvin") || p.get("tint")) {
        if (const prefab::Json* k = p.get("kelvin"))
            kelvin = (float)k->number;
        if (const prefab::Json* t = p.get("tint"))
            tint = (float)t->number;
        float c[3];
        rood_math::kelvin_tint(kelvin, tint, c);
        apply(c, true);
        estimated_for = { c[0], c[1], c[2] };
    }
    if (const prefab::Json* h = p.get("hex")) {
        float c[3];
        if (!rood_math::parse_hex(h->string, c)) {
            *error = "hex: #RRGGBB";
            return false;
        }
        apply(c, true);
    }
    if (const prefab::Json* c = p.get("rgb")) {
        if (c->type != prefab::Json::Array || c->items.size() != 3) {
            *error = "rgb: [r, g, b] (0..1)";
            return false;
        }
        float v[3] = { (float)c->items[0].number, (float)c->items[1].number, (float)c->items[2].number };
        apply(v, true);
    }
    for (const char* list : { "palette", "recent" })
        if (const prefab::Json* i = p.get(list)) {
            std::vector<RGB> colours = !strcmp(list, "palette") ? palette() : recent;
            int k = (int)i->number;
            if (k < 0 || k >= (int)colours.size()) {
                *error = std::string(list) + ": no colour " + std::to_string(k);
                return false;
            }
            apply(colours[k].data(), true);
        }
    return true;
}
bool rood_colour::parse_setting(const char* key, const char* rest)
{
    if (!strcmp(key, "editor_colour_tabs")) {
        int t[4] = { 0, 0, 0, 0 };
        sscanf(rest, "%d %d %d %d", &t[0], &t[1], &t[2], &t[3]);
        for (int k = 0; k < 4; k++)
            tabs[k] = t[k] == COLOUR_TAB_WHEEL ? COLOUR_TAB_WHEEL : COLOUR_TAB_TEMPERATURE;
        return true;
    }
    if (!strcmp(key, "editor_recent_colours")) {
        recent.clear();
        const char* p = rest;
        RGB c;
        int used = 0;
        while (recent.size() < 8 && sscanf(p, " %f,%f,%f%n", &c[0], &c[1], &c[2], &used) == 3) {
            recent.push_back(c);
            p += used;
        }
        return true;
    }
    return false;
}
void rood_colour::write_settings(FILE* f)
{
    fprintf(f, "editor_colour_tabs %d %d %d %d\n", tabs[0], tabs[1], tabs[2], tabs[3]);
    if (recent.empty())
        return;
    fprintf(f, "editor_recent_colours");
    for (const RGB& c : recent)
        fprintf(f, " %.3f,%.3f,%.3f", c[0], c[1], c[2]);
    fputc('\n', f);
}
```

- [ ] **Step 5: The swatches, the RPC, the frame, Escape, settings**

In `pc/src/editor_light_handles.cpp`, add `#include "editor_colour.h"` after `#include "editor_math.h"`; in the anonymous namespace after `shadows_toggle` add

```cpp
/* The colour swatch, below right of the ring: a click opens (or closes) the colour popover. */
Handle swatch(const Ref& r, float cx, float cy, float ring_radius, const float colour[3])
{
    Handle h;
    h.name = "colour", h.label = "Colour";
    h.shape = rood_handles::SHAPE_SQUARE;
    h.x = cx + ring_radius * 0.75f + 10 * em(), h.y = cy + ring_radius * 0.75f + 10 * em(), h.radius = 6 * em();
    h.colour = colour_of(colour);
    float x = h.x, y = h.y;
    h.click = [r, x, y]() {
        if (rood_colour::is_open())
            rood_colour::close();
        else
            rood_colour::open(r, x, y);
    };
    return h;
}
```

In `rood_handles::light_handles`, at the end (after the `if (l.type == 2) { ... }` block) add

```cpp
    out.push_back(swatch(r, c[0], c[1], intensity.radius, l.color));
```

and in `rood_handles::sun_handles`, after the `out.push_back(ring(...));` statement add

```cpp
    out.push_back(swatch(Ref { rood_selection::KIND_SUN, "env:sun" }, px[0], px[1], 22 * em(), s.color));
```

In `pc/src/editor_handles.cpp`, add `#include "editor_colour.h"` after `#include "editor_math.h"`; in `rood_handles::state_json` change the last line `+ (aim_target ? q(aim_target.id) : std::string("null")) + "}";` to

```cpp
        + (aim_target ? q(aim_target.id) : std::string("null")) + ",\"colour\":" + rood_colour::state_json() + "}";
```

and in `pc_editor_handles_rpc`, before `} else if (action == "state") {` add

```cpp
    } else if (action == "colour") { /* the colour popover (editor_colour.h rpc) */
        if (!rood_colour::rpc(params, &error)) {
            snprintf(out, size, "%s", error.c_str());
            return -1;
        }
        text = rood_handles::state_json();
```

and change the unknown-action message to `"action: state, where or colour"`. Its comment block gains: `colour {open, tab, kelvin, tint, hex, rgb, palette, recent}: the colour popover (editor_colour.h);`.

In `pc/src/dev_overlay.cpp`, add `#include "editor_colour.h"` after `#include "editor_handles.h"`; after the handles' Escape lines add

```cpp
        /* Escape closes the colour popover (editor_colour.cpp). */
        if (key == SDLK_ESCAPE && visible && down && !typing && rood_colour::cancel())
            return 1;
```

and in `pc_overlay_frame`, after `viewport_keys();` add

```cpp
        rood_colour::draw(); /* the colour popover (editor_colour.cpp) */
```

In `pc/src/project_settings.cpp`, after `#include "editor_selection.h" /* ... */` (line 89) add `#include "editor_colour.h" /* editor_colour_tabs, editor_recent_colours */`; after the `editor_hidden_` lines (`return rood_selection::parse_hidden(key, rest) ? 1 : 0;`) add

```cpp
    if (rood_colour::parse_setting(key, rest)) /* editor state: the colour popover's tabs and recent colours */
        return 1;
```

and after `rood_selection::write_hidden(f);` add `rood_colour::write_settings(f);`.

In `pc/tools/build_boot.py`, after `root / "pc/src/editor_light_handles.cpp",` add

```python
    root / "pc/src/editor_colour.cpp",
```

- [ ] **Step 6: Run it and see it pass**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles system_settings
```

Expected: `2/2 passed` (the new `editor_` settings lines read and write; the settings test is unaffected).

- [ ] **Step 7: Commit**

```bash
git add pc/src/editor_colour.h pc/src/editor_colour.cpp pc/src/editor_light_handles.cpp pc/src/editor_handles.cpp pc/src/dev_overlay.cpp pc/src/project_settings.cpp pc/tools/build_boot.py pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
Colour popover from the swatch: Kelvin strip with stops and tint, hue wheel and value, room palette, recent colours, Hex/RGB, last tab per light type

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 20: The eyedropper

**Files:**
- Modify: `pc/src/gl_present.c` (new functions after `pc_write_ppm_atomic`; `capture_present`, after `++pc_presents;`), `pc/src/vk_present.c` (externs after `extern unsigned pc_presents;`; `capture_present`, after `++pc_presents;`), `pc/src/editor_colour.h`, `pc/src/editor_colour.cpp` (externs; `shared`; `viewport_click`; `draw`; `cancel`), `pc/src/lights.cpp` (include; the click hook from Task 13)
- Test: `pc/tests/editor_handles_smoke.py`

- [ ] **Step 1: Write the failing section**

In `pc/tests/editor_handles_smoke.py`, add `import golden_frames as gf  # noqa: E402` after the `editor_schema_smoke` import, and before `SECTIONS = ` add:

```python
def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def check_eyedropper(s):
    """The eyedropper: armed in the popover, a click on the picture gives the light that pixel's colour
    (display to linear, brightest channel 1), one undo step "Pick colour"."""
    g = s.game
    g.call("actors", time=0)
    s.select(action="set", ids=[s.light])
    g.run(2)
    colour(s, open=True)
    g.run(3)
    st = handles(s)["colour"]
    xs = [r[0] for r in st["rects"].values()] + [r[0] + r[2] for r in st["rects"].values()]
    ys = [r[1] for r in st["rects"].values()] + [r[1] + r[3] for r in st["rects"].values()]
    popover = [min(xs) - 20, min(ys) - 40, max(xs) + 20, max(ys) + 20]
    view = s.select()["view"]
    objects = [o["screen"] for o in s.select()["objects"] if o["screen"]]
    image = gf.capture(g, s.out / "eyedropper")
    spot = None
    for fx, fy in ((0.1, 0.85), (0.9, 0.85), (0.1, 0.5), (0.5, 0.9), (0.9, 0.5)):
        xy = [view[0] + view[2] * fx, view[1] + view[3] * fy]
        inside = popover[0] <= xy[0] <= popover[2] and popover[1] <= xy[1] <= popover[3]
        box = image[int(xy[1]) - 1:int(xy[1]) + 2, int(xy[0]) - 1:int(xy[0]) + 2].reshape(-1, 3).mean(axis=0) / 255
        if not inside and all(math.dist(xy, o) > 60 for o in objects) and box.max() > 0.12:
            spot = (xy, box)
            break
    if not s.check(spot is not None, "a lit spot of the picture away from the popover and the icons"):
        return
    xy, box = spot
    want = [srgb_to_linear(c) for c in box]
    want = [c / max(want) for c in want]
    s.click(centre(st["rects"]["eyedropper"]))
    s.check(handles(s)["colour"]["armed"], "the Eyedropper button arms it")
    done = s.history()["done"]
    s.click(xy)
    g.run(4)
    got, hist = value(s, s.light, "color"), s.history()
    s.check(close(got, want, 0.06) and hist["done"] == done + 1 and hist["undo"] == "Pick colour",
            f"a click on the picture: the light takes its colour ({got} vs {[round(c, 3) for c in want]}; {hist['undo']})")
    s.check(not handles(s)["colour"]["armed"] and s.select()["selection"] == [s.light],
            "the click was the eyedropper's (the selection unchanged)")
    g.call("editor_undo", action="undo")
    colour(s, open=False)
```

change `SECTIONS` to `[check_layer, check_point, check_spot, check_rect, check_sun, check_colour, check_eyedropper]`, and add to the docstring:

```
- eyedropper: armed in the popover, a click on the picture gives the light
  that pixel's colour (display to linear, brightest channel 1) in one undo
  step; the click selects nothing.
```

- [ ] **Step 2: Run it and see it fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
```

Expected: `0/1 passed`; the log shows `KeyError: 'eyedropper'` (no such button yet).

- [ ] **Step 3: A pixel of the presented image**

In `pc/src/gl_present.c`, after the end of `pc_write_ppm_atomic` add:

```c
/* The eyedropper (editor_colour.cpp): the mean of the 3 x 3 pixels around
 * window pixel (x, y) of the next presented image (display values 0..1,
 * the overlay included, as present_dump sees it). Both presenters read it
 * at present time (capture_present). */
static int sample_state; /* 0 none, 1 asked for, 2 read */
static int sample_xy[2];
static float sample_rgb[3];
int pc_present_sample_request(float x, float y)
{
    sample_xy[0] = (int)x, sample_xy[1] = (int)y;
    sample_state = 1;
    return 1;
}
int pc_present_sample_result(float rgb[3])
{
    if (sample_state != 2)
        return 0;
    memcpy(rgb, sample_rgb, sizeof sample_rgb);
    sample_state = 0;
    return 1;
}
/* For the presenters: the pixel asked for (0: none), and its 3 x 3 read (`stride` bytes per pixel, RGB first). */
int pc_present_sample_pending(int xy[2])
{
    if (sample_state != 1)
        return 0;
    xy[0] = sample_xy[0], xy[1] = sample_xy[1];
    return 1;
}
void pc_present_sample_store(const unsigned char* pixels, int stride)
{
    float sum[3] = { 0, 0, 0 };
    for (int i = 0; i < 9; i++)
        for (int k = 0; k < 3; k++)
            sum[k] += pixels[i * stride + k];
    for (int k = 0; k < 3; k++)
        sample_rgb[k] = sum[k] / (9 * 255.0f);
    sample_state = 2;
}
```

In the GL `capture_present`, after `++pc_presents;` add:

```c
    int sample_at[2];
    if (pc_present_sample_pending(sample_at)) { /* the eyedropper's pixel (GL rows count from the bottom) */
        unsigned char px[27];
        int x = sample_at[0] - 1, y = window_h - 2 - sample_at[1];
        x = x < 0 ? 0 : x > window_w - 3 ? window_w - 3 : x;
        y = y < 0 ? 0 : y > window_h - 3 ? window_h - 3 : y;
        gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl_ReadPixels(x, y, 3, 3, GL_RGB, GL_UNSIGNED_BYTE, px);
        pc_present_sample_store(px, 3);
    }
```

In `pc/src/vk_present.c`, after `extern unsigned pc_presents;` add:

```c
extern int pc_present_sample_pending(int xy[2]); /* gl_present.c: the eyedropper's pixel */
extern void pc_present_sample_store(const unsigned char* pixels, int stride);
```

and in its `capture_present`, after `++pc_presents;` add:

```c
    int sample_at[2];
    if (pc_present_sample_pending(sample_at)) { /* the eyedropper's pixel */
        unsigned char px[36];
        int w = window_image.width, h = window_image.height;
        int x = sample_at[0] - 1, y = sample_at[1] - 1;
        x = x < 0 ? 0 : x > w - 3 ? w - 3 : x;
        y = y < 0 ? 0 : y > h - 3 ? h - 3 : y;
        if (vk_read_image(&window_image, VK_IMAGE_ASPECT_COLOR_BIT, x, y, 3, 3, px, 4))
            pc_present_sample_store(px, 4);
    }
```

- [ ] **Step 4: The popover's eyedropper**

In `pc/src/editor_colour.h`, after `void draw();` add

```cpp
/* The eyedropper armed: this click in the view picks the colour under it
 * (on the next presented image); true when it took the click. */
bool viewport_click(float x, float y);
```

and change `cancel`'s comment to `/* Escape: the eyedropper disarmed, else the popover closed; true when either */`. In `pc/src/editor_colour.cpp`, after the `using` lines add

```cpp
extern "C" int pc_present_sample_request(float x, float y); /* gl_present.c */
extern "C" int pc_present_sample_result(float rgb[3]);
```

In `shared`, after `ImGui::Separator();` add

```cpp
    if (ImGui::Button(armed ? "Click the picture..." : "Eyedropper"))
        armed = !armed, waiting = false;
    keep_rect("eyedropper");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Pick a colour from the picture: click here, then on the view.\n"
                          "Its brightest channel becomes 1 (Intensity keeps the brightness).");
    ImGui::SameLine();
```

In `rood_colour::draw`, after `rects.clear();` add

```cpp
    /* The pixel the eyedropper asked for has arrived: display to linear, brightest channel 1. */
    float picked[3];
    if (waiting && pc_present_sample_result(picked)) {
        waiting = false;
        float c[3];
        for (int k = 0; k < 3; k++)
            c[k] = rood_math::srgb_to_linear(picked[k]);
        rood_math::normalize_colour(c);
        if (rood_scene::exists(target))
            apply(c, true, "Pick colour");
    }
```

replace `rood_colour::cancel` with

```cpp
bool rood_colour::cancel()
{
    if (armed || waiting) {
        armed = waiting = false;
        return true;
    }
    if (!shown)
        return false;
    close();
    return true;
}
```

and add after it

```cpp
bool rood_colour::viewport_click(float x, float y)
{
    if (!armed || !shown)
        return false;
    armed = false;
    waiting = pc_present_sample_request(x, y) != 0;
    return true;
}
```

In `pc/src/lights.cpp`, add `#include "editor_colour.h"` after `#include "editor_handles.h"`, and change the click hook from Task 13 to

```cpp
    /* A spot waiting to be aimed (its handle) or the colour popover's eyedropper takes the click. */
    if (rood_handles::viewport_click(io.MousePos.x, io.MousePos.y) || rood_colour::viewport_click(io.MousePos.x, io.MousePos.y))
        return;
```

- [ ] **Step 5: Run it on both renderers**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 editor_handles
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan editor_handles
```

Expected: both builds exit 0; `1/1 passed` on OpenGL and on Vulkan.

- [ ] **Step 6: Commit**

```bash
git add pc/src/gl_present.c pc/src/vk_present.c pc/src/editor_colour.h pc/src/editor_colour.cpp pc/src/lights.cpp pc/tests/editor_handles_smoke.py
git commit -m "$(cat <<'EOF'
Colour popover eyedropper: a click on the picture samples the presented image (GL and Vulkan), display to linear, one undo step

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 21: Docs, help and full verification

**Files:**
- Modify: `lighting/README.md` (the light and actor fields, line 76), `pc/docs/Vulkan_Renderer.md` (after the "Direct light, unshadowed" bullet, line 379), `pc/src/dev_overlay.cpp` (`help_window`, after the rows added in Task 6)

- [ ] **Step 1: lighting/README.md**

Replace

```
- Light fields: `name type(point|spot|rect) pos dir color intensity radius
  cone size(w,h) shadows enabled`. Actor fields: `name prefab pos rot(yaw,pitch,roll)
  seed enabled glow glowkey fixture fixture_radius fixture_shadows
  fixture_type flame`, and any parameter of its prefab
```

with

```
- Light fields: `name type(point|spot|rect) pos dir color intensity radius
  cone inner size(w,h) barn_angle barn_length shadows enabled`. `inner` (a
  spot): full brightness inside this angle, fading out to `cone` (0: the
  outer quarter of the cone fades). `barn_angle`, `barn_length` (a rect):
  its four barn doors, at that angle from its facing (0 straight ahead, 90
  folded flat) and that long (0: no doors). Actor fields: `name prefab pos
  rot(yaw,pitch,roll) scale seed enabled glow glowkey fixture fixture_radius
  fixture_shadows fixture_type flame` (`scale`: its size; its lights'
  offsets, radii and sizes follow, never their brightness), and any parameter of its prefab
```

- [ ] **Step 2: Vulkan_Renderer.md**

After the bullet that starts `- **Direct light, unshadowed** (\`scaled.frag\`)` (it ends `... cost a compare).`) add:

```
- **Light shapes** (`light_shape.glsl`; OpenGL's copy in `light_shape.h`):
  a spot fades from its inner cone to its cone (inner 0: the outer quarter
  of the cone's cosine range, the old edge); a rect lights only in front of
  it, times its barn doors: per axis, the share of its width a point sees
  past the two doors (length and angle from its facing). They travel in
  slots the list had free: a spot's `right.x` (cos of the inner half angle),
  a rect's `dir.w` and `right.w` (length x cos and sin of the doors' angle).
  Every evaluation (present, trace, GI bounces, haze, probes) multiplies the
  falloff by it, so traced and mapped shadows need nothing new. The fog
  volumes' own renderer (`fog_volume.glsl`, three vectors per light) keeps
  the old spot edge and has no barn doors.
```

- [ ] **Step 3: Help rows**

In `pc/src/dev_overlay.cpp` `help_window`, after `{ "Tools", "World / Local (toolbar)", ... },` add:

```cpp
        { "Handles", "Drag the ring", "The selected light's or sun's intensity: doubles every 100 pixels to the right" },
        { "Handles", "Drag a dot", "Radius, range, cone, inner cone, a rect's edges (Alt: both edges), barn doors" },
        { "Handles", "Ctrl / Shift while dragging", "Fine steps / round values; the value shows by the pointer; Esc puts it back" },
        { "Handles", "Ring's top dot", "A point light's shadows on or off" },
        { "Handles", "Swatch", "The colour popover: Kelvin and tint, a hue wheel, the room's palette, recent colours, eyedropper, Hex / RGB" },
        { "Handles", "Spot's crosshair, then a click", "Aim the spot at the clicked surface (Esc stops)" },
        { "Handles", "Sun: drag its disc", "Its direction across the sky dome (azimuth and elevation)" },
```

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
```

Expected: exit 0.

- [ ] **Step 4: Every build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows
```

Expected: each exits 0 with a `Built …` line; the Windows build reports no failed units (a failure names the unit; the usual cause is a Windows macro used as an identifier).

- [ ] **Step 5: 32-bit suites (OpenGL)**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest editor barn_doors light_actors atmosphere fog glow fixture lighting room_browser gi sky system_settings
```

Expected: `N/N passed`, or only the failures recorded in Task 1.

- [ ] **Step 6: 64-bit Vulkan suites**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan editor barn_doors light_actors atmosphere fog glow fixture lighting gi sky
```

Expected: `N/N passed` (baseline failures excepted).

- [ ] **Step 7: Leak scan of the branch's committed text**

```bash
pc/tools/publish.sh --dry-run "leak check"
```

Expected: `clean`.

- [ ] **Step 8: Commit**

```bash
git add lighting/README.md pc/docs/Vulkan_Renderer.md pc/src/dev_overlay.cpp
git commit -m "$(cat <<'EOF'
Docs and help: inner cone, barn doors, actor scale; light shapes in the renderers; the handles' keys

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-review

### Spec coverage (phases B and C)

| Requirement | Task |
|---|---|
| W/E/R transform (R = scale) | 6 (key, toolbar, gizmo), 5 (keys test) |
| Scale semantics: point radius (uniform) | 3 (table), 6 (gizmo + RPC), 5 (lights, mouse) |
| Spot range; cone has its own handle | 3, 5 (cone kept), 15 (cone handle) |
| Rect width x height (non-uniform) and range on forward | 3, 5 |
| Fog volume size X/Y/Z (Size mode becomes R) | 3, 6 (atmosphere follows W/E/R), 5 (fog) |
| Prefab: uniform size; light positions and radii scale together, intensities don't | 3, 4 (actor `scale`, evaluate), 5 (actor) |
| Scaling never changes intensity | 3 (probe), 5 (every light kind, actor) |
| World/local toggle on the toolbar | 6 (`pc_lights_gizmo_space`, toolbar, settings, RPC), 5 (space) |
| Handle layer `pc/src/editor_handles.*` (hit area, drag mapping linear/log, shared modifiers) | 2 (mappings), 13 |
| Ctrl fine, Shift snap | 2 (maths), 14 (ring with Ctrl and Shift held), every drag function |
| Value shown while dragging | 13 (readout), 14 (test) |
| Esc cancels | 13, 14/15/16/17 (tests) |
| One undo step per drag (`pc_undo_hint` / `pc_undo_begin`) | 13, every `check_drag` |
| Handles hidden while flying | 13 (test) |
| Only the selection shows handles | 13 (nothing / actor / hidden), 14 (primary of two) |
| Each kind describes its handles in terms of schema fields; a drag changes one field | 13 (`set` via `rood_schema::apply`), 14-16 (`check_drag`: exactly one field) |
| Point: intensity ring (log), radius dot, swatch, shadows toggle on the ring | 14, 19 (swatch) |
| Spot: ring, swatch, range dot, outer cone, inner (soft) cone, aim at surface | 8 (inner field), 15, 19 |
| Rect: ring, swatch, edges (Alt symmetric), range dot | 16, 19 |
| Rect barn doors dragged as flaps (angle, length) | 8 (fields), 16 (flaps, handles) |
| Barn doors in the renderer: parameters, storage in the lighting item format | 8 (`barn_angle`, `barn_length`; lighting_layers.cpp + lighting_format.py) |
| Barn doors (and the inner cone) in RT/raster evaluation: Vulkan | 9 (packing), 11 (rt_lights.glsl, rt_haze, probe_trace, scaled.frag; shadow maps: factor before the map lookup, softness guard) |
| ... OpenGL | 9, 12 (ptgi.c x4, gl_present.c, editor_view.cpp) |
| Barn doors visibly cut light (pixel measure) on both renderers | 10, 11, 12 |
| Sun/moon: ring, swatch, disc dragged across a sky dome (azimuth, elevation), replacing the rotate gizmo | 17, 19 (swatch) |
| Colour popover: Temperature tab (Kelvin strip with named stops, tint) | 18, 19 |
| Colour tab (hue wheel and value) | 19 |
| Room palette, recent colours, Hex/RGB | 19 |
| Eyedropper from the scene | 20 (GL and Vulkan present sampling) |
| Remembers the last tab per light type (user settings) | 19 (`editor_colour_tabs`, test) |
| Tests: handle drag changes one field by the expected amount, Esc restores, one undo step | 14-17 (`check_drag`, `check_escape`) |
| Tests: R on each light type, never intensity; prefab scale moves lights and radii | 6 |
| Tests: colour popover set / eyedropper / palette | 19, 20 |
| Existing editor / lighting / glow / GI suites stay green | 2, 4, 8, 9, 11, 12, 17, 21 |
| No src/ changes; ImGui + ImGuizmo kept; editor state in settings; no names/paths; no CPU load | Ground rules; 21 (leak scan) |
| Menus session's `add_kind` / `draw_kind` not conflicted | Ground rules (no registry changes; Task 17's Details note is a self-contained branch) |
| Not in B/C: fog volume handles (phase D), library (phase E), "fit to window" (later) | not planned |

### Placeholder scan

No step says "TBD", "add error handling", "similar to Task N" or "write tests for the above"; every code step shows the code. `editor_light_handles.cpp` starts in Task 13 with two empty entry points by design (the layer is tested alone first); Task 14 replaces the file and Tasks 15-19 add to it with full code. Two edits are mechanical replacements given by exact old and new text: ptgi.c's three identical light lines (`replace_all`) and the sun gizmo's removal (from its section comment to the line before `/* ---- RPC environment ---- */`).

### Type and name consistency

- `rood_math::{project, view_axes(view, projection, rect, right, up, forward), view_eye, light_frame, line_param, plane_hit, sphere_hits(o, d, c, r, near_dir, far_dir) -> int, log_drag, linear_drag, snap_step, snap_125, sun_direction, sun_angles, DOME_AHEAD, DOME_RADIUS, dome_centre(view, projection, rect, out), auto_inner}` (Task 2) and `{kelvin_tint, estimate_kelvin_tint, kelvin_strip_u, kelvin_strip_k, parse_hex, format_hex, srgb_to_linear, normalize_colour}` (Task 18): used with these signatures in Tasks 7, 13-20.
- `rood_transform::{RADIUS_MIN, RADIUS_MAX, SIZE_MIN, SIZE_MAX, SCALE_MIN, SCALE_MAX, VOLUME_MIN, uniform_ratio, scale_light, scale_actor, scale_volume}`: Task 3; used by `scale_object` (Task 6) and `edge_handle` (Task 16).
- `pc_spot_cos_inner`, `pc_barn_pack`, `pc_barn_doors`, `PC_LIGHT_SHAPE_GLSL` (Task 9) and `light_shape.glsl`'s `barn_doors` / `light_shape(type, centre, dir, right, up, p, l)` (Task 11): the same arguments at every call site in Tasks 11 and 12.
- Fields: `Light::{inner, barn_angle, barn_length}`, `EvalLight` / `LightTemplate` same names (Task 8); `Actor::scale` (Task 3, used from Task 4); item keys `inner`, `barn_angle`, `barn_length`, `scale` in `lighting_layers.cpp` and `lighting_format.py` alike; schema field names `inner`, `barn_angle`, `barn_length`, `scale` (Tasks 4, 8) are what the handles set (Tasks 15, 16) and the tests read.
- `rood_handles::{Shape SHAPE_*, Drag, Handle, light_handles, light_extras, sun_handles, sun_icon, frame, dragging, hot, handle_pixel, cancel, arm_aim, viewport_click, get_value, get, set, readout, state_json}`, `pc_editor_handles_rpc`, `pc_editor_handle_drag`: declared in Task 13 (`sun_handles`, `sun_icon`, `Drag::sun` in Task 17), defined in `editor_handles.cpp` / `editor_light_handles.cpp`.
- `rood_colour::{Tab COLOUR_TAB_*, open, is_open, close, draw, cancel, state_json, rpc, parse_setting, write_settings}` (Task 19), `viewport_click` (Task 20).
- `rood_env::{SunState, sun_state, set_sun_state, sun_icon_shown}` (Task 17), `rood_atmo::{volume_size, set_volume_size}` (Task 6), `rood_selection::KIND_SUN` / `"env:sun"` (Task 17).
- `gizmo_operation` 0/1/2, `pc_lights_gizmo_space`, `pc_editor_transform_rpc`, `scale_object` (Task 6); `pc_present_sample_{request, result, pending, store}` (Task 20).
- RPC fields the tests read match the C++ that writes them: `editor_transform` {operation, space, gizmo{shown, subject, operation, mode}}; `editor_select` project → `pixel`, pick → `world`, `normal`; `editor_handles` {subject, hidden, handles[{name, label, shape, screen, centre, radius}], drag{active, handle, moved, readout}, aim, colour{open, subject, tab, tabs, kelvin, tint, colour, armed, waiting, palette, recent, rects}}, where → `screen`; `editor_input` → `held`; handle names `intensity, radius, shadows, range, cone, inner, aim, width±, height±, barn_angle, barn_length, sun, colour`; undo names `Edit <label>`, `Scale ...`, `Aim at surface`, `Pick colour`, `Edit Colour`.
