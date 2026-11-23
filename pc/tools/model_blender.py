#!/usr/bin/env python3
"""One model (a character, weapon or prop) through Blender and back (Gitea #20; pc/docs/Materials.md, "Blender round trip").

    python3 pc/tools/model_blender.py export --model HASH [--zone Z --room R] [--out DIR]
    python3 pc/tools/model_blender.py bake   [--out DIR] [--samples N]
    python3 pc/tools/model_blender.py import [--out DIR]
    python3 pc/tools/model_blender.py shot   [--out DIR] [--zone Z --room R]
    python3 pc/tools/model_blender.py all    --model HASH [--zone Z --room R] [--out DIR]

room_blender.py's round trip for a model: the same bake (pc/tools/blender/bake_room.py), the same maps (room_blender.json,
the `model` section's values over the room's), texel for texel on the model's atlas.

export  The model as the game draws it now (paint_roundtrip.py's package: paint.gltf, one atlas per texture page and palette,
        `m<model>_p<page>_c<palette>`; the pose is the game's, faces turned outward) into <out>; with --zone and --room, in
        that room with its enemies.
bake    Blender bakes each atlas from the model's own mesh: edge wear, cavity, and which texels a triangle covers. A texel two
        triangles share (a left and a right arm drawn from the same texels) holds the one baked last.
import  Each texture of the model (a box of the atlas: the game's texture, its size) gets height, roughness and normal maps
        cut from the bakes into <out>/materials/<texture>/@m<model>/: the folder the game reads for an entry keyed by that
        model, so they apply to every copy of it (a palette variant of the model is other textures: export and bake it too).
shot    The model in the game twice, the same frame: an entry for each texture (the preset) with the maps
        off, then on (put into the user layer the game watches). <out>/shots/before_after.png, and the views of room_blender.json
        model.shot.views (graphics view_mode: the shading normals with the relief, the material target) as <name>_before_after.png.

Everything under <out> (default pc/build/model_blender/<model>) is game art derived from the game: local only, never
committed. Only these tools are."""
import argparse
import json
import math
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ.setdefault("ROOD_ARCH", "64")  # the 32-bit build is retired: run by hand, these tools use the 64-bit game
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import paint_roundtrip as pr  # noqa: E402
import room_blender as rb  # noqa: E402


def settings():
    """room_blender.json with the `model` section's bake and maps laid over the room's."""
    cfg = rb.config()
    model = cfg["model"]
    cfg["bake"] = {**cfg["bake"], **model.get("bake", {})}
    cfg["maps"] = {**cfg["maps"], **model.get("maps", {})}
    return cfg


def model_dir(args):
    return pathlib.Path(args.out) if args.out else ROOT / "pc/build/model_blender" / (args.model or "current")


def seed(out):
    return json.loads((out / "seed.json").read_text())


def export(args):
    if not args.model:
        raise SystemExit("model_blender: --model ID (RPC material pick's object text without the m: the 10 digit content key, or the 8 digit legacy id)")
    out = model_dir(args)
    code = pr.export_package(argparse.Namespace(target="model", zone=args.zone, room=args.room, model=args.model, model_at=None,
                                                bake=[], out=str(out), card=args.card))
    if code:
        return code
    # what the bake needs of the package: each atlas (material) and its size
    labels = seed(out)["labels"]
    (out / "export.json").write_text(json.dumps({"textures": {safe: {"w": r["w"], "h": r["h"]} for safe, r in
                                                              ((pr.safe(label), r) for label, r in labels.items())}}, indent=1))
    return 0


def bake(args):
    out = model_dir(args)
    cfg = settings()
    code = rb.run_bake(out / "paint.gltf", out / "export.json", out / "bakes", args.samples or cfg["samples"], args.threads, cfg["bake"])
    if not code:
        print(f"bake: {len(list((out / 'bakes').glob('*/wear.png')))} atlases baked -> {out / 'bakes'}")
    return code


def import_maps(args):
    out = model_dir(args)
    meta = seed(out)
    p = settings()["maps"]
    layer = out / "materials"
    shutil.rmtree(layer, ignore_errors=True)
    done = 0
    for label, rec in meta["labels"].items():
        d = out / "bakes" / pr.safe(label)
        if not (d / "wear.png").exists():
            continue
        wear, cavity, cover = rb.gray(d / "wear.png"), rb.gray(d / "cavity.png"), rb.gray(d / "cover.png")
        if wear.shape != (rec["h"], rec["w"]):
            raise SystemExit(f"model_blender: {label}: the bake is {wear.shape[::-1]}, the atlas {rec['w']} x {rec['h']}")
        for sl in rec["slices"]:  # each box of the atlas is a texture of the game: its maps are made from its own texels
            u0, v0, u1, v1 = sl["box"]
            box = (slice(v0, v1 + 1), slice(u0, u1 + 1))
            height, rough, normal = rb.maps(wear[box], cavity[box], cover[box], p)
            dest = layer / sl["key"] / f"@{rec['object']}"
            dest.mkdir(parents=True, exist_ok=True)
            for name, array, mode in (("height", height, "L"), ("roughness", rough, "L"), ("normal", normal, "RGB")):
                Image.fromarray(array, mode).save(dest / f"{name}.png")
            done += 1
    print(f"import: {done} textures' height, roughness and normal maps -> {layer} (the baked layer: $ROOD_MATERIALS_BAKED)")
    return 0


