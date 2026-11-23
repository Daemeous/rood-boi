#!/usr/bin/env python3
"""A stick pressed into stair-stepped walls does not ratchet (analog_move; vs_actors_moveActorWithCollision.zig).

    python3 pc/tests/analog_corner_smoke.py [--card pc/save/card1.mcd] [--out DIR]

The original's wall contacts push an actor off a wall by 8 to 16 units a frame. A stick's slide along a stair-stepped
wall into a notch is only 2 or 3 units a frame, so the push carried her back out and she ran in again: a four-frame
ratchet. A push that reverses what the step asked on an axis now stops her at the contact. From a copy of the card
(skipped without one), warped to Snowfly Forest (zone 40 room 1) and three other rooms with walls, the stick is held at
odd headings for 120 game frames from one marked moment, and the way she moves along each axis, a game frame at a time,
must not reverse more than once (she progresses along the wall or rests in the corner); Snowfly Forest's headings
290 to 320 degrees from forward are the ones that ratcheted."""
import argparse
import sys

from analog_common import Checks, default_card, field_game, prepare, stick_at, warp

ROOMS = [((40, 1), (290, 295, 300, 305, 307, 310, 315, 320), "Snowfly Forest: stair-stepped walls"),
         ((50, 2), (23, 113, 203, 293), "The Crossing: open, walls"),
         ((9, 4), (37, 127, 217, 307), "Smokebarrel Stair: stairs"),
         ((9, 9), (52, 142, 232, 322), "The Reckoning Room: doors")]
FRAMES = 120


def reversals(values):
    signs = [(v > 0) - (v < 0) for v in values if v]
    return sum(1 for a, b in zip(signs, signs[1:]) if a != b)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "analog_corner")
    if not ready:
        return 77
    out, card = ready
    check = Checks()
    game = field_game(out, card)
    try:
        game.call("settings", apply=["analog_move 1", "analog_dead_zone 20", "analog_pace 1", "analog_hysteresis 15"])
        for (zone, room), headings, what in ROOMS:
            if not warp(game, zone, room):
                check(False, f"zone {zone} room {room}: warped")
                continue
            game.mark()
            bad = []
            for degrees in headings:
                game.rewind()
                game.call("input", vsyncs=2 * FRAMES, left_stick=stick_at(degrees, 1.0))
                track = []
                for _ in range(FRAMES):
                    game.run(2)
                    p = game.state()["player"]
                    track.append((p["x"], p["z"]))
                if any(abs(b[0] - a[0]) + abs(b[1] - a[1]) > 200 for a, b in zip(track, track[1:])):
                    continue  # a room exit or scripted move
                tail = track[10:]
                dx = [b[0] - a[0] for a, b in zip(tail, tail[1:])]
                dz = [b[1] - a[1] for a, b in zip(tail, tail[1:])]
                if reversals(dx) > 1 or reversals(dz) > 1:
                    bad.append(f"{degrees}: x reverses {reversals(dx)}, z {reversals(dz)}")
            check(not bad, f"zone {zone} room {room} ({what}): {len(headings)} headings without back-and-forth"
                  + ("" if not bad else ": " + "; ".join(bad)))
    finally:
        game.close()
    return check.finish("analog_corner_smoke")


if __name__ == "__main__":
    sys.exit(main())
