#!/usr/bin/env python3
"""The editor's property schema (pc/src/editor_schema.h) and Details drawn
from it (editor_details.cpp).

    python3 pc/tests/editor_schema_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The same save room and objects as editor_selection_smoke.py (a point light,
a candle, a fog volume). Sections:

- lint: every field of every kind has a name, label, category, a range and
  a default inside it (the schema lint, and the kinds as listed);
- lights: RPC get / set round-trips each light field (clamped to its range),
  the type switches the kind, reset (the ↺) restores the default, and each
  edit is one undo step named after its field;
- actors: a light actor's fields are its transform and its prefab's
  parameters; a parameter set is an override, reset drops it;
- fog: a fog volume's fields round-trip; reset restores the default, or the
  preset's value when it has one;
- details: Details draws the selection's fields by category (a fog
  volume's Look closed at first); a changed value's reset arrow, clicked
  with the mouse, restores it;
- search_lock: search shows only matching fields; the lock keeps Details on
  its object (no tab switch), RPC edits without an id go to it; unlocking
  follows the selection again;
- ladder: a middle press opens the value ladder on the field's step; one row
  up and five notches right adds 0.5 (Intensity), three notches left in
  steps of 10 takes 30 (Radius); the release is one undo step; Escape puts
  the value back and leaves the editor open;
- multi: with several objects of one kind selected, a shared field goes to
  each, the position to the edited one only, and a vec3 component dragged
  on the ladder leaves the others' other components alone;
- sun: the room's sun is a schema kind (env:sun): get / set / reset its
  fields (clamped), one undo step each; the environment RPC follows;
  Details draws its fields;
- reload: a prefab kind taken before the prefab library reloads in the same
  frame still reads (its old fields, retired until the next frame); after a
  frame the kind is the reloaded one;
- standalone: a kind registered with add_kind and no scene object, the
  menu model's studio light (ui/menu_light_kind.cpp): RPC editor_schema
  {kind} gets / sets / resets it (clamped; one undo step, undo and redo put
  it back and forth), the rig follows (graphics menu_light), reset returns
  the shipped rig exactly; the dev tools' Graphics tab draws it
  (rood_details::draw_kind under kind_section): its fields, its own search,
  a changed value's reset arrow clicked, the value ladder on Intensity.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import RpcError  # noqa: E402
from editor_selection_smoke import VOLUME, centre, finish, pinned_card, start  # noqa: E402

EXPECTED_KINDS = {"point", "spot", "rect", "fog", "sun", "actor:candle", "actor:torch", "menu_model_light", "emitter", "particles"}


def schema(s, **params):
    return s.game.call("editor_schema", **params)


def close(a, b, tolerance=1e-3):
    if isinstance(a, list) or isinstance(b, list):
        return isinstance(a, list) and isinstance(b, list) and len(a) == len(b) \
            and all(abs(x - y) <= tolerance * max(1, abs(y)) for x, y in zip(a, b))
    if isinstance(a, (bool, str)) or isinstance(b, (bool, str)):
        return a == b
    return abs(a - b) <= tolerance * max(1, abs(b))


def value(s, ident, field):
    return schema(s, action="get", id=ident)["fields"][field]["value"]


def check_lint(s):
    lint = schema(s, action="lint")
    s.check(lint["problems"] == [] and lint["fields"] > 0,
            f"the schema lint is clean ({lint['kinds']} kinds, {lint['fields']} fields; {lint['problems'][:5]})")
    kinds = {k["name"]: k for k in schema(s, action="kinds")["kinds"]}
    s.check(EXPECTED_KINDS <= set(kinds), f"the kinds ({sorted(kinds)})")
    bad = [f"{k['name']}.{f['name']}" for k in kinds.values() for f in k["fields"]
           if not f["min"] < f["max"] or f["default"] is None]
    s.check(not bad, f"every field has a range and a default, as listed ({bad[:5]})")


def check_lights(s):
    g = s.game
    got = schema(s, action="get", id=s.light)
    s.check(got["kind"] == "point" and close(got["fields"]["intensity"]["value"], 1)
            and got["fields"]["radius"]["source"] == "default", f"a new point light: its kind and defaults ({got['kind']})")
    before = s.history()["done"]
    values = {"intensity": 2.5, "color": [0.2, 0.4, 0.6], "radius": 900, "shadows": True}
    for name, v in values.items():
        schema(s, action="set", id=s.light, values={name: v})
        s.check(close(value(s, s.light, name), v), f"set {name} = {v}: get reads it back")
    light = next(x for x in g.call("lights")["lights"] if x["id"] == s.light)
    s.check(close(light["intensity"], 2.5) and close(light["color"], [0.2, 0.4, 0.6]) and close(light["radius"], 900),
            "the lights RPC sees the same values")
    h = s.history()
    s.check(h["done"] == before + len(values) and h["undo"] == "Edit Casts shadows",
            f"one undo step per edit, named after the field ({h['undo']})")
    schema(s, action="set", id=s.light, values={"intensity": 99})
    s.check(close(value(s, s.light, "intensity"), 8), "a value past the range is clamped to it (8)")
    schema(s, action="reset", id=s.light, fields=["radius", "intensity"])
    f = schema(s, action="get", id=s.light)["fields"]
    s.check(close(f["radius"]["value"], 600) and close(f["intensity"]["value"], 1) and not f["radius"]["overridden"],
            "reset restores the defaults")
    s.check(s.history()["undo"].startswith("Reset"), f"and is named Reset ({s.history()['undo']})")
    schema(s, action="set", id=s.light, values={"type": "spot"})
    got = schema(s, action="get", id=s.light)
    s.check(got["kind"] == "spot" and "cone" in got["fields"], "the type switches the light's kind (a spot has a cone)")
    schema(s, action="set", id=s.light, values={"cone": 60, "direction": [1, 0, 0]})
    s.check(close(value(s, s.light, "cone"), 60) and close(value(s, s.light, "direction"), [1, 0, 0]),
            "spot fields round-trip")
    schema(s, action="set", id=s.light, values={"inner": 20})
    s.check(close(value(s, s.light, "inner"), 20), "a spot's inner cone round-trips")
    schema(s, action="set", id=s.light, values={"type": "rect"})
    schema(s, action="set", id=s.light, values={"width": 200, "height": 96})
    got = schema(s, action="get", id=s.light)
    s.check(got["kind"] == "rect" and close(got["fields"]["width"]["value"], 200)
            and close(got["fields"]["height"]["value"], 96), "rect fields round-trip")
    schema(s, action="set", id=s.light, values={"barn_angle": 30, "barn_length": 48})
    s.check(close(value(s, s.light, "barn_angle"), 30) and close(value(s, s.light, "barn_length"), 48),
            "a rect's barn doors round-trip")
    schema(s, action="reset", id=s.light, fields=["barn_angle", "barn_length"])
    schema(s, action="set", id=s.light, values={"type": "point"})
    try:
        schema(s, action="set", id=s.light, values={"nonsense": 1})
        s.check(False, "an unknown field is refused")
    except RpcError:
        s.check(True, "an unknown field is refused")


def check_actors(s):
    got = schema(s, action="get", id=s.actor)
    f = got["fields"]
    s.check(got["kind"] == "actor:candle" and {"position", "rotation", "intensity", "tint"} <= set(f)
            and f["intensity"]["source"] == "prefab" and not f["intensity"]["overridden"],
            f"a candle: its transform and its prefab's parameters ({sorted(f)})")
    schema(s, action="set", id=s.actor, values={"intensity": 2.5, "tint": [1, 0.5, 0.25], "rotation": [90, 0, 0]})
    actor = next(a for a in s.game.call("actors")["actors"] if a["id"] == s.actor)
    s.check(close(actor["overrides"].get("intensity", 0), 2.5) and close(actor["overrides"].get("tint", [0, 0, 0]), [1, 0.5, 0.25])
            and close(actor["rotation"], [90, 0, 0]), f"parameters become overrides, rotation its own ({actor['overrides']})")
    s.check(s.history()["undo"] == "Edit 3 fields", f"one undo step for the request ({s.history()['undo']})")
    candle = next(k for k in schema(s, action="kinds")["kinds"] if k["name"] == "actor:candle")
    glow = next(x for x in candle["fields"] if x["name"] == "glow_mode")
    schema(s, action="set", id=s.actor, values={"glow_mode": glow["options"][0]})
    s.check(value(s, s.actor, "glow_mode") == glow["options"][0], "an enum parameter by its option's name")
    schema(s, action="reset", id=s.actor, fields=["intensity"])
    actor = next(a for a in s.game.call("actors")["actors"] if a["id"] == s.actor)
    s.check("intensity" not in actor["overrides"] and not schema(s, action="get", id=s.actor)["fields"]["intensity"]["overridden"],
            "reset drops the override: the prefab's value again")


def check_fog(s):
    g = s.game
    got = schema(s, action="get", id=s.volume)
    f = got["fields"]
    s.check(got["kind"] == "fog" and close(f["density"]["value"], 0.004) and f["density"]["source"] == "default",
            f"a fog volume: its kind and the lighting schema's defaults ({got['kind']})")
    schema(s, action="set", id=s.volume, values={"density": 0.01, "shape": "sphere", "size": [200, 100, 300], "yaw": 30,
                                                 "disturb_player": 0.5})
    v = next(x for x in g.call("environment")["volumes"] if x["id"] == VOLUME)
    s.check(close(v["density"], 0.01) and v["shape"] == "sphere" and close(v["size"], [200, 100, 300])
            and close(v["yaw"], 30) and close(v["disturb_player"], 0.5), "fog fields reach the volume (environment RPC)")
    schema(s, action="reset", id=s.volume, fields=["density"])
    s.check(close(value(s, s.volume, "density"), 0.004), "reset without a preset: the default")
    g.call("environment", volumes=[{"id": VOLUME, "preset": "ankle_mist"}])
    f = schema(s, action="get", id=s.volume)["fields"]
    s.check(close(f["density"]["value"], 0.012) and f["density"]["source"] == "preset" and not f["density"]["overridden"],
            f"with a preset: its value, from the preset ({f['density']})")
    schema(s, action="set", id=s.volume, values={"density": 0.02})
    s.check(schema(s, action="get", id=s.volume)["fields"]["density"]["overridden"], "a changed preset field is overridden")
    schema(s, action="reset", id=s.volume, fields=["density"])
    s.check(close(value(s, s.volume, "density"), 0.012), "reset with a preset: the preset's value")
    # The disturb weights: "Use global" (the volume follows the global weight) or its own.
    f = schema(s, action="get", id=s.volume)["fields"]
    glob = g.call("environment")["disturb"]["player"]
    s.check(f["disturb_player"]["global"] is True and close(f["disturb_player"]["value"], glob),
            f"a weight left global: the box ticked, the global weight shown ({f['disturb_player']})")
    schema(s, action="set", id=s.volume, values={"disturb_player": 0.25})
    v = next(x for x in g.call("environment")["volumes"] if x["id"] == VOLUME)
    s.check(close(v["disturb_player"], 0.25) and schema(s, action="get", id=s.volume)["fields"]["disturb_player"]["global"]
            is False, "a value of its own unticks it")
    schema(s, action="set", id=s.volume, values={"disturb_player": "global"})
    v = next(x for x in g.call("environment")["volumes"] if x["id"] == VOLUME)
    s.check(close(v["disturb_player"], -1) and s.history()["undo"] == "Use global Ashley",
            f"\"global\" ticks it again (-1 in the data; {s.history()['undo']!r})")
    schema(s, action="set", id=s.volume, values={"disturb_player": 0.25})
    schema(s, action="set", id=s.volume, values={"disturb_player": -1})
    s.check(schema(s, action="get", id=s.volume)["fields"]["disturb_player"]["global"] is True,
            "a negative weight (the old -1) also means Use global, not 0")
    kinds = {k["name"]: k for k in schema(s, action="kinds")["kinds"]}
    weight = next(x for x in kinds["fog"]["fields"] if x["name"] == "disturb_player")
    s.check(weight["min"] == 0 and weight["max"] == 2, f"the weights' range is 0..2 ({weight['min']}..{weight['max']})")
    # Every group's reset goes to the preset: one field per group, changed then reset.
    g.call("environment", volumes=[{"id": VOLUME, "preset": "ankle_mist"}])
    for field, changed_to in (("density", 0.03), ("brightness", 2.5), ("sim_trail", 2.5), ("shadow", 1.5)):
        base = value(s, s.volume, field)
        schema(s, action="set", id=s.volume, values={field: changed_to})
        schema(s, action="reset", id=s.volume, fields=[field])
        s.check(close(value(s, s.volume, field), base), f"reset on {field} restores the preset's {base}")
    g.call("environment", volumes=[{"id": VOLUME, "preset": ""}])
    g.call("environment", volumes=[{"id": VOLUME, "preset": "", "size": [160, 128, 160], "shape": "box", "yaw": 0}])


def check_details(s):
    """Details draws the selection's fields by category, with a reset arrow beside each changed value."""
    g = s.game
    s.select(action="set", ids=[s.light])
    g.call("editor_ui", details="details")
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.light and ui["kind"] == "point" and {"intensity", "radius", "color"} <= set(ui["fields"])
            and "position.x" in ui["rects"], f"Details shows the light's fields ({ui['fields']})")
    s.check(all(r[2] > 0 and r[3] > 0 for r in ui["rects"].values()), "each value has its place in the panel")
    s.select(action="set", ids=[s.volume])
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.volume and "density" in ui["fields"] and "opacity" not in ui["fields"],
            f"a fog volume: Basics open, Look closed at first ({ui['fields']})")
    cats = [(c["name"], c["open"]) for c in ui["categories"]]
    s.check(cats == [("Basics", True), ("Look", False), ("Motion & interaction", False), ("Advanced", False),
                     ("Transform", True)] and ui["header"],
            f"a fog volume's groups in order, Basics and Transform open, its preset row first ({cats}, header {ui['header']})")
    schema(s, action="ui", search="ashley")
    g.run(3)
    ui = schema(s, action="ui")
    box = ui["rects"].get("disturb_player.global")
    if s.check(box is not None, f"the weight's Use global box ({sorted(ui['rects'])})"):
        done = s.history()["done"]
        s.click(centre(box))
        f = schema(s, action="get", id=s.volume)["fields"]["disturb_player"]
        s.check(f["global"] is False and s.history()["done"] == done + 1 and s.history()["undo"] == "Edit Ashley",
                f"unticking it: its own weight (from the global one), one undo step ({f}, {s.history()['undo']!r})")
        s.click(centre(box))
        s.check(schema(s, action="get", id=s.volume)["fields"]["disturb_player"]["global"] is True, "ticking it: global again")
    schema(s, action="ui", search="")
    s.select(action="set", ids=[s.actor])
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.actor and "intensity" in ui["fields"], "a light actor: its prefab's parameters")
    s.select(action="set", ids=[s.light])
    schema(s, action="set", id=s.light, values={"radius": 900})
    g.run(4)
    ui = schema(s, action="ui")
    rect = ui["reset"].get("radius")
    if s.check(rect is not None and "intensity" not in ui["reset"], f"a changed value (only) has a reset arrow ({ui['reset']})"):
        s.click(centre(rect))
        s.check(close(value(s, s.light, "radius"), 600) and s.history()["undo"] == "Reset Radius",
                f"clicking it restores the default, one undo step ({s.history()['undo']})")


