#!/usr/bin/env python3
"""Master materials and instances (pc/docs/plans/2026-10-30-master-materials-plan.md; RPC
material_instance, material_preview).

    python3 pc/tests/material_instances_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only a,b]

Continues from a copy of the card into its save room with the editor open, a scratch user material
layer written in format 1 (presets and entries with overrides, palette and texture matches, scopes,
a user preset, an entry with an unknown preset), then:

- migrate: every preset is a root instance and every entry an instance of its preset whose own values
  are its explicit fields; the resolved values equal a Python resolver of the old format; the dropped
  entry is reported; the Entry list (RPC material list) is what the old reader made of the file;
  the user layer rewritten through `migrate` reads back identically (entries and resolved values).
- parents: a child of a preset, a child of that child, reset to parent, a loop refused, a parent of
  itself refused; Save writes `parent` (format 2) and the file reads back; Revert.
- assign: a new instance assigned to the part-2 selection's texture (the floor under Ashley), scoped to
  this room: the picker reports it on that texture; its palette; unassign.
- panel: Window > Materials and Material preview open by command and dock; the master lists its five
  parameters.
- preview: the sphere's digest changes with roughness and the light; the swatch is lit (centre brighter
  than its edge toward the light); the same digest for the same inputs (it is CPU shaded).
- identity: the Keep (29/3) with the scratch user layer (an override of M013 and a palette entry): the
  frame with the format-1 file and with its migrated rewrite are byte-identical; the planes agree.
- preview_room: lit like the room (on by default): a light beside Ashley in the Keep becomes the key, the game's
  room light the ambient, the scales are settings; M013's sphere has the floor's texture, the planes' values and
  its estimated relief (relief 0 differs); the digests equal the other renderer's run of the same binary.
- lua_panel: Window > Materials in Lua (pc/plugins/rood.materials) is off by default; on, it replaces the C++
  window and its tree and rows drive RPC material_instance (select, set, reset) with no file read; off, the C++
  window is back.
- drag: twenty `material_instance set` steps, one a presented frame: each is the instance's value at once
  (Vulkan: every frame changes), no catalog file is read (RPC material_instance status), the structure
  generation stays; Revert reads the files again, Save writes the value and it reads back.
"""
import argparse
import json
import os
import pathlib
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import RpcError  # noqa: E402
import editor_selection_smoke as es  # noqa: E402

STANDARD = {"roughness": 1.0, "specular": 0.0, "metal": 0.0, "auto_height": 0.0, "bump": 1.0}
PARAMS = ("roughness", "specular", "metal", "auto_height", "bump")

USER_LAYER = {
    "format": 1,
    "presets": {"test wax": {"roughness": 0.25, "specular": 0.5, "metal": 0, "bump": 0.3}},
    "entries": [
        # the Keep's floor palette, the shipped M013 replaced by id (the user layer wins per id)
        {"id": "M013", "name": "Keep floor, smoother", "match": {"clut": "415127aebd"}, "scope": {"room": [29, 3]},
         "preset": "flagstone", "roughness": 0.45, "review": "accepted"},
        # a palette entry over several rooms, preset plus two overrides
        {"id": "Rtest000001", "name": "walls elsewhere", "match": {"clut": "97ad08ce0c"},
         "scope": {"rooms": [[29, 3], [40, 13]]}, "preset": "rough stone", "specular": 0.7, "bump": 1.5,
         "review": "accepted"},
        # a texture entry with a user preset, no overrides, one room
        {"id": "T001", "name": "waxed tile", "match": {"key": "66b4521ff4", "size": [31, 31]},
         "scope": {"room": [29, 3]}, "preset": "test wax"},
        # no preset: every value explicit, everywhere, this palette only
        {"id": "T002", "match": {"key": "dfcfeabea7", "size": [31, 31], "clut": "9987540535"},
         "roughness": 0.2, "specular": 0.9, "metal": 1, "auto_height": True, "bump": 2},
        # an unknown preset: dropped, as the old reader dropped it
        {"id": "T003", "match": {"key": "fff1111071", "size": [63, 127]}, "preset": "no such preset"},
    ],
}


def legacy_resolve(presets, entry):
    """The old reader: the preset's values, then the entry's explicit fields, over the defaults."""
    v = dict(STANDARD)
    if "preset" in entry:
        v.update({k: float(x) for k, x in presets[entry["preset"]].items()})
    for k in PARAMS:
        if k in entry:
            v[k] = float(entry[k])
    return v


