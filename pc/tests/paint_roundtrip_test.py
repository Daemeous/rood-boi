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
import subcraft_project as sp  # noqa: E402
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
        pr.import_package(argparse.Namespace(dir=str(source), layer=str(layer), catalog=str(catalog), normal_format="auto", keep_seed=False))
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
    pr.import_package(argparse.Namespace(dir=str(pkg), layer=str(layer), catalog=str(catalog), normal_format="auto", keep_seed=False))
    check(not layer.exists() or not tree(layer), "an unpainted model imports to nothing")
    path = pkg / "tex" / "m4b8870d7_p16_c0005_MetallicRoughness.png"
    image = np.asarray(Image.open(path).convert("RGB")).copy()
    image[10, 18, 2] = 255  # inside the second box, at (2, 2) of it
    pr.save_image(path, image)
    pr.import_package(argparse.Namespace(dir=str(pkg), layer=str(layer), catalog=str(catalog), normal_format="auto", keep_seed=False))
    check(tree(layer) == ["cccccccccc/@m4b8870d7/metal.png"], f"a texel of the atlas is a map of the box it lies in, for the model: {tree(layer)}")
    metal = np.asarray(Image.open(layer / "cccccccccc" / "@m4b8870d7" / "metal.png"))
    check(metal.shape == (6, 8) and int(metal[2, 2]) == 255 and int((metal > 0).sum()) == 1, "...cut to that texture's size, the texel where it was painted")
    entry = json.loads(catalog.read_text())["entries"][0]
    check(entry["object"] == {"models": ["4b8870d7"]} and "scope" not in entry and entry["match"] == {"key": "cccccccccc", "size": [8, 6]},
          "...with a model entry (every copy, any room)")