def check_search_lock(s):
    g = s.game
    s.select(action="set", ids=[s.light])
    g.call("editor_ui", details="details")
    g.run(4)
    schema(s, action="ui", search="rad")
    g.run(3)
    ui = schema(s, action="ui")
    s.check(ui["fields"] == ["radius"] and ui["search"] == "rad", f"search: only the matching field shows ({ui['fields']})")
    schema(s, action="ui", search="")
    g.run(3)
    s.check(len(schema(s, action="ui")["fields"]) > 4, "an empty search shows every field again")
    s.select(action="lock", on=True)
    s.select(action="set", ids=[s.actor])
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == s.light and ui["locked"] == s.light, "locked: Details keeps the light while the candle is selected")
    g.call("editor_ui", details="graphics")
    g.run(2)
    s.select(action="set", ids=[s.volume])
    g.run(3)
    s.check(g.call("editor_ui")["details"] == "graphics", "locked: a new selection does not switch tabs")
    schema(s, action="set", values={"intensity": 3})  # no id: the object Details shows
    s.check(close(value(s, s.light, "intensity"), 3), "an edit without an id goes to Details' (locked) object")
    s.select(action="lock", on=False)
    g.call("editor_ui", details="details")
    g.run(4)
    s.check(schema(s, action="ui")["subject"] == s.volume, "unlocked: Details follows the selection again")
    schema(s, action="reset", id=s.light, fields=["intensity"])


