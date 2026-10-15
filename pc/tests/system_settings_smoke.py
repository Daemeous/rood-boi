#!/usr/bin/env python3
"""System settings (pc/src/ui/ui_settings.h hooks): window mode, master volume,
brightness and binding capture apply live, save to the settings file and come
back after a relaunch; saved widescreen / perspective_textures survive the
presenter's start (it used to overwrite them from the environment).

    python3 pc/tests/system_settings_smoke.py [--card pc/save/card1.mcd] [--out DIR]

First run, with ROOD_SETTINGS on a fresh file and audio recorded to a WAV:
  - defaults: windowed, master_volume 100 (gain 1), brightness 50
  - brightness: 80 brightens and 20 darkens the presented picture, 50 gives
    the same image back
  - master_volume: 0 ramps the gain to 0, 40 to 0.16, 100 to 1; in the save
    room the recorded music goes silent while it is 0
  - window_mode: borderless and fullscreen report a fullscreen window
    (exclusive for fullscreen) where the video driver supports it (headless
    offscreen may not: reported, not failed); windowed restores the size
  - in the save room (from a copy of the card): the Bindings capture takes a
    pushed SDL key (Triangle gets "m"; F1 before it is refused with a
    reason; m held and repeating stays the capture's until released),
    Escape cancels a capture, a pushed
    gamepad trigger binds (Circle gets the left trigger), a capture started
    from the gamepad gives up after 5 s; the captured key never reaches the
    game, and afterwards "m" opens the menu in the field
  - the file holds every value (and widescreen 1 / perspective_textures 0)
Second run with the same file (no ROOD_WIDESCREEN / ROOD_PERSPECTIVE): every
value is back; hand-written ui_menu_tint 1 / ui_menu_tint_custom 40 -3 9 load
as Classic with the channels clamped to 0..31 (and save as classic, 31 0 9). Third run with ROOD_WIDESCREEN=0 ROOD_PERSPECTIVE=1: they apply,
a save keeps the file's widescreen / perspective_textures (environment
overrides are never saved as the user's), until the setting is changed.
Without the card, the field part is skipped."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, Game, symbols  # noqa: E402
from weapon_slots_smoke import default_card  # noqa: E402

CLOSED = 0x3F


def settings_lines(path):
    lines = {}
    for line in pathlib.Path(path).read_text().splitlines():
        if line and not line.startswith("#"):
            key, _, value = line.partition(" ")
            lines[key] = value.strip()
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "system_settings"
    out.mkdir(parents=True, exist_ok=True)
    settings = out / "settings.cfg"
    settings.unlink(missing_ok=True)
    wav = out / "audio.wav"
    wav.unlink(missing_ok=True)
    have_card = pathlib.Path(args.card).exists()
    card = out / "card1.mcd"
    if have_card:
        shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SETTINGS": str(settings)}
    for name in ("ROOD_WIDESCREEN", "ROOD_PERSPECTIVE"):
        os.environ.pop(name, None)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def picture(game, name):
        ppm = out / f"{name}.ppm"
        ppm.unlink(missing_ok=True)
        game.call("present_dump", path=str(ppm.resolve()))
        for _ in range(8):
            game.run(1)
            if ppm.exists() and ppm.stat().st_size:
                break
        image = np.asarray(Image.open(ppm).convert("RGB")).astype(np.float64)
        ppm.unlink()
        return image

    # ---- first run ----
    game = Game.launch(fast=True, card=str(card) if have_card else None, audio=f"wav:{wav}",
                       log=out / "game.log", extra_env=env)
    try:
        game.wait_title()
        s = game.call("system")
        check(s["window_mode"] == "windowed" and s["master_volume"] == 100 and s["gain"] == 1
              and s["brightness"] == 50, f"defaults ({s})")
        g = game.call("graphics")
        check(not g["widescreen"] and g["perspective"],
              f"without a saved file: 4:3, perspective on ({g['widescreen']}, {g['perspective']})")

        # brightness: the title screen with the game paused (the same frame each time)
        game.run(30)
        base = picture(game, "brightness50")
        game.call("system", brightness=80)
        bright = picture(game, "brightness80")
        game.call("system", brightness=20)
        dark = picture(game, "brightness20")
        game.call("system", brightness=50)
        again = picture(game, "brightness50-again")
        lit = base.mean() > 2
        check(lit, f"the title is drawn (mean {base.mean():.2f})")
        check(bright.mean() > base.mean() + 1, f"brightness 80 brightens ({base.mean():.2f} -> {bright.mean():.2f})")
        check(dark.mean() < base.mean() - 1, f"brightness 20 darkens ({base.mean():.2f} -> {dark.mean():.2f})")
        check(np.array_equal(base, again), "brightness 50 gives the same picture back")
        game.call("system", brightness=70)

        # master volume: the gain (the recording's) ramps to the setting
        game.call("system", master_volume=0)
        game.run(60)
        s = game.call("system")
        check(s["master_volume"] == 0 and s["gain"] == 0, f"master_volume 0: gain 0 ({s['gain']})")
        game.call("system", master_volume=40)
        game.run(30)
        s = game.call("system")
        check(abs(s["gain"] - 0.16) < 1e-6, f"master_volume 40: gain 0.16 ({s['gain']})")
        game.call("system", master_volume=100)
        game.run(30)
        check(game.call("system")["gain"] == 1, "master_volume 100: gain 1 (samples untouched)")

        # window mode
        size = game.call("system")["window"]["size"]
        s = game.call("system", window_mode="borderless")
        game.run(4)
        w = game.call("system")["window"]
        if w["failed"]:
            print(f"note: {w['video']} cannot go fullscreen here ({w})")
            check(w["mode"] == "windowed" and w["failed_mode"] == "borderless",
                  f"a failed switch keeps the window's mode and reports the one that failed ({w})")
        else:
            check(w["fullscreen"] and not w["exclusive"], f"borderless: fullscreen at the desktop mode ({w})")
        game.call("system", window_mode="fullscreen")
        game.run(4)
        w = game.call("system")["window"]
        if not w["failed"]:
            check(w["fullscreen"], f"fullscreen: fullscreen ({w})")
            print(f"note: fullscreen exclusive mode reported: {w['exclusive']} ({w['video']})")
        game.call("system", window_mode="windowed")
        game.run(4)
        w = game.call("system")["window"]
        check(w["mode"] == "windowed" and not w["fullscreen"], f"windowed again ({w})")
        check(w["size"] == size, f"windowed: the size before fullscreen comes back ({size} -> {w['size']})")
        game.call("system", window_mode="borderless")
        game.run(4)

        game.call("graphics", widescreen=True, perspective=False)

        if have_card:
            sym = symbols()
            menu_state = sym["vs_battle_menuState"]
            game.press("start", vsyncs=4, settle=90)
            for _ in range(3):
                game.press("circle", vsyncs=3, settle=90)
            game.run(400)
            state = game.state()
            check((state["zone"], state["room"]) == (9, 17), f"in the save room ({state})")

            def current_menu():
                return game.read(menu_state, 1)[0]

            def key(name, vsyncs=4, settle=40):
                game.call("system", key=name, down=True)
                game.run(vsyncs)
                game.call("system", key=name, down=False)
                game.run(settle)

            check(current_menu() == CLOSED, f"the field, no menu ({current_menu():#x})")
            # the save room's music, recorded: 3 s at 100, 3 s at 0, 3 s at 100 (frame numbers in the WAV)
            marks = [game.call("system")["audio_recorded"]]
            for volume in (0, 100):
                game.run(180)
                marks.append(game.call("system", master_volume=volume)["audio_recorded"])
            game.run(180)
            marks.append(game.call("system", master_volume=40)["audio_recorded"])
            # capture a key for Triangle: the key goes to the capture, not the game
            c = game.call("system", bind="triangle")["capture"]
            check(c["listening"] and c["button"] == 4, f"capture for Triangle listens ({c})")
            # a reserved key is refused with a reason; the capture goes on
            key("f1", settle=4)
            c = game.call("system")["capture"]
            check(c["listening"] and "F1" in c["refused"], f"F1 is refused, the capture listens on ({c})")
            # m ends it; held, its auto-repeats (more key downs) stay the capture's, though m is Triangle's now
            game.call("system", key="m", down=True)
            game.run(4)
            c = game.call("system")["capture"]
            check(not c["listening"] and c["last"] >= -1 and c["key_held"] == "m", f"m ends the capture ({c})")
            for _ in range(3):
                game.call("system", key="m", down=True)
                game.run(10)
            check(current_menu() == CLOSED, f"the captured key (and its repeats) did not reach the game ({current_menu():#x})")
            game.call("system", key="m", down=False)
            game.run(40)
            check(game.call("system")["capture"]["key_held"] == "", "released: m is the game's again")
            # Escape cancels
            c = game.call("system", bind="circle")["capture"]
            key("escape", settle=4)
            c = game.call("system")["capture"]
            check(not c["listening"] and c["last"] == -1, f"Escape cancels a capture ({c})")
            # a gamepad trigger past half way binds
            c = game.call("system", bind="circle")["capture"]
            check(c["listening"] and not c["pad_timeout"], f"capture for Circle (keyboard: no timeout) ({c})")
            game.call("system", pad="lefttrigger", down=True)
            game.run(4)
            c = game.call("system")["capture"]
            check(not c["listening"] and c["pad_held"] == 104, f"the left trigger ends the capture, held ({c})")
            game.call("system", pad="lefttrigger", down=False)
            game.run(4)
            check(game.call("system")["capture"]["pad_held"] == -1, "released: the pad goes back to the game")
            # started from the gamepad: gives up after 5 s
            game.call("system", pad="south", down=True)
            game.run(2)
            game.call("system", pad="south", down=False)
            game.run(2)
            c = game.call("system", bind="square")["capture"]
            check(c["listening"] and c["pad_timeout"], f"capture from the gamepad has a timeout ({c})")
            # paused: the window still pumps events and ticks the capture; wait for it (bounded)
            t0 = time.monotonic()
            while c["listening"] and time.monotonic() - t0 < 15:
                time.sleep(0.25)
                c = game.call("system")["capture"]
            waited = time.monotonic() - t0
            check(not c["listening"] and c["last"] == -1 and waited > 4.5,
                  f"about 5 s idle cancels it ({waited:.1f} s, {c})")
            # the new key works in the field
            key("m", settle=60)
            check(current_menu() == 0, f"m opens the menu in the field ({current_menu():#x})")
            game.press("triangle", vsyncs=3, settle=90)
            check(current_menu() == CLOSED, f"Triangle closes it ({current_menu():#x})")

        game.call("system", master_volume=40)
        game.call("settings", action="save")
        saved = settings_lines(settings)
        expect = {"window_mode": "borderless", "master_volume": "40", "brightness": "70", "widescreen": "1",
                  "perspective_textures": "0"}
        for k, v in expect.items():
            check(saved.get(k) == v, f"saved {k} {v} ({saved.get(k)})")
        if have_card:
            check(saved.get("bind_key_triangle", "").split(",")[0] == "m",
                  f"saved bind_key_triangle m,... ({saved.get('bind_key_triangle')})")
            check(saved.get("bind_pad_circle") == "lefttrigger",
                  f"saved bind_pad_circle lefttrigger ({saved.get('bind_pad_circle')})")
    finally:
        game.close()

    # the recording around master_volume 0 in the save room: sound, silence (after the 20 ms ramp), sound
    if have_card:
        data = np.frombuffer(wav.read_bytes()[44:], dtype=np.int16)
        frames = data[:len(data) // 2 * 2].reshape(-1, 2).astype(np.int32)
        a, b, c, d = marks
        peaks = [int(np.abs(frames[x + 1000:y]).max()) if y > x + 1000 else 0 for x, y in ((a, b), (b, c), (c, d))]
        print(f"note: recorded {len(frames)} frames; marks {marks}; peaks at 100 / 0 / 100: {peaks}")
        check(len(frames) >= d and peaks[0] > 0 and peaks[2] > 0, f"the save room's music is recorded ({peaks})")
        check(peaks[1] == 0, f"master_volume 0 silences the output after the ramp ({peaks[1]})")

    # ---- second run: the same file (with a hand-written menu tint: a digit and out-of-range channels) ----
    text = "".join(line + "\n" for line in settings.read_text().splitlines()
                   if not line.startswith("ui_menu_tint"))
    settings.write_text(text + "ui_menu_tint 1\nui_menu_tint_custom 40 -3 9\n")
    game = Game.launch(fast=True, card=str(card) if have_card else None, log=out / "game2.log", extra_env=env)
    try:
        game.wait_title()
        s = game.call("system")
        check(s["window_mode"] == "borderless" and s["master_volume"] == 40 and s["brightness"] == 70,
              f"relaunch: window_mode, master_volume, brightness back ({s})")
        g = game.call("graphics")
        check(g["widescreen"] and not g["perspective"],
              f"relaunch: widescreen 1, perspective_textures 0 kept ({g['widescreen']}, {g['perspective']})")
        sym = symbols()
        tint = struct.unpack("<i", game.read(sym["pc_ui_menu_tint"], 4))[0]
        custom = list(struct.unpack("<3i", game.read(sym["pc_ui_menu_tint_custom"], 12)))
        check(tint == 1 and custom == [31, 0, 9],
              f"ui_menu_tint 1: Classic; ui_menu_tint_custom 40 -3 9 clamped to [31, 0, 9] ({tint}, {custom})")
        game.call("settings", action="save")
        saved = settings_lines(settings)
        check(saved.get("ui_menu_tint") == "classic" and saved.get("ui_menu_tint_custom") == "31 0 9",
              f"saved ui_menu_tint classic, ui_menu_tint_custom 31 0 9 ({saved.get('ui_menu_tint')}, "
              f"{saved.get('ui_menu_tint_custom')})")
        if have_card:
            check(saved.get("bind_key_triangle", "").split(",")[0] == "m"
                  and saved.get("bind_pad_circle") == "lefttrigger", "relaunch: the bindings are back")
    finally:
        game.close()

    # ---- third run: environment overrides apply but are not saved as the user's ----
    game = Game.launch(fast=True, log=out / "game3.log",
                       extra_env=dict(env, ROOD_WIDESCREEN="0", ROOD_PERSPECTIVE="1"))
    try:
        game.wait_title()
        g = game.call("graphics")
        check(not g["widescreen"] and g["perspective"],
              f"ROOD_WIDESCREEN=0, ROOD_PERSPECTIVE=1 apply ({g['widescreen']}, {g['perspective']})")
        game.call("system", brightness=60)
        game.call("settings", action="save")
        saved = settings_lines(settings)
        check(saved.get("widescreen") == "1" and saved.get("perspective_textures") == "0"
              and saved.get("brightness") == "60",
              f"a save keeps the file's widescreen 1 / perspective_textures 0, and saves the rest "
              f"({saved.get('widescreen')}, {saved.get('perspective_textures')}, {saved.get('brightness')})")
        game.call("graphics", widescreen=True)
        game.call("settings", action="save")
        game.call("graphics", widescreen=False)
        game.call("settings", action="save")
        saved = settings_lines(settings)
        check(saved.get("widescreen") == "0", f"changed since: the user's value is saved ({saved.get('widescreen')})")
    finally:
        game.close()

    print("PASS" if not failures else f"FAIL: {len(failures)} check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
