#!/usr/bin/env python3
"""The editor's library (pc/src/editor_library.h): one panel for basic lights,
light prefabs, fixtures, fog volume presets and (when a source registers
them) emitters.

    python3 pc/tests/editor_library_smoke.py [--card pc/build/test-card1.mcd] [--out DIR] [--only NAME,...]

The save room with the editor open (editor_selection_smoke.py's start: a
point light, a candle and a fog volume around Ashley), scratch prefab and
thumbnail directories (ROOD_PREFABS, ROOD_LIBRARY_THUMBS). Sections:

- sources: every kind's entries (point, spot and rect lights; the built-in
  prefabs; the fixture catalog's light classes; a plain fog volume and the
  fog presets), each with a name and a category; no emitters without their
  source;
- filters: a kind chip, a category, search words, favourites (a user
  setting) and Mine;
- drops: through the drag and drop (editor_place, as a drag from a tile):
  a fog preset on the floor by Ashley rests its bottom on that floor with
  its footprint inside the room, one undo step "Place <name>", selected;
  a plain fog volume dropped on a wall goes to the floor below; a spot
  dropped on the wall aims out of it, a torch (align normal) faces out, a
  lantern (upright) turns to face out with no pitch or roll; a fixture
  class dropped where there is no fixture places its prefab with the
  class's parameters; the fog preset's footprint fits the level floor area
  at the drop; a wall torch class on the wall faces out; a candle class
  dropped on the room's niche candle lights that fixture;
- thumbs: each kind's thumbnail is rendered once into the cache directory
  (96 x 96 RGBA behind a 16-byte header), the same bytes when rendered
  again, then read from the cache; a fog preset's differs from a light's;
  changing the neutral scene's look gives another file; the look saves to
  its file and reads back; a fog preset's look is in its hash;
- panel: the side column's Library tab draws a tile per shown entry with its
  thumbnail; a kind chip filters; List shows rows; a real mouse drag from a
  fog preset's tile onto the floor places it (one undo step, selected); Pop
  out floats the panel in its own window, Dock puts it back; both are user
  settings;
- save: Save as prefab from a light makes a light prefab of yours (its file
  in the user prefab directory; the light becomes an actor of it), listed
  under Mine; dropped again it lights the same; from a light actor, a
  prefab with its values; from a fog volume, a fog preset of yours
  (<user prefabs>/fog/<id>.json) that the volume then uses, dropped again
  with the same look; a mixed selection is refused;
- save_groups: two light actors, or a light and an actor, save as one
  prefab of all their lights (where they were, as bright), replaced by one
  actor of it (one undo step brings them back); several fog volumes are
  refused saying why; the Outliner's right-click menu has Save as
  prefab..., which opens the dialog;
- emitters: JSON files in the emitters directory (ROOD_EMITTERS) and the
  user's are entries of kind emitter under their own chip, named from the
  file; gone with the files;
- manage: an entry's file (yours only), Rename (its name, its id stays),
  Delete refused for a built-in; a real right-click on a tile of yours
  opens its menu: Edit prefab opens the prefab editor on it; Delete... asks
  first (Cancel keeps it), then moves its file aside; the footer's Edit
  prefab for the current item; RPC delete;
"""
import argparse
import math
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, RpcError  # noqa: E402
from editor_selection_smoke import centre, distance, finish, pinned_card, start  # noqa: E402
from editor_schema_smoke import close, schema, value  # noqa: E402


def lib(s, **params):
    return s.game.call("editor_library", **params)


def items(r):
    return [e["item"] for e in r["entries"]]


def check_sources(s):
    r = lib(s, action="filter", kind="", category="", search="", favourites=False, mine=False)
    got = set(items(r))
    want = {"light:point", "light:spot", "light:rect", "prefab:candle", "prefab:torch", "prefab:lantern",
            "fixture:wall_torch", "fixture:candle", "fog:", "fog:ankle_mist"}
    s.check(want <= got, f"every kind's entries ({sorted(want - got)} missing)")
    s.check(r["kinds"][:4] == ["light", "prefab", "fixture", "fog"] and "emitter" not in r["kinds"],
            f"the kinds in order, no emitters without their source ({r['kinds']})")
    s.check(all(e["name"] and e["category"] and e["kind"] for e in r["entries"]), "each has a name, a category, a kind")
    s.check(not any(e["item"] == "fixture:cobweb" for e in r["entries"]), "fixture classes that are no light are left out")