# a painter's export as SubCraft writes it: <mesh>_<set>_<Map>.png at its own (bigger) resolution, GL or DX normals, separate or packed
with tempfile.TemporaryDirectory() as d:
    import shutil
    d = pathlib.Path(d)
    positions = np.array([[[0, 0, 0], [512, 0, 0], [0, 0, 512]]], np.float32)
    tris = [{"tpage": 17, "clut": 1, "group": 0, "flags": 0, "uv": [[0, 0], [7, 0], [0, 5]], "polygon": 0x108}]
    found = rb.assign(tris, [{"key": "aaaaaaaaaa", "tpage": 17, "clut": 1, "box": [0, 0, 7, 5]}])
    groups = [{"offset": 0x100, "first": 0x108, "end": 0x128}]
    labels, split = rb.split_shared(tris, found, groups, {"aaaaaaaaaa": {"w": 8, "h": 6, "box": [0, 0, 7, 5], "tpage": 17, "clut": 1}}, always=True)
    rgba = np.full((6, 8, 4), 255, np.uint8)
    bake = d / "bake" / "aaaaaaaaaa" / "@g100"
    bake.mkdir(parents=True)
    angle = (np.arange(48).reshape(6, 8) / 48.0) * 0.8  # a tilted, unit normal at every texel
    unit = np.stack([np.sin(angle) * 0.6, np.sin(angle) * -0.3, np.sqrt(1 - (np.sin(angle) * 0.6) ** 2 - (np.sin(angle) * 0.3) ** 2)], axis=2)
    normal = np.clip(np.rint(128 + unit * 127.5), 0, 255).astype(np.uint8)
    Image.fromarray(normal, "RGB").save(bake / "normal.png")
    pkg = d / "pkg"
    pkg.mkdir()
    pr.write_package(pkg, positions, np.full((1, 3, 3), 0.5, np.float32), tris, found, split, labels, {"aaaaaaaaaa": rgba},
                     {"target": "room", "zone": 9, "room": 17}, pr.seed_layers([d / "bake"]))
    SCALE = 8  # SubCraft's texture sets are 2048 by default: far above the game's 8 x 6 here

    def big(array):
        return np.repeat(np.repeat(array, SCALE, axis=0), SCALE, axis=1)

    def seeds(name):
        return np.asarray(Image.open(pkg / "seed" / f"aaaaaaaaaa_g100_{name}.png").convert("RGB"))

    def export(folder, files):
        """The folder a user exports SubCraft's textures into (the package next to them, its glTF and tex/ there as the export left them)."""
        shutil.copytree(pkg, folder)
        for name, array in files.items():
            pr.save_image(folder / name, big(array) if array.shape[:2] == (6, 8) else array)
        return folder

    def run(folder, **options):
        layer = d / f"layer-{folder.name}"
        pr.import_package(argparse.Namespace(dir=str(folder), layer=str(layer), catalog=str(d / f"c-{folder.name}.json"),
                                             **{"normal_format": "auto", "keep_seed": False, **options}))
        return layer
    mr = seeds("MetallicRoughness")
    unchanged = {"paint_aaaaaaaaaa_g100_BaseColor.png": rgba, "paint_aaaaaaaaaa_g100_Roughness.png": mr[..., 1],
                 "paint_aaaaaaaaaa_g100_Metallic.png": mr[..., 2], "paint_aaaaaaaaaa_g100_Normal.png": seeds("Normal"),
                 "paint_aaaaaaaaaa_g100_AO.png": seeds("Occlusion")[..., 0], "paint_aaaaaaaaaa_g100_Height.png": np.zeros((6, 8), np.uint8)}
    layer = run(export(d / "unchanged", unchanged))
    check(not layer.exists() or not tree(layer), "SubCraft's PBR metal/rough export at 8x the size, unchanged, imports to nothing")
    packed = {"paint_aaaaaaaaaa_g100_OcclusionRoughnessMetallic.png": mr, "paint_aaaaaaaaaa_g100_Normal.png": seeds("Normal")}
    check(not run(export(d / "orm", packed)).exists(), "...and so does the glTF / Unreal template's OcclusionRoughnessMetallic pack (a seed with no AO has R at 255)")
    layer = run(export(d / "orm2", {"paint_aaaaaaaaaa_g100_ORM.png": mr}))
    check(not layer.exists(), "...and the Godot ORM pack")

    painted = mr[..., 2].copy()
    painted[2, 3] = 255
    layer = run(export(d / "metal", {"paint_aaaaaaaaaa_g100_Metallic.png": painted, "paint_aaaaaaaaaa_g100_Roughness.png": mr[..., 1]}))
    check(tree(layer) == ["aaaaaaaaaa/@g100/metal.png"], f"a painted Metallic file (mesh prefix and all) is the metal map and nothing else: {tree(layer)}")
    metal = np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g100" / "metal.png"))
    check(metal.shape == (6, 8) and int(metal[2, 3]) == 255 and int((metal > 0).sum()) == 1, "...averaged down to the texture's size, the texel where it was painted")

    # a brush that covers part of a texel is the area average, not a nearest pick
    soft = big(mr[..., 2]).copy()
    soft[16:24, 24:28] = 255  # half of texel (2, 3)
    pr.save_image(d / "soft.png", soft)
    check(int(pr.resample(soft, 6, 8)[2, 3]) in (127, 128), "an area filter: half a texel of paint reads as half")

    dx = pr.flip_green(seeds("Normal"))
    layer = run(export(d / "dx", {"paint_aaaaaaaaaa_g100_NormalDX.png": dx}))
    check(not layer.exists(), "a DirectX normal map named normalDX, unchanged, imports to nothing")
    touched = dx.copy()
    touched[1, 1] = (39, 89, 211)
    layer = run(export(d / "dx2", {"paint_aaaaaaaaaa_g100_NormalDX.png": touched}))
    written = np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g100" / "normal.png").convert("RGB"))
    check(written.shape == (6, 8, 3) and np.abs(written[1, 1].astype(int) - (39, 166, 211)).max() <= 2, f"...a painted one has its green flipped back to OpenGL: {tuple(int(v) for v in written[1, 1])}")
    check(not run(export(d / "dx3", {"paint_aaaaaaaaaa_g100_Normal.png": dx}), normal_format="directx").exists(),
          "--normal-format directx flips a normal map that is named plain Normal")
    check(tree(run(export(d / "dx4", {"paint_aaaaaaaaaa_g100_Normal.png": dx}))) != [], "...while auto reads plain Normal as OpenGL (the flipped one then is a change)")
    # the other way: a small image is repeated up, a base colour edit is not imported
    small = np.asarray(Image.fromarray(painted[1:5, 2:6])).copy()
    check(pr.resample(small, 6, 8).shape == (6, 8), "an image smaller than the texture is repeated up")
    # the exported image beats the older copy in tex/ the package's glTF names; --keep-seed keeps the game's map where nothing was painted
    rough_seed = np.full((6, 8), 90, np.uint8)
    Image.fromarray(rough_seed, "L").save(d / "bake" / "aaaaaaaaaa" / "@g100" / "roughness.png")
    shutil.rmtree(pkg)
    pkg.mkdir()
    pr.write_package(pkg, positions, np.full((1, 3, 3), 0.5, np.float32), tris, found, split, labels, {"aaaaaaaaaa": rgba},
                     {"target": "room", "zone": 9, "room": 17}, pr.seed_layers([d / "bake"]))
    flat = np.full((6, 8), 128, np.uint8)
    flat[0, 0] = 255
    layer = run(export(d / "keep", {"paint_aaaaaaaaaa_g100_Roughness.png": flat}), keep_seed=True)
    rough = np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g100" / "roughness.png"))
    check(int(rough[0, 0]) == 255 and int((rough == 90).sum()) == 47, "--keep-seed: the painter's flat fill is the game's own roughness again, the painted texel stays")
    layer = run(export(d / "nokeep", {"paint_aaaaaaaaaa_g100_Roughness.png": flat}))
    check(int((np.asarray(Image.open(layer / "aaaaaaaaaa" / "@g100" / "roughness.png")) == 128).sum()) == 47, "...and without it the export is taken as it is")

    # the SubCraft project of the package: the game's texture as the bottom layer, nearest, the mesh as SubCraft's blob
    import struct
    import zipfile
    project = sp.build(pkg, d / "paint.subcraft", version="0.4.5")
    with zipfile.ZipFile(project) as z:
        manifest = json.loads(z.read("manifest.json"))
        document = json.loads(z.read("document.json"))
        blob = z.read("mesh.bin")
        check(manifest["format"] == "subcraft-project" and manifest["schema_version"] == 18, "the project is schema 18 of SubCraft's project format")
        (ts,) = document["texture_sets"]
        check(ts["name"] == "aaaaaaaaaa_g100" and ts["material_slot"] == 0 and ts["albedo_filter"] == "nearest" and ts["resolution"] == 128,
              f"one texture set per material, named as it, nearest filtered: {ts['name']} {ts['resolution']}")
        fill = ts["layers"][0]["content"]
        by = {c["channel"]: c["source"]["resource"] for c in fill["channels"]}
        check(fill["type"] == "fill" and fill["projection"]["mode"] == "uv" and sorted(by) == sorted(["base_color", "roughness", "metallic", "normal", "ambient_occlusion"]),
              "a UV-projected fill layer of the seed's five maps")
        files = {r["id"]: r["file"] for r in document["resources"]}
        got = np.asarray(Image.open(__import__("io").BytesIO(z.read(files[by["normal"]]))).convert("RGB"))
        check(np.array_equal(got, seeds("Normal")), "...the normal resource is the seed's, texel for texel")
        rough = np.asarray(Image.open(__import__("io").BytesIO(z.read(files[by["roughness"]]))))
        check(np.array_equal(rough, seeds("MetallicRoughness")[..., 1]), "...and the roughness is the seed's green")
    check(blob[:6] == b"SCMESH" and struct.unpack("<H", blob[6:8])[0] == 2, "mesh.bin is a version 2 SubCraft mesh blob")
    offset = 8
    for _ in range(1):  # the name, then the material names
        (n,) = struct.unpack("<I", blob[offset:offset + 4])
        offset += 4 + n
    (materials,) = struct.unpack("<I", blob[offset:offset + 4])
    check(materials == 1, "...with the package's one material")
    try:
        sp.build(pkg, d / "never.subcraft", version="9.9.9")
        check(False, "a SubCraft version this writer does not know is refused")
    except sp.FormatMismatch as e:
        check("9.9.9" in str(e) and not (d / "never.subcraft").exists(), "a SubCraft version this writer does not know is refused loudly, nothing written")
