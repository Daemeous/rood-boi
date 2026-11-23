#!/usr/bin/env python3
"""Analog speed by pressure and the stride that keeps pace (analog_pace, analog_curve, analog_min_speed,
analog_anim_rate; pc/src/analog_move.c).

    python3 pc/tests/analog_pace_smoke.py [--card pc/save/card1.mcd] [--out DIR]

The original plays ONE stride cycle (animations 13 and 14, a foot each, 13 game frames) at the D-pad's run and at R2's
walk alike (the run cycle's speed test, 19 and 20, never passes for Ashley), so the feet slide at the walk. From a copy
of the card (skipped without one), warped to the Undercity's Crumbling Market (zone 48 room 21: open ground to the east),
with the weapon sheathed, the stick held at 30%, 60% and 100% of its travel for 20 vsyncs after her turn:

- speed (her displacement): rises with the pressure and follows the curve (dead zone 20%, analog_min_speed 10%,
  analog_curve 100: speed / run = 0.10 + 0.90 * (travel - 0.2) / 0.8), within 10%
- the stride (pose changes: the animation's frame advancing, in whole game frames with a carried fraction): its rate
  follows the speed, within one change of 40 vsyncs, and the distance she covers per pose change is the same at
  the three pressures within 25% (the feet do not slide)
- analog_anim_rate 0: the original's fixed playback at every speed (the same pose changes at all three, the distance
  per change differing by the speed)
- analog_pace 0: the original's two paces: 0.5 travel is R2's walk (half the run), 0.9 the run; the stride's rate
  is the same at both
- analog_curve 200 makes 60% slower than 100; analog_min_speed 30 makes 25% travel faster than 10
- deterministic: the same hold again gives the same positions and poses (the stride's fraction is game state)
- shots of 30%, 60% and 100% (--out) at the same moment, with a contact sheet"""
import argparse
import math
import subprocess
import sys

from analog_common import Checks, default_card, field_game, prepare, stick_at, warp