def check_filters(s):
    g = s.game
    r = lib(s, action="filter", kind="fog")
    s.check(r["entries"] and all(e["kind"] == "fog" for e in r["entries"]) and r["filter"]["kind"] == "fog",
            f"a kind chip: fog volumes only ({len(r['entries'])})")
    r = lib(s, action="filter", kind="", search="ankle mist")
    s.check(items(r) == ["fog:ankle_mist"], f"search: every word ({items(r)})")
    r = lib(s, action="filter", search="", kind="prefab", category="Fire")
    s.check(r["entries"] and all(e["category"] == "Fire" and e["kind"] == "prefab" for e in r["entries"]),
            f"a kind and a category ({items(r)})")
    lib(s, action="filter", kind="", category="")
    lib(s, action="favourite", item="prefab:candle", on=True)
    r = lib(s, action="filter", favourites=True)
    s.check(items(r) == ["prefab:candle"] and r["favourites"] == ["prefab:candle"], f"favourites only ({items(r)})")
    g.call("settings", action="save")
    s.check("editor_library_favourites prefab:candle" in pathlib.Path(s.settings).read_text(),
            "the favourites are a user setting")
    lib(s, action="favourite", item="prefab:candle", on=False)
    r = lib(s, action="filter", favourites=False, mine=True)
    s.check(r["entries"] == [], f"Mine: nothing of the user's yet ({items(r)})")
    lib(s, action="filter", mine=False)


def floor_and_wall(s):
    """A floor pixel by Ashley and a wall pixel (its normal across), both in the view; either may be None."""
    p = s.player
    floor = None
    # Around Ashley: the first pixel whose surface is level floor (the save room has steps and a save point).
    for dx, dz in ((60, 140), (0, 160), (-60, 140), (120, 60), (-120, 60), (0, -140), (160, 160), (-160, 160)):
        px = s.select(action="project", point=[p["x"] + dx, p["y"], p["z"] + dz])["pixel"]
        hit = s.select(action="pick", x=px[0], y=px[1]) if px else None
        if hit and hit["normal"] and hit["normal"][1] < -0.7:
            floor = (px, hit)
            break
    view = s.select()["view"]
    for fy in (0.15, 0.25, 0.35, 0.45):
        for fx in (0.08, 0.2, 0.32, 0.68, 0.8, 0.92):
            xy = [view[0] + view[2] * fx, view[1] + view[3] * fy]
            hit = s.select(action="pick", x=xy[0], y=xy[1])
            if hit["normal"] and abs(hit["normal"][1]) < 0.3:
                return floor, (xy, hit)
    return floor, None


def drop(s, item, xy):
    """A drag of `item` from the library dropped at window pixel xy: (editor_place's reply, undo steps it made)."""
    g = s.game
    done = s.history()["done"]
    g.call("editor_place", item=item, phase="begin", x=xy[0], y=xy[1])
    r = g.call("editor_place", phase="drop")
    return r, s.history()["done"] - done


def new_volume(s):
    sel = s.select()["selection"]
    ident = sel[0][4:] if sel and sel[0].startswith("fog:") else None
    return next((v for v in s.game.call("environment")["volumes"] if v["id"] == ident), None)


def forward(rotation):
    """An actor's forward axis from yaw, pitch, roll (light_prefabs.cpp rotation_basis)."""
    yaw, pitch = math.radians(rotation[0]), math.radians(rotation[1])
    return [math.sin(yaw) * math.cos(pitch), -math.sin(pitch), math.cos(yaw) * math.cos(pitch)]


def flat(v):
    n = math.hypot(v[0], v[2]) or 1
    return [v[0] / n, 0, v[2] / n]


