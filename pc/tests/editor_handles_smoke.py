#!/usr/bin/env python3
"""The editor's viewport handles (pc/src/editor_handles.h, editor_light_handles.cpp)
and the colour popover (editor_colour.cpp), driven with real mouse events.

    python3 pc/tests/editor_handles_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The same save room and objects as editor_selection_smoke.py (a point light, a
candle, a fog volume). Each handle drag is checked the same way: dragged to
where the handle shows the wanted value (RPC editor_handles where), exactly
its field changes, by the expected amount; the drag is one undo step named
after it, and undo puts it back. Sections:

- layer: handles show for the selection's primary only (a light; none for
  nothing, a light actor or a hidden light), never while the camera flies;
  editor_input holds and releases modifiers;
- point: the intensity ring (100 px right doubles it; Ctrl a tenth as fast;
  Shift 1-2-5 steps), the radius dot, Escape mid-drag, the readout, the
  shadows toggle on the ring; with two lights selected only the primary's;
- spot: the range dot on its axis, the outer and inner cone handles on the
  cone's side, Escape; the crosshair then a click on the floor aims it there
  (one undo step "Aim at surface"); Escape disarms it;
- rect: an edge with Alt changes only the width (both edges move); a plain
  edge drag moves that edge (the width and the centre, one step); the height,
  the range dot, the barn doors' length and angle (flaps on its edges), Escape;
- sun: its icon on the sky dome selects it (env:sun); its disc dragged
  across the dome sets azimuth and elevation in one undo step; its ring its
  intensity; Escape puts it back;
- colour: the swatch opens the popover (Temperature first); Kelvin and tint
  give a warm colour in one undo step; the Candle stop; Hex typed; the room's
  palette and the recent colours; the tab remembered per light type, saved
  in the user settings; Escape closes it;
- volume: a fog volume selected shows its handles (the colour swatch, the
  density ring, the soft edge dot); the swatch opens the colour popover on
  its colour (one undo step, only its colour changes); each drag changes
  exactly one field by the expected amount in one undo step; Escape puts
  it back; the editor closing mid-drag keeps the value and closes its one
  undo step; R still sizes it while its handles show;
- fit: "fit rect light to window" on a wall: a new rect light on the
  clicked pane's plane (editor_fit_window_offset in front, facing out), its
  size the pane's, selected, one undo step; with that light selected a
  second fit moves it (no new light); the offset is tunable; closing the
  editor disarms it; armed from
  RPC (as the Details button and the toolbar do) a real click fits;
  Escape disarms;
- eyedropper: armed in the popover, a click on the picture gives the light
  that pixel's colour (display to linear, brightest channel 1) in one undo
  step; the click selects nothing.
"""
import argparse
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from editor_selection_smoke import centre, finish, pinned_card, start  # noqa: E402
from editor_schema_smoke import close, schema, value  # noqa: E402
from editor_library_smoke import floor_and_wall  # noqa: E402
import golden_frames as gf  # noqa: E402


def handles(s, **params):
    return s.game.call("editor_handles", **params)


def handle(s, name):
    return next((h for h in handles(s)["handles"] if h["name"] == name), None)


def fields(s, ident):
    return {k: v["value"] for k, v in schema(s, action="get", id=ident)["fields"].items()}


def changed(a, b):
    return sorted(k for k in b if k in a and not close(a[k], b[k]))


def drag(s, start, end, hold=None, steps=2, release=True):
    """A real mouse drag (RPC system mouse) from start to end, modifiers held (editor_input hold)."""
    g = s.game
    if hold:
        g.call("editor_input", hold=hold)
        g.run(2)
    s.mouse(start, down=True)
    for k in range(1, steps + 1):
        s.mouse([start[0] + (end[0] - start[0]) * k / steps, start[1] + (end[1] - start[1]) * k / steps], down=True)
    if release:
        s.mouse(end, down=False)
    if hold:
        g.call("editor_input", release=True)
        g.run(2)


