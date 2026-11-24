#!/usr/bin/env python3
"""Decode the game's GIM pictures (data/GIM/*.GIM) to PNGs, from the player's own disc files (nothing from the game is kept in the repo:
the default output, pc/build/gim, is ignored).

    python3 pc/tools/gim_decode.py [--data data/GIM] [--out pc/build/gim] [NAME ...]

NAME is a file stem (JO, STAFF003, DEMO_001) or a glob (STAFF*); none: every file. One PNG per image of the file (a file holds one to three:
JO.png, JO_1.png ...), RGBA, the colours as the game's palette gives them, a palette entry of 0 transparent. What the files are (findings about
the pictures the game shows as text):

    JO_1            the intro quote ("The body is but a vessel for the soul ... A. J. Durai"), one image of eight lines
    VKP_0/1/2 _n    the Valendia Knights' report pages (Leá Monde's history, the survivors' report, the incident report)
    DEMO_001..008   the opening's briefing pages ("We have received a report ...", "It reads: Ashley Riot ...", ...), two images each
    STAFF001..013   the staff credit cards (title, then names), orange on black
    DEMOFIN _1 _2   the title card, the credits' closing card (the programmers' line, the copyright), "WINTER"
    EPILOGUE, EPI_E01B, EPI_INF, 1142, DADDY   short caption cards ("-- And so began the story of the wanderer, the Vagrant.", "11:42 A.M.", ...)
    ENKEI*, DEMOENK, HEADQUAT, JYO, SUNRISE, 019XXX01, N_LEA, LOGO_*, SQUARE, GAMEOVER, MAIN03   stills and logos

The format is the loader's (vs_render_updateGimImageSet, BATTLE 5BF94.c) and the tiled drawer's (vs_render_drawGimTiles): u16 words; up to
three image tables (word 0: columns | rows << 8, then columns * rows tile words, 4 header words, word 2 the palette mode), word 3 how many
tables follow the first; then (aligned to 2 words) the palettes, the pixel pages (64 halfwords wide, 17 tile rows of 15 lines each, word 1:
palette size in 16-word units << 8 | tile rows), and the tiles: a tile word picks u = (tile & 3) * 64, v = ((tile >> 2) & 31) * 15 and a
page ((tile >> 8) & 7) - 2 of the pixel pages; a tile is 64 x 15 texels. Palette mode 0: 4 bits, 16 colours per image; 1 and 2: 8 bits,
256 colours."""
import argparse
import pathlib
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]


def split(data):
    """The words, the image tables (columns, rows, tile words), the palette base, the pixel base, the page count and tile rows."""
    words = np.frombuffer(data[:len(data) // 2 * 2], dtype="<u2")
    tables, pos = [], 0
    for _ in range(1 + (1 if words[3] else 0) + (1 if words[3] == 2 else 0)):
        columns, rows = int(words[pos]) & 255, int(words[pos]) >> 8
        size = columns * rows + 4
        tables.append((columns, rows, words[pos:pos + size]))
        pos += size
    palette = (pos + 1) & ~1
    tile_rows = int(words[1]) & 255
    pixels = palette + (int(words[1]) >> 8) * 16
    return words, tables, palette, pixels, (tile_rows + 16) // 17, tile_rows


def colours(values):
    v = values.astype(np.uint32)
    rgb = np.stack([(v & 31), (v >> 5) & 31, (v >> 10) & 31], axis=-1) * 255 // 31
    return np.concatenate([rgb, np.where(values[..., None] == 0, 0, 255)], axis=-1).astype(np.uint8)


def decode(path):
    """The images of one GIM file as RGBA arrays."""
    words, tables, palette, pixels, pages, tile_rows = split(pathlib.Path(path).read_bytes())
    mode = int(tables[0][2][2])
    halfwords = np.zeros((255, pages * 64), dtype="<u2")
    for k in range(pages):
        rows = 255 if tile_rows - k * 17 >= 17 else (tile_rows - k * 17) * 15
        block = words[pixels + k * 0x3FC0: pixels + k * 0x3FC0 + 64 * rows]
        block = np.pad(block, (0, 64 * rows - block.size))
        halfwords[:rows, k * 64:(k + 1) * 64] = block.reshape(rows, 64)
    per = 4 if mode == 0 else 2
    texels = np.zeros((255, halfwords.shape[1] * per), dtype=np.uint8)
    for s in range(per):
        texels[:, s::per] = (halfwords >> (s * (16 // per))) & (15 if mode == 0 else 255)
    images = []
    for j, (columns, rows, table) in enumerate(tables):
        pal = colours(words[palette + j * 16: palette + j * 16 + 16] if mode == 0 else words[palette + 64: palette + 64 + 256])
        image = np.zeros((rows * 15, columns * 64, 4), dtype=np.uint8)
        grid = table[4:4 + columns * rows].reshape(rows, columns)
        for r in range(rows):
            for c in range(columns):
                tile = int(grid[r, c])
                if not tile:
                    continue
                u, v, page = (tile & 3) * 64, ((tile >> 2) & 31) * 15, ((tile >> 8) & 7) - 2
                x = page * 64 * per + u
                block = texels[v:v + 15, x:x + 64]
                if block.shape == (15, 64):
                    image[r * 15:(r + 1) * 15, c * 64:(c + 1) * 64] = pal[block]
        images.append(image)
    return images


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", default=str(ROOT / "data/GIM"))
    ap.add_argument("--out", default=str(ROOT / "pc/build/gim"))
    ap.add_argument("names", nargs="*", help="file stems or globs (default: every GIM)")
    args = ap.parse_args()
    src, dst = pathlib.Path(args.data), pathlib.Path(args.out)
    dst.mkdir(parents=True, exist_ok=True)
    files = sorted({f for n in (args.names or ["*"]) for f in src.glob(n if n.upper().endswith(".GIM") else n + ".GIM")})
    if not files:
        print("no GIM files in", src, file=sys.stderr)
        return 1
    for f in files:
        images = decode(f)
        for j, image in enumerate(images):
            Image.fromarray(image, "RGBA").save(dst / f"{f.stem}{'' if j == 0 else f'_{j}'}.png")
        print(f"{f.name}: {len(images)} image(s) {images[0].shape[1]}x{images[0].shape[0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
