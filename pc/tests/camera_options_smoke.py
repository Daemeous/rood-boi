#!/usr/bin/env python3
"""The optional map panning and first-person up/down inversions (settings
qol_map_invert and qol_fp_invert_y) and the right stick's first-person look
(pc/src/camera_stick.c pc_camera_stick_first_person, always on).

    python3 pc/tests/camera_options_smoke.py [--card pc/save/card1.mcd]

From a copy of the card (skipped without one), in the field:
  - the settings file's qol_map_invert 1 applies at startup (RPC
    camera_options), qol_fp_invert_y stays 0
  - first-person view (Start; vs_battle_cameraCurrentSpherical, pitch set to
    0 before each measure): D-pad Up lowers the pitch, the right stick up
    too; stick right raises the yaw as D-pad Right does and leaves the
    pitch; the follow camera's stick turns nothing meanwhile; with
    qol_fp_invert_y Up and the stick up raise the pitch, stick right still
    raises the yaw
  - R3 leaves the first-person view (the follow camera's R3 reset is off
    there) and resets nothing
  - the map (top-level Map: MENU5, _geomOffsetX / _geomOffsetY): with
    qol_map_invert off Right raises X and Up lowers Y; on, Right lowers X and
    Up raises Y (the D-pad, which the left stick and the arrow keys feed)."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F
MAP_ROW = 5  # the top-level row of Map (MENU5)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="output directory")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "camera_options"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    settings = out / "settings.cfg"
    settings.write_text("qol_map_invert 1\n")
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SETTINGS": str(settings)}
    sym = symbols()
    spherical, fp_flag = sym["vs_battle_cameraCurrentSpherical"], sym["_firstPersonViewEnabled"]
    menu_state, submenu = sym["vs_battle_menuState"], sym["vs_battle_submenuStates"]
    geom_x, geom_y = sym["_geomOffsetX"], sym["_geomOffsetY"]
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    game = Game.launch(fast=True, card=str(card), log=out / "game.log", extra_env=env)
    try:
        def options(**params):
            return game.call("camera_options", **params)

        def i32(address):
            return struct.unpack("<i", game.read(address, 4))[0]

        def yaw_pitch():
            return struct.unpack("<ii", game.read(spherical, 8))

        def wrap(d):
            return (d + 2048) % 4096 - 2048

        def look(buttons=None, stick=None, vsyncs=12):
            """Pitch 0, then hold `buttons` or the right stick; the yaw and pitch changes."""
            game.write(spherical + 4, struct.pack("<i", 0))
            game.run(2)
            y0, p0 = yaw_pitch()
            if stick:
                game.call("input", right_stick=stick, vsyncs=vsyncs)
            else:
                game.call("input", buttons=buttons, vsyncs=vsyncs)
            game.run(vsyncs + 2)
            y1, p1 = yaw_pitch()
            return wrap(y1 - y0), p1 - p0

        o = options()
        check(o["map_invert"] == 1 and o["fp_invert_y"] == 0, f"settings file: qol_map_invert 1 ({o})")

        game.wait_title()
        game.press("start", vsyncs=4, settle=90)
        for _ in range(3):  # Continue, the card, its first save
            game.press("circle", vsyncs=3, settle=90)
        game.run(400)
        state = game.state()
        if not state.get("player"):
            print(f"FAIL: not in the field after Continue: {state}")
            return 1

        # ---- first-person view ----
        game.press("start", vsyncs=3, settle=90)
        cam = game.call("camera_stick")
        check(i32(fp_flag) == 1 and cam["first_person"] == 1,
              f"Start: the first-person view ({i32(fp_flag)}, {cam['first_person']})")
        turns = cam["turns"]
        _, up = look(buttons=["up"])
        _, stick_up = look(stick=[0, -1])
        stick_yaw, stick_pitch = look(stick=[1, 0])
        dpad_yaw, _ = look(buttons=["right"])
        cam = game.call("camera_stick")
        check(up < 0, f"off: D-pad Up lowers the pitch ({up})")
        check(stick_up < 0, f"off: the right stick up lowers the pitch too ({stick_up}, fp_looks {cam['fp_looks']})")
        check(stick_yaw > 0 and dpad_yaw > 0 and stick_pitch == 0,
              f"stick right turns as D-pad Right ({stick_yaw}, {dpad_yaw}), pitch unchanged ({stick_pitch})")
        check(cam["turns"] == turns, f"the follow camera's stick turned nothing ({turns} -> {cam['turns']})")
        options(fp_invert_y=True)
        _, up = look(buttons=["up"])
        _, stick_up = look(stick=[0, -1])
        stick_yaw, _ = look(stick=[1, 0])
        check(up > 0, f"qol_fp_invert_y: D-pad Up raises the pitch ({up})")
        check(stick_up > 0, f"qol_fp_invert_y: the right stick up raises it too ({stick_up})")
        check(stick_yaw > 0, f"qol_fp_invert_y: stick right still turns right ({stick_yaw})")
        options(fp_invert_y=False)
        resets = game.call("camera_stick")["resets"]
        game.press("r3", vsyncs=3, settle=90)
        cam = game.call("camera_stick")
        check(i32(fp_flag) == 0 and cam["resets"] == resets,
              f"R3 leaves the first-person view ({i32(fp_flag)}), no camera reset ({resets} -> {cam['resets']})")
        game.run(60)

        # ---- the map ----
        def current_menu():
            return game.read(menu_state, 1)[0]

        game.press("triangle", vsyncs=3, settle=40)
        for _ in range(10):
            if game.read(submenu, 1)[0] == MAP_ROW:
                break
            game.press("down", vsyncs=3, settle=8)
        game.press("circle", vsyncs=3, settle=240)
        check(current_menu() not in (0, CLOSED), f"Map opens ({current_menu():#x})")

        def pan(button):
            """The map's offset change while `button` is held 8 vsyncs (MENU5's statics are host data, read
            only: the moves stay within the +-80 / +-96 clamps from the centre the map opens on)."""
            x0, y0 = i32(geom_x), i32(geom_y)
            game.call("input", buttons=[button], vsyncs=8)
            game.run(10)
            return i32(geom_x) - x0, i32(geom_y) - y0

        options(map_invert=False)
        right, _ = pan("right")
        _, up = pan("up")
        check(right > 0 and up < 0, f"map, off: Right raises X ({right}), Up lowers Y ({up})")
        options(map_invert=True)
        right, _ = pan("right")
        _, up = pan("up")
        check(right < 0 and up > 0, f"map, qol_map_invert: Right lowers X ({right}), Up raises Y ({up})")
        for _ in range(4):
            if current_menu() == CLOSED:
                break
            game.press("triangle", vsyncs=3, settle=90)
        check(current_menu() == CLOSED, f"Triangle closes the map and the menu ({current_menu():#x})")
    finally:
        game.close()

    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
