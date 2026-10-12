#!/usr/bin/env python3
"""Textured polygons sample only their own texels (pc/src/gpu_hw.c
pc_hw_set_flipped_uv and FLAG_CLAMP; pc/src/boot_polygons.inc).

The PS1 takes a pixel's texel at its top-left corner. A sprite drawn as a
polygon and flipped (U running from 87 down to 63 over 24 pixels, as the
game's "..." emote bubble does) therefore samples texels 87..64: sampled at
pixel centres it took 86..63, and texel 63 (the edge of the neighbouring
image in the page) showed as a thin bar beside the bubble, at every
internal scale and on both rasterizers.

Draws synthetic quads with RPC gpu_draw at the title screen (no game
imagery): a 15-bit texture whose image (texels 1..24 in U and V) is green,
surrounded by red; quads whose UVs span the image edge to edge, plain, flipped
in U, in V, in both, magnified 2x and minified 2:1. Checks:

- the CPU rasterizer (the reference VRAM, read through the vram symbol):
  every covered pixel is green where the PS1 samples inside the image;
- the hardware target (RPC gpu_dump) at internal scale 1 and 4: every covered
  pixel is green (no texel outside the image), coverage is the quad's;
- a sprite (GP0 0x65) of the same texels is unchanged.

    python3 pc/tests/flipped_sprite_smoke.py [--out DIR]
"""
import argparse
import os
import pathlib
import re
import shutil
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
from rood_rpc import OUT, Game, symbols  # noqa: E402

failures = []


def check(ok, what):
    print(("ok:   " if ok else "FAIL: ") + what)
    if not ok:
        failures.append(what)
    return ok


