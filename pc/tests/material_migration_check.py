#!/usr/bin/env python3
"""Check the instance migration of a real user material layer, on a copy
(pc/docs/plans/2026-10-30-master-materials-plan.md, task 5).

    python3 pc/tests/material_migration_check.py --user pc/user/lighting/materials.json [--rooms 29/3,13/3]

The layer is copied under pc/build (the original is only read), a headless game
reads the copy as instances, and the script reports: entries in the file, instances
made of them, each one's resolved values against a Python resolver of the format-1
reader (identical or not), problems reported, the layer rewritten through RPC
material_instance migrate and read back (entries and values the same), and, per
room, the frame with the copy and with its rewrite (byte-identical or not; Vulkan
shows the materials, GL has no response). Nothing outside pc/build is written.
"""
import argparse
import json
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import editor_selection_smoke as es  # noqa: E402
from material_instances_smoke import PARAMS, legacy_resolve, close, frame  # noqa: E402


def settle(game):
    """Files changed on disk read again (without the entries: too many for one reply), the paused frame redrawn."""
    game.call("material", action="reload", entries=False)
    for _ in range(3):
        game.call("redraw")
        game.wait_presents(2)


def values_of(game):
    """Every instance's resolved values, paged."""
    out = {}
    offset = 0
    while True:
        page = game.call("material_instance", action="list", offset=offset, limit=40, masters=False)
        for i in page["instances"]:
            out[i["id"]] = i.get("values")
        offset += 40
        if offset >= page["count"]:
            return out


def uses_of(game):
    return {u["key"]: u for u in game.call("material", action="list", entries=False)["uses"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--user", required=True, help="the user layer to copy and check (never written)")
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--rooms", default="29/3,13/3", help="rooms to compare frames in (zone/room,...)")
    parser.add_argument("--out", help="keep the frames and logs here")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "material_migration"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    user = out / "materials.json"
    shutil.copyfile(args.user, user)
    original = json.loads(user.read_text())
    shipped = json.loads((ROOT / "pc/assets/materials/catalog.json").read_text())
    presets = dict(shipped["presets"])
    presets.update(original.get("presets", {}))
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    (out / "lights").mkdir()
    (out / "settings.cfg").write_text("light_animation 0\ndust 0\naa off\n")
    env = {"ROOD_LIGHTS": str(out / "lights"), "ROOD_SCALE": "2", "ROOD_WINDOW": "1280x960", "ROOD_GI": "0",
           "ROOD_SETTINGS": str(out / "settings.cfg"), "ROOD_MATERIAL_CATALOG_USER": str(user),
           "ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"),
           "ROOD_MATERIALS": str(out / "maps"), "ROOD_MATERIALS_SHIPPED": str(out / "shipped-maps"),
           "ROOD_MATERIALS_BAKED": str(out / "baked"), "ROOD_MATERIAL_EXPORTS": str(out / "exports")}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-mig-{os.getpid()}.sock"
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    status = 0
    try:
        listed = game.call("material_instance", action="list", limit=40)
        instances = list(listed["instances"])
        while len(instances) < listed["count"]:
            instances += game.call("material_instance", action="list", offset=len(instances), limit=40, masters=False)["instances"]
        by_id = {i["id"]: i for i in instances}
        user_entries = original.get("entries", [])
        print(f"file: {len(user_entries)} entries, {len(original.get('presets', {}))} presets, format {original.get('format')}")
        converted = identical = missing = differing = 0
        for e in user_entries:
            i = by_id.get(e["id"])
            if i is None or not i.get("user"):
                missing += 1
                print(f"  NOT CONVERTED: {e['id']}")
                continue
            converted += 1
            if "preset" in e and e["preset"] not in presets:
                print(f"  unresolved (unknown preset, dropped by the old reader too): {e['id']}")
                continue
            want = legacy_resolve(presets, e)
            own = {k: float(e[k]) for k in PARAMS if k in e}
            same = ("values" in i and all(close(i["values"][k], want[k]) for k in PARAMS)
                    and i["parent"] == e.get("preset", "") and {k: round(v, 3) for k, v in i["own"].items()} == {k: round(v, 3) for k, v in own.items()}
                    and i.get("scope", {}) == e.get("scope", {}) and i.get("match") == e["match"])
            if same:
                identical += 1
            else:
                differing += 1
                print(f"  DIFFERS: {e['id']}: {i.get('values')} vs {want}; parent {i['parent']!r}; scope {i.get('scope')} vs {e.get('scope')}")
        print(f"instances: {converted} converted, {identical} identical to the old reader, {differing} differing, {missing} missing")
        print(f"problems: {listed['problems']}")
        v = game.call("material_instance", action="validate")
        print(f"validate: {v['resolved']} resolved, {len(v['unresolved'])} unresolved, {v['entries']} entries derived")
        before = values_of(game)
        r = game.call("material_instance", action="migrate", out=str(out / "migrated.json"))
        migrated = json.loads((out / "migrated.json").read_text())
        print(f"migrate: {r['presets']} presets, {r['entries']} entries written, format {migrated['format']}")
        rooms = [tuple(int(n) for n in rr.split("/")) if "/" in rr else None for rr in args.rooms.split(",") if rr]
        for zone, room in [r for r in rooms if r]:
            # Both frames in one visit (a room entered twice is not the same frame: actors, animation).
            if not es.goto(game, zone, room):
                print(f"  room {zone}/{room}: not reached")
                continue
            game.call("editor_camera", off=1)
            game.call("graphics", interpolate=False)
            shutil.copyfile(args.user, user)
            settle(game)
            old, old_uses = frame(game, out / f"{zone}-{room}-format1.png"), uses_of(game)
            shutil.copyfile(out / "migrated.json", user)
            settle(game)
            pic, uses = frame(game, out / f"{zone}-{room}-migrated.png"), uses_of(game)
            diff = int((np.abs(old - pic).max(2) > 0).sum())
            print(f"  room {zone}/{room}: {len(uses)} textures with a material, frame {'byte-identical' if diff == 0 else f'{diff} pixels differ'}, "
                  f"uses {'identical' if uses == old_uses else 'DIFFERENT'}")
            status |= diff != 0 or uses != old_uses
        shutil.copyfile(out / "migrated.json", user)
        settle(game)
        after = values_of(game)
        same = before == after
        print(f"read back: {len(after)} instances, values {'identical' if same else 'DIFFERENT'} to the format-1 copy")
        status |= not same
        status |= missing != 0 or differing != 0
    finally:
        game.close()
    print("PASS" if status == 0 else "FAIL")
    return status


if __name__ == "__main__":
    sys.exit(main())
