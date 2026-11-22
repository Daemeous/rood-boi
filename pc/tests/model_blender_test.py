#!/usr/bin/env python3
"""pc/tools/model_blender.py without the game or Blender (Gitea #20): the model's settings over the room's, the catalog entries a model's
textures get, the maps cut from an atlas bake into each box of it.

    python3 pc/tests/model_blender_test.py"""
import json
import pathlib
import sys
import tempfile
import argparse

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import model_blender as mb  # noqa: E402
import room_blender as rb  # noqa: E402

failures = 0


def check(ok, message):
    global failures
    print(("ok:   " if ok else "FAIL: ") + message)
    failures += not ok


cfg, room = mb.settings(), rb.config()
check(cfg["bake"]["bevel_radius"] == room["model"]["bake"]["bevel_radius"] != room["bake"]["bevel_radius"]
      and cfg["bake"]["wear_gain"] == room["bake"]["wear_gain"] and cfg["maps"] == room["maps"],
      "a model's bake takes the model section's values over the room's, the rest the room's")

SEED = {"labels": {"m4b8870d7_p16_c3930": {
    "key": "m4b8870d7_p16_c3930", "object": "m4b8870d7", "model": "4b8870d7", "w": 16, "h": 12,
    "slices": [{"key": "aaaaaaaaaa", "box": [0, 0, 7, 5]}, {"key": "bbbbbbbbbb", "box": [8, 6, 10, 11]}]}}}
got = mb.entries(SEED, "stone")
check([e["match"] for e in got] == [{"key": "aaaaaaaaaa", "size": [8, 6]}, {"key": "bbbbbbbbbb", "size": [3, 6]}]
      and all(e["object"] == {"models": ["4b8870d7"]} and e["preset"] == "stone" and "scope" not in e for e in got),
      "an entry per texture of the atlas, keyed by the model (every copy, any room)")
check(len({e["id"] for e in got}) == 2, "...each with an id of its own")

with tempfile.TemporaryDirectory() as d:
    out = pathlib.Path(d)
    (out / "seed.json").write_text(json.dumps(SEED))
    label = "m4b8870d7_p16_c3930"
    bakes = out / "bakes" / label
    bakes.mkdir(parents=True)
    wear = np.zeros((12, 16), np.uint8)
    wear[:, 8:] = 255  # (the second box is all worn edge, the first none)
    for name, array in (("wear", wear), ("cavity", np.zeros_like(wear)), ("cover", np.full_like(wear, 255))):
        Image.fromarray(array, "L").save(bakes / f"{name}.png")
    mb.import_maps(argparse.Namespace(out=str(out), model="4b8870d7"))
    layer = out / "materials"
    files = sorted(str(f.relative_to(layer)) for f in layer.rglob("*.png"))
    check(files == [f"{k}/@m4b8870d7/{n}.png" for k in ("aaaaaaaaaa", "bbbbbbbbbb") for n in ("height", "normal", "roughness")],
          f"height, roughness and normal for each texture, in the folder of its model: {files}")
    a = np.asarray(Image.open(layer / "aaaaaaaaaa" / "@m4b8870d7" / "roughness.png"))
    b = np.asarray(Image.open(layer / "bbbbbbbbbb" / "@m4b8870d7" / "roughness.png"))
    check(a.shape == (6, 8) and b.shape == (6, 3) and a.min() > b.max(), "each cut to its texture's size, from its own part of the bake (worn is smoother)")
print("FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
