#!/usr/bin/env python3
"""enemy_free_angle (pc/src/analog_move.c pc_analog_step_remainder; moveActorWithCollision).

    python3 pc/tests/enemy_free_angle_smoke.py [--card pc/save/card1.mcd] [--out DIR]

The enemies' own logic is unchanged (their route, when they walk tile by tile or at a free angle, is the AI's); with the
setting on, each actor's step carries the fraction the engine truncates, so a free-angle step goes at its true heading.
From a copy of the card (skipped without one), warped to the Wine Cellar's Reckoning Room (zone 9 room 9) with its
enemies, from one marked moment:

- the setting is off by default
- off: an enemy walks (it moves at least 50 units in 300 vsyncs); the same moment again gives the same course
- on: the same enemy walks too, by a course that differs from the original's (the lost fractions are carried) but is
  as long to within 25% (the AI's pace is the same); the same moment again gives the same course (the carried
  fraction is game state)
- Ashley, standing still, is not moved by either"""
import argparse
import math
import sys

from analog_common import Checks, default_card, field_game, prepare

ROOM = (9, 9)
VSYNCS = 300


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "enemy_free_angle")
    if not ready:
        return 77
    out, card = ready
    check = Checks()
    game = field_game(out, card)
    try:
        def positions():
            r = {}
            for i in range(1, 17):
                try:
                    r[i] = (game.value(f"vs_actors_actorRecords[{i}]->unk0.position.vx"),
                            game.value(f"vs_actors_actorRecords[{i}]->unk0.position.vz"))
                except Exception:
                    pass
            return r

        def course():
            """From the marked moment: each enemy's positions every 20 vsyncs."""
            game.rewind()
            track = [positions()]
            for _ in range(VSYNCS // 20):
                game.run(20)
                track.append(positions())
            return track

        def length(track):
            return sum(math.hypot(b[i][0] - a[i][0], b[i][1] - a[i][1]) for a, b in zip(track, track[1:]) for i in a if i in b)

        check(game.call("settings", get="enemy_free_angle")["line"] == "enemy_free_angle 0", "enemy_free_angle is off by default")
        game.call("room_goto", zone=ROOM[0], room=ROOM[1], enemies=True, scripts=False)
        for _ in range(120):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        game.run(60)
        game.mark()
        ash = game.state()["player"]
        off1, off2 = course(), course()
        check(off1 == off2, "off: the same moment again gives the same course")
        check(length(off1) >= 50, f"off: the enemies walk ({length(off1):.0f} units)")
        game.call("settings", apply=["enemy_free_angle 1"])
        on1, on2 = course(), course()
        check(on1 == on2, "on: the same moment again gives the same course")
        check(length(on1) >= 50, f"on: the enemies walk ({length(on1):.0f} units)")
        check(on1 != off1, "on: the course is not the truncated one's")
        check(0.75 <= length(on1) / max(length(off1), 1) <= 1.25,
              f"on: as long as the original's within 25% ({length(on1):.0f} and {length(off1):.0f})")
        p = game.state()["player"]
        check((p["x"], p["z"]) == (ash["x"], ash["z"]), "Ashley, standing, is not moved")
    finally:
        game.close()
    return check.finish("enemy_free_angle_smoke")


if __name__ == "__main__":
    sys.exit(main())
