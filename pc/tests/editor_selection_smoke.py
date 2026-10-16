#!/usr/bin/env python3
"""The editor's selection (pc/src/editor_selection.h, editor_scene.cpp): one
set of typed references (light, light actor, glow, fog volume) shared by the
viewport, the Outliner, Details, undo and the RPC.

    python3 pc/tests/editor_selection_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room with the editor open, a scratch lights directory
(ROOD_LIGHTS) and a scratch settings file (ROOD_SETTINGS). A point light, a
candle and a fog volume are placed around Ashley; each section in SECTIONS
checks one part:

- rpc: the RPC's selection is what the lights, actors, glows and fog
  volumes report as selected; unknown ids are refused; undo restores it;
- picking: a click selects a light, an actor (its pivot), a glow or a fog
  volume (inside its outline, by a ray), through the RPC and with real
  mouse events; a click on nothing selects nothing;
- modifiers: Shift adds, Ctrl toggles; the primary has the gizmo;
- box: a drag on empty space selects the light and the candle (real mouse
  events); Shift+box adds, Ctrl+box toggles, a plain box replaces;
- outliner: a selection from elsewhere scrolls the Outliner to its row (not
  one from the Outliner itself); fog volumes have rows;
- details_follow: a selection switches to the Details tab (not one from
  Details); every room-wide tab still opens;
- hide: H hides the selection (no icon, no pick, no light while the editor
  shows), Ctrl+H shows all, Shift+H hides the rest; the set is a user
  setting (saved and loaded), never lighting data;
- clipboard: Ctrl+C / Ctrl+V paste the light and candle in place with new
  ids, Ctrl+D 64 units along x; a fog volume duplicates; each is one undo
  step;
- flying: while the camera flies (right mouse held) Ctrl+D, Ctrl+V and H
  are the fly's keys, not editor shortcuts;
- delete: Delete takes lights, actors and fog volumes in one undo step; undo
  restores them selected;
- frame: F frames a fog volume;
- view: project gives a world point's window pixel, pick the surface under a
  pixel (the handles' tests use both).
"""
import argparse
import math
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from checkpoints import launch_at  # noqa: E402
import golden_frames as gf  # noqa: E402

VOLUME = "seltest0000001"  # the fog volume's id (item fog:<id>)


def pinned_card():
    """The tests' pinned card (run.py default_save): pc/build/test-card1.mcd, here or in the main checkout."""
    roots = [ROOT]
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
    if common:
        roots.append(pathlib.Path(common).parent)
    for root in roots:
        if (root / "pc/build/test-card1.mcd").exists():
            return root / "pc/build/test-card1.mcd"
    return ROOT / "pc/build/test-card1.mcd"


def centre(rect):
    return [rect[0] + rect[2] / 2, rect[1] + rect[3] / 2]


def distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


