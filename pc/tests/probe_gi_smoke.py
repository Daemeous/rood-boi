"""Probe GI (gi_method probes; pc/docs/Probe_GI.md): New Game, the first
room after its opening pan with a test light in view, on whichever renderer
run.py selects (OpenGL 4.3 compute, Vulkan ray queries).

Checks:
- the presets offer probes here and gi_method probes runs them (the backend
  the renderer should use), the grid fits the room, some probes are active
  and some inside or far from geometry are not;
- the field converges (its mean irradiance settles) and is stable frame to
  frame;
- its indirect light is close to the path tracer's: on Vulkan per GI pixel on
  the room's surfaces (gi_dump light: raw path-traced samples averaged over
  many updates vs the probe field written into the traced result), on
  OpenGL over the indirect-light view;
- no light through a wall (Vulkan): a light in the corridor beside the hall,
  seen from the hall with the free camera, probes 256 units apart (cells
  straddle the wall): where the path tracer finds no indirect light, the
  probes add little;
- a light change counts as a light reset, a room change refits the grid;
- no crash, no Vulkan validation messages.
Skipped (77) where neither OpenGL compute nor Vulkan is available."""
import os
import pathlib
import sys
import time

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from rood_rpc import OUT, Game, RpcError  # noqa: E402

VULKAN = os.environ.get("ROOD_RENDERER") == "vulkan"
lights = pathlib.Path(os.environ.get("ROOD_LIGHTS") or OUT / "probe-lights")
lights.mkdir(parents=True, exist_ok=True)
room_file = lights / "zone01_room00.txt"
room_file.unlink(missing_ok=True)
failures = []