def check_ladder(s):
    """Middle-drag on a number: up/down picks steps of 10, 1, 0.1, 0.01, left/right adds them; Esc cancels."""
    g = s.game
    s.select(action="set", ids=[s.light])
    schema(s, action="reset", id=s.light, fields=["intensity", "radius"])
    g.call("editor_ui", details="details")
    g.run(4)
    c = centre(schema(s, action="ui")["rects"]["intensity"])
    done = s.history()["done"]
    s.mouse(c, button=2, down=True)
    lad = schema(s, action="ui")["ladder"]
    s.check(lad["active"] and lad["field"] == "intensity" and close(lad["step"], 0.01),
            f"a middle press on Intensity opens the ladder on its own step ({lad})")
    rh, notch = lad["row_height"], lad["notch"]
    s.mouse([c[0], c[1] - rh], button=2, down=True)  # one row up: steps of 0.1
    s.mouse([c[0] + notch * 5.5, c[1] - rh], button=2, down=True)  # five notches right
    lad = schema(s, action="ui")["ladder"]
    s.check(close(lad["step"], 0.1) and close(lad["value"], 1.5), f"one row up, five notches right: +0.5 ({lad})")
    s.mouse([c[0] + notch * 5.5, c[1] - rh], button=2, down=False)
    h = s.history()
    s.check(close(value(s, s.light, "intensity"), 1.5) and not schema(s, action="ui")["ladder"]["active"]
            and h["done"] == done + 1 and h["undo"] == "Edit Intensity",
            f"released: the value stays, one undo step ({h['undo']})")
    c = centre(schema(s, action="ui")["rects"]["radius"])
    s.mouse(c, button=2, down=True)
    lad = schema(s, action="ui")["ladder"]
    s.check(close(lad["step"], 1), f"Radius opens on steps of 1 ({lad})")
    s.mouse([c[0], c[1] - lad["row_height"]], button=2, down=True)  # up: steps of 10
    s.mouse([c[0] - notch * 3.5, c[1] - lad["row_height"]], button=2, down=True)  # three notches left
    s.mouse([c[0] - notch * 3.5, c[1] - lad["row_height"]], button=2, down=False)
    s.check(close(value(s, s.light, "radius"), 570), f"Radius: three notches left in steps of 10 ({value(s, s.light, 'radius')})")
    c = centre(schema(s, action="ui")["rects"]["intensity"])
    done = s.history()["done"]
    s.mouse(c, button=2, down=True)
    s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=True)  # steps of 0.01: +0.03
    mid = value(s, s.light, "intensity")
    state = s.keys(["escape"])
    s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=False)
    s.check(close(mid, 1.53) and close(value(s, s.light, "intensity"), 1.5) and state["visible"]
            and s.history()["done"] == done, f"Escape cancels: the value back, the editor open, no undo step ({mid})")
    # The editor closing mid-drag (Escape, F1, RPC) ends the ladder: its value stays, its undo step closes.
    s.select(action="set", ids=[s.light])
    g.call("editor_ui", details="details")
    g.run(4)
    rect = schema(s, action="ui")["rects"]["intensity"]
    start = centre(rect)
    before = value(s, s.light, "intensity")
    done = s.history()["done"]
    s.mouse(start, button=2, down=True)
    s.mouse([start[0] + notch * 5.5, start[1]], button=2, down=True)
    mid = value(s, s.light, "intensity")
    g.call("editor_ui", visible=False)
    g.run(3)
    undo = g.call("editor_undo", action="list")
    s.mouse([start[0] + notch * 5.5, start[1]], button=2, down=False)
    g.call("editor_ui", visible=True)
    g.run(4)
    s.check(not close(mid, before) and close(value(s, s.light, "intensity"), mid) and undo["hold"] == 0
            and not undo["open"] and undo["done"] == done + 1 and undo["undo"] == "Edit Intensity"
            and not schema(s, action="ui")["ladder"]["active"],
            f"closing the editor mid-ladder keeps {mid} and closes its undo step (hold {undo['hold']}, "
            f"open {undo['open']}, {undo.get('undo')!r})")
    g.call("editor_undo", action="undo")