def rpc(s, **params):
    return s.game.call("material_instance", **params)


def write_json(path, data):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=1) + "\n")
    os.replace(tmp, path)


def settle(game):
    game.call("material", action="reload")
    for _ in range(3):
        game.call("redraw")
        game.wait_presents(2)


def frame(game, path):
    game.call("redraw")
    game.wait_presents(6)
    game.present_screenshot(str(path))
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def entries_of(game):
    """RPC material list's entries by id: the resolved value, the origin and the written form."""
    return {e["id"]: e for e in game.call("material", action="list")["entries"]}


def close(a, b):
    return abs(a - b) < 1e-3


def check_migrate(s):
    presets = dict(json.loads((ROOT / "pc/assets/materials/catalog.json").read_text())["presets"])
    presets.update(USER_LAYER["presets"])
    listed = rpc(s, action="list")
    by_id = {i["id"]: i for i in listed["instances"]}
    s.check(len(listed["masters"]) >= 1 and listed["masters"][0]["name"] == "standard_surface",
            f"the standard surface master is listed ({[m['name'] for m in listed['masters']]})")
    s.check([p["name"] for p in listed["masters"][0]["parameters"]] == list(PARAMS), "its parameters are the five catalog fields")
    for name, values in presets.items():
        i = by_id.get(name)
        if not s.check(i and i["root"] and not i["parent"] and not i["assigned"], f"preset {name} is a root instance"):
            continue
        ok = all(close(i["values"][k], float(values.get(k, STANDARD[k]))) for k in PARAMS)
        s.check(ok, f"preset {name} resolves to its values ({i['values']})")
    s.check(by_id["test wax"]["user"], "the user's preset is a user root")
    for entry in USER_LAYER["entries"]:
        i = by_id.get(entry["id"])
        if entry["id"] == "T003":
            s.check(i is not None and i.get("error") and "values" not in i,
                    f"an entry with an unknown preset is listed unresolved ({i and i.get('error')})")
            s.check(any("T003" in p for p in listed["problems"]), f"...reported, and it makes no entry ({listed['problems']})")
            continue
        if not s.check(i is not None, f"entry {entry['id']} is an instance"):
            continue
        s.check(i["parent"] == entry.get("preset", ""), f"{entry['id']}: parent is its preset ({i['parent']!r})")
        own = {k: float(entry[k]) for k in PARAMS if k in entry}
        s.check({k: round(v, 3) for k, v in i["own"].items()} == {k: round(v, 3) for k, v in own.items()},
                f"{entry['id']}: own values are its explicit fields ({i['own']})")
        want = legacy_resolve(presets, entry)
        s.check(all(close(i["values"][k], want[k]) for k in PARAMS), f"{entry['id']}: resolves as the old reader ({i['values']} vs {want})")
        s.check(i["assigned"] and i["user"], f"{entry['id']}: assigned, the user's")
        if "scope" in entry:
            s.check(i["scope"] == entry["scope"], f"{entry['id']}: scope kept ({i['scope']})")
    ent = entries_of(s.game)
    s.check(set(ent) >= {"M013", "Rtest000001", "T001", "T002"} and "T003" not in ent, "material list derives the entries")
    s.check(close(ent["M013"]["value"]["roughness"], 0.45) and ent["M013"]["preset"] == "flagstone"
            and ent["M013"]["origin"] == "user", f"M013 is the user's override of the shipped entry ({ent['M013']})")
    s.check("preset" not in ent["T002"] and ent["T002"]["value"]["metal"] == 1 and ent["T002"]["value"]["auto_height"] is True,
            f"T002 has no preset and every value its own ({ent['T002']})")
    # The migration made explicit: the user layer rewritten, read back the same.
    before = {k: v["value"] for k, v in ent.items()}
    before_inst = {i["id"]: (i["parent"], i.get("values"), i.get("scope"), i.get("match")) for i in listed["instances"]}
    migrated = s.out / "migrated.json"
    r = rpc(s, action="migrate", out=str(migrated))
    s.check(r["presets"] == 1 and r["entries"] == 5, f"migrate writes the user layer's 1 preset and 5 entries, the unresolved one as read ({r})")
    data = json.loads(migrated.read_text())
    s.check(data["format"] == 1 and "parent" not in json.dumps(data), "a format-1 layer stays format 1 (nothing new to say)")
    s.check(data["presets"] == {"test wax": {"roughness": 0.25, "specular": 0.5, "metal": 0, "bump": 0.3}},
            f"the preset is written back as it was ({data['presets']})")
    write_json(s.user_file, data)
    settle(s.game)
    after = {k: v["value"] for k, v in entries_of(s.game).items()}
    s.check(after == before, f"the migrated file reads back to the same entries ({len(after)})")
    after_inst = {i["id"]: (i["parent"], i.get("values"), i.get("scope"), i.get("match")) for i in rpc(s, action="list")["instances"]}
    s.check(after_inst == before_inst, "...and the same instances")
    s.migrated = data


