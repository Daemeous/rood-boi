#!/usr/bin/env python3
"""The title's fade-out after Continue / New Game matches the original frame
by frame (no black box over the menu panel, no backdrop left unfaded).

    python3 pc/tests/title_fade_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

TITLE vs_title_exec leaves the title by drawing a full-screen subtractive
tile (8, 8, 8) 64 times over a picture it no longer redraws, so everything
on screen darkens together. The hardware renderer recognises that tile as
a screen fade (gpu_hw.c BLEND_FADE: it subtracts from the 2D under it, and
the presenter fades the lit 3D after lighting). The title's backdrop is no
2D draw but an uploaded image (LoadImage over a ClearImage: far depth), so
in 4:3 the fade skipped it: the backdrop kept its full brightness while the
menu panel, redrawn as 2D sprites every menu frame, went black (a black
rectangle under the fading CONTINUE).

Frame-exact check, from the title checkpoint (Start on the default item:
Continue with a card, New Game without), every few vsyncs through the fade:
the presented image against the software rasteriser's display
(RPC screenshot: the CPU VRAM, as the PS1 shows it), in blocks of 1/16 of
the picture height. No block may be black where the original is lit (the
box), nor lit where the original has faded (the unfaded backdrop).
  - 4:3 with the user's anti-aliasing (MSAA 8 + FXAA): the case that broke;
  - 16:9: the picture is laid out differently from the 4:3 original, so its
    overall brightness must fall as the original's does (mean luminance
    relative to the title frame, within 0.1).
--out keeps frames (game imagery: keep it out of git)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
import rood_rpc  # noqa: E402
from rood_rpc import OUT  # noqa: E402
from checkpoints import launch_at  # noqa: E402

STEPS = (1, 2, 4, 6, 9, 12, 16, 20, 25, 30, 36, 44)  # vsyncs after the press (the fade: 64)
BLOCKS = 16  # blocks per picture height


def load(path):
    return np.asarray(Image.open(path).convert("L"), dtype=np.float32)


def compare_43(hw, ref):
    """(blocks black where the original is lit, blocks lit where it is not)."""
    h, w = hw.shape
    r = np.asarray(Image.fromarray(ref).resize((w, h), Image.BILINEAR), dtype=np.float32)
    bs = h // BLOCKS
    dark = bright = 0
    for y in range(0, h - bs + 1, bs):
        for x in range(0, w - bs + 1, bs):
            rb, hb = r[y:y + bs, x:x + bs].mean(), hw[y:y + bs, x:x + bs].mean()
            dark += rb > 20 and hb < rb * 0.25
            bright += hb > rb * 2 + 10
    return dark, bright


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default=str(OUT / "test-card1.mcd"))
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "title_fade"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = pathlib.Path(args.card)
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)

    def run_case(name, widescreen, window, settings):
        work = out / name
        work.mkdir()
        (work / "settings.cfg").write_text("".join(f"{k} {v}\n" for k, v in settings.items()))
        cards = []
        if card.exists():
            cards = [work / "card.mcd"]
            shutil.copyfile(card, cards[0])
        env = {"ROOD_WINDOW": window, "ROOD_GI": "0", "ROOD_LIGHTS": str(work / "lights"),
               "ROOD_SETTINGS": str(work / "settings.cfg"), "ROOD_OUT": str(work)}
        game = launch_at("title", card=cards[0] if cards else None, log=work / "game.log", extra_env=env,
                         fast=True, socket_path=f"/tmp/rood-titlefade-{os.getpid()}.sock")
        frames = []
        try:
            game.call("graphics", widescreen=widescreen, interpolate=False)
            game.run(8)

            def capture(label):
                ppm = work / f"{label}.ppm"
                ppm.unlink(missing_ok=True)
                game.call("present_dump", path=str(ppm))
                if not rood_rpc.wait_for_file(ppm, 20):
                    raise rood_rpc.RpcError("no frame presented")
                hw = load(ppm)
                ppm.unlink()
                ref_ppm = work / f"{label}-ref.ppm"
                game.call("screenshot", path=str(ref_ppm.resolve()))
                ref = np.asarray(Image.open(ref_ppm).convert("L"))
                ref_ppm.unlink()
                if args.out:
                    Image.fromarray(hw.astype(np.uint8)).save(work / f"{label}.png")
                    Image.fromarray(ref).save(work / f"{label}-ref.png")
                frames.append((label, hw, ref))

            capture("title")
            game.call("input", buttons=["start"], vsyncs=4)
            start = game.status()["vsync"]
            for step in STEPS:
                game.run(start + step - game.status()["vsync"])
                capture(f"v{step:02d}")
        finally:
            game.close()
        log = pathlib.Path(game.log).read_text(errors="replace")
        check("[crash]" not in log, f"{name}: no crash")
        return frames

    # 4:3, the user's settings that showed the black box.
    frames = run_case("4x3", False, "960x720",
                      {"widescreen": 0, "internal_resolution": 2, "aa": "msaa8+fxaa", "post_stack": 1})
    worst = []
    for label, hw, ref in frames:
        dark, bright = compare_43(hw, ref)
        worst.append((dark, bright, label))
        print(f"  4:3 {label}: {dark} blocks black where the original is lit, {bright} lit where it has faded")
    check(max(w[0] for w in worst) <= 2,
          f"4:3: no black box over the menu (worst {max(worst)[2]}: {max(w[0] for w in worst)} blocks)")
    check(max(w[1] for w in worst) <= 2,
          f"4:3: the backdrop fades with the rest (worst {max(worst, key=lambda w: w[1])[2]}: "
          f"{max(w[1] for w in worst)} blocks lit where the original has faded)")
    # The fade is live: the last frames are much darker than the title.
    check(frames[-1][1].mean() < frames[0][1].mean() * 0.25,
          f"4:3: the picture fades (mean {frames[0][1].mean():.1f} -> {frames[-1][1].mean():.1f})")

    # 16:9: the overall brightness follows the original's.
    frames = run_case("16x9", True, "1280x720", {"widescreen": 1, "internal_resolution": 2})
    hw0, ref0 = frames[0][1].mean(), frames[0][2].mean()
    gaps = []
    for label, hw, ref in frames[1:]:
        gap = abs(hw.mean() / hw0 - ref.mean() / ref0)
        gaps.append((gap, label))
        print(f"  16:9 {label}: brightness {hw.mean() / hw0:.3f} of the title (original {ref.mean() / ref0:.3f})")
    check(max(gaps)[0] <= 0.1, f"16:9: the picture fades as the original does (worst {max(gaps)[1]}: "
          f"{max(gaps)[0]:.3f} apart)")

    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS: the title fades out as the original (4:3 frame-exact, 16:9 brightness)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