def check_drag(s, ident, name, field, label, target_value, expect, tolerance=0.02, hold=None, also=()):
    """Drag handle `name` to where it shows `target_value`: only `field` (and `also`) change, `field` to
    `expect`; one undo step "Edit <label>"; undo puts it back. Returns the fields after the drag."""
    h = handle(s, name)
    if not s.check(h is not None, f"{ident}: a {name} handle ({[x['name'] for x in handles(s)['handles']]})"):
        return None
    target = handles(s, action="where", handle=name, value=target_value)["screen"]
    # Dear ImGui floors the pointer to whole pixels: press on a whole pixel and move by the
    # rounded offset; the value may then be off by up to a pixel's worth (foreshortened handles).
    eps = max(abs(target_value) * 0.02, 0.5)
    nudged = handles(s, action="where", handle=name, value=target_value + eps)["screen"]
    slack = eps / max(math.dist(nudged, target), 1e-3)  # value per pixel
    press = [round(c) for c in h["screen"]]
    end = [press[k] + round(target[k] - h["screen"][k]) for k in range(2)]
    before, done = fields(s, ident), s.history()["done"]
    drag(s, press, end, hold=hold)
    after, hist = fields(s, ident), s.history()
    s.check(changed(before, after) == sorted([field, *also])
            and (close(after[field], expect, tolerance) or abs(after[field] - expect) <= slack),
            f"{name} dragged{' with ' + '+'.join(hold) if hold else ''}: {changed(before, after)}; "
            f"{field} {before[field]} -> {after[field]} ({expect} expected)")
    s.check(hist["done"] == done + 1 and hist["undo"] == "Edit " + label,
            f"{name}: one undo step ({hist['undo']!r}, {hist['done'] - done} steps)")
    s.game.call("editor_undo", action="undo")
    s.check(close(fields(s, ident)[field], before[field]), f"{name}: undo puts {field} back")
    return after


def check_escape(s, ident, name, field, target_value):
    """Escape during a drag: the value back, no undo step, the editor still open."""
    h = handle(s, name)
    target = handles(s, action="where", handle=name, value=target_value)["screen"]
    before, done = fields(s, ident), s.history()["done"]
    drag(s, h["screen"], target, release=False)
    mid = fields(s, ident)[field]
    state = s.keys(["escape"])
    s.mouse(target, down=False)
    after = fields(s, ident)
    s.check(not close(mid, before[field]) and changed(before, after) == [] and state["visible"]
            and s.history()["done"] == done,
            f"{name}: Escape mid-drag ({field} {mid}) puts it back ({after[field]}), no undo step, the editor open")


def check_layer(s):
    g = s.game
    s.select(action="clear")
    st = handles(s)
    s.check(st["hidden"] == "nothing" and st["handles"] == [] and st["subject"] is None,
            f"nothing selected: no handles ({st['hidden']})")
    s.select(action="set", ids=[s.actor])
    g.run(2)
    st = handles(s)
    s.check(st["hidden"] == "unsupported" and st["handles"] == [], f"a light actor: none ({st['hidden']})")
    s.select(action="set", ids=[s.light])
    g.run(2)
    st = handles(s)
    s.check(st["subject"] == s.light and st["hidden"] is None, f"a light selected: its handles ({st['subject']})")
    s.keys(["h"])  # H hides the selection (and takes it out of it)
    s.select(action="set", ids=[s.light])
    g.run(2)
    st = handles(s)
    s.check(st["hidden"] == "hidden" and st["handles"] == [], f"a hidden light: none ({st['hidden']})")
    s.keys(["h"], ctrl=True)
    s.select(action="set", ids=[s.light])
    view = centre(s.select()["view"])
    s.mouse(view, 3, True)
    g.run(2)
    st = handles(s)
    s.mouse(view, 3, False)
    s.check(st["hidden"] == "flying" and st["handles"] == [], f"while the camera flies: none ({st['hidden']})")
    g.call("editor_camera", off=1)
    g.run(4)
    s.check(g.call("editor_input", hold=["shift"])["held"] == ["shift"], "editor_input holds Shift")
    s.check(g.call("editor_input", release=True)["held"] == [], "and lets it go")