def check_parents(s):
    g = s.game
    child = rpc(s, action="new", id="child", parent="flagstone", params={"roughness": 0.3})
    s.check(child["id"] == "child" and child["parent"] == "flagstone" and close(child["values"]["roughness"], 0.3)
            and close(child["values"]["bump"], 0.7), f"a child of flagstone with its own roughness ({child['values']})")
    s.check(child["from"]["roughness"] == "child" and child["from"]["bump"] == "flagstone" and child["from"]["metal"] == "flagstone",
            f"from: each value's source ({child['from']})")
    grand = rpc(s, action="new", id="grandchild", parent="child", params={"bump": 2.0})
    s.check(close(grand["values"]["roughness"], 0.3) and close(grand["values"]["bump"], 2.0) and grand["chain"] == ["grandchild", "child", "flagstone"],
            f"a child of the child inherits through it ({grand['chain']}, {grand['values']})")
    r = rpc(s, action="set", id="child", params={"roughness": 0.5})
    g2 = rpc(s, action="get", id="grandchild")
    s.check(close(r["values"]["roughness"], 0.5) and close(g2["values"]["roughness"], 0.5), "an edit on the parent reaches the grandchild")
    r = rpc(s, action="set", id="child", params={"roughness": None})
    s.check("roughness" not in r["own"] and close(r["values"]["roughness"], 0.62), f"reset to parent: flagstone's 0.62 ({r['values']['roughness']})")
    for bad, params in (("itself", dict(id="child", parent="child")), ("a loop", dict(id="flagstone", parent="grandchild"))):
        try:
            rpc(s, action="parent", **params)
            s.check(False, f"{bad} as a parent is refused")
        except RpcError as e:
            s.check(True, f"{bad} as a parent is refused ({e})")
    try:
        rpc(s, action="parent", id="child", parent="grandchild")
        s.check(False, "a loop (child <- grandchild <- child) is refused")
    except RpcError as e:
        s.check(True, f"a loop is refused ({e})")
    s.check(rpc(s, action="list")["dirty"], "the edits are unsaved")
    rpc(s, action="save")
    data = json.loads(s.user_file.read_text())
    ids = {e["id"]: e for e in data["entries"]}
    s.check(data["format"] == 2 and ids["grandchild"]["parent"] == "child" and ids["grandchild"]["preset"] == "flagstone"
            and "match" not in ids["grandchild"], f"saved: format 2, parent and the root preset ({ids['grandchild']})")
    s.check(ids["child"].get("preset") == "flagstone" and "parent" not in ids["child"], f"a child of a preset is written with preset only ({ids['child']})")
    settle(g)
    r = rpc(s, action="get", id="grandchild")
    s.check(close(r["values"]["bump"], 2.0) and r["chain"] == ["grandchild", "child", "flagstone"], "read back from the file")
    rpc(s, action="remove", id="grandchild")
    rpc(s, action="remove", id="child")
    rpc(s, action="save")
    s.check(not [i for i in rpc(s, action="list")["instances"] if i["id"] in ("child", "grandchild")], "removed and saved")


