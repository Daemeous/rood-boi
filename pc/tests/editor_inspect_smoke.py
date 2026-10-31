#!/usr/bin/env python3
"""Click-to-inspect (pc/src/editor_inspect.cpp, plan
pc/docs/plans/2026-10-30-click-to-inspect-plan.md): a click on a room surface
or a drawn model selects it (kinds surface / model); Details describes it;
the Outliner lists it; where-used names its rooms with Go; view modes by name.

    python3 pc/tests/editor_inspect_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

Continues from a copy of the card into its save room with the editor open
(editor_selection_smoke's session). Sections:

- pick: RPC editor_inspect pick on the floor under Ashley replies a room
  surface with a texture key and size; it is selected (surface:...); a
  click through editor_select on the same pixel selects it too; Escape
  clears; a pick on Ashley replies a model (actor slot 0, "Ashley"); picking
  works while the game is paused;
- details: the state has the Details fields (mesh, texture, palette, material
  entry and its layer, shader, maps); open_material switches Details to the
  Material tab with the texture picked there;
- outliner: the surface and the models drawn have rows; the Outliner's row
  of the primary is scrolled to; selecting a model row (RPC set) selects it;
- where_used: the floor texture's rooms include the save room (live); with
  the rollout survey present, Go to another listed room warps there, and the
  inspected items of the old room are dropped; back to the save room;
- view_modes: each named mode sets and the previous restores; the ones the
  renderer has change the picture against lit; wireframe toggles both
  overlays; the original mode is back at the end.
"""
import argparse
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import RpcError  # noqa: E402
import editor_selection_smoke as es  # noqa: E402

try:
    import numpy as np
    from PIL import Image
except ImportError:  # the view-mode picture checks need them
    np = Image = None


def inspect(s, **params):
    return s.game.call("editor_inspect", **params)


def rollout_dir():
    """The rollout survey (pc/build/material_rollout) here or in the main checkout; None without one."""
    e = os.environ.get("ROOD_MATERIAL_ROLLOUT")
    if e:
        return pathlib.Path(e)
    roots = [ROOT]
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    if common:
        roots.append(pathlib.Path(common).parent)
    for root in roots:
        if (root / "pc/build/material_rollout/rooms").is_dir():
            return root / "pc/build/material_rollout"
    return None


def floor_pixel(s):
    p = s.player
    return s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]


def frame(s, path):
    s.game.wait_presents(3)
    s.game.present_screenshot(str(path))
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)


def differ(x, y):
    return int((np.abs(x - y).max(2) > 8).sum())


def check_pick(s):
    g = s.game
    feet = floor_pixel(s)
    if not s.check(feet is not None, "Ashley's feet project into the view"):
        return
    r = inspect(s, action="pick", x=feet[0], y=feet[1])
    s.check(r.get("valid") and r.get("kind") == "surface", f"pick under Ashley: a room surface ({r.get('kind')})")
    if not r.get("valid"):
        return
    s.surface = r["id"]
    s.key, s.clut = r["key"], r["clut_key"]
    s.check(r["key"] and r["size"][0] > 0 and r["size"][1] > 0, f"the surface has a texture key and size ({r['key']}, {r['size']})")
    s.check(r["id"].startswith("surface:") and r["selected"], f"the surface is selected ({r['id']})")
    sel = s.select()
    s.check(sel["primary"] == r["id"], f"editor_select's primary is the surface ({sel['primary']})")
    s.check(any(o["id"] == r["id"] and o["kind"] == "surface" for o in sel["objects"]), "the surface is in the objects list")
    # Escape clears; a click through the selection RPC picks it again.
    s.keys(["escape"])
    s.check(s.select()["selection"] == [] and s.game.call("editor_ui")["visible"],
            "Escape clears the inspected selection (the editor stays open)")
    under = s.select(action="click", x=feet[0], y=feet[1])["under"]
    s.check(under == r["id"], f"editor_select click on the floor selects the surface ({under})")
    # Real mouse events on the same pixel.
    s.select(action="clear")
    s.click(feet)
    s.check(s.select()["primary"] == r["id"], "a real mouse click on the floor selects the surface")
    # Ashley: a model, actor slot 0.
    p = s.player
    chest = s.select(action="project", point=[p["x"], p["y"] - 90, p["z"]])["pixel"]
    m = inspect(s, action="pick", x=chest[0], y=chest[1]) if chest else {"valid": False}
    if not m.get("valid") or m.get("kind") != "model":  # her pose: try a little higher and lower
        for dy in (-60, -120, -40, -140):
            px = s.select(action="project", point=[p["x"], p["y"] + dy, p["z"]])["pixel"]
            m = inspect(s, action="pick", x=px[0], y=px[1]) if px else {"valid": False}
            if m.get("valid") and m.get("kind") == "model":
                break
    s.check(m.get("valid") and m.get("kind") == "model", f"a pick on Ashley replies a model ({m.get('kind')})")
    if m.get("kind") == "model":
        s.model = m["id"]
        s.check(m["actor_slot"] == 0 and m["label"] == "Ashley", f"the model is actor slot 0, Ashley ({m['actor_slot']}, {m['label']})")
        s.check(m["polygons"] > 0 and m["list"] == "object", f"its mesh has polygons and is an object list ({m['polygons']}, {m['list']})")
    # Paused: picking still answers.
    g.call("pause", paused=True)
    g.wait_presents(2)
    try:
        r2 = inspect(s, action="pick", x=feet[0], y=feet[1])
        s.check(r2.get("valid") and r2["id"] == r["id"], f"picking works paused ({r2.get('id')})")
    finally:
        g.call("pause", paused=False)
        g.run(3)


