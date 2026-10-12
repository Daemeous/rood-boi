"""Ray-traced lighting reconstructs each pixel's world position from the
G-buffer's depth through the camera; that camera must be the one the picture
was drawn with. The game shows each frame a frame after building it, so the
GTE camera of the frame being built runs ahead of the picture while the
camera moves: surfaces (and so shadows) slid with every camera motion.

New Game, the opening's first room (zone 1 room 0) with a shadow-casting
test light, during its camera pan (vsyncs ~2420-2750): every GI update's
world positions (RPC gi_dump, with its camera) must lie on the captured room
geometry along the update's eye rays. Vulkan ray tracing only (exit 77,
skipped, on OpenGL)."""
import os
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game  # noqa: E402

if os.environ.get("ROOD_RENDERER") != "vulkan" or os.environ.get("ROOD_ARCH") != "64":
    print("SKIP: needs the Vulkan renderer (64-bit) for gi_dump")
    sys.exit(77)

lights = pathlib.Path(os.environ.get("ROOD_LIGHTS") or OUT / "rt-camera-lights")
lights.mkdir(parents=True, exist_ok=True)
# light <type> x y z  r g b  intensity radius  dx dy dz cone  shadows enabled
(lights / "zone01_room00.txt").write_text("light point 768 -600 836 1 0.75 0.45 2 1500 0 1 0 45 1 1\n")


def load_dump(path):
    with open(path, "rb") as f:
        head = f.readline().split()
        w, h = int(head[1]), int(head[2])
        data = np.frombuffer(f.read(), np.float32).reshape(h, w, 11)
    v = [float(x) for x in head[8:24]]
    camera = np.array(v[:9]).reshape(3, 3), np.array(v[9:12])
    return data, np.array([float(x) for x in head[3:6]]), int(head[6]), camera


def surface_error(points, eye, triangles):
    """Distance along each eye ray from the G-buffer point to the nearest
    room surface the ray crosses (inf: none)."""
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


game = Game.launch(extra_env={"ROOD_GI": "1", "ROOD_LIGHTS": str(lights)}, fast=True)
try:
    game.wait_title()
    game.press("start")
    game.run(2440 - game.status()["vsync"])
    state = game.state()
    assert (state["zone"], state["room"]) == (1, 0), state
    assert len(game.call("lights")["lights"]) == 1, "the test light is loaded with the room"
    obj = OUT / "rt-camera-room.obj"
    game.call("scene_dump", path=str(obj))
    vertices = np.array([[float(x) for x in line.split()[1:4]] for line in obj.read_text().splitlines()
                         if line.startswith("v ")])
    triangles = vertices.reshape(-1, 3, 3)
    results, eyes, rotations = [], [], []
    for i in range(16):
        game.run(1)
        path = OUT / "rt-camera-gi.bin"
        game.call("gi_dump", path=str(path))
        data, eye, replay, (rotation, translation) = load_dump(path)
        path.unlink()
        sample = data[::6, ::6].reshape(-1, 11)
        room = sample[np.abs(sample[:, 3] - 1) < 0.5, :3]  # room surfaces (not characters)
        error = surface_error(room, eye, triangles)
        error = error[np.isfinite(error)]
        off = float(np.mean(error > 8)) if error.size else 1.0
        results.append((i, replay, off, float(np.median(error)) if error.size else -1))
        eyes.append(eye)
        rotations.append(rotation)
        print(f"update {i:2d} {'replay' if replay else 'live  '} eye {np.round(eye)}: "
              f"{off * 100:5.1f}% of {error.size} room pixels off their surface by > 8 units", flush=True)
finally:
    game.close()
moved = float(np.max(np.linalg.norm(np.array(eyes) - eyes[0], axis=1)))
turned = float(np.max(np.abs(np.array(rotations) - rotations[0])))
assert moved > 10 and turned > 0.01, f"the camera did not move during the pan ({moved:.0f} units, {turned:.3f}): the test saw nothing"
worst = max(r[2] for r in results)
assert worst < 0.05, f"G-buffer positions off the room by > 8 units on {worst * 100:.1f}% of pixels while the camera moves"
log = pathlib.Path(game.log).read_text(errors="replace")
assert "[crash]" not in log
print(f"PASS: 16 GI updates during a camera pan ({moved:.0f} units, rotation {turned:.3f}): world positions on the room surfaces "
      f"(worst {worst * 100:.1f}% of pixels off by > 8 units)")