def check_assign(s):
    g = s.game
    p = s.player
    feet = s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]
    pick = g.call("editor_inspect", action="pick", x=feet[0], y=feet[1])
    if not s.check(pick.get("valid") and pick.get("kind") == "surface", "the floor under Ashley is picked"):
        return
    inst = rpc(s, action="new", id="floor here", parent="stone", params={"roughness": 0.33})
    r = rpc(s, action="assign_selection", id="floor here")
    s.check(r["assigned"] and r["match"].get("key") == pick["key"] and r["match"]["size"] == pick["size"]
            and r["scope"] == {"room": [s.zone, s.room]}, f"assigned to the selection's texture in this room ({r['match']}, {r['scope']})")
    mp = g.call("material", action="pick", x=feet[0], y=feet[1])
    s.check(mp.get("entry") == "floor here", f"the picker reports the instance on that texture ({mp.get('entry')})")
    if pick.get("clut_key"):
        r = rpc(s, action="assign_selection", id="floor here", palette=True)
        s.check(r["match"] == {"clut": pick["clut_key"]}, f"...its palette ({r['match']})")
    r = rpc(s, action="unassign", id="floor here")
    s.check(not r["assigned"], "unassigned")
    try:
        rpc(s, action="assign_selection", id="stone")
        s.check(False, "a preset cannot be assigned")
    except RpcError:
        s.check(True, "a preset cannot be assigned (make a child)")
    rpc(s, action="revert")
    s.check(not rpc(s, action="list")["dirty"], "reverted")


def check_panel(s):
    g = s.game
    for cmd, name in (("window.materials", "Materials"), ("window.material_preview", "Material preview")):
        g.call("editor_command", action="run", id=cmd)
        g.run(4)
        w = g.call("editor_shell", action="window", name=name)
        s.check(w.get("exists") and w.get("docked"), f"{name} opens by command and docks ({w})")
    m = rpc(s, action="masters")
    s.check(m["masters"][0]["path"].endswith("standard_surface.json"), f"the standard master comes from its data file ({m['masters'][0]['path']})")
    s.check(len(m["masters"][0]["slots"]) == 5, "its texture slots: the texel and the four channel maps")


def check_preview(s):
    g = s.game
    g.call("graphics", material_preview_room=False)  # (its own key light: the settings below)
    rpc(s, action="new", id="pv", parent="polished metal")
    a = g.call("material_preview", action="select", id="pv")
    s.check(a["id"] == "pv" and a["size"] > 0 and a["texture"] == "", f"the preview shows the instance as a swatch ({a['size']} px)")
    s.check(sum(a["centre"]) > sum(a["edge"]), f"lit: the centre is brighter than the edge ({a['centre']} vs {a['edge']})")
    rpc(s, action="set", id="pv", params={"roughness": 0.95, "metal": 0})
    b = g.call("material_preview")
    s.check(a["digest"] != b["digest"], "roughness changes the sphere")
    g.call("graphics", material_preview_light=0.2)
    c = g.call("material_preview")
    s.check(c["digest"] != b["digest"] and c["mean"] < b["mean"], f"a weaker key light darkens it ({c['mean']:.1f} < {b['mean']:.1f})")
    g.call("graphics", material_preview_light=1.6)
    d = g.call("material_preview")
    s.check(d["digest"] == b["digest"], "the same inputs give the same digest")
    s.check(not d["lighting"]["room"] and d["lighting"]["key_from"] == "preview", f"room lighting off: its own light ({d['lighting']})")
    g.call("graphics", material_preview_room=True)
    rpc(s, action="revert")


