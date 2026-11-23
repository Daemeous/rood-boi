#!/usr/bin/env python3
"""A model's paint follows its content key, and the user's existing Ashley paint still applies (model studio P3; paint_roundtrip.py rekey).

    python3 pc/tests/model_key_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--source DIR]

Reads (never writes) the user's lighting folder (pc/user/lighting, or --source): the catalog entries that name Ashley's legacy model id and her map
folders are COPIED into the test's scratch folder; the game runs on the scratch copy only. In the save room, with the game held and the material view
(Vulkan), per state a still picture:

- the paint applies: with the user's entries and maps (the legacy id, as always) the picture differs from one with none;
- RPC `material pick` names Ashley by her content key (10 hex digits, `legacy` beside it) and `material model_ids` pairs the two;
- migrated: after rekey_tree (the entries name the content key, the maps copied to @m<content>), the picture is byte-identical to the legacy one;
- the legacy folders gone, the content-key folders alone give the same picture;
- another page: entries naming a legacy id that is not hers, with only a model_aliases.json line to say it is her model, give the same picture
  (the alias is what lets a model loaded at another texture page find its paint).
Skipped (77) without the user's Ashley paint."""
import argparse
import json
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
os.environ.setdefault("ROOD_ARCH", "64")
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import editor_selection_smoke as es  # noqa: E402
import paint_roundtrip as pr  # noqa: E402
from rood_rpc import RpcError  # noqa: E402

LEGACY = "4b8870d7"  # Ashley's legacy model id in the owner's paint (the model as loaded at the save room's page)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out")
    parser.add_argument("--source", default=str(ROOT / "pc/user/lighting"))
    args = parser.parse_args()
    source = pathlib.Path(args.source)
    try:
        catalog = json.loads((source / "materials.json").read_text())
    except (OSError, ValueError):
        print("SKIP: no user catalog", source)
        return 77
    mine = [e for e in catalog.get("entries", []) if LEGACY in (e.get("object") or {}).get("models", [])]
    if not mine:
        print(f"SKIP: no user paint for model {LEGACY}")
        return 77
    out = pathlib.Path(args.out) if args.out else es.OUT / "model_key"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    maps, cat, aliases = out / "maps", out / "materials.json", out / "model_aliases.json"
    for e in mine:  # (a copy of the model's entries and map folders: read from the user's tree, written only here)
        src = source / "materials" / e["match"]["key"] / f"@m{LEGACY}"
        if src.is_dir():
            shutil.copytree(src, maps / e["match"]["key"] / f"@m{LEGACY}")
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def write_catalog(entries):
        cat.write_text(json.dumps({"format": 2, "presets": {}, "entries": entries}))
    write_catalog(mine)
    env = {"ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"), "ROOD_MATERIAL_CATALOG_USER": str(cat),
           "ROOD_MATERIALS": str(maps), "ROOD_MATERIALS_SHIPPED": str(out / "shipped"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_MODEL_ALIASES": str(aliases), "ROOD_GI": "0"}
    s = es.start(argparse.Namespace(card=args.card, out=str(out / "game")), "model_key_smoke", extra_env=env)
    if s is None:
        return 77
    try:
        g, p = s.game, s.player
        feet = s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]
        g.call("editor_ui", visible=False)
        g.call("graphics", interpolate=False, specular=1, view_mode=6)

        def shot(name):
            g.call("material", action="reload")
            for _ in range(3):
                g.call("redraw")
                g.wait_presents(6)
            g.present_screenshot(str(out / f"{name}.png"))
            return np.asarray(Image.open(out / f"{name}.png").convert("RGB")).astype(np.int32)

        def differing(a, b):
            return int((np.abs(a - b).max(2) > 0).sum())
        # Ashley: her ids
        ids = g.call("material", action="model_ids")["models"]
        pair = next((r for r in ids if r["legacy"] == LEGACY), None)
        check(pair is not None and len(pair["content"]) == 10, f"the game pairs her legacy id with a content key: {pair}")
        if pair is None:
            return 1
        content = pair["content"]
        aroundher = None
        for dx, dy in ((0, -60), (0, -90), (0, -30), (15, -60), (-15, -60)):
            got = g.call("material", action="pick", x=feet[0] + dx, y=feet[1] + dy)
            if got.get("kind") == "model" and got.get("object", {}).get("legacy") == LEGACY:
                aroundher = got
                break
        check(aroundher is not None and aroundher["object"]["text"] == f"m{content}", f"material pick names her by the content key, the legacy id beside it: {aroundher and aroundher['object']}")

        g.call("material", action="reload")
        painted = shot("painted-legacy")
        write_catalog([])
        none = shot("unpainted")
        check(differing(painted, none) > 50, f"the user's Ashley paint applies to the model ({differing(painted, none)} pixels differ from none)")
        # migrated: the entries name the content key, the maps copied (the legacy ones stay)
        write_catalog(mine)
        report = pr.rekey_tree(cat, maps, {LEGACY: content})
        check(report["entries"] == len(mine) and report["copied"] > 0 and not report["conflicts"], f"rekey_tree: {report['entries']} entries, {report['copied']} map files copied")
        migrated = shot("migrated")
        check(differing(migrated, painted) == 0, f"migrated entries and maps: the same picture, pixel for pixel ({differing(migrated, painted)} differ)")
        check(all((maps / e["match"]["key"] / f"@m{LEGACY}").exists() for e in mine if (source / "materials" / e["match"]["key"] / f"@m{LEGACY}").is_dir()),
              "...and the legacy map folders are still there (copied, never moved)")
        for d in maps.glob(f"*/@m{LEGACY}"):
            shutil.rmtree(d)
        content_only = shot("content-only")
        check(differing(content_only, painted) == 0, f"the content-key folders alone give the same picture ({differing(content_only, painted)} differ)")
        # another page: entries and folders under a legacy id that is not hers, her model named only by an alias
        fake = "deadbeef"
        for d in list(maps.glob(f"*/@m{content}")):
            shutil.copytree(d, d.parent / f"@m{fake}")
            shutil.rmtree(d)
        write_catalog([{**e, "object": {"models": [fake]}} for e in mine])
        no_alias = shot("other-page-no-alias")
        check(differing(no_alias, none) == 0, "an entry naming another legacy id does nothing to her without an alias")
        aliases.write_text(json.dumps({fake: content}))
        aliased = shot("other-page-alias")
        check(differing(aliased, painted) == 0, f"with the alias line (model_aliases.json) the same paint applies to her ({differing(aliased, painted)} differ)")
    except RpcError as e:
        check(False, f"RPC error {e}")
    finally:
        es.finish(s, "model_key")
    if failures:
        print(f"FAIL: {len(failures)} checks")
        return 1
    print("PASS: a model's paint follows its content key; the user's Ashley paint still applies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
