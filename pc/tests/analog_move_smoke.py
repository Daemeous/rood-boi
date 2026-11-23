#!/usr/bin/env python3
"""Analog movement (settings analog_move, analog_dead_zone, analog_run;
pc/src/analog_move.c) and rumble (the game's actuator bytes to the gamepad's
motors, pc/src/boot_gamepad.c), both through the RPC.

    python3 pc/tests/analog_move_smoke.py [--card pc/save/card1.mcd] [--out DIR]

From a copy of the card (skipped without one), in its save room with the
weapon sheathed. Ashley's facing follows her heading, so it is the heading's
measure (her position is also stopped by the walls of a small room):

- off (analog_move 0; on is the default for a gamepad's stick): the left stick (RPC input {left_stick}) does not turn or
  move her; the D-pad turns her to the camera-relative cardinal facings and
  moves her, with R2 faster
- on (with analog_pace 0, the original's two paces; the continuous speed is analog_pace_smoke's): the stick at 0, 30, 45, 60, 90, 180 and 270 degrees from forward faces
  her at the D-pad-forward facing plus that angle (within 3 degrees of the
  game's own 12-bit angle), at any deflection past the dead zone;
  inside the dead zone nothing changes; the D-pad still gives the same facings
  and speeds
- speed (from one marked moment, rewound each time, so a pace is exactly the
  D-pad's or not): below analog_run she walks at the pace R2 held gives (the
  original has two: the D-pad's is the run), from it she runs at the D-pad's;
  R2 still walks; analog_run is a tunable (40 makes half deflection run); a larger
  dead zone makes the same deflection do nothing
- RPC settings: the three keys read back; off again returns to the D-pad's
  behaviour
- rumble: the actuator bytes the game keeps (_portInfo[0].actData, written
  each frame by vs_main_setVibrateParams in battle) reach the host rumble
  call: small motor on (any nonzero byte) is the high-frequency motor at full,
  the large motor 0..255 is the low-frequency one scaled to 16 bits;
  rumble_strength scales both (0 sends nothing); zero bytes stop both. The
  pad is DualShock-shaped (an extended ID), so the small motor's byte is its
  on/off."""
import argparse
import math
import pathlib
import shutil
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402


