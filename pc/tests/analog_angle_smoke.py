#!/usr/bin/env python3
"""Analog movement at any angle, and its walls (setting analog_move; pc/src/analog_move.c).

    python3 pc/tests/analog_angle_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one), warped to the Abandoned Mines' Crossing (zone 48 room 21, the Undercity's Crumbling
Market: open to the south and east, its west wall behind) with the weapon sheathed, the stick held at odd angles (70, 110, 160, 200 degrees from forward):

- her facing settles on the stick's angle from the forward facing (within 3 degrees of the game's 12-bit angle)
- the way she goes (her displacement over three 12-vsync steps) is the way she faces, within 1.5 degrees at the
  walk (two paces) and at the run, 3 at a slow continuous speed (3 units a frame): the engine truncates each frame's
  step to whole units, which bent the heading by up to 4 degrees; the lost fraction is carried from frame to
  frame (pc_analog_step_remainder). Not one of eight directions. The same with analog_move 0 and the D-pad
  at its diagonal: the original's steps, unchanged (the carried fraction is only under analog movement)
- at the room's far wall the stick at 20 degrees off the wall's normal keeps the wall's parallel part of the
  step and slides along it, x unchanged, z still moving (the terrain probe keeps the axis that is free)
- screenshots (--out) of the heading at 20 and 70 degrees and of the slide, with a contact sheet"""
import argparse
import math
import subprocess
import sys

from analog_common import Checks, default_card, field_game, prepare, stick_at, warp, wrap

