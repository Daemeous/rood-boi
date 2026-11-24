#!/usr/bin/env python3
"""Encounter lab: a chosen fight in a chosen room, for testing.

  encounter.py roster ZONE
      the zone's enemies (ZONEnnn.ZND, the ids to use) and where its rooms
      place them (each room's MPD enemy records: tile, the record's index)
  encounter.py run --zone Z --room R --enemy Ghoul@5,7 --enemy 3@6,8:2 [--seed N]
                   [--ashley X,Z[,FACING]] [--entry N] [--save preset.json]
  encounter.py run ... [--profile NAME] [--stats max_hp=400,strength=90]
                   [--trace fight.jsonl [--trace-frames N]]
  encounter.py run --preset preset.json
      warps to the room (the Room Browser's room_goto, its scripts off) and
      has the game spawn these enemies there instead of the room's own:
      name or id @ tile x,z [: facing 0-3]. Each enemy copies a real spawn
      record of the zone (its AI settings and drops), from this room if it
      has one. The game checks each tile on arrival and leaves out (never
      moves) an enemy whose tile is blocked or off the floor. A seed makes
      the game's random numbers repeat.

      --profile gives Ashley a test setup first (VS-NativePort's Combat Lab
      profiles): post-prologue (Map and Score menus), post-minotaur (three
      chain and defense abilities, a spell of each class, the equipped
      family's first Break Art), all-spells, all-abilities, all-break-arts.
      --stats sets max HP/MP (refilled) and STR/INT/AGI (1..999). Both only
      add to the running game, and a save made afterwards keeps them.
      --trace records the fight from the spawn on: a JSON line for every
      frame in which something changed (pad, action, each character's HP,
      MP, Risk, stats, statuses, position, and the random draws), until
      --trace-frames have passed or `encounter.py trace stop`.
  encounter.py trace stop | status
  encounter.py trace summary fight.jsonl
      what happened in a trace: who was there, every HP/MP/status change
      with its frame, the actions, and the random draws.

The game side is pc/src/encounter_lab.c (RPC encounter). The running game is
reached through ROOD_RPC (tcp:PORT[:TOKEN]) or --rpc; the disc is ROOD_DISC
or --disc (default disks/SLUS-01040.bin).
"""
import argparse
import ast
import json
import os
import pathlib
import struct
import sys
import time

TOOLS = pathlib.Path(__file__).resolve().parent
ROOT = TOOLS.parents[1]
sys.path.insert(0, str(TOOLS))

RECORD = 0x28  # MPD enemy record (mpdEnemySpawn, 146C.c)
ZND_ENEMY = 0x464  # vs_battle_charInitData
LAB_MAX = 8


# ---- the disc ----

def text_table():
    """The game's text table (tools/etc/vsString.py), read without importing it."""
    tree = ast.parse((ROOT / "tools/etc/vsString.py").read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, "id", "") == "table" for t in node.targets):
            return ast.literal_eval(node.value)
    raise SystemExit("tools/etc/vsString.py: no text table")


def decode(raw, table):
    out, i = [], 0
    while i < len(raw):
        c = raw[i]
        if c == 0xE7:
            break
        if c >= 0xF8:  # a control code and its argument (0xFA: a space of some width)
            out.append(" " if c == 0xFA else "")
            i += 2
            continue
        if c < len(table) and isinstance(table[c], str):
            out.append(table[c])
        i += 1
    return " ".join("".join(out).split())