def check_point(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "point"})
    # A small radius keeps its dot (and a drag to 1.5x and 2x) inside the view.
    schema(s, action="set", id=s.light, values={"intensity": 1, "radius": 300, "shadows": False})
    g.run(2)
    names = {h["name"] for h in handles(s)["handles"]}
    s.check({"intensity", "radius", "shadows"} <= names, f"a point light's handles ({sorted(names)})")
    check_drag(s, s.light, "intensity", "intensity", "Intensity", 2, 2)  # 100 px right: twice as bright
    ring = handle(s, "intensity")
    for hold, expect, what in ((["ctrl"], 2 ** 0.1, "Ctrl: a tenth as fast"), (["shift"], 2, "Shift: 1, 2, 5 steps")):
        right = 100 if hold == ["ctrl"] else 60  # Shift: 2^0.6 = 1.52 snaps to 2
        drag(s, ring["screen"], [ring["screen"][0] + right, ring["screen"][1]], hold=hold)
        v = value(s, s.light, "intensity")
        s.check(close(v, expect, 0.01), f"intensity ring {right} px with {hold[0]}: {v} ({what}, {expect:.3f})")
        g.call("editor_undo", action="undo")
    check_drag(s, s.light, "radius", "radius", "Radius", 450, 450)
    check_escape(s, s.light, "radius", "radius", 600)
    h = handle(s, "radius")  # the readout while dragging
    s.mouse(h["screen"], down=True)
    s.mouse([h["screen"][0] + 20, h["screen"][1]], down=True)
    st = handles(s)
    s.keys(["escape"])
    s.mouse([h["screen"][0] + 20, h["screen"][1]], down=False)
    s.check(st["drag"]["active"] and st["drag"]["handle"] == "radius" and (st["drag"]["readout"] or "").startswith("Radius"),
            f"the value shows by the pointer while dragging ({st['drag']})")
    done = s.history()["done"]
    s.click(handle(s, "shadows")["screen"])
    h = s.history()
    s.check(value(s, s.light, "shadows") is True and h["done"] == done + 1 and h["undo"] == "Edit Casts shadows",
            f"a click on the ring's toggle: casts shadows, one undo step ({h['undo']})")
    g.call("editor_undo", action="undo")
    # Only the primary: a second light selected after it has the handles.
    other = g.call("lights", action="add", position=[s.player["x"], s.player["y"] - 200, s.player["z"] + 120])["lights"][-1]["id"]
    s.select(action="set", ids=[s.light, other])
    g.run(2)
    s.check(handles(s)["subject"] == other, "two lights selected: the primary's handles only")
    s.select(action="set", ids=[other])
    s.keys(["delete"])
    s.select(action="set", ids=[s.light])


def check_spot(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "spot"})
    schema(s, action="set", id=s.light, values={"radius": 300, "cone": 60, "inner": 0, "direction": [0, 1, 0]})
    g.run(2)
    names = {h["name"] for h in handles(s)["handles"]}
    s.check({"intensity", "range", "cone", "inner", "aim"} <= names, f"a spot's handles ({sorted(names)})")
    check_drag(s, s.light, "range", "radius", "Range", 450, 450)
    check_drag(s, s.light, "cone", "cone", "Cone", 80, 80)
    check_drag(s, s.light, "inner", "inner", "Inner cone", 40, 40)  # from the automatic inner cone (about 52)
    check_escape(s, s.light, "cone", "cone", 90)
    # Aim at surface: the crosshair, then a click on the floor by Ashley.
    p = s.player
    floor = s.select(action="project", point=[p["x"] + 120, p["y"], p["z"] + 60])["pixel"]
    hit = s.select(action="pick", x=floor[0], y=floor[1])["world"]
    light = next(x for x in g.call("lights")["lights"] if x["id"] == s.light)
    done = s.history()["done"]
    s.click(handle(s, "aim")["screen"])
    s.check(handles(s)["aim"] == s.light, "the crosshair arms aiming")
    s.click(floor)
    want = [a - b for a, b in zip(hit, light["position"])]
    n = math.sqrt(sum(x * x for x in want))
    got, hist = value(s, s.light, "direction"), s.history()
    s.check(close(got, [x / n for x in want], 0.02) and hist["done"] == done + 1 and hist["undo"] == "Aim at surface"
            and handles(s)["aim"] is None,
            f"a click on the floor aims the spot there ({got} vs {[round(x / n, 3) for x in want]}; {hist['undo']})")
    g.call("editor_undo", action="undo")
    s.click(handle(s, "aim")["screen"])
    s.keys(["escape"])
    s.check(handles(s)["aim"] is None and s.keys([])["visible"], "Escape stops aiming (the editor stays open)")