class Session:
    """A game in the save room with the editor open and three objects placed around Ashley."""

    def __init__(self, game, out):
        self.game, self.out, self.failures = game, out, []
        self.light = self.actor = self.volume = None
        self.player = None
        self.zone = self.room = None
        self.lights_dir = self.settings = None

    def check(self, ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            self.failures.append(message)
        return ok

    def select(self, **params):
        return self.game.call("editor_select", **params)

    def objects(self):
        return {o["id"]: o for o in self.select()["objects"]}

    def keys(self, names, **kw):
        self.game.call("editor_input", keys=names, **kw)
        self.game.run(4)  # the events reach the overlay over the next frames
        return self.game.call("editor_input")

    def mouse(self, xy, button=1, down=True):
        """Real SDL mouse events (RPC system mouse): the pointer to window pixel xy, then the button down or up."""
        self.game.call("system", mouse=[float(xy[0]), float(xy[1])], button=button, down=down)
        self.game.run(3)

    def click(self, xy, button=1):
        self.mouse(xy, button, True)
        self.mouse(xy, button, False)

    def index(self, method, ident):
        return [x["id"] for x in self.game.call(method)[method]].index(ident)

    def history(self):
        return self.game.call("editor_undo", action="list")

    def place(self):
        """A point light, a candle and a fog volume around Ashley, each with its own spot on screen."""
        g, p = self.game, self.player
        self.light = g.call("lights", action="add", position=[p["x"] - 160, p["y"] - 140, p["z"]])["lights"][-1]["id"]
        g.call("actors", action="add", prefab="candle", position=[p["x"] + 160, p["y"] - 120, p["z"]], seed=3)
        self.actor = g.call("actors")["actors"][-1]["id"]
        self.volume = "fog:" + VOLUME
        for dz in (320, -320, 480, -480, 640, -640):
            g.call("environment", volumes=[{"id": VOLUME, "pos": [p["x"], p["y"] - 64, p["z"] + dz],
                                            "size": [160, 128, 160]}])
            g.run(6)
            o = self.objects()
            mine = [o[i]["screen"] for i in (self.light, self.actor, self.volume)]
            others = [x["screen"] for i, x in o.items() if x["screen"] and i != self.volume]
            # ... and no room surface in front of the volume's centre (a click there picks it)
            if all(mine) and all(distance(mine[2], xy) > 60 for xy in others) \
                    and self.select(action="click", x=mine[2][0], y=mine[2][1])["under"] == self.volume:
                self.select(action="clear")
                return True
        return False


def start(args, name):
    """Launch into the save room, editor open, objects placed: a Session, or None to skip."""
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return None
    out = pathlib.Path(args.out) if args.out else OUT / name
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    # A settings file with an aa line: no first-run detection (lighting keeps its built-in values).
    settings.write_text(f"# {name}: scratch settings\naa fxaa\n")
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_SETTINGS": str(settings), "ROOD_SCALE": "2"}
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-{name[7:12]}-{os.getpid()}.sock"  # sun_path is short
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket)
    s = Session(game, out)
    s.lights_dir, s.settings = lights, settings
    game.call("graphics", **{**gf.DEFAULTS, "scale": 2, "editor_cutaway": False, "editor_snap_point": False,
                             "editor_snap_edge": False, "editor_snap_surface": False})
    state = game.state()
    s.player = state.get("player")
    if not s.player or not game.call("scene")["triangles"]:
        s.check(False, f"no room with Ashley after Continue: {state}")
        return s
    s.zone, s.room = state["zone"], state["room"]
    game.call("editor_ui", visible=True)
    game.run(6)
    s.check(s.place(), "the light, the candle and the fog volume each have their own spot on screen")
    game.run(6)
    return s


