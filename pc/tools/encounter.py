#!/usr/bin/env python3
"""Encounter lab: a chosen fight in a chosen room, for testing.

  encounter.py roster ZONE
      the zone's enemies (ZONEnnn.ZND, the ids to use) and where its rooms
      place them (each room's MPD enemy records: tile, the record's index)
  encounter.py run --zone Z --room R --enemy Ghoul@5,7 --enemy 3@6,8:2 [--seed N]
                   [--ashley X,Z[,FACING]] [--entry N] [--save preset.json]
  encounter.py run --preset preset.json
      warps to the room (the Room Browser's room_goto, its scripts off) and
      has the game spawn these enemies there instead of the room's own:
      name or id @ tile x,z [: facing 0-3]. Each enemy copies a real spawn
      record of the zone (its AI settings and drops), from this room if it
      has one. The game checks each tile on arrival and leaves out (never
      moves) an enemy whose tile is blocked or off the floor. A seed makes
      the game's random numbers repeat.

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
        if last.get("arrival", 0) > arrival and status.get("spawned"):
            print(json.dumps(status, indent=1))
            callo = game.call("state").get("callo") or {}
            her = callo.get("slot", 0) if callo.get("present") else 0
            slots = [i for i in range(16) if status["slots"] >> i & 1 and i != her]
            print("enemies in actor slots", slots, f"(Merlose: slot {her})" if her else "")
            for r in last.get("rejected", []):
                print(f"left out: enemy {r['enemy']} ({preset['enemies'][r['enemy']]}): {r['why']}")
            return
        time.sleep(0.25)
    game.call("encounter", action="cancel")
    game.call("room_goto", action="cancel")
    raise SystemExit("the warp did not arrive in time (cancelled; is the game in the field, no menu or event?)")


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
    g.set_defaults(func=cmd_run)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
