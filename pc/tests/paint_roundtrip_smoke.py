#!/usr/bin/env python3
"""Painting the maps in a painter and bringing them back (pc/tools/paint_roundtrip.py; pc/docs/Materials.md "Painting the maps in a
painter"; Gitea #20).

    python3 pc/tests/paint_roundtrip_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Exports the save room as a glTF paint package (every object owns its texels: one material per room group and texture; a model's
page is one atlas), then:

- export: the glTF has a PBR metallic-roughness material per object with the game texture as its base colour (nearest sampling) and
  metallicRoughness, normal and occlusion images; a texture shared by room groups has an image set for each group.
- no loss: importing the package unpainted writes no map and no catalog entry; the game with the user layer (empty) draws the same
  material picture.
- one texel: a single texel painted metal in one object's image lands as <layer>/<key>/@<object>/metal.png with that texel set (and
  nothing else: no roughness, no other folder), with a user catalog entry for the object.
- live: with the game running, painting a whole object (metal, smooth) and importing changes the picture without any reload; the
  maps and the catalog entry came through the files the game watches.
- formats: the maps are found by name when the painter wrote files beside the glTF (Roughness, Metallic, ORM) and are read at a
  whole multiple of the texture's size.
"""
import argparse
import json
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import editor_selection_smoke as es  # noqa: E402
import paint_roundtrip as pr  # noqa: E402
from material_instances_smoke import frame  # noqa: E402
from rood_rpc import RpcError  # noqa: E402


