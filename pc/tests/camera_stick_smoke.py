#!/usr/bin/env python3
"""The right-stick camera (pc/src/camera_stick.c) in the game.

    python3 pc/tests/camera_stick_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Right stick X turns the game's follow camera continuously, Y zooms, R3
resets. Analog values go in through the RPC (`input {right_stick: [x, y]}`).
Continues from a copy of the card (never the card itself; skipped without
one) into its save room (zone 9 room 17) and checks:

- the stick turns the game's own yaw (vs_battle_cameraCurrentSpherical)
  continuously (small steps, one direction, off the 45-degree grid) at a
  speed proportional to the deflection; the camera's derived yaw (scratch
  0x1F800058) follows and every frame's facing mask is the one that yaw
  gives (the cutaway walls the game itself picks);
- L1/R1 still turn to the 45-degree lines (from the stick's yaw: the nearest
  line), and from the grid exactly 0x200 as before;
- zoom out and in stay within the limits; the placed camera is at the
  reported distance; the game's own distance is unchanged;
- zoomed out at eight yaws (and with a larger zoom limit), the camera never
  ends up behind a drawn room surface beyond the game's own distance
  (checked here against the captured room, independently of the C);
- R3 turns back to the grid and zoom 1 without entering the first-person
  view; with R3 reset off, R3 is the game's (first-person view), where the
  stick looks around (camera_stick.c pc_camera_stick_first_person) and the
  follow camera does not tick;
- with Vulkan ray tracing (64-bit, ROOD_RENDERER=vulkan), the G-buffer's
  world positions lie on the room surfaces while the stick turns the camera
  (the RT camera follows the picture's: no sliding shadows);
- New Game's opening (a scripted camera): the stick held through it never
  takes the camera (no turn, zoom 1, the game's distance).

--out keeps evidence frames (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import golden_frames as gf  # noqa: E402

FACING = 0x1F800010  # scratch: facing mask (low byte), first-person view (bit 16)
POSITION = 0x1F800034  # scratch camera.position (VECTOR, 20.12)
LOOK_AT = 0x1F800044
YAW = 0x1F800058  # scratch camera.angles.vy
VULKAN = os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("ROOD_ARCH") == "64"


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def mask_for(yaw):
    """func_8008AC78 (scene_submit.inc): the camera's left, front and right bits."""
    return ((1 << (((0x9C0 - yaw) & 0xFFF) >> 9)) | (1 << (((0x840 - yaw) & 0xFFF) >> 9))
            | (1 << (((0x900 - yaw) & 0xFFF) >> 9)))


def wrap(d):
    return ((d + 2048) & 0xFFF) - 2048


def solid(flags, facing):
    """camera_stick.c solid(): drawn under the mask, not disabled, not the alternate set."""
    if flags & 0x9100:
        return False
    return bool(facing >> 16) or not ((flags ^ 255) & facing & 255)


def load_obj(path):
    vertices, faces = [], []
    for line in path.read_text().splitlines():
        if line.startswith("v "):
            vertices.append([float(x) for x in line.split()[1:4]])
        elif line.startswith("f "):
            faces.append(int(line.split("flags")[1].split()[0], 16))
    return np.array(vertices).reshape(-1, 3, 3), np.array(faces, dtype=np.int64)