# the surface a first import keeps: what the game drew the texture with (its entry, presets resolved), else the standard no-entry one
with tempfile.TemporaryDirectory() as tmp:
    shipped, user = pathlib.Path(tmp) / "shipped.json", pathlib.Path(tmp) / "user.json"
    shipped.write_text(json.dumps({"presets": {"stone": {"roughness": 0.7, "specular": 1, "metal": 0, "auto_height": True}},
                                   "entries": [{"id": "A", "match": {"key": "aaaaaaaaaa", "size": [4, 4]}, "preset": "stone", "roughness": 0.5},
                                               {"id": "B", "match": {"key": "bbbbbbbbbb", "size": [4, 4]}, "preset": "stone", "scope": {"room": [1, 2]}}]}))
    user.write_text(json.dumps({"entries": [{"id": "A", "match": {"key": "aaaaaaaaaa", "size": [4, 4]}, "preset": "stone", "metal": 0.25}]}))
    files = [shipped, user]
    values, _ = pr.surface_of(files, {"object": "g10", "entry": "A"}, "aaaaaaaaaa", 4, 4)
    check(values == {"roughness": 0.7, "specular": 1, "metal": 0.25, "auto_height": True, "bump": 1.0}, f"an entry the game matched, its user copy over the shipped, preset resolved: {values}")
    values, _ = pr.surface_of(files, {"object": "g10", "entry": ""}, "cccccccccc", 4, 4)
    check(values == pr.STANDARD and pr.uniform(values, "roughness") == 255, "a texture with no entry is the game's standard surface (rough 1, no specular, no metal)")
    values, _ = pr.surface_of(files, {"object": "m4b8870d7", "model": "4b8870d7"}, "bbbbbbbbbb", 4, 4, 1, 2)
    check(values == pr.STANDARD, "(an entry the game did not match for it is not assumed)")
    shipped.write_text(json.dumps({"entries": [{"id": "O", "match": {"key": "dddddddddd", "size": [4, 4]}, "roughness": 0.3, "object": {"models": ["4b8870d7"]}}]}))
    values, _ = pr.surface_of(files, {"object": "m4b8870d7", "model": "4b8870d7"}, "dddddddddd", 4, 4)
    check(values["roughness"] == 0.3, "an entry made for the model wins")
print("FAIL" if failures else "PASS")
sys.exit(1 if failures else 0)
