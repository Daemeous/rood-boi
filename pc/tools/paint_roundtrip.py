#!/usr/bin/env python3
"""Painting the game's textures in a real painting program and bringing the maps back (Gitea #20; pc/docs/Materials.md, "Painting
the maps in a painter").

    python3 pc/tools/paint_roundtrip.py export room  [--zone Z --room R] [--out DIR]
    python3 pc/tools/paint_roundtrip.py export model (--model HASH | --model-at X,Y) [--out DIR]
    python3 pc/tools/paint_roundtrip.py import DIR   [--layer DIR] [--catalog FILE] [--preset NAME]

export  A glTF 2.0 file (paint.gltf) of the room the game is in (or of one model: a character, weapon or prop, as it stands in the
        newest frame) with the PBR metallic-roughness material: one material per object and texture (a texture used by more than
        one room group is a material of its own for each group, so each object owns its texels), its base colour the game's texture
        texel for texel (nearest sampling), and the metallicRoughness (G roughness, B metal), normal and occlusion images seeded from
        the maps the game has now (or empty): the user layer's, the shipped, the bake's (--bake, or $ROOD_MATERIALS_BAKED), so the geometry-based normals the game shows start the painter's normal map. The folder holds paint.gltf, paint.bin, tex/ (the images a painter edits), seed/
        (pristine copies, never edit) and seed.json. It goes to a user folder (default pc/user/paint/<name>): never to git.
import  Reads what the painter saved: the glTF with its images, or the images it wrote by name next to it (<material>_<Map>.png:
        BaseColor, Roughness, Metallic, Normal, AO or OcclusionRoughnessMetallic / ORM / MetallicRoughness, any case), and writes the
        channels that differ from the seed as the user material layer's maps of that object (<layer>/<key>/@<object>/roughness.png,
        metal.png, normal.png, occlusion.png), with a user catalog entry for each painted object so the maps apply. Unpainted
        channels write nothing: an export imported without painting changes nothing.
Only the game's own files are read and written; the folder it exports is game art and stays on the machine."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import sys
import urllib.parse

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import room_blender as rb  # noqa: E402

CHANNELS = ("basecolor", "metallicroughness", "normal", "occlusion")
NAMES = {"basecolor": "BaseColor", "metallicroughness": "MetallicRoughness", "normal": "Normal", "occlusion": "Occlusion"}
# the names painters give a map (lower case, without the material's name and the separator)
ALIASES = {
    "basecolor": ("basecolor", "base_color", "albedo", "diffuse", "color"),
    "metallicroughness": ("metallicroughness", "occlusionroughnessmetallic", "orm", "metallic_roughness", "mr", "rma"),
    "roughness": ("roughness", "rough"),
    "metal": ("metallic", "metalness", "metal"),
    "normal": ("normal", "normalopengl", "normaldirectx", "nrm"),
    "occlusion": ("occlusion", "ambientocclusion", "ambient_occlusion", "ao"),
}
DEFAULT_ROUGHNESS = 204  # (0.8: what an unpainted texture reads as)


def safe(label):
    return label.replace("@", "_")


def user_layer():
    return pathlib.Path(os.environ.get("ROOD_MATERIALS") or (pathlib.Path(os.environ.get("ROOD_LIGHTS") or ROOT / "pc/user/lighting") / "materials"))


def user_catalog():
    return pathlib.Path(os.environ.get("ROOD_MATERIAL_CATALOG_USER") or (pathlib.Path(os.environ.get("ROOD_LIGHTS") or ROOT / "pc/user/lighting") / "materials.json"))


def seed_layers(extra=()):
    """The folders the game reads maps from, the user's first (as materials.cpp does); `extra` (a bake: room_blender.py's
    <out>/materials, the geometry bake whose normals and relief the game shows) after the user's."""
    out = [user_layer()] + [pathlib.Path(e) for e in extra]
    out.append(pathlib.Path(os.environ.get("ROOD_MATERIALS_SHIPPED") or ROOT / "pc/assets/materials"))
    baked = os.environ.get("ROOD_MATERIALS_BAKED")
    out.append(pathlib.Path(baked) if baked else ROOT / "pc/build/materials")
    return out


def read_map(layers, key, obj, name):
    """The map `name` of texture `key` (for object `obj`: its own folder first), as an 8-bit array, or None."""
    for layer in layers:
        for folder in ((layer / key / f"@{obj}",) if obj else ()) + (layer / key,):
            path = folder / f"{name}.png"
            if path.exists():
                return np.asarray(Image.open(path).convert("L" if name != "normal" else "RGB"))
    return None