def check_preview_room(s):
    """Lit like the room (material_preview_room, on by default): the strongest light at Ashley is the key, the
    game's room light the ambient; the relief and the planes' values of the texture the instance draws; the same
    digest on GL and Vulkan (each run writes its own and compares with the other's of the same binary)."""
    g = s.game
    if not s.check(es.goto(g, 29, 3), "to the Keep (29/3)"):
        return
    g.call("graphics", interpolate=False, specular=1, light_animation=False)
    write_json(s.user_file, USER_LAYER)
    settle(g)
    st = g.call("material_preview", action="select", id="M013")
    s.check(st["settings"]["material_preview_room"] is True and st["lighting"]["room"],
            f"room lighting is on by default and the Keep has a room ({st['lighting']})")
    amb = st["lighting"]["ambient"]
    s.check(len(amb) == 3 and sum(amb) > 0, f"the ambient is the game's room light at Ashley ({amb})")
    p = g.state()["player"]
    g.call("lights", action="add", position=[p["x"] + 60, p["y"] - 60, p["z"]])
    g.run(4)
    a = g.call("material_preview")
    s.check(a["lighting"]["key_from"].startswith("light") and sum(a["lighting"]["key"]) > 0,
            f"a light beside Ashley is the key ({a['lighting']['key_from']}, {a['lighting']['key']})")
    s.check(a["digest"] != st["digest"], "...and the sphere follows it")
    g.call("graphics", material_preview_room_key=0.5)
    b = g.call("material_preview")
    s.check(b["mean"] < a["mean"], f"the room key's scale is a setting ({b['mean']:.1f} < {a['mean']:.1f})")
    g.call("graphics", material_preview_room_key=1.0)
    g.call("graphics", material_preview_room=False)
    own = g.call("material_preview")
    s.check(own["lighting"]["key_from"] == "preview" and own["digest"] != a["digest"], "off: the preview's own light")
    g.call("graphics", material_preview_room=True)
    # The relief: M013 draws the Keep's floor with its estimated relief; the sphere shows the planes' values.
    s.check(a["texture"] != "" and a["planes"] and a["relief"] == 2,
            f"M013's texture with the planes' values and its estimated relief (texture {a['texture']}, planes {a['planes']}, relief {a['relief']})")
    relief = a["settings"]["material_preview_relief"]
    g.call("graphics", material_preview_relief=0)
    flat = g.call("material_preview")
    g.call("graphics", material_preview_relief=relief)
    s.check(flat["digest_hex"] != a["digest_hex"], "the relief bends the sphere's shading (relief 0 differs)")
    again = g.call("material_preview")
    s.check(again["digest_hex"] == a["digest_hex"], "the same inputs give the same digest")
    # The same picture on both renderers (CPU-shaded): compared with the other renderer's run of this binary.
    renderer = os.environ.get("ROOD_RENDERER", "gl")
    stamp = os.stat(rood_rpc.BINARY).st_mtime_ns if rood_rpc.BINARY.exists() else 0
    mine = {"binary": stamp, "digest": a["digest_hex"], "flat": flat["digest_hex"]}
    keep = ROOT / "pc/build/material_preview_digests"  # (outside the suite's folder, which each run empties)
    keep.mkdir(parents=True, exist_ok=True)
    (keep / f"{renderer}.json").write_text(json.dumps(mine))
    other = keep / f"{'gl' if renderer == 'vulkan' else 'vulkan'}.json"
    if other.exists() and json.loads(other.read_text())["binary"] == stamp:
        theirs = json.loads(other.read_text())
        s.check(theirs["digest"] == mine["digest"] and theirs["flat"] == mine["flat"],
                f"the same preview digests as the other renderer's run ({mine} vs {theirs})")
    else:
        print("note: the other renderer's preview digests are not here yet (run both)")
    g.call("lights", action="reload")
    g.call("graphics", light_animation=True)


def check_identity(s):
    g = s.game
    if not s.check(es.goto(g, 29, 3), "to the Keep (29/3)"):
        return
    g.call("editor_ui", visible=False)
    g.call("graphics", interpolate=False)
    write_json(s.user_file, USER_LAYER)
    settle(g)
    old = frame(g, s.out / "keep-format1.png")
    uses_old = {u["key"]: u for u in g.call("material", action="list")["uses"]}
    s.check("M013" in {u["entry"] for u in uses_old.values()}, "the user's M013 is drawn in the Keep")
    write_json(s.user_file, s.migrated)
    settle(g)
    new = frame(g, s.out / "keep-migrated.png")
    uses_new = {u["key"]: u for u in g.call("material", action="list")["uses"]}
    diff = int((np.abs(old - new).max(2) > 0).sum())
    s.check(diff == 0, f"the Keep renders byte-identically on the migrated layer ({diff} pixels differ)")
    s.check(uses_old == uses_new, f"the same uses and planes ({len(uses_new)})")
    g.call("editor_ui", visible=True)


