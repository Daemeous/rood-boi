#!/usr/bin/env python3
"""An odd-angle sweep of analog movement through the game's rooms (analog_move; pc/src/analog_move.c).

    python3 pc/tests/analog_sweep_smoke.py [--card pc/save/card1.mcd] [--out DIR] [--rooms 9/4,50/2]

From a copy of the card (skipped without one), warped (the Room Browser's warp) to rooms with walls, doors, stairs and
slopes, ledges and climbs, crates and pushable blocks, the stick is held at 13, 37, 52 and 77 degrees from forward
and the same turned by 90, 180 and 270 degrees (16 headings) for 80 vsyncs from one marked moment in each. Each
hold must be:

- on the floor: her position stays inside the room's floor grid, her height changes by under 160 units a sample
  (a step or a slope, not a fall; a ledge's climb or hang excepted) and ends within 700 units of where she began
- not shaking: the way she moves along each axis does not reverse more than once in the last 40 vsyncs (a slide along a
  wall settles; a corner does not jitter) and her facing varies by under 5 degrees in the last 30
- not stuck without a wall: a room where fewer than half the headings moved her 8 units is a failure

Reports which rooms showed stairs or slopes (height changes), special animations (a climb, a hang, a jump) and how many
headings met a wall. Interaction's side at odd angles is analog_interact_smoke's (the same helper serves every site)."""
import argparse
import math
import sys

from analog_common import Checks, default_card, field_game, prepare, stick_at, warp

# (zone, room): a name for the report
ROOMS = [((9, 17), "save room: walls, crates"), ((9, 4), "Smokebarrel Stair: stairs"), ((50, 2), "The Crossing: open, walls"),
         ((53, 12), "The Dreamer's Climb: climbs"), ((40, 1), "Snowfly Forest: slopes, ledges"),
         ((9, 9), "The Reckoning Room: doors"), ((48, 21), "The Crumbling Market: crates")]
BASE = (13, 37, 52, 77)
HOLD, SAMPLE = 80, 10
WALK_ANIMS = {1, 13, 14, 15, 16, 19, 20}
CLIMB_ANIMS = {49, 50, 54, 55}  # a ledge's climb or hang: the height changes by design


def reversals(values):
    signs = [(v > 0) - (v < 0) for v in values if v]
    return sum(1 for a, b in zip(signs, signs[1:]) if a != b)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    parser.add_argument("--rooms", help="zone/room,... instead of the list")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "analog_sweep")
    if not ready:
        return 77
    out, card = ready
    rooms = ROOMS
    if args.rooms:
        rooms = [(tuple(int(n) for n in r.split("/")), r) for r in args.rooms.split(",")]
    check = Checks()
    game = field_game(out, card)
    try:
        sizes = {(r["zone"], r["room"]): r["floor"] for r in game.call("rooms")["rooms"]}
        game.call("settings", apply=["analog_move 1", "analog_dead_zone 20", "analog_pace 1", "analog_hysteresis 15"])
        for (zone, room), what in rooms:
            if not warp(game, zone, room):
                check(False, f"zone {zone} room {room}: warped")
                continue
            width, height = sizes[(zone, room)]
            game.mark()
            moved = walls = events = 0
            heights, specials = [], set()
            problems = []
            for turn in (0, 90, 180, 270):
                for base in BASE:
                    degrees = (base + turn) % 360
                    game.rewind()
                    track = []
                    for _ in range(HOLD // SAMPLE):
                        game.call("input", vsyncs=SAMPLE, left_stick=stick_at(degrees, 1.0))
                        game.run(SAMPLE)
                        p = game.state()["player"]
                        track.append((p["x"], p["y"], p["z"], p["facing"], game.value("vs_actors_actorRecords[0]->animationId")))
                    if any(math.hypot(b[0] - a[0], b[2] - a[2]) > 400 for a, b in zip(track, track[1:])):
                        events += 1  # (a scripted move or a room exit took her: not the stick's)
                        continue
                    specials |= {t[4] for t in track} - WALK_ANIMS
                    climbed = bool({t[4] for t in track} & CLIMB_ANIMS)
                    heights += [t[1] for t in track]
                    start_y = track[0][1]
                    inside = all(0 <= t[0] < width * 128 and 0 <= t[2] < height * 128 for t in track)
                    steps_y = [abs(b[1] - a[1]) for a, b in zip(track, track[1:])]
                    last = track[-4:]
                    dx = [b[0] - a[0] for a, b in zip(last, last[1:])]
                    dz = [b[2] - a[2] for a, b in zip(last, last[1:])]
                    facing = [t[3] for t in track[-3:]]
                    spread = max(abs(((f - facing[0] + 2048) % 4096) - 2048) for f in facing) * 360 / 4096
                    travelled = math.hypot(track[-1][0] - track[0][0], track[-1][2] - track[0][2])
                    if travelled >= 8:
                        moved += 1
                    if math.hypot(dx[-1], dz[-1]) == 0 and travelled < 8 + HOLD:
                        walls += 1
                    if not inside:
                        problems.append(f"{degrees}: left the floor grid ({track[-1][:3]})")
                    if not climbed and (max(steps_y, default=0) >= 160 or abs(track[-1][1] - start_y) > 700):
                        problems.append(f"{degrees}: height jumped ({[t[1] for t in track]})")
                    if reversals(dx) > 1 or reversals(dz) > 1:
                        problems.append(f"{degrees}: shaking at the end (dx {dx}, dz {dz})")
                    if spread > 5:
                        problems.append(f"{degrees}: facing wobbles {spread:.1f} degrees")
            ystep = (max(heights) - min(heights)) if heights else 0
            check(not problems, f"zone {zone} room {room} ({what}): 16 headings clean" + ("" if not problems else ": " + "; ".join(problems[:4])))
            check(moved >= 8 - events, f"zone {zone} room {room}: {moved} of 16 headings moved her")
            print(f"      zone {zone} room {room}: height range {ystep}, special animations {sorted(specials) or 'none'}, "
                  f"{walls} headings ended against a wall, {events} cut short by a scripted move", flush=True)
    finally:
        game.close()
    return check.finish("analog_sweep_smoke")


if __name__ == "__main__":
    sys.exit(main())
