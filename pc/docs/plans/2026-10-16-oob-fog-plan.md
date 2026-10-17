# Out-of-bounds fog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Where the game cuts away the camera-side walls (Undercity, zone 48 room 0, Ashley at about (972, 0, 1221)) the void around the room shows clean darkness, as on the PS1, with the haze and ground fog on: no ground fog over floor-grid cells without a floor, the haze and ground fog confined to the room's interior (not its bounds box), and no room medium at all on pixels that show the void. Fog still lies on the room's floor; hand-placed fog volumes keep working; the `gameplay_walls smart` + ghost look still works in this room.

**Architecture:** Three layers, each a room setting (data + the Room settings panel's Atmosphere page + RPC `environment {haze}`), each on by default (this is a fix; 0 restores the old picture):

1. **The floor grid knows where the room is** (`scene_capture.c` + a new pure `floor_grid.c`): besides the per-cell lowest level floor it records where the room has any floor at all (up-facing faces up to 60 degrees, black shells left out), and finishes the grid with a morphological closing (`haze_interior_fill` cells) instead of growing every empty cell from its neighbours. Each cell is EMPTY, SOLID or FILLED (a small hole). Heights keep their old values everywhere (so `haze_interior 0` is the old fog).
2. **The medium keeps to the interior** (`atmosphere.cpp` packs, `medium.glsl` reads): EMPTY cells carry the 12-bit step 4095; with `haze_interior` the uniform haze and the ground fog are multiplied by the cells' bilinear floor share (a soft edge at the cell border), and the haze fades out within `haze_under_floor` units under the lowest floor. One look-up of the grid per sample (`medium_room_air`). The grid is packed whenever the haze or the ground fog is on (it used to need the fog).
3. **Void pixels get no room medium** (the present passes: `scaled.frag`, `gl_present.c`): a pixel with nothing drawn, or whose surface the PS1 shows black whatever its texture (G-buffer shade: vertex colour at most `haze_void_level`, textured or flat, not raw, not emissive: the rooms' black shells), skips the traced haze, haze Low and the GL traced haze's light (`medium_void_pixel`, flag in the medium record). Fog volumes are exempt (their own renderer and their medium entries are untouched). `haze_debug 2` shows the classification.

**Tech Stack:** C11 / C++17, GLSL (Vulkan via glslang; OpenGL 3.3/4.3 strings generated from the same `medium.glsl` by `pc/tools/gl_shaders.py`), the dev RPC (`pc/src/dev_rpc.c`, `atmosphere.cpp` `environment`), Python 3 headless tests (`pc/tests/run.py`, `rood_rpc.py`, numpy + PIL), CTest (`pc/CMakeLists.txt`).

---

## Ground rules for every task

- No changes under `src/` (the decompilation). Everything is `pc/` (plus `lighting/README.md`).
- `$MAIN` (the user's checkout) is read only: never build, test or write there. Work in your own worktree (`$WT`, the one the harness gives you, or one made with superpowers:using-git-worktrees outside `$MAIN`'s tree).
- Heavy work (every build, every game test, CTest) runs under `flock "$ROOD_HEAVY_LOCK"`, tests at `-j 2`. `ROOD_HEAVY_LOCK` is the shared lock file named in the project rules (export it in your shell; never write its path into a committed file). No step creates artificial CPU load (no stress tools, busy loops or extra parallel builds), not even to reproduce a flake; a test waits only with `game.run(n)`.
- Tests use the pinned card `pc/build/test-card1.mcd` (or temp copies); they never read or write `pc/save`, `pc/user` or `pc/lights` (scratch `ROOD_LIGHTS` under the test's output directory, `ROOD_SETTINGS` unset).
- No game data or game-derived assets in commits (test frames stay in `pc/build/tests/`). Committed files never mention other game ports by name and never contain home-directory or temp-directory paths; docs write the lock as `$ROOD_HEAVY_LOCK`.
- Look/feel parameters are room settings (data, panel, RPC), never constants. The PS1 look stays: nearest-filtered textures; nothing here filters a texture.
- Windows: `windows.h` is force-included on MinGW, so no new identifier may be a Windows macro or typedef (`near`, `far`, `small`, `interface`, `DELETE`, `VOID`). New C names are prefixed (`PC_FLOOR_*`, `PC_MEDIUM_*`, `pc_haze_*`); GLSL never names anything `void`.
- Commit after each task; messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Do not merge into main, do not push. Kill processes by PID only; bound every wait with a timeout; leave no background processes.

## Decisions taken where the brief leaves room

1. **Void = what the PS1 shows black whatever the texture.** A present pixel is void when nothing is drawn there (depth 1) or its G-buffer shade is a lit 3D surface (shade alpha 0.5 flat or 1 textured; never 0.25 emissive or 0 unlit) whose vertex colour is at most `haze_void_level` (0..32 of 255, default 0: exactly black). Those are the black shells (Native_Rendering.md "Seeing into rooms": each wall's thickness, top and outside, every vertex colour 0) that the cut-away reveals as "the void around the room". This works on both renderers and both room paths (`native_room` 0 and 2) without plumbing group ids into the G-buffer. A black-painted surface inside the room (vertex colour exactly 0) also counts: on the PS1 it is black anyway, and fog over it reads as the same glowing mist.
2. **Fog volumes are exempt** from both the interior mask and the void test: an artist placed them, so they render wherever they are, including in front of void pixels (the far side of a volume that hangs past a cut-away wall still shows; a volume entirely behind a black shell is hidden by the shell's depth as before). The volumes past the first 8 (drawn by the haze passes, not their own renderer) are skipped on void pixels with the rest of the haze: a documented limitation.
3. **Interior from the floor grid, not a new 3D mask.** The 24 x 24 grid and the 165-vec4 record stay (every haze pass, the GL uniform arrays and the present UBO are sized for it). An EMPTY cell is the 12-bit step 4095 (heights now quantise to 0..4094, so the room's lowest floor, which is often the bounds' bottom, never collides with it). The interior's edge is the bilinear floor share's 0.3..0.7 smoothstep: at most one cell (bounds / 24) off the true wall, which the void test covers where it shows. The edge softness is the grid's resolution, not a look parameter, so it stays in the shader.
4. **Occupancy:** a cell has a floor when an up-facing face covers it with |n.y| >= 0.5 |n| (slopes up to 60 degrees; heights still come from the level faces, |n.y| >= 0.8 |n|, as before). Black-shell triangles (all three vertex colours 0, not a raw texture: `room_native.c` `group_is_black`'s test per triangle) count for neither, so the fog no longer lies on black wall tops either.
5. **Holes:** a closing with a square of radius `haze_interior_fill` cells (0..4, default 1: holes up to 2 cells across), the grid's outside counting as floor while eroding so a floor that reaches the bounds keeps its edge. A concave corner (an L-shaped room's notch) is never filled.
6. **Haze under the floor:** with the interior on, the uniform haze fades out within `haze_under_floor` units (0..256, default 32) under the lowest floor of the column (the void under a ledge); the ground fog keeps its own fade (one scale height).
7. **Heights unchanged:** every cell still gets a height as before (lowest of its 3 x 3, then grown outward), so `haze_interior 0` gives the old ground fog (except that black shells no longer lift it onto wall tops). Consumers of heights (`fit_to_floor`, `atmosphere_sweep.py` `floor_at`) are untouched; the cells come from a new `pc_scene_floor_cells`.
8. **Room settings, not a schema kind:** the five values follow the haze limits (`haze_max_opacity` ...): `set` lines in the lighting layers with project defaults, the Room settings panel's Atmosphere page, RPC `environment {haze: {interior, interior_fill, under_floor, void, void_level}}` (booleans or numbers) and the reply's `haze` object. The editor schema has no singleton for the room's air yet; the limits are not in it either.
9. **The medium record is always filled for the present** (Vulkan used to fill it only for haze Low): the void flag is read by every haze path.
10. **Debug view:** `haze_debug 2` paints red where a pixel is void, green the interior share at the surface point (both renderers).
11. **Not changed:** dynamic lights may still add a little light to a black shell (the test compares with the atmosphere off, so it measures the medium alone); the post profile's depth fog (`depth_fog_*`) is a separate effect and keeps its behaviour; ptgi.c's GL haze light is still marched on void GI pixels (the present no longer uses it there; a few pixels of bleed into floor pixels next to the void through gi_at's 4 x 4 haze average are accepted).

## File structure

Created:

| File | Responsibility |
|---|---|
| `pc/src/floor_grid.h`, `pc/src/floor_grid.c` | The floor grid's last steps without the game: heights (lowest of 3 x 3, grown outward, as before), the cells (EMPTY / SOLID / FILLED) by a closing. |
| `pc/tests/floor_grid_probe.c` | CTest probe of `floor_grid.c`. |
| `pc/tests/void_fog_smoke.py` | Headless regression: zone 48 room 0, the camera seeing the void through a cut-away wall; void black with fog on, fog on the floor, the switches, ghost walls, a volume, `haze_debug 2`. |

Modified:

| File | Change |
|---|---|
| `pc/src/scene_capture.c`, `scene_capture.h` | Rasterises occupancy (black shells left out), calls `pc_floor_grid_finish`, `pc_scene_floor_cells`. |
| `pc/src/atmosphere.h` | `PC_MEDIUM_GRID/INTERIOR/VOID`, `PC_MEDIUM_FLOOR_NONE`, the five settings' externs, record layout comments. |
| `pc/src/atmosphere.cpp` | The settings' definitions; packing (grid whenever haze or fog, sentinel, flags, 6.w, 7.w); RPC haze keys (bools); JSON (`haze` keys, `floor_grid.occupied`); `pc_medium_hash`. |
| `pc/src/room_settings.cpp` | Five Atmosphere settings. |
| `pc/shaders/vulkan/medium.glsl` | Flags, cell decode with "no floor", `medium_floor_quad`, `medium_room_air`, `medium_interior`, `medium_at` / `medium_maybe` / `medium_fog_base` through them, `medium_void_pixel`; header comment. |
| `pc/shaders/vulkan/scaled.frag` | Void pixels skip the haze (lit, unlit, `haze_debug`); `haze_debug 2`. |
| `pc/src/vk_present.c` | Fills `pu->medium` every frame. |
| `pc/src/gl_present.c` | The same void handling and `haze_debug 2` in the GL present shader. |
| `pc/tools/build_boot.py` | Compiles `floor_grid`. |
| `pc/CMakeLists.txt` | `floor_grid_probe`. |
| `pc/tests/run.py` | Registers `void_fog_smoke` (checkpoint suite). |
| `pc/docs/Vulkan_Renderer.md`, `pc/docs/Native_Rendering.md`, `lighting/README.md` | The interior, the void, the settings, the record. |

---

### Task 1: Worktree, data and baseline

**Files:** none changed.

- [ ] **Step 1: Worktree and data (copies, never symlinks)**

In `$WT` on a new branch (e.g. `claude/oob-fog`) from `main`:

```bash
cd "$WT"
cp -a "$MAIN/build" build
cp -a "$MAIN/data" data
cp -a "$MAIN/disks" disks
mkdir -p pc/build
cp "$MAIN/pc/build/test-card1.mcd" pc/build/
cp -a "$MAIN/pc/build/nrd" pc/build/   # the 64-bit build needs NRD
```

Expected: `pc/build/test-card1.mcd`, `data/`, `disks/`, `build/` present; `git status` shows nothing new tracked.

- [ ] **Step 2: CTest tree and the test binaries**

```bash
test -f pc/build/build.ninja || flock "$ROOD_HEAVY_LOCK" cmake -S pc -B pc/build -G Ninja
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
```

Expected: both builds exit 0 with a `Built …` line and no `error:` line.

- [ ] **Step 3: Baseline of the suites this plan touches**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest atmosphere_smoke atmosphere_sweep fog_volume_smoke fog_sim_smoke sky_smoke facing_cutaway
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan atmosphere_smoke atmosphere_sweep fog_volume_smoke fog_sim_smoke sky_smoke
```

Expected: tables ending in `N/N passed` (or a list of known failures). Write down every suite that already fails and its message; later tasks must not add failures.

---

### Task 2: The regression smoke, failing on the old code

**Files:**
- Create: `pc/tests/void_fog_smoke.py`
- Modify: `pc/tests/run.py` (a `Suite` after `atmosphere_sweep`, line ~212; `CHECKPOINT_SUITES`)

- [ ] **Step 1: Write the test**

Create `pc/tests/void_fog_smoke.py`:

```python
#!/usr/bin/env python3
"""The void past cut-away walls stays black with the atmosphere on
(pc/docs/Vulkan_Renderer.md "Haze, ground fog and sun shafts", "The room's
interior and the void"). The user's report: the Undercity (zone 48 room 0),
Ashley at about (972, 0, 1221); the game cuts away the camera-side walls (as
on the PS1) and the void around the room showed glowing mist: the ground fog
followed a floor grid whose empty cells grew from their neighbours, the haze
filled the room's bounds box, and the black shells the void is made of took
the haze like any surface.

Warps there (a made-up door onto Ashley's cell), stops the fog's drift, turns
the game camera through its eight yaws and keeps the one that shows the most
void: pixels the PS1 draws black whatever their texture (RPC graphics
{view_mode: 4}, the vertex colours: exactly black, eroded 3 pixels). At that
pose, on the 3D view (below the HUD's top band, above its bottom one),
luminance 0-255:

- with the room's generated fog and haze (A) against the atmosphere off (B):
  the void is as without it (mean |A - B| < 1, 99th percentile < 8) and dark;
- with a bright, purely emissive ground fog (it glows wherever it is): the
  haze's light alone (H, graphics {haze_debug: 1}) is black over the void and
  clearly there over the room's floor;
- the switches: environment {haze: {interior: false, void: false}} brings
  the old mist back over the void (the test sees what it is for);
- the classification view (haze_debug 2) marks the void pixels;
- the floor grid: the cell under Ashley has a floor (floor_grid.occupied 1);
- gameplay_walls smart with ghosts: walls the mask cuts are drawn again (or
  as ghosts) and what void is left is still clean;
- a fog volume at Ashley still draws;
- no crash, shader error or validation message.

    python3 pc/tests/void_fog_smoke.py [--card pc/build/test-card1.mcd] [--shots DIR]

ROOD_ARCH=64 ROOD_RENDERER=vulkan pick the Vulkan build (as pc/tests/run.py).
Frames stay under ROOD_OUT (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402
from facing_cutaway_smoke import kept  # noqa: E402
import lighting_format as lf  # noqa: E402

ROOM = (48, 0)
SPOT = (972, 0, 1221)  # the report's spot (world x, y, z; y down)
CELL = [7, 0, 9, 0]  # its 128-unit floor cell (x, level, z, facing): room_goto's made-up door
YAW = 0x1F800058  # scratch: camera.angles.vy
LUMA = np.array([0.299, 0.587, 0.114], np.float32)
GLOW_FOG = {"density": 0.01, "height": 48, "offset": 0, "noise": 0, "color": [0, 0, 0], "fill": [0.6, 0.6, 0.6],
            "sky": 0}  # emits, scatters nothing: it glows wherever it is


def view(image):
    h = image.shape[0]
    return image[int(h * 0.12):int(h * 0.78)]


def erode(mask, r):
    m = mask.copy()
    for _ in range(r):
        n = m.copy()
        n[1:] &= m[:-1]
        n[:-1] &= m[1:]
        n[:, 1:] &= m[:, :-1]
        n[:, :-1] &= m[:, 1:]
        m = n
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", default=str(default_card()))
    ap.add_argument("--shots", help="keep the frames here")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    gen = ROOT / "lighting/default/generated" / lf.room_file_name(*ROOM)
    if not gen.exists() or "env fog" not in gen.read_text():
        print(f"SKIP: no generated fog for zone {ROOM[0]} room {ROOM[1]} (pc/tools/generate_lighting.py)")
        return 77
    out = OUT / "void_fog_smoke"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shots = pathlib.Path(args.shots) if args.shots else out
    shots.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.card, out / "card1.mcd")
    packs, user = out / "packs", out / "user"
    shutil.copytree(ROOT / "lighting", packs)
    user.mkdir()
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(packs), "ROOD_GI": "1", "ROOD_SCALE": "2",
           "ROOD_LIGHTING_PRESET": "ultra"}
    if vulkan and os.environ.get("VK_LAYER_PATH"):
        env.setdefault("ROOD_VK_VALIDATE", "1")
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-void-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)

    def yaw():
        return struct.unpack("<I", game.read(YAW, 4))[0] & 0xFFF

    def turn():
        """R1: the game camera turns 45 degrees; wait until it holds still."""
        start = yaw()
        game.call("input", buttons=["r1"], vsyncs=4)
        previous, steady = start, 0
        for _ in range(90):
            game.run(2)
            y = yaw()
            steady = steady + 1 if y == previous else 0
            previous = y
            if y != start and steady >= 10:
                break
        game.run(30)

    def frame(name, vsyncs):
        game.run(vsyncs)
        path = game.present_screenshot(shots / f"void-{name}.png")
        return view(np.asarray(Image.open(path).convert("RGB"), np.float32))

    def void_mask(name):
        """Pixels the PS1 draws black whatever their texture: the vertex colours (view_mode 4) exactly black."""
        game.call("graphics", view_mode=4)
        rgb = frame(f"{name}-vertex", 20)
        game.call("graphics", view_mode=0)
        game.run(4)
        return erode(rgb.max(axis=2) <= 1, 3)

    def atmosphere(name, density):
        """The frame with the atmosphere (A), its light alone (H), without it (B)."""
        a = frame(f"{name}-on", 150) @ LUMA
        game.call("graphics", haze_debug=1)
        h = frame(f"{name}-haze", 20) @ LUMA
        game.call("graphics", haze_debug=0, fog_density=0)
        game.call("environment", fog_global=0)
        b = frame(f"{name}-off", 60) @ LUMA
        game.call("graphics", fog_density=density)
        game.call("environment", fog_global=1)
        return a, h, b

    def groups():
        r = game.call("room_groups")
        return r["facing"], r["groups"]

    try:
        game.call("graphics", gameplay_walls="original", gameplay_walls_ghost=False)
        reply = game.call("room_goto", zone=ROOM[0], room=ROOM[1], cell=CELL, enemies=False, scripts=False)
        if not reply.get("ok"):
            print(f"(cell {CELL} refused: {reply.get('message')}; the room's own door)", flush=True)
            reply = game.call("room_goto", zone=ROOM[0], room=ROOM[1], enemies=False, scripts=False)
        for _ in range(120):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        st = game.state()
        if not check((st["zone"], st["room"]) == ROOM, f"warped to zone {ROOM[0]} room {ROOM[1]} "
                                                      f"(at {st['zone']}/{st['room']})"):
            return 1
        player = st["player"]
        check(abs(player["x"] - SPOT[0]) < 160 and abs(player["z"] - SPOT[2]) < 160,
              f"Ashley at the report's spot ({player['x']}, {player['y']}, {player['z']}; want about {SPOT})")
        e = game.call("environment", fog_time=5)
        density = e["haze"]["density"]
        check(e["fog"]["defined"], "the room has its generated ground fog")
        grid = e["fog"].get("floor_grid") or {}
        occupied = grid.get("occupied")
        if check(occupied is not None, "the floor grid reports where the room has a floor (occupied)"):
            n, (cx, cz), (sx, sz) = grid["n"], grid["corner"], grid["cell"]
            i = min(max(int((player["x"] - cx) // sx), 0), n - 1)
            j = min(max(int((player["z"] - cz) // sz), 0), n - 1)
            empty = sum(1 for c in occupied if c == 0)
            check(occupied[j * n + i] == 1, f"the cell under Ashley has a floor ({occupied[j * n + i]}); "
                                            f"{empty} of {n * n} cells have none")

        # ---- the pose: the yaw that shows the most void ----
        best = None
        for k in range(8):
            mask = void_mask(f"yaw{k}")
            print(f"yaw {yaw():#05x}: void {mask.mean() * 100:.1f} %", flush=True)
            if best is None or mask.mean() > best[1]:
                best = (k, mask.mean())
            turn()
        for _ in range(best[0]):  # eight turns brought it back to the start
            turn()
        void = void_mask("pose")
        facing, gs = groups()
        cut = [i for i, g in enumerate(gs) if kept(g["flags"], facing) is False]
        check(void.mean() >= 0.03 and cut, f"the camera sees the void through cut-away walls (yaw {yaw():#05x}: "
                                           f"{void.mean() * 100:.1f} % of the view, {len(cut)} groups cut)")

        # ---- the room's own fog and haze: the void as without them ----
        a, h, b = atmosphere("room", density)
        v = void & (b <= 12)
        d = a[v] - b[v]
        check(v.mean() >= 0.02 and abs(float(d.mean())) < 1.0 and float(np.percentile(np.abs(d), 99)) < 8,
              f"the void with the room's fog and haze is as without them (mean {d.mean():+.2f}, "
              f"99th percentile {np.percentile(np.abs(d), 99):.1f}, {v.mean() * 100:.1f} % of the view)")
        check(float(a[v].mean()) < 10, f"the void stays dark ({a[v].mean():.1f})")

        # ---- a glowing fog: where the medium is ----
        game.call("environment", fog=GLOW_FOG)
        _, glow, _ = atmosphere("glow", density)
        inside = erode(~void, 3) & (b > 8)
        check(float(glow[v].mean()) < 1.0, f"a glowing fog's light over the void: {glow[v].mean():.2f} (black)")
        check(inside.mean() > 0.2 and float(glow[inside].mean()) > 3.0,
              f"it still shows over the room ({glow[inside].mean():.1f} on {inside.mean() * 100:.0f} % of the view)")
        game.call("environment", haze={"interior": False, "void": False})
        _, old, _ = atmosphere("glow-old", density)
        check(float(old[v].mean()) > float(glow[v].mean()) + 1.0,
              f"interior and void off: the old mist over the void comes back ({old[v].mean():.1f})")
        game.call("environment", haze={"interior": True, "void": True})
        game.call("graphics", haze_debug=2)
        classes = frame("classes", 20)
        game.call("graphics", haze_debug=0)
        check(float((classes[..., 0][v] > 128).mean()) > 0.8,
              f"haze_debug 2 marks the void ({(classes[..., 0][v] > 128).mean() * 100:.0f} % of it red)")

        # ---- gameplay_walls smart with ghosts: an alternative look ----
        game.call("graphics", gameplay_walls="smart", gameplay_walls_ghost=True)
        game.run(120)
        facing, gs = groups()
        cut = [i for i, g in enumerate(gs) if kept(g["flags"], facing) is False]
        back = [i for i in cut if gs[i]["drawn"] == 1]
        ghost_void = void_mask("ghost")
        check(back and ghost_void.mean() <= void.mean() + 0.005,
              f"smart walls with ghosts draw cut walls again {back} (void {ghost_void.mean() * 100:.1f} % vs "
              f"{void.mean() * 100:.1f} %)")
        _, glow_ghost, b_ghost = atmosphere("ghost", density)
        vg = ghost_void & (b_ghost <= 12)
        check(vg.sum() == 0 or float(glow_ghost[vg].mean()) < 1.0,
              f"what void is left stays clean ({glow_ghost[vg].mean() if vg.sum() else 0:.2f})")
        game.call("graphics", gameplay_walls="original", gameplay_walls_ghost=False)
        game.run(60)

        # ---- a fog volume still draws (exempt from the interior and the void) ----
        game.call("environment", remove_fog=True, haze={"density": 0})
        vol = {"id": "test0000000001", "pos": [player["x"], player["y"] - 80, player["z"]], "size": [240, 160, 240],
               "density": 0.01, "edge": 0.2, "noise": 0, "color": [0, 0, 0], "fill": [0.6, 0.6, 0.6]}
        game.call("environment", volumes=[vol])
        game.call("graphics", haze_debug=1)
        lit = frame("volume", 150) @ LUMA > 6
        game.call("graphics", haze_debug=0)
        check(0.005 < lit.mean() < 0.7, f"a fog volume at Ashley draws ({lit.mean() * 100:.1f} % of the view)")
        game.call("environment", volumes=[{"id": "test0000000001", "remove": True}])
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    bad = [line for line in log.splitlines() if "validation:" in line or "[crash]" in line or "[gl] shader" in line]
    check(not bad, f"no crash, shader error or validation message {bad[:3]}")
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Register it**

In `pc/tests/run.py`, after the `atmosphere_sweep` `Suite(...)` (inside the same `if args.save.exists()` list):

```python
            Suite("void_fog_smoke", [PY, str(TESTS / "void_fog_smoke.py"), "--card", str(args.save)], 420,
                  note="the void past cut-away walls (Undercity, zone 48 room 0) stays black with the fog and "
                       "haze on; fog still on the floor; interior/void switches; ghost walls; a volume draws"),
```

and add `"void_fog_smoke"` to `CHECKPOINT_SUITES` (after `"atmosphere_smoke"`).

- [ ] **Step 3: Run it on the old code: it must fail**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 void_fog_smoke
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan void_fog_smoke
```

Expected: FAIL on both, at least: "the floor grid reports where the room has a floor (occupied)", "the void with the room's fog and haze is as without them" and/or "a glowing fog's light over the void", "interior and void off ..." (no switch yet), "haze_debug 2 marks the void". The pose checks (warp, Ashley's spot, "the camera sees the void") must PASS; if they do not, fix the pose first:

- Ashley elsewhere: read her position, adjust `CELL` (x = floor(972 / 128), z = floor(1221 / 128); try facing 0-3) until she lands within 160 units of `SPOT`.
- No void at any yaw: look at `void-yaw*-vertex.png`; if the void is not exactly black in view_mode 4 (post noise), raise the `<= 1` to the smallest value that keeps the room's floor out of the mask (print a histogram of `rgb.max(axis=2)` over the view).

If the void checks PASS on the old code, stop and investigate before implementing (superpowers:systematic-debugging): look at `void-room-on.png` / `void-glow-haze.png` against the report; the plan assumes the mist sits over black-shell or empty pixels.

- [ ] **Step 4: Commit**

```bash
git add pc/tests/void_fog_smoke.py pc/tests/run.py
git commit -m "Tests: void_fog_smoke, the void past cut-away walls stays black with the fog on (fails: the old medium fills it)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The floor grid's cells (pure, with a probe)

**Files:**
- Create: `pc/src/floor_grid.h`, `pc/src/floor_grid.c`, `pc/tests/floor_grid_probe.c`
- Modify: `pc/CMakeLists.txt` (after the `selection_probe` block)

- [ ] **Step 1: Write the failing probe**

Create `pc/tests/floor_grid_probe.c`:

```c
/* The room's floor grid's last steps (pc/src/floor_grid.c) without the game:
 * where the room is (its cells), holes filled, nothing grown past its walls;
 * heights as before. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "floor_grid.h"

enum { N = 12 };
static int failures;
static float lowest[N * N], heights[N * N];
static unsigned char has[N * N], occupied[N * N], cells[N * N];

static void check(int ok, const char* what)
{
    printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
    failures += !ok;
}
static void clear(void)
{
    memset(lowest, 0, sizeof lowest);
    memset(has, 0, sizeof has);
    memset(occupied, 0, sizeof occupied);
}
/* Level floor at height y over cells i0..i1 x j0..j1. */
static void floor_rect(int i0, int j0, int i1, int j1, float y)
{
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            lowest[j * N + i] = y, has[j * N + i] = 1, occupied[j * N + i] = 1;
}
static void hole(int i0, int j0, int i1, int j1)
{
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            has[j * N + i] = 0, occupied[j * N + i] = 0;
}
static int all_cells(int i0, int j0, int i1, int j1, int kind)
{
    int ok = 1;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            ok &= cells[j * N + i] == kind;
    return ok;
}

int main(void)
{
    /* An L-shaped room: floor (y 0) over x 0-5, z 0-11 and x 6-11, z 0-5; the
     * quadrant x 6-11, z 6-11 lies outside its walls. */
    clear();
    floor_rect(0, 0, 5, 11, 0);
    floor_rect(6, 0, 11, 5, 0);
    check(pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells), "a floor: a grid");
    check(all_cells(6, 6, 11, 11, PC_FLOOR_EMPTY), "the cells outside the walls stay empty (not grown into)");
    check(all_cells(0, 0, 5, 11, PC_FLOOR_SOLID) && all_cells(6, 0, 11, 5, PC_FLOOR_SOLID), "the floor's cells are solid");
    check(fabsf(heights[8 * N + 8]) < 1e-3f, "a cell without a floor still gets a height (the old fog's base)");
    pc_floor_grid_finish(N, 2, lowest, has, occupied, heights, cells);
    check(all_cells(6, 6, 11, 11, PC_FLOOR_EMPTY), "a concave corner is never filled (fill 2)");

    /* Holes in a full floor. */
    clear();
    floor_rect(0, 0, 11, 11, 0);
    hole(5, 5, 5, 5);
    hole(1, 8, 2, 9);
    hole(7, 7, 9, 9);
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(cells[5 * N + 5] == PC_FLOOR_FILLED, "fill 1: a one-cell hole is filled");
    check(all_cells(1, 8, 2, 9, PC_FLOOR_FILLED), "fill 1: a two-cell hole is filled");
    check(all_cells(7, 7, 9, 9, PC_FLOOR_EMPTY), "fill 1: a three-cell hole stays empty");
    pc_floor_grid_finish(N, 2, lowest, has, occupied, heights, cells);
    check(all_cells(7, 7, 9, 9, PC_FLOOR_FILLED), "fill 2: the three-cell hole is filled");
    pc_floor_grid_finish(N, 0, lowest, has, occupied, heights, cells);
    check(cells[5 * N + 5] == PC_FLOOR_EMPTY, "fill 0: nothing is filled");

    /* Heights as before: each cell the lowest (largest y, y down) of its 3 x 3. */
    clear();
    floor_rect(0, 0, 11, 11, 0);
    floor_rect(3, 3, 3, 3, -100); /* a crate top */
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(fabsf(heights[3 * N + 3]) < 1e-3f && cells[3 * N + 3] == PC_FLOOR_SOLID,
        "a raised cell takes the floor around it (the lowest of its 3 x 3)");

    /* A slope: a floor (occupied) without a level face. */
    clear();
    floor_rect(0, 0, 11, 11, 0);
    has[6 * N + 6] = 0;
    pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells);
    check(cells[6 * N + 6] == PC_FLOOR_SOLID, "a sloped floor is solid without a level face");

    clear();
    check(!pc_floor_grid_finish(N, 1, lowest, has, occupied, heights, cells), "no floor: no grid");
    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
```

In `pc/CMakeLists.txt`, after the `selection_probe` block (line ~51):

```cmake
# The room's floor grid's last steps (pc/src/floor_grid.c: the ground fog's floor, the room's interior) without the game.
add_executable(floor_grid_probe tests/floor_grid_probe.c src/floor_grid.c)
target_include_directories(floor_grid_probe PRIVATE src)
target_compile_options(floor_grid_probe PRIVATE -Wall -Wextra -Werror)
target_link_libraries(floor_grid_probe PRIVATE m)
add_test(NAME floor_grid_probe COMMAND floor_grid_probe)
```

- [ ] **Step 2: Run it: it fails to build**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target floor_grid_probe
```

Expected: `fatal error: floor_grid.h: No such file or directory`.

- [ ] **Step 3: Implement**

Create `pc/src/floor_grid.h`:

```c
/* The room's floor grid's last steps (scene_capture.c pc_scene_floor_grid),
 * without the game. Rooms: pc/docs/Vulkan_Renderer.md "The room's interior
 * and the void". */
#ifndef ROOD_FLOOR_GRID_H
#define ROOD_FLOOR_GRID_H
#ifdef __cplusplus
extern "C" {
#endif
/* A cell (scene_capture.h pc_scene_floor_cells): no floor (outside the
 * room's walls, the void), a floor, a small hole the closing fills. */
#define PC_FLOOR_EMPTY 0
#define PC_FLOOR_SOLID 1
#define PC_FLOOR_FILLED 2
#define PC_FLOOR_GRID_MAX 64
/* From an n x n grid (n <= PC_FLOOR_GRID_MAX; [j * n + i]: i in x, j in z):
 * lowest (where has is set) the lowest level up-facing face over the cell
 * (world y, y down: the largest), occupied where any up-facing face of the
 * room covers it (level or sloped; not a black shell):
 * - heights: each cell the lowest of the level floors in its 3 x 3, the
 *   cells without one the mean of their neighbours', growing outward (every
 *   cell gets one, as before the cells existed);
 * - cells: SOLID where occupied or has; FILLED where a closing (dilated by
 *   `fill` cells, then eroded by `fill`, a square; outside the grid counts
 *   as floor while eroding) covers a cell without a floor: holes up to
 *   2 x fill cells across, never past the room's edge or into a concave
 *   corner; EMPTY elsewhere.
 * Returns 0 (outputs untouched) when no cell has a level floor. */
int pc_floor_grid_finish(int n, int fill, const float* lowest, const unsigned char* has, const unsigned char* occupied,
    float* heights, unsigned char* cells);
#ifdef __cplusplus
}
#endif
#endif
```

Create `pc/src/floor_grid.c` (the heights loop is the old `pc_scene_floor_grid`'s, moved):

```c
/* The room's floor grid's last steps (floor_grid.h). */
#include <string.h>

#include "floor_grid.h"

enum { M = PC_FLOOR_GRID_MAX };

int pc_floor_grid_finish(int n, int fill, const float* lowest, const unsigned char* has, const unsigned char* occupied,
    float* heights, unsigned char* cells)
{
    static float grown[M * M];
    static unsigned char known[M * M], next[M * M], ground[M * M], dilated[M * M];
    if (n <= 0 || n > M)
        return 0;
    int any = 0;
    for (int k = 0; k < n * n; ++k)
        any |= has[k];
    if (!any)
        return 0;
    /* each cell the lowest of its 3 x 3 (cells with a level floor) */
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int at = j * n + i, found = 0;
            float y = 0;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    int x = i + di, z = j + dj;
                    if (x < 0 || z < 0 || x >= n || z >= n || !has[z * n + x])
                        continue;
                    float h = lowest[z * n + x];
                    if (!found || h > y)
                        y = h, found = 1;
                }
            grown[at] = y, next[at] = (unsigned char)found;
        }
    /* cells still without one: the mean of their neighbours', growing outward */
    for (int round = 0; round < 2 * n; ++round) {
        int missing = 0;
        memcpy(known, next, (size_t)(n * n));
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                int at = j * n + i;
                if (known[at])
                    continue;
                float sum = 0;
                int count = 0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        int x = i + di, z = j + dj;
                        if (x >= 0 && z >= 0 && x < n && z < n && known[z * n + x])
                            sum += grown[z * n + x], count++;
                    }
                if (count)
                    grown[at] = sum / count, next[at] = 1;
                else
                    missing++;
            }
        if (!missing)
            break;
    }
    memcpy(heights, grown, sizeof(float) * (size_t)(n * n));
    /* the room's interior: its floor, and the holes a closing fills */
    if (fill < 0)
        fill = 0;
    for (int k = 0; k < n * n; ++k)
        ground[k] = occupied[k] || has[k];
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int hit = 0;
            for (int dj = -fill; dj <= fill && !hit; ++dj)
                for (int di = -fill; di <= fill && !hit; ++di) {
                    int x = i + di, z = j + dj;
                    hit = x >= 0 && z >= 0 && x < n && z < n && ground[z * n + x];
                }
            dilated[j * n + i] = (unsigned char)hit;
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int at = j * n + i, closed = 1;
            for (int dj = -fill; dj <= fill && closed; ++dj)
                for (int di = -fill; di <= fill && closed; ++di) {
                    int x = i + di, z = j + dj;
                    closed = x < 0 || z < 0 || x >= n || z >= n || dilated[z * n + x];
                }
            cells[at] = ground[at] ? PC_FLOOR_SOLID : closed ? PC_FLOOR_FILLED : PC_FLOOR_EMPTY;
        }
    return 1;
}
```

- [ ] **Step 4: Run the probe: it passes**

```bash
flock "$ROOD_HEAVY_LOCK" cmake --build pc/build --target floor_grid_probe
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R floor_grid_probe --output-on-failure
```

Expected: every line `ok:`, `PASS`, `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
git add pc/src/floor_grid.h pc/src/floor_grid.c pc/tests/floor_grid_probe.c pc/CMakeLists.txt
git commit -m "Floor grid: cells (empty, floor, filled hole) by a closing; heights as before (floor_grid.c, probe)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The capture uses it: occupancy, black shells left out, `pc_scene_floor_cells`

**Files:**
- Modify: `pc/src/scene_capture.c` (`pc_scene_floor_grid`, lines 518-636), `pc/src/scene_capture.h` (after `pc_scene_floor_grid`, line 67), `pc/tools/build_boot.py` (line 922 list: `"scene_capture", "floor_grid",`)

- [ ] **Step 1: The failing check**

The smoke's "the floor grid reports where the room has a floor (occupied)" fails (Task 2). It passes after Task 6 (the JSON); this task is checked by building and by the probe staying green.

- [ ] **Step 2: Implement**

`pc/src/scene_capture.h`, replace the `pc_scene_floor_grid` comment and add the cells (after line 67):

```c
/* The room's floor seen from above, a PC_SCENE_FLOOR_GRID x PC_SCENE_FLOOR_GRID
 * grid over its bounds (x, z): per cell the lowest up-facing level face's
 * height (world y; the street, not the roofs above it), each cell the
 * lowest of its neighbours too (a street's edge is the street), cells
 * without a floor from their neighbours. Black shells (every vertex colour
 * 0) count for nothing. heights[j * N + i]: cell i in x, j in z; corner
 * (lo_x, lo_z), cell size (cell_x, cell_z). Cached per capture. Returns 0
 * without a captured room (or no level face). The ground fog lies on it
 * (atmosphere.cpp). */
#define PC_SCENE_FLOOR_GRID 24
int pc_scene_floor_grid(float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID], float corner[2], float cell[2]);
/* The same grid's cells (floor_grid.h PC_FLOOR_EMPTY / SOLID / FILLED):
 * where the room has a floor (any up-facing face up to 60 degrees, not a
 * black shell), and its holes up to 2 x the room setting haze_interior_fill
 * cells across. The haze and ground fog keep to them (atmosphere.cpp). */
int pc_scene_floor_cells(unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID]);
```

`pc/src/scene_capture.c`: add `#include "floor_grid.h"` after `#include "room_effects.h"`, and replace the whole of `pc_scene_floor_grid` (from its comment at line 518 to its closing brace at line 636) with:

```c
/* The floor seen from above (scene_capture.h). Up-facing: the room's
 * polygons wind one way seen from their front (b - a) x (c - a) has +y (y
 * down) on a floor seen from above, -y on a ceiling or an arch's underside;
 * a room whose level faces all wind the other way takes them all. */
extern float pc_haze_interior_fill __attribute__((weak)); /* atmosphere.cpp: room setting haze_interior_fill */
static int floor_fill(void)
{
    int n = &pc_haze_interior_fill ? (int)lroundf(pc_haze_interior_fill) : 1;
    return n < 0 ? 0 : n > 4 ? 4 : n;
}
/* A black shell's triangle: every vertex colour 0 and not a raw texture
 * (room_native.c group_is_black, per triangle): drawn black whatever its
 * texture, the void around the room. */
static int black_triangle(const scene_triangle* t)
{
    if (t->code & 1)
        return 0;
    for (int j = 0; j < 3; ++j)
        if (t->vertex[j].rgb[0] || t->vertex[j].rgb[1] || t->vertex[j].rgb[2])
            return 0;
    return 1;
}
enum { FLOOR_N = PC_SCENE_FLOOR_GRID };
static float floor_heights[FLOOR_N * FLOOR_N], floor_corner[2], floor_cell[2];
static unsigned char floor_cells[FLOOR_N * FLOOR_N];
static int floor_serial = -1, floor_count = -1, floor_fill_used = -1, floor_ok;
static void floor_mark(int at, float y, int level, float* lowest, unsigned char* has, unsigned char* occupied)
{
    occupied[at] = 1;
    if (level && (!has[at] || y > lowest[at]))
        lowest[at] = y, has[at] = 1;
}
/* The cells whose centre the triangle covers, and the cells of its corners and centroid. */
static void floor_raster(const float* a, const float* b, const float* c, const float lo[3], int level, float* lowest,
    unsigned char* has, unsigned char* occupied)
{
    float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float y = (a[1] + b[1] + c[1]) / 3.0f;
    float px[4] = { a[0], b[0], c[0], (a[0] + b[0] + c[0]) / 3 }, pz[4] = { a[2], b[2], c[2], (a[2] + b[2] + c[2]) / 3 };
    int i0 = FLOOR_N, i1 = -1, j0 = FLOOR_N, j1 = -1;
    for (int k = 0; k < 4; ++k) {
        int i = (int)((px[k] - lo[0]) / floor_cell[0]), j = (int)((pz[k] - lo[2]) / floor_cell[1]);
        i = i < 0 ? 0 : i >= FLOOR_N ? FLOOR_N - 1 : i, j = j < 0 ? 0 : j >= FLOOR_N ? FLOOR_N - 1 : j;
        i0 = i < i0 ? i : i0, i1 = i > i1 ? i : i1, j0 = j < j0 ? j : j0, j1 = j > j1 ? j : j1;
        floor_mark(j * FLOOR_N + i, y, level, lowest, has, occupied);
    }
    float area2 = u[0] * v[2] - u[2] * v[0]; /* twice the signed area in x, z */
    if (fabsf(area2) < 1e-6f)
        return;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            float x = lo[0] + (i + 0.5f) * floor_cell[0] - a[0], z = lo[2] + (j + 0.5f) * floor_cell[1] - a[2];
            float s = (x * v[2] - z * v[0]) / area2, r = (u[0] * z - u[2] * x) / area2;
            if (s < 0 || r < 0 || s + r > 1)
                continue;
            floor_mark(j * FLOOR_N + i, y, level, lowest, has, occupied);
        }
}
static void floor_update(void)
{
    int fill = floor_fill();
    if (floor_serial == scene_serial && floor_count == triangle_count && floor_fill_used == fill)
        return;
    floor_serial = scene_serial, floor_count = triangle_count, floor_fill_used = fill;
    float lo[3], hi[3];
    pc_scene_bounds(lo, hi);
    floor_corner[0] = lo[0], floor_corner[1] = lo[2];
    floor_cell[0] = fmaxf((hi[0] - lo[0]) / FLOOR_N, 1.0f), floor_cell[1] = fmaxf((hi[2] - lo[2]) / FLOOR_N, 1.0f);
    static float lowest[FLOOR_N * FLOOR_N];
    static unsigned char has[FLOOR_N * FLOOR_N], occupied[FLOOR_N * FLOOR_N];
    float up_area = 0, all_area = 0;
    for (int pass = 0; pass < 2; ++pass) {
        /* pass 0 counts the level faces; pass 1 rasterises (up-facing only, unless almost none are) */
        int up_only = pass == 1 && up_area > all_area * 0.05f;
        memset(has, 0, sizeof has);
        memset(occupied, 0, sizeof occupied);
        for (int t = 0; t < triangle_count; ++t) {
            const float *a = triangles[t].vertex[0].position, *b = triangles[t].vertex[1].position,
                        *c = triangles[t].vertex[2].position;
            float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            float n[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len < 1e-3f || fabsf(n[1]) < 0.5f * len || black_triangle(&triangles[t]))
                continue; /* a wall, or the void's black shell */
            int level = fabsf(n[1]) >= 0.8f * len; /* stairs' treads and gentle slopes are */
            if (pass == 0) {
                if (level)
                    all_area += len, up_area += n[1] > 0 ? len : 0;
                continue;
            }
            if (up_only && n[1] <= 0)
                continue;
            floor_raster(a, b, c, lo, level, lowest, has, occupied);
        }
    }
    floor_ok = pc_floor_grid_finish(FLOOR_N, fill, lowest, has, occupied, floor_heights, floor_cells);
}
int pc_scene_floor_grid(float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID], float corner[2], float cell[2])
{
    floor_update();
    if (floor_ok) {
        memcpy(heights, floor_heights, sizeof floor_heights);
        corner[0] = floor_corner[0], corner[1] = floor_corner[1];
        cell[0] = floor_cell[0], cell[1] = floor_cell[1];
    }
    return floor_ok;
}
int pc_scene_floor_cells(unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID])
{
    floor_update();
    if (floor_ok)
        memcpy(cells, floor_cells, sizeof floor_cells);
    return floor_ok;
}
```

Note: the old function returned the cached grid; `floor_ok` stays 0 (and the outputs untouched) without a level face, as before. Check `scene_triangle`'s vertex colour field name in `scene_capture.h` (`rgb[3]`, as `add_triangle`'s caller sets) and that `PC_SCENE_FLOOR_GRID` (24) <= `PC_FLOOR_GRID_MAX`.

`pc/tools/build_boot.py` line 922: insert `"floor_grid",` right after `"scene_capture",` in the host-unit list (it is then also cross-compiled for Windows).

- [ ] **Step 3: Build and run the probe, the old atmosphere suites**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" ctest --test-dir pc/build -R floor_grid_probe --output-on-failure
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 atmosphere_smoke atmosphere_sweep
```

Expected: build exit 0; probe PASS; both suites as in the baseline (only the black shells' faces left the heights; the sweep's "fog's floor under Ashley" stays below her feet).

- [ ] **Step 4: Commit**

```bash
git add pc/src/scene_capture.c pc/src/scene_capture.h pc/tools/build_boot.py
git commit -m "Scene capture: the floor grid's cells (where the room has a floor), black shells left out (pc_scene_floor_cells)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The five room settings

**Files:**
- Modify: `pc/src/atmosphere.h` (after `pc_haze_max_forward`'s extern, line ~127), `pc/src/atmosphere.cpp` (the limits' definitions, line ~55), `pc/src/room_settings.cpp` (extern list line 25; `settings[]` after `haze_max_forward`, line 59)

- [ ] **Step 1: The failing check**

The smoke's "interior and void off: the old mist over the void comes back" needs the RPC keys (Task 6); this task only adds the values. Check: after the build, `python3 pc/tools/rood_rpc.py` is not needed; the panel shows them (Step 3).

- [ ] **Step 2: Implement**

`pc/src/atmosphere.h`, after the `pc_haze_max_*` extern:

```c
/* Where the room's air is (room settings; pc/docs/Vulkan_Renderer.md "The
 * room's interior and the void"): haze_interior (1: the uniform haze and the
 * ground fog keep to the floor grid's cells with a floor, not the room's
 * bounds box; 0 as before), haze_interior_fill (holes up to twice this many
 * cells across count as floor; 0..4), haze_under_floor (the haze fades out
 * within this many world units under the lowest floor), haze_void (1: pixels
 * that show the void around the room, nothing drawn or a surface the PS1
 * draws black whatever its texture, get no haze or ground fog; the fog
 * volumes are exempt), haze_void_level (the brightest vertex colour, of 255,
 * that still counts as that black). */
extern float pc_haze_interior, pc_haze_interior_fill, pc_haze_under_floor, pc_haze_void, pc_haze_void_level;
```

`pc/src/atmosphere.cpp`, inside the `extern "C" { ... }` holding the limits (after `float pc_haze_max_forward = 3.0f;`):

```cpp
/* Where the room's air is (atmosphere.h). */
float pc_haze_interior = 1, pc_haze_interior_fill = 1, pc_haze_under_floor = 32, pc_haze_void = 1,
      pc_haze_void_level = 0;
```

`pc/src/room_settings.cpp`: append `, pc_haze_interior, pc_haze_interior_fill, pc_haze_under_floor, pc_haze_void, pc_haze_void_level` to the extern float list, and after the `haze_max_forward` row:

```cpp
    /* Where the room's air is (atmosphere.h): its interior, not its bounds;
     * clear over the void around the room. */
    { "haze_interior", "Keep to the room's interior", "Atmosphere", &pc_haze_interior, 0, 1, "toggle", false },
    { "haze_interior_fill", "Interior: holes filled (cells)", "Atmosphere", &pc_haze_interior_fill, 0, 4, "%.0f", false },
    { "haze_under_floor", "Interior: haze under the floor", "Atmosphere", &pc_haze_under_floor, 0, 256, "%.0f", false },
    { "haze_void", "Clear over the void", "Atmosphere", &pc_haze_void, 0, 1, "toggle", false },
    { "haze_void_level", "Void: brightest black (of 255)", "Atmosphere", &pc_haze_void_level, 0, 32, "%.0f", false },
```

(`builtin[64]` holds up to 64 settings; the table has about 36 after this.)

- [ ] **Step 3: Build; look at the panel**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
```

Expected: exit 0. Optional look (headless): launch with `rood_rpc.py`, `editor_ui {visible: true, details: "atmosphere"}`, screenshot under `pc/build/tests/`: five new rows on the Atmosphere page.

- [ ] **Step 4: Commit**

```bash
git add pc/src/atmosphere.h pc/src/atmosphere.cpp pc/src/room_settings.cpp
git commit -m "Room settings haze_interior, haze_interior_fill, haze_under_floor, haze_void, haze_void_level

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Packing the interior and the flags; RPC and JSON

**Files:**
- Modify: `pc/src/atmosphere.h` (record defines, after `PC_MEDIUM_FLOOR_DATA`), `pc/src/atmosphere.cpp` (`pc_medium_gpu_get` lines 768-800; RPC `haze` loop line 2115; `json()` line 2180; `pc_medium_hash` line 1100)

- [ ] **Step 1: The failing check**

`void_fog_smoke`'s "the floor grid reports where the room has a floor (occupied)" and the `haze {interior, void}` keys. Run after Step 3.

- [ ] **Step 2: Implement**

`pc/src/atmosphere.h`, after `#define PC_MEDIUM_FLOOR_DATA ...`:

```c
/* (PC_MEDIUM_FLOOR + 1).w: the record's switches. GRID: the floor grid is
 * packed (the ground fog lies on it); INTERIOR: the haze and ground fog keep
 * to its cells with a floor (a cell's 12-bit step PC_MEDIUM_FLOOR_NONE: none,
 * heights quantised to 0..4094); VOID: present passes leave void pixels
 * without the haze (record 7.w: the brightest black, (level + 0.5) / 255;
 * 6.w: haze_under_floor). */
#define PC_MEDIUM_GRID 1
#define PC_MEDIUM_INTERIOR 2
#define PC_MEDIUM_VOID 4
#define PC_MEDIUM_FLOOR_NONE 4095
```

`pc/src/atmosphere.cpp`, `pc_medium_gpu_get`: replace from `float sky = fog.defined ? ...` through the end of the `if (fogs && fog_on()) { ... }` block with:

```cpp
    float sky = fog.defined ? fmaxf(fog.sky, 0.0f) : 1.0f;
    bool fog_here = fogs && fog_on();
    /* The floor grid: the ground fog lies on it, and with haze_interior the
     * haze and the fog keep to its cells with a floor (the room's interior,
     * not its bounds: the void past a cut-away wall stays clear). */
    static float heights[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    static unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    float corner[2], cell[2], floor_y;
    int flags = 0;
    bool grid = pc_fog_floor_grid && (m[0][0] > 0 || fog_here) && pc_scene_floor_grid(heights, corner, cell);
    bool interior = grid && pc_haze_interior > 0.5f && pc_scene_floor_cells(cells);
    if (grid) {
        float* g = m[PC_MEDIUM_FLOOR];
        float* q = m[PC_MEDIUM_FLOOR + 1];
        float lo = room_lo[1], span = fmaxf(room_hi[1] - room_lo[1], 1.0f);
        g[0] = corner[0], g[1] = corner[1], g[2] = 1.0f / cell[0], g[3] = 1.0f / cell[1];
        q[0] = lo, q[1] = span / 4094.0f, q[2] = PC_MEDIUM_FLOOR_GRID;
        float* data = m[PC_MEDIUM_FLOOR_DATA];
        for (int k = 0; k < PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID; k += 2) {
            int step[2];
            for (int e = 0; e < 2; e++) {
                int s = (int)lroundf((heights[k + e] - lo) / span * 4094.0f);
                step[e] = interior && cells[k + e] == PC_FLOOR_EMPTY ? PC_MEDIUM_FLOOR_NONE
                                                                    : s < 0 ? 0 : s > 4094 ? 4094 : s;
            }
            data[k / 2] = (float)(step[0] * 4096 + step[1]); /* exact: below 2^24 */
        }
        flags |= PC_MEDIUM_GRID | (interior ? PC_MEDIUM_INTERIOR : 0);
    }
    if (fog_here) {
        if (grid)
            m[1][2] = fog.offset; /* the grid's floor minus it (medium.glsl medium_room_air) */
        else {
            if (!pc_scene_floor(&floor_y))
                floor_y = room_hi[1];
            m[1][2] = floor_y - fog.offset; /* y down: up is smaller y */
        }
        m[1][0] = fog.density;
        m[1][1] = fmaxf(fog.height, 1.0f);
        m[1][3] = fminf(fmaxf(fog.noise, 0.0f), 1.0f);
        for (int k = 0; k < 3; k++) {
            m[2][k] = fmaxf(fog.color[k], 0.0f);
            m[3][k] = fmaxf(fog.fill[k], 0.0f);
            m[4][k] = fog.wind[k];
        }
        m[3][3] = 1;
    }
    /* Void pixels (present passes: nothing drawn, or a surface the PS1 draws black) keep clear. */
    if (pc_haze_void > 0.5f)
        flags |= PC_MEDIUM_VOID;
    m[PC_MEDIUM_FLOOR + 1][3] = (float)flags;
    m[6][3] = fmaxf(pc_haze_under_floor, 0.0f);
    m[7][3] = (fminf(fmaxf(pc_haze_void_level, 0.0f), 255.0f) + 0.5f) / 255.0f;
```

(`#include "floor_grid.h"` near the other includes for `PC_FLOOR_EMPTY`. The bounds' `m[6][k]`, `m[7][k]` for k < 3 stay as they are, set above.)

RPC (`params.get("haze")` loop, line ~2115): replace the loop with

```cpp
    if (const prefab::Json* hz = params.get("haze")) { /* the room settings haze_density, haze_anisotropy, haze_glow, ... */
        for (const char* key : { "density", "anisotropy", "glow", "max_opacity", "max_light", "shafts", "max_forward",
                 "interior", "interior_fill", "under_floor", "void", "void_level" })
            if (const prefab::Json* j = hz->get(key)) {
                double v = j->type == prefab::Json::Bool ? (j->boolean ? 1.0 : 0.0) : j->number;
                char line[96];
                snprintf(line, sizeof line, "set haze_%s %.6f", key, v);
                pc_room_settings_parse(line);
                changed = true;
            }
    }
```

`json()`: after `grid = "{\"n\":" ... + "}";` build the cells, and add the settings to `haze`:

```cpp
    static unsigned char cells[PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID];
    if (pc_scene_floor_grid(heights, corner, cell) && pc_scene_floor_cells(cells)) {
        std::string occupied;
        for (int k = 0; k < PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID; k++)
            occupied += (k ? "," : "") + std::to_string(cells[k]);
        grid = "{\"n\":" + std::to_string(PC_SCENE_FLOOR_GRID) + ",\"corner\":" + jvec(corner, 2) + ",\"cell\":"
            + jvec(cell, 2) + ",\"heights\":" + jvec(heights, PC_SCENE_FLOOR_GRID * PC_SCENE_FLOOR_GRID)
            + ",\"occupied\":[" + occupied + "]}";
    }
```

(replacing the old `if (pc_scene_floor_grid(...)) grid = ...;`), and in the `haze` object after `max_forward`:

```cpp
        + ",\"interior\":" + jbool(pc_haze_interior > 0.5f) + ",\"interior_fill\":" + num(pc_haze_interior_fill, 0)
        + ",\"under_floor\":" + num(pc_haze_under_floor, 1) + ",\"void\":" + jbool(pc_haze_void > 0.5f)
        + ",\"void_level\":" + num(pc_haze_void_level, 0)
```

`pc_medium_hash`: before `return h;`, so the haze's history restarts when the interior changes:

```cpp
    char extra[96];
    snprintf(extra, sizeof extra, "%g %g %g %d", pc_haze_interior, pc_haze_interior_fill, pc_haze_under_floor,
        pc_scene_serial());
    for (const char* c = extra; *c; c++)
        h = (h ^ (unsigned char)*c) * 16777619u;
```

- [ ] **Step 3: Build, run the smoke**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 void_fog_smoke
```

Expected: build exit 0; now "the floor grid reports ... occupied" and "the cell under Ashley has a floor" PASS (note the printed count of empty cells). The void checks still FAIL (the shaders do not read the new record yet; with the sentinel unread, the old shader treats 4095 as the bounds' bottom: the fog over empty cells drops to there, which is the expected transitional state).

- [ ] **Step 4: Commit**

```bash
git add pc/src/atmosphere.h pc/src/atmosphere.cpp
git commit -m "Atmosphere: pack the room's interior (empty floor cells) and the medium's switches; RPC haze keys, floor_grid occupied

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The medium keeps to the interior (`medium.glsl`)

**Files:**
- Modify: `pc/shaders/vulkan/medium.glsl` (header comment lines 21-46; `medium_floor_cell` .. `medium_fog_base` lines 125-159; `medium_at` lines 237-266; `medium_maybe` lines 322-333)

- [ ] **Step 1: The failing check**

`void_fog_smoke`'s "a glowing fog's light over the void" (still failing after Task 6).

- [ ] **Step 2: Implement**

Header comment: replace the lines for 6, 7 and 89-91 with

```glsl
//  6, 7: the room's bounds (haze and ground fog fill them only); 6.w the haze's reach under the floor
//      (world units: it fades out there, haze_under_floor), 7.w the void's brightest black ((level + 0.5) / 255)
//  ...
//  89, 90: the ground fog's floor, a top-down grid over the room: x, z of its corner, cells per unit in x, z;
//      y of height 0, units per step, cells per side, w the switches (MEDIUM_FLAG_*: atmosphere.h PC_MEDIUM_GRID,
//      PC_MEDIUM_INTERIOR, PC_MEDIUM_VOID)
//  91..162: the grid's heights, two 12-bit steps per float (a * 4096 + b), row by row (z), x within a row;
//      4095: no floor there (outside the room; only with MEDIUM_FLAG_INTERIOR)
```

After `#define MEDIUM_DETAIL 164` add:

```glsl
#define MEDIUM_FLAG_GRID 1
#define MEDIUM_FLAG_INTERIOR 2
#define MEDIUM_FLAG_VOID 4
```

Replace `medium_floor_cell`, `medium_floor` and `medium_fog_base` with:

```glsl
// The record's switches (atmosphere.h PC_MEDIUM_GRID, PC_MEDIUM_INTERIOR, PC_MEDIUM_VOID).
int medium_flags() { return int(MEDIUM(MEDIUM_FLOOR + 1).w + 0.5); }
bool medium_grid() { return (medium_flags() & MEDIUM_FLAG_GRID) != 0; }
bool medium_interior_on() { return (medium_flags() & (MEDIUM_FLAG_GRID | MEDIUM_FLAG_INTERIOR)) == (MEDIUM_FLAG_GRID | MEDIUM_FLAG_INTERIOR); }
// One cell of the floor grid (i, j clamped to it): x its height (world y), y 1 with a floor, 0 without
// (step 4095: outside the room's walls, the void).
vec2 medium_floor_cell(int i, int j) {
    int n = int(MEDIUM(MEDIUM_FLOOR + 1).z + 0.5);
    int k = clamp(j, 0, n - 1) * n + clamp(i, 0, n - 1);
    float v = MEDIUM(MEDIUM_FLOOR_DATA + k / 8)[(k / 2) & 3];
    float a = floor(v / 4096.0);
    float q = (k & 1) == 0 ? a : v - a * 4096.0;
    bool none = q > 4094.5;
    return vec2(MEDIUM(MEDIUM_FLOOR + 1).x + (none ? 4094.0 : q) * MEDIUM(MEDIUM_FLOOR + 1).y, none ? 0.0 : 1.0);
}
// The four cells around x: bilinear weights w, heights h, floors f (1 with a floor).
void medium_floor_quad(vec3 x, out vec4 w, out vec4 h, out vec4 f) {
    vec2 c = (x.xz - MEDIUM(MEDIUM_FLOOR).xy) * MEDIUM(MEDIUM_FLOOR).zw - 0.5;
    vec2 t = c - floor(c);
    ivec2 i = ivec2(floor(c));
    vec2 a = medium_floor_cell(i.x, i.y), b = medium_floor_cell(i.x + 1, i.y);
    vec2 d = medium_floor_cell(i.x, i.y + 1), e = medium_floor_cell(i.x + 1, i.y + 1);
    w = vec4((1.0 - t.x) * (1.0 - t.y), t.x * (1.0 - t.y), (1.0 - t.x) * t.y, t.x * t.y);
    h = vec4(a.x, b.x, d.x, e.x);
    f = vec4(a.y, b.y, d.y, e.y);
}
// The floor under x from its cells: bilinear over the cells with a floor (the void's cells carry none).
float medium_quad_floor(vec4 w, vec4 h, vec4 f) {
    float s = dot(w, f);
    return s > 1e-4 ? dot(w * f, h) / s : dot(w, h);
}
// The ground fog's floor under x (world y, y down): the room's lowest floor
// there (scene_capture.c pc_scene_floor_grid), bilinear between cells; the
// room's main floor without a grid.
float medium_floor(vec3 x) {
    if (!medium_grid()) return MEDIUM(1).z;
    vec4 w, h, f;
    medium_floor_quad(x, w, h, f);
    return medium_quad_floor(w, h, f);
}
// How much of x's column is the room's interior (MEDIUM_FLAG_INTERIOR): 1 over
// the grid's cells with a floor, 0 over the cells without (outside the walls:
// the void past a cut-away wall), a soft edge where the bilinear share of
// floor crosses a half (the cells' border); 1 without the switch.
float medium_interior(vec3 x) {
    if (!medium_interior_on()) return 1.0;
    vec4 w, h, f;
    medium_floor_quad(x, w, h, f);
    return smoothstep(0.3, 0.7, dot(w, f));
}
// The room's own air at x (inside its bounds), one look-up of the grid: the
// uniform haze's share (the interior, fading out within MEDIUM(6).w units
// under the lowest floor) and the ground fog's density without noise:
// thickest at its base, falling off by e every scale height up (y down: the
// height above the base is base y - y). With the floor grid its base is the
// floor under x raised by its offset (MEDIUM(1).z), and under its base (under
// the lowest floor: the void past a ledge, a pit the grid smooths over) it
// fades within one more scale height, so no column holds an unbounded depth
// of it; with the interior, only over the room's floor. Without a grid (RPC
// environment {floor_grid: 0}, no level face) the base is the room's main
// floor and everything under it has the full density, as before.
void medium_room_air(vec3 x, out float haze_share, out float fog) {
    haze_share = 1.0;
    fog = 0.0;
    float height = max(MEDIUM(1).y, 1.0);
    if (!medium_grid()) {
        if (MEDIUM(1).x > 0.0) fog = MEDIUM(1).x * exp(-max(MEDIUM(1).z - x.y, 0.0) / height);
        return;
    }
    vec4 w, h, f;
    medium_floor_quad(x, w, h, f);
    float floor_y = medium_quad_floor(w, h, f);
    float inside = 1.0;
    if (medium_interior_on()) {
        inside = smoothstep(0.3, 0.7, dot(w, f));
        float under = x.y - floor_y; // y down: > 0 under the floor
        haze_share = under > 0.0 ? inside * (1.0 - smoothstep(0.0, max(MEDIUM(6).w, 1.0), under)) : inside;
    }
    if (MEDIUM(1).x > 0.0 && inside > 0.0) {
        float up = floor_y - MEDIUM(1).z - x.y;
        fog = inside * MEDIUM(1).x * exp(up >= 0.0 ? -up / height : -max(-up - height, 0.0) / height);
    }
}
float medium_fog_base(vec3 x) {
    float haze_share, fog;
    medium_room_air(x, haze_share, fog);
    return fog;
}
```

In `medium_at`, replace the `if (medium_in_room(x)) { ... }` block with:

```glsl
    if (medium_in_room(x)) {
        float share, f;
        medium_room_air(x, share, f);
        extinction = MEDIUM(0).x * share;
        scatter = vec3(extinction);
        if (f > 1e-9) {
            if (MEDIUM(1).w > 0.0) {
                n = medium_noise(x);
                f *= medium_wisps(n, MEDIUM(1).w);
            }
            extinction += f;
            scatter += f * MEDIUM(2).rgb;
            emit += f * MEDIUM(3).rgb;
        }
    }
```

(the volumes' loop after it is unchanged: they are exempt.) In `medium_maybe`, replace its first line with:

```glsl
    if (medium_in_room(x)) {
        float share, f;
        medium_room_air(x, share, f);
        if ((MEDIUM(0).x > 0.0 && share > 0.0) || f * (1.0 + MEDIUM(1).w) > 1e-7) return true;
    }
```

At the end of the file (before `#endif`), the void test for the present passes:

```glsl
// A present pass's pixel that shows the void around the room (MEDIUM_FLAG_VOID):
// nothing drawn (depth d >= 1), or a 3D surface the PS1 draws black whatever its
// texture, its vertex colour at most the record's level (MEDIUM(7).w; the rooms'
// black shells, Native_Rendering.md "Seeing into rooms"). shade: the G-buffer's
// shade target (a 0.5 untextured, 1 textured; 0.25 emissive and 0 unlit are
// never void). The room's haze and ground fog leave it alone; the fog volumes
// do not (an artist placed them).
bool medium_void_pixel(float d, vec4 shade) {
    if ((medium_flags() & MEDIUM_FLAG_VOID) == 0) return false;
    if (d >= 1.0) return true;
    return d * 65536.0 >= 0.5 && shade.a > 0.375 && max(max(shade.r, shade.g), shade.b) <= MEDIUM(7).w;
}
```

- [ ] **Step 3: Build both, run the atmosphere suites and the smoke**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 void_fog_smoke atmosphere_smoke atmosphere_sweep fog_volume_smoke
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan void_fog_smoke atmosphere_smoke
```

Expected: builds exit 0 (glslang compiles every haze shader; the GL strings come from `gl_shaders.py`), no `[gl] shader` or validation lines. In `void_fog_smoke` the glow-fog-over-void value drops (print it); some void checks may still fail where the ray to a black shell crosses interior air (Task 8 removes that); the "inside" and volume checks pass. The atmosphere suites as in the baseline.

- [ ] **Step 4: Commit**

```bash
git add pc/shaders/vulkan/medium.glsl
git commit -m "Medium: the haze and ground fog keep to the room's interior (floor cells), not its bounds box; void pixel test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Void pixels get no room medium (both present passes); `haze_debug 2`

**Files:**
- Modify: `pc/shaders/vulkan/scaled.frag` (`main`, lines 380-395, 493-495, 510-513), `pc/src/vk_present.c` (lines 1069-1074), `pc/src/gl_present.c` (the present shader's `main`: lines 509-531, 620-628, 640-642)

- [ ] **Step 1: The failing check**

`void_fog_smoke`: "the void with the room's fog and haze is as without them", "a glowing fog's light over the void", "haze_debug 2 marks the void".

- [ ] **Step 2: Implement (Vulkan)**

`scaled.frag` `main`: after `float d = depth_at(uv);` add

```glsl
    // The void around the room (medium.glsl medium_void_pixel): no haze or ground fog there.
    bool void_px = medium_void_pixel(d, texelFetch(shade_tex, texel(uv), 0));
```

Replace the `haze_debug` block with:

```glsl
    if (u.quality1.z != 0) { // haze_debug 1: the haze's light alone (over black); 2: where the room's air is
        bool ui_ = d * 65536.0 < 0.5;
        if (u.quality1.z == 2) { // red: a void pixel; green: the room's interior at the surface (medium_interior)
            float inside = !ui_ && d < 1.0 ? medium_interior(world_at(texel(uv), d * 65536.0)) : 0.0;
            color = vec4(!ui_ && void_px ? 1.0 : 0.0, inside, 0.0, ui_ ? 9999.0 : 0.0);
            return;
        }
        vec3 hz = vec3(0.0);
        if (!ui_ && !void_px && u.haze_grid.w != 0) hz = hazed(vec3(0.0), texel(uv), min(d, 1.0));
        else if (!ui_ && !void_px && u.quality0.x > 0.0) hz = low_haze(vec3(0.0), world_at(texel(uv), min(d, 0.999) * 65536.0));
        if (!ui_ && u.fogvol.z != 0) hz += fogvol_apply(vec3(0.0), texel(uv), d);
        color = vec4(min(hz, vec3(1.0)), ui_ ? 9999.0 : 0.0);
        return;
    }
```

In the lit branch, replace the two haze lines with:

```glsl
            if (void_px) haze_done = true; // the void: clean darkness, as on the PS1
            else if (u.haze_grid.w != 0) { c = hazed(c, q, d); haze_done = true; }
            else if (u.quality0.x > 0.0) { c = low_haze(c, world); haze_done = true; }
```

and add `&& !void_px` to both unlit tail conditions:

```glsl
    if (u.haze_grid.w != 0 && !haze_done && !ui && !void_px && d < 1.0 && view_mode != 1) c = hazed(c, texel(uv), d);
    else if (u.quality0.x > 0.0 && !haze_done && !ui && !void_px && d < 1.0 && view_mode != 1)
        c = low_haze(c, world_at(texel(uv), d * 65536.0));
```

(`fogvol_apply` lines stay: the volumes are exempt. `world_at` is defined above `main`; if the compiler complains it is declared after `low_haze`, it is still before `main`.)

`pc/src/vk_present.c`, replace the `pu->quality0[0] = ...` statement with:

```c
            /* The room's medium every frame (whatever the haze path: scaled.frag reads its void switch
             * and the interior for haze_debug 2); haze Low (or High where nothing traced it this frame)
             * marches it per pixel, unshadowed but for the sun's map. */
            int medium_any = !pc_gl_show_depth
                && pc_medium_gpu_get(&pu->medium, pc_lighting_low_haze_density(0), pu->room_lo, pu->room_hi);
            pu->quality0[0] = !pu->haze_grid[3] && medium_any
                /* the uniform haze alone with nothing to light it would only darken: as before, none */
                && (present_light_count > 0 || pu->env_mode[2] || pc_medium_has_fog());
```

(`pc_medium_gpu_get` fills the switches even when it returns 0. Under show_depth the record is not read.)

- [ ] **Step 3: Implement (OpenGL, `gl_present.c`)**

In the present shader's `main` after `"    float d = depth_at(uv);\n"` add:

```c
    "    bool void_px = medium_void_pixel(d, texelFetch(shade_tex, texel(uv), 0));\n" /* the void: no haze (medium.glsl) */
```

Replace the `haze_debug` block (`"    if (quality1.z != 0) {\n"` .. its `"    }\n"`) with:

```c
    "    if (quality1.z != 0) {\n" /* haze_debug 1: the haze's light alone; 2: where the room's air is (scaled.frag the same) */
    "        bool ui_ = d * 65536.0 < 0.5; vec3 hz = vec3(0.0);\n"
    "        if (quality1.z == 2) {\n"
    "            float inside = !ui_ && d < 1.0 ? medium_interior(world_at(texel(uv), d * 65536.0)) : 0.0;\n"
    "            color = vec4(!ui_ && void_px ? 1.0 : 0.0, inside, 0.0, ui_ ? 9999.0 : 0.0);\n"
    "            return;\n"
    "        }\n"
    "        if (!ui_ && !void_px && (gi_on & 128) != 0 && fog_density > 0.0) {\n" /* traced (ptgi.c): its GI pixel */
    "            vec2 display = (vec2(texel(uv)) + 0.5) / float(scale) - vec2(rect.xy);\n"
    "            hz = max(texelFetch(gi_extra, clamp(ivec2(display / vec2(rect.zw) * vec2(gi_size)), ivec2(0), gi_size - 1), 0).gba, vec3(0.0));\n"
    "        } else if (!ui_ && !void_px && quality0.x > 0.0) hz = low_haze(vec3(0.0), world_at(texel(uv), min(d, 0.999) * 65536.0));\n"
    "        if (!ui_ && fogvol_on.x != 0) hz += fogvol_apply(vec3(0.0), texel(uv), d);\n"
    "        color = vec4(min(hz, vec3(1.0)), ui_ ? 9999.0 : 0.0);\n"
    "        return;\n"
    "    }\n"
```

In the lit branch, replace `"            if ((gi_on & 128) != 0 && fog_density > 0.0) {\n"` with

```c
    "            if (void_px) haze_done = true;\n" /* the void: clean darkness, as on the PS1 */
    "            else if ((gi_on & 128) != 0 && fog_density > 0.0) {\n" /* the traced haze (ptgi.c) between the eye and the surface */
```

and the unlit tail:

```c
    "    if (quality0.x > 0.0 && !haze_done && !ui && !void_px && d < 1.0 && view_mode != 1)\n" /* unlit 3D pixels */
```

(GL fills `medium_` every frame already: `gl_Uniform4fv(u_medium, ...)` runs whatever `medium_on` is. If `world_at` is defined after the haze_debug block's position in the string, it is still a function defined before `main`; check `gl_present.c` and move nothing else.)

- [ ] **Step 4: Build both; the smoke passes**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 void_fog_smoke
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan void_fog_smoke
```

Expected: `PASS` on both (every `ok:`). Look at `pc/build/tests/void_fog_smoke/void-room-on.png` against `void-room-off.png`: the void identical, the floor fogged. If a threshold misses by a hair, read the printed values: thresholds may move toward the measured value only when the frames show the void clean (never loosen "over the void" past 1.5 or "over the room" below 2.0); if the inside check fails, the interior is too tight (look at `void-classes.png`: green must cover the floor) and `haze_interior_fill` or the occupancy test (Task 4) is the place to look, not the threshold.

- [ ] **Step 5: Commit**

```bash
git add pc/shaders/vulkan/scaled.frag pc/src/vk_present.c pc/src/gl_present.c
git commit -m "Present: void pixels (nothing drawn, black shells) get no haze or ground fog; haze_debug 2 shows the void and the interior

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Ghost walls in this room

**Files:** none expected (`gameplay_walls.c` only if the check fails).

- [ ] **Step 1: Read the ghost check's result**

From Task 8's runs: "smart walls with ghosts draw cut walls again" and "what void is left stays clean" on both builds. Look at `void-ghost-vertex.png`, `void-ghost-on.png`.

- [ ] **Step 2: If it failed**

- No cut group drawn (`back` empty): print `room_groups` at the pose (`occluding`, `cells`, `shown` per cut group). If no cut group faces the camera with cells on screen (they all face away: nothing to reveal) and none is in Ashley's region, the room simply has nothing for smart walls to bring back at this pose: change the check to pass when every cut group reports `cells[3] == 0` (nothing on screen), print that, and note it in the Vulkan_Renderer.md paragraph (Task 10). If groups with cells on screen stay hidden, that is a `gameplay_walls.c` bug: debug it with superpowers:systematic-debugging, fix it in `pc/src/gameplay_walls.c` with a check added to `facing_cutaway_smoke.py`, and run `facing_cutaway_smoke` too.
- Ghost pixels fogged wrongly: ghost pixels carry normal alpha 0.55 and the wall's shade (not void): they take the fog like any wall. Nothing to change unless the frame shows otherwise.

- [ ] **Step 3: Commit (only if something changed)**

```bash
git add -A pc/src pc/tests
git commit -m "Smart walls: <what was fixed> in the Undercity (void_fog_smoke's ghost check)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Documentation

**Files:**
- Modify: `pc/docs/Vulkan_Renderer.md` ("Haze, ground fog and sun shafts", line 524+), `pc/docs/Native_Rendering.md` ("Seeing into rooms", the paragraph "None of this touches the ray-traced scene"), `lighting/README.md` (the haze limits' paragraph, line ~155), `pc/src/atmosphere.h` (the record comment above `PC_MEDIUM_VEC4S`, if not already done)

- [ ] **Step 1: Write**

`Vulkan_Renderer.md`:

- The haze bullet: "uniform inside the room's **interior** (the floor grid's cells with a floor; with `haze_interior` 0 its bounds, as before), fading out within `haze_under_floor` units under the lowest floor".
- The ground fog bullet: replace "empty cells grown from their neighbours" with "cells without a floor (no up-facing face up to 60 degrees that is not a black shell) stay empty: no fog over them (the void outside the walls); holes up to twice `haze_interior_fill` cells across count as floor (a closing, `floor_grid.c`); every cell still has a height (lowest of its 3 x 3, then grown outward) for `haze_interior` 0; packed two 12-bit heights per float, 4095 for an empty cell, bilinear over the cells with a floor in `medium_floor`".
- A new subsection after the limits, **The room's interior and the void (2026-10-16)**: the report (Undercity, 48/0, Ashley at about (972, 0, 1221): the cut-away walls revealed the void and the fog glowed over it); why (the grid grew empty cells, the haze filled the bounds box, the black shells took the haze like any wall); the three layers and their settings; the void test (`medium_void_pixel`: depth 1, or shade alpha 0.5/1 with vertex colour <= `haze_void_level`); fog volumes exempt (decision 2) and the volumes past the first 8 skipped on void pixels; `haze_debug 2` (red void, green interior); the record's new slots (6.w, 7.w, 90.w flags); the limitation that the interior's edge is the grid's cell (bounds / 24), which the void test covers where it shows; `ptgi.c`'s GL haze still marched on void GI pixels. Test: `pc/tests/void_fog_smoke.py` (what it checks; old code fails it) and the probe `floor_grid_probe`.

`Native_Rendering.md`: after "(and haze stays bounded by the room)" add: "The haze and ground fog also leave the void alone: present pixels that show a black shell or nothing get none (Vulkan_Renderer.md "The room's interior and the void"), and the shells count for nothing in the fog's floor grid."

`lighting/README.md`, after the limits' bullet:

```markdown
- Where the room's air is, also room settings (Details > Atmosphere):
  `haze_interior` (1: the haze and ground fog keep to the room's floor, not
  its bounds box; 0 as before), `haze_interior_fill` (1: holes up to two
  floor-grid cells across count as floor), `haze_under_floor` (32: how far
  under the lowest floor the haze reaches), `haze_void` (1: the void around
  the room, what the PS1 draws black, stays clean darkness; fog volumes are
  exempt), `haze_void_level` (0: the brightest vertex colour, of 255, that
  counts as that black). RPC `environment {haze: {interior, interior_fill,
  under_floor, void, void_level}}`.
```

- [ ] **Step 2: Check the committed text**

```bash
git diff --cached --stat; git grep -nE '/hom[e]/|/tm[p]/claude' -- pc/docs lighting pc/src pc/tests pc/shaders | head
```

Expected: no matches.

- [ ] **Step 3: Commit**

```bash
git add pc/docs/Vulkan_Renderer.md pc/docs/Native_Rendering.md lighting/README.md pc/src/atmosphere.h
git commit -m "Docs: the room's interior and the void (haze, ground fog, settings, record, test)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Full verification

**Files:** none (fixes go in their own commits).

- [ ] **Step 1: Every build**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --battle
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --arch 64
flock "$ROOD_HEAVY_LOCK" python3 pc/tools/build_boot.py --target windows
```

Expected: all exit 0, no `error:` (Windows: `floor_grid.c` compiles with MinGW; no identifier clashes).

- [ ] **Step 2: The suites, both builds**

```bash
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 ctest void_fog_smoke atmosphere_sweep atmosphere_smoke fog_volume_smoke fog_sim_smoke sky_smoke facing_cutaway
flock "$ROOD_HEAVY_LOCK" python3 pc/tests/run.py -j 2 --arch 64 --renderer vulkan void_fog_smoke atmosphere_sweep atmosphere_smoke fog_volume_smoke fog_sim_smoke sky_smoke
```

Expected: everything that passed in Task 1's baseline passes, plus `void_fog_smoke` and `floor_grid_probe`. If `atmosphere_smoke`'s absolute `base.mean() > 3` (the save room's emissive fog) now fails, look at `atmo-fog-floor.png`: if the light it lost was over the void (black shells around the save room), measure that check over the non-void pixels (the `view_mode 4` mask, as `void_fog_smoke`) in its own commit; never lower the threshold. If `atmosphere_sweep` loses detail or the Snowfly Forest (40/6) fog shrinks visibly, look at `haze_debug 2` there: terrain steeper than 60 degrees left empty is the suspect (raise that room's `haze_interior_fill` in its room file only if the hole is real terrain, or widen the occupancy slope in Task 4's test with a probe case first).

- [ ] **Step 3: The old picture is one switch away**

With `rood_rpc.py` (headless, a temp card copy, scratch `ROOD_LIGHTS`): in 48/0 at the test's pose, `environment {haze: {interior: false, void: false}}` must give the old frame (compare with Task 2's `void-room-on.png` from the old build: mean difference under 1). Record the numbers in the final message.

- [ ] **Step 4: Commit fixes, if any; report**

Each fix in its own commit with the attribution line. Final message: the suites' tables for both builds, the void and inside values `void_fog_smoke` printed (old code vs new), and the decisions above that the implementation changed, if any.

## After review (what changed from the decisions above)

- Decision 1: a void pixel no longer drops the whole ray's haze and fog. It keeps the room's air up to where its ray last lies in the interior (`medium_void_reach`, 16 samples), so a black doorway, pit or prop seen across a fogged floor is not cut out of the fog; the void past a cut-away wall keeps none. Without the interior the reach is 0.
- Decision 3: the edge softness is a room setting (`haze_interior_edge`), and so is the occupancy's slope limit (`haze_interior_slope`). The record grew one vec4 (165, the room's air): 166 vec4s.
- Decision 4: black shells are found per group (every captured triangle of the group black), and only while `haze_interior` is on; off, the grid is the old one exactly.
- Decision 5: past the grid's edge counts as floor while eroding only next to an edge cell with a floor (an empty strip along the edge stayed filled before).
- Decision 6 and the side effect: the uniform haze keeps `haze_outside` (default 1) of itself above the cells without a floor, so the air between an outside camera and the room stays (the rooms' tuned moods); under the floor it fades below the room's lowest floor there. With the interior on the heights pack in 11 bits plus a no-floor bit; off, 12 bits as before.
- Decision 7: with `haze_interior` and `haze_void` 0 the picture is the old one exactly.
- The grid is packed for the ground fog, the haze with the interior, and `haze_debug 2` (whose blue now shows the reach).
- The test also checks the user's view (the game camera, haze Low, the room's own fog made visible), each layer alone, the floor's interior, a dark in-room surface and a volume over the void; `atmosphere_sweep.py` records each pose's luma change from the interior.