def check_rect(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "rect"})
    schema(s, action="set", id=s.light, values={"radius": 300, "width": 128, "height": 96, "direction": [0, 1, 0],
                                                 "barn_angle": 45, "barn_length": 0})
    g.run(2)
    names = {h["name"] for h in handles(s)["handles"]}
    want = {"intensity", "width+", "width-", "height+", "height-", "range", "barn_angle", "barn_length"}
    s.check(want <= names, f"a rect's handles ({sorted(names)})")
    # Alt: both edges move, only the width changes.
    check_drag(s, s.light, "width+", "width", "Width", 192, 192, hold=["alt"])
    # A plain drag moves that edge only: the width and the centre change (the opposite edge stays).
    pos0 = value(s, s.light, "position")
    after = check_drag(s, s.light, "width+", "width", "Width", 2 * 192 - 128, 192, also=("position",))
    if after:
        moved = math.dist(after["position"], pos0)
        s.check(abs(moved - 32) < 2, f"the centre moved half the change (32 units: {moved:.1f})")
    check_drag(s, s.light, "height-", "height", "Height", 64, 64, hold=["alt"])
    check_drag(s, s.light, "range", "radius", "Range", 450, 450)
    check_drag(s, s.light, "barn_length", "barn_length", "Barn door length", 48, 48, tolerance=0.03)
    schema(s, action="set", id=s.light, values={"barn_length": 48})
    check_drag(s, s.light, "barn_angle", "barn_angle", "Barn door angle", 20, 20, tolerance=0.03)
    check_escape(s, s.light, "barn_angle", "barn_angle", 70)
    schema(s, action="reset", id=s.light, fields=["barn_length", "barn_angle"])
    schema(s, action="set", id=s.light, values={"type": "point"})


def check_sun(s):
    g = s.game

    def sun():
        return g.call("environment")["sun"]

    g.call("environment", sun={"enabled": True, "azimuth": 180, "elevation": 30, "intensity": 1, "color": [1, 1, 1]})
    g.run(4)
    o = s.objects()
    if not s.check("env:sun" in o and o["env:sun"]["screen"] is not None, "the sun's icon is on its dome in the view"):
        return
    s.click(o["env:sun"]["screen"])
    g.run(2)
    s.check(s.select()["selection"] == ["env:sun"], "a click on it selects the sun")
    names = {h["name"] for h in handles(s)["handles"]}
    s.check({"sun", "intensity"} <= names, f"the sun's handles ({sorted(names)})")
    h = handle(s, "sun")
    target = handles(s, action="where", handle="sun", value=[200, 40])["screen"]
    done = s.history()["done"]
    drag(s, h["screen"], target)
    after, hist = sun(), s.history()
    s.check(abs(after["azimuth"] - 200) < 1.5 and abs(after["elevation"] - 40) < 1.5 and hist["done"] == done + 1
            and hist["undo"] == "Edit Sun direction",
            f"its disc dragged across the dome: azimuth {after['azimuth']}, elevation {after['elevation']} (200, 40); "
            f"{hist['undo']!r}")
    g.call("editor_undo", action="undo")
    s.check(abs(sun()["azimuth"] - 180) < 0.5 and abs(sun()["elevation"] - 30) < 0.5, "undo turns it back")
    ring = handle(s, "intensity")
    drag(s, ring["screen"], [ring["screen"][0] + 100, ring["screen"][1]])
    s.check(close(sun()["intensity"], 2, 0.01) and s.history()["undo"] == "Edit Sun intensity",
            f"its ring: intensity 2 ({sun()['intensity']}, {s.history()['undo']!r})")
    g.call("editor_undo", action="undo")
    h = handle(s, "sun")
    target = handles(s, action="where", handle="sun", value=[160, 20])["screen"]
    drag(s, h["screen"], target, release=False)
    s.keys(["escape"])
    s.mouse(target, down=False)
    s.check(abs(sun()["azimuth"] - 180) < 0.5 and abs(sun()["elevation"] - 30) < 0.5, "Escape mid-drag puts the sun back")
    g.call("environment", remove="sun")
    s.select(action="set", ids=[s.light])