def wait_light_reset(game, resets, seconds=8.0):
    """The lights file is polled twice a second (wall time; fast runs outpace it)."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if game.call("probe_gi")["light_resets"] > resets:
            return True
        game.run(15)
        time.sleep(0.25)
    return False

def check(ok, message):
    print(("ok:   " if ok else "FAIL: ") + message, flush=True)
    if not ok:
        failures.append(message)


def lum(x):
    return x @ np.array([0.299, 0.587, 0.114], np.float32)


def light_dump(game):
    """gi_dump light: per GI pixel kind, raw traced rgb, displayed rgb."""
    path = OUT / "probe-light.bin"
    game.call("gi_dump", path=str(path), light=True)
    with open(path, "rb") as f:
        head = f.readline().split()
        w, h = int(head[1]), int(head[2])
        data = np.frombuffer(f.read(), np.float32).reshape(h, w, 9).copy()
    path.unlink()
    return data


def field_mean(game):
    """Mean irradiance of the probe atlas (probe_gi dump)."""
    path = OUT / "probe-field.bin"
    game.call("probe_gi", dump=str(path))
    with open(path, "rb") as f:
        head = f.readline().split()
        columns, rows, iw, ih = int(head[6]), int(head[7]), int(head[17]), int(head[18])
        data = np.frombuffer(f.read(columns * rows * 16), np.float32).reshape(-1, 4)
        irradiance = np.frombuffer(f.read(iw * ih * 16), np.float32).reshape(ih, iw, 4)
    path.unlink()
    return float(lum(irradiance[..., :3]).mean()), int((data[:, 3] > 0).sum())


def wait_converged(game, updates=60, limit=40):
    for _ in range(limit):
        state = game.call("probe_gi")
        if state["valid"] and state["converged"] >= updates:
            return state
        game.run(10)
    return game.call("probe_gi")


def screenshot_mean(game, name):
    path = game.present_screenshot(OUT / f"probe-{name}.png")
    from PIL import Image
    image = np.asarray(Image.open(path).convert("RGB"), np.float32)
    return float(image[int(image.shape[0] * 0.4):].mean())  # below the dialog box


game = Game.launch(extra_env={"ROOD_GI": "1", "ROOD_LIGHTS": str(lights)}, fast=True)
try:
    game.wait_title()
    graphics = game.call("graphics")
    caps = graphics.get("lighting_caps", {})
    if not caps.get("probes"):
        print(f"SKIP: no probe GI on this renderer ({caps})")
        game.close()
        sys.exit(77)
    game.press("start")
    game.run(3300 - game.status()["vsync"])  # after the opening pan
    state = game.state()
    assert (state["zone"], state["room"]) == (1, 0), state
    camera = game.call("scene")["camera"]
    rotation, translation = np.array(camera["rotation"]), np.array(camera["translation"])
    eye = -np.linalg.solve(rotation, translation)
    forward = rotation[2] / np.linalg.norm(rotation[2])
    x, y, z = (eye + forward * 700).round().astype(int)
    room_file.write_text(f"light point {x} {y} {z} 1 0.85 0.6 2 3000 0 1 0 45 1 1\n")
    game.run(150)  # the lights file is polled

    # Probes: backend, grid, convergence, stability.
    game.call("graphics", gi_method="probes", probe_spacing=96, probe_rays=64)
    game.run(4)
    traced = VULKAN and caps.get("ray_query")
    want_backend = (1 if traced else 2) if VULKAN else 3
    first = game.call("probe_gi")
    check(first["available"] and first["backend"] == want_backend,
          f"probe GI runs on the expected backend ({first['backend']}, want {want_backend})")
    state = wait_converged(game)
    check(state["valid"] and state["converged"] >= 60, f"the field converges ({state['converged']} updates)")
    counts = state["counts"]
    check(min(counts) >= 2 and state["probes"] == counts[0] * counts[1] * counts[2] and 4096 >= state["probes"] > 500,
          f"grid fits the room within the budget ({counts}, {state['probes']} probes)")
    check(0 < state["active"] < state["probes"],
          f"some probes active, inside/empty ones not ({state['active']} of {state['probes']})")
    mean_a, active_a = field_mean(game)
    game.run(40)
    mean_b, active_b = field_mean(game)
    check(mean_a > 1e-3 and abs(mean_b - mean_a) <= 0.03 * mean_a,
          f"converged: field mean {mean_a:.4f} -> {mean_b:.4f} over 40 vsyncs")
    check(active_a == active_b, f"classification is stable ({active_a}, {active_b})")

    if traced:
        # Energy against the path tracer, per GI pixel on room surfaces.
        game.call("graphics", gi_method="path")
        game.run(30)
        acc = None
        for _ in range(24):
            game.run(1)
            data = light_dump(game)
            acc = data[..., 1:4] if acc is None else acc + data[..., 1:4]
        path_kind, path_light = data[..., 0], acc / 24
        game.call("graphics", gi_method="probes")
        wait_converged(game, 40)
        data = light_dump(game)
        room = (path_kind > 0.5) & (path_kind < 1.5) & (data[..., 0] > 0.5) & (data[..., 0] < 1.5)
        e_path, e_probes = float(lum(path_light[room]).mean()), float(lum(data[..., 4:7][room]).mean())
        check(room.mean() > 0.3 and e_path > 1e-3, f"room surfaces in view ({room.mean() * 100:.0f}%), lit ({e_path:.4f})")
        check(0.7 <= e_probes / e_path <= 1.5,
              f"energy: probes {e_probes:.4f} vs path traced {e_path:.4f} (ratio {e_probes / e_path:.2f})")
        steps, previous = [], data[..., 4:7]
        for _ in range(6):
            game.run(1)
            current = light_dump(game)[..., 4:7]
            steps.append(float(np.abs(lum(current[room]) - lum(previous[room])).mean() / e_probes))
            previous = current
        check(np.mean(steps) < 0.03, f"stable: consecutive frames differ {np.mean(steps) * 100:.2f}% of the light")
    else:
        # OpenGL / no ray queries: the indirect-light view against the path tracer's (when it has one).
        game.call("graphics", gi_debug=1)
        game.run(2)
        probes_view = screenshot_mean(game, "indirect")
        game.call("graphics", gi_method="path")
        game.run(60)
        path_view = screenshot_mean(game, "indirect-path")
        game.call("graphics", gi_method="probes", gi_debug=0)
        if caps.get("traced"):
            check(0.6 <= probes_view / max(path_view, 1e-6) <= 1.7,
                  f"indirect view: probes {probes_view:.1f} vs path traced {path_view:.1f}")
        else:
            check(probes_view > 5, f"indirect view lit by probes ({probes_view:.1f})")
        wait_converged(game, 20)

    # A light change: counted, the field follows.
    resets = game.call("probe_gi")["light_resets"]
    if traced:
        # No light through the wall between the hall and the corridor: the
        # light in the corridor, the free camera in the hall looking at the
        # doorway, cells straddling the wall.
        room_file.write_text("light point 1420 -900 900 1 0.8 0.5 4 2000 0 1 0 45 1 1\n")
        # Let the probes see the new light before the path-traced comparison
        # switches them off (the file poll is wall-time; a fast run could
        # otherwise reload it while the probes are inactive).
        wait_light_reset(game, resets)
        game.call("editor_camera", x=450, y=-900, z=700, yaw=90, pitch=0)
        game.run(150)
        game.call("graphics", gi_method="path", probe_spacing=256)
        game.run(30)
        acc = None
        for _ in range(24):
            game.run(1)
            data = light_dump(game)
            acc = data[..., 1:4] if acc is None else acc + data[..., 1:4]
        kind, path_light = data[..., 0], lum(acc / 24)
        game.call("graphics", gi_method="probes")
        wait_converged(game, 60)
        probes_light = lum(light_dump(game)[..., 4:7])
        room = (kind > 0.5) & (kind < 1.5)
        dark = room & (path_light < 0.002)
        lit_mean = float(probes_light[room].mean())
        leak = float(probes_light[dark].mean()) / max(lit_mean, 1e-6) if dark.any() else 0.0
        check(dark.mean() > 0.2 and lit_mean > 1e-3,
              f"leak view: {dark.mean() * 100:.0f}% of the pixels unlit by the path tracer, probes {lit_mean:.4f}")
        check(leak < 0.2, f"no leak through the wall: probes give unlit pixels {leak * 100:.1f}% of the mean light")
        game.call("editor_camera", off=1)
        game.call("graphics", probe_spacing=96)
    else:
        room_file.write_text(f"light point {x} {y} {z} 0.6 0.8 1 2 3000 0 1 0 45 1 1\n")
        game.run(150)
        wait_light_reset(game, resets)
    check(game.call("probe_gi")["light_resets"] > resets, "a light change restarts the convergence")

    # A room change refits the grid.
    before = game.call("probe_gi")
    room_before = (game.state()["zone"], game.state()["room"])
    changed = None
    for _ in range(120):  # the story goes on as the dialogue is answered
        game.press("circle", 2, 100)
        s = game.state()
        if (s["zone"], s["room"]) != room_before and game.call("scene")["triangles"]:
            changed = (s["zone"], s["room"])
            break
    if changed is None:
        check(False, "the opening reached another room")
    else:
        game.run(60)
        after = game.call("probe_gi")
        check(after["rebuilds"] > before["rebuilds"] and (after["counts"] != before["counts"]
              or after["origin"] != before["origin"]),
              f"room {room_before} -> {changed}: grid refitted ({before['counts']} -> {after['counts']}, "
              f"{after['rebuilds']} fits)")
except RpcError as e:
    check(False, f"RPC: {e}")
finally:
    game.close()
log = pathlib.Path(game.log).read_text(errors="replace")
check("[gi] probe GI ready" in log, "probe GI reported ready")
check("[crash]" not in log, "no crash")
check("[vk] validation:" not in log, "no Vulkan validation messages")
check("[gl] compute" not in log and "[gl] shader" not in log, "no OpenGL shader errors")
if failures:
    print(f"FAIL: {len(failures)} checks")
    sys.exit(1)
print("PASS: probe GI converges, matches the path tracer's energy, is stable, does not leak, follows lights and rooms")
