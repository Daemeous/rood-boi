#!/usr/bin/env python3
"""pc/tools/paint_roundtrip.py without the game (Gitea #20): what the seed holds, what an import writes, what it never touches.

    python3 pc/tests/paint_roundtrip_test.py"""
import argparse
import json
import pathlib
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
import paint_roundtrip as pr  # noqa: E402
import room_blender as rb  # noqa: E402

failures = 0


def check(ok, message):
    global failures
    print(("ok:   " if ok else "FAIL: ") + message)
    failures += not ok


def tree(root):
    return sorted(str(p.relative_to(root)) for p in pathlib.Path(root).rglob("*") if p.is_file())


def digests(root):
    return {str(p.relative_to(root)): p.read_bytes() for p in pathlib.Path(root).rglob("*") if p.is_file()}


with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    # two triangles of one 8 x 6 texture on two room groups: each group owns its texels
    positions = np.array([[[0, 0, 0], [512, 0, 0], [0, 0, 512]], [[512, 0, 0], [512, 0, 512], [0, 0, 512]]], np.float32)
    colours = np.full((2, 3, 3), 0.5, np.float32)
    tris = [{"tpage": 17, "clut": 1, "group": 0, "flags": 0, "uv": [[0, 0], [7, 0], [0, 5]], "polygon": 0x108},
            {"tpage": 17, "clut": 1, "group": 1, "flags": 0, "uv": [[7, 0], [7, 5], [0, 5]], "polygon": 0x208}]
    uses = [{"key": "aaaaaaaaaa", "tpage": 17, "clut": 1, "box": [0, 0, 7, 5]}]
    found = rb.assign(tris, uses)
    groups = [{"offset": 0x100, "first": 0x108, "end": 0x128}, {"offset": 0x200, "first": 0x208, "end": 0x228}]
    textures = {"aaaaaaaaaa": {"w": 8, "h": 6, "box": [0, 0, 7, 5], "tpage": 17, "clut": 1}}
    labels, split = rb.split_shared(tris, found, groups, textures, always=True)
    rgba = np.zeros((6, 8, 4), np.uint8)
    rgba[..., :3] = (139, 98, 65)
    rgba[..., 3] = 255
    # the geometry bake the game shows: normals (not flat) and relief for group 0x100's texture, a roughness for the other
    bake, user = d / "bake", d / "user"
    obj_a = bake / "aaaaaaaaaa" / "@g100"
    obj_a.mkdir(parents=True)
    normal = np.zeros((6, 8, 3), np.uint8)
    normal[..., 0] = np.arange(8, dtype=np.uint8)[None, :] * 9 + 40
    normal[..., 1] = np.arange(6, dtype=np.uint8)[:, None] * 11 + 60
    normal[..., 2] = 230
    Image.fromarray(normal, "RGB").save(obj_a / "normal.png")
    height = (np.arange(48, dtype=np.uint8).reshape(6, 8) * 3).astype(np.uint8)
    Image.fromarray(height, "L").save(obj_a / "height.png")
    rough = np.full((6, 8), 120, np.uint8)
    Image.fromarray(rough, "L").save(bake / "aaaaaaaaaa" / "@g200" / "roughness.png") if (bake / "aaaaaaaaaa" / "@g200").mkdir() is None else None
    pkg = d / "pkg"
    pkg.mkdir()
    records = pr.write_package(pkg, positions, colours, tris, found, split, labels, {"aaaaaaaaaa": rgba},
                               {"target": "room", "zone": 9, "room": 17}, pr.seed_layers([bake]))
    check(sorted(records) == ["aaaaaaaaaa@g100", "aaaaaaaaaa@g200"], "each group owns an image set")
    seed_normal = np.asarray(Image.open(pkg / "tex" / "aaaaaaaaaa_g100_Normal.png").convert("RGB"))
    check(np.array_equal(seed_normal, normal), "the seed normal map is the geometry bake's, texel for texel")
    other = np.asarray(Image.open(pkg / "tex" / "aaaaaaaaaa_g200_Normal.png").convert("RGB"))
    check((other == (128, 128, 255)).all(), "an object with no bake starts flat (128, 128, 255)")
    mr = np.asarray(Image.open(pkg / "tex" / "aaaaaaaaaa_g200_MetallicRoughness.png").convert("RGB"))
    check((mr[..., 1] == 120).all() and (mr[..., 2] == 0).all(), "the seed roughness is the bake's, metal empty")
    g = json.loads((pkg / "paint.gltf").read_text())
    check(len(g["materials"]) == 2 and all("normalTexture" in m and "occlusionTexture" in m for m in g["materials"]), "glTF: a PBR material per object with normal and occlusion")
    before = digests(bake)

    def run_import(source):
        layer = d / f"layer-{source.name}"
        catalog = d / f"catalog-{source.name}.json"
        pr.import_package(argparse.Namespace(dir=str(source), layer=str(layer), catalog=str(catalog), preset="stone"))
        return layer, catalog
    # no loss: unpainted, nothing written; the bake is untouched
    layer, catalog = run_import(pkg)
    check(not layer.exists() or not tree(layer), "an unpainted export imports to nothing: the normals, relief and roughness stay the bake's")
    check(not catalog.exists(), "...and no catalog entry")
    check(digests(bake) == before, "...and the bake's files are as they were")

    # metal painted: normals are not written, the relief (height) is never written
    one = d / "one"
    import shutil
    shutil.copytree(pkg, one)
    path = one / "tex" / "aaaaaaaaaa_g100_MetallicRoughness.png"
    image = np.asarray(Image.open(path).convert("RGB")).copy()
    image[2, 3, 2] = 255
    pr.save_image(path, image)
    layer, catalog = run_import(one)
    check(tree(layer) == ["aaaaaaaaaa/@g100/metal.png"], f"one texel of metal is one map of one object, no normal and no relief written: {tree(layer)}")
    metal = np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g100" / "metal.png"))
    check(int(metal[2, 3]) == 255 and int((metal > 0).sum()) == 1, "...with that texel set")
    entry = json.loads(catalog.read_text())["entries"][0]
    check(entry["object"] == {"groups": [0x100]} and entry["scope"] == {"room": [9, 17]} and entry["match"]["key"] == "aaaaaaaaaa", "...and an entry for that object in that room")

    # a normal painted over: written, the rest of it the bake's
    two = d / "two"
    shutil.copytree(pkg, two)
    path = two / "tex" / "aaaaaaaaaa_g100_Normal.png"
    image = np.asarray(Image.open(path).convert("RGB")).copy()
    image[1, 1] = (10, 20, 250)
    pr.save_image(path, image)
    layer, catalog = run_import(two)
    check(tree(layer) == ["aaaaaaaaaa/@g100/normal.json", "aaaaaaaaaa/@g100/normal.png", "aaaaaaaaaa/@g100/normal_x.png", "aaaaaaaaaa/@g100/normal_y.png"],
          f"a painted normal is the only map written (as the normal, the two 8-bit parts and the strength the game reads): {tree(layer)}")
    check(json.loads((layer / "aaaaaaaaaa" / "@g100" / "normal.json").read_text())["strength"] == rb.config()["maps"]["normal_strength"],
          "...the strength is the bake's (room_blender.json), recorded beside the parts")
    nx = np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g100" / "normal_x.png"))
    check(int(nx[1, 1]) == 10 and np.array_equal(nx[0], normal[0, :, 0]), "...the parts are the normal's x and y")
    written = np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g100" / "normal.png").convert("RGB"))
    check(tuple(written[1, 1]) == (10, 20, 250) and int((written != normal).any(2).sum()) == 1, "...the bake's normals everywhere else, the painted texel as painted")
    check(digests(bake) == before, "...and the bake itself is still untouched")

    # glTF re-saved by a painter under other file names, and packed ORM
    three = d / "three"
    shutil.copytree(pkg, three)
    renamed = three / "painted" / "wall_ORM.png"
    renamed.parent.mkdir()
    packed = np.asarray(Image.open(three / "tex" / "aaaaaaaaaa_g200_MetallicRoughness.png").convert("RGB")).copy()
    packed[..., 1] = 33
    pr.save_image(renamed, packed)
    g = json.loads((three / "paint.gltf").read_text())
    material = next(m for m in g["materials"] if m["name"] == "aaaaaaaaaa_g200")
    g["images"][g["textures"][material["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"]]["source"]]["uri"] = "painted/wall_ORM.png"
    (three / "paint.gltf").write_text(json.dumps(g))
    layer, catalog = run_import(three)
    check(tree(layer) == ["aaaaaaaaaa/@g200/roughness.png"] and int(np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g200" / "roughness.png")).min()) == 33,
          "the glTF a painter re-saved (its own image names) is read by what the glTF says")
# a model's page is one atlas: a texel painted in it lands in the box (the texture of the game) it belongs to, for the model
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d)
    boxes = [{"key": "bbbbbbbbbb", "box": [0, 0, 7, 5], "rgba": np.full((6, 8, 4), 200, np.uint8)},
             {"key": "cccccccccc", "box": [16, 8, 23, 13], "rgba": np.full((6, 8, 4), 90, np.uint8)}]
    positions = np.zeros((1, 3, 3), np.float32)
    positions[0, 1, 0] = positions[0, 2, 2] = 100
    tris = [{"tpage": 16, "clut": 5, "group": 0, "flags": 0, "uv": [[0, 0], [7, 0], [16, 8]], "polygon": 0}]
    textures = {"m4b8870d7_p16_c0005": {"w": 256, "h": 256, "box": [0, 0, 255, 255], "object": "m4b8870d7", "model": "4b8870d7", "atlas": boxes}}
    pkg = d / "pkg"
    pkg.mkdir()
    pr.write_package(pkg, positions, np.full((1, 3, 3), 0.5, np.float32), tris, [{"key": "bbbbbbbbbb"}], textures, ["m4b8870d7_p16_c0005"],
                     {}, {"target": "model", "model": "4b8870d7"}, [d / "none"])
    base = np.asarray(Image.open(pkg / "tex" / "m4b8870d7_p16_c0005_BaseColor.png"))
    check(base.shape == (256, 256, 4) and base[0, 0, 0] == 200 and base[8, 16, 0] == 90 and base[100, 100, 3] == 0, "a model's page is one atlas of its boxes, empty elsewhere")
    layer, catalog = d / "layer", d / "catalog.json"
    pr.import_package(argparse.Namespace(dir=str(pkg), layer=str(layer), catalog=str(catalog), preset="stone"))
    check(not layer.exists() or not tree(layer), "an unpainted model imports to nothing")
    path = pkg / "tex" / "m4b8870d7_p16_c0005_MetallicRoughness.png"
    image = np.asarray(Image.open(path).convert("RGB")).copy()
    image[10, 18, 2] = 255  # inside the second box, at (2, 2) of it
    pr.save_image(path, image)
    pr.import_package(argparse.Namespace(dir=str(pkg), layer=str(layer), catalog=str(catalog), preset="stone"))
    check(tree(layer) == ["cccccccccc/@m4b8870d7/metal.png"], f"a texel of the atlas is a map of the box it lies in, for the model: {tree(layer)}")
    metal = np.asarray(Image.open(layer / "cccccccccc" / "@m4b8870d7" / "metal.png"))
    check(metal.shape == (6, 8) and int(metal[2, 2]) == 255 and int((metal > 0).sum()) == 1, "...cut to that texture's size, the texel where it was painted")
    entry = json.loads(catalog.read_text())["entries"][0]
    check(entry["object"] == {"models": ["4b8870d7"]} and "scope" not in entry and entry["match"] == {"key": "cccccccccc", "size": [8, 6]},
          "...with a model entry (every copy, any room)")
print("FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
