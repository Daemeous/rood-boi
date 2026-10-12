#!/usr/bin/env python3
"""Anti-aliasing: GPU cost and picture per method (pc/README.md "Anti-aliasing").

    python3 pc/tools/aa_bench.py --out DIR [--card pc/save/card1.mcd]
        [--window 1440x1080 --window 3840x2160] [--modes off,fxaa,smaa,msaa4,...]
        [--scale 4] [--vsyncs 300] [--light] [--crop x,y,w,h]

Runs the game (the renderer and GPU as the environment says: ROOD_RENDERER,
ROOD_VK_DEVICE, ROOD_EGL_DEVICE, ROOD_ARCH) into the save room from the
card (Continue, Ashley in view; --light adds a shadow-casting test light by
her), or the first room of New Game without a card. For each window size and
each `aa` mode:

  - cost: RPC perf over --vsyncs vsyncs of play (ROOD_PERF=1): the GPU time
    per presented frame, and on Vulkan its sections ("draw": the frame's
    drawing incl. MSAA's load and resolve; "aa": the post-process filter;
    "downsample": supersampling's filter; "present": lighting);
  - picture: the game paused and the same frame redrawn (RPC redraw) under
    every mode, written as DIR/<window>-<mode>.png, with a zoomed crop sheet
    of all modes (DIR/<window>-crops.jpg) around --crop (default: the view's
    centre, where the game camera keeps Ashley).

Prints a table of costs (ms) relative to `off`. The images contain game
imagery: keep them local (never commit them).
"""
import argparse
import os
import pathlib
import shutil
import sys
import tempfile
import time

import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import Game  # noqa: E402

DEFAULT_MODES = "off,fxaa,smaa,msaa2,msaa4,msaa8,msaa4+smaa,ssaa1.5,ssaa2"
FIRST_ROOM = 1500  # golden_frames.py: vsyncs after New Game


def capture(game, path, timeout=20):
    """The next presented image (works paused: the game re-presents)."""
    ppm = path.with_suffix(".ppm")
    ppm.unlink(missing_ok=True)
    game.call("present_dump", path=str(ppm))
    deadline = time.monotonic() + timeout
    while not (ppm.exists() and ppm.stat().st_size) and time.monotonic() < deadline:
        time.sleep(0.05)
    time.sleep(0.2)
    image = np.asarray(Image.open(ppm).convert("RGB"))
    ppm.unlink()
    return image


