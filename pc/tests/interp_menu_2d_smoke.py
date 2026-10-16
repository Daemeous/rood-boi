#!/usr/bin/env python3
"""Frame interpolation leaves the original menus' 2D as the game drew it, at
16:9 (GitHub issues #8 and #14).

The original Status and Equip screens redraw a few rows of their own picture
each frame (sprites whose texels are the frame buffer under the information
line). Drawn live they sample the target as drawn so far (pc_hw_sync: the
16:9 picture, 2D compressed about the centre); the interpolated images
redrew them from native VRAM (the CPU's 4:3 picture), so every other image
showed a smeared copy of "INFORMATION" to its left and dashes right of the
stat lines (with present_rate 3, in bursts). Now a replayed batch that
samples what its frame drew samples the replay target (gpu_hw.c
op_feedback / replay_sample_own).

The memory card screen (MENU7) swaps the CLUT row its pictures share in
for each save's thumbnail and back. The in-between images redrew the
batches drawn through that row before the swap (the whole-screen backdrop)
by copying the game's finished picture there, and the batches after it
blended over that copy a second time: the "MENU" title shone brighter on
every other image (issue #14). Now they sample the row as it was when they
drew (gpu_hw.c replay_unwrite: the writes since undone).

From the pinned test card's save room at 16:9, anti-aliasing off: the
original Status screen (Triangle, STATUS), the Equip screen's affinity
view (ITEMS > EQUIP, weapon, Square) and the memory card screen's save
list (DATA > LOAD > MEMORY CARD slot 1). For each: a burst of presented images
with interpolation off over ~3 s (the reference; where those differ among
themselves,
Ashley's idle and the animated backdrop, is masked, dilated, and Ashley's
whole box), then bursts
with interpolation on at present_rate 1 and 3 (live and in-between images
alike). Outside the mask every image must equal the reference (no stray
glyph or line pixels).

The title's memory card screen (Continue): its bars run off the screen's
right edge and stretch to the view's edge; its background is 2D, compressed,
and never reaches the view's sides. What the bars painted there stayed in
both frame buffers (issue #14): after the screen closed, the right ends of
the three save bars; above the first save, the end of the "MEMORY CARD slot
2" bar, different in each buffer (flickering). Now opaque 2D over the
screen's edge clears what an extension painted beside it (gpu_hw.c
side_paint). Opened and closed with interpolation off, then on at
present_rate 1 and 3: in every image no bright side pixel beside a dark
screen edge (a bar's end without its bar), the open list's images equal
the list with interpolation off, and after closing the images equal the
slot chooser before it opened.

    python3 pc/tests/interp_menu_2d_smoke.py [--card CARD] [--out DIR]

--out keeps the bursts (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from checkpoints import launch_at  # noqa: E402
from rood_rpc import OUT  # noqa: E402

COUNT = 12
STRAY = 60  # summed RGB difference that counts as a changed pixel
SCREENS = {
    "status": [("triangle", 90), ("down", 20), ("down", 20), ("circle", 150)],
    "equip-affinity": [("triangle", 90), ("down", 20), ("circle", 90), ("circle", 120), ("circle", 90),
                       ("square", 90)],
    "card-list": [("triangle", 60), ("down", 10), ("down", 10), ("down", 10), ("down", 10), ("circle", 60),
                  ("down", 10), ("circle", 150), ("circle", 150)],
}
failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    if not ok:
        failures.append(what)
    return ok


def burst(game, base):
    for old in base.parent.glob(base.name + ".*"):
        old.unlink()
    game.call("present_dump", path=str(base), count=COUNT)
    game.run(COUNT + 6)
    paths = [pathlib.Path(f"{base}.{i:04d}.ppm") for i in range(COUNT)]
    for _ in range(40):  # written a few presents later
        if all(p.exists() for p in paths):
            break
        game.run(1)
    frames = []
    for p in paths:
        frames.append(np.asarray(Image.open(p)).astype(np.int16))
        p.unlink()
    return frames


def changed(a, b):
    return np.abs(a - b).sum(axis=2) > STRAY


def screen(card, out, name, steps, keep):
    work = out / name
    work.mkdir(parents=True, exist_ok=True)
    copy = work / "card1.mcd"
    shutil.copyfile(card, copy)
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_GI": "0",
           "ROOD_WINDOW": "1280x720"}
    game = launch_at("save_room", card=str(copy), fast=True, socket_path=f"/tmp/rood-i2d-{os.getpid()}-{name}.sock",
                     log=str(work / "game.log"), extra_env=env)
    try:
        game.call("graphics", widescreen=True, interpolate=False, present_rate=1, aa="off")
        game.run(10)
        for button, settle in steps:
            game.press(button, vsyncs=3, settle=settle)
        game.run(30)
        reference = []
        for _ in range(4):  # over ~3 s: Ashley's idle and the backdrop's motion
            reference += burst(game, work / "off")
            game.run(40)
        moving = np.zeros(reference[0].shape[:2], bool)
        for f in reference[1:]:
            moving |= changed(reference[0], f)
        moving = ndimage.binary_dilation(moving, iterations=6)
        # Ashley (3D, interpolated: in-between poses are new pixels): the box around each large moving part.
        labels, count = ndimage.label(moving)
        for k, box in enumerate(ndimage.find_objects(labels)):
            if box and int((labels[box] == k + 1).sum()) > 2000:
                y, x = box
                moving[max(y.start - 12, 0):y.stop + 12, max(x.start - 12, 0):x.stop + 12] = True
        still = int((~moving).sum())
        check(still > 0.5 * moving.size, f"{name}: the screen is mostly still with interpolation off "
              f"({100 * still / moving.size:.0f}% of the image)")
        # The screen shows (not the field): the stat column's lettering is there.
        check(int((reference[0].sum(axis=2) > 600).sum()) > 2000, f"{name}: the menu is drawn")
        for rate in (1, 3):
            game.call("graphics", interpolate=True, present_rate=rate)
            game.run(8)
            frames = burst(game, work / f"on-r{rate}")
            stray = [int((changed(reference[0], f) & ~moving).sum()) for f in frames]
            if keep:
                worst = int(np.argmax(stray))
                Image.fromarray(frames[worst].astype(np.uint8)).save(work / f"on-r{rate}-worst.png")
            check(max(stray) <= 20, f"{name}, interpolation on, present_rate {rate}: no 2D pixel outside the "
                  f"moving parts differs from the game's own frame (per image: {stray})")
        if keep:
            Image.fromarray(reference[0].astype(np.uint8)).save(work / "off.png")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log, f"{name}: no crash")


def side_strays(image):
    """Bright pixels in the view's sides (beyond the compressed 4:3 screen,
    VRAM x 40..280 of 320) in rows where the screen's edge beside them is
    dark: a bar's end without its bar. Extended bars continue the bar at the
    edge; the title's background leaves the sides black."""
    a = image.astype(np.int32)
    k = a.shape[1] / 320.0
    right = (a[:, int(281 * k) + 2:].sum(axis=2) > 90) & (a[:, int(278 * k)].sum(axis=1) < 45)[:, None]
    left = (a[:, :int(39 * k) - 2].sum(axis=2) > 90) & (a[:, int(42 * k)].sum(axis=1) < 45)[:, None]
    return int(right.sum() + left.sum())