def check_volume(s):
    g = s.game
    s.select(action="set", ids=[s.volume])
    schema(s, action="set", id=s.volume, values={"density": 0.004, "edge": 0.5, "color": [1, 1, 1]})
    g.run(2)
    st = handles(s)
    names = {h["name"] for h in st["handles"]}
    if not s.check(st["subject"] == s.volume and st["hidden"] is None and "colour" in names,
                   f"a fog volume's handles ({st['hidden']}, {sorted(names)})"):
        return
    before, done = fields(s, s.volume), s.history()["done"]
    s.click(handle(s, "colour")["screen"])
    st = handles(s)["colour"]
    s.check(st["open"] and st["subject"] == s.volume, f"its swatch opens the colour popover on it ({st['subject']})")
    colour(s, kelvin=3200, tint=0)
    after = fields(s, s.volume)
    s.check(changed(before, after) == ["color"] and s.history()["done"] == done + 1
            and s.history()["undo"] == "Edit Colour", f"only its colour changes, one undo step ({changed(before, after)})")
    g.call("editor_undo", action="undo")
    s.keys(["escape"])
    st = handles(s)
    names = {h["name"] for h in st["handles"]}
    s.check({"density", "edge", "colour"} <= names, f"the density ring and the soft edge dot too ({sorted(names)})")
    check_drag(s, s.volume, "density", "density", "Density", 0.008, 0.008)  # 100 px right doubles it
    ring = handle(s, "density")
    drag(s, ring["screen"], [ring["screen"][0] + 60, ring["screen"][1]], hold=["shift"])  # 2^0.6 = 1.52: snaps to 0.005
    s.check(close(value(s, s.volume, "density"), 0.005, 0.01), f"Shift: 1, 2, 5 steps ({value(s, s.volume, 'density')})")
    g.call("editor_undo", action="undo")
    check_drag(s, s.volume, "edge", "edge", "Soft edge", 0.25, 0.25)
    check_escape(s, s.volume, "edge", "edge", 0.8)
    # The editor closing mid-drag (F1, RPC) ends the drag: its value stays, its one undo step closes.
    ring = handle(s, "density")
    before, done = value(s, s.volume, "density"), s.history()["done"]
    end = [ring["screen"][0] + 100, ring["screen"][1]]
    drag(s, ring["screen"], end, release=False)
    mid = value(s, s.volume, "density")
    g.call("editor_ui", visible=False)
    g.run(3)
    undo = g.call("editor_undo", action="list")
    s.mouse(end, down=False)
    g.call("editor_ui", visible=True)
    g.run(4)
    s.check(not close(mid, before) and close(value(s, s.volume, "density"), mid) and undo["hold"] == 0
            and not undo["open"] and undo["done"] == done + 1 and undo["undo"] == "Edit Density",
            f"closing the editor mid-drag keeps {mid} and closes one undo step (hold {undo['hold']}, open {undo['open']}, "
            f"{undo.get('undo')!r})")
    g.call("editor_undo", action="undo")
    # R: the scale tool still sizes it (only its size) while its handles show. The view's keys need
    # the pointer over the view (not a panel).
    s.mouse(centre(s.select()["view"]), down=False)
    s.keys(["r"])
    before = fields(s, s.volume)
    g.call("editor_transform", action="scale", ratio=[2, 1, 1])
    after = fields(s, s.volume)
    s.check(changed(before, after) == ["size"] and close(after["size"][0], before["size"][0] * 2)
            and g.call("editor_transform")["operation"] == 2 and handles(s)["subject"] == s.volume,
            f"R with its handles shown: only its size ({changed(before, after)}, {before['size']} -> {after['size']}, "
            f"operation {g.call('editor_transform')['operation']}, handles on {handles(s)['subject']})")
    g.call("editor_undo", action="undo")
    s.keys(["w"])


