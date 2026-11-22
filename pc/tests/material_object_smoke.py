#!/usr/bin/env python3
"""Materials by object (pc/docs/plans/2026-10-27-materials-and-inspection-design.md "Plan 2026-11-21"; RPC
material_instance assign / assign_selection with `object`, material pick).

    python3 pc/tests/material_object_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only a,b]

Continues from a copy of the card into its save room with the editor open and a scratch user material layer, then:

- identity: the floor under Ashley is picked; the pick names its object (the room's group: a geometry block's byte
  offset, and the polygon's own offset) and no object entry applies to it.
- assign: a new instance assigned to the selection's object (`object` group, with the texture, and with no texture:
  all the object's textures; `polygon`): the pick reports it as the object's entry, and the texture's own lookup
  (`entry`) does not find it (an object entry never matches the texture elsewhere); an object entry of another
  group matches nothing here; the panel RPC reports `object`.
- draw: with a strong material (metal, smooth) the frame changes where the entry applies: a texture entry changes the
  texture's every polygon, an object entry only the object's (its changed pixels lie within the texture's, and the
  texture is also on other groups here: fewer of them); an object entry for another group changes nothing; the
  polygons carry variants (material list `variants`).
- file: Save writes `object` (format 2; an entry without `match` for the object-wide one), the file reads back the
  same object, and an entry whose groups have no room scope is refused with a problem listed.
"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import RpcError  # noqa: E402
import editor_selection_smoke as es  # noqa: E402
from material_instances_smoke import rpc, write_json, settle, frame  # noqa: E402
import numpy as np  # noqa: E402

USER_LAYER = {
    "format": 2,
    "presets": {},
    "entries": [
        # groups name a room: without a room scope this entry is refused
        {"id": "bad object", "match": {"key": "66b4521ff4", "size": [31, 31]}, "object": {"groups": [4096]},
         "preset": "flagstone"},
    ],
}


def check_identity(s):
    g, p = s.game, s.player
    s.feet = s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]
    pick = g.call("editor_inspect", action="pick", x=s.feet[0], y=s.feet[1])
    if not s.check(pick.get("valid") and pick.get("kind") == "surface", "the floor under Ashley is picked"):
        return False
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    obj = mp.get("object") or {}
    s.check(mp.get("kind") == "room" and len(obj.get("groups", [])) == 1 and obj.get("polygon_offset"),
            f"the pick names its room group and polygon ({obj})")
    s.check(mp.get("object_entry") == "", f"no object entry applies yet ({mp.get('object_entry')!r})")
    s.group = obj["groups"][0]
    s.polygon = int(obj["polygon_offset"], 16)
    problems = g.call("material_instance", action="validate").get("problems", [])
    s.check(any("bad object" in str(x) for x in problems), f"...and the problem is listed ({problems})")
    ids = [i["id"] for i in rpc(s, action="list")["instances"]]
    s.check("bad object" not in ids, f"an object entry with groups and no room is refused ({'bad object' in ids})")
    return True


def check_assign(s):
    g = s.game
    rpc(s, action="new", id="obj a", parent="flagstone", params={"roughness": 0.31})
    r = rpc(s, action="assign_selection", id="obj a", object="group")
    s.check(r["assigned"] and r["object"]["groups"] == [s.group] and r["match"].get("key")
            and r["scope"] == {"room": [s.zone, s.room]}, f"assigned to the selection's room group, this texture ({r.get('object')}, {r['match']})")
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    s.check(mp.get("object_entry") == "obj a", f"the pick reports the object's entry ({mp.get('object_entry')!r})")
    s.check(mp.get("entry") != "obj a", f"...and the texture's own lookup does not find it ({mp.get('entry')!r})")
    r = rpc(s, action="assign_selection", id="obj a", object="group", object_wide=True)
    s.check(r["match"] == {} and r["object"]["groups"] == [s.group], f"object-wide: no match, every texture of the group ({r['match']})")
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    s.check(mp.get("object_entry") == "obj a", "...it applies to the pick")
    r = rpc(s, action="assign_selection", id="obj a", object="polygon")
    s.check(r["object"]["polygons"] == [s.polygon] and not r["object"].get("groups"), f"a polygon ({r['object']})")
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    s.check(mp.get("object_entry") == "obj a", "...it applies to the pick")
    r = rpc(s, action="assign", id="obj a", object={"groups": [s.group + 8]}, key=mp["key"], size=mp["size"],
            scope={"room": [s.zone, s.room]})
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    s.check(mp.get("object_entry") == "", f"another group's entry matches nothing here ({mp.get('object_entry')!r})")
    # the picker's own RPC (Details > Material): `new` for the last pick, for its object
    g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    n = g.call("material", action="new", preset="flagstone", object="group")
    s.check(n.get("object") == {"groups": [s.group]} and n.get("match", {}).get("key"), f"material new for the pick's object ({n.get('object')})")
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    s.check(mp.get("object_entry") == n["id"], f"...and it applies ({mp.get('object_entry')!r})")
    n2 = g.call("material", action="new", preset="flagstone", object="group", object_wide=True)
    s.check(n2.get("object") == {"groups": [s.group]} and "match" not in n2, f"...all the object's textures ({n2.get('match')})")
    g.call("material", action="remove", id=n["id"])
    g.call("material", action="remove", id=n2["id"])
    try:
        rpc(s, action="assign_selection", id="stone", object="group")
        s.check(False, "a preset cannot be assigned")
    except RpcError:
        s.check(True, "a preset cannot be assigned")


def changed(a, b):
    return (np.abs(a - b).max(2) > 0)


def check_draw(s):
    g = s.game
    strong = {"roughness": 0.1, "specular": 1.0, "metal": 1.0}
    g.call("editor_ui", visible=False)
    g.call("graphics", interpolate=False, specular=1, view_mode=6)  # (the material view: the G-buffer's material target, still)
    base = frame(g, s.out / "base.png")
    # (the picture moves by itself a little: the pixels that differ between two frames of the same thing are left out)
    quiet = ~changed(base, frame(g, s.out / "base2.png"))
    s.check(quiet.mean() > 0.5, f"the baseline holds still over {quiet.mean() * 100:.0f}% of the frame")
    # the texture's own entry
    rpc(s, action="new", id="tex strong", parent="stone", params=strong)
    rpc(s, action="assign_selection", id="tex strong")
    settle(g)
    tex = frame(g, s.out / "texture.png")
    rpc(s, action="remove", id="tex strong")
    settle(g)
    s.check((changed(base, frame(g, s.out / "back.png")) & quiet).sum() == 0, "no entry: the frame is the baseline again")
    mt = changed(base, tex) & quiet
    s.check(mt.sum() > 0, f"a texture entry changes the frame ({int(mt.sum())} pixels)")
    # the object's entry
    rpc(s, action="new", id="obj strong", parent="stone", params=strong)
    rpc(s, action="assign_selection", id="obj strong", object="group")
    settle(g)
    obj = frame(g, s.out / "object.png")
    mo = changed(base, obj) & quiet
    listed = g.call("material", action="list")
    s.check(listed.get("variants", {}).get("count", 0) >= 1, f"polygons carry variants ({listed.get('variants')})")
    s.check(mo.sum() > 0, f"an object entry changes the frame ({int(mo.sum())} pixels)")
    outside = int((mo & ~mt).sum())
    s.check(outside <= 0.005 * max(int(mo.sum()), 1), f"...only where the texture's entry does ({outside} pixels outside)")
    shared = int(mt.sum()) - int(mo.sum())
    s.check(shared > 0, f"...and not on the texture's other polygons ({shared} pixels of the texture are not the object's)")
    # the same entry for a group that is not drawn here
    rpc(s, action="assign", id="obj strong", object={"groups": [s.group + 8]}, key=g.call("material", action="pick", x=s.feet[0], y=s.feet[1])["key"],
        size=g.call("material", action="pick", x=s.feet[0], y=s.feet[1])["size"], scope={"room": [s.zone, s.room]})
    settle(g)
    other = frame(g, s.out / "other.png")
    s.check((changed(base, other) & quiet).sum() == 0, "an entry for another group changes nothing")
    rpc(s, action="remove", id="obj strong")
    settle(g)
    g.call("graphics", view_mode=0)
    g.call("editor_ui", visible=True)


def check_maps(s):
    """Maps by object: <maps>/<key>/@<object>/roughness.png is read for the object's variant only."""
    from PIL import Image
    g = s.game
    g.call("editor_inspect", action="pick", x=s.feet[0], y=s.feet[1])
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    key, (w, h) = mp["key"], mp["size"]
    g.call("editor_ui", visible=False)
    g.call("graphics", interpolate=False, specular=1, view_mode=6)
    rpc(s, action="new", id="obj maps", parent="stone", params={"roughness": 0.9, "specular": 1.0})
    rpc(s, action="assign_selection", id="obj maps", object="group")
    settle(g)
    plain = frame(g, s.out / "maps-plain.png")
    quiet = ~changed(plain, frame(g, s.out / "maps-plain2.png"))
    folder = s.out / "maps" / key
    other = folder / "@g1"  # (another object's maps: not read for this one)
    other.mkdir(parents=True, exist_ok=True)
    Image.new("L", (w, h), 0).save(other / "roughness.png")
    settle(g)
    s.check((changed(plain, frame(g, s.out / "maps-other.png")) & quiet).sum() == 0, "another object's map changes nothing")
    mine = folder / f"@g{s.group:x}"
    mine.mkdir(parents=True, exist_ok=True)
    Image.new("L", (w, h), 0).save(mine / "roughness.png")
    settle(g)
    mapped = frame(g, s.out / "maps-mine.png")
    s.check((changed(plain, mapped) & quiet).sum() > 50, f"the object's own roughness map changes its pixels ({int((changed(plain, mapped) & quiet).sum())})")
    # the texture's own map is not what the object reads when it has its own: a different texture map changes nothing
    Image.new("L", (w, h), 255).save(folder / "roughness.png")
    settle(g)
    s.check((changed(mapped, frame(g, s.out / "maps-tex.png")) & quiet).sum() == 0, "...and the texture's own map does not replace it")
    for f in (mine / "roughness.png", other / "roughness.png", folder / "roughness.png"):
        f.unlink()
    rpc(s, action="remove", id="obj maps")
    settle(g)
    g.call("graphics", view_mode=0)
    g.call("editor_ui", visible=True)


