#!/usr/bin/env python3
"""Painting the game's textures in a real painting program and bringing the maps back (pc/docs/Materials.md, "Painting
the maps in a painter").

    python3 pc/tools/paint_roundtrip.py export room  [--zone Z --room R] [--out DIR]
    python3 pc/tools/paint_roundtrip.py export model (--model HASH|studio | --model-at X,Y) [--zone Z --room R] [--out DIR]
    python3 pc/tools/paint_roundtrip.py paint  (room|model) ...   export, then open paint.gltf in SubCraft (or Blender)
    python3 pc/tools/paint_roundtrip.py (export|paint) ... --painter aseprite   also <out>/aseprite/<material>.aseprite (a layer per channel: Height, Metal, Roughness)
    python3 pc/tools/paint_roundtrip.py watch DIR     import each .aseprite file when it is saved
    python3 pc/tools/paint_roundtrip.py palette DIR   rewrite the swatches of DIR/aseprite/*.aseprite from the preset files (pixels untouched)
    python3 pc/tools/paint_roundtrip.py live DIR      watch, and take strokes live from the Aseprite extension (pc/tools/paint_live.py)
    python3 pc/tools/paint_roundtrip.py pack DIR [--out FILE]    one small shareable .roodpack.zip of the painted pixels (no game texture)
    python3 pc/tools/paint_roundtrip.py unpack FILE DIR           apply a pack onto a fresh export (--painter aseprite) of the same model or room
    python3 pc/tools/paint_roundtrip.py import DIR   [--layer DIR] [--catalog FILE] [--normal-format auto|opengl|directx] [--keep-seed]

export  A glTF 2.0 file (paint.gltf) of the room the game is in (or of one model: a character, weapon or prop, as it stands in the
        newest frame) with the PBR metallic-roughness material: one material per object and texture (a texture used by more than
        one room group is a material of its own for each group, so each object owns its texels), its base colour the game's texture
        texel for texel (nearest sampling), and the metallicRoughness (G roughness, B metal), normal and occlusion images seeded from
        the maps the game has now (or empty): the user layer's, the shipped, the bake's (--bake, or $ROOD_MATERIALS_BAKED), so the geometry-based normals the game shows start the painter's normal map. The folder holds paint.gltf, paint.bin, tex/ (the images a painter edits), seed/
        (pristine copies, never edit) and seed.json. It goes to a user folder (default pc/user/paint/<name>): never to git.
import  Reads what the painter saved: the glTF with its images, or the images it wrote by name next to it (<material>_<Map>.png:
        BaseColor, Roughness, Metallic, Normal, AO or OcclusionRoughnessMetallic / ORM / MetallicRoughness, any case), and writes the
        channels that differ from the seed as the user material layer's maps of that object (<layer>/<key>/@<object>/roughness.png,
        metal.png, normal.png, occlusion.png), with a user catalog entry for each painted object so the maps apply. A painter's file
        name may carry a prefix (SubCraft writes <mesh>_<material>_<Map>.png), its images may be bigger than the texture (they are
        averaged down, normals renormalised; the game reads maps only at the texture's size) and its normals DirectX (named so, or
        --normal-format). Unpainted
        channels write nothing: an export imported without painting changes nothing.
        Also the .aseprite files (aseprite_io.py): their channel layers (paint_channels.py: Height, Metal, Roughness) and the Material layer (preset swatches: material_presets.py) are the maps, a transparent pixel keeps the game's
        value, a file newer than the PNGs wins its channel, and a map follows its layer (an emptied layer resets it).
Only the game's own files are read and written; the folder it exports is game art and stays on the machine."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import zlib

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ.setdefault("ROOD_ARCH", "64")  # the 32-bit build is retired: run by hand, these tools use the 64-bit game
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import aseprite_io as ase  # noqa: E402
import material_presets as mp  # noqa: E402
import paint_channels as pch  # noqa: E402
import room_blender as rb  # noqa: E402

CHANNELS = ("basecolor", "metallicroughness", "normal", "occlusion", "height")  # the package's images (height: a file of its own, not in the glTF)
NAMES = {"basecolor": "BaseColor", "metallicroughness": "MetallicRoughness", "normal": "Normal", "occlusion": "Occlusion", "height": "Height"}
# the names painters give a map (lower case, without the material's name and the separator)
ALIASES = {
    "basecolor": ("basecolor", "base_color", "albedo", "diffuse", "color"),
    "metallicroughness": ("metallicroughness", "occlusionroughnessmetallic", "orm", "metallic_roughness", "mr", "rma"),
    "roughness": ("roughness", "rough"),
    "metal": ("metallic", "metalness", "metal"),
    "normal": ("normal", "normalopengl", "normal_opengl", "normalgl", "normal_gl", "normaldirectx", "normal_directx", "normaldx", "normal_dx", "nrm"),
    "occlusion": ("occlusion", "ambientocclusion", "ambient_occlusion", "ao"),
}
# (no "height" here: the Height layer of the .aseprite file is the painter for height; SubCraft and the like export a Height image of their own that is
# not the game's relief, and the package's tex/<material>_Height.png is only the seed's copy)
DIRECTX = re.compile(r"(directx|(?<![a-z])dx(?![a-z])|normaldx)")  # a name that says its normal map is DirectX (green down)
# the layers of a material's .aseprite file come from the channel table (pc/tools/aseprite/channels.json): the reference colour, one layer
# per channel (display opacity and visibility only: import reads the cel's pixels), the preset layer above them
ASE_LAYERS = pch.LAYERS
ASE_MATERIAL = mp.LAYER  # the layer painted with the preset swatches (above the channel layers): one stroke sets every channel the preset has
ASE_COLOUR = pch.REFERENCE
ASE_MATERIAL_OPACITY = pch.PRESET_LAYER["opacity"]
PACKED = {c: pch.seed_image(c) for c in pch.NAMES if pch.seed_image(c)[1] is not None}  # channels that live in a plane of the glTF's metallicRoughness image
STANDARD = {"roughness": 1.0, "specular": 0.0, "metal": 0.0, "auto_height": False, "bump": 1.0}  # what a texture with no catalog entry is (materials.cpp surface_of)
RESERVED = {"id", "name", "where", "review", "match", "scope", "object", "preset", "parent"}  # (an entry's own identity, not its look)


def safe(label):
    return label.replace("@", "_")


def user_layer():
    return pathlib.Path(os.environ.get("ROOD_MATERIALS") or (pathlib.Path(os.environ.get("ROOD_LIGHTS") or ROOT / "pc/user/lighting") / "materials"))


def user_catalog():
    return pathlib.Path(os.environ.get("ROOD_MATERIAL_CATALOG_USER") or (pathlib.Path(os.environ.get("ROOD_LIGHTS") or ROOT / "pc/user/lighting") / "materials.json"))


def catalog_files(user=None):
    """The catalogs the game reads, the shipped one first and the user's after it (a user entry of the same id replaces it in place)."""
    shipped = os.environ.get("ROOD_MATERIAL_CATALOG") or str(pathlib.Path(os.environ.get("ROOD_MATERIALS_SHIPPED") or ROOT / "pc/assets/materials") / "catalog.json")
    return [pathlib.Path(shipped), pathlib.Path(user) if user else user_catalog()]


def load_catalog(files):
    """({id: raw preset or entry}, [raw entries in the game's order]) of catalog files (a missing or unreadable one is empty)."""
    byid, entries = {}, []
    for path in files:
        try:
            data = json.loads(pathlib.Path(path).read_text())
        except (OSError, ValueError):
            continue
        for name, preset in (data.get("presets") or {}).items():
            byid[name] = preset
        for e in data.get("entries") or []:
            old = next((i for i, x in enumerate(entries) if x.get("id") == e.get("id")), None)
            if old is None:
                entries.append(e)
            else:
                entries[old] = e
            byid[e.get("id")] = e
    return byid, entries