def check_drops(s):
    g = s.game
    floor, wall = floor_and_wall(s)
    if not s.check(floor and floor[1]["normal"] and floor[1]["normal"][1] < -0.7, f"a floor pixel by Ashley ({floor})"):
        return
    fxy, fhit = floor
    r, steps = drop(s, "fog:ankle_mist", fxy)
    v = new_volume(s)
    lo, hi = g.call("scene")["bounds"]
    s.check(v is not None and steps == 1 and s.history()["undo"] == "Place Ankle mist" and v["preset"] == "ankle_mist"
            and abs(v["pos"][1] + v["size"][1] / 2 - fhit["world"][1]) < 2
            and v["size"][0] <= 600.5 and v["size"][2] <= 600.5,
            f"a fog preset on the floor: its bottom on it, at most the preset's size, one undo step, selected ({v}, "
            f"{s.history()['undo']!r})")
    if v:
        s.check(v["pos"][0] - v["size"][0] / 2 >= lo[0] - 1 and v["pos"][0] + v["size"][0] / 2 <= hi[0] + 1
                and v["pos"][2] - v["size"][2] / 2 >= lo[2] - 1 and v["pos"][2] + v["size"][2] / 2 <= hi[2] + 1,
                f"its footprint stays inside the room ({v['pos']}, {v['size']}; room {lo}..{hi})")
        area = lib(s, action="floor", x=fhit["world"][0], z=fhit["world"][2])["area"]
        if area and abs(area["height"] - fhit["world"][1]) <= 12:
            a_lo, a_hi = area["lo"], area["hi"]
            s.check(v["pos"][0] - v["size"][0] / 2 >= a_lo[0] - 1 and v["pos"][0] + v["size"][0] / 2 <= a_hi[0] + 1
                    and v["pos"][2] - v["size"][2] / 2 >= a_lo[1] - 1 and v["pos"][2] + v["size"][2] / 2 <= a_hi[1] + 1
                    and v["size"][0] >= min(600, a_hi[0] - a_lo[0]) - 1 and v["size"][2] >= min(600, a_hi[1] - a_lo[1]) - 1,
                    f"its footprint fits the level floor area there ({v['pos']}, {v['size']}; area {a_lo}..{a_hi}), "
                    "as large as the preset or the area allows")
        else:
            s.check(area is not None, f"the level floor area at the drop ({area})")
    g.call("editor_undo", action="undo")
    s.check(new_volume(s) is None, "undo takes it away")
    if not s.check(wall is not None, "a wall pixel in the view"):
        return
    wxy, whit = wall
    n = whit["normal"]
    r, steps = drop(s, "fog:", wxy)
    v = new_volume(s)
    s.check(v is not None and v["pos"][1] + v["size"][1] / 2 >= whit["world"][1] - 1
            and abs(v["pos"][1] + v["size"][1] / 2 - s.player["y"]) < 24,
            f"a fog volume dropped on a wall goes to the floor below ({v and v['pos']}, wall hit {whit['world']})")
    g.call("editor_undo", action="undo")
    r, steps = drop(s, "light:spot", wxy)
    spot = g.call("lights")["lights"][-1]["id"]  # the lights RPC has no direction: the schema's (editor_schema get)
    aim = value(s, spot, "direction")
    s.check(steps == 1 and close(aim, n, 0.02), f"a spot on the wall aims out ({aim} vs {n})")
    g.call("editor_undo", action="undo")
    for prefab, upright in (("torch", False), ("lantern", True)):
        r, steps = drop(s, "prefab:" + prefab, wxy)
        a = g.call("actors")["actors"][-1]
        f = forward(a["rotation"])
        want = flat(n) if upright else n
        s.check(a["prefab"] == prefab and steps == 1 and close(f, want, 0.03)
                and (not upright or (abs(a["rotation"][1]) < 0.5 and abs(a["rotation"][2]) < 0.5)),
                f"a {prefab} on the wall faces out ({[round(x, 3) for x in f]} vs {want}; rotation {a['rotation']})")
        g.call("editor_undo", action="undo")
    r, steps = drop(s, "fixture:wall_torch", fxy)
    a = g.call("actors")["actors"][-1]
    s.check(a["prefab"] == "torch" and close(a["overrides"].get("radius", 0), 520) and steps == 1
            and s.history()["undo"] == "Place Wall torch",
            f"a fixture class with no fixture there: its prefab with the class's parameters ({a['overrides']})")
    g.call("editor_undo", action="undo")
    count = len(g.call("actors")["actors"])
    r, steps = drop(s, "fixture:wall_torch", wxy)
    actors = g.call("actors")["actors"]
    if s.check(len(actors) == count + 1 and steps == 1, f"a wall torch class dropped on the wall: one new actor ({r})"):
        a = actors[-1]
        f = forward(a["rotation"])
        s.check(a["prefab"] == "torch" and close(f, n, 0.05),
                f"a wall fixture class faces out of the wall ({[round(x, 3) for x in f]} vs {n}; {a['prefab']})")
        g.call("editor_undo", action="undo")
    # Dropped on one of the room's own fixtures (the save room's niche candle: room polygons, no glow) it
    # lights that fixture, as the Light fixture tool.
    niche = [190, -148, 1031]
    px = hit = None
    if (s.zone, s.room) == (9, 17):
        g.call("editor_camera", x=190, y=-150, z=830, yaw=0, pitch=0)  # facing the niche from the room
        g.run(6)
        px = s.select(action="project", point=niche)["pixel"]
        hit = s.select(action="pick", x=px[0], y=px[1]) if px else None
    if hit and hit["world"] and math.dist(hit["world"], niche) < 40:
        count = len(g.call("actors")["actors"])
        r, steps = drop(s, "fixture:candle", px)
        actors = g.call("actors")["actors"]
        a = actors[-1] if len(actors) == count + 1 else None
        s.check(a is not None and a["fixture"] and math.dist(a["lights"][0]["position"], niche) < 40 and steps == 1,
                f"a candle class dropped on the niche's candle lights that fixture ({a and a['fixture']}, {r})")
        g.call("editor_undo", action="undo")
    else:
        s.check((s.zone, s.room) != (9, 17), f"the niche candle in view ({px}, {hit and hit['world']})")
    g.call("editor_camera", off=1)
    g.run(4)