def load(path, mode="RGB"):
    return np.asarray(Image.open(path).convert(mode))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else es.OUT / "paint_roundtrip"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    pkg, layer, catalog = out / "pkg", out / "layer", out / "layer" / "materials.json"
    layer.mkdir()
    maps = layer / "maps"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    # the export (its own game: the checkpoint the room lab uses)
    pr.export_package(argparse.Namespace(target="room", zone=None, room=None, model=None, model_at=None, bake=[], out=str(pkg), card=args.card))
    seed = json.loads((pkg / "seed.json").read_text())
    gltf = json.loads((pkg / "paint.gltf").read_text())
    names = {m["name"] for m in gltf["materials"]}
    check(all(pr.safe(label) in names for label in seed["labels"]) and len(seed["labels"]) > 10,
          f"a material for each object and texture ({len(seed['labels'])} of {len(gltf['materials'])})")
    m = gltf["materials"][0]
    check({"baseColorTexture", "metallicRoughnessTexture"} <= set(m["pbrMetallicRoughness"]) and "normalTexture" in m and "occlusionTexture" in m,
          f"PBR metallic-roughness with base colour, metallicRoughness, normal and occlusion ({m['name']})")
    check(gltf["samplers"][0]["magFilter"] == 9728 and gltf["samplers"][0]["minFilter"] == 9728, "nearest sampling")
    by_key = {}
    for label, rec in seed["labels"].items():
        by_key.setdefault(rec["key"], []).append(label)
    shared = [labels for labels in by_key.values() if len(labels) > 1]
    check(bool(shared), f"a texture used by several room groups has an image set for each ({len(shared)} such textures)")
    rec0 = next(iter(seed["labels"].values()))
    base = load(pkg / rec0["files"]["basecolor"], "RGBA")
    check(base.shape[:2] == (rec0["h"], rec0["w"]), "the base colour is the game's texture texel for texel")

    # no loss: nothing painted, nothing written
    ns = argparse.Namespace(dir=str(pkg), layer=str(maps), catalog=str(catalog), preset="stone")
    pr.import_package(ns)
    check(not list(maps.rglob("*.png")) and not catalog.exists(), "an export imported without painting writes no map and no catalog entry")

    env = {"ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"), "ROOD_MATERIAL_CATALOG_USER": str(catalog),
           "ROOD_MATERIALS": str(maps), "ROOD_MATERIALS_SHIPPED": str(out / "shipped"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_GI": "0"}
    gargs = argparse.Namespace(card=args.card, out=str(out / "game"))
    s = es.start(gargs, "paint_roundtrip", extra_env=env)
    if s is None:
        return 77
    try:
        g, p = s.game, s.player
        feet = s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]
        g.call("editor_inspect", action="pick", x=feet[0], y=feet[1])
        mp = g.call("material", action="pick", x=feet[0], y=feet[1])
        label = f"{mp['key']}@{mp['object']['text']}"
        check(label in seed["labels"], f"the object under Ashley's feet is in the package ({label})")
        g.call("editor_ui", visible=False)
        g.call("graphics", interpolate=False, specular=1, view_mode=6)

        def shot(name):
            return frame(g, out / f"{name}.png")
        base_frame = shot("base")
        quiet = ~(np.abs(base_frame - shot("base2")).max(2) > 0)
        check(not (np.abs(base_frame - shot("base3")).max(2) > 0)[quiet].any(), "the material view is steady")

        # one texel painted metal lands on that object only
        one = out / "pkg-one"
        shutil.copytree(pkg, one)
        rec = seed["labels"][label]
        path = one / rec["files"]["metallicroughness"]
        mr = load(path).copy()
        mr[2, 3, 2] = 255
        pr.save_image(path, mr)
        one_layer, one_catalog = out / "one-layer", out / "one-catalog.json"
        pr.import_package(argparse.Namespace(dir=str(one), layer=str(one_layer), catalog=str(one_catalog), preset="stone"))
        folders = sorted(str(f.relative_to(one_layer)) for f in one_layer.rglob("*.png"))
        obj = rec["object"]
        check(folders == [f"{rec['key']}/@{obj}/metal.png"], f"the one texel is one map of that object: {folders}")
        metal = load(one_layer / rec["key"] / f"@{obj}" / "metal.png", "L")
        check(metal.shape == (rec["h"], rec["w"]) and int(metal[2, 3]) == 255 and int((metal > 0).sum()) == 1, "...with that texel set and no other")
        entries = json.loads(one_catalog.read_text())["entries"]
        check(len(entries) == 1 and entries[0]["object"] == {"groups": [rec["group_offset"]]} and entries[0]["scope"] == {"room": [9, 17]},
              f"...and a catalog entry for that object only ({entries[0]['id']})")

        # live: paint the whole object while the game runs
        everything = out / "pkg-all"
        shutil.copytree(pkg, everything)
        path = everything / rec["files"]["metallicroughness"]
        mr = load(path).copy()
        mr[..., 1], mr[..., 2] = 30, 255
        pr.save_image(path, mr)
        pr.import_package(argparse.Namespace(dir=str(everything), layer=str(maps), catalog=str(catalog), preset="stone"))
        for _ in range(8):  # (the watch looks at the files a few times a second; a paused game redraws)
            g.call("redraw")
            g.wait_presents(6)
            painted = shot("painted")
            if (np.abs(painted - base_frame).max(2) > 0)[quiet].any():
                break
        changed = int((np.abs(painted - base_frame).max(2) > 0)[quiet].sum())
        check(changed > 50, f"painting the object changes the picture with the game running, no reload ({changed} pixels)")
        mp2 = g.call("material", action="pick", x=feet[0], y=feet[1])
        check(mp2.get("object_entry", "").startswith("PT-"), f"...through the catalog entry the import made ({mp2.get('object_entry')!r})")

        # normals: unpainted ones leave the relief as it is; a painted normal shows (the shading normals view)
        g.call("graphics", view_mode=9)
        g.wait_presents(4)
        relief_before = shot("relief-before")
        again = out / "pkg-normal"
        shutil.copytree(pkg, again)
        pr.import_package(argparse.Namespace(dir=str(again), layer=str(maps), catalog=str(catalog), preset="stone"))
        g.call("redraw")
        g.wait_presents(6)
        check(not (np.abs(shot("relief-unpainted") - relief_before).max(2) > 0)[quiet].any(), "importing the unpainted normals changes no shading normal")
        npath = again / rec["files"]["normal"]
        tilted = load(npath).copy()
        tilted[...] = (230, 128, 200)  # a strong lean along x
        pr.save_image(npath, tilted)
        pr.import_package(argparse.Namespace(dir=str(again), layer=str(maps), catalog=str(catalog), preset="stone"))
        for _ in range(8):
            g.call("redraw")
            g.wait_presents(6)
            relief_after = shot("relief-painted")
            if (np.abs(relief_after - relief_before).max(2) > 0)[quiet].any():
                break
        leaned = int((np.abs(relief_after - relief_before).max(2) > 0)[quiet].sum())
        check(leaned > 50, f"a painted normal changes the shading normals of that object ({leaned} pixels)")
        # the strength a painted normal is read with is data (normal.json beside normal_x.png): a stronger one leans the shading normals further
        folder = maps / rec["key"] / f"@{obj}"
        check(json.loads((folder / "normal.json").read_text())["strength"] == json.loads((ROOT / "pc/tools/room_blender.json").read_text())["maps"]["normal_strength"],
              "the strength recorded with the painted normal is the bake's")
        (folder / "normal.json").write_text(json.dumps({"strength": 0.5}))
        # (the file itself is watched: the edit applies on its own)
        for _ in range(8):
            g.call("redraw")
            g.wait_presents(6)
            weaker = shot("relief-weaker")
            if (np.abs(weaker - relief_after).max(2) > 0)[quiet].any():
                break
        check((np.abs(weaker - relief_after).max(2) > 0)[quiet].sum() > 50, "a different recorded strength changes the relief the same painted normal gives")
        g.call("graphics", view_mode=6)

        # a model: Ashley's atlas, one texel painted metal changes her pixels only
        mpkg = out / "pkg-model"
        pr.export_package(argparse.Namespace(target="model", zone=None, room=None, model="4b8870d7", model_at=None, bake=[], out=str(mpkg), card=args.card))
        mseed = json.loads((mpkg / "seed.json").read_text())
        check(len(mseed["labels"]) == 1 and all(r["slices"] for r in mseed["labels"].values()), f"a model's page is one atlas ({len(mseed['labels'])} material)")
        mlabel, mrec = next(iter(mseed["labels"].items()))
        body = None
        for up in (40, 80, 120, 160, 200, 260):  # (up the body from the feet: y is down)
            at = s.select(action="project", point=[p["x"], p["y"] - up, p["z"]])["pixel"]
            if g.call("editor_inspect", action="pick", x=at[0], y=at[1]).get("kind") == "model":
                body = at
                break
        check(body is not None, "Ashley is under a pixel of the view")
        if body is None:
            raise RpcError("no pixel on Ashley")
        hit = g.call("material", action="pick", x=body[0], y=body[1])
        box = next((sl["box"] for sl in mrec["slices"] if sl["key"] == hit["key"]), None)
        check(box is not None and hit["object"]["text"] == mrec["object"], f"the pixel on her is a box of the atlas ({hit['key']}, {hit['object']['text']})")
        g.call("graphics", view_mode=6)
        g.wait_presents(4)
        before = shot("model-before")
        quiet2 = ~(np.abs(before - shot("model-before2")).max(2) > 0)
        mpath = mpkg / mrec["files"]["metallicroughness"]
        atlas = load(mpath).copy()
        row, col = box[1] + hit["texel"][1], box[0] + hit["texel"][0]
        atlas[row, col, 2] = 255  # one texel metal
        pr.save_image(mpath, atlas)
        pr.import_package(argparse.Namespace(dir=str(mpkg), layer=str(maps), catalog=str(catalog), preset="stone"))
        folders = sorted(str(f.relative_to(maps)) for f in maps.rglob("metal.png") if "@m4b8870d7" in str(f))
        check(folders == [f"{hit['key']}/@m4b8870d7/metal.png"], f"the one texel is one map of her box for her model: {folders}")
        one = None
        for _ in range(8):
            g.call("redraw")
            g.wait_presents(6)
            one = (np.abs(shot("model-one") - before).max(2) > 0) & quiet2
            if one.any():
                break
        check(one.any(), f"the texel changes her picture with the game running ({int(one.sum())} pixels)")
        atlas[..., 2] = 255  # the whole page: where she is, for comparison
        pr.save_image(mpath, atlas)
        pr.import_package(argparse.Namespace(dir=str(mpkg), layer=str(maps), catalog=str(catalog), preset="stone"))
        for _ in range(8):
            g.call("redraw")
            g.wait_presents(6)
            whole = (np.abs(shot("model-all") - before).max(2) > 0) & quiet2
            if whole.sum() > one.sum():
                break
        ys, xs = np.nonzero(whole)
        check(bool(whole.any()) and not (one & ~whole).any(), "the texel's pixels lie among those of her whole page")
        check(whole.any() and xs.min() <= body[0] <= xs.max() and ys.min() <= body[1] <= ys.max() and whole.mean() < 0.1,
              f"her pixels are the ones that change, a small part of the frame around her ({whole.mean() * 100:.1f}%: x {xs.min()}..{xs.max()}, y {ys.min()}..{ys.max()})")
        g.call("graphics", view_mode=6)

        # formats: the maps by name beside the glTF, at twice the size
        named = out / "pkg-named"
        shutil.copytree(pkg, named)
        (named / "paint.gltf").unlink()  # (no glTF: the painter wrote only images)
        stem = pr.safe(label)
        src = load(named / rec["files"]["metallicroughness"])
        rough = np.repeat(np.repeat(np.full(src.shape[:2], 77, np.uint8), 2, 0), 2, 1)
        Image.fromarray(rough, "L").save(named / "tex" / f"{stem}_Roughness.png")
        named_layer = out / "named-layer"
        pr.import_package(argparse.Namespace(dir=str(named), layer=str(named_layer), catalog=str(out / "named.json"), preset="stone"))
        got = named_layer / rec["key"] / f"@{obj}" / "roughness.png"
        check(got.exists() and int(load(got, "L").min()) == 77 and load(got, "L").shape == (rec["h"], rec["w"]),
              "maps found by name beside the files (Roughness.png) and read from twice the texture's size")
    except RpcError as e:
        check(False, f"RPC error {e}")
    finally:
        es.finish(s, "paint round trip")
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: the paint round trip: glTF per object, nothing lost unpainted, one texel on one object, live in the game")
    return 0


if __name__ == "__main__":
    sys.exit(main())
