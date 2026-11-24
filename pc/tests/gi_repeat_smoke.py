"""A paused game redrawn again and again keeps the same path-traced picture.

A paused game redraws at 60 Hz and after every served request: as many times as the
machine's speed allows. The GI target (raw and displayed indirect light, the NRD history
length) of one game frame must not depend on that number (the trace's frame index, the
temporal and NRD histories and the lights' animation follow the game's frames, not the
presents or the wall clock). Same setup as nrd_smoke (New Game, the first room, a test
light, after the opening pan): read the GI images, wait while the game redraws many times,
read them again: they are equal bit for bit, and so after a served request that only
re-presents. Skipped (exit 77) on OpenGL, 32 bits, builds without NRD, devices without
ray tracing."""

import os
import pathlib
import sys
import time

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game, RpcError  # noqa: E402

if os.environ.get("ROOD_RENDERER") != "vulkan" or os.environ.get("ROOD_ARCH") != "64":
    print("SKIP: needs the Vulkan renderer (64-bit)")
    sys.exit(77)

lights = pathlib.Path(os.environ.get("ROOD_LIGHTS") or OUT / "nrd-lights")
lights.mkdir(parents=True, exist_ok=True)
# light <type> x y z  r g b  intensity radius  dx dy dz cone  shadows enabled
(lights / "zone01_room00.txt").write_text("light point 768 -600 836 1 0.75 0.45 2 1500 0 1 0 45 1 1\n")


def dump(game, name):
    path = OUT / f"nrd-{name}.bin"
    game.call("gi_dump", path=str(path), light=True)
    with open(path, "rb") as f:
        head = f.readline().split()
        w, h = int(head[1]), int(head[2])
        data = np.frombuffer(f.read(), np.float32).reshape(h, w, 9)
    path.unlink()
    return data, {"w": w, "h": h, "denoiser": int(head[3]), "nrd": int(head[5]), "since_reset": int(head[6])}


failures = []


def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)


game = Game.launch(extra_env={"ROOD_GI": "1", "ROOD_LIGHTS": str(lights)}, fast=True)
try:
    game.wait_title()
    graphics = game.call("graphics")
    if not graphics.get("nrd_built"):
        print("SKIP: built without NRD (build_boot.py --arch 64 --nrd off, or NRD could not be fetched)")
        game.close()
        sys.exit(77)
    game.call("graphics", gi=True, rt_shadows=True, gi_resolution=2)
    game.press("start")
    game.run(3300 - game.status()["vsync"])  # after the opening pan
    state = game.state()
    assert (state["zone"], state["room"]) == (1, 0), state
    # A second light in front of this view (the first lights the pan).
    camera = game.call("scene")["camera"]
    rotation, translation = np.array(camera["rotation"]), np.array(camera["translation"])
    eye = -np.linalg.solve(rotation, translation)
    forward = rotation[2] / np.linalg.norm(rotation[2])
    x, y, z = (eye + forward * 700).round().astype(int)
    pan_light = "light point 768 -600 836 1 0.75 0.45 2 1500 0 1 0 45 1 1\n"
    view_light = f"light point {x} {y} {z} 1 0.85 0.6 2 3000 0 1 0 45 1 1\n"

    try:
        game.run(150)
        data0, head0 = dump(game, "a")
    except RpcError as e:
        print(f"SKIP: no ray-traced GI here ({e})")
        game.close()
        sys.exit(77)
    time.sleep(2.0)  # about a hundred redraws of the same game frame
    data1, head1 = dump(game, "b")
    game.call("status")
    time.sleep(0.5)
    data2, head2 = dump(game, "c")
    check(head0["since_reset"] == head1["since_reset"] == head2["since_reset"],
          f"NRD's history length does not advance on redraws ({head0['since_reset']}, {head1['since_reset']}, {head2['since_reset']})")
    check(np.array_equal(data0, data1) and np.array_equal(data0, data2),
          "the GI target is bit-identical after many redraws of the same game frame")
    game.run(1)
    data3, head3 = dump(game, "d")
    check(head3["since_reset"] > head0["since_reset"] or not np.array_equal(data0, data3),
          "a new game frame does advance it")
finally:
    game.close()
log = pathlib.Path(game.log).read_text(errors="replace")
check("[crash]" not in log, "no crash")
check("[vk] validation:" not in log, "no Vulkan validation messages")
if failures:
    print(f"FAIL: {len(failures)} checks")
    sys.exit(1)
print("PASS: redrawing a paused game leaves its path-traced picture unchanged")