HEADER = 16


def check_thumbs(s):
    wanted = ["light:point", "light:spot", "light:rect", "prefab:candle", "fog:ankle_mist", "fixture:wall_torch"]
    r = lib(s, action="thumbs", items=wanted)
    got = {t["item"]: t for t in r["thumbs"]}
    files = {i: pathlib.Path(got[i]["file"]) for i in wanted if i in got and got[i]["file"]}
    s.check(set(files) == set(wanted) and all(f.exists() and f.stat().st_size == HEADER + 96 * 96 * 4 for f in files.values())
            and all(not got[i]["cached"] for i in wanted) and all(str(f).startswith(str(s.work / "thumbs")) for f in files.values()),
            f"a thumbnail per kind, rendered into the cache directory ({sorted(files)})")
    if len(files) != len(wanted):
        return
    first = {i: f.read_bytes() for i, f in files.items()}
    again = {t["item"]: t for t in lib(s, action="thumbs", items=wanted)["thumbs"]}
    s.check(all(again[i]["cached"] and again[i]["hash"] == got[i]["hash"] for i in wanted), "a second time: from the cache")
    files["fog:ankle_mist"].unlink()
    lib(s, action="thumbs", items=["fog:ankle_mist"])
    s.check(files["fog:ankle_mist"].read_bytes() == first["fog:ankle_mist"], "rendered again: the same bytes")
    s.check(first["fog:ankle_mist"] != first["light:point"] and first["light:spot"] != first["light:point"],
            "a fog preset's thumbnail differs from a light's, a spot's from a point's")
    r = lib(s, action="thumbs", items=["light:point"], look={"floor": 0.3})
    s.check(r["thumbs"][0]["hash"] != got["light:point"]["hash"] and pathlib.Path(r["thumbs"][0]["file"]).exists(),
            "another look of the neutral scene: another file")
    lib(s, action="thumbs", items=[], look={"floor": 0.5})
    # The look is data: saved to (and read from) its file.
    r = lib(s, action="thumbs", items=[], look={"exposure": 2.25}, save_look=True)
    look_file = pathlib.Path(r["look_file"])
    s.check(look_file == s.work / "thumbs_look.json" and '"exposure": 2.25' in look_file.read_text()
            and close(r["look"]["exposure"], 2.25), f"the look saved to its file ({r['look_file']}, {r['look']})")
    look_file.write_text(look_file.read_text().replace('"exposure": 2.25', '"exposure": 1.8'))
    r = lib(s, action="thumbs", items=[], reload_look=True)
    s.check(close(r["look"]["exposure"], 1.8), f"and read back from it ({r['look']})")
    fogs = lib(s, action="thumbs", items=["fog:ankle_mist", "fog:"])["thumbs"]
    s.check(fogs[0]["hash"] != fogs[1]["hash"], "a fog preset's look (wisps, taper, phase, ...) is in its hash")


def check_panel(s):
    g = s.game
    lib(s, action="filter", kind="", category="", search="", favourites=False, mine=False)
    lib(s, action="view", mode="grid", docked=True)
    g.call("editor_ui", tab="library")
    g.run(12)  # thumbnails: at most two rendered per frame
    r = lib(s)
    tiles = {e["item"]: e for e in r["entries"] if e["tile"]}
    s.check(r["drawn"] and "light:point" in tiles and tiles["light:point"]["thumb"],
            f"the Library tab: tiles with their thumbnails ({len(tiles)} drawn)")
    chip = r["rects"].get("chip:fog")
    if s.check(chip is not None, f"a kind chip for fog volumes ({sorted(r['rects'])})"):
        s.click(centre(chip))
        r = lib(s)
        s.check(r["filter"]["kind"] == "fog" and all(e["kind"] == "fog" for e in r["entries"]), "clicked: fog volumes only")
    tile = next((e["tile"] for e in lib(s)["entries"] if e["item"] == "fog:ankle_mist" and e["tile"]), None)
    floor, _ = floor_and_wall(s)
    if s.check(tile is not None and floor is not None, "the Ankle mist tile and a floor pixel"):
        done = s.history()["done"]
        start_xy, end_xy = centre(tile), floor[0]
        s.mouse(start_xy, down=True)
        for k in range(1, 7):  # past the drag threshold, then across into the view
            s.mouse([start_xy[0] + (end_xy[0] - start_xy[0]) * k / 6, start_xy[1] + (end_xy[1] - start_xy[1]) * k / 6], down=True)
        s.mouse(end_xy, down=False)
        v = new_volume(s)
        s.check(v is not None and v["preset"] == "ankle_mist" and s.history()["done"] == done + 1,
                f"a real drag from the tile onto the floor places it, selected, one undo step ({v and v['id']})")
        g.call("editor_undo", action="undo")
    lib(s, action="view", mode="list")
    g.run(3)
    rows = [e["tile"] for e in lib(s)["entries"] if e["tile"]]
    s.check(rows and all(t[3] < 60 for t in rows), f"List: rows instead of tiles ({rows[:2]})")
    lib(s, action="view", mode="grid", docked=False)
    g.run(3)
    r = lib(s)
    s.check(not r["docked"] and r["window"] is not None and g.call("editor_ui")["tab"] != "library",
            f"Pop out: its own window ({r['window']}), not a tab")
    g.call("settings", action="save")
    text = pathlib.Path(s.settings).read_text()
    s.check("editor_library_docked 0" in text and "editor_library_view grid" in text, "docked and the view are user settings")
    lib(s, action="view", docked=True)
    lib(s, action="filter", kind="")