def in_scope(entry, zone, room):
    scope = entry.get("scope") or {}
    rooms = [scope["room"]] if "room" in scope else scope.get("rooms")
    return rooms is None or (zone is not None and [zone, room] in [list(r) for r in rooms])


def object_ids(rec):
    if rec.get("object") and rec["object"].startswith("g"):
        return {rec["object"]}
    if not rec.get("model"):
        return set()
    # a model is its content key (10 hex digits) and the legacy ids it was loaded under (8: model_aliases.json names them)
    ids = {rec["model"], rec.get("model_legacy") or rec["model"]}
    ids |= {legacy for legacy, content in model_aliases().items() if content == rec["model"]}
    return {f"m{i}" for i in ids if i}


def entry_for(files, rec, key, w, h, zone=None, room=None):
    """The raw catalog entry the game draws texture `key` (w x h) of `rec`'s object with, or None: an entry for the object first (the last
    that matches), else the entry the game itself said the texture had when it was exported (`rec["entry"]`: it knows palettes)."""
    byid, entries = load_catalog(files)
    mine = object_ids(rec)
    for e in reversed(entries):
        obj = e.get("object")
        if not obj or not in_scope(e, zone, room):
            continue
        ids = {f"g{g:x}" for g in obj.get("groups", [])} | {f"m{m}" for m in obj.get("models", [])}
        m = e.get("match") or {}
        if mine & ids and not m.get("clut") and (m.get("key", key) == key) and (not m.get("size") or list(m["size"]) == [w, h]):
            return e
    return byid.get(rec.get("entry") or "")


def rekey_tree(catalog_path, layer, aliases, apply=True):
    """Moves a user tree to the models' content keys, losslessly (design 7.2): for each user catalog entry that names a model by a legacy id
    with an alias (`aliases`: {legacy: content}), its map folders <layer>/<texture>/@m<legacy> are COPIED to @m<content> (the legacy ones stay;
    where both exist the newer file wins and the conflict is listed) and the entry names the content key instead (its id is kept). Nothing
    is written without `apply`. Returns {entries, folders, copied, conflicts}."""
    catalog_path, layer = pathlib.Path(catalog_path), pathlib.Path(layer)
    out = {"entries": 0, "folders": 0, "copied": 0, "conflicts": []}
    try:
        data = json.loads(catalog_path.read_text())
    except (OSError, ValueError):
        return out
    changed = False
    for entry in data.get("entries", []):
        models = (entry.get("object") or {}).get("models")
        if not models or not any(m in aliases for m in models):
            continue
        new = []
        for m in models:
            to = aliases.get(m, m)
            if to not in new:
                new.append(to)
            if to == m:
                continue
            key = (entry.get("match") or {}).get("key")
            for src in ([layer / key / f"@m{m}"] if key else sorted(layer.glob(f"*/@m{m}"))):
                if not src.is_dir():
                    continue
                dst = src.parent / f"@m{to}"
                out["folders"] += 1
                for f in sorted(src.iterdir()):
                    target = dst / f.name
                    if target.exists() and target.read_bytes() != f.read_bytes():
                        newer = f.stat().st_mtime_ns > target.stat().st_mtime_ns
                        out["conflicts"].append(f"{target.relative_to(layer)}: {'the legacy file is newer and wins' if newer else 'the content-key file is newer and stays'}")
                        if not newer:
                            continue
                    elif target.exists():
                        continue
                    if apply:
                        dst.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(f, target)
                    out["copied"] += 1
        entry["object"]["models"] = new
        out["entries"] += 1
        changed = True
    if changed and apply:
        tmp = catalog_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=1) + "\n")
        os.replace(tmp, catalog_path)
    return out


def rekey(args):
    """`rekey`: asks the game (after visiting the rooms named by --visit Z,R) for every model's legacy id and content key, merges them into
    model_aliases.json (the game reads it, so old ids and unmigrated entries keep working), and with --apply moves the user catalog and maps
    to the content keys (rekey_tree: copies, never moves; the old ones stay)."""
    import material_rollout
    scratch = ROOT / "pc/build/rekey"
    scratch.mkdir(parents=True, exist_ok=True)
    game = material_rollout.launch(scratch, argparse.Namespace(card=args.card))
    pairs = {}
    try:
        for visit in [None] + (args.visit or []):
            if visit:
                zone, room = (int(v) for v in visit.split(","))
                rb.goto(game, zone, room, enemies=True)
            game.run(30)
            for r in game.call("material", action="model_ids")["models"]:
                if r["content"]:
                    pairs[r["legacy"]] = r["content"]
    finally:
        material_rollout.kill(game)
    path = aliases_file()
    aliases = {**model_aliases(path), **pairs}
    print(f"rekey: {len(pairs)} models seen, {len(set(pairs.values()))} content keys, {len(aliases)} aliases in {path}")
    if not args.apply:
        print("rekey: dry run (--apply writes the aliases and moves the tree)")
        return 0
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(sorted(aliases.items())), indent=1) + "\n")
    report = rekey_tree(args.catalog or user_catalog(), args.layer or user_layer(), aliases)
    print(f"rekey: {report['entries']} entries now name the content key, {report['copied']} map files copied from {report['folders']} folders; "
          f"{len(report['conflicts'])} conflicts")
    for line in report["conflicts"]:
        print("  " + line)
    return 0


def aliases_file():
    return pathlib.Path(os.environ.get("ROOD_MODEL_ALIASES") or (pathlib.Path(os.environ.get("ROOD_LIGHTS") or ROOT / "pc/user/lighting") / "model_aliases.json"))


def model_aliases(path=None):
    """{legacy id (8 hex): content key (10 hex)} of the user's model_aliases.json (the game reads it too: materials.cpp model_alias)."""
    try:
        data = json.loads(pathlib.Path(path or aliases_file()).read_text())
    except (OSError, ValueError):
        return {}
    return {k: v for k, v in data.items() if isinstance(v, str) and len(k) == 8 and len(v) == 10}


def surface_of(files, rec, key, w, h, zone=None, room=None):
    """What the game draws texture `key` with now: ({roughness, specular, metal, auto_height, bump}, the entry's other values (a master's
    parameters)), the standard no-entry surface when nothing matches. Presets and parents resolve as materials.cpp does."""
    byid, _ = load_catalog(files)
    entry = entry_for(files, rec, key, w, h, zone, room)
    chain, seen = [], set()
    while entry is not None and id(entry) not in seen:
        seen.add(id(entry))
        chain.append(entry)
        entry = byid.get(entry.get("parent") or entry.get("preset") or "")
    values, extra = dict(STANDARD), {}
    for e in reversed(chain):
        for name, value in e.items():
            if name in RESERVED:
                continue
            (values if name in STANDARD else extra)[name] = value
    return values, extra


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


def seed_images(layers, key, obj, rgba, surface=None):
    """The four images an export starts a painter with: the game's texture and the maps the game has (else the entry's uniform values,
    `surface`: surface_of's values, the standard no-entry ones when there is none)."""
    h, w = rgba.shape[:2]
    surface = surface or STANDARD
    rough = read_map(layers, key, obj, "roughness")
    metal = read_map(layers, key, obj, "metal")
    mr = np.zeros((h, w, 3), np.uint8)
    mr[..., 0] = 255
    mr[..., 1] = rough if rough is not None and rough.shape == (h, w) else uniform(surface, "roughness")
    mr[..., 2] = metal if metal is not None and metal.shape == (h, w) else uniform(surface, "metal")
    height = read_map(layers, key, obj, "height")
    if height is None or height.shape != (h, w):
        height = np.full((h, w), pch.blank("height"), np.uint8)
    normal = read_map(layers, key, obj, "normal")
    if normal is None or normal.shape[:2] != (h, w):
        normal = np.zeros((h, w, 3), np.uint8)
        normal[...] = (128, 128, 255)
    occlusion = read_map(layers, key, obj, "occlusion")
    if occlusion is None or occlusion.shape != (h, w):
        occlusion = np.full((h, w), 255, np.uint8)
    return {"basecolor": rgba, "metallicroughness": mr, "normal": normal, "occlusion": occlusion, "height": height}