def finish(s, label):
    """Close the game; the exit status (a crash or shader error fails)."""
    s.game.close()
    shutil.rmtree(s.lights_dir, ignore_errors=True)
    log = pathlib.Path(s.game.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", s.game.log)
        return 1
    if s.failures:
        print(f"FAIL: {len(s.failures)} checks")
        return 1
    print("PASS: " + label)
    return 0


def check_rpc(s):
    """The RPC's selection is what every kind's own state reports; undo restores it."""
    g = s.game
    r = s.select(action="set", ids=[s.light])
    st = g.call("editor_place", phase="state")
    s.check(r["selection"] == [s.light] and r["primary"] == s.light and r["source"] == "rpc"
            and st["selected_light"] == s.index("lights", s.light) and st["selected_actor"] == -1,
            f"set: the light is the selection and the lights' own selected index ({r['selection']}, {st})")
    r = s.select(action="add", ids=[s.actor])
    st = g.call("editor_place", phase="state")
    s.check(r["selection"] == [s.light, s.actor] and st["selected_actor"] == s.index("actors", s.actor)
            and st["selected_light"] == -1, "add: the actor joins as the primary (the gizmo's object)")
    r = s.select(action="toggle", ids=[s.light])
    s.check(r["selection"] == [s.actor], f"toggle: the light goes ({r['selection']})")
    g.call("actors", action="select", index=s.index("actors", s.actor))
    s.check(s.select()["selection"] == [s.actor], "the actors RPC's select is the selection")
    g.call("environment", select_volume=VOLUME)
    r = s.select()
    s.check(r["selection"] == [s.volume], f"environment select_volume: the fog volume ({r['selection']})")
    glows = [x for x in g.call("glows")["glows"] if x["category"] != "effect"]
    if glows:
        g.call("glows", action="select", id=glows[0]["id"])
        g.run(2)
        r = s.select()
        s.check(len(r["selection"]) == 1 and r["primary"].startswith("glow:")
                and g.call("glows")["selected"] == glows[0]["id"],
                f"glows select: the glow, by its key ({r['selection']})")
    try:
        s.select(action="set", ids=["light:nosuchlight"])
        s.check(False, "an unknown id is refused")
    except RpcError:
        s.check(True, "an unknown id is refused")
    s.select(action="set", ids=[s.actor])
    s.keys(["delete"])
    gone = s.actor not in [a["id"] for a in g.call("actors")["actors"]]
    s.check(gone and s.select()["selection"] == [], "Delete: the actor goes, and with it the selection")
    g.call("editor_undo", action="undo")
    r = s.select()
    s.check(r["selection"] == [s.actor], f"undo: the actor and its selection are back ({r['selection']})")


def check_picking(s):
    """A click picks what is under it: an icon first, else a fog volume (a ray against its box)."""
    o = s.objects()
    glows = [i for i, x in o.items() if x["kind"] == "glow" and x["screen"]][:1]
    for ident in [s.light, s.actor, s.volume] + glows:
        xy = o[ident]["screen"]
        if not s.check(xy is not None, f"{o[ident]['label']} is on screen"):
            continue
        r = s.select(action="click", x=xy[0], y=xy[1])
        s.check(r["under"] == ident and r["selection"] == [ident] and r["source"] == "rpc",
                f"a click on {o[ident]['label']} selects it ({r['selection']})")
    s.select(action="clear")
    s.click(o[s.volume]["screen"])
    r = s.select()
    s.check(r["selection"] == [s.volume] and r["source"] == "viewport",
            f"a mouse click inside the fog volume's outline picks it ({r['selection']})")
    view, empty = r["view"], None
    for fx, fy in ((0.06, 0.1), (0.94, 0.1), (0.06, 0.9), (0.94, 0.9), (0.5, 0.06)):
        xy = [view[0] + view[2] * fx, view[1] + view[3] * fy]
        if all(distance(xy, x["screen"]) > 80 for x in o.values() if x["screen"]) \
                and s.select(action="click", x=xy[0], y=xy[1], shift=True)["under"] is None:
            empty = xy
            break
    if s.check(empty is not None, "the view has an empty spot"):
        s.select(action="set", ids=[s.light])
        s.click(empty)
        s.check(s.select()["selection"] == [], "a mouse click on nothing selects nothing")


def check_modifiers(s):
    """Shift adds, Ctrl toggles (Unreal); the primary has the gizmo."""
    o = s.objects()

    def click(ident, **kw):
        xy = o[ident]["screen"]
        return s.select(action="click", x=xy[0], y=xy[1], **kw)["selection"]

    s.check(click(s.light) == [s.light], "click: the light alone")
    s.check(click(s.actor, shift=True) == [s.light, s.actor], "Shift+click adds the candle")
    s.check(click(s.volume, shift=True) == [s.light, s.actor, s.volume], "Shift+click adds the fog volume")
    s.check(click(s.light, ctrl=True) == [s.actor, s.volume], "Ctrl+click takes the light out")
    s.check(click(s.light, ctrl=True) == [s.actor, s.volume, s.light], "Ctrl+click puts it back, as the primary")
    st = s.game.call("editor_place", phase="state")
    s.check(st["selected_light"] == s.index("lights", s.light), "the primary has the gizmo")


def check_box(s):
    """A drag on empty space boxes what it encloses; Shift adds, Ctrl toggles."""
    o = s.objects()
    a, b, v = o[s.light]["screen"], o[s.actor]["screen"], o[s.volume]["screen"]
    others = [x["screen"] for i, x in o.items() if x["screen"] and i not in (s.light, s.actor)]
    boxable = [x["screen"] for i, x in o.items() if x["screen"] and i not in (s.light, s.actor) and x["kind"] != "glow"]
    lo = hi = None
    for margin in (30, 50, 70):
        lo = [min(a[0], b[0]) - margin, min(a[1], b[1]) - margin]
        hi = [max(a[0], b[0]) + margin, max(a[1], b[1]) + margin]
        # glows may lie inside: box select never takes them
        enclosed = [xy for xy in boxable if lo[0] <= xy[0] <= hi[0] and lo[1] <= xy[1] <= hi[1]]
        if not enclosed and all(distance(lo, xy) > 30 for xy in others + [a, b]) \
                and s.select(action="click", x=lo[0], y=lo[1], shift=True)["under"] is None:
            break
    else:
        s.check(False, "test geometry: a box around the light and the candle alone, from an empty corner")
        return
    s.select(action="clear")
    s.mouse(lo, 1, True)
    s.mouse([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2], 1, True)
    s.mouse(hi, 1, True)
    s.mouse(hi, 1, False)
    r = s.select()
    s.check(sorted(r["selection"]) == sorted([s.light, s.actor]) and r["source"] == "viewport",
            f"a drag on empty space boxes the light and the candle ({r['selection']})")
    r = s.select(action="box", x0=v[0] - 20, y0=v[1] - 20, x1=v[0] + 20, y1=v[1] + 20, shift=True)
    s.check(sorted(r["selection"]) == sorted([s.light, s.actor, s.volume]), "Shift+box adds the fog volume")
    r = s.select(action="box", x0=a[0] - 10, y0=a[1] - 10, x1=a[0] + 10, y1=a[1] + 10, ctrl=True)
    s.check(sorted(r["selection"]) == sorted([s.actor, s.volume]), "Ctrl+box takes the light out")
    r = s.select(action="box", x0=a[0] - 10, y0=a[1] - 10, x1=a[0] + 10, y1=a[1] + 10)
    s.check(r["selection"] == [s.light], "a plain box replaces the selection")


def check_outliner(s):
    """A selection made anywhere but the Outliner scrolls the Outliner to its row and opens its section."""
    g, p = s.game, s.player
    for k in range(30):
        g.call("lights", action="add", position=[p["x"] + 8 * k, p["y"] - 300, p["z"]])
    ids = [x["id"] for x in g.call("lights")["lights"]]
    g.call("editor_ui", tab="outliner")
    s.select(action="set", ids=[ids[0]])
    g.run(6)
    first = s.select()["outliner"]
    s.select(action="set", ids=[ids[-1]], source="outliner")
    g.run(6)
    unscrolled = s.select()["outliner"]
    s.select(action="set", ids=[ids[-1]])
    g.run(6)
    scrolled = s.select()["outliner"]
    s.check(first["target"] == ids[0] and first["visible"], f"the first light's row shows ({first})")
    s.check(unscrolled["target"] == ids[-1] and not unscrolled["visible"],
            f"a selection made in the Outliner itself does not scroll it (the 31st light is out of sight: {unscrolled})")
    s.check(scrolled["target"] == ids[-1] and scrolled["visible"],
            f"a selection made elsewhere scrolls the Outliner to its row ({scrolled})")
    s.select(action="set", ids=[s.volume])
    g.run(6)
    r = s.select()["outliner"]
    s.check(r["target"] == s.volume and r["visible"], f"fog volumes are in the Outliner too ({r})")
    g.call("editor_undo", action="undo", steps=30)
    s.check(len(g.call("lights")["lights"]) == len(ids) - 30, "the 30 extra lights are undone")


def check_details_follow(s):
    """Details shows the selection: a new one switches to the Details tab, unless it came from Details."""
    g = s.game
    g.call("editor_ui", details="graphics")
    g.run(3)
    s.select(action="set", ids=[s.light])
    g.run(3)
    s.check(g.call("editor_ui")["details"] == "details", "a selection switches Details to the Details tab")
    g.call("editor_ui", details="atmosphere")
    g.run(3)
    s.select(action="set", ids=[s.actor], source="details")
    g.run(3)
    s.check(g.call("editor_ui")["details"] == "atmosphere", "a change made from Details itself keeps the tab")
    for name in ("lighting", "inspector", "atmosphere", "post", "graphics", "emissive", "diagnostics", "details"):
        g.call("editor_ui", details=name)
        g.run(2)
        s.check(g.call("editor_ui")["details"] == name, f"the {name} tab opens on its own")


def check_hide(s):
    """H hides the selection in the editor (its icon, picking, its light while the editor shows), Ctrl+H shows
    everything, Shift+H hides everything else; kept in the user settings, never in the room's lighting."""
    g = s.game
    g.run(2)
    before = g.call("actors")["lights_wanted"]
    xy = s.objects()[s.light]["screen"]
    s.select(action="set", ids=[s.light])
    s.keys(["h"])
    r = s.select()
    o = {x["id"]: x for x in r["objects"]}
    s.check(s.light in r["hidden"] and r["selection"] == [] and o[s.light]["hidden"] and o[s.light]["screen"] is None,
            f"H: the light is hidden and deselected ({r['hidden']})")
    s.check(r["hidden_setting"] == f"editor_hidden_{s.zone}_{s.room} {s.light}", f"its settings line ({r['hidden_setting']})")
    g.run(2)
    s.check(g.call("actors")["lights_wanted"] == before - 1, "while the editor shows, a hidden light lights nothing")
    s.check(s.light not in s.select(action="click", x=xy[0], y=xy[1])["selection"], "a hidden light cannot be picked")
    saved = g.call("lighting", action="save")
    text = (s.lights_dir / f"zone{s.zone:02d}_room{s.room:02d}.txt").read_text()
    s.check(saved.get("ok") and s.light.split(":", 1)[1] in text and "hidden" not in text,
            "saving the room writes the light; nothing about hiding goes into the lighting data")
    g.call("settings", action="save")
    s.check(f"editor_hidden_{s.zone}_{s.room} {s.light}" in s.settings.read_text(), "File > Save settings keeps the hidden set")
    s.keys(["h"], ctrl=True)
    g.run(2)
    s.check(s.select()["hidden"] == [] and g.call("actors")["lights_wanted"] == before, "Ctrl+H shows everything again")
    loads, history = g.call("lights")["loads"], s.history()
    g.call("settings", action="load")
    g.run(2)
    s.check(s.select()["hidden"] == [s.light], "loading the settings brings the hidden set back")
    time.sleep(0.7)  # the editor polls the lighting config and room files twice a second (wall clock)
    g.run(2)
    s.check(g.call("lights")["loads"] == loads and s.history()["done"] == history["done"],
            "loading the settings it saved changes no lighting config: the room is not reloaded, its undo history stays "
            f"(loads {g.call('lights')['loads']} vs {loads}, done {s.history()['done']} vs {history['done']})")
    s.keys(["h"], ctrl=True)
    s.select(action="set", ids=[s.actor])
    s.keys(["h"], shift=True)
    r = s.select()
    s.check(s.light in r["hidden"] and s.volume in r["hidden"] and s.actor not in r["hidden"] and r["selection"] == [s.actor],
            f"Shift+H hides everything but the selection ({r['hidden']})")
    s.keys(["h"], ctrl=True)
    s.check(s.select()["hidden"] == [], "and Ctrl+H shows it all")
    vxy = s.objects()[s.volume]["screen"]
    s.select(action="hide", ids=[s.volume])
    r = s.select(action="click", x=vxy[0], y=vxy[1])
    s.check(r["under"] != s.volume and s.volume not in r["selection"], f"a hidden fog volume cannot be picked ({r['under']})")
    s.keys(["h"], ctrl=True)


def check_clipboard(s):
    """Ctrl+C / Ctrl+V / Ctrl+D through the lighting item format: new ids, selected, one undo step each."""
    g = s.game

    def counts():
        return len(g.call("lights")["lights"]), len(g.call("actors")["actors"]), len(g.call("environment")["volumes"])

    def position(method, ident):
        return next(x for x in g.call(method)[method] if x["id"] == ident)["position"]

    start = counts()
    s.select(action="set", ids=[s.light, s.actor])
    copied = s.select(action="copy")
    text = copied["clipboard"]
    s.check(copied["count"] == 2 and text.startswith("rood-lighting 2") and f"light {s.light[6:]} " in text
            and f"actor {s.actor[6:]} " in text, "copy: the light and the candle as lighting entries")
    s.keys(["c"], ctrl=True)
    s.keys(["v"], ctrl=True)
    sel = s.select()["selection"]
    new_light = next((i for i in sel if i.startswith("light:")), None)
    new_actor = next((i for i in sel if i.startswith("actor:")), None)
    s.check(counts() == (start[0] + 1, start[1] + 1, start[2]) and new_light and new_actor
            and s.light not in sel and s.actor not in sel, f"Ctrl+V: a new light and candle, selected ({sel})")
    s.check(position("lights", new_light) == position("lights", s.light), "pasted where they were copied")
    s.check(s.history()["undo"] == "Paste 2 objects", f"one undo step ({s.history()['undo']})")
    s.keys(["d"], ctrl=True)
    sel = s.select()["selection"]
    dup = next(i for i in sel if i.startswith("light:"))
    a, b = position("lights", dup), position("lights", new_light)
    s.check(counts() == (start[0] + 2, start[1] + 2, start[2]) and abs(a[0] - b[0] - 64) < 0.01 and a[1:] == b[1:],
            "Ctrl+D: copies 64 units along x, selected")
    s.check(s.history()["undo"] == "Duplicate 2 objects", f"one undo step ({s.history()['undo']})")
    g.call("editor_undo", action="undo", steps=2)
    s.check(counts() == start, "undo takes both away")
    s.select(action="set", ids=[s.volume])
    r = s.select(action="duplicate")
    volumes = g.call("environment")["volumes"]
    s.check(r["count"] == 1 and len(volumes) == start[2] + 1 and r["primary"].startswith("fog:") and r["primary"] != s.volume,
            "a fog volume duplicates (through its item: preset and fields)")
    g.call("editor_undo", action="undo")
    r = s.select(action="paste", text=text)
    s.check(r["count"] == 2 and counts() == (start[0] + 1, start[1] + 1, start[2]), "pasting text from the RPC")
    g.call("editor_undo", action="undo")


def check_flying(s):
    """While the camera flies (right mouse held in the view) the keys fly it: Ctrl+D, Ctrl+V and H do nothing."""
    g = s.game

    def counts():
        return len(g.call("lights")["lights"]), len(g.call("actors")["actors"]), len(g.call("environment")["volumes"])

    s.select(action="set", ids=[s.light])
    s.keys(["c"], ctrl=True)
    start, undo = counts(), s.history().get("undo")
    view = centre(s.select()["view"])
    s.mouse(view, 3, True)
    s.keys(["d"], ctrl=True)
    s.keys(["v"], ctrl=True)
    s.keys(["h"])
    r = s.select()
    s.mouse(view, 3, False)
    s.check(counts() == start and s.history().get("undo") == undo, f"flying: Ctrl+D / Ctrl+V add nothing ({counts()} vs {start}; undo {s.history().get('undo')!r} vs {undo!r})")
    s.check(r["hidden"] == [] and r["selection"] == [s.light], f"flying: H hides nothing ({r['hidden']})")
    g.call("editor_camera", off=1)
    g.run(4)
    s.keys(["d"], ctrl=True)
    s.check(counts()[0] == start[0] + 1, "after landing, Ctrl+D duplicates again")
    g.call("editor_undo", action="undo")
    s.select(action="set", ids=[s.light])


def check_delete(s):
    """Delete takes the whole selection (lights, actors, fog volumes) as one undo step; undo brings it back selected."""
    g = s.game
    s.select(action="set", ids=[s.light, s.actor, s.volume])
    s.keys(["delete"])
    ids = [x["id"] for x in s.select()["objects"]]
    s.check(s.light not in ids and s.actor not in ids and s.volume not in ids, "Delete: the light, the candle and the fog volume go")
    s.check(s.history()["undo"] == "Delete 3 objects", f"one undo step ({s.history()['undo']})")
    g.call("editor_undo", action="undo")
    r = s.select()
    ids = [x["id"] for x in r["objects"]]
    s.check({s.light, s.actor, s.volume} <= set(ids) and r["selection"] == [s.light, s.actor, s.volume],
            f"undo: all three back, and selected ({r['selection']}; {len(ids)} objects; history {s.history()})")


def check_frame(s):
    """F frames the selection's primary, a fog volume too (the free camera)."""
    g, p = s.game, s.player
    s.select(action="set", ids=[s.volume])
    centre_of = next(v for v in g.call("environment")["volumes"] if v["id"] == VOLUME)["pos"]
    g.call("editor_camera", x=p["x"] + 1500, y=p["y"] - 800, z=p["z"] + 1500, yaw=0, pitch=0)
    g.run(8)
    s.keys(["f"])
    cam = g.call("editor_camera")
    d = math.dist([cam["x"], cam["y"], cam["z"]], centre_of)
    s.check(abs(d - 300) < 5, f"F frames the fog volume ({d:.1f} units from its centre, 300 expected)")
    g.call("editor_camera", off=1)
    g.run(4)


def check_view(s):
    """project / pick (the tests' view helpers): a light's position projects onto its icon; a pick under Ashley finds a surface by her."""
    o = s.objects()
    light = next(x for x in s.game.call("lights")["lights"] if x["id"] == s.light)
    px = s.select(action="project", point=light["position"])["pixel"]
    s.check(px is not None and distance(px, o[s.light]["screen"]) < 1.0,
            f"project: the light's position is its icon ({px} vs {o[s.light]['screen']})")
    p = s.player
    feet = s.select(action="project", point=[p["x"], p["y"], p["z"]])["pixel"]
    hit = s.select(action="pick", x=feet[0], y=feet[1])["world"] if feet else None
    s.check(hit is not None and math.dist(hit, [p["x"], p["y"], p["z"]]) < 160,
            f"pick under Ashley's feet: a surface by her ({hit})")


# In order: later sections start from where earlier ones leave the room.
SECTIONS = [check_rpc, check_picking, check_modifiers, check_box, check_outliner, check_details_follow, check_hide,
            check_clipboard, check_flying, check_delete, check_frame, check_view]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (rpc, picking, ...)")
    args = parser.parse_args()
    s = start(args, "editor_selection")
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
        status = finish(s, "the selection follows the RPC, clicks, boxes and the Outliner; Details shows it; "
                           "hiding, copying and deleting act on it")
    return status


if __name__ == "__main__":
    sys.exit(main())
