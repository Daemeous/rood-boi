#!/usr/bin/env python3
"""Ashley turns in the same time at a light push as at a full one (analog_move; vs_actors_updateAndDrawRoomModels.zig).

    python3 pc/tests/analog_turn_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Under analog movement her stride plays at the pace of her speed, so at a light push the pose steps only every few game
frames. Only the animation's frame is gated by that pace: the facing's 12-frame turn (and the eased scale and tweened
moves) run every frame, as in the original. From a copy of the card (skipped without one), warped to the Crumbling
Market, the stick turned 90 degrees from where she faces and held at 100%, 60% and 30% of its travel:

- the vsyncs until her facing is within 3 degrees of where it settles are the same at the three pressures, within 3
  vsyncs (the turn is 12 game frames, 24 vsyncs)
- she settles at the same facing at the three pressures
- deterministic: the same hold again gives the same facings"""
import argparse
import sys

from analog_common import Checks, default_card, field_game, prepare, stick_at, warp, wrap

ROOM = (48, 21)
HOLD = 60


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "analog_turn")
    if not ready:
        return 77
    out, card = ready
    check = Checks()
    game = field_game(out, card)
    try:
        def turn(travel):
            """Her facing at each vsync of the hold, from the marked moment."""
            game.rewind()
            game.call("input", vsyncs=HOLD, left_stick=stick_at(90, travel))
            facings = []
            for _ in range(HOLD):
                game.run(1)
                facings.append(game.state()["player"]["facing"])
            return facings

        def settle_time(facings):
            final = facings[-1]
            return next(i for i in range(HOLD) if all(abs(wrap(f - final)) * 360 / 4096 <= 3 for f in facings[i:]))

        check(warp(game, *ROOM), f"warped to zone {ROOM[0]} room {ROOM[1]}")
        game.call("settings", apply=["analog_move 1", "analog_dead_zone 20", "analog_pace 1", "analog_anim_rate 100"])
        game.run(30)
        game.mark()
        runs = {t: turn(t) for t in (1.0, 0.6, 0.3)}
        times = {t: settle_time(f) for t, f in runs.items()}
        finals = {t: f[-1] for t, f in runs.items()}
        print("settle vsyncs", times, "final facings", finals, flush=True)
        check(max(times.values()) - min(times.values()) <= 3,
              f"the turn takes the same time at 100%, 60% and 30% of the stick ({times[1.0]}, {times[0.6]}, {times[0.3]} vsyncs)")
        check(max(abs(wrap(finals[t] - finals[1.0])) for t in finals) * 360 / 4096 <= 3,
              f"and ends at the same facing ({finals[1.0]}, {finals[0.6]}, {finals[0.3]})")
        check(turn(0.3) == runs[0.3], "the same hold again gives the same facings")
    finally:
        game.close()
    return check.finish("analog_turn_smoke")


if __name__ == "__main__":
    sys.exit(main())