def check_fit(s):
    g = s.game
    _, wall = floor_and_wall(s)
    if not s.check(wall is not None, "a wall pixel in the view"):
        return
    xy, hit = wall
    s.select(action="clear")
    count, done = len(g.call("lights")["lights"]), s.history()["done"]
    r = handles(s, action="fit_window", x=xy[0], y=xy[1])
    lights = g.call("lights")["lights"]
    new = lights[-1]["id"] if len(lights) == count + 1 else None
    if not s.check(new is not None and lights[-1]["type"] == "rect" and r["light"] == new, f"a new rect light ({r})"):
        return
    f = fields(s, new)
    n = f["direction"]
    off = sum((f["position"][k] - hit["world"][k]) * n[k] for k in range(3))
    s.check(abs(sum(n[k] * hit["normal"][k] for k in range(3))) > 0.95 and abs(off - 2) < 0.5
            and f["width"] >= 8 and f["height"] >= 8 and s.select()["selection"] == [new]
            and s.history()["done"] == done + 1 and s.history()["undo"] == "Fit rect light to window",
            f"on the pane's plane, 2 units out, facing out, sized ({f['width']} x {f['height']}), selected, one undo step "
            f"({off:.2f}, {s.history()['undo']!r})")
    view = s.select()["view"]
    other = [xy[0] + (40 if xy[0] < view[0] + view[2] / 2 else -40), xy[1] + 20]
    r = handles(s, action="fit_window", x=other[0], y=other[1])
    s.check(r["light"] == new and len(g.call("lights")["lights"]) == count + 1,
            "with it selected a second fit moves that light (no new one)")
    g.call("editor_undo", action="undo")
    handles(s, action="fit_window", offset=10)
    handles(s, action="fit_window", x=xy[0], y=xy[1])
    f = fields(s, new)
    off = sum((f["position"][k] - hit["world"][k]) * f["direction"][k] for k in range(3))
    s.check(abs(off - 10) < 0.5, f"the offset is tunable (10: {off:.2f})")
    handles(s, action="fit_window", offset=2)
    handles(s, action="fit_arm")
    s.check(handles(s)["fit"]["armed"] and handles(s)["fit"]["target"] == new, "armed on the selected rect light")
    s.select(action="clear")  # its handles and gizmo out of the way of the click (the tool keeps its target)
    s.click(other)
    s.check(not handles(s)["fit"]["armed"] and s.history()["undo"] == "Fit rect light to window", "a real click fits it")
    handles(s, action="fit_arm", new=True)
    s.keys(["escape"])
    s.check(not handles(s)["fit"]["armed"] and s.keys([])["visible"], "Escape disarms (the editor stays open)")
    handles(s, action="fit_arm", new=True)
    g.call("editor_ui", visible=False)
    g.run(2)
    g.call("editor_ui", visible=True)
    g.run(2)
    s.check(not handles(s)["fit"]["armed"], "closing the editor disarms it (the next click after reopening only selects)")
    s.select(action="set", ids=[new])
    s.keys(["delete"])
    s.select(action="set", ids=[s.light])