def segment_hits(origin, direction, t0, t1, triangles):
    """Two-sided ray-triangle distances in (t0, t1] (Moller-Trumbore)."""
    a, b, c = triangles[:, 0], triangles[:, 1], triangles[:, 2]
    e1, e2 = b - a, c - a
    p = np.cross(direction[None], e2)
    det = np.einsum("tk,tk->t", e1, p)
    with np.errstate(divide="ignore", invalid="ignore"):
        inv = np.where(np.abs(det) < 1e-6, np.nan, 1.0 / det)
        tv = origin[None] - a
        u = np.einsum("tk,tk->t", tv, p) * inv
        q = np.cross(tv, e1)
        v = np.einsum("k,tk->t", direction, q) * inv
        t = np.einsum("tk,tk->t", e2, q) * inv
    ok = (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (t > t0) & (t <= t1)
    return t[ok]


def surface_error(points, eye, triangles):
    """rt_camera_smoke.py: distance along each eye ray from the G-buffer
    point to the nearest room surface the ray crosses (inf: none)."""
    a, b, c = triangles[:, 0], triangles[:, 1], triangles[:, 2]
    e1, e2 = b - a, c - a
    d = points - eye
    dist = np.linalg.norm(d, axis=1)
    d /= dist[:, None]
    out = np.full(len(points), np.inf)
    tv = eye - a
    q = np.cross(tv, e1)
    t_num = np.einsum("tk,tk->t", e2, q)
    for s in range(0, len(points), 512):
        ds = d[s:s + 512]
        pv = np.cross(ds[:, None, :], e2[None])
        det = np.einsum("tk,ntk->nt", e1, pv)
        with np.errstate(divide="ignore", invalid="ignore"):
            inv = np.where(np.abs(det) < 1e-9, np.nan, 1.0 / det)
            u = np.einsum("tk,ntk->nt", tv, pv) * inv
            v = np.einsum("nk,tk->nt", ds, q) * inv
            t = t_num[None] * inv
        t = np.where((u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1), t, np.inf)
        k = np.argmin(np.abs(t - dist[s:s + 512, None]), axis=1)
        out[s:s + 512] = np.abs(t[np.arange(len(k)), k] - dist[s:s + 512])
    return out


def load_gi_dump(path):
    with open(path, "rb") as f:
        head = f.readline().split()
        w, h = int(head[1]), int(head[2])
        data = np.frombuffer(f.read(), np.float32).reshape(h, w, 11)
    return data, np.array([float(x) for x in head[3:6]])


def opening_check(out):
    """New Game's opening pan (a scripted camera, vsyncs ~2300-2700) with the
    stick held through it: the stick never has the camera (no turn written,
    the placement untouched). (Two runs of the opening are not frame-exact,
    so this is checked within one run rather than against a run without.)"""
    env = {"ROOD_LIGHTS": str(pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))), "ROOD_GI": "0"}
    game = Game.launch(fast=True, log=out / "opening.log", extra_env=env)
    samples = []
    try:
        game.wait_title()
        game.press("start")
        game.run(2300 - game.status()["vsync"])
        start = game.call("camera_stick")
        for _ in range(5):
            game.call("input", right_stick=[1, 1], vsyncs=90)
            for _ in range(4):
                game.run(20)
                c = game.call("camera_stick")
                samples.append((c["active"], c["zoom_applied"], c["turns"], c["distance"], c["game_distance"]))
        log = pathlib.Path(game.log).read_text(errors="replace")
    finally:
        game.close()
    return start, samples, "[crash]" not in log


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep evidence frames here")
    parser.add_argument("--no-opening", action="store_true", help="skip the New Game opening comparison")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "camera_stick"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    if VULKAN:  # a shadow-casting light for the path tracer (zone 9 room 17)
        (lights / "zone09_room17.txt").write_text("light point 0 -400 0 1 0.8 0.5 2 1800 0 1 0 45 1 1\n")
    env = {"ROOD_WINDOW": gf.WINDOW, "ROOD_LIGHTS": str(lights), "ROOD_GI": "1" if VULKAN else "0",
           "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    failures = []
    sym = symbols()

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def u32(address):
        return struct.unpack("<I", game.read(address, 4))[0]

    def i32s(address, count):
        return struct.unpack(f"<{count}i", game.read(address, 4 * count))

    def spherical():
        """values (yaw, pitch, distance, mode), delta (yaw, pitch, distance, mode)."""
        v = i32s(sym["vs_battle_cameraCurrentSpherical"], 8)
        return v[:4], v[4:]

    def game_state():
        return u32(sym["D_800F196C"]), u32(sym["_cameraMode"])

    def scratch_yaw():
        return u32(YAW) & 0xFFF

    def arm():
        """Placed camera: position, look-at (world units), distance."""
        v = np.array(i32s(POSITION, 12), dtype=np.float64)
        position, look = v[0:3] / 4096, v[4:7] / 4096
        return position, look, float(np.linalg.norm(position - look))

    def cam():
        return game.call("camera_stick")

    def shot(name):
        if args.out:
            from PIL import Image
            Image.fromarray(gf.capture(game, out / f"{name}.png")).save(out / f"{name}.png")

    def settle_turn(limit=120):
        previous, steady = None, 0
        for _ in range(limit):
            game.run(2)
            y = spherical()[0][0]
            steady = steady + 1 if y == previous and spherical()[1][0] == 0 else 0
            previous = y
            if steady >= 6:
                return y
        return previous

    def stick_turn(x, vsyncs, sample=2, masks=None):
        """Hold the stick; the yaws every `sample` vsyncs; masks: (facing, yaws) per sample."""
        yaws = [spherical()[0][0]]
        recent = [scratch_yaw()] * 3
        game.call("input", right_stick=[x, 0], vsyncs=vsyncs)
        for _ in range(vsyncs // sample):
            game.run(sample)
            yaws.append(spherical()[0][0])
            recent = recent[-2:] + [scratch_yaw()]
            if masks is not None:
                masks.append((u32(FACING), list(recent)))
        game.run(12)  # released: the smoothing stops
        return yaws

    try:
        game.call("graphics", **{**gf.DEFAULTS, "scale": 2, "interpolate": True,
                                 "editor_hide_masks": True, "editor_game_cutaway": True, "editor_cutaway": True})
        s = game.state()
        if (s["zone"], s["room"]) != (9, 17) or not s.get("player"):
            print(f"FAIL: not in the save room after Continue: zone {s['zone']} room {s['room']}")
            return 1
        c = cam()
        state = game_state()
        check(c["enabled"] == 1 and state[0] == 1, f"the camera stick is on; the field camera (state {state})")
        values, delta = spherical()
        position, look, distance = arm()
        shot("game-camera")
        check(abs(distance - values[2]) < 0.02 * values[2] and c["zoom_applied"] == 1,
              f"untouched: the camera at the game's own distance {values[2]} ({distance:.0f})")

        # Continuous yaw, proportional to the deflection (linear response for the ratio).
        game.call("camera_stick", curve=1.0, deadzone=0.15)
        masks = []
        full = stick_turn(1.0, 60, masks=masks)
        steps = [wrap(b - a) for a, b in zip(full, full[1:])]
        total_full = sum(steps)
        check(all(-120 <= d <= 0 for d in steps) and total_full < -800,
              f"full right: the yaw turns continuously one way, {total_full} units in 60 vsyncs "
              f"(steps {min(steps)}..{max(steps)}; R1's direction)")
        check(spherical()[0][0] & 0x1FF != 0, f"off the 45-degree grid: yaw {spherical()[0][0]:#05x}")
        shot("turned-between-lines")
        bad = [(f, r) for f, r in masks if (f & 0xFF) not in [mask_for(y) for y in r] or f >> 16]
        check(not bad, f"every frame's facing mask is its yaw's ({len(masks)} frames; off: {bad[:3]})")
        derived = scratch_yaw()
        check(abs(wrap(derived - spherical()[0][0])) <= 8,
              f"the camera's derived yaw {derived:#05x} follows the game's yaw {spherical()[0][0]:#05x}")
        half = stick_turn(-0.575, 60)
        total_half = sum(wrap(b - a) for a, b in zip(half, half[1:]))
        ratio = -total_half / total_full
        check(0.4 <= ratio <= 0.6, f"half left: {total_half} units, {ratio:.2f} of full speed, the other way")
        game.call("camera_stick", curve=1.6)

        # L1/R1: from the stick's yaw R1 turns to the nearest line below; from the grid 0x200.
        y0 = spherical()[0][0]
        game.press("r1", vsyncs=3, settle=2)
        y1 = settle_turn()
        check(y1 == (y0 & ~0x1FF) & 0xFFF, f"R1 from the stick's yaw {y0:#05x}: {y1:#05x} (nearest line below)")
        game.press("r1", vsyncs=3, settle=2)
        y2 = settle_turn()
        check(y2 == (y1 - 0x200) & 0xFFF, f"R1 on the grid: {y1:#05x} -> {y2:#05x} (45 degrees, as before)")
        game.press("l1", vsyncs=3, settle=2)
        y3 = settle_turn()
        check(y3 == y1, f"L1 back: {y2:#05x} -> {y3:#05x}")

        # Zoom out and in.
        values = spherical()[0]
        game.call("input", right_stick=[0, 1], vsyncs=150)
        game.run(170)
        c = cam()
        position, look, distance = arm()
        check(c["distance"] > values[2] * 1.2 and c["distance"] <= values[2] * c["zoom_max"] + 1
              and abs(distance - c["distance"]) < 0.02 * c["distance"],
              f"zoomed out: {c['distance']} (game {values[2]}, max x{c['room_zoom_max']:.2f}), placed {distance:.0f}, "
              f"pitch {c['pitch']} (game {values[1]})")
        check(spherical()[0][2] == values[2], "the game's own distance is unchanged")
        shot("zoomed-out")
        game.call("input", right_stick=[0, -1], vsyncs=200)
        game.run(220)
        c = cam()
        position, look, distance = arm()
        check(abs(c["distance"] - values[2] * c["zoom_min"]) < 8 and c["pitch"] > values[1],
              f"zoomed in to the minimum: {c['distance']} (x{c['zoom_min']}), flatter pitch {c['pitch']}")
        shot("zoomed-in")

        # Zoomed out at eight yaws (a larger limit too): never behind a drawn surface.
        obj = out / "room.obj"
        game.call("scene_dump", path=str(obj))
        triangles, flags = load_obj(obj)
        engaged = violations = 0
        for zoom_max in (1.5, 3.0):
            game.call("camera_stick", zoom_max=zoom_max)
            game.call("input", right_stick=[0, 1], vsyncs=200)
            game.run(210)
            for k in range(8):
                game.call("input", right_stick=[0.6, 1], vsyncs=24)
                game.run(30)
                c = cam()
                position, look, distance = arm()
                facing = u32(FACING)
                keep = np.array([solid(int(f), facing) for f in flags])
                direction = (position - look) / distance
                hits = segment_hits(look, direction, c["game_distance"] + 1, distance, triangles[keep])
                engaged += c["hit"] > 0
                if len(hits):
                    violations += 1
                    print(f"  yaw {spherical()[0][0]:#05x} zoom x{zoom_max}: camera at {distance:.0f} behind a surface "
                          f"at {hits.min():.0f} (hit {c['hit']:.0f}, arm {c['arm']:.0f})")
        check(violations == 0, f"zoomed out at 16 poses: never behind a drawn room surface beyond the game's "
                               f"distance ({engaged} pulled in)")
        game.call("camera_stick", zoom_max=1.5)

        # R3: back to the grid and zoom 1, no first-person view.
        game.call("input", right_stick=[0.5, 0.5], vsyncs=20)
        game.run(30)
        game.press("r3", vsyncs=3, settle=4)
        y = settle_turn()
        game.run(30)
        c = cam()
        values = spherical()[0]
        position, look, distance = arm()
        check(y & 0x1FF == 0 and c["zoom_applied"] == 1 and abs(distance - values[2]) < 0.02 * values[2]
              and game_state()[0] == 1 and not u32(FACING) >> 16,
              f"R3: yaw {y:#05x} on the grid, zoom 1 ({distance:.0f}), still the field camera {game_state()}")

        # Idle reset (off by default): on, it turns back by itself.
        game.call("camera_stick", idle_reset=1.0)
        game.call("input", right_stick=[0.7, 0], vsyncs=20)
        game.run(24)
        moved = spherical()[0][0]
        game.run(120)
        y = settle_turn()
        check(moved & 0x1FF and y & 0x1FF == 0, f"idle reset after 1 s: {moved:#05x} -> {y:#05x}")
        game.call("camera_stick", idle_reset=0)

        # R3 reset off: R3 is the game's first-person view; the stick looks around there, the follow
        # camera does not tick (camera_options_smoke.py tests the directions).
        game.call("camera_stick", r3_reset=False)
        game.press("r3", vsyncs=3, settle=60)
        state = game_state()
        if state[0] != 7:
            check(False, f"R3 (reset off) enters the first-person view (state {state})")
        else:
            before = spherical()
            c0, looks = cam()["ticks"], cam()["fp_looks"]
            game.call("input", right_stick=[1, 1], vsyncs=60)
            game.run(60)
            after = spherical()
            check(after != before and cam()["ticks"] == c0 and cam()["fp_looks"] > looks,
                  f"first-person view (state {state}): the stick looks around, the follow camera does not tick")
            for _ in range(4):
                game.press("r3", vsyncs=3, settle=60)
                if game_state()[0] == 1:
                    break
            check(game_state()[0] == 1, f"back from the first-person view ({game_state()})")
        game.call("camera_stick", r3_reset=True)
        game.run(30)

        # The targeting sphere (battle mode): turns where the game turns with L1/R1.
        game.press("circle", vsyncs=3, settle=60)  # weapon drawn
        game.press("circle", vsyncs=3, settle=40)  # the targeting sphere
        state = game_state()
        if state[0] == 2:
            y0 = spherical()[0][0]
            game.call("input", right_stick=[1, 0], vsyncs=20)
            game.run(30)
            y1 = spherical()[0][0]
            check(wrap(y1 - y0) < -100, f"targeting sphere (state {state}): the stick turns ({y0:#05x} -> {y1:#05x})")
        else:
            print(f"note: no targeting sphere here (state {state})")
        for _ in range(3):
            game.press("cross", vsyncs=3, settle=40)
        game.run(60)

        if VULKAN:
            # The path tracer's world positions on the room surfaces while the stick turns.
            game.call("graphics", gi=True)
            game.run(30)
            worst, moved_eye, last_eye = 0.0, 0.0, None
            for i in range(10):
                game.call("input", right_stick=[1, 0.3], vsyncs=4)
                game.run(3)
                path = out / "gi.bin"
                game.call("gi_dump", path=str(path))
                data, eye = load_gi_dump(path)
                path.unlink()
                sample = data[::6, ::6].reshape(-1, 11)
                room = sample[np.abs(sample[:, 3] - 1) < 0.5, :3]
                error = surface_error(room, eye, triangles)
                error = error[np.isfinite(error)]
                off = float(np.mean(error > 8)) if error.size else 1.0
                worst = max(worst, off)
                if last_eye is not None:
                    moved_eye = max(moved_eye, float(np.linalg.norm(eye - last_eye)))
                last_eye = eye
            check(moved_eye > 5 and worst < 0.05,
                  f"RT while the stick turns: G-buffer on the room surfaces (worst {worst * 100:.1f}% of pixels "
                  f"off by > 8 units; eye moved up to {moved_eye:.0f} a dump)")

        log = pathlib.Path(game.log).read_text(errors="replace")
        check("[crash]" not in log, "no crash")
    finally:
        game.close()

    if not args.no_opening:
        start, samples, clean = opening_check(out)
        check(clean and all(a == 0 and z == 1 and t == start["turns"] and d == g for a, z, t, d, g in samples),
              f"New Game's opening (scripted camera): the stick held through it never takes the camera "
              f"({len(samples)} samples: inactive, zoom 1, no turn, the game's distance)")

    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: right-stick camera")
    return 0


if __name__ == "__main__":
    sys.exit(main())