def seed_images(layers, key, obj, rgba):
    """The four images an export starts a painter with: the game's texture and the maps the game has (or neutral ones)."""
    h, w = rgba.shape[:2]
    rough = read_map(layers, key, obj, "roughness")
    metal = read_map(layers, key, obj, "metal")
    mr = np.zeros((h, w, 3), np.uint8)
    mr[..., 0] = 255
    mr[..., 1] = rough if rough is not None and rough.shape == (h, w) else DEFAULT_ROUGHNESS
    mr[..., 2] = metal if metal is not None and metal.shape == (h, w) else 0
    normal = read_map(layers, key, obj, "normal")
    if normal is None or normal.shape[:2] != (h, w):
        normal = np.zeros((h, w, 3), np.uint8)
        normal[...] = (128, 128, 255)
    occlusion = read_map(layers, key, obj, "occlusion")
    if occlusion is None or occlusion.shape != (h, w):
        occlusion = np.full((h, w), 255, np.uint8)
    return {"basecolor": rgba, "metallicroughness": mr, "normal": normal, "occlusion": occlusion}


def atlas_images(layers, obj, slices, w, h):
    """A model's page as one set of images: each box of it (a use: slices of {key, box, rgba}) pasted where it lies in the page, the
    rest empty (transparent colour, neutral maps), so a painter has one coherent atlas to paint on."""
    out = {"basecolor": np.zeros((h, w, 4), np.uint8), "metallicroughness": np.zeros((h, w, 3), np.uint8),
           "normal": np.zeros((h, w, 3), np.uint8), "occlusion": np.full((h, w), 255, np.uint8)}
    out["metallicroughness"][...] = (255, DEFAULT_ROUGHNESS, 0)
    out["normal"][...] = (128, 128, 255)
    for sl in slices:
        u0, v0, u1, v1 = sl["box"]
        part = seed_images(layers, sl["key"], obj, sl["rgba"])
        for channel in CHANNELS:
            out[channel][v0:v1 + 1, u0:u1 + 1] = part[channel]
    return out


def save_image(path, array):
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = "RGBA" if array.ndim == 3 and array.shape[2] == 4 else "RGB" if array.ndim == 3 else "L"
    Image.fromarray(array, mode).save(path)


def write_package(out, positions, colours, tris, found, textures, labels, rgba_of, meta, layers):
    """paint.gltf, tex/, seed/ and seed.json in `out`. textures: {label: {w, h, box, key?, object?, ...}}, rgba_of: {key: RGBA array}."""
    shutil.rmtree(out / "tex", ignore_errors=True)
    shutil.rmtree(out / "seed", ignore_errors=True)
    pbr, records = {}, {}
    for label in sorted(set(labels)):
        t = textures.get(label)
        if not t:
            continue
        key = t.get("texture", label)
        obj = t.get("object", "")
        atlas = t.get("atlas")
        images = atlas_images(layers, obj, atlas, t["w"], t["h"]) if atlas else seed_images(layers, key, obj, rgba_of[key])
        files = {"name": safe(label)}
        for channel in CHANNELS:
            name = f"{safe(label)}_{NAMES[channel]}.png"
            save_image(out / "tex" / name, images[channel])
            save_image(out / "seed" / name, images[channel])
            files[channel] = f"tex/{name}"
        pbr[label] = files
        records[label] = {"key": key, "object": obj, "w": t["w"], "h": t["h"], "group_offset": t.get("group_offset"),
                          "model": t.get("model"), "files": {c: files[c] for c in CHANNELS},
                          **({"slices": [{k: v for k, v in sl.items() if k != "rgba"} for sl in atlas]} if atlas else {})}
    rb.write_gltf(out, positions, colours, tris, found, textures, labels, pbr=pbr, folder=".", name="paint")
    (out / "seed.json").write_text(json.dumps({"version": 1, **meta, "labels": records}, indent=1))
    return records


# ---------------------------------------------------------------------------------------------- export

