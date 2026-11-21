#!/usr/bin/env python3
"""pc/tools/room_blender.py without the game or Blender (Gitea #20): the scene dump's polygons to textures, the room's winding, the
glTF written, the maps made from a bake.

    python3 pc/tests/room_blender_test.py"""
import json
import pathlib
import sys
import tempfile

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import room_blender as rb  # noqa: E402

failures = 0


def check(ok, message):
    global failures
    print(("ok:   " if ok else "FAIL: ") + message)
    failures += not ok


DUMP = """# rood-boi scene capture serial 1, 4 triangles
v 0 0 0 0.5 0.5 0.5
v 512 0 0 0.5 0.5 0.5
v 0 0 512 0.5 0.5 0.5
v 512 0 0 0.5 0.5 0.5
v 512 0 512 0.5 0.5 0.5
v 0 0 512 0.5 0.5 0.5
v 0 0 0 0.5 0.5 0.5
v 512 0 0 0.5 0.5 0.5
v 0 -512 0 0.5 0.5 0.5
f 1 2 3 # tpage 0031 clut 38b8 group 4 flags 000042ff uv 10 20 41 20 10 50 code 3e quad 1
f 4 5 6 # tpage 0031 clut 38b8 group 4 flags 000042ff uv 41 20 41 50 10 50 code 3e quad 2
f 7 8 9 # tpage 0031 clut 38b8 group 5 flags 00000000 uv 200 200 201 200 200 201 code 3e quad 0
"""
USES = [{"key": "aaaaaaaaaa", "tpage": 17, "clut": 0x38b8, "box": [10, 20, 41, 50]},
        {"key": "bbbbbbbbbb", "tpage": 17, "clut": 0x38b8, "box": [0, 0, 255, 255]},
        {"key": "cccccccccc", "tpage": 17, "clut": 0x0001, "box": [10, 20, 41, 50]}]
with tempfile.TemporaryDirectory() as d:
    path = pathlib.Path(d) / "scene.obj"
    path.write_text(DUMP)
    positions, colours, tris = rb.parse_dump(path)
    check(positions.shape == (3, 3, 3) and len(tris) == 3 and tris[0]["group"] == 4 and tris[0]["uv"][1] == [41, 20],
          "the dump's triangles, texels and tags parse")
    found = rb.assign(tris, USES)
    check([u["key"] if u else None for u in found] == ["aaaaaaaaaa", "aaaaaaaaaa", "bbbbbbbbbb"],
          "each triangle takes the smallest box on its page and CLUT that holds its texels (not another CLUT's)")
    flip = rb.face_inward(positions)
    check(flip.shape == (3,), "a winding flag for each triangle")
    out = pathlib.Path(d) / "out"
    (out / "gltf" / "tex").mkdir(parents=True)
    textures = {"aaaaaaaaaa": {"w": 32, "h": 31, "box": [10, 20, 41, 50], "tpage": 17, "clut": 0x38b8},
                "bbbbbbbbbb": {"w": 256, "h": 256, "box": [0, 0, 255, 255], "tpage": 17, "clut": 0x38b8}}
    rb.write_gltf(out, positions, colours, tris, found, textures)
    g = json.loads((out / "gltf" / "room.gltf").read_text())
    prims = g["meshes"][0]["primitives"]
    check(sorted(p["extras"]["key"] for p in prims) == ["aaaaaaaaaa", "bbbbbbbbbb"] and len(g["materials"]) == 2,
          "one primitive and material per texture")
    blob = (out / "gltf" / "room.bin").read_bytes()
    a = [x for x in g["accessors"] if x["type"] == "VEC2"]
    view = g["bufferViews"][a[0]["bufferView"]]
    uv = np.frombuffer(blob, np.float32, a[0]["count"] * 2, view["byteOffset"]).reshape(-1, 2)
    check(a[0]["count"] == 6 and abs(uv[:, 0].max() - 31 / 32) < 1e-6 and uv.min() == 0.0 and uv.max() < 1.0,
          "UVs run over the texture's own box: texel u0 at 0, a texel k of w at k / w")
    check(len(prims[0]["extras"]["triangles"]) == 2 and prims[0]["extras"]["triangles"][0]["group"] == 4,
          "each primitive keeps its triangles' tags")
p = {"height_base": 140.0, "height_wear": -70.0, "height_cavity": -45.0, "rough_base": 0.8, "rough_wear": -0.3,
     "rough_cavity": 0.14, "normal_strength": 3.0}
wear = np.zeros((4, 1), np.float32)
wear[0, 0] = 1.0
cover = np.ones((4, 1), np.float32)
cover[3, 0] = 0.0
h, r, n = rb.maps(wear, np.zeros((4, 1), np.float32), cover, p)
check(h.shape == (4, 1) and h[0, 0] < h[1, 0] and h[3, 0] == 128 and r[0, 0] < r[1, 0] and n.shape == (4, 1, 3),
      "the maps: wear carves the height and polishes, an uncovered texel stays the blank template; a one texel wide texture works")
print("FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