ROOM = (48, 21)
HEADING = 160  # from forward: open ground in this room (all the way east)
HOLD = 40
RUN_UNITS = 9.0  # the run, units a vsync (the stat's 18 a game frame)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "analog_pace")
    if not ready:
        return 77
    out, card = ready
    check = Checks()
    game = field_game(out, card)
    try:
        def pose():
            return (game.value("vs_actors_actorRecords[0]->animationId"), game.value("vs_actors_actorRecords[0]->unkC54.unk540"))

        def hold(travel, shot=None):
            """From the marked moment: (units covered, pose changes, the positions) over HOLD vsyncs after her turn."""
            game.rewind()
            game.call("input", vsyncs=16, left_stick=stick_at(HEADING, travel))
            game.run(16)
            p = game.state()["player"]
            a = (p["x"], p["z"])
            poses = [pose()]
            for _ in range(HOLD):
                game.call("input", vsyncs=1, left_stick=stick_at(HEADING, travel))
                game.run(1)
                poses.append(pose())
            p = game.state()["player"]
            b = (p["x"], p["z"])
            if shot:
                game.present_screenshot(out / shot)
            changes = sum(1 for x, y in zip(poses, poses[1:]) if x != y)
            return math.hypot(b[0] - a[0], b[1] - a[1]) / HOLD, changes, (a, b, poses)

        check(warp(game, *ROOM), f"warped to zone {ROOM[0]} room {ROOM[1]}")
        game.call("settings", apply=["analog_move 1", "analog_dead_zone 20", "analog_pace 1", "analog_curve 100",
                                     "analog_min_speed 10", "analog_anim_rate 100"])
        game.run(30)
        game.mark()

        run_speed, run_changes, _ = hold(1.0)
        check(run_speed > 0.8 * RUN_UNITS, f"the full push runs: {run_speed:.1f} units a vsync")
        check(run_changes >= HOLD // 2 - 1, f"and her stride plays at the original's rate: {run_changes} pose changes in {HOLD} vsyncs")

        shots = []
        results = {}
        for travel in (0.3, 0.6, 1.0):
            name = f"stick_{round(travel * 100):03d}.png"
            speed, changes, _ = hold(travel, shot=name)
            shots.append(out / name)
            results[travel] = (speed, changes)
            t = (travel - 0.2) / 0.8
            want = 0.10 + 0.90 * t
            got = speed / run_speed
            check(abs(got - want) <= 0.10, f"travel {travel}: speed {got:.2f} of the run (the curve's {want:.2f})")
            expect_changes = run_changes * want
            check(abs(changes - expect_changes) <= 1.5, f"travel {travel}: {changes} pose changes in {HOLD} vsyncs "
                  f"(the speed's {expect_changes:.1f})")
        speeds = [results[t][0] for t in (0.3, 0.6, 1.0)]
        check(speeds[0] < speeds[1] < speeds[2], f"speed rises with the pressure ({', '.join(f'{s:.1f}' for s in speeds)})")
        strides = [results[t][0] / max(results[t][1], 1) for t in (0.3, 0.6, 1.0)]
        check(max(strides) <= 1.25 * min(strides), "the distance per pose change is the same at 30%, 60% and 100% "
              f"within 25% ({', '.join(f'{s:.2f}' for s in strides)}: the feet keep pace)")

        # The original's fixed playback: the feet slide.
        game.call("settings", apply=["analog_anim_rate 0"])
        fixed = {t: hold(t)[:2] for t in (0.3, 0.6, 1.0)}
        check(len({c for _, c in fixed.values()}) == 1 and fixed[0.6][1] == run_changes,
              f"analog_anim_rate 0: the same {fixed[0.6][1]} pose changes at every speed ({[c for _, c in fixed.values()]})")
        fixed_strides = [fixed[t][0] / max(fixed[t][1], 1) for t in (0.3, 0.6, 1.0)]
        check(max(fixed_strides) > 2 * min(fixed_strides), "and the distance per pose change differs by the speed "
              f"({', '.join(f'{s:.2f}' for s in fixed_strides)}: the feet slide)")
        game.call("settings", apply=["analog_anim_rate 100"])

        # The original's two paces: a walk, a run, one stride rate.
        game.call("settings", apply=["analog_pace 0", "analog_run 70"])
        walk_speed, walk_changes, _ = hold(0.5)
        fast_speed, fast_changes, _ = hold(0.9)
        check(abs(walk_speed / fast_speed - 0.5) <= 0.1, f"analog_pace 0: 0.5 walks, 0.9 runs ({walk_speed:.1f} and {fast_speed:.1f})")
        check(walk_changes == fast_changes == run_changes, f"analog_pace 0: the stride's rate is the original's ({walk_changes}, {fast_changes})")
        game.call("settings", apply=["analog_pace 1"])

        # The curve and the floor.
        base = hold(0.6)[0]
        game.call("settings", apply=["analog_curve 200"])
        check(hold(0.6)[0] < 0.85 * base, "analog_curve 200: the same push is slower")
        game.call("settings", apply=["analog_curve 100", "analog_min_speed 10"])
        low = hold(0.25)[0]
        game.call("settings", apply=["analog_min_speed 30"])
        check(hold(0.25)[0] > 1.4 * low, "analog_min_speed 30: a light push is faster")
        game.call("settings", apply=["analog_min_speed 10"])

        # Deterministic: the same hold, the same strides.
        first = hold(0.6)
        second = hold(0.6)
        check(first[2] == second[2], "the same hold again gives the same positions and poses")

        if all(s.exists() for s in shots):
            subprocess.run(["montage", *map(str, shots), "-tile", "3x1", "-geometry", "+4+4",
                            str(out / "contact_sheet.png")], check=False, stderr=subprocess.DEVNULL)
    finally:
        game.close()
    return check.finish("analog_pace_smoke")


if __name__ == "__main__":
    sys.exit(main())
