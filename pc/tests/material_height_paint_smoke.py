#!/usr/bin/env python3
"""A painted height map shades as the baked one does (pc/docs/Materials.md "Height and normals"; RPC material map_set).

    python3 pc/tests/material_height_paint_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The Keep (29/3), Vulkan, in the relief normals view (view_mode 9), the game held (a still picture per check), the estimate previewed on every
texture (RPC material preview) and the baked layer off, so the relief every texture has is its estimate. Then a wall texture's height set
in memory the way the Aseprite bridge does (RPC material map_set, channel height):

- a map that is flat adds nothing: with painted_texture_relief 1 the picture is the estimate's, pixel for pixel (a painted map keeps the
  texture's own relief, as a baked one does, so a first stroke does not change the texels it did not touch);
- with painted_texture_relief 0 the same flat map is the whole relief: the picture changes (the tunable is live);
- one raised column of texels changes the picture, and only around that texel: the changed pixels are far fewer than a ramp over the whole
  texture changes, and none lies outside the ones the ramp changed;
- a ramp painted live and the same ramp as a baked map (the bake's layer) give the same picture at each share of the estimate:
  the painted map shades as the baked one does;
- RPC material map_clear takes a map set in memory away (the game's own relief shows again);
- a map of the wrong size is refused with a message, and a channel the game does not know too;
OpenGL draws no relief: only the tunable is checked.
"""
import argparse
import base64
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import material_survey  # noqa: E402

WALL = (470, 240)  # a stone wall of the Keep at the game camera (pixel)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--out", help="keep the frames and logs here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    out = pathlib.Path(args.out) if args.out else OUT / "material_height_paint"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = out / "lights"
    lights.mkdir(exist_ok=True)
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\naa off\n")
    env = {"ROOD_LIGHTS": str(lights), "ROOD_LIGHTING": str(ROOT / "lighting"), "ROOD_SCALE": "2",
           "ROOD_WINDOW": "1280x960", "ROOD_GI": "1" if vulkan else "0", "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_MATERIAL_CATALOG_USER": str(out / "materials.json"), "ROOD_MATERIALS": str(out / "maps"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports")}
    (out / "materials.json").unlink(missing_ok=True)
    shutil.rmtree(out / "baked", ignore_errors=True)
    shutil.rmtree(out / "maps", ignore_errors=True)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-height-paint-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        def shot(name):
            for _ in range(3):  # (the game held: a map set in memory shows on the second redraw)
                game.call("redraw")
                game.wait_presents(4)
            game.present_screenshot(str(out / f"{name}.png"))
            return np.asarray(Image.open(out / f"{name}.png").convert("RGB")).astype(np.int32)

        def differing(a, b):
            return np.abs(a - b).max(2) > 0

        game.call("graphics", painted_texture_relief=0.25)
        check(abs(game.call("graphics")["painted_texture_relief"] - 0.25) < 1e-6, "painted_texture_relief is a tunable (RPC graphics)")
        game.call("graphics", painted_texture_relief=1)
        if not vulkan:
            print("OpenGL: no relief drawn; the tunable only")
        elif not material_survey.goto(game, 29, 3):
            print("FAIL: could not reach the Keep")
            return 1
        else:
            game.call("editor_camera", off=1)
            game.call("graphics", interpolate=False, view_mode=9, bump_strength=1, key_light=0, materials_baked=0)
            game.call("material", action="preview", auto_height=True)
            pick = game.call("material", action="pick", x=WALL[0], y=WALL[1])
            key, (w, h) = pick["key"], pick["size"]  # (the texture's own map: a map of an object also needs a catalog entry that names it, which the paint bridge writes)

            def put(array, channel="height", width=w, height=h):
                game.call("material", action="map_set", key=key, channel=channel, w=width, h=height,
                          data=base64.b64encode(np.ascontiguousarray(array).tobytes()).decode())
            estimate = shot("estimate")
            check(not differing(estimate, shot("estimate2")).any(), "the held picture is steady (no map: the estimate's relief)")
            flat = np.full((h, w), 128, np.uint8)
            ramp = np.tile((np.arange(w) * 7 % 256).astype(np.uint8), (h, 1))
            # the ramp as a baked map first (the bake's layer, the game's own; no map in memory yet): the picture to match later
            (out / "baked" / key).mkdir(parents=True)
            Image.fromarray(ramp, "L").save(out / "baked" / key / "height.png")
            game.call("material", action="reload")
            game.call("graphics", materials_baked=1)
            baked_ramp, painted_ramp = {}, {}
            for share in (1, 0, 0.5):
                game.call("graphics", baked_texture_relief=share, painted_texture_relief=share)
                baked_ramp[share] = shot(f"ramp-baked-{share}")
            game.call("graphics", materials_baked=0, baked_texture_relief=1, painted_texture_relief=1)
            check(not differing(shot("estimate-again"), estimate).any(), "the baked layer off, the estimate's picture is back")
            put(flat)
            kept = shot("flat-kept")
            check(not differing(kept, estimate).any(),
                  f"a flat painted height with painted_texture_relief 1 is the estimate's picture ({int(differing(kept, estimate).sum())} pixels differ)")
            game.call("graphics", painted_texture_relief=0)
            alone = shot("flat-alone")
            check(differing(alone, estimate).sum() > 50, f"with painted_texture_relief 0 the flat map is the whole relief: the picture changes ({int(differing(alone, estimate).sum())} pixels)")
            game.call("graphics", painted_texture_relief=1)
            spike = flat.copy()
            spike[:, w // 2] = 255  # (a stroke down the texture: the one column of texels)
            put(spike)
            one = shot("one-column")
            put(ramp)
            whole = shot("ramp")
            one_px, whole_px = differing(one, kept), differing(whole, kept)
            check(0 < one_px.sum() < whole_px.sum() // 4, f"one raised column of texels changes the relief around it ({int(one_px.sum())} pixels), far less than a ramp over the texture ({int(whole_px.sum())})")
            check(not (one_px & ~whole_px).any(), "...and only where the texture is: no pixel outside the ones the ramp changed")
            put(flat)
            back = shot("flat-again")
            check(not differing(back, kept).any(), "setting the flat map again puts the picture back")
            put(ramp)
            for share in (1, 0, 0.5):  # (the same ramp painted now, at each share of the estimate)
                game.call("graphics", baked_texture_relief=share, painted_texture_relief=share)
                painted_ramp[share] = shot(f"ramp-painted-{share}")
                check(not differing(painted_ramp[share], baked_ramp[share]).any() and differing(painted_ramp[share], kept).any(),
                      f"a ramp painted live shades as the same ramp baked, with {share} of the estimate on both ({int(differing(painted_ramp[share], baked_ramp[share]).sum())} pixels differ)")
            game.call("graphics", baked_texture_relief=1, painted_texture_relief=1)
            game.call("material", action="map_clear", key=key, channel="height")
            check(not differing(shot("cleared"), estimate).any(), "map_clear takes the map away: the estimate's picture is back")
            for bad, why in ((dict(array=np.zeros((h + 1, w), np.uint8), width=w, height=h), "a map of the wrong size"),
                             (dict(array=flat, channel="sparkle"), "a channel the game does not know")):
                try:
                    put(**bad)
                    check(False, f"{why} is refused")
                except RpcError as e:
                    check("map_set" in str(e), f"{why} is refused: {e}")
    finally:
        game.close()
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: a painted height map shades as a baked one does")
    return 0


if __name__ == "__main__":
    sys.exit(main())