def check_multi(s):
    """Several selected objects of one kind: a shared field goes to every one; the position stays the
    edited one's; a vec3 component dragged in Details goes to the others without their other components."""
    g = s.game
    s.select(action="set", ids=[s.light])
    other = s.select(action="duplicate")["primary"]
    p_other = value(s, other, "position")
    s.select(action="set", ids=[other, s.light])  # the light is the primary: Details' object
    schema(s, action="set", values={"intensity": 3})
    s.check(close(value(s, s.light, "intensity"), 3) and close(value(s, other, "intensity"), 3),
            "a shared field goes to every selected light")
    p = value(s, s.light, "position")
    schema(s, action="set", values={"position": [p[0], p[1], p[2] + 40]})
    s.check(close(value(s, s.light, "position")[2], p[2] + 40) and close(value(s, other, "position"), p_other),
            "the position only to the edited one")
    s.select(action="set", ids=[s.volume])
    dup = s.select(action="duplicate")["primary"]
    schema(s, action="set", id=s.volume, values={"size": [160, 128, 160]})
    schema(s, action="set", id=dup, values={"size": [300, 50, 200]})
    s.select(action="set", ids=[dup, s.volume])
    g.call("editor_ui", details="details")
    schema(s, action="ui", search="size")  # Transform is the last group: the search brings Size on screen
    g.run(4)
    rects = schema(s, action="ui")["rects"]
    if s.check("size.x" in rects, f"Details shows the volume's size ({sorted(rects)[:8]})"):
        c = centre(rects["size.x"])
        s.mouse(c, button=2, down=True)
        notch = schema(s, action="ui")["ladder"]["notch"]
        s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=True)  # steps of 1: +3
        s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=False)
        a, b = value(s, s.volume, "size"), value(s, dup, "size")
        s.check(close(a, [163, 128, 160]) and close(b, [163, 50, 200]),
                f"the ladder on size x: x to both, y and z each their own ({a}, {b})")
    # Escape on the ladder over a weight: another selected volume left on Use global stays on it.
    s.select(action="clear")  # an edit goes to every selected volume: one at a time here
    schema(s, action="set", id=dup, values={"disturb_player": 0.5})
    schema(s, action="set", id=s.volume, values={"disturb_player": "global"})
    s.select(action="set", ids=[s.volume, dup])  # dup is the primary (its own weight: the slider is live)
    schema(s, action="ui", search="ashley")
    g.run(4)
    rects = schema(s, action="ui")["rects"]
    if s.check("disturb_player" in rects, f"Details shows the weight ({sorted(rects)[:8]})"):
        c = centre(rects["disturb_player"])
        done = s.history()["done"]
        s.mouse(c, button=2, down=True)
        notch = schema(s, action="ui")["ladder"]["notch"]
        s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=True)
        s.keys(["escape"])
        s.mouse([c[0] + notch * 3.5, c[1]], button=2, down=False)
        fa = schema(s, action="get", id=s.volume)["fields"]["disturb_player"]
        fb = schema(s, action="get", id=dup)["fields"]["disturb_player"]
        s.check(fa["global"] is True and fb["global"] is False and close(fb["value"], 0.5)
                and s.history()["done"] == done,
                f"Escape: the other volume still on Use global, the primary back to 0.5, no undo step ({fa}, {fb})")
    schema(s, action="ui", search="")
    s.select(action="set", ids=[other, dup])
    s.select(action="delete")
    schema(s, action="reset", id=s.light, fields=["intensity"])
    schema(s, action="set", id=s.light, values={"position": p})