def still_mask(frames):
    """Where a burst's images differ among themselves (the cursor), dilated."""
    moving = np.zeros(frames[0].shape[:2], bool)
    for f in frames[1:]:
        moving |= changed(frames[0], f)
    return ndimage.binary_dilation(moving, iterations=6)


def title_card(card, out, keep):
    """The title's memory card screen opened and closed (issue #14)."""
    name = "title-card"
    work = out / name
    work.mkdir(parents=True, exist_ok=True)
    copy = work / "card1.mcd"
    shutil.copyfile(card, copy)
    env = {"ROOD_OUT": str(work), "ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_GI": "0",
           "ROOD_WINDOW": "1280x720"}
    game = launch_at("title", card=str(copy), fast=True, socket_path=f"/tmp/rood-i2d-{os.getpid()}-{name}.sock",
                     log=str(work / "game.log"), extra_env=env)
    try:
        game.call("graphics", widescreen=True, interpolate=False, present_rate=1, aa="off")
        game.press("start", vsyncs=4, settle=90)
        game.press("circle", vsyncs=3, settle=150)  # Continue: the slot chooser
        chooser = burst(game, work / "chooser")
        chooser_moving = still_mask(chooser)
        check(int((chooser[0].sum(axis=2) > 600).sum()) > 2000, f"{name}: the slot chooser is drawn")
        reference = None
        for interpolate, rate in ((False, 1), (True, 1), (True, 3)):
            what = f"interpolation {'on, present_rate ' + str(rate) if interpolate else 'off'}"
            game.call("graphics", interpolate=interpolate, present_rate=rate)
            game.press("circle", vsyncs=3, settle=150)  # slot 1: the save list
            listed = burst(game, work / f"list-{int(interpolate)}-r{rate}")
            strays = [side_strays(f) for f in listed]
            check(max(strays) == 0, f"{name}, {what}, save list: no bar end left in the view's sides "
                  f"(per image: {strays})")
            if reference is None:
                reference, list_moving = listed, still_mask(listed)
                check(int((changed(chooser[0], listed[0]) & ~chooser_moving).sum()) > 2000,
                      f"{name}: the save list opened")
            else:
                stray = [int((changed(reference[0], f) & ~list_moving).sum()) for f in listed]
                check(max(stray) <= 20, f"{name}, {what}, save list: every image is the list as drawn with "
                      f"interpolation off (per image: {stray})")
            game.press("cross", vsyncs=3, settle=120)  # back to the slot chooser
            after = burst(game, work / f"after-{int(interpolate)}-r{rate}")
            strays = [side_strays(f) for f in after]
            stray = [int((changed(chooser[0], f) & ~chooser_moving).sum()) for f in after]
            check(max(strays) == 0 and max(stray) <= 20, f"{name}, {what}, closed: the slot chooser as before it "
                  f"opened, no save bars left behind (bar ends per image: {strays}; changed pixels: {stray})")
            if keep:
                worst = int(np.argmax(stray))
                Image.fromarray(after[worst].astype(np.uint8)).save(work / f"after-{int(interpolate)}-r{rate}.png")
                Image.fromarray(listed[0].astype(np.uint8)).save(work / f"list-{int(interpolate)}-r{rate}.png")
        if keep:
            Image.fromarray(chooser[0].astype(np.uint8)).save(work / "chooser.png")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log, f"{name}: no crash")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=os.environ.get("ROOD_TEST_CARD") or str(ROOT / "pc/build/test-card1.mcd"),
                        help="the pinned test card (copied, never written)")
    parser.add_argument("--out", help="keep the images here")
    args = parser.parse_args()
    card = pathlib.Path(args.card)
    if not card.exists():
        print("SKIP: no memory card", card)
        return 77
    out = pathlib.Path(args.out) if args.out else OUT / "interp_menu_2d"
    out.mkdir(parents=True, exist_ok=True)
    for name, steps in SCREENS.items():
        screen(card, out, name, steps, bool(args.out))
    title_card(card, out, bool(args.out))
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS: interpolated images keep the original menus' 2D at 16:9 (present_rate 1 and 3); "
          "the title's card screen leaves no bars behind")
    return 0


if __name__ == "__main__":
    sys.exit(main())
