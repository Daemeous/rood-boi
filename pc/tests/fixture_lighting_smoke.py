#!/usr/bin/env python3
"""Generated fixture lighting in the game (pc/docs/Fixture_Catalog.md).

    python3 pc/tests/fixture_lighting_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues a copy of the card's save (zone 9 room 17, the wine cellar's save
room) with private copies of the lighting packs (ROOD_LIGHTING) and of the
fixture catalog (ROOD_FIXTURES), so nothing tracked is written, then:

- save room: the generated layer's actors are there, one per flame, each
  naming its fixture type; enabled ones are lit (intensity > 0), inferred
  fill lights (the save point's) are off; the fixture shadow exclusion is
  active (each light's shadow_skip is its type's fixture radius); the save
  point's glow is linked (the actor's glow key is the glow's) and typed;
  everything comes from the generated layer unchanged (badge G, the user
  layer as saved would be empty);
- warps to zone 9 room 5 (candles, a fire, the save point and decals and
  light shafts drawn as additive polygons): candle actors exist and are
  lit; the glows of suppressed types (shafts, decals) are muted by their
  type (muted 4) and not traced, the others are traced;
- a type edit (RPC fixtures set: intensity, light offset, fixture radius)
  applies live to every instance of the type in the room, nothing else;
  glow suppress on another type mutes its glows at once, and back;
- saving the catalog writes it in the tools' layout (fixture_lib.json_write
  of what was written gives the same bytes); an instance override (by id)
  goes to the user layer as one override line; regenerate (the generator
  run by the game into the private packs) leaves the generated file as it
  was (deterministic);
- other zones: a torch room (zone 13 room 0), the town's street lanterns
  (zone 48 room 5: their class lights cool) and the 67-candle hall (zone 26
  room 0, reached through a made-up door): every generated actor is there
  and lit; Vulkan takes every light (none left out), OpenGL keeps the most
  important within its budget.
"""
import argparse
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from editor_cutaway_smoke import default_card  # noqa: E402
import fixture_lib as fl  # noqa: E402
import lighting_format as lf  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", default=str(default_card()))
    ap.add_argument("--out")
    args = ap.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if not (ROOT / "lighting/default/generated/zone09_room17.txt").exists():
        print("SKIP: no generated lighting for zone 9 (pc/tools/generate_lighting.py --zone 9)")
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "fixture_lighting"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shutil.copyfile(args.card, out / "card1.mcd")
    packs, user = out / "packs", out / "user"
    shutil.copytree(ROOT / "lighting", packs)
    user.mkdir()
    catalog = out / "catalog.json"
    shutil.copyfile(fl.CATALOG, catalog)
    env = {"ROOD_LIGHTS": str(user), "ROOD_LIGHTING": str(packs), "ROOD_FIXTURES": str(catalog), "ROOD_GI": "1",
           "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-fixtures-{os.getpid()}.sock"
    failures = []
    cat = fl.load_catalog(catalog)
    types = {t["id"]: fl.type_fields(cat, t) for t in cat["types"]}

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)
        return ok

    game = launch_at("save_room", card=out / "card1.mcd", fast=True, log=out / "game.log", extra_env=env,
                     socket_path=socket)

    def warp(zone, room):
        game.call("room_goto", zone=zone, room=room)
        for _ in range(90):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        st = game.state()
        return (st["zone"], st["room"]) == (zone, room)

    def generated(zone, room):
        f = lf.read_file(str(packs / "default/generated" / lf.room_file_name(zone, room)))
        return {e.id: e.item for e in f.entries if e.verb == "define"}

    def actors_by_id():
        return {a["id"]: a for a in game.call("actors", time=1.0)["actors"]}

    try:
        st = game.state()
        if (st["zone"], st["room"]) != (9, 17) and not warp(9, 17):
            check(False, f"in the save room (zone {st['zone']} room {st['room']})")
            return 1
        game.call("graphics", gi=True)
        game.run(60)

        # ---- the save room ----
        gen = generated(9, 17)
        live = actors_by_id()
        gen_actors = {i: it for i, it in gen.items() if it.kind == "actor"}
        check(set(gen_actors) <= set(live) and gen_actors,
              f"the generated actors are loaded ({len(gen_actors)}: {sorted(gen_actors)})")
        for i, it in gen_actors.items():
            a = live.get(i)
            if not a:
                continue
            t = it.get("fixture_type")
            f = types[t]
            check(a["fixture_type"] == t and a["prefab"] == f["prefab"], f"{i}: type {t}, prefab {a['prefab']}")
            lit = all(l["intensity"] > 0 and l["enabled"] for l in a["lights"]) and a["lights"]
            if f["enabled"]:
                check(a["enabled"] and lit, f"{i}: on and lit ({[round(l['intensity'], 3) for l in a['lights']]})")
            else:
                check(not a["enabled"], f"{i}: an inferred fill light, off by default")
            skips = {round(l["shadow_skip"], 2) for l in a["lights"]}
            check(skips == {round(f["fixture_radius"], 2)} and a["fixture"],
                  f"{i}: fixture shadow exclusion active (shadow_skip {skips}, the type's radius {f['fixture_radius']})")
        fx = game.call("fixtures")
        glow_keys = {it.name for it in gen.values() if it.kind == "glow"}
        typed = {g["key"]: g for g in fx["glows"]}
        check(glow_keys and glow_keys <= set(typed), f"the room's catalogued glows carry their type ({sorted(typed)})")
        linked = [a for a in live.values() if a["glow_key"]]
        check(linked and all(a["glow_key"] in typed for a in linked),
              f"linked actors name the game's glow ({[a['glow_key'] for a in linked]})")
        lit = game.call("lighting")
        badges = {i["id"]: i["badge"] for i in lit["items"]}
        check(all(badges.get(i) == "G" for i in gen), f"generated items unchanged by loading (badges {set(badges.values())})")
        check(lit["user_layer"] == "", f"nothing to save in the user layer ({lit['user_layer'][:120]!r})")

        # ---- room 5: candles, a fire, the save point, shafts and decals ----
        check(warp(9, 5), "warped to zone 9 room 5")
        game.run(90)
        gen = generated(9, 5)
        live = actors_by_id()
        candles = [i for i, it in gen.items() if it.kind == "actor" and types[it.get("fixture_type")]["prefab"] == "candle"]
        check(len(candles) >= 3 and all(i in live for i in candles)
              and all(live[i]["lights"] and live[i]["lights"][0]["intensity"] > 0 for i in candles),
              f"{len(candles)} candle actors, lit")
        glows = {g["key"]: g for g in game.call("glows")["glows"]}
        sup, kept = [], []
        for it in gen.values():
            if it.kind != "glow":
                continue
            g = glows.get(it.name)
            if not g:
                check(False, f"glow {it.name} exists in the game")
                continue
            (sup if types[it.get("fixture_type")]["glow"] == "suppress" else kept).append(g)
        check(sup and all(g["muted"] == 4 and not g["traced"] for g in sup),
              f"{len(sup)} glows of suppressed types (shafts, decals) are muted by their type and not traced")
        check(kept and all(g["muted"] == 0 for g in kept),
              f"{len(kept)} other catalogued glows still light ({sum(g['traced'] for g in kept)} traced)")

        # ---- a live type edit ----
        by_type = {}
        for i, it in gen.items():
            if it.kind == "actor":
                by_type.setdefault(it.get("fixture_type"), []).append(i)
        tid = max(by_type, key=lambda t: (types[t]["prefab"] == "candle", len(by_type[t])))
        others = [i for t, ids in by_type.items() if t != tid for i in ids]
        before = actors_by_id()
        game.call("fixtures", action="set", type=tid, params={"intensity": 2.5}, offset=[0, -10, 0], fixture_radius=21)
        after = actors_by_id()
        ok = True
        for i in by_type[tid]:
            b, a = before[i]["lights"][0], after[i]["lights"][0]
            want = 2.5 / (b["intensity"] / b["factor"]) if b["factor"] else 0
            ratio = (a["intensity"] / b["intensity"]) if b["intensity"] else 0
            ok &= abs(ratio - want) < 0.02 and abs((b["position"][1] - a["position"][1]) - 10) < 0.6 \
                and abs(a["shadow_skip"] - 21) < 1e-3
        check(ok and len(by_type[tid]) >= 2, f"editing type {tid} changes all {len(by_type[tid])} instances at once "
                                             "(intensity, offset, fixture radius), live")
        check(all(before[i]["lights"] == after[i]["lights"] for i in others if i in before and i in after),
              "other types' actors are untouched")
        gl_type = next((it.get("fixture_type") for it in gen.values() if it.kind == "glow"
                        and types[it.get("fixture_type")]["glow"] != "suppress"
                        and types[it.get("fixture_type")]["prefab"] == "none"), None)
        if gl_type:
            game.call("fixtures", action="set", type=gl_type, glow="suppress")
            game.run(4)
            g2 = game.call("fixtures")["glows"]
            muted = [g for g in g2 if g["type"] == gl_type]
            check(muted and all(g["muted"] == 4 for g in muted), f"glow suppress on {gl_type} mutes its glows at once")
            game.call("fixtures", action="set", type=gl_type, glow=None)
            game.run(4)
            back = [g for g in game.call("fixtures")["glows"] if g["type"] == gl_type]
            check(back and all(g["muted"] == 0 for g in back), f"and back ({gl_type}: its class's treatment)")
        saved = game.call("fixtures", action="save")
        text = catalog.read_text()
        check(not saved["dirty"] and fl.json_write(fl.load_catalog(catalog)) + "\n" == text,
              "Save writes the catalog in the tools' canonical layout")
        written = fl.load_catalog(catalog)
        t = next(x for x in written["types"] if x["id"] == tid)
        check(t.get("params", {}).get("intensity") == 2.5 and t.get("offset") == [0, -10, 0]
              and t.get("fixture_radius") == 21, f"the saved type {tid}: params, offset, radius")
        lit = game.call("lighting")
        check(lit["user_layer"] == "", "type edits are not instance edits (the user layer stays empty)")

        # ---- an instance override goes to the user layer by id ----
        one = by_type[tid][0]
        game.call("actors", action="set", index=after[one]["index"], params={"intensity": 0.4})
        game.call("lighting", action="save")
        f = lf.read_file(str(user / lf.room_file_name(9, 5)))
        check(len(f.entries) == 1 and f.entries[0].verb == "override" and f.entries[0].id == one
              and dict(f.entries[0].set).get("intensity") == "0.4",
              f"an instance override is one override line by id ({[lf.format_entry(e) for e in f.entries]})")

        # ---- regenerate from the game: deterministic ----
        path = packs / "default/generated" / lf.room_file_name(9, 5)
        old = path.read_bytes()
        r = game.call("fixtures", action="regenerate")
        check(path.read_bytes() == old, f"regenerating the room keeps its generated file ({r['message']})")
        game.run(30)
        live = actors_by_id()
        check(abs(live[one]["lights"][0]["intensity"] / live[one]["lights"][0]["factor"] - 0.4) < 1e-3,
              "the instance override survives regeneration")

        # ---- other zones: torches, the town's street lanterns, the candle hall ----
        vulkan = os.environ.get("ROOD_RENDERER") == "vulkan"
        for zone, room, cell, what in ((13, 0, None, "torches and a brazier"), (48, 5, None, "a street lantern"),
                                       (26, 0, [8, 0, 8, 0], "the 67-candle hall (no door: a made-up one)")):
            kw = {"cell": cell} if cell else {}
            game.call("room_goto", zone=zone, room=room, enemies=False, **kw)
            for _ in range(90):
                game.run(20)
                if game.call("room_goto", action="status")["phase"] in (0, 4):
                    break
            game.run(60)
            st = game.state()
            if not check((st["zone"], st["room"]) == (zone, room), f"warped to zone {zone} room {room} ({what})"):
                continue
            gen = {i: it for i, it in generated(zone, room).items() if it.kind == "actor"}
            reply = game.call("actors")
            live = {a["id"]: a for a in reply["actors"]}
            lit = [i for i, it in gen.items() if i in live and live[i]["lights"]
                   and all(l["intensity"] > 0 for l in live[i]["lights"])]
            on = [i for i, it in gen.items() if types[it.get("fixture_type")]["enabled"]]
            check(gen and set(on) <= set(lit), f"zone {zone} room {room}: {len(gen)} generated actors, "
                                               f"{len(lit)} lit ({what})")
            if zone == 48:  # the street lanterns' class: cool light (cyan panes), not a warm flame
                c = [live[i]["lights"][0]["color"] for i in lit if gen[i].get("fixture_type") in ("T013", "T014")]
                check(c and all(x[2] > x[0] for x in c), f"street lanterns light cool ({[[round(v, 2) for v in x] for x in c]})")
            game.run(4)
            reply = game.call("actors")
            wanted, dropped = reply["lights_wanted"], reply["dropped"]
            if vulkan:
                check(wanted >= len(on) and dropped == 0,
                      f"Vulkan takes every light ({wanted} wanted, {dropped} left out)")
            else:
                check(wanted - dropped in (min(wanted, 16), min(wanted, 64)),
                      f"OpenGL keeps the most important within its budget ({wanted} wanted, {dropped} left out)")
    finally:
        game.close()
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
