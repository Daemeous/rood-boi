#!/usr/bin/env python3
"""The memory card menu's slot images keep their colours on interpolated
frames (the user's report: in the save point's DATA > LOAD menu, slot 1's
location thumbnail was a white/blue blotch and the empty slots' "NO DATA"
images grey streaks).

    python3 pc/tests/save_menu_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

Continues from a copy of the card (never the card itself, inside
card_fixtures.card_guard: the player's cards hashed before and after;
skipped without one, or without the disc's data/MENU files) to the save room and opens
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

Then, in 16:9 (a 1280 x 720 window), DATA > SAVE > MEMORY CARD slot 1 >
the first file: "Overwrite the file?" and its YES / NO rows, each a menu
window from the screen's left edge, which widescreen extends to the view's
edge (gpu_hw.c primitive_extend). With NO and then YES focused, each row's
extension beside the 4:3 screen covers the same pixel rows as the row
inside it (the cursor hand touching the edge cleared the extension in its
rows: the bars came apart, the user's report), at rest (two presented
images the same); Cross backs out without saving.

Then, in 16:9, Game Over (Ashley's HP written to 0) > Continue > the LOAD
menu (MENU7's file menu over the title's background, whose black texels
draw nothing): both frame buffers hold the same picture (the PS1 view, four
vsyncs), eight presented images in a row do not change (but for the cursor
hand), and the view's right side below the menu rows is black. The Game
Over screen leaves the room showing beside its banner (x 280-320, rows
90-135), each buffer its own last frame; the menu did not clear the buffers
as the title's LOAD screen does, so a piece of the room flickered at 30 Hz
in the view's right side (the user's report).

--out keeps frames for looking (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from menu_nav import DATA_COMMAND, top_command  # noqa: E402
from checkpoints import launch_at  # noqa: E402
from card_fixtures import card_guard, default_save  # noqa: E402

SHEET_W, SHEET_H = 448, 256  # SPMCIMG.BIN: 7 x 8 images of 64 x 32, 8-bit
CLUT0_COLUMNS = {1, 2, 3, 4}  # locations 0-31; the rest (and the icons) use CLUT 1
SLOTS = 3  # slots shown in the list
MAX_ERROR = 16.0  # mean |difference| per channel; cursor and labels overlap a few pixels


def default_card():
    """The tests' pinned card (run.py default_save), never the player's."""
    return default_save()


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
    with card_guard(out / "cards", source=args.card) as cards:
        return run(args, out, cards.card1, images)


def bands(column, level=30):
    """The runs of lit pixels down one column of a picture: [(first, last), ...]."""
    lit = column.astype(int).sum(axis=1) > level
    runs, start = [], None
    for y, on in enumerate(lit):
        if on and start is None:
            start = y
        elif not on and start is not None:
            runs.append((start, y - 1))
            start = None
    if start is not None:
        runs.append((start, len(lit) - 1))
    return runs


def check_save_confirm(args, out, card, check):
    """16:9: the save confirm's YES / NO rows are one piece each, their extensions at the rows' heights."""
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_WIDESCREEN": "1"}
    game = launch_at("save_room", card=card, fast=True, log=out / "game-16x9.log", extra_env=env)
    try:
        game.call("graphics", widescreen=True)
        game.press("triangle", vsyncs=3, settle=60)
        top_command(game, symbols(), DATA_COMMAND)
        game.press("circle", vsyncs=3, settle=60)  # DATA: SAVE is the first row
        for _ in range(3):  # SAVE, MEMORY CARD slot 1, the first file: "Overwrite the file?"
            game.press("circle", vsyncs=3, settle=150)

        def at_rest(name):
            """A presented image the same as the one before it (the rows have slid in)."""
            previous = None
            for _ in range(30):
                image = np.asarray(Image.open(game.present_screenshot(out / f"{name}.png")).convert("RGB"))
                if previous is not None and np.array_equal(image, previous):
                    return image
                previous = image
                game.run(4)
            return previous

        for focus, press in (("NO", None), ("YES", "up")):
            if press:
                game.press(press, vsyncs=3, settle=30)
            image = at_rest(f"confirm-{focus.lower()}")
            h, w = image.shape[:2]
            top = image[: h // 4]  # the rows sit at the top (PS1 y 18..46 of 224)
            side, inside = bands(top[:, w // 64]), bands(top[:, w * 3 // 8])
            rows = [b for b in inside if b[1] - b[0] >= h // 40][:2]  # YES and NO (the file list starts below)
            matched = [any(abs(s[0] - r[0]) <= 2 and abs(s[1] - r[1]) <= 2 for s in side) for r in rows]
            check(len(rows) == 2 and all(matched),
                  f"16:9 save confirm, {focus} focused: both rows' extensions at the rows' heights (inside "
                  f"{rows}, beside the screen {side})")
        game.press("cross", vsyncs=3, settle=60)  # NO: nothing saved
    finally:
        game.close()


PTR = 8 if os.environ.get("ROOD_ARCH") == "64" else 4  # host pointers (vs_battle_actors)


def unstable_blocks(frames, skip, block=16, level=8.0):
    """Blocks (x, y, mean difference) of a frame series that change from the
    first frame (skip(x0, y0, x1, y1) leaves out the cursor's blocks)."""
    first = frames[0].astype(np.int32)
    h, w = first.shape[:2]
    found = []
    for frame in frames[1:]:
        diff = np.abs(frame.astype(np.int32) - first).mean(axis=-1)
        for y in range(0, h - block + 1, block):
            for x in range(0, w - block + 1, block):
                if skip(x / w, y / h, (x + block) / w, (y + block) / h):
                    continue
                d = diff[y:y + block, x:x + block].mean()
                if d > level:
                    found.append((x, y, round(float(d), 1)))
    return found


def check_game_over_load(args, out, card, check):
    """16:9 (the user's report): Game Over > Continue > the LOAD menu (MENU7's
    file menu, the title's look) is the same in both frame buffers: no piece
    of the room left beside the Game Over banner (x 280-320, rows 90-135),
    which flickered under the menu at 30 Hz in the view's right side."""
    import struct
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_WINDOW": "1280x720", "ROOD_LIGHTS": str(lights), "ROOD_GI": "0", "ROOD_WIDESCREEN": "1"}
    game = launch_at("save_room", card=card, fast=True, log=out / "game-over.log", extra_env=env)
    work = out / "game-over"
    work.mkdir(exist_ok=True)

    def cpu():
        """The displayed frame buffer as the PS1 shows it (RPC screenshot: CPU VRAM, 4:3)."""
        path = work / "cpu.ppm"
        game.call("screenshot", path=str(path.resolve()))
        image = np.asarray(Image.open(path).convert("RGB"))
        path.unlink()
        return image

    def presents(count):
        """`count` consecutive presented images (what the window shows)."""
        base = work / "present"
        game.call("present_dump", path=str(base.resolve()), count=count)
        paths = [pathlib.Path(f"{base}.{i:04d}.ppm") for i in range(count)]
        game.until(lambda: all(p.exists() for p in paths), step=1, guard=60, what="present burst")
        images = [np.asarray(Image.open(p).convert("RGB")) for p in paths]
        for i, p in enumerate(paths):
            if args.out:
                Image.fromarray(images[i]).save(work / f"load-{i}.png")
            p.unlink()
        return images

    try:
        game.call("graphics", widescreen=True, interpolate=True, present_rate=1)
        # Ashley's HP to 0 (vs_battle_actors[0] -> +0x3c -> +0x18): the game's own Game Over follows.
        actor = int.from_bytes(game.read(symbols()["vs_battle_actors"], PTR), "little")
        record = int.from_bytes(game.read(actor + 0x3C, 4), "little")
        game.write(record + 0x18, struct.pack("<h", 0))
        game.until(lambda: game.state()["game_over"], step=10, guard=120, what="Game Over")

        def lit(image, x, y):  # (4:3 PS1 pixels)
            return image[y - 2:y + 3, x - 2:x + 3].mean() > 40

        # CONTINUE is lit once the Game Over screen has faded in (its options at 112..208, 128..192).
        game.until(lambda: lit(cpu(), 160, 144), step=4, guard=120, what="Game Over screen")
        game.run(30)
        game.press("circle", vsyncs=3, settle=0)
        # The LOAD menu: its title bar and both slot rows slid in (to x 180 / 194; rows at y 34..78).
        game.until(lambda: all(lit(cpu(), x, y) for x, y in ((200, 40), (230, 56), (230, 72))), step=2,
                   guard=120, what="the LOAD menu")
        game.run(20)
        if args.out:
            Image.fromarray(cpu()).save(work / "load-cpu.png")

        # The cursor hand (x 180, rows 26..90) changes colour every frame: left out.
        def cursor_43(x0, y0, x1, y1):
            return x1 > 165 / 320 and x0 < 200 / 320 and y1 > 20 / 224 and y0 < 95 / 224

        frames = []
        for _ in range(4):  # both frame buffers, twice (a game frame is 2 vsyncs)
            frames.append(cpu())
            game.run(1)
        moving = unstable_blocks(frames, cursor_43)
        check(not moving, f"Game Over > LOAD: both frame buffers hold the same picture (PS1 view; "
                          f"blocks that change: {moving[:6]})")

        def cursor_169(x0, y0, x1, y1):  # the 4:3 screen compressed about the 16:9 view's centre
            return x1 > 0.125 + 0.75 * 165 / 320 and x0 < 0.125 + 0.75 * 200 / 320 and y1 > 20 / 224 and y0 < 95 / 224

        images = presents(8)
        moving = unstable_blocks(images, cursor_169)
        check(not moving, f"Game Over > LOAD, 16:9: the presented images do not flicker (blocks that change: "
                          f"{moving[:6]})")
        h, w = images[0].shape[:2]
        side = max(float(i[int(h * 0.4):int(h * 0.8), int(w * 0.875):].mean()) for i in images)
        check(side < 4, f"Game Over > LOAD, 16:9: the view's right side below the menu rows is black "
                        f"(mean {side:.1f} < 4; the room's piece was there)")
    finally:
        game.close()


def run(args, out, card, images):
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
        # Command menu > DATA > LOAD (second) > MEMORY CARD slot 1.
        game.press("triangle", vsyncs=3, settle=60)
        top_command(game, symbols(), DATA_COMMAND)  # by the game's cursor (fast travel's Magic on a Circle)
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
    check_save_confirm(args, out, card, check)
    check_game_over_load(args, out, card, check)
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