def next_frame(game, path):
    """The very next presented frame after an edit (no settling: a value edit must show at once)."""
    game.call("redraw")
    game.wait_presents(1)
    game.present_screenshot(str(path))
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def check_drag(s):
    """A slider dragged over RPC (material_instance set, twenty values, one a presented frame): every step is
    in the entries at once (Vulkan: every frame changes), no catalog file is read and the structure stays;
    Save writes and Revert reads the files as before."""
    g = s.game
    if not s.check(es.goto(g, 29, 3), "to the Keep (29/3)"):
        return
    g.call("editor_ui", visible=False)
    g.call("graphics", interpolate=False, specular=1)
    p = g.state()["player"]
    g.call("lights", action="add", position=[p["x"] + 40, p["y"] - 40, p["z"] + 40])  # (a highlight to move)
    write_json(s.user_file, USER_LAYER)
    settle(g)
    vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
    st0 = rpc(s, action="status")
    last = frame(g, s.out / "keep-drag0.png")
    moved, followed = 0, 0
    for k in range(20):
        v = round(0.05 + 0.9 * (k + 1) / 20, 4)
        rpc(s, action="set", id="M013", params={"roughness": v, "specular": round(0.3 + 0.6 * (k + 1) / 20, 4)})
        f = next_frame(g, s.out / "keep-drag.png")
        moved += differ_px(last, f) > 0
        last = f
        e = g.call("material_instance", action="get", id="M013")
        followed += close(e["values"]["roughness"], v)
    st = rpc(s, action="status")
    s.check(followed == 20, f"every step of the drag is the instance's value at once ({followed}/20)")
    m013 = entries_of(g)["M013"]
    s.check(close(m013["value"]["roughness"], 0.95), f"...and the entry the planes read ({m013['value']})")
    if vulkan:
        s.check(moved == 20, f"a dragged parameter: {moved}/20 consecutive presented frames changed")
    else:
        print(f"note: GL draws no material response ({moved}/20 frames changed)")
    s.check(st["file_reads"] == st0["file_reads"], f"no catalog file read during the drag ({st0['file_reads']} -> {st['file_reads']})")
    s.check(st["structure"] == st0["structure"] and st["generation"] >= st0["generation"] + 20,
            f"the structure kept, the values moved ({st0['structure']} -> {st['structure']}, generation +{st['generation'] - st0['generation']})")
    s.check(st["dirty"] and st["pending"] == 1, f"the drag is one unsaved edit ({st})")
    # Revert: the files are read again, the frame is the first one.
    rpc(s, action="revert")
    first = frame(g, s.out / "keep-drag-reverted.png")
    st = rpc(s, action="status")
    s.check(st["file_reads"] > st0["file_reads"] and not st["dirty"], f"Revert reads the files again ({st['file_reads']})")
    s.check(close(rpc(s, action="get", id="M013")["values"]["roughness"], 0.45), "...and M013 is as the file has it")
    # Save: written once, then read back.
    rpc(s, action="set", id="M013", params={"roughness": 0.33})
    rpc(s, action="save")
    saved = json.loads(s.user_file.read_text())
    m = [e for e in saved["entries"] if e["id"] == "M013"]
    s.check(m and close(m[0]["roughness"], 0.33) and not rpc(s, action="status")["dirty"], "Save writes the dragged value")
    s.check(close(rpc(s, action="get", id="M013")["values"]["roughness"], 0.33), "...and it reads back")
    write_json(s.user_file, USER_LAYER)
    settle(g)
    back = frame(g, s.out / "keep-drag-back.png")
    s.check(differ_px(first, back) == 0, f"the file restored: the first frame ({differ_px(first, back)} pixels)")
    g.call("lights", action="reload")  # (the test light was never saved)
    g.call("editor_ui", visible=True)