def game_maps(layers, key, obj, h, w):
    """The channels of the table the game has a map for (a file of the right size in a layer it reads) for texture `key` of `obj`: what an
    exported seed is the game's own values of; the others' seeds are the blank value standing in for no map (the live bridge puts no map back
    for those, so the estimate or the entry's own value shows again)."""
    out = []
    for channel in pch.NAMES:
        got = read_map(layers, key, obj, channel)
        if got is not None and got.shape == (h, w):
            out.append(channel)
    return out


def uniform(surface, channel):
    """A uniform fraction as the 8-bit value of a map."""
    return int(round(float(surface[channel]) * 255))


def atlas_images(layers, obj, slices, w, h, surfaces=None):
    """A model's page as one set of images: each box of it (a use: slices of {key, box, rgba}) pasted where it lies in the page, the
    rest empty (transparent colour, neutral maps), so a painter has one coherent atlas to paint on."""
    out = {"basecolor": np.zeros((h, w, 4), np.uint8), "metallicroughness": np.zeros((h, w, 3), np.uint8),
           "normal": np.zeros((h, w, 3), np.uint8), "occlusion": np.full((h, w), 255, np.uint8),
           "height": np.full((h, w), pch.blank("height"), np.uint8)}
    out["metallicroughness"][...] = (255, pch.blank("roughness"), pch.blank("metal"))
    out["normal"][...] = (128, 128, 255)
    for sl in slices:
        u0, v0, u1, v1 = sl["box"]
        part = seed_images(layers, sl["key"], obj, sl["rgba"], (surfaces or {}).get(sl["key"]))
        for channel in CHANNELS:
            out[channel][v0:v1 + 1, u0:u1 + 1] = part[channel]
    return out


def save_image(path, array):
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = "RGBA" if array.ndim == 3 and array.shape[2] == 4 else "RGB" if array.ndim == 3 else "L"
    Image.fromarray(array, mode).save(path)


def write_aseprite(path, images):
    """A material's .aseprite file for a pixel painter: the game's colour (locked), then a layer per channel of the table (Height, Metal,
    Roughness...: RGBA gray from the maps the game has, shown or hidden and at the opacity the table says), then Material (empty: painted
    with the palette's preset swatches); a transparent pixel of a layer keeps the game's value. The palette is the presets' swatches, named,
    then grays (material_presets.py)."""
    h, w = images["basecolor"].shape[:2]

    def gray(values):
        out = np.empty((h, w, 4), np.uint8)
        out[..., :3] = values[..., None]
        out[..., 3] = 255
        return out
    layers = [{"name": ASE_COLOUR, "pixels": images["basecolor"], "locked": True}]
    for channel in pch.NAMES:
        row = pch.BY_CHANNEL[channel]
        image, plane = pch.seed_image(channel)
        values = images[image] if plane is None else images[image][..., plane]
        layer = {"name": row["layer"], "pixels": gray(values), "opacity": row["opacity"]}
        if not row["visible"]:
            layer["visible"] = False
        layers.append(layer)
    layers.append({"name": ASE_MATERIAL, "pixels": np.zeros((h, w, 4), np.uint8), "opacity": ASE_MATERIAL_OPACITY})
    path.parent.mkdir(parents=True, exist_ok=True)
    ase.write(path, w, h, layers, palette=mp.palette(mp.load()))


def write_package(out, positions, colours, tris, found, textures, labels, rgba_of, meta, layers, outward=False, aseprite=False):
    """paint.gltf, tex/, seed/ and seed.json in `out`. textures: {label: {w, h, box, key?, object?, ...}}, rgba_of: {key: RGBA array}."""
    shutil.rmtree(out / "tex", ignore_errors=True)
    shutil.rmtree(out / "seed", ignore_errors=True)
    shutil.rmtree(out / "aseprite", ignore_errors=True)
    pbr, records = {}, {}
    for label in sorted(set(labels)):
        t = textures.get(label)
        if not t:
            continue
        key = t.get("texture", label)
        obj = t.get("object", "")
        atlas = t.get("atlas")
        rec = {"object": obj, "model": t.get("model"), "model_legacy": t.get("model_legacy"), "entry": t.get("entry", "")}
        zone, room = meta.get("zone"), meta.get("room")
        own_maps = None
        if atlas:
            surfaces = {sl["key"]: surface_of(catalog_files(), {**rec, "entry": sl.get("entry", "")}, sl["key"], sl["box"][2] - sl["box"][0] + 1,
                                             sl["box"][3] - sl["box"][1] + 1, zone, room)[0] for sl in atlas}
            images = atlas_images(layers, obj, atlas, t["w"], t["h"], surfaces)
            for sl in atlas:
                sl["maps"] = game_maps(layers, sl["key"], obj, sl["box"][3] - sl["box"][1] + 1, sl["box"][2] - sl["box"][0] + 1)
        else:
            images = seed_images(layers, key, obj, rgba_of[key], surface_of(catalog_files(), rec, key, t["w"], t["h"], zone, room)[0])
            own_maps = game_maps(layers, key, obj, t["h"], t["w"])
        files = {"name": safe(label)}
        for channel in CHANNELS:
            name = f"{safe(label)}_{NAMES[channel]}.png"
            save_image(out / "tex" / name, images[channel])
            save_image(out / "seed" / name, images[channel])
            files[channel] = f"tex/{name}"
        pbr[label] = files
        if aseprite:
            write_aseprite(out / "aseprite" / f"{safe(label)}.aseprite", images)
        records[label] = {"key": key, "object": obj, "w": t["w"], "h": t["h"], "group_offset": t.get("group_offset"),
                          "model": t.get("model"), "model_legacy": t.get("model_legacy"), "entry": t.get("entry", ""), "files": {c: files[c] for c in CHANNELS},
                          **({"maps": own_maps} if own_maps is not None else {}),
                          **({"slices": [{k: v for k, v in sl.items() if k != "rgba"} for sl in atlas]} if atlas else {})}
    rb.write_gltf(out, positions, colours, tris, found, textures, labels, pbr=pbr, folder=".", name="paint", outward=outward)
    (out / "seed.json").write_text(json.dumps({"version": 1, **meta, "labels": records}, indent=1))
    return records


# ---------------------------------------------------------------------------------------------- export