def check_details(s):
    if not getattr(s, "surface", None):
        s.check(False, "no surface picked")
        return
    s.select(action="set", ids=[s.surface])
    st = inspect(s, action="state")
    s.check(st.get("valid") and st["id"] == s.surface, "state replies the primary's item")
    for field in ("triangles", "polygons", "list", "key", "size", "clut_key", "tpage", "clut", "box", "entry",
                  "entry_source", "maps", "shader", "room", "polygon"):
        s.check(field in st, f"state has {field}")
    s.check(st["entry_source"] in ("none", "shipped catalog", "user layer", "unsaved edit", "review preview"),
            f"the entry's layer is named ({st['entry_source']})")
    s.check("GL" in st["shader"] or "Vulkan" in st["shader"], f"the shader is named ({st['shader']})")
    ui = s.game.call("editor_ui")
    s.check(ui["details"] == "details", f"a new selection shows the Details tab ({ui['details']})")
    r = inspect(s, action="open_material")
    s.game.run(3)
    ui = s.game.call("editor_ui")
    s.check(r.get("opened") and ui["details"] == "material", f"open_material opens Details > Material ({ui['details']})")
    mat = s.game.call("material", action="list")
    picked = mat.get("picked") or {}
    s.check(picked.get("key") == s.key, f"the Material panel has the texture picked ({picked.get('key')} vs {s.key})")
    s.game.call("editor_ui", details="details")


def check_outliner(s):
    if not getattr(s, "surface", None):
        return
    s.select(action="set", ids=[s.surface])
    s.game.run(4)
    rows = inspect(s, action="outliner")["rows"]
    ids = [r["id"] for r in rows]
    s.check(s.surface in ids, f"the Outliner section lists the surface ({len(rows)} rows)")
    models = [r for r in rows if r["id"].startswith("model:")]
    s.check(models, f"the models drawn now have rows ({[r['label'] for r in models]})")
    s.check(any(r["actor_slot"] == 0 for r in models), "Ashley's model row knows its actor slot")
    sel = s.select()
    s.check(sel["outliner"]["target"] == s.surface, f"the Outliner scrolled to the surface's row ({sel['outliner']})")
    if models:
        s.select(action="set", ids=[models[0]["id"]], source="outliner")
        s.game.run(3)
        st = inspect(s, action="state")
        s.check(st.get("valid") and st["id"] == models[0]["id"] and st["kind"] == "model",
                f"a model selected from the Outliner is the inspected primary ({st.get('id')})")
        rows = inspect(s, action="outliner")["rows"]
        s.check(any(r["id"] == models[0]["id"] and r["selected"] for r in rows), "its row shows selected")