def check_save(s):
    g, p = s.game, s.player
    floor, _ = floor_and_wall(s)
    if not s.check(floor is not None, "a floor pixel"):
        return
    # A light: a light prefab of the user's.
    light = g.call("lights", action="add", position=[p["x"] - 100, p["y"] - 120, p["z"] + 60])["lights"][-1]["id"]
    schema(s, action="set", id=light, values={"intensity": 2.5, "radius": 480})
    s.select(action="set", ids=[light])
    done = s.history()["done"]
    r = lib(s, action="save", name="Test lamp")
    path = s.work / "prefabs" / "test_lamp.json"
    s.check(r["item"] == "prefab:test_lamp" and path.exists() and s.history()["done"] == done + 1,
            f"a light saved as prefab: {r['item']}, its file, one undo step ({r.get('message')})")
    mine = lib(s, action="filter", mine=True)
    e = next((x for x in mine["entries"] if x["item"] == "prefab:test_lamp"), None)
    s.check(e is not None and e["mine"] and e["category"] == "My prefabs", f"listed under Mine ({items(mine)})")
    lib(s, action="filter", mine=False)
    r, steps = drop(s, "prefab:test_lamp", floor[0])
    a = g.call("actors")["actors"][-1]
    s.check(a["prefab"] == "test_lamp" and close(a["lights"][0]["intensity"], 2.5, 0.05)
            and close(a["lights"][0]["radius"], 480, 0.01),
            f"dropped again it lights the same ({a['lights'][0]['intensity']}, {a['lights'][0]['radius']})")
    g.call("editor_undo", action="undo")
    # A light actor: a prefab with its values.
    schema(s, action="set", id=s.actor, values={"intensity": 3})
    before = next(x for x in g.call("actors")["actors"] if x["id"] == s.actor)["lights"][0]["intensity"]
    s.select(action="set", ids=[s.actor])
    r = lib(s, action="save", name="Bright candle")
    after = next(x for x in g.call("actors")["actors"] if x["id"] == s.actor)
    s.check(r["item"] == "prefab:bright_candle" and after["prefab"] == "bright_candle" and not after["overrides"]
            and close(after["lights"][0]["intensity"], before, 0.05),
            f"a light actor saved as prefab: it uses it, its values baked in ({after['prefab']}, {after['overrides']})")
    # A fog volume: a fog preset of the user's.
    schema(s, action="set", id=s.volume, values={"density": 0.02, "color": [0.5, 0.6, 1]})
    s.select(action="set", ids=[s.volume])
    r = lib(s, action="save", name="Test mist")
    fog_file = s.work / "prefabs" / "fog" / "test_mist.json"
    v = next(x for x in g.call("environment")["volumes"] if "fog:" + x["id"] == s.volume)
    s.check(r["item"] == "fog:test_mist" and fog_file.exists() and v["preset"] == "test_mist",
            f"a fog volume saved: a preset of yours that it uses ({r['item']}, {v['preset']})")
    line = next((x["line"] for x in g.call("lighting")["items"] if x["id"] == s.volume), "")
    s.check("preset=test_mist" in line and "density=0.02" in line and "color=" in line,
            f"the room's line keeps every value (a preset of yours is on this machine only): {line}")
    e = next((x for x in lib(s, action="filter", mine=True)["entries"] if x["item"] == "fog:test_mist"), None)
    s.check(e is not None and e["kind"] == "fog" and e["category"] == "My presets", "listed under Mine with the fog presets")
    lib(s, action="filter", mine=False)
    r, steps = drop(s, "fog:test_mist", floor[0])
    nv = new_volume(s)
    got = schema(s, action="get", id="fog:" + nv["id"])["fields"] if nv else {}
    s.check(nv is not None and nv["preset"] == "test_mist" and close(got["density"]["value"], 0.02)
            and close(got["color"]["value"], [0.5, 0.6, 1]), "dropped again: the same look, from its preset")
    g.call("editor_undo", action="undo")
    # Anything else is refused.
    s.select(action="set", ids=[s.actor, s.volume])
    try:
        lib(s, action="save", name="Mixed")
        s.check(False, "a mixed selection is refused")
    except RpcError as err:
        s.check("select" in str(err), f"a mixed selection is refused ({err})")