def studio_model(game):
    """The content key of the model summoned in the running game's model studio: what is under the middle of the picture (the studio frames
    the model there), looking outwards over a grid of pixels until one is a model."""
    state = game.call("studio")
    if not state.get("entered") or not state.get("item"):
        raise SystemExit("paint_roundtrip: --model studio needs the game's model studio open with a model summoned (Window > Studio)")
    models = state.get("models") or []
    if len(models) == 1:  # (the studio says which model it drew)
        return models[0]
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:  # (the window's size: the header of a presented frame)
        frame = pathlib.Path(tmp) / "frame.ppm"
        game.call("present_dump", path=str(frame))
        deadline = time.time() + 30
        while time.time() < deadline and not (frame.exists() and len(frame.read_bytes()[:20].split()) >= 3):
            time.sleep(0.05)
        w, h = (int(v) for v in frame.read_bytes()[:20].split()[1:3])
    cx, cy = w // 2, h // 2
    for r in range(0, min(w, h) // 2, 12):
        for dx, dy in ((0, 0),) if r == 0 else ((r, 0), (-r, 0), (0, r), (0, -r), (r, r), (-r, r), (r, -r), (-r, -r)):
            text = game.call("material", action="pick", x=cx + dx, y=cy + dy).get("object", {}).get("text", "")
            if text.startswith("m"):
                return text[1:]
    if models:
        return models[0]
    raise SystemExit("paint_roundtrip: could not find the studio's model in the picture (is it on screen?)")


def export_package(args):
    import material_rollout
    out = pathlib.Path(args.out) if args.out else ROOT / "pc/user/paint" / (
        f"model-{args.model}" if args.target == "model" and args.model else
        f"room-{args.zone}-{args.room}" if args.zone is not None else args.target)
    out.mkdir(parents=True, exist_ok=True)
    live = getattr(args, "game", None)
    was_paused = True
    if live:  # the running game (its studio's model, or the room it is in) instead of one started here
        import rood_rpc
        game = rood_rpc.Game(live)
    else:
        scratch = out / "scratch"
        scratch.mkdir(exist_ok=True)
        game = material_rollout.launch(scratch, argparse.Namespace(card=args.card))
    try:
        if args.zone is not None:
            rb.goto(game, args.zone, args.room, enemies=args.target == "model")  # (a model: the room's enemies are there to export)
        was_paused = bool(game.status().get("paused")) if live else True
        game.run(30)
        model = args.model
        if args.target == "model":
            if model == "studio":
                model = studio_model(game)
                if not args.out:  # (named after the model, as --model HASH is)
                    out = ROOT / "pc/user/paint" / f"model-{model}"
                    out.mkdir(parents=True, exist_ok=True)
            if not model and args.model_at:
                x, y = (int(v) for v in args.model_at.split(","))
                text = game.call("material", action="pick", x=x, y=y).get("object", {}).get("text", "")
                if not text.startswith("m"):
                    raise SystemExit(f"paint_roundtrip: the pick at {x},{y} is not a model ({text or 'nothing'})")
                model = text[1:]
            if not model:
                raise SystemExit("paint_roundtrip: export model needs --model HASH or --model-at X,Y")
            dump = out / "scene.obj"
            try:
                game.call("material", action="model_dump", model=model, path=str(dump))
            except Exception as e:
                if "not drawn now" not in str(e):
                    raise
                raise SystemExit(f"paint_roundtrip: {e}\nThe model has to be on screen in the game that is asked: summon it in the model studio and "
                                 "export from that game (--game SOCKET --model studio), or name the room that has it (--zone Z --room R).")
            # the model's ids: its content key names its materials (it follows the model into any texture page); the legacy id is kept beside
            row = next((r for r in game.call("material", action="model_ids")["models"] if model in (r["legacy"], r["content"])), None)
            model_legacy = row["legacy"] if row else (model if len(model) == 8 else "")
            if row and row["content"]:
                model = row["content"]
            groups = []
        else:
            dump = out / "scene.obj"
            game.call("scene_dump", path=str(dump))
            groups = game.call("material", action="groups")["groups"]
        uses = game.call("emissive", action="uses", keys=True)["uses"]
        positions, colours, tris = rb.parse_dump(dump)
        if args.target == "model":  # the model stands at the origin, its feet on the ground (the painter's view of it; the pose is the game's)
            lo, hi = positions.reshape(-1, 3).min(axis=0), positions.reshape(-1, 3).max(axis=0)
            positions = positions - np.array([(lo[0] + hi[0]) / 2, hi[1], (lo[2] + hi[2]) / 2], np.float32)  # (y is down)
        found = rb.assign(tris, uses)
        textures, rgba_of = {}, {}
        for t, u in zip(tris, found):
            if u and u["key"] not in textures:
                got = game.call("material", action="texture", key=u["key"])
                w, h = int(got["w"]), int(got["h"])
                rgba = np.frombuffer(__import__("base64").b64decode(got["rgba"]), np.uint8).reshape(h, w, 4).copy()
                rgba_of[u["key"]] = rgba
                textures[u["key"]] = {"w": w, "h": h, "box": u["box"], "tpage": u["tpage"], "clut": u["clut"], "entry": u.get("entry", "")}
        state = game.state() if args.target == "room" else None  # (a room's package names its zone and room; a model's needs no game: rood-studio has none)
    finally:
        if not live:
            material_rollout.kill(game)
        elif not was_paused:
            game.pause(False)  # (a game that was running runs on: the studio is not left stopped at the frame it was exported at)
    if args.target == "model":
        # a model's textures are boxes of a few pages: one atlas per page and palette (the painter paints the page, import cuts the boxes)
        labels, split, slices = [], {}, {}
        for u in found:
            if not u:
                labels.append("unmapped")
                continue
            label = f"m{model}_p{u['tpage'] & 31}_c{u['clut']:04x}"
            labels.append(label)
            slices.setdefault(label, {})[u["key"]] = {"key": u["key"], "box": [int(b) for b in u["box"]], "rgba": rgba_of[u["key"]], "entry": u.get("entry", "")}
            split.setdefault(label, {"w": 256, "h": 256, "box": [0, 0, 255, 255], "tpage": u["tpage"], "clut": u["clut"],
                                     "object": f"m{model}", "model": model, "model_legacy": model_legacy})
        for label, boxes in slices.items():
            split[label]["atlas"] = sorted(boxes.values(), key=lambda b: b["key"])
        textures = split
        meta = {"target": "model", "model": model, "model_legacy": model_legacy}
    else:
        labels, textures = rb.split_shared(tris, found, groups, textures, always=True)
        meta = {"target": "room", "zone": state["zone"], "room": state["room"]}
    records = write_package(out, positions, colours, tris, found, textures, labels, rgba_of, meta, seed_layers(args.bake),
                             outward=args.target == "model", aseprite=getattr(args, "painter", None) == "aseprite")
    args.out = str(out)  # (where it went: `paint` opens it)
    print(f"export: {meta['target']} {meta.get('model') or '%s-%s' % (meta['zone'], meta['room'])}: {len(tris)} triangles, "
          f"{len(records)} materials -> {out / 'paint.gltf'} (images in {out / 'tex'})")
    return 0


# ---------------------------------------------------------------------------------------------- import

def find_images(folder, label_safe, gltf):
    """{channel or 'roughness'/'metal'/...: path} the painter left for a material: its glTF's images and the files it exported by name
    (`<mesh>_<material>_<Map>.png` as SubCraft writes them, `<material>_<Map>.png`, any case); of two for one map the newer wins, so
    an export made after the package was written beats the package's own images."""
    cands = {}

    def add(channel, path):
        cands.setdefault(channel, []).append(path)
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
                    add(channel, path)
    ase_file = ase_path(folder, label_safe)
    if ase_file.exists():
        for channel, rgba in read_ase_channels(ase_file).items():
            add(channel, AseSource(ase_file, rgba))
    label = re.escape(label_safe.lower())
    pattern = re.compile(rf"(?:.*[_.\- ])?{label}[_.\- ]+(.+)")
    for path in folder.rglob("*"):
        if path.suffix.lower() not in (".png", ".tga", ".tif", ".tiff", ".bmp") or "seed" in path.parts:
            continue
        hit = pattern.fullmatch(path.stem.lower())
        if not hit:
            continue
        for channel, names in ALIASES.items():
            if hit.group(1) in names:
                add(channel, path)
    best = {channel: max(paths, key=lambda p: p.stat().st_mtime_ns) for channel, paths in cands.items()}
    packed = best.get("metallicroughness")
    for channel in PACKED:  # (a packed image the painter saved after the .aseprite file is the newer: it wins that channel)
        if isinstance(best.get(channel), AseSource) and packed is not None and packed.stat().st_mtime_ns > best[channel].stat().st_mtime_ns:
            del best[channel]
            best.pop("material", None)  # (the file's Material layer goes with it: the packed export is the painter's newer work)
    return best


_warned = set()


def warn_once(message):
    if message not in _warned:
        _warned.add(message)
        print(f"import: {message}")


def resample(array, h, w, normal=False):
    """`array` as h x w: a painter's export is often bigger than the game's tiny textures (SubCraft's default is 2048), so it is
    averaged down (an area filter; a normal map is renormalised after it), or repeated up when it is smaller. The game reads maps texel for
    texel at the texture's size, so nothing else renders."""
    ah, aw = array.shape[:2]
    if (ah, aw) == (h, w):
        return array
    warn_once(f"images are not the texture's size (e.g. {aw} x {ah} for {w} x {h}): resampled to the texture's size "
              f"({'area average' if ah >= h and aw >= w else 'repeated'}); the game only reads maps at the texture's size")
    if ah < h or aw < w:
        return np.asarray(Image.fromarray(array).resize((w, h), Image.NEAREST))
    planes = array if array.ndim == 3 else array[..., None]
    if normal:
        planes = (planes.astype(np.float32) - 128) / 127.5
    out = np.stack([np.asarray(Image.fromarray(planes[..., c].astype(np.float32), "F").resize((w, h), Image.BOX))
                    for c in range(planes.shape[2])], axis=2)
    if normal:
        out = out / np.maximum(np.linalg.norm(out, axis=2, keepdims=True), 1e-6)
        out = 128 + out * 127.5
    out = np.clip(np.rint(out), 0, 255).astype(np.uint8)
    return out if array.ndim == 3 else out[..., 0]


def is_directx(path, normal_format):
    """Whether a normal map file is DirectX (green down): the format asked for, or by its name (`normalDX`, `Normal_DirectX`, ...)."""
    if normal_format != "auto":
        return normal_format == "directx"
    return bool(DIRECTX.search(path.stem.lower()))


def flip_green(array):
    out = array.copy()
    out[..., 1] = 255 - out[..., 1]
    return out


def keep_seed(now, was):
    """`now` with the painter's untouched flat fill (the value most of the image has: what a painter exports where nothing is painted)
    replaced by the seed's own map, so painting a corner does not flatten the rest of the map the game already has."""
    flat = now.reshape(-1, now.shape[-1]) if now.ndim == 3 else now.reshape(-1, 1)
    values, counts = np.unique(flat, axis=0, return_counts=True)
    top = counts.argmax()
    if counts[top] * 2 < len(flat):
        return now
    mask = (flat == values[top]).all(axis=1).reshape(now.shape[:2])
    out = now.copy()
    out[mask] = was[mask]
    return out


def load(path, mode):
    return np.asarray(Image.open(path).convert(mode))


class AseSource:
    """A channel layer of a material's .aseprite file: its cel pixels as a canvas-sized RGBA image (transparent: keep the game's value).
    Stands in for a Path in find_images' candidates (stat), the newest file winning."""

    def __init__(self, path, rgba):
        self.path, self.rgba = path, rgba

    def stat(self):
        return self.path.stat()


def ase_path(folder, label_safe):
    return pathlib.Path(folder) / "aseprite" / f"{label_safe}.aseprite"


def read_ase_channels(path):
    """{channel: RGBA canvas} of the Metal and Roughness layers of an .aseprite file (a layer without a cel is all transparent, so an
    emptied layer resets); nothing for a layer the file does not have, or a file that cannot be read."""
    try:
        sprite = ase.read(path)
    except (ValueError, OSError, KeyError, EOFError, zlib.error, struct.error) as e:
        warn_once(f"{path.name}: not read ({e}); a file Aseprite is still writing is read again on its next save")
        return {}
    out = {}
    for channel, name in {**ASE_LAYERS, "material": ASE_MATERIAL}.items():
        layer = sprite.layer(name)
        if layer is not None:
            out[channel] = layer.pixels if layer.pixels is not None else np.zeros((sprite.height, sprite.width, 4), np.uint8)
    return out


def over_seed(rgba, seed):
    """A channel layer's map: where the layer is not transparent its gray (luminance), elsewhere the game's value (`seed`)."""
    r, g, b = (rgba[..., i].astype(np.uint32) for i in range(3))
    gray = (r * 299 + g * 587 + b * 114 + 500) // 1000
    return np.where(rgba[..., 3] > 0, gray, seed).astype(np.uint8)


def layered_map(rgba, seed, preset=None):
    """A map from a channel layer's canvas (`rgba`, None: no layer), the Material layer's preset values (`preset`: (values, mask) from
    material_presets.expand, None: no preset anywhere) and the game's own map (`seed`). Per texel: the channel layer's gray where it is
    opaque and (where a preset applies) differs from the game's value, else the preset's value, else the game's value. (The layer is
    exported as the game's own values, so a pixel still at the game's value is untouched, not a stroke; to force the game's value over a
    preset, paint it a level off.)"""
    out = seed.astype(np.uint8)
    if preset is not None:
        values, mask = preset
        out = np.where(mask, values, out).astype(np.uint8)
    if rgba is None:
        return out
    painted = rgba[..., 3] > 0
    gray = over_seed(rgba, seed)
    if preset is not None:
        painted &= gray != seed
    return np.where(painted, gray, out).astype(np.uint8)


def neutral_map(channel, h, w):
    return np.full((h, w), pch.blank(channel), np.uint8)


def stale(path, array, was):
    """Whether `array` has to be written to `path`: it differs from the map there, or (no map there) from the seed `was`."""
    if path.exists():
        have = load(path, "L")
        return have.shape != array.shape or not np.array_equal(have, array)
    return was is None or not np.array_equal(array, was)


_catalog_lock = threading.Lock()


def import_package(args):
    result = run_import(args)
    print(f"import: {result['written']} maps for {len(result['changed'])} materials into {result['layer']}"
          + (f", {result['entries']} catalog entries added" if result["entries"] else ""))
    return 0


def run_import(args, only=None, say=print):
    """Imports what the painter left for the materials `only` (default: all). Returns {written, changed: [(label, rec, names)], entries, layer}."""
    folder = pathlib.Path(args.dir)
    seed = json.loads((folder / "seed.json").read_text())
    gltf_path = folder / "paint.gltf"
    gltf = json.loads(gltf_path.read_text()) if gltf_path.exists() else None
    layer = pathlib.Path(args.layer) if args.layer else user_layer()
    changed_labels, written = [], 0
    for label, rec in seed["labels"].items():
        if only is not None and label not in only:
            continue
        key, obj, h, w = rec["key"], rec["object"], rec["h"], rec["w"]
        label_safe = safe(label)
        images = find_images(folder, label_safe, gltf)

        def painted(channel, mode):
            """The painter's image of `channel` as the seed's shape (DirectX normals turned to OpenGL), and the pristine one."""
            seed_path = folder / "seed" / pathlib.Path(rec["files"][channel]).name
            now = resample(load(images[channel], mode), h, w, normal=channel == "normal")
            if channel == "normal" and is_directx(images[channel], args.normal_format):
                now = flip_green(now)
            return now, load(seed_path, mode)

        def differs(now, was):
            """A painter's export is the seed through its own float pipeline, a level off here and there (and a renormalised normal map
            too): a change of one level is not a stroke."""
            return np.abs(now.astype(int) - was).max() > 1

        def kept(now, was):
            return keep_seed(now, was) if args.keep_seed else now
        maps, exact, seeds = {}, set(), {}
        if "metallicroughness" in images:
            now, was = painted("metallicroughness", "RGB")
            for channel, (_, plane) in PACKED.items():
                if differs(now[..., plane], was[..., plane]):
                    maps[channel] = kept(now[..., plane], was[..., plane])
            if "occlusion" not in images and differs(now[..., 0], was[..., 0]):  # an ORM pack: R is the occlusion
                maps["occlusion"] = kept(now[..., 0], was[..., 0])
        material = images.get("material") if isinstance(images.get("material"), AseSource) else None
        presets = expanded = None
        for channel in pch.NAMES:
            layer_source = images.get(channel) if isinstance(images.get(channel), AseSource) else None
            mat = material if channel not in images or layer_source is not None else None  # (a PNG saved after the file is the newer: it wins)
            if channel in images or mat is not None:  # separate maps (Roughness, Metallic files), or the layers of the .aseprite
                was = painted_seed(folder, rec, channel, h, w)
                if layer_source is not None or mat is not None:  # the layers' pixels are the map exactly (no resampling, no tolerance)
                    rgba = layer_source.rgba if layer_source is not None else None
                    if any(s.rgba.shape[:2] != (h, w) for s in (layer_source, mat) if s is not None):
                        shape = (layer_source or mat).rgba.shape
                        warn_once(f"{label_safe}.aseprite is {shape[1]} x {shape[0]}, the texture {w} x {h}: not imported (do not resize the canvas)")
                        continue
                    if mat is not None and expanded is None:
                        presets = mp.load(warn=warn_once)
                        expanded = mp.expand(mat.rgba, presets)
                    seeds[channel] = was if was is not None else neutral_map(channel, h, w)
                    maps[channel] = layered_map(rgba, seeds[channel], (expanded or {}).get(channel))
                    exact.add(channel)
                    continue
                now = resample(load(images[channel], "L"), h, w)
                if was is None or differs(now, was):
                    maps[channel] = kept(now, was) if was is not None else now
        for channel, name, mode in (("normal", "normal", "RGB"), ("occlusion", "occlusion", "L")):
            if channel in images:
                now, was = painted(channel, mode)
                if differs(now, was):
                    maps[name] = kept(now, was)
        if "basecolor" in images:
            now, was = painted("basecolor", "RGBA")
            if not np.array_equal(now, was):
                say(f"import: {label}: the base colour was edited; it is not imported (the game's texture is the base colour)")
        if not maps:
            continue
        if rec.get("slices"):  # a model's page: each box of it is a texture of the game, cut from the atlas
            for sl in rec["slices"]:
                u0, v0, u1, v1 = sl["box"]
                dest = layer / sl["key"] / f"@{obj}"
                cut = {name: np.ascontiguousarray(array[v0:v1 + 1, u0:u1 + 1]) for name, array in maps.items()}
                was = {name: seed_slice(folder, rec, name, sl["box"]) for name in cut}
                cut = {name: array for name, array in cut.items()
                       if (stale(dest / f"{name}.png", array, was[name]) if name in exact
                           else was[name] is None or np.abs(array.astype(int) - was[name]).max() > 1)}
                if not cut:
                    continue
                dest.mkdir(parents=True, exist_ok=True)
                for name, array in cut.items():
                    save_image(dest / f"{name}.png", array)
                    written += 1
                write_normal_parts(dest, cut)
                changed_labels.append((label, {**rec, "key": sl["key"], "entry": sl.get("entry", ""), "w": u1 - u0 + 1, "h": v1 - v0 + 1}, sorted(cut)))
                say(f"import: {label}: {sl['key']}: {', '.join(sorted(cut))} -> {dest}")
            continue
        dest = layer / key / (f"@{obj}" if obj else "")
        maps = {name: array for name, array in maps.items() if name not in exact or stale(dest / f"{name}.png", array, seeds[name])}
        if not maps:
            continue
        dest.mkdir(parents=True, exist_ok=True)
        for name, array in maps.items():
            save_image(dest / f"{name}.png", np.ascontiguousarray(array))
            written += 1
        write_normal_parts(dest, maps)
        changed_labels.append((label, rec, sorted(maps)))
        say(f"import: {label}: {', '.join(sorted(maps))} -> {dest}")
    with _catalog_lock:
        entries = ensure_entries(args, seed, changed_labels)
    return {"written": written, "changed": changed_labels, "entries": entries, "layer": layer}


def seed_slice(folder, rec, name, box):
    """The seed's `name` map over a box of an atlas (roughness and metal from the packed image; normal, occlusion as they are)."""
    u0, v0, u1, v1 = box
    if name in pch.BY_CHANNEL:
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
    """The seed's map of a table channel (roughness, metal, height...): from the packed image's plane, or the channel's own image; None when
    the package has none (one made before the channel was in the table: the channel's blank value stands in)."""
    image, plane = pch.seed_image(channel)
    name = (rec.get("files") or {}).get(image)
    path = folder / "seed" / pathlib.Path(name).name if name else None
    if path is None or not path.exists():
        return None
    got = load(path, "RGB" if plane is not None else "L")
    return got[..., plane] if plane is not None else got


def ensure_entries(args, seed, changed):
    """A user catalog entry for each painted object (a map only applies where an entry matches): its texture on its room group or model,
    with the values the game drew that object's texture with until now (the entry it matched, resolved; the standard no-entry surface
    when it had none), so a first import changes only what was painted; but a painted height on a surface whose bump was 0 turns it to the standard
    (1), or the painting would show nothing. An entry that is there already is left as it is."""
    path = pathlib.Path(args.catalog) if args.catalog else user_catalog()
    data = json.loads(path.read_text()) if path.exists() else {"format": 2, "presets": {}, "entries": []}
    ids = {e["id"] for e in data.get("entries", [])}
    added = 0
    for label, rec, names in changed:
        key, obj = rec["key"], rec["object"]
        entry_id = f"PT-{key}" + (f"-{obj}" if obj else "")
        if entry_id in ids:
            continue
        entry = {"id": entry_id, "name": f"painted {key}" + (f" on {obj}" if obj else ""), "where": "paint_roundtrip",
                 "match": {"key": key, "size": [rec["w"], rec["h"]]}}
        values, extra = surface_of(catalog_files(path), rec, key, rec["w"], rec["h"], seed.get("zone"), seed.get("room"))
        entry.update(extra)
        entry.update({n: (bool(v) if n == "auto_height" else float(v)) for n, v in values.items()})
        if "height" in names and entry.get("bump", 1.0) <= 0:  # (a painted height shows through bump: an entry that had none gets the standard 1)
            entry["bump"] = STANDARD["bump"]
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


def say_now(*parts):
    print(*parts, flush=True)  # (a terminal redirected to a file or a pipe shows each line as it happens)


def watch_package(args, stop=None, say=say_now):
    """Imports each material's .aseprite file when it is saved: polls <dir>/aseprite every 0.2 s, and once a file's stamp has not moved for
    `args.debounce` seconds imports that material (as `import` does, the other materials untouched). `stop`: a threading.Event."""
    folder = pathlib.Path(args.dir)
    seed = json.loads((folder / "seed.json").read_text())
    by_file = {ase_path(folder, safe(label)): label for label in seed["labels"]}
    ns = argparse.Namespace(dir=str(folder), layer=getattr(args, "layer", None), catalog=getattr(args, "catalog", None),
                            normal_format="auto", keep_seed=False)
    seen = {}
    for path in by_file:  # (what is there now is not an edit: only a later save is)
        if path.exists():
            seen[path] = (path.stat().st_mtime_ns, path.stat().st_size)
    pending = {}
    say(f"watch: {len(by_file)} materials in {folder / 'aseprite'}; save in Aseprite (Ctrl+S) to import; Ctrl+C stops")
    stop = stop or threading.Event()
    try:
        while not stop.is_set():
            now = time.monotonic()
            for path, label in by_file.items():
                try:
                    st = path.stat()
                except OSError:
                    continue
                stamp = (st.st_mtime_ns, st.st_size)
                if seen.get(path) != stamp:
                    seen[path] = stamp
                    pending[path] = now
            for path, since in list(pending.items()):
                if now - since < args.debounce:
                    continue
                del pending[path]
                started = time.monotonic()
                result = run_import(ns, only={by_file[path]}, say=lambda *_: None)
                names = sorted({n for _, _, ns_ in result["changed"] for n in ns_})
                say(f"watch: {time.strftime('%H:%M:%S')} {path.name}: " + (f"{', '.join(names)} imported" if names else "nothing changed")
                    + f" ({(time.monotonic() - started) * 1000:.0f} ms)")
            stop.wait(0.2)
    except KeyboardInterrupt:
        say("watch: stopped")
    return 0


# ---------------------------------------------------------------------------------------------- pack / unpack

PACK_FORMAT = 1


def gray_of(rgba):
    r, g, b = (rgba[..., i].astype(np.uint32) for i in range(3))
    return ((r * 299 + g * 587 + b * 114 + 500) // 1000).astype(np.uint8)


def pack_package(args, say=say_now):
    """One small file (`.roodpack.zip`) of what a painter painted in a package's .aseprite files, safe to share: the model's content key,
    and per page only the pixels the painter changed (a channel layer's pixels that differ from the game's own value, the Material layer's
    swatch pixels), as PNGs that are transparent everywhere else. The reference layer (the game's texture) and the game's own map values
    are never read into it. `unpack` applies it onto an export of the same model."""
    import io
    import zipfile
    folder = pathlib.Path(args.dir)
    seed = json.loads((folder / "seed.json").read_text())
    pages, files = {}, {}
    for label, rec in seed["labels"].items():
        path = ase_path(folder, safe(label))
        if not path.exists():
            continue
        h, w = rec["h"], rec["w"]
        got = read_ase_channels(path)
        kept = {}
        for channel, canvas in got.items():
            if canvas.shape[:2] != (h, w):
                raise SystemExit(f"pack: {path.name}: the canvas is {canvas.shape[1]} x {canvas.shape[0]}, not {w} x {h}: it was resized")
            if channel == "material":
                painted = canvas[..., 3] > 0
            else:
                was = painted_seed(folder, rec, channel, h, w)
                painted = (canvas[..., 3] > 0) & (gray_of(canvas) != (was if was is not None else neutral_map(channel, h, w)))
            if not painted.any():
                continue
            out = np.zeros((h, w, 4), np.uint8)
            out[painted] = canvas[painted]
            out[painted, 3] = 255
            buffer = io.BytesIO()
            Image.fromarray(out, "RGBA").save(buffer, "PNG", optimize=True)
            files[f"{safe(label)}/{channel}.png"] = buffer.getvalue()
            kept[channel] = int(painted.sum())
        if kept:
            pages[label] = {"w": w, "h": h, "key": rec.get("key"), "object": rec.get("object"), "painted_texels": kept}
    if not pages:
        say("pack: nothing painted in the .aseprite files of " + str(folder) + " (save in Aseprite first)")
        return 1
    meta = {"format": PACK_FORMAT, "target": seed.get("target"), "model": seed.get("model"), "model_legacy": seed.get("model_legacy"),
            "zone": seed.get("zone"), "room": seed.get("room"), "pages": pages, "about": "paint_roundtrip.py pack: painted pixels only (no game texture); apply with paint_roundtrip.py unpack"}
    name = f"{seed.get('model') or 'room-%s-%s' % (seed.get('zone'), seed.get('room'))}.roodpack.zip"
    target = pathlib.Path(args.out) if args.out else folder / name
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("pack.json", json.dumps(meta, indent=1))
        for member, data in sorted(files.items()):
            z.writestr(member, data)
    say(f"pack: {'model ' + str(seed.get('model')) if seed.get('model') else 'room %s-%s' % (seed.get('zone'), seed.get('room'))}: {len(pages)} pages, {sum(sum(p['painted_texels'].values()) for p in pages.values())} painted texels, "
        f"{target.stat().st_size} bytes -> {target}")
    return 0


def unpack_package(args, say=say_now):
    """Applies a pack onto a fresh export of the same model (`export model ... --painter aseprite`): its pixels go into the package's .aseprite
    files (the file stays what Aseprite opens, so the work can go on there) and are imported, as a save in Aseprite would."""
    import io
    import zipfile
    folder = pathlib.Path(args.dir)
    seed = json.loads((folder / "seed.json").read_text())
    with zipfile.ZipFile(args.pack) as z:
        meta = json.loads(z.read("pack.json"))
        if meta.get("format") != PACK_FORMAT:
            raise SystemExit(f"unpack: {args.pack} is a pack of format {meta.get('format')}; this tool reads {PACK_FORMAT}")
        if [meta.get(k) for k in ("target", "model", "zone", "room")] != [seed.get(k) for k in ("target", "model", "zone", "room")]:
            raise SystemExit(f"unpack: the pack is for {meta.get('model') or ('room %s-%s' % (meta.get('zone'), meta.get('room')))}, this export is of "
                             f"{seed.get('model') or ('room %s-%s' % (seed.get('zone'), seed.get('room')))}: export what the pack is for "
                             "(a model: summon it in the model studio, then export with --model studio)")
        applied = []
        for label in meta["pages"]:
            rec = seed["labels"].get(label)
            path = ase_path(folder, safe(label))
            if rec is None or not path.exists():
                say(f"unpack: {label}: not in this export" + ("" if rec is None else " (export with --painter aseprite)") + "; skipped")
                continue
            sprite = ase.read(path)
            h, w = sprite.height, sprite.width
            for channel, name in {**ASE_LAYERS, "material": ASE_MATERIAL}.items():
                member = f"{safe(label)}/{channel}.png"
                if member not in z.namelist():
                    continue
                layer = sprite.layer(name)
                if layer is None:
                    say(f"unpack: {label}: the file has no {name} layer; skipped")
                    continue
                painted = np.asarray(Image.open(io.BytesIO(z.read(member))).convert("RGBA"))
                if painted.shape[:2] != (h, w):
                    raise SystemExit(f"unpack: {label}: the pack's {name} is {painted.shape[1]} x {painted.shape[0]}, the file {w} x {h}")
                pixels = layer.pixels.copy() if layer.pixels is not None else np.zeros((h, w, 4), np.uint8)
                mask = painted[..., 3] > 0
                pixels[mask] = painted[mask]
                layer.pixels = pixels
            ase.write(path, w, h, [{"name": l.name, "pixels": l.pixels if l.pixels is not None else np.zeros((h, w, 4), np.uint8),
                                    "visible": l.visible, "locked": l.locked, "opacity": l.opacity, "blend": l.blend}
                                   for l in sprite.layers if l.kind == ase.KIND_NORMAL], palette=sprite.palette or None)
            applied.append(label)
    ns = argparse.Namespace(dir=str(folder), layer=getattr(args, "layer", None), catalog=getattr(args, "catalog", None), normal_format="auto", keep_seed=False)
    result = run_import(ns, only=set(applied), say=say)
    say(f"unpack: {len(applied)} pages applied, {result['written']} maps written into {result['layer']}")
    return 0 if applied else 1


def open_painter(out, painter):
    """Starts a painter on the package's paint.gltf, detached: SubCraft when it is on the PATH (and `painter` is auto or subcraft),
    else Blender. Returns what it started, or None."""
    gltf = pathlib.Path(out) / "paint.gltf"
    if painter == "aseprite":
        files = sorted((pathlib.Path(out) / "aseprite").glob("*.aseprite"))
        if not shutil.which("aseprite") or not files or len(files) > 6:
            return None, pathlib.Path(out) / "aseprite"
        subprocess.Popen(["aseprite", *map(str, files)], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        return "Aseprite", files[0]
    if painter in ("auto", "subcraft") and shutil.which("subcraft"):
        command, name = ["subcraft", str(gltf)], "SubCraft"
        try:  # a project with the game's texture already in it (subcraft_project.py); the plain glTF when its format is not known
            import subcraft_project
            command[1] = str(subcraft_project.build(out, pathlib.Path(out) / "paint.subcraft"))
        except subcraft_project.FormatMismatch as e:
            print(f"paint: {e}", file=sys.stderr)
            print("paint: opening paint.gltf instead: its base colour has to be added by hand (see Materials.md)", file=sys.stderr)
    elif painter in ("auto", "blender") and shutil.which("blender"):
        command, name = ["blender", "--python-expr", f"import bpy; bpy.ops.import_scene.gltf(filepath={str(gltf)!r})"], "Blender"
    else:
        return None, gltf
    subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    return name, (pathlib.Path(command[1]) if name == "SubCraft" else gltf)


def refresh_palettes(args, say=print):
    """The palette of each .aseprite file of the package set to the presets' swatches (named) and the grays; nothing else in the file changes."""
    presets = mp.load()
    entries = mp.palette(presets)
    files = sorted((pathlib.Path(args.dir) / "aseprite").glob("*.aseprite"))
    for path in files:
        ase.set_palette(path, entries)
    say(f"palette: {len(presets)} presets ({', '.join(p['name'] for p in presets)}) in {len(files)} files; close and reopen them in Aseprite")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    e = sub.add_parser("export")
    pt = sub.add_parser("paint", help="export, then open the package in SubCraft (or Blender): the arguments of export, plus --painter")
    e.add_argument("target", choices=["room", "model"])
    e.add_argument("--zone", type=int)
    e.add_argument("--room", type=int)
    e.add_argument("--model", help="a model's content key (10 hex digits: RPC material pick's object text without the m) or its legacy id (8)")
    e.add_argument("--model-at", help="X,Y: the model under a pixel of the view")
    e.add_argument("--bake", action="append", default=[], help="a bake's materials folder (room_blender.py <out>/materials) to seed the maps from, the geometry-based normals included; also $ROOD_MATERIALS_BAKED")
    e.add_argument("--out")
    e.add_argument("--game", metavar="SOCKET", help="export from this running game (its RPC socket: $ROOD_RPC, pc/build/boot/rpc-live.sock for pc/run-game.sh) instead of starting one; --model studio is the model summoned in its model studio")
    e.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    painters = ["auto", "subcraft", "blender", "aseprite"]
    e.add_argument("--painter", choices=painters, default="auto", help="aseprite: also write <out>/aseprite/<material>.aseprite (Metal and Roughness layers for a pixel painter; Materials.md)")
    pt.add_argument("--painter", choices=painters, default="auto")
    for flag in e._actions[1:]:
        if flag.dest == "painter":
            continue
        if flag.option_strings or flag.dest == "target":
            kwargs = {k: getattr(flag, k) for k in ("default", "help", "choices") if getattr(flag, k, None) is not None}
            pt.add_argument(*(flag.option_strings or [flag.dest]), **({"action": "append"} if flag.dest == "bake" else {}), **kwargs)
    i = sub.add_parser("import")
    i.add_argument("dir")
    i.add_argument("--layer", help="the maps' layer (default: the user layer, ROOD_MATERIALS or <lighting>/materials)")
    i.add_argument("--catalog", help="the user catalog file (default: ROOD_MATERIAL_CATALOG_USER or <lighting>/materials.json)")
    i.add_argument("--normal-format", choices=["auto", "opengl", "directx"], default="auto",
                   help="the painter's normal maps: auto (DirectX when the file's name says so: normalDX, Normal_DirectX), opengl, or directx (green is flipped)")
    i.add_argument("--keep-seed", action="store_true",
                   help="where a painted map still has the painter's flat fill (most of the image), keep the game's own map there instead")
    pk = sub.add_parser("pack", help="write one small file (.roodpack.zip) of what you painted in a model's package: painted pixels only, no game texture, safe to share")
    pk.add_argument("dir")
    pk.add_argument("--out", help="the file (default: <dir>/<model>.roodpack.zip)")
    up = sub.add_parser("unpack", help="apply a .roodpack.zip onto a fresh export of the same model (export model ... --painter aseprite): into its .aseprite files and the game's maps")
    up.add_argument("pack")
    up.add_argument("dir")
    up.add_argument("--layer", help="the maps' layer (default: the user layer)")
    up.add_argument("--catalog", help="the user catalog file")
    pal = sub.add_parser("palette", help="rewrite the palette of the package's .aseprite files from the preset files (material_presets.py); pixels untouched; close the files in Aseprite first")
    pal.add_argument("dir")
    rk = sub.add_parser("rekey", help="move the user's model paint to the models' content keys: the game's ids into model_aliases.json, with --apply the entries and maps (copied)")
    rk.add_argument("--visit", action="append", metavar="Z,R", help="a room to visit first so its models are loaded (repeatable)")
    rk.add_argument("--apply", action="store_true", help="write model_aliases.json and the migrated catalog and maps (else a dry run)")
    rk.add_argument("--layer", help="the maps' layer (default: the user layer)")
    rk.add_argument("--catalog", help="the user catalog file")
    rk.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"), help="the memory card the game starts from")
    for name, text in (("watch", "import a material's .aseprite file each time it is saved (Ctrl+C stops)"),
                       ("live", "watch, and take strokes live from the Aseprite extension over a WebSocket (pc/tools/aseprite/)")):
        w = sub.add_parser(name, help=text)
        w.add_argument("dir")
        w.add_argument("--layer", help="the maps' layer (default: the user layer)")
        w.add_argument("--catalog", help="the user catalog file")
        w.add_argument("--debounce", type=float, default=0.3, help="seconds a file must be still before it is imported")
        if name == "live":
            w.add_argument("--port", type=int, default=47480, help="the WebSocket port on localhost (the extension's default)")
            w.add_argument("--rpc", action="append", metavar="SOCKET", help="an RPC socket to paint into, repeatable: rood-studio's and the game's (default: ROOD_RPC, else rpc-studio.sock (pc/run-studio.sh) and rpc-live.sock (pc/run-game.sh) in pc/build/boot, each when it runs); the texel locator talks to the first that is up")
            w.add_argument("--no-watch", action="store_true", help="do not also import saved files")
    args = parser.parse_args()
    if args.command == "watch":
        return watch_package(args)
    if args.command == "palette":
        return refresh_palettes(args)
    if args.command == "pack":
        return pack_package(args)
    if args.command == "unpack":
        return unpack_package(args)
    if args.command == "rekey":
        return rekey(args)
    if args.command == "live":
        import paint_live
        return paint_live.live(args)
    if args.command in ("export", "paint") and (args.zone is None) != (args.room is None):
        parser.error("--zone and --room go together")
    if args.command == "import":
        return import_package(args)
    status = export_package(args)
    if args.command == "paint" and not status:
        out = args.out or ROOT / "pc/user/paint" / (f"model-{args.model}" if args.target == "model" and args.model else
                                                    f"room-{args.zone}-{args.room}" if args.zone is not None else args.target)
        started, opened = open_painter(out, args.painter)
        print((f"paint: Aseprite opened {opened}; start the live link in another terminal: python3 pc/tools/paint_roundtrip.py live {out}"
               f" (Ctrl+S in Aseprite keeps your work)" if started == "Aseprite" else
               f"paint: {started} opened {opened}; paint, export the maps into that folder, then: "
               f"python3 pc/tools/paint_roundtrip.py import {out}") if started else
              f"paint: Aseprite is not on the PATH (or there are more than six files); open the files in {opened} by hand, then: python3 pc/tools/paint_roundtrip.py live {out}"
              if args.painter == "aseprite" else
              f"paint: neither subcraft nor blender is on the PATH; open {pathlib.Path(out) / 'paint.gltf'} in your painter")
    return status


if __name__ == "__main__":
    sys.exit(main())
