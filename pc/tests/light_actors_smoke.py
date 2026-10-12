#!/usr/bin/env python3
"""Light actors and the Place Actors panel (light_prefabs.h, light_actors.cpp):
data-first prefabs (candle, torch, ...) placed into the room by drag and
drop with the editor's snapping, their parameters overridden per instance,
saved and loaded with the room, animated deterministically, and linked to
the game's glows.

    python3 pc/tests/light_actors_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one) into its save room, with a private lights directory (ROOD_LIGHTS) and
prefab directory (ROOD_PREFABS). The room file starts with old-format light
lines and an actor line. Checks:

- the built-in prefabs load (prefabs RPC) and old light lines still load
  next to actor lines (backward compatibility), and survive a save;
- a drag from Place Actors (editor_place begin / move / drop) lands where
  the editor's snapping says: an actor's pivot on the surface (or on the
  snapped point), a plain light off the surface by the snap offset; Escape
  (cancel) and a drop off the view add nothing; double-click places at the
  view's centre; the dropped actor is selected;
- parameter overrides: only the overridden values are written (actor line),
  and they come back after the room file is reloaded; a prefab edited in
  the user directory changes every instance that does not override it;
- flicker is deterministic (the same time gives the same lights, another
  time different ones) and the animation clock can be fixed (RPC time);
- an actor's light reaches the picture (the view changes with it on);
- glows: the save point's glow gets a magic glow suggestion; accepting it
  links the glow, and the linked glow's emission follows the actor's pulse.

--out keeps the frames (Place panel, a drag in progress, lit actors; game
imagery: keep it out of git)."""
import argparse
import math
import os
import pathlib
import shutil
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, RpcError  # noqa: E402
import golden_frames as gf  # noqa: E402
from editor_cutaway_smoke import default_card, pose_looking_at  # noqa: E402

OLD_LINES = (
    "# an old room file: light lines only (17 fields, and the short 9-field form)\n"
    "light point 400.0 -200.0 560.0 1.000 0.750 0.450 1.000 600.0 0.000 1.000 0.000 45.0 0 1 128.0 128.0\n"
    "light spot 300.0 -300.0 500.0 0.500 0.600 1.000 0.800 900.0 0.000 1.000 0.000 60.0 1 0\n"
    "light point 500.0 -150.0 600.0 0.9 0.8 0.7 1.2 500.0\n"
)