def entries(meta, preset):
    """A catalog entry for each texture of the model: its own, keyed by the model (every copy of it)."""
    out = []
    for label, rec in meta["labels"].items():
        for sl in rec["slices"]:
            u0, v0, u1, v1 = sl["box"]
            out.append({"id": f"MB-{sl['key']}-{rec['model']}", "name": f"model_blender {sl['key']} on {rec['object']}",
                        "match": {"key": sl["key"], "size": [u1 - u0 + 1, v1 - v0 + 1]}, "preset": preset,
                        "object": {"models": [rec["model"]]}})
    return out


def shot(args):
    import material_rollout
    out = model_dir(args)
    meta = seed(out)
    cfg = settings()
    framing = cfg["model"]["shot"]
    shots = out / "shots"
    shutil.rmtree(shots, ignore_errors=True)
    shots.mkdir(parents=True)
    scratch = out / "shot-game"
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    # the entries (the preset on each texture) and no maps: the relief the game derives from the texture itself
    (scratch / "materials.json").write_text(json.dumps({"format": 2, "presets": {}, "entries": entries(meta, cfg["model"]["preset"])}))
    (scratch / "maps").mkdir()
    os.environ["ROOD_MATERIALS_BAKED"] = str(scratch / "no-maps")
    os.environ["ROOD_MATERIAL_CATALOG_USER"] = str(scratch / "materials.json")
    game = material_rollout.launch(scratch, argparse.Namespace(card=args.card))
    try:
        if args.zone is not None:
            rb.goto(game, args.zone, args.room, enemies=True)
        game.run(framing["settle"])
        dump = scratch / "model.obj"
        game.call("material", action="model_dump", model=meta["model"], path=str(dump))
        points = rb.parse_dump(dump)[0].reshape(-1, 3)
        lo, hi = points.min(axis=0), points.max(axis=0)
        target, size = (lo + hi) / 2, float((hi - lo).max())
        yaw, pitch = math.radians(args.yaw), math.radians(framing["pitch"])
        forward = np.array([math.cos(pitch) * math.sin(yaw), -math.sin(pitch), math.cos(pitch) * math.cos(yaw)], np.float32)
        eye = target - forward * size * framing["distance"]
        game.call("editor_camera", x=float(eye[0]), y=float(eye[1]), z=float(eye[2]), yaw=args.yaw, pitch=framing["pitch"])
        game.call("editor_ui", visible=False)
        game.run(4)  # (the camera takes a few frames; the model hardly moves in them)
        game.call("pause", paused=True)

        def frame(name, view_mode=0):
            """The picture once it has settled (the exposure and the accumulation converge over some frames of a paused game)."""
            game.call("graphics", view_mode=view_mode)
            last = None
            for _ in range(framing["converge"]):
                for _ in range(4):
                    game.call("redraw")
                    game.wait_presents(6)
                path = game.present_screenshot(str(shots / f"{name}.ppm"))
                now = np.asarray(Image.open(path).convert("RGB"), np.int16)
                if last is not None and not (np.abs(now - last).max(axis=2) > 0).any():
                    break
                last = now
            Image.fromarray(now.astype(np.uint8)).save(shots / f"{name}.png")
            return Image.open(shots / f"{name}.png")
        pairs = {}
        for name, mode in framing["views"].items():  # (the maps off: the entries' preset alone, then the maps in the user layer the game watches)
            pairs[name] = [frame(f"before_{name}" if name else "before", mode)]
        shutil.copytree(out / "materials", scratch / "maps", dirs_exist_ok=True)
        for name, mode in framing["views"].items():
            pairs[name].append(frame(f"after_{name}" if name else "after", mode))
        game.call("pause", paused=False)
    finally:
        material_rollout.kill(game)
    for name, (before, after) in pairs.items():
        rb.side_by_side(before, after, shots / (f"{name}_before_after.png" if name else "before_after.png"))
        changed = int((np.abs(np.asarray(before, np.int16) - np.asarray(after, np.int16)).max(axis=2) > 0).sum())
        print(f"shot: {name or 'lit'}: {changed} pixels differ")
    print(f"shot: {shots}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["export", "bake", "import", "shot", "all"])
    parser.add_argument("--model", help="the model's content hash (8 hex digits)")
    parser.add_argument("--zone", type=int)
    parser.add_argument("--room", type=int)
    parser.add_argument("--out")
    parser.add_argument("--card", default=str(ROOT / "pc/build/test-card1.mcd"))
    parser.add_argument("--samples", type=int, help="Cycles samples per bake (default: room_blender.json)")
    parser.add_argument("--threads", type=int, default=4, help="Blender's threads (default 4)")
    parser.add_argument("--yaw", type=float, default=0.0, help="shot: the camera's yaw in degrees (the way it looks along the ground)")
    args = parser.parse_args()
    if (args.zone is None) != (args.room is None):
        parser.error("--zone and --room go together")
    steps = ["export", "bake", "import", "shot"] if args.command == "all" else [args.command]
    for step in steps:
        code = {"export": export, "bake": bake, "import": import_maps, "shot": shot}[step](args)
        if code:
            return code
    return 0


if __name__ == "__main__":
    sys.exit(main())