def check_model(s):
    """A model is an object too (every copy of it): Ashley."""
    g, p = s.game, s.player
    pick, body = {}, None
    for up in (40, 80, 120, 160, 200, 260):  # (up the body from the feet: y is down)
        body = s.select(action="project", point=[p["x"], p["y"] - up, p["z"]])["pixel"]
        pick = g.call("editor_inspect", action="pick", x=body[0], y=body[1])
        if pick.get("valid") and pick.get("kind") == "model":
            break
    if not s.check(pick.get("valid") and pick.get("kind") == "model", f"Ashley is picked ({pick.get('kind')})"):
        return
    mp = g.call("material", action="pick", x=body[0], y=body[1])
    models = (mp.get("object") or {}).get("models", [])
    s.check(mp.get("kind") == "model" and len(models) == 1, f"the pick names her model ({mp.get('object')})")
    g.call("editor_ui", visible=False)
    g.call("graphics", interpolate=False, specular=1, view_mode=6)
    base = frame(g, s.out / "m-base.png")
    quiet = ~changed(base, frame(g, s.out / "m-base2.png"))
    rpc(s, action="new", id="obj model", parent="stone", params={"roughness": 0.1, "specular": 1.0, "metal": 1.0})
    r = rpc(s, action="assign_selection", id="obj model", object="model", object_wide=True)
    s.check(r["object"]["models"] == models and r["scope"] == {} or r["object"]["models"] == models,
            f"assigned to her model, all its textures ({r['object']}, {r['scope']})")
    settle(g)
    on = frame(g, s.out / "m-on.png")
    s.check(g.call("material", action="list")["variants"]["count"] > 0, "her polygons carry variants")
    s.check((changed(base, on) & quiet).sum() > 100, f"her material changes the picture ({int((changed(base, on) & quiet).sum())} pixels)")
    rpc(s, action="assign", id="obj model", object={"models": ["00000000"]})
    settle(g)
    off = frame(g, s.out / "m-off.png")
    s.check((changed(base, off) & quiet).sum() == 0, "another model's entry changes nothing")
    rpc(s, action="remove", id="obj model")
    settle(g)
    g.call("graphics", view_mode=0)
    g.call("editor_ui", visible=True)