STEP = 8
ROOM = (48, 21)
ANGLES = (70, 110, 160, 200)  # from forward (the camera's -x): all open ground in this room


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    ready = prepare(args.card, args.out, "analog_angle")
    if not ready:
        return 77
    out, card = ready
    check = Checks()
    game = field_game(out, card)
    try:
        def pos():
            p = game.state()["player"]
            return p["x"], p["z"], p["facing"]

        def walk(degrees, steps, travel):
            """From the marked moment, the stick held: her position after each 12-vsync step (the first 24 vsyncs
            are her turn: the track starts after them)."""
            game.rewind()
            game.call("input", vsyncs=20, left_stick=stick_at(degrees, travel))
            game.run(20)
            track = [pos()]
            for _ in range(steps):
                game.call("input", vsyncs=STEP, left_stick=stick_at(degrees, travel))
                game.run(STEP)
                track.append(pos())
            return track

        def course(a, b):
            """The way (x, z) moved, as the game's 12-bit heading of that displacement: the facing's own measure."""
            # position += (rcos(0xC00 - h), rsin(0xC00 - h)) * speed: h = 0xC00 - atan2(dz, dx)
            return (3072 - round(math.atan2(b[1] - a[1], b[0] - a[0]) * 4096 / (2 * math.pi))) & 0xFFF

        check(warp(game, *ROOM), f"warped to zone {ROOM[0]} room {ROOM[1]}")
        game.call("settings", apply=["analog_move 1", "analog_dead_zone 20", "analog_run 70", "analog_pace 0"])
        game.run(30)
        game.mark()  # (every trial starts from this moment)

        # The forward facing (the stick at 0), then the odd angles from it.
        walk(0, 1, 1.0)
        base_f = pos()[2]
        # (pace, travel, tolerance in degrees): the walk of two paces, the run, a slow continuous speed.
        for pace, travel, tol in ((0, 0.5, 1.5), (0, 1.0, 1.5), (1, 0.3, 3.0)):
            game.call("settings", apply=[f"analog_pace {pace}"])
            game.run(10)
            tracks, settled = {}, {}
            for degrees in ANGLES:
                tracks[degrees] = walk(degrees, 3, travel)
                game.run(40)
                f = settled[degrees] = pos()[2]
                want = (base_f + round(degrees * 4096 / 360)) & 0xFFF
                other = (base_f - round(degrees * 4096 / 360)) & 0xFFF
                ferr = min(abs(wrap(f - want)), abs(wrap(f - other))) * 360 / 4096
                check(ferr <= 3, f"pace {pace}, travel {travel}, stick {degrees}: she faces {wrap(f - base_f) * 360 / 4096:.1f} "
                      f"degrees from forward")
            lengths = {d: math.hypot(t[3][0] - t[0][0], t[3][1] - t[0][1]) for d, t in tracks.items()}
            free = [d for d, n in lengths.items() if n >= 0.97 * max(lengths.values())]  # (the others met a wall)
            check(len(free) >= 2, f"pace {pace}, travel {travel}: {len(free)} of {len(ANGLES)} angles had open ground "
                  f"({', '.join(f'{d}: {n:.0f}' for d, n in lengths.items())} units)")
            for degrees in free:
                track = tracks[degrees]
                err = wrap(course(track[0], track[3]) - settled[degrees], 4096) * 360 / 4096
                check(abs(err) <= tol, f"pace {pace}, travel {travel}, stick {degrees}: she goes {err:+.1f} degrees "
                      f"off the way she faces ({track[0][:2]} to {track[3][:2]})")

        # The original's eight directions, unchanged: analog_move 0, the D-pad's diagonal goes between its two
        # directions (45 degrees from each, whatever way the card's camera looks: where the camera looks the
        # diagonal may run along one axis).
        game.call("settings", apply=["analog_move 0"])
        game.run(10)

        def dpad(buttons):
            game.rewind()
            game.call("input", buttons=buttons, vsyncs=36)
            game.run(36)
            return pos()[2]  # the facing: the heading the pad's vector gave (walls may stop the walk, not the turn)

        down, right, diag = dpad(["down"]), dpad(["right"]), dpad(["down", "right"])
        apart = abs(wrap(right - down, 4096)) * 360 / 4096
        off = abs(wrap(2 * diag - down - right, 4096)) * 360 / 8192  # from the middle of the two (either way round)
        off = min(off, 180 - off)
        check(60 <= apart <= 120 and off <= 8, f"the D-pad diagonal heads between down and right: facings {down}, {right}, "
              f"{diag} ({apart:.0f} degrees apart, {off:.1f} off the middle)")
        game.call("settings", apply=["analog_move 1", "analog_pace 0"])
        game.run(10)

        # The heading at 20 and 70 degrees, while she walks (the game paused on the frame).
        for degrees in (20, 70):
            game.rewind()
            game.call("input", vsyncs=60, left_stick=stick_at(degrees, 0.5))
            game.run(40)
            game.present_screenshot(out / f"heading_{degrees:03d}.png")

        # Into the west wall at 20 degrees off its normal (forward, the camera's, is toward -x): she keeps the wall's
        # parallel part of the step and slides along it. One of the two sides has room along the wall.
        slid = None
        game.rewind()
        game.call("input", vsyncs=40, left_stick=stick_at(160, 1.0))  # (out into the room's middle first)
        game.run(50)
        game.mark()
        for degrees in (20, 340, 10, 350):
            game.rewind()
            track = [pos()]
            for _ in range(60):
                game.call("input", vsyncs=STEP, left_stick=stick_at(degrees, 1.0))
                game.run(STEP)
                track.append(pos())
                if len(track) > 5 and len({p[0] for p in track[-4:]}) == 1:
                    break
            slide = []
            for _ in range(4):  # along the wall for four more steps: x stays, z moves
                game.call("input", vsyncs=STEP, left_stick=stick_at(degrees, 1.0))
                game.run(STEP)
                slide.append(pos())
            xs = [p[0] for p in track[-4:] + slide]
            zs = [p[1] for p in slide]
            if len(set(xs)) == 1 and zs[-1] != zs[0]:
                slid = degrees
                break
        check(slid is not None, f"the stick at 20 degrees off the wall's normal slides along it (x unchanged, z moving: {slid})")
        game.present_screenshot(out / "slide.png")
        shots = [out / n for n in ("heading_020.png", "heading_070.png", "slide.png")]
        if all(s.exists() for s in shots):
            subprocess.run(["montage", *map(str, shots), "-tile", "3x1", "-geometry", "+4+4",
                            str(out / "contact_sheet.png")], check=False, stderr=subprocess.DEVNULL)
    finally:
        game.close()
    return check.finish("analog_angle_smoke")


if __name__ == "__main__":
    sys.exit(main())
