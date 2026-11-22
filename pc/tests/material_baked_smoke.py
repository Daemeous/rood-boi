#!/usr/bin/env python3
"""The baked material layer (pc/docs/Materials.md, "Baked maps"; RPC graphics
materials_baked, emissive materials, material list).

    python3 pc/tests/material_baked_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The Keep (29/3) at the game camera (no GI: still frames are exact) with the
shipped catalog, scratch user, shipped and baked layers. The biggest stone
wall texture whose relief is estimated from its colours:

- a height.png written into the baked layer is picked up live (the use's
  relief turns "painted", the channel is listed as baked);
- materials_baked 0 is the picture without the baked layer, byte for byte
  (the estimate again), and the setting is listed by RPC material list;
- on again: the frame changes (Vulkan) and the relief is the baked map's;
- a user height.png wins over the baked one (listed as the user's).
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


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the frames and logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    out = pathlib.Path(args.out) if args.out else OUT / "material_baked"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    (out / "lights").mkdir()
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\naa off\n")
    baked, user = out / "baked", out / "maps"
    env = {"ROOD_LIGHTS": str(out / "lights"), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_SCALE": "2",
           "ROOD_WINDOW": "1280x960", "ROOD_GI": "1" if vulkan else "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_MATERIAL_CATALOG_USER": str(out / "materials.json"), "ROOD_MATERIALS": str(user),
           "ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"),
           "ROOD_MATERIALS_SHIPPED": str(out / "shipped-maps"), "ROOD_MATERIALS_BAKED": str(baked),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-material-baked-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        def still_shot(name):
            for _ in range(2):
                game.call("redraw")
                game.wait_presents(4)
            game.present_screenshot(str(out / f"{name}.png"))
            return np.asarray(Image.open(out / f"{name}.png").convert("RGB"))

        def settle():  # past the maps' poll, the game held (redraws only: the same frame)
            for _ in range(40):
                game.call("redraw")
                game.wait_presents(2)

        def use_of(key):
            for u in game.call("material", action="list")["uses"]:
                if u["key"] == key:
                    return u
            return None

        def channel(key, name):
            for c in game.call("emissive", action="materials", key=key).get("channels", []):
                if c["name"] == name:
                    return c
            return None

        if not material_survey.goto(game, 29, 3):
            print("FAIL: could not reach the Keep")
            return 1
        game.call("editor_camera", off=1)
        game.call("graphics", interpolate=False, bump_strength=1)
        before = still_shot("before")
        listed = game.call("material", action="list")
        check(listed.get("materials_baked") is True and listed.get("baked_dir") == str(baked),
              f"materials_baked listed, on by default, the layer from $ROOD_MATERIALS_BAKED: {listed.get('baked_dir')}")
        estimated = [u for u in listed["uses"] if u["relief"] == "estimated"]
        check(bool(estimated), f"the Keep has walls whose relief is estimated ({len(estimated)})")
        if not estimated:
            return 1
        biggest = max(estimated, key=lambda u: u["size"][0] * u["size"][1])
        key, (w, h) = biggest["key"], biggest["size"]

        y, x = np.mgrid[0:h, 0:w]
        grooves = np.where((x % 8 < 2) | (y % 8 < 2), 40, 200).astype(np.uint8)
        (baked / key).mkdir(parents=True)
        Image.fromarray(grooves, "L").save(baked / key / "height.png")
        settle()
        u, c = use_of(key), channel(key, "height")
        check(u and u["relief"] == "painted", f"a baked height.png is picked up live: relief {u and u['relief']}")
        check(c and c.get("baked") is True and c.get("user") is False, f"listed as baked: {c}")
        on = still_shot("baked-on")

        game.call("graphics", materials_baked=0)
        settle()
        u, c = use_of(key), channel(key, "height")
        check(game.call("material", action="list").get("materials_baked") is False, "graphics switches it off")
        check(u and u["relief"] == "estimated" and c is None, f"off: the estimate again, no baked channel ({u and u['relief']})")
        off = still_shot("baked-off")
        check(np.array_equal(off, before), "off is the picture without the baked layer, byte for byte")
        if vulkan:
            check(not np.array_equal(on, before), "on: the baked relief changes the picture")
        game.call("graphics", materials_baked=1)
        settle()
        check(use_of(key)["relief"] == "painted", "on again: the baked relief")

        (user / key).mkdir(parents=True)
        Image.fromarray(np.full((h, w), 128, np.uint8), "L").save(user / key / "height.png")
        settle()
        c = channel(key, "height")
        check(c and c.get("user") is True and c.get("baked") is False and str(user) in c.get("path", ""),
              f"a user height.png wins over the baked one: {c}")
    finally:
        game.close()
    if failures:
        print(f"{len(failures)} failure(s)")
        return 1
    print("material_baked: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
