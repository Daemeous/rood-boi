#!/usr/bin/env python3
"""A screen that owns the display clears the 16:9 view's sides (pc/src/screen_owner.c, gpu_hw.c screen_owner_primitive).

    python3 pc/tests/screen_owner_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

The user's report: "at the Game Over screen there's some random chunk of the level still in the frame on either the left or
right" (16:9). The Game Over picture is tiles of a GIM image, opaque 2D over the whole 4:3 screen; in the original it hides
the room drawn before it. Widescreen compresses the tiles to the 4:3 part of the view and the room (3D, drawn across the
whole view) shows in the sides wherever no tile extends into them (the rows whose tiles are textured).

Checks, in a 1280 x 720 window with the widescreen picture:
 - Game Over, the setting screen_owner 0: the room's chunk is in the right side (a positive control: the picture that was
   reported, 20+ mean brightness there);
 - the same frame with screen_owner 1: both sides black (mean < 2) and the 4:3 part of the picture unchanged (the same pixels
   to within the sides' bands);
 - screen_owner_fill, a tunable: the sides take another colour (RPC settings apply);
 - a field frame with the HUD and the command menu open (opaque 2D, never the whole screen): no table completes (the
   counters of RPC graphics do not move) and the view's sides keep the scene (a no-coverage control).

--out keeps frames for looking (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import struct
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

PTR = 8 if os.environ.get("ROOD_ARCH", "64") == "64" else 4
failures = []


def check(ok, message):
    print(("PASS: " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def present(game, out, name):
    game.wait_presents(3)
    path = out / f"{name}.png"
    game.present_screenshot(path)
    image = np.asarray(Image.open(path).convert("RGB")).astype(int)
    path.unlink()
    return image


def sides(image):
    """The mean brightness of the view's left and right sides below the banner's top (the VRAM sides: 40 of 320 columns each)."""
    h, w = image.shape[:2]
    band = slice(int(h * 0.05), int(h * 0.95))
    return float(image[band, : w // 8].mean()), float(image[band, w - w // 8:].mean())


def launch(out, card, label):
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_WIDESCREEN": "1"}
    game = launch_at("save_room", card=card, fast=True, log=out / f"screen-owner-{label}.log", extra_env=env)
    game.call("graphics", widescreen=True, interpolate=False)
    return game


def game_over(game):
    """Ashley's HP to 0; the Game Over screen once it has faded in."""
    game.store("vs_battle_actors[0]->stats->currentHP", 0)
    game.until(lambda: game.state()["game_over"], step=10, guard=200, what="Game Over")
    game.run(240)


def run(args, out, card):
    # A field frame with the HUD and the command menu: opaque 2D, never the whole screen (a no-coverage control).
    game = launch(out, card, "field")
    try:
        game.run(60)
        before = game.call("graphics")
        game.press("triangle", vsyncs=3, settle=90)
        game.run(30)
        field = present(game, out, "field-menu")
        after = game.call("graphics")
        left, right = sides(field)
        check(after["screen_owner_completed"] == before["screen_owner_completed"],
              f"field with the command menu: no table completed ({before['screen_owner_completed']} -> "
              f"{after['screen_owner_completed']})")
        check(left > 8 or right > 8, f"field with the command menu: the sides keep the scene (left {left:.1f}, right {right:.1f})")
    finally:
        game.close()

    # Game Over with the rule off: the reported picture (the positive control).
    game = launch(out, card, "off")
    try:
        game.call("graphics", screen_owner=False)
        game_over(game)
        off = present(game, out, "gameover-off")
        off_left, off_right = sides(off)
        check(max(off_left, off_right) > 5,
              f"Game Over, screen_owner 0 (the reported picture): a chunk of the room in a side (left {off_left:.1f}, "
              f"right {off_right:.1f}; the rule on: 0)")
    finally:
        game.close()

    # The rule on (the default).
    game = launch(out, card, "on")
    try:
        check(game.call("graphics")["screen_owner"] == 1, "screen_owner is on by default")
        game_over(game)
        on = present(game, out, "gameover-on")
        on_left, on_right = sides(on)
        stats = game.call("graphics")
        check(on_left < 2 and on_right < 2, f"Game Over, screen_owner 1: both sides black (left {on_left:.1f}, right {on_right:.1f})")
        check(stats["screen_owner_cleared"] > 0, f"Game Over: the picture completed and its sides were cleared "
                                                 f"({stats['screen_owner_completed']} completed, {stats['screen_owner_cleared']} runs)")
        h, w = on.shape[:2]
        middle = slice(w // 8, w - w // 8)
        same = float(np.abs(on[:, middle] - off[:, middle]).mean())
        check(same < 0.5, f"Game Over: the 4:3 part is the same picture with the rule on or off (mean difference {same:.3f})")
    finally:
        game.close()

    # The fill colour is a setting.
    game = launch(out, card, "green")
    try:
        game.call("settings", apply="screen_owner_fill 0 40 0")
        game_over(game)
        green = present(game, out, "gameover-green")
        gm = green[int(h * 0.41):int(h * 0.59), w - w // 8:].mean(axis=(0, 1))  # (the rows whose tiles are textured: the others extend black)
        check(abs(gm[1] - 40) < 6 and gm[0] < 4 and gm[2] < 4,
              f"Game Over: screen_owner_fill 0 40 0 fills the sides green (mean rgb {gm.round(1).tolist()})")
    finally:
        game.close()
    if args.out:
        for name, image in (("field-menu", field), ("gameover-off", off), ("gameover-on", on), ("gameover-green", green)):
            Image.fromarray(image.astype(np.uint8)).save(out / f"keep-{name}.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_save()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "screen_owner"
    out.mkdir(parents=True, exist_ok=True)
    with card_guard(out / "cards", source=args.card) as cards:
        run(args, out, cards.card1)
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