MENU_LIGHT = "menu_model_light"


def check_standalone(s):
    """The menu model's studio light: a standalone kind, by {kind}; drawn in the Graphics tab by draw_kind."""
    g = s.game

    def get(field):
        return schema(s, action="get", kind=MENU_LIGHT)["fields"][field]["value"]

    got = schema(s, action="get", kind=MENU_LIGHT)
    f = got["fields"]
    kind = next(k for k in schema(s, action="kinds", kind=MENU_LIGHT)["kinds"])
    s.check(got["kind"] == MENU_LIGHT and kind["object"] == "none" and kind["count"] == 1
            and close(f["key_intensity"]["value"], 0.5) and not any(x["overridden"] for x in f.values()),
            f"the rig as shipped, every field at its default ({kind['object']}, {sorted(f)[:6]}...)")
    done = s.history()["done"]
    schema(s, action="set", kind=MENU_LIGHT, values={"key_intensity": 1.5, "key_azimuth": 20})
    rig = g.call("graphics", menu_light=True)["menu_light"]
    s.check(close(rig["key_intensity"], 1.5) and close(rig["key_azimuth"], 20) and rig["edited"],
            f"edits reach the rig (graphics menu_light: {rig['key_intensity']}, {rig['key_azimuth']})")
    h = s.history()
    s.check(h["done"] == done + 1 and h["undo"] == "Edit 2 fields", f"one undo step for the request ({h['undo']})")
    g.call("editor_undo", action="undo")
    s.check(close(get("key_intensity"), 0.5) and close(get("key_azimuth"), f["key_azimuth"]["value"]),
            "undo puts the rig back")
    g.call("editor_undo", action="redo")
    s.check(close(get("key_intensity"), 1.5), "redo applies it again")
    schema(s, action="set", kind=MENU_LIGHT, values={"key_intensity": 99, "rim_power": 0})
    s.check(close(get("key_intensity"), 4) and close(get("rim_power"), 0.25), "values past the range are clamped")
    try:
        schema(s, action="get", kind="point")
        s.check(False, "a scene kind by {kind} is refused")
    except RpcError:
        s.check(True, "a scene kind by {kind} is refused")
    schema(s, action="reset", kind=MENU_LIGHT)
    rig = g.call("graphics", menu_light=True)["menu_light"]
    s.check(rig["key_direction"] == [-0.45, -0.5, -0.74] and rig["key_intensity"] == 0.5 and not rig["edited"],
            f"reset: the shipped rig exactly ({rig['key_direction']}, edited {rig['edited']})")
    # Details' rows for it in the Graphics tab
    g.call("editor_ui", details="graphics")
    schema(s, action="ui", open=MENU_LIGHT)
    g.run(4)
    ui = schema(s, action="ui")["standalone"].get(MENU_LIGHT)
    if not s.check(ui and {"key_azimuth", "key_elevation", "key_color", "key_intensity", "rim_power"} <= set(ui["fields"])
                   and "key_intensity" in ui["rects"], f"the Graphics tab draws it ({ui and ui['fields']})"):
        return
    schema(s, action="ui", kind_search={MENU_LIGHT: "rim"})
    g.run(3)
    fields = schema(s, action="ui")["standalone"][MENU_LIGHT]["fields"]
    s.check(fields and all(x.startswith("rim_") for x in fields), f"its own search: rim's fields only ({fields})")
    schema(s, action="ui", kind_search={MENU_LIGHT: ""})
    schema(s, action="set", kind=MENU_LIGHT, values={"fill_intensity": 1})
    g.run(4)
    ui = schema(s, action="ui")["standalone"][MENU_LIGHT]
    rect = ui["reset"].get("fill_intensity")
    if s.check(rect is not None and "key_intensity" not in ui["reset"], f"a changed value (only) has a reset arrow ({ui['reset']})"):
        s.click(centre(rect))
        s.check(close(get("fill_intensity"), 0.15) and s.history()["undo"] == "Reset Intensity",
                f"clicking it restores the shipped value, one undo step ({s.history()['undo']})")
    c = centre(schema(s, action="ui")["standalone"][MENU_LIGHT]["rects"]["key_intensity"])
    done = s.history()["done"]
    s.mouse(c, button=2, down=True)
    lad = schema(s, action="ui")["ladder"]
    s.check(lad["active"] and lad["field"] == "key_intensity", f"a middle press opens the value ladder ({lad})")
    s.mouse([c[0] + lad["notch"] * 5.5, c[1]], button=2, down=True)  # five notches right in steps of 0.01
    s.mouse([c[0] + lad["notch"] * 5.5, c[1]], button=2, down=False)
    h = s.history()
    s.check(close(get("key_intensity"), 0.55) and h["done"] == done + 1 and h["undo"] == "Edit Intensity",
            f"the ladder: +0.05, one undo step ({get('key_intensity')}, {h['undo']})")
    schema(s, action="reset", kind=MENU_LIGHT)


