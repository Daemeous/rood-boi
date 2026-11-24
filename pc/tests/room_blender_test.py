#!/usr/bin/env python3
"""pc/tools/room_blender.py without the game or Blender: the scene dump's polygons to textures, the room's winding, the
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
# a model's faces turn outward whatever way they were drawn (no culling: the winding is mixed); a room's face its middle
def tetra(winding, at=0.0):
    v = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], np.float32) + at
    faces = [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)]  # (each seen from outside)
    return np.array([[v[i] for i in (f if w else f[::-1])] for f, w in zip(faces, winding)], np.float32)


def outward(positions, flip):
    """Whether each face, turned over where `flip` says, points away from its piece's middle."""
    first, second, third = positions[:, 0], np.where(flip[:, None], positions[:, 2], positions[:, 1]), np.where(flip[:, None], positions[:, 1], positions[:, 2])
    return (np.cross(second - first, third - first) * (positions.mean(axis=1) - positions.reshape(-1, 3).mean(axis=0))).sum(axis=1) > 0


for winding in ([1, 1, 1, 1], [0, 1, 0, 0], [0, 0, 0, 0]):
    t = tetra(winding)
    check(outward(t, rb.face_outward(t)).all(), f"a closed piece drawn with winding {winding} is turned outward as a whole")
check(not outward(tetra([0, 1, 0, 0]), np.zeros(4, bool)).all(), "(the winding as drawn is not outward: the flags do the turning)")
pieces = np.concatenate([tetra([1, 0, 1, 1]), tetra([0, 0, 0, 0], 5.0)])
flip = rb.face_outward(pieces)
check(outward(pieces[:4], flip[:4]).all() and outward(pieces[4:], flip[4:]).all(), "two separate pieces are each turned outward on their own")
# a texture shared by two room groups is baked per group (materials by object)
GROUPS = [{"offset": 0x100, "first": 0x108, "end": 0x108 + 2 * 32}, {"offset": 0x200, "first": 0x208, "end": 0x208 + 32}]
shared = [{"tpage": 17, "clut": 0x38b8, "group": 0, "flags": 0, "uv": [[10, 20], [41, 20], [10, 50]], "polygon": 0x108},
          {"tpage": 17, "clut": 0x38b8, "group": 0, "flags": 0, "uv": [[10, 20], [41, 20], [10, 50]], "polygon": 0x128},
          {"tpage": 17, "clut": 0x38b8, "group": 1, "flags": 0, "uv": [[10, 20], [41, 20], [10, 50]], "polygon": 0x208},
          {"tpage": 17, "clut": 0x38b8, "group": 2, "flags": 0, "uv": [[200, 200], [201, 200], [200, 201]], "polygon": 0x300}]
check([rb.object_of(t, GROUPS) for t in shared] == [0x100, 0x100, 0x200, None],
      "a triangle's room group: the block whose polygons hold its polygon offset")
found = rb.assign(shared, USES)
textures2 = {"aaaaaaaaaa": {"w": 32, "h": 31, "box": [10, 20, 41, 50], "tpage": 17, "clut": 0x38b8},
             "bbbbbbbbbb": {"w": 256, "h": 256, "box": [0, 0, 255, 255], "tpage": 17, "clut": 0x38b8}}
labels, split = rb.split_shared(shared, found, GROUPS, textures2)
check(labels == ["aaaaaaaaaa@g100", "aaaaaaaaaa@g100", "aaaaaaaaaa@g200", "bbbbbbbbbb"],
      f"a texture on two groups is split per group, one on a single group keeps its key ({labels})")
check(split["aaaaaaaaaa@g200"]["texture"] == "aaaaaaaaaa" and split["aaaaaaaaaa@g200"]["object"] == "g200"
      and split["aaaaaaaaaa@g200"]["group_offset"] == 0x200 and "aaaaaaaaaa" in split, "the split entries name their texture and object")
check(rb.map_dir(pathlib.Path("L"), "aaaaaaaaaa@g200", split["aaaaaaaaaa@g200"]) == pathlib.Path("L/aaaaaaaaaa/@g200")
      and rb.map_dir(pathlib.Path("L"), "bbbbbbbbbb", split["bbbbbbbbbb"]) == pathlib.Path("L/bbbbbbbbbb"),
      "an object's maps go in <key>/@<object>/, a texture's own in <key>/")
with tempfile.TemporaryDirectory() as d2:
    o2 = pathlib.Path(d2)
    (o2 / "gltf" / "tex").mkdir(parents=True)
    pos2 = np.zeros((4, 3, 3), np.float32)
    pos2[:, 1, 0] = 1
    pos2[:, 2, 2] = 1
    rb.write_gltf(o2, pos2, np.zeros((4, 3, 3), np.float32), shared, found, split, labels)
    g2 = json.loads((o2 / "gltf" / "room.gltf").read_text())
    check(sorted(m["name"] for m in g2["materials"]) == ["aaaaaaaaaa@g100", "aaaaaaaaaa@g200", "bbbbbbbbbb"]
          and {i["uri"] for i in g2["images"]} == {"tex/aaaaaaaaaa.png", "tex/bbbbbbbbbb.png"},
          "one primitive and material per (texture, group), sharing the texture's image")
# metal from the texture's colours: grey bands over brown planks are iron; a texture grey all over, or without a band, is not
cfg = rb.config()["metal"]
planks = np.zeros((20, 31, 4), np.uint8)
planks[..., :3] = (139, 98, 65)
planks[..., 3] = 255
planks[:, 8:12, :3] = 65  # two iron bands, 4 texels wide
planks[:, 20:24, :3] = 65
metal, own = rb.classify_metal(planks, cfg)
check(metal[:, 9].min() > 0.8 and metal[:, 0].max() == 0 and metal[:, 14].max() == 0 and abs(own[5, 9] - 0.42) < 1e-6,
      "grey bands over brown planks are iron (metal 0.9, its roughness), the planks are not")
stone = np.full((20, 31, 4), 255, np.uint8)
stone[..., :3] = 90
check(rb.classify_metal(stone, cfg)[0].max() == 0, "a texture that is grey all over is stone, not iron")
speck = planks.copy()
speck[:, 8:12, :3] = (139, 98, 65)
speck[:, 20:24, :3] = (139, 98, 65)
speck[3:5, 9:11, :3] = 65  # (four texels: under min_area)
check(rb.classify_metal(speck, cfg)[0].max() == 0, "one grey texel is a speck, not a piece of metal")
wear = np.zeros(planks.shape[:2], np.float32)
wear[:, 9] = 1.0
cavity = np.zeros_like(wear)
cavity[:, 22] = 1.0
cover = np.ones_like(wear)
rough = np.full(wear.shape, 204, np.uint8)
m8, r8 = rb.metal_maps(planks, wear, cavity, cover, rough, cfg)
check(m8 is not None and m8[5, 9] > m8[5, 10] > 0 and m8[5, 22] < m8[5, 21] and r8[5, 9] < r8[5, 10] < r8[5, 22] < 204 and r8[5, 0] == 204,
      "metal is stronger and smoother at a worn edge, weaker and rougher in a crevice, the planks keep their roughness")
check(rb.metal_maps(stone, wear, cavity, cover, rough, cfg)[0] is None, "a texture with no metal gets no metal map")
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
