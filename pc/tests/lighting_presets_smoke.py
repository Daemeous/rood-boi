#!/usr/bin/env python3
"""Lighting quality presets (pc/src/lighting_quality.cpp; README "Lighting
quality"): Original / Low / Medium / High / Ultra / Custom.

    python3 pc/tests/lighting_presets_smoke.py [--card pc/save/card1.mcd] [--shots DIR]

Runs on the backend the environment selects (run.py: 32-bit GL, 64-bit GL,
or ROOD_RENDERER=vulkan) and on its reduced form in a second launch (Vulkan
with ROOD_VK_RT=0, as a GPU without ray queries; OpenGL with
ROOD_NO_COMPUTE=1, as a context without GL 4.3 compute). Each launch:

- first run (ROOD_SETTINGS names a missing file): the preset is the one the
  auto-detection picks for this backend and GPU class; the rule itself
  checked for every case (RPC lighting_autodetect);
- the messages for what cannot run name this backend's reason (no OpenGL
  advice on Vulkan, no ray-query advice on OpenGL);
- in the save room (a copy of the card, a test light by Ashley), each preset
  applies its keys (the table below, with this backend's fallbacks), renders
  without shader errors, validation messages or crashes, and changes the
  picture (Original has no dynamic light; Ultra differs from it); GPU time
  per preset with ROOD_PERF=1 (RPC perf);
- changing a key makes the preset Custom, choosing a preset sets its keys
  again;
- persistence: settings saved (RPC settings) carry the preset and keys, and
  load back (a preset re-applies, Custom keeps its values).

--shots keeps a screenshot per backend and preset (and the editor's
Lighting quality UI) for looking: game imagery, keep it out of git."""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

PRESETS = ["original", "low", "medium", "high", "ultra"]
# The presets as lighting_quality.cpp defines them: value choices in order of
# preference (the first this backend can run).
TABLE = {
    "original": dict(dynamic_lights=0, gi_method="off", shadows="off", shadow_map_filter="pcf", glow_shadows=0,
                     characters_shadow_baked=0, haze="off", ao=0, gi_resolution=1, shadow_rays=1, gi_denoiser="filter",
                     probe_spacing=128, probe_rays=32),
    "low": dict(dynamic_lights=1, gi_method="off", shadows="maps", shadow_map_filter="pcf", glow_shadows=0,
                characters_shadow_baked=0, haze="off", ao=1, gi_resolution=1, shadow_rays=1, gi_denoiser="filter",
                probe_spacing=128, probe_rays=32),
    "medium": dict(dynamic_lights=1, gi_method="probes|off", shadows="maps", shadow_map_filter="pcss", glow_shadows=0,
                   characters_shadow_baked=0, haze="low", ao=1, gi_resolution=1.5, shadow_rays=1,
                   gi_denoiser="filter", probe_spacing=128, probe_rays=32),
    "high": dict(dynamic_lights=1, gi_method="probes|off", shadows="traced|maps", shadow_map_filter="pcss",
                 glow_shadows=1, characters_shadow_baked=1, haze="high|low", ao=1, gi_resolution=1.5, shadow_rays=1,
                 gi_denoiser="filter", probe_spacing=128, probe_rays=64),
    "ultra": dict(dynamic_lights=1, gi_method="path|probes|off", shadows="traced|maps", shadow_map_filter="pcss",
                  glow_shadows=1, characters_shadow_baked=1, haze="high|low", ao=0, gi_resolution=3, shadow_rays=2,
                  gi_denoiser="nrd|filter", probe_spacing=96, probe_rays=64),
}


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def available(caps, key, value):
    if key == "gi_method":
        return value == "off" or (value == "probes" and caps["probes"]) or (value == "path" and caps["traced"])
    if key == "shadows":
        return value in ("off", "maps") or (value == "traced" and caps["traced"])
    if key == "haze":
        return value in ("off", "low") or (value == "high" and caps["traced"])
    if key == "gi_denoiser":
        return value != "nrd" or caps["nrd"]
    return True


def expected(preset, caps):
    out = {}
    for key, value in TABLE[preset].items():
        if isinstance(value, str):
            choices = value.split("|")
            value = next((c for c in choices if available(caps, key, c)), choices[-1])
        if key == "gi_resolution" and caps["integrated"]:
            value = min(value, 1.5)
        out[key] = value
    return out


def capture(game, path, timeout=60):
    """The next presented image (RPC present_dump), read once the PPM is
    complete: under load the game may still be writing it when it appears."""
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm.resolve()))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            data = ppm.read_bytes()
            header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", data)
            if header:
                w, h = int(header.group(1)), int(header.group(2))
                pixels = data[header.end():header.end() + w * h * 3]
                if len(pixels) == w * h * 3:
                    image = np.frombuffer(pixels, np.uint8).reshape(h, w, 3)
                    Image.fromarray(image).save(path)
                    ppm.unlink()
                    return image
        except (OSError, ValueError):
            pass
        time.sleep(0.05)
    raise RuntimeError(f"no complete frame in {timeout} s")


