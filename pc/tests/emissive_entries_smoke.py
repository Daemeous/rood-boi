#!/usr/bin/env python3
"""Emissive entries in the editor (pc/src/emissive_editor.cpp): Duplicate and
Delete, by RPC and with the panel's buttons, each one undo step.

    python3 pc/tests/emissive_entries_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The save room with the editor open (editor_selection_smoke.py's start; the
emissive user layer is the scratch lights directory's emissive.json).

- rpc: an entry of yours duplicates under a new id with " copy" after its
  name (one undo step "Duplicate emissive entry"; undo takes the copy away,
  redo brings it back); remove deletes it (one undo step "Delete emissive
  entry"; undo brings it back);
- panel: on the Emissive tab, Delete... asks first (the entry stays while the
  confirm shows); Yes deletes it, one undo step; Cancel keeps it; a curated
  entry has Duplicate but no Delete, and its copy is yours;
- disk: undo back to the saved layer leaves nothing unsaved; the layer read
  again from its file (a hand edit, reload) is no undo step;
- view: the texture view (a floor texture picked, a new entry on it) is as
  wide as the panel and fitted; the readout names the texel under the
  pointer and its CLUT index; three wheel notches zoom in (whole-number
  steps) keeping that texel under the pointer, one zooms back out; the grid
  shows zoomed in; a rect dragged at 24x is the texels under the press and
  the release, Shift + drag adds one; a middle drag pans by what the pointer
  moved; the zoom and pan stay with the texture across another entry; Fit
  fits; brush dabs (radius 0.5) at the fit zoom and at 32x paint exactly the
  texels under the pointer into the map file. Real SDL mouse events.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import centre, finish, pinned_card, start  # noqa: E402

TEST = "U950"
DISK = "U951"


def entries(s):
    return {e["id"]: e for e in s.game.call("emissive")["entries"]}


def ui(s):
    s.game.run(3)
    return s.game.call("emissive", action="ui")


def check_rpc(s, curated):
    g = s.game
    g.call("emissive", action="set", entry={"id": TEST, "name": "entries test", "match": curated["match"], "mask": {},
                                            "color": [1, 0.5, 0.25], "strength": 2, "light": 0})
    copy = g.call("emissive", action="duplicate", id=TEST)
    e, h = entries(s), s.history()
    s.check(copy["id"] != TEST and copy["id"] in e and e[copy["id"]]["name"] == "entries test copy"
            and e[copy["id"]]["strength"] == 2 and e[copy["id"]]["origin"] == "user"
            and h["undo"] == "Duplicate emissive entry",
            f"duplicate: a copy of yours under {copy['id']}, one undo step ({h['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(copy["id"] not in entries(s), "undo takes the copy away")
    g.call("editor_undo", action="redo")
    s.check(copy["id"] in entries(s), "redo brings it back")
    g.call("emissive", action="remove", id=copy["id"])
    s.check(copy["id"] not in entries(s) and s.history()["undo"] == "Delete emissive entry",
            f"remove: gone, one undo step ({s.history()['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(copy["id"] in entries(s), "undo brings the deleted entry back")
    g.call("emissive", action="remove", id=copy["id"])


def check_disk(s, curated):
    """The layer read again from its file is no undo step; undo back to what is saved is not unsaved."""
    g = s.game
    g.call("emissive", action="set", entry={"id": DISK, "name": "disk test", "match": curated["match"], "mask": {},
                                            "color": [1, 1, 1], "strength": 1, "light": 0})
    g.call("emissive", action="save")
    path = pathlib.Path(g.call("emissive")["user"])
    s.check(not g.call("emissive")["unsaved"], "saved: nothing unsaved")
    g.call("emissive", action="set", id=DISK, name="disk test changed")
    s.check(g.call("emissive")["unsaved"], "an edit: unsaved")
    g.call("editor_undo", action="undo")
    g.run(2)
    s.check(not g.call("emissive")["unsaved"] and entries(s)[DISK]["name"] == "disk test",
            "undone back to the saved layer: nothing unsaved (hand edits reload again)")
    done = s.history()["done"]
    text = path.read_text().replace('"disk test"', '"disk test edited"')
    path.write_text(text)
    g.call("emissive", action="reload")
    g.run(3)
    h = s.history()
    s.check(entries(s)[DISK]["name"] == "disk test edited" and h["done"] == done,
            f"the file read again: no undo step ({h['done']} vs {done})")
    g.call("emissive", action="remove", id=DISK)
    g.call("emissive", action="save")


def check_panel(s, curated):
    g = s.game
    g.call("editor_ui", details="emissive")
    g.call("emissive", action="select", id=TEST)
    state = ui(s)
    if not s.check("delete" in state["rects"] and "duplicate" in state["rects"] and state["selected"] == TEST,
                   f"the panel shows the entry's Duplicate and Delete... ({state})"):
        return
    s.click(centre(state["rects"]["delete"]))
    state = ui(s)
    s.check(state["confirm"] and TEST in entries(s), "Delete... asks first; the entry stays")
    s.click(centre(state["rects"]["confirm_cancel"]))
    state = ui(s)
    s.check(not state["confirm"] and TEST in entries(s), "Cancel keeps it")
    s.click(centre(state["rects"]["delete"]))
    state = ui(s)
    done = s.history()["done"]
    s.click(centre(state["rects"]["confirm_yes"]))
    ui(s)
    h = s.history()
    s.check(TEST not in entries(s) and h["done"] == done + 1 and h["undo"] == "Delete emissive entry",
            f"Yes deletes it, one undo step ({h['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(TEST in entries(s), "undo brings it back")
    g.call("emissive", action="select", id=curated["id"])
    state = ui(s)
    s.check("duplicate" in state["rects"] and "delete" not in state["rects"],
            f"a curated entry: Duplicate, no Delete ({sorted(state['rects'])})")
    before = set(entries(s))
    s.click(centre(state["rects"]["duplicate"]))
    ui(s)
    new = sorted(set(entries(s)) - before)
    s.check(len(new) == 1 and entries(s)[new[0]]["origin"] == "user"
            and entries(s)[new[0]]["name"] == (curated["name"] or curated["id"]) + " copy",
            f"its copy is yours ({new})")
    for ident in new + [TEST]:
        g.call("emissive", action="remove", id=ident)


def view(s, **params):
    """The texture view as last drawn (RPC emissive ui texture_view), after `params` are set."""
    s.game.run(3)
    return s.game.call("emissive", action="ui", **params)


def texel_xy(tv, x, y, fx=0.5, fy=0.5):
    """The window pixel over texel (x, y) of the texture view (fx, fy: where in the texel)."""
    r, z, pan = tv["rect"], tv["zoom"], tv["pan"]
    return [r[0] + (x + fx - pan[0]) * z, r[1] + (y + fy - pan[1]) * z]


def inside(tv, xy):
    r = tv["rect"]
    return r[0] + 1 < xy[0] < r[0] + r[2] - 1 and r[1] + 1 < xy[1] < r[1] + r[3] - 1


def hover(s, xy):
    """The pointer to xy (no button: a wheel step of 0)."""
    s.game.call("system", mouse=[float(xy[0]), float(xy[1])], button=0, wheel=0)
    s.game.run(3)


def wheel(s, xy, steps):
    s.game.call("system", mouse=[float(xy[0]), float(xy[1])], button=0, wheel=steps)
    s.game.run(3)


def mask_rects(s, ident):
    return entries(s)[ident]["mask"].get("rects", [])


def check_view(s):
    """The texture view: as wide as the panel and fitted, a hover readout, the wheel zooming about the texel under
    the pointer, the grid, a middle drag panning, Fit; the zoom kept per texture; rect drags (Shift adds) and
    brush dabs land on the texels under the pointer at any zoom."""
    g = s.game
    g.call("editor_ui", details="emissive")
    g.run(3)
    picked = None
    for y in range(560, 700, 30):
        for x in range(200, 860, 60):
            p = g.call("emissive", action="pick", x=x, y=y)
            if p.get("valid") and p.get("room") and not p.get("entry") and min(p["size"]) >= 16:
                picked = p
                break
        if picked:
            break
    if not s.check(picked is not None, "view: a room texture of 16 texels or more picked"):
        return
    key, (w, h) = picked["key"], picked["size"]
    ident = g.call("emissive", action="new")["id"]
    g.call("emissive", action="set", id=ident, mask={"map": "emissive"})
    g.call("emissive", action="ui", tool="rectangle")
    g.call("emissive", action="select", id=ident)
    tv = view(s, texture={"show": True}).get("texture_view")
    tv = view(s).get("texture_view")
    if not s.check(tv is not None and tv["key"] == key and tv["size"] == [w, h],
                   f"view: the picked texture shows ({tv})"):
        return
    r = tv["rect"]
    fit = int(min(r[2] / w, r[3] / h))
    s.check(r[2] > 256 and r[1] >= 0 and r[1] + r[3] <= 720 and tv["fitted"] and tv["zoom"] == fit == tv["fit"],
            f"view: as wide as the panel ({r[2]:.0f} px, more than the old 256), on screen, fitted at {fit}x "
            f"({tv['zoom']}x)")
    # the hover readout: the texel under the pointer and its CLUT index
    indices = g.call("emissive", action="texture", tpage=picked["tpage"], clut=picked["clut"],
                     box=picked["box"])["indices"]
    hover(s, texel_xy(tv, 5, 7))
    tv = view(s)["texture_view"]
    s.check(tv["hover"] == [5, 7, indices[7][5]], f"view: the readout names texel 5, 7 and its CLUT index "
            f"({tv['hover']} vs index {indices[7][5]})")
    # the wheel: zoom steps about the texel under the pointer
    a = [w // 2, h // 2]
    zooms = [tv["zoom"]]
    for _ in range(3):
        wheel(s, texel_xy(tv, *a, 0.3, 0.6), 1)
        tv = view(s)["texture_view"]
        zooms.append(tv["zoom"])
        if tv["hover"][:2] != a:
            break
    s.check(zooms == sorted(set(zooms)) and len(zooms) == 4 and zooms[-1] >= 2 * fit and tv["hover"][:2] == a
            and not tv["fitted"], f"view: three wheel notches zoom in ({zooms}), the texel under the pointer "
            f"stays ({tv['hover']} vs {a})")
    s.check(tv["grid"] == (tv["zoom"] >= view(s)["view"]["grid_zoom"]), "view: the texel grid shows zoomed in")
    wheel(s, texel_xy(tv, *a, 0.3, 0.6), -1)
    tv2 = view(s)["texture_view"]
    s.check(tv2["zoom"] == zooms[-2] and tv2["hover"][:2] == a, f"view: a notch back out ({tv2['zoom']}x)")
    wheel(s, texel_xy(tv2, *a, 0.3, 0.6), 1)
    tv = view(s)["texture_view"]
    # A rectangle at this zoom adds the texels under the press and release to the painted map.
    b = [a[0] + 3, a[1] + 2]
    pa, pb = texel_xy(tv, *a), texel_xy(tv, *b)
    if s.check(inside(tv, pa) and inside(tv, pb), f"view: texels {a} and {b} are in the canvas at {tv['zoom']}x"):
        s.mouse(pa, down=True)
        s.mouse([(pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2], down=True)
        s.mouse(pb, down=True)
        s.mouse(pb, down=False)
        from PIL import Image
        path = s.lights_dir / "materials" / key / "emissive.png"
        img = Image.open(path).convert("L")
        expected = {(x, y) for y in range(a[1], b[1] + 1) for x in range(a[0], b[0] + 1)}
        lit = {(x, y) for y in range(h) for x in range(w) if img.getpixel((x, y))}
        s.check(lit == expected, f"view: rectangle drag at {tv['zoom']}x paints exactly its texels")
        # Another drag adds one (also while Shift is held, dragged the other way).
        # First paint removes the missing-map warning above the canvas.
        # Read its new screen position rather than reusing pre-paint pixels.
        tv_before = tv
        tv = view(s)["texture_view"]
        c, d = [a[0] - 2, a[1] + 1], [a[0] - 4, a[1] - 1]
        pc_, pd = texel_xy(tv, *c), texel_xy(tv, *d)
        g.call("editor_input", hold=["shift"])
        g.run(2)
        s.mouse(pc_, down=True)
        s.mouse(pd, down=True)
        s.mouse(pd, down=False)
        g.call("editor_input", release=True)
        g.run(2)
        img = Image.open(path).convert("L")
        expected |= {(x, y) for y in range(d[1], c[1] + 1) for x in range(d[0], c[0] + 1)}
        lit = {(x, y) for y in range(h) for x in range(w) if img.getpixel((x, y))}
        s.check(lit == expected, "view: another rectangle drag adds without replacing the first "
                f"(missing={sorted(expected - lit)}, extra={sorted(lit - expected)}, "
                f"canvas={tv_before['rect']} -> {tv['rect']})")
    # a middle drag pans by what the pointer moved
    tv = view(s)["texture_view"]
    mid = [tv["rect"][0] + tv["rect"][2] / 2, tv["rect"][1] + tv["rect"][3] / 2]
    s.mouse(mid, button=2, down=True)
    s.mouse([mid[0] + 24, mid[1] + 12], button=2, down=True)
    s.mouse([mid[0] + 48, mid[1] + 24], button=2, down=True)
    s.mouse([mid[0] + 48, mid[1] + 24], button=2, down=False)
    tv2 = view(s)["texture_view"]
    z = tv["zoom"]
    s.check(tv2["zoom"] == z and abs(tv2["pan"][0] - (tv["pan"][0] - 48 / z)) < 0.05
            and abs(tv2["pan"][1] - (tv["pan"][1] - 24 / z)) < 0.05,
            f"view: a middle drag pans ({tv['pan']} -> {tv2['pan']} at {z}x)")
    # the zoom and pan are the texture's: another entry (another texture) and back keeps them
    other = next((e["id"] for e in g.call("emissive")["entries"] if e["origin"] == "curated"), None)
    if other:
        g.call("emissive", action="select", id=other)
        view(s)
        g.call("emissive", action="select", id=ident)
        tv3 = view(s, texture={"show": True})
        tv3 = view(s)["texture_view"]
        s.check(tv3["key"] == key and tv3["zoom"] == tv2["zoom"] and tv3["pan"] == tv2["pan"],
                f"view: back on the texture, its zoom and pan are kept ({tv3['zoom']}x {tv3['pan']})")
    # Fit
    state = view(s)
    s.click(centre(state["rects"]["view_fit"]))
    tv = view(s)["texture_view"]
    s.check(tv["fitted"] and tv["zoom"] == tv["fit"] == fit, f"view: Fit shows the whole texture ({tv['zoom']}x)")
    # the brush at two zooms: one dab each (radius 0.5), on the texel under the pointer
    g.call("emissive", action="set", id=ident, mask={"map": "emissive"})
    g.call("emissive", action="paint", key=key, fill=0)
    view(s, brush={"on": True, "radius": 0.5, "hardness": 1, "opacity": 1, "value": 255})
    tv = view(s)["texture_view"]
    painted = []
    for zoom, t in ((tv["zoom"], [3, 4]), (32, [w - 5, h - 3])):
        tv = view(s, texture={"zoom": zoom, "pan": [t[0] - 2.25, t[1] - 3.5]})["texture_view"]
        tv = view(s)["texture_view"]
        xy = texel_xy(tv, *t, 0.4, 0.6)
        if not s.check(inside(tv, xy), f"brush: texel {t} in the canvas at {tv['zoom']}x"):
            continue
        s.mouse(xy, down=True)
        s.mouse(xy, down=False)
        painted.append(t)
    path = s.lights_dir / "materials" / key / "emissive.png"
    if s.check(path.exists(), f"brush: the map is written ({path.name})"):
        from PIL import Image
        img = Image.open(path).convert("L")
        lit = sorted([x, y] for y in range(img.height) for x in range(img.width) if img.getpixel((x, y)) > 0)
        s.check(img.size == (w, h) and lit == sorted(painted),
                f"brush: exactly the texels under the pointer are painted ({lit} vs {painted})")
    view(s, brush={"on": False}, texture={"fit": True})
    g.call("emissive", action="remove", id=ident)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    args = parser.parse_args()
    s = start(args, "emissive_entries")
    if s is None:
        return 77
    try:
        curated = next((e for e in s.game.call("emissive")["entries"] if e["origin"] == "curated"), None)
        if s.check(curated is not None, "a curated entry to copy the match from"):
            print("-- rpc", flush=True)
            check_rpc(s, curated)
            print("-- disk", flush=True)
            check_disk(s, curated)
            print("-- panel", flush=True)
            check_panel(s, curated)
            print("-- view", flush=True)
            check_view(s)
    finally:
        status = finish(s, "emissive entries duplicate and delete (with a confirm), each one undo step")
    return status


if __name__ == "__main__":
    sys.exit(main())
