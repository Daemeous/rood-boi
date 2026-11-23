#!/usr/bin/env python3
"""Interaction's tile direction under analog movement (analog_hysteresis; pc/src/analog_move.c pc_analog_cardinal).

    python3 pc/tests/analog_interact_smoke.py [--card pc/save/card1.mcd] [--out DIR]

The game reads the nearest of the four tile directions from Ashley's facing (chests, doors, blocks, the one-tile
step lock: vs_field_gimmickInteractionDirection, which Cross's lookup writes). From a copy of the card, in the save
room, the stick used last (so the hysteresis applies) and her facing set on the 45 degree boundary by hand:

- hysteresis 15: from a facing at a cardinal, 50 degrees round (5 past the boundary) the direction is the same
  (the original's rounding would change it); 70 degrees round it changes; back at 50 degrees it stays on the new
  one; 40 degrees round it still stays (the original's rounding would be the first) -- a facing on the boundary keeps
  its side, whichever way it came
- hysteresis 0: the original's rounding at all of those
- the D-pad used last (a button, after the stick): the original's rounding, whatever the stick chose before
- analog_move 0: the original's rounding"""
import argparse
import sys

from analog_common import Checks, default_card, field_game, prepare, stick_at


def classic(facing):
    return ((facing + 0x200) & 0xFFF) // 0x400


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "analog_interact")
    if not ready:
        return 77
    out, card = ready
    check = Checks()
    game = field_game(out, card)
    try:
        facing = game.field("vs_actors_actorRecords[0]->unk0.facing")

        def set_facing(angle):
            game.write(facing.address, (angle & 0xFFF).to_bytes(2, "little"))

        def side(angle):
            """Cross's lookup at her facing `angle`: the tile direction the game chose."""
            set_facing(angle)
            game.press("cross", vsyncs=2, settle=6)
            set_facing(angle)  # (a turn the press started)
            return game.value("vs_field_gimmickInteractionDirection")

        def stick_used():
            game.call("input", vsyncs=3, left_stick=stick_at(0, 1.0))
            game.run(40)  # her stride and turn over

        def deg(d):
            return round(d * 4096 / 360)

        game.call("settings", apply=["analog_move 1", "analog_hysteresis 15"])
        game.run(10)
        game.mark()
        base = 0x400  # a cardinal (the facing of 90 degrees)
        for hyst, expect_hold in ((15, True), (0, False)):
            game.rewind()
            game.call("settings", apply=[f"analog_hysteresis {hyst}"])
            stick_used()
            a = side(base)
            check(a == classic(base), f"hysteresis {hyst}: on the cardinal it is {a} (the original's {classic(base)})")
            b = side(base + deg(50))
            want = a if expect_hold else classic(base + deg(50))
            check(b == want, f"hysteresis {hyst}: 50 degrees round it is {b} (want {want}; the original's rounding "
                  f"{classic(base + deg(50))})")
            c = side(base + deg(70))
            check(c == classic(base + deg(70)), f"hysteresis {hyst}: 70 degrees round it is {c} "
                  f"(want {classic(base + deg(70))})")
            d = side(base + deg(50))
            want = c if expect_hold else classic(base + deg(50))
            check(d == want, f"hysteresis {hyst}: back at 50 degrees it is {d} (want {want})")
            e = side(base + deg(40))
            want = c if expect_hold else classic(base + deg(40))
            check(e == want, f"hysteresis {hyst}: 40 degrees round it is {e} (want {want}; the original's rounding "
                  f"{classic(base + deg(40))})")
            if expect_hold:
                check(len({a, c}) == 2, "the two sides differ (the facing crossed the boundary)")

        # The D-pad used last: the original's rounding.
        game.rewind()
        game.call("settings", apply=["analog_hysteresis 15"])
        stick_used()
        a = side(base)
        game.call("input", buttons=["up"], vsyncs=3)
        game.run(40)
        b = side(base + deg(50))
        check(a != b and b == classic(base + deg(50)), f"D-pad last: 50 degrees round it is {b}, the original's "
              f"{classic(base + deg(50))}")

        # analog_move 0: the original's rounding.
        game.rewind()
        game.call("settings", apply=["analog_move 0", "analog_hysteresis 15"])
        game.run(5)
        a = side(base)
        b = side(base + deg(50))
        check(b == classic(base + deg(50)), f"analog_move 0: 50 degrees round it is {b} (the original's)")
    finally:
        game.close()
    return check.finish("analog_interact_smoke")


if __name__ == "__main__":
    sys.exit(main())
