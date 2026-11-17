#!/usr/bin/env python3
"""The event film's scanlines cover the 16:9 view, not only its 4:3 part (pc/src/boot_lines.inc, gpu_hw.c pc_hw_set_bounds).

    python3 pc/tests/scanline_sides_smoke.py [--out DIR]

The scanline overlay (vs_event_isScanlineOverlayEnabled: EVT 6 and 15 turn it on; the opening's flashbacks) is a 320 pixel
wide line per shaded row. A line was compressed to the 4:3 part like other 2D, so the view's sides had none: a hard seam where
the stripes stopped. A primitive that stands for the screen's width (a polygon, a rectangle) already spans the view
(pc_hw_set_bounds); lines now go through the same decision.

Checks, in the opening (from the title, New Game; a picture with light in the sides), the flag forced on as EVT 6 and 15 do,
in a 1280 x 720 window with the widescreen picture:
 - the stripes (a spectrum peak at 112 cycles per frame: the overlay shades every other line of 224) show in the 4:3 middle
   (a positive control: the effect draws at all);
 - the same in each lit side band, at least 0.4 of the middle's strength and clear of the scene's own detail: the sides carry
   the pattern too;
 - 4:3 (widescreen off): the stripes show.
--out keeps frames for looking (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import sys

import numpy as np
from PIL import Image

os.environ["ROOD_TEST_SNAPSHOTS"] = "0"  # (New Game with no memory card: the checkpoint is booted, a snapshot is keyed by a card file)
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

failures = []


def check(ok, message):
    print(("PASS: " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def present(game, out, name):
    game.wait_presents(3)
    path = out / f"{name}.png"
    game.present_screenshot(path)
    image = np.asarray(Image.open(path).convert("RGB")).astype(float)
    if not out.joinpath("keep").exists():
        path.unlink()
    return image


def stripes(image, columns):
    """How strongly the rows of `columns` alternate every other row of the 224 line picture (the overlay's period): the largest
    spectrum peak of the column-averaged brightness (rows 10 % to 90 % down, the slow part removed) between 40 and 240 cycles per
    frame height, over the median of those. A scene's own detail peaks at 10 to 25; the overlay at 40 to 170 (always at 112)."""
    h = image.shape[0]
    y = image[int(h * 0.1):int(h * 0.9), columns].mean(axis=(1, 2))
    y = y - np.convolve(y, np.ones(15) / 15, mode="same")
    spectrum = np.abs(np.fft.rfft(y * np.hanning(len(y))))
    cycles = np.arange(len(spectrum)) / (len(y) / h)
    band = (cycles > 40) & (cycles < 240)
    return float(spectrum[band].max() / np.median(spectrum[band])), float(cycles[band][spectrum[band].argmax()])


STEPS = (44, 46, 48)  # the opening played this far (30 vsyncs, Circle each), the overlay forced on from step 30 to 40 as EVT 6 and 15 do


def take(widescreen, out, label):
    env = {"ROOD_WINDOW": "1280x720" if widescreen else "960x720", "ROOD_GI": "0", "ROOD_LIGHTS": str(out / f"lights-{label}")}
    if widescreen:
        env["ROOD_WIDESCREEN"] = "1"
    game = launch_at("title", card=str(out / "no-card.mcd"), fast=True, log=out / f"scanline-{label}.log", extra_env=env)
    try:
        game.call("graphics", widescreen=widescreen, interpolate=False)
        game.run(60)
        game.press("start", vsyncs=4, settle=90)
        game.press("circle", vsyncs=3, settle=0)
        shots = []
        for step in range(max(STEPS) + 1):
            if 30 <= step < 40:
                game.store("vs_event_isScanlineOverlayEnabled", 1)
            game.run(30)
            game.press("circle", vsyncs=2, settle=4)
            if step in STEPS:
                shots.append(present(game, out, f"{label}-{step}"))
        return shots
    finally:
        game.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", help="keep the frames here")
    args = parser.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "scanline_sides"
    out.mkdir(parents=True, exist_ok=True)
    if args.out:
        (out / "keep").mkdir(exist_ok=True)
    wide = take(True, out, "wide")
    w = wide[0].shape[1]
    bands = {"left": slice(0, w // 6), "middle": slice(w // 4, w - w // 4), "right": slice(w - w // 6, w)}
    measured = [{name: stripes(image, cols) for name, cols in bands.items()} for image in wide]
    brightness = [{name: float(image[:, cols].mean()) for name, cols in bands.items()} for image in wide]
    for index, (m, b) in enumerate(zip(measured, brightness)):
        print(f"  frame {index}: " + ", ".join(f"{n} {m[n][0]:.0f} (luma {b[n]:.0f})" for n in bands))
    check(all(m["middle"][0] > 40 and m["middle"][1] == m["middle"][1] and abs(m["middle"][1] - 112) < 3 for m in measured),
          "16:9: the stripes show in the 4:3 middle (peak 40 or more over the median, at 112 cycles)")
    for name in ("left", "right"):
        lit = [(m[name][0], m["middle"][0]) for m, b in zip(measured, brightness) if b[name] > 6]
        check(len(lit) >= 1, f"16:9: the {name} side is lit in a frame of the opening (luma over 6)")
        check(all(side > 30 and side > 0.4 * middle for side, middle in lit),
              f"16:9: the {name} side carries the stripes in each lit frame ({[round(side) for side, _ in lit]} against the middle's "
              f"{[round(mid) for _, mid in lit]})")
    narrow = take(False, out, "narrow")
    nw = narrow[0].shape[1]
    peaks = [stripes(image, slice(nw // 8, nw - nw // 8)) for image in narrow]
    check(all(p[0] > 40 and abs(p[1] - 112) < 3 for p in peaks), f"4:3: the stripes show ({[round(p[0]) for p in peaks]})")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