def check_lua_panel(s):
    """Window > Materials in Lua (pc/plugins/rood.materials, off by default): on, it replaces the C++ window
    (neither drawn nor listed); its tree and parameter rows drive the same RPC material_instance (select, a
    value set and reset from the panel's own path, no file read); off again, the C++ window is back."""
    g = s.game
    plugin, window = "rood.materials", "rood.materials/materials"
    s.check(g.lua_plugins().get(plugin, {}).get("enabled") is False, "the Lua Materials window is off by default")
    write_json(s.user_file, USER_LAYER)
    settle(g)
    g.call("editor_ui", visible=True)
    g.call("lua", action="enable", plugin=plugin, enabled=True)
    windows = {w["id"]: w for w in g.call("lua", action="windows")["windows"]}
    if not s.check(window in windows and windows[window]["replaces"] == "materials",
                   f"on: its window replaces the C++ one ({sorted(windows)})"):
        return
    cmds = {c["id"]: c for c in g.call("editor_command", action="list")["commands"]}
    s.check("window.materials" not in cmds or not cmds["window.materials"].get("enabled", True),
            "...and the C++ window's command steps aside")
    g.call("lua", action="window", id=window, open=True, focus=True)
    for _ in range(4):
        g.call("redraw")
        g.wait_presents(1)

    def lua(code):
        r = g.lua(code, plugin=plugin)
        if not r.get("ok"):
            return {"error": r.get("error")}
        res = r.get("results") or []
        return res[0] if res else None

    s.check((lua("return frames") or 0) > 0, "the window drew")
    count = rpc(s, action="list", brief=True, masters=False)["count"]
    s.check(lua("return #rows") == count, f"its tree holds every instance ({lua('return #rows')} of {count})")
    s.check(lua("return select_instance('M013')") is True and rpc(s, action="status")["selected"] == "M013",
            "a row selected in the Lua tree is the RPC's selection")
    g.call("redraw")
    g.wait_presents(2)
    s.check(lua("return info and info.id") == "M013", "its parameter panel shows the instance")
    reads = rpc(s, action="status")["file_reads"]
    s.check(lua("return set_value('roughness', 0.37)") is True, "a slider's path sets a value")
    s.check(close(rpc(s, action="get", id="M013")["values"]["roughness"], 0.37), "...it reaches the instance")
    s.check(lua("return set_value('auto_height', false)") is True
            and rpc(s, action="get", id="M013")["values"]["auto_height"] == 0, "a checkbox's path (a boolean)")
    reset = lua("return reset_value('roughness')")
    after = rpc(s, action="get", id="M013")
    s.check(reset is True and "roughness" not in after["own"] and after["from"].get("roughness") != "M013",
            f"Reset gives the parent's value back ({after['values']['roughness']} from {after['from'].get('roughness')!r})")
    s.check(rpc(s, action="status")["file_reads"] == reads, "no catalog file read by the panel's edits")
    for _ in range(3):
        g.call("redraw")
        g.wait_presents(1)
    g.present_screenshot(str(s.out / "lua-materials.png"))  # (for a look; nothing checked in it)
    s.check(lua("return draw_errors") == 0, f"no draw errors ({lua('return draw_errors')})")
    j = rpc(s, action="json", id="M013")
    s.check('"M013"' in j["json"], "Copy JSON's text (RPC json)")
    get = rpc(s, action="get", id="M013", slots=True)
    s.check(len(get.get("slots", [])) == 5, f"the texture slots for the panel ({get.get('slots')})")
    rpc(s, action="revert")
    g.call("lua", action="enable", plugin=plugin, enabled=False)
    g.call("editor_command", action="run", id="window.materials")
    g.run(4)
    w = g.call("editor_shell", action="window", name="Materials")
    if not w.get("exists"):
        g.call("editor_command", action="run", id="window.materials")
        g.run(4)
        w = g.call("editor_shell", action="window", name="Materials")
    s.check(w.get("exists"), f"off: the C++ window is Window > Materials again ({w})")


def differ_px(a, b):
    return int((np.abs(a - b).max(2) > 0).sum())


SECTIONS = [check_migrate, check_parents, check_assign, check_panel, check_preview, check_identity, check_drag,
            check_preview_room, check_lua_panel]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else es.OUT / "material_instances"
    out.mkdir(parents=True, exist_ok=True)
    user_file = out / "materials.json"
    write_json(user_file, USER_LAYER)
    env = {"ROOD_MATERIAL_CATALOG": str(ROOT / "pc/assets/materials/catalog.json"),
           "ROOD_MATERIAL_CATALOG_USER": str(user_file), "ROOD_MATERIALS": str(out / "maps"),
           "ROOD_MATERIALS_SHIPPED": str(out / "shipped-maps"), "ROOD_MATERIALS_BAKED": str(out / "baked"),
           "ROOD_MATERIAL_EXPORTS": str(out / "exports"), "ROOD_GI": "0"}
    args.out = str(out)
    s = es.start(args, "material_instances", extra_env=env)
    if s is None:
        return 77
    s.user_file, s.migrated = user_file, None
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                try:
                    section(s)
                except RpcError as e:
                    s.check(False, f"{name}: RPC error {e}")
    finally:
        status = es.finish(s, "master materials and instances: migration, parents, assignment, panel, preview, identity")
    return status


if __name__ == "__main__":
    sys.exit(main())