def autodetect(ray_query, integrated):
    if ray_query:
        return "high" if integrated else "ultra"
    return "low" if integrated else "medium"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--shots", help="keep a screenshot per backend and preset here")
    parser.add_argument("--window", default="960x720", help="window size (ROOD_WINDOW)")
    parser.add_argument("--only", help="run one configuration (e.g. vulkan-rt, vulkan-nort, gl, gl-nocompute)")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan" and os.environ.get("ROOD_ARCH") == "64"
    arch = "64" if os.environ.get("ROOD_ARCH") == "64" else "32"
    configs = ([("vulkan-rt", {}), ("vulkan-nort", {"ROOD_VK_RT": "0"})] if vulkan
               else [(f"gl{arch}", {}), (f"gl{arch}-nocompute", {"ROOD_NO_COMPUTE": "1"})])
    if args.only:
        configs = [c for c in configs if c[0] == args.only or c[0].startswith(args.only)]
    out = OUT / "lighting_presets"
    out.mkdir(parents=True, exist_ok=True)
    shots = pathlib.Path(args.shots) if args.shots else None
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    failures = []
    timings = {}

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    for name, extra in configs:
        card = out / "card1.mcd"
        shutil.copyfile(args.card, card)
        lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
        settings = out / f"settings-{name}.cfg"
        settings.unlink(missing_ok=True)
        env = {"ROOD_WINDOW": args.window, "ROOD_LIGHTS": str(lights), "ROOD_GI": "1", "ROOD_SCALE": "2",
               "ROOD_PERF": "1", "ROOD_SETTINGS": str(settings), **extra}
        socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-presets-{os.getpid()}.sock"
        print(f"== {name} ==", flush=True)
        game = Game.launch(fast=True, card=str(card), log=out / f"game-{name}.log", extra_env=env, socket_path=socket)
        try:
            game.wait_title()
            g = game.call("graphics")
            caps = g["lighting_caps"]
            print(f"caps {caps}", flush=True)
            # First run: the auto-detected preset.
            want = autodetect(caps["vulkan"] and caps["ray_query"], caps["integrated"])
            check(g["lighting_preset"] == want and g["lighting_autodetect"] == want,
                  f"{name}: first run picks {want} (got {g['lighting_preset']})")
            for rq in (0, 1):
                for integrated in (0, 1):
                    got = game.call("lighting_autodetect", ray_query=bool(rq), integrated=bool(integrated))["preset"]
                    check(got == autodetect(rq, integrated),
                          f"auto-detect ray_query={rq} integrated={integrated}: {got}")
            # Messages name this backend's reason.
            why = g["lighting_unavailable"]
            if caps["vulkan"]:
                check(caps["traced"] == (name == "vulkan-rt"), f"{name}: ray tracing {caps['traced']}")
                check(not any("OpenGL" in text for text in why.values()), f"{name}: no OpenGL advice on Vulkan {why}")
                if not caps["traced"]:
                    check("ROOD_VK_RT=0" in why.get("gi_method:path", "") and "ROOD_VK_RT=0" in why.get("shadows:traced", ""),
                          f"{name}: path tracing unavailable because ray tracing is off ({why.get('gi_method:path')})")
            else:
                check(caps["traced"] == (not extra), f"{name}: GL compute path tracer {caps['traced']}")
                check("Vulkan" in why.get("gi_denoiser:nrd", ""), f"{name}: NRD needs Vulkan ({why.get('gi_denoiser:nrd')})")
                check(not any("ray queries" in text for text in why.values()), f"{name}: no ray-query advice on GL")
                if not caps["traced"]:
                    check("OpenGL 4.3" in why.get("gi_method:path", ""), f"{name}: needs GL 4.3 compute "
                          f"({why.get('gi_method:path')})")
            check(("gi_method:probes" in why) != bool(caps["probes"]), f"{name}: probes {caps['probes']}")

            # The save room with a light by Ashley.
            game.press("start", vsyncs=4, settle=90)
            for _ in range(3):  # Continue, the card, its first save
                game.press("circle", vsyncs=3, settle=90)
            game.run(400)
            state = game.state()
            player = state.get("player")
            if not player or not game.call("scene")["triangles"]:
                print(f"FAIL: no room with Ashley after Continue: {state}")
                return 1
            # light <type> x y z  r g b  intensity radius  dx dy dz cone  shadows enabled
            (lights / f"zone{state['zone']:02d}_room{state['room']:02d}.txt").write_text(
                f"light point {player['x'] + 180} {player['y'] - 260} {player['z'] - 120} 1 0.8 0.55 1.4 1200 "
                "0 1 0 45 1 1\n")
            for _ in range(4):  # room files are polled twice a second
                game.run(15)
                time.sleep(0.25)
            check(len(game.call("lights")["lights"]) == 1, f"{name}: the test light is loaded")

            images = {}
            for preset in PRESETS:
                g = game.call("graphics", lighting_preset=preset)
                exp = expected(preset, caps)
                wrong = {k: (g[k], v) for k, v in exp.items() if g[k] != v and not (
                    isinstance(v, float) and abs(g[k] - v) < 1e-4)}
                check(g["lighting_preset"] == preset and not wrong, f"{name} {preset}: keys {wrong or 'as the table'}")
                game.run(60)
                game.call("perf", reset=True)
                game.run(240)
                perf = game.call("perf")
                timings[(name, preset)] = perf["gpu_ms"]
                path = out / f"{name}-{preset}.png"
                image = capture(game, path)
                images[preset] = image
                if shots:
                    shutil.copyfile(path, shots / path.name)
                path.unlink()
                lit = float(image.mean())
                # GL: every present counted, the CPU's wait for frames in flight, the path tracer's cadence.
                extra = (f", {perf['presents']} presents, waited {perf['gpu_wait_ms']:.0f} ms"
                         + (f", GI {perf['gi_traced']} traced/{perf['gi_reused']} reused at {perf['gi_gpu_ms']:.1f} ms"
                            if perf.get("gi_traced") else "") if "presents" in perf else "")
                check(lit > 8 and np.isfinite(image).all(), f"{name} {preset}: renders (mean {lit:.1f}, "
                      f"GPU {perf['gpu_ms']:.2f} ms over {perf['frames']} timed frames{extra})")
            d = float(np.abs(images["ultra"].astype(int) - images["original"].astype(int)).mean())
            check(d > 1.0, f"{name}: Ultra differs from Original (mean {d:.2f})")
            d = float(np.abs(images["low"].astype(int) - images["original"].astype(int)).mean())
            check(d > 0.5, f"{name}: Low (light, shadow maps, AO) differs from Original (mean {d:.2f})")

            # Custom: touching a key; choosing a preset again.
            g = game.call("graphics", lighting_preset="high")
            g = game.call("graphics", shadow_map_filter="hard")
            check(g["lighting_preset"] == "custom", f"{name}: changing a key makes the preset Custom")
            g = game.call("graphics", rt_shadows=False)  # the older switch counts too
            check(g["lighting_preset"] == "custom" and g["shadows"] in ("maps", "off"), f"{name}: rt_shadows -> Custom")
            g = game.call("graphics", lighting_preset="medium")
            check(g["lighting_preset"] == "medium" and g["shadow_map_filter"] == "pcss",
                  f"{name}: choosing Medium sets its keys again")
            g = game.call("graphics", ao_strength=0.5)  # not a preset key
            check(g["lighting_preset"] == "medium", f"{name}: AO strength is not part of the preset")

            # Persistence.
            game.call("settings", action="save")
            text = settings.read_text()
            check("lighting_preset medium" in text and "shadows maps" in text and "haze low" in text,
                  f"{name}: saved settings carry the preset and its keys")
            game.call("graphics", lighting_preset="original")
            g = game.call("settings", action="load")
            g = game.call("graphics")
            check(g["lighting_preset"] == "medium" and g["haze"] == "low", f"{name}: loading brings Medium back")
            game.call("graphics", lighting_preset="low", ao_radius=30, gi_method="off")
            game.call("settings", action="save")
            check("lighting_preset custom" in settings.read_text(), f"{name}: Custom saves as custom")
            game.call("graphics", lighting_preset="ultra")
            game.call("settings", action="load")
            g = game.call("graphics")
            check(g["lighting_preset"] == "custom" and g["ao_radius"] == 30 and g["shadows"] == "maps",
                  f"{name}: Custom loads its own values (ao_radius {g['ao_radius']}, shadows {g['shadows']})")

            # The editor's UI (screenshots): toolbar dropdown, Graphics > Lighting quality.
            if shots:
                game.call("graphics", lighting_preset=want)
                game.call("editor_ui", visible=True, details="graphics")
                game.run(8)
                capture(game, shots / f"{name}-ui-graphics.png")
                game.call("editor_ui", lighting_menu=True)
                game.run(4)
                capture(game, shots / f"{name}-ui-menu.png")
                game.call("editor_ui", visible=False)
        finally:
            game.close()
            shutil.rmtree(lights, ignore_errors=True)
        log = pathlib.Path(game.log).read_text(errors="replace")
        bad = [line for line in log.splitlines()
               if "[crash]" in line or "[gl] shader" in line or "[gl] compute" in line or "validation:" in line
               or "error" in line.lower() and ("[vk]" in line or "[gl]" in line)]
        check(not bad, f"{name}: no crash, shader or validation errors {bad[:3]}")

    print("GPU time per presented frame (ms, ROOD_PERF):")
    for (name, preset), ms in timings.items():
        print(f"  {name:16s} {preset:9s} {ms:7.2f}")
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: every preset applies, renders and persists; Custom and auto-detection work")
    return 0


if __name__ == "__main__":
    sys.exit(main())
