#!/usr/bin/env python3
"""The memory card menu's slot images keep their colours on interpolated
frames (the user's report: in the save point's DATA > LOAD menu, slot 1's
location thumbnail was a white/blue blotch and the empty slots' "NO DATA"
images grey streaks).

    python3 pc/tests/save_menu_smoke.py [--card pc/save/card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself; skipped without
one, or without the disc's data/MENU files) to the save room and opens
DATA > LOAD > MEMORY CARD slot 1 (MENU7.PRG). Each slot's 64x32 image is an
8-bit sprite from SPMCIMG.BIN (VRAM 800,256) drawn through the CLUT row at
768,227: the menu stores that row, uploads the location's CLUT, draws the
sprite and uploads the old row back (_drawFileMenuElement). Frame
interpolation replays the newest frame's draws for the in-between images;
redrawn after the swap, the sprite sampled the restored menu CLUT. Batches
whose texels or CLUT were rewritten since they drew keep the frame's own
pixels in the replay (gpu_hw.c op_rewritten).

Checks, on the live target and on interpolated (replay) images over
several frames: every slot image matches one of the sheet's 56 images
decoded from the disc data (SPMCIMG.BIN with MCDATA.BIN's two location
CLUTs, as tools/data/MENU/SPMCIMG_BIN.py does) at the menu's brightness
(selected: 0x80, others: 0x40).

--out keeps frames for looking (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

SHEET_W, SHEET_H = 448, 256  # SPMCIMG.BIN: 7 x 8 images of 64 x 32, 8-bit
CLUT0_COLUMNS = {1, 2, 3, 4}  # locations 0-31; the rest (and the icons) use CLUT 1
SLOTS = 3  # slots shown in the list
MAX_ERROR = 16.0  # mean |difference| per channel; cursor and labels overlap a few pixels


def default_card():
    path = ROOT / "pc/save/card1.mcd"
    if not path.exists():  # a worktree: the main checkout's card
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                capture_output=True, text=True).stdout.strip()
        if common:
            path = pathlib.Path(common).parent / "pc/save/card1.mcd"
    return path


def data_file(name):
    for base in (ROOT / "data", ROOT / "build/data"):
        if (base / name).exists():
            return base / name
    return None


def sheet_images():
    """[(image index, brightness, 32x64x3 int array)] decoded from the disc data."""
    indices = np.frombuffer(data_file("MENU/SPMCIMG.BIN").read_bytes()[:SHEET_W * SHEET_H], np.uint8)
    indices = indices.reshape(SHEET_H, SHEET_W)
    cluts = np.frombuffer(data_file("MENU/MCDATA.BIN").read_bytes()[:1024], "<u2").reshape(2, 256)
    rgb = np.stack([(cluts & 31) << 3, ((cluts >> 5) & 31) << 3, ((cluts >> 10) & 31) << 3], -1).astype(np.int32)
    images = []
    for column in range(SHEET_W // 64):
        for row in range(SHEET_H // 32):
            texels = indices[row * 32:(row + 1) * 32, column * 64:(column + 1) * 64]
            colours = rgb[0 if column in CLUT0_COLUMNS else 1][texels]
            for brightness in (0x40, 0x80):
                images.append((column * 8 + row, brightness, np.minimum(colours * brightness // 0x80, 255)))
    return images


def native(path):
    """A gpu_dump (scaled VRAM target) at native resolution: each pixel's centre sample."""
    image = np.asarray(Image.open(path).convert("RGB")).astype(np.int32)
    k = image.shape[1] // 1024
    return image[k // 2::k, k // 2::k]


def slot_errors(vram, images):
    """Per slot: the best match (error, image, brightness, x, y) of the slot's
    image area against the sheet. The list is drawn in either buffer of the
    double-buffered display (x 0 or 320), slots 40 lines apart from y 72."""
    found = []
    for slot in range(SLOTS):
        best = None
        for x0 in (0, 320):
            for y in range(72 + 40 * slot - 6, 72 + 40 * slot + 7):
                for x in range(x0, x0 + 3):
                    area = vram[y:y + 32, x:x + 64]
                    for index, brightness, image in images:
                        error = float(np.abs(area - image).mean())
                        if best is None or error < best[0]:
                            best = (error, index, brightness, x, y)
        found.append(best)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(default_card()))
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    if not data_file("MENU/SPMCIMG.BIN") or not data_file("MENU/MCDATA.BIN"):
        print("SKIP: no data/MENU/SPMCIMG.BIN or MCDATA.BIN (extracted disc data)")
        return 77
    images = sheet_images()
    out = pathlib.Path(args.out) if args.out else OUT / "save_menu"
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "960x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_SCALE": "2"}
    os.environ.pop("ROOD_SETTINGS", None)
    game = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message)
        if not ok:
            failures.append(message)

    try:
        game.call("graphics", interpolate=True, present_rate=1)
        state = game.state()
        if not state.get("player"):
            print(f"FAIL: no room with Ashley after Continue: {state}")
            return 1
        # Command menu > DATA (fifth) > LOAD (second) > MEMORY CARD slot 1.
        game.press("triangle", vsyncs=3, settle=60)
        for _ in range(4):
            game.press("down", vsyncs=3, settle=10)
        game.press("circle", vsyncs=3, settle=60)
        game.press("down", vsyncs=3, settle=10)
        game.press("circle", vsyncs=3, settle=150)
        game.press("circle", vsyncs=3, settle=150)
        if args.out:
            game.present_screenshot(out / "load-menu.png")

        replays = 0
        for frame in range(6):
            game.run(1)
            for target in ("live", "replay"):
                path = out / f"{target}-{frame}.ppm"
                game.call("gpu_dump", path=str(path), replay=target == "replay")
                vram = native(path)
                if not args.out:
                    path.unlink()
                found = slot_errors(vram, images)
                print(f"{target} {frame}: " + ", ".join(
                    f"slot {i + 1} image {f[1]} x{f[2]:#x} at {f[3]},{f[4]} error {f[0]:.1f}" for i, f in enumerate(found)))
                for i, f in enumerate(found):
                    check(f[0] <= MAX_ERROR, f"{target} frame {frame}: slot {i + 1} is a sheet image "
                          f"(error {f[0]:.1f} <= {MAX_ERROR})")
                if target == "live":
                    live_found = found
                else:
                    # The replay of a menu frame is that frame: the same images.
                    check([f[1:3] for f in found] == [f[1:3] for f in live_found],
                          f"replay frame {frame}: the live frame's slot images")
                    replays += 1
        check(replays > 0, "interpolated images were checked")
    finally:
        game.close()
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