def export_package(args):
    import material_rollout
    out = pathlib.Path(args.out) if args.out else ROOT / "pc/user/paint" / (
        f"model-{args.model}" if args.target == "model" and args.model else
        f"room-{args.zone}-{args.room}" if args.zone is not None else args.target)
    out.mkdir(parents=True, exist_ok=True)
    scratch = out / "scratch"
    scratch.mkdir(exist_ok=True)
    game = material_rollout.launch(scratch, argparse.Namespace(card=args.card))
    try:
        if args.zone is not None:
            rb.goto(game, args.zone, args.room)
        game.run(30)
        dump = out / "scene.obj"
        model = args.model
        if args.target == "model":
            if not model and args.model_at:
                x, y = (int(v) for v in args.model_at.split(","))
                text = game.call("material", action="pick", x=x, y=y).get("object", {}).get("text", "")
                if not text.startswith("m"):
                    raise SystemExit(f"paint_roundtrip: the pick at {x},{y} is not a model ({text or 'nothing'})")
                model = text[1:]
            if not model:
                raise SystemExit("paint_roundtrip: export model needs --model HASH or --model-at X,Y")
            game.call("material", action="model_dump", model=model, path=str(dump))
            groups = []
        else:
            game.call("scene_dump", path=str(dump))
            groups = game.call("material", action="groups")["groups"]
        uses = game.call("emissive", action="uses", keys=True)["uses"]
        positions, colours, tris = rb.parse_dump(dump)
        found = rb.assign(tris, uses)
        textures, rgba_of = {}, {}
        for t, u in zip(tris, found):
            if u and u["key"] not in textures:
                got = game.call("material", action="texture", key=u["key"])
                w, h = int(got["w"]), int(got["h"])
                rgba = np.frombuffer(__import__("base64").b64decode(got["rgba"]), np.uint8).reshape(h, w, 4).copy()
                rgba_of[u["key"]] = rgba
                textures[u["key"]] = {"w": w, "h": h, "box": u["box"], "tpage": u["tpage"], "clut": u["clut"]}
        state = game.state()
    finally:
        material_rollout.kill(game)
    if args.target == "model":
        # a model's textures are boxes of a few pages: one atlas per page and palette (the painter paints the page, import cuts the boxes)
        labels, split, slices = [], {}, {}
        for u in found:
            if not u:
                labels.append("unmapped")
                continue
            label = f"m{model}_p{u['tpage'] & 31}_c{u['clut']:04x}"
            labels.append(label)
            slices.setdefault(label, {})[u["key"]] = {"key": u["key"], "box": [int(b) for b in u["box"]], "rgba": rgba_of[u["key"]]}
            split.setdefault(label, {"w": 256, "h": 256, "box": [0, 0, 255, 255], "tpage": u["tpage"], "clut": u["clut"],
                                     "object": f"m{model}", "model": model})
        for label, boxes in slices.items():
            split[label]["atlas"] = sorted(boxes.values(), key=lambda b: b["key"])
        textures = split
        meta = {"target": "model", "model": model}
    else:
        labels, textures = rb.split_shared(tris, found, groups, textures, always=True)
        meta = {"target": "room", "zone": state["zone"], "room": state["room"]}
    records = write_package(out, positions, colours, tris, found, textures, labels, rgba_of, meta, seed_layers(args.bake))
    print(f"export: {meta['target']} {meta.get('model') or '%s-%s' % (meta['zone'], meta['room'])}: {len(tris)} triangles, "
          f"{len(records)} materials -> {out / 'paint.gltf'} (images in {out / 'tex'})")
    return 0


# ---------------------------------------------------------------------------------------------- import

def find_images(folder, label_safe, gltf):
    """{channel or 'roughness'/'metal'/...: path} the painter left for a material: its glTF's, then files named after it."""
    found = {}
    if gltf:
        by = {m.get("name", ""): m for m in gltf.get("materials", [])}
        m = by.get(label_safe) or next((v for k, v in by.items() if k.split(".")[0] == label_safe), None)
        if m:
            def uri(ref):
                if ref is None:
                    return None
                tex = gltf["textures"][ref["index"]]
                return folder / urllib.parse.unquote(gltf["images"][tex["source"]]["uri"])
            pbr = m.get("pbrMetallicRoughness", {})
            for channel, ref in (("basecolor", pbr.get("baseColorTexture")), ("metallicroughness", pbr.get("metallicRoughnessTexture")),
                                 ("normal", m.get("normalTexture")), ("occlusion", m.get("occlusionTexture"))):
                path = uri(ref)
                if path and path.exists():
                    found[channel] = path
    prefix = label_safe.lower()
    for path in folder.rglob("*"):
        if path.suffix.lower() not in (".png", ".tga", ".tif", ".tiff", ".bmp") or "seed" in path.parts:
            continue
        stem = path.stem.lower()
        if not stem.startswith(prefix):
            continue
        rest = stem[len(prefix):].lstrip("_.- ")
        for channel, names in ALIASES.items():
            if rest in names and channel not in found:
                found[channel] = path
    return found