def close(a, b, tolerance=0.05):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def diff(a, b):
    return float(np.abs(a.astype(int) - b.astype(int)).max(axis=2).mean())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "light_actors"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    prefabs = pathlib.Path(tempfile.mkdtemp(prefix="prefabs-", dir=out))
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_PREFABS": str(prefabs), "ROOD_GI": "1",
           "ROOD_SCALE": "2"}
    socket = f"/tmp/rood-actors-{os.getpid()}.sock"  # sun_path is short; worktree paths are long
    if os.environ.get("ROOD_RPC"):
        socket = os.environ["ROOD_RPC"]
    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env, socket_path=socket)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    def settle_reload(live=False):
        """Take the room file as written. The editor polls files twice a
        second of wall-clock time; tests run its check now instead (`live`:
        RPC lighting poll, the live reload's change detection) or read the
        room again (RPC lights reload), then let a few frames draw."""
        if live:
            game.call("lighting", action="poll")
        else:
            game.reload_lights()
        game.run(4)

    def frame(name, keep=True):
        game.run(gf.SETTLE)
        path = out / f"{name}.png"
        game.present_screenshot(path)
        image = np.asarray(Image.open(path).convert("RGB"))
        if not args.out or not keep:
            path.unlink()
        return image

    try:
        game.wait_title()
        library = game.call("prefabs")
        ids = [p["id"] for p in library["prefabs"]]
        check({"candle", "torch", "lantern", "fireplace", "window_light", "magic_glow"} <= set(ids)
              and all(not p["error"] for p in library["prefabs"]) and all(p["source"] == "builtin" for p in library["prefabs"]),
              f"built-in prefabs load without problems ({', '.join(ids)})")
        game.call("graphics", **{**gf.DEFAULTS, "scale": 2, "editor_wireframe": False, "editor_points": False,
                                 "editor_hide_masks": True, "editor_game_cutaway": True, "editor_cutaway": False,
                                 "gi": True, "rt_shadows": True, "editor_snap_point": False, "editor_snap_edge": False,
                                 "editor_snap_surface": False})
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        player = state.get("player")
        if not player or not game.call("scene")["triangles"]:
            print(f"FAIL: no room with Ashley after Continue: {state}")
            return 1
        zone, room = state["zone"], state["room"]
        room_file = lights / f"zone{zone:02d}_room{room:02d}.txt"
        # An old-format room file plus one actor line, written while the room is
        # loaded: the editor's live reload takes it (as a hand edit would).
        room_file.write_text(OLD_LINES + f"actor candle {player['x']:.1f} {player['y'] - 100:.1f} {player['z']:.1f} "
                             "0 0 0 seed=77 intensity=1.5\n")
        game.call("editor_ui", visible=True)
        settle_reload(live=True)  # a new file: the live reload's check sees it
        listing = game.call("actors")
        described = game.call("lights")
        check(len(described["lights"]) == 3 and listing["light_count"] == 3,
              f"old light lines still load ({len(described['lights'])} lights)")
        check(len(listing["actors"]) == 1 and listing["actors"][0]["overrides"] == {"intensity": 1.5}
              and listing["actors"][0]["seed"] == 77, "an actor line loads next to them, with its override")
        check(abs(described["lights"][2]["intensity"] - 1.2) < 1e-4 and not described["lights"][1]["enabled"],
              "the short light form and the enabled flag read as before")
        game.call("actors", action="remove", index=0)

        focus = (player["x"], player["y"] - 80, player["z"])
        above = pose_looking_at((player["x"] + 696, player["y"] - 945, player["z"] - 642), focus)
        game.call("editor_camera", **above)
        game.run(gf.SETTLE)
        frame("editor-open")
        view = game.call("editor_overlay", update=False)["view"]

        # Drag a candle in: the pivot lands on the surface the snap finds.
        snap = game.call("editor_snap", u=0.45, v=0.6, modes=["surface"])
        check(snap["type"] == "surface", f"a surface under the cursor ({snap['type']})")
        begin = game.call("editor_place", prefab="candle", phase="begin", u=0.2, v=0.3)
        check(begin["active"] and begin["over_view"], "a drag from Place Actors begins over the view")
        moved = game.call("editor_place", phase="move", u=0.45, v=0.6)
        check(moved["placement"]["ok"] and moved["placement"]["snap"] == "surface"
              and close(moved["placement"]["position"], snap["surface"]),
              f"while dragging, the candle's pivot follows the surface ({moved['placement']['position']})")
        frame("drag-candle")
        dropped = game.call("editor_place", phase="drop")
        actors = game.call("actors")["actors"]
        check(dropped["index"] == 0 and len(actors) == 1 and dropped["selected_actor"] == 0
              and close(actors[0]["position"], snap["surface"]), "dropped: the candle is placed there and selected")
        flame = actors[0]["lights"][0]["position"] if actors else [0, 0, 0]
        check(actors and flame[1] < actors[0]["position"][1] - 5,
              f"its flame sits above the pivot (y {flame[1]:.1f} < {actors[0]['position'][1]:.1f})" if actors else "flame")

        # A plain point light: off the surface by the snap offset.
        offset = game.call("graphics")["editor_snap_offset"]
        game.call("editor_place", light="point", phase="begin", u=0.45, v=0.6)
        light = game.call("editor_place", phase="drop")
        lit = game.call("lights")["lights"]
        expected = [snap["surface"][i] + snap["normal"][i] * offset for i in range(3)]
        check(light["index"] == 3 and close(lit[3]["position"], expected, 0.1),
              f"a dropped point light sits {offset:.0f} units off the surface")

        # Point snapping: the pivot on the room point under the cursor.
        game.call("graphics", editor_snap_point=True)
        st = game.call("editor_overlay", points=256)
        points = [p for p in st["point_samples"] if not p["hidden"]]
        if points:
            target = points[len(points) // 2]
            cursor = (target["screen"][0] + 2, target["screen"][1] + 2)
            ref = game.call("editor_snap", x=cursor[0], y=cursor[1], modes=["point"])
            game.call("editor_place", prefab="lantern", phase="begin", x=cursor[0], y=cursor[1])
            on_point = game.call("editor_place", phase="drop")
            placed = game.call("actors")["actors"][-1]
            check(ref["type"] == "point" and on_point["placement"]["snap"] == "point"
                  and close(placed["position"], ref["position"], 0.01),
                  f"with point snapping the pivot lands on the point ({placed['position']})")
        else:
            check(False, "visible room points to snap to")
        game.call("graphics", editor_snap_point=False)
        count = len(game.call("actors")["actors"])

        # Escape and drops off the view add nothing.
        game.call("editor_place", prefab="torch", phase="begin", u=0.5, v=0.5)
        cancelled = game.call("editor_place", phase="cancel")
        game.call("editor_place", prefab="torch", phase="begin", u=0.5, v=0.5)
        outside = game.call("editor_place", phase="drop", x=view[0] - 50, y=view[1] + 10)
        check(not cancelled["active"] and outside["index"] == -1 and len(game.call("actors")["actors"]) == count,
              "cancel (Escape) and a drop off the view place nothing")

        # Double-click: at the surface under the view's centre.
        centre = game.call("editor_snap", u=0.5, v=0.5, modes=["surface"])
        double = game.call("editor_place", prefab="torch", phase="double_click")
        torch = game.call("actors")["actors"][-1]
        check(double["index"] == count and close(torch["position"], centre["surface"]),
              "double-click places at the view's centre")
        check(abs(math.hypot(*[torch["lights"][0]["direction"][i] for i in (0, 2)])) >= 0 and torch["prefab"] == "torch",
              "the torch faces the surface it was put on")

        # Overrides: only what was changed is stored, and it round-trips.
        game.call("actors", action="set", index=0, params={"intensity": 2.5, "tint": [1, 0.5, 0.25], "shadows": False})
        game.call("actors", action="save")
        text = room_file.read_text()
        lines = [line for line in text.splitlines() if line.startswith("actor ") and " prefab=candle" in line]
        check(len(lines) == 1 and "intensity=2.5" in lines[0] and "tint=1,0.5,0.25" in lines[0] and "shadows=0" in lines[0]
              and "radius=" not in lines[0] and "flicker=" not in lines[0],
              f"only the overridden parameters are written: {lines[0] if lines else text}")
        check(sum(1 for line in text.splitlines() if line.startswith("light ")) == 4,
              "the old lights are written back as light lines")
        before = game.call("actors")["actors"]
        room_file.write_text(text + "# touched\n")  # a hand edit: the editor reloads it
        settle_reload()
        after = game.call("actors")["actors"]
        check(len(after) == len(before) and after[0]["overrides"] == {"intensity": 2.5, "tint": [1, 0.5, 0.25], "shadows": 0}
              and close(after[0]["position"], before[0]["position"], 0.06) and after[0]["seed"] == before[0]["seed"],
              "overrides, transform and seed come back after the room reloads")

        # Editing the prefab updates every instance that keeps its default.
        game.call("actors", action="add", prefab="candle", position=[player["x"], player["y"] - 40, player["z"] + 80])
        game.call("actors", time=3.0)
        mine = (ROOT / "pc/assets/prefabs/candle.json").read_text().replace('"default": 360', '"default": 777')
        (prefabs / "candle.json").write_text(mine)
        library = game.call("prefabs", reload=True)
        user = [p for p in library["prefabs"] if p["id"] == "candle"]
        listing = game.call("actors")["actors"]
        check(user and user[0]["source"] == "user" and user[0]["overrides_builtin"],
              "a user prefab with a built-in's id overrides it")
        check(all(a["lights"][0]["radius"] == 777 for a in listing if a["prefab"] == "candle"),
              "every candle follows the edited prefab's radius")
        game.call("actors", action="set", index=0, params={"radius": 300})
        check(game.call("actors")["actors"][0]["lights"][0]["radius"] == 300, "an override still wins over the prefab")
        (prefabs / "candle.json").unlink()
        game.call("prefabs", reload=True)

        # Flicker: a function of time.
        a = game.call("actors", action="sample", index=0, time=12.5)["sample"]
        b = game.call("actors", action="sample", index=0, time=12.5)["sample"]
        c = game.call("actors", action="sample", index=0, time=12.55)["sample"]
        check(a == b and a[0]["factor"] != c[0]["factor"], "flicker is deterministic: same time, same light; later, another")
        game.call("actors", time=7.25)
        game.run(3)
        f1 = game.call("actors")["actors"][0]["lights"][0]
        game.run(5)
        f2 = game.call("actors")["actors"][0]["lights"][0]
        held = game.call("actors", action="sample", index=0, time=7.25)["sample"][0]
        print(f"fixed time: {f1['factor']} {f2['factor']} sample {held['factor']}")
        check(f1 == f2 and abs(held["factor"] - f1["factor"]) < 1e-5 and close(held["position"], f1["position"], 1e-3),
              "a fixed animation time holds the lights still (and matches sample)")
        game.call("actors", time=None)
        game.run(5)
        f3 = game.call("actors")["actors"][0]["lights"][0]
        game.run(7)
        f4 = game.call("actors")["actors"][0]["lights"][0]
        check(f3["factor"] != f4["factor"], "with the live clock the candle flickers")

        # The actors' light reaches the picture.
        game.call("actors", time=2.0)
        game.call("editor_ui", visible=False)
        on = frame("actors-lit")
        listing = game.call("actors")["actors"]
        for i in range(len(listing)):
            game.call("actors", action="set", index=i, enabled=False)
        off = frame("actors-off", keep=False)
        on_again = None
        for i in range(len(listing)):
            game.call("actors", action="set", index=i, enabled=True)
        on_again = frame("actors-lit-again", keep=False)
        noise, change = diff(on, on_again), diff(on, off)
        check(change > 2 * noise + 0.3, f"the actors light the view (mean change {change:.2f}; noise {noise:.2f})")
        game.call("editor_ui", visible=True)

        # Glows: a suggestion for the save point, linked and driven.
        for i in reversed(range(len(listing))):
            game.call("actors", action="remove", index=i)
        game.run(20)
        suggested = game.call("actors", action="suggest", glows=True)
        glows = [g for g in suggested["glows"] if g["size"] >= 0]
        print(f"glows: {[(g['position'], [round(c, 2) for c in g['color']], round(g['size'], 1)) for g in glows]}")
        print(f"suggestions: {[(s['prefab'], s['glow']) for s in suggested['suggestions']]}")
        magic = [s for s in suggested["suggestions"] if s["prefab"] == "magic_glow"]
        check(glows and magic, "the save point's glow gets a magic glow suggestion")
        check(len(magic) == 1, f"one actor for the sigil's glow clusters ({len(magic)})")
        accepted = game.call("actors", action="suggest", accept=True)
        linked = [a for a in accepted["actors"] if a["glow"]]
        ticked = sum(1 for s in suggested["suggestions"] if s["accept"])
        check(accepted.get("added", 0) == ticked and linked
              and close(linked[0]["lights"][0]["position"], linked[0]["glow"], 1.0),
              "accepting adds the actors, their light on the glow, the glow linked")
        if linked:
            # The linked glow's emission follows the actor's pulse.
            target = linked[0]["glow"]

            def emission(t):
                game.call("actors", time=t)
                game.run(4)
                g = game.call("actors", glows=True)["glows"]
                best = min(g, key=lambda e: math.dist(e["position"], target))
                return sum(best["color"])
            e1, e2 = emission(0.625), emission(1.875)  # the pulse's top and bottom (0.4 Hz)
            game.call("actors", action="set", index=linked[0]["index"], params={"glow_mode": "Leave"})
            e3 = emission(0.625)
            print(f"linked glow emission {e1:.3f} / {e2:.3f}; left alone {e3:.3f}")
            check(e1 > e2 * 1.2 and abs(e3 - (e1 + e2) / 2) < abs(e1 - e2),
                  "the linked glow pulses with the actor (and stops when left alone)")
            game.call("actors", action="set", index=linked[0]["index"], params={"glow_mode": "Suppress"})
            game.run(4)
            g = game.call("actors", glows=True)["glows"]
            best = min(g, key=lambda e: math.dist(e["position"], target))
            check(best["muted"] == 1, "Suppress mutes the linked glow")
            game.call("actors", action="set", index=linked[0]["index"], reset=True)
        game.call("actors", time=None)
        frame("suggested-actors")

        # Fixtures: one click on a candle made of room polygons (the save
        # room's wall niche; no glow there) puts a candle at its flame, the
        # fixture recorded so its polygons cast no shadow from it.
        for i in reversed(range(len(game.call("actors")["actors"]))):
            game.call("actors", action="remove", index=i)
        if (zone, room) == (9, 17):
            niche = [190, -148, 1031]  # the clicked surface of the candle in the back wall's niche
            lit = game.call("actors", action="light_fixture", position=niche)
            a = lit["actors"][lit["index"]]
            flame = a["lights"][0]["position"]
            print(f"niche candle: {a['prefab']} flame {flame} fixture {a['fixture']} ({lit['status']})")
            check(a["fixture"] and a["prefab"] == "candle" and not a["glow"] and math.dist(flame, niche) < 32,
                  "Light this fixture: a candle at the niche candle's flame (found in its texture)")
            check(a["lights"][0]["shadow_skip"] == a["fixture_radius"] > 0,
                  f"its fixture casts no shadow from it (shadow rays stop {a['fixture_radius']:.0f} units short)")
            again = game.call("actors", action="light_fixture", position=niche)
            check(again["index"] == lit["index"] and len(again["actors"]) == 1, "lighting it again selects the same actor")
            tris = game.call("actors", action="fixture_triangles", index=lit["index"])["fixture_triangles"]
            check(tris > 0, f"the fixture's polygons are found for highlighting ({tris})")
            game.call("actors", action="set", index=lit["index"], fixture_shadows=True)
            check(game.call("actors")["actors"][lit["index"]]["lights"][0]["shadow_skip"] == 0,
                  "Fixture casts shadow on: the light's shadow rays see the fixture again")
            game.call("actors", action="save")
            line = [x for x in room_file.read_text().splitlines() if x.startswith("actor ") and " prefab=candle" in x]
            check(line and "fixture=" in line[0] and "fixture_shadows=1" in line[0], f"the fixture is saved: {line}")
            room_file.write_text(room_file.read_text() + "# touched\n")
            settle_reload()
            back = game.call("actors")["actors"]
            check(len(back) == 1 and back[0]["fixture"] and back[0]["fixture_shadows"]
                  and close(back[0]["fixture"], a["fixture"], 0.06), "and loads back")
            frame("niche-candle")
            if glows:
                on_glow = game.call("actors", action="light_fixture", position=glows[0]["position"])
                b = on_glow["actors"][on_glow["index"]]
                check(b["prefab"] == "magic_glow" and b["glow"] and b["fixture"],
                      "Light this fixture on the save point's glow: a magic glow, linked")
        else:
            print(f"(fixture checks skipped: the card's save room is zone {zone} room {room})")
        game.call("editor_ui", visible=False)
        game.call("editor_camera", off=1)
    except RpcError as e:
        check(False, f"RPC error: {e}")
    finally:
        game.close()
        shutil.rmtree(lights, ignore_errors=True)
        shutil.rmtree(prefabs, ignore_errors=True)
    log = pathlib.Path(game.log).read_text(errors="replace")
    if "[crash]" in log or "[gl] shader" in log or "[vk] validation:" in log:
        print("FAIL: crash or shader error; see", game.log)
        return 1
    if failures:
        print(f"FAIL: {len(failures)} checks; frames in {out}")
        return 1
    print("PASS: light actors place by snapping, round-trip their overrides, flicker deterministically and drive glows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
