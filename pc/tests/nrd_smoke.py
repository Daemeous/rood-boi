"""NVIDIA NRD (RELAX diffuse) denoises the path-traced indirect light on
Vulkan (gi_denoiser nrd, the default when built with NRD;
pc/docs/Vulkan_Renderer.md, "NRD").

New Game, the first room (zone 1 room 0) with a shadow-casting test light,
after the opening pan (a still camera). RPC gi_dump {light: true} reads the
GI images back (raw and displayed indirect light, NRD's history length).
Checks:

- the default denoiser is NRD and it runs (history accumulates);
- it keeps the light's energy (mean displayed vs raw indirect light) and
  outputs finite, non-negative light;
- it is stable: consecutive updates differ far less than the raw samples;
- switching to the filter and back, and a GI resolution change (NRD's
  images are re-created), work, and history restarts and recovers;
- no Vulkan validation messages, no crash.

Skipped (exit 77) on OpenGL, 32 bits, builds without NRD and devices without
ray tracing."""
import os
import pathlib
import sys

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
    (lights / "zone01_room00.txt").write_text(
        "light point 768 -600 836 1 0.75 0.45 2 1500 0 1 0 45 1 1\n"
        f"light point {x} {y} {z} 1 0.85 0.6 2 3000 0 1 0 45 1 1\n")
    game.run(30)
    try:
        data, head = dump(game, "first")
    except RpcError as e:
        print(f"SKIP: no ray-traced GI here ({e})")
        game.close()
        sys.exit(77)
    for _ in range(30):  # the new light reset NRD's history: let it accumulate
        if head["since_reset"] >= 40:
            break
        game.run(10)
        data, head = dump(game, "first")
    graphics = game.call("graphics")
    check(graphics["gi_denoiser"] == "nrd" and graphics["gi_denoiser_active"] == "nrd",
          f"NRD is the default denoiser ({graphics['gi_denoiser']}, active {graphics['gi_denoiser_active']})")
    check(head["denoiser"] == 2 and head["nrd"] == 1, f"NRD ran for the last update ({head})")

    def stats(data):
        surface = data[..., 0] > 0.5
        raw, shown, history = data[..., 1:4][surface], data[..., 4:7][surface], data[..., 7][surface]
        return surface, raw, shown, history

    surface, raw, shown, history = stats(data)
    check(surface.mean() > 0.3, f"GI surfaces cover the view ({surface.mean() * 100:.0f}%)")
    check(np.isfinite(shown).all() and (shown >= 0).all(), "denoised light is finite and non-negative")
    check(np.median(history) >= 10, f"history accumulates (median {np.median(history):.0f} frames)")
    # Energy: the mean over many updates of raw samples vs the displayed light.
    raw_means, shown_means, raw_steps, shown_steps = [], [], [], []
    previous = None
    for i in range(12):
        game.run(1)
        data, _ = dump(game, f"still{i}")
        surface, raw, shown, _ = stats(data)
        raw_means.append(raw.mean())
        shown_means.append(shown.mean())
        if previous is not None and previous.shape == data.shape:
            both = surface & (previous[..., 0] > 0.5)
            raw_steps.append(np.abs(data[..., 1:4][both] - previous[..., 1:4][both]).mean())
            shown_steps.append(np.abs(data[..., 4:7][both] - previous[..., 4:7][both]).mean())
        previous = data
    raw_mean, shown_mean = float(np.mean(raw_means)), float(np.mean(shown_means))
    check(raw_mean > 1e-4, f"there is indirect light ({raw_mean:.4f})")
    check(abs(shown_mean - raw_mean) <= 0.15 * raw_mean,
          f"energy kept: displayed {shown_mean:.4f} vs raw {raw_mean:.4f}")
    ratio = float(np.mean(shown_steps) / max(np.mean(raw_steps), 1e-9))
    check(ratio < 0.3, f"stable: consecutive updates differ {ratio * 100:.0f}% as much as the raw samples")

    # The filter, then NRD again: its history restarts and recovers.
    game.call("graphics", gi_denoiser="filter")
    game.run(4)
    data, head = dump(game, "filter")
    check(head["denoiser"] == 1 and head["nrd"] == 0, f"filter: NRD off ({head})")
    game.call("graphics", gi_denoiser="nrd")
    game.run(1)  # a paused game re-presents (a few updates)
    data, back = dump(game, "back")
    for _ in range(30):
        game.run(10)
        data, head = dump(game, "recovered")
        if head["since_reset"] >= 40:
            break
    check(back["nrd"] == 1 and back["since_reset"] < head["since_reset"] and np.median(stats(data)[3]) >= 10,
          f"back to NRD: history restarted ({back['since_reset']} updates since the reset, then "
          f"{head['since_reset']}) and recovers (median {np.median(stats(data)[3]):.0f})")

    # A GI resolution change: new images, NRD re-created at the new size.
    size = (head["w"], head["h"])
    game.call("graphics", gi_resolution=1.5)
    for _ in range(30):
        game.run(10)
        data, head = dump(game, "resized")
        if head["since_reset"] >= 40:
            break
    surface, _, shown, history = stats(data)
    check((head["w"], head["h"]) != size and head["nrd"] == 1,
          f"resized {size} -> {(head['w'], head['h'])}, NRD running")
    check(np.isfinite(shown).all() and np.median(history) >= 10,
          f"after the resize: finite light, history {np.median(history):.0f}")
    raw_check = game.call("graphics", gi_denoiser="raw")
    check(raw_check["gi_denoiser_active"] == "raw", "raw samples selectable")
    game.run(2)
finally:
    game.close()
log = pathlib.Path(game.log).read_text(errors="replace")
check("[nrd] NRD" in log and "RELAX diffuse ready" in log, "NRD reported ready in the log")
check("[crash]" not in log, "no crash")
check("[vk] validation:" not in log, "no Vulkan validation messages")
if failures:
    print(f"FAIL: {len(failures)} checks")
    sys.exit(1)
print("PASS: NRD RELAX denoises the indirect light (default on Vulkan), stable, energy kept, resize and switching work")