def crop_sheet(images, box, zoom, path):
    x, y, w, h = box
    tiles = []
    for name, image in images.items():
        tile = Image.fromarray(image[y:y + h, x:x + w]).resize((w * zoom, h * zoom), Image.NEAREST)
        canvas = Image.new("RGB", (tile.width, tile.height + 18), (16, 16, 16))
        canvas.paste(tile, (0, 18))
        ImageDraw.Draw(canvas).text((4, 3), name, fill=(255, 230, 0))
        tiles.append(canvas)
    columns = min(len(tiles), 5)
    rows = (len(tiles) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * (tiles[0].width + 4), rows * (tiles[0].height + 4)), (60, 60, 60))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, ((i % columns) * (tile.width + 4), (i // columns) * (tile.height + 4)))
    sheet.save(path, quality=90)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", required=True)
    parser.add_argument("--card", default=str(ROOT / "pc/save/card1.mcd"))
    parser.add_argument("--window", action="append")
    parser.add_argument("--modes", default=DEFAULT_MODES)
    parser.add_argument("--scale", type=int, default=4, help="internal resolution (supersampling raises it)")
    parser.add_argument("--vsyncs", type=int, default=300)
    parser.add_argument("--light", action="store_true", help="a shadow-casting test light by Ashley")
    parser.add_argument("--crop", help="x,y,w,h in window pixels (default: around the view's centre)")
    parser.add_argument("--zoom", type=int, default=4)
    parser.add_argument("--settings", nargs="*", default=[], help="extra graphics key=value (numbers, true/false)")
    parser.add_argument("--no-images", action="store_true")
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    modes = args.modes.split(",")
    extra = {}
    for item in args.settings:
        key, value = item.split("=", 1)
        extra[key] = (value == "true") if value in ("true", "false") else float(value) if "." in value else \
            int(value) if value.lstrip("-").isdigit() else value
    results = {}
    for window in args.window or ["1440x1080"]:
        lights = pathlib.Path(tempfile.mkdtemp(prefix="aa-lights-", dir=out))
        env = {"ROOD_WINDOW": window, "ROOD_LIGHTS": str(lights), "ROOD_SCALE": str(args.scale), "ROOD_PERF": "1",
               "ROOD_GI": os.environ.get("ROOD_GI", "1")}
        os.environ.pop("ROOD_SETTINGS", None)
        card = None
        if pathlib.Path(args.card).exists():
            card = str(out / "card1.mcd")
            shutil.copyfile(args.card, card)
        game = Game.launch(fast=False, card=card, log=out / f"game-{window}.log", extra_env=env,
                           socket_path=f"/tmp/aa-bench-{os.getpid()}.sock")
        try:
            game.wait_title()
            game.call("graphics", interpolate=True, **extra)
            if card:
                game.press("start", vsyncs=4, settle=90)
                for _ in range(3):  # Continue, the card, its first save
                    game.press("circle", vsyncs=3, settle=90)
                game.run(400)
                state = game.state()
                player = state.get("player")
                if args.light and player:
                    # light <type> x y z  r g b  intensity radius  dx dy dz cone  shadows enabled
                    (lights / f"zone{state['zone']:02d}_room{state['room']:02d}.txt").write_text(
                        f"light point {player['x'] + 180} {player['y'] - 260} {player['z'] - 120} 1 0.8 0.55 1.4 "
                        "1200 0 1 0 45 1 1\n")
                    for _ in range(4):
                        game.run(15)
                        time.sleep(0.25)
            else:
                game.press("start", vsyncs=4, settle=0)
                game.run(FIRST_ROOM)
            print(f"== {window}: {game.call('graphics')['lighting_preset']} preset, renderer log {game.log}",
                  flush=True)
            # Cost: play, per mode.
            for mode in modes:
                g = game.call("graphics", aa=mode)
                game.run(90)  # settle (supersampling resizes the targets)
                game.call("perf", reset=True)
                game.run(args.vsyncs)
                perf = game.call("perf")
                g = game.call("graphics")
                results[(window, mode)] = perf
                sections = perf.get("sections") or {}
                print(f"{mode:>12}: {perf['gpu_ms']:7.3f} ms/frame (max {perf['gpu_max_ms']:.2f}), scale {g['scale']}, "
                      f"msaa {g['msaa_active']}  " + " ".join(f"{k} {v:.3f}" for k, v in sections.items()), flush=True)
            if args.no_images:
                continue
            # Pictures: one frame per internal resolution, every mode at its own (supersampling
            # raises it; changing it drops the recorded frame, so modes are grouped by it). The
            # game pauses and the frame is redrawn under each mode.
            scales = {}
            for mode in modes:
                game.call("graphics", aa=mode)
                game.run(4)
                scales[mode] = game.call("graphics")["scale"]
            images = {}
            for scale in sorted(set(scales.values())):
                group = [m for m in modes if scales[m] == scale]
                game.call("graphics", aa=group[0])
                game.run(60)
                game.call("pause", paused=True)
                for mode in group:
                    game.call("graphics", aa=mode)
                    time.sleep(0.1)
                    game.call("redraw")
                    time.sleep(0.3)
                    images[mode] = capture(game, out / "capture.png")
                    Image.fromarray(images[mode]).save(out / f"{window}-{mode}.png")
                game.call("pause", paused=False)
            images = {mode: images[mode] for mode in modes}
            h, w = images[modes[0]].shape[:2]
            if args.crop:
                box = [int(v) for v in args.crop.split(",")]
            else:
                cw, ch = w // 8, h // 6
                box = [w // 2 - cw // 2, h // 2 - ch // 2, cw, ch]
            crop_sheet(images, box, args.zoom, out / f"{window}-crops.jpg")
        finally:
            game.close()
            shutil.rmtree(lights, ignore_errors=True)
        log = pathlib.Path(game.log).read_text(errors="replace")
        for line in log.splitlines():
            if "validation:" in line or "[gl] shader" in line or "[crash]" in line:
                print("LOG:", line)
    print("\nGPU ms per frame (and over off):")
    for window in args.window or ["1440x1080"]:
        base = results.get((window, "off"), {}).get("gpu_ms")
        for mode in modes:
            perf = results.get((window, mode))
            if perf:
                delta = f" (+{perf['gpu_ms'] - base:.3f})" if base is not None and mode != "off" else ""
                print(f"  {window} {mode:>12}: {perf['gpu_ms']:.3f}{delta}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