# Texture: a 32 x 32 15-bit page area at VRAM (768, 256); texels 1..24 (U and
# V) are the image (green), everything else red.
TEX_X, TEX_Y, TEX = 768, 256, 32
TPAGE = (TEX_X // 64) | (1 << 4) | (2 << 7)  # page 12, lower half, 15-bit
GREEN, RED, BLUE = 0x03E0, 0x001F, 0x7C00
# Drawing region: VRAM (640, 256) .. (768, 512) (unused at the title screen).
BASE_X, BASE_Y = 640, 256
# Packets carry coordinates relative to a draw offset far from any GTE
# projection (negative screen positions), so nothing reads them as 3D.
OFFSET_X, OFFSET_Y = BASE_X + 400, BASE_Y + 300


def texture_hex():
    words = []
    for v in range(TEX):
        for u in range(TEX):
            words.append(GREEN if 1 <= u <= 24 and 1 <= v <= 24 else RED)
    return "".join(f"{w & 255:02x}{w >> 8:02x}" for w in words)


def word_hex(words):
    return "".join(f"{w & 255:02x}{(w >> 8) & 255:02x}{(w >> 16) & 255:02x}{(w >> 24) & 255:02x}" for w in words)


def xy(x, y):
    x -= OFFSET_X - BASE_X
    y -= OFFSET_Y - BASE_Y
    return (x & 0xFFFF) | ((y & 0xFFFF) << 16)


def quad(x, y, w, h, u0, u1, v0, v1):
    """Raw-textured quad (GP0 0x2D) over x..x+w, y..y+h (region-relative)
    with U u0 at the left edge, u1 at the right; V v0 at the top, v1 at the
    bottom."""
    return [0x2D808080,
            xy(x, y), u0 | v0 << 8,
            xy(x + w, y), u1 | v0 << 8 | TPAGE << 16,
            xy(x, y + h), u0 | v1 << 8,
            xy(x + w, y + h), u1 | v1 << 8]


# (name, x, y, w, h, u0, u1, v0, v1, cpu): the image spans texels 1..24; a
# quad's far edge carries the coordinate one past the last texel it shows
# (plain: 1 -> 25), a flipped one runs from 24 down to 0 (the game's bubble:
# 87 -> 63). cpu: the corner sampling's result is inside the image (a
# magnified plain quad reaches 25 at its last pixel on the PS1 too).
QUADS = [
    ("plain", 4, 4, 24, 24, 1, 25, 1, 25, True),
    ("flipped U", 32, 4, 24, 24, 24, 0, 1, 25, True),
    ("flipped V", 60, 4, 24, 24, 1, 25, 24, 0, True),
    ("flipped U and V", 88, 4, 24, 24, 24, 0, 24, 0, True),
    ("flipped U, magnified 2x", 4, 32, 48, 24, 24, 0, 1, 25, True),
    ("plain, magnified 2x", 56, 32, 48, 24, 1, 25, 1, 25, False),
    ("flipped U, minified 2:1", 4, 60, 12, 24, 24, 0, 1, 25, True),
    ("flipped V, minified 2:1", 20, 60, 24, 12, 1, 25, 24, 0, True),
]
SPRITE = ("sprite", 52, 60, 24, 24)


def packet():
    words = [0x28FF0000, xy(0, 0), xy(128, 0), xy(0, 128), xy(128, 128)]  # blue backdrop
    for q in QUADS:
        words += quad(*q[1:9])
    name, x, y, w, h = SPRITE
    words += [0xE1000000 | TPAGE, 0x65808080, xy(x, y), 1 | 1 << 8, w | h << 16]
    return words


def draw(game):
    reply = game.call("gpu_draw", texture_x=TEX_X, texture_y=TEX_Y, texture_w=TEX, texture_h=TEX,
                      texture=texture_hex(), area_x=BASE_X, area_y=BASE_Y, area_w=128, area_h=256,
                      offset_x=OFFSET_X, offset_y=OFFSET_Y, packet=word_hex(packet()))
    return reply["words"]


def classify(rgb):
    """0 other, 1 green, 2 red, 3 blue (both 1x CPU pixels and 8-bit target pixels)."""
    r, g, b = (rgb[..., 0].astype(int), rgb[..., 1].astype(int), rgb[..., 2].astype(int))
    out = np.zeros(r.shape, np.uint8)
    out[(g > 200) & (r < 60) & (b < 60)] = 1
    out[(r > 200) & (g < 60) & (b < 60)] = 2
    out[(b > 200) & (r < 60) & (g < 60)] = 3
    return out


def cpu_region(game):
    base = symbols()["vram"]
    rows = []
    for y in range(BASE_Y, BASE_Y + 128):
        raw = game.read(base + (y * 1024 + BASE_X) * 2, 256)
        rows.append(np.frombuffer(raw, "<u2"))
    px = np.array(rows)
    rgb = np.stack([(px & 31) * 8, ((px >> 5) & 31) * 8, ((px >> 10) & 31) * 8], -1)
    return classify(rgb)


def target_region(game, path):
    game.call("gpu_dump", path=str(path))
    im = Image.open(path)
    scale = im.width // 1024
    crop = np.asarray(im.crop((BASE_X * scale, BASE_Y * scale, (BASE_X + 128) * scale, (BASE_Y + 128) * scale)))
    path.unlink()
    return scale, classify(crop)


def check_region(kinds, scale, label, cpu=False):
    for name, x, y, w, h, *_rest in QUADS + [SPRITE + (True,)]:
        if cpu and not _rest[-1]:
            continue
        inside = kinds[y * scale:(y + h) * scale, x * scale:(x + w) * scale]
        ring = kinds[max(0, y * scale - scale):(y + h + 1) * scale, max(0, x * scale - scale):(x + w + 1) * scale]
        red = int((inside == 2).sum())
        green = int((inside == 1).sum())
        check(red == 0 and green == inside.size and int((ring == 2).sum()) == 0,
              f"{label}: {name}: {green}/{inside.size} pixels sample the image, {red} outside it")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    out = pathlib.Path(args.out) if args.out else OUT / "flipped_sprite"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-flip-{os.getpid()}.sock"
    game = Game.launch(fast=True, log=out / "game.log", socket_path=socket,
                       extra_env={"ROOD_GI": "0", "ROOD_WINDOW": "640x480"})
    try:
        game.wait_title()
        game.run(30)
        for scale in (1, 4):
            game.call("graphics", scale=scale)
            game.run(4)  # the target is re-created at the new scale
            check(draw(game) == len(packet()), f"scale {scale}: gpu_draw drew the test packet")
            got, kinds = target_region(game, out / "target.ppm")
            check(got == scale, f"hardware target at internal scale {got}")
            check_region(kinds, got, f"hardware {got}x")
            if scale == 1:
                check_region(cpu_region(game), 1, "CPU rasterizer", cpu=True)
        log = pathlib.Path(game.log).read_text(errors="replace")
        check("[crash]" not in log and not re.search(r"shader.*(error|fail)", log, re.I),
              "no crash or shader error in the log")
    finally:
        game.close()
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