def check_emitters(s):
    """The emitters as data: a JSON file in the emitters directory (ROOD_EMITTERS) shows up as an entry
    under an Emitters chip, its name and category from the file; one of the user's (<user prefabs>/emitters)
    is Mine; gone again when the files are."""
    g = s.game
    d, mine = s.work / "emitters", s.work / "prefabs" / "emitters"
    d.mkdir(parents=True, exist_ok=True)
    mine.mkdir(parents=True, exist_ok=True)
    (d / "sparks.json").write_text('{"name": "Forge sparks", "category": "Fire", "tags": ["spark"], "description": "test"}\n')
    (mine / "drips.json").write_text('{"name": "Drips"}\n')
    # the directories are looked at twice a second (real time): run the game until they show
    g.until(lambda: "emitter" in lib(s, action="filter", kind="", category="", search="", favourites=False,
                                     mine=False)["kinds"], step=10)
    r = lib(s)
    e = {x["item"]: x for x in r["entries"]}
    s.check("emitter" in r["sources"] and "emitter" in r["kinds"] and e.get("emitter:sparks", {}).get("name") == "Forge sparks"
            and e["emitter:sparks"]["category"] == "Fire" and e.get("emitter:drips", {}).get("mine") is True,
            f"emitter files: entries of kind emitter, named from their files, yours Mine ({r['kinds']})")
    lib(s, action="filter", kind="emitter")
    g.call("editor_ui", tab="library")
    g.run(4)
    r = lib(s)
    s.check("chip:emitter" in r["rects"] and sorted(items(r)) == ["emitter:drips", "emitter:sparks"],
            f"an Emitters chip filters them ({items(r)})")
    s.check(lib(s, action="file", item="emitter:drips")["file"].endswith("emitters/drips.json"), "yours has its file")
    lib(s, action="filter", kind="")
    g.call("editor_ui", tab="outliner")
    (d / "sparks.json").unlink()
    (mine / "drips.json").unlink()
    g.until(lambda: "emitter" not in lib(s)["kinds"], step=10)
    r = lib(s)
    s.check("emitter" not in r["kinds"], "the files gone: no emitters")
    # A drop: the shipped dust motes as an asset of this run, placed on the floor (one undo step, selected)
    floor, _ = floor_and_wall(s)
    if not s.check(floor is not None, "a floor pixel for the emitter drop"):
        return
    (d / "dust_motes.json").write_text((ROOT / "pc/assets/emitters/dust_motes.json").read_text())
    g.call("particles", reload_assets=True)
    g.until(lambda: "emitter:dust_motes" in items(lib(s, action="filter", kind="", category="", search="",
                                                    favourites=False, mine=False)), step=10)
    r, steps = drop(s, "emitter:dust_motes", floor[0])
    sel = s.select()["selection"]
    s.check(steps == 1 and sel and sel[0].startswith("emitter:"),
            f"an emitter dropped from the library: placed, selected, one undo step ({r}, {sel}, {steps} steps)")
    g.call("editor_undo", action="undo")
    (d / "dust_motes.json").unlink()