def check_sun(s):
    g = s.game
    g.call("environment", sun={"enabled": True, "azimuth": 180, "elevation": 30, "intensity": 1, "color": [1, 1, 1]})
    g.run(2)
    got = schema(s, action="get", id="env:sun")
    f = got["fields"]
    s.check(got["kind"] == "sun" and {"enabled", "azimuth", "elevation", "color", "intensity", "angle", "shadows"} <= set(f)
            and close(f["elevation"]["value"], 30), f"the sun's fields ({sorted(f)})")
    done = s.history()["done"]
    schema(s, action="set", id="env:sun", values={"elevation": 60, "intensity": 99})
    sun = g.call("environment")["sun"]
    s.check(close(sun["elevation"], 60) and sun["intensity"] <= 8 and s.history()["done"] == done + 1,
            f"set reaches the sun, clamped, one undo step ({sun}, {s.history()['undo']!r})")
    g.call("editor_undo", action="undo")
    s.check(close(g.call("environment")["sun"]["elevation"], 30), "undo puts it back")
    schema(s, action="set", id="env:sun", values={"angle": 2})
    schema(s, action="reset", id="env:sun", fields=["angle"])
    s.check(close(value(s, "env:sun", "angle"), 0.53) and s.history()["undo"] == "Reset Angular size",
            f"reset: the default ({s.history()['undo']!r})")
    s.select(action="set", ids=["env:sun"])
    g.call("editor_ui", details="details")
    g.run(4)
    ui = schema(s, action="ui")
    s.check(ui["subject"] == "env:sun" and ui["kind"] == "sun" and "azimuth" in ui["fields"],
            f"Details draws the sun's fields ({ui.get('fields')})")
    g.call("environment", remove="sun")
    s.select(action="clear")


def check_reload(s):
    """A Kind* held across a prefab library reload stays valid until the next frame (editor_schema.cpp collect)."""
    g = s.game
    r = schema(s, action="reload_probe", prefab="candle")
    s.check(r["kept"] and r["name"] == "actor:candle" and r["fields_before"] == r["fields_after"] and r["retired"] >= 1,
            f"the old kind still reads after the reload, retired ({r})")
    g.run(2)
    r2 = schema(s, action="reload_probe", prefab="candle", reload=False)
    s.check(r2["retired"] == 0 and r2["kept"], f"a frame later the retired kinds are gone ({r2})")


SECTIONS = [check_lint, check_lights, check_actors, check_fog, check_details, check_search_lock, check_ladder, check_multi,
            check_sun, check_reload, check_standalone]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (lint, lights, ...)")
    args = parser.parse_args()
    s = start(args, "editor_schema")
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
        status = finish(s, "every kind's fields lint clean, round-trip, reset; Details draws them; the value ladder")
    return status


if __name__ == "__main__":
    sys.exit(main())