def wrap(d):
    return (d + 2048) % 4096 - 2048


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "analog_move"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = launch_at("save_room", card=card, fast=True, log=out / "game.log",
                     extra_env={"ROOD_WINDOW": "960x720", "ROOD_GI": "0"})
    try:

        def settings(*lines):
            game.call("settings", apply=list(lines))

        def pos():
            p = game.state()["player"]
            return p["x"], p["z"], p["facing"]

        def hold(vsyncs=60, **kw):
            """Hold an input: her facing once settled."""
            game.call("input", vsyncs=vsyncs, **kw)
            game.run(vsyncs + 10)
            return pos()[2]

        def idle(**kw):
            """Hold an input she should ignore: whether her position and facing are unchanged."""
            game.run(40)  # whatever she was doing (a turn, a stride) is over
            before = pos()
            hold(**kw)
            return pos() == before

        def trial(direction, kind, travel=1.0):
            """From the marked moment (turned to `direction`), the input held 20 vsyncs: how far she went."""
            game.rewind()
            kw = {}
            if kind.startswith("dpad"):
                kw["buttons"] = [direction]
            else:
                kw["left_stick"] = stick_at(0 if direction == "up" else 180, travel)
            if kind.endswith("r2"):
                kw["buttons"] = kw.get("buttons", []) + ["r2"]
            a = pos()
            game.call("input", vsyncs=20, **kw)
            game.run(20)
            b = pos()
            return b[0] - a[0], b[1] - a[1]

        def same(a, b):
            """The same pace: the stick's steps carry their lost fraction (the D-pad's truncate), so the same
            distance within 15% (the walk loses most to the truncation) and the same way within 3 degrees, not the same whole units."""
            da, db = math.hypot(*a), math.hypot(*b)
            turn = abs(math.degrees(math.atan2(a[1], a[0]) - math.atan2(b[1], b[0])))
            return db > 0 and abs(da / db - 1) <= 0.15 and min(turn, 360 - turn) <= 3

        game.run(300)
        for _ in range(2):  # leave battle mode: the weapon sheathed
            game.press("cross", vsyncs=3, settle=30)
        check(game.state().get("player") is not None, "in the field with Ashley")

        def stick_at(degrees, travel):
            r = math.radians(degrees)  # from forward (up) toward the right
            return [travel * math.sin(r), -travel * math.cos(r)]

        # ---- off: the stick is the D-pad's, nothing more ----
        check(game.call("settings", get="analog_move")["line"] == "analog_move 1", "analog_move is on by default")
        check(game.call("settings", get="analog_pace")["line"] == "analog_pace 1", "analog_pace is 1 by default")
        settings("analog_move 0", "analog_pace 0")  # (the original's two paces: the speeds below are the D-pad's and R2's)
        base_f = hold(buttons=["up"])
        check(idle(left_stick=stick_at(90, 1.0)), "off: the stick at 1.0 does not turn or move her")
        right_f = hold(buttons=["right"])
        check(abs(wrap(right_f - base_f - 1024)) <= 4, f"off: D-pad Right faces 90 degrees from Up ({wrap(right_f - base_f)})")

        # ---- on: the stick's own angle ----
        settings("analog_move 1")
        check(game.call("settings", get="analog_move")["line"] == "analog_move 1", "analog_move 1 applies")
        for degrees in (0, 30, 45, 60, 90, 180, 270, 330):
            for travel in (1.0, 0.5):
                f = hold(left_stick=stick_at(degrees, travel))
                want = degrees * 4096 / 360
                err = wrap(f - base_f - round(want)) * 360 / 4096
                check(abs(err) <= 3, f"on: the stick at {degrees} degrees, travel {travel}: facing "
                                     f"{wrap(f - base_f) * 360 / 4096:.1f} degrees from forward (error {err:.1f})")
        check(idle(left_stick=stick_at(120, 0.1)), "on: travel 0.1 is inside the dead zone: nothing")
        f = hold(buttons=["right"])
        check(abs(wrap(f - base_f - 1024)) <= 4, "on: the D-pad still faces Right as before")

        # ---- speeds: forward and back (the room is small), the D-pad's walk and R2's run as the measure ----
        # The same moment (mark, rewind) each time, so a pace is exactly the D-pad's or it is not.
        hold(24, buttons=["down"])
        game.mark()
        run, walk = trial("down", "dpad"), trial("down", "dpad+r2")  # the original: R2 held is the slower pace
        dr, dw = math.hypot(*run), math.hypot(*walk)
        check(dw > 0 and dr > 1.5 * dw, f"the D-pad's run and R2's walk: {dr:.0f} and {dw:.0f} units in 20 vsyncs")
        check(same(trial("down", "stick", 0.5), walk), "on: half travel walks as D-pad + R2 does")
        check(same(trial("down", "stick", 1.0), run), "on: full travel runs as the D-pad does")
        check(same(trial("down", "stick", 0.69), walk), "on: travel 0.69 (just under analog_run 70) walks")
        check(same(trial("down", "stick", 0.71), run), "on: travel 0.71 runs")
        check(same(trial("down", "stick+r2", 1.0), walk), "on: R2 held with full travel still walks")
        check(trial("down", "dpad") == run and trial("down", "dpad+r2") == walk, "on: the D-pad's paces are unchanged")
        settings("analog_run 40")
        check(same(trial("down", "stick", 0.5), run), "analog_run 40: half travel runs")
        check(same(trial("down", "stick", 0.3), walk), "analog_run 40: travel 0.3 walks")
        settings("analog_run 70", "analog_dead_zone 60")
        check(idle(left_stick=stick_at(120, 0.5)), "analog_dead_zone 60: travel 0.5 does nothing")
        settings("analog_dead_zone 20")
        f = hold(left_stick=stick_at(120, 0.5))
        check(abs(wrap(f - base_f - round(120 * 4096 / 360))) < 12, "analog_dead_zone 20: travel 0.5 turns her again")

        # ---- off again ----
        settings("analog_move 0")
        check(idle(left_stick=stick_at(150, 1.0)), "off again: the stick does nothing")

        # ---- rumble ----
        act = "_portInfo[0]"  # (the pad port: the actuator bytes sit 8 on)
        check(game.value("_portInfo[0].exId") != 0, "the pad has an extended ID (the small motor's byte is its on/off)")

        def rumble(small, large):
            """The game rewrites its actuator bytes every frame in the field (vs_main_setVibrateParams: no
            script, so zero), so paused: the bytes are written and one vsync stepped, which reads them."""
            game.pause()
            game.store_data(act, bytes([small, large]), offset=8)
            game.run(1)
            return game.call("rumble")

        game.call("rumble", reset=True)
        r = rumble(0, 0)
        check((r["low"], r["high"]) == (0, 0), f"actuator bytes 0, 0: both motors off ({r})")
        r = rumble(1, 0)
        check((r["low"], r["high"]) == (0, 0xFFFF), f"small motor on: the high-frequency motor at full ({r})")
        r = rumble(0, 200)
        check((r["low"], r["high"]) == (200 * 257, 0), f"large motor 200: the low-frequency motor {200 * 257} ({r})")
        r = rumble(255, 255)
        check((r["low"], r["high"]) == (65535, 0xFFFF), f"both full ({r})")
        settings("rumble_strength 50")
        r = rumble(255, 255)
        check(abs(r["low"] - 32767) <= 130 and abs(r["high"] - 32767) <= 1,
              f"rumble_strength 50: half of each ({r})")
        settings("rumble_strength 0")
        r = rumble(255, 255)
        check((r["low"], r["high"]) == (0, 0), f"rumble_strength 0: nothing ({r})")
        settings("rumble_strength 100")
        r = rumble(0, 0)
        check((r["low"], r["high"]) == (0, 0), f"back to zero bytes: motors stopped ({r})")
        calls = r["calls"]
        game.run(40)
        check(game.call("rumble")["calls"] == calls, "silent motors are not asked again")
        rumble(0, 90)
        for _ in range(12):  # held by the game: one refresh in 8 fields
            game.store_data(act, bytes([0, 90]), offset=8)
            game.run(1)
        check(game.call("rumble")["calls"] >= calls + 2, "a running motor is refreshed (SDL stops it after its duration)")
        rumble(0, 0)
    finally:
        game.close()
    if failures:
        print(f"{len(failures)} failed")
        return 1
    print("analog_move_smoke: all ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