def check_save_groups(s):
    """Save as prefab from several lights and actors (one prefab of them all, their lights baked), refused
    for several fog volumes; the Outliner's right-click menu opens its dialog."""
    g, p = s.game, s.player
    s.select(action="set", ids=[s.actor])
    other = s.select(action="duplicate")["primary"]
    actors_before = g.call("actors")["actors"]
    lights_of = {a["id"]: len(a["lights"]) for a in actors_before}
    want = lights_of[s.actor] + lights_of[other]
    s.select(action="set", ids=[s.actor, other])
    done = s.history()["done"]
    r = lib(s, action="save", name="Candle pair")
    actors = g.call("actors")["actors"]
    new = actors[-1]
    s.check(r["item"] == "prefab:candle_pair" and new["prefab"] == "candle_pair" and len(new["lights"]) == want
            and len(actors) == len(actors_before) - 1 and s.history()["done"] == done + 1
            and s.select()["selection"] == [new["id"]],
            f"two light actors: one prefab of both ({len(new['lights'])} lights, {want} wanted), one actor of it, "
            f"selected, one undo step ({r.get('message')})")
    old = {a["id"]: a for a in actors_before}[s.actor]
    base = old["lights"][0]["intensity"] / old["lights"][0]["factor"]  # as bright at rest (both flicker and jitter)
    s.check(any(math.dist(l["position"], old["lights"][0]["position"]) < 3 and close(l["intensity"] / l["factor"], base, 0.02)
                and l["light_size"] == old["lights"][0]["light_size"] for l in new["lights"]),
            f"their lights stay where they were, as bright ({old['lights'][0]}; {new['lights']})")
    g.call("editor_undo", action="undo")
    s.check(len(g.call("actors")["actors"]) == len(actors_before), "undo brings both actors back")
    light = g.call("lights", action="add", position=[p["x"] + 80, p["y"] - 100, p["z"]])["lights"][-1]["id"]
    s.select(action="set", ids=[light, s.actor])
    r = lib(s, action="save", name="Lamp and candle")
    new = g.call("actors")["actors"][-1]
    s.check(new["prefab"] == "lamp_and_candle" and len(new["lights"]) == 1 + lights_of[s.actor]
            and light not in [x["id"] for x in g.call("lights")["lights"]],
            f"a light and an actor: one prefab ({len(new['lights'])} lights)")
    g.call("editor_undo", action="undo")
    s.select(action="set", ids=[light, other])
    s.select(action="delete")
    s.select(action="set", ids=[s.volume])
    dup = s.select(action="duplicate")["primary"]
    s.select(action="set", ids=[s.volume, dup])
    try:
        lib(s, action="save", name="Two mists")
        s.check(False, "several fog volumes are refused")
    except RpcError as err:
        s.check("one volume" in str(err), f"several fog volumes: refused, saying why ({err})")
    s.select(action="set", ids=[dup])
    s.select(action="delete")
    # The Outliner's right-click menu: Save as prefab... opens the dialog.
    lib(s, action="filter", kind="", category="", search="", favourites=False, mine=False)
    lib(s, action="view", mode="grid", docked=True)
    g.call("editor_ui", tab="library")
    first = tile_of(s, "light:point")
    g.call("editor_ui", tab="outliner")
    s.select(action="set", ids=[s.light])
    g.run(3)
    if s.check(first is not None, "a point inside the side column"):
        s.click(centre(first), button=3)
        r = lib(s)
        if s.check(r["save_item"] is not None, f"the Outliner's right-click menu has Save as prefab... ({r['save_item']})"):
            s.click(centre(r["save_item"]))
            g.call("editor_ui", tab="library")
            g.run(3)
            r = lib(s)
            s.check(r["save_dialog"] and "save_cancel" in r["rects"], "it opens Save as prefab's dialog")
            if "save_cancel" in r["rects"]:
                s.click(centre(r["rects"]["save_cancel"]))
                s.check(not lib(s)["save_dialog"], "Cancel closes it")
    g.call("editor_ui", tab="outliner")


def tile_of(s, item):
    s.game.run(3)
    return next((e["tile"] for e in lib(s)["entries"] if e["item"] == item and e["tile"]), None)


