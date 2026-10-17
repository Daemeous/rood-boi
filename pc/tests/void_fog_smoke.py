#!/usr/bin/env python3
"""The void past cut-away walls stays black with the atmosphere on
(pc/docs/Vulkan_Renderer.md "Haze, ground fog and sun shafts", "The room's
interior and the void"). The user's report: the Undercity (zone 48 room 0),
Ashley at about (972, 0, 1221); the game cuts away the camera-side walls (as
on the PS1) and the void around the room showed glowing mist: the ground fog
followed a floor grid whose empty cells grew from their neighbours, the haze
filled the room's bounds box, and the black shells the void is made of took
the haze like any surface.

Warps there (a made-up door onto Ashley's cell) and stops the fog's drift.
The void: pixels the PS1 draws black whatever their texture (RPC graphics
{view_mode: 4}, the vertex colours: exactly black, eroded 3 pixels). On the
3D view (below the HUD's top band, above its bottom one), luminance 0-255:

The user's view first: the game's own camera there, haze Low (the user's
setting), the room's generated fog and haze:
- the void is as with the atmosphere off;
- the room's own fog made visible (its density, height, offset and scale,
  purely emissive) is black over the void, and the old code (the switches
  off) shows it there (the mist the user saw, 13 of 255 on Vulkan); each
  layer alone: the interior lowers it (the shells right by the floor lie
  within a grid cell of it), the void test takes all of it;
- the haze alone (no ground fog) keeps its mood: the whole view within 1.5
  of the old picture (haze_outside 1 keeps the air between the camera and
  the room).

Then the game camera turns through its eight yaws and keeps the one that
shows the most void. At that pose:

- with the room's generated fog and haze (A) against the atmosphere off (B):
  the void is as without it (mean |A - B| < 1, 99th percentile < 8) and dark;
- with a bright, purely emissive ground fog (it glows wherever it is): the
  haze's light alone (H, graphics {haze_debug: 1}) is black over the void
  (its pixels whose ray never crosses the room: most of them) and clearly
  there over the room's floor;
- the void test alone keeps the fog and haze off void pixels;
- the switches: environment {haze: {interior: false, void: false}} brings
  the old mist back over the void (the test sees what it is for);
- the classification view (haze_debug 2) marks the void pixels red, the
  room's floor green (interior), and a void pixel's reach (blue) is 0 past
  the cut-away walls;
- a dark surface inside the room classed as void (haze_void_level 32) keeps
  the fog in front of it (its ray's reach crosses the room): no cut-out;
- the floor grid: the cell under Ashley has a floor (floor_grid.occupied 1);
- gameplay_walls smart with ghosts: walls the mask cuts are drawn again (or
  as ghosts) and what void is left is still clean;
- a fog volume at Ashley still draws; one over the whole room and past it
  draws over the void too (the volumes are exempt);
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
ON = {"interior": True, "void": True}
OFF = {"interior": False, "void": False}


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
        room_fog = {k: e["fog"][k] for k in ("density", "height", "offset", "scale", "noise", "color", "fill")}
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

        # ---- the user's view: the game's camera there, haze Low, the room's own fog and haze ----
        game.call("graphics", haze="low")
        user_void = void_mask("user")
        a, _, b = atmosphere("user", density)
        uv = user_void & (b <= 12)
        d = a[uv] - b[uv]
        check(uv.mean() >= 0.02 and abs(float(d.mean())) < 1.0,
              f"the user's view: the void with the room's fog and haze is as without them (mean {d.mean():+.2f}, "
              f"{uv.mean() * 100:.1f} % of the view)")
        visible = dict(room_fog, color=[0, 0, 0], fill=[0.6, 0.6, 0.6], noise=0, sky=0)
        game.call("environment", fog=visible)
        _, shown, _ = atmosphere("user-visible", density)
        game.call("environment", haze=OFF)
        _, shown_old, _ = atmosphere("user-visible-old", density)
        game.call("environment", haze={"interior": True, "void": False})
        _, shown_interior, _ = atmosphere("user-visible-interior", density)
        game.call("environment", haze={"interior": False, "void": True})
        _, shown_void, _ = atmosphere("user-visible-void", density)
        game.call("environment", haze=ON)
        check(float(shown[uv].mean()) < 1.0 and float(shown_old[uv].mean()) > float(shown[uv].mean()) + 1.0,
              f"the user's view: the room's own fog, made visible, is black over the void "
              f"({shown[uv].mean():.2f}; the old code: {shown_old[uv].mean():.2f})")
        # each layer alone: the interior keeps the fog off the cells outside the walls, but a grid cell (the
        # room's bounds / 24) is coarser than a wall: the black shells right by the floor lie in the floor's own
        # cells or its soft edge, so it only lowers the light there; the void test takes all of it
        check(float(shown_interior[uv].mean()) < float(shown_old[uv].mean()) - 0.5,
              f"the user's view: the interior alone lowers it ({shown_interior[uv].mean():.2f})")
        check(float(shown_void[uv].mean()) < 1.0,
              f"the user's view: the void test alone keeps it off void pixels ({shown_void[uv].mean():.2f})")
        game.call("environment", fog=dict(room_fog, density=0))
        haze_new = frame("user-haze", 150) @ LUMA
        game.call("environment", haze=OFF)
        haze_old = frame("user-haze-old", 150) @ LUMA
        game.call("environment", haze=ON, fog=room_fog)
        game.run(30)
        check(abs(float(haze_new.mean() - haze_old.mean())) < 1.5,
              f"the user's view: the haze alone keeps its mood (mean luma {haze_old.mean():.2f} -> "
              f"{haze_new.mean():.2f})")
        game.call("graphics", lighting_preset="ultra")
        game.run(30)

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
        game.call("graphics", haze_debug=2)
        classes = frame("classes", 20)
        game.call("graphics", haze_debug=0)
        far = v & (classes[..., 2] < 32)  # the void pixels whose ray never crosses the room (reach 0)
        check(float(far.sum()) > 0.5 * float(v.sum()),
              f"most of the void's pixels keep none of the room's air ({far.sum() / max(v.sum(), 1) * 100:.0f} % reach 0)")
        _, glow, _ = atmosphere("glow", density)
        inside = erode(~void, 3) & (b > 8)
        check(float(glow[far].mean()) < 1.0,
              f"a glowing fog's light over the void: {glow[far].mean():.2f} (black; {glow[v].mean():.2f} with the "
              f"void pixels whose ray crosses the room first)")
        check(inside.mean() > 0.2 and float(glow[inside].mean()) > 3.0,
              f"it still shows over the room ({glow[inside].mean():.1f} on {inside.mean() * 100:.0f} % of the view)")
        game.call("environment", haze={"interior": False, "void": True})
        _, void_only, _ = atmosphere("glow-void", density)
        check(float(void_only[v].mean()) < 1.0,
              f"the void test alone keeps the fog and haze off void pixels ({void_only[v].mean():.2f})")
        game.call("environment", haze=OFF)
        _, old, _ = atmosphere("glow-old", density)
        check(float(old[v].mean()) > float(glow[far].mean()) + 1.0 and float(old[v].mean()) > float(glow[v].mean()),
              f"interior and void off: the old mist over the void comes back ({old[v].mean():.1f})")
        game.call("environment", haze=ON)
        # the room's own fog and haze (A, B above) still lie over its floor: they dim it (their light is faint)
        check(float((b - a)[inside].mean()) > 1.0,
              f"the room's own fog and haze still lie over its floor (it dims by {(b - a)[inside].mean():.1f})")
        check(float((classes[..., 0][v] > 128).mean()) > 0.8,
              f"haze_debug 2 marks the void ({(classes[..., 0][v] > 128).mean() * 100:.0f} % of it red)")
        check(float((classes[..., 1][inside] > 128).mean()) > 0.8,
              f"haze_debug 2 marks the room's floor as its interior ({(classes[..., 1][inside] > 128).mean() * 100:.0f} %"
              f" of it green)")

        # ---- a dark surface inside the room: no cut-out of the fog in front of it ----
        game.call("environment", haze={"void_level": 32})
        game.call("graphics", haze_debug=2)
        dark = frame("classes-dark", 20)
        game.call("graphics", haze_debug=0)
        _, dark_on, _ = atmosphere("glow-dark", density)
        game.call("environment", haze={"void": False})
        _, dark_off, _ = atmosphere("glow-dark-off", density)
        game.call("environment", haze={"void": True, "void_level": 0})
        newly = erode((dark[..., 0] > 128) & (classes[..., 0] <= 128) & (dark[..., 1] > 128), 2) & (dark_off > 3)
        if newly.sum() >= 200:
            ratio = float(dark_on[newly].mean()) / max(float(dark_off[newly].mean()), 1e-3)
            check(ratio > 0.8, f"a dark surface inside the room classed as void keeps the fog in front of it "
                               f"({dark_on[newly].mean():.1f} vs {dark_off[newly].mean():.1f} without the void test, "
                               f"{newly.mean() * 100:.1f} % of the view)")
        else:
            print(f"(no dark surface inside the room at this pose: {int(newly.sum())} pixels)")

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
        if grid.get("corner"):
            n, (cx, cz), (sx, sz) = grid["n"], grid["corner"], grid["cell"]
            big = dict(vol, id="test0000000002", pos=[cx + n * sx / 2, player["y"] - 200, cz + n * sz / 2],
                       size=[n * sx + 1024, 2400, n * sz + 1024], density=0.002)
            game.call("environment", volumes=[big])
            game.call("graphics", haze_debug=1)
            over = frame("volume-void", 150) @ LUMA
            game.call("graphics", haze_debug=0)
            check(float(over[v].mean()) > 1.0,
                  f"a fog volume over the whole room and past it draws over the void ({over[v].mean():.1f})")
            game.call("environment", volumes=[{"id": "test0000000002", "remove": True}])
    finally:
        game.close()
    log = (out / "game.log").read_text(errors="replace")
    bad = [line for line in log.splitlines() if "validation:" in line or "[crash]" in line or "[gl] shader" in line]
    check(not bad, f"no crash, shader error or validation message {bad[:3]}")
    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