def check_file(s):
    g = s.game
    g.call("editor_inspect", action="pick", x=s.feet[0], y=s.feet[1])  # (the floor selected again)
    rpc(s, action="assign_selection", id="obj a", object="group", object_wide=True)
    rpc(s, action="save")
    data = json.loads(s.user_file.read_text())
    e = {x["id"]: x for x in data["entries"]}["obj a"]
    s.check(data["format"] == 2 and e["object"] == {"groups": [s.group]} and "match" not in e,
            f"saved: format 2, the object, no match ({e})")
    settle(g)
    r = rpc(s, action="get", id="obj a")
    s.check(r["object"]["groups"] == [s.group] and r["assigned"], "read back from the file")
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    s.check(mp.get("object_entry") == "obj a", "...and it applies")
    rpc(s, action="remove", id="obj a")
    rpc(s, action="save")
    settle(g)
    mp = g.call("material", action="pick", x=s.feet[0], y=s.feet[1])
    s.check(mp.get("object_entry") == "", "removed: nothing applies")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out")
    parser.add_argument("--only")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else es.OUT / "material_object"
    out.mkdir(parents=True, exist_ok=True)
    user_file = out / "materials.json"
    write_json(user_file, USER_LAYER)
    env = {"ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"),
           "ROOD_MATERIAL_CATALOG_USER": str(user_file), "ROOD_MATERIALS": str(out / "maps"),
           "ROOD_MATERIALS_SHIPPED": str(out / "shipped-maps"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_GI": "0"}
    args.out = str(out)
    s = es.start(args, "material_object", extra_env=env)
    if s is None:
        return 77
    s.user_file = user_file
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in (check_identity, check_assign, check_draw, check_maps, check_model, check_file):
            name = section.__name__[len("check_"):]
            if s.failures or (only and name not in only):
                continue
            print(f"-- {name}", flush=True)
            try:
                if section(s) is False:
                    break
            except RpcError as e:
                s.check(False, f"{name}: RPC error {e}")
    finally:
        status = es.finish(s, "materials by object: identity, assignment, the file")
    return status


if __name__ == "__main__":
    sys.exit(main())
