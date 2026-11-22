#!/usr/bin/env python3
"""The per-room key light (pc/docs/Materials.md "The key light"; room settings key_*, graphics key_light,
key_light_strength; lighting/default/defaults.txt).

    python3 pc/tests/material_key_light_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Data (any renderer): the switch and the strength are lighting-quality tunables (RPC graphics); every key_* is a room
setting (RPC room_settings: value, project default, whether the room overrides it) whose default is the pack's
defaults.txt line (the interior one, or `exterior.` for a room open to the sky); an override sets it and a reset
returns it; the exterior and interior defaults differ where the file says so.

Pictures (Vulkan), the Keep (29/3) at the game camera, steady frames (preset Low, no AO, no traced reflections):
- the paused frame redrawn is byte-identical, key light on and off;
- off, strength 0, intensity 0 and a room whose lighting switch is off are all the same picture (byte for byte);
- on, the picture changes, the walls (relief mapped) gain contrast (the high-pass deviation of their luminance rises)
  while their mean luminance stays within 2.5 % (relief only: light moves between the slopes);
- with the relief share 0 it is a plain light: the walls get brighter;
- the same room settings give the same frame twice (deterministic).
OpenGL draws no relief: only the data is checked.
"""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import material_survey  # noqa: E402

BROAD = {"wall": (420, 150, 520, 330), "far wall": (930, 300, 1000, 400)}
KEYS = ("key_intensity", "key_relief", "key_gain", "key_azimuth", "key_elevation", "key_view", "key_red", "key_green",
        "key_blue", "key_specular")


def pack_defaults():
    """lighting/default/defaults.txt: {key: value} and {key: exterior value}."""
    interior, exterior = {}, {}
    for line in (ROOT / "lighting/default/defaults.txt").read_text().splitlines():
        line = line.split("#")[0].split()
        if len(line) == 3 and line[0] == "set":
            (exterior if line[1].startswith("exterior.") else interior)[line[1].removeprefix("exterior.")] = float(line[2])
    return interior, exterior


def luma(img):
    return img @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def crop(img, box):
    return img[box[1]:box[3], box[0]:box[2]]


def detail(img, box):
    y = luma(crop(img, box))
    k = 3
    pad = np.pad(y, k, mode="edge")
    mean = sum(pad[k + j:k + j + y.shape[0], k + i:k + i + y.shape[1]] for j in range(-k, k + 1) for i in range(-k, k + 1))
    return float((y - mean / (2 * k + 1) ** 2).std())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the frames and logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    out = pathlib.Path(args.out) if args.out else OUT / "material_key_light"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = out / "lights"
    lights.mkdir(exist_ok=True)
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\naa off\n")
    env = {"ROOD_LIGHTS": str(lights), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_SCALE": "2",
           "ROOD_WINDOW": "1280x960", "ROOD_GI": "1" if vulkan else "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_MATERIAL_CATALOG_USER": str(out / "materials.json"), "ROOD_MATERIALS": str(out / "maps"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports")}
    (out / "materials.json").unlink(missing_ok=True)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-material-key-light-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    interior, exterior = pack_defaults()
    check(set(KEYS) <= set(interior) and set(KEYS) <= set(exterior), "the pack's defaults.txt has both defaults of every key")
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        def still_shot(name):  # the game held: the same frame redrawn
            game.call("redraw")
            game.wait_presents(4)
            game.call("redraw")
            game.wait_presents(4)
            game.present_screenshot(str(out / f"{name}.png"))
            return np.asarray(Image.open(out / f"{name}.png").convert("RGB")).astype(np.float32)

        g = game.call("graphics")
        check(g.get("key_light") == 0 and g.get("key_light_strength") == 1, f"the switch and strength are tunables, the light off by default: {g.get('key_light')}, {g.get('key_light_strength')}")
        game.call("graphics", key_light=1)
        game.call("graphics", key_light_strength=0.5)
        check(game.call("graphics").get("key_light_strength") == 0.5, "graphics sets the strength")
        game.call("graphics", key_light_strength=1)

        def room(**kw):
            return game.call("room_settings", **kw)

        if not material_survey.goto(game, 29, 3):
            print("FAIL: could not reach the Keep")
            return 1
        rs = room()
        check(all(k in rs for k in KEYS), "every key light value is a room setting")
        check(all(rs[k]["default"] == interior[k] and not rs[k]["room"] for k in KEYS),
              "the Keep (a roofed room) has the interior defaults, none overridden")
        rs = room(key_azimuth=200)
        check(rs["key_azimuth"]["value"] == 200 and rs["key_azimuth"]["room"], "an override sets the room's value")
        rs = room(key_azimuth="default")
        check(rs["key_azimuth"]["value"] == interior["key_azimuth"] and not rs["key_azimuth"]["room"], "and a reset returns the default")
        if material_survey.goto(game, 40, 0):  # the Snowfly Forest is open to the sky
            game.run(10)
            rs = room()
            check(all(rs[k]["default"] == exterior[k] for k in KEYS), "a room open to the sky takes the exterior defaults")
            check(any(exterior[k] != interior[k] for k in KEYS), "and they differ from the interior ones")
            rs = room(key_elevation=10)
            check(rs["key_elevation"]["value"] == 10 and rs["key_elevation"]["room"], "an override there holds")
            room(key_elevation="default")
        if not material_survey.goto(game, 29, 3):
            print("FAIL: could not return to the Keep")
            return 1
        if not vulkan:
            print("OpenGL: no relief drawn; data only")
        else:
            game.call("editor_camera", off=1)
            game.call("graphics", interpolate=False, view_mode=0)
            before = game.call("graphics")
            game.call("graphics", lighting_preset="low", ao=0, reflection_bounces=0, key_light=0)
            off = still_shot("key-off")
            check(np.array_equal(off, still_shot("key-off-again")), "the paused frame redrawn is steady (key light off)")
            game.call("graphics", key_light=1)
            on = still_shot("key-on")
            check(np.array_equal(on, still_shot("key-on-again")), "the paused frame redrawn is steady (key light on): deterministic")
            check(float(np.abs(on - off).mean()) > 0.05, f"the key light changes the picture ({float(np.abs(on - off).mean()):.3f})")
            game.call("graphics", key_light_strength=0)
            check(np.array_equal(still_shot("key-strength-0"), off), "strength 0 is the picture without it, byte for byte")
            game.call("graphics", key_light_strength=1)
            room(key_intensity=0)
            check(np.array_equal(still_shot("key-intensity-0"), off), "intensity 0 is the picture without it, byte for byte")
            room(key_intensity="default")
            for name, box in BROAD.items():
                m0, m1 = float(luma(crop(off, box)).mean()), float(luma(crop(on, box)).mean())
                d0, d1 = detail(off, box), detail(on, box)
                check(abs(m1 / max(m0, 1e-3) - 1) < 0.025, f"{name}: mean luminance kept ({m0:.2f} -> {m1:.2f})")
                check(d1 > d0 * 1.02, f"{name}: the relief shows more (detail {d0:.2f} -> {d1:.2f})")
            room(key_relief=0)
            plain = still_shot("key-plain")
            for name, box in BROAD.items():
                m0, m1 = float(luma(crop(off, box)).mean()), float(luma(crop(plain, box)).mean())
                check(m1 > m0 * 1.02, f"{name}: with no relief share it is a plain light, brighter ({m0:.2f} -> {m1:.2f})")
            room(key_relief="default")
            game.call("graphics", **{k: before[k] for k in ("lighting_preset", "ao", "reflection_bounces") if k in before})
    finally:
        game.close()
    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