def colour(s, **params):
    return handles(s, action="colour", **params)["colour"]


def check_colour(s):
    g = s.game
    g.call("actors", time=0)
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"type": "point"})
    schema(s, action="set", id=s.light, values={"color": [0.2, 0.4, 0.6]})
    g.run(2)
    s.click(handle(s, "colour")["screen"])
    st = handles(s)["colour"]
    s.check(st["open"] and st["subject"] == s.light and st["tab"] == "temperature",
            f"the swatch opens the popover on Temperature ({st['open']}, {st['tab']})")
    done = s.history()["done"]
    st = colour(s, kelvin=2700, tint=0)
    c = value(s, s.light, "color")
    s.check(close(c, st["colour"], 0.002) and close(c[0], 1) and c[0] > c[1] > c[2] and abs(st["kelvin"] - 2700) < 80,
            f"2700 K: a warm colour, brightest channel 1 ({c}; it reads back {st['kelvin']:.0f} K)")
    s.check(s.history()["done"] == done + 1 and s.history()["undo"] == "Edit Colour", "one undo step, Edit Colour")
    g.run(2)
    st = handles(s)["colour"]
    s.click(centre(st["rects"]["stop_1900"]))
    st = handles(s)["colour"]
    s.check(abs(st["kelvin"] - 1900) < 60 and value(s, s.light, "color")[2] < 0.3,
            f"the Candle stop: 1900 K ({st['kelvin']:.0f})")
    s.click(centre(st["rects"]["hex"]))  # the Hex field: typed, then Enter
    g.call("editor_input", text="#4080FF")
    s.keys(["enter"])
    c, st = value(s, s.light, "color"), handles(s)["colour"]
    blue = [64 / 255, 128 / 255, 1.0]
    s.check(close(c, blue, 0.005) and close(st["recent"][0], blue, 0.005),
            f"Hex #4080FF typed: the colour ({c}), first of the recent colours")
    candle = next(a for a in g.call("actors")["actors"] if a["id"] == s.actor)["lights"][0]["color"]
    k = next((i for i, p in enumerate(st["palette"]) if close(p, candle, 0.02)), None)
    if s.check(k is not None, f"the room's palette has the candle's colour ({st['palette']})"):
        done = s.history()["done"]
        s.click(centre(st["rects"][f"palette{k}"]))
        s.check(close(value(s, s.light, "color"), st["palette"][k], 0.002) and s.history()["done"] == done + 1,
                "a palette swatch clicked: the light takes it, one undo step")
    st = handles(s)["colour"]
    j = next((i for i, p in enumerate(st["recent"]) if close(p, blue, 0.005)), None)
    if s.check(j is not None, f"the hex colour is among the recent ones ({st['recent']})"):
        s.click(centre(st["rects"][f"recent{j}"]))
        s.check(close(value(s, s.light, "color"), blue, 0.005), "a recent colour clicked")
    tab = centre(handles(s)["colour"]["rects"]["tab_colour"])
    s.mouse(tab, down=False)  # hovered first: a tab takes a press only where the pointer already was
    s.click(tab)
    g.run(2)
    st = handles(s)["colour"]
    s.check(st["tab"] == "colour" and st["tabs"][0] == 1, f"the Colour tab, remembered for point lights ({st['tabs']})")
    s.keys(["escape"])
    s.check(not handles(s)["colour"]["open"] and s.keys([])["visible"], "Escape closes the popover (the editor stays open)")
    schema(s, action="set", id=s.light, values={"type": "spot"})
    colour(s, open=True)
    g.run(3)
    s.check(handles(s)["colour"]["tab"] == "temperature", "a spot opens on its own last tab (Temperature)")
    colour(s, open=False)
    schema(s, action="set", id=s.light, values={"type": "point"})
    colour(s, open=True)
    g.run(3)
    s.check(handles(s)["colour"]["tab"] == "colour", "a point light again: Colour")
    colour(s, open=False)
    g.call("settings", action="save")
    text = pathlib.Path(s.settings).read_text()
    s.check("editor_colour_tabs 1 0 0 0" in text and "editor_recent_colours" in text,
            "the tabs and the recent colours are user settings")


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def check_eyedropper(s):
    """The eyedropper: armed in the popover, a click on the picture gives the light that pixel's colour
    (display to linear, brightest channel 1), one undo step "Pick colour"."""
    g = s.game
    g.call("actors", time=0)
    s.select(action="set", ids=[s.light])
    g.run(2)
    colour(s, open=True)
    g.run(3)
    s.click(centre(handles(s)["colour"]["rects"]["eyedropper"]))
    st = handles(s)["colour"]
    s.check(st["armed"], "the Eyedropper button arms it")
    xs = [r[0] for r in st["rects"].values()] + [r[0] + r[2] for r in st["rects"].values()]
    ys = [r[1] for r in st["rects"].values()] + [r[1] + r[3] for r in st["rects"].values()]
    popover = [min(xs) - 20, min(ys) - 40, max(xs) + 20, max(ys) + 20]
    view = s.select()["view"]
    objects = [o["screen"] for o in s.select()["objects"] if o["screen"]]
    # Three pictures some frames apart: a spot that stays the same (the room's glows pulse).
    images = []
    for k in range(3):
        images.append(gf.capture(g, s.out / "eyedropper"))
        g.run(8)
    spot = None
    for fx, fy in ((0.1, 0.85), (0.9, 0.85), (0.1, 0.5), (0.5, 0.9), (0.9, 0.5), (0.3, 0.8), (0.7, 0.8), (0.3, 0.6)):
        xy = [view[0] + view[2] * fx, view[1] + view[3] * fy]
        inside = popover[0] <= xy[0] <= popover[2] and popover[1] <= xy[1] <= popover[3]
        boxes = [im[int(xy[1]) - 1:int(xy[1]) + 2, int(xy[0]) - 1:int(xy[0]) + 2].reshape(-1, 3).mean(axis=0) / 255
                 for im in images]
        steady = max(abs(a - b).max() for a in boxes for b in boxes) < 0.01
        box = boxes[-1]
        if not inside and steady and all(math.dist(xy, o) > 60 for o in objects) and box.max() > 0.12:
            spot = (xy, box)
            break
    if not s.check(spot is not None, "a lit, steady spot of the picture away from the popover and the icons"):
        return
    xy, box = spot
    want = [srgb_to_linear(c) for c in box]
    want = [c / max(want) for c in want]
    done = s.history()["done"]
    s.click(xy)
    g.run(4)
    got, hist = value(s, s.light, "color"), s.history()
    s.check(close(got, want, 0.06) and hist["done"] == done + 1 and hist["undo"] == "Pick colour",
            f"a click on the picture: the light takes its colour ({got} vs {[round(c, 3) for c in want]}; {hist['undo']})")
    s.check(not handles(s)["colour"]["armed"] and s.select()["selection"] == [s.light],
            "the click was the eyedropper's (the selection unchanged)")
    g.call("editor_undo", action="undo")
    colour(s, open=False)


SECTIONS = [check_layer, check_point, check_spot, check_rect, check_sun, check_volume, check_colour, check_eyedropper, check_fit]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (layer, point, ...)")
    args = parser.parse_args()
    s = start(args, "editor_handles")
    if s is None:
        return 77
    try:
        only = set(args.only.split(",")) if args.only else None
        for section in SECTIONS if not s.failures else []:
            name = section.__name__[len("check_"):]
            if only is None or name in only:
                print(f"-- {name}", flush=True)
                section(s)
    finally:
        status = finish(s, "handles on the selected light and the sun; the colour popover")
    return status


if __name__ == "__main__":
    sys.exit(main())