def check_where_used(s):
    if not getattr(s, "surface", None):
        return
    g = s.game
    s.select(action="set", ids=[s.surface])
    r = inspect(s, action="where_used")
    rooms = r["rooms"]
    here = [x for x in rooms if (x["zone"], x["room"]) == (s.zone, s.room)]
    s.check(here and here[0]["live"], f"the floor texture's rooms include this one, live ({len(rooms)} rooms)")
    s.check(r["key"] == s.key, f"the texture asked for is the inspected one ({r['key']})")
    survey = rollout_dir()
    if survey is None:
        s.check("no survey data" in r["status"], f"without survey data the status says so ({r['status']})")
        print("skip: no rollout survey (pc/build/material_rollout); Go is not exercised")
        return
    s.check(r["status"] == "", f"survey data read ({r['status']!r})")
    others = [x for x in rooms if (x["zone"], x["room"]) != (s.zone, s.room)]
    if not others:
        # Another texture of this room may be shared; pick the palette's rooms instead.
        r2 = inspect(s, action="where_used", key="", clut=s.clut)
        others = [x for x in r2["rooms"] if (x["zone"], x["room"]) != (s.zone, s.room)]
    if not s.check(others, "the survey names another room using the texture or its palette"):
        return
    target = others[0]
    g.call("editor_camera", off=1)
    ok = inspect(s, action="go", zone=target["zone"], room=target["room"]).get("going")
    for _ in range(60):
        g.run(20)
        if g.call("room_goto", action="status")["phase"] in (0, 4):
            break
    g.run(40)
    state = g.state()
    s.check(ok and (state["zone"], state["room"]) == (target["zone"], target["room"]),
            f"Go warps to {target['zone']}/{target['room']} ({state['zone']}/{state['room']})")
    g.call("editor_ui", visible=True)
    g.run(6)
    sel = s.select()
    s.check(sel["selection"] == [] and not inspect(s, action="items")["items"],
            f"the old room's inspected items are dropped ({sel['selection']})")
    s.check(es.goto(g, s.zone, s.room), f"back to the save room {s.zone}/{s.room}")
    g.call("editor_ui", visible=True)
    g.run(6)


def check_view_modes(s):
    g = s.game
    modes = inspect(s, action="view_modes")
    names = [m["name"] for m in modes["modes"]]
    for want in ("lit", "unlit", "lighting", "base_colour", "normals", "roughness", "metal", "emissive", "ao", "wireframe"):
        s.check(want in names, f"view mode {want} is listed")
    original = modes["current"]
    s.check(original == "lit", f"the test starts lit ({original!r})")
    cmds = [c["id"] for c in g.call("editor_command", action="list")["commands"]]
    s.check("view.mode.roughness" in cmds and "view.mode.wireframe" in cmds, "the modes are commands (view.mode.<name>)")
    lit = frame(s, s.out / "mode-lit.png") if np is not None else None
    for m in modes["modes"]:
        name = m["name"]
        if name == "lit":
            continue
        r = inspect(s, action="view_mode", name=name)
        s.check(r["previous"] == "lit" and r["current"] == name, f"{name}: set from lit ({r['previous']} -> {r['current']})")
        if name == "wireframe":
            s.check(r["wireframe"], "wireframe: both wireframe overlays are on")
        elif lit is not None and m["available"]:
            pic = frame(s, s.out / f"mode-{name}.png")
            s.check(differ(pic, lit) > 500, f"{name}: the picture differs from lit ({differ(pic, lit)} pixels)")
        back = inspect(s, action="view_mode", name=r["previous"])
        s.check(back["current"] == "lit" and not back["wireframe"], f"{name}: restored to lit")
    try:
        inspect(s, action="view_mode", name="nonsense")
        s.check(False, "an unknown mode is refused")
    except RpcError:
        s.check(True, "an unknown mode is refused")
    s.check(inspect(s, action="view_modes")["current"] == original, "the original mode is back")


SECTIONS = [check_pick, check_details, check_outliner, check_where_used, check_view_modes]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(es.pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (pick, details, ...)")
    args = parser.parse_args()
    env = {}
    survey = rollout_dir()
    if survey is not None:
        env["ROOD_MATERIAL_ROLLOUT"] = str(survey)
    s = es.start(args, "editor_inspect", extra_env=env)
    if s is None:
        return 77
    s.surface = s.model = s.key = s.clut = None
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
        status = es.finish(s, "click-to-inspect: surfaces and models pick, Details, the Outliner, where used, view modes")
    return status


if __name__ == "__main__":
    sys.exit(main())
