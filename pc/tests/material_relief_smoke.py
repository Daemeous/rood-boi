#!/usr/bin/env python3
"""The relief fades to flat where the texture is stretched (pc/docs/Materials.md,
"Height and normals"; RPC graphics relief_aniso, relief_footprint).

    python3 pc/tests/material_relief_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The Keep (29/3) at the game camera with the shipped catalog (its stone walls
estimate their relief from the colours). Vulkan, in the relief normals view
(view_mode 9):

- the keys are tunables: RPC material list returns them, graphics sets them;
- the side of the arch is a face whose texture coordinates are nearly
  degenerate (mapped to a sliver): without the limit (relief_aniso 0) its
  relief normals are speckled, with it they are smooth (neighbouring pixels'
  mean difference below a bound);
- the broad faces (the wall, the far wall, the floor) are unchanged by the
  limit (mean difference of the two pictures below a bound) and still show
  relief (the picture differs from bump_strength 0 there).

Then in the lit picture (view_mode 0; the Keep has no dynamic light, so its
walls are the game's baked light and the indirect light only), the relief under
the baked and indirect light (relief_baked, relief_indirect, relief_reach,
relief_overhead; "Height and normals"):

- with both strengths 0 the frame is byte-identical to one with them on but
  nothing to give the light a direction (reach 0, no light from above: every
  factor exactly 1), i.e. to the picture without the terms;
- with the defaults the relief-mapped walls gain contrast (the high-pass
  deviation of their luminance rises) while their mean luminance stays within
  1.5 % (the terms only move light between the slopes).
OpenGL draws no relief: only the tunables are checked.
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

# Pixel boxes (x0, y0, x1, y1) of the 1280 x 960 game camera in the Keep.
STRIP = (330, 260, 400, 360)  # the arch's side
BROAD = {"wall": (420, 150, 520, 330), "far wall": (930, 300, 1000, 400), "floor": (300, 700, 700, 900)}


def noise(img, box):
    c = img[box[1]:box[3], box[0]:box[2]]
    return float(np.abs(np.diff(c, axis=1)).mean() + np.abs(np.diff(c, axis=0)).mean())


def crop(img, box):
    return img[box[1]:box[3], box[0]:box[2]]


def luma(img):
    return img @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def detail(img, box):
    """The high-pass deviation of the luminance in box (minus a 7 x 7 mean)."""
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
    out = pathlib.Path(args.out) if args.out else OUT / "material_relief"
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
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-material-relief-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    try:
        def shot(name):
            for _ in range(2):
                game.run(4)
                game.call("redraw")
                game.wait_presents(4)
            game.present_screenshot(str(out / f"{name}.png"))
            return np.asarray(Image.open(out / f"{name}.png").convert("RGB")).astype(np.float32)

        def still_shot(name):  # the game held: the same frame redrawn
            game.call("redraw")
            game.wait_presents(4)
            game.call("redraw")
            game.wait_presents(4)
            game.present_screenshot(str(out / f"{name}.png"))
            return np.asarray(Image.open(out / f"{name}.png").convert("RGB")).astype(np.float32)

        listed = game.call("material", action="list")
        check(listed.get("relief_aniso", 0) > 0 and listed.get("relief_footprint", 0) > 0,
              f"the limits are tunables with defaults on: {listed.get('relief_aniso')}, {listed.get('relief_footprint')}")
        game.call("graphics", relief_aniso=3, relief_footprint=5)
        again = game.call("material", action="list")
        check(again.get("relief_aniso") == 3 and again.get("relief_footprint") == 5, "graphics sets them")
        game.call("graphics", relief_aniso=listed["relief_aniso"], relief_footprint=listed["relief_footprint"])
        if not vulkan:
            print("OpenGL: no relief drawn; tunables only")
        else:
            if not material_survey.goto(game, 29, 3):
                print("FAIL: could not reach the Keep")
                return 1
            game.call("editor_camera", off=1)
            game.call("graphics", interpolate=False, view_mode=9, bump_strength=1, key_light=0)  # (the key light: its own smoke)
            on = shot("normals-limited")
            game.call("graphics", relief_aniso=0, relief_footprint=0)
            off = shot("normals-unlimited")
            game.call("graphics", relief_aniso=listed["relief_aniso"], relief_footprint=listed["relief_footprint"],
                      bump_strength=0)
            flat = shot("normals-flat")
            game.call("graphics", bump_strength=1)
            n_off, n_on = noise(off, STRIP), noise(on, STRIP)
            check(n_off > 6 and n_on < 2, f"the arch's side: speckled without the limit ({n_off:.2f}), smooth with it ({n_on:.2f})")
            for name, box in BROAD.items():
                diff = float(np.abs(crop(on, box) - crop(off, box)).mean())
                relief = float(np.abs(crop(on, box) - crop(flat, box)).mean())
                check(diff < 0.5, f"{name}: unchanged by the limit (mean difference {diff:.3f})")
                if name != "floor":  # the floor's stone is smooth: its relief is slight
                    check(relief > 1.0, f"{name}: still shows relief (mean difference from no relief {relief:.2f})")
                else:
                    print(f"floor relief difference from no relief {relief:.2f}")
            # The relief under the baked and indirect light.
            terms = ("relief_baked", "relief_indirect", "relief_reach", "relief_overhead")
            check(all(k in listed for k in terms), "the baked/indirect relief terms are tunables: "
                  + ", ".join(f"{k} {listed.get(k)}" for k in terms))
            check(listed.get("relief_baked", 0) > 0 and listed.get("relief_indirect", 0) > 0, "and on by default")
            # Byte for byte where a redrawn paused frame is the same every time (preset Low, no ambient
            # occlusion, no traced reflections: material_smoke's): strength 0 against the terms on with
            # nothing to give the light a direction (every factor exactly 1).
            before = game.call("graphics")
            game.call("graphics", view_mode=0, lighting_preset="low")
            game.call("graphics", ao=0, reflection_bounces=0, relief_baked=0, relief_indirect=0)
            still = still_shot("still-terms-0")
            check(np.array_equal(still, still_shot("still-terms-0-again")), "the paused frame redrawn is steady")
            game.call("graphics", relief_baked=listed["relief_baked"], relief_indirect=listed["relief_indirect"],
                      relief_reach=0, relief_overhead=0)
            still_undirected = still_shot("still-terms-undirected")
            game.call("graphics", relief_reach=listed["relief_reach"], relief_overhead=listed["relief_overhead"])
            still_on = still_shot("still-terms-on")
            differ = int((still != still_undirected).any(axis=2).sum())
            check(differ == 0, f"steady: strength 0 is byte-identical to the terms without a direction ({differ} pixels differ)")
            check(float(np.abs(still - still_on).mean()) > 0.05, "steady: the terms change the picture")
            game.call("graphics", lighting_preset=before["lighting_preset"])
            game.call("graphics", **{k: before[k] for k in ("ao", "reflection_bounces", "gi_method", "shadows") if k in before})
            game.call("graphics", view_mode=0, relief_baked=0, relief_indirect=0)
            zero = shot("lit-terms-0")
            game.call("graphics", relief_baked=listed["relief_baked"], relief_indirect=listed["relief_indirect"],
                      relief_reach=0, relief_overhead=0)
            undirected = shot("lit-terms-undirected")
            game.call("graphics", relief_reach=listed["relief_reach"], relief_overhead=listed["relief_overhead"])
            lit = shot("lit-terms-on")
            game.call("graphics", relief_baked=0, relief_indirect=0)
            again = shot("lit-terms-0-again")
            game.call("graphics", relief_baked=listed["relief_baked"], relief_indirect=listed["relief_indirect"])
            steady = np.array_equal(zero, again)
            same = int((zero != undirected).any(axis=2).sum())
            if steady:
                check(same == 0, f"strength 0 is the picture without the terms (pixels differing: {same})")
            else:  # the traced light still moving between the shots: within its own frame-to-frame change
                drift = float(np.abs(zero - again).mean())
                check(float(np.abs(zero - undirected).mean()) <= drift + 0.05,
                      f"strength 0 is the picture without the terms (within the frames' own drift {drift:.3f})")
            for name in ("wall", "far wall"):
                box = BROAD[name]
                m0, m1 = float(luma(crop(zero, box)).mean()), float(luma(crop(lit, box)).mean())
                d0, d1 = detail(zero, box), detail(lit, box)
                check(abs(m1 / max(m0, 1e-3) - 1) < 0.015, f"{name}: mean luminance kept ({m0:.2f} -> {m1:.2f})")
                check(d1 > d0 * 1.03, f"{name}: the relief catches the baked light (detail {d0:.2f} -> {d1:.2f})")
    finally:
        game.close()
    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
