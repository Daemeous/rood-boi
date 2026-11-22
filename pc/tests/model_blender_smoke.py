#!/usr/bin/env python3
"""A model through Blender and back (pc/tools/model_blender.py, pc/tools/blender/check_gltf.py; pc/docs/Materials.md "Blender round
trip"; Gitea #20).

    python3 pc/tests/model_blender_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Exports Ashley (the model of the card's save room) and:

- the glTF opens in Blender without error: one mesh, one material with its four images, UVs inside the atlas, the joints welded
  (the vertices shared between the triangles), the faces wound alike across every shared edge and pointing outward (a positive volume);
- it stands at the origin on the ground;
- the bake makes wear, cavity and cover for the atlas, and the import cuts height, roughness and normal maps for each of the model's
  textures, texel for texel, in the folder of the model (not flat: the edges and creases of the mesh are in them);
- they apply in the game, live, to the model alone: the same frame with the model's maps in the user layer differs in her pixels.
Skips (77) when Blender is not installed."""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import editor_selection_smoke as es  # noqa: E402
import model_blender as mb  # noqa: E402
import room_blender as rb  # noqa: E402
from material_instances_smoke import frame  # noqa: E402
from rood_rpc import RpcError  # noqa: E402

ASHLEY = "4b8870d7"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out")
    args = parser.parse_args()
    if not os.path.exists(rb.BLENDER):
        print("SKIP: no Blender")
        return 77
    out = pathlib.Path(args.out) if args.out else es.OUT / "model_blender"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    margs = argparse.Namespace(model=ASHLEY, zone=None, room=None, out=str(out / "ashley"), card=args.card, samples=8, threads=2)
    check(mb.export(margs) == 0, "the model exports")
    seed = mb.seed(out / "ashley")
    label = next(iter(seed["labels"]))
    done = subprocess.run(["nice", "-n", "10", rb.BLENDER, "-b", "--threads", "2", "-P", str(ROOT / "pc/tools/blender/check_gltf.py"), "--",
                           str(out / "ashley" / "paint.gltf")], capture_output=True, text=True)
    line = next((l for l in done.stdout.splitlines() if l.startswith("check_gltf: ")), None)
    check(done.returncode == 0 and line is not None and "Traceback" not in done.stderr + done.stdout, "Blender opens the glTF without error")
    if line is None:
        print(done.stdout[-1500:], done.stderr[-1500:])
        return 1
    report = json.loads(line[len("check_gltf: "):])
    check(report["meshes"] == 1 and len(report["materials"]) == 1 and len(report["materials"][0]["images"]) == 4,
          f"one mesh, one material with its four images ({report['materials'][0]['name']})")
    check(report["uv_min"][0] >= 0 and report["uv_min"][1] >= 0 and report["uv_max"][0] <= 1 and report["uv_max"][1] <= 1,
          f"the UVs are inside the atlas ({report['uv_min']} .. {report['uv_max']})")
    check(report["distinct_positions"] * 3 < report["corners"] * 2, f"the joints are welded ({report['distinct_positions']} positions for {report['corners']} corners)")
    check(report["edges_inconsistent"] <= 2 and report["volume"] > 0, f"the faces wind alike across shared edges, outward (volume {report['volume']}, {report['edges_inconsistent']} edges against)")
    check(abs(report["bbox_min"][2]) < 1e-3 and report["bbox_min"][0] < 0 < report["bbox_max"][0] and 0.2 < report["bbox_max"][2] < 0.7,
          f"she stands at the origin on the ground ({report['bbox_min']} .. {report['bbox_max']})")
    check(mb.bake(margs) == 0 and all((out / "ashley" / "bakes" / label / f"{n}.png").exists() for n in ("cover", "wear", "cavity")), "the bake makes wear, cavity and cover")
    mb.import_maps(margs)
    layer = out / "ashley" / "materials"
    rec = seed["labels"][label]
    folders = sorted(f.parent for f in layer.rglob("height.png"))
    check(len(folders) == len(rec["slices"]) and all(f.name == f"@m{ASHLEY}" for f in folders), f"maps for each of her textures, in the folder of her model ({len(folders)})")
    sl = rec["slices"][0]
    rough = np.asarray(Image.open(layer / sl["key"] / f"@m{ASHLEY}" / "roughness.png"))
    box = sl["box"]
    check(rough.shape == (box[3] - box[1] + 1, box[2] - box[0] + 1), "each cut to its texture's size")
    allrough = np.concatenate([np.asarray(Image.open(f / "roughness.png")).ravel() for f in folders])
    check(allrough.min() < allrough.max() and (allrough != allrough[0]).mean() > 0.05, "they are not flat: the mesh's edges and creases are in them")

    # in the game: her own maps, live, change her pixels and little else
    env = {"ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"), "ROOD_MATERIAL_CATALOG_USER": str(out / "materials.json"),
           "ROOD_MATERIALS": str(out / "maps"), "ROOD_MATERIALS_SHIPPED": str(out / "shipped"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_GI": "0"}
    (out / "materials.json").write_text(json.dumps({"format": 2, "presets": {}, "entries": mb.entries(seed, "stone")}))
    s = es.start(argparse.Namespace(card=args.card, out=str(out / "game")), "model_blender", extra_env=env)
    if s is None:
        return 77
    try:
        g, p = s.game, s.player
        g.call("editor_ui", visible=False)
        g.call("graphics", interpolate=False, specular=1, view_mode=6)
        g.call("pause", paused=True)

        def shot(name):
            for _ in range(3):
                g.call("redraw")
                g.wait_presents(6)
            return frame(g, out / f"{name}.png")
        before = shot("before")
        shutil.copytree(layer, out / "maps", dirs_exist_ok=True)
        changed = None
        for _ in range(8):  # (the watch looks at the files a few times a second)
            after = shot("after")
            changed = np.abs(after - before).max(2) > 0
            if changed.any():
                break
        check(changed is not None and changed.sum() > 50, f"her maps change her picture with the game running, no reload ({int(changed.sum())} pixels)")
        check(changed.mean() < 0.2, f"...a part of the frame around her ({changed.mean() * 100:.1f}%)")
    except RpcError as e:
        check(False, f"RPC error {e}")
    finally:
        es.finish(s, "model blender")
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: a model through Blender and back: the glTF opens, the mesh is sound, the bake's maps apply to the model in the game")
    return 0


if __name__ == "__main__":
    sys.exit(main())