def check_manage(s):
    """An entry's right-click menu: Edit prefab (also the footer's button for the current item); for one
    of yours Rename... and Delete... (a confirm first; the file moved aside), its file's path."""
    g = s.game
    s.select(action="set", ids=[s.actor])
    r = lib(s, action="save", name="Manage candle")
    g.call("editor_undo", action="undo")  # the candle keeps its own prefab; the saved one stays in the library
    s.select(action="set", ids=[s.volume])
    r2 = lib(s, action="save", name="Manage mist")
    g.call("editor_undo", action="undo")
    candle, mist = r["item"], r2["item"]
    f = lib(s, action="file", item=candle)["file"]
    s.check(f.endswith("manage_candle.json") and pathlib.Path(f).exists() and lib(s, action="file", item="prefab:candle")["file"] == "",
            f"file: a prefab of yours has its file, a built-in none ({f})")
    lib(s, action="rename", item=candle, name="Renamed candle")
    e = next(x for x in lib(s)["entries"] if x["item"] == candle)
    s.check(e["name"] == "Renamed candle" and e["id"] == "manage_candle" and '"Renamed candle"' in pathlib.Path(f).read_text(),
            f"rename: its name (in its file), its id stays ({e['name']}, {e['id']})")
    try:
        lib(s, action="delete", item="prefab:candle")
        s.check(False, "a built-in cannot be deleted")
    except RpcError as err:
        s.check("yours" in str(err), f"a built-in cannot be deleted ({err})")
    # The panel: a real right-click on a tile of yours.
    lib(s, action="filter", kind="", category="", search="", favourites=False, mine=True)
    lib(s, action="view", mode="grid", docked=True)
    g.call("editor_ui", tab="library")
    tile = tile_of(s, candle)
    if s.check(tile is not None, "a tile of yours under Mine"):
        s.click(centre(tile), button=3)
        r = lib(s)
        s.check(r["menu"] == candle and "menu:edit_prefab" in r["rects"] and "menu:delete" in r["rects"],
                f"right-click: its menu ({r['menu']!r}, {sorted(k for k in r['rects'] if k.startswith('menu:'))})")
        if "menu:edit_prefab" in r["rects"]:
            s.click(centre(r["rects"]["menu:edit_prefab"]))
            s.check(lib(s)["editing_prefab"] == "manage_candle", f"Edit prefab opens the prefab editor on it "
                                                                  f"({lib(s)['editing_prefab']!r})")
        lib(s, action="edit_prefab", item="")
    tile = tile_of(s, mist)
    if s.check(tile is not None, "the fog preset of yours under Mine"):
        fog_file = pathlib.Path(lib(s, action="file", item=mist)["file"])
        s.click(centre(tile), button=3)
        r = lib(s)
        if s.check("menu:delete" in r["rects"], f"its menu has Delete... ({sorted(r['rects'])})"):
            s.click(centre(r["rects"]["menu:delete"]))
            r = lib(s)
            s.check("delete_yes" in r["rects"] and mist in items(r), "Delete... asks first; it stays meanwhile")
            if "delete_cancel" in r["rects"]:
                s.click(centre(r["rects"]["delete_cancel"]))
                s.check(mist in items(lib(s)) and fog_file.exists(), "Cancel keeps it")
            s.click(centre(tile_of(s, mist) or tile), button=3)
            s.click(centre(lib(s)["rects"]["menu:delete"]))
            r = lib(s)
            if "delete_yes" in r["rects"]:
                s.click(centre(r["rects"]["delete_yes"]))
            s.check(mist not in items(lib(s)) and not fog_file.exists()
                    and pathlib.Path(str(fog_file) + ".bak").exists(),
                    "Delete: gone from the library, its file moved aside (.bak)")
    # The footer's Edit prefab: the current item's prefab.
    lib(s, action="filter", mine=False, kind="prefab")
    tile = tile_of(s, "prefab:candle")
    if s.check(tile is not None, "the candle's tile"):
        s.click(centre(tile))
        r = lib(s)
        if s.check("edit_prefab" in r["rects"], f"the footer's Edit prefab for the current item ({sorted(r['rects'])})"):
            s.click(centre(r["rects"]["edit_prefab"]))
            s.check(lib(s)["editing_prefab"] == "candle", "it opens the prefab editor on the candle")
        lib(s, action="edit_prefab", item="")
    lib(s, action="delete", item=candle)
    s.check(candle not in items(lib(s, action="filter", kind="")) and pathlib.Path(f + ".bak").exists(),
            "RPC delete: a prefab of yours moved aside")
    g.call("editor_ui", tab="outliner")


SECTIONS = [check_sources, check_filters, check_drops, check_thumbs, check_panel, check_save, check_save_groups, check_manage,
            check_emitters]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(pinned_card()))
    parser.add_argument("--out", help="keep the output here")
    parser.add_argument("--only", help="comma-separated section names (sources, filters, ...)")
    args = parser.parse_args()
    work = pathlib.Path(args.out) if args.out else OUT / "editor_library"  # start() writes its own files here too
    for sub in ("prefabs", "thumbs", "emitters"):  # a rerun starts with no prefabs of the user's and no cached thumbnails
        shutil.rmtree(work / sub, ignore_errors=True)
    (work / "thumbs_look.json").unlink(missing_ok=True)
    s = start(args, "editor_library", extra_env={"ROOD_PREFABS": str(work / "prefabs"),
                                                  "ROOD_LIBRARY_THUMBS": str(work / "thumbs"),
                                                  "ROOD_LIBRARY_THUMBS_LOOK": str(work / "thumbs_look.json"),
                                                  "ROOD_EMITTERS": str(work / "emitters")})
    if s is not None:
        s.work = work
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
        status = finish(s, "the library: sources, filters, favourites, thumbnails, drops on floors and walls, "
                           "the panel, Save as prefab, an entry's menu")
    return status


if __name__ == "__main__":
    sys.exit(main())
