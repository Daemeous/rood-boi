#!/usr/bin/env python3
"""Room Browser and lighting layers in the game.

Continues the memory card's save (a copy) in its save room with a private
user layer (ROOD_LIGHTS) and a private packs directory (ROOD_LIGHTING) that
holds a generated and an authored layer for two rooms, then:
- RPC rooms: the zones and rooms read from the disc at run time (names from
  the game's own text), the current room among them;
- RPC lighting: the save room's layers merged by id, layer badges, a
  dangling override reported (and kept);
- RPC room_goto: warps to rooms in the same zone and in two other zones, as
  the game's door transition does: the state, the lights' room and the
  captured room geometry follow, Ashley is placed, no enemies, the map's
  "rooms seen" bits are untouched, saving is refused while warped; editing
  there saves to the user layer (version 2, the diff from the packs),
  revert and Promote work;
- return to the save room ends the warped state; no crash.

    python3 pc/tests/room_browser_smoke.py --card pc/save/card1.mcd [--out DIR] [--evidence DIR]

--evidence saves small PNGs of the editor (Room Browser, Outliner layers).
"""
import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402
import lighting_format as lf  # noqa: E402

GENERATED = """rood-lighting 2
# test: a generated layer
actor gen.candle.0001 prefab=candle pos={x},{y},{z} seed=3
light gen.lamp.0001 pos={x},{y},{z} intensity=0.8 radius=900
"""
AUTHORED = """rood-lighting 2
# test: an authored layer (this comment stays when promoting)
override light:gen.lamp.0001 intensity=1.6
override light:gen.lamp.gone intensity=1
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    ap.add_argument("--out")
    ap.add_argument("--evidence", help="save editor screenshots (PNG) here")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "room_browser"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    user, packs = out / "user-lighting", out / "packs"
    user.mkdir()
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(packs), "ROOD_GI": "0",
           "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-rooms-{os.getpid()}.sock"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    def pack_room(zone, room, x, y, z):
        name = lf.room_file_name(zone, room)
        (packs / "default/generated").mkdir(parents=True, exist_ok=True)
        (packs / "default/rooms").mkdir(parents=True, exist_ok=True)
        (packs / "default/generated" / name).write_text(GENERATED.format(x=x, y=y - 120, z=z))
        (packs / "default/rooms" / name).write_text(AUTHORED)

    # The save room's pack lighting goes in before launch (Ashley's spot
    # comes later; any point in the room will do for the layers).
    pack_room(9, 17, 448, 0, 576)
    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env, socket_path=socket)

    def settle(vsyncs=60):
        # The editor polls the files twice a second of wall-clock time: run
        # its check now (the live reload's change detection), then draw.
        game.call("lighting", action="poll")
        game.run(vsyncs)

    def warp(zone, room, **kw):
        reply = game.call("room_goto", zone=zone, room=room, **kw)
        if not reply["ok"]:
            return reply, None
        for _ in range(60):
            game.run(20)
            status = game.call("room_goto", action="status")
            if status["phase"] in (0, 4):
                break
        game.run(30)
        return status, game.state()

    def evidence(name):
        if not args.evidence:
            return
        d = pathlib.Path(args.evidence)
        d.mkdir(parents=True, exist_ok=True)
        game.run(6)
        path = d / f"{name}.png"
        game.present_screenshot(path)
        try:
            from PIL import Image
            im = Image.open(path)
            im.thumbnail((960, 540))
            im.save(path)
        except ImportError:
            pass

    try:
        game.wait_title()
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        home = (state["zone"], state["room"])
        check(state.get("player") is not None and game.call("scene")["triangles"] > 0,
              f"continued into zone {home[0]} room {home[1]} with Ashley")
        if home != (9, 17):
            print(f"note: the card's save is in zone {home[0]} room {home[1]}, not the save room 9/17")
        game.call("editor_ui", visible=True, rooms=True)
        game.run(4)

        # The rooms, from the disc.
        rooms = game.call("rooms")
        listed = {(r["zone"], r["room"]): r for r in rooms["rooms"]}
        zones = {r["zone"] for r in rooms["rooms"] if r["entries"]}
        named = sum(1 for r in rooms["rooms"] if r["name"])
        check(rooms["count"] > 300 and not rooms["error"], f"{rooms['count']} rooms read from the disc in {rooms['ms']} ms")
        check(len(zones) > 30 and named > 300, f"{len(zones)} zones with doors, {named} rooms named from the game's text")
        check(home in listed and tuple(rooms["current"]) == home, f"the current room is listed ({listed.get(home)})")

        # The save room's layers.
        if home == (9, 17):
            lit = game.call("lighting")
            badges = {i["id"]: i["badge"] for i in lit["items"]}
            check([l["label"] for l in lit["layers"]] == ["default/generated", "default/rooms", "user"],
                  "three layers: generated, authored, user")
            check(badges.get("light:gen.lamp.0001") == "G>P" and badges.get("actor:gen.candle.0001") == "G",
                  f"items carry their layers' badges ({badges})")
            check(any("light:gen.lamp.gone" in p["message"] and p["layer"] == "default/rooms" for p in lit["problems"]),
                  "the dangling override is reported")
            evidence("outliner-layers-save-room")

        # Warps: the same zone, two other zones, then home.
        same = next((r for r in rooms["rooms"] if r["zone"] == home[0] and r["entries"]
                     and (r["zone"], r["room"]) != home), None)
        others = []
        for want in ((11, 0), (13, 0), (32, 0), (50, 0)):
            if want in listed and listed[want]["entries"] and want[0] != home[0]:
                others.append(listed[want])
        if len(others) < 2:
            others += [r for r in rooms["rooms"] if r["entries"] and r["zone"] not in (home[0], *(o["zone"] for o in others))][:2]
        targets = ([same] if same else []) + others[:3]
        map_before = game.read_symbol("vs_main_mapStatus", 72)
        game.call("graphics", gi=False)
        visited = set()
        for i, t in enumerate(targets):
            where = (t["zone"], t["room"])
            status, st = warp(*where)
            ok = st is not None and (st["zone"], st["room"]) == where
            check(ok, f"warp to zone {where[0]} room {where[1]} ({t['name'] or t['area']}): {status.get('message')}")
            if not ok:
                continue
            visited.add(where[0])
            scene = game.call("scene")
            lights = game.call("lighting")
            check(st.get("player") is not None and scene["triangles"] > 0 and (lights["zone"], lights["room"]) == where,
                  f"  Ashley placed, {scene['triangles']} room triangles captured, the editor follows")
            check(status["warped"] and status["block_saves"], "  warped; saving is refused meanwhile")
            if i == 0:
                evidence("room-browser-warped")
        check(len(visited) >= 3 or (len(visited) >= 2 and not same), f"warped across zones {sorted(visited)}")
        check(game.read_symbol("vs_main_mapStatus", 72) == map_before, "the map's rooms-seen bits are untouched")

        # Lighting in a warped-to room: pack layers, a user edit, revert, promote.
        target = targets[-1]
        tz, tr = target["zone"], target["room"]
        st = game.state()
        player = st["player"]
        pack_room(tz, tr, player["x"], player["y"], player["z"])
        settle()  # live reload of the new pack files
        lit = game.call("lighting")
        ids = {i["id"] for i in lit["items"]}
        check({"light:gen.lamp.0001", "actor:gen.candle.0001"} <= ids, f"new pack files load live ({sorted(ids)})")
        game.call("lights", action="add", position=[player["x"], player["y"] - 200, player["z"]])
        game.call("actors", action="save")
        text = (user / lf.room_file_name(tz, tr)).read_text()
        f = lf.parse_text(text)
        check(f.version == 2 and len(f.entries) == 1 and f.entries[0].id.startswith("light:"),
              f"Save area writes only the new light to the user layer: {text.strip().splitlines()[-1]}")
        new_id = f.entries[0].id
        lit = game.call("lighting")
        badge = {i["id"]: i["badge"] for i in lit["items"]}
        check(badge.get(new_id) == "U", f"the new light's badge is U ({badge.get(new_id)})")
        before = {i["id"]: i["line"] for i in lit["items"]}
        game.call("lighting", action="promote", ids=[new_id])
        authored = (packs / "default/rooms" / lf.room_file_name(tz, tr)).read_text()
        check(new_id.split(":", 1)[1] in authored and "# test: an authored layer" in authored
              and not (user / lf.room_file_name(tz, tr)).exists(),
              "Promote moves the light into the pack's authored file (its comment kept; the user file emptied)")
        lit = game.call("lighting")
        check({i["id"]: i["line"] for i in lit["items"]} == before and {i["id"]: i["badge"] for i in lit["items"]}.get(new_id) == "P",
              "the room is unchanged by promoting; the light is now P")
        game.call("lighting", action="revert", id=new_id)
        lit = game.call("lighting")
        check(new_id in {i["id"] for i in lit["items"]}, "reverting a pack item (no user change) keeps it")
        # A pack actor changed by the user: G>U; revert (undoable); delete and restore.
        def badge_of(id):
            return {i["id"]: i["badge"] for i in game.call("lighting")["items"]}.get(id)

        candle = "actor:gen.candle.0001"
        index = next(a["index"] for a in game.call("actors")["actors"] if a["id"] == candle)
        game.call("actors", action="set", index=index, params={"intensity": 2.5})
        user_text = game.call("lighting")["user_layer"]
        check(badge_of(candle) == "G>U" and f"override {candle} intensity=2.5" in user_text,
              f"a changed pack actor is G>U and saves as an override ({badge_of(candle)})")
        game.call("lighting", action="revert", id=candle)
        check(badge_of(candle) == "G" and candle not in game.call("lighting")["user_layer"],
              "Revert to the layers below drops the user's change")
        game.call("editor_undo", action="undo")
        check(badge_of(candle) == "G>U", f"and undo brings it back ({game.call('editor_undo', action='list')['undo']})")
        index = next(a["index"] for a in game.call("actors")["actors"] if a["id"] == candle)
        game.call("actors", action="remove", index=index)
        lit = game.call("lighting")
        check(candle not in {i["id"] for i in lit["items"]} and f"delete {candle}" in lit["user_layer"],
              "deleting a pack actor saves as a delete")
        game.call("lighting", action="revert", id=candle)
        check(badge_of(candle) == "G", "and Restore (revert) brings the pack's actor back")

        # Home again.
        status = game.call("room_goto", action="return")
        for _ in range(60):
            game.run(20)
            status = game.call("room_goto", action="status")
            if status["phase"] in (0, 4):
                break
        game.run(30)
        st = game.state()
        check((st["zone"], st["room"]) == home and not status["warped"], f"Return goes home and ends the warped state ({status})")
        check(game.call("scene")["triangles"] > 0, "the home room renders")
        evidence("room-browser-home")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    print("FAIL" if failures else "PASS", f"({len(failures)} failures)" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