def fit(array, h, w):
    """`array` as h x w (nearest: a painter's export at a whole multiple of the texture's size is read at each texel's middle)."""
    if array.shape[:2] == (h, w):
        return array
    ah, aw = array.shape[:2]
    if ah % h or aw % w:
        raise SystemExit(f"paint_roundtrip: an image is {aw} x {ah}, the texture {w} x {h}: export the painter's maps at the texture's size or a whole multiple of it")
    return array[ah // h // 2::ah // h, aw // w // 2::aw // w][:h, :w]


def load(path, mode):
    return np.asarray(Image.open(path).convert(mode))


def import_package(args):
    folder = pathlib.Path(args.dir)
    seed = json.loads((folder / "seed.json").read_text())
    gltf_path = folder / "paint.gltf"
    gltf = json.loads(gltf_path.read_text()) if gltf_path.exists() else None
    layer = pathlib.Path(args.layer) if args.layer else user_layer()
    changed_labels, written = [], 0
    for label, rec in seed["labels"].items():
        key, obj, h, w = rec["key"], rec["object"], rec["h"], rec["w"]
        label_safe = safe(label)
        images = find_images(folder, label_safe, gltf)

        def painted(channel, mode):
            """The painter's image of `channel` as the seed's shape, and the pristine one (None when it is not there)."""
            if channel not in images:
                return None, None
            seed_path = folder / "seed" / pathlib.Path(rec["files"][channel]).name
            return fit(load(images[channel], mode), h, w), load(seed_path, mode)
        maps = {}
        if "metallicroughness" in images:
            now, was = painted("metallicroughness", "RGB")
            if not np.array_equal(now[..., 1], was[..., 1]):
                maps["roughness"] = now[..., 1]
            if not np.array_equal(now[..., 2], was[..., 2]):
                maps["metal"] = now[..., 2]
        for channel, mode in (("roughness", "L"), ("metal", "L")):
            if channel in images:  # separate maps (Roughness, Metallic files)
                now = fit(load(images[channel], mode), h, w)
                was = painted_seed(folder, rec, channel, h, w)
                if was is None or not np.array_equal(now, was):
                    maps[channel] = now
        for channel, name, mode in (("normal", "normal", "RGB"), ("occlusion", "occlusion", "L")):
            if channel in images:
                now, was = painted(channel, mode)
                if not np.array_equal(now, was):
                    maps[name] = now
        if "basecolor" in images:
            now, was = painted("basecolor", "RGBA")
            if not np.array_equal(now, was):
                print(f"import: {label}: the base colour was edited; it is not imported (the game's texture is the base colour)")
        if not maps:
            continue
        if rec.get("slices"):  # a model's page: each box of it is a texture of the game, cut from the atlas
            for sl in rec["slices"]:
                u0, v0, u1, v1 = sl["box"]
                cut = {name: np.ascontiguousarray(array[v0:v1 + 1, u0:u1 + 1]) for name, array in maps.items()}
                was = {name: seed_slice(folder, rec, name, sl["box"]) for name in cut}
                cut = {name: array for name, array in cut.items() if was[name] is None or not np.array_equal(array, was[name])}
                if not cut:
                    continue
                dest = layer / sl["key"] / f"@{obj}"
                dest.mkdir(parents=True, exist_ok=True)
                for name, array in cut.items():
                    save_image(dest / f"{name}.png", array)
                    written += 1
                write_normal_parts(dest, cut)
                changed_labels.append((label, {**rec, "key": sl["key"], "w": u1 - u0 + 1, "h": v1 - v0 + 1}, sorted(cut)))
                print(f"import: {label}: {sl['key']}: {', '.join(sorted(cut))} -> {dest}")
            continue
        dest = layer / key / (f"@{obj}" if obj else "")
        dest.mkdir(parents=True, exist_ok=True)
        for name, array in maps.items():
            save_image(dest / f"{name}.png", np.ascontiguousarray(array))
            written += 1
        write_normal_parts(dest, maps)
        changed_labels.append((label, rec, sorted(maps)))
        print(f"import: {label}: {', '.join(sorted(maps))} -> {dest}")
    entries = ensure_entries(args, seed, changed_labels)
    print(f"import: {written} maps for {len(changed_labels)} materials into {layer}" + (f", {entries} catalog entries added" if entries else ""))
    return 0


def seed_slice(folder, rec, name, box):
    """The seed's `name` map over a box of an atlas (roughness and metal from the packed image; normal, occlusion as they are)."""
    u0, v0, u1, v1 = box
    if name in ("roughness", "metal"):
        whole = painted_seed(folder, rec, name, rec["h"], rec["w"])
    else:
        path = folder / "seed" / pathlib.Path(rec["files"][name]).name
        whole = load(path, "RGB" if name == "normal" else "L") if path.exists() else None
    return None if whole is None else np.ascontiguousarray(whole[v0:v1 + 1, u0:u1 + 1])


def write_normal_parts(dest, maps):
    """A painted normal also as the two 8-bit maps the game reads for its relief (normal_x.png, normal_y.png: the normal's x and y, 128 + n x 127.5;
    normal.png stays for other tools)."""
    if "normal" in maps:
        # how strong its relief is: the strength the bake made normals from heights with (the game reads it from normal.json, so they cannot drift)
        strength = rb.config()["maps"]["normal_strength"]
        (dest / "normal.json").write_text(json.dumps({"strength": strength, "about": "paint_roundtrip.py: room_blender.json maps.normal_strength"}) + "\n")
        for index, name in enumerate(("normal_x", "normal_y")):
            save_image(dest / f"{name}.png", np.ascontiguousarray(maps["normal"][..., index]))


def painted_seed(folder, rec, channel, h, w):
    """The seed's roughness or metal (from its packed image) for painters that write them as separate files."""
    path = folder / "seed" / pathlib.Path(rec["files"]["metallicroughness"]).name
    if not path.exists():
        return None
    mr = load(path, "RGB")
    return mr[..., 1] if channel == "roughness" else mr[..., 2]


def ensure_entries(args, seed, changed):
    """A user catalog entry for each painted object (a map only applies where an entry matches): its texture on its room group or model,
    the preset a neutral surface. An entry that is there already is left as it is."""
    path = pathlib.Path(args.catalog) if args.catalog else user_catalog()
    data = json.loads(path.read_text()) if path.exists() else {"format": 2, "presets": {}, "entries": []}
    ids = {e["id"] for e in data.get("entries", [])}
    added = 0
    for label, rec, _ in changed:
        key, obj = rec["key"], rec["object"]
        entry_id = f"PT-{key}" + (f"-{obj}" if obj else "")
        if entry_id in ids:
            continue
        entry = {"id": entry_id, "name": f"painted {key}" + (f" on {obj}" if obj else ""), "where": "paint_roundtrip",
                 "match": {"key": key, "size": [rec["w"], rec["h"]]}, "preset": args.preset}
        room = [seed["zone"], seed["room"]] if seed.get("target") == "room" else None
        if obj and rec.get("group_offset") is not None:
            entry["object"] = {"groups": [rec["group_offset"]]}
        elif obj and rec.get("model"):
            entry["object"] = {"models": [rec["model"]]}
        if room and not rec.get("model"):
            entry["scope"] = {"room": room}
        data.setdefault("entries", []).append(entry)
        if "object" in entry:
            data["format"] = 2
        added += 1
    if added:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=1) + "\n")
        os.replace(tmp, path)
    return added


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    e = sub.add_parser("export")
    e.add_argument("target", choices=["room", "model"])
    e.add_argument("--zone", type=int)
    e.add_argument("--room", type=int)
    e.add_argument("--model", help="a model's content hash (8 hex digits: RPC material pick's object text without the m)")
    e.add_argument("--model-at", help="X,Y: the model under a pixel of the view")
    e.add_argument("--bake", action="append", default=[], help="a bake's materials folder (room_blender.py <out>/materials) to seed the maps from, the geometry-based normals included; also $ROOD_MATERIALS_BAKED")
    e.add_argument("--out")
    e.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    i = sub.add_parser("import")
    i.add_argument("dir")
    i.add_argument("--layer", help="the maps' layer (default: the user layer, ROOD_MATERIALS or <lighting>/materials)")
    i.add_argument("--catalog", help="the user catalog file (default: ROOD_MATERIAL_CATALOG_USER or <lighting>/materials.json)")
    i.add_argument("--preset", default="stone", help="the preset of the entries made for painted objects")
    args = parser.parse_args()
    if args.command == "export" and (args.zone is None) != (args.room is None):
        parser.error("--zone and --room go together")
    return export_package(args) if args.command == "export" else import_package(args)


if __name__ == "__main__":
    sys.exit(main())
