#!/usr/bin/env python3
"""16:9: the field draws every pixel of the view every frame (no trails).

    python3 pc/tests/widescreen_undrawn_smoke.py [--out DIR]

The game never clears the frame buffer in the field (PutDrawEnv isbg 0): its
only clear is the background sphere (pc/src/boot_sdk.c
pc_background_packets), a gradient about the camera drawn as 2D behind the
set. In widescreen it was culled at the 4:3 sides and drawn compressed into
the middle (its triangles reaching past the top or bottom uncompressed, as
screen effects), so where no set reached - mostly the 16:9 sides - pixels
were never drawn and kept the picture drawn there two frames before: rain
streaks piled up and edges smeared as the camera moved ("trails").

Frame-exact check: the diagnostic ROOD_UNDRAWN_DEBUG (RPC graphics
undrawn_debug) fills each new drawing area magenta in the hardware target
first, so any pixel a frame does not draw is magenta in the presented image
(on interpolated images too: they start from the newest frame).

- New Game through the opening (the cell, the manor's exterior in the rain,
  the courtyard) at 16:9 with interpolation: a sample every 150 vsyncs, no
  magenta at all with the fix;
- the manor's exterior shot (zone 2 room 0) while the camera moves: RPC
  graphics widescreen_scene off is the control (the sphere's old placement)
  and leaves undrawn pixels, mostly in the side bands, alternately on the
  same shot.

4:3 is untouched (the sphere's widening and placement act only in
widescreen): pc/tests/golden_frames.py covers it. --out keeps frames (game
imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game  # noqa: E402

SAMPLE = 150       # vsyncs between samples
SHOT = (6300, 7200)  # vsyncs after New Game: the manor's exterior (zone 2 room 0)
BURST = 12         # consecutive presented images per sample on that shot


def magenta(image):
    """Undrawn pixels (the diagnostic's fill) in the whole view and in the side bands
    (the view's outer eighths, beyond the 4:3 middle)."""
    a = image.astype(np.int32)
    m = (a[:, :, 0] > 200) & (a[:, :, 1] < 60) & (a[:, :, 2] > 200)
    b = m.shape[1] // 8
    return int(m.sum()), int(m[:, :b].sum() + m[:, -b:].sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "widescreen_undrawn"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    env = {"ROOD_WINDOW": "640x360", "ROOD_SCALE": "1", "ROOD_GI": "0", "ROOD_WIDESCREEN": "1",
           "ROOD_UNDRAWN_DEBUG": "1", "ROOD_LIGHTS": str(out / "lights"), "ROOD_SETTINGS": str(out / "settings.cfg"),
           "ROOD_CARD1": str(out / "absent-card1.mcd")}
    game = Game.launch(fast=True, log=out / "game.log", extra_env=env,
                       socket_path=f"/tmp/rood-undrawn-{os.getpid()}.sock")

    def burst(name, count):
        """The next `count` presented images (as the window shows them, interpolated ones
        too) while the game runs; each image's undrawn pixels (total, in the sides)."""
        base = out / name
        game.call("present_dump", path=str(base), count=count)
        game.run(count + 6)
        paths = [pathlib.Path(f"{base}.{i:04d}.ppm") for i in range(count)]
        for _ in range(40):  # (written a few presents later, on the game thread)
            if all(p.exists() for p in paths):
                break
            game.run(1)
        counts = []
        for i, path in enumerate(paths):
            with open(path, "rb") as f:
                f.readline()
                w, h = map(int, f.readline().split())
                f.readline()
                image = np.frombuffer(f.read(), np.uint8).reshape(h, w, 3)
            path.unlink()
            counts.append(magenta(image))
            if counts[-1][0]:
                Image.fromarray(image).save(out / f"{name}-{i:02d}.png")
        return counts

    try:
        game.wait_title()
        game.call("graphics", aa=False, interpolate=True)
        game.press("start", vsyncs=4, settle=60)  # New Game (no card)
        start = game.status()["vsync"]
        holes, rooms, controls, k = [], set(), [], 0
        while game.status()["vsync"] - start < SHOT[1]:
            game.run(SAMPLE)
            k += 1
            at = game.status()["vsync"] - start
            st = game.state()
            rooms.add((st["zone"], st["room"]))
            shot = (st["zone"], st["room"]) == (2, 0) and at >= SHOT[0]
            if shot:
                # The control on the same shot (the sphere's old placement).
                game.call("graphics", widescreen_scene=False)
                game.run(12)  # (a frame built and shown with it: the cutscene runs at 15-30 fps)
                controls += burst(f"s{k:03d}-control", BURST)
                print(f"  vsync {at}: control up to {max(c[0] for c in controls[-BURST:])} undrawn pixels "
                      f"({max(c[1] for c in controls[-BURST:])} in the sides)")
                game.call("graphics", widescreen_scene=True)
                game.run(12)  # (a frame built and shown with it: the cutscene runs at 15-30 fps)
            for total, sides in burst(f"s{k:03d}", BURST if shot else 4):
                if total:
                    holes.append((at, st["zone"], st["room"], total, sides))
                    print(f"  vsync {at} zone {st['zone']} room {st['room']}: {total} undrawn pixels "
                          f"({sides} in the sides)")
            if k % 2 == 0:
                game.press("circle", vsyncs=2, settle=4)  # the dialogue
        check({(1, 0), (2, 0)} <= rooms, f"the opening reaches the manor's exterior (rooms {sorted(rooms)})")
        check(bool(controls) and max(c[1] for c in controls) > 100,
              "the control leaves pixels of the side bands undrawn on the manor's exterior (the measure is live)")
        check(not holes, f"every pixel of the 16:9 view is drawn every frame ({len(holes)} samples with holes)")
    finally:
        game.close()
    log = pathlib.Path(game.log).read_text(errors="replace")
    check("[crash]" not in log, "no crash")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS: 16:9 field frames draw the whole view (no undrawn side bands, no trails)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