def zone_data(disc, zone):
    """(enemies, spawns) of a zone: enemies as dicts by ZND index, spawns as
    every enemy record of its rooms with the raw bytes."""
    from fixture_lib import zone_rooms

    znd, rooms, _ = zone_rooms(disc, zone)
    table = text_table()
    eo, _el = struct.unpack("<2I", znd[8:16])
    count = struct.unpack("<I", znd[eo:eo + 4])[0]
    base = eo + 4 + 8 * count
    enemies = []
    for i in range(count):
        e = znd[base + ZND_ENEMY * i:base + ZND_ENEMY * (i + 1)]
        hp, mp = struct.unpack("<2h", e[0x1C:0x20])
        enemies.append({"id": i, "name": decode(e[4:28], table), "hp": hp, "mp": mp,
                        "str": e[0x20], "int": e[0x21], "agi": e[0x22]})
    spawns = []
    for room, name in enumerate(rooms):
        if not name:
            continue
        mpd = disc.read("MAP/" + name)
        offset, length = struct.unpack("<2i", mpd[0x20:0x28])
        for k in range(length // RECORD if offset > 0 else 0):
            r = mpd[offset + RECORD * k:offset + RECORD * (k + 1)]
            if r[0] == 0 and r[4] != 0x7F:  # an enemy (type 1: an object; id 0x7F: a placeholder)
                spawns.append({"room": room, "index": k, "id": r[4], "x": r[0xC], "z": r[0xE],
                               "facing": r[0x23], "record": r})
    return enemies, spawns


def open_disc(path):
    from fixture_lib import Disc

    return Disc(path or os.environ.get("ROOD_DISC") or str(ROOT / "disks/SLUS-01040.bin"))


# ---- commands ----

def cmd_roster(args):
    disc = open_disc(args.disc)
    enemies, spawns = zone_data(disc, args.zone)
    print(f"zone {args.zone}: {len(enemies)} enemies")
    for e in enemies:
        rooms = sorted({s["room"] for s in spawns if s["id"] == e["id"]})
        where = ", ".join(map(str, rooms)) if rooms else "none (scripted only: no template record)"
        print(f"  {e['id']:3d}  {e['name']:<24} HP {e['hp']:4d}  MP {e['mp']:3d}  "
              f"STR {e['str']:3d} INT {e['int']:3d} AGI {e['agi']:3d}  rooms: {where}")
    print("spawn records (room: index id @ x,z):")
    for room in sorted({s["room"] for s in spawns}):
        line = "  ".join(f"{s['index']}:{s['id']}@{s['x']},{s['z']}" for s in spawns if s["room"] == room)
        print(f"  room {room:3d}: {line}")


def parse_enemy(spec):
    """'Ghoul@5,7', '3@5,7:2' -> {enemy, tile, facing?}."""
    who, _, where = spec.partition("@")
    tile, _, facing = where.partition(":")
    try:
        x, z = (int(v) for v in tile.split(","))
    except ValueError:
        raise SystemExit(f"--enemy {spec!r}: want NAME_OR_ID@X,Z[:FACING]")
    out = {"enemy": int(who) if who.isdigit() else who, "tile": [x, z]}
    if facing:
        out["facing"] = int(facing)
    return out


def resolve(enemies, spawns, room, want):
    """The enemy's id and a template record for it (bytes), or exit."""
    who = want["enemy"]
    if isinstance(who, int):
        if not 0 <= who < len(enemies):
            raise SystemExit(f"enemy {who}: this zone has ids 0-{len(enemies) - 1}")
        ids = [who]
    else:
        ids = [e["id"] for e in enemies if e["name"].lower() == who.lower()]
        if not ids:
            names = ", ".join(sorted({e["name"] for e in enemies}))
            raise SystemExit(f"enemy {who!r} is not in this zone ({names})")
    # A template: this room's record of the enemy, else another room's.
    for prefer_room in (True, False):
        for s in spawns:
            if s["id"] in ids and (s["room"] == room or not prefer_room):
                return s["id"], s
    raise SystemExit(f"enemy {who!r}: no room of this zone places it (scripted only), so no template record")


def build_records(enemies, spawns, room, wanted):
    records = []
    for want in wanted:
        enemy_id, template = resolve(enemies, spawns, room, want)
        r = bytearray(template["record"])
        r[0xC], r[0xE] = want["tile"]
        if "facing" in want:
            r[0x23] = want["facing"] & 3
        records.append(bytes(r))
        print(f"  {enemies[enemy_id]['name']} (id {enemy_id}) at {want['tile'][0]},{want['tile'][1]}"
              f" - template: room {template['room']} record {template['index']}")
    return records


def cmd_run(args):
    if args.preset:
        preset = json.loads(pathlib.Path(args.preset).read_text())
    else:
        if args.zone is None or args.room is None or not args.enemy:
            raise SystemExit("run needs --preset, or --zone, --room and at least one --enemy")
        preset = {"zone": args.zone, "room": args.room, "enemies": [parse_enemy(e) for e in args.enemy]}
        if args.seed is not None:
            preset["seed"] = args.seed
        if args.ashley:
            v = [int(n) for n in args.ashley.split(",")]
            preset["ashley"] = {"tile": v[:2], "facing": v[2] if len(v) > 2 else 0}
        if args.entry is not None:
            preset["entry"] = args.entry
        if args.profile:
            preset["profile"] = args.profile
        if args.stats:
            preset["stats"] = parse_stats(args.stats)
    if len(preset["enemies"]) > LAB_MAX:
        raise SystemExit(f"at most {LAB_MAX} enemies")
    if args.save:
        pathlib.Path(args.save).write_text(json.dumps(preset, indent=2) + "\n")
        print("saved", args.save)

    disc = open_disc(args.disc)
    enemies, spawns = zone_data(disc, preset["zone"])
    print(f"zone {preset['zone']} room {preset['room']}:")
    records = build_records(enemies, spawns, preset["room"], preset["enemies"])

    from rood_rpc import Game

    game = Game(args.rpc or os.environ.get("ROOD_RPC") or "tcp:47331")
    if preset.get("profile") or preset.get("stats"):
        setup = game.call("ashley", profile=preset.get("profile", ""), **preset.get("stats", {}))
        print("ashley:", json.dumps(setup))
        if not setup.get("applied"):
            raise SystemExit("Ashley's setup was not applied")
    before = game.call("encounter")
    arrival = (before.get("last") or {}).get("arrival", 0)
    params = {"zone": preset["zone"], "room": preset["room"], "records": [r.hex() for r in records]}
    if "seed" in preset:
        params["seed"] = preset["seed"]
    game.call("encounter", **params)
    goto = {"zone": preset["zone"], "room": preset["room"], "enemies": True, "scripts": False,
            "entry": preset.get("entry", 0)}
    if "ashley" in preset:
        x, z = preset["ashley"]["tile"]
        goto["cell"] = [x, preset["ashley"].get("level", 0), z, preset["ashley"].get("facing", 0)]
    print("room_goto:", json.dumps(game.call("room_goto", **goto))[:300])
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        status = game.call("encounter")
        last = status.get("last") or {}
        if last.get("arrival", 0) > arrival and status.get("ready"):
            print(json.dumps(status, indent=1))
            callo = game.call("state").get("callo") or {}
            her = callo.get("slot", 0) if callo.get("present") else 0
            slots = [i for i in range(16) if status["slots"] >> i & 1 and i != her]
            print("enemies in actor slots", slots, f"(Merlose: slot {her})" if her else "")
            for r in last.get("rejected", []):
                print(f"left out: enemy {r['enemy']} ({preset['enemies'][r['enemy']]}): {r['why']}")
            if args.trace:
                path = str(pathlib.Path(args.trace).resolve())
                note = f"zone {preset['zone']} room {preset['room']}" + (
                    f" seed {preset['seed']}" if "seed" in preset else "")
                print("trace:", json.dumps(game.call("trace", action="start", path=path,
                                                     frames=args.trace_frames, note=note)))
            return
        time.sleep(0.25)
    game.call("encounter", action="cancel")
    game.call("room_goto", action="cancel")
    raise SystemExit("the warp did not arrive in time (cancelled; is the game in the field, no menu or event?)")


STATS = ("max_hp", "max_mp", "strength", "intelligence", "agility")
PROFILES = ("post-prologue", "post-minotaur", "all-spells", "all-abilities", "all-break-arts")


def parse_stats(spec):
    out = {}
    for item in spec.split(","):
        key, _, value = item.partition("=")
        key = {"hp": "max_hp", "mp": "max_mp", "str": "strength", "int": "intelligence",
               "agi": "agility"}.get(key.strip(), key.strip())
        if key not in STATS or not value.strip().isdigit() or not 1 <= int(value) <= 999:
            raise SystemExit(f"--stats: {item!r} (keys {', '.join(STATS)}; values 1..999)")
        out[key] = int(value)
    return out


STATUS_NAMES = {}  # bit -> name, from vs_battle_statuses (146C.h) when the checkout is there


def status_names():
    if STATUS_NAMES:
        return STATUS_NAMES
    import re
    header = ROOT / "src/BATTLE/BATTLE.PRG/146C.h"
    if header.exists():
        for name, bit in re.findall(r"vsBattleStatus(\w+)\s*=\s*1\s*<<\s*(\d+)", header.read_text()):
            STATUS_NAMES[int(bit)] = name
    return STATUS_NAMES


def describe_statuses(mask):
    names = status_names()
    return ",".join(names.get(b, f"bit{b}") for b in range(32) if mask >> b & 1) or "none"


def cmd_trace(args):
    if args.what in ("stop", "status"):
        from rood_rpc import Game
        game = Game(args.rpc or os.environ.get("ROOD_RPC") or "tcp:47331")
        print(json.dumps(game.call("trace", **({"action": "stop"} if args.what == "stop" else {}))))
        return
    if not args.file:
        raise SystemExit("trace summary needs the trace file")
    try:
        table = text_table()
    except Exception:
        table = None
    names, prev, draws, actions, lines, action_names = {}, {}, 0, 0, 0, {}
    last_action = None
    first = end = None
    for raw in pathlib.Path(args.file).read_text().splitlines():
        rec = json.loads(raw)
        if rec.get("event") == "begin":
            print(f"trace begun at frame {rec['frame']} ({rec.get('note') or 'no note'}), rand state {rec['rng']}")
            continue
        if rec.get("event") == "end":
            end = rec
            continue
        lines += 1
        frame = rec["frame"]
        first = first if first is not None else frame
        draws += rec["draws"]
        game = rec["game"]
        act = game.get("action")
        key = act and (act["actor"], act["id"], act["chain"])
        if act and "name" in act:
            action_names[act["id"]] = decode(bytes.fromhex(act["name"]), table) if table else ""
        if key and key != last_action:
            actions += 1
            who = names.get(act["actor"], f"slot {act['actor']}")
            tgt = names.get(act["target"], f"slot {act['target']}") if act["target"] >= 0 else "-"
            what = action_names.get(act["id"]) or "attack"
            step = f", chain step {act['chain']}" if act["chain"] else ""
            print(f"{frame:>8}  {who}: {what} (action {act['id']}{step}) on {tgt}")
        last_action = key
        for a in game["actors"]:
            slot = a["slot"]
            if "name" in a:
                raw_name = bytes.fromhex(a["name"])
                names[slot] = decode(raw_name, table) if table else f"slot {slot}"
                print(f"{frame:>8}  slot {slot}: {names[slot]} HP {a['hp']}/{a['max_hp']} MP {a['mp']}/{a['max_mp']}"
                      f" STR {a['str']} INT {a['int']} AGI {a['agi']}")
            old = prev.get(slot)
            if old:
                who = names.get(slot, f"slot {slot}")
                for k, label in (("hp", "HP"), ("mp", "MP")):
                    if a[k] != old[k]:
                        print(f"{frame:>8}  {who} {label} {old[k]} -> {a[k]} ({a[k] - old[k]:+d})")
                if a["statuses"] != old["statuses"]:
                    print(f"{frame:>8}  {who} statuses: {describe_statuses(a['statuses'])}")
                if a["defeated"] and not old["defeated"]:
                    print(f"{frame:>8}  {who} defeated")
            prev[slot] = a
    if first is None:
        print("no frames recorded")
        return
    last_frame = end["frame"] if end else frame
    print(f"{lines} lines over frames {first}..{last_frame}; {actions} actions; {draws} random draws"
          + (f"; ended: {end['why']}" if end else "; no end line (still running, or the game closed)"))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--disc")
    p.add_argument("--rpc")
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("roster")
    r.add_argument("zone", type=int)
    r.set_defaults(func=cmd_roster)
    g = sub.add_parser("run")
    g.add_argument("--preset")
    g.add_argument("--zone", type=int)
    g.add_argument("--room", type=int)
    g.add_argument("--enemy", action="append", help="NAME_OR_ID@X,Z[:FACING]")
    g.add_argument("--seed", type=int)
    g.add_argument("--ashley", help="X,Z[,FACING]: Ashley's tile (else the room's first door)")
    g.add_argument("--entry", type=int, help="the door to arrive by (default 0)")
    g.add_argument("--save", help="write the preset to this file")
    g.add_argument("--timeout", type=float, default=60)
    g.add_argument("--profile", choices=PROFILES, help="give Ashley this test setup first")
    g.add_argument("--stats", help="max_hp=N,max_mp=N,strength=N,intelligence=N,agility=N (or hp, mp, str, int, agi)")
    g.add_argument("--trace", help="record the fight to this JSON-lines file from the spawn on")
    g.add_argument("--trace-frames", type=int, default=0, help="stop the trace after N frames (0: until trace stop)")
    g.set_defaults(func=cmd_run)
    t = sub.add_parser("trace")
    t.add_argument("what", choices=("stop", "status", "summary"))
    t.add_argument("file", nargs="?")
    t.set_defaults(func=cmd_trace)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
